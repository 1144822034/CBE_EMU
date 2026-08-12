/*
 * PixiJS_web 恩怨 / 江湖追杀令 — WT kind=43.
 * subtypes: 1 page (dir 0=我击杀, 1=击杀我), 2 hunt-order use on enemy.
 * Included from mock-server.c after auction (needs guild online lookup +
 * scene_display_name + backpack consume helpers already in the TU).
 */

enum
{
    VM_MOCK_ENMITY_WT_KIND = 43,
    VM_MOCK_ENMITY_WT_PAGE = 1,
    VM_MOCK_ENMITY_WT_HUNT = 2,
    VM_MOCK_ENMITY_PAGE_SIZE_DEFAULT = 20,
    VM_MOCK_ENMITY_PAGE_SIZE_MAX = 40,
    VM_MOCK_ENMITY_DIR_I_KILLED = 0,
    VM_MOCK_ENMITY_DIR_KILLED_ME = 1
};

typedef struct
{
    u32 roleId;
    char roleName[33];
    u32 killCount;
    u8 online;
} vm_mock_enmity_row;

typedef struct
{
    vm_mock_enmity_row *rows;
    u32 capacity;
    u32 count;
    u8 invalid;
} vm_mock_enmity_list_context;

typedef struct
{
    u32 value;
    u8 found;
    u8 invalid;
} vm_mock_enmity_count_context;

static bool vm_mock_enmity_ensure_table(void)
{
    static bool attempted = false;
    static bool ok = false;

    if (attempted)
        return ok;
    attempted = true;
    ok = vm_mysql_exec(
        "CREATE TABLE IF NOT EXISTS enmity_kills ("
        "killer_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
        "killer_role_id INT UNSIGNED NOT NULL,"
        "killer_role_name VARBINARY(32) NOT NULL DEFAULT '',"
        "victim_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
        "victim_role_id INT UNSIGNED NOT NULL,"
        "victim_role_name VARBINARY(32) NOT NULL DEFAULT '',"
        "kill_count INT UNSIGNED NOT NULL DEFAULT 1,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "PRIMARY KEY (killer_account_id, killer_role_id, victim_account_id, victim_role_id),"
        "KEY idx_enmity_victim (victim_account_id, victim_role_id),"
        "KEY idx_enmity_killer (killer_account_id, killer_role_id)"
        ") ENGINE=InnoDB");
    if (!ok)
    {
        printf("[warn][mock-service] enmity_table_ensure_failed error=%s\n",
               vm_mysql_last_error());
    }
    return ok;
}

static bool vm_mock_enmity_count_row(void *contextValue, unsigned int columnCount,
                                     const char *const *values, const size_t *lengths)
{
    vm_mock_enmity_count_context *ctx = (vm_mock_enmity_count_context *)contextValue;

    (void)columnCount;
    if (ctx == NULL || values == NULL || lengths == NULL || values[0] == NULL)
    {
        if (ctx)
            ctx->invalid = 1;
        return false;
    }
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &ctx->value))
    {
        ctx->invalid = 1;
        return false;
    }
    ctx->found = 1;
    return true;
}

static bool vm_mock_enmity_list_row(void *contextValue, unsigned int columnCount,
                                    const char *const *values, const size_t *lengths)
{
    vm_mock_enmity_list_context *ctx = (vm_mock_enmity_list_context *)contextValue;
    vm_mock_enmity_row *row = NULL;
    char nameBuf[65];

    if (ctx == NULL || ctx->rows == NULL || values == NULL || lengths == NULL ||
        columnCount < 3)
    {
        if (ctx)
            ctx->invalid = 1;
        return false;
    }
    if (ctx->count >= ctx->capacity)
        return true;
    row = &ctx->rows[ctx->count];
    memset(row, 0, sizeof(*row));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->roleId) ||
        row->roleId == 0)
    {
        ctx->invalid = 1;
        return false;
    }
    memset(nameBuf, 0, sizeof(nameBuf));
    if (values[1] != NULL && lengths[1] > 0)
    {
        size_t decodedLen = 0;
        if (vm_mysql_hex_decode(values[1], lengths[1], nameBuf,
                                sizeof(nameBuf) - 1, &decodedLen))
        {
            nameBuf[decodedLen < sizeof(nameBuf) ? decodedLen : sizeof(nameBuf) - 1] = 0;
            snprintf(row->roleName, sizeof(row->roleName), "%s", nameBuf);
        }
        else
        {
            size_t copy = lengths[1] < sizeof(row->roleName) - 1
                              ? lengths[1]
                              : sizeof(row->roleName) - 1;
            memcpy(row->roleName, values[1], copy);
            row->roleName[copy] = 0;
        }
    }
    if (row->roleName[0] == 0)
        snprintf(row->roleName, sizeof(row->roleName), "Player");
    if (!vm_mock_mysql_parse_u32(values[2], lengths[2], &row->killCount) ||
        row->killCount == 0)
    {
        row->killCount = 1;
    }
    ++ctx->count;
    return true;
}

