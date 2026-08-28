#include "mock_server.h"

static const char *vm_net_mock_exp_card_active_info(u32 multiplier)
{
    switch (multiplier)
    {
    case 2:
        return "\xCB\xAB\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
    case 4:
        return "\xCB\xC4\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
    case 10:
        return "\xCA\xAE\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
    default:
        return "";
    }
}

static bool vm_net_mock_is_exp_card_status_request(const u8 *request,
                                                   u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    return request != NULL && requestLen >= 9 && request[0] == 'W' &&
           request[1] == 'T' &&
           vm_net_mock_next_request_object(request, requestLen, &offset, &object) &&
           offset == requestLen && object.major == 1 && object.kind == 7 &&
           object.subtype == 31;
}

u32 vm_net_mock_build_exp_card_status_response(const u8 *request,
                                               u32 requestLen,
                                               u8 *out, u32 outCap)
{
    u32 multiplier = 1;
    u32 pos = 5;
    u32 objectStart = 0;

    if (!vm_net_mock_is_exp_card_status_request(request, requestLen) ||
        out == NULL || outCap < pos)
    {
        return 0;
    }

    multiplier = vm_net_mock_role_active_exp_card_multiplier(vm_net_mock_active_role());
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 31, &objectStart) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "expinfo",
                                       vm_net_mock_exp_card_active_info(multiplier)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (multiplier <= 1)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 32, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "expcard", 0))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
    }
    vm_net_mock_finish_wt_packet(out, pos, multiplier > 1 ? 1 : 2);

    printf("[info][network] mock_exp_card_status multiplier=%u active=%u response=%u evidence=JianghuOL.CBE:0x0100E3B8+0x01011A5E\\n",
           multiplier, multiplier > 1 ? 1u : 0u, pos);
    return pos;
}

/* Clicking the scene's battle-insight badge emits exactly one empty
 * `1/7/36` object. JianghuOL.CBE:0x01011C88 dispatches subtype 36 to
 * 0x01011A1E.  That handler obtains `bookinfo` and passes the returned
 * pointer directly to the native text formatter, so this one field must use
 * the WT blob wrapper with an inner NUL terminator.  This is a
 * status-description request only: it neither consumes an item nor changes
 * the active timed effect. */
static bool vm_net_mock_is_battle_insight_status_request(const u8 *request,
                                                          u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    return request != NULL && requestLen == 9 && request[0] == 'W' &&
           request[1] == 'T' &&
           vm_net_mock_next_request_object(request, requestLen, &offset,
                                           &object) &&
           offset == requestLen && object.major == 1 && object.kind == 7 &&
           object.subtype == 36 && object.payloadLen == 0;
}

static const char *vm_net_mock_battle_insight_status_info(bool active)
{
    if (active)
        return vm_net_mock_special_item_success_info(828);
    /* 当前未使用战斗心得。 */
    return "\xB5\xB1\xC7\xB0\xCE\xB4\xCA\xB9\xD3\xC3\xD5\xBD\xB6\xB7\xD0\xC4\xB5\xC3\xA1\xA3";
}

u32 vm_net_mock_build_battle_insight_status_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    const bool active = vm_net_mock_role_active_battle_insight_flag() != 0;
    const char *bookInfo = vm_net_mock_battle_insight_status_info(active);
    u32 pos = 5;
    u32 objectStart = 0;

    if (!vm_net_mock_is_battle_insight_status_request(request, requestLen) ||
        out == NULL || outCap < pos || bookInfo == NULL ||
        bookInfo[0] == '\0')
    {
        return 0;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 36,
                                     &objectStart) ||
        !vm_net_mock_put_object_cstring(out, outCap, &pos, "bookinfo",
                                        bookInfo))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_battle_insight_status request=7/36 active=%u response=%u action=client-description evidence=JianghuOL.CBE:0x01011A1E\\n",
           active ? 1u : 0u, pos);
    vm_autotest_note("mock_battle_insight_status request=7/36 active=%u response=%u action=client-description evidence=JianghuOL.CBE:0x01011A1E\\n",
                     active ? 1u : 0u, pos);
    return pos;
}
