<?php
/*
 * Isolated regression for the NPC exclusive-stock administration contract.
 * It drives the admin HTTP surface and the normal CBMS/WT service endpoint,
 * verifying both persistence and the client-facing NPC dialog response in the
 * run-specific jh_online_autotest_* schema.  It does not control a desktop
 * client or write to the user's jh_online schema.
 *
 * Usage:
 *   php scripts/npc-stock-regression.php prepare <database>
 *   php scripts/npc-stock-regression.php verify <admin-port> <service-port> <database>
 */

declare(strict_types=1);

/* Match the reported production scene: it has service-side built-in NPCs and
 * therefore proves that a newly created merchant remains addressable beside
 * the scene's native catalog. */
const TEST_SCENE_UTF8 = 'c04临安府_01.sce';
const TEST_ACTOR_ID = 990001;
const TEST_CREATED_ACTOR_ID = 990002;
const TEST_EXTERNAL_ACTOR_ID = 990003;
const TEST_ADMIN_ACCOUNT = 'npc-stock-admin';
const TEST_ADMIN_PASSWORD = 'automation-admin';
const TEST_BULK_SELECTION_COUNT = 84;
const TEST_GAME_ACCOUNT = 'npc-stock-auto';
const TEST_GAME_PASSWORD = 'npc-stock-pass';
const TEST_GAME_ROLE_ID = 990004;
const TEST_GAME_CLIENT_ID = 0x7a2f9904;

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

function contains(string $haystack, string $needle): bool {
    return strpos($haystack, $needle) !== false;
}

function wt_entry(string $name, string $value): string {
    return chr(strlen($name)) . $name . pack('n', strlen($value)) . $value;
}
function wt_u8(string $name, int $value): string {
    return wt_entry($name, "\x00\x01" . chr($value));
}
function wt_u32(string $name, int $value): string {
    return wt_entry($name, "\x00\x04" . pack('N', $value));
}
function wt_string(string $name, string $value): string {
    return wt_entry($name, pack('n', strlen($value)) . $value);
}
function wt_packet(int $kind, int $subtype, string $fields = ''): string {
    $object = chr(1) . chr($kind) . chr($subtype) . pack('n', 5 + strlen($fields)) . $fields;
    return 'WT' . pack('n', 4 + strlen($object)) . $object;
}
function socket_read_exact($socket, int $length): string {
    $data = '';
    while (strlen($data) < $length && !feof($socket)) {
        $chunk = fread($socket, $length - strlen($data));
        if ($chunk === false || $chunk === '') break;
        $data .= $chunk;
    }
    return $data;
}
function service_call(int $port, int $clientId, string $packet): string {
    $metadata = pack('V', $clientId);
    $body = $metadata . $packet;
    $frame = 'CBMS' . pack('V4', 1, 0, strlen($body), strlen($metadata)) . $body;
    $socket = stream_socket_client("tcp://127.0.0.1:$port", $errno, $error, 3);
    expect($socket !== false, "service connection failed: $errno $error");
    stream_set_timeout($socket, 5);
    expect(fwrite($socket, $frame) === strlen($frame), 'service request write was short');
    $header = socket_read_exact($socket, 20);
    expect(strlen($header) === 20 && substr($header, 0, 4) === 'CBMR',
           'service response header was invalid');
    $length = unpack('Vlength', substr($header, 12, 4))['length'];
    $response = socket_read_exact($socket, $length);
    fclose($socket);
    expect(strlen($response) === $length, 'service response body was short');
    return $response;
}
function wt_response_field(string $response, int $kind, int $subtype, string $wanted): ?string {
    if (substr($response, 0, 2) !== 'WT' || strlen($response) < 5) return null;
    $offset = 5;
    for ($index = 0, $count = ord($response[4]); $index < $count; ++$index) {
        if ($offset + 6 > strlen($response)) return null;
        $length = unpack('nlength', substr($response, $offset + 4, 2))['length'];
        $end = $offset + $length;
        if ($length < 6 || $end > strlen($response)) return null;
        $objectKind = ord($response[$offset + 1]);
        $objectSubtype = ord($response[$offset + 2]);
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
            if ($objectKind === $kind && $objectSubtype === $subtype && $name === $wanted)
                return $value;
        }
        $offset = $end;
    }
    return null;
}
function dialog_read_u8(string $dialog, int &$offset): ?int {
    if ($offset + 3 > strlen($dialog) || substr($dialog, $offset, 2) !== "\x00\x01") return null;
    $value = ord($dialog[$offset + 2]);
    $offset += 3;
    return $value;
}
function dialog_read_u32(string $dialog, int &$offset): ?int {
    if ($offset + 6 > strlen($dialog) || substr($dialog, $offset, 2) !== "\x00\x04") return null;
    $value = unpack('Nvalue', substr($dialog, $offset + 2, 4))['value'];
    $offset += 6;
    return $value;
}
function dialog_read_string(string $dialog, int &$offset): ?string {
    if ($offset + 2 > strlen($dialog)) return null;
    $length = unpack('nlength', substr($dialog, $offset, 2))['length'];
    $offset += 2;
    if ($offset + $length > strlen($dialog)) return null;
    $value = substr($dialog, $offset, $length);
    $offset += $length;
    return $value;
}
function dialog_option_values(?string $dialog): array {
    if (!is_string($dialog)) return [];
    $offset = 0;
    if (dialog_read_u8($dialog, $offset) === null || dialog_read_string($dialog, $offset) === null) return [];
    $count = dialog_read_u8($dialog, $offset);
    if ($count === null) return [];
    $values = [];
    for ($i = 0; $i < $count; ++$i) {
        if (dialog_read_u8($dialog, $offset) === null || dialog_read_string($dialog, $offset) === null ||
            dialog_read_u8($dialog, $offset) === null || ($value = dialog_read_u32($dialog, $offset)) === null ||
            dialog_read_string($dialog, $offset) === null) return [];
        $values[] = $value;
    }
    return $values;
}

