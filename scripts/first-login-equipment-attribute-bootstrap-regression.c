/*
 * Deterministic server-only regression for the first-login equipment bootstrap.
 *
 * It neither starts a listener nor connects to MySQL.  A role with one normal
 * backpack row and durable HP/MP-raising equipment receives a 30/21 backpack
 * snapshot followed by full 7/7(type=2) equipped rows and an empty
 * 7/7(type=3) completion in the first 5/10 + 7/7(type=1) bootstrap response.
 * The title 1/1/6 + 1/1/15 acknowledgement must not consume that one-shot;
 * a repeated group poll must not duplicate it, and a new role select must
 * re-arm it.
 *
 * The type-2 rows use the fixed equipment-slot sequence namespace and full
 * common equipment attributes; type-3 has exactly one zero byte of iteminfo.
 * The durable role's derived vitals remain authoritative through actorinfo.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool begin_request_object(u8 *out, u32 outCap, u32 *pos, u8 kind,
                                 u8 subtype, u32 *objectStart)
{
    if (out == NULL || pos == NULL || *pos + 5 > outCap)
        return false;
    if (objectStart)
        *objectStart = *pos;
    out[(*pos)++] = 1;
    out[(*pos)++] = kind;
    out[(*pos)++] = subtype;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    return true;
}

static void finish_request_object(u8 *out, u32 objectStart, u32 pos)
{
    u32 objectLen = pos - objectStart;

    out[objectStart + 3] = (u8)(objectLen >> 8);
    out[objectStart + 4] = (u8)objectLen;
}

static void finish_request_packet(u8 *out, u32 length)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(length >> 8);
    out[3] = (u8)length;
}

static bool build_role_select_request(u8 *out, u32 outCap, u32 actorId,
                                      u32 *lengthOut)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (out == NULL || lengthOut == NULL ||
        !begin_request_object(out, outCap, &pos, 1, 6, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "actorID", actorId))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    finish_request_packet(out, pos);
    *lengthOut = pos;
    return true;
}

static bool build_group_type1_request(u8 *out, u32 outCap, u32 *lengthOut)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (out == NULL || lengthOut == NULL ||
        !begin_request_object(out, outCap, &pos, 5, 10, &objectStart))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    if (!begin_request_object(out, outCap, &pos, 7, 7, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 1))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    finish_request_packet(out, pos);
    *lengthOut = pos;
    return true;
}

static bool response_object_at(const u8 *packet, u32 length, u8 index,
                               u8 *majorOut, u8 *kindOut, u8 *subtypeOut,
                               const u8 **payloadOut, u16 *payloadLenOut)
{
    u32 offset = 5;

    if (majorOut)
        *majorOut = 0;
    if (kindOut)
        *kindOut = 0;
    if (subtypeOut)
        *subtypeOut = 0;
    if (payloadOut)
        *payloadOut = NULL;
    if (payloadLenOut)
        *payloadLenOut = 0;
    if (packet == NULL || length < 5 || packet[0] != 'W' ||
        packet[1] != 'T' || index >= packet[4])
    {
        return false;
    }

    for (u8 current = 0; current <= index; ++current)
    {
        u16 objectLen = 0;

        if (offset + 6 > length)
            return false;
        objectLen = (u16)(((u16)packet[offset + 4] << 8) |
                          packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > length)
            return false;
        if (current == index)
        {
            if (majorOut)
                *majorOut = packet[offset];
            if (kindOut)
                *kindOut = packet[offset + 1];
            if (subtypeOut)
                *subtypeOut = packet[offset + 2];
            if (payloadOut)
                *payloadOut = packet + offset + 6;
            if (payloadLenOut)
                *payloadLenOut = (u16)(objectLen - 6);
            return true;
        }
        offset += objectLen;
    }
    return false;
}

static bool actorinfo_read_u32(const u8 *actorInfo, u32 actorInfoLen,
                               u32 *pos, u32 *valueOut)
{
    if (actorInfo == NULL || pos == NULL || *pos + 6 > actorInfoLen ||
        actorInfo[*pos] != 0 || actorInfo[*pos + 1] != 4)
    {
        return false;
    }
    if (valueOut)
    {
        *valueOut = ((u32)actorInfo[*pos + 2] << 24) |
                    ((u32)actorInfo[*pos + 3] << 16) |
                    ((u32)actorInfo[*pos + 4] << 8) |
                    actorInfo[*pos + 5];
    }
    *pos += 6;
    return true;
}

static bool actorinfo_skip_u8(const u8 *actorInfo, u32 actorInfoLen,
                              u32 *pos)
{
    if (actorInfo == NULL || pos == NULL || *pos + 3 > actorInfoLen ||
        actorInfo[*pos] != 0 || actorInfo[*pos + 1] != 1)
    {
        return false;
    }
    *pos += 3;
    return true;
}

static bool actorinfo_skip_string(const u8 *actorInfo, u32 actorInfoLen,
                                  u32 *pos)
{
    u16 length = 0;

    if (actorInfo == NULL || pos == NULL || *pos + 2 > actorInfoLen)
        return false;
    length = (u16)(((u16)actorInfo[*pos] << 8) | actorInfo[*pos + 1]);
    *pos += 2;
    if (length == 0 || *pos + length > actorInfoLen)
        return false;
    *pos += length;
    return true;
}

static bool actorinfo_read_vitals(const u8 *actorInfo, u32 actorInfoLen,
                                  u32 *hpCurrentOut, u32 *hpBaseMaxOut,
                                  u32 *mpCurrentOut, u32 *mpBaseMaxOut,
                                  u32 *hpDisplayMaxOut, u32 *mpDisplayMaxOut)
{
    u32 pos = 0;
    u32 ignored = 0;

    if (!actorinfo_read_u32(actorInfo, actorInfoLen, &pos, &ignored) ||
        !actorinfo_skip_u8(actorInfo, actorInfoLen, &pos) ||
        !actorinfo_skip_u8(actorInfo, actorInfoLen, &pos) ||
        !actorinfo_skip_string(actorInfo, actorInfoLen, &pos) ||
        !actorinfo_read_u32(actorInfo, actorInfoLen, &pos, &ignored) ||
        !actorinfo_skip_string(actorInfo, actorInfoLen, &pos) ||
        !actorinfo_read_u32(actorInfo, actorInfoLen, &pos, &ignored) ||
        !actorinfo_read_u32(actorInfo, actorInfoLen, &pos, hpCurrentOut) ||
        !actorinfo_read_u32(actorInfo, actorInfoLen, &pos, hpBaseMaxOut) ||
        !actorinfo_read_u32(actorInfo, actorInfoLen, &pos, mpCurrentOut) ||
        !actorinfo_read_u32(actorInfo, actorInfoLen, &pos, mpBaseMaxOut))
    {
        return false;
    }
    /* strength, six secondary attributes, total EXP and the documented gap. */
    for (u32 i = 0; i < 9; ++i)
    {
        if (!actorinfo_read_u32(actorInfo, actorInfoLen, &pos, &ignored))
            return false;
    }
    return actorinfo_skip_u8(actorInfo, actorInfoLen, &pos) &&
           actorinfo_skip_u8(actorInfo, actorInfoLen, &pos) &&
           actorinfo_read_u32(actorInfo, actorInfoLen, &pos, hpDisplayMaxOut) &&
           actorinfo_read_u32(actorInfo, actorInfoLen, &pos, mpDisplayMaxOut);
}

