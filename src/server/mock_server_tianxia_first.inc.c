/*
 * Pixi：70 大乱斗第一名雕像（宣阳广场）+ 膜拜抽黄金宝箱奖池。
 * 外观/坐标/文案/翅膀坐骑称号开关见 server_tianxia_statue_config（管理后台可调）。
 */

typedef struct
{
    bool loaded;
    bool valid;
    u32 roleId;
    u8 job; /* 1..3 */
    u8 sex; /* 0 male / 1 female */
    u32 eqChenghao;
    u32 eqWing;
    u32 eqMount;
    u8 designationId;
    /* 穿戴 enhance>=16 件数（脚底光环），0..8. */
    u8 plus16Count;
    char roleName[32];
    char accountId[64];
    char actorResource[64];
} vm_mock_tianxia_champion;

typedef struct
{
    bool loaded;
    u8 enabled;
    u16 posX;
    u16 posY;
    u16 scaleX100; /* 100=1.0×, 200=2.0× */
    u8 showWing;
    u8 showChenghao;
    u8 showMount;
    char titleText[32];   /* GBK 头顶字 */
    char optionOther[32]; /* GBK 膜拜大侠 */
    char optionChamp[32]; /* GBK 接受膜拜 */
    /* GBK；空则对话主文用默认「这个冠军很高冷,什么也没说!」 */
    char signature[VM_NET_MOCK_TIANXIA_SIGNATURE_GBK_BYTES + 1];
} vm_mock_tianxia_config;

/* 这个冠军很高冷,什么也没说! */
static const char g_vm_mock_tianxia_signature_default[] =
    "\xd5\xe2\xb8\xf6\xb9\xda\xbe\xfc\xba\xdc\xb8\xdf\xc0\xe4\x2c"
    "\xca\xb2\xc3\xb4\xd2\xb2\xc3\xbb\xcb\xb5\x21";

static vm_mock_tianxia_champion g_vm_mock_tianxia_champ;
static vm_mock_tianxia_config g_vm_mock_tianxia_cfg;
static bool g_vm_mock_tianxia_tables_ready = false;

static void vm_mock_tianxia_config_set_defaults(vm_mock_tianxia_config *cfg)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1;
    cfg->posX = 168;
    cfg->posY = 200;
    cfg->scaleX100 = 200;
    cfg->showWing = 0;
    cfg->showChenghao = 0;
    cfg->showMount = 0;
    snprintf(cfg->titleText, sizeof(cfg->titleText), "%s",
             "\xcc\xec\xcf\xc2\xb5\xda\xd2\xbb"); /* 天下第一 */
    snprintf(cfg->optionOther, sizeof(cfg->optionOther), "%s",
             "\xc4\xa4\xb0\xdd\xb4\xf3\xcf\xc0"); /* 膜拜大侠 */
    snprintf(cfg->optionChamp, sizeof(cfg->optionChamp), "%s",
             "\xbd\xd3\xca\xdc\xc4\xa4\xb0\xdd"); /* 接受膜拜 */
    cfg->signature[0] = 0;
}

static bool vm_mock_tianxia_scene_is_xuanyang(const char *scene)
{
    return scene != NULL &&
           vm_net_mock_scene_names_equal_loose(
               scene, "\x63\x30\x34\xc1\xd9\xb0\xb2\xb8\xae\x5f\x31\x30");
}

static const char *vm_mock_tianxia_actor_resource_for_job_sex(u8 job, u8 sex)
{
    static const char *const names[6] = {
        "h_warrior.actor",   "hW_warrior.actor",  "h_assassin.actor",
        "hW_assassin.actor", "h_mage.actor",      "hW_mage.actor",
    };
    u8 jobIndex = (job >= 1 && job <= 3) ? (u8)(job - 1) : 0;
    u8 s = sex ? 1u : 0u;
    return names[jobIndex * 2u + s];
}

