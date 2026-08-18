/*
 * Pure regression for the chest-open display-only acquire notice.
 *
 * It exercises the real packet object builder only.  It does not open a
 * listener, connect to MySQL, or mutate a role/backpack.
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

static bool assert_acquire_notice_packet(const u8 *packet, u32 packetLen,
                                         const char *expectedMsg)
{
    const u8 *value = NULL;
    u16 valueLen = 0;
    u32 offset = 11;
    u16 msgLen;

    if (packet == NULL || packetLen < offset || packet[0] != 'W' ||
        packet[1] != 'T' || packet[4] != 1 || packet[5] != 1 ||
        packet[6] != 7 || packet[7] != 37 || packet[8] != 0 ||
        (packet[9] == 0 && packet[10] == 0) ||
        !read_object_field(packet, packetLen, &offset, "msg", &value,
                           &valueLen) || valueLen < 2)
        return false;
    msgLen = (u16)(((u16)value[0] << 8) | value[1]);
    return msgLen == strlen(expectedMsg) &&
           valueLen == (u16)(msgLen + 2u) &&
           memcmp(value + 2, expectedMsg, msgLen) == 0 &&
           read_object_field(packet, packetLen, &offset, "result", &value,
                             &valueLen) &&
           valueLen == 3 && value[0] == 0 && value[1] == 1 && value[2] == 1 &&
           offset == packetLen;
}

static bool assert_chest_packet_without_modal(const u8 *packet, u32 packetLen)
{
    static const u8 expectedKind[] = { 7, 7, 7, 7 };
    static const u8 expectedSubtype[] = { 11, 11, 7, 37 };
    u32 offset = 5;
    u8 objectCount;

    if (packet == NULL || packetLen < offset || packet[0] != 'W' ||
        packet[1] != 'T' || packet[4] != 4)
        return false;
    objectCount = packet[4];
    for (u8 i = 0; i < objectCount; ++i)
    {
        u16 objectLen;

        if (offset + 6u > packetLen || packet[offset] != 1 ||
            packet[offset + 1u] != expectedKind[i] ||
            packet[offset + 2u] != expectedSubtype[i])
        {
            fprintf(stderr, "object[%u] got major=%u kind=%u subtype=%u offset=%u expected=%u/%u\n",
                    i, offset < packetLen ? packet[offset] : 0,
                    offset + 1u < packetLen ? packet[offset + 1u] : 0,
                    offset + 2u < packetLen ? packet[offset + 2u] : 0,
                    offset, expectedKind[i], expectedSubtype[i]);
            return false;
        }
        objectLen = (u16)(((u16)packet[offset + 4u] << 8) |
                          packet[offset + 5u]);
        if (objectLen < 6 || offset + objectLen > packetLen)
            return false;
        offset += objectLen;
    }
    return offset == packetLen;
}

int main(void)
{
    static const char rewardGbk[] = "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9";
    static const char expectedGbk[] =
        "\xBF\xAA\xC6\xF4\xBB\xC6\xBD\xF0\xB1\xA6\xCF\xE4"
        "\xA3\xAC\xBB\xF1\xB5\xC3\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9";
    u8 packet[512];
    u32 packetLen = 5;
    u8 objectCount = 0;
    u8 chestPacket[2048];
    u32 chestPacketLen = 5;
    u8 chestObjectCount = 0;

    memset(packet, 0, sizeof(packet));
    if (!vm_net_mock_append_chest_open_reward_notice_object(
            packet, sizeof(packet), &packetLen, &objectCount, 524, rewardGbk,
            1) || objectCount != 1)
    {
        fputs("chest reward notice object was not built\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(packet, packetLen, objectCount);
    if (!assert_acquire_notice_packet(packet, packetLen, expectedGbk))
    {
        fputs("chest reward notice did not match 1/7/37 display-only GBK contract\n",
              stderr);
        return 1;
    }

    memset(chestPacket, 0, sizeof(chestPacket));
    if (!vm_net_mock_append_backpack_item_count11_object(
            chestPacket, sizeof(chestPacket), &chestPacketLen,
            &chestObjectCount, 238, 524, 0) ||
        !vm_net_mock_append_backpack_item_count11_object(
            chestPacket, sizeof(chestPacket), &chestPacketLen,
            &chestObjectCount, 258, 815, 0) ||
        !vm_net_mock_append_backpack_item_add7_object(
            chestPacket, sizeof(chestPacket), &chestPacketLen, 284, 902, 1))
    {
        fputs("chest inventory response objects were not built\n", stderr);
        return 1;
    }
    ++chestObjectCount;
    if (!vm_net_mock_append_chest_open_reward_notice_object(
            chestPacket, sizeof(chestPacket), &chestPacketLen,
            &chestObjectCount, 524, rewardGbk, 1) || chestObjectCount != 4)
    {
        fputs("chest reward response object was not appended\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(chestPacket, chestPacketLen,
                                 chestObjectCount);
    if (!assert_chest_packet_without_modal(chestPacket, chestPacketLen))
    {
        fputs("chest response still contains a modal 7/1 success ack or has an invalid order\n",
              stderr);
        return 1;
    }
    puts("chest-open reward-notice regression passed: 1/7/37 display-only acquire notice");
    puts("chest-open response regression passed: sequence-only 7/11 consumption; no additive 7/7 type=2 rows");
    return 0;
}
