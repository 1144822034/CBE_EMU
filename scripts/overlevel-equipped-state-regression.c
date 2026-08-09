/*
 * Regression for the persisted over-level equipped-item repair.
 *
 * The test includes the real server implementation but does not start its
 * listener, connect to MySQL or touch an account.  It proves the load-time
 * repair's three ownership cases:
 *   1. a level-34 role has a level-70 durable item returned to its backpack;
 *   2. a level-70 role keeps the same item equipped; and
 *   3. a full backpack leaves the invalid source state untouched (no loss or
 *      partial migration) and reports it as blocked.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static void setup_overlevel_wand(vm_net_mock_role_state *role)
{
    memset(role, 0, sizeof(*role));
    role->roleId = 70034;
    role->exp = vm_net_mock_role_level_start_exp(34);
    role->level = 34;
    role->backpackCapacity = 64;
    role->nextBackpackSeq = VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST;
    /* equip.dsh: 40067 belongs in slot 0 and requires level 70. */
    role->equippedItems[0].itemId = 40067;
    role->equippedItems[0].durability = 107;
    role->equippedItems[0].durabilityMax = 480;
    role->equippedItems[0].enhanceLevel = 4;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_state before;
    u8 equipmentBlob[VM_NET_MOCK_EQUIPMENT_LOGIN_ITEMINFO_MAX_BYTES];
    u32 equipmentBlobLen = 0;
    u8 equipmentRows = 0;
    u32 moved = 0;
    u32 blocked = 0;

    setup_overlevel_wand(&role);
    if (!vm_net_mock_role_recover_overlevel_equipment(&role, &moved, &blocked) ||
        moved != 1 || blocked != 0 || role.equippedItems[0].itemId != 0 ||
        role.backpackItemCount != 1 || role.backpackItems[0].itemId != 40067 ||
        role.backpackItems[0].durability != 107 ||
        role.backpackItems[0].durabilityMax != 480 ||
        role.backpackItems[0].enhanceLevel != 4 || role.backpackItems[0].count != 1)
    {
        fputs("overlevel equipment was not preserved into the backpack\n", stderr);
        return 1;
    }

    setup_overlevel_wand(&role);
    role.roleId = 70070;
    role.exp = vm_net_mock_role_level_start_exp(70);
    role.level = 70;
    moved = 99;
    blocked = 99;
    if (vm_net_mock_role_recover_overlevel_equipment(&role, &moved, &blocked) ||
        moved != 0 || blocked != 0 || role.equippedItems[0].itemId != 40067 ||
        role.backpackItemCount != 0)
    {
        fputs("legal level-70 equipment was incorrectly removed\n", stderr);
        return 1;
    }

    setup_overlevel_wand(&role);
    role.backpackCapacity = 1;
    role.backpackItemCount = 1;
    role.backpackItems[0].itemId = 1001;
    role.backpackItems[0].seq = VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST;
    role.backpackItems[0].count = 1;
    role.backpackItems[0].durability = 50;
    role.backpackItems[0].durabilityMax = 50;
    role.nextBackpackSeq = VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST + 1;
    before = role;
    moved = 0;
    blocked = 0;
    if (vm_net_mock_role_recover_overlevel_equipment(&role, &moved, &blocked) ||
        moved != 0 || blocked != 1 || memcmp(&role, &before, sizeof(role)) != 0)
    {
        fputs("full-backpack path did not preserve the original equipped state\n", stderr);
        return 1;
    }
    memset(equipmentBlob, 0, sizeof(equipmentBlob));
    if (!vm_net_mock_build_equipment_login_iteminfo_blob(
            equipmentBlob, sizeof(equipmentBlob), &role,
            &equipmentBlobLen, &equipmentRows) ||
        equipmentRows != 0 || equipmentBlobLen == 0 || equipmentBlob[0] != 0)
    {
        fputs("blocked invalid equipment leaked into the client equipment bootstrap\n", stderr);
        return 1;
    }

    puts("overlevel equipped-state regression passed: migrate, legal retain, full-backpack quarantine");
    return 0;
}
