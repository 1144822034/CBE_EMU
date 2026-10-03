/* Whole-server monster percent knobs (MySQL server_monster_global_config).
 * Included from mock_server_role.c near other singleton configs. Defaults are
 * all 100 (= unchanged). 90 lowers, 110 raises. Admin edits stay on raw bases;
 * these percents apply only at combat/reward read time. */

typedef struct
{
    u32 hpPercent;           /* default 100 */
    u32 attackPercent;       /* default 100 */
    u32 defensePercent;      /* default 100 */
    u32 dropRatePercent;     /* default 100 */
    u32 rewardExpPercent;    /* default 100 */
    u32 rewardMoneyPercent;  /* default 100 */
    /* When level multiplier >= 6 (Lv>=56), multiply HP by 3 after the
     * level scale and before Boss HP×5 / whole-server percents. 0=off. */
    u32 hpExtraX3WhenMultGe6;
} vm_net_mock_monster_global_config;

static vm_net_mock_monster_global_config g_vm_net_mock_monster_global_config;
static bool g_vm_net_mock_monster_global_config_loaded = false;

static void vm_net_mock_monster_global_config_set_defaults(
    vm_net_mock_monster_global_config *cfg)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->hpPercent = 100;
    cfg->attackPercent = 100;
    cfg->defensePercent = 100;
    cfg->dropRatePercent = 100;
    cfg->rewardExpPercent = 100;
    cfg->rewardMoneyPercent = 100;
    cfg->hpExtraX3WhenMultGe6 = 0;
}

static void vm_net_mock_monster_global_config_sanitize(
    vm_net_mock_monster_global_config *cfg)
{
    if (cfg == NULL)
        return;
    /* Combat attrs: keep at least 1% so battles stay resolvable. */
    if (cfg->hpPercent == 0)
        cfg->hpPercent = 1;
    if (cfg->hpPercent > 1000u)
        cfg->hpPercent = 1000u;
    if (cfg->attackPercent == 0)
        cfg->attackPercent = 1;
    if (cfg->attackPercent > 1000u)
        cfg->attackPercent = 1000u;
    if (cfg->defensePercent > 1000u)
        cfg->defensePercent = 1000u;
    /* Drop / rewards may be 0 (= none). */
    if (cfg->dropRatePercent > 1000u)
        cfg->dropRatePercent = 1000u;
    if (cfg->rewardExpPercent > 1000u)
        cfg->rewardExpPercent = 1000u;
    if (cfg->rewardMoneyPercent > 1000u)
        cfg->rewardMoneyPercent = 1000u;
    cfg->hpExtraX3WhenMultGe6 = cfg->hpExtraX3WhenMultGe6 != 0 ? 1u : 0u;
}

static u32 vm_net_mock_scale_u32_by_percent(u32 value, u32 percent)
{
    uint64_t product;

    if (percent == 100u || value == 0)
        return value;
    if (percent == 0)
        return 0;
    product = ((uint64_t)value * (uint64_t)percent) / 100ull;
    return product > 0xffffffffull ? 0xffffffffu : (u32)product;
}

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} vm_net_mock_monster_global_column_ctx;

static bool vm_net_mock_monster_global_column_count_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_monster_global_column_ctx *ctx =
        (vm_net_mock_monster_global_column_ctx *)contextValue;

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

static bool vm_net_mock_monster_global_ensure_column(const char *columnName,
                                                    const char *alterSql)
{
    vm_net_mock_monster_global_column_ctx ctx;
    char probe[320];

    if (columnName == NULL || alterSql == NULL)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(probe, sizeof(probe),
             "SELECT COUNT(*) FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA=DATABASE() AND "
             "TABLE_NAME='server_monster_global_config' AND COLUMN_NAME='%s'",
             columnName);
    if (!vm_mysql_query(probe, vm_net_mock_monster_global_column_count_row,
                        &ctx) ||
        ctx.invalid || !ctx.found)
    {
        printf("[error][mock-service] monster_global column probe %s error=%s\n",
               columnName, vm_mysql_last_error());
        return false;
    }
    if (ctx.count == 0)
    {
        if (!vm_mysql_exec(alterSql))
        {
            printf("[error][mock-service] monster_global add column %s "
                   "error=%s\n",
                   columnName, vm_mysql_last_error());
            return false;
        }
        printf("[info][mock-service] monster_global added column %s\n",
               columnName);
    }
    return true;
}

