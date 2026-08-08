<?php
/*
 * Scene-key identity regression.  These two persisted keys name distinct maps
 * and must survive settings "unstuck" -> direct 16/2 byte-for-byte:
 *
 *   c00蓬莱仙岛_02.sce
 *   00_蓬莱仙岛02.sce
 *   00蓬莱仙岛_02.sce
 *
 * It deliberately does not assert an NPC catalog for either unresolved key.
 * Borrowing an SCE or built-in NPC catalog from a similarly named map is the
 * failure this regression protects against.
 */

function entry($name, $value) { return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value; }
function f_u8($name, $value) { return entry($name, "\x00\x01" . chr($value)); }
function f_u32($name, $value) { return entry($name, "\x00\x04" . pack('N', $value)); }
function f_string($name, $value) { return entry($name, pack('n', strlen($value)) . $value); }
function object_record($major, $kind, $subtype, $fields = '') {
    return chr($major) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
}
function wt_objects(...$objects) {
    $body = implode('', $objects);
    return 'WT' . pack('n', 4 + strlen($body)) . $body;
}
function wt($major, $kind, $subtype, $fields = '') {
    return wt_objects(object_record($major, $kind, $subtype, $fields));
}
function read_exact($socket, $length) {
    $data = '';
    while (strlen($data) < $length && !feof($socket)) {
        $chunk = fread($socket, $length - strlen($data));
        if ($chunk === false || $chunk === '') break;
        $data .= $chunk;
    }
    return $data;
}
function call_service($port, $clientId, $packet = '', $flags = 0) {
    $metadata = pack('V', $clientId);
    $body = $metadata . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), strlen($metadata)) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    if (!$socket) throw new RuntimeException("connect failed: $errno $error");
    stream_set_timeout($socket, 5);
    if (fwrite($socket, $frame) !== strlen($frame)) throw new RuntimeException('short request write');
    $header = read_exact($socket, 20);
    if (strlen($header) !== 20 || substr($header, 0, 4) !== 'CBMR') throw new RuntimeException('bad response header');
    $length = unpack('V', substr($header, 12, 4))[1];
    $response = read_exact($socket, $length);
    fclose($socket);
    if (strlen($response) !== $length) throw new RuntimeException('short response');
    return $response;
}
function response_field($response, $kind, $subtype, $wanted) {
    if (substr($response, 0, 2) !== 'WT' || strlen($response) < 5) return null;
    $count = ord($response[4]);
    for ($offset = 5, $i = 0; $i < $count; ++$i) {
        if ($offset + 6 > strlen($response)) return null;
        $length = unpack('n', substr($response, $offset + 4, 2))[1];
        $end = $offset + $length;
        if ($length < 6 || $end > strlen($response)) return null;
        if (ord($response[$offset + 1]) === $kind && ord($response[$offset + 2]) === $subtype) {
            for ($field = $offset + 6; $field < $end;) {
                if ($field + 3 > $end) return null;
                $nameLength = ord($response[$field++]);
                if ($field + $nameLength + 2 > $end) return null;
                $name = substr($response, $field, $nameLength);
                $field += $nameLength;
                $valueLength = unpack('n', substr($response, $field, 2))[1];
                $field += 2;
                if ($field + $valueLength > $end) return null;
                $value = substr($response, $field, $valueLength);
                $field += $valueLength;
                if ($name === $wanted) return $value;
            }
        }
        $offset = $end;
    }
    return null;
}
function assert_same_scene_key($wireValue, $expected, $phase) {
    if ($wireValue === null || strlen($wireValue) < 2) {
        throw new RuntimeException("$phase missing scene field");
    }
    $length = unpack('n', substr($wireValue, 0, 2))[1];
    $actual = substr($wireValue, 2);
    if ($length !== strlen($actual) || $actual !== $expected) {
        throw new RuntimeException("$phase rewrote scene key expected=" . bin2hex($expected) .
            " actual=" . bin2hex($actual));
    }
}
$mode = $argv[1] ?? 'run';
$port = isset($argv[2]) ? (int)$argv[2] : 19090;
$accounts = [
    'codex_scene_key_identity_c',
    'codex_scene_key_identity_legacy',
    'codex_scene_key_identity_canonical'
];
$password = 'scene-key-identity-pass';
$role = 59317;
$clientId = 0x7A245931;
$scenes = [
    hex2bin('633030C5EEC0B3CFC9B5BA5F30322E736365'), /* c00蓬莱仙岛_02.sce */
    hex2bin('30305FC5EEC0B3CFC9B5BA5F30322E736365'), /* 00_蓬莱仙岛02.sce */
    hex2bin('3030C5EEC0B3CFC9B5BA5F30322E736365')   /* 00蓬莱仙岛_02.sce */
];
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root', getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
function cleanup($pdo, $account) {
    foreach (['account_role_equipment_durability','account_role_equipment','account_role_skills','account_role_tasks','account_role_backpack','account_role_state','accounts'] as $table) {
        $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
    }
    $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
}
if ($mode === 'setup') {
    foreach ($accounts as $account) cleanup($pdo, $account);
    $pdo->beginTransaction();
    try {
        foreach ($accounts as $index => $account) {
            $roleId = $role + $index;
            $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, $password]);
            $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)')->execute([$account, $roleId]);
            $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,5,1250,100,120,41,100,654,1000,?,338,125,0,0,1)')->execute([$account, $roleId, 'SceneKey', $scenes[$index]]);
        }
        $pdo->commit();
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    echo "scene-key identity fixture prepared\n";
    exit(0);
}
if ($mode === 'cleanup') {
    foreach ($accounts as $account) cleanup($pdo, $account);
    echo "scene-key identity fixture removed\n";
    exit(0);
}

try {
    foreach ($scenes as $index => $scene) {
        $account = $accounts[$index];
        call_service($port, $clientId + $index, wt(1, 1, 12,
            f_string('coreVer', '1') . f_string('appVer', '1') .
            f_string('imsi', 'scene-key-identity-regression-' . $index) .
            f_string('username', $account) . f_string('password', $password)));
        $unstuck = call_service($port, $clientId + $index,
            wt(0, 12, 3, f_u32('id', 0x5317)));
        assert_same_scene_key(response_field($unstuck, 16, 3, 'scene'),
                              $scene, 'settings unstuck #' . $index);
        $direct = call_service($port, $clientId + $index,
            wt(1, 16, 2, f_u8('type', 0)));
        assert_same_scene_key(response_field($direct, 16, 2, 'scene'),
                              $scene, 'direct 16/2 #' . $index);
        call_service($port, $clientId + $index, '', 4);
    }
    echo "scene-key identity regression passed distinct_keys=3\n";
} finally {
    foreach (array_keys($scenes) as $index) {
        try { call_service($port, $clientId + $index, '', 4); } catch (Throwable $ignored) {}
    }
}