static int assert_title_role_select_reply(const u8 *packet, u32 length,
                                          u32 expectedRoleId,
                                          const vm_net_mock_player_stats *baseStats,
                                          const vm_net_mock_player_stats *fullStats)
{
    u8 major = 0;
    u8 kind = 0;
    u8 subtype = 0;
    const u8 *titlePayload = NULL;
    const u8 *actorInfo = NULL;
    u16 titlePayloadLen = 0;
    u16 actorInfoLen = 0;
    u32 hpCurrent = 0;
    u32 hpBaseMax = 0;
    u32 mpCurrent = 0;
    u32 mpBaseMax = 0;
    u32 hpDisplayMax = 0;
    u32 mpDisplayMax = 0;

    if (packet == NULL || length < 5 || packet[4] != 2 ||
        g_netMockBackpackGridSeededRoleId != 0)
    {
        fprintf(stderr,
                "role-select unexpectedly consumed the group equipment seed: "
                "len=%u objects=%u seeded=%u\n",
                length, packet ? packet[4] : 0,
                g_netMockBackpackGridSeededRoleId);
        return 1;
    }
    for (u8 index = 0; index < packet[4]; ++index)
    {
        if (!response_object_at(packet, length, index, &major, &kind,
                                &subtype, NULL, NULL) ||
            major != 1 || kind != 1 ||
            subtype != (index == 0 ? 6 : 15))
        {
            fprintf(stderr,
                    "role-select object %u changed: got=%u/%u/%u "
                    "expected=1/1/%u\n",
                    index, major, kind, subtype, index == 0 ? 6 : 15);
            return 1;
        }
    }
    if (expectedRoleId == 0)
    {
        return 1;
    }
    if (baseStats == NULL || fullStats == NULL ||
        !response_object_at(packet, length, 0, &major, &kind, &subtype,
                            &titlePayload, &titlePayloadLen) ||
        !vm_net_mock_get_object_entry_bytes(titlePayload, titlePayloadLen,
                                            "actorinfo", &actorInfo,
                                            &actorInfoLen) ||
        !actorinfo_read_vitals(actorInfo, actorInfoLen,
                               &hpCurrent, &hpBaseMax,
                               &mpCurrent, &mpBaseMax,
                               &hpDisplayMax, &mpDisplayMax) ||
        hpCurrent != fullStats->maxHp || mpCurrent != fullStats->maxMp ||
        hpBaseMax != baseStats->maxHp || mpBaseMax != baseStats->maxMp ||
        hpDisplayMax != fullStats->maxHp ||
        mpDisplayMax != fullStats->maxMp)
    {
        fprintf(stderr,
                "ActorInfo vitals mismatch: current=%u/%u base=%u/%u "
                "display=%u/%u expected-full=%u/%u expected-base=%u/%u\n",
                hpCurrent, mpCurrent, hpBaseMax, mpBaseMax,
                hpDisplayMax, mpDisplayMax,
                fullStats ? fullStats->maxHp : 0,
                fullStats ? fullStats->maxMp : 0,
                baseStats ? baseStats->maxHp : 0,
                baseStats ? baseStats->maxMp : 0);
        return 1;
    }
    return 0;
}

