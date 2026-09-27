/* Gem socket runtime knobs (MySQL server_gem_config).
 * Included from mock_server_role.c near combat_formula so panel/helpers share
 * one cache. Defaults: max_gem_level=3, gem_percent_per_level=1,
 * crystal16_gem_count=5, crystal16_exchange_enabled=0, socket_embed_enabled=1. */

enum
{
    VM_NET_MOCK_GEM_TYPE_NONE = 0,
    VM_NET_MOCK_GEM_TYPE_REFLECT = 1,
    VM_NET_MOCK_GEM_TYPE_COMBO = 2,
    VM_NET_MOCK_GEM_TYPE_REVIVE = 3,
    VM_NET_MOCK_GEM_LEVEL_RESERVE = 16,
    VM_NET_MOCK_GEM_REFLECT_BASE_ID = 922,
    VM_NET_MOCK_GEM_COMBO_BASE_ID = 942,
    VM_NET_MOCK_GEM_REVIVE_BASE_ID = 962,
    VM_NET_MOCK_GEM_FRAGMENT_ITEM_ID = 978,
    VM_NET_MOCK_GEM_COMBINE_COST = 3,
    VM_NET_MOCK_GEM_TYPE_EXCHANGE_COST = 2,
    VM_NET_MOCK_GEM_CRYSTAL16_ITEM_ID = 916,
    VM_NET_MOCK_GEM_CRYSTAL16_GRANT_DEFAULT = 5,
    VM_NET_MOCK_GEM_CRYSTAL16_GRANT_MAX = 20,
    VM_NET_MOCK_GEM_FRAGMENT_COMBINE_COST = 3
};

typedef struct
{
    u8 maxGemLevel;                 /* default 3; admin 1..16 */
    u8 gemPercentPerLevel;          /* default 1; admin 1..100 */
    u8 crystal16GemCount;           /* default 5; admin 1..20 gems per 916 */
    u8 crystal16ExchangeEnabled;    /* default 0; admin 0/1 gate for 29/21 */
    u8 socketEmbedEnabled;          /* default 1; admin 0/1 gate for 29/17 */
} vm_net_mock_gem_config;

static vm_net_mock_gem_config g_vm_net_mock_gem_config;
static bool g_vm_net_mock_gem_config_loaded = false;

static void vm_net_mock_gem_config_set_defaults(vm_net_mock_gem_config *cfg)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->maxGemLevel = 3;
    cfg->gemPercentPerLevel = 1;
    cfg->crystal16GemCount = (u8)VM_NET_MOCK_GEM_CRYSTAL16_GRANT_DEFAULT;
    cfg->crystal16ExchangeEnabled = 0;
    cfg->socketEmbedEnabled = 1;
}

static void vm_net_mock_gem_config_sanitize(vm_net_mock_gem_config *cfg)
{
    if (cfg == NULL)
        return;
    if (cfg->maxGemLevel == 0)
        cfg->maxGemLevel = 1;
    if (cfg->maxGemLevel > (u8)VM_NET_MOCK_GEM_LEVEL_RESERVE)
        cfg->maxGemLevel = (u8)VM_NET_MOCK_GEM_LEVEL_RESERVE;
    if (cfg->gemPercentPerLevel == 0)
        cfg->gemPercentPerLevel = 1;
    if (cfg->gemPercentPerLevel > 100)
        cfg->gemPercentPerLevel = 100;
    if (cfg->crystal16GemCount == 0)
        cfg->crystal16GemCount = 1;
    if (cfg->crystal16GemCount > (u8)VM_NET_MOCK_GEM_CRYSTAL16_GRANT_MAX)
        cfg->crystal16GemCount = (u8)VM_NET_MOCK_GEM_CRYSTAL16_GRANT_MAX;
    if (cfg->crystal16ExchangeEnabled > 1)
        cfg->crystal16ExchangeEnabled = 1;
    if (cfg->socketEmbedEnabled > 1)
        cfg->socketEmbedEnabled = 1;
}

