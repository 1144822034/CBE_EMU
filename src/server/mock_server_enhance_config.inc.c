/* Enhance milestone config + washable attr pool (MySQL-backed).
 * Included from mock_server_catalog.c after chest-reward helpers. */

enum
{
    VM_NET_MOCK_ENHANCE_MILESTONE_CFG_MAX = 32, /* 8 slots × 4 unlocks */
    VM_NET_MOCK_ENHANCE_ATTR_POOL_MAX = 32
};

typedef struct
{
    u8 equipSlot;
    u8 unlockLevel;
    u8 attrType;
    u8 flag;
    u8 enabled;
} vm_net_mock_enhance_milestone_cfg;

typedef struct
{
    u8 attrType;
    u8 flag;
    u32 valueMin;
    u32 valueMax;
    u32 weight;
    u8 enabled;
} vm_net_mock_enhance_attr_pool_cfg;

static vm_net_mock_enhance_milestone_cfg
    g_vm_net_mock_enhance_milestones[VM_NET_MOCK_ENHANCE_MILESTONE_CFG_MAX];
static u32 g_vm_net_mock_enhance_milestone_count = 0;
static bool g_vm_net_mock_enhance_milestone_db_loaded = false;
static bool g_vm_net_mock_enhance_milestone_db_valid = false;

static vm_net_mock_enhance_attr_pool_cfg
    g_vm_net_mock_enhance_attr_pool[VM_NET_MOCK_ENHANCE_ATTR_POOL_MAX];
static u32 g_vm_net_mock_enhance_attr_pool_count = 0;
static bool g_vm_net_mock_enhance_attr_pool_db_loaded = false;
static bool g_vm_net_mock_enhance_attr_pool_db_valid = false;

/* Built-in defaults matching legacy static slot tables. */
static const vm_net_mock_enhance_milestone_cfg
    g_vm_net_mock_enhance_milestone_builtin[] = {
        /* weapon 0 */
        {0, 4, VM_NET_MOCK_EQUIP_ATTR_CRIT, 0, 1},
        {0, 8, VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0, 1},
        {0, 12, VM_NET_MOCK_EQUIP_ATTR_HP, 0, 1},
        {0, 16, VM_NET_MOCK_EQUIP_ATTR_STRENGTH, 0, 1},
        /* helm 1 */
        {1, 4, VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0, 1},
        {1, 8, VM_NET_MOCK_EQUIP_ATTR_HP, 0, 1},
        {1, 12, VM_NET_MOCK_EQUIP_ATTR_MP, 1, 1},
        {1, 16, VM_NET_MOCK_EQUIP_ATTR_WISDOM, 0, 1},
        /* chest 2 */
        {2, 4, VM_NET_MOCK_EQUIP_ATTR_CRIT, 0, 1},
        {2, 8, VM_NET_MOCK_EQUIP_ATTR_HP, 1, 1},
        {2, 12, VM_NET_MOCK_EQUIP_ATTR_MP, 1, 1},
        {2, 16, VM_NET_MOCK_EQUIP_ATTR_STRENGTH, 0, 1},
        /* cloak 3 */
        {3, 4, VM_NET_MOCK_EQUIP_ATTR_CRIT, 0, 1},
        {3, 8, VM_NET_MOCK_EQUIP_ATTR_HIT, 0, 1},
        {3, 12, VM_NET_MOCK_EQUIP_ATTR_DODGE, 0, 1},
        {3, 16, VM_NET_MOCK_EQUIP_ATTR_AGILITY, 0, 1},
        /* belt 4 */
        {4, 4, VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0, 1},
        {4, 8, VM_NET_MOCK_EQUIP_ATTR_MP, 0, 1},
        {4, 12, VM_NET_MOCK_EQUIP_ATTR_HP, 1, 1},
        {4, 16, VM_NET_MOCK_EQUIP_ATTR_STRENGTH, 0, 1},
        /* legs 5 */
        {5, 4, VM_NET_MOCK_EQUIP_ATTR_ATTACK, 0, 1},
        {5, 8, VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0, 1},
        {5, 12, VM_NET_MOCK_EQUIP_ATTR_HIT, 0, 1},
        {5, 16, VM_NET_MOCK_EQUIP_ATTR_HP, 0, 1},
        /* boots 6 */
        {6, 4, VM_NET_MOCK_EQUIP_ATTR_DODGE, 0, 1},
        {6, 8, VM_NET_MOCK_EQUIP_ATTR_HP, 0, 1},
        {6, 12, VM_NET_MOCK_EQUIP_ATTR_HIT, 0, 1},
        {6, 16, VM_NET_MOCK_EQUIP_ATTR_AGILITY, 0, 1},
        /* ring 7 */
        {7, 4, VM_NET_MOCK_EQUIP_ATTR_ATTACK, 0, 1},
        {7, 8, VM_NET_MOCK_EQUIP_ATTR_HP, 0, 1},
        {7, 12, VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0, 1},
        {7, 16, VM_NET_MOCK_EQUIP_ATTR_WISDOM, 0, 1},
};

