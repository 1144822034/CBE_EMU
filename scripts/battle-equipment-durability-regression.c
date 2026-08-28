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

static u32 read_be32(const u8 *value)
{
    return ((u32)value[0] << 24) | ((u32)value[1] << 16) |
           ((u32)value[2] << 8) | (u32)value[3];
}

static bool read_durability_count11_object(const u8 *packet, u32 packetLen,
                                           u16 expectedSeq, u32 expectedDurability)
{
    const u8 *info = NULL;
    u32 offset = 5;
    u32 infoOffset = 0;
    u16 objectLen = 0;
    u16 infoLen = 0;

    if (packet == NULL || packetLen < 5 + 6 || packet[0] != 'W' ||
        packet[1] != 'T' || packet[4] != 1 || packet[offset] != 1 ||
        packet[offset + 1] != 7 || packet[offset + 2] != 11)
    {
        return false;
    }
    objectLen = (u16)(((u16)packet[offset + 4] << 8) | packet[offset + 5]);
    offset += 6;
    if (objectLen < 6 || offset > packetLen || objectLen - 6 > packetLen - offset ||
        packet[offset] != 4 || memcmp(packet + offset + 1, "info", 4) != 0)
    {
        return false;
    }
    offset += 5;
    if (offset + 2 > packetLen)
        return false;
    infoLen = (u16)(((u16)packet[offset] << 8) | packet[offset + 1]);
    offset += 2;
    if (infoLen != 13 || offset + infoLen != 5 + objectLen ||
        offset + infoLen != packetLen)
    {
        return false;
    }
    info = packet + offset;
    if (info[infoOffset] != 0 || info[infoOffset + 1] != 1 ||
        info[infoOffset + 2] != 1)
    {
        return false;
    }
    infoOffset += 3;
    if (info[infoOffset] != 0 || info[infoOffset + 1] != 2 ||
        (u16)(((u16)info[infoOffset + 2] << 8) | info[infoOffset + 3]) != expectedSeq)
    {
        return false;
    }
    infoOffset += 4;
    return info[infoOffset] == 0 && info[infoOffset + 1] == 4 &&
           read_be32(info + infoOffset + 2) == expectedDurability;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_service_state state;
    u8 bootstrapItemInfo[256];
    u8 durabilityRefreshPacket[128];
    u32 bootstrapItemInfoLen = 0;
    u32 durabilityRefreshPacketLen = 5;
    u8 bootstrapRows = 0;
    u8 durabilityRefreshRows = 0;
    bool durabilityRefreshAppended = false;
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

    /* 7/11 is the only verified in-session update path.  Its category-15
     * branch writes item+272, rather than inserting a second equipment row. */
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = role.roleId;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0] = role;
    memset(durabilityRefreshPacket, 0, sizeof(durabilityRefreshPacket));
    if (!vm_net_mock_append_battle_equipment_durability_counts_object(
            durabilityRefreshPacket, sizeof(durabilityRefreshPacket),
            &durabilityRefreshPacketLen, &durabilityRefreshAppended,
            &durabilityRefreshRows) || !durabilityRefreshAppended ||
        durabilityRefreshRows != 1)
    {
        fputs("battle equipment durability 7/11 refresh was not built\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(durabilityRefreshPacket,
                                 durabilityRefreshPacketLen, 1);
    if (!read_durability_count11_object(durabilityRefreshPacket,
                                        durabilityRefreshPacketLen, 1, 43))
    {
        fputs("battle equipment durability 7/11 refresh fields are invalid\n", stderr);
        return 1;
    }

    /* The next client-owned 7/7 type-2 bootstrap reads the same current
     * durability field.  This does not claim an in-session refresh path: it
     * protects the already verified rebootstrap contract without emitting an
     * unproven object from the battle-close callback. */
    if (!vm_net_mock_build_equipment_login_iteminfo_blob(
            bootstrapItemInfo, sizeof(bootstrapItemInfo), &role,
            &bootstrapItemInfoLen, &bootstrapRows) ||
        bootstrapRows != 1 || bootstrapItemInfoLen < 19 ||
        bootstrapItemInfo[0] != 0 || bootstrapItemInfo[1] != 1 ||
        bootstrapItemInfo[2] != 1 ||
        bootstrapItemInfo[3] != 0 || bootstrapItemInfo[4] != 2 ||
        bootstrapItemInfo[5] != 0 || bootstrapItemInfo[6] != 1 ||
        bootstrapItemInfo[7] != 0 || bootstrapItemInfo[8] != 4 ||
        read_be32(&bootstrapItemInfo[9]) != 1001 ||
        bootstrapItemInfo[13] != 0 || bootstrapItemInfo[14] != 4 ||
        read_be32(&bootstrapItemInfo[15]) != 43)
    {
        fputs("battle equipment durability bootstrap current-count failed\n", stderr);
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

    puts("battle-equipment-durability-v3 passed: usable=44->43->0 battle-7/11=43 bootstrap=43 broken/wrong-slot unchanged");
    return 0;
}
