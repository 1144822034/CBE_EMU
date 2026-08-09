$ErrorActionPreference = "Stop"
$dest = "d:\works\my\jianghu\CBE_EMU_pay\PixiJS_web\reference-shots"
$src = "d:\works\my\jianghu\CBE_EMU_pay\bin\autotest\screens"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Add-Type -AssemblyName System.Drawing

$toolbar = 32
$gameW = 240
$gameH = 400

Get-ChildItem $src -Filter "*.bmp" | ForEach-Object {
  $bmpSrc = [System.Drawing.Bitmap]::FromFile($_.FullName)
  $fullPng = Join-Path $dest ($_.BaseName + ".png")
  $bmpSrc.Save($fullPng, [System.Drawing.Imaging.ImageFormat]::Png)

  $crop = New-Object System.Drawing.Bitmap $gameW, $gameH
  $g = [System.Drawing.Graphics]::FromImage($crop)
  $srcY = if ($bmpSrc.Height -ge ($toolbar + $gameH)) { $toolbar } else { 0 }
  $srcRect = New-Object System.Drawing.Rectangle 0, $srcY, $gameW, $gameH
  $dstRect = New-Object System.Drawing.Rectangle 0, 0, $gameW, $gameH
  $g.DrawImage($bmpSrc, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
  $g.Dispose()
  $cropPath = Join-Path $dest ($_.BaseName + "-game.png")
  $crop.Save($cropPath, [System.Drawing.Imaging.ImageFormat]::Png)
  $crop.Dispose()
  $bmpSrc.Dispose()
  Write-Host ("{0}: crop from y={1}" -f $_.Name, $srcY)
}

Write-Host "done"
