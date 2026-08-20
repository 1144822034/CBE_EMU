<?php
/*
 * Isolated two-client service regression for the native nearby-player spar
 * handshake and duel round barrier.  It is launched only by
 * run-duel-round-barrier-automation.ps1.
 */
declare(strict_types=1);

function test_db(): string {
    $db = getenv('CBE_TEST_MYSQL_DATABASE') ?: '';
    if (!preg_match('/^jh_online_autotest_[0-9a-f]{16,32}$/', $db)) {
        throw new RuntimeException('refusing non-isolated test database');
    }
    return $db;
}
function pdo(): PDO {
    return new PDO(
        'mysql:host=' . (getenv('CBE_TEST_MYSQL_HOST') ?: '127.0.0.1') .
        ';port=' . (getenv('CBE_TEST_MYSQL_PORT') ?: '3306') .
        ';dbname=' . test_db() . ';charset=utf8mb4',
        getenv('CBE_TEST_MYSQL_USER') ?: 'root',
        getenv('CBE_TEST_MYSQL_PASSWORD') ?: '',
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
         PDO::ATTR_EMULATE_PREPARES => false]);
}
function entry(string $name, string $value): string {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}
function u8(string $name, int $value): string {
    return entry($name, "\0\x01" . chr($value));
}
function u32(string $name, int $value): string {
    return entry($name, "\0\x04" . pack('N', $value));
}
function text_field(string $name, string $value): string {
    return entry($name, pack('n', strlen($value)) . $value);
}
function request_object(int $kind, int $subtype, string $fields = ''): string {
    return chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
}
function wt_objects(array $objects): string {
    $body = implode('', $objects);
    return 'WT' . pack('n', 4 + strlen($body)) . $body;
}
function wt(int $kind, int $subtype, string $fields = ''): string {
    return wt_objects([request_object($kind, $subtype, $fields)]);
}
function read_exact($socket, int $length): string {
    $data = '';
    while (strlen($data) < $length) {
        $chunk = fread($socket, $length - strlen($data));
        if ($chunk === false) throw new RuntimeException('socket read failed');
        if ($chunk === '') {
            $meta = stream_get_meta_data($socket);
            if (($meta['timed_out'] ?? false) || ($meta['eof'] ?? false)) {
                throw new RuntimeException("short socket read: expected $length, received " . strlen($data));
            }
            usleep(1000);
            continue;
        }
        $data .= $chunk;
    }
    return $data;
}
function call_service(int $port, int $client, string $packet = '', int $flags = 0): string {
    $body = pack('V', $client) . $packet;
    $frame = 'CBMS' . pack('V4', 1, $flags, strlen($body), 4) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    if (!$socket) throw new RuntimeException("service connect failed: $errno $error");
    stream_set_timeout($socket, 10);
    for ($offset = 0; $offset < strlen($frame);) {
        $written = fwrite($socket, substr($frame, $offset));
        if ($written === false || $written === 0) throw new RuntimeException('short socket write');
        $offset += $written;
    }
    $header = read_exact($socket, 20);
    if (substr($header, 0, 4) !== 'CBMR') throw new RuntimeException('invalid CBMR header');
    $length = unpack('Vlength', substr($header, 12, 4))['length'];
    $reply = read_exact($socket, $length);
    fclose($socket);
    return $reply;
}
function response_objects(string $reply): array {
    if (strlen($reply) < 5 || substr($reply, 0, 2) !== 'WT') {
        throw new RuntimeException('non-WT response: ' . bin2hex($reply));
    }
    $objects = [];
    $offset = 5;
    for ($i = 0, $count = ord($reply[4]); $i < $count; ++$i) {
        if ($offset + 6 > strlen($reply)) throw new RuntimeException('truncated WT object');
        $length = unpack('nlength', substr($reply, $offset + 4, 2))['length'];
        if ($length < 6 || $offset + $length > strlen($reply)) {
            throw new RuntimeException('invalid WT object length');
        }
        $objects[] = [
            'major' => ord($reply[$offset]),
            'kind' => ord($reply[$offset + 1]),
            'subtype' => ord($reply[$offset + 2]),
            'payload' => substr($reply, $offset + 6, $length - 6),
        ];
        $offset += $length;
    }
    if ($offset !== strlen($reply)) throw new RuntimeException('WT trailing bytes');
    return $objects;
}
function object_field(array $object, string $wanted): ?string {
    $payload = $object['payload'];
    for ($offset = 0; $offset < strlen($payload);) {
        $nameLength = ord($payload[$offset++]);
        if ($offset + $nameLength + 2 > strlen($payload)) return null;
        $name = substr($payload, $offset, $nameLength);
        $offset += $nameLength;
        $valueLength = unpack('nlength', substr($payload, $offset, 2))['length'];
        $offset += 2;
        if ($offset + $valueLength > strlen($payload)) return null;
        $value = substr($payload, $offset, $valueLength);
        $offset += $valueLength;
        if ($name === $wanted) return $value;
    }
    return null;
}
function find_object(string $reply, int $kind, int $subtype): ?array {
    foreach (response_objects($reply) as $object) {
        if ($object['major'] === 1 && $object['kind'] === $kind &&
            $object['subtype'] === $subtype) return $object;
    }
    return null;
}
function tagged_u8(?string $value): ?int {
    return $value !== null && strlen($value) === 3 && substr($value, 0, 2) === "\0\x01"
        ? ord($value[2]) : null;
}
function tagged_u32(?string $value): ?int {
    return $value !== null && strlen($value) === 6 && substr($value, 0, 2) === "\0\x04"
        ? unpack('Nvalue', substr($value, 2, 4))['value'] : null;
}
function scene_poll_until(int $port, int $client, int $kind, int $subtype,
                          int $limit = 12): string {
    for ($attempt = 0; $attempt < $limit; ++$attempt) {
        $reply = call_service($port, $client, '', 2);
        if ($reply !== '' && find_object($reply, $kind, $subtype) !== null) return $reply;
        usleep(20000);
    }
    throw new RuntimeException("scene poll did not deliver $kind/$subtype in $limit attempts");
}
function login_scene(int $port, int $client, string $account, string $password,
                     int $role): void {
    call_service($port, $client, wt(1, 12,
        text_field('coreVer', '1') . text_field('appVer', '1') .
        text_field('imsi', 'duel-round-barrier-v1') .
        text_field('username', $account) . text_field('password', $password)));
    call_service($port, $client, wt(1, 16));
    call_service($port, $client, wt(1, 4, u32('serverID', 1) . u32('moneytype', 0)));
    call_service($port, $client, wt(1, 6, u32('actorID', $role)));
    $followup = wt_objects([
        request_object(12, 1), request_object(6, 1), request_object(6, 13),
        request_object(6, 14), request_object(2, 10, u8('Type', 101)),
        request_object(25, 5),
    ]);
    /* Test setup uses the detector's complete six-object signature but omits
     * the unrelated optional field present in the observed 54-byte client
     * packet.  This request only establishes isolated scene presence; none of
     * the duel assertions treats it as business success evidence. */
    if (strlen($followup) !== 44) throw new RuntimeException('scene followup fixture is not 44 bytes');
    call_service($port, $client, $followup);
}
function decode_duel_peer_visual(string $battleInfo): array {
    if (strlen($battleInfo) < 42 || substr($battleInfo, 0, 3) !== "\0\x01\x01") {
        throw new RuntimeException('invalid duel battleinfo left row');
    }
    $offset = 3;
    for ($i = 0; $i < 5; ++$i) {
        if (substr($battleInfo, $offset, 2) !== "\0\x04") {
            throw new RuntimeException('invalid tagged u32 in duel peer row');
        }
        $offset += 6;
    }
    $nameLength = unpack('nlength', substr($battleInfo, $offset, 2))['length'];
    $offset += 2;
    if ($offset + $nameLength + 6 > strlen($battleInfo) ||
        substr($battleInfo, $offset + $nameLength, 2) !== "\0\x01" ||
        substr($battleInfo, $offset + $nameLength + 3, 2) !== "\0\x01") {
        throw new RuntimeException('truncated duel peer visual row');
    }
    $name = rtrim(substr($battleInfo, $offset, $nameLength), "\0");
    $offset += $nameLength;
    return ['name' => $name, 'sexGroup' => ord($battleInfo[$offset + 2]),
            'jobIndex' => ord($battleInfo[$offset + 5])];
}
function assert_empty_ack(string $reply, string $phase): void {
    if ($reply !== "WT\0\x05\0") {
        throw new RuntimeException("$phase was not a five-byte zero-object WT: " . bin2hex($reply));
    }
}
function assert_scene_default_ack(string $reply, string $phase): void {
    $object = find_object($reply, 25, 5);
    if ($object === null || tagged_u8(object_field($object, 'result')) !== 4) {
        throw new RuntimeException("$phase did not receive native 25/5 scene ack: " . bin2hex($reply));
    }
}
function is_duel_terminal_reply(string $reply): bool {
    $objects = response_objects($reply);
    return count($objects) === 3 && $objects[0]['major'] === 1 &&
        $objects[0]['kind'] === 4 && $objects[0]['subtype'] === 6 &&
        $objects[1]['major'] === 1 && $objects[1]['kind'] === 4 &&
        $objects[1]['subtype'] === 11 && $objects[2]['major'] === 1 &&
        $objects[2]['kind'] === 4 && $objects[2]['subtype'] === 9;
}
function assert_duel_terminal(string $reply, string $phase): array {
    $objects = response_objects($reply);
    if (!is_duel_terminal_reply($reply) || $objects[0]['major'] !== 1 ||
        $objects[0]['kind'] !== 4 || $objects[0]['subtype'] !== 6 ||
        tagged_u8(object_field($objects[1], 'result')) !== 1 ||
        tagged_u8(object_field($objects[1], 'type')) !== 0 ||
        tagged_u8(object_field($objects[2], 'result')) !== 1 ||
        find_object($reply, 4, 7) !== null) {
        throw new RuntimeException(
            "$phase was not one native 4/6+4/11+4/9 no-reward close packet: " . bin2hex($reply));
    }
    $actions = decode_round_actions($reply, $phase);
    if (count($actions) < 1 || count($actions) > 2) {
        throw new RuntimeException("$phase has an invalid terminal action count " . count($actions));
    }
    foreach ($actions as $i => $action) {
        if ($action['type'] === 3 || $action['type'] === 4) {
            throw new RuntimeException("$phase action $i entered the ordinary death-action family");
        }
    }
    return $actions;
}
function assert_duel_escape(string $reply, string $phase): void {
    $objects = response_objects($reply);
    if (count($objects) !== 1 || $objects[0]['major'] !== 1 ||
        $objects[0]['kind'] !== 4 || $objects[0]['subtype'] !== 4 ||
        tagged_u8(object_field($objects[0], 'result')) !== 1) {
        throw new RuntimeException(
            "$phase was not one native 4/4 result=1 object: " . bin2hex($reply));
    }
}
function seq_u8(string $data, int &$offset, string $phase): int {
    if ($offset + 3 > strlen($data) || substr($data, $offset, 2) !== "\0\x01") {
        throw new RuntimeException("$phase has an invalid tagged u8 at $offset");
    }
    $value = ord($data[$offset + 2]);
    $offset += 3;
    return $value;
}
function seq_u32(string $data, int &$offset, string $phase): int {
    if ($offset + 6 > strlen($data) || substr($data, $offset, 2) !== "\0\x04") {
        throw new RuntimeException("$phase has an invalid tagged u32 at $offset");
    }
    $value = unpack('Nvalue', substr($data, $offset + 2, 4))['value'];
    $offset += 6;
    return $value;
}
function decode_round_actions(string $reply, string $phase): array {
    $object = find_object($reply, 4, 6);
    $actionCount = $object === null ? null : tagged_u8(object_field($object, 'actionnum'));
    $actionInfo = $object === null ? null : object_field($object, 'actioninfo');
    if ($object === null || $actionCount === null ||
        $actionInfo === null) {
        throw new RuntimeException("$phase did not release one action-bearing 4/6: " . bin2hex($reply));
    }
    $actions = [];
    $offset = 0;
    for ($i = 0; $i < $actionCount; ++$i) {
        $type = seq_u8($actionInfo, $offset, $phase);
        $actor = seq_u8($actionInfo, $offset, $phase);
        $targets = [];
        if ($type !== 3 && $type !== 4) {
            $childCount = seq_u8($actionInfo, $offset, $phase);
            if ($childCount < 1 || $childCount > 6) {
                throw new RuntimeException("$phase has invalid child count $childCount");
            }
            for ($child = 0; $child < $childCount; ++$child) {
                $targets[] = seq_u8($actionInfo, $offset, $phase);
                seq_u8($actionInfo, $offset, $phase);
                seq_u32($actionInfo, $offset, $phase);
                seq_u32($actionInfo, $offset, $phase);
            }
            if ($type === 1 || $type === 2) {
                seq_u32($actionInfo, $offset, $phase);
                if ($offset + 2 > strlen($actionInfo)) {
                    throw new RuntimeException("$phase has a truncated action string");
                }
                $length = unpack('nlength', substr($actionInfo, $offset, 2))['length'];
                $offset += 2 + $length;
                for ($tail = 0; $tail < 3; ++$tail) seq_u8($actionInfo, $offset, $phase);
            }
        }
        $actions[] = ['type' => $type, 'actor' => $actor, 'targets' => $targets];
    }
    if ($offset !== strlen($actionInfo)) {
        throw new RuntimeException("$phase actioninfo has trailing bytes");
    }
    return $actions;
}
function assert_round(string $reply, string $phase): array {
    $actions = decode_round_actions($reply, $phase);
    if (count($actions) !== 2) {
        throw new RuntimeException("$phase did not contain exactly two combat actions");
    }
    foreach ($actions as $i => $action) {
        if ($action['type'] === 3 || $action['type'] === 4) {
            throw new RuntimeException("$phase action $i entered the ordinary death-action family");
        }
    }
    return $actions;
}
function assert_mirrored_actions(array $local, array $peer, string $phase): void {
    if (count($local) !== count($peer)) {
        throw new RuntimeException("$phase mirrored action counts differ");
    }
    foreach ($local as $i => $action) {
        $mirror = $peer[$i];
        if ($action['type'] !== $mirror['type'] ||
            $action['actor'] + $mirror['actor'] !== 1 ||
            count($action['targets']) !== count($mirror['targets'])) {
            throw new RuntimeException("$phase action $i is not mirrored");
        }
        foreach ($action['targets'] as $child => $target) {
            if ($target + $mirror['targets'][$child] !== 1) {
                throw new RuntimeException("$phase action $i target $child is not mirrored");
            }
        }
    }
}

