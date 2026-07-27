<?php
/*
 * End-to-end regression for the public user-center backpack page.
 *
 * It creates an isolated account through the actual /user/register endpoint,
 * seeds two roles, deletes a training-book instance on the non-active role
 * through /user/backpack/delete, then verifies MySQL state and cleans up.
 *
 * Usage: CBE_TEST_MYSQL_PASSWORD=... php scripts/user-web-backpack-regression.php [admin-port]
 */

$port = isset($argv[1]) ? (int)$argv[1] : 19091;
$password = getenv('CBE_TEST_MYSQL_PASSWORD') ?: '123456';
$account = 'codex_webbag_' . random_int(100000, 999999);
$loginPassword = 'webbag-pass';
$pdo = new PDO(
    'mysql:host=127.0.0.1;dbname=jh_online;charset=utf8mb4', 'root', $password,
    [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
);

function http_request($port, $method, $path, $body = '', $cookie = '') {
    $headers = [
        'Host: 127.0.0.1:' . $port,
        'Connection: close',
    ];
    if ($cookie !== '') $headers[] = 'Cookie: ' . $cookie;
    if ($method === 'POST') {
        $headers[] = 'Content-Type: application/x-www-form-urlencoded';
        $headers[] = 'Content-Length: ' . strlen($body);
    }
    $context = stream_context_create(['http' => [
        'method' => $method,
        'header' => implode("\r\n", $headers) . "\r\n",
        'content' => $body,
        'ignore_errors' => true,
        'follow_location' => 0,
        'timeout' => 8,
    ]]);
    $response = @file_get_contents('http://127.0.0.1:' . $port . $path, false, $context);
    $responseHeaders = $http_response_header ?? [];
    if (!$responseHeaders) throw new RuntimeException('HTTP response missing for ' . $path);
    return [$responseHeaders, $response === false ? '' : $response];
}

function status_code($headers) {
    if (!preg_match('/\\s(\\d{3})\\s/', $headers[0] ?? '', $matches)) return 0;
    return (int)$matches[1];
}

function is_redirect($headers) {
    $status = status_code($headers);
    return $status >= 300 && $status < 400;
}

function header_value($headers, $name) {
    foreach ($headers as $header) {
        if (stripos($header, $name . ':') === 0) return trim(substr($header, strlen($name) + 1));
    }
    return '';
}

function cleanup($pdo, $account) {
    $pdo->beginTransaction();
    try {
        $pdo->prepare('DELETE FROM account_role_state WHERE account_id=?')->execute([$account]);
        $pdo->prepare('DELETE FROM accounts WHERE account_id=?')->execute([$account]);
        $pdo->commit();
    } catch (Throwable $error) {
        if ($pdo->inTransaction()) $pdo->rollBack();
        throw $error;
    }
}

try {
    /* Account registration must update the running service's account cache;
     * do not seed accounts directly before posting the page form. */
    [$headers] = http_request($port, 'POST', '/user/register', http_build_query([
        'account' => $account,
        'password' => $loginPassword,
    ]));
    if (!is_redirect($headers)) throw new RuntimeException('registration did not redirect');
    $cookie = header_value($headers, 'Set-Cookie');
    if (!preg_match('/^(cbe_user=[^;]+)/', $cookie, $matches)) {
        throw new RuntimeException('registration did not issue user cookie');
    }
    $cookie = $matches[1];

    $maxRole = (int)$pdo->query('SELECT COALESCE(MAX(role_id),900000) FROM account_roles')->fetchColumn();
    $activeRoleId = max($maxRole + 1, 900001);
    $targetRoleId = $activeRoleId + 1;
    $scene = $pdo->query('SELECT scene FROM account_roles ORDER BY role_id LIMIT 1')->fetchColumn();
    if ($scene === false) throw new RuntimeException('fixture requires one existing scene');

    $pdo->beginTransaction();
    try {
        $pdo->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,5,?,2)')
            ->execute([$account, $activeRoleId]);
        $roleInsert = $pdo->prepare(
            'INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) '
            . 'VALUES(?,?,?, ?,1,1,20,1,0,120,120,100,100,0,0,?,220,440,1,0,?)'
        );
        $roleInsert->execute([$account, $activeRoleId, 0, 'WebBagActive', $scene, 61002]);
        $roleInsert->execute([$account, $targetRoleId, 1, 'WebBagTarget', $scene, 61003]);
        $bagInsert = $pdo->prepare(
            'INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level) VALUES(?,?,?,?,?,?,?)'
        );
        $bagInsert->execute([$account, $activeRoleId, 0, 812, 61001, 4, 0]);
        $bagInsert->execute([$account, $targetRoleId, 0, 921, 61002, 1, 0]);
        $pdo->prepare(
            "INSERT INTO account_role_training_books(account_id,role_id,item_seq,title,book_description,book_info,book_level,book_experience) VALUES(?,?,?,X'7469746C65',X'64657363',X'696E666F',1,0)"
        )->execute([$account, $targetRoleId, 61002]);
        $pdo->commit();
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }

    [$headers, $dashboard] = http_request($port, 'GET', '/', '', $cookie);
    if (status_code($headers) !== 200 || strpos($dashboard, '背包管理') === false ||
        strpos($dashboard, 'name="item_seq" value="61002"') === false) {
        throw new RuntimeException('dashboard did not render selected backpack instance');
    }

    [$headers] = http_request($port, 'POST', '/user/backpack/delete', http_build_query([
        'role_id' => $targetRoleId,
        'item_id' => 921,
        'item_seq' => 61002,
    ]), $cookie);
    if (!is_redirect($headers) || strpos(header_value($headers, 'Location'), 'status=ok') === false) {
        throw new RuntimeException('backpack delete did not report success');
    }

    $exists = $pdo->prepare('SELECT COUNT(*) FROM account_role_backpack WHERE account_id=? AND role_id=? AND item_id=921 AND item_seq=61002');
    $exists->execute([$account, $targetRoleId]);
    if ((int)$exists->fetchColumn() !== 0) throw new RuntimeException('target backpack instance remained');
    $book = $pdo->prepare('SELECT COUNT(*) FROM account_role_training_books WHERE account_id=? AND role_id=? AND item_seq=61002');
    $book->execute([$account, $targetRoleId]);
    if ((int)$book->fetchColumn() !== 0) throw new RuntimeException('orphan training-book record remained');
    $active = $pdo->prepare('SELECT active_role_id FROM account_role_state WHERE account_id=?');
    $active->execute([$account]);
    if ((int)$active->fetchColumn() !== $activeRoleId) throw new RuntimeException('active role changed during delete');
    $survivor = $pdo->prepare('SELECT item_count FROM account_role_backpack WHERE account_id=? AND role_id=? AND item_id=812 AND item_seq=61001');
    $survivor->execute([$account, $activeRoleId]);
    if ((int)$survivor->fetchColumn() !== 4) throw new RuntimeException('other role backpack was changed');

    echo "user web backpack regression passed: exact non-active role deletion, companion cleanup, active role preserved\n";
} finally {
    cleanup($pdo, $account);
}
