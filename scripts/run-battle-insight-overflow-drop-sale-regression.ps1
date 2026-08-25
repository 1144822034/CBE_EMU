param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$testSource = Join-Path $PSScriptRoot 'battle-insight-overflow-drop-sale-regression.c'
$testExe = Join-Path $root 'tmp\battle-insight-overflow-drop-sale-regression.exe'

Push-Location $root
try {
    & gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 `
        -ffunction-sections -fdata-sections -w $testSource `
        src\gifDecode.c src\mystd.c src\mysql-client.c src\md5.c `
        -o $testExe '-Wl,--gc-sections' -lpthread -liconv -lm -lkernel32 -lws2_32
    if ($LASTEXITCODE -ne 0) {
        throw 'failed to compile battle insight overflow-drop sale regression'
    }
    & $testExe
    if ($LASTEXITCODE -ne 0) {
        throw 'battle insight overflow-drop sale regression failed'
    }
} finally {
    Pop-Location
}
