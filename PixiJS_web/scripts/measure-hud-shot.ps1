Add-Type -AssemblyName System.Drawing
$path = "d:\works\my\jianghu\CBE_EMU_pay\PixiJS_web\reference-shots\000017_00025672-game.png"
$bmp = [System.Drawing.Bitmap]::FromFile($path)
Write-Host "size $($bmp.Width)x$($bmp.Height)"

function Row-Stats($bmp, $y) {
  $sumR=0;$sumG=0;$sumB=0;$minL=255;$maxL=0
  for ($x=0; $x -lt $bmp.Width; $x++) {
    $c = $bmp.GetPixel($x,$y)
    $sumR += $c.R; $sumG += $c.G; $sumB += $c.B
    $L = ($c.R+$c.G+$c.B)/3
    if ($L -lt $minL) { $minL = $L }
    if ($L -gt $maxL) { $maxL = $L }
  }
  $n = $bmp.Width
  [pscustomobject]@{ y=$y; r=[int]($sumR/$n); g=[int]($sumG/$n); b=[int]($sumB/$n); range=[int]($maxL-$minL) }
}

Write-Host "--- TOP rows ---"
0..69 | ForEach-Object {
  $s = Row-Stats $bmp $_
  $tag = if ($s.range -lt 30) { "FLAT" } elseif ($s.g -gt ($s.r+20) -and $s.range -gt 80) { "MAP" } else { "UI" }
  "{0,3} rng={1,3} rgb=({2,3},{3,3},{4,3}) {5}" -f $s.y,$s.range,$s.r,$s.g,$s.b,$tag
}

Write-Host "--- BOTTOM rows ---"
330..399 | ForEach-Object {
  $s = Row-Stats $bmp $_
  $tag = if ($s.range -lt 30) { "FLAT" } elseif ($s.g -gt ($s.r+20) -and $s.range -gt 80) { "MAP" } else { "UI" }
  "{0,3} rng={1,3} rgb=({2,3},{3,3},{4,3}) {5}" -f $s.y,$s.range,$s.r,$s.g,$s.b,$tag
}

# Sample key pixels for portrait/icons
Write-Host "--- sample pixels ---"
foreach ($p in @(
  @(120,8),(4,28),(20,28),(40,30),(70,30),(110,30),(130,30),(150,30),(170,30),(190,30),(210,30),(230,30),
  @(10,52),(40,52),(8,70),(120,200),(30,350),(120,355),(200,350),(20,380)
)) {
  $c = $bmp.GetPixel($p[0],$p[1])
  "($($p[0]),$($p[1])) = $($c.R),$($c.G),$($c.B)"
}
$bmp.Dispose()
