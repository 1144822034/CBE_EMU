[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$ServicePort = 19190,
    [ValidateRange(1024, 65535)]
    [int]$AdminPort = 19191,
    [ValidateSet('shop-return-hangup-v1', 'direct-hangup-control-v1', 'title-module-update-v1', 'scene-teleport-stone-probe-v1', 'equipment-enhance-rules-probe-v1', 'equipment-enhance-bag-probe-v1', 'equipment-enhance-stage1-probe-v1', 'hangup-auto-cancel-v1', 'hangup-auto-terminal-v1', 'hangup-native-auto-exit-v1')]
    [string]$Scenario = 'shop-return-hangup-v1',
    [switch]$KeepDatabase
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$client = Join-Path $repo 'bin\main.exe'
$fixture = Join-Path $repo 'scripts\automation-shop-return-hangup-fixture.php'
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
Assert-Path $client 'client binary'
Assert-Path $fixture 'automation fixture'
Assert-Path $resourceRoot 'server resource root'
if ($ServicePort -eq $AdminPort) {
    throw 'automation service and admin ports must differ'
}
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) {
        throw "refusing to use occupied declared automation port $port"
    }
}
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}

$runStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$runId = "$Scenario-$runStamp-$PID"
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) { throw "refusing to overwrite existing run directory: $runDir" }
$null = New-Item -ItemType Directory -Path $runDir
$null = New-Item -ItemType Directory -Path (Join-Path $runDir 'frames')
$clientDir = Join-Path $runDir 'client'
$null = New-Item -ItemType Directory -Path $clientDir
$serverResourceRoot = Join-Path $runDir 'server-resource'
# The update catalog and delivery ledger are service state, even though they
# reside beside static resources.  Run the isolated service against a private
# copy so an existing interactive client's delivery entry cannot affect the
# result and the test never writes back into web/fs/JHOnlineData.
Copy-Item -LiteralPath $resourceRoot -Destination $serverResourceRoot -Recurse
$isolatedDeliveryLedger = Join-Path $serverResourceRoot 'server_update_delivery.tsv'
if (Test-Path -LiteralPath $isolatedDeliveryLedger) {
    # A delivery ledger is volatile service state, not part of the immutable
    # resource fixture.  Every run starts with no completed clients.
    Remove-Item -LiteralPath $isolatedDeliveryLedger -Force
}
# The client-side protocol probes are deliberately append-only host logs.  The
# emulator does not create parent directories for them, so prepare this
# per-run artifact directory before launch.  This is not guest state and does
# not alter packets, callbacks, or the client executable.
$null = New-Item -ItemType Directory -Path (Join-Path $clientDir 'logs')
$database = 'jh_online_autotest_' + ([Guid]::NewGuid().ToString('N'))
$php = $env:CBE_AUTOMATION_PHP
if ([string]::IsNullOrEmpty($php)) {
    # The legacy PHP 7 distribution bundled by phpStudy can leave PDO calls
    # blocked under its Xdebug configuration.  Prefer an installed PHP 8 CLI;
    # callers can select another audited CLI with CBE_AUTOMATION_PHP.
    $php = @(Get-Command php -All -ErrorAction Stop |
        Where-Object { $_.Source -match 'php8\.' } |
        Select-Object -ExpandProperty Source -First 1)[0]
}
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
$serverProcess = $null
$clientProcess = $null
$oldEnvironment = @{}
$clientScenario = $Scenario

