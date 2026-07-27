<?php
/*
 * Regression for equip.dsh-derived durability limits.
 *
 * Setup a role with the old fabricated 100 max, start a server on an isolated
 * port, then run:
 *   php scripts/equipment-durability-max-regression.php run 19168
 */
function entry($name, $value) { return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value; }
function f_u8($name, $value) { return entry($name, "\x00\x01" . chr($value)); }
function f_u32($name, $value) { return entry($name, "\x00\x04" . pack('N', $value)); }
function f_string($name, $value) { return entry($name, pack('n', strlen($value)) . $value); }
function wt_object($kind, $subtype, $fields = '') {
    return chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
}
function wt_combo(...$objects) {
    $payload = implode('', $objects);
    /* Client requests are a sequence of WT objects directly after the 4-byte
     * header.  (Responses have a count byte; do not copy that framing here.) */
    return 'WT' . pack('n', 4 + strlen($payload)) . $payload;
}
function wt($kind, $subtype, $fields = '') {
    return wt_combo(wt_object($kind, $subtype, $fields));
}
function call_service($port, $clientId, $packet = '', $flags = 0) {
    $meta = pack('V', $clientId);
    $body = $meta . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), strlen($meta)) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    if (!$socket) throw new RuntimeException("connect failed: $errno $error");
    stream_set_timeout($socket, 3);
    fwrite($socket, $frame);
    $header = '';
    while (strlen($header) < 20 && !feof($socket)) $header .= fread($socket, 20 - strlen($header));
    if (strlen($header) !== 20 || substr($header, 0, 4) !== 'CBMR')
        throw new RuntimeException('bad response header');
    $length = unpack('Vlength', substr($header, 12, 4))['length'];
    $response = '';
    while (strlen($response) < $length && !feof($socket)) $response .= fread($socket, $length - strlen($response));
    fclose($socket);
    if (strlen($response) !== $length) throw new RuntimeException('short response');
    return $response;
}
function has_object($response, $kind, $subtype) {
    if (substr($response, 0, 2) !== 'WT') return false;
    for ($at = 5, $count = ord($response[4]); $count-- > 0;) {
        if ($at + 6 > strlen($response)) return false;
        $length = unpack('nlength', substr($response, $at + 4, 2))['length'];
        if ($length < 6 || $at + $length > strlen($response)) return false;
        if (ord($response[$at + 1]) === $kind && ord($response[$at + 2]) === $subtype) return true;
        $at += $length;
    }
    return false;
}
function durability($pdo, $account, $role, $slot) {
    $stmt = $pdo->prepare('SELECT item_id,durability,durability_max FROM account_role_equipment WHERE account_id=? AND role_id=? AND slot_index=?');
    $stmt->execute([$account, $role, $slot]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    if (!$row) throw new RuntimeException("missing durability row slot=$slot");
    return [(int)$row['item_id'], (int)$row['durability'], (int)$row['durability_max']];
}
function money($pdo, $account, $role) {
    $stmt = $pdo->prepare('SELECT money FROM account_roles WHERE account_id=? AND role_id=?');
    $stmt->execute([$account, $role]);
    $value = $stmt->fetchColumn();
    return $value === false ? null : (int)$value;
}

$mode = $argv[1] ?? 'run';
$port = isset($argv[2]) ? (int)$argv[2] : 19168;
$account = 'codex_durability_max';
$password = 'durability-max-pass';
$role = 59341;
$clientId = 0x7A2300A1;
$scene = hex2bin('633030C5EEC0B3CFC9B5BA5F30312E736365'); /* c00蓬莱仙岛_01.sce */
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root', getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);

if ($mode === 'setup') {
    $pdo->beginTransaction();
    try {
        foreach (['account_role_equipment_durability', 'account_role_equipment', 'account_role_state', 'accounts'] as $table)
            $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
        $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, $password]);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)')->execute([$account, $role]);
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,1,0,120,120,100,100,1000,0,?,220,440,0,0,1)')->execute([$account, $role, 'DurabilityMax', $scene]);
        /* Format 5 rows deliberately lack instance durability.  The legacy
           companion table below is only a migration source; after login the
           v6 service writes the recovered state into account_role_equipment. */
        $pdo->prepare('INSERT INTO account_role_equipment(account_id,role_id,slot_index,item_id,enhance_level,durability,durability_max) VALUES(?,?,0,1001,0,0,0),(?,?,1,1101,0,0,0)')->execute([$account, $role, $account, $role]);
        /* Legacy fabricated 100 max rows: 1001 is really 50; 1101 is really 80. */
        $pdo->prepare('INSERT INTO account_role_equipment_durability(account_id,role_id,slot_index,item_id,durability,durability_max) VALUES(?,?,0,1001,44,100),(?,?,1,1101,70,100)')->execute([$account, $role, $account, $role]);
        $pdo->commit();
        echo "equipment durability max fixture prepared\n";
    } catch (Throwable $error) { $pdo->rollBack(); throw $error; }
    exit(0);
}
if ($mode === 'cleanup') {
    $pdo->beginTransaction();
    try {
        foreach (['account_role_equipment_durability', 'account_role_equipment', 'account_role_state', 'accounts'] as $table)
            $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
        $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
        $pdo->commit();
        echo "equipment durability max fixture removed\n";
    } catch (Throwable $error) { $pdo->rollBack(); throw $error; }
    exit(0);
}

try {
    call_service($port, $clientId, wt(1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1') .
        f_string('imsi', 'durability-max-regression') .
        f_string('username', $account) . f_string('password', $password)));
    call_service($port, $clientId, wt(1, 6, f_u32('actorID', $role)));
    /* The established item-manager bootstrap is the compound 5/10 +
     * 7/7(type=1) request.  Its response includes the durable 1/7/7(type=2)
     * rows after the grid initializer has created the target manager. */
    call_service($port, $clientId, wt_combo(
        wt_object(5, 10, f_u32('id', $role)),
        wt_object(7, 7, f_u8('type', 1))));

    if (durability($pdo, $account, $role, 0) !== [1001, 44, 50] ||
        durability($pdo, $account, $role, 1) !== [1101, 70, 80]) {
        throw new RuntimeException('login did not migrate legacy durability into the equipment instance');
    }
    $repair = call_service($port, $clientId, wt(26, 1,
        f_u8('type', 2) . f_u32('id', 0xE3000001)));
    if (!has_object($repair, 26, 1) ||
        durability($pdo, $account, $role, 0) !== [1001, 50, 50] ||
        durability($pdo, $account, $role, 1) !== [1101, 80, 80] ||
        money($pdo, $account, $role) !== 984) {
        throw new RuntimeException('repair did not restore exactly the equip.dsh maxima');
    }
    echo "equipment durability max regression passed repaired=50/50,80/80 money=984\n";
} finally {
    try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
}
