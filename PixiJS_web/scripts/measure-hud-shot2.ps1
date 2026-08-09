Add-Type -AssemblyName System.Drawing
$path = "d:\works\my\jianghu\CBE_EMU_pay\PixiJS_web\reference-shots\000017_00025672-game.png"
$bmp = [System.Drawing.Bitmap]::FromFile($path)

# Detect top HUD end: first row after y=40 that is mostly grass-like AND has high variance across x
# Also scan for gold icon borders (UIsh ~21px squares)

Write-Host "=== find gold frames (UIsh) on rows 20-70 ==="
for ($y=20; $y -le 70; $y++) {
  $gold=0
  for ($x=0; $x -lt 240; $x++) {
    $c=$bmp.GetPixel($x,$y)
    # gold-ish
    if ($c.R -gt 160 -and $c.G -gt 120 -and $c.B -lt 100 -and ($c.R-$c.B) -gt 60) { $gold++ }
  }
  if ($gold -gt 8) { "y=$y goldPixels=$gold" }
}

Write-Host "=== find red HP fill rows ==="
for ($y=20; $y -le 70; $y++) {
  $red=0
  for ($x=0; $x -lt 120; $x++) {
    $c=$bmp.GetPixel($x,$y)
    if ($c.R -gt 150 -and $c.G -lt 80 -and $c.B -lt 80) { $red++ }
  }
  if ($red -gt 5) { "y=$y redPixels=$red" }
}

Write-Host "=== find purple EXP fill ==="
for ($y=40; $y -le 80; $y++) {
  $pur=0
  for ($x=0; $x -lt 240; $x++) {
    $c=$bmp.GetPixel($x,$y)
    if ($c.R -gt 100 -and $c.B -gt 100 -and $c.G -lt 90) { $pur++ }
  }
  if ($pur -gt 5) { "y=$y purplePixels=$pur" }
}

Write-Host "=== find hangup button cyan border ==="
for ($y=330; $y -le 390; $y++) {
  $cy=0
  for ($x=40; $x -lt 160; $x++) {
    $c=$bmp.GetPixel($x,$y)
    if ($c.B -gt 150 -and $c.G -gt 150 -and $c.R -lt 120) { $cy++ }
    elseif ($c.R -gt 180 -and $c.G -gt 180 -and $c.B -gt 100 -and $c.R -lt 240) { $cy++ } # cream/gold button
  }
  if ($cy -gt 5) { "y=$y brightPixels=$cy" }
}

Write-Host "=== x profile at status mid (find portrait/icons) ==="
# Pick y with max gold
$bestY=0;$bestG=0
for ($y=20; $y -le 70; $y++) {
  $gold=0
  for ($x=0; $x -lt 240; $x++) {
    $c=$bmp.GetPixel($x,$y)
    if ($c.R -gt 160 -and $c.G -gt 120 -and $c.B -lt 100 -and ($c.R-$c.B) -gt 60) { $gold++ }
  }
  if ($gold -gt $bestG) { $bestG=$gold; $bestY=$y }
}
"best gold row y=$bestY count=$bestG"
$xs=@()
for ($x=0; $x -lt 240; $x++) {
  $c=$bmp.GetPixel($x,$bestY)
  if ($c.R -gt 160 -and $c.G -gt 120 -and $c.B -lt 100 -and ($c.R-$c.B) -gt 60) { $xs += $x }
}
if ($xs.Count) {
  "gold x range=$($xs[0])..$($xs[-1])"
  # cluster
  $clusters=@(); $start=$xs[0]; $prev=$xs[0]
  for ($i=1; $i -lt $xs.Count; $i++) {
    if ($xs[$i] -gt $prev+3) { $clusters += "$start-$prev"; $start=$xs[$i] }
    $prev=$xs[$i]
  }
  $clusters += "$start-$prev"
  "gold clusters: $($clusters -join ', ')"
}

Write-Host "=== dark chat vs soft: avg L by row bottom ==="
for ($y=340; $y -le 399; $y++) {
  $sum=0
  for ($x=0;$x -lt 240;$x++){ $c=$bmp.GetPixel($x,$y); $sum += ($c.R+$c.G+$c.B)/3 }
  $avg=[int]($sum/240)
  if ($y % 2 -eq 0) { "y=$y L=$avg" }
}

$bmp.Dispose()
