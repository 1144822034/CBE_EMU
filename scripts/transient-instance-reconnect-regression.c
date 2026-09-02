/* Deterministic transient-instance reconnect regression.
 *
 * This invokes the production reconnect handoff and the existing 27/4 timer
 * builder with in-memory sessions.  It starts no listener, database, or
 * client.  The contract is intentionally narrow: an unexpected disconnect
 * snapshot may be resumed only by the same account and role; ActorInfo's
 * scene/position sources and the FB `min` field then use the recovered
 * temporary destination.  A timer that expires before role-select must not
 * restore that destination.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main
#include "../src/server/mock-server.c"

static const char k_test_world_scene[] =
    "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"; /* 01桃花岛_01.sce, GBK */
static const char k_test_instance_scene[] =
    "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"; /* 00蓬莱仙岛_02.sce, GBK */

static void clear_test_reconnect_records(void)
{
    while (g_vm_mock_service_transient_instance_reconnects != NULL)
    {
        vm_mock_service_transient_instance_reconnect *record =
            g_vm_mock_service_transient_instance_reconnects;

        g_vm_mock_service_transient_instance_reconnects = record->next;
        free(record);
    }
}

static int inspect_reconnect_fb_tail(const u8 *packet, u32 packetLen,
                                     u32 *minMinutesOut)
{
    u32 offset = 5;
    u8 expectedSubtype = 12;

    while (expectedSubtype != 0)
    {
        u16 objectLen = 0;

        if (offset + 6u > packetLen || packet[offset] != 1 ||
            packet[offset + 1] != 0x1b || packet[offset + 2] != expectedSubtype)
        {
            return 1;
        }
        objectLen = (u16)(((u16)packet[offset + 4] << 8) |
                          packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > packetLen)
            return 1;
        if (expectedSubtype == 4 &&
            !vm_net_mock_get_object_u32_field(packet + offset + 6,
                                               objectLen - 6,
                                               "min", minMinutesOut))
        {
            return 1;
        }
        offset += objectLen;
        expectedSubtype = expectedSubtype == 12 ? 11 :
                          expectedSubtype == 11 ? 4 : 0;
    }
    return offset == packetLen ? 0 : 1;
}

static bool append_empty_request_object(u8 *request, u32 requestCap,
                                        u32 *pos, u8 kind, u8 subtype)
{
    if (request == NULL || pos == NULL || *pos + 5u > requestCap)
        return false;
    request[(*pos)++] = 1;
    request[(*pos)++] = kind;
    request[(*pos)++] = subtype;
    request[(*pos)++] = 0;
    request[(*pos)++] = 5;
    return true;
}

static bool append_type101_request_object(u8 *request, u32 requestCap,
                                          u32 *pos)
{
    const u8 typeField[] = { 4, 'T', 'y', 'p', 'e', 0, 1, 101 };

    if (request == NULL || pos == NULL ||
        *pos + 5u + sizeof(typeField) > requestCap)
    {
        return false;
    }
    request[(*pos)++] = 1;
    request[(*pos)++] = 2;
    request[(*pos)++] = 10;
    request[(*pos)++] = 0;
    request[(*pos)++] = (u8)(5u + sizeof(typeField));
    memcpy(request + *pos, typeField, sizeof(typeField));
    *pos += sizeof(typeField);
    return true;
}

static bool build_reconnect_scene_subset_request(u8 *request, u32 requestCap,
                                                 u32 *requestLenOut)
{
    u32 pos = 4;

    if (request == NULL || requestLenOut == NULL || requestCap < pos)
        return false;
    memset(request, 0, requestCap);
    request[0] = 'W';
    request[1] = 'T';
    if (!append_empty_request_object(request, requestCap, &pos, 12, 1) ||
        !append_empty_request_object(request, requestCap, &pos, 7, 42) ||
        !append_empty_request_object(request, requestCap, &pos, 6, 1) ||
        !append_empty_request_object(request, requestCap, &pos, 6, 13) ||
        !append_empty_request_object(request, requestCap, &pos, 6, 14) ||
        !append_type101_request_object(request, requestCap, &pos) ||
        !append_empty_request_object(request, requestCap, &pos, 25, 5))
    {
        return false;
    }
    request[2] = (u8)(pos >> 8);
    request[3] = (u8)pos;
    *requestLenOut = pos;
    return true;
}