static bool vm_net_mock_gem_config_row(void *contextValue, unsigned int columnCount,
                                      const char *const *values,
                                      const size_t *lengths)
{
    vm_net_mock_gem_config *cfg = (vm_net_mock_gem_config *)contextValue;
    u32 maxLevel = 3;
    u32 percent = 1;
    u32 crystalGrant = (u32)VM_NET_MOCK_GEM_CRYSTAL16_GRANT_DEFAULT;
    u32 crystalEnabled = 0;
    u32 socketEmbed = 1;

    (void)columnCount;
    if (cfg == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &maxLevel))
        return false;
    if (columnCount >= 2 && values[1] != NULL)
    {
        if (!vm_mock_mysql_parse_u32(values[1], lengths[1], &percent))
            return false;
    }
    if (columnCount >= 3 && values[2] != NULL)
    {
        if (!vm_mock_mysql_parse_u32(values[2], lengths[2], &crystalGrant))
            return false;
    }
    if (columnCount >= 4 && values[3] != NULL)
    {
        if (!vm_mock_mysql_parse_u32(values[3], lengths[3], &crystalEnabled))
            return false;
    }
    if (columnCount >= 5 && values[4] != NULL)
    {
        if (!vm_mock_mysql_parse_u32(values[4], lengths[4], &socketEmbed))
            return false;
    }
    cfg->maxGemLevel = (u8)maxLevel;
    cfg->gemPercentPerLevel = (u8)percent;
    cfg->crystal16GemCount = (u8)crystalGrant;
    cfg->crystal16ExchangeEnabled = crystalEnabled ? 1u : 0u;
    cfg->socketEmbedEnabled = socketEmbed ? 1u : 0u;
    return true;
}

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} vm_net_mock_gem_column_ctx;

static bool vm_net_mock_gem_column_count_row(void *contextValue,
                                            unsigned int columnCount,
                                            const char *const *values,
                                            const size_t *lengths)
{
    vm_net_mock_gem_column_ctx *ctx = (vm_net_mock_gem_column_ctx *)contextValue;

    if (ctx == NULL || columnCount < 1 || values == NULL || values[0] == NULL ||
        lengths == NULL ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &ctx->count))
    {
        if (ctx != NULL)
            ctx->invalid = true;
        return true;
    }
    ctx->found = true;
    return true;
}

static bool vm_net_mock_gem_config_ensure_column(const char *columnName,
                                                const char *alterSql)
{
    vm_net_mock_gem_column_ctx ctx;
    char probe[320];

    if (columnName == NULL || alterSql == NULL)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(probe, sizeof(probe),
             "SELECT COUNT(*) FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA=DATABASE() AND "
             "TABLE_NAME='server_gem_config' AND COLUMN_NAME='%s'",
             columnName);
    if (!vm_mysql_query(probe, vm_net_mock_gem_column_count_row, &ctx) ||
        ctx.invalid || !ctx.found)
    {
        printf("[error][mock-admin] gem_config column probe %s error=%s\n",
               columnName, vm_mysql_last_error());
        return false;
    }
    if (ctx.count == 0)
    {
        if (!vm_mysql_exec(alterSql))
        {
            printf("[error][mock-admin] gem_config add column %s error=%s\n",
                   columnName, vm_mysql_last_error());
            return false;
        }
        printf("[info][mock-admin] gem_config added column %s\n", columnName);
    }
    return true;
}

static bool vm_net_mock_gem_config_ensure_table(void)
{
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_gem_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "max_gem_level TINYINT UNSIGNED NOT NULL DEFAULT 3,"
            "gem_percent_per_level TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "crystal16_gem_count TINYINT UNSIGNED NOT NULL DEFAULT 5,"
            "crystal16_exchange_enabled TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "socket_embed_enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
        return false;
    if (!vm_net_mock_gem_config_ensure_column(
            "crystal16_gem_count",
            "ALTER TABLE server_gem_config ADD COLUMN "
            "crystal16_gem_count TINYINT UNSIGNED NOT NULL DEFAULT 5 "
            "AFTER gem_percent_per_level"))
        return false;
    if (!vm_net_mock_gem_config_ensure_column(
            "crystal16_exchange_enabled",
            "ALTER TABLE server_gem_config ADD COLUMN "
            "crystal16_exchange_enabled TINYINT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER crystal16_gem_count"))
        return false;
    if (!vm_net_mock_gem_config_ensure_column(
            "socket_embed_enabled",
            "ALTER TABLE server_gem_config ADD COLUMN "
            "socket_embed_enabled TINYINT UNSIGNED NOT NULL DEFAULT 1 "
            "AFTER crystal16_exchange_enabled"))
        return false;
    if (!vm_mysql_exec(
            "INSERT IGNORE INTO server_gem_config "
            "(id,max_gem_level,gem_percent_per_level,crystal16_gem_count,"
            "crystal16_exchange_enabled,socket_embed_enabled) "
            "VALUES (1,3,1,5,0,1)"))
        return false;
    return true;
}

