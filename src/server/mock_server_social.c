static u32 vm_net_mock_build_guild_kick_response(const u8 *request, u32 requestLen,
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

static u32 vm_net_mock_build_friend_page_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u8 friendInfo[8192];
    vm_net_mock_role_state *ownerRole = vm_net_mock_active_role();
    const char *ownerAccountId = g_vm_mock_service_active_account_id;
    u32 index = 0;
    u32 friendInfoLen = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u32 totalPages = 1;
    u32 totalFriends = 0;
    u32 skippedFriends = 0;
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
    vm_mock_service_friend_db_load();
    if (ownerRole != NULL && ownerAccountId != NULL && ownerAccountId[0] != 0 &&
        g_vm_mock_service_friend_db_valid)
    {
        for (u32 i = 0; i < g_vm_mock_service_friend_db.recordCount; ++i)
        {
            const vm_mock_service_friend_record *record = &g_vm_mock_service_friend_db.records[i];
            if (record->ownerRoleId == ownerRole->roleId &&
                strcmp(record->ownerAccountId, ownerAccountId) == 0)
            {
                ++totalFriends;
            }
        }
    }
    if (totalFriends > 0)
        totalPages = (totalFriends + pageSize - 1u) / pageSize;
    allPages8 = (u8)(totalPages > 0xffu ? 0xffu : totalPages);

    if (!vm_net_mock_seq_put_i16(friendInfo, sizeof(friendInfo), &friendInfoLen, 0))
        return 0;
    if (ownerRole != NULL && ownerAccountId != NULL && ownerAccountId[0] != 0)
    {
        for (u32 i = 0; i < g_vm_mock_service_friend_db.recordCount; ++i)
        {
            const vm_mock_service_friend_record *record = &g_vm_mock_service_friend_db.records[i];
            vm_mock_service_client_session *onlineSession = NULL;
            const char *friendName = NULL;
            u8 friendState = 0;
            u8 friendAttr8 = 1;

            if (record->ownerRoleId != ownerRole->roleId ||
                strcmp(record->ownerAccountId, ownerAccountId) != 0)
            {
                continue;
            }
            if (skippedFriends < index)
            {
                ++skippedFriends;
                continue;
            }
            if (rowCount >= pageSize)
                break;
            onlineSession = vm_mock_service_find_online_friend_session(record);
            friendName = onlineSession && onlineSession->onlineRoleName[0] ?
                         onlineSession->onlineRoleName : record->targetRoleName;
            friendState = onlineSession ? 1 : 0;
            friendAttr8 = onlineSession && onlineSession->onlineJob ?
                          onlineSession->onlineJob : record->targetJob;
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

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, const char *sceneOverride,
                                                               bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly,
                                                               bool preferSceneNpcOther);

static u32 vm_net_mock_build_group_type1_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen < 9 || outCap < 5)
        return 0;

    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 leadId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    bool hasGroup10 = false;
    bool hasType1 = false;
    bool enableMiscSync8 = vm_net_mock_env_u8("CBE_GROUP_TYPE1_MISC_SYNC8", 0) != 0;
    const char *scene = vm_net_mock_current_scene_name();
    /*
     * Runtime evidence now shows the first visible scene enter happens in the
     * later WT 12/1 follow-up, not in this earlier 5/10 group/type packet.
     * Keep 30/3 room+npc data out of 5/10 by default; otherwise NPC seeds are
     * delivered before 30/1 and never materialize on screen.
     *
     * Evidence:
     * - runtime: group/type1 packet arrives before the WT 49 scene/resource
     *   follow-up request and before the later 12/1 response.
     * - runtime: NPC list is absent on map even though mock_scene_room_npc_object
     *   is emitted here.
     * - IDA: scene enter parser is kind 30/subtype 1 (0x010396D6), while NPC
     *   list parser is kind 30/subtype 3 (0x01039222); ordering them as 30/3
     *   before 30/1 is weaker than the later scene-followup path.
     */
    bool includeRoomNpc = vm_net_mock_env_u8("CBE_GROUP_TYPE1_ROOM_NPC", 0) != 0;
    bool includeRoomRoles = vm_net_mock_env_u8("CBE_GROUP_TYPE1_ROOM_ROLES", 0) != 0;
    bool includeFbTargetClear = false;
    u8 sceneNpcNum = includeRoomNpc ? vm_net_mock_scene_room_npc_seed_count(scene) : 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.kind == 5 && object.subtype == 10)
        {
            hasGroup10 = true;
            vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &leadId);
        }
        else if (object.kind == 7 && object.subtype == 7)
        {
            u8 requestType = 0;
            if (vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &requestType) && requestType == 1)
                hasType1 = true;
        }
    }

    if (!hasGroup10)
        return 0;
    u32 pos = 5;
    u8 objectCount = 0;
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = session ? vm_mock_service_team_find_for_client(session->clientId) : NULL;
    if ((team != NULL && !vm_net_mock_append_team_group_info_object(out, outCap, &pos, team, session, 10)) ||
        (team == NULL && !vm_net_mock_append_group_info_object(out, outCap, &pos, leadId)))
        return 0;
    objectCount += 1;
    if (hasType1 && !vm_net_mock_append_type1_object(out, outCap, &pos, sceneNpcNum))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_backpack_role_grid_main_objects(out, outCap, &pos, &objectCount))
        return 0;
    if (hasType1 && enableMiscSync8 && !vm_net_mock_append_misc_player_sync8_object(out, outCap, &pos))
        return 0;
    if (hasType1 && enableMiscSync8)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_misc_player_type_object(out, outCap, &pos, 20))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && !vm_net_mock_append_misc_player_type_object(out, outCap, &pos, 32))
        return 0;
    if (hasType1)
        objectCount += 1;
    if (hasType1 && includeRoomNpc && !vm_net_mock_append_scene_room_npc_object(out, outCap, &pos))
        return 0;
    if (hasType1 && includeRoomNpc)
        objectCount += 1;
    if (hasType1 && includeRoomRoles && !vm_net_mock_append_scene_room_roles_object(out, outCap, &pos, NULL))
        return 0;
    if (hasType1 && includeRoomRoles)
        objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_scene_change_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_scene_change_target target;
    if (outCap < pos)
        return 0;
    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2, target.scene, target.x, target.y))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    return pos;
}

static bool vm_net_mock_append_role_skills_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u8 learnedSkill[512];
    u32 learnedSkillLen = 0;
    u8 learnedCount = 0;
    char learnedPreview[192];
    u8 roleJob = role ? role->job : 1;
    u8 rawJob = vm_net_mock_role_job_to_skill_raw_job(roleJob);

    memset(learnedSkill, 0, sizeof(learnedSkill));
    learnedPreview[0] = 0;
    learnedSkillLen = vm_net_mock_build_role_learned_skill_blob(role,
                                                                learnedSkill,
                                                                sizeof(learnedSkill),
                                                                &learnedCount,
                                                                learnedPreview,
                                                                sizeof(learnedPreview));

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0c, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u16(out, outCap, pos, "learnednum", learnedCount))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "learnedskill",
                                    learnedSkillLen ? learnedSkill : NULL,
                                    (u16)learnedSkillLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    vm_autotest_note("mock_role_skills role=%u role_job=%u raw_job=%u job_name=%s level=%u learned=%u ids=%s evidence=JianghuOL.CBE:0x1010594/0x103550E skill.dsh\n",
                     role ? role->roleId : 0,
                     roleJob,
                     rawJob,
                     vm_net_mock_skill_raw_job_name(rawJob),
                     role ? role->level : 1,
                     learnedCount,
                     learnedPreview);
    return true;
}

static bool vm_net_mock_append_login_tail_skill_objects(u8 *out, u32 outCap, u32 *pos, u8 *addedCount)
{
    u32 objectStart = 0;

    if (addedCount == NULL)
        return false;
    *addedCount = 0;
    if (!vm_net_mock_append_role_skills_object(out, outCap, pos))
        return false;
    *addedCount = (u8)(*addedCount + 1);

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 42, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "booknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "booksinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    *addedCount = (u8)(*addedCount + 1);

    if (!vm_net_mock_append_backpack_items_object(out, outCap, pos))
        return false;
    *addedCount = (u8)(*addedCount + 1);
    return true;
}

static bool vm_net_mock_append_books42_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 42, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "booknum", 0))
        return false;
    if (!vm_net_mock_put_object_blob(out, outCap, pos, "booksinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_empty11_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 11, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_scene_npcs11_object(u8 *out, u32 outCap, u32 *pos,
                                                             const char *scene,
                                                             u8 *npcNumOut,
                                                             u32 *npcInfoLenOut)
{
    u32 objectStart = 0;
    u8 npcInfo[1024];
    u32 npcInfoLen = 0;
    u8 npcNum = 0;

    if (npcNumOut)
        *npcNumOut = 0;
    if (npcInfoLenOut)
        *npcInfoLenOut = 0;
    if (!vm_net_mock_build_scene_npcinfo_blob(scene, npcInfo, sizeof(npcInfo), &npcNum, &npcInfoLen))
        return false;
    printf("[info][network] vm_net_mock_append_fb_target_scene_npcs11_object: scene=%s npcNum=%u npcInfoLen=%u\n",
           scene, (u32)npcNum, (u32)npcInfoLen);
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 11, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", npcNum))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "npcinfo", npcInfo, (u16)npcInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (npcNumOut)
        *npcNumOut = npcNum;
    if (npcInfoLenOut)
        *npcInfoLenOut = npcInfoLen;
    return true;
}

static bool vm_net_mock_append_scene_npcs11_once_or_empty(u8 *out, u32 outCap, u32 *pos,
                                                         const char *scene,
                                                         const char *phase)
{
    u8 npcNum = 0;
    u32 npcInfoLen = 0;

    vm_net_mock_reset_scene_moveinfo_npc_seed_if_needed(scene);
    if (g_vm_net_mock_scene_moveinfo_npc_seeded &&
        g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] != 0 &&
        scene != NULL &&
        vm_net_mock_scene_names_equal_loose(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                                            scene))
    {
        return vm_net_mock_append_fb_target_empty11_object(out, outCap, pos);
    }
    if (!vm_net_mock_append_fb_target_scene_npcs11_object(out, outCap, pos, scene,
                                                         &npcNum, &npcInfoLen))
    {
        return false;
    }
    g_vm_net_mock_scene_moveinfo_npc_pending = false;
    g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    g_vm_net_mock_scene_moveinfo_npc_seeded = true;
    snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
             sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene),
             "%s", scene ? scene : "");
    if (npcNum != 0)
        vm_mock_service_session_arm_task_prompt_refresh(scene);
    printf("[info][network] mock_scene_npc_seed phase=%s scene=%s source=27/11-catalog npcnum=%u npcinfo_len=%u once=1 evidence=JianghuOL.CBE:0x01037998\n",
           phase ? phase : "-",
           scene ? scene : "-",
           (u32)npcNum,
           npcInfoLen);
    return true;
}

static bool vm_net_mock_append_fb_target_result12_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneKey, u16 spawnX, u16 spawnY)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    u32 objectStart = 0;
    if (posInfoLen == 0)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 12, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "fb", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "name", sceneKey))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_fb_target_result12_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_fb_target_result12_for_scene(out, outCap, pos,
                                                          vm_net_mock_scene_key_name(),
                                                          vm_net_mock_scene_spawn_x(),
                                                          vm_net_mock_scene_spawn_y());
}

static bool vm_net_mock_append_fb_target_result4_object(u8 *out, u32 outCap, u32 *pos,
                                                        u8 typeValue, const char *infoText)
{
    u32 objectStart = 0;
    if (infoText == NULL)
        infoText = "";
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1b, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "min", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", typeValue))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "fb", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "info", infoText))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u8 vm_net_mock_get_request_fb4_type(const u8 *request, u32 requestLen, u8 fallbackType)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 fb4Type = fallbackType;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 0x1b && object.subtype == 4)
        {
            (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &fb4Type);
            break;
        }
    }

    return fb4Type;
}

static bool vm_net_mock_is_penglai02_repeat_scene_change_request(const u8 *request, u32 requestLen)
{
    vm_net_mock_scene_change_target target;

    if (!vm_net_mock_is_scene_change_request(request, requestLen))
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42) ||
        vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1))
    {
        return false;
    }

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (target.needsSceneDownload)
        return false;
    return vm_net_mock_scene_is_penglai02(target.scene) &&
           target.exitId == 0 &&
           vm_net_mock_is_recent_completed_scene_change_target(&target);
}

static bool vm_net_mock_is_current_scene_completion_request(const u8 *request, u32 requestLen)
{
    vm_net_mock_scene_change_target target;
    const char *currentScene = NULL;

    if (!vm_net_mock_is_scene_change_request(request, requestLen))
        return false;
    if (vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42))
    {
        return false;
    }

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (target.scene[0] == 0 || target.needsSceneDownload)
        return false;

    currentScene = vm_net_mock_current_scene_name();
    return currentScene != NULL &&
           vm_net_mock_scene_uses_current_scene_completion(currentScene) &&
           vm_net_mock_scene_names_equal_loose(currentScene, target.scene) &&
           !vm_net_mock_is_recent_completed_scene_change_target(&target);
}

static bool vm_net_mock_is_current_scene_repeat_scene_change_request(const u8 *request, u32 requestLen)
{
    vm_net_mock_scene_change_target target;
    const char *currentScene = NULL;

    if (!vm_net_mock_is_scene_change_request(request, requestLen))
        return false;
    if (vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42))
    {
        return false;
    }

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (target.scene[0] == 0 || target.needsSceneDownload)
        return false;

    currentScene = vm_net_mock_current_scene_name();
    return currentScene != NULL &&
           vm_net_mock_scene_uses_current_scene_completion(currentScene) &&
           vm_net_mock_scene_names_equal_loose(currentScene, target.scene) &&
           vm_net_mock_is_recent_completed_scene_change_target(&target);
}

static u32 vm_net_mock_build_penglai02_repeat_scene_change_response(const u8 *request, u32 requestLen,
                                                                    u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    u8 fb4Type = 1;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_penglai02_repeat_scene_change_request(request, requestLen))
        return 0;

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);

    /*
     * Runtime evidence:
     * - the first _01 -> _02 portal request is WT 2/3 len=74; the local portal
     *   has created the destination shell, but this direction still needs one
     *   30/2 position-bearing completion to leave loading; the redundant 30/1
     *   scene-enter object is omitted;
     * - after visible render, the client re-emits a narrower WT 2/3 contract
     *   for the same _02 exit=0 target together with 27/11, 27/4, and 7/42.
     *
     * Treat that second packet as a completion ack only. Re-sending 30/1 or a
     * 30/2 with posinfo restarts the same-scene enter loop and eventually drops
     * back to loading.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;
    if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                       target.scene,
                                                       "penglai02-repeat"))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     fb4Type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-penglai02-repeat-ack");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    return pos;
}

static u32 vm_net_mock_build_current_scene_completion_response(const u8 *request, u32 requestLen,
                                                               u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;
    u16 currentX = 0;
    u16 currentY = 0;
    u8 fb4Type = 1;
    bool hasFb4 = false;
    bool closeTeleportDirectEnter = false;

    if (outCap < pos || !vm_net_mock_is_current_scene_completion_request(request, requestLen))
        return 0;

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    closeTeleportDirectEnter =
        g_vm_net_mock_teleport_stone_direct_enter_pending &&
        g_vm_net_mock_last_scene_change_target_valid &&
        vm_net_mock_scene_names_equal_loose(
            target.scene,
            g_vm_net_mock_last_scene_change_target.scene);
    hasFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, g_vm_net_mock_last_scene_change_fb4_type);
    if (!hasFb4 && vm_net_mock_read_current_player_grid(NULL, NULL, &currentX, &currentY, NULL, NULL))
    {
        target.x = currentX;
        target.y = currentY;
    }

    /*
     * Direct startup into saved `_03` already has the local scene shell on
     * screen before this same-target WT 2/3 completion arrives. The client
     * still wants the 27/12 + 27/11 + 27/4 + 7/42 completion family, but a
     * second 30/2 scene+posinfo commit re-enters the same scene again and
     * shows the repeated loading bar on first visible arrival.
     *
     * Keep this path as a completion ack only. The earlier scene shell remains
     * authoritative for the active screen; later task-subset follow-ups can
     * read the completed target from g_vm_net_mock_last_completed_scene_change_target.
     */
    if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos,
                                                         target.scene, target.x, target.y))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                       target.scene,
                                                       "current-scene-completion"))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     fb4Type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    /*
     * A teleport-stone 30/1 is deliberately delivered from a later poll so
     * the item confirmation callback can unwind before EnterSceneByMapName().
     * Its first same-scene 2/3 request lands in this completion path rather
     * than the normal 25/5 resource path.  The common current-scene response
     * intentionally omits 30/2 to avoid a second position-bearing scene enter,
     * but that also leaves the teleport loading widget active forever.
     *
     * JianghuOL.CBE:0x01039770 handles 30/2 and unconditionally calls
     * ResetDownloadState at 0x0103993C.  A result=1 scene ack without posinfo
     * reaches that cleanup without calling the scene-position entry method.
     * Append it only for the saved teleport lifecycle and keep it last, like
     * the normal mmGame resource + 30/2 completion response.
     */
    if (closeTeleportDirectEnter)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2,
                                         &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2,
                                                       target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-current-scene-ack");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    if (closeTeleportDirectEnter)
    {
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
        g_vm_net_mock_teleport_stone_direct_enter_pending = false;
        printf("[info][network] mock_teleport_stone_current_scene_complete scene=%s pos=(%u,%u) objects=%u resp=%u response=27-family+30/2-no-posinfo action=reset-download-state\n",
               target.scene, target.x, target.y, objectCount, pos);
        vm_autotest_note("mock_teleport_stone_current_scene_complete scene=%s pos=(%u,%u) objects=%u response=30/2-no-posinfo evidence=JianghuOL.CBE:0x01039770+0x0103993C\n",
                         target.scene, target.x, target.y, objectCount);
    }
    return pos;
}

static u32 vm_net_mock_build_current_scene_repeat_scene_change_response(const u8 *request, u32 requestLen,
                                                                        u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    u8 fb4Type = 1;
    vm_net_mock_scene_change_target target;

    if (outCap < pos || !vm_net_mock_is_current_scene_repeat_scene_change_request(request, requestLen))
        return 0;

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;
    if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                       target.scene,
                                                       "current-scene-repeat"))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     fb4Type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-current-scene-repeat-ack");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    return pos;
}

