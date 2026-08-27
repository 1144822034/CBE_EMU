/*
 * Pure regression for the blacksmith crystal-synthesis service.  It does not
 * open a listener, connect to MySQL, mutate a live role, or run a client
 * binary.  It exercises the same private action=1 transaction boundary used
 * by the real NPC dialog handler.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static int expect(bool condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "npc crystal synthesis regression: %s\n", message);
    return 1;
}

static bool read_object_field(const u8 *packet, u32 packetLen, u32 *offset,
                              const char *expectedName, const u8 **valueOut,
                              u16 *valueLenOut)
{
    u8 nameLen;
    u16 valueLen;

    if (packet == NULL || offset == NULL || expectedName == NULL ||
        *offset >= packetLen)
    {
        return false;
    }
    nameLen = packet[(*offset)++];
    if (*offset + nameLen + 2u > packetLen ||
        nameLen != strlen(expectedName) ||
        memcmp(packet + *offset, expectedName, nameLen) != 0)
    {
        return false;
    }
    *offset += nameLen;
    valueLen = (u16)(((u16)packet[*offset] << 8) | packet[*offset + 1u]);
    *offset += 2u;
    if (*offset + valueLen > packetLen)
        return false;
    if (valueOut != NULL)
        *valueOut = packet + *offset;
    if (valueLenOut != NULL)
        *valueLenOut = valueLen;
    *offset += valueLen;
    return true;
}

static bool read_tagged_u16(const u8 *blob, u16 blobLen, u32 *offset,
                            u16 *valueOut)
{
    if (blob == NULL || offset == NULL || *offset + 4u > blobLen ||
        blob[*offset] != 0 || blob[*offset + 1u] != 2)
    {
        return false;
    }
    if (valueOut != NULL)
    {
        *valueOut = (u16)(((u16)blob[*offset + 2u] << 8) |
                          blob[*offset + 3u]);
    }
    *offset += 4u;
    return true;
}

static bool read_tagged_u32(const u8 *blob, u16 blobLen, u32 *offset,
                            u32 *valueOut)
{
    if (blob == NULL || offset == NULL || *offset + 6u > blobLen ||
        blob[*offset] != 0 || blob[*offset + 1u] != 4)
    {
        return false;
    }
    if (valueOut != NULL)
    {
        *valueOut = ((u32)blob[*offset + 2u] << 24) |
                    ((u32)blob[*offset + 3u] << 16) |
                    ((u32)blob[*offset + 4u] << 8) |
                    blob[*offset + 5u];
    }
    *offset += 6u;
    return true;
}

static bool read_tagged_u8(const u8 *blob, u16 blobLen, u32 *offset,
                           u8 *valueOut)
{
    if (blob == NULL || offset == NULL || *offset + 3u > blobLen ||
        blob[*offset] != 0 || blob[*offset + 1u] != 1)
    {
        return false;
    }
    if (valueOut != NULL)
        *valueOut = blob[*offset + 2u];
    *offset += 3u;
    return true;
}

static bool read_reward_row(const u8 *info, u16 infoLen, u32 *offset,
                            u32 expectedItemId, u16 expectedSeq,
                            u32 expectedCount)
{
    u32 itemId = 0;
    u16 seq = 0;
    u32 count = 0;
    u16 enhanceLevel = 0;
    u16 enhanceMax = 0;
    u8 attrCount = 0;

    return read_tagged_u32(info, infoLen, offset, &itemId) &&
           read_tagged_u16(info, infoLen, offset, &seq) &&
           read_tagged_u32(info, infoLen, offset, &count) &&
           read_tagged_u16(info, infoLen, offset, &enhanceLevel) &&
           read_tagged_u16(info, infoLen, offset, &enhanceMax) &&
           read_tagged_u8(info, infoLen, offset, &attrCount) &&
           itemId == expectedItemId && seq == expectedSeq &&
           count == expectedCount && enhanceLevel == 0 && enhanceMax == 0 &&
           attrCount == 0;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_state before;
    vm_net_mock_scene_npcinfo_seed seed;
    vm_mock_service_client_session session;
    vm_mock_service_npc_context serviceContext;
    vm_mock_service_npc_transaction_context transaction;
    vm_net_mock_backpack_item_state *result = NULL;
    vm_net_mock_role_state tierRole;
    vm_net_mock_backpack_item_state *tierResult = NULL;
    vm_net_mock_reward15_item_row rewardRow;
    const char *name = NULL;
    const char *description = NULL;
    u32 value = 0;
    u32 resultItemId = 0;
    u32 sourceRemaining = 0;
    u32 packetLen = 5;
    u32 objectStart = 0;
    u32 offset = 5;
    u32 fieldOffset = 0;
    u32 countInfoOffset = 0;
    u32 itemInfoOffset = 0;
    u16 sourceSeq = 0;
    u16 resultSeq = 0;
    u16 objectLen = 0;
    u16 countObjectLen = 0;
    u16 rewardObjectLen = 0;
    u16 countInfoLen = 0;
    u16 resultLen = 0;
    u16 totalLen = 0;
    u16 itemInfoLen = 0;
    u16 wireSourceSeq = 0;
    u32 wireSourceRemaining = 0;
    const u8 *countInfo = NULL;
    const u8 *wireResult = NULL;
    const u8 *wireTotal = NULL;
    const u8 *itemInfo = NULL;
    u8 objectCount = 0;
    u8 wireUseResult = 0;
    u8 packet[2048];

    memset(&role, 0, sizeof(role));
    memset(&seed, 0, sizeof(seed));
    memset(&session, 0, sizeof(session));
    memset(&serviceContext, 0, sizeof(serviceContext));
    memset(&transaction, 0, sizeof(transaction));
    memset(&tierRole, 0, sizeof(tierRole));
    memset(&rewardRow, 0, sizeof(rewardRow));
    memset(packet, 0, sizeof(packet));
    seed.actorId = 20020;
    role.roleId = 7003;
    role.backpackCapacity = 24;
    role.nextBackpackSeq = 40;
    role.backpackItemCount = 1;
    role.backpackItems[0].itemId = 900;
    role.backpackItems[0].seq = 39;
    role.backpackItems[0].count =
        VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_FRAGMENT_MATERIAL_COUNT +
        2u;
    serviceContext.active = true;
    serviceContext.roleId = role.roleId;
    serviceContext.actorId = seed.actorId;
    serviceContext.serviceMask = vm_net_mock_npc_service_kind_mask(
        VM_NET_MOCK_NPC_KIND_CRYSTAL_SYNTHESIS);
    snprintf(serviceContext.scene, sizeof(serviceContext.scene), "%s",
             "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"); /* 00蓬莱仙岛_02.sce */

    if (expect(vm_net_mock_npc_service_option_default(
                   &seed, VM_NET_MOCK_NPC_KIND_CRYSTAL_SYNTHESIS, &name,
                   &description, &value) &&
                   name != NULL && description != NULL &&
                   value == VM_NET_MOCK_NPC_SERVICE_OPEN_CRYSTAL_SYNTHESIS_BASE,
               "crystal synthesis is not exposed as a normal NPC service") ||
        expect(vm_net_mock_npc_service_opcode_is_supported(
                   VM_NET_MOCK_NPC_SERVICE_OPEN_CRYSTAL_SYNTHESIS_BASE) &&
                   vm_net_mock_npc_service_opcode_is_supported(
                       VM_NET_MOCK_NPC_SERVICE_SYNTHESIZE_CRYSTAL_BASE) &&
                   !vm_net_mock_npc_service_opcode_is_supported(0xfa000000u),
               "crystal synthesis opcode boundary is not exact") ||
        expect(vm_net_mock_crystal_synthesis_recipe(900, &resultItemId) &&
                   resultItemId == 901 &&
                   vm_net_mock_crystal_synthesis_item_page(900) == 0 &&
                   vm_net_mock_crystal_synthesis_item_page(905) == 1,
               "fragment or page recipe is incorrect") ||
        expect(!vm_net_mock_crystal_synthesis_recipe(916, &resultItemId) &&
                   !vm_net_mock_crystal_synthesis_recipe(899, &resultItemId),
               "out-of-range crystal recipe was accepted") ||
        expect(vm_net_mock_npc_transaction_context_begin(
                   &session, &role, &serviceContext,
                   VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS, 900,
                   0, 0, 0,
                   VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_FRAGMENT_MATERIAL_COUNT),
               "synthesis prompt did not create a confirmation context") ||
        expect(vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction) &&
                   transaction.kind ==
                       VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS &&
                   transaction.itemId == 900 && transaction.page == 0 &&
                   transaction.quotedPrice ==
                       VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_FRAGMENT_MATERIAL_COUNT,
               "synthesis confirmation lost recipe or material count") ||
        expect(!vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction),
               "synthesis confirmation context can be replayed") ||
        expect(vm_net_mock_role_crystal_synthesize_in_memory(
                   &role, 900, &sourceSeq, &sourceRemaining, &resultSeq) &&
                   sourceSeq == 39 && sourceRemaining == 2 &&
                   resultSeq != 0 &&
                   (vm_net_mock_role_find_backpack_item(&role, 900, sourceSeq)) != NULL &&
                   vm_net_mock_role_find_backpack_item(&role, 900, sourceSeq)->count == 2 &&
                   (result = vm_net_mock_role_find_backpack_item(
                        &role, 901, resultSeq)) != NULL &&
                   result->count == 1,
               "ten fragments did not atomically become one first-level crystal"))
    {
        return 1;
    }
    before = role;
    if (expect(!vm_net_mock_role_crystal_synthesize_in_memory(
                   &role, 901, &sourceSeq, &sourceRemaining, &resultSeq) &&
                   memcmp(&role, &before, sizeof(role)) == 0,
               "insufficient material changed the backpack") ||
        expect(!vm_net_mock_role_crystal_synthesize_in_memory(
                   &role, 916, &sourceSeq, &sourceRemaining, &resultSeq) &&
                   memcmp(&role, &before, sizeof(role)) == 0,
               "sixteenth-level crystal produced an invalid output"))
    {
        return 1;
    }

    tierRole.backpackCapacity = 24;
    tierRole.nextBackpackSeq = 62;
    tierRole.backpackItemCount = 2;
    tierRole.backpackItems[0].itemId = 901;
    tierRole.backpackItems[0].seq = 60;
    tierRole.backpackItems[0].count =
        VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_MATERIAL_COUNT * 2u;
    tierRole.backpackItems[1].itemId = 902;
    tierRole.backpackItems[1].seq = 61;
    tierRole.backpackItems[1].count = 4;
    if (expect(vm_net_mock_role_crystal_synthesize_in_memory(
                   &tierRole, 901, &sourceSeq, &sourceRemaining, &resultSeq) &&
                   sourceSeq == 60 && sourceRemaining == 3 && resultSeq == 61 &&
                   vm_net_mock_role_find_backpack_item(&tierRole, 901, sourceSeq) != NULL &&
                   vm_net_mock_role_find_backpack_item(&tierRole, 901, sourceSeq)->count == 3 &&
                   (tierResult = vm_net_mock_role_find_backpack_item(
                        &tierRole, 902, resultSeq)) != NULL &&
                   tierResult->count == 5,
               "three same-level crystals did not update the existing higher-level stack"))
    {
        return 1;
    }

    if (!vm_net_mock_begin_wt_object(packet, sizeof(packet), &packetLen,
                                     1, 26, 1, &objectStart) ||
        !vm_net_mock_put_object_u8(packet, sizeof(packet), &packetLen,
                                   "hidebtn", 0))
    {
        fputs("crystal dialog object was not built\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_object(packet, objectStart, packetLen);
    ++objectCount;
    rewardRow.item = tierResult;
    rewardRow.acquiredCount = 1;
    if (!vm_net_mock_append_backpack_item_count11_object(
            packet, sizeof(packet), &packetLen, &objectCount,
            sourceSeq, 901, sourceRemaining) ||
        !vm_net_mock_append_backpack_reward15_object(
            packet, sizeof(packet), &packetLen, &objectCount, &rewardRow, 1) ||
        !vm_net_mock_append_backpack_item_count11_object(
            packet, sizeof(packet), &packetLen, &objectCount,
            resultSeq, 902, tierResult->count) ||
        objectCount != 4)
    {
        fputs("crystal backpack refresh objects were not built\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(packet, packetLen, objectCount);

    if (packet[0] != 'W' || packet[1] != 'T' || packet[4] != 4 ||
        offset + 6u > packetLen || packet[offset] != 1 ||
        packet[offset + 1u] != 26 || packet[offset + 2u] != 1)
    {
        fputs("crystal response did not keep dialog first\n", stderr);
        return 1;
    }
    objectLen = (u16)(((u16)packet[offset + 4u] << 8) |
                      packet[offset + 5u]);
    if (objectLen < 6u || offset + objectLen + 6u > packetLen)
    {
        fputs("crystal dialog length is invalid\n", stderr);
        return 1;
    }
    offset += objectLen;
    if (packet[offset] != 1 || packet[offset + 1u] != 7 ||
        packet[offset + 2u] != 11)
    {
        fputs("crystal response did not update the source stack with 7/11\n", stderr);
        return 1;
    }
    countObjectLen = (u16)(((u16)packet[offset + 4u] << 8) |
                           packet[offset + 5u]);
    fieldOffset = offset + 6u;
    if (countObjectLen < 6u || offset + countObjectLen + 6u > packetLen ||
        !read_object_field(packet, packetLen, &fieldOffset, "info", &countInfo,
                           &countInfoLen) ||
        fieldOffset != offset + countObjectLen ||
        !read_tagged_u8(countInfo, countInfoLen, &countInfoOffset,
                        &wireUseResult) ||
        !read_tagged_u16(countInfo, countInfoLen, &countInfoOffset,
                         &wireSourceSeq) ||
        !read_tagged_u32(countInfo, countInfoLen, &countInfoOffset,
                         &wireSourceRemaining) ||
        countInfoOffset != countInfoLen || wireUseResult != 1 ||
        wireSourceSeq != sourceSeq ||
        wireSourceRemaining != sourceRemaining)
    {
        fputs("crystal 7/11 source-stack fields are invalid\n", stderr);
        return 1;
    }
    offset += countObjectLen;
    if (packet[offset] != 1 || packet[offset + 1u] != 7 ||
        packet[offset + 2u] != 15)
    {
        fputs("crystal response did not use native 7/15 reward delta\n", stderr);
        return 1;
    }
    rewardObjectLen = (u16)(((u16)packet[offset + 4u] << 8) |
                            packet[offset + 5u]);
    fieldOffset = offset + 6u;
    if (rewardObjectLen < 6u || offset + rewardObjectLen + 6u > packetLen ||
        !read_object_field(packet, packetLen, &fieldOffset, "result",
                           &wireResult, &resultLen) ||
        !read_object_field(packet, packetLen, &fieldOffset, "total",
                           &wireTotal, &totalLen) ||
        !read_object_field(packet, packetLen, &fieldOffset, "iteminfo",
                           &itemInfo, &itemInfoLen) ||
        fieldOffset != offset + rewardObjectLen || resultLen != 3 || wireResult[0] != 0 ||
        wireResult[1] != 1 || wireResult[2] != 1 || totalLen != 3 ||
        wireTotal[0] != 0 || wireTotal[1] != 1 || wireTotal[2] != 1 ||
        !read_reward_row(itemInfo, itemInfoLen, &itemInfoOffset, 902,
                         resultSeq, 1) || itemInfoOffset != itemInfoLen)
    {
        fputs("crystal 7/15 output-item delta fields are invalid\n", stderr);
        return 1;
    }
    offset += rewardObjectLen;
    countInfoOffset = 0;
    fieldOffset = offset + 6u;
    if (packet[offset] != 1 || packet[offset + 1u] != 7 ||
        packet[offset + 2u] != 11)
    {
        fputs("crystal response did not update the result stack with 7/11\n", stderr);
        return 1;
    }
    countObjectLen = (u16)(((u16)packet[offset + 4u] << 8) |
                           packet[offset + 5u]);
    if (countObjectLen < 6u || offset + countObjectLen != packetLen ||
        !read_object_field(packet, packetLen, &fieldOffset, "info", &countInfo,
                           &countInfoLen) ||
        fieldOffset != packetLen ||
        !read_tagged_u8(countInfo, countInfoLen, &countInfoOffset,
                        &wireUseResult) ||
        !read_tagged_u16(countInfo, countInfoLen, &countInfoOffset,
                         &wireSourceSeq) ||
        !read_tagged_u32(countInfo, countInfoLen, &countInfoOffset,
                         &wireSourceRemaining) ||
        countInfoOffset != countInfoLen || wireUseResult != 1 ||
        wireSourceSeq != resultSeq ||
        wireSourceRemaining != tierResult->count)
    {
        fputs("crystal 7/11 result-stack fields are invalid\n", stderr);
        return 1;
    }

    puts("npc crystal synthesis regression passed: 10:1 fragment recipe, 3:1 crystal recipe, one-time confirmation, atomic mutation, and 26/1+7/11+7/15+7/11 backpack refresh");
    return 0;
}