/* Wash pool seed: quality table low..high spans. */
static const vm_net_mock_enhance_attr_pool_cfg
    g_vm_net_mock_enhance_attr_pool_builtin[] = {
        {VM_NET_MOCK_EQUIP_ATTR_STRENGTH, 0, 120, 600, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_AGILITY, 0, 120, 600, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_WISDOM, 0, 120, 600, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_ATTACK, 0, 23, 296, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0, 87, 350, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_DODGE, 0, 1, 5, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_HIT, 0, 1, 5, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_CRIT, 0, 1, 5, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_HP, 0, 150, 600, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_MP, 0, 150, 600, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_HP, 1, 5, 25, 1, 1},
        {VM_NET_MOCK_EQUIP_ATTR_MP, 1, 5, 25, 1, 1},
};

static void vm_net_mock_enhance_config_fill_builtin_milestones(void)
{
    u32 n = (u32)(sizeof(g_vm_net_mock_enhance_milestone_builtin) /
                  sizeof(g_vm_net_mock_enhance_milestone_builtin[0]));
    if (n > VM_NET_MOCK_ENHANCE_MILESTONE_CFG_MAX)
        n = VM_NET_MOCK_ENHANCE_MILESTONE_CFG_MAX;
    memcpy(g_vm_net_mock_enhance_milestones,
           g_vm_net_mock_enhance_milestone_builtin,
           n * sizeof(g_vm_net_mock_enhance_milestones[0]));
    g_vm_net_mock_enhance_milestone_count = n;
}

static void vm_net_mock_enhance_config_fill_builtin_pool(void)
{
    u32 n = (u32)(sizeof(g_vm_net_mock_enhance_attr_pool_builtin) /
                  sizeof(g_vm_net_mock_enhance_attr_pool_builtin[0]));
    if (n > VM_NET_MOCK_ENHANCE_ATTR_POOL_MAX)
        n = VM_NET_MOCK_ENHANCE_ATTR_POOL_MAX;
    memcpy(g_vm_net_mock_enhance_attr_pool,
           g_vm_net_mock_enhance_attr_pool_builtin,
           n * sizeof(g_vm_net_mock_enhance_attr_pool[0]));
    g_vm_net_mock_enhance_attr_pool_count = n;
}

static bool vm_net_mock_enhance_milestone_db_row(void *context_value,
                                                 unsigned int column_count,
                                                 const char *const *values,
                                                 const size_t *lengths)
{
    u32 *loaded = (u32 *)context_value;
    u32 slot = 0, unlock = 0, type = 0, flag = 0, enabled = 1;
    vm_net_mock_enhance_milestone_cfg *dst;

    (void)lengths;
    if (column_count < 5 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &slot) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &unlock) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &type) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &flag) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &enabled) ||
        slot > 7 ||
        (unlock != 4 && unlock != 8 && unlock != 12 && unlock != 16) ||
        type < 1 || type > 10 || flag > 1)
    {
        return true;
    }
    if (g_vm_net_mock_enhance_milestone_count >=
        VM_NET_MOCK_ENHANCE_MILESTONE_CFG_MAX)
        return true;
    dst = &g_vm_net_mock_enhance_milestones[g_vm_net_mock_enhance_milestone_count++];
    memset(dst, 0, sizeof(*dst));
    dst->equipSlot = (u8)slot;
    dst->unlockLevel = (u8)unlock;
    dst->attrType = (u8)type;
    dst->flag = (u8)flag;
    dst->enabled = enabled != 0 ? 1 : 0;
    if (loaded != NULL)
        ++(*loaded);
    return true;
}