/** @return array{status:int,headers:list<string>,body:string} */
function http_request(int $port, string $method, string $path,
                      string $body = '', string $cookie = ''): array {
    $headers = [
        'Content-Type: application/x-www-form-urlencoded',
        'Content-Length: ' . strlen($body),
        'Connection: close',
    ];
    if ($cookie !== '') $headers[] = 'Cookie: ' . $cookie;
    $context = stream_context_create(['http' => [
        'method' => $method,
        'header' => implode("\r\n", $headers),
        'content' => $body,
        'ignore_errors' => true,
        'follow_location' => 0,
        'max_redirects' => 0,
        'timeout' => 10,
    ]]);
    $url = 'http://127.0.0.1:' . $port . $path;
    $response = file_get_contents($url, false, $context);
    $responseHeaders = $http_response_header ?? [];
    expect($response !== false && isset($responseHeaders[0]), 'admin HTTP request failed: ' . $path);
    expect(preg_match('/\s(\d{3})\s/', $responseHeaders[0], $matches) === 1,
           'admin response has no HTTP status: ' . $path);
    return ['status' => (int)$matches[1], 'headers' => $responseHeaders, 'body' => $response];
}

function response_header(array $response, string $name): string {
    foreach ($response['headers'] as $header) {
        if (stripos($header, $name . ':') === 0) {
            return trim(substr($header, strlen($name) + 1));
        }
    }
    return '';
}

function is_redirect(int $status): bool {
    return $status === 302 || $status === 303;
}

function assert_ok_redirect(array $response, string $stage): void {
    expect(is_redirect($response['status']), $stage . ' did not redirect with success');
    $location = response_header($response, 'Location');
    expect($location !== '' && contains(rawurldecode($location), 'status=ok'),
           $stage . ' was rejected: ' . rawurldecode($location));
}

