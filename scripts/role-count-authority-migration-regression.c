/*
 * Isolated persistence regression for role-count-authority-v3.
 *
 * The launcher owns a disposable cbe_auto_* database. It first records the
 * historical v2 marker, then reproduces stale account_role_state.role_count,
 * runs the real v3 startup migration with database triggers, exercises direct
 * single/batch writes and role changes, then exercises the relational loader.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool role_count_test_query_u32(const char *query, u32 *valueOut)
{
    return vm_mock_service_role_count_query_u32(query, valueOut);
}

static bool role_count_test_create_schema(void)
{
    return vm_mysql_exec(
               "CREATE TABLE accounts ("
               "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
               "password_value VARBINARY(64) NOT NULL,PRIMARY KEY(account_id)) ENGINE=InnoDB") &&
           vm_mysql_exec(
               "CREATE TABLE friendships ("
               "owner_account_id VARCHAR(63) NOT NULL,owner_role_id INT UNSIGNED NOT NULL,"
               "target_account_id VARCHAR(63) NOT NULL,target_role_id INT UNSIGNED NOT NULL,"
               "PRIMARY KEY(owner_account_id,owner_role_id,target_account_id,target_role_id)) ENGINE=InnoDB") &&
           vm_mysql_exec(
               "CREATE TABLE account_role_state ("
               "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
               "format_version INT UNSIGNED NOT NULL,active_role_id INT UNSIGNED NOT NULL DEFAULT 0,"
               "role_count TINYINT UNSIGNED NOT NULL DEFAULT 0,PRIMARY KEY(account_id)) ENGINE=InnoDB") &&
           vm_mysql_exec(
               "CREATE TABLE account_roles ("
               "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
               "role_id INT UNSIGNED NOT NULL,role_index TINYINT UNSIGNED NOT NULL,"
               "role_name VARBINARY(32) NOT NULL,job TINYINT UNSIGNED NOT NULL,"
               "sex TINYINT UNSIGNED NOT NULL,backpack_capacity TINYINT UNSIGNED NOT NULL,"
               "level INT UNSIGNED NOT NULL,exp INT UNSIGNED NOT NULL,hp INT UNSIGNED NOT NULL,"
               "hp_max INT UNSIGNED NOT NULL,mp INT UNSIGNED NOT NULL,mp_max INT UNSIGNED NOT NULL,"
               "money INT UNSIGNED NOT NULL,wcoin INT UNSIGNED NOT NULL,scene VARBINARY(64) NOT NULL,"
               "pos_x SMALLINT UNSIGNED NOT NULL,pos_y SMALLINT UNSIGNED NOT NULL,"
               "backpack_item_count TINYINT UNSIGNED NOT NULL,designation_id TINYINT UNSIGNED NOT NULL,"
               "next_backpack_seq SMALLINT UNSIGNED NOT NULL,PRIMARY KEY(account_id,role_id),"
               "UNIQUE KEY uk_role_id(role_id),UNIQUE KEY uk_role_index(account_id,role_index)) ENGINE=InnoDB") &&
           vm_mysql_exec(
               "CREATE TABLE account_role_equipment ("
               "account_id VARCHAR(63) NOT NULL,role_id INT UNSIGNED NOT NULL,"
               "slot_index TINYINT UNSIGNED NOT NULL,item_id INT UNSIGNED NOT NULL,"
               "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
               "enhance_affix_types INT UNSIGNED NOT NULL DEFAULT 0,"
               "enhance_affix_values BIGINT UNSIGNED NOT NULL DEFAULT 0,"
               "durability SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
               "durability_max SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
               "PRIMARY KEY(account_id,role_id,slot_index)) ENGINE=InnoDB") &&
           vm_mysql_exec(
               "CREATE TABLE account_role_backpack ("
               "account_id VARCHAR(63) NOT NULL,role_id INT UNSIGNED NOT NULL,"
               "slot_index SMALLINT UNSIGNED NOT NULL,item_id INT UNSIGNED NOT NULL,"
               "item_seq SMALLINT UNSIGNED NOT NULL,item_count INT UNSIGNED NOT NULL,"
               "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
               "enhance_affix_types INT UNSIGNED NOT NULL DEFAULT 0,"
               "enhance_affix_values BIGINT UNSIGNED NOT NULL DEFAULT 0,"
               "durability SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
               "durability_max SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
               "PRIMARY KEY(account_id,role_id,slot_index)) ENGINE=InnoDB");
}

static bool role_count_test_seed(void)
{
    return vm_mysql_exec(
               "INSERT INTO accounts(account_id,password_value) VALUES "
               "('broken','x'),('empty','x'),('clean','x'),('insert_only','x')") &&
           vm_mysql_exec(
               "INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) VALUES "
               "('broken',8,101,3),('empty',8,0,1),('clean',8,201,1)") &&
           vm_mysql_exec(
               "INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,"
               "backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,"
               "backpack_item_count,designation_id,next_backpack_seq) VALUES "
               "('broken',101,0,X'41',1,0,20,1,0,100,100,50,50,10,0,X'746573742E736365',1,2,0,0,1),"
               "('broken',102,1,X'42',1,0,20,1,0,100,100,50,50,10,0,X'746573742E736365',3,4,0,0,1),"
               "('clean',201,0,X'43',1,0,20,1,0,100,100,50,50,10,0,X'746573742E736365',5,6,0,0,1)");
}

int main(void)
{
    const char *database = getenv("CBE_TEST_MYSQL_DATABASE");
    u32 value = 0;
    bool found = false;
    bool backfill = false;
    bool expMigration = false;
    bool affixMigration = false;

    if (database == NULL || strncmp(database, "cbe_auto_", 9) != 0)
    {
        fputs("refusing non-isolated database\n", stderr);
        return 2;
    }
    if (!role_count_test_create_schema() || !role_count_test_seed())
    {
        fprintf(stderr, "fixture setup failed: %s\n", vm_mysql_last_error());
        return 1;
    }
    if (!vm_mock_service_mysql_authority_prepare() ||
        !vm_mysql_exec(
            "INSERT INTO server_data_migrations(migration_name) VALUES"
            "('role-count-authority-v2')") ||
        !vm_mock_service_role_count_authority_prepare_and_migrate())
    {
        fprintf(stderr, "role-count migration failed: %s\n", vm_mysql_last_error());
        return 1;
    }
    if (!role_count_test_query_u32(
            "SELECT COUNT(*) FROM account_role_state WHERE "
            "(account_id='broken' AND role_count=2 AND format_version=8) OR "
            "(account_id='empty' AND role_count=0 AND format_version=8) OR "
            "(account_id='clean' AND role_count=1 AND format_version=8)", &value) ||
        value != 3u ||
        !role_count_test_query_u32(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='role-count-authority-v2'", &value) ||
        value != 1u ||
        !role_count_test_query_u32(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='role-count-authority-v3'", &value) ||
        value != 1u ||
        !role_count_test_query_u32("SELECT COUNT(*) FROM account_roles", &value) ||
        value != 3u)
    {
        fputs("migration did not reconcile only the cached role counts\n", stderr);
        return 1;
    }

    /* Direct state writes are corrected at the database boundary.  The audit
     * rows retain the attempted values, including every row of a batch UPDATE. */
    if (!role_count_test_query_u32(
            "SELECT COUNT(*) FROM account_role_count_write_audit", &value) ||
        value != 0u ||
        !vm_mysql_exec(
            "UPDATE account_role_state SET role_count=99 WHERE account_id='broken'") ||
        !vm_mysql_exec(
            "UPDATE account_role_state SET role_count=77 WHERE account_id IN "
            "('empty','clean')") ||
        !vm_mysql_exec(
            "INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) "
            "VALUES('insert_only',8,0,7)") ||
        !role_count_test_query_u32(
            "SELECT COUNT(*) FROM account_role_state WHERE "
            "(account_id='broken' AND role_count=2) OR "
            "(account_id='empty' AND role_count=0) OR "
            "(account_id='clean' AND role_count=1) OR "
            "(account_id='insert_only' AND role_count=0)", &value) ||
        value != 4u ||
        !role_count_test_query_u32(
            "SELECT COUNT(*) FROM account_role_count_write_audit "
            "WHERE attempted_role_count IN (99,77,7) "
            "AND attempted_role_count<>authoritative_role_count", &value) ||
        value != 4u ||
        !role_count_test_query_u32(
            "SELECT COUNT(*) FROM account_role_count_write_audit "
            "WHERE connection_id<>0 AND database_user<>''", &value) ||
        value != 4u)
    {
        fputs("database trigger did not correct and audit direct state writes\n", stderr);
        return 1;
    }

    g_vm_mock_service_active_account_id = "broken";
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    if (!vm_net_mock_role_db_load_mysql_relational(
            &found, &backfill, &expMigration, &affixMigration) || !found ||
        g_vm_net_mock_role_db.roleCount != 2u ||
        g_vm_net_mock_role_db.activeRoleId != 101u ||
        g_vm_net_mock_role_db.roles[0].roleId != 101u ||
        g_vm_net_mock_role_db.roles[1].roleId != 102u)
    {
        fputs("reconciled account still failed the relational role loader\n", stderr);
        return 1;
    }
    if (!vm_mock_service_role_count_authority_prepare_and_migrate() ||
        !role_count_test_query_u32(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='role-count-authority-v3'", &value) ||
        value != 1u)
    {
        fputs("migration marker was not idempotent\n", stderr);
        return 1;
    }

    /* Simulate a post-v3 writer that bypassed the trigger during a deployment
     * gap.  A marker must not suppress the next startup reconciliation. */
    if (!vm_mysql_exec("DROP TRIGGER cbe_role_count_state_bu") ||
        !vm_mysql_exec(
            "UPDATE account_role_state SET role_count=1 WHERE account_id='broken'") ||
        !vm_mock_service_role_count_authority_prepare_and_migrate() ||
        !role_count_test_query_u32(
            "SELECT role_count FROM account_role_state WHERE account_id='broken'", &value) ||
        value != 2u)
    {
        fputs("existing marker did not trigger runtime reconciliation\n", stderr);
        return 1;
    }

    /* A role move/admin change can alter account_roles while an existing
     * session still has an older in-memory role list.  An unrelated active
     * role save must repair the cached aggregate and refuse that stale cache,
     * rather than writing its old count back to account_role_state. */
    g_vm_net_mock_role_db_valid = true;
    if (!vm_mysql_exec(
            "INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,"
            "backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,"
            "backpack_item_count,designation_id,next_backpack_seq) VALUES "
            "('broken',103,2,X'43',1,0,20,1,0,100,100,50,50,10,0,"
            "X'746573742E736365',5,6,0,0,1)") ||
        !vm_mysql_exec(
            "UPDATE account_role_state SET role_count=2 WHERE account_id='broken'") ||
        vm_net_mock_role_db_save_relational("stale-cache-regression", NULL, NULL,
                                            0, false, NULL, NULL, NULL) ||
        !role_count_test_query_u32(
            "SELECT COUNT(*) FROM account_role_state "
            "WHERE account_id='broken' AND role_count=3", &value) ||
        value != 1u || g_vm_net_mock_role_db_loaded || g_vm_net_mock_role_db_valid)
    {
        fputs("stale role cache rewrote role_count instead of repairing and invalidating\n", stderr);
        return 1;
    }
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    if (!vm_net_mock_role_db_load_mysql_relational(
            &found, &backfill, &expMigration, &affixMigration) || !found ||
        g_vm_net_mock_role_db.roleCount != 3u ||
        g_vm_net_mock_role_db.roles[2].roleId != 103u)
    {
        fputs("repaired role count did not reload from account_roles\n", stderr);
        return 1;
    }

    if (!vm_mysql_exec("DELETE FROM account_roles WHERE account_id='broken' AND role_id=103") ||
        !role_count_test_query_u32(
            "SELECT role_count FROM account_role_state WHERE account_id='broken'", &value) ||
        value != 2u)
    {
        fputs("role delete trigger did not maintain the derived count\n", stderr);
        return 1;
    }
    if (!vm_mysql_exec(
            "INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,"
            "backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,"
            "backpack_item_count,designation_id,next_backpack_seq) VALUES "
            "('broken',103,2,X'43',1,0,20,1,0,100,100,50,50,10,0,"
            "X'746573742E736365',5,6,0,0,1)") ||
        !role_count_test_query_u32(
            "SELECT role_count FROM account_role_state WHERE account_id='broken'", &value) ||
        value != 3u)
    {
        fputs("role insert trigger did not maintain the derived count\n", stderr);
        return 1;
    }

    if (!vm_mysql_exec(
            "DELETE FROM server_data_migrations "
            "WHERE migration_name='role-count-authority-v3'") ||
        !vm_mysql_exec(
            "UPDATE account_role_state SET role_count=1 WHERE account_id='empty'") ||
        !vm_mysql_exec(
            "UPDATE account_roles SET role_index=1 WHERE account_id='clean'") ||
        vm_mock_service_role_count_authority_prepare_and_migrate() ||
        !role_count_test_query_u32(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='role-count-authority-v3'", &value) ||
        value != 0u ||
        !role_count_test_query_u32(
            "SELECT role_count FROM account_role_state WHERE account_id='empty'",
            &value) || value != 0u)
    {
        fputs("invalid role structure was not rejected atomically\n", stderr);
        return 1;
    }
    vm_mysql_close();
    puts("role-count authority migration regression passed: v2-to-v3 counts repaired, stale active-save cache repaired and invalidated, roles preserved, loader accepted, rerun idempotent, invalid structure rejected");
    return 0;
}
