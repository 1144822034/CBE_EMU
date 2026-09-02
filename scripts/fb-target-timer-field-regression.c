/* Deterministic WT 1/27/4 `min` field regression.
 *
 * JianghuOL.CBE's 0x01033CF2 getter reads `min` as a u32.  This test drives
 * the production fb-target builder and proves both a session-bound instance
 * value and the zero default retain that wire width. It starts no listener,
 * database, client, or scheduler.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main
/* The production service links this aggregation unit separately.  Include it
 * here so this standalone regression invokes the actual fb-target builders
 * and session state rather than only their declarations. */
#include "../src/server/mock-server.c"

static int assert_min_field(u32 configuredMinutes, const char *label)
{
    u8 object[128];
    u32 pos = 5;
    u32 decoded = 0;
    vm_mock_service_client_session session;
    vm_mock_service_client_session *savedSessions =
        g_vm_mock_service_client_sessions;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    bool encoded = false;

    memset(object, 0, sizeof(object));
    memset(&session, 0, sizeof(session));
    session.clientId = 0x1234u;
    session.transientInstanceActive = true;
    snprintf(session.transientInstanceScene,
             sizeof(session.transientInstanceScene), "timer-test.sce");
    session.transientInstanceTimerMinutes = configuredMinutes;
    session.transientInstanceTimerStartedMs = scheduler_get_tick_ms();
    session.next = savedSessions;
    g_vm_mock_service_client_sessions = &session;
    g_vm_mock_service_active_client_id = session.clientId;
    encoded = vm_net_mock_append_fb_target_result4_object(
        object, sizeof(object), &pos, 1, "") &&
              object[5] == 1 && object[6] == 0x1b && object[7] == 4 &&
              vm_net_mock_get_object_u32_field(object + 5, pos - 5, "min",
                                               &decoded);
    g_vm_mock_service_client_sessions = savedSessions;
    g_vm_mock_service_active_client_id = savedClientId;
    if (!encoded || decoded > configuredMinutes ||
        (configuredMinutes != 0 && decoded + 1u < configuredMinutes))
    {
        fprintf(stderr,
                "%s: WT 1/27/4 min did not encode the session timer (%u, got %u)\n",
                label, configuredMinutes, decoded);
        return 1;
    }
    return 0;
}

static int assert_elapsed_timer_contract(void)
{
    vm_mock_service_client_session session;

    memset(&session, 0, sizeof(session));
    session.transientInstanceActive = true;
    snprintf(session.transientInstanceScene,
             sizeof(session.transientInstanceScene), "timer-test.sce");
    session.transientInstanceTimerMinutes = 5;
    session.transientInstanceTimerStartedMs = 1000;
    if (vm_mock_service_active_transient_instance_timer_remaining_minutes(
        &session, 1000) != 5 ||
        vm_mock_service_active_transient_instance_timer_remaining_minutes(
            &session, 61000) != 4 ||
        vm_mock_service_active_transient_instance_timer_remaining_minutes(
            &session, 301000) != 0)
    {
        fputs("session timer remaining-minutes contract failed\n", stderr);
        return 1;
    }
    return 0;
}

static int assert_instance_completion_scope(void)
{
    vm_mock_service_client_session session;
    vm_mock_service_client_session *savedSessions =
        g_vm_mock_service_client_sessions;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    vm_net_mock_scene_change_target target;
    int failed = 0;

    memset(&session, 0, sizeof(session));
    memset(&target, 0, sizeof(target));
    session.clientId = 0x5678u;
    session.transientInstanceActive = true;
    snprintf(session.transientInstanceScene,
             sizeof(session.transientInstanceScene), "timer-test.sce");
    session.transientInstanceX = 50;
    session.transientInstanceY = 50;
    snprintf(target.scene, sizeof(target.scene), "%s",
             "timer-test.sce");
    session.next = savedSessions;
    g_vm_mock_service_client_sessions = &session;
    g_vm_mock_service_active_client_id = session.clientId;

    if (!vm_net_mock_is_active_transient_instance_target(&target))
    {
        fputs("active instance target was not recognized\n", stderr);
        failed = 1;
    }
    snprintf(target.scene, sizeof(target.scene), "%s", "ordinary-map.sce");
    if (vm_net_mock_is_active_transient_instance_target(&target))
    {
        fputs("instance completion escaped its session scene\n", stderr);
        failed = 1;
    }

    g_vm_mock_service_client_sessions = savedSessions;
    g_vm_mock_service_active_client_id = savedClientId;
    return failed;
}

