<?php
/*
 * Isolated database fixture for shop-return-hangup-v1.  This script never
 * writes to jh_online: its database argument must be a freshly generated
 * jh_online_autotest_<hex> schema owned by the automation runner.  `create`
 * reads only schema metadata from jh_online so the isolated schema preserves
 * the service's transactional table contract.
 *
 * Usage:
 *   php scripts/automation-shop-return-hangup-fixture.php create <database>
 *   php scripts/automation-shop-return-hangup-fixture.php seed <database> [hangup-peach|hangup-vitals|hangup-vitals-flask|teleport-stone-c00|equipment-enhance|linan-scene-battle]
 *   php scripts/automation-shop-return-hangup-fixture.php client-login <nvram-file>
 *   php scripts/automation-shop-return-hangup-fixture.php cleanup <database>
 */

declare(strict_types=1);

function require_test_database(string $database): void {
    if (!preg_match('/^jh_online_autotest_[0-9a-f]{16,32}$/', $database)) {
        throw new InvalidArgumentException('refusing non-isolated automation database');
    }
}

function pdo(?string $database = null): PDO {
    $host = getenv('CBE_AUTOMATION_MYSQL_HOST') ?: '127.0.0.1';
    $port = getenv('CBE_AUTOMATION_MYSQL_PORT') ?: '3306';
    $password = getenv('CBE_AUTOMATION_MYSQL_PASSWORD');
    if ($password === false || $password === '') {
        throw new RuntimeException('CBE_AUTOMATION_MYSQL_PASSWORD is required');
    }
    $dsn = "mysql:host=$host;port=$port";
    if ($database !== null) $dsn .= ";dbname=$database";
    return new PDO($dsn, getenv('CBE_AUTOMATION_MYSQL_USER') ?: 'root', $password, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_EMULATE_PREPARES => false,
    ]);
}

$mode = $argv[1] ?? '';
$database = $argv[2] ?? '';

/* The client persistence fixture is intentionally separate from the MySQL
 * fixture.  It provides the same 180-byte LoginRecord that Storage_Date
 * persists after a genuine title login, so the actual title protocol takes
 * its existing-account branch instead of generating an unrelated guest.
 * This is host-side test setup only: no guest memory, CBE/CBM code or network
 * payload is changed. */
if ($mode === 'client-login') {
    $path = $database;
    if ($path === '' || !preg_match('/(?:^|[\\\\\/])nvram[\\\\\/][^\\\\\/]+_storage_mmorpg_LoginRecord\\.bin$/', $path)) {
        throw new InvalidArgumentException('refusing unexpected LoginRecord fixture path');
    }
    $directory = dirname($path);
    if (!is_dir($directory) && !mkdir($directory, 0777, true) && !is_dir($directory)) {
        throw new RuntimeException('could not create isolated nvram directory');
    }
    $record = str_repeat("\0", 180);
    $record[0] = "\x01";
    $record = substr_replace($record, "guest00001\0", 16, 11);
    $record = substr_replace($record, "automation-only\0", 48, 16);
    $record[80] = "\x01";
    $record = substr_replace($record, '1111111111111111', 81, 16);
    if (file_put_contents($path, $record, LOCK_EX) !== 180) {
        throw new RuntimeException('could not write isolated LoginRecord fixture');
    }
    echo "wrote isolated client LoginRecord for guest00001\n";
    exit(0);
}

require_test_database($database);

