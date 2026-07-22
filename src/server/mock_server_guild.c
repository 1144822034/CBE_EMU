static bool vm_net_mock_append_scene_room_npc_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 roomList[128];
    u8 npcInfo[1024];
    u8 colNames[64];
    u32 roomListLen = 0;
    u32 npcInfoLen = 0;
    u32 colNamesLen = 0;
    u8 npcNum = 0;
    static const char *const roomColumns[] = {"id", "room", "scene", "state"};
    const char *scene = vm_net_mock_current_scene_name();
    bool seedSceneNpcInfo = false;
    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames), &colNamesLen, roomColumns, 4))
        return false;
    if (!vm_net_mock_seq_put_u32(roomList, sizeof(roomList), &roomListLen, 1001))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "1001"))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "Penglai"))
        return false;
    if (!vm_net_mock_seq_put_string(roomList, sizeof(roomList), &roomListLen, "Scene"))
        return false;
    if (!vm_net_mock_build_scene_npcinfo_blob(scene, npcInfo, sizeof(npcInfo), &npcNum, &npcInfoLen))
        return false;
    printf("[info][network] vm_net_mock_append_scene_room_npc_object: scene=%s npcNum=%u npcInfoLen=%u\n",
           scene ? scene : "NULL", (u32)npcNum, (u32)npcInfoLen);
    seedSceneNpcInfo = npcNum > 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 3, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "curpage", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "pagenum", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames, (u16)colNamesLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "roomnum", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "roomlist", roomList, (u16)roomListLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", npcNum))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "npcinfo", npcInfo, (u16)npcInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

enum
{
    VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX = 3
};

typedef struct
{
    u32 actorId;
    u16 x;
    u16 y;
    u8 job;
    u8 sex;
    u16 level;
    u32 hp;
    u32 hpMax;
    u32 mp;
    u32 mpMax;
    const char *roleName;
    const char *titleText;
    const char *titleBadge;
    const char *stateText;
    vm_mock_service_client_session *session;
} vm_net_mock_scene_role_seed;

static const char *vm_net_mock_role_job_name_ascii(u8 job)
{
    switch (job)
    {
    case 2:
        return "Assassin";
    case 3:
        return "Mage";
    default:
        return "Warrior";
    }
}

static u16 vm_net_mock_offset_coord(u16 base, int delta)
{
    int value = (int)base + delta;
    if (value < 16)
        value = 16;
    if (value > 0x7fff)
        value = 0x7fff;
    return (u16)value;
}

static bool vm_net_mock_scene_role_actor_id_in_use(const vm_net_mock_scene_role_seed *seeds,
                                                   u8 count,
                                                   u32 actorId)
{
    if (seeds == NULL || actorId == 0)
        return false;
    for (u8 i = 0; i < count; ++i)
    {
        if (seeds[i].actorId == actorId)
            return true;
    }
    return false;
}

static bool vm_net_mock_get_object_entry_field(const u8 *request, u32 requestLen, const char *field,
                                               const u8 **value, u16 *valueLen)
{
    u32 fieldLen = (u32)strlen(field);
    if (value)
        *value = NULL;
    if (valueLen)
        *valueLen = 0;
    if (fieldLen == 0 || fieldLen > 0xff || requestLen < fieldLen + 3)
        return false;

    for (u32 i = 0; i + fieldLen + 3 <= requestLen; ++i)
    {
        if (request[i] != (u8)fieldLen)
            continue;
        if (memcmp(request + i + 1, field, fieldLen) != 0)
            continue;
        {
            u32 p = i + 1 + fieldLen;
            if (p < requestLen && request[p] == 0)
                p++;
            if (p + 2 > requestLen)
                return false;
            {
                u16 rawLen = (u16)(((u16)request[p] << 8) | request[p + 1]);
                p += 2;
                if (p + rawLen > requestLen)
                    return false;
                if (value)
                    *value = request + p;
                if (valueLen)
                    *valueLen = rawLen;
                return true;
            }
        }
    }
    return false;
}

static u32 vm_net_mock_scene_role_actor_id_for_session(const vm_mock_service_client_session *session,
                                                       const vm_net_mock_scene_role_seed *seeds,
                                                       u8 count,
                                                       const vm_net_mock_role_state *activeRole)
{
    u32 actorId = 0;
    if (session == NULL)
        return 0;
    actorId = session->onlineRoleId;
    if (actorId != 0 &&
        (activeRole == NULL || actorId != activeRole->roleId) &&
        !vm_net_mock_scene_role_actor_id_in_use(seeds, count, actorId))
    {
        return actorId;
    }
    actorId = 0x6A000000u | (session->clientId & 0x00FFFFFFu);
    while (actorId == 0 ||
           (activeRole != NULL && actorId == activeRole->roleId) ||
           vm_net_mock_scene_role_actor_id_in_use(seeds, count, actorId))
    {
        ++actorId;
    }
    return actorId;
}

static u8 vm_net_mock_build_scene_role_seeds(const char *scene,
                                             vm_net_mock_scene_role_seed *seeds,
                                             u8 seedCap)
{
    static const struct
    {
        int dx;
        int dy;
    } offsets[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX] = {
        { 32, 0 },
        { -40, 24 },
        { 20, -36 },
    };
    u8 count = 0;
    u16 baseX = vm_net_mock_scene_spawn_x();
    u16 baseY = vm_net_mock_scene_spawn_y();
    vm_net_mock_role_state *activeRole = vm_net_mock_active_role();
    vm_mock_service_client_session *session = NULL;

    if (seeds == NULL || seedCap == 0)
        return 0;
    memset(seeds, 0, sizeof(*seeds) * seedCap);
    if (activeRole != NULL)
    {
        if (activeRole->x != 0)
            baseX = activeRole->x;
        if (activeRole->y != 0)
            baseY = activeRole->y;
    }
    if (!vm_net_mock_scene_name_is_safe(scene))
        return 0;

    session = g_vm_mock_service_client_sessions;
    while (session != NULL && count < seedCap)
    {
        if (session->clientId != 0 &&
            session->clientId != g_vm_mock_service_active_client_id &&
            vm_mock_service_session_scene_is_visible(session, scene))
        {
            seeds[count].actorId = vm_net_mock_scene_role_actor_id_for_session(session,
                                                                               seeds,
                                                                               count,
                                                                               activeRole);
            seeds[count].x = session->sceneVisibleX ? session->sceneVisibleX :
                             vm_net_mock_offset_coord(baseX, offsets[count].dx);
            seeds[count].y = session->sceneVisibleY ? session->sceneVisibleY :
                             vm_net_mock_offset_coord(baseY, offsets[count].dy);
            seeds[count].job = session->onlineJob ? session->onlineJob : 1;
            seeds[count].sex = session->onlineSex <= 1 ? session->onlineSex : 0;
            seeds[count].level = session->onlineLevel ? session->onlineLevel : 1;
            seeds[count].hp = session->onlineHp;
            seeds[count].hpMax = session->onlineHpMax;
            seeds[count].mp = session->onlineMp;
            seeds[count].mpMax = session->onlineMpMax;
            seeds[count].roleName = session->onlineRoleName[0] ? session->onlineRoleName : "Player";
            seeds[count].titleText = session->onlineRoleTitle[0] ? session->onlineRoleTitle : "";
            seeds[count].titleBadge = session->onlineRoleTitleBadge[0] ? session->onlineRoleTitleBadge : "";
            seeds[count].stateText = "Online";
            seeds[count].session = session;
            ++count;
        }
        session = session->next;
    }
    return count;
}

/*
 * The 2/7 scene-control page has its own otherinfo table.  Its row layout is
 * exactly the reader sequence in HandleFactionOtherInfoResponse(0x01031162):
 * id, two u8 values, display name, two u8 values, two strings, and two i16
 * coordinates.  The page only retains the id, first two bytes and display
 * name today, but serializing every consumed field keeps the stream aligned.
 */
static bool vm_net_mock_build_scene_list_otherinfo_blob(const char *scene,
                                                        u8 *otherInfo,
                                                        u32 otherInfoCap,
                                                        u32 *otherInfoLenOut,
                                                        u32 *roleCountOut)
{
    vm_net_mock_scene_role_seed seeds[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX];
    u8 roleCount = 0;
    u32 otherInfoLen = 0;

    if (otherInfoLenOut)
        *otherInfoLenOut = 0;
    if (roleCountOut)
        *roleCountOut = 0;
    if (otherInfo == NULL || otherInfoCap == 0)
        return false;

    roleCount = vm_net_mock_build_scene_role_seeds(scene, seeds,
                                                    VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX);
    for (u8 i = 0; i < roleCount; ++i)
    {
        const vm_net_mock_scene_role_seed *seed = &seeds[i];

        if (!vm_net_mock_seq_put_u32(otherInfo, otherInfoCap, &otherInfoLen, seed->actorId) ||
            !vm_net_mock_seq_put_u8(otherInfo, otherInfoCap, &otherInfoLen,
                                    seed->job > 0 ? (u8)(seed->job - 1) : 0) ||
            !vm_net_mock_seq_put_u8(otherInfo, otherInfoCap, &otherInfoLen,
                                    (u8)(seed->sex + 1)) ||
            !vm_net_mock_seq_put_string(otherInfo, otherInfoCap, &otherInfoLen,
                                        seed->roleName ? seed->roleName : "Player") ||
            !vm_net_mock_seq_put_u8(otherInfo, otherInfoCap, &otherInfoLen,
                                    (u8)(seed->x / 16u)) ||
            !vm_net_mock_seq_put_u8(otherInfo, otherInfoCap, &otherInfoLen,
                                    (u8)(seed->y / 16u)) ||
            !vm_net_mock_seq_put_string(otherInfo, otherInfoCap, &otherInfoLen,
                                        seed->titleText ? seed->titleText : "") ||
            !vm_net_mock_seq_put_string(otherInfo, otherInfoCap, &otherInfoLen,
                                        seed->titleBadge ? seed->titleBadge : "") ||
            !vm_net_mock_seq_put_i16(otherInfo, otherInfoCap, &otherInfoLen, seed->x) ||
            !vm_net_mock_seq_put_i16(otherInfo, otherInfoCap, &otherInfoLen, seed->y))
        {
            return false;
        }
    }
    if (otherInfoLenOut)
        *otherInfoLenOut = otherInfoLen;
    if (roleCountOut)
        *roleCountOut = roleCount;
    return true;
}

static bool vm_net_mock_build_scene_room_roles_blob(const char *scene,
                                                    u8 *rolesInfo,
                                                    u32 rolesInfoCap,
                                                    u32 *rolesInfoLenOut,
                                                    u32 *roleNumOut)
{
    vm_net_mock_scene_role_seed seeds[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX];
    u8 roleCount = 0;
    u32 rolesInfoLen = 0;

    if (rolesInfoLenOut)
        *rolesInfoLenOut = 0;
    if (roleNumOut)
        *roleNumOut = 0;
    if (rolesInfo == NULL || rolesInfoCap == 0)
        return false;

    roleCount = vm_net_mock_build_scene_role_seeds(scene, seeds, VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX);
    for (u8 i = 0; i < roleCount; ++i)
    {
        char levelText[16];
        snprintf(levelText, sizeof(levelText), "Lv%u", seeds[i].level ? seeds[i].level : 1);
        if (!vm_net_mock_seq_put_string(rolesInfo, rolesInfoCap, &rolesInfoLen, seeds[i].roleName) ||
            !vm_net_mock_seq_put_string(rolesInfo, rolesInfoCap, &rolesInfoLen,
                                        vm_net_mock_role_job_name_ascii(seeds[i].job)) ||
            !vm_net_mock_seq_put_string(rolesInfo, rolesInfoCap, &rolesInfoLen, levelText) ||
            !vm_net_mock_seq_put_string(rolesInfo, rolesInfoCap, &rolesInfoLen,
                                        seeds[i].stateText ? seeds[i].stateText : "Nearby"))
        {
            return false;
        }
    }
    if (rolesInfoLenOut)
        *rolesInfoLenOut = rolesInfoLen;
    if (roleNumOut)
        *roleNumOut = roleCount;
    return true;
}

static bool vm_net_mock_append_scene_room_roles_object(u8 *out, u32 outCap, u32 *pos, u32 *roleNumOut)
{
    u32 objectStart = 0;
    u8 rolesInfo[512];
    u8 colNames[64];
    u32 rolesInfoLen = 0;
    u32 colNamesLen = 0;
    u32 roleNum = 0;
    const char *scene = vm_net_mock_current_scene_name();
    static const char *const roleColumns[] = {"name", "job", "level", "state"};
    if (roleNumOut)
        *roleNumOut = 0;
    if (!vm_net_mock_seq_put_string_list(colNames, sizeof(colNames), &colNamesLen, roleColumns, 4))
        return false;
    if (!vm_net_mock_build_scene_room_roles_blob(scene,
                                                 rolesInfo,
                                                 sizeof(rolesInfo),
                                                 &rolesInfoLen,
                                                 &roleNum))
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x1e, 7, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "roomid", 1001))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "colnum", 4))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "colnames", colNames, (u16)colNamesLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "rolenum", (u8)roleNum))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "rolesinfo", rolesInfo, (u16)rolesInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (roleNumOut)
        *roleNumOut = roleNum;
    return true;
}

static vm_mock_service_client_session *vm_mock_service_find_online_friend_session(
    const vm_mock_service_friend_record *record)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;

    if (record == NULL)
        return NULL;
    while (session != NULL)
    {
        if (session->roleOnline && session->onlinePresenceValid &&
            vm_mock_service_session_presence_is_recent(session) &&
            session->onlineRoleId == record->targetRoleId &&
            strcmp(session->accountId, record->targetAccountId) == 0)
        {
            return session;
        }
        session = session->next;
    }
    return NULL;
}

static bool vm_net_mock_find_nearby_role_seed_by_actor_id(
    const char *scene, u32 actorId, vm_net_mock_scene_role_seed *seedOut)
{
    vm_net_mock_scene_role_seed seeds[VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX];
    u8 seedCount = 0;

    if (seedOut)
        memset(seedOut, 0, sizeof(*seedOut));
    if (actorId == 0 || !vm_net_mock_scene_name_is_safe(scene))
        return false;
    seedCount = vm_net_mock_build_scene_role_seeds(scene, seeds,
                                                   VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX);
    for (u8 i = 0; i < seedCount; ++i)
    {
        if (seeds[i].actorId == actorId && seeds[i].session != NULL)
        {
            if (seedOut)
                *seedOut = seeds[i];
            return true;
        }
    }
    return false;
}

/* A friend-list row uses the persistent role id, rather than a temporary
 * scene-node index.  29/4 is available from that list even if the target is
 * no longer in the caller's nearby seed set, so resolve only a confirmed
 * friend row as the second, still-narrow target scope. */
static bool vm_net_mock_find_friend_role_seed_by_role_id(
    u32 roleId, vm_net_mock_scene_role_seed *seedOut)
{
    vm_net_mock_role_state *ownerRole = vm_net_mock_active_role();
    const char *ownerAccountId = g_vm_mock_service_active_account_id;

    if (seedOut)
        memset(seedOut, 0, sizeof(*seedOut));
    if (roleId == 0 || ownerRole == NULL || ownerAccountId == NULL ||
        ownerAccountId[0] == 0)
    {
        return false;
    }

    vm_mock_service_friend_db_load();
    if (!g_vm_mock_service_friend_db_valid)
        return false;
    for (u32 i = 0; i < g_vm_mock_service_friend_db.recordCount; ++i)
    {
        const vm_mock_service_friend_record *record = &g_vm_mock_service_friend_db.records[i];
        vm_mock_service_client_session *onlineSession = NULL;
        vm_net_mock_scene_role_seed seed;

        if (record->ownerRoleId != ownerRole->roleId ||
            record->targetRoleId != roleId ||
            strcmp(record->ownerAccountId, ownerAccountId) != 0)
        {
            continue;
        }
        memset(&seed, 0, sizeof(seed));
        onlineSession = vm_mock_service_find_online_friend_session(record);
        seed.actorId = record->targetRoleId;
        seed.x = onlineSession ? onlineSession->onlineX : 0;
        seed.y = onlineSession ? onlineSession->onlineY : 0;
        seed.job = onlineSession && onlineSession->onlineJob ?
                   onlineSession->onlineJob : (record->targetJob ? record->targetJob : 1);
        seed.sex = onlineSession && onlineSession->onlineSex <= 1 ?
                   onlineSession->onlineSex : (record->targetSex <= 1 ? record->targetSex : 0);
        seed.level = onlineSession && onlineSession->onlineLevel ?
                     onlineSession->onlineLevel : (u16)(record->targetLevel ? record->targetLevel : 1);
        seed.hp = onlineSession ? onlineSession->onlineHp : 0;
        seed.hpMax = onlineSession ? onlineSession->onlineHpMax : 0;
        seed.mp = onlineSession ? onlineSession->onlineMp : 0;
        seed.mpMax = onlineSession ? onlineSession->onlineMpMax : 0;
        seed.roleName = onlineSession && onlineSession->onlineRoleName[0] ?
                        onlineSession->onlineRoleName :
                        (record->targetRoleName[0] ? record->targetRoleName : "Player");
        seed.stateText = onlineSession ? "Online" : "Offline";
        seed.session = onlineSession;
        if (seedOut)
            *seedOut = seed;
        return true;
    }
    return false;
}

static bool vm_net_mock_open_server_data_resource(const char *name,
                                                  const char *requiredSuffix,
                                                  FILE **fpOut,
                                                  char *pathOut,
                                                  size_t pathOutCap)
{
    static const char *pathFormats[] = {
        "../web/fs/JHOnlineData/%s",
        "web/fs/JHOnlineData/%s"
    };
    char candidate[1200];

    if (fpOut)
        *fpOut = NULL;
    if (pathOut && pathOutCap != 0)
        pathOut[0] = 0;
    if (name == NULL || name[0] == 0 ||
        vm_net_mock_scene_name_has_path_separator(name) ||
        (requiredSuffix != NULL && requiredSuffix[0] != 0 &&
         !vm_net_mock_str_ends_with(name, requiredSuffix)))
    {
        return false;
    }

    if (g_vm_net_mock_resource_dir[0] != 0 &&
        vm_net_mock_build_configured_resource_path(name, candidate,
                                                   sizeof(candidate)))
    {
        FILE *fp = vm_net_mock_fopen_game_path(candidate, "rb");
        if (fp != NULL)
        {
            if (pathOut && pathOutCap != 0)
                snprintf(pathOut, pathOutCap, "%s", candidate);
            if (fpOut)
                *fpOut = fp;
            else
                fclose(fp);
            return true;
        }
    }

    for (u32 i = 0; i < sizeof(pathFormats) / sizeof(pathFormats[0]); ++i)
    {
        snprintf(candidate, sizeof(candidate), pathFormats[i], name);
        FILE *fp = vm_net_mock_fopen_game_path(candidate, "rb");
        if (fp == NULL)
            continue;
        if (pathOut && pathOutCap != 0)
            snprintf(pathOut, pathOutCap, "%s", candidate);
        if (fpOut)
            *fpOut = fp;
        else
            fclose(fp);
        return true;
    }
    return false;
}

/* Recruitment and friend-list actions use a persistent role id, not
 * necessarily an actor id from the observer's current scene.  In particular,
 * the guild recruitment page may show the current role or an online role from
 * another scene.  Keep this resolver separate from the nearby resolver so a
 * scene-node id is never silently treated as a global role id. */
static bool vm_net_mock_find_online_role_seed_by_role_id(
    u32 roleId, vm_net_mock_scene_role_seed *seedOut)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;

    if (seedOut)
        memset(seedOut, 0, sizeof(*seedOut));
    if (roleId == 0)
        return false;

    while (session != NULL)
    {
        if (session->roleOnline && session->onlinePresenceValid &&
            vm_mock_service_session_presence_is_recent(session) &&
            session->onlineRoleId == roleId)
        {
            if (seedOut)
            {
                seedOut->actorId = roleId;
                seedOut->x = session->onlineX;
                seedOut->y = session->onlineY;
                seedOut->job = session->onlineJob ? session->onlineJob : 1;
                seedOut->sex = session->onlineSex <= 1 ? session->onlineSex : 0;
                seedOut->level = session->onlineLevel ? session->onlineLevel : 1;
                seedOut->hp = session->onlineHp;
                seedOut->hpMax = session->onlineHpMax;
                seedOut->mp = session->onlineMp;
                seedOut->mpMax = session->onlineMpMax;
                seedOut->roleName = session->onlineRoleName[0] ?
                                    session->onlineRoleName : "Player";
                seedOut->titleText = session->onlineRoleTitle[0] ?
                                     session->onlineRoleTitle : "";
                seedOut->titleBadge = session->onlineRoleTitleBadge[0] ?
                                      session->onlineRoleTitleBadge : "";
                seedOut->stateText = "Online";
                seedOut->session = session;
            }
            return true;
        }
        session = session->next;
    }
    return false;
}