static u32 vm_net_mock_build_scene_change_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    bool needSkill = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    bool needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    bool needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    bool needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    bool splitScenePosCommit = true;
    bool currentSceneAlreadyTarget = false;
    u8 fb4Type = 1;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;
    bool recentCompletedTarget = false;
    bool deferTeleportResourceCompletion = false;
    const char *currentScene = NULL;
    char missingResource[64];
    bool resourcesReady = false;
    if (outCap < pos)
        return 0;
    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    resourcesReady = vm_net_mock_prepare_scene_enter_resources(&target,
                                                               missingResource,
                                                               sizeof(missingResource));
    recentCompletedTarget = vm_net_mock_is_recent_completed_scene_change_target(&target);
    if (recentCompletedTarget && target.needsSceneDownload)
    {
        printf("[info][network] mock_scene_change_completed_stale_download scene=%s pos=(%u,%u) exit=%u\n",
               target.scene, target.x, target.y, target.exitId);
        target.needsSceneDownload = false;
    }
    currentScene = vm_net_mock_current_scene_name();
    currentSceneAlreadyTarget = resourcesReady &&
                                !target.needsSceneDownload &&
                                currentScene != NULL &&
                                vm_net_mock_scene_uses_current_scene_completion(currentScene) &&
                                vm_net_mock_scene_names_equal_loose(currentScene, target.scene);
    deferTeleportResourceCompletion =
        !recentCompletedTarget &&
        g_vm_net_mock_teleport_stone_direct_enter_pending &&
        g_vm_net_mock_last_scene_change_target_valid &&
        vm_net_mock_scene_names_equal_loose(
            g_vm_net_mock_last_scene_change_target.scene,
            target.scene);
    if (!recentCompletedTarget &&
        vm_net_mock_should_use_full_scene_bootstrap(currentScene, &target))
    {
        u8 targetNpcCount = 0;

        /*
         * The local edge-portal path has already created and initialized the
         * destination scene screen before this WT 2/3 response is dispatched.
         * A position-bearing 30/2 is the required completion for this request
         * family. Appending 30/1 immediately after it calls the same scene-enter
         * vtable a second time and visibly restarts loading. Send exactly one
         * resolved scene-position completion.
         *
         * That locally-created screen is nevertheless a fresh scene shell.  The
         * normal 30/1 path rearms the one-shot 27/11 NPC catalog, but this 30/2
         * completion deliberately bypasses 30/1.  Rearm it explicitly here and,
         * once target resources are ready, seed the catalog before task rows so
         * task prompt icons are rebuilt against the newly-created NPC nodes.
         */
        vm_net_mock_mark_scene_moveinfo_npc_seed_pending(target.scene);
        targetNpcCount = vm_net_mock_scene_room_npc_seed_count(target.scene);
        if (resourcesReady && targetNpcCount > 0)
        {
            if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                               target.scene,
                                                               "scene-change-full-bootstrap"))
            {
                return 0;
            }
            objectCount += 1;
        }
        printf("[info][network] mock_scene_npc_rearm scene=%s trigger=scene-change-full-bootstrap resources_ready=%u npcnum=%u immediate=%u evidence=JianghuOL.CBE:0x01037998+0x01039770\n",
               target.scene,
               resourcesReady ? 1u : 0u,
               (u32)targetNpcCount,
               (resourcesReady && targetNpcCount > 0) ? 1u : 0u);
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               target.scene,
                                                               true, true, true, true, false, false, false))
            return 0;
        if (!vm_net_mock_append_scene_pos_result_object_for_scene(out, outCap, &pos,
                                                                  target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        /*
         * The full response already includes the single required 30/2 position
         * completion. Leaving the same target pending makes the next 25/5 subset
         * replay it.
         */
        if (!resourcesReady)
        {
            vm_net_mock_defer_scene_enter_completion(&target,
                                                     "scene-change-full-bootstrap",
                                                     missingResource);
            g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
            g_vm_net_mock_last_scene_change_fb4_type = 1;
        }
        else
        {
            vm_net_mock_mark_completed_scene_change_target(&target);
            g_vm_net_mock_last_scene_change_target_valid = false;
            g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
            g_vm_net_mock_last_scene_change_fb4_type = 1;
            vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-transfer-target");
        }
        return pos;
    }
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);
    if (splitScenePosCommit)
    {
        /*
         * A deferred teleport 30/1 can make the destination the service's
         * current scene before the client has installed its SCE-declared MAP,
         * Actor and GIF dependencies.  A 30/2 here calls ResetDownloadState
         * and snapshots the target as completed while WT18/7 downloads are
         * still in flight.  Keep the request's other result objects, but leave
         * the scene lifecycle pending until the post-download WT6/1 callback.
         */
        if (!deferTeleportResourceCompletion)
        {
            if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
                return 0;
            if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
                return 0;
            vm_net_mock_finish_wt_object(out, objectStart, pos);
            objectCount += 1;
        }
    }
    else
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, true, true, 2, target.scene, target.x, target.y))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    if (needFb11)
    {
        if (!splitScenePosCommit)
        {
            if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos, target.scene, target.x, target.y))
                return 0;
            objectCount += 1;
        }
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    if (needFb4)
    {
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, vm_net_mock_fb_target_info_text()))
            return 0;
        objectCount += 1;
    }
    if (needSkill)
    {
        u8 added = 0;
        if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos, &added))
            return 0;
        objectCount = (u8)(objectCount + added);
    }
    else if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    if (!splitScenePosCommit)
    {
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos, target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    if (deferTeleportResourceCompletion)
    {
        g_vm_net_mock_last_scene_change_target = target;
        g_vm_net_mock_last_scene_change_target_valid = true;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
        printf("[info][network] mock_scene_change_teleport_resource_pending scene=%s pos=(%u,%u) objects=%u resp=%u completion=defer-30/2-until-WT6/1 evidence=WT18/7-client-download\n",
               target.scene, target.x, target.y, objectCount, pos);
        vm_autotest_note("mock_scene_change_teleport_resource_pending scene=%s pos=(%u,%u) objects=%u response=no-30/2 completion=WT6/1-after-download evidence=JianghuOL.CBE:0x01037000+0x01039770\n",
                         target.scene, target.x, target.y, objectCount);
        return pos;
    }
    if (vm_net_mock_scene_change_target_is_unresolved_existing_scene(&target))
    {
        /*
         * This is a repeat/completion WT 2/3 for a scene the server already
         * knows, but the request no longer carries enough source-portal context
         * to resolve an entry. Sending 30/1 or keeping a pending target here
         * re-enters the map at (0,0). Acknowledge the packet only; the first
         * valid scene-enter response has already driven EnterSceneByMapName().
         */
        vm_net_mock_clear_unresolved_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
        printf("[warn][network] mock_scene_change_unresolved_entry_ack scene=%s exit=%u resp=%u action=ack-only-no-pending evidence=JianghuOL.CBE:0x1039BB4 case2-no-posinfo\n",
               target.scene, target.exitId, pos);
        vm_autotest_note("mock_scene_change_unresolved_entry_ack scene=%s exit=%u response=scene-ack-only-no-pending evidence=JianghuOL.CBE:0x1039BB4\n",
                         target.scene, target.exitId);
        return pos;
    }
    if (target.needsSceneDownload)
    {
        if (g_vm_net_mock_last_scene_change_target_valid &&
            vm_net_mock_scene_names_equal_loose(g_vm_net_mock_last_scene_change_target.scene, target.scene))
        {
            g_vm_net_mock_last_scene_change_target.needsSceneDownload = true;
        }
        else
        {
            vm_net_mock_remember_scene_change_target(&target);
        }
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
        vm_autotest_note("mock_scene_download_ack scene=%s exit=%u response=scene-ack-without-posinfo evidence=JianghuOL:0x100369C/0x1036C66\n",
                         target.scene, target.exitId);
        return pos;
    }
    if (!resourcesReady)
    {
        vm_net_mock_defer_scene_enter_completion(&target,
                                                 "scene-change-ack",
                                                 missingResource);
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
        return pos;
    }
    if (currentSceneAlreadyTarget)
    {
        /*
         * Some edge portals locally create the destination scene screen before
         * the skill/books-flavoured WT 2/3 request arrives. This request cannot
         * hit the lighter current-scene completion detector because it also asks
         * for 12/1 and 7/42, but keeping it pending makes the following 25/5
         * send another 30/2 scene+posinfo object and re-enter the map.
         */
        vm_net_mock_mark_completed_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_target_valid = false;
    }
    else if (!recentCompletedTarget)
    {
        vm_net_mock_remember_scene_change_target(&target);
    }
    else
    {
        printf("[info][network] mock_scene_change_completed_repeat_ack scene=%s pos=(%u,%u) exit=%u completed_exit=%u age=%u\n",
               target.scene, target.x, target.y, target.exitId,
               g_vm_net_mock_last_completed_scene_change_target.exitId,
               g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick);
    }
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    if (!recentCompletedTarget || currentSceneAlreadyTarget)
        vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-target");
    g_vm_net_mock_last_scene_change_fb4_type = fb4Type;
    return pos;
}

static u32 vm_net_mock_build_type27_followup_combo_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    bool needSceneDefault = vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
    bool needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    bool needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    bool needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    u8 fb4Type = 1;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 objectStart = 0;
    const char *scene = vm_net_mock_current_scene_name();

    if (outCap < pos || !needFb4)
        return 0;

    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);

    if (needSceneDefault)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x19, 12, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    if (needFb11)
    {
        if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                           scene,
                                                           "type27-followup"))
            return 0;
        objectCount += 1;
    }

    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, ""))
        return 0;
    objectCount += 1;

    if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_net_mock_append_battle_drop_refresh7_if_needed(u8 *out, u32 outCap,
                                                              u32 *pos, u8 *objectCount,
                                                              const char *phase,
                                                              bool allowActiveSession);

static u32 vm_net_mock_build_scene_default_event_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    if (outCap < pos)
        return 0;
    if (g_vm_net_mock_last_scene_change_target_valid)
    {
        vm_net_mock_scene_change_target target = g_vm_net_mock_last_scene_change_target;
        char missingResource[64];
        bool resourcesReady = vm_net_mock_prepare_scene_enter_resources(&target,
                                                                        missingResource,
                                                                        sizeof(missingResource));
        g_vm_net_mock_last_scene_change_target = target;
        if (resourcesReady && vm_net_mock_scene_is_penglai_transfer_scene(target.scene))
        {
            if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                                   target.scene,
                                                                   true, true, true, true, false, false, false))
                return 0;
            if (!vm_net_mock_append_scene_pos_result_object_for_scene(out, outCap, &pos,
                                                                      target.scene, target.x, target.y))
                return 0;
            objectCount += 1;
            vm_net_mock_finish_wt_packet(out, pos, objectCount);
            vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-transfer-completion");
            vm_net_mock_mark_completed_scene_change_target(&target);
            g_vm_net_mock_last_scene_change_target_valid = false;
            g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
            return pos;
        }
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               target.scene,
                                                               true, true, true, true, false, false, false))
            return 0;
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                            target.scene, target.x, target.y))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        if (!resourcesReady)
        {
            vm_net_mock_defer_scene_enter_completion(&target,
                                                     "scene-default-event",
                                                     missingResource);
            return pos;
        }
        vm_net_mock_mark_pending_scene_pos_save(target.scene, target.x, target.y, "scene-change-completion");
        vm_net_mock_mark_completed_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        return pos;
    }
    if (g_mockBattleOperateSessionFinished != 0)
    {
        g_mockBattleOperateSessionFinished = 0;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x19, 5, &objectStart))
        return 0;
    /*
     * scene_runtime_tick() sends the empty 0x19/5 scene-default event request.
     * Repository evidence already showed that a same-subtype `1/25/5 {result=4}`
     * shell is parser-safe on the scene resource-followup path, while the older
     * generic `1/25/12 {result=4}` clear path is consumed globally by
     * net_handle_info_banner_state() and now also correlates with battle exit.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;
    if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                           &objectCount,
                                                           "scene-default-event",
                                                           false))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_net_mock_append_info_banner_result5_object(u8 *out, u32 outCap, u32 *pos);

static bool vm_net_mock_is_scene_change_post_enter_followup_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 30)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x19 || object.subtype != 5 || object.payloadLen != 0)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 3)
        return false;
    if (!vm_net_mock_request_contains(object.payload, object.payloadLen, "maptype") ||
        !vm_net_mock_request_contains(object.payload, object.payloadLen, "mapID") ||
        !vm_net_mock_request_contains(object.payload, object.payloadLen, "exitID"))
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x1b || object.subtype != 11)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 42)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_actor_other_scene_change_post_enter_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 40)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 10)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 3)
        return false;
    if (!vm_net_mock_request_contains(object.payload, object.payloadLen, "maptype") ||
        !vm_net_mock_request_contains(object.payload, object.payloadLen, "mapID") ||
        !vm_net_mock_request_contains(object.payload, object.payloadLen, "exitID"))
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x1b || object.subtype != 11)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 42)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_scene_change_post_enter_followup_response(const u8 *request, u32 requestLen,
                                                                       u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;
    char missingResource[64];
    bool resourcesReady = false;
    bool recentCompletedTarget = false;
    u32 nearbyRoleCount = 0;
    u32 nearbyOtherInfoLen = 0;
    u8 nearbyMoveinfoCount = 0;

    if (outCap < pos ||
        (!vm_net_mock_is_scene_change_post_enter_followup_request(request, requestLen) &&
         !vm_net_mock_is_actor_other_scene_change_post_enter_request(request, requestLen)))
    {
        return 0;
    }

    vm_net_mock_get_scene_change_target(request, requestLen, &target);
    if (target.scene[0] == 0)
        return 0;
    resourcesReady = vm_net_mock_prepare_scene_enter_resources(&target,
                                                               missingResource,
                                                               sizeof(missingResource));
    recentCompletedTarget = vm_net_mock_is_recent_completed_scene_change_target(&target);

    if (!resourcesReady)
    {
        u32 objectStart = 0;

        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        vm_net_mock_defer_scene_enter_completion(&target,
                                                 "scene-change-post-enter-followup",
                                                 missingResource);
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        vm_autotest_note("mock_scene_download_followup_ack scene=%s exit=%u response=scene-ack-without-posinfo\n",
                         target.scene, target.exitId);
        return pos;
    }

    if (recentCompletedTarget)
    {
        u32 objectStart = 0;

        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                           target.scene,
                                                           "post-enter-repeat"))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        printf("[info][network] mock_scene_change_post_enter_repeat_ack scene=%s pos=(%u,%u) objects=%u resp=%u age=%u\n",
               target.scene,
               target.x,
               target.y,
               objectCount,
               pos,
               g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick);
        return pos;
    }

    if (!g_vm_net_mock_last_scene_change_target_valid ||
        !vm_net_mock_scene_change_targets_equal(&target, &g_vm_net_mock_last_scene_change_target))
    {
        vm_net_mock_remember_scene_change_target(&target);
    }

    /*
     * Evidence:
     * - runtime: once EnterSceneByMapName() gets a real same-screen lifecycle,
     *   Penglai _02 emits `25/5 + 2/3 + 27/11 + 7/42` instead of stalling;
     * - IDA/runtime from earlier scene probes: 27/11 is consumed by
     *   scene_parse_npcinfo_and_spawn_npcs(), and deferred scene completion
     *   already wants 27/12 + 27/11 + 27/4. A later 30/2 scene-pos result
     *   repeats the mode-7 scene-enter lifecycle after the local portal shell.
     *
     * Populate 27/11 from the current SCE only after the scene shell exists;
     * the one-shot guard prevents a later follow-up from recreating the nodes.
     */
    if (!vm_net_mock_append_info_banner_result5_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos,
                                                         target.scene, target.x, target.y))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                       target.scene,
                                                       "post-enter-completion"))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                     g_vm_net_mock_last_scene_change_fb4_type,
                                                     vm_net_mock_fb_target_info_text()))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    vm_net_mock_mark_completed_scene_change_target(&target);
    {
        u8 addedNearbyObjects = vm_net_mock_append_scene_nearby_role_objects(out,
                                                                             outCap,
                                                                             &pos,
                                                                             target.scene,
                                                                             &nearbyRoleCount,
                                                                             &nearbyOtherInfoLen,
                                                                             &nearbyMoveinfoCount);
        if (addedNearbyObjects > 0)
        {
            objectCount = (u8)(objectCount + addedNearbyObjects);
            printf("[info][network] mock_scene_change_post_enter_nearby scene=%s nearby_roles=%u otherinfo_len=%u moveinfo=%u resp=%u\n",
                   target.scene,
                   nearbyRoleCount,
                   nearbyOtherInfoLen,
                   nearbyMoveinfoCount,
                   pos);
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "scene-change-post-enter-complete");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    printf("[info][network] mock_scene_change_post_enter_followup scene=%s pos=(%u,%u) objects=%u resp=%u\n",
           target.scene, target.x, target.y, objectCount, pos);
    return pos;
}

static bool vm_net_mock_is_actor_other_scene_default_combo_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 typeValue = 0;

    if (request == NULL || requestLen != 24)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 10)
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue) || typeValue != 1)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x19 || object.subtype != 5 || object.payloadLen != 0)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_short_scene_default_event_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x19 || object.subtype != 5 || object.payloadLen != 0)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_current_scene_reload_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    const char *scene = NULL;
    u16 x = 0;
    u16 y = 0;
    bool isReloadTrigger = false;

    if (outCap < pos)
        return 0;
    isReloadTrigger = vm_net_mock_is_short_scene_default_event_request(request, requestLen) ||
                      vm_net_mock_is_actor_other_scene_default_combo_request(request, requestLen);
    if (!isReloadTrigger || !vm_net_mock_scene_runtime_pending_without_target())
        return 0;

    scene = vm_net_mock_current_scene_name();
    if (scene == NULL ||
        scene[0] == 0 ||
        vm_net_mock_is_recent_current_scene_reload(scene, 90) ||
        vm_net_mock_is_recent_completed_scene_name(scene, 90))
    {
        return 0;
    }
    x = vm_net_mock_scene_spawn_x();
    y = vm_net_mock_scene_spawn_y();
    (void)vm_net_mock_read_current_player_grid(NULL, NULL, &x, &y, NULL, NULL);
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);

    /*
     * mmGame menu "脱离卡死" re-enters the current scene without a portal
     * target. In that state the client emits the default scene event family,
     * but the old empty 25/5 ack leaves the new scene screen on loading.
     * Reuse the first-scene resource family and finish with 30/1 so
     * scene_handle_enter_with_scene_pos() can load the current map normally.
     */
    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           scene,
                                                           true, true, true, true,
                                                           false, false, false))
        return 0;
    if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos, scene, x, y))
        return 0;
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_mark_current_scene_reload(scene);
    vm_net_mock_save_player_pos_state(scene, x, y, "scene-current-reload");
    printf("[info][network] mock_scene_current_reload scene=%s pos=(%u,%u) objects=%u resp=%u\n",
           scene, x, y, objectCount, pos);
    vm_autotest_note("mock_scene_current_reload scene=%s pos=(%u,%u) trigger=%s objects=%u evidence=mmGame:0x24E6 JianghuOL:0x10396D6\n",
                     scene,
                     x,
                     y,
                     vm_net_mock_is_actor_other_scene_default_combo_request(request, requestLen) ? "2/10+25/5" : "25/5",
                     objectCount);
    return pos;
}

static bool vm_net_mock_is_mmgame_scene_transfer_followup_request(const u8 *request, u32 requestLen)
{
    if (!g_vm_net_mock_last_scene_change_target_valid)
        return false;
    if (g_vm_net_mock_teleport_stone_direct_enter_pending)
        return false;
    return vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
}

