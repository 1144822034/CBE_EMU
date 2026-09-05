/*
 * Pixi 娱乐「水果机」(WT kind=48): 轮盘/开奖/比大小均在服务端。
 * 布局与 luckyFruit (DareCreate/luckyFruit) 24 格一致；倍率/权重/货币由 MySQL 配置。
 * Included after mock_server_guess.c.
 */

enum
{
    VM_MOCK_FRUIT_WT_KIND = 48,
    VM_MOCK_FRUIT_WT_STATUS = 1,
    VM_MOCK_FRUIT_WT_INSERT = 2,
    VM_MOCK_FRUIT_WT_SPIN = 3,
    VM_MOCK_FRUIT_WT_COLLECT = 4,
    VM_MOCK_FRUIT_WT_COMPARE = 5,
    VM_MOCK_FRUIT_WT_CASH_OUT = 6,
    VM_MOCK_FRUIT_CURRENCY_GOLD = 0,
    VM_MOCK_FRUIT_CURRENCY_KUBAO = 1,
    VM_MOCK_FRUIT_BET_BAR = 1,
    VM_MOCK_FRUIT_BET_SEVEN = 2,
    VM_MOCK_FRUIT_BET_STAR = 3,
    VM_MOCK_FRUIT_BET_WATERMELON = 4,
    VM_MOCK_FRUIT_BET_BELL = 5,
    VM_MOCK_FRUIT_BET_MANGO = 6,
    VM_MOCK_FRUIT_BET_ORANGE = 7,
    VM_MOCK_FRUIT_BET_APPLE = 8,
    VM_MOCK_FRUIT_COMPARE_LOWER = 1,
    VM_MOCK_FRUIT_COMPARE_BIGGER = 2,
    VM_MOCK_FRUIT_WHEEL_SLOTS = 24
};

typedef struct
{
    u8 enabled;
    u8 showEntry;
    u8 currencyType;
    u32 creditsPerUnit;
    u32 minInsert;
    u32 maxInsert;
    u8 bonusChance;
    u8 bonusExtraPercent;
} vm_mock_fruit_config;

typedef struct
{
    u8 fruitId;
    u16 multiplier;
    u16 weight;
} vm_mock_fruit_wheel_slot;

typedef struct
{
    u32 credits;
    u32 winChips;
    bool found;
} vm_mock_fruit_session_row;

static vm_mock_fruit_config g_vm_mock_fruit_config;
static bool g_vm_mock_fruit_config_loaded = false;
static vm_mock_fruit_wheel_slot g_vm_mock_fruit_wheel[VM_MOCK_FRUIT_WHEEL_SLOTS];
static u32 g_vm_mock_fruit_rng = 0;

static const u8 g_vm_mock_fruit_default_fruit[VM_MOCK_FRUIT_WHEEL_SLOTS] = {
    0, 8, 5, 7, 5, 1, 1, 8, 8, 6, 4, 4, 0, 8, 7, 7, 5, 2, 2, 8, 6, 6, 3, 3};
static const u8 g_vm_mock_fruit_default_mult[VM_MOCK_FRUIT_WHEEL_SLOTS] = {
    0, 3, 3, 10, 10, 50, 100, 3, 5, 10, 10, 5, 0, 3, 5, 10, 10, 30, 40, 3, 5, 10, 30, 40};
static const u16 g_vm_mock_fruit_default_weight[VM_MOCK_FRUIT_WHEEL_SLOTS] = {
    28, 90, 70, 75, 55, 18, 6, 85, 65, 55, 50, 45, 28, 85, 50, 45, 45, 14, 9, 85, 42, 42, 12, 9};

static void vm_mock_fruit_apply_defaults(void)
{
    u32 i;

    memset(&g_vm_mock_fruit_config, 0, sizeof(g_vm_mock_fruit_config));
    g_vm_mock_fruit_config.enabled = 1;
    g_vm_mock_fruit_config.showEntry = 1;
    g_vm_mock_fruit_config.currencyType = VM_MOCK_FRUIT_CURRENCY_GOLD;
    g_vm_mock_fruit_config.creditsPerUnit = 10;
    g_vm_mock_fruit_config.minInsert = 1;
    g_vm_mock_fruit_config.maxInsert = 999;
    g_vm_mock_fruit_config.bonusChance = 8;
    g_vm_mock_fruit_config.bonusExtraPercent = 50;
    for (i = 0; i < VM_MOCK_FRUIT_WHEEL_SLOTS; ++i)
    {
        g_vm_mock_fruit_wheel[i].fruitId = g_vm_mock_fruit_default_fruit[i];
        g_vm_mock_fruit_wheel[i].multiplier = g_vm_mock_fruit_default_mult[i];
        g_vm_mock_fruit_wheel[i].weight = g_vm_mock_fruit_default_weight[i];
    }
}

static bool vm_mock_fruit_seed_default_wheel_rows(void)
{
    u32 i;
    char query[256];

    for (i = 0; i < VM_MOCK_FRUIT_WHEEL_SLOTS; ++i)
    {
        snprintf(
            query, sizeof(query),
            "INSERT IGNORE INTO server_fruit_wheel "
            "(slot_index,fruit_id,multiplier,weight) VALUES (%u,%u,%u,%u)",
            i, (u32)g_vm_mock_fruit_default_fruit[i],
            (u32)g_vm_mock_fruit_default_mult[i],
            (u32)g_vm_mock_fruit_default_weight[i]);
        if (!vm_mysql_exec(query))
            return false;
    }
    return true;
}