static bool vm_net_mock_enhance_attr_pool_db_row(void *context_value,
                                                 unsigned int column_count,
                                                 const char *const *values,
                                                 const size_t *lengths)
{
    u32 *loaded = (u32 *)context_value;
    u32 type = 0, flag = 0, vmin = 0, vmax = 0, weight = 1, enabled = 1;
    vm_net_mock_enhance_attr_pool_cfg *dst;

    (void)lengths;
    if (column_count < 6 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &type) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &flag) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &vmin) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &vmax) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &weight) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &enabled) ||
        type < 1 || type > 10 || flag > 1 || vmin > 65535u || vmax > 65535u ||
        vmax < vmin || weight == 0)
    {
        return true;
    }
    if (g_vm_net_mock_enhance_attr_pool_count >= VM_NET_MOCK_ENHANCE_ATTR_POOL_MAX)
        return true;
    dst = &g_vm_net_mock_enhance_attr_pool[g_vm_net_mock_enhance_attr_pool_count++];
    memset(dst, 0, sizeof(*dst));
    dst->attrType = (u8)type;
    dst->flag = (u8)flag;
    dst->valueMin = vmin;
    dst->valueMax = vmax;
    dst->weight = weight;
    dst->enabled = enabled != 0 ? 1 : 0;
    if (loaded != NULL)
        ++(*loaded);
    return true;
}

static bool vm_net_mock_enhance_config_seed_milestones_if_empty(void)
{
    u32 i;
    char query[384];

    for (i = 0; i < (u32)(sizeof(g_vm_net_mock_enhance_milestone_builtin) /
                          sizeof(g_vm_net_mock_enhance_milestone_builtin[0]));
         ++i)
    {
        const vm_net_mock_enhance_milestone_cfg *row =
            &g_vm_net_mock_enhance_milestone_builtin[i];
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO server_enhance_milestones"
                 "(equip_slot,unlock_level,attr_type,flag,enabled) "
                 "VALUES(%u,%u,%u,%u,%u)",
                 row->equipSlot, row->unlockLevel, row->attrType, row->flag,
                 row->enabled);
        if (!vm_mysql_exec(query))
            return false;
    }
    return true;
}

static bool vm_net_mock_enhance_config_seed_pool_if_empty(void)
{
    u32 i;
    char query[384];

    for (i = 0; i < (u32)(sizeof(g_vm_net_mock_enhance_attr_pool_builtin) /
                          sizeof(g_vm_net_mock_enhance_attr_pool_builtin[0]));
         ++i)
    {
        const vm_net_mock_enhance_attr_pool_cfg *row =
            &g_vm_net_mock_enhance_attr_pool_builtin[i];
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO server_enhance_attr_pool"
                 "(attr_type,flag,value_min,value_max,weight,enabled) "
                 "VALUES(%u,%u,%u,%u,%u,%u)",
                 row->attrType, row->flag, row->valueMin, row->valueMax,
                 row->weight, row->enabled);
        if (!vm_mysql_exec(query))
            return false;
    }
    return true;
}