static vm_mock_service_client_session *vm_mock_enmity_session_for_role(u32 roleId)
{
    vm_mock_service_client_session *s = g_vm_mock_service_client_sessions;

    if (roleId == 0)
        return NULL;
    for (; s != NULL; s = s->next)
    {
        if (s->roleOnline && s->onlineRoleId == roleId)
            return s;
    }
    return NULL;
}

static void vm_mock_enmity_record_kill(
    const char *killerAccountId, u32 killerRoleId, const char *killerName,
    const char *victimAccountId, u32 victimRoleId, const char *victimName)
{
    char killerAccHex[129];
    char victimAccHex[129];
    char killerNameHex[96];
    char victimNameHex[96];
    char query[768];
    size_t killerAccLen;
    size_t victimAccLen;
    const char *kName = killerName && killerName[0] ? killerName : "Player";
    const char *vName = victimName && victimName[0] ? victimName : "Player";

    if (killerAccountId == NULL || killerAccountId[0] == 0 || killerRoleId == 0 ||
        victimAccountId == NULL || victimAccountId[0] == 0 || victimRoleId == 0 ||
        killerRoleId == victimRoleId)
    {
        return;
    }
    if (!vm_mock_enmity_ensure_table())
        return;

    killerAccLen = vm_mock_mysql_bounded_strlen(killerAccountId, 64);
    victimAccLen = vm_mock_mysql_bounded_strlen(victimAccountId, 64);
    if (killerAccLen == 0 || victimAccLen == 0 ||
        vm_mysql_hex_encode(killerAccountId, killerAccLen, killerAccHex,
                            sizeof(killerAccHex)) == 0 ||
        vm_mysql_hex_encode(victimAccountId, victimAccLen, victimAccHex,
                            sizeof(victimAccHex)) == 0 ||
        vm_mysql_hex_encode(kName, strlen(kName) > 32 ? 32 : strlen(kName),
                            killerNameHex, sizeof(killerNameHex)) == 0 ||
        vm_mysql_hex_encode(vName, strlen(vName) > 32 ? 32 : strlen(vName),
                            victimNameHex, sizeof(victimNameHex)) == 0)
    {
        return;
    }

    snprintf(query, sizeof(query),
             "INSERT INTO enmity_kills("
             "killer_account_id,killer_role_id,killer_role_name,"
             "victim_account_id,victim_role_id,victim_role_name,kill_count) "
             "VALUES(CAST(X'%s' AS CHAR),%u,UNHEX('%s'),"
             "CAST(X'%s' AS CHAR),%u,UNHEX('%s'),1) "
             "ON DUPLICATE KEY UPDATE "
             "kill_count=kill_count+1,"
             "killer_role_name=VALUES(killer_role_name),"
             "victim_role_name=VALUES(victim_role_name),"
             "updated_at=CURRENT_TIMESTAMP",
             killerAccHex, killerRoleId, killerNameHex, victimAccHex, victimRoleId,
             victimNameHex);
    if (!vm_mysql_exec(query))
    {
        printf("[warn][mock-service] enmity_record_failed killer=%s/%u "
               "victim=%s/%u error=%s\n",
               killerAccountId, killerRoleId, victimAccountId, victimRoleId,
               vm_mysql_last_error());
        return;
    }
    printf("[info][mock-service] enmity_record killer=%s/%u victim=%s/%u\n",
           killerAccountId, killerRoleId, victimAccountId, victimRoleId);
}

static void vm_mock_enmity_record_duel_result(vm_mock_service_duel *duel)
{
    vm_mock_service_client_session *a = NULL;
    vm_mock_service_client_session *b = NULL;
    int killerIndex = -1;
    int victimIndex = -1;

    if (duel == NULL)
        return;
    /* 恩怨仅累计普通切磋；擂台 / 赌战不计。 */
    if (duel->arenaMode || duel->wagerMode)
        return;
    /* Only record when exactly one seat is dead. */
    if (duel->hp[0] == 0 && duel->hp[1] > 0)
    {
        killerIndex = 1;
        victimIndex = 0;
    }
    else if (duel->hp[1] == 0 && duel->hp[0] > 0)
    {
        killerIndex = 0;
        victimIndex = 1;
    }
    else
        return;

    a = vm_mock_service_find_client_session(duel->clientIds[killerIndex]);
    b = vm_mock_service_find_client_session(duel->clientIds[victimIndex]);
    if (a == NULL || b == NULL || a->accountId[0] == 0 || b->accountId[0] == 0 ||
        a->onlineRoleId == 0 || b->onlineRoleId == 0)
    {
        return;
    }
    vm_mock_enmity_record_kill(
        a->accountId, a->onlineRoleId,
        a->onlineRoleName[0] ? a->onlineRoleName : "Player", b->accountId,
        b->onlineRoleId, b->onlineRoleName[0] ? b->onlineRoleName : "Player");
}

