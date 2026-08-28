#include "mock_server.h"

bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen,
                                            u8 kind, u8 subtype)
{
    if (request == NULL || requestLen != 9)
        return false;
    return request[0] == 'W' &&
           request[1] == 'T' &&
           request[2] == 0 &&
           request[3] == 9 &&
           request[4] == 1 &&
           request[5] == kind &&
           request[6] == subtype &&
           request[7] == 0 &&
           request[8] == 5;
}

static u32 vm_net_mock_build_empty_wt_ack_response(u8 *out, u32 outCap)
{
    if (outCap < 5)
        return 0;
    vm_net_mock_finish_wt_packet(out, 5, 0);
    return 5;
}

u32 vm_net_mock_build_short_wt_control_ack_response(const u8 *request,
                                                     u32 requestLen,
                                                     u8 kind, u8 subtype,
                                                     u8 *out, u32 outCap)
{
    (void)request;
    (void)requestLen;
    (void)kind;
    (void)subtype;
    /*
     * 0x63/1 sits on the startup/login bridge. The old echo copied the
     * request-side short-object layout back to the client, but response parsing
     * uses the normal WT object layout. That malformed echo trips
     * event_packet_init() and shows the unpack-error popup. A zero-object WT ack
     * still delivers the network event without feeding business dispatch an
     * unsupported 0x63 object.
     */
    return vm_net_mock_build_empty_wt_ack_response(out, outCap);
}

u32 vm_net_mock_build_practise_info18_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_practise_info info;

    if (outCap < pos || role == NULL ||
        !vm_net_mock_practise_get_info(role, &info))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 18, &objectStart))
        return 0;
    /*
     * sub_102CB46 handles the active practise-info panel. For subtype 18 it
     * reads todaypasthour, todaypastmin, getexp, todaylasthour,
     * todaylastmin, alllasthour, alllastmin, then isgold.
     */
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaypasthour",
                                    info.todayPastHours))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaypastmin",
                                    info.todayPastMinutes))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "getexp", info.gainedExp))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaylasthour",
                                    info.todayRemainingHours))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaylastmin",
                                    info.todayRemainingMinutes))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "alllasthour",
                                    info.allRemainingHours))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "alllastmin",
                                    info.allRemainingMinutes))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "isgold", info.goldEnabled))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

/* JianghuOL.CBE:HandleExpBattleAction(0x0102C3D6) emits precisely one
 * 1/7/21 object with `opengold` when the cultivation setting is confirmed.
 * HandleExpBattleResponse(0x0102CB46) waits for this matching subtype and
 * reads only `result`; an empty acknowledgement leaves the loading dialog
 * active. */
u32 vm_net_mock_build_practise_setting21_response(const u8 *request,
                                                  u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    u32 offset = 4;
    u32 openGold = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    bool success = false;

    if (request == NULL || requestLen < 9 || out == NULL || outCap < pos ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 7 ||
        object.subtype != 21 ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "opengold", &openGold) ||
        openGold > 1)
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    success = vm_net_mock_practise_set_gold(role, openGold != 0);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 21, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 0))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_practise_setting21 role=%u opengold=%u result=%u response=%u evidence=JianghuOL.CBE:0x0102C3D6+0x0102CB46\n",
           role ? role->roleId : 0, openGold, success ? 1u : 0u, pos);
    return pos;
}

/* JianghuOL.CBE:HandleTradeInput(0x0102C3D6) sends this exact one-object
 * request when the cultivation panel's help action is selected.  The
 * response parser (HandleExpBattleResponse, 0x0102CB46 case 19) reads only
 * `helpinfo` as a GBK byte string and then releases the pending dialog. */
static bool vm_net_mock_is_practise_help19_request(const u8 *request,
                                                   u32 requestLen)
{
    vm_net_mock_request_object object;
    u32 offset = 4;
    u32 requestType = 0;

    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                         &object) ||
        offset != requestLen || object.major != 1 || object.kind != 7 ||
        object.subtype != 19 ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "type", &requestType))
    {
        return false;
    }
    return requestType == 0;
}

/* 827 修炼丹 is backed by the same account/role lifecycle as the practise
 * panel.  7/16 is the native quantity preflight; it must not mutate durable
 * state.  The CBE sends the selected count in the distinct 7/17 request,
 * which owns both the debit and HandleItemUseResponse completion lifecycle. */