static bool vm_net_mock_find_player_info_seed_by_actor_id(
    const char *scene, u32 actorId, vm_net_mock_scene_role_seed *seedOut,
    const char **scopeOut)
{
    vm_net_mock_role_state *activeRole = NULL;
    vm_mock_service_client_session *activeSession = NULL;

    if (scopeOut)
        *scopeOut = "unresolved";
    if (vm_net_mock_find_nearby_role_seed_by_actor_id(scene, actorId, seedOut))
    {
        if (scopeOut)
            *scopeOut = "nearby";
        return true;
    }
    if (vm_net_mock_find_friend_role_seed_by_role_id(actorId, seedOut))
    {
        if (scopeOut)
            *scopeOut = "friend";
        return true;
    }
    if (vm_net_mock_find_online_role_seed_by_role_id(actorId, seedOut))
    {
        if (scopeOut)
            *scopeOut = "online-role";
        return true;
    }

    /* The service can receive a recruitment-profile request before presence
     * has been refreshed.  Resolve the active persisted role as the final
     * narrow scope; this is also the row emitted by the legacy 10/32 role-list
     * response. */
    activeRole = vm_net_mock_active_role();
    if (activeRole == NULL || activeRole->roleId != actorId)
        return false;
    activeSession = vm_mock_service_get_active_client_session();
    if (seedOut)
    {
        memset(seedOut, 0, sizeof(*seedOut));
        seedOut->actorId = activeRole->roleId;
        seedOut->x = activeRole->x;
        seedOut->y = activeRole->y;
        seedOut->job = activeRole->job ? activeRole->job : 1;
        seedOut->sex = activeRole->sex <= 1 ? activeRole->sex : 0;
        seedOut->level = activeRole->level ? (u16)activeRole->level : 1;
        seedOut->hp = activeRole->hp;
        seedOut->hpMax = activeRole->hpMax;
        seedOut->mp = activeRole->mp;
        seedOut->mpMax = activeRole->mpMax;
        seedOut->roleName = activeRole->name[0] ? activeRole->name : "Player";
        seedOut->stateText = "Online";
        seedOut->session = activeSession;
    }
    if (scopeOut)
        *scopeOut = "active-role";
    return true;
}

/*
 * The surrounding-player action menu is a set of independent client requests,
 * not variants of 2/7.  The sender functions are:
 *
 *   10/2  { id, type=2 }      player/faction information
 *   29/4  { id }              equipment view
 *   10/13 { id, fid }         faction invitation
 *   21/1  { id }              trade request
 *   5/1   { id }              team invitation
 *   4/14  { id }              sparring request
 *   10/3  { id }              friend invitation (handled below)
 *
 * Evidence: JianghuOL.CBE Init/HandleFriendListInput at 0x0103023C and
 * SendShopType2Event/SendBattleEnterReq/SendSkillUseReq/EnterSceneTransition/
 * CheckBattleJoinCondition/ShowSceneActionConfirm at 0x0101A5AA,
 * 0x0101A2B6, 0x0101A3C8, 0x0101C408, 0x0101BEF4, and 0x0102E624.
 */
static bool vm_net_mock_is_nearby_actor_action_request(const u8 *request, u32 requestLen,
                                                        u8 kind, u8 subtype,
                                                        const char *requiredField,
                                                        bool requireType, u8 typeValue,
                                                        u32 *actorIdOut)
{
    u32 offset = 4;
    u32 actorId = 0;
    u8 requestType = 0;
    const u8 *requiredValue = NULL;
    u16 requiredValueLen = 0;
    vm_net_mock_request_object object;

    if (actorIdOut)
        *actorIdOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen ||
        object.major != 1 || object.kind != kind || object.subtype != subtype ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &actorId) ||
        actorId == 0)
    {
        return false;
    }
    if (requiredField != NULL &&
        !vm_net_mock_get_object_entry_field(object.payload, object.payloadLen, requiredField,
                                            &requiredValue, &requiredValueLen))
    {
        return false;
    }
    if (requireType &&
        (!vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &requestType) ||
         requestType != typeValue))
    {
        return false;
    }
    if (actorIdOut)
        *actorIdOut = actorId;
    return true;
}

static bool vm_net_mock_is_nearby_player_info_request(const u8 *request, u32 requestLen,
                                                       u32 *actorIdOut)
{
    return vm_net_mock_is_nearby_actor_action_request(request, requestLen, 10, 2,
                                                       NULL, true, 2, actorIdOut);
}

static bool vm_net_mock_is_nearby_equip_view_request(const u8 *request, u32 requestLen,
                                                      u32 *actorIdOut)
{
    return vm_net_mock_is_nearby_actor_action_request(request, requestLen, 29, 4,
                                                       NULL, false, 0, actorIdOut);
}

static bool vm_net_mock_is_nearby_guild_invite_request(const u8 *request, u32 requestLen,
                                                        u32 *actorIdOut)
{
    return vm_net_mock_is_nearby_actor_action_request(request, requestLen, 10, 13,
                                                       "fid", false, 0, actorIdOut);
}

static bool vm_net_mock_is_nearby_trade_request(const u8 *request, u32 requestLen,
                                                 u32 *actorIdOut)
{
    return vm_net_mock_is_nearby_actor_action_request(request, requestLen, 21, 1,
                                                       NULL, false, 0, actorIdOut);
}

static bool vm_net_mock_is_nearby_team_invite_request(const u8 *request, u32 requestLen,
                                                       u32 *actorIdOut)
{
    return vm_net_mock_is_nearby_actor_action_request(request, requestLen, 5, 1,
                                                       NULL, false, 0, actorIdOut);
}

static bool vm_net_mock_is_nearby_spar_request(const u8 *request, u32 requestLen,
                                                u32 *actorIdOut)
{
    return vm_net_mock_is_nearby_actor_action_request(request, requestLen, 4, 14,
                                                       NULL, false, 0, actorIdOut);
}

static u32 vm_net_mock_build_nearby_player_info_response(const u8 *request, u32 requestLen,
                                                          u8 *out, u32 outCap)
{
    vm_net_mock_scene_role_seed targetSeed;
    u8 playerInfo[256];
    char nameLine[96];
    char levelJobLine[128];
    char factionSexLine[128];
    u32 playerInfoLen = 0;
    u32 objectStart = 0;
    u32 actorId = 0;
    u32 pos = 5;
    const char *scene = vm_net_mock_current_scene_name();
    const char *displayName = NULL;
    const char *jobName = NULL;
    const char *sexName = NULL;
    const char *guildName = "\xce\xde\xb0\xef\xc5\xc9"; /* GBK: 无帮派 */
    const char *targetScope = "unresolved";
    vm_net_mock_guild_record guild;

    memset(&targetSeed, 0, sizeof(targetSeed));
    memset(playerInfo, 0, sizeof(playerInfo));
    memset(nameLine, 0, sizeof(nameLine));
    memset(levelJobLine, 0, sizeof(levelJobLine));
    memset(factionSexLine, 0, sizeof(factionSexLine));
    memset(&guild, 0, sizeof(guild));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_nearby_player_info_request(request, requestLen, &actorId))
    {
        return 0;
    }

    /* Send a parser-matching failure object even if the selected role has gone
     * offline.  HandleFactionDegreeResponse(10/2) clears the action dialog's
     * pending flag before it examines result; falling through to the generic
     * type=2 handler changes the response to 10/20 and leaves the progress bar
     * running forever. */
    if (!vm_net_mock_find_player_info_seed_by_actor_id(scene, actorId, &targetSeed,
                                                       &targetScope))
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 2, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 0))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
        printf("[warn][network] mock_player_info actor=%u scope=unresolved "
               "response=10/2 result=0 resp=%u evidence=JianghuOL.CBE:0x010211A8\n",
               actorId, pos);
        return pos;
    }

    /* The CBE renders the strings as GBK.  Keep fixed labels as explicit GBK
     * bytes so they remain Chinese regardless of the host compiler's source
     * encoding; roleName itself is already the role DB's GBK display name. */
    displayName = targetSeed.roleName && targetSeed.roleName[0] &&
                  strcmp(targetSeed.roleName, "Player") != 0 ?
                  targetSeed.roleName : "\xcf\xc0\xbf\xcd"; /* GBK: 侠客 */
    switch (targetSeed.job)
    {
    case 2:
        jobName = "\xb4\xcc\xbf\xcd"; /* GBK: 刺客 */
        break;
    case 3:
        jobName = "\xb7\xa8\xca\xa6"; /* GBK: 法师 */
        break;
    case 1:
    default:
        jobName = "\xd5\xbd\xca\xbf"; /* GBK: 战士 */
        break;
    }
    sexName = targetSeed.sex == 1 ? "\xc5\xae" : "\xc4\xd0"; /* GBK: 女 / 男 */
    if (targetSeed.session != NULL &&
        vm_net_mock_guild_find_membership_for_account(targetSeed.session->accountId,
                                                       targetSeed.actorId,
                                                       &guild, NULL) &&
        guild.guildName[0] != 0)
    {
        guildName = guild.guildName;
    }
    snprintf(nameLine, sizeof(nameLine), "\xd0\xd5\xc3\xfb\xa3\xba%s", displayName); /* 姓名： */
    snprintf(levelJobLine, sizeof(levelJobLine),
             "\xb5\xc8\xbc\xb6\xa3\xba%u\xbc\xb6\xa1\xa1\xd6\xb0\xd2\xb5\xa3\xba%s",
             targetSeed.level ? targetSeed.level : 1, jobName); /* 等级：<n>级　职业： */
    snprintf(factionSexLine, sizeof(factionSexLine),
             "\xb0\xef\xc5\xc9\xa3\xba%s\xa1\xa1\xd0\xd4\xb1\xf0\xa3\xba%s",
             guildName, sexName); /* 帮派：<name>　性别： */
    /* HandleFactionDegreeResponse reads each playerinfo element as a normal
     * length-prefixed serial string, then appends fdegree as its final row.
     * Two playerinfo rows plus fdegree make a compact, evenly spaced Chinese
     * profile without changing the client's list renderer. */
    if (!vm_net_mock_seq_put_string(playerInfo, sizeof(playerInfo), &playerInfoLen,
                                    nameLine) ||
        !vm_net_mock_seq_put_string(playerInfo, sizeof(playerInfo), &playerInfoLen,
                                    levelJobLine) ||
        playerInfoLen == 0 || playerInfoLen > 0xffffu ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 2, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 2) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "fdegree", factionSexLine) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "num", 2) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "playerinfo",
                                    playerInfo, (u16)playerInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_player_info actor=%u scope=%s name=%s level=%u scene=%s playerinfo_len=%u resp=%u evidence=JianghuOL.CBE:0x0101A5AA+0x010211A8\n",
           actorId,
           targetScope,
           targetSeed.roleName ? targetSeed.roleName : "Player",
           targetSeed.level,
           scene ? scene : "-",
           playerInfoLen,
           pos);
    vm_autotest_note("mock_player_info actor=%u scope=%s level=%u response=10/2 evidence=JianghuOL.CBE:0x0101A5AA+0x010211A8\n",
                     actorId, targetScope, targetSeed.level);
    return pos;
}

/* HandleEquipInfoResponse(0x010216D6) gets the row count from `num`, then
 * reads each raw row as u32 itemId, u16 seq, and ParseEquipAttributes data.
 * Unlike backpack iteminfo, equipinfo has no count byte in its raw stream. */
static bool vm_net_mock_build_nearby_equipinfo_blob(
    u8 *out, u32 outCap, const vm_net_mock_scene_role_seed *targetSeed,
    u32 *blobLenOut, u8 *rowCountOut)
{
    const vm_mock_service_client_session *session = NULL;
    u32 pos = 0;
    u8 rowCount = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (rowCountOut)
        *rowCountOut = 0;
    if (out == NULL || targetSeed == NULL)
        return false;

    session = targetSeed->session;
    for (u8 slot = 0; session != NULL && slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        u32 itemId = session->onlineEquippedItemIds[slot];

        /* The client opens its local equip.dsh for each row.  Do not emit an
         * unknown id that would leave its visual/slot lookup uninitialized. */
        if (itemId == 0 || vm_net_mock_find_equipment_catalog_item(itemId) == NULL)
            continue;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, itemId) ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, (u16)(slot + 1)) ||
            !vm_net_mock_seq_put_item_common_extra(out, outCap, &pos, 0, 0))
        {
            return false;
        }
        ++rowCount;
    }

    /* Every newly created role has this weapon.  The fallback also covers a
     * visible session captured before its first post-upgrade presence refresh. */
    if (rowCount == 0)
    {
        u32 itemId = vm_net_mock_role_default_weapon_for_job(targetSeed->job);
        if (vm_net_mock_find_equipment_catalog_item(itemId) == NULL ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, itemId) ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, 1) ||
            !vm_net_mock_seq_put_item_common_extra(out, outCap, &pos, 0, 0))
        {
            return false;
        }
        rowCount = 1;
    }

    if (blobLenOut)
        *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = rowCount;
    return true;
}

static u32 vm_net_mock_build_nearby_equip_view_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    vm_net_mock_scene_role_seed targetSeed;
    u8 equipInfo[VM_NET_MOCK_EQUIP_SLOT_COUNT * 11];
    u32 objectStart = 0;
    u32 actorId = 0;
    u32 pos = 5;
    u32 equipInfoLen = 0;
    u8 equipRowCount = 0;
    u8 workVisualVariant = 0;
    u8 sexVisualGroup = 1;
    const char *targetScope = "nearby";
    const char *scene = vm_net_mock_current_scene_name();

    memset(&targetSeed, 0, sizeof(targetSeed));
    memset(equipInfo, 0, sizeof(equipInfo));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_nearby_equip_view_request(request, requestLen, &actorId) ||
        (!vm_net_mock_find_nearby_role_seed_by_actor_id(scene, actorId, &targetSeed) &&
         (targetScope = "friend",
          !vm_net_mock_find_friend_role_seed_by_role_id(actorId, &targetSeed))) ||
        !vm_net_mock_build_nearby_equipinfo_blob(equipInfo, sizeof(equipInfo), &targetSeed,
                                                  &equipInfoLen, &equipRowCount) ||
        equipRowCount == 0)
    {
        return 0;
    }

    /* The equipment-view draw path (0x01020C1A -> 0x010206C4) indexes its
     * role-resource table as 2 * work + sex.  It has three zero-based jobs
     * and two one-based sex groups.  Persisted jobs are 1..3, so sending one
     * unchanged shifts every portrait and makes job 3 read past that table. */
    workVisualVariant = targetSeed.job >= 1 && targetSeed.job <= 3 ?
                        (u8)(targetSeed.job - 1) : 0;
    sexVisualGroup = targetSeed.sex <= 1 ? (u8)(targetSeed.sex + 1) : 1;

    /* result=1 enters the equipment viewer. Its renderer requires at least
     * one valid local equip.dsh row; `num=0` leaves the viewer half-initialized
     * and reaches DrawMapTileLayer with an invalid tile-layer pointer. */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 4, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "num", equipRowCount) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "level",
                                    targetSeed.level ? targetSeed.level : 1) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "name",
                                       targetSeed.roleName ? targetSeed.roleName : "Player") ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "work", workVisualVariant) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "sex", sexVisualGroup) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "equipinfo",
                                    equipInfo, (u16)equipInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_nearby_equip_view actor=%u scope=%s name=%s level=%u job=%u sex=%u work=%u sex_group=%u scene=%s entries=%u equipinfo_len=%u resp=%u evidence=JianghuOL.CBE:0x0101A2B6+0x010216D6+0x01020C1A\n",
           actorId,
           targetScope,
           targetSeed.roleName ? targetSeed.roleName : "Player",
           targetSeed.level,
           targetSeed.job,
           targetSeed.sex,
           workVisualVariant,
           sexVisualGroup,
           scene ? scene : "-",
           equipRowCount,
           equipInfoLen,
           pos);
    vm_autotest_note("mock_nearby_equip_view actor=%u scope=%s work=%u sex_group=%u entries=%u equipinfo_len=%u response=29/4 evidence=JianghuOL.CBE:0x0101A2B6+0x010216D6+0x01020C1A\n",
                     actorId, targetScope, workVisualVariant, sexVisualGroup,
                     equipRowCount, equipInfoLen);
    return pos;
}

/* The request sender displays its own \"sent\" message.  Its target-facing
 * notice is queued for the receiver's next normal scene-sync poll. */
static u32 vm_net_mock_build_nearby_social_action_ack_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap,
    bool (*isRequest)(const u8 *, u32, u32 *), const char *action,
    u8 targetNoticeType, bool allowConfirmedFriend)
{
    vm_net_mock_scene_role_seed targetSeed;
    vm_mock_service_client_session *sourceSession = vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *sourceRole = vm_net_mock_active_role();
    const char *sourceAccountId = g_vm_mock_service_active_account_id;
    const char *scene = vm_net_mock_current_scene_name();
    const char *targetScope = "nearby";
    u32 actorId = 0;
    u32 pos = 5;
    bool queued = false;

    memset(&targetSeed, 0, sizeof(targetSeed));
    if (out == NULL || outCap < pos || isRequest == NULL ||
        !isRequest(request, requestLen, &actorId) ||
        (!vm_net_mock_find_nearby_role_seed_by_actor_id(scene, actorId, &targetSeed) &&
         (!allowConfirmedFriend ||
          (targetScope = "friend",
           !vm_net_mock_find_friend_role_seed_by_role_id(actorId, &targetSeed)))))
    {
        return 0;
    }
    if (targetNoticeType != VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE &&
        targetSeed.session != NULL && sourceSession != NULL && sourceRole != NULL)
    {
        queued = vm_mock_service_session_enqueue_social_notice(
            targetSeed.session, targetNoticeType, 0, sourceSession, sourceRole, sourceAccountId);
    }
    vm_net_mock_finish_wt_packet(out, pos, 0);
    printf("[info][network] mock_nearby_%s actor=%u scope=%s target=%s scene=%s queued=%u transport_ack=empty-wt resp=%u\n",
           action ? action : "action",
           actorId,
           targetScope,
           targetSeed.roleName ? targetSeed.roleName : "Player",
           scene ? scene : "-",
           queued ? 1u : 0u,
           pos);
    vm_autotest_note("mock_nearby_%s actor=%u scope=%s queued=%u response=empty-wt sender-confirmed=1\n",
                     action ? action : "action", actorId, targetScope,
                     queued ? 1u : 0u);
    return pos;
}

static u32 vm_net_mock_build_nearby_guild_invite_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    return vm_net_mock_build_nearby_social_action_ack_response(
        request, requestLen, out, outCap, vm_net_mock_is_nearby_guild_invite_request,
        "guild-invite", VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE, false);
}

static u32 vm_net_mock_build_nearby_trade_request_response(const u8 *request, u32 requestLen,
                                                            u8 *out, u32 outCap)
{
    return vm_net_mock_build_nearby_social_action_ack_response(
        request, requestLen, out, outCap, vm_net_mock_is_nearby_trade_request,
        "trade-request", VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_INVITE, true);
}

static u32 vm_net_mock_build_nearby_team_invite_response(const u8 *request, u32 requestLen,
                                                          u8 *out, u32 outCap)
{
    vm_net_mock_scene_role_seed targetSeed;
    vm_mock_service_client_session *sourceSession = vm_mock_service_get_active_client_session();
    vm_mock_service_team *sourceTeam = NULL;
    vm_net_mock_role_state *sourceRole = vm_net_mock_active_role();
    const char *sourceAccountId = g_vm_mock_service_active_account_id;
    const char *scene = vm_net_mock_current_scene_name();
    const char *targetScope = "nearby";
    const char *reason = "invalid";
    u32 actorId = 0;
    u32 pos = 5;
    bool queued = false;

    memset(&targetSeed, 0, sizeof(targetSeed));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_nearby_team_invite_request(request, requestLen, &actorId) ||
        (!vm_net_mock_find_nearby_role_seed_by_actor_id(scene, actorId, &targetSeed) &&
         (targetScope = "friend",
          !vm_net_mock_find_friend_role_seed_by_role_id(actorId, &targetSeed))))
    {
        return 0;
    }
    if (sourceSession != NULL && sourceRole != NULL && targetSeed.session != NULL &&
        sourceSession->clientId != targetSeed.session->clientId)
    {
        /* A nearby row supplies its observer-scoped scene actor id; a friend
         * row supplies the persistent role id and may point to another scene.
         * Both narrow resolvers already bind the request id to an authorized
         * online session, so do not compare the transport id with the target's
         * persistent id again. */
        sourceTeam = vm_mock_service_team_find_for_client(sourceSession->clientId);
        if (sourceTeam != NULL && !vm_mock_service_team_is_leader(sourceTeam, sourceSession->clientId))
        {
            reason = "not-leader";
        }
        else if (sourceTeam != NULL &&
                 sourceTeam->memberCount >= VM_MOCK_SERVICE_TEAM_MEMBER_MAX)
        {
            reason = "team-full";
        }
        else if (vm_mock_service_team_find_for_client(targetSeed.session->clientId) != NULL)
        {
            reason = "target-in-team";
        }
        else
        {
            queued = vm_mock_service_session_enqueue_social_notice(
                targetSeed.session, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_INVITE, 0,
                sourceSession, sourceRole, sourceAccountId);
            reason = queued ? "queued" : "notice-queue-full";
        }
    }
    else
    {
        reason = "target-unavailable";
    }

    /* CheckBattleJoinCondition already presents its sent/error message on the
     * initiator.  5/1 only needs a transport ack; the real incoming 5/2 is
     * delivered to the target through its normal scene-sync poll. */
    vm_net_mock_finish_wt_packet(out, pos, 0);
    printf("[info][network] mock_team_invite actor=%u scope=%s source=%08x/%u target=%08x/%u queued=%u reason=%s resp=%u evidence=JianghuOL.CBE:0x0101BEF4+0x01011F3A(subtype2)\n",
           actorId,
           targetScope,
           sourceSession ? sourceSession->clientId : 0,
           sourceRole ? sourceRole->roleId : 0,
           targetSeed.session ? targetSeed.session->clientId : 0,
           targetSeed.session ? targetSeed.session->onlineRoleId : 0,
           queued ? 1u : 0u, reason, pos);
    return pos;
}

/* HandleGuildJoinConfirm(0x01011ED0) is named after a stale UI label, but its
 * actual wire request is the group-invite reply: 1/5/3 { id, result }, where
 * result=1 accepts and result=0 declines the player named by id.  `id` is the
 * client-visible remote actor id, not necessarily the persistent role id. */
