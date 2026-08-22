/*
 * Pixi-only cosmetics: chenghao / wing / mount unlock CSV + equipped ids.
 * Wire: WT 1/31 (CBE ignores). Does not touch designation 23/u8 masks.
 */

enum
{
    VM_NET_MOCK_COSMETIC_KIND_CHENGHAO = 1,
    VM_NET_MOCK_COSMETIC_KIND_WING = 2,
    VM_NET_MOCK_COSMETIC_KIND_MOUNT = 3,
    /* Match VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX — 8 slots dropped peers in towns. */
    VM_NET_MOCK_COSMETIC_NOTICE_MAX = 64
};

static const u32 g_vm_net_mock_chenghao_ids[] = {
    86001, 86002, 86003, 86004, 86005, 86006, 86007, 86008, 86009, 86010,
    86011, 86012, 86013, 86014, 86015, 86016, 86017, 86018, 86019, 86020,
    86021, 86022, 86024, 86025, 86026, 86027, 86028, 86029, 86031, 86032,
    86033, 86034, 86036, 86037, 86038, 86039, 86040, 86041, 86042, 86043,
    86044, 86045, 86046, 86047, 86048
};

static const u32 g_vm_net_mock_wing_ids[] = {
    88010, 88011, 88012, 88014, 88015, 88016, 88017, 88018, 88019, 88020,
    88021, 88022, 88023, 88024, 88025, 88026, 88027, 88028, 88029, 88030,
    88031, 88032, 88033, 88034, 88035, 88036, 90002, 90003, 90006, 90007,
    90010
};

static const u32 g_vm_net_mock_mount_ids[] = {
    11370, 11372, 11802, 16171, 20148, 40183, 75535, 75537, 75549
};

static bool vm_net_mock_cosmetic_id_in_list(u32 id, const u32 *ids, u32 count)
{
    u32 i;
    if (id == 0 || ids == NULL)
        return false;
    for (i = 0; i < count; ++i)
    {
        if (ids[i] == id)
            return true;
    }
    return false;
}

static bool vm_net_mock_is_chenghao_cosmetic_id(u32 id)
{
    return vm_net_mock_cosmetic_id_in_list(
        id, g_vm_net_mock_chenghao_ids,
        (u32)(sizeof(g_vm_net_mock_chenghao_ids) / sizeof(g_vm_net_mock_chenghao_ids[0])));
}

static bool vm_net_mock_is_wing_cosmetic_id(u32 id)
{
    return vm_net_mock_cosmetic_id_in_list(
        id, g_vm_net_mock_wing_ids,
        (u32)(sizeof(g_vm_net_mock_wing_ids) / sizeof(g_vm_net_mock_wing_ids[0])));
}

static bool vm_net_mock_is_mount_cosmetic_id(u32 id)
{
    return vm_net_mock_cosmetic_id_in_list(
        id, g_vm_net_mock_mount_ids,
        (u32)(sizeof(g_vm_net_mock_mount_ids) / sizeof(g_vm_net_mock_mount_ids[0])));
}

static bool vm_net_mock_cosmetic_csv_has(const char *csv, u32 id)
{
    char needle[16];
    const char *p;
    size_t nlen;
    if (csv == NULL || id == 0)
        return false;
    snprintf(needle, sizeof(needle), "%u", id);
    nlen = strlen(needle);
    p = csv;
    while (*p)
    {
        const char *start = p;
        while (*p && *p != ',')
            ++p;
        if ((size_t)(p - start) == nlen && memcmp(start, needle, nlen) == 0)
            return true;
        if (*p == ',')
            ++p;
    }
    return false;
}

static bool vm_net_mock_cosmetic_csv_add(char *csv, size_t csvCap, u32 id)
{
    char token[16];
    size_t cur;
    size_t add;
    if (csv == NULL || csvCap < 2 || id == 0)
        return false;
    if (vm_net_mock_cosmetic_csv_has(csv, id))
        return true;
    snprintf(token, sizeof(token), "%u", id);
    add = strlen(token);
    cur = strlen(csv);
    if (cur == 0)
    {
        if (add + 1 > csvCap)
            return false;
        memcpy(csv, token, add + 1);
        return true;
    }
    if (cur + 1 + add + 1 > csvCap)
        return false;
    csv[cur] = ',';
    memcpy(csv + cur + 1, token, add + 1);
    return true;
}

