/* Enhance catalog-base scaling (MySQL server_enhance_base_scale).
 * Included from mock_server_catalog.c before M(L) helpers.
 * M(L) bonusPct = stepPercent*L + tierExtraPercent*floor(L/4).
 * Each attr keeps base and takes applyPercent% of (M(L)-base). */

enum
{
    VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX = 1000
};

typedef struct
{
    u32 stepPercent;
    u32 tierExtraPercent;
    u32 applyAttack;
    u32 applyArmor;
    u32 applyStrength;
    u32 applyAgility;
    u32 applyWisdom;
    u32 applyHp;
    u32 applyMp;
    u32 applyDodge;
    u32 applyHit;
    u32 applyCrit;
    u32 applyResist;
} vm_net_mock_enhance_base_scale;

static vm_net_mock_enhance_base_scale g_vm_net_mock_enhance_base_scale;
static bool g_vm_net_mock_enhance_base_scale_loaded = false;

static void vm_net_mock_enhance_base_scale_set_defaults(
    vm_net_mock_enhance_base_scale *cfg)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->stepPercent = 10;
    cfg->tierExtraPercent = 35;
    cfg->applyAttack = 100;
    cfg->applyArmor = 50;
    cfg->applyStrength = 50;
    cfg->applyAgility = 50;
    cfg->applyWisdom = 50;
    cfg->applyHp = 50;
    cfg->applyMp = 50;
    cfg->applyDodge = 0;
    cfg->applyHit = 0;
    cfg->applyCrit = 0;
    cfg->applyResist = 0;
}