static bool vm_net_mock_is_team_invite_reply_request(const u8 *request, u32 requestLen,
                                                      u32 *sourceWireIdOut, u8 *resultOut)
{
    u32 offset = 4;
    u32 sourceWireId = 0;
    u8 result = 0;
    vm_net_mock_request_object object;

    if (sourceWireIdOut)
        *sourceWireIdOut = 0;
    if (resultOut)
        *resultOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 5 || object.subtype != 3 ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &sourceWireId) ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "result", &result) ||
        sourceWireId == 0 || result > 1)
    {
        return false;
    }
    if (sourceWireIdOut)
        *sourceWireIdOut = sourceWireId;
    if (resultOut)
        *resultOut = result;
    return true;
}

static void vm_mock_service_team_enqueue_member_join_for_peers(
    const vm_mock_service_team *team, u32 exceptClientA, u32 exceptClientB,
    const vm_mock_service_client_session *joinedMember)
{
    if (team == NULL || !team->active || joinedMember == NULL)
        return;
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        vm_mock_service_client_session *peer =
            vm_mock_service_find_client_session(team->memberClientIds[member]);
        if (peer != NULL && peer->clientId != exceptClientA && peer->clientId != exceptClientB)
        {
            (void)vm_mock_service_session_enqueue_social_notice(
                peer, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_MEMBER_JOIN, 0,
                joinedMember, NULL, joinedMember->accountId);
        }
    }
}

static u32 vm_net_mock_build_team_invite_reply_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    vm_mock_service_client_session *responder = vm_mock_service_get_active_client_session();
    vm_mock_service_client_session *source = NULL;
    vm_mock_service_team *team = NULL;
    vm_net_mock_role_state *responderRole = vm_net_mock_active_role();
    u32 sourceWireId = 0;
    u32 pos = 5;
    u32 objectCount = 0;
    u32 objectStart = 0;
    u8 result = 0;
    bool accepted = false;
    bool sourceNotified = false;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_team_invite_reply_request(request, requestLen, &sourceWireId, &result))
    {
        return 0;
    }
    if (responder == NULL || responderRole == NULL || !responder->teamInviteReplyActive ||
        responder->teamInviteSourceWireId != sourceWireId)
    {
        /* This remains a narrow 1/5/3 acknowledgement, but make a stale or
         * mismatched confirmation observable instead of falling through to a
         * generic unhandled-packet assertion. */
        vm_net_mock_finish_wt_packet(out, pos, 0);
        printf("[warn][network] mock_team_invite_reply_reject target=%08x id=%u result=%u pending=%u pending_source=%u reason=stale-or-id-mismatch\n",
               responder ? responder->clientId : 0, sourceWireId, result,
               responder && responder->teamInviteReplyActive ? 1u : 0u,
               responder ? responder->teamInviteSourceWireId : 0);
        return pos;
    }
    source = vm_mock_service_find_client_session(responder->teamInviteSourceClientId);
    if (result == 1 && source != NULL && source->roleOnline &&
        source->clientId != responder->clientId &&
        vm_mock_service_team_find_for_client(responder->clientId) == NULL)
    {
        team = vm_mock_service_team_find_for_client(source->clientId);
        if (team == NULL)
            team = vm_mock_service_team_create(source);
        if (team != NULL && vm_mock_service_team_is_leader(team, source->clientId) &&
            vm_mock_service_team_add_member(team, responder))
        {
            accepted = true;
            vm_mock_service_team_enqueue_member_join_for_peers(
                team, source->clientId, responder->clientId, responder);
        }
    }

    if (source != NULL)
    {
        sourceNotified = vm_mock_service_session_enqueue_social_notice(
            source, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_RESULT, accepted ? 1 : 0,
            responder, responderRole, g_vm_mock_service_active_account_id);
    }
    responder->teamInviteReplyActive = false;
    responder->teamInviteSourceClientId = 0;
    responder->teamInviteSourceWireId = 0;

    /* net_handle_group_info(0x01011F3A) cases 3/10 share the full-roster
     * parser.  For subtype 3, result=1 falls straight through to num and
     * groupinfo; returning result alone is therefore a truncated success and
     * cannot create the party UI.  A refused/failed reply has no roster. */
    if (accepted && team != NULL)
    {
        if (!vm_net_mock_append_team_group_info_object(out, outCap, &pos,
                                                       team, responder, 3))
        {
            return 0;
        }
    }
    else if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 5, 3, &objectStart) ||
             !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 0))
    {
        return 0;
    }
    else
    {
        vm_net_mock_finish_wt_object(out, objectStart, pos);
    }
    objectCount = 1;
    vm_net_mock_finish_wt_packet(out, pos, (u8)objectCount);
    printf("[info][network] mock_team_invite_reply source=%08x/%u target=%08x/%u result=%u accepted=%u notify_source=%u roster=%s members=%u resp=%u evidence=JianghuOL.CBE:0x0101216A(subtype3-full)\n",
           source ? source->clientId : 0, sourceWireId,
           responder->clientId, responderRole->roleId,
           result, accepted ? 1u : 0u, sourceNotified ? 1u : 0u,
           accepted ? "inline-5/3" : "none",
           team ? team->memberCount : 0, pos);
    return pos;
}

/* The CBE uses 1/5/6 as its local leave request.  The actual roster mutation
 * is conveyed to every client, including the sender, by subtype 7 { id }. */
static bool vm_net_mock_is_team_leave_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    return request != NULL && requestLen >= 9 && request[0] == 'W' && request[1] == 'T' &&
           vm_net_mock_next_request_object(request, requestLen, &offset, &object) &&
           offset == requestLen && object.major == 1 && object.kind == 5 && object.subtype == 6;
}

static u32 vm_net_mock_build_team_leave_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    vm_mock_service_client_session *leaver = vm_mock_service_get_active_client_session();
    u32 pos = 5;
    u32 objectStart = 0;
    bool removed = false;

    if (out == NULL || outCap < pos || !vm_net_mock_is_team_leave_request(request, requestLen))
        return 0;
    if (leaver != NULL && leaver->onlineRoleId != 0)
        removed = vm_mock_service_team_remove_member(leaver, "leave-request");
    if (removed)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 5, 7, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "id", leaver->onlineRoleId))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
    }
    else
    {
        vm_net_mock_finish_wt_packet(out, pos, 0);
    }
    printf("[info][network] mock_team_leave client=%08x role=%u removed=%u resp=%u evidence=JianghuOL.CBE:0x01011F3A(subtype7)\n",
           leaver ? leaver->clientId : 0, leaver ? leaver->onlineRoleId : 0,
           removed ? 1u : 0u, pos);
    return pos;
}

static vm_mock_service_duel *vm_mock_service_duel_begin(
    vm_mock_service_client_session *inviter,
    vm_mock_service_client_session *responder)
{
    vm_mock_service_duel *slot = NULL;
    vm_mock_service_team *inviterTeam = NULL;
    vm_mock_service_team *responderTeam = NULL;

    if (inviter == NULL || responder == NULL || inviter == responder ||
        inviter->clientId == 0 || responder->clientId == 0 ||
        !inviter->roleOnline || !responder->roleOnline ||
        inviter->onlineRoleId == 0 || responder->onlineRoleId == 0 ||
        !inviter->sceneVisibleReady || !responder->sceneVisibleReady ||
        inviter->sceneVisiblePending || responder->sceneVisiblePending ||
        !vm_mock_service_session_scene_is_visible(responder,
                                                  inviter->sceneVisibleScene) ||
        vm_mock_service_duel_find_for_client(inviter->clientId, NULL) != NULL ||
        vm_mock_service_duel_find_for_client(responder->clientId, NULL) != NULL)
    {
        return NULL;
    }
    inviterTeam = vm_mock_service_team_find_for_client(inviter->clientId);
    responderTeam = vm_mock_service_team_find_for_client(responder->clientId);
    if ((inviterTeam != NULL && inviterTeam->battleActive) ||
        (responderTeam != NULL && responderTeam->battleActive))
    {
        return NULL;
    }
    for (u32 i = 0; i < VM_MOCK_SERVICE_DUEL_MAX; ++i)
    {
        if (!g_vm_mock_service_duels[i].active)
        {
            slot = &g_vm_mock_service_duels[i];
            break;
        }
    }
    if (slot == NULL)
        return NULL;

    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->serial = ++g_vm_mock_service_duel_serial;
    if (slot->serial == 0)
        slot->serial = ++g_vm_mock_service_duel_serial;
    slot->clientIds[0] = inviter->clientId;
    slot->clientIds[1] = responder->clientId;
    snprintf(slot->scene, sizeof(slot->scene), "%s", inviter->sceneVisibleScene);
    slot->hpMax[0] = inviter->onlineHpMax ? inviter->onlineHpMax : 1;
    slot->hpMax[1] = responder->onlineHpMax ? responder->onlineHpMax : 1;
    slot->hp[0] = inviter->onlineHp ?
        vm_net_mock_min_u32(inviter->onlineHp, slot->hpMax[0]) : slot->hpMax[0];
    slot->hp[1] = responder->onlineHp ?
        vm_net_mock_min_u32(responder->onlineHp, slot->hpMax[1]) : slot->hpMax[1];
    slot->mpMax[0] = inviter->onlineMpMax;
    slot->mpMax[1] = responder->onlineMpMax;
    slot->mp[0] = vm_net_mock_min_u32(inviter->onlineMp, slot->mpMax[0]);
    slot->mp[1] = vm_net_mock_min_u32(responder->onlineMp, slot->mpMax[1]);
    slot->startPendingMask = 3;
    slot->turnIndex = 0xff;
    printf("[info][mock-service] duel_begin serial=%u inviter=%08x/%u "
           "responder=%08x/%u scene=%s hp=%u/%u,%u/%u mp=%u/%u,%u/%u\n",
           slot->serial,
           inviter->clientId, inviter->onlineRoleId,
           responder->clientId, responder->onlineRoleId,
           slot->scene,
           slot->hp[0], slot->hpMax[0], slot->hp[1], slot->hpMax[1],
           slot->mp[0], slot->mpMax[0], slot->mp[1], slot->mpMax[1]);
    return slot;
}

static u32 vm_net_mock_build_duel_start_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer)
{
    int observerIndex = -1;
    int peerIndex = -1;
    vm_mock_service_duel *duel = observer ?
        vm_mock_service_duel_find_for_client(observer->clientId,
                                             &observerIndex) : NULL;
    vm_mock_service_client_session *peer = NULL;
    u8 battleInfo[256];
    u32 battleInfoLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 observerWireId = 0;
    u32 peerWireId = 0;
    u8 observerBit = 0;

    if (out == NULL || outCap < pos || observer == NULL || duel == NULL ||
        observerIndex < 0 || observerIndex > 1)
    {
        return 0;
    }
    observerBit = (u8)(1u << observerIndex);
    if ((duel->startPendingMask & observerBit) == 0)
        return 0;
    peerIndex = 1 - observerIndex;
    peer = vm_mock_service_find_client_session(duel->clientIds[peerIndex]);
    if (peer == NULL || !peer->roleOnline || peer->onlineRoleId == 0 ||
        !vm_mock_service_session_scene_is_visible(observer, duel->scene) ||
        !vm_mock_service_session_scene_is_visible(peer, duel->scene))
    {
        vm_mock_service_duel_cancel_for_client(observer->clientId,
                                               "start-peer-or-scene-unavailable");
        return 0;
    }
    observerWireId = vm_mock_service_team_member_wire_id(observer, observer);
    peerWireId = vm_mock_service_team_member_wire_id(observer, peer);
    if (observerWireId == 0 || peerWireId == 0)
        return 0;

    /* mmBattle HandleBattleStartMsg(0x66CC), subtype 10:
     *   left/full row  = the remote opponent
     *   right/id row   = this observer's local controllable role
     * With side=1 and one fighter per side, wire 0 maps to the right/local
     * fighter and wire 1 maps to the left/opponent fighter. */
    memset(battleInfo, 0, sizeof(battleInfo));
    if (!vm_net_mock_seq_put_u8(battleInfo, sizeof(battleInfo), &battleInfoLen, 1) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 peerWireId) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 duel->hp[peerIndex]) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 duel->hpMax[peerIndex]) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 duel->mp[peerIndex]) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 duel->mpMax[peerIndex]) ||
        !vm_net_mock_seq_put_string(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                    peer->onlineRoleName[0] ?
                                        peer->onlineRoleName : "Player") ||
        !vm_net_mock_seq_put_u8(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                vm_mock_service_team_member_job_code(peer)) ||
        !vm_net_mock_seq_put_u8(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                vm_mock_service_team_member_sex_code(peer)) ||
        !vm_net_mock_seq_put_u8(battleInfo, sizeof(battleInfo), &battleInfoLen, 1) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 observerWireId) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 duel->hp[observerIndex]) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 duel->hpMax[observerIndex]) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 duel->mp[observerIndex]) ||
        !vm_net_mock_seq_put_u32(battleInfo, sizeof(battleInfo), &battleInfoLen,
                                 duel->mpMax[observerIndex]))
    {
        return 0;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 10,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "side", 1) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "battleinfo",
                                    battleInfo, (u16)battleInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    duel->startPendingMask &= (u8)~observerBit;
    duel->startedMask |= observerBit;
    printf("[info][mock-service] duel_start_deliver serial=%u observer=%08x "
           "peer=%08x local_wire=%u peer_wire=%u side=1 subtype=10 "
           "battleinfo=%u started=%02x pending=%02x resp=%u "
           "evidence=mmBattle:0x66CC/0x6C5E/0x6CE8\n",
           duel->serial, observer->clientId, peer->clientId,
           observerWireId, peerWireId, battleInfoLen,
           duel->startedMask, duel->startPendingMask, pos);
    return pos;
}

static u32 vm_net_mock_build_nearby_spar_request_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    vm_net_mock_scene_role_seed targetSeed;
    vm_mock_service_client_session *sourceSession = vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *sourceRole = vm_net_mock_active_role();
    const char *sourceAccountId = g_vm_mock_service_active_account_id;
    const char *scene = vm_net_mock_current_scene_name();
    u32 actorId = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 result = 4; /* net_handle_login_or_name_result: player not found. */
    bool queued = false;

    memset(&targetSeed, 0, sizeof(targetSeed));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_nearby_spar_request(request, requestLen, &actorId))
    {
        return 0;
    }
    if (sourceSession != NULL && sourceRole != NULL &&
        vm_net_mock_find_nearby_role_seed_by_actor_id(scene, actorId, &targetSeed) &&
        targetSeed.session != NULL &&
        targetSeed.session->clientId != sourceSession->clientId)
    {
        queued = vm_mock_service_session_enqueue_social_notice(
            targetSeed.session, VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_INVITE, 0,
            sourceSession, sourceRole, sourceAccountId);
        result = queued ? 1 : 3; /* target state cannot accept another modal. */
    }

    /* Subtype 14 accepts result=1 without presenting another message; the
     * sender already displayed "request sent".  Errors 2..7 are rendered by
     * net_handle_login_or_name_result(0x0101258A). */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 14, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_spar_request actor=%u source=%08x/%u "
           "target=%08x/%u scene=%s queued=%u result=%u resp=%u "
           "evidence=JianghuOL.CBE:0x0102E624+0x0101258A(subtype14/15)\n",
           actorId,
           sourceSession ? sourceSession->clientId : 0,
           sourceRole ? sourceRole->roleId : 0,
           targetSeed.session ? targetSeed.session->clientId : 0,
           targetSeed.session ? targetSeed.session->onlineRoleId : 0,
           scene ? scene : "-", queued ? 1u : 0u, result, pos);
    return pos;
}

static bool vm_net_mock_parse_spar_invite_reply_request(
    const u8 *request, u32 requestLen, u32 *sourceWireIdOut,
    u8 *resultOut, bool *readyIncludedOut)
{
    u32 offset = 4;
    u32 sourceWireId = 0;
    u32 readyWireId = 0;
    u8 result = 0;
    u8 objectCount = 0;
    bool haveReply = false;
    bool haveReady = false;
    vm_net_mock_request_object object;

    if (sourceWireIdOut)
        *sourceWireIdOut = 0;
    if (resultOut)
        *resultOut = 0;
    if (readyIncludedOut)
        *readyIncludedOut = false;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        ++objectCount;
        if (object.major != 1 || object.kind != 4 || objectCount > 2)
            return false;
        if (object.subtype == 16 && !haveReply &&
            vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                             "id", &sourceWireId) &&
            vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                            "result", &result) &&
            sourceWireId != 0 && (result == 1 || result == 2))
        {
            haveReply = true;
        }
        else if (object.subtype == 9 && !haveReady &&
                 vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                                  "id", &readyWireId) &&
                 readyWireId != 0)
        {
            haveReady = true;
        }
        else
        {
            return false;
        }
    }
    if (offset != requestLen || !haveReply ||
        (haveReady && (result != 1 || readyWireId != sourceWireId)))
    {
        return false;
    }
    if (sourceWireIdOut)
        *sourceWireIdOut = sourceWireId;
    if (resultOut)
        *resultOut = result;
    if (readyIncludedOut)
        *readyIncludedOut = haveReady;
    return true;
}

static u32 vm_net_mock_build_spar_invite_reply_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_mock_service_client_session *responder = vm_mock_service_get_active_client_session();
    vm_mock_service_client_session *source = NULL;
    vm_mock_service_duel *duel = NULL;
    vm_net_mock_role_state *responderRole = vm_net_mock_active_role();
    u32 sourceWireId = 0;
    u32 pos = 5;
    u8 result = 0;
    bool readyIncluded = false;
    bool accepted = false;
    bool sourceNotified = false;
    bool startDelivered = false;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_spar_invite_reply_request(
            request, requestLen, &sourceWireId, &result, &readyIncluded))
    {
        return 0;
    }
    if (responder == NULL || responderRole == NULL ||
        !responder->sparInviteReplyActive ||
        responder->sparInviteSourceWireId != sourceWireId)
    {
        vm_net_mock_finish_wt_packet(out, pos, 0);
        printf("[warn][network] mock_spar_reply_reject target=%08x id=%u "
               "result=%u pending=%u pending_source=%u reason=stale-or-id-mismatch\n",
               responder ? responder->clientId : 0, sourceWireId, result,
               responder && responder->sparInviteReplyActive ? 1u : 0u,
               responder ? responder->sparInviteSourceWireId : 0);
        return pos;
    }

    source = vm_mock_service_find_client_session(responder->sparInviteSourceClientId);
    accepted = result == 1 && source != NULL && source->roleOnline &&
               source->clientId != responder->clientId &&
               responder->sceneVisibleReady &&
               vm_mock_service_session_scene_is_visible(source,
                                                        responder->sceneVisibleScene);
    if (accepted && readyIncluded)
    {
        duel = vm_mock_service_duel_begin(source, responder);
        accepted = duel != NULL;
    }
    if (source != NULL)
    {
        sourceNotified = vm_mock_service_session_enqueue_social_notice(
            source, VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_RESULT,
            accepted ? 1 : 2, responder, responderRole,
            g_vm_mock_service_active_account_id);
    }
    if (accepted && !readyIncluded)
    {
        responder->sparBattleReadyPending = true;
        responder->sparBattlePeerClientId = source->clientId;
        responder->sparBattlePeerWireId = sourceWireId;
    }
    else
    {
        responder->sparBattleReadyPending = false;
        responder->sparBattlePeerClientId = 0;
        responder->sparBattlePeerWireId = 0;
    }
    responder->sparInviteReplyActive = false;
    responder->sparInviteSourceClientId = 0;
    responder->sparInviteSourceWireId = 0;

    /* An inline 4/9 is the responder's battle-ready edge.  PvP uses subtype
     * 10 rather than the scene-monster subtype 5, so return the responder's
     * mirrored start immediately and leave the inviter's start pending for
     * its service poll.  A separated 4/9 is handled below. */
    if (accepted && readyIncluded && duel != NULL)
    {
        pos = vm_net_mock_build_duel_start_response(out, outCap, responder);
        startDelivered = pos != 0;
        if (!startDelivered)
        {
            vm_mock_service_duel_cancel_for_client(responder->clientId,
                                                   "inline-start-build-failed");
            pos = 5;
            vm_net_mock_finish_wt_packet(out, pos, 0);
        }
    }
    else
    {
        vm_net_mock_finish_wt_packet(out, pos, 0);
    }
    printf("[info][network] mock_spar_reply source=%08x/%u target=%08x/%u "
           "result=%u accepted=%u ready_inline=%u notify_source=%u duel=%u "
           "start=%u resp=%u evidence=JianghuOL.CBE:0x010124EE+0x01012528;"
           "mmBattle:0x66CC(subtype10)\n",
           source ? source->clientId : 0, sourceWireId,
           responder->clientId, responderRole->roleId,
           result, accepted ? 1u : 0u, readyIncluded ? 1u : 0u,
           sourceNotified ? 1u : 0u, duel ? duel->serial : 0,
           startDelivered ? 1u : 0u, pos);
    return pos;
}

static bool vm_net_mock_is_spar_ready_request(const u8 *request, u32 requestLen,
                                               u32 *sourceWireIdOut)
{
    u32 offset = 4;
    u32 sourceWireId = 0;
    vm_net_mock_request_object object;

    if (sourceWireIdOut)
        *sourceWireIdOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 4 ||
        object.subtype != 9 ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                          "id", &sourceWireId) ||
        sourceWireId == 0)
    {
        return false;
    }
    if (sourceWireIdOut)
        *sourceWireIdOut = sourceWireId;
    return true;
}