static int assert_reconnect_scene_subset_timer_packet(
    vm_mock_service_client_session *session,
    const char *scene, u32 expectedMinMinutes)
{
    u8 request[128];
    u8 response[4096];
    u32 requestLen = 0;
    u32 responseLen = 0;
    u32 offset = 5;
    u8 objectCount = 0;
    u32 minMinutes = 0;
    bool sawFb12 = false;
    bool sawFb11 = false;
    bool sawFb4 = false;
    bool sawInfoBanner = false;

    if (session == NULL ||
        !build_reconnect_scene_subset_request(request, sizeof(request),
                                              &requestLen))
    {
        return 1;
    }
    memset(response, 0, sizeof(response));
    g_vm_net_mock_title_role_scene_followup_pending = true;
    responseLen = vm_net_mock_build_scene_task_subset_followup_response(
        request, requestLen, response, sizeof(response));
    if (responseLen < 5 || response[0] != 'W' || response[1] != 'T' ||
        response[4] != VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX ||
        session->transientInstanceReconnectFbPending)
    {
        return 1;
    }
    while (offset < responseLen)
    {
        u16 objectLen = 0;

        if (offset + 6u > responseLen)
            return 1;
        objectLen = (u16)(((u16)response[offset + 4] << 8) |
                          response[offset + 5]);
        if (objectLen < 6 || offset + objectLen > responseLen)
            return 1;
        if (response[offset] == 1 && response[offset + 1] == 0x1b)
        {
            if (response[offset + 2] == 12)
                sawFb12 = true;
            else if (response[offset + 2] == 11)
                sawFb11 = true;
            else if (response[offset + 2] == 4)
            {
                sawFb4 = vm_net_mock_get_object_u32_field(
                    response + offset + 6, objectLen - 6, "min", &minMinutes);
            }
        }
        if (response[offset] == 1 && response[offset + 1] == 25 &&
            response[offset + 2] == 5)
        {
            sawInfoBanner = true;
        }
        ++objectCount;
        offset += objectLen;
    }
    return offset == responseLen &&
           objectCount == VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX &&
           sawFb12 && sawFb11 && sawFb4 &&
           minMinutes == expectedMinMinutes && !sawInfoBanner ? 0 : 1;
}