static bool vm_mock_enmity_pair_exists(const char *ownerAccountId, u32 ownerRoleId,
                                        u32 peerRoleId)
{
    char ownerHex[129];
    char query[512];
    size_t ownerLen;
    vm_mock_enmity_count_context ctx;

    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        peerRoleId == 0 || !vm_mock_enmity_ensure_table())
    {
        return false;
    }
    ownerLen = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    if (ownerLen == 0 ||
        vm_mysql_hex_encode(ownerAccountId, ownerLen, ownerHex, sizeof(ownerHex)) == 0)
    {
        return false;
    }
    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM enmity_kills WHERE "
             "((killer_account_id=CAST(X'%s' AS CHAR) AND killer_role_id=%u AND "
             "victim_role_id=%u) OR "
             "(victim_account_id=CAST(X'%s' AS CHAR) AND victim_role_id=%u AND "
             "killer_role_id=%u))",
             ownerHex, ownerRoleId, peerRoleId, ownerHex, ownerRoleId, peerRoleId);
    if (!vm_mysql_query(query, vm_mock_enmity_count_row, &ctx) || ctx.invalid ||
        !ctx.found)
    {
        return false;
    }
    return ctx.value > 0;
}

static bool vm_mock_enmity_count_owner(const char *ownerAccountId, u32 ownerRoleId,
                                       u8 dir, u32 *countOut)
{
    char ownerHex[129];
    char query[512];
    size_t ownerLen;
    vm_mock_enmity_count_context ctx;

    if (countOut)
        *countOut = 0;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        !vm_mock_enmity_ensure_table())
    {
        return false;
    }
    ownerLen = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    if (ownerLen == 0 ||
        vm_mysql_hex_encode(ownerAccountId, ownerLen, ownerHex, sizeof(ownerHex)) == 0)
    {
        return false;
    }
    memset(&ctx, 0, sizeof(ctx));
    if (dir == VM_MOCK_ENMITY_DIR_KILLED_ME)
    {
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM enmity_kills WHERE "
                 "victim_account_id=CAST(X'%s' AS CHAR) AND victim_role_id=%u",
                 ownerHex, ownerRoleId);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM enmity_kills WHERE "
                 "killer_account_id=CAST(X'%s' AS CHAR) AND killer_role_id=%u",
                 ownerHex, ownerRoleId);
    }
    if (!vm_mysql_query(query, vm_mock_enmity_count_row, &ctx) || ctx.invalid ||
        !ctx.found)
    {
        return false;
    }
    if (countOut)
        *countOut = ctx.value;
    return true;
}

static bool vm_mock_enmity_list_owner_page(const char *ownerAccountId, u32 ownerRoleId,
                                           u8 dir, u32 skip, u32 limit,
                                           vm_mock_enmity_row *rowsOut, u32 rowsCap,
                                           u32 *returnedOut)
{
    char ownerHex[129];
    char query[640];
    size_t ownerLen;
    vm_mock_enmity_list_context ctx;

    if (returnedOut)
        *returnedOut = 0;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        rowsOut == NULL || rowsCap == 0 || limit == 0 ||
        !vm_mock_enmity_ensure_table())
    {
        return false;
    }
    if (limit > rowsCap)
        limit = rowsCap;
    ownerLen = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    if (ownerLen == 0 ||
        vm_mysql_hex_encode(ownerAccountId, ownerLen, ownerHex, sizeof(ownerHex)) == 0)
    {
        return false;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.rows = rowsOut;
    ctx.capacity = limit;
    if (dir == VM_MOCK_ENMITY_DIR_KILLED_ME)
    {
        snprintf(query, sizeof(query),
                 "SELECT killer_role_id,HEX(killer_role_name),kill_count "
                 "FROM enmity_kills WHERE victim_account_id=CAST(X'%s' AS CHAR) "
                 "AND victim_role_id=%u ORDER BY updated_at DESC,killer_role_id "
                 "LIMIT %u OFFSET %u",
                 ownerHex, ownerRoleId, limit, skip);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "SELECT victim_role_id,HEX(victim_role_name),kill_count "
                 "FROM enmity_kills WHERE killer_account_id=CAST(X'%s' AS CHAR) "
                 "AND killer_role_id=%u ORDER BY updated_at DESC,victim_role_id "
                 "LIMIT %u OFFSET %u",
                 ownerHex, ownerRoleId, limit, skip);
    }
    if (!vm_mysql_query(query, vm_mock_enmity_list_row, &ctx) || ctx.invalid)
        return false;
    if (returnedOut)
        *returnedOut = ctx.count;
    return true;
}

