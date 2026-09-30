/*
 * PixiJS-only 角色封印卷 (item 857):
 *   29/25 seal sibling role onto escrow + bound scroll
 *   29/26 unseal bound scroll onto buyer account
 * Included from mock_server_equipment_npc.c after rename_card.
 */

typedef struct
{
    u32 value;
    bool found;
    bool invalid;
} vm_net_mock_role_seal_u32_ctx;

typedef struct
{
    u8 used[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
    bool invalid;
} vm_net_mock_role_seal_index_scan_ctx;

typedef struct
{
    u8 used[256];
    u32 maxIndex;
    bool invalid;
} vm_net_mock_role_seal_escrow_scan_ctx;

typedef struct
{
    u32 sealedRoleId;
    char previewName[32];
    u32 previewLevel;
    u8 previewJob;
    u8 previewSex;
    char previewScene[64];
    u16 previewX;
    u16 previewY;
    bool found;
    bool invalid;
} vm_net_mock_role_seal_row_ctx;

typedef struct
{
    char accountId[64];
    bool found;
    bool invalid;
} vm_net_mock_role_seal_account_ctx;

static void vm_net_mock_role_seal_splice_memory(u32 roleId);
static bool vm_net_mock_role_find_free_escrow_index(u8 *indexOut);

static bool vm_net_mock_role_seal_u32_row(void *ctx, unsigned int cols,
                                          const char *const *vals,
                                          const size_t *lens)
{
    vm_net_mock_role_seal_u32_ctx *c = (vm_net_mock_role_seal_u32_ctx *)ctx;

    if (c == NULL || c->found || cols < 1 ||
        !vm_mock_mysql_parse_u32(vals[0], lens[0], &c->value))
    {
        if (c != NULL)
            c->invalid = true;
        return true;
    }
    c->found = true;
    return true;
}

static bool vm_net_mock_role_seal_index_scan_row(void *ctx, unsigned int cols,
                                                 const char *const *vals,
                                                 const size_t *lens)
{
    vm_net_mock_role_seal_index_scan_ctx *c =
        (vm_net_mock_role_seal_index_scan_ctx *)ctx;
    u32 idx = 0;

    if (c == NULL || cols < 1 ||
        !vm_mock_mysql_parse_u32(vals[0], lens[0], &idx) ||
        idx >= (u32)VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (c != NULL)
            c->invalid = true;
        return true;
    }
    c->used[idx] = 1;
    return true;
}

static bool vm_net_mock_role_seal_escrow_scan_row(void *ctx, unsigned int cols,
                                                  const char *const *vals,
                                                  const size_t *lens)
{
    vm_net_mock_role_seal_escrow_scan_ctx *c =
        (vm_net_mock_role_seal_escrow_scan_ctx *)ctx;
    u32 idx = 0;

    if (c == NULL || cols < 1 ||
        !vm_mock_mysql_parse_u32(vals[0], lens[0], &idx) || idx > 254u)
    {
        if (c != NULL)
            c->invalid = true;
        return true;
    }
    c->used[idx] = 1;
    if (idx > c->maxIndex)
        c->maxIndex = idx;
    return true;
}

static bool vm_net_mock_role_seal_row_cb(void *ctx, unsigned int cols,
                                         const char *const *vals,
                                         const size_t *lens)
{
    vm_net_mock_role_seal_row_ctx *c = (vm_net_mock_role_seal_row_ctx *)ctx;
    size_t nlen = 0;
    size_t slen = 0;
    u32 tmp = 0;

    if (c == NULL || cols < 5)
    {
        if (c != NULL)
            c->invalid = true;
        return true;
    }
    memset(c, 0, sizeof(*c));
    if (!vm_mock_mysql_parse_u32(vals[0], lens[0], &c->sealedRoleId))
    {
        c->invalid = true;
        return true;
    }
    if (vals[1])
        (void)vm_mysql_hex_decode(vals[1], lens[1], c->previewName,
                                  sizeof(c->previewName) - 1, &nlen);
    c->previewName[nlen] = 0;
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &c->previewLevel);
    (void)vm_mock_mysql_parse_u32(vals[3], lens[3], &tmp);
    c->previewJob = (u8)tmp;
    (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &tmp);
    c->previewSex = (u8)tmp;
    if (cols >= 8)
    {
        if (vals[5])
            (void)vm_mysql_hex_decode(vals[5], lens[5], c->previewScene,
                                      sizeof(c->previewScene) - 1, &slen);
        c->previewScene[slen] = 0;
        (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &tmp);
        c->previewX = (u16)tmp;
        (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &tmp);
        c->previewY = (u16)tmp;
    }
    c->found = true;
    return true;
}

static bool vm_net_mock_role_seal_account_row(void *ctx, unsigned int cols,
                                              const char *const *vals,
                                              const size_t *lens)
{
    vm_net_mock_role_seal_account_ctx *c =
        (vm_net_mock_role_seal_account_ctx *)ctx;
    size_t n = 0;

    if (c == NULL || cols < 1 || vals == NULL || vals[0] == NULL)
    {
        if (c != NULL)
            c->invalid = true;
        return true;
    }
    n = lens ? lens[0] : strlen(vals[0]);
    if (n >= sizeof(c->accountId))
        n = sizeof(c->accountId) - 1;
    memcpy(c->accountId, vals[0], n);
    c->accountId[n] = 0;
    c->found = true;
    return true;
}

static bool vm_net_mock_role_seal_table_exists(const char *tableName)
{
    char query[320];
    vm_net_mock_role_seal_u32_ctx ctx;

    if (tableName == NULL || tableName[0] == 0)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM information_schema.TABLES "
             "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='%s'",
             tableName);
    if (!vm_mysql_query(query, vm_net_mock_role_seal_u32_row, &ctx) ||
        ctx.invalid || !ctx.found)
        return false;
    return ctx.value > 0;
}

