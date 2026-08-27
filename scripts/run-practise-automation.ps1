[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)] [int]$ServicePort = 19200,
    [ValidateRange(1024, 65535)] [int]$AdminPort = 19201,
    [switch]$KeepDatabase
)

# Deterministic service-contract regression for offline cultivation.  It owns
# only the process, ports, schema and artifacts declared below; it never opens
# or controls a desktop client and never writes the user's jh_online schema.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
$regression = Join-Path $repo 'scripts\practise-regression.php'
$resourceRoot = Join-Path $repo 'web\fs\JHOnlineData'

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Label not found: $Path" }
}
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

foreach ($entry in @(@($server, 'server binary'), @($fixture, 'schema fixture'),
                      @($regression, 'cultivation regression'), @($resourceRoot, 'resources'))) {
    Require-Path $entry[0] $entry[1]
}
if ($ServicePort -eq $AdminPort) { throw 'service and admin ports must differ' }
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) { throw "refusing occupied automation port $port" }
}
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}

$runId = 'practise-v1-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $PID
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) { throw "refusing to overwrite $runDir" }
$null = New-Item -ItemType Directory -Path $runDir
$serverResourceRoot = Join-Path $runDir 'server-resource'
Copy-Item -LiteralPath $resourceRoot -Destination $serverResourceRoot -Recurse
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
@{ scenario = 'practise-v1'; database = $database; service_port = $ServicePort; admin_port = $AdminPort;
   max_steps = 16; total_timeout_seconds = 20; step_timeout_seconds = 10;
   trigger_rule = 'once'; max_repetitions = 1;
   failure_conditions = @('non-WT response', 'wrong subtype/field contract', 'missing 1/7/17 native completion', 'non-atomic pill state', 'wrong timer/exp settlement');
   input = @('WT 1/7/18', 'WT 1/7/21 opengold=1', 'WT 1/7/16 itemseq=62001', 'WT 1/7/17 usenum:u32(2),itemseq:u16', 'offline 15m fixture') } |
    ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $runDir 'run-context.json') -Encoding utf8

$php = $env:CBE_AUTOMATION_PHP
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
$serverProcess = $null
$oldEnvironment = @{}
try {
    & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database create failed' }
    & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $fixture seed $database hangup-peach | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database seed failed' }

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

    & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $regression setup | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'cultivation fixture setup failed' }
    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru `
        -ArgumentList "--mock-service-only", "--mock-service-bind=127.0.0.1", "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    @{ server = $serverProcess.Id } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort
    & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $regression run $ServicePort | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'cultivation regression failed' }
    Write-Host "practise-v1 passed; evidence: $runDir"
}
finally {
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    if (-not $KeepDatabase -and $database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append }
        catch { Write-Warning "could not remove isolated database: $_" }
    }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
}
