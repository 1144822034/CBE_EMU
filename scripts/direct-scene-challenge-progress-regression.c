/* Regression for the direct current-scene action13 battle lifecycle.
 *
 * This runs the server's real request builders in one process. It does not
 * start a listener or VM: the assertions cover the packet/state contract
 * under which action13's live scene node stays owned by mmBattle. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool build_action13_request(u8 *out, u32 outCap, u32 enemyId,
                                   u32 sceneIndex, u32 *lengthOut)
{
    u32 pos = 9;
    u32 objectStart = 4;

    if (out == NULL || lengthOut == NULL || outCap < pos || enemyId == 0 ||
        sceneIndex == 0 || sceneIndex >= 25)
    {
        return false;
    }
    memset(out, 0, outCap);
    out[0] = 'W';
    out[1] = 'T';
    out[objectStart] = 1;
    out[objectStart + 1] = 4;
    out[objectStart + 2] = 1;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "id", enemyId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "index", sceneIndex) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "posx", 0) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "posy", 0))
    {
        return false;
    }
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[objectStart + 3] = (u8)((pos - objectStart) >> 8);
    out[objectStart + 4] = (u8)(pos - objectStart);
    *lengthOut = pos;
    return true;
}

static bool response_has_exact_objects(const u8 *response, u32 responseLen,
                                       u8 firstKind, u8 firstSubtype,
                                       u8 secondKind, u8 secondSubtype)
{
    vm_net_mock_response_object object;
    u32 offset = 5;

    memset(&object, 0, sizeof(object));
    return response != NULL && responseLen >= 5 && response[0] == 'W' &&
           response[1] == 'T' &&
           vm_net_mock_next_response_object(response, responseLen, &offset,
                                            &object) &&
           object.major == 1 && object.kind == firstKind &&
           object.subtype == firstSubtype &&
           vm_net_mock_next_response_object(response, responseLen, &offset,
                                            &object) &&
           object.major == 1 && object.kind == secondKind &&
           object.subtype == secondSubtype && offset == responseLen;
}

int main(void)
{
    static const char penglaiScene[] =
        "00\xC5\xEE\xC0\xB3\xCF\xC9\xB5\xBA_02.sce";
    vm_net_mock_role_state savedRole;
    vm_mock_service_client_session *savedSessions =
        g_vm_mock_service_client_sessions;
    vm_mock_service_client_session *session = NULL;
    u32 savedRoleCount = g_vm_net_mock_role_db.roleCount;
    u32 savedActiveRoleId = g_vm_net_mock_role_db.activeRoleId;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;
    u32 savedActiveClientId = g_vm_mock_service_active_client_id;
    u32 savedSchedulerTick = g_schedulerTick;
    u8 request[96];
    u8 response[1024];
    u32 requestLen = 0;
    u32 responseLen = 0;
    u32 configuredIndex = 0;
    u32 expectedX = 0;
    u32 expectedY = 0;
    int status = 1;

    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData"))
    {
        fputs("direct action13 fixture could not locate JHOnlineData\n",
              stderr);
        return 1;
    }

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
    g_vm_net_mock_role_db.roles[0].job = 1;
    g_vm_net_mock_role_db.roles[0].level = 1;
    g_vm_net_mock_role_db.roles[0].hp = VM_NET_MOCK_ROLE_DEFAULT_HP;
    g_vm_net_mock_role_db.roles[0].hpMax = VM_NET_MOCK_ROLE_DEFAULT_HP;
    g_vm_net_mock_role_db.roles[0].mp = VM_NET_MOCK_ROLE_DEFAULT_MP;
    g_vm_net_mock_role_db.roles[0].mpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", penglaiScene);
    g_vm_net_mock_role_db.roles[0].x = 1;
    g_vm_net_mock_role_db.roles[0].y = 1;

    g_vm_mock_service_client_sessions = NULL;
    g_vm_mock_service_active_client_id = 0x0d1ce013u;
    g_schedulerTick = 400u;
    session = vm_mock_service_get_or_create_client_session(
        g_vm_mock_service_active_client_id);
    if (session == NULL)
    {
        fputs("direct action13 fixture could not allocate a client session\n",
              stderr);
        goto done;
    }
    session->onlineRoleId = 424242u;
    session->sceneVisibleReady = true;
    session->sceneVisiblePending = false;
    snprintf(session->sceneVisibleScene, sizeof(session->sceneVisibleScene),
             "%s", penglaiScene);
    session->sceneVisibleX = 1;
    session->sceneVisibleY = 1;
    if (!vm_net_mock_select_sce_combat_spawn(penglaiScene, 1000,
                                             &configuredIndex, &expectedX,
                                             &expectedY) ||
        configuredIndex == 0 || expectedX == 0 || expectedY == 0)
    {
        fputs("direct action13 fixture lacks the observed Penglai monkey spawn\n",
              stderr);
        goto done;
    }
    g_vm_net_mock_role_db.roles[0].x = (u16)expectedX;
    g_vm_net_mock_role_db.roles[0].y = (u16)expectedY;
    session->sceneVisibleX = (u16)expectedX;
    session->sceneVisibleY = (u16)expectedY;
    session->instanceChallengeDirectPending = true;
    session->instanceChallengeDirectSceneMonster = true;
    session->instanceChallengeActorId = 9001u;
    session->instanceChallengeEnemyId = 1000u;
    session->instanceChallengeTick = g_schedulerTick;
    snprintf(session->instanceChallengeScene,
             sizeof(session->instanceChallengeScene), "%s", penglaiScene);

    if (!build_action13_request(request, sizeof(request), 1000u, configuredIndex,
                                &requestLen) ||
        (responseLen = vm_net_mock_build_challenge_interaction_response(
             request, requestLen, response, sizeof(response))) == 0 ||
        !response_has_exact_objects(response, responseLen, 2, 2, 4, 5) ||
        session->instanceChallengePending ||
        session->instanceChallengeBattlePending ||
        session->instanceChallengeDirectPending ||
        session->instanceChallengeDirectSceneMonster ||
        session->instanceChallengeSceneIndex != 0)
    {
        fputs("direct action13 did not immediately deliver WT2/2 + WT4/5\n",
              stderr);
        goto done;
    }

    puts("direct scene challenge progress regression passed: WT4/1 -> WT2/2+WT4/5");
    status = 0;

done:
    g_vm_mock_service_active_client_id = savedActiveClientId;
    g_vm_mock_service_client_sessions = savedSessions;
    g_schedulerTick = savedSchedulerTick;
    free(session);
    g_vm_net_mock_role_db.roles[0] = savedRole;
    g_vm_net_mock_role_db.roleCount = savedRoleCount;
    g_vm_net_mock_role_db.activeRoleId = savedActiveRoleId;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    return status;
}
