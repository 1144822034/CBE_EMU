/*
 * Resource-only regression for the scene teleport-stone exitinfo catalog.
 *
 * It neither starts a listener nor writes MySQL. The catalog is rebuilt from
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

static bool begin_request_object(u8 *out, u32 outCap, u32 *pos, u8 kind,
                                 u8 subtype, u32 *objectStart)
{
    if (out == NULL || pos == NULL || *pos + 5 > outCap)
        return false;
    if (objectStart)
        *objectStart = *pos;
    out[(*pos)++] = 1;
    out[(*pos)++] = kind;
    out[(*pos)++] = subtype;
    out[(*pos)++] = 0;
    out[(*pos)++] = 0;
    return true;
}

static void finish_request_object(u8 *out, u32 objectStart, u32 pos)
{
    u32 objectLen = pos - objectStart;

    out[objectStart + 3] = (u8)(objectLen >> 8);
    out[objectStart + 4] = (u8)objectLen;
}

static void finish_request_packet(u8 *out, u32 pos)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
}

static bool append_empty_request_object(u8 *out, u32 outCap, u32 *pos,
                                        u8 kind, u8 subtype)
{
    u32 objectStart = 0;

    if (!begin_request_object(out, outCap, pos, kind, subtype, &objectStart))
        return false;
    finish_request_object(out, objectStart, *pos);
    return true;
}

static bool build_exit_select_request(u32 exitId, u8 *out, u32 outCap,
                                      u32 *outLen)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (outLen)
        *outLen = 0;
    if (out == NULL || outLen == NULL || outCap < pos ||
        !begin_request_object(out, outCap, &pos, 0x10, 2, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "exitID", exitId) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 3))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    finish_request_packet(out, pos);
    *outLen = pos;
    return true;
}

/* The scene-stone selection's observed first direct-entry follow-up has no
 * 16/3 current-X ACK; it asks only for the NPC and skill-book catalogs. */
static bool build_scene_stone_direct_catalog_request(u8 *out, u32 outCap,
                                                     u32 *outLen)
{
    u32 pos = 4;

    if (outLen)
        *outLen = 0;
    if (out == NULL || outLen == NULL ||
        !append_empty_request_object(out, outCap, &pos, 0x1b, 11) ||
        !append_empty_request_object(out, outCap, &pos, 7, 42))
    {
        return false;
    }
    finish_request_packet(out, pos);
    *outLen = pos;
    return true;
}

static bool build_scene_resource_completion_request(u8 *out, u32 outCap,
                                                    u32 *outLen)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (outLen)
        *outLen = 0;
    if (out == NULL || outLen == NULL ||
        !append_empty_request_object(out, outCap, &pos, 6, 1) ||
        !append_empty_request_object(out, outCap, &pos, 6, 13) ||
        !append_empty_request_object(out, outCap, &pos, 6, 14) ||
        !begin_request_object(out, outCap, &pos, 2, 10, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "Type", 101))
    {
        return false;
    }
    finish_request_object(out, objectStart, pos);
    if (!append_empty_request_object(out, outCap, &pos, 0x19, 5))
        return false;
    finish_request_packet(out, pos);
    *outLen = pos;
    return true;
}

