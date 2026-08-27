/*
 * Client-status projection regression for the scene's native timed-item
 * badges.  It does not start a listener, open MySQL, or write role state.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static int expect_status(bool timedCombatActive, bool expCardActive,
                         bool battleInsightActive, u8 ruffianflag,
                         u8 expcard, u8 expbook)
{
    vm_net_mock_scene_timed_item_status status =
        vm_net_mock_scene_timed_item_status_for_active_effects(
            timedCombatActive, expCardActive, battleInsightActive);

    if (status.pcimg != 1 || status.ruffianflag != ruffianflag ||
        status.expcard != expcard || status.expbook != expbook)
    {
        fprintf(stderr,
                "status mapping mismatch input=%u/%u/%u actual="
                "pcimg=%u ruffian=%u expcard=%u expbook=%u\n",
                timedCombatActive ? 1 : 0, expCardActive ? 1 : 0,
                battleInsightActive ? 1 : 0, status.pcimg,
                status.ruffianflag, status.expcard, status.expbook);
        return 1;
    }
    return 0;
}

int main(void)
{
    u8 request[128];
    u8 response[2048];
    u8 field = 0;
    u8 kind = 0;
    u8 subtype = 0;
    u32 pos = 5;
    u32 requestLen = 0;
    u32 responseLen = 0;
    u32 objectStart = 0;

    if (expect_status(false, false, false, 0, 0, 0) != 0 ||
        expect_status(true, false, false, 1, 0, 0) != 0 ||
        expect_status(false, true, false, 0, 1, 0) != 0 ||
        expect_status(false, false, true, 0, 0, 1) != 0 ||
        expect_status(true, true, true, 1, 1, 1) != 0)
    {
        return 1;
    }

    memset(response, 0, sizeof(response));
    if (!vm_net_mock_append_login_success_object(response, sizeof(response),
                                                 &pos, 6, true, NULL))
    {
        fputs("unable to build inactive role status object\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(response, pos, 1);
    if (!vm_net_mock_get_object_u8_field(response, pos, "pcimg", &field) ||
        field != 1 ||
        !vm_net_mock_get_object_u8_field(response, pos, "ruffianflag",
                                         &field) ||
        field != 0 ||
        !vm_net_mock_get_object_u8_field(response, pos, "expcard", &field) ||
        field != 0 ||
        !vm_net_mock_get_object_u8_field(response, pos, "expbook", &field) ||
        field != 0)
    {
        fputs("inactive 1/1/6 status object regressed\n", stderr);
        return 1;
    }

    memset(request, 0, sizeof(request));
    pos = 5;
    if (!vm_net_mock_begin_wt_object(request, sizeof(request), &pos, 1, 7, 7,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(request, sizeof(request), &pos, "type", 2))
    {
        fputs("unable to build pcimg status request\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_object(request, objectStart, pos);
    vm_net_mock_finish_wt_packet(request, pos, 1);
    requestLen = pos;
    memset(response, 0, sizeof(response));
    responseLen = vm_net_mock_build_game_type_response(
        request, requestLen, response, sizeof(response), 2);
    if (responseLen == 0 ||
        !vm_net_mock_get_first_object_kind_subtype(response, responseLen,
                                                   &kind, &subtype) ||
        kind != 7 || subtype != 20 ||
        !vm_net_mock_get_object_u8_field(response, responseLen, "result", &field) ||
        field != 1 ||
        !vm_net_mock_get_object_u8_field(response, responseLen, "pcimg", &field) ||
        field != 1)
    {
        fputs("independent pcimg status response regressed\n", stderr);
        return 1;
    }

    memset(request, 0, sizeof(request));
    pos = 5;
    if (!vm_net_mock_begin_wt_object(request, sizeof(request), &pos, 1, 7, 7,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(request, sizeof(request), &pos, "type", 3))
    {
        fputs("unable to build expcard status request\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_object(request, objectStart, pos);
    vm_net_mock_finish_wt_packet(request, pos, 1);
    requestLen = pos;
    memset(response, 0, sizeof(response));
    responseLen = vm_net_mock_build_game_type_response(
        request, requestLen, response, sizeof(response), 3);
    if (responseLen == 0 ||
        !vm_net_mock_get_first_object_kind_subtype(response, responseLen,
                                                   &kind, &subtype) ||
        kind != 7 || subtype != 32 ||
        !vm_net_mock_get_object_u8_field(response, responseLen, "result", &field) ||
        field != 1 ||
        !vm_net_mock_get_object_u8_field(response, responseLen, "expcard", &field) ||
        field != 0)
    {
        fputs("independent expcard status response regressed\n", stderr);
        return 1;
    }

    puts("timed-item-status-icon regression passed: pcimg hides fixed badge; "
         "combat/exp-card/battle-insight map to ruffianflag/expcard/expbook; "
         "independent 7/20 and 7/32 keep their parser-specific contracts");
    return 0;
}
