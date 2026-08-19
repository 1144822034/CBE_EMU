<?php
/*
 * Read-only wire check for a persisted account's title role list.
 *
 * The companion PowerShell launcher owns the service process and its artifact
 * directory. This script obtains the account password only to send the normal
 * login request; it never prints the password or mutates database rows.
 *
 * Usage:
 *   php scripts/title-role-list-account-regression.php verify <port> <database> <account> <artifact-dir>
 */

declare(strict_types=1);

function fail(string $message): never {
    throw new RuntimeException($message);
}

function db_password(): string {
    foreach (['CBE_AUTOMATION_MYSQL_PASSWORD', 'CBE_MYSQL_PASSWORD', 'CBE_TEST_MYSQL_PASSWORD'] as $name) {
        $value = getenv($name);
        if (is_string($value) && $value !== '') return $value;
    }
    /* Matches the current local development server default. */
    return '123456';
}

function db(string $database): PDO {
    if ($database !== 'jh_online_release') fail('refusing a database other than jh_online_release');
    return new PDO(
        'mysql:host=' . (getenv('CBE_AUTOMATION_MYSQL_HOST') ?: '127.0.0.1') .
        ';port=' . (getenv('CBE_AUTOMATION_MYSQL_PORT') ?: '3306') .
        ';dbname=' . $database . ';charset=utf8mb4',
        getenv('CBE_AUTOMATION_MYSQL_USER') ?: 'root', db_password(),
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
         PDO::ATTR_EMULATE_PREPARES => false]);
}

function entry(string $name, string $value): string {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}

function text_field(string $name, string $value): string {
    return entry($name, pack('n', strlen($value)) . $value);
}

function u32(string $name, int $value): string {
    return entry($name, "\0\x04" . pack('N', $value));
}

function wt(int $kind, int $subtype, string $fields = ''): string {
    $object = chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
    return 'WT' . pack('n', 4 + strlen($object)) . $object;
}

function read_exact($socket, int $length): string {
    $data = '';
    while (strlen($data) < $length) {
        $chunk = fread($socket, $length - strlen($data));
        if ($chunk === false) fail('socket read failed');
        if ($chunk === '') {
            $meta = stream_get_meta_data($socket);
            if (($meta['timed_out'] ?? false) || ($meta['eof'] ?? false)) {
                fail('short socket read expected=' . $length . ' actual=' . strlen($data));
            }
            usleep(1000);
            continue;
        }
        $data .= $chunk;
    }
    return $data;
}

function service_call(int $port, int $clientId, string $packet, int $flags = 0): string {
    $body = pack('V', $clientId) . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), 4) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    if ($socket === false) fail("service connection failed: $errno $error");
    stream_set_timeout($socket, 10);
    for ($offset = 0; $offset < strlen($frame);) {
        $written = fwrite($socket, substr($frame, $offset));
        if ($written === false || $written === 0) fail('short socket write');
        $offset += $written;
    }
    $header = read_exact($socket, 20);
    if (substr($header, 0, 4) !== 'CBMR') fail('invalid CBMR header');
    $length = unpack('Vlength', substr($header, 12, 4))['length'];
    $response = read_exact($socket, $length);
    fclose($socket);
    return $response;
}

