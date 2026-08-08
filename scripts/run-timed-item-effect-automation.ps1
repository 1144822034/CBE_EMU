[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$ServicePort = 19198,
    [ValidateRange(1024, 65535)]
    [int]$AdminPort = 19199,
    [switch]$KeepDatabase
)

# Deterministic, service-level regression for time-limited item use.  It
# sends only ordinary CBMS frames and creates a private MySQL schema and
# resource copy; it never starts or controls the desktop client.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
$regression = Join-Path $repo 'tmp\timed-item-effect-regression.php'
$resourceRoot = Join-Path $repo 'web\fs\JHOnlineData'

function Assert-Path([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Description not found: $Path" }
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

foreach ($required in @(
    @($server, 'server binary'), @($fixture, 'automation schema fixture'),
    @($regression, 'timed-item regression'), @($resourceRoot, 'server resource root')
)) { Assert-Path $required[0] $required[1] }
if ($ServicePort -eq $AdminPort) { throw 'automation service and admin ports must differ' }
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) { throw "refusing to use occupied declared automation port $port" }
}
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}

$runId = 'timed-item-effects-v1-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $PID
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) { throw "refusing to overwrite existing run directory: $runDir" }
$null = New-Item -ItemType Directory -Path $runDir
$serverResourceRoot = Join-Path $runDir 'server-resource'
Copy-Item -LiteralPath $resourceRoot -Destination $serverResourceRoot -Recurse
$deliveryLedger = Join-Path $serverResourceRoot 'server_update_delivery.tsv'
if (Test-Path -LiteralPath $deliveryLedger) { Remove-Item -LiteralPath $deliveryLedger -Force }
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
@{ scenario = 'timed-item-effects-v1'; database = $database; service_port = $ServicePort; admin_port = $AdminPort } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'run-context.json') -Encoding utf8
$php = $env:CBE_AUTOMATION_PHP
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
$serverProcess = $null
$oldEnvironment = @{}

try {
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database create failed' }
    & $php $fixture seed $database hangup-peach | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database seed failed' }

    foreach ($name in @(
        'CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD',
        'CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT','CBE_BATTLE_ENEMY_COUNT',
        'CBE_BATTLE_ENEMY_HP','CBE_BATTLE_ENEMY_ATTACK','CBE_BATTLE_ENEMY_DEFENSE',
        'CBE_TEST_MYSQL_HOST','CBE_TEST_MYSQL_PORT','CBE_TEST_MYSQL_USER',
        'CBE_TEST_MYSQL_PASSWORD','CBE_TEST_MYSQL_DATABASE'
    )) { $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
    $dbHost = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $mysqlPort = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $user = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_HOST = $dbHost
    $env:CBE_MYSQL_PORT = $mysqlPort
    $env:CBE_MYSQL_USER = $user
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $serverResourceRoot
    # High HP guarantees the normal action packet includes a return attack;
    # fixed combat values make damage comparisons independent of resource
    # monster tuning and do not alter any user service.
    $env:CBE_BATTLE_ENEMY_COUNT = '1'
    $env:CBE_BATTLE_ENEMY_HP = '1000'
    $env:CBE_BATTLE_ENEMY_ATTACK = '100'
    $env:CBE_BATTLE_ENEMY_DEFENSE = '0'
    $env:CBE_TEST_MYSQL_HOST = $dbHost
    $env:CBE_TEST_MYSQL_PORT = $mysqlPort
    $env:CBE_TEST_MYSQL_USER = $user
    $env:CBE_TEST_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_TEST_MYSQL_DATABASE = $database

    # Seed every test role before the service starts.  This lets the normal
    # account-local role-cache load observe one coherent snapshot, matching
    # production login rather than mutating account rows after startup.
    & $php $regression setup | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'timed-item fixture setup failed' }

    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru `
        -ArgumentList "--mock-service-only", "--mock-service-bind=127.0.0.1", "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    @{ server = $serverProcess.Id } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort

    & $php $regression run $ServicePort | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'timed-item regression failed' }
    Write-Host "timed-item-effects-v1 passed; evidence: $runDir"
}
finally {
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    if (-not $KeepDatabase -and $database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try {
            & $php $regression cleanup | Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
        } catch { Write-Warning "could not remove timed-item fixture rows: $_" }
        try {
            & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
        } catch { Write-Warning "could not remove isolated database: $_" }
    }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
}
