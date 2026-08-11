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

static bool vm_net_mock_enhance_wash_roll_attr(u8 *typeOut, u8 *flagOut,
                                               u16 *valueOut)
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
        u32 span;
        u32 value;

        if (!row->enabled || row->weight == 0)
            continue;
        cursor += row->weight;
        if (pick >= cursor)
            continue;
        span = row->valueMax - row->valueMin;
        value = row->valueMin;
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
