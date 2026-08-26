[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$ServicePort = 19198,
    [ValidateRange(1024, 65535)]
    [int]$AdminPort = 19199
)

# Scenario: mock-service-ingress-backpressure-v1
#
# The scenario uses a disposable database and resource copy.  Four peers send
# a complete CBMS header whose declared body never arrives.  A fifth peer then
# sends the normal CBMS ping frame.  The ping is a real service transport
# contract, but it is deliberately stateless: this test verifies admission and
# worker availability rather than fabricating a gameplay transition.

$ErrorActionPreference = 'Stop'
$ScenarioId = 'mock-service-ingress-backpressure-v1'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
$resourceRoot = Join-Path $repo 'web\fs\JHOnlineData'
$php = $env:CBE_AUTOMATION_PHP
$runId = "$ScenarioId-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$PID"
$runDir = Join-Path $repo "artifacts\automation\$runId"
$resourceCopy = Join-Path $runDir 'server-resource'
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
$serverProcess = $null
$partialClients = @()
$oldEnvironment = @{}

function Assert-Path([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Description not found: $Path" }
}
function Test-ListeningPort([int]$Port) {
    return @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue).Count -ne 0
}
function Wait-OwnedService([Diagnostics.Process]$Process, [int]$Port) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) { throw "isolated service exited with code $($Process.ExitCode)" }
        if (Test-ListeningPort $Port) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "isolated service did not listen on 127.0.0.1:$Port"
}
function Stop-OwnedProcess([Diagnostics.Process]$Process) {
    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -ErrorAction Stop
        $Process.WaitForExit(5000) | Out-Null
    }
}
function Read-Exactly([Net.Sockets.NetworkStream]$Stream, [int]$Length) {
    [byte[]]$buffer = New-Object byte[] $Length
    $offset = 0
    while ($offset -lt $Length) {
        $read = $Stream.Read($buffer, $offset, $Length - $offset)
        if ($read -le 0) { throw "service closed connection while reading response" }
        $offset += $read
    }
    return $buffer
}
function New-CbmsHeader([uint32]$Flags, [uint32]$BodyLength, [uint32]$Aux) {
    $stream = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter($stream)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('CBMS'))
    $writer.Write([uint32]1)
    $writer.Write($Flags)
    $writer.Write($BodyLength)
    $writer.Write($Aux)
    $writer.Flush()
    [byte[]]$frame = $stream.ToArray()
    $writer.Dispose()
    $stream.Dispose()
    return $frame
}
function Open-IncompleteFrame([int]$Port) {
    $client = New-Object Net.Sockets.TcpClient
    $client.ReceiveTimeout = 5000
    $client.SendTimeout = 5000
    $client.Connect('127.0.0.1', $Port)
    # Declares the four-byte request metadata, but never transmits it.
    [byte[]]$header = New-CbmsHeader 0 4 4
    $client.GetStream().Write($header, 0, $header.Length)
    return $client
}
function Open-IncompleteAdminRequest([int]$Port) {
    $client = New-Object Net.Sockets.TcpClient
    $client.ReceiveTimeout = 5000
    $client.SendTimeout = 5000
    $client.Connect('127.0.0.1', $Port)
    [byte[]]$header = [Text.Encoding]::ASCII.GetBytes("GET / HTTP/1.1`r`nHost: regression`r`n")
    $client.GetStream().Write($header, 0, $header.Length)
    return $client
}
function Write-Be16([IO.BinaryWriter]$Writer, [int]$Value) {
    $Writer.Write([byte](($Value -shr 8) -band 0xff))
    $Writer.Write([byte]($Value -band 0xff))
}
function Add-StringField([IO.BinaryWriter]$Writer, [string]$Name, [string]$Value) {
    $nameBytes = [Text.Encoding]::ASCII.GetBytes($Name)
    $valueBytes = [Text.Encoding]::ASCII.GetBytes($Value)
    $Writer.Write([byte]$nameBytes.Length)
    $Writer.Write($nameBytes)
    Write-Be16 $Writer $valueBytes.Length
    $Writer.Write($valueBytes)
}
function New-LoginFrame {
    $payloadStream = New-Object IO.MemoryStream
    $payloadWriter = New-Object IO.BinaryWriter($payloadStream)
    foreach ($field in @(
        @('coreVer', '1'), @('appVer', '1'), @('imsi', 'ingress-regression'),
        @('username', 'guest00001'), @('password', 'automation-only')
    )) { Add-StringField $payloadWriter $field[0] $field[1] }
    $payloadWriter.Flush()
    [byte[]]$payload = $payloadStream.ToArray()
    $payloadWriter.Dispose()
    $payloadStream.Dispose()

    $objectStream = New-Object IO.MemoryStream
    $objectWriter = New-Object IO.BinaryWriter($objectStream)
    $objectWriter.Write([byte]1)
    $objectWriter.Write([byte]1)
    $objectWriter.Write([byte]12)
    Write-Be16 $objectWriter (5 + $payload.Length)
    $objectWriter.Write($payload)
    $objectWriter.Flush()
    [byte[]]$object = $objectStream.ToArray()
    $objectWriter.Dispose()
    $objectStream.Dispose()

    $wtStream = New-Object IO.MemoryStream
    $wtWriter = New-Object IO.BinaryWriter($wtStream)
    $wtWriter.Write([Text.Encoding]::ASCII.GetBytes('WT'))
    Write-Be16 $wtWriter (4 + $object.Length)
    $wtWriter.Write($object)
    $wtWriter.Flush()
    [byte[]]$wt = $wtStream.ToArray()
    $wtWriter.Dispose()
    $wtStream.Dispose()

    $requestStream = New-Object IO.MemoryStream
    $requestWriter = New-Object IO.BinaryWriter($requestStream)
    $requestWriter.Write([uint32]0x7a225154)
    $requestWriter.Write($wt)
    $requestWriter.Flush()
    [byte[]]$request = $requestStream.ToArray()
    $requestWriter.Dispose()
    $requestStream.Dispose()

    $frameStream = New-Object IO.MemoryStream
    $frameWriter = New-Object IO.BinaryWriter($frameStream)
    $frameWriter.Write((New-CbmsHeader 0 $request.Length 4))
    $frameWriter.Write($request)
    $frameWriter.Flush()
    [byte[]]$frame = $frameStream.ToArray()
    $frameWriter.Dispose()
    $frameStream.Dispose()
    return $frame
}
function Invoke-ServicePing([int]$Port) {
    $client = New-Object Net.Sockets.TcpClient
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $client.ReceiveTimeout = 1500
        $client.SendTimeout = 1500
        $client.Connect('127.0.0.1', $Port)
        [byte[]]$frame = New-CbmsHeader 1 0 0
        $stream = $client.GetStream()
        $stream.Write($frame, 0, $frame.Length)
        [byte[]]$response = Read-Exactly $stream 20
        $stopwatch.Stop()
        if ([Text.Encoding]::ASCII.GetString($response, 0, 4) -ne 'CBMR') {
            throw 'ping response did not use CBMR'
        }
        if ([BitConverter]::ToUInt32($response, 12) -ne 0) {
            throw 'ping response unexpectedly contained a body'
        }
        return [uint32]$stopwatch.ElapsedMilliseconds
    } finally {
        if ($stopwatch.IsRunning) { $stopwatch.Stop() }
        $client.Dispose()
    }
}
function Invoke-ServiceNonCanonicalPing([int]$Port) {
    $client = New-Object Net.Sockets.TcpClient
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $client.ReceiveTimeout = 1500
        $client.SendTimeout = 1500
        $client.Connect('127.0.0.1', $Port)
        # The legacy handler treats every PING-bit frame as a stateless empty
        # CBMR probe, even when another transport flag is present.
        [byte[]]$frame = New-CbmsHeader 3 0 0
        $stream = $client.GetStream()
        $stream.Write($frame, 0, $frame.Length)
        [byte[]]$response = Read-Exactly $stream 20
        $stopwatch.Stop()
        if ([Text.Encoding]::ASCII.GetString($response, 0, 4) -ne 'CBMR') {
            throw 'non-canonical ping response did not use CBMR'
        }
        if ([BitConverter]::ToUInt32($response, 12) -ne 0) {
            throw 'non-canonical ping response unexpectedly contained a body'
        }
        return [uint32]$stopwatch.ElapsedMilliseconds
    } finally {
        if ($stopwatch.IsRunning) { $stopwatch.Stop() }
        $client.Dispose()
    }
}
function Invoke-ServiceLogin([int]$Port) {
    $client = New-Object Net.Sockets.TcpClient
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $client.ReceiveTimeout = 1500
        $client.SendTimeout = 1500
        $client.Connect('127.0.0.1', $Port)
        [byte[]]$frame = New-LoginFrame
        $stream = $client.GetStream()
        $stream.Write($frame, 0, $frame.Length)
        [byte[]]$response = Read-Exactly $stream 20
        $stopwatch.Stop()
        if ([Text.Encoding]::ASCII.GetString($response, 0, 4) -ne 'CBMR') {
            throw 'login response did not use CBMR'
        }
        $responseLength = [BitConverter]::ToUInt32($response, 12)
        if ($responseLength -eq 0) { throw 'login request received an empty service response' }
        $null = Read-Exactly $stream ([int]$responseLength)
        return [uint32]$stopwatch.ElapsedMilliseconds
    } finally {
        if ($stopwatch.IsRunning) { $stopwatch.Stop() }
        $client.Dispose()
    }
}