function prepare(string $database): void {
    $pdo = pdo($database);
    $scene = iconv('UTF-8', 'GBK//IGNORE', TEST_SCENE_UTF8);
    $name = iconv('UTF-8', 'GBK//IGNORE', '库存自动化商人');
    expect($scene !== false && $name !== false && str_ends_with($scene, '.sce'),
           'GBK fixture conversion failed');
    $legacyScene = substr($scene, 0, -4);

    $pdo->beginTransaction();
    try {
        foreach (['server_npc_shop_inventory', 'server_dynamic_npc_tasks',
                  'server_dynamic_npc_instances', 'server_dynamic_npcs'] as $table) {
            foreach ([TEST_ACTOR_ID, TEST_CREATED_ACTOR_ID, TEST_EXTERNAL_ACTOR_ID] as $actorId) {
                $pdo->prepare('DELETE FROM ' . $table . ' WHERE scene IN (?,?) AND actor_id=?')
                    ->execute([$scene, $legacyScene, $actorId]);
            }
        }
        $pdo->prepare(
            'INSERT INTO server_dynamic_npcs('
            . 'scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,enabled) '
            . 'VALUES(?,?,?,?,?,?,?,?,?,1)'
        )->execute([$scene, TEST_ACTOR_ID, 160, 320, 1, 0,
                    'n_man1.actor', $name, '']);
        /* Regression for the production failure: the old bare scene key has
         * the same actor id but a non-merchant service.  Startup migration
         * must remove it before the inventory POST resolves service kind. */
        $pdo->prepare(
            'INSERT INTO server_dynamic_npcs('
            . 'scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,enabled) '
            . 'VALUES(?,?,?,?,?,?,?,?,?,1)'
        )->execute([$legacyScene, TEST_ACTOR_ID, 160, 320, 2, 0,
                    'n_man1.actor', $name, '']);
        $pdo->prepare(
            'INSERT INTO server_admin_users(account_id,password_value,failed_attempts,locked) '
            . 'VALUES(?,?,0,0) ON DUPLICATE KEY UPDATE '
            . 'password_value=VALUES(password_value),failed_attempts=0,locked=0'
        )->execute([TEST_ADMIN_ACCOUNT, TEST_ADMIN_PASSWORD]);
        /* This is the production regression: the global-mall switch is off,
         * while a later enabled NPC stock row must remain purchasable. */
        $pdo->prepare(
            'INSERT INTO server_shop_items(item_id,price,enabled,shop_section) '
            . 'VALUES(1001,1,0,0) ON DUPLICATE KEY UPDATE '
            . 'price=VALUES(price),enabled=VALUES(enabled),shop_section=VALUES(shop_section)'
        )->execute();
        foreach (['account_role_equipment_durability', 'account_role_equipment',
                  'account_role_skills', 'account_role_tasks',
                  'account_role_backpack', 'account_role_state'] as $table) {
            $pdo->prepare('DELETE FROM ' . $table . ' WHERE account_id=?')
                ->execute([TEST_GAME_ACCOUNT]);
        }
        $pdo->prepare('DELETE FROM account_roles WHERE account_id=?')
            ->execute([TEST_GAME_ACCOUNT]);
        $pdo->prepare('DELETE FROM accounts WHERE account_id=?')
            ->execute([TEST_GAME_ACCOUNT]);
        $pdo->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')
            ->execute([TEST_GAME_ACCOUNT, TEST_GAME_PASSWORD]);
        $pdo->prepare(
            'INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) '
            . 'VALUES(?,6,?,1)'
        )->execute([TEST_GAME_ACCOUNT, TEST_GAME_ROLE_ID]);
        $pdo->prepare(
            'INSERT INTO account_roles('
            . 'account_id,role_id,role_index,role_name,job,sex,backpack_capacity,'
            . 'level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,'
            . 'backpack_item_count,designation_id,next_backpack_seq) '
            . 'VALUES(?,?,0,?,1,1,20,5,1250,120,120,100,100,100000,0,?,188,320,0,0,1)'
        )->execute([TEST_GAME_ACCOUNT, TEST_GAME_ROLE_ID, 'NpcStockAuto', $scene]);
        $pdo->commit();
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }
    echo "npc-stock fixture prepared: dynamic weapon merchant\n";
}

