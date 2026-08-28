#include "mock_server.h"

u32 vm_net_mock_build_battle_death_prompt_error_response(u8 *out, u32 outCap,
                                                         const char *info)
{
    u32 pos = 5;
    u32 objectStart = 0;
    /* 20/1.info is consumed by the same CBE text renderer as other game
     * notices, therefore both the fallback and all callers use GBK bytes. */
    static const char revivalStoneUnavailableGbk[] =
        "\xB8\xB4\xBB\xEE\xCA\xAF\xB2\xBB\xBF\xC9\xD3\xC3"; /* 复活石不可用 */

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 20, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos,
                                       "info", info ? info : revivalStoneUnavailableGbk))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}
