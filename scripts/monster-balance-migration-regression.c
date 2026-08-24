/*
 * Isolated persistence regression for the V4 combat and V11 reward-profile
 * migrations.
 *
 * The launcher creates a disposable cbe_auto_* database.  This test inserts
 * one fully automatic V4 reward row, one deliberately edited row, and one
 * short-lived V5 0.050%-reward row, then loads the real service database
 * layer.  It proves the transactional migrations update only fully automatic
 * reward profiles and record every migration marker.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} migration_count_context;

static bool count_row(void *contextValue, unsigned int columnCount,
                      const char *const *values, const size_t *lengths)
{
    migration_count_context *context =
        (migration_count_context *)contextValue;

    if (context == NULL || columnCount != 1 || context->found ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->count))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool query_count(const char *sql, u32 *countOut)
{
    migration_count_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(sql, count_row, &context) || context.invalid ||
        !context.found)
        return false;
    if (countOut != NULL)
        *countOut = context.count;
    return true;
}

static bool create_seed_table(void)
{
    return vm_mysql_exec(
        "CREATE TABLE IF NOT EXISTS server_monsters ("
        "monster_id SMALLINT UNSIGNED NOT NULL,level TINYINT UNSIGNED NOT NULL,"
        "family TINYINT UNSIGNED NOT NULL,hp INT UNSIGNED NOT NULL,"
        "mp INT UNSIGNED NOT NULL,attack_value INT UNSIGNED NOT NULL,"
        "defense_value INT UNSIGNED NOT NULL,reward_exp INT UNSIGNED NOT NULL,"
        "reward_money INT UNSIGNED NOT NULL,drop_item_id INT UNSIGNED NOT NULL DEFAULT 0,"
        "drop_rate_percent DECIMAL(5,2) UNSIGNED NOT NULL DEFAULT 0,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "PRIMARY KEY(monster_id)) ENGINE=InnoDB");
}

int main(void)
{
    const char *database = getenv("CBE_TEST_MYSQL_DATABASE");
    vm_net_mock_monster_entry automaticEntry;
    vm_net_mock_monster_entry manualEntry;
    vm_net_mock_monster_entry predecessorEntry;
    vm_net_mock_monster_stats automaticV1;
    vm_net_mock_monster_stats automaticV4;
    vm_net_mock_monster_stats automaticV6;
    vm_net_mock_monster_stats manualV1;
    vm_net_mock_monster_stats manualV4;
    vm_net_mock_monster_stats manualV6;
    vm_net_mock_monster_stats predecessorV1;
    vm_net_mock_monster_stats predecessorV4;
    vm_net_mock_monster_override *automaticOverride = NULL;
    vm_net_mock_monster_override *manualOverride = NULL;
    vm_net_mock_monster_override *predecessorOverride = NULL;
    const char *resetError = NULL;
    char query[1024];
    u32 markerCount = 0;
    int automaticIndex = -1;
    int manualIndex = -1;
    int predecessorIndex = -1;

    if (database == NULL || strncmp(database, "cbe_auto_", 9) != 0)
    {
        fputs("refusing non-isolated database\n", stderr);
        return 2;
    }
    vm_net_mock_monster_catalog_ensure_loaded();
    if (g_vm_net_mock_monster_catalog_count < 3)
    {
        fputs("monster catalog did not provide two real identities\n", stderr);
        return 1;
    }
    automaticEntry = g_vm_net_mock_monster_catalog_entries[0];
    manualEntry = g_vm_net_mock_monster_catalog_entries[1];
    predecessorEntry = g_vm_net_mock_monster_catalog_entries[2];
    automaticEntry.level = 60;
    automaticEntry.family = VM_NET_MOCK_MONSTER_BEAST;
    manualEntry.level = 60;
    manualEntry.family = VM_NET_MOCK_MONSTER_BEAST;
    predecessorEntry.level = 60;
    predecessorEntry.family = VM_NET_MOCK_MONSTER_BEAST;
    automaticV1 = vm_net_mock_monster_base_stats_for_entry_curve(
        &automaticEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V1);
    automaticV4 = vm_net_mock_monster_base_stats_for_entry_curve(
        &automaticEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V4);
    automaticV6 = vm_net_mock_monster_base_stats_for_entry_curve(
        &automaticEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V6);
    manualV1 = vm_net_mock_monster_base_stats_for_entry_curve(
        &manualEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V1);
    manualV4 = vm_net_mock_monster_base_stats_for_entry_curve(
        &manualEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V4);
    manualV6 = vm_net_mock_monster_base_stats_for_entry_curve(
        &manualEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V6);
    predecessorV1 = vm_net_mock_monster_base_stats_for_entry_curve(
        &predecessorEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V1);
    predecessorV4 = vm_net_mock_monster_base_stats_for_entry_curve(
        &predecessorEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V4);

    if (!create_seed_table())
    {
        fprintf(stderr, "seed schema failed: %s\n", vm_mysql_last_error());
        return 1;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_monsters(monster_id,level,family,hp,mp,attack_value,"
             "defense_value,reward_exp,reward_money) VALUES "
             "(%u,60,%u,%u,%u,%u,%u,%u,%u),"
             "(%u,60,%u,%u,%u,%u,%u,%u,%u),"
             "(%u,60,%u,%u,%u,%u,%u,%u,%u)",
             automaticEntry.enemyId, automaticEntry.family,
             automaticV1.hp, automaticV1.mp, automaticV1.attack,
             automaticV1.defense, automaticV4.exp,
             vm_net_mock_monster_reward_gold_v7_predecessor(
                 (vm_net_mock_monster_family)automaticEntry.family,
                 automaticEntry.level),
             manualEntry.enemyId, manualEntry.family,
             manualV1.hp + 1u, manualV1.mp, manualV1.attack, manualV1.defense,
             manualV4.exp + 1u,
             vm_net_mock_monster_reward_gold_v7_predecessor(
                 (vm_net_mock_monster_family)manualEntry.family,
                 manualEntry.level) + 1u,
             predecessorEntry.enemyId, predecessorEntry.family,
             predecessorV1.hp, predecessorV1.mp, predecessorV1.attack,
             predecessorV1.defense,
             vm_net_mock_normal_monster_exp_for_level_v5_predecessor(60),
             vm_net_mock_monster_reward_gold_v7_predecessor(
                 (vm_net_mock_monster_family)predecessorEntry.family,
                 predecessorEntry.level));
    if (!vm_mysql_exec(query))
    {
        fprintf(stderr, "seed rows failed: %s\n", vm_mysql_last_error());
        return 1;
    }

    if (!vm_net_mock_monster_db_load())
    {
        fprintf(stderr, "monster database load failed: %s\n", vm_mysql_last_error());
        return 1;
    }
    automaticIndex = vm_net_mock_monster_catalog_index(automaticEntry.enemyId);
    manualIndex = vm_net_mock_monster_catalog_index(manualEntry.enemyId);
    predecessorIndex = vm_net_mock_monster_catalog_index(predecessorEntry.enemyId);
    if (automaticIndex < 0 || manualIndex < 0 || predecessorIndex < 0)
    {
        fputs("seed identities disappeared from monster catalog\n", stderr);
        return 1;
    }
    automaticOverride = &g_vm_net_mock_monster_overrides[automaticIndex];
    manualOverride = &g_vm_net_mock_monster_overrides[manualIndex];
    predecessorOverride = &g_vm_net_mock_monster_overrides[predecessorIndex];
    if (!automaticOverride->used || !manualOverride->used ||
        !predecessorOverride->used ||
        automaticOverride->stats.hp != automaticV4.hp ||
        automaticOverride->stats.mp != automaticV4.mp ||
        automaticOverride->stats.attack != automaticV4.attack ||
        automaticOverride->stats.defense != automaticV4.defense ||
        automaticOverride->stats.exp != automaticV6.exp ||
        automaticOverride->stats.gold != automaticV6.gold ||
        manualOverride->stats.hp != manualV1.hp + 1u ||
        manualOverride->stats.mp != manualV1.mp ||
        manualOverride->stats.attack != manualV1.attack ||
        manualOverride->stats.defense != manualV1.defense ||
        manualOverride->stats.exp != manualV4.exp + 1u ||
        manualOverride->stats.gold !=
            vm_net_mock_monster_reward_gold_v7_predecessor(
                (vm_net_mock_monster_family)manualEntry.family,
                manualEntry.level) + 1u ||
        predecessorOverride->stats.hp != predecessorV4.hp ||
        predecessorOverride->stats.mp != predecessorV4.mp ||
        predecessorOverride->stats.attack != predecessorV4.attack ||
        predecessorOverride->stats.defense != predecessorV4.defense ||
        predecessorOverride->stats.exp != automaticV6.exp ||
        predecessorOverride->stats.gold != automaticV6.gold ||
        !query_count(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='monster-quality-zero-balance-v2'",
            &markerCount) || markerCount != 1u ||
        !query_count(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='monster-quality-zero-balance-v3'",
            &markerCount) || markerCount != 1u ||
        !query_count(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='monster-quality-zero-balance-v4'",
            &markerCount) || markerCount != 1u ||
        !query_count(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='monster-exp-reward-v2'",
            &markerCount) || markerCount != 1u ||
        !query_count(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='monster-exp-reward-v3'",
            &markerCount) || markerCount != 1u ||
        !query_count(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='monster-exp-reward-v4'",
            &markerCount) || markerCount != 1u ||
        !query_count(
            "SELECT COUNT(*) FROM server_data_migrations "
            "WHERE migration_name='monster-reward-profile-v5'",
            &markerCount) || markerCount != 1u)
    {
        fputs("formula/default migration did not preserve the explicit override boundary\n",
              stderr);
        return 1;
    }

    /* The admin reset now restores the complete combat reward profile:
     * HP, MP, attack, defense, experience, and money.  Drops stay independent.
     * Verify the result both in memory and after a fresh database load. */
    if (!vm_net_mock_monster_admin_reset_combat_stats(
            manualEntry.enemyId, &resetError) ||
        manualOverride->stats.hp != manualV4.hp ||
        manualOverride->stats.mp != manualV4.mp ||
        manualOverride->stats.attack != manualV4.attack ||
        manualOverride->stats.defense != manualV4.defense ||
        manualOverride->stats.exp != manualV6.exp ||
        manualOverride->stats.gold != manualV6.gold)
    {
        fprintf(stderr, "combat reward reset did not restore defaults: %s\n",
                resetError != NULL ? resetError : "unknown error");
        return 1;
    }
    vm_mysql_close();
    g_vm_net_mock_monster_db_loaded = false;
    g_vm_net_mock_monster_db_valid = false;
    if (!vm_net_mock_monster_db_load())
    {
        fprintf(stderr, "monster reload after combat reset failed: %s\n",
                vm_mysql_last_error());
        return 1;
    }
    manualOverride = &g_vm_net_mock_monster_overrides[manualIndex];
    if (!manualOverride->used || manualOverride->stats.hp != manualV4.hp ||
        manualOverride->stats.mp != manualV4.mp ||
        manualOverride->stats.attack != manualV4.attack ||
        manualOverride->stats.defense != manualV4.defense ||
        manualOverride->stats.exp != manualV6.exp ||
        manualOverride->stats.gold != manualV6.gold)
    {
        fputs("combat reward reset persistence did not restore rewards\n", stderr);
        return 1;
    }
    vm_mysql_close();
    puts("monster balance migration regression passed: default reward migration and six-field reset");
    return 0;
}