static bool vm_mock_fruit_config_row(void *contextValue, unsigned int columnCount,
                                      const char *const *values, const size_t *lengths)
{
    vm_mock_fruit_config *cfg = (vm_mock_fruit_config *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (cfg == NULL || values == NULL || lengths == NULL)
        return false;
    if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        cfg->enabled = (u8)tmp;
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        cfg->showEntry = (u8)tmp;
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        cfg->currencyType = (u8)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        cfg->creditsPerUnit = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        cfg->minInsert = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        cfg->maxInsert = tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        cfg->bonusChance = (u8)tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        cfg->bonusExtraPercent = (u8)tmp;
    return true;
}

static bool vm_mock_fruit_wheel_row(void *contextValue, unsigned int columnCount,
                                    const char *const *values, const size_t *lengths)
{
    u32 slot = 0;
    u32 tmp = 0;
    vm_mock_fruit_wheel_slot *ws;

    (void)contextValue;
    (void)columnCount;
    if (values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &slot) ||
        slot >= VM_MOCK_FRUIT_WHEEL_SLOTS)
        return false;
    ws = &g_vm_mock_fruit_wheel[slot];
    ws->fruitId = 0;
    ws->multiplier = 0;
    ws->weight = 1;
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        ws->fruitId = (u8)tmp;
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        ws->multiplier = (u16)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        ws->weight = (u16)(tmp ? tmp : 1u);
    return true;
}

static bool vm_mock_fruit_u32_row(void *contextValue, unsigned int columnCount,
                                  const char *const *values, const size_t *lengths)
{
    u32 *out = (u32 *)contextValue;

    (void)columnCount;
    if (out == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    return vm_mock_mysql_parse_u32(values[0], lengths[0], out);
}

static bool vm_mock_fruit_ensure_tables(void)
{
    static bool ready = false;

    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_fruit_session ("
            "role_id INT UNSIGNED NOT NULL,"
            "credits INT UNSIGNED NOT NULL DEFAULT 0,"
            "win_chips INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY (role_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_fruit_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "show_entry TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "currency_type TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "credits_per_unit INT UNSIGNED NOT NULL DEFAULT 10,"
            "min_insert INT UNSIGNED NOT NULL DEFAULT 1,"
            "max_insert INT UNSIGNED NOT NULL DEFAULT 999,"
            "bonus_chance TINYINT UNSIGNED NOT NULL DEFAULT 8,"
            "bonus_extra_percent TINYINT UNSIGNED NOT NULL DEFAULT 50,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY (id)) ENGINE=InnoDB"))
        return false;
    (void)vm_mysql_exec(
        "ALTER TABLE server_fruit_config ADD COLUMN "
        "show_entry TINYINT UNSIGNED NOT NULL DEFAULT 1");
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_fruit_wheel ("
            "slot_index TINYINT UNSIGNED NOT NULL,"
            "fruit_id TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "multiplier SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "weight SMALLINT UNSIGNED NOT NULL DEFAULT 1,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY (slot_index)) ENGINE=InnoDB"))
        return false;
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_fruit_config "
        "(id,enabled,show_entry,currency_type,credits_per_unit,min_insert,"
        "max_insert,bonus_chance,bonus_extra_percent) "
        "VALUES (1,1,1,0,10,1,999,8,50)");
    (void)vm_mysql_exec(
        "UPDATE server_fruit_config SET show_entry=0 WHERE enabled=0");
    if (!vm_mock_fruit_seed_default_wheel_rows())
        return false;
    ready = true;
    return true;
}

static void vm_mock_fruit_load_config(bool force)
{
    u32 wheelCount = 0;

    if (g_vm_mock_fruit_config_loaded && !force)
        return;
    vm_mock_fruit_apply_defaults();
    if (!vm_mock_fruit_ensure_tables())
        return;
    if (vm_mysql_query(
            "SELECT enabled,show_entry,currency_type,credits_per_unit,min_insert,"
            "max_insert,bonus_chance,bonus_extra_percent FROM server_fruit_config "
            "WHERE id=1 LIMIT 1",
            vm_mock_fruit_config_row, &g_vm_mock_fruit_config))
    {
        (void)vm_mysql_query(
            "SELECT slot_index,fruit_id,multiplier,weight FROM server_fruit_wheel "
            "ORDER BY slot_index",
            vm_mock_fruit_wheel_row, NULL);
        (void)vm_mysql_query("SELECT COUNT(*) FROM server_fruit_wheel",
                             vm_mock_fruit_u32_row, &wheelCount);
        if (wheelCount < VM_MOCK_FRUIT_WHEEL_SLOTS)
            (void)vm_mock_fruit_seed_default_wheel_rows();
        g_vm_mock_fruit_config_loaded = true;
    }
}

const vm_mock_fruit_config *vm_net_mock_fruit_config_get(void)
{
    vm_mock_fruit_load_config(false);
    return &g_vm_mock_fruit_config;
}

const vm_mock_fruit_wheel_slot *vm_net_mock_fruit_wheel_slot(u32 index)
{
    vm_mock_fruit_load_config(false);
    if (index >= VM_MOCK_FRUIT_WHEEL_SLOTS)
        return NULL;
    return &g_vm_mock_fruit_wheel[index];
}

void vm_mock_fruit_admin_reload_config(void)
{
    g_vm_mock_fruit_config_loaded = false;
    vm_mock_fruit_load_config(true);
}

bool vm_mock_fruit_admin_save_config(u8 enabled, u8 showEntry, u8 currencyType,
                                     u32 creditsPerUnit, u32 minInsert, u32 maxInsert,
                                     u8 bonusChance, u8 bonusExtraPercent)
{
    char query[768];

    if (!vm_mock_fruit_ensure_tables())
        return false;
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_fruit_config "
        "(id,enabled,show_entry,currency_type,credits_per_unit,min_insert,"
        "max_insert,bonus_chance,bonus_extra_percent) VALUES "
        "(1,%u,%u,%u,%u,%u,%u,%u,%u) "
        "ON DUPLICATE KEY UPDATE "
        "enabled=VALUES(enabled),show_entry=VALUES(show_entry),"
        "currency_type=VALUES(currency_type),"
        "credits_per_unit=VALUES(credits_per_unit),min_insert=VALUES(min_insert),"
        "max_insert=VALUES(max_insert),bonus_chance=VALUES(bonus_chance),"
        "bonus_extra_percent=VALUES(bonus_extra_percent)",
        (u32)enabled, (u32)showEntry, (u32)currencyType, creditsPerUnit, minInsert,
        maxInsert, (u32)bonusChance, (u32)bonusExtraPercent);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_fruit_config_loaded = false;
    vm_mock_fruit_load_config(true);
    return true;
}