$mode = $argv[1] ?? '';
$port = isset($argv[2]) ? (int)$argv[2] : 0;
$accounts = ['codex_duel_job1', 'codex_duel_job3'];
$password = 'duel-round-pass';
$roles = [881901, 881902];
$clients = [0x7A19D001, 0x7A19D002];
$scene = hex2bin('3031CCD2BBA8B5BA5F30312E736365'); /* 01桃花岛_01.sce */

if ($mode === 'setup') {
    $db = pdo();
    $db->beginTransaction();
    try {
        foreach ($accounts as $account) {
            foreach (['account_role_equipment_durability', 'account_role_equipment',
                      'account_role_skills', 'account_role_tasks', 'account_role_backpack',
                      'account_role_state'] as $table) {
                $db->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
            }
            $db->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
            $db->prepare('DELETE FROM accounts WHERE account_id=?')->execute([$account]);
        }
        foreach ($accounts as $i => $account) {
            $job = $i === 0 ? 1 : 3;
            $sex = $i === 0 ? 1 : 0;
            $name = $i === 0 ? 'DuelJobOne' : 'DuelJobThree';
            $db->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')
                ->execute([$account, $password]);
            $db->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,8,?,1)')
                ->execute([$account, $roles[$i]]);
            $db->prepare(
                'INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,' .
                'backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,' .
                'backpack_item_count,designation_id,next_backpack_seq) ' .
                'VALUES(?,?,0,?,?,?,20,10,0,10000,10000,10000,10000,0,0,?,220,440,0,0,1)')
                ->execute([$account, $roles[$i], $name, $job, $sex, $scene]);
        }
        $db->commit();
        echo "duel round barrier fixture seeded\n";
    } catch (Throwable $error) {
        $db->rollBack();
        throw $error;
    }
    exit(0);
}
if ($mode !== 'run' || $port < 1024) throw new RuntimeException('usage: setup | run <port>');

