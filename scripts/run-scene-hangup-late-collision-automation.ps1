[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$ServicePort = 19370,
    [ValidateRange(1024, 65535)]
    [int]$AdminPort = 19371,
    [switch]$KeepDatabase
)

$ErrorActionPreference = 'Stop'
$scenario = 'scene-hangup-late-collision-v1'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
$probe = Join-Path $repo 'scripts\scene-hangup-late-collision-regression.php'
$resourceRoot = Join-Path $repo 'web\fs\JHOnlineData'

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
        $Process.WaitForExit(5000)
    }
}

Assert-Path $server 'server binary'
Assert-Path $fixture 'automation fixture'
Assert-Path $probe 'late-collision regression probe'
Assert-Path $resourceRoot 'server resource root'
if ($ServicePort -eq $AdminPort) { throw 'automation service and admin ports must differ' }
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) { throw "refusing to use occupied declared automation port $port" }
}
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}
$php = (Get-Command php -ErrorAction Stop).Source
$runStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$runId = "$scenario-$runStamp-$PID"
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) { throw "refusing to overwrite run directory: $runDir" }
$null = New-Item -ItemType Directory -Path $runDir
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
$isolatedResources = Join-Path $runDir 'server-resource'
$serverProcess = $null
$oldEnvironment = @{}

try {
    # The source resource tree is copied because the delivery ledger is mutable
    # service state.  No production database, cache, port or user process is
    # used by this scenario.
    Copy-Item -LiteralPath $resourceRoot -Destination $isolatedResources -Recurse
    $deliveryLedger = Join-Path $isolatedResources 'server_update_delivery.tsv'
    if (Test-Path -LiteralPath $deliveryLedger) { Remove-Item -LiteralPath $deliveryLedger -Force }
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database create failed' }
    & $php $fixture seed $database hangup-peach | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database seed failed' }

    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT','CBE_BATTLE_ENEMY_COUNT','CBE_BATTLE_ENEMY_HP')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_MYSQL_HOST = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $env:CBE_MYSQL_PORT = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $env:CBE_MYSQL_USER = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $isolatedResources
    $env:CBE_BATTLE_ENEMY_COUNT = '1'
    $env:CBE_BATTLE_ENEMY_HP = '20'
    [pscustomobject]@{
        scenario = $scenario
        max_steps = 6
        total_timeout_seconds = 30
        step_timeout_seconds = 5
        input = @('1/12 login','1/6 role-select','scene 39-byte resource-followup','2/10+25/3 hangup start','late 4/1 collision','4/12 native auto timer')
        assertions = @('late 4/1 response is only 4/11(type=1)','following 4/12 produces 4/6')
        failure_conditions = @('second 4/5','unhandled 4/12','service exit','timeout')
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runDir 'test-plan.json') -Encoding utf8

    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru -ArgumentList @(
        '--mock-service-only', '--mock-service-bind=127.0.0.1', "--mock-service-port=$ServicePort",
        "--mock-admin-port=$AdminPort", "--resource-root=$isolatedResources"
    ) -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    [pscustomobject]@{ server = $serverProcess.Id } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort 15

    & $php $probe $ServicePort | Tee-Object -FilePath (Join-Path $runDir 'probe.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'late-collision regression probe failed' }
    $serverLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'server.stdout.log')
    if ($serverLog -notmatch 'mock_scene_hangup_duplicate_challenge_reaffirm .*request=4/1 response=4/11\(type=1\)' -or
        $serverLog -match 'unhandled wt=4/12') {
        throw 'required retained-session trace missing or native 4/12 became unhandled'
    }
    [pscustomobject]@{
        result = 'passed'
        scenario = $scenario
        source_contract = '4/1 -> only 4/11(type=1)'
        followup_contract = '4/12 -> 4/6'
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'result.json') -Encoding utf8
    Write-Host "automation run directory: $runDir"
    Write-Host "$scenario passed"
}
finally {
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
    if (-not $KeepDatabase -and $database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append } catch { Write-Warning "could not remove isolated test database: $_" }
    }
}
