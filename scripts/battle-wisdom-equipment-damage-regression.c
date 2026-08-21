/*
 * Server-only regression for the "wisdom staff appears to deal the same
 * damage" observation.  It loads the shipped catalog, equips staff 12013,
 * and compares skill 201 (wire Operate=203) before and after equipment.
 *
 * The battle wire cannot report more damage than a target's current HP.  The
 * fixture therefore proves both parts of the contract: raw wisdom-scaled
 * damage rises, while the visible result remains 609 against a 609-HP target
 * because that cast is a one-hit kill in both states.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    const u32 roleId = 920001;
    const u32 skillId = 201;
    const u32 operate = skillId + 2;
    const u32 enemyId = 30;
    const u32 lowEnemyHp = 609;
    const vm_net_mock_equipment_catalog_item *staff = NULL;
    const vm_net_mock_skill_catalog_item *skill = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_player_stats beforeStats;
    vm_net_mock_player_stats afterStats;
    u32 rawBefore = 0;
    u32 rawAfter = 0;
    u32 finalBefore = 0;
    u32 finalAfter = 0;
    u32 highTargetBefore = 0;
    u32 highTargetAfter = 0;

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
    role->job = 3; /* rawJob=2 in skill.dsh is the staff profession. */

    staff = vm_net_mock_find_equipment_catalog_item(12013);
    skill = vm_net_mock_find_skill_catalog_item(skillId);
    if (staff == NULL || skill == NULL || staff->slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
        staff->bonus.wisdom == 0 || skill->wisdomCoeff == 0 ||
        skill->targetDirection != 3 || skill->hpChange >= 0)
    {
        fputs("staff 12013 or skill 201 does not match the wisdom-damage contract\n",
              stderr);
        return 1;
    }

    vm_net_mock_role_build_player_stats(role, &beforeStats);
    rawBefore = vm_net_mock_battle_skill_raw_damage_from_stats(skill, &beforeStats);
    finalBefore = vm_net_mock_battle_player_skill_damage_to_enemy(
        operate, enemyId, lowEnemyHp, 0x201u, NULL, NULL);
    highTargetBefore = vm_net_mock_battle_player_skill_damage_to_enemy(
        operate, enemyId, 9999, 0x201u, NULL, NULL);

    role->equippedItems[staff->slot].itemId = staff->itemId;
    role->equippedItems[staff->slot].durability = staff->durabilityMax;
    role->equippedItems[staff->slot].durabilityMax = staff->durabilityMax;
    vm_net_mock_role_build_player_stats(role, &afterStats);
    rawAfter = vm_net_mock_battle_skill_raw_damage_from_stats(skill, &afterStats);
    finalAfter = vm_net_mock_battle_player_skill_damage_to_enemy(
        operate, enemyId, lowEnemyHp, 0x201u, NULL, NULL);
    highTargetAfter = vm_net_mock_battle_player_skill_damage_to_enemy(
        operate, enemyId, 9999, 0x201u, NULL, NULL);

    if (afterStats.wisdom <= beforeStats.wisdom || rawAfter <= rawBefore ||
        highTargetAfter <= highTargetBefore || finalBefore != lowEnemyHp ||
        finalAfter != lowEnemyHp)
    {
        fprintf(stderr,
                "wisdom-damage contract failed: wisdom=%u->%u raw=%u->%u "
                "high-target=%u->%u low-target=%u->%u\n",
                beforeStats.wisdom, afterStats.wisdom, rawBefore, rawAfter,
                highTargetBefore, highTargetAfter, finalBefore, finalAfter);
        return 1;
    }

    printf("wisdom equipment damage regression passed staff=%u wisdom=%u->%u "
           "skill=%u coeff=%u raw=%u->%u target9999=%u->%u target609=%u->%u\n",
           staff->itemId, beforeStats.wisdom, afterStats.wisdom,
           skill->skillId, skill->wisdomCoeff, rawBefore, rawAfter,
           highTargetBefore, highTargetAfter, finalBefore, finalAfter);
    return 0;
}
