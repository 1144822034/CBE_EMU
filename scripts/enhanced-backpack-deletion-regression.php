<?php
/*
 * End-to-end regression for the two web routes that delete a backpack
 * instance.  The service must already be running against the supplied,
 * isolated automation schema.
 *
 * Usage:
 *   CBE_AUTOMATION_MYSQL_PASSWORD=... php scripts/enhanced-backpack-deletion-regression.php <port> <database>
 */

declare(strict_types=1);

$port = isset($argv[1]) ? (int)$argv[1] : 0;
$database = $argv[2] ?? '';
$password = getenv('CBE_AUTOMATION_MYSQL_PASSWORD');
$account = 'enhanced-delete-' . random_int(100000, 999999);
$adminAccount = 'enhanced-delete-admin-' . random_int(100000, 999999);
$loginPassword = 'enhanced-delete-user';
$adminPassword = 'enhanced-delete-admin';

if ($port < 1024 || $port > 65535 ||
    !preg_match('/^jh_online_autotest_[0-9a-f]{16,32}$/', $database) ||
    $password === false || $password === '') {
    throw new RuntimeException('requires an isolated jh_online_autotest_* database and CBE_AUTOMATION_MYSQL_PASSWORD');
}

$pdo = new PDO(
    'mysql:host=' . (getenv('CBE_AUTOMATION_MYSQL_HOST') ?: '127.0.0.1') .
    ';port=' . (getenv('CBE_AUTOMATION_MYSQL_PORT') ?: '3306') .
    ';dbname=' . $database . ';charset=utf8mb4',
    getenv('CBE_AUTOMATION_MYSQL_USER') ?: 'root', $password,
    [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_EMULATE_PREPARES => false]
);

function http_request(int $port, string $method, string $path,
                      string $body = '', string $cookie = ''): array {
    $headers = ['Host: 127.0.0.1:' . $port, 'Connection: close'];
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
    $response = @file_get_contents('http://127.0.0.1:' . $port . $path, false,
                                   $context);
    $responseHeaders = $http_response_header ?? [];
    if (!$responseHeaders) throw new RuntimeException('missing HTTP response for ' . $path);
    return [$responseHeaders, $response === false ? '' : $response];
}

function status_code(array $headers): int {
    return preg_match('/\\s(\\d{3})\\s/', $headers[0] ?? '', $matches)
        ? (int)$matches[1] : 0;
}

function is_redirect(array $headers): bool {
    return status_code($headers) >= 300 && status_code($headers) < 400;
}

function redirect_ok(array $headers): bool {
    if (!is_redirect($headers)) return false;
    foreach ($headers as $header) {
        if (stripos($header, 'Location:') === 0 && strpos($header, 'status=ok') !== false)
            return true;
    }
    return false;
}

function cookie_value(array $headers, string $name): string {
    foreach ($headers as $header) {
        if (stripos($header, 'Set-Cookie:') !== 0) continue;
        if (preg_match('/' . preg_quote($name, '/') . '=[^;]+/', $header, $matches))
            return $matches[0];
    }
    return '';
}

function count_instance(PDO $pdo, string $account, int $roleId,
                        int $itemId, int $itemSeq): int {
    $query = $pdo->prepare(
        'SELECT COUNT(*) FROM account_role_backpack '
        . 'WHERE account_id=? AND role_id=? AND item_id=? AND item_seq=?'
    );
    $query->execute([$account, $roleId, $itemId, $itemSeq]);
    return (int)$query->fetchColumn();
}

function cleanup(PDO $pdo, string $account, string $adminAccount): void {
    $pdo->beginTransaction();
    try {
        $hasLogTable = (bool)$pdo->query(
            "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() "
            . "AND TABLE_NAME='server_admin_operation_logs'"
        )->fetchColumn();
        if ($hasLogTable) {
            $pdo->prepare('DELETE FROM server_admin_operation_logs '
                          . 'WHERE target_account_id=? OR operator_account_id=?')
                ->execute([$account, $adminAccount]);
        }
        $pdo->prepare('DELETE FROM account_role_state WHERE account_id=?')->execute([$account]);
        $pdo->prepare('DELETE FROM accounts WHERE account_id=?')->execute([$account]);
        $pdo->prepare('DELETE FROM server_admin_users WHERE account_id=?')
            ->execute([$adminAccount]);
        $pdo->commit();
    } catch (Throwable $error) {
        if ($pdo->inTransaction()) $pdo->rollBack();
        throw $error;
    }
}

