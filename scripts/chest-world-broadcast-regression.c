/*
 * Pure regression for the reward-level chest world-broadcast message.
 *
 * It checks only deterministic server helpers: GBK framing, the golden-chest
 * identity and quantity suffix.  It neither opens a listener nor connects to
 * MySQL, so it cannot change a role, chest pool or world-chat history.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections
 *       scripts/chest-world-broadcast-regression.c obj/client/gifDecode.o
 *       obj/client/cbeParser.o obj/client/mystd.o obj/client/fontEngine.o
 *       obj/client/vmMalloc.o obj/client/fileIoEngine.o obj/client/lcd.o
 *       obj/client/automation_png.o obj/client/md5.o obj/server/mysql-client.o
 *       -Wl,--gc-sections -o tmp/chest-world-broadcast-regression.exe
 *       -lpthread -liconv -lm -lmingw32 -lkernel32 -lws2_32
 *       Lib/unicorn-2.1.4/unicorn-import.lib
 *       -LLib/sdl2-2.0.10/lib -lSDL2main -lSDL2
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    static const char openerGbk[] = "\xD0\xA1\xBA\xDA"; /* 小黑 */
    static const char rewardGbk[] =
        "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9"; /* 修炼天书 */
    static const char expectedOneGbk[] =
        "\xB9\xA7\xCF\xB2\xCD\xE6\xBC\xD2\xA1\xBE\xD0\xA1\xBA\xDA"
        "\xA1\xBF\xBF\xAA\xC6\xF4\xBB\xC6\xBD\xF0\xB1\xA6\xCF\xE4"
        "\xBB\xF1\xB5\xC3\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9";
    static const char expectedManyGbk[] =
        "\xB9\xA7\xCF\xB2\xCD\xE6\xBC\xD2\xA1\xBE\xD0\xA1\xBA\xDA"
        "\xA1\xBF\xBF\xAA\xC6\xF4\xBB\xC6\xBD\xF0\xB1\xA6\xCF\xE4"
        "\xBB\xF1\xB5\xC3\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9\xA1\xC1\x33";
    char message[256];
    char source[82];
    char wireMessage[VM_MOCK_CHAT_MESSAGE_MAX_BYTES + 1];
    size_t wireLen = 0;
    bool truncated = false;

    memset(message, 0, sizeof(message));
    if (strcmp(vm_net_mock_chest_world_broadcast_name_gbk(524),
               "\xBB\xC6\xBD\xF0\xB1\xA6\xCF\xE4") != 0 ||
        !vm_mock_world_chat_build_chest_reward_message(
            openerGbk, vm_net_mock_chest_world_broadcast_name_gbk(524),
            rewardGbk, 1, message, sizeof(message)) ||
        strcmp(message, expectedOneGbk) != 0)
    {
        fprintf(stderr, "single golden-chest world broadcast did not match GBK contract\n");
        return 1;
    }
    if (!vm_mock_world_chat_build_chest_reward_message(
            openerGbk, vm_net_mock_chest_world_broadcast_name_gbk(524),
            rewardGbk, 3, message, sizeof(message)) ||
        strcmp(message, expectedManyGbk) != 0)
    {
        fprintf(stderr, "multi-count chest broadcast did not include quantity\n");
        return 1;
    }

    memset(source, 'A', 79);
    source[79] = 0;
    wireLen = vm_mock_chat_copy_wire_message(
        wireMessage, sizeof(wireMessage), source, &truncated);
    if (wireLen != 79 || truncated || wireMessage[79] != 0)
    {
        fprintf(stderr, "79-byte chat message did not preserve its terminator\n");
        return 1;
    }

    memset(source, 'B', 81);
    source[81] = 0;
    wireLen = vm_mock_chat_copy_wire_message(
        wireMessage, sizeof(wireMessage), source, &truncated);
    if (wireLen != 79 || !truncated || wireMessage[79] != 0)
    {
        fprintf(stderr, "81-byte legacy chat message was not normalized to 79 bytes\n");
        return 1;
    }

    memset(source, 'C', 78);
    source[78] = (char)0xD0;
    source[79] = (char)0xDE;
    source[80] = 0;
    wireLen = vm_mock_chat_copy_wire_message(
        wireMessage, sizeof(wireMessage), source, &truncated);
    if (wireLen != 78 || !truncated || wireMessage[78] != 0)
    {
        fprintf(stderr, "GBK chat normalization split a two-byte character\n");
        return 1;
    }

    puts("chest world-broadcast regression passed: golden template + 79-byte GBK-safe wire limit");
    return 0;
}
