<?php
/*
 * Isolated protocol/persistence regression for offline cultivation.  It sends
 * the native WT request shapes only; no desktop client state is modified.
 * It is launched exclusively by run-practise-automation.ps1.
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
        getenv('CBE_TEST_MYSQL_USER') ?: 'root', getenv('CBE_TEST_MYSQL_PASSWORD') ?: '',
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_EMULATE_PREPARES => false]);
}
function entry(string $name, string $value): string {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}
function u8(string $name, int $value): string { return entry($name, "\0\x01" . chr($value)); }
function u16(string $name, int $value): string { return entry($name, "\0\x02" . pack('n', $value)); }
function u32(string $name, int $value): string { return entry($name, "\0\x04" . pack('N', $value)); }
function text_field(string $name, string $value): string { return entry($name, pack('n', strlen($value)) . $value); }
function wt(int $kind, int $subtype, string $fields = ''): string {
    $object = chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
    return 'WT' . pack('n', 4 + strlen($object)) . $object;
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
function write_all($socket, string $data): void {
    $offset = 0;
    $length = strlen($data);
    while ($offset < $length) {
        $written = fwrite($socket, substr($data, $offset));
        if ($written === false || $written === 0) {
            throw new RuntimeException("short socket write: expected $length, sent $offset");
        }
        $offset += $written;
    }
}
function call_service(int $port, int $client, string $packet): string {
    $body = pack('V', $client) . $packet;
    $frame = 'CBMS' . pack('V4', 1, 0, strlen($body), 4) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    if (!$socket) throw new RuntimeException("service connect failed: $errno $error");
    stream_set_timeout($socket, 10);
    write_all($socket, $frame);
    $header = read_exact($socket, 20);
    if (strlen($header) !== 20 || substr($header, 0, 4) !== 'CBMR') {
        throw new RuntimeException('invalid CBMR header');
    }
    $length = unpack('Vlength', substr($header, 12, 4))['length'];
    $reply = read_exact($socket, $length);
    fclose($socket);
    if (strlen($reply) !== $length) throw new RuntimeException('short CBMR response');
    return $reply;
}
function field(string $reply, int $kind, int $subtype, string $wanted): string {
    if (substr($reply, 0, 2) !== 'WT' || strlen($reply) < 5) throw new RuntimeException('non-WT response');
    for ($at = 5, $i = 0, $count = ord($reply[4]); $i < $count; ++$i) {
        $len = unpack('nlen', substr($reply, $at + 4, 2))['len'];
        $end = $at + $len;
        if ($len < 6 || $end > strlen($reply)) throw new RuntimeException('invalid WT object');
        if (ord($reply[$at]) === 1 && ord($reply[$at + 1]) === $kind && ord($reply[$at + 2]) === $subtype) {
            for ($entry = $at + 6; $entry < $end;) {
                $nameLen = ord($reply[$entry++]);
                $name = substr($reply, $entry, $nameLen); $entry += $nameLen;
                $valueLen = unpack('nlen', substr($reply, $entry, 2))['len']; $entry += 2;
                $value = substr($reply, $entry, $valueLen); $entry += $valueLen;
                if ($name === $wanted) return $value;
            }
        }
        $at = $end;
    }
    throw new RuntimeException("missing $kind/$subtype.$wanted");
}
function field_u8(string $reply, int $kind, int $subtype, string $name): int {
    $raw = field($reply, $kind, $subtype, $name);
    if (strlen($raw) !== 3 || substr($raw, 0, 2) !== "\0\x01") throw new RuntimeException("invalid u8 $name");
    return ord($raw[2]);
}
function field_u32(string $reply, int $kind, int $subtype, string $name): int {
    $raw = field($reply, $kind, $subtype, $name);
    if (strlen($raw) !== 6 || substr($raw, 0, 2) !== "\0\x04") throw new RuntimeException("invalid u32 $name");
    return unpack('Nvalue', substr($raw, 2, 4))['value'];
}
function field_string(string $reply, int $kind, int $subtype, string $name): string {
    $raw = field($reply, $kind, $subtype, $name);
    if (strlen($raw) < 2) throw new RuntimeException("invalid string $name");
    $length = unpack('nlength', substr($raw, 0, 2))['length'];
    if (strlen($raw) !== 2 + $length) throw new RuntimeException("invalid string length $name");
    return substr($raw, 2);
}
function login_role(int $port, int $client, string $account, int $role,
                    string $password = 'practise-pass'): void {
    echo "practise login step=auth\n";
    call_service($port, $client, wt(1, 12,
        text_field('coreVer', '1') . text_field('appVer', '1') .
        text_field('imsi', 'practise-regression') .
        text_field('username', $account) . text_field('password', $password)));
    echo "practise login step=account-page\n";
    call_service($port, $client, wt(1, 16));
    echo "practise login step=server-select\n";
    call_service($port, $client, wt(1, 4, u32('serverID', 1) . u32('moneytype', 0)));
    echo "practise login step=role-select\n";
    call_service($port, $client, wt(1, 6, u32('actorID', $role)));
}
function practise_info(int $port, int $client): string { return call_service($port, $client, wt(7, 18)); }

$mode = $argv[1] ?? '';
$port = isset($argv[2]) ? (int)$argv[2] : 0;

/* `7/19` is a stateless help contract: the real client sends it only after
 * opening the cultivation UI, but its response parser has no role-state
 * dependency.  Keep this probe independent from the full role-schema fixture
 * so it can prove the packet contract without reading or writing jh_online. */