static int assert_instance_entry_copper_cost_contract(void)
{
    vm_net_mock_role_state role;
    u32 before = 0;
    u32 after = 0;

    memset(&role, 0, sizeof(role));
    role.money = 750;
    if (!vm_net_mock_instance_entry_copper_prepare_debit(
            &role, 0, &before, &after) ||
        before != 750 || after != 750 || role.money != 750)
    {
        fputs("free instance entry did not preserve the copper balance\n", stderr);
        return 1;
    }
    if (!vm_net_mock_instance_entry_copper_prepare_debit(
            &role, 750, &before, &after) ||
        before != 750 || after != 0 || role.money != 750)
    {
        fputs("exact instance entry copper cost was not accepted without mutation\n", stderr);
        return 1;
    }
    if (vm_net_mock_instance_entry_copper_prepare_debit(
            &role, 751, &before, &after) || role.money != 750)
    {
        fputs("insufficient instance entry copper was accepted or mutated\n", stderr);
        return 1;
    }
    return 0;
}

static int assert_expired_instance_exit_poll_contract(void)
{
    u8 response[256];
    char scene[64];
    vm_mock_service_client_session session;
    vm_net_mock_scene_change_target completionTarget;
    vm_mock_service_client_session *savedSessions =
        g_vm_mock_service_client_sessions;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    u8 savedBattleSceneMonsterStartActive = g_mockBattleSceneMonsterStartActive;
    u32 nowMs = scheduler_get_tick_ms();
    u32 responseLen = 0;
    int failed = 0;

    memset(response, 0, sizeof(response));
    memset(scene, 0, sizeof(scene));
    memset(&session, 0, sizeof(session));
    memset(&completionTarget, 0, sizeof(completionTarget));
    session.clientId = 0x9abcu;
    session.transientInstanceActive = true;
    snprintf(session.transientInstanceScene,
             sizeof(session.transientInstanceScene), "29梦境空间_03.sce");
    session.transientInstanceX = 50;
    session.transientInstanceY = 50;
    snprintf(session.transientInstanceReturnScene,
             sizeof(session.transientInstanceReturnScene), "c04临安府_06.sce");
    session.transientInstanceReturnX = 137;
    session.transientInstanceReturnY = 321;
    session.sceneVisibleReady = true;
    snprintf(session.sceneVisibleScene,
             sizeof(session.sceneVisibleScene), "%s", session.transientInstanceScene);
    session.sceneVisibleX = session.transientInstanceX;
    session.sceneVisibleY = session.transientInstanceY;
    session.next = savedSessions;
    g_vm_mock_service_client_sessions = &session;
    g_vm_mock_service_active_client_id = session.clientId;

    /* A configured zero remains the established "no expiry action" value.
     * It must not turn the normal, display-only zero into a return event. */
    if (vm_net_mock_build_active_transient_instance_expiry_exit_response(
            response, sizeof(response), &session) != 0 ||
        !session.transientInstanceActive)
    {
        fputs("zero-minute instance unexpectedly delivered an exit event\n", stderr);
        failed = 1;
    }
    session.transientInstanceTimerMinutes = 1;
    session.transientInstanceTimerStartedMs = nowMs - 60000u;

    if (vm_mock_service_get_active_client_session() != &session ||
        !vm_net_mock_scene_name_is_persistable(session.transientInstanceScene) ||
        !vm_net_mock_scene_name_is_persistable(session.transientInstanceReturnScene) ||
        vm_mock_service_active_transient_instance_timer_remaining_minutes(
            &session, nowMs) != 0)
    {
        fprintf(stderr,
                "expiry exit precondition failed active=%u instance_resource=%u "
                "return_resource=%u remaining=%u\n",
                vm_mock_service_get_active_client_session() == &session ? 1u : 0u,
                vm_net_mock_scene_name_is_persistable(session.transientInstanceScene) ? 1u : 0u,
                vm_net_mock_scene_name_is_persistable(session.transientInstanceReturnScene) ? 1u : 0u,
                vm_mock_service_active_transient_instance_timer_remaining_minutes(
                    &session, nowMs));
        failed = 1;
    }

    /* A scene 30/1 cannot safely cross the native battle lifecycle.  It must
     * wait for the same 25/5 release that returns the CBE to its scene actor
     * owner; otherwise the return scene consumes its NPC catalog too early. */
    g_mockBattleSceneMonsterStartActive = 1;
    if (!failed &&
        vm_net_mock_build_active_transient_instance_expiry_exit_response(
            response, sizeof(response), &session) != 0)
    {
        fputs("expired instance delivered a scene exit while battle was active\n",
              stderr);
        failed = 1;
    }
    if (!session.transientInstanceActive ||
        !session.transientInstanceExpiryExitAwaitingBattleClose)
    {
        fputs("expired instance did not retain its pending exit through battle close\n",
              stderr);
        failed = 1;
    }
    g_mockBattleSceneMonsterStartActive = 0;

    if (!failed)
        responseLen = vm_net_mock_build_active_transient_instance_expiry_exit_response(
            response, sizeof(response), &session);
    if (responseLen < 5 || response[0] != 'W' || response[1] != 'T' ||
        response[4] != 1 || response[5] != 1 || response[6] != 30 ||
        response[7] != 1 ||
        !vm_net_mock_get_object_string_field(response + 5, responseLen - 5,
                                             "scene", scene, sizeof(scene)) ||
        strcmp(scene, "c04临安府_06.sce") != 0)
    {
        fputs("expired instance did not deliver a 30/1 return scene on poll\n", stderr);
        failed = 1;
    }
    if (session.transientInstanceActive || session.transientInstanceScene[0] != 0 ||
        session.transientInstanceReturnScene[0] != 0)
    {
        fputs("expired instance session was not cleared after its return event\n", stderr);
        failed = 1;
    }
    snprintf(completionTarget.scene, sizeof(completionTarget.scene), "%s",
             "c04临安府_06.sce");
    completionTarget.x = 137;
    completionTarget.y = 321;
    if (!session.transientInstanceExpiryExitCompletionPending ||
        !vm_mock_service_active_transient_instance_expiry_exit_completion_matches(
            &completionTarget))
    {
        fputs("expired instance did not preserve its one-shot return completion\n",
              stderr);
        failed = 1;
    }
    completionTarget.x = 138;
    if (vm_mock_service_active_transient_instance_expiry_exit_completion_matches(
            &completionTarget))
    {
        fputs("expired instance completion escaped its exact return target\n",
              stderr);
        failed = 1;
    }
    completionTarget.x = 137;
    if (!failed && vm_net_mock_build_active_transient_instance_expiry_exit_response(
                       response, sizeof(response), &session) != 0)
    {
        fputs("expired instance delivered a duplicate return event\n", stderr);
        failed = 1;
    }
    vm_mock_service_active_transient_instance_expiry_exit_completion_clear(
        "regression-cleanup");
    if (session.transientInstanceExpiryExitCompletionPending)
    {
        fputs("expired instance completion marker did not clear\n", stderr);
        failed = 1;
    }
    if (!vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_begin(
            "c04临安府_06.sce") ||
        !vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_matches(
            "c04临安府_06.sce") ||
        vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_matches(
            "29梦境空间_03.sce"))
    {
        fputs("expired instance NPC reseed did not remain scoped to its return scene\n",
              stderr);
        failed = 1;
    }
    vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_clear(
        "regression-cleanup");
    if (session.transientInstanceExpiryExitNpcReseedPending)
    {
        fputs("expired instance NPC reseed marker did not clear\n", stderr);
        failed = 1;
    }

    g_vm_mock_service_client_sessions = savedSessions;
    g_vm_mock_service_active_client_id = savedClientId;
    g_mockBattleSceneMonsterStartActive = savedBattleSceneMonsterStartActive;
    return failed;
}

int main(void)
{
    if (assert_min_field(0, "zero default") != 0 ||
        assert_min_field(3605, "session countdown") != 0 ||
        assert_elapsed_timer_contract() != 0 ||
        assert_instance_completion_scope() != 0 ||
        assert_instance_entry_copper_cost_contract() != 0 ||
        assert_expired_instance_exit_poll_contract() != 0)
    {
        return 1;
    }
    puts("fb-target timer field regression passed");
    return 0;
}