static bool vm_net_mock_role_try_unlock_chenghao(vm_net_mock_role_state *role,
                                                 u32 id,
                                                 const char *reason)
{
    if (role == NULL || !vm_net_mock_is_chenghao_cosmetic_id(id))
        return false;
    if (vm_net_mock_cosmetic_csv_has(role->unlockedChenghaoIds, id))
        return false;
    if (!vm_net_mock_cosmetic_csv_add(role->unlockedChenghaoIds,
                                      sizeof(role->unlockedChenghaoIds), id))
        return false;
    printf("[info][network] mock_cosmetic_unlock kind=chenghao role=%u id=%u reason=%s\n",
           role->roleId, id, reason ? reason : "unlock");
    vm_net_mock_role_mark_inventory_dirty("cosmetic-unlock-chenghao");
    return true;
}

static bool vm_net_mock_role_try_unlock_wing(vm_net_mock_role_state *role,
                                             u32 id,
                                             const char *reason)
{
    if (role == NULL || !vm_net_mock_is_wing_cosmetic_id(id))
        return false;
    if (vm_net_mock_cosmetic_csv_has(role->unlockedWingIds, id))
        return false;
    if (!vm_net_mock_cosmetic_csv_add(role->unlockedWingIds,
                                      sizeof(role->unlockedWingIds), id))
        return false;
    printf("[info][network] mock_cosmetic_unlock kind=wing role=%u id=%u reason=%s\n",
           role->roleId, id, reason ? reason : "unlock");
    vm_net_mock_role_mark_inventory_dirty("cosmetic-unlock-wing");
    return true;
}

static bool vm_net_mock_role_try_unlock_mount(vm_net_mock_role_state *role,
                                              u32 id,
                                              const char *reason)
{
    if (role == NULL || !vm_net_mock_is_mount_cosmetic_id(id))
        return false;
    if (vm_net_mock_cosmetic_csv_has(role->unlockedMountIds, id))
        return false;
    if (!vm_net_mock_cosmetic_csv_add(role->unlockedMountIds,
                                      sizeof(role->unlockedMountIds), id))
        return false;
    printf("[info][network] mock_cosmetic_unlock kind=mount role=%u id=%u reason=%s\n",
           role->roleId, id, reason ? reason : "unlock");
    vm_net_mock_role_mark_inventory_dirty("cosmetic-unlock-mount");
    return true;
}

static u32 vm_net_mock_role_unlock_all_chenghao(vm_net_mock_role_state *role,
                                                const char *reason)
{
    u32 n = 0;
    u32 i;
    if (role == NULL)
        return 0;
    for (i = 0; i < (u32)(sizeof(g_vm_net_mock_chenghao_ids) /
                           sizeof(g_vm_net_mock_chenghao_ids[0]));
         ++i)
    {
        if (vm_net_mock_role_try_unlock_chenghao(role, g_vm_net_mock_chenghao_ids[i],
                                                 reason))
            ++n;
    }
    return n;
}

static u32 vm_net_mock_role_unlock_all_wings(vm_net_mock_role_state *role,
                                              const char *reason)
{
    u32 n = 0;
    u32 i;
    if (role == NULL)
        return 0;
    for (i = 0; i < (u32)(sizeof(g_vm_net_mock_wing_ids) /
                           sizeof(g_vm_net_mock_wing_ids[0]));
         ++i)
    {
        if (vm_net_mock_role_try_unlock_wing(role, g_vm_net_mock_wing_ids[i], reason))
            ++n;
    }
    return n;
}

static u32 vm_net_mock_role_unlock_all_mounts(vm_net_mock_role_state *role,
                                               const char *reason)
{
    u32 n = 0;
    u32 i;
    if (role == NULL)
        return 0;
    for (i = 0; i < (u32)(sizeof(g_vm_net_mock_mount_ids) /
                           sizeof(g_vm_net_mock_mount_ids[0]));
         ++i)
    {
        if (vm_net_mock_role_try_unlock_mount(role, g_vm_net_mock_mount_ids[i],
                                              reason))
            ++n;
    }
    return n;
}

static void vm_net_mock_role_clear_equipped_chenghao(vm_net_mock_role_state *role,
                                                     const char *reason)
{
    if (role == NULL || role->equippedChenghaoId == 0)
        return;
    printf("[info][network] mock_cosmetic_clear_chenghao role=%u was=%u reason=%s\n",
           role->roleId, role->equippedChenghaoId,
           reason ? reason : "clear");
    role->equippedChenghaoId = 0;
    vm_net_mock_role_mark_inventory_dirty("cosmetic-clear-chenghao");
}