static bool vm_net_mock_role_seal_ensure_schema(void)
{
    static bool ensured = false;
    static bool ok = false;
    char escrowHex[160];
    char query[768];

    if (ensured)
        return ok;
    ensured = true;
    if (vm_mysql_hex_encode(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT,
                            strlen(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT),
                            escrowHex, sizeof(escrowHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO accounts(account_id,password_value) "
             "VALUES(CAST(X'%s' AS CHAR),'!')",
             escrowHex);
    if (!vm_mysql_exec(query))
    {
        printf("[error][network] mock_role_seal_schema accounts error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO account_role_state"
             "(account_id,format_version,active_role_id,role_count) "
             "VALUES(CAST(X'%s' AS CHAR),1,0,0)",
             escrowHex);
    if (!vm_mysql_exec(query))
    {
        printf("[error][network] mock_role_seal_schema role_state error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_sealed_roles ("
            "item_instance_id INT UNSIGNED NOT NULL,"
            "sealed_role_id INT UNSIGNED NOT NULL,"
            "origin_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin "
            "NOT NULL,"
            "preview_name VARBINARY(32) NOT NULL DEFAULT '',"
            "preview_level INT UNSIGNED NOT NULL DEFAULT 1,"
            "preview_job TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "preview_sex TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "preview_scene VARBINARY(64) NOT NULL DEFAULT '',"
            "preview_pos_x SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "preview_pos_y SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "sealed_at INT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(item_instance_id),"
            "UNIQUE KEY uk_sealed_roles_role(sealed_role_id),"
            "KEY idx_sealed_roles_origin(origin_account_id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
    {
        printf("[error][network] mock_role_seal_schema sealed_roles error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    /* Older DBs: add scene/pos columns used by orphan recreate. */
    (void)vm_mysql_exec(
        "ALTER TABLE server_sealed_roles "
        "ADD COLUMN preview_scene VARBINARY(64) NOT NULL DEFAULT ''");
    (void)vm_mysql_exec(
        "ALTER TABLE server_sealed_roles "
        "ADD COLUMN preview_pos_x SMALLINT UNSIGNED NOT NULL DEFAULT 0");
    (void)vm_mysql_exec(
        "ALTER TABLE server_sealed_roles "
        "ADD COLUMN preview_pos_y SMALLINT UNSIGNED NOT NULL DEFAULT 0");
    ok = true;
    return true;
}

/* Current account_id owning role_id, or false if missing/error. */
static bool vm_net_mock_role_seal_lookup_owner(u32 roleId, char *out,
                                               size_t outCap)
{
    char query[192];
    vm_net_mock_role_seal_account_ctx ctx;

    if (out && outCap)
        out[0] = 0;
    if (roleId == 0 || out == NULL || outCap < 2)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT account_id FROM account_roles WHERE role_id=%u LIMIT 1",
             roleId);
    if (!vm_mysql_query(query, vm_net_mock_role_seal_account_row, &ctx) ||
        ctx.invalid || !ctx.found || ctx.accountId[0] == 0)
        return false;
    snprintf(out, outCap, "%s", ctx.accountId);
    return true;
}

/* True when role_id is bound in server_sealed_roles (must not live in seller memory). */
static bool vm_net_mock_role_seal_is_bound_role(u32 roleId)
{
    char query[192];
    vm_net_mock_role_seal_u32_ctx ctx;

    if (roleId == 0 || !vm_net_mock_role_seal_ensure_schema())
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT item_instance_id FROM server_sealed_roles "
             "WHERE sealed_role_id=%u LIMIT 1",
             roleId);
    if (!vm_mysql_query(query, vm_net_mock_role_seal_u32_row, &ctx) ||
        ctx.invalid || !ctx.found)
        return false;
    return ctx.value != 0;
}

/* Drop any bound sealed roles that sticky restore leaked back into memory. */
static void vm_net_mock_role_seal_purge_bound_from_memory(void)
{
    u32 i = 0;

    if (!g_vm_net_mock_role_db_valid || g_vm_net_mock_role_db.roleCount == 0)
        return;
    if (!vm_net_mock_role_seal_ensure_schema())
        return;
    /* Walk backwards so splice indices stay valid. */
    for (i = g_vm_net_mock_role_db.roleCount; i > 0; --i)
    {
        u32 roleId = g_vm_net_mock_role_db.roles[i - 1u].roleId;
        if (roleId != 0 && vm_net_mock_role_seal_is_bound_role(roleId))
        {
            printf("[warn][network] mock_role_seal_purge_sticky role=%u "
                   "evidence=bound-role-must-not-be-in-account-memory\n",
                   roleId);
            vm_net_mock_role_seal_splice_memory(roleId);
        }
    }
}

/*
 * Recreate a missing account_roles row for an orphaned sealed binding.
 * Sticky save / CASCADE can leave server_sealed_roles pointing at a deleted
 * role_id; rebuild a minimal escrow row from preview and reattach orphans.
 */
static bool vm_net_mock_role_seal_recreate_missing_role(
    const vm_net_mock_role_seal_row_ctx *sealed)
{
    char escrowHex[160];
    char nameHex[96];
    char sceneHex[160];
    char query[1280];
    u8 escrowIndex = 0;
    static const char *const childTables[] = {
        "account_role_equipment",
        "account_role_equipment_durability",
        "account_role_skills",
        "account_role_backpack",
        "account_role_warehouse",
        "account_role_warehouse_bank",
        "account_role_item_effects",
        "account_role_offline_practise",
        "account_role_tasks",
        "account_role_npc_task_daily",
        "account_role_vitality",
        "account_role_ring_daily",
        "guild_members"};
    u32 i = 0;
    u32 level = 1;
    u32 job = 1;
    u32 sex = 0;
    u32 hp = 100;
    u16 posX = VM_NET_MOCK_ROLE_INITIAL_X;
    u16 posY = VM_NET_MOCK_ROLE_INITIAL_Y;
    const char *scene = NULL;

    if (sealed == NULL || sealed->sealedRoleId == 0)
        return false;
    if (!vm_net_mock_role_seal_ensure_schema())
        return false;
    if (vm_mysql_hex_encode(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT,
                            strlen(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT),
                            escrowHex, sizeof(escrowHex)) == 0)
        return false;
    if (sealed->previewName[0] != 0)
    {
        if (vm_mysql_hex_encode(sealed->previewName, strlen(sealed->previewName),
                                nameHex, sizeof(nameHex)) == 0)
            return false;
    }
    else
    {
        /* 「封印角色」 GBK */
        snprintf(nameHex, sizeof(nameHex), "B7E2D3A1BDC7C9AB");
    }
    /*
     * Never use ASCII placeholders like "xinshoucun" — client map enter needs
     * the real GBK .sce name (same as role-create / default Penglai).
     */
    scene = sealed->previewScene;
    if (scene == NULL || scene[0] == 0 || strchr(scene, '.') == NULL)
        scene = vm_net_mock_role_initial_scene_name();
    if (vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
        return false;
    level = sealed->previewLevel ? sealed->previewLevel : 1;
    job = sealed->previewJob;
    if (job == 0 || job > 3)
        job = 1;
    sex = sealed->previewSex ? 1u : 0u;
    hp = 50u + level * 10u;
    if (sealed->previewX != 0 && sealed->previewY != 0)
    {
        posX = sealed->previewX;
        posY = sealed->previewY;
    }
    if (!vm_net_mock_role_find_free_escrow_index(&escrowIndex))
        escrowIndex = 0;

    snprintf(query, sizeof(query),
             "INSERT INTO account_roles("
             "account_id,role_id,role_index,role_name,job,sex,backpack_capacity,"
             "level,exp,hp,hp_max,mp,mp_max,money,wcoin,kcoin,pk_points,"
             "pk_pve_streak,scene,pos_x,pos_y,backpack_item_count,designation_id,"
             "arena_designation_mask,next_backpack_seq) VALUES("
             "CAST(X'%s' AS CHAR),%u,%u,UNHEX('%s'),%u,%u,30,"
             "%u,0,%u,%u,%u,%u,0,0,0,0,"
             "0,UNHEX('%s'),%u,%u,0,0,"
             "0,1)",
             escrowHex, sealed->sealedRoleId, (u32)escrowIndex, nameHex, job, sex,
             level, hp, hp, hp / 2u, hp / 2u, sceneHex, (u32)posX, (u32)posY);
    if (!vm_mysql_exec(query))
    {
        printf("[error][network] mock_role_seal_recreate_fail role=%u error=%s\n",
               sealed->sealedRoleId, vm_mysql_last_error());
        return false;
    }

    /* Reattach any orphan child rows (sticky may have written them under the
     * seller account while account_roles stayed on escrow / got deleted). */
    for (i = 0; i < sizeof(childTables) / sizeof(childTables[0]); ++i)
    {
        if (!vm_net_mock_role_seal_table_exists(childTables[i]))
            continue;
        snprintf(query, sizeof(query),
                 "UPDATE `%s` SET account_id=CAST(X'%s' AS CHAR) "
                 "WHERE role_id=%u",
                 childTables[i], escrowHex, sealed->sealedRoleId);
        (void)vm_mysql_exec(query);
    }
    printf("[warn][network] mock_role_seal_recreate_ok role=%u level=%u "
           "scene=%s evidence=orphaned-sealed-binding\n",
           sealed->sealedRoleId, level, scene);
    return true;
}

/* Existing seal binding for role, or 0. */
static u32 vm_net_mock_role_seal_find_instance_by_role(u32 roleId)
{
    char query[192];
    vm_net_mock_role_seal_u32_ctx ctx;

    if (roleId == 0 || !vm_net_mock_role_seal_ensure_schema())
        return 0;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT item_instance_id FROM server_sealed_roles "
             "WHERE sealed_role_id=%u LIMIT 1",
             roleId);
    if (!vm_mysql_query(query, vm_net_mock_role_seal_u32_row, &ctx) ||
        ctx.invalid || !ctx.found)
        return 0;
    return ctx.value;
}

static bool vm_net_mock_role_seal_instance_exists(u32 instanceId)
{
    char query[192];
    vm_net_mock_role_seal_u32_ctx ctx;

    if (instanceId == 0 || !vm_net_mock_role_seal_ensure_schema())
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT sealed_role_id FROM server_sealed_roles "
             "WHERE item_instance_id=%u LIMIT 1",
             instanceId);
    if (!vm_mysql_query(query, vm_net_mock_role_seal_u32_row, &ctx) ||
        ctx.invalid)
        return false;
    return ctx.found && ctx.value != 0;
}

static bool vm_net_mock_role_seal_load_by_instance(
    u32 instanceId, vm_net_mock_role_seal_row_ctx *out)
{
    char query[320];

    if (out == NULL || instanceId == 0 || !vm_net_mock_role_seal_ensure_schema())
        return false;
    memset(out, 0, sizeof(*out));
    snprintf(query, sizeof(query),
             "SELECT sealed_role_id,HEX(preview_name),preview_level,preview_job,"
             "preview_sex,HEX(preview_scene),preview_pos_x,preview_pos_y "
             "FROM server_sealed_roles "
             "WHERE item_instance_id=%u LIMIT 1",
             instanceId);
    (void)vm_mysql_query(query, vm_net_mock_role_seal_row_cb, out);
    return out->found && !out->invalid && out->sealedRoleId != 0;
}

static bool vm_net_mock_role_is_online(u32 roleId)
{
    vm_mock_service_client_session *s = g_vm_mock_service_client_sessions;

    if (roleId == 0)
        return false;
    for (; s != NULL; s = s->next)
    {
        if (s->roleOnline && s->onlineRoleId == roleId)
            return true;
    }
    return false;
}

static bool vm_net_mock_role_find_free_index(const char *accountId, u8 *indexOut)
{
    char accountHex[160];
    char query[320];
    vm_net_mock_role_seal_index_scan_ctx ctx;
    u8 i = 0;

    if (indexOut)
        *indexOut = 0;
    if (accountId == NULL || accountId[0] == 0 ||
        vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                            sizeof(accountHex)) == 0)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT role_index FROM account_roles "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_index<%u",
             accountHex, (u32)VM_NET_MOCK_ROLE_DB_MAX_ROLES);
    if (!vm_mysql_query(query, vm_net_mock_role_seal_index_scan_row, &ctx) ||
        ctx.invalid)
        return false;
    for (i = 0; i < (u8)VM_NET_MOCK_ROLE_DB_MAX_ROLES; ++i)
    {
        if (!ctx.used[i])
        {
            if (indexOut)
                *indexOut = i;
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_role_find_free_escrow_index(u8 *indexOut)
{
    char escrowHex[160];
    char query[320];
    vm_net_mock_role_seal_escrow_scan_ctx ctx;
    u32 i = 0;

    if (indexOut)
        *indexOut = 0;
    if (vm_mysql_hex_encode(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT,
                            strlen(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT),
                            escrowHex, sizeof(escrowHex)) == 0)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT role_index FROM account_roles "
             "WHERE account_id=CAST(X'%s' AS CHAR)",
             escrowHex);
    if (!vm_mysql_query(query, vm_net_mock_role_seal_escrow_scan_row, &ctx) ||
        ctx.invalid)
        return false;
    if (ctx.maxIndex < 254u && !ctx.used[ctx.maxIndex + 1u])
    {
        if (indexOut)
            *indexOut = (u8)(ctx.maxIndex + 1u);
        return true;
    }
    for (i = 0; i < 255u; ++i)
    {
        if (!ctx.used[i])
        {
            if (indexOut)
                *indexOut = (u8)i;
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_role_db_reassign_account(u32 roleId,
                                                 const char *fromAccount,
                                                 const char *toAccount,
                                                 u8 newRoleIndex,
                                                 const char **reasonOut)
{
    char fromHex[160];
    char toHex[160];
    char query[768];
    char ownerAfter[64];
    /* guild_applications uses applicant_account_id/applicant_role_id — not
     * account_id/role_id. Clearing it is handled separately below.
     * account_role_tianxia_worship_daily is role_id-only (no account_id). */
    static const char *const childTables[] = {
        "account_role_equipment",
        "account_role_equipment_durability",
        "account_role_skills",
        "account_role_backpack",
        "account_role_warehouse",
        "account_role_warehouse_bank",
        "account_role_item_effects",
        "account_role_offline_practise",
        "account_role_tasks",
        "account_role_npc_task_daily",
        "account_role_vitality",
        "account_role_ring_daily",
        "guild_members"};
    u32 i = 0;
    vm_net_mock_role_seal_u32_ctx affected;

    if (reasonOut)
        *reasonOut = "ok";
    if (roleId == 0 || fromAccount == NULL || toAccount == NULL ||
        fromAccount[0] == 0 || toAccount[0] == 0 ||
        strcmp(fromAccount, toAccount) == 0)
    {
        if (reasonOut)
            *reasonOut = "bad-args";
        return false;
    }
    if (vm_mysql_hex_encode(fromAccount, strlen(fromAccount), fromHex,
                            sizeof(fromHex)) == 0 ||
        vm_mysql_hex_encode(toAccount, strlen(toAccount), toHex,
                            sizeof(toHex)) == 0)
    {
        if (reasonOut)
            *reasonOut = "hex-encode";
        return false;
    }
    /* Destination must have role_state — escrow is created in ensure_schema,
     * but buyers/sellers can still miss a row on older DBs. */
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO account_role_state"
             "(account_id,format_version,active_role_id,role_count) "
             "VALUES(CAST(X'%s' AS CHAR),1,0,0)",
             toHex);
    if (!vm_mysql_exec(query))
    {
        if (reasonOut)
            *reasonOut = "ensure-to-state";
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO account_role_state"
             "(account_id,format_version,active_role_id,role_count) "
             "VALUES(CAST(X'%s' AS CHAR),1,0,0)",
             fromHex);
    (void)vm_mysql_exec(query);

    if (!vm_mysql_exec("START TRANSACTION") ||
        !vm_mysql_exec("SET FOREIGN_KEY_CHECKS=0"))
    {
        if (reasonOut)
            *reasonOut = "tx-begin";
        (void)vm_mysql_exec("ROLLBACK");
        return false;
    }

    snprintf(query, sizeof(query),
             "UPDATE account_roles SET account_id=CAST(X'%s' AS CHAR),"
             "role_index=%u WHERE role_id=%u AND account_id=CAST(X'%s' AS CHAR)",
             toHex, (u32)newRoleIndex, roleId, fromHex);
    if (!vm_mysql_exec(query))
    {
        if (reasonOut)
            *reasonOut = "update-account-roles";
        printf("[error][network] mock_role_reassign_update_roles role=%u "
               "from=%s to=%s idx=%u error=%s\n",
               roleId, fromAccount, toAccount, (u32)newRoleIndex,
               vm_mysql_last_error());
        goto fail;
    }
    memset(&affected, 0, sizeof(affected));
    if (!vm_mysql_query("SELECT ROW_COUNT()", vm_net_mock_role_seal_u32_row,
                        &affected) ||
        affected.invalid || !affected.found)
    {
        /* Fall through to ownership verify — ROW_COUNT can be flaky. */
        affected.value = 0;
    }
    if (affected.value != 1u)
    {
        ownerAfter[0] = 0;
        if (vm_net_mock_role_seal_lookup_owner(roleId, ownerAfter,
                                               sizeof(ownerAfter)) &&
            strcmp(ownerAfter, toAccount) == 0)
        {
            snprintf(query, sizeof(query),
                     "UPDATE account_roles SET role_index=%u WHERE role_id=%u",
                     (u32)newRoleIndex, roleId);
            (void)vm_mysql_exec(query);
            printf("[info][network] mock_role_reassign_already_on_target "
                   "role=%u account=%s\n",
                   roleId, toAccount);
        }
        else
        {
            if (reasonOut)
                *reasonOut = "account-roles-miss";
            printf("[error][network] mock_role_reassign_miss role=%u from=%s "
                   "to=%s owner=%s row_count=%u error=%s\n",
                   roleId, fromAccount, toAccount,
                   ownerAfter[0] ? ownerAfter : "-", affected.value,
                   vm_mysql_last_error());
            goto fail;
        }
    }

    for (i = 0; i < sizeof(childTables) / sizeof(childTables[0]); ++i)
    {
        if (!vm_net_mock_role_seal_table_exists(childTables[i]))
        {
            printf("[warn][network] mock_role_reassign_skip_missing table=%s\n",
                   childTables[i]);
            continue;
        }
        snprintf(query, sizeof(query),
                 "UPDATE `%s` SET account_id=CAST(X'%s' AS CHAR) "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
                 childTables[i], toHex, fromHex, roleId);
        if (!vm_mysql_exec(query))
        {
            /* Optional / legacy tables: never abort the whole transfer. */
            printf("[warn][network] mock_role_reassign_skip_child table=%s "
                   "role=%u error=%s\n",
                   childTables[i], roleId, vm_mysql_last_error());
            continue;
        }
    }
    if (vm_net_mock_role_seal_table_exists("account_role_kcoin_payee"))
    {
        snprintf(query, sizeof(query),
                 "UPDATE account_role_kcoin_payee SET account_id=CAST(X'%s' AS CHAR) "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
                 toHex, fromHex, roleId);
        (void)vm_mysql_exec(query);
    }
    /* Drop pending/history apps; column names differ from other child tables. */
    if (vm_net_mock_role_seal_table_exists("guild_applications"))
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM guild_applications WHERE applicant_role_id=%u",
                 roleId);
        if (!vm_mysql_exec(query))
        {
            printf("[warn][network] mock_role_reassign_skip_apps role=%u "
                   "error=%s\n",
                   roleId, vm_mysql_last_error());
        }
    }

    if (vm_net_mock_role_seal_table_exists("friendships"))
    {
        snprintf(query, sizeof(query),
                 "UPDATE friendships SET owner_account_id=CAST(X'%s' AS CHAR) "
                 "WHERE owner_role_id=%u",
                 toHex, roleId);
        if (!vm_mysql_exec(query))
        {
            printf("[warn][network] mock_role_reassign_skip_friends_owner "
                   "role=%u error=%s\n",
                   roleId, vm_mysql_last_error());
        }
        snprintf(query, sizeof(query),
                 "UPDATE friendships SET target_account_id=CAST(X'%s' AS CHAR) "
                 "WHERE target_role_id=%u",
                 toHex, roleId);
        if (!vm_mysql_exec(query))
        {
            printf("[warn][network] mock_role_reassign_skip_friends_target "
                   "role=%u error=%s\n",
                   roleId, vm_mysql_last_error());
        }
    }

    /* Clear spouse links either side of this role (columns may be empty). */
    snprintf(query, sizeof(query),
             "UPDATE account_roles SET spouse_account_id='',spouse_role_id=0,"
             "spouse_name='' WHERE role_id=%u OR spouse_role_id=%u",
             roleId, roleId);
    if (!vm_mysql_exec(query))
    {
        printf("[warn][network] mock_role_reassign_skip_spouse role=%u error=%s\n",
               roleId, vm_mysql_last_error());
    }

    /* Patch guild leader FK if this role still leads a guild. */
    if (vm_net_mock_role_seal_table_exists("guilds"))
    {
        snprintf(query, sizeof(query),
                 "UPDATE guilds SET leader_account_id=CAST(X'%s' AS CHAR) "
                 "WHERE leader_role_id=%u",
                 toHex, roleId);
        (void)vm_mysql_exec(query);
    }

    snprintf(query, sizeof(query),
             "UPDATE account_role_state SET role_count=("
             "SELECT COUNT(*) FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR)"
             ") WHERE account_id=CAST(X'%s' AS CHAR)",
             fromHex, fromHex);
    if (!vm_mysql_exec(query))
    {
        if (reasonOut)
            *reasonOut = "count-from";
        goto fail;
    }
    snprintf(query, sizeof(query),
             "UPDATE account_role_state SET role_count=("
             "SELECT COUNT(*) FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR)"
             ") WHERE account_id=CAST(X'%s' AS CHAR)",
             toHex, toHex);
    if (!vm_mysql_exec(query))
    {
        if (reasonOut)
            *reasonOut = "count-to";
        goto fail;
    }

    /* Final verify — do not trust ROW_COUNT alone. */
    ownerAfter[0] = 0;
    if (!vm_net_mock_role_seal_lookup_owner(roleId, ownerAfter,
                                            sizeof(ownerAfter)) ||
        strcmp(ownerAfter, toAccount) != 0)
    {
        if (reasonOut)
            *reasonOut = "verify-owner";
        printf("[error][network] mock_role_reassign_verify_fail role=%u "
               "want=%s got=%s\n",
               roleId, toAccount, ownerAfter[0] ? ownerAfter : "-");
        goto fail;
    }

    if (!vm_mysql_exec("SET FOREIGN_KEY_CHECKS=1") ||
        !vm_mysql_exec("COMMIT"))
    {
        if (reasonOut)
            *reasonOut = "commit";
        goto fail;
    }
    return true;

fail:
    (void)vm_mysql_exec("SET FOREIGN_KEY_CHECKS=1");
    (void)vm_mysql_exec("ROLLBACK");
    return false;
}

static void vm_net_mock_role_seal_splice_memory(u32 roleId)
{
    u32 deleteIndex = VM_NET_MOCK_ROLE_DB_MAX_ROLES;
    u32 i = 0;

    if (roleId == 0)
        return;
    for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == roleId)
        {
            deleteIndex = i;
            break;
        }
    }
    if (deleteIndex == (u32)VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        return;
    for (i = deleteIndex; i + 1 < g_vm_net_mock_role_db.roleCount; ++i)
        g_vm_net_mock_role_db.roles[i] = g_vm_net_mock_role_db.roles[i + 1];
    if (g_vm_net_mock_role_db.roleCount > 0)
    {
        memset(&g_vm_net_mock_role_db.roles[g_vm_net_mock_role_db.roleCount - 1],
               0,
               sizeof(g_vm_net_mock_role_db.roles[0]));
        g_vm_net_mock_role_db.roleCount -= 1;
    }
    if (g_vm_net_mock_role_db.roleCount == 0)
        g_vm_net_mock_role_db.activeRoleId = 0;
    else if (g_vm_net_mock_role_db.activeRoleId == roleId)
        g_vm_net_mock_role_db.activeRoleId = g_vm_net_mock_role_db.roles[0].roleId;
}

static bool vm_net_mock_role_seal_grant_bound_scroll(vm_net_mock_role_state *role,
                                                     u32 instanceId,
                                                     u16 *seqOut)
{
    u32 itemCount = 0;
    u32 freeSlots = 0;
    vm_net_mock_backpack_item_state *item = NULL;

    if (seqOut)
        *seqOut = 0;
    if (role == NULL || instanceId == 0)
        return false;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    freeSlots = role->backpackCapacity > itemCount
                    ? (u32)role->backpackCapacity - itemCount
                    : 0;
    if (freeSlots < 1)
    {
        /* Stacked blank on a full bag: grow capacity one slot for the bound
         * scroll (normalize would otherwise drop the overflow row). */
        if (role->backpackCapacity < VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT &&
            itemCount < VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        {
            role->backpackCapacity = (u16)(role->backpackCapacity + 1);
            freeSlots = 1;
            printf("[warn][network] mock_role_seal_grant_grow_cap role=%u "
                   "cap=%u items=%u\n",
                   role->roleId, role->backpackCapacity, itemCount);
        }
        else
            return false;
    }
    if (itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        return false;
    item = &role->backpackItems[itemCount];
    memset(item, 0, sizeof(*item));
    item->itemId = VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID;
    item->seq = role->nextBackpackSeq ? role->nextBackpackSeq : 1;
    item->count = VM_NET_MOCK_ROLE_SEAL_SCROLL_DURABILITY;
    item->enhanceInstanceId = instanceId;
    if (seqOut)
        *seqOut = item->seq;
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
    role->backpackItemCount = (u16)(itemCount + 1);
    vm_net_mock_role_normalize_backpack(role);
    vm_net_mock_role_mark_inventory_dirty("role-seal-grant-bound");
    return true;
}

/*
 * Turn blank 857 into a bound scroll.  Prefer in-place bind when the chosen
 * stack has count==1 so a full backpack still works (consume+grant would need
 * a free slot after leaving a leftover blank stack).
 */
static bool vm_net_mock_role_seal_bind_scroll(vm_net_mock_role_state *role,
                                              u16 cardSeq, u32 instanceId,
                                              u32 *remainingOut, u16 *boundSeqOut)
{
    vm_net_mock_backpack_item_state *card = NULL;

    if (remainingOut)
        *remainingOut = 0;
    if (boundSeqOut)
        *boundSeqOut = 0;
    if (role == NULL || cardSeq == 0 || instanceId == 0)
        return false;
    vm_net_mock_role_normalize_backpack(role);
    card = vm_net_mock_role_find_backpack_item(
        role, VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID, cardSeq);
    if (card == NULL || card->count == 0 ||
        card->itemId != VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID)
    {
        printf("[error][network] mock_role_seal_bind missing seq=%u\n", cardSeq);
        return false;
    }
    if (card->enhanceInstanceId != 0)
    {
        /* Already bound to this (or any) instance — treat as success. */
        if (remainingOut)
            *remainingOut = card->count;
        if (boundSeqOut)
            *boundSeqOut = card->seq;
        printf("[info][network] mock_role_seal_bind already instance=%u seq=%u\n",
               card->enhanceInstanceId, card->seq);
        return true;
    }

    if (card->count == 1u)
    {
        card->enhanceInstanceId = instanceId;
        card->count = VM_NET_MOCK_ROLE_SEAL_SCROLL_DURABILITY;
        if (remainingOut)
            *remainingOut = card->count;
        if (boundSeqOut)
            *boundSeqOut = card->seq;
        vm_net_mock_role_mark_inventory_dirty("role-seal-bind-inplace");
        return true;
    }

    if (!vm_net_mock_role_consume_backpack_item(
            role, VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID, cardSeq, 1u,
            remainingOut))
    {
        printf("[error][network] mock_role_seal_bind consume seq=%u count=%u\n",
               cardSeq, card->count);
        return false;
    }
    if (!vm_net_mock_role_seal_grant_bound_scroll(role, instanceId, boundSeqOut))
    {
        (void)vm_net_mock_role_add_backpack_item_to_role(
            role, VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID, 1, 0, NULL,
            "role-seal-bind-rollback", NULL, 0);
        printf("[error][network] mock_role_seal_bind grant_full role=%u "
               "items=%u cap=%u\n",
               role->roleId, vm_net_mock_role_backpack_count(role),
               role->backpackCapacity);
        return false;
    }
    return true;
}

static bool vm_net_mock_role_seal_detach_guild(u32 roleId, const char **reasonOut)
{
    vm_net_mock_guild_record guild;
    u8 rank = 0;
    char accountHex[160];
    char query[384];
    const char *accountId = g_vm_mock_service_active_account_id;

    if (reasonOut)
        *reasonOut = "ok";
    if (roleId == 0 || accountId == NULL || accountId[0] == 0)
        return true;
    memset(&guild, 0, sizeof(guild));
    if (!vm_net_mock_guild_find_membership_for_account(accountId, roleId, &guild,
                                                       &rank) ||
        guild.guildId == 0)
        return true;
    if (rank == 1)
    {
        if (!vm_net_mock_guild_find_by_id(guild.guildId, &guild) ||
            guild.memberCount > 1u)
        {
            if (reasonOut)
                *reasonOut = "guild-leader-nonempty";
            return false;
        }
    }
    if (vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                            sizeof(accountHex)) == 0)
    {
        if (reasonOut)
            *reasonOut = "guild-hex";
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM guild_members WHERE guild_id=%u AND role_id=%u "
             "AND account_id=CAST(X'%s' AS CHAR)",
             guild.guildId, roleId, accountHex);
    if (!vm_mysql_exec(query))
    {
        if (reasonOut)
            *reasonOut = "guild-member-delete";
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM guild_applications WHERE guild_id=%u "
             "AND applicant_role_id=%u "
             "AND applicant_account_id=CAST(X'%s' AS CHAR)",
             guild.guildId, roleId, accountHex);
    (void)vm_mysql_exec(query);
    if (rank == 1)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM guild_applications WHERE guild_id=%u",
                 guild.guildId);
        (void)vm_mysql_exec(query);
        snprintf(query, sizeof(query), "DELETE FROM guilds WHERE guild_id=%u",
                 guild.guildId);
        (void)vm_mysql_exec(query);
    }
    return true;
}

static u32 vm_net_mock_build_role_seal_response(const u8 *request, u32 requestLen,
                                                u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 cardSeqValue = 0;
    u32 targetRoleValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 cardSeq = 0;
    u32 cardRemaining = 0;
    u32 instanceId = 0;
    u16 boundSeq = 0;
    const char *reason = "ok";
    const char *guildReason = "ok";
    const char *reassignReason = "ok";
    const char *accountId = NULL;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state *target = NULL;
    vm_net_mock_backpack_item_state *card = NULL;
    u8 escrowIndex = 0;
    char nameHex[96];
    char accountHex[160];
    char escrowHex[160];
    char sceneHex[160];
    char query[960];
    u32 i = 0;
    u32 sealedAt = 0;
    bool didReassign = false;
    bool didInsertSealed = false;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 25 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "cardseq", &cardSeqValue) ||
        cardSeqValue == 0 || cardSeqValue > 0xffffu)
    {
        return 0;
    }
    cardSeq = (u16)cardSeqValue;
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "targetrole", &targetRoleValue) ||
        targetRoleValue == 0)
    {
        result = 3;
        reason = "bad-target";
    }

    role = vm_net_mock_active_role();
    accountId = g_vm_mock_service_active_account_id;
    if (result == 1 && (role == NULL || accountId == NULL || accountId[0] == 0))
    {
        result = 2;
        reason = "no-role";
    }
    else if (result == 1 && !vm_net_mock_role_seal_ensure_schema())
    {
        result = 10;
        reason = "schema";
    }
    else if (result == 1)
    {
        vm_net_mock_role_normalize_backpack(role);
        card = vm_net_mock_role_find_backpack_item(role, 0, cardSeq);
        if (card == NULL || card->count == 0 ||
            card->itemId != VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID)
        {
            result = 2;
            reason = "scroll-missing";
        }
        else if (card->enhanceInstanceId != 0)
        {
            result = 11;
            reason = "already-bound";
        }
        else if ((u32)role->roleId == targetRoleValue)
        {
            result = 4;
            reason = "target-self";
        }
        else if (g_vm_net_mock_role_db.roleCount < 2u)
        {
            result = 6;
            reason = "need-two-roles";
        }
        else
        {
            for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
            {
                if (g_vm_net_mock_role_db.roles[i].roleId == targetRoleValue)
                {
                    target = &g_vm_net_mock_role_db.roles[i];
                    break;
                }
            }
            if (target == NULL)
            {
                result = 7;
                reason = "not-sibling";
            }
            else if (vm_net_mock_role_is_online(target->roleId))
            {
                result = 5;
                reason = "target-online";
            }
        }
    }

    if (result == 1 && target != NULL)
    {
        if (!vm_net_mock_role_seal_detach_guild(target->roleId, &guildReason))
        {
            result = 8;
            reason = guildReason;
        }
    }

    if (result == 1 && role != NULL && target != NULL && card != NULL)
    {
        /* Only persist the caster role. Full-account pre-seal rewrote sibling
         * rows under this account and broke half-sealed escrow FK recovery. */
        if (!vm_net_mock_role_db_save_role("pre-seal", role->roleId))
        {
            result = 10;
            reason = "pre-save";
        }
        else if (!vm_net_mock_role_find_free_escrow_index(&escrowIndex))
        {
            char ownerTmp[64];

            ownerTmp[0] = 0;
            /* Half-seal may already occupy escrow; no new slot needed. */
            if (vm_net_mock_role_seal_lookup_owner(target->roleId, ownerTmp,
                                                   sizeof(ownerTmp)) &&
                strcmp(ownerTmp, VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT) == 0)
            {
                escrowIndex = 0;
            }
            else
            {
                result = 10;
                reason = "escrow-index";
            }
        }
        if (result == 1)
        {
            instanceId = vm_net_mock_equipment_instance_alloc();
            if (instanceId == 0)
            {
                result = 12;
                reason = "instance-alloc";
                printf("[error][network] mock_role_seal instance_alloc error=%s\n",
                       vm_mysql_last_error());
            }
        }
    }

    if (result == 1 && role != NULL && target != NULL && instanceId != 0)
    {
        u32 existingInstance = 0;
        char ownerAccount[64];

        sealedAt = (u32)time(NULL);
        ownerAccount[0] = 0;
        didReassign = false;
        didInsertSealed = false;
        existingInstance =
            vm_net_mock_role_seal_find_instance_by_role(target->roleId);
        if (vm_mysql_hex_encode(target->name, strlen(target->name), nameHex,
                                sizeof(nameHex)) == 0 ||
            vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                                sizeof(accountHex)) == 0 ||
            vm_mysql_hex_encode(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT,
                                strlen(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT),
                                escrowHex, sizeof(escrowHex)) == 0)
        {
            result = 10;
            reason = "hex";
        }
        else if (!vm_net_mock_role_seal_lookup_owner(target->roleId, ownerAccount,
                                                     sizeof(ownerAccount)))
        {
            result = 10;
            reason = "role-missing-db";
        }
        else if (strcmp(ownerAccount, accountId) != 0 &&
                 strcmp(ownerAccount, VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT) != 0)
        {
            result = 10;
            reason = "role-wrong-account";
        }
        else
        {
            /* Reuse prior half-seal binding so INSERT unique key cannot fail. */
            if (existingInstance != 0)
            {
                instanceId = existingInstance;
                printf("[info][network] mock_role_seal_reuse_instance "
                       "role=%u instance=%u\n",
                       target->roleId, instanceId);
            }

            if (strcmp(ownerAccount, VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT) != 0)
            {
                if (!vm_net_mock_role_db_reassign_account(
                        target->roleId, accountId,
                        VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT, escrowIndex,
                        &reassignReason))
                {
                    result = 10;
                    reason = reassignReason;
                }
                else
                {
                    didReassign = true;
                }
            }
            else
            {
                printf("[info][network] mock_role_seal_already_escrow role=%u\n",
                       target->roleId);
            }

            if (result == 1 && existingInstance == 0)
            {
                const char *sealScene = target->scene;
                if (sealScene == NULL || sealScene[0] == 0 ||
                    strchr(sealScene, '.') == NULL)
                    sealScene = vm_net_mock_role_initial_scene_name();
                if (vm_mysql_hex_encode(sealScene, strlen(sealScene), sceneHex,
                                        sizeof(sceneHex)) == 0)
                {
                    result = 10;
                    reason = "hex-scene";
                }
                else
                {
                snprintf(query, sizeof(query),
                         "INSERT INTO server_sealed_roles("
                         "item_instance_id,sealed_role_id,origin_account_id,"
                         "preview_name,preview_level,preview_job,preview_sex,"
                         "preview_scene,preview_pos_x,preview_pos_y,"
                         "sealed_at) VALUES(%u,%u,CAST(X'%s' AS CHAR),UNHEX('%s'),"
                         "%u,%u,%u,UNHEX('%s'),%u,%u,%u)",
                         instanceId, target->roleId, accountHex, nameHex,
                         target->level, (u32)target->job, (u32)target->sex,
                         sceneHex,
                         (u32)target->x, (u32)target->y, sealedAt);
                if (!vm_mysql_exec(query))
                {
                    result = 10;
                    reason = "insert-sealed";
                    printf("[error][network] mock_role_seal_insert_fail "
                           "role=%u instance=%u error=%s\n",
                           target->roleId, instanceId, vm_mysql_last_error());
                }
                else
                {
                    didInsertSealed = true;
                }
                }
            }
        }

        if (result != 1)
        {
            const char *undoReason = "ok";
            u8 restoreIndex = 0;

            if (didInsertSealed)
            {
                char delQ[160];
                snprintf(delQ, sizeof(delQ),
                         "DELETE FROM server_sealed_roles WHERE "
                         "item_instance_id=%u",
                         instanceId);
                (void)vm_mysql_exec(delQ);
                didInsertSealed = false;
            }
            if (didReassign &&
                vm_net_mock_role_find_free_index(accountId, &restoreIndex))
            {
                (void)vm_net_mock_role_db_reassign_account(
                    target->roleId, VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT,
                    accountId, restoreIndex, &undoReason);
                didReassign = false;
            }
        }
    }

    if (result == 1 && role != NULL && target != NULL && card != NULL &&
        instanceId != 0)
    {
        u32 sealedRoleId = target->roleId;
        u32 keepActiveRoleId = role->roleId;

        if (!vm_net_mock_role_seal_bind_scroll(role, cardSeq, instanceId,
                                               &cardRemaining, &boundSeq))
        {
            result = 9;
            reason = "bind-scroll-failed";
        }
        else
        {
            /*
             * Splice BEFORE save_role.  save_role → note_durable_role_write
             * copies g_vm_net_mock_role_db into account_state.roleDb; doing
             * that with the sealed sibling still present left sticky restore
             * able to bring the role (and blank scroll) back after capture
             * skip — tip said 封印成功 but list/login/bag looked unchanged.
             */
            vm_net_mock_role_seal_splice_memory(sealedRoleId);
            /* Full snapshot renumbers role_index 0..n-1 for remaining roles.
             * Scoped save left gaps that broke login (role_index >= role_count). */
            if (!vm_net_mock_role_db_save_relational_ex(
                    "role-seal", g_vm_mock_service_active_account_id,
                    &g_vm_net_mock_role_db, g_vm_net_mock_role_db_valid, NULL,
                    true, NULL, NULL, 0, true, 0, NULL))
            {
                result = 13;
                reason = "save-after-seal";
                printf("[error][network] mock_role_seal save_after error=%s "
                       "role=%u instance=%u roles=%u\n",
                       vm_mysql_last_error(), keepActiveRoleId, instanceId,
                       g_vm_net_mock_role_db.roleCount);
            }
            else
            {
                reason = "seal-ok";
            }
        }

        if (result != 1)
        {
            char delQ[160];
            const char *undoReason = "ok";
            u8 restoreIndex = 0;
            vm_mock_service_account_state *account = NULL;

            if (didInsertSealed)
            {
                snprintf(delQ, sizeof(delQ),
                         "DELETE FROM server_sealed_roles WHERE "
                         "item_instance_id=%u",
                         instanceId);
                (void)vm_mysql_exec(delQ);
                didInsertSealed = false;
            }
            if (didReassign &&
                vm_net_mock_role_find_free_index(accountId, &restoreIndex))
            {
                (void)vm_net_mock_role_db_reassign_account(
                    sealedRoleId, VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT, accountId,
                    restoreIndex, &undoReason);
                didReassign = false;
            }
            /* Memory may already be spliced; force MySQL reload like unseal. */
            g_vm_net_mock_role_inventory_dirty = false;
            g_vm_net_mock_role_position_dirty = false;
            g_vm_net_mock_role_db_loaded = false;
            g_vm_net_mock_role_db_valid = false;
            account = g_vm_mock_service_accounts;
            while (account != NULL)
            {
                if (strcmp(account->accountId, accountId) == 0)
                {
                    account->roleDbLoaded = false;
                    account->roleDbValid = false;
                    account->roleInventoryDirty = false;
                    account->rolePositionDirty = false;
                    break;
                }
                account = account->next;
            }
            vm_net_mock_role_db_load();
            if (account != NULL && g_vm_net_mock_role_db_valid)
            {
                account->roleDb = g_vm_net_mock_role_db;
                account->roleDbLoaded = true;
                account->roleDbValid = true;
                g_vm_mock_service_globals_account = account;
            }
            (void)vm_net_mock_select_active_role(keepActiveRoleId);
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 25,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "cardseq", cardSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining",
                                    cardRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "targetrole",
                                    result == 1 ? targetRoleValue : 0) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "instanceid",
                                    result == 1 ? instanceId : 0))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_role_seal card_seq=%u target=%u instance=%u "
           "remain=%u result=%u reason=%s resp=29/25\n",
           cardSeq, targetRoleValue, instanceId, cardRemaining, result, reason);
    vm_autotest_note(
        "mock_role_seal card=%u target=%u result=%u reason=%s "
        "evidence=pixijs-only-29/25\n",
        cardSeq, targetRoleValue, result, reason);
    return pos;
}