bool vm_mock_fruit_admin_save_wheel_slot(u8 slotIndex, u8 fruitId, u16 multiplier,
                                         u16 weight)
{
    char query[256];

    if (slotIndex >= VM_MOCK_FRUIT_WHEEL_SLOTS || weight == 0 || fruitId > 8)
        return false;
    if (!vm_mock_fruit_ensure_tables())
        return false;
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_fruit_wheel "
        "(slot_index,fruit_id,multiplier,weight) VALUES (%u,%u,%u,%u) "
        "ON DUPLICATE KEY UPDATE fruit_id=VALUES(fruit_id),"
        "multiplier=VALUES(multiplier),weight=VALUES(weight)",
        (u32)slotIndex, (u32)fruitId, (u32)multiplier, (u32)weight);
    return vm_mysql_exec(query);
}

bool vm_mock_fruit_admin_reset_wheel_defaults(void)
{
    u32 i;

    if (!vm_mock_fruit_ensure_tables())
        return false;
    if (!vm_mysql_exec("DELETE FROM server_fruit_wheel"))
        return false;
    if (!vm_mock_fruit_seed_default_wheel_rows())
        return false;
    for (i = 0; i < VM_MOCK_FRUIT_WHEEL_SLOTS; ++i)
    {
        g_vm_mock_fruit_wheel[i].fruitId = g_vm_mock_fruit_default_fruit[i];
        g_vm_mock_fruit_wheel[i].multiplier = g_vm_mock_fruit_default_mult[i];
        g_vm_mock_fruit_wheel[i].weight = g_vm_mock_fruit_default_weight[i];
    }
    return true;
}

static const vm_mock_fruit_config *vm_mock_fruit_cfg(void)
{
    return vm_net_mock_fruit_config_get();
}

static u32 vm_mock_fruit_wallet_units(vm_net_mock_role_state *role)
{
    const vm_mock_fruit_config *cfg;

    if (role == NULL)
        return 0;
    cfg = vm_mock_fruit_cfg();
    if (cfg != NULL && cfg->currencyType == VM_MOCK_FRUIT_CURRENCY_KUBAO)
        return role->wcoin;
    return role->money / VM_NET_MOCK_COPPER_PER_GOLD;
}

static void vm_mock_fruit_rng_mix(u32 entropy)
{
    g_vm_mock_fruit_rng ^= entropy + 0x9e3779b9u + (g_vm_mock_fruit_rng << 6) +
                           (g_vm_mock_fruit_rng >> 2);
    if (g_vm_mock_fruit_rng == 0)
        g_vm_mock_fruit_rng = 0xF8170813u ^ entropy ^ 1u;
}

static u32 vm_mock_fruit_rng_u32(void)
{
    if (g_vm_mock_fruit_rng == 0)
        vm_mock_fruit_rng_mix((u32)time(NULL) ^ 0x5151u);
    g_vm_mock_fruit_rng = g_vm_mock_fruit_rng * 1664525u + 1013904223u;
    return g_vm_mock_fruit_rng;
}

static u32 vm_mock_fruit_rng_range(u32 maxExclusive)
{
    if (maxExclusive <= 1u)
        return 0;
    return vm_mock_fruit_rng_u32() % maxExclusive;
}

static bool vm_mock_fruit_session_row_cb(void *contextValue, unsigned int columnCount,
                                         const char *const *values, const size_t *lengths)
{
    vm_mock_fruit_session_row *row = (vm_mock_fruit_session_row *)contextValue;

    (void)columnCount;
    if (row == NULL || values == NULL || lengths == NULL)
        return false;
    row->credits = 0;
    row->winChips = 0;
    row->found = false;
    if (values[0] != NULL &&
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &row->credits))
        return false;
    if (values[1] != NULL &&
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &row->winChips))
        return false;
    row->found = true;
    return true;
}

static bool vm_mock_fruit_load_session(u32 roleId, u32 *creditsOut, u32 *winChipsOut)
{
    char query[192];
    vm_mock_fruit_session_row row;

    if (creditsOut)
        *creditsOut = 0;
    if (winChipsOut)
        *winChipsOut = 0;
    if (roleId == 0)
        return false;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT credits,win_chips FROM server_fruit_session WHERE role_id=%u LIMIT 1",
             roleId);
    (void)vm_mysql_query(query, vm_mock_fruit_session_row_cb, &row);
    if (!row.found)
        return true;
    if (creditsOut)
        *creditsOut = row.credits;
    if (winChipsOut)
        *winChipsOut = row.winChips;
    return true;
}

static bool vm_mock_fruit_save_session(u32 roleId, u32 credits, u32 winChips)
{
    char query[256];

    if (roleId == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_fruit_session (role_id,credits,win_chips) VALUES "
             "(%u,%u,%u) ON DUPLICATE KEY UPDATE credits=%u,win_chips=%u",
             roleId, credits, winChips, credits, winChips);
    return vm_mysql_exec(query);
}

