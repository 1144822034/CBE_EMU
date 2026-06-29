$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $repoRoot "web\build"
$files = @("cbe-emu.js", "cbe-emu.wasm", "cbe-emu.data")

foreach ($file in $files) {
    $path = Join-Path $buildDir $file
    if (-not (Test-Path $path)) {
        throw "missing wasm build output: $path"
    }
}

$hashes = @{}
$sizes = @{}
$fileSha = [System.Security.Cryptography.SHA256]::Create()
foreach ($file in $files) {
    $path = Join-Path $buildDir $file
    try {
        $bytes = [System.IO.File]::ReadAllBytes($path)
        $hashes[$file] = [System.BitConverter]::ToString($fileSha.ComputeHash($bytes)).Replace("-", "").ToLowerInvariant()
        $sizes[$file] = $bytes.Length
    } finally {
        $bytes = $null
    }
}
$fileSha.Dispose()

$versionSource = ($files | ForEach-Object { $_ + ":" + $hashes[$_] }) -join "|"
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $versionBytes = [System.Text.Encoding]::UTF8.GetBytes($versionSource)
    $versionHash = [System.BitConverter]::ToString($sha.ComputeHash($versionBytes)).Replace("-", "").ToLowerInvariant()
} finally {
    $sha.Dispose()
}

$manifest = [ordered]@{
    version = $versionHash.Substring(0, 16)
    createdAtUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    files = [ordered]@{
        "cbe-emu.js" = [ordered]@{
            size = $sizes["cbe-emu.js"]
            sha256 = $hashes["cbe-emu.js"]
        }
        "cbe-emu.wasm" = [ordered]@{
            size = $sizes["cbe-emu.wasm"]
            sha256 = $hashes["cbe-emu.wasm"]
        }
        "cbe-emu.data" = [ordered]@{
            size = $sizes["cbe-emu.data"]
            sha256 = $hashes["cbe-emu.data"]
        }
    }
}

$json = $manifest | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText((Join-Path $buildDir "cbe-version.json"), $json + "`n", [System.Text.Encoding]::UTF8)
