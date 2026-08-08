[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)] [int]$ServicePort = 19324,
    [ValidateRange(1024, 65535)] [int]$AdminPort = 19325
)

# Isolated admin regression for NPC exclusive-stock batch configuration.
# It starts and stops only this run's service PID and uses one temporary
# jh_online_autotest_* database.  It never attaches to a desktop client or
# mutates the interactive jh_online database.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
$regression = Join-Path $repo 'scripts\npc-stock-regression.php'
$resourceRoot = Join-Path $repo 'web\fs\JHOnlineData'

function Require-Path([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Label not found: $Path" }
}
function Test-ListeningPort([int]$Port) {
    return @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue).Count -ne 0
}
function Wait-OwnedService([Diagnostics.Process]$Process, [int[]]$Ports) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) { throw "isolated service exited early with code $($Process.ExitCode)" }
        if ((@($Ports | Where-Object { -not (Test-ListeningPort $_) }).Count) -eq 0) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "isolated service did not listen on declared ports: $($Ports -join ', ')"
}
function Stop-OwnedProcess([Diagnostics.Process]$Process) {
    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -ErrorAction Stop
        $Process.WaitForExit(5000)
    }
}

foreach ($entry in @(@($server, 'server binary'), @($fixture, 'schema fixture'),
                      @($regression, 'NPC stock regression'), @($resourceRoot, 'resources'))) {
    Require-Path $entry[0] $entry[1]
}
if ($ServicePort -eq $AdminPort) { throw 'service and admin ports must differ' }
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) { throw "refusing occupied automation port $port" }
}
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}

$runId = 'npc-stock-bulk-v1-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $PID
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) { throw "refusing to overwrite $runDir" }
$null = New-Item -ItemType Directory -Path $runDir
$serverResourceRoot = Join-Path $runDir 'server-resource'
Copy-Item -LiteralPath $resourceRoot -Destination $serverResourceRoot -Recurse
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
@{
    scenario = 'npc-stock-bulk-v1'; database = $database
    service_port = $ServicePort; admin_port = $AdminPort
    max_steps = 9; total_timeout_seconds = 35; step_timeout_seconds = 10
    trigger_rule = 'once'; max_repetitions = 1
    failure_conditions = @('admin login fails', 'multi-picker missing', 'blank price rejected', 'wrong category accepted', 'partial write', 'bulk delete partial')
    input = @('HTTP admin login', 'GET NPC content page', 'POST bulk select (84 weapons)', 'WT NPC dialog', 'WT weapon category and item page', 'POST out-of-category weapon-stock request', 'POST bulk remove')
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $runDir 'run-context.json') -Encoding utf8

$php = $env:CBE_AUTOMATION_PHP
if ([string]::IsNullOrEmpty($php)) {
    # The legacy phpStudy PHP 7 CLI can block PDO calls under its Xdebug
    # configuration.  Match the existing isolated runners and prefer PHP 8.
    $php = @(Get-Command php -All -ErrorAction Stop |
        Where-Object { $_.Source -match 'php8\.' } |
        Select-Object -ExpandProperty Source -First 1)[0]
}
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
$node = (Get-Command node -ErrorAction Stop).Source
$serverProcess = $null
$oldEnvironment = @{}
try {
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'isolated schema-only database create failed' }
    & $php $regression prepare $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'isolated NPC stock fixture preparation failed' }

    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT','CBE_AUTOMATION_ARTIFACT_DIR')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_MYSQL_HOST = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $env:CBE_MYSQL_PORT = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $env:CBE_MYSQL_USER = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $serverResourceRoot
    $env:CBE_AUTOMATION_ARTIFACT_DIR = $runDir

    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru `
        -ArgumentList '--mock-service-only', '--mock-service-bind=127.0.0.1', "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    @{ server = $serverProcess.Id } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess @($ServicePort, $AdminPort)
    & $php $regression verify $AdminPort $ServicePort $database | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'NPC stock regression failed' }
    & $node --check (Join-Path $runDir 'admin.js') | Tee-Object -FilePath (Join-Path $runDir 'javascript-check.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'served admin JavaScript has invalid syntax' }
    Write-Host "npc-stock-bulk-v1 passed; evidence: $runDir"
}
finally {
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    if ($database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append }
        catch { Write-Warning "could not remove isolated database: $_" }
    }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
}
