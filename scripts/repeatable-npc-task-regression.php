<?php
/*
 * Verifies the server-side repeatable NPC task contract on an isolated service.
 *
 *   php scripts/repeatable-npc-task-regression.php setup
 *   bin\jh-online-server.exe --mock-service-port=19095 --mock-admin-port=19096
 *   php scripts/repeatable-npc-task-regression.php run 19095
 *   php scripts/repeatable-npc-task-regression.php cleanup
 *
 * The fixture starts completed (state 3). A forged 6/11 must be rejected;
 * only a 26/1 dialog from the repeatable binding may reopen it as state 1.
 * It also validates the native 25/5 + 6/4 submit response's taskdes string
 * encoding, because case 4 reads that field through the WT string accessor.
 */

function entry($name, $value) { return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value; }
function f_u8($name, $value) { return entry($name, "\x00\x01" . chr($value)); }
function f_u32($name, $value) { return entry($name, "\x00\x04" . pack('N', $value)); }
function f_string($name, $value) { return entry($name, pack('n', strlen($value)) . $value); }
function object_record($major, $kind, $subtype, $fields = '') {
    return chr($major) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
}
function wt_packet(...$objects) {
    $body = implode('', $objects);
    return 'WT' . pack('n', 4 + strlen($body)) . $body;
}
function wt($major, $kind, $subtype, $fields = '') { return wt_packet(object_record($major, $kind, $subtype, $fields)); }
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
    if (strlen($response) !== $length) throw new RuntimeException('short response body');
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
function tagged_u8($value) {
    return is_string($value) && strlen($value) === 3 && $value[0] === "\x00" && $value[1] === "\x01"
        ? ord($value[2]) : null;
}
function dialog_read_tagged_u8($dialog, &$offset) {
    if ($offset + 3 > strlen($dialog) || $dialog[$offset] !== "\x00" ||
        $dialog[$offset + 1] !== "\x01") return null;
    $value = ord($dialog[$offset + 2]);
    $offset += 3;
    return $value;
}
function dialog_skip_string($dialog, &$offset) {
    if ($offset + 2 > strlen($dialog)) return false;
    $length = unpack('n', substr($dialog, $offset, 2))[1];
    $offset += 2 + $length;
    return $offset <= strlen($dialog);
}
function dialog_offers_task($dialog, $taskId) {
    if (!is_string($dialog) || strlen($dialog) < 8) return false;
    $offset = 0;
    if (dialog_read_tagged_u8($dialog, $offset) === null ||
        !dialog_skip_string($dialog, $offset)) return false;
    $count = dialog_read_tagged_u8($dialog, $offset);
    if ($count === null) return false;
    for ($i = 0; $i < $count; ++$i) {
        if (dialog_read_tagged_u8($dialog, $offset) === null ||
            !dialog_skip_string($dialog, $offset) || $offset + 9 > strlen($dialog)) return false;
        $action = dialog_read_tagged_u8($dialog, $offset);
        if ($action === null || $dialog[$offset] !== "\x00" || $dialog[$offset + 1] !== "\x04") return false;
        $value = unpack('N', substr($dialog, $offset + 2, 4))[1];
        $offset += 6;
        if (!dialog_skip_string($dialog, $offset)) return false;
        if ($action === 4 && $value === $taskId) return true;
    }
    return false;
}
function task_state($pdo, $account, $roleId, $taskId) {
    $statement = $pdo->prepare('SELECT task_state,progress1,progress2 FROM account_role_tasks WHERE account_id=? AND role_id=? AND task_id=?');
    $statement->execute([$account, $roleId, $taskId]);
    return $statement->fetch(PDO::FETCH_ASSOC) ?: null;
}
function cleanup($pdo, $account, $roleId, $scene, $actorId, $nonRepeatableActorId, $taskId) {
    $pdo->prepare('DELETE FROM server_dynamic_npcs WHERE scene=? AND actor_id IN (?,?)')->execute([$scene, $actorId, $nonRepeatableActorId]);
    $pdo->prepare('DELETE FROM server_tasks WHERE task_id=?')->execute([$taskId]);
    foreach (['account_role_equipment_durability','account_role_equipment','account_role_skills','account_role_tasks','account_role_backpack','account_role_state','accounts'] as $table) {
        $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
    }
    $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
}

$mode = $argv[1] ?? 'run';
$port = isset($argv[2]) ? (int)$argv[2] : 19095;
$account = 'codex_repeatable_npc_task';
$password = 'repeatable-npc-task-pass';
$roleId = 59381;
$actorId = 59382;
$nonRepeatableActorId = 59383;
$taskId = 3999900001;
$clientId = 0x7A245981;
$scene = hex2bin('3030C5EEC0B3CFC9B5BA5F30322E736365'); /* 00蓬莱仙岛_02.sce */
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root', getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);

