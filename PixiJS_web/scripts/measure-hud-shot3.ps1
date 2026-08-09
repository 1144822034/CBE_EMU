Add-Type -AssemblyName System.Drawing
$path = "d:\works\my\jianghu\CBE_EMU_pay\PixiJS_web\reference-shots\000017_00025672-game.png"
$bmp = [System.Drawing.Bitmap]::FromFile($path)

function Find-Runs($ys, $xs, $pred) {
  $runs = @()
  foreach ($y in $ys) {
    $on=$false; $s=0
    for ($x=$xs[0]; $x -le $xs[1]; $x++) {
      $c=$bmp.GetPixel($x,$y)
      $hit = & $pred $c
      if ($hit -and -not $on) { $on=$true; $s=$x }
      elseif (-not $hit -and $on) { $on=$false; $runs += [pscustomobject]@{y=$y;x0=$s;x1=$x-1;w=$x-$s} }
    }
    if ($on) { $runs += [pscustomobject]@{y=$y;x0=$s;x1=$xs[1];w=$xs[1]-$s+1} }
  }
  $runs
}

Write-Host "=== portrait frame left (dark/gold) y=35..60 x=0..40 ==="
for ($y=35; $y -le 62; $y++) {
  $line=""
  for ($x=0; $x -le 40; $x++) {
    $c=$bmp.GetPixel($x,$y)
    $L=($c.R+$c.G+$c.B)/3
    if ($c.R -gt 160 -and $c.G -gt 120 -and $c.B -lt 100) { $line += "G" }
    elseif ($L -lt 40) { $line += "#" }
    elseif ($c.R -gt 100 -and $c.G -lt 60) { $line += "R" }
    elseif ($c.B -gt 100 -and $c.R -lt 80) { $line += "B" }
    elseif ($L -gt 140) { $line += "." }
    else { $line += " " }
  }
  "{0,2}|{1}" -f $y,$line
}

Write-Host "=== HP/SP bar x at y=44-45 ==="
foreach ($y in 42,43,44,45,46,47,48,49,50) {
  $red=@(); $blu=@()
  for ($x=0; $x -lt 100; $x++) {
    $c=$bmp.GetPixel($x,$y)
    if ($c.R -gt 140 -and $c.G -lt 90 -and $c.B -lt 90) { $red += $x }
    if ($c.B -gt 120 -and $c.R -lt 100 -and $c.G -lt 140) { $blu += $x }
  }
  $rr = if($red){"$($red[0])-$($red[-1]) n=$($red.Count)"} else {"-"}
  $bb = if($blu){"$($blu[0])-$($blu[-1]) n=$($blu.Count)"} else {"-"}
  "y=$y red=$rr blu=$bb"
}

Write-Host "=== EXP purple x at y=69 ==="
$pur=@()
for ($x=0;$x -lt 240;$x++){
  $c=$bmp.GetPixel($x,69)
  if ($c.R -gt 100 -and $c.B -gt 100 -and $c.G -lt 90) { $pur += $x }
}
if($pur){"purple x=$($pur[0])-$($pur[-1]) n=$($pur.Count)"}

Write-Host "=== EXP row luminance y=60-75 ==="
for ($y=58; $y -le 75; $y++) {
  $sum=0;$pur=0;$gold=0
  for ($x=0;$x -lt 240;$x++){
    $c=$bmp.GetPixel($x,$y)
    $sum += ($c.R+$c.G+$c.B)/3
    if ($c.R -gt 100 -and $c.B -gt 100 -and $c.G -lt 90) { $pur++ }
    if ($c.R -gt 160 -and $c.G -gt 120 -and $c.B -lt 100) { $gold++ }
  }
  "y=$y L=$([int]($sum/240)) pur=$pur gold=$gold"
}

Write-Host "=== hangup button bbox (cream/yellow) ==="
$minx=999;$maxx=0;$miny=999;$maxy=0;$n=0
for ($y=330; $y -le 365; $y++) {
  for ($x=60; $x -lt 180; $x++) {
    $c=$bmp.GetPixel($x,$y)
    # cream button face
    if ($c.R -gt 200 -and $c.G -gt 180 -and $c.B -gt 80 -and $c.B -lt 160) {
      if($x -lt $minx){$minx=$x}; if($x -gt $maxx){$maxx=$x}
      if($y -lt $miny){$miny=$y}; if($y -gt $maxy){$maxy=$y}
      $n++
    }
  }
}
"cream hangup approx x=$minx-$maxx y=$miny-$maxy n=$n"

Write-Host "=== soft row text-ish (bright) y=365-395 ==="
for ($y=363; $y -le 399; $y++) {
  $br=@()
  for ($x=0;$x -lt 240;$x++){
    $c=$bmp.GetPixel($x,$y)
    $L=($c.R+$c.G+$c.B)/3
    if ($L -gt 140) { $br += $x }
  }
  if ($br.Count -gt 3) {
    $clusters=@(); $s=$br[0]; $p=$br[0]
    for($i=1;$i -lt $br.Count;$i++){
      if($br[$i]-gt $p+4){$clusters+="$s-$p";$s=$br[$i]}
      $p=$br[$i]
    }
    $clusters+="$s-$p"
    "y=$y bright=$($br.Count) clusters=$($clusters -join '; ')"
  }
}

# Location banner: look for dark panel top center
Write-Host "=== location banner dark pixels y=0-28 ==="
for ($y=0; $y -le 28; $y++) {
  $dk=0;$minx=999;$maxx=0
  for ($x=40;$x -lt 200;$x++){
    $c=$bmp.GetPixel($x,$y)
    $L=($c.R+$c.G+$c.B)/3
    if ($L -lt 55 -and $c.G -lt 80) {
      $dk++; if($x -lt $minx){$minx=$x}; if($x -gt $maxx){$maxx=$x}
    }
  }
  if ($dk -gt 10) { "y=$y dark=$dk x=$minx-$maxx" }
}

$bmp.Dispose()