function verify_npc_shop_response(int $servicePort, int $expectedItemId): void {
    expect($servicePort >= 1024 && $servicePort <= 65535, 'invalid service port');
    service_call($servicePort, TEST_GAME_CLIENT_ID,
        wt_packet(1, 12, wt_string('coreVer', '1') . wt_string('appVer', '1') .
                  wt_string('imsi', 'npc-stock-regression') .
                  wt_string('username', TEST_GAME_ACCOUNT) .
                  wt_string('password', TEST_GAME_PASSWORD)));
    service_call($servicePort, TEST_GAME_CLIENT_ID,
        wt_packet(1, 6, wt_u32('actorID', TEST_GAME_ROLE_ID)));
    $dialogResponse = service_call($servicePort, TEST_GAME_CLIENT_ID,
        wt_packet(26, 1, wt_u8('type', 1) . wt_u32('id', TEST_CREATED_ACTOR_ID)));
    $dialogOptions = dialog_option_values(wt_response_field($dialogResponse, 26, 1, 'dialog'));
    expect(in_array(0xe1000001, $dialogOptions, true),
           'new weapon merchant did not expose the parser-backed weapon option');
    $categoriesResponse = service_call($servicePort, TEST_GAME_CLIENT_ID,
        wt_packet(26, 1, wt_u8('type', 2) . wt_u32('id', 0xe1000001)));
    $categoryOptions = dialog_option_values(wt_response_field($categoriesResponse, 26, 1, 'dialog'));
    expect(in_array(0xe8000008, $categoryOptions, true),
           'enabled NPC stock was filtered out by the disabled global-mall product');
    $itemsResponse = service_call($servicePort, TEST_GAME_CLIENT_ID,
        wt_packet(26, 1, wt_u8('type', 2) . wt_u32('id', 0xe8000008)));
    $itemOptions = dialog_option_values(wt_response_field($itemsResponse, 26, 1, 'dialog'));
    expect(in_array(0xe9000000 | $expectedItemId, $itemOptions, true),
           'NPC category response omitted its enabled stock item');
}

