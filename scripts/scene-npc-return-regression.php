<?php
/*
 * Reproduces a fresh local scene shell returning to the same exact map key.
 * Run setup before starting an isolated service, then run this script against
 * that service and finally run cleanup.  The test uses its own account so it
 * cannot alter an interactive character's persisted scene or position.
 */

function entry($name, $value) { return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value; }
function f_u8($name, $value) { return entry($name, "\x00\x01" . chr($value)); }
function f_u32($name, $value) { return entry($name, "\x00\x04" . pack('N', $value)); }
function f_string($name, $value) { return entry($name, pack('n', strlen($value)) . $value); }
function object_record($kind, $subtype, $fields = '') {
    return chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
}
function wt_packet(...$objects) {
    $body = implode('', $objects);
    return 'WT' . pack('n', 4 + strlen($body)) . $body;
}
function wt($kind, $subtype, $fields = '') { return wt_packet(object_record($kind, $subtype, $fields)); }
function scene_change($scene) {
    return wt(2, 3, f_u8('maptype', 2) . f_string('mapID', $scene) . f_u32('exitID', 1));
}
function scene_post_enter_followup($scene) {
    return wt_packet(
        object_record(0x19, 5),
        object_record(2, 3, f_u8('maptype', 2) . f_string('mapID', $scene) . f_u32('exitID', 0)),
        object_record(0x1b, 11),
        object_record(7, 42)
    );
}
function scene_subset() {
    return wt_packet(
        object_record(6, 1), object_record(6, 13), object_record(6, 14),
        object_record(2, 10, f_u8('Type', 101)), object_record(0x19, 5)
    );
}
function read_exact($socket, $length) {
    $out = '';
    while (strlen($out) < $length && !feof($socket)) {
        $chunk = fread($socket, $length - strlen($out));
        if ($chunk === false || $chunk === '') break;
        $out .= $chunk;
    }
    return $out;
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
    if (strlen($header) !== 20 || substr($header, 0, 4) !== 'CBMR') {
        throw new RuntimeException('bad response header');
    }
    $length = unpack('V', substr($header, 12, 4))[1];
    $response = read_exact($socket, $length);
    fclose($socket);
    if (strlen($response) !== $length) throw new RuntimeException('short response body');
    return $response;
}
function npc_count($response) {
    if (substr($response, 0, 2) !== 'WT' || strlen($response) < 5) return null;
    $count = ord($response[4]);
    for ($offset = 5, $i = 0; $i < $count; ++$i) {
        if ($offset + 6 > strlen($response)) return null;
        $length = unpack('n', substr($response, $offset + 4, 2))[1];
        $end = $offset + $length;
        if ($length < 6 || $end > strlen($response)) return null;
        if (ord($response[$offset + 1]) === 0x1b && ord($response[$offset + 2]) === 11) {
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
                if ($name === 'npcnum' && strlen($value) === 3 && $value[0] === "\x00" && $value[1] === "\x01") {
                    return ord($value[2]);
                }
            }
            return null;
        }
        $offset = $end;
    }
    return null;
}
function require_catalog($response, $phase) {
    $count = npc_count($response);
    if ($count === null || $count === 0) {
        throw new RuntimeException("$phase missing non-empty 27/11; npcnum=" . var_export($count, true));
    }
    return $count;
}
function cleanup($pdo, $account) {
    foreach (['account_role_equipment_durability', 'account_role_equipment', 'account_role_skills', 'account_role_tasks', 'account_role_backpack', 'account_role_state', 'accounts'] as $table) {
        $pdo->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
    }
    $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
}

