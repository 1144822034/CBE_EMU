#include "mock_server.h"

u32 vm_net_mock_build_training_book_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_mock_service_training_book_use_view view;
    u16 itemSeq = 0;
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 40,
                                                    "seq", false, &itemSeq) ||
        !vm_mock_service_training_book_use(itemSeq, &view))
    {
        return 0;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 40, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", view.success ? 0 : 1) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "bookinfo", view.bookInfo))
    {
        return 0;
    }
    if (view.success)
    {
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "exp", view.exp) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "level", view.level) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "lastexp", view.lastExp) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "curexp", view.nextLevelExp) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "persentexp", view.percentExp))
        {
            return 0;
        }
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_training_book_use role=%u seq=%u book_level=%u book_exp=%u recipient_level=%u success=%u item_remaining=%u response=%u evidence=JianghuOL.CBE:0x010238B6+0x01025AE6(case40)+0x01046EDA\\n",
           view.roleId, itemSeq, view.bookLevel, view.bookExperience,
           view.recipientLevel, view.success ? 1u : 0u, view.itemRemaining, pos);
    return pos;
}

u32 vm_net_mock_build_unresolved_special_item_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_mock_service_training_book_description_view view;
    u16 requestedSeq = 0;
    u8 subtype = 0;
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 35,
                                                    "seq", false, &requestedSeq))
    {
        subtype = 35;
    }
    else if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 38,
                                                         "seq", false, &requestedSeq))
    {
        subtype = 38;
    }
    else
    {
        return 0;
    }
    if (!vm_mock_service_training_book_description(subtype, requestedSeq, &view) ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, subtype, &objectStart))
    {
        return 0;
    }
    if (subtype == 35)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "bookdes", view.bookInfo))
        {
            return 0;
        }
    }
    else if (subtype == 38 &&
             !vm_net_mock_put_object_string(out, outCap, &pos, "bookdes", view.bookInfo))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (view.trainingBookLoaded)
    {
        printf("[info][network] mock_training_book_instance_read request=7/%u seq=%u role=%u action=not-consumed response=%u evidence=JianghuOL.CBE:0x010238B6+0x01025AE6\\n",
               subtype, requestedSeq, view.roleId, pos);
    }
    else
    {
        printf("[warn][network] mock_special_item_unresolved request=7/%u seq=%u action=not-consumed response=%u evidence=JianghuOL.CBE:0x01025AE6\\n",
               subtype, requestedSeq, pos);
    }
    return pos;
}
