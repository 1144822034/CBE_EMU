<?php

function entry($name, $value) {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}
function f_u8($name, $value) { return entry($name, "\x00\x01" . chr($value)); }
function f_u32($name, $value) { return entry($name, "\x00\x04" . pack('N', $value)); }
function f_string($name, $value) { return entry($name, pack('n', strlen($value)) . $value); }
function object_record($kind, $subtype, $fields = '') {
    return chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
}
function wt_packet($objects) {
    $body = implode('', $objects);
    return 'WT' . pack('n', 4 + strlen($body)) . $body;
}
function wt($kind, $subtype, $fields = '') {
    return wt_packet([object_record($kind, $subtype, $fields)]);
}
function scene_startup_request() {
    return wt_packet([
        object_record(12, 1), object_record(7, 42), object_record(17, 1),
        object_record(6, 1), object_record(6, 13), object_record(6, 14),
        object_record(2, 10, f_u8('Type', 101)), object_record(0x19, 5),
    ]);
}
function call_service($port, $clientId, $packet = '', $flags = 0) {
    $meta = pack('V', $clientId);
    $body = $meta . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), strlen($meta)) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $errstr, 3);
    if (!$socket) throw new RuntimeException("connect failed: $errno $errstr");
    stream_set_timeout($socket, 3);
    $written = fwrite($socket, $frame);
    if ($written !== strlen($frame)) {
        throw new RuntimeException('short request write: ' . var_export($written, true)
            . '/' . strlen($frame));
    }
    $header = '';
    while (strlen($header) < 20 && !feof($socket)) {
        $header .= fread($socket, 20 - strlen($header));
    }
    if (strlen($header) !== 20 || substr($header, 0, 4) !== 'CBMR') {
        throw new RuntimeException('bad response header: ' . bin2hex($header));
    }
    $values = unpack('Vversion/Vflags/Vlen/Vevent', substr($header, 4));
    $response = '';
    while (strlen($response) < $values['len'] && !feof($socket)) {
        $response .= fread($socket, $values['len'] - strlen($response));
    }
    fclose($socket);
    if (strlen($response) !== $values['len']) throw new RuntimeException('short response');
    return $response;
}
function login_role($port, $clientId, $accountId, $password, $roleId) {
    $login = wt(1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1')
        . f_string('imsi', 'designation-level-regression')
        . f_string('username', $accountId) . f_string('password', $password));
    call_service($port, $clientId, $login);
    call_service($port, $clientId, wt(1, 6, f_u32('actorID', $roleId)));
    call_service($port, $clientId, scene_startup_request());
}
function gbk($value) {
    return iconv('UTF-8', 'GBK//IGNORE', $value);
}
function http_post($port, $path, $fields) {
    $body = http_build_query($fields);
    $context = stream_context_create(['http' => [
        'method' => 'POST',
        'header' => "Content-Type: application/x-www-form-urlencoded\r\n"
            . 'Content-Length: ' . strlen($body) . "\r\n"
            . "Connection: close\r\n",
        'content' => $body,
        'ignore_errors' => true,
        'follow_location' => 0,
        'timeout' => 8,
    ]]);
    $response = @file_get_contents('http://127.0.0.1:' . $port . $path, false, $context);
    $headers = $http_response_header ?? [];
    if (!$headers || !preg_match('/\s(3\d{2})\s/', $headers[0], $matches)) {
        throw new RuntimeException('HTTP registration did not redirect: ' . implode(' | ', $headers));
    }
    return $response;
}

