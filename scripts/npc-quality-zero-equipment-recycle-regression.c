/*
 * Pure regression for the equipment buyer's one-key quality-zero recovery.
 * It uses only in-memory catalog and role fixtures: no listener, MySQL
 * connection, client binary, account, or persisted role is touched.
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
    fprintf(stderr, "quality-zero equipment recycle regression: %s\n", message);
    return 1;
}

static bool advance_object(const u8 *packet, u32 packetLen, u32 *offset,
                           u8 expectedKind, u8 expectedSubtype,
                           u32 *objectStartOut, u32 *objectEndOut)
{
    u32 start = 0;
    u16 objectLen = 0;

    if (packet == NULL || offset == NULL || *offset + 6u > packetLen)
        return false;
    start = *offset;
    if (packet[start] != 1 || packet[start + 1u] != expectedKind ||
        packet[start + 2u] != expectedSubtype)
    {
        return false;
    }
    objectLen = (u16)(((u16)packet[start + 4u] << 8) | packet[start + 5u]);
    if (objectLen < 6u || start + objectLen > packetLen)
        return false;
    *offset = start + objectLen;
    if (objectStartOut != NULL)
        *objectStartOut = start;
    if (objectEndOut != NULL)
        *objectEndOut = *offset;
    return true;
}

static bool read_object_field(const u8 *packet, u32 objectEnd,
                              u32 *offset, const char *expectedName,
                              const u8 **valueOut, u16 *valueLenOut)
{
    u8 nameLen = 0;
    u16 valueLen = 0;

    if (packet == NULL || offset == NULL || expectedName == NULL ||
        *offset >= objectEnd)
    {
        return false;
    }
    nameLen = packet[(*offset)++];
    if (*offset + nameLen + 2u > objectEnd ||
        nameLen != strlen(expectedName) ||
        memcmp(packet + *offset, expectedName, nameLen) != 0)
    {
        return false;
    }
    *offset += nameLen;
    valueLen = (u16)(((u16)packet[*offset] << 8) | packet[*offset + 1u]);
    *offset += 2u;
    if (*offset + valueLen > objectEnd)
        return false;
    if (valueOut != NULL)
        *valueOut = packet + *offset;
    if (valueLenOut != NULL)
        *valueLenOut = valueLen;
    *offset += valueLen;
    return true;
}

static bool read_count11_zero(const u8 *packet, u32 packetLen, u32 *offset,
                              u16 expectedSeq)
{
    const u8 *info = NULL;
    u16 infoLen = 0;
    u32 objectStart = 0;
    u32 objectEnd = 0;
    u32 fieldOffset = 0;
    u32 infoOffset = 0;
    u16 sequence = 0;
    u32 remaining = 0;

    if (!advance_object(packet, packetLen, offset, 7, 11, &objectStart,
                        &objectEnd))
    {
        return false;
    }
    fieldOffset = objectStart + 6u;
    if (!read_object_field(packet, objectEnd, &fieldOffset, "info", &info,
                           &infoLen) ||
        fieldOffset != objectEnd || infoLen != 13u || info[0] != 0 ||
        info[1] != 1 || info[2] != 1 || info[3] != 0 || info[4] != 2)
    {
        return false;
    }
    infoOffset = 5;
    sequence = (u16)(((u16)info[infoOffset] << 8) | info[infoOffset + 1u]);
    infoOffset += 2u;
    if (infoOffset + 6u != infoLen || info[infoOffset] != 0 ||
        info[infoOffset + 1u] != 4)
    {
        return false;
    }
    remaining = ((u32)info[infoOffset + 2u] << 24) |
                ((u32)info[infoOffset + 3u] << 16) |
                ((u32)info[infoOffset + 4u] << 8) |
                info[infoOffset + 5u];
    return sequence == expectedSeq && remaining == 0;
}

static bool read_success_dialog_return_option(const u8 *packet, u32 objectStart,
                                              u32 objectEnd)
{
    const u8 *dialog = NULL;
    u16 dialogLen = 0;
    u32 fieldOffset = objectStart + 6u;
    u32 dialogOffset = 0;
    u16 textLen = 0;
    u16 optionNameLen = 0;
    u16 optionDescriptionLen = 0;
    u32 actionValue = 0;

    if (!read_object_field(packet, objectEnd, &fieldOffset, "hidebtn", NULL,
                           NULL) ||
        !read_object_field(packet, objectEnd, &fieldOffset, "dialog", &dialog,
                           &dialogLen) ||
        fieldOffset != objectEnd || dialogLen < 22u || dialog == NULL ||
        dialog[dialogOffset++] != 0 || dialog[dialogOffset++] != 1 ||
        dialog[dialogOffset++] != 0 || dialogOffset + 2u > dialogLen)
    {
        return false;
    }
    textLen = (u16)(((u16)dialog[dialogOffset] << 8) |
                    dialog[dialogOffset + 1u]);
    dialogOffset += 2u;
    if (textLen <= 1u || dialogOffset + textLen + 3u > dialogLen)
        return false;
    dialogOffset += textLen;
    if (dialog[dialogOffset++] != 0 || dialog[dialogOffset++] != 1 ||
        dialog[dialogOffset++] != 1 || dialogOffset + 3u > dialogLen ||
        dialog[dialogOffset++] != 0 || dialog[dialogOffset++] != 1 ||
        dialog[dialogOffset++] != 4 || dialogOffset + 2u > dialogLen)
    {
        return false;
    }
    optionNameLen = (u16)(((u16)dialog[dialogOffset] << 8) |
                          dialog[dialogOffset + 1u]);
    dialogOffset += 2u;
    if (optionNameLen <= 1u || dialogOffset + optionNameLen + 9u > dialogLen)
        return false;
    dialogOffset += optionNameLen;
    if (dialog[dialogOffset++] != 0 || dialog[dialogOffset++] != 1 ||
        dialog[dialogOffset++] != 1 || dialog[dialogOffset++] != 0 ||
        dialog[dialogOffset++] != 4)
    {
        return false;
    }
    actionValue = ((u32)dialog[dialogOffset] << 24) |
                  ((u32)dialog[dialogOffset + 1u] << 16) |
                  ((u32)dialog[dialogOffset + 2u] << 8) |
                  (u32)dialog[dialogOffset + 3u];
    dialogOffset += 4u;
    if (actionValue != VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE ||
        dialogOffset + 2u > dialogLen)
    {
        return false;
    }
    optionDescriptionLen = (u16)(((u16)dialog[dialogOffset] << 8) |
                                 dialog[dialogOffset + 1u]);
    dialogOffset += 2u;
    if (optionDescriptionLen <= 1u ||
        dialogOffset + optionDescriptionLen + 3u != dialogLen)
    {
        return false;
    }
    dialogOffset += optionDescriptionLen;
    return dialog[dialogOffset] == 0 && dialog[dialogOffset + 1u] == 1 &&
           dialog[dialogOffset + 2u] == 0;
}

static void set_catalog_item(u32 index, u32 itemId, u32 price, u8 quality)
{
    g_vm_net_mock_shop_catalog[index].itemId = itemId;
    g_vm_net_mock_shop_catalog[index].price = price;
    g_vm_net_mock_shop_catalog[index].isEquip = 1;
    g_vm_net_mock_shop_catalog[index].quality = quality;
    snprintf(g_vm_net_mock_shop_catalog[index].name,
             sizeof(g_vm_net_mock_shop_catalog[index].name), "equip%u", itemId);

    g_vm_net_mock_equipment_catalog[index].itemId = itemId;
    g_vm_net_mock_equipment_catalog[index].quality = quality;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_state roleBefore;
    vm_net_mock_role_state staleRole;
    vm_net_mock_role_state staleBefore;
    vm_mock_service_client_session session;
    vm_mock_service_npc_context serviceContext;
    vm_mock_service_npc_transaction_context transaction;
    u32 qualityZeroCount = 0;
    u32 qualityZeroPrice = 0;
    u32 recycledCount = 0;
    u32 recycledPrice = 0;
    u8 refreshPacket[1024];
    u8 refreshDialog[512];
    u32 refreshPacketLen = 5;
    u32 refreshDialogLen = 0;
    u32 refreshObjectStart = 0;
    u32 refreshObjectEnd = 0;
    u32 refreshOffset = 5;
    u8 refreshObjectCount = 0;

    memset(&role, 0, sizeof(role));
    memset(&roleBefore, 0, sizeof(roleBefore));
    memset(&staleRole, 0, sizeof(staleRole));
    memset(&staleBefore, 0, sizeof(staleBefore));
    memset(&session, 0, sizeof(session));
    memset(&serviceContext, 0, sizeof(serviceContext));
    memset(&transaction, 0, sizeof(transaction));
    memset(refreshPacket, 0, sizeof(refreshPacket));
    memset(refreshDialog, 0, sizeof(refreshDialog));
    memset(g_vm_net_mock_shop_catalog, 0, sizeof(g_vm_net_mock_shop_catalog));
    memset(g_vm_net_mock_equipment_catalog, 0,
           sizeof(g_vm_net_mock_equipment_catalog));

    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_catalog_count = 4;
    g_vm_net_mock_equipment_catalog_loaded = true;
    g_vm_net_mock_equipment_catalog_count = 4;
    set_catalog_item(0, 1001, 100, 0); /* equipped-only quality 0 */
    set_catalog_item(1, 1002, 100, 0); /* backpack quality 0, +5 */
    set_catalog_item(2, 1003, 101, 1); /* backpack quality 1 */
    set_catalog_item(3, 1004, 101, 0); /* backpack quality 0 */

    role.roleId = 9001;
    role.money = 500;
    role.backpackCapacity = 20;
    role.backpackItemCount = 3;
    role.equippedItems[0].itemId = 1001;
    role.backpackItems[0].itemId = 1002;
    role.backpackItems[0].seq = 9;
    role.backpackItems[0].enhanceLevel = 5;
    role.backpackItems[0].count = 1;
    role.backpackItems[1].itemId = 1003;
    role.backpackItems[1].seq = 10;
    role.backpackItems[1].count = 1;
    role.backpackItems[2].itemId = 1004;
    role.backpackItems[2].seq = 11;
    role.backpackItems[2].count = 1;
    roleBefore = role;

    serviceContext.active = true;
    serviceContext.roleId = role.roleId;
    serviceContext.actorId = 20020;
    serviceContext.serviceMask = vm_net_mock_npc_service_kind_mask(
        VM_NET_MOCK_NPC_KIND_EQUIPMENT_BUYER);
    snprintf(serviceContext.scene, sizeof(serviceContext.scene), "%s",
             "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"); /* 00蓬莱仙岛_02.sce */

    if (expect(vm_net_mock_npc_service_opcode_is_supported(
                   VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO_BASE),
               "quality-zero operation is missing from the 26/1 whitelist") ||
        expect(vm_net_mock_npc_collect_quality_zero_equipment(
                   &role, NULL, NULL, 0, &qualityZeroCount,
                   &qualityZeroPrice) &&
                   qualityZeroCount == 2 && qualityZeroPrice == 21,
               "quote did not select exactly the two backpack quality-zero rows") ||
        expect(vm_net_mock_npc_transaction_context_begin(
                   &session, &role, &serviceContext,
                   VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO,
                   VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO, 0,
                   qualityZeroCount, 0, qualityZeroPrice) &&
                   memcmp(&role, &roleBefore, sizeof(role)) == 0,
               "first click did not remain a mutation-free quote") ||
        expect(vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction) &&
                   transaction.kind ==
                       VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO &&
                   transaction.selector == qualityZeroCount &&
                   transaction.quotedPrice == qualityZeroPrice &&
                   !session.npcTransactionContext.active,
               "confirmation did not consume the exact batch quote") ||
        expect(!vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, NULL),
               "quality-zero confirmation context can be replayed"))
    {
        return 1;
    }

    staleRole = role;
    staleBefore = staleRole;
    g_vm_net_mock_equipment_catalog[3].quality = 1;
    if (expect(!vm_net_mock_role_recycle_quality_zero_equipment_in_memory(
                   &staleRole, qualityZeroCount, qualityZeroPrice, NULL,
                   NULL) &&
                   memcmp(&staleRole, &staleBefore, sizeof(staleRole)) == 0,
               "changed quality was not rejected before mutating the role"))
    {
        return 1;
    }
    g_vm_net_mock_equipment_catalog[3].quality = 0;

    if (expect(vm_net_mock_role_recycle_quality_zero_equipment_in_memory(
                   &role, transaction.selector, transaction.quotedPrice,
                   &recycledCount, &recycledPrice),
               "batch settlement rejected an unchanged confirmed quote") ||
        expect(recycledCount == 2 && recycledPrice == 21,
               "batch settlement returned an incorrect count or total") ||
        expect(role.money == 521,
               "batch settlement did not add the confirmed copper total") ||
        expect(role.equippedItems[0].itemId == 1001,
               "batch settlement changed an equipped instance") ||
        expect(vm_net_mock_role_find_backpack_item(&role, 1002, 9) == NULL &&
                   vm_net_mock_role_find_backpack_item(&role, 1004, 11) == NULL,
               "batch settlement retained a quality-zero backpack row") ||
        expect(vm_net_mock_role_find_backpack_item(&role, 1003, 10) != NULL,
               "batch settlement removed a nonzero-quality backpack row"))
    {
        return 1;
    }

    /* This is the exact post-commit object family used by the NPC handler.
     * A deferred 17/1 query is not emitted after action=1, so every removed
     * durable row must take the client parser's sequence-keyed zero-count
     * deletion branch in the same normal response.  The first dialog has a
     * real return action: an empty option list takes the firmware's empty-list
     * presentation branch before its success text can remain visible. */
    if (!vm_net_mock_seq_put_u8(refreshDialog, sizeof(refreshDialog),
                                &refreshDialogLen, 0) ||
        !vm_net_mock_seq_put_string(
            refreshDialog, sizeof(refreshDialog), &refreshDialogLen,
            "\xd2\xbb\xbc\xfc\xbb\xd8\xca\xd5\xb3\xc9\xb9\xa6\xa3\xac\xbb\xf1\xb5\xc3\xcd\xad\xc7\xae\xa1\xa3") ||
        !vm_net_mock_seq_put_u8(refreshDialog, sizeof(refreshDialog),
                                &refreshDialogLen, 1) ||
        !vm_net_mock_append_npc_service_dialog_option(
            refreshDialog, sizeof(refreshDialog), &refreshDialogLen,
            "\xb7\xb5\xbb\xd8\xd7\xb0\xb1\xb8\xc1\xd0\xb1\xed",
            1, VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE,
            "\xd2\xbb\xbc\xfc\xbb\xd8\xca\xd5\xd2\xd1\xcd\xea\xb3\xc9") ||
        !vm_net_mock_seq_put_u8(refreshDialog, sizeof(refreshDialog),
                                &refreshDialogLen, 0) ||
        !vm_net_mock_begin_wt_object(refreshPacket, sizeof(refreshPacket),
                                     &refreshPacketLen, 1, 26, 1,
                                     &refreshObjectStart) ||
        !vm_net_mock_put_object_u8(refreshPacket, sizeof(refreshPacket),
                                   &refreshPacketLen, "hidebtn", 0) ||
        !vm_net_mock_put_object_raw(refreshPacket, sizeof(refreshPacket),
                                    &refreshPacketLen, "dialog", refreshDialog,
                                    (u16)refreshDialogLen))
    {
        fputs("could not construct quality-zero recovery dialog\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_object(refreshPacket, refreshObjectStart,
                                 refreshPacketLen);
    ++refreshObjectCount;
    if (!vm_net_mock_append_backpack_item_count11_object(
            refreshPacket, sizeof(refreshPacket), &refreshPacketLen,
            &refreshObjectCount, 9, 1002, 0) ||
        !vm_net_mock_append_backpack_item_count11_object(
            refreshPacket, sizeof(refreshPacket), &refreshPacketLen,
            &refreshObjectCount, 11, 1004, 0) ||
        !vm_net_mock_append_type1_object(refreshPacket, sizeof(refreshPacket),
                                          &refreshPacketLen, 0) ||
        refreshObjectCount != 3)
    {
        fputs("could not construct quality-zero recovery refresh response\n",
              stderr);
        return 1;
    }
    ++refreshObjectCount;
    vm_net_mock_finish_wt_packet(refreshPacket, refreshPacketLen,
                                 refreshObjectCount);

    if (expect(refreshPacket[0] == 'W' && refreshPacket[1] == 'T' &&
                   refreshPacket[4] == 4,
               "recovery response object count is incorrect") ||
        expect(advance_object(refreshPacket, refreshPacketLen, &refreshOffset,
                              26, 1, &refreshObjectStart, &refreshObjectEnd) &&
                   read_success_dialog_return_option(
                       refreshPacket, refreshObjectStart, refreshObjectEnd),
               "recovery response does not keep a returnable success dialog first") ||
        expect(read_count11_zero(refreshPacket, refreshPacketLen,
                                 &refreshOffset, 9) &&
                   read_count11_zero(refreshPacket, refreshPacketLen,
                                     &refreshOffset, 11),
               "recovery response does not delete every recycled sequence") ||
        expect(advance_object(refreshPacket, refreshPacketLen, &refreshOffset,
                              10, 26, NULL, NULL) &&
                   refreshOffset == refreshPacketLen,
               "recovery response does not refresh the copper HUD"))
    {
        return 1;
    }

    puts("quality-zero equipment recycle regression passed: quote, one-shot confirmation, stale quote rejection, atomic settlement, and returnable-26/1+7/11*+10/26 refresh");
    return 0;
}
