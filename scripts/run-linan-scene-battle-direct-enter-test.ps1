[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$ServicePort = 19450,
    [ValidateRange(1024, 65535)]
    [int]$AdminPort = 19451,
    [ValidateRange(1, 15)]
    [int]$MaxMinutes = 10,
    [switch]$KeepDatabase
)

# Scenario: linan-scene-battle-direct-enter-v1
#
# This runner owns every mutable target: a newly generated test database, an
# artifact-directory resource copy, a fresh client profile, and the processes
# it starts.  It deliberately leaves client input to the user; no window,
# mouse, keyboard, or client-memory automation is used.

$ErrorActionPreference = 'Stop'
$scenario = 'linan-scene-battle-direct-enter-v1'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bin = Join-Path $repo 'bin'
$server = Join-Path $bin 'jh-online-server.exe'
$client = Join-Path $bin 'main.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
$sourceResources = Join-Path $repo 'web\fs\JHOnlineData'
$sourceOverlay = Join-Path $sourceResources '.cbe-overlays\jh_online'
$maxSteps = 5
$singleStepTimeoutSeconds = 20
$originalAutomationPassword = [Environment]::GetEnvironmentVariable(
    'CBE_AUTOMATION_MYSQL_PASSWORD', 'Process')

function Assert-Path([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Description not found: $Path" }
}

function Test-ListeningPort([int]$Port) {
    return @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue).Count -ne 0
}

function Wait-OwnedService([Diagnostics.Process]$Process, [int]$Port, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) {
            throw "isolated service exited early with code $($Process.ExitCode)"
        }
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

function Get-AutomationPassword {
    if (-not [string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
        return $env:CBE_AUTOMATION_MYSQL_PASSWORD
    }
    $secure = Read-Host 'MySQL test password' -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    }
}

function Copy-ClientBaseResources([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -ErrorAction Stop | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | Where-Object { $_.Name -ne '.cbe-overlays' } |
        Copy-Item -Destination $Destination -Recurse -Force
}

Assert-Path $server 'server binary'
Assert-Path $client 'client binary'
Assert-Path $fixture 'isolated account fixture'
Assert-Path $sourceResources 'server resource root'
Assert-Path $sourceOverlay 'deployed Linan resource overlay'
if ($ServicePort -eq $AdminPort) { throw 'service and admin ports must differ' }
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) {
        throw "refusing to use occupied isolated-test port $port"
    }
}

$php = (Get-Command php -ErrorAction Stop).Source
$password = Get-AutomationPassword
if ([string]::IsNullOrEmpty($password)) { throw 'a MySQL test password is required' }
$env:CBE_AUTOMATION_MYSQL_PASSWORD = $password
$runStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$runId = "$scenario-$runStamp-$PID"
$runDir = Join-Path $repo "artifacts\automation\$runId"
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
$serverResources = Join-Path $runDir 'server-resource'
$clientProfile = Join-Path $runDir 'player-3'
$clientResources = Join-Path $clientProfile 'JHOnlineData'
$clientLogs = Join-Path $clientProfile 'logs'
$clientLogin = Join-Path $clientProfile 'nvram\CBE_______OL.CBE_storage_mmorpg_LoginRecord.bin'
$serverProcess = $null
$clientProcess = $null
$oldEnvironment = @{}
$result = 'failed'
$failure = $null

