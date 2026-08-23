/*
 * Pure resource/parser regression for the native 05上古皇陵_02 hangup target.
 *
 * No server is started and no database/client state is opened.  The test
 * decodes the same server resource root used by the production selector and
 * verifies that the three native actor-id 25 records remain discoverable by
 * the production SCE2 kind-3 parser.  A hangup 4/5 must only use one of
 * these live scene nodes; replacing the target with a 4/10 player template
 * is not a valid recovery for a missing match.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static int verify_client_prefix(void)
{
    const char scene[] =
        "\x30\x30\xC5\xEE\xC0\xB3\xCF\xC9\xB5\xBA\x5F\x30\x32\x2E\x73\x63\x65";
        /* 00蓬莱仙岛_02.sce */
    u8 data[8192];
    u32 len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    u32 props = 0;
    u32 countOffset = 0;

    if (len == 0 ||
        !vm_net_mock_parse_sce2_client_prefix(data, len, &props,
                                               &countOffset) ||
        props != 4 || countOffset != 100)
    {
        fprintf(stderr,
                "native client-prefix contract mismatch: len=%u props=%u count=%u\n",
                len, props, countOffset);
        return 1;
    }
    printf("native-scene-hangup-spawn-v1 client-prefix passed: "
           "scene=00蓬莱仙岛_02.sce props=%u count=%u\n", props,
           countOffset);
    return 0;
}

int main(void)
{
    const char scene[] = "05\xC9\xCF\xB9\xC5\xBB\xCA\xC1\xEA_02.sce"; /* 05上古皇陵_02.sce */
    u8 data[8192];
    u32 len = 0;
    u32 countOffset = 0;
    u32 propCount = 0;
    u32 kind3Count = 0;
    u32 targetCount = 0;

    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData"))
    {
        fputs("unable to select isolated server resource root\n", stderr);
        return 1;
    }
    if (verify_client_prefix() != 0)
        return 1;
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    if (len == 0 ||
        !vm_net_mock_parse_sce2_client_prefix(data, len, &propCount,
                                               &countOffset))
    {
        fprintf(stderr, "native SCE2 client prefix was not parsed: len=%u\n",
                len);
        return 1;
    }

    for (u32 combatOrdinal = 0; combatOrdinal < 256u; ++combatOrdinal)
    {
        vm_net_mock_sce_combat_spawn spawn;

        if (!vm_net_mock_scene_battle_monster_counted_spawn_at(
                data, len, combatOrdinal, &spawn, NULL))
            break;
        ++kind3Count;
        if (spawn.actorId == 25)
        {
            ++targetCount;
            printf("native-scene-hangup-spawn-v1 target actor=%u pos=(%u,%u) "
                   "actor=%s effect=%s\n",
                   spawn.actorId, spawn.x, spawn.y, spawn.actorResource,
                   spawn.effectResource);
        }
    }

    if (countOffset != 40 || propCount != 0 || kind3Count != 4 ||
        targetCount != 3)
    {
        fprintf(stderr,
                "native 05 hangup scene contract mismatch: count=%u props=%u kind3=%u actor25=%u\n",
                countOffset, propCount, kind3Count, targetCount);
        return 1;
    }
    printf("native-scene-hangup-spawn-v1 passed: scene=05上古皇陵_02.sce "
           "props=%u kind3=%u actor25=%u\n",
           propCount, kind3Count, targetCount);
    return 0;
}