static bool vm_net_mock_enhance_config_db_load(void)
{
    u32 milestoneLoaded = 0;
    u32 poolLoaded = 0;

    if (g_vm_net_mock_enhance_milestone_db_loaded &&
        g_vm_net_mock_enhance_attr_pool_db_loaded)
    {
        return g_vm_net_mock_enhance_milestone_db_valid &&
               g_vm_net_mock_enhance_attr_pool_db_valid;
    }

    g_vm_net_mock_enhance_milestone_db_loaded = true;
    g_vm_net_mock_enhance_attr_pool_db_loaded = true;
    g_vm_net_mock_enhance_milestone_db_valid = false;
    g_vm_net_mock_enhance_attr_pool_db_valid = false;
    g_vm_net_mock_enhance_milestone_count = 0;
    g_vm_net_mock_enhance_attr_pool_count = 0;

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_enhance_milestones ("
            "equip_slot TINYINT UNSIGNED NOT NULL,"
            "unlock_level TINYINT UNSIGNED NOT NULL,"
            "attr_type TINYINT UNSIGNED NOT NULL,"
            "flag TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(equip_slot,unlock_level)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_enhance_attr_pool ("
            "attr_type TINYINT UNSIGNED NOT NULL,"
            "flag TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "value_min INT UNSIGNED NOT NULL DEFAULT 1,"
            "value_max INT UNSIGNED NOT NULL DEFAULT 1,"
            "weight INT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(attr_type,flag)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
    {
        printf("[error][mock-admin] enhance_config create failed error=%s\n",
               vm_mysql_last_error());
        vm_net_mock_enhance_config_fill_builtin_milestones();
        vm_net_mock_enhance_config_fill_builtin_pool();
        return false;
    }

    (void)vm_net_mock_enhance_config_seed_milestones_if_empty();
    (void)vm_net_mock_enhance_config_seed_pool_if_empty();

    if (!vm_mysql_query(
            "SELECT equip_slot,unlock_level,attr_type,flag,enabled "
            "FROM server_enhance_milestones ORDER BY equip_slot,unlock_level",
            vm_net_mock_enhance_milestone_db_row, &milestoneLoaded) ||
        !vm_mysql_query(
            "SELECT attr_type,flag,value_min,value_max,weight,enabled "
            "FROM server_enhance_attr_pool ORDER BY attr_type,flag",
            vm_net_mock_enhance_attr_pool_db_row, &poolLoaded))
    {
        printf("[error][mock-admin] enhance_config load failed error=%s\n",
               vm_mysql_last_error());
        vm_net_mock_enhance_config_fill_builtin_milestones();
        vm_net_mock_enhance_config_fill_builtin_pool();
        return false;
    }

    if (g_vm_net_mock_enhance_milestone_count == 0)
        vm_net_mock_enhance_config_fill_builtin_milestones();
    if (g_vm_net_mock_enhance_attr_pool_count == 0)
        vm_net_mock_enhance_config_fill_builtin_pool();

    g_vm_net_mock_enhance_milestone_db_valid = true;
    g_vm_net_mock_enhance_attr_pool_db_valid = true;
    printf("[info][mock-admin] enhance_config_db_load milestones=%u pool=%u\n",
           g_vm_net_mock_enhance_milestone_count,
           g_vm_net_mock_enhance_attr_pool_count);
    return true;
}

static bool vm_net_mock_enhance_config_admin_reload(void)
{
    g_vm_net_mock_enhance_milestone_db_loaded = false;
    g_vm_net_mock_enhance_attr_pool_db_loaded = false;
    return vm_net_mock_enhance_config_db_load();
}

static bool vm_net_mock_enhance_milestone_lookup(u8 equipSlot, u8 unlockLevel,
                                                u8 *typeOut, u8 *flagOut)
{
    u32 i;

    (void)vm_net_mock_enhance_config_db_load();
    for (i = 0; i < g_vm_net_mock_enhance_milestone_count; ++i)
    {
        const vm_net_mock_enhance_milestone_cfg *row =
            &g_vm_net_mock_enhance_milestones[i];
        if (row->equipSlot == equipSlot && row->unlockLevel == unlockLevel &&
            row->enabled)
        {
            if (typeOut)
                *typeOut = row->attrType;
            if (flagOut)
                *flagOut = row->flag;
            return true;
        }
    }
    return false;
}

static u32 vm_net_mock_enhance_attr_pool_admin_count(void)
{
    (void)vm_net_mock_enhance_config_db_load();
    return g_vm_net_mock_enhance_attr_pool_count;
}

static const vm_net_mock_enhance_attr_pool_cfg *
vm_net_mock_enhance_attr_pool_admin_row_at(u32 index)
{
    (void)vm_net_mock_enhance_config_db_load();
    if (index >= g_vm_net_mock_enhance_attr_pool_count)
        return NULL;
    return &g_vm_net_mock_enhance_attr_pool[index];
}

static u32 vm_net_mock_enhance_milestone_admin_count(void)
{
    (void)vm_net_mock_enhance_config_db_load();
    return g_vm_net_mock_enhance_milestone_count;
}

static const vm_net_mock_enhance_milestone_cfg *
vm_net_mock_enhance_milestone_admin_row_at(u32 index)
{
    (void)vm_net_mock_enhance_config_db_load();
    if (index >= g_vm_net_mock_enhance_milestone_count)
        return NULL;
    return &g_vm_net_mock_enhance_milestones[index];
}

static bool vm_net_mock_enhance_milestone_admin_upsert(u8 equipSlot,
                                                       u8 unlockLevel,
                                                       u8 attrType, u8 flag,
                                                       u8 enabled)
{
    char query[384];

    if (equipSlot > 7 ||
        (unlockLevel != 4 && unlockLevel != 8 && unlockLevel != 12 &&
         unlockLevel != 16) ||
        attrType < 1 || attrType > 10 || flag > 1)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_enhance_milestones"
             "(equip_slot,unlock_level,attr_type,flag,enabled) "
             "VALUES(%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "attr_type=VALUES(attr_type),flag=VALUES(flag),"
             "enabled=VALUES(enabled)",
             equipSlot, unlockLevel, attrType, flag, enabled ? 1u : 0u);
    if (!vm_mysql_exec(query))
        return false;
    return vm_net_mock_enhance_config_admin_reload();
}

static bool vm_net_mock_enhance_attr_pool_admin_upsert_row(
    u8 attrType, u8 flag, u32 valueMin, u32 valueMax, u32 weight, u8 enabled)
{
    char query[384];

    if (attrType < 1 || attrType > 10 || flag > 1 || valueMax < valueMin ||
        valueMax > 65535u || weight == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_enhance_attr_pool"
             "(attr_type,flag,value_min,value_max,weight,enabled) "
             "VALUES(%u,%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "value_min=VALUES(value_min),value_max=VALUES(value_max),"
             "weight=VALUES(weight),enabled=VALUES(enabled)",
             attrType, flag, valueMin, valueMax, weight, enabled ? 1u : 0u);
    return vm_mysql_exec(query);
}