static bool equip_vital_bonus_pair(vm_net_mock_role_state *role)
{
    u32 catalogCount = vm_net_mock_load_equipment_catalog();

    if (role == NULL)
        return false;
    for (u32 hpIndex = 0; hpIndex < catalogCount; ++hpIndex)
    {
        const vm_net_mock_equipment_catalog_item *hpItem =
            &g_vm_net_mock_equipment_catalog[hpIndex];

        if (hpItem->slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
            hpItem->levelRequired > role->level || hpItem->bonus.hp == 0)
        {
            continue;
        }
        for (u32 mpIndex = 0; mpIndex < catalogCount; ++mpIndex)
        {
            const vm_net_mock_equipment_catalog_item *mpItem =
                &g_vm_net_mock_equipment_catalog[mpIndex];

            if (mpItem->slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
                mpItem->levelRequired > role->level || mpItem->bonus.mp == 0 ||
                (mpItem != hpItem && mpItem->slot == hpItem->slot))
            {
                continue;
            }
            role->equippedItems[hpItem->slot].itemId = hpItem->itemId;
            role->equippedItems[hpItem->slot].durability = hpItem->durabilityMax;
            role->equippedItems[hpItem->slot].durabilityMax = hpItem->durabilityMax;
            role->equippedItems[mpItem->slot].itemId = mpItem->itemId;
            role->equippedItems[mpItem->slot].durability = mpItem->durabilityMax;
            role->equippedItems[mpItem->slot].durabilityMax = mpItem->durabilityMax;
            if (!vm_net_mock_equipment_enhancement_ensure_affixes(
                    hpItem, 0,
                    &role->equippedItems[hpItem->slot].enhanceAffixes,
                    role->roleId ^ hpItem->itemId ^
                        ((hpItem->slot + 1u) * 0x9e3779b9u)) ||
                !vm_net_mock_equipment_enhancement_ensure_affixes(
                    mpItem, 0,
                    &role->equippedItems[mpItem->slot].enhanceAffixes,
                    role->roleId ^ mpItem->itemId ^
                        ((mpItem->slot + 1u) * 0x9e3779b9u)))
            {
                return false;
            }
            return true;
        }
    }
    return false;
}

