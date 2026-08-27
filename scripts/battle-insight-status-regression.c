/*
 * Regression for the scene badge click after a battle-insight effect is
 * visible. The observed client request is exactly WT 1/7/36 with no body.
 * The CBE handler at 0x01011A1E requires `bookinfo`; without a response the
 * native progress dialog remains pending. This fixture uses no listener or
 * database writes.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static u32 build_badge_request(u8 *out, u32 outCap)
{
    if (out == NULL || outCap < 9)
        return 0;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = 0;
    out[3] = 9;
    out[4] = 1;
    out[5] = 7;
    out[6] = 36;
    out[7] = 0;
    out[8] = 5;
    return 9;
}

static bool response_bookinfo_has_terminator(const u8 *packet, u32 packetLen,
                                              const char *expected)
{
    const u32 expectedLen = (u32)strlen(expected);
    const u32 nameLen = 8; /* strlen("bookinfo") */
    u32 objectLen;
    u32 payloadPos;
    u32 objectEnd;
    u16 entryLen;
    u16 innerLen;

    if (packet == NULL || expected == NULL || packetLen < 24 ||
        packet[4] != 1 || packet[5] != 1 || packet[6] != 7 ||
        packet[7] != 36)
    {
        return false;
    }
    objectLen = ((u32)packet[9] << 8) | packet[10];
    objectEnd = 5u + objectLen;
    payloadPos = 11;
    if (objectLen < 6 || objectEnd != packetLen ||
        payloadPos + 1u + nameLen + 4u > objectEnd ||
        packet[payloadPos] != nameLen ||
        memcmp(packet + payloadPos + 1u, "bookinfo", nameLen) != 0)
    {
        return false;
    }
    payloadPos += 1u + nameLen;
    entryLen = (u16)(((u16)packet[payloadPos] << 8) | packet[payloadPos + 1u]);
    innerLen = (u16)(((u16)packet[payloadPos + 2u] << 8) |
                     packet[payloadPos + 3u]);
    payloadPos += 4u;
    return entryLen == innerLen + 2u && innerLen == expectedLen + 1u &&
           payloadPos + innerLen == objectEnd &&
           memcmp(packet + payloadPos, expected, expectedLen) == 0 &&
           packet[payloadPos + expectedLen] == 0;
}

int main(void)
{
    u8 request[16] = {0};
    u8 response[128] = {0};
    char bookInfo[96] = {0};
    u8 kind = 0;
    u8 subtype = 0;
    u32 requestLen = build_badge_request(request, sizeof(request));
    u32 responseLen = vm_net_mock_build_response(request, requestLen,
                                                 response, sizeof(response));

    if (vm_net_mock_battle_insight_status_info(true)[0] == '\0' ||
        vm_net_mock_battle_insight_status_info(false)[0] == '\0')
    {
        fputs("battle-insight 7/36 status text mapping mismatch\n", stderr);
        return 1;
    }
    if (requestLen != 9 || responseLen == 0 || response[4] != 1 ||
        !vm_net_mock_get_first_object_kind_subtype(response, responseLen,
                                                   &kind, &subtype) ||
        kind != 7 || subtype != 36 ||
        !vm_net_mock_get_object_string_field(response, responseLen,
                                             "bookinfo", bookInfo,
                                             sizeof(bookInfo)) ||
        bookInfo[0] == '\0')
    {
        fputs("battle-insight 7/36 status response mismatch\n", stderr);
        return 1;
    }
    if (!response_bookinfo_has_terminator(
            response, responseLen,
            vm_net_mock_battle_insight_status_info(false)))
    {
        fputs("battle-insight 7/36 bookinfo lacks its required NUL terminator\n",
              stderr);
        return 1;
    }
    if (vm_net_mock_build_response(request, requestLen - 1, response,
                                   sizeof(response)) != 0)
    {
        fputs("battle-insight 7/36 accepted truncated request\n", stderr);
        return 1;
    }
    puts("battle-insight-status regression passed: 7/36 receives one "
         "NUL-terminated bookinfo description object");
    return 0;
}
