/* Pixi client UI visibility knobs (MySQL server_client_ui_config).
 * Defaults: wash/翅/骑 ON; multi_open_limit=1; hangup quota without insight OFF. */

typedef struct vm_net_mock_client_ui_config
{
    u8 washButtonEnabled;
    u8 wingStatusEnabled;
    u8 mountStatusEnabled;
    /** Max concurrent Pixi client windows on one device (1–8). */
    u8 multiOpenLimit;
    /**
     * 1 = without 战斗心得, hangup stops after 64 fights.
     * 0 = unlimited hangup battles even without insight.
     */
    u8 hangupQuotaWithoutInsight;
} vm_net_mock_client_ui_config;

static vm_net_mock_client_ui_config g_vm_net_mock_client_ui_config;
static bool g_vm_net_mock_client_ui_config_loaded = false;

enum
{
    VM_NET_MOCK_CLIENT_UI_MULTI_OPEN_MIN = 1,
    VM_NET_MOCK_CLIENT_UI_MULTI_OPEN_MAX = 8
};

static void vm_net_mock_client_ui_config_set_defaults(
    vm_net_mock_client_ui_config *cfg)
{
    if (cfg == NULL)
        return;
    cfg->washButtonEnabled = 1;
    cfg->wingStatusEnabled = 1;
    cfg->mountStatusEnabled = 1;
    cfg->multiOpenLimit = 1;
    /* 0 = hangup battles unlimited without 战斗心得 (product default). */
    cfg->hangupQuotaWithoutInsight = 0;
}

static void vm_net_mock_client_ui_config_sanitize(
    vm_net_mock_client_ui_config *cfg)
{
    if (cfg == NULL)
        return;
    cfg->washButtonEnabled = cfg->washButtonEnabled ? 1u : 0u;
    cfg->wingStatusEnabled = cfg->wingStatusEnabled ? 1u : 0u;
    cfg->mountStatusEnabled = cfg->mountStatusEnabled ? 1u : 0u;
    if (cfg->multiOpenLimit < VM_NET_MOCK_CLIENT_UI_MULTI_OPEN_MIN)
        cfg->multiOpenLimit = VM_NET_MOCK_CLIENT_UI_MULTI_OPEN_MIN;
    if (cfg->multiOpenLimit > VM_NET_MOCK_CLIENT_UI_MULTI_OPEN_MAX)
        cfg->multiOpenLimit = VM_NET_MOCK_CLIENT_UI_MULTI_OPEN_MAX;
    cfg->hangupQuotaWithoutInsight = cfg->hangupQuotaWithoutInsight ? 1u : 0u;
}

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} vm_net_mock_client_ui_column_ctx;

static bool vm_net_mock_client_ui_column_count_row(void *contextValue,
                                                  unsigned int columnCount,
                                                  char **values,
                                                  unsigned long *lengths)
{
    vm_net_mock_client_ui_column_ctx *ctx =
        (vm_net_mock_client_ui_column_ctx *)contextValue;

    (void)lengths;
    if (ctx == NULL || columnCount < 1 || values == NULL || values[0] == NULL)
    {
        if (ctx != NULL)
            ctx->invalid = true;
        return true;
    }
    ctx->count = (u32)strtoul(values[0], NULL, 10);
    ctx->found = true;
    return true;
}

static bool vm_net_mock_client_ui_config_ensure_column(const char *columnName,
                                                      const char *alterSql)
{
    vm_net_mock_client_ui_column_ctx ctx;
    char probe[360];

    if (columnName == NULL || alterSql == NULL)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(probe, sizeof(probe),
             "SELECT COUNT(*) FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA=DATABASE() AND "
             "TABLE_NAME='server_client_ui_config' AND COLUMN_NAME='%s'",
             columnName);
    if (!vm_mysql_query(probe, vm_net_mock_client_ui_column_count_row, &ctx) ||
        ctx.invalid || !ctx.found)
    {
        printf("[error][mock-admin] client_ui_config column probe %s error=%s\n",
               columnName, vm_mysql_last_error());
        return false;
    }
    if (ctx.count == 0)
    {
        if (!vm_mysql_exec(alterSql))
        {
            printf("[error][mock-admin] client_ui_config add column %s error=%s\n",
                   columnName, vm_mysql_last_error());
            return false;
        }
        printf("[info][mock-admin] client_ui_config added column %s\n",
               columnName);
    }
    return true;
}