static int assert_group_login_bootstrap(const u8 *packet, u32 length,
                                        u32 expectedRoleId, bool expectSeed)
{
    const u8 *equipmentPayload = NULL;
    const u8 *equipmentItemInfo = NULL;
    const u8 *completionPayload = NULL;
    const u8 *completionItemInfo = NULL;
    u16 equipmentPayloadLen = 0;
    u16 equipmentItemInfoLen = 0;
    u16 completionPayloadLen = 0;
    u16 completionItemInfoLen = 0;
    u8 gridIndex = 0xff;
    u8 equipmentIndex = 0xff;
    u8 completionIndex = 0xff;
    u8 major = 0;
    u8 kind = 0;
    u8 subtype = 0;

    if (packet == NULL || length < 5 || packet[4] < 2 ||
        g_netMockBackpackGridSeededRoleId != expectedRoleId)
    {
        fprintf(stderr,
                "group bootstrap has invalid response or lifecycle state: "
                "len=%u objects=%u seeded=%u expected=%u\n",
                length, packet ? packet[4] : 0,
                g_netMockBackpackGridSeededRoleId, expectedRoleId);
        return 1;
    }
    for (u8 index = 0; index < packet[4]; ++index)
    {
        const u8 *payload = NULL;
        u16 payloadLen = 0;
        u8 type = 0;

        if (!response_object_at(packet, length, index, &major, &kind,
                                &subtype, &payload, &payloadLen))
        {
            fputs("group bootstrap contains a malformed response object\n", stderr);
            return 1;
        }
        if (major == 1 && kind == 30 && subtype == 21)
        {
            if (gridIndex != 0xff)
            {
                fputs("group bootstrap duplicated the backpack grid\n", stderr);
                return 1;
            }
            gridIndex = index;
        }
        if (major == 1 && kind == 7 && subtype == 7 &&
            vm_net_mock_get_object_u8_field(payload, payloadLen, "type", &type) &&
            type == 2)
        {
            if (equipmentIndex != 0xff)
            {
                fputs("group bootstrap duplicated the equipped-item stream\n", stderr);
                return 1;
            }
            equipmentIndex = index;
            equipmentPayload = payload;
            equipmentPayloadLen = payloadLen;
        }
        if (major == 1 && kind == 7 && subtype == 7 &&
            vm_net_mock_get_object_u8_field(payload, payloadLen, "type", &type) &&
            type == 3)
        {
            if (completionIndex != 0xff)
            {
                fputs("group bootstrap duplicated the type-3 completion\n", stderr);
                return 1;
            }
            completionIndex = index;
            completionPayload = payload;
            completionPayloadLen = payloadLen;
        }
    }
    if (!expectSeed)
    {
        if (gridIndex != 0xff || equipmentIndex != 0xff || completionIndex != 0xff)
        {
            fputs("repeated group bootstrap replayed the equipment seed\n", stderr);
            return 1;
        }
        return 0;
    }
    if (gridIndex == 0xff || equipmentIndex == 0xff || completionIndex == 0xff ||
        gridIndex >= equipmentIndex || completionIndex != equipmentIndex + 1 ||
        !vm_net_mock_get_object_entry_bytes(equipmentPayload,
                                            equipmentPayloadLen, "iteminfo",
                                            &equipmentItemInfo,
                                            &equipmentItemInfoLen) ||
        equipmentItemInfo == NULL || equipmentItemInfoLen != 161 ||
        equipmentItemInfo[0] != 0 || equipmentItemInfo[1] != 1 ||
        equipmentItemInfo[2] != 2 || equipmentItemInfo[27] != 0 ||
        equipmentItemInfo[28] != 1 || equipmentItemInfo[29] != 4 ||
        equipmentItemInfo[106] != 0 || equipmentItemInfo[107] != 1 ||
        equipmentItemInfo[108] != 4 ||
        !vm_net_mock_get_object_entry_bytes(completionPayload,
                                            completionPayloadLen, "iteminfo",
                                            &completionItemInfo,
                                            &completionItemInfoLen) ||
        completionItemInfo == NULL || completionItemInfoLen != 1 ||
        completionItemInfo[0] != 0)
    {
        fputs("group bootstrap lacks the ordered full equipment seed\n", stderr);
        return 1;
    }
    return 0;
}

