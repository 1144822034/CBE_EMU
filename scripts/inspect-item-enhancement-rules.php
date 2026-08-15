<?php

/* Read-only inspection for the item.dsh category consumed by
 * JianghuOL.CBE:LoadItemDataSheet(0x010285B6).  This deliberately verifies
 * that category 23 is the occult-crystal catalog, not the adjacent native
 * enhancement-stat rule table. */

function dsh_u32le(string $data, int $offset): int
{
    return unpack('V', substr($data, $offset, 4))[1];
}

function dsh_utf8(string $value): string
{
    $converted = iconv('GBK', 'UTF-8//IGNORE', $value);
    return $converted === false ? '' : $converted;
}

$path = $argv[1] ?? 'bin/JHOnlineData/item.dsh';
$wantedCategory = isset($argv[2]) ? intval($argv[2], 10) : 23;
$data = @file_get_contents($path);
if ($data === false || strlen($data) < 16) {
    throw new RuntimeException("cannot read item data sheet: $path");
}

$columnCount = dsh_u32le($data, 4);
$rowCount = dsh_u32le($data, 8);
if ($columnCount === 0 || $columnCount > 64 ||
    $rowCount === 0 || $rowCount > 10000) {
    throw new RuntimeException(
        "unexpected item.dsh header columns=$columnCount rows=$rowCount");
}

$pos = 16;
$columnNames = [];
for ($column = 0; $column < $columnCount; ++$column) {
    if ($pos >= strlen($data)) {
        throw new RuntimeException('truncated column names');
    }
    $length = ord($data[$pos++]);
    if ($pos + $length > strlen($data)) {
        throw new RuntimeException('invalid column name length');
    }
    $columnNames[$column] = dsh_utf8(substr($data, $pos, $length));
    $pos += $length;
}

$categoryColumn = array_search('类别', $columnNames, true);
$idColumn = array_search('ID', $columnNames, true);
$nameColumn = array_search('名称', $columnNames, true);
if ($categoryColumn === false || $idColumn === false || $nameColumn === false) {
    throw new RuntimeException('item.dsh lacks 类别/ID/名称 columns');
}

$matches = [];
for ($row = 0; $row < $rowCount; ++$row) {
    if ($pos + 4 > strlen($data)) {
        throw new RuntimeException("truncated row length at row=$row");
    }
    $rowEnd = $pos + 4 + dsh_u32le($data, $pos);
    $pos += 4;
    if ($rowEnd > strlen($data)) {
        throw new RuntimeException("truncated row body at row=$row");
    }
    $columns = [];
    for ($column = 0; $column < $columnCount; ++$column) {
        if ($pos >= $rowEnd) {
            throw new RuntimeException(
                "truncated row=$row column=$column");
        }
        $length = ord($data[$pos++]);
        if ($pos + $length > $rowEnd) {
            throw new RuntimeException(
                "invalid value row=$row column=$column");
        }
        $columns[$column] = substr($data, $pos, $length);
        $pos += $length;
    }
    if ($pos !== $rowEnd) {
        throw new RuntimeException("row boundary mismatch row=$row");
    }
    if (intval($columns[$categoryColumn], 10) !== $wantedCategory) {
        continue;
    }
    $matches[] = [
        'row' => $row,
        'id' => intval($columns[$idColumn], 10),
        'name_raw' => $columns[$nameColumn],
    ];
}

printf("item occult catalog path=%s category=%d rows=%d\n",
       $path, $wantedCategory, count($matches));
foreach ($matches as $index => $match) {
    printf("stage=%d row=%d id=%d name_len=%d name_hex=%s name_text=%s\n",
           $index + 1, $match['row'], $match['id'],
           strlen($match['name_raw']), bin2hex($match['name_raw']),
           dsh_utf8($match['name_raw']));
}