static void vm_net_mock_gem_config_load(bool force)
{
    if (g_vm_net_mock_gem_config_loaded && !force)
        return;
    vm_net_mock_gem_config_set_defaults(&g_vm_net_mock_gem_config);
    if (!vm_net_mock_gem_config_ensure_table())
    {
        printf("[error][mock-admin] gem_config ensure failed error=%s\n",
               vm_mysql_last_error());
        vm_net_mock_gem_config_sanitize(&g_vm_net_mock_gem_config);
        g_vm_net_mock_gem_config_loaded = true;
        return;
    }
    if (!vm_mysql_query(
            "SELECT max_gem_level,gem_percent_per_level,crystal16_gem_count,"
            "crystal16_exchange_enabled,socket_embed_enabled "
            "FROM server_gem_config WHERE id=1 LIMIT 1",
            vm_net_mock_gem_config_row, &g_vm_net_mock_gem_config))
    {
        printf("[error][mock-admin] gem_config load failed error=%s\n",
               vm_mysql_last_error());
        vm_net_mock_gem_config_set_defaults(&g_vm_net_mock_gem_config);
    }
    vm_net_mock_gem_config_sanitize(&g_vm_net_mock_gem_config);
    g_vm_net_mock_gem_config_loaded = true;
    printf("[info][mock-admin] gem_config_load max_gem_level=%u "
           "gem_percent_per_level=%u crystal16_gem_count=%u "
           "crystal16_exchange_enabled=%u socket_embed_enabled=%u\n",
           (u32)g_vm_net_mock_gem_config.maxGemLevel,
           (u32)g_vm_net_mock_gem_config.gemPercentPerLevel,
           (u32)g_vm_net_mock_gem_config.crystal16GemCount,
           (u32)g_vm_net_mock_gem_config.crystal16ExchangeEnabled,
           (u32)g_vm_net_mock_gem_config.socketEmbedEnabled);
}

static bool vm_net_mock_gem_config_admin_save(u8 maxGemLevel, u8 gemPercentPerLevel,
                                              u8 crystal16GemCount,
                                              u8 crystal16ExchangeEnabled,
                                              u8 socketEmbedEnabled)
{
    vm_net_mock_gem_config cfg;
    char query[768];

    vm_net_mock_gem_config_set_defaults(&cfg);
    cfg.maxGemLevel = maxGemLevel;
    cfg.gemPercentPerLevel = gemPercentPerLevel;
    cfg.crystal16GemCount = crystal16GemCount;
    cfg.crystal16ExchangeEnabled = crystal16ExchangeEnabled ? 1u : 0u;
    cfg.socketEmbedEnabled = socketEmbedEnabled ? 1u : 0u;
    vm_net_mock_gem_config_sanitize(&cfg);
    if (!vm_net_mock_gem_config_ensure_table())
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_gem_config "
             "(id,max_gem_level,gem_percent_per_level,crystal16_gem_count,"
             "crystal16_exchange_enabled,socket_embed_enabled) "
             "VALUES (1,%u,%u,%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE "
             "max_gem_level=VALUES(max_gem_level),"
             "gem_percent_per_level=VALUES(gem_percent_per_level),"
             "crystal16_gem_count=VALUES(crystal16_gem_count),"
             "crystal16_exchange_enabled=VALUES(crystal16_exchange_enabled),"
             "socket_embed_enabled=VALUES(socket_embed_enabled)",
             (u32)cfg.maxGemLevel, (u32)cfg.gemPercentPerLevel,
             (u32)cfg.crystal16GemCount, (u32)cfg.crystal16ExchangeEnabled,
             (u32)cfg.socketEmbedEnabled);
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-admin] gem_config save failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_gem_config_loaded = false;
    vm_net_mock_gem_config_load(true);
    return true;
}

static const vm_net_mock_gem_config *vm_net_mock_gem_config_get(void)
{
    vm_net_mock_gem_config_load(false);
    return &g_vm_net_mock_gem_config;
}

static u8 vm_net_mock_gem_max_level(void)
{
    return vm_net_mock_gem_config_get()->maxGemLevel;
}

static u8 vm_net_mock_gem_percent_per_level(void)
{
    return vm_net_mock_gem_config_get()->gemPercentPerLevel;
}

static u8 vm_net_mock_gem_crystal16_grant(void)
{
    return vm_net_mock_gem_config_get()->crystal16GemCount;
}

static bool vm_net_mock_gem_crystal16_exchange_enabled(void)
{
    return vm_net_mock_gem_config_get()->crystal16ExchangeEnabled != 0;
}

static bool vm_net_mock_gem_socket_embed_enabled(void)
{
    return vm_net_mock_gem_config_get()->socketEmbedEnabled != 0;
}

static u8 vm_net_mock_gem_clamp_level(u8 level)
{
    u8 maxLv = vm_net_mock_gem_max_level();
    if (level == 0)
        return 0;
    if (level > maxLv)
        return maxLv;
    return level;
}

