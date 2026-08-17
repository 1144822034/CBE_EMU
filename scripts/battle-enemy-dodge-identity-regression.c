/*
 * Pure regression for deterministic monster hit rolls in a three-monster
 * battle.  It does not start a listener, access MySQL or open the client.
 *
 * The native 4/6 actioninfo contract is already established in
 * docs/re/2026-08-12-battle-secondary-stat-settlement.md: a miss is a
 * zero-damage child with child_flag=3.  This test isolates the earlier server
 * boundary, where identical monster-type ids incorrectly gave all three
 * action children the same hit result.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_net_mock_monster_stats monster;
    vm_net_mock_player_stats player;
    static const u8 enemyWires[3] = { 2, 3, 0 };
    bool hits[3] = {false, false, false};
    u32 salts[3] = {0, 0, 0};
    u32 selectedSession = 0;

    memset(&monster, 0, sizeof(monster));
    memset(&player, 0, sizeof(player));
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    monster.enemyId = 105;
    monster.level = 1;
    /* 79 / (79 + 118) rounds to the documented 40% minimum hit rate. */
    player.dodge = 118;
    g_mockBattleOperateTurnCounter = 7;
    g_vm_net_mock_battle_role_id_current = 810001;

    for (u32 session = 1; session != 100000u; ++session)
    {
        g_mockBattleOperateSessionSerial = session;
        for (u8 i = 0; i < 3; ++i)
        {
            salts[i] = vm_net_mock_battle_enemy_attack_roll_salt(
                monster.enemyId, enemyWires[i]);
            hits[i] = vm_net_mock_battle_enemy_attack_hits(
                &monster, &player, salts[i]);
        }
        /* Reproduce the reported shape: the first monster dodges while the
         * next one hits in the very same session/turn. */
        if (!hits[0] && hits[1])
        {
            selectedSession = session;
            break;
        }
    }

    if (selectedSession == 0 || salts[0] == salts[1] ||
        salts[1] == salts[2] || salts[0] == salts[2] ||
        vm_net_mock_battle_enemy_attack_hits(&monster, &player, salts[0]) != hits[0] ||
        vm_net_mock_battle_enemy_attack_hits(&monster, &player, salts[1]) != hits[1] ||
        vm_net_mock_battle_enemy_attack_hits(&monster, &player, salts[2]) != hits[2])
    {
        fputs("three-monster deterministic dodge identity contract failed\n", stderr);
        return 1;
    }

    printf("battle-enemy-dodge-identity-v1 passed: session=%u turn=%u "
           "wires=%u/%u/%u hit=%u/%u/%u actioninfo=%u/%u/%u\n",
           selectedSession, g_mockBattleOperateTurnCounter,
           enemyWires[0], enemyWires[1], enemyWires[2],
           hits[0] ? 1u : 0u, hits[1] ? 1u : 0u, hits[2] ? 1u : 0u,
           hits[0] ? 0u : VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE,
           hits[1] ? 0u : VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE,
           hits[2] ? 0u : VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE);
    return 0;
}