static u32 vm_net_mock_build_mmgame_scene_transfer_followup_response(const u8 *request, u32 requestLen,
                                                                     u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    vm_net_mock_scene_change_target target;
    const char *currentScene = NULL;
    char missingResource[64];
    bool resourcesReady = false;
    u8 targetNpcCount = 0;

    if (outCap < pos || !vm_net_mock_is_mmgame_scene_transfer_followup_request(request, requestLen))
        return 0;
    target = g_vm_net_mock_last_scene_change_target;
    printf("[info][network] mmgame-transfer-followup target scene=%s needsDownload=%u x=%u y=%u exit=%u valid=%u\n",
           target.scene, target.needsSceneDownload ? 1 : 0, target.x, target.y, target.exitId,
           g_vm_net_mock_last_scene_change_target_valid ? 1 : 0);
    if (vm_net_mock_scene_change_target_is_unresolved_existing_scene(&target))
    {
        u32 ackLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (ackLen == 0)
            return 0;
        vm_net_mock_clear_unresolved_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        printf("[warn][network] mock_mmgame_scene_transfer_unresolved_entry_ack scene=%s exit=%u resp=%u action=drop-pending-no-30-1 evidence=JianghuOL.CBE:0x10396D6\n",
               target.scene, target.exitId, ackLen);
        vm_autotest_note("mock_mmgame_scene_transfer_unresolved_entry_ack scene=%s exit=%u response=25/5-ack-no-30/1\n",
                         target.scene, target.exitId);
        return ackLen;
    }
    resourcesReady = vm_net_mock_prepare_scene_enter_resources(&target,
                                                               missingResource,
                                                               sizeof(missingResource));
    /*
     * Re-evaluate resource readiness against the actual on-disk scene file
     * rather than trusting the possibly-stale needsSceneDownload flag carried
     * from the remembered target.  A scene that exists locally is ready
     * regardless of whether the original request thought it needed downloading.
     */
    if (!resourcesReady && vm_net_mock_scene_resource_exists(target.scene))
    {
        target.needsSceneDownload = false;
        if (target.x == 0 && target.y == 0)
        {
            u16 cx = 0, cy = 0;
            if (vm_net_mock_get_scene_reasonable_spawn_from_sce(target.scene,
                                                                &cx,
                                                                &cy,
                                                                NULL))
            {
                target.x = cx;
                target.y = cy;
                target.hasSceEntry = true;
            }
        }
        resourcesReady = vm_net_mock_prepare_scene_enter_resources(&target,
                                                                   missingResource,
                                                                   sizeof(missingResource));
    }
    g_vm_net_mock_last_scene_change_target = target;
    currentScene = vm_net_mock_current_scene_name();
    if (resourcesReady &&
        !g_vm_net_mock_teleport_stone_map_enter_pending &&
        currentScene != NULL &&
        vm_net_mock_scene_names_equal_loose(currentScene, target.scene) &&
        vm_net_mock_is_recent_completed_scene_name(currentScene, 90))
    {
        u32 ackLen = 0;

        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
        g_vm_net_mock_teleport_stone_map_enter_pending = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        ackLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (ackLen == 0)
            return 0;
        printf("[info][network] mock_mmgame_scene_transfer_repeat_ack scene=%s pos=(%u,%u) resp=%u age=%u\n",
               target.scene,
               target.x,
               target.y,
               ackLen,
               g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick);
        vm_autotest_note("mock_mmgame_scene_transfer_repeat_ack scene=%s pos=(%u,%u) response=25/5-ack-only age=%u\n",
                         target.scene,
                         target.x,
                         target.y,
                         g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick);
        return ackLen;
    }

    if (resourcesReady &&
        g_vm_net_mock_teleport_stone_map_enter_pending &&
        currentScene != NULL &&
        vm_net_mock_scene_names_equal_loose(currentScene, target.scene) &&
        vm_net_mock_is_recent_completed_scene_name(currentScene, 90))
    {
        /*
         * A fresh 16/4 map-stone request can legitimately start another load
         * cycle for the same scene and landing point before the completed-scene
         * reuse window expires.  That is different from the post-download 2/3
         * repeat which has no map-enter pending provenance.  Ack-only here
         * leaves the newly opened loading screen without its resource-complete
         * family, so let this request continue through the normal resource +
         * 30/2(no-posinfo) completion below.
         */
        printf("[info][network] mock_mmgame_scene_transfer_fresh_same_target scene=%s pos=(%u,%u) age=%u action=deliver-resource-completion\n",
               target.scene,
               target.x,
               target.y,
               g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick);
        vm_autotest_note("mock_mmgame_scene_transfer_fresh_same_target scene=%s pos=(%u,%u) action=deliver-resource-completion evidence=16/4-map-enter-pending\n",
                         target.scene,
                         target.x,
                          target.y);
    }

    /*
     * This completion also follows a client-created destination scene shell and
     * intentionally ends with 30/2(no-posinfo), not 30/1.  Treat it as the same
     * NPC lifecycle boundary as a normal scene enter: every fresh shell needs a
     * fresh 27/11 catalog even when the player is returning to a scene that was
     * seeded earlier in this service session.
     */
    vm_net_mock_mark_scene_moveinfo_npc_seed_pending(target.scene);
    targetNpcCount = vm_net_mock_scene_room_npc_seed_count(target.scene);
    if (resourcesReady && targetNpcCount > 0)
    {
        if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                           target.scene,
                                                           "mmgame-scene-transfer-followup"))
        {
            return 0;
        }
        objectCount += 1;
    }
    printf("[info][network] mock_scene_npc_rearm scene=%s trigger=mmgame-scene-transfer-followup resources_ready=%u npcnum=%u immediate=%u evidence=JianghuOL.CBE:0x01037998+0x01039770\n",
           target.scene,
           resourcesReady ? 1u : 0u,
           (u32)targetNpcCount,
           (resourcesReady && targetNpcCount > 0) ? 1u : 0u);

    if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                           target.scene,
                                                           true, true, true, true,
                                                           false, false, false))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    if (!resourcesReady)
    {
        vm_net_mock_defer_scene_enter_completion(&target,
                                                 "mmgame-scene-transfer-followup",
                                                 missingResource);
        printf("[info][network] mock_mmgame_scene_transfer_followup scene=%s pos=(%u,%u) objects=%u resp=%u complete=0\n",
               target.scene, target.x, target.y, objectCount, pos);
        return pos;
    }

    vm_net_mock_mark_completed_scene_change_target(&target);
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y, "mmgame-scene-transfer-followup");
    g_vm_net_mock_last_scene_change_target_valid = false;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    printf("[info][network] mock_mmgame_scene_transfer_followup scene=%s pos=(%u,%u) objects=%u resp=%u\n",
           target.scene, target.x, target.y, objectCount, pos);
    vm_autotest_note("mock_mmgame_scene_transfer_followup scene=%s pos=(%u,%u) objects=%u response=resources+30/2-ack-no-posinfo evidence=JianghuOL:0x1039770(result=2) runtime=post-local-portal-shell\n",
                     target.scene, target.x, target.y, objectCount);
    return pos;
}

static bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen,
                                                   u8 kind, u8 subtype);

static u32 vm_net_mock_build_teleport_stone_direct_enter_default_ack_response(const u8 *request,
                                                                              u32 requestLen,
                                                                              u8 *out,
                                                                              u32 outCap)
{
    u32 len = 0;
    vm_net_mock_scene_change_target target;

    if (!g_vm_net_mock_teleport_stone_direct_enter_pending ||
        !g_vm_net_mock_last_scene_change_target_valid ||
        !vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x19, 5))
    {
        return 0;
    }

    target = g_vm_net_mock_last_scene_change_target;
    len = vm_net_mock_build_scene_default_event_response(out, outCap);
    if (len == 0)
        return 0;

    printf("[info][network] mock_teleport_stone_direct_enter_ack scene=%s pos=(%u,%u) keep_pending=1 resp=%u\n",
           target.scene, target.x, target.y, len);
    vm_autotest_note("mock_teleport_stone_direct_enter_ack scene=%s pos=(%u,%u) keep_pending=1 response=25/5 evidence=runtime:post-30/1-short-default\n",
                     target.scene, target.x, target.y);
    return len;
}

static bool vm_net_mock_is_compact_scene_resource_followup_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen != 19 ||
        request[0] != 'W' || request[1] != 'T')
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 0x0c || object.subtype != 1 ||
        object.payloadLen != 0)
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 7 || object.subtype != 42 ||
        object.payloadLen != 0)
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 0x19 || object.subtype != 5 ||
        object.payloadLen != 0)
    {
        return false;
    }
    return !vm_net_mock_next_request_object(request, requestLen, &offset, &object);
}

static bool vm_net_mock_is_scene_resource_followup_request(const u8 *request, u32 requestLen)
{
    u8 typeValue = 0;
    bool needSkillBooks = false;
    bool needTaskFamily = false;
    if (request == NULL || (requestLen != 49 && requestLen != 39))
        return false;
    needSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
                     vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    if (needSkillBooks &&
        (!vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) ||
         !vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42)))
        return false;
    needTaskFamily = vm_net_mock_request_contains_object(request, requestLen, 1, 6, 1) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 6, 13) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 6, 14) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) &&
                     vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5);
    if (!needTaskFamily)
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue))
        return false;
    return typeValue == 101;
}

static bool vm_net_mock_is_current_scene_task_subset_followup_request(const u8 *request, u32 requestLen)
{
    const char *currentScene = NULL;

    if (requestLen != 39 || !vm_net_mock_is_scene_resource_followup_request(request, requestLen))
        return false;
    currentScene = vm_net_mock_current_scene_name();
    if (currentScene == NULL || !vm_net_mock_scene_uses_current_scene_completion(currentScene))
        return false;
    if (g_vm_net_mock_last_completed_scene_change_target_valid &&
        vm_net_mock_scene_names_equal_loose(currentScene, g_vm_net_mock_last_completed_scene_change_target.scene))
    {
        return true;
    }
    return g_vm_net_mock_last_scene_change_target_valid &&
           vm_net_mock_scene_names_equal_loose(currentScene, g_vm_net_mock_last_scene_change_target.scene);
}

static bool vm_net_mock_is_scene_task_subset_followup_request(const u8 *request, u32 requestLen)
{
    u8 typeValue = 0;
    if (request == NULL || requestLen < 24)
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 6, 1) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 6, 13) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 6, 14) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 0x19, 5))
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "Type", &typeValue))
        return false;
    return typeValue == 101;
}

static bool vm_net_mock_append_taskinfo_empty1_object(u8 *out, u32 outCap, u32 *pos,
                                                       const char *sceneOverride)
{
    vm_net_mock_role_state *activeRole = vm_net_mock_active_role();
    vm_net_mock_task_state_list_row states[10];
    u32 stateCount = 0;
    u8 taskInfo[2048];
    u32 taskInfoLen = 0;
    u8 taskNum = 0;
    u32 objectStart = 0;

    memset(states, 0, sizeof(states));
    memset(taskInfo, 0, sizeof(taskInfo));
    if (activeRole != NULL)
    {
        (void)vm_net_mock_task_state_list_load(activeRole->roleId, true,
                                               states, 10, &stateCount);
    }
    for (u32 i = 0; i < stateCount && taskNum < 10; ++i)
    {
        const vm_net_mock_task_definition *task =
            vm_net_mock_task_catalog_find_by_id(states[i].taskId);
        bool appended = false;

        if (states[i].taskId == VM_NET_MOCK_TEST_TASK_ID)
        {
            appended = vm_net_mock_append_test_task_record(taskInfo,
                                                           sizeof(taskInfo),
                                                           &taskInfoLen,
                                                           states[i].state,
                                                           states[i].progress1,
                                                           states[i].progress2);
        }
        else if (task != NULL)
        {
            char promptReceiver[32];
            const char *scene = vm_net_mock_scene_name_is_safe(sceneOverride)
                                    ? sceneOverride
                                    : vm_net_mock_current_scene_name();

            memset(promptReceiver, 0, sizeof(promptReceiver));
            appended = vm_net_mock_append_catalog_task_record(taskInfo,
                                                              sizeof(taskInfo),
                                                              &taskInfoLen,
                                                              task,
                                                              vm_net_mock_task_prompt_receiver_for_scene(
                                                                  task, scene,
                                                                  promptReceiver,
                                                                  sizeof(promptReceiver)),
                                                              states[i].state,
                                                              states[i].progress1,
                                                              states[i].progress2);
        }
        if (!appended && (states[i].taskId == VM_NET_MOCK_TEST_TASK_ID || task != NULL))
        {
            return false;
        }
        if (appended)
            taskNum += 1;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "tasknum", taskNum) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "taskinfo",
                                    taskInfo, (u16)taskInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_task_list role=%u tasknum=%u persisted_active=%u taskinfo_len=%u source=task.dsh+xse evidence=JianghuOL.CBE:0x0104726C(case1)+0x01046D24\n",
           activeRole ? activeRole->roleId : 0, taskNum, stateCount, taskInfoLen);
    return true;
}

static bool vm_net_mock_append_tasktypes_empty13_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u32 blobPos = 0;
    u8 blob[64];

    memset(blob, 0, sizeof(blob));
    for (u32 i = 0; i < 6; ++i)
    {
        if (!vm_net_mock_seq_put_u8(blob, sizeof(blob), &blobPos, 0))
            return false;
        if (!vm_net_mock_seq_put_string(blob, sizeof(blob), &blobPos, ""))
            return false;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 13, &objectStart))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "tasktypes", blob, (u16)blobPos))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_taskaction14_object(u8 *out, u32 outCap, u32 *pos,
                                                    const char *sceneOverride)
{
    vm_net_mock_role_state *activeRole = vm_net_mock_active_role();
    const char *scene = vm_net_mock_scene_name_is_safe(sceneOverride)
                            ? sceneOverride
                            : vm_net_mock_current_scene_name();
    vm_net_mock_scene_npcinfo_seed seeds[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    vm_net_mock_task_state_list_row states[VM_NET_MOCK_TASK_CATALOG_MAX];
    u32 stateCount = 0;
    u32 seedCount = 0;
    u32 totalNpcCount = 0;
    u32 dynamicNpcCount = 0;
    u32 candidateTaskIds[10];
    u8 taskInfo[2048];
    u32 taskInfoLen = 0;
    u8 taskNum = 0;
    u32 objectStart = 0;

    memset(seeds, 0, sizeof(seeds));
    memset(states, 0, sizeof(states));
    memset(candidateTaskIds, 0, sizeof(candidateTaskIds));
    memset(taskInfo, 0, sizeof(taskInfo));
    if (activeRole != NULL)
    {
        (void)vm_net_mock_task_state_list_load(activeRole->roleId, false,
                                               states,
                                               VM_NET_MOCK_TASK_CATALOG_MAX,
                                               &stateCount);
    }
    if (activeRole != NULL && vm_net_mock_scene_is_penglai02(scene) &&
        vm_net_mock_task_state_list_find(states, stateCount,
                                         VM_NET_MOCK_TEST_TASK_ID) == NULL)
    {
        if (!vm_net_mock_append_test_task_candidate_record(taskInfo, sizeof(taskInfo),
                                                           &taskInfoLen))
        {
            return false;
        }
        taskNum = 1;
        candidateTaskIds[0] = VM_NET_MOCK_TEST_TASK_ID;
    }

    seedCount = vm_net_mock_select_scene_npcinfo_seeds(scene, seeds,
                                                      VM_NET_MOCK_SCENE_NPCINFO_MAX,
                                                      &totalNpcCount,
                                                      &dynamicNpcCount);
    for (u32 seedIndex = 0; activeRole != NULL && seedIndex < seedCount && taskNum < 10;
         ++seedIndex)
    {
        vm_net_mock_xse_summary summary;

        if (seeds[seedIndex].taskId != 0)
        {
            const vm_net_mock_task_definition *boundTask =
                vm_net_mock_task_catalog_find_by_id(seeds[seedIndex].taskId);
            bool duplicate = false;

            for (u32 existing = 0; existing < taskNum; ++existing)
            {
                if (candidateTaskIds[existing] == seeds[seedIndex].taskId)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate &&
                vm_net_mock_task_definition_available(boundTask, activeRole,
                                                       states, stateCount))
            {
                if (!vm_net_mock_append_catalog_task_candidate_record(
                        taskInfo, sizeof(taskInfo), &taskInfoLen,
                        boundTask, seeds[seedIndex].displayName))
                {
                    return false;
                }
                candidateTaskIds[taskNum++] = boundTask->taskId;
                if (taskNum >= 10)
                    break;
            }
        }

        memset(&summary, 0, sizeof(summary));
        if (seeds[seedIndex].scriptName[0] == 0 ||
            !vm_net_mock_load_xse_summary(seeds[seedIndex].scriptName, &summary))
        {
            continue;
        }
        for (u32 refIndex = 0; refIndex < summary.taskRefCount && taskNum < 10;
             ++refIndex)
        {
            const vm_net_mock_xse_task_ref *ref = &summary.taskRefs[refIndex];
            const vm_net_mock_task_definition *task = NULL;
            bool duplicate = false;

            if (!ref->offer)
                continue;
            for (u32 existing = 0; existing < taskNum; ++existing)
            {
                if (candidateTaskIds[existing] == ref->taskId)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            task = vm_net_mock_task_catalog_find_by_id(ref->taskId);
            if (!vm_net_mock_task_definition_available(task, activeRole,
                                                       states, stateCount))
            {
                continue;
            }
            if (!vm_net_mock_append_catalog_task_candidate_record(
                    taskInfo, sizeof(taskInfo), &taskInfoLen,
                    task, seeds[seedIndex].displayName))
            {
                return false;
            }
            candidateTaskIds[taskNum++] = task->taskId;
        }
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 14, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "action", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "tasknum", taskNum))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "taskinfo",
                                    taskInfo, (u16)taskInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_task_candidates role=%u scene=%s tasknum=%u taskinfo_len=%u persisted=%u npc_rows=%u npc_total=%u source=task.dsh+xse evidence=JianghuOL.CBE:0x01046E00+0x01017C6C(prompt2)\n",
           activeRole ? activeRole->roleId : 0,
           scene ? scene : "-",
           taskNum,
           taskInfoLen,
           stateCount,
           seedCount,
           totalNpcCount);
    return true;
}

static const char *vm_net_mock_actor_resource_name(u8 actorJob, u8 actorSex);

static bool vm_net_mock_append_actor_other_empty10_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "othernum", 0))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "otherinfo", NULL, 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static void vm_net_mock_copy_scene_node_label(const char *src, char *dst, u32 dstCap, u32 maxBytes)
{
    u32 copyLen = 0;

    if (dst == NULL || dstCap == 0)
        return;
    memset(dst, 0, dstCap);
    if (src == NULL || src[0] == 0 || maxBytes == 0)
        return;
    copyLen = vm_net_mock_shop_safe_name_len((const u8 *)src, (u32)strlen(src), maxBytes);
    if (copyLen >= dstCap)
        copyLen = dstCap - 1;
    memcpy(dst, src, copyLen);
    dst[copyLen] = 0;
}

static bool vm_net_mock_seq_put_actor_other_scene_role(u8 *out, u32 outCap, u32 *pos,
                                                       const vm_net_mock_scene_role_seed *seed)
{
    const char *labelText = NULL;
    const char *shortLabel = NULL;
    const char *longLabel = NULL;
    char labelTextBuf[32];
    char shortLabelBuf[16];
    char longLabelBuf[32];

    if (seed == NULL)
        return false;
    labelText = seed->roleName ? seed->roleName : "Player";
    shortLabel = seed->titleText ? seed->titleText : "";
    longLabel = seed->titleBadge ? seed->titleBadge : "";
    vm_net_mock_copy_scene_node_label(labelText, labelTextBuf, sizeof(labelTextBuf), 31);
    vm_net_mock_copy_scene_node_label(shortLabel, shortLabelBuf, sizeof(shortLabelBuf), 9);
    vm_net_mock_copy_scene_node_label(longLabel, longLabelBuf, sizeof(longLabelBuf), 19);

    return vm_net_mock_seq_put_u32(out, outCap, pos, seed->actorId) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, seed->job > 0 ? (u8)(seed->job - 1) : 0) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, (u8)(seed->sex + 1)) &&
           vm_net_mock_seq_put_string(out, outCap, pos, labelTextBuf) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, (u8)(seed->x / 16u)) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, (u8)(seed->y / 16u)) &&
           vm_net_mock_seq_put_string(out, outCap, pos, shortLabelBuf) &&
           vm_net_mock_seq_put_string(out, outCap, pos, longLabelBuf) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->x) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->y);
}