if ($mode === 'create') {
    $db = pdo();
    /* Copy schema only.  The service validates its transactional authority
     * before it creates optional tables, so an empty database is not a valid
     * test target.  SHOW CREATE TABLE is read-only against jh_online; no data
     * or user row is copied. */
    $source = pdo('jh_online');
    $tables = $source->query('SHOW FULL TABLES WHERE Table_type = \'BASE TABLE\'')
        ->fetchAll(PDO::FETCH_NUM);
    if (count($tables) === 0) throw new RuntimeException('jh_online schema has no base tables');
    $db->exec(sprintf('CREATE DATABASE %s CHARACTER SET utf8mb4 COLLATE utf8mb4_bin', $database));
    $db->exec(sprintf('USE %s', $database));
    $db->exec('SET FOREIGN_KEY_CHECKS=0');
    try {
        foreach ($tables as $row) {
            $table = (string)$row[0];
            if (!preg_match('/^[A-Za-z0-9_]+$/', $table)) {
                throw new RuntimeException('unexpected source table name');
            }
            $create = $source->query(sprintf('SHOW CREATE TABLE %s', $table))->fetch(PDO::FETCH_NUM);
            if ($create === false || !isset($create[1])) {
                throw new RuntimeException("could not read schema for $table");
            }
            $db->exec($create[1]);
        }
        $db->exec('SET FOREIGN_KEY_CHECKS=1');
    } catch (Throwable $error) {
        $db->exec('SET FOREIGN_KEY_CHECKS=1');
        throw $error;
    }
    echo "created isolated automation database with schema only\n";
    exit(0);
}

if ($mode === 'cleanup') {
    $db = pdo();
    $db->exec(sprintf('DROP DATABASE IF EXISTS %s', $database));
    echo "removed isolated automation database\n";
    exit(0);
}

if ($mode !== 'seed') {
    throw new InvalidArgumentException('expected create, seed, or cleanup');
}

/* `create` has already copied the schema; seed before the isolated service
 * starts so its startup migration encounters a coherent test account. */
$db = pdo($database);
$account = 'guest00001';
$roleId = 810001;
$profile = $argv[3] ?? 'hangup-peach';
$backpackItemCount = 0;
$nextBackpackSeq = 1;
if ($profile === 'hangup-peach') {
    $scene = hex2bin('3031CCD2BBA8B5BA5F30312E736365'); /* 01桃花岛_01.sce, GBK */
    $posX = 146;
    $posY = 349;
    $hp = 120;
    $mp = 100;
} elseif ($profile === 'hangup-vitals') {
    /* Same authored scene node as the real hangup flow, but deliberately
     * below maximum vitals.  The regression runner uses an explicit service
     * recovery fixture and must prove that the next native battle starts from
     * the post-4/7 absolute state rather than a stale pre-settlement cache. */
    $scene = hex2bin('3031CCD2BBA8B5BA5F30312E736365'); /* 01桃花岛_01.sce, GBK */
    $posX = 146;
    $posY = 349;
    $hp = 80;
    $mp = 70;
} elseif ($profile === 'hangup-vitals-flask') {
    /* Identical initial vitals to hangup-vitals, but the recovery originates
     * from the actual 802/803 reservoir items.  This keeps the client-side
     * expected result (95/295,80/205) fixed while proving that 4/6's cached
     * MP and 4/7's HP/MP deltas agree with the durable item consumption path. */
    $scene = hex2bin('3031CCD2BBA8B5BA5F30312E736365'); /* 01桃花岛_01.sce, GBK */
    $posX = 146;
    $posY = 349;
    $hp = 80;
    $mp = 70;
    /* account_roles is the authoritative in-memory allocation bound used
     * while loading account_role_backpack.  Keep it consistent with the two
     * rows below; otherwise normalization correctly treats them as outside
     * the role's declared backpack and the test never reaches flask use. */
    $backpackItemCount = 2;
    $nextBackpackSeq = 3;
} elseif ($profile === 'teleport-stone-c00') {
    /* Exact resource key: this is not interchangeable with 00蓬莱仙岛_03
     * or 00_蓬莱仙岛03.  (157,47) is the authored safe landing beside the
     * c00 scene's north portal, established from the SCE entry record. */
    $scene = hex2bin('633030C5EEC0B3CFC9B5BA5F30332E736365'); /* c00蓬莱仙岛_03.sce, GBK */
    $posX = 157;
    $posY = 47;
    $hp = 120;
    $mp = 100;
} elseif ($profile === 'equipment-enhance') {
    /* One actual level-3 wooden wide-sword instance in the backpack.  The instance has
     * exactly the same item ID, sequence, durability, and enhancement shape
     * as the user's reproduction, while remaining inside an isolated schema.
     * The list count and next sequence must agree with the inserted row. */
    $scene = hex2bin('3031CCD2BBA8B5BA5F30312E736365'); /* 01桃花岛_01.sce, GBK */
    $posX = 146;
    $posY = 349;
    $hp = 120;
    $mp = 100;
    $backpackItemCount = 1;
    $nextBackpackSeq = 10;
} elseif ($profile === 'linan-scene-battle') {
    /* c04临安府_01.sce, GBK.  The copied isolated resource overlay contains
     * the deployed battle actor; this role position is the recorded manual
     * collision fixture, not a fabricated client-side coordinate. */
    $scene = hex2bin('633034C1D9B0B2B8AE5F30312E736365');
    $posX = 108;
    $posY = 104;
    $hp = 120;
    $mp = 100;
} else {
    throw new InvalidArgumentException('unknown isolated automation fixture profile');
}

