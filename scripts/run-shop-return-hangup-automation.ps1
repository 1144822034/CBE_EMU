[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$ServicePort = 19190,
    [ValidateRange(1024, 65535)]
    [int]$AdminPort = 19191,
    [ValidateSet('shop-return-hangup-v1', 'direct-hangup-control-v1', 'title-module-update-v1', 'scene-teleport-stone-probe-v1', 'dream-clock-probe-v1', 'dream-npc-entry-clock-probe-v1', 'equipment-enhance-rules-probe-v1', 'equipment-enhance-bag-probe-v1', 'equipment-enhance-stage1-probe-v1', 'hangup-auto-cancel-v1', 'hangup-auto-terminal-v1', 'hangup-native-auto-exit-v1')]
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
function Get-Sha256Hex([string]$Path) {
    # Keep the runner compatible with the legacy powershell.exe installed on
    # this host, where Get-FileHash is unavailable.
    $hashAlgorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        return ([BitConverter]::ToString($hashAlgorithm.ComputeHash($stream))).Replace('-', '')
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        $hashAlgorithm.Dispose()
    }
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
function Invoke-IsolatedAdminFormPost([int]$Port, [string]$Path,
                                      [string]$Body, [string]$Cookie = '') {
    # Keep the admin deployment on its normal HTTP form route while avoiding
    # WinHTTP/IE-engine differences between Windows PowerShell installations.
    # This is a loopback-only client for the service started by this runner;
    # it neither calls an in-process admin function nor observes user state.
    $tcp = New-Object System.Net.Sockets.TcpClient
    $stream = $null
    $memory = $null
    try {
        $payload = [Text.Encoding]::UTF8.GetBytes($Body)
        $tcp.Connect('127.0.0.1', $Port)
        $stream = $tcp.GetStream()
        $stream.ReadTimeout = 5000
        $stream.WriteTimeout = 5000
        $header = "POST $Path HTTP/1.1`r`nHost: 127.0.0.1:$Port`r`nConnection: close`r`nContent-Type: application/x-www-form-urlencoded`r`nContent-Length: $($payload.Length)`r`n"
        if (-not [string]::IsNullOrEmpty($Cookie)) {
            $header += "Cookie: $Cookie`r`n"
        }
        $header += "`r`n"
        $headerBytes = [Text.Encoding]::ASCII.GetBytes($header)
        $stream.Write($headerBytes, 0, $headerBytes.Length)
        $stream.Write($payload, 0, $payload.Length)
        $stream.Flush()
        $memory = New-Object System.IO.MemoryStream
        $buffer = New-Object byte[] 4096
        while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $memory.Write($buffer, 0, $read)
        }
        $response = $memory.ToArray()
        $headerEnd = -1
        for ($index = 0; $index + 3 -lt $response.Length; ++$index) {
            if ($response[$index] -eq 13 -and $response[$index + 1] -eq 10 -and
                $response[$index + 2] -eq 13 -and $response[$index + 3] -eq 10) {
                $headerEnd = $index + 4
                break
            }
        }
        if ($headerEnd -lt 0) { throw 'isolated admin response had no complete HTTP header' }
        $responseHeader = [Text.Encoding]::ASCII.GetString($response, 0, $headerEnd)
        if ($responseHeader -notmatch '^HTTP/1\.[01]\s+(\d{3})\s') {
            throw 'isolated admin response had an invalid HTTP status line'
        }
        $statusCode = [int]$matches[1]
        $sessionCookie = ''
        if ($responseHeader -match '(?im)^Set-Cookie:\s*(cbe_admin=[^;\r\n]+)') {
            $sessionCookie = $matches[1]
        }
        return [pscustomobject]@{
            status_code = $statusCode
            session_cookie = $sessionCookie
        }
    } finally {
        if ($null -ne $memory) { $memory.Dispose() }
        if ($null -ne $stream) { $stream.Dispose() }
        $tcp.Dispose()
    }
}