static bool vm_net_mock_role_equip_cosmetic(vm_net_mock_role_state *role,
                                            u8 kind,
                                            u32 id,
                                            const char **errOut)
{
    if (errOut)
        *errOut = NULL;
    if (role == NULL)
    {
        if (errOut)
            *errOut = "no-role";
        return false;
    }
    if (kind == VM_NET_MOCK_COSMETIC_KIND_CHENGHAO)
    {
        if (id != 0 &&
            (!vm_net_mock_is_chenghao_cosmetic_id(id) ||
             !vm_net_mock_cosmetic_csv_has(role->unlockedChenghaoIds, id)))
        {
            if (errOut)
                *errOut = "locked-or-unknown";
            return false;
        }
        role->equippedChenghaoId = id;
        vm_net_mock_role_mark_inventory_dirty("cosmetic-equip-chenghao");
        return true;
    }
    if (kind == VM_NET_MOCK_COSMETIC_KIND_WING)
    {
        if (id != 0 &&
            (!vm_net_mock_is_wing_cosmetic_id(id) ||
             !vm_net_mock_cosmetic_csv_has(role->unlockedWingIds, id)))
        {
            if (errOut)
                *errOut = "locked-or-unknown";
            return false;
        }
        role->equippedWingId = id;
        vm_net_mock_role_mark_inventory_dirty("cosmetic-equip-wing");
        return true;
    }
    if (kind == VM_NET_MOCK_COSMETIC_KIND_MOUNT)
    {
        if (id != 0 &&
            (!vm_net_mock_is_mount_cosmetic_id(id) ||
             !vm_net_mock_cosmetic_csv_has(role->unlockedMountIds, id)))
        {
            if (errOut)
                *errOut = "locked-or-unknown";
            return false;
        }
        role->equippedMountId = id;
        vm_net_mock_role_mark_inventory_dirty("cosmetic-equip-mount");
        return true;
    }
    if (errOut)
        *errOut = "bad-kind";
    return false;
}

static bool vm_net_mock_append_cosmetics_state_fields(u8 *out, u32 outCap, u32 *pos,
                                                      const vm_net_mock_role_state *role)
{
    if (role == NULL)
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "unlockedChenghao",
                                       role->unlockedChenghaoIds[0]
                                           ? role->unlockedChenghaoIds
                                           : ""))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "unlockedWing",
                                       role->unlockedWingIds[0]
                                           ? role->unlockedWingIds
                                           : ""))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "unlockedMount",
                                       role->unlockedMountIds[0]
                                           ? role->unlockedMountIds
                                           : ""))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "eqChenghao",
                                    role->equippedChenghaoId))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "eqWing",
                                    role->equippedWingId))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "eqMount",
                                    role->equippedMountId))
        return false;
    return true;
}

static bool vm_net_mock_append_cosmetics31_2_object(u8 *out, u32 outCap, u32 *pos,
                                                    const vm_net_mock_role_state *role)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 31, 2, &objectStart))
        return false;
    if (!vm_net_mock_append_cosmetics_state_fields(out, outCap, pos, role))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_cosmetics31_4_object(u8 *out, u32 outCap, u32 *pos,
                                                    u8 result,
                                                    const vm_net_mock_role_state *role)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 31, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", result))
        return false;
    if (result == 1 && role != NULL)
    {
        if (!vm_net_mock_append_cosmetics_state_fields(out, outCap, pos, role))
            return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_cosmetics31_5_object(u8 *out, u32 outCap, u32 *pos,
                                                    u32 actorId,
                                                    u32 eqChenghao,
                                                    u32 eqWing,
                                                    u32 eqMount)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 31, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "actorId", actorId))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "eqChenghao", eqChenghao))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "eqWing", eqWing))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "eqMount", eqMount))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_mock_service_session_enqueue_cosmetic_notice(
    vm_mock_service_client_session *target,
    u32 actorId,
    u32 eqChenghao,
    u32 eqWing,
    u32 eqMount)
{
    u8 i;
    u8 freeIdx = 0xff;
    u8 oldestIdx = 0;
    if (target == NULL || actorId == 0)
        return false;
    for (i = 0; i < VM_NET_MOCK_COSMETIC_NOTICE_MAX; ++i)
    {
        if (target->cosmeticNotices[i].valid &&
            target->cosmeticNotices[i].actorId == actorId)
        {
            target->cosmeticNotices[i].eqChenghao = eqChenghao;
            target->cosmeticNotices[i].eqWing = eqWing;
            target->cosmeticNotices[i].eqMount = eqMount;
            return true;
        }
        if (freeIdx == 0xff && !target->cosmeticNotices[i].valid)
            freeIdx = i;
    }
    if (freeIdx == 0xff)
    {
        /* Full: overwrite slot 0 rather than drop a newly visible peer forever. */
        freeIdx = oldestIdx;
        printf("[warn][network] cosmetics31_5 queue full observer=%08x overwrite actor=%u "
               "with actor=%u\n",
               target->clientId, target->cosmeticNotices[freeIdx].actorId, actorId);
    }
    target->cosmeticNotices[freeIdx].valid = true;
    target->cosmeticNotices[freeIdx].actorId = actorId;
    target->cosmeticNotices[freeIdx].eqChenghao = eqChenghao;
    target->cosmeticNotices[freeIdx].eqWing = eqWing;
    target->cosmeticNotices[freeIdx].eqMount = eqMount;
    return true;
}