$db->beginTransaction();
try {
    foreach ([
        'account_role_equipment_durability', 'account_role_equipment',
        'account_role_skills', 'account_role_tasks', 'account_role_backpack',
        'account_role_state'
    ] as $table) {
        $db->prepare("DELETE FROM $table WHERE account_id=?")->execute([$account]);
    }
    $db->prepare('DELETE FROM account_roles WHERE account_id=?')->execute([$account]);
    $db->prepare('DELETE FROM accounts WHERE account_id=?')->execute([$account]);
    $db->prepare('INSERT INTO accounts(account_id,password_value) VALUES(?,?)')
        ->execute([$account, 'automation-only']);
    $db->prepare('INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES(?,6,?,1)')
        ->execute([$account, $roleId]);
    $db->prepare(
        'INSERT INTO account_roles('
        . 'account_id,role_id,role_index,role_name,job,sex,backpack_capacity,'
        . 'level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,'
        . 'backpack_item_count,designation_id,next_backpack_seq) '
        . 'VALUES(?,?,0,?,1,1,20,5,1250,?,120,?,100,100000,1000,?,?,?, ?,0,?)'
    )->execute([$account, $roleId, 'AutoHangup', $hp, $mp, $scene, $posX, $posY,
                $backpackItemCount, $nextBackpackSeq]);
    /* Keep one real, durable equip.dsh weapon on the isolated role.  Besides
     * matching a normal new-character state, this makes the client execute
     * its native CalcEquipStatBonus path during scene status reconstruction.
     * The automation probe only reads that client-owned enhancement table;
     * it does not synthesize a result or modify any production role. */
    if ($profile !== 'equipment-enhance') {
        $db->prepare(
            'INSERT INTO account_role_equipment('
            . 'account_id,role_id,slot_index,item_id,enhance_level,durability,durability_max) '
            . 'VALUES(?,?,?,?,?,?,?)'
        )->execute([$account, $roleId, 0, 1001, 0, 50, 50]);
    }
    if ($profile === 'hangup-vitals-flask') {
        /* item_count is the native reservoir amount for 802/803, not a stack
         * quantity.  One terminal result consumes these exact 15/10 units. */
        $db->prepare(
            'INSERT INTO account_role_backpack('
            . 'account_id,role_id,slot_index,item_id,item_seq,item_count,'
            . 'enhance_level,durability,durability_max) VALUES(?,?,?,?,?,?,?,?,?)'
        )->execute([$account, $roleId, 0, 802, 1, 15, 0, 0, 0]);
        $db->prepare(
            'INSERT INTO account_role_backpack('
            . 'account_id,role_id,slot_index,item_id,item_seq,item_count,'
            . 'enhance_level,durability,durability_max) VALUES(?,?,?,?,?,?,?,?,?)'
        )->execute([$account, $roleId, 1, 803, 2, 10, 0, 0, 0]);
    } elseif ($profile === 'equipment-enhance') {
        $db->prepare(
            'INSERT INTO account_role_backpack('
            . 'account_id,role_id,slot_index,item_id,item_seq,item_count,'
            . 'enhance_level,durability,durability_max) VALUES(?,?,?,?,?,?,?,?,?)'
        )->execute([$account, $roleId, 0, 1101, 9, 1, 2, 80, 80]);
    }
    $db->commit();
    echo "seeded guest00001 role=810001 profile=$profile in isolated automation database\n";
} catch (Throwable $error) {
    $db->rollBack();
    throw $error;
}