try {
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database create failed' }

    # The schema is available immediately after `create`.  Seed before the
    # service starts so startup migration sees one internally consistent test
    # account/role pair, just as it would in a populated deployment.  The
    # fixture writes only to the uniquely named automation schema.
    $fixtureProfile = if ($Scenario -in @('scene-teleport-stone-probe-v1', 'title-module-update-v1')) {
        'teleport-stone-c00'
    } elseif ($Scenario -in @('equipment-enhance-rules-probe-v1', 'equipment-enhance-bag-probe-v1', 'equipment-enhance-stage1-probe-v1')) {
        'equipment-enhance'
    } else {
        'hangup-peach'
    }
    & $php $fixture seed $database $fixtureProfile | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture seed failed' }

    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT','CBE_BATTLE_ENEMY_COUNT','CBE_BATTLE_ENEMY_HP')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_MYSQL_HOST = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $env:CBE_MYSQL_PORT = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $env:CBE_MYSQL_USER = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $serverResourceRoot
    # These are per-run service fixtures, not user-server configuration.  The
    # The cancel case keeps three high-HP targets alive until the native
    # cancel button is dispatched.  The ordinary terminal scenarios retain
    # three one-hit enemies.
    if ($Scenario -eq 'hangup-auto-cancel-v1') {
        $env:CBE_BATTLE_ENEMY_COUNT = '3'
        $env:CBE_BATTLE_ENEMY_HP = '100'
    } elseif ($Scenario -in @('hangup-auto-terminal-v1', 'hangup-native-auto-exit-v1')) {
        $env:CBE_BATTLE_ENEMY_COUNT = '3'
        $env:CBE_BATTLE_ENEMY_HP = '20'
    }
    [pscustomobject]@{
        runner_scenario = $Scenario
        client_input_scenario = $clientScenario
        fixture_profile = $fixtureProfile
        service_port = $ServicePort
        admin_port = $AdminPort
        native_auto_exit = ($Scenario -eq 'hangup-native-auto-exit-v1')
        native_auto_exit_contract = if ($Scenario -eq 'hangup-native-auto-exit-v1') {
            '25/2(result=1,type=1) -> mmBattle 0x8996 -> 0x5E92 -> client 25/5 -> scene poll next 4/5/4/6; no 0x60C8'
        } else { $null }
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'test-plan.json') -Encoding utf8

    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru `
        -ArgumentList "--mock-service-only", "--mock-service-bind=127.0.0.1", "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    [pscustomobject]@{ server = $serverProcess.Id; client = $null } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort 15

    foreach ($name in @('main.exe','SDL2.dll','unicorn.dll','libwinpthread-1.dll','font_gb.uc3','updatetk42.dat')) {
        $source = Join-Path $repo "bin\$name"
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $clientDir }
    }
    New-Item -ItemType Junction -Path (Join-Path $clientDir 'CBE') -Target (Join-Path $repo 'bin\CBE') | Out-Null
    # The module updater creates MMORPGTempcbm while it streams a CBM.  Do not
    # read from or junction to bin\JHOnlineData: an interactive client may be
    # holding that cache open, and a junction would let a test mutate it.
    # resourceRoot is the server-owned input for this isolated run and is
    # copied into a per-run client cache instead.
    Copy-Item -LiteralPath $serverResourceRoot -Destination (Join-Path $clientDir 'JHOnlineData') -Recurse
    $isolatedUpdateTemp = Join-Path $clientDir 'JHOnlineData\MMORPGTempcbm'
    if (Test-Path -LiteralPath $isolatedUpdateTemp) {
        # A host cache may contain a partial update from a prior interactive
        # run.  The isolated client must begin this test from a deterministic
        # update state; this path is inside the just-created run directory.
        Remove-Item -LiteralPath $isolatedUpdateTemp -Force
    }
    if ($Scenario -eq 'title-module-update-v1') {
        # The copied service tree can contain the current module-version table.
        # This test needs the documented pre-release client state (slot 1 at
        # version 0), so remove that table only from its private cache.  The
        # CBE initializes an absent table to its built-in module versions and
        # must then perform the regular 18/5 -> 18/6 transaction itself.
        $isolatedVersionTable = Join-Path $clientDir 'JHOnlineData\mmorpg_updateversioncbm'
        if (Test-Path -LiteralPath $isolatedVersionTable) {
            Remove-Item -LiteralPath $isolatedVersionTable -Force
        }
    }
    $loginRecord = Join-Path $clientDir 'nvram\CBE_______OL.CBE_storage_mmorpg_LoginRecord.bin'
    & $php $fixture client-login $loginRecord | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'isolated client LoginRecord fixture failed' }
    $env:CBE_SERVER_ENDPOINT = "127.0.0.1:$ServicePort"
    # These are the formerly manually documented title inputs, now an explicit
    # one-shot schedule delivered through the VM's ordinary input queue.  The
    # title-module probe purposely sends no input: it stops at the native
    # module-updater terminal state before the title screen accepts controls.
    $titleActions = '5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f'
    $clientArguments = @("--automation-scenario=$clientScenario", "--automation-artifacts=$runDir")
    if ($clientScenario -ne 'title-module-update-v1') {
        $clientArguments += '--automation-title-driver=timed-title-v1'
        $clientArguments += "--actions=$titleActions"
    }
    $clientProcess = Start-Process -FilePath (Join-Path $clientDir 'main.exe') -WorkingDirectory $clientDir -PassThru `
        -ArgumentList $clientArguments `
        -RedirectStandardOutput (Join-Path $runDir 'client.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'client.stderr.log')
    [pscustomobject]@{ server = $serverProcess.Id; client = $clientProcess.Id } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    if (-not $clientProcess.WaitForExit(190000)) {
        throw 'automation client exceeded its bounded 180-second scenario timeout plus shutdown margin'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $runDir 'result.json'))) {
        throw 'automation client exited without result.json'
    }
    $result = Get-Content -Raw -LiteralPath (Join-Path $runDir 'result.json') | ConvertFrom-Json
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'runner-result.json') -Encoding utf8
    Write-Host "automation run directory: $runDir"
    if ($result.result -ne 'passed') {
        throw "scenario failed at $($result.stage): $($result.reason)"
    }
    if ($Scenario -eq 'hangup-native-auto-exit-v1') {
        $serverLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'server.stdout.log')
        $automationLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'automation.log')
        $responseObject = [regex]::Matches(
            $automationLog,
            'automation_hangup_native_auto_exit_response .*result=1 type=1 '
        ).Count
        $parser8996 = [regex]::Matches(
            $automationLog,
            'automation_hangup_native_auto_exit_parser local_pc=0x8996 '
        ).Count
        $sender5e92 = [regex]::Matches(
            $automationLog,
            'automation_hangup_native_auto_exit_pc local_pc=0x5e92 '
        ).Count
        $manualExit = [regex]::Matches(
            $automationLog,
            'automation_hangup_native_auto_exit_manual_pc '
        ).Count
        $exitUplink = [regex]::Matches(
            $automationLog,
            'automation_hangup_native_auto_exit_uplink wt=25/5 .*reentry_handler_reset=1'
        ).Count
        $serverRoundComplete = [regex]::Matches(
            $serverLog,
            'scene_hangup_round_complete '
        ).Count
        $scenePollStart = [regex]::Matches(
            $serverLog,
            'mock_hangup_battle_start source=scene-poll .*native_exit25_2=terminal-after-4/7 '
        ).Count
        $completion = [regex]::Matches(
            $automationLog,
            'automation_hangup_native_auto_exit_complete '
        ).Count
        [pscustomobject]@{
            response_object = $responseObject
            parser_8996 = $parser8996
            sender_5e92 = $sender5e92
            manual_exit_pc = $manualExit
            native_exit_uplink = $exitUplink
            server_round_complete = $serverRoundComplete
            scene_poll_start = $scenePollStart
            completion = $completion
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'native-auto-exit-evidence.json') -Encoding utf8
        if ($responseObject -ne 1 -or $parser8996 -ne 1 -or $sender5e92 -ne 1 -or
            $manualExit -ne 0 -or $exitUplink -ne 1 -or $serverRoundComplete -lt 1 -or
            $scenePollStart -lt 1 -or $completion -ne 1) {
            throw 'native auto-exit evidence missing: expected 25/2->0x8996->0x5e92->client 25/5->scene poll, with no 0x60C8'
        }
    }
    Write-Host "$Scenario passed"
}
finally {
    try { Stop-OwnedProcess $clientProcess } catch { Write-Warning "could not stop owned client: $_" }
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
    if (-not $KeepDatabase -and $database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append } catch { Write-Warning "could not remove isolated test database: $_" }
    }
}
