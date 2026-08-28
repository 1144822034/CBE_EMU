#include "mock_server.h"

u32 vm_net_mock_build_guild_kick_response(const u8 *request, u32 requestLen,
                                           u8 *out, u32 outCap)
{
    vm_net_mock_guild_kick_action action;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 guildId = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 requesterRank = 0;
    u8 targetRank = 0;
    u8 result = 5;

    memset(&action, 0, sizeof(action));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_guild_kick_action(request, requestLen, &action))
    {
        return 0;
    }
    result = vm_net_mock_apply_guild_kick_action(&action, role, &guildId,
                                                  &requesterRank, &targetRank);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 40, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_kick requester=%u guild=%u requester_rank=%u "
           "target=%u requested_rank=%u target_rank=%u valid=%u result=%u resp=%u "
           "evidence=JianghuOL.CBE:0x01042D18+0x01042D92+0x01042F16+0x01042E3E\n",
           role ? role->roleId : 0, guildId, requesterRank,
           action.roleId, action.memberRank, targetRank,
           action.valid ? 1u : 0u, result, pos);
    return pos;
}
