/*
 * Pure regression for the battle-wear state boundary.  It does not start a
 * listener, access MySQL or open the client.  The production terminal owner
 * calls vm_net_mock_role_service_apply_battle_wear(), whose persistence and
 * per-session guard wrap the pure instance mutation tested here.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_service_state state;
    u32 worn = 0;

    memset(&role, 0, sizeof(role));
    memset(&state, 0, sizeof(state));
    role.roleId = 810002;
    role.job = 1;
    role.exp = 0;

    /* 1001 is the real equip.dsh wooden sword (slot 0, max durability 50). */
    role.equippedItems[0].itemId = 1001;
    role.equippedItems[0].durability = 44;
    role.equippedItems[0].durabilityMax = 50;
    state.used = true;
    state.roleId = role.roleId;
    state.equipmentItemIds[0] = 1001;
    state.durability[0] = 44;
    state.durabilityMax[0] = 50;

    /* A broken item, a wrong-slot item and an empty slot must not wear. */
    role.equippedItems[1].itemId = 1001;
    role.equippedItems[1].durability = 12;
    role.equippedItems[1].durabilityMax = 50;
    role.equippedItems[2].durability = 0;

    worn = vm_net_mock_role_service_wear_equipment_instances(&role, &state);
    if (worn != 1 || role.equippedItems[0].durability != 43 ||
        state.durability[0] != 43 || role.equippedItems[1].durability != 12 ||
        role.equippedItems[2].durability != 0)
    {
        fputs("battle equipment durability wear contract failed\n", stderr);
        return 1;
    }

    /* The helper is monotonic and clamps at zero. */
    role.equippedItems[0].durability = 1;
    state.durability[0] = 1;
    worn = vm_net_mock_role_service_wear_equipment_instances(&role, &state);
    if (worn != 1 || role.equippedItems[0].durability != 0 ||
        state.durability[0] != 0)
    {
        fputs("battle equipment durability zero clamp failed\n", stderr);
        return 1;
    }
    worn = vm_net_mock_role_service_wear_equipment_instances(&role, &state);
    if (worn != 0 || role.equippedItems[0].durability != 0)
    {
        fputs("battle equipment durability repeated zero wear failed\n", stderr);
        return 1;
    }

    puts("battle-equipment-durability-v1 passed: usable=44->43->0 broken/wrong-slot unchanged");
    return 0;
}
