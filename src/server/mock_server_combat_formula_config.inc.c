/* Combat formula singleton config (MySQL-backed).
 * Included from mock_server_role.c before panel/battle math so role/helpers/
 * battle/admin share one cache. Defaults match provisional server formulas. */

typedef struct
{
    /* Panel derived stats */
    u32 panelDefenseBase;           /* default 4 */
    u32 panelDefenseLevelCoeff;     /* defense += level * coeff; default 1 */
    u32 panelDefenseEnduranceDiv;   /* endurance / div; default 2; min 1 */
    u32 panelHitBase;               /* hit = base + equip.hit; default 101 */
    /* bareHp += strength * x100 / 100; bareMp += wisdom * x100 / 100
     * (panel totals = base + equip; default 50 = 0.5) */
    u32 panelStrengthHpCoeffX100;   /* default 50 (=0.5) */
    u32 panelWisdomMpCoeffX100;     /* default 50 (=0.5) */

    /* Panel/battle attack: attack = jobPrimary * coeff% / 100 + equip.attack */
    u32 primaryAttackCoeffTianjiPercent;   /* job 1; default 100 */
    u32 primaryAttackCoeffHuanjianPercent; /* job 2; default 100 */
    u32 primaryAttackCoeffGuidaoPercent;   /* job 3; default 100 */

    /* Soft armor NA mitigation: damage=(atk*K+def/2)/(K+def) */
    u32 softArmorK;                 /* default 2000 */
    u32 equipArmorDivisor;          /* mitigation uses equip.armor/div; default 5 */

    /* Hit / dodge / crit / resist rolls */
    u32 hitChanceMin;               /* clamp floor; default 5 */
    u32 hitChanceMax;               /* clamp ceil; default 95 */
    u32 critDamagePercent;          /* crit multiplier; default 150 */
    u32 resistCapPercent;           /* hard cap on resist mitigation; default 70 */

    /* Skill / duel modifiers */
    u32 jobSkillAttackBonusPercent; /* PvE skill +%; default 5 */
    u32 naVariancePercent;          /* NA ±% PvE+duel; default 5 */
    u32 skillVariancePercent;       /* skill ±% PvE+duel; default 3 */
    u32 duelJobBonusTianjiPercent;  /* job 1; default 10 */
    u32 duelJobBonusHuanjianPercent;/* job 2; default 8 */
    u32 duelJobBonusGuidaoPercent;  /* job 3; default 12 */
    u32 skillVsNaFloorPercent;      /* single-target skill floor vs NA; default 90 */

    /* Monster secondary defaults (env CBE_BATTLE_MONSTER_* still overrides) */
    u32 monsterHitDefault;          /* default 100 */
    u32 monsterDodgeDefault;        /* default 0 */
    u32 monsterCritDefault;         /* default 0 */
    u32 monsterResistDefault;       /* default 0 */

    /* Boss skill counter */
    u32 bossSkillChancePercent;     /* default 45 */
    u32 bossSkillDamagePercent;     /* default 300 */
} vm_net_mock_combat_formula_config;

static vm_net_mock_combat_formula_config g_vm_net_mock_combat_formula_config;
static bool g_vm_net_mock_combat_formula_config_loaded = false;

static void vm_net_mock_combat_formula_config_set_defaults(
    vm_net_mock_combat_formula_config *cfg)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->panelDefenseBase = 4;
    cfg->panelDefenseLevelCoeff = 1;
    cfg->panelDefenseEnduranceDiv = 2;
    cfg->panelHitBase = 101;
    cfg->panelStrengthHpCoeffX100 = 50;
    cfg->panelWisdomMpCoeffX100 = 50;
    cfg->primaryAttackCoeffTianjiPercent = 100;
    cfg->primaryAttackCoeffHuanjianPercent = 100;
    cfg->primaryAttackCoeffGuidaoPercent = 100;
    cfg->softArmorK = 2000;
    cfg->equipArmorDivisor = 5;
    cfg->hitChanceMin = 5;
    cfg->hitChanceMax = 95;
    cfg->critDamagePercent = 150;
    cfg->resistCapPercent = 70;
    cfg->jobSkillAttackBonusPercent = 5;
    cfg->naVariancePercent = 5;
    cfg->skillVariancePercent = 3;
    cfg->duelJobBonusTianjiPercent = 10;
    cfg->duelJobBonusHuanjianPercent = 8;
    cfg->duelJobBonusGuidaoPercent = 12;
    cfg->skillVsNaFloorPercent = 90;
    cfg->monsterHitDefault = 100;
    cfg->monsterDodgeDefault = 0;
    cfg->monsterCritDefault = 0;
    cfg->monsterResistDefault = 0;
    cfg->bossSkillChancePercent = 45;
    cfg->bossSkillDamagePercent = 300;
}

