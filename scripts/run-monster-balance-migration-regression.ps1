param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}
$php = (Get-Command php -ErrorAction Stop).Source
$database = 'cbe_auto_monster_balance_' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff') + '_' + $PID
$fixture = Join-Path $PSScriptRoot 'monster-balance-migration-fixture.php'
$testSource = Join-Path $PSScriptRoot 'monster-balance-migration-regression.c'
$testExe = Join-Path $root 'tmp\monster-balance-migration-regression.exe'
$oldEnvironment = @{}
foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER',
                     'CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE',
                     'CBE_TEST_MYSQL_DATABASE','CBE_RESOURCE_ROOT')) {
    $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    & $php $fixture setup $database
    $env:CBE_MYSQL_HOST = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
    $env:CBE_MYSQL_PORT = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
    $env:CBE_MYSQL_USER = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_TEST_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = Join-Path $root 'web\fs'
    Push-Location $root
    try {
        & gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w `
            $testSource src\gifDecode.c src\mystd.c src\mysql-client.c src\md5.c `
            -o $testExe '-Wl,--gc-sections' -lpthread -liconv -lm -lkernel32 -lws2_32
        if ($LASTEXITCODE -ne 0) { throw 'failed to compile monster balance migration regression' }
        & $testExe
        if ($LASTEXITCODE -ne 0) { throw 'monster balance migration regression failed' }
    } finally {
        Pop-Location
    }
} finally {
    foreach ($name in $oldEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
    }
    & $php $fixture cleanup $database
    if ($LASTEXITCODE -ne 0) { throw 'failed to remove isolated monster balance database' }
}