static bool vm_net_mock_append_actor_other_scene_roles10_object(u8 *out, u32 outCap, u32 *pos,
                                                               const char *scene,
                                                               u32 *roleCountOut,
                                                               u32 *otherInfoLenOut)
{
    vm_net_mock_scene_role_seed seeds[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX];
    u8 otherInfo[512];
    u32 objectStart = 0;
    u32 otherInfoLen = 0;
    u8 roleCount = 0;

    if (roleCountOut)
        *roleCountOut = 0;
    if (otherInfoLenOut)
        *otherInfoLenOut = 0;

    roleCount = vm_net_mock_build_scene_role_seeds(scene, seeds, VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX);
    if (roleCount == 0)
        return false;
    memset(otherInfo, 0, sizeof(otherInfo));
    for (u8 i = 0; i < roleCount; ++i)
    {
        if (!vm_net_mock_seq_put_actor_other_scene_role(otherInfo, sizeof(otherInfo),
                                                        &otherInfoLen, &seeds[i]))
        {
            return false;
        }
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 10, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "othernum", roleCount))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "otherinfo", otherInfo, (u16)otherInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (roleCountOut)
        *roleCountOut = roleCount;
    if (otherInfoLenOut)
        *otherInfoLenOut = otherInfoLen;
    return true;
}

typedef struct
{
    u32 actorId;
    u16 x;
    u16 y;
    u8 kind;
    const char *actorResource;
    u8 sceneType;
    const char *displayName;
    u8 scriptType;
    const char *scriptName;
} vm_net_mock_scene_npc_seed;

static bool vm_net_mock_scene_is_penglai02(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32") == 0 ||
            strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_penglai01(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31") == 0 ||
            strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0 ||
            strcmp(scene, "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31") == 0 ||
            strcmp(scene, "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31\x2e\x73\x63\x65") == 0 ||
            strcmp(scene, "c00PenglaiXiandao_01") == 0 ||
            strcmp(scene, "c00PenglaiXiandao_01.sce") == 0);
}

static bool vm_net_mock_scene_is_penglai03(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33") == 0 ||
            strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0 ||
            strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33") == 0 ||
            strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_penglai04(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34") == 0 ||
            strcmp(scene, "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_penglai_transfer_scene(const char *scene)
{
    return vm_net_mock_scene_is_penglai01(scene) ||
           vm_net_mock_scene_is_penglai02(scene) ||
           vm_net_mock_scene_is_penglai03(scene) ||
           vm_net_mock_scene_is_penglai04(scene);
}

static bool vm_net_mock_scene_supports_actor_other_npc_seed(const char *scene)
{
    (void)scene;
    return false;
}

static bool vm_net_mock_seq_put_actor_other_scene_npc(u8 *out, u32 outCap, u32 *pos,
                                                      const vm_net_mock_scene_npc_seed *seed)
{
    if (seed == NULL)
        return false;

    /*
     * ParseSceneOtherNodeData(0x01012958) reads a tagged stream per row:
     *   u32 actorId,
     *   u8 visualVariant, u8 visualGroup,
     *   str labelText,
     *   u8 targetPosX, u8 targetPosY,
     *   str shortLabel,
     *   str longLabel,
     *   i16 gridPosX, i16 gridPosY.
     *
     * Evidence:
     * - main:stream_reader_init_from_blob_0x01033B16 installs readers:
     *   +0x20=i32, +0x28=i8, +0x1C=len16 string, +0x24=i16,
     *   +0x2C=peek next len16.
     * - main:ParseSceneOtherNodeData_0x01012958 calls those readers in the
     *   order above, then calls scene_node_find_or_create().
     * - negative runtime: visualGroup=0 underflows
     *   scene_node_refresh_visual(0x0100EDB0), which indexes
     *   node + 8 * visualGroup - 4 + 4 * visualVariant.
     *
     * The live NPC visual table is still not fully identified. Use the first
     * valid visual family to avoid the underflow, keep the actor resource key
     * in labelText for user-visible context, and put the NPC name in shortLabel.
     */
    return vm_net_mock_seq_put_u32(out, outCap, pos, seed->actorId) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 1) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->actorResource) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, (u8)(seed->x / 16u)) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, (u8)(seed->y / 16u)) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->displayName) &&
           vm_net_mock_seq_put_string(out, outCap, pos, seed->scriptName) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->x) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, seed->y);
}

static bool vm_net_mock_append_actor_other_scene_npcs10_object(u8 *out, u32 outCap, u32 *pos,
                                                              const char *scene,
                                                              bool minimalist,
                                                              u32 *npcCountOut,
                                                              u32 *otherInfoLenOut)
{
    if (npcCountOut)
        *npcCountOut = 0;
    if (otherInfoLenOut)
        *otherInfoLenOut = 0;

    (void)scene;
    (void)minimalist;
    return vm_net_mock_append_actor_other_empty10_object(out, outCap, pos);
}

static bool vm_net_mock_select_scene_actorinfo_npc_seed(const char *scene,
                                                       vm_net_mock_scene_npc_seed *seedOut)
{
    if (seedOut == NULL)
        return false;
    memset(seedOut, 0, sizeof(*seedOut));

    (void)scene;
    return false;
}

static bool vm_net_mock_build_scene_actorinfo_npc_blob(u8 *out, u32 outCap, u32 *blobLen,
                                                       const vm_net_mock_scene_npc_seed *seed)
{
    u32 pos = 0;
    u8 targetX = 0;
    u8 targetY = 0;

    if (out == NULL || blobLen == NULL || seed == NULL || seed->actorResource == NULL)
        return false;

    targetX = (u8)(seed->x / 16u);
    targetY = (u8)(seed->y / 16u);

    /*
     * net_handle_actor_move_info case 5 (0x01012ADC -> 0x01012B7E) reads:
     *   i16 gridX, i16 gridY, i32 actorId,
     *   u8 visualVariant, u8 visualGroup,
     *   str labelText/resource, u8 targetX, u8 targetY,
     *   str shortLabel, str longLabel,
     * then calls scene_node_find_or_create(0x0100EFC4).
     */
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->x))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->y))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, seed->actorId))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, seed->actorResource))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, targetX))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, targetY))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos,
                                   seed->displayName ? seed->displayName : "NPC"))
        return false;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos,
                                   seed->scriptName ? seed->scriptName : ""))
        return false;

    *blobLen = pos;
    return true;
}

static bool vm_net_mock_append_scene_actorinfo_npc_object(u8 *out, u32 outCap, u32 *pos,
                                                         const char *scene,
                                                         u32 *actorInfoLenOut,
                                                         u32 *actorIdOut)
{
    vm_net_mock_scene_npc_seed seed;
    u8 actorInfo[256];
    u32 actorInfoLen = 0;
    u32 objectStart = 0;

    if (actorInfoLenOut)
        *actorInfoLenOut = 0;
    if (actorIdOut)
        *actorIdOut = 0;

    if (!vm_net_mock_select_scene_actorinfo_npc_seed(scene, &seed))
        return false;
    memset(actorInfo, 0, sizeof(actorInfo));
    if (!vm_net_mock_build_scene_actorinfo_npc_blob(actorInfo, sizeof(actorInfo), &actorInfoLen, &seed))
        return false;
    if (actorInfoLen == 0 || actorInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    if (actorIdOut)
        *actorIdOut = seed.actorId;
    return true;
}

static bool vm_net_mock_append_battle_status7_object(u8 *out, u32 outCap, u32 *pos);
static u32 vm_net_mock_build_battle_pending_settlement_response(u8 *out, u32 outCap);
static bool vm_net_mock_append_battle_drop_refresh7_if_needed(
    u8 *out,
    u32 outCap,
    u32 *pos,
    u8 *objectCount,
    const char *phase,
    bool allowActiveSession);

static bool vm_net_mock_is_actor_other_only10_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 10)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_scene_interaction_followup_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_request_contains_object(request, requestLen, 1, 14, 14) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 14, 4) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 14, 5) ||
        !vm_net_mock_request_contains_object(request, requestLen, 1, 14, 6))
    {
        return false;
    }

    return true;
}

static u32 vm_net_mock_build_actor_other_only10_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u8 requestType = 0;
    u32 nearbyRoleCount = 0;
    u32 nearbyOtherInfoLen = 0;
    u8 nearbyMoveinfoCount = 0;
    bool hasType = false;
    bool allowDirectNpcOtherInfo = false;

    if (outCap < pos)
        return 0;
    hasType = vm_net_mock_get_object_u8_field(request, requestLen, "Type", &requestType);
    if (g_vm_net_mock_last_scene_change_target_valid)
    {
        const vm_net_mock_scene_change_target *target = &g_vm_net_mock_last_scene_change_target;
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               target->scene,
                                                               false, true, true, true, false, false, false))
            return 0;
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                             target->scene, target->x, target->y))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        vm_net_mock_mark_pending_scene_pos_save(target->scene, target->x, target->y, "actor-other-portal-deferred-completion");
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        return pos;
    }
    if (hasType && requestType == 100)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, 1);
        return pos;
    }

    if (hasType && requestType == 1)
    {
        const char *scene = vm_net_mock_current_scene_name();
        if (!vm_net_mock_active_client_scene_ready_for_nearby(scene))
        {
            if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
                return 0;
            vm_net_mock_finish_wt_packet(out, pos, 1);
            printf("[info][network] mock_actor_other_roles_defer scene=%s type=%u pending_target=%u pending_runtime=%u resp=%u\n",
                   scene ? scene : "-",
                   requestType,
                   g_vm_net_mock_last_scene_change_target_valid ? 1u : 0u,
                   vm_net_mock_scene_runtime_pending_without_target() ? 1u : 0u,
                   pos);
            vm_autotest_note("mock_actor_other_roles_defer scene=%s type=%u pending_target=%u pending_runtime=%u response=2/10-empty-defer\n",
                             scene ? scene : "-",
                             requestType,
                             g_vm_net_mock_last_scene_change_target_valid ? 1u : 0u,
                             vm_net_mock_scene_runtime_pending_without_target() ? 1u : 0u);
            return pos;
        }
        objectCount = vm_net_mock_append_scene_nearby_role_objects(out,
                                                                   outCap,
                                                                   &pos,
                                                                   scene,
                                                                   &nearbyRoleCount,
                                                                   &nearbyOtherInfoLen,
                                                                   &nearbyMoveinfoCount);
        if (objectCount > 0)
        {
            vm_net_mock_finish_wt_packet(out, pos, objectCount);
            printf("[info][network] mock_actor_other_roles scene=%s type=%u roles=%u otherinfo_len=%u moveinfo=%u resp=%u\n",
                   scene ? scene : "-",
                   requestType,
                   nearbyRoleCount,
                   nearbyOtherInfoLen,
                   nearbyMoveinfoCount,
                   pos);
            vm_autotest_note("mock_actor_other_roles scene=%s type=%u roles=%u otherinfo_len=%u moveinfo=%u response=2/10+2/2 evidence=JianghuOL.CBE:0x01012958/0x01012A76\n",
                             scene ? scene : "-",
                             requestType,
                             nearbyRoleCount,
                             nearbyOtherInfoLen,
                             nearbyMoveinfoCount);
            return pos;
        }
        pos = 5;
        objectCount = 0;
    }

    allowDirectNpcOtherInfo = hasType &&
                              requestType == 1 &&
                              vm_net_mock_env_u8("CBE_SCENE_NPC_OTHERINFO_DIRECT", 0) != 0;
    if (allowDirectNpcOtherInfo ||
        (hasType &&
         requestType == 1 &&
         vm_net_mock_env_u32("CBE_SCENE_NPC_OTHERINFO", 0) != 0))
    {
        const char *scene = vm_net_mock_current_scene_name();
        u32 npcCount = 0;
        u32 otherInfoLen = 0;

        if (!vm_net_mock_append_actor_other_scene_npcs10_object(out, outCap, &pos,
                                                               scene, false, &npcCount, &otherInfoLen))
            return 0;
        if (npcCount > 0)
        {
            vm_net_mock_finish_wt_packet(out, pos, 1);
            return pos;
        }
        pos = 5;
    }

    if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static bool vm_net_mock_read_current_player_grid(u32 *nodeOut, u32 *actorIdOut,
                                                 u16 *gridXOut, u16 *gridYOut,
                                                 u16 *targetXOut, u16 *targetYOut)
{
#ifdef CBE_SERVER_ONLY
    /* Only the emulator process owns a guest scene-node table.  Headless
     * movement state comes from uploaded moveinfo and service sessions. */
    (void)nodeOut;
    (void)actorIdOut;
    (void)gridXOut;
    (void)gridYOut;
    (void)targetXOut;
    (void)targetYOut;
    return false;
#else
    u32 hudState = 0;
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;

    if (!Global_R9)
        return false;
    hudState = Global_R9 + 0x5C64;
    if (uc_mem_read(MTK, hudState + 0x40, &currentSceneNode, 4) != UC_ERR_OK || currentSceneNode == 0)
        return false;
    (void)uc_mem_read(MTK, currentSceneNode + 0x64, &actorId, 4);
    (void)uc_mem_read(MTK, currentSceneNode + 0x18, &gridX, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x1A, &gridY, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x11E, &targetX, 2);
    (void)uc_mem_read(MTK, currentSceneNode + 0x120, &targetY, 2);

    if ((gridX == 0 || gridY == 0) && targetX != 0 && targetY != 0)
    {
        gridX = targetX;
        gridY = targetY;
    }
    if (gridX == 0 || gridY == 0)
        return false;

    if (nodeOut)
        *nodeOut = currentSceneNode;
    if (actorIdOut)
        *actorIdOut = actorId;
    if (gridXOut)
        *gridXOut = gridX;
    if (gridYOut)
        *gridYOut = gridY;
    if (targetXOut)
        *targetXOut = targetX;
    if (targetYOut)
        *targetYOut = targetY;
    return true;
#endif
}

typedef struct
{
    const char *sourceScene;
    u16 left;
    u16 top;
    u16 right;
    u16 bottom;
    u8 sourceEntry;
    u8 targetEntry;
    const char *targetScene;
    u16 targetX;
    u16 targetY;
    const char *routeName;
} vm_net_mock_portal_fallback;

static const vm_net_mock_portal_fallback kVmNetMockPortalFallbacks[] = {
        {
            "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31\x2e\x73\x63\x65",
            119, 480, 264, 502,
            0, 0,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            396, 473,
            "penglai-01-bottom-to-02"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            108, 5, 148, 25,
            1, 2,
            "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            223, 382,
            "penglai-02-north-to-01"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            421, 453, 441, 490,
            0, 3,
            "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            145, 47,
            "penglai-02-east-to-03"
        },
        {
            "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33",
            75, 10, 135, 43,
            0, 1,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            128, 45,
            "penglai-03-north-to-02"
        },
        {
            "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33",
            421, 268, 441, 308,
            1, 3,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            225, 116,
            "c00-penglai-03-east-to-taohuadao-01"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            75, 10, 135, 43,
            1, 2,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            128, 45,
            "penglai-03-north-to-02"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            64, 400, 144, 416,
            0, 0,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            136, 58,
            "penglai-03-bottom-to-04"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            116, 10, 156, 43,
            0, 2,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            105, 395,
            "penglai-04-north-to-03"
        },
        {
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            224, 256, 288, 285,
            1, 2,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            225, 116,
            "penglai-04-to-taohuadao-01"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            208, 432, 256, 448,
            0, 0,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            80, 60,
            "taohuadao-01-bottom-to-02"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            240, 96, 272, 128,
            1, 3,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            256, 300,
            "taohuadao-01-to-penglai-04"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            320, 272, 352, 336,
            1, 3,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            40, 70,
            "taohuadao-02-east-to-03"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            32, 0, 128, 48,
            0, 2,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65",
            225, 116,
            "taohuadao-02-north-to-01"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            160, 553, 240, 570,
            0, 0,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            42, 60,
            "taohuadao-03-bottom-to-04"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            20, 10, 60, 55,
            1, 2,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65",
            305, 310,
            "taohuadao-03-north-to-02"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            336, 160, 384, 224,
            1, 0,
            "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            136, 58,
            "taohuadao-04-to-penglai-04"
        },
        {
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65",
            20, 5, 64, 35,
            0, 2,
            "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65",
            200, 540,
            "taohuadao-04-north-to-03"
        },
};

static bool vm_net_mock_find_portal_fallback_with_margin(const char *scene, u16 gridX, u16 gridY,
                                                         u16 margin,
                                                         vm_net_mock_scene_change_target *target,
                                                         const char **routeNameOut)
{
    size_t i;

    if (scene == NULL || target == NULL)
        return false;

    if (vm_net_mock_get_scene_portal_target_from_sce(scene, gridX, gridY, margin, target))
    {
        if (routeNameOut)
            *routeNameOut = "sce-edge-portal";
        return true;
    }

    if (vm_net_mock_scene_edge_data_available(scene))
        return false;

    if (vm_net_mock_scene_name_is_download_key(scene))
    {
        vm_autotest_note("mock_scene_missing_sce source=%s pos=(%u,%u) action=wait-client-download\n",
                         scene, gridX, gridY);
        return false;
    }

    for (i = 0; i < sizeof(kVmNetMockPortalFallbacks) / sizeof(kVmNetMockPortalFallbacks[0]); ++i)
    {
        const vm_net_mock_portal_fallback *fallback = &kVmNetMockPortalFallbacks[i];
        u16 left = (fallback->left > margin) ? (u16)(fallback->left - margin) : 0;
        u16 top = (fallback->top > margin) ? (u16)(fallback->top - margin) : 0;
        u16 right = (u16)(fallback->right + margin);
        u16 bottom = (u16)(fallback->bottom + margin);
        if (strcmp(scene, fallback->sourceScene) != 0)
            continue;
        if (gridX < left || gridX > right || gridY < top || gridY > bottom)
            continue;

        memset(target, 0, sizeof(*target));
        snprintf(target->scene, sizeof(target->scene), "%s", fallback->targetScene);
        target->x = fallback->targetX;
        target->y = fallback->targetY;
        if (routeNameOut)
            *routeNameOut = fallback->routeName;
        return true;
    }

    return false;
}

static bool vm_net_mock_find_portal_fallback(const char *scene, u16 gridX, u16 gridY,
                                             vm_net_mock_scene_change_target *target,
                                             const char **routeNameOut)
{
    return vm_net_mock_find_portal_fallback_with_margin(scene, gridX, gridY, 0,
                                                       target, routeNameOut);
}

static bool vm_net_mock_find_portal_fallback_margin(const char *scene, u16 gridX, u16 gridY,
                                                    u16 margin,
                                                    vm_net_mock_scene_change_target *target,
                                                    const char **routeNameOut)
{
    return vm_net_mock_find_portal_fallback_with_margin(scene, gridX, gridY, margin,
                                                       target, routeNameOut);
}

