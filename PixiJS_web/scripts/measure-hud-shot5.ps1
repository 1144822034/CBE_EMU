Add-Type -AssemblyName System.Drawing
$path = "d:\works\my\jianghu\CBE_EMU_pay\PixiJS_web\reference-shots\000017_00025672-game.png"
$bmp = [System.Drawing.Bitmap]::FromFile($path)

Write-Host "=== cyan/bag-like pixels bottom ==="
for ($y=330; $y -le 399; $y++) {
  $xs=@()
  for ($x=0; $x -lt 240; $x++) {
    $c=$bmp.GetPixel($x,$y)
    # cyan-ish text (背包)
    if ($c.B -gt 150 -and $c.G -gt 150 -and $c.R -lt 120) { $xs += $x }
    elseif ($c.R -lt 100 -and $c.G -gt 180 -and $c.B -gt 180) { $xs += $x }
  }
  if ($xs.Count -gt 2) { "y=$y cyan=$($xs.Count) x=$($xs[0])-$($xs[-1])" }
}

Write-Host "=== white text on soft (挂机/菜单) ==="
for ($y=340; $y -le 365; $y++) {
  $xs=@()
  for ($x=0; $x -lt 240; $x++) {
    $c=$bmp.GetPixel($x,$y)
    if ($c.R -gt 200 -and $c.G -gt 200 -and $c.B -gt 200) { $xs += $x }
  }
  if ($xs.Count -gt 2) {
    $clusters=@(); $s=$xs[0]; $p=$xs[0]
    for($i=1;$i -lt $xs.Count;$i++){ if($xs[$i]-gt $p+3){$clusters+="$s-$p";$s=$xs[$i]}; $p=$xs[$i] }
    $clusters+="$s-$p"
    "y=$y white=$($xs.Count) $($clusters -join '; ')"
  }
}

# Measure icon exact tops by scanning column at icon centers
Write-Host "=== icon columns vertical extent ==="
foreach ($cx in @(100,131,162,193,224)) {
  $ys=@()
  for ($y=30; $y -le 70; $y++) {
    $c=$bmp.GetPixel($cx,$y)
    # not olive bg
    $olive = ($c.R -gt 60 -and $c.R -lt 90 -and $c.G -gt 100 -and $c.G -lt 130 -and $c.B -gt 80 -and $c.B -lt 110)
    if (-not $olive -and ($c.R+$c.G+$c.B) -gt 30) { $ys += $y }
  }
  if ($ys.Count) { "cx=$cx y=$($ys[0])-$($ys[-1]) n=$($ys.Count)" }
}

# tiao size probe - beige container
Write-Host "=== beige tiao pixels ==="
for ($y=35; $y -le 62; $y++) {
  $xs=@()
  for ($x=30; $x -lt 100; $x++) {
    $c=$bmp.GetPixel($x,$y)
    if ($c.R -gt 140 -and $c.G -gt 120 -and $c.B -gt 60 -and $c.B -lt 140 -and ($c.R-$c.B) -gt 40) { $xs += $x }
  }
  if ($xs.Count -gt 5) { "y=$y beige=$($xs.Count) x=$($xs[0])-$($xs[-1])" }
}

# Also check 000018 for consistency on WORLD bands
$path2 = "d:\works\my\jianghu\CBE_EMU_pay\PixiJS_web\reference-shots\000018_00027182-game.png"
if (Test-Path $path2) {
  $b2 = [System.Drawing.Bitmap]::FromFile($path2)
  Write-Host "=== 000018 size $($b2.Width)x$($b2.Height) key rows ==="
  foreach ($y in @(28,31,35,40,58,62,66,69,75,340,355,360,365,390)) {
    $c=$b2.GetPixel(120,$y)
    "y=$y center=$($c.R),$($c.G),$($c.B)"
  }
  $b2.Dispose()
}

$bmp.Dispose()