Assert-Path $server 'server binary'
Assert-Path $fixture 'automation database fixture'
Assert-Path $resourceRoot 'server resource root'
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
if ($ServicePort -eq $AdminPort) { throw 'service and admin ports must differ' }
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) { throw "refusing occupied automation port $port" }
}

try {
    New-Item -ItemType Directory -Path $runDir -ErrorAction Stop | Out-Null
    Copy-Item -LiteralPath $resourceRoot -Destination $resourceCopy -Recurse
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database create failed' }

    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT','CBE_MOCK_VERBOSE_LOG')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_MYSQL_HOST = '127.0.0.1'
    $env:CBE_MYSQL_PORT = '3306'
    $env:CBE_MYSQL_USER = 'root'
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $resourceCopy
    $env:CBE_MOCK_VERBOSE_LOG = '1'
    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru -WindowStyle Hidden `
        -ArgumentList '--mock-service-only', '--mock-service-bind=127.0.0.1', "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$resourceCopy" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    [pscustomobject]@{ scenario = $ScenarioId; max_steps = 8; total_timeout_seconds = 30; single_step_timeout_seconds = 3; server_pid = $serverProcess.Id; database = $database } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'run.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort

    for ($i = 0; $i -lt 4; ++$i) { $partialClients += Open-IncompleteFrame $ServicePort }
    # The fifth same-source incomplete connection must be reclaimed early;
    # it must not prevent a well-formed frame from the same NAT address.
    $partialClients += Open-IncompleteFrame $ServicePort
    for ($i = 0; $i -lt 4; ++$i) { $partialClients += Open-IncompleteAdminRequest $AdminPort }
    # Input pacing only: give the listener one select cycle to register all
    # four incomplete frames before the real ping is sent.
    Start-Sleep -Milliseconds 300
    $pingMs = Invoke-ServicePing $ServicePort
    if ($pingMs -gt 1500) { throw "valid ping waited $pingMs ms behind incomplete frames" }
    $nonCanonicalPingMs = Invoke-ServiceNonCanonicalPing $ServicePort
    if ($nonCanonicalPingMs -gt 1500) { throw "non-canonical ping waited $nonCanonicalPingMs ms behind incomplete frames" }
    $loginMs = Invoke-ServiceLogin $ServicePort
    if ($loginMs -gt 1500) { throw "valid login waited $loginMs ms behind incomplete admin frames" }

    Start-Sleep -Milliseconds 2300
    $serverLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'server.stdout.log')
    $timeoutCount = [regex]::Matches($serverLog, 'ingress_drop .*reason=frame-timeout').Count
    if ($timeoutCount -lt 4) { throw "expected four ingress frame timeouts, found $timeoutCount" }
    if ($serverLog -notmatch 'ingress_drop .*reason=source-pending-cap .*source=127\.0\.0\.1') {
        throw 'fifth same-source incomplete frame was not reclaimed at ingress'
    }
    if ($serverLog -notmatch 'ingress_ping .*flags=1 ') { throw 'canonical ping did not use the ingress fast path' }
    if ($serverLog -notmatch 'ingress_ping .*flags=3 ') { throw 'non-canonical ping did not use the ingress fast path' }
    [pscustomobject]@{ result = 'passed'; scenario = $ScenarioId; ping_ms = $pingMs; noncanonical_ping_ms = $nonCanonicalPingMs; login_ms = $loginMs; ingress_timeouts = $timeoutCount; assertions = @('four-incomplete-game-frames-never-enter-protocol-worker-pool','same-source-fifth-incomplete-frame-reclaimed-before-timeout','four-incomplete-admin-frames-never-starve-game-worker-pool','all-ping-bit-frames-receive-cbmr-without-protocol-worker','valid-login-received-cbmr-under-1500ms','incomplete-game-frames-reclaimed-at-ingress') } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'result.json') -Encoding utf8
    Write-Host "$ScenarioId passed run_dir=$runDir ping_ms=$pingMs noncanonical_ping_ms=$nonCanonicalPingMs login_ms=$loginMs ingress_timeouts=$timeoutCount"
}
finally {
    foreach ($client in $partialClients) { try { $client.Dispose() } catch {} }
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
    if ($database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append } catch { Write-Warning "could not remove isolated database: $_" }
    }
}