static u32 vm_mock_fruit_pick_wheel_pos(void)
{
    u32 total = 0;
    u32 pick;
    u32 i;

    for (i = 0; i < VM_MOCK_FRUIT_WHEEL_SLOTS; ++i)
        total += g_vm_mock_fruit_wheel[i].weight;
    if (total == 0)
        return vm_mock_fruit_rng_range(VM_MOCK_FRUIT_WHEEL_SLOTS);
    pick = vm_mock_fruit_rng_range(total);
    for (i = 0; i < VM_MOCK_FRUIT_WHEEL_SLOTS; ++i)
    {
        if (pick < g_vm_mock_fruit_wheel[i].weight)
            return i;
        pick -= g_vm_mock_fruit_wheel[i].weight;
    }
    return VM_MOCK_FRUIT_WHEEL_SLOTS - 1u;
}

static u32 vm_mock_fruit_resolve_luck_pos(u32 luckPos)
{
    u32 reroll = vm_mock_fruit_pick_wheel_pos();
    u32 guard = 0;

    (void)luckPos;
    while (guard++ < 8u)
    {
        u8 fruit = g_vm_mock_fruit_wheel[reroll].fruitId;
        if (fruit != 0 && g_vm_mock_fruit_wheel[reroll].multiplier > 0)
            return reroll;
        reroll = vm_mock_fruit_rng_range(VM_MOCK_FRUIT_WHEEL_SLOTS);
    }
    return 1u;
}

static u32 vm_mock_fruit_bet_total(const u32 bets[9], u32 multiple)
{
    u32 total = 0;
    u32 id;

    if (multiple == 0)
        return 0;
    for (id = 1; id <= 8u; ++id)
        total += bets[id] * multiple;
    return total;
}

static u32 vm_mock_fruit_calc_single_win(u32 triggerPos, const u32 bets[9], u32 multiple)
{
    u8 fruit;
    u16 mult;
    u32 win = 0;

    if (triggerPos >= VM_MOCK_FRUIT_WHEEL_SLOTS || multiple == 0)
        return 0;
    fruit = g_vm_mock_fruit_wheel[triggerPos].fruitId;
    mult = g_vm_mock_fruit_wheel[triggerPos].multiplier;
    if (fruit == 0 || mult == 0)
        return 0;
    if (fruit <= 8u && bets[fruit] > 0)
        win = bets[fruit] * multiple * (u32)mult;
    return win;
}

static u32 vm_mock_fruit_calc_group_win(const u8 *positions, u8 count, const u32 bets[9],
                                        u32 multiple)
{
    u32 win = 0;
    u8 i;

    if (positions == NULL || count == 0)
        return 0;
    for (i = 0; i < count; ++i)
    {
        if (positions[i] >= VM_MOCK_FRUIT_WHEEL_SLOTS)
            continue;
        win += vm_mock_fruit_calc_single_win(positions[i], bets, multiple);
    }
    return win;
}

static bool vm_mock_fruit_pos_in(u8 pos, const u8 *arr, u8 n)
{
    u8 i;
    for (i = 0; i < n; ++i)
    {
        if (arr[i] == pos)
            return true;
    }
    return false;
}

static void vm_mock_fruit_append_unique(u8 *out, u8 *count, u8 cap, u8 pos)
{
    if (out == NULL || count == NULL || *count >= cap || pos >= VM_MOCK_FRUIT_WHEEL_SLOTS)
        return;
    if (vm_mock_fruit_pos_in(pos, out, *count))
        return;
    out[(*count)++] = pos;
}

/* 把 positions 格式化为 "a,b,c" */
static void vm_mock_fruit_format_group(char *buf, u32 cap, const u8 *positions, u8 count)
{
    u32 pos = 0;
    u8 i;

    if (buf == NULL || cap == 0)
        return;
    buf[0] = '\0';
    for (i = 0; i < count; ++i)
    {
        int n;
        if (pos + 4 >= cap)
            break;
        n = snprintf(buf + pos, cap - pos, i == 0 ? "%u" : ",%u", (u32)positions[i]);
        if (n <= 0)
            break;
        pos += (u32)n;
    }
}

/*
 * 特殊牌型（经典水果机 + luckyFruit 客户端契约）：
 * XSY 小三元 / DSY 大三元 / DSX 大四喜 / ZHSH 纵横四海 /
 * XNSH 仙女散花 / CONTINUITY 送灯 / HC 开火车 /
 * TLBB 天龙八部 / JLBD 九莲宝灯 / DMG 大满贯
 */
