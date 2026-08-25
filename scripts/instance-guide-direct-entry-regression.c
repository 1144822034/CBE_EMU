/*
 * Regression for dynamic NPC instance-guide first-click routing.
 *
 * A guide with an authoritative target scene must expose the existing
 * ENTER_INSTANCE action directly, so the first task-hall action=1 request is
 * resolved by the normal 30/1 scene-enter builder.  A separately configured
 * guard challenge must use the first-dialog action-13 path, avoiding both an
 * instance-operation dialog and the native 30/9 confirmation window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool build_settings_unstuck_16_2_request(u8 *out, u32 outCap,
                                                 u32 *lengthOut)
{
    u32 pos = 4;
    u32 objectStart = 0;

    if (out == NULL || lengthOut == NULL || outCap < 14)
        return false;
    objectStart = pos;
    out[pos++] = 1;
    out[pos++] = 0x10;
    out[pos++] = 2;
    out[pos++] = 0;
    out[pos++] = 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
        return false;
    out[objectStart + 3] = (u8)((pos - objectStart) >> 8);
    out[objectStart + 4] = (u8)(pos - objectStart);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    *lengthOut = pos;
    return true;
}

static int assert_instance_session_ownership(void)
{
    static const char instanceScene[] =
        "b_29\xC3\xCE\xBE\xB3\xBF\xD5\xBC\xE4.sce"; /* 梦境空间 */
    const char *durableScene = vm_net_mock_default_scene_name();
    vm_net_mock_role_state savedRole;
    vm_mock_service_client_session *savedSessions =
        g_vm_mock_service_client_sessions;
    vm_mock_service_client_session *session = NULL;
    vm_net_mock_scene_change_target savedTarget =
        g_vm_net_mock_last_scene_change_target;
    bool savedTargetValid = g_vm_net_mock_last_scene_change_target_valid;
    u32 savedTargetSerial = g_vm_net_mock_last_scene_change_target_serial;
    vm_net_mock_scene_change_target savedCompletedTarget =
        g_vm_net_mock_last_completed_scene_change_target;
    bool savedCompletedTargetValid =
        g_vm_net_mock_last_completed_scene_change_target_valid;
    u32 savedCompletedTargetTick = g_vm_net_mock_last_completed_scene_change_tick;
    bool savedNpcPending = g_vm_net_mock_scene_moveinfo_npc_pending;
    bool savedNpcSeeded = g_vm_net_mock_scene_moveinfo_npc_seeded;
    char savedNpcPendingScene[sizeof(g_vm_net_mock_scene_moveinfo_npc_pending_scene)];
    char savedNpcSeededScene[sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene)];
    u32 savedActiveClientId = g_vm_mock_service_active_client_id;
    u32 savedRoleCount = g_vm_net_mock_role_db.roleCount;
    u32 savedActiveRoleId = g_vm_net_mock_role_db.activeRoleId;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;
    bool savedStoneAck = g_vm_net_mock_teleport_stone_subtype3_ack_sent;
    bool savedStoneDirect = g_vm_net_mock_teleport_stone_direct_enter_pending;
    bool savedStoneMap = g_vm_net_mock_teleport_stone_map_enter_pending;
    vm_net_mock_scene_npcinfo_seed seed;
    u8 request[64];
    u8 response[256];
    u32 responseLen = 0;
    u32 requestLen = 0;
    u16 x = 0;
    u16 y = 0;
    u8 kind = 0;
    u8 subtype = 0;
    u8 responseResult = 0;
    char responseScene[64];
    char responseHint[64];
    int result = 1;

    if (durableScene == NULL || durableScene[0] == 0 ||
        !vm_net_mock_scene_name_is_safe(instanceScene))
    {
        fputs("instance ownership fixture lacks an installed scene resource\n", stderr);
        return 1;
    }

    memcpy(savedNpcPendingScene, g_vm_net_mock_scene_moveinfo_npc_pending_scene,
           sizeof(savedNpcPendingScene));
    memcpy(savedNpcSeededScene, g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
           sizeof(savedNpcSeededScene));

    memset(&savedRole, 0, sizeof(savedRole));
    if (g_vm_net_mock_role_db.roleCount != 0)
        savedRole = g_vm_net_mock_role_db.roles[0];
    memset(&g_vm_net_mock_role_db.roles[0], 0,
           sizeof(g_vm_net_mock_role_db.roles[0]));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 424242u;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 424242u;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", durableScene);
    g_vm_net_mock_role_db.roles[0].x = 172;
    g_vm_net_mock_role_db.roles[0].y = 132;

    g_vm_mock_service_client_sessions = NULL;
    g_vm_mock_service_active_client_id = 0x31415926u;
    session = vm_mock_service_get_or_create_client_session(
        g_vm_mock_service_active_client_id);
    if (session == NULL)
    {
        fputs("instance ownership fixture could not allocate session\n", stderr);
        goto done;
    }
    session->onlineRoleId = 424242u;

    memset(&seed, 0, sizeof(seed));
    seed.actorId = 39991u;
    snprintf(seed.instanceScene, sizeof(seed.instanceScene), "%s", instanceScene);
    seed.instanceX = 50;
    seed.instanceY = 50;
    memset(response, 0, sizeof(response));
    responseLen = vm_net_mock_build_instance_enter_response(
        &seed, response, sizeof(response));
    if (responseLen < 11 || !session->transientInstanceActive ||
        !vm_net_mock_scene_names_equal_exact(session->transientInstanceScene,
                                             instanceScene) ||
        !vm_mock_service_active_transient_instance_position(&x, &y) ||
        x == 0 || y == 0 ||
        !vm_net_mock_scene_names_equal_exact(vm_net_mock_current_scene_name(),
                                             instanceScene) ||
        !vm_net_mock_scene_names_equal_exact(g_vm_net_mock_role_db.roles[0].scene,
                                             durableScene) ||
        g_vm_net_mock_role_db.roles[0].x != 172 ||
        g_vm_net_mock_role_db.roles[0].y != 132)
    {
        fputs("instance entry did not retain the durable ActorInfo anchor\n", stderr);
        goto done;
    }

    vm_net_mock_save_player_pos_state(instanceScene, 76, 80,
                                      "instance-ownership-regression-move");
    if (!vm_mock_service_active_transient_instance_position(&x, &y) ||
        (x == 50 && y == 50) ||
        !vm_net_mock_scene_names_equal_exact(g_vm_net_mock_role_db.roles[0].scene,
                                             durableScene) ||
        g_vm_net_mock_role_db.roles[0].x != 172 ||
        g_vm_net_mock_role_db.roles[0].y != 132)
    {
        fputs("instance move leaked into the durable role anchor\n", stderr);
        goto done;
    }
    if (!vm_net_mock_role_set_timeline_position(
            instanceScene, 84, 88, "instance-ownership-regression-timeline") ||
        !vm_mock_service_active_transient_instance_position(&x, &y) ||
        x != 84 || y != 88 ||
        !vm_net_mock_scene_names_equal_exact(g_vm_net_mock_role_db.roles[0].scene,
                                             durableScene) ||
        g_vm_net_mock_role_db.roles[0].x != 172 ||
        g_vm_net_mock_role_db.roles[0].y != 132)
    {
        fputs("instance movement timeline leaked into the durable role anchor\n", stderr);
        goto done;
    }

    /* Reproduce player-3's completed-instance compact settings action.  The
     * client enters WT16/2(result=1) through the direct scene-entry API; a
     * same-instance response reuses its live background Actor array and
     * faults at 0x0100DA4E.  mmGame:sub_11CE's parser-backed result=4 + hint
     * branch closes the menu wait without rebuilding that live scene shell. */
    session->sceneVisibleReady = true;
    session->sceneVisiblePending = false;
    snprintf(session->sceneVisibleScene, sizeof(session->sceneVisibleScene), "%s",
             instanceScene);
    session->sceneVisibleX = 84;
    session->sceneVisibleY = 88;
    memset(request, 0, sizeof(request));
    memset(response, 0, sizeof(response));
    memset(responseScene, 0, sizeof(responseScene));
    memset(responseHint, 0, sizeof(responseHint));
    if (!build_settings_unstuck_16_2_request(request, sizeof(request), &requestLen) ||
        !vm_net_mock_is_settings_unstuck_16_2_request(request, requestLen) ||
        (responseLen = vm_net_mock_build_settings_unstuck_16_2_response(
             request, requestLen, response, sizeof(response))) == 0 ||
        !vm_net_mock_get_first_object_kind_subtype(response, responseLen,
                                                   &kind, &subtype) ||
        kind != 0x10 || subtype != 2 ||
        !vm_net_mock_get_object_u8_field(response, responseLen, "result",
                                         &responseResult) ||
        responseResult != 4 ||
        !vm_net_mock_get_object_string_field(response, responseLen, "hint",
                                             responseHint, sizeof(responseHint)) ||
        strcmp(responseHint,
               "\xb8\xb1\xb1\xbe\xc4\xda\xce\xde\xb7\xa8\xca\xb9\xd3\xc3\xcd\xd1\xc0\xeb\xbf\xa8\xcb\xc0") != 0 ||
        vm_net_mock_get_object_string_field(response, responseLen, "scene",
                                            responseScene, sizeof(responseScene)) ||
        !session->transientInstanceActive ||
        !vm_mock_service_active_transient_instance_position(&x, &y) ||
        x != 84 || y != 88 ||
        !vm_net_mock_scene_names_equal_exact(g_vm_net_mock_role_db.roles[0].scene,
                                             durableScene) ||
        g_vm_net_mock_role_db.roles[0].x != 172 ||
        g_vm_net_mock_role_db.roles[0].y != 132)
    {
        fputs("completed-instance settings WT16/2 did not preserve its live instance\n",
              stderr);
        goto done;
    }

    vm_mock_service_session_mark_offline(session, "instance-ownership-regression");
    if (session->transientInstanceActive ||
        vm_mock_service_active_transient_instance_scene() != NULL ||
        !vm_net_mock_scene_names_equal_exact(vm_net_mock_current_scene_name(),
                                             durableScene) ||
        !vm_net_mock_scene_names_equal_exact(vm_net_mock_scene_key_name(),
                                             durableScene) ||
        g_vm_net_mock_role_db.roles[0].x != 172 ||
        g_vm_net_mock_role_db.roles[0].y != 132)
    {
        fputs("disconnect did not restore durable ActorInfo bootstrap ownership\n",
              stderr);
        goto done;
    }

    result = 0;