static u32 vm_net_mock_build_actor_other_portal_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    const char *scene = NULL;
    vm_net_mock_scene_change_target target;
    u16 gridX = 0;
    u16 gridY = 0;

    if (outCap < pos || !vm_net_mock_is_actor_other_only10_request(request, requestLen))
        return 0;

    scene = vm_net_mock_current_scene_name();
    if (vm_net_mock_is_recent_completed_scene_name(scene, 180))
        return 0;

    if (!vm_net_mock_read_current_player_grid(NULL, NULL, &gridX, &gridY, NULL, NULL))
        return 0;
    if (vm_net_mock_scene_is_penglai_transfer_scene(scene))
        return 0;

    if (!vm_net_mock_find_portal_fallback(scene, gridX, gridY, &target, NULL))
    {
        return 0;
    }

    if (!(g_vm_net_mock_pending_scene_save_valid &&
          strcmp(g_vm_net_mock_pending_scene_save_scene, target.scene) == 0))
    {
        u32 objectStart = 0;
        vm_net_mock_remember_scene_change_target(&target);
        g_vm_net_mock_last_scene_change_from_actor_other_portal = true;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        return pos;
    }

    /*
     * IDA/runtime evidence: these same-class edge portals are already present in
     * the local scene table consumed by sub_1018166(). When the player steps
     * inside such a rect, the client emits this 2/10 Type=1 request as part of
     * its local transition countdown. If the target is already queued for a
     * pending save, leave this duplicate as a no-op and let the later completion
     * request close the transition.
     */
    return 0;
}

static bool vm_net_mock_is_actor_moveinfo_upload_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 1)
        return false;
    if (!vm_net_mock_request_contains(request, requestLen, "moveinfo"))
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static bool vm_net_mock_is_actor_type_control_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 controlType = 0;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 1)
        return false;
    if (vm_net_mock_request_contains(request, requestLen, "moveinfo"))
        return false;
    if (!vm_net_mock_get_object_u8_field(request, requestLen, "type", &controlType))
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_actor_type_control_ack_response(const u8 *request, u32 requestLen,
                                                              u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (!vm_net_mock_is_actor_type_control_request(request, requestLen) || outCap < pos)
        return 0;
    /*
     * The client only needs completion of this scene/actor control object.  Keep
     * the original 2/1 identity and do not reinterpret its generic `type` field
     * as the unrelated 7/7 bootstrap command.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 1, &objectStart))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static bool vm_net_mock_is_actor_moveinfo_name_update_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 2 || object.subtype != 12)
        return false;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_actor_moveinfo_name_update_response(const u8 *request, u32 requestLen,
                                                                u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *name = role && role->name[0] ? role->name : vm_net_mock_default_role_name();

    (void)request;
    (void)requestLen;
    if (outCap < pos || !vm_net_mock_is_actor_moveinfo_name_update_request(request, requestLen))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", name ? name : ""))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static bool vm_net_mock_append_actor_moveinfo_empty_ack_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 1, &objectStart))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_seq_put_len16_blob(u8 *out, u32 outCap, u32 *pos,
                                           const u8 *data, u16 dataLen)
{
    return vm_net_mock_put_be16(out, outCap, pos, dataLen) &&
           vm_net_mock_put_bytes(out, outCap, pos, data, dataLen);
}

static bool vm_net_mock_build_scene_monster_moveinfo_blob(u8 *out, u32 outCap,
                                                          u32 *blobLenOut,
                                                          u32 actorId,
                                                          u32 posx,
                                                          u32 posy)
{
    u32 pos = 0;
    u8 state[64];
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(actorId);
    u32 enemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
    u32 enemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", enemyHp);
    u32 enemyMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", stats.mp);
    u32 enemyMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_MP", enemyMp);

    if (out == NULL || blobLenOut == NULL || actorId == 0)
        return false;
    if (enemyMaxHp < enemyHp)
        enemyMaxHp = enemyHp;
    if (enemyMaxMp < enemyMp)
        enemyMaxMp = enemyMp;

    memset(state, 0, sizeof(state));
    /*
     * net_handle_actor_move_info case 2 copies this raw blob into
     * ActorSceneNode+0x88. Battle.cbm subtype 5 then reads HP/MP from
     * node+0xB4/+0xB8/+0xBC/+0xC0, i.e. blob+44/+48/+52/+56.
     */
    vm_net_mock_store_le32(state, 44, enemyHp);
    vm_net_mock_store_le32(state, 48, enemyMp);
    vm_net_mock_store_le32(state, 52, enemyMaxHp);
    vm_net_mock_store_le32(state, 56, enemyMaxMp);

    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, (u16)posx))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, (u16)posy))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorId))
        return false;
    if (!vm_net_mock_seq_put_len16_blob(out, outCap, &pos, state, (u16)sizeof(state)))
        return false;

    *blobLenOut = pos;
    return true;
}

static bool vm_net_mock_build_scene_player_moveinfo_blob(u8 *out, u32 outCap,
                                                         u32 *blobLenOut,
                                                         const vm_net_mock_scene_role_seed *seed,
                                                         u32 *deliveredMoveSerialOut)
{
    u32 pos = 0;
    u8 state[64];
    u32 hp = 0;
    u32 hpMax = 0;
    u32 mp = 0;
    u32 mpMax = 0;
    vm_mock_service_client_session *observer = vm_mock_service_get_active_client_session();
    vm_mock_service_peer_sync *peerSync = NULL;

    if (deliveredMoveSerialOut)
        *deliveredMoveSerialOut = 0;
    if (out == NULL || blobLenOut == NULL || seed == NULL || seed->actorId == 0)
        return false;
    if (observer != NULL && seed->session != NULL)
    {
        peerSync = vm_mock_service_get_peer_sync(observer,
                                                 seed->session->clientId,
                                                 seed->actorId,
                                                 seed->session->pendingDirQueueSerial,
                                                 false,
                                                 NULL);
    }
    if (seed->session != NULL &&
        seed->session->lastMoveinfoValid &&
        seed->session->lastMoveinfoLen <= outCap)
    {
        if (seed->session->lastMoveinfoFormat == VM_MOCK_SERVICE_MOVEINFO_FORMAT_RESPONSE_ENTRY &&
            seed->session->lastMoveinfoLen >= 16)
        {
            memcpy(out, seed->session->lastMoveinfoBlob, seed->session->lastMoveinfoLen);
            if (out[0] == 0 && out[1] == 2 &&
                out[4] == 0 && out[5] == 2 &&
                out[8] == 0 && out[9] == 4)
            {
                vm_net_mock_store_be16(out, 2, seed->x);
                vm_net_mock_store_be16(out, 6, seed->y);
                vm_net_mock_store_be32(out, 10, seed->actorId);
                *blobLenOut = seed->session->lastMoveinfoLen;
                return true;
            }
        }
        if (seed->session->lastMoveinfoFormat == VM_MOCK_SERVICE_MOVEINFO_FORMAT_TIMELINE &&
            vm_net_mock_is_actor_moveinfo_timeline(seed->session->lastMoveinfoBlob,
                                                   seed->session->lastMoveinfoLen))
        {
            if (peerSync != NULL &&
                peerSync->visible &&
                peerSync->lastMoveSerial != seed->session->pendingDirQueueSerial &&
                seed->session->pendingDirQueueValid &&
                seed->session->pendingDirQueueSerial != 0 &&
                seed->session->pendingDirQueueLen > 0 &&
                seed->session->pendingDirQueueLen <= sizeof(seed->session->pendingDirQueueBlob))
            {
                u8 catchupQueue[32];
                u16 catchupLen = 0;
                u16 catchupX = seed->session->pendingDirQueueStartX;
                u16 catchupY = seed->session->pendingDirQueueStartY;
                u16 directionCount = 0;
                u16 skipDirections = 0;
                u16 maxCatchupSteps = vm_net_mock_env_u8("CBE_SCENE_SYNC_MAX_CATCHUP_STEPS", 4);

                /*
                 * scene_node_update_move_blob() copies the raw queue into
                 * node+136 and resets node+317/node+318. The per-frame
                 * ProcessSceneAutoAction() loop then starts from the outer x/y
                 * position and consumes one 4-pixel step per queue byte. So a
                 * live movement burst must be sent as:
                 *   start-position + queued-bytes
                 * once per observer-side delivery cursor. The source keeps the
                 * latest burst until every observer has independently polled it.
                 *
                 * The upload is already a completed ten-frame history by the
                 * time the service receives it. Replaying all ten frames keeps
                 * the remote actor a second behind. Advance the exact outer
                 * start through the older prefix and retain only the last few
                 * real direction steps. Zero/idle bytes carry historical timing
                 * but no position, so they are intentionally omitted here.
                 */
                if (maxCatchupSteps == 0)
                    maxCatchupSteps = 1;
                if (maxCatchupSteps > sizeof(catchupQueue))
                    maxCatchupSteps = sizeof(catchupQueue);
                for (u16 i = 0; i < seed->session->pendingDirQueueLen; ++i)
                {
                    if (seed->session->pendingDirQueueBlob[i] != 0)
                        ++directionCount;
                }
                skipDirections = directionCount > maxCatchupSteps ?
                                 (u16)(directionCount - maxCatchupSteps) : 0;
                for (u16 i = 0; i < seed->session->pendingDirQueueLen; ++i)
                {
                    u8 direction = seed->session->pendingDirQueueBlob[i];
                    if (direction == 0)
                        continue;
                    if (skipDirections > 0)
                    {
                        vm_net_mock_apply_actor_moveinfo_timeline(&catchupX, &catchupY,
                                                                  &direction, 1);
                        --skipDirections;
                        continue;
                    }
                    catchupQueue[catchupLen++] = direction;
                }
                if (!vm_net_mock_seq_put_i16(out, outCap, &pos,
                                             catchupX))
                    return false;
                if (!vm_net_mock_seq_put_i16(out, outCap, &pos,
                                             catchupY))
                    return false;
                if (!vm_net_mock_seq_put_u32(out, outCap, &pos, seed->actorId))
                    return false;
                if (!vm_net_mock_seq_put_len16_blob(out, outCap, &pos,
                                                    catchupQueue,
                                                    catchupLen))
                    return false;
                printf("[info][mock-service] scene_move_catchup source=%08x frames=%u directions=%u sent=%u start=(%u,%u) end=(%u,%u)\n",
                       seed->session->clientId,
                       seed->session->pendingDirQueueLen,
                       directionCount,
                       catchupLen,
                       catchupX,
                       catchupY,
                       seed->session->pendingDirQueueEndX,
                       seed->session->pendingDirQueueEndY);
                if (deliveredMoveSerialOut)
                    *deliveredMoveSerialOut = seed->session->pendingDirQueueSerial;
                *blobLenOut = pos;
                return true;
            }
            if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->x))
                return false;
            if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->y))
                return false;
            if (!vm_net_mock_seq_put_u32(out, outCap, &pos, seed->actorId))
                return false;
            if (!vm_net_mock_seq_put_len16_blob(out, outCap, &pos, NULL, 0))
                return false;
            *blobLenOut = pos;
            return true;
        }
        if (seed->session->lastMoveinfoFormat == VM_MOCK_SERVICE_MOVEINFO_FORMAT_OPAQUE_SMALL &&
            seed->session->lastMoveinfoLen > 0)
        {
            /*
             * Upload-side small moveinfo blobs are observed during steady-state
             * polls, but their exact downlink contract is still unresolved.
             * Replaying them verbatim into 1/2/2 keeps re-arming the remote
             * node's movement state and causes visible action loops. Until the
             * client-side producer/parser pair is fully recovered, treat them
             * as upload-only hints and publish only the authoritative position.
             */
            if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->x))
                return false;
            if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->y))
                return false;
            if (!vm_net_mock_seq_put_u32(out, outCap, &pos, seed->actorId))
                return false;
            if (!vm_net_mock_seq_put_len16_blob(out, outCap, &pos, NULL, 0))
                return false;
            *blobLenOut = pos;
            return true;
        }
    }
    hp = seed->hp;
    hpMax = seed->hpMax;
    mp = seed->mp;
    mpMax = seed->mpMax;
    if (hpMax < hp)
        hpMax = hp;
    if (mpMax < mp)
        mpMax = mp;

    memset(state, 0, sizeof(state));
    vm_net_mock_store_le32(state, 44, hp);
    vm_net_mock_store_le32(state, 48, mp);
    vm_net_mock_store_le32(state, 52, hpMax);
    vm_net_mock_store_le32(state, 56, mpMax);

    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->x))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seed->y))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, seed->actorId))
        return false;
    if (!vm_net_mock_seq_put_len16_blob(out, outCap, &pos, state, (u16)sizeof(state)))
        return false;

    *blobLenOut = pos;
    return true;
}

static bool vm_net_mock_append_scene_monster_moveinfo2_object(u8 *out, u32 outCap,
                                                              u32 *pos,
                                                              u32 actorId,
                                                              u32 posx,
                                                              u32 posy)
{
    u8 moveInfo[128];
    u32 moveInfoLen = 0;
    u32 objectStart = 0;

    memset(moveInfo, 0, sizeof(moveInfo));
    if (!vm_net_mock_build_scene_monster_moveinfo_blob(moveInfo, sizeof(moveInfo),
                                                       &moveInfoLen,
                                                       actorId, posx, posy))
        return false;
    if (moveInfoLen == 0 || moveInfoLen > 0xffff)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 2, &objectStart))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "moveinfo",
                                      moveInfo, (u16)moveInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(actorId);
        u32 enemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
        u32 enemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", enemyHp);
        u32 enemyMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", stats.mp);
        u32 enemyMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_MP", enemyMp);
        if (enemyMaxHp < enemyHp)
            enemyMaxHp = enemyHp;
        if (enemyMaxMp < enemyMp)
            enemyMaxMp = enemyMp;
        vm_autotest_note("mock_scene_monster_moveinfo actor=%u level=%u pos=(%u,%u) hp=%u/%u mp=%u/%u len=%u\n",
                         actorId, stats.level, posx, posy,
                         enemyHp, enemyMaxHp, enemyMp, enemyMaxMp,
                         moveInfoLen);
    }
    return true;
}

static bool vm_net_mock_append_scene_player_moveinfo2_object(u8 *out, u32 outCap,
                                                             u32 *pos,
                                                             const vm_net_mock_scene_role_seed *seed)
{
    u8 moveInfo[128];
    u32 moveInfoLen = 0;
    u32 deliveredMoveSerial = 0;
    u32 objectStart = 0;

    memset(moveInfo, 0, sizeof(moveInfo));
    if (!vm_net_mock_build_scene_player_moveinfo_blob(moveInfo, sizeof(moveInfo),
                                                      &moveInfoLen,
                                                      seed,
                                                      &deliveredMoveSerial))
        return false;
    if (moveInfoLen == 0 || moveInfoLen > 0xffff)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 2, &objectStart))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "moveinfo",
                                      moveInfo, (u16)moveInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (seed->session != NULL)
    {
        vm_mock_service_client_session *observer = vm_mock_service_get_active_client_session();
        vm_mock_service_peer_sync *peerSync = vm_mock_service_get_peer_sync(
            observer,
            seed->session->clientId,
            seed->actorId,
            seed->session->pendingDirQueueSerial,
            true,
            NULL);
        if (peerSync != NULL)
        {
            peerSync->visible = true;
            peerSync->actorId = seed->actorId;
            if (deliveredMoveSerial != 0)
                peerSync->lastMoveSerial = deliveredMoveSerial;
        }
    }
    return true;
}

static bool vm_net_mock_append_scene_role_remove6_object(u8 *out, u32 outCap, u32 *pos, u32 actorId)
{
    u32 objectStart = 0;

    if (actorId == 0)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 2, 6, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "actorid", actorId))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u8 vm_net_mock_append_scene_role_remove6_objects(u8 *out, u32 outCap, u32 *pos,
                                                        const char *scene)
{
    vm_net_mock_scene_role_seed seeds[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX];
    u8 roleCount = 0;
    u8 appended = 0;
    u32 savedPos = 0;

    if (pos == NULL)
        return 0;
    savedPos = *pos;
    roleCount = vm_net_mock_build_scene_role_seeds(scene, seeds, VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX);
    for (u8 i = 0; i < roleCount; ++i)
    {
        if (!vm_net_mock_append_scene_role_remove6_object(out, outCap, pos, seeds[i].actorId))
        {
            *pos = savedPos;
            return 0;
        }
        ++appended;
    }
    return appended;
}

static u8 vm_net_mock_append_scene_role_moveinfo2_objects(u8 *out, u32 outCap, u32 *pos,
                                                          const char *scene)
{
    vm_net_mock_scene_role_seed seeds[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX];
    u8 roleCount = 0;
    u8 appended = 0;
    u32 savedPos = 0;

    if (pos == NULL)
        return 0;
    savedPos = *pos;
    roleCount = vm_net_mock_build_scene_role_seeds(scene, seeds, VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX);
    for (u8 i = 0; i < roleCount; ++i)
    {
        if (!vm_net_mock_append_scene_player_moveinfo2_object(out, outCap, pos, &seeds[i]))
        {
            *pos = savedPos;
            return 0;
        }
        ++appended;
    }
    return appended;
}

static u8 vm_net_mock_append_scene_nearby_role_objects(u8 *out, u32 outCap, u32 *pos,
                                                       const char *scene,
                                                       u32 *roleCountOut,
                                                       u32 *otherInfoLenOut,
                                                       u8 *moveinfoCountOut)
{
    u32 savedPos = 0;
    u32 roleCount = 0;
    u32 otherInfoLen = 0;
    u8 moveinfoCount = 0;

    if (roleCountOut)
        *roleCountOut = 0;
    if (otherInfoLenOut)
        *otherInfoLenOut = 0;
    if (moveinfoCountOut)
        *moveinfoCountOut = 0;
    if (pos == NULL || !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_active_client_scene_ready_for_nearby(scene))
    {
        return 0;
    }

    savedPos = *pos;
    if (!vm_net_mock_append_actor_other_scene_roles10_object(out,
                                                             outCap,
                                                             pos,
                                                             scene,
                                                             &roleCount,
                                                             &otherInfoLen) ||
        roleCount == 0)
    {
        *pos = savedPos;
        return 0;
    }
    moveinfoCount = vm_net_mock_append_scene_role_moveinfo2_objects(out, outCap, pos, scene);
    if (moveinfoCount == 0 && roleCount > 0)
    {
        *pos = savedPos;
        return 0;
    }

    if (roleCountOut)
        *roleCountOut = roleCount;
    if (otherInfoLenOut)
        *otherInfoLenOut = otherInfoLen;
    if (moveinfoCountOut)
        *moveinfoCountOut = moveinfoCount;
    return (u8)(1 + moveinfoCount);
}

/* A first-scene response has a confirmed practical ceiling of ten WT objects.
 * Put nearby rows into the request's existing 2/10 response slot, then use
 * this helper to baseline the observer-side movement cursors after the active
 * session becomes visible.  The next real movement serial is still delivered
 * once, while an old queue cannot replay immediately after node creation. */