static u32 vm_net_mock_build_spar_ready_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_mock_service_client_session *responder = vm_mock_service_get_active_client_session();
    vm_mock_service_client_session *source = NULL;
    vm_mock_service_duel *duel = NULL;
    u32 sourceWireId = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_spar_ready_request(request, requestLen, &sourceWireId) ||
        responder == NULL || !responder->sparBattleReadyPending ||
        responder->sparBattlePeerWireId != sourceWireId)
    {
        return 0;
    }
    source = vm_mock_service_find_client_session(responder->sparBattlePeerClientId);
    if (source != NULL && source->roleOnline &&
        vm_mock_service_session_scene_is_visible(source,
                                                 responder->sceneVisibleScene))
    {
        duel = vm_mock_service_duel_begin(source, responder);
    }
    if (duel != NULL)
        pos = vm_net_mock_build_duel_start_response(out, outCap, responder);
    if (duel == NULL || pos == 0)
    {
        if (duel != NULL)
            vm_mock_service_duel_cancel_for_client(responder->clientId,
                                                   "ready-start-build-failed");
        pos = 5;
        vm_net_mock_finish_wt_packet(out, pos, 0);
    }
    printf("[info][network] mock_spar_ready target=%08x peer=%08x wire=%u "
           "duel=%u action=%s resp=%u evidence=JianghuOL.CBE:0x01012528(4/9);"
           "mmBattle:0x66CC(subtype10)\n",
           responder->clientId, responder->sparBattlePeerClientId,
           sourceWireId, duel ? duel->serial : 0,
           duel ? "battle-start" : "empty-ack", pos);
    responder->sparBattleReadyPending = false;
    responder->sparBattlePeerClientId = 0;
    responder->sparBattlePeerWireId = 0;
    return pos;
}

static bool vm_net_mock_is_friend_add_request(const u8 *request, u32 requestLen,
                                               u32 *actorIdOut)
{
    u32 offset = 4;
    u32 actorId = 0;
    vm_net_mock_request_object object;

    if (actorIdOut)
        *actorIdOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen ||
        object.major != 1 || object.kind != 10 || object.subtype != 3 ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &actorId) ||
        actorId == 0)
    {
        return false;
    }
    if (actorIdOut)
        *actorIdOut = actorId;
    return true;
}

static u32 vm_net_mock_build_friend_add_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    vm_net_mock_scene_role_seed targetSeed;
    vm_mock_service_client_session *sourceSession = vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *ownerRole = vm_net_mock_active_role();
    const char *ownerAccountId = g_vm_mock_service_active_account_id;
    const char *scene = vm_net_mock_current_scene_name();
    u32 actorId = 0;
    u32 pos = 5;
    bool queued = false;

    memset(&targetSeed, 0, sizeof(targetSeed));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_friend_add_request(request, requestLen, &actorId))
    {
        return 0;
    }
    if (ownerRole != NULL && ownerAccountId != NULL && ownerAccountId[0] != 0 &&
        sourceSession != NULL &&
        vm_net_mock_find_nearby_role_seed_by_actor_id(scene, actorId, &targetSeed) &&
        targetSeed.session != NULL)
    {
        queued = vm_mock_service_session_enqueue_social_notice(
            targetSeed.session,
            VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_INVITE,
            0,
            sourceSession,
            ownerRole,
            ownerAccountId);
    }

    /*
     * sub_101A2EA sends 10/3 and immediately displays "invitation sent"; it
     * does not install a request-specific response callback.  The target's
     * normal scene-sync poll receives the separately confirmed 10/4 notice.
     */
    vm_net_mock_finish_wt_packet(out, pos, 0);

    printf("[info][network] mock_friend_add actor=%u queued=%u owner=%s/%u target=%s/%u resp=%u\n",
           actorId,
           queued ? 1u : 0u,
           ownerAccountId ? ownerAccountId : "-",
           ownerRole ? ownerRole->roleId : 0,
           targetSeed.session && targetSeed.session->accountId[0] ? targetSeed.session->accountId : "-",
           targetSeed.session ? targetSeed.session->onlineRoleId : 0,
           pos);
    vm_autotest_note("mock_friend_add actor=%u queued=%u response=empty-wt evidence=JianghuOL.CBE:0x0101A2EA+0x010114FC(subtype4) message=invite-sent\n",
                     actorId,
                     queued ? 1u : 0u);
    return pos;
}

static bool vm_net_mock_is_friend_invite_reply_request(const u8 *request, u32 requestLen,
                                                        u32 *sourceRoleIdOut, u8 *resultOut)
{
    u32 offset = 4;
    u32 sourceRoleId = 0;
    u8 result = 0;
    vm_net_mock_request_object object;

    if (sourceRoleIdOut)
        *sourceRoleIdOut = 0;
    if (resultOut)
        *resultOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 || object.subtype != 5 ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &sourceRoleId) ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "result", &result) ||
        sourceRoleId == 0 || (result != 1 && result != 2))
    {
        return false;
    }
    if (sourceRoleIdOut)
        *sourceRoleIdOut = sourceRoleId;
    if (resultOut)
        *resultOut = result;
    return true;
}

static u32 vm_net_mock_build_friend_invite_reply_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    vm_mock_service_client_session *responderSession = vm_mock_service_get_active_client_session();
    vm_mock_service_client_session *sourceSession = NULL;
    vm_net_mock_role_state *responderRole = vm_net_mock_active_role();
    const char *responderAccountId = g_vm_mock_service_active_account_id;
    u32 sourceRoleId = 0;
    u32 pos = 5;
    u8 result = 0;
    u8 sourceResult = 2;
    bool added = false;
    bool resultQueued = false;

    if (out == NULL || outCap < pos || responderSession == NULL || responderRole == NULL ||
        !vm_net_mock_is_friend_invite_reply_request(request, requestLen, &sourceRoleId, &result) ||
        !responderSession->friendInviteReplyActive ||
        responderSession->friendInviteSourceRoleId != sourceRoleId)
    {
        return 0;
    }
    sourceSession = vm_mock_service_find_client_session(
        responderSession->friendInviteSourceClientId);
    if (result == 1 && sourceSession != NULL && sourceSession->accountId[0] != 0 &&
        responderAccountId != NULL && responderAccountId[0] != 0 &&
        vm_mock_service_friend_db_add_pair(
            sourceSession->accountId,
            sourceSession->onlineRoleId,
            sourceSession->onlineRoleName,
            sourceSession->onlineLevel,
            sourceSession->onlineJob,
            sourceSession->onlineSex,
            responderAccountId,
            responderRole->roleId,
            responderRole->name,
            responderRole->level,
            responderRole->job,
            responderRole->sex,
            &added))
    {
        sourceResult = 1;
    }
    if (sourceSession != NULL)
    {
        resultQueued = vm_mock_service_session_enqueue_social_notice(
            sourceSession,
            VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_RESULT,
            sourceResult,
            responderSession,
            responderRole,
            responderAccountId);
    }
    responderSession->friendInviteReplyActive = false;
    responderSession->friendInviteSourceClientId = 0;
    responderSession->friendInviteSourceRoleId = 0;
    vm_net_mock_finish_wt_packet(out, pos, 0);
    printf("[info][network] mock_friend_invite_reply source=%08x/%u target=%08x/%u result=%u added=%u notify_source=%u resp=%u evidence=JianghuOL.CBE:0x010114A4+0x010114FC\n",
           sourceSession ? sourceSession->clientId : 0,
           sourceRoleId,
           responderSession->clientId,
           responderRole->roleId,
           result,
           added ? 1u : 0u,
           resultQueued ? 1u : 0u,
           pos);
    vm_autotest_note("mock_friend_invite_reply source_role=%u result=%u added=%u notify_source=%u response=empty-wt evidence=JianghuOL.CBE:0x010114A4\n",
                     sourceRoleId, result, added ? 1u : 0u, resultQueued ? 1u : 0u);
    return pos;
}

static bool vm_net_mock_is_trade_invite_reply_request(const u8 *request, u32 requestLen,
                                                       u32 *sourceRoleIdOut, u8 *resultOut)
{
    u32 offset = 4;
    u32 sourceRoleId = 0;
    u8 result = 0;
    vm_net_mock_request_object object;

    if (sourceRoleIdOut)
        *sourceRoleIdOut = 0;
    if (resultOut)
        *resultOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 21 || object.subtype != 3 ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "id", &sourceRoleId) ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "result", &result) ||
        sourceRoleId == 0 || (result != 1 && result != 2))
    {
        return false;
    }
    if (sourceRoleIdOut)
        *sourceRoleIdOut = sourceRoleId;
    if (resultOut)
        *resultOut = result;
    return true;
}

static u32 vm_net_mock_build_trade_invite_reply_response(const u8 *request, u32 requestLen,
                                                          u8 *out, u32 outCap)
{
    vm_mock_service_client_session *responderSession = vm_mock_service_get_active_client_session();
    vm_mock_service_client_session *sourceSession = NULL;
    vm_mock_service_trade *trade = NULL;
    vm_net_mock_role_state *responderRole = vm_net_mock_active_role();
    const char *responderAccountId = g_vm_mock_service_active_account_id;
    u32 sourceRoleId = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 result = 0;
    bool resultQueued = false;
    bool responderStarted = false;
    bool tradeStarted = false;

    if (out == NULL || outCap < pos || responderSession == NULL || responderRole == NULL ||
        !vm_net_mock_is_trade_invite_reply_request(request, requestLen, &sourceRoleId, &result) ||
        !responderSession->tradeInviteReplyActive ||
        responderSession->tradeInviteSourceRoleId != sourceRoleId)
    {
        return 0;
    }
    sourceSession = vm_mock_service_find_client_session(
        responderSession->tradeInviteSourceClientId);
    if (result == 1 && sourceSession == NULL)
        result = 2;
    if (result == 1 && sourceSession != NULL)
    {
        trade = vm_mock_service_trade_begin(sourceSession, responderSession);
        tradeStarted = trade != NULL;
        if (!tradeStarted)
            result = 2;
    }
    if (sourceSession != NULL)
    {
        resultQueued = vm_mock_service_session_enqueue_social_notice(
            sourceSession,
            VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_RESULT,
            result,
            responderSession,
            responderRole,
            responderAccountId);
    }
    if (trade != NULL && !resultQueued)
    {
        /* Neither peer entered the trade UI, so no terminal packet is needed.
         * Free the just-created slot instead of leaving both clients busy. */
        memset(trade, 0, sizeof(*trade));
        trade = NULL;
        tradeStarted = false;
        result = 2;
    }
    responderSession->tradeInviteReplyActive = false;
    responderSession->tradeInviteSourceClientId = 0;
    responderSession->tradeInviteSourceRoleId = 0;
    /* The confirmation callback only emits 21/3 and clears the modal state.
     * Both peers enter the trade screen exclusively through subtype 21/4
     * result=1, so return that object directly to the accepting peer while
     * the queued TRADE_RESULT delivers the mirror object to the requester. */
    if (result == 1 && resultQueued && sourceSession != NULL)
    {
        const char *sourceName = sourceSession->onlineRoleName[0] ?
                                 sourceSession->onlineRoleName : "Player";
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 21, 4, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "name", sourceName))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
        responderStarted = true;
    }
    else
    {
        vm_net_mock_finish_wt_packet(out, pos, 0);
    }
    printf("[info][network] mock_trade_invite_reply source=%08x/%u target=%08x/%u result=%u notify_source=%u trade_started=%u responder_start=%u resp=%u evidence=JianghuOL.CBE:0x01011076+0x01011132(subtype4)\n",
           sourceSession ? sourceSession->clientId : 0,
           sourceRoleId,
           responderSession->clientId,
           responderRole->roleId,
           result,
           resultQueued ? 1u : 0u,
           tradeStarted ? 1u : 0u,
           responderStarted ? 1u : 0u,
           pos);
    vm_autotest_note("mock_trade_invite_reply source_role=%u result=%u notify_source=%u trade_started=%u responder_start=%u response=%s evidence=JianghuOL.CBE:0x01011076+0x01011132\n",
                     sourceRoleId, result, resultQueued ? 1u : 0u,
                     tradeStarted ? 1u : 0u,
                     responderStarted ? 1u : 0u,
                     responderStarted ? "21/4" : "empty-wt");
    return pos;
}

static bool vm_net_mock_trade_seq_read_uint(const u8 *data,
                                            u32 dataLen,
                                            u32 *pos,
                                            u32 *valueOut)
{
    u32 width = 0;
    u32 value = 0;

    if (valueOut)
        *valueOut = 0;
    if (data == NULL || pos == NULL || *pos + 2 > dataLen || data[*pos] != 0)
        return false;
    width = data[*pos + 1];
    *pos += 2;
    if ((width != 1 && width != 2 && width != 4) || *pos + width > dataLen)
        return false;
    for (u32 i = 0; i < width; ++i)
        value = (value << 8) | data[(*pos)++];
    if (valueOut)
        *valueOut = value;
    return true;
}

static bool vm_net_mock_parse_trade_offer_request(
    const u8 *request,
    u32 requestLen,
    vm_mock_service_trade_offer *offer,
    bool *fieldsValidOut)
{
    u32 offset = 4;
    u32 itemCount = 0;
    u32 money = 0;
    u32 blobPos = 0;
    const u8 *itemInfo = NULL;
    u16 itemInfoLen = 0;
    vm_net_mock_request_object object;

    if (offer)
        memset(offer, 0, sizeof(*offer));
    if (fieldsValidOut)
        *fieldsValidOut = false;
    if (request == NULL || requestLen < 9 || offer == NULL ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 21 ||
        object.subtype != 5)
    {
        return false;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "num", &itemCount) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "trademoney", &money) ||
        !vm_net_mock_get_object_blob_field(object.payload, object.payloadLen,
                                           "iteminfo", &itemInfo, &itemInfoLen) ||
        itemCount > VM_MOCK_SERVICE_TRADE_ITEM_MAX)
    {
        return true;
    }
    offer->itemCount = (u8)itemCount;
    offer->money = money;
    for (u32 i = 0; i < itemCount; ++i)
    {
        u32 seq = 0;
        u32 count = 0;
        if (!vm_net_mock_trade_seq_read_uint(itemInfo, itemInfoLen, &blobPos, &seq) ||
            !vm_net_mock_trade_seq_read_uint(itemInfo, itemInfoLen, &blobPos, &count) ||
            seq == 0 || seq > 0xffffu || count == 0 || count > 0xffffu)
        {
            return true;
        }
        offer->items[i].sourceSeq = (u16)seq;
        offer->items[i].count = count;
    }
    if (blobPos != itemInfoLen || (itemCount == 0 && money == 0))
        return true;
    if (fieldsValidOut)
        *fieldsValidOut = true;
    return true;
}

static bool vm_net_mock_trade_validate_offer(vm_mock_service_trade_offer *offer,
                                             vm_net_mock_role_state *role)
{
    if (offer == NULL || role == NULL ||
        offer->itemCount > VM_MOCK_SERVICE_TRADE_ITEM_MAX ||
        offer->money > role->money ||
        (offer->itemCount == 0 && offer->money == 0))
    {
        return false;
    }
    for (u32 i = 0; i < offer->itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = NULL;
        if (offer->items[i].sourceSeq == 0 || offer->items[i].count == 0)
            return false;
        for (u32 previous = 0; previous < i; ++previous)
        {
            if (offer->items[previous].sourceSeq == offer->items[i].sourceSeq)
                return false;
        }
        item = vm_net_mock_role_find_backpack_item(
            role, 0, offer->items[i].sourceSeq);
        if (item == NULL || item->itemId == 0 || item->count < offer->items[i].count)
            return false;
        offer->items[i].itemId = item->itemId;
    }
    return true;
}

static bool vm_net_mock_append_trade_offer_object(
    u8 *out,
    u32 outCap,
    u32 *pos,
    const vm_mock_service_trade_offer *offer)
{
    u8 itemInfo[2048];
    u32 itemInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || offer == NULL || !offer->submitted ||
        offer->itemCount > VM_MOCK_SERVICE_TRADE_ITEM_MAX)
    {
        return false;
    }
    memset(itemInfo, 0, sizeof(itemInfo));
    for (u32 i = 0; i < offer->itemCount; ++i)
    {
        const vm_mock_service_trade_item *item = &offer->items[i];
        if (!vm_net_mock_seq_put_i16(itemInfo, sizeof(itemInfo), &itemInfoLen,
                                     item->sourceSeq) ||
            !vm_net_mock_seq_put_u32(itemInfo, sizeof(itemInfo), &itemInfoLen,
                                     item->itemId) ||
            !vm_net_mock_seq_put_i16(itemInfo, sizeof(itemInfo), &itemInfoLen,
                                     (u16)SDL_min(item->count, 0xffffu)) ||
            (item->itemId < 1000 &&
             !vm_net_mock_seq_put_string(itemInfo, sizeof(itemInfo),
                                         &itemInfoLen, "")) ||
            !vm_net_mock_seq_put_item_common_extra(itemInfo, sizeof(itemInfo),
                                                   &itemInfoLen, 0, 0))
        {
            return false;
        }
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 21, 6,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "num", offer->itemCount) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "trademoney", offer->money) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo",
                                    itemInfo, (u16)itemInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_trade_terminal_object(
    u8 *out,
    u32 outCap,
    u32 *pos,
    u8 subtype,
    u8 result,
    u32 finalMoney,
    const vm_mock_service_trade_offer *receipt)
{
    u8 itemInfo[512];
    u32 itemInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || (subtype != 7 && subtype != 8))
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (subtype == 8 && result == 1)
    {
        if (receipt == NULL || receipt->itemCount > VM_MOCK_SERVICE_TRADE_ITEM_MAX)
            return false;
        for (u32 i = 0; i < receipt->itemCount; ++i)
        {
            const vm_mock_service_trade_item *item = &receipt->items[i];
            if (!vm_net_mock_seq_put_i16(itemInfo, sizeof(itemInfo), &itemInfoLen,
                                         item->destinationSeq) ||
                !vm_net_mock_seq_put_u32(itemInfo, sizeof(itemInfo), &itemInfoLen,
                                         item->itemId) ||
                !vm_net_mock_seq_put_i16(itemInfo, sizeof(itemInfo), &itemInfoLen,
                                         (u16)SDL_min(item->count, 0xffffu)))
            {
                return false;
            }
        }
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 21, subtype,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", result))
    {
        return false;
    }
    if (subtype == 8 && result == 1 &&
        (!vm_net_mock_put_object_u8(out, outCap, pos, "num", receipt->itemCount) ||
         !vm_net_mock_put_object_u32(out, outCap, pos, "money", finalMoney) ||
         !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo",
                                     itemInfo, (u16)itemInfoLen)))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_trade_offer_response(const u8 *request,
                                                   u32 requestLen,
                                                   u8 *out,
                                                   u32 outCap)
{
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_trade_offer parsed;
    vm_mock_service_trade *trade = NULL;
    u32 pos = 5;
    u32 objectStart = 0;
    int index = -1;
    bool fieldsValid = false;
    u8 result = 3;

    if (!vm_net_mock_parse_trade_offer_request(request, requestLen,
                                               &parsed, &fieldsValid))
    {
        return 0;
    }
    trade = session ? vm_mock_service_trade_find_for_client(session->clientId,
                                                            &index) : NULL;
    if (trade == NULL || index < 0 || !trade->active)
    {
        result = 2;
    }
    else if (fieldsValid && vm_net_mock_trade_validate_offer(&parsed, role))
    {
        parsed.submitted = true;
        trade->offers[index] = parsed;
        trade->confirmedMask = 0;
        trade->offerPendingMask |= (u8)(1u << (1 - index));
        result = 1;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 21, 5,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_trade_offer client=%08x side=%d result=%u items=%u money=%u peer_pending=%u resp=%u evidence=JianghuOL.CBE:0x01022D4E+0x01025AE6(subtype5/6)\n",
           session ? session->clientId : 0, index, result,
           parsed.itemCount, parsed.money,
           trade ? ((trade->offerPendingMask >> (1 - index)) & 1u) : 0u,
           pos);
    return pos;
}

enum
{
    VM_MOCK_TRADE_COMMIT_OK = 1,
    VM_MOCK_TRADE_COMMIT_INVALID = 2,
    VM_MOCK_TRADE_COMMIT_BAG_FULL = 3,
    VM_MOCK_TRADE_COMMIT_STORAGE_FAILED = 4
};