static bool vm_net_mock_client_ui_config_row(void *contextValue,
                                             unsigned int columnCount,
                                             char **values, unsigned long *lengths)
{
    vm_net_mock_client_ui_config *cfg =
        (vm_net_mock_client_ui_config *)contextValue;
    u32 tmp = 0;

    (void)lengths;
    if (cfg == NULL || columnCount < 3 || values == NULL)
        return false;
    if (values[0] != NULL && values[0][0] != 0)
        tmp = (u32)strtoul(values[0], NULL, 10);
    cfg->washButtonEnabled = tmp ? 1u : 0u;
    tmp = 0;
    if (values[1] != NULL && values[1][0] != 0)
        tmp = (u32)strtoul(values[1], NULL, 10);
    cfg->wingStatusEnabled = tmp ? 1u : 0u;
    tmp = 0;
    if (values[2] != NULL && values[2][0] != 0)
        tmp = (u32)strtoul(values[2], NULL, 10);
    cfg->mountStatusEnabled = tmp ? 1u : 0u;
    tmp = 1;
    if (columnCount >= 4 && values[3] != NULL && values[3][0] != 0)
        tmp = (u32)strtoul(values[3], NULL, 10);
    cfg->multiOpenLimit = (u8)tmp;
    tmp = 1;
    if (columnCount >= 5 && values[4] != NULL && values[4][0] != 0)
        tmp = (u32)strtoul(values[4], NULL, 10);
    cfg->hangupQuotaWithoutInsight = tmp ? 1u : 0u;
    return true;
}

static bool vm_net_mock_client_ui_config_ensure_table(void)
{
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_client_ui_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "wash_button_enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "wing_status_enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "mount_status_enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "multi_open_limit TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "hangup_quota_without_insight TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
        return false;
    if (!vm_net_mock_client_ui_config_ensure_column(
            "multi_open_limit",
            "ALTER TABLE server_client_ui_config ADD COLUMN "
            "multi_open_limit TINYINT UNSIGNED NOT NULL DEFAULT 1 "
            "AFTER mount_status_enabled"))
        return false;
    if (!vm_net_mock_client_ui_config_ensure_column(
            "hangup_quota_without_insight",
            "ALTER TABLE server_client_ui_config ADD COLUMN "
            "hangup_quota_without_insight TINYINT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER multi_open_limit"))
        return false;
    if (!vm_mysql_exec(
            "INSERT IGNORE INTO server_client_ui_config "
            "(id,wash_button_enabled,wing_status_enabled,mount_status_enabled,"
            "multi_open_limit,hangup_quota_without_insight) "
            "VALUES (1,1,1,1,1,0)"))
        return false;
    /* Product: hangup battles always unlimited; clear legacy quota-on rows. */
    (void)vm_mysql_exec(
        "UPDATE server_client_ui_config SET hangup_quota_without_insight=0 "
        "WHERE id=1 AND hangup_quota_without_insight<>0");
    return true;
}

