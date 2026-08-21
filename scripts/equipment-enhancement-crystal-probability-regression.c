/*
 * Deterministic server-only regression for the crystal-grade probability
 * curve.  It does not start a listener or contact MySQL: it builds the real
 * 29/1, 29/2 and 29/3 packets against an isolated in-memory role.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static u32 g_saveCalls;
static const char *g_saveReason;

static bool save_fixture(const char *reason)
{
    ++g_saveCalls;
    g_saveReason = reason;
    return true;
}

static bool begin_field(u8 *out, u32 outCap, u32 *pos, const char *name,
                        const u8 *value, u16 valueLen)
{
    size_t nameLen = name ? strlen(name) : 0;

    if (out == NULL || pos == NULL || nameLen == 0 || nameLen > 255 ||
        value == NULL || *pos + 1u + nameLen + 2u + valueLen > outCap)
    {
        return false;
    }
    out[(*pos)++] = (u8)nameLen;
    memcpy(out + *pos, name, nameLen);
    *pos += (u32)nameLen;
    out[(*pos)++] = (u8)(valueLen >> 8);
    out[(*pos)++] = (u8)valueLen;
    memcpy(out + *pos, value, valueLen);
    *pos += valueLen;
    return true;
}

static u32 build_enhance_request(u8 *out, u32 outCap, u8 subtype,
                                 u16 equipSeq, u32 crystalId, u8 crystalRows)
{
    u8 taggedSeq[4] = {0, 2, (u8)(equipSeq >> 8), (u8)equipSeq};
    u8 occult[45];
    u32 pos = 9;
    u32 objectLen = 0;

    if (out == NULL || (subtype != 1 && (crystalRows == 0 || crystalRows > 5)))
        return 0;
    memset(out, 0, outCap);
    out[0] = 'W';
    out[1] = 'T';
    out[4] = 1;
    out[5] = 29;
    out[6] = subtype;
    if (subtype == 1)
    {
        u8 taggedU32[6] = {0, 4, 0, 0, (u8)(equipSeq >> 8), (u8)equipSeq};

        if (!begin_field(out, outCap, &pos, "seq", taggedU32,
                         sizeof(taggedU32)))
        {
            return 0;
        }
    }
    else
    {
        if (!begin_field(out, outCap, &pos, "equipseq", taggedSeq,
                         sizeof(taggedSeq)))
        {
            return 0;
        }
        for (u8 row = 0; row < crystalRows; ++row)
        {
            u32 rowOffset = (u32)row * 9u;

            occult[rowOffset] = 0;
            occult[rowOffset + 1] = 4;
            occult[rowOffset + 2] = (u8)(crystalId >> 24);
            occult[rowOffset + 3] = (u8)(crystalId >> 16);
            occult[rowOffset + 4] = (u8)(crystalId >> 8);
            occult[rowOffset + 5] = (u8)crystalId;
            occult[rowOffset + 6] = 0;
            occult[rowOffset + 7] = 1;
            occult[rowOffset + 8] = 1;
        }
        if (!begin_field(out, outCap, &pos, "occultinfo", occult,
                         (u16)((u32)crystalRows * 9u)))
        {
            return 0;
        }
    }
    objectLen = pos - 4u;
    if (objectLen > 0xffffu || pos > 0xffffu)
        return 0;
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[7] = (u8)(objectLen >> 8);
    out[8] = (u8)objectLen;
    return pos;
}

static bool response_object(const u8 *packet, u32 packetLen,
                            vm_net_mock_request_object *objectOut)
{
    u16 objectLen = 0;

    if (packet == NULL || objectOut == NULL || packetLen < 11 ||
        packet[0] != 'W' || packet[1] != 'T' || packet[4] != 1 ||
        packet[5] != 1 || packet[6] != 29)
    {
        return false;
    }
    objectLen = (u16)(((u16)packet[9] << 8) | packet[10]);
    if (objectLen < 6 || 5u + objectLen != packetLen)
        return false;
    objectOut->major = packet[5];
    objectOut->kind = packet[6];
    objectOut->subtype = packet[7];
    objectOut->payload = packet + 11;
    objectOut->payloadLen = (u16)(objectLen - 6u);
    return true;
}

static bool response_u32(const u8 *packet, u32 packetLen, const char *field,
                         u32 expected)
{
    vm_net_mock_request_object object;
    u32 value = 0;

    return response_object(packet, packetLen, &object) &&
           vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                            field, &value) &&
           value == expected;
}

static bool response_u8(const u8 *packet, u32 packetLen, const char *field,
                        u8 expected)
{
    vm_net_mock_request_object object;
    u8 value = 0;

    return response_object(packet, packetLen, &object) &&
           vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                           field, &value) &&
           value == expected;
}

static bool response_sequence_u32(const u8 *packet, u32 packetLen,
                                  const char *field, u32 index, u32 expected)
{
    vm_net_mock_request_object object;
    const u8 *raw = NULL;
    u16 rawLen = 0;
    u32 offset = index * 6u;
    u32 value = 0;

    if (!response_object(packet, packetLen, &object) ||
        !vm_net_mock_get_object_entry_bytes(object.payload, object.payloadLen,
                                            field, &raw, &rawLen) ||
        raw == NULL || offset + 6u > rawLen || raw[offset] != 0 ||
        raw[offset + 1] != 4)
    {
        return false;
    }
    value = ((u32)raw[offset + 2] << 24) |
            ((u32)raw[offset + 3] << 16) |
            ((u32)raw[offset + 4] << 8) | raw[offset + 5];
    return value == expected;
}

static void seed_catalog_and_role(u32 crystalId, u8 crystalCount)
{
    vm_net_mock_role_state *role = NULL;

    memset(g_vm_net_mock_equipment_catalog, 0,
           sizeof(g_vm_net_mock_equipment_catalog));
    g_vm_net_mock_equipment_catalog_loaded = true;
    g_vm_net_mock_equipment_catalog_count = 1;
    g_vm_net_mock_equipment_catalog[0].itemId = 1001;
    g_vm_net_mock_equipment_catalog[0].slot = 0;
    g_vm_net_mock_equipment_catalog[0].levelRequired = 1;
    g_vm_net_mock_equipment_catalog[0].durabilityMax = 100;
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 830001;
    role = &g_vm_net_mock_role_db.roles[0];
    role->roleId = 830001;
    role->level = 70;
    role->money = 20000;
    role->backpackCapacity = 64;
    role->backpackItemCount = 2;
    role->nextBackpackSeq = 19;
    role->backpackItems[0].itemId = 1001;
    role->backpackItems[0].seq = 17;
    role->backpackItems[0].count = 1;
    role->backpackItems[0].enhanceLevel = 15;
    role->backpackItems[0].durability = 100;
    role->backpackItems[0].durabilityMax = 100;
    role->backpackItems[1].itemId = crystalId;
    role->backpackItems[1].seq = 18;
    role->backpackItems[1].count = crystalCount;
    vm_net_mock_role_normalize_backpack(role);
}

static int assert_curve(void)
{
    u32 level6Five = 5u * vm_net_mock_equipment_enhance_crystal_power(6);
    u32 level14One = vm_net_mock_equipment_enhance_crystal_power(14);
    u32 level15One = vm_net_mock_equipment_enhance_crystal_power(15);
    u32 level16One = vm_net_mock_equipment_enhance_crystal_power(16);

    if (vm_net_mock_equipment_enhance_required_power(15) != 14348907u ||
        level16One != 14348907u || level15One * 3u != level16One ||
        level14One * 9u != level16One ||
        vm_net_mock_equipment_enhance_success_rate(0,
            vm_net_mock_equipment_enhance_crystal_power(1)) != 100u ||
        vm_net_mock_equipment_enhance_success_rate(15, level6Five) != 0u ||
        vm_net_mock_equipment_enhance_success_rate(15, level14One) != 11u ||
        vm_net_mock_equipment_enhance_success_rate(15, level15One) != 33u ||
        vm_net_mock_equipment_enhance_success_rate(15, level16One) != 100u)
    {
        fputs("crystal probability curve changed\n", stderr);
        return 1;
    }
    return 0;
}

static int assert_protocol_uses_curve(void)
{
    u8 request[160];
    u8 response[4096];
    u32 requestLen = 0;
    u32 responseLen = 0;
    vm_net_mock_role_state *role = NULL;

    seed_catalog_and_role(915, 1); /* one level-15 crystal */
    requestLen = build_enhance_request(request, sizeof(request), 1, 17, 0, 0);
    responseLen = vm_net_mock_build_equipment_enhance_response_with_save_callback(
        request, requestLen, response, sizeof(response), save_fixture);
    if (requestLen == 0 || responseLen == 0 || !response_u8(response, responseLen,
        "result", 1) || !response_sequence_u32(response, responseLen, "data1", 15,
        14348907u) || !response_sequence_u32(response, responseLen, "data2", 5, 243u))
    {
        fputs("29/1 did not publish the one-third requirement/power tables\n", stderr);
        return 1;
    }

    requestLen = build_enhance_request(request, sizeof(request), 2, 17, 915, 1);
    responseLen = vm_net_mock_build_equipment_enhance_response_with_save_callback(
        request, requestLen, response, sizeof(response), save_fixture);
    if (requestLen == 0 || responseLen == 0 || !response_u8(response, responseLen,
        "result", 1) || !response_u32(response, responseLen, "value", 33u) ||
        !response_u32(response, responseLen, "money", 1600u))
    {
        fputs("29/2 did not preview one level-fifteen crystal as 33% at +15\n",
              stderr);
        return 1;
    }

    role = &g_vm_net_mock_role_db.roles[0];
    for (g_schedulerTick = 0; g_schedulerTick < 1000u; ++g_schedulerTick)
    {
        if (vm_net_mock_equipment_enhance_roll(
                17, 15, vm_net_mock_equipment_enhance_required_power(15)) >=
            vm_net_mock_equipment_enhance_crystal_power(15))
        {
            break;
        }
    }
    if (g_schedulerTick == 1000u)
    {
        fputs("could not schedule a failing one-third enhancement roll\n", stderr);
        return 1;
    }
    g_saveCalls = 0;
    g_saveReason = NULL;
    requestLen = build_enhance_request(request, sizeof(request), 3, 17, 915, 1);
    responseLen = vm_net_mock_build_equipment_enhance_response_with_save_callback(
        request, requestLen, response, sizeof(response), save_fixture);
    if (requestLen == 0 || responseLen == 0 || !response_u8(response, responseLen,
        "result", 2) || g_saveCalls != 1 || g_saveReason == NULL ||
        strcmp(g_saveReason, "equipment-enhance-failed") != 0 ||
        role->backpackItems[0].enhanceLevel != 15 ||
        role->backpackItems[1].count != 0 || role->money != 18400u)
    {
        fputs("29/3 did not apply the same one-third +15 probability\n",
              stderr);
        return 1;
    }

    seed_catalog_and_role(915, 1);
    role = &g_vm_net_mock_role_db.roles[0];
    for (g_schedulerTick = 0; g_schedulerTick < 1000u; ++g_schedulerTick)
    {
        if (vm_net_mock_equipment_enhance_roll(
                17, 15, vm_net_mock_equipment_enhance_required_power(15)) <
            vm_net_mock_equipment_enhance_crystal_power(15))
        {
            break;
        }
    }
    if (g_schedulerTick == 1000u)
    {
        fputs("could not schedule a successful one-third enhancement roll\n", stderr);
        return 1;
    }
    g_saveCalls = 0;
    g_saveReason = NULL;
    requestLen = build_enhance_request(request, sizeof(request), 3, 17, 915, 1);
    responseLen = vm_net_mock_build_equipment_enhance_response_with_save_callback(
        request, requestLen, response, sizeof(response), save_fixture);
    if (requestLen == 0 || responseLen == 0 || !response_u8(response, responseLen,
        "result", 1) || g_saveCalls != 1 || g_saveReason == NULL ||
        strcmp(g_saveReason, "equipment-enhance-success") != 0 ||
        role->backpackItems[0].enhanceLevel != 16 ||
        role->backpackItems[1].count != 0 || role->money != 18400u)
    {
        fputs("29/3 did not preserve the same successful one-third probability\n",
              stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (assert_curve() != 0 || assert_protocol_uses_curve() != 0)
        return 1;
    puts("equipment enhancement crystal probability regression passed: +15->+16 level-15 crystal contributes one third in 29/1, 29/2 and 29/3");
    return 0;
}