static void vm_net_mock_enhance_base_scale_sanitize(
    vm_net_mock_enhance_base_scale *cfg)
{
    if (cfg == NULL)
        return;
    if (cfg->stepPercent > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->stepPercent = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->tierExtraPercent > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->tierExtraPercent = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyAttack > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyAttack = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyArmor > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyArmor = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyStrength > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyStrength = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyAgility > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyAgility = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyWisdom > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyWisdom = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyHp > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyHp = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyMp > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyMp = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyDodge > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyDodge = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyHit > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyHit = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyCrit > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyCrit = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
    if (cfg->applyResist > VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX)
        cfg->applyResist = VM_NET_MOCK_ENHANCE_BASE_SCALE_APPLY_MAX;
}

static bool vm_net_mock_enhance_base_scale_count_row(void *contextValue,
                                                    unsigned int columnCount,
                                                    const char *const *values,
                                                    const size_t *lengths)
{
    u32 *count = (u32 *)contextValue;

    (void)columnCount;
    (void)lengths;
    if (count == NULL || values == NULL || values[0] == NULL)
        return false;
    *count = (u32)strtoul(values[0], NULL, 10);
    return true;
}

static bool vm_net_mock_enhance_base_scale_ensure_column(const char *columnName,
                                                        const char *alterSql)
{
    u32 count = 0;
    char probe[320];

    if (columnName == NULL || alterSql == NULL)
        return false;
    snprintf(probe, sizeof(probe),
             "SELECT COUNT(*) FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA=DATABASE() AND "
             "TABLE_NAME='server_enhance_base_scale' AND COLUMN_NAME='%s'",
             columnName);
    if (!vm_mysql_query(probe, vm_net_mock_enhance_base_scale_count_row, &count))
        return false;
    if (count != 0)
        return true;
    if (!vm_mysql_exec(alterSql))
    {
        printf("[error][mock-admin] enhance_base_scale add %s error=%s\n",
               columnName, vm_mysql_last_error());
        return false;
    }
    printf("[info][mock-admin] enhance_base_scale added column %s\n",
           columnName);
    return true;
}

static bool vm_net_mock_enhance_base_scale_row(void *contextValue,
                                              unsigned int columnCount,
                                              const char *const *values,
                                              const size_t *lengths)
{
    vm_net_mock_enhance_base_scale *cfg =
        (vm_net_mock_enhance_base_scale *)contextValue;
    u32 parsed[13];
    u32 i;

    if (cfg == NULL || values == NULL || lengths == NULL || columnCount < 13)
        return false;
    vm_net_mock_enhance_base_scale_set_defaults(cfg);
    for (i = 0; i < 13; ++i)
        parsed[i] = 0;
    for (i = 0; i < 13; ++i)
    {
        if (values[i] == NULL ||
            !vm_mock_mysql_parse_u32(values[i], lengths[i], &parsed[i]))
            continue;
    }
    cfg->stepPercent = parsed[0];
    cfg->tierExtraPercent = parsed[1];
    cfg->applyAttack = parsed[2];
    cfg->applyArmor = parsed[3];
    cfg->applyStrength = parsed[4];
    cfg->applyAgility = parsed[5];
    cfg->applyWisdom = parsed[6];
    cfg->applyHp = parsed[7];
    cfg->applyMp = parsed[8];
    cfg->applyDodge = parsed[9];
    cfg->applyHit = parsed[10];
    cfg->applyCrit = parsed[11];
    cfg->applyResist = parsed[12];
    vm_net_mock_enhance_base_scale_sanitize(cfg);
    return true;
}

static bool vm_net_mock_enhance_base_scale_ensure_table(void)
{
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_enhance_base_scale ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "step_percent INT UNSIGNED NOT NULL DEFAULT 10,"
            "tier_extra_percent INT UNSIGNED NOT NULL DEFAULT 35,"
            "apply_attack INT UNSIGNED NOT NULL DEFAULT 100,"
            "apply_armor INT UNSIGNED NOT NULL DEFAULT 50,"
            "apply_strength INT UNSIGNED NOT NULL DEFAULT 50,"
            "apply_agility INT UNSIGNED NOT NULL DEFAULT 50,"
            "apply_wisdom INT UNSIGNED NOT NULL DEFAULT 50,"
            "apply_hp INT UNSIGNED NOT NULL DEFAULT 50,"
            "apply_mp INT UNSIGNED NOT NULL DEFAULT 50,"
            "apply_dodge INT UNSIGNED NOT NULL DEFAULT 0,"
            "apply_hit INT UNSIGNED NOT NULL DEFAULT 0,"
            "apply_crit INT UNSIGNED NOT NULL DEFAULT 0,"
            "apply_resist INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
        return false;
    if (!vm_mysql_exec(
            "INSERT IGNORE INTO server_enhance_base_scale "
            "(id,step_percent,tier_extra_percent,apply_attack,apply_armor,"
            "apply_strength,apply_agility,apply_wisdom,apply_hp,apply_mp,"
            "apply_dodge,apply_hit,apply_crit,apply_resist) "
            "VALUES (1,10,35,100,50,50,50,50,50,50,0,0,0,0)"))
        return false;
    return true;
}

static void vm_net_mock_enhance_base_scale_load(bool force)
{
    if (g_vm_net_mock_enhance_base_scale_loaded && !force)
        return;
    vm_net_mock_enhance_base_scale_set_defaults(
        &g_vm_net_mock_enhance_base_scale);
    if (!vm_net_mock_enhance_base_scale_ensure_table())
    {
        printf("[error][mock-admin] enhance_base_scale ensure failed error=%s\n",
               vm_mysql_last_error());
        g_vm_net_mock_enhance_base_scale_loaded = true;
        return;
    }
    if (!vm_mysql_query(
            "SELECT step_percent,tier_extra_percent,apply_attack,apply_armor,"
            "apply_strength,apply_agility,apply_wisdom,apply_hp,apply_mp,"
            "apply_dodge,apply_hit,apply_crit,apply_resist "
            "FROM server_enhance_base_scale WHERE id=1 LIMIT 1",
            vm_net_mock_enhance_base_scale_row,
            &g_vm_net_mock_enhance_base_scale))
    {
        printf("[error][mock-admin] enhance_base_scale load failed error=%s\n",
               vm_mysql_last_error());
        vm_net_mock_enhance_base_scale_set_defaults(
            &g_vm_net_mock_enhance_base_scale);
    }
    vm_net_mock_enhance_base_scale_sanitize(&g_vm_net_mock_enhance_base_scale);
    g_vm_net_mock_enhance_base_scale_loaded = true;
    printf("[info][mock-admin] enhance_base_scale step=%u tier=%u "
           "atk=%u armor=%u str=%u agi=%u wis=%u hp=%u mp=%u "
           "dodge=%u hit=%u crit=%u resist=%u\n",
           g_vm_net_mock_enhance_base_scale.stepPercent,
           g_vm_net_mock_enhance_base_scale.tierExtraPercent,
           g_vm_net_mock_enhance_base_scale.applyAttack,
           g_vm_net_mock_enhance_base_scale.applyArmor,
           g_vm_net_mock_enhance_base_scale.applyStrength,
           g_vm_net_mock_enhance_base_scale.applyAgility,
           g_vm_net_mock_enhance_base_scale.applyWisdom,
           g_vm_net_mock_enhance_base_scale.applyHp,
           g_vm_net_mock_enhance_base_scale.applyMp,
           g_vm_net_mock_enhance_base_scale.applyDodge,
           g_vm_net_mock_enhance_base_scale.applyHit,
           g_vm_net_mock_enhance_base_scale.applyCrit,
           g_vm_net_mock_enhance_base_scale.applyResist);
}

static const vm_net_mock_enhance_base_scale *
vm_net_mock_enhance_base_scale_get(void)
{
    vm_net_mock_enhance_base_scale_load(false);
    return &g_vm_net_mock_enhance_base_scale;
}

static bool vm_net_mock_enhance_base_scale_admin_save(
    const vm_net_mock_enhance_base_scale *src)
{
    vm_net_mock_enhance_base_scale cfg;
    char query[1024];

    if (src == NULL)
        return false;
    cfg = *src;
    vm_net_mock_enhance_base_scale_sanitize(&cfg);
    if (!vm_net_mock_enhance_base_scale_ensure_table())
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_enhance_base_scale "
             "(id,step_percent,tier_extra_percent,apply_attack,apply_armor,"
             "apply_strength,apply_agility,apply_wisdom,apply_hp,apply_mp,"
             "apply_dodge,apply_hit,apply_crit,apply_resist) "
             "VALUES (1,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE "
             "step_percent=VALUES(step_percent),"
             "tier_extra_percent=VALUES(tier_extra_percent),"
             "apply_attack=VALUES(apply_attack),"
             "apply_armor=VALUES(apply_armor),"
             "apply_strength=VALUES(apply_strength),"
             "apply_agility=VALUES(apply_agility),"
             "apply_wisdom=VALUES(apply_wisdom),"
             "apply_hp=VALUES(apply_hp),"
             "apply_mp=VALUES(apply_mp),"
             "apply_dodge=VALUES(apply_dodge),"
             "apply_hit=VALUES(apply_hit),"
             "apply_crit=VALUES(apply_crit),"
             "apply_resist=VALUES(apply_resist)",
             cfg.stepPercent, cfg.tierExtraPercent, cfg.applyAttack,
             cfg.applyArmor, cfg.applyStrength, cfg.applyAgility,
             cfg.applyWisdom, cfg.applyHp, cfg.applyMp, cfg.applyDodge,
             cfg.applyHit, cfg.applyCrit, cfg.applyResist);
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-admin] enhance_base_scale save failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_enhance_base_scale_loaded = false;
    vm_net_mock_enhance_base_scale_load(true);
    return true;
}

