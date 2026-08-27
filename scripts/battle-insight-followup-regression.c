/*
 * Regression for the client-owned completion step after using 战斗心得 (828).
 * The fixture runs the protocol's 1/25/7 follow-up through the same service
 * dispatcher that owns live requests.  The durable item/effect mutation may
 * happen only after a matching 1/25/6 quantity window has armed server-side
 * confirmation context; opening that window is read-only for the role.
 * A follow-up without that context must return the client's result=2 failure
 * branch rather than consume a stack.  A zero `num` remains unhandled. The
 * fixture uses player-2's observed `num:u32=2, seq:u16=56` 31-byte WT frame.
 * It never opens a listener or mutates a
 * role/backpack.  The production dispatcher may perform failed read-only
 * fallback probes when no test database is configured; this fixture supplies
 * none and never writes account data.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static u32 build_followup_request(u8 *out, u32 outCap, u32 num, u16 seq)
{
    u32 pos = 4;
    u32 objectStart = pos;
    u32 objectLen = 0;

    if (out == NULL || outCap < 32)
        return 0;
    out[pos++] = 1;
    out[pos++] = 25;
    out[pos++] = 7;
    out[pos++] = 0;
    out[pos++] = 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "num", num) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", seq))
    {
        return 0;
    }
    objectLen = pos - objectStart;
    out[objectStart + 3] = (u8)(objectLen >> 8);
    out[objectStart + 4] = (u8)objectLen;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    return pos;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_state roleBefore;
    vm_net_mock_battle_insight_pending *pending;
    u8 request[64];
    u8 response[1024];
    u32 requestLen = 0;
    u32 responseLen = 0;
    u8 kind = 0;
    u8 subtype = 0;
    u8 result = 0;
    u32 maxnum = 0;
    char itemInfo[8] = {0};

    memset(&role, 0, sizeof(role));
    role.roleId = 650003;
    role.backpackCapacity = 20;
    role.backpackItemCount = 1;
    role.backpackItems[0].itemId = 828;
    role.backpackItems[0].seq = 56;
    role.backpackItems[0].count = 2;
    roleBefore = role;
    g_vm_mock_service_active_client_id = 0xB1730003u;
    vm_net_mock_battle_insight_pending_clear(
        g_vm_mock_service_active_client_id, role.roleId);
    if (!vm_net_mock_battle_insight_pending_prepare(&role, 56, 2) ||
        memcmp(&role, &roleBefore, sizeof(role)) != 0)
    {
        fputs("battle-insight 25/6 preparation mutated role state\n", stderr);
        return 1;
    }
    memset(response, 0, sizeof(response));
    responseLen = 5;
    if (!vm_net_mock_begin_wt_object(response, sizeof(response), &responseLen,
                                     1, 25, 6, NULL) ||
        !vm_net_mock_append_battle_insight_use_fields(
            response, sizeof(response), &responseLen, true,
            vm_net_mock_battle_insight_use_maxnum(true, &role.backpackItems[0]),
            vm_net_mock_battle_insight_quantity_window_info()))
    {
        fputs("unable to build battle-insight quantity window\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_object(response, 5, responseLen);
    vm_net_mock_finish_wt_packet(response, responseLen, 1);
    if (!vm_net_mock_get_object_u8_field(response, responseLen, "result", &result) ||
        result != 1 ||
        !vm_net_mock_get_object_u32_field(response, responseLen, "maxnum", &maxnum) ||
        maxnum != 2 ||
        !vm_net_mock_get_object_string_field(response, responseLen, "iteminfo",
                                             itemInfo, sizeof(itemInfo)) ||
        itemInfo[0] != '\0')
    {
        fputs("battle-insight 25/6 window prematurely reports an active effect\n",
              stderr);
        return 1;
    }
    pending = vm_net_mock_battle_insight_pending_find(
        g_vm_mock_service_active_client_id, role.roleId, false);
    if (pending == NULL || pending->seq != 56 ||
        pending->state != VM_NET_MOCK_BATTLE_INSIGHT_PENDING_PREPARED ||
        pending->maxQuantity != 2 ||
        pending->confirmedQuantity != 0)
    {
        fputs("battle-insight 25/6 preparation context missing\n", stderr);
        return 1;
    }
    if (!vm_net_mock_battle_insight_pending_prepare(&role, 57, 2) ||
        pending->seq != 57 ||
        pending->state != VM_NET_MOCK_BATTLE_INSIGHT_PENDING_PREPARED ||
        pending->maxQuantity != 2)
    {
        fputs("new battle-insight window did not supersede pending sequence\n",
              stderr);
        return 1;
    }
    pending->state = VM_NET_MOCK_BATTLE_INSIGHT_PENDING_COMMITTED;
    pending->confirmedQuantity = 2;
    if (pending->seq != 57 || pending->confirmedQuantity != 2 ||
        pending->state != VM_NET_MOCK_BATTLE_INSIGHT_PENDING_COMMITTED)
    {
        fputs("battle-insight confirmation idempotency record mismatch\n", stderr);
        return 1;
    }
    if (vm_net_mock_battle_insight_confirm_pending(&role, 57, 3, NULL))
    {
        fputs("battle-insight accepted a duplicate with a different quantity\n",
              stderr);
        return 1;
    }
    vm_net_mock_battle_insight_pending_clear(
        g_vm_mock_service_active_client_id, role.roleId);
    g_vm_mock_service_active_client_id = 0;

    memset(request, 0, sizeof(request));
    memset(response, 0, sizeof(response));
    requestLen = build_followup_request(request, sizeof(request), 1, 56);
    responseLen = vm_net_mock_build_response(request, requestLen,
                                             response, sizeof(response));
    if (requestLen != 31 || responseLen == 0 || response[4] != 1 ||
        !vm_net_mock_get_first_object_kind_subtype(response, responseLen,
                                                   &kind, &subtype) ||
        kind != 25 || subtype != 7 ||
        !vm_net_mock_get_object_u8_field(response, responseLen, "result",
                                         &result) ||
        result != 2)
    {
        fputs("battle-insight 25/7 accepted missing confirmation context\n",
              stderr);
        return 1;
    }

    requestLen = build_followup_request(request, sizeof(request), 0, 56);
    if (requestLen == 0 ||
        vm_net_mock_build_response(request, requestLen,
                                   response, sizeof(response)) != 0)
    {
        fputs("battle-insight 25/7 accepted zero num\n", stderr);
        return 1;
    }

    printf("battle-insight-followup-v9 passed: 25/6 returns the selected "
           "stack's tagged-u32 count without role mutation; 25/7 requires "
           "matching bounded context and keeps an idempotency record\n");
    return 0;
}
