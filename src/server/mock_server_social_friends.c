#include "mock_server.h"

static bool vm_net_mock_is_friend_page_request(const u8 *request, u32 requestLen,
                                                u32 *indexOut, u8 *pageSizeOut)
{
    u32 offset = 4;
    u32 index = 0;
    u8 pageSize = 0;
    vm_net_mock_request_object object;

    if (indexOut)
        *indexOut = 0;
    if (pageSizeOut)
        *pageSizeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen ||
        object.major != 1 || object.kind != 10 || object.subtype != 1)
    {
        return false;
    }
    /* JianghuOL.CBE:SendPagedListReq(0x0101A5EE) writes these exact fields. */
    if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "index", &index) ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "pageSize", &pageSize) ||
        pageSize == 0)
    {
        return false;
    }
    if (indexOut)
        *indexOut = index;
    if (pageSizeOut)
        *pageSizeOut = pageSize;
    return true;
}

u32 vm_net_mock_build_friend_page_response(const u8 *request, u32 requestLen,
                                           u8 *out, u32 outCap)
{
    u8 friendInfo[8192];
    vm_mock_service_friend_record friendRecords[VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS];
    vm_net_mock_role_state *ownerRole = vm_net_mock_active_role();
    const char *ownerAccountId = vm_mock_service_active_account_id();
    u32 index = 0;
    u32 friendInfoLen = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u32 totalPages = 1;
    u32 totalFriends = 0;
    u32 skippedFriends = 0;
    u32 friendRecordCount = 0;
    u16 rowCount = 0;
    u8 pageSize = 0;
    u8 allPages8 = 1;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_friend_page_request(request, requestLen, &index, &pageSize))
    {
        return 0;
    }

    /*
     * HandleFriendInfoResponse(0x0102FF54) first reads an i16 row count, then
     * consumes {u32 id, string name, u8 state, u32 attr32, u8 attr8}.
     * SortFriendListByOnline(0x0102FD86) proves state 1/2 are the live-row
     * family. HandleFriendResponse(0x0102157A) matches friendid and adds
     * addedfd to attr32, proving that attr32 is the persisted friend degree.
     */
    if (ownerRole != NULL)
    {
        friendRecordCount = vm_mock_service_friend_record_collect(
            ownerRole->roleId, ownerAccountId, friendRecords,
            VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS);
    }
    totalFriends = friendRecordCount;
    if (totalFriends > 0)
        totalPages = (totalFriends + pageSize - 1u) / pageSize;
    allPages8 = (u8)(totalPages > 0xffu ? 0xffu : totalPages);

    if (!vm_net_mock_seq_put_i16(friendInfo, sizeof(friendInfo), &friendInfoLen, 0))
        return 0;
    if (ownerRole != NULL)
    {
        for (u32 i = 0; i < friendRecordCount; ++i)
        {
            const vm_mock_service_friend_record *record = &friendRecords[i];
            vm_mock_service_client_session *onlineSession = NULL;
            vm_mock_service_online_session_view onlineView;
            const char *friendName = NULL;
            u8 friendState = 0;
            u8 friendAttr8 = 1;

            if (skippedFriends < index)
            {
                ++skippedFriends;
                continue;
            }
            if (rowCount >= pageSize)
                break;
            onlineSession = vm_mock_service_find_online_friend_session(record);
            memset(&onlineView, 0, sizeof(onlineView));
            if (onlineSession != NULL &&
                vm_mock_service_session_get_online_view(onlineSession, &onlineView))
            {
                friendName = onlineView.onlineRoleName[0] ? onlineView.onlineRoleName :
                             record->targetRoleName;
                friendState = 1;
                friendAttr8 = onlineView.onlineJob ? onlineView.onlineJob :
                              record->targetJob;
            }
            else
            {
                friendName = record->targetRoleName;
                friendAttr8 = record->targetJob;
            }
            if (!vm_net_mock_seq_put_u32(friendInfo, sizeof(friendInfo), &friendInfoLen,
                                         record->targetRoleId) ||
                !vm_net_mock_seq_put_string(friendInfo, sizeof(friendInfo), &friendInfoLen,
                                            friendName && friendName[0] ? friendName : "Player") ||
                !vm_net_mock_seq_put_u8(friendInfo, sizeof(friendInfo), &friendInfoLen,
                                        friendState) ||
                !vm_net_mock_seq_put_u32(friendInfo, sizeof(friendInfo), &friendInfoLen,
                                         record->friendDegree) ||
                !vm_net_mock_seq_put_u8(friendInfo, sizeof(friendInfo), &friendInfoLen,
                                        friendAttr8 ? friendAttr8 : 1))
            {
                return 0;
            }
            ++rowCount;
        }
    }
    /* Patch the typed i16 row count emitted at the head of friendinfo. */
    friendInfo[2] = (u8)(rowCount >> 8);
    friendInfo[3] = (u8)rowCount;

    if (friendInfoLen > 0xffff ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 1, &objectStart))
    {
        return 0;
    }
    /* HandleFriendInfoResponse reads allpgs through LookupItemByteField. */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "allpgs", allPages8) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "friendinfo",
                                    friendInfo, (u16)friendInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    printf("[info][network] mock_friend_page index=%u page_size=%u owner=%s/%u total=%u rows=%u allpgs=%u friendinfo_len=%u resp=%u\n",
           index,
           pageSize,
           ownerAccountId ? ownerAccountId : "-",
           ownerRole ? ownerRole->roleId : 0,
           totalFriends,
           rowCount,
           totalPages,
           friendInfoLen,
           pos);
    vm_autotest_note("mock_friend_page index=%u page_size=%u owner=%u total=%u rows=%u response=10/1 evidence=JianghuOL.CBE:0x0101A5EE+0x0102FF54+0x0102157A\n",
                     index,
                     pageSize,
                     ownerRole ? ownerRole->roleId : 0,
                     totalFriends,
                     rowCount);
    return pos;
}