try {
    New-Item -ItemType Directory -Path $runDir -ErrorAction Stop | Out-Null
    Copy-Item -LiteralPath $sourceResources -Destination $serverResources -Recurse -Force
    $serverDelivery = Join-Path $serverResources 'server_update_delivery.tsv'
    if (Test-Path -LiteralPath $serverDelivery) {
        Remove-Item -LiteralPath $serverDelivery -Force
    }
    $isolatedOverlay = Join-Path $serverResources ".cbe-overlays\$database"
    New-Item -ItemType Directory -Path $isolatedOverlay -Force | Out-Null
    Get-ChildItem -LiteralPath $sourceOverlay -Force |
        Copy-Item -Destination $isolatedOverlay -Recurse -Force
    if (-not (Test-Path -LiteralPath (Join-Path $isolatedOverlay 'c04临安府_01.sce'))) {
        throw 'the copied isolated overlay does not contain c04临安府_01.sce'
    }

    New-Item -ItemType Directory -Path (Join-Path $clientProfile 'nvram') -Force | Out-Null
    New-Item -ItemType Directory -Path $clientLogs -Force | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $clientProfile 'CBE') -Target (Join-Path $bin 'CBE') -ErrorAction Stop | Out-Null
    Copy-ClientBaseResources $sourceResources $clientResources
    foreach ($name in @('font_16_sky.uc3', 'font_gb.uc3', 'gb16.uc2.uc3', 'updatetk42.dat')) {
        Copy-Item -LiteralPath (Join-Path $bin $name) -Destination $clientProfile -Force
    }
    & $php $fixture client-login $clientLogin | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'could not create the isolated client login record' }

    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'isolated database creation failed' }
    & $php $fixture seed $database linan-scene-battle | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'isolated Linan role setup failed' }

    foreach ($name in @(
        'CBE_MYSQL_HOST', 'CBE_MYSQL_PORT', 'CBE_MYSQL_USER', 'CBE_MYSQL_PASSWORD',
        'CBE_MYSQL_DATABASE', 'CBE_RESOURCE_ROOT', 'CBE_TEST_STARTUP_SCE_DIRECT_ENTER',
        'CBE_MOCK_SERVICE', 'CBE_TRACE_SCE_ENTITY_CALLBACK',
        'CBE_TRACE_SCENE_BATTLE_COLLISION', 'CBE_TRACE_SCE_NODE_ACTOR_ID'
    )) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_AUTOMATION_MYSQL_PASSWORD = $password
    $env:CBE_MYSQL_HOST = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $env:CBE_MYSQL_PORT = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $env:CBE_MYSQL_USER = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_PASSWORD = $password
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $serverResources
    $env:CBE_TEST_STARTUP_SCE_DIRECT_ENTER = '1'

    [pscustomobject]@{
        scenario = $scenario
        run_id = $runId
        max_steps = $maxSteps
        total_timeout_seconds = $MaxMinutes * 60
        single_step_timeout_seconds = $singleStepTimeoutSeconds
        database = $database
        service = "127.0.0.1:$ServicePort"
        client_profile = $clientProfile
        inputs = @('native client input only: move through scene-battle actor 1')
        assertions = @(
            'final SCE WT18/7 arms one startup WT25/5 -> 16/2(result=1)',
            'client control_state advances to 1 through its normal parser',
            'collision path reaches TriggerAutoBattle and WT4/1'
        )
        failure_conditions = @('service/client exits early', 'no direct-enter arm', 'no control-state transition', 'no WT4/1', 'timeout')
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runDir 'test-plan.json') -Encoding utf8

    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru -ArgumentList @(
        '--mock-service-only', '--mock-service-bind=127.0.0.1', "--mock-service-port=$ServicePort",
        "--mock-admin-port=$AdminPort", "--resource-root=$serverResources"
    ) -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
      -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    Wait-OwnedService $serverProcess $ServicePort $singleStepTimeoutSeconds

    $env:CBE_MOCK_SERVICE = "127.0.0.1:$ServicePort"
    $env:CBE_TRACE_SCE_ENTITY_CALLBACK = '1'
    $env:CBE_TRACE_SCENE_BATTLE_COLLISION = '1'
    $env:CBE_TRACE_SCE_NODE_ACTOR_ID = '1'
    $clientProcess = Start-Process -FilePath $client -WorkingDirectory $clientProfile -PassThru `
        -ArgumentList "--mock-service=127.0.0.1:$ServicePort"
    [pscustomobject]@{ server = $serverProcess.Id; client = $clientProcess.Id } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Write-Host "Test client started. Enter c04临安府_01.sce and walk through fireball actor 1; then close the client."

    if (-not $clientProcess.WaitForExit($MaxMinutes * 60 * 1000)) {
        throw "manual client run exceeded $MaxMinutes minutes"
    }
    if ($clientProcess.ExitCode -ne 0) {
        throw "test client exited with code $($clientProcess.ExitCode)"
    }

    $serverLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'server.stdout.log')
    $collisionPath = Join-Path $clientLogs 'scene-battle-collision.log'
    $collisionLog = if (Test-Path -LiteralPath $collisionPath) {
        Get-Content -Raw -LiteralPath $collisionPath
    } else { '' }
    $assertions = [ordered]@{
        startup_direct_enter_armed = $serverLog -match 'mock_startup_sce_install_scene_enter_test_armed'
        startup_direct_enter_sent = $serverLog -match 'mock_startup_sce_install_scene_enter .*response=16/2-result1'
        client_control_state_one = $collisionLog -match 'control_state=1'
        client_battle_trigger = $collisionLog -match 'TriggerAutoBattle|scene_battle_action'
        client_wt4_1 = $serverLog -match 'request=4/1|wt=4/1'
    }
    $missing = @($assertions.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object { $_.Key })
    if ($missing.Count -ne 0) {
        throw "manual lifecycle assertions not observed: $($missing -join ', ')"
    }
    $result = 'passed'
}
catch {
    $failure = $_.Exception.Message
    Write-Error $failure
}
finally {
    try { Stop-OwnedProcess $clientProcess } catch { Write-Warning "could not stop owned client: $_" }
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    foreach ($name in $oldEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
    }
    [Environment]::SetEnvironmentVariable('CBE_AUTOMATION_MYSQL_PASSWORD',
                                           $originalAutomationPassword,
                                           'Process')
    if (-not $KeepDatabase -and $database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append } catch { Write-Warning "could not remove isolated test database: $_" }
    }
    [pscustomobject]@{
        scenario = $scenario
        run_id = $runId
        result = $result
        failure = $failure
        server_pid = if ($null -eq $serverProcess) { $null } else { $serverProcess.Id }
        client_pid = if ($null -eq $clientProcess) { $null } else { $clientProcess.Id }
        assertions = if ($null -eq $assertions) { @{} } else { $assertions }
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runDir 'result.json') -Encoding utf8
    Write-Host "Evidence directory: $runDir"
}

if ($result -ne 'passed') { exit 1 }