done:
    g_vm_mock_service_active_client_id = savedActiveClientId;
    g_vm_mock_service_client_sessions = savedSessions;
    free(session);
    g_vm_net_mock_role_db.roles[0] = savedRole;
    g_vm_net_mock_role_db.roleCount = savedRoleCount;
    g_vm_net_mock_role_db.activeRoleId = savedActiveRoleId;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    g_vm_net_mock_last_scene_change_target = savedTarget;
    g_vm_net_mock_last_scene_change_target_valid = savedTargetValid;
    g_vm_net_mock_last_scene_change_target_serial = savedTargetSerial;
    g_vm_net_mock_last_completed_scene_change_target = savedCompletedTarget;
    g_vm_net_mock_last_completed_scene_change_target_valid = savedCompletedTargetValid;
    g_vm_net_mock_last_completed_scene_change_tick = savedCompletedTargetTick;
    g_vm_net_mock_scene_moveinfo_npc_pending = savedNpcPending;
    g_vm_net_mock_scene_moveinfo_npc_seeded = savedNpcSeeded;
    memcpy(g_vm_net_mock_scene_moveinfo_npc_pending_scene, savedNpcPendingScene,
           sizeof(savedNpcPendingScene));
    memcpy(g_vm_net_mock_scene_moveinfo_npc_seeded_scene, savedNpcSeededScene,
           sizeof(savedNpcSeededScene));
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = savedStoneAck;
    g_vm_net_mock_teleport_stone_direct_enter_pending = savedStoneDirect;
    g_vm_net_mock_teleport_stone_map_enter_pending = savedStoneMap;
    return result;
}