static u32 vm_net_mock_build_enmity_page_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *ownerRole = vm_net_mock_active_role();
    const char *ownerAccountId = g_vm_mock_service_active_account_id;
    u32 index = 0;
    u32 pageSize = VM_MOCK_ENMITY_PAGE_SIZE_DEFAULT;
    u32 dirU = 0;
    u8 dir = VM_MOCK_ENMITY_DIR_I_KILLED;
    u32 total = 0;
    u32 totalPages = 1;
    u8 allPages8 = 1;
    u8 blob[4096];
    u32 blobLen = 0;
    u16 rowCount = 0;
    vm_mock_enmity_row pageRows[40];
    u32 pageCount = 0;
    u32 objectStart = 0;
    u32 pos = 5;

    if (request == NULL || out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ENMITY_WT_KIND ||
        object.subtype != VM_MOCK_ENMITY_WT_PAGE || offset != requestLen)
    {
        return 0;
    }
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "dir",
                                              &dirU);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "index", &index);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "pageSize", &pageSize);
    dir = dirU == VM_MOCK_ENMITY_DIR_KILLED_ME ? VM_MOCK_ENMITY_DIR_KILLED_ME
                                               : VM_MOCK_ENMITY_DIR_I_KILLED;
    if (pageSize == 0)
        pageSize = VM_MOCK_ENMITY_PAGE_SIZE_DEFAULT;
    if (pageSize > VM_MOCK_ENMITY_PAGE_SIZE_MAX)
        pageSize = VM_MOCK_ENMITY_PAGE_SIZE_MAX;

    if (ownerRole != NULL && ownerAccountId != NULL && ownerAccountId[0] != 0)
    {
        if (!vm_mock_enmity_count_owner(ownerAccountId, ownerRole->roleId, dir, &total))
            total = 0;
    }
    if (total > 0)
        totalPages = (total + pageSize - 1u) / pageSize;
    allPages8 = (u8)(totalPages > 0xffu ? 0xffu : totalPages);

    if (!vm_net_mock_seq_put_i16(blob, sizeof(blob), &blobLen, 0))
        return 0;
    if (ownerRole != NULL && ownerAccountId != NULL && ownerAccountId[0] != 0)
    {
        if (!vm_mock_enmity_list_owner_page(ownerAccountId, ownerRole->roleId, dir,
                                            index, pageSize, pageRows,
                                            (u32)(sizeof(pageRows) / sizeof(pageRows[0])),
                                            &pageCount))
        {
            pageCount = 0;
        }
        for (u32 i = 0; i < pageCount; ++i)
        {
            vm_mock_service_client_session *online =
                vm_mock_enmity_session_for_role(pageRows[i].roleId);
            const char *name =
                online && online->onlineRoleName[0] ? online->onlineRoleName
                                                    : pageRows[i].roleName;
            u8 onlineState = online ? 1 : 0;

            if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen,
                                         pageRows[i].roleId) ||
                !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen,
                                            name && name[0] ? name : "Player") ||
                !vm_net_mock_seq_put_u8(blob, sizeof(blob), &blobLen, onlineState) ||
                !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen,
                                         pageRows[i].killCount))
            {
                return 0;
            }
            ++rowCount;
        }
    }
    blob[2] = (u8)(rowCount >> 8);
    blob[3] = (u8)rowCount;

    if (blobLen > 0xffff ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ENMITY_WT_KIND,
                                     VM_MOCK_ENMITY_WT_PAGE, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "allpgs", allPages8) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "dir", dir) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "enmityinfo", blob,
                                    (u16)blobLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_enmity_page dir=%u index=%u page_size=%u "
           "owner=%s/%u total=%u rows=%u resp=%u\n",
           dir, index, pageSize, ownerAccountId ? ownerAccountId : "-",
           ownerRole ? ownerRole->roleId : 0, total, rowCount, pos);
    return pos;
}

