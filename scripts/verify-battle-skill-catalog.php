<?php

/* Read-only regression for the authoritative skill semantics in skill.dsh.
 * It deliberately does not start a service or connect to MySQL. */
function u32le(string $data, int $offset): int {
    return unpack('V', substr($data, $offset, 4))[1];
}

function numeric(string $value): int {
    return $value === '' ? 0 : intval($value, 10);
}

$path = $argv[1] ?? 'bin/JHOnlineData/skill.dsh';
$data = @file_get_contents($path);
if ($data === false || strlen($data) < 16) {
    throw new RuntimeException("cannot read skill data sheet: $path");
}
$columnCount = u32le($data, 4);
$rowCount = u32le($data, 8);
if ($columnCount !== 32 || $rowCount === 0 || $rowCount > 256) {
    throw new RuntimeException("unexpected skill.dsh header columns=$columnCount rows=$rowCount");
}
$pos = 16;
for ($i = 0; $i < $columnCount; ++$i) {
    if ($pos >= strlen($data)) throw new RuntimeException('truncated column names');
    $len = ord($data[$pos++]);
    $pos += $len;
}

$counts = [];
for ($row = 0; $row < $rowCount; ++$row) {
    if ($pos + 4 > strlen($data)) throw new RuntimeException('truncated row length');
    $rowEnd = $pos + 4 + u32le($data, $pos);
    $pos += 4;
    if ($rowEnd > strlen($data)) throw new RuntimeException('truncated row body');
    $columns = [];
    for ($col = 0; $col < $columnCount; ++$col) {
        if ($pos >= $rowEnd) throw new RuntimeException("truncated row=$row column=$col");
        $len = ord($data[$pos++]);
        if ($pos + $len > $rowEnd) throw new RuntimeException("invalid value row=$row column=$col");
        $columns[$col] = substr($data, $pos, $len);
        $pos += $len;
    }
    if ($pos !== $rowEnd) throw new RuntimeException("row boundary mismatch row=$row");
    $scope = numeric($columns[10]);
    $duration = numeric($columns[9]);
    $hp = numeric($columns[14]);
    $effect = numeric($columns[25]);
    $hasModifier = false;
    for ($col = 16; $col <= 24; ++$col) $hasModifier = $hasModifier || numeric($columns[$col]) !== 0;

    if ($scope === 0 && $hp === 0 && $duration > 0 && $hasModifier) $kind = 'self_modifier';
    elseif ($scope === 1 && $hp > 0 && $effect === 3) $kind = 'single_revive';
    elseif ($scope === 1 && $hp > 0) $kind = 'single_heal';
    elseif ($scope === 2 && $hp > 0) $kind = 'group_heal';
    elseif ($scope === 2 && $hp === 0 && $duration > 0 && $hasModifier) $kind = 'group_modifier';
    elseif (($scope === 3 || $scope === 4) && $hp < 0 && $duration > 0 && !$hasModifier) $kind = 'enemy_dot';
    elseif (($scope === 3 || $scope === 4) && $hp < 0 && $duration > 0 && $hasModifier) $kind = 'enemy_damage_modifier';
    elseif (($scope === 3 || $scope === 4) && $hp === 0 && $effect === 1) $kind = 'enemy_silence';
    elseif (($scope === 3 || $scope === 4) && $hp < 0 && $effect === 2) $kind = 'enemy_damage_dispel';
    elseif (($scope === 3 || $scope === 4) && $hp < 0) $kind = 'enemy_damage';
    else throw new RuntimeException('unclassified skill id=' . numeric($columns[0]));
    $counts[$kind] = ($counts[$kind] ?? 0) + 1;
}

$expected = [
    'enemy_damage' => 18, 'enemy_damage_modifier' => 15, 'enemy_damage_dispel' => 1,
    'enemy_dot' => 4, 'enemy_silence' => 1, 'group_heal' => 3, 'group_modifier' => 4,
    'self_modifier' => 22, 'single_heal' => 5, 'single_revive' => 1,
];
ksort($counts);
ksort($expected);
if ($counts !== $expected) {
    throw new RuntimeException('skill semantic inventory changed: ' . json_encode($counts));
}
echo 'battle skill catalog verified rows=' . $rowCount . ' classes=' . json_encode($counts) . PHP_EOL;
