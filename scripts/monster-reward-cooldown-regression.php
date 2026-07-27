<?php

/*
 * Usage:
 *   php scripts/monster-reward-cooldown-regression.php setup
 *   start a fresh service with CBE_BATTLE_ENEMY_COUNT=1 and CBE_BATTLE_ENEMY_HP=1,
 *     CBE_BATTLE_REWARD_EXP=11, CBE_BATTLE_REWARD_GOLD=13,
 *     CBE_BATTLE_DROP_ITEM_ID=304 and CBE_BATTLE_DROP_RATE=100
 *   php scripts/monster-reward-cooldown-regression.php run 19154
 *   php scripts/monster-reward-cooldown-regression.php cleanup
 *
 * The second victory deliberately reconnects through another client id.  This
 * proves the rule is a durable account/role claim, not a per-socket or
 * per-battle-session flag.  Moving the durable timestamp back is a deterministic
 * substitute for an eight-second sleep in CI.
 */
function entry(string $name, string $value): string {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}
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

$mode = $argv[1] ?? 'run';
$port = isset($argv[2]) ? (int)$argv[2] : 19154;
$account = 'codex_monster_reward_cooldown';
$password = 'monster-reward-cooldown-pass';
$roleId = 59154;
$clientA = 0x7A225154;
$clientB = 0x7A225155;
$scene = hex2bin('633030C5EEC0B3CFC9B5BA5F30312E736365'); /* c00蓬莱仙岛_01.sce */
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root',
    getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);

function delete_fixture(PDO $pdo, string $account): void {
    foreach (['account_role_equipment_durability', 'account_role_equipment', 'account_role_skills',
              'account_role_tasks', 'account_role_backpack', 'account_role_state'] as $table) {
        $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
    }
    /* account_role_monster_reward_cooldowns is a foreign-key child and is
     * removed by this role delete, including after a previous failed run. */
    $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
    $pdo->prepare('DELETE FROM accounts WHERE account_id=?')->execute([$account]);
}
function login_and_select(int $port, int $clientId, string $account, string $password, int $roleId): void {
    call_service($port, $clientId, wt(1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1') .
        f_string('imsi', 'monster-reward-cooldown-regression') .
        f_string('username', $account) . f_string('password', $password)));
    call_service($port, $clientId, wt(1, 6, f_u32('actorID', $roleId)));
}
function win_monster_battle(int $port, int $clientId): void {
    $start = call_service($port, $clientId,
        wt(4, 1, f_u32('id', 1) . f_u32('index', 0) . f_u32('posx', 0) . f_u32('posy', 0)));
    if (object_field($start, 4, 5, 'battleinfo') === null)
        throw new RuntimeException('battle start missing 4/5');
    $settle = call_service($port, $clientId,
        wt(4, 2, f_u32('index', 0) . f_u32('Operate', 0)));
    if (object_field($settle, 4, 7, 'exp') === null)
        throw new RuntimeException('battle victory missing parser-required 4/7: ' . bin2hex($settle));
}
function assert_state(PDO $pdo, string $account, int $roleId, int $wantExp, int $wantMoney, int $wantDrops): void {
    $role = $pdo->prepare('SELECT exp,money FROM account_roles WHERE account_id=? AND role_id=?');
    $role->execute([$account, $roleId]);
    $row = $role->fetch(PDO::FETCH_ASSOC);
    if (!$row || (int)$row['exp'] !== $wantExp || (int)$row['money'] !== $wantMoney)
        throw new RuntimeException("unexpected role state exp=" . ($row['exp'] ?? 'missing') .
            " money=" . ($row['money'] ?? 'missing') . " expected=$wantExp/$wantMoney");
    $items = $pdo->prepare('SELECT COALESCE(SUM(item_count),0) FROM account_role_backpack WHERE account_id=? AND role_id=? AND item_id=304');
    $items->execute([$account, $roleId]);
    $actualDrops = (int)$items->fetchColumn();
    if ($actualDrops !== $wantDrops)
        throw new RuntimeException("unexpected 304 drop count=$actualDrops expected=$wantDrops");
}

if ($mode === 'setup') {
    $pdo->beginTransaction();
    try {
        delete_fixture($pdo, $account);
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, $password]);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)')->execute([$account, $roleId]);
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,2,100,120,120,100,100,200,0,?,220,440,0,0,1)')
            ->execute([$account, $roleId, 'MonsterRewardCooldown', $scene]);
        $pdo->commit();
        echo "monster reward cooldown fixture prepared\n";
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    exit(0);
}
if ($mode === 'cleanup') {
    delete_fixture($pdo, $account);
    echo "monster reward cooldown fixture removed\n";
    exit(0);
}

try {
    login_and_select($port, $clientA, $account, $password, $roleId);
    win_monster_battle($port, $clientA);
    assert_state($pdo, $account, $roleId, 111, 213, 1);

    call_service($port, $clientA, '', 4);
    login_and_select($port, $clientB, $account, $password, $roleId);
    win_monster_battle($port, $clientB);
    assert_state($pdo, $account, $roleId, 111, 213, 1);

    $advance = $pdo->prepare('UPDATE account_role_monster_reward_cooldowns SET last_reward_ms=last_reward_ms-9000 WHERE account_id=? AND role_id=?');
    $advance->execute([$account, $roleId]);
    if ($advance->rowCount() !== 1) throw new RuntimeException('cooldown row missing after first victory');
    win_monster_battle($port, $clientB);
    assert_state($pdo, $account, $roleId, 122, 226, 2);

    echo "monster reward cooldown regression passed first=reward reconnect=blocked expiry=reward\n";
} finally {
    foreach ([$clientA, $clientB] as $clientId) {
        try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
    }
}