static void vm_mock_service_broadcast_cosmetics31_5(u32 roleId,
                                                    const char *scene,
                                                    u32 eqChenghao,
                                                    u32 eqWing,
                                                    u32 eqMount,
                                                    u32 excludeClientId)
{
    vm_mock_service_client_session *s;
    vm_mock_service_client_session *source = NULL;
    if (roleId == 0 || scene == NULL || scene[0] == 0)
        return;
    for (s = g_vm_mock_service_client_sessions; s != NULL; s = s->next)
    {
        if (s->roleOnline && s->onlineRoleId == roleId)
        {
            source = s;
            break;
        }
    }
    for (s = g_vm_mock_service_client_sessions; s != NULL; s = s->next)
    {
        u32 wireActorId = roleId;
        if (!s->roleOnline || s->clientId == excludeClientId)
            continue;
        if (!s->onlinePresenceValid || s->onlineRoleId == 0)
            continue;
        if (!vm_net_mock_scene_names_equal_loose(s->onlineScene, scene))
            continue;
        if (source != NULL)
        {
            vm_mock_service_peer_sync *peerSync = vm_mock_service_get_peer_sync(
                s, source->clientId, 0, source->pendingDirQueueSerial, false, NULL);
            if (peerSync != NULL && peerSync->actorId != 0)
                wireActorId = peerSync->actorId;
        }
        (void)vm_mock_service_session_enqueue_cosmetic_notice(
            s, wireActorId, eqChenghao, eqWing, eqMount);
    }
}

static int vm_net_mock_append_pending_cosmetics31_5(u8 *out, u32 outCap, u32 *pos,
                                                    vm_mock_service_client_session *observer,
                                                    u8 maxObjects)
{
    int appended = 0;
    u8 i;
    if (out == NULL || pos == NULL || observer == NULL)
        return -1;
    for (i = 0; i < VM_NET_MOCK_COSMETIC_NOTICE_MAX && appended < maxObjects; ++i)
    {
        if (!observer->cosmeticNotices[i].valid)
            continue;
        if (!vm_net_mock_append_cosmetics31_5_object(
                out, outCap, pos, observer->cosmeticNotices[i].actorId,
                observer->cosmeticNotices[i].eqChenghao,
                observer->cosmeticNotices[i].eqWing,
                observer->cosmeticNotices[i].eqMount))
            return -1;
        memset(&observer->cosmeticNotices[i], 0,
               sizeof(observer->cosmeticNotices[i]));
        ++appended;
    }
    return appended;
}

/* Admin unlock while online: push refreshed 31/2 on next scene-sync poll. */
static int vm_net_mock_append_pending_cosmetics31_2(u8 *out, u32 outCap, u32 *pos,
                                                    vm_mock_service_client_session *observer)
{
    vm_net_mock_role_state *role = NULL;
    u32 i;

    if (out == NULL || pos == NULL || observer == NULL)
        return -1;
    if (!observer->cosmeticsStatePushPending)
        return 0;
    observer->cosmeticsStatePushPending = false;
    if (!g_vm_net_mock_role_db_valid || observer->onlineRoleId == 0)
        return 0;
    for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == observer->onlineRoleId)
        {
            role = &g_vm_net_mock_role_db.roles[i];
            break;
        }
    }
    if (role == NULL)
        return 0;
    if (!vm_net_mock_append_cosmetics31_2_object(out, outCap, pos, role))
        return -1;
    return 1;
}

