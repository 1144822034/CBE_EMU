<?php
declare(strict_types=1);

/* Disposable database owner for monster-balance-migration-regression.c. */

if ($argc !== 3 || !in_array($argv[1], ['setup', 'cleanup'], true) ||
    !preg_match('/^cbe_auto_[a-z0-9_]+$/', $argv[2])) {
    fwrite(STDERR, "usage: php monster-balance-migration-fixture.php setup|cleanup cbe_auto_name\n");
    exit(2);
}
$password = getenv('CBE_AUTOMATION_MYSQL_PASSWORD');
if ($password === false || $password === '') {
    throw new RuntimeException('CBE_AUTOMATION_MYSQL_PASSWORD is required');
}
$database = $argv[2];
$pdo = new PDO(
    'mysql:host=' . (getenv('CBE_AUTOMATION_MYSQL_HOST') ?: '127.0.0.1') .
    ';port=' . (getenv('CBE_AUTOMATION_MYSQL_PORT') ?: '3306') . ';charset=utf8mb4',
    getenv('CBE_AUTOMATION_MYSQL_USER') ?: 'root', $password,
    [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
     PDO::ATTR_EMULATE_PREPARES => false]);
if ($argv[1] === 'setup') {
    $pdo->exec("CREATE DATABASE `$database` CHARACTER SET utf8mb4 COLLATE utf8mb4_bin");
    echo "created isolated monster-balance database\n";
} else {
    $pdo->exec("DROP DATABASE IF EXISTS `$database`");
    echo "removed isolated monster-balance database\n";
}
