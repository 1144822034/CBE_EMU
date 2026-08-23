/*
 * Pure wire regression for a successful mailbox claim.  It has no listener,
 * MySQL connection, or role mutation: the transactional mailbox test owns
 * those boundaries.  This locks the client-facing response order only.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

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
    if (valueOut)
        *valueOut = packet + *offset;
    if (valueLenOut)
        *valueLenOut = valueLen;
    *offset += valueLen;
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
    if (valueOut)
    {
        *valueOut = ((u32)blob[*offset + 2u] << 24) |
                    ((u32)blob[*offset + 3u] << 16) |
                    ((u32)blob[*offset + 4u] << 8) |
                    blob[*offset + 5u];
    }
    *offset += 6u;
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
    if (valueOut)
        *valueOut = (u16)(((u16)blob[*offset + 2u] << 8) |
                          blob[*offset + 3u]);
    *offset += 4u;
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
    if (valueOut)
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
    vm_net_mock_backpack_item_state first;
    vm_net_mock_backpack_item_state second;
    vm_net_mock_reward15_item_row rows[2];
    u8 packet[2048];
    u32 packetLen = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    u32 offset = 5;
    u16 dialogObjectLen;
    u16 rewardObjectLen;
    const u8 *result = NULL;
    const u8 *total = NULL;
    const u8 *itemInfo = NULL;
    u16 resultLen = 0;
    u16 totalLen = 0;
    u16 itemInfoLen = 0;
    u32 fieldOffset;
    u32 itemInfoOffset = 0;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(rows, 0, sizeof(rows));
    memset(packet, 0, sizeof(packet));
    first.itemId = 902;
    first.seq = 284;
    first.count = 99; /* Projected stack total; the wire carries delta=3. */
    second.itemId = 903;
    second.seq = 285;
    second.count = 7; /* Projected stack total; the wire carries delta=2. */
    rows[0].item = &first;
    rows[0].acquiredCount = 3;
    rows[1].item = &second;
    rows[1].acquiredCount = 2;

    if (!vm_net_mock_begin_wt_object(packet, sizeof(packet), &packetLen,
                                     1, 26, 1, &objectStart) ||
        !vm_net_mock_put_object_u8(packet, sizeof(packet), &packetLen,
                                   "hidebtn", 0))
    {
        fputs("mailbox dialog object was not built\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_object(packet, objectStart, packetLen);
    ++objectCount;
    if (!vm_net_mock_append_backpack_reward15_object(
            packet, sizeof(packet), &packetLen, &objectCount, rows, 2) ||
        objectCount != 2)
    {
        fputs("mailbox reward refresh object was not built\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(packet, packetLen, objectCount);

    if (packet[0] != 'W' || packet[1] != 'T' || packet[4] != 2 ||
        offset + 6u > packetLen || packet[offset] != 1 ||
        packet[offset + 1u] != 26 || packet[offset + 2u] != 1)
    {
        fputs("mailbox response did not keep dialog first\n", stderr);
        return 1;
    }
    dialogObjectLen = (u16)(((u16)packet[offset + 4u] << 8) |
                            packet[offset + 5u]);
    if (dialogObjectLen < 6u || offset + dialogObjectLen + 6u > packetLen)
    {
        fputs("mailbox dialog length is invalid\n", stderr);
        return 1;
    }
    offset += dialogObjectLen;
    if (packet[offset] != 1 || packet[offset + 1u] != 7 ||
        packet[offset + 2u] != 15)
    {
        fputs("mailbox response did not use native 7/15 reward refresh\n", stderr);
        return 1;
    }
    rewardObjectLen = (u16)(((u16)packet[offset + 4u] << 8) |
                            packet[offset + 5u]);
    fieldOffset = offset + 6u;
    if (rewardObjectLen < 6u || offset + rewardObjectLen != packetLen ||
        !read_object_field(packet, packetLen, &fieldOffset, "result", &result,
                           &resultLen) ||
        !read_object_field(packet, packetLen, &fieldOffset, "total", &total,
                           &totalLen) ||
        !read_object_field(packet, packetLen, &fieldOffset, "iteminfo",
                           &itemInfo, &itemInfoLen) ||
        fieldOffset != packetLen || resultLen != 3 || result[0] != 0 ||
        result[1] != 1 || result[2] != 1 || totalLen != 3 || total[0] != 0 ||
        total[1] != 1 || total[2] != 2 ||
        !read_reward_row(itemInfo, itemInfoLen, &itemInfoOffset, 902, 284, 3) ||
        !read_reward_row(itemInfo, itemInfoLen, &itemInfoOffset, 903, 285, 2) ||
        itemInfoOffset != itemInfoLen)
    {
        fputs("mailbox reward delta fields are invalid\n", stderr);
        return 1;
    }

    puts("mailbox-claim backpack refresh regression passed: 26/1 dialog then one 7/15 two-row native reward delta");
    return 0;
}