static u8 vm_mock_service_trade_commit(vm_mock_service_trade *trade)
{
    vm_mock_service_client_session *sessions[2];
    const vm_mock_service_client_session *persistSessions[2];
    vm_mock_service_account_state *accounts[2];
    vm_net_mock_role_state *liveRoles[2];
    vm_net_mock_role_state roles[2];

    if (trade == NULL || !trade->active ||
        !trade->offers[0].submitted || !trade->offers[1].submitted)
    {
        return VM_MOCK_TRADE_COMMIT_INVALID;
    }
    memset(sessions, 0, sizeof(sessions));
    memset(persistSessions, 0, sizeof(persistSessions));
    memset(accounts, 0, sizeof(accounts));
    memset(liveRoles, 0, sizeof(liveRoles));
    memset(roles, 0, sizeof(roles));
    for (u32 side = 0; side < 2; ++side)
    {
        sessions[side] = vm_mock_service_find_client_session(trade->clientIds[side]);
        persistSessions[side] = sessions[side];
        liveRoles[side] = vm_mock_service_trade_role_for_session(sessions[side],
                                                                 &accounts[side]);
        if (sessions[side] == NULL || !sessions[side]->roleOnline ||
            liveRoles[side] == NULL ||
            !vm_net_mock_trade_validate_offer(&trade->offers[side], liveRoles[side]))
        {
            return VM_MOCK_TRADE_COMMIT_INVALID;
        }
        roles[side] = *liveRoles[side];
        memset(&trade->receipts[side], 0, sizeof(trade->receipts[side]));
    }
    for (u32 side = 0; side < 2; ++side)
    {
        const vm_mock_service_trade_offer *offer = &trade->offers[side];
        for (u32 i = 0; i < offer->itemCount; ++i)
        {
            if (!vm_net_mock_role_consume_backpack_item(
                    &roles[side], offer->items[i].itemId,
                    offer->items[i].sourceSeq, offer->items[i].count, NULL))
            {
                return VM_MOCK_TRADE_COMMIT_INVALID;
            }
        }
        roles[side].money -= offer->money;
    }
    for (u32 side = 0; side < 2; ++side)
    {
        const vm_mock_service_trade_offer *incoming = &trade->offers[1 - side];
        vm_mock_service_trade_offer *receipt = &trade->receipts[side];
        uint64_t finalMoney = (uint64_t)roles[side].money + incoming->money;
        if (finalMoney > 0xffffffffull)
            return VM_MOCK_TRADE_COMMIT_INVALID;
        roles[side].money = (u32)finalMoney;
        receipt->submitted = true;
        receipt->itemCount = incoming->itemCount;
        for (u32 i = 0; i < incoming->itemCount; ++i)
        {
            receipt->items[i] = incoming->items[i];
            if (!vm_mock_service_trade_role_add_item(
                    &roles[side], incoming->items[i].itemId,
                    incoming->items[i].count,
                    &receipt->items[i].destinationSeq))
            {
                return VM_MOCK_TRADE_COMMIT_BAG_FULL;
            }
        }
        vm_net_mock_role_normalize_backpack(&roles[side]);
        trade->finalMoney[side] = roles[side].money;
    }
    if (!vm_mock_service_trade_persist_pair(persistSessions, roles))
    {
        return VM_MOCK_TRADE_COMMIT_STORAGE_FAILED;
    }
    for (u32 side = 0; side < 2; ++side)
        *liveRoles[side] = roles[side];
    printf("[info][mock-service] trade_commit first=%08x/%u money=%u items=%u second=%08x/%u money=%u items=%u\n",
           sessions[0]->clientId, roles[0].roleId, roles[0].money,
           trade->receipts[0].itemCount,
           sessions[1]->clientId, roles[1].roleId, roles[1].money,
           trade->receipts[1].itemCount);
    return VM_MOCK_TRADE_COMMIT_OK;
}

static bool vm_net_mock_parse_trade_confirm_request(const u8 *request,
                                                    u32 requestLen,
                                                    u8 *resultOut)
{
    u32 offset = 4;
    u8 result = 0;
    vm_net_mock_request_object object;

    if (resultOut)
        *resultOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 21 ||
        object.subtype != 7 ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                         "result", &result) ||
        (result != 1 && result != 2))
    {
        return false;
    }
    if (resultOut)
        *resultOut = result;
    return true;
}

static u32 vm_net_mock_build_trade_confirm_response(const u8 *request,
                                                     u32 requestLen,
                                                     u8 *out,
                                                     u32 outCap)
{
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();
    vm_mock_service_trade *trade = NULL;
    u32 pos = 5;
    int index = -1;
    u8 requestResult = 0;
    u8 responseSubtype = 7;
    u8 responseResult = 2;
    u8 commitResult = 0;

    if (!vm_net_mock_parse_trade_confirm_request(request, requestLen,
                                                 &requestResult))
    {
        return 0;
    }
    trade = session ? vm_mock_service_trade_find_for_client(session->clientId,
                                                            &index) : NULL;
    if (trade != NULL && index >= 0 && trade->active)
    {
        if (requestResult == 2)
        {
            vm_mock_service_trade_set_terminal(trade, 7, 2,
                                               (u8)(1u << (1 - index)));
            responseResult = 2;
        }
        else if (trade->offers[0].submitted && trade->offers[1].submitted)
        {
            trade->confirmedMask |= (u8)(1u << index);
            responseResult = 1;
            if (trade->confirmedMask == 3)
            {
                commitResult = vm_mock_service_trade_commit(trade);
                if (commitResult == VM_MOCK_TRADE_COMMIT_OK)
                {
                    responseSubtype = 8;
                    responseResult = 1;
                    vm_mock_service_trade_set_terminal(trade, 8, 1,
                                                       (u8)(1u << (1 - index)));
                }
                else if (commitResult == VM_MOCK_TRADE_COMMIT_BAG_FULL)
                {
                    responseSubtype = 8;
                    responseResult = 3;
                    vm_mock_service_trade_set_terminal(trade, 8, 3,
                                                       (u8)(1u << (1 - index)));
                }
                else if (commitResult == VM_MOCK_TRADE_COMMIT_STORAGE_FAILED)
                {
                    responseSubtype = 8;
                    responseResult = 2;
                    vm_mock_service_trade_set_terminal(trade, 8, 2,
                                                       (u8)(1u << (1 - index)));
                }
                else
                {
                    responseSubtype = 7;
                    responseResult = 3;
                    vm_mock_service_trade_set_terminal(trade, 7, 3,
                                                       (u8)(1u << (1 - index)));
                }
            }
        }
        else
        {
            responseResult = 2;
            vm_mock_service_trade_set_terminal(trade, 7, 2,
                                               (u8)(1u << (1 - index)));
        }
    }
    if (!vm_net_mock_append_trade_terminal_object(
            out, outCap, &pos, responseSubtype, responseResult,
            trade && index >= 0 ? trade->finalMoney[index] : 0,
            trade && index >= 0 ? &trade->receipts[index] : NULL))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_trade_confirm client=%08x side=%d request=%u response=21/%u result=%u confirmed_mask=%u commit=%u resp=%u evidence=JianghuOL.CBE:0x01022E24+0x01027726\n",
           session ? session->clientId : 0, index, requestResult,
           responseSubtype, responseResult,
           trade ? trade->confirmedMask : 0,
           commitResult, pos);
    if (trade != NULL && responseSubtype == 8)
        vm_mock_service_trade_release_if_delivered(trade);
    return pos;
}

static bool vm_net_mock_is_guild_page_request(const u8 *request, u32 requestLen,
                                               u32 *indexOut, u32 *pageSizeOut,
                                               u8 *requestSubtypeOut)
{
    u32 offset = 4;
    u32 index = 0;
    u32 pageSize = 0;
    vm_net_mock_request_object object;

    if (indexOut)
        *indexOut = 0;
    if (pageSizeOut)
        *pageSizeOut = 0;
    if (requestSubtypeOut)
        *requestSubtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        (object.subtype != 20 && object.subtype != 21) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "index", &index) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "pagesize", &pageSize) ||
        pageSize == 0)
    {
        return false;
    }
    if (pageSize > VM_NET_MOCK_GUILD_PAGE_MAX)
        pageSize = VM_NET_MOCK_GUILD_PAGE_MAX;
    if (indexOut)
        *indexOut = index;
    if (pageSizeOut)
        *pageSizeOut = pageSize;
    if (requestSubtypeOut)
        *requestSubtypeOut = object.subtype;
    return true;
}

static bool vm_net_mock_is_guild_member_page_request(const u8 *request, u32 requestLen,
                                                       u32 *indexOut, u32 *pageSizeOut,
                                                       u8 *requestSubtypeOut)
{
    u32 offset = 4;
    u32 index = 0;
    u32 pageSize = 0;
    vm_net_mock_request_object object;

    if (indexOut)
        *indexOut = 0;
    if (pageSizeOut)
        *pageSizeOut = 0;
    if (requestSubtypeOut)
        *requestSubtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        (object.subtype != 20 && object.subtype != 37) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "index", &index) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "pagesize", &pageSize) ||
        pageSize == 0)
    {
        return false;
    }
    if (pageSize > VM_NET_MOCK_GUILD_PAGE_MAX)
        pageSize = VM_NET_MOCK_GUILD_PAGE_MAX;
    if (indexOut)
        *indexOut = index;
    if (pageSizeOut)
        *pageSizeOut = pageSize;
    if (requestSubtypeOut)
        *requestSubtypeOut = object.subtype;
    return true;
}

static bool vm_net_mock_is_guild_detail_request(const u8 *request, u32 requestLen,
                                                 u32 *guildIdOut)
{
    u32 offset = 4;
    u32 guildId = 0;
    vm_net_mock_request_object object;

    if (guildIdOut)
        *guildIdOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        (object.subtype != 22 && object.subtype != 23) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "id", &guildId) ||
        guildId == 0)
    {
        return false;
    }
    if (guildIdOut)
        *guildIdOut = guildId;
    return true;
}

/* The guild-member screen reuses 10/22 for two different operations.  A
 * selected guild row sends id=guildId and expects the 10/23 detail object;
 * confirming "leave guild" sends id=actor+100 (the current role id) and
 * HandleBattleResultEvent consumes a same-subtype 10/22 {result}. */
static bool vm_net_mock_parse_guild_leave_request(const u8 *request, u32 requestLen,
                                                   u32 *roleIdOut)
{
    u32 offset = 4;
    u32 roleId = 0;
    vm_net_mock_request_object object;

    if (roleIdOut)
        *roleIdOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        object.subtype != 22 ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "id", &roleId) ||
        roleId == 0)
    {
        return false;
    }
    if (roleIdOut)
        *roleIdOut = roleId;
    return true;
}

static bool vm_net_mock_is_guild_create_start_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    return request != NULL && requestLen >= 9 && request[0] == 'W' && request[1] == 'T' &&
           vm_net_mock_next_request_object(request, requestLen, &offset, &object) &&
           offset == requestLen && object.major == 1 && object.kind == 10 &&
           object.subtype == 30 && object.payloadLen == 0;
}

/* DispatchRoleAction sends this empty permission/dialog gate before the
 * client-owned guild-management continuation.  HandleDialogResult consumes a
 * same-subtype result and clears the network wait flag before invoking that
 * continuation on success. */
static bool vm_net_mock_is_guild_dialog_gate_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    return request != NULL && requestLen == 9 &&
           request[0] == 'W' && request[1] == 'T' &&
           vm_net_mock_next_request_object(request, requestLen, &offset, &object) &&
           offset == requestLen && object.major == 1 && object.kind == 10 &&
           object.subtype == 19 && object.payloadLen == 0;
}

static bool vm_net_mock_parse_guild_name_request(const u8 *request, u32 requestLen,
                                                  u8 subtype,
                                                  char *nameOut, u32 nameOutSize)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    if (nameOut != NULL && nameOutSize > 0)
        nameOut[0] = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        nameOut == NULL || nameOutSize == 0 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        object.subtype != subtype ||
        !vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                             "name", nameOut, nameOutSize))
    {
        return false;
    }
    return nameOut[0] != 0;
}

static bool vm_net_mock_is_guild_apply_request(const u8 *request, u32 requestLen,
                                                  u32 *guildIdOut)
{
    u32 offset = 4;
    u32 guildId = 0;
    vm_net_mock_request_object object;
    if (guildIdOut)
        *guildIdOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        object.subtype != 31 ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "id", &guildId) ||
        guildId == 0)
    {
        return false;
    }
    if (guildIdOut)
        *guildIdOut = guildId;
    return true;
}

typedef struct
{
    bool valid;
    u32 requestId;
    u32 index;
    u32 pageSize;
} vm_net_mock_guild_application_page_request;

typedef struct
{
    bool valid;
    u8 type;
    u32 roleId;
} vm_net_mock_guild_application_action;

/* The recruitment screen is not the title role selector.  SendRankingPageReq
 * emits 10/32 {id=0,index=one-based first row,pagesize}; HandleRoleListResponse
 * consumes the pending applicants from roleinfo.  Keep malformed 10/32 packets
 * in this handler so the screen receives an error result and clears +145. */
static bool vm_net_mock_parse_guild_application_page_request(
    const u8 *request, u32 requestLen,
    vm_net_mock_guild_application_page_request *pageOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_guild_application_page_request page;

    memset(&page, 0, sizeof(page));
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        object.subtype != 32)
    {
        return false;
    }
    page.valid =
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "id", &page.requestId) &&
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "index", &page.index) &&
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "pagesize", &page.pageSize) &&
        page.index > 0 && page.pageSize > 0;
    if (page.pageSize > VM_NET_MOCK_GUILD_PAGE_MAX)
        page.pageSize = VM_NET_MOCK_GUILD_PAGE_MAX;
    if (pageOut)
        *pageOut = page;
    return true;
}

/* The applicant row action uses type=1 for approval and type=2 for rejection.
 * HandleRankingResponse expects the reply on the same 10/33 subtype. */
static bool vm_net_mock_parse_guild_application_action(
    const u8 *request, u32 requestLen,
    vm_net_mock_guild_application_action *actionOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_guild_application_action action;

    memset(&action, 0, sizeof(action));
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        object.subtype != 33)
    {
        return false;
    }
    action.valid =
        vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                        "type", &action.type) &&
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "roleid", &action.roleId) &&
        (action.type == 1 || action.type == 2) && action.roleId != 0;
    if (actionOut)
        *actionOut = action;
    return true;
}

/* The guild menu uses a two-stage slogan flow.  Selecting "publish notice"
 * sends an empty 10/35 to load the current text; confirming the editor sends
 * 10/34 {slogan}.  The two requests have separate client result handlers:
 * HandleGuildSloganResult consumes 10/35 {result,slogan}, while
 * HandleChatChannelResult consumes 10/34 {result}. */
static bool vm_net_mock_parse_guild_slogan_request(const u8 *request, u32 requestLen,
                                                    bool *publishOut,
                                                    bool *sloganParsedOut,
                                                    char *sloganOut, u32 sloganOutSize)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (publishOut)
        *publishOut = false;
    if (sloganParsedOut)
        *sloganParsedOut = false;
    if (sloganOut != NULL && sloganOutSize > 0)
        sloganOut[0] = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10)
    {
        return false;
    }
    if (object.subtype == 35 && object.payloadLen == 0)
        return true;
    if (object.subtype != 34)
        return false;
    if (publishOut)
        *publishOut = true;
    if (sloganOut != NULL && sloganOutSize > 0 &&
        vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                            "slogan", sloganOut, sloganOutSize))
    {
        if (sloganParsedOut)
            *sloganParsedOut = true;
    }
    /* A malformed publish still belongs to this flow.  Returning a 10/34
     * failure is required to clear screen+145 instead of falling through to
     * the service's unhandled-packet assertion. */
    return true;
}

static bool vm_net_mock_guild_slogan_is_valid(const char *slogan)
{
    size_t length = 0;
    size_t offset = 0;

    if (slogan == NULL)
        return false;
    length = vm_mock_mysql_bounded_strlen(slogan, VM_NET_MOCK_GUILD_TEXT_SIZE);
    if (length >= VM_NET_MOCK_GUILD_TEXT_SIZE ||
        length > VM_NET_MOCK_GUILD_NOTICE_MAX_BYTES)
    {
        return false;
    }
    while (offset < length)
    {
        const unsigned char lead = (unsigned char)slogan[offset];
        if (lead < 0x20u || lead == 0x7fu)
            return false;
        if (lead < 0x80u)
        {
            ++offset;
            continue;
        }
        if (offset + 1 >= length)
            return false;
        const unsigned char trail = (unsigned char)slogan[offset + 1];
        if (trail < 0x40u || trail == 0x7fu)
            return false;
        offset += 2;
    }
    return true;
}

typedef struct
{
    u8 subtype;
    bool valid;
    u32 aid;
    u32 id;
    char dname[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
    u32 did;
    u32 drank;
    char uname[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE];
    u32 uid;
    u32 urank;
} vm_net_mock_guild_rank_action;

typedef struct
{
    bool valid;
    u32 roleId;
    u32 memberRank;
} vm_net_mock_guild_kick_action;

/* ProcessBattleSceneInput confirms the member-row "kick" action with:
 *   10/40 {id=target role id,rid=target member rank}
 * Keep malformed subtype-40 objects in this handler so HandleBattleMenuResult
 * still receives a same-subtype result and clears the screen wait state. */
static bool vm_net_mock_parse_guild_kick_action(const u8 *request, u32 requestLen,
                                                 vm_net_mock_guild_kick_action *actionOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_guild_kick_action action;

    memset(&action, 0, sizeof(action));
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        object.subtype != 40)
    {
        return false;
    }
    action.valid =
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "id", &action.roleId) &&
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "rid", &action.memberRank) &&
        action.roleId != 0 && action.memberRank != 0;
    if (actionOut)
        *actionOut = action;
    return true;
}

/* HandleFactionActionInput sends three distinct rank mutations:
 *   10/38 {aid,id}                                      vacant position
 *   10/39 {dname,did,drank,uname,uid,urank}             replace occupant
 *   10/41 {uname,uid,urank}                             leader hand-over
 * Recognize malformed objects as part of this flow too, so the matching
 * result response always clears the client's network wait flag. */
static bool vm_net_mock_parse_guild_rank_action(const u8 *request, u32 requestLen,
                                                 vm_net_mock_guild_rank_action *actionOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_guild_rank_action action;

    memset(&action, 0, sizeof(action));
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 10 ||
        (object.subtype != 38 && object.subtype != 39 && object.subtype != 41))
    {
        return false;
    }
    action.subtype = object.subtype;
    if (object.subtype == 38)
    {
        action.valid =
            vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "aid", &action.aid) &&
            vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "id", &action.id) &&
            action.aid != 0 && action.id != 0;
    }
    else if (object.subtype == 39)
    {
        action.valid =
            vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                                 "dname", action.dname,
                                                 sizeof(action.dname)) &&
            vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "did", &action.did) &&
            vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "drank", &action.drank) &&
            vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                                 "uname", action.uname,
                                                 sizeof(action.uname)) &&
            vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "uid", &action.uid) &&
            vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "urank", &action.urank) &&
            action.dname[0] != 0 && action.uname[0] != 0 &&
            action.did != 0 && action.uid != 0 && action.did != action.uid;
    }
    else
    {
        action.valid =
            vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                                 "uname", action.uname,
                                                 sizeof(action.uname)) &&
            vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "uid", &action.uid) &&
            vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "urank", &action.urank) &&
            action.uname[0] != 0 && action.uid != 0;
    }
    if (actionOut)
        *actionOut = action;
    return true;
}

static u32 vm_net_mock_build_guild_dialog_gate_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_guild_record guild;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 memberRank = 0;
    u8 result = 2;

    memset(&guild, 0, sizeof(guild));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_dialog_gate_request(request, requestLen))
    {
        return 0;
    }
    /* This gate leads into the privileged guild-management continuation.
     * Rank mutations remain independently revalidated by 10/38, 10/39 and
     * 10/41; only the current leader may pass this preflight. */
    if (role != NULL &&
        vm_net_mock_guild_find_role_membership(role->roleId, &guild, &memberRank) &&
        guild.guildId != 0 && memberRank == 1)
    {
        result = 1;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 19, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_dialog_gate role=%u guild=%u rank=%u "
           "result=%u response=10/19 resp=%u "
           "evidence=JianghuOL.CBE:0x01040AD8+0x01040B36+0x0104149E+0x01041094\n",
           role ? role->roleId : 0, guild.guildId, memberRank, result, pos);
    return pos;
}

static u32 vm_net_mock_build_guild_page_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    vm_net_mock_guild_record rows[VM_NET_MOCK_GUILD_PAGE_MAX];
    vm_net_mock_guild_record membership;
    u8 faction[8192];
    char tail[96];
    u32 index = 0;
    u32 pageSize = 0;
    u32 offset = 0;
    u32 totalGuilds = 0;
    u32 totalPages = 1;
    u32 rowCount = 0;
    u32 factionLen = 0;
    u32 currentGuildId = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 requestSubtype = 0;

    memset(rows, 0, sizeof(rows));
    memset(&membership, 0, sizeof(membership));
    memset(faction, 0, sizeof(faction));
    memset(tail, 0, sizeof(tail));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_page_request(request, requestLen, &index, &pageSize,
                                           &requestSubtype))
    {
        return 0;
    }
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        if (role != NULL &&
            vm_net_mock_guild_find_role_membership(role->roleId, &membership, NULL))
        {
            currentGuildId = membership.guildId;
        }
    }
    /* SendGuildPageReq uses a one-based first-row index: page zero sends 1. */
    offset = index > 0 ? index - 1u : 0;
    if (!vm_net_mock_guild_count("guilds", &totalGuilds))
        return 0;
    if (totalGuilds > 0)
        totalPages = (totalGuilds + pageSize - 1u) / pageSize;
    snprintf(tail, sizeof(tail), "LIMIT %u OFFSET %u", pageSize, offset);
    if (!vm_net_mock_guild_query_records(NULL, tail, rows,
                                          VM_NET_MOCK_GUILD_PAGE_MAX, &rowCount))
    {
        return 0;
    }
    for (u32 i = 0; i < rowCount; ++i)
    {
        const vm_net_mock_guild_record *guild = &rows[i];
        /* The object blob accessor leaves its leading len16 in front of the
         * stream.  The first i32 reader consumes that len16 as its tag, so the
         * first row id must be raw BE32.  Later row ids remain tagged. */
        bool wroteGuildId = i == 0 ?
                            vm_net_mock_put_be32(faction, sizeof(faction), &factionLen,
                                                 guild->guildId) :
                            vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen,
                                                    guild->guildId);
        if (!wroteGuildId ||
            !vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen,
                                     guild->guildLevel) ||
            !vm_net_mock_seq_put_string(faction, sizeof(faction), &factionLen, guild->guildName) ||
            !vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen,
                                     guild->memberCount) ||
            !vm_net_mock_seq_put_string(faction, sizeof(faction), &factionLen, guild->leaderName))
        {
            return 0;
        }
    }
    if (factionLen > 0xffffu || rowCount > 0xffu || totalPages > 0xffffu ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 21, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "fid", currentGuildId) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "name", "") ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "allpgs", (u16)totalPages) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "num", (u8)rowCount) ||
        !vm_net_mock_put_object_blob(out, outCap, &pos, "faction", faction, (u16)factionLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_page request=10/%u index=%u page_size=%u "
           "fid=%u total=%u rows=%u allpgs=%u faction_len=%u row_numbers=u32 "
           "response=10/21 resp=%u "
           "evidence=JianghuOL.CBE:0x010414DC+0x0103F566\n",
           requestSubtype, index, pageSize, currentGuildId,
           totalGuilds, rowCount, totalPages, factionLen, pos);
    return pos;
}

