#include "mock_server.h"

bool vm_net_mock_is_login_tail_skill_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen != 14)
        return false;
    if (request[0] != 'W' || request[1] != 'T')
        return false;

    u32 offset = 4;
    vm_net_mock_request_object object;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x0c || object.subtype != 1 || object.payloadLen != 0)
        return false;

    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 42 || object.payloadLen != 0)
        return false;

    return offset == requestLen;
}

u32 vm_net_mock_build_login_tail_skill_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos, &objectCount,
                                                      false))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}