static bool vm_net_mock_monster_global_config_ensure_table(void)
{
    static bool ready = false;

    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_monster_global_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "hp_percent INT UNSIGNED NOT NULL DEFAULT 100,"
            "attack_percent INT UNSIGNED NOT NULL DEFAULT 100,"
            "defense_percent INT UNSIGNED NOT NULL DEFAULT 100,"
            "drop_rate_percent INT UNSIGNED NOT NULL DEFAULT 100,"
            "reward_exp_percent INT UNSIGNED NOT NULL DEFAULT 100,"
            "reward_money_percent INT UNSIGNED NOT NULL DEFAULT 100,"
            "hp_extra_x3_when_mult_ge6 TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
    {
        printf("[error][mock-service] monster_global ensure_table failed "
               "error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (!vm_net_mock_monster_global_ensure_column(
            "reward_exp_percent",
            "ALTER TABLE server_monster_global_config ADD COLUMN "
            "reward_exp_percent INT UNSIGNED NOT NULL DEFAULT 100 "
            "AFTER drop_rate_percent") ||
        !vm_net_mock_monster_global_ensure_column(
            "reward_money_percent",
            "ALTER TABLE server_monster_global_config ADD COLUMN "
            "reward_money_percent INT UNSIGNED NOT NULL DEFAULT 100 "
            "AFTER reward_exp_percent") ||
        !vm_net_mock_monster_global_ensure_column(
            "hp_extra_x3_when_mult_ge6",
            "ALTER TABLE server_monster_global_config ADD COLUMN "
            "hp_extra_x3_when_mult_ge6 TINYINT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER reward_money_percent"))
    {
        return false;
    }
    if (!vm_mysql_exec(
            "INSERT IGNORE INTO server_monster_global_config ("
            "id,hp_percent,attack_percent,defense_percent,drop_rate_percent,"
            "reward_exp_percent,reward_money_percent,"
            "hp_extra_x3_when_mult_ge6) "
            "VALUES (1,100,100,100,100,100,100,0)"))
    {
        printf("[error][mock-service] monster_global seed failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    ready = true;
    return true;
}

static bool vm_net_mock_monster_global_config_row(void *contextValue,
                                                  unsigned int columnCount,
                                                  const char *const *values,
                                                  const size_t *lengths)
{
    vm_net_mock_monster_global_config *cfg =
        (vm_net_mock_monster_global_config *)contextValue;
    u32 fields[7];
    u32 i;

    if (cfg == NULL || values == NULL || lengths == NULL || columnCount < 7)
        return true;
    for (i = 0; i < 7; ++i)
    {
        if (values[i] == NULL ||
            !vm_mock_mysql_parse_u32(values[i], lengths[i], &fields[i]))
            return true;
    }
    cfg->hpPercent = fields[0];
    cfg->attackPercent = fields[1];
    cfg->defensePercent = fields[2];
    cfg->dropRatePercent = fields[3];
    cfg->rewardExpPercent = fields[4];
    cfg->rewardMoneyPercent = fields[5];
    cfg->hpExtraX3WhenMultGe6 = fields[6];
    vm_net_mock_monster_global_config_sanitize(cfg);
    return true;
}

static void vm_net_mock_monster_global_config_load(bool force)
{
    if (g_vm_net_mock_monster_global_config_loaded && !force)
        return;
    vm_net_mock_monster_global_config_set_defaults(
        &g_vm_net_mock_monster_global_config);
    if (!vm_net_mock_monster_global_config_ensure_table())
    {
        vm_net_mock_monster_global_config_sanitize(
            &g_vm_net_mock_monster_global_config);
        g_vm_net_mock_monster_global_config_loaded = true;
        return;
    }
    if (!vm_mysql_query(
            "SELECT hp_percent,attack_percent,defense_percent,"
            "drop_rate_percent,reward_exp_percent,reward_money_percent,"
            "hp_extra_x3_when_mult_ge6 "
            "FROM server_monster_global_config WHERE id=1 LIMIT 1",
            vm_net_mock_monster_global_config_row,
            &g_vm_net_mock_monster_global_config))
    {
        printf("[error][mock-service] monster_global load failed error=%s\n",
               vm_mysql_last_error());
        vm_net_mock_monster_global_config_set_defaults(
            &g_vm_net_mock_monster_global_config);
    }
    vm_net_mock_monster_global_config_sanitize(
        &g_vm_net_mock_monster_global_config);
    g_vm_net_mock_monster_global_config_loaded = true;
    printf("[info][mock-service] monster_global_config_load hp%%=%u atk%%=%u "
           "def%%=%u drop%%=%u exp%%=%u money%%=%u hp_x3=%u\n",
           g_vm_net_mock_monster_global_config.hpPercent,
           g_vm_net_mock_monster_global_config.attackPercent,
           g_vm_net_mock_monster_global_config.defensePercent,
           g_vm_net_mock_monster_global_config.dropRatePercent,
           g_vm_net_mock_monster_global_config.rewardExpPercent,
           g_vm_net_mock_monster_global_config.rewardMoneyPercent,
           g_vm_net_mock_monster_global_config.hpExtraX3WhenMultGe6);
}

static bool vm_net_mock_monster_global_config_admin_save(
    const vm_net_mock_monster_global_config *incoming)
{
    vm_net_mock_monster_global_config cfg;
    char query[768];

    if (incoming == NULL)
        return false;
    cfg = *incoming;
    vm_net_mock_monster_global_config_sanitize(&cfg);
    if (!vm_net_mock_monster_global_config_ensure_table())
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_monster_global_config ("
             "id,hp_percent,attack_percent,defense_percent,drop_rate_percent,"
             "reward_exp_percent,reward_money_percent,"
             "hp_extra_x3_when_mult_ge6) "
             "VALUES (1,%u,%u,%u,%u,%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE "
             "hp_percent=VALUES(hp_percent),"
             "attack_percent=VALUES(attack_percent),"
             "defense_percent=VALUES(defense_percent),"
             "drop_rate_percent=VALUES(drop_rate_percent),"
             "reward_exp_percent=VALUES(reward_exp_percent),"
             "reward_money_percent=VALUES(reward_money_percent),"
             "hp_extra_x3_when_mult_ge6=VALUES(hp_extra_x3_when_mult_ge6)",
             cfg.hpPercent, cfg.attackPercent, cfg.defensePercent,
             cfg.dropRatePercent, cfg.rewardExpPercent, cfg.rewardMoneyPercent,
             cfg.hpExtraX3WhenMultGe6);
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-service] monster_global save failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_monster_global_config_loaded = false;
    vm_net_mock_monster_global_config_load(true);
    return true;
}

static const vm_net_mock_monster_global_config *
vm_net_mock_monster_global_config_get(void)
{
    vm_net_mock_monster_global_config_load(false);
    return &g_vm_net_mock_monster_global_config;
}

static u32 vm_net_mock_monster_global_effective_drop_rate(u32 configuredRate)
{
    /* configuredRate is percent×10 (1=0.1%, 1000=100%). */
    const u32 rateX10Max = 1000u;
    const vm_net_mock_monster_global_config *cfg =
        vm_net_mock_monster_global_config_get();
    u32 percent;
    u32 scaled;

    if (configuredRate == 0)
        return 0;
    percent = cfg != NULL ? cfg->dropRatePercent : 100u;
    if (percent == 100u)
        return configuredRate > rateX10Max ? rateX10Max : configuredRate;
    scaled = vm_net_mock_scale_u32_by_percent(configuredRate, percent);
    if (scaled > rateX10Max)
        scaled = rateX10Max;
    return scaled;
}
