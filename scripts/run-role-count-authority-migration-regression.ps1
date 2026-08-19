param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($env:CBE_AUTOMATION_MYSQL_PASSWORD)) {
    throw 'set CBE_AUTOMATION_MYSQL_PASSWORD before running isolated automation'
}
$database = 'cbe_auto_role_count_' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff') + '_' + $PID
$testSource = Join-Path $PSScriptRoot 'role-count-authority-migration-regression.c'
$testExe = Join-Path $root 'tmp\role-count-authority-migration-regression.exe'
$mysql = if ($env:CBE_AUTOMATION_MYSQL_CLIENT) {
    (Resolve-Path -LiteralPath $env:CBE_AUTOMATION_MYSQL_CLIENT -ErrorAction Stop).Path
} else {
    (Get-Command mysql -ErrorAction Stop).Source
}
$mysqlHost = if ($env:CBE_AUTOMATION_MYSQL_HOST) { $env:CBE_AUTOMATION_MYSQL_HOST } else { '127.0.0.1' }
$mysqlPort = if ($env:CBE_AUTOMATION_MYSQL_PORT) { $env:CBE_AUTOMATION_MYSQL_PORT } else { '3306' }
$mysqlUser = if ($env:CBE_AUTOMATION_MYSQL_USER) { $env:CBE_AUTOMATION_MYSQL_USER } else { 'root' }
$oldEnvironment = @{}
foreach ($name in @('CBE_MYSQL_HOST','CBE_MYSQL_PORT','CBE_MYSQL_USER',
                     'CBE_MYSQL_PASSWORD','CBE_MYSQL_DATABASE',
                     'CBE_TEST_MYSQL_DATABASE','CBE_RESOURCE_ROOT','MYSQL_PWD')) {
    $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    $env:MYSQL_PWD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    & $mysql -h $mysqlHost -P $mysqlPort -u $mysqlUser -N -e `
        "CREATE DATABASE $database CHARACTER SET utf8mb4 COLLATE utf8mb4_bin"
    if ($LASTEXITCODE -ne 0) { throw 'failed to create isolated role-count migration database' }
    Write-Output 'created isolated role-count migration database'
    $env:CBE_MYSQL_HOST = $mysqlHost
    $env:CBE_MYSQL_PORT = $mysqlPort
    $env:CBE_MYSQL_USER = $mysqlUser
    $env:CBE_MYSQL_PASSWORD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    $env:CBE_MYSQL_DATABASE = $database
    $env:CBE_TEST_MYSQL_DATABASE = $database
    $env:CBE_RESOURCE_ROOT = Join-Path $root 'web\fs'
    Push-Location $root
    try {
        & gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w `
            $testSource src\gifDecode.c src\mystd.c src\mysql-client.c src\md5.c `
            -o $testExe '-Wl,--gc-sections' -lpthread -liconv -lm -lkernel32 -lws2_32
        if ($LASTEXITCODE -ne 0) { throw 'failed to compile role-count migration regression' }
        & $testExe
        if ($LASTEXITCODE -ne 0) { throw 'role-count migration regression failed' }
    } finally {
        Pop-Location
    }
} finally {
    foreach ($name in $oldEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
    }
    $env:MYSQL_PWD = $env:CBE_AUTOMATION_MYSQL_PASSWORD
    & $mysql -h $mysqlHost -P $mysqlPort -u $mysqlUser -N -e `
        "DROP DATABASE IF EXISTS $database"
    if ($LASTEXITCODE -ne 0) { throw 'failed to remove isolated role-count migration database' }
    [Environment]::SetEnvironmentVariable('MYSQL_PWD', $oldEnvironment['MYSQL_PWD'], 'Process')
    Write-Output 'removed isolated role-count migration database'
}