static void vm_net_mock_client_ui_config_load(bool force)
{
    if (g_vm_net_mock_client_ui_config_loaded && !force)
        return;
    vm_net_mock_client_ui_config_set_defaults(&g_vm_net_mock_client_ui_config);
    if (!vm_net_mock_client_ui_config_ensure_table())
    {
        printf("[error][mock-admin] client_ui_config ensure failed error=%s\n",
               vm_mysql_last_error());
        vm_net_mock_client_ui_config_sanitize(&g_vm_net_mock_client_ui_config);
        g_vm_net_mock_client_ui_config_loaded = true;
        return;
    }
    if (!vm_mysql_query(
            "SELECT wash_button_enabled,wing_status_enabled,"
            "mount_status_enabled,multi_open_limit,"
            "hangup_quota_without_insight "
            "FROM server_client_ui_config WHERE id=1 LIMIT 1",
            vm_net_mock_client_ui_config_row, &g_vm_net_mock_client_ui_config))
    {
        printf("[error][mock-admin] client_ui_config load failed error=%s\n",
               vm_mysql_last_error());
        vm_net_mock_client_ui_config_set_defaults(
            &g_vm_net_mock_client_ui_config);
    }
    vm_net_mock_client_ui_config_sanitize(&g_vm_net_mock_client_ui_config);
    g_vm_net_mock_client_ui_config_loaded = true;
    printf("[info][mock-admin] client_ui_config_load wash_button=%u "
           "wing_status=%u mount_status=%u multi_open_limit=%u "
           "hangup_quota_without_insight=%u\n",
           (u32)g_vm_net_mock_client_ui_config.washButtonEnabled,
           (u32)g_vm_net_mock_client_ui_config.wingStatusEnabled,
           (u32)g_vm_net_mock_client_ui_config.mountStatusEnabled,
           (u32)g_vm_net_mock_client_ui_config.multiOpenLimit,
           (u32)g_vm_net_mock_client_ui_config.hangupQuotaWithoutInsight);
}

static bool vm_net_mock_client_ui_config_admin_save(
    u8 washButtonEnabled, u8 wingStatusEnabled, u8 mountStatusEnabled,
    u8 multiOpenLimit, u8 hangupQuotaWithoutInsight)
{
    vm_net_mock_client_ui_config cfg;
    char query[768];

    vm_net_mock_client_ui_config_set_defaults(&cfg);
    cfg.washButtonEnabled = washButtonEnabled ? 1u : 0u;
    cfg.wingStatusEnabled = wingStatusEnabled ? 1u : 0u;
    cfg.mountStatusEnabled = mountStatusEnabled ? 1u : 0u;
    cfg.multiOpenLimit = multiOpenLimit;
    cfg.hangupQuotaWithoutInsight = hangupQuotaWithoutInsight ? 1u : 0u;
    vm_net_mock_client_ui_config_sanitize(&cfg);
    if (!vm_net_mock_client_ui_config_ensure_table())
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_client_ui_config "
             "(id,wash_button_enabled,wing_status_enabled,mount_status_enabled,"
             "multi_open_limit,hangup_quota_without_insight) "
             "VALUES (1,%u,%u,%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE "
             "wash_button_enabled=VALUES(wash_button_enabled),"
             "wing_status_enabled=VALUES(wing_status_enabled),"
             "mount_status_enabled=VALUES(mount_status_enabled),"
             "multi_open_limit=VALUES(multi_open_limit),"
             "hangup_quota_without_insight=VALUES(hangup_quota_without_insight)",
             (u32)cfg.washButtonEnabled, (u32)cfg.wingStatusEnabled,
             (u32)cfg.mountStatusEnabled, (u32)cfg.multiOpenLimit,
             (u32)cfg.hangupQuotaWithoutInsight);
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-admin] client_ui_config save failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_client_ui_config_loaded = false;
    vm_net_mock_client_ui_config_load(true);
    return true;
}

static const vm_net_mock_client_ui_config *vm_net_mock_client_ui_config_get(void)
{
    vm_net_mock_client_ui_config_load(false);
    return &g_vm_net_mock_client_ui_config;
}

static bool vm_net_mock_client_ui_wash_button_enabled(void)
{
    return vm_net_mock_client_ui_config_get()->washButtonEnabled != 0;
}

static bool vm_net_mock_client_ui_wing_status_enabled(void)
{
    return vm_net_mock_client_ui_config_get()->wingStatusEnabled != 0;
}

static bool vm_net_mock_client_ui_mount_status_enabled(void)
{
    return vm_net_mock_client_ui_config_get()->mountStatusEnabled != 0;
}

static u8 vm_net_mock_client_ui_multi_open_limit(void)
{
    return vm_net_mock_client_ui_config_get()->multiOpenLimit;
}

static bool vm_net_mock_client_ui_hangup_quota_without_insight(void)
{
    return vm_net_mock_client_ui_config_get()->hangupQuotaWithoutInsight != 0;
}