if ($mode === 'help') {
    if ($port < 1024) throw new RuntimeException('usage: help <port>');
    $client = 0x7A285719;
    /* The real help click occurs after title login/role selection.  Preserve
     * that transport ownership boundary instead of whitelisting 7/19 as an
     * unauthenticated request merely to simplify the regression. */
    login_role($port, $client, 'guest00001', 810001, 'automation-only');
    $nativeHelpRequest = wt(7, 19, u8('type', 0));
    if (strlen($nativeHelpRequest) !== 19) {
        throw new RuntimeException('1/7/19 fixture no longer matches the observed 19-byte help request');
    }
    $help = field_string(call_service($port, $client, $nativeHelpRequest), 7, 19, 'helpinfo');
    $helpTitle = iconv('UTF-8', 'GBK//IGNORE', '修炼帮助');
    if ($help === '' || $helpTitle === false ||
        strncmp($help, $helpTitle, strlen($helpTitle)) !== 0) {
        throw new RuntimeException('1/7/19 did not return the client-required GBK helpinfo field');
    }
    echo "practise help regression passed: 1/7/19 type=0 -> GBK helpinfo\n";
    exit(0);
}

$pdo = pdo();
$account = 'codex_practise';
$role = 870101;
$client = 0x7A285701;

if ($mode === 'setup') {
    /* A valid GBK scene key is part of the fixture itself; the cultivation
     * contract does not depend on another seeded player existing. */
    $scene = hex2bin('3031CCD2BBA8B5BA5F30312E736365'); /* 01桃花岛_01.sce */
    $pdo->beginTransaction();
    try {
        /* account_role_practise is deliberately created by the service on the
         * first real 7/18 request, so an older source schema need not already
         * contain it when this isolated fixture is prepared. */
        foreach (['account_role_backpack', 'account_role_state', 'account_roles', 'accounts'] as $table) {
            $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
        }
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, 'practise-pass']);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,7,?,1)')->execute([$account, $role]);
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,40,1,0,120,120,100,100,0,0,?,220,440,1,0,62000)')
            ->execute([$account, $role, 'PractiseFixture', $scene]);
        $pdo->prepare('INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level,durability,durability_max) VALUES(?,?,?,?,?,?,0,0,0)')
            ->execute([$account, $role, 0, 827, 62001, 3]);
        $pdo->commit();
        echo "practise fixture seeded\n";
    } catch (Throwable $error) { $pdo->rollBack(); throw $error; }
    exit(0);
}
if ($mode !== 'run' || $port < 1024) throw new RuntimeException('usage: setup | run <port>');