static const char *vm_net_mock_guild_rank_default_title(u8 memberRank)
{
    switch (memberRank)
    {
    case 1:
        return "\xb0\xef\xd6\xf7"; /* GBK: 帮主 */
    case 2:
        return "\xb9\xdc\xc0\xed"; /* GBK: 管理 */
    default:
        return "\xb3\xc9\xd4\xb1"; /* GBK: 成员 */
    }
}

static u8 vm_net_mock_guild_member_online(const vm_net_mock_guild_member_record *member)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;
    if (member == NULL)
        return 0;
    while (session != NULL)
    {
        if (session->roleOnline && session->onlineRoleId == member->roleId &&
            strcmp(session->accountId, member->accountId) == 0)
        {
            return 1;
        }
        session = session->next;
    }
    return 0;
}

/* Guild application approval is persisted immediately, but an already-online
 * applicant still has the old actor+104 guild id in the CBE until it receives
 * the native 10/36 status update.  Queue that packet for the applicant's next
 * ordinary scene-sync poll; offline roles will obtain the same state from the
 * persisted actorinfo during their next login. */
static bool vm_mock_service_enqueue_guild_application_notice(
    const vm_net_mock_guild_application_record *application,
    const vm_net_mock_guild_record *guild,
    u8 actionType,
    const vm_net_mock_role_state *requester)
{
    vm_mock_service_client_session *target = g_vm_mock_service_client_sessions;
    vm_mock_service_client_session *source = vm_mock_service_get_active_client_session();
    vm_mock_service_social_notice *slot = NULL;
    u8 noticeType = actionType == 1
                        ? VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED
                        : VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_REJECTED;

    if (application == NULL || guild == NULL || guild->guildId == 0 ||
        application->roleId == 0 || application->accountId[0] == 0 ||
        (actionType != 1 && actionType != 2))
    {
        return false;
    }
    while (target != NULL)
    {
        if (target->roleOnline && target->onlinePresenceValid &&
            vm_mock_service_session_presence_is_recent(target) &&
            target->onlineRoleId == application->roleId &&
            strcmp(target->accountId, application->accountId) == 0)
        {
            break;
        }
        target = target->next;
    }
    if (target == NULL)
    {
        printf("[info][mock-service] guild_application_notice_queue "
               "target=%s/%u action=%s guild=%u queued=0 reason=target-offline\n",
               application->accountId, application->roleId,
               vm_mock_service_social_notice_name(noticeType), guild->guildId);
        return false;
    }

    for (u32 i = 0; i < VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX; ++i)
    {
        vm_mock_service_social_notice *entry = &target->socialNotices[i];
        if ((entry->type == VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED ||
             entry->type == VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_REJECTED) &&
            entry->guildId == guild->guildId)
        {
            slot = entry;
            break;
        }
        if (slot == NULL && entry->type == VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE)
            slot = entry;
    }
    if (slot == NULL)
    {
        printf("[warn][mock-service] guild_application_notice_queue "
               "target=%08x/%u action=%s guild=%u queued=0 reason=queue-full\n",
               target->clientId, application->roleId,
               vm_mock_service_social_notice_name(noticeType), guild->guildId);
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    slot->type = noticeType;
    slot->result = actionType;
    slot->sourceClientId = source ? source->clientId : 0;
    slot->sourceRoleId = requester ? requester->roleId : 0;
    slot->sourceLevel = (u16)(requester && requester->level ? requester->level : 1);
    slot->sourceJob = requester && requester->job ? requester->job : 1;
    slot->sourceSex = requester && requester->sex <= 1 ? requester->sex : 0;
    snprintf(slot->sourceAccountId, sizeof(slot->sourceAccountId), "%s",
             g_vm_mock_service_active_account_id ?
                 g_vm_mock_service_active_account_id : "");
    snprintf(slot->sourceName, sizeof(slot->sourceName), "%s",
             requester && requester->name[0] ? requester->name : "Guild");
    slot->guildId = guild->guildId;
    slot->guildStatus = actionType == 1 ? 3 : 0;
    snprintf(slot->guildName, sizeof(slot->guildName), "%s", guild->guildName);
    slot->queuedTick = g_schedulerTick;
    printf("[info][mock-service] guild_application_notice_queue "
           "target=%08x/%u action=%s guild=%u name=%s status=%u queued=1\n",
           target->clientId, application->roleId,
           vm_mock_service_social_notice_name(noticeType), guild->guildId,
           guild->guildName, slot->guildStatus);
    return true;
}

static u32 vm_net_mock_build_guild_member_page_response(const u8 *request, u32 requestLen,
                                                          u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_guild_record guild;
    vm_net_mock_guild_record membership;
    vm_net_mock_guild_member_record rows[VM_NET_MOCK_GUILD_PAGE_MAX];
    u8 playerInfo[8192];
    u32 index = 0;
    u32 pageSize = 0;
    u32 offset = 0;
    u32 totalPages = 1;
    u32 rowCount = 0;
    u32 playerInfoLen = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 requestSubtype = 0;
    u8 result = 2;

    memset(&guild, 0, sizeof(guild));
    memset(&membership, 0, sizeof(membership));
    memset(rows, 0, sizeof(rows));
    memset(playerInfo, 0, sizeof(playerInfo));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_member_page_request(request, requestLen, &index, &pageSize,
                                                   &requestSubtype))
    {
        return 0;
    }
    offset = index > 0 ? index - 1u : 0;
    if (role != NULL &&
        vm_net_mock_guild_find_role_membership(role->roleId, &membership, NULL) &&
        membership.guildId != 0 &&
        vm_net_mock_guild_find_by_id(membership.guildId, &guild))
    {
        if (guild.memberCount > 0)
            totalPages = (guild.memberCount + pageSize - 1u) / pageSize;
        if (totalPages > 0xffffu)
            totalPages = 0xffffu;
        if (!vm_net_mock_guild_query_members(guild.guildId, offset, pageSize,
                                              rows, VM_NET_MOCK_GUILD_PAGE_MAX,
                                              &rowCount))
        {
            return 0;
        }
        for (u32 i = 0; i < rowCount; ++i)
        {
            vm_net_mock_guild_member_record *member = &rows[i];
            const char *title = member->memberTitle[0] != 0 ?
                                member->memberTitle :
                                vm_net_mock_guild_rank_default_title(member->memberRank);
            member->online = vm_net_mock_guild_member_online(member);
            /* As with every blob-backed row stream, the first id reuses the
             * field's leading len16 and is raw BE32; subsequent ids are tagged. */
            bool wroteRoleId = i == 0 ?
                               vm_net_mock_put_be32(playerInfo, sizeof(playerInfo),
                                                    &playerInfoLen, member->roleId) :
                               vm_net_mock_seq_put_u32(playerInfo, sizeof(playerInfo),
                                                       &playerInfoLen, member->roleId);
            if (!wroteRoleId ||
                !vm_net_mock_seq_put_u8(playerInfo, sizeof(playerInfo), &playerInfoLen,
                                        member->online) ||
                !vm_net_mock_seq_put_string(playerInfo, sizeof(playerInfo), &playerInfoLen,
                                            member->roleName) ||
                !vm_net_mock_seq_put_string(playerInfo, sizeof(playerInfo), &playerInfoLen,
                                            title) ||
                !vm_net_mock_seq_put_u32(playerInfo, sizeof(playerInfo), &playerInfoLen,
                                         member->memberRank) ||
                !vm_net_mock_seq_put_u32(playerInfo, sizeof(playerInfo), &playerInfoLen,
                                         member->level))
            {
                return 0;
            }
        }
        result = 1;
    }
    if (playerInfoLen > 0xffffu || rowCount > 0xffu ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 20, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        (result == 1 &&
         (!vm_net_mock_put_object_u32(out, outCap, &pos, "cnum", guild.memberCount) ||
          !vm_net_mock_put_object_u32(out, outCap, &pos, "mnum", guild.memberLimit) ||
          !vm_net_mock_put_object_u32(out, outCap, &pos, "allpgs", totalPages) ||
          !vm_net_mock_put_object_u8(out, outCap, &pos, "num", (u8)rowCount) ||
          !vm_net_mock_put_object_blob(out, outCap, &pos, "playerinfo",
                                       playerInfo, (u16)playerInfoLen))))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_member_page request=10/%u role=%u guild=%u index=%u page_size=%u "
           "members=%u/%u rows=%u allpgs=%u playerinfo_len=%u result=%u resp=%u "
           "evidence=JianghuOL.CBE:0x01041502+0x0104214E+0x01041D66\n",
           requestSubtype, role ? role->roleId : 0, guild.guildId, index, pageSize,
           guild.memberCount, guild.memberLimit, rowCount, totalPages,
           playerInfoLen, result, pos);
    vm_autotest_note("mock_guild_member_page role=%u guild=%u rows=%u response=10/20 "
                     "evidence=JianghuOL.CBE:0x0104214E+0x01041D66\n",
                     role ? role->roleId : 0, guild.guildId, rowCount);
    return pos;
}

static u32 vm_net_mock_build_guild_rank_page_response(const u8 *request, u32 requestLen,
                                                       u8 *out, u32 outCap)
{
    static const char emptyPosition[] = "\xce\xde"; /* GBK: 无 */
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_guild_record membership;
    vm_net_mock_guild_member_record members[VM_NET_MOCK_GUILD_PAGE_MAX];
    u8 rankInfo[1024];
    u32 index = 0;
    u32 pageSize = 0;
    u32 memberCount = 0;
    u32 rankInfoLen = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 requestSubtype = 0;
    u8 requesterRank = 0;
    u8 result = 2;

    memset(&membership, 0, sizeof(membership));
    memset(members, 0, sizeof(members));
    memset(rankInfo, 0, sizeof(rankInfo));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_member_page_request(request, requestLen,
                                                   &index, &pageSize,
                                                   &requestSubtype) ||
        requestSubtype != 37)
    {
        return 0;
    }
    if (role != NULL &&
        vm_net_mock_guild_find_role_membership(role->roleId,
                                                &membership, &requesterRank) &&
        membership.guildId != 0 &&
        vm_net_mock_guild_query_members(membership.guildId, 0,
                                         VM_NET_MOCK_GUILD_PAGE_MAX,
                                         members, VM_NET_MOCK_GUILD_PAGE_MAX,
                                         &memberCount))
    {
        for (u32 position = 1; position <= VM_NET_MOCK_GUILD_POSITION_COUNT; ++position)
        {
            const vm_net_mock_guild_member_record *occupant = NULL;
            const char *occupantName = emptyPosition;
            u32 occupantRoleId = 0;
            for (u32 i = 0; i < memberCount; ++i)
            {
                if (members[i].memberRank == position)
                {
                    occupant = &members[i];
                    break;
                }
            }
            if (occupant != NULL)
            {
                occupantName = occupant->roleName;
                occupantRoleId = occupant->roleId;
            }
            /* HandleGuildRankResponse's first stream u32 consumes the blob
             * field's leading len16 as its tag.  Keep only that first position
             * id raw; all later sequence values retain their explicit tags. */
            bool wrotePosition = position == 1 ?
                vm_net_mock_put_be32(rankInfo, sizeof(rankInfo), &rankInfoLen, position) :
                vm_net_mock_seq_put_u32(rankInfo, sizeof(rankInfo), &rankInfoLen, position);
            if (!wrotePosition ||
                !vm_net_mock_seq_put_string(rankInfo, sizeof(rankInfo), &rankInfoLen,
                                            vm_net_mock_guild_rank_default_title((u8)position)) ||
                !vm_net_mock_seq_put_string(rankInfo, sizeof(rankInfo), &rankInfoLen,
                                            occupantName) ||
                !vm_net_mock_seq_put_u32(rankInfo, sizeof(rankInfo), &rankInfoLen,
                                         occupantRoleId))
            {
                return 0;
            }
        }
        result = 1;
    }
    if (rankInfoLen > 0xffffu ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 37, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        (result == 1 &&
         (!vm_net_mock_put_object_u8(out, outCap, &pos, "flag",
                                     requesterRank <= 2 ? requesterRank : 0) ||
          !vm_net_mock_put_object_u32(out, outCap, &pos, "allpgs", 1) ||
          !vm_net_mock_put_object_u8(out, outCap, &pos, "num",
                                    VM_NET_MOCK_GUILD_POSITION_COUNT) ||
          !vm_net_mock_put_object_blob(out, outCap, &pos, "rank",
                                       rankInfo, (u16)rankInfoLen))))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_rank_page role=%u guild=%u flag=%u positions=%u "
           "rank_len=%u result=%u response=10/37 resp=%u "
           "evidence=JianghuOL.CBE:0x01041130+0x0104154A+0x01042198\n",
           role ? role->roleId : 0, membership.guildId, requesterRank,
           VM_NET_MOCK_GUILD_POSITION_COUNT, rankInfoLen, result, pos);
    return pos;
}

/* Request 10/37 is shared by the initial rank table and the member-selection
 * page.  The active screen consumes one object and ignores the other; sending
 * both mirrors the existing 10/20 member/list compatibility strategy without
 * guessing client UI state on the service thread. */
static u32 vm_net_mock_build_guild_rank_compat_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u8 memberPacket[16384];
    u8 rankPacket[4096];
    u32 index = 0;
    u32 pageSize = 0;
    u32 memberLen = 0;
    u32 rankLen = 0;
    u32 pos = 5;
    u8 requestSubtype = 0;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_member_page_request(request, requestLen,
                                                   &index, &pageSize,
                                                   &requestSubtype) ||
        requestSubtype != 37)
    {
        return 0;
    }
    memset(memberPacket, 0, sizeof(memberPacket));
    memset(rankPacket, 0, sizeof(rankPacket));
    memberLen = vm_net_mock_build_guild_member_page_response(
        request, requestLen, memberPacket, sizeof(memberPacket));
    rankLen = vm_net_mock_build_guild_rank_page_response(
        request, requestLen, rankPacket, sizeof(rankPacket));
    if (memberLen <= 5 || rankLen <= 5 ||
        pos + (memberLen - 5) + (rankLen - 5) > outCap)
    {
        return 0;
    }
    memcpy(out + pos, memberPacket + 5, memberLen - 5);
    pos += memberLen - 5;
    memcpy(out + pos, rankPacket + 5, rankLen - 5);
    pos += rankLen - 5;
    vm_net_mock_finish_wt_packet(out, pos, 2);
    printf("[info][network] mock_guild_rank_compat request=10/37 index=%u page_size=%u "
           "objects=2 member=10/20 rank=10/37 resp=%u "
           "evidence=JianghuOL.CBE:0x0104149E+0x01042CB2\n",
           index, pageSize, pos);
    return pos;
}

/* Both the joined-guild member page and the other-guild list use request
 * 10/20 {index,pagesize}.  Their active screen callbacks deliberately consume
 * different response subtypes: the member page handles 10/20 while the guild
 * list handles 10/21.  A packet containing both objects is therefore the only
 * stateless way to preserve both client flows, including page-one navigation,
 * without guessing the current UI from server-side timing. */
static u32 vm_net_mock_build_guild_page_compat_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u8 memberPacket[16384];
    u8 guildPacket[16384];
    u32 index = 0;
    u32 pageSize = 0;
    u32 memberLen = 0;
    u32 guildLen = 0;
    u32 pos = 5;
    u8 requestSubtype = 0;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_page_request(request, requestLen, &index, &pageSize,
                                           &requestSubtype) ||
        requestSubtype != 20)
    {
        return 0;
    }
    memset(memberPacket, 0, sizeof(memberPacket));
    memset(guildPacket, 0, sizeof(guildPacket));
    memberLen = vm_net_mock_build_guild_member_page_response(
        request, requestLen, memberPacket, sizeof(memberPacket));
    guildLen = vm_net_mock_build_guild_page_response(
        request, requestLen, guildPacket, sizeof(guildPacket));
    if (memberLen <= 5 || guildLen <= 5 ||
        pos + (memberLen - 5) + (guildLen - 5) > outCap)
    {
        return 0;
    }
    memcpy(out + pos, memberPacket + 5, memberLen - 5);
    pos += memberLen - 5;
    memcpy(out + pos, guildPacket + 5, guildLen - 5);
    pos += guildLen - 5;
    vm_net_mock_finish_wt_packet(out, pos, 2);
    printf("[info][network] mock_guild_page_compat request=10/20 index=%u page_size=%u "
           "objects=2 member=10/20 guild=10/21 resp=%u "
           "evidence=JianghuOL.CBE:0x01041502+0x0103FC00+0x010420B8\n",
           index, pageSize, pos);
    return pos;
}

static u8 vm_net_mock_apply_guild_leave(u32 requestedRoleId,
                                         vm_net_mock_role_state *role,
                                         u32 *guildIdOut,
                                         u8 *memberRankOut,
                                         bool *disbandedOut)
{
    vm_net_mock_guild_record guild;
    char accountHex[129];
    char query[1024];
    char tableAndWhere[512];
    u32 remainingRows = 0;
    u32 memberCount = 0;
    u8 memberRank = 0;
    bool transactionStarted = false;
    bool disbanded = false;

    if (guildIdOut)
        *guildIdOut = 0;
    if (memberRankOut)
        *memberRankOut = 0;
    if (disbandedOut)
        *disbandedOut = false;
    memset(&guild, 0, sizeof(guild));
    memset(accountHex, 0, sizeof(accountHex));
    memset(query, 0, sizeof(query));
    memset(tableAndWhere, 0, sizeof(tableAndWhere));

    if (role == NULL || requestedRoleId == 0 || requestedRoleId != role->roleId ||
        !vm_net_mock_guild_find_role_membership(role->roleId, &guild, &memberRank) ||
        guild.guildId == 0 || !vm_net_mock_mysql_account_hex(accountHex))
    {
        return 0;
    }
    if (guildIdOut)
        *guildIdOut = guild.guildId;
    if (memberRankOut)
        *memberRankOut = memberRank;

    snprintf(tableAndWhere, sizeof(tableAndWhere),
             "guild_members WHERE guild_id=%u", guild.guildId);
    if (!vm_net_mock_guild_count(tableAndWhere, &memberCount) || memberCount == 0)
        return 0;

    /* No separate dissolve-guild action exists in this client.  Let the sole
     * leader leave by deleting the guild (FK cascades members/applications),
     * but reject leaving a populated guild until leadership is handed over. */
    if (memberRank == 1 && memberCount != 1)
        return 0;

    if (!vm_mysql_exec("START TRANSACTION"))
        return 0;
    transactionStarted = true;

    snprintf(query, sizeof(query),
             "DELETE FROM guild_applications "
             "WHERE applicant_account_id=CAST(X'%s' AS CHAR) AND applicant_role_id=%u",
             accountHex, role->roleId);
    if (!vm_mysql_exec(query))
        goto failed;

    if (memberRank == 1)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM guilds WHERE guild_id=%u "
                 "AND leader_account_id=CAST(X'%s' AS CHAR) AND leader_role_id=%u",
                 guild.guildId, accountHex, role->roleId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(tableAndWhere, sizeof(tableAndWhere),
                 "guilds WHERE guild_id=%u", guild.guildId);
        if (!vm_net_mock_guild_count(tableAndWhere, &remainingRows) || remainingRows != 0)
            goto failed;
        disbanded = true;
    }
    else
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM guild_members WHERE guild_id=%u "
                 "AND account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND member_rank=%u",
                 guild.guildId, accountHex, role->roleId, memberRank);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(tableAndWhere, sizeof(tableAndWhere),
                 "guild_members WHERE guild_id=%u "
                 "AND account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
                 guild.guildId, accountHex, role->roleId);
        if (!vm_net_mock_guild_count(tableAndWhere, &remainingRows) || remainingRows != 0)
            goto failed;
    }

    if (!vm_mysql_exec("COMMIT"))
        goto failed;
    if (g_vm_mock_service_active_account != NULL)
        g_vm_mock_service_active_account->selectedGuildId = 0;
    if (disbandedOut)
        *disbandedOut = disbanded;
    return 1;

failed:
    printf("[error][network] mock_guild_leave_db_failed guild=%u role=%u rank=%u "
           "members=%u error=%s\n",
           guild.guildId, role ? role->roleId : 0, memberRank,
           memberCount, vm_mysql_last_error());
    if (transactionStarted)
        vm_mysql_exec("ROLLBACK");
    return 0;
}

static u32 vm_net_mock_build_guild_leave_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 requestedRoleId = 0;
    u32 guildId = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 memberRank = 0;
    u8 result = 0;
    bool disbanded = false;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_guild_leave_request(request, requestLen, &requestedRoleId) ||
        role == NULL || requestedRoleId != role->roleId)
    {
        return 0;
    }
    result = vm_net_mock_apply_guild_leave(requestedRoleId, role, &guildId,
                                            &memberRank, &disbanded);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 22, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_leave role=%u requested=%u guild=%u rank=%u "
           "result=%u disbanded=%u response=10/22 resp=%u "
           "evidence=JianghuOL.CBE:0x0103D2FE+0x0103D5F2+0x0103DABC\n",
           role ? role->roleId : 0, requestedRoleId, guildId, memberRank,
           result, disbanded ? 1u : 0u, pos);
    return pos;
}

