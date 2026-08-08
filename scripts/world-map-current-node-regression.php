<?php
/*
 * Isolated wire regression for the world-map current-node contract.
 *
 * ActorInfo's final scene string becomes the local scene controller's
 * currentMapIdText.  LoadSceneRes later performs an exact mode-4 sMap.dsh
 * lookup with that value. This fixture seeds the historical bare spelling
 * c04临安府_01, then verifies that role load performs a one-time sMap.dsh
 * migration and persists the exact c04临安府_01.sce identity. Runtime never
 * compares the two spellings as aliases.
 *
 * Usage:
 *   php scripts/world-map-current-node-regression.php prepare <database>
 *   php scripts/world-map-current-node-regression.php verify <service-port> <database>
 *   php scripts/world-map-current-node-regression.php cleanup <database>
 */

declare(strict_types=1);

const TEST_ACCOUNT = 'codex_world_map_current_node';
const TEST_PASSWORD = 'world-map-current-node-pass';
const TEST_ROLE_ID = 59941;
const TEST_CLIENT_ID = 0x7A2F9941;

function require_test_database(string $database): void {
    if (!preg_match('/^jh_online_autotest_[0-9a-f]{16,32}$/', $database)) {
        throw new InvalidArgumentException('refusing non-isolated automation database');
    }
}

function pdo(string $database): PDO {
    require_test_database($database);
    $password = getenv('CBE_AUTOMATION_MYSQL_PASSWORD');
    if ($password === false || $password === '') {
        throw new RuntimeException('CBE_AUTOMATION_MYSQL_PASSWORD is required');
    }
    return new PDO(
        'mysql:host=' . (getenv('CBE_AUTOMATION_MYSQL_HOST') ?: '127.0.0.1') .
        ';port=' . (getenv('CBE_AUTOMATION_MYSQL_PORT') ?: '3306') .
        ';dbname=' . $database . ';charset=utf8mb4',
        getenv('CBE_AUTOMATION_MYSQL_USER') ?: 'root', $password,
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_EMULATE_PREPARES => false]);
}

function expect(bool $condition, string $message): void {
    if (!$condition) throw new RuntimeException($message);
}

function wt_entry(string $name, string $value): string {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}
function wt_string(string $name, string $value): string {
    return wt_entry($name, pack('n', strlen($value)) . $value);
}
function wt_u32(string $name, int $value): string {
    return wt_entry($name, "\x00\x04" . pack('N', $value));
}
function wt_packet(int $kind, int $subtype, string $fields = ''): string {
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
function service_call(int $port, string $packet, int $flags = 0): string {
    $metadata = pack('V', TEST_CLIENT_ID);
    $body = $metadata . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), strlen($metadata)) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    expect($socket !== false, "service connection failed: $errno $error");
    stream_set_timeout($socket, 5);
    expect(fwrite($socket, $frame) === strlen($frame), 'service request write was short');
    $header = read_exact($socket, 20);
    expect(strlen($header) === 20 && substr($header, 0, 4) === 'CBMR', 'invalid CBMR header');
    $length = unpack('V', substr($header, 12, 4))[1];
    $response = read_exact($socket, $length);
    fclose($socket);
    expect(strlen($response) === $length, 'service response was short');
    return $response;
}
function response_field(string $response, int $kind, int $subtype, string $wanted): ?string {
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
function cleanup(PDO $pdo): void {
    foreach (['account_role_equipment_durability', 'account_role_equipment', 'account_role_skills',
              'account_role_tasks', 'account_role_backpack', 'account_role_state', 'accounts'] as $table) {
        $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([TEST_ACCOUNT]);
    }
    $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([TEST_ACCOUNT]);
}

$mode = $argv[1] ?? '';
$database = $argv[2] ?? '';
require_test_database($database);
$pdo = pdo($database);
$scene = iconv('UTF-8', 'GBK//IGNORE', 'c04临安府_01.sce');
expect(is_string($scene) && $scene !== '', 'could not encode Linan scene key as GBK');
$legacyScene = substr($scene, 0, -4);

if ($mode === 'prepare') {
    cleanup($pdo);
    $pdo->beginTransaction();
    try {
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')
            ->execute([TEST_ACCOUNT, TEST_PASSWORD]);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,6,?,1)')
            ->execute([TEST_ACCOUNT, TEST_ROLE_ID]);
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,5,1250,100,120,41,100,654,1000,?,201,196,0,0,1)')
            ->execute([TEST_ACCOUNT, TEST_ROLE_ID, 'WorldMapNode', $legacyScene]);
        $pdo->commit();
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    echo "world-map current-node fixture prepared\n";
    exit(0);
}
if ($mode === 'cleanup') {
    cleanup($pdo);
    echo "world-map current-node fixture removed\n";
    exit(0);
}
if ($mode !== 'verify') {
    throw new InvalidArgumentException('expected prepare, verify, or cleanup');
}

$port = isset($argv[3]) ? (int)$argv[3] : 19326;
try {
    service_call($port, wt_packet(1, 12,
        wt_string('coreVer', '1') . wt_string('appVer', '1') .
        wt_string('imsi', 'world-map-current-node-regression') .
        wt_string('username', TEST_ACCOUNT) . wt_string('password', TEST_PASSWORD)));
    $roleSelect = service_call($port, wt_packet(1, 6, wt_u32('actorID', TEST_ROLE_ID)));
    $actorInfo = response_field($roleSelect, 1, 6, 'actorinfo');
    expect($actorInfo !== null, 'role-select response does not contain 1/1/6.actorinfo');
    $wireScene = pack('n', strlen($scene) + 1) . $scene . "\0";
    expect(strpos($actorInfo, $wireScene) !== false,
           'ActorInfo scene key is not the exact c04临安府_01.sce sMap key; actual=' . bin2hex($actorInfo));
    $stored = $pdo->prepare('SELECT scene FROM account_roles WHERE account_id=? AND role_id=?');
    $stored->execute([TEST_ACCOUNT, TEST_ROLE_ID]);
    $storedScene = $stored->fetchColumn();
    expect(is_string($storedScene) && $storedScene === $scene,
           'legacy scene was not migrated to the exact sMap key; actual=' . bin2hex((string)$storedScene));
    echo "world-map current-node regression passed scene_key=smap-exact migration=one-time actorinfo=1/1/6\n";
} finally {
    try { service_call($port, '', 4); } catch (Throwable $ignored) {}
}