static bool vm_mock_tianxia_ensure_tables(void)
{
    if (g_vm_mock_tianxia_tables_ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_tianxia_worship_daily ("
            "role_id INT UNSIGNED NOT NULL,"
            "day_ymd INT UNSIGNED NOT NULL,"
            "used_count TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(role_id,day_ymd)) ENGINE=InnoDB"))
    {
        return false;
    }
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_tianxia_statue_config ("
            "id TINYINT UNSIGNED NOT NULL,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "pos_x SMALLINT UNSIGNED NOT NULL DEFAULT 168,"
            "pos_y SMALLINT UNSIGNED NOT NULL DEFAULT 200,"
            "scale_x100 SMALLINT UNSIGNED NOT NULL DEFAULT 200,"
            "show_wing TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "show_chenghao TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "show_mount TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "title_text VARBINARY(32) NOT NULL DEFAULT '',"
            "option_other VARBINARY(32) NOT NULL DEFAULT '',"
            "option_champ VARBINARY(32) NOT NULL DEFAULT '',"
            "signature VARBINARY(64) NOT NULL DEFAULT '',"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(id)) ENGINE=InnoDB"))
    {
        return false;
    }
    (void)vm_mysql_exec(
        "ALTER TABLE server_tianxia_statue_config ADD COLUMN "
        "signature VARBINARY(64) NOT NULL DEFAULT '' AFTER option_champ");
    /* Seed defaults once (GBK hex for 天下第一 / 膜拜大侠 / 接受膜拜). */
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_tianxia_statue_config "
        "(id,enabled,pos_x,pos_y,scale_x100,show_wing,show_chenghao,show_mount,"
        "title_text,option_other,option_champ,signature) VALUES "
        "(1,1,168,200,200,0,0,0,"
        "0xCCECCFC2B5DAD2BB,0xC4A4B0DDB4F3CFC0,0xBDD3CADCC4A4B0DD,'')");
    g_vm_mock_tianxia_tables_ready = true;
    return true;
}

typedef struct
{
    vm_mock_tianxia_config *cfg;
    bool found;
} vm_mock_tianxia_cfg_load_ctx;

static bool vm_mock_tianxia_config_row_cb(void *ctx, unsigned int cc,
                                         const char *const *values,
                                         const size_t *lengths)
{
    vm_mock_tianxia_cfg_load_ctx *row = (vm_mock_tianxia_cfg_load_ctx *)ctx;
    size_t n = 0;
    u32 tmp = 0;

    (void)cc;
    if (row == NULL || row->cfg == NULL || values == NULL || lengths == NULL)
        return false;
    if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        row->cfg->enabled = tmp ? 1u : 0u;
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        row->cfg->posX = (u16)tmp;
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        row->cfg->posY = (u16)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        row->cfg->scaleX100 = (u16)(tmp == 0 ? 100u : tmp);
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        row->cfg->showWing = tmp ? 1u : 0u;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        row->cfg->showChenghao = tmp ? 1u : 0u;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        row->cfg->showMount = tmp ? 1u : 0u;
    if (values[7] && lengths[7] > 0)
    {
        if (vm_mysql_hex_decode(values[7], lengths[7], row->cfg->titleText,
                                sizeof(row->cfg->titleText) - 1, &n))
            row->cfg->titleText[n] = 0;
    }
    if (values[8] && lengths[8] > 0)
    {
        if (vm_mysql_hex_decode(values[8], lengths[8], row->cfg->optionOther,
                                sizeof(row->cfg->optionOther) - 1, &n))
            row->cfg->optionOther[n] = 0;
    }
    if (values[9] && lengths[9] > 0)
    {
        if (vm_mysql_hex_decode(values[9], lengths[9], row->cfg->optionChamp,
                                sizeof(row->cfg->optionChamp) - 1, &n))
            row->cfg->optionChamp[n] = 0;
    }
    if (values[10] && lengths[10] > 0)
    {
        if (vm_mysql_hex_decode(values[10], lengths[10], row->cfg->signature,
                                sizeof(row->cfg->signature) - 1, &n))
            row->cfg->signature[n] = 0;
    }
    row->found = true;
    return false;
}