function verify(int $adminPort, int $servicePort, string $database): void {
    expect($adminPort >= 1024 && $adminPort <= 65535, 'invalid admin port');
    $login = http_request($adminPort, 'POST', '/admin-418yz6/login',
                          http_build_query(['account' => TEST_ADMIN_ACCOUNT,
                                            'password' => TEST_ADMIN_PASSWORD]));
    expect(is_redirect($login['status']),
           'admin login was rejected status=' . $login['status'] .
           ' headers=' . implode(' | ', $login['headers']) .
           ' body=' . substr($login['body'], 0, 240));
    $setCookie = response_header($login, 'Set-Cookie');
    expect(preg_match('/(cbe_admin=[^;]+)/', $setCookie, $cookieMatch) === 1,
           'admin login did not issue a session cookie');
    $cookie = $cookieMatch[1];
    $contentPath = '/admin-418yz6/?tab=content&scene=' . rawurlencode(TEST_SCENE_UTF8);
    $content = http_request($adminPort, 'GET', $contentPath, '', $cookie);
    $artifactDir = getenv('CBE_AUTOMATION_ARTIFACT_DIR');
    if ($artifactDir !== false && $artifactDir !== '') {
        file_put_contents($artifactDir . DIRECTORY_SEPARATOR . 'content-before.html', $content['body']);
    }
    expect($content['status'] === 200, 'content page did not load');
    expect(contains($content['body'], 'data-npc-stock-manager'),
           'weapon merchant has no exclusive-stock manager');
    expect(contains($content['body'], 'data-npc-stock-select-category'),
           'stock manager is missing category select-all control');
    expect(contains($content['body'], 'data-npc-stock-remove'),
           'stock manager is missing bulk remove control');
    expect(contains($content['body'], 'data-npc-stock-current-quality'),
           'weapon merchant is missing the existing-stock quality filter');
    expect(contains($content['body'], 'id="npc-stock-quality"'),
           'stock multi-picker is missing the quality filter control');
    expect(contains($content['body'], 'inventory-form-tag add') &&
           contains($content['body'], 'inventory-form-tag remove') &&
           contains($content['body'], '全选当前筛选'),
           'stock manager does not render the grouped inventory toolbar layout');
    expect(contains($content['body'], 'npc-editor-grid') &&
           contains($content['body'], 'npc-editor-options') &&
           contains($content['body'], 'npc-editor-post-actions'),
           'dynamic Actor editor does not render the grouped form layout');
    expect(contains($content['body'], 'name="actor_id" value="' . TEST_ACTOR_ID . '"'),
           'content page did not select the isolated test merchant');
    expect(preg_match('/<select id="npc-stock-picker-options" hidden>(.*?)<\\/select>/s',
                      $content['body'], $picker) === 1,
           'stock multi-picker source was not rendered');
    expect(preg_match_all('/<option value="(\d+)" data-category="e[789]" data-quality="(\d+)" data-price="(\d+)">/',
                          $picker[1], $matches, PREG_SET_ORDER) >= TEST_BULK_SELECTION_COUNT,
           'catalog does not expose enough weapon items with equip.dsh quality metadata');
    $items = array_slice($matches, 0, TEST_BULK_SELECTION_COUNT);
    foreach ($items as $item) {
        expect((int)$item[3] > 0, 'catalog default price must be positive');
    }

    /* Regression for the real editor flow: the parent does not exist when the
     * page is opened.  It is created through save-npc and its first inventory
     * POST must resolve the new in-memory parent without requiring a restart. */
    $create = http_request($adminPort, 'POST', '/admin-418yz6/action',
        http_build_query([
            'action' => 'save-npc', 'scene' => TEST_SCENE_UTF8,
            'actor_id' => TEST_CREATED_ACTOR_ID, 'display_name' => '新增库存回归商人',
            'actor_resource' => 'n_man1.actor', 'x' => '188', 'y' => '320',
            'kind' => '1', 'script_name' => '', 'task_id' => '0',
            'task_repeatable' => '0',
        ]), $cookie);
    assert_ok_redirect($create, 'create weapon merchant');
    $afterCreate = http_request($adminPort, 'GET', $contentPath, '', $cookie);
    expect($afterCreate['status'] === 200, 'content page did not reload after NPC create');
    $artifactDir = getenv('CBE_AUTOMATION_ARTIFACT_DIR');
    if ($artifactDir !== false && $artifactDir !== '') {
        file_put_contents($artifactDir . DIRECTORY_SEPARATOR . 'content-after-create.html',
                          $afterCreate['body']);
    }
    expect(preg_match_all(
        '~<form[^>]*data-npc-stock-add-form[^>]*>(.*?)</form>~s',
        $afterCreate['body'], $stockForms) !== false,
        'stock add forms were not rendered after NPC create');
    $createdFormUsesActorId = false;
    foreach ($stockForms[1] as $stockForm) {
        if (preg_match('~name="actor_id" value="(\d+)"~', $stockForm, $actorMatch) === 1 &&
            (int)$actorMatch[1] === TEST_CREATED_ACTOR_ID) {
            $createdFormUsesActorId = true;
            break;
        }
    }
    expect($createdFormUsesActorId,
           'new merchant stock form uses service kind instead of its Actor ID');

    /* The cache was built when the page was loaded.  Insert a second exact
     * merchant through the isolated fixture connection to model a save made
     * by another server instance; inventory ownership must still come from
     * the durable exact parent, not from that old cache. */
    $fixturePdo = pdo($database);
    $fixtureScene = iconv('UTF-8', 'GBK//IGNORE', TEST_SCENE_UTF8);
    $fixtureName = iconv('UTF-8', 'GBK//IGNORE', '外部写入回归商人');
    expect($fixtureScene !== false && $fixtureName !== false,
           'GBK conversion failed for external-parent fixture');
    $fixturePdo->prepare(
        'INSERT INTO server_dynamic_npcs('
        . 'scene,actor_id,pos_x,pos_y,npc_kind,orientation,actor_resource,display_name,script_name,enabled) '
        . 'VALUES(?,?,?,?,?,?,?,?,?,1)'
    )->execute([$fixtureScene, TEST_EXTERNAL_ACTOR_ID, 196, 320, 1, 0,
                'n_man1.actor', $fixtureName, '']);
    $externalSave = http_request($adminPort, 'POST', '/admin-418yz6/action',
        http_build_query([
            'action' => 'save-npc-inventory-bulk', 'scene' => TEST_SCENE_UTF8,
            'actor_id' => TEST_EXTERNAL_ACTOR_ID, 'item_ids' => $items[0][1],
            'unit_price' => '', 'enabled' => '1',
        ]), $cookie);
    assert_ok_redirect($externalSave, 'stale-cache exact-parent bulk save');

    $ids = implode(',', array_map(static function (array $item): string {
        return $item[1];
    }, $items));
    $save = http_request($adminPort, 'POST', '/admin-418yz6/action',
        http_build_query([
            'action' => 'save-npc-inventory-bulk',
            'scene' => TEST_SCENE_UTF8,
            'actor_id' => TEST_CREATED_ACTOR_ID,
            'item_ids' => $ids,
            'unit_price' => '',
            'enabled' => '1',
        ]), $cookie);
    assert_ok_redirect($save, 'empty-price bulk save');

    $pdo = pdo($database);
    $scene = iconv('UTF-8', 'GBK//IGNORE', TEST_SCENE_UTF8);
    $legacyScene = substr($scene, 0, -4);
    $legacyCheck = $pdo->prepare(
        'SELECT COUNT(*) FROM server_dynamic_npcs WHERE scene=? AND actor_id=?'
    );
    $legacyCheck->execute([$legacyScene, TEST_ACTOR_ID]);
    expect((int)$legacyCheck->fetchColumn() === 0,
           'legacy bare-scene NPC row was not migrated away before inventory save');
    $canonicalCheck = $pdo->prepare(
        'SELECT npc_kind FROM server_dynamic_npcs WHERE scene=? AND actor_id=?'
    );
    $canonicalCheck->execute([$scene, TEST_ACTOR_ID]);
    expect((int)$canonicalCheck->fetchColumn() === 1,
           'scene-key migration removed or changed the canonical weapon-merchant parent');
    $createdCheck = $pdo->prepare(
        'SELECT npc_kind FROM server_dynamic_npcs WHERE scene=? AND actor_id=?'
    );
    $createdCheck->execute([$scene, TEST_CREATED_ACTOR_ID]);
    expect((int)$createdCheck->fetchColumn() === 1,
           'admin create did not persist the new weapon-merchant parent');
    $externalCheck = $pdo->prepare(
        'SELECT npc_kind FROM server_dynamic_npcs WHERE scene=? AND actor_id=?'
    );
    $externalCheck->execute([$scene, TEST_EXTERNAL_ACTOR_ID]);
    expect((int)$externalCheck->fetchColumn() === 1,
           'external exact weapon-merchant fixture was not persisted');
    $externalInventoryCheck = $pdo->prepare(
        'SELECT unit_price FROM server_npc_shop_inventory WHERE scene=? AND actor_id=? AND item_id=?'
    );
    $externalInventoryCheck->execute([$scene, TEST_EXTERNAL_ACTOR_ID, (int)$items[0][1]]);
    expect((int)$externalInventoryCheck->fetchColumn() === (int)$items[0][3],
           'stale-cache parent did not resolve its default-priced inventory item');
    $query = $pdo->prepare(
        'SELECT item_id,unit_price,enabled FROM server_npc_shop_inventory '
        . 'WHERE scene=? AND actor_id=? ORDER BY item_id'
    );
    $query->execute([$scene, TEST_CREATED_ACTOR_ID]);
    $rows = $query->fetchAll(PDO::FETCH_ASSOC);
    expect(count($rows) === TEST_BULK_SELECTION_COUNT,
           'bulk save did not persist every selected good');
    $expected = [];
    foreach ($items as $item) $expected[(int)$item[1]] = (int)$item[3];
    foreach ($rows as $row) {
        expect(isset($expected[(int)$row['item_id']]), 'unexpected inventory item was saved');
        expect((int)$row['unit_price'] === $expected[(int)$row['item_id']],
               'blank price did not resolve to the catalog default');
        expect((int)$row['enabled'] === 1, 'bulk save lost enabled state');
    }
    expect((int)$items[0][1] === 1001,
           'disabled-global-mall regression fixture must exercise item 1001');
    verify_npc_shop_response($servicePort, (int)$items[0][1]);

    $wrong = [];
    expect(preg_match('/<option value="(\d+)" data-category="i10" data-quality="0" data-price="\d+">/',
                      $picker[1], $wrong) === 1,
           'catalog does not expose a medicine for category-boundary regression');
    $reject = http_request($adminPort, 'POST', '/admin-418yz6/action',
        http_build_query([
            'action' => 'save-npc-inventory-bulk', 'scene' => TEST_SCENE_UTF8,
            'actor_id' => TEST_CREATED_ACTOR_ID, 'item_ids' => $wrong[1],
            'unit_price' => '', 'enabled' => '1',
        ]), $cookie);
    expect(is_redirect($reject['status']) &&
           contains(rawurldecode(response_header($reject, 'Location')), 'status=error'),
           'server accepted an item outside the weapon merchant category');

    $updated = http_request($adminPort, 'GET', $contentPath, '', $cookie);
    expect($updated['status'] === 200 &&
           contains($updated['body'], 'NPC 专属库存（' . TEST_BULK_SELECTION_COUNT . '）'),
           'saved stock list was not rendered back to the administrator');

    $remove = http_request($adminPort, 'POST', '/admin-418yz6/action',
        http_build_query([
            'action' => 'delete-npc-inventory-bulk', 'scene' => TEST_SCENE_UTF8,
            'actor_id' => TEST_CREATED_ACTOR_ID, 'item_ids' => $ids,
        ]), $cookie);
    assert_ok_redirect($remove, 'bulk remove');
    $query->execute([$scene, TEST_CREATED_ACTOR_ID]);
    expect(count($query->fetchAll(PDO::FETCH_ASSOC)) === 0,
           'bulk remove did not remove every selected good');

    $javascript = http_request($adminPort, 'GET', '/admin-418yz6/admin.js', '', $cookie);
    expect($javascript['status'] === 200 && contains($javascript['body'], 'setupNpcStock'),
           'stock-picker behavior script was not served');
    expect(contains($javascript['body'], 'rebuildQualities') &&
           contains($javascript['body'], 'choice.dataset.quality') &&
           contains($javascript['body'], 'quality.addEventListener'),
           'served stock-picker script does not apply the equipment quality filter');
    $artifactDir = getenv('CBE_AUTOMATION_ARTIFACT_DIR');
    if ($artifactDir !== false && $artifactDir !== '') {
        file_put_contents($artifactDir . DIRECTORY_SEPARATOR . 'admin.js', $javascript['body']);
    }
    echo "npc-stock regression passed: 84-item select, legacy-scene migration, category/quality metadata, default price, disabled-mall NPC visibility, atomic remove\n";
}

$mode = $argv[1] ?? '';
if ($mode === 'prepare') {
    $database = $argv[2] ?? '';
    require_test_database($database);
    prepare($database);
    exit(0);
}
if ($mode === 'verify') {
    $adminPort = isset($argv[2]) ? (int)$argv[2] : 0;
    $servicePort = isset($argv[3]) ? (int)$argv[3] : 0;
    $database = $argv[4] ?? '';
    require_test_database($database);
    verify($adminPort, $servicePort, $database);
    exit(0);
}
throw new InvalidArgumentException('usage: prepare <database> or verify <admin-port> <service-port> <database>');