try {
    login_scene($port, $clients[0], $accounts[0], $password, $roles[0]);
    login_scene($port, $clients[1], $accounts[1], $password, $roles[1]);

    $invite = call_service($port, $clients[0], wt(4, 14, u32('id', $roles[1])));
    $inviteObject = find_object($invite, 4, 14);
    if ($inviteObject === null || tagged_u8(object_field($inviteObject, 'result')) !== 1) {
        throw new RuntimeException('spar invite was not accepted by service');
    }
    $notice = scene_poll_until($port, $clients[1], 4, 15);
    $noticeObject = find_object($notice, 4, 15);
    $sourceWire = tagged_u32(object_field($noticeObject, 'id'));
    if ($sourceWire === null || $sourceWire === 0) throw new RuntimeException('spar notice missing source id');
    $nameWire = object_field($noticeObject, 'name');
    if ($nameWire !== "\0\x0bDuelJobOne\0") {
        throw new RuntimeException('spar notice name is not one NUL-terminated source name: ' .
            bin2hex((string)$nameWire));
    }

    $accept = wt_objects([
        request_object(4, 16, u32('id', $sourceWire) . u8('result', 1)),
        request_object(4, 9, u32('id', $sourceWire)),
    ]);
    assert_empty_ack(call_service($port, $clients[1], $accept), 'spar accept');

    $startB = scene_poll_until($port, $clients[1], 4, 10);
    scene_poll_until($port, $clients[0], 4, 17);
    $startA = scene_poll_until($port, $clients[0], 4, 10);
    $visualA = decode_duel_peer_visual(object_field(find_object($startA, 4, 10), 'battleinfo'));
    $visualB = decode_duel_peer_visual(object_field(find_object($startB, 4, 10), 'battleinfo'));
    if ($visualA !== ['name' => 'DuelJobThree', 'sexGroup' => 1, 'jobIndex' => 2] ||
        $visualB !== ['name' => 'DuelJobOne', 'sexGroup' => 2, 'jobIndex' => 0]) {
        throw new RuntimeException('duel peer visual order is not name,sexGroup,jobIndex: ' .
            json_encode([$visualA, $visualB]));
    }

    $physical = wt(4, 2, u32('index', 1) . u32('Operate', 0));
    assert_empty_ack(call_service($port, $clients[0], $physical), 'round1 first submit');
    assert_empty_ack(call_service($port, $clients[0], $physical), 'round1 duplicate submit');
    $round1Local = assert_round(call_service($port, $clients[1], $physical), 'round1 second submit');
    $round1Peer = assert_round(scene_poll_until($port, $clients[0], 4, 6), 'round1 peer delivery');
    assert_mirrored_actions($round1Local, $round1Peer, 'round1');

    assert_empty_ack(call_service($port, $clients[1], $physical), 'round2 first submit');
    $round2Local = assert_round(call_service($port, $clients[0], $physical), 'round2 second submit');
    $round2Peer = assert_round(scene_poll_until($port, $clients[1], 4, 6), 'round2 peer delivery');
    assert_mirrored_actions($round2Local, $round2Peer, 'round2');

    foreach ($clients as $client) {
        $toggle = call_service($port, $client, wt(4, 11, u8('type', 1)));
        $toggleObject = find_object($toggle, 4, 11);
        if ($toggleObject === null ||
            tagged_u8(object_field($toggleObject, 'result')) !== 1 ||
            tagged_u8(object_field($toggleObject, 'type')) !== 1) {
            throw new RuntimeException('duel auto mode did not acknowledge 4/11 type=1');
        }
    }
    assert_empty_ack(call_service($port, $clients[0], wt(4, 12)), 'auto round first submit');
    $autoLocal = assert_round(call_service($port, $clients[1], wt(4, 12)), 'auto round second submit');
    $autoPeer = assert_round(scene_poll_until($port, $clients[0], 4, 6), 'auto round peer delivery');
    assert_mirrored_actions($autoLocal, $autoPeer, 'auto round');

    $terminalReplies = [null, null];
    $terminalActions = [null, null];
    for ($round = 4; $round <= 20; ++$round) {
        $first = call_service($port, $clients[0], wt(4, 12));
        if (is_duel_terminal_reply($first)) {
            $terminalActions[0] = assert_duel_terminal($first, "round$round first terminal");
            $terminalReplies[0] = $first;
            break;
        }
        assert_empty_ack($first, "round$round first auto submit");
        $second = call_service($port, $clients[1], wt(4, 12));
        if (is_duel_terminal_reply($second)) {
            $terminalActions[1] = assert_duel_terminal($second, "round$round second terminal");
            $terminalReplies[1] = $second;
            break;
        }
        $secondActions = assert_round($second, "round$round second auto submit");
        $peer = scene_poll_until($port, $clients[0], 4, 6);
        $peerActions = assert_round($peer, "round$round peer delivery");
        assert_mirrored_actions($secondActions, $peerActions, "round$round");
    }
    foreach ($clients as $i => $client) {
        if ($terminalReplies[$i] === null) {
            $terminalReplies[$i] = scene_poll_until($port, $client, 4, 6, 180);
            $terminalActions[$i] = assert_duel_terminal(
                $terminalReplies[$i], "client$i terminal delivery");
        }
    }
    assert_mirrored_actions($terminalActions[0], $terminalActions[1], 'terminal round');

    assert_empty_ack(call_service($port, $clients[0], $physical),
        'late 4/2 after both terminal deliveries');
    assert_empty_ack(call_service($port, $clients[1], wt(4, 12)),
        'late 4/12 after both terminal deliveries');

    /* This is only a protocol-lifecycle assertion: the real CBE sends 25/5
     * after its no-reward terminal action closes. The packet test does not
     * claim to prove animation or presentation completion. */
    assert_scene_default_ack(call_service($port, $clients[0], wt(25, 5)),
        'first native duel exit');
    assert_empty_ack(call_service($port, $clients[1], $physical),
        'peer late 4/2 before its native exit');
    assert_scene_default_ack(call_service($port, $clients[1], wt(25, 5)),
        'second native duel exit');

    $reinvite = call_service($port, $clients[0], wt(4, 14, u32('id', $roles[1])));
    $reinviteObject = find_object($reinvite, 4, 14);
    if ($reinviteObject === null ||
        tagged_u8(object_field($reinviteObject, 'result')) !== 1) {
        throw new RuntimeException('duel was not released after both native 25/5 exits');
    }

    $secondNotice = scene_poll_until($port, $clients[1], 4, 15);
    $secondNoticeObject = find_object($secondNotice, 4, 15);
    $secondSourceWire = tagged_u32(object_field($secondNoticeObject, 'id'));
    if ($secondSourceWire === null || $secondSourceWire === 0) {
        throw new RuntimeException('second spar notice missing source id');
    }
    $secondAccept = wt_objects([
        request_object(4, 16, u32('id', $secondSourceWire) . u8('result', 1)),
        request_object(4, 9, u32('id', $secondSourceWire)),
    ]);
    assert_empty_ack(call_service($port, $clients[1], $secondAccept), 'second spar accept');
    scene_poll_until($port, $clients[1], 4, 10);
    scene_poll_until($port, $clients[0], 4, 17);
    scene_poll_until($port, $clients[0], 4, 10);

    assert_duel_escape(call_service($port, $clients[0], wt(4, 4)),
        'active escape direct response');
    assert_duel_escape(scene_poll_until($port, $clients[1], 4, 4, 180),
        'active escape peer delivery');
    assert_scene_default_ack(call_service($port, $clients[0], wt(25, 5)),
        'active escape source exit');
    assert_scene_default_ack(call_service($port, $clients[1], wt(25, 5)),
        'active escape peer exit');

    foreach ($clients as $client) call_service($port, $client, '', 4);
    $rows = pdo()->query(
        'SELECT role_id,hp,hp_max,mp,mp_max FROM account_roles ' .
        'WHERE role_id IN (881901,881902) ORDER BY role_id'
    )->fetchAll(PDO::FETCH_ASSOC);
    if (count($rows) !== 2 ||
        (int)$rows[0]['hp'] === 0 || (int)$rows[0]['hp'] !== (int)$rows[0]['hp_max'] ||
        (int)$rows[0]['mp'] !== (int)$rows[0]['mp_max'] ||
        (int)$rows[1]['hp'] === 0 || (int)$rows[1]['hp'] !== (int)$rows[1]['hp_max'] ||
        (int)$rows[1]['mp'] !== (int)$rows[1]['mp_max']) {
        throw new RuntimeException('friendly duel polluted durable role HP/MP: ' . json_encode($rows));
    }
    echo "duel-round-barrier-v1 passed: barrier + native 4/6+4/11+4/9 no-reward close + isolated 4/4 active escape\n";
} finally {
    foreach ($clients as $client) {
        try { call_service($port, $client, '', 4); } catch (Throwable $ignored) {}
    }
}