static void vm_mock_tianxia_config_load(bool force)
{
    vm_mock_tianxia_cfg_load_ctx ctx;
    vm_mock_tianxia_config defaults;

    if (!force && g_vm_mock_tianxia_cfg.loaded)
        return;
    vm_mock_tianxia_config_set_defaults(&defaults);
    g_vm_mock_tianxia_cfg = defaults;
    g_vm_mock_tianxia_cfg.loaded = true;
    if (!vm_mock_tianxia_ensure_tables())
        return;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = &g_vm_mock_tianxia_cfg;
    (void)vm_mysql_query(
        "SELECT enabled,pos_x,pos_y,scale_x100,show_wing,show_chenghao,"
        "show_mount,HEX(title_text),HEX(option_other),HEX(option_champ),"
        "HEX(signature) "
        "FROM server_tianxia_statue_config WHERE id=1 LIMIT 1",
        vm_mock_tianxia_config_row_cb, &ctx);
    if (!ctx.found)
        g_vm_mock_tianxia_cfg = defaults;
    if (g_vm_mock_tianxia_cfg.titleText[0] == 0)
        snprintf(g_vm_mock_tianxia_cfg.titleText,
                 sizeof(g_vm_mock_tianxia_cfg.titleText), "%s",
                 defaults.titleText);
    if (g_vm_mock_tianxia_cfg.optionOther[0] == 0)
        snprintf(g_vm_mock_tianxia_cfg.optionOther,
                 sizeof(g_vm_mock_tianxia_cfg.optionOther), "%s",
                 defaults.optionOther);
    if (g_vm_mock_tianxia_cfg.optionChamp[0] == 0)
        snprintf(g_vm_mock_tianxia_cfg.optionChamp,
                 sizeof(g_vm_mock_tianxia_cfg.optionChamp), "%s",
                 defaults.optionChamp);
    if (g_vm_mock_tianxia_cfg.scaleX100 < 50u)
        g_vm_mock_tianxia_cfg.scaleX100 = 50u;
    if (g_vm_mock_tianxia_cfg.scaleX100 > 400u)
        g_vm_mock_tianxia_cfg.scaleX100 = 400u;
    g_vm_mock_tianxia_cfg.loaded = true;
}

static const vm_mock_tianxia_config *vm_mock_tianxia_config_get(void)
{
    vm_mock_tianxia_config_load(false);
    return &g_vm_mock_tianxia_cfg;
}

static const char *vm_mock_tianxia_signature_display(void)
{
    const vm_mock_tianxia_config *cfg = vm_mock_tianxia_config_get();
    if (cfg != NULL && cfg->signature[0] != 0)
        return cfg->signature;
    return g_vm_mock_tianxia_signature_default;
}

static void vm_mock_tianxia_config_invalidate(void)
{
    g_vm_mock_tianxia_cfg.loaded = false;
}

