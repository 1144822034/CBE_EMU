/*
 * Deterministic server-only regression for the special-title equipment-set
 * contract.  It reads the shipped equip.dsh directory but never starts a
 * listener or connects to MySQL.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool equip_special_title_set(vm_net_mock_role_state *role, u32 setId)
{
    static const u32 christmasBySlot[VM_NET_MOCK_EQUIP_SLOT_COUNT] = {
        40020, 40025, 40023, 40028, 40027, 40024, 40026, 40029,
    };
    static const u32 wulinBySlot[VM_NET_MOCK_EQUIP_SLOT_COUNT] = {
        40150, 40155, 40153, 40158, 40157, 40154, 40156, 40159,
    };
    const u32 *items = NULL;

    if (role == NULL)
        return false;
    if (setId == VM_NET_MOCK_DESIGNATION_EQUIPMENT_SET_CHRISTMAS)
        items = christmasBySlot;
    else if (setId == VM_NET_MOCK_DESIGNATION_EQUIPMENT_SET_WULIN)
        items = wulinBySlot;
    else
        return false;
    memset(role->equippedItems, 0, sizeof(role->equippedItems));
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        const vm_net_mock_equipment_catalog_item *item =
            vm_net_mock_find_equipment_catalog_item(items[slot]);

        if (item == NULL || item->slot != slot)
            return false;
        role->equippedItems[slot].itemId = item->itemId;
        role->equippedItems[slot].durability = item->durabilityMax;
        role->equippedItems[slot].durabilityMax = item->durabilityMax;
    }
    return true;
}

static int expect_set_unlock(u32 setId, u8 designationId,
                             u8 otherDesignationId)
{
    vm_net_mock_role_state role;
    const vm_net_mock_designation_entry *designation =
        vm_net_mock_designation_by_id(designationId);
    const vm_net_mock_designation_entry *other =
        vm_net_mock_designation_by_id(otherDesignationId);

    memset(&role, 0, sizeof(role));
    role.level = 65;
    role.exp = vm_net_mock_role_level_start_exp(role.level);
    role.job = 1;
    if (designation == NULL || other == NULL ||
        !equip_special_title_set(&role, setId) ||
        !vm_net_mock_designation_is_unlocked(&role, designation) ||
        vm_net_mock_designation_is_unlocked(&role, other))
    {
        fprintf(stderr, "special-title set unlock mismatch set=%u title=%u\n",
                setId, designationId);
        return 1;
    }
    /* A full set in the backpack is not a worn set. */
    role.equippedItems[5].itemId = 0;
    role.backpackItemCount = 1;
    role.backpackItems[0].itemId =
        setId == VM_NET_MOCK_DESIGNATION_EQUIPMENT_SET_CHRISTMAS ?
            40024 : 40154;
    role.backpackItems[0].count = 1;
    if (vm_net_mock_designation_is_unlocked(&role, designation))
    {
        fprintf(stderr, "backpack item unlocked special title set=%u\n", setId);
        return 1;
    }
    if (!equip_special_title_set(&role, setId))
        return 1;
    /* A valid item from the other set cannot replace one required slot. */
    role.equippedItems[3].itemId =
        setId == VM_NET_MOCK_DESIGNATION_EQUIPMENT_SET_CHRISTMAS ?
            40158 : 40028;
    if (vm_net_mock_designation_is_unlocked(&role, designation))
    {
        fprintf(stderr, "mixed equipment unlocked special title set=%u\n", setId);
        return 1;
    }
    if (!equip_special_title_set(&role, setId))
        return 1;
    /* Durability affects combat bonuses, but a broken item is still worn. */
    role.equippedItems[6].durability = 0;
    if (!vm_net_mock_designation_is_unlocked(&role, designation))
    {
        fprintf(stderr, "broken but equipped set lost special title set=%u\n",
                setId);
        return 1;
    }
    return 0;
}

int main(void)
{
    vm_net_mock_designation_config *christmas = NULL;
    vm_net_mock_designation_config *wulin = NULL;

    if (vm_net_mock_load_equipment_catalog() == 0)
    {
        fputs("unable to load shipped equip.dsh\n", stderr);
        return 1;
    }
    vm_net_mock_designation_config_reset_to_defaults();
    g_vm_net_mock_designation_config_db_loaded = true;
    g_vm_net_mock_designation_config_db_valid = true;
    christmas = vm_net_mock_designation_config_by_id(33);
    wulin = vm_net_mock_designation_config_by_id(34);
    if (christmas == NULL || wulin == NULL ||
        christmas->conditionKind !=
            VM_NET_MOCK_DESIGNATION_CONDITION_EQUIPMENT_SET ||
        christmas->conditionValue !=
            VM_NET_MOCK_DESIGNATION_EQUIPMENT_SET_CHRISTMAS ||
        wulin->conditionKind !=
            VM_NET_MOCK_DESIGNATION_CONDITION_EQUIPMENT_SET ||
        wulin->conditionValue != VM_NET_MOCK_DESIGNATION_EQUIPMENT_SET_WULIN)
    {
        fputs("special-title equipment defaults are invalid\n", stderr);
        return 1;
    }
    christmas->enabled = 1;
    wulin->enabled = 1;
    if (expect_set_unlock(VM_NET_MOCK_DESIGNATION_EQUIPMENT_SET_CHRISTMAS,
                          33, 34) != 0 ||
        expect_set_unlock(VM_NET_MOCK_DESIGNATION_EQUIPMENT_SET_WULIN,
                          34, 33) != 0)
    {
        return 1;
    }
    puts("designation equipment-set regression passed: Christmas/Wulin full worn sets only");
    return 0;
}