int main(void)
{
    vm_net_mock_scene_npcinfo_seed seed;
    const char *name = NULL;
    const char *description = NULL;
    u8 response[256];
    u32 responseLen = 0;
    u32 value = 0;
    const u32 actorId = 39991u;

    memset(&seed, 0, sizeof(seed));
    seed.actorId = actorId;
    snprintf(seed.instanceScene, sizeof(seed.instanceScene), "%s",
             "b_29\xC3\xCE\xBE\xB3\xBF\xD5\xBC\xE4.sce"); /* 梦境空间 */
    seed.instanceX = 50;
    seed.instanceY = 50;
    seed.instanceSpawnEnemyId = 205;
    seed.challengeEnemyId = 105;
    if (assert_instance_session_ownership() != 0)
        return 1;
    if (!vm_net_mock_npc_service_option_default(
            &seed, VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE, &name, &description,
            &value) ||
        value != (VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE | actorId) ||
        name == NULL || description == NULL)
    {
        fputs("configured instance guide did not route first selection to direct entry\n",
              stderr);
        return 1;
    }
    memset(response, 0, sizeof(response));
    /* Runtime entry must reject an advertised kind-3 target until the exact
     * target scene resource contains the deployed row. */
    responseLen = vm_net_mock_build_instance_enter_response(
        &seed, response, sizeof(response));
    if (responseLen != 0)
    {
        fputs("undeployed instance spawn target unexpectedly entered scene\n",
              stderr);
        return 1;
    }
    seed.instanceSpawnEnemyId = 0;
    /* A direct NPC instance entry is not a map-stone route.  Seed stale
     * map-stone provenance to ensure the builder clears it before the first
     * destination WT2/3 is dispatched. */
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = true;
    g_vm_net_mock_teleport_stone_direct_enter_pending = true;
    g_vm_net_mock_teleport_stone_map_enter_pending = true;
    responseLen = vm_net_mock_build_instance_enter_response(
        &seed, response, sizeof(response));
    /* Response objects use the client event-packet layout: the count is at
     * byte 4 and the first object's major/kind/subtype are 5/6/7. */
    if (responseLen < 11 || response[0] != 'W' || response[1] != 'T' ||
        response[4] != 1 || response[5] != 1 || response[6] != 30 ||
        response[7] != 1)
    {
        fputs("direct instance entry did not produce one 30/1 scene response\n",
              stderr);
        return 1;
    }
    if (g_vm_net_mock_teleport_stone_subtype3_ack_sent ||
        g_vm_net_mock_teleport_stone_direct_enter_pending ||
        g_vm_net_mock_teleport_stone_map_enter_pending)
    {
        fputs("direct instance entry leaked map-stone completion provenance\n",
              stderr);
        return 1;
    }

    value = 0;
    if (!vm_net_mock_npc_service_option_default(
            &seed, VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE, &name, &description,
            &value) ||
        value != (VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE | actorId) ||
        !vm_net_mock_npc_service_is_direct_instance_challenge(
            &seed, VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE) ||
        vm_net_mock_npc_service_is_direct_instance_challenge(
            &seed, VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE))
    {
        fputs("dedicated guard challenge did not use direct action-13 routing\n",
              stderr);
        return 1;
    }

    puts("instance guide direct-entry regression passed: direct 26/1 entry + action-13 guard challenge");
    return 0;
}