Assert-Path $server 'server binary'
Assert-Path $client 'client binary'
Assert-Path $fixture 'automation fixture'
Assert-Path $resourceRoot 'server resource root'
$dreamClockScenario = $Scenario -in @('dream-clock-probe-v1', 'dream-npc-entry-clock-probe-v1')
$dreamClockSceneHash = '682ACD48190B5796A306FF8E7F0329F80E72DF62B03234107C3E7FE2BF57D81F'
$dreamClockSceneSource = $null
$dreamClockSceneName = $null
if ($dreamClockScenario) {
    # PowerShell 5.1 can decode a UTF-8-without-BOM script as the local ANSI
    # code page, so do not put the Chinese scene name in executable source.
    # The ASCII prefix, byte length, and content hash uniquely identify the
    # installed player-1 snapshot without any filename-encoding dependency.
    $dreamClockSceneDirectory = Join-Path $repo 'bin\JHOnlineData'
    Assert-Path $dreamClockSceneDirectory 'player-1 resource directory'
    $dreamClockSceneMatches = @(
        Get-ChildItem -LiteralPath $dreamClockSceneDirectory -File -Filter '29*.sce' |
            Where-Object {
                $_.Length -eq 411 -and
                (Get-Sha256Hex $_.FullName) -eq $dreamClockSceneHash
            }
    )
    if ($dreamClockSceneMatches.Count -ne 1) {
        throw "could not find exactly one 411-byte player-1 dream-scene resource with SHA-256 $dreamClockSceneHash"
    }
    $dreamClockSceneSource = $dreamClockSceneMatches[0].FullName
    $dreamClockSceneName = Split-Path -Path $dreamClockSceneSource -Leaf
}
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
$runnerStage = 'prepare-isolated-fixture'