static bool vm_net_mock_enhance_attr_pool_admin_upsert(u8 attrType, u8 flag,
                                                       u32 valueMin,
                                                       u32 valueMax,
                                                       u32 weight, u8 enabled)
{
    if (!vm_net_mock_enhance_attr_pool_admin_upsert_row(
            attrType, flag, valueMin, valueMax, weight, enabled))
        return false;
    return vm_net_mock_enhance_config_admin_reload();
}

/* Batch save pool rows then reload once (admin 一键保存全部). */
static bool vm_net_mock_enhance_attr_pool_admin_save_all(
    const vm_net_mock_enhance_attr_pool_cfg *rows, u32 rowCount)
{
    u32 i;

    if (rows == NULL || rowCount == 0)
        return false;
    for (i = 0; i < rowCount; ++i)
    {
        if (!vm_net_mock_enhance_attr_pool_admin_upsert_row(
                rows[i].attrType, rows[i].flag, rows[i].valueMin,
                rows[i].valueMax, rows[i].weight, rows[i].enabled))
            return false;
    }
    return vm_net_mock_enhance_config_admin_reload();
}

/* Dedicated wash RNG — do not use libc rand() or a tick-only formula.
 * Consecutive washes on the same equip+tier must keep mixing entropy. */
static u32 g_vm_net_mock_enhance_wash_rng = 0;

static void vm_net_mock_enhance_wash_rand_mix(u32 entropy)
{
    g_vm_net_mock_enhance_wash_rng ^= entropy + 0x9e3779b9u;
    if (g_vm_net_mock_enhance_wash_rng == 0)
        g_vm_net_mock_enhance_wash_rng = 0x85ebca6bu;
}

static u32 vm_net_mock_enhance_wash_rand_u32(void)
{
    if (g_vm_net_mock_enhance_wash_rng == 0)
    {
        g_vm_net_mock_enhance_wash_rng =
            0xa5a5a5a5u ^ (g_schedulerTick * 1664525u) ^
            ((u32)time(NULL) * 1013904223u);
        if (g_vm_net_mock_enhance_wash_rng == 0)
            g_vm_net_mock_enhance_wash_rng = 0x9e3779b9u;
    }
    g_vm_net_mock_enhance_wash_rng ^= g_vm_net_mock_enhance_wash_rng << 13;
    g_vm_net_mock_enhance_wash_rng ^= g_vm_net_mock_enhance_wash_rng >> 17;
    g_vm_net_mock_enhance_wash_rng ^= g_vm_net_mock_enhance_wash_rng << 5;
    if (g_vm_net_mock_enhance_wash_rng == 0)
        g_vm_net_mock_enhance_wash_rng = 0x9e3779b9u;
    return g_vm_net_mock_enhance_wash_rng;
}

static u32 vm_net_mock_enhance_wash_rand_below(u32 bound)
{
    if (bound == 0)
        return 0;
    return vm_net_mock_enhance_wash_rand_u32() % bound;
}

/*
 * Pool valueMax scaled by equip.dsh 品质 (5 tiers 0..4):
 *   Q0 → 1/5, Q1 → 2/5, Q2 → 3/5, Q3 → 4/5, Q4+ → 5/5.
 * valueMin is unchanged; if min > scaled max, roll collapses to scaled max.
 */
static u32 vm_net_mock_enhance_wash_pool_scaled_max(u32 valueMax, u8 quality)
{
    u8 q = quality;

    if (q > 4)
        q = 4;
    return (valueMax * ((u32)q + 1u)) / 5u;
}