static void vm_mock_service_session_sync_online_cosmetics(
    vm_mock_service_client_session *session,
    const vm_net_mock_role_state *role);

static bool vm_mock_service_session_has_pending_cosmetics(
    const vm_mock_service_client_session *observer)
{
    u8 i;

    if (observer == NULL)
        return false;
    if (observer->cosmeticsStatePushPending)
        return true;
    for (i = 0; i < VM_NET_MOCK_COSMETIC_NOTICE_MAX; ++i)
    {
        if (observer->cosmeticNotices[i].valid)
            return true;
    }
    return false;
}

static void vm_mock_service_session_enqueue_peer_cosmetics(
    vm_mock_service_client_session *observer,
    const vm_mock_service_client_session *source,
    u32 mapActorId)
{
    u32 actorId = mapActorId;
    u32 eqChenghao;
    u32 eqWing;
    u32 eqMount;

    if (observer == NULL || source == NULL)
        return;
    if (actorId == 0)
        actorId = source->onlineRoleId;
    if (actorId == 0)
        return;
    eqChenghao = source->onlineEqChenghaoId;
    eqWing = source->onlineEqWingId;
    eqMount = source->onlineEqMountId;
    /*
     * Presence cache can lag first visibility (baseline/moveinfo enqueue before
     * the next capture_online_presence).  Empty 31/5 is ignored once by Pixi and
     * never retried until equip change — resolve equipped ids from role DB.
     */
    if (!(eqChenghao || eqWing || eqMount) &&
        source->onlineRoleId != 0 &&
        g_vm_net_mock_role_db_valid)
    {
        u32 i;

        for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];

            if (role->roleId != source->onlineRoleId)
                continue;
            eqChenghao = role->equippedChenghaoId;
            eqWing = role->equippedWingId;
            eqMount = role->equippedMountId;
            {
                vm_mock_service_client_session *mutableSource =
                    vm_mock_service_find_client_session(source->clientId);
                if (mutableSource != NULL)
                    vm_mock_service_session_sync_online_cosmetics(mutableSource,
                                                                 role);
            }
            break;
        }
    }
    /* Do not enqueue all-zero: Pixi ignores the first empty notice forever. */
    if (!(eqChenghao || eqWing || eqMount))
        return;
    (void)vm_mock_service_session_enqueue_cosmetic_notice(
        observer, actorId, eqChenghao, eqWing, eqMount);
}

static void vm_mock_service_session_sync_online_cosmetics(
    vm_mock_service_client_session *session,
    const vm_net_mock_role_state *role)
{
    if (session == NULL || role == NULL)
        return;
    session->onlineEqChenghaoId = role->equippedChenghaoId;
    session->onlineEqWingId = role->equippedWingId;
    session->onlineEqMountId = role->equippedMountId;
}

static bool vm_net_mock_is_cosmetics31_request(const u8 *request, u32 requestLen,
                                                u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 subtype = 0;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 31 ||
        (object.subtype != 1 && object.subtype != 3))
        return false;
    subtype = object.subtype;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (subtypeOut)
        *subtypeOut = subtype;
    return offset == requestLen;
}

