/*
 * Server-only regression for removal of the old derived-combat-stat 9999
 * ceiling.  A synthetic, valid equipment-catalog row supplies values above
 * that former boundary; no port, MySQL connection, or player save is used.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    const u32 roleId = 920003;
    const u32 itemId = 990001;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_equipment_catalog_item *item = NULL;
    vm_net_mock_player_stats stats;
    vm_net_mock_actorinfo_status_fields fields;

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
    role->job = 3;

    memset(g_vm_net_mock_equipment_catalog, 0,
           sizeof(g_vm_net_mock_equipment_catalog));
    g_vm_net_mock_equipment_catalog_count = 1;
    g_vm_net_mock_equipment_catalog_loaded = true;
    item = &g_vm_net_mock_equipment_catalog[0];
    item->itemId = itemId;
    item->slot = 0;
    item->levelRequired = 1;
    item->quality = 0;
    item->category = 9;
    item->durabilityMax = 1;
    item->bonus.hp = 10000;
    item->bonus.mp = 10000;
    item->bonus.attack = 10000;
    item->bonus.armor = 10000;
    item->bonus.strength = 10000;
    item->bonus.agility = 10000;
    item->bonus.wisdom = 10000;
    item->bonus.crit = 10000;
    item->bonus.hit = 10000;
    item->bonus.dodge = 10000;
    item->bonus.resist = 10000;
    role->equippedItems[0].itemId = itemId;
    role->equippedItems[0].durability = 1;
    role->equippedItems[0].durabilityMax = 1;

    vm_net_mock_role_build_player_stats_impl(role, &stats, true, false);
    vm_net_mock_build_actorinfo_status_fields(&stats, &fields);

    if (stats.maxHp <= 9999 || stats.maxMp <= 9999 ||
        stats.attack <= 9999 || stats.defense <= 9999 ||
        stats.hit <= 9999 || stats.dodge <= 9999 ||
        stats.crit <= 9999 || stats.resist <= 9999 ||
        fields.words[2] != stats.hit || fields.words[4] != stats.defense ||
        fields.words[5] != stats.resist ||
        vm_net_mock_battle_apply_signed_stat_change(9999, 1) != 10000 ||
        vm_net_mock_mul_div_u32_saturating(10000, 1100, 100) != 110000)
    {
        fprintf(stderr,
                "derived-stat uncap contract failed: hp=%u mp=%u atk=%u def=%u "
                "hit=%u dodge=%u crit=%u resist=%u wire=%u/%u/%u\n",
                stats.maxHp, stats.maxMp, stats.attack, stats.defense,
                stats.hit, stats.dodge, stats.crit, stats.resist,
                fields.words[2], fields.words[4], fields.words[5]);
        return 1;
    }

    printf("derived-stat uncap regression passed hp=%u mp=%u atk=%u def=%u "
           "hit=%u dodge=%u crit=%u resist=%u\n",
           stats.maxHp, stats.maxMp, stats.attack, stats.defense,
           stats.hit, stats.dodge, stats.crit, stats.resist);
    return 0;
}