static void vm_mock_fruit_build_special(u32 triggerPos, const u32 bets[9], u32 multiple,
                                        const char **typeOut, u8 *groupOut, u8 *groupCountOut,
                                        u32 *winOut)
{
    static const u8 kXsy[3] = {4, 9, 3};    /* 大铃 大芒 大橙 */
    static const u8 kDsy[3] = {10, 18, 22}; /* 大瓜 大77 大星 */
    static const u8 kDsx[4] = {1, 7, 13, 19};
    static const u8 kZhsh[4][7] = {
        {21, 22, 23, 0, 1, 2, 3},
        {3, 4, 5, 6, 7, 8, 9},
        {9, 10, 11, 12, 13, 14, 15},
        {15, 16, 17, 18, 19, 20, 21},
    };
    u8 group[24];
    u8 count = 0;
    u8 fruit;
    u32 roll;
    u32 win;
    u8 i;

    if (typeOut == NULL || groupOut == NULL || groupCountOut == NULL || winOut == NULL)
        return;
    *typeOut = "SINGLE";
    *groupCountOut = 0;
    *winOut = 0;
    memset(group, 0, sizeof(group));
    fruit = g_vm_mock_fruit_wheel[triggerPos].fruitId;
    roll = vm_mock_fruit_rng_range(1000);

    /* 超稀有：大满贯 / 九莲宝灯 / 天龙八部 / 开火车 / 纵横四海 */
    if (roll < 3u)
    {
        for (i = 0; i < VM_MOCK_FRUIT_WHEEL_SLOTS; ++i)
        {
            if (g_vm_mock_fruit_wheel[i].fruitId != 0)
                vm_mock_fruit_append_unique(group, &count, 24, i);
        }
        *typeOut = "DMG";
    }
    else if (roll < 8u)
    {
        u8 need = 9;
        vm_mock_fruit_append_unique(group, &count, 24, (u8)triggerPos);
        while (count < need)
        {
            u8 p = (u8)vm_mock_fruit_rng_range(VM_MOCK_FRUIT_WHEEL_SLOTS);
            if (g_vm_mock_fruit_wheel[p].fruitId == 0)
                continue;
            vm_mock_fruit_append_unique(group, &count, 24, p);
        }
        *typeOut = "JLBD";
    }
    else if (roll < 15u)
    {
        u8 need = 8;
        vm_mock_fruit_append_unique(group, &count, 24, (u8)triggerPos);
        while (count < need)
        {
            u8 p = (u8)vm_mock_fruit_rng_range(VM_MOCK_FRUIT_WHEEL_SLOTS);
            if (g_vm_mock_fruit_wheel[p].fruitId == 0)
                continue;
            vm_mock_fruit_append_unique(group, &count, 24, p);
        }
        *typeOut = "TLBB";
    }
    else if (roll < 30u)
    {
        u8 len = (u8)(4u + vm_mock_fruit_rng_range(3u)); /* 4..6 */
        for (i = 0; i < len; ++i)
            vm_mock_fruit_append_unique(
                group, &count, 24, (u8)((triggerPos + i) % VM_MOCK_FRUIT_WHEEL_SLOTS));
        *typeOut = "HC";
    }
    else if (roll < 45u)
    {
        u8 side = (u8)vm_mock_fruit_rng_range(4u);
        for (i = 0; i < 7u; ++i)
            vm_mock_fruit_append_unique(group, &count, 24, kZhsh[side][i]);
        if (!vm_mock_fruit_pos_in((u8)triggerPos, group, count))
        {
            /* 触发点不在该边则换到含触发点的边 */
            for (side = 0; side < 4u; ++side)
            {
                if (vm_mock_fruit_pos_in((u8)triggerPos, kZhsh[side], 7))
                    break;
            }
            if (side >= 4u)
                side = 0;
            count = 0;
            for (i = 0; i < 7u; ++i)
                vm_mock_fruit_append_unique(group, &count, 24, kZhsh[side][i]);
        }
        *typeOut = "ZHSH";
    }
    else if (fruit == 0)
    {
        /* 幸运格：送灯 / 仙女散花 / 未中 */
        if (roll < 400u)
        {
            u8 n = (u8)(1u + vm_mock_fruit_rng_range(3u));
            while (count < n)
            {
                u8 p = (u8)vm_mock_fruit_rng_range(VM_MOCK_FRUIT_WHEEL_SLOTS);
                if (g_vm_mock_fruit_wheel[p].fruitId == 0)
                    continue;
                vm_mock_fruit_append_unique(group, &count, 24, p);
            }
            *typeOut = "CONTINUITY";
        }
        else if (roll < 700u)
        {
            u8 n = 5;
            while (count < n)
            {
                u8 p = (u8)vm_mock_fruit_rng_range(VM_MOCK_FRUIT_WHEEL_SLOTS);
                if (g_vm_mock_fruit_wheel[p].fruitId == 0)
                    continue;
                vm_mock_fruit_append_unique(group, &count, 24, p);
            }
            *typeOut = "XNSH";
        }
        else
        {
            *typeOut = "LUCK";
            *winOut = 0;
            return;
        }
    }
    else if (vm_mock_fruit_pos_in((u8)triggerPos, kDsx, 4) && roll < 200u)
    {
        for (i = 0; i < 4u; ++i)
            group[count++] = kDsx[i];
        *typeOut = "DSX";
    }
    else if (vm_mock_fruit_pos_in((u8)triggerPos, kDsy, 3) && roll < 220u)
    {
        /* 触发格放首位，便于跑灯 */
        group[count++] = (u8)triggerPos;
        for (i = 0; i < 3u; ++i)
        {
            if (kDsy[i] != (u8)triggerPos)
                group[count++] = kDsy[i];
        }
        *typeOut = "DSY";
    }
    else if (vm_mock_fruit_pos_in((u8)triggerPos, kXsy, 3) && roll < 220u)
    {
        group[count++] = (u8)triggerPos;
        for (i = 0; i < 3u; ++i)
        {
            if (kXsy[i] != (u8)triggerPos)
                group[count++] = kXsy[i];
        }
        *typeOut = "XSY";
    }
    else
    {
        group[count++] = (u8)triggerPos;
        *typeOut = "SINGLE";
    }

    if (count == 0)
    {
        group[count++] = (u8)triggerPos;
        *typeOut = "SINGLE";
    }
    win = vm_mock_fruit_calc_group_win(group, count, bets, multiple);
    memcpy(groupOut, group, count);
    *groupCountOut = count;
    *winOut = win;
}

static u32 vm_mock_fruit_finish_packet(u8 *out, u32 pos, u8 objectCount)
{
    if (out == NULL || pos < 5)
        return 0;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)((pos >> 8) & 0xffu);
    out[3] = (u8)(pos & 0xffu);
    out[4] = objectCount;
    return pos;
}

static bool vm_mock_fruit_enabled(void)
{
    const vm_mock_fruit_config *cfg = vm_mock_fruit_cfg();

    return cfg != NULL && cfg->enabled;
}