static bool response_has_object(const u8 *packet, u32 packetLen,
                                u8 kind, u8 subtype)
{
    vm_net_mock_response_object object;
    u32 offset = 5;

    if (packet == NULL || packetLen < 5 || packet[0] != 'W' ||
        packet[1] != 'T')
    {
        return false;
    }
    while (vm_net_mock_next_response_object(packet, packetLen, &offset,
                                             &object))
    {
        if (object.major == 1 && object.kind == kind &&
            object.subtype == subtype)
        {
            return true;
        }
    }
    return false;
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
    vm_net_mock_response_object responseObject;
    u8 exitInfo[4096];
    u8 request[128];
    u8 runtimeFollowup[128];
    u8 resourceFollowup[128];
    u8 response[4096];
    u32 destinationCount = 0;
    u32 exitInfoLen = 0;
    u32 exitInfoCount = 0;
    u32 requestLen = 0;
    u32 runtimeFollowupLen = 0;
    u32 resourceFollowupLen = 0;
    u32 responseLen = 0;
    u32 responseOffset = 5;
    u16 taiyiStoneX = 0;
    u16 taiyiStoneY = 0;
    u16 taiyiLandingX = 0;
    u16 taiyiLandingY = 0;
    const u8 *responseResult = NULL;
    u16 responseResultLen = 0;
    char responseScene[64];
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

    /* Keep an in-memory authenticated role at the selected landing point. The
     * production builder sees the same already-selected role and its location
     * persistence is a no-op, so the regression does not mutate account data. */
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 1;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", taiyiScene);
    g_vm_net_mock_role_db.roles[0].x = taiyiLandingX;
    g_vm_net_mock_role_db.roles[0].y = taiyiLandingY;

    /* A scene-stone choice is free and must use the direct 16/2(result=1)
     * entry.  16/2(result=2) instead means W-coin-insufficient recharge. */
    responseLen = vm_net_mock_build_teleport_stone_transfer_response(
        request, requestLen, 2, response, sizeof(response));
    if (responseLen < 5 || response[4] != 1)
        return fail("scene-stone selection did not return one WT object");
    if (!vm_net_mock_next_response_object(response, responseLen,
                                          &responseOffset, &responseObject) ||
        responseOffset != responseLen)
    {
        return fail("scene-stone selection returned an invalid WT object boundary");
    }
    if (responseObject.major != 1 || responseObject.kind != 0x10 ||
        responseObject.subtype != 2)
    {
        return fail("scene-stone selection did not return the direct 16/2 entry");
    }
    if (!vm_net_mock_response_object_field(&responseObject, "result",
                                           &responseResult, &responseResultLen) ||
        responseResultLen != 3 || responseResult[0] != 0 ||
        responseResult[1] != 1 || responseResult[2] != 1)
    {
        return fail("scene-stone direct entry did not retain 16/2 result=1");
    }
    if (!vm_net_mock_response_object_string(&responseObject, "scene",
                                            responseScene, sizeof(responseScene)) ||
        strcmp(responseScene, taiyiScene) != 0)
    {
        return fail("scene-stone direct entry did not retain the selected scene");
    }
    if (vm_net_mock_response_object_field(&responseObject, "value", NULL, NULL))
    {
        return fail("free scene-stone entry unexpectedly returned an item cost");
    }

    /* This regression owns the wire hand-off, not the database-backed dynamic
     * NPC catalog. A matching already-seeded catalog makes 27/11 take its
     * production empty-repeat branch while still proving that the complete
     * runtime stream is recognized and 7/42 is served. */
    g_vm_net_mock_scene_moveinfo_npc_seeded = true;
    snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
             sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene), "%s",
             taiyiScene);
    g_vm_net_mock_scene_moveinfo_npc_pending = false;
    g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;

    if (!g_vm_net_mock_teleport_stone_direct_enter_pending ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        g_vm_net_mock_last_scene_change_target.sceneCompletionSent)
    {
        return fail("scene-stone direct entry did not keep its target pending");
    }

    if (!build_scene_stone_direct_catalog_request(runtimeFollowup,
                                                  sizeof(runtimeFollowup),
                                                  &runtimeFollowupLen))
    {
        return fail("unable to construct scene-stone direct catalog request");
    }
    responseLen = vm_net_mock_build_teleport_stone_selected_direct_catalog_response(
        runtimeFollowup, runtimeFollowupLen, response, sizeof(response));
    if (responseLen < 5 || response[4] != 2 ||
        !response_has_object(response, responseLen, 0x1b, 11) ||
        !response_has_object(response, responseLen, 7, 42) ||
        !g_vm_net_mock_teleport_stone_direct_enter_pending ||
        !g_vm_net_mock_last_scene_change_target_valid)
    {
        return fail("scene-stone direct entry did not continue its catalog sync");
    }

    if (!build_scene_resource_completion_request(resourceFollowup,
                                                 sizeof(resourceFollowup),
                                                 &resourceFollowupLen) ||
        resourceFollowupLen != 39)
    {
        return fail("unable to construct scene-stone resource completion request");
    }
    responseLen = vm_net_mock_build_scene_resource_followup_response(
        resourceFollowup, resourceFollowupLen, response, sizeof(response));
    if (responseLen < 5 || !response_has_object(response, responseLen,
                                                0x1e, 2) ||
        g_vm_net_mock_teleport_stone_direct_enter_pending ||
        g_vm_net_mock_last_scene_change_target_valid)
    {
        return fail("scene-stone resource completion did not close once");
    }

    printf("teleport-stone-scene-catalog regression passed: "
           "entries=%u taiyi_exit=90 taiyi_stone=(%u,%u)\n",
           destinationCount, taiyiStoneX, taiyiStoneY);
    return 0;
}