function response_field(string $response, int $kind, int $subtype, string $wanted): ?string {
    if (strlen($response) < 5 || substr($response, 0, 2) !== 'WT') return null;
    $count = ord($response[4]);
    for ($offset = 5, $i = 0; $i < $count; ++$i) {
        if ($offset + 6 > strlen($response)) return null;
        $length = unpack('nlength', substr($response, $offset + 4, 2))['length'];
        $end = $offset + $length;
        if ($length < 6 || $end > strlen($response)) return null;
        if (ord($response[$offset]) === 1 && ord($response[$offset + 1]) === $kind &&
            ord($response[$offset + 2]) === $subtype) {
            for ($field = $offset + 6; $field < $end;) {
                if ($field + 3 > $end) return null;
                $nameLength = ord($response[$field++]);
                if ($field + $nameLength + 2 > $end) return null;
                $name = substr($response, $field, $nameLength);
                $field += $nameLength;
                $valueLength = unpack('nlength', substr($response, $field, 2))['length'];
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

function tagged_u32(string $data, int &$offset, string $phase): int {
    if ($offset + 6 > strlen($data) || substr($data, $offset, 2) !== "\0\x04") {
        fail($phase . ' has invalid tagged u32 at offset=' . $offset);
    }
    $value = unpack('Nvalue', substr($data, $offset + 2, 4))['value'];
    $offset += 6;
    return $value;
}

function tagged_u8(string $data, int &$offset, string $phase): int {
    if ($offset + 3 > strlen($data) || substr($data, $offset, 2) !== "\0\x01") {
        fail($phase . ' has invalid tagged u8 at offset=' . $offset);
    }
    $value = ord($data[$offset + 2]);
    $offset += 3;
    return $value;
}

function parse_actorinfo(string $data): array {
    $offset = 0;
    $count = tagged_u8($data, $offset, 'actorinfo count');
    $roles = [];
    for ($i = 0; $i < $count; ++$i) {
        $id = tagged_u32($data, $offset, "actorinfo row $i id");
        tagged_u8($data, $offset, "actorinfo row $i job");
        tagged_u8($data, $offset, "actorinfo row $i sex");
        if ($offset + 2 > strlen($data)) fail("actorinfo row $i missing name length");
        $nameLength = unpack('nlength', substr($data, $offset, 2))['length'];
        $offset += 2;
        if ($nameLength == 0 || $offset + $nameLength > strlen($data) ||
            $data[$offset + $nameLength - 1] !== "\0") {
            fail("actorinfo row $i has invalid NUL-terminated name");
        }
        $offset += $nameLength;
        if ($offset + 4 > strlen($data) || substr($data, $offset, 2) !== "\0\x02") {
            fail("actorinfo row $i has invalid tagged level");
        }
        $offset += 4;
        $roles[] = $id;
    }
    if ($offset !== strlen($data)) fail('actorinfo has trailing bytes=' . (strlen($data) - $offset));
    return $roles;
}

$mode = $argv[1] ?? '';
$port = isset($argv[2]) ? (int)$argv[2] : 0;
$database = $argv[3] ?? '';
$account = $argv[4] ?? '';
$artifactDir = $argv[5] ?? '';
if ($mode !== 'verify' || $port < 1024 || $account === '' || $artifactDir === '') {
    fail('usage: verify <port> <database> <account> <artifact-dir>');
}
if (!is_dir($artifactDir)) fail('artifact directory does not exist');
if (!preg_match('/^[A-Za-z0-9_]{1,63}$/', $account)) fail('account name is not artifact-safe');
$accountArtifactDir = $artifactDir . DIRECTORY_SEPARATOR . 'account-' . $account;
if (!mkdir($accountArtifactDir, 0777, false) && !is_dir($accountArtifactDir)) {
    fail('could not create account artifact directory');
}

$pdo = db($database);
$accountRow = $pdo->prepare('SELECT password_value FROM accounts WHERE account_id=?');
$accountRow->execute([$account]);
$password = $accountRow->fetchColumn();
if (!is_string($password) || $password === '') fail('account password row is unavailable');
$rolesStatement = $pdo->prepare(
    'SELECT role_id FROM account_roles WHERE account_id=? ORDER BY role_index,role_id');
$rolesStatement->execute([$account]);
$expectedIds = array_map(static function (array $row): int {
    return (int)$row['role_id'];
}, $rolesStatement->fetchAll(PDO::FETCH_ASSOC));
if ($expectedIds === []) fail('database has no persisted roles for account');

$clientId = 0x7A00723;
$requests = [
    '01-login-1-12' => wt(1, 12,
        text_field('coreVer', '1') . text_field('appVer', '1') .
        text_field('imsi', 'title-role-list-account-v1') .
        text_field('username', $account) . text_field('password', $password)),
    '02-role-list-stage-1-16' => wt(1, 16),
    '03-server-select-1-4' => wt(1, 4, u32('serverID', 1) . u32('moneytype', 0)),
];

try {
    $responses = [];
    foreach ($requests as $name => $request) {
        $response = service_call($port, $clientId, $request);
        file_put_contents($accountArtifactDir . DIRECTORY_SEPARATOR . $name . '.request.bin', $request);
        file_put_contents($accountArtifactDir . DIRECTORY_SEPARATOR . $name . '.response.bin', $response);
        $responses[$name] = $response;
    }
    $actorinfo = response_field($responses['03-server-select-1-4'], 1, 4, 'actorinfo');
    if ($actorinfo === null) fail('1/1/4 response is missing actorinfo');
    $actualIds = parse_actorinfo($actorinfo);
    if ($actualIds !== $expectedIds) {
        fail('actorinfo roles mismatch expected=' . json_encode($expectedIds) .
             ' actual=' . json_encode($actualIds));
    }
    $summary = [
        'scenario' => 'title-role-list-account-v1',
        'account' => $account,
        'database' => $database,
        'requests' => ['1/1/12', '1/1/16', '1/1/4'],
        'expected_role_count' => count($expectedIds),
        'actorinfo_role_count' => count($actualIds),
        'actor_ids' => $actualIds,
    ];
    file_put_contents($accountArtifactDir . DIRECTORY_SEPARATOR . 'wire-summary.json',
                      json_encode($summary, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES) . "\n");
    echo 'title role-list account regression passed account=' . $account .
         ' roles=' . count($actualIds) . "\n";
} finally {
    try { service_call($port, $clientId, '', 4); } catch (Throwable $ignored) {}
}
