param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$testSource = Join-Path $PSScriptRoot 'scene-battle-task-objective-regression.c'
$testExe = Join-Path $root 'tmp\scene-battle-task-objective-regression.exe'

Push-Location $root
try {
    & gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 `
        -ffunction-sections -fdata-sections -w $testSource `
        src\gifDecode.c src\mystd.c src\mysql-client.c src\md5.c `
        -o $testExe '-Wl,--gc-sections' -lpthread -liconv -lm `
        -lkernel32 -lws2_32
    if ($LASTEXITCODE -ne 0) {
        throw "scene battle task objective regression compile failed: $LASTEXITCODE"
    }
    & $testExe
    if ($LASTEXITCODE -ne 0) {
        throw "scene battle task objective regression failed: $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