$mode = $argv[1] ?? 'run';
$port = isset($argv[2]) ? (int)$argv[2] : 19090;
$account = 'codex_scene_npc_return';
$password = 'scene-npc-return-pass';
$roleId = 59421;
$clientId = 0x7A245942;
$forgeValley = hex2bin('3030C5EEC0B3CFC9B5BA5F30322E736365'); /* 00蓬莱仙岛_02.sce */
$awayScene = hex2bin('633030C5EEC0B3CFC9B5BA5F30312E736365');   /* c00蓬莱仙岛_01.sce */
$downloadScene = hex2bin('3031CCD2BBA8B5BA5F30322E736365');      /* 01桃花岛_02.sce */
$downloadNpcId = 59422;
$pdo = new PDO('mysql:host=localhost;dbname=jh_online;charset=utf8mb4', 'root', getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456', [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);

if ($mode === 'setup') {
    cleanup($pdo, $account);
    $pdo->beginTransaction();
    try {
        $pdo->prepare('DELETE FROM server_dynamic_npcs WHERE scene=? AND actor_id=?')->execute([$downloadScene, $downloadNpcId]);
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')->execute([$account, $password]);
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,1)')->execute([$account, $roleId]);
        $pdo->prepare('INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) VALUES(?,?,0,?,1,1,20,5,1250,100,120,41,100,654,1000,?,338,125,0,0,1)')->execute([$account, $roleId, 'NpcReturn', $forgeValley]);
        $pdo->prepare('INSERT INTO server_dynamic_npcs(scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,enabled) VALUES(?,?,?,?,0,0,?,?,?,1)')->execute([$downloadScene, $downloadNpcId, 160, 342, 'n_man1.actor', 'TeleportNpc', '']);
        $pdo->commit();
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    echo "scene-npc return fixture prepared\n";
    exit(0);
}
if ($mode === 'cleanup') {
    $pdo->prepare('DELETE FROM server_dynamic_npcs WHERE scene=? AND actor_id=?')->execute([$downloadScene, $downloadNpcId]);
    cleanup($pdo, $account);
    echo "scene-npc return fixture removed\n";
    exit(0);
}

try {
    call_service($port, $clientId, wt(1, 12,
        f_string('coreVer', '1') . f_string('appVer', '1') .
        f_string('imsi', 'scene-npc-return-regression') .
        f_string('username', $account) . f_string('password', $password)));
    call_service($port, $clientId, wt(1, 6, f_u32('actorID', $roleId)));
    $startup = call_service($port, $clientId, scene_subset());
    $startupCount = require_catalog($startup, 'startup');

    call_service($port, $clientId, scene_change($awayScene));
    call_service($port, $clientId, scene_subset());
    $return = call_service($port, $clientId, scene_change($forgeValley));
    $returnCount = npc_count($return);
    if ($returnCount !== null && $returnCount !== 0) {
        throw new RuntimeException("return bootstrap consumed NPC catalog too early; npcnum=$returnCount");
    }
    $returnFollowup = call_service($port, $clientId, scene_post_enter_followup($forgeValley));
    $returnCount = require_catalog($returnFollowup, 'return-post-enter-followup');

    /* The resource-download map-stone lifecycle ends at WT6/1.  It is a
     * separate fresh scene-shell boundary from the ordinary A->B->A return
     * above; the temporary, exact-key NPC makes a missing 27/11 observable. */
    $confirm = call_service($port, $clientId,
        wt(16, 4, f_u32('curid', 4) . f_u32('objid', 2)));
    if (strpos($confirm, "\x01\x10\x04") === false) {
        throw new RuntimeException('missing teleport map confirmation');
    }
    call_service($port, $clientId, wt_packet(
        object_record(16, 2, f_u32('exitID', 41) . f_u32('type', 3)),
        object_record(16, 3, f_u32('exitID', 41) . f_u32('type', 3))
    ));
    usleep(150000);
    $enter = call_service($port, $clientId, '', 2);
    if (strpos($enter, "\x01\x1e\x01") === false) {
        throw new RuntimeException('missing deferred teleport 30/1');
    }
    $duringDownload = call_service($port, $clientId, scene_change($downloadScene));
    if (strpos($duringDownload, "\x01\x1e\x02") !== false) {
        throw new RuntimeException('teleport 2/3 closed with premature 30/2');
    }
    $afterDownload = call_service($port, $clientId, scene_subset());
    if (strpos($afterDownload, "\x01\x1e\x02") === false) {
        throw new RuntimeException('post-download WT6/1 missing 30/2 completion');
    }
    $downloadCount = require_catalog($afterDownload, 'post-download scene followup');
    call_service($port, $clientId, '', 4);
    echo "scene-npc return regression passed startup_npc=$startupCount return_npc=$returnCount download_npc=$downloadCount return_response=" . strlen($return) . " followup_response=" . strlen($returnFollowup) . " download_response=" . strlen($afterDownload) . PHP_EOL;
} finally {
    try { call_service($port, $clientId, '', 4); } catch (Throwable $ignored) {}
}
