Add-Type -AssemblyName System.Drawing
$path = "d:\works\my\jianghu\CBE_EMU_pay\PixiJS_web\reference-shots\000017_00025672-game.png"
$bmp = [System.Drawing.Bitmap]::FromFile($path)

# Soft buttons are dark teal with bright border. Chat is darker.
Write-Host "=== row avg + teal-button pixels (soft chrome) ==="
for ($y=320; $y -le 399; $y++) {
  $sumR=0;$sumG=0;$sumB=0;$teal=0;$dark=0;$yellow=0
  for ($x=0; $x -lt 240; $x++) {
    $c=$bmp.GetPixel($x,$y)
    $sumR+=$c.R;$sumG+=$c.G;$sumB+=$c.B
    # teal panel-ish (soft bg / button)
    if ($c.G -gt 80 -and $c.G -gt $c.R+20 -and $c.G -gt $c.B -and $c.R -lt 120 -and $c.B -lt 120) { $teal++ }
    if (($c.R+$c.G+$c.B)/3 -lt 40) { $dark++ }
    # chat yellow text
    if ($c.R -gt 180 -and $c.G -gt 160 -and $c.B -lt 80) { $yellow++ }
  }
  $n=240
  "y={0} rgb=({1,3},{2,3},{3,3}) teal={4,3} dark={5,3} yel={6,3}" -f $y,[int]($sumR/$n),[int]($sumG/$n),[int]($sumB/$n),$teal,$dark,$yellow
}

Write-Host "=== sample soft/chat pixels ==="
foreach ($p in @(
  @(8,340),(20,350),(100,350),(200,350),(220,350),
  @(8,355),(100,355),(220,355),
  @(8,360),(100,360),
  @(8,365),(60,365),(120,365),(200,365),(220,365),
  @(8,370),(100,370),(200,370),
  @(20,380),(100,380),(20,390),(100,390)
)) {
  $c=$bmp.GetPixel($p[0],$p[1])
  "($($p[0]),$($p[1]))=$($c.R),$($c.G),$($c.B)"
}

# Find menu button (left small square) by looking for teal island at left
Write-Host "=== left menu button bbox (teal island x<40) ==="
$minx=999;$maxx=0;$miny=999;$maxy=0;$n=0
for ($y=330; $y -le 380; $y++) {
  for ($x=0; $x -lt 40; $x++) {
    $c=$bmp.GetPixel($x,$y)
    # dark teal button face
    if ($c.R -ge 20 -and $c.R -le 70 -and $c.G -ge 50 -and $c.G -le 110 -and $c.B -ge 40 -and $c.B -le 90 -and ($c.G -gt $c.R)) {
      if($x -lt $minx){$minx=$x}; if($x -gt $maxx){$maxx=$x}
      if($y -lt $miny){$miny=$y}; if($y -gt $maxy){$maxy=$y}
      $n++
    }
  }
}
"menu-ish x=$minx-$maxx y=$miny-$maxy n=$n"

# Status row olive band - measure continuous olive
Write-Host "=== olive status bg rows (sample x=200 away from icons) ==="
for ($y=28; $y -le 75; $y++) {
  $c=$bmp.GetPixel(200,$y)
  $c2=$bmp.GetPixel(10,$y)
  "y=$y x200=$($c.R),$($c.G),$($c.B) x10=$($c2.R),$($c2.G),$($c2.B)"
}

$bmp.Dispose()