try {
    & $php $fixture create $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture database create failed' }

    # The schema is available immediately after `create`.  Seed before the
    # service starts so startup migration sees one internally consistent test
    # account/role pair, just as it would in a populated deployment.  The
    # fixture writes only to the uniquely named automation schema.
    $fixtureProfile = if ($Scenario -eq 'dream-clock-probe-v1') {
        'dream-clock'
    } elseif ($Scenario -eq 'dream-npc-entry-clock-probe-v1') {
        'dream-npc-entry'
    } elseif ($Scenario -in @('scene-teleport-stone-probe-v1', 'title-module-update-v1')) {
        'teleport-stone-c00'
    } elseif ($Scenario -in @('equipment-enhance-rules-probe-v1', 'equipment-enhance-bag-probe-v1', 'equipment-enhance-stage1-probe-v1')) {
        'equipment-enhance'
    } else {
        'hangup-peach'
    }
    & $php $fixture seed $database $fixtureProfile | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append
    if ($LASTEXITCODE -ne 0) { throw 'fixture seed failed' }
    foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER','CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE','CBE_RESOURCE_ROOT','CBE_BATTLE_ENEMY_COUNT','CBE_BATTLE_ENEMY_HP','CBE_TRACE_SCENE_NUMBERS')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_MYSQL_HOST = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $env:CBE_MYSQL_PORT = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $env:CBE_MYSQL_USER = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = $serverResourceRoot
    if ($dreamClockScenario) {
        # Read-only client trace: it only records existing glyph blits after
        # the normal client-owned scene boundary.
        $env:CBE_TRACE_SCENE_NUMBERS = '1'
    }
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
        dream_clock_fixture_sha256 = if ($dreamClockScenario) { $dreamClockSceneHash } else { $null }
        dream_clock_fixture_bytes = if ($dreamClockScenario) { 411 } else { $null }
        dream_clock_server_base = if ($dreamClockScenario) { 'factory 278-byte scene; captured by formal isolated deploy' } else { $null }
        dream_clock_server_overlay = if ($dreamClockScenario) { 'generated by formal isolated deploy; SHA-256 must equal pinned player-1 snapshot' } else { $null }
        dream_npc_entry_input = if ($Scenario -eq 'dream-npc-entry-clock-probe-v1') {
            @('JianghuOL.CBE+0x015154 nearest-NPC prompt -> F', 'JianghuOL.CBE+0x0380e8 first 26/1 dialog parser -> F')
        } else { $null }
        service_port = $ServicePort
        admin_port = $AdminPort
        native_auto_exit = ($Scenario -eq 'hangup-native-auto-exit-v1')
        native_auto_exit_contract = if ($Scenario -eq 'hangup-native-auto-exit-v1') {
            '25/2(result=1,type=1) -> mmBattle 0x8996 -> 0x5E92 -> client 25/5 -> scene poll next 4/5/4/6; no 0x60C8'
        } else { $null }
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'test-plan.json') -Encoding utf8

    $runnerStage = 'start-predeploy-service'
    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru `
        -ArgumentList "--mock-service-only", "--mock-service-bind=127.0.0.1", "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    [pscustomobject]@{ server = $serverProcess.Id; client = $null } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort 15
    if ($dreamClockScenario) {
        # Do not copy a published SCE or deployment ledger into this run.
        # Authenticate a fixture-owned operator, then send the same local
        # admin action used by the editor against the isolated service and
        # schema only.  The filename is constructed from UTF-8 bytes to keep
        # this PowerShell 5.1-compatible source ASCII.
        $runnerStage = 'wait-predeploy-admin-listener'
        Wait-OwnedService $serverProcess $AdminPort 15
        $runnerStage = 'authenticate-isolated-admin'
        # The service deliberately keeps its management surface below a
        # non-root prefix.  Use the exact public routes declared by
        # web_admin_server.c; /login and /action are ordinary, non-admin
        # paths and therefore never establish the cbe_admin session needed
        # for this normal deploy operation.
        $adminBasePath = '/admin-418yz6'
        $adminLoginBody = 'account=dream_probe_admin&password=dream-probe-admin-v1'
        $adminLoginResponse = Invoke-IsolatedAdminFormPost $AdminPort "$adminBasePath/login" $adminLoginBody
        $adminSessionCookie = $adminLoginResponse.session_cookie
        if ($adminLoginResponse.status_code -ne 303 -or
            [string]::IsNullOrEmpty($adminSessionCookie)) {
            throw 'isolated admin login did not establish exactly one private session'
        }
        $adminLoginBody = $null
        $runnerStage = 'submit-formal-scene-deploy'
        $dreamClockSceneUtf8 = [Text.Encoding]::UTF8.GetString([byte[]](
            0x32,0x39,0xe6,0xa2,0xa6,0xe5,0xa2,0x83,0xe7,0xa9,0xba,0xe9,0x97,0xb4,
            0x5f,0x30,0x33,0x2e,0x73,0x63,0x65
        ))
        $deployBody = 'action=deploy-scene-battle-monsters&scene=' +
            [Uri]::EscapeDataString($dreamClockSceneUtf8)
        $deployResponse = Invoke-IsolatedAdminFormPost $AdminPort "$adminBasePath/action" $deployBody $adminSessionCookie
        $dreamClockOverlayPath = Join-Path (Join-Path (Join-Path $serverResourceRoot '.cbe-overlays') $database) $dreamClockSceneName
        if ($deployResponse.status_code -ne 303 -or -not (Test-Path -LiteralPath $dreamClockOverlayPath)) {
            throw 'formal isolated dream-scene deploy did not produce its private overlay'
        }
        $dreamClockOverlayHash = Get-Sha256Hex $dreamClockOverlayPath
        [pscustomobject]@{
            action = 'deploy-scene-battle-monsters'
            scene_utf8_byte_count = 21
            authenticated_fixture_operator = 'dream_probe_admin'
            private_session_cookie_received = -not [string]::IsNullOrEmpty($adminSessionCookie)
            login_http_status = $adminLoginResponse.status_code
            http_status = $deployResponse.status_code
            overlay_path = $dreamClockOverlayPath
            overlay_bytes = (Get-Item -LiteralPath $dreamClockOverlayPath).Length
            overlay_sha256 = $dreamClockOverlayHash
            expected_sha256 = $dreamClockSceneHash
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'dream-formal-deploy-evidence.json') -Encoding utf8
        if ($dreamClockOverlayHash -ne $dreamClockSceneHash) {
            throw "formal isolated dream-scene deploy hash mismatch: expected $dreamClockSceneHash, got $dreamClockOverlayHash"
        }
        # Dynamic NPC rows are validated while a service starts.  Restart the
        # process we own only after the authenticated deploy commits, so its
        # normal DB load observes the generated overlay and deployment ledger.
        # This is the same persisted-content boundary as a real service
        # restart; no client state, packet or CBE memory is adjusted.
        $runnerStage = 'restart-after-formal-deploy'
        Stop-OwnedProcess $serverProcess
        # Start-Process may recreate redirected output files.  Preserve the
        # authenticated-login and deploy server evidence before launching the
        # post-deploy service that will own the game session.
        Copy-Item -LiteralPath (Join-Path $runDir 'server.stdout.log') `
            -Destination (Join-Path $runDir 'server-predeploy.stdout.log') -Force
        Copy-Item -LiteralPath (Join-Path $runDir 'server.stderr.log') `
            -Destination (Join-Path $runDir 'server-predeploy.stderr.log') -Force
        $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru `
            -ArgumentList "--mock-service-only", "--mock-service-bind=127.0.0.1", "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$serverResourceRoot" `
            -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
            -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
        [pscustomobject]@{ server = $serverProcess.Id; client = $null } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
        Wait-OwnedService $serverProcess $ServicePort 15
        Wait-OwnedService $serverProcess $AdminPort 15
        if ($Scenario -eq 'dream-npc-entry-clock-probe-v1' -and
            -not ((Get-Content -Raw -LiteralPath (Join-Path $runDir 'server.stdout.log')) -match
                  'dynamic_npc_db_load rows=1 skipped=0')) {
            throw 'published dream scene did not re-enable its NPC on isolated service restart'
        }
    }

    $runnerStage = 'prepare-isolated-client'
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
    $runnerStage = 'start-isolated-client'
    $clientProcess = Start-Process -FilePath (Join-Path $clientDir 'main.exe') -WorkingDirectory $clientDir -PassThru `
        -ArgumentList $clientArguments `
        -RedirectStandardOutput (Join-Path $runDir 'client.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'client.stderr.log')
    [pscustomobject]@{ server = $serverProcess.Id; client = $clientProcess.Id } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    $clientWaitMs = if ($Scenario -eq 'dream-npc-entry-clock-probe-v1') { 220000 } else { 190000 }
    $runnerStage = 'wait-for-client-scenario'
    if (-not $clientProcess.WaitForExit($clientWaitMs)) {
        throw 'automation client exceeded its bounded scenario timeout plus shutdown margin'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $runDir 'result.json'))) {
        throw 'automation client exited without result.json'
    }
    $result = Get-Content -Raw -LiteralPath (Join-Path $runDir 'result.json') | ConvertFrom-Json
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'runner-result.json') -Encoding utf8
    Write-Host "automation run directory: $runDir"
    if ($dreamClockScenario) {
        $sceneNumberLog = Join-Path $clientDir 'logs\scene-number-draw.log'
        if (Test-Path -LiteralPath $sceneNumberLog) {
            Copy-Item -LiteralPath $sceneNumberLog -Destination (Join-Path $runDir 'scene-number-draw.log') -Force
        }
    }
    if ($result.result -ne 'passed') {
        throw "scenario failed at $($result.stage): $($result.reason)"
    }
    if ($dreamClockScenario) {
        $sceneNumberLog = Join-Path $runDir 'scene-number-draw.log'
        if ($result.reason -ne 'dream-map-number-draw-observed' -or
            -not (Test-Path -LiteralPath $sceneNumberLog) -or
            -not ((Get-Content -Raw -LiteralPath $sceneNumberLog) -match 'atlas=combat-number-atlas')) {
            throw 'dream-clock probe passed without its exact map-number draw reason and atlas trace'
        }
        if ($Scenario -eq 'dream-npc-entry-clock-probe-v1') {
            $serverLog = Get-Content -Raw -LiteralPath (Join-Path $runDir 'server.stdout.log')
            if (-not ($serverLog -match 'mock_npc_dialog actor=30406') -or
                -not ($serverLog -match 'mock_npc_instance_enter actor=30406')) {
                throw 'dream NPC probe passed without the two required server-side 26/1 entry boundaries'
            }
        }
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
    $runnerStage = 'completed'
    Write-Host "$Scenario passed"
}
catch {
    # The BAT is deliberately thin, so retain the first PowerShell failure in
    # the per-run directory even when the console is closed by double-click.
    # Do not serialize request bodies, session tokens or database passwords.
    [pscustomobject]@{
        scenario = $Scenario
        stage = $runnerStage
        message = $_.Exception.Message
        exception_type = $_.Exception.GetType().FullName
        client_started = ($null -ne $clientProcess)
        service_started = ($null -ne $serverProcess)
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'runner-failure.json') -Encoding utf8
    throw
}
finally {
    try { Stop-OwnedProcess $clientProcess } catch { Write-Warning "could not stop owned client: $_" }
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
    if (-not $KeepDatabase -and $database -match '^jh_online_autotest_[0-9a-f]{16,32}$') {
        try { & $php $fixture cleanup $database | Tee-Object -FilePath (Join-Path $runDir 'fixture.log') -Append } catch { Write-Warning "could not remove isolated test database: $_" }
    }
}
