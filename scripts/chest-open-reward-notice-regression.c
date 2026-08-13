/*
 * Pure regression for the personal chest-open reward notice.
 *
 * It checks deterministic GBK text framing only.  It does not open a
 * listener, connect to MySQL or change a role, backpack or reward pool.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections
 *       scripts/chest-open-reward-notice-regression.c obj/server/gifDecode.o
 *       obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o
 *       -Wl,--gc-sections -o tmp/chest-open-reward-notice-regression.exe
 *       -lpthread -liconv -lm -lkernel32 -lws2_32
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    static const char rewardGbk[] =
        "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9"; /* 修炼天书 */
    static const char expectedBronze[] =
        "\xBF\xAA\xC6\xF4\xC7\xE0\xCD\xAD\xB1\xA6\xCF\xE4"
        "\xA3\xAC\xBB\xF1\xB5\xC3\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9";
    static const char expectedSilverMany[] =
        "\xBF\xAA\xC6\xF4\xB0\xD7\xD2\xF8\xB1\xA6\xCF\xE4"
        "\xA3\xAC\xBB\xF1\xB5\xC3\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9\xA1\xC1\x31\x32";
    static const char expectedGold[] =
        "\xBF\xAA\xC6\xF4\xBB\xC6\xBD\xF0\xB1\xA6\xCF\xE4"
        "\xA3\xAC\xBB\xF1\xB5\xC3\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9";
    char hint[128];

    if (!vm_net_mock_format_chest_open_reward_hint_gbk(
            522, rewardGbk, 1, hint, sizeof(hint)) ||
        strcmp(hint, expectedBronze) != 0)
    {
        fprintf(stderr, "bronze chest reward notice did not match GBK contract\n");
        return 1;
    }
    if (!vm_net_mock_format_chest_open_reward_hint_gbk(
            523, rewardGbk, 12, hint, sizeof(hint)) ||
        strcmp(hint, expectedSilverMany) != 0)
    {
        fprintf(stderr, "silver chest reward notice did not include quantity\n");
        return 1;
    }
    if (!vm_net_mock_format_chest_open_reward_hint_gbk(
            524, rewardGbk, 1, hint, sizeof(hint)) ||
        strcmp(hint, expectedGold) != 0)
    {
        fprintf(stderr, "gold chest reward notice did not match GBK contract\n");
        return 1;
    }
    puts("chest-open reward-notice regression passed: bronze/silver/gold GBK text");
    return 0;
}
