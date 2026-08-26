/*
 * The chest-capacity failure uses the normal 1/16/2 hint object.  Keep the
 * user-facing text in Chinese without changing the response contract.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool packet_contains(const u8 *packet, u32 packetLen,
                            const char *expected)
{
    size_t expectedLen = expected ? strlen(expected) : 0;

    if (packet == NULL || expectedLen == 0 || expectedLen > packetLen)
        return false;
    for (u32 offset = 0; offset + expectedLen <= packetLen; ++offset)
    {
        if (memcmp(packet + offset, expected, expectedLen) == 0)
            return true;
    }
    return false;
}

int main(void)
{
    static const char expected[] = "背包空间不足，无法获得宝箱奖励";
    u8 packet[256];
    u32 packetLen = 0;

    memset(packet, 0, sizeof(packet));
    packetLen = vm_net_mock_build_item_use_hint_response(
        packet, sizeof(packet), expected);
    if (packetLen == 0 || packet[4] != 1 || packet[5] != 1 ||
        packet[6] != 16 || packet[7] != 2 ||
        !packet_contains(packet, packetLen, expected))
    {
        fputs("chest reward hint regression: Chinese 1/16/2 hint missing\n",
              stderr);
        return 1;
    }
    puts("chest reward hint regression passed: Chinese 1/16/2 hint");
    return 0;
}