static void vm_net_mock_combat_formula_config_sanitize(
    vm_net_mock_combat_formula_config *cfg)
{
    if (cfg == NULL)
        return;
    if (cfg->panelDefenseEnduranceDiv == 0)
        cfg->panelDefenseEnduranceDiv = 1;
    if (cfg->panelStrengthHpCoeffX100 > 1000u)
        cfg->panelStrengthHpCoeffX100 = 1000u;
    if (cfg->panelWisdomMpCoeffX100 > 1000u)
        cfg->panelWisdomMpCoeffX100 = 1000u;
    if (cfg->primaryAttackCoeffTianjiPercent > 1000u)
        cfg->primaryAttackCoeffTianjiPercent = 1000u;
    if (cfg->primaryAttackCoeffHuanjianPercent > 1000u)
        cfg->primaryAttackCoeffHuanjianPercent = 1000u;
    if (cfg->primaryAttackCoeffGuidaoPercent > 1000u)
        cfg->primaryAttackCoeffGuidaoPercent = 1000u;
    if (cfg->softArmorK == 0)
        cfg->softArmorK = 1;
    if (cfg->equipArmorDivisor == 0)
        cfg->equipArmorDivisor = 1;
    if (cfg->hitChanceMin > 100u)
        cfg->hitChanceMin = 100u;
    if (cfg->hitChanceMax > 100u)
        cfg->hitChanceMax = 100u;
    if (cfg->hitChanceMin > cfg->hitChanceMax)
    {
        u32 tmp = cfg->hitChanceMin;
        cfg->hitChanceMin = cfg->hitChanceMax;
        cfg->hitChanceMax = tmp;
    }
    if (cfg->critDamagePercent < 100u)
        cfg->critDamagePercent = 100u;
    if (cfg->critDamagePercent > 1000u)
        cfg->critDamagePercent = 1000u;
    if (cfg->resistCapPercent > 100u)
        cfg->resistCapPercent = 100u;
    if (cfg->jobSkillAttackBonusPercent > 500u)
        cfg->jobSkillAttackBonusPercent = 500u;
    if (cfg->naVariancePercent > 50u)
        cfg->naVariancePercent = 50u;
    if (cfg->skillVariancePercent > 50u)
        cfg->skillVariancePercent = 50u;
    if (cfg->duelJobBonusTianjiPercent > 500u)
        cfg->duelJobBonusTianjiPercent = 500u;
    if (cfg->duelJobBonusHuanjianPercent > 500u)
        cfg->duelJobBonusHuanjianPercent = 500u;
    if (cfg->duelJobBonusGuidaoPercent > 500u)
        cfg->duelJobBonusGuidaoPercent = 500u;
    if (cfg->skillVsNaFloorPercent > 200u)
        cfg->skillVsNaFloorPercent = 200u;
    if (cfg->monsterHitDefault > 1000u)
        cfg->monsterHitDefault = 1000u;
    if (cfg->monsterDodgeDefault > 1000u)
        cfg->monsterDodgeDefault = 1000u;
    if (cfg->monsterCritDefault > 100u)
        cfg->monsterCritDefault = 100u;
    if (cfg->monsterResistDefault > 100u)
        cfg->monsterResistDefault = 100u;
    if (cfg->bossSkillChancePercent > 100u)
        cfg->bossSkillChancePercent = 100u;
    if (cfg->bossSkillDamagePercent < 100u)
        cfg->bossSkillDamagePercent = 100u;
    if (cfg->bossSkillDamagePercent > 1000u)
        cfg->bossSkillDamagePercent = 1000u;
}

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} vm_net_mock_combat_formula_column_ctx;

