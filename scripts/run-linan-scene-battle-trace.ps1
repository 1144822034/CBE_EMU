[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$MonsterId = 1,
    [string]$ClientPath,
    [string]$PlayerLogDirectory
)

$ErrorActionPreference = 'Stop'
$scenario = 'linan-scene-battle-trace-v1'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if ([string]::IsNullOrWhiteSpace($ClientPath)) {
    $ClientPath = Join-Path $repo 'bin\main.exe'
}
if ([string]::IsNullOrWhiteSpace($PlayerLogDirectory)) {
    $PlayerLogDirectory = Join-Path $repo 'bin\multiplayer-data\player-3\logs'
}

$ClientPath = (Resolve-Path -LiteralPath $ClientPath -ErrorAction Stop).Path
$PlayerLogDirectory = (Resolve-Path -LiteralPath $PlayerLogDirectory -ErrorAction Stop).Path
$clientName = [System.IO.Path]::GetFileNameWithoutExtension($ClientPath)
$runningClient = @(Get-Process -Name $clientName -ErrorAction SilentlyContinue)
if ($runningClient.Count -ne 0) {
    throw "Refusing to start a second client. Exit the existing $clientName.exe process and run this script again."
}

$runStamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$runId = "$scenario-$runStamp-$PID"
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) {
    throw "Refusing to overwrite evidence directory: $runDir"
}
$null = New-Item -ItemType Directory -Path $runDir

$logNames = @(
    'sce-entity-callback.log',
    'scene-battle-collision.log',
    'actor-resource-cache.log',
    'actor-scene-node-capacity.log'
)

function Get-LogSnapshot([string]$Name) {
    $path = Join-Path $PlayerLogDirectory $Name
    if (-not (Test-Path -LiteralPath $path)) {
        return [pscustomobject]@{ Name = $Name; Path = $path; Exists = $false; Length = 0 }
    }
    $item = Get-Item -LiteralPath $path
    return [pscustomobject]@{ Name = $Name; Path = $path; Exists = $true; Length = [int64]$item.Length }
}

function Export-LogDelta($Before) {
    $target = Join-Path $runDir $Before.Name
    if (-not (Test-Path -LiteralPath $Before.Path)) {
        return [pscustomobject]@{ log = $Before.Name; captured = 'missing'; bytes = 0 }
    }

    $after = Get-Item -LiteralPath $Before.Path
    if (-not $Before.Exists -or $after.Length -lt $Before.Length) {
        Copy-Item -LiteralPath $Before.Path -Destination $target -Force
        return [pscustomobject]@{ log = $Before.Name; captured = 'full'; bytes = [int64]$after.Length }
    }

    $input = [System.IO.File]::Open($Before.Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    $output = [System.IO.File]::Open($target, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        [void]$input.Seek($Before.Length, [System.IO.SeekOrigin]::Begin)
        $input.CopyTo($output)
    }
    finally {
        $output.Dispose()
        $input.Dispose()
    }
    return [pscustomobject]@{ log = $Before.Name; captured = 'delta'; bytes = [int64]($after.Length - $Before.Length) }
}

$snapshots = @($logNames | ForEach-Object { Get-LogSnapshot $_ })
$traceNames = @(
    'CBE_TRACE_SCE_ENTITY_CALLBACK',
    'CBE_TRACE_SCENE_BATTLE_COLLISION',
    'CBE_TRACE_SCE_NODE_ACTOR_ID'
)
$previousEnvironment = @{}
$clientProcess = $null

try {
    foreach ($name in $traceNames) {
        $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_TRACE_SCE_ENTITY_CALLBACK = '1'
    $env:CBE_TRACE_SCENE_BATTLE_COLLISION = '1'
    $env:CBE_TRACE_SCE_NODE_ACTOR_ID = [string]$MonsterId

    [pscustomobject]@{
        scenario = $scenario
        run_id = $runId
        monster_id = $MonsterId
        client = $ClientPath
        player_logs = $PlayerLogDirectory
        trace_environment = @{
            CBE_TRACE_SCE_ENTITY_CALLBACK = '1'
            CBE_TRACE_SCENE_BATTLE_COLLISION = '1'
            CBE_TRACE_SCE_NODE_ACTOR_ID = [string]$MonsterId
        }
        procedure = @(
            'Log in and enter c04临安府_01.sce.',
            'Walk directly through the deployed fireball.',
            'Close the client after the result is visible.'
        )
        expected_evidence = @(
            'kind-2 scene node or its absence for the selected monster ID',
            'collision callback result and nearest distance',
            'WT 4/1 presence or absence in the service log'
        )
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runDir 'test-plan.json') -Encoding utf8

    $clientProcess = Start-Process -FilePath $ClientPath -WorkingDirectory (Split-Path -Parent $ClientPath) -PassThru
    [pscustomobject]@{ client = $clientProcess.Id } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Write-Host "Trace client started (PID $($clientProcess.Id)). Reproduce the c04临安府_01 fireball collision, then close the client."
    $clientProcess.WaitForExit()
}
finally {
    foreach ($name in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
    }
    $captures = @($snapshots | ForEach-Object { Export-LogDelta $_ })
    [pscustomobject]@{
        scenario = $scenario
        run_id = $runId
        client_pid = if ($null -eq $clientProcess) { $null } else { $clientProcess.Id }
        client_exit_code = if ($null -eq $clientProcess -or -not $clientProcess.HasExited) { $null } else { $clientProcess.ExitCode }
        captures = $captures
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runDir 'result.json') -Encoding utf8
    Write-Host "Evidence directory: $runDir"
}