static bool vm_mock_tianxia_admin_save_config(
    u8 enabled, u16 posX, u16 posY, u16 scaleX100, u8 showWing, u8 showChenghao,
    u8 showMount, const char *titleGbk, const char *optionOtherGbk,
    const char *optionChampGbk, const char *signatureGbk)
{
    char titleHex[96];
    char otherHex[96];
    char champHex[96];
    char sigHex[160];
    char query[960];
    const char *title = titleGbk && titleGbk[0] ? titleGbk
                                                : "\xcc\xec\xcf\xc2\xb5\xda\xd2\xbb";
    const char *other =
        optionOtherGbk && optionOtherGbk[0]
            ? optionOtherGbk
            : "\xc4\xa4\xb0\xdd\xb4\xf3\xcf\xc0";
    const char *champ =
        optionChampGbk && optionChampGbk[0]
            ? optionChampGbk
            : "\xbd\xd3\xca\xdc\xc4\xa4\xb0\xdd";
    const char *sig = signatureGbk ? signatureGbk : "";
    size_t sigLen = 0;

    if (!vm_mock_tianxia_ensure_tables())
        return false;
    if (scaleX100 < 50u)
        scaleX100 = 50u;
    if (scaleX100 > 400u)
        scaleX100 = 400u;
    sigLen = strlen(sig);
    if (sigLen > VM_NET_MOCK_TIANXIA_SIGNATURE_GBK_BYTES)
        sigLen = VM_NET_MOCK_TIANXIA_SIGNATURE_GBK_BYTES;
    if (vm_mysql_hex_encode(title, strlen(title), titleHex, sizeof(titleHex)) ==
            0 ||
        vm_mysql_hex_encode(other, strlen(other), otherHex, sizeof(otherHex)) ==
            0 ||
        vm_mysql_hex_encode(champ, strlen(champ), champHex, sizeof(champHex)) ==
            0)
    {
        return false;
    }
    if (sigLen == 0)
        snprintf(sigHex, sizeof(sigHex), "%s", "");
    else if (vm_mysql_hex_encode(sig, sigLen, sigHex, sizeof(sigHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_tianxia_statue_config "
             "(id,enabled,pos_x,pos_y,scale_x100,show_wing,show_chenghao,"
             "show_mount,title_text,option_other,option_champ,signature) VALUES "
             "(1,%u,%u,%u,%u,%u,%u,%u,UNHEX('%s'),UNHEX('%s'),UNHEX('%s'),"
             "%s%s%s) "
             "ON DUPLICATE KEY UPDATE enabled=VALUES(enabled),"
             "pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),"
             "scale_x100=VALUES(scale_x100),show_wing=VALUES(show_wing),"
             "show_chenghao=VALUES(show_chenghao),show_mount=VALUES(show_mount),"
             "title_text=VALUES(title_text),option_other=VALUES(option_other),"
             "option_champ=VALUES(option_champ),signature=VALUES(signature)",
             (u32)enabled, (u32)posX, (u32)posY, (u32)scaleX100, (u32)showWing,
             (u32)showChenghao, (u32)showMount, titleHex, otherHex, champHex,
             sigLen ? "UNHEX('" : "''", sigLen ? sigHex : "",
             sigLen ? "')" : "");
    if (!vm_mysql_exec(query))
        return false;
    vm_mock_tianxia_config_invalidate();
    return true;
}

static bool vm_mock_tianxia_save_signature(const char *signatureGbk)
{
    char sigHex[160];
    char query[320];
    size_t sigLen = 0;
    const char *sig = signatureGbk ? signatureGbk : "";

    if (!vm_mock_tianxia_ensure_tables())
        return false;
    sigLen = strlen(sig);
    if (sigLen > VM_NET_MOCK_TIANXIA_SIGNATURE_GBK_BYTES)
        sigLen = VM_NET_MOCK_TIANXIA_SIGNATURE_GBK_BYTES;
    if (sigLen == 0)
    {
        snprintf(query, sizeof(query),
                 "UPDATE server_tianxia_statue_config SET signature='' "
                 "WHERE id=1");
    }
    else
    {
        if (vm_mysql_hex_encode(sig, sigLen, sigHex, sizeof(sigHex)) == 0)
            return false;
        snprintf(query, sizeof(query),
                 "UPDATE server_tianxia_statue_config SET signature=UNHEX('%s') "
                 "WHERE id=1",
                 sigHex);
    }
    if (!vm_mysql_exec(query))
        return false;
    vm_mock_tianxia_config_invalidate();
    return true;
}

typedef struct
{
    vm_mock_tianxia_champion *champ;
    bool gotTitle;
    bool gotRole;
} vm_mock_tianxia_load_ctx;

static bool vm_mock_tianxia_title_row_cb(void *ctx, unsigned int cc,
                                        const char *const *values,
                                        const size_t *lengths)
{
    vm_mock_tianxia_load_ctx *row = (vm_mock_tianxia_load_ctx *)ctx;
    size_t nameLen = 0;

    (void)cc;
    if (row == NULL || row->champ == NULL || values == NULL || lengths == NULL)
        return false;
    if (values[0] == NULL || values[1] == NULL || values[2] == NULL)
        return false;
    row->champ->roleId = (u32)strtoul(values[0], NULL, 10);
    if (row->champ->roleId == 0)
        return false;
    snprintf(row->champ->accountId, sizeof(row->champ->accountId), "%.*s",
             (int)lengths[1], values[1]);
    if (!vm_mysql_hex_decode(values[2], lengths[2], row->champ->roleName,
                             sizeof(row->champ->roleName) - 1, &nameLen))
    {
        row->champ->roleName[0] = 0;
        return false;
    }
    row->champ->roleName[nameLen] = 0;
    row->gotTitle = true;
    return false;
}

static bool vm_mock_tianxia_role_row_cb(void *ctx, unsigned int cc,
                                       const char *const *values,
                                       const size_t *lengths)
{
    vm_mock_tianxia_load_ctx *row = (vm_mock_tianxia_load_ctx *)ctx;
    size_t nameLen = 0;
    u32 job = 0;
    u32 sex = 0;
    u32 tmp = 0;

    (void)cc;
    if (row == NULL || row->champ == NULL || values == NULL || lengths == NULL)
        return false;
    if (values[0] == NULL || values[1] == NULL)
        return false;
    job = (u32)strtoul(values[0], NULL, 10);
    sex = (u32)strtoul(values[1], NULL, 10);
    row->champ->job = (job >= 1 && job <= 3) ? (u8)job : 1;
    row->champ->sex = sex ? 1u : 0u;
    if (values[2] != NULL && lengths[2] > 0)
    {
        if (vm_mysql_hex_decode(values[2], lengths[2], row->champ->roleName,
                                sizeof(row->champ->roleName) - 1, &nameLen))
            row->champ->roleName[nameLen] = 0;
    }
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        row->champ->eqChenghao = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        row->champ->eqWing = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        row->champ->eqMount = tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        row->champ->designationId = (u8)tmp;
    row->gotRole = true;
    return false;
}

static bool vm_mock_tianxia_plus16_row_cb(void *ctx, unsigned int cc,
                                         const char *const *values,
                                         const size_t *lengths)
{
    vm_mock_tianxia_champion *champ = (vm_mock_tianxia_champion *)ctx;
    u32 tmp = 0;

    (void)cc;
    if (champ == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    if (vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
    {
        if (tmp > 8u)
            tmp = 8u;
        champ->plus16Count = (u8)tmp;
    }
    return false;
}

static void vm_mock_tianxia_champ_load(bool force);

static void vm_mock_tianxia_champ_invalidate(void)
{
    g_vm_mock_tianxia_champ.loaded = false;
    g_vm_mock_tianxia_champ.valid = false;
}

/**
 * Prefer live online cosmetics / +16 when the champion is currently logged in.
 * Mutates the cached champ row in place (safe: callers treat it as snapshot).
 */
static void vm_mock_tianxia_overlay_live_cosmetics(vm_mock_tianxia_champion *champ)
{
    vm_mock_service_client_session *s;
    u8 plus16 = 0;
    u8 slot;

    if (champ == NULL || champ->roleId == 0)
        return;
    for (s = g_vm_mock_service_client_sessions; s != NULL; s = s->next)
    {
        if (!s->roleOnline || s->onlineRoleId != champ->roleId)
            continue;
        champ->eqChenghao = s->onlineEqChenghaoId;
        champ->eqWing = s->onlineEqWingId;
        champ->eqMount = s->onlineEqMountId;
        for (slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
        {
            if (s->onlineEquippedEnhanceLevels[slot] >=
                (u16)VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
                ++plus16;
        }
        if (plus16 > 8u)
            plus16 = 8u;
        champ->plus16Count = plus16;
        return;
    }
}

/** Push statue wing/chenghao/mount to observers currently in 宣阳广场. */
static void vm_mock_tianxia_notify_plaza_statue_cosmetics(u32 eqChenghao,
                                                         u32 eqWing,
                                                         u32 eqMount)
{
    vm_mock_service_client_session *s;
    const vm_mock_tianxia_config *cfg = vm_mock_tianxia_config_get();
    u32 wing = 0;
    u32 chenghao = 0;
    u32 mount = 0;

    if (cfg == NULL || !cfg->enabled)
        return;
    if (cfg->showWing)
        wing = eqWing;
    if (cfg->showChenghao)
        chenghao = eqChenghao;
    if (cfg->showMount)
        mount = eqMount;
    for (s = g_vm_mock_service_client_sessions; s != NULL; s = s->next)
    {
        if (!s->roleOnline || !s->onlinePresenceValid)
            continue;
        if (!vm_mock_tianxia_scene_is_xuanyang(s->onlineScene))
            continue;
        (void)vm_mock_service_session_enqueue_cosmetic_notice(
            s, VM_NET_MOCK_TIANXIA_FIRST_ACTOR_ID, chenghao, wing, mount, "");
    }
}

/**
 * Champion switched wing / 动态称号 / mount (or cleared chenghao via classic
 * designation). Drop sticky cache and live-update plaza statues.
 */
static void vm_mock_tianxia_on_champ_cosmetics_changed(u32 roleId)
{
    u32 eqChenghao = 0;
    u32 eqWing = 0;
    u32 eqMount = 0;
    vm_mock_service_client_session *s;
    vm_net_mock_role_state *role;
    bool matched = false;

    if (roleId == 0)
        return;
    if (!g_vm_mock_tianxia_champ.loaded)
        vm_mock_tianxia_champ_load(false);
    if (!g_vm_mock_tianxia_champ.valid ||
        g_vm_mock_tianxia_champ.roleId != roleId)
        return;

    /* Prefer active role memory — session onlineEq* may not be synced yet. */
    role = vm_net_mock_active_role();
    if (role != NULL && role->roleId == roleId)
    {
        eqChenghao = role->equippedChenghaoId;
        eqWing = role->equippedWingId;
        eqMount = role->equippedMountId;
        matched = true;
    }
    if (!matched)
    {
        for (s = g_vm_mock_service_client_sessions; s != NULL; s = s->next)
        {
            if (s->roleOnline && s->onlineRoleId == roleId)
            {
                eqChenghao = s->onlineEqChenghaoId;
                eqWing = s->onlineEqWingId;
                eqMount = s->onlineEqMountId;
                matched = true;
                break;
            }
        }
    }
    if (!matched)
    {
        eqChenghao = g_vm_mock_tianxia_champ.eqChenghao;
        eqWing = g_vm_mock_tianxia_champ.eqWing;
        eqMount = g_vm_mock_tianxia_champ.eqMount;
    }

    vm_mock_tianxia_champ_invalidate();
    vm_mock_tianxia_notify_plaza_statue_cosmetics(eqChenghao, eqWing, eqMount);
    printf("[info][mock-service] tianxia_champ_cosmetics_changed role=%u "
           "eqC=%u eqW=%u eqM=%u\n",
           roleId, eqChenghao, eqWing, eqMount);
}

static void vm_mock_tianxia_champ_load(bool force)
{
    vm_mock_tianxia_load_ctx ctx;
    char query[480];

    if (!force && g_vm_mock_tianxia_champ.loaded)
        return;
    memset(&g_vm_mock_tianxia_champ, 0, sizeof(g_vm_mock_tianxia_champ));
    g_vm_mock_tianxia_champ.loaded = true;
    memset(&ctx, 0, sizeof(ctx));
    ctx.champ = &g_vm_mock_tianxia_champ;
    snprintf(query, sizeof(query),
             "SELECT role_id,account_id,HEX(role_name) FROM server_arena_titles "
             "WHERE job=0 AND bracket=77 AND place=1 LIMIT 1");
    (void)vm_mysql_query(query, vm_mock_tianxia_title_row_cb, &ctx);
    if (!ctx.gotTitle || g_vm_mock_tianxia_champ.roleId == 0)
        return;
    snprintf(query, sizeof(query),
             "SELECT job,sex,HEX(role_name),equipped_chenghao_id,"
             "equipped_wing_id,equipped_mount_id,designation_id "
             "FROM account_roles "
             "WHERE role_id=%u LIMIT 1",
             g_vm_mock_tianxia_champ.roleId);
    (void)vm_mysql_query(query, vm_mock_tianxia_role_row_cb, &ctx);
    if (!ctx.gotRole)
    {
        g_vm_mock_tianxia_champ.job = 1;
        g_vm_mock_tianxia_champ.sex = 0;
    }
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM account_role_equipment "
             "WHERE role_id=%u AND enhance_level>=16",
             g_vm_mock_tianxia_champ.roleId);
    (void)vm_mysql_query(query, vm_mock_tianxia_plus16_row_cb,
                         &g_vm_mock_tianxia_champ);
    snprintf(g_vm_mock_tianxia_champ.actorResource,
             sizeof(g_vm_mock_tianxia_champ.actorResource), "%s",
             vm_mock_tianxia_actor_resource_for_job_sex(
                 g_vm_mock_tianxia_champ.job, g_vm_mock_tianxia_champ.sex));
    g_vm_mock_tianxia_champ.valid = g_vm_mock_tianxia_champ.roleName[0] != 0;
    printf("[info][mock-service] tianxia_champ load role=%u job=%u sex=%u "
           "name=%s wing=%u chenghao=%u mount=%u plus16=%u valid=%u\n",
           g_vm_mock_tianxia_champ.roleId, g_vm_mock_tianxia_champ.job,
           g_vm_mock_tianxia_champ.sex, g_vm_mock_tianxia_champ.roleName,
           g_vm_mock_tianxia_champ.eqWing, g_vm_mock_tianxia_champ.eqChenghao,
           g_vm_mock_tianxia_champ.eqMount, g_vm_mock_tianxia_champ.plus16Count,
           g_vm_mock_tianxia_champ.valid ? 1u : 0u);
}

static const vm_mock_tianxia_champion *vm_mock_tianxia_champ_get(void)
{
    vm_mock_tianxia_champ_load(false);
    if (!g_vm_mock_tianxia_champ.valid)
        return NULL;
    vm_mock_tianxia_overlay_live_cosmetics(&g_vm_mock_tianxia_champ);
    return &g_vm_mock_tianxia_champ;
}

/* Pixi scriptName: pixi_tx1|scale|wing|chenghao|mount|titleHex|playerTitleHex|plus16 */
static void vm_mock_tianxia_pack_script_name(char *out, size_t outCap,
                                            const vm_mock_tianxia_config *cfg,
                                            const vm_mock_tianxia_champion *champ)
{
    char titleHex[80];
    char playerHex[80];
    const char *playerTitle = "";
    u32 wing = 0;
    u32 chenghao = 0;
    u32 mount = 0;
    u32 scale = 200;
    u32 plus16 = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    titleHex[0] = 0;
    playerHex[0] = 0;
    if (cfg == NULL)
        return;
    scale = cfg->scaleX100 ? cfg->scaleX100 : 200u;
    if (champ != NULL)
    {
        if (cfg->showWing)
            wing = champ->eqWing;
        if (cfg->showChenghao)
        {
            chenghao = champ->eqChenghao;
            /* 没装动态称号时，用角色当前称号（富可敌国 / 天下第一 等）. */
            if (chenghao == 0)
            {
                const vm_net_mock_designation_entry *entry =
                    vm_net_mock_designation_by_id(champ->designationId);
                if (entry != NULL)
                {
                    if (entry->overheadResource != NULL &&
                        entry->overheadResource[0] != 0)
                        playerTitle = entry->overheadResource;
                    else if (entry->name != NULL && entry->name[0] != 0)
                        playerTitle = entry->name;
                }
            }
        }
        if (cfg->showMount)
            mount = champ->eqMount;
        plus16 = champ->plus16Count;
        if (plus16 > 8u)
            plus16 = 8u;
    }
    if (cfg->titleText[0] != 0 &&
        vm_mysql_hex_encode(cfg->titleText, strlen(cfg->titleText), titleHex,
                            sizeof(titleHex)) == 0)
        titleHex[0] = 0;
    if (playerTitle[0] != 0 &&
        vm_mysql_hex_encode(playerTitle, strlen(playerTitle), playerHex,
                            sizeof(playerHex)) == 0)
        playerHex[0] = 0;
    snprintf(out, outCap, "pixi_tx1|%u|%u|%u|%u|%s|%s|%u", scale, wing, chenghao,
             mount, titleHex, playerHex, plus16);
}

static bool vm_mock_tianxia_try_fill_seed(vm_net_mock_scene_npcinfo_seed *seed)
{
    const vm_mock_tianxia_champion *champ = vm_mock_tianxia_champ_get();
    const vm_mock_tianxia_config *cfg = vm_mock_tianxia_config_get();

    if (seed == NULL || champ == NULL || cfg == NULL || !cfg->enabled)
        return false;
    memset(seed, 0, sizeof(*seed));
    seed->actorId = VM_NET_MOCK_TIANXIA_FIRST_ACTOR_ID;
    seed->x = cfg->posX;
    seed->y = cfg->posY;
    snprintf(seed->actorResource, sizeof(seed->actorResource), "%s",
             champ->actorResource);
    snprintf(seed->displayName, sizeof(seed->displayName), "%s",
             champ->roleName);
    vm_mock_tianxia_pack_script_name(seed->scriptName, sizeof(seed->scriptName),
                                     cfg, champ);
    return true;
}

static bool vm_mock_tianxia_append_builtin_if_plaza(
    const char *scene, vm_net_mock_scene_npcinfo_seed *seeds, u32 seedCap,
    u32 *countInOut)
{
    vm_net_mock_scene_npcinfo_seed seed;
    u32 count;

    if (countInOut == NULL || seeds == NULL || seedCap == 0)
        return false;
    count = *countInOut;
    if (!vm_mock_tianxia_scene_is_xuanyang(scene) || count >= seedCap)
        return false;
    if (!vm_mock_tianxia_try_fill_seed(&seed))
        return false;
    seeds[count++] = seed;
    *countInOut = count;
    return true;
}

typedef struct
{
    u16 usedCount;
    bool found;
} vm_mock_tianxia_worship_row;

static bool vm_mock_tianxia_worship_row_cb(void *ctx, unsigned int cc,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_mock_tianxia_worship_row *row = (vm_mock_tianxia_worship_row *)ctx;

    (void)cc;
    (void)lengths;
    if (row == NULL || values == NULL || values[0] == NULL)
        return false;
    row->usedCount = (u16)strtoul(values[0], NULL, 10);
    row->found = true;
    return false;
}

static u16 vm_mock_tianxia_worship_used_today(u32 roleId)
{
    vm_mock_tianxia_worship_row row;
    char query[256];
    u32 dayYmd;

    if (roleId == 0 || !vm_mock_tianxia_ensure_tables())
        return 0xffffu;
    dayYmd = vm_mock_activities_day_ymd();
    if (dayYmd == 0)
        return 0xffffu;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT used_count FROM account_role_tianxia_worship_daily "
             "WHERE role_id=%u AND day_ymd=%u LIMIT 1",
             roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_tianxia_worship_row_cb, &row);
    return row.found ? row.usedCount : 0;
}

static bool vm_mock_tianxia_worship_inc_today(u32 roleId)
{
    char query[320];
    u32 dayYmd;

    if (roleId == 0 || !vm_mock_tianxia_ensure_tables())
        return false;
    dayYmd = vm_mock_activities_day_ymd();
    if (dayYmd == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_tianxia_worship_daily "
             "(role_id,day_ymd,used_count) VALUES (%u,%u,1) "
             "ON DUPLICATE KEY UPDATE used_count=used_count+1",
             roleId, dayYmd);
    return vm_mysql_exec(query);
}

static void vm_net_mock_tianxia_worship_maybe_announce(u32 rewardItemId)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    static const char prefixGbk[] =
        "\xba\xcd\xbd\xad\xba\xfe\xb5\xda\xd2\xbb\xb8\xdf\xca\xd6"
        "\xbd\xbb\xc1\xf7\xd2\xe2\xcd\xe2\xca\xd5\xbb\xf1";
    const vm_net_mock_shop_catalog_item *shop = NULL;
    const char *itemName = NULL;
    char message[96];
    int wrote = 0;
    u32 recipients = 0;

    if (!vm_net_mock_gold_chest_reward_warrants_world_announce(rewardItemId))
        return;
    shop = vm_net_mock_find_shop_catalog_item(rewardItemId);
    if (shop == NULL || shop->name[0] == 0)
        return;
    itemName = shop->name;
    memset(message, 0, sizeof(message));
    wrote = snprintf(message, sizeof(message), "%s%s", prefixGbk, itemName);
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
    {
        printf("[warn][network] tianxia_worship_announce item=%u "
               "reason=message-overflow\n",
               rewardItemId);
        return;
    }
    recipients = vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][network] tianxia_worship_announce item=%u recipients=%u "
           "evidence=worship-gold-chest-pool\n",
           rewardItemId, recipients);
}

static u32 vm_mock_tianxia_gbk_char_count(const char *s)
{
    u32 n = 0;
    const u8 *p = (const u8 *)s;

    if (p == NULL)
        return 0;
    while (*p)
    {
        if (*p >= 0x80u)
        {
            if (p[1] == 0)
                break;
            p += 2;
        }
        else
            p += 1;
        n += 1;
    }
    return n;
}

/*
 * PixiJS-only 天下第一江湖签名: 1/29/24 { signature }.
 * Champion only. Empty clears to default display text.
 * result: 1=ok, 2=not champ, 3=empty (unused; empty clears), 4=too long, 5=save fail.
 */
static u32 vm_net_mock_build_tianxia_signature_response(const u8 *request,
                                                        u32 requestLen, u8 *out,
                                                        u32 outCap)
{
    u32 offset = 4;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    char signature[VM_NET_MOCK_TIANXIA_SIGNATURE_GBK_BYTES + 1];
    size_t sigLen = 0;
    const char *reason = "ok";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    const vm_mock_tianxia_champion *champ = NULL;

    signature[0] = 0;
    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                          &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 24 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                              "signature", signature,
                                              sizeof(signature)))
    {
        signature[0] = 0;
    }
    sigLen = strlen(signature);
    while (sigLen > 0 &&
           (signature[sigLen - 1] == ' ' || signature[sigLen - 1] == '\t'))
    {
        signature[--sigLen] = 0;
    }
    {
        size_t lead = 0;
        while (signature[lead] == ' ' || signature[lead] == '\t')
            ++lead;
        if (lead > 0)
        {
            memmove(signature, signature + lead, sigLen - lead + 1);
            sigLen -= lead;
        }
    }
    if (sigLen > VM_NET_MOCK_TIANXIA_SIGNATURE_GBK_BYTES ||
        vm_mock_tianxia_gbk_char_count(signature) >
            VM_NET_MOCK_TIANXIA_SIGNATURE_MAX_CHARS)
    {
        result = 4;
        reason = "too-long";
        signature[0] = 0;
    }

    role = vm_net_mock_active_role();
    champ = vm_mock_tianxia_champ_get();
    if (result == 1 && (role == NULL || champ == NULL ||
                        role->roleId != champ->roleId))
    {
        result = 2;
        reason = "not-champion";
    }
    else if (result == 1)
    {
        if (!vm_mock_tianxia_save_signature(signature))
        {
            result = 5;
            reason = "save-failed";
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 24,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "signature",
                                       result == 1 ? signature : ""))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_tianxia_signature role=%u result=%u "
           "reason=%s len=%u resp=29/24\n",
           role ? role->roleId : 0u, result, reason, (u32)sigLen);
    return pos;
}