login_role($port, $client, $account, $role);
$initial = practise_info($port, $client);
if (field_u32($initial, 7, 18, 'todaypasthour') !== 0 ||
    field_u32($initial, 7, 18, 'todaypastmin') !== 0 ||
    field_u32($initial, 7, 18, 'getexp') !== 0 ||
    field_u32($initial, 7, 18, 'todaylasthour') !== 8 ||
    field_u32($initial, 7, 18, 'alllasthour') !== 0 ||
    field_u8($initial, 7, 18, 'isgold') !== 0) {
    throw new RuntimeException('new cultivation state did not render its authoritative zero/8-hour values');
}

$nativeHelpRequest = wt(7, 19, u8('type', 0));
if (strlen($nativeHelpRequest) !== 19) {
    throw new RuntimeException('1/7/19 fixture no longer matches the observed 19-byte help request');
}
$help = field_string(call_service($port, $client, $nativeHelpRequest), 7, 19, 'helpinfo');
$helpTitle = iconv('UTF-8', 'GBK//IGNORE', '修炼帮助');
if ($help === '' || $helpTitle === false ||
    strncmp($help, $helpTitle, strlen($helpTitle)) !== 0) {
    throw new RuntimeException('1/7/19 did not return the client-required GBK helpinfo field');
}

$nativeSettingRequest = wt(7, 21, u8('opengold', 1));
if (strlen($nativeSettingRequest) !== 23) {
    throw new RuntimeException('1/7/21 fixture no longer matches the observed 23-byte opengold request');
}
$setting = call_service($port, $client, $nativeSettingRequest);
if (field_u8($setting, 7, 21, 'result') !== 1) {
    throw new RuntimeException('1/7/21 did not return the client-required result success field');
}
$gold = practise_info($port, $client);
if (field_u8($gold, 7, 18, 'isgold') !== 1 || field_u32($gold, 7, 18, 'todaylasthour') !== 4) {
    throw new RuntimeException('gold setting did not persist into 1/7/18 state');
}

$pill = call_service($port, $client, wt(7, 16, u16('itemseq', 62001)));
if (field_u8($pill, 7, 16, 'result') !== 1 || field_u32($pill, 7, 16, 'maxnum') !== 3) {
    throw new RuntimeException('827 1/7/16 did not return the native quantity upper bound');
}
$count = (int)$pdo->query("SELECT item_count FROM account_role_backpack WHERE account_id='codex_practise' AND role_id=870101 AND item_seq=62001")->fetchColumn();
if ($count !== 3) throw new RuntimeException('827 1/7/16 mutated the backpack before user confirmation');

/* The CBE sends this separate native completion request after a successful
 * 7/16 reply (and its ordinary 2/10 actor refresh).  It is not a second
 * UI-only acknowledgement: 0x0102C032 puts the selected count in `usenum`,
 * and 0x0102C104 needs result/useinfo/pcimg to finish the original progress
 * state and emit its own event 100.  The observed exact 38-byte layout is
 * usenum:tagged-u32,itemseq:tagged-u16. */
$nativePillCompletionRequest = wt(7, 17,
    u32('usenum', 2) . u16('itemseq', 62001));
if (strlen($nativePillCompletionRequest) !== 38) {
    throw new RuntimeException('1/7/17 fixture no longer matches the observed 38-byte 827 completion request');
}
$completion = call_service($port, $client, $nativePillCompletionRequest);
if (field_u8($completion, 7, 17, 'result') !== 1 ||
    field_string($completion, 7, 17, 'useinfo') === '' ||
    field_u8($completion, 7, 17, 'pcimg') !== 1) {
    throw new RuntimeException('1/7/17 did not return the native quantity-2 827 completion contract');
}
$count = (int)$pdo->query("SELECT item_count FROM account_role_backpack WHERE account_id='codex_practise' AND role_id=870101 AND item_seq=62001")->fetchColumn();
$availableMinutes = (int)$pdo->query("SELECT available_minutes FROM account_role_practise WHERE account_id='codex_practise' AND role_id=870101")->fetchColumn();
if ($count !== 1 || $availableMinutes !== 120) {
    throw new RuntimeException('1/7/17 quantity-2 commit did not atomically debit two pills and credit 120 minutes');
}
/* Transport retries must replay the same completion only; they must not debit
 * two more items or grant another two hours. */
