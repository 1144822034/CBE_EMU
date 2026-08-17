[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$ServicePort = 19190,
    [ValidateRange(1024, 65535)]
    [int]$AdminPort = 19191,
    [ValidateSet('shop-return-hangup-v1', 'direct-hangup-control-v1', 'title-module-update-v1', 'scene-teleport-stone-probe-v1', 'equipment-enhance-rules-probe-v1', 'equipment-enhance-bag-probe-v1', 'equipment-enhance-stage1-probe-v1', 'hangup-auto-cancel-v1', 'hangup-auto-terminal-v1', 'hangup-native-auto-exit-v1', 'hangup-auto-reward-continue-v1', 'hangup-auto-rapid-entry-v1', 'hangup-auto-vitals-recovery-v1', 'hangup-auto-vitals-flask-v1', 'hangup-auto-restart-delay-v1')]
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
$clientScenario = if ($Scenario -eq 'hangup-auto-vitals-flask-v1') {
    # The native client scenario already knows the identical 95/295,80/205
    # expected result and performs its read-only scene-HUD-node assertion.
    'hangup-auto-vitals-recovery-v1'
} elseif ($Scenario -eq 'hangup-auto-restart-delay-v1') {
    # Terminal recovery and restart timing must exercise product-mode hangup:
    # the client closes its rendered 4/7 through the normal 25/5 path, then a
    # later scene poll starts the next round.  Do not inject a second manual
    # hangup tap here, because that is intentionally rejected during the
    # five-second inter-round interval.
    'hangup-auto-reward-continue-v1'
} else {
    $Scenario
}

try {
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database create failed' }

    # The schema is available immediately after `create`.  Seed before the
    # service starts so startup migration sees one internally consistent test
    # account/role pair, just as it would in a populated deployment.  The
    # fixture writes only to the uniquely named automation schema.
    $fixtureProfile = if ($Scenario -in @('scene-teleport-stone-probe-v1', 'title-module-update-v1')) {
        'teleport-stone-c00'
    } elseif ($Scenario -eq 'hangup-auto-vitals-recovery-v1') {
        'hangup-vitals'
    } elseif ($Scenario -eq 'hangup-auto-vitals-flask-v1') {
        'hangup-vitals-flask'
    } elseif ($Scenario -in @('equipment-enhance-rules-probe-v1', 'equipment-enhance-bag-probe-v1', 'equipment-enhance-stage1-probe-v1')) {
        'equipment-enhance'
    } else {
        'hangup-peach'
    }
    & $php $fixture seed $database $fixtureProfile | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture seed failed' }

    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT','CBE_BATTLE_ENEMY_COUNT','CBE_BATTLE_ENEMY_HP','CBE_HANGUP_AUTO_CONFIRM','CBE_BATTLE_RECOVER_HP','CBE_BATTLE_RECOVER_MP')) {
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
    # three one-hit enemies.  The direct-restart probe deliberately uses one
    # enemy per round so it reaches two complete, real client settlements
    # within its bounded scenario runtime.  The three-second audit is not an
    # outcome requirement here: normal result-panel animation may legitimately
    # make those two battle-entry timestamps farther apart.
    if ($Scenario -eq 'hangup-auto-cancel-v1') {
        $env:CBE_BATTLE_ENEMY_COUNT = '3'
        $env:CBE_BATTLE_ENEMY_HP = '100'
    } elseif ($Scenario -in @('hangup-auto-terminal-v1', 'hangup-native-auto-exit-v1', 'hangup-auto-reward-continue-v1')) {
        $env:CBE_BATTLE_ENEMY_COUNT = '3'
        $env:CBE_BATTLE_ENEMY_HP = '20'
    } elseif ($Scenario -in @('hangup-auto-rapid-entry-v1', 'hangup-auto-vitals-recovery-v1', 'hangup-auto-vitals-flask-v1', 'hangup-auto-restart-delay-v1')) {
        $env:CBE_BATTLE_ENEMY_COUNT = '1'
        $env:CBE_BATTLE_ENEMY_HP = '20'
    }
    # The legacy input helper stays disabled for the native-auto-exit run.
    # Its 25/5 must originate from mmBattle's 0x5E92 sender after parsing
    # 25/2(result=1,type=1), never from a host-side confirmation event.
    $env:CBE_HANGUP_AUTO_CONFIRM = if ($Scenario -in @('hangup-auto-reward-continue-v1', 'hangup-auto-rapid-entry-v1', 'hangup-auto-vitals-recovery-v1', 'hangup-auto-vitals-flask-v1', 'hangup-auto-restart-delay-v1')) { '1' } else { '0' }
    if ($Scenario -eq 'hangup-auto-vitals-recovery-v1') {
        $env:CBE_BATTLE_RECOVER_HP = '15'
        $env:CBE_BATTLE_RECOVER_MP = '10'
    } else {
        Remove-Item Env:CBE_BATTLE_RECOVER_HP -ErrorAction SilentlyContinue
        Remove-Item Env:CBE_BATTLE_RECOVER_MP -ErrorAction SilentlyContinue
    }
    [pscustomobject]@{
        runner_scenario = $Scenario
        client_input_scenario = $clientScenario
        fixture_profile = $fixtureProfile
        service_port = $ServicePort
        admin_port = $AdminPort
        configured_recover_hp = $env:CBE_BATTLE_RECOVER_HP
        configured_recover_mp = $env:CBE_BATTLE_RECOVER_MP
        native_auto_exit = ($Scenario -eq 'hangup-native-auto-exit-v1')
        native_auto_exit_contract = if ($Scenario -eq 'hangup-native-auto-exit-v1') {
            'CBE_HANGUP_AUTO_CONFIRM=0; 25/2(result=1,type=1) -> mmBattle 0x8996 -> 0x5E92 -> client 25/5 -> scene poll next 4/5/4/6; no 0x60C8'
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
        $clientLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'client.stdout.log')
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
            'mock_hangup_battle_start source=scene-poll .*native_exit25_2=1 '
        ).Count
        $hostConfirm = [regex]::Matches(
            $clientLog,
            'reward_auto_confirm_input '
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
            host_confirm_inputs = $hostConfirm
            completion = $completion
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'native-auto-exit-evidence.json') -Encoding utf8
        if ($responseObject -ne 1 -or $parser8996 -ne 1 -or $sender5e92 -ne 1 -or
            $manualExit -ne 0 -or $exitUplink -ne 1 -or $serverRoundComplete -lt 1 -or
            $scenePollStart -lt 1 -or $hostConfirm -ne 0 -or $completion -ne 1) {
            throw 'native auto-exit evidence missing: expected 25/2->0x8996->0x5e92->client 25/5->scene poll, with no 0x60C8 or host confirmation input'
        }
    }
    if ($Scenario -in @('hangup-auto-rapid-entry-v1', 'hangup-auto-vitals-recovery-v1', 'hangup-auto-vitals-flask-v1', 'hangup-auto-restart-delay-v1')) {
        # The isolated server trace proves the terminal contract; the old
        # 4/11+4/9 terminal workaround is forbidden.  The vital-recovery
        # scenario intentionally stops after the first client-owned 25/5 and
        # its read-only scene-node assertion; restart timing is covered by its
        # dedicated scenario.
        $serverLogPath = Join-Path $runDir 'server.stdout.log'
        $serverLog = Get-Content -Raw -LiteralPath $serverLogPath
        $rewardedSettlements = [regex]::Matches($serverLog, 'mock_battle_settle .*reward_claimed=1').Count
        $suppressedSettlements = [regex]::Matches($serverLog, 'mock_battle_settle .*reward_claimed=0').Count
        $legacyClose = [regex]::IsMatch($serverLog, 'mock_battle_terminal_close_deliver .*response=4/11\+4/9')
        # `01桃花岛_01.sce` begins with five b_flowers01 placements.  Its
        # first actor_id=105 combat record is therefore client scene row 6,
        # not combat ordinal 1.  The old ordinal response sometimes survived
        # by the client scanning an already-active node, but it crashes when
        # that node is not active at the first battle frame.  Require the
        # direct 4/5 index contract rather than accepting that accidental scan.
        $runtimeSceneStarts = [regex]::Matches(
            $serverLog,
            'mock_hangup_battle_start .*subtype=5 runtime_index=6 pos=\(295,57\) target_source=sce-static-node-order'
        ).Count
        $vitalRecoveryPattern = if ($Scenario -eq 'hangup-auto-vitals-flask-v1') {
            'mock_battle_settle .*vitals=95/295,80/205 recover=15/10 auto_recover=15/10 configured_recover=0/0 .*role=810001'
        } else {
            'mock_battle_settle .*vitals=95/295,80/205 recover=15/10 auto_recover=0/0 configured_recover=15/10 .*role=810001'
        }
        $vitalRecoveryRows = [regex]::Matches($serverLog, $vitalRecoveryPattern).Count
        $flaskConsumptionRows = [regex]::Matches(
            $serverLog,
            'mock_battle_auto_flask role=810001 hp=15 mp=10 rows=2 response=4/7\+7/11'
        ).Count
        $nextBattleStartsFromRecoveredVitals = [regex]::Matches(
            $serverLog,
            'mock_hangup_battle_start .*roleid=810001 rolehp=95/295 rolemp=80/205 '
        ).Count
        # The scene probe is a read-only snapshot emitted after the native
        # 4/7 parser has returned to the scene renderer.  It is evidence that
        # the client-owned actor state received the same delta; no CBE memory
        # is written by this runner.
        $clientAutomationLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'automation.log')
        $clientSceneVitalRows = [regex]::Matches(
            $clientAutomationLog,
            'automation_hangup_vital_scene_observed .*actorId=810001 battleHp=95/295 battleMp=80/205'
        ).Count
        # InitActionSlot_B (mmBattle 0x6DBC) reads 4/6.teaminfo before the
        # terminal 4/7 is parsed.  Require the serialised action cache value
        # to match the later 4/7 result, not merely the service's next start.
        $terminalActionVitalRows = [regex]::Matches(
            $serverLog,
            'mock_battle_terminal_action_vitals role=810001 hp=95/295 mp=80/205 source=4/6-teaminfo-before-4/7'
        ).Count
        $restartDeadlineRows = [regex]::Matches(
            $serverLog,
            'scene_hangup_round_complete .*next_tick=(\d+) delay_ms=5000 '
        )
        $restartDelaySatisfied = $false
        foreach ($deadline in $restartDeadlineRows) {
            $following = $serverLog.Substring($deadline.Index + $deadline.Length)
            $nextStart = [regex]::Match($following, 'mock_hangup_battle_start .*tick=(\d+) ')
            $deadlineTick = [Convert]::ToUInt32($deadline.Groups[1].Value)
            $nextStartTick = if ($nextStart.Success) {
                [Convert]::ToUInt32($nextStart.Groups[1].Value)
            } else {
                0
            }
            if ($nextStart.Success -and $nextStartTick -ge $deadlineTick) {
                $restartDelaySatisfied = $true
                break
            }
        }
        [pscustomobject]@{
            rewarded_settlements = $rewardedSettlements
            suppressed_settlements = $suppressedSettlements
            legacy_terminal_close = $legacyClose
            runtime_scene_starts = $runtimeSceneStarts
            vital_recovery_rows = $vitalRecoveryRows
            flask_consumption_rows = $flaskConsumptionRows
            next_battle_starts_from_recovered_vitals = $nextBattleStartsFromRecoveredVitals
            client_scene_vital_rows = $clientSceneVitalRows
            terminal_action_vital_rows = $terminalActionVitalRows
            restart_deadline_rows = $restartDeadlineRows.Count
            restart_delay_satisfied = $restartDelaySatisfied
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'rapid-entry-evidence.json') -Encoding utf8
        $isVitalScenario = $Scenario -in @('hangup-auto-vitals-recovery-v1', 'hangup-auto-vitals-flask-v1')
        $requiredSettlements = if ($isVitalScenario) { 1 } else { 2 }
        $requiredRuntimeStarts = if ($isVitalScenario) { 1 } else { 2 }
        if ($rewardedSettlements -lt $requiredSettlements -or $suppressedSettlements -ne 0 -or $legacyClose -or $runtimeSceneStarts -lt $requiredRuntimeStarts) {
            throw 'hangup contract evidence missing: expected the required rewarded 4/7 settlements, no suppressed settlement, no 4/11+4/9 terminal close, and the required native runtime-index 4/5 starts'
        }
        if ($isVitalScenario -and
            ($vitalRecoveryRows -lt 1 -or $clientSceneVitalRows -lt 1 -or
             $terminalActionVitalRows -lt 1)) {
            throw 'vital recovery contract evidence missing: expected matching terminal 4/6 teaminfo and 4/7 recovery plus the client scene actor HP/MP 95/295,80/205 after its native 25/5 exit'
        }
        if ($Scenario -eq 'hangup-auto-vitals-flask-v1' -and $flaskConsumptionRows -lt 1) {
            throw 'vital flask contract evidence missing: expected one 802/803 reservoir settlement with 15/10 recovery and two native 7/11 count updates'
        }
        if ($Scenario -eq 'hangup-auto-restart-delay-v1' -and
            (-not $restartDelaySatisfied)) {
            throw 'hangup restart delay evidence missing: expected a 5000 ms round deadline and the following native 4/5 start at or after that deadline'
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
