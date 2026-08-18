/*
 * Pure WT21/8 receipt-layout regression.  It does not open a listener,
 * connect to MySQL, or mutate a live account.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool read_tagged_i16(const u8 *blob, u16 blobLen, u32 *offset,
                            u16 *valueOut)
{
    if (blob == NULL || offset == NULL || *offset + 4u > blobLen ||
        blob[*offset] != 0 || blob[*offset + 1u] != 2)
        return false;
    if (valueOut)
        *valueOut = (u16)(((u16)blob[*offset + 2u] << 8) |
                          blob[*offset + 3u]);
    *offset += 4u;
    return true;
}

static bool read_tagged_u32(const u8 *blob, u16 blobLen, u32 *offset,
                            u32 *valueOut)
{
    if (blob == NULL || offset == NULL || *offset + 6u > blobLen ||
        blob[*offset] != 0 || blob[*offset + 1u] != 4)
        return false;
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

static bool find_raw_field(const u8 *payload, u16 payloadLen, const char *name,
                           const u8 **valueOut, u16 *valueLenOut)
{
    u32 nameLen = (u32)strlen(name);
    u32 offset = 0;
    while (offset + nameLen + 3u <= payloadLen)
    {
        u8 currentNameLen = payload[offset++];
        if (offset + currentNameLen + 2u > payloadLen)
            return false;
        if (currentNameLen == nameLen &&
            memcmp(payload + offset, name, nameLen) == 0)
        {
            offset += currentNameLen;
            u16 valueLen = (u16)(((u16)payload[offset] << 8) |
                                 payload[offset + 1u]);
            offset += 2u;
            if (offset + valueLen > payloadLen)
                return false;
            if (valueOut)
                *valueOut = payload + offset;
            if (valueLenOut)
                *valueLenOut = valueLen;
            return true;
        }
        offset += currentNameLen;
        u16 valueLen = (u16)(((u16)payload[offset] << 8) |
                             payload[offset + 1u]);
        offset += 2u + valueLen;
    }
    return false;
}

int main(void)
{
    static const u16 expectedSeq[] = { 14, 15 };
    static const u32 expectedItemId[] = { 40085, 6404 };
    static const u32 expectedCount[] = { 1, 3 };
    vm_mock_service_trade_offer receipt;
    const u8 *itemInfo = NULL;
    u16 itemInfoLen = 0;
    u16 objectLen = 0;
    u8 packet[512];
    u32 packetLen = 5;
    u32 blobOffset = 0;

    memset(&receipt, 0, sizeof(receipt));
    memset(packet, 0, sizeof(packet));
    receipt.submitted = true;
    receipt.itemCount = 2;
    for (u32 i = 0; i < receipt.itemCount; ++i)
    {
        receipt.items[i].destinationSeq = expectedSeq[i];
        receipt.items[i].itemId = expectedItemId[i];
        receipt.items[i].count = expectedCount[i];
    }

    if (!vm_net_mock_append_trade_terminal_object(
            packet, sizeof(packet), &packetLen, 8, 1, 925854, &receipt))
    {
        fputs("trade terminal iteminfo regression: builder failed\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(packet, packetLen, 1);
    objectLen = (u16)(((u16)packet[9] << 8) | packet[10]);
    if (packetLen < 11 || packet[4] != 1 || packet[5] != 1 ||
        packet[6] != 21 || packet[7] != 8 || objectLen < 6 ||
        5u + objectLen != packetLen ||
        !find_raw_field(packet + 11, objectLen - 6u, "iteminfo", &itemInfo,
                        &itemInfoLen))
    {
        fputs("trade terminal iteminfo regression: WT21/8 object invalid\n",
              stderr);
        return 1;
    }

    for (u32 i = 0; i < receipt.itemCount; ++i)
    {
        u16 seq = 0;
        u32 itemId = 0;
        u32 count = 0;
        if (!read_tagged_i16(itemInfo, itemInfoLen, &blobOffset, &seq) ||
            !read_tagged_u32(itemInfo, itemInfoLen, &blobOffset, &itemId) ||
            !read_tagged_u32(itemInfo, itemInfoLen, &blobOffset, &count) ||
            seq != expectedSeq[i] || itemId != expectedItemId[i] ||
            count != expectedCount[i])
        {
            fprintf(stderr,
                    "trade terminal iteminfo regression: row %u mismatch "
                    "seq=%u item=%u count=%u offset=%u len=%u\n",
                    i, seq, itemId, count, blobOffset, itemInfoLen);
            return 1;
        }
    }
    if (blobOffset != itemInfoLen || itemInfoLen != 32)
    {
        fprintf(stderr,
                "trade terminal iteminfo regression: trailing bytes "
                "offset=%u len=%u\n",
                blobOffset, itemInfoLen);
        return 1;
    }

    puts("trade terminal iteminfo regression passed: two WT21/8 rows use i16 seq + i32 itemId + i32 count");
    return 0;
}
