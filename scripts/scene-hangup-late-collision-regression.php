<?php
/*
 * Service-side regression for a real client ordering hazard:
 *
 *   scene-poll: 2/2 + 4/5 + 4/11(type=1)
 *   delayed client scene collision: 4/1
 *   already scheduled client auto timer: 4/12
 *
 * This runner does not claim to render a client frame.  The parser-side
 * contract is recorded in docs/re/2026-08-09-scene-hangup-late-collision-ownership.md:
 * mmBattle HandleServerBattleCmd(0x7BD0) rebuilds slots for 4/5 but preserves
 * them for 4/11.  This test proves the service retains that contract and
 * still accepts the native 4/12 continuation.
 *
 * Usage: php scripts/scene-hangup-late-collision-regression.php <port>
 */

declare(strict_types=1);

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
function object(int $kind, int $subtype, string $fields = ''): string {
    return chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
}
function wt(array $objects): string {
    $body = implode('', $objects);
    return 'WT' . pack('n', 4 + strlen($body)) . $body;
}
function read_exact($socket, int $length): string {
    $result = '';
    while (strlen($result) < $length && !feof($socket)) {
        $part = fread($socket, $length - strlen($result));
        if ($part === false || $part === '') break;
        $result .= $part;
    }
    return $result;
}
function call_service(int $port, int $clientId, string $packet = '', int $flags = 0): string {
    $metadata = pack('V', $clientId);
    $body = $metadata . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), strlen($metadata)) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    if ($socket === false) throw new RuntimeException("connect failed: $errno $error");
    stream_set_timeout($socket, 5);
    if (fwrite($socket, $frame) !== strlen($frame)) throw new RuntimeException('short request write');
    $header = read_exact($socket, 20);
    if (strlen($header) !== 20 || substr($header, 0, 4) !== 'CBMR') {
        throw new RuntimeException('bad CBMR header');
    }
    $length = unpack('Vlength', substr($header, 12, 4))['length'];
    $response = read_exact($socket, $length);
    fclose($socket);
    if (strlen($response) !== $length) throw new RuntimeException('short CBMR response');
    return $response;
}
/** @return array<int, array{kind:int, subtype:int, fields:array<string,string>}> */
function parse_response(string $response): array {
    if (strlen($response) < 5 || substr($response, 0, 2) !== 'WT') {
        throw new RuntimeException('response is not WT: ' . bin2hex($response));
    }
    $objects = [];
    $offset = 5;
    $count = ord($response[4]);
    for ($i = 0; $i < $count; ++$i) {
        if ($offset + 6 > strlen($response)) throw new RuntimeException('truncated object header');
        $length = unpack('nlength', substr($response, $offset + 4, 2))['length'];
        $end = $offset + $length;
        if ($length < 6 || $end > strlen($response)) throw new RuntimeException('invalid object length');
        $fields = [];
        for ($field = $offset + 6; $field < $end;) {
            $nameLength = ord($response[$field++]);
            if ($field + $nameLength + 2 > $end) throw new RuntimeException('truncated field name');
            $name = substr($response, $field, $nameLength);
            $field += $nameLength;
            $valueLength = unpack('nlength', substr($response, $field, 2))['length'];
            $field += 2;
            if ($field + $valueLength > $end) throw new RuntimeException('truncated field value');
            $fields[$name] = substr($response, $field, $valueLength);
            $field += $valueLength;
        }
        $objects[] = [
            'kind' => ord($response[$offset + 1]),
            'subtype' => ord($response[$offset + 2]),
            'fields' => $fields,
        ];
        $offset = $end;
    }
    if ($offset !== strlen($response)) throw new RuntimeException('trailing response bytes');
    return $objects;
}
function tagged_u8(?string $value): ?int {
    return $value !== null && strlen($value) === 3 && substr($value, 0, 2) === "\x00\x01"
        ? ord($value[2]) : null;
}
function find_object(array $objects, int $kind, int $subtype): ?array {
    foreach ($objects as $object) {
        if ($object['kind'] === $kind && $object['subtype'] === $subtype) return $object;
    }
    return null;
}

$port = isset($argv[1]) ? (int)$argv[1] : 19370;
$clientId = 0x7A227370;
$roleId = 810001;

try {
    $login = wt([object(1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1') .
        f_string('imsi', 'scene-hangup-late-collision-v1') .
        f_string('username', 'guest00001') . f_string('password', 'automation-only'))]);
    call_service($port, $clientId, $login);
    call_service($port, $clientId, wt([object(1, 6, f_u32('actorID', $roleId))]));

    /* Exact 39-byte role-scene resource-followup request: it is the client
     * scene-ready boundary, not a server-side presence shortcut. */
    $sceneReady = wt([
        object(6, 1), object(6, 13), object(6, 14),
        object(2, 10, f_u8('Type', 101)), object(25, 5),
    ]);
    if (strlen($sceneReady) !== 39) throw new RuntimeException('scene-ready request shape changed');
    call_service($port, $clientId, $sceneReady);

    /* Genuine scene-hangup enter request family: 2/10(Type=2)+25/3. */
    $start = parse_response(call_service($port, $clientId, wt([
        object(2, 10, f_u8('Type', 2)), object(25, 3),
    ])));
    $startBattle = find_object($start, 4, 5);
    $startAuto = find_object($start, 4, 11);
    if ($startBattle === null || $startAuto === null ||
        tagged_u8($startAuto['fields']['result'] ?? null) !== 1 ||
        tagged_u8($startAuto['fields']['type'] ?? null) !== 1) {
        throw new RuntimeException('hangup start must establish 4/5 + 4/11(type=1)');
    }

    /* This is a legitimate late 1/4/1 from the live scene.  It used to create
     * a second 4/5 and clear auto before the first battle's 4/12 timer fired. */
    $lateCollision = parse_response(call_service($port, $clientId, wt([
        object(4, 1, f_u32('id', 105) . f_u32('index', 9) .
            f_u32('posx', 292) . f_u32('posy', 484)),
    ])));
    $reaffirm = find_object($lateCollision, 4, 11);
    if (count($lateCollision) !== 1 || $reaffirm === null ||
        tagged_u8($reaffirm['fields']['result'] ?? null) !== 1 ||
        tagged_u8($reaffirm['fields']['type'] ?? null) !== 1 ||
        find_object($lateCollision, 4, 5) !== null) {
        throw new RuntimeException('late 4/1 must retain the existing battle with only 4/11(type=1)');
    }

    /* Native BattleScene automatic timer request.  A valid response proves
     * that the late collision did not cancel or replace the armed session. */
    $replay = parse_response(call_service($port, $clientId, wt([object(4, 12)])));
    if (find_object($replay, 4, 6) === null) {
        throw new RuntimeException('native 4/12 was not accepted after late collision');
    }
    echo "scene-hangup-late-collision-v1 passed: 4/1 -> 4/11(type=1), then 4/12 -> 4/6\n";
} finally {
    try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
}
