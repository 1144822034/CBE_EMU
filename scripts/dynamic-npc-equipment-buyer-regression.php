<?php
/*
 * Verifies the dynamic NPC equipment-buyer contract on an isolated service.
 *
 *   php scripts/dynamic-npc-equipment-buyer-regression.php setup
 *   bin\jh-online-server.exe --mock-service-port=19167 --mock-admin-port=19168
 *   php scripts/dynamic-npc-equipment-buyer-regression.php run 19167
 *   php scripts/dynamic-npc-equipment-buyer-regression.php cleanup
 *
 * The fixture owns six independent copies of equip.dsh item 1001 and one
 * non-equipment row.  It proves that the NPC dialog exposes only the buyer
 * action, pages the six equipment instances by seq, sells exactly one chosen
 * instance for ceil(base_value * 50%), and returns only the parser-owned 26/1
 * dialog response.  The next native backpack-list request reloads the
 * committed role state; an equipment sale must not misuse the 7/7 type=2
 * equipment-install stream as an inventory deletion notification.
 */

function entry(string $name, string $value): string {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}
function f_u8(string $name, int $value): string {
    return entry($name, "\x00\x01" . chr($value));
}
function f_u32(string $name, int $value): string {
    return entry($name, "\x00\x04" . pack('N', $value));
}
function f_string(string $name, string $value): string {
    return entry($name, pack('n', strlen($value)) . $value);
}
function wt(int $kind, int $subtype, string $fields = ''): string {
    $object = chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
    return 'WT' . pack('n', 4 + strlen($object)) . $object;
}
function read_exact($socket, int $length): string {
    $data = '';
    while (strlen($data) < $length && !feof($socket)) {
        $chunk = fread($socket, $length - strlen($data));
        if ($chunk === false || $chunk === '') break;
        $data .= $chunk;
    }
    return $data;
}
function call_service(int $port, int $clientId, string $packet = '', int $flags = 0): string {
    $metadata = pack('V', $clientId);
    $body = $metadata . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), strlen($metadata)) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    if (!$socket) throw new RuntimeException("connect failed: $errno $error");
    stream_set_timeout($socket, 5);
    if (fwrite($socket, $frame) !== strlen($frame)) throw new RuntimeException('short request write');
    $header = read_exact($socket, 20);
    if (strlen($header) !== 20 || substr($header, 0, 4) !== 'CBMR') {
        throw new RuntimeException('bad CBMR header');
    }
    $length = unpack('Vlength', substr($header, 12, 4))['length'];
    $response = read_exact($socket, $length);
    fclose($socket);
    if (strlen($response) !== $length) throw new RuntimeException('short response');
    return $response;
}
function response_objects(string $response): array {
    if (substr($response, 0, 2) !== 'WT' || strlen($response) < 5) return [];
    $objects = [];
    for ($offset = 5, $index = 0, $count = ord($response[4]); $index < $count; ++$index) {
        if ($offset + 6 > strlen($response)) return [];
        $length = unpack('nlength', substr($response, $offset + 4, 2))['length'];
        $end = $offset + $length;
        if ($length < 6 || $end > strlen($response)) return [];
        $fields = [];
        for ($field = $offset + 6; $field < $end;) {
            if ($field + 3 > $end) return [];
            $nameLength = ord($response[$field++]);
            if ($field + $nameLength + 2 > $end) return [];
            $name = substr($response, $field, $nameLength);
            $field += $nameLength;
            $valueLength = unpack('nlength', substr($response, $field, 2))['length'];
            $field += 2;
            if ($field + $valueLength > $end) return [];
            $fields[$name] = substr($response, $field, $valueLength);
            $field += $valueLength;
        }
        $objects[] = ['kind' => ord($response[$offset + 1]),
                      'subtype' => ord($response[$offset + 2]),
                      'fields' => $fields];
        $offset = $end;
    }
    return $objects;
}
function response_field(string $response, int $kind, int $subtype, string $field): ?string {
    foreach (response_objects($response) as $object) {
        if ($object['kind'] === $kind && $object['subtype'] === $subtype &&
            array_key_exists($field, $object['fields'])) {
            return $object['fields'][$field];
        }
    }
    return null;
}
function response_has_object(string $response, int $kind, int $subtype): bool {
    foreach (response_objects($response) as $object) {
        if ($object['kind'] === $kind && $object['subtype'] === $subtype) return true;
    }
    return false;
}
function tagged_u8(?string $value): ?int {
    return $value !== null && strlen($value) === 3 && substr($value, 0, 2) === "\x00\x01"
        ? ord($value[2]) : null;
}
function dialog_read_tagged_u8(string $dialog, int &$offset): ?int {
    if ($offset + 3 > strlen($dialog) || substr($dialog, $offset, 2) !== "\x00\x01") return null;
    $value = ord($dialog[$offset + 2]);
    $offset += 3;
    return $value;
}
function dialog_read_tagged_u32(string $dialog, int &$offset): ?int {
    if ($offset + 6 > strlen($dialog) || substr($dialog, $offset, 2) !== "\x00\x04") return null;
    $value = unpack('Nvalue', substr($dialog, $offset + 2, 4))['value'];
    $offset += 6;
    return $value;
}
function dialog_read_string(string $dialog, int &$offset): ?string {
    if ($offset + 2 > strlen($dialog)) return null;
    $length = unpack('nlength', substr($dialog, $offset, 2))['length'];
    $offset += 2;
    if ($offset + $length > strlen($dialog)) return null;
    $value = substr($dialog, $offset, $length);
    $offset += $length;
    return $value;
}
function dialog_options(?string $dialog): ?array {
    if (!is_string($dialog)) return null;
    $offset = 0;
    if (dialog_read_tagged_u8($dialog, $offset) === null ||
        dialog_read_string($dialog, $offset) === null) return null;
    $count = dialog_read_tagged_u8($dialog, $offset);
    if ($count === null) return null;
    $options = [];
    for ($i = 0; $i < $count; ++$i) {
        if (dialog_read_tagged_u8($dialog, $offset) === null) return null;
        $name = dialog_read_string($dialog, $offset);
        $action = dialog_read_tagged_u8($dialog, $offset);
        $value = dialog_read_tagged_u32($dialog, $offset);
        $description = dialog_read_string($dialog, $offset);
        if ($name === null || $action === null || $value === null || $description === null) return null;
        $options[] = compact('name', 'action', 'value', 'description');
    }
    return $options;
}
function account_role(PDO $pdo, string $account, int $roleId): array {
    $statement = $pdo->prepare('SELECT money FROM account_roles WHERE account_id=? AND role_id=?');
    $statement->execute([$account, $roleId]);
    return $statement->fetch(PDO::FETCH_ASSOC) ?: [];
}
function backpack_seqs(PDO $pdo, string $account, int $roleId): array {
    $statement = $pdo->prepare('SELECT item_seq,item_id,item_count FROM account_role_backpack WHERE account_id=? AND role_id=? ORDER BY item_seq');
    $statement->execute([$account, $roleId]);
    return $statement->fetchAll(PDO::FETCH_ASSOC);
}
function cleanup(PDO $pdo, string $account, int $roleId, string $scene, int $actorId): void {
    $pdo->prepare('DELETE FROM server_dynamic_npcs WHERE scene=? AND actor_id=?')->execute([$scene, $actorId]);
    foreach (['account_role_equipment_durability', 'account_role_equipment', 'account_role_skills',
              'account_role_tasks', 'account_role_backpack', 'account_role_state'] as $table) {
        $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
    }
    $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
    $pdo->prepare('DELETE FROM accounts WHERE account_id=?')->execute([$account]);
}