static int assert_grid_minimum_stage_plan(
    const u8 *packet, u32 length,
    const vm_net_mock_backpack_item_state *expectedEquipment)
{
    const u8 *gridPayload = NULL;
    const u8 *itemInfo = NULL;
    u16 gridPayloadLen = 0;
    u16 itemInfoLen = 0;

    if (packet == NULL || length < 5 || expectedEquipment == NULL ||
        expectedEquipment->enhanceAffixes.type[0] == 0 ||
        expectedEquipment->enhanceAffixes.value[0] == 0)
        return 1;
    for (u8 index = 0; index < packet[4]; ++index)
    {
        u8 major = 0;
        u8 kind = 0;
        u8 subtype = 0;

        if (!response_object_at(packet, length, index, &major, &kind,
                                &subtype, &gridPayload, &gridPayloadLen))
        {
            fputs("could not parse group bootstrap while locating grid\n", stderr);
            return 1;
        }
        if (major == 1 && kind == 30 && subtype == 21)
            break;
        gridPayload = NULL;
    }
    if (gridPayload == NULL ||
        !vm_net_mock_get_object_entry_bytes(gridPayload, gridPayloadLen,
                                            "iteminfo", &itemInfo,
                                            &itemInfoLen) ||
        itemInfo == NULL || itemInfoLen != 67 ||
        /* The normal row remains compact.  The equipment row carries exactly
         * its durable +4 plan (the typed scalar layout is 3+3+3+4 bytes). */
        itemInfo[26] != 0 || itemInfo[53] != 1 || itemInfo[56] != 4 ||
        itemInfo[59] != expectedEquipment->enhanceAffixes.type[0] ||
        itemInfo[62] != 0 ||
        (((u16)itemInfo[65] << 8) | itemInfo[66]) !=
            expectedEquipment->enhanceAffixes.value[0])
    {
        fprintf(stderr,
                "grid did not preserve the minimum durable +4 plan: "
                "iteminfo_len=%u attr0=%u attr1=%u threshold=%u type=%u\n",
                itemInfoLen,
                itemInfo != NULL && itemInfoLen > 26 ? itemInfo[26] : 0,
                itemInfo != NULL && itemInfoLen > 53 ? itemInfo[53] : 0,
                itemInfo != NULL && itemInfoLen > 56 ? itemInfo[56] : 0,
                itemInfo != NULL && itemInfoLen > 59 ? itemInfo[59] : 0);
        return 1;
    }
    return 0;
}

/* The recorded failure shape contains 48 visible rows, 31 of them equipment.
 * Check the entire maximum historical equipment set rather than inferring
 * that the two-row login fixture covers every iteration of the serializer. */
