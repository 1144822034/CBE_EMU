[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$ServicePort = 19196,
    [ValidateRange(1024, 65535)]
    [int]$AdminPort = 19197,
    [ValidateRange(121, 600)]
    [int]$ObservationSeconds = 125,
    [switch]$KeepDatabase
)

# Scenario: mysql-worker-keepalive-v1
#
# This is a service transport regression, not a client/gameplay shortcut.  It
# starts only an isolated service and database schema, sends one protocol-valid
# 1/1/12 login request to make exactly one connection worker open its normal
# thread-local MySQL socket, and then observes MySQL processlist after the
# configured server wait_timeout has elapsed.  The sender does not alter guest
# RAM, response bytes, server state, or the user's service/database.

$ErrorActionPreference = 'Stop'
$ScenarioId = 'mysql-worker-keepalive-v1'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
$resourceRoot = Join-Path $repo 'web\fs\JHOnlineData'
$mysql = 'D:\phpstudy_pro\Extensions\MySQL5.7.26\bin\mysql.exe'
$maxSteps = 6
$singleStepTimeoutSeconds = 15

function Assert-Path([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Description not found: $Path" }
}
function Test-ListeningPort([int]$Port) {
    return @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue).Count -ne 0
}
function Wait-OwnedService([Diagnostics.Process]$Process, [int]$Port, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) { throw "isolated service exited early with code $($Process.ExitCode)" }
        if (Test-ListeningPort $Port) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "isolated service did not listen on 127.0.0.1:$Port within $TimeoutSeconds seconds"
}
function Stop-OwnedProcess([Diagnostics.Process]$Process) {
    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -ErrorAction Stop
        $Process.WaitForExit(5000) | Out-Null
    }
}
function Write-Be16([IO.BinaryWriter]$Writer, [int]$Value) {
    $Writer.Write([byte](($Value -shr 8) -band 0xff))
    $Writer.Write([byte]($Value -band 0xff))
}
function Read-Exactly([Net.Sockets.NetworkStream]$Stream, [int]$Length) {
    [byte[]]$buffer = New-Object byte[] $Length
    $at = 0
    while ($at -lt $Length) {
        $got = $Stream.Read($buffer, $at, $Length - $at)
        if ($got -le 0) { throw "service closed connection while reading $Length-byte response" }
        $at += $got
    }
    return $buffer
}
function Add-StringField([IO.BinaryWriter]$Writer, [string]$Name, [string]$Value) {
    $nameBytes = [Text.Encoding]::ASCII.GetBytes($Name)
    $valueBytes = [Text.Encoding]::ASCII.GetBytes($Value)
    if ($nameBytes.Length -gt 255 -or $valueBytes.Length -gt 65535) {
        throw "test field is outside WT string bounds: $Name"
    }
    $Writer.Write([byte]$nameBytes.Length)
    $Writer.Write($nameBytes)
    Write-Be16 $Writer $valueBytes.Length
    $Writer.Write($valueBytes)
}
function New-LoginPayload {
    $payloadStream = New-Object IO.MemoryStream
    $payloadWriter = New-Object IO.BinaryWriter($payloadStream)
    foreach ($field in @(
        @('coreVer', '1'), @('appVer', '1'), @('imsi', 'automation-keepalive'),
        @('username', 'guest00001'), @('password', 'automation-only')
    )) {
        Add-StringField $payloadWriter $field[0] $field[1]
    }
    $payloadWriter.Flush()
    [byte[]]$payload = $payloadStream.ToArray()
    $payloadWriter.Dispose()
    $payloadStream.Dispose()

    $objectStream = New-Object IO.MemoryStream
    $objectWriter = New-Object IO.BinaryWriter($objectStream)
    $objectWriter.Write([byte]1) # WT major
    $objectWriter.Write([byte]1) # kind
    $objectWriter.Write([byte]12) # login validation subtype
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
    return $wt
}
function Invoke-LoginQuery([int]$Port) {
    [byte[]]$wt = New-LoginPayload
    $requestStream = New-Object IO.MemoryStream
    $requestWriter = New-Object IO.BinaryWriter($requestStream)
    $requestWriter.Write([uint32]0x7a225154) # deterministic test client id, little endian
    $requestWriter.Write($wt)
    $requestWriter.Flush()
    [byte[]]$request = $requestStream.ToArray()
    $requestWriter.Dispose()
    $requestStream.Dispose()

    $frameStream = New-Object IO.MemoryStream
    $frameWriter = New-Object IO.BinaryWriter($frameStream)
    $frameWriter.Write([Text.Encoding]::ASCII.GetBytes('CBMS'))
    $frameWriter.Write([uint32]1)
    $frameWriter.Write([uint32]0)
    $frameWriter.Write([uint32]$request.Length)
    $frameWriter.Write([uint32]4)
    $frameWriter.Write($request)
    $frameWriter.Flush()
    [byte[]]$frame = $frameStream.ToArray()
    $frameWriter.Dispose()
    $frameStream.Dispose()

    $client = New-Object Net.Sockets.TcpClient
    try {
        $client.ReceiveTimeout = $singleStepTimeoutSeconds * 1000
        $client.SendTimeout = $singleStepTimeoutSeconds * 1000
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        $stream.Write($frame, 0, $frame.Length)
        $header = Read-Exactly $stream 20
        if ([Text.Encoding]::ASCII.GetString($header, 0, 4) -ne 'CBMR') {
            throw 'login response did not use CBMR frame contract'
        }
        $responseLength = [BitConverter]::ToUInt32($header, 12)
        if ($responseLength -eq 0) { throw 'login query received an empty service response' }
        $null = Read-Exactly $stream ([int]$responseLength)
        return [pscustomobject]@{ request_bytes = $request.Length; response_bytes = $responseLength }
    } finally {
        $client.Dispose()
    }
}
function Get-ServiceMySqlRows([string]$Database, [string]$OutputPath) {
    $sql = "SELECT ID,USER,DB,COMMAND,TIME FROM information_schema.PROCESSLIST WHERE DB='$Database' AND USER='root' ORDER BY ID"
    # mysql.exe writes a warning whenever a password is supplied as an argv
    # option.  MYSQL_PWD avoids putting the secret in the spawned command line
    # and keeps this machine-readable processlist probe free of false stderr
    # failures.  It is scoped to this isolated runner process and restored in
    # the outer finally block.
    $env:MYSQL_PWD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $rows = & $mysql '--protocol=TCP' '--host=127.0.0.1' '--port=3306' '--user=root' '--batch' '--skip-column-names' "--execute=$sql"
    $rows | Set-Content -LiteralPath $OutputPath -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "processlist query failed: $($rows -join '; ')" }
    $parsed = @()
    foreach ($row in $rows) {
        $parts = $row -split "`t"
        if ($parts.Count -ne 5) { continue }
        $parsed += [pscustomobject]@{
            id = [uint32]$parts[0]; user = $parts[1]; database = $parts[2]
            command = $parts[3]; idle_seconds = [uint32]$parts[4]
        }
    }
    return $parsed
}