$mode = $argv[1] ?? 'run';
$port = isset($argv[2]) ? (int)$argv[2] : 19167;
$account = 'codex_npc_equipment_buyer';
$password = 'npc-equipment-buyer-pass';
$roleId = 59667;
$actorId = 59668;
$clientId = 0x7A245967;
$scene = hex2bin('3030C5EEC0B3CFC9B5BA5F30322E736365'); /* 00蓬莱仙岛_02.sce */
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root',
    getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);

if ($mode === 'setup') {
    cleanup($pdo, $account, $roleId, $scene, $actorId);
    $pdo->beginTransaction();
    try {
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, $password]);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)')->execute([$account, $roleId]);
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,5,1250,100,120,41,100,654,0,?,338,125,7,0,48)')
            ->execute([$account, $roleId, 'EquipmentBuyer', $scene]);
        for ($slot = 0, $seq = 41; $seq <= 46; ++$seq, ++$slot) {
            $pdo->prepare('INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level) VALUES(?,?,?,?,?,?,?)')
                ->execute([$account, $roleId, $slot, 1001, $seq, 1, $seq === 42 ? 3 : 0]);
        }
        $pdo->prepare('INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level) VALUES(?,?,?,?,?,?,0)')
            ->execute([$account, $roleId, 6, 800, 47, 1]);
        $pdo->prepare('INSERT INTO server_dynamic_npcs(scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,enabled) VALUES(?,?,?,?,7,0,?,?,?,1)')
            ->execute([$scene, $actorId, 336, 124, 'n_man1.actor', 'EquipmentBuyer', '']);
        $pdo->commit();
        echo "dynamic NPC equipment-buyer fixture prepared\n";
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    exit(0);
}
if ($mode === 'cleanup') {
    cleanup($pdo, $account, $roleId, $scene, $actorId);
    echo "dynamic NPC equipment-buyer fixture removed\n";
    exit(0);
}