static bool vm_net_mock_enhance_wash_roll_attr(u8 quality, u8 *typeOut,
                                               u8 *flagOut, u16 *valueOut)
{
    u32 i;
    u32 totalWeight = 0;
    u32 pick;
    u32 cursor = 0;

    (void)vm_net_mock_enhance_config_db_load();
    for (i = 0; i < g_vm_net_mock_enhance_attr_pool_count; ++i)
    {
        const vm_net_mock_enhance_attr_pool_cfg *row =
            &g_vm_net_mock_enhance_attr_pool[i];
        if (!row->enabled || row->weight == 0)
            continue;
        if (totalWeight > 0xffffffffu - row->weight)
            totalWeight = 0xffffffffu;
        else
            totalWeight += row->weight;
    }
    if (totalWeight == 0)
        return false;
    pick = vm_net_mock_enhance_wash_rand_below(totalWeight);
    for (i = 0; i < g_vm_net_mock_enhance_attr_pool_count; ++i)
    {
        const vm_net_mock_enhance_attr_pool_cfg *row =
            &g_vm_net_mock_enhance_attr_pool[i];
        u32 vmax;
        u32 vmin;
        u32 span;
        u32 value;

        if (!row->enabled || row->weight == 0)
            continue;
        cursor += row->weight;
        if (pick >= cursor)
            continue;
        vmax = vm_net_mock_enhance_wash_pool_scaled_max(row->valueMax, quality);
        vmin = row->valueMin;
        if (vmin > vmax)
            vmin = vmax;
        span = vmax - vmin;
        value = vmin;
        if (span > 0)
            value += vm_net_mock_enhance_wash_rand_below(span + 1u);
        if (value > 65535u)
            value = 65535u;
        if (typeOut)
            *typeOut = row->attrType;
        if (flagOut)
            *flagOut = row->flag;
        if (valueOut)
            *valueOut = (u16)value;
        return true;
    }
    return false;
}

/*
 * First obtain of an equipment instance:
 *   20% — roll +4/+8/+12/+16 from the wash attr pool (washed=1, washCount=0)
 *   80% — keep empty wash → resolve uses config default + quality formula
 * Call only when no inherited wash pointer is supplied (trade/equip/warehouse/
 * auction must pass existing wash and must not re-roll).
 */
static bool vm_net_mock_enhance_wash_seed_initial(
    vm_net_mock_enhance_wash_state *wash, u32 itemId, u32 instanceId, u16 seq)
{
    u8 i;
    u8 rolled = 0;
    u8 quality = 0;
    const vm_net_mock_equipment_catalog_item *equip =
        vm_net_mock_find_equipment_catalog_item(itemId);

    if (wash == NULL)
        return false;
    if (equip != NULL)
        quality = equip->quality;
    vm_net_mock_enhance_wash_clear(wash);
    vm_net_mock_enhance_wash_rand_mix(
        itemId ^ (instanceId * 0x85ebca6bu) ^ ((u32)seq << 16) ^
        ((u32)time(NULL)) ^ (g_schedulerTick * 1664525u) ^
        ((u32)quality << 24));
    /* 0..19 → seed (20%); 20..99 → default config (80%). */
    if (vm_net_mock_enhance_wash_rand_below(100u) >= 20u)
        return false;
    for (i = 0; i < VM_NET_MOCK_ENHANCE_WASH_SLOT_COUNT; ++i)
    {
        u8 type = 0;
        u8 flag = 0;
        u16 value = 0;

        vm_net_mock_enhance_wash_rand_mix(((u32)i + 1u) * 0x9e3779b9u ^ itemId ^
                                          instanceId ^ ((u32)seq << 8));
        if (!vm_net_mock_enhance_wash_roll_attr(quality, &type, &flag, &value))
            continue;
        wash->slots[i].washed = 1;
        wash->slots[i].type = type;
        wash->slots[i].flag = flag;
        wash->slots[i].value = value;
        wash->slots[i].washCount = 0;
        rolled++;
    }
    return rolled > 0;
}

static u32 vm_net_mock_enhance_wash_next_gold(u16 washCount)
{
    /* Nth wash on same equip+tier costs 5*N 金 (N = washCount+1). */
    u32 n = (u32)washCount + 1u;
    return n * 5u;
}

static u32 vm_net_mock_enhance_wash_next_copper(u16 washCount)
{
    return vm_net_mock_enhance_wash_next_gold(washCount) *
           (u32)VM_NET_MOCK_MONEY_COPPER_PER_GOLD;
}

