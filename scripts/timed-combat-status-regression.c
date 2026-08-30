/*
 * Regression for clicking the scene's timed-combat "力" badge.  The client
 * sends exactly one empty WT 1/22/6 object, then its 0x01010F6C handler
 * requires `info` and `ruffianflag` before completing the pending progress
 * dialog.  This fixture performs no listener or database I/O.
 */

#include <stdio.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static u32 build_timed_combat_badge_request(u8 *out, u32 outCap)
{
    if (out == NULL || outCap < 9)
        return 0;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = 0;
    out[3] = 9;
    out[4] = 1;
    out[5] = 22;
    out[6] = 6;
    out[7] = 0;
    out[8] = 5;
    return 9;
}

int main(void)
{
    u8 request[16] = {0};
    u8 response[160] = {0};
    char info[120] = {0};
    u8 kind = 0;
    u8 subtype = 0;
    u8 ruffianFlag = 0xff;
    u32 requestLen = build_timed_combat_badge_request(request, sizeof(request));
    u32 responseLen = vm_net_mock_build_response(request, requestLen,
                                                 response, sizeof(response));

    if (requestLen != 9 || responseLen == 0 || response[4] != 1 ||
        !vm_net_mock_get_first_object_kind_subtype(response, responseLen,
                                                   &kind, &subtype) ||
        kind != 22 || subtype != 6 ||
        !vm_net_mock_get_object_string_field(response, responseLen, "info",
                                             info, sizeof(info)) ||
        info[0] == '\0' ||
        !vm_net_mock_get_object_u8_field(response, responseLen,
                                         "ruffianflag", &ruffianFlag) ||
        ruffianFlag != 0)
    {
        fputs("timed-combat 22/6 status response mismatch\n", stderr);
        return 1;
    }
    if (vm_net_mock_build_response(request, requestLen - 1, response,
                                   sizeof(response)) != 0)
    {
        fputs("timed-combat 22/6 accepted truncated request\n", stderr);
        return 1;
    }
    request[7] = 1;
    if (vm_net_mock_build_response(request, requestLen, response,
                                   sizeof(response)) != 0)
    {
        fputs("timed-combat 22/6 accepted a payload-bearing request\n", stderr);
        return 1;
    }
    puts("timed-combat-status regression passed: empty 22/6 receives one "
         "info + ruffianflag description object");
    return 0;
}