static u32 vm_net_mock_build_enmity_hunt_result(u8 *out, u32 outCap, u8 result,
                                                 const char *tipGbk, u16 remaining)
{
    u32 objectStart = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ENMITY_WT_KIND,
                                     VM_MOCK_ENMITY_WT_HUNT, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "remaining", remaining) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "tip",
                                       tipGbk ? tipGbk : ""))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_enmity_hunt_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *ownerAccountId = g_vm_mock_service_active_account_id;
    u32 itemId = 0;
    u32 seqU = 0;
    u32 targetId = 0;
    u16 seq = 0;
    u32 remaining = 0;
    vm_mock_service_client_session *target = NULL;
    char mapName[96];
    char tip[160];
    const char *scene = NULL;
    /* GBK tips */
    static const char tipOffline[] = "\xb6\xd4\xb7\xbd\xb2\xbb\xd4\xda\xcf\xdf";
    static const char tipNotEnemy[] =
        "\xb7\xc7\xb3\xf0\xc8\xcb\xb2\xbb\xbf\xc9\xca\xb9\xd3\xc3";
    static const char tipNoItem[] = "\xd7\xb7\xc9\xb1\xc1\xee\xb2\xbb\xd7\xe3";
    static const char tipPrefix[] = "\xb3\xf0\xc8\xcb\xb5\xb1\xc7\xb0\xd4\xda";

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ENMITY_WT_KIND ||
        object.subtype != VM_MOCK_ENMITY_WT_HUNT || offset != requestLen)
    {
        return 0;
    }
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "itemId", &itemId);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "seq",
                                              &seqU);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "targetId", &targetId);
    seq = (u16)seqU;
    if (itemId == 0)
        itemId = VM_NET_MOCK_HUNT_ORDER_ITEM_ID;
    if (role == NULL || ownerAccountId == NULL || ownerAccountId[0] == 0)
        return vm_net_mock_build_enmity_hunt_result(out, outCap, 0, tipNoItem, 0);
    if (itemId != VM_NET_MOCK_HUNT_ORDER_ITEM_ID || targetId == 0)
        return vm_net_mock_build_enmity_hunt_result(out, outCap, 0, tipNotEnemy, 0);
    if (!vm_mock_enmity_pair_exists(ownerAccountId, role->roleId, targetId))
        return vm_net_mock_build_enmity_hunt_result(out, outCap, 0, tipNotEnemy, 0);

    target = vm_mock_enmity_session_for_role(targetId);
    if (target == NULL)
    {
        /* Offline: do not consume. */
        printf("[info][network] mock_enmity_hunt offline target=%u role=%u "
               "consume=0\n",
               targetId, role->roleId);
        return vm_net_mock_build_enmity_hunt_result(out, outCap, 0, tipOffline, 0);
    }

    scene = target->sceneVisibleScene[0] ? target->sceneVisibleScene : target->onlineScene;
    memset(mapName, 0, sizeof(mapName));
    if (scene && scene[0] &&
        vm_net_mock_scene_display_name_load(scene, mapName, sizeof(mapName)) &&
        mapName[0] != 0)
    {
        /* display name ok */
    }
    else if (scene && scene[0])
    {
        snprintf(mapName, sizeof(mapName), "%s", scene);
    }
    else
    {
        snprintf(mapName, sizeof(mapName), "?");
    }
    snprintf(tip, sizeof(tip), "%s%s", tipPrefix, mapName);

    if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1, &remaining))
    {
        /* Try any live 847 row if seq drifted. */
        const vm_net_mock_backpack_item_state *any =
            vm_net_mock_role_find_backpack_item(role, itemId, 0);
        if (any == NULL ||
            !vm_net_mock_role_consume_backpack_item(role, itemId, any->seq, 1,
                                                    &remaining))
        {
            return vm_net_mock_build_enmity_hunt_result(out, outCap, 0, tipNoItem, 0);
        }
    }
    vm_net_mock_role_mark_inventory_dirty("hunt-order-use");
    printf("[info][network] mock_enmity_hunt ok target=%u role=%u scene=%s "
           "remaining=%u\n",
           targetId, role->roleId, scene ? scene : "-", remaining);
    return vm_net_mock_build_enmity_hunt_result(out, outCap, 1, tip, (u16)remaining);
}

static u32 vm_net_mock_build_enmity_protocol_response(const u8 *request, u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || out == NULL)
        return 0;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ENMITY_WT_KIND)
    {
        return 0;
    }
    if (object.subtype == VM_MOCK_ENMITY_WT_PAGE)
        return vm_net_mock_build_enmity_page_response(request, requestLen, out, outCap);
    if (object.subtype == VM_MOCK_ENMITY_WT_HUNT)
        return vm_net_mock_build_enmity_hunt_response(request, requestLen, out, outCap);
    return 0;
}
