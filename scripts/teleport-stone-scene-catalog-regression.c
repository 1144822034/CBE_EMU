/*
 * Resource-only regression for the scene teleport-stone exitinfo catalog.
 *
 * It neither starts a listener nor opens MySQL.  The catalog is rebuilt from
 * the same SCE directory and sMap.dsh resources as the production 16/1 and
 * 16/2 handlers.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static int fail(const char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    return 1;
}

static bool build_exit_select_request(u32 exitId, u8 *out, u32 outCap,
                                      u32 *outLen)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (outLen)
        *outLen = 0;
    if (out == NULL || outLen == NULL || outCap < pos ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x10, 2,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "exitID", exitId))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    *outLen = pos;
    return true;
}

int main(void)
{
    static const char taiyiScene[] =
        "11\xD6\xD5\xC4\xCF\xC9\xBD_02.sce"; /* 11终南山_02.sce */
    static const char penglai04Scene[] =
        "00\xC5\xEE\xC0\xB3\xCF\xC9\xB5\xBA_04.sce"; /* 00蓬莱仙岛_04.sce */
    vm_net_mock_teleport_stone_destination destinations[
        VM_NET_MOCK_TELEPORT_STONE_DESTINATION_MAX];
    vm_net_mock_scene_change_target target;
    u8 exitInfo[4096];
    u8 request[128];
    u32 destinationCount = 0;
    u32 exitInfoLen = 0;
    u32 exitInfoCount = 0;
    u32 requestLen = 0;
    u16 taiyiStoneX = 0;
    u16 taiyiStoneY = 0;
    u16 taiyiLandingX = 0;
    u16 taiyiLandingY = 0;
    bool taiyiFound = false;
    bool penglai04Found = false;

    destinationCount = vm_net_mock_collect_teleport_stone_destinations(
        destinations, sizeof(destinations) / sizeof(destinations[0]));
    if (destinationCount == 0 ||
        destinationCount >= VM_NET_MOCK_TELEPORT_STONE_DESTINATION_MAX)
    {
        return fail("expected a non-empty bounded map-backed teleport catalog");
    }
    for (u32 i = 0; i < destinationCount; ++i)
    {
        if (!vm_net_mock_death_respawn_scene_has_teleport_stone(
                destinations[i].scene))
        {
            return fail("catalog exposed a scene without the n_telestone actor");
        }
        if (!destinations[i].hasSmapRow || destinations[i].exitId == 0 ||
            destinations[i].stoneX == 0 || destinations[i].stoneY == 0)
        {
            return fail("catalog exposed a scene without an sMap row or stone anchor");
        }
        if (strcmp(destinations[i].scene, taiyiScene) == 0)
        {
            if (destinations[i].exitId != 90 ||
                !destinations[i].hasSmapRow ||
                strcmp(destinations[i].label,
                       "\xCC\xAB\xD2\xD2\xB7\xE5") != 0) /* 太乙峰 */
            {
                return fail("Taiyi Peak did not retain its sMap id and alias");
            }
            taiyiStoneX = destinations[i].stoneX;
            taiyiStoneY = destinations[i].stoneY;
            taiyiFound = true;
        }
        if (strcmp(destinations[i].scene, penglai04Scene) == 0)
        {
            penglai04Found = true;
        }
    }
    if (!taiyiFound)
        return fail("known map-backed teleport-stone scene is missing from the catalog");
    if (penglai04Found)
        return fail("unmapped teleport-stone scene must not be in the catalog");
    taiyiLandingX = taiyiStoneX;
    taiyiLandingY = taiyiStoneY;
    vm_net_mock_adjust_safe_player_pos_for_scene(
        taiyiScene, &taiyiLandingX, &taiyiLandingY);

    if (!vm_net_mock_build_teleport_stone_exitinfo_blob(
            exitInfo, sizeof(exitInfo), &exitInfoLen, &exitInfoCount))
    {
        return fail("16/1 exitinfo builder rejected the map-backed catalog");
    }
    if (exitInfoLen < 3 || exitInfoCount != destinationCount ||
        exitInfo[0] != 0 || exitInfo[1] != 1 ||
        exitInfo[2] != (u8)destinationCount)
    {
        return fail("16/1 exitinfo metadata does not match the catalog");
    }

    memset(&target, 0, sizeof(target));
    if (!vm_net_mock_get_teleport_stone_catalog_target(90, &target) ||
        strcmp(target.scene, taiyiScene) != 0 || target.exitId != 90 ||
        target.x != taiyiLandingX || target.y != taiyiLandingY)
    {
        return fail("Taiyi Peak list id did not resolve to its stone anchor");
    }

    if (!build_exit_select_request(90, request, sizeof(request), &requestLen))
        return fail("unable to construct a 16/2 scene-stone selection request");
    memset(&target, 0, sizeof(target));
    if (!vm_net_mock_get_teleport_stone_target(request, requestLen, &target) ||
        strcmp(target.scene, taiyiScene) != 0 || target.exitId != 90 ||
        target.x != taiyiLandingX || target.y != taiyiLandingY)
    {
        return fail("16/2 selection did not use its teleport-stone anchor");
    }

    printf("teleport-stone-scene-catalog regression passed: "
           "entries=%u taiyi_exit=90 taiyi_stone=(%u,%u)\n",
           destinationCount, taiyiStoneX, taiyiStoneY);
    return 0;
}