/* Resolve type/flag/value for one milestone line (default or washed). */
static bool vm_net_mock_equip_enhance_resolve_milestone(
    const vm_net_mock_equipment_catalog_item *item, u8 slotIndex,
    const vm_net_mock_enhance_wash_state *wash, u8 *typeOut, u8 *flagOut,
    u16 *valueOut)
{
    u8 unlock;
    u8 type = 0;
    u8 flag = 0;
    u16 value = 0;

    if (item == NULL || slotIndex >= VM_NET_MOCK_EQUIP_ATTR_MILESTONE_SLOTS)
        return false;
    unlock = vm_net_mock_enhance_unlock_for_index(slotIndex);

    if (wash != NULL && wash->slots[slotIndex].washed != 0 &&
        wash->slots[slotIndex].type >= 1 && wash->slots[slotIndex].type <= 10)
    {
        type = wash->slots[slotIndex].type;
        flag = wash->slots[slotIndex].flag;
        value = wash->slots[slotIndex].value;
        if (typeOut)
            *typeOut = type;
        if (flagOut)
            *flagOut = flag;
        if (valueOut)
            *valueOut = value;
        return true;
    }

    if (!vm_net_mock_enhance_milestone_lookup(item->slot, unlock, &type, &flag))
    {
        /* Fallback: legacy static tables. */
        const vm_net_mock_equip_enhance_attr_def *defs = NULL;
        u8 knownCount = 0;
        defs = vm_net_mock_equip_enhance_attrs_for_slot(item->slot, &knownCount);
        if (defs == NULL || slotIndex >= knownCount)
            return false;
        type = defs[slotIndex].type;
        flag = defs[slotIndex].flag;
    }

    type = vm_net_mock_equip_enhance_milestone_type(item, slotIndex, type);
    value = vm_net_mock_equip_enhance_milestone_value(item, unlock, type, flag);
    if (typeOut)
        *typeOut = type;
    if (flagOut)
        *flagOut = flag;
    if (valueOut)
        *valueOut = value;
    return true;
}

/* -------------------------------------------------------------------------
 * account_role_enhance_wash_record — last wash snapshot keyed by
 * item_instance_id (fallback by bag_seq for legacy rows without instance id).
 * ------------------------------------------------------------------------- */

typedef struct
{
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    bool found;
} vm_net_mock_enhance_wash_record_lookup_ctx;

static bool vm_net_mock_enhance_wash_record_wash_row_cb(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_enhance_wash_record_lookup_ctx *ctx =
        (vm_net_mock_enhance_wash_record_lookup_ctx *)contextValue;

    (void)columnCount;
    if (ctx == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    if (lengths[0] >= sizeof(ctx->washText))
        return false;
    memcpy(ctx->washText, values[0], lengths[0]);
    ctx->washText[lengths[0]] = 0;
    ctx->found = true;
    return true;
}

static bool vm_net_mock_enhance_wash_record_ensure_table(void)
{
    static bool ready = false;

    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_enhance_wash_record ("
            "role_id INT UNSIGNED NOT NULL,"
            "item_instance_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "bag_seq SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "item_id INT UNSIGNED NOT NULL,"
            "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "enhance_wash VARCHAR(96) NOT NULL DEFAULT "
            "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',"
            "unlock_level TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "washed_unix INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(role_id,bag_seq),"
            "KEY idx_role_item_instance(role_id,item_instance_id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"))
        return false;
    (void)vm_mysql_exec(
        "ALTER TABLE account_role_enhance_wash_record ADD COLUMN "
        "item_instance_id INT UNSIGNED NOT NULL DEFAULT 0 AFTER role_id");
    (void)vm_mysql_exec(
        "ALTER TABLE account_role_enhance_wash_record ADD KEY "
        "idx_role_item_instance (role_id,item_instance_id)");
    ready = true;
    return true;
}

static bool vm_net_mock_enhance_wash_record_wash_sql_text(
    const vm_net_mock_enhance_wash_state *wash, char *out, size_t outCap)
{
    size_t i;

    if (out == NULL || outCap < 8)
        return false;
    if (!vm_net_mock_enhance_wash_encode(wash, out, outCap) || out[0] == 0)
    {
        snprintf(out, outCap, "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    }
    for (i = 0; out[i] != 0; ++i)
    {
        char c = out[i];
        if (!((c >= '0' && c <= '9') || c == ',' || c == ';'))
        {
            snprintf(out, outCap, "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
            return false;
        }
    }
    return true;
}

static bool vm_net_mock_enhance_wash_record_upsert(
    u32 roleId, u32 itemInstanceId, u16 bagSeq, u32 itemId, u16 enhanceLevel,
    const vm_net_mock_enhance_wash_state *wash, u8 unlockLevel)
{
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char query[512];
    u32 now = (u32)time(NULL);

    if (roleId == 0 || itemId == 0 || wash == NULL ||
        vm_net_mock_enhance_wash_is_empty(wash))
        return false;
    if (!vm_net_mock_enhance_wash_record_ensure_table())
        return false;
    (void)vm_net_mock_enhance_wash_record_wash_sql_text(wash, washText,
                                                        sizeof(washText));
    if (itemInstanceId != 0)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_enhance_wash_record "
                 "WHERE role_id=%u AND item_instance_id=%u",
                 roleId, itemInstanceId);
        (void)vm_mysql_exec(query);
    }
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_enhance_wash_record "
             "(role_id,item_instance_id,bag_seq,item_id,enhance_level,enhance_wash,"
             "unlock_level,washed_unix) VALUES (%u,%u,%u,%u,%u,'%s',%u,%u) "
             "ON DUPLICATE KEY UPDATE item_instance_id=VALUES(item_instance_id),"
             "item_id=VALUES(item_id),"
             "enhance_level=VALUES(enhance_level),"
             "enhance_wash=VALUES(enhance_wash),"
             "unlock_level=VALUES(unlock_level),"
             "washed_unix=VALUES(washed_unix)",
             roleId, itemInstanceId, (u32)bagSeq, itemId, (u32)enhanceLevel,
             washText, (u32)unlockLevel, now);
    if (!vm_mysql_exec(query))
        return false;
    printf("[info][mock-service] enhance_wash_record_upsert role=%u instance=%u "
           "seq=%u item=%u level=%u unlock=%u evidence=last-wash-snapshot\n",
           roleId, itemInstanceId, (u32)bagSeq, itemId, (u32)enhanceLevel,
           (u32)unlockLevel);
    return true;
}