u32 vm_net_mock_build_practise_pill16_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = NULL;
    u16 itemSeq = 0;
    u32 maxUse = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    bool success = false;
    const char *itemInfo = "";

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 16,
                                                    "itemseq", false, &itemSeq))
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    success = vm_net_mock_practise_pill_max_usable(role, itemSeq, &maxUse);
    if (!success)
    {
        itemInfo =
            "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xD2\xD1\xB4\xEF\xB5\xBD\xC0\xDB\xBC\xC6\xC9\xCF\xCF\xDE\xA3\xAC\xCE\xB4\xCA\xB9\xD3\xC3\xA1\xA3"; /* 修炼时间已达到累计上限，未使用。 */
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 16, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 2) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", maxUse) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", itemInfo))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (success)
        vm_mock_service_arm_practise_pill17_followup(role, itemSeq, maxUse);
    else
        vm_mock_service_clear_practise_pill17_followup(role, itemSeq,
                                                        "1/7/16-rejected");
    printf("[info][network] mock_practise_pill16 role=%u seq=%u success=%u max_use=%u action=preflight response=%u evidence=item.dsh:827+JianghuOL.CBE:0x0102355E+0x0102615A\n",
           role ? role->roleId : 0, itemSeq, success ? 1u : 0u,
           maxUse, pos);
    return pos;
}

/* Runtime after 827's successful 1/7/16 reply: the CBE emits exactly one
 * 38-byte 1/7/17 request.  Its 29-byte object payload is fully accounted for
 * by usenum:tagged-u32(the quantity selected by the player) and
 * itemseq:tagged-u16.  Do not generalize this detector to item use:
 * 0x0102C104 only gives this request's result/useinfo/pcimg shell meaning
 * after the preceding 7/16 quantity preflight. */
static bool vm_net_mock_is_practise_pill17_followup_request(
    const u8 *request, u32 requestLen, u16 *itemSeqOut, u32 *useNumOut)
{
    vm_net_mock_request_object object;
    u32 offset = 4;
    u32 itemSeq = 0;
    u32 useNum = 0;

    if (itemSeqOut != NULL)
        *itemSeqOut = 0;
    if (useNumOut != NULL)
        *useNumOut = 0;
    if (request == NULL || requestLen != 38 || request[0] != 'W' ||
        request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 7 ||
        object.subtype != 17 || object.payloadLen != 29 ||
        !vm_net_mock_get_object_tagged_number_entry(
            object.payload, object.payloadLen, "usenum", &useNum) ||
        !vm_net_mock_get_object_tagged_number_entry(
            object.payload, object.payloadLen, "itemseq", &itemSeq) ||
        useNum == 0 || useNum > 0xffffu || itemSeq == 0 ||
        itemSeq > 0xffffu)
    {
        return false;
    }
    if (itemSeqOut != NULL)
        *itemSeqOut = (u16)itemSeq;
    if (useNumOut != NULL)
        *useNumOut = useNum;
    return true;
}

/* JianghuOL.CBE:0x0102C104 handles 7/17 separately from 7/16.  On result=1
 * it emits the CBE-owned event 100 and lets the original UI code close the
 * progress state.  `pcimg=1` is the established no-fixed-status-image value;
 * 827 changes stored practise time, not the scene's fixed "修" badge. */
u32 vm_net_mock_build_practise_pill17_followup_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = NULL;
    u16 itemSeq = 0;
    u32 useNum = 0;
    u32 practiseMinutes = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    bool replay = false;
    bool rejected = false;
    bool success = false;
    char useInfo[64];

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_practise_pill17_followup_request(
            request, requestLen, &itemSeq, &useNum))
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    if (!vm_mock_service_practise_pill17_followup_matches(
            role, itemSeq, useNum, &replay, &rejected))
        return 0;
    if (replay)
    {
        success = true;
    }
    else if (!rejected)
    {
        success = vm_net_mock_practise_use_pill(role, itemSeq, useNum,
                                                 &practiseMinutes);
        if (success)
            vm_mock_service_commit_practise_pill17_followup(role, itemSeq,
                                                             useNum);
    }
    if (success)
    {
        snprintf(useInfo, sizeof(useInfo),
                 "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xD4\xF6\xBC\xD3%u\xD0\xA1\xCA\xB1\xA1\xA3",
                 useNum); /* 修炼时间增加N小时。 */
    }
    else
    {
        useInfo[0] = '\0';
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 17,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 2) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "useinfo", useInfo) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "pcimg", 1))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_practise_pill17_followup role=%u seq=%u usenum=%u result=%u replay=%u rejected=%u practise_minutes=%u pcimg=1 response=%u evidence=runtime:1/7/16->1/2/10->1/7/17+JianghuOL.CBE:0x0102C032+0x0102C104\n",
           role ? role->roleId : 0, (u32)itemSeq, useNum, success ? 1u : 2u,
           replay ? 1u : 0u, rejected ? 1u : 0u, practiseMinutes, pos);
    vm_autotest_note("mock_practise_pill17_followup role=%u seq=%u usenum=%u result=%u replay=%u rejected=%u practise_minutes=%u pcimg=1 response=%u evidence=runtime:1/7/16->1/2/10->1/7/17+JianghuOL.CBE:0x0102C032+0x0102C104\n",
                     role ? role->roleId : 0, (u32)itemSeq, useNum,
                     success ? 1u : 2u, replay ? 1u : 0u,
                     rejected ? 1u : 0u, practiseMinutes, pos);
    fflush(stdout);
    return pos;
}