static u32 vm_net_mock_build_role_unseal_response(const u8 *request,
                                                   u32 requestLen, u8 *out,
                                                   u32 outCap)
{
    u32 offset = 4;
    u32 cardSeqValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 cardSeq = 0;
    u32 cardRemaining = 0;
    u32 sealedRoleId = 0;
    u32 instanceId = 0;
    u8 freeIndex = 0;
    const char *reason = "ok";
    const char *reassignReason = "ok";
    const char *accountId = NULL;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *card = NULL;
    vm_net_mock_role_seal_row_ctx sealed;
    char query[256];
    vm_mock_service_account_state *account = NULL;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 26 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "cardseq", &cardSeqValue) ||
        cardSeqValue == 0 || cardSeqValue > 0xffffu)
    {
        return 0;
    }
    cardSeq = (u16)cardSeqValue;

    role = vm_net_mock_active_role();
    accountId = g_vm_mock_service_active_account_id;
    if (role == NULL || accountId == NULL || accountId[0] == 0)
    {
        result = 2;
        reason = "no-role";
    }
    else if (!vm_net_mock_role_seal_ensure_schema())
    {
        result = 5;
        reason = "schema";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        card = vm_net_mock_role_find_backpack_item(role, 0, cardSeq);
        if (card == NULL || card->count == 0 ||
            card->itemId != VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID)
        {
            result = 2;
            reason = "scroll-missing";
        }
        else if (card->enhanceInstanceId == 0)
        {
            result = 3;
            reason = "not-bound";
        }
        else
        {
            instanceId = card->enhanceInstanceId;
            memset(&sealed, 0, sizeof(sealed));
            if (!vm_net_mock_role_seal_load_by_instance(instanceId, &sealed))
            {
                result = 3;
                reason = "no-sealed-row";
            }
            else
            {
                sealedRoleId = sealed.sealedRoleId;
            }
        }
    }

    if (result == 1)
    {
        char ownerAccount[64];

        ownerAccount[0] = 0;
        if (vm_net_mock_role_is_online(sealedRoleId))
        {
            /* Escrow roles should never be online; treat as reassign fail. */
            result = 5;
            reason = "sealed-online";
        }
        else if (!vm_net_mock_role_seal_lookup_owner(sealedRoleId, ownerAccount,
                                                     sizeof(ownerAccount)))
        {
            /* Sticky/CASCADE can leave server_sealed_roles with no account_roles
             * row. Rebuild a minimal escrow role from preview so unseal can
             * still reassign to the buyer. */
            if (!vm_net_mock_role_seal_recreate_missing_role(&sealed) ||
                !vm_net_mock_role_seal_lookup_owner(sealedRoleId, ownerAccount,
                                                   sizeof(ownerAccount)))
            {
                result = 5;
                reason = "role-missing-db";
            }
            else
            {
                printf("[warn][network] mock_role_unseal_recreated role=%u "
                       "owner=%s instance=%u evidence=orphaned-sealed-binding\n",
                       sealedRoleId, ownerAccount, instanceId);
            }
        }

        if (result == 1 && strcmp(ownerAccount, accountId) == 0)
        {
            /* Already on this account (half-unseal / sticky restore). Keep
             * current role_index — do not require a free slot. */
            printf("[info][network] mock_role_unseal_already_owned role=%u "
                   "account=%s\n",
                   sealedRoleId, accountId);
            reason = "already-owned";
            snprintf(query, sizeof(query),
                     "DELETE FROM server_sealed_roles WHERE item_instance_id=%u",
                     instanceId);
            if (!vm_mysql_exec(query))
            {
                result = 5;
                reason = "delete-sealed";
            }
        }
        else if (result == 1 &&
                 !vm_net_mock_role_find_free_index(accountId, &freeIndex))
        {
            result = 4;
            reason = "slots-full";
        }
        else if (result == 1 &&
                 !vm_net_mock_role_db_reassign_account(
                     sealedRoleId, ownerAccount, accountId, freeIndex,
                     &reassignReason))
        {
            result = 5;
            reason = reassignReason;
            printf("[error][network] mock_role_unseal_reassign_fail role=%u "
                   "from=%s to=%s reason=%s\n",
                   sealedRoleId, ownerAccount, accountId, reassignReason);
        }
        else if (result == 1)
        {
            snprintf(query, sizeof(query),
                     "DELETE FROM server_sealed_roles WHERE item_instance_id=%u",
                     instanceId);
            if (!vm_mysql_exec(query))
            {
                u8 escrowIndex = 0;
                const char *undo = "ok";
                if (vm_net_mock_role_find_free_escrow_index(&escrowIndex))
                    (void)vm_net_mock_role_db_reassign_account(
                        sealedRoleId, accountId,
                        VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT, escrowIndex, &undo);
                result = 5;
                reason = "delete-sealed";
            }
        }
    }

    if (result == 1 && role != NULL && card != NULL)
    {
        if (!vm_net_mock_role_consume_backpack_item(
                role, VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID, cardSeq, 1u,
                &cardRemaining))
        {
            u8 escrowIndex = 0;
            const char *undo = "ok";
            char restoreQ[640];
            char accountHex[160];
            char nameHex[96];
            char sceneHex[160];

            if (vm_net_mock_role_find_free_escrow_index(&escrowIndex))
                (void)vm_net_mock_role_db_reassign_account(
                    sealedRoleId, accountId, VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT,
                    escrowIndex, &undo);
            if (vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                                    sizeof(accountHex)) != 0 &&
                vm_mysql_hex_encode(sealed.previewName,
                                    strlen(sealed.previewName), nameHex,
                                    sizeof(nameHex)) != 0)
            {
                if (sealed.previewScene[0] == 0 ||
                    vm_mysql_hex_encode(sealed.previewScene,
                                        strlen(sealed.previewScene), sceneHex,
                                        sizeof(sceneHex)) == 0)
                    sceneHex[0] = 0;
                snprintf(restoreQ, sizeof(restoreQ),
                         "INSERT IGNORE INTO server_sealed_roles("
                         "item_instance_id,sealed_role_id,origin_account_id,"
                         "preview_name,preview_level,preview_job,preview_sex,"
                         "preview_scene,preview_pos_x,preview_pos_y,"
                         "sealed_at) VALUES(%u,%u,CAST(X'%s' AS CHAR),UNHEX('%s'),"
                         "%u,%u,%u,UNHEX('%s'),%u,%u,%u)",
                         instanceId, sealedRoleId, accountHex, nameHex,
                         sealed.previewLevel, (u32)sealed.previewJob,
                         (u32)sealed.previewSex, sceneHex[0] ? sceneHex : "",
                         (u32)sealed.previewX, (u32)sealed.previewY,
                         (u32)time(NULL));
                (void)vm_mysql_exec(restoreQ);
            }
            result = 6;
            reason = "consume-failed";
        }
        else
        {
            u32 keepActiveRoleId = role->roleId;

            (void)vm_net_mock_role_db_save("pre-unseal");
            g_vm_net_mock_role_inventory_dirty = false;
            g_vm_net_mock_role_position_dirty = false;
            g_vm_net_mock_role_db_loaded = false;
            g_vm_net_mock_role_db_valid = false;
            account = g_vm_mock_service_accounts;
            while (account != NULL)
            {
                if (strcmp(account->accountId, accountId) == 0)
                {
                    account->roleDbLoaded = false;
                    account->roleDbValid = false;
                    account->roleInventoryDirty = false;
                    account->rolePositionDirty = false;
                    break;
                }
                account = account->next;
            }
            vm_net_mock_role_db_load();
            if (account != NULL && g_vm_net_mock_role_db_valid)
            {
                u32 hi = 0;
                /* Heal orphan-recreate placeholders (ASCII scene without .sce). */
                for (hi = 0; hi < g_vm_net_mock_role_db.roleCount; ++hi)
                {
                    vm_net_mock_role_state *r =
                        &g_vm_net_mock_role_db.roles[hi];
                    if (r->roleId != sealedRoleId)
                        continue;
                    if (r->scene[0] == 0 || strchr(r->scene, '.') == NULL)
                    {
                        snprintf(r->scene, sizeof(r->scene), "%s",
                                 vm_net_mock_role_initial_scene_name());
                        r->x = VM_NET_MOCK_ROLE_INITIAL_X;
                        r->y = VM_NET_MOCK_ROLE_INITIAL_Y;
                        printf("[warn][network] mock_role_unseal_heal_scene "
                               "role=%u evidence=invalid-orphan-scene\n",
                               sealedRoleId);
                        (void)vm_net_mock_role_db_save("unseal-heal-scene");
                    }
                    break;
                }
                account->roleDb = g_vm_net_mock_role_db;
                account->roleDbLoaded = true;
                account->roleDbValid = true;
                g_vm_mock_service_globals_account = account;
            }
            (void)vm_net_mock_select_active_role(keepActiveRoleId);
            reason = "unseal-ok";
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 26,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "cardseq", cardSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining",
                                    cardRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "roleid",
                                    result == 1 ? sealedRoleId : 0))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_role_unseal card_seq=%u role=%u instance=%u "
           "remain=%u result=%u reason=%s resp=29/26\n",
           cardSeq, sealedRoleId, instanceId, cardRemaining, result, reason);
    vm_autotest_note(
        "mock_role_unseal card=%u role=%u result=%u reason=%s "
        "evidence=pixijs-only-29/26\n",
        cardSeq, sealedRoleId, result, reason);
    return pos;
}