static u32 vm_net_mock_enhance_base_scale_apply_for_type(u8 type)
{
    const vm_net_mock_enhance_base_scale *cfg =
        vm_net_mock_enhance_base_scale_get();

    switch (type)
    {
    case VM_NET_MOCK_EQUIP_ATTR_ATTACK:
        return cfg->applyAttack;
    case VM_NET_MOCK_EQUIP_ATTR_ARMOR:
        return cfg->applyArmor;
    case VM_NET_MOCK_EQUIP_ATTR_STRENGTH:
        return cfg->applyStrength;
    case VM_NET_MOCK_EQUIP_ATTR_AGILITY:
        return cfg->applyAgility;
    case VM_NET_MOCK_EQUIP_ATTR_WISDOM:
        return cfg->applyWisdom;
    case VM_NET_MOCK_EQUIP_ATTR_HP:
        return cfg->applyHp;
    case VM_NET_MOCK_EQUIP_ATTR_MP:
        return cfg->applyMp;
    case VM_NET_MOCK_EQUIP_ATTR_DODGE:
        return cfg->applyDodge;
    case VM_NET_MOCK_EQUIP_ATTR_HIT:
        return cfg->applyHit;
    case VM_NET_MOCK_EQUIP_ATTR_CRIT:
        return cfg->applyCrit;
    default:
        return 0;
    }
}
