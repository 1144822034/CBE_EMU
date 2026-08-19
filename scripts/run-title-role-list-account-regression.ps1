[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)] [int]$ServicePort = 19342,
    [ValidateRange(1024, 65535)] [int]$AdminPort = 19343,
    [string]$Database = 'jh_online_release',
    [string[]]$Account = @('guest00723', '21642502')
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$server = Join-Path $repo 'bin\jh-online-server.exe'
$regression = Join-Path $PSScriptRoot 'title-role-list-account-regression.php'
$resourceRoot = Join-Path $repo 'web\fs\JHOnlineData'

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
        Stop-Process -Id $Process.Id
        $Process.WaitForExit(5000)
    }
}

if ($Database -ne 'jh_online_release') { throw 'this diagnostic is limited to jh_online_release' }
if ($Account.Count -eq 0 -or @($Account | Where-Object { [string]::IsNullOrWhiteSpace($_) }).Count -ne 0) {
    throw 'at least one non-empty account is required'
}
if ($ServicePort -eq $AdminPort) { throw 'service and admin ports must differ' }
foreach ($path in @($server, $regression, $resourceRoot)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "required path not found: $path" }
}
foreach ($port in @($ServicePort, $AdminPort)) {
    if (Test-ListeningPort $port) { throw "refusing occupied diagnostic port $port" }
}

$runId = 'title-role-list-account-v1-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $PID
$runDir = Join-Path $repo "artifacts\automation\$runId"
if (Test-Path -LiteralPath $runDir) { throw "refusing to overwrite $runDir" }
$null = New-Item -ItemType Directory -Path $runDir
@{
    scenario = 'title-role-list-account-v1'; database = $Database; accounts = $Account
    service_port = $ServicePort; admin_port = $AdminPort
    max_steps = 3; total_timeout_seconds = 45; step_timeout_seconds = 10
    trigger_rule = 'once'; max_repetitions = 1
    input = @('CBMS 1/1/12 account login', 'CBMS 1/1/16 title role-list stage',
        'CBMS 1/1/4 selected server')
    assertions = @('server binds each account to its request client ID',
        '1/1/4 contains compact actorinfo', 'actorinfo count and role IDs match account_roles')
    failure_conditions = @('service startup failure', 'role DB relational load failure',
        'missing actorinfo', 'zero or mismatched actorinfo role list')
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $runDir 'run-context.json') -Encoding utf8

$php = @(Get-Command php -All -ErrorAction Stop | Where-Object { $_.Source -match 'php8\.' } |
    Select-Object -ExpandProperty Source -First 1)[0]
if ([string]::IsNullOrEmpty($php)) { $php = (Get-Command php -ErrorAction Stop).Source }
$oldEnvironment = @{}
$serverProcess = $null
try {
    foreach ($name in @('CBE_MYSQL_HOST', 'CBE_MYSQL_PORT', 'CBE_MYSQL_USER',
                         'CBE_MYSQL_PASSWORD', 'CBE_MYSQL_DATABASE', 'CBE_RESOURCE_ROOT')) {
        $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CBE_MYSQL_HOST = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $env:CBE_MYSQL_PORT = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $env:CBE_MYSQL_USER = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_PASSWORD = if ($env:CBE_AUTOMATION_MYSQL_PASSWORD) { $env:CBE_AUTOMATION_MYSQL_PASSWORD } else { '123456' }
    $env:CBE_MYSQL_DATABASE = $Database
    $env:CBE_RESOURCE_ROOT = $resourceRoot

    $serverProcess = Start-Process -FilePath $server -WorkingDirectory $runDir -PassThru -WindowStyle Hidden `
        -ArgumentList '--mock-service-only', '--mock-service-bind=127.0.0.1',
            "--mock-service-port=$ServicePort", "--mock-admin-port=$AdminPort", "--resource-root=$resourceRoot" `
        -RedirectStandardOutput (Join-Path $runDir 'server.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'server.stderr.log')
    @{ server = $serverProcess.Id } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding utf8
    Wait-OwnedService $serverProcess $ServicePort
    foreach ($currentAccount in $Account) {
        & $php $regression verify $ServicePort $Database $currentAccount $runDir |
            Tee-Object -FilePath (Join-Path $runDir 'regression.log') -Append
        if ($LASTEXITCODE -ne 0) { throw "title role-list account regression failed account=$currentAccount" }
    }
    Stop-OwnedProcess $serverProcess
    $serverProcess = $null

    $serverLog = Get-Content -LiteralPath (Join-Path $runDir 'server.stdout.log') -Raw
    if ($serverLog -match 'mock_role_db_mysql_load_failed') {
        throw 'role database loader failed; see server.stdout.log'
    }
    foreach ($currentAccount in $Account) {
        $escapedAccount = [regex]::Escape($currentAccount)
        if ($serverLog -notmatch "mock_role_db_mysql_load account=$escapedAccount source=relational roles=[1-5] active=[1-9][0-9]*") {
            throw "missing relational role-loader evidence for account=$currentAccount"
        }
    }
    Write-Host "title-role-list-account-v1 passed; evidence: $runDir"
}
finally {
    try { Stop-OwnedProcess $serverProcess } catch { Write-Warning "could not stop owned service: $_" }
    foreach ($name in $oldEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
    }
}
