/*
 * Server-only regression for player-3's level-70 ghost-path equipment
 * snapshot.  It proves the primary-stat path does not reintroduce the old
 * 999 ceiling between DSH equipment parsing and hostile-skill damage.
 *
 * The fixture is offline: it opens no listener and does not connect to MySQL.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static void equip_catalog_item(vm_net_mock_role_state *role,
                               const vm_net_mock_equipment_catalog_item *item)
{
    if (role == NULL || item == NULL || item->slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
        return;
    role->equippedItems[item->slot].itemId = item->itemId;
    role->equippedItems[item->slot].durability = item->durabilityMax;
    role->equippedItems[item->slot].durabilityMax = item->durabilityMax;
}

int main(void)
{
    const u32 roleId = 920002;
    const u32 skillId = 201;
    const u32 enemyDefense = 65;
    const vm_net_mock_equipment_catalog_item *staff = NULL;
    const vm_net_mock_equipment_catalog_item *helmet = NULL;
    const vm_net_mock_equipment_catalog_item *boots = NULL;
    const vm_net_mock_skill_catalog_item *skill = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_player_stats withoutBoots;
    vm_net_mock_player_stats withBoots;
    u32 rawWithoutBoots = 0;
    u32 rawWithBoots = 0;
    u32 damageWithoutBoots = 0;
    u32 damageWithBoots = 0;

    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
    g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = roleId;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;

    role = &g_vm_net_mock_role_db.roles[0];
    role->roleId = roleId;
    role->level = 70;
    role->exp = vm_net_mock_role_level_start_exp(role->level);
    role->job = 3; /* Ghost path, rawJob=2 in skill.dsh. */

    staff = vm_net_mock_find_equipment_catalog_item(2001);
    helmet = vm_net_mock_find_equipment_catalog_item(40076);
    boots = vm_net_mock_find_equipment_catalog_item(16402);
    skill = vm_net_mock_find_skill_catalog_item(skillId);
    if (staff == NULL || helmet == NULL || boots == NULL || skill == NULL ||
        staff->slot != 0 || helmet->slot != 1 || boots->slot != 6 ||
        staff->bonus.wisdom != 26 || helmet->bonus.wisdom != 382 ||
        boots->bonus.wisdom != 21 || skill->wisdomCoeff != 110 ||
        skill->targetDirection != 3 || skill->hpChange >= 0)
    {
        fputs("player-3 wisdom fixture no longer matches its catalog contract\n", stderr);
        return 1;
    }

    equip_catalog_item(role, staff);
    equip_catalog_item(role, helmet);
    vm_net_mock_role_build_player_stats_impl(role, &withoutBoots, true, false);
    rawWithoutBoots = vm_net_mock_battle_skill_raw_damage_from_stats(
        skill, &withoutBoots);
    damageWithoutBoots = vm_net_mock_damage_after_defense(rawWithoutBoots,
                                                           enemyDefense);
    if (damageWithoutBoots < (u32)(0 - skill->hpChange))
        damageWithoutBoots = (u32)(0 - skill->hpChange);

    equip_catalog_item(role, boots);
    vm_net_mock_role_build_player_stats_impl(role, &withBoots, true, false);
    rawWithBoots = vm_net_mock_battle_skill_raw_damage_from_stats(skill, &withBoots);
    damageWithBoots = vm_net_mock_damage_after_defense(rawWithBoots, enemyDefense);
    if (damageWithBoots < (u32)(0 - skill->hpChange))
        damageWithBoots = (u32)(0 - skill->hpChange);

    if (withoutBoots.wisdom != 1251 || withBoots.wisdom != 1272 ||
        withBoots.wisdom != withoutBoots.wisdom + boots->bonus.wisdom ||
        rawWithBoots <= rawWithoutBoots || damageWithBoots <= damageWithoutBoots)
    {
        fprintf(stderr,
                "primary-stat uncap contract failed: wisdom=%u->%u raw=%u->%u "
                "damage=%u->%u\n",
                withoutBoots.wisdom, withBoots.wisdom,
                rawWithoutBoots, rawWithBoots,
                damageWithoutBoots, damageWithBoots);
        return 1;
    }

    printf("primary-stat uncap regression passed wisdom=%u->%u raw=%u->%u "
           "damage=%u->%u enemy_defense=%u\n",
           withoutBoots.wisdom, withBoots.wisdom,
           rawWithoutBoots, rawWithBoots,
           damageWithoutBoots, damageWithBoots, enemyDefense);
    return 0;
}
