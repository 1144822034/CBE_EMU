/*
 * Isolated contract regression for the temporary startup-SCE direct-enter
 * experiment.  It drives the registered short-WT25/5 builder with its actual
 * request bytes: no listener, database, account, client memory, or packet
 * rewriting is used.
 *
 * Default: the direct-enter builder does not consume standalone WT25/5, so
 * dispatch retains its ordinary control acknowledgement.  With
 * CBE_TEST_STARTUP_SCE_DIRECT_ENTER=1: the one matching WT25/5 produces
 * exactly one mmGame 16/3(result=2), then this builder releases the request
 * back to the ordinary acknowledgement path.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool build_short_wt25_5(u8 *out, u32 outCap, u32 *lengthOut)
{
    if (out == NULL || lengthOut == NULL || outCap < 9)
        return false;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = 0;
    out[3] = 9;
    out[4] = 1;
    out[5] = 0x19;
    out[6] = 5;
    out[7] = 0;
    out[8] = 5;
    *lengthOut = 9;
    return true;
}

static bool response_has_object(const u8 *packet, u32 length, u8 kind,
                                u8 subtype)
{
    u32 offset = 5;

    if (packet == NULL || length < 5 || packet[0] != 'W' || packet[1] != 'T')
        return false;
    for (u32 i = 0; i < packet[4]; ++i)
    {
        u16 objectLen = 0;

        if (offset + 6 > length)
            return false;
        objectLen = (u16)((packet[offset + 4] << 8) | packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > length)
            return false;
        if (packet[offset] == 1 && packet[offset + 1] == kind &&
            packet[offset + 2] == subtype)
        {
            return true;
        }
        offset += objectLen;
    }
    return false;
}

static bool response_object_get_u8_field(const u8 *packet, u32 length,
                                         u8 kind, u8 subtype,
                                         const char *fieldName, u8 *valueOut)
{
    u32 offset = 5;

    if (valueOut != NULL)
        *valueOut = 0;
    if (packet == NULL || length < 5 || packet[0] != 'W' || packet[1] != 'T' ||
        fieldName == NULL || fieldName[0] == 0)
    {
        return false;
    }
    for (u32 i = 0; i < packet[4]; ++i)
    {
        u16 objectLen = 0;

        if (offset + 6 > length)
            return false;
        objectLen = (u16)((packet[offset + 4] << 8) | packet[offset + 5]);
        if (objectLen < 6 || offset + objectLen > length)
            return false;
        if (packet[offset] == 1 && packet[offset + 1] == kind &&
            packet[offset + 2] == subtype)
        {
            return vm_net_mock_get_object_u8_field(packet + offset + 6,
                                                   objectLen - 6, fieldName,
                                                   valueOut);
        }
        offset += objectLen;
    }
    return false;
}

static void reset_test_state(const char *scene, u32 clientId)
{
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    g_vm_net_mock_role_db.roles[0].roleId = 1;
    snprintf(g_vm_net_mock_role_db.roles[0].scene,
             sizeof(g_vm_net_mock_role_db.roles[0].scene), "%s", scene);
    g_vm_net_mock_role_db.roles[0].x = 108;
    g_vm_net_mock_role_db.roles[0].y = 104;

    memset(&g_vm_net_mock_last_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_scene_change_target));
    memset(&g_vm_net_mock_last_completed_scene_change_target, 0,
           sizeof(g_vm_net_mock_last_completed_scene_change_target));
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_target_valid = false;
    g_vm_net_mock_last_completed_scene_change_tick = 0;
    memset(&g_vm_net_mock_startup_sce_enter_target, 0,
           sizeof(g_vm_net_mock_startup_sce_enter_target));
    g_vm_net_mock_startup_sce_enter_pending = false;
    g_vm_net_mock_startup_sce_enter_install_generation = 0;
    g_vm_net_mock_startup_sce_enter_armed_tick = 0;
    g_vm_net_mock_title_role_scene_followup_pending = true;
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;
    g_vm_mock_service_active_client_id = clientId;
    g_schedulerTick = 100;

    memset(&g_vm_net_mock_content_update, 0,
           sizeof(g_vm_net_mock_content_update));
    memset(g_vm_net_mock_content_client_states, 0,
           sizeof(g_vm_net_mock_content_client_states));
    g_vm_net_mock_content_update_loaded = true;
    g_vm_net_mock_content_update.enabled = true;
    g_vm_net_mock_content_update.id = 907;
    g_vm_net_mock_content_update.code = 908;
    g_vm_net_mock_content_update.nameCount = 1;
    snprintf(g_vm_net_mock_content_update.names[0],
             sizeof(g_vm_net_mock_content_update.names[0]), "%s", scene);
    vm_net_mock_content_client_note_version(clientId, true, false);
    vm_net_mock_note_update_chunk_complete(scene);
    g_vm_net_mock_update_completed_reenter_pending = false;
    g_vm_net_mock_update_completed_name[0] = 0;
}

int main(void)
{
    static const char targetScene[] =
        "\x63\x30\x34\xc1\xd9\xb0\xb2\xb8\xae\x5f\x30\x31\x2e\x73\x63\x65";
    const u32 clientId = 0x51627384u;
    u8 request[32];
    u8 response[512];
    u32 requestLen = 0;
    u32 responseLen = 0;
    u8 result = 0;

#ifdef _WIN32
    _putenv_s("CBE_MYSQL_HOST", "127.0.0.1");
    _putenv_s("CBE_MYSQL_PORT", "1");
    _putenv_s("CBE_MYSQL_DATABASE", "cbe_startup_sce_gate_regression");
    _putenv_s("CBE_TEST_STARTUP_SCE_DIRECT_ENTER", "0");
#else
    setenv("CBE_MYSQL_HOST", "127.0.0.1", 1);
    setenv("CBE_MYSQL_PORT", "1", 1);
    setenv("CBE_MYSQL_DATABASE", "cbe_startup_sce_gate_regression", 1);
    setenv("CBE_TEST_STARTUP_SCE_DIRECT_ENTER", "0", 1);
#endif

    if (!build_short_wt25_5(request, sizeof(request), &requestLen))
    {
        fputs("could not construct short WT25/5 request\n", stderr);
        return 1;
    }

    reset_test_state(targetScene, clientId);
    vm_net_mock_arm_startup_sce_install_scene_enter(targetScene);
    if (g_vm_net_mock_startup_sce_enter_pending)
    {
        fputs("default startup SCE install unexpectedly armed direct enter\n",
              stderr);
        return 1;
    }
    responseLen = vm_net_mock_build_startup_sce_install_scene_enter_response(
        request, requestLen, response, sizeof(response));
    if (responseLen != 0)
    {
        fputs("default startup WT25/5 was consumed by direct-enter builder\n",
              stderr);
        return 1;
    }

#ifdef _WIN32
    _putenv_s("CBE_TEST_STARTUP_SCE_DIRECT_ENTER", "1");
#else
    setenv("CBE_TEST_STARTUP_SCE_DIRECT_ENTER", "1", 1);
#endif
    reset_test_state(targetScene, clientId);
    vm_net_mock_arm_startup_sce_install_scene_enter(targetScene);
    if (!g_vm_net_mock_startup_sce_enter_pending)
    {
        fputs("test gate did not arm startup SCE direct enter\n", stderr);
        return 1;
    }
    g_vm_net_mock_last_completed_scene_change_target =
        g_vm_net_mock_startup_sce_enter_target;
    g_vm_net_mock_last_completed_scene_change_target_valid = true;
    g_vm_net_mock_last_completed_scene_change_tick = g_schedulerTick;

    responseLen = vm_net_mock_build_startup_sce_install_scene_enter_response(
        request, requestLen, response, sizeof(response));
    if (responseLen == 0 || !response_has_object(response, responseLen, 0x10, 3) ||
        !response_object_get_u8_field(response, responseLen, 0x10, 3,
                                      "result", &result) ||
        result != 2 || g_vm_net_mock_startup_sce_enter_pending)
    {
        fputs("test-gated startup WT25/5 did not emit one 16/3 result=2\n",
              stderr);
        return 1;
    }

    responseLen = vm_net_mock_build_startup_sce_install_scene_enter_response(
        request, requestLen, response, sizeof(response));
    if (responseLen != 0)
    {
        fputs("repeated test-gated WT25/5 was consumed more than once\n",
              stderr);
        return 1;
    }

#ifdef _WIN32
    _putenv_s("CBE_TEST_STARTUP_SCE_DIRECT_ENTER", "0");
#else
    setenv("CBE_TEST_STARTUP_SCE_DIRECT_ENTER", "0", 1);
#endif
    puts("startup SCE direct-enter test-gate regression passed");
    return 0;
}