try {
    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS server_admin_users ('
        . 'account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,'
        . 'password_value VARBINARY(64) NOT NULL,failed_attempts TINYINT UNSIGNED NOT NULL DEFAULT 0,'
        . 'locked TINYINT UNSIGNED NOT NULL DEFAULT 0,created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,'
        . 'updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,'
        . 'PRIMARY KEY(account_id)) ENGINE=InnoDB'
    );
    cleanup($pdo, $account, $adminAccount);

    [$headers] = http_request($port, 'POST', '/user/register', http_build_query([
        'account' => $account, 'password' => $loginPassword,
    ]));
    $userCookie = cookie_value($headers, 'cbe_user');
    if (!is_redirect($headers) || $userCookie === '')
        throw new RuntimeException('user registration did not issue a session');

    $maxRoleId = (int)$pdo->query(
        'SELECT COALESCE(MAX(role_id),900000) FROM account_roles'
    )->fetchColumn();
    $activeRoleId = max($maxRoleId + 1, 900001);
    $targetRoleId = $activeRoleId + 1;
    /* This web-only fixture does not enter a scene; an ASCII placeholder keeps
     * it independent of pre-existing roles in the isolated schema. */
    $scene = 'automation.scene';

    $pdo->beginTransaction();
    try {
        $pdo->prepare(
            'INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) '
            . 'VALUES(?,5,?,2)'
        )->execute([$account, $activeRoleId]);
        $roleInsert = $pdo->prepare(
            'INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,'
            . 'level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,'
            . 'designation_id,next_backpack_seq) VALUES(?,?,?, ?,1,1,20,1,0,120,120,100,100,0,0,?,220,440,2,0,?)'
        );
        $roleInsert->execute([$account, $activeRoleId, 0, 'EnhancedDeleteActive', $scene, 61002]);
        $roleInsert->execute([$account, $targetRoleId, 1, 'EnhancedDeleteTarget', $scene, 61004]);
        $bagInsert = $pdo->prepare(
            'INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,'
            . 'enhance_level,durability,durability_max) VALUES(?,?,?,?,?,?,?,?,?)'
        );
        $bagInsert->execute([$account, $activeRoleId, 0, 812, 61001, 4, 0, 0, 0]);
        $bagInsert->execute([$account, $targetRoleId, 0, 1101, 61002, 1, 2, 80, 80]);
        $bagInsert->execute([$account, $targetRoleId, 1, 1102, 61003, 1, 3, 90, 90]);
        $pdo->commit();
    } catch (Throwable $error) {
        $pdo->rollBack();
        throw $error;
    }

    [$headers, $dashboard] = http_request($port, 'GET', '/', '', $userCookie);
    if (status_code($headers) !== 200 ||
        strpos($dashboard, 'name="item_seq" value="61002"') === false ||
        strpos($dashboard, '强化 +2') === false) {
        throw new RuntimeException('user backpack did not render the enhanced instance');
    }
    [$headers] = http_request($port, 'POST', '/user/backpack/delete', http_build_query([
        'role_id' => $targetRoleId, 'item_id' => 1101, 'item_seq' => 61002,
    ]), $userCookie);
    if (!redirect_ok($headers) || count_instance($pdo, $account, $targetRoleId, 1101, 61002) !== 0 ||
        count_instance($pdo, $account, $targetRoleId, 1102, 61003) !== 1) {
        throw new RuntimeException('user discard did not remove only its enhanced instance');
    }

    $pdo->prepare(
        'INSERT INTO server_admin_users(account_id,password_value,failed_attempts,locked) VALUES(?,?,0,0)'
    )->execute([$adminAccount, $adminPassword]);
    [$headers] = http_request($port, 'POST', '/admin-418yz6/login', http_build_query([
        'account' => $adminAccount, 'password' => $adminPassword,
    ]));
    $adminCookie = cookie_value($headers, 'cbe_admin');
    if (!is_redirect($headers) || $adminCookie === '')
        throw new RuntimeException('automation administrator login failed');
    [$headers] = http_request($port, 'POST', '/admin-418yz6/action', http_build_query([
        'action' => 'remove-role-backpack-item', 'account' => $account,
        'role' => $targetRoleId, 'item' => 1102, 'item_seq' => 61003,
    ]), $adminCookie);
    if (!redirect_ok($headers) || count_instance($pdo, $account, $targetRoleId, 1102, 61003) !== 0 ||
        count_instance($pdo, $account, $activeRoleId, 812, 61001) !== 1) {
        throw new RuntimeException('admin delete did not remove only its enhanced instance');
    }

    echo "enhanced backpack deletion regression passed: user discard and admin delete\n";
} finally {
    cleanup($pdo, $account, $adminAccount);
}
