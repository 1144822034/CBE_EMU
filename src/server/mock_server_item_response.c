#include "mock_server.h"

u32 vm_net_mock_build_item_use_hint_response(u8 *out, u32 outCap, const char *hint)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 16, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "hint", hint ? hint : "OK"))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

bool vm_net_mock_append_item_use_success_notice_object(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount, const char *msg)
{
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || objectCount == NULL || *objectCount == 0xff)
        return false;
    if (msg == NULL || msg[0] == 0)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 37,
                                     &objectStart) ||
        !vm_net_mock_put_object_string(out, outCap, pos, "msg", msg) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    ++*objectCount;
    return true;
}

/* 7/11 is an in-place quantity stream, not a single-row acknowledgement.
 * A role can legitimately have several physical rows for one stackable item
 * after prior grants (for example 9+10).  Updating only the selected seq makes
 * the UI show 9 and leaves the other rows stale.  Emit every surviving row,
 * plus the selected seq at zero when that row was consumed completely. */
bool vm_net_mock_build_item_use_count_rows_blob(
    u8 *out, u32 outCap, const vm_net_mock_role_state *role,
    u32 itemId, u16 selectedSeq, u32 selectedRemaining, u32 *blobLenOut)
{
    u32 pos = 0;
    u8 rowCount = 0;
    bool selectedSeen = false;
    u8 itemCount = vm_net_mock_role_backpack_count(role);

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || role == NULL || itemId == 0)
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
        return false;

    if (selectedSeq != 0)
    {
        if (!vm_net_mock_seq_put_i16(out, outCap, &pos, selectedSeq) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, selectedRemaining))
        {
            return false;
        }
        rowCount = 1;
        selectedSeen = true;
    }
    for (u8 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *row = &role->backpackItems[i];
        if (row->itemId != itemId || row->seq == 0 ||
            (selectedSeen && row->seq == selectedSeq))
        {
            continue;
        }
        if (rowCount == 0xff ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, row->seq) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, row->count))
        {
            return false;
        }
        ++rowCount;
    }
    out[2] = rowCount;
    *blobLenOut = pos;
    return rowCount != 0;
}