Assert-Path $server 'server binary'
Assert-Path $fixture 'automation database fixture'
Assert-Path $resourceRoot 'server resource root'
Assert-Path $mysql 'MySQL client'
if ($ServicePort -eq $AdminPort) { throw 'automation service and admin ports must differ' }
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) { throw "refusing to use occupied declared automation port $port" }
}
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}

$runStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$runId = "$ScenarioId-$runStamp-$PID"
$runDir = Join-Path $repo "artifacts\automation\$runId"
$serverResourceRoot = Join-Path $runDir 'server-resource'
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
$php = $env:CBE_AUTOMATION_PHP
if ([string]::IsNullOrEmpty($php)) {
    $php = @(Get-Command php -All -ErrorAction Stop | Where-Object { $_.Source -match 'php8\.' } |
        Select-Object -ExpandProperty Source -First 1)[0]
}
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
$serverProcess = $null
$oldEnvironment = @{}
$oldMysqlPwd = [Environment]::GetEnvironmentVariable('MYSQL_PWD', 'Process')

try {
    New-Item -ItemType Directory -Path $runDir -ErrorAction Stop | Out-Null
    Copy-Item -LiteralPath $resourceRoot -Destination $serverResourceRoot -Recurse
    if (Test-Path -LiteralPath (Join-Path $serverResourceRoot 'server_update_delivery.tsv')) {
        Remove-Item -LiteralPath (Join-Path $serverResourceRoot 'server_update_delivery.tsv') -Force
    }
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database create failed' }
    & $php $fixture seed $database hangup-peach | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture seed failed' }

    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_MYSQL_HOST = '127.0.0.1'
    $env:CBE_MYSQL_PORT = '3306'
    $env:CBE_MYSQL_USER = 'root'
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $serverResourceRoot
    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru `
        -ArgumentList '--mock-service-only', '--mock-service-bind=127.0.0.1', "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    [pscustomobject]@{ scenario = $ScenarioId; max_steps = $maxSteps; total_timeout_seconds = $ObservationSeconds + 45; single_step_timeout_seconds = $singleStepTimeoutSeconds; server_pid = $serverProcess.Id; database = $database } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'run.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort $singleStepTimeoutSeconds

    $login = Invoke-LoginQuery $ServicePort
    $login | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'login-result.json') -Encoding utf8
    Write-Host "$ScenarioId step=login-query-complete response_bytes=$($login.response_bytes)"

    # First observation verifies the expected normal worker connection exists.
    $initialRows = Get-ServiceMySqlRows $database (Join-Path $runDir 'processlist-initial.tsv')
    if ($initialRows.Count -eq 0) { throw 'no service MySQL connection was visible after protocol-valid login' }

    $firstDelay = [Math]::Min(65, $ObservationSeconds - 1)
    Start-Sleep -Seconds $firstDelay
    $midRows = Get-ServiceMySqlRows $database (Join-Path $runDir 'processlist-after-first-ping.tsv')
    if ($midRows.Count -eq 0) { throw 'service MySQL connection disappeared before first keepalive observation' }
    # Startup migration can own a separate main-thread connection.  It is not
    # a long-lived request worker and is allowed to expire after bootstrap.
    # Select the row whose timer was demonstrably reset by the first worker
    # keepalive, then require this exact MySQL connection id to survive the
    # second keepalive window as well.
    $refreshedRows = @($midRows | Where-Object { $_.idle_seconds -le 30 })
    if ($refreshedRows.Count -eq 0) {
        throw 'no worker MySQL connection showed a timer refresh after first keepalive'
    }
    $keptConnectionId = [uint32]$refreshedRows[0].id
    Write-Host "$ScenarioId step=first-keepalive-observed worker_connection=$keptConnectionId rows=$($midRows.Count) max_idle=$((($midRows | Measure-Object -Property idle_seconds -Maximum).Maximum))"

    $remainingDelay = $ObservationSeconds - $firstDelay
    Start-Sleep -Seconds $remainingDelay
    $finalRows = Get-ServiceMySqlRows $database (Join-Path $runDir 'processlist-after-timeout-threshold.tsv')
    $keptRows = @($finalRows | Where-Object { $_.id -eq $keptConnectionId })
    if ($keptRows.Count -ne 1) {
        throw "worker MySQL connection $keptConnectionId was reclaimed or replaced after $ObservationSeconds seconds"
    }
    $maxIdle = [uint32]$keptRows[0].idle_seconds
    if ($maxIdle -gt 30) { throw "worker MySQL idle timer was not refreshed by second keepalive (idle $maxIdle seconds)" }
    $serverLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'server.stdout.log')
    if ($serverLog -match 'mysql_keepalive_reset|mysql_transport_retry') {
        throw 'keepalive scenario recorded a transport reset/retry; inspect server.stdout.log'
    }
    [pscustomobject]@{ result = 'passed'; scenario = $ScenarioId; assertions = @('protocol-login-query', 'worker-connection-present-after-first-keepalive', 'same-worker-connection-present-past-120-second-wait-timeout', 'worker-idle-timer-refreshed-twice', 'no-keepalive-reset'); worker_connection_id = $keptConnectionId; max_idle_seconds = $maxIdle } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'result.json') -Encoding utf8
    Write-Host "$ScenarioId passed run_dir=$runDir max_idle_seconds=$maxIdle"
}
finally {
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
    [Environment]::SetEnvironmentVariable('MYSQL_PWD', $oldMysqlPwd, 'Process')
    if (-not $KeepDatabase -and $database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append } catch { Write-Warning "could not remove isolated test database: $_" }
    }
}