$completionRetry = call_service($port, $client, $nativePillCompletionRequest);
if (field_u8($completionRetry, 7, 17, 'result') !== 1 ||
    (int)$pdo->query("SELECT item_count FROM account_role_backpack WHERE account_id='codex_practise' AND role_id=870101 AND item_seq=62001")->fetchColumn() !== 1 ||
    (int)$pdo->query("SELECT available_minutes FROM account_role_practise WHERE account_id='codex_practise' AND role_id=870101")->fetchColumn() !== 120) {
    throw new RuntimeException('1/7/17 quantity-2 retry was not idempotent after the committed 827 debit');
}

/* This is an isolated-fixture clock setup, not a success assertion by itself:
 * the following 7/18 must actually perform the server's offline settlement. */
$pdo->exec("UPDATE account_role_practise SET offline_started_unix=UNIX_TIMESTAMP()-900 WHERE account_id='codex_practise' AND role_id=870101");
$settled = practise_info($port, $client);
if (field_u32($settled, 7, 18, 'todaypastmin') !== 15 ||
    field_u32($settled, 7, 18, 'getexp') !== 240 ||
    field_u32($settled, 7, 18, 'todaylasthour') !== 3 ||
    field_u32($settled, 7, 18, 'todaylastmin') !== 45 ||
    field_u32($settled, 7, 18, 'alllasthour') !== 1 ||
    field_u32($settled, 7, 18, 'alllastmin') !== 45) {
    throw new RuntimeException('offline settlement did not produce the 15-minute golden-practise contract');
}
$exp = (int)$pdo->query("SELECT exp FROM account_roles WHERE account_id='codex_practise' AND role_id=870101")->fetchColumn();
if ($exp !== 240) throw new RuntimeException("offline practise exp persisted as $exp, expected 240");

$normalSetting = call_service($port, $client, wt(7, 21, u8('opengold', 0)));
if (field_u8($normalSetting, 7, 21, 'result') !== 1) {
    throw new RuntimeException('normal-practise setting did not acknowledge success');
}
$normal = practise_info($port, $client);
if (field_u8($normal, 7, 18, 'isgold') !== 0 ||
    field_u32($normal, 7, 18, 'todaylasthour') !== 7 ||
    field_u32($normal, 7, 18, 'todaylastmin') !== 45) {
    throw new RuntimeException('normal-practise daily 8-hour ceiling did not replace the golden 4-hour ceiling');
}
$levelAtNormalIntervalStart = (int)$pdo->query("SELECT level FROM account_roles WHERE account_id='codex_practise' AND role_id=870101")->fetchColumn();
$normalMinuteExp = 8 * max(1, $levelAtNormalIntervalStart);
$pdo->exec("UPDATE account_role_practise SET offline_started_unix=UNIX_TIMESTAMP()-60 WHERE account_id='codex_practise' AND role_id=870101");
$normalSettled = practise_info($port, $client);
if (field_u32($normalSettled, 7, 18, 'todaypastmin') !== 16 ||
    field_u32($normalSettled, 7, 18, 'getexp') !== 240 + $normalMinuteExp ||
    field_u32($normalSettled, 7, 18, 'alllastmin') !== 44) {
    throw new RuntimeException('normal-practise one-minute rate or remaining-time calculation is incorrect');
}

/* A full 100-hour bank must reject a pill without deleting its exact instance. */
$pdo->exec("UPDATE account_role_practise SET available_minutes=6000 WHERE account_id='codex_practise' AND role_id=870101");
$fullBank = call_service($port, $client, wt(7, 16, u16('itemseq', 62001)));
if (field_u8($fullBank, 7, 16, 'result') !== 2 ||
    (int)$pdo->query("SELECT item_count FROM account_role_backpack WHERE account_id='codex_practise' AND role_id=870101 AND item_seq=62001")->fetchColumn() !== 1) {
    throw new RuntimeException('100-hour cultivation bank did not reject 827 while preserving its stack');
}
echo "practise regression passed: 7/18 -> 7/19 -> 7/21 -> 7/16 -> 7/17 -> offline settle (15m, 240 exp)\n";