static bool vm_net_mock_enhance_wash_record_relocate_seq(u32 roleId,
                                                         u32 itemInstanceId,
                                                         u16 oldSeq, u16 newSeq,
                                                         u32 itemId, u16 enhanceLevel,
                                                         const vm_net_mock_enhance_wash_state *wash)
{
    char query[384];

    if (roleId == 0 || newSeq == 0)
        return false;
    if (!vm_net_mock_enhance_wash_record_ensure_table())
        return false;
    if (oldSeq != 0 && oldSeq != newSeq)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_enhance_wash_record "
                 "WHERE role_id=%u AND bag_seq=%u AND item_instance_id=0",
                 roleId, (u32)oldSeq);
        (void)vm_mysql_exec(query);
    }
    return vm_net_mock_enhance_wash_record_upsert(roleId, itemInstanceId, newSeq,
                                                  itemId, enhanceLevel, wash, 0);
}

static bool vm_net_mock_enhance_wash_record_restore(u32 roleId, u32 itemInstanceId,
                                                    u32 itemId, u16 enhanceLevel,
                                                    u16 bagSeq,
                                                    vm_net_mock_enhance_wash_state *washOut)
{
    vm_net_mock_enhance_wash_record_lookup_ctx ctx;
    char query[320];

    (void)itemId;
    (void)enhanceLevel;
    if (roleId == 0 || washOut == NULL || !vm_net_mock_enhance_wash_is_empty(washOut))
        return false;
    if (!vm_net_mock_enhance_wash_record_ensure_table())
        return false;
    memset(&ctx, 0, sizeof(ctx));
    if (itemInstanceId != 0)
    {
        snprintf(query, sizeof(query),
                 "SELECT enhance_wash FROM account_role_enhance_wash_record "
                 "WHERE role_id=%u AND item_instance_id=%u LIMIT 1",
                 roleId, itemInstanceId);
        (void)vm_mysql_query(query, vm_net_mock_enhance_wash_record_wash_row_cb,
                             &ctx);
    }
    if (!ctx.found && bagSeq != 0)
    {
        memset(&ctx, 0, sizeof(ctx));
        snprintf(query, sizeof(query),
                 "SELECT enhance_wash FROM account_role_enhance_wash_record "
                 "WHERE role_id=%u AND bag_seq=%u LIMIT 1",
                 roleId, (u32)bagSeq);
        (void)vm_mysql_query(query, vm_net_mock_enhance_wash_record_wash_row_cb,
                             &ctx);
    }
    if (!ctx.found)
        return false;
    (void)vm_net_mock_enhance_wash_decode(ctx.washText, strlen(ctx.washText),
                                          washOut);
    if (vm_net_mock_enhance_wash_is_empty(washOut))
        return false;
    printf("[info][mock-service] enhance_wash_record_restore role=%u instance=%u "
           "seq=%u item=%u level=%u evidence=fallback-from-record\n",
           roleId, itemInstanceId, (u32)bagSeq, itemId, (u32)enhanceLevel);
    return true;
}

static void vm_net_mock_enhance_wash_coalesce_from_record(
    u32 roleId, u32 itemInstanceId, u32 itemId, u16 enhanceLevel, u16 bagSeq,
    vm_net_mock_enhance_wash_state *wash)
{
    if (wash == NULL || roleId == 0 || itemId == 0)
        return;
    if (!vm_net_mock_enhance_wash_is_empty(wash))
        return;
    (void)vm_net_mock_enhance_wash_record_restore(roleId, itemInstanceId, itemId,
                                                  enhanceLevel, bagSeq, wash);
}