static u32 vm_net_mock_build_guild_detail_response(const u8 *request, u32 requestLen,
                                                    u8 *out, u32 outCap)
{
    vm_net_mock_guild_record guild;
    u8 faction[1024];
    char memberCount[32];
    u32 guildId = 0;
    u32 resolvedGuildId = 0;
    u32 factionLen = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 result = 1;

    memset(&guild, 0, sizeof(guild));
    memset(faction, 0, sizeof(faction));
    memset(memberCount, 0, sizeof(memberCount));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_detail_request(request, requestLen, &guildId))
    {
        return 0;
    }
    resolvedGuildId = guildId;
    if (!vm_net_mock_guild_find_by_id(resolvedGuildId, &guild))
    {
        result = 2;
    }
    if (result == 1)
    {
        snprintf(memberCount, sizeof(memberCount), "%u/%u",
                 guild.memberCount, guild.memberLimit);
        /* HandleFactionInfoResponse's first i32 reader consumes the blob
         * field's own len16 prefix.  Write the id raw; adding a tagged-u32 here
         * shifts every following field by two bytes and also makes the parsed
         * id differ from actor+104, which disables the normal back state. */
        if (!vm_net_mock_put_be32(faction, sizeof(faction), &factionLen, guild.guildId) ||
            !vm_net_mock_seq_put_string(faction, sizeof(faction), &factionLen, guild.guildName) ||
            !vm_net_mock_seq_put_string(faction, sizeof(faction), &factionLen, guild.leaderName) ||
            !vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen, guild.guildLevel) ||
            !vm_net_mock_seq_put_string(faction, sizeof(faction), &factionLen, memberCount) ||
            !vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen, guild.guildMoney) ||
            !vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen, guild.prosperity) ||
            !vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen, guild.actionPower) ||
            !vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen, guild.researchPower) ||
            !vm_net_mock_seq_put_u32(faction, sizeof(faction), &factionLen, guild.construction) ||
            !vm_net_mock_seq_put_string(faction, sizeof(faction), &factionLen,
                                        guild.currentConstruction) ||
            !vm_net_mock_seq_put_string(faction, sizeof(faction), &factionLen, guild.notice))
        {
            return 0;
        }
        if (g_vm_mock_service_active_account != NULL)
            g_vm_mock_service_active_account->selectedGuildId = resolvedGuildId;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 23, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        (result == 1 && !vm_net_mock_put_object_blob(out, outCap, &pos, "faction",
                                                     faction, (u16)factionLen)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_detail request_id=%u guild=%u result=%u name=%s members=%u/%u faction_len=%u first_id=raw-be32 resp=%u evidence=JianghuOL.CBE:0x0103D6BE+0x0103F088\n",
           guildId, resolvedGuildId, result, result == 1 ? guild.guildName : "-",
           guild.memberCount, guild.memberLimit, factionLen, pos);
    return pos;
}

static bool vm_net_mock_guild_create_name_valid(const char *name)
{
    size_t length = 0;
    size_t offset = 0;
    if (name == NULL)
        return false;
    length = vm_mock_mysql_bounded_strlen(name, VM_NET_MOCK_GUILD_NAME_SIZE);
    if (length == 0 || length > 12 || length >= VM_NET_MOCK_GUILD_NAME_SIZE)
        return false;
    while (offset < length)
    {
        const unsigned char lead = (unsigned char)name[offset];
        if (lead < 0x20u || lead == 0x7fu || lead == ' ')
            return false;
        if (lead < 0x80u)
        {
            ++offset;
            continue;
        }
        if (offset + 1 >= length)
            return false;
        const unsigned char trail = (unsigned char)name[offset + 1];
        if (trail < 0x40u || trail == 0x7fu)
            return false;
        offset += 2;
    }
    return true;
}

static bool vm_net_mock_guild_name_exists(const char *name, bool *existsOut)
{
    char nameHex[VM_NET_MOCK_GUILD_NAME_SIZE * 2 + 1];
    char query[512];
    size_t nameLen = 0;
    vm_mock_mysql_guild_u32_context context;
    if (existsOut)
        *existsOut = false;
    if (!vm_net_mock_guild_create_name_valid(name))
        return false;
    nameLen = vm_mock_mysql_bounded_strlen(name, VM_NET_MOCK_GUILD_NAME_SIZE);
    if (vm_mysql_hex_encode(name, nameLen, nameHex, sizeof(nameHex)) == 0)
        return false;
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM guilds WHERE guild_name=X'%s'", nameHex);
    if (!vm_net_mock_guild_mysql_query(query, vm_mock_mysql_guild_u32_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (existsOut)
        *existsOut = context.value != 0;
    return true;
}

static u32 vm_net_mock_build_guild_create_start_response(const u8 *request, u32 requestLen,
                                                          u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_guild_record membership;
    u32 objectStart = 0;
    u32 pos = 5;
    bool alreadyJoined = false;
    u8 responseSubtype = 27;
    u8 result = 1;

    memset(&membership, 0, sizeof(membership));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_create_start_request(request, requestLen))
    {
        return 0;
    }
    if (g_vm_mock_service_active_account != NULL)
    {
        g_vm_mock_service_active_account->pendingGuildCreateNameValid = false;
        g_vm_mock_service_active_account->pendingGuildCreateName[0] = 0;
    }
    alreadyJoined = role != NULL &&
                    vm_net_mock_guild_find_role_membership(role->roleId, &membership, NULL);
    if (role == NULL)
    {
        result = 0;
    }
    else if (alreadyJoined)
    {
        /* The same 10/30 action is used by a joined role for the guild-scene
         * path.  Result 4 is the client's non-destructive "already here / no
         * transfer needed" branch; never reopen the create-name editor. */
        responseSubtype = 30;
        result = 4;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, responseSubtype, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        (responseSubtype == 30 &&
         !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_create_start role=%u joined=%u response=10/%u result=%u resp=%u evidence=JianghuOL.CBE:0x0103D15A+0x0103C50E(subtype27)\n",
           role ? role->roleId : 0, alreadyJoined ? 1u : 0u,
           responseSubtype, result, pos);
    return pos;
}

static u32 vm_net_mock_build_guild_create_name_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    static const char confirmText[] =
        "\xb0\xef\xc5\xc9\xc3\xfb\xb3\xc6\xc8\xb7\xc8\xcf\xce\xde\xce\xf3\xa3\xac"
        "\xca\xc7\xb7\xf1\xb4\xb4\xbd\xa8\xa3\xbf"; /* GBK: 帮派名称确认无误，是否创建？ */
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_guild_record membership;
    char name[VM_NET_MOCK_GUILD_NAME_SIZE];
    u32 objectStart = 0;
    u32 pos = 5;
    u8 result = 1;
    bool exists = false;

    memset(&membership, 0, sizeof(membership));
    memset(name, 0, sizeof(name));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_guild_name_request(request, requestLen, 28,
                                              name, sizeof(name)))
    {
        return 0;
    }
    if (role == NULL || g_vm_mock_service_active_account == NULL ||
        vm_net_mock_guild_find_role_membership(role->roleId, &membership, NULL))
    {
        result = 3;
    }
    else if (!vm_net_mock_guild_create_name_valid(name))
    {
        result = 2;
    }
    else if (!vm_net_mock_guild_name_exists(name, &exists))
    {
        result = 3;
    }
    else if (exists)
    {
        result = 2;
    }
    if (result == 1)
    {
        snprintf(g_vm_mock_service_active_account->pendingGuildCreateName,
                 sizeof(g_vm_mock_service_active_account->pendingGuildCreateName),
                 "%s", name);
        g_vm_mock_service_active_account->pendingGuildCreateNameValid = true;
    }
    else if (g_vm_mock_service_active_account != NULL)
    {
        g_vm_mock_service_active_account->pendingGuildCreateNameValid = false;
        g_vm_mock_service_active_account->pendingGuildCreateName[0] = 0;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 28, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        (result == 1 &&
         !vm_net_mock_put_object_string(out, outCap, &pos, "info", confirmText)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_create_name role=%u name=%s exists=%u result=%u resp=%u evidence=JianghuOL.CBE:0x0103CCA8+0x0103C27E\n",
           role ? role->roleId : 0, name, exists ? 1u : 0u, result, pos);
    return pos;
}

static bool vm_net_mock_create_guild(const char *name,
                                     vm_net_mock_role_state *role,
                                     u32 *guildIdOut,
                                     const char **errorTextOut)
{
    static const char invalidNameText[] =
        "\xb0\xef\xc5\xc9\xc3\xfb\xb3\xc6\xce\xde\xd0\xa7\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xca\xe4\xc8\xeb";
    static const char duplicateNameText[] =
        "\xb0\xef\xc5\xc9\xc3\xfb\xb3\xc6\xd2\xd1\xb4\xe6\xd4\xda\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xca\xe4\xc8\xeb";
    static const char alreadyJoinedText[] =
        "\xc4\xe3\xd2\xd1\xbe\xad\xbc\xd3\xc8\xeb\xb0\xef\xc5\xc9\xa3\xac\xb2\xbb\xc4\xdc\xd6\xd8\xb8\xb4\xb4\xb4\xbd\xa8";
    static const char retryText[] =
        "\xb4\xb4\xbd\xa8\xb0\xef\xc5\xc9\xca\xa7\xb0\xdc\xa3\xac\xc7\xeb\xc9\xd4\xba\xf3\xd6\xd8\xca\xd4";
    vm_net_mock_guild_record membership;
    vm_mock_mysql_guild_u32_context idContext;
    char accountHex[129];
    char nameHex[VM_NET_MOCK_GUILD_NAME_SIZE * 2 + 1];
    char roleNameHex[sizeof(role->name) * 2 + 1];
    char query[2048];
    size_t nameLen = 0;
    size_t roleNameLen = 0;
    bool exists = false;
    bool transactionStarted = false;
    u32 guildId = 0;

    if (guildIdOut)
        *guildIdOut = 0;
    if (errorTextOut)
        *errorTextOut = retryText;
    memset(&membership, 0, sizeof(membership));
    if (role == NULL || !vm_net_mock_guild_create_name_valid(name))
    {
        if (errorTextOut)
            *errorTextOut = invalidNameText;
        return false;
    }
    if (vm_net_mock_guild_find_role_membership(role->roleId, &membership, NULL))
    {
        if (errorTextOut)
            *errorTextOut = alreadyJoinedText;
        return false;
    }
    if (!vm_net_mock_guild_name_exists(name, &exists))
        return false;
    if (exists)
    {
        if (errorTextOut)
            *errorTextOut = duplicateNameText;
        return false;
    }
    nameLen = vm_mock_mysql_bounded_strlen(name, VM_NET_MOCK_GUILD_NAME_SIZE);
    roleNameLen = vm_mock_mysql_bounded_strlen(role->name, sizeof(role->name));
    if (!vm_net_mock_mysql_account_hex(accountHex) || roleNameLen == 0 ||
        roleNameLen >= sizeof(role->name) ||
        vm_mysql_hex_encode(name, nameLen, nameHex, sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(role->name, roleNameLen,
                            roleNameHex, sizeof(roleNameHex)) == 0)
    {
        return false;
    }
    if (!vm_mysql_exec("START TRANSACTION"))
        return false;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "INSERT INTO guilds(guild_name,leader_account_id,leader_role_id,leader_role_name) "
             "VALUES(X'%s',CAST(X'%s' AS CHAR),%u,X'%s')",
             nameHex, accountHex, role->roleId, roleNameHex);
    if (!vm_mysql_exec(query))
        goto failed;
    memset(&idContext, 0, sizeof(idContext));
    if (!vm_net_mock_guild_mysql_query("SELECT LAST_INSERT_ID()", vm_mock_mysql_guild_u32_row, &idContext) ||
        idContext.invalid || !idContext.found || idContext.value == 0)
    {
        goto failed;
    }
    guildId = idContext.value;
    snprintf(query, sizeof(query),
             "INSERT INTO guild_members(guild_id,account_id,role_id,role_name,member_rank) "
             "VALUES(%u,CAST(X'%s' AS CHAR),%u,X'%s',1)",
             guildId, accountHex, role->roleId, roleNameHex);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "DELETE FROM guild_applications WHERE applicant_account_id=CAST(X'%s' AS CHAR) "
             "AND applicant_role_id=%u",
             accountHex, role->roleId);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
    if (guildIdOut)
        *guildIdOut = guildId;
    if (errorTextOut)
        *errorTextOut = NULL;
    return true;

failed:
    printf("[error][network] mock_guild_create_db_failed role=%u name=%s error=%s\n",
           role ? role->roleId : 0, name ? name : "-", vm_mysql_last_error());
    if (transactionStarted)
        vm_mysql_exec("ROLLBACK");
    return false;
}

static u32 vm_net_mock_build_guild_create_commit_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    static const char invalidNameText[] =
        "\xb0\xef\xc5\xc9\xc3\xfb\xb3\xc6\xce\xde\xd0\xa7\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xca\xe4\xc8\xeb";
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    char name[VM_NET_MOCK_GUILD_NAME_SIZE];
    const char *errorText = invalidNameText;
    u32 guildId = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 result = 4;

    memset(name, 0, sizeof(name));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_guild_name_request(request, requestLen, 11,
                                              name, sizeof(name)))
    {
        return 0;
    }
    if (g_vm_mock_service_active_account != NULL &&
        g_vm_mock_service_active_account->pendingGuildCreateNameValid &&
        strcmp(name, g_vm_mock_service_active_account->pendingGuildCreateName) == 0 &&
        vm_net_mock_create_guild(name, role, &guildId, &errorText))
    {
        result = 1;
        g_vm_mock_service_active_account->selectedGuildId = guildId;
    }
    if (g_vm_mock_service_active_account != NULL)
    {
        g_vm_mock_service_active_account->pendingGuildCreateNameValid = false;
        g_vm_mock_service_active_account->pendingGuildCreateName[0] = 0;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 11, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        (result == 1 &&
         (!vm_net_mock_put_object_u32(out, outCap, &pos, "id", guildId) ||
          !vm_net_mock_put_object_u32(out, outCap, &pos, "money", role ? role->money : 0) ||
          !vm_net_mock_put_object_u8(out, outCap, &pos, "num", 0))) ||
        (result != 1 &&
         !vm_net_mock_put_object_string(out, outCap, &pos, "msg",
                                        errorText ? errorText : invalidNameText)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_create_commit role=%u guild=%u name=%s result=%u resp=%u evidence=JianghuOL.CBE:0x0103CCA8+0x0103C07E\n",
           role ? role->roleId : 0, guildId, name, result, pos);
    return pos;
}

static u8 vm_net_mock_submit_guild_application(u32 guildId,
                                                vm_net_mock_role_state *role,
                                                vm_net_mock_guild_record *guildOut)
{
    vm_net_mock_guild_record membership;
    vm_net_mock_guild_record guild;
    vm_mock_mysql_guild_u32_context context;
    char accountHex[129];
    char roleNameHex[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE * 2 + 1];
    char query[2048];
    size_t roleNameLen = 0;

    if (guildOut)
        memset(guildOut, 0, sizeof(*guildOut));
    if (guildId == 0 || role == NULL || !vm_net_mock_mysql_account_hex(accountHex) ||
        !vm_net_mock_guild_find_by_id(guildId, &guild))
    {
        return 7;
    }
    if (guildOut)
        *guildOut = guild;
    memset(&membership, 0, sizeof(membership));
    if (vm_net_mock_guild_find_role_membership(role->roleId, &membership, NULL))
        return 3;
    if (role->level < guild.minimumLevel)
        return 2;
    if (guild.memberCount >= guild.memberLimit)
        return 4;
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM guild_applications WHERE guild_id=%u AND status=0",
             guildId);
    if (!vm_net_mock_guild_mysql_query(query, vm_mock_mysql_guild_u32_row, &context) ||
        context.invalid || !context.found)
    {
        return 7;
    }
    if (context.value >= 50)
        return 5;
    roleNameLen = vm_mock_mysql_bounded_strlen(role->name, sizeof(role->name));
    if (roleNameLen == 0 || roleNameLen >= sizeof(role->name) ||
        vm_mysql_hex_encode(role->name, roleNameLen,
                            roleNameHex, sizeof(roleNameHex)) == 0)
    {
        return 7;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO guild_applications(guild_id,applicant_account_id,applicant_role_id,"
             "applicant_role_name,applicant_level,applicant_job,applicant_sex,status) "
             "VALUES(%u,CAST(X'%s' AS CHAR),%u,X'%s',%u,%u,%u,0) "
             "ON DUPLICATE KEY UPDATE guild_id=VALUES(guild_id),"
             "applicant_role_name=VALUES(applicant_role_name),applicant_level=VALUES(applicant_level),"
             "applicant_job=VALUES(applicant_job),applicant_sex=VALUES(applicant_sex),"
             "status=0,created_at=CURRENT_TIMESTAMP",
             guildId, accountHex, role->roleId, roleNameHex,
             role->level, role->job, role->sex);
    if (!vm_mysql_exec(query))
    {
        printf("[error][network] mock_guild_apply_db_failed guild=%u role=%u error=%s\n",
               guildId, role->roleId, vm_mysql_last_error());
        return 7;
    }
    return 1;
}

static u32 vm_net_mock_build_guild_apply_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_guild_record guild;
    u32 guildId = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 result = 7;

    memset(&guild, 0, sizeof(guild));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_guild_apply_request(request, requestLen, &guildId))
        return 0;
    result = vm_net_mock_submit_guild_application(guildId, role, &guild);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 31, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "level",
                                    (u16)(guild.minimumLevel > 0xffffu ? 0xffffu : guild.minimumLevel)) ||
        (result == 1 && !vm_net_mock_put_object_string(out, outCap, &pos, "name", guild.guildName)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_apply guild=%u role=%u result=%u name=%s resp=%u evidence=JianghuOL.CBE:0x0103F99E\n",
           guildId, role ? role->roleId : 0, result,
           guild.guildName[0] ? guild.guildName : "-", pos);
    return pos;
}

static u32 vm_net_mock_build_guild_application_page_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_guild_application_page_request page;
    vm_net_mock_guild_application_record rows[VM_NET_MOCK_GUILD_PAGE_MAX];
    vm_net_mock_guild_record guild;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u8 roleInfo[4096];
    char tableAndWhere[256];
    u32 roleInfoLen = 0;
    u32 rowCount = 0;
    u32 totalApplications = 0;
    u32 totalPages = 1;
    u32 offset = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 memberRank = 0;
    u8 result = 2;

    memset(&page, 0, sizeof(page));
    memset(rows, 0, sizeof(rows));
    memset(&guild, 0, sizeof(guild));
    memset(roleInfo, 0, sizeof(roleInfo));
    memset(tableAndWhere, 0, sizeof(tableAndWhere));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_guild_application_page_request(request, requestLen, &page))
    {
        return 0;
    }

    if (page.valid && role != NULL &&
        vm_net_mock_guild_find_role_membership(role->roleId, &guild, &memberRank) &&
        guild.guildId != 0 && memberRank > 0 && memberRank <= 2 &&
        vm_net_mock_guild_find_by_id(guild.guildId, &guild))
    {
        snprintf(tableAndWhere, sizeof(tableAndWhere),
                 "guild_applications WHERE guild_id=%u AND status=0", guild.guildId);
        if (vm_net_mock_guild_count(tableAndWhere, &totalApplications))
        {
            if (totalApplications == 0)
            {
                result = 4; /* HandleRoleListResponse: no current applicants. */
            }
            else
            {
                offset = page.index - 1;
                totalPages = (totalApplications + page.pageSize - 1) / page.pageSize;
                if (totalPages == 0)
                    totalPages = 1;
                if (vm_net_mock_guild_query_applications(
                        guild.guildId, offset, page.pageSize,
                        rows, VM_NET_MOCK_GUILD_PAGE_MAX, &rowCount))
                {
                    result = 1;
                    for (u32 i = 0; i < rowCount; ++i)
                    {
                        if (!vm_net_mock_seq_put_u32(roleInfo, sizeof(roleInfo),
                                                     &roleInfoLen, rows[i].roleId) ||
                            !vm_net_mock_seq_put_string(roleInfo, sizeof(roleInfo),
                                                        &roleInfoLen, rows[i].roleName) ||
                            !vm_net_mock_seq_put_u32(roleInfo, sizeof(roleInfo),
                                                     &roleInfoLen, rows[i].level))
                        {
                            result = 2;
                            roleInfoLen = 0;
                            rowCount = 0;
                            break;
                        }
                    }
                }
            }
        }
    }
    else if (page.valid)
    {
        result = 3; /* permission denied / requester is no longer management */
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 32, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        (result == 1 &&
         (!vm_net_mock_put_object_u32(out, outCap, &pos, "roles", guild.memberCount) ||
          !vm_net_mock_put_object_u32(out, outCap, &pos, "maxroles", guild.memberLimit) ||
          !vm_net_mock_put_object_u32(out, outCap, &pos, "allpgs", totalPages) ||
          !vm_net_mock_put_object_u32(out, outCap, &pos, "num", rowCount) ||
          !vm_net_mock_put_object_entry(out, outCap, &pos, "roleinfo",
                                         roleInfo, (u16)roleInfoLen))))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_application_page requester=%u guild=%u rank=%u "
           "index=%u page_size=%u members=%u/%u pending=%u rows=%u "
           "first_applicant=%u result=%u response=10/32 resp=%u "
           "evidence=JianghuOL.CBE:0x01040276+0x0103FF2C+0x01040AA8\n",
           role ? role->roleId : 0, guild.guildId, memberRank,
           page.index, page.pageSize, guild.memberCount, guild.memberLimit,
           totalApplications, rowCount, rowCount ? rows[0].roleId : 0,
           result, pos);
    return pos;
}

