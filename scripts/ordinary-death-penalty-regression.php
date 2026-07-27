<?php

/*
 * Direct regression for the ordinary death confirmation WT 1/7/14(result=2).
 *
 * Usage:
 *   php scripts/ordinary-death-penalty-regression.php setup
 *   bin/jh-online-server.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19153
 *   php scripts/ordinary-death-penalty-regression.php run 19153
 *   php scripts/ordinary-death-penalty-regression.php cleanup
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
    $body = pack('V', $clientId) . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), 4) . $body;
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
function has_object(string $response, int $kind, int $subtype): bool {
    if (substr($response, 0, 2) !== 'WT' || strlen($response) < 5) return false;
    for ($offset = 5, $i = 0, $count = ord($response[4]); $i < $count; ++$i) {
        if ($offset + 6 > strlen($response)) return false;
        $length = unpack('nlength', substr($response, $offset + 4, 2))['length'];
        if ($length < 6 || $offset + $length > strlen($response)) return false;
        if (ord($response[$offset + 1]) === $kind && ord($response[$offset + 2]) === $subtype) return true;
        $offset += $length;
    }
    return false;
}

$mode = $argv[1] ?? 'run';
$port = isset($argv[2]) ? (int)$argv[2] : 19153;
$account = 'codex_ordinary_death_penalty';
$password = 'ordinary-death-penalty-pass';
$roleId = 59427;
$clientId = 0x7A235942;
$sourceScene = hex2bin('633030C5EEC0B3CFC9B5BA5F30312E736365'); /* c00蓬莱仙岛_01.sce */
$targetScene = hex2bin('633030C5EEC0B3CFC9B5BA5F30332E736365'); /* c00蓬莱仙岛_03.sce */
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root',
    getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);

function cleanup_fixture(PDO $pdo, string $account): void {
    foreach (['account_role_equipment_durability', 'account_role_equipment', 'account_role_skills',
              'account_role_tasks', 'account_role_backpack', 'account_role_state'] as $table) {
        $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
    }
    $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
    $pdo->prepare('DELETE FROM accounts WHERE account_id=?')->execute([$account]);
}

if ($mode === 'setup') {
    cleanup_fixture($pdo, $account);
    $pdo->beginTransaction();
    try {
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, $password]);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)')->execute([$account, $roleId]);
        /* Level 5 spans [1000,1500).  60% of this upgrade is 300 EXP;
         * 1300 must become exactly 1000 and remain level 5. */
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,5,1300,0,120,41,100,654,1000,?,220,454,0,0,1)')
            ->execute([$account, $roleId, 'OrdinaryDeathPenalty', $sourceScene]);
        $pdo->commit();
        echo "ordinary death-penalty fixture prepared\n";
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    exit(0);
}
if ($mode === 'cleanup') {
    cleanup_fixture($pdo, $account);
    echo "ordinary death-penalty fixture removed\n";
    exit(0);
}

try {
    call_service($port, $clientId, wt(1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1') .
        f_string('imsi', 'ordinary-death-penalty') .
        f_string('username', $account) . f_string('password', $password)));
    call_service($port, $clientId, wt(1, 6, f_u32('actorID', $roleId)));
    $response = call_service($port, $clientId, wt(7, 14, f_u8('result', 2)));
    if (ord($response[4]) !== 2 || !has_object($response, 20, 1) || !has_object($response, 30, 1)) {
        throw new RuntimeException('ordinary death response contract mismatch: ' . bin2hex($response));
    }

    $query = $pdo->prepare('SELECT level,exp,hp,hp_max,mp,mp_max,money,scene,pos_x,pos_y FROM account_roles WHERE account_id=? AND role_id=?');
    $query->execute([$account, $roleId]);
    $state = $query->fetch(PDO::FETCH_ASSOC);
    $expectHp = intdiv((int)$state['hp_max'] * 30 + 99, 100);
    $expectMp = intdiv((int)$state['mp_max'] * 30 + 99, 100);
    if (!$state || (int)$state['level'] !== 5 || (int)$state['exp'] !== 1000 ||
        (int)$state['money'] !== 647 || (int)$state['hp'] !== $expectHp ||
        (int)$state['mp'] !== $expectMp || $state['scene'] !== $targetScene ||
        (int)$state['pos_x'] === 0 || (int)$state['pos_y'] === 0) {
        throw new RuntimeException('ordinary death state mismatch: ' . json_encode($state, JSON_UNESCAPED_UNICODE));
    }
    $beforeRepeat = $state;
    $repeat = call_service($port, $clientId, wt(7, 14, f_u8('result', 2)));
    if (!has_object($repeat, 20, 1) || has_object($repeat, 30, 1)) {
        throw new RuntimeException('living repeated ordinary death request was not rejected');
    }
    $query->execute([$account, $roleId]);
    if ($query->fetch(PDO::FETCH_ASSOC) !== $beforeRepeat) {
        throw new RuntimeException('rejected repeated request changed the persisted role');
    }
    echo 'ordinary death penalty regression passed level=5 exp=1300->1000 money=654->647' . PHP_EOL;
} finally {
    try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
}