if ($mode === 'setup') {
    cleanup($pdo, $account, $roleId, $scene, $actorId, $nonRepeatableActorId, $taskId);
    $pdo->beginTransaction();
    try {
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, $password]);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)')->execute([$account, $roleId]);
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,5,1250,100,120,41,100,654,1000,?,338,125,0,0,1)')->execute([$account, $roleId, 'RepeatableTask', $scene]);
        $pdo->prepare('INSERT INTO server_tasks(task_id,enabled,level,difficulty,classification,requirement_type1,requirement_count1,requirement_id1,requirement_type2,requirement_count2,requirement_id2,prerequisite_task_id,given_item_id,given_item_count,reward_exp,reward_money,reward_item_id,reward_item_count,reward_item_type,name,giver,receiver,goal,reward_text,offer_dialog,active_dialog,completed_dialog) VALUES(?,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,?,?,?,?,?,?,?,?)')->execute([$taskId, 'RepeatableTask', 'RepeatableNpc', 'RepeatableNpc', 'Talk again', '', 'Accept again', '', 'Completed']);
        $pdo->prepare('INSERT INTO server_dynamic_npcs(scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,enabled) VALUES(?,?,?,?,0,0,?,?,?,1)')->execute([$scene, $actorId, 336, 124, 'n_man1.actor', 'RepeatableNpc', '']);
        $pdo->prepare('INSERT INTO server_dynamic_npc_tasks(scene,actor_id,task_id,repeatable) VALUES(?,?,?,1)')->execute([$scene, $actorId, $taskId]);
        $pdo->prepare('INSERT INTO server_dynamic_npcs(scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,enabled) VALUES(?,?,?,?,0,0,?,?,?,1)')->execute([$scene, $nonRepeatableActorId, 368, 124, 'n_man1.actor', 'OneShotNpc', '']);
        $pdo->prepare('INSERT INTO server_dynamic_npc_tasks(scene,actor_id,task_id,repeatable) VALUES(?,?,?,0)')->execute([$scene, $nonRepeatableActorId, $taskId]);
        $pdo->prepare('INSERT INTO account_role_tasks(account_id,role_id,task_id,task_state,progress1,progress2) VALUES(?,?,?,3,0,0)')->execute([$account, $roleId, $taskId]);
        $pdo->commit();
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    echo "repeatable NPC task fixture prepared\n";
    exit(0);
}
if ($mode === 'cleanup') {
    cleanup($pdo, $account, $roleId, $scene, $actorId, $nonRepeatableActorId, $taskId);
    echo "repeatable NPC task fixture removed\n";
    exit(0);
}

try {
    call_service($port, $clientId, wt(1, 1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1') .
        f_string('imsi', 'repeatable-npc-task-regression') .
        f_string('username', $account) . f_string('password', $password)));
    call_service($port, $clientId, wt(1, 1, 6, f_u32('actorID', $roleId)));

    /* Completed state alone must not permit reacceptance. */
    $forged = call_service($port, $clientId,
        wt_packet(object_record(1, 6, 11, f_u32('taskinfo', $taskId)), object_record(1, 0x19, 5)));
    if (tagged_u8(response_field($forged, 6, 11, 'result')) !== 1 ||
        (task_state($pdo, $account, $roleId, $taskId)['task_state'] ?? null) != 3) {
        throw new RuntimeException('completed task was accepted without a repeatable NPC offer');
    }

    $oneShotDialog = call_service($port, $clientId,
        wt(1, 26, 1, f_u8('type', 1) . f_u32('id', $nonRepeatableActorId)));
    if (dialog_offers_task(response_field($oneShotDialog, 26, 1, 'dialog'), $taskId)) {
        throw new RuntimeException('non-repeatable NPC exposed a completed task for reacceptance');
    }

    $dialogResponse = call_service($port, $clientId,
        wt(1, 26, 1, f_u8('type', 1) . f_u32('id', $actorId)));
    $dialog = response_field($dialogResponse, 26, 1, 'dialog');
    if (!dialog_offers_task($dialog, $taskId)) {
        throw new RuntimeException('repeatable NPC dialog did not expose action=4 for the completed task; dialog_hex=' .
            (is_string($dialog) ? bin2hex($dialog) : 'missing'));
    }
    $detail = call_service($port, $clientId,
        wt(1, 6, 10, f_u32('taskid', $taskId) . f_u8('state', 3)));
    if (response_field($detail, 6, 10, 'info') === null) {
        throw new RuntimeException('task detail response missing after repeatable NPC offer');
    }
    $accepted = call_service($port, $clientId,
        wt_packet(object_record(1, 6, 11, f_u32('taskinfo', $taskId)), object_record(1, 0x19, 5)));
    $state = task_state($pdo, $account, $roleId, $taskId);
    if (tagged_u8(response_field($accepted, 6, 11, 'result')) !== 0 ||
        $state === null || (int)$state['task_state'] !== 1 ||
        (int)$state['progress1'] !== 0 || (int)$state['progress2'] !== 0) {
        throw new RuntimeException('repeatable NPC offer did not atomically reopen the task as state 1');
    }

    /* This fixture has no requirements, so state 2 is the persisted condition
     * immediately before the client sends its native 25/5 + 6/4 submit pair. */
    $pdo->prepare('UPDATE account_role_tasks SET task_state=2 WHERE account_id=? AND role_id=? AND task_id=?')
        ->execute([$account, $roleId, $taskId]);
    $submitted = call_service($port, $clientId, wt_packet(
        object_record(1, 0x19, 5),
        object_record(1, 6, 4, f_u32('taskid', $taskId))));
    $expectedTaskdes = "\xc8\xce\xce\xf1\xcc\xe1\xbd\xbb\xb3\xc9\xb9\xa6\xa3\xa1"; /* 任务提交成功！ */
    $taskdes = response_field($submitted, 6, 4, 'taskdes');
    $submitState = task_state($pdo, $account, $roleId, $taskId);
    if (tagged_u8(response_field($submitted, 6, 4, 'result')) !== 1 ||
        !is_string($taskdes) || strlen($taskdes) < 2 ||
        unpack('n', substr($taskdes, 0, 2))[1] !== strlen($expectedTaskdes) ||
        substr($taskdes, 2) !== $expectedTaskdes ||
        $submitState === null || (int)$submitState['task_state'] !== 3) {
        throw new RuntimeException('task submit response did not carry a valid taskdes string blob');
    }
    call_service($port, $clientId, '', 4);
    echo "repeatable NPC task regression passed actor=$actorId task=$taskId state=3->1->3\n";
} finally {
    try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
}