static u32 vm_net_mock_build_fruit_op_result(u8 *out, u32 outCap, u8 subtype, u32 result,
                                           const char *message, u32 credits, u32 winChips,
                                           u32 walletUnits, u32 point,
                                           const char *compareType)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < 5)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_FRUIT_WT_KIND, subtype,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message",
                                       message ? message : ""))
        return 0;
    if (subtype == VM_MOCK_FRUIT_WT_STATUS || subtype == VM_MOCK_FRUIT_WT_INSERT ||
        subtype == VM_MOCK_FRUIT_WT_SPIN || subtype == VM_MOCK_FRUIT_WT_COLLECT ||
        subtype == VM_MOCK_FRUIT_WT_CASH_OUT)
    {
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "credits", credits) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "winChips", winChips))
            return 0;
    }
    if (subtype == VM_MOCK_FRUIT_WT_INSERT || subtype == VM_MOCK_FRUIT_WT_CASH_OUT ||
        subtype == VM_MOCK_FRUIT_WT_STATUS)
    {
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "goldJin", walletUnits))
            return 0;
    }
    if (subtype == VM_MOCK_FRUIT_WT_COMPARE)
    {
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "winChips", winChips) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "point", point) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "compareType",
                                           compareType ? compareType : ""))
            return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_fruit_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_fruit_status_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const vm_mock_fruit_config *cfg;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 credits = 0;
    u32 winChips = 0;

    if (out == NULL || outCap < 5 || role == NULL)
        return 0;
    cfg = vm_mock_fruit_cfg();
    (void)vm_mock_fruit_load_session(role->roleId, &credits, &winChips);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_FRUIT_WT_KIND,
                                     VM_MOCK_FRUIT_WT_STATUS, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "credits", credits) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "winChips", winChips) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "goldJin",
                                    vm_mock_fruit_wallet_units(role)) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "copper", role->money) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "wcoin", role->wcoin) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "currencyType",
                                    cfg ? cfg->currencyType : 0) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "creditsPerGold",
                                    cfg ? cfg->creditsPerUnit : 10) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "minInsertGold",
                                    cfg ? cfg->minInsert : 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxInsertGold",
                                    cfg ? cfg->maxInsert : 999) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "hasWin", winChips > 0 ? 1u : 0u) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "enabled",
                                    cfg && cfg->enabled ? 1u : 0u) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "showEntry",
                                    cfg && cfg->enabled && cfg->showEntry ? 1u : 0u))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_fruit_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_fruit_insert_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const vm_mock_fruit_config *cfg;
    u32 units = 0;
    u32 stakeCopper = 0;
    u32 credits = 0;
    u32 winChips = 0;
    u32 addCredits = 0;
    u32 wcoinBefore = 0;
    u32 moneyBefore = 0;

    if (out == NULL || role == NULL || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_FRUIT_WT_KIND ||
        object.subtype != VM_MOCK_FRUIT_WT_INSERT)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                 "no-role", 0, 0, 0, 0, NULL);
    if (!vm_mock_fruit_enabled())
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                 "disabled", 0, 0, 0, 0, NULL);
    cfg = vm_mock_fruit_cfg();
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "gold",
                                           &units);
    if (cfg == NULL || units < cfg->minInsert || units > cfg->maxInsert)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                 "bad-gold", 0, 0, 0, 0, NULL);
    addCredits = units * cfg->creditsPerUnit;
    if (cfg->creditsPerUnit == 0 || addCredits / cfg->creditsPerUnit != units)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                 "overflow", 0, 0, 0, 0, NULL);
    if (cfg->currencyType == VM_MOCK_FRUIT_CURRENCY_KUBAO)
    {
        if (role->wcoin < units)
            return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                     "balance", 0, 0, 0, 0, NULL);
        wcoinBefore = role->wcoin;
        role->wcoin -= units;
    }
    else
    {
        if (units > 0xffffffffu / VM_NET_MOCK_COPPER_PER_GOLD)
            return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                     "bad-gold", 0, 0, 0, 0, NULL);
        stakeCopper = units * VM_NET_MOCK_COPPER_PER_GOLD;
        if (role->money < stakeCopper)
            return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                     "balance", 0, 0, 0, 0, NULL);
        moneyBefore = role->money;
        role->money -= stakeCopper;
    }
    (void)vm_mock_fruit_load_session(role->roleId, &credits, &winChips);
    if (credits + addCredits < credits)
    {
        if (cfg->currencyType == VM_MOCK_FRUIT_CURRENCY_KUBAO)
            role->wcoin = wcoinBefore;
        else
            role->money = moneyBefore;
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                 "overflow", credits, winChips, 0, 0, NULL);
    }
    credits += addCredits;
    vm_net_mock_role_mark_inventory_dirty("fruit-insert");
    if (!vm_mock_fruit_save_session(role->roleId, credits, winChips))
    {
        if (cfg->currencyType == VM_MOCK_FRUIT_CURRENCY_KUBAO)
            role->wcoin = wcoinBefore;
        else
            role->money = moneyBefore;
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_INSERT, 2,
                                                 "save", credits - addCredits, winChips, 0,
                                                 0, NULL);
    }
    vm_net_mock_role_db_save("fruit-insert");
    printf("[info][mock-service] fruit_insert role=%u units=%u credits=%u currency=%u\n",
           role->roleId, units, credits, cfg->currencyType);
    return vm_net_mock_build_fruit_op_result(
        out, outCap, VM_MOCK_FRUIT_WT_INSERT, 1, "ok", credits, winChips,
        vm_mock_fruit_wallet_units(role), 0, NULL);
}