/* The friend screen sends the removal and its normal page reload together:
 *
 *   1/10/9 { id } + 1/10/1 { index, pageSize }
 *
 * The main CBE kind-10 dispatcher has no subtype-9 response branch.  Its
 * page callback, however, consumes the following 10/1 response and clears the
 * screen's network wait.  Preserve that client-owned lifecycle by returning
 * only the post-transaction page object, never an invented 10/9 result. */
u32 vm_net_mock_build_friend_remove_and_page_response(const u8 *request,
                                                       u32 requestLen,
                                                       u8 *out, u32 outCap)
{
    vm_net_mock_role_state *ownerRole = vm_net_mock_active_role();
    const char *ownerAccountId = vm_mock_service_active_account_id();
    vm_net_mock_request_object removeObject;
    vm_net_mock_request_object pageObject;
    u8 pageRequest[128];
    u32 offset = 4;
    u32 targetRoleId = 0;
    u32 pageRequestLen = 0;
    bool removed = false;
    bool persisted = false;
    u32 responseLen = 0;

    if (request == NULL || requestLen < 9 || out == NULL || outCap < 5 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &removeObject) ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &pageObject) ||
        offset != requestLen || removeObject.major != 1 || removeObject.kind != 10 ||
        removeObject.subtype != 9 || pageObject.major != 1 || pageObject.kind != 10 ||
        pageObject.subtype != 1 ||
        !vm_net_mock_get_object_u32_field(removeObject.payload,
                                          removeObject.payloadLen, "id", &targetRoleId) ||
        targetRoleId == 0 || pageObject.payloadLen + 9u > sizeof(pageRequest))
    {
        return 0;
    }

    /* Rebuild only the already validated 10/1 request.  The existing page
     * builder owns its exact allpgs/friendinfo format and reads the committed
     * authoritative relationship snapshot. */
    pageRequestLen = pageObject.payloadLen + 9u;
    memset(pageRequest, 0, sizeof(pageRequest));
    pageRequest[0] = 'W';
    pageRequest[1] = 'T';
    pageRequest[2] = (u8)(pageRequestLen >> 8);
    pageRequest[3] = (u8)pageRequestLen;
    memcpy(pageRequest + 4, pageObject.payload - 5, pageObject.payloadLen + 5u);

    if (ownerRole != NULL && ownerAccountId != NULL && ownerAccountId[0] != 0)
    {
        persisted = vm_mock_service_friend_db_remove_pair(ownerAccountId,
                                                           ownerRole->roleId,
                                                           targetRoleId,
                                                           &removed);
    }
    responseLen = vm_net_mock_build_friend_page_response(pageRequest,
                                                          pageRequestLen,
                                                          out, outCap);
    if (responseLen == 0)
        return 0;

    printf("[info][network] mock_friend_remove owner=%s/%u target=%u persisted=%u removed=%u response=10/1 resp=%u "
           "evidence=runtime:WT10/9+10/1;JianghuOL.CBE:0x01012E4C+0x010114FC\n",
           ownerAccountId ? ownerAccountId : "-", ownerRole ? ownerRole->roleId : 0,
           targetRoleId, persisted ? 1u : 0u, removed ? 1u : 0u, responseLen);
    vm_autotest_note("mock_friend_remove owner=%u target=%u persisted=%u removed=%u response=10/1 evidence=WT10/9+10/1\n",
                     ownerRole ? ownerRole->roleId : 0, targetRoleId,
                     persisted ? 1u : 0u, removed ? 1u : 0u);
    return responseLen;
}
