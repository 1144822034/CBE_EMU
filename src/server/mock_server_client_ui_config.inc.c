/* Pixi client UI visibility knobs (MySQL server_client_ui_config).
 * Defaults all ON so existing clients keep wash button + 翅/骑 status badges. */

typedef struct vm_net_mock_client_ui_config
{
    u8 washButtonEnabled;
    u8 wingStatusEnabled;
    u8 mountStatusEnabled;
} vm_net_mock_client_ui_config;

static vm_net_mock_client_ui_config g_vm_net_mock_client_ui_config;
static bool g_vm_net_mock_client_ui_config_loaded = false;

static void vm_net_mock_client_ui_config_set_defaults(
    vm_net_mock_client_ui_config *cfg)
{
    if (cfg == NULL)
        return;
    cfg->washButtonEnabled = 1;
    cfg->wingStatusEnabled = 1;
    cfg->mountStatusEnabled = 1;
}

static void vm_net_mock_client_ui_config_sanitize(
    vm_net_mock_client_ui_config *cfg)
{
    if (cfg == NULL)
        return;
    cfg->washButtonEnabled = cfg->washButtonEnabled ? 1u : 0u;
    cfg->wingStatusEnabled = cfg->wingStatusEnabled ? 1u : 0u;
    cfg->mountStatusEnabled = cfg->mountStatusEnabled ? 1u : 0u;
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
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
        return false;
    if (!vm_mysql_exec(
            "INSERT IGNORE INTO server_client_ui_config "
            "(id,wash_button_enabled,wing_status_enabled,mount_status_enabled) "
            "VALUES (1,1,1,1)"))
        return false;
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
            "mount_status_enabled "
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
           "wing_status=%u mount_status=%u\n",
           (u32)g_vm_net_mock_client_ui_config.washButtonEnabled,
           (u32)g_vm_net_mock_client_ui_config.wingStatusEnabled,
           (u32)g_vm_net_mock_client_ui_config.mountStatusEnabled);
}

static bool vm_net_mock_client_ui_config_admin_save(u8 washButtonEnabled,
                                                    u8 wingStatusEnabled,
                                                    u8 mountStatusEnabled)
{
    vm_net_mock_client_ui_config cfg;
    char query[512];

    vm_net_mock_client_ui_config_set_defaults(&cfg);
    cfg.washButtonEnabled = washButtonEnabled ? 1u : 0u;
    cfg.wingStatusEnabled = wingStatusEnabled ? 1u : 0u;
    cfg.mountStatusEnabled = mountStatusEnabled ? 1u : 0u;
    vm_net_mock_client_ui_config_sanitize(&cfg);
    if (!vm_net_mock_client_ui_config_ensure_table())
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_client_ui_config "
             "(id,wash_button_enabled,wing_status_enabled,mount_status_enabled) "
             "VALUES (1,%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE "
             "wash_button_enabled=VALUES(wash_button_enabled),"
             "wing_status_enabled=VALUES(wing_status_enabled),"
             "mount_status_enabled=VALUES(mount_status_enabled)",
             (u32)cfg.washButtonEnabled, (u32)cfg.wingStatusEnabled,
             (u32)cfg.mountStatusEnabled);
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