static u32 vm_net_mock_build_fruit_spin_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const vm_mock_fruit_config *cfg;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 multiple = 0;
    u32 bets[9];
    u32 cost = 0;
    u32 credits = 0;
    u32 winChips = 0;
    u32 triggerPos = 0;
    u32 win = 0;
    u32 i;
    const char *cardGroupType = "SINGLE";

    memset(bets, 0, sizeof(bets));
    if (out == NULL || role == NULL || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_FRUIT_WT_KIND ||
        object.subtype != VM_MOCK_FRUIT_WT_SPIN)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_SPIN, 2,
                                                 "no-role", 0, 0, 0, 0, NULL);
    if (!vm_mock_fruit_enabled())
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_SPIN, 2,
                                                 "disabled", 0, 0, 0, 0, NULL);
    cfg = vm_mock_fruit_cfg();
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "multiple",
                                           &multiple);
    for (i = 1; i <= 8u; ++i)
    {
        char field[8];
        snprintf(field, sizeof(field), "bet%u", i);
        (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, field,
                                               &bets[i]);
    }
    if (multiple == 0 || (multiple != 1 && multiple != 5 && multiple != 10 &&
                          multiple != 50 && multiple != 100))
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_SPIN, 2,
                                                 "bad-mult", 0, 0, 0, 0, NULL);
    cost = vm_mock_fruit_bet_total(bets, multiple);
    if (cost == 0)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_SPIN, 2,
                                                 "no-bet", 0, 0, 0, 0, NULL);
    (void)vm_mock_fruit_load_session(role->roleId, &credits, &winChips);
    if (winChips > 0)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_SPIN, 2,
                                                 "pending-win", credits, winChips, 0, 0,
                                                 NULL);
    if (credits < cost)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_SPIN, 2,
                                                 "no-credit", credits, winChips, 0, 0,
                                                 NULL);
    vm_mock_fruit_rng_mix((u32)time(NULL) ^ role->roleId ^ cost ^ multiple);
    triggerPos = vm_mock_fruit_pick_wheel_pos();
    if (g_vm_mock_fruit_wheel[triggerPos].fruitId == 0)
    {
        /* 35%：幸运格改派到普通水果，再走特殊牌型判定 */
        if (vm_mock_fruit_rng_range(100) < 35u)
            triggerPos = vm_mock_fruit_resolve_luck_pos(triggerPos);
    }
    {
        const char *specialType = "SINGLE";
        u8 group[24];
        u8 groupCount = 0;
        char groupStr[96];

        memset(group, 0, sizeof(group));
        groupStr[0] = '\0';
        vm_mock_fruit_build_special(triggerPos, bets, multiple, &specialType, group,
                                    &groupCount, &win);
        cardGroupType = specialType;
        vm_mock_fruit_format_group(groupStr, sizeof(groupStr), group, groupCount);
        if (win > 0 && cfg != NULL && cfg->bonusChance > 0 &&
            strcmp(cardGroupType, "SINGLE") == 0 &&
            vm_mock_fruit_rng_range(100) < cfg->bonusChance)
        {
            cardGroupType = "BONUS";
            win += win * (u32)cfg->bonusExtraPercent / 100u;
        }
        credits -= cost;
        if (win > 0)
            winChips = win;
        if (!vm_mock_fruit_save_session(role->roleId, credits, winChips))
            return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_SPIN, 2,
                                                     "save", credits + cost, 0, 0, 0, NULL);
        printf("[info][mock-service] fruit_spin role=%u pos=%u type=%s win=%u cost=%u "
               "mult=%u group=%s\n",
               role->roleId, triggerPos, cardGroupType, win, cost, multiple, groupStr);
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_FRUIT_WT_KIND,
                                         VM_MOCK_FRUIT_WT_SPIN, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "message", "ok") ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "triggerPos", triggerPos) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "cardGroupType",
                                           cardGroupType) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "cardGroup", groupStr) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "win", win) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "credits", credits) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "winChips", winChips))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        return vm_mock_fruit_finish_packet(out, pos, 1);
    }
}

static u32 vm_net_mock_build_fruit_collect_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 credits = 0;
    u32 winChips = 0;

    if (out == NULL || role == NULL)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COLLECT, 2,
                                                 "no-role", 0, 0, 0, 0, NULL);
    if (!vm_mock_fruit_enabled())
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COLLECT, 2,
                                                 "disabled", 0, 0, 0, 0, NULL);
    (void)vm_mock_fruit_load_session(role->roleId, &credits, &winChips);
    if (winChips == 0)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COLLECT, 2,
                                                 "nothing", credits, 0, 0, 0, NULL);
    if (credits + winChips < credits)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COLLECT, 2,
                                                 "overflow", credits, winChips, 0, 0, NULL);
    credits += winChips;
    winChips = 0;
    if (!vm_mock_fruit_save_session(role->roleId, credits, winChips))
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COLLECT, 2,
                                                 "save", credits - winChips, winChips, 0, 0,
                                                 NULL);
    printf("[info][mock-service] fruit_collect role=%u credits=%u\n", role->roleId,
           credits);
    return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COLLECT, 1, "ok",
                                           credits, winChips, 0, 0, NULL);
}

static u32 vm_net_mock_build_fruit_compare_response(const u8 *request, u32 requestLen,
                                                    u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 guess = 0;
    u32 credits = 0;
    u32 winChips = 0;
    u32 point = 0;
    const char *compareType = "LOSE";

    if (out == NULL || role == NULL || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_FRUIT_WT_KIND ||
        object.subtype != VM_MOCK_FRUIT_WT_COMPARE)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COMPARE, 2,
                                                 "no-role", 0, 0, 0, 0, NULL);
    if (!vm_mock_fruit_enabled())
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COMPARE, 2,
                                                 "disabled", 0, 0, 0, 0, NULL);
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "guess",
                                           &guess);
    (void)vm_mock_fruit_load_session(role->roleId, &credits, &winChips);
    if (winChips == 0)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COMPARE, 2,
                                                 "nothing", credits, 0, 0, 0, NULL);
    if (guess != VM_MOCK_FRUIT_COMPARE_LOWER && guess != VM_MOCK_FRUIT_COMPARE_BIGGER)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COMPARE, 2,
                                                 "bad-guess", credits, winChips, 0, 0,
                                                 NULL);
    vm_mock_fruit_rng_mix((u32)time(NULL) ^ role->roleId ^ winChips ^ guess);
    point = 1u + vm_mock_fruit_rng_range(14u);
    if (point == 7u)
        compareType = "TIED";
    else if ((point > 7u && guess == VM_MOCK_FRUIT_COMPARE_BIGGER) ||
             (point < 7u && guess == VM_MOCK_FRUIT_COMPARE_LOWER))
    {
        compareType = "WIN";
        if (winChips > 0xffffffffu / 2u)
            winChips = 0xffffffffu;
        else
            winChips *= 2u;
    }
    else
        winChips = 0;
    if (!vm_mock_fruit_save_session(role->roleId, credits, winChips))
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COMPARE, 2,
                                                 "save", credits, winChips, 0, point,
                                                 compareType);
    printf("[info][mock-service] fruit_compare role=%u point=%u type=%s win=%u\n",
           role->roleId, point, compareType, winChips);
    return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_COMPARE, 1, "ok",
                                           credits, winChips, 0, point, compareType);
}