static u32 vm_mock_service_mark_scene_nearby_role_baseline_visible(const char *scene)
{
    vm_mock_service_client_session *observer =
        vm_mock_service_get_active_client_session();
    vm_net_mock_scene_role_seed seeds[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX];
    u8 seedCount = 0;
    u32 markedCount = 0;

    if (observer == NULL ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_mock_service_session_scene_is_visible(observer, scene))
    {
        return 0;
    }

    seedCount = vm_net_mock_build_scene_role_seeds(scene,
                                                    seeds,
                                                    VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX);
    for (u8 i = 0; i < seedCount; ++i)
    {
        vm_mock_service_client_session *source = seeds[i].session;
        vm_mock_service_peer_sync *peerSync = NULL;

        if (source == NULL)
            continue;
        peerSync = vm_mock_service_get_peer_sync(observer,
                                                 source->clientId,
                                                 seeds[i].actorId,
                                                 source->pendingDirQueueSerial,
                                                 true,
                                                 NULL);
        if (peerSync == NULL)
            continue;
        peerSync->visible = true;
        peerSync->actorId = seeds[i].actorId;
        peerSync->lastMoveSerial = source->pendingDirQueueSerial;
        ++markedCount;
    }
    return markedCount;
}

static bool vm_net_mock_scene_role_seeds_contain_client(const vm_net_mock_scene_role_seed *seeds,
                                                        u8 roleCount,
                                                        u32 clientId)
{
    if (seeds == NULL || clientId == 0)
        return false;
    for (u8 i = 0; i < roleCount; ++i)
    {
        if (seeds[i].session != NULL && seeds[i].session->clientId == clientId)
            return true;
    }
    return false;
}

/* net_handle_type_payload_detail(0x010126C6) reads 1/3/3 as:
 *   type:u8, chatinfo:blob
 * chatinfo is count:u8 followed by repeated
 *   senderName:len16-string, senderRoleId:tagged-u32, message:len16-string.
 * The blob field's own len16 prefix supplies the first tagged reader header,
 * so the count itself is deliberately written as one raw byte.
 *
 * The parser always appends to the aggregate chat list before its type switch.
 * Type 0 therefore is not ignored: it remains the native [世] entry rendered
 * by RenderColoredTextLines(0x01034DF6) as RGB 0xFFDE00.  Type 6 is a [系]
 * entry rendered green and must not be used as a world-message surrogate. */
static bool vm_net_mock_append_chat_message_object(
    u8 *out,
    u32 outCap,
    u32 *pos,
    u8 type,
    u32 sourceRoleId,
    const char *sourceName,
    const char *message)
{
    u8 chatInfo[160];
    u32 chatInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || sourceName == NULL || message == NULL ||
        (type != VM_MOCK_CHAT_TYPE_WORLD &&
         (type < VM_MOCK_CHAT_TYPE_TEAM || type > VM_MOCK_CHAT_TYPE_TEAM_NOTICE)) ||
        strlen(sourceName) > 15 || strlen(message) > 81)
    {
        return false;
    }
    if (!vm_net_mock_put_u8(chatInfo, sizeof(chatInfo), &chatInfoLen, 1) ||
        !vm_net_mock_seq_put_string(chatInfo, sizeof(chatInfo), &chatInfoLen, sourceName) ||
        !vm_net_mock_seq_put_u32(chatInfo, sizeof(chatInfo), &chatInfoLen, sourceRoleId) ||
        !vm_net_mock_seq_put_string(chatInfo, sizeof(chatInfo), &chatInfoLen, message) ||
        !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 3, 3, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "type", type) ||
        !vm_net_mock_put_object_blob(out, outCap, pos, "chatinfo", chatInfo,
                                     (u16)chatInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static int vm_net_mock_append_scene_sync_chat_objects_limited(
    u8 *out,
    u32 outCap,
    u32 *pos,
    vm_mock_service_client_session *observer,
    u8 maxObjects)
{
    int appended = 0;

    if (out == NULL || pos == NULL || observer == NULL)
        return -1;
    while (observer->chatNoticeCount > 0 &&
           appended < VM_MOCK_SERVICE_CHAT_POLL_MAX &&
           appended < maxObjects)
    {
        vm_mock_service_chat_notice *notice =
            &observer->chatNotices[observer->chatNoticeHead];
        vm_mock_service_client_session *source = NULL;
        u32 sourceWireId = notice->sourceRoleId;
        u32 ageTicks = 0;

        if (!notice->valid)
        {
            memset(notice, 0, sizeof(*notice));
            observer->chatNoticeHead =
                (u8)((observer->chatNoticeHead + 1) % VM_MOCK_SERVICE_CHAT_NOTICE_MAX);
            --observer->chatNoticeCount;
            continue;
        }
        if (notice->sourceClientId != 0)
        {
            source = vm_mock_service_find_client_session(notice->sourceClientId);
            if (source != NULL && source->onlineRoleId == notice->sourceRoleId)
            {
                u32 resolvedWireId = vm_mock_service_team_member_wire_id(observer, source);
                if (resolvedWireId != 0)
                    sourceWireId = resolvedWireId;
            }
        }
        if (!vm_net_mock_append_chat_message_object(out, outCap, pos,
                                                    notice->type,
                                                    sourceWireId,
                                                    notice->sourceName,
                                                    notice->message))
        {
            return -1;
        }
        if (g_schedulerTick >= notice->queuedTick)
            ageTicks = g_schedulerTick - notice->queuedTick;
        printf("[info][mock-service] chat_notice_deliver observer=%08x type=%s source=%08x/%u bytes=%u age_ticks=%u\n",
               observer->clientId,
               vm_mock_service_chat_type_name(notice->type),
               notice->sourceClientId,
               sourceWireId,
               (u32)strlen(notice->message),
               ageTicks);
        memset(notice, 0, sizeof(*notice));
        observer->chatNoticeHead =
            (u8)((observer->chatNoticeHead + 1) % VM_MOCK_SERVICE_CHAT_NOTICE_MAX);
        --observer->chatNoticeCount;
        ++appended;
    }
    return appended;
}

static int vm_net_mock_append_scene_sync_chat_objects(
    u8 *out,
    u32 outCap,
    u32 *pos,
    vm_mock_service_client_session *observer)
{
    return vm_net_mock_append_scene_sync_chat_objects_limited(
        out, outCap, pos, observer, VM_MOCK_SERVICE_CHAT_POLL_MAX);
}

/*
 * The remote mock service is request/response TCP, but every live emulator
 * issues a scene-sync poll on the normal event-7 path.  Use that existing
 * client-driven poll as the delivery point for social notices rather than
 * attempting a server-side socket push that the CBE has no listener for.
 */
static int vm_net_mock_append_scene_sync_social_notice_object(
    u8 *out, u32 outCap, u32 *pos, vm_mock_service_client_session *observer,
    u8 *noticeTypeOut)
{
    vm_mock_service_social_notice *notice = NULL;
    u32 objectStart = 0;
    u8 noticeType = VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE;

    if (noticeTypeOut)
        *noticeTypeOut = VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE;
    if (out == NULL || pos == NULL || observer == NULL)
        return -1;

    /* A modal invitation remains pending until the target emits 10/5, 21/3,
     * 5/3, or 4/16.  Do not stack another modal over it. */
    if (observer->friendInviteReplyActive || observer->tradeInviteReplyActive ||
        observer->teamInviteReplyActive || observer->sparInviteReplyActive)
        return 0;

    for (u32 i = 0; i < VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX; ++i)
    {
        if (observer->socialNotices[i].type != VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE)
        {
            notice = &observer->socialNotices[i];
            break;
        }
    }
    if (notice == NULL)
        return 0;

    noticeType = notice->type;
    switch (noticeType)
    {
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_INVITE:
        /* net_handle_role_login_gift_glamour(0x010114FC), subtype 4:
         * reads id/name and opens the confirm callback that sends 10/5. */
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 10, 4, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "id", notice->sourceRoleId) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name", notice->sourceName))
        {
            return -1;
        }
        observer->friendInviteReplyActive = true;
        observer->friendInviteSourceClientId = notice->sourceClientId;
        observer->friendInviteSourceRoleId = notice->sourceRoleId;
        break;

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_INVITE:
        /* net_handle_trade_response(0x01011132), subtype 2: id/name then
         * net_send_trade_invite_reply() emits 21/3. */
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 21, 2, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "id", notice->sourceRoleId) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name", notice->sourceName))
        {
            return -1;
        }
        observer->tradeInviteReplyActive = true;
        observer->tradeInviteSourceClientId = notice->sourceClientId;
        observer->tradeInviteSourceRoleId = notice->sourceRoleId;
        break;

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_RESULT:
        /* Same handler, subtype 6: result=1 accepted, non-1 refused. */
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 10, 6, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "result", notice->result) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name", notice->sourceName))
        {
            return -1;
        }
        break;

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_RESULT:
        /* net_handle_trade_response subtype 4 opens the trade UI on result=1
         * and renders the peer's refusal on result=2. */
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 21, 4, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "result", notice->result) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name", notice->sourceName))
        {
            return -1;
        }
        break;

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_INVITE:
    {
        vm_mock_service_client_session *source =
            vm_mock_service_find_client_session(notice->sourceClientId);
        u32 sourceWireId = 0;

        if (source == NULL || !source->roleOnline ||
            source->onlineRoleId != notice->sourceRoleId ||
            (sourceWireId = vm_mock_service_team_member_wire_id(observer, source)) == 0)
        {
            printf("[warn][mock-service] team_invite_drop observer=%08x source=%08x/%u reason=source-unavailable\n",
                   observer->clientId, notice->sourceClientId, notice->sourceRoleId);
            memset(notice, 0, sizeof(*notice));
            return 0;
        }
        /* net_handle_group_info subtype 2 reads id/name and invokes
         * HandleGuildJoinConfirm(0x01011ED0), which sends 1/5/3.  Use the
         * exact actor id that represents this remote player to `observer`;
         * guest persistent role ids collide with the observer's own 10001. */
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 2, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "id", sourceWireId) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name", notice->sourceName))
        {
            return -1;
        }
        observer->teamInviteReplyActive = true;
        observer->teamInviteSourceClientId = notice->sourceClientId;
        observer->teamInviteSourceWireId = sourceWireId;
        break;
    }

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_INVITE:
    {
        vm_mock_service_client_session *source =
            vm_mock_service_find_client_session(notice->sourceClientId);
        u32 sourceWireId = 0;

        if (source == NULL || !source->roleOnline ||
            source->onlineRoleId != notice->sourceRoleId ||
            !observer->sceneVisibleReady ||
            !vm_mock_service_session_scene_is_visible(source,
                                                       observer->sceneVisibleScene) ||
            (sourceWireId = vm_mock_service_team_member_wire_id(observer, source)) == 0)
        {
            printf("[warn][mock-service] spar_invite_drop observer=%08x "
                   "source=%08x/%u reason=source-unavailable-or-scene-changed\n",
                   observer->clientId, notice->sourceClientId,
                   notice->sourceRoleId);
            memset(notice, 0, sizeof(*notice));
            return 0;
        }
        /* net_handle_login_or_name_result(0x0101258A), subtype 15, stores id
         * and name then opens the native duel confirmation dialog. */
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 15,
                                         &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "id", sourceWireId) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name",
                                           notice->sourceName))
        {
            return -1;
        }
        observer->sparInviteReplyActive = true;
        observer->sparInviteSourceClientId = notice->sourceClientId;
        observer->sparInviteSourceWireId = sourceWireId;
        break;
    }

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_RESULT:
    {
        vm_mock_service_client_session *source =
            vm_mock_service_find_client_session(notice->sourceClientId);
        u32 sourceWireId = source ?
            vm_mock_service_team_member_wire_id(observer, source) :
            notice->sourceRoleId;

        /* Subtype 17 result=2 renders "<name>不同意和你切磋".  Result=1 is
         * intentionally silent in the client but records the accepted reply. */
        if (sourceWireId == 0 ||
            !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 17,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "result",
                                       notice->result) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "id", sourceWireId) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name",
                                           notice->sourceName))
        {
            return -1;
        }
        break;
    }

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_RESULT:
    {
        vm_mock_service_client_session *source =
            vm_mock_service_find_client_session(notice->sourceClientId);
        vm_mock_service_team *team =
            vm_mock_service_team_find_for_client(observer->clientId);
        u32 sourceWireId = source ? vm_mock_service_team_member_wire_id(observer, source)
                                  : notice->sourceRoleId;
        int appendedObjects = notice->result == 1 ? 2 : 1;

        /* Subtype 4 only reports the target's answer; it never inserts roster
         * rows.  Friend-list invites start from an empty local roster, so a
         * lone subtype 5 would add only the remote member and leave the local
         * count at one.  The team UI requires a count greater than one.  Send
         * the authoritative subtype-10 roster to the inviter, while existing
         * third-party members continue to receive the subtype-5 delta. */
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 4, &objectStart) ||
            sourceWireId == 0 ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "id", sourceWireId) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "result", notice->result) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name", notice->sourceName))
        {
            return -1;
        }
        vm_net_mock_finish_wt_object(out, objectStart, *pos);
        if (notice->result == 1)
        {
            if (source == NULL || source->onlineRoleId != notice->sourceRoleId ||
                team == NULL ||
                !vm_net_mock_append_team_group_info_object(out, outCap, pos,
                                                           team, observer, 10))
            {
                return -1;
            }
        }
        printf("[info][mock-service] team_result_deliver observer=%08x result=%u roster_update=%s members=%u\n",
               observer->clientId, notice->result,
               notice->result == 1 ? "5/10-full" : "none",
               team ? team->memberCount : 0);
        memset(notice, 0, sizeof(*notice));
        if (noticeTypeOut)
            *noticeTypeOut = noticeType;
        return appendedObjects;
    }

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_MEMBER_JOIN:
    {
        vm_mock_service_client_session *source =
            vm_mock_service_find_client_session(notice->sourceClientId);
        if (source == NULL || source->onlineRoleId != notice->sourceRoleId ||
            !vm_net_mock_append_team_member_join_object(out, outCap, pos,
                                                        observer, source))
        {
            memset(notice, 0, sizeof(*notice));
            return 0;
        }
        printf("[info][mock-service] team_member_join_deliver observer=%08x member=%08x/%u update=5/5\n",
               observer->clientId, source->clientId, source->onlineRoleId);
        memset(notice, 0, sizeof(*notice));
        if (noticeTypeOut)
            *noticeTypeOut = noticeType;
        return 1;
    }

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_LEAVE:
    {
        vm_mock_service_client_session *source =
            vm_mock_service_find_client_session(notice->sourceClientId);
        u32 leaveWireId = source ? vm_mock_service_team_member_wire_id(observer, source)
                                 : notice->sourceRoleId;
        if (leaveWireId == 0)
        {
            memset(notice, 0, sizeof(*notice));
            return 0;
        }
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 5, 7, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "id", leaveWireId))
        {
            return -1;
        }
        break;
    }

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_HSP:
    {
        vm_mock_service_client_session *source =
            vm_mock_service_find_client_session(notice->sourceClientId);
        if (source == NULL || source->onlineRoleId != notice->sourceRoleId ||
            !vm_net_mock_append_team_hsp_object(out, outCap, pos, observer, source))
        {
            memset(notice, 0, sizeof(*notice));
            return 0;
        }
        printf("[info][mock-service] team_hsp_deliver observer=%08x member=%08x/%u hp=%u/%u mp=%u/%u\n",
               observer->clientId, source->clientId, source->onlineRoleId,
               source->onlineHp, source->onlineHpMax,
               source->onlineMp, source->onlineMpMax);
        memset(notice, 0, sizeof(*notice));
        if (noticeTypeOut)
            *noticeTypeOut = noticeType;
        return 1;
    }

    case VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED:
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_REJECTED:
    {
        static const char approvedFormat[] =
            "\xc8\xeb\xb0\xef\xc9\xea\xc7\xeb\xd2\xd1\xc5\xfa\xd7\xbc\xa3\xac"
            "\xd2\xd1\xbc\xd3\xc8\xeb\xa1\xbe%s\xa1\xbf";
        static const char rejectedFormat[] =
            "\xc4\xe3\xbc\xd3\xc8\xeb\xa1\xbe%s\xa1\xbf\xb5\xc4\xc9\xea\xc7\xeb"
            "\xd2\xd1\xb1\xbb\xbe\xdc\xbe\xf8";
        char info[80];
        u8 statusType = noticeType ==
                            VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED
                            ? 1 : 2;

        memset(info, 0, sizeof(info));
        snprintf(info, sizeof(info),
                 statusType == 1 ? approvedFormat : rejectedFormat,
                 notice->guildName);
        /* HandleGuildBusinessDispatch(0x0103C50E) routes 10/36 to
         * scene_update_status_summary_from_packet(0x0103BCBA).  Type 1 shows
         * info and updates actor+104/+108/+312; type 2 reports a rejected
         * application without changing an existing guild identity. */
        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 10, 36,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "type", statusType) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "info", info) ||
            (statusType == 1 &&
             (!vm_net_mock_put_object_u32(out, outCap, pos, "id", notice->guildId) ||
              !vm_net_mock_put_object_string(out, outCap, pos, "name",
                                             notice->guildName) ||
              !vm_net_mock_put_object_u32(out, outCap, pos, "status",
                                          notice->guildStatus))))
        {
            return -1;
        }
        printf("[info][mock-service] guild_application_notice_deliver "
               "observer=%08x action=%s guild=%u name=%s status=%u response=10/36 "
               "evidence=JianghuOL.CBE:0x0103C50E+0x0103BCBA\n",
               observer->clientId,
               vm_mock_service_social_notice_name(noticeType),
               notice->guildId, notice->guildName, notice->guildStatus);
        break;
    }

    default:
        memset(notice, 0, sizeof(*notice));
        return 0;
    }

    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][mock-service] social_notice_deliver observer=%08x action=%s source=%08x/%u name=%s result=%u age_ticks=%u\n",
           observer->clientId,
           vm_mock_service_social_notice_name(noticeType),
           notice->sourceClientId,
           notice->sourceRoleId,
           notice->sourceName,
           notice->result,
           g_schedulerTick >= notice->queuedTick ? g_schedulerTick - notice->queuedTick : 0);
    memset(notice, 0, sizeof(*notice));
    if (noticeTypeOut)
        *noticeTypeOut = noticeType;
    return 1;
}

static int vm_net_mock_append_scene_sync_trade_object(
    u8 *out,
    u32 outCap,
    u32 *pos,
    vm_mock_service_client_session *observer,
    u8 *subtypeOut)
{
    vm_mock_service_trade *trade = NULL;
    int index = -1;
    u8 mask = 0;

    if (subtypeOut)
        *subtypeOut = 0;
    if (out == NULL || pos == NULL || observer == NULL)
        return -1;
    trade = vm_mock_service_trade_find_for_client(observer->clientId, &index);
    if (trade == NULL || index < 0)
        return 0;
    mask = (u8)(1u << index);
    if (trade->terminalPendingMask & mask)
    {
        u8 subtype = trade->terminalSubtype;
        u8 result = trade->terminalResult;
        if (!vm_net_mock_append_trade_terminal_object(
                out, outCap, pos, subtype, result,
                trade->finalMoney[index], &trade->receipts[index]))
        {
            return -1;
        }
        trade->terminalPendingMask &= (u8)~mask;
        if (subtypeOut)
            *subtypeOut = subtype;
        printf("[info][mock-service] trade_notice_deliver observer=%08x side=%d subtype=%u result=%u\n",
               observer->clientId, index, subtype, result);
        vm_mock_service_trade_release_if_delivered(trade);
        return 1;
    }
    if (trade->active && (trade->offerPendingMask & mask))
    {
        const vm_mock_service_trade_offer *peerOffer = &trade->offers[1 - index];
        if (!vm_net_mock_append_trade_offer_object(out, outCap, pos, peerOffer))
            return -1;
        trade->offerPendingMask &= (u8)~mask;
        if (subtypeOut)
            *subtypeOut = 6;
        printf("[info][mock-service] trade_notice_deliver observer=%08x side=%d subtype=6 items=%u money=%u\n",
               observer->clientId, index, peerOffer->itemCount, peerOffer->money);
        return 1;
    }
    return 0;
}

