<?php

/*
 * Usage:
 *   php scripts/battle-auto-toggle-regression.php setup
 *   start a fresh service on the selected port
 *   php scripts/battle-auto-toggle-regression.php run 19152
 *   php scripts/battle-auto-toggle-regression.php cleanup
 *
 * The fixture proves the parser-facing state transition without modifying an
 * interactive role: enable auto (4/11 type=1), execute one normal round, then
 * cancel auto (4/11 type=0).  A normal 4/6 action after the enable response
 * proves that the toggle did not consume or reset the server battle session.
 */
function entry(string $name, string $value): string {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}
function f_u8(string $name, int $value): string { return entry($name, "\x00\x01" . chr($value)); }
function f_u32(string $name, int $value): string { return entry($name, "\x00\x04" . pack('N', $value)); }
function f_string(string $name, string $value): string { return entry($name, pack('n', strlen($value)) . $value); }
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
    if (strlen($header) !== 20 || substr($header, 0, 4) !== 'CBMR') throw new RuntimeException('bad CBMR header');
    $length = unpack('Vlength', substr($header, 12, 4))['length'];
    $response = read_exact($socket, $length);
    fclose($socket);
    if (strlen($response) !== $length) throw new RuntimeException('short response');
    return $response;
}
function object_field(string $response, int $wantKind, int $wantSubtype, string $wantField): ?string {
    if (substr($response, 0, 2) !== 'WT' || strlen($response) < 5) return null;
    for ($offset = 5, $i = 0, $count = ord($response[4]); $i < $count; ++$i) {
        if ($offset + 6 > strlen($response)) return null;
        $length = unpack('nlength', substr($response, $offset + 4, 2))['length'];
        $end = $offset + $length;
        if ($length < 6 || $end > strlen($response)) return null;
        if (ord($response[$offset + 1]) === $wantKind && ord($response[$offset + 2]) === $wantSubtype) {
            for ($field = $offset + 6; $field < $end;) {
                $nameLength = ord($response[$field++]);
                if ($field + $nameLength + 2 > $end) return null;
                $name = substr($response, $field, $nameLength); $field += $nameLength;
                $valueLength = unpack('nlength', substr($response, $field, 2))['length']; $field += 2;
                if ($field + $valueLength > $end) return null;
                $value = substr($response, $field, $valueLength); $field += $valueLength;
                if ($name === $wantField) return $value;
            }
        }
        $offset = $end;
    }
    return null;
}
function tagged_u8(?string $value): ?int {
    return $value !== null && strlen($value) === 3 && substr($value, 0, 2) === "\x00\x01" ? ord($value[2]) : null;
}

$mode = $argv[1] ?? 'run';
$port = isset($argv[2]) ? (int)$argv[2] : 19152;
$account = 'codex_battle_auto_toggle';
$password = 'battle-auto-toggle-pass';
$roleId = 59152;
$clientId = 0x7A225152;
$scene = hex2bin('633030C5EEC0B3CFC9B5BA5F30312E736365'); /* c00蓬莱仙岛_01.sce */
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root',
    getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);

function delete_fixture(PDO $pdo, string $account): void {
    foreach (['account_role_equipment_durability', 'account_role_equipment', 'account_role_skills',
              'account_role_tasks', 'account_role_backpack', 'account_role_state'] as $table) {
        $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
    }
    $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
    $pdo->prepare('DELETE FROM accounts WHERE account_id=?')->execute([$account]);
}

if ($mode === 'setup') {
    $pdo->beginTransaction();
    try {
        delete_fixture($pdo, $account);
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, $password]);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)')->execute([$account, $roleId]);
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,1,0,120,120,100,100,0,0,?,220,440,0,0,1)')
            ->execute([$account, $roleId, 'BattleAutoToggle', $scene]);
        $pdo->commit();
        echo "battle auto-toggle fixture prepared\n";
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    exit(0);
}
if ($mode === 'cleanup') {
    delete_fixture($pdo, $account);
    echo "battle auto-toggle fixture removed\n";
    exit(0);
}

try {
    call_service($port, $clientId, wt(1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1') .
        f_string('imsi', 'battle-auto-toggle-regression') .
        f_string('username', $account) . f_string('password', $password)));
    call_service($port, $clientId, wt(1, 6, f_u32('actorID', $roleId)));
    $start = call_service($port, $clientId,
        wt(4, 1, f_u32('id', 1) . f_u32('index', 0) . f_u32('posx', 0) . f_u32('posy', 0)));
    if (object_field($start, 4, 5, 'battleinfo') === null) throw new RuntimeException('battle start missing 4/5');

    $enabled = call_service($port, $clientId, wt(4, 11, f_u8('type', 1)));
    if (tagged_u8(object_field($enabled, 4, 11, 'result')) !== 1 ||
        tagged_u8(object_field($enabled, 4, 11, 'type')) !== 1) {
        throw new RuntimeException('auto-enable did not return 4/11 result=1,type=1: ' . bin2hex($enabled));
    }

    $action = call_service($port, $clientId, wt(4, 2, f_u32('index', 0) . f_u32('Operate', 0)));
    if (object_field($action, 4, 6, 'actioninfo') === null) {
        throw new RuntimeException('battle operation after auto-enable missing 4/6: ' . bin2hex($action));
    }

    $disabled = call_service($port, $clientId, wt(4, 11, f_u8('type', 0)));
    if (tagged_u8(object_field($disabled, 4, 11, 'result')) !== 1 ||
        tagged_u8(object_field($disabled, 4, 11, 'type')) !== 0) {
        throw new RuntimeException('auto-disable did not return 4/11 result=1,type=0: ' . bin2hex($disabled));
    }
    echo 'battle auto-toggle regression passed enable=4/11 on action=4/6 disable=4/11 off' . PHP_EOL;
} finally {
    try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
}