static u32 vm_net_mock_build_fruit_cash_out_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const vm_mock_fruit_config *cfg;
    u32 credits = 0;
    u32 winChips = 0;
    u32 units = 0;
    u32 reqUnits = 0;
    u32 maxUnits = 0;
    u32 copper = 0;
    u32 wcoinBefore = 0;
    u32 moneyBefore = 0;

    if (out == NULL || role == NULL)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                 "no-role", 0, 0, 0, 0, NULL);
    if (!vm_mock_fruit_enabled())
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                 "disabled", 0, 0, 0, 0, NULL);
    cfg = vm_mock_fruit_cfg();
    (void)vm_mock_fruit_load_session(role->roleId, &credits, &winChips);
    if (winChips > 0)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                 "pending-win", credits, winChips, 0, 0,
                                                 NULL);
    if (cfg == NULL || cfg->creditsPerUnit == 0 ||
        credits < cfg->creditsPerUnit)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                 "too-small", credits, 0, 0, 0, NULL);
    maxUnits = credits / cfg->creditsPerUnit;
    /* Optional gold(=wallet units). Missing/0 → cash out all convertible credits. */
    if (request != NULL && requestLen >= 9 &&
        vm_net_mock_next_request_object(request, requestLen, &offset, &object) &&
        object.major == 1 && object.kind == VM_MOCK_FRUIT_WT_KIND &&
        object.subtype == VM_MOCK_FRUIT_WT_CASH_OUT)
    {
        (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "gold",
                                               &reqUnits);
    }
    if (reqUnits == 0)
        units = maxUnits;
    else if (reqUnits > maxUnits)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                 "too-much", credits, 0, 0, 0, NULL);
    else
        units = reqUnits;
    if (units == 0)
        return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                 "bad-gold", credits, 0, 0, 0, NULL);
    credits -= units * cfg->creditsPerUnit;
    if (cfg->currencyType == VM_MOCK_FRUIT_CURRENCY_KUBAO)
    {
        wcoinBefore = role->wcoin;
        if (role->wcoin + units < role->wcoin)
            return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                     "overflow", credits, 0, 0, 0, NULL);
        role->wcoin += units;
    }
    else
    {
        copper = units * VM_NET_MOCK_COPPER_PER_GOLD;
        if (copper / VM_NET_MOCK_COPPER_PER_GOLD != units)
            return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                     "overflow", credits, 0, 0, 0, NULL);
        moneyBefore = role->money;
        if (role->money + copper < role->money)
            return vm_net_mock_build_fruit_op_result(out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2,
                                                     "overflow", credits, 0, 0, 0, NULL);
        role->money += copper;
    }
    vm_net_mock_role_mark_inventory_dirty("fruit-cashout");
    if (!vm_mock_fruit_save_session(role->roleId, credits, winChips))
    {
        if (cfg->currencyType == VM_MOCK_FRUIT_CURRENCY_KUBAO)
            role->wcoin = wcoinBefore;
        else
            role->money = moneyBefore;
        return vm_net_mock_build_fruit_op_result(
            out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 2, "save",
            credits + units * cfg->creditsPerUnit, 0, 0, 0, NULL);
    }
    vm_net_mock_role_db_save("fruit-cashout");
    printf("[info][mock-service] fruit_cashout role=%u units=%u credits=%u currency=%u\n",
           role->roleId, units, credits, cfg->currencyType);
    return vm_net_mock_build_fruit_op_result(
        out, outCap, VM_MOCK_FRUIT_WT_CASH_OUT, 1, "ok", credits, winChips,
        vm_mock_fruit_wallet_units(role), 0, NULL);
}

static bool vm_net_mock_is_fruit48_request(const u8 *request, u32 requestLen, u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != VM_MOCK_FRUIT_WT_KIND)
        return false;
    if (subtypeOut)
        *subtypeOut = object.subtype;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_fruit_protocol_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u8 subtype = 0;

    if (!vm_net_mock_is_fruit48_request(request, requestLen, &subtype))
        return 0;
    vm_mock_fruit_load_config(false);
    if (subtype == VM_MOCK_FRUIT_WT_STATUS)
        return vm_net_mock_build_fruit_status_response(out, outCap);
    if (subtype == VM_MOCK_FRUIT_WT_INSERT)
        return vm_net_mock_build_fruit_insert_response(request, requestLen, out, outCap);
    if (subtype == VM_MOCK_FRUIT_WT_SPIN)
        return vm_net_mock_build_fruit_spin_response(request, requestLen, out, outCap);
    if (subtype == VM_MOCK_FRUIT_WT_COLLECT)
        return vm_net_mock_build_fruit_collect_response(out, outCap);
    if (subtype == VM_MOCK_FRUIT_WT_COMPARE)
        return vm_net_mock_build_fruit_compare_response(request, requestLen, out, outCap);
    if (subtype == VM_MOCK_FRUIT_WT_CASH_OUT)
        return vm_net_mock_build_fruit_cash_out_response(request, requestLen, out, outCap);
    return 0;
}