static u32 vm_net_mock_build_pending_team_battle_start_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer);
static u32 vm_net_mock_build_pending_team_battle_action_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer);
static u32 vm_net_mock_build_pending_duel_action_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer);
static u32 vm_net_mock_build_pending_duel_terminal_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer);

static u32 vm_net_mock_build_scene_sync_poll_response(u8 *out, u32 outCap)
{
    vm_mock_service_client_session *observer = vm_mock_service_get_active_client_session();
    vm_net_mock_scene_role_seed seeds[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX];
    const char *scene = NULL;
    u8 roleCount = 0;
    u8 objectCount = 0;
    u8 movementObjectCount = 0;
    u8 socialNoticeType = VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE;
    u32 pos = 5;
    u32 maxMovementAgeTicks = 0;
    bool needsBaseline = false;
    int chatAppend = 0;
    int tradeAppend = 0;
    int socialAppend = 0;
    u8 tradeSubtype = 0;
    u32 teamBattleResponseLen = 0;
    bool npcCatalogAppended = false;
    bool taskPromptRefreshAppended = false;

    if (out == NULL || outCap < pos || observer == NULL)
        return 0;
    /* This one-shot is deliberately checked before sceneVisiblePending.  The
     * target is only marked pending while this builder emits 30/1, so the old
     * scene remains pollable during the item/confirmation phase. */
    if (g_vm_net_mock_teleport_stone_deferred_enter_valid)
        return vm_net_mock_build_teleport_stone_deferred_enter_response(out, outCap);
    if (!observer->sceneVisibleReady || observer->sceneVisiblePending ||
        !vm_net_mock_scene_name_is_safe(observer->sceneVisibleScene))
    {
        return 0;
    }
    scene = observer->sceneVisibleScene;
    teamBattleResponseLen =
        vm_net_mock_build_pending_instance_challenge_battle_response(
            out, outCap, observer);
    if (teamBattleResponseLen != 0)
        return teamBattleResponseLen;
    teamBattleResponseLen = vm_net_mock_build_pending_team_battle_start_response(
        out, outCap, observer);
    if (teamBattleResponseLen != 0)
        return teamBattleResponseLen;
    teamBattleResponseLen = vm_net_mock_build_pending_team_battle_action_response(
        out, outCap, observer);
    if (teamBattleResponseLen != 0)
        return teamBattleResponseLen;
    teamBattleResponseLen = vm_net_mock_build_duel_start_response(
        out, outCap, observer);
    if (teamBattleResponseLen != 0)
        return teamBattleResponseLen;
    teamBattleResponseLen = vm_net_mock_build_pending_duel_action_response(
        out, outCap, observer);
    if (teamBattleResponseLen != 0)
        return teamBattleResponseLen;
    teamBattleResponseLen = vm_net_mock_build_pending_duel_terminal_response(
        out, outCap, observer);
    if (teamBattleResponseLen != 0)
        return teamBattleResponseLen;
    vm_net_mock_reset_scene_moveinfo_npc_seed_if_needed(scene);
    if (!g_vm_net_mock_scene_moveinfo_npc_seeded &&
        g_vm_net_mock_scene_moveinfo_npc_pending &&
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] != 0 &&
        vm_net_mock_scene_names_equal_loose(g_vm_net_mock_scene_moveinfo_npc_pending_scene,
                                            scene))
    {
        /*
         * Normal startup consumes this one-shot in the first scene resource or
         * task follow-up. Keep the poll path as a fallback for unusual clients
         * that reach sceneVisibleReady without sending either follow-up.
         */
        if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                           scene,
                                                           "startup-scene-sync-poll"))
            return 0;
        ++objectCount;
        npcCatalogAppended = true;
    }
    if (observer->taskPromptRefreshPending &&
        observer->taskPromptRefreshScene[0] != 0 &&
        !vm_net_mock_scene_names_equal_loose(observer->taskPromptRefreshScene,
                                             scene))
    {
        printf("[info][mock-service] task_prompt_refresh_cancel client=%08x armed_scene=%s current_scene=%s reason=scene-changed\n",
               observer->clientId, observer->taskPromptRefreshScene, scene);
        observer->taskPromptRefreshPending = false;
        observer->taskPromptRefreshScene[0] = 0;
    }
    /* 27/11 creates the NPC nodes synchronously, but its visual activation can
     * finish at the end of that network dispatch.  Re-send the two task tables
     * once on the next service poll so scene_refresh_interact_prompt_types sees
     * occupied node+319 entries.  Do not combine this retry with a poll that is
     * itself delivering 27/11, or the original race is preserved. */
    if (!npcCatalogAppended && observer->taskPromptRefreshPending &&
        observer->taskPromptRefreshScene[0] != 0 &&
        vm_net_mock_scene_names_equal_loose(observer->taskPromptRefreshScene,
                                            scene))
    {
        if (!vm_net_mock_append_taskinfo_empty1_object(out, outCap, &pos, scene) ||
            !vm_net_mock_append_taskaction14_object(out, outCap, &pos, scene))
        {
            return 0;
        }
        objectCount = (u8)(objectCount + 2);
        taskPromptRefreshAppended = true;
        observer->taskPromptRefreshPending = false;
        observer->taskPromptRefreshScene[0] = 0;
        printf("[info][mock-service] task_prompt_refresh_deliver client=%08x scene=%s objects=2 evidence=JianghuOL.CBE:0x0104726C(cases1,14)->0x01017C6C\n",
               observer->clientId, scene);
    }
    roleCount = vm_net_mock_build_scene_role_seeds(scene,
                                                   seeds,
                                                   VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX);
    for (u8 i = 0; i < roleCount; ++i)
    {
        vm_mock_service_client_session *source = seeds[i].session;
        vm_mock_service_peer_sync *peerSync = NULL;

        if (source == NULL)
            continue;
        peerSync = vm_mock_service_get_peer_sync(observer,
                                                 source->clientId,
                                                 seeds[i].actorId,
                                                 source->pendingDirQueueSerial,
                                                 false,
                                                 NULL);
        if (peerSync == NULL || !peerSync->visible)
        {
            needsBaseline = true;
            break;
        }
    }
    if (needsBaseline)
    {
        u32 nearbyRoleCount = 0;
        u32 nearbyOtherInfoLen = 0;
        u8 nearbyMoveinfoCount = 0;

        u8 addedNearbyObjects = vm_net_mock_append_scene_nearby_role_objects(out,
                                                                             outCap,
                                                                             &pos,
                                                                             scene,
                                                                             &nearbyRoleCount,
                                                                             &nearbyOtherInfoLen,
                                                                             &nearbyMoveinfoCount);
        objectCount = (u8)(objectCount + addedNearbyObjects);
        if (objectCount == 0)
            return 0;
        chatAppend = vm_net_mock_append_scene_sync_chat_objects(
            out, outCap, &pos, observer);
        if (chatAppend < 0)
            return 0;
        if (chatAppend > 0)
            objectCount = (u8)(objectCount + chatAppend);
        tradeAppend = vm_net_mock_append_scene_sync_trade_object(
            out, outCap, &pos, observer, &tradeSubtype);
        if (tradeAppend < 0)
            return 0;
        if (tradeAppend > 0)
            objectCount = (u8)(objectCount + tradeAppend);
        socialAppend = vm_net_mock_append_scene_sync_social_notice_object(
            out, outCap, &pos, observer, &socialNoticeType);
        if (socialAppend < 0)
            return 0;
        if (socialAppend > 0)
            objectCount = (u8)(objectCount + socialAppend);
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[info][mock-service] scene_sync_poll baseline observer=%08x scene=%s roles=%u moveinfo=%u npc=%u task_prompt=%u chat=%d trade=%u social=%s resp=%u evidence=IDA:0x01037998/0x01012958/0x01012A76/0x010126C6\n",
               observer->clientId,
               scene,
               nearbyRoleCount,
               nearbyMoveinfoCount,
               npcCatalogAppended ? 1u : 0u,
               taskPromptRefreshAppended ? 1u : 0u,
               chatAppend,
               tradeSubtype,
               vm_mock_service_social_notice_name(socialNoticeType),
               pos);
        return pos;
    }

    for (u8 i = 0; i < roleCount; ++i)
    {
        vm_mock_service_client_session *source = seeds[i].session;
        vm_mock_service_peer_sync *peerSync = NULL;

        if (source == NULL)
            continue;
        peerSync = vm_mock_service_get_peer_sync(observer,
                                                 source->clientId,
                                                 seeds[i].actorId,
                                                 source->pendingDirQueueSerial,
                                                 false,
                                                 NULL);
        if (peerSync == NULL || !peerSync->visible ||
            !source->pendingDirQueueValid ||
            source->pendingDirQueueSerial == 0 ||
            peerSync->lastMoveSerial == source->pendingDirQueueSerial)
        {
            continue;
        }
        if (!vm_net_mock_append_scene_player_moveinfo2_object(out, outCap, &pos, &seeds[i]))
            return 0;
        ++objectCount;
        ++movementObjectCount;
        if (g_schedulerTick >= source->pendingDirQueueTick)
        {
            u32 ageTicks = g_schedulerTick - source->pendingDirQueueTick;
            if (ageTicks > maxMovementAgeTicks)
                maxMovementAgeTicks = ageTicks;
        }
    }

    for (u32 i = 0; i < VM_MOCK_SERVICE_PEER_SYNC_MAX; ++i)
    {
        vm_mock_service_peer_sync *peerSync = &observer->peerSync[i];
        if (!peerSync->visible || peerSync->sourceClientId == 0 ||
            vm_net_mock_scene_role_seeds_contain_client(seeds, roleCount, peerSync->sourceClientId))
        {
            continue;
        }
        if (peerSync->actorId != 0)
        {
            if (!vm_net_mock_append_scene_role_remove6_object(out, outCap, &pos, peerSync->actorId))
                return 0;
            ++objectCount;
        }
        peerSync->visible = false;
    }

    chatAppend = vm_net_mock_append_scene_sync_chat_objects(
        out, outCap, &pos, observer);
    if (chatAppend < 0)
        return 0;
    if (chatAppend > 0)
        objectCount = (u8)(objectCount + chatAppend);

    tradeAppend = vm_net_mock_append_scene_sync_trade_object(
        out, outCap, &pos, observer, &tradeSubtype);
    if (tradeAppend < 0)
        return 0;
    if (tradeAppend > 0)
        objectCount = (u8)(objectCount + tradeAppend);

    socialAppend = vm_net_mock_append_scene_sync_social_notice_object(
        out, outCap, &pos, observer, &socialNoticeType);
    if (socialAppend < 0)
        return 0;
    if (socialAppend > 0)
        objectCount = (u8)(objectCount + socialAppend);

    if (objectCount == 0)
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][mock-service] scene_sync_poll delta observer=%08x scene=%s objects=%u movement=%u npc=%u task_prompt=%u chat=%d trade=%u social=%s queue_age_ticks=%u queue_age_ms=%u resp=%u evidence=IDA:0x01037998/0x01012A76/0x010126C6\n",
           observer->clientId,
           scene,
           objectCount,
           movementObjectCount,
           npcCatalogAppended ? 1u : 0u,
           taskPromptRefreshAppended ? 1u : 0u,
           chatAppend,
           tradeSubtype,
           vm_mock_service_social_notice_name(socialNoticeType),
           maxMovementAgeTicks,
           maxMovementAgeTicks * VM_SCHED_FRAME_MS,
           pos);
    return pos;
}

typedef struct
{
    u8 requestSubtype;
    u8 requestType;
    u32 sendTo;
    char message[82];
} vm_net_mock_chat_request;

static bool vm_net_mock_request_contains_chat_object(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 3 &&
            (object.subtype == 1 || object.subtype == 2))
        {
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_parse_chat_request(const u8 *request,
                                           u32 requestLen,
                                           vm_net_mock_chat_request *parsed)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (parsed)
        memset(parsed, 0, sizeof(*parsed));
    if (request == NULL || requestLen < 9 || parsed == NULL ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 3 ||
        (object.subtype != 1 && object.subtype != 2) ||
        !vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                             "data", parsed->message,
                                             sizeof(parsed->message)) ||
        parsed->message[0] == 0)
    {
        return false;
    }

    parsed->requestSubtype = object.subtype;
    if (object.subtype == 1)
    {
        if (!vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                            "type", &parsed->requestType) ||
            (parsed->requestType != VM_MOCK_CHAT_REQUEST_WORLD &&
             parsed->requestType != VM_MOCK_CHAT_REQUEST_TEAM &&
             parsed->requestType != VM_MOCK_CHAT_REQUEST_GUILD &&
             parsed->requestType != VM_MOCK_CHAT_REQUEST_LOCAL))
        {
            return false;
        }
    }
    else if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                               "sendTo", &parsed->sendTo) ||
             parsed->sendTo == 0)
    {
        return false;
    }
    return true;
}

static vm_mock_service_client_session *vm_mock_service_find_chat_target(
    vm_mock_service_client_session *source,
    u32 sendTo)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;
    vm_net_mock_scene_role_seed nearbySeed;

    if (source == NULL || sendTo == 0)
        return NULL;
    while (session != NULL)
    {
        if (session != source && session->roleOnline && session->onlinePresenceValid &&
            vm_mock_service_session_presence_is_recent(session) &&
            (session->onlineRoleId == sendTo ||
             vm_mock_service_team_member_wire_id(source, session) == sendTo))
        {
            return session;
        }
        session = session->next;
    }
    memset(&nearbySeed, 0, sizeof(nearbySeed));
    if (source->sceneVisibleReady &&
        vm_net_mock_find_nearby_role_seed_by_actor_id(source->sceneVisibleScene,
                                                      sendTo, &nearbySeed) &&
        nearbySeed.session != NULL && nearbySeed.session != source)
    {
        return nearbySeed.session;
    }
    return NULL;
}

static u8 vm_net_mock_chat_response_type(u8 requestType)
{
    switch (requestType)
    {
    case VM_MOCK_CHAT_REQUEST_WORLD:
        return VM_MOCK_CHAT_TYPE_WORLD;
    case VM_MOCK_CHAT_REQUEST_TEAM:
        return VM_MOCK_CHAT_TYPE_TEAM;
    case VM_MOCK_CHAT_REQUEST_GUILD:
        return VM_MOCK_CHAT_TYPE_GUILD;
    case VM_MOCK_CHAT_REQUEST_LOCAL:
        return VM_MOCK_CHAT_TYPE_LOCAL;
    default:
        return VM_MOCK_CHAT_TYPE_INVALID;
    }
}

static u32 vm_net_mock_build_chat_response(const u8 *request,
                                           u32 requestLen,
                                           u8 *out,
                                           u32 outCap)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    static const char teamRequiredGbk[] =
        "\xB5\xB1\xC7\xB0\xCE\xB4\xBC\xD3\xC8\xEB\xB6\xD3\xCE\xE9";
    static const char targetOfflineGbk[] =
        "\xB6\xD4\xB7\xBD\xB2\xBB\xD4\xDA\xCF\xDF";
    static const char guildUnavailableGbk[] =
        "\xB0\xEF\xC5\xC9\xCF\xFB\xCF\xA2\xD4\xDD\xCE\xB4\xBF\xAA\xB7\xC5";
    static const char worldStoreFailedGbk[] =
        "\xCA\xC0\xBD\xE7\xCF\xFB\xCF\xA2\xB7\xA2\xCB\xCD\xCA\xA7\xB0\xDC";
    vm_net_mock_chat_request chat;
    vm_mock_service_client_session *source = vm_mock_service_get_active_client_session();
    vm_mock_service_client_session *privateTarget = NULL;
    vm_mock_service_team *team = NULL;
    const char *systemMessage = NULL;
    u8 deliveryType = VM_MOCK_CHAT_TYPE_INVALID;
    char sourceName[16];
    u32 pos = 5;
    u32 recipientCount = 0;
    bool senderEcho = false;

    if (out == NULL || outCap < pos || source == NULL || source->onlineRoleId == 0 ||
        !vm_net_mock_parse_chat_request(request, requestLen, &chat))
    {
        return 0;
    }
    memset(sourceName, 0, sizeof(sourceName));
    snprintf(sourceName, sizeof(sourceName), "%s",
             source->onlineRoleName[0] ? source->onlineRoleName : "Player");

    if (chat.requestSubtype == 2)
    {
        deliveryType = VM_MOCK_CHAT_TYPE_PRIVATE;
        privateTarget = vm_mock_service_find_chat_target(source, chat.sendTo);
        if (privateTarget == NULL)
            systemMessage = targetOfflineGbk;
    }
    else
    {
        deliveryType = vm_net_mock_chat_response_type(chat.requestType);
        if (deliveryType == VM_MOCK_CHAT_TYPE_TEAM)
        {
            team = vm_mock_service_team_find_for_client(source->clientId);
            if (team == NULL)
                systemMessage = teamRequiredGbk;
        }
        else if (deliveryType == VM_MOCK_CHAT_TYPE_GUILD)
            systemMessage = guildUnavailableGbk;
    }

    if (deliveryType == VM_MOCK_CHAT_TYPE_INVALID)
    {
        return 0;
    }
    if (systemMessage == NULL && deliveryType == VM_MOCK_CHAT_TYPE_WORLD &&
        !vm_mock_world_chat_store(source, sourceName, chat.message))
    {
        systemMessage = worldStoreFailedGbk;
    }
    if (systemMessage != NULL)
    {
        if (!vm_net_mock_append_chat_message_object(out, outCap, &pos,
                                                    VM_MOCK_CHAT_TYPE_SYSTEM, 0,
                                                    systemNameGbk, systemMessage))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, 1);
    }
    else if (deliveryType == VM_MOCK_CHAT_TYPE_WORLD)
    {
        /* World chat is the small-horn path. DispatchSceneTransition
         * (0x01015FAC) sends 1/3/1 and consumes item 807, but unlike the
         * ordinary local/team paths it does not make the submitted text
         * visible in the world list. Return the authoritative 1/3/3 object in
         * the same response so a lone online player also sees the message. */
        if (!vm_net_mock_append_chat_message_object(out, outCap, &pos,
                                                    deliveryType,
                                                    source->onlineRoleId,
                                                    sourceName,
                                                    chat.message))
        {
            return 0;
        }
        senderEcho = true;
        vm_net_mock_finish_wt_packet(out, pos, 1);
    }
    else
    {
        /* Other successful channel sends are inserted by the client-side
         * submission flow; only remote recipients need a server delivery. */
        vm_net_mock_finish_wt_packet(out, pos, 0);
    }

    if (systemMessage == NULL &&
        (deliveryType == VM_MOCK_CHAT_TYPE_WORLD ||
         deliveryType == VM_MOCK_CHAT_TYPE_LOCAL ||
         deliveryType == VM_MOCK_CHAT_TYPE_TEAM))
    {
        vm_mock_service_client_session *target = g_vm_mock_service_client_sessions;
        while (target != NULL)
        {
            bool selected = false;
            if (target != source && target->roleOnline && target->onlinePresenceValid &&
                vm_mock_service_session_presence_is_recent(target))
            {
                if (deliveryType == VM_MOCK_CHAT_TYPE_WORLD)
                {
                    selected = true;
                }
                else if (deliveryType == VM_MOCK_CHAT_TYPE_LOCAL &&
                         source->sceneVisibleReady &&
                         vm_mock_service_session_scene_is_visible(
                             target, source->sceneVisibleScene))
                {
                    selected = true;
                }
                else if (deliveryType == VM_MOCK_CHAT_TYPE_TEAM && team != NULL &&
                         vm_mock_service_team_contains_client(team, target->clientId))
                {
                    selected = true;
                }
            }
            if (selected && vm_mock_service_session_enqueue_chat_notice(
                                target, deliveryType, source, sourceName, chat.message))
            {
                ++recipientCount;
            }
            target = target->next;
        }
    }
    else if (systemMessage == NULL && deliveryType == VM_MOCK_CHAT_TYPE_PRIVATE &&
             privateTarget != NULL &&
             vm_mock_service_session_enqueue_chat_notice(
                 privateTarget, deliveryType, source, sourceName, chat.message))
    {
        recipientCount = 1;
    }

    printf("[info][network] mock_chat_send source=%08x/%u request=%u/%u delivery_type=%s ack=%s sender_echo=%u send_to=%u recipients=%u bytes=%u resp=%u evidence=JianghuOL.CBE:0x01015FAC/0x010126C6\n",
           source->clientId,
           source->onlineRoleId,
           chat.requestSubtype,
           chat.requestType,
           vm_mock_service_chat_type_name(deliveryType),
           systemMessage != NULL ? "system-error" :
               (senderEcho ? "server-echo" : "empty"),
           senderEcho ? 1u : 0u,
           chat.sendTo,
           recipientCount,
           (u32)strlen(chat.message),
           pos);
    return pos;
}