$port = isset($argv[1]) ? intval($argv[1]) : 19090;
$adminPort = getenv('CBE_TEST_ADMIN_PORT') ? (int)getenv('CBE_TEST_ADMIN_PORT') : 19091;
$dbPassword = getenv('CBE_TEST_MYSQL_PASSWORD');
if ($dbPassword === false) $dbPassword = '123456';
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root', $dbPassword,
    [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
$suffix = (string)random_int(100000, 999999);
$accounts = ['codex_title_high_' . $suffix, 'codex_title_low_' . $suffix];
$roles = [68000 + (getmypid() % 1000) * 2, 68001 + (getmypid() % 1000) * 2];
$clients = [0x7A2A1001 + (getmypid() & 0xff), 0x7A2A1101 + (getmypid() & 0xff)];
$password = 'designation-pass';
$scene = hex2bin('633030C5EEC0B3CFC9B5BA5F30332E736365');
$insertState = $pdo->prepare(
    'INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)');
$insertRole = $pdo->prepare(
    'INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,'
    . 'level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,'
    . 'designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,?,?,120,120,100,100,0,0,?,220,440,0,16,1)');
$deleteState = $pdo->prepare('DELETE FROM account_role_state WHERE account_id=?');
$deleteAccount = $pdo->prepare('DELETE FROM accounts WHERE account_id=?');

try {
    /*
     * Registration uses the running service so its in-memory account cache is
     * authoritative before the game-login request.  The role fixtures are
     * still direct MySQL rows, and are first loaded only after this point.
     */
    foreach ($accounts as $accountId) {
        http_post($adminPort, '/user/register', ['account' => $accountId, 'password' => $password]);
    }
    $insertState->execute([$accounts[0], $roles[0]]);
    /* level 60 starts at 59*60/2*100 = 177000 EXP. */
    $insertRole->execute([$accounts[0], $roles[0], 'LevelHigh', 60, 177000, $scene]);
    $insertState->execute([$accounts[1], $roles[1]]);
    $insertRole->execute([$accounts[1], $roles[1], 'LevelLow', 1, 0, $scene]);

    login_role($port, $clients[0], $accounts[0], $password, $roles[0]);
    $list = call_service($port, $clients[0], wt(0x17, 1, f_u8('index', 0)));
    if (strpos($list, f_u8('count', 14)) === false
        || strpos($list, f_u8('equiptype', 16)) === false) {
        throw new RuntimeException('level-60 designation list has wrong count/active title: ' . bin2hex($list));
    }
    for ($i = 0; $i <= 12; ++$i) {
        if (strpos($list, 'level_name' . $i . '.gif') === false) {
            throw new RuntimeException("level designation badge missing: level_name{$i}.gif");
        }
    }
    foreach (['不堪一击', '小试牛刀', '开山鼻祖'] as $title) {
        if (strpos($list, gbk($title)) === false) {
            throw new RuntimeException("level designation name missing: $title");
        }
    }

    $select = call_service($port, $clients[0], wt(0x17, 3, f_u8('type', 28)));
    if (strpos($select, "\x01\x17\x03") === false
        || strpos($select, "\x01\x17\x02") === false
        || strpos($select, f_u8('result', 1)) === false
        || strpos($select, 'level_name12.gif') === false
        || strpos($select, gbk('开山鼻祖')) === false) {
        throw new RuntimeException('level-60 select response misses 23/3 confirmation or 23/2 actor update');
    }
    $selectedId = (int)$pdo->query('SELECT designation_id FROM account_roles WHERE account_id='
        . $pdo->quote($accounts[0]) . ' AND role_id=' . $roles[0])->fetchColumn();
    if ($selectedId !== 28) throw new RuntimeException("selected designation was not persisted: $selectedId");

    login_role($port, $clients[1], $accounts[1], $password, $roles[1]);
    $locked = call_service($port, $clients[1], wt(0x17, 3, f_u8('type', 26)));
    if (strpos($locked, "\x01\x17\x03") === false
        || strpos($locked, f_u8('result', 0)) === false
        || strpos($locked, "\x01\x17\x02") !== false) {
        throw new RuntimeException('locked level designation emitted an actor update or a non-failure result');
    }

    echo 'role designation level regression passed rows=14 selected=28 locked=26' . PHP_EOL;
} finally {
    foreach ($clients as $clientId) {
        try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
    }
    foreach ($accounts as $accountId) {
        $deleteState->execute([$accountId]);
        $deleteAccount->execute([$accountId]);
    }
}