static u32 vm_net_mock_build_cosmetics31_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u8 subtype = 0;
    u32 pos = 5;
    u8 objectCount = 0;
    vm_net_mock_role_state *role;
    vm_mock_service_client_session *session;
    u8 kind = 0;
    u32 id = 0;
    const char *err = NULL;

    if (outCap < 16 || !vm_net_mock_is_cosmetics31_request(request, requestLen, &subtype))
        return 0;
    role = vm_net_mock_active_role();
    session = vm_mock_service_get_active_client_session();
    if (role == NULL)
        return 0;

    if (subtype == 1)
    {
        if (!vm_net_mock_append_cosmetics31_2_object(out, outCap, &pos, role))
            return 0;
        objectCount = 1;
        printf("[info][network] mock_cosmetics31_query role=%u eqC=%u eqW=%u eqM=%u\n",
               role->roleId, role->equippedChenghaoId, role->equippedWingId,
               role->equippedMountId);
    }
    else
    {
        u32 offset = 4;
        vm_net_mock_request_object object;
        bool ok;
        if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
            return 0;
        if (!vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "kind",
                                             &kind) ||
            !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id",
                                              &id))
        {
            if (!vm_net_mock_append_cosmetics31_4_object(out, outCap, &pos, 0, NULL))
                return 0;
            objectCount = 1;
        }
        else
        {
            ok = vm_net_mock_role_equip_cosmetic(role, kind, id, &err);
            if (ok)
            {
                (void)vm_net_mock_role_db_save("cosmetic-equip");
                if (session != NULL)
                {
                    vm_mock_service_session_sync_online_cosmetics(session, role);
                    vm_mock_service_session_invalidate_nearby_observer_cursors(
                        session, "cosmetic-equip");
                }
                vm_mock_service_broadcast_cosmetics31_5(
                    role->roleId, role->scene, role->equippedChenghaoId,
                    role->equippedWingId, role->equippedMountId,
                    session ? session->clientId : 0);
            }
            if (!vm_net_mock_append_cosmetics31_4_object(out, outCap, &pos,
                                                         ok ? 1 : 0, ok ? role : NULL))
                return 0;
            objectCount = 1;
            if (ok)
            {
                if (!vm_net_mock_append_cosmetics31_5_object(
                        out, outCap, &pos, role->roleId, role->equippedChenghaoId,
                        role->equippedWingId, role->equippedMountId))
                    return 0;
                objectCount = 2;
            }
            printf("[info][network] mock_cosmetics31_equip role=%u kind=%u id=%u "
                   "ok=%u err=%s\n",
                   role->roleId, kind, id, ok ? 1u : 0u, err ? err : "-");
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static bool vm_mock_service_account_grant_role_cosmetic(
    const char *accountId,
    const char *roleSelector,
    const char *kind,
    u32 id,
    bool unlockAll,
    u32 *grantedOut,
    const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    u32 granted = 0;
    vm_mock_service_client_session *s;

    if (grantedOut)
        *grantedOut = 0;
    if (messageOut)
        *messageOut = "外观解锁失败";
    if (kind == NULL || kind[0] == 0)
    {
        if (messageOut)
            *messageOut = "请选择称号/翅膀/坐骑";
        return false;
    }
    state = vm_mock_service_open_account_role_db_for_management(accountId,
                                                                 messageOut);
    if (state == NULL)
        return false;
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut)
            *messageOut = "角色不存在";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    if (strcmp(kind, "chenghao") == 0)
    {
        if (unlockAll)
            granted = vm_net_mock_role_unlock_all_chenghao(role, "admin-grant-all");
        else if (vm_net_mock_role_try_unlock_chenghao(role, id, "admin-grant"))
            granted = 1;
    }
    else if (strcmp(kind, "wing") == 0)
    {
        if (unlockAll)
            granted = vm_net_mock_role_unlock_all_wings(role, "admin-grant-all");
        else if (vm_net_mock_role_try_unlock_wing(role, id, "admin-grant"))
            granted = 1;
    }
    else if (strcmp(kind, "mount") == 0)
    {
        if (unlockAll)
            granted = vm_net_mock_role_unlock_all_mounts(role, "admin-grant-all");
        else if (vm_net_mock_role_try_unlock_mount(role, id, "admin-grant"))
            granted = 1;
    }
    else
    {
        if (messageOut)
            *messageOut = "未知外观类型";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    if (granted == 0)
    {
        if (messageOut)
            *messageOut = unlockAll ? "无需解锁（已全部拥有或目录为空）"
                                    : "解锁失败（未知 id 或已拥有）";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    if (!vm_net_mock_role_db_save("admin-cosmetic-grant"))
    {
        if (messageOut)
            *messageOut = "外观解锁持久化失败";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    vm_mock_service_account_capture(state);
    for (s = g_vm_mock_service_client_sessions; s != NULL; s = s->next)
    {
        if (s->roleOnline && s->onlineRoleId == role->roleId)
            s->cosmeticsStatePushPending = true;
    }
    printf("[info][mock-service] account_cosmetic_grant user=%s role=%u kind=%s "
           "id=%u all=%u granted=%u\n",
           accountId, role->roleId, kind, id, unlockAll ? 1u : 0u, granted);
    if (grantedOut)
        *grantedOut = granted;
    if (messageOut)
        *messageOut = "外观解锁成功";
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}
