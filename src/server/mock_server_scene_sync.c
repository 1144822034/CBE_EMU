static bool vm_net_mock_append_scene_room_roles_object(u8 *out, u32 outCap, u32 *pos, u32 *roleNumOut);
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
static bool vm_net_mock_build_scene_list_otherinfo_blob(const char *scene,
                                                        u8 *otherInfo,
                                                        u32 otherInfoCap,
                                                        u32 *otherInfoLenOut,
                                                        u32 *roleCountOut);

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
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg", 0))
            return false;
    }
    else if (subtype == 32)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
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

static bool vm_net_mock_build_scene_npcinfo_blob(
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
             * monkey). Publish every selected row as one safe dependency set
             * so a clean client can load the whole catalog. */
            if (!vm_net_mock_ensure_actor_resource_published(
                    selectedSeeds[i].actorResource, &publishError))
            {
                printf("[error][network] mock_scene_npc_resource_publish_failed scene=%s actor=%u resource=%s reason=%s action=skip-row\n",
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
         * deliver in the initial scene lifecycle. */
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
    u32 prerequisiteTaskId;
    u32 givenItemId;
    u32 givenItemCount;
    u32 rewardExp;
    u32 rewardMoney;
    u32 rewardItemId;
    u32 rewardItemCount;
    u8 rewardItemType;
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

static bool vm_net_mock_task_catalog_apply_db(void)
{
    vm_net_mock_task_catalog_db_context context;

    memset(&context, 0, sizeof(context));
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
        !vm_mysql_query(
            "SELECT task_id,enabled,level,difficulty,classification,"
            "requirement_type1,requirement_count1,requirement_id1,requirement_type2,requirement_count2,requirement_id2,"
            "prerequisite_task_id,given_item_id,given_item_count,reward_exp,reward_money,reward_item_id,reward_item_count,reward_item_type,"
            "HEX(name),HEX(giver),HEX(receiver),HEX(goal),HEX(reward_text),HEX(offer_dialog),HEX(active_dialog),HEX(completed_dialog) "
            "FROM server_tasks ORDER BY task_id",
            vm_net_mock_task_catalog_db_row, &context))
    {
        printf("[error][mock-admin] task_catalog_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    printf("[info][mock-admin] task_catalog_db_load rows=%u overridden=%u custom=%u skipped=%u\n",
           context.loaded, context.overridden, context.custom, context.skipped);
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
    vm_mock_mysql_guild_u32_context context;

    if (countOut)
        *countOut = 0;
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM account_role_tasks WHERE task_id=%u AND task_state IN (1,2)",
             taskId);
    if (!vm_mysql_query(query, vm_mock_mysql_guild_u32_row, &context) ||
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
    char nameHex[sizeof(task->name) * 2 + 1];
    char giverHex[sizeof(task->giver) * 2 + 1];
    char receiverHex[sizeof(task->receiver) * 2 + 1];
    char goalHex[sizeof(task->goal) * 2 + 1];
    char rewardTextHex[sizeof(task->rewardText) * 2 + 1];
    char offerHex[sizeof(task->offerDialog) * 2 + 1];
    char activeHex[sizeof(task->activeDialog) * 2 + 1];
    char completedHex[sizeof(task->completedDialog) * 2 + 1];
    char query[8192];
    u32 activeCount = 0;

    if (errorOut)
        *errorOut = "invalid task definition";
    if (!vm_net_mock_load_task_catalog() ||
        !vm_net_mock_task_definition_is_valid(task))
    {
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
    if (!vm_mysql_exec(query) || !vm_net_mock_task_catalog_reload())
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
}

static bool vm_net_mock_task_admin_delete_override(u32 taskId,
                                                    const char **errorOut)
{
    const vm_net_mock_task_definition *task = vm_net_mock_task_admin_find(taskId);
    char query[256];
    u32 activeCount = 0;
    u32 bindingCount = 0;
    vm_mock_mysql_guild_u32_context countContext;

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
        (!vm_mysql_query(query, vm_mock_mysql_guild_u32_row, &countContext) ||
         countContext.invalid || !countContext.found ||
         (bindingCount = countContext.value) != 0))
    {
        if (errorOut)
            *errorOut = bindingCount != 0
                            ? "the task is still bound to a dynamic npc"
                            : vm_mysql_last_error();
        return false;
    }
    snprintf(query, sizeof(query), "DELETE FROM server_tasks WHERE task_id=%u",
             taskId);
    if (!vm_mysql_exec(query) || !vm_net_mock_task_catalog_reload())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] task_override_delete task=%u\n", taskId);
    return true;
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
        "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31"; /* c00蓬莱仙岛_01 */
    static const char swordValleyScene[] =
        "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32"; /* 00蓬莱仙岛_02 */
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
    printf("[info][network] mock_scene_npc_alias_validate scene=%s rows=%u total=%u guojing=%u policy=guojing-only source=00_legacy-sce evidence=runtime-empty-c00+SCE2\n",
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
        copperStageCount < 1 || copperStageTotal < 1 || !foundGuoJing ||
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
           copperStageCount >= 1 && copperStageTotal >= 1 && foundGuoJing &&
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

    if (context == NULL || columnCount != 4 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &taskId) || taskId == 0 ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &state) || state > 0xffu ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &progress1) || progress1 > 0xffu ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &progress2) || progress2 > 0xffu)
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
             "SELECT task_id,task_state,progress1,progress2 FROM account_role_tasks "
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

static bool vm_net_mock_task_definition_available(
    const vm_net_mock_task_definition *task,
    const vm_net_mock_role_state *role,
    const vm_net_mock_task_state_list_row *states,
    u32 stateCount)
{
    const vm_net_mock_task_state_list_row *prerequisite = NULL;

    if (task == NULL || role == NULL || role->level < task->level ||
        vm_net_mock_task_state_list_find(states, stateCount, task->taskId) != NULL)
    {
        return false;
    }
    if (task->prerequisiteTaskId == 0)
        return true;
    prerequisite = vm_net_mock_task_state_list_find(states, stateCount,
                                                    task->prerequisiteTaskId);
    return prerequisite != NULL && prerequisite->state == 3;
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
        if (persisted == NULL &&
            vm_net_mock_task_definition_available(task, role, states, stateCount))
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
                 vm_net_mock_task_definition_available(task, role, states, stateCount))
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
        !vm_net_mock_scene_names_equal_loose(cache->scene, scene))
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

