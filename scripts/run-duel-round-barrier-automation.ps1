[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)] [int]$ServicePort = 19340,
    [ValidateRange(1024, 65535)] [int]$AdminPort = 19341,
    [switch]$KeepDatabase
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
$regression = Join-Path $repo 'scripts\duel-round-barrier-regression.php'
$resourceRoot = Join-Path $repo 'web\fs\JHOnlineData'

function Test-ListeningPort([int]$Port) {
    return @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue).Count -ne 0
}
function Wait-OwnedService([Diagnostics.Process]$Process, [int]$Port) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) { throw "isolated service exited early with code $($Process.ExitCode)" }
        if (Test-ListeningPort $Port) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "isolated service did not listen on 127.0.0.1:$Port"
}
function Stop-OwnedProcess([Diagnostics.Process]$Process) {
    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -ErrorAction Stop
        $Process.WaitForExit(5000)
    }
}

foreach ($path in @($server, $fixture, $regression, $resourceRoot)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "required path not found: $path" }
}
if ($ServicePort -eq $AdminPort) { throw 'service and admin ports must differ' }
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) { throw "refusing occupied automation port $port" }
}
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}

$runId = 'duel-round-barrier-v1-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $PID
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) { throw "refusing to overwrite $runDir" }
$null = New-Item -ItemType Directory -Path $runDir
$serverResourceRoot = Join-Path $runDir 'server-resource'
Copy-Item -LiteralPath $resourceRoot -Destination $serverResourceRoot -Recurse
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
@{
    scenario = 'duel-round-barrier-v1'; database = $database
    service_port = $ServicePort; admin_port = $AdminPort
    max_steps = 256; total_timeout_seconds = 45; step_timeout_seconds = 10
    trigger_rule = 'once'; max_repetitions = 1
    failure_conditions = @('duel visual row mismatch', 'early 4/6', 'duplicate intent overwrite',
        'missing combined round', 'wrong mirror delivery', '4/12 bypasses barrier',
        'terminal round bypasses barrier', 'missing native death action in friendly duel',
        'terminal duel action queued after its target reached zero',
        'wrong duel terminal object', 'duel released before both native exits',
        'durable role vitals changed')
    input = @('two CBMS logins', 'two scene-ready six-object WT subset requests', '4/14 invite',
        'scene-poll 4/15', '4/16+4/9 accept', 'two mirrored 4/10 starts',
        'manual 4/2 rounds', 'duplicate 4/2', 'automatic 4/12 rounds through terminal',
        'two terminal 4/6(damage+death(type=3)) no-reward-close deliveries', 'late 4/2 and 4/12 ownership checks',
        'two native 25/5 exit acknowledgements', 'post-exit reinvite',
        'second duel active 4/4 escape and two 25/5 acknowledgements',
        'durable vital checks')
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $runDir 'run-context.json') -Encoding utf8

$php = @(Get-Command php -All -ErrorAction Stop |
    Where-Object { $_.Source -match 'php8\.' } |
    Select-Object -ExpandProperty Source -First 1)[0]
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
$serverProcess = $null
$oldEnvironment = @{}
try {
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'isolated schema creation failed' }
    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD',
                         'CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT','CBE_TEST_MYSQL_HOST',
                         'CBE_TEST_MYSQL_PORT','CBE_TEST_MYSQL_USER','CBE_TEST_MYSQL_PASSWORD',
                         'CBE_TEST_MYSQL_DATABASE')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $dbHost = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $dbPort = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $dbUser = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_HOST = $dbHost; $env:CBE_MYSQL_PORT = $dbPort; $env:CBE_MYSQL_USER = $dbUser
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD; $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $serverResourceRoot
    $env:CBE_TEST_MYSQL_HOST = $dbHost; $env:CBE_TEST_MYSQL_PORT = $dbPort; $env:CBE_TEST_MYSQL_USER = $dbUser
    $env:CBE_TEST_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD; $env:CBE_TEST_MYSQL_DATABASE = $database

    & $php $regression setup | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'duel fixture setup failed' }
    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru -WindowStyle Hidden `
        -ArgumentList '--mock-service-only', '--mock-service-bind=127.0.0.1',
            "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    @{ server = $serverProcess.Id } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort
    & $php $regression run $ServicePort | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'duel round barrier regression failed' }
    Stop-OwnedProcess $serverProcess
    $serverProcess = $null
    $serverLog = Get-Content -LiteralPath (Join-Path $runDir 'server.stdout.log') -Raw
    foreach ($required in @('response=4/6(damage+death(type=3)) kind=native-death-close',
                             'duel_escape', 'response=4/4(result=1) kind=escape',
                             'duel_terminal_exit_ack', 'duel_release')) {
        if (-not $serverLog.Contains($required)) { throw "missing server evidence: $required" }
    }
    foreach ($forbidden in @('response=4/8', 'kind=auto-restore', 'response=4/6+4/7',
                              'source=builtin-battle-operate',
                              'mock_battle_death_prompt_choice')) {
        if ($serverLog.Contains($forbidden)) { throw "forbidden terminal path reached: $forbidden" }
    }
    $firstRelease = $serverLog.IndexOf('duel_release')
    if ($firstRelease -lt 0) { throw 'natural duel was not released' }
    $naturalLog = $serverLog.Substring(0, $firstRelease)
    if ($naturalLog.Contains('kind=escape') -or $naturalLog.Contains('response=4/4')) {
        throw 'natural duel terminal entered the active escape path'
    }
    if ($naturalLog -notmatch 'duel_action_round_release[^\r\n]*terminal=1 post_defeat_actions=0') {
        throw 'terminal duel did not prove that no action followed a defeated target'
    }
    if ([regex]::Matches($naturalLog, 'duel_terminal_packet .*objects=1 .*actionnum=2 .*source=4/6\(damage-before-death\(type=3\)\)').Count -ne 2) {
        throw 'terminal duel did not deliver the expected damage-plus-death close packet to both observers'
    }
    $lastExitAck = $serverLog.LastIndexOf('duel_terminal_exit_ack')
    $release = $serverLog.IndexOf('duel_release', $lastExitAck)
    if ($lastExitAck -lt 0 -or $release -lt $lastExitAck) {
        throw 'duel release did not follow the second native exit acknowledgement'
    }
    Write-Host "duel-round-barrier-v1 passed; evidence: $runDir"
}
finally {
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    if (-not $KeepDatabase -and $database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append }
        catch { Write-Warning "could not remove isolated database: $_" }
    }
    foreach ($name in $oldEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
    }
}