static int assert_transient_instance_reconnect(void)
{
    vm_mock_service_client_session oldSession;
    vm_mock_service_client_session resumedSession;
    vm_mock_service_client_session *savedSessions =
        g_vm_mock_service_client_sessions;
    vm_mock_service_transient_instance_reconnect *savedReconnects =
        g_vm_mock_service_transient_instance_reconnects;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    vm_net_mock_role_db_file savedRoleDb = g_vm_net_mock_role_db;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;
    vm_net_mock_role_state *role = NULL;
    u8 packet[512];
    u8 overflowPacket[512];
    u32 pos = 5;
    u16 objectLen = 0;
    u32 minMinutes = 0;
    u8 reconnectFbObjectCount = 0;
    u8 overflowObjectCount = 8;
    u32 nowMs = scheduler_get_tick_ms();
    int failed = 0;

    memset(&oldSession, 0, sizeof(oldSession));
    memset(&resumedSession, 0, sizeof(resumedSession));
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    memset(packet, 0, sizeof(packet));
    g_vm_mock_service_transient_instance_reconnects = NULL;

    if (!vm_mock_service_transient_instance_offline_reason_allows_reconnect(
            "explicit-disconnect") ||
        vm_mock_service_transient_instance_offline_reason_allows_reconnect(
            "account-rebind"))
    {
        fputs("transient reconnect disconnect-boundary policy failed\n", stderr);
        failed = 1;
        goto cleanup;
    }

    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 10001;
    role = &g_vm_net_mock_role_db.roles[0];
    role->roleId = 10001;
    snprintf(role->scene, sizeof(role->scene), "%s", k_test_world_scene);
    role->x = 12;
    role->y = 10;

    oldSession.clientId = 0x1e71u;
    snprintf(oldSession.accountId, sizeof(oldSession.accountId), "%s",
             "reconnect-test");
    oldSession.transientInstanceActive = true;
    oldSession.transientInstanceRoleId = role->roleId;
    snprintf(oldSession.transientInstanceScene,
             sizeof(oldSession.transientInstanceScene), "%s",
             k_test_instance_scene);
    oldSession.transientInstanceX = 30;
    oldSession.transientInstanceY = 40;
    oldSession.transientInstanceTimerMinutes = 5;
    oldSession.transientInstanceTimerStartedMs = nowMs - 60000u;
    snprintf(oldSession.transientInstanceReturnScene,
             sizeof(oldSession.transientInstanceReturnScene), "%s",
             role->scene);
    oldSession.transientInstanceReturnX = role->x;
    oldSession.transientInstanceReturnY = role->y;

    if (!vm_mock_service_transient_instance_reconnect_snapshot(
            &oldSession, role->roleId, "explicit-disconnect"))
    {
        fputs("reconnect snapshot was not retained\n", stderr);
        failed = 1;
        goto cleanup;
    }

    resumedSession.clientId = 0x2e71u;
    snprintf(resumedSession.accountId, sizeof(resumedSession.accountId), "%s",
             "reconnect-test");
    resumedSession.next = savedSessions;
    g_vm_mock_service_client_sessions = &resumedSession;
    g_vm_mock_service_active_client_id = resumedSession.clientId;

    if (vm_mock_service_active_transient_instance_resume_after_role_select(10002) ||
        !vm_mock_service_active_transient_instance_resume_after_role_select(
            role->roleId) ||
        !resumedSession.transientInstanceActive ||
        resumedSession.transientInstanceRoleId != role->roleId ||
        strcmp(resumedSession.transientInstanceScene, k_test_instance_scene) != 0 ||
        resumedSession.transientInstanceX != 30 ||
        resumedSession.transientInstanceY != 40 ||
        !resumedSession.transientInstanceReconnectFbPending ||
        strcmp(vm_net_mock_scene_key_name(), k_test_instance_scene) != 0 ||
        vm_net_mock_scene_spawn_x() != 30 || vm_net_mock_scene_spawn_y() != 40)
    {
        fprintf(stderr,
                "reconnect role/scene restore contract failed active=%u role=%u scene=%s pos=(%u,%u) key=%s spawn=(%u,%u)\n",
                resumedSession.transientInstanceActive ? 1u : 0u,
                resumedSession.transientInstanceRoleId,
                resumedSession.transientInstanceScene,
                resumedSession.transientInstanceX,
                resumedSession.transientInstanceY,
                vm_net_mock_scene_key_name(), vm_net_mock_scene_spawn_x(),
                vm_net_mock_scene_spawn_y());
        failed = 1;
        goto cleanup;
    }

    if (!vm_net_mock_append_reconnect_transient_instance_fb_completion(
            packet, sizeof(packet), &pos, &reconnectFbObjectCount,
            k_test_instance_scene) ||
        reconnectFbObjectCount != 3 ||
        inspect_reconnect_fb_tail(packet, pos, &minMinutes) != 0 ||
        minMinutes != 4 || resumedSession.transientInstanceReconnectFbPending)
    {
        fprintf(stderr,
                "reconnect FB completion contract failed objects=%u min=%u pending=%u\n",
                (u32)reconnectFbObjectCount, minMinutes,
                resumedSession.transientInstanceReconnectFbPending ? 1u : 0u);
        failed = 1;
        goto cleanup;
    }

    /* The role-select scene subset also carries eight ordinary replies.  The
     * reconnect tail must reserve three slots before appending, otherwise its
     * 27/4(min) becomes the eleventh object and never reaches the CBE parser. */
    memset(overflowPacket, 0, sizeof(overflowPacket));
    pos = 5;
    resumedSession.transientInstanceReconnectFbPending = true;
    if (vm_net_mock_append_reconnect_transient_instance_fb_completion(
            overflowPacket, sizeof(overflowPacket), &pos, &overflowObjectCount,
            k_test_instance_scene) || pos != 5 || overflowObjectCount != 8)
    {
        fputs("reconnect FB completion accepted an over-limit packet\n", stderr);
        failed = 1;
        goto cleanup;
    }

    snprintf(role->scene, sizeof(role->scene), "%s", k_test_instance_scene);
    resumedSession.transientInstanceReconnectFbPending = true;
    if (assert_reconnect_scene_subset_timer_packet(&resumedSession,
                                                    k_test_instance_scene, 4) != 0)
    {
        fputs("reconnect scene subset did not reserve timer packet headroom\n",
              stderr);
        failed = 1;
        goto cleanup;
    }

    memset(packet, 0, sizeof(packet));
    pos = 5;
    if (!vm_net_mock_append_fb_target_result4_object(packet, sizeof(packet), &pos,
                                                      1, "") ||
        pos < 11 || packet[5] != 1 || packet[6] != 0x1b || packet[7] != 4)
    {
        fputs("reconnect FB timer object builder failed\n", stderr);
        failed = 1;
        goto cleanup;
    }
    objectLen = (u16)(((u16)packet[9] << 8) | packet[10]);
    if (objectLen < 6 || 5u + objectLen != pos ||
        !vm_net_mock_get_object_u32_field(packet + 11, objectLen - 6,
                                           "min", &minMinutes) ||
        minMinutes != 4)
    {
        fprintf(stderr, "reconnect FB min field mismatch min=%u expected=4\n",
                minMinutes);
        failed = 1;
        goto cleanup;
    }

    memset(&oldSession, 0, sizeof(oldSession));
    oldSession.clientId = 0x3e71u;
    snprintf(oldSession.accountId, sizeof(oldSession.accountId), "%s",
             "reconnect-test");
    oldSession.transientInstanceActive = true;
    oldSession.transientInstanceRoleId = role->roleId;
    snprintf(oldSession.transientInstanceScene,
             sizeof(oldSession.transientInstanceScene), "%s",
             k_test_instance_scene);
    oldSession.transientInstanceX = 30;
    oldSession.transientInstanceY = 40;
    oldSession.transientInstanceTimerMinutes = 2;
    oldSession.transientInstanceTimerStartedMs = scheduler_get_tick_ms() - 60000u;
    snprintf(oldSession.transientInstanceReturnScene,
             sizeof(oldSession.transientInstanceReturnScene), "%s",
             role->scene);
    oldSession.transientInstanceReturnX = role->x;
    oldSession.transientInstanceReturnY = role->y;
    if (!vm_mock_service_transient_instance_reconnect_snapshot(
            &oldSession, role->roleId, "heartbeat-timeout") ||
        g_vm_mock_service_transient_instance_reconnects == NULL)
    {
        fputs("expiring reconnect fixture was not retained\n", stderr);
        failed = 1;
        goto cleanup;
    }
    /* Model elapsed service time without sleeping: a record which was still
     * valid when the transport died may expire before its title role-select. */
    g_vm_mock_service_transient_instance_reconnects->timerStartedMs =
        scheduler_get_tick_ms() - 120000u;
    memset(&resumedSession, 0, sizeof(resumedSession));
    resumedSession.clientId = 0x4e71u;
    snprintf(resumedSession.accountId, sizeof(resumedSession.accountId), "%s",
             "reconnect-test");
    resumedSession.next = savedSessions;
    g_vm_mock_service_client_sessions = &resumedSession;
    g_vm_mock_service_active_client_id = resumedSession.clientId;
    if (vm_mock_service_active_transient_instance_resume_after_role_select(
            role->roleId) || resumedSession.transientInstanceActive ||
        g_vm_mock_service_transient_instance_reconnects != NULL)
    {
        fputs("expired reconnect record restored a transient instance\n", stderr);
        failed = 1;
    }

cleanup:
    clear_test_reconnect_records();
    g_vm_mock_service_client_sessions = savedSessions;
    g_vm_mock_service_transient_instance_reconnects = savedReconnects;
    g_vm_mock_service_active_client_id = savedClientId;
    g_vm_net_mock_role_db = savedRoleDb;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    return failed;
}

int main(void)
{
    if (assert_transient_instance_reconnect() != 0)
        return 1;
    puts("transient instance reconnect regression passed");
    return 0;
}