static int assert_full_grid_minimum_stage_plan(
    const vm_net_mock_equipment_catalog_item *equipment)
{
    enum
    {
        kGridRows = 48,
        kEquipmentRows = 31,
        kCompactRowBytes = 27,
        kFirstStageRowBytes = 40
    };
    vm_net_mock_role_state role;
    u8 itemInfo[VM_NET_MOCK_BACKPACK_GRID_ITEMINFO_MAX_BYTES];
    u32 itemInfoLen = 0;
    u32 gridCount = 0;
    u32 pos = 0;

    if (equipment == NULL || equipment->itemId == 0)
        return 1;
    memset(&role, 0, sizeof(role));
    memset(itemInfo, 0, sizeof(itemInfo));
    role.roleId = 910002;
    role.backpackCapacity = kGridRows;
    role.backpackItemCount = kGridRows;
    role.nextBackpackSeq = kGridRows + 1;

    for (u32 index = 0; index < kGridRows; ++index)
    {
        vm_net_mock_backpack_item_state *item = &role.backpackItems[index];

        item->itemId = index < kEquipmentRows ? equipment->itemId : 801;
        item->seq = (u16)(index + 1);
        item->count = 1;
        if (index < kEquipmentRows &&
            !vm_net_mock_equipment_enhancement_ensure_affixes(
                equipment, 0, &item->enhanceAffixes,
                role.roleId ^ item->itemId ^
                    ((u32)item->seq * 0x9e3779b9u)))
        {
            fputs("could not create a full-grid durable +4 plan\n", stderr);
            return 1;
        }
    }
    if (!vm_net_mock_build_backpack_grid_iteminfo_blob(
            itemInfo, sizeof(itemInfo), &role, &itemInfoLen, &gridCount) ||
        gridCount != kGridRows ||
        itemInfoLen != kEquipmentRows * kFirstStageRowBytes +
                           (kGridRows - kEquipmentRows) * kCompactRowBytes)
    {
        fprintf(stderr,
                "full-grid minimum-stage layout changed: rows=%u iteminfo_len=%u\n",
                gridCount, itemInfoLen);
        return 1;
    }
    for (u32 index = 0; index < kGridRows; ++index)
    {
        const vm_net_mock_backpack_item_state *item = &role.backpackItems[index];
        const bool isEquipment = index < kEquipmentRows;
        const u32 rowBytes = isEquipment ? kFirstStageRowBytes :
                                           kCompactRowBytes;

        if (pos + rowBytes > itemInfoLen ||
            itemInfo[pos + 26] != (isEquipment ? 1 : 0) ||
            (isEquipment &&
             (itemInfo[pos + 29] != 4 ||
              itemInfo[pos + 32] != item->enhanceAffixes.type[0] ||
              itemInfo[pos + 35] != 0 ||
              (((u16)itemInfo[pos + 38] << 8) | itemInfo[pos + 39]) !=
                  item->enhanceAffixes.value[0])))
        {
            fprintf(stderr,
                    "full-grid row %u did not retain its minimum +4 plan\n",
                    index);
            return 1;
        }
        pos += rowBytes;
    }
    return pos == itemInfoLen ? 0 : 1;
}

