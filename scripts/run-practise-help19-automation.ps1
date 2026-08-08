[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)] [int]$ServicePort = 19202,
    [ValidateRange(1024, 65535)] [int]$AdminPort = 19203
)

# Deterministic WT 1/7/19 protocol regression. It owns only the server PID,
# two declared ports, a random schema-only test database and its artifact directory.
# It never opens or controls a desktop client and never reads or writes
# jh_online data.
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
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
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

$runId = 'practise-help19-v1-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $PID
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) { throw "refusing to overwrite $runDir" }
$null = New-Item -ItemType Directory -Path $runDir
$serverResourceRoot = Join-Path $runDir 'server-resource'
Copy-Item -LiteralPath $resourceRoot -Destination $serverResourceRoot -Recurse
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
@{ scenario = 'practise-help19-v1'; database = $database; service_port = $ServicePort; admin_port = $AdminPort;
   max_steps = 6; total_timeout_seconds = 20; step_timeout_seconds = 10;
   trigger_rule = 'once'; max_repetitions = 1;
   failure_conditions = @('service does not listen', 'non-WT response', 'wrong subtype or missing helpinfo', 'non-GBK help title');
   input = @('WT 1/12 login', 'WT 1/16 account page', 'WT 1/4 server select', 'WT 1/6 role select', 'WT 1/7/19 { type: tagged-u8(0) }') } |
    ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $runDir 'run-context.json') -Encoding utf8

$php = $env:CBE_AUTOMATION_PHP
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
$serverProcess = $null
$oldEnvironment = @{}
try {
    & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'isolated schema-only database create failed' }
    & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $fixture seed $database hangup-peach | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'isolated test account seed failed' }

    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD',
                         'CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_MYSQL_HOST = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $env:CBE_MYSQL_PORT = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $env:CBE_MYSQL_USER = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $serverResourceRoot

    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru `
        -ArgumentList "--mock-service-only", "--mock-service-bind=127.0.0.1", "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    @{ server = $serverProcess.Id } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort
    & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $regression help $ServicePort | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'cultivation help regression failed' }
    Write-Host "practise-help19-v1 passed; evidence: $runDir"
}
finally {
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    if ($database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php -d xdebug.remote_enable=0 -d xdebug.remote_autostart=0 $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append }
        catch { Write-Warning "could not remove isolated database: $_" }
    }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
}