static u32 vm_net_mock_gem_item_id_for(u8 type, u8 level)
{
    u32 base = 0;
    if (level == 0 || level > (u8)VM_NET_MOCK_GEM_LEVEL_RESERVE)
        return 0;
    switch (type)
    {
    case VM_NET_MOCK_GEM_TYPE_REFLECT:
        base = (u32)VM_NET_MOCK_GEM_REFLECT_BASE_ID;
        break;
    case VM_NET_MOCK_GEM_TYPE_COMBO:
        base = (u32)VM_NET_MOCK_GEM_COMBO_BASE_ID;
        break;
    case VM_NET_MOCK_GEM_TYPE_REVIVE:
        base = (u32)VM_NET_MOCK_GEM_REVIVE_BASE_ID;
        break;
    default:
        return 0;
    }
    return base + (u32)level - 1u;
}

static bool vm_net_mock_gem_parse_item(u32 itemId, u8 *typeOut, u8 *levelOut)
{
    u8 type = VM_NET_MOCK_GEM_TYPE_NONE;
    u8 level = 0;
    u32 base = 0;

    if (typeOut)
        *typeOut = 0;
    if (levelOut)
        *levelOut = 0;
    if (itemId >= (u32)VM_NET_MOCK_GEM_REFLECT_BASE_ID &&
        itemId < (u32)VM_NET_MOCK_GEM_REFLECT_BASE_ID +
                     (u32)VM_NET_MOCK_GEM_LEVEL_RESERVE)
    {
        type = VM_NET_MOCK_GEM_TYPE_REFLECT;
        base = (u32)VM_NET_MOCK_GEM_REFLECT_BASE_ID;
    }
    else if (itemId >= (u32)VM_NET_MOCK_GEM_COMBO_BASE_ID &&
             itemId < (u32)VM_NET_MOCK_GEM_COMBO_BASE_ID +
                          (u32)VM_NET_MOCK_GEM_LEVEL_RESERVE)
    {
        type = VM_NET_MOCK_GEM_TYPE_COMBO;
        base = (u32)VM_NET_MOCK_GEM_COMBO_BASE_ID;
    }
    else if (itemId >= (u32)VM_NET_MOCK_GEM_REVIVE_BASE_ID &&
             itemId < (u32)VM_NET_MOCK_GEM_REVIVE_BASE_ID +
                          (u32)VM_NET_MOCK_GEM_LEVEL_RESERVE)
    {
        type = VM_NET_MOCK_GEM_TYPE_REVIVE;
        base = (u32)VM_NET_MOCK_GEM_REVIVE_BASE_ID;
    }
    else
        return false;
    level = (u8)(itemId - base + 1u);
    if (typeOut)
        *typeOut = type;
    if (levelOut)
        *levelOut = level;
    return true;
}

static bool vm_net_mock_gem_item_is_gem_or_fragment(u32 itemId)
{
    u8 type = 0;
    u8 level = 0;
    if (itemId == (u32)VM_NET_MOCK_GEM_FRAGMENT_ITEM_ID)
        return true;
    return vm_net_mock_gem_parse_item(itemId, &type, &level);
}

static bool vm_net_mock_gem_sockets_occupied(u8 reflect, u8 combo, u8 revive)
{
    return reflect != 0 || combo != 0 || revive != 0;
}

static u32 vm_net_mock_gem_level_to_percent(u8 level)
{
    u8 capped = vm_net_mock_gem_clamp_level(level);
    return (u32)capped * (u32)vm_net_mock_gem_percent_per_level();
}

static void vm_net_mock_role_collect_gem_percents(const vm_net_mock_role_state *role,
                                                 u32 *reflectOut, u32 *comboOut,
                                                 u32 *reviveOut)
{
    vm_net_mock_role_service_state *service = NULL;
    u32 reflect = 0;
    u32 combo = 0;
    u32 revive = 0;
    u8 slot = 0;

    if (reflectOut)
        *reflectOut = 0;
    if (comboOut)
        *comboOut = 0;
    if (reviveOut)
        *reviveOut = 0;
    if (role == NULL)
        return;
    service = vm_net_mock_role_service_state_get(role);
    for (slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        u32 itemId = role->equippedItemIds[slot];
        if (itemId == 0)
            continue;
        if (service != NULL && service->equipmentItemIds[slot] == itemId &&
            service->durability[slot] == 0)
            continue;
        reflect += vm_net_mock_gem_level_to_percent(role->equippedGemReflect[slot]);
        combo += vm_net_mock_gem_level_to_percent(role->equippedGemCombo[slot]);
        revive += vm_net_mock_gem_level_to_percent(role->equippedGemRevive[slot]);
    }
    if (reflect > 100u)
        reflect = 100u;
    if (combo > 100u)
        combo = 100u;
    if (revive > 100u)
        revive = 100u;
    if (reflectOut)
        *reflectOut = reflect;
    if (comboOut)
        *comboOut = combo;
    if (reviveOut)
        *reviveOut = revive;
}