static bool vm_net_mock_select_scene_actor_moveinfo_target(u32 actorId,
                                                           u32 *indexOut,
                                                           u32 *posxOut,
                                                           u32 *posyOut)
{
#ifdef CBE_SERVER_ONLY
    /* The authoritative service has no emulator-local scene-node memory. Use
     * the real SCE2 combat-spawn catalog that created those client nodes. */
    return vm_net_mock_select_sce_combat_spawn(vm_net_mock_current_scene_name(),
                                               actorId,
                                               indexOut, posxOut, posyOut);
#else
    u32 sceneNodeBase = 0;

    if (actorId == 0 || Global_R9 == 0)
    {
        return vm_net_mock_select_sce_combat_spawn(vm_net_mock_current_scene_name(),
                                                   actorId,
                                                   indexOut, posxOut, posyOut);
    }
    if (uc_mem_read(MTK, Global_R9 + 0x5CB0, &sceneNodeBase, sizeof(sceneNodeBase)) != UC_ERR_OK ||
        sceneNodeBase == 0)
    {
        return vm_net_mock_select_sce_combat_spawn(vm_net_mock_current_scene_name(),
                                                   actorId,
                                                   indexOut, posxOut, posyOut);
    }

    for (u32 i = 0; i < 25; ++i)
    {
        u32 node = sceneNodeBase + i * 340u;
        u32 nodeActorId = 0;
        u32 nodePosX = 0;
        u32 nodePosY = 0;
        u8 active = 0;
        if (uc_mem_read(MTK, node + 319, &active, sizeof(active)) != UC_ERR_OK || active == 0)
            continue;
        if (uc_mem_read(MTK, node + 100, &nodeActorId, sizeof(nodeActorId)) != UC_ERR_OK ||
            nodeActorId != actorId)
            continue;
        (void)uc_mem_read(MTK, node + 240, &nodePosX, sizeof(nodePosX));
        (void)uc_mem_read(MTK, node + 244, &nodePosY, sizeof(nodePosY));
        if (indexOut)
            *indexOut = i;
        if (posxOut)
            *posxOut = nodePosX;
        if (posyOut)
            *posyOut = nodePosY;
        return true;
    }
    return vm_net_mock_select_sce_combat_spawn(vm_net_mock_current_scene_name(),
                                               actorId,
                                               indexOut, posxOut, posyOut);
#endif
}

static bool vm_net_mock_snapshot_current_player_pos(const char *reason)
{
    u32 currentSceneNode = 0;
    u32 actorId = 0;
    u16 gridX = 0;
    u16 gridY = 0;
    u16 targetX = 0;
    u16 targetY = 0;

    if (!vm_net_mock_read_current_player_grid(&currentSceneNode, &actorId,
                                              &gridX, &gridY, &targetX, &targetY))
        return false;

    vm_net_mock_save_player_pos_state(NULL, gridX, gridY, reason);
    return true;
}

static bool vm_net_mock_parse_actor_moveinfo_pos(const u8 *moveInfo,
                                                 u16 moveInfoLen,
                                                 u16 *xOut,
                                                 u16 *yOut)
{
    u16 x = 0;
    u16 y = 0;

    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (moveInfo == NULL || moveInfoLen < 8)
        return false;
    if (moveInfo[0] != 0 || moveInfo[1] != 2 ||
        moveInfo[4] != 0 || moveInfo[5] != 2)
    {
        return false;
    }
    x = vm_net_mock_read_be16_at(moveInfo, 2);
    y = vm_net_mock_read_be16_at(moveInfo, 6);
    if (x == 0 || y == 0)
        return false;
    if (xOut)
        *xOut = x;
    if (yOut)
        *yOut = y;
    return true;
}
static u32 vm_net_mock_build_actor_moveinfo_ack_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 timingStartMs = scheduler_get_tick_ms();
    u32 timingFieldMs = timingStartMs;
    u32 timingPositionMs = timingStartMs;
    u32 timingSessionMs = timingStartMs;
    u32 pos = 5;
    u8 objectCount = 0;
    const u8 *moveInfo = NULL;
    u16 moveInfoLen = 0;
    bool snappedPos = false;
    const char *scene = NULL;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u16 gridX = 0;
    u16 gridY = 0;
    u16 uploadedX = 0;
    u16 uploadedY = 0;
    bool parsedUploadedPos = false;
    const char *posSource = "none";
    vm_mock_service_client_session *activeSession = vm_mock_service_get_active_client_session();
    bool usedTimeline = false;
    u16 timelineStartX = 0;
    u16 timelineStartY = 0;
    char timelineText[64];
    const char *moveinfoFieldKind = "missing";

    if (outCap < pos || !vm_net_mock_is_actor_moveinfo_upload_request(request, requestLen))
        return 0;
    timelineText[0] = 0;
    if (vm_net_mock_get_object_blob_field(request, requestLen, "moveinfo", &moveInfo, &moveInfoLen))
    {
        moveinfoFieldKind = "blob";
    }
    else if (vm_net_mock_get_object_entry_field(request, requestLen, "moveinfo", &moveInfo, &moveInfoLen))
    {
        moveinfoFieldKind = "entry";
    }
    timingFieldMs = scheduler_get_tick_ms();
    parsedUploadedPos = vm_net_mock_parse_actor_moveinfo_pos(moveInfo, moveInfoLen,
                                                             &uploadedX, &uploadedY);
    if (parsedUploadedPos &&
        role != NULL &&
        vm_net_mock_scene_name_is_safe(role->scene))
    {
        scene = role->scene;
        gridX = uploadedX;
        gridY = uploadedY;
        vm_net_mock_remember_moveinfo_source_pos(scene, gridX, gridY, "moveinfo-upload-packet");
        vm_net_mock_save_player_pos_state(scene, gridX, gridY, "moveinfo-upload-packet");
        snappedPos = true;
        posSource = "packet";
    }
    else
    {
        scene = (role != NULL && vm_net_mock_scene_name_is_safe(role->scene)) ?
                role->scene : vm_net_mock_current_scene_name();
        if (vm_net_mock_is_actor_moveinfo_timeline(moveInfo, moveInfoLen))
        {
            if (activeSession != NULL &&
                activeSession->sceneVisibleReady &&
                !activeSession->sceneVisiblePending &&
                activeSession->sceneVisibleX != 0 &&
                activeSession->sceneVisibleY != 0)
            {
                gridX = activeSession->sceneVisibleX;
                gridY = activeSession->sceneVisibleY;
            }
            else if (role != NULL && role->x != 0 && role->y != 0)
            {
                gridX = role->x;
                gridY = role->y;
            }
            else
            {
                gridX = vm_net_mock_scene_spawn_x();
                gridY = vm_net_mock_scene_spawn_y();
            }
            timelineStartX = gridX;
            timelineStartY = gridY;
            vm_net_mock_apply_actor_moveinfo_timeline(&gridX, &gridY, moveInfo, moveInfoLen);
            snappedPos = vm_net_mock_scene_name_is_safe(scene) && gridX != 0 && gridY != 0;
            posSource = "timeline";
            usedTimeline = snappedPos;
            vm_net_mock_format_moveinfo_timeline(moveInfo, moveInfoLen, timelineText, sizeof(timelineText));
            if (snappedPos)
            {
                vm_net_mock_role_set_timeline_position(scene, gridX, gridY, "moveinfo-upload-timeline");
                vm_net_mock_remember_moveinfo_source_pos(scene, gridX, gridY, "moveinfo-upload-timeline");
                vm_mock_service_session_store_pending_timeline(activeSession,
                                                               moveInfo,
                                                               moveInfoLen,
                                                               timelineStartX,
                                                               timelineStartY,
                                                               gridX,
                                                               gridY);
            }
        }
        else if (vm_net_mock_read_current_player_grid(NULL, NULL, &gridX, &gridY, NULL, NULL))
        {
            snappedPos = true;
            posSource = "runtime-grid";
            if (vm_net_mock_scene_name_is_safe(scene))
            {
                vm_net_mock_save_player_pos_state(scene, gridX, gridY, "moveinfo-upload-runtime-grid");
                vm_net_mock_remember_moveinfo_source_pos(scene, gridX, gridY, "moveinfo-upload-runtime-grid");
            }
        }
        else if (activeSession != NULL &&
                 activeSession->sceneVisibleReady &&
                 !activeSession->sceneVisiblePending &&
                 vm_net_mock_scene_name_is_safe(activeSession->sceneVisibleScene) &&
                 vm_net_mock_scene_names_equal_loose(activeSession->sceneVisibleScene, scene) &&
                 activeSession->sceneVisibleX != 0 &&
                 activeSession->sceneVisibleY != 0)
        {
            gridX = activeSession->sceneVisibleX;
            gridY = activeSession->sceneVisibleY;
            snappedPos = true;
            posSource = "session-visible";
            vm_net_mock_save_player_pos_state(scene, gridX, gridY, "moveinfo-upload-session-visible");
            vm_net_mock_remember_moveinfo_source_pos(scene, gridX, gridY, "moveinfo-upload-session-visible");
        }
        else
        {
            gridX = vm_net_mock_scene_spawn_x();
            gridY = vm_net_mock_scene_spawn_y();
            snappedPos = vm_net_mock_scene_name_is_safe(scene) && gridX != 0 && gridY != 0;
            posSource = "spawn";
            if (snappedPos)
            {
                vm_net_mock_save_player_pos_state(scene, gridX, gridY, "moveinfo-upload-runtime-spawn");
                vm_net_mock_remember_moveinfo_source_pos(scene, gridX, gridY, "moveinfo-upload-runtime-spawn");
            }
        }
    }
    timingPositionMs = scheduler_get_tick_ms();
    vm_net_mock_reset_scene_moveinfo_npc_seed_if_needed(scene);
    if (vm_net_mock_scene_name_is_safe(scene) && gridX != 0 && gridY != 0)
    {
        vm_mock_service_session_update_move_position(activeSession, scene, gridX, gridY);
        vm_mock_service_session_store_moveinfo(activeSession,
                                               scene,
                                               moveInfo,
                                               moveInfoLen,
                                               gridX,
                                               gridY,
                                               posSource);
    }
    timingSessionMs = scheduler_get_tick_ms();

    /*
     * Fallback only. Runtime evidence showed that post-scene 2/1 moveinfo arrives
     * after 30/1 has closed sceneObj+0x164, so the normal business dispatcher
     * decodes this packet into the shared container but does not enter the
     * kind=2/subtype=10 actor parser. The default scene follow-up now seeds
     * NPC otherinfo while dispatchGate is still open; keep this path gated by
     * pending+not-seeded for rollback experiments and negative evidence.
     */
    if (!g_vm_net_mock_scene_moveinfo_npc_seeded &&
        vm_net_mock_is_scene_moveinfo_npc_seed_request(scene, moveInfo, moveInfoLen) &&
        vm_net_mock_scene_supports_actor_other_npc_seed(scene))
    {
        u8 objectCount = 0;
        u32 npcCount = 0;
        u32 otherInfoLen = 0;

        if (!vm_net_mock_append_actor_other_scene_npcs10_object(out, outCap, &pos,
                                                                scene, false,
                                                                &npcCount, &otherInfoLen))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        g_vm_net_mock_scene_moveinfo_npc_pending = false;
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
        g_vm_net_mock_scene_moveinfo_npc_seeded = true;
        snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                 sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene),
                 "%s", scene ? scene : "");
        return pos;
    }

    /*
     * net_handle_actor_move_info() only reads moveinfo for subtype 2. Subtype 1
     * falls through the default branch at 0x01012B0C, so keep this upload ack
     * empty until the live server semantics are recovered.
     */
    if (!vm_net_mock_append_actor_moveinfo_empty_ack_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    printf("[info][network] mock_actor_moveinfo_ack source=%s field=%s len=%u uploaded=%u timeline=%u steps=%s pos=(%u,%u) nearby_delivery=scene-sync-poll resp=%u scene=%s\n",
           posSource,
           moveinfoFieldKind,
           (u32)moveInfoLen,
           parsedUploadedPos ? 1u : 0u,
           usedTimeline ? 1u : 0u,
           timelineText[0] ? timelineText : "-",
           gridX,
           gridY,
           pos,
           scene ? scene : "-");
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    {
        u32 timingEndMs = scheduler_get_tick_ms();
        if (timingEndMs - timingStartMs >= 50)
        {
            printf("[warn][network] actor_moveinfo_stage field_ms=%u position_ms=%u session_ms=%u finish_ms=%u total_ms=%u source=%s scene=%s\n",
                   timingFieldMs - timingStartMs,
                   timingPositionMs - timingFieldMs,
                   timingSessionMs - timingPositionMs,
                   timingEndMs - timingSessionMs,
                   timingEndMs - timingStartMs,
                   posSource,
                   scene ? scene : "-");
        }
    }
    return pos;
}

static bool vm_net_mock_is_challenge_interaction_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    bool hasChallenge = false;
    bool hasMoveinfo = false;

    if (request == NULL || requestLen < 9)
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 4 && object.subtype == 1)
        {
            if (hasChallenge)
                return false;
            hasChallenge = true;
        }
        else if (object.major == 1 && object.kind == 2 && object.subtype == 1)
        {
            if (hasMoveinfo)
                return false;
            hasMoveinfo = true;
        }
        else
        {
            return false;
        }
    }
    return offset == requestLen &&
           hasChallenge &&
           vm_net_mock_request_contains(request, requestLen, "id") &&
           vm_net_mock_request_contains(request, requestLen, "index") &&
           vm_net_mock_request_contains(request, requestLen, "posx") &&
           vm_net_mock_request_contains(request, requestLen, "posy");
}

static u32 vm_net_mock_build_battle_start_info_blob(u8 *out, u32 outCap,
                                                    u32 roleId, u32 enemyId,
                                                    bool playerOnRight)
{
    u32 pos = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();
    const char *leftName = vm_net_mock_env_str("CBE_BATTLE_LEFT_NAME", "Monster");
    u32 roleIdDefault = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleHp = 0;
    u32 roleMaxHp = 0;
    u32 roleMp = 0;
    u32 roleMaxMp = 0;
    vm_net_mock_monster_stats enemyStats;
    u32 enemyHp = 0;
    u32 enemyMaxHp = 0;
    u32 enemyMp = 0;
    u32 enemyMaxMp = 0;
    u32 leftId = 0;
    u32 leftHp = 0;
    u32 leftMaxHp = 0;
    u32 leftMp = 0;
    u32 leftMaxMp = 0;
    u32 rightId = 0;
    u32 rightHp = 0;
    u32 rightMaxHp = 0;
    u32 rightMp = 0;
    u32 rightMaxMp = 0;
    u8 leftVisual0 = vm_net_mock_env_u8("CBE_BATTLE_LEFT_VISUAL_BYTE0", 0);
    u8 leftVisual1 = vm_net_mock_env_u8("CBE_BATTLE_LEFT_VISUAL_BYTE1", 1);

    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    roleHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", roleHpDefault);
    roleMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", roleMaxHpDefault);
    roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", roleMpDefault);
    roleMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_MP", roleMaxMpDefault);
    if (roleId == 0)
        roleId = roleIdDefault;
    if (roleMaxHp < roleMaxHpDefault)
        roleMaxHp = roleMaxHpDefault;
    if (roleMaxMp < roleMaxMpDefault)
        roleMaxMp = roleMaxMpDefault;
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;
    if (enemyId == 0)
        enemyId = VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
    enemyStats = vm_net_mock_monster_stats_for_enemy(enemyId);
    enemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", enemyStats.hp);
    enemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", enemyHp);
    enemyMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", enemyStats.mp);
    enemyMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_MP", enemyMp);
    if (enemyMaxHp < enemyHp)
        enemyMaxHp = enemyHp;
    if (enemyMaxMp < enemyMp)
        enemyMaxMp = enemyMp;
    leftId = playerOnRight ? enemyId : roleId;
    leftHp = playerOnRight ? enemyHp : roleHp;
    leftMaxHp = playerOnRight ? enemyMaxHp : roleMaxHp;
    leftMp = playerOnRight ? enemyMp : roleMp;
    leftMaxMp = playerOnRight ? enemyMaxMp : roleMaxMp;
    rightId = playerOnRight ? roleId : enemyId;
    rightHp = playerOnRight ? roleHp : enemyHp;
    rightMaxHp = playerOnRight ? roleMaxHp : enemyMaxHp;
    rightMp = playerOnRight ? roleMp : enemyMp;
    rightMaxMp = playerOnRight ? roleMaxMp : enemyMaxMp;

    /*
     * Battle.cbm HandleBattleStartMsg(0x66CC) places the first count at
     * x=40 and the second count at x=200. Keep the default player-on-right
     * layout by sending the monster as the left record and the role id as the
     * right record; sub_6C5E() then finds the current controllable fighter by
     * matching the role id, so input follows the right-side sprite.
     *
     * The first record's visual bytes are passed as sub_23F6(second, first).
     * Defaults 0/1 preserve the existing non-crashing left-side art.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftId))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftMaxHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftMp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, leftMaxMp))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, playerOnRight ? leftName : roleName))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, leftVisual0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, leftVisual1))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightId))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightMaxHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightMp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rightMaxMp))
        return 0;
    return pos;
}