static bool vm_net_mock_combat_formula_column_count_row(void *contextValue,
                                                       unsigned int columnCount,
                                                       const char *const *values,
                                                       const size_t *lengths)
{
    vm_net_mock_combat_formula_column_ctx *ctx =
        (vm_net_mock_combat_formula_column_ctx *)contextValue;

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

static bool vm_net_mock_combat_formula_ensure_column(const char *columnName,
                                                    const char *alterSql)
{
    vm_net_mock_combat_formula_column_ctx ctx;
    char probe[320];

    if (columnName == NULL || alterSql == NULL)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(probe, sizeof(probe),
             "SELECT COUNT(*) FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA=DATABASE() AND "
             "TABLE_NAME='server_combat_formula_config' AND COLUMN_NAME='%s'",
             columnName);
    if (!vm_mysql_query(probe, vm_net_mock_combat_formula_column_count_row, &ctx) ||
        ctx.invalid || !ctx.found)
    {
        printf("[error][mock-service] combat_formula column probe %s error=%s\n",
               columnName, vm_mysql_last_error());
        return false;
    }
    if (ctx.count == 0)
    {
        if (!vm_mysql_exec(alterSql))
        {
            printf("[error][mock-service] combat_formula add column %s "
                   "error=%s\n",
                   columnName, vm_mysql_last_error());
            return false;
        }
        printf("[info][mock-service] combat_formula added column %s\n",
               columnName);
    }
    return true;
}

static bool vm_net_mock_combat_formula_ensure_table(void)
{
    static bool ready = false;
    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_combat_formula_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "panel_defense_base INT UNSIGNED NOT NULL DEFAULT 4,"
            "panel_defense_level_coeff INT UNSIGNED NOT NULL DEFAULT 1,"
            "panel_defense_endurance_div INT UNSIGNED NOT NULL DEFAULT 2,"
            "panel_hit_base INT UNSIGNED NOT NULL DEFAULT 101,"
            "panel_strength_hp_coeff_x100 INT UNSIGNED NOT NULL DEFAULT 50,"
            "panel_wisdom_mp_coeff_x100 INT UNSIGNED NOT NULL DEFAULT 50,"
            "primary_attack_coeff_tianji_percent INT UNSIGNED NOT NULL "
            "DEFAULT 100,"
            "primary_attack_coeff_huanjian_percent INT UNSIGNED NOT NULL "
            "DEFAULT 100,"
            "primary_attack_coeff_guidao_percent INT UNSIGNED NOT NULL "
            "DEFAULT 100,"
            "soft_armor_k INT UNSIGNED NOT NULL DEFAULT 2000,"
            "equip_armor_divisor INT UNSIGNED NOT NULL DEFAULT 5,"
            "hit_chance_min INT UNSIGNED NOT NULL DEFAULT 5,"
            "hit_chance_max INT UNSIGNED NOT NULL DEFAULT 95,"
            "crit_damage_percent INT UNSIGNED NOT NULL DEFAULT 150,"
            "resist_cap_percent INT UNSIGNED NOT NULL DEFAULT 70,"
            "job_skill_attack_bonus_percent INT UNSIGNED NOT NULL DEFAULT 5,"
            "na_variance_percent INT UNSIGNED NOT NULL DEFAULT 5,"
            "skill_variance_percent INT UNSIGNED NOT NULL DEFAULT 3,"
            "duel_job_bonus_tianji_percent INT UNSIGNED NOT NULL DEFAULT 10,"
            "duel_job_bonus_huanjian_percent INT UNSIGNED NOT NULL DEFAULT 8,"
            "duel_job_bonus_guidao_percent INT UNSIGNED NOT NULL DEFAULT 12,"
            "skill_vs_na_floor_percent INT UNSIGNED NOT NULL DEFAULT 90,"
            "monster_hit_default INT UNSIGNED NOT NULL DEFAULT 100,"
            "monster_dodge_default INT UNSIGNED NOT NULL DEFAULT 0,"
            "monster_crit_default INT UNSIGNED NOT NULL DEFAULT 0,"
            "monster_resist_default INT UNSIGNED NOT NULL DEFAULT 0,"
            "boss_skill_chance_percent INT UNSIGNED NOT NULL DEFAULT 45,"
            "boss_skill_damage_percent INT UNSIGNED NOT NULL DEFAULT 300,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id)"
            ") ENGINE=InnoDB"))
    {
        printf("[error][mock-service] combat_formula ensure_table failed "
               "error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    /* Existing DBs created before these columns: ADD with default 100. */
    if (!vm_net_mock_combat_formula_ensure_column(
            "primary_attack_coeff_tianji_percent",
            "ALTER TABLE server_combat_formula_config ADD COLUMN "
            "primary_attack_coeff_tianji_percent INT UNSIGNED NOT NULL "
            "DEFAULT 100 AFTER panel_hit_base") ||
        !vm_net_mock_combat_formula_ensure_column(
            "primary_attack_coeff_huanjian_percent",
            "ALTER TABLE server_combat_formula_config ADD COLUMN "
            "primary_attack_coeff_huanjian_percent INT UNSIGNED NOT NULL "
            "DEFAULT 100 AFTER primary_attack_coeff_tianji_percent") ||
        !vm_net_mock_combat_formula_ensure_column(
            "primary_attack_coeff_guidao_percent",
            "ALTER TABLE server_combat_formula_config ADD COLUMN "
            "primary_attack_coeff_guidao_percent INT UNSIGNED NOT NULL "
            "DEFAULT 100 AFTER primary_attack_coeff_huanjian_percent") ||
        !vm_net_mock_combat_formula_ensure_column(
            "panel_strength_hp_coeff_x100",
            "ALTER TABLE server_combat_formula_config ADD COLUMN "
            "panel_strength_hp_coeff_x100 INT UNSIGNED NOT NULL "
            "DEFAULT 50 AFTER panel_hit_base") ||
        !vm_net_mock_combat_formula_ensure_column(
            "panel_wisdom_mp_coeff_x100",
            "ALTER TABLE server_combat_formula_config ADD COLUMN "
            "panel_wisdom_mp_coeff_x100 INT UNSIGNED NOT NULL "
            "DEFAULT 50 AFTER panel_strength_hp_coeff_x100"))
    {
        return false;
    }
    /* 2026-09-06: skill variance default 8 → 3. Only rewrite still-at-old-default
     * rows so admin custom values survive. */
    (void)vm_mysql_exec(
        "UPDATE server_combat_formula_config SET skill_variance_percent=3 "
        "WHERE id=1 AND skill_variance_percent=8");
    (void)vm_mysql_exec(
        "ALTER TABLE server_combat_formula_config MODIFY COLUMN "
        "skill_variance_percent INT UNSIGNED NOT NULL DEFAULT 3");
    ready = true;
    return true;
}

typedef struct
{
    vm_net_mock_combat_formula_config *cfg;
    u32 loaded;
} vm_net_mock_combat_formula_load_ctx;

static bool vm_net_mock_combat_formula_config_row_ctx(void *contextValue,
                                                     unsigned int columnCount,
                                                     const char *const *values,
                                                     const size_t *lengths)
{
    vm_net_mock_combat_formula_load_ctx *ctx =
        (vm_net_mock_combat_formula_load_ctx *)contextValue;
    vm_net_mock_combat_formula_config *cfg;
    u32 fields[28];
    u32 i;

    if (ctx == NULL || ctx->cfg == NULL || columnCount < 28)
        return true;
    cfg = ctx->cfg;
    for (i = 0; i < 28; ++i)
    {
        if (!vm_mock_mysql_parse_u32(values[i], lengths[i], &fields[i]))
            return true;
    }
    cfg->panelDefenseBase = fields[0];
    cfg->panelDefenseLevelCoeff = fields[1];
    cfg->panelDefenseEnduranceDiv = fields[2];
    cfg->panelHitBase = fields[3];
    cfg->panelStrengthHpCoeffX100 = fields[4];
    cfg->panelWisdomMpCoeffX100 = fields[5];
    cfg->primaryAttackCoeffTianjiPercent = fields[6];
    cfg->primaryAttackCoeffHuanjianPercent = fields[7];
    cfg->primaryAttackCoeffGuidaoPercent = fields[8];
    cfg->softArmorK = fields[9];
    cfg->equipArmorDivisor = fields[10];
    cfg->hitChanceMin = fields[11];
    cfg->hitChanceMax = fields[12];
    cfg->critDamagePercent = fields[13];
    cfg->resistCapPercent = fields[14];
    cfg->jobSkillAttackBonusPercent = fields[15];
    cfg->naVariancePercent = fields[16];
    cfg->skillVariancePercent = fields[17];
    cfg->duelJobBonusTianjiPercent = fields[18];
    cfg->duelJobBonusHuanjianPercent = fields[19];
    cfg->duelJobBonusGuidaoPercent = fields[20];
    cfg->skillVsNaFloorPercent = fields[21];
    cfg->monsterHitDefault = fields[22];
    cfg->monsterDodgeDefault = fields[23];
    cfg->monsterCritDefault = fields[24];
    cfg->monsterResistDefault = fields[25];
    cfg->bossSkillChancePercent = fields[26];
    cfg->bossSkillDamagePercent = fields[27];
    vm_net_mock_combat_formula_config_sanitize(cfg);
    ctx->loaded = 1;
    return true;
}

static bool vm_net_mock_combat_formula_config_seed_if_empty(void)
{
    char query[1792];
    int written;
    vm_net_mock_combat_formula_config def;

    vm_net_mock_combat_formula_config_set_defaults(&def);
    written = snprintf(
        query, sizeof(query),
        "INSERT IGNORE INTO server_combat_formula_config ("
        "id,panel_defense_base,panel_defense_level_coeff,"
        "panel_defense_endurance_div,panel_hit_base,"
        "panel_strength_hp_coeff_x100,panel_wisdom_mp_coeff_x100,"
        "primary_attack_coeff_tianji_percent,"
        "primary_attack_coeff_huanjian_percent,"
        "primary_attack_coeff_guidao_percent,"
        "soft_armor_k,"
        "equip_armor_divisor,hit_chance_min,hit_chance_max,crit_damage_percent,"
        "resist_cap_percent,job_skill_attack_bonus_percent,na_variance_percent,"
        "skill_variance_percent,duel_job_bonus_tianji_percent,"
        "duel_job_bonus_huanjian_percent,duel_job_bonus_guidao_percent,"
        "skill_vs_na_floor_percent,monster_hit_default,monster_dodge_default,"
        "monster_crit_default,monster_resist_default,boss_skill_chance_percent,"
        "boss_skill_damage_percent) VALUES ("
        "1,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
        "%u,%u,%u,%u,%u)",
        def.panelDefenseBase, def.panelDefenseLevelCoeff,
        def.panelDefenseEnduranceDiv, def.panelHitBase,
        def.panelStrengthHpCoeffX100, def.panelWisdomMpCoeffX100,
        def.primaryAttackCoeffTianjiPercent,
        def.primaryAttackCoeffHuanjianPercent,
        def.primaryAttackCoeffGuidaoPercent, def.softArmorK,
        def.equipArmorDivisor, def.hitChanceMin, def.hitChanceMax,
        def.critDamagePercent, def.resistCapPercent,
        def.jobSkillAttackBonusPercent, def.naVariancePercent,
        def.skillVariancePercent, def.duelJobBonusTianjiPercent,
        def.duelJobBonusHuanjianPercent, def.duelJobBonusGuidaoPercent,
        def.skillVsNaFloorPercent, def.monsterHitDefault,
        def.monsterDodgeDefault, def.monsterCritDefault,
        def.monsterResistDefault, def.bossSkillChancePercent,
        def.bossSkillDamagePercent);
    if (written < 0 || (size_t)written >= sizeof(query))
        return false;
    return vm_mysql_exec(query);
}

static bool vm_net_mock_combat_formula_config_load(bool force)
{
    vm_net_mock_combat_formula_load_ctx ctx;

    if (g_vm_net_mock_combat_formula_config_loaded && !force)
        return true;
    vm_net_mock_combat_formula_config_set_defaults(
        &g_vm_net_mock_combat_formula_config);
    if (!vm_net_mock_combat_formula_ensure_table())
    {
        g_vm_net_mock_combat_formula_config_loaded = true;
        return false;
    }
    (void)vm_net_mock_combat_formula_config_seed_if_empty();
    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = &g_vm_net_mock_combat_formula_config;
    if (!vm_mysql_query(
            "SELECT panel_defense_base,panel_defense_level_coeff,"
            "panel_defense_endurance_div,panel_hit_base,"
            "panel_strength_hp_coeff_x100,panel_wisdom_mp_coeff_x100,"
            "primary_attack_coeff_tianji_percent,"
            "primary_attack_coeff_huanjian_percent,"
            "primary_attack_coeff_guidao_percent,"
            "soft_armor_k,"
            "equip_armor_divisor,hit_chance_min,hit_chance_max,"
            "crit_damage_percent,resist_cap_percent,"
            "job_skill_attack_bonus_percent,na_variance_percent,"
            "skill_variance_percent,duel_job_bonus_tianji_percent,"
            "duel_job_bonus_huanjian_percent,duel_job_bonus_guidao_percent,"
            "skill_vs_na_floor_percent,monster_hit_default,"
            "monster_dodge_default,monster_crit_default,monster_resist_default,"
            "boss_skill_chance_percent,boss_skill_damage_percent "
            "FROM server_combat_formula_config WHERE id=1 LIMIT 1",
            vm_net_mock_combat_formula_config_row_ctx, &ctx))
    {
        printf("[error][mock-service] combat_formula load failed error=%s\n",
               vm_mysql_last_error());
        g_vm_net_mock_combat_formula_config_loaded = true;
        return false;
    }
    if (!ctx.loaded)
        vm_net_mock_combat_formula_config_set_defaults(
            &g_vm_net_mock_combat_formula_config);
    vm_net_mock_combat_formula_config_sanitize(
        &g_vm_net_mock_combat_formula_config);
    g_vm_net_mock_combat_formula_config_loaded = true;
    printf("[info][mock-service] combat_formula_config_load soft_k=%u "
           "armor_div=%u crit%%=%u resist_cap=%u hit=%u..%u "
           "primary_atk_coeff=%u/%u/%u str_hp_x100=%u wis_mp_x100=%u\n",
           g_vm_net_mock_combat_formula_config.softArmorK,
           g_vm_net_mock_combat_formula_config.equipArmorDivisor,
           g_vm_net_mock_combat_formula_config.critDamagePercent,
           g_vm_net_mock_combat_formula_config.resistCapPercent,
           g_vm_net_mock_combat_formula_config.hitChanceMin,
           g_vm_net_mock_combat_formula_config.hitChanceMax,
           g_vm_net_mock_combat_formula_config.primaryAttackCoeffTianjiPercent,
           g_vm_net_mock_combat_formula_config.primaryAttackCoeffHuanjianPercent,
           g_vm_net_mock_combat_formula_config.primaryAttackCoeffGuidaoPercent,
           g_vm_net_mock_combat_formula_config.panelStrengthHpCoeffX100,
           g_vm_net_mock_combat_formula_config.panelWisdomMpCoeffX100);
    return true;
}

static const vm_net_mock_combat_formula_config *
vm_net_mock_combat_formula_config_get(void)
{
    (void)vm_net_mock_combat_formula_config_load(false);
    return &g_vm_net_mock_combat_formula_config;
}

static bool vm_net_mock_combat_formula_admin_save(
    const vm_net_mock_combat_formula_config *src)
{
    vm_net_mock_combat_formula_config cfg;
    char query[3072];
    int written;

    if (src == NULL)
        return false;
    cfg = *src;
    vm_net_mock_combat_formula_config_sanitize(&cfg);
    if (!vm_net_mock_combat_formula_ensure_table())
        return false;
    written = snprintf(
        query, sizeof(query),
        "INSERT INTO server_combat_formula_config ("
        "id,panel_defense_base,panel_defense_level_coeff,"
        "panel_defense_endurance_div,panel_hit_base,"
        "panel_strength_hp_coeff_x100,panel_wisdom_mp_coeff_x100,"
        "primary_attack_coeff_tianji_percent,"
        "primary_attack_coeff_huanjian_percent,"
        "primary_attack_coeff_guidao_percent,"
        "soft_armor_k,"
        "equip_armor_divisor,hit_chance_min,hit_chance_max,crit_damage_percent,"
        "resist_cap_percent,job_skill_attack_bonus_percent,na_variance_percent,"
        "skill_variance_percent,duel_job_bonus_tianji_percent,"
        "duel_job_bonus_huanjian_percent,duel_job_bonus_guidao_percent,"
        "skill_vs_na_floor_percent,monster_hit_default,monster_dodge_default,"
        "monster_crit_default,monster_resist_default,boss_skill_chance_percent,"
        "boss_skill_damage_percent) VALUES ("
        "1,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
        "%u,%u,%u,%u,%u) "
        "ON DUPLICATE KEY UPDATE "
        "panel_defense_base=VALUES(panel_defense_base),"
        "panel_defense_level_coeff=VALUES(panel_defense_level_coeff),"
        "panel_defense_endurance_div=VALUES(panel_defense_endurance_div),"
        "panel_hit_base=VALUES(panel_hit_base),"
        "panel_strength_hp_coeff_x100=VALUES(panel_strength_hp_coeff_x100),"
        "panel_wisdom_mp_coeff_x100=VALUES(panel_wisdom_mp_coeff_x100),"
        "primary_attack_coeff_tianji_percent="
        "VALUES(primary_attack_coeff_tianji_percent),"
        "primary_attack_coeff_huanjian_percent="
        "VALUES(primary_attack_coeff_huanjian_percent),"
        "primary_attack_coeff_guidao_percent="
        "VALUES(primary_attack_coeff_guidao_percent),"
        "soft_armor_k=VALUES(soft_armor_k),"
        "equip_armor_divisor=VALUES(equip_armor_divisor),"
        "hit_chance_min=VALUES(hit_chance_min),"
        "hit_chance_max=VALUES(hit_chance_max),"
        "crit_damage_percent=VALUES(crit_damage_percent),"
        "resist_cap_percent=VALUES(resist_cap_percent),"
        "job_skill_attack_bonus_percent=VALUES(job_skill_attack_bonus_percent),"
        "na_variance_percent=VALUES(na_variance_percent),"
        "skill_variance_percent=VALUES(skill_variance_percent),"
        "duel_job_bonus_tianji_percent=VALUES(duel_job_bonus_tianji_percent),"
        "duel_job_bonus_huanjian_percent=VALUES(duel_job_bonus_huanjian_percent),"
        "duel_job_bonus_guidao_percent=VALUES(duel_job_bonus_guidao_percent),"
        "skill_vs_na_floor_percent=VALUES(skill_vs_na_floor_percent),"
        "monster_hit_default=VALUES(monster_hit_default),"
        "monster_dodge_default=VALUES(monster_dodge_default),"
        "monster_crit_default=VALUES(monster_crit_default),"
        "monster_resist_default=VALUES(monster_resist_default),"
        "boss_skill_chance_percent=VALUES(boss_skill_chance_percent),"
        "boss_skill_damage_percent=VALUES(boss_skill_damage_percent)",
        cfg.panelDefenseBase, cfg.panelDefenseLevelCoeff,
        cfg.panelDefenseEnduranceDiv, cfg.panelHitBase,
        cfg.panelStrengthHpCoeffX100, cfg.panelWisdomMpCoeffX100,
        cfg.primaryAttackCoeffTianjiPercent,
        cfg.primaryAttackCoeffHuanjianPercent,
        cfg.primaryAttackCoeffGuidaoPercent, cfg.softArmorK,
        cfg.equipArmorDivisor, cfg.hitChanceMin, cfg.hitChanceMax,
        cfg.critDamagePercent, cfg.resistCapPercent,
        cfg.jobSkillAttackBonusPercent, cfg.naVariancePercent,
        cfg.skillVariancePercent, cfg.duelJobBonusTianjiPercent,
        cfg.duelJobBonusHuanjianPercent, cfg.duelJobBonusGuidaoPercent,
        cfg.skillVsNaFloorPercent, cfg.monsterHitDefault,
        cfg.monsterDodgeDefault, cfg.monsterCritDefault,
        cfg.monsterResistDefault, cfg.bossSkillChancePercent,
        cfg.bossSkillDamagePercent);
    if (written < 0 || (size_t)written >= sizeof(query))
    {
        printf("[error][mock-service] combat_formula save query truncated "
               "written=%d cap=%u\n",
               written, (u32)sizeof(query));
        return false;
    }
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-service] combat_formula save failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_combat_formula_config_loaded = false;
    return vm_net_mock_combat_formula_config_load(true);
}
