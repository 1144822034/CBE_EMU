static bool vm_net_mock_scene_runtime_pending_without_target(void);
static u8 vm_net_mock_append_scene_role_moveinfo2_objects(u8 *out, u32 outCap, u32 *pos,
                                                          const char *scene);
static bool vm_net_mock_append_scene_role_remove6_object(u8 *out, u32 outCap, u32 *pos,
                                                         u32 actorId);
static u8 vm_net_mock_append_scene_nearby_role_objects(u8 *out, u32 outCap, u32 *pos,
                                                       const char *scene,
                                                       u32 *roleCountOut,
                                                       u32 *otherInfoLenOut,
                                                       u8 *moveinfoCountOut);
static bool vm_net_mock_is_actor_moveinfo_timeline(const u8 *moveInfo, u16 moveInfoLen);
static void vm_net_mock_apply_actor_moveinfo_timeline(u16 *x, u16 *y,
                                                      const u8 *moveInfo, u16 moveInfoLen);
static void vm_net_mock_format_moveinfo_timeline(const u8 *moveInfo, u16 moveInfoLen,
                                                 char *out, u32 outCap);
static bool vm_net_mock_active_client_scene_ready_for_nearby(const char *scene)
{
    const vm_mock_service_client_session *session = NULL;

    if (g_vm_mock_service_active_client_id != 0)
    {
        session = vm_mock_service_find_client_session(g_vm_mock_service_active_client_id);
        if (session != NULL)
            return vm_mock_service_session_scene_is_visible(session, scene);
    }
    return !g_vm_net_mock_last_scene_change_target_valid &&
           !vm_net_mock_scene_runtime_pending_without_target();
}

static bool vm_net_mock_is_scene_ctrl_page_request(const u8 *request, u32 requestLen, u32 *pageOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 page = 0;
    u8 page8 = 0;

    if (pageOut)
        *pageOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        if (object.major == 1 && object.kind == 2 && object.subtype == 7)
        {
            /* InitSceneCtrlState writes pgIdx through the event's u8 setter. */
            if (!vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "pgIdx", &page8))
                (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "pgIdx", &page);
            else
                page = page8;
            if (pageOut)
                *pageOut = page;
            return true;
        }
    }
    return false;
}

static u32 vm_net_mock_build_scene_ctrl_page_response(const u8 *request, u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    u8 otherInfo[512];
    u32 pos = 5;
    u32 objectStart = 0;
    u32 page = 0;
    u32 nearbyRoleNum = 0;
    u32 otherInfoLen = 0;
    u8 objectCount = 0;
    bool allowNearbyList = false;
    bool appendedNearbyList = false;
    bool promotedSceneReady = false;
    const char *scene = NULL;

    if (outCap < pos || !vm_net_mock_is_scene_ctrl_page_request(request, requestLen, &page))
        return 0;

    scene = vm_net_mock_current_scene_name();
    memset(otherInfo, 0, sizeof(otherInfo));
    /*
     * InitSceneCtrlState sends 2/7 only after the current map UI exists.  If
     * the normal post-enter task subset did not run (for example the user
     * opened the page before making a move), this request is concrete packet
     * evidence that the active role is observing its saved scene.  Promote
     * before constructing this answer so the first page open can contain its
     * already-visible peers.
     */
    if (page == 0 &&
        !vm_net_mock_active_client_scene_ready_for_nearby(scene) &&
        !g_vm_net_mock_last_scene_change_target_valid &&
        !vm_net_mock_scene_runtime_pending_without_target())
    {
        promotedSceneReady = vm_mock_service_mark_active_session_scene_ready_from_role(
            scene,
            "scene-ctrl-page");
    }
    /*
     * HandleFactionOtherInfoResponse(0x01031162) owns the 2/7 page.  It
     * reads allpgs, othernum and otherinfo from this very object; 30/7 is a
     * distinct task-hall room table and never reaches the page cache.
     */
    allowNearbyList = page == 0 && vm_net_mock_active_client_scene_ready_for_nearby(scene);
    if (allowNearbyList)
    {
        if (!vm_net_mock_build_scene_list_otherinfo_blob(scene,
                                                         otherInfo, sizeof(otherInfo),
                                                         &otherInfoLen, &nearbyRoleNum))
        {
            return 0;
        }
        appendedNearbyList = true;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 7, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "pgIdx", page))
        return 0;
    /* LookupItemByteField (+0x4C) consumes allpgs in the 2/7 handler. */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "allpgs", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "othernum", nearbyRoleNum) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "otherinfo",
                                    otherInfo, (u16)otherInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_scene_ctrl_page pgIdx=%u scene=%s nearby_roles=%u otherinfo_len=%u allow_nearby_list=%u appended_nearby_list=%u promoted_ready=%u pending_target=%u pending_runtime=%u resp=%u\n",
           page,
           scene ? scene : "-",
           nearbyRoleNum,
           otherInfoLen,
           allowNearbyList ? 1u : 0u,
           appendedNearbyList ? 1u : 0u,
           promotedSceneReady ? 1u : 0u,
           g_vm_net_mock_last_scene_change_target_valid ? 1u : 0u,
           vm_net_mock_scene_runtime_pending_without_target() ? 1u : 0u,
           pos);
    vm_autotest_note("mock_scene_ctrl_page pgIdx=%u nearby_roles=%u otherinfo_len=%u allow_nearby_list=%u appended_nearby_list=%u response=2/7-otherinfo evidence=JianghuOL.CBE:0x0103014C + 0x01031162\n",
                     page,
                     nearbyRoleNum,
                     otherInfoLen,
                     allowNearbyList ? 1u : 0u,
                     appendedNearbyList ? 1u : 0u);
    return pos;
}

static bool vm_net_mock_append_misc_player_sync8_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    static const u8 seqOne[] = {0x00, 0x02, 0x00, 0x01};
    /*
     * Unsafe negative-evidence experiment; disabled by default.  The client's
     * 7/8 type=4 success handler removes the object referenced by its local
     * pending-unequip pointer (R9+38020).  A login push has no such pointer and
     * must never be treated as an equipment-list bootstrap.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 8, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 4))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "seq", seqOne, sizeof(seqOne)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_misc_player_type_object(u8 *out, u32 outCap, u32 *pos, u8 subtype)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (subtype == 20)
    {
        if (!vm_net_mock_put_object_u8(
                out, outCap, pos, "pcimg",
                vm_net_mock_scene_timed_item_status_for_active_effects(
                    false, false, false)
                    .pcimg))
            return false;
    }
    else if (subtype == 32)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard",
                                       vm_net_mock_role_active_exp_card_flag()))
            return false;
    }
    else
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_enter_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                            const char *sceneName, u16 spawnX, u16 spawnY)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, pos, false, false, 0,
                                           sceneName, spawnX, spawnY))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    vm_net_mock_mark_scene_moveinfo_npc_seed_pending(sceneName);
    return true;
}

static bool vm_net_mock_append_scene_pos_result_object_for_scene(u8 *out, u32 outCap, u32 *pos,
                                                                 const char *sceneName, u16 spawnX, u16 spawnY)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 2, &objectStart))
        return false;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, pos, true, true, 2,
                                           sceneName, spawnX, spawnY))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_enter_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_scene_enter_object_for_scene(out, outCap, pos,
                                                          vm_net_mock_scene_key_name(),
                                                          vm_net_mock_scene_spawn_x(),
                                                          vm_net_mock_scene_spawn_y());
}

static bool vm_net_mock_scene_is_penglai01(const char *scene);
static bool vm_net_mock_scene_is_penglai02(const char *scene);
static bool vm_net_mock_scene_is_penglai03(const char *scene);
static bool vm_net_mock_scene_is_penglai04(const char *scene);
static bool vm_net_mock_scene_is_penglai_transfer_scene(const char *scene);
static bool vm_net_mock_scene_supports_actor_other_npc_seed(const char *scene);

static u8 vm_net_mock_scene_room_npc_seed_count(const char *scene)
{
    vm_net_mock_scene_npcinfo_seed seeds[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    u32 total = 0;
    u32 dynamic = 0;
    u32 count = 0;

    /* The scene bootstrap asks for the count, then the 27/11 blob, then task
     * prompt candidates, and finally the count again for optional probes.
     * During that one request, select once and reuse the exact same stable
     * four-row catalog instead of decoding the SCE on every check. */
    if (g_vm_net_mock_scene_npc_request_cache.active)
    {
        count = vm_net_mock_select_scene_npcinfo_seeds(
            scene, seeds, VM_NET_MOCK_SCENE_NPCINFO_MAX, &total, &dynamic);
    }
    else
    {
        count = vm_net_mock_collect_scene_npcinfo_seeds(
            scene, seeds, VM_NET_MOCK_SCENE_NPCINFO_MAX, &total, &dynamic);
    }
    (void)total;
    (void)dynamic;
    return (u8)count;
}

bool vm_net_mock_build_scene_npcinfo_blob(
    const char *scene, u8 *npcInfo, u32 npcInfoCap,
    u8 *npcNumOut, u32 *npcInfoLenOut)
{
    vm_net_mock_scene_npcinfo_seed selectedSeeds[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    u32 selectedCount = 0;
    u32 totalCount = 0;
    u32 dynamicCount = 0;
    u32 npcInfoLen = 0;
    const char *catalogSource = "sce";

    if (npcNumOut)
        *npcNumOut = 0;
    if (npcInfoLenOut)
        *npcInfoLenOut = 0;
    if (npcInfo == NULL || npcInfoCap == 0)
        return false;

    memset(npcInfo, 0, npcInfoCap);
    memset(selectedSeeds, 0, sizeof(selectedSeeds));
    selectedCount = vm_net_mock_select_scene_npcinfo_seeds(
        scene, selectedSeeds, VM_NET_MOCK_SCENE_NPCINFO_MAX,
        &totalCount, &dynamicCount);
    if (dynamicCount != 0)
    {
        u32 deliverCount = 0;
        for (u32 i = 0; i < selectedCount; ++i)
        {
            const char *publishError = NULL;
            /* A service-side catalog may contain both Web/MySQL rows and
             * built-in companions (for example the Penglai blacksmith and
             * monkey). Validate every selected row as one dependency set so
             * a clean client can load the whole catalog through WT 18/7. */
            if (!vm_net_mock_ensure_actor_resource_available(
                    selectedSeeds[i].actorResource, &publishError))
            {
                printf("[error][network] mock_scene_npc_resource_invalid scene=%s actor=%u resource=%s reason=%s action=skip-row\n",
                       scene ? scene : "-", selectedSeeds[i].actorId,
                       selectedSeeds[i].actorResource,
                       publishError ? publishError : "unknown");
                continue;
            }
            if (deliverCount != i)
                selectedSeeds[deliverCount] = selectedSeeds[i];
            ++deliverCount;
        }
        selectedCount = deliverCount;
    }
    for (u32 i = 0; i < selectedCount; ++i)
    {
        const vm_net_mock_scene_npcinfo_seed *seed = &selectedSeeds[i];

        /* scene_parse_npcinfo_and_spawn_npcs(0x01037998):
         * row id, x, y, visible name, actor resource, script metadata,
         * dynamic actor-resource key, final actor id. The fourth string is
         * registered into the scene node's visual slot. The emulator resolves
         * a missing published Actor/GIF through WT 18/7 before returning the
         * file-open failure to this parser, so all catalog rows are safe to
         * deliver in the initial scene lifecycle.  There is deliberately no
         * orientation field: the client parser has no such slot. */
        if (!vm_net_mock_seq_put_u32(npcInfo, npcInfoCap, &npcInfoLen, seed->actorId) ||
            !vm_net_mock_seq_put_u32(npcInfo, npcInfoCap, &npcInfoLen, seed->x) ||
            !vm_net_mock_seq_put_u32(npcInfo, npcInfoCap, &npcInfoLen, seed->y) ||
            !vm_net_mock_seq_put_string(npcInfo, npcInfoCap, &npcInfoLen, seed->displayName) ||
            !vm_net_mock_seq_put_string(npcInfo, npcInfoCap, &npcInfoLen, seed->actorResource) ||
            !vm_net_mock_seq_put_string(npcInfo, npcInfoCap, &npcInfoLen, seed->scriptName) ||
            !vm_net_mock_seq_put_string(npcInfo, npcInfoCap, &npcInfoLen, seed->actorResource) ||
            !vm_net_mock_seq_put_u32(npcInfo, npcInfoCap, &npcInfoLen, seed->actorId))
        {
            return false;
        }
    }
    if (npcNumOut)
        *npcNumOut = (u8)selectedCount;
    if (npcInfoLenOut)
        *npcInfoLenOut = npcInfoLen;
    {
        vm_mock_service_client_session *session =
            vm_mock_service_get_active_client_session();

        /* This is the authoritative, post-filtering count: every selected
         * row above has both been encoded and accepted for initial delivery.
         * A kind-3 SCE battle record must use this same count when resolving
         * its live index for a later 4/5 start. */
        if (session != NULL && vm_net_mock_scene_name_is_safe(scene))
        {
            session->sceneNpcNodeCount = (u8)selectedCount;
            snprintf(session->sceneNpcNodeScene,
                     sizeof(session->sceneNpcNodeScene), "%s", scene);
        }
    }
    if (dynamicCount != 0)
        catalogSource = totalCount > dynamicCount ? "sce+service-dynamic" : "service-dynamic";
    printf("[info][network] mock_scene_npc_catalog scene=%s source=%s delivery=initial actors=%u selected=%u rows=%u dynamic=%u truncated=%u npcinfo_len=%u resource=client-file-miss-WT18/7 evidence=JianghuOL.CBE:0x01037998+0x01044E48\n",
           scene ? scene : "-", catalogSource,
           totalCount, selectedCount, selectedCount,
           dynamicCount,
           totalCount > selectedCount ? totalCount - selectedCount : 0,
           npcInfoLen);
    return true;
}

static bool vm_net_mock_is_npc_dialog_request(const u8 *request, u32 requestLen,
                                              u32 *actorIdOut, u32 *indexOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 requestType = 0;
    u32 actorId = 0;
    u32 index = 0;
    u32 posX = 0;
    u32 posY = 0;

    if (actorIdOut)
        *actorIdOut = 0;
    if (indexOut)
        *indexOut = 0;
    if (request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' || request[4] != 1)
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 26 || object.subtype != 1)
    {
        return false;
    }
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen)
    {
        return false;
    }

    if (!vm_net_mock_get_object_u8_field(request + 9, requestLen - 9,
                                         "type", &requestType) ||
        requestType != 1 ||
        !vm_net_mock_get_object_number_field(request + 9, requestLen - 9,
                                             "id", &actorId) ||
        actorId == 0)
    {
        return false;
    }
    /* SendNPCInteractReq writes index/posx/posy too, but the WT writer omits
     * zero-valued fields. The observed index-0 request therefore contains only
     * type and id. Keep those two as the required discriminator and parse the
     * optional values only when the writer kept them. */
    (void)vm_net_mock_get_object_number_field(request + 9, requestLen - 9,
                                              "index", &index);
    (void)vm_net_mock_get_object_number_field(request + 9, requestLen - 9,
                                              "posx", &posX);
    (void)vm_net_mock_get_object_number_field(request + 9, requestLen - 9,
                                              "posy", &posY);
    if (actorIdOut)
        *actorIdOut = actorId;
    if (indexOut)
        *indexOut = index;
    return true;
}

enum
{
    VM_NET_MOCK_TASK_CATALOG_MAX = 256,
    VM_NET_MOCK_XSE_TASK_REF_MAX = 8,
    VM_NET_MOCK_XSE_DIRECT_DIALOG_MAX = 16
};

typedef struct
{
    u32 itemId;
    u32 count;
    u8 itemType;
} vm_net_mock_task_reward_item;

typedef struct
{
    u32 taskId;
    bool enabled;
    bool builtin;
    bool overridden;
    u8 level;
    u8 difficulty;
    u8 classification;
    u8 requirementType1;
    u8 requirementCount1;
    u8 requirementType2;
    u8 requirementCount2;
    u32 requirementId1;
    u32 requirementId2;
    /* An optional scene key makes a type-2 objective refer to the native
     * SCE battle node in one exact scene instead of every monster template
     * sharing the same numeric id.  Empty retains task.dsh compatibility. */
    char requirementScene1[64];
    char requirementScene2[64];
    u32 prerequisiteTaskId;
    u32 givenItemId;
    u32 givenItemCount;
    u32 rewardExp;
    u32 rewardMoney;
    u32 rewardItemId;
    u32 rewardItemCount;
    u8 rewardItemType;
    /* The legacy fields above remain the first reward for task.dsh and
     * server_tasks compatibility.  New MySQL reward rows are authoritative
     * when present and are serialized as the native multi-row 6/4 awardinfo
     * sequence consumed by the task callback. */
    u8 rewardItemNum;
    vm_net_mock_task_reward_item
        rewardItems[VM_NET_MOCK_TASK_REWARD_ITEM_MAX];
    bool rewardItemsOverridden;
    char name[32];
    /* Preserve the task.dsh name as an XSE marker alias when the admin edits
     * the player-facing title of a built-in task. */
    char sourceName[32];
    char giver[16];
    char receiver[16];
    char goal[96];
    char rewardText[32];
    char offerDialog[256];
    char activeDialog[256];
    char completedDialog[256];
} vm_net_mock_task_definition;

typedef struct
{
    u32 taskId;
    bool offer;
    bool active;
    bool completed;
} vm_net_mock_xse_task_ref;

typedef struct
{
    bool loaded;
    u32 stringCount;
    u32 taskRefCount;
    u32 directDialogCount;
    char offerDialog[256];
    char completedDialog[256];
    char idleDialog[256];
    vm_net_mock_xse_task_ref taskRefs[VM_NET_MOCK_XSE_TASK_REF_MAX];
} vm_net_mock_xse_summary;

static vm_net_mock_task_definition g_vm_net_mock_task_catalog[VM_NET_MOCK_TASK_CATALOG_MAX];
static u32 g_vm_net_mock_task_catalog_count = 0;
static bool g_vm_net_mock_task_catalog_attempted = false;

static void vm_net_mock_copy_bounded_field(char *out, size_t outCap,
                                           const u8 *src, u32 srcLen)
{
    u32 copyLen = 0;
    u32 limit = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (src == NULL || srcLen == 0)
        return;
    limit = srcLen;
    if (limit >= outCap)
        limit = (u32)outCap - 1;
    /* GBK lead bytes are >= 0x81. Advance by complete encoded characters so
     * a fixed-size client string can never end on half of a double-byte pair. */
    while (copyLen < limit)
    {
        u32 charLen = src[copyLen] >= 0x81 ? 2u : 1u;
        if (copyLen + charLen > limit || copyLen + charLen > srcLen)
            break;
        copyLen += charLen;
    }
    memcpy(out, src, copyLen);
    out[copyLen] = 0;
}

static u32 vm_net_mock_parse_decimal_slice(const u8 *data, u32 len)
{
    u32 value = 0;
    bool sawDigit = false;

    if (data == NULL)
        return 0;
    for (u32 i = 0; i < len; ++i)
    {
        if (data[i] < '0' || data[i] > '9')
            break;
        sawDigit = true;
        if (value > 429496729u)
            return 0;
        value = value * 10u + (u32)(data[i] - '0');
    }
    return sawDigit ? value : 0;
}

typedef struct
{
    u32 loaded;
    u32 overridden;
    u32 custom;
    u32 skipped;
} vm_net_mock_task_catalog_db_context;

static int vm_net_mock_task_catalog_raw_index(u32 taskId)
{
    for (u32 i = 0; i < g_vm_net_mock_task_catalog_count; ++i)
    {
        if (g_vm_net_mock_task_catalog[i].taskId == taskId)
            return (int)i;
    }
    return -1;
}

static void vm_net_mock_task_reward_items_from_legacy(
    vm_net_mock_task_definition *task)
{
    if (task == NULL)
        return;
    memset(task->rewardItems, 0, sizeof(task->rewardItems));
    task->rewardItemNum = 0;
    task->rewardItemsOverridden = false;
    if (task->rewardItemId != 0 && task->rewardItemCount != 0)
    {
        task->rewardItems[0].itemId = task->rewardItemId;
        task->rewardItems[0].count = task->rewardItemCount;
        task->rewardItems[0].itemType = task->rewardItemType;
        task->rewardItemNum = 1;
    }
}

static void vm_net_mock_task_reward_items_sync_legacy(
    vm_net_mock_task_definition *task)
{
    if (task == NULL)
        return;
    if (task->rewardItemNum == 0)
    {
        task->rewardItemId = 0;
        task->rewardItemCount = 0;
        task->rewardItemType = 0;
        return;
    }
    task->rewardItemId = task->rewardItems[0].itemId;
    task->rewardItemCount = task->rewardItems[0].count;
    task->rewardItemType = task->rewardItems[0].itemType;
}

static bool vm_net_mock_task_reward_items_are_valid(
    const vm_net_mock_task_definition *task)
{
    if (task == NULL || task->rewardItemNum > VM_NET_MOCK_TASK_REWARD_ITEM_MAX)
        return false;
    for (u8 i = 0; i < task->rewardItemNum; ++i)
    {
        if (task->rewardItems[i].itemId == 0 ||
            task->rewardItems[i].count == 0)
        {
            return false;
        }
        /* Equipment and vitality flasks are durable individual backpack
         * instances.  One awardinfo row identifies one sequence, so a
         * multi-instance grant must be expressed as separate future rows,
         * never as a misleading stack count on the first instance. */
        if ((vm_net_mock_find_equipment_catalog_item(
                 task->rewardItems[i].itemId) != NULL ||
             vm_net_mock_backpack_item_id_uses_reservoir_count(
                 task->rewardItems[i].itemId)) &&
            task->rewardItems[i].count != 1)
        {
            return false;
        }
        for (u8 previous = 0; previous < i; ++previous)
        {
            if (task->rewardItems[previous].itemId ==
                task->rewardItems[i].itemId)
            {
                return false;
            }
        }
    }
    return true;
}

static bool vm_net_mock_task_scene_battle_target_is_well_formed(
    u8 requirementType, u32 requirementId, const char *scene)
{
    if (scene == NULL)
        return false;
    if (scene[0] == 0)
        return true;
    return requirementType == 2 && requirementId != 0 &&
           requirementId <= 0xffffu && strlen(scene) < 64 &&
           vm_net_mock_scene_name_is_safe(scene);
}

static bool vm_net_mock_task_scene_battle_targets_are_well_formed(
    const vm_net_mock_task_definition *task)
{
    return task != NULL &&
           vm_net_mock_task_scene_battle_target_is_well_formed(
               task->requirementType1, task->requirementId1,
               task->requirementScene1) &&
           vm_net_mock_task_scene_battle_target_is_well_formed(
               task->requirementType2, task->requirementId2,
               task->requirementScene2);
}

static bool vm_net_mock_task_scene_battle_targets_are_configured(
    const vm_net_mock_task_definition *task)
{
    if (!vm_net_mock_task_scene_battle_targets_are_well_formed(task))
        return false;
    return (task->requirementScene1[0] == 0 ||
            vm_net_mock_scene_battle_monster_configured_target_exists(
                task->requirementScene1, task->requirementId1)) &&
           (task->requirementScene2[0] == 0 ||
            vm_net_mock_scene_battle_monster_configured_target_exists(
                task->requirementScene2, task->requirementId2));
}

static bool vm_net_mock_task_scene_battle_targets_are_ready(
    const vm_net_mock_task_definition *task)
{
    if (!vm_net_mock_task_scene_battle_targets_are_well_formed(task))
        return false;
    return (task->requirementScene1[0] == 0 ||
            vm_net_mock_scene_battle_monster_target_ready(
                task->requirementScene1, task->requirementId1)) &&
           (task->requirementScene2[0] == 0 ||
            vm_net_mock_scene_battle_monster_target_ready(
                task->requirementScene2, task->requirementId2));
}

/* This is deliberately a pure comparison: battle settlement already carries
 * the real client-selected scene context, so it must not infer another scene
 * or start a new database lookup while processing the result. */
static bool vm_net_mock_task_battle_requirement_matches(
    const vm_net_mock_task_definition *task, u8 requirementSlot, u32 enemyId,
    const char *currentScene)
{
    u8 requirementType = 0;
    u32 requirementId = 0;
    const char *targetScene = NULL;

    if (task == NULL || enemyId == 0 ||
        (requirementSlot != 1 && requirementSlot != 2))
    {
        return false;
    }
    if (requirementSlot == 1)
    {
        requirementType = task->requirementType1;
        requirementId = task->requirementId1;
        targetScene = task->requirementScene1;
    }
    else
    {
        requirementType = task->requirementType2;
        requirementId = task->requirementId2;
        targetScene = task->requirementScene2;
    }
    if (requirementType != 2 || requirementId != enemyId)
        return false;
    if (targetScene == NULL || targetScene[0] == 0)
        return true;
    return currentScene != NULL && vm_net_mock_scene_name_is_safe(currentScene) &&
           strcmp(targetScene, currentScene) == 0;
}

static bool vm_net_mock_task_definition_is_valid(
    const vm_net_mock_task_definition *task)
{
    return task != NULL && task->taskId != 0 &&
           task->taskId != VM_NET_MOCK_TEST_TASK_ID &&
           task->name[0] != 0 && task->giver[0] != 0 &&
           task->receiver[0] != 0 &&
           strlen(task->name) < sizeof(task->name) &&
           strlen(task->giver) < sizeof(task->giver) &&
           strlen(task->receiver) < sizeof(task->receiver) &&
           strlen(task->goal) < sizeof(task->goal) &&
           strlen(task->rewardText) < sizeof(task->rewardText) &&
           strlen(task->offerDialog) < sizeof(task->offerDialog) &&
           strlen(task->activeDialog) < sizeof(task->activeDialog) &&
           strlen(task->completedDialog) < sizeof(task->completedDialog) &&
           task->requirementType1 <= 2 && task->requirementType2 <= 2 &&
           vm_net_mock_task_scene_battle_targets_are_well_formed(task) &&
           vm_net_mock_task_reward_items_are_valid(task) &&
           task->prerequisiteTaskId != task->taskId;
}

static bool vm_net_mock_task_catalog_db_row(
    void *contextValue, unsigned int columnCount,
    const char *const *values, const size_t *lengths)
{
    vm_net_mock_task_catalog_db_context *context =
        (vm_net_mock_task_catalog_db_context *)contextValue;
    vm_net_mock_task_definition task;
    u32 number[19];
    int existing = -1;

    memset(&task, 0, sizeof(task));
    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 27)
        return false;
    for (u32 i = 0; i < 19; ++i)
    {
        if (!vm_mock_mysql_parse_u32(values[i], lengths[i], &number[i]))
        {
            ++context->skipped;
            return true;
        }
    }
    if (number[0] == 0 || number[0] == VM_NET_MOCK_TEST_TASK_ID ||
        number[1] > 1 || number[2] > 0xffu || number[3] > 0xffu ||
        number[4] > 0xffu || number[5] > 2 || number[6] > 0xffu ||
        number[8] > 2 || number[9] > 0xffu || number[18] > 0xffu)
    {
        ++context->skipped;
        return true;
    }

    existing = vm_net_mock_task_catalog_raw_index(number[0]);
    if (existing >= 0)
        task = g_vm_net_mock_task_catalog[existing];
    else if (g_vm_net_mock_task_catalog_count >= VM_NET_MOCK_TASK_CATALOG_MAX)
    {
        ++context->skipped;
        return true;
    }
    task.taskId = number[0];
    task.enabled = number[1] != 0;
    task.level = (u8)number[2];
    task.difficulty = (u8)number[3];
    task.classification = (u8)number[4];
    task.requirementType1 = (u8)number[5];
    task.requirementCount1 = (u8)number[6];
    task.requirementId1 = number[7];
    task.requirementType2 = (u8)number[8];
    task.requirementCount2 = (u8)number[9];
    task.requirementId2 = number[10];
    task.prerequisiteTaskId = number[11];
    task.givenItemId = number[12];
    task.givenItemCount = number[13];
    task.rewardExp = number[14];
    task.rewardMoney = number[15];
    task.rewardItemId = number[16];
    task.rewardItemCount = number[17];
    task.rewardItemType = (u8)number[18];
    /* server_tasks predates the separate multi-reward relation.  Seed its
     * single legacy reward first; the ordered relation below replaces it only
     * when rows actually exist for this task. */
    vm_net_mock_task_reward_items_from_legacy(&task);
    if (!vm_net_mock_dynamic_npc_decode_hex(values[19], lengths[19],
                                             task.name, sizeof(task.name)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[20], lengths[20],
                                             task.giver, sizeof(task.giver)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[21], lengths[21],
                                             task.receiver, sizeof(task.receiver)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[22], lengths[22],
                                             task.goal, sizeof(task.goal)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[23], lengths[23],
                                             task.rewardText, sizeof(task.rewardText)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[24], lengths[24],
                                             task.offerDialog, sizeof(task.offerDialog)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[25], lengths[25],
                                             task.activeDialog, sizeof(task.activeDialog)) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[26], lengths[26],
                                             task.completedDialog, sizeof(task.completedDialog)) ||
        !vm_net_mock_task_definition_is_valid(&task))
    {
        ++context->skipped;
        return true;
    }
    task.builtin = existing >= 0 && g_vm_net_mock_task_catalog[existing].builtin;
    task.overridden = true;
    if (existing >= 0)
    {
        g_vm_net_mock_task_catalog[existing] = task;
        ++context->overridden;
    }
    else
    {
        g_vm_net_mock_task_catalog[g_vm_net_mock_task_catalog_count++] = task;
        ++context->custom;
    }
    ++context->loaded;
    return true;
}

typedef struct
{
    u32 loaded;
    u32 taskCount;
    u32 skipped;
} vm_net_mock_task_reward_catalog_db_context;

static bool vm_net_mock_task_reward_catalog_db_row(
    void *contextValue, unsigned int columnCount,
    const char *const *values, const size_t *lengths)
{
    vm_net_mock_task_reward_catalog_db_context *context =
        (vm_net_mock_task_reward_catalog_db_context *)contextValue;
    u32 number[5];
    int index = -1;
    vm_net_mock_task_definition *task = NULL;

    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 5)
        return false;
    for (u32 i = 0; i < 5; ++i)
    {
        if (!vm_mock_mysql_parse_u32(values[i], lengths[i], &number[i]))
        {
            ++context->skipped;
            return true;
        }
    }
    if (number[0] == 0 || number[1] >= VM_NET_MOCK_TASK_REWARD_ITEM_MAX ||
        number[2] == 0 || number[3] == 0 || number[4] > 0xffu ||
        vm_net_mock_find_shop_catalog_item(number[2]) == NULL)
    {
        ++context->skipped;
        return true;
    }
    index = vm_net_mock_task_catalog_raw_index(number[0]);
    if (index < 0)
    {
        ++context->skipped;
        return true;
    }
    task = &g_vm_net_mock_task_catalog[index];
    if (!task->rewardItemsOverridden)
    {
        memset(task->rewardItems, 0, sizeof(task->rewardItems));
        task->rewardItemNum = 0;
        task->rewardItemsOverridden = true;
        ++context->taskCount;
    }
    /* The primary key and ORDER BY make contiguous slots a storage contract.
     * Reject holes rather than silently moving reward order in a live task. */
    if (number[1] != task->rewardItemNum)
    {
        ++context->skipped;
        return true;
    }
    task->rewardItems[task->rewardItemNum].itemId = number[2];
    task->rewardItems[task->rewardItemNum].count = number[3];
    task->rewardItems[task->rewardItemNum].itemType = (u8)number[4];
    ++task->rewardItemNum;
    vm_net_mock_task_reward_items_sync_legacy(task);
    ++context->loaded;
    return true;
}

typedef struct
{
    u32 loaded;
    u32 skipped;
} vm_net_mock_task_scene_battle_target_db_context;

static bool vm_net_mock_task_scene_battle_target_db_row(
    void *contextValue, unsigned int columnCount,
    const char *const *values, const size_t *lengths)
{
    vm_net_mock_task_scene_battle_target_db_context *context =
        (vm_net_mock_task_scene_battle_target_db_context *)contextValue;
    vm_net_mock_task_definition *task = NULL;
    char scene[64];
    u32 taskId = 0;
    u32 requirementSlot = 0;
    int index = -1;

    memset(scene, 0, sizeof(scene));
    if (context == NULL || columnCount != 3 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &taskId) || taskId == 0 ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &requirementSlot) ||
        (requirementSlot != 1 && requirementSlot != 2) ||
        !vm_net_mock_dynamic_npc_decode_hex(values[2], lengths[2], scene,
                                            sizeof(scene)))
    {
        if (context != NULL)
            ++context->skipped;
        return true;
    }
    index = vm_net_mock_task_catalog_raw_index(taskId);
    if (index < 0)
    {
        ++context->skipped;
        return true;
    }
    task = &g_vm_net_mock_task_catalog[index];
    if ((requirementSlot == 1 &&
         !vm_net_mock_task_scene_battle_target_is_well_formed(
             task->requirementType1, task->requirementId1, scene)) ||
        (requirementSlot == 2 &&
         !vm_net_mock_task_scene_battle_target_is_well_formed(
             task->requirementType2, task->requirementId2, scene)))
    {
        ++context->skipped;
        return true;
    }
    if (requirementSlot == 1)
        snprintf(task->requirementScene1, sizeof(task->requirementScene1),
                 "%s", scene);
    else
        snprintf(task->requirementScene2, sizeof(task->requirementScene2),
                 "%s", scene);
    ++context->loaded;
    return true;
}

static bool vm_net_mock_task_catalog_apply_db(void)
{
    vm_net_mock_task_catalog_db_context context;
    vm_net_mock_task_reward_catalog_db_context rewardContext;
    vm_net_mock_task_scene_battle_target_db_context sceneTargetContext;

    memset(&context, 0, sizeof(context));
    memset(&rewardContext, 0, sizeof(rewardContext));
    memset(&sceneTargetContext, 0, sizeof(sceneTargetContext));
    /* vm_net_mock_task_reward_catalog_db_row validates each reward through
     * vm_net_mock_find_shop_catalog_item().  That lookup lazily loads the
     * shop's MySQL overrides on a cold service.  A row callback executes
     * before its SELECT result is fully drained, so resolve that dependency
     * before beginning either task result set rather than nesting a query on
     * the same protocol stream. */
    (void)vm_net_mock_load_shop_catalog();
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_tasks ("
            "task_id INT UNSIGNED NOT NULL,enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "level TINYINT UNSIGNED NOT NULL DEFAULT 1,difficulty TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "classification TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "requirement_type1 TINYINT UNSIGNED NOT NULL DEFAULT 0,requirement_count1 TINYINT UNSIGNED NOT NULL DEFAULT 0,requirement_id1 INT UNSIGNED NOT NULL DEFAULT 0,"
            "requirement_type2 TINYINT UNSIGNED NOT NULL DEFAULT 0,requirement_count2 TINYINT UNSIGNED NOT NULL DEFAULT 0,requirement_id2 INT UNSIGNED NOT NULL DEFAULT 0,"
            "prerequisite_task_id INT UNSIGNED NOT NULL DEFAULT 0,given_item_id INT UNSIGNED NOT NULL DEFAULT 0,given_item_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_exp INT UNSIGNED NOT NULL DEFAULT 0,reward_money INT UNSIGNED NOT NULL DEFAULT 0,reward_item_id INT UNSIGNED NOT NULL DEFAULT 0,reward_item_count INT UNSIGNED NOT NULL DEFAULT 0,reward_item_type TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "name VARBINARY(31) NOT NULL,giver VARBINARY(15) NOT NULL,receiver VARBINARY(15) NOT NULL,goal VARBINARY(95) NOT NULL DEFAULT '',reward_text VARBINARY(31) NOT NULL DEFAULT '',"
            "offer_dialog VARBINARY(255) NOT NULL DEFAULT '',active_dialog VARBINARY(255) NOT NULL DEFAULT '',completed_dialog VARBINARY(255) NOT NULL DEFAULT '',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(task_id),KEY idx_server_tasks_enabled(enabled,task_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_task_reward_items ("
            "task_id INT UNSIGNED NOT NULL,reward_order TINYINT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL,item_count INT UNSIGNED NOT NULL,"
            "item_type TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "PRIMARY KEY(task_id,reward_order),KEY idx_server_task_reward_items_item(item_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_task_scene_battle_targets ("
            "task_id INT UNSIGNED NOT NULL,requirement_slot TINYINT UNSIGNED NOT NULL,"
            "scene VARBINARY(63) NOT NULL,"
            "PRIMARY KEY(task_id,requirement_slot),"
            "KEY idx_server_task_scene_battle_targets_scene(scene)) ENGINE=InnoDB") ||
        !vm_mysql_query(
            "SELECT task_id,enabled,level,difficulty,classification,"
            "requirement_type1,requirement_count1,requirement_id1,requirement_type2,requirement_count2,requirement_id2,"
            "prerequisite_task_id,given_item_id,given_item_count,reward_exp,reward_money,reward_item_id,reward_item_count,reward_item_type,"
            "HEX(name),HEX(giver),HEX(receiver),HEX(goal),HEX(reward_text),HEX(offer_dialog),HEX(active_dialog),HEX(completed_dialog) "
            "FROM server_tasks ORDER BY task_id",
            vm_net_mock_task_catalog_db_row, &context) ||
        !vm_mysql_query(
            "SELECT task_id,reward_order,item_id,item_count,item_type "
            "FROM server_task_reward_items ORDER BY task_id,reward_order",
            vm_net_mock_task_reward_catalog_db_row, &rewardContext) ||
        !vm_mysql_query(
            "SELECT task_id,requirement_slot,HEX(scene) "
            "FROM server_task_scene_battle_targets ORDER BY task_id,requirement_slot",
            vm_net_mock_task_scene_battle_target_db_row, &sceneTargetContext))
    {
        printf("[error][mock-admin] task_catalog_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    printf("[info][mock-admin] task_catalog_db_load rows=%u overridden=%u custom=%u skipped=%u reward_rows=%u reward_tasks=%u reward_skipped=%u scene_target_rows=%u scene_target_skipped=%u\n",
           context.loaded, context.overridden, context.custom, context.skipped,
           rewardContext.loaded, rewardContext.taskCount, rewardContext.skipped,
           sceneTargetContext.loaded, sceneTargetContext.skipped);
    return true;
}

static bool vm_net_mock_load_task_catalog(void)
{
    char path[256];
    u8 data[32768];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 headerBytes = 0;
    u32 pos = 16;

    if (g_vm_net_mock_task_catalog_attempted)
        return g_vm_net_mock_task_catalog_count != 0;
    g_vm_net_mock_task_catalog_attempted = true;
    g_vm_net_mock_task_catalog_count = 0;

    if (!vm_net_mock_open_server_data_resource("task.dsh", ".dsh", NULL,
                                               path, sizeof(path)))
        return false;
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 20 || vm_net_mock_read_le32_at(data, 0) != len - 4)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    headerBytes = vm_net_mock_read_le32_at(data, 12);
    if (columnCount != 25 || rowCount == 0 || rowCount > VM_NET_MOCK_TASK_CATALOG_MAX ||
        16u + headerBytes > len)
    {
        return false;
    }
    for (u32 column = 0; column < columnCount; ++column)
    {
        u32 stringLen = 0;
        if (pos >= len)
            return false;
        stringLen = data[pos++];
        if (pos + stringLen > len)
            return false;
        pos += stringLen;
    }
    if (pos > 16u + headerBytes)
        return false;
    pos = 16u + headerBytes;

    for (u32 rowIndex = 0; rowIndex < rowCount && pos + 4 <= len; ++rowIndex)
    {
        const u8 *values[25];
        u8 valueLens[25];
        vm_net_mock_task_definition task;
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowEnd = 0;
        bool valid = true;

        pos += 4;
        if (rowLen == 0 || rowLen > len - pos)
            return false;
        rowEnd = pos + rowLen;
        memset(values, 0, sizeof(values));
        memset(valueLens, 0, sizeof(valueLens));
        for (u32 column = 0; column < columnCount; ++column)
        {
            u32 stringLen = 0;
            if (pos >= rowEnd)
            {
                valid = false;
                break;
            }
            stringLen = data[pos++];
            if (pos + stringLen > rowEnd)
            {
                valid = false;
                break;
            }
            values[column] = data + pos;
            valueLens[column] = (u8)stringLen;
            pos += stringLen;
        }
        pos = rowEnd;
        if (!valid)
            return false;

        memset(&task, 0, sizeof(task));
        task.taskId = vm_net_mock_parse_decimal_slice(values[0], valueLens[0]);
        task.level = (u8)vm_net_mock_parse_decimal_slice(values[2], valueLens[2]);
        task.difficulty = (u8)vm_net_mock_parse_decimal_slice(values[3], valueLens[3]);
        task.classification = (u8)vm_net_mock_parse_decimal_slice(values[4], valueLens[4]);
        task.givenItemId = vm_net_mock_parse_decimal_slice(values[8], valueLens[8]);
        task.givenItemCount = vm_net_mock_parse_decimal_slice(values[9], valueLens[9]);
        task.rewardExp = vm_net_mock_parse_decimal_slice(values[11], valueLens[11]);
        task.rewardMoney = vm_net_mock_parse_decimal_slice(values[12], valueLens[12]);
        task.rewardItemId = vm_net_mock_parse_decimal_slice(values[13], valueLens[13]);
        task.rewardItemType = (u8)vm_net_mock_parse_decimal_slice(values[14], valueLens[14]);
        task.rewardItemCount = vm_net_mock_parse_decimal_slice(values[15], valueLens[15]);
        task.requirementType1 = (u8)vm_net_mock_parse_decimal_slice(values[16], valueLens[16]);
        task.requirementId1 = vm_net_mock_parse_decimal_slice(values[17], valueLens[17]);
        task.requirementCount1 = (u8)vm_net_mock_parse_decimal_slice(values[19], valueLens[19]);
        task.requirementType2 = (u8)vm_net_mock_parse_decimal_slice(values[20], valueLens[20]);
        task.requirementId2 = vm_net_mock_parse_decimal_slice(values[21], valueLens[21]);
        task.requirementCount2 = (u8)vm_net_mock_parse_decimal_slice(values[23], valueLens[23]);
        task.prerequisiteTaskId = vm_net_mock_parse_decimal_slice(values[24], valueLens[24]);
        task.enabled = true;
        task.builtin = true;
        task.overridden = false;
        vm_net_mock_task_reward_items_from_legacy(&task);
        vm_net_mock_copy_bounded_field(task.name, sizeof(task.name), values[1], valueLens[1]);
        snprintf(task.sourceName, sizeof(task.sourceName), "%s", task.name);
        vm_net_mock_copy_bounded_field(task.giver, sizeof(task.giver), values[5], valueLens[5]);
        vm_net_mock_copy_bounded_field(task.receiver, sizeof(task.receiver), values[6], valueLens[6]);
        vm_net_mock_copy_bounded_field(task.goal, sizeof(task.goal), values[7], valueLens[7]);
        vm_net_mock_copy_bounded_field(task.rewardText, sizeof(task.rewardText), values[10], valueLens[10]);
        if (task.taskId != 0 && task.name[0] != 0 &&
            g_vm_net_mock_task_catalog_count < VM_NET_MOCK_TASK_CATALOG_MAX)
        {
            g_vm_net_mock_task_catalog[g_vm_net_mock_task_catalog_count++] = task;
        }
    }
    if (!vm_net_mock_task_catalog_apply_db())
        return false;
    printf("[info][network] mock_task_catalog source=task.dsh+mysql rows=%u declared_rows=%u path=%s\n",
           g_vm_net_mock_task_catalog_count, rowCount, path);
    return g_vm_net_mock_task_catalog_count != 0;
}

static const vm_net_mock_task_definition *vm_net_mock_task_catalog_find_by_id(u32 taskId)
{
    if (!vm_net_mock_load_task_catalog())
        return NULL;
    for (u32 i = 0; i < g_vm_net_mock_task_catalog_count; ++i)
    {
        if (g_vm_net_mock_task_catalog[i].taskId == taskId &&
            g_vm_net_mock_task_catalog[i].enabled)
            return &g_vm_net_mock_task_catalog[i];
    }
    return NULL;
}

static void vm_net_mock_normalize_task_name(const char *src, char *out, size_t outCap)
{
    static const u8 difficultSuffix[] = {0xa3, 0xa8, 0xc0, 0xa7, 0xc4, 0xd1, 0xa3, 0xa9}; /* （困难） */
    const u8 *bytes = (const u8 *)(src ? src : "");
    u32 len = (u32)strlen((const char *)bytes);
    u32 pos = 0;
    u32 outPos = 0;

    if (out == NULL || outCap == 0)
        return;
    if (len >= sizeof(difficultSuffix) &&
        memcmp(bytes + len - sizeof(difficultSuffix), difficultSuffix,
               sizeof(difficultSuffix)) == 0)
    {
        len -= sizeof(difficultSuffix);
    }
    while (pos < len && outPos + 1 < outCap)
    {
        if (bytes[pos] == ' ' || bytes[pos] == '\t' || bytes[pos] == '!')
        {
            pos += 1;
            continue;
        }
        if (pos + 1 < len && bytes[pos] == 0xa3 && bytes[pos + 1] == 0xa1) /* ！ */
        {
            pos += 2;
            continue;
        }
        if (bytes[pos] >= 0x81 && pos + 1 < len)
        {
            if (outPos + 2 >= outCap)
                break;
            out[outPos++] = (char)bytes[pos++];
            out[outPos++] = (char)bytes[pos++];
        }
        else
        {
            out[outPos++] = (char)bytes[pos++];
        }
    }
    out[outPos] = 0;
}

static const vm_net_mock_task_definition *vm_net_mock_task_catalog_find_by_name(const char *name)
{
    char normalizedName[64];

    if (!vm_net_mock_load_task_catalog() || name == NULL || name[0] == 0)
        return NULL;
    vm_net_mock_normalize_task_name(name, normalizedName, sizeof(normalizedName));
    for (u32 i = 0; i < g_vm_net_mock_task_catalog_count; ++i)
    {
        char normalizedCatalogName[64];
        if (!g_vm_net_mock_task_catalog[i].enabled)
            continue;
        vm_net_mock_normalize_task_name(g_vm_net_mock_task_catalog[i].name,
                                        normalizedCatalogName,
                                        sizeof(normalizedCatalogName));
        if (strcmp(normalizedName, normalizedCatalogName) == 0)
            return &g_vm_net_mock_task_catalog[i];
        if (g_vm_net_mock_task_catalog[i].sourceName[0] != 0)
        {
            vm_net_mock_normalize_task_name(
                g_vm_net_mock_task_catalog[i].sourceName,
                normalizedCatalogName, sizeof(normalizedCatalogName));
            if (strcmp(normalizedName, normalizedCatalogName) == 0)
                return &g_vm_net_mock_task_catalog[i];
        }
    }
    return NULL;
}

static const vm_net_mock_task_definition *vm_net_mock_task_admin_find(u32 taskId)
{
    int index = -1;
    if (!vm_net_mock_load_task_catalog())
        return NULL;
    index = vm_net_mock_task_catalog_raw_index(taskId);
    return index >= 0 ? &g_vm_net_mock_task_catalog[index] : NULL;
}

static u32 vm_net_mock_task_admin_list(vm_net_mock_task_definition *rows,
                                       u32 rowCap)
{
    u32 count = 0;
    if (!vm_net_mock_load_task_catalog())
        return 0;
    count = g_vm_net_mock_task_catalog_count;
    if (count > rowCap)
        count = rowCap;
    if (rows != NULL && count != 0)
        memcpy(rows, g_vm_net_mock_task_catalog, count * sizeof(*rows));
    return count;
}

static bool vm_net_mock_task_active_state_count(u32 taskId, u32 *countOut)
{
    char query[256];
    vm_mock_mysql_u32_context context;

    if (countOut)
        *countOut = 0;
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM account_role_tasks WHERE task_id=%u AND task_state IN (1,2)",
             taskId);
    if (!vm_mysql_query(query, vm_mock_mysql_single_u32_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (countOut)
        *countOut = context.value;
    return true;
}

static bool vm_net_mock_task_catalog_reload(void)
{
    g_vm_net_mock_task_catalog_attempted = false;
    g_vm_net_mock_task_catalog_count = 0;
    memset(g_vm_net_mock_task_catalog, 0, sizeof(g_vm_net_mock_task_catalog));
    return vm_net_mock_load_task_catalog();
}

static bool vm_net_mock_task_admin_save(
    const vm_net_mock_task_definition *task, const char **errorOut)
{
    vm_net_mock_task_definition normalizedTask;
    char nameHex[sizeof(task->name) * 2 + 1];
    char giverHex[sizeof(task->giver) * 2 + 1];
    char receiverHex[sizeof(task->receiver) * 2 + 1];
    char goalHex[sizeof(task->goal) * 2 + 1];
    char rewardTextHex[sizeof(task->rewardText) * 2 + 1];
    char offerHex[sizeof(task->offerDialog) * 2 + 1];
    char activeHex[sizeof(task->activeDialog) * 2 + 1];
    char completedHex[sizeof(task->completedDialog) * 2 + 1];
    char requirementScene1Hex[sizeof(task->requirementScene1) * 2 + 1];
    char requirementScene2Hex[sizeof(task->requirementScene2) * 2 + 1];
    char query[8192];
    u32 activeCount = 0;
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "invalid task definition";
    if (task == NULL)
        return false;
    normalizedTask = *task;
    vm_net_mock_task_reward_items_sync_legacy(&normalizedTask);
    task = &normalizedTask;
    if (!vm_net_mock_load_task_catalog() ||
        !vm_net_mock_task_definition_is_valid(task))
    {
        return false;
    }
    if (!vm_net_mock_task_scene_battle_targets_are_configured(task))
    {
        if (errorOut)
            *errorOut = "scene battle task target is not configured";
        return false;
    }
    if (!task->enabled &&
        (!vm_net_mock_task_active_state_count(task->taskId, &activeCount) ||
         activeCount != 0))
    {
        if (errorOut)
            *errorOut = activeCount != 0
                            ? "the task still has active player states"
                            : vm_mysql_last_error();
        return false;
    }
    memset(nameHex, 0, sizeof(nameHex));
    memset(giverHex, 0, sizeof(giverHex));
    memset(receiverHex, 0, sizeof(receiverHex));
    memset(goalHex, 0, sizeof(goalHex));
    memset(rewardTextHex, 0, sizeof(rewardTextHex));
    memset(offerHex, 0, sizeof(offerHex));
    memset(activeHex, 0, sizeof(activeHex));
    memset(completedHex, 0, sizeof(completedHex));
    memset(requirementScene1Hex, 0, sizeof(requirementScene1Hex));
    memset(requirementScene2Hex, 0, sizeof(requirementScene2Hex));
#define VM_TASK_ENCODE_TEXT(field, output)                                      \
    do                                                                          \
    {                                                                           \
        if ((field)[0] != 0 &&                                                  \
            vm_mysql_hex_encode((field), strlen(field), (output),               \
                                sizeof(output)) == 0)                            \
        {                                                                       \
            if (errorOut)                                                       \
                *errorOut = "task text encoding failed";                       \
            return false;                                                       \
        }                                                                       \
    } while (0)
    VM_TASK_ENCODE_TEXT(task->name, nameHex);
    VM_TASK_ENCODE_TEXT(task->giver, giverHex);
    VM_TASK_ENCODE_TEXT(task->receiver, receiverHex);
    VM_TASK_ENCODE_TEXT(task->goal, goalHex);
    VM_TASK_ENCODE_TEXT(task->rewardText, rewardTextHex);
    VM_TASK_ENCODE_TEXT(task->offerDialog, offerHex);
    VM_TASK_ENCODE_TEXT(task->activeDialog, activeHex);
    VM_TASK_ENCODE_TEXT(task->completedDialog, completedHex);
    VM_TASK_ENCODE_TEXT(task->requirementScene1, requirementScene1Hex);
    VM_TASK_ENCODE_TEXT(task->requirementScene2, requirementScene2Hex);
#undef VM_TASK_ENCODE_TEXT
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_tasks(task_id,enabled,level,difficulty,classification,"
        "requirement_type1,requirement_count1,requirement_id1,requirement_type2,requirement_count2,requirement_id2,"
        "prerequisite_task_id,given_item_id,given_item_count,reward_exp,reward_money,reward_item_id,reward_item_count,reward_item_type,"
        "name,giver,receiver,goal,reward_text,offer_dialog,active_dialog,completed_dialog) "
        "VALUES(%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
        "X'%s',X'%s',X'%s',X'%s',X'%s',X'%s',X'%s',X'%s') "
        "ON DUPLICATE KEY UPDATE enabled=VALUES(enabled),level=VALUES(level),difficulty=VALUES(difficulty),classification=VALUES(classification),"
        "requirement_type1=VALUES(requirement_type1),requirement_count1=VALUES(requirement_count1),requirement_id1=VALUES(requirement_id1),"
        "requirement_type2=VALUES(requirement_type2),requirement_count2=VALUES(requirement_count2),requirement_id2=VALUES(requirement_id2),"
        "prerequisite_task_id=VALUES(prerequisite_task_id),given_item_id=VALUES(given_item_id),given_item_count=VALUES(given_item_count),"
        "reward_exp=VALUES(reward_exp),reward_money=VALUES(reward_money),reward_item_id=VALUES(reward_item_id),reward_item_count=VALUES(reward_item_count),reward_item_type=VALUES(reward_item_type),"
        "name=VALUES(name),giver=VALUES(giver),receiver=VALUES(receiver),goal=VALUES(goal),reward_text=VALUES(reward_text),"
        "offer_dialog=VALUES(offer_dialog),active_dialog=VALUES(active_dialog),completed_dialog=VALUES(completed_dialog)",
        task->taskId, task->enabled ? 1u : 0u, task->level,
        task->difficulty, task->classification,
        task->requirementType1, task->requirementCount1, task->requirementId1,
        task->requirementType2, task->requirementCount2, task->requirementId2,
        task->prerequisiteTaskId, task->givenItemId, task->givenItemCount,
        task->rewardExp, task->rewardMoney, task->rewardItemId,
        task->rewardItemCount, task->rewardItemType,
        nameHex, giverHex, receiverHex, goalHex, rewardTextHex,
        offerHex, activeHex, completedHex);
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    transactionStarted = true;
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "DELETE FROM server_task_scene_battle_targets WHERE task_id=%u",
             task->taskId);
    if (!vm_mysql_exec(query))
        goto failed;
    if (task->requirementScene1[0] != 0)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_task_scene_battle_targets(task_id,requirement_slot,scene) "
                 "VALUES(%u,1,X'%s')",
                 task->taskId, requirementScene1Hex);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    if (task->requirementScene2[0] != 0)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_task_scene_battle_targets(task_id,requirement_slot,scene) "
                 "VALUES(%u,2,X'%s')",
                 task->taskId, requirementScene2Hex);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_task_reward_items WHERE task_id=%u",
             task->taskId);
    if (!vm_mysql_exec(query))
        goto failed;
    for (u8 rewardIndex = 0; rewardIndex < task->rewardItemNum;
         ++rewardIndex)
    {
        const vm_net_mock_task_reward_item *reward =
            &task->rewardItems[rewardIndex];
        snprintf(query, sizeof(query),
                 "INSERT INTO server_task_reward_items(task_id,reward_order,item_id,item_count,item_type) "
                 "VALUES(%u,%u,%u,%u,%u)",
                 task->taskId, rewardIndex, reward->itemId,
                 reward->count, reward->itemType);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
    if (!vm_net_mock_task_catalog_reload())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] task_save task=%u enabled=%u builtin=%u name=%s\n",
           task->taskId, task->enabled ? 1u : 0u,
           task->builtin ? 1u : 0u, task->name);
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (errorOut)
        *errorOut = vm_mysql_last_error();
    return false;
}

static bool vm_net_mock_task_admin_delete_override(u32 taskId,
                                                    const char **errorOut)
{
    const vm_net_mock_task_definition *task = vm_net_mock_task_admin_find(taskId);
    char query[256];
    u32 activeCount = 0;
    u32 bindingCount = 0;
    vm_mock_mysql_u32_context countContext;

    if (errorOut)
        *errorOut = "task override not found";
    if (task == NULL || !task->overridden)
        return false;
    if (!vm_net_mock_task_active_state_count(taskId, &activeCount) ||
        activeCount != 0)
    {
        if (errorOut)
            *errorOut = activeCount != 0
                            ? "the task still has active player states"
                            : vm_mysql_last_error();
        return false;
    }
    memset(&countContext, 0, sizeof(countContext));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM server_dynamic_npc_tasks WHERE task_id=%u",
             taskId);
    if (!task->builtin &&
        (!vm_mysql_query(query, vm_mock_mysql_single_u32_row, &countContext) ||
         countContext.invalid || !countContext.found ||
         (bindingCount = countContext.value) != 0))
    {
        if (errorOut)
            *errorOut = bindingCount != 0
                            ? "the task is still bound to a dynamic npc"
                            : vm_mysql_last_error();
        return false;
    }
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_task_reward_items WHERE task_id=%u", taskId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "DELETE FROM server_task_scene_battle_targets WHERE task_id=%u",
             taskId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query), "DELETE FROM server_tasks WHERE task_id=%u",
             taskId);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    if (!vm_net_mock_task_catalog_reload())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] task_override_delete task=%u\n", taskId);
    return true;

failed:
    (void)vm_mysql_exec("ROLLBACK");
    if (errorOut)
        *errorOut = vm_mysql_last_error();
    return false;
}

static bool vm_net_mock_xse_ascii_identifier(const u8 *data, u32 len, u32 *pos,
                                             bool zeroPrefixed)
{
    u32 cursor = pos ? *pos : 0;
    u32 nameLen = 0;

    if (data == NULL || pos == NULL || cursor >= len)
        return false;
    if (zeroPrefixed)
    {
        if (data[cursor] != 0 || cursor + 1 >= len)
            return false;
        cursor += 1;
    }
    nameLen = data[cursor++];
    if (nameLen == 0 || nameLen > 64 || cursor + nameLen > len)
        return false;
    if (!((data[cursor] >= 'A' && data[cursor] <= 'Z') ||
          (data[cursor] >= 'a' && data[cursor] <= 'z') ||
          data[cursor] == '_'))
    {
        return false;
    }
    for (u32 i = 1; i < nameLen; ++i)
    {
        u8 ch = data[cursor + i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_'))
        {
            return false;
        }
    }
    *pos = cursor + nameLen;
    return true;
}

static u32 vm_net_mock_xse_function_table_offset(const u8 *data, u32 len)
{
    u32 bestOffset = 0;
    int bestScore = -1;

    if (data == NULL || len < 24)
        return 0;
    for (u32 off = 0x10; off + 8 <= len; ++off)
    {
        u32 functionCount = vm_net_mock_read_le32_at(data, off);
        u32 cursor = off + 4;
        u32 commandCount = 0;
        bool valid = true;

        if (functionCount == 0 || functionCount > 64)
            continue;
        for (u32 i = 0; i < functionCount; ++i)
        {
            if (cursor + 8 > len)
            {
                valid = false;
                break;
            }
            cursor += 8;
            if (!vm_net_mock_xse_ascii_identifier(data, len, &cursor, true))
            {
                valid = false;
                break;
            }
        }
        if (!valid || cursor + 4 > len)
            continue;
        commandCount = vm_net_mock_read_le32_at(data, cursor);
        cursor += 4;
        if (commandCount == 0 || commandCount > 128)
            continue;
        for (u32 i = 0; i < commandCount; ++i)
        {
            if (!vm_net_mock_xse_ascii_identifier(data, len, &cursor, false))
            {
                valid = false;
                break;
            }
        }
        if (valid && cursor == len)
        {
            int score = (int)(functionCount * 8u + commandCount * 4u);
            if (score > bestScore)
            {
                bestScore = score;
                bestOffset = off;
            }
        }
    }
    return bestOffset;
}

static u32 vm_net_mock_xse_string_pool_offset(const u8 *data, u32 functionTableOffset)
{
    u32 bestOffset = 0;
    int bestScore = -1;

    if (data == NULL || functionTableOffset <= 0x14)
        return 0;
    for (u32 off = 0x10; off + 4 <= functionTableOffset; ++off)
    {
        u32 count = vm_net_mock_read_le32_at(data, off);
        u32 cursor = off + 4;
        u32 nonEmpty = 0;
        bool valid = true;

        if (count == 0 || count > 512)
            continue;
        for (u32 i = 0; i < count; ++i)
        {
            u32 stringLen = 0;
            if (cursor + 4 > functionTableOffset)
            {
                valid = false;
                break;
            }
            stringLen = vm_net_mock_read_le32_at(data, cursor);
            cursor += 4;
            if (stringLen > functionTableOffset - cursor)
            {
                valid = false;
                break;
            }
            if (stringLen != 0)
                nonEmpty += 1;
            cursor += stringLen;
        }
        if (valid && cursor == functionTableOffset)
        {
            int score = (int)(count * 4u + nonEmpty);
            if (score >= bestScore)
            {
                bestScore = score;
                bestOffset = off;
            }
        }
    }
    return bestOffset;
}

static int vm_net_mock_xse_command_index(const u8 *data, u32 len,
                                         u32 functionTableOffset,
                                         const char *commandName)
{
    u32 functionCount = 0;
    u32 commandCount = 0;
    u32 cursor = functionTableOffset;

    if (data == NULL || commandName == NULL || commandName[0] == 0 ||
        cursor + 4 > len)
    {
        return -1;
    }
    functionCount = vm_net_mock_read_le32_at(data, cursor);
    cursor += 4;
    if (functionCount == 0 || functionCount > 64)
        return -1;
    for (u32 i = 0; i < functionCount; ++i)
    {
        if (cursor + 8 > len)
            return -1;
        cursor += 8;
        if (!vm_net_mock_xse_ascii_identifier(data, len, &cursor, true))
            return -1;
    }
    if (cursor + 4 > len)
        return -1;
    commandCount = vm_net_mock_read_le32_at(data, cursor);
    cursor += 4;
    if (commandCount == 0 || commandCount > 128)
        return -1;
    for (u32 i = 0; i < commandCount; ++i)
    {
        u32 nameLen = 0;
        size_t wantedLen = strlen(commandName);

        if (cursor >= len)
            return -1;
        nameLen = data[cursor++];
        if (nameLen == 0 || nameLen > 64 || cursor + nameLen > len)
            return -1;
        if (wantedLen == nameLen && memcmp(data + cursor, commandName, nameLen) == 0)
            return (int)i;
        cursor += nameLen;
    }
    return -1;
}

static bool vm_net_mock_xse_parse_task_marker(const u8 *text, u32 textLen,
                                              char *taskName, size_t taskNameCap,
                                              bool *offerOut, bool *activeOut,
                                              bool *completedOut)
{
    static const u8 suffixOffer[] = {0xa3, 0xa8, 0xce, 0xb4, 0xbd, 0xd3, 0xa3, 0xa9}; /* （未接） */
    static const u8 suffixActive[] = {0xa3, 0xa8, 0xce, 0xb4, 0xcd, 0xea, 0xb3, 0xc9, 0xa3, 0xa9}; /* （未完成） */
    static const u8 suffixCompleted[] = {0xa3, 0xa8, 0xd2, 0xd1, 0xcd, 0xea, 0xb3, 0xc9, 0xa3, 0xa9}; /* （已完成） */
    const u8 *suffix = NULL;
    u32 suffixLen = 0;
    u32 start = 0;
    u32 nameLen = 0;

    if (taskName && taskNameCap)
        taskName[0] = 0;
    if (offerOut)
        *offerOut = false;
    if (activeOut)
        *activeOut = false;
    if (completedOut)
        *completedOut = false;
    if (text == NULL || textLen < 6 || taskName == NULL || taskNameCap == 0 ||
        text[0] != 0xa3 || (text[1] != 0xbf && text[1] != 0xa1))
    {
        return false;
    }
    if (textLen >= sizeof(suffixOffer) &&
        memcmp(text + textLen - sizeof(suffixOffer), suffixOffer, sizeof(suffixOffer)) == 0)
    {
        suffix = suffixOffer;
        suffixLen = sizeof(suffixOffer);
        if (offerOut)
            *offerOut = true;
    }
    else if (textLen >= sizeof(suffixActive) &&
             memcmp(text + textLen - sizeof(suffixActive), suffixActive, sizeof(suffixActive)) == 0)
    {
        suffix = suffixActive;
        suffixLen = sizeof(suffixActive);
        if (activeOut)
            *activeOut = true;
    }
    else if (textLen >= sizeof(suffixCompleted) &&
             memcmp(text + textLen - sizeof(suffixCompleted), suffixCompleted, sizeof(suffixCompleted)) == 0)
    {
        suffix = suffixCompleted;
        suffixLen = sizeof(suffixCompleted);
        if (completedOut)
            *completedOut = true;
    }
    if (suffix == NULL)
        return false;
    start = 2;
    while (start < textLen - suffixLen && (text[start] == ' ' || text[start] == '\t'))
        start += 1;
    nameLen = textLen - suffixLen - start;
    while (nameLen != 0 && (text[start + nameLen - 1] == ' ' || text[start + nameLen - 1] == '\t'))
        nameLen -= 1;
    vm_net_mock_copy_bounded_field(taskName, taskNameCap, text + start, nameLen);
    return taskName[0] != 0;
}

static bool vm_net_mock_load_xse_summary(const char *scriptName,
                                         vm_net_mock_xse_summary *summaryOut)
{
    u8 data[8192];
    vm_net_mock_xse_summary summary;
    u32 len = 0;
    u32 functionTableOffset = 0;
    u32 stringPoolOffset = 0;
    u32 stringCount = 0;
    u32 cursor = 0;
    u32 stringOffsets[512];
    u32 stringLengths[512];
    u32 dialogStringIndices[VM_NET_MOCK_XSE_DIRECT_DIALOG_MAX];
    u32 storedDialogCount = 0;
    int showDialogCommand = -1;

    if (summaryOut)
        memset(summaryOut, 0, sizeof(*summaryOut));
    if (scriptName == NULL || scriptName[0] == 0 || summaryOut == NULL)
        return false;
    memset(&summary, 0, sizeof(summary));
    memset(stringOffsets, 0, sizeof(stringOffsets));
    memset(stringLengths, 0, sizeof(stringLengths));
    memset(dialogStringIndices, 0, sizeof(dialogStringIndices));
    len = vm_net_mock_load_xse_resource(scriptName, data, sizeof(data));
    if (len < 16)
        return false;
    functionTableOffset = vm_net_mock_xse_function_table_offset(data, len);
    stringPoolOffset = vm_net_mock_xse_string_pool_offset(data, functionTableOffset);
    if (functionTableOffset == 0 || stringPoolOffset == 0)
        return false;
    stringCount = vm_net_mock_read_le32_at(data, stringPoolOffset);
    cursor = stringPoolOffset + 4;
    for (u32 i = 0; i < stringCount; ++i)
    {
        u32 textLen = 0;
        const u8 *text = NULL;
        char markerName[64];
        bool offer = false;
        bool active = false;
        bool completed = false;

        if (cursor + 4 > functionTableOffset)
            return false;
        textLen = vm_net_mock_read_le32_at(data, cursor);
        cursor += 4;
        if (textLen > functionTableOffset - cursor)
            return false;
        text = data + cursor;
        stringOffsets[i] = cursor;
        stringLengths[i] = textLen;
        cursor += textLen;
        summary.stringCount += 1;
        if (vm_net_mock_xse_parse_task_marker(text, textLen,
                                              markerName, sizeof(markerName),
                                              &offer, &active, &completed))
        {
            const vm_net_mock_task_definition *task =
                vm_net_mock_task_catalog_find_by_name(markerName);
            if (task != NULL)
            {
                vm_net_mock_xse_task_ref *ref = NULL;
                for (u32 refIndex = 0; refIndex < summary.taskRefCount; ++refIndex)
                {
                    if (summary.taskRefs[refIndex].taskId == task->taskId)
                    {
                        ref = &summary.taskRefs[refIndex];
                        break;
                    }
                }
                if (ref == NULL && summary.taskRefCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
                {
                    ref = &summary.taskRefs[summary.taskRefCount++];
                    memset(ref, 0, sizeof(*ref));
                    ref->taskId = task->taskId;
                }
                if (ref != NULL)
                {
                    ref->offer = ref->offer || offer;
                    ref->active = ref->active || active;
                    ref->completed = ref->completed || completed;
                }
            }
            continue;
        }
        if (textLen != 0)
            vm_net_mock_copy_bounded_field(summary.idleDialog,
                                           sizeof(summary.idleDialog),
                                           text, textLen);
    }

    /* The XSE VM encodes a direct string push followed by an external command
     * call as two eight-byte records. Restrict extraction to the bytecode body
     * and require the resolved SHOWDIALOG command index, so task marker strings
     * in the pool can never be mistaken for dialogue. */
    showDialogCommand = vm_net_mock_xse_command_index(data, len,
                                                      functionTableOffset,
                                                      "SHOWDIALOG");
    if (showDialogCommand >= 0)
    {
        static const u8 pushStringOpcode[4] = {0x1a, 0x00, 0x01, 0x02};
        static const u8 callCommandOpcode[4] = {0x1e, 0x00, 0x01, 0x07};

        for (u32 off = 0x1c; off + 16 <= stringPoolOffset; ++off)
        {
            u32 stringIndex = 0;
            u32 commandIndex = 0;

            if (memcmp(data + off, pushStringOpcode, sizeof(pushStringOpcode)) != 0 ||
                memcmp(data + off + 8, callCommandOpcode,
                       sizeof(callCommandOpcode)) != 0)
            {
                continue;
            }
            stringIndex = vm_net_mock_read_le32_at(data, off + 4);
            commandIndex = vm_net_mock_read_le32_at(data, off + 12);
            if (commandIndex != (u32)showDialogCommand || stringIndex >= stringCount)
                continue;
            summary.directDialogCount += 1;
            if (storedDialogCount < VM_NET_MOCK_XSE_DIRECT_DIALOG_MAX)
                dialogStringIndices[storedDialogCount++] = stringIndex;
            off += 15;
        }
    }
    if (storedDialogCount != 0)
    {
        u32 idleIndex = dialogStringIndices[storedDialogCount - 1];
        vm_net_mock_copy_bounded_field(summary.idleDialog,
                                       sizeof(summary.idleDialog),
                                       data + stringOffsets[idleIndex],
                                       stringLengths[idleIndex]);
    }
    /* Most ordinary one-task scripts use one of these proven layouts:
     * offer/idle, offer/completed/idle, or
     * offer/continuation/completed/idle. Complex scripts remain on their
     * proven final idle line until their control-flow graph is reconstructed. */
    if (summary.taskRefCount == 1 && storedDialogCount >= 2)
    {
        const vm_net_mock_xse_task_ref *ref = &summary.taskRefs[0];
        u32 offerIndex = dialogStringIndices[0];

        if (ref->offer)
        {
            vm_net_mock_copy_bounded_field(summary.offerDialog,
                                           sizeof(summary.offerDialog),
                                           data + stringOffsets[offerIndex],
                                           stringLengths[offerIndex]);
        }
        if (ref->completed && storedDialogCount >= 3 && storedDialogCount <= 4)
        {
            u32 completedIndex = dialogStringIndices[storedDialogCount - 2];
            vm_net_mock_copy_bounded_field(summary.completedDialog,
                                           sizeof(summary.completedDialog),
                                           data + stringOffsets[completedIndex],
                                           stringLengths[completedIndex]);
        }
    }
    summary.loaded = true;
    *summaryOut = summary;
    return true;
}

static bool vm_net_mock_validate_xse_task_resources(void)
{
    static const char *scripts[] = {
        "task0.xse",
        "\x30\x34\xc1\xd9\xb0\xb2\xba\xfa\xec\xb3\x2e\x78\x73\x65", /* 04临安胡斐.xse */
        "\x30\x36\xd2\xb0\xd6\xed\xc1\xd6\xc1\xd6\xb3\xe5\x2e\x78\x73\x65", /* 06野猪林林冲.xse */
        "\xd0\xc5\xcf\xe4\x2e\x78\x73\x65" /* 信箱.xse */
    };
    static const char copperStageScene[] =
        "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"; /* c00蓬莱仙岛_01.sce */
    static const char swordValleyScene[] =
        "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"; /* 00蓬莱仙岛_02.sce */
    vm_net_mock_scene_npcinfo_seed copperStageSeeds[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    vm_net_mock_scene_npcinfo_seed swordValleySeeds[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    u32 loadedCount = 0;
    u32 taskRefCount = 0;
    u32 copperStageCount = 0;
    u32 copperStageTotal = 0;
    bool foundGuoJing = false;
    u32 swordValleyCount = 0;
    u32 swordValleyTotal = 0;
    bool foundBlacksmith = false;
    bool foundMonkey = false;
    bool foundTestTaskNpc = false;

    if (!vm_net_mock_load_task_catalog())
    {
        printf("[error][network] mock_xse_catalog_validate task_catalog=missing "
               "required=web/fs/JHOnlineData/task.dsh\n");
        return false;
    }
    for (u32 i = 0; i < sizeof(scripts) / sizeof(scripts[0]); ++i)
    {
        vm_net_mock_xse_summary summary;
        memset(&summary, 0, sizeof(summary));
        if (!vm_net_mock_load_xse_summary(scripts[i], &summary))
        {
            printf("[error][network] mock_xse_validate script=%s result=load-failed\n",
                   scripts[i]);
            continue;
        }
        loadedCount += 1;
        taskRefCount += summary.taskRefCount;
        printf("[info][network] mock_xse_validate script=%s strings=%u task_refs=%u direct_dialogs=%u offer_len=%u completed_len=%u idle_len=%u\n",
               scripts[i], summary.stringCount, summary.taskRefCount,
               summary.directDialogCount,
               (u32)strlen(summary.offerDialog),
               (u32)strlen(summary.completedDialog),
               (u32)strlen(summary.idleDialog));
    }
    printf("[info][network] mock_xse_catalog_validate loaded=%u expected=%u task_refs=%u source=web/fs/JHOnlineData evidence=XSE0+task.dsh\n",
           loadedCount, (u32)(sizeof(scripts) / sizeof(scripts[0])), taskRefCount);
    memset(copperStageSeeds, 0, sizeof(copperStageSeeds));
    copperStageCount = vm_net_mock_collect_scene_npcinfo_seeds(
        copperStageScene, copperStageSeeds, VM_NET_MOCK_SCENE_NPCINFO_MAX,
        &copperStageTotal, NULL);
    for (u32 i = 0; i < copperStageCount; ++i)
    {
        foundGuoJing = foundGuoJing ||
            (strcmp(copperStageSeeds[i].scriptName, "task0.xse") == 0 &&
             strcmp(copperStageSeeds[i].displayName,
                    "\xb4\xf3\xcf\xc0\xb9\xf9\xbe\xb8") == 0); /* 大侠郭靖 */
    }
    printf("[info][network] mock_scene_npc_exact_validate scene=%s rows=%u total=%u guojing=%u policy=exact-scene-only\n",
           copperStageScene, copperStageCount, copperStageTotal,
           foundGuoJing ? 1u : 0u);
    memset(swordValleySeeds, 0, sizeof(swordValleySeeds));
    swordValleyCount = vm_net_mock_collect_scene_npcinfo_seeds(
        swordValleyScene, swordValleySeeds, VM_NET_MOCK_SCENE_NPCINFO_MAX,
        &swordValleyTotal, NULL);
    for (u32 i = 0; i < swordValleyCount; ++i)
    {
        foundBlacksmith = foundBlacksmith || swordValleySeeds[i].actorId == 20020;
        foundMonkey = foundMonkey || swordValleySeeds[i].actorId == 20021;
        foundTestTaskNpc = foundTestTaskNpc ||
                           swordValleySeeds[i].actorId == VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID;
    }
    printf("[info][network] mock_scene_npc_service_validate scene=%s rows=%u total=%u blacksmith=%u monkey=%u test_task_npc=%u policy=production-catalog-only\n",
           swordValleyScene, swordValleyCount, swordValleyTotal,
           foundBlacksmith ? 1u : 0u, foundMonkey ? 1u : 0u,
           foundTestTaskNpc ? 1u : 0u);
    if (loadedCount != sizeof(scripts) / sizeof(scripts[0]) ||
        swordValleyCount < 2 || swordValleyTotal < 2 ||
        !foundBlacksmith || !foundMonkey || foundTestTaskNpc)
    {
        printf("[error][network] mock_xse_task_validate result=failed "
               "scripts=%u/%u task_refs=%u copper=%u/%u guojing=%u "
               "sword=%u/%u blacksmith=%u monkey=%u test_npc=%u\n",
               loadedCount, (u32)(sizeof(scripts) / sizeof(scripts[0])),
               taskRefCount, copperStageCount, copperStageTotal,
               foundGuoJing ? 1u : 0u, swordValleyCount, swordValleyTotal,
               foundBlacksmith ? 1u : 0u, foundMonkey ? 1u : 0u,
               foundTestTaskNpc ? 1u : 0u);
    }
    return loadedCount == sizeof(scripts) / sizeof(scripts[0]) &&
           swordValleyCount >= 2 && swordValleyTotal >= 2 &&
           foundBlacksmith && foundMonkey && !foundTestTaskNpc;
}

typedef struct
{
    u8 state;
    u8 progress1;
    u8 progress2;
    bool found;
    bool invalid;
} vm_net_mock_task_state_row;

static bool vm_net_mock_task_state_mysql_row(void *contextValue,
                                             unsigned int columnCount,
                                             const char *const *values,
                                             const size_t *lengths)
{
    vm_net_mock_task_state_row *row = (vm_net_mock_task_state_row *)contextValue;
    u32 state = 0;
    u32 progress1 = 0;
    u32 progress2 = 0;

    if (row == NULL || row->found || columnCount != 3 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &state) || state > 0xffu ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &progress1) || progress1 > 0xffu ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &progress2) || progress2 > 0xffu)
    {
        if (row)
            row->invalid = true;
        return true;
    }
    row->state = (u8)state;
    row->progress1 = (u8)progress1;
    row->progress2 = (u8)progress2;
    row->found = true;
    return true;
}

static bool vm_net_mock_task_state_load(u32 roleId, u32 taskId,
                                        vm_net_mock_task_state_row *rowOut)
{
    char accountHex[129];
    char query[512];
    vm_net_mock_task_state_row row;
    bool queryOk = false;

    if (rowOut)
        memset(rowOut, 0, sizeof(*rowOut));
    if (roleId == 0 || taskId == 0 || !vm_net_mock_mysql_account_hex(accountHex))
        return false;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT task_state,progress1,progress2 FROM account_role_tasks "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND task_id=%u",
             accountHex, roleId, taskId);
    queryOk = vm_mysql_query(query, vm_net_mock_task_state_mysql_row, &row);
    if (!queryOk &&
        strcmp(vm_mysql_last_error(), "MySQL socket send failed") == 0)
    {
        /* A stale persistent socket is common after an idle map session.  A
         * SELECT is safe to replay because this failure happens before any row
         * callback.  Without the retry, login can briefly report tasknum=0 and
         * offer an already accepted task again. */
        printf("[warn][network] mock_task_mysql_reconnect role=%u task=%u reason=socket-send-failed\n",
               roleId, taskId);
        memset(&row, 0, sizeof(row));
        queryOk = vm_mysql_query(query, vm_net_mock_task_state_mysql_row, &row);
    }
    if (!queryOk || row.invalid)
    {
        printf("[error][network] mock_task_mysql_load_failed role=%u task=%u error=%s invalid=%u\n",
               roleId, taskId, vm_mysql_last_error(), row.invalid ? 1u : 0u);
        return false;
    }
    if (rowOut)
        *rowOut = row;
    return true;
}

typedef struct
{
    u32 taskId;
    u8 state;
    u8 progress1;
    u8 progress2;
    /* State 3 is terminal until its dynamic-NPC repeat policy allows a new
     * acceptance.  `updated_at` is written when state becomes 3 and is the
     * durable completion instant used for calendar-based policies. */
    u32 completedAt;
} vm_net_mock_task_state_list_row;

typedef struct
{
    vm_net_mock_task_state_list_row *rows;
    u32 rowCap;
    u32 rowCount;
    bool invalid;
} vm_net_mock_task_state_list_context;

typedef struct
{
    bool active;
    bool loaded;
    bool loadOk;
    u32 roleId;
    u32 rowCount;
    vm_net_mock_task_state_list_row rows[VM_NET_MOCK_TASK_CATALOG_MAX];
} vm_net_mock_task_state_request_cache;

static vm_net_mock_task_state_request_cache g_vm_net_mock_task_state_request_cache;

static bool vm_net_mock_task_state_list_mysql_row(void *contextValue,
                                                  unsigned int columnCount,
                                                  const char *const *values,
                                                  const size_t *lengths)
{
    vm_net_mock_task_state_list_context *context =
        (vm_net_mock_task_state_list_context *)contextValue;
    u32 taskId = 0;
    u32 state = 0;
    u32 progress1 = 0;
    u32 progress2 = 0;
    u32 completedAt = 0;

    if (context == NULL || columnCount != 5 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &taskId) || taskId == 0 ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &state) || state > 0xffu ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &progress1) || progress1 > 0xffu ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &progress2) || progress2 > 0xffu ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &completedAt))
    {
        if (context)
            context->invalid = true;
        return true;
    }
    if (context->rowCount < context->rowCap)
    {
        vm_net_mock_task_state_list_row *row = &context->rows[context->rowCount++];
        row->taskId = taskId;
        row->state = (u8)state;
        row->progress1 = (u8)progress1;
        row->progress2 = (u8)progress2;
        row->completedAt = state == 3 ? completedAt : 0;
    }
    return true;
}

static bool vm_net_mock_task_state_list_load_mysql(u32 roleId, bool activeOnly,
                                                   vm_net_mock_task_state_list_row *rows,
                                                   u32 rowCap, u32 *rowCountOut)
{
    char accountHex[129];
    char query[768];
    vm_net_mock_task_state_list_context context;
    bool queryOk = false;

    if (rowCountOut)
        *rowCountOut = 0;
    if (roleId == 0 || rows == NULL || rowCap == 0 ||
        !vm_net_mock_mysql_account_hex(accountHex))
    {
        return false;
    }
    memset(rows, 0, sizeof(*rows) * rowCap);
    memset(&context, 0, sizeof(context));
    context.rows = rows;
    context.rowCap = rowCap;
    snprintf(query, sizeof(query),
             "SELECT task_id,task_state,progress1,progress2,"
             "CASE WHEN task_state=3 THEN UNIX_TIMESTAMP(updated_at) ELSE 0 END "
             "FROM account_role_tasks "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u %s"
             "ORDER BY updated_at,task_id LIMIT %u",
             accountHex, roleId,
             activeOnly ? "AND task_state IN (1,2) " : "",
             rowCap);
    queryOk = vm_mysql_query(query, vm_net_mock_task_state_list_mysql_row, &context);
    if (!queryOk && strcmp(vm_mysql_last_error(), "MySQL socket send failed") == 0)
    {
        memset(rows, 0, sizeof(*rows) * rowCap);
        context.rowCount = 0;
        context.invalid = false;
        queryOk = vm_mysql_query(query, vm_net_mock_task_state_list_mysql_row, &context);
    }
    if (!queryOk || context.invalid)
    {
        printf("[error][network] mock_task_mysql_list_failed role=%u active_only=%u error=%s invalid=%u\n",
               roleId, activeOnly ? 1u : 0u, vm_mysql_last_error(),
               context.invalid ? 1u : 0u);
        return false;
    }
    if (rowCountOut)
        *rowCountOut = context.rowCount;
    return true;
}

static void vm_net_mock_task_state_request_cache_begin(void)
{
    memset(&g_vm_net_mock_task_state_request_cache, 0,
           sizeof(g_vm_net_mock_task_state_request_cache));
    g_vm_net_mock_task_state_request_cache.active = true;
}

static void vm_net_mock_task_state_request_cache_end(void)
{
    g_vm_net_mock_task_state_request_cache.active = false;
}

static bool vm_net_mock_task_state_list_load(u32 roleId, bool activeOnly,
                                             vm_net_mock_task_state_list_row *rows,
                                             u32 rowCap, u32 *rowCountOut)
{
    vm_net_mock_task_state_request_cache *cache =
        &g_vm_net_mock_task_state_request_cache;
    u32 copied = 0;

    if (!cache->active)
    {
        return vm_net_mock_task_state_list_load_mysql(roleId, activeOnly,
                                                      rows, rowCap, rowCountOut);
    }
    if (rowCountOut)
        *rowCountOut = 0;
    if (roleId == 0 || rows == NULL || rowCap == 0)
        return false;

    if (!cache->loaded || cache->roleId != roleId)
    {
        memset(cache->rows, 0, sizeof(cache->rows));
        cache->roleId = roleId;
        cache->rowCount = 0;
        cache->loadOk = vm_net_mock_task_state_list_load_mysql(
            roleId, false, cache->rows, VM_NET_MOCK_TASK_CATALOG_MAX,
            &cache->rowCount);
        cache->loaded = true;
        printf("[debug][mock-service] task_state_request_snapshot role=%u rows=%u ok=%u\n",
               roleId, cache->rowCount, cache->loadOk ? 1u : 0u);
    }
    if (!cache->loadOk)
        return false;

    memset(rows, 0, sizeof(*rows) * rowCap);
    for (u32 i = 0; i < cache->rowCount && copied < rowCap; ++i)
    {
        if (activeOnly && cache->rows[i].state != 1 && cache->rows[i].state != 2)
            continue;
        rows[copied++] = cache->rows[i];
    }
    if (rowCountOut)
        *rowCountOut = copied;
    return true;
}

static const vm_net_mock_task_state_list_row *vm_net_mock_task_state_list_find(
    const vm_net_mock_task_state_list_row *rows, u32 rowCount, u32 taskId)
{
    for (u32 i = 0; rows != NULL && i < rowCount; ++i)
    {
        if (rows[i].taskId == taskId)
            return &rows[i];
    }
    return NULL;
}

/* Task material drops are authored as ordinary monster drops, but their
 * eligibility belongs to the role's accepted task state.  The battle reward
 * path calls this before adding an item to the backpack; this routine uses the
 * same catalog and persisted progress that task settlement uses afterwards.
 *
 * `remainingOut` is the greatest remaining requirement among active matching
 * tasks.  The subsequent battle-progress path applies a single received item
 * to every matching task row, so summing would over-grant materials when two
 * accepted tasks happen to require the same item. */
static bool vm_net_mock_task_material_drop_policy(u32 roleId, u32 itemId,
                                                  bool *isTaskMaterialOut,
                                                  u32 *remainingOut)
{
    const vm_net_mock_shop_catalog_item *catalogItem = NULL;
    vm_net_mock_task_state_list_row states[VM_NET_MOCK_TASK_CATALOG_MAX];
    u32 stateCount = 0;
    bool isTaskMaterial = false;
    u32 remaining = 0;

    if (isTaskMaterialOut)
        *isTaskMaterialOut = false;
    if (remainingOut)
        *remainingOut = 0;
    if (roleId == 0 || itemId == 0 || !vm_net_mock_load_task_catalog())
        return false;

    /* The item resource owns the base classification.  Requirement rows are
     * an additional compatibility source for custom collection tasks whose
     * administrator intentionally uses an item from another category. */
    catalogItem = vm_net_mock_find_shop_catalog_item(itemId);
    isTaskMaterial = catalogItem != NULL && !catalogItem->isEquip &&
                     catalogItem->category == VM_NET_MOCK_ITEM_CATEGORY_TASK;
    for (u32 i = 0; i < g_vm_net_mock_task_catalog_count; ++i)
    {
        const vm_net_mock_task_definition *task = &g_vm_net_mock_task_catalog[i];

        if ((task->requirementType1 == 1 &&
             task->requirementId1 == itemId && task->requirementCount1 != 0) ||
            (task->requirementType2 == 1 &&
             task->requirementId2 == itemId && task->requirementCount2 != 0))
        {
            isTaskMaterial = true;
            break;
        }
    }
    if (!isTaskMaterial)
        return true;

    if (!vm_net_mock_task_state_list_load(roleId, true, states,
                                          VM_NET_MOCK_TASK_CATALOG_MAX,
                                          &stateCount))
    {
        return false;
    }
    for (u32 i = 0; i < stateCount; ++i)
    {
        const vm_net_mock_task_definition *task =
            vm_net_mock_task_catalog_find_by_id(states[i].taskId);
        u32 taskRemaining = 0;

        /* State 2 has already met its requirements and must not continue to
         * receive material drops while waiting for its turn-in dialog. */
        if (task == NULL || states[i].state != 1)
            continue;
        if (task->requirementType1 == 1 && task->requirementId1 == itemId &&
            task->requirementCount1 > states[i].progress1)
        {
            taskRemaining = task->requirementCount1 - states[i].progress1;
        }
        if (task->requirementType2 == 1 && task->requirementId2 == itemId &&
            task->requirementCount2 > states[i].progress2)
        {
            u32 secondRemaining = task->requirementCount2 - states[i].progress2;
            if (secondRemaining > taskRemaining)
                taskRemaining = secondRemaining;
        }
        if (taskRemaining > remaining)
            remaining = taskRemaining;
    }
    if (isTaskMaterialOut)
        *isTaskMaterialOut = true;
    if (remainingOut)
        *remainingOut = remaining;
    return true;
}

static bool vm_net_mock_task_repeat_policy_allows_completed(
    u8 repeatPolicy, u32 completedAt)
{
    time_t now = time(NULL);
    time_t completed = (time_t)completedAt;
    struct tm nowLocal;
    struct tm completedLocal;

    if (repeatPolicy == VM_NET_MOCK_TASK_REPEAT_UNLIMITED)
        return true;
    if (repeatPolicy < VM_NET_MOCK_TASK_REPEAT_DAILY ||
        repeatPolicy > VM_NET_MOCK_TASK_REPEAT_MONTHLY || completedAt == 0 ||
        now == (time_t)-1)
    {
        return false;
    }
#ifdef _WIN32
    if (localtime_s(&nowLocal, &now) != 0 ||
        localtime_s(&completedLocal, &completed) != 0)
#else
    if (localtime_r(&now, &nowLocal) == NULL ||
        localtime_r(&completed, &completedLocal) == NULL)
#endif
    {
        return false;
    }
    if (repeatPolicy == VM_NET_MOCK_TASK_REPEAT_DAILY)
        return nowLocal.tm_year != completedLocal.tm_year ||
               nowLocal.tm_yday != completedLocal.tm_yday;
    if (repeatPolicy == VM_NET_MOCK_TASK_REPEAT_MONTHLY)
        return nowLocal.tm_year != completedLocal.tm_year ||
               nowLocal.tm_mon != completedLocal.tm_mon;
    nowLocal.tm_hour = completedLocal.tm_hour = 0;
    nowLocal.tm_min = completedLocal.tm_min = 0;
    nowLocal.tm_sec = completedLocal.tm_sec = 0;
    nowLocal.tm_mday -= (nowLocal.tm_wday + 6) % 7;
    completedLocal.tm_mday -= (completedLocal.tm_wday + 6) % 7;
    nowLocal.tm_isdst = completedLocal.tm_isdst = -1;
    return mktime(&nowLocal) != mktime(&completedLocal);
}

static bool vm_net_mock_task_definition_available(
    const vm_net_mock_task_definition *task,
    const vm_net_mock_role_state *role,
    const vm_net_mock_task_state_list_row *states,
    u32 stateCount,
    u8 repeatPolicy)
{
    const vm_net_mock_task_state_list_row *persisted = NULL;
    const vm_net_mock_task_state_list_row *prerequisite = NULL;

    if (task == NULL || role == NULL || role->level < task->level)
    {
        return false;
    }
    persisted = vm_net_mock_task_state_list_find(states, stateCount, task->taskId);
    if (persisted != NULL &&
        !(persisted->state == 3 &&
          vm_net_mock_task_repeat_policy_allows_completed(
              repeatPolicy, persisted->completedAt)))
    {
        return false;
    }
    if (task->prerequisiteTaskId != 0)
    {
        prerequisite = vm_net_mock_task_state_list_find(
            states, stateCount, task->prerequisiteTaskId);
        if (prerequisite == NULL || prerequisite->state != 3)
            return false;
    }
    return vm_net_mock_task_scene_battle_targets_are_ready(task);
}

/* Keep the acceptance predicate authoritative, but expose its first failed
 * precondition through a normal GBK UI message instead of collapsing every
 * rejection to a silent result=1. This helper performs no writes. */
static const char *vm_net_mock_task_definition_unavailable_reason(
    const vm_net_mock_task_definition *task,
    const vm_net_mock_role_state *role,
    const vm_net_mock_task_state_list_row *states,
    u32 stateCount,
    u8 repeatPolicy,
    const char **reasonCodeOut,
    char *out,
    size_t outCap)
{
    const vm_net_mock_task_state_list_row *persisted = NULL;
    const vm_net_mock_task_state_list_row *prerequisite = NULL;

    if (reasonCodeOut != NULL)
        *reasonCodeOut = NULL;
    if (out == NULL || outCap == 0)
        return NULL;
    out[0] = 0;
    if (task == NULL)
    {
        if (reasonCodeOut != NULL)
            *reasonCodeOut = "task-missing";
        snprintf(out, outCap,
                 "\xC8\xCE\xCE\xF1\xCA\xFD\xBE\xDD\xB2\xBB\xB4\xE6\xD4\xDA\xBB\xF2\xD2\xD1\xB9\xD8\xB1\xD5\xA1\xA3"); /* 任务数据不存在或已关闭。 */
        return out;
    }
    if (role == NULL)
    {
        if (reasonCodeOut != NULL)
            *reasonCodeOut = "role-missing";
        snprintf(out, outCap,
                 "\xC8\xCE\xCE\xF1\xD7\xB4\xCC\xAC\xCE\xB4\xBE\xCD\xD0\xF7\xA3\xAC\xC7\xEB\xD6\xD8\xD0\xC2\xBD\xF8\xC8\xEB\xB3\xA1\xBE\xB0\xA1\xA3"); /* 任务状态未就绪，请重新进入场景。 */
        return out;
    }
    if (role->level < task->level)
    {
        if (reasonCodeOut != NULL)
            *reasonCodeOut = "level";
        snprintf(out, outCap,
                 "\xB5\xC8\xBC\xB6\xB2\xBB\xD7\xE3\xA3\xAC\xB4\xEF\xB5\xBD%u\xBC\xB6\xBA\xF3\xBF\xC9\xBD\xD3\xC8\xA1\xC8\xCE\xCE\xF1\xA1\xA3", /* 等级不足，达到%u级后可接取任务。 */
                 task->level);
        return out;
    }
    persisted = vm_net_mock_task_state_list_find(states, stateCount,
                                                  task->taskId);
    if (persisted != NULL &&
        !(persisted->state == 3 &&
          vm_net_mock_task_repeat_policy_allows_completed(
              repeatPolicy, persisted->completedAt)))
    {
        if (persisted->state == 1)
        {
            if (reasonCodeOut != NULL)
                *reasonCodeOut = "active";
            snprintf(out, outCap,
                     "\xB8\xC3\xC8\xCE\xCE\xF1\xD2\xD1\xBE\xAD\xBD\xD3\xC8\xA1\xA1\xA3"); /* 该任务已经接取。 */
        }
        else if (persisted->state == 2)
        {
            if (reasonCodeOut != NULL)
                *reasonCodeOut = "submittable";
            snprintf(out, outCap,
                     "\xC8\xCE\xCE\xF1\xD2\xD1\xCD\xEA\xB3\xC9\xA3\xAC\xC7\xEB\xC7\xB0\xCD\xF9\xBD\xBB\xB8\xB6NPC\xCC\xE1\xBD\xBB\xA1\xA3"); /* 任务已完成，请前往交付NPC提交。 */
        }
        else
        {
            if (reasonCodeOut != NULL)
                *reasonCodeOut = "completed";
            snprintf(out, outCap,
                     "\xB8\xC3\xC8\xCE\xCE\xF1\xD2\xD1\xBE\xAD\xCD\xEA\xB3\xC9\xA1\xA3"); /* 该任务已经完成。 */
        }
        return out;
    }
    if (task->prerequisiteTaskId != 0)
    {
        prerequisite = vm_net_mock_task_state_list_find(
            states, stateCount, task->prerequisiteTaskId);
        if (prerequisite == NULL || prerequisite->state != 3)
        {
            if (reasonCodeOut != NULL)
                *reasonCodeOut = "prerequisite";
            snprintf(out, outCap,
                     "\xC7\xEB\xCF\xC8\xCD\xEA\xB3\xC9\xC7\xB0\xD6\xC3\xC8\xCE\xCE\xF1\xA1\xA3"); /* 请先完成前置任务。 */
            return out;
        }
    }
    if (!vm_net_mock_task_scene_battle_targets_are_ready(task))
    {
        if (reasonCodeOut != NULL)
            *reasonCodeOut = "scene-battle-target-unready";
        snprintf(out, outCap,
                 "Scene battle target is not deployed or is not ready.");
        return out;
    }
    return NULL;
}

/* The CBE action=4 follow-up carries only a task id. Preserve the prior NPC
 * dialog authorization on the service session: offer context owns 6/11 and
 * submit context owns 6/4. */
static void vm_net_mock_task_interaction_context_reset(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session != NULL)
    {
        memset(session->taskOfferContexts, 0,
               sizeof(session->taskOfferContexts));
    }
}

static void vm_net_mock_task_interaction_context_record(u32 taskId, u32 actorId,
                                                         u8 repeatPolicy,
                                                         u8 interaction,
                                                         const char *scene)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_task_offer_context *slot = NULL;

    if (session == NULL || role == NULL || taskId == 0 || actorId == 0 ||
        (interaction != VM_MOCK_SERVICE_TASK_INTERACTION_OFFER &&
         interaction != VM_MOCK_SERVICE_TASK_INTERACTION_SUBMIT) ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return;
    }
    for (u32 i = 0; i < VM_MOCK_SERVICE_TASK_OFFER_CONTEXT_MAX; ++i)
    {
        vm_mock_service_task_offer_context *candidate =
            &session->taskOfferContexts[i];
        if (candidate->taskId == taskId && candidate->roleId == role->roleId &&
            candidate->interaction == interaction)
        {
            slot = candidate;
            break;
        }
        if (slot == NULL && candidate->taskId == 0)
            slot = candidate;
    }
    if (slot == NULL)
        return;
    memset(slot, 0, sizeof(*slot));
    slot->roleId = role->roleId;
    slot->taskId = taskId;
    slot->actorId = actorId;
    slot->repeatable = repeatPolicy != VM_NET_MOCK_TASK_REPEAT_NEVER;
    slot->repeatPolicy = repeatPolicy;
    slot->interaction = interaction;
    snprintf(slot->scene, sizeof(slot->scene), "%s", scene);
}

static bool vm_net_mock_task_offer_context_consume(u32 taskId,
                                                    u8 *repeatPolicyOut)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *scene = vm_net_mock_current_scene_name();

    if (repeatPolicyOut != NULL)
        *repeatPolicyOut = VM_NET_MOCK_TASK_REPEAT_NEVER;
    if (session == NULL || role == NULL || taskId == 0 ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return false;
    }
    for (u32 i = 0; i < VM_MOCK_SERVICE_TASK_OFFER_CONTEXT_MAX; ++i)
    {
        vm_mock_service_task_offer_context *context =
            &session->taskOfferContexts[i];
        if (context->taskId != taskId || context->roleId != role->roleId ||
            context->interaction != VM_MOCK_SERVICE_TASK_INTERACTION_OFFER ||
            strcmp(context->scene, scene) != 0)
        {
            continue;
        }
        if (repeatPolicyOut != NULL)
            *repeatPolicyOut = context->repeatPolicy;
        memset(context, 0, sizeof(*context));
        return true;
    }
    return false;
}

/* A 6/4 submit packet contains no actor id. Its accepted predecessor must be
 * the state-2 submit action from this same scene and role, not merely any NPC
 * which happens to share the task's offer binding. */
static bool vm_net_mock_task_submit_context_consume(u32 taskId,
                                                     u32 *actorIdOut)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *scene = vm_net_mock_current_scene_name();

    if (actorIdOut != NULL)
        *actorIdOut = 0;
    if (session == NULL || role == NULL || taskId == 0 ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return false;
    }
    for (u32 i = 0; i < VM_MOCK_SERVICE_TASK_OFFER_CONTEXT_MAX; ++i)
    {
        vm_mock_service_task_offer_context *context =
            &session->taskOfferContexts[i];

        if (context->taskId != taskId || context->roleId != role->roleId ||
            context->interaction != VM_MOCK_SERVICE_TASK_INTERACTION_SUBMIT ||
            strcmp(context->scene, scene) != 0)
        {
            continue;
        }
        if (actorIdOut != NULL)
            *actorIdOut = context->actorId;
        memset(context, 0, sizeof(*context));
        return true;
    }
    return false;
}

static u32 vm_net_mock_scene_npc_seed_priority(
    const vm_net_mock_scene_npcinfo_seed *seed,
    const vm_net_mock_role_state *role,
    const vm_net_mock_task_state_list_row *states,
    u32 stateCount)
{
    vm_net_mock_xse_summary summary;
    u32 priority = 1;

    if (seed == NULL)
        return 0;
    if (seed->taskId != 0)
    {
        const vm_net_mock_task_state_list_row *persisted =
            vm_net_mock_task_state_list_find(states, stateCount, seed->taskId);
        const vm_net_mock_task_definition *task =
            vm_net_mock_task_catalog_find_by_id(seed->taskId);

        priority = 20;
        if (persisted != NULL && persisted->state == 2)
            return 500;
        if (persisted != NULL && persisted->state == 1)
            return 400;
        if (vm_net_mock_task_definition_available(
                task, role, states, stateCount,
                vm_net_mock_task_repeat_policy_from_seed(seed)))
        {
            return 300;
        }
    }
    if (seed->scriptName[0] == 0)
        return priority;
    priority = 10;
    memset(&summary, 0, sizeof(summary));
    if (!vm_net_mock_load_xse_summary(seed->scriptName, &summary))
        return priority;
    if (summary.taskRefCount != 0)
        priority = 20;
    for (u32 refIndex = 0; refIndex < summary.taskRefCount; ++refIndex)
    {
        const vm_net_mock_xse_task_ref *ref = &summary.taskRefs[refIndex];
        const vm_net_mock_task_state_list_row *persisted =
            vm_net_mock_task_state_list_find(states, stateCount, ref->taskId);
        const vm_net_mock_task_definition *task =
            vm_net_mock_task_catalog_find_by_id(ref->taskId);

        if (persisted != NULL && persisted->state == 2 && ref->completed)
        {
            if (priority < 500)
                priority = 500;
        }
        else if (persisted != NULL && persisted->state == 1 && ref->completed)
        {
            if (priority < 450)
                priority = 450;
        }
        else if (persisted != NULL && persisted->state == 1 && ref->active)
        {
            if (priority < 400)
                priority = 400;
        }
        else if (persisted != NULL && persisted->state == 1)
        {
            if (priority < 350)
                priority = 350;
        }
        else if (persisted == NULL && ref->offer &&
                 vm_net_mock_task_definition_available(task, role, states,
                                                       stateCount,
                                                       VM_NET_MOCK_TASK_REPEAT_NEVER))
        {
            if (priority < 300)
                priority = 300;
        }
    }
    return priority;
}

static u32 vm_net_mock_select_scene_npcinfo_seeds_uncached(
    const char *scene,
    vm_net_mock_scene_npcinfo_seed *seeds,
    u32 seedCap,
    u32 *totalOut,
    u32 *dynamicOut)
{
    vm_net_mock_scene_npcinfo_seed catalog[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    vm_net_mock_task_state_list_row states[VM_NET_MOCK_TASK_CATALOG_MAX];
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 priorities[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    bool selected[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    u32 catalogCount = 0;
    u32 stateCount = 0;
    u32 selectCount = 0;
    u32 total = 0;
    u32 dynamic = 0;

    if (totalOut)
        *totalOut = 0;
    if (dynamicOut)
        *dynamicOut = 0;
    if (seeds == NULL || seedCap == 0)
        return 0;
    if (seedCap > VM_NET_MOCK_SCENE_NPCINFO_MAX)
        seedCap = VM_NET_MOCK_SCENE_NPCINFO_MAX;
    memset(seeds, 0, sizeof(*seeds) * seedCap);
    memset(catalog, 0, sizeof(catalog));
    memset(states, 0, sizeof(states));
    memset(priorities, 0, sizeof(priorities));
    memset(selected, 0, sizeof(selected));
    catalogCount = vm_net_mock_collect_scene_npcinfo_seeds(
        scene, catalog, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX, &total, &dynamic);
    if (role != NULL)
    {
        (void)vm_net_mock_task_state_list_load(role->roleId, false, states,
                                               VM_NET_MOCK_TASK_CATALOG_MAX,
                                               &stateCount);
    }
    for (u32 i = 0; i < catalogCount; ++i)
    {
        priorities[i] = vm_net_mock_scene_npc_seed_priority(
            &catalog[i], role, states, stateCount);
    }
    selectCount = catalogCount < seedCap ? catalogCount : seedCap;
    for (u32 pick = 0; pick < selectCount; ++pick)
    {
        u32 bestIndex = catalogCount;
        u32 bestPriority = 0;

        for (u32 i = 0; i < catalogCount; ++i)
        {
            if (!selected[i] &&
                (bestIndex == catalogCount || priorities[i] > bestPriority))
            {
                bestIndex = i;
                bestPriority = priorities[i];
            }
        }
        if (bestIndex < catalogCount)
            selected[bestIndex] = true;
    }
    /* Preserve SCE order among the chosen rows. Actor ids and click indices are
     * stable even when a task-relevant NPC displaces an idle row in a scene
     * containing more than four actors. */
    selectCount = 0;
    for (u32 i = 0; i < catalogCount && selectCount < seedCap; ++i)
    {
        if (selected[i])
            seeds[selectCount++] = catalog[i];
    }
    if (totalOut)
        *totalOut = total;
    if (dynamicOut)
        *dynamicOut = dynamic;
    if (total > selectCount)
    {
        printf("[info][network] mock_scene_npc_select scene=%s catalog=%u total=%u selected=%u task_states=%u policy=task-state-first client_slots=4\n",
               scene ? scene : "-", catalogCount, total, selectCount, stateCount);
    }
    return selectCount;
}

static u32 vm_net_mock_select_scene_npcinfo_seeds(
    const char *scene,
    vm_net_mock_scene_npcinfo_seed *seeds,
    u32 seedCap,
    u32 *totalOut,
    u32 *dynamicOut)
{
    vm_net_mock_scene_npc_request_cache *cache =
        &g_vm_net_mock_scene_npc_request_cache;
    u32 copyCount = 0;

    if (!cache->active)
    {
        return vm_net_mock_select_scene_npcinfo_seeds_uncached(
            scene, seeds, seedCap, totalOut, dynamicOut);
    }
    if (totalOut)
        *totalOut = 0;
    if (dynamicOut)
        *dynamicOut = 0;
    if (scene == NULL || seeds == NULL || seedCap == 0)
        return 0;

    if (!cache->loaded ||
        !vm_net_mock_scene_names_equal_exact(cache->scene, scene))
    {
        memset(cache->seeds, 0, sizeof(cache->seeds));
        snprintf(cache->scene, sizeof(cache->scene), "%s", scene);
        cache->selectedCount = vm_net_mock_select_scene_npcinfo_seeds_uncached(
            scene, cache->seeds, VM_NET_MOCK_SCENE_NPCINFO_MAX,
            &cache->totalCount, &cache->dynamicCount);
        cache->loaded = true;
        printf("[debug][mock-service] scene_npc_request_snapshot scene=%s selected=%u total=%u dynamic=%u\n",
               scene, cache->selectedCount, cache->totalCount,
               cache->dynamicCount);
    }
    copyCount = cache->selectedCount < seedCap ? cache->selectedCount : seedCap;
    memset(seeds, 0, sizeof(*seeds) * seedCap);
    memcpy(seeds, cache->seeds, sizeof(*seeds) * copyCount);
    if (totalOut)
        *totalOut = cache->totalCount;
    if (dynamicOut)
        *dynamicOut = cache->dynamicCount;
    return copyCount;
}

static bool vm_net_mock_task_accept(u32 roleId, u32 taskId,
                                    bool replaceCompletedState)
{
    char accountHex[129];
    char query[768];
    bool transactionStarted = false;

    if (roleId == 0 || taskId == 0 || !vm_net_mock_mysql_account_hex(accountHex))
        return false;
    if (replaceCompletedState)
    {
        if (!vm_mysql_exec("START TRANSACTION"))
            return false;
        transactionStarted = true;
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_tasks "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND task_id=%u "
                 "AND task_state=3",
                 accountHex, roleId, taskId);
        if (!vm_mysql_exec(query))
            goto failed;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_tasks"
             "(account_id,role_id,task_id,task_state,progress1,progress2) "
             "VALUES(CAST(X'%s' AS CHAR),%u,%u,1,0,0)",
             accountHex, roleId, taskId);
    if (!vm_mysql_exec(query))
        goto failed;
    if (!transactionStarted || vm_mysql_exec("COMMIT"))
        return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    return false;
}

static bool vm_net_mock_task_state_store(u32 roleId, u32 taskId, u8 state)
{
    char accountHex[129];
    char query[640];

    if (roleId == 0 || taskId == 0 || !vm_net_mock_mysql_account_hex(accountHex))
        return false;
    snprintf(query, sizeof(query),
             "UPDATE account_role_tasks SET task_state=%u "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND task_id=%u",
             state, accountHex, roleId, taskId);
    return vm_mysql_exec(query);
}

static bool vm_net_mock_task_state_restore(
    u32 roleId, const vm_net_mock_task_state_list_row *state)
{
    char accountHex[129];
    char query[768];

    if (roleId == 0 || state == NULL || state->taskId == 0 ||
        !vm_net_mock_mysql_account_hex(accountHex))
    {
        return false;
    }
    if (state->state == 3 && state->completedAt != 0)
    {
        /* A failed re-accept must restore the original completion instant;
         * otherwise the automatic ON UPDATE timestamp would silently extend
         * a daily/weekly/monthly cooldown. */
        snprintf(query, sizeof(query),
                 "UPDATE account_role_tasks SET task_state=%u,progress1=%u,progress2=%u,"
                 "updated_at=FROM_UNIXTIME(%u) WHERE account_id=CAST(X'%s' AS CHAR) "
                 "AND role_id=%u AND task_id=%u",
                 state->state, state->progress1, state->progress2,
                 state->completedAt, accountHex, roleId, state->taskId);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "UPDATE account_role_tasks SET task_state=%u,progress1=%u,progress2=%u "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND task_id=%u",
                 state->state, state->progress1, state->progress2,
                 accountHex, roleId, state->taskId);
    }
    return vm_mysql_exec(query);
}

static bool vm_net_mock_task_delete(u32 roleId, u32 taskId)
{
    char accountHex[129];
    char query[640];

    if (roleId == 0 || taskId == 0 || !vm_net_mock_mysql_account_hex(accountHex))
        return false;
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_tasks "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND task_id=%u "
             "AND task_state IN (1,2)",
             accountHex, roleId, taskId);
    return vm_mysql_exec(query);
}

/* A task can consume two collected materials and the item it gave when the
 * task was accepted.  Keep one canonical list for the persistence transaction,
 * capacity projection and the native 6/4 client-side deletion stream. */
enum
{
    VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX = 3,
    /* Every 6/4 iteminfo entry is a tagged i16 backpack sequence followed by
     * a tagged u8 remaining count. */
    VM_NET_MOCK_TASK_SUBMIT_ITEMINFO_MAX_BYTES =
        VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX * (4 + 3)
};

static bool vm_net_mock_task_collect_consumed_items(
    const vm_net_mock_task_definition *task,
    u32 itemIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    u32 itemCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    u8 *itemCountOut)
{
    const u32 candidateIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX] = {
        task != NULL && task->requirementType1 == 1 ? task->requirementId1 : 0,
        task != NULL && task->requirementType2 == 1 ? task->requirementId2 : 0,
        task != NULL ? task->givenItemId : 0};
    const u32 candidateCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX] = {
        task != NULL && task->requirementType1 == 1 ? task->requirementCount1 : 0,
        task != NULL && task->requirementType2 == 1 ? task->requirementCount2 : 0,
        task != NULL ? task->givenItemCount : 0};
    u8 itemCount = 0;

    if (itemCountOut != NULL)
        *itemCountOut = 0;
    if (task == NULL || itemIds == NULL || itemCounts == NULL)
        return false;
    memset(itemIds, 0, VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX * sizeof(*itemIds));
    memset(itemCounts, 0,
           VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX * sizeof(*itemCounts));
    for (u8 candidate = 0;
         candidate < VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX; ++candidate)
    {
        bool merged = false;

        if (candidateIds[candidate] == 0 || candidateCounts[candidate] == 0)
            continue;
        for (u8 index = 0; index < itemCount; ++index)
        {
            if (itemIds[index] != candidateIds[candidate])
                continue;
            if (0xffffffffu - itemCounts[index] < candidateCounts[candidate])
                return false;
            itemCounts[index] += candidateCounts[candidate];
            merged = true;
            break;
        }
        if (merged)
            continue;
        if (itemCount >= VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX)
            return false;
        itemIds[itemCount] = candidateIds[candidate];
        itemCounts[itemCount] = candidateCounts[candidate];
        ++itemCount;
    }
    if (itemCountOut != NULL)
        *itemCountOut = itemCount;
    return true;
}

static bool vm_net_mock_task_role_has_required_items(
    vm_net_mock_role_state *role, const vm_net_mock_task_definition *task)
{
    u32 itemIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u32 itemCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 itemCount = 0;

    if (role == NULL || task == NULL ||
        !vm_net_mock_task_collect_consumed_items(task, itemIds, itemCounts,
                                                 &itemCount))
    {
        return false;
    }
    for (u8 i = 0; i < itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = NULL;

        item = vm_net_mock_role_find_backpack_item(role, itemIds[i], 0);
        if (item == NULL || item->count < itemCounts[i])
            return false;
    }
    return true;
}

/* A task becomes submittable only when both protocol progress slots are done
 * and every item that its 6/4 commit will consume is still present.  This
 * makes a no-slot task with a given item a normal logistics hand-in rather
 * than inventing an unsupported requirement_type=3. */
static bool vm_net_mock_task_delivery_is_ready(
    vm_net_mock_role_state *role, const vm_net_mock_task_definition *task,
    u8 progress1, u8 progress2)
{
    return role != NULL && task != NULL &&
           progress1 >= task->requirementCount1 &&
           progress2 >= task->requirementCount2 &&
           vm_net_mock_task_role_has_required_items(role, task);
}

static bool vm_net_mock_task_backpack_can_receive(
    vm_net_mock_role_state *role, u32 itemId, u32 count,
    const vm_net_mock_task_definition *consumedByTask)
{
    u8 itemCount = 0;

    if (itemId == 0 || count == 0)
        return true;
    if (role == NULL)
        return false;
    if (vm_net_mock_role_find_backpack_item(role, itemId, 0) != NULL)
        return true;
    itemCount = vm_net_mock_role_backpack_count(role);
    if (itemCount < role->backpackCapacity &&
        itemCount < VM_NET_MOCK_BACKPACK_MAX_ITEMS)
    {
        return true;
    }
    if (consumedByTask != NULL)
    {
        u32 itemIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
        u32 itemCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
        u8 itemCount = 0;

        if (!vm_net_mock_task_collect_consumed_items(
                consumedByTask, itemIds, itemCounts, &itemCount))
        {
            return false;
        }
        for (u8 i = 0; i < itemCount; ++i)
        {
            vm_net_mock_backpack_item_state *item = NULL;

            item = vm_net_mock_role_find_backpack_item(role, itemIds[i], 0);
            if (item != NULL && item->count <= itemCounts[i])
                return true;
        }
    }
    return false;
}

static bool vm_net_mock_task_grant_accept_item(
    vm_net_mock_role_state *role, const vm_net_mock_task_definition *task)
{
    if (task == NULL || task->givenItemId == 0 || task->givenItemCount == 0)
        return true;
    if (!vm_net_mock_task_backpack_can_receive(role, task->givenItemId,
                                               task->givenItemCount, NULL))
    {
        return false;
    }
    return vm_net_mock_role_add_backpack_item(task->givenItemId,
                                              task->givenItemCount, NULL);
}

/* Capacity must be evaluated for the complete award, after task materials are
 * removed.  Checking each reward against the original backpack independently
 * lets a full backpack accept two distinct rewards even though it only has
 * one released slot.  The same in-memory mutation primitive used by bundled
 * shop operations gives this preflight the exact stack/equipment/reservoir
 * semantics of the real commit. */
static bool vm_net_mock_task_backpack_can_receive_rewards(
    const vm_net_mock_role_state *role,
    const vm_net_mock_task_definition *task)
{
    vm_net_mock_role_state projected;
    u32 consumedIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u32 consumedCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 consumedItemCount = 0;

    if (role == NULL || task == NULL ||
        !vm_net_mock_task_reward_items_are_valid(task) ||
        !vm_net_mock_task_collect_consumed_items(
            task, consumedIds, consumedCounts, &consumedItemCount))
    {
        return false;
    }
    projected = *role;
    for (u8 i = 0; i < consumedItemCount; ++i)
    {
        if (!vm_net_mock_role_consume_backpack_item(
                &projected, consumedIds[i], 0, consumedCounts[i], NULL))
        {
            return false;
        }
    }
    for (u8 i = 0; i < task->rewardItemNum; ++i)
    {
        if (!vm_net_mock_role_add_backpack_item_to_role_in_memory(
                &projected, task->rewardItems[i].itemId,
                task->rewardItems[i].count, NULL))
        {
            return false;
        }
    }
    return true;
}

/* `net_handle_task_response_dispatch` case 4 consumes a count-prefixed list
 * of tagged `(i16 sequence, u8 remaining)` iteminfo rows before awardinfo.
 * The server persistence step and this native client update must describe the
 * same rows; otherwise an accepted task item can remain visible until relogin.
 */
static bool vm_net_mock_task_consume_items(
    vm_net_mock_role_state *role, const vm_net_mock_task_definition *task,
    u16 consumedSeqOut[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    u8 consumedRemainingOut[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    u8 *consumedCountOut)
{
    u32 consumedIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u32 consumedCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 consumedItemCount = 0;

    if (consumedSeqOut != NULL)
        memset(consumedSeqOut, 0,
               VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX * sizeof(*consumedSeqOut));
    if (consumedRemainingOut != NULL)
        memset(consumedRemainingOut, 0,
               VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX * sizeof(*consumedRemainingOut));
    if (consumedCountOut != NULL)
        *consumedCountOut = 0;
    if (role == NULL || task == NULL ||
        !vm_net_mock_task_collect_consumed_items(
            task, consumedIds, consumedCounts, &consumedItemCount))
    {
        return false;
    }
    /* Validate every row before changing the role. The case-4 iteminfo count
     * reader is one byte wide, so do not silently truncate a visible stack. */
    for (u8 i = 0; i < consumedItemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item =
            vm_net_mock_role_find_backpack_item(role, consumedIds[i], 0);
        if (item == NULL || item->seq == 0 || item->count < consumedCounts[i] ||
            item->count - consumedCounts[i] > 0xffu)
        {
            return false;
        }
    }
    for (u8 i = 0; i < consumedItemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item =
            vm_net_mock_role_find_backpack_item(role, consumedIds[i], 0);
        u16 sequence = item->seq;
        u32 remaining = 0;

        if (!vm_net_mock_role_consume_backpack_item(
                role, consumedIds[i], 0, consumedCounts[i], &remaining))
        {
            return false;
        }
        if (consumedSeqOut != NULL)
            consumedSeqOut[i] = sequence;
        if (consumedRemainingOut != NULL)
            consumedRemainingOut[i] = (u8)remaining;
    }
    if (consumedCountOut != NULL)
        *consumedCountOut = consumedItemCount;
    return true;
}

/* task.dsh is an authored content source, but some historical rows award
 * more EXP than several whole levels on the current curve (for example, a
 * level-20 row awards 42,000).  Keep its zero/smaller rewards intact while
 * making the progression budget deterministic: one completion can grant at
 * most eight percent of the task's own level interval.  Using task.level,
 * rather than the recipient's current level, prevents over-levelled hand-ins
 * from scaling up and preserves the reward shown in awardinfo. */
enum { VM_NET_MOCK_TASK_EXP_REWARD_CAP_BASIS_POINTS = 800 };

static u32 vm_net_mock_task_effective_reward_exp(
    const vm_net_mock_task_definition *task)
{
    u32 taskLevel = 1;
    u32 interval = 0;
    u32 cap = 0;

    if (task == NULL || task->rewardExp == 0)
        return 0;
    taskLevel = task->level ? task->level : 1;
    if (taskLevel > VM_NET_MOCK_ROLE_LEVEL_CAP)
        taskLevel = VM_NET_MOCK_ROLE_LEVEL_CAP;
    interval = vm_net_mock_role_exp_interval_for_level(taskLevel);
    cap = (u32)(((unsigned long long)interval *
                 VM_NET_MOCK_TASK_EXP_REWARD_CAP_BASIS_POINTS + 9999ull) /
                10000ull);
    if (cap == 0)
        cap = 1;
    return task->rewardExp < cap ? task->rewardExp : cap;
}

static bool vm_net_mock_task_commit_reward(
    vm_net_mock_role_state *role, const vm_net_mock_task_definition *task,
    u16 rewardSeqOut[VM_NET_MOCK_TASK_REWARD_ITEM_MAX],
    u8 *rewardCountOut,
    u16 consumedSeqOut[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    u8 consumedRemainingOut[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    u8 *consumedCountOut)
{
    u32 rewardExp = 0;
    vm_net_mock_role_state before;

    if (rewardSeqOut != NULL)
        memset(rewardSeqOut, 0,
               VM_NET_MOCK_TASK_REWARD_ITEM_MAX * sizeof(*rewardSeqOut));
    if (rewardCountOut != NULL)
        *rewardCountOut = 0;
    if (consumedSeqOut != NULL)
        memset(consumedSeqOut, 0,
               VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX * sizeof(*consumedSeqOut));
    if (consumedRemainingOut != NULL)
        memset(consumedRemainingOut, 0,
               VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX *
                   sizeof(*consumedRemainingOut));
    if (consumedCountOut != NULL)
        *consumedCountOut = 0;
    if (role == NULL || task == NULL)
        return false;
    rewardExp = vm_net_mock_task_effective_reward_exp(task);
    if (!vm_net_mock_task_role_has_required_items(role, task) ||
        !vm_net_mock_task_backpack_can_receive_rewards(role, task))
    {
        return false;
    }
    if (!vm_net_mock_task_state_store(role->roleId, task->taskId, 3))
        return false;
    before = *role;
    if (!vm_net_mock_task_consume_items(
            role, task, consumedSeqOut, consumedRemainingOut, consumedCountOut))
    {
        *role = before;
        (void)vm_net_mock_task_state_store(role->roleId, task->taskId, 2);
        return false;
    }
    for (u8 i = 0; i < task->rewardItemNum; ++i)
    {
        u16 rewardSeq = 0;
        if (!vm_net_mock_role_add_backpack_item_to_role_in_memory(
                role, task->rewardItems[i].itemId,
                task->rewardItems[i].count, &rewardSeq) ||
            rewardSeq == 0)
        {
            *role = before;
            (void)vm_net_mock_task_state_store(role->roleId, task->taskId, 2);
            return false;
        }
        if (rewardSeqOut != NULL)
            rewardSeqOut[i] = rewardSeq;
    }

    (void)vm_net_mock_role_add_exp(role, rewardExp);
    role->money = (0xffffffffu - role->money < task->rewardMoney)
                      ? 0xffffffffu
                      : role->money + task->rewardMoney;
    vm_net_mock_role_normalize(role);
    if (!vm_net_mock_role_db_save("task-commit"))
    {
        *role = before;
        (void)vm_net_mock_task_state_store(role->roleId, task->taskId, 2);
        return false;
    }
    if (rewardCountOut != NULL)
        *rewardCountOut = task->rewardItemNum;
    printf("[info][network] mock_task_reward task=%u role=%u exp=%u raw_exp=%u money=%u items=%u first_item=%u first_type=%u first_count=%u consumed=%u\n",
           task->taskId, role->roleId, rewardExp, task->rewardExp,
           task->rewardMoney,
           task->rewardItemNum, task->rewardItemId, task->rewardItemType,
           task->rewardItemCount,
           consumedCountOut != NULL ? *consumedCountOut : 0);
    return true;
}

static bool vm_net_mock_build_task_submit_iteminfo(
    u8 *out, u32 outCap, u32 *blobLenOut,
    const u16 consumedSeqs[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    const u8 consumedRemainings[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    u8 consumedCount)
{
    u32 pos = 0;

    if (blobLenOut != NULL)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL ||
        consumedCount > VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX ||
        (consumedCount != 0 &&
         (consumedSeqs == NULL || consumedRemainings == NULL)))
    {
        return false;
    }
    for (u8 i = 0; i < consumedCount; ++i)
    {
        if (consumedSeqs[i] == 0 ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, consumedSeqs[i]) ||
            !vm_net_mock_seq_put_u8(out, outCap, &pos, consumedRemainings[i]))
        {
            return false;
        }
    }
    *blobLenOut = pos;
    return true;
}

/* JianghuOL.CBE:net_handle_task_response_dispatch(0x0104726C), case 4,
 * consumes awardinfo directly as a one-shot item-add stream after the reward
 * EXP/money fields.  This is intentionally not a 17/1 backpack page: the
 * task callback, rather than the backpack screen, owns this parser. */
static bool vm_net_mock_build_task_awardinfo(
    u8 *out, u32 outCap, u32 *blobLenOut,
    vm_net_mock_role_state *role,
    const vm_net_mock_task_definition *task,
    const u16 rewardSeqs[VM_NET_MOCK_TASK_REWARD_ITEM_MAX],
    u8 rewardCount)
{
    u32 pos = 0;
    u32 rewardExp = vm_net_mock_task_effective_reward_exp(task);
    u32 rewardMoney = task != NULL ? task->rewardMoney : 0;

    if (blobLenOut != NULL)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || role == NULL)
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, rewardExp) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, rewardMoney))
    {
        return false;
    }

    if (task == NULL || rewardCount == 0)
    {
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 0))
            return false;
        *blobLenOut = pos;
        return true;
    }

    if (rewardCount != task->rewardItemNum ||
        rewardCount > VM_NET_MOCK_TASK_REWARD_ITEM_MAX || rewardSeqs == NULL)
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, rewardCount))
        return false;
    for (u8 i = 0; i < rewardCount; ++i)
    {
        const vm_net_mock_task_reward_item *configured = &task->rewardItems[i];
        const vm_net_mock_backpack_item_state *rewardItem =
            vm_net_mock_role_find_backpack_item(role, configured->itemId,
                                                rewardSeqs[i]);
        u32 incrementalCount = 0;
        bool isReservoir =
            vm_net_mock_backpack_item_id_uses_reservoir_count(configured->itemId);

        if (rewardSeqs[i] == 0 || configured->itemId == 0 ||
            configured->count == 0 || rewardItem == NULL ||
            rewardItem->count == 0)
        {
            printf("[error][network] mock_task_awardinfo_invalid task=%u role=%u row=%u item=%u reward_seq=%u reason=missing-reward-row\n",
                   task->taskId, role->roleId, i, configured->itemId,
                   rewardSeqs[i]);
            return false;
        }
        /* Ordinary rows use this award's delta.  Reservoirs are independent
         * containers, whose client-side count is their initialized pool. */
        incrementalCount = isReservoir ? rewardItem->count : configured->count;
        if (incrementalCount == 0 || incrementalCount > 0x7fffffffu ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, rewardSeqs[i]) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, configured->itemId) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, incrementalCount) ||
            !vm_net_mock_seq_put_item_common_extra(
                out, outCap, &pos, configured->itemId,
                (u8)SDL_min(rewardItem->enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                vm_net_mock_item_common_extra_enhance_cap(
                    configured->itemId),
                &rewardItem->enhanceAffixes))
        {
            printf("[error][network] mock_task_awardinfo_invalid task=%u role=%u row=%u item=%u reward_seq=%u pos=%u cap=%u reason=serialize\n",
                   task->taskId, role->roleId, i, configured->itemId,
                   rewardSeqs[i], pos, outCap);
            return false;
        }
    }
    *blobLenOut = pos;
    return true;
}

static bool vm_net_mock_task_progress_store(u32 roleId, u32 taskId,
                                            u8 progress1, u8 progress2,
                                            u8 state)
{
    char accountHex[129];
    char query[768];

    if (roleId == 0 || taskId == 0 || !vm_net_mock_mysql_account_hex(accountHex))
        return false;
    snprintf(query, sizeof(query),
             "UPDATE account_role_tasks SET progress1=%u,progress2=%u,task_state=%u "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND task_id=%u "
             "AND task_state=1",
             progress1, progress2, state, accountHex, roleId, taskId);
    return vm_mysql_exec(query);
}

/* `6/3 taskinfo` is client-to-server only, and `1/6/2` is only its result
 * acknowledgement.  A progress change therefore cannot be represented by a
 * synthetic task-response object.  Use the already client-proven system
 * message queue, delivered by the following scene-sync poll, instead of
 * contaminating the battle response or pretending that the task was turned
 * in. */
static bool vm_net_mock_task_progress_enqueue_notice(
    const vm_net_mock_task_definition *task,
    bool progress1Advanced, u8 requirementType1, u32 progress1,
    u32 requirementCount1, bool progress2Advanced, u8 requirementType2,
    u32 progress2, u32 requirementCount2)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    static const char killLabelGbk[] = "\xBB\xF7\xC9\xB1"; /* 击杀 */
    static const char collectLabelGbk[] = "\xCA\xD5\xBC\xAF"; /* 收集 */
    const char *label1 = requirementType1 == 1 ? collectLabelGbk : killLabelGbk;
    const char *label2 = requirementType2 == 1 ? collectLabelGbk : killLabelGbk;
    char message[82];

    if (task == NULL || task->name[0] == 0 || session == NULL ||
        (!progress1Advanced && !progress2Advanced))
    {
        return false;
    }
    if (progress1Advanced && progress2Advanced)
    {
        snprintf(message, sizeof(message),
                 "\xC8\xCE\xCE\xF1\xBD\xF8\xB6\xC8\xA3\xBA%s %s%u/%u\xA1\xA2%s%u/%u",
                 task->name, label1, progress1, requirementCount1,
                 label2, progress2, requirementCount2);
    }
    else if (progress1Advanced)
    {
        snprintf(message, sizeof(message),
                 "\xC8\xCE\xCE\xF1\xBD\xF8\xB6\xC8\xA3\xBA%s %s%u/%u",
                 task->name, label1, progress1, requirementCount1);
    }
    else
    {
        snprintf(message, sizeof(message),
                 "\xC8\xCE\xCE\xF1\xBD\xF8\xB6\xC8\xA3\xBA%s %s%u/%u",
                 task->name, label2, progress2, requirementCount2);
    }
    return vm_mock_service_session_enqueue_system_message(session, message);
}

static void vm_net_mock_task_progress_after_battle(u32 enemyId,
                                                   u32 enemyCount,
                                                   u32 dropItemId,
                                                   u32 dropCount)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *currentScene = vm_net_mock_current_scene_name();
    vm_net_mock_task_state_list_row states[VM_NET_MOCK_TASK_CATALOG_MAX];
    u32 stateCount = 0;

    /* A battle victory and each granted loot stack are distinct events.  The
     * old code only ran from the loot loop, silently leaving kill-only tasks
     * at state 1 whenever the monster had no drop (and counting a kill once
     * per drop when it did). */
    if (role == NULL ||
        ((enemyId == 0 || enemyCount == 0) &&
         (dropItemId == 0 || dropCount == 0)) ||
        !vm_net_mock_load_task_catalog() ||
        !vm_net_mock_task_state_list_load(role->roleId, true, states,
                                          VM_NET_MOCK_TASK_CATALOG_MAX,
                                          &stateCount))
    {
        return;
    }
    for (u32 i = 0; i < stateCount; ++i)
    {
        const vm_net_mock_task_definition *task =
            vm_net_mock_task_catalog_find_by_id(states[i].taskId);
        u32 progress1 = states[i].progress1;
        u32 progress2 = states[i].progress2;
        u32 previousProgress1 = progress1;
        u32 previousProgress2 = progress2;
        bool changed = false;
        bool progress1Advanced = false;
        bool progress2Advanced = false;
        bool progressNoticeQueued = false;
        u8 nextState = 1;

        if (task == NULL || states[i].state != 1)
            continue;
        if (enemyId != 0 && enemyCount != 0 &&
            vm_net_mock_task_battle_requirement_matches(
                task, 1, enemyId, currentScene))
        {
            progress1 = vm_net_mock_min_u32(progress1 + enemyCount,
                                            task->requirementCount1);
            if (progress1 != previousProgress1)
            {
                changed = true;
                progress1Advanced = true;
            }
        }
        else if (dropItemId != 0 && dropCount != 0 &&
                 task->requirementType1 == 1 &&
                 task->requirementId1 == dropItemId)
        {
            progress1 = vm_net_mock_min_u32(progress1 + dropCount,
                                            task->requirementCount1);
            if (progress1 != previousProgress1)
            {
                changed = true;
                progress1Advanced = true;
            }
        }
        if (enemyId != 0 && enemyCount != 0 &&
            vm_net_mock_task_battle_requirement_matches(
                task, 2, enemyId, currentScene))
        {
            progress2 = vm_net_mock_min_u32(progress2 + enemyCount,
                                            task->requirementCount2);
            if (progress2 != previousProgress2)
            {
                changed = true;
                progress2Advanced = true;
            }
        }
        else if (dropItemId != 0 && dropCount != 0 &&
                 task->requirementType2 == 1 &&
                 task->requirementId2 == dropItemId)
        {
            progress2 = vm_net_mock_min_u32(progress2 + dropCount,
                                            task->requirementCount2);
            if (progress2 != previousProgress2)
            {
                changed = true;
                progress2Advanced = true;
            }
        }
        if (!changed)
            continue;
        if (progress1 >= task->requirementCount1 &&
            progress2 >= task->requirementCount2)
        {
            nextState = 2;
        }
        if (vm_net_mock_task_progress_store(role->roleId, task->taskId,
                                            (u8)progress1, (u8)progress2,
                                            nextState))
        {
            progressNoticeQueued = vm_net_mock_task_progress_enqueue_notice(
                task, progress1Advanced, task->requirementType1, progress1,
                task->requirementCount1, progress2Advanced,
                task->requirementType2, progress2, task->requirementCount2);
            if (nextState == 2)
            {
                vm_mock_service_session_arm_task_prompt_refresh(
                    vm_net_mock_current_scene_name());
            }
            printf("[info][network] mock_task_battle_progress task=%u role=%u scene=%s enemy=%u enemies=%u drop=%u drop_count=%u progress=%u/%u,%u/%u state=%u progress_notice=%u\n",
                   task->taskId, role->roleId, currentScene ? currentScene : "-",
                   enemyId, enemyCount,
                   dropItemId, dropCount,
                   progress1, task->requirementCount1,
                   progress2, task->requirementCount2, nextState,
                   progressNoticeQueued ? 1u : 0u);
        }
    }
}

static bool vm_net_mock_append_test_task_record(u8 *out, u32 outCap, u32 *pos,
                                                u8 state, u8 progress1, u8 progress2)
{
    /* ParseItemDataFields(0x01046D24) consumes this exact tagged sequence.
     * Type 1 + id 65535 deliberately keeps the test task active without
     * accidentally matching ordinary inventory changes. */
    return vm_net_mock_seq_put_u32(out, outCap, pos, VM_NET_MOCK_TEST_TASK_ID) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_string(out, outCap, pos,
                                      "\xb2\xe2\xca\xd4\xc8\xce\xce\xf1") && /* 测试任务 */
           vm_net_mock_seq_put_string(out, outCap, pos,
                                      "\xc8\xce\xce\xf1\xca\xb9\xd5\xdf") && /* 任务使者 */
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_string(out, outCap, pos,
                                      "\xc8\xce\xce\xf1\xca\xb9\xd5\xdf") &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 1) &&
           vm_net_mock_seq_put_u32(out, outCap, pos, 65535) &&
           vm_net_mock_seq_put_string(out, outCap, pos,
                                      "\xb2\xe2\xca\xd4\xc6\xbe\xd6\xa4") && /* 测试凭证 */
           vm_net_mock_seq_put_u8(out, outCap, pos, progress1) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 1) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_u32(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_string(out, outCap, pos, "") &&
           vm_net_mock_seq_put_u8(out, outCap, pos, progress2) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, state);
}

static bool vm_net_mock_append_test_task_candidate_record(u8 *out, u32 outCap,
                                                          u32 *pos)
{
    /* DeserializeRoleInfo(0x01046E00) expands this stream into one 76-byte
     * available-task row.  scene_refresh_interact_prompt_types(0x01017C6C)
     * compares the second string (row+37) with scene-node+68 and assigns
     * prompt type 2, the client's normal exclamation mark. */
    return vm_net_mock_seq_put_u32(out, outCap, pos, VM_NET_MOCK_TEST_TASK_ID) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_string(out, outCap, pos,
                                      "\xb2\xe2\xca\xd4\xc8\xce\xce\xf1") && /* 测试任务 */
           vm_net_mock_seq_put_string(out, outCap, pos,
                                      "\xc8\xce\xce\xf1\xca\xb9\xd5\xdf") && /* 任务使者 */
           vm_net_mock_seq_put_u8(out, outCap, pos, 1);
}

static bool vm_net_mock_append_catalog_task_record(
    u8 *out, u32 outCap, u32 *pos,
    const vm_net_mock_task_definition *task,
    const char *receiverOverride,
    u8 state, u8 progress1, u8 progress2)
{
    const char *receiver = receiverOverride && receiverOverride[0]
                               ? receiverOverride
                               : (task ? task->receiver : "");

    if (task == NULL)
        return false;
    /* ParseItemDataFields(0x01046D24) owns 32/16/16-byte fixed strings at
     * row+4/+36/+52. task.dsh maxima are 18/11/11 bytes, so the catalog values
     * fit without the overrun that malformed synthetic records caused. */
    return vm_net_mock_seq_put_u32(out, outCap, pos, task->taskId) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->difficulty) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->classification) &&
           vm_net_mock_seq_put_string(out, outCap, pos, task->name) &&
           vm_net_mock_seq_put_string(out, outCap, pos, task->giver) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->level) &&
           vm_net_mock_seq_put_string(out, outCap, pos, receiver) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->requirementType1) &&
           vm_net_mock_seq_put_u32(out, outCap, pos, task->requirementId1) &&
           vm_net_mock_seq_put_string(out, outCap, pos, "") &&
           vm_net_mock_seq_put_u8(out, outCap, pos, progress1) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->requirementCount1) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->requirementType2) &&
           vm_net_mock_seq_put_u32(out, outCap, pos, task->requirementId2) &&
           vm_net_mock_seq_put_string(out, outCap, pos, "") &&
           vm_net_mock_seq_put_u8(out, outCap, pos, progress2) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->requirementCount2) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, 0) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, state);
}

static const char *vm_net_mock_task_prompt_receiver_for_scene(
    const vm_net_mock_task_definition *task,
    const char *scene,
    char *resolved,
    size_t resolvedCap)
{
    if (resolved != NULL && resolvedCap != 0)
        resolved[0] = 0;
    /* `task.dsh` gives every valid task a declared receiver. Scene-slot
     * selection controls visibility only; it must never replace the owner of
     * an active task with its offer NPC when a receiver is not selected. */
    (void)scene;
    return task != NULL ? task->receiver : "";
}

/* A dynamic binding owns offers. Delivery is exclusively the task definition's
 * declared receiver, so prompt-slot fallback and XSE offer aliases cannot
 * authorize a different NPC to submit it. */
static bool vm_net_mock_task_delivery_matches_scene_npc(
    const vm_net_mock_task_definition *task,
    const vm_net_mock_scene_npcinfo_seed *seed,
    const char *scene)
{
    if (task == NULL || task->receiver[0] == 0 || seed == NULL ||
        seed->displayName[0] == 0)
        return false;
    (void)scene;
    return strcmp(task->receiver, seed->displayName) == 0;
}

static bool vm_net_mock_append_catalog_task_candidate_record(
    u8 *out, u32 outCap, u32 *pos,
    const vm_net_mock_task_definition *task,
    const char *sceneNpcName)
{
    const char *giver = sceneNpcName && sceneNpcName[0] ? sceneNpcName :
                        (task ? task->giver : "");

    if (task == NULL || strlen(giver) >= 32)
        return false;
    /* DeserializeRoleInfo(0x01046E00): id, difficulty, classification,
     * 32-byte task name, 32-byte scene-node name, level.  The scene-node name
     * deliberately comes from the SCE actor row so prompt matching survives
     * old task.dsh giver suffixes such as “-天机”. */
    return vm_net_mock_seq_put_u32(out, outCap, pos, task->taskId) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->difficulty) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->classification) &&
           vm_net_mock_seq_put_string(out, outCap, pos, task->name) &&
           vm_net_mock_seq_put_string(out, outCap, pos, giver) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, task->level);
}

static bool vm_net_mock_append_task_state_object(u8 *out, u32 outCap, u32 *pos,
                                                 u32 taskId, u8 state)
{
    u8 taskState[16];
    u32 taskStateLen = 0;
    u32 objectStart = 0;

    memset(taskState, 0, sizeof(taskState));
    if (!vm_net_mock_seq_put_u32(taskState, sizeof(taskState), &taskStateLen, taskId) ||
        !vm_net_mock_seq_put_u8(taskState, sizeof(taskState), &taskStateLen, state) ||
        !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 6, 6, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "tasknum", 1) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "taskstate",
                                    taskState, (u16)taskStateLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_task_read_tagged_u32(const u8 *data, u32 dataLen, u32 *valueOut)
{
    if (valueOut)
        *valueOut = 0;
    if (data == NULL || dataLen != 6 || data[0] != 0 || data[1] != 4)
        return false;
    if (valueOut)
    {
        *valueOut = ((u32)data[2] << 24) | ((u32)data[3] << 16) |
                    ((u32)data[4] << 8) | (u32)data[5];
    }
    return true;
}

static bool vm_net_mock_task_read_progress_blob(const u8 *data, u32 dataLen,
                                                u32 *taskIdOut,
                                                u8 *progress1Out,
                                                u8 *progress2Out)
{
    u32 taskId = 0;

    if (taskIdOut)
        *taskIdOut = 0;
    if (progress1Out)
        *progress1Out = 0;
    if (progress2Out)
        *progress2Out = 0;
    /* UpdateTaskProgress(0x01047ACE) writes taskinfo with the tagged stream
     * callbacks: u32 task id, u8 first progress, u8 second progress. */
    if (data == NULL || dataLen != 12 ||
        data[0] != 0 || data[1] != 4 ||
        data[6] != 0 || data[7] != 1 ||
        data[9] != 0 || data[10] != 1)
    {
        return false;
    }
    taskId = ((u32)data[2] << 24) | ((u32)data[3] << 16) |
             ((u32)data[4] << 8) | (u32)data[5];
    if (taskId == 0)
        return false;
    if (taskIdOut)
        *taskIdOut = taskId;
    if (progress1Out)
        *progress1Out = data[8];
    if (progress2Out)
        *progress2Out = data[11];
    return true;
}

static const char *vm_net_mock_npc_dialog_text(u32 actorId)
{
    switch (actorId)
    {
    case 20020: /* 欧冶子 */
        return "\xd6\xfd\xbd\xa3\xbd\xb2\xbe\xbf\xbb\xf0\xba\xf2\xba\xcd\xb2\xc4\xc1\xcf\xa3\xac\xc9\xd9\xcf\xc0\xc8\xf4\xd3\xd0\xb1\xf8\xc6\xf7\xc9\xcf\xb5\xc4\xca\xc2\xa3\xac\xbf\xc9\xd2\xd4\xc0\xb4\xd5\xd2\xce\xd2\xa1\xa3";
    case 20021: /* 小猴子 */
        return "\xd6\xa8\xd6\xa8\xa3\xa1\xd0\xa1\xba\xef\xd7\xd3\xb3\xe5\xc4\xe3\xd5\xa3\xc1\xcb\xd5\xa3\xd1\xdb\xa1\xa3";
    case VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID: /* 任务使者 */
        return "\xc9\xd9\xcf\xc0\xa3\xac\xce\xd2\xd5\xe2\xc0\xef\xd3\xd0\xd2\xbb\xcf\xee\xc8\xce\xce\xf1\xbf\xc9\xd2\xd4\xb9\xa9\xc4\xe3\xb2\xe2\xca\xd4\xa1\xa3";
    case 20090: /* 王朝：04临安王朝.xse 的常态对白 */
        return "\xbf\xbf\xa3\xac\xc0\xcf\xb4\xf3\xb0\xae\xcc\xfd\xb0\xfc\xb9\xab\xb4\xab\xa3\xac\xbe\xcd\xd3\xb2\xb1\xc6\xce\xd2\xc3\xc7\xb8\xc4\xc1\xcb\xc3\xfb\xd7\xd6\xc8\xc3\xcb\xfb\xd2\xb2\xb9\xfd\xb9\xfd\xb0\xfc\xb9\xab\xf1\xab\xa1\xa3";
    case 20091: /* 马汉：04临安马汉.xse 的常态对白 */
        return "\xc7\xb0\xc3\xe6\xca\xc7\xbb\xca\xb9\xac\xd6\xd8\xb5\xd8\xa3\xa1";
    case 20092: /* 胡斐：04临安胡斐.xse 的常态对白 */
        return "\xc8\xcb\xd4\xda\xbd\xad\xba\xfe\xc6\xae\xa3\xac\xc4\xc4\xc4\xdc\xb2\xbb\xb0\xa4\xb5\xb6\xa3\xbf\xb0\xa4\xb5\xb6\xb2\xbb\xd3\xc3\xc5\xc2\xa3\xac\xbc\xd7\xba\xf1\xc8\xcb\xb2\xbb\xb9\xd2\xa3\xa1";
    default:
        return "\xc9\xd9\xcf\xc0\xa3\xac\xd3\xd0\xca\xb2\xc3\xb4\xca\xc2\xc2\xf0\xa3\xbf"; /* 少侠，有什么事吗？ */
    }
}

static bool vm_net_mock_npc_service_kind_uses_inventory(u16 serviceKind)
{
    return serviceKind == VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT ||
           serviceKind == VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT ||
           serviceKind == VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT;
}

/* Keep visible labels coupled to the already implemented private type=2
 * service values.  The database may only override name/description; it never
 * supplies an arbitrary client action or opcode. */
static bool vm_net_mock_npc_service_option_default(
    const vm_net_mock_scene_npcinfo_seed *seed, u16 serviceKind,
    const char **nameOut, const char **descriptionOut, u32 *valueOut)
{
    const char *name = NULL;
    const char *description = NULL;
    u32 value = 0;

    if (seed == NULL || nameOut == NULL || descriptionOut == NULL ||
        valueOut == NULL)
    {
        return false;
    }
    switch (serviceKind)
    {
    case VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT:
        name = "\xb2\xe9\xbf\xb4\xbf\xc9\xb9\xba\xc2\xf2\xce\xe4\xc6\xf7"; /* 查看可购买武器 */
        description = "\xce\xe4\xc6\xf7\xc9\xcc\xb5\xea"; /* 武器商店 */
        value = VM_NET_MOCK_NPC_SERVICE_OPEN_WEAPON;
        break;
    case VM_NET_MOCK_NPC_KIND_EQUIPMENT_REPAIR:
        name = "\xd0\xde\xc0\xed\xc8\xab\xb2\xbf\xd7\xb0\xb1\xb8"; /* 修理全部装备 */
        description = "\xd7\xb0\xb1\xb8\xd0\xde\xc0\xed"; /* 装备修理 */
        value = VM_NET_MOCK_NPC_SERVICE_REPAIR_ALL;
        break;
    case VM_NET_MOCK_NPC_KIND_SKILL_TRAINER:
        name = "\xbc\xbc\xc4\xdc\xd0\xde\xcf\xb0"; /* 技能修习 */
        description = "\xbc\xbc\xc4\xdc\xb5\xbc\xca\xa6"; /* 技能导师 */
        value = VM_NET_MOCK_NPC_SERVICE_OPEN_SKILLS;
        break;
    case VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT:
        name = "\xb9\xba\xc2\xf2\xb7\xc0\xbe\xdf"; /* 购买防具 */
        description = "\xb7\xc0\xbe\xdf\xc9\xcc\xb5\xea"; /* 防具商店 */
        value = VM_NET_MOCK_NPC_SERVICE_OPEN_ARMOR;
        break;
    case VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT:
        name = "\xb9\xba\xc2\xf2\xd2\xa9\xc6\xb7"; /* 购买药品 */
        description = "\xd2\xa9\xc6\xb7\xc9\xcc\xb5\xea"; /* 药品商店 */
        value = VM_NET_MOCK_NPC_SERVICE_OPEN_MEDICINE;
        break;
    case VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE:
        if (seed->actorId > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK)
            return false;
        /* A configured destination is a transport service.  Point the first
         * action=1 selection straight at the parser-backed ENTER_INSTANCE
         * request so the client receives its 30/1 scene-enter response
         * immediately, rather than rendering a second operation dialog. */
        if (seed->instanceScene[0] != 0)
        {
            name = "\xbd\xf8\xc8\xeb\xb8\xb1\xb1\xbe"; /* 进入副本 */
            description = "\xd6\xb1\xbd\xd3\xb4\xab\xcb\xcd\xb5\xbd\xb8\xb1\xb1\xbe\xb3\xa1\xbe\xb0"; /* 直接传送到副本场景 */
            value = VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE |
                    seed->actorId;
        }
        else
        {
            /* Compatibility projection for old challenge-only kind-6 rows.
             * New admin saves use INSTANCE_CHALLENGE below. */
            name = "\xcc\xf4\xd5\xbd\xca\xd8\xb9\xd8\xb9\xd6"; /* 挑战守关怪 */
            description = "\xd6\xb1\xbd\xd3\xbf\xaa\xca\xbc\xd5\xbd\xb6\xb7"; /* 直接开始战斗 */
            value = VM_NET_MOCK_NPC_SERVICE_OPEN_INSTANCE_BASE |
                    seed->actorId;
        }
        break;
    case VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE:
        if (seed->actorId > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK)
            return false;
        name = "\xcc\xf4\xd5\xbd\xca\xd8\xb9\xd8\xb9\xd6"; /* 挑战守关怪 */
        description = "\xd6\xb1\xbd\xd3\xbf\xaa\xca\xbc\xd5\xbd\xb6\xb7"; /* 直接开始战斗 */
        /* The dialog serializer replaces this private value with the guarded
         * action-13 enemy id.  Keep a supported value here so a malformed
         * caller cannot turn an unknown opcode into a service action. */
        value = VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE |
                seed->actorId;
        break;
    case VM_NET_MOCK_NPC_KIND_EQUIPMENT_BUYER:
        name = "\xb3\xf6\xca\xdb\xd7\xb0\xb1\xb8"; /* 出售装备 */
        description = "\xd7\xb0\xb1\xb8\xbb\xd8\xca\xd5"; /* 装备回收 */
        value = VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE;
        break;
    case VM_NET_MOCK_NPC_KIND_ARENA_MASTER:
        name = "\xbf\xaa\xc9\xe8\xc0\xde\xcc\xa8"; /* 开设擂台 */
        description = "\xc9\xe8\xd6\xc3\xb1\xc8\xce\xe4\xb2\xce\xca\xfd"; /* 设置比武参数 */
        value = VM_NET_MOCK_NPC_SERVICE_OPEN_ARENA_CREATE;
        break;
    case VM_NET_MOCK_NPC_KIND_MAILBOX:
        name = "\xd3\xca\xcf\xe4"; /* 邮箱 */
        description = "\xc1\xec\xc8\xa1\xcf\xb5\xcd\xb3\xbd\xb1\xc0\xf8"; /* 领取系统奖励 */
        value = VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX;
        break;
    case VM_NET_MOCK_NPC_KIND_CRYSTAL_SYNTHESIS:
        name = "\xd0\xfe\xbe\xa7\xba\xcf\xb3\xc9"; /* 玄晶合成 */
        description = "\xca\xae\xb8\xf6\xd0\xfe\xbe\xa7\xcb\xe9\xc6\xac\xba\xcf\xb3\xc9\xd2\xbb\xbc\xb6\xa3\xbb\xcd\xac\xbc\xb6\xc8\xfd\xb8\xf6\xba\xcf\xb3\xc9\xb8\xdf\xd2\xbb\xbc\xb6"; /* 十个玄晶碎片合成一级；同级三个合成高一级 */
        value = VM_NET_MOCK_NPC_SERVICE_OPEN_CRYSTAL_SYNTHESIS_BASE;
        break;
    default:
        return false;
    }
    *nameOut = name;
    *descriptionOut = description;
    *valueOut = value;
    return true;
}

static bool vm_net_mock_npc_service_is_direct_instance_challenge(
    const vm_net_mock_scene_npcinfo_seed *seed, u16 serviceKind)
{
    if (seed == NULL || seed->challengeEnemyId == 0)
        return false;
    /* Action13 is a current-scene request and cannot own a target-instance
     * battle shell.  A destination-bearing NPC must use its normal 30/1
     * entry and the target SCE's collision path instead.  Keep the legacy
     * direct guard only for an NPC with no configured destination. */
    return seed->instanceScene[0] == 0 &&
           (serviceKind == VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE ||
            serviceKind == VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE);
}

static void vm_net_mock_npc_service_context_record(
    vm_mock_service_client_session *session, const vm_net_mock_role_state *role,
    const char *scene, const vm_net_mock_scene_npcinfo_seed *seed,
    u32 serviceMask)
{
    if (session == NULL)
        return;
    /* Opening any NPC starts a new authority scope, so a confirm option from
     * the previous merchant cannot survive into this dialog. */
    memset(&session->npcTransactionContext, 0,
           sizeof(session->npcTransactionContext));
    memset(&session->npcServiceContext, 0,
           sizeof(session->npcServiceContext));
    if (role == NULL || scene == NULL || seed == NULL || seed->actorId == 0 ||
        serviceMask == 0 ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return;
    }
    session->npcServiceContext.active = true;
    session->npcServiceContext.roleId = role->roleId;
    session->npcServiceContext.actorId = seed->actorId;
    session->npcServiceContext.serviceMask = serviceMask;
    snprintf(session->npcServiceContext.scene,
             sizeof(session->npcServiceContext.scene), "%s", scene);
}

bool vm_net_mock_npc_service_context_has(
    const vm_mock_service_npc_context *context, u16 serviceKind)
{
    u32 bit = vm_net_mock_npc_service_kind_mask(serviceKind);

    return context != NULL && bit != 0 &&
           (context->serviceMask & bit) != 0;
}

const vm_mock_service_npc_context *
vm_net_mock_npc_service_context_get(const vm_mock_service_client_session *session,
                                    const vm_net_mock_role_state *role)
{
    const char *scene = vm_net_mock_current_scene_name();

    if (session == NULL || role == NULL ||
        !session->npcServiceContext.active ||
        session->npcServiceContext.roleId != role->roleId || scene == NULL ||
        strcmp(session->npcServiceContext.scene, scene) != 0)
    {
        return NULL;
    }
    return &session->npcServiceContext;
}

static void vm_net_mock_npc_transaction_context_clear(
    vm_mock_service_client_session *session)
{
    if (session != NULL)
    {
        memset(&session->npcTransactionContext, 0,
               sizeof(session->npcTransactionContext));
    }
}

static bool vm_net_mock_npc_transaction_context_begin(
    vm_mock_service_client_session *session, const vm_net_mock_role_state *role,
    const vm_mock_service_npc_context *serviceContext, u8 kind, u32 itemId,
    u16 backpackSeq, u32 selector, u32 page, u32 quotedPrice)
{
    vm_mock_service_npc_transaction_context *transaction = NULL;

    if (session == NULL || role == NULL || serviceContext == NULL ||
        (kind != VM_MOCK_SERVICE_NPC_TRANSACTION_BUY &&
         kind != VM_MOCK_SERVICE_NPC_TRANSACTION_SELL &&
         kind != VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN &&
         kind != VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_FORGET &&
         kind != VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS &&
         kind != VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO) ||
        itemId == 0 ||
        ((kind == VM_MOCK_SERVICE_NPC_TRANSACTION_BUY ||
          kind == VM_MOCK_SERVICE_NPC_TRANSACTION_SELL ||
          kind == VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO) &&
         quotedPrice == 0) ||
        (kind == VM_MOCK_SERVICE_NPC_TRANSACTION_SELL && backpackSeq == 0) ||
        !vm_net_mock_scene_name_is_safe(serviceContext->scene))
    {
        return false;
    }
    transaction = &session->npcTransactionContext;
    memset(transaction, 0, sizeof(*transaction));
    transaction->active = true;
    transaction->kind = kind;
    transaction->roleId = role->roleId;
    transaction->actorId = serviceContext->actorId;
    transaction->serviceMask = serviceContext->serviceMask;
    transaction->itemId = itemId;
    transaction->backpackSeq = backpackSeq;
    transaction->selector = selector;
    transaction->page = page;
    transaction->quotedPrice = quotedPrice;
    snprintf(transaction->scene, sizeof(transaction->scene), "%s",
             serviceContext->scene);
    return true;
}

/* Consume the context even on a failed revalidation.  A private action value
 * is not a durable transaction token and must never be replayable. */
static bool vm_net_mock_npc_transaction_context_take(
    vm_mock_service_client_session *session, const vm_net_mock_role_state *role,
    const vm_mock_service_npc_context *serviceContext,
    vm_mock_service_npc_transaction_context *transactionOut)
{
    vm_mock_service_npc_transaction_context transaction;
    bool valid = false;

    if (transactionOut != NULL)
        memset(transactionOut, 0, sizeof(*transactionOut));
    if (session == NULL)
        return false;
    transaction = session->npcTransactionContext;
    vm_net_mock_npc_transaction_context_clear(session);
    valid = role != NULL && serviceContext != NULL && transaction.active &&
            (transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_BUY ||
             transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_SELL ||
             transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN ||
             transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_FORGET ||
             transaction.kind ==
                 VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS ||
             transaction.kind ==
                 VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO) &&
            transaction.roleId == role->roleId &&
            transaction.actorId == serviceContext->actorId &&
            transaction.serviceMask == serviceContext->serviceMask &&
            strcmp(transaction.scene, serviceContext->scene) == 0;
    if (!valid)
        return false;
    if (transactionOut != NULL)
        *transactionOut = transaction;
    return true;
}

static bool vm_net_mock_npc_shop_selector_allowed_for_service(
    u32 selector, u32 serviceMask)
{
    /* A dynamic NPC can expose several merchant services.  Authorize by the
     * requested catalog category, not by the first service bit encountered;
     * otherwise a weapon+armor NPC can open the armor menu but its later
     * purchase request is incorrectly rejected as weapon-only. */
    if (selector >= 8u && selector <= 10u)
    {
        return (serviceMask & vm_net_mock_npc_service_kind_mask(
                                  VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT)) != 0;
    }
    if (selector >= 1u && selector <= 7u)
    {
        return (serviceMask & vm_net_mock_npc_service_kind_mask(
                                  VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT)) != 0;
    }
    return selector == VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR &&
           (serviceMask & vm_net_mock_npc_service_kind_mask(
                              VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT)) != 0;
}

static u32 vm_net_mock_build_npc_dialog_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    vm_net_mock_scene_npcinfo_seed seeds[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    const vm_net_mock_scene_npcinfo_seed *matchedSeed = NULL;
    const char *scene = vm_net_mock_current_scene_name();
    const char *dialogText = NULL;
    u32 actorId = 0;
    u32 index = 0;
    u32 totalCount = 0;
    u32 dynamicCount = 0;
    u32 seedCount = 0;
    vm_net_mock_role_state *activeRole = NULL;
    vm_net_mock_task_state_row taskState;
    vm_net_mock_task_state_list_row allTaskStates[VM_NET_MOCK_TASK_CATALOG_MAX];
    u32 allTaskStateCount = 0;
    vm_net_mock_xse_summary xseSummary;
    const vm_net_mock_task_definition *optionTasks[VM_NET_MOCK_XSE_TASK_REF_MAX];
    bool optionSubmits[VM_NET_MOCK_XSE_TASK_REF_MAX];
    u32 optionCount = 0;
    u32 completedTaskIds[VM_NET_MOCK_XSE_TASK_REF_MAX];
    u32 completedTaskCount = 0;
    bool taskAlreadyAccepted = false;
    bool taskCompletedNow = false;
    bool showTaskOption = false;
    vm_net_mock_npc_service_option
        configuredServices[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
    u32 configuredServiceCount = 0;
    u32 emittedServiceCount = 0;
    u32 emittedServiceMask = 0;
    bool directChallengeServiceEmitted = false;
    bool directChallengeServiceConfigured = false;
    bool directChallengeNodeReady = false;
    bool directChallengeUnavailable = false;
    bool servicesConfigured = false;
    u32 taskEntryCount = 0;
    u8 dialog[3072];
    u32 dialogLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (!vm_net_mock_is_npc_dialog_request(request, requestLen, &actorId, &index) ||
        out == NULL || outCap < pos)
    {
        return 0;
    }
    /* A type=2 subrequest carries no NPC identity.  Clear any previous menu
     * authorization before building this response so a malformed/oversized
     * new menu can never retain a stale actor's service mask. */
    vm_net_mock_npc_service_context_record(session, NULL, NULL, NULL, 0);
    /* A new NPC dialog also invalidates a previous action-13 authorization.
     * The subsequent battle request contains only a monster id, so letting a
     * prior dialog survive would make its NPC origin ambiguous. */
    if (session != NULL)
    {
        session->instanceChallengePending = false;
        session->instanceChallengeBattlePending = false;
        session->instanceChallengeDirectPending = false;
        session->instanceChallengeDirectSceneMonster = false;
        session->instanceChallengeActorId = 0;
        session->instanceChallengeEnemyId = 0;
        session->instanceChallengeSceneIndex = 0;
        session->instanceChallengeX = 0;
        session->instanceChallengeY = 0;
        session->instanceChallengeTick = 0;
        session->instanceChallengeScene[0] = 0;
    }

    memset(seeds, 0, sizeof(seeds));
    seedCount = vm_net_mock_collect_scene_npcinfo_seeds(scene, seeds,
                                                       VM_NET_MOCK_SCENE_NPC_CATALOG_MAX,
                                                       &totalCount, &dynamicCount);
    for (u32 i = 0; i < seedCount; ++i)
    {
        if (seeds[i].actorId == actorId)
        {
            matchedSeed = &seeds[i];
            break;
        }
    }
    if (matchedSeed != NULL)
        vm_net_mock_task_interaction_context_reset();
    dialogText = vm_net_mock_npc_dialog_text(actorId);
    memset(&taskState, 0, sizeof(taskState));
    memset(&xseSummary, 0, sizeof(xseSummary));
    memset(allTaskStates, 0, sizeof(allTaskStates));
    memset(optionTasks, 0, sizeof(optionTasks));
    memset(optionSubmits, 0, sizeof(optionSubmits));
    memset(completedTaskIds, 0, sizeof(completedTaskIds));
    memset(configuredServices, 0, sizeof(configuredServices));
    activeRole = vm_net_mock_active_role();
    if (matchedSeed != NULL && matchedSeed->scriptName[0] != 0 &&
        vm_net_mock_load_xse_summary(matchedSeed->scriptName, &xseSummary) &&
        xseSummary.idleDialog[0] != 0)
    {
        dialogText = xseSummary.idleDialog;
    }
    if (activeRole != NULL)
    {
        (void)vm_net_mock_task_state_list_load(activeRole->roleId, false,
                                               allTaskStates,
                                               VM_NET_MOCK_TASK_CATALOG_MAX,
                                               &allTaskStateCount);
    }
    if (actorId == VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID)
    {
        if (activeRole != NULL &&
            vm_net_mock_task_state_load(activeRole->roleId,
                                        VM_NET_MOCK_TEST_TASK_ID,
                                        &taskState) &&
            taskState.found)
        {
            taskAlreadyAccepted = true;
            if (taskState.state == 1 &&
                vm_net_mock_task_state_store(activeRole->roleId,
                                             VM_NET_MOCK_TEST_TASK_ID, 2))
            {
                taskState.state = 2;
                taskCompletedNow = true;
                dialogText =
                    "\xb2\xe2\xca\xd4\xc4\xbf\xb1\xea\xd2\xd1\xcd\xea\xb3\xc9\xa3\xac\xc7\xeb\xd4\xd9\xb4\xce"
                    "\xd3\xeb\xce\xd2\xbd\xbb\xcc\xb8\xcc\xe1\xbd\xbb\xc8\xce\xce\xf1\xa1\xa3"; /* 测试目标已完成，请再次与我交谈提交任务。 */
            }
            else if (taskState.state == 2)
            {
                showTaskOption = true;
                dialogText =
                    "\xd5\xe2\xcf\xee\xb2\xe2\xca\xd4\xc8\xce\xce\xf1\xd2\xd1\xcd\xea\xb3\xc9\xa3\xac\xbf\xc9\xd2\xd4"
                    "\xcc\xe1\xbd\xbb\xc1\xcb\xa1\xa3"; /* 这项测试任务已完成，可以提交了。 */
            }
            else
            {
                dialogText =
                    "\xd5\xe2\xcf\xee\xb2\xe2\xca\xd4\xc8\xce\xce\xf1\xd2\xd1\xbe\xad\xbd\xe1\xca\xf8\xa1\xa3"; /* 这项测试任务已经结束。 */
            }
        }
        else
        {
            showTaskOption = true;
        }
    }

    if (matchedSeed != NULL && activeRole != NULL && xseSummary.loaded &&
        matchedSeed->taskId == 0)
    {
        for (u32 refIndex = 0; refIndex < xseSummary.taskRefCount; ++refIndex)
        {
            const vm_net_mock_xse_task_ref *ref = &xseSummary.taskRefs[refIndex];
            const vm_net_mock_task_definition *task =
                vm_net_mock_task_catalog_find_by_id(ref->taskId);
            const vm_net_mock_task_state_list_row *persisted =
                vm_net_mock_task_state_list_find(allTaskStates,
                                                 allTaskStateCount,
                                                 ref->taskId);
            u8 state = persisted ? persisted->state : 0;
            u8 progress1 = persisted ? persisted->progress1 : 0;
            u8 progress2 = persisted ? persisted->progress2 : 0;
            bool requirementsDone = false;

            if (task == NULL)
                continue;
            requirementsDone = vm_net_mock_task_delivery_is_ready(
                activeRole, task, progress1, progress2);
            /* A completion marker in this XSE means the clicked actor owns the
             * delivery branch. A logistics task has both progress thresholds
             * at zero, but its given item remains a required hand-in. */
            if (state == 1 && ref->completed && requirementsDone &&
                vm_net_mock_task_state_store(activeRole->roleId, task->taskId, 2))
            {
                state = 2;
                if (completedTaskCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
                    completedTaskIds[completedTaskCount++] = task->taskId;
            }
            if (state == 0 && ref->offer &&
                vm_net_mock_task_definition_available(task, activeRole,
                                                      allTaskStates,
                                                      allTaskStateCount,
                                                      VM_NET_MOCK_TASK_REPEAT_NEVER) &&
                optionCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
            {
                optionTasks[optionCount] = task;
                optionSubmits[optionCount] = false;
                optionCount += 1;
            }
            else if (state == 2 && ref->completed &&
                     optionCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
            {
                optionTasks[optionCount] = task;
                optionSubmits[optionCount] = true;
                optionCount += 1;
            }
        }
    }

    /* Offer bindings are one-to-one with NPCs, whereas delivery belongs to
     * each task's receiver. Scan active task rows separately so an NPC may
     * deliver an earlier task while offering a later task in the same chain. */
    if (matchedSeed != NULL && activeRole != NULL)
    {
        for (u32 stateIndex = 0;
             stateIndex < allTaskStateCount &&
             optionCount < VM_NET_MOCK_XSE_TASK_REF_MAX;
             ++stateIndex)
        {
            const vm_net_mock_task_state_list_row *persisted =
                &allTaskStates[stateIndex];
            const vm_net_mock_task_definition *task =
                vm_net_mock_task_catalog_find_by_id(persisted->taskId);
            u8 state = persisted->state;
            bool duplicate = false;
            bool requirementsDone = false;

            if (task == NULL || (state != 1 && state != 2) ||
                !vm_net_mock_task_delivery_matches_scene_npc(task, matchedSeed,
                                                              scene))
            {
                continue;
            }
            for (u32 optionIndex = 0; optionIndex < optionCount; ++optionIndex)
            {
                if (optionTasks[optionIndex] != NULL &&
                    optionTasks[optionIndex]->taskId == task->taskId)
                {
                    duplicate = true;
                    break;
                }
            }
            requirementsDone = vm_net_mock_task_delivery_is_ready(
                activeRole, task, persisted->progress1, persisted->progress2);
            if (state == 1 && requirementsDone &&
                vm_net_mock_task_state_store(activeRole->roleId, task->taskId, 2))
            {
                state = 2;
                if (completedTaskCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
                    completedTaskIds[completedTaskCount++] = task->taskId;
            }
            if (state == 2 && !duplicate &&
                optionCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
            {
                optionTasks[optionCount] = task;
                optionSubmits[optionCount] = true;
                optionCount += 1;
            }
            if (state == 1)
                dialogText = task->activeDialog[0] != 0
                                 ? task->activeDialog
                                 : "\xc8\xce\xce\xf1\xbb\xb9\xd4\xda\xbd\xf8\xd0\xd0\xd6\xd0\xa3\xac\xc7\xeb\xcd\xea\xb3\xc9\xc4\xbf\xb1\xea\xba\xf3\xd4\xd9\xc0\xb4\xa1\xa3";
            else
                dialogText = task->completedDialog[0] != 0
                                 ? task->completedDialog
                                 : "\xc8\xce\xce\xf1\xd2\xd1\xbe\xad\xcd\xea\xb3\xc9\xa3\xac\xbf\xc9\xd2\xd4\xcc\xe1\xbd\xbb\xc1\xcb\xa1\xa3";
        }
    }

    if (xseSummary.loaded && xseSummary.taskRefCount == 1)
    {
        bool hasOffer = false;
        bool hasSubmit = false;

        for (u32 optionIndex = 0; optionIndex < optionCount; ++optionIndex)
        {
            if (optionSubmits[optionIndex])
                hasSubmit = true;
            else
                hasOffer = true;
        }
        if (hasSubmit && xseSummary.completedDialog[0] != 0)
            dialogText = xseSummary.completedDialog;
        else if (completedTaskCount != 0 && xseSummary.completedDialog[0] != 0)
            dialogText = xseSummary.completedDialog;
        else if (hasOffer && xseSummary.offerDialog[0] != 0)
            dialogText = xseSummary.offerDialog;
    }

    if (matchedSeed != NULL && matchedSeed->taskId != 0 && activeRole != NULL)
    {
        const vm_net_mock_task_definition *task =
            vm_net_mock_task_catalog_find_by_id(matchedSeed->taskId);
        const vm_net_mock_task_state_list_row *persisted =
            vm_net_mock_task_state_list_find(allTaskStates,
                                             allTaskStateCount,
                                             matchedSeed->taskId);
        u8 state = persisted ? persisted->state : 0;
        u8 progress1 = persisted ? persisted->progress1 : 0;
        u8 progress2 = persisted ? persisted->progress2 : 0;
        bool duplicate = false;
        bool deliveryMatches = false;
        bool directOfferAvailable = false;
        char unavailableTaskText[128];

        memset(unavailableTaskText, 0, sizeof(unavailableTaskText));

        for (u32 optionIndex = 0; optionIndex < optionCount; ++optionIndex)
        {
            if (optionTasks[optionIndex] != NULL &&
                optionTasks[optionIndex]->taskId == matchedSeed->taskId)
            {
                duplicate = true;
                break;
            }
        }
        deliveryMatches = vm_net_mock_task_delivery_matches_scene_npc(
            task, matchedSeed, scene);
        if (task != NULL && (state == 0 ||
                             (state == 3 &&
                              vm_net_mock_task_repeat_policy_from_seed(
                                  matchedSeed) != VM_NET_MOCK_TASK_REPEAT_NEVER)))
        {
            directOfferAvailable = vm_net_mock_task_definition_available(
                task, activeRole, allTaskStates, allTaskStateCount,
                vm_net_mock_task_repeat_policy_from_seed(matchedSeed));
        }
        if (task != NULL && !duplicate &&
            optionCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
        {
            if (deliveryMatches && state == 1 &&
                vm_net_mock_task_delivery_is_ready(activeRole, task, progress1,
                                                    progress2) &&
                vm_net_mock_task_state_store(activeRole->roleId, task->taskId, 2))
            {
                state = 2;
                if (completedTaskCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
                    completedTaskIds[completedTaskCount++] = task->taskId;
            }
            if (directOfferAvailable)
            {
                optionTasks[optionCount] = task;
                optionSubmits[optionCount] = false;
                optionCount += 1;
            }
            else if (state == 2 && deliveryMatches)
            {
                optionTasks[optionCount] = task;
                optionSubmits[optionCount] = true;
                optionCount += 1;
            }
        }
        if (task != NULL)
        {
            if (directOfferAvailable)
                dialogText = task->offerDialog[0] != 0
                                 ? task->offerDialog
                                 : "\xce\xd2\xd5\xe2\xc0\xef\xd3\xd0\xd2\xbb\xcf\xee\xc8\xce\xce\xf1\xa3\xac\xc4\xe3\xd4\xb8\xd2\xe2\xb0\xef\xc3\xa6\xc2\xf0\xa3\xbf";
            /* Do not fabricate an action when the authoritative predicate
             * declined this offer.  The regular NPC-dialog field is the
             * parser-backed place to explain why this actor has no task
             * option; only use it when no other XSE task action is present. */
            else if (optionCount == 0 &&
                     (state == 0 ||
                      (state == 3 &&
                       vm_net_mock_task_repeat_policy_from_seed(matchedSeed) !=
                           VM_NET_MOCK_TASK_REPEAT_NEVER)) &&
                     vm_net_mock_task_definition_unavailable_reason(
                         task, activeRole, allTaskStates, allTaskStateCount,
                         vm_net_mock_task_repeat_policy_from_seed(matchedSeed), NULL,
                         unavailableTaskText,
                         sizeof(unavailableTaskText)) != NULL)
                dialogText = unavailableTaskText;
            else if (state == 1)
                dialogText = task->activeDialog[0] != 0
                                 ? task->activeDialog
                                 : "\xc8\xce\xce\xf1\xbb\xb9\xd4\xda\xbd\xf8\xd0\xd0\xd6\xd0\xa3\xac\xc7\xeb\xcd\xea\xb3\xc9\xc4\xbf\xb1\xea\xba\xf3\xd4\xd9\xc0\xb4\xa1\xa3";
            else if (state == 2 && deliveryMatches)
                dialogText = task->completedDialog[0] != 0
                                 ? task->completedDialog
                                 : "\xc8\xce\xce\xf1\xd2\xd1\xbe\xad\xcd\xea\xb3\xc9\xa3\xac\xbf\xc9\xd2\xd4\xcc\xe1\xbd\xbb\xc1\xcb\xa1\xa3";
        }
    }

    if (matchedSeed != NULL &&
        !vm_net_mock_npc_service_options_resolve(
            scene, matchedSeed->actorId, matchedSeed->kind,
            matchedSeed->serviceOptionName,
            matchedSeed->serviceOptionDescription, configuredServices,
            VM_NET_MOCK_NPC_SERVICE_OPTION_MAX, &configuredServiceCount,
            &servicesConfigured))
    {
        /* Dialog text and task actions remain valid even when an admin-owned
         * service configuration is unavailable.  Do not invent a fallback
         * opcode from incomplete data; emit no action-1 services instead. */
        configuredServiceCount = 0;
        servicesConfigured = false;
        printf("[error][mock-admin] npc_service_options_resolve_failed scene=%s actor=%u error=%s\n",
               scene ? scene : "-", matchedSeed->actorId,
               vm_mysql_last_error());
    }

    /* action 13 is only valid when the client can resolve the configured
     * enemy to a live type-2 scene node.  Its request builder sends that node
     * index in 4/1; advertising action 13 before the SCE kind-3 record has
     * been installed makes the client send index=0 and leaves its loading
     * state waiting for a 4/5 battle that cannot legally be built. */
    if (matchedSeed != NULL && matchedSeed->challengeEnemyId != 0)
    {
        for (u32 serviceIndex = 0;
             serviceIndex < configuredServiceCount; ++serviceIndex)
        {
            if (vm_net_mock_npc_service_is_direct_instance_challenge(
                    matchedSeed, configuredServices[serviceIndex].kind))
            {
                directChallengeServiceConfigured = true;
                break;
            }
        }
        if (directChallengeServiceConfigured)
        {
            directChallengeNodeReady = vm_net_mock_select_sce_combat_spawn(
                scene, matchedSeed->challengeEnemyId, NULL, NULL, NULL);
            if (!directChallengeNodeReady)
            {
                printf("[warn][network] mock_npc_direct_challenge_withheld "
                       "scene=%s actor=%u enemy=%u "
                       "action=omit-action13 reason=live-kind3-node-unready "
                       "evidence=SendNPCInteractReq:0x01037ED4-index-required+"
                       "mmBattle:0x66CC\n",
                       scene ? scene : "-", matchedSeed->actorId,
                       matchedSeed->challengeEnemyId);
            }
        }
    }

    taskEntryCount =
        (actorId == VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID && showTaskOption ? 1u : 0u) +
        optionCount;
    if (taskEntryCount > VM_NET_MOCK_NPC_DIALOG_MAX_OPTIONS)
    {
        /* Task entries retain priority because their action=4 progression is
         * already active.  Trim only the tail in the same deterministic order
         * used to build optionTasks. */
        optionCount = VM_NET_MOCK_NPC_DIALOG_MAX_OPTIONS -
                      (actorId == VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID &&
                               showTaskOption
                           ? 1u
                           : 0u);
        taskEntryCount = VM_NET_MOCK_NPC_DIALOG_MAX_OPTIONS;
    }
    for (u32 serviceIndex = 0;
         serviceIndex < configuredServiceCount &&
         taskEntryCount + emittedServiceCount < VM_NET_MOCK_NPC_DIALOG_MAX_OPTIONS;
         ++serviceIndex)
    {
        const char *defaultName = NULL;
        const char *defaultDescription = NULL;
        u32 serviceValue = 0;

        if (!vm_net_mock_npc_service_option_default(
                matchedSeed, configuredServices[serviceIndex].kind,
                &defaultName, &defaultDescription, &serviceValue) ||
            serviceValue == 0)
        {
            continue;
        }
        /* Older rows could persist both a target scene and the former direct
         * guard service.  Do not expose its action-1 fallback: that invokes
         * the already disproved action13/4-10 owner path.  A subsequent admin
         * save rejects this combination and retains the target's selected
         * collision monster under instance_spawn_enemy_id. */
        if (matchedSeed != NULL && matchedSeed->instanceScene[0] != 0 &&
            configuredServices[serviceIndex].kind ==
                VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE)
        {
            continue;
        }
        if (directChallengeServiceConfigured && !directChallengeNodeReady &&
            vm_net_mock_npc_service_is_direct_instance_challenge(
                matchedSeed, configuredServices[serviceIndex].kind))
        {
            directChallengeUnavailable = true;
            continue;
        }
        /* Arena's native UI has two mutually exclusive entry modes: 30/4 is
         * creation and 30/3 is the room list.  One service configuration
         * intentionally projects to two action-1 rows, provided there is room
         * for both after higher-priority task options. */
        emittedServiceCount +=
            configuredServices[serviceIndex].kind ==
                    VM_NET_MOCK_NPC_KIND_ARENA_MASTER &&
                taskEntryCount + emittedServiceCount + 1 <
                    VM_NET_MOCK_NPC_DIALOG_MAX_OPTIONS
                ? 2u
                : 1u;
        emittedServiceMask |= vm_net_mock_npc_service_kind_mask(
            configuredServices[serviceIndex].kind);
    }
    if (directChallengeUnavailable && taskEntryCount == 0 &&
        emittedServiceCount == 0)
    {
        dialogText =
            "\xcc\xf4\xd5\xbd\xc4\xbf\xb1\xea\xc9\xd0\xce\xb4\xb2\xbf\xca\xf0\xa3\xac"
            "\xc7\xeb\xcd\xea\xd5\xfb\xcd\xcb\xb3\xf6\xba\xf3\xd6\xd8\xd0\xc2\xbd\xf8\xc8\xeb\xb3\xa1\xbe\xb0\xa1\xa3"; /* 挑战目标尚未部署，请完整退出后重新进入场景。 */
    }
    /* ParseNPCDialogData(0x010380E8) consumes the raw sequence as:
     * dialog-kind:u8, main-text:string, option-count:u8, then each option as
     * display-type:u8/name:string/action:u8/value:u32/description:string,
     * followed by button-count:u8.  The parser stores action at option+44;
     * task_hall_activate_selected_entry(0x010492B0) switches that byte and only
     * action 4 enters the 6/10 task-detail path.  A completed task exposes the
     * same action again; the client derives request state 3 from its active row
     * and subsequently sends 6/4 to commit it. */
    memset(dialog, 0, sizeof(dialog));
    if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 0) ||
        !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen, dialogText) ||
        !vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen,
                                taskEntryCount + emittedServiceCount))
    {
        return 0;
    }
    if (actorId == VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID && showTaskOption)
    {
        if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 4) ||
            !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                        taskAlreadyAccepted
                                            ? "\xcc\xe1\xbd\xbb\xb2\xe2\xca\xd4\xc8\xce\xce\xf1" /* 提交测试任务 */
                                            : "\xbd\xd3\xca\xdc\xb2\xe2\xca\xd4\xc8\xce\xce\xf1") || /* 接受测试任务 */
            !vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 4) ||
            !vm_net_mock_seq_put_u32(dialog, sizeof(dialog), &dialogLen,
                                     VM_NET_MOCK_TEST_TASK_ID) ||
            !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                        "\xb2\xe9\xbf\xb4\xc8\xce\xce\xf1\xcf\xea\xc7\xe9")) /* 查看任务详情 */
        {
            return 0;
        }
    }
    for (u32 serviceIndex = 0, emitted = 0;
         serviceIndex < configuredServiceCount && emitted < emittedServiceCount;
         ++serviceIndex)
    {
        const char *defaultName = NULL;
        const char *defaultDescription = NULL;
        const char *serviceName = NULL;
        const char *serviceDescription = NULL;
        u32 serviceValue = 0;
        u8 serviceAction = 1;
        bool directChallengeService = false;

        if (!vm_net_mock_npc_service_option_default(
                matchedSeed, configuredServices[serviceIndex].kind,
                &defaultName, &defaultDescription, &serviceValue) ||
            serviceValue == 0)
        {
            continue;
        }
        if (matchedSeed != NULL && matchedSeed->instanceScene[0] != 0 &&
            configuredServices[serviceIndex].kind ==
                VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE)
        {
            continue;
        }
        serviceName = configuredServices[serviceIndex].optionName[0] != 0
                          ? configuredServices[serviceIndex].optionName
                          : defaultName;
        serviceDescription =
            configuredServices[serviceIndex].optionDescription[0] != 0
                ? configuredServices[serviceIndex].optionDescription
                : defaultDescription;
        /* A dedicated guard-challenge service is encoded as the native
         * action-13 option in this first dialog.  The next client action is
         * the real 4/1 battle request, never a second NPC dialog or a
         * 30/9 confirmation window. */
        directChallengeService =
            vm_net_mock_npc_service_is_direct_instance_challenge(
                matchedSeed, configuredServices[serviceIndex].kind);
        if (directChallengeService && !directChallengeNodeReady)
            continue;
        if (directChallengeService)
        {
            if (configuredServices[serviceIndex].optionName[0] == 0)
                serviceName = "\xcc\xf4\xd5\xbd\xca\xd8\xb9\xd8\xb9\xd6"; /* 挑战守关怪 */
            if (configuredServices[serviceIndex].optionDescription[0] == 0)
                serviceDescription = "\xd6\xb1\xbd\xd3\xbf\xaa\xca\xbc\xd5\xbd\xb6\xb7"; /* 直接开始战斗 */
            serviceAction = 13;
            serviceValue = matchedSeed->challengeEnemyId;
        }
        if (configuredServices[serviceIndex].kind ==
            VM_NET_MOCK_NPC_KIND_ARENA_MASTER)
        {
            /* The configured custom label remains the create label.  Challenge
             * is a distinct task-hall action and must use its own documented
             * service value, otherwise the handler cannot select mode 27. */
            if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 4) ||
                !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                            serviceName) ||
                !vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 1) ||
                !vm_net_mock_seq_put_u32(dialog, sizeof(dialog), &dialogLen,
                                         VM_NET_MOCK_NPC_SERVICE_OPEN_ARENA_CREATE) ||
                !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                            serviceDescription))
            {
                return 0;
            }
            ++emitted;
            if (emitted >= emittedServiceCount)
                continue;
            if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 4) ||
                !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                            "\xcc\xf4\xd5\xbd\xc0\xde\xcc\xa8") || /* 挑战擂台 */
                !vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 1) ||
                !vm_net_mock_seq_put_u32(dialog, sizeof(dialog), &dialogLen,
                                         VM_NET_MOCK_NPC_SERVICE_OPEN_ARENA_CHALLENGE) ||
                !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                            "\xb2\xe9\xbf\xb4\xbf\xc9\xcc\xf4\xd5\xbd\xb5\xc4\xc0\xde\xcc\xa8")) /* 查看可挑战的擂台 */
            {
                return 0;
            }
            ++emitted;
            continue;
        }
        if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 4) ||
            !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                        serviceName) ||
            !vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen,
                                    serviceAction) ||
            !vm_net_mock_seq_put_u32(dialog, sizeof(dialog), &dialogLen,
                                     serviceValue) ||
            !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                        serviceDescription))
        {
            return 0;
        }
        if (directChallengeService)
            directChallengeServiceEmitted = true;
        ++emitted;
    }
    for (u32 optionIndex = 0; optionIndex < optionCount; ++optionIndex)
    {
        const vm_net_mock_task_definition *task = optionTasks[optionIndex];
        char optionName[64];
        const char *prefix = optionSubmits[optionIndex]
                                 ? "\xcc\xe1\xbd\xbb" /* 提交 */
                                 : "\xbd\xd3\xca\xdc"; /* 接受 */

        snprintf(optionName, sizeof(optionName), "%s%s", prefix, task->name);
        if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 4) ||
            !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen, optionName) ||
            !vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 4) ||
            !vm_net_mock_seq_put_u32(dialog, sizeof(dialog), &dialogLen, task->taskId) ||
            !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen, task->goal))
        {
            return 0;
        }
    }
    if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 0))
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 26, 1, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "hidebtn", 0) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "dialog", dialog, (u16)dialogLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (taskCompletedNow &&
        !vm_net_mock_append_task_state_object(out, outCap, &pos,
                                              VM_NET_MOCK_TEST_TASK_ID, 2))
    {
        return 0;
    }
    for (u32 completedIndex = 0; completedIndex < completedTaskCount; ++completedIndex)
    {
        if (!vm_net_mock_append_task_state_object(out, outCap, &pos,
                                                  completedTaskIds[completedIndex], 2))
        {
            return 0;
        }
    }
    vm_net_mock_finish_wt_packet(out, pos,
                                 (u8)(1u + (taskCompletedNow ? 1u : 0u) +
                                      completedTaskCount));
    /* A nested private service request does not carry actor identity.  Bind
     * only the concrete subset actually written into the successfully
     * encoded dialog; clipped/invalid rows must not be callable merely
     * because they exist in SQL. */
    vm_net_mock_npc_service_context_record(
        session, activeRole, scene,
        emittedServiceMask != 0 ? matchedSeed : NULL, emittedServiceMask);
    if (directChallengeServiceEmitted && matchedSeed != NULL &&
        session != NULL && vm_net_mock_scene_name_is_safe(scene))
    {
        /* action=13 sends only the enemy id.  Bind it to the dialog actor and
         * visible scene until the client emits 4/1; the battle handler then
         * validates this binding before it starts the exact SCE live node. */
        session->instanceChallengeDirectPending = true;
        session->instanceChallengeDirectSceneMonster = true;
        session->instanceChallengeActorId = matchedSeed->actorId;
        session->instanceChallengeEnemyId = matchedSeed->challengeEnemyId;
        session->instanceChallengeSceneIndex = 0;
        session->instanceChallengeX = matchedSeed->instanceX != 0
                                          ? matchedSeed->instanceX
                                          : matchedSeed->x;
        session->instanceChallengeY = matchedSeed->instanceY != 0
                                          ? matchedSeed->instanceY
                                          : matchedSeed->y;
        session->instanceChallengeTick = g_schedulerTick;
        snprintf(session->instanceChallengeScene,
                 sizeof(session->instanceChallengeScene), "%s", scene);
    }
    /* The later action=4 task request contains only task id. Record exactly
     * the options in the delivered dialog after every packet object has been
     * built, preserving whether that id was authorized for acceptance or
     * submission by this clicked actor. */
    for (u32 optionIndex = 0; optionIndex < optionCount; ++optionIndex)
    {
        const vm_net_mock_task_definition *task = optionTasks[optionIndex];
        u8 repeatPolicy = VM_NET_MOCK_TASK_REPEAT_NEVER;

        if (!optionSubmits[optionIndex] && matchedSeed != NULL &&
            matchedSeed->taskId == task->taskId)
        {
            repeatPolicy = vm_net_mock_task_repeat_policy_from_seed(matchedSeed);
        }

        vm_net_mock_task_interaction_context_record(
            task->taskId, actorId, repeatPolicy,
            optionSubmits[optionIndex]
                ? VM_MOCK_SERVICE_TASK_INTERACTION_SUBMIT
                : VM_MOCK_SERVICE_TASK_INTERACTION_OFFER,
            scene);
    }

    printf("[info][network] mock_npc_dialog actor=%u index=%u name=%s script=%s scene=%s catalog_match=%u legacy_service_kind=%u scene_entity_kind=%u native=%u service_configured=%u service_count=%u service_mask=%08x direct_challenge=%u task_options=%u task_accepted=%u task_state=%u task_completed_now=%u task_option_action=%u xse_dialogs=%u dialog_len=%u objects=%u resp=%u evidence=JianghuOL.CBE:0x01037ED4+0x010380E8+0x010492B0(action1/action4/action13)+0x0104726C(case6)\n",
           actorId,
           index,
           matchedSeed && matchedSeed->displayName[0] ? matchedSeed->displayName : "-",
           matchedSeed && matchedSeed->scriptName[0] ? matchedSeed->scriptName : "-",
           scene ? scene : "-",
           matchedSeed ? 1u : 0u,
           matchedSeed ? matchedSeed->kind : 0u,
           matchedSeed ? matchedSeed->sceneEntityKind : 0u,
           matchedSeed && matchedSeed->nativeSceneActor ? 1u : 0u,
           servicesConfigured ? 1u : 0u,
           emittedServiceCount,
           emittedServiceMask,
           directChallengeServiceEmitted ? 1u : 0u,
           (actorId == VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID && showTaskOption ? 1u : 0u) + optionCount,
           taskAlreadyAccepted ? 1u : 0u,
           taskState.state,
           taskCompletedNow ? 1u : 0u,
           (actorId == VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID && showTaskOption) || optionCount != 0 ? 4u : 0u,
           xseSummary.directDialogCount,
           dialogLen,
           1u + (taskCompletedNow ? 1u : 0u) + completedTaskCount,
           pos);
    vm_autotest_note("mock_npc_dialog actor=%u index=%u catalog_match=%u dialog_len=%u response=26/1 evidence=JianghuOL.CBE:0x01037ED4+0x010380E8\n",
                     actorId, index, matchedSeed ? 1u : 0u, dialogLen);
    return pos;
}

static bool vm_net_mock_npc_shop_selector_is_valid(u32 selector)
{
    return (selector >= 1u && selector <= 10u) ||
           selector == VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR;
}

static u32 vm_net_mock_npc_shop_selector_for_item(
    const vm_net_mock_shop_catalog_item *item)
{
    u8 slot = 0xff;

    if (item == NULL)
        return 0;
    if (!item->isEquip)
        return item->category == 10
                   ? VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR
                   : 0;
    slot = vm_net_mock_equipment_slot_for_category(item->category);
    return slot < VM_NET_MOCK_EQUIP_SLOT_COUNT
               ? (u32)item->category + 1u
               : 0;
}

static bool vm_net_mock_npc_shop_item_matches_selector(
    const vm_net_mock_shop_catalog_item *item, u32 selector,
    const vm_mock_service_npc_context *context, u32 *unitPriceOut)
{
    const vm_net_mock_npc_shop_inventory_row *inventory = NULL;

    if (unitPriceOut)
        *unitPriceOut = 0;
    /* `server_shop_items.enabled` controls placement in the global mall.
     * A configured NPC inventory is a separate selling channel: its own
     * enabled flag is the availability authority.  Reusing the mall flag
     * here makes a fully enabled private merchant look empty whenever the
     * same DSH item is hidden from the mall. */
    if (item == NULL || item->itemId > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK ||
        context == NULL ||
        (!vm_net_mock_npc_service_context_has(
             context, VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT) &&
         !vm_net_mock_npc_service_context_has(
             context, VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT) &&
         !vm_net_mock_npc_service_context_has(
             context, VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT)) ||
        !vm_net_mock_npc_shop_selector_allowed_for_service(
            selector, context->serviceMask))
    {
        return false;
    }
    inventory = vm_net_mock_npc_shop_inventory_find_exact(
        context->scene, context->actorId, item->itemId);
    if (inventory == NULL || !inventory->enabled || inventory->unitPrice == 0)
        return false;
    if (selector == VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR)
    {
        if (item->isEquip || item->category != 10)
            return false;
    }
    else if (!(selector >= 1u && selector <= 10u && item->isEquip &&
               item->category == selector - 1u &&
               vm_net_mock_equipment_slot_for_category(item->category) <
                   VM_NET_MOCK_EQUIP_SLOT_COUNT))
    {
        return false;
    }
    if (unitPriceOut)
        *unitPriceOut = inventory->unitPrice;
    return true;
}

static u32 vm_net_mock_npc_shop_selector_total(
    u32 selector, const vm_mock_service_npc_context *context)
{
    u32 total = 0;

    if (!vm_net_mock_npc_shop_selector_is_valid(selector))
        return 0;
    for (u32 i = 0; i < vm_net_mock_load_shop_catalog(); ++i)
    {
        if (vm_net_mock_npc_shop_item_matches_selector(
                &g_vm_net_mock_shop_catalog[i], selector, context, NULL))
        {
            ++total;
        }
    }
    return total;
}

static const vm_net_mock_shop_catalog_item *
vm_net_mock_npc_shop_selector_item_at(
    u32 selector, u32 ordinal, const vm_mock_service_npc_context *context,
    u32 *unitPriceOut)
{
    u32 seen = 0;

    if (unitPriceOut)
        *unitPriceOut = 0;
    if (!vm_net_mock_npc_shop_selector_is_valid(selector))
        return NULL;
    for (u32 i = 0; i < vm_net_mock_load_shop_catalog(); ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        u32 unitPrice = 0;

        if (!vm_net_mock_npc_shop_item_matches_selector(item, selector,
                                                        context, &unitPrice))
            continue;
        if (seen++ == ordinal)
        {
            if (unitPriceOut)
                *unitPriceOut = unitPrice;
            return item;
        }
    }
    return NULL;
}

/* The native action=1 item request carries only itemId, not the page that
 * contained the row.  Recover that page from the same authoritative ordered
 * catalog used to build the visible list before storing the confirmation
 * context. */
static u32 vm_net_mock_npc_shop_item_page(
    const vm_net_mock_shop_catalog_item *item, u32 selector,
    const vm_mock_service_npc_context *context)
{
    u32 total = 0;

    if (item == NULL || !vm_net_mock_npc_shop_selector_is_valid(selector) ||
        context == NULL)
    {
        return 0;
    }
    total = vm_net_mock_npc_shop_selector_total(selector, context);
    for (u32 ordinal = 0; ordinal < total; ++ordinal)
    {
        const vm_net_mock_shop_catalog_item *candidate =
            vm_net_mock_npc_shop_selector_item_at(selector, ordinal, context,
                                                  NULL);
        if (candidate == item)
            return ordinal / VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
    }
    return 0;
}

static const char *vm_net_mock_npc_shop_selector_name(u32 selector)
{
    static const char *names[] = {
        "\xcd\xb7\xbf\xf8", /* 头盔 */
        "\xd2\xc2\xbc\xd7", /* 衣甲 */
        "\xc5\xfb\xb7\xe7", /* 披风 */
        "\xd1\xfc\xb4\xf8", /* 腰带 */
        "\xbb\xa4\xcd\xc8", /* 护腿 */
        "\xd0\xac\xd1\xa5", /* 鞋靴 */
        "\xbd\xe4\xd6\xb8", /* 戒指 */
        "\xbd\xa3",         /* 剑 */
        "\xd8\xb0\xca\xd7", /* 匕首 */
        "\xb7\xa8\xd5\xc8"  /* 法杖 */
    };

    if (selector == VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR)
        return "\xd2\xa9\xc6\xb7"; /* 药品 */
    if (selector >= 1u && selector <= 10u)
        return names[selector - 1u];
    return "";
}

/* Equipment recovery is deliberately driven by the active role's concrete
 * backpack sequence, not a catalog item id.  A player can own several copies
 * of one equipment id with different enhancement state, and an equipped item
 * is absent from this list by construction. */
static bool vm_net_mock_npc_sell_backpack_item_matches(
    const vm_net_mock_backpack_item_state *backpackItem,
    const vm_net_mock_shop_catalog_item **catalogItemOut)
{
    const vm_net_mock_shop_catalog_item *catalogItem = NULL;

    if (catalogItemOut)
        *catalogItemOut = NULL;
    if (backpackItem == NULL || backpackItem->itemId == 0 ||
        backpackItem->seq == 0 || backpackItem->count == 0)
    {
        return false;
    }
    catalogItem = vm_net_mock_find_shop_catalog_item(backpackItem->itemId);
    if (catalogItem == NULL || !catalogItem->isEquip || catalogItem->price == 0)
        return false;
    if (catalogItemOut)
        *catalogItemOut = catalogItem;
    return true;
}

static u32 vm_net_mock_npc_sell_equipment_price(
    const vm_net_mock_shop_catalog_item *catalogItem)
{
    if (catalogItem == NULL || !catalogItem->isEquip || catalogItem->price == 0)
        return 0;
    /* The configured price is the DSH/server-catalog base value.  There is no
     * client or resource evidence for an enhancement surcharge, so recovery
     * intentionally remains 10% of that base value. */
    return (catalogItem->price / 100u) *
               VM_NET_MOCK_NPC_SERVICE_EQUIPMENT_SELL_PERCENT +
           (((catalogItem->price % 100u) *
                 VM_NET_MOCK_NPC_SERVICE_EQUIPMENT_SELL_PERCENT +
             99u) /
            100u);
}

/* Quality belongs to equip.dsh metadata, not the equipment instance's
 * enhancement level.  Keep the candidate list on concrete backpack rows so
 * equipped instances remain outside the operation by construction. */
static bool vm_net_mock_npc_collect_quality_zero_equipment(
    const vm_net_mock_role_state *role, u32 *itemIdsOut, u16 *sequencesOut,
    u32 outputCap, u32 *countOut, u32 *priceOut)
{
    u32 count = 0;
    u32 price = 0;
    u8 itemCount = 0;

    if (countOut)
        *countOut = 0;
    if (priceOut)
        *priceOut = 0;
    if (role == NULL ||
        ((itemIdsOut == NULL || sequencesOut == NULL) && outputCap != 0))
    {
        return false;
    }

    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *catalogItem = NULL;
        const vm_net_mock_equipment_catalog_item *equipment = NULL;
        const vm_net_mock_backpack_item_state *backpackItem =
            &role->backpackItems[i];
        u32 itemPrice = 0;

        if (!vm_net_mock_npc_sell_backpack_item_matches(backpackItem,
                                                        &catalogItem) ||
            (equipment = vm_net_mock_find_equipment_catalog_item(
                 backpackItem->itemId)) == NULL ||
            equipment->quality != 0 ||
            (itemPrice = vm_net_mock_npc_sell_equipment_price(catalogItem)) ==
                0)
        {
            continue;
        }
        if (count >= VM_NET_MOCK_BACKPACK_MAX_ITEMS ||
            (itemIdsOut != NULL && sequencesOut != NULL && count >= outputCap))
        {
            return false;
        }
        if (itemIdsOut != NULL && sequencesOut != NULL)
        {
            itemIdsOut[count] = backpackItem->itemId;
            sequencesOut[count] = backpackItem->seq;
        }
        price = vm_net_mock_add_capped_u32(price, itemPrice);
        ++count;
    }
    if (countOut)
        *countOut = count;
    if (priceOut)
        *priceOut = price;
    return count != 0 && price != 0;
}

/* The caller persists the final role snapshot exactly once.  Collect every
 * candidate before deleting anything so an invalid/missing row leaves the
 * role byte-for-byte unchanged.  Each row mirrors the single-item recovery
 * contract and consumes one durable equipment instance. */
static bool vm_net_mock_role_recycle_quality_zero_equipment_in_memory(
    vm_net_mock_role_state *role, u32 expectedCount, u32 expectedPrice,
    u32 *recycledCountOut, u32 *recycledPriceOut)
{
    vm_net_mock_role_state before;
    u32 itemIds[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
    u16 sequences[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
    u32 count = 0;
    u32 price = 0;

    if (recycledCountOut)
        *recycledCountOut = 0;
    if (recycledPriceOut)
        *recycledPriceOut = 0;
    if (role == NULL || expectedCount == 0 || expectedPrice == 0 ||
        !vm_net_mock_npc_collect_quality_zero_equipment(
            role, itemIds, sequences, VM_NET_MOCK_BACKPACK_MAX_ITEMS, &count,
            &price) ||
        count != expectedCount || price != expectedPrice)
    {
        return false;
    }

    before = *role;
    for (u32 i = 0; i < count; ++i)
    {
        if (!vm_net_mock_role_consume_backpack_item(role, itemIds[i],
                                                     sequences[i], 1, NULL))
        {
            *role = before;
            return false;
        }
    }
    role->money = vm_net_mock_add_capped_u32(role->money, price);
    if (recycledCountOut)
        *recycledCountOut = count;
    if (recycledPriceOut)
        *recycledPriceOut = price;
    return true;
}

static u32 vm_net_mock_npc_sell_equipment_total(
    const vm_net_mock_role_state *role)
{
    u32 total = 0;
    u8 itemCount = 0;

    if (role == NULL)
        return 0;
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        if (vm_net_mock_npc_sell_backpack_item_matches(
                &role->backpackItems[i], NULL))
        {
            ++total;
        }
    }
    return total;
}

static vm_net_mock_backpack_item_state *
vm_net_mock_npc_sell_equipment_item_at(vm_net_mock_role_state *role,
                                       u32 ordinal,
                                       const vm_net_mock_shop_catalog_item **catalogItemOut)
{
    u32 seen = 0;
    u8 itemCount = 0;

    if (catalogItemOut)
        *catalogItemOut = NULL;
    if (role == NULL)
        return NULL;
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *catalogItem = NULL;
        vm_net_mock_backpack_item_state *backpackItem = &role->backpackItems[i];

        if (!vm_net_mock_npc_sell_backpack_item_matches(backpackItem,
                                                        &catalogItem))
        {
            continue;
        }
        if (seen++ != ordinal)
            continue;
        if (catalogItemOut)
            *catalogItemOut = catalogItem;
        return backpackItem;
    }
    return NULL;
}

/* Equipment resale rows are ordered from the live backpack.  The row request
 * contains only its sequence, so use that stable sequence to recover the
 * page that was visible when the confirmation was opened. */
static u32 vm_net_mock_npc_sell_equipment_item_page(
    const vm_net_mock_role_state *role, u16 backpackSeq)
{
    u32 total = vm_net_mock_npc_sell_equipment_total(role);

    if (role == NULL || backpackSeq == 0)
        return 0;
    for (u32 ordinal = 0; ordinal < total; ++ordinal)
    {
        vm_net_mock_backpack_item_state *candidate =
            vm_net_mock_npc_sell_equipment_item_at(
                (vm_net_mock_role_state *)role, ordinal, NULL);
        if (candidate != NULL && candidate->seq == backpackSeq)
            return ordinal / VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
    }
    return 0;
}

/* JianghuOL.CBE:0x01033544 consumes 7/11 by sequence.  It updates the
 * existing row for a positive count and calls the item manager's delete path
 * when the count reaches zero.  A 7/7 type=2 iteminfo row must not accompany
 * it: mmGame:sub_D04 feeds that row to the additive item manager, which leaks
 * physical category-15 slots when the supplied count is the remaining stack. */
static bool vm_net_mock_append_backpack_item_count11_object(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount, u16 seq, u32 itemId,
    u32 remaining)
{
    u8 countInfo[32];
    u32 countInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || objectCount == NULL || seq == 0 ||
        itemId == 0 ||
        !vm_net_mock_build_item_use_count_info_blob(
            countInfo, sizeof(countInfo), seq, remaining, &countInfoLen) ||
        countInfoLen == 0 || countInfoLen > 0xffffu)
    {
        return false;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 11,
                                     &objectStart) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "info", countInfo,
                                    (u16)countInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    ++*objectCount;
    return true;
}

/* A 6/11 accept callback does not rebuild the backpack page.  Compare the
 * durable pre-grant snapshot with the persisted role so every affected stack
 * reaches the CBE item manager through its native incremental reward path.
 * The following 7/11 rows then establish the authoritative total for both a
 * fresh row and an already-visible partial stack. */
static bool vm_net_mock_append_task_accept_backpack_refresh(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
    const vm_net_mock_role_state *before,
    vm_net_mock_role_state *after,
    const vm_net_mock_task_definition *task)
{
    vm_net_mock_reward15_item_row rows[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
    u8 rowCount = 0;
    u32 granted = 0;
    u8 afterCount = 0;

    if (task == NULL || task->givenItemId == 0 || task->givenItemCount == 0)
        return true;
    if (out == NULL || pos == NULL || objectCount == NULL || before == NULL ||
        after == NULL)
    {
        return false;
    }
    memset(rows, 0, sizeof(rows));
    afterCount = vm_net_mock_role_backpack_count(after);
    for (u8 i = 0; i < afterCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = &after->backpackItems[i];
        u32 priorCount = 0;

        if (item->itemId != task->givenItemId || item->seq == 0 ||
            item->count == 0)
        {
            continue;
        }
        for (u8 prior = 0;
             prior < vm_net_mock_role_backpack_count(before); ++prior)
        {
            const vm_net_mock_backpack_item_state *priorItem =
                &before->backpackItems[prior];

            if (priorItem->seq == item->seq)
            {
                priorCount = priorItem->count;
                break;
            }
        }
        if (item->count <= priorCount || rowCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS ||
            0xffffffffu - granted < item->count - priorCount)
        {
            if (item->count <= priorCount)
                continue;
            return false;
        }
        rows[rowCount].item = item;
        rows[rowCount].acquiredCount = item->count - priorCount;
        granted += rows[rowCount].acquiredCount;
        ++rowCount;
    }
    if (rowCount == 0 || granted != task->givenItemCount)
        return false;

    for (u8 start = 0; start < rowCount;)
    {
        u8 batchCount = rowCount - start;

        if (batchCount > VM_NET_MOCK_REWARD15_MAX_ROWS)
            batchCount = VM_NET_MOCK_REWARD15_MAX_ROWS;
        if (!vm_net_mock_append_backpack_reward15_object(
                out, outCap, pos, objectCount, &rows[start], batchCount))
        {
            return false;
        }
        start = (u8)(start + batchCount);
    }
    for (u8 i = 0; i < rowCount; ++i)
    {
        if (!vm_net_mock_append_backpack_item_count11_object(
                out, outCap, pos, objectCount, rows[i].item->seq,
                rows[i].item->itemId, rows[i].item->count))
        {
            return false;
        }
    }
    return true;
}

/* Case 4's task-local iteminfo is still required for the task callback, but
 * an already-open backpack component owns its visible rows through 7/11.
 * Mirror the persisted case-4 remainders through that independent,
 * sequence-keyed client path; zero remains the client's native delete value. */
static bool vm_net_mock_append_task_submit_backpack_refresh(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
    const vm_net_mock_task_definition *task,
    const u16 consumedSeqs[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    const u8 consumedRemainings[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX],
    u8 consumedCount)
{
    u32 itemIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u32 itemCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 itemCount = 0;

    if (consumedCount == 0)
        return true;
    if (out == NULL || pos == NULL || objectCount == NULL || task == NULL ||
        consumedSeqs == NULL || consumedRemainings == NULL ||
        consumedCount > VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX ||
        !vm_net_mock_task_collect_consumed_items(
            task, itemIds, itemCounts, &itemCount) ||
        itemCount != consumedCount)
    {
        return false;
    }
    for (u8 i = 0; i < consumedCount; ++i)
    {
        if (consumedSeqs[i] == 0 || itemIds[i] == 0 || itemCounts[i] == 0 ||
            !vm_net_mock_append_backpack_item_count11_object(
                out, outCap, pos, objectCount, consumedSeqs[i], itemIds[i],
                consumedRemainings[i]))
        {
            return false;
        }
    }
    return true;
}

static bool vm_net_mock_npc_service_opcode_is_supported(u32 opcode)
{
    switch (opcode)
    {
    case VM_NET_MOCK_NPC_SERVICE_OPEN_WEAPON &
        VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK:
    case VM_NET_MOCK_NPC_SERVICE_BUY_WEAPON_BASE:
    case VM_NET_MOCK_NPC_SERVICE_REPAIR_ALL &
        VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_SKILLS &
        VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK:
    case VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_ARMOR &
        VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_MEDICINE &
        VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE:
    case VM_NET_MOCK_NPC_SERVICE_BUY_ITEM_BASE:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_INSTANCE_BASE:
    case VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE:
    case VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE:
    case VM_NET_MOCK_NPC_SERVICE_SELL_EQUIPMENT_BASE:
    case VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO_BASE:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_ARENA &
        VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK:
    case VM_NET_MOCK_NPC_SERVICE_CONFIRM_TRANSACTION &
        VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK:
    case VM_NET_MOCK_NPC_SERVICE_CANCEL_TRANSACTION &
        VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_LEARN_BASE:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_FORGET_BASE:
    case VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX_BASE:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_MAIL_BASE:
    case VM_NET_MOCK_NPC_SERVICE_CLAIM_MAIL_BASE:
    case VM_NET_MOCK_NPC_SERVICE_OPEN_CRYSTAL_SYNTHESIS_BASE:
    case VM_NET_MOCK_NPC_SERVICE_SYNTHESIZE_CRYSTAL_BASE:
        return true;
    default:
        return false;
    }
}

bool vm_net_mock_is_npc_service_dialog_request(
    const u8 *request, u32 requestLen, u32 *serviceValueOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 requestType = 0;
    u32 serviceValue = 0;

    if (serviceValueOut)
        *serviceValueOut = 0;
    if (request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' || request[4] != 1 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 26 || object.subtype != 1 ||
        object.payloadLen == 0 || offset != requestLen ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                         "type", &requestType) ||
        requestType != 2 ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "id", &serviceValue) ||
        !vm_net_mock_npc_service_opcode_is_supported(
            serviceValue & VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK))
    {
        return false;
    }
    if (serviceValueOut)
        *serviceValueOut = serviceValue;
    return true;
}

static bool vm_net_mock_append_npc_service_dialog_option(
    u8 *dialog, u32 dialogCap, u32 *dialogLen,
    const char *name, u8 action, u32 value, const char *description)
{
    return vm_net_mock_seq_put_u8(dialog, dialogCap, dialogLen, 4) &&
           vm_net_mock_seq_put_string(dialog, dialogCap, dialogLen,
                                      name ? name : "") &&
           vm_net_mock_seq_put_u8(dialog, dialogCap, dialogLen, action) &&
           vm_net_mock_seq_put_u32(dialog, dialogCap, dialogLen, value) &&
           vm_net_mock_seq_put_string(dialog, dialogCap, dialogLen,
                                      description ? description : "");
}

/* NPC merchant items are represented by task-hall dialog options, rather
 * than mmShop's 14/* iteminfo page.  The description field is what the
 * client redraws as the cursor moves, so derive it directly from the same
 * equip.dsh base-stat catalogue used for battle calculations. */
static void vm_net_mock_format_npc_equipment_option_description(
    char *out, u32 outCap, const vm_net_mock_equipment_catalog_item *equipment)
{
    typedef struct
    {
        const char *name;
        u32 value;
    } vm_net_mock_npc_equipment_option_attr;
    vm_net_mock_npc_equipment_option_attr attrs[11];
    u32 pos = 0;
    int written = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (equipment == NULL)
        return;

    written = snprintf(out, outCap, "Lv.%u", equipment->levelRequired);
    if (written < 0 || (u32)written >= outCap)
    {
        out[outCap - 1u] = 0;
        return;
    }
    pos = (u32)written;

    attrs[0].name = "\xce\xef\xb9\xa5"; /* 物攻 */
    attrs[0].value = equipment->bonus.attack;
    attrs[1].name = "\xbb\xa4\xbc\xd7"; /* 护甲 */
    attrs[1].value = equipment->bonus.armor;
    attrs[2].name = "\xc6\xf8\xd1\xaa"; /* 气血 */
    attrs[2].value = equipment->bonus.hp;
    attrs[3].name = "\xb7\xa8\xc1\xa6"; /* 法力 */
    attrs[3].value = equipment->bonus.mp;
    attrs[4].name = "\xc1\xa6\xc1\xbf"; /* 力量 */
    attrs[4].value = equipment->bonus.strength;
    attrs[5].name = "\xc3\xf4\xbd\xdd"; /* 敏捷 */
    attrs[5].value = equipment->bonus.agility;
    attrs[6].name = "\xd6\xc7\xbb\xdb"; /* 智慧 */
    attrs[6].value = equipment->bonus.wisdom;
    attrs[7].name = "\xb1\xa9\xbb\xf7"; /* 暴击 */
    attrs[7].value = equipment->bonus.crit;
    attrs[8].name = "\xc3\xfc\xd6\xd0"; /* 命中 */
    attrs[8].value = equipment->bonus.hit;
    attrs[9].name = "\xb6\xe3\xc9\xc1"; /* 躲闪 */
    attrs[9].value = equipment->bonus.dodge;
    attrs[10].name = "\xbf\xb9\xd0\xd4"; /* 抗性 */
    attrs[10].value = equipment->bonus.resist;

    for (u32 i = 0; i < (u32)(sizeof(attrs) / sizeof(attrs[0])); ++i)
    {
        if (attrs[i].value == 0 || pos >= outCap)
            continue;
        written = snprintf(out + pos, outCap - pos, "\n%s+%u",
                           attrs[i].name, attrs[i].value);
        if (written < 0 || (u32)written >= outCap - pos)
        {
            out[outCap - 1u] = 0;
            return;
        }
        pos += (u32)written;
    }
}

static void vm_net_mock_format_npc_equipment_instance_option_description(
    char *out, u32 outCap, const vm_net_mock_equipment_catalog_item *equipment,
    u8 enhanceLevel)
{
    u32 used = 0;
    int written = 0;

    vm_net_mock_format_npc_equipment_option_description(out, outCap, equipment);
    if (out == NULL || outCap == 0 || enhanceLevel == 0)
        return;
    used = (u32)strlen(out);
    if (used >= outCap)
    {
        out[outCap - 1u] = 0;
        return;
    }
    written = snprintf(out + used, outCap - used, "\n\xc7\xbf\xbb\xaf+%u",
                       enhanceLevel); /* 强化 */
    if (written < 0 || (u32)written >= outCap - used)
        out[outCap - 1u] = 0;
}

/* The selected option description is the client-owned detail pane.  Keep the
 * purchase/recovery amount alongside the resource-backed item details.  The
 * client text renderer already supports LF in task/dialog text; formatters
 * above therefore use one actual LF per detail row. */
static void vm_net_mock_append_npc_option_price_description(
    char *out, u32 outCap, const char *label, u32 price)
{
    u32 used = 0;
    int written = 0;

    if (out == NULL || outCap == 0 || label == NULL || price == 0)
        return;
    used = (u32)strlen(out);
    if (used >= outCap)
    {
        out[outCap - 1u] = 0;
        return;
    }
    written = snprintf(out + used, outCap - used, "\n%s%u%s", label, price,
                       "\xcd\xad"); /* 铜 */
    if (written < 0 || (u32)written >= outCap - used)
        out[outCap - 1u] = 0;
}

/* ParseNPCDialogData stores the dialog main text separately from each
 * option's description.  The confirmation view renders the former, while
 * the latter is only available to the selected-row detail pane.  Mirror the
 * already formatted item detail into the main text so confirmation remains
 * informative even when that pane is not rendered by the active layout. */
static void vm_net_mock_append_npc_confirmation_detail(
    char *dialog, u32 dialogCap, const char *detail)
{
    u32 used = 0;
    int written = 0;

    if (dialog == NULL || dialogCap == 0 || detail == NULL || detail[0] == 0)
        return;
    used = (u32)strlen(dialog);
    if (used >= dialogCap)
    {
        dialog[dialogCap - 1u] = 0;
        return;
    }
    written = snprintf(dialog + used, dialogCap - used, "\n%s", detail);
    if (written < 0 || (u32)written >= dialogCap - used)
        dialog[dialogCap - 1u] = 0;
}

/* item.dsh supplies the only authoritative values for ordinary medicine
 * effects.  Keep the text in the existing 26/1 option-description field:
 * ParseNPCDialogData allocates 0xC8 bytes for that client-side string. */
static void vm_net_mock_format_npc_item_effect_option_description(
    char *out, u32 outCap, const vm_net_mock_item_effect_catalog_item *effect)
{
    u32 pos = 0;
    int written = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (effect == NULL)
        return;

    written = snprintf(out, outCap, "Lv.%u", effect->levelRequired);
    if (written < 0 || (u32)written >= outCap)
    {
        out[outCap - 1u] = 0;
        return;
    }
    pos = (u32)written;

#define VM_NET_MOCK_NPC_APPEND_ITEM_EFFECT_TEXT(format, value)                 \
    do                                                                           \
    {                                                                            \
        if ((value) != 0 && pos < outCap)                                       \
        {                                                                        \
            written = snprintf(out + pos, outCap - pos, format, (value));       \
            if (written < 0 || (u32)written >= outCap - pos)                    \
            {                                                                    \
                out[outCap - 1u] = 0;                                           \
                return;                                                          \
            }                                                                    \
            pos += (u32)written;                                                \
        }                                                                        \
    } while (0)

    VM_NET_MOCK_NPC_APPEND_ITEM_EFFECT_TEXT(
        "\n\xbb\xd6\xb8\xb4\xc6\xf8\xd1\xaa+%u", effect->hp); /* 恢复气血 */
    VM_NET_MOCK_NPC_APPEND_ITEM_EFFECT_TEXT(
        "\n\xbb\xd6\xb8\xb4\xb7\xa8\xc1\xa6+%u", effect->mp); /* 恢复法力 */
    VM_NET_MOCK_NPC_APPEND_ITEM_EFFECT_TEXT(
        "\n\xbe\xad\xd1\xe9+%u", effect->exp); /* 经验 */
    VM_NET_MOCK_NPC_APPEND_ITEM_EFFECT_TEXT(
        "\n\xca\xb1\xd0\xa7%u\xb7\xd6\xd6\xd3", effect->durationMinutes); /* 时效...分钟 */

#undef VM_NET_MOCK_NPC_APPEND_ITEM_EFFECT_TEXT
}

static u32 vm_net_mock_build_challenge_interaction_response_ex(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap,
    bool forceNonSceneStart, bool forceSceneMonsterStart);

static const vm_net_mock_scene_npcinfo_seed *
vm_net_mock_instance_guide_seed(u32 actorId)
{
    const char *scene = vm_net_mock_current_scene_name();
    static vm_net_mock_scene_npcinfo_seed resolved[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    vm_net_mock_npc_service_option
        services[VM_NET_MOCK_NPC_SERVICE_OPTION_MAX];
    u32 selected = 0;

    if (scene == NULL || actorId == 0)
    {
        return NULL;
    }
    memset(resolved, 0, sizeof(resolved));
    selected = vm_net_mock_select_scene_npcinfo_seeds(
        scene, resolved, VM_NET_MOCK_SCENE_NPCINFO_MAX, NULL, NULL);
    for (u32 i = 0; i < selected; ++i)
    {
        u32 serviceCount = 0;

        if (resolved[i].actorId != actorId)
            continue;
        memset(services, 0, sizeof(services));
        /* `kind` is now only the legacy compatibility projection.  A
         * multi-service NPC may list a weapon shop first and its instance
         * guide later, so selecting by this field would incorrectly make the
         * emitted instance action unresolvable.  Resolve the same effective
         * service set that produced the parent dialog instead. */
        if (vm_net_mock_npc_service_options_resolve(
                scene, actorId, resolved[i].kind,
                resolved[i].serviceOptionName,
                resolved[i].serviceOptionDescription, services,
                VM_NET_MOCK_NPC_SERVICE_OPTION_MAX, &serviceCount, NULL) &&
            (vm_net_mock_npc_service_options_has_kind(
                 services, serviceCount,
                 VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE) ||
             vm_net_mock_npc_service_options_has_kind(
                 services, serviceCount,
                 VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE)))
        {
            return &resolved[i];
        }
    }
    return NULL;
}

static u32 vm_net_mock_build_instance_enter_response(
    const vm_net_mock_scene_npcinfo_seed *seed, u8 *out, u32 outCap)
{
    vm_net_mock_scene_change_target target;
    char entryScene[64];
    u32 pos = 0;

    memset(entryScene, 0, sizeof(entryScene));
    if (seed == NULL || seed->instanceScene[0] == 0 ||
        !vm_net_mock_str_ends_with(seed->instanceScene, ".sce") ||
        seed->instanceX == 0 || seed->instanceY == 0 ||
        !vm_net_mock_scene_battle_monster_instance_entry_scene(
            seed->instanceScene, seed->instanceSpawnEnemyId, entryScene,
            sizeof(entryScene)))
    {
        if (seed != NULL && seed->instanceSpawnEnemyId != 0)
            printf("[error][network] mock_npc_instance_enter_spawn_unresolved actor=%u scene=%s spawn_enemy=%u source=SCE2-kind3+city-mirror action=no-fallback\n",
                   seed->actorId, seed->instanceScene,
                   seed->instanceSpawnEnemyId);
        return 0;
    }
    memset(&target, 0, sizeof(target));
    snprintf(target.scene, sizeof(target.scene), "%s", entryScene);
    target.x = seed->instanceX;
    target.y = seed->instanceY;
    target.mapType = 2;
    target.hasSceEntry = true;
    pos = vm_net_mock_build_scene_channel_enter_combo_for_target(
        &target, out, outCap);
    if (pos == 0)
        return 0;

    /* The response above is the position-bearing 30/1 that creates the
     * destination scene shell.  Preserve that fact on the pending target so
     * its WT6/1 resource callback cannot fall through to the generic first
     * scene path and emit a second 30/1. */
    target.sceneEnterPosinfoSent = true;
    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    /*
     * NPC instance entry is a direct 30/1 scene enter, but it did not pass
     * through a map-stone 16/1/16/2/16/3 confirmation.  Marking it as a
     * map-stone direct entry makes an unrelated map-stone completion handler
     * own the target.  The observed instance path instead sends a composite
     * WT2/1 type-27 request and then WT6/1 after this 30/1.
     *
     * Keep the target pending: WT2/1 answers only its requested families, and
     * WT6/1 owns resources, NPC data and the one no-posinfo 30/2 completion.
     */
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    if (!vm_mock_service_active_transient_instance_begin(
            target.scene, target.x, target.y, "npc-instance-enter"))
    {
        /* A real network entry is always bound to a selected role session.
         * Keep standalone builder fixtures non-persistent, but do not invent
         * a durable fallback when that test-only context is absent. */
        printf("[warn][network] mock_npc_instance_enter_session_unbound actor=%u scene=%s action=no-durable-position-save\n",
               seed->actorId, target.scene);
    }
    printf("[info][network] mock_npc_instance_enter actor=%u configured_scene=%s scene=%s pos=(%u,%u) spawn_enemy=%u source=SCE2-kind3+city-mirror response=30/1 resp=%u position_owner=session-transient evidence=JianghuOL.CBE:0x01039B8A+0x010396D6\n",
           seed->actorId, seed->instanceScene, target.scene, target.x, target.y,
           seed->instanceSpawnEnemyId, pos);
    return pos;
}

static u32 vm_net_mock_build_instance_challenge_battle_response(
    u32 actorId, u32 enemyId, u16 challengeX, u16 challengeY,
    u8 *out, u32 outCap)
{
    u8 synthetic[192];
    u32 requestPos = 9;
    u32 objectStart = 4;
    u32 responseLen = 0;

    if (actorId == 0 || enemyId == 0 || challengeX == 0 || challengeY == 0)
        return 0;
    memset(synthetic, 0, sizeof(synthetic));
    synthetic[0] = 'W';
    synthetic[1] = 'T';
    synthetic[objectStart] = 1;
    synthetic[objectStart + 1] = 4;
    synthetic[objectStart + 2] = 1;
    if (!vm_net_mock_put_object_u32(synthetic, sizeof(synthetic), &requestPos,
                                    "id", enemyId) ||
        !vm_net_mock_put_object_u32(synthetic, sizeof(synthetic), &requestPos,
                                    "index", 0) ||
        !vm_net_mock_put_object_u32(synthetic, sizeof(synthetic), &requestPos,
                                    "posx", challengeX) ||
        !vm_net_mock_put_object_u32(synthetic, sizeof(synthetic), &requestPos,
                                    "posy", challengeY))
    {
        return 0;
    }
    synthetic[2] = (u8)(requestPos >> 8);
    synthetic[3] = (u8)requestPos;
    synthetic[objectStart + 3] = (u8)((requestPos - objectStart) >> 8);
    synthetic[objectStart + 4] = (u8)(requestPos - objectStart);
    responseLen = vm_net_mock_build_challenge_interaction_response_ex(
        synthetic, requestPos, out, outCap, true, false);
    if (responseLen != 0)
    {
        printf("[info][network] mock_npc_instance_challenge_start actor=%u enemy=%u pos=(%u,%u) response=4/10-direct resp=%u evidence=JianghuOL.CBE:0x01039566(30/10)+mmBattle:0x67AC(non-scene-start)\n",
               actorId, enemyId, challengeX, challengeY,
               responseLen);
    }
    return responseLen;
}

/* A direct action13 target is already a live current-scene kind-2 node.  The
 * client contributes that node's index in 4/1; the server re-resolves the
 * SCE row only for its authoritative coordinates, then lets the established
 * 2/2 + 4/5 builder populate battleinfo. */
static u32 vm_net_mock_build_direct_scene_challenge_battle_response(
    u32 enemyId, u32 sceneIndex, u16 sceneX, u16 sceneY, u8 *out,
    u32 outCap)
{
    u8 synthetic[192];
    u32 requestPos = 9;
    u32 objectStart = 4;

    if (enemyId == 0 || sceneIndex == 0 || sceneIndex >= 25 || sceneX == 0 ||
        sceneY == 0 || out == NULL || outCap < 10)
    {
        return 0;
    }
    memset(synthetic, 0, sizeof(synthetic));
    synthetic[0] = 'W';
    synthetic[1] = 'T';
    synthetic[objectStart] = 1;
    synthetic[objectStart + 1] = 4;
    synthetic[objectStart + 2] = 1;
    if (!vm_net_mock_put_object_u32(synthetic, sizeof(synthetic), &requestPos,
                                    "id", enemyId) ||
        !vm_net_mock_put_object_u32(synthetic, sizeof(synthetic), &requestPos,
                                    "index", sceneIndex) ||
        !vm_net_mock_put_object_u32(synthetic, sizeof(synthetic), &requestPos,
                                    "posx", sceneX) ||
        !vm_net_mock_put_object_u32(synthetic, sizeof(synthetic), &requestPos,
                                    "posy", sceneY))
    {
        return 0;
    }
    synthetic[2] = (u8)(requestPos >> 8);
    synthetic[3] = (u8)requestPos;
    synthetic[objectStart + 3] = (u8)((requestPos - objectStart) >> 8);
    synthetic[objectStart + 4] = (u8)(requestPos - objectStart);
    return vm_net_mock_build_challenge_interaction_response_ex(
        synthetic, requestPos, out, outCap, false, true);
}

static u32 vm_net_mock_build_instance_challenge_prompt_response(
    const vm_net_mock_scene_npcinfo_seed *seed, u8 *out, u32 outCap)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = session != NULL
                                        ? vm_mock_service_team_find_for_client(
                                              session->clientId)
                                        : NULL;
    const char *scene = vm_net_mock_current_scene_name();
    const char *challengeText =
        "\xca\xc7\xb7\xf1\xbf\xaa\xca\xbc\xb8\xb1\xb1\xbe\xcc\xf4\xd5\xbd\xa3\xbf"; /* 是否开始副本挑战？ */
    bool leaderBlocked = team != NULL &&
                         !vm_mock_service_team_is_leader(team, session->clientId);
    u16 challengeX = 0;
    u16 challengeY = 0;
    u32 pos = 5;
    u32 ackObjectStart = 0;
    u32 objectStart = 0;

    if (seed == NULL || seed->challengeEnemyId == 0 || session == NULL ||
        !vm_net_mock_scene_name_is_safe(scene) || out == NULL || outCap < pos)
    {
        return 0;
    }
    challengeX = seed->instanceX != 0 ? seed->instanceX : seed->x;
    challengeY = seed->instanceY != 0 ? seed->instanceY : seed->y;
    if (leaderBlocked)
    {
        challengeText =
            "\xd6\xbb\xd3\xd0\xb6\xd3\xb3\xa4\xbf\xc9\xd2\xd4\xb7\xa2\xc6\xf0\xb8\xb1\xb1\xbe\xcc\xf4\xd5\xbd\xa1\xa3"; /* 只有队长可以发起副本挑战。 */
    }

    /* The challenge option itself is sent through task-hall action=1 as a
     * 26/1 request.  The client only clears that request's pending/progress
     * state in DispatchItemEvent (0x01039C28), i.e. while dispatching a
     * kind-26 response object.  Put a no-op 26/0 acknowledgement first.
     *
     * Keep the native instance confirmation as the second object.  Its
     * confirmation callback sends 30/10.  That request is acknowledged first;
     * the actual non-scene battle start is delivered independently so the
     * business dispatcher cannot gate the battle-module callback. */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 26, 0,
                                     &ackObjectStart))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, ackObjectStart, pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 30, 9,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "isleader",
                                   leaderBlocked ? 1u : 0u) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "challenge",
                                       challengeText))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 2);

    session->instanceChallengePending = !leaderBlocked;
    session->instanceChallengeBattlePending = false;
    session->instanceChallengeActorId = !leaderBlocked ? seed->actorId : 0;
    session->instanceChallengeEnemyId =
        !leaderBlocked ? seed->challengeEnemyId : 0;
    if (leaderBlocked || !session->instanceChallengeDirectSceneMonster)
        session->instanceChallengeSceneIndex = 0;
    session->instanceChallengeX = !leaderBlocked ? challengeX : 0;
    session->instanceChallengeY = !leaderBlocked ? challengeY : 0;
    session->instanceChallengeTick = !leaderBlocked ? g_schedulerTick : 0;
    if (!leaderBlocked)
    {
        snprintf(session->instanceChallengeScene,
                 sizeof(session->instanceChallengeScene), "%s", scene);
    }
    else
    {
        session->instanceChallengeScene[0] = 0;
        session->instanceChallengeDirectPending = false;
        session->instanceChallengeDirectSceneMonster = false;
    }
    printf("[info][network] mock_npc_instance_challenge_prompt client=%08x actor=%u enemy=%u scene=%s pos=(%u,%u) blocked=%u response=26/0+30/9 resp=%u evidence=JianghuOL.CBE:0x01039C28+0x010395AA\n",
           session->clientId, seed->actorId, seed->challengeEnemyId, scene,
           challengeX, challengeY, leaderBlocked ? 1u : 0u, pos);
    return pos;
}

static u32 vm_net_mock_build_direct_scene_challenge_prompt_response(
    vm_mock_service_client_session *session, u8 *out, u32 outCap)
{
    vm_net_mock_scene_npcinfo_seed seed;
    u32 responseLen = 0;

    if (session == NULL || !session->instanceChallengeDirectSceneMonster ||
        session->instanceChallengeActorId == 0 ||
        session->instanceChallengeEnemyId == 0 ||
        session->instanceChallengeSceneIndex == 0 ||
        session->instanceChallengeSceneIndex >= 25 ||
        session->instanceChallengeX == 0 || session->instanceChallengeY == 0)
    {
        return 0;
    }
    memset(&seed, 0, sizeof(seed));
    seed.actorId = session->instanceChallengeActorId;
    seed.challengeEnemyId = session->instanceChallengeEnemyId;
    seed.instanceX = session->instanceChallengeX;
    seed.instanceY = session->instanceChallengeY;
    responseLen = vm_net_mock_build_instance_challenge_prompt_response(
        &seed, out, outCap);
    if (responseLen != 0)
    {
        printf("[info][network] mock_direct_scene_challenge_prompt client=%08x actor=%u enemy=%u index=%u scene=%s response=26/0+30/9 resp=%u evidence=JianghuOL.CBE:0x01039C28+0x01039566\n",
               session->clientId, seed.actorId, seed.challengeEnemyId,
               session->instanceChallengeSceneIndex,
               session->instanceChallengeScene, responseLen);
    }
    return responseLen;
}

static bool vm_net_mock_is_instance_challenge_confirm_request(
    const u8 *request, u32 requestLen)
{
    vm_net_mock_request_object object;
    vm_net_mock_request_object extra;
    u32 offset = 4;
    u8 agree = 0;

    if (request == NULL || requestLen != 20 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 30 || object.subtype != 10 ||
        object.payloadLen != 11 ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                         "agree", &agree) ||
        agree > 1 ||
        vm_net_mock_next_request_object(request, requestLen, &offset, &extra))
    {
        return false;
    }
    return offset == requestLen;
}

static u32 vm_net_mock_build_instance_challenge_confirm_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    const char *scene = vm_net_mock_current_scene_name();
    u32 pos = 5;
    u32 ackObjectStart = 0;
    u32 ageTicks = 0;

    /* HandleResConfirmCb(0x01039566) emits one strict 30/10 {agree} object.
     * Do not consume broader packets: subtype 10 is meaningful here only while
     * this connection owns a pending NPC challenge confirmation. */
    if (!vm_net_mock_is_instance_challenge_confirm_request(request,
                                                            requestLen) ||
        session == NULL ||
        !session->instanceChallengePending)
    {
        return 0;
    }
    ageTicks = g_schedulerTick - session->instanceChallengeTick;
    if (ageTicks > (60u * 1000u / VM_SCHED_FRAME_MS) ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_names_equal_exact(
            scene, session->instanceChallengeScene))
    {
        printf("[warn][network] mock_npc_instance_challenge_confirm_drop client=%08x actor=%u enemy=%u age_ticks=%u current_scene=%s pending_scene=%s reason=expired-or-scene-changed\n",
               session->clientId, session->instanceChallengeActorId,
               session->instanceChallengeEnemyId, ageTicks,
               scene ? scene : "-", session->instanceChallengeScene);
        session->instanceChallengePending = false;
        session->instanceChallengeBattlePending = false;
        session->instanceChallengeDirectPending = false;
        session->instanceChallengeDirectSceneMonster = false;
        session->instanceChallengeSceneIndex = 0;
        return 0;
    }

    if (out == NULL || outCap < pos ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 30, 10,
                                     &ackObjectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 0))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, ackObjectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    session->instanceChallengePending = false;
    session->instanceChallengeBattlePending = true;
    session->instanceChallengeTick = g_schedulerTick;
    printf("[info][network] mock_npc_instance_challenge_confirm client=%08x actor=%u enemy=%u age_ticks=%u request=30/10{agree} response=30/10{result=0} battle_delivery=next-scene-poll resp=%u evidence=JianghuOL.CBE:0x01039528(clear-pending)+0x01012F8E(module-callback-gate)\n",
           session->clientId, session->instanceChallengeActorId,
           session->instanceChallengeEnemyId, ageTicks, pos);
    return pos;
}

static u32 vm_net_mock_build_pending_instance_challenge_battle_response(
    u8 *out, u32 outCap, vm_mock_service_client_session *session)
{
    u32 responseLen = 0;
    u32 ageTicks = 0;
    u32 configuredSceneIndex = 0;
    u32 sceneX = 0;
    u32 sceneY = 0;
    bool directSceneMonster = false;

    if (out == NULL || session == NULL ||
        !session->instanceChallengeBattlePending)
    {
        return 0;
    }
    ageTicks = g_schedulerTick - session->instanceChallengeTick;
    if (ageTicks > (10u * 1000u / VM_SCHED_FRAME_MS) ||
        !session->sceneVisibleReady || session->sceneVisiblePending ||
        !vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) ||
        !vm_net_mock_scene_names_equal_exact(
            session->sceneVisibleScene, session->instanceChallengeScene))
    {
        printf("[warn][mock-service] instance_challenge_battle_drop client=%08x actor=%u enemy=%u age_ticks=%u visible_scene=%s pending_scene=%s reason=expired-or-scene-changed\n",
               session->clientId, session->instanceChallengeActorId,
               session->instanceChallengeEnemyId, ageTicks,
               session->sceneVisibleScene, session->instanceChallengeScene);
        session->instanceChallengeBattlePending = false;
        session->instanceChallengeDirectPending = false;
        session->instanceChallengeDirectSceneMonster = false;
        session->instanceChallengeActorId = 0;
        session->instanceChallengeEnemyId = 0;
        session->instanceChallengeSceneIndex = 0;
        session->instanceChallengeX = 0;
        session->instanceChallengeY = 0;
        session->instanceChallengeTick = 0;
        session->instanceChallengeScene[0] = 0;
        return 0;
    }

    directSceneMonster = session->instanceChallengeDirectSceneMonster;
    if (directSceneMonster)
    {
        if (session->instanceChallengeSceneIndex == 0 ||
            session->instanceChallengeSceneIndex >= 25 ||
            !vm_net_mock_select_sce_combat_spawn(
                session->sceneVisibleScene, session->instanceChallengeEnemyId,
                &configuredSceneIndex, &sceneX, &sceneY) ||
            sceneX == 0 || sceneX > UINT16_MAX || sceneY == 0 ||
            sceneY > UINT16_MAX)
        {
            printf("[warn][mock-service] direct_scene_challenge_battle_drop client=%08x actor=%u enemy=%u index=%u scene=%s reason=spawn-unresolved-on-confirmed-poll\n",
                   session->clientId, session->instanceChallengeActorId,
                   session->instanceChallengeEnemyId,
                   session->instanceChallengeSceneIndex,
                   session->sceneVisibleScene);
            session->instanceChallengeBattlePending = false;
            session->instanceChallengeDirectSceneMonster = false;
            session->instanceChallengeActorId = 0;
            session->instanceChallengeEnemyId = 0;
            session->instanceChallengeSceneIndex = 0;
            session->instanceChallengeX = 0;
            session->instanceChallengeY = 0;
            session->instanceChallengeTick = 0;
            session->instanceChallengeScene[0] = 0;
            return 0;
        }
        responseLen = vm_net_mock_build_direct_scene_challenge_battle_response(
            session->instanceChallengeEnemyId,
            session->instanceChallengeSceneIndex, (u16)sceneX, (u16)sceneY,
            out, outCap);
    }
    else
    {
        responseLen = vm_net_mock_build_instance_challenge_battle_response(
            session->instanceChallengeActorId,
            session->instanceChallengeEnemyId,
            session->instanceChallengeX,
            session->instanceChallengeY,
            out, outCap);
    }
    if (responseLen == 0)
        return 0;

    printf("[info][mock-service] instance_challenge_battle_deliver client=%08x actor=%u enemy=%u index=%u config_index=%u age_ticks=%u scene=%s response=%s resp=%u evidence=%s\n",
           session->clientId, session->instanceChallengeActorId,
           session->instanceChallengeEnemyId,
           directSceneMonster ? session->instanceChallengeSceneIndex : 0,
           directSceneMonster ? configuredSceneIndex : 0, ageTicks,
           session->sceneVisibleScene,
           directSceneMonster ? "2/2+4/5-scene" : "4/10",
           responseLen,
           directSceneMonster
               ? "JianghuOL.CBE:0x01012F8E+mmBattle:0x66CC(scene-node)"
               : "JianghuOL.CBE:0x01012F8E+mmBattle:0x67AC(non-scene-start)");
    session->instanceChallengeBattlePending = false;
    session->instanceChallengeDirectPending = false;
    session->instanceChallengeDirectSceneMonster = false;
    session->instanceChallengeActorId = 0;
    session->instanceChallengeEnemyId = 0;
    session->instanceChallengeSceneIndex = 0;
    session->instanceChallengeX = 0;
    session->instanceChallengeY = 0;
    session->instanceChallengeTick = 0;
    session->instanceChallengeScene[0] = 0;
    return responseLen;
}

static bool vm_net_mock_npc_skill_is_starter(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL && skill->levelRequired <= 1u;
}

static bool vm_net_mock_npc_skill_list_matches(
    const vm_net_mock_skill_catalog_item *skill,
    const vm_net_mock_role_state *role,
    const vm_net_mock_role_service_state *serviceState,
    bool forgetList)
{
    u8 rawJob = role ? vm_net_mock_role_job_to_skill_raw_job(role->job) : 0xffu;
    bool learned = skill != NULL &&
                   vm_net_mock_role_service_has_skill(serviceState,
                                                      skill->skillId);

    if (skill == NULL || role == NULL || skill->rawJob != rawJob)
        return false;
    if (forgetList)
        return learned && !vm_net_mock_npc_skill_is_starter(skill);
    return !learned && skill->levelRequired <= role->level;
}

static u32 vm_net_mock_npc_skill_list_total(
    const vm_net_mock_role_state *role,
    const vm_net_mock_role_service_state *serviceState,
    bool forgetList)
{
    u32 total = 0;

    for (u32 i = 0; i < vm_net_mock_load_skill_catalog(); ++i)
    {
        if (vm_net_mock_npc_skill_list_matches(&g_vm_net_mock_skill_catalog[i],
                                               role, serviceState,
                                               forgetList))
        {
            ++total;
        }
    }
    return total;
}

static u32 vm_net_mock_npc_skill_list_item_page(
    const vm_net_mock_role_state *role,
    const vm_net_mock_role_service_state *serviceState,
    bool forgetList, u32 skillId)
{
    u32 ordinal = 0;

    for (u32 i = 0; i < vm_net_mock_load_skill_catalog(); ++i)
    {
        const vm_net_mock_skill_catalog_item *skill =
            &g_vm_net_mock_skill_catalog[i];
        if (!vm_net_mock_npc_skill_list_matches(skill, role, serviceState,
                                                forgetList))
        {
            continue;
        }
        if (skill->skillId == skillId)
            return ordinal / VM_NET_MOCK_NPC_SERVICE_SKILL_PAGE_ITEMS;
        ++ordinal;
    }
    return 0;
}

static u32 vm_net_mock_npc_skill_list_clamp_page(u32 total, u32 page)
{
    if (total == 0)
        return 0;
    if (page >= (total + VM_NET_MOCK_NPC_SERVICE_SKILL_PAGE_ITEMS - 1u) /
                    VM_NET_MOCK_NPC_SERVICE_SKILL_PAGE_ITEMS)
    {
        return (total - 1u) / VM_NET_MOCK_NPC_SERVICE_SKILL_PAGE_ITEMS;
    }
    return page;
}

/* `item.dsh` gives item 900 the explicit blacksmith-synthesis description,
 * then names 901..916 as first through sixteenth-level crystals.  Keep the
 * externally requested recipe policy in this tiny, auditable boundary; no
 * packet may select an arbitrary output item. */
static bool vm_net_mock_crystal_synthesis_recipe(u32 sourceItemId,
                                                 u32 *resultItemIdOut)
{
    if (resultItemIdOut != NULL)
        *resultItemIdOut = 0;
    if (sourceItemId < VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_FIRST ||
        sourceItemId > VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_LAST)
    {
        return false;
    }
    if (resultItemIdOut != NULL)
        *resultItemIdOut = sourceItemId + 1u;
    return true;
}

static u32 vm_net_mock_crystal_synthesis_material_count(u32 sourceItemId)
{
    if (!vm_net_mock_crystal_synthesis_recipe(sourceItemId, NULL))
        return 0;
    return sourceItemId == VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_FIRST
               ? VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_FRAGMENT_MATERIAL_COUNT
               : VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_MATERIAL_COUNT;
}

static u32 vm_net_mock_crystal_synthesis_item_page(u32 sourceItemId)
{
    if (!vm_net_mock_crystal_synthesis_recipe(sourceItemId, NULL))
        return 0;
    return (sourceItemId - VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_FIRST) /
           VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_PAGE_ITEMS;
}

static void vm_net_mock_crystal_synthesis_item_label(u32 itemId, char *out,
                                                      u32 outCap)
{
    if (out == NULL || outCap == 0)
        return;
    if (itemId == VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_FIRST)
    {
        snprintf(out, outCap, "%s", "\xd0\xfe\xbe\xa7\xcb\xe9\xc6\xac"); /* 玄晶碎片 */
        return;
    }
    snprintf(out, outCap, "\x25\x75\xbc\xb6\xd0\xfe\xbe\xa7", /* %u级玄晶 */
             itemId - VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_FIRST);
}

/* Compose consume and add against one in-memory role snapshot.  The caller
 * persists exactly the final role after this function returns successfully;
 * a full backpack or a stale material row restores the whole snapshot. */
static bool vm_net_mock_role_crystal_synthesize_in_memory(
    vm_net_mock_role_state *role, u32 sourceItemId, u16 *sourceSeqOut,
    u32 *sourceRemainingOut, u16 *resultSeqOut)
{
    vm_net_mock_role_state before;
    vm_net_mock_backpack_item_state *source = NULL;
    u32 resultItemId = 0;
    u32 materialCount = 0;
    u32 sourceRemaining = 0;
    u16 sourceSeq = 0;
    u16 resultSeq = 0;

    if (sourceSeqOut != NULL)
        *sourceSeqOut = 0;
    if (sourceRemainingOut != NULL)
        *sourceRemainingOut = 0;
    if (resultSeqOut != NULL)
        *resultSeqOut = 0;
    if (role == NULL ||
        !vm_net_mock_crystal_synthesis_recipe(sourceItemId, &resultItemId))
    {
        return false;
    }
    materialCount = vm_net_mock_crystal_synthesis_material_count(sourceItemId);
    if (materialCount == 0)
        return false;
    before = *role;
    source = vm_net_mock_role_find_backpack_item(role, sourceItemId, 0);
    if (source == NULL || source->count < materialCount)
    {
        return false;
    }
    sourceSeq = source->seq;
    if (!vm_net_mock_role_consume_backpack_item(
            role, sourceItemId, sourceSeq, materialCount, &sourceRemaining) ||
        !vm_net_mock_role_add_backpack_item_to_role_in_memory(
            role, resultItemId, 1, &resultSeq))
    {
        *role = before;
        return false;
    }
    if (sourceSeqOut != NULL)
        *sourceSeqOut = sourceSeq;
    if (sourceRemainingOut != NULL)
        *sourceRemainingOut = sourceRemaining;
    if (resultSeqOut != NULL)
        *resultSeqOut = resultSeq;
    return true;
}

static u32 vm_net_mock_build_npc_service_dialog_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_role_service_state *serviceState = NULL;
    const char *dialogText =
        "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
    const char *optionNames[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS];
    const char *optionDescriptions[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS];
    char optionNameStorage[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS][64];
    char optionDescriptionStorage[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS][192];
    /* The parser owns the main text buffer length, so keep enough room for a
     * catalog name plus the complete (<=192-byte) formatted detail. */
    char dialogTextStorage[512];
    u32 optionValues[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS];
    u32 serviceValue = 0;
    u32 operation = 0;
    u32 value = 0;
    u8 optionCount = 0;
    u8 instanceChallengeOptionIndex = 0xff;
    u8 dialog[4096];
    u32 dialogLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    bool appendSkills = false;
    bool skillPrompt = false;
    u16 backpackAddSeq = 0;
    const char *action = "invalid";
    u32 result = 0;
    u32 restoredListPage = 0;
    u32 skillEligibleCount = 0;
    u32 skillLearnedCount = 0;
    u32 skillLevelLockedCount = 0;
    const vm_net_mock_skill_catalog_item *skillNextLocked = NULL;
    const vm_net_mock_scene_npcinfo_seed *instanceSeed = NULL;
    const vm_mock_service_npc_context *shopContext = NULL;
    vm_mock_service_npc_transaction_context transaction;
    bool transactionConfirm = false;
    bool transactionCancel = false;
    vm_net_mock_mailbox_dialog mailboxView;
    bool mailboxHandled = false;
    bool mailboxClaimRefresh = false;
    bool crystalSynthesisRefresh = false;
    u32 crystalSynthesisSourceItemId = 0;
    u32 crystalSynthesisSourceRemaining = 0;
    u32 crystalSynthesisResultItemId = 0;
    u32 crystalSynthesisResultTotal = 0;
    u16 crystalSynthesisSourceSeq = 0;
    u16 crystalSynthesisResultSeq = 0;
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (role == NULL || out == NULL || outCap < pos ||
        !vm_net_mock_is_npc_service_dialog_request(request, requestLen,
                                                   &serviceValue))
    {
        return 0;
    }
    memset(optionNames, 0, sizeof(optionNames));
    memset(optionDescriptions, 0, sizeof(optionDescriptions));
    memset(optionNameStorage, 0, sizeof(optionNameStorage));
    memset(optionDescriptionStorage, 0, sizeof(optionDescriptionStorage));
    memset(dialogTextStorage, 0, sizeof(dialogTextStorage));
    memset(optionValues, 0, sizeof(optionValues));
    operation = serviceValue & VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK;
    value = serviceValue & VM_NET_MOCK_NPC_SERVICE_VALUE_MASK;
    shopContext = vm_net_mock_npc_service_context_get(session, role);
    memset(&transaction, 0, sizeof(transaction));
    memset(&mailboxView, 0, sizeof(mailboxView));
    transactionConfirm = operation ==
                         (VM_NET_MOCK_NPC_SERVICE_CONFIRM_TRANSACTION &
                          VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK);
    transactionCancel = operation ==
                        (VM_NET_MOCK_NPC_SERVICE_CANCEL_TRANSACTION &
                         VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK);
    if (transactionConfirm || transactionCancel)
    {
        if (!vm_net_mock_npc_transaction_context_take(session, role,
                                                        shopContext,
                                                        &transaction))
        {
            action = transactionConfirm ? "transaction-confirm-invalid"
                                        : "transaction-cancel-invalid";
            goto npc_service_serialize;
        }
        if (transactionCancel)
        {
            if (transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_BUY)
            {
                operation = VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE;
                value = (transaction.page <<
                         VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_SHIFT) |
                        transaction.selector;
                action = "shop-buy-cancel";
            }
            else if (transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_SELL ||
                     transaction.kind ==
                         VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO)
            {
                operation = VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE;
                value = transaction.page;
                action = "equipment-sell-cancel";
            }
            else if (transaction.kind ==
                     VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN)
            {
                operation = VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_LEARN_BASE;
                value = transaction.page;
                action = "skill-learn-cancel";
            }
            else if (transaction.kind ==
                     VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS)
            {
                operation = VM_NET_MOCK_NPC_SERVICE_OPEN_CRYSTAL_SYNTHESIS_BASE;
                value = transaction.page;
                action = "crystal-synthesis-cancel";
            }
            else
            {
                operation = VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_FORGET_BASE;
                value = transaction.page;
                action = "skill-forget-cancel";
            }
            serviceValue = operation | value;
            restoredListPage = transaction.page;
        }
        else if (transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_BUY)
        {
            operation = VM_NET_MOCK_NPC_SERVICE_BUY_ITEM_BASE;
            value = transaction.itemId;
            serviceValue = operation | value;
        }
        else if (transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_SELL)
        {
            operation = VM_NET_MOCK_NPC_SERVICE_SELL_EQUIPMENT_BASE;
            value = transaction.backpackSeq;
            serviceValue = operation | value;
        }
        else if (transaction.kind ==
                 VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO)
        {
            operation = VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO_BASE;
            value = 1;
            serviceValue = operation | value;
        }
        else if (transaction.kind ==
                 VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN)
        {
            operation = VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE;
            value = transaction.itemId;
            serviceValue = operation | value;
        }
        else if (transaction.kind ==
                 VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS)
        {
            operation = VM_NET_MOCK_NPC_SERVICE_SYNTHESIZE_CRYSTAL_BASE;
            value = transaction.itemId;
            serviceValue = operation | value;
        }
        else
        {
            operation = VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE;
            value = transaction.itemId;
            serviceValue = operation | value;
        }
    }
    else
    {
        /* Any unrelated nested service selection makes a prior quote stale.
         * A second first-click below records its replacement explicitly. */
        vm_net_mock_npc_transaction_context_clear(session);
    }

    if (operation == VM_NET_MOCK_NPC_SERVICE_OPEN_CRYSTAL_SYNTHESIS_BASE ||
        operation == VM_NET_MOCK_NPC_SERVICE_SYNTHESIZE_CRYSTAL_BASE)
    {
        const bool synthesisRequest =
            operation == VM_NET_MOCK_NPC_SERVICE_SYNTHESIZE_CRYSTAL_BASE;
        const u32 total = VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_LAST -
                          VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_FIRST + 1u;
        u32 page = synthesisRequest
                       ? vm_net_mock_crystal_synthesis_item_page(value)
                       : value;
        u32 start = 0;

        if (!vm_net_mock_npc_service_context_has(
                shopContext, VM_NET_MOCK_NPC_KIND_CRYSTAL_SYNTHESIS))
        {
            action = "crystal-synthesis-unauthorized";
        }
        else if (!synthesisRequest)
        {
            dialogText =
                "\xca\xae\xb8\xf6\xd0\xfe\xbe\xa7\xcb\xe9\xc6\xac\xbf\xc9\xba\xcf\xb3\xc9\xd2\xbb\xbc\xb6\xd0\xfe\xbe\xa7\xa3\xbb\xc8\xfd\xb8\xf6\xcd\xac\xbc\xb6\xd0\xfe\xbe\xa7\xbf\xc9\xba\xcf\xb3\xc9\xb8\xdf\xd2\xbb\xbc\xb6\xd0\xfe\xbe\xa7\xa1\xa3"; /* 十个玄晶碎片可合成一级玄晶；三个同级玄晶可合成高一级玄晶。 */
            action = "crystal-synthesis-list";
        }
        else
        {
            vm_net_mock_backpack_item_state *source = NULL;
            u32 resultItemId = 0;
            u32 materialCount = 0;

            if (!vm_net_mock_crystal_synthesis_recipe(value, &resultItemId))
            {
                action = "crystal-synthesis-invalid-recipe";
            }
            else
            {
                materialCount = vm_net_mock_crystal_synthesis_material_count(
                    value);
                source = vm_net_mock_role_find_backpack_item(role, value, 0);
                if (materialCount == 0 || source == NULL ||
                    source->count < materialCount)
                {
                    char sourceLabel[64];

                    memset(sourceLabel, 0, sizeof(sourceLabel));
                    vm_net_mock_crystal_synthesis_item_label(
                        value, sourceLabel, sizeof(sourceLabel));
                    snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                             "\xb2\xc4\xc1\xcf\xb2\xbb\xd7\xe3\xa3\xac\xd0\xe8\xd2\xaa\x25\x75\xb8\xf6\x25\x73\xa1\xa3", /* 材料不足，需要%u个%s。 */
                             materialCount, sourceLabel);
                    dialogText = dialogTextStorage;
                    action = "crystal-synthesis-material-insufficient";
                }
                else if (!transactionConfirm)
                {
                    char sourceLabel[64];
                    char resultLabel[64];

                    memset(sourceLabel, 0, sizeof(sourceLabel));
                    memset(resultLabel, 0, sizeof(resultLabel));
                    vm_net_mock_crystal_synthesis_item_label(
                        value, sourceLabel, sizeof(sourceLabel));
                    vm_net_mock_crystal_synthesis_item_label(
                        resultItemId, resultLabel, sizeof(resultLabel));
                    if (!vm_net_mock_npc_transaction_context_begin(
                            session, role, shopContext,
                            VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS,
                            value, 0, 0, page,
                            materialCount))
                    {
                        action = "crystal-synthesis-prompt-invalid";
                    }
                    else
                    {
                        snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                                 "\xba\xcf\xb3\xc9\xc8\xb7\xc8\xcf\xa3\xba\x25\x75\xb8\xf6\x25\x73\x20\x2d\x3e\x20\x31\xb8\xf6\x25\x73", /* 合成确认：%u个%s -> 1个%s */
                                 materialCount, sourceLabel, resultLabel);
                        dialogText = dialogTextStorage;
                        optionNames[0] =
                            "\xc8\xb7\xc8\xcf\xba\xcf\xb3\xc9"; /* 确认合成 */
                        optionDescriptions[0] = dialogTextStorage;
                        optionValues[0] =
                            VM_NET_MOCK_NPC_SERVICE_CONFIRM_TRANSACTION;
                        optionNames[1] =
                            "\xd4\xdd\xb2\xbb\xba\xcf\xb3\xc9"; /* 暂不合成 */
                        optionDescriptions[1] =
                            "\xd4\xdd\xb2\xbb\xba\xcf\xb3\xc9"; /* 暂不合成 */
                        optionValues[1] =
                            VM_NET_MOCK_NPC_SERVICE_CANCEL_TRANSACTION;
                        optionCount = 2;
                        action = "crystal-synthesis-prompt";
                        goto npc_service_serialize;
                    }
                }
                else if (transaction.kind !=
                             VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS ||
                         transaction.itemId != value ||
                         transaction.quotedPrice !=
                             materialCount)
                {
                    dialogText =
                        "\xba\xcf\xb3\xc9\xd7\xb4\xcc\xac\xd2\xd1\xb1\xe4\xbb\xaf\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xd1\xa1\xd4\xf1\xa1\xa3"; /* 合成状态已变化，请重新选择。 */
                    action = "crystal-synthesis-state-invalid";
                }
                else
                {
                    vm_net_mock_role_state before = *role;
                    vm_net_mock_backpack_item_state *resultItem = NULL;
                    u16 sourceSeq = 0;
                    u16 resultSeq = 0;
                    u32 sourceRemaining = 0;

                    if (!vm_net_mock_role_crystal_synthesize_in_memory(
                            role, value, &sourceSeq, &sourceRemaining,
                            &resultSeq))
                    {
                        dialogText =
                            "\xb1\xb3\xb0\xfc\xd2\xd1\xc2\xfa\xa3\xac\xce\xde\xb7\xa8\xb7\xc5\xc8\xeb\xba\xcf\xb3\xc9\xbd\xe1\xb9\xfb\xa1\xa3"; /* 背包已满，无法放入合成结果。 */
                        action = "crystal-synthesis-mutate-failed";
                    }
                    else if (!vm_net_mock_role_db_save("npc-crystal-synthesis"))
                    {
                        *role = before;
                        action = "crystal-synthesis-persist-failed";
                    }
                    else
                    {
                        resultItem = vm_net_mock_role_find_backpack_item(
                            role, resultItemId, resultSeq);
                        if (sourceSeq == 0 || resultSeq == 0 ||
                            resultItem == NULL || resultItem->count == 0)
                        {
                            *role = before;
                            if (!vm_net_mock_role_db_save(
                                    "npc-crystal-synthesis-rollback"))
                            {
                                return 0;
                            }
                            action = "crystal-synthesis-postcondition-failed";
                        }
                        else
                        {
                            /* The client did not issue the deferred backpack
                             * query after this action=1 response.  Keep the
                             * dialog first, then use the sequence-keyed 7/11
                             * update for the consumed stack and the native
                             * 7/15 reward delta for the one output item. */
                            crystalSynthesisRefresh = true;
                            crystalSynthesisSourceItemId = value;
                            crystalSynthesisSourceRemaining = sourceRemaining;
                            crystalSynthesisResultItemId = resultItemId;
                            crystalSynthesisResultTotal = resultItem->count;
                            crystalSynthesisSourceSeq = sourceSeq;
                            crystalSynthesisResultSeq = resultSeq;
                            dialogText =
                                "\xba\xcf\xb3\xc9\xb3\xc9\xb9\xa6\xa1\xa3"; /* 合成成功。 */
                            action = "crystal-synthesis-success";
                            result = 1;
                            restoredListPage = page;
                        }
                    }
                }
            }
        }

        if (vm_net_mock_npc_service_context_has(
                shopContext, VM_NET_MOCK_NPC_KIND_CRYSTAL_SYNTHESIS))
        {
            start = page * VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_PAGE_ITEMS;
            if (start >= total)
            {
                page = (total - 1u) /
                       VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_PAGE_ITEMS;
                start = page *
                        VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_PAGE_ITEMS;
            }
            for (u32 ordinal = start;
                 ordinal < total &&
                 ordinal < start +
                               VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_PAGE_ITEMS;
                 ++ordinal)
            {
                const u32 sourceItemId =
                    VM_NET_MOCK_CRYSTAL_SYNTHESIS_INPUT_ITEM_FIRST + ordinal;
                const u32 nextItemId = sourceItemId + 1u;
                const u32 materialCount =
                    vm_net_mock_crystal_synthesis_material_count(sourceItemId);
                vm_net_mock_backpack_item_state *source =
                    vm_net_mock_role_find_backpack_item(role, sourceItemId, 0);
                char sourceLabel[64];
                char resultLabel[64];
                u32 sourceCount = source != NULL ? source->count : 0;

                if (optionCount >=
                    VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
                {
                    return 0;
                }
                memset(sourceLabel, 0, sizeof(sourceLabel));
                memset(resultLabel, 0, sizeof(resultLabel));
                vm_net_mock_crystal_synthesis_item_label(
                    sourceItemId, sourceLabel, sizeof(sourceLabel));
                vm_net_mock_crystal_synthesis_item_label(
                    nextItemId, resultLabel, sizeof(resultLabel));
                snprintf(optionNameStorage[optionCount],
                         sizeof(optionNameStorage[optionCount]),
                         "\xba\xcf\xb3\xc9\x25\x73", resultLabel); /* 合成%s */
                snprintf(optionDescriptionStorage[optionCount],
                         sizeof(optionDescriptionStorage[optionCount]),
                         "\xcf\xfb\xba\xc4\x25\x75\xb8\xf6\x25\x73\xa3\xa8\xb5\xb1\xc7\xb0\x25\x75\xb8\xf6\xa3\xa9", /* 消耗%u个%s（当前%u个） */
                         materialCount, sourceLabel, sourceCount);
                optionNames[optionCount] = optionNameStorage[optionCount];
                optionDescriptions[optionCount] =
                    optionDescriptionStorage[optionCount];
                optionValues[optionCount] =
                    VM_NET_MOCK_NPC_SERVICE_SYNTHESIZE_CRYSTAL_BASE |
                    sourceItemId;
                ++optionCount;
            }
            if (page > 0)
            {
                optionNames[optionCount] =
                    "\xc9\xcf\xd2\xbb\xd2\xb3"; /* 上一页 */
                optionDescriptions[optionCount] = optionNames[optionCount];
                optionValues[optionCount] =
                    VM_NET_MOCK_NPC_SERVICE_OPEN_CRYSTAL_SYNTHESIS_BASE |
                    (page - 1u);
                ++optionCount;
            }
            if (start + VM_NET_MOCK_NPC_SERVICE_CRYSTAL_SYNTHESIS_PAGE_ITEMS <
                total)
            {
                optionNames[optionCount] =
                    "\xcf\xc2\xd2\xbb\xd2\xb3"; /* 下一页 */
                optionDescriptions[optionCount] = optionNames[optionCount];
                optionValues[optionCount] =
                    VM_NET_MOCK_NPC_SERVICE_OPEN_CRYSTAL_SYNTHESIS_BASE |
                    (page + 1u);
                ++optionCount;
            }
        }
    }
    else if (operation == VM_NET_MOCK_NPC_SERVICE_OPEN_MAILBOX_BASE ||
        operation == VM_NET_MOCK_NPC_SERVICE_OPEN_MAIL_BASE ||
        operation == VM_NET_MOCK_NPC_SERVICE_CLAIM_MAIL_BASE)
    {
        mailboxHandled = vm_net_mock_mailbox_build_dialog(
            role, shopContext, operation, value, &mailboxView);
        if (!mailboxHandled)
            return 0;
        dialogText = mailboxView.dialog;
        optionCount = mailboxView.optionCount;
        for (u32 i = 0; i < optionCount; ++i)
        {
            optionNames[i] = mailboxView.optionNames[i];
            optionDescriptions[i] = mailboxView.optionDescriptions[i];
            optionValues[i] = mailboxView.optionValues[i];
        }
        action = mailboxView.action;
        result = mailboxView.result;
        restoredListPage = mailboxView.page;
        mailboxClaimRefresh = result == 1 &&
                              strcmp(action, "mailbox-claim") == 0 &&
                              mailboxView.claimedItemCount != 0;
    }
    else if (operation == VM_NET_MOCK_NPC_SERVICE_OPEN_INSTANCE_BASE ||
        operation == VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE ||
        operation == VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE)
    {
        instanceSeed = vm_net_mock_instance_guide_seed(value);
        action = operation == VM_NET_MOCK_NPC_SERVICE_OPEN_INSTANCE_BASE
                     ? "instance-menu"
                     : (operation == VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE
                            ? "instance-enter"
                            : "instance-challenge");
        if (instanceSeed == NULL ||
            !vm_net_mock_npc_service_context_has(
                shopContext, VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE) ||
            shopContext->actorId != instanceSeed->actorId)
        {
            dialogText =
                "\xb8\xb1\xb1\xbe\xc5\xe4\xd6\xc3\xd2\xd1\xca\xa7\xd0\xa7\xa1\xa3"; /* 副本配置已失效。 */
        }
        else if (role->level < instanceSeed->instanceMinLevel)
        {
            snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                     "%s%u%s",
                     "\xbd\xf8\xc8\xeb\xb8\xb1\xb1\xbe\xd0\xe8\xd2\xaa\xb5\xbd\xb4\xef", /* 进入副本需要到达 */
                     instanceSeed->instanceMinLevel,
                     "\xbc\xb6\xa1\xa3"); /* 级。 */
            dialogText = dialogTextStorage;
        }
        else if (operation == VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE)
        {
            u32 transferLen = vm_net_mock_build_instance_enter_response(
                instanceSeed, out, outCap);
            if (transferLen != 0)
                return transferLen;
            dialogText =
                "\xb8\xb1\xb1\xbe\xb4\xab\xcb\xcd\xb5\xe3\xce\xb4\xc5\xe4\xd6\xc3\xa1\xa3"; /* 副本传送点未配置。 */
        }
        else if (operation == VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE)
        {
            u32 challengeLen = vm_net_mock_build_instance_challenge_prompt_response(
                instanceSeed, out, outCap);
            if (challengeLen != 0)
                return challengeLen;
            dialogText =
                "\xb8\xb1\xb1\xbe\xcc\xf4\xd5\xbd\xb6\xd4\xcf\xf3\xce\xb4\xc5\xe4\xd6\xc3\xa1\xa3"; /* 副本挑战对象未配置。 */
        }
        else
        {
            dialogText =
                "\xc7\xeb\xd1\xa1\xd4\xf1\xb8\xb1\xb1\xbe\xb2\xd9\xd7\xf7\xa3\xba"; /* 请选择副本操作： */
            if (instanceSeed->instanceScene[0] != 0)
            {
                optionNames[optionCount] =
                    "\xbd\xf8\xc8\xeb\xb8\xb1\xb1\xbe"; /* 进入副本 */
                optionDescriptions[optionCount] =
                    "\xb4\xab\xcb\xcd\xb5\xbd\xb8\xb1\xb1\xbe\xb3\xa1\xbe\xb0"; /* 传送到副本场景 */
                optionValues[optionCount] =
                    VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE |
                    instanceSeed->actorId;
                ++optionCount;
            }
            if (instanceSeed->challengeEnemyId != 0 &&
                optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
            {
                instanceChallengeOptionIndex = optionCount;
                optionNames[optionCount] =
                    "\xcc\xf4\xd5\xbd\xca\xd8\xb9\xd8\xb9\xd6"; /* 挑战守关怪 */
                optionDescriptions[optionCount] =
                    "\xbf\xaa\xca\xbc\xb8\xb1\xb1\xbe\xd5\xbd\xb6\xb7"; /* 开始副本战斗 */
                /* task_hall_activate_selected_entry action 13 is the native
                 * scene challenge path.  It emits 4/1 {id,index,posx,posy};
                 * using the monster id here lets the client select its live
                 * scene node before the same event receives its 4/5 start. */
                optionValues[optionCount] = instanceSeed->challengeEnemyId;
                ++optionCount;
            }
        }
    }

    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_WEAPON)
    {
        static const u8 weaponSelectors[] = {8, 9, 10};

        action = "weapon-categories";
        if (!vm_net_mock_npc_service_context_has(
                shopContext, VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT))
        {
            dialogText =
                "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
        }
        else
        {
            dialogText =
                "\xc7\xeb\xd1\xa1\xd4\xf1\xce\xe4\xc6\xf7\xc0\xe0\xd0\xcd\xa3\xba"; /* 请选择武器类型： */
            for (u32 i = 0; i < sizeof(weaponSelectors); ++i)
            {
                u32 selector = weaponSelectors[i];
                if (vm_net_mock_npc_shop_selector_total(selector, shopContext) == 0)
                    continue;
                optionNames[optionCount] =
                    vm_net_mock_npc_shop_selector_name(selector);
                optionDescriptions[optionCount] =
                    "\xb2\xe9\xbf\xb4\xb8\xc3\xc0\xe0\xc9\xcc\xc6\xb7"; /* 查看该类商品 */
                optionValues[optionCount] =
                    VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE | selector;
                ++optionCount;
            }
            if (optionCount == 0)
                dialogText =
                    "\xd4\xdd\xce\xde\xbf\xc9\xb9\xba\xc2\xf2\xb5\xc4\xc9\xcc\xc6\xb7\xa1\xa3"; /* 暂无可购买的商品。 */
        }
    }
    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_ARMOR)
    {
        action = "armor-categories";
        if (!vm_net_mock_npc_service_context_has(
                shopContext, VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT))
        {
            dialogText =
                "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
        }
        else
        {
            dialogText =
                "\xc7\xeb\xd1\xa1\xd4\xf1\xb7\xc0\xbe\xdf\xc0\xe0\xd0\xcd\xa3\xba"; /* 请选择防具类型： */
            for (u32 selector = 1;
                 selector <= 7 &&
                 optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS;
                 ++selector)
            {
                if (vm_net_mock_npc_shop_selector_total(selector, shopContext) == 0)
                    continue;
                optionNames[optionCount] =
                    vm_net_mock_npc_shop_selector_name(selector);
                optionDescriptions[optionCount] =
                    "\xb2\xe9\xbf\xb4\xb8\xc3\xc0\xe0\xc9\xcc\xc6\xb7"; /* 查看该类商品 */
                optionValues[optionCount] =
                    VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE | selector;
                ++optionCount;
            }
            if (optionCount == 0)
                dialogText =
                    "\xd4\xdd\xce\xde\xbf\xc9\xb9\xba\xc2\xf2\xb5\xc4\xc9\xcc\xc6\xb7\xa1\xa3"; /* 暂无可购买的商品。 */
        }
    }
    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_MEDICINE ||
             operation == VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE ||
             operation == VM_NET_MOCK_NPC_SERVICE_BUY_ITEM_BASE ||
             operation == VM_NET_MOCK_NPC_SERVICE_BUY_WEAPON_BASE)
    {
        const vm_net_mock_shop_catalog_item *buyItem = NULL;
        u32 selector = 0;
        u32 page = 0;
        u32 total = 0;
        u32 start = 0;
        u32 unitPrice = 0;
        bool buyRequest = operation == VM_NET_MOCK_NPC_SERVICE_BUY_ITEM_BASE ||
                          operation == VM_NET_MOCK_NPC_SERVICE_BUY_WEAPON_BASE;
        bool buyPrompt = buyRequest && !transactionConfirm;
        bool buyConfirm = buyRequest && transactionConfirm;
        bool legacyWeaponBuy =
            operation == VM_NET_MOCK_NPC_SERVICE_BUY_WEAPON_BASE;

        action = buyPrompt ? "shop-buy-confirm-prompt"
                           : (buyConfirm ? "shop-buy"
                                         : "shop-category");
        if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_MEDICINE)
        {
            selector = VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR;
        }
        else if (operation == VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE)
        {
            selector = value & VM_NET_MOCK_NPC_SERVICE_CATEGORY_MASK;
            page = value >> VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_SHIFT;
            restoredListPage = page;
        }
        else
        {
            buyItem = vm_net_mock_find_shop_catalog_item(value);
            selector = vm_net_mock_npc_shop_selector_for_item(buyItem);
            if (legacyWeaponBuy && (selector < 8u || selector > 10u))
                selector = 0;
            if (buyConfirm)
            {
                selector = transaction.selector;
                page = transaction.page;
            }
        }
        if (buyPrompt)
        {
            page = vm_net_mock_npc_shop_item_page(buyItem, selector,
                                                   shopContext);
            restoredListPage = page;
        }
        else if (buyConfirm)
        {
            restoredListPage = page;
        }

        if (!vm_net_mock_npc_shop_selector_is_valid(selector) ||
            shopContext == NULL ||
            !vm_net_mock_npc_shop_selector_allowed_for_service(
                selector, shopContext->serviceMask))
        {
            dialogText =
                "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
        }
        else
        {
            dialogText = selector == VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR
                             ? "\xc7\xeb\xd1\xa1\xd4\xf1\xd2\xaa\xb9\xba\xc2\xf2\xb5\xc4\xd2\xa9\xc6\xb7\xa3\xba" /* 请选择要购买的药品： */
                             : "\xc7\xeb\xd1\xa1\xd4\xf1\xd2\xaa\xb9\xba\xc2\xf2\xb5\xc4\xc9\xcc\xc6\xb7\xa3\xba"; /* 请选择要购买的商品： */
        }

        if (buyPrompt)
        {
            if (buyItem == NULL ||
                !vm_net_mock_npc_shop_item_matches_selector(
                    buyItem, selector, shopContext, &unitPrice) ||
                !vm_net_mock_npc_transaction_context_begin(
                    session, role, shopContext,
                    VM_MOCK_SERVICE_NPC_TRANSACTION_BUY, buyItem->itemId,
                    0, selector, page, unitPrice))
            {
                dialogText =
                    "\xb8\xc3\xc9\xcc\xc6\xb7\xd2\xd1\xcf\xc2\xbc\xdc\xa1\xa3"; /* 该商品已下架。 */
            }
            else
            {
                const vm_net_mock_equipment_catalog_item *equipment =
                    buyItem->isEquip
                        ? vm_net_mock_find_equipment_catalog_item(buyItem->itemId)
                        : NULL;
                const vm_net_mock_item_effect_catalog_item *effect =
                    !buyItem->isEquip
                        ? vm_net_mock_find_item_effect_catalog_item(buyItem->itemId)
                        : NULL;

                snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                         "%s%s", "\xb9\xba\xc2\xf2\xc8\xb7\xc8\xcf\xa3\xba",
                         buyItem->name); /* 购买确认 */
                dialogText = dialogTextStorage;
                snprintf(optionNameStorage[0], sizeof(optionNameStorage[0]),
                         "%s %u%s", "\xc8\xb7\xc8\xcf\xb9\xba\xc2\xf2",
                         unitPrice, "\xcd\xad"); /* 确认购买 */
                if (equipment != NULL)
                {
                    vm_net_mock_format_npc_equipment_option_description(
                        optionDescriptionStorage[0],
                        sizeof(optionDescriptionStorage[0]), equipment);
                }
                else if (effect != NULL)
                {
                    vm_net_mock_format_npc_item_effect_option_description(
                        optionDescriptionStorage[0],
                        sizeof(optionDescriptionStorage[0]), effect);
                }
                else
                {
                    snprintf(optionDescriptionStorage[0],
                             sizeof(optionDescriptionStorage[0]), "%s",
                             buyItem->name);
                }
                vm_net_mock_append_npc_option_price_description(
                    optionDescriptionStorage[0],
                    sizeof(optionDescriptionStorage[0]),
                    "\xbc\xdb\xb8\xf1\xa3\xba", unitPrice); /* 价格 */
                vm_net_mock_append_npc_confirmation_detail(
                    dialogTextStorage, sizeof(dialogTextStorage),
                    optionDescriptionStorage[0]);
                optionNames[0] = optionNameStorage[0];
                optionDescriptions[0] = optionDescriptionStorage[0];
                optionValues[0] = VM_NET_MOCK_NPC_SERVICE_CONFIRM_TRANSACTION;
                optionNames[1] = "\xb7\xb5\xbb\xd8\xc9\xcc\xc6\xb7\xc1\xd0\xb1\xed"; /* 返回商品列表 */
                optionDescriptions[1] =
                    "\xb2\xbb\xb9\xba\xc2\xf2\xb8\xc3\xc9\xcc\xc6\xb7"; /* 不购买该商品 */
                optionValues[1] = VM_NET_MOCK_NPC_SERVICE_CANCEL_TRANSACTION;
                optionCount = 2;
                goto npc_service_serialize;
            }
        }
        else if (buyRequest)
        {
            if (buyItem == NULL ||
                !vm_net_mock_npc_shop_item_matches_selector(
                    buyItem, selector, shopContext, &unitPrice))
            {
                dialogText =
                    "\xb8\xc3\xc9\xcc\xc6\xb7\xd2\xd1\xcf\xc2\xbc\xdc\xa1\xa3"; /* 该商品已下架。 */
            }
            else if (buyConfirm &&
                     (transaction.kind != VM_MOCK_SERVICE_NPC_TRANSACTION_BUY ||
                      transaction.itemId != buyItem->itemId ||
                      transaction.selector != selector ||
                      transaction.quotedPrice != unitPrice))
            {
                dialogText =
                    "\xbc\xdb\xb8\xf1\xd2\xd1\xb1\xe4\xb8\xfc\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xd1\xa1\xd4\xf1\xa1\xa3"; /* 价格已变更，请重新选择。 */
            }
            else if (role->money < unitPrice)
            {
                dialogText =
                    "\xcd\xad\xc7\xae\xb2\xbb\xd7\xe3\xa3\xac\xce\xde\xb7\xa8\xb9\xba\xc2\xf2\xa1\xa3"; /* 铜钱不足，无法购买。 */
            }
            else
            {
                /* vm_net_mock_role_add_backpack_item_to_role persists its
                 * mutation.  Retain the complete pre-purchase state so an
                 * impossible postcondition can be rolled back consistently
                 * instead of refunding only money in RAM after the item has
                 * already reached MySQL. */
                vm_net_mock_role_state purchaseBefore = *role;
                role->money -= unitPrice;
                if (!vm_net_mock_role_add_backpack_item_to_role(
                        role, buyItem->itemId, 1, &backpackAddSeq,
                        buyItem->isEquip ? "npc-equipment-buy"
                                         : "npc-medicine-buy"))
                {
                    role->money += unitPrice;
                    dialogText =
                        "\xb1\xb3\xb0\xfc\xd2\xd1\xc2\xfa\xa3\xac\xce\xde\xb7\xa8\xb9\xba\xc2\xf2\xa1\xa3"; /* 背包已满，无法购买。 */
                }
                else
                {
                    vm_net_mock_backpack_item_state *purchasedItem =
                        vm_net_mock_role_find_backpack_item(
                            role, buyItem->itemId, backpackAddSeq);

                    /* This action-1 request is owned by the task-hall dialog
                     * parser.  Its only completion object is 26/1.  The
                     * persisted backpack row will be read by the next native
                     * backpack query; appending item-manager 7/7 here crosses
                     * parser ownership and re-arms the action wait. */
                    if (purchasedItem == NULL || backpackAddSeq == 0)
                    {
                        *role = purchaseBefore;
                        if (!vm_net_mock_role_db_save("npc-purchase-rollback"))
                            return 0;
                        dialogText =
                            "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
                    }
                    else
                    {
                        u32 purchasedItemCount =
                            vm_net_mock_backpack_item_id_uses_reservoir_count(
                                buyItem->itemId)
                                ? purchasedItem->count
                                : 1;
                        if (purchasedItemCount == 0)
                        {
                            *role = purchaseBefore;
                            if (!vm_net_mock_role_db_save("npc-purchase-rollback"))
                                return 0;
                            dialogText =
                                "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
                        }
                        else
                        {
                            dialogText =
                                "\xb9\xba\xc2\xf2\xb3\xc9\xb9\xa6\xa1\xa3"; /* 购买成功。 */
                            vm_net_mock_backpack_queue_authoritative_role_list(
                                buyItem->isEquip ? "npc-equipment-buy"
                                                 : "npc-medicine-buy");
                            result = 1;
                        }
                    }
                }
            }
            page = buyConfirm ? transaction.page : 0;
            if (buyConfirm)
                restoredListPage = page;
        }

        total = vm_net_mock_npc_shop_selector_total(selector, shopContext);
        start = page * VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
        if (total != 0 && start >= total)
        {
            page = (total - 1u) /
                   VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
            start = page * VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
        }
        for (u32 ordinal = start;
             ordinal < total &&
             ordinal < start + VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
             ++ordinal)
        {
            const vm_net_mock_shop_catalog_item *item =
                vm_net_mock_npc_shop_selector_item_at(selector, ordinal,
                                                      shopContext, &unitPrice);
            const vm_net_mock_equipment_catalog_item *equipment = NULL;
            const vm_net_mock_item_effect_catalog_item *effect = NULL;
            u32 levelRequired = 1;

            if (item == NULL)
                continue;
            if (item->isEquip)
            {
                equipment = vm_net_mock_find_equipment_catalog_item(item->itemId);
                if (equipment != NULL)
                    levelRequired = equipment->levelRequired;
            }
            else
            {
                effect = vm_net_mock_find_item_effect_catalog_item(item->itemId);
                if (effect != NULL)
                    levelRequired = effect->levelRequired;
            }
            snprintf(optionNameStorage[optionCount],
                     sizeof(optionNameStorage[optionCount]), "%s%s %u%s",
                     "\xb9\xba\xc2\xf2", item->name, unitPrice,
                     "\xcd\xad"); /* 购买...铜 */
            if (equipment != NULL)
            {
                vm_net_mock_format_npc_equipment_option_description(
                    optionDescriptionStorage[optionCount],
                    sizeof(optionDescriptionStorage[optionCount]), equipment);
            }
            else if (effect != NULL)
            {
                vm_net_mock_format_npc_item_effect_option_description(
                    optionDescriptionStorage[optionCount],
                    sizeof(optionDescriptionStorage[optionCount]), effect);
            }
            else
            {
                snprintf(optionDescriptionStorage[optionCount],
                         sizeof(optionDescriptionStorage[optionCount]),
                         "%s Lv.%u", item->name, levelRequired);
            }
            optionNames[optionCount] = optionNameStorage[optionCount];
            optionDescriptions[optionCount] =
                optionDescriptionStorage[optionCount];
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_BUY_ITEM_BASE | item->itemId;
            ++optionCount;
        }
        if (page > 0 &&
            optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
        {
            optionNames[optionCount] =
                "\xc9\xcf\xd2\xbb\xd2\xb3"; /* 上一页 */
            optionDescriptions[optionCount] =
                vm_net_mock_npc_shop_selector_name(selector);
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE |
                ((page - 1u) << VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_SHIFT) |
                selector;
            ++optionCount;
        }
        if (start + VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS < total &&
            optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
        {
            optionNames[optionCount] =
                "\xcf\xc2\xd2\xbb\xd2\xb3"; /* 下一页 */
            optionDescriptions[optionCount] =
                vm_net_mock_npc_shop_selector_name(selector);
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE |
                ((page + 1u) << VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_SHIFT) |
                selector;
            ++optionCount;
        }
        if (optionCount == 0 && result == 0)
            dialogText =
                "\xd4\xdd\xce\xde\xbf\xc9\xb9\xba\xc2\xf2\xb5\xc4\xc9\xcc\xc6\xb7\xa1\xa3"; /* 暂无可购买的商品。 */
    }
    else if (operation == VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE ||
             operation == VM_NET_MOCK_NPC_SERVICE_SELL_EQUIPMENT_BASE ||
             operation == VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO_BASE)
    {
        u32 page = operation == VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE
                       ? value
                       : 0;
        u32 total = 0;
        u32 start = 0;
        bool saleRequest =
            operation == VM_NET_MOCK_NPC_SERVICE_SELL_EQUIPMENT_BASE;
        bool salePrompt = saleRequest && !transactionConfirm;
        bool saleConfirm = saleRequest && transactionConfirm;
        bool qualityZeroSaleRequest =
            operation == VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO_BASE;
        bool qualityZeroSalePrompt =
            qualityZeroSaleRequest && !transactionConfirm;
        bool qualityZeroSaleConfirm =
            qualityZeroSaleRequest && transactionConfirm;

        restoredListPage = page;

        action = qualityZeroSalePrompt
                     ? "equipment-sell-quality-zero-confirm-prompt"
                     : (qualityZeroSaleConfirm
                            ? "equipment-sell-quality-zero"
                            : (salePrompt ? "equipment-sell-confirm-prompt"
                                          : (saleConfirm
                                                 ? "equipment-sell"
                                                 : "equipment-sell-list")));
        if (!vm_net_mock_npc_service_context_has(
                shopContext, VM_NET_MOCK_NPC_KIND_EQUIPMENT_BUYER))
        {
            dialogText =
                "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
        }
        else
        {
        dialogText =
            "\xc7\xeb\xd1\xa1\xd4\xf1\xd2\xaa\xb3\xf6\xca\xdb\xb5\xc4\xd7\xb0\xb1\xb8\xa3\xba"; /* 请选择要出售的装备： */

        if (qualityZeroSalePrompt)
        {
            u32 qualityZeroCount = 0;
            u32 price = 0;

            page = 0;
            restoredListPage = page;
            if (value != 1 || !vm_net_mock_npc_collect_quality_zero_equipment(
                                  role, NULL, NULL, 0, &qualityZeroCount,
                                  &price) ||
                !vm_net_mock_npc_transaction_context_begin(
                    session, role, shopContext,
                    VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO,
                    VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO, 0,
                    qualityZeroCount, page, price))
            {
                dialogText =
                    "\xb5\xb1\xc7\xb0\xc3\xbb\xd3\xd0\xc6\xb7\xd6\xca\x30\xd7\xb0\xb1\xb8\xa1\xa3"; /* 当前没有品质0装备。 */
            }
            else
            {
                snprintf(dialogTextStorage, sizeof(dialogTextStorage), "%s",
                         "\xbb\xd8\xca\xd5\xc8\xb7\xc8\xcf\xa3\xba\xc6\xb7\xd6\xca\x30\xd7\xb0\xb1\xb8"); /* 回收确认：品质0装备 */
                dialogText = dialogTextStorage;
                snprintf(optionNameStorage[0], sizeof(optionNameStorage[0]),
                         "%s %u%s +%u%s", "\xc8\xb7\xc8\xcf\xbb\xd8\xca\xd5",
                         qualityZeroCount, "\xbc\xfe", price, "\xcd\xad"); /* 确认回收 %u件 +%u铜 */
                snprintf(optionDescriptionStorage[0],
                         sizeof(optionDescriptionStorage[0]),
                         "\xd2\xbb\xbc\xfc\xbb\xd8\xca\xd5\xb1\xb3\xb0\xfc\xd6\xd0\xcb\xf9\xd3\xd0\xc6\xb7\xd6\xca\x30\xd7\xb0\xb1\xb8\xa3\xac\xb9\xb2%u\xbc\xfe\xa3\xac\xbb\xf1\xb5\xc3%u\xcd\xad\xc7\xae\xa1\xa3",
                         qualityZeroCount, price); /* 一键回收背包中所有品质0装备，共%u件，获得%u铜钱。 */
                vm_net_mock_append_npc_confirmation_detail(
                    dialogTextStorage, sizeof(dialogTextStorage),
                    optionDescriptionStorage[0]);
                optionNames[0] = optionNameStorage[0];
                optionDescriptions[0] = optionDescriptionStorage[0];
                optionValues[0] = VM_NET_MOCK_NPC_SERVICE_CONFIRM_TRANSACTION;
                optionNames[1] =
                    "\xb7\xb5\xbb\xd8\xd7\xb0\xb1\xb8\xc1\xd0\xb1\xed"; /* 返回装备列表 */
                optionDescriptions[1] =
                    "\xb2\xbb\xbb\xd8\xca\xd5\xc6\xb7\xd6\xca\x30\xd7\xb0\xb1\xb8"; /* 不回收品质0装备 */
                optionValues[1] = VM_NET_MOCK_NPC_SERVICE_CANCEL_TRANSACTION;
                optionCount = 2;
                goto npc_service_serialize;
            }
        }
        else if (salePrompt)
        {
            vm_net_mock_backpack_item_state *backpackItem = NULL;
            const vm_net_mock_shop_catalog_item *catalogItem = NULL;
            u32 price = 0;

            page = 0;
            if (value != 0 && value <= 0xffffu)
            {
                page = vm_net_mock_npc_sell_equipment_item_page(
                    role, (u16)value);
                restoredListPage = page;
            }
            if (value == 0 || value > 0xffffu ||
                (backpackItem = vm_net_mock_role_find_backpack_item(
                     role, 0, (u16)value)) == NULL ||
                !vm_net_mock_npc_sell_backpack_item_matches(
                    backpackItem, &catalogItem) ||
                (price = vm_net_mock_npc_sell_equipment_price(catalogItem)) == 0 ||
                !vm_net_mock_npc_transaction_context_begin(
                    session, role, shopContext,
                    VM_MOCK_SERVICE_NPC_TRANSACTION_SELL, backpackItem->itemId,
                    backpackItem->seq, 0, page, price))
            {
                dialogText =
                    "\xb8\xc3\xd7\xb0\xb1\xb8\xd2\xd1\xb2\xbb\xd4\xda\xb1\xb3\xb0\xfc\xd6\xd0\xa1\xa3"; /* 该装备已不在背包中。 */
            }
            else
            {
                const vm_net_mock_equipment_catalog_item *equipment =
                    vm_net_mock_find_equipment_catalog_item(catalogItem->itemId);

                snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                         "%s%s +%u", "\xbb\xd8\xca\xd5\xc8\xb7\xc8\xcf\xa3\xba",
                         catalogItem->name, backpackItem->enhanceLevel); /* 回收确认 */
                dialogText = dialogTextStorage;
                snprintf(optionNameStorage[0], sizeof(optionNameStorage[0]),
                         "%s +%u%s", "\xc8\xb7\xc8\xcf\xbb\xd8\xca\xd5",
                         price, "\xcd\xad"); /* 确认回收 */
                if (equipment != NULL)
                {
                    vm_net_mock_format_npc_equipment_instance_option_description(
                        optionDescriptionStorage[0],
                        sizeof(optionDescriptionStorage[0]), equipment,
                        backpackItem->enhanceLevel);
                }
                else
                {
                    snprintf(optionDescriptionStorage[0],
                             sizeof(optionDescriptionStorage[0]), "%s +%u",
                             catalogItem->name, backpackItem->enhanceLevel);
                }
                vm_net_mock_append_npc_option_price_description(
                    optionDescriptionStorage[0],
                    sizeof(optionDescriptionStorage[0]),
                    "\xbb\xd8\xca\xd5\xbc\xdb\xa3\xba", price); /* 回收价 */
                vm_net_mock_append_npc_confirmation_detail(
                    dialogTextStorage, sizeof(dialogTextStorage),
                    optionDescriptionStorage[0]);
                optionNames[0] = optionNameStorage[0];
                optionDescriptions[0] = optionDescriptionStorage[0];
                optionValues[0] = VM_NET_MOCK_NPC_SERVICE_CONFIRM_TRANSACTION;
                optionNames[1] = "\xb7\xb5\xbb\xd8\xd7\xb0\xb1\xb8\xc1\xd0\xb1\xed"; /* 返回装备列表 */
                optionDescriptions[1] =
                    "\xb2\xbb\xbb\xd8\xca\xd5\xb8\xc3\xd7\xb0\xb1\xb8"; /* 不回收该装备 */
                optionValues[1] = VM_NET_MOCK_NPC_SERVICE_CANCEL_TRANSACTION;
                optionCount = 2;
                goto npc_service_serialize;
            }
        }
        else if (qualityZeroSaleRequest)
        {
            vm_net_mock_role_state before;
            u32 qualityZeroCount = 0;
            u32 price = 0;
            u32 recycledCount = 0;
            u32 recycledPrice = 0;

            page = qualityZeroSaleConfirm ? transaction.page : 0;
            if (qualityZeroSaleConfirm)
                restoredListPage = page;
            if (value != 1 || !vm_net_mock_npc_collect_quality_zero_equipment(
                                  role, NULL, NULL, 0, &qualityZeroCount,
                                  &price))
            {
                dialogText =
                    "\xb5\xb1\xc7\xb0\xc3\xbb\xd3\xd0\xc6\xb7\xd6\xca\x30\xd7\xb0\xb1\xb8\xa1\xa3"; /* 当前没有品质0装备。 */
            }
            else if (qualityZeroSaleConfirm &&
                     (transaction.kind !=
                          VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO ||
                      transaction.itemId !=
                          VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO ||
                      transaction.selector != qualityZeroCount ||
                      transaction.quotedPrice != price))
            {
                dialogText =
                    "\xc6\xb7\xd6\xca\x30\xd7\xb0\xb1\xb8\xd7\xb4\xcc\xac\xd2\xd1\xb1\xe4\xb8\xfc\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xd1\xa1\xd4\xf1\xa1\xa3"; /* 品质0装备状态已变更，请重新选择。 */
            }
            else
            {
                const u32 moneyBefore = role->money;

                before = *role;
                if (!vm_net_mock_role_recycle_quality_zero_equipment_in_memory(
                        role, qualityZeroCount, price, &recycledCount,
                        &recycledPrice))
                {
                    dialogText =
                        "\xc6\xb7\xd6\xca\x30\xd7\xb0\xb1\xb8\xd7\xb4\xcc\xac\xd2\xd1\xb1\xe4\xb8\xfc\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xd1\xa1\xd4\xf1\xa1\xa3"; /* 品质0装备状态已变更，请重新选择。 */
                }
                else if (!vm_net_mock_role_db_save(
                             "npc-equipment-sell-quality-zero"))
                {
                    *role = before;
                    dialogText =
                        "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
                }
                else
                {
                    const char *auditAccountId =
                        session != NULL && session->accountId[0] != 0
                            ? session->accountId
                            : g_vm_mock_service_active_account_id;

                    result = 1;
                    if (auditAccountId != NULL && auditAccountId[0] != 0)
                    {
                        char operationDetail[256];

                        snprintf(operationDetail, sizeof(operationDetail),
                                 "一键回收品质0装备 %u 件，获得铜钱 %u，余额 %u→%u",
                                 recycledCount, recycledPrice, moneyBefore,
                                 role->money);
                        if (!vm_mock_admin_operation_log_record(
                                "recycle-quality-zero-equipment",
                                auditAccountId, role->roleId, 0,
                                recycledCount, recycledPrice, operationDetail,
                                NULL))
                        {
                            printf("[error][mock-service] "
                                   "operation_log_quality_zero_equipment_recycle_failed "
                                   "account=%s role=%u count=%u price=%u error=%s\n",
                                   auditAccountId, role->roleId, recycledCount,
                                   recycledPrice, vm_mysql_last_error());
                        }
                    }
                    /* A 26/1 action=1 request must complete with its dialog
                     * alone.  Item-manager and HUD objects in this callback
                     * make the CBE reject the response before it can show
                     * the result.  The committed role snapshot is queued for
                     * the next native backpack query instead. */
                    vm_net_mock_backpack_queue_authoritative_role_list(
                        "npc-equipment-sell-quality-zero");
                    snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                             "\xd2\xbb\xbc\xfc\xbb\xd8\xca\xd5\xb3\xc9\xb9\xa6\xa3\xac\xbb\xf1\xb5\xc3%u\xcd\xad\xc7\xae\xa1\xa3",
                             recycledPrice); /* 一键回收成功，获得%u铜钱。 */
                    dialogText = dialogTextStorage;
                    /* A completed NPC dialog must retain a native action.
                     * With zero options the firmware's screen rebuild takes
                     * its empty-list presentation branch and replaces this
                     * success text.  Returning to the list is a new normal
                     * user action and reads the already committed role. */
                    optionNames[0] =
                        "\xb7\xb5\xbb\xd8\xd7\xb0\xb1\xb8\xc1\xd0\xb1\xed"; /* 返回装备列表 */
                    optionDescriptions[0] =
                        "\xd2\xbb\xbc\xfc\xbb\xd8\xca\xd5\xd2\xd1\xcd\xea\xb3\xc9"; /* 一键回收已完成 */
                    optionValues[0] =
                        VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE |
                        page;
                    optionCount = 1;
                }
            }
        }
        else if (saleRequest)
        {
            vm_net_mock_backpack_item_state *backpackItem = NULL;
            const vm_net_mock_shop_catalog_item *catalogItem = NULL;
            vm_net_mock_role_state before;
            u32 price = 0;

            page = saleConfirm ? transaction.page : 0;
            if (saleConfirm)
                restoredListPage = page;
            if (value == 0 || value > 0xffffu)
            {
                dialogText =
                    "\xb8\xc3\xd7\xb0\xb1\xb8\xd2\xd1\xb2\xbb\xd4\xda\xb1\xb3\xb0\xfc\xd6\xd0\xa1\xa3"; /* 该装备已不在背包中。 */
            }
            else
            {
                backpackItem = vm_net_mock_role_find_backpack_item(
                    role, 0, (u16)value);
                if (!vm_net_mock_npc_sell_backpack_item_matches(
                        backpackItem, &catalogItem) ||
                    (price = vm_net_mock_npc_sell_equipment_price(catalogItem)) == 0)
                {
                    dialogText =
                    "\xb8\xc3\xd7\xb0\xb1\xb8\xd2\xd1\xb2\xbb\xd4\xda\xb1\xb3\xb0\xfc\xd6\xd0\xa1\xa3"; /* 该装备已不在背包中。 */
                }
                else if (saleConfirm &&
                         (transaction.kind != VM_MOCK_SERVICE_NPC_TRANSACTION_SELL ||
                          transaction.itemId != backpackItem->itemId ||
                          transaction.backpackSeq != backpackItem->seq ||
                          transaction.quotedPrice != price))
                {
                    dialogText =
                        "\xb8\xc3\xd7\xb0\xb1\xb8\xd7\xb4\xcc\xac\xd2\xd1\xb1\xe4\xb8\xfc\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xd1\xa1\xd4\xf1\xa1\xa3"; /* 该装备状态已变更，请重新选择。 */
                }
                else
                {
                    const u32 soldItemId = backpackItem->itemId;
                    const u16 soldItemSeq = backpackItem->seq;
                    const u16 soldEnhanceLevel = backpackItem->enhanceLevel;
                    const u32 moneyBefore = role->money;

                    before = *role;
                    if (!vm_net_mock_role_consume_backpack_item(
                            role, soldItemId, soldItemSeq,
                            1, NULL))
                    {
                        dialogText =
                            "\xb8\xc3\xd7\xb0\xb1\xb8\xd2\xd1\xb2\xbb\xd4\xda\xb1\xb3\xb0\xfc\xd6\xd0\xa1\xa3"; /* 该装备已不在背包中。 */
                    }
                    else
                    {
                        role->money = vm_net_mock_add_capped_u32(role->money,
                                                                   price);
                        /* role_db_save_relational replaces this active role's
                         * backpack rows and money inside one MySQL transaction.
                         * Restore RAM only after its rollback path reports a
                         * failure; never leave the item deleted without the
                         * corresponding copper credit. */
                        if (!vm_net_mock_role_db_save("npc-equipment-sell"))
                        {
                            *role = before;
                            dialogText =
                                "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
                        }
                        else
                        {
                            const char *auditAccountId =
                                session != NULL && session->accountId[0] != 0
                                    ? session->accountId
                                    : g_vm_mock_service_active_account_id;

                            result = 1;
                            if (auditAccountId != NULL &&
                                auditAccountId[0] != 0)
                            {
                                char operationDetail[256];

                                snprintf(operationDetail,
                                         sizeof(operationDetail),
                                         "装备回收 ID %u（背包序号 %u，强化 +%u），"
                                         "获得铜钱 %u，余额 %u→%u",
                                         soldItemId, (u32)soldItemSeq,
                                         (u32)soldEnhanceLevel, price,
                                         moneyBefore, role->money);
                                if (!vm_mock_admin_operation_log_record(
                                        "recycle-equipment", auditAccountId,
                                        role->roleId, soldItemId, 1, price,
                                        operationDetail, NULL))
                                {
                                    printf("[error][mock-service] "
                                           "operation_log_equipment_recycle_failed "
                                           "account=%s role=%u item=%u seq=%u "
                                           "price=%u error=%s\n",
                                           auditAccountId, role->roleId,
                                           soldItemId, (u32)soldItemSeq,
                                           price, vm_mysql_last_error());
                                }
                            }
                            vm_net_mock_backpack_queue_authoritative_role_list(
                                "npc-equipment-sell");
                            snprintf(dialogTextStorage,
                                     sizeof(dialogTextStorage), "%s%u%s",
                                     "\xb3\xf6\xca\xdb\xb3\xc9\xb9\xa6\xa3\xac\xbb\xf1\xb5\xc3", /* 出售成功，获得 */
                                     price,
                                     "\xcd\xad\xc7\xae\xa1\xa3"); /* 铜钱。 */
                            dialogText = dialogTextStorage;
                        }
                    }
                }
            }
        }

        total = vm_net_mock_npc_sell_equipment_total(role);
        start = page * VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
        if (total != 0 && start >= total)
        {
            page = (total - 1u) /
                   VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
            start = page * VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
        }
        /* Keep the one-key action on the first list page.  That page still
         * fits five normal rows and its next-page control in the native
         * seven-option dialog; later pages retain both navigation controls. */
        if (page == 0 &&
            optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
        {
            u32 qualityZeroCount = 0;
            u32 qualityZeroPrice = 0;

            if (vm_net_mock_npc_collect_quality_zero_equipment(
                    role, NULL, NULL, 0, &qualityZeroCount,
                    &qualityZeroPrice))
            {
                snprintf(optionNameStorage[optionCount],
                         sizeof(optionNameStorage[optionCount]), "%s",
                         "\xd2\xbb\xbc\xfc\xbb\xd8\xca\xd5\xc6\xb7\xd6\xca\x30\xd7\xb0\xb1\xb8"); /* 一键回收品质0装备 */
                snprintf(optionDescriptionStorage[optionCount],
                         sizeof(optionDescriptionStorage[optionCount]),
                         "\xb9\xb2%u\xbc\xfe\xa3\xac\xd4\xa4\xbc\xc6\xbb\xf1\xb5\xc3%u\xcd\xad\xc7\xae\xa1\xa3",
                         qualityZeroCount, qualityZeroPrice); /* 共%u件，预计获得%u铜钱。 */
                optionNames[optionCount] = optionNameStorage[optionCount];
                optionDescriptions[optionCount] =
                    optionDescriptionStorage[optionCount];
                optionValues[optionCount] =
                    VM_NET_MOCK_NPC_SERVICE_SELL_QUALITY_ZERO;
                ++optionCount;
            }
        }
        for (u32 ordinal = start;
             ordinal < total &&
             ordinal < start + VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS;
             ++ordinal)
        {
            const vm_net_mock_shop_catalog_item *catalogItem = NULL;
            vm_net_mock_backpack_item_state *backpackItem =
                vm_net_mock_npc_sell_equipment_item_at(role, ordinal,
                                                       &catalogItem);
            u32 price = vm_net_mock_npc_sell_equipment_price(catalogItem);

            if (backpackItem == NULL || catalogItem == NULL || price == 0)
                continue;
            snprintf(optionNameStorage[optionCount],
                     sizeof(optionNameStorage[optionCount]), "%s%s %u%s",
                     "\xbb\xd8\xca\xd5", /* 回收 */
                     catalogItem->name, price,
                     "\xcd\xad"); /* 铜 */
            {
                const vm_net_mock_equipment_catalog_item *equipment =
                    vm_net_mock_find_equipment_catalog_item(catalogItem->itemId);

                if (equipment != NULL)
                {
                    vm_net_mock_format_npc_equipment_instance_option_description(
                        optionDescriptionStorage[optionCount],
                        sizeof(optionDescriptionStorage[optionCount]), equipment,
                        backpackItem->enhanceLevel);
                }
                else
                {
                    snprintf(optionDescriptionStorage[optionCount],
                             sizeof(optionDescriptionStorage[optionCount]),
                             "%s +%u", catalogItem->name,
                             backpackItem->enhanceLevel);
                }
            }
            optionNames[optionCount] = optionNameStorage[optionCount];
            optionDescriptions[optionCount] = optionDescriptionStorage[optionCount];
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_SELL_EQUIPMENT_BASE |
                backpackItem->seq;
            ++optionCount;
        }
        if (page > 0 &&
            optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
        {
            optionNames[optionCount] =
                "\xc9\xcf\xd2\xbb\xd2\xb3"; /* 上一页 */
            optionDescriptions[optionCount] =
                "\xd7\xb0\xb1\xb8\xbb\xd8\xca\xd5"; /* 装备回收 */
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE |
                (page - 1u);
            ++optionCount;
        }
        if (start + VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_ITEMS < total &&
            optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
        {
            optionNames[optionCount] =
                "\xcf\xc2\xd2\xbb\xd2\xb3"; /* 下一页 */
            optionDescriptions[optionCount] =
                "\xd7\xb0\xb1\xb8\xbb\xd8\xca\xd5"; /* 装备回收 */
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_EQUIPMENT_SELL_BASE |
                (page + 1u);
            ++optionCount;
        }
        if (optionCount == 0)
        {
            dialogText =
                "\xb5\xb1\xc7\xb0\xc3\xbb\xd3\xd0\xbf\xc9\xb3\xf6\xca\xdb\xb5\xc4\xd7\xb0\xb1\xb8\xa1\xa3"; /* 当前没有可出售的装备。 */
        }
        }
    }
    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_REPAIR_ALL)
    {
        u16 repairCount = 0;
        u32 repairCost = 0;

        action = "repair-all";
        if (!vm_net_mock_npc_service_context_has(
                shopContext, VM_NET_MOCK_NPC_KIND_EQUIPMENT_REPAIR))
        {
            dialogText =
                "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
        }
        else
        {
        serviceState = vm_net_mock_role_service_state_get(role);
        repairCost = vm_net_mock_role_service_repair_cost(serviceState, role,
                                                          &repairCount);
        if (repairCount == 0)
        {
            dialogText =
                "\xb5\xb1\xc7\xb0\xd7\xb0\xb1\xb8\xce\xde\xd0\xe8\xd0\xde\xc0\xed\xa1\xa3"; /* 当前装备无需修理。 */
            result = 1;
        }
        else if (role->money < repairCost)
        {
            dialogText =
                "\xcd\xad\xc7\xae\xb2\xbb\xd7\xe3\xa3\xac\xce\xde\xb7\xa8\xd0\xde\xc0\xed\xa1\xa3"; /* 铜钱不足，无法修理。 */
        }
        else if (vm_net_mock_role_service_repair_all(role, &repairCount,
                                                     &repairCost))
        {
            dialogText =
                "\xd7\xb0\xb1\xb8\xd2\xd1\xc8\xab\xb2\xbf\xd0\xde\xb8\xb4\xa1\xa3"; /* 装备已全部修复。 */
            result = 1;
        }
        }
    }
    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_SKILLS ||
             operation == VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_LEARN_BASE ||
             operation == VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_FORGET_BASE ||
             operation == VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE ||
             operation == VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE)
    {
        u8 rawJob = vm_net_mock_role_job_to_skill_raw_job(role->job);
        bool learnList = operation == VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_LEARN_BASE ||
                         operation == VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE;
        bool forgetList = operation == VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_FORGET_BASE ||
                          operation == VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE;
        bool skillMutationRequest =
            operation == VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE ||
            operation == VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE;
        u32 page = (learnList || forgetList) ? value : 0;
        u32 total = 0;
        u32 start = 0;
        u32 ordinal = 0;

        if (!transactionCancel)
        {
            action = serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_SKILLS
                         ? "skill-menu"
                         : (operation == VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE
                                ? (transactionConfirm
                                       ? "skill-learn"
                                       : "skill-learn-confirm-prompt")
                                : (operation == VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE
                                       ? (transactionConfirm
                                              ? "skill-forget"
                                              : "skill-forget-confirm-prompt")
                                       : (learnList ? "skill-learn-list"
                                                    : "skill-forget-list")));
        }
        if (!vm_net_mock_npc_service_context_has(
                shopContext, VM_NET_MOCK_NPC_KIND_SKILL_TRAINER))
        {
            dialogText =
                "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
        }
        else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_SKILLS)
        {
            dialogText =
                "\xc7\xeb\xd1\xa1\xd4\xf1\xbc\xbc\xc4\xdc\xb5\xbc\xca\xa6\xb7\xfe\xce\xf1\xa3\xba"; /* 请选择技能导师服务： */
            optionNames[0] =
                "\xd1\xa7\xcf\xb0\xd0\xc2\xbc\xbc\xc4\xdc"; /* 学习新技能 */
            optionDescriptions[0] =
                "\xb2\xe9\xbf\xb4\xb5\xb1\xc7\xb0\xb5\xc8\xbc\xb6\xbf\xc9\xd1\xa7\xcf\xb0\xb5\xc4\xbc\xbc\xc4\xdc"; /* 查看当前等级可学习的技能 */
            optionValues[0] = VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_LEARN_BASE;
            optionNames[1] =
                "\xd2\xc5\xcd\xfc\xd2\xd1\xd1\xa7\xbc\xbc\xc4\xdc"; /* 遗忘已学技能 */
            optionDescriptions[1] =
                "\xb2\xe9\xbf\xb4\xbf\xc9\xd2\xc5\xcd\xfc\xb5\xc4\xd2\xd1\xd1\xa7\xbc\xbc\xc4\xdc"; /* 查看可遗忘的已学技能 */
            optionValues[1] = VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_FORGET_BASE;
            optionCount = 2;
            result = 1;
        }
        else
        {
            serviceState = vm_net_mock_role_service_state_get(role);
            if (operation == VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE)
            {
                const vm_net_mock_skill_catalog_item *skill =
                    vm_net_mock_find_skill_catalog_item(value);
                page = vm_net_mock_npc_skill_list_item_page(
                    role, serviceState, false, value);
                restoredListPage = page;
                if (skill == NULL || skill->rawJob != rawJob)
                {
                    dialogText =
                        "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
                }
                else if (skill->levelRequired > role->level)
                {
                    snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                             "%s%u%s%s%s%u%s",
                             "\xb5\xb1\xc7\xb0\xb5\xc8\xbc\xb6", role->level,
                             "\xa3\xac", skill->name,
                             "\xd0\xe8\xd2\xaa", skill->levelRequired,
                             "\xbc\xb6\xa3\xac\xce\xde\xb7\xa8\xd1\xa7\xcf\xb0\xa1\xa3"); /* 当前等级...需要...级，无法学习。 */
                }
                else if (vm_net_mock_role_service_has_skill(serviceState,
                                                             skill->skillId))
                {
                    dialogText =
                        "\xb8\xc3\xbc\xbc\xc4\xdc\xd2\xd1\xbe\xad\xd1\xa7\xbb\xe1\xa1\xa3"; /* 该技能已经学会。 */
                }
                else if (role->money < skill->learnPrice)
                {
                    snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                             "%s%u%s",
                             "\xcd\xad\xc7\xae\xb2\xbb\xd7\xe3\xa3\xac\xd0\xe8\xd2\xaa",
                             skill->learnPrice,
                             "\xcd\xad\xc7\xae\xa1\xa3"); /* 铜钱不足，需要...铜钱。 */
                    dialogText = dialogTextStorage;
                }
                else if (!transactionConfirm)
                {
                    if (!vm_net_mock_npc_transaction_context_begin(
                            session, role, shopContext,
                            VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN,
                            skill->skillId, 0, 0, page, skill->learnPrice))
                    {
                        dialogText =
                            "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
                    }
                    else
                    {
                        snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                                 "%s%s\n%s%u\n%s%u%s",
                                 "\xd1\xa7\xcf\xb0\xc8\xb7\xc8\xcf\xa3\xba",
                                 skill->name,
                                 "\xb5\xc8\xbc\xb6\xa3\xba\x4c\x76\x2e",
                                 skill->levelRequired,
                                 "\xb7\xd1\xd3\xc3\xa3\xba", skill->learnPrice,
                                 "\xcd\xad\xc7\xae"); /* 学习确认/等级/费用 */
                        dialogText = dialogTextStorage;
                        optionNames[0] =
                            "\xc8\xb7\xc8\xcf\xd1\xa7\xcf\xb0"; /* 确认学习 */
                        optionDescriptions[0] =
                            "\xc8\xb7\xc8\xcf\xd1\xa7\xcf\xb0\xb8\xc3\xbc\xbc\xc4\xdc"; /* 确认学习该技能 */
                        optionValues[0] =
                            VM_NET_MOCK_NPC_SERVICE_CONFIRM_TRANSACTION;
                        optionNames[1] =
                            "\xb7\xb5\xbb\xd8\xbc\xbc\xc4\xdc\xc1\xd0\xb1\xed"; /* 返回技能列表 */
                        optionDescriptions[1] =
                            "\xd4\xdd\xb2\xbb\xd1\xa7\xcf\xb0\xb8\xc3\xbc\xbc\xc4\xdc"; /* 暂不学习该技能 */
                        optionValues[1] =
                            VM_NET_MOCK_NPC_SERVICE_CANCEL_TRANSACTION;
                        optionCount = 2;
                        skillPrompt = true;
                    }
                }
                else if (transaction.kind !=
                             VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN ||
                         transaction.itemId != skill->skillId ||
                         transaction.quotedPrice != skill->learnPrice)
                {
                    dialogText =
                        "\xbc\xbc\xc4\xdc\xd7\xb4\xcc\xac\xd2\xd1\xb1\xe4\xbb\xaf\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xd1\xa1\xd4\xf1\xa1\xa3"; /* 技能状态已变化，请重新选择。 */
                }
                else
                {
                    vm_net_mock_role_state before = *role;

                    if (!vm_net_mock_role_service_add_skill(role,
                                                             skill->skillId))
                    {
                        dialogText =
                            "\xbc\xbc\xc4\xdc\xb2\xd9\xd7\xf7\xca\xa7\xb0\xdc\xa3\xac\xc7\xeb\xc9\xd4\xba\xf3\xd6\xd8\xca\xd4\xa3\xa1"; /* 技能操作失败，请稍后重试！ */
                    }
                    else
                    {
                        role->money -= skill->learnPrice;
                        if (!vm_net_mock_role_db_save("npc-skill-learn"))
                        {
                            *role = before;
                            if (!vm_net_mock_role_service_remove_skill(
                                    role, skill->skillId))
                            {
                                printf("[error][network] mock_role_skill_learn_rollback role=%u skill=%u\n",
                                       role->roleId, skill->skillId);
                            }
                            dialogText =
                                "\xbc\xbc\xc4\xdc\xb2\xd9\xd7\xf7\xca\xa7\xb0\xdc\xa3\xac\xc7\xeb\xc9\xd4\xba\xf3\xd6\xd8\xca\xd4\xa3\xa1"; /* 技能操作失败，请稍后重试！ */
                        }
                        else
                        {
                            snprintf(dialogTextStorage,
                                     sizeof(dialogTextStorage), "%s%u%s",
                                     "\xbc\xbc\xc4\xdc\xd1\xa7\xcf\xb0\xb3\xc9\xb9\xa6\xa3\xac\xcf\xfb\xba\xc4",
                                     skill->learnPrice,
                                     "\xcd\xad\xc7\xae\xa1\xa3"); /* 技能学习成功，消耗...铜钱。 */
                            dialogText = dialogTextStorage;
                            appendSkills = true;
                            result = 1;
                            serviceState =
                                vm_net_mock_role_service_state_get(role);
                        }
                    }
                }
            }
            else if (operation == VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE)
            {
                const vm_net_mock_skill_catalog_item *skill =
                    vm_net_mock_find_skill_catalog_item(value);
                page = vm_net_mock_npc_skill_list_item_page(
                    role, serviceState, true, value);
                restoredListPage = page;
                if (skill == NULL || skill->rawJob != rawJob ||
                    !vm_net_mock_role_service_has_skill(serviceState,
                                                        value))
                {
                    dialogText =
                        "\xb8\xc3\xbc\xbc\xc4\xdc\xce\xb4\xd1\xa7\xbb\xe1\xbb\xf2\xd2\xd1\xd2\xc5\xcd\xfc\xa1\xa3"; /* 该技能未学会或已遗忘。 */
                }
                else if (vm_net_mock_npc_skill_is_starter(skill))
                {
                    dialogText =
                        "\xb3\xf5\xca\xbc\xbc\xbc\xc4\xdc\xb2\xbb\xc4\xdc\xd2\xc5\xcd\xfc\xa1\xa3"; /* 初始技能不能遗忘。 */
                }
                else if (role->money < skill->learnPrice)
                {
                    snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                             "%s%u%s",
                             "\xcd\xad\xc7\xae\xb2\xbb\xd7\xe3\xa3\xac\xd0\xe8\xd2\xaa",
                             skill->learnPrice,
                             "\xcd\xad\xc7\xae\xa1\xa3"); /* 铜钱不足，需要...铜钱。 */
                    dialogText = dialogTextStorage;
                }
                else if (!transactionConfirm)
                {
                    if (!vm_net_mock_npc_transaction_context_begin(
                            session, role, shopContext,
                            VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_FORGET,
                            skill->skillId, 0, 0, page, skill->learnPrice))
                    {
                        dialogText =
                            "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
                    }
                    else
                    {
                        snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                                 "%s%s\n%s%u\n%s%u%s",
                                 "\xd2\xc5\xcd\xfc\xc8\xb7\xc8\xcf\xa3\xba",
                                 skill->name,
                                 "\xb5\xc8\xbc\xb6\xa3\xba\x4c\x76\x2e",
                                 skill->levelRequired,
                                 "\xb7\xd1\xd3\xc3\xa3\xba", skill->learnPrice,
                                 "\xcd\xad\xc7\xae"); /* 遗忘确认/等级/费用 */
                        dialogText = dialogTextStorage;
                        optionNames[0] =
                            "\xc8\xb7\xc8\xcf\xd2\xc5\xcd\xfc"; /* 确认遗忘 */
                        optionDescriptions[0] =
                            "\xc8\xb7\xc8\xcf\xd2\xc5\xcd\xfc\xb8\xc3\xbc\xbc\xc4\xdc"; /* 确认遗忘该技能 */
                        optionValues[0] =
                            VM_NET_MOCK_NPC_SERVICE_CONFIRM_TRANSACTION;
                        optionNames[1] =
                            "\xb7\xb5\xbb\xd8\xbc\xbc\xc4\xdc\xc1\xd0\xb1\xed"; /* 返回技能列表 */
                        optionDescriptions[1] =
                            "\xd4\xdd\xb2\xbb\xd2\xc5\xcd\xfc\xb8\xc3\xbc\xbc\xc4\xdc"; /* 暂不遗忘该技能 */
                        optionValues[1] =
                            VM_NET_MOCK_NPC_SERVICE_CANCEL_TRANSACTION;
                        optionCount = 2;
                        skillPrompt = true;
                    }
                }
                else if (transaction.kind !=
                             VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_FORGET ||
                         transaction.itemId != skill->skillId ||
                         transaction.quotedPrice != skill->learnPrice)
                {
                    dialogText =
                        "\xbc\xbc\xc4\xdc\xd7\xb4\xcc\xac\xd2\xd1\xb1\xe4\xbb\xaf\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xd1\xa1\xd4\xf1\xa1\xa3"; /* 技能状态已变化，请重新选择。 */
                }
                else
                {
                    vm_net_mock_role_state before = *role;

                    if (!vm_net_mock_role_service_remove_skill(role,
                                                                skill->skillId))
                    {
                        dialogText =
                            "\xbc\xbc\xc4\xdc\xb2\xd9\xd7\xf7\xca\xa7\xb0\xdc\xa3\xac\xc7\xeb\xc9\xd4\xba\xf3\xd6\xd8\xca\xd4\xa3\xa1"; /* 技能操作失败，请稍后重试！ */
                    }
                    else
                    {
                        role->money -= skill->learnPrice;
                        if (!vm_net_mock_role_db_save("npc-skill-forget"))
                        {
                            *role = before;
                            if (!vm_net_mock_role_service_add_skill(
                                    role, skill->skillId))
                            {
                                printf("[error][network] mock_role_skill_forget_rollback role=%u skill=%u\n",
                                       role->roleId, skill->skillId);
                            }
                            dialogText =
                                "\xbc\xbc\xc4\xdc\xb2\xd9\xd7\xf7\xca\xa7\xb0\xdc\xa3\xac\xc7\xeb\xc9\xd4\xba\xf3\xd6\xd8\xca\xd4\xa3\xa1"; /* 技能操作失败，请稍后重试！ */
                        }
                        else
                        {
                            snprintf(dialogTextStorage,
                                     sizeof(dialogTextStorage), "%s%s%s%u%s",
                                     "\xd2\xd1\xd2\xc5\xcd\xfc\xbc\xbc\xc4\xdc\xa3\xba",
                                     skill->name,
                                     "\xa3\xac\xcf\xfb\xba\xc4",
                                     skill->learnPrice,
                                     "\xcd\xad\xc7\xae\xa1\xa3"); /* 已遗忘技能：...，消耗...铜钱。 */
                            dialogText = dialogTextStorage;
                            appendSkills = true;
                            result = 1;
                            serviceState =
                                vm_net_mock_role_service_state_get(role);
                        }
                    }
                }
            }

            if (skillPrompt)
                goto npc_service_serialize;

            for (u32 i = 0; i < vm_net_mock_load_skill_catalog(); ++i)
            {
                const vm_net_mock_skill_catalog_item *skill =
                    &g_vm_net_mock_skill_catalog[i];
                if (skill->rawJob != rawJob)
                    continue;
                if (vm_net_mock_role_service_has_skill(serviceState,
                                                       skill->skillId))
                {
                    ++skillLearnedCount;
                }
                else if (skill->levelRequired > role->level)
                {
                    ++skillLevelLockedCount;
                    if (skillNextLocked == NULL)
                        skillNextLocked = skill;
                }
                else
                {
                    ++skillEligibleCount;
                }
            }

            total = vm_net_mock_npc_skill_list_total(role, serviceState,
                                                     forgetList);
            page = vm_net_mock_npc_skill_list_clamp_page(total, page);
            start = page * VM_NET_MOCK_NPC_SERVICE_SKILL_PAGE_ITEMS;
            restoredListPage = page;
            if (!skillMutationRequest)
            {
                snprintf(dialogTextStorage, sizeof(dialogTextStorage), "%s%u%s",
                         forgetList
                             ? "\xc7\xeb\xd1\xa1\xd4\xf1\xd2\xaa\xd2\xc5\xcd\xfc\xb5\xc4\xbc\xbc\xc4\xdc\xa3\xa8\xb5\xb1\xc7\xb0"
                             : "\xc7\xeb\xd1\xa1\xd4\xf1\xd2\xaa\xd1\xa7\xcf\xb0\xb5\xc4\xbc\xbc\xc4\xdc\xa3\xa8\xb5\xb1\xc7\xb0", /* 请选择要遗忘/学习的技能（当前 */
                         role->level,
                         "\xbc\xb6\xa3\xa9\xa3\xba"); /* 级）： */
                dialogText = dialogTextStorage;
            }

            ordinal = 0;
            for (u32 i = 0; i < vm_net_mock_load_skill_catalog(); ++i)
            {
                const vm_net_mock_skill_catalog_item *skill =
                    &g_vm_net_mock_skill_catalog[i];
                if (!vm_net_mock_npc_skill_list_matches(skill, role,
                                                        serviceState,
                                                        forgetList))
                {
                    continue;
                }
                if (ordinal >= start &&
                    ordinal < start + VM_NET_MOCK_NPC_SERVICE_SKILL_PAGE_ITEMS)
                {
                    snprintf(optionNameStorage[optionCount],
                             sizeof(optionNameStorage[optionCount]), "%s%s",
                             forgetList ? "\xd2\xc5\xcd\xfc" : "\xd1\xa7\xcf\xb0",
                             skill->name); /* 遗忘/学习... */
                    if (forgetList)
                    {
                        snprintf(optionDescriptionStorage[optionCount],
                                 sizeof(optionDescriptionStorage[optionCount]),
                                 "%s Lv.%u %u%s", skill->name,
                                 skill->levelRequired, skill->learnPrice,
                                 "\xcd\xad\xc7\xae");
                    }
                    else
                    {
                        snprintf(optionDescriptionStorage[optionCount],
                                 sizeof(optionDescriptionStorage[optionCount]),
                                 "%s Lv.%u %u%s", skill->name,
                                 skill->levelRequired, skill->learnPrice,
                                 "\xcd\xad\xc7\xae");
                    }
                    optionNames[optionCount] = optionNameStorage[optionCount];
                    optionDescriptions[optionCount] =
                        optionDescriptionStorage[optionCount];
                    optionValues[optionCount] =
                        (forgetList
                             ? VM_NET_MOCK_NPC_SERVICE_FORGET_SKILL_BASE
                             : VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE) |
                        skill->skillId;
                    ++optionCount;
                }
                ++ordinal;
            }
            if (page > 0 &&
                optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
            {
                optionNames[optionCount] =
                    "\xc9\xcf\xd2\xbb\xd2\xb3"; /* 上一页 */
                optionDescriptions[optionCount] =
                    forgetList ? "\xd2\xc5\xcd\xfc\xbc\xbc\xc4\xdc"
                               : "\xd1\xa7\xcf\xb0\xbc\xbc\xc4\xdc";
                optionValues[optionCount] =
                    (forgetList
                         ? VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_FORGET_BASE
                         : VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_LEARN_BASE) |
                    (page - 1u);
                ++optionCount;
            }
            if (start + VM_NET_MOCK_NPC_SERVICE_SKILL_PAGE_ITEMS < total &&
                optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
            {
                optionNames[optionCount] =
                    "\xcf\xc2\xd2\xbb\xd2\xb3"; /* 下一页 */
                optionDescriptions[optionCount] =
                    forgetList ? "\xd2\xc5\xcd\xfc\xbc\xbc\xc4\xdc"
                               : "\xd1\xa7\xcf\xb0\xbc\xbc\xc4\xdc";
                optionValues[optionCount] =
                    (forgetList
                         ? VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_FORGET_BASE
                         : VM_NET_MOCK_NPC_SERVICE_OPEN_SKILL_LEARN_BASE) |
                    (page + 1u);
                ++optionCount;
            }
            if (optionCount == 0 && result == 0 && !skillMutationRequest)
            {
                if (!forgetList && skillNextLocked != NULL)
                {
                    snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                             "%s%u%s%s%s%u%s%u%s",
                             "\xb5\xb1\xc7\xb0", role->level,
                             "\xbc\xb6\xa3\xbb\xcf\xc2\xd2\xbb\xbc\xbc\xc4\xdc",
                             skillNextLocked->name,
                             "\xd0\xe8\xd2\xaa", skillNextLocked->levelRequired,
                             "\xbc\xb6\xa3\xac\xd1\xa7\xcf\xb0\xb7\xd1\xd3\xc3",
                             skillNextLocked->learnPrice,
                             "\xcd\xad\xc7\xae\xa1\xa3"); /* 当前...级；下一技能... */
                    dialogText = dialogTextStorage;
                }
                else
                {
                    dialogText = forgetList
                                     ? "\xb5\xb1\xc7\xb0\xc3\xbb\xd3\xd0\xbf\xc9\xd2\xc5\xcd\xfc\xb5\xc4\xbc\xbc\xc4\xdc\xa1\xa3" /* 当前没有可遗忘的技能。 */
                                     : "\xb5\xb1\xc7\xb0\xc3\xbb\xd3\xd0\xbf\xc9\xd2\xd4\xd1\xa7\xcf\xb0\xb5\xc4\xbc\xbc\xc4\xdc\xa1\xa3"; /* 当前没有可以学习的技能。 */
                }
            }
        }
    }

npc_service_serialize:
    memset(dialog, 0, sizeof(dialog));
    if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 0) ||
        !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                    dialogText) ||
        !vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen,
                                optionCount))
    {
        return 0;
    }
    for (u32 i = 0; i < optionCount; ++i)
    {
        if (!vm_net_mock_append_npc_service_dialog_option(
                dialog, sizeof(dialog), &dialogLen,
                optionNames[i],
                i == instanceChallengeOptionIndex ? 13u : 1u,
                optionValues[i], optionDescriptions[i]))
        {
            return 0;
        }
    }
    if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 0) ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 26, 1,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "hidebtn", 0) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "dialog", dialog,
                                    (u16)dialogLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (crystalSynthesisRefresh)
    {
        vm_net_mock_backpack_item_state *resultItem =
            vm_net_mock_role_find_backpack_item(
                role, crystalSynthesisResultItemId,
                crystalSynthesisResultSeq);
        vm_net_mock_reward15_item_row rewardRow;

        if (crystalSynthesisSourceItemId == 0 ||
            crystalSynthesisSourceSeq == 0 ||
            crystalSynthesisResultItemId == 0 ||
            crystalSynthesisResultSeq == 0 ||
            crystalSynthesisResultTotal == 0 || resultItem == NULL ||
            resultItem->count != crystalSynthesisResultTotal)
        {
            return 0;
        }
        memset(&rewardRow, 0, sizeof(rewardRow));
        rewardRow.item = resultItem;
        rewardRow.acquiredCount = 1;
        if (!vm_net_mock_append_backpack_item_count11_object(
                out, outCap, &pos, &objectCount,
                crystalSynthesisSourceSeq, crystalSynthesisSourceItemId,
                crystalSynthesisSourceRemaining) ||
            !vm_net_mock_append_backpack_reward15_object(
                out, outCap, &pos, &objectCount, &rewardRow, 1) ||
            /* 7/15 creates the client reward row and presents its native
             * notice.  The following sequence-keyed 7/11 is deliberately
             * the authoritative total, so an already visible output stack
             * is refreshed instead of retaining its old count. */
            !vm_net_mock_append_backpack_item_count11_object(
                out, outCap, &pos, &objectCount,
                crystalSynthesisResultSeq, crystalSynthesisResultItemId,
                crystalSynthesisResultTotal))
        {
            return 0;
        }
    }
    if (mailboxClaimRefresh)
    {
        vm_net_mock_reward15_item_row rewardRows[VM_NET_MOCK_REWARD15_MAX_ROWS];

        if (mailboxView.claimedItemCount > VM_NET_MOCK_REWARD15_MAX_ROWS)
            return 0;
        memset(rewardRows, 0, sizeof(rewardRows));
        for (u8 i = 0; i < mailboxView.claimedItemCount; ++i)
        {
            vm_net_mock_mail_claimed_item *claimed =
                &mailboxView.claimedItems[i];
            vm_net_mock_backpack_item_state *item =
                vm_net_mock_role_find_backpack_item(role, claimed->itemId,
                                                    claimed->seq);

            if (item == NULL || claimed->count == 0)
                return 0;
            rewardRows[i].item = item;
            rewardRows[i].acquiredCount = claimed->count;
        }
        if (!vm_net_mock_append_backpack_reward15_object(
                out, outCap, &pos, &objectCount, rewardRows,
                mailboxView.claimedItemCount))
        {
            return 0;
        }
    }
    if (appendSkills)
    {
        if (!vm_net_mock_append_role_skills_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    /* The 26/1 dialog completes action=1, while the scene HUD owns copper
     * through 10/26.  Do not append item-manager or HUD objects here: they
     * belong to different callbacks and can make the dialog response fail to
     * unpack. */
    if (result == 1 &&
        (strcmp(action, "shop-buy") == 0 ||
         strcmp(action, "weapon-buy") == 0 ||
         strcmp(action, "skill-learn") == 0 ||
         strcmp(action, "skill-forget") == 0))
    {
        if (!vm_net_mock_append_type1_object(out, outCap, &pos, 0))
            return 0;
        ++objectCount;
    }
    if (instanceChallengeOptionIndex != 0xff && instanceSeed != NULL &&
        session != NULL)
    {
        const char *scene = vm_net_mock_current_scene_name();
        session->instanceChallengeDirectPending = true;
        /* A guide with a destination scene keeps its isolated-instance
         * contract.  Only the no-destination option targets a monster that
         * must already be live in this visible scene. */
        session->instanceChallengeDirectSceneMonster =
            instanceSeed->instanceScene[0] == 0;
        session->instanceChallengePending = false;
        session->instanceChallengeBattlePending = false;
        session->instanceChallengeActorId = instanceSeed->actorId;
        session->instanceChallengeEnemyId = instanceSeed->challengeEnemyId;
        session->instanceChallengeSceneIndex = 0;
        session->instanceChallengeX = instanceSeed->instanceX != 0
                                          ? instanceSeed->instanceX
                                          : instanceSeed->x;
        session->instanceChallengeY = instanceSeed->instanceY != 0
                                          ? instanceSeed->instanceY
                                          : instanceSeed->y;
        session->instanceChallengeTick = g_schedulerTick;
        snprintf(session->instanceChallengeScene,
                 sizeof(session->instanceChallengeScene), "%s",
                 vm_net_mock_scene_name_is_safe(scene) ? scene : "");
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_npc_service action=%s opcode=%08x value=%u page=%u role=%u job=%u level=%u result=%u options=%u money=%u skill_eligible=%u skill_learned=%u skill_level_locked=%u next_skill=%u next_level=%u next_price=%u objects=%u resp=%u inventory_sync=%s evidence=JianghuOL.CBE:0x010492B0(action1)+0x010380E8+skill.dsh\n",
           action, serviceValue, value, restoredListPage, role->roleId,
           role->job, role->level,
           result, optionCount, role->money, skillEligibleCount,
           skillLearnedCount, skillLevelLockedCount,
           skillNextLocked ? skillNextLocked->skillId : 0,
           skillNextLocked ? skillNextLocked->levelRequired : 0,
           skillNextLocked ? skillNextLocked->learnPrice : 0,
           objectCount, pos,
           result == 1 &&
                   strcmp(action, "equipment-sell-quality-zero") == 0
               ? "success-dialog-only:26/1(return);backpack-on-native-query"
               : crystalSynthesisRefresh
               ? "dialog+source-count+reward+result-count:26/1+7/11+7/15+7/11"
               : result == 1 && (strcmp(action, "shop-buy") == 0 ||
                                  strcmp(action, "weapon-buy") == 0)
               ? "dialog+wallet:26/1+10/26;backpack-on-native-query"
               : mailboxClaimRefresh
                     ? "dialog+reward:26/1+7/15;native-item-manager-delta"
               : "not-applicable");
    return pos;
}

static bool vm_net_mock_append_info_banner_result5_object(u8 *out, u32 outCap,
                                                          u32 *pos);
static bool vm_net_mock_append_info_banner_text11_object(u8 *out, u32 outCap,
                                                         u32 *pos,
                                                         const char *info);
/* Defined by the scene-sync poll module after this handler.  A successful
 * 6/11 accept only confirms the dialog operation; case 11 does not rebuild
 * the client's active-task table or refresh scene-node prompt types. */
static bool vm_net_mock_append_taskinfo_empty1_object(u8 *out, u32 outCap,
                                                       u32 *pos,
                                                       const char *sceneOverride);
static bool vm_net_mock_append_taskaction14_object(u8 *out, u32 outCap,
                                                    u32 *pos,
                                                    const char *sceneOverride);
static u32 vm_net_mock_build_single_object_request(
    const vm_net_mock_request_object *object, u8 *out, u32 outCap);
static bool vm_net_mock_append_response_objects(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
    const u8 *response, u32 responseLen);

static u32 vm_net_mock_build_task_response(const u8 *request, u32 requestLen,
                                           u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_request_object trailingObject;
    vm_net_mock_role_state *activeRole = NULL;
    vm_net_mock_task_state_row taskState;
    const u8 *taskBlob = NULL;
    u16 taskBlobLen = 0;
    u32 taskId = 0;
    u8 requestState = 0;
    u8 reportedProgress1 = 0;
    u8 reportedProgress2 = 0;
    u8 taskInfo[512];
    u32 taskInfoLen = 0;
    /* Case 4 reads a bounded multi-row awardinfo sequence.  Each row can be
     * an equipment record carrying all enhancement-stage attributes. */
    u8 awardInfo[VM_NET_MOCK_TASK_AWARDINFO_MAX_BYTES];
    u32 awardInfoLen = 0;
    u16 committedRewardSeqs[VM_NET_MOCK_TASK_REWARD_ITEM_MAX];
    u8 committedRewardCount = 0;
    u8 consumedItemInfo[VM_NET_MOCK_TASK_SUBMIT_ITEMINFO_MAX_BYTES];
    u32 consumedItemInfoLen = 0;
    u16 committedConsumedSeqs[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 committedConsumedRemainings[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 committedConsumedCount = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 result = 1;
    u8 responseSubtype = 0;
    u8 responseObjectCount = 1;
    u32 vitality = 0;
    u32 vitalityMax = 0;
    bool hasInfoBannerPrefix = false;
    bool hasInfoBannerTail = false;
    bool hasProgressStateTail = false;
    bool taskAcceptBlockedByBackpack = false;
    bool taskAcceptOfferStillAvailable = false;
    bool taskAcceptNeedsCandidateRefresh = false;
    bool taskAcceptNeedsScenePromptRefresh = false;
    bool taskAcceptNeedsBackpackRefresh = false;
    bool taskAcceptOfferContext = false;
    vm_net_mock_role_state taskAcceptBackpackBefore;
    const vm_net_mock_task_definition *taskDefinition = NULL;
    char detailText[256];
    char destinationText[128];
    char promptReceiver[32];
    char taskAcceptFailureInfo[128];
    const char *taskAcceptFailureCode = NULL;
    const char *action = NULL;
    const char *evidence = "JianghuOL.CBE:0x0104726C";

    if (request == NULL || requestLen < 9 || out == NULL || outCap < pos ||
        request[0] != 'W' || request[1] != 'T' || request[4] != 1 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        return 0;
    }
    /* The real completion path flushes its progress-banner request before the
     * task request.  Runtime has shown exactly two prefix forms:
     *   `25/5(empty) + 6/4{taskid}` commits the completed task;
     *   `25/5(empty) + 6/10{taskid,state,agree}` refreshes its detail text.
     * Keep this exception narrow; other task operations either contain one
     * object or, for 6/11 accept, carry the empty 25/5 object after the task
     * object. */
    if (object.major == 1 && object.kind == 0x19 &&
        object.subtype == 5 && object.payloadLen == 0)
    {
        if (!vm_net_mock_next_request_object(request, requestLen, &offset,
                                             &object) ||
            object.major != 1 || object.kind != 6 ||
            (object.subtype != 4 && object.subtype != 10) ||
            offset != requestLen)
        {
            return 0;
        }
        hasInfoBannerPrefix = true;
    }
    if (object.major != 1 || object.kind != 6 ||
        (object.subtype != 3 && object.subtype != 4 && object.subtype != 6 && object.subtype != 7 &&
         object.subtype != 10 && object.subtype != 11 &&
         object.subtype != 12))
    {
        return 0;
    }
    if (offset != requestLen)
    {
        if (object.subtype == 11 &&
            vm_net_mock_next_request_object(request, requestLen, &offset,
                                            &trailingObject) &&
            trailingObject.major == 1 && trailingObject.kind == 0x19 &&
            trailingObject.subtype == 5 && trailingObject.payloadLen == 0 &&
            offset == requestLen)
        {
            hasInfoBannerTail = true;
        }
        else if (object.subtype == 3)
        {
            const u8 *progressBlob = NULL;
            const u8 *stateTaskBlob = NULL;
            u16 progressBlobLen = 0;
            u16 stateTaskBlobLen = 0;
            u32 progressTaskId = 0;
            u32 stateTaskId = 0;
            u8 ignoredProgress1 = 0;
            u8 ignoredProgress2 = 0;
            u8 stateTaskNum = 0;

            /* When UpdateTaskProgress reaches the requirement, it immediately
             * calls SendTaskStateUpdate. The outgoing event queue can flush the
             * two requests as one WT packet in this exact order. Validate both
             * task ids before applying either side effect. */
            if (!vm_net_mock_next_request_object(request, requestLen, &offset,
                                                 &trailingObject) ||
                trailingObject.major != 1 || trailingObject.kind != 6 ||
                trailingObject.subtype != 6 || trailingObject.payloadLen != 28 ||
                offset != requestLen || object.payloadLen != 23 ||
                !vm_net_mock_get_object_entry_bytes(object.payload,
                                                    object.payloadLen,
                                                    "taskinfo", &progressBlob,
                                                    &progressBlobLen) ||
                !vm_net_mock_task_read_progress_blob(progressBlob,
                                                     progressBlobLen,
                                                     &progressTaskId,
                                                     &ignoredProgress1,
                                                     &ignoredProgress2) ||
                !vm_net_mock_get_object_u8_field(trailingObject.payload,
                                                 trailingObject.payloadLen,
                                                 "tasknum", &stateTaskNum) ||
                stateTaskNum != 1 ||
                !vm_net_mock_get_object_entry_bytes(trailingObject.payload,
                                                    trailingObject.payloadLen,
                                                    "taskid", &stateTaskBlob,
                                                    &stateTaskBlobLen) ||
                !vm_net_mock_task_read_tagged_u32(stateTaskBlob,
                                                  stateTaskBlobLen,
                                                  &stateTaskId) ||
                progressTaskId == 0 || progressTaskId != stateTaskId)
            {
                return 0;
            }
            hasProgressStateTail = true;
        }
        else
        {
            return 0;
        }
    }

    memset(&taskState, 0, sizeof(taskState));
    memset(taskInfo, 0, sizeof(taskInfo));
    memset(awardInfo, 0, sizeof(awardInfo));
    memset(committedRewardSeqs, 0, sizeof(committedRewardSeqs));
    memset(consumedItemInfo, 0, sizeof(consumedItemInfo));
    memset(committedConsumedSeqs, 0, sizeof(committedConsumedSeqs));
    memset(committedConsumedRemainings, 0,
           sizeof(committedConsumedRemainings));
    memset(detailText, 0, sizeof(detailText));
    memset(destinationText, 0, sizeof(destinationText));
    memset(promptReceiver, 0, sizeof(promptReceiver));
    memset(taskAcceptFailureInfo, 0, sizeof(taskAcceptFailureInfo));
    memset(&taskAcceptBackpackBefore, 0, sizeof(taskAcceptBackpackBefore));

    if (hasProgressStateTail)
    {
        u8 progressRequest[128];
        u8 stateRequest[128];
        u8 progressResponse[512];
        u8 stateResponse[512];
        u32 progressRequestLen = 0;
        u32 stateRequestLen = 0;
        u32 progressResponseLen = 0;
        u32 stateResponseLen = 0;

        progressRequestLen = vm_net_mock_build_single_object_request(
            &object, progressRequest, sizeof(progressRequest));
        stateRequestLen = vm_net_mock_build_single_object_request(
            &trailingObject, stateRequest, sizeof(stateRequest));
        if (progressRequestLen == 0 || stateRequestLen == 0)
            return 0;
        progressResponseLen = vm_net_mock_build_task_response(
            progressRequest, progressRequestLen,
            progressResponse, sizeof(progressResponse));
        if (progressResponseLen == 0)
            return 0;
        stateResponseLen = vm_net_mock_build_task_response(
            stateRequest, stateRequestLen,
            stateResponse, sizeof(stateResponse));
        if (stateResponseLen == 0)
            return 0;

        pos = 5;
        responseObjectCount = 0;
        if (!vm_net_mock_append_response_objects(out, outCap, &pos,
                                                 &responseObjectCount,
                                                 progressResponse,
                                                 progressResponseLen) ||
            !vm_net_mock_append_response_objects(out, outCap, &pos,
                                                 &responseObjectCount,
                                                 stateResponse,
                                                 stateResponseLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
        printf("[info][network] mock_task_progress_state_combo request=6/3+6/6 response=6/2+6/6 objects=%u resp=%u evidence=JianghuOL.CBE:0x01047ACE->0x01046E64+0x0104726C(cases2,6)\n",
               responseObjectCount, pos);
        vm_autotest_note("mock_task_progress_state_combo request=6/3+6/6 response=6/2+6/6 objects=%u resp=%u evidence=runtime:len65\n",
                         responseObjectCount, pos);
        return pos;
    }

    /* Preserve the request ordering.  result=4 is the normal 25/5 completion
     * consumed by net_handle_info_banner_state.  The following task object is
     * the matching 6/4 submit result or 6/10 detail response. */
    if (hasInfoBannerPrefix)
    {
        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, &pos))
            return 0;
        responseObjectCount += 1;
    }

    if (object.subtype == 3)
    {
        u8 authoritativeProgress1 = 0;
        u8 authoritativeProgress2 = 0;
        u8 nextState = 1;
        bool stored = false;

        if (!vm_net_mock_get_object_entry_bytes(object.payload, object.payloadLen,
                                                "taskinfo", &taskBlob,
                                                &taskBlobLen) ||
            !vm_net_mock_task_read_progress_blob(taskBlob, taskBlobLen,
                                                 &taskId,
                                                 &reportedProgress1,
                                                 &reportedProgress2))
        {
            return 0;
        }
        activeRole = vm_net_mock_active_role();
        taskDefinition = vm_net_mock_task_catalog_find_by_id(taskId);
        if (activeRole != NULL && taskDefinition != NULL &&
            vm_net_mock_task_state_load(activeRole->roleId, taskId, &taskState) &&
            taskState.found && (taskState.state == 1 || taskState.state == 2))
        {
            authoritativeProgress1 = (u8)vm_net_mock_min_u32(
                reportedProgress1, taskDefinition->requirementCount1);
            authoritativeProgress2 = (u8)vm_net_mock_min_u32(
                reportedProgress2, taskDefinition->requirementCount2);
            if (authoritativeProgress1 < taskState.progress1)
                authoritativeProgress1 = taskState.progress1;
            if (authoritativeProgress2 < taskState.progress2)
                authoritativeProgress2 = taskState.progress2;
            nextState = taskState.state;
            if (nextState == 1 &&
                authoritativeProgress1 >= taskDefinition->requirementCount1 &&
                authoritativeProgress2 >= taskDefinition->requirementCount2)
            {
                nextState = 2;
            }
            stored = taskState.state == 2 ||
                     vm_net_mock_task_progress_store(activeRole->roleId, taskId,
                                                     authoritativeProgress1,
                                                     authoritativeProgress2,
                                                     nextState);
        }
        result = stored ? 0 : 1;
        responseSubtype = 2;
        /* The task response dispatcher has no case 3.  Its case 2 is the
         * progress-upload acknowledgement and consumes only "result". */
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 6,
                                         responseSubtype, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        {
            return 0;
        }
        action = "progress";
        evidence = "JianghuOL.CBE:0x01047ACE+0x01047BEC+0x0104726C(case2)";
        printf("[info][network] mock_task_progress_report task=%u role=%u reported=%u/%u authoritative=%u/%u state=%u result=%u\n",
               taskId, activeRole ? activeRole->roleId : 0,
               reportedProgress1, reportedProgress2,
               authoritativeProgress1, authoritativeProgress2,
               nextState, result);
    }
    else if (object.subtype == 4)
    {
        static const char submitSuccessText[] =
            "\xc8\xce\xce\xf1\xcc\xe1\xbd\xbb\xb3\xc9\xb9\xa6\xa3\xa1"; /* 任务提交成功！ */
        bool committed = false;
        bool submitContextValid = false;

        if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "taskid", &taskId))
        {
            return 0;
        }
        taskDefinition = vm_net_mock_task_catalog_find_by_id(taskId);
        if (taskId != VM_NET_MOCK_TEST_TASK_ID && taskDefinition == NULL)
            return 0;
        activeRole = vm_net_mock_active_role();
        if (activeRole == NULL)
            return 0;
        /* 6/4 case 4 writes energy directly into both client role caches.
         * Take the authoritative snapshot before committing task rewards so a
         * database read failure reports a normal task failure rather than
         * committing an award and then leaving the client request unanswered. */
        if (!vm_net_mock_vitality_snapshot(activeRole, &vitality,
                                            &vitalityMax))
        {
            printf("[error][mock-service] task_vitality_snapshot_failed task=%u role=%u error=%s\n",
                   taskId, activeRole->roleId, vm_mysql_last_error());
        }
        /* Case 4 carries only a task id. For catalog tasks consume the
         * preceding dialog's receiver-bound submit authorization before a
         * state-2 row may award anything. The synthetic test task predates
         * that catalogue contract and retains its existing isolated path. */
        /* A failed snapshot is an infrastructure failure, not an attempted
         * submission.  Do not consume the one-shot dialog authorization in
         * that case: the client may retry only after the server can build the
         * complete 6/4 success contract. */
        if (vitalityMax != 0)
        {
            submitContextValid = taskDefinition == NULL ||
                                 vm_net_mock_task_submit_context_consume(
                                     taskId, NULL);
        }
        if (vitalityMax != 0 && submitContextValid &&
            vm_net_mock_task_state_load(activeRole->roleId, taskId, &taskState) &&
            taskState.found && taskState.state == 2 &&
            ((taskDefinition != NULL &&
              vm_net_mock_task_commit_reward(activeRole, taskDefinition,
                                             committedRewardSeqs,
                                             &committedRewardCount,
                                             committedConsumedSeqs,
                                             committedConsumedRemainings,
                                             &committedConsumedCount)) ||
             (taskDefinition == NULL &&
              vm_net_mock_task_state_store(activeRole->roleId, taskId, 3))))
        {
            committed = true;
        }
        if (!committed && taskDefinition != NULL && vitalityMax != 0 &&
            !submitContextValid)
        {
            printf("[info][network] mock_task_submit_rejected task=%u role=%u "
                   "reason=delivery-context scene=%s\n",
                   taskId, activeRole->roleId,
                   vm_net_mock_current_scene_name());
        }
        /* net_handle_task_response_dispatch(0x0104726C) case 4 is the normal
         * submit result and uses result=1 for success.  Subtype 16 belongs to
         * HandleTaskCompleteResult(0x01038E6E), whose success text is the
         * hard-coded “重置成功!”, so it must never be used for submission. */
        result = committed ? 1 : 0;
        responseSubtype = 4;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 6,
                                         responseSubtype,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        {
            return 0;
        }
        if (result == 1)
        {
            u32 totalExp = activeRole->exp;
            if (!vm_net_mock_build_task_submit_iteminfo(
                    consumedItemInfo, sizeof(consumedItemInfo),
                    &consumedItemInfoLen, committedConsumedSeqs,
                    committedConsumedRemainings, committedConsumedCount) ||
                !vm_net_mock_build_task_awardinfo(
                    awardInfo, sizeof(awardInfo), &awardInfoLen, activeRole,
                    taskDefinition, committedRewardSeqs,
                    committedRewardCount) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "energy", vitality) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "energymax", vitalityMax) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "exp", totalExp) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "level", activeRole->level) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "lastexp",
                                            vm_net_mock_role_last_level_exp(totalExp)) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "curexp",
                                            vm_net_mock_role_next_level_start_exp(totalExp)) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "persentexp",
                                            vm_net_mock_role_exp_percent(totalExp)) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "seqnum",
                                           committedConsumedCount) ||
                !vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo",
                                            consumedItemInfo,
                                            (u16)consumedItemInfoLen) ||
                !vm_net_mock_put_object_raw(out, outCap, &pos, "awardinfo",
                                            awardInfo, (u16)awardInfoLen) ||
                /* case 4 reads taskdes through the WT string accessor
                 * (a2+64/a2+84), unlike iteminfo/awardinfo which are raw
                 * stream blobs.  The inner BE length is therefore part of
                 * this field's contract; a raw GBK payload makes its first
                 * two glyph bytes look like a huge length and overruns the
                 * client's fixed submit-message buffer. */
                !vm_net_mock_put_object_string(out, outCap, &pos, "taskdes",
                                               submitSuccessText))
            {
                return 0;
            }
            printf("[info][network] mock_task_submit_iteminfo task=%u role=%u seqnum=%u iteminfo_len=%u evidence=JianghuOL.CBE:0x010473D0-0x01047430\n",
                   taskId, activeRole->roleId, committedConsumedCount,
                   consumedItemInfoLen);
        }
        action = "commit";
        evidence = "JianghuOL.CBE:0x01047CFC+0x0104726C(case4)+0x01046EDA";
    }
    else if (object.subtype == 7)
    {
        if (!vm_net_mock_get_object_number_field(object.payload,
                                                 object.payloadLen,
                                                 "taskid", &taskId))
        {
            return 0;
        }
        taskDefinition = vm_net_mock_task_catalog_find_by_id(taskId);
        if (taskId != VM_NET_MOCK_TEST_TASK_ID && taskDefinition == NULL)
            return 0;
        activeRole = vm_net_mock_active_role();
        if (activeRole == NULL)
            return 0;
        if (vm_net_mock_task_state_load(activeRole->roleId, taskId, &taskState) &&
            taskState.found &&
            (taskState.state == 1 || taskState.state == 2) &&
            vm_net_mock_task_delete(activeRole->roleId, taskId))
        {
            result = 0;
            if (taskDefinition != NULL && taskDefinition->givenItemId != 0 &&
                taskDefinition->givenItemCount != 0 &&
                vm_net_mock_role_find_backpack_item(activeRole,
                                                    taskDefinition->givenItemId,
                                                    0) != NULL)
            {
                (void)vm_net_mock_role_consume_backpack_item(
                    activeRole, taskDefinition->givenItemId, 0,
                    taskDefinition->givenItemCount, NULL);
                vm_net_mock_role_db_save("task-abandon");
            }
        }
        responseSubtype = 7;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 6,
                                         responseSubtype, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        {
            return 0;
        }
        action = "abandon";
        evidence = "JianghuOL.CBE:0x01047DAC+0x0104778C+0x0104726C(case7)";
    }
    else if (object.subtype == 10)
    {
        if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "taskid", &taskId))
        {
            return 0;
        }
        taskDefinition = vm_net_mock_task_catalog_find_by_id(taskId);
        if (taskId != VM_NET_MOCK_TEST_TASK_ID && taskDefinition == NULL)
            return 0;
        if (taskDefinition != NULL)
        {
            snprintf(detailText, sizeof(detailText), "%s\n%s",
                     taskDefinition->goal,
                     taskDefinition->rewardText);
        }
        else
        {
            snprintf(detailText, sizeof(detailText), "%s",
                     "\xd5\xe2\xca\xc7\xd2\xbb\xcf\xee\xc8\xce\xce\xf1\xcf\xb5\xcd\xb3\xb2\xe2\xca\xd4"
                     "\xc8\xce\xce\xf1\xa1\xa3\xc8\xb7\xc8\xcf\xba\xf3\xbd\xab\xbc\xd3\xc8\xeb\xc8\xce"
                     "\xce\xf1\xc1\xd0\xb1\xed\xa1\xa3"); /* 这是一项任务系统测试任务。确认后将加入任务列表。 */
        }
        (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                              "state", &requestState);
        taskInfoLen = (u32)strlen(detailText);
        responseSubtype = 10;
        /* ReqTaskInfo(0x01038D2C) forwards the field to SendTaskHallReq
         * (0x01038CB2), which uses the response object's string accessor at
         * +0x40 and copies the returned text directly.  This is not a tagged
         * stream like 6/1 taskinfo or 26/1 dialog. */
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 6,
                                         responseSubtype, &objectStart) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "info", detailText))
        {
            return 0;
        }
        result = 0;
        action = "detail";
        evidence = "JianghuOL.CBE:0x010491FA+0x01038CB2+0x0104726C(case10)";
    }
    else if (object.subtype == 12)
    {
        if (!vm_net_mock_get_object_number_field(object.payload,
                                                 object.payloadLen,
                                                 "id", &taskId))
        {
            return 0;
        }
        taskDefinition = vm_net_mock_task_catalog_find_by_id(taskId);
        if (taskId != VM_NET_MOCK_TEST_TASK_ID && taskDefinition == NULL)
            return 0;
        if (taskDefinition != NULL)
            snprintf(destinationText, sizeof(destinationText), "%s", taskDefinition->goal);
        else
            snprintf(destinationText, sizeof(destinationText), "%s",
                     "\xc8\xce\xce\xf1\xc4\xbf\xb1\xea\xa3\xba\xd3\xeb\xc5\xee\xc0\xb3\xa1\xaa\xd6\xfd\xbd\xa3\xb9\xc8\xb5\xc4"
                     "\xc8\xce\xce\xf1\xca\xb9\xd5\xdf\xbd\xbb\xcc\xb8\xa1\xa3"); /* 任务目标：与蓬莱-铸剑谷的任务使者交谈。 */
        /* task_handle_destinfo_response(0x01047F0A) only consumes the
         * response string field "text".  The client may issue this read-only
         * request for a task row that has just become completed (state 3) but
         * has not yet been removed from the current screen.  Gating the reply
         * on the persisted active state leaves that screen's wait overlay
         * running forever, so answer every known catalog task here. */
        activeRole = vm_net_mock_active_role();
        taskInfoLen = (u32)strlen(destinationText);
        responseSubtype = 12;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 6,
                                         responseSubtype, &objectStart) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "text",
                                           destinationText))
        {
            return 0;
        }
        result = 0;
        action = "destination";
        evidence = "JianghuOL.CBE:0x01047E0C+0x01047F0A+0x0104726C(case12)";
    }
    else
    {
        const char *fieldName = object.subtype == 11 ? "taskinfo" : "taskid";
        bool taskIdParsed =
            vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                             fieldName, &taskId);
        if (!taskIdParsed &&
            vm_net_mock_get_object_blob_field(object.payload, object.payloadLen,
                                              fieldName, &taskBlob, &taskBlobLen))
        {
            taskIdParsed = vm_net_mock_task_read_tagged_u32(taskBlob,
                                                            taskBlobLen,
                                                            &taskId);
        }
        if (!taskIdParsed)
        {
            return 0;
        }
        taskDefinition = vm_net_mock_task_catalog_find_by_id(taskId);
        if (taskId != VM_NET_MOCK_TEST_TASK_ID && taskDefinition == NULL)
            return 0;
        activeRole = vm_net_mock_active_role();
        if (activeRole == NULL)
            return 0;

        if (object.subtype == 11)
        {
            vm_net_mock_task_state_list_row allStates[VM_NET_MOCK_TASK_CATALOG_MAX];
            const vm_net_mock_task_state_list_row *previousState = NULL;
            u32 allStateCount = 0;
            bool canAccept = false;
            bool offeredByNpc = false;
            u8 repeatPolicyOffer = VM_NET_MOCK_TASK_REPEAT_NEVER;
            bool replacingCompletedState = false;

            responseSubtype = 11;
            if (taskDefinition != NULL)
            {
                bool definitionAvailable = false;
                bool backpackCanReceive = false;

                offeredByNpc = vm_net_mock_task_offer_context_consume(
                    taskId, &repeatPolicyOffer);
                taskAcceptOfferContext = offeredByNpc;
                canAccept = vm_net_mock_task_state_list_load(
                    activeRole->roleId, false, allStates,
                    VM_NET_MOCK_TASK_CATALOG_MAX, &allStateCount);
                if (!canAccept)
                {
                    taskAcceptFailureCode = "state-read";
                    snprintf(taskAcceptFailureInfo,
                             sizeof(taskAcceptFailureInfo),
                             "\xC8\xCE\xCE\xF1\xD7\xB4\xCC\xAC\xB6\xC1\xC8\xA1"
                             "\xCA\xA7\xB0\xDC\xA3\xAC\xC7\xEB\xC9\xD4\xBA\xF3\xD6\xD8"
                             "\xCA\xD4\xA1\xA3"); /* 任务状态读取失败，请稍后重试。 */
                }
                else
                {
                    previousState = vm_net_mock_task_state_list_find(
                        allStates, allStateCount, taskId);
                    replacingCompletedState = offeredByNpc &&
                                              repeatPolicyOffer !=
                                                  VM_NET_MOCK_TASK_REPEAT_NEVER &&
                                              previousState != NULL &&
                                              previousState->state == 3;
                    definitionAvailable = vm_net_mock_task_definition_available(
                        taskDefinition, activeRole, allStates, allStateCount,
                        repeatPolicyOffer);
                    if (!definitionAvailable)
                    {
                        (void)vm_net_mock_task_definition_unavailable_reason(
                            taskDefinition, activeRole, allStates,
                            allStateCount, repeatPolicyOffer,
                            &taskAcceptFailureCode, taskAcceptFailureInfo,
                            sizeof(taskAcceptFailureInfo));
                    }
                    else
                    {
                        backpackCanReceive = vm_net_mock_task_backpack_can_receive(
                            activeRole, taskDefinition->givenItemId,
                            taskDefinition->givenItemCount, NULL);
                        /* The client clears its interaction prompt while the
                         * NPC offer flow is open.  Retain only the fact that
                         * this exact offer was still authoritative; the final
                         * candidate record is rebuilt after a failed accept. */
                        taskAcceptOfferStillAvailable = offeredByNpc;
                        taskAcceptBlockedByBackpack =
                            taskDefinition->givenItemId != 0 &&
                            taskDefinition->givenItemCount != 0 &&
                            !backpackCanReceive;
                        if (taskAcceptBlockedByBackpack)
                        {
                            taskAcceptFailureCode = "backpack";
                            snprintf(taskAcceptFailureInfo,
                                     sizeof(taskAcceptFailureInfo),
                                     "\xB1\xB3\xB0\xFC\xBF\xD5\xBC\xE4\xB2\xBB\xD7\xE3\xA3\xAC"
                                     "\xCE\xDE\xB7\xA8\xBD\xD3\xC8\xA1\xC8\xCE\xCE\xF1\xA1\xA3"); /* 背包空间不足，无法接取任务。 */
                        }
                    }
                    canAccept = definitionAvailable && backpackCanReceive;
                }
            }
            else
            {
                canAccept = vm_net_mock_task_state_load(activeRole->roleId,
                                                        taskId, &taskState) &&
                            !taskState.found;
            }
            result = canAccept && vm_net_mock_task_accept(activeRole->roleId, taskId,
                                                           replacingCompletedState)
                         ? 0
                         : 1;
            if (result == 0 && taskDefinition != NULL &&
                taskDefinition->givenItemId != 0 &&
                taskDefinition->givenItemCount != 0)
            {
                taskAcceptBackpackBefore = *activeRole;
                if (!vm_net_mock_task_grant_accept_item(activeRole,
                                                        taskDefinition))
                {
                    if (replacingCompletedState)
                        (void)vm_net_mock_task_state_restore(activeRole->roleId,
                                                             previousState);
                    else
                        (void)vm_net_mock_task_delete(activeRole->roleId,
                                                      taskId);
                    result = 1;
                }
                else
                {
                    taskAcceptNeedsBackpackRefresh = true;
                }
            }
            if (result == 0 &&
                (!vm_net_mock_task_state_load(activeRole->roleId, taskId, &taskState) ||
                 !taskState.found || taskState.state != 1))
            {
                result = 1;
            }
            if (result != 0 && taskAcceptFailureInfo[0] == 0)
            {
                taskAcceptFailureCode = "persist";
                snprintf(taskAcceptFailureInfo,
                         sizeof(taskAcceptFailureInfo),
                         "\xC8\xCE\xCE\xF1\xBD\xD3\xC8\xA1\xCA\xA7\xB0\xDC\xA3\xAC"
                         "\xC7\xEB\xC9\xD4\xBA\xF3\xD6\xD8\xCA\xD4\xA1\xA3"); /* 任务接取失败，请稍后重试。 */
            }
            if (result == 0 &&
                !((taskId == VM_NET_MOCK_TEST_TASK_ID &&
                   vm_net_mock_append_test_task_record(taskInfo, sizeof(taskInfo),
                                                       &taskInfoLen,
                                                       taskState.state,
                                                       taskState.progress1,
                                                       taskState.progress2)) ||
                  (taskDefinition != NULL &&
                   vm_net_mock_append_catalog_task_record(taskInfo,
                                                          sizeof(taskInfo),
                                                          &taskInfoLen,
                                                          taskDefinition,
                                                          vm_net_mock_task_prompt_receiver_for_scene(
                                                              taskDefinition,
                                                              vm_net_mock_current_scene_name(),
                                                              promptReceiver,
                                                              sizeof(promptReceiver)),
                                                          taskState.state,
                                                          taskState.progress1,
                                                          taskState.progress2))))
            {
                return 0;
            }
            taskAcceptNeedsCandidateRefresh =
                result != 0 && taskAcceptOfferStillAvailable;
            /* 6/11 is the accept-result parser only.  The original client
             * derives the grey active-task question mark from the complete
             * 6/1 task row, then clears/rebuilds offer prompts from 6/14.
             * Leaving either table stale makes a successfully accepted task
             * look as if it disappeared from its giver NPC. */
            taskAcceptNeedsScenePromptRefresh = result == 0;
            if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 6, 11,
                                             &objectStart) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
                (result == 0 &&
                 !vm_net_mock_put_object_raw(out, outCap, &pos, "taskinfo",
                                             taskInfo, (u16)taskInfoLen)))
            {
                return 0;
            }
            action = "accept";
            evidence = "JianghuOL.CBE:0x01047A7C+0x0104726C(case11)";
        }
        else
        {
            responseSubtype = 6;
            /* SendTaskStateUpdate(0x01046E64) uses 6/6 as the completed-state
             * notification.  Echo the persisted state in the parser-backed
             * taskstate stream so the active task entry updates in place. */
            result = vm_net_mock_task_state_store(activeRole->roleId, taskId, 2) ? 0 : 1;
            if (!vm_net_mock_seq_put_u32(taskInfo, sizeof(taskInfo), &taskInfoLen, taskId) ||
                !vm_net_mock_seq_put_u8(taskInfo, sizeof(taskInfo), &taskInfoLen,
                                        result == 0 ? 2 : 1) ||
                !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 6, 6,
                                             &objectStart) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "tasknum", 1) ||
                !vm_net_mock_put_object_raw(out, outCap, &pos, "taskstate",
                                            taskInfo, (u16)taskInfoLen))
            {
                return 0;
            }
            action = "state";
            evidence = "JianghuOL.CBE:0x01046E64+0x0104726C(case6)";
        }
    }

    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (hasInfoBannerTail)
    {
        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, &pos))
            return 0;
        responseObjectCount += 1;
    }
    if (object.subtype == 4 && result == 1 &&
        !vm_net_mock_append_task_submit_backpack_refresh(
            out, outCap, &pos, &responseObjectCount, taskDefinition,
            committedConsumedSeqs, committedConsumedRemainings,
            committedConsumedCount))
    {
        return 0;
    }
    if (object.subtype == 4 && result == 1 && committedConsumedCount != 0)
    {
        printf("[info][network] mock_task_submit_backpack_refresh task=%u role=%u rows=%u response=7/11 evidence=JianghuOL.CBE:0x01033544\n",
               taskId, activeRole ? activeRole->roleId : 0,
               committedConsumedCount);
    }
    if (taskAcceptNeedsBackpackRefresh &&
        !vm_net_mock_append_task_accept_backpack_refresh(
            out, outCap, &pos, &responseObjectCount,
            &taskAcceptBackpackBefore, activeRole, taskDefinition))
    {
        return 0;
    }
    if (taskAcceptNeedsScenePromptRefresh)
    {
        if (!vm_net_mock_append_taskinfo_empty1_object(
                out, outCap, &pos, vm_net_mock_current_scene_name()) ||
            !vm_net_mock_append_taskaction14_object(out, outCap, &pos, NULL))
        {
            return 0;
        }
        responseObjectCount = (u8)(responseObjectCount + 2u);
        printf("[info][network] mock_task_accept_prompt_refresh task=%u role=%u "
               "objects=6/1+6/14 evidence=0x0104726C(case1,14)->0x01017C6C\n",
               taskId, activeRole ? activeRole->roleId : 0);
    }
    if (object.subtype == 11 && result != 0 &&
        taskAcceptFailureInfo[0] != 0)
    {
        /* `6/11 result=1` preserves task-state atomicity.  The client has a
         * native 25/11 parser for a result-8 GBK info banner.  It follows the
         * failed task result and the request-tail 25/5 acknowledgement, so it
         * cannot be mistaken for a successful accept. */
        if (!vm_net_mock_append_info_banner_text11_object(
                out, outCap, &pos, taskAcceptFailureInfo))
        {
            return 0;
        }
        responseObjectCount += 1;
        printf("[info][network] mock_task_accept_rejected task=%u role=%u "
               "reason=%s offer_context=%u backpack_full=%u "
               "response_objects=%u\n",
               taskId, activeRole ? activeRole->roleId : 0,
               taskAcceptFailureCode ? taskAcceptFailureCode : "unknown",
               taskAcceptOfferContext ? 1u : 0u,
               taskAcceptBlockedByBackpack ? 1u : 0u,
               responseObjectCount);
    }
    if (taskAcceptNeedsCandidateRefresh)
    {
        /* `6/14 action=0` is the parser-backed candidate refresh.  Its case
         * calls scene_refresh_interact_prompt_types after deserializing the
         * rows, restoring the NPC exclamation mark from current authority. */
        if (!vm_net_mock_append_taskaction14_object(out, outCap, &pos, NULL))
            return 0;
        responseObjectCount += 1;
        printf("[info][network] mock_task_accept_restore_candidates task=%u role=%u "
               "result=%u request_info_tail=%u backpack_full=%u response_objects=%u\n",
               taskId, activeRole ? activeRole->roleId : 0,
               result, hasInfoBannerTail ? 1u : 0u,
               taskAcceptBlockedByBackpack ? 1u : 0u, responseObjectCount);
    }
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
    printf("[info][network] mock_task action=%s task=%u role=%u request_subtype=%u response_subtype=%u request_state=%u result=%u request_info_prefix=%u request_info_tail=%u response_objects=%u taskinfo_len=%u resp=%u evidence=%s\n",
           action ? action : "-",
           taskId,
           activeRole ? activeRole->roleId : 0,
           object.subtype,
           responseSubtype,
           requestState,
           result,
           hasInfoBannerPrefix ? 1u : 0u,
           hasInfoBannerTail ? 1u : 0u,
           responseObjectCount,
           taskInfoLen,
           pos,
           evidence);
    vm_autotest_note("mock_task action=%s task=%u role=%u result=%u info_prefix=%u info_tail=%u response_objects=%u taskinfo_len=%u request=6/%u response=6/%u evidence=%s\n",
                     action ? action : "-", taskId,
                     activeRole ? activeRole->roleId : 0,
                     result, hasInfoBannerPrefix ? 1u : 0u,
                     hasInfoBannerTail ? 1u : 0u,
                     responseObjectCount, taskInfoLen,
                     object.subtype, responseSubtype,
                     evidence);
    return pos;
}

typedef struct
{
    u32 taskId;
    char scene[64];
    char sceneName[32];
    u16 x;
    u16 y;
} vm_net_mock_task_transport_target;

static bool vm_net_mock_task_transport_read_smap_row(
    const u8 *data, u32 len, u32 columnCount, u32 *pos,
    char *scene, size_t sceneCap,
    char *sceneName, size_t sceneNameCap,
    u16 *xOut, u16 *yOut)
{
    u32 rowLen = 0;
    u32 rowPos = 0;
    u32 rowEnd = 0;
    u32 x = 0;
    u32 y = 0;

    if (scene != NULL && sceneCap != 0)
        scene[0] = 0;
    if (sceneName != NULL && sceneNameCap != 0)
        sceneName[0] = 0;
    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (data == NULL || pos == NULL || *pos + 4 > len ||
        scene == NULL || sceneCap == 0 ||
        sceneName == NULL || sceneNameCap == 0)
    {
        return false;
    }

    rowLen = vm_net_mock_read_le32_at(data, *pos);
    rowPos = *pos + 4;
    rowEnd = rowPos + rowLen;
    if (rowLen == 0 || rowEnd > len || rowEnd < rowPos)
        return false;
    for (u32 column = 0; column < columnCount && rowPos < rowEnd; ++column)
    {
        u32 valueLen = data[rowPos++];
        const u8 *value = data + rowPos;

        if (rowPos + valueLen > rowEnd)
            return false;
        if (column == 1)
            (void)vm_net_mock_copy_dsh_string_field(scene, sceneCap, value, valueLen);
        else if (column == 2)
            (void)vm_net_mock_copy_dsh_string_field(sceneName, sceneNameCap,
                                                    value, valueLen);
        else if (column == 3)
            x = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
        else if (column == 4)
            y = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
        rowPos += valueLen;
    }
    *pos = rowEnd;
    if (!vm_net_mock_str_ends_with(scene, ".sce") ||
        !vm_net_mock_scene_name_is_download_key(scene))
    {
        return false;
    }
    if (xOut)
        *xOut = (u16)(x <= 0xffffu ? x : 0);
    if (yOut)
        *yOut = (u16)(y <= 0xffffu ? y : 0);
    return true;
}

static bool vm_net_mock_task_transport_scene_contains_npc(
    const char *scene, const char *displayName)
{
    u8 data[8192];
    u32 len = 0;
    u32 start = 0;

    if (scene == NULL || displayName == NULL || displayName[0] == 0)
        return false;
    len = vm_net_mock_load_scene_resource(scene, data, sizeof(data));
    start = vm_net_mock_scene_payload_start(data, len);
    if (len == 0 || start == 0)
        return false;
    for (u32 off = start; off + 8 <= len; ++off)
    {
        u32 pos = off + 2;
        char actorResource[64];
        char scriptName[64];
        char candidateName[32];

        if (vm_net_mock_read_le16_at(data, off) > 32 ||
            !vm_net_mock_read_sce_string_field(
                data, len, &pos, 3, actorResource, sizeof(actorResource)) ||
            !vm_net_mock_str_ends_with(actorResource, ".actor") ||
            !vm_net_mock_read_sce_string_field(
                data, len, &pos, 4, scriptName, sizeof(scriptName)) ||
            !vm_net_mock_str_ends_with(scriptName, ".xse"))
        {
            continue;
        }
        if (pos + 4 <= len && vm_net_mock_read_le16_at(data, pos) == 3 &&
            vm_net_mock_read_le16_at(data, pos + 2) == 2)
        {
            char stateText[32];
            if (!vm_net_mock_read_sce_string_field(
                    data, len, &pos, 2, stateText, sizeof(stateText)))
            {
                continue;
            }
        }
        if (vm_net_mock_read_sce_string_field(
                data, len, &pos, 1, candidateName, sizeof(candidateName)) &&
            strcmp(candidateName, displayName) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_task_transport_resolve_catalog(
    const vm_net_mock_task_definition *task,
    vm_net_mock_task_transport_target *target)
{
    char path[256];
    u8 data[16384];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 headerBytes = 0;

    if (task == NULL || target == NULL ||
        !vm_net_mock_open_server_data_resource("sMap.dsh", ".dsh", NULL,
                                               path, sizeof(path)))
    {
        return false;
    }
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 20 || vm_net_mock_read_le32_at(data, 0) != len - 4)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    headerBytes = vm_net_mock_read_le32_at(data, 12);
    if (columnCount < 5 || columnCount > 64 || rowCount > 10000 ||
        16u + headerBytes > len)
    {
        return false;
    }

    /* Prefer a scene alias explicitly named by the task goal.  Kill and item
     * objectives usually name their hunting scene, while talk-only tasks fall
     * through to the receiver-NPC lookup below. */
    for (u32 pass = 0; pass < 2; ++pass)
    {
        u32 pos = 16u + headerBytes;

        for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
        {
            char scene[64];
            char sceneName[32];
            u16 x = 0;
            u16 y = 0;

            if (!vm_net_mock_task_transport_read_smap_row(
                    data, len, columnCount, &pos,
                    scene, sizeof(scene), sceneName, sizeof(sceneName),
                    &x, &y))
            {
                continue;
            }
            if (pass == 0)
            {
                if (sceneName[0] == 0 || task->goal[0] == 0 ||
                    strstr(task->goal, sceneName) == NULL)
                {
                    continue;
                }
            }
            else
            {
                vm_net_mock_scene_npcinfo_seed seeds[16];
                u32 seedCount = vm_net_mock_collect_scene_npcinfo_seeds(
                    scene, seeds, 16, NULL, NULL);
                bool receiverFound = false;

                for (u32 seedIndex = 0; seedIndex < seedCount; ++seedIndex)
                {
                    if (task->receiver[0] != 0 &&
                        strcmp(seeds[seedIndex].displayName, task->receiver) == 0)
                    {
                        receiverFound = true;
                        x = seeds[seedIndex].x;
                        y = seeds[seedIndex].y;
                        break;
                    }
                }
                if (!receiverFound &&
                    vm_net_mock_task_transport_scene_contains_npc(
                        scene, task->receiver))
                {
                    receiverFound = true;
                }
                if (!receiverFound)
                    continue;
            }

            memset(target, 0, sizeof(*target));
            target->taskId = task->taskId;
            snprintf(target->scene, sizeof(target->scene), "%s", scene);
            snprintf(target->sceneName, sizeof(target->sceneName), "%s",
                     sceneName[0] != 0 ? sceneName : task->receiver);
            target->x = x;
            target->y = y;
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_task_transport_resolve(
    u32 taskId, vm_net_mock_task_transport_target *target)
{
    const vm_net_mock_task_definition *task = NULL;

    if (target == NULL || taskId == 0)
        return false;
    memset(target, 0, sizeof(*target));
    if (taskId == VM_NET_MOCK_TEST_TASK_ID)
    {
        target->taskId = taskId;
        snprintf(target->scene, sizeof(target->scene), "%s",
                 "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"); /* 00蓬莱仙岛_02.sce */
        snprintf(target->sceneName, sizeof(target->sceneName), "%s",
                 "\xc5\xee\xc0\xb3\x2d\xd6\xfd\xbd\xa3\xb9\xc8"); /* 蓬莱-铸剑谷 */
        target->x = 300;
        target->y = 125;
        return true;
    }
    task = vm_net_mock_task_catalog_find_by_id(taskId);
    return vm_net_mock_task_transport_resolve_catalog(task, target);
}

static u32 vm_net_mock_build_task_transport_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_task_state_row taskState;
    vm_net_mock_task_transport_target target;
    u8 destInfo[128];
    u32 destInfoLen = 0;
    u32 taskId = 0;
    u32 transId = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    bool confirm = false;
    bool hasTaskId = false;
    bool hasTransId = false;
    bool activeTask = false;
    bool resolved = false;

    if (request == NULL || requestLen < 9 || out == NULL || outCap < pos ||
        request[0] != 'W' || request[1] != 'T' || request[4] != 1 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 16 ||
        (object.subtype != 5 && object.subtype != 6))
    {
        return 0;
    }

    /*
     * task_handle_destinfo_response(0x01047F0A) turns the 16/6 destination
     * confirmation into a second, one-object 16/6 request after the player
     * chooses \"yes\".  The packet captured on the failing path is exactly
     * 24 bytes: the 15-byte payload is the wrapped u32 `taskid` field below.
     *
     * Do not claim other 16/6 requests here.  Several unrelated mmGame
     * confirmations share the same kind/subtype, and accepting a loose
     * `taskid` scan would steal those request lifecycles.
     */
    confirm = object.subtype == 6;
    if (confirm)
    {
        if (object.payloadLen != 15 || object.payload[0] != 6 ||
            memcmp(object.payload + 1, "taskid", 6) != 0 ||
            object.payload[7] != 0 || object.payload[8] != 0x06 ||
            object.payload[9] != 0 || object.payload[10] != 4 ||
            !vm_net_mock_get_object_u32_field(object.payload,
                                              object.payloadLen,
                                              "taskid", &taskId))
        {
            return 0;
        }
        hasTaskId = true;
    }
    else
    {
        hasTaskId = vm_net_mock_get_object_u32_field(object.payload,
                                                     object.payloadLen,
                                                     "taskid", &taskId);
        hasTransId = vm_net_mock_get_object_u32_field(object.payload,
                                                      object.payloadLen,
                                                      "transid", &transId);
        if (hasTaskId == hasTransId)
            return 0;
    }

    role = vm_net_mock_active_role();
    memset(&taskState, 0, sizeof(taskState));
    if (hasTransId)
        taskId = transId;
    if (role != NULL && taskId != 0 &&
        vm_net_mock_task_state_load(role->roleId, taskId, &taskState) &&
        taskState.found && (taskState.state == 1 || taskState.state == 2))
    {
        activeTask = true;
        resolved = vm_net_mock_task_transport_resolve(taskId, &target);
    }

    if (confirm)
    {
        vm_net_mock_scene_change_target sceneTarget;

        /* A confirmed teleport is a fresh direct mmGame scene entry, rather
         * than an empty acknowledgement.  This is the same 16/3 contract used
         * by client-initiated direct scene entries: mmGame parses its scene and
         * posinfo fields, then emits the ordinary runtime follow-up requests.
         * Sending a 30/1 in this callback would re-enter the scene manager
         * while HandleItemUseConfirm is still unwinding and violates that
         * lifecycle. */
        if (!activeTask || !resolved)
            return 0;

        memset(&sceneTarget, 0, sizeof(sceneTarget));
        snprintf(sceneTarget.scene, sizeof(sceneTarget.scene), "%s", target.scene);
        sceneTarget.x = target.x;
        sceneTarget.y = target.y;
        sceneTarget.exitId = 0;
        sceneTarget.mapType = 2;
        sceneTarget.hasSceEntry = true;
        sceneTarget.needsSceneDownload = false;
        pos = vm_net_mock_build_mmgame_scene_transfer_start_response(
            &sceneTarget, out, outCap);
        if (pos == 0)
            return 0;

        vm_net_mock_mark_direct_scene_enter_completed(
            &sceneTarget, "task-transport-confirm");
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        g_vm_net_mock_last_scene_change_fb4_type = 1;
        vm_net_mock_save_player_pos_state(sceneTarget.scene, sceneTarget.x,
                                          sceneTarget.y,
                                          "task-transport-confirm");
        printf("[info][network] mock_task_transport phase=confirm task=%u role=%u active=1 resolved=1 scene=%s scene_name=%s pos=(%u,%u) response=16/3 resp=%u evidence=JianghuOL.CBE:0x01047F0A->0x010190A8->mmGame:0x11CE,0x0BCC\n",
               taskId, role ? role->roleId : 0, target.scene,
               target.sceneName, target.x, target.y, pos);
        vm_autotest_note("mock_task_transport phase=confirm task=%u scene=%s pos=(%u,%u) response=16/3 evidence=JianghuOL.CBE:0x01047F0A+0x010190A8 mmGame:0x11CE,0x0BCC\n",
                         taskId, target.scene, target.x, target.y);
        return pos;
    }

    if (hasTaskId)
    {
        if (resolved &&
            (!vm_net_mock_seq_put_u32(destInfo, sizeof(destInfo), &destInfoLen,
                                      taskId) ||
             !vm_net_mock_seq_put_string(destInfo, sizeof(destInfo), &destInfoLen,
                                         target.sceneName)))
        {
            return 0;
        }
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 16, 5,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "destnum",
                                       resolved ? 1 : 0) ||
            (resolved &&
             !vm_net_mock_put_object_raw(out, outCap, &pos, "destinfo",
                                         destInfo, (u16)destInfoLen)))
        {
            return 0;
        }
    }
    else
    {
        if (!resolved)
        {
            if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 16, 5,
                                             &objectStart) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "destnum", 0))
            {
                return 0;
            }
        }
        else if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 16, 6,
                                         &objectStart) ||
            !vm_net_mock_put_object_string(
                out, outCap, &pos, "text",
                "\xc8\xb7\xb6\xa8\xcb\xb2\xd2\xc6\xa3\xbf") || /* 确定瞬移？ */
            !vm_net_mock_put_object_string(out, outCap, &pos, "destscene",
                                           target.scene) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "scenename",
                                           target.sceneName) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "transid", taskId) ||
            /* task_handle_destinfo_response(0x01047F0A) forwards this same
             * 16/6 object to HandleItemUseConfirm(0x010190A8) for a
             * same-scene destination.  That path requires result/value just
             * like the proven 16/4 teleport-stone confirmation contract. */
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 0) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "value",
                                        VM_NET_MOCK_TELEPORT_STONE_COST))
        {
            return 0;
        }
    }

    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_task_transport phase=%s task=%u role=%u active=%u resolved=%u scene=%s scene_name=%s pos=(%u,%u) response=16/%u resp=%u evidence=JianghuOL.CBE:0x01047E9A+0x01047F0A\n",
           hasTaskId ? "list" : "select", taskId,
           role ? role->roleId : 0, activeTask ? 1u : 0u,
           resolved ? 1u : 0u,
           resolved ? target.scene : "-",
           resolved ? target.sceneName : "-",
           resolved ? target.x : 0,
           resolved ? target.y : 0,
           hasTaskId || !resolved ? 5u : 6u, pos);
    vm_autotest_note("mock_task_transport phase=%s task=%u active=%u resolved=%u response=16/%u evidence=JianghuOL.CBE:0x01047E9A+0x01047F0A runtime:wt16/5\n",
                     hasTaskId ? "list" : "select", taskId,
                     activeTask ? 1u : 0u, resolved ? 1u : 0u,
                     hasTaskId || !resolved ? 5u : 6u);
    return pos;
}