int main(void)
{
    const u32 roleId = 910001;
    vm_net_mock_role_state *role = NULL;
    vm_mock_service_client_session *session = NULL;
    const vm_net_mock_equipment_catalog_item *backpackEquipment = NULL;
    vm_net_mock_player_stats baseStats;
    vm_net_mock_player_stats fullStats;
    u8 selectRequest[128];
    u8 groupRequest[128];
    u8 response[16384];
    u32 selectRequestLen = 0;
    u32 groupRequestLen = 0;
    u32 responseLen = 0;

    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
    g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = roleId;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_netMockBackpackGridSeededRoleId = 0;
    g_netMockBackpackGridReseedPendingRoleId = 0;

    role = &g_vm_net_mock_role_db.roles[0];
    role->roleId = roleId;
    role->job = 1;
    role->backpackCapacity = 36;
    role->level = 70;
    role->exp = vm_net_mock_role_level_start_exp(role->level);
    role->hp = role->hpMax = 1000;
    role->mp = role->mpMax = 500;
    role->nextBackpackSeq = 3;
    snprintf(role->name, sizeof(role->name), "seed-test");
    snprintf(role->scene, sizeof(role->scene), "01\xCC\xD2\xBB\xA8\xB5\xBA_01.sce");
    role->backpackItemCount = 2;
    role->backpackItems[0].itemId = 801;
    role->backpackItems[0].seq = 1;
    role->backpackItems[0].count = 1;
    if (!equip_vital_bonus_pair(role))
    {
        fputs("could not find usable HP/MP equipment pair in catalog\n", stderr);
        return 1;
    }
    for (u8 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (role->equippedItems[slot].itemId != 0)
        {
            backpackEquipment = vm_net_mock_find_equipment_catalog_item(
                role->equippedItems[slot].itemId);
            break;
        }
    }
    if (backpackEquipment == NULL)
    {
        fputs("could not select a catalog equipment item for backpack seed\n", stderr);
        return 1;
    }
    if (assert_full_grid_minimum_stage_plan(backpackEquipment) != 0)
        return 1;
    role->backpackItems[1].itemId = backpackEquipment->itemId;
    role->backpackItems[1].seq = 2;
    role->backpackItems[1].count = 1;
    if (!vm_net_mock_equipment_enhancement_ensure_affixes(
            backpackEquipment, 0, &role->backpackItems[1].enhanceAffixes,
            roleId ^ backpackEquipment->itemId ^ (2u * 0x9e3779b9u)))
    {
        fputs("could not create the backpack equipment stage plan\n", stderr);
        return 1;
    }
    vm_net_mock_role_build_base_player_stats(role, &baseStats);
    vm_net_mock_role_build_player_stats(role, &fullStats);
    if (fullStats.maxHp <= baseStats.maxHp || fullStats.maxMp <= baseStats.maxMp)
    {
        fprintf(stderr,
                "selected equipment did not raise both vitals: base=%u/%u full=%u/%u\n",
                baseStats.maxHp, baseStats.maxMp,
                fullStats.maxHp, fullStats.maxMp);
        return 1;
    }
    /* Model the user's regression exactly: durable current values have been
     * restored to the full equipped maxima before a new login. */
    role->hp = role->hpMax = fullStats.maxHp;
    role->mp = role->mpMax = fullStats.maxMp;

    /* The real 5/10 builder also initializes the client-owned solo roster.
     * Give this no-network fixture one ordinary active session so it exercises
     * that actual builder rather than calling its backpack helper directly. */
    session = vm_mock_service_get_or_create_client_session(0x910001u);
    if (session == NULL)
    {
        fputs("could not create isolated group-bootstrap session\n", stderr);
        return 1;
    }
    g_vm_mock_service_active_client_id = session->clientId;
    session->roleOnline = true;
    session->onlinePresenceValid = true;
    session->onlineRoleId = roleId;
    session->onlineJob = role->job;
    session->onlineLevel = (u16)role->level;
    session->onlineHp = role->hp;
    session->onlineHpMax = role->hpMax;
    session->onlineMp = role->mp;
    session->onlineMpMax = role->mpMax;
    snprintf(session->onlineRoleName, sizeof(session->onlineRoleName), "%s",
             role->name);
    snprintf(session->onlineScene, sizeof(session->onlineScene), "%s",
             role->scene);

    if (!build_role_select_request(selectRequest, sizeof(selectRequest),
                                   roleId, &selectRequestLen))
    {
        fputs("could not construct first-login requests\n", stderr);
        return 1;
    }
    if (!build_group_type1_request(groupRequest, sizeof(groupRequest),
                                   &groupRequestLen))
    {
        fputs("could not construct first-login group bootstrap request\n",
              stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_title_role_select_response(
        selectRequest, selectRequestLen, response, sizeof(response));
    if (assert_title_role_select_reply(response, responseLen, roleId,
                                       &baseStats, &fullStats) != 0)
        return 1;

    responseLen = vm_net_mock_build_group_type1_response(
        groupRequest, groupRequestLen, response, sizeof(response));
    if (assert_group_login_bootstrap(response, responseLen, roleId, true) != 0 ||
        assert_grid_minimum_stage_plan(response, responseLen,
                                       &role->backpackItems[1]) != 0)
        return 1;

    responseLen = vm_net_mock_build_group_type1_response(
        groupRequest, groupRequestLen, response, sizeof(response));
    if (assert_group_login_bootstrap(response, responseLen, roleId, false) != 0)
        return 1;

    responseLen = vm_net_mock_build_title_role_select_response(
        selectRequest, selectRequestLen, response, sizeof(response));
    if (assert_title_role_select_reply(response, responseLen, roleId,
                                       &baseStats, &fullStats) != 0)
        return 1;

    responseLen = vm_net_mock_build_group_type1_response(
        groupRequest, groupRequestLen, response, sizeof(response));
    if (assert_group_login_bootstrap(response, responseLen, roleId, true) != 0)
        return 1;

    printf("first-login equipment bootstrap regression passed type3_completion=1\n");
    return 0;
}