try {
    call_service($port, $clientId, wt(1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1') .
        f_string('imsi', 'npc-equipment-buyer-regression') .
        f_string('username', $account) . f_string('password', $password)));
    call_service($port, $clientId, wt(1, 6, f_u32('actorID', $roleId)));

    $npcDialog = call_service($port, $clientId,
        wt(26, 1, f_u8('type', 1) . f_u32('id', $actorId)));
    $npcOptions = dialog_options(response_field($npcDialog, 26, 1, 'dialog'));
    if ($npcOptions === null || !array_filter($npcOptions, function ($option) {
        return $option['action'] === 1 && $option['value'] === 0xED000000;
    })) {
        throw new RuntimeException('equipment-buyer NPC did not expose the 26/1 sell entry');
    }

    $pageOne = call_service($port, $clientId,
        wt(26, 1, f_u8('type', 2) . f_u32('id', 0xED000000)));
    $pageOneOptions = dialog_options(response_field($pageOne, 26, 1, 'dialog'));
    $sellOptions = array_values(array_filter($pageOneOptions ?? [], function ($option) {
        return $option['action'] === 1 &&
            ($option['value'] & 0xFF000000) === 0xEE000000;
    }));
    $salePrice = 0;
    foreach ($sellOptions as $option) {
        if ($option['value'] !== 0xEE000029 ||
            !preg_match_all('/[0-9]+/', $option['name'], $matches) ||
            empty($matches[0])) {
            continue;
        }
        $salePrice = (int)end($matches[0]);
        break;
    }
    if (count($sellOptions) !== 5 ||
        $salePrice === 0 || !array_filter($pageOneOptions ?? [], function ($option) {
            return $option['value'] === 0xED000001;
        })) {
        throw new RuntimeException('first recovery page did not contain five equipment rows and a next page: sells=' .
            count($sellOptions) . ' price=' . $salePrice . ' options=' .
            bin2hex(response_field($pageOne, 26, 1, 'dialog') ?: ''));
    }
    $pageTwo = call_service($port, $clientId,
        wt(26, 1, f_u8('type', 2) . f_u32('id', 0xED000001)));
    $pageTwoOptions = dialog_options(response_field($pageTwo, 26, 1, 'dialog'));
    $pageTwoSell = array_values(array_filter($pageTwoOptions ?? [], function ($option) {
        return $option['action'] === 1 &&
            ($option['value'] & 0xFF000000) === 0xEE000000;
    }));
    if (count($pageTwoSell) !== 1 ||
        !array_filter($pageTwoOptions ?? [], function ($option) {
            return $option['value'] === 0xED000000;
        })) {
        throw new RuntimeException('second recovery page did not preserve the sixth row and previous page');
    }

    $sale = call_service($port, $clientId,
        wt(26, 1, f_u8('type', 2) . f_u32('id', 0xEE000029)));
    $afterSale = account_role($pdo, $account, $roleId);
    $rowsAfterSale = backpack_seqs($pdo, $account, $roleId);
    $seqsAfterSale = array_map(function ($row) {
        return (int)$row['item_seq'];
    }, $rowsAfterSale);
    if (!response_has_object($sale, 26, 1) || response_has_object($sale, 7, 7) ||
        response_has_object($sale, 7, 11) ||
        (int)($afterSale['money'] ?? -1) !== 654 + $salePrice ||
        in_array(41, $seqsAfterSale, true) || !in_array(42, $seqsAfterSale, true) ||
        !in_array(47, $seqsAfterSale, true)) {
        throw new RuntimeException('sale did not atomically remove seq 41, credit the displayed recovery price, and emit only the parser-owned 26/1 dialog');
    }

    $staleSale = call_service($port, $clientId,
        wt(26, 1, f_u8('type', 2) . f_u32('id', 0xEE000029)));
    $afterStale = account_role($pdo, $account, $roleId);
    if (response_has_object($staleSale, 7, 7) || response_has_object($staleSale, 7, 11) ||
        (int)($afterStale['money'] ?? -1) !== 654 + $salePrice) {
        throw new RuntimeException('stale equipment sequence credited money or emitted a removal update');
    }
    echo "dynamic NPC equipment-buyer regression passed pages=5+1 sale_seq=41 copper=$salePrice response=26/1-only stale=blocked\n";
} finally {
    try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
}
