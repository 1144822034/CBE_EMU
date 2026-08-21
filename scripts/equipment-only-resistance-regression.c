/*
 * Deterministic server-only regression for the equipment-only resistance
 * contract.  It neither starts a listener nor connects to MySQL.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static const vm_net_mock_equipment_catalog_item *find_usable_resistance_item(
    const vm_net_mock_role_state *role)
{
    u32 catalogCount = vm_net_mock_load_equipment_catalog();

    for (u32 index = 0; index < catalogCount; ++index)
    {
        const vm_net_mock_equipment_catalog_item *item =
            &g_vm_net_mock_equipment_catalog[index];

        if (item->slot < VM_NET_MOCK_EQUIP_SLOT_COUNT &&
            item->levelRequired <= role->level && item->bonus.resist != 0)
        {
            return item;
        }
    }
    return NULL;
}

static int assert_unequipped_resistance(u32 level, u32 job)
{
    vm_net_mock_role_state role;
    vm_net_mock_player_stats baseStats;
    vm_net_mock_player_stats fullStats;

    memset(&role, 0, sizeof(role));
    role.level = level;
    role.exp = vm_net_mock_role_level_start_exp(level);
    role.job = (u8)job;
    vm_net_mock_role_build_base_player_stats(&role, &baseStats);
    vm_net_mock_role_build_player_stats_impl(&role, &fullStats, true, false);
    if (baseStats.resist != 0 || fullStats.resist != 0)
    {
        fprintf(stderr, "unequipped resistance grew: level=%u job=%u base=%u full=%u\n",
                level, job, baseStats.resist, fullStats.resist);
        return 1;
    }
    return 0;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_player_stats baseStats;
    vm_net_mock_player_stats equippedStats;
    vm_net_mock_player_stats battleStats;
    const vm_net_mock_equipment_catalog_item *item = NULL;

    for (u32 job = 1; job <= 3; ++job)
    {
        if (assert_unequipped_resistance(1, job) != 0 ||
            assert_unequipped_resistance(70, job) != 0 ||
            assert_unequipped_resistance(100, job) != 0)
        {
            return 1;
        }
    }

    memset(&role, 0, sizeof(role));
    role.level = 100;
    role.exp = vm_net_mock_role_level_start_exp(role.level);
    role.job = 1;
    item = find_usable_resistance_item(&role);
    if (item == NULL)
    {
        fputs("could not find a usable resistance equipment record\n", stderr);
        return 1;
    }
    role.equippedItems[item->slot].itemId = item->itemId;
    role.equippedItems[item->slot].durability = item->durabilityMax;
    role.equippedItems[item->slot].durabilityMax = item->durabilityMax;
    vm_net_mock_role_build_base_player_stats(&role, &baseStats);
    vm_net_mock_role_build_player_stats_impl(&role, &equippedStats, true, false);
    if (baseStats.resist != 0 || equippedStats.resist != item->bonus.resist)
    {
        fprintf(stderr, "equipment resistance mismatch: item=%u base=%u full=%u expected=%u\n",
                item->itemId, baseStats.resist, equippedStats.resist,
                item->bonus.resist);
        return 1;
    }

    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    g_vm_net_mock_battle_active_modifier_current.remainingRounds = 1;
    g_vm_net_mock_battle_active_modifier_current.wisdom = 99;
    g_vm_net_mock_battle_active_modifier_current.resist = 999;
    battleStats = equippedStats;
    vm_net_mock_battle_apply_active_stat_modifier(&battleStats);
    if (battleStats.resist != item->bonus.resist)
    {
        fprintf(stderr, "battle modifier changed resistance: item=%u got=%u expected=%u\n",
                item->itemId, battleStats.resist, item->bonus.resist);
        return 1;
    }

    role.equippedItems[item->slot].durability = 0;
    vm_net_mock_role_build_player_stats_impl(&role, &battleStats, true, false);
    if (battleStats.resist != 0)
    {
        fprintf(stderr, "broken equipment retained resistance: item=%u resist=%u\n",
                item->itemId, battleStats.resist);
        return 1;
    }

    role.equippedItems[item->slot].durability = item->durabilityMax;
    role.level = item->levelRequired > 1 ? item->levelRequired - 1 : 1;
    role.exp = vm_net_mock_role_level_start_exp(role.level);
    if (item->levelRequired > role.level)
    {
        vm_net_mock_role_build_player_stats_impl(&role, &battleStats, true, false);
        if (battleStats.resist != 0)
        {
            fprintf(stderr, "overlevel equipment retained resistance: item=%u level=%u required=%u resist=%u\n",
                    item->itemId, role.level, item->levelRequired,
                    battleStats.resist);
            return 1;
        }
    }

    printf("equipment-only resistance regression passed item=%u resist=%u\n",
           item->itemId, item->bonus.resist);
    return 0;
}