/* 833 聚元丹 uses the same CBE result shell as 827, but its `maxnum` is the
 * current vitality, not a disguised HP/MP amount.  result=1 is emitted only
 * after the exact selected stack and account_role_vitality row committed in
 * one transaction.  HandleShopBuyItem's 7/33 branch does not write the
 * role's energy cache, so a successful operation is followed by the native
 * 2/13 energy update that net_handle_actor_move_info already consumes. */
u32 vm_net_mock_build_vitality_pill33_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = NULL;
    u16 itemSeq = 0;
    u32 current = 0;
    u32 maximum = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 responseObjectCount = 1;
    bool success = false;
    const char *itemInfo =
        "\xBB\xEE\xC1\xA6\xD2\xD1\xC2\xFA\xA3\xAC\xCE\xB4\xCA\xB9\xD3\xC3\xA1\xA3"; /* 活力已满，未使用。 */

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 33,
                                                    "itemseq", false, &itemSeq))
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    success = vm_net_mock_vitality_use_pill(role, itemSeq, &current, &maximum);
    if (success)
    {
        itemInfo = "\xBB\xEE\xC1\xA6\xBB\xD6\xB8\xB4\xB3\xC9\xB9\xA6\xA1\xA3"; /* 活力恢复成功。 */
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 33, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 2) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", current) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", itemInfo))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (success)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 13,
                                         &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "energy", current) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "energymax", maximum))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        responseObjectCount = 2;
    }
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
    printf("[info][network] mock_vitality_pill33 role=%u seq=%u success=%u vitality=%u/%u response_objects=%u response=%u evidence=JianghuOL.CBE:0x0102355E+0x01025AE6+0x01012ADC(case13),item.dsh:833\n",
           role ? role->roleId : 0, itemSeq, success ? 1u : 0u,
           current, maximum, responseObjectCount, pos);
    return pos;
}

u32 vm_net_mock_build_practise_help19_response(const u8 *request,
                                               u32 requestLen,
                                               u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    /* GBK: 修炼帮助：\r\n使用修炼丹可增加修炼时间，每颗增加1小时。\r\n
     * 角色离线后自动修炼。普通修炼每日最多8小时；黄金修炼每日最多4小时，经验翻倍。\r\n
     * 修炼时间最多累计100小时。 */
    static const char helpInfoGbk[] =
        "\xD0\xDE\xC1\xB6\xB0\xEF\xD6\xFA\xA3\xBA\x0D\x0A"
        "\xCA\xB9\xD3\xC3\xD0\xDE\xC1\xB6\xB5\xA4\xBF\xC9\xD4\xF6\xBC\xD3"
        "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xA3\xAC\xC3\xBF\xBF\xC5\xD4\xF6\xBC\xD3"
        "\x31\xD0\xA1\xCA\xB1\xA1\xA3\x0D\x0A"
        "\xBD\xC7\xC9\xAB\xC0\xEB\xCF\xDF\xBA\xF3\xD7\xD4\xB6\xAF\xD0\xDE\xC1\xB6"
        "\xA1\xA3\xC6\xD5\xCD\xA8\xD0\xDE\xC1\xB6\xC3\xBF\xC8\xD5\xD7\xEE\xB6\xE0"
        "\x38\xD0\xA1\xCA\xB1\xA1\xA3\xBB\xBB\xC6\xBD\xF0\xD0\xDE\xC1\xB6\xC3\xBF\xC8"
        "\xD5\xD7\xEE\xB6\xE0\x34\xD0\xA1\xCA\xB1\xA1\xA3\xAC\xBE\xAD\xD1\xE9\xB7\xAD"
        "\xB1\xB6\xA1\xA3\x0D\x0A"
        "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xD7\xEE\xB6\xE0\xC0\xDB\xBC\xC6\x31\x30"
        "\x30\xD0\xA1\xCA\xB1\xA1\xA3";

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_practise_help19_request(request, requestLen) ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 19,
                                     &objectStart) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "helpinfo",
                                       helpInfoGbk))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_practise_help19 type=0 response=%u evidence=JianghuOL.CBE:0x0102C3D6+0x0102CB46(case19)\n",
           pos);
    return pos;
}
