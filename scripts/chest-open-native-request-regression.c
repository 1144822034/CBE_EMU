/*
 * Regression for the native Jianghu OL chest-open request.
 *
 * This is a pure decoder test: it does not open a listener, connect to MySQL,
 * or change a role.  It locks the exact live packet shape captured from
 * guest00024: WT 7/15 { box: tagged-u16(seq), key: tagged-u16(seq) }.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections
 *       scripts/chest-open-native-request-regression.c obj/server/gifDecode.o
 *       obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o
 *       -Wl,--gc-sections -o tmp/chest-open-native-request-regression.exe
 *       -lpthread -liconv -lm -lkernel32 -lws2_32
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    const u8 request[] = {
        'W', 'T', 0, 29,
        1, 7, 15, 0, 25,
        3, 'b', 'o', 'x', 0, 4, 0, 2, 0, 14,
        3, 'k', 'e', 'y', 0, 4, 0, 2, 0, 15};
    vm_net_mock_chest_open_request parsed;
    u8 requestSubtype = 0;

    memset(&parsed, 0, sizeof(parsed));
    if (!vm_net_mock_parse_chest_open_request(
            request, sizeof(request), &parsed, &requestSubtype) ||
        requestSubtype != 15 || parsed.chestItemId != 0 ||
        parsed.chestSeq != 14 || parsed.keySeq != 15 || parsed.count != 1 ||
        parsed.itemUseType != 1)
    {
        fprintf(stderr, "native chest request did not decode as box/key sequences\n");
        return 1;
    }
    puts("native chest request regression passed: 7/15 box=14 key=15");
    return 0;
}