static bool vm_net_mock_task_accept(u32 roleId, u32 taskId)
{
    char accountHex[129];
    char query[768];

    if (roleId == 0 || taskId == 0 || !vm_net_mock_mysql_account_hex(accountHex))
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_tasks"
             "(account_id,role_id,task_id,task_state,progress1,progress2) "
             "VALUES(CAST(X'%s' AS CHAR),%u,%u,1,0,0)",
             accountHex, roleId, taskId);
    return vm_mysql_exec(query);
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

static u32 vm_net_mock_task_required_item_count(
    const vm_net_mock_task_definition *task, u32 itemId)
{
    u32 count = 0;

    if (task == NULL || itemId == 0)
        return 0;
    if (task->requirementType1 == 1 && task->requirementId1 == itemId)
        count += task->requirementCount1;
    if (task->requirementType2 == 1 && task->requirementId2 == itemId)
        count += task->requirementCount2;
    return count;
}

static bool vm_net_mock_task_role_has_required_items(
    vm_net_mock_role_state *role, const vm_net_mock_task_definition *task)
{
    u32 itemIds[2];

    if (role == NULL || task == NULL)
        return false;
    itemIds[0] = task->requirementType1 == 1 ? task->requirementId1 : 0;
    itemIds[1] = task->requirementType2 == 1 ? task->requirementId2 : 0;
    for (u32 i = 0; i < 2; ++i)
    {
        vm_net_mock_backpack_item_state *item = NULL;
        u32 requiredCount = 0;

        if (itemIds[i] == 0 || (i != 0 && itemIds[i] == itemIds[0]))
            continue;
        requiredCount = vm_net_mock_task_required_item_count(task, itemIds[i]);
        item = vm_net_mock_role_find_backpack_item(role, itemIds[i], 0);
        if (requiredCount != 0 && (item == NULL || item->count < requiredCount))
            return false;
    }
    return true;
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
        u32 itemIds[2] = {
            consumedByTask->requirementType1 == 1 ? consumedByTask->requirementId1 : 0,
            consumedByTask->requirementType2 == 1 ? consumedByTask->requirementId2 : 0};
        for (u32 i = 0; i < 2; ++i)
        {
            vm_net_mock_backpack_item_state *item = NULL;
            u32 consumedCount = 0;

            if (itemIds[i] == 0 || (i != 0 && itemIds[i] == itemIds[0]))
                continue;
            consumedCount = vm_net_mock_task_required_item_count(consumedByTask,
                                                                 itemIds[i]);
            item = vm_net_mock_role_find_backpack_item(role, itemIds[i], 0);
            if (item != NULL && consumedCount != 0 && item->count <= consumedCount)
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

static bool vm_net_mock_task_commit_reward(
    vm_net_mock_role_state *role, const vm_net_mock_task_definition *task)
{
    u32 consumedIds[2] = {0, 0};
    u32 consumedCounts[2] = {0, 0};
    u32 consumedSlots = 0;
    bool rewardAdded = false;

    if (role == NULL || task == NULL)
        return false;
    if (!vm_net_mock_task_role_has_required_items(role, task) ||
        !vm_net_mock_task_backpack_can_receive(role, task->rewardItemId,
                                               task->rewardItemCount, task))
    {
        return false;
    }
    if (!vm_net_mock_task_state_store(role->roleId, task->taskId, 3))
        return false;

    if (task->requirementType1 == 1 && task->requirementId1 != 0 &&
        task->requirementCount1 != 0)
    {
        consumedIds[consumedSlots] = task->requirementId1;
        consumedCounts[consumedSlots] = task->requirementCount1;
        ++consumedSlots;
    }
    if (task->requirementType2 == 1 && task->requirementId2 != 0 &&
        task->requirementCount2 != 0)
    {
        if (consumedSlots != 0 && consumedIds[0] == task->requirementId2)
            consumedCounts[0] += task->requirementCount2;
        else
        {
            consumedIds[consumedSlots] = task->requirementId2;
            consumedCounts[consumedSlots] = task->requirementCount2;
            ++consumedSlots;
        }
    }
    for (u32 i = 0; i < consumedSlots; ++i)
    {
        if (!vm_net_mock_role_consume_backpack_item(role, consumedIds[i], 0,
                                                    consumedCounts[i], NULL))
        {
            for (u32 restored = 0; restored < i; ++restored)
                (void)vm_net_mock_role_add_backpack_item(consumedIds[restored],
                                                         consumedCounts[restored], NULL);
            (void)vm_net_mock_task_state_store(role->roleId, task->taskId, 2);
            return false;
        }
    }
    rewardAdded = task->rewardItemId == 0 || task->rewardItemCount == 0 ||
                  vm_net_mock_role_add_backpack_item(task->rewardItemId,
                                                     task->rewardItemCount, NULL);
    if (!rewardAdded)
    {
        for (u32 i = 0; i < consumedSlots; ++i)
            (void)vm_net_mock_role_add_backpack_item(consumedIds[i],
                                                     consumedCounts[i], NULL);
        (void)vm_net_mock_task_state_store(role->roleId, task->taskId, 2);
        return false;
    }

    (void)vm_net_mock_role_add_exp(role, task->rewardExp);
    role->money = (0xffffffffu - role->money < task->rewardMoney)
                      ? 0xffffffffu
                      : role->money + task->rewardMoney;
    vm_net_mock_role_normalize(role);
    vm_net_mock_role_db_save("task-commit");
    printf("[info][network] mock_task_reward task=%u role=%u exp=%u money=%u item=%u item_type=%u count=%u consumed=%u\n",
           task->taskId, role->roleId, task->rewardExp, task->rewardMoney,
           task->rewardItemId, task->rewardItemType, task->rewardItemCount,
           consumedSlots);
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

static void vm_net_mock_task_progress_after_battle(u32 enemyId,
                                                   u32 enemyCount,
                                                   u32 dropItemId,
                                                   u32 dropCount)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_task_state_list_row states[VM_NET_MOCK_TASK_CATALOG_MAX];
    u32 stateCount = 0;

    if (role == NULL || enemyId == 0 || enemyCount == 0 ||
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
        bool changed = false;
        u8 nextState = 1;

        if (task == NULL || states[i].state != 1)
            continue;
        if (task->requirementType1 == 2 && task->requirementId1 == enemyId)
        {
            progress1 = vm_net_mock_min_u32(progress1 + enemyCount,
                                            task->requirementCount1);
            changed = true;
        }
        else if (task->requirementType1 == 1 && task->requirementId1 == dropItemId &&
                 dropCount != 0)
        {
            progress1 = vm_net_mock_min_u32(progress1 + dropCount,
                                            task->requirementCount1);
            changed = true;
        }
        if (task->requirementType2 == 2 && task->requirementId2 == enemyId)
        {
            progress2 = vm_net_mock_min_u32(progress2 + enemyCount,
                                            task->requirementCount2);
            changed = true;
        }
        else if (task->requirementType2 == 1 && task->requirementId2 == dropItemId &&
                 dropCount != 0)
        {
            progress2 = vm_net_mock_min_u32(progress2 + dropCount,
                                            task->requirementCount2);
            changed = true;
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
            printf("[info][network] mock_task_battle_progress task=%u role=%u enemy=%u enemies=%u drop=%u drop_count=%u progress=%u/%u,%u/%u state=%u\n",
                   task->taskId, role->roleId, enemyId, enemyCount,
                   dropItemId, dropCount,
                   progress1, task->requirementCount1,
                   progress2, task->requirementCount2, nextState);
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
    vm_net_mock_scene_npcinfo_seed seeds[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    u32 seedCount = 0;

    if (resolved != NULL && resolvedCap != 0)
        resolved[0] = 0;
    if (task == NULL || resolved == NULL || resolvedCap == 0 ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return task ? task->receiver : "";
    }

    memset(seeds, 0, sizeof(seeds));
    seedCount = vm_net_mock_select_scene_npcinfo_seeds(
        scene, seeds, VM_NET_MOCK_SCENE_NPCINFO_MAX, NULL, NULL);

    /* Preserve the original receiver whenever that actor really exists in the
     * current scene.  Admin-bound tasks can intentionally use another NPC;
     * their runtime row must carry that visible node name or the client's
     * node+68 comparison can never assign the grey/normal question mark. */
    for (u32 i = 0; i < seedCount; ++i)
    {
        if (task->receiver[0] != 0 &&
            strcmp(seeds[i].displayName, task->receiver) == 0)
        {
            return task->receiver;
        }
    }
    for (u32 i = 0; i < seedCount; ++i)
    {
        if (seeds[i].taskId == task->taskId && seeds[i].displayName[0] != 0)
        {
            snprintf(resolved, resolvedCap, "%s", seeds[i].displayName);
            return resolved;
        }
    }
    for (u32 i = 0; i < seedCount; ++i)
    {
        vm_net_mock_xse_summary summary;

        memset(&summary, 0, sizeof(summary));
        if (seeds[i].scriptName[0] == 0 ||
            !vm_net_mock_load_xse_summary(seeds[i].scriptName, &summary))
        {
            continue;
        }
        for (u32 refIndex = 0; refIndex < summary.taskRefCount; ++refIndex)
        {
            const vm_net_mock_xse_task_ref *ref = &summary.taskRefs[refIndex];
            if (ref->taskId == task->taskId && (ref->active || ref->completed) &&
                seeds[i].displayName[0] != 0)
            {
                snprintf(resolved, resolvedCap, "%s", seeds[i].displayName);
                return resolved;
            }
        }
    }
    return task->receiver;
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
    const char *serviceOptionName = NULL;
    const char *serviceOptionDescription = NULL;
    u32 serviceOptionValue = 0;
    u8 serviceOptionCount = 0;
    u8 dialog[1024];
    u32 dialogLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;

    if (!vm_net_mock_is_npc_dialog_request(request, requestLen, &actorId, &index) ||
        out == NULL || outCap < pos)
    {
        return 0;
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
    dialogText = vm_net_mock_npc_dialog_text(actorId);
    memset(&taskState, 0, sizeof(taskState));
    memset(&xseSummary, 0, sizeof(xseSummary));
    memset(allTaskStates, 0, sizeof(allTaskStates));
    memset(optionTasks, 0, sizeof(optionTasks));
    memset(optionSubmits, 0, sizeof(optionSubmits));
    memset(completedTaskIds, 0, sizeof(completedTaskIds));
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
            requirementsDone =
                progress1 >= task->requirementCount1 &&
                progress2 >= task->requirementCount2;
            /* A completion marker in this XSE means the clicked actor owns the
             * delivery branch. For talk-only tasks both required counts are
             * zero; combat/item tasks transition only after persisted progress
             * reaches task.dsh's two thresholds. */
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
                                                      allTaskStateCount) &&
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

        for (u32 optionIndex = 0; optionIndex < optionCount; ++optionIndex)
        {
            if (optionTasks[optionIndex] != NULL &&
                optionTasks[optionIndex]->taskId == matchedSeed->taskId)
            {
                duplicate = true;
                break;
            }
        }
        if (task != NULL && state == 1 &&
            progress1 >= task->requirementCount1 &&
            progress2 >= task->requirementCount2 &&
            vm_net_mock_task_state_store(activeRole->roleId, task->taskId, 2))
        {
            state = 2;
            if (completedTaskCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
                completedTaskIds[completedTaskCount++] = task->taskId;
        }
        if (task != NULL && !duplicate && optionCount < VM_NET_MOCK_XSE_TASK_REF_MAX)
        {
            if (state == 0 &&
                vm_net_mock_task_definition_available(task, activeRole,
                                                      allTaskStates,
                                                      allTaskStateCount))
            {
                optionTasks[optionCount] = task;
                optionSubmits[optionCount] = false;
                optionCount += 1;
            }
            else if (state == 2)
            {
                optionTasks[optionCount] = task;
                optionSubmits[optionCount] = true;
                optionCount += 1;
            }
        }
        if (task != NULL)
        {
            if (state == 0)
                dialogText = task->offerDialog[0] != 0
                                 ? task->offerDialog
                                 : "\xce\xd2\xd5\xe2\xc0\xef\xd3\xd0\xd2\xbb\xcf\xee\xc8\xce\xce\xf1\xa3\xac\xc4\xe3\xd4\xb8\xd2\xe2\xb0\xef\xc3\xa6\xc2\xf0\xa3\xbf";
            else if (state == 1)
                dialogText = task->activeDialog[0] != 0
                                 ? task->activeDialog
                                 : "\xc8\xce\xce\xf1\xbb\xb9\xd4\xda\xbd\xf8\xd0\xd0\xd6\xd0\xa3\xac\xc7\xeb\xcd\xea\xb3\xc9\xc4\xbf\xb1\xea\xba\xf3\xd4\xd9\xc0\xb4\xa1\xa3";
            else
                dialogText = task->completedDialog[0] != 0
                                 ? task->completedDialog
                                 : "\xc8\xce\xce\xf1\xd2\xd1\xbe\xad\xcd\xea\xb3\xc9\xa3\xac\xbf\xc9\xd2\xd4\xcc\xe1\xbd\xbb\xc1\xcb\xa1\xa3";
        }
    }

    if (matchedSeed != NULL)
    {
        switch (matchedSeed->kind)
        {
        case VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT:
            serviceOptionName = "\xb2\xe9\xbf\xb4\xbf\xc9\xb9\xba\xc2\xf2\xce\xe4\xc6\xf7"; /* 查看可购买武器 */
            serviceOptionDescription = "\xce\xe4\xc6\xf7\xc9\xcc\xb5\xea"; /* 武器商店 */
            serviceOptionValue = VM_NET_MOCK_NPC_SERVICE_OPEN_WEAPON;
            break;
        case VM_NET_MOCK_NPC_KIND_EQUIPMENT_REPAIR:
            serviceOptionName = "\xd0\xde\xc0\xed\xc8\xab\xb2\xbf\xd7\xb0\xb1\xb8"; /* 修理全部装备 */
            serviceOptionDescription = "\xd7\xb0\xb1\xb8\xd0\xde\xc0\xed"; /* 装备修理 */
            serviceOptionValue = VM_NET_MOCK_NPC_SERVICE_REPAIR_ALL;
            break;
        case VM_NET_MOCK_NPC_KIND_SKILL_TRAINER:
            serviceOptionName = "\xd1\xa7\xcf\xb0\xbc\xbc\xc4\xdc"; /* 学习技能 */
            serviceOptionDescription = "\xbc\xbc\xc4\xdc\xb5\xbc\xca\xa6"; /* 技能导师 */
            serviceOptionValue = VM_NET_MOCK_NPC_SERVICE_OPEN_SKILLS;
            break;
        case VM_NET_MOCK_NPC_KIND_ARMOR_MERCHANT:
            serviceOptionName = "\xb9\xba\xc2\xf2\xb7\xc0\xbe\xdf"; /* 购买防具 */
            serviceOptionDescription = "\xb7\xc0\xbe\xdf\xc9\xcc\xb5\xea"; /* 防具商店 */
            serviceOptionValue = VM_NET_MOCK_NPC_SERVICE_OPEN_ARMOR;
            break;
        case VM_NET_MOCK_NPC_KIND_MEDICINE_MERCHANT:
            serviceOptionName = "\xb9\xba\xc2\xf2\xd2\xa9\xc6\xb7"; /* 购买药品 */
            serviceOptionDescription = "\xd2\xa9\xc6\xb7\xc9\xcc\xb5\xea"; /* 药品商店 */
            serviceOptionValue = VM_NET_MOCK_NPC_SERVICE_OPEN_MEDICINE;
            break;
        case VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE:
            if (matchedSeed->actorId <= VM_NET_MOCK_NPC_SERVICE_VALUE_MASK)
            {
                serviceOptionName = "\xb8\xb1\xb1\xbe\xb4\xab\xcb\xcd\xd3\xeb\xcc\xf4\xd5\xbd"; /* 副本传送与挑战 */
                serviceOptionDescription = "\xb8\xb1\xb1\xbe\xcf\xf2\xb5\xbc"; /* 副本向导 */
                serviceOptionValue = VM_NET_MOCK_NPC_SERVICE_OPEN_INSTANCE_BASE |
                                     matchedSeed->actorId;
            }
            break;
        default:
            break;
        }
        serviceOptionCount = serviceOptionValue != 0 ? 1 : 0;
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
                                (actorId == VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID &&
                                 showTaskOption ? 1u : 0u) + optionCount +
                                    serviceOptionCount))
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
    if (serviceOptionCount != 0)
    {
        if (!vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 4) ||
            !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                        serviceOptionName) ||
            !vm_net_mock_seq_put_u8(dialog, sizeof(dialog), &dialogLen, 1) ||
            !vm_net_mock_seq_put_u32(dialog, sizeof(dialog), &dialogLen,
                                     serviceOptionValue) ||
            !vm_net_mock_seq_put_string(dialog, sizeof(dialog), &dialogLen,
                                        serviceOptionDescription))
        {
            return 0;
        }
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

    printf("[info][network] mock_npc_dialog actor=%u index=%u name=%s script=%s scene=%s catalog_match=%u npc_kind=%u service_action=%u task_offer=%u task_accepted=%u task_state=%u task_completed_now=%u task_option_action=%u xse_dialogs=%u dialog_len=%u objects=%u resp=%u evidence=JianghuOL.CBE:0x01037ED4+0x010380E8+0x010492B0(action1/action4)+0x0104726C(case6)\n",
           actorId,
           index,
           matchedSeed && matchedSeed->displayName[0] ? matchedSeed->displayName : "-",
           matchedSeed && matchedSeed->scriptName[0] ? matchedSeed->scriptName : "-",
           scene ? scene : "-",
           matchedSeed ? 1u : 0u,
           matchedSeed ? matchedSeed->kind : 0u,
           serviceOptionValue,
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
    const vm_net_mock_shop_catalog_item *item, u32 selector)
{
    if (item == NULL || !item->enabled ||
        item->itemId > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK)
    {
        return false;
    }
    if (selector == VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR)
        return !item->isEquip && item->category == 10;
    return selector >= 1u && selector <= 10u && item->isEquip &&
           item->category == selector - 1u &&
           vm_net_mock_equipment_slot_for_category(item->category) <
               VM_NET_MOCK_EQUIP_SLOT_COUNT;
}

static u32 vm_net_mock_npc_shop_selector_total(u32 selector)
{
    u32 total = 0;

    if (!vm_net_mock_npc_shop_selector_is_valid(selector))
        return 0;
    for (u32 i = 0; i < vm_net_mock_load_shop_catalog(); ++i)
    {
        if (vm_net_mock_npc_shop_item_matches_selector(
                &g_vm_net_mock_shop_catalog[i], selector))
        {
            ++total;
        }
    }
    return total;
}

static const vm_net_mock_shop_catalog_item *
vm_net_mock_npc_shop_selector_item_at(u32 selector, u32 ordinal)
{
    u32 seen = 0;

    if (!vm_net_mock_npc_shop_selector_is_valid(selector))
        return NULL;
    for (u32 i = 0; i < vm_net_mock_load_shop_catalog(); ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        if (!vm_net_mock_npc_shop_item_matches_selector(item, selector))
            continue;
        if (seen++ == ordinal)
            return item;
    }
    return NULL;
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

static bool vm_net_mock_is_npc_service_dialog_request(
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
        (serviceValue & VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK) <
            (VM_NET_MOCK_NPC_SERVICE_OPEN_WEAPON &
             VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK) ||
        (serviceValue & VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK) >
            (VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE &
             VM_NET_MOCK_NPC_SERVICE_OPCODE_MASK))
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

static u32 vm_net_mock_build_challenge_interaction_response_ex(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap,
    bool forceNonSceneStart);

static const vm_net_mock_scene_npcinfo_seed *
vm_net_mock_instance_guide_seed(u32 actorId)
{
    const char *scene = vm_net_mock_current_scene_name();
    int index = vm_net_mock_dynamic_npc_find_override(scene, actorId);

    if (index < 0 ||
        g_vm_net_mock_dynamic_npc_overrides[index].seed.kind !=
            VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE ||
        !g_vm_net_mock_dynamic_npc_overrides[index].enabled)
    {
        return NULL;
    }
    return &g_vm_net_mock_dynamic_npc_overrides[index].seed;
}

static u32 vm_net_mock_build_instance_enter_response(
    const vm_net_mock_scene_npcinfo_seed *seed, u8 *out, u32 outCap)
{
    vm_net_mock_scene_change_target target;
    u32 pos = 0;

    if (seed == NULL || seed->instanceScene[0] == 0 ||
        seed->instanceX == 0 || seed->instanceY == 0)
    {
        return 0;
    }
    memset(&target, 0, sizeof(target));
    snprintf(target.scene, sizeof(target.scene), "%s", seed->instanceScene);
    target.x = seed->instanceX;
    target.y = seed->instanceY;
    target.mapType = 2;
    target.hasSceEntry = true;
    pos = vm_net_mock_build_scene_channel_enter_combo_for_target(
        &target, out, outCap);
    if (pos == 0)
        return 0;

    vm_net_mock_remember_scene_change_target(&target);
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = true;
    g_vm_net_mock_teleport_stone_direct_enter_pending = true;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    vm_net_mock_save_player_pos_state(target.scene, target.x, target.y,
                                      "npc-instance-enter");
    printf("[info][network] mock_npc_instance_enter actor=%u scene=%s pos=(%u,%u) response=30/1 resp=%u evidence=JianghuOL.CBE:0x01039B8A+0x010396D6\n",
           seed->actorId, target.scene, target.x, target.y, pos);
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
        synthetic, requestPos, out, outCap, true);
    if (responseLen != 0)
    {
        printf("[info][network] mock_npc_instance_challenge_start actor=%u enemy=%u pos=(%u,%u) response=4/10 resp=%u evidence=JianghuOL.CBE:0x01039566(30/10)+mmBattle:0x67AC(non-scene-start)\n",
               actorId, enemyId, challengeX, challengeY,
               responseLen);
    }
    return responseLen;
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
    }
    printf("[info][network] mock_npc_instance_challenge_prompt client=%08x actor=%u enemy=%u scene=%s pos=(%u,%u) blocked=%u response=26/0+30/9 resp=%u evidence=JianghuOL.CBE:0x01039C28+0x010395AA\n",
           session->clientId, seed->actorId, seed->challengeEnemyId, scene,
           challengeX, challengeY, leaderBlocked ? 1u : 0u, pos);
    return pos;
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
        !vm_net_mock_scene_names_equal_loose(
            scene, session->instanceChallengeScene))
    {
        printf("[warn][network] mock_npc_instance_challenge_confirm_drop client=%08x actor=%u enemy=%u age_ticks=%u current_scene=%s pending_scene=%s reason=expired-or-scene-changed\n",
               session->clientId, session->instanceChallengeActorId,
               session->instanceChallengeEnemyId, ageTicks,
               scene ? scene : "-", session->instanceChallengeScene);
        session->instanceChallengePending = false;
        session->instanceChallengeBattlePending = false;
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

    if (out == NULL || session == NULL ||
        !session->instanceChallengeBattlePending)
    {
        return 0;
    }
    ageTicks = g_schedulerTick - session->instanceChallengeTick;
    if (ageTicks > (10u * 1000u / VM_SCHED_FRAME_MS) ||
        !session->sceneVisibleReady || session->sceneVisiblePending ||
        !vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) ||
        !vm_net_mock_scene_names_equal_loose(
            session->sceneVisibleScene, session->instanceChallengeScene))
    {
        printf("[warn][mock-service] instance_challenge_battle_drop client=%08x actor=%u enemy=%u age_ticks=%u visible_scene=%s pending_scene=%s reason=expired-or-scene-changed\n",
               session->clientId, session->instanceChallengeActorId,
               session->instanceChallengeEnemyId, ageTicks,
               session->sceneVisibleScene, session->instanceChallengeScene);
        session->instanceChallengeBattlePending = false;
        session->instanceChallengeActorId = 0;
        session->instanceChallengeEnemyId = 0;
        session->instanceChallengeX = 0;
        session->instanceChallengeY = 0;
        session->instanceChallengeTick = 0;
        session->instanceChallengeScene[0] = 0;
        return 0;
    }

    responseLen = vm_net_mock_build_instance_challenge_battle_response(
        session->instanceChallengeActorId,
        session->instanceChallengeEnemyId,
        session->instanceChallengeX,
        session->instanceChallengeY,
        out, outCap);
    if (responseLen == 0)
        return 0;

    printf("[info][mock-service] instance_challenge_battle_deliver client=%08x actor=%u enemy=%u age_ticks=%u scene=%s response=4/10 resp=%u evidence=JianghuOL.CBE:0x01012F8E(isolated-module-callback)+mmBattle:0x67AC(non-scene-start)\n",
           session->clientId, session->instanceChallengeActorId,
           session->instanceChallengeEnemyId, ageTicks,
           session->sceneVisibleScene, responseLen);
    session->instanceChallengeBattlePending = false;
    session->instanceChallengeActorId = 0;
    session->instanceChallengeEnemyId = 0;
    session->instanceChallengeX = 0;
    session->instanceChallengeY = 0;
    session->instanceChallengeTick = 0;
    session->instanceChallengeScene[0] = 0;
    return responseLen;
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
    char optionDescriptionStorage[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS][64];
    char dialogTextStorage[256];
    u32 optionValues[VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS];
    u32 serviceValue = 0;
    u32 operation = 0;
    u32 value = 0;
    u8 optionCount = 0;
    u8 instanceChallengeOptionIndex = 0xff;
    u8 dialog[1536];
    u32 dialogLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    bool appendBackpack = false;
    bool appendSkills = false;
    const char *action = "invalid";
    u32 result = 0;
    u32 skillEligibleCount = 0;
    u32 skillLearnedCount = 0;
    u32 skillLevelLockedCount = 0;
    const vm_net_mock_skill_catalog_item *skillNextLocked = NULL;
    const vm_net_mock_scene_npcinfo_seed *instanceSeed = NULL;
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

    if (operation == VM_NET_MOCK_NPC_SERVICE_OPEN_INSTANCE_BASE ||
        operation == VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE ||
        operation == VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE)
    {
        instanceSeed = vm_net_mock_instance_guide_seed(value);
        action = operation == VM_NET_MOCK_NPC_SERVICE_OPEN_INSTANCE_BASE
                     ? "instance-menu"
                     : (operation == VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE
                            ? "instance-enter"
                            : "instance-challenge");
        if (instanceSeed == NULL)
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
                 * using the monster id here lets the client prepare the battle
                 * request/module before it receives the non-scene 4/10 start. */
                optionValues[optionCount] = instanceSeed->challengeEnemyId;
                ++optionCount;
            }
        }
    }

    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_WEAPON)
    {
        static const u8 weaponSelectors[] = {8, 9, 10};

        action = "weapon-categories";
        dialogText =
            "\xc7\xeb\xd1\xa1\xd4\xf1\xce\xe4\xc6\xf7\xc0\xe0\xd0\xcd\xa3\xba"; /* 请选择武器类型： */
        for (u32 i = 0; i < sizeof(weaponSelectors); ++i)
        {
            u32 selector = weaponSelectors[i];
            optionNames[optionCount] =
                vm_net_mock_npc_shop_selector_name(selector);
            optionDescriptions[optionCount] =
                "\xb2\xe9\xbf\xb4\xb8\xc3\xc0\xe0\xc9\xcc\xc6\xb7"; /* 查看该类商品 */
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE | selector;
            ++optionCount;
        }
    }
    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_ARMOR)
    {
        action = "armor-categories";
        dialogText =
            "\xc7\xeb\xd1\xa1\xd4\xf1\xb7\xc0\xbe\xdf\xc0\xe0\xd0\xcd\xa3\xba"; /* 请选择防具类型： */
        for (u32 selector = 1;
             selector <= 7 &&
             optionCount < VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS;
             ++selector)
        {
            optionNames[optionCount] =
                vm_net_mock_npc_shop_selector_name(selector);
            optionDescriptions[optionCount] =
                "\xb2\xe9\xbf\xb4\xb8\xc3\xc0\xe0\xc9\xcc\xc6\xb7"; /* 查看该类商品 */
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE | selector;
            ++optionCount;
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
        bool buyRequest = operation == VM_NET_MOCK_NPC_SERVICE_BUY_ITEM_BASE ||
                          operation == VM_NET_MOCK_NPC_SERVICE_BUY_WEAPON_BASE;
        bool legacyWeaponBuy =
            operation == VM_NET_MOCK_NPC_SERVICE_BUY_WEAPON_BASE;

        action = buyRequest ? (legacyWeaponBuy ? "weapon-buy" : "shop-buy")
                            : "shop-category";
        if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_MEDICINE)
        {
            selector = VM_NET_MOCK_NPC_SERVICE_MEDICINE_SELECTOR;
        }
        else if (operation == VM_NET_MOCK_NPC_SERVICE_OPEN_CATEGORY_BASE)
        {
            selector = value & VM_NET_MOCK_NPC_SERVICE_CATEGORY_MASK;
            page = value >> VM_NET_MOCK_NPC_SERVICE_CATEGORY_PAGE_SHIFT;
        }
        else
        {
            buyItem = vm_net_mock_find_shop_catalog_item(value);
            selector = vm_net_mock_npc_shop_selector_for_item(buyItem);
            if (legacyWeaponBuy && (selector < 8u || selector > 10u))
                selector = 0;
        }

        if (!vm_net_mock_npc_shop_selector_is_valid(selector))
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

        if (buyRequest)
        {
            if (buyItem == NULL ||
                !vm_net_mock_npc_shop_item_matches_selector(buyItem, selector))
            {
                dialogText =
                    "\xb8\xc3\xc9\xcc\xc6\xb7\xd2\xd1\xcf\xc2\xbc\xdc\xa1\xa3"; /* 该商品已下架。 */
            }
            else if (role->money < buyItem->price)
            {
                dialogText =
                    "\xcd\xad\xc7\xae\xb2\xbb\xd7\xe3\xa3\xac\xce\xde\xb7\xa8\xb9\xba\xc2\xf2\xa1\xa3"; /* 铜钱不足，无法购买。 */
            }
            else
            {
                role->money -= buyItem->price;
                if (!vm_net_mock_role_add_backpack_item_to_role(
                        role, buyItem->itemId, 1, NULL,
                        buyItem->isEquip ? "npc-equipment-buy"
                                         : "npc-medicine-buy"))
                {
                    role->money += buyItem->price;
                    dialogText =
                        "\xb1\xb3\xb0\xfc\xd2\xd1\xc2\xfa\xa3\xac\xce\xde\xb7\xa8\xb9\xba\xc2\xf2\xa1\xa3"; /* 背包已满，无法购买。 */
                }
                else
                {
                    dialogText =
                        "\xb9\xba\xc2\xf2\xb3\xc9\xb9\xa6\xa1\xa3"; /* 购买成功。 */
                    appendBackpack = true;
                    result = 1;
                }
            }
            page = 0;
        }

        total = vm_net_mock_npc_shop_selector_total(selector);
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
                vm_net_mock_npc_shop_selector_item_at(selector, ordinal);
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
                     "\xb9\xba\xc2\xf2", item->name, item->price,
                     "\xcd\xad"); /* 购买...铜 */
            snprintf(optionDescriptionStorage[optionCount],
                     sizeof(optionDescriptionStorage[optionCount]),
                     "%s Lv.%u", item->name, levelRequired);
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
        if (optionCount == 0 && !appendBackpack)
            dialogText =
                "\xd4\xdd\xce\xde\xbf\xc9\xb9\xba\xc2\xf2\xb5\xc4\xc9\xcc\xc6\xb7\xa1\xa3"; /* 暂无可购买的商品。 */
    }
    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_REPAIR_ALL)
    {
        u16 repairCount = 0;
        u32 repairCost = 0;

        action = "repair-all";
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
    else if (serviceValue == VM_NET_MOCK_NPC_SERVICE_OPEN_SKILLS ||
             operation == VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE)
    {
        u8 rawJob = vm_net_mock_role_job_to_skill_raw_job(role->job);
        action = operation == VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE
                     ? "skill-learn"
                     : "skill-list";
        serviceState = vm_net_mock_role_service_state_get(role);
        snprintf(dialogTextStorage, sizeof(dialogTextStorage), "%s%u%s",
                 "\xc7\xeb\xd1\xa1\xd4\xf1\xd2\xaa\xd1\xa7\xcf\xb0\xb5\xc4\xbc\xbc\xc4\xdc\xa3\xa8\xb5\xb1\xc7\xb0", /* 请选择要学习的技能（当前 */
                 role->level,
                 "\xbc\xb6\xa3\xa9\xa3\xba"); /* 级）： */
        dialogText = dialogTextStorage;
        if (operation == VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE)
        {
            const vm_net_mock_skill_catalog_item *skill =
                vm_net_mock_find_skill_catalog_item(value);
            if (skill == NULL || skill->rawJob != rawJob)
            {
                dialogText =
                    "\xb7\xfe\xce\xf1\xc7\xeb\xc7\xf3\xce\xde\xd0\xa7\xa1\xa3"; /* 服务请求无效。 */
            }
            else if (skill->levelRequired > role->level)
            {
                snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                         "%s%u%s%s%s%u%s",
                         "\xb5\xb1\xc7\xb0\xb5\xc8\xbc\xb6", /* 当前等级 */
                         role->level,
                         "\xa3\xac", /* ， */
                         skill->name,
                         "\xd0\xe8\xd2\xaa", /* 需要 */
                         skill->levelRequired,
                         "\xbc\xb6\xa3\xac\xce\xde\xb7\xa8\xd1\xa7\xcf\xb0\xa1\xa3"); /* 级，无法学习。 */
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
                         "\xcd\xad\xc7\xae\xb2\xbb\xd7\xe3\xa3\xac\xd0\xe8\xd2\xaa", /* 铜钱不足，需要 */
                         skill->learnPrice,
                         "\xcd\xad\xc7\xae\xa1\xa3"); /* 铜钱。 */
            }
            else if (vm_net_mock_role_service_add_skill(role, skill->skillId))
            {
                role->money -= skill->learnPrice;
                vm_net_mock_role_db_save("npc-skill-learn");
                snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                         "%s%u%s",
                         "\xbc\xbc\xc4\xdc\xd1\xa7\xcf\xb0\xb3\xc9\xb9\xa6\xa3\xac\xcf\xfb\xba\xc4", /* 技能学习成功，消耗 */
                         skill->learnPrice,
                         "\xcd\xad\xc7\xae\xa1\xa3"); /* 铜钱。 */
                appendSkills = true;
                result = 1;
                serviceState = vm_net_mock_role_service_state_get(role);
            }
        }
        for (u32 i = 0; i < vm_net_mock_load_skill_catalog(); ++i)
        {
            const vm_net_mock_skill_catalog_item *skill =
                &g_vm_net_mock_skill_catalog[i];
            if (skill->rawJob != rawJob)
            {
                continue;
            }
            if (vm_net_mock_role_service_has_skill(serviceState,
                                                   skill->skillId))
            {
                ++skillLearnedCount;
                continue;
            }
            if (skill->levelRequired > role->level)
            {
                ++skillLevelLockedCount;
                if (skillNextLocked == NULL)
                    skillNextLocked = skill;
                continue;
            }
            ++skillEligibleCount;
            if (skill->skillId > VM_NET_MOCK_NPC_SERVICE_VALUE_MASK ||
                optionCount >= VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS)
            {
                continue;
            }
            snprintf(optionNameStorage[optionCount],
                     sizeof(optionNameStorage[optionCount]), "%s%s",
                     "\xd1\xa7\xcf\xb0", skill->name); /* 学习... */
            snprintf(optionDescriptionStorage[optionCount],
                     sizeof(optionDescriptionStorage[optionCount]),
                     "%s Lv.%u %u%s", skill->name, skill->levelRequired,
                     skill->learnPrice, "\xcd\xad\xc7\xae"); /* 铜钱 */
            optionNames[optionCount] = optionNameStorage[optionCount];
            optionDescriptions[optionCount] =
                optionDescriptionStorage[optionCount];
            optionValues[optionCount] =
                VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE | skill->skillId;
            ++optionCount;
        }
        if (operation != VM_NET_MOCK_NPC_SERVICE_LEARN_SKILL_BASE &&
            optionCount == 0 && !appendSkills)
        {
            if (skillNextLocked != NULL)
            {
                snprintf(dialogTextStorage, sizeof(dialogTextStorage),
                         "%s%u%s%s%s%u%s%u%s",
                         "\xb5\xb1\xc7\xb0", /* 当前 */
                         role->level,
                         "\xbc\xb6\xa3\xbb\xcf\xc2\xd2\xbb\xbc\xbc\xc4\xdc", /* 级；下一技能 */
                         skillNextLocked->name,
                         "\xd0\xe8\xd2\xaa", /* 需要 */
                         skillNextLocked->levelRequired,
                         "\xbc\xb6\xa3\xac\xd1\xa7\xcf\xb0\xb7\xd1\xd3\xc3", /* 级，学习费用 */
                         skillNextLocked->learnPrice,
                         "\xcd\xad\xc7\xae\xa1\xa3"); /* 铜钱。 */
            }
            else
            {
                dialogText =
                    "\xb5\xb1\xc7\xb0\xc3\xbb\xd3\xd0\xbf\xc9\xd2\xd4\xd1\xa7\xcf\xb0\xb5\xc4\xbc\xbc\xc4\xdc\xa1\xa3"; /* 当前没有可以学习的技能。 */
            }
        }
    }

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
    if (appendBackpack)
    {
        if (!vm_net_mock_append_backpack_items_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    if (appendSkills)
    {
        if (!vm_net_mock_append_role_skills_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    if (instanceChallengeOptionIndex != 0xff && instanceSeed != NULL &&
        session != NULL)
    {
        const char *scene = vm_net_mock_current_scene_name();
        session->instanceChallengeDirectPending = true;
        session->instanceChallengePending = false;
        session->instanceChallengeBattlePending = false;
        session->instanceChallengeActorId = instanceSeed->actorId;
        session->instanceChallengeEnemyId = instanceSeed->challengeEnemyId;
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
    printf("[info][network] mock_npc_service action=%s opcode=%08x value=%u role=%u job=%u level=%u result=%u options=%u money=%u skill_eligible=%u skill_learned=%u skill_level_locked=%u next_skill=%u next_level=%u next_price=%u objects=%u resp=%u evidence=JianghuOL.CBE:0x010492B0(action1)+0x010380E8+skill.dsh\n",
           action, serviceValue, value, role->roleId, role->job, role->level,
           result, optionCount, role->money, skillEligibleCount,
           skillLearnedCount, skillLevelLockedCount,
           skillNextLocked ? skillNextLocked->skillId : 0,
           skillNextLocked ? skillNextLocked->levelRequired : 0,
           skillNextLocked ? skillNextLocked->learnPrice : 0,
           objectCount, pos);
    return pos;
}

static bool vm_net_mock_append_info_banner_result5_object(u8 *out, u32 outCap,
                                                          u32 *pos);
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
    u8 awardInfo[32];
    u32 awardInfoLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 result = 1;
    u8 responseSubtype = 0;
    u8 responseObjectCount = 1;
    bool hasInfoBannerPrefix = false;
    bool hasInfoBannerTail = false;
    bool hasProgressStateTail = false;
    const vm_net_mock_task_definition *taskDefinition = NULL;
    char detailText[256];
    char destinationText[128];
    char promptReceiver[32];
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
    memset(detailText, 0, sizeof(detailText));
    memset(destinationText, 0, sizeof(destinationText));
    memset(promptReceiver, 0, sizeof(promptReceiver));

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
        if (vm_net_mock_task_state_load(activeRole->roleId, taskId, &taskState) &&
            taskState.found && taskState.state == 2 &&
            ((taskDefinition != NULL &&
              vm_net_mock_task_commit_reward(activeRole, taskDefinition)) ||
             (taskDefinition == NULL &&
              vm_net_mock_task_state_store(activeRole->roleId, taskId, 3))))
        {
            committed = true;
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
            u32 rewardExp = taskDefinition ? taskDefinition->rewardExp : 0;
            u32 rewardMoney = taskDefinition ? taskDefinition->rewardMoney : 0;

            if (!vm_net_mock_seq_put_u32(awardInfo, sizeof(awardInfo),
                                         &awardInfoLen, rewardExp) ||
                !vm_net_mock_seq_put_u32(awardInfo, sizeof(awardInfo),
                                         &awardInfoLen, rewardMoney) ||
                !vm_net_mock_seq_put_u8(awardInfo, sizeof(awardInfo),
                                        &awardInfoLen, 0) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "energy", 100) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "energymax", 100) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "exp", totalExp) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "level", activeRole->level) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "lastexp",
                                            vm_net_mock_role_last_level_exp(totalExp)) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "curexp",
                                            vm_net_mock_role_next_level_start_exp(totalExp)) ||
                !vm_net_mock_put_object_u32(out, outCap, &pos, "persentexp",
                                            vm_net_mock_role_exp_percent(totalExp)) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "seqnum", 0) ||
                !vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo", NULL, 0) ||
                !vm_net_mock_put_object_raw(out, outCap, &pos, "awardinfo",
                                            awardInfo, (u16)awardInfoLen) ||
                !vm_net_mock_put_object_raw(out, outCap, &pos, "taskdes",
                                            (const u8 *)submitSuccessText,
                                            (u16)strlen(submitSuccessText)))
            {
                return 0;
            }
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
            u32 allStateCount = 0;
            bool canAccept = false;

            responseSubtype = 11;
            if (taskDefinition != NULL)
            {
                canAccept = vm_net_mock_task_state_list_load(
                                activeRole->roleId, false, allStates,
                                VM_NET_MOCK_TASK_CATALOG_MAX, &allStateCount) &&
                            vm_net_mock_task_definition_available(
                                taskDefinition, activeRole, allStates, allStateCount);
            }
            else
            {
                canAccept = vm_net_mock_task_state_load(activeRole->roleId,
                                                        taskId, &taskState) &&
                            !taskState.found;
            }
            result = canAccept && vm_net_mock_task_accept(activeRole->roleId, taskId)
                         ? 0
                         : 1;
            if (result == 0 && taskDefinition != NULL &&
                !vm_net_mock_task_grant_accept_item(activeRole, taskDefinition))
            {
                (void)vm_net_mock_task_delete(activeRole->roleId, taskId);
                result = 1;
            }
            if (result == 0 &&
                (!vm_net_mock_task_state_load(activeRole->roleId, taskId, &taskState) ||
                 !taskState.found))
            {
                result = 1;
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

static bool vm_net_mock_task_transport_legacy_scene_key(
    const char *scene, char *out, size_t outCap)
{
    return vm_net_mock_scene_resource_legacy_alias(scene, out, outCap);
}

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
                char legacyScene[64];
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
                if (!receiverFound &&
                    vm_net_mock_task_transport_legacy_scene_key(
                        scene, legacyScene, sizeof(legacyScene)))
                {
                    seedCount = vm_net_mock_collect_scene_npcinfo_seeds(
                        legacyScene, seeds, 16, NULL, NULL);
                    for (u32 seedIndex = 0; seedIndex < seedCount; ++seedIndex)
                    {
                        if (task->receiver[0] != 0 &&
                            strcmp(seeds[seedIndex].displayName,
                                   task->receiver) == 0)
                        {
                            receiverFound = true;
                            x = seeds[seedIndex].x;
                            y = seeds[seedIndex].y;
                            break;
                        }
                    }
                    if (!receiverFound &&
                        vm_net_mock_task_transport_scene_contains_npc(
                            legacyScene, task->receiver))
                    {
                        receiverFound = true;
                    }
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
    bool hasTaskId = false;
    bool hasTransId = false;
    bool activeTask = false;
    bool resolved = false;

    if (request == NULL || requestLen < 9 || out == NULL || outCap < pos ||
        request[0] != 'W' || request[1] != 'T' || request[4] != 1 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 16 ||
        object.subtype != 5)
    {
        return 0;
    }
    hasTaskId = vm_net_mock_get_object_u32_field(object.payload,
                                                 object.payloadLen,
                                                 "taskid", &taskId);
    hasTransId = vm_net_mock_get_object_u32_field(object.payload,
                                                  object.payloadLen,
                                                  "transid", &transId);
    if (hasTaskId == hasTransId)
        return 0;

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

