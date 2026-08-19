/*
 * Pure regression for the native chest-open completion and reward notice.
 *
 * It exercises the real WT object builders without opening a listener,
 * connecting to MySQL, or mutating a role/backpack.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections -w
 *       scripts/chest-open-reward-notice-regression.c obj/client/gifDecode.o
 *       obj/client/cbeParser.o obj/client/mystd.o obj/client/fontEngine.o
 *       obj/client/vmMalloc.o obj/client/fileIoEngine.o obj/client/lcd.o
 *       obj/client/automation_png.o obj/client/md5.o obj/server/mysql-client.o
 *       -Wl,--gc-sections -o tmp/chest-open-reward-notice-regression.exe
 *       -lpthread -liconv -lm -lmingw32 -lkernel32 -lws2_32
 *       Lib/unicorn-2.1.4/unicorn-import.lib -LLib/sdl2-2.0.10/lib
 *       -lSDL2main -lSDL2
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
        return false;
    nameLen = packet[(*offset)++];
    if (*offset + nameLen + 2u > packetLen ||
        nameLen != strlen(expectedName) ||
        memcmp(packet + *offset, expectedName, nameLen) != 0)
        return false;
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

static bool read_tagged_u8(const u8 *blob, u16 blobLen, u32 *offset,
                           u8 *valueOut)
{
    if (blob == NULL || offset == NULL || *offset + 3u > blobLen ||
        blob[*offset] != 0 || blob[*offset + 1u] != 1)
        return false;
    if (valueOut)
        *valueOut = blob[*offset + 2u];
    *offset += 3u;
    return true;
}

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

static bool assert_native_chest_packet(const u8 *packet, u32 packetLen)
{
    static const u8 expectedSubtype[] = { 4, 11, 11, 15 };
    u32 offset = 5;

    if (packet == NULL || packetLen < offset || packet[0] != 'W' ||
        packet[1] != 'T' || packet[4] != 4)
        return false;
    for (u8 i = 0; i < packet[4]; ++i)
    {
        u16 objectLen;

        if (offset + 6u > packetLen || packet[offset] != 1 ||
            packet[offset + 1u] != 7 ||
            packet[offset + 2u] != expectedSubtype[i])
            return false;
        objectLen = (u16)(((u16)packet[offset + 4u] << 8) |
                          packet[offset + 5u]);
        if (objectLen < 6 || offset + objectLen > packetLen)
            return false;
        if (i == 0)
        {
            const u8 *result = NULL;
            u16 resultLen = 0;
            u32 fieldOffset = offset + 6u;

            if (!read_object_field(packet, offset + objectLen, &fieldOffset,
                                   "result", &result, &resultLen) ||
                resultLen != 3 || result[0] != 0 || result[1] != 1 ||
                result[2] != 1 || fieldOffset != offset + objectLen)
                return false;
        }
        if (i == 3)
        {
            const u8 *result = NULL;
            const u8 *total = NULL;
            const u8 *itemInfo = NULL;
            u16 resultLen = 0;
            u16 totalLen = 0;
            u16 itemInfoLen = 0;
            u32 fieldOffset = offset + 6u;
            u32 blobOffset = 0;
            u32 itemId = 0;
            u32 count = 0;
            u16 seq = 0;
            u16 enhanceLevel = 0;
            u16 enhanceMax = 0;
            u8 attrCount = 0;

            if (!read_object_field(packet, offset + objectLen, &fieldOffset,
                                   "result", &result, &resultLen) ||
                resultLen != 3 || result[0] != 0 || result[1] != 1 ||
                result[2] != 1 ||
                !read_object_field(packet, offset + objectLen, &fieldOffset,
                                   "total", &total, &totalLen) ||
                totalLen != 3 || total[0] != 0 || total[1] != 1 ||
                total[2] != 1 ||
                !read_object_field(packet, offset + objectLen, &fieldOffset,
                                   "iteminfo", &itemInfo, &itemInfoLen) ||
                fieldOffset != offset + objectLen || itemInfoLen == 0 ||
                !read_tagged_u32(itemInfo, itemInfoLen, &blobOffset, &itemId) ||
                !read_tagged_i16(itemInfo, itemInfoLen, &blobOffset, &seq) ||
                !read_tagged_u32(itemInfo, itemInfoLen, &blobOffset, &count) ||
                !read_tagged_i16(itemInfo, itemInfoLen, &blobOffset,
                                 &enhanceLevel) ||
                !read_tagged_i16(itemInfo, itemInfoLen, &blobOffset,
                                 &enhanceMax) ||
                !read_tagged_u8(itemInfo, itemInfoLen, &blobOffset,
                                &attrCount) ||
                itemId != 902 || seq != 284 || count != 3 ||
                enhanceLevel != 0 || enhanceMax != 0 || attrCount != 0 ||
                blobOffset != itemInfoLen)
            {
                return false;
            }
        }
        offset += objectLen;
    }
    return offset == packetLen;
}

int main(void)
{
    vm_net_mock_backpack_item_state rewardItem;
    u8 packet[2048];
    u32 packetLen = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;

    memset(&rewardItem, 0, sizeof(rewardItem));
    rewardItem.itemId = 902;
    rewardItem.seq = 284;
    /* The native row carries this acquisition's delta, not the projected
     * stack total used by server persistence. */
    rewardItem.count = 99;
    memset(packet, 0, sizeof(packet));

    if (!vm_net_mock_begin_wt_object(packet, sizeof(packet), &packetLen,
                                     1, 7, 4, &objectStart) ||
        !vm_net_mock_put_object_u8(packet, sizeof(packet), &packetLen,
                                   "result", 1))
    {
        fputs("chest completion object was not built\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_object(packet, objectStart, packetLen);
    ++objectCount;
    if (!vm_net_mock_append_backpack_item_count11_object(
            packet, sizeof(packet), &packetLen, &objectCount, 238, 524, 0) ||
        !vm_net_mock_append_backpack_item_count11_object(
            packet, sizeof(packet), &packetLen, &objectCount, 258, 815, 0) ||
        !vm_net_mock_append_chest_open_reward15_object(
            packet, sizeof(packet), &packetLen, &objectCount, &rewardItem, 3) ||
        objectCount != 4)
    {
        fputs("native chest reward objects were not built\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(packet, packetLen, objectCount);
    if (!assert_native_chest_packet(packet, packetLen))
    {
        fputs("native chest response contract is invalid\n", stderr);
        return 1;
    }

    puts("chest-open reward-notice regression passed: 7/4 completion + sequence-only 7/11 consumption + native 7/15 reward/prompt");
    return 0;
}