static u8 vm_net_mock_apply_guild_application_action(
    const vm_net_mock_guild_application_action *action,
    vm_net_mock_role_state *requester,
    u32 *guildIdOut, u8 *requesterRankOut)
{
    vm_net_mock_guild_application_record application;
    vm_net_mock_guild_member_record acceptedMember;
    vm_net_mock_guild_record guild;
    vm_net_mock_guild_record existingMembership;
    char accountHex[129];
    char query[1536];
    char tableAndWhere[512];
    u32 matchingRows = 0;
    u32 memberCount = 0;
    u8 requesterRank = 0;
    bool transactionStarted = false;

    if (guildIdOut)
        *guildIdOut = 0;
    if (requesterRankOut)
        *requesterRankOut = 0;
    memset(&application, 0, sizeof(application));
    memset(&acceptedMember, 0, sizeof(acceptedMember));
    memset(&guild, 0, sizeof(guild));
    memset(&existingMembership, 0, sizeof(existingMembership));
    memset(accountHex, 0, sizeof(accountHex));
    memset(query, 0, sizeof(query));
    memset(tableAndWhere, 0, sizeof(tableAndWhere));

    if (requester == NULL ||
        !vm_net_mock_guild_find_role_membership(requester->roleId,
                                                 &guild, &requesterRank) ||
        guild.guildId == 0)
    {
        return 2;
    }
    if (guildIdOut)
        *guildIdOut = guild.guildId;
    if (requesterRankOut)
        *requesterRankOut = requesterRank;
    if (requesterRank == 0 || requesterRank > 2)
        return 2;
    if (action == NULL || !action->valid)
        return 0;
    if (!vm_net_mock_guild_find_by_id(guild.guildId, &guild))
        return 0;
    if (!vm_net_mock_guild_find_pending_application(guild.guildId, action->roleId,
                                                     &application))
    {
        return 5;
    }
    if (action->type == 1)
    {
        if (vm_net_mock_guild_find_membership_for_account(application.accountId,
                                                           application.roleId,
                                                           &existingMembership, NULL))
        {
            return 4;
        }
        if (guild.memberCount >= guild.memberLimit)
            return 3;
    }
    if (vm_mysql_hex_encode(application.accountId,
                            vm_mock_mysql_bounded_strlen(application.accountId,
                                                         sizeof(application.accountId)),
                            accountHex, sizeof(accountHex)) == 0)
    {
        return 0;
    }

    if (!vm_mysql_exec("START TRANSACTION"))
        return 0;
    transactionStarted = true;

    /* Re-read the pending row after opening the transaction.  A second manager
     * may already have handled the same application between list and action. */
    if (!vm_net_mock_guild_find_pending_application(guild.guildId, action->roleId,
                                                     &application))
    {
        vm_mysql_exec("ROLLBACK");
        return 5;
    }
    if (action->type == 2)
    {
        snprintf(query, sizeof(query),
                 "UPDATE guild_applications SET status=2 "
                 "WHERE guild_id=%u AND applicant_account_id=CAST(X'%s' AS CHAR) "
                 "AND applicant_role_id=%u AND status=0",
                 guild.guildId, accountHex, application.roleId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(tableAndWhere, sizeof(tableAndWhere),
                 "guild_applications WHERE guild_id=%u "
                 "AND applicant_account_id=CAST(X'%s' AS CHAR) "
                 "AND applicant_role_id=%u AND status=2",
                 guild.guildId, accountHex, application.roleId);
        if (!vm_net_mock_guild_count(tableAndWhere, &matchingRows) || matchingRows != 1 ||
            !vm_mysql_exec("COMMIT"))
        {
            goto failed;
        }
        (void)vm_mock_service_enqueue_guild_application_notice(
            &application, &guild, action->type, requester);
        return 1;
    }

    memset(&existingMembership, 0, sizeof(existingMembership));
    if (vm_net_mock_guild_find_membership_for_account(application.accountId,
                                                       application.roleId,
                                                       &existingMembership, NULL))
    {
        vm_mysql_exec("ROLLBACK");
        return 4;
    }
    snprintf(tableAndWhere, sizeof(tableAndWhere),
             "guild_members WHERE guild_id=%u", guild.guildId);
    if (!vm_net_mock_guild_count(tableAndWhere, &memberCount))
        goto failed;
    if (memberCount >= guild.memberLimit)
    {
        vm_mysql_exec("ROLLBACK");
        return 3;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO guild_members(guild_id,account_id,role_id,role_name,member_rank,member_title) "
             "SELECT guild_id,applicant_account_id,applicant_role_id,applicant_role_name,3,'' "
             "FROM guild_applications WHERE guild_id=%u "
             "AND applicant_account_id=CAST(X'%s' AS CHAR) "
             "AND applicant_role_id=%u AND status=0",
             guild.guildId, accountHex, application.roleId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "UPDATE guild_applications SET status=1 "
             "WHERE guild_id=%u AND applicant_account_id=CAST(X'%s' AS CHAR) "
             "AND applicant_role_id=%u AND status=0",
             guild.guildId, accountHex, application.roleId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(tableAndWhere, sizeof(tableAndWhere),
             "guild_applications WHERE guild_id=%u "
             "AND applicant_account_id=CAST(X'%s' AS CHAR) "
             "AND applicant_role_id=%u AND status=1",
             guild.guildId, accountHex, application.roleId);
    if (!vm_net_mock_guild_count(tableAndWhere, &matchingRows) || matchingRows != 1 ||
        !vm_net_mock_guild_find_member(guild.guildId, application.roleId, &acceptedMember) ||
        strcmp(acceptedMember.accountId, application.accountId) != 0 ||
        !vm_mysql_exec("COMMIT"))
    {
        goto failed;
    }
    (void)vm_mock_service_enqueue_guild_application_notice(
        &application, &guild, action->type, requester);
    return 1;

failed:
    printf("[error][network] mock_guild_application_action_db_failed guild=%u "
           "requester=%u target=%u type=%u error=%s\n",
           guild.guildId, requester ? requester->roleId : 0,
           action ? action->roleId : 0, action ? action->type : 0,
           vm_mysql_last_error());
    if (transactionStarted)
        vm_mysql_exec("ROLLBACK");
    return 0;
}

static u32 vm_net_mock_build_guild_application_action_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_guild_application_action action;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 guildId = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 requesterRank = 0;
    u8 result = 0;

    memset(&action, 0, sizeof(action));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_guild_application_action(request, requestLen, &action))
    {
        return 0;
    }
    result = vm_net_mock_apply_guild_application_action(&action, role,
                                                         &guildId, &requesterRank);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10, 33, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", action.type))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_application_action requester=%u guild=%u rank=%u "
           "target=%u type=%u valid=%u result=%u response=10/33 resp=%u "
           "evidence=JianghuOL.CBE:0x010402BC+0x01040318+0x0104094A+0x01040AA8\n",
           role ? role->roleId : 0, guildId, requesterRank,
           action.roleId, action.type, action.valid ? 1u : 0u, result, pos);
    return pos;
}

static u32 vm_net_mock_build_guild_slogan_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_guild_record guild;
    char slogan[VM_NET_MOCK_GUILD_TEXT_SIZE];
    char sloganHex[VM_NET_MOCK_GUILD_NOTICE_MAX_BYTES * 2 + 1];
    char query[512];
    size_t sloganLen = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 rank = 0;
    u8 result = 0;
    u8 responseSubtype = 35;
    bool publish = false;
    bool sloganParsed = false;

    memset(&guild, 0, sizeof(guild));
    memset(slogan, 0, sizeof(slogan));
    memset(sloganHex, 0, sizeof(sloganHex));
    memset(query, 0, sizeof(query));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_guild_slogan_request(request, requestLen,
                                                &publish, &sloganParsed,
                                                slogan, sizeof(slogan)))
    {
        return 0;
    }

    if (role != NULL &&
        vm_net_mock_guild_find_role_membership(role->roleId, &guild, &rank) &&
        guild.guildId != 0 && vm_net_mock_guild_find_by_id(guild.guildId, &guild))
    {
        /* member_rank 1 is the guild leader and 2 is management.  The client
         * normally exposes this action only to those ranks, but enforce the
         * same boundary server-side for forged 10/34 requests. */
        if (rank > 0 && rank <= 2)
        {
            result = 1;
            if (publish)
            {
                sloganLen = vm_mock_mysql_bounded_strlen(slogan, sizeof(slogan));
                if (!sloganParsed || !vm_net_mock_guild_slogan_is_valid(slogan) ||
                    (sloganLen != 0 &&
                     vm_mysql_hex_encode(slogan, sloganLen,
                                         sloganHex, sizeof(sloganHex)) == 0))
                {
                    result = 0;
                }
                else
                {
                    snprintf(query, sizeof(query),
                             "UPDATE guilds SET notice=X'%s' WHERE guild_id=%u",
                             sloganHex, guild.guildId);
                    if (!vm_mysql_exec(query))
                    {
                        printf("[error][network] mock_guild_slogan_db_failed guild=%u role=%u error=%s\n",
                               guild.guildId, role->roleId, vm_mysql_last_error());
                        result = 0;
                    }
                    else
                    {
                        memcpy(guild.notice, slogan, sloganLen + 1);
                    }
                }
            }
        }
        else
        {
            result = publish ? 3 : 2;
        }
    }
    else
    {
        /* The load and publish handlers use different permission result
         * values for the same localized message. */
        result = publish ? 3 : 2;
    }

    responseSubtype = publish ? 34 : 35;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10,
                                     responseSubtype, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        (!publish && result == 1 &&
         !vm_net_mock_put_object_string(out, outCap, &pos, "slogan", guild.notice)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_slogan action=%s role=%u guild=%u rank=%u "
           "result=%u bytes=%u response=10/%u resp=%u "
           "evidence=JianghuOL.CBE:0x0103FC7E+0x0103FE7E+0x01040FCE+0x0104149E\n",
           publish ? "publish" : "load",
           role ? role->roleId : 0, guild.guildId, rank, result,
           result == 1 ? (u32)strlen(guild.notice) : 0,
           responseSubtype, pos);
    return pos;
}

static u8 vm_net_mock_apply_guild_rank_action(
    const vm_net_mock_guild_rank_action *action,
    vm_net_mock_role_state *requester,
    u32 *guildIdOut,
    u8 *requesterRankOut)
{
    vm_net_mock_guild_record guild;
    vm_net_mock_guild_member_record target;
    vm_net_mock_guild_member_record displaced;
    char tableAndWhere[256];
    char query[1536];
    char accountHex[129];
    char requesterAccountHex[129];
    char roleNameHex[VM_NET_MOCK_GUILD_ROLE_NAME_SIZE * 2 + 1];
    u32 positionUsers = 0;
    u8 requesterRank = 0;
    bool transactionStarted = false;

    if (guildIdOut)
        *guildIdOut = 0;
    if (requesterRankOut)
        *requesterRankOut = 0;
    memset(&guild, 0, sizeof(guild));
    memset(&target, 0, sizeof(target));
    memset(&displaced, 0, sizeof(displaced));
    memset(accountHex, 0, sizeof(accountHex));
    memset(requesterAccountHex, 0, sizeof(requesterAccountHex));
    memset(roleNameHex, 0, sizeof(roleNameHex));
    if (action == NULL || requester == NULL ||
        !vm_net_mock_guild_find_role_membership(requester->roleId,
                                                 &guild, &requesterRank) ||
        guild.guildId == 0)
    {
        return 2;
    }
    if (guildIdOut)
        *guildIdOut = guild.guildId;
    if (requesterRankOut)
        *requesterRankOut = requesterRank;
    if (!action->valid || requesterRank != 1)
        return 6;

    if (action->subtype == 38)
    {
        if (action->id == 0 || action->id > VM_NET_MOCK_GUILD_POSITION_COUNT)
            return 6;
        if (!vm_net_mock_guild_find_member(guild.guildId, action->aid, &target))
            return 3;
        /* Rank 3 is the ordinary guild-member state.  A member who already
         * owns rank 1/2 must be changed through 10/39, which names both sides
         * of the atomic replacement. */
        if (target.memberRank != 3)
            return 4;
        snprintf(tableAndWhere, sizeof(tableAndWhere),
                 "guild_members WHERE guild_id=%u AND member_rank=%u",
                 guild.guildId, action->id);
        if (!vm_net_mock_guild_count(tableAndWhere, &positionUsers))
            return 6;
        if (positionUsers != 0)
            return 5;
        {
            size_t accountLen = vm_mock_mysql_bounded_strlen(target.accountId,
                                                              sizeof(target.accountId));
            if (accountLen == 0 || accountLen >= sizeof(target.accountId) ||
                vm_mysql_hex_encode(target.accountId, accountLen,
                                    accountHex, sizeof(accountHex)) == 0)
            {
                return 6;
            }
        }
        if (!vm_mysql_exec("START TRANSACTION"))
            return 6;
        transactionStarted = true;
        snprintf(query, sizeof(query),
                 "UPDATE guild_members SET member_rank=%u,member_title='' "
                 "WHERE guild_id=%u AND account_id=CAST(X'%s' AS CHAR) AND role_id=%u "
                 "AND member_rank=3",
                 action->id, guild.guildId,
                 accountHex,
                 target.roleId);
        if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
            goto failed;
        return 1;
    }

    if (action->subtype == 39)
    {
        /* The leader position is transferred only through subtype 41.  This
         * path replaces the single management position and demotes its former
         * occupant to the selected member's ordinary rank. */
        if (action->drank != 2 || action->urank != 3)
            return 6;
        if (!vm_net_mock_guild_find_member(guild.guildId, action->did, &displaced) ||
            !vm_net_mock_guild_find_member(guild.guildId, action->uid, &target))
        {
            return 3;
        }
        if (strcmp(displaced.roleName, action->dname) != 0 ||
            strcmp(target.roleName, action->uname) != 0 ||
            displaced.memberRank != action->drank ||
            target.memberRank != action->urank)
        {
            return 6;
        }
        if (!vm_mysql_exec("START TRANSACTION"))
            return 6;
        transactionStarted = true;
        snprintf(query, sizeof(query),
                 "UPDATE guild_members SET member_rank=CASE role_id "
                 "WHEN %u THEN %u WHEN %u THEN %u ELSE member_rank END,member_title='' "
                 "WHERE guild_id=%u AND role_id IN (%u,%u)",
                 displaced.roleId, action->urank,
                 target.roleId, action->drank,
                 guild.guildId, displaced.roleId, target.roleId);
        if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
            goto failed;
        return 1;
    }

    if (action->subtype == 41)
    {
        size_t accountLen = 0;
        size_t requesterAccountLen = 0;
        size_t roleNameLen = 0;
        if (action->urank != 3 || action->uid == requester->roleId ||
            !vm_net_mock_guild_find_member(guild.guildId, action->uid, &target))
        {
            return action->uid == requester->roleId ? 6 : 3;
        }
        if (strcmp(target.roleName, action->uname) != 0 ||
            target.memberRank != action->urank)
        {
            return 6;
        }
        accountLen = vm_mock_mysql_bounded_strlen(target.accountId,
                                                   sizeof(target.accountId));
        requesterAccountLen = g_vm_mock_service_active_account_id != NULL ?
            vm_mock_mysql_bounded_strlen(g_vm_mock_service_active_account_id, 64) : 0;
        roleNameLen = vm_mock_mysql_bounded_strlen(target.roleName,
                                                    sizeof(target.roleName));
        if (accountLen == 0 || accountLen >= sizeof(target.accountId) ||
            g_vm_mock_service_active_account_id == NULL || requesterAccountLen == 0 ||
            requesterAccountLen >= 64 ||
            roleNameLen == 0 || roleNameLen >= sizeof(target.roleName) ||
            vm_mysql_hex_encode(target.accountId, accountLen,
                                accountHex, sizeof(accountHex)) == 0 ||
            vm_mysql_hex_encode(g_vm_mock_service_active_account_id,
                                requesterAccountLen, requesterAccountHex,
                                sizeof(requesterAccountHex)) == 0 ||
            vm_mysql_hex_encode(target.roleName, roleNameLen,
                                roleNameHex, sizeof(roleNameHex)) == 0)
        {
            return 6;
        }
        if (!vm_mysql_exec("START TRANSACTION"))
            return 6;
        transactionStarted = true;
        snprintf(query, sizeof(query),
                 "UPDATE guild_members SET member_rank=CASE role_id "
                 "WHEN %u THEN %u WHEN %u THEN 1 ELSE member_rank END,member_title='' "
                 "WHERE guild_id=%u AND role_id IN (%u,%u)",
                 requester->roleId, action->urank, target.roleId,
                 guild.guildId, requester->roleId, target.roleId);
        if (!vm_mysql_exec(query))
            goto failed;
        snprintf(query, sizeof(query),
                 "UPDATE guilds SET leader_account_id=CAST(X'%s' AS CHAR),"
                 "leader_role_id=%u,leader_role_name=X'%s' WHERE guild_id=%u "
                 "AND leader_account_id=CAST(X'%s' AS CHAR) AND leader_role_id=%u",
                 accountHex, target.roleId, roleNameHex, guild.guildId,
                 requesterAccountHex,
                 requester->roleId);
        if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
            goto failed;
        return 1;
    }
    return 6;

failed:
    printf("[error][network] mock_guild_rank_db_failed subtype=%u guild=%u role=%u error=%s\n",
           action ? action->subtype : 0, guild.guildId,
           requester ? requester->roleId : 0, vm_mysql_last_error());
    if (transactionStarted)
        vm_mysql_exec("ROLLBACK");
    return 6;
}

static u32 vm_net_mock_build_guild_rank_action_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    vm_net_mock_guild_rank_action action;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 guildId = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 requesterRank = 0;
    u8 result = 6;

    memset(&action, 0, sizeof(action));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_guild_rank_action(request, requestLen, &action))
    {
        return 0;
    }
    result = vm_net_mock_apply_guild_rank_action(&action, role,
                                                  &guildId, &requesterRank);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 10,
                                     action.subtype, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_guild_rank_action subtype=%u role=%u guild=%u "
           "requester_rank=%u target=%u desired=%u displaced=%u old_rank=%u "
           "valid=%u result=%u resp=%u "
           "evidence=JianghuOL.CBE:0x010420E6+0x01042198+0x010428D0\n",
           action.subtype, role ? role->roleId : 0, guildId, requesterRank,
           action.subtype == 38 ? action.aid : action.uid,
           action.subtype == 38 ? action.id : (action.subtype == 41 ? 1u : action.drank),
           action.did, action.urank, action.valid ? 1u : 0u, result, pos);
    return pos;
}

static u8 vm_net_mock_apply_guild_kick_action(
    const vm_net_mock_guild_kick_action *action,
    vm_net_mock_role_state *requester,
    u32 *guildIdOut,
    u8 *requesterRankOut,
    u8 *targetRankOut)
{
    vm_net_mock_guild_record guild;
    vm_net_mock_guild_member_record target;
    char accountHex[129];
    char query[1024];
    char tableAndWhere[256];
    u32 remainingRows = 0;
    u8 requesterRank = 0;
    bool transactionStarted = false;

    if (guildIdOut)
        *guildIdOut = 0;
    if (requesterRankOut)
        *requesterRankOut = 0;
    if (targetRankOut)
        *targetRankOut = 0;
    memset(&guild, 0, sizeof(guild));
    memset(&target, 0, sizeof(target));
    memset(accountHex, 0, sizeof(accountHex));
    if (action == NULL || requester == NULL ||
        !vm_net_mock_guild_find_role_membership(requester->roleId,
                                                 &guild, &requesterRank) ||
        guild.guildId == 0)
    {
        return 2; /* permission denied / requester is no longer in a guild */
    }
    if (guildIdOut)
        *guildIdOut = guild.guildId;
    if (requesterRankOut)
        *requesterRankOut = requesterRank;
    if (requesterRank != 1)
        return 2;
    if (!action->valid)
        return 5;
    if (!vm_net_mock_guild_find_member(guild.guildId, action->roleId, &target))
        return 3;
    if (targetRankOut)
        *targetRankOut = target.memberRank;
    /* The client deliberately routes rank 1/2 through the demotion flow first.
     * Enforce the same rule server-side, including attempts to remove oneself. */
    if (target.memberRank <= 2)
        return 4;
    if (target.memberRank != 3 || action->memberRank != target.memberRank)
        return 5;
    {
        size_t accountLen = vm_mock_mysql_bounded_strlen(target.accountId,
                                                          sizeof(target.accountId));
        if (accountLen == 0 || accountLen >= sizeof(target.accountId) ||
            vm_mysql_hex_encode(target.accountId, accountLen,
                                accountHex, sizeof(accountHex)) == 0)
        {
            return 5;
        }
    }

    if (!vm_mysql_exec("START TRANSACTION"))
        return 5;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "DELETE FROM guild_members WHERE guild_id=%u "
             "AND account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND member_rank=3",
             guild.guildId, accountHex, target.roleId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(tableAndWhere, sizeof(tableAndWhere),
             "guild_members WHERE guild_id=%u "
             "AND account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             guild.guildId, accountHex, target.roleId);
    if (!vm_net_mock_guild_count(tableAndWhere, &remainingRows) || remainingRows != 0 ||
        !vm_mysql_exec("COMMIT"))
    {
        goto failed;
    }
    return 1;

failed:
    printf("[error][network] mock_guild_kick_db_failed guild=%u requester=%u "
           "target=%u error=%s\n",
           guild.guildId, requester ? requester->roleId : 0,
           action ? action->roleId : 0, vm_mysql_last_error());
    if (transactionStarted)
        vm_mysql_exec("ROLLBACK");
    return 5;
}

