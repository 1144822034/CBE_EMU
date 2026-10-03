/*
 * Pixi activities: vitality, festival exchange (节日使者), Yanmen materials,
 * ring quests. Included after mock_server_guess.c.
 */

enum
{
    VM_MOCK_ACTIVITIES_WT_KIND = 46,
    VM_MOCK_ACTIVITIES_WT_STATUS = 1,
    VM_MOCK_ACTIVITIES_WT_FESTIVAL_EXCHANGE = 2,
    VM_MOCK_ACTIVITIES_WT_YANMEN_LIST = 3,
    VM_MOCK_ACTIVITIES_WT_YANMEN_EXCHANGE = 4,
    VM_MOCK_RING_WT_KIND = 47,
    VM_MOCK_RING_WT_STATUS = 1,
    VM_MOCK_RING_WT_ACCEPT = 2,
    VM_MOCK_RING_WT_SUBMIT = 3,
    VM_MOCK_RING_WT_CLAIM_DAILY = 4,
    VM_MOCK_RING_WT_REFRESH = 5,
    /* 刷新当前环任务消耗酷宝（role.wcoin） */
    VM_MOCK_RING_REFRESH_WCOIN = 10,
    VM_MOCK_ACTIVITIES_EXCHANGE_MAX = 32,
    VM_MOCK_ACTIVITIES_ARENA_VITALITY_PER_ROUND = 20,
    VM_MOCK_ACTIVITIES_RING_VITALITY_REWARD = 5,
    VM_MOCK_ACTIVITIES_LADDER_VITALITY_REWARD = 5,
    VM_MOCK_ACTIVITIES_VITALITY_DEFAULT = 0,
    VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT = 10000,
    VM_MOCK_ACTIVITIES_ONLINE_VITALITY_PER_HOUR_DEFAULT = 10,
    VM_MOCK_ACTIVITIES_ONLINE_HOUR_SEC = 3600,
    VM_MOCK_RING_DAILY_MAX_DEFAULT = 20,
    VM_MOCK_RING_KEY_RATE_DEFAULT = 5,
    VM_MOCK_RING_TYPE_VISIT = 1,
    VM_MOCK_RING_TYPE_KILL_MOB = 2,
    VM_MOCK_RING_TYPE_KILL_BOSS = 3,
    VM_MOCK_RING_TYPE_KILL_PLAYER = 4,
    VM_MOCK_RING_TYPE_COLLECT = 5,
    VM_MOCK_RING_TYPE_SUBMIT_MED = 6,
    VM_MOCK_RING_TYPE_SUBMIT_EQUIP = 7,
    VM_MOCK_RING_TYPE_WORLD_TRUTH = 8,
    VM_MOCK_RING_TYPE_COUNT = 8,
    /* NPC 药商一次购买步长/一组数量（与常见江湖「一组10个」一致） */
    VM_MOCK_RING_MED_PER_GROUP = 10,
    VM_MOCK_RING_TRUTH_MSG_MAX = 48,
    VM_MOCK_RING_TRUTH_LINE_MAX = 256
};

/* GBK defaults: 节日使者 / 关隘使者 / 跑环使 */
static const char VM_MOCK_FESTIVAL_NPC_KEYWORD_DEFAULT[] =
    "\xbd\xda\xc8\xd5\xca\xb9\xd5\xdf";
static const char VM_MOCK_YANMEN_NPC_KEYWORD_DEFAULT[] =
    "\xb9\xd8\xb0\xac\xca\xb9\xd5\xdf";
static const char VM_MOCK_RING_NPC_KEYWORD_DEFAULT[] =
    "\xc5\xdc\xbb\xb7\xca\xb9\xd5\xdf";
static const char VM_MOCK_YANMEN_SCENE_KEYWORD_DEFAULT[] =
    "\xd1\xe3\xc3\xc5\xb9\xd8";

typedef struct
{
    u8 enabled;
    char npcKeyword[32];
    char welcomeText[128];
} vm_mock_festival_config;

typedef struct
{
    u32 exchangeId;
    u32 vitalityCost;
    u32 itemId;
    u32 itemCount;
    char label[48];
    u8 enabled;
    u16 sortOrder;
} vm_mock_festival_exchange;

typedef struct
{
    u8 enabled;
    char sceneKeyword[32];
    char npcKeyword[32];
    u32 woodItemId;
    u32 stoneItemId;
    u8 woodDropMin;
    u8 woodDropMax;
    u8 stoneDropMin;
    u8 stoneDropMax;
} vm_mock_yanmen_config;

typedef struct
{
    u32 exchangeId;
    u32 woodCost;
    u32 stoneCost;
    u32 itemId;
    u32 itemCount;
    char label[48];
    u8 enabled;
    u16 sortOrder;
} vm_mock_yanmen_exchange;

typedef struct
{
    u8 enabled;
    char npcKeyword[32];
    u16 dailyMax;
    u32 dailyBonusItemId;
    u32 dailyBonusCount;
    u8 keyDropRate;
    u32 keyItemIds[3];
    u32 baseExp;
    u32 baseMoney;
    u32 expPerRing;
    u32 moneyPerRing;
} vm_mock_ring_config;

typedef struct
{
    u32 taskType;
    u32 targetId;
    u32 targetCount;
    u32 progress;
    char targetNpc[16];
    char desc[160];
    u8 active;
} vm_mock_ring_runtime;

static vm_mock_festival_config g_vm_mock_festival_config;
static bool g_vm_mock_festival_config_loaded = false;
static vm_mock_festival_exchange g_vm_mock_festival_exchanges[VM_MOCK_ACTIVITIES_EXCHANGE_MAX];
static u32 g_vm_mock_festival_exchange_count = 0;

/* 在线时长活力：每满 1 小时发放 onlinePerHour 点（后台可配；0=关闭） */
static u32 g_vm_mock_vitality_online_per_hour =
    VM_MOCK_ACTIVITIES_ONLINE_VITALITY_PER_HOUR_DEFAULT;
static bool g_vm_mock_vitality_online_config_loaded = false;

static vm_mock_yanmen_config g_vm_mock_yanmen_config;
static bool g_vm_mock_yanmen_config_loaded = false;
static vm_mock_yanmen_exchange g_vm_mock_yanmen_exchanges[VM_MOCK_ACTIVITIES_EXCHANGE_MAX];
static u32 g_vm_mock_yanmen_exchange_count = 0;

static vm_mock_ring_config g_vm_mock_ring_config;
static bool g_vm_mock_ring_config_loaded = false;
static vm_mock_ring_runtime g_vm_mock_ring_rt;
static u32 g_vm_mock_ring_rng = 0xA11CEu;

/* chatList.txt（GBK）：世界真心话候选句，targetId=1..count */
static char g_vm_mock_ring_truth_msgs[VM_MOCK_RING_TRUTH_LINE_MAX]
    [VM_MOCK_RING_TRUTH_MSG_MAX];
static u32 g_vm_mock_ring_truth_count = 0;
static bool g_vm_mock_ring_truth_loaded = false;

static u32 vm_mock_activities_day_ymd(void)
{
    time_t now = time(NULL);
    struct tm *tmNow = localtime(&now);
    if (tmNow == NULL)
        return 0;
    return (u32)((tmNow->tm_year + 1900) * 10000 + (tmNow->tm_mon + 1) * 100 +
                 tmNow->tm_mday);
}

static u32 vm_mock_activities_rng_next(void)
{
    g_vm_mock_ring_rng ^= g_vm_mock_ring_rng << 13;
    g_vm_mock_ring_rng ^= g_vm_mock_ring_rng >> 17;
    g_vm_mock_ring_rng ^= g_vm_mock_ring_rng << 5;
    return g_vm_mock_ring_rng;
}

static void vm_mock_ring_truth_trim_inplace(char *s)
{
    size_t len;
    size_t i;

    if (s == NULL || s[0] == 0)
        return;
    len = strlen(s);
    while (len > 0 &&
           (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' ||
            s[len - 1] == '\t'))
    {
        s[len - 1] = 0;
        --len;
    }
    i = 0;
    while (s[i] == ' ' || s[i] == '\t')
        ++i;
    if (i > 0)
        memmove(s, s + i, strlen(s + i) + 1);
}

static void vm_mock_ring_truth_load(void)
{
    char path[1200];
    FILE *fp = NULL;
    char line[256];
    static const char *fallbackPaths[] = {
        "web/fs/JHOnlineData/chatList.txt",
        "../web/fs/JHOnlineData/chatList.txt",
        "bin/JHOnlineData/chatList.txt",
        "../bin/JHOnlineData/chatList.txt",
        "JHOnlineData/chatList.txt"
    };
    u32 i;

    if (g_vm_mock_ring_truth_loaded)
        return;
    g_vm_mock_ring_truth_loaded = true;
    g_vm_mock_ring_truth_count = 0;
    memset(g_vm_mock_ring_truth_msgs, 0, sizeof(g_vm_mock_ring_truth_msgs));

    path[0] = 0;
    if (vm_net_mock_build_configured_resource_path("chatList.txt", path,
                                                   sizeof(path)))
        fp = fopen(path, "rb");
    for (i = 0; fp == NULL &&
         i < sizeof(fallbackPaths) / sizeof(fallbackPaths[0]);
         ++i)
        fp = fopen(fallbackPaths[i], "rb");
    if (fp == NULL)
    {
        printf("[warn][mock-service] ring_truth_chatlist_missing "
               "evidence=chatList.txt\n");
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL &&
           g_vm_mock_ring_truth_count < VM_MOCK_RING_TRUTH_LINE_MAX)
    {
        char *tab;
        char *msg;

        vm_mock_ring_truth_trim_inplace(line);
        if (line[0] == 0)
            continue;
        tab = strchr(line, '\t');
        msg = tab ? (tab + 1) : NULL;
        if (msg == NULL)
        {
            /* allow "N message" with spaces */
            msg = line;
            while (*msg && *msg != ' ' && *msg != '\t')
                ++msg;
            while (*msg == ' ' || *msg == '\t')
                ++msg;
        }
        vm_mock_ring_truth_trim_inplace(msg);
        if (msg[0] == 0)
            continue;
        snprintf(g_vm_mock_ring_truth_msgs[g_vm_mock_ring_truth_count],
                 sizeof(g_vm_mock_ring_truth_msgs[0]), "%s", msg);
        g_vm_mock_ring_truth_count += 1;
    }
    fclose(fp);
    printf("[info][mock-service] ring_truth_chatlist loaded=%u source=chatList.txt\n",
           g_vm_mock_ring_truth_count);
}

static const char *vm_mock_ring_truth_message(u32 targetId)
{
    vm_mock_ring_truth_load();
    if (targetId == 0 || targetId > g_vm_mock_ring_truth_count)
        return "";
    return g_vm_mock_ring_truth_msgs[targetId - 1];
}

static u32 vm_mock_ring_pick_truth_id(void)
{
    vm_mock_ring_truth_load();
    if (g_vm_mock_ring_truth_count == 0)
        return 0;
    return 1u + (vm_mock_activities_rng_next() % g_vm_mock_ring_truth_count);
}

static bool vm_mock_activities_account_hex_of(const char *accountId, char *hexOut,
                                              size_t hexCap)
{
    char canonical[64];
    const char *id = accountId;
    vm_mock_service_account_record *record;

    if (accountId == NULL || accountId[0] == 0 || hexOut == NULL || hexCap == 0)
        return false;
    /*
     * accounts.account_id is ascii_bin.  Session bind stores the canonical
     * spelling, but account_state / typed login may still hold another case.
     * UNHEX is byte-exact, so vitality grant vs panel must share one key.
     */
    canonical[0] = 0;
    record = vm_mock_service_account_find_record(accountId);
    if (record != NULL && record->username[0] != 0)
        snprintf(canonical, sizeof(canonical), "%s", record->username);
    if (canonical[0] != 0)
        id = canonical;
    return vm_mysql_hex_encode(id, strlen(id), hexOut, hexCap) != 0;
}

static bool vm_mock_activities_account_hex(char *hexOut, size_t hexCap)
{
    return vm_mock_activities_account_hex_of(g_vm_mock_service_active_account_id,
                                             hexOut, hexCap);
}

static const char *vm_mock_activities_vitality_account_for_role(
    const vm_net_mock_role_state *role)
{
    vm_mock_service_client_session *session;

    if (role != NULL && role->roleId != 0)
    {
        for (session = g_vm_mock_service_client_sessions; session != NULL;
             session = session->next)
        {
            if (!session->roleOnline || session->accountId[0] == 0)
                continue;
            if (session->onlineRoleId == role->roleId)
                return session->accountId;
        }
    }
    return g_vm_mock_service_active_account_id;
}

static u32 vm_mock_activities_backpack_count(const vm_net_mock_role_state *role,
                                             u32 itemId)
{
    u32 total = 0;
    u32 i;
    if (role == NULL || itemId == 0)
        return 0;
    for (i = 0; i < role->backpackItemCount && i < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++i)
    {
        if (role->backpackItems[i].itemId == itemId)
            total += role->backpackItems[i].count;
    }
    return total;
}

static vm_net_mock_role_state *vm_mock_activities_role_by_id(u32 roleId)
{
    u32 i;

    if (roleId == 0)
        return NULL;
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid)
        return NULL;
    for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == roleId)
            return &g_vm_net_mock_role_db.roles[i];
    }
    return NULL;
}

static void vm_mock_activities_clip_binary_label(char *label, size_t maxBytes)
{
    size_t len;

    if (label == NULL || maxBytes < 2)
        return;
    len = strlen(label);
    if (len < maxBytes)
        return;
    label[maxBytes - 1] = 0;
    len = maxBytes - 1;
    while (len > 0 && ((unsigned char)label[len - 1] & 0xC0) == 0x80)
    {
        label[len - 1] = 0;
        --len;
    }
}

static bool vm_mock_activities_copy_row_label(char *dst, size_t dstCap,
                                              const char *src, size_t srcLen)
{
    size_t n;

    if (dst == NULL || dstCap == 0)
        return false;
    dst[0] = 0;
    if (src == NULL || srcLen == 0)
        return true;
    n = srcLen;
    if (n >= dstCap)
        n = dstCap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return true;
}

static bool vm_mock_activities_ensure_tables(void)
{
    static bool ready = false;
    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_vitality ("
            "account_id VARBINARY(63) NOT NULL,role_id INT UNSIGNED NOT NULL,"
            "vitality INT UNSIGNED NOT NULL DEFAULT 0,"
            "vitality_max INT UNSIGNED NOT NULL DEFAULT 10000,"
            "online_accrue_sec INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(account_id,role_id)) "
            "ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_vitality_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "online_per_hour INT UNSIGNED NOT NULL DEFAULT 10,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_festival_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "npc_keyword VARBINARY(32) NOT NULL DEFAULT '',"
            "welcome_text VARBINARY(128) NOT NULL DEFAULT '',"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_festival_exchange ("
            "exchange_id INT UNSIGNED NOT NULL,"
            "vitality_cost INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 1,"
            "label VARBINARY(48) NOT NULL DEFAULT '',"
            "sort_order SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(exchange_id)) "
            "ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_yanmen_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "scene_keyword VARBINARY(32) NOT NULL DEFAULT '',"
            "npc_keyword VARBINARY(32) NOT NULL DEFAULT '',"
            "wood_item_id INT UNSIGNED NOT NULL DEFAULT 3,"
            "stone_item_id INT UNSIGNED NOT NULL DEFAULT 5,"
            "wood_drop_min TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "wood_drop_max TINYINT UNSIGNED NOT NULL DEFAULT 3,"
            "stone_drop_min TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "stone_drop_max TINYINT UNSIGNED NOT NULL DEFAULT 2,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_yanmen_exchange ("
            "exchange_id INT UNSIGNED NOT NULL,"
            "wood_cost INT UNSIGNED NOT NULL DEFAULT 0,"
            "stone_cost INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 1,"
            "label VARBINARY(48) NOT NULL DEFAULT '',"
            "sort_order SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(exchange_id)) "
            "ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_ring_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "npc_keyword VARBINARY(32) NOT NULL DEFAULT '',"
            "daily_max SMALLINT UNSIGNED NOT NULL DEFAULT 20,"
            "daily_bonus_item_id INT UNSIGNED NOT NULL DEFAULT 905,"
            "daily_bonus_count INT UNSIGNED NOT NULL DEFAULT 1,"
            "key_drop_rate TINYINT UNSIGNED NOT NULL DEFAULT 5,"
            "key_item_id1 INT UNSIGNED NOT NULL DEFAULT 813,"
            "key_item_id2 INT UNSIGNED NOT NULL DEFAULT 814,"
            "key_item_id3 INT UNSIGNED NOT NULL DEFAULT 815,"
            "base_exp INT UNSIGNED NOT NULL DEFAULT 500,"
            "base_money INT UNSIGNED NOT NULL DEFAULT 1000,"
            "exp_per_ring INT UNSIGNED NOT NULL DEFAULT 200,"
            "money_per_ring INT UNSIGNED NOT NULL DEFAULT 500,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_ring_daily ("
            "account_id VARBINARY(63) NOT NULL,role_id INT UNSIGNED NOT NULL,"
            "day_ymd INT UNSIGNED NOT NULL,ring_count SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "bonus_claimed TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "task_type TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "target_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "target_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "progress INT UNSIGNED NOT NULL DEFAULT 0,"
            "target_npc VARBINARY(16) NOT NULL DEFAULT '',"
            "task_active TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(account_id,role_id,day_ymd)) "
            "ENGINE=InnoDB"))
        return false;
    /* Soft migrate: old rows kept vitality_max=200; raise floor to current default. */
    (void)vm_mysql_exec(
        "ALTER TABLE account_role_vitality "
        "MODIFY COLUMN vitality INT UNSIGNED NOT NULL DEFAULT 0,"
        "MODIFY COLUMN vitality_max INT UNSIGNED NOT NULL DEFAULT 10000");
    (void)vm_mysql_exec(
        "ALTER TABLE account_role_vitality ADD COLUMN online_accrue_sec "
        "INT UNSIGNED NOT NULL DEFAULT 0 AFTER vitality_max");
    (void)vm_mysql_exec(
        "UPDATE account_role_vitality SET vitality_max=10000 "
        "WHERE vitality_max < 10000");
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_vitality_config(id,online_per_hour) "
        "VALUES(1,10)");
    ready = true;
    return true;
}

static void vm_mock_activities_seed_defaults(void)
{
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_festival_config(id,enabled,npc_keyword) "
        "VALUES(1,1,X'" "BDDA" "C8D5" "CAB9" "D5DF" "')");
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_festival_exchange "
        "(exchange_id,vitality_cost,item_id,item_count,label,sort_order,enabled) VALUES "
        "(1,50,813,1,X'C3" "F7" "BE" "A7" "D4" "BF" "B3" "D7',1,1),"
        "(2,100,905,1,X'CE" "E5" "BC" "B6" "D0" "FE" "BE" "A7',2,1),"
        "(3,200,907,1,X'C6" "DF" "BC" "B6" "D0" "FE" "BE" "A7',3,1)");
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_yanmen_config(id,enabled,scene_keyword,npc_keyword,"
        "wood_item_id,stone_item_id) VALUES(1,1,X'" "D1E3" "C3C5" "B9D8" "',X'" "B9D8" "B0AC" "CAB9" "D5DF" "',3,5)");
    /* 旧默认 850/851 不在 item.dsh，迁移到 3木材 / 5石料 */
    (void)vm_mysql_exec(
        "UPDATE server_yanmen_config SET wood_item_id=3 WHERE id=1 AND wood_item_id=850");
    (void)vm_mysql_exec(
        "UPDATE server_yanmen_config SET stone_item_id=5 WHERE id=1 AND stone_item_id=851");
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_yanmen_exchange "
        "(exchange_id,wood_cost,stone_cost,item_id,item_count,label,sort_order,enabled) VALUES "
        "(1,5,5,813,1,X'C3" "F7" "BE" "A7" "D4" "BF" "B3" "D7',1,1),"
        "(2,10,10,905,1,X'CE" "E5" "BC" "B6" "D0" "FE" "BE" "A7',2,1),"
        "(3,20,20,907,1,X'C7" "DF" "BC" "B6" "D0" "FE" "BE" "A7',3,1)");
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_ring_config(id,enabled,npc_keyword,daily_max,"
        "daily_bonus_item_id) VALUES(1,1,X'" "C5DC" "BB7" "CAB9" "D5DF" "',20,905)");
}

static bool vm_mock_festival_config_row(void *ctx, unsigned int cc,
                                        const char *const *values,
                                        const size_t *lengths)
{
    vm_mock_festival_config *cfg = (vm_mock_festival_config *)ctx;
    u32 tmp = 0;
    (void)cc;
    if (cfg == NULL || values == NULL)
        return false;
    if (values[0] && lengths[0])
        cfg->enabled = (u8)(values[0][0] == '0' ? 0 : 1);
    if (values[1] && lengths[1] > 0 && lengths[1] < sizeof(cfg->npcKeyword))
    {
        memcpy(cfg->npcKeyword, values[1], lengths[1]);
        cfg->npcKeyword[lengths[1]] = 0;
    }
    if (values[2] && lengths[2] > 0 && lengths[2] < sizeof(cfg->welcomeText))
    {
        memcpy(cfg->welcomeText, values[2], lengths[2]);
        cfg->welcomeText[lengths[2]] = 0;
    }
    return true;
}

static bool vm_mock_festival_exchange_row(void *ctx, unsigned int cc,
                                          const char *const *values,
                                          const size_t *lengths)
{
    (void)ctx;
    (void)cc;
    if (values == NULL || g_vm_mock_festival_exchange_count >=
                              VM_MOCK_ACTIVITIES_EXCHANGE_MAX)
        return false;
    {
        vm_mock_festival_exchange *row =
            &g_vm_mock_festival_exchanges[g_vm_mock_festival_exchange_count++];
        u32 tmp = 0;
        memset(row, 0, sizeof(*row));
        row->enabled = 1;
        row->itemCount = 1;
        if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
            row->exchangeId = tmp;
        if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
            row->vitalityCost = tmp;
        if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
            row->itemId = tmp;
        if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
            row->itemCount = tmp ? tmp : 1;
        if (values[4] && lengths[4] > 0)
            (void)vm_mock_activities_copy_row_label(row->label, sizeof(row->label),
                                                     values[4], lengths[4]);
        if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
            row->sortOrder = (u16)tmp;
        if (values[6] && lengths[6])
            row->enabled = (u8)(values[6][0] == '0' ? 0 : 1);
    }
    return true;
}

static void vm_mock_festival_load_config(bool force)
{
    if (g_vm_mock_festival_config_loaded && !force)
        return;
    (void)vm_mock_activities_ensure_tables();
    vm_mock_activities_seed_defaults();
    memset(&g_vm_mock_festival_config, 0, sizeof(g_vm_mock_festival_config));
    g_vm_mock_festival_config.enabled = 1;
    snprintf(g_vm_mock_festival_config.npcKeyword,
             sizeof(g_vm_mock_festival_config.npcKeyword), "%s",
             VM_MOCK_FESTIVAL_NPC_KEYWORD_DEFAULT);
    g_vm_mock_festival_exchange_count = 0;
    (void)vm_mysql_query(
        "SELECT enabled,npc_keyword,welcome_text FROM server_festival_config "
        "WHERE id=1 LIMIT 1",
        vm_mock_festival_config_row, &g_vm_mock_festival_config);
    (void)vm_mysql_query(
        "SELECT exchange_id,vitality_cost,item_id,item_count,label,sort_order,"
        "enabled FROM server_festival_exchange ORDER BY sort_order,exchange_id",
        vm_mock_festival_exchange_row, NULL);
    g_vm_mock_festival_config_loaded = true;
}

static bool vm_mock_yanmen_config_row(void *ctx, unsigned int cc,
                                      const char *const *values,
                                      const size_t *lengths)
{
    vm_mock_yanmen_config *cfg = (vm_mock_yanmen_config *)ctx;
    u32 tmp = 0;
    (void)cc;
    if (cfg == NULL || values == NULL)
        return false;
    if (values[0] && lengths[0])
        cfg->enabled = (u8)(values[0][0] == '0' ? 0 : 1);
    if (values[1] && lengths[1] > 0 && lengths[1] < sizeof(cfg->sceneKeyword))
    {
        memcpy(cfg->sceneKeyword, values[1], lengths[1]);
        cfg->sceneKeyword[lengths[1]] = 0;
    }
    if (values[2] && lengths[2] > 0 && lengths[2] < sizeof(cfg->npcKeyword))
    {
        memcpy(cfg->npcKeyword, values[2], lengths[2]);
        cfg->npcKeyword[lengths[2]] = 0;
    }
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        cfg->woodItemId = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        cfg->stoneItemId = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        cfg->woodDropMin = (u8)tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        cfg->woodDropMax = (u8)tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        cfg->stoneDropMin = (u8)tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        cfg->stoneDropMax = (u8)tmp;
    return true;
}

static bool vm_mock_yanmen_exchange_row(void *ctx, unsigned int cc,
                                        const char *const *values,
                                        const size_t *lengths)
{
    (void)ctx;
    (void)cc;
    if (values == NULL || g_vm_mock_yanmen_exchange_count >=
                              VM_MOCK_ACTIVITIES_EXCHANGE_MAX)
        return false;
    {
        vm_mock_yanmen_exchange *row =
            &g_vm_mock_yanmen_exchanges[g_vm_mock_yanmen_exchange_count++];
        u32 tmp = 0;
        memset(row, 0, sizeof(*row));
        row->enabled = 1;
        row->itemCount = 1;
        if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
            row->exchangeId = tmp;
        if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
            row->woodCost = tmp;
        if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
            row->stoneCost = tmp;
        if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
            row->itemId = tmp;
        if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
            row->itemCount = tmp ? tmp : 1;
        if (values[5] && lengths[5] > 0)
            (void)vm_mock_activities_copy_row_label(row->label, sizeof(row->label),
                                                     values[5], lengths[5]);
        if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
            row->sortOrder = (u16)tmp;
        if (values[7] && lengths[7])
            row->enabled = (u8)(values[7][0] == '0' ? 0 : 1);
    }
    return true;
}

static void vm_mock_yanmen_load_config(bool force)
{
    if (g_vm_mock_yanmen_config_loaded && !force)
        return;
    (void)vm_mock_activities_ensure_tables();
    vm_mock_activities_seed_defaults();
    memset(&g_vm_mock_yanmen_config, 0, sizeof(g_vm_mock_yanmen_config));
    g_vm_mock_yanmen_config.enabled = 1;
    g_vm_mock_yanmen_config.woodItemId = 3;
    g_vm_mock_yanmen_config.stoneItemId = 5;
    g_vm_mock_yanmen_config.woodDropMin = 1;
    g_vm_mock_yanmen_config.woodDropMax = 3;
    g_vm_mock_yanmen_config.stoneDropMin = 1;
    g_vm_mock_yanmen_config.stoneDropMax = 2;
    snprintf(g_vm_mock_yanmen_config.sceneKeyword,
             sizeof(g_vm_mock_yanmen_config.sceneKeyword), "%s",
             VM_MOCK_YANMEN_SCENE_KEYWORD_DEFAULT);
    snprintf(g_vm_mock_yanmen_config.npcKeyword,
             sizeof(g_vm_mock_yanmen_config.npcKeyword), "%s",
             VM_MOCK_YANMEN_NPC_KEYWORD_DEFAULT);
    g_vm_mock_yanmen_exchange_count = 0;
    (void)vm_mysql_query(
        "SELECT enabled,scene_keyword,npc_keyword,wood_item_id,stone_item_id,"
        "wood_drop_min,wood_drop_max,stone_drop_min,stone_drop_max "
        "FROM server_yanmen_config WHERE id=1 LIMIT 1",
        vm_mock_yanmen_config_row, &g_vm_mock_yanmen_config);
    (void)vm_mysql_query(
        "SELECT exchange_id,wood_cost,stone_cost,item_id,item_count,label,"
        "sort_order,enabled FROM server_yanmen_exchange "
        "ORDER BY sort_order,exchange_id",
        vm_mock_yanmen_exchange_row, NULL);
    g_vm_mock_yanmen_config_loaded = true;
}

static bool vm_mock_ring_config_row(void *ctx, unsigned int cc,
                                    const char *const *values,
                                    const size_t *lengths)
{
    vm_mock_ring_config *cfg = (vm_mock_ring_config *)ctx;
    u32 tmp = 0;
    (void)cc;
    if (cfg == NULL || values == NULL)
        return false;
    if (values[0] && lengths[0])
        cfg->enabled = (u8)(values[0][0] == '0' ? 0 : 1);
    if (values[1] && lengths[1] > 0 && lengths[1] < sizeof(cfg->npcKeyword))
    {
        memcpy(cfg->npcKeyword, values[1], lengths[1]);
        cfg->npcKeyword[lengths[1]] = 0;
    }
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        cfg->dailyMax = (u16)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        cfg->dailyBonusItemId = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        cfg->dailyBonusCount = tmp ? tmp : 1;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        cfg->keyDropRate = (u8)tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        cfg->keyItemIds[0] = tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        cfg->keyItemIds[1] = tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        cfg->keyItemIds[2] = tmp;
    if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
        cfg->baseExp = tmp;
    if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
        cfg->baseMoney = tmp;
    if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
        cfg->expPerRing = tmp;
    if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
        cfg->moneyPerRing = tmp;
    return true;
}

static void vm_mock_ring_load_config(bool force)
{
    if (g_vm_mock_ring_config_loaded && !force)
        return;
    (void)vm_mock_activities_ensure_tables();
    vm_mock_activities_seed_defaults();
    memset(&g_vm_mock_ring_config, 0, sizeof(g_vm_mock_ring_config));
    g_vm_mock_ring_config.enabled = 1;
    g_vm_mock_ring_config.dailyMax = VM_MOCK_RING_DAILY_MAX_DEFAULT;
    g_vm_mock_ring_config.dailyBonusItemId = 905;
    g_vm_mock_ring_config.dailyBonusCount = 1;
    g_vm_mock_ring_config.keyDropRate = VM_MOCK_RING_KEY_RATE_DEFAULT;
    g_vm_mock_ring_config.keyItemIds[0] = 813;
    g_vm_mock_ring_config.keyItemIds[1] = 814;
    g_vm_mock_ring_config.keyItemIds[2] = 815;
    g_vm_mock_ring_config.baseExp = 500;
    g_vm_mock_ring_config.baseMoney = 1000;
    g_vm_mock_ring_config.expPerRing = 200;
    g_vm_mock_ring_config.moneyPerRing = 500;
    snprintf(g_vm_mock_ring_config.npcKeyword,
             sizeof(g_vm_mock_ring_config.npcKeyword), "%s",
             VM_MOCK_RING_NPC_KEYWORD_DEFAULT);
    (void)vm_mysql_query(
        "SELECT enabled,npc_keyword,daily_max,daily_bonus_item_id,"
        "daily_bonus_count,key_drop_rate,key_item_id1,key_item_id2,key_item_id3,"
        "base_exp,base_money,exp_per_ring,money_per_ring "
        "FROM server_ring_config WHERE id=1 LIMIT 1",
        vm_mock_ring_config_row, &g_vm_mock_ring_config);
    g_vm_mock_ring_config_loaded = true;
}

static bool vm_mock_activities_npc_match(const char *npcName, const char *keyword)
{
    if (npcName == NULL || npcName[0] == 0 || keyword == NULL || keyword[0] == 0)
        return false;
    return strstr(npcName, keyword) != NULL;
}

bool vm_mock_festival_npc_offers(const char *npcName)
{
    vm_mock_festival_load_config(false);
    if (!g_vm_mock_festival_config.enabled)
        return false;
    return vm_mock_activities_npc_match(npcName,
                                        g_vm_mock_festival_config.npcKeyword);
}

bool vm_mock_yanmen_npc_offers(const char *npcName)
{
    vm_mock_yanmen_load_config(false);
    if (!g_vm_mock_yanmen_config.enabled)
        return false;
    return vm_mock_activities_npc_match(npcName, g_vm_mock_yanmen_config.npcKeyword);
}

bool vm_mock_ring_npc_offers(const char *npcName)
{
    vm_mock_ring_load_config(false);
    if (!g_vm_mock_ring_config.enabled)
        return false;
    return vm_mock_activities_npc_match(npcName, g_vm_mock_ring_config.npcKeyword);
}

typedef struct
{
    u32 vitality;
    u32 vitalityMax;
    u32 onlineAccrueSec;
    bool found;
} vm_mock_vitality_row_ctx;

static bool vm_mock_vitality_row_cb(void *ctx, unsigned int cc,
                                    const char *const *values,
                                    const size_t *lengths)
{
    vm_mock_vitality_row_ctx *row = (vm_mock_vitality_row_ctx *)ctx;
    (void)cc;
    if (row == NULL || values == NULL)
        return false;
    row->found = true;
    if (values[0] && !vm_mock_mysql_parse_u32(values[0], lengths[0], &row->vitality))
        row->vitality = VM_MOCK_ACTIVITIES_VITALITY_DEFAULT;
    if (values[1] && !vm_mock_mysql_parse_u32(values[1], lengths[1], &row->vitalityMax))
        row->vitalityMax = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
    if (values[2] && lengths[2] &&
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &row->onlineAccrueSec))
        row->onlineAccrueSec = 0;
    return true;
}

/*
 * ascii_bin 大小写不一致时，发放可能写在另一行。面板读规范键若仍是 0，
 * 把同角色大小写折叠命中的活力/进度并回规范键，并删掉孤儿行。
 */
static void vm_mock_activities_vitality_merge_case_orphans(const char *accountHex,
                                                           u32 roleId,
                                                           vm_mock_vitality_row_ctx *row)
{
    vm_mock_vitality_row_ctx fold;
    char query[640];

    if (accountHex == NULL || accountHex[0] == 0 || roleId == 0 || row == NULL)
        return;
    memset(&fold, 0, sizeof(fold));
    snprintf(query, sizeof(query),
             "SELECT vitality,vitality_max,online_accrue_sec FROM "
             "account_role_vitality WHERE role_id=%u AND "
             "LOWER(CAST(account_id AS CHAR CHARACTER SET ascii))=LOWER(CAST(UNHEX('%s') "
             "AS CHAR CHARACTER SET ascii)) AND account_id<>UNHEX('%s') "
             "ORDER BY vitality DESC,online_accrue_sec DESC LIMIT 1",
             roleId, accountHex, accountHex);
    (void)vm_mysql_query(query, vm_mock_vitality_row_cb, &fold);
    if (!fold.found)
        return;
    if (fold.vitality > row->vitality)
        row->vitality = fold.vitality;
    if (fold.vitalityMax > row->vitalityMax)
        row->vitalityMax = fold.vitalityMax;
    if (fold.onlineAccrueSec > row->onlineAccrueSec)
        row->onlineAccrueSec = fold.onlineAccrueSec;
    if (row->vitalityMax < (u32)VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT)
        row->vitalityMax = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
    row->found = true;
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_vitality "
             "(account_id,role_id,vitality,vitality_max,online_accrue_sec) VALUES "
             "(UNHEX('%s'),%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "vitality=GREATEST(vitality,VALUES(vitality)),"
             "vitality_max=GREATEST(vitality_max,VALUES(vitality_max)),"
             "online_accrue_sec=GREATEST(online_accrue_sec,VALUES(online_accrue_sec))",
             accountHex, roleId, row->vitality, row->vitalityMax,
             row->onlineAccrueSec);
    if (!vm_mysql_exec(query))
        return;
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_vitality WHERE role_id=%u AND "
             "LOWER(CAST(account_id AS CHAR CHARACTER SET ascii))=LOWER(CAST(UNHEX('%s') "
             "AS CHAR CHARACTER SET ascii)) AND account_id<>UNHEX('%s')",
             roleId, accountHex, accountHex);
    (void)vm_mysql_exec(query);
    printf("[info][mock-service] vitality_case_orphan_merged role=%u "
           "vitality=%u accrue=%u evidence=ascii-bin-account-key\n",
           roleId, row->vitality, row->onlineAccrueSec);
}

static void vm_mock_activities_vitality_load_of(const char *accountId, u32 roleId,
                                               u32 *vitalityOut, u32 *maxOut)
{
    char accountHex[160];
    char query[512];
    vm_mock_vitality_row_ctx row;

    if (vitalityOut)
        *vitalityOut = VM_MOCK_ACTIVITIES_VITALITY_DEFAULT;
    if (maxOut)
        *maxOut = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
    if (accountId == NULL || accountId[0] == 0 || roleId == 0)
        return;
    (void)vm_mock_activities_ensure_tables();
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return;
    memset(&row, 0, sizeof(row));
    row.vitality = VM_MOCK_ACTIVITIES_VITALITY_DEFAULT;
    row.vitalityMax = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
    snprintf(query, sizeof(query),
             "SELECT vitality,vitality_max,online_accrue_sec FROM "
             "account_role_vitality "
             "WHERE account_id=UNHEX('%s') AND role_id=%u LIMIT 1",
             accountHex, roleId);
    (void)vm_mysql_query(query, vm_mock_vitality_row_cb, &row);
    if (!row.found || row.vitality == 0)
        vm_mock_activities_vitality_merge_case_orphans(accountHex, roleId, &row);
    if (!row.found)
    {
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO account_role_vitality "
                 "(account_id,role_id,vitality,vitality_max) "
                 "VALUES(UNHEX('%s'),%u,%u,%u)",
                 accountHex, roleId, VM_MOCK_ACTIVITIES_VITALITY_DEFAULT,
                 VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT);
        (void)vm_mysql_exec(query);
        memset(&row, 0, sizeof(row));
        row.vitality = VM_MOCK_ACTIVITIES_VITALITY_DEFAULT;
        row.vitalityMax = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
        snprintf(query, sizeof(query),
                 "SELECT vitality,vitality_max,online_accrue_sec FROM "
                 "account_role_vitality "
                 "WHERE account_id=UNHEX('%s') AND role_id=%u LIMIT 1",
                 accountHex, roleId);
        (void)vm_mysql_query(query, vm_mock_vitality_row_cb, &row);
        if (!row.found || row.vitality == 0)
            vm_mock_activities_vitality_merge_case_orphans(accountHex, roleId, &row);
    }
    else if (row.vitalityMax < (u32)VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT)
    {
        row.vitalityMax = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
        snprintf(query, sizeof(query),
                 "UPDATE account_role_vitality SET vitality_max=%u "
                 "WHERE account_id=UNHEX('%s') AND role_id=%u",
                 row.vitalityMax, accountHex, roleId);
        (void)vm_mysql_exec(query);
    }
    if (vitalityOut)
        *vitalityOut = row.vitality;
    if (maxOut)
        *maxOut = row.vitalityMax;
}

static bool vm_mock_activities_vitality_save_of(const char *accountId, u32 roleId,
                                               u32 vitality, u32 vitalityMax)
{
    char accountHex[160];
    char query[512];
    if (accountId == NULL || accountId[0] == 0 || roleId == 0)
        return false;
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_vitality "
             "(account_id,role_id,vitality,vitality_max) VALUES "
             "(UNHEX('%s'),%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE vitality=VALUES(vitality),"
             "vitality_max=VALUES(vitality_max)",
             accountHex, roleId, vitality, vitalityMax);
    return vm_mysql_exec(query);
}

static void vm_mock_activities_vitality_load(vm_net_mock_role_state *role,
                                             u32 *vitalityOut, u32 *maxOut)
{
    const char *accountId = vm_mock_activities_vitality_account_for_role(role);
    if (role == NULL)
    {
        if (vitalityOut)
            *vitalityOut = VM_MOCK_ACTIVITIES_VITALITY_DEFAULT;
        if (maxOut)
            *maxOut = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
        return;
    }
    vm_mock_activities_vitality_load_of(accountId, role->roleId, vitalityOut,
                                        maxOut);
}

static bool vm_mock_activities_vitality_save(vm_net_mock_role_state *role,
                                             u32 vitality, u32 vitalityMax)
{
    if (role == NULL)
        return false;
    return vm_mock_activities_vitality_save_of(
        vm_mock_activities_vitality_account_for_role(role), role->roleId,
        vitality, vitalityMax);
}

void vm_mock_activities_vitality_wire(vm_net_mock_role_state *role,
                                      u32 *energyOut, u32 *energyMaxOut)
{
    u32 v = VM_MOCK_ACTIVITIES_VITALITY_DEFAULT;
    u32 m = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
    vm_mock_activities_vitality_load(role, &v, &m);
    if (energyOut)
        *energyOut = v;
    if (energyMaxOut)
        *energyMaxOut = m;
}

/* 按指定账号写活力（在线 tick 等跨会话路径禁止用 active account）. */
static bool vm_mock_activities_vitality_add_of(const char *accountId, u32 roleId,
                                              u32 add)
{
    u32 v;
    u32 m;
    if (accountId == NULL || accountId[0] == 0 || roleId == 0 || add == 0)
        return false;
    vm_mock_activities_vitality_load_of(accountId, roleId, &v, &m);
    v = vm_net_mock_add_capped_u32(v, add);
    if (v > m)
        v = m;
    return vm_mock_activities_vitality_save_of(accountId, roleId, v, m);
}

static bool vm_mock_activities_vitality_add(vm_net_mock_role_state *role, u32 add)
{
    if (role == NULL || add == 0)
        return false;
    return vm_mock_activities_vitality_add_of(
        vm_mock_activities_vitality_account_for_role(role), role->roleId, add);
}

static bool vm_mock_activities_vitality_spend(vm_net_mock_role_state *role, u32 cost)
{
    u32 v;
    u32 m;
    if (role == NULL || cost == 0)
        return false;
    vm_mock_activities_vitality_load(role, &v, &m);
    if (v < cost)
        return false;
    v -= cost;
    return vm_mock_activities_vitality_save(role, v, m);
}

static bool vm_mock_vitality_online_config_row(void *ctx, unsigned int cc,
                                              const char *const *values,
                                              const size_t *lengths)
{
    u32 *perHour = (u32 *)ctx;
    u32 tmp = 0;

    (void)cc;
    if (perHour == NULL || values == NULL || lengths == NULL)
        return false;
    if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        *perHour = tmp;
    return true;
}

static void vm_mock_vitality_online_load_config(bool force)
{
    u32 perHour = VM_MOCK_ACTIVITIES_ONLINE_VITALITY_PER_HOUR_DEFAULT;

    if (g_vm_mock_vitality_online_config_loaded && !force)
        return;
    (void)vm_mock_activities_ensure_tables();
    (void)vm_mysql_query(
        "SELECT online_per_hour FROM server_vitality_config WHERE id=1 LIMIT 1",
        vm_mock_vitality_online_config_row, &perHour);
    g_vm_mock_vitality_online_per_hour = perHour;
    g_vm_mock_vitality_online_config_loaded = true;
}

u32 vm_mock_vitality_online_per_hour_get(void)
{
    vm_mock_vitality_online_load_config(false);
    return g_vm_mock_vitality_online_per_hour;
}

bool vm_mock_vitality_admin_save_online_per_hour(u32 perHour)
{
    char query[192];

    if (!vm_mock_activities_ensure_tables())
        return false;
    if (perHour > 100000u)
        perHour = 100000u;
    snprintf(query, sizeof(query),
             "INSERT INTO server_vitality_config(id,online_per_hour) VALUES(1,%u) "
             "ON DUPLICATE KEY UPDATE online_per_hour=VALUES(online_per_hour)",
             perHour);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_vitality_online_per_hour = perHour;
    g_vm_mock_vitality_online_config_loaded = true;
    return true;
}

static bool vm_mock_activities_vitality_load_accrue(const char *accountId,
                                                    u32 roleId, u32 *accrueOut)
{
    char accountHex[160];
    char query[512];
    vm_mock_vitality_row_ctx row;

    if (accrueOut)
        *accrueOut = 0;
    if (accountId == NULL || accountId[0] == 0 || roleId == 0 || accrueOut == NULL)
        return false;
    (void)vm_mock_activities_ensure_tables();
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return false;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT vitality,vitality_max,online_accrue_sec FROM "
             "account_role_vitality WHERE account_id=UNHEX('%s') AND role_id=%u "
             "LIMIT 1",
             accountHex, roleId);
    (void)vm_mysql_query(query, vm_mock_vitality_row_cb, &row);
    if (!row.found)
        return true;
    /* 保留可能未发放的整小时进度；由 begin/tick/end 负责发放后再取余. */
    *accrueOut = row.onlineAccrueSec;
    return true;
}

static bool vm_mock_activities_vitality_save_accrue(const char *accountId,
                                                    u32 roleId, u32 accrueSec)
{
    char accountHex[160];
    char query[512];

    if (accountId == NULL || accountId[0] == 0 || roleId == 0)
        return false;
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_vitality "
             "(account_id,role_id,vitality,vitality_max,online_accrue_sec) VALUES "
             "(UNHEX('%s'),%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "online_accrue_sec=VALUES(online_accrue_sec)",
             accountHex, roleId, VM_MOCK_ACTIVITIES_VITALITY_DEFAULT,
             VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT, accrueSec);
    return vm_mysql_exec(query);
}

/* 满小时发放到 session 本人账号；成功才消耗 bank 中的整小时. */
static bool vm_mock_activities_vitality_online_try_grant(
    vm_mock_service_client_session *session, u32 *bankInOut, u32 perHour)
{
    u32 bank;
    u32 hours;
    u32 grant;
    u32 remainder;
    u32 before = 0;
    u32 after = 0;
    u32 vmax = 0;
    u32 expected = 0;

    if (session == NULL || bankInOut == NULL || perHour == 0)
        return false;
    if (session->accountId[0] == 0 || session->onlineRoleId == 0)
        return false;
    bank = *bankInOut;
    hours = bank / (u32)VM_MOCK_ACTIVITIES_ONLINE_HOUR_SEC;
    if (hours == 0)
        return false;
    remainder = bank % (u32)VM_MOCK_ACTIVITIES_ONLINE_HOUR_SEC;
    grant = hours * perHour;
    vm_mock_activities_vitality_load_of(session->accountId, session->onlineRoleId,
                                        &before, &vmax);
    if (!vm_mock_activities_vitality_add_of(session->accountId,
                                           session->onlineRoleId, grant))
    {
        printf("[warn][mock-service] vitality_online_grant_fail role=%u "
               "hours=%u add=%u account=%s evidence=account-scoped-write\n",
               session->onlineRoleId, hours, grant,
               session->accountId[0] ? session->accountId : "-");
        return false;
    }
    vm_mock_activities_vitality_load_of(session->accountId, session->onlineRoleId,
                                        &after, &vmax);
    expected = vm_net_mock_add_capped_u32(before, grant);
    if (vmax != 0 && expected > vmax)
        expected = vmax;
    if (after != expected)
    {
        printf("[warn][mock-service] vitality_online_grant_mismatch role=%u "
               "hours=%u add=%u before=%u after=%u expected=%u account=%s "
               "evidence=panel-key-must-match-grant\n",
               session->onlineRoleId, hours, grant, before, after, expected,
               session->accountId[0] ? session->accountId : "-");
        /* 回读仍是发放前的值：键没写上，保留整小时下次再发；已写上则继续提示. */
        if (after == before && before < vmax)
            return false;
    }
    *bankInOut = remainder;
    session->vitalityOnlineAccrueSec = remainder;
    (void)vm_mock_activities_vitality_save_accrue(
        session->accountId, session->onlineRoleId, remainder);
    printf("[info][mock-service] vitality_online_grant role=%u hours=%u "
           "add=%u per_hour=%u bank=%u account=%s\n",
           session->onlineRoleId, hours, grant, perHour, remainder,
           session->accountId[0] ? session->accountId : "-");
    {
        char tip[64];
        /* 在线满时获得活力：N */
        snprintf(tip, sizeof(tip),
                 "\xd4\xda\xcf\xdf\xc2\xfa\xca\xb1\xbb\xf1\xb5\xc3\xbb\xee\xc1\xa6\xa3\xba%u",
                 grant);
        (void)vm_mock_service_session_enqueue_system_message(session, tip);
    }
    return true;
}

static void vm_mock_activities_vitality_online_session_begin(
    vm_mock_service_client_session *session)
{
    u32 accrue = 0;
    u32 now = (u32)time(NULL);
    u32 perHour;

    if (session == NULL || session->accountId[0] == 0 || session->onlineRoleId == 0)
        return;
    (void)vm_mock_activities_vitality_load_accrue(session->accountId,
                                                  session->onlineRoleId, &accrue);
    session->vitalityOnlineAccrueSec = accrue;
    session->vitalityOnlineLastUnix = now ? now : 1;
    /* 上次下线未发出的整小时：上线立刻补发. */
    vm_mock_vitality_online_load_config(false);
    perHour = g_vm_mock_vitality_online_per_hour;
    if (perHour != 0 &&
        session->vitalityOnlineAccrueSec >= (u32)VM_MOCK_ACTIVITIES_ONLINE_HOUR_SEC)
    {
        u32 bank = session->vitalityOnlineAccrueSec;
        (void)vm_mock_activities_vitality_online_try_grant(session, &bank, perHour);
        session->vitalityOnlineAccrueSec = bank;
    }
}

static void vm_mock_activities_vitality_online_session_end(
    vm_mock_service_client_session *session)
{
    u32 now = (u32)time(NULL);
    u32 perHour;

    if (session == NULL || session->vitalityOnlineLastUnix == 0)
        return;
    if (session->accountId[0] != 0 && session->onlineRoleId != 0)
    {
        if (now > session->vitalityOnlineLastUnix)
        {
            u32 delta = now - session->vitalityOnlineLastUnix;
            session->vitalityOnlineAccrueSec =
                vm_net_mock_add_capped_u32(session->vitalityOnlineAccrueSec, delta);
        }
        vm_mock_vitality_online_load_config(false);
        perHour = g_vm_mock_vitality_online_per_hour;
        if (perHour != 0 &&
            session->vitalityOnlineAccrueSec >=
                (u32)VM_MOCK_ACTIVITIES_ONLINE_HOUR_SEC)
        {
            u32 bank = session->vitalityOnlineAccrueSec;
            (void)vm_mock_activities_vitality_online_try_grant(session, &bank,
                                                              perHour);
            session->vitalityOnlineAccrueSec = bank;
        }
        else if (session->vitalityOnlineAccrueSec >=
                 (u32)VM_MOCK_ACTIVITIES_ONLINE_HOUR_SEC)
        {
            /* 发放关闭：只保留不足 1 小时的余数，避免进度膨胀. */
            session->vitalityOnlineAccrueSec %=
                (u32)VM_MOCK_ACTIVITIES_ONLINE_HOUR_SEC;
        }
        (void)vm_mock_activities_vitality_save_accrue(
            session->accountId, session->onlineRoleId,
            session->vitalityOnlineAccrueSec);
    }
    session->vitalityOnlineAccrueSec = 0;
    session->vitalityOnlineLastUnix = 0;
}

static void vm_mock_activities_vitality_online_tick(void)
{
    static u32 s_lastTickUnix = 0;
    u32 now = (u32)time(NULL);
    u32 perHour;
    vm_mock_service_client_session *session;

    if (now == 0)
        return;
    if (s_lastTickUnix != 0 && now <= s_lastTickUnix)
        return;
    s_lastTickUnix = now;

    vm_mock_vitality_online_load_config(false);
    perHour = g_vm_mock_vitality_online_per_hour;

    for (session = g_vm_mock_service_client_sessions; session != NULL;
         session = session->next)
    {
        u32 delta;
        u32 bank;

        if (!session->roleOnline || session->accountId[0] == 0 ||
            session->onlineRoleId == 0)
            continue;
        if (session->vitalityOnlineLastUnix == 0)
        {
            vm_mock_activities_vitality_online_session_begin(session);
            continue;
        }
        if (now <= session->vitalityOnlineLastUnix)
            continue;
        delta = now - session->vitalityOnlineLastUnix;
        session->vitalityOnlineLastUnix = now;
        if (perHour == 0)
        {
            /* 关闭发放时仍推进时钟，避免后台重新开启后一次性补发. */
            continue;
        }
        bank = vm_net_mock_add_capped_u32(session->vitalityOnlineAccrueSec, delta);
        if (bank < (u32)VM_MOCK_ACTIVITIES_ONLINE_HOUR_SEC)
        {
            session->vitalityOnlineAccrueSec = bank;
            continue;
        }
        /* 成功才消耗整小时；失败保留 bank，下次再发. */
        if (!vm_mock_activities_vitality_online_try_grant(session, &bank, perHour))
        {
            session->vitalityOnlineAccrueSec = bank;
            continue;
        }
        session->vitalityOnlineAccrueSec = bank;
    }
}

void vm_mock_activities_arena_round_vitality(vm_mock_service_duel *duel)
{
    u32 i;
    if (duel == NULL || !duel->arenaMode)
        return;
    for (i = 0; i < 2u; ++i)
    {
        vm_mock_service_client_session *session =
            vm_mock_service_find_client_session(duel->clientIds[i]);
        vm_net_mock_role_state *role =
            session ? vm_mock_service_trade_role_for_session(session, NULL) : NULL;
        if (role != NULL)
            (void)vm_mock_activities_vitality_add(
                role, VM_MOCK_ACTIVITIES_ARENA_VITALITY_PER_ROUND);
    }
}

static void vm_mock_activities_on_ladder_victory(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    (void)vm_mock_activities_vitality_add(
        role, VM_MOCK_ACTIVITIES_LADDER_VITALITY_REWARD);
}

static bool vm_mock_activities_scene_is_yanmen(const char *scene)
{
    vm_mock_yanmen_load_config(false);
    if (scene == NULL || !g_vm_mock_yanmen_config.enabled)
        return false;
    if (g_vm_mock_yanmen_config.sceneKeyword[0] == 0)
        return false;
    return strstr(scene, g_vm_mock_yanmen_config.sceneKeyword) != NULL;
}

void vm_mock_activities_on_pve_victory(const char *scene, u32 enemyId, bool isBoss)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 wood;
    u32 stone;
    u16 seq = 0;
    u32 spanWood;
    u32 spanStone;

    (void)enemyId;
    (void)isBoss;
    if (role == NULL || scene == NULL || !vm_mock_activities_scene_is_yanmen(scene))
        return;
    vm_mock_yanmen_load_config(false);
    spanWood = g_vm_mock_yanmen_config.woodDropMax >= g_vm_mock_yanmen_config.woodDropMin
                   ? (u32)(g_vm_mock_yanmen_config.woodDropMax -
                           g_vm_mock_yanmen_config.woodDropMin + 1)
                   : 1u;
    spanStone =
        g_vm_mock_yanmen_config.stoneDropMax >= g_vm_mock_yanmen_config.stoneDropMin
            ? (u32)(g_vm_mock_yanmen_config.stoneDropMax -
                    g_vm_mock_yanmen_config.stoneDropMin + 1)
            : 1u;
    wood = g_vm_mock_yanmen_config.woodDropMin +
           (spanWood > 1 ? vm_mock_activities_rng_next() % spanWood : 0);
    stone = g_vm_mock_yanmen_config.stoneDropMin +
            (spanStone > 1 ? vm_mock_activities_rng_next() % spanStone : 0);
    if (g_vm_mock_yanmen_config.woodItemId && wood)
        (void)vm_net_mock_role_add_backpack_item_to_role(
            role, g_vm_mock_yanmen_config.woodItemId, wood, 0, &seq,
            "yanmen-wood", NULL, 0);
    if (g_vm_mock_yanmen_config.stoneItemId && stone)
        (void)vm_net_mock_role_add_backpack_item_to_role(
            role, g_vm_mock_yanmen_config.stoneItemId, stone, 0, &seq,
            "yanmen-stone", NULL, 0);
    vm_net_mock_role_db_save("yanmen-drop");
}

typedef struct
{
    u16 ringCount;
    u8 bonusClaimed;
    u8 taskType;
    u32 targetId;
    u32 targetCount;
    u32 progress;
    char targetNpc[16];
    u8 taskActive;
    bool found;
} vm_mock_ring_daily_ctx;

static bool vm_mock_ring_daily_row_cb(void *ctx, unsigned int cc,
                                      const char *const *values,
                                      const size_t *lengths)
{
    vm_mock_ring_daily_ctx *row = (vm_mock_ring_daily_ctx *)ctx;
    u32 tmp = 0;
    (void)cc;
    if (row == NULL || values == NULL)
        return false;
    row->found = true;
    if (values[0] && vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        row->ringCount = (u16)tmp;
    if (values[1] && lengths[1])
        row->bonusClaimed = (u8)(values[1][0] == '0' ? 0 : 1);
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        row->taskType = (u8)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        row->targetId = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        row->targetCount = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        row->progress = tmp;
    if (values[6] && lengths[6] > 0 && lengths[6] < sizeof(row->targetNpc))
    {
        memcpy(row->targetNpc, values[6], lengths[6]);
        row->targetNpc[lengths[6]] = 0;
    }
    if (values[7] && lengths[7])
        row->taskActive = (u8)(values[7][0] == '0' ? 0 : 1);
    return true;
}

/* 当日跑环已满（ring_count >= daily_max）；膜拜天下第一前置. */
static bool vm_mock_ring_role_finished_daily(vm_net_mock_role_state *role)
{
    char accountHex[160];
    char query[640];
    vm_mock_ring_daily_ctx row;
    u32 dayYmd;

    if (role == NULL ||
        !vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
        return false;
    vm_mock_ring_load_config(false);
    dayYmd = vm_mock_activities_day_ymd();
    if (dayYmd == 0)
        return false;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, role->roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    return row.found && row.ringCount >= g_vm_mock_ring_config.dailyMax;
}

static void vm_mock_ring_format_desc(void);

static void vm_mock_ring_sync_runtime_from_db(vm_net_mock_role_state *role)
{
    char accountHex[160];
    char query[640];
    vm_mock_ring_daily_ctx row;
    u32 dayYmd = vm_mock_activities_day_ymd();

    memset(&g_vm_mock_ring_rt, 0, sizeof(g_vm_mock_ring_rt));
    if (role == NULL || !vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
        return;
    vm_mock_ring_load_config(false);
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, role->roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    if (!row.found)
    {
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO account_role_ring_daily "
                 "(account_id,role_id,day_ymd) VALUES(UNHEX('%s'),%u,%u)",
                 accountHex, role->roleId, dayYmd);
        (void)vm_mysql_exec(query);
    }
    g_vm_mock_ring_rt.taskType = row.taskType;
    g_vm_mock_ring_rt.targetId = row.targetId;
    g_vm_mock_ring_rt.targetCount = row.targetCount ? row.targetCount : 1;
    g_vm_mock_ring_rt.progress = row.progress;
    g_vm_mock_ring_rt.active = row.taskActive;
    snprintf(g_vm_mock_ring_rt.targetNpc, sizeof(g_vm_mock_ring_rt.targetNpc),
             "%s", row.targetNpc);
    vm_mock_ring_format_desc();
}

static bool vm_mock_ring_save_runtime(vm_net_mock_role_state *role, u16 ringCount,
                                      u8 bonusClaimed)
{
    char accountHex[160];
    char query[1024];
    char npcHex[64];
    u32 dayYmd = vm_mock_activities_day_ymd();

    if (role == NULL || !vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
        return false;
    vm_mysql_hex_encode(g_vm_mock_ring_rt.targetNpc,
                        strlen(g_vm_mock_ring_rt.targetNpc), npcHex,
                        sizeof(npcHex));
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_ring_daily "
             "(account_id,role_id,day_ymd,ring_count,bonus_claimed,task_type,"
             "target_id,target_count,progress,target_npc,task_active) "
             "VALUES(UNHEX('%s'),%u,%u,%u,%u,%u,%u,%u,%u,X'%s',%u) "
             "ON DUPLICATE KEY UPDATE ring_count=VALUES(ring_count),"
             "bonus_claimed=VALUES(bonus_claimed),task_type=VALUES(task_type),"
             "target_id=VALUES(target_id),target_count=VALUES(target_count),"
             "progress=VALUES(progress),target_npc=VALUES(target_npc),"
             "task_active=VALUES(task_active)",
             accountHex, role->roleId, dayYmd, (u32)ringCount, (u32)bonusClaimed,
             (u32)g_vm_mock_ring_rt.taskType, g_vm_mock_ring_rt.targetId,
             g_vm_mock_ring_rt.targetCount, g_vm_mock_ring_rt.progress, npcHex,
             (u32)g_vm_mock_ring_rt.active);
    return vm_mysql_exec(query);
}

static void vm_mock_ring_persist_progress(vm_net_mock_role_state *role)
{
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();

    if (role == NULL || !g_vm_mock_ring_rt.active ||
        !vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
        return;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, role->roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    (void)vm_mock_ring_save_runtime(role, row.ringCount, row.bonusClaimed);
}

static bool vm_mock_ring_item_is_medicine(u32 itemId)
{
    const vm_net_mock_item_effect_catalog_item *cat =
        vm_net_mock_find_item_effect_catalog_item(itemId);
    return cat != NULL && cat->category == 10;
}

static bool vm_mock_ring_item_is_equipment(u32 itemId)
{
    return vm_net_mock_find_equipment_catalog_item(itemId) != NULL;
}

static bool vm_mock_ring_item_matches_submit_med(u32 itemId)
{
    if (g_vm_mock_ring_rt.targetId != 0)
        return itemId == g_vm_mock_ring_rt.targetId;
    return vm_mock_ring_item_is_medicine(itemId);
}

static bool vm_mock_ring_item_matches_submit_equip(u32 itemId)
{
    if (g_vm_mock_ring_rt.targetId != 0)
        return itemId == g_vm_mock_ring_rt.targetId;
    return vm_mock_ring_item_is_equipment(itemId);
}

static bool vm_mock_ring_consume_matching_items(vm_net_mock_role_state *role,
                                                u32 needCount,
                                                bool (*match)(u32 itemId))
{
    u32 remain = needCount;
    u32 guard = 0;

    if (role == NULL || needCount == 0 || match == NULL)
        return needCount == 0;
    while (remain > 0 && guard < 256)
    {
        u32 i;
        bool consumed = false;

        ++guard;
        for (i = 0; i < role->backpackItemCount &&
                    i < VM_NET_MOCK_BACKPACK_MAX_ITEMS;
             ++i)
        {
            vm_net_mock_consume_sync_row sync[VM_NET_MOCK_CONSUME_SYNC_MAX];
            u32 syncCount = 0;
            u32 itemId = role->backpackItems[i].itemId;
            u32 have = role->backpackItems[i].count;
            u32 take;
            u32 instanceId = role->backpackItems[i].enhanceInstanceId;
            u16 seq = role->backpackItems[i].seq;
            bool isEquip = vm_mock_ring_item_is_equipment(itemId);

            if (have == 0 || !match(itemId))
                continue;
            take = have < remain ? have : remain;
            if (isEquip)
                take = 1;
            if (!vm_net_mock_role_consume_backpack_item_amount(
                    role, itemId, take, sync, VM_NET_MOCK_CONSUME_SYNC_MAX,
                    &syncCount))
                return false;
            if (isEquip && take >= have)
                (void)vm_net_mock_equipment_instance_retire(role->roleId,
                                                            instanceId, seq);
            remain -= take;
            consumed = true;
            break;
        }
        if (!consumed)
            return false;
    }
    return remain == 0;
}

#define VM_MOCK_RING_BRACKET_MIN 20u

static u16 vm_mock_ring_bracket_for_level(u32 level)
{
    if (level >= 70)
        return 70;
    if (level >= 50)
        return 50;
    if (level >= VM_MOCK_RING_BRACKET_MIN)
        return 20;
    return 0;
}

static u32 vm_mock_ring_encode_pvp_target(u8 job, u16 bracket)
{
    return ((u32)bracket << 8) | (u32)(job & 0xffu);
}

static void vm_mock_ring_decode_pvp_target(u32 targetId, u8 *jobOut,
                                           u16 *bracketOut)
{
    if (jobOut != NULL)
        *jobOut = (u8)(targetId & 0xffu);
    if (bracketOut != NULL)
        *bracketOut = (u16)((targetId >> 8) & 0xffffu);
}

static const char *vm_mock_ring_job_name_gbk(u8 job)
{
    switch (job)
    {
    case 2:
        return "\xbb\xc3\xbd\xa3"; /* 幻剑 */
    case 3:
        return "\xb9\xed\xb5\xc0"; /* 鬼道 */
    default:
        return "\xcc\xec\xbb\xfa"; /* 天机 */
    }
}

static const char *vm_mock_ring_bracket_label_gbk(u16 bracket)
{
    switch (bracket)
    {
    case 70:
        return "\x37\x30\xbc\xb6\xd2\xd4\xc9\xcf"; /* 70级以上 */
    case 50:
        return "\x35\x30\x2d\x36\x39\xbc\xb6"; /* 50-69级 */
    case 20:
        return "\x32\x30\x2d\x34\x39\xbc\xb6"; /* 20-49级 */
    default:
        return "";
    }
}

static void vm_mock_ring_monster_name(u32 enemyId, char *nameOut, size_t nameCap)
{
    if (nameOut == NULL || nameCap == 0)
        return;
    nameOut[0] = 0;
    (void)vm_net_mock_monster_battle_left_appearance(enemyId, nameOut, nameCap,
                                                     NULL, NULL, NULL);
    if (nameOut[0] == 0)
        snprintf(nameOut, nameCap, "ID%u", enemyId);
}

/* firstScene / SCE id → 场景名（优先后台 display_name / sMap 别名） */
static void vm_mock_ring_scene_label(const char *scene, char *out, size_t outCap)
{
    char withSce[80];

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (scene == NULL || scene[0] == 0)
        return;
    vm_mock_world_boss_resolve_map_label_gbk(scene, out, outCap);
    if (out[0] != 0)
        return;
    /* sMap 行多为 xxx.sce，补后缀再试一次 */
    if (strstr(scene, ".sce") == NULL && strstr(scene, ".SCE") == NULL)
    {
        snprintf(withSce, sizeof(withSce), "%s.sce", scene);
        vm_mock_world_boss_resolve_map_label_gbk(withSce, out, outCap);
    }
}

/*
 * 跑环只认可走进的地图 SCE。task.dsh#N 等资源溯源会经
 * resolve_map_label 兜底变成非空「标签」，不能当作地图。
 */
static bool vm_mock_ring_first_scene_is_playable_map(const char *scene)
{
    char mapLabel[40];

    if (scene == NULL || scene[0] == 0)
        return false;
    if (strstr(scene, ".dsh") != NULL || strstr(scene, ".DSH") != NULL)
        return false;
    mapLabel[0] = 0;
    vm_mock_ring_scene_label(scene, mapLabel, sizeof(mapLabel));
    if (mapLabel[0] == 0)
        return false;
    if (strstr(mapLabel, ".dsh") != NULL || strstr(mapLabel, ".DSH") != NULL)
        return false;
    return true;
}

static void vm_mock_ring_monster_scene(u32 enemyId, char *sceneOut, size_t sceneCap)
{
    vm_net_mock_monster_admin_row rows[96];
    u32 total;
    u32 i;

    if (sceneOut == NULL || sceneCap == 0)
        return;
    sceneOut[0] = 0;
    total = vm_net_mock_monster_admin_list(rows, 96);
    for (i = 0; i < total && i < 96; ++i)
    {
        if (rows[i].enemyId != enemyId)
            continue;
        if (!vm_mock_ring_first_scene_is_playable_map(rows[i].firstScene))
            return;
        vm_mock_ring_scene_label(rows[i].firstScene, sceneOut, sceneCap);
        return;
    }
}

static u32 vm_mock_ring_med_need_count(void)
{
    u32 groups = g_vm_mock_ring_rt.targetCount ? g_vm_mock_ring_rt.targetCount : 1;

    return groups * (u32)VM_MOCK_RING_MED_PER_GROUP;
}

/* 拜访 NPC 常见落点 SCE → 场景名 */
static void vm_mock_ring_visit_scene_label(const char *npcKeyword, char *out,
                                           size_t outCap)
{
    const char *sceneId = "";

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (npcKeyword == NULL || npcKeyword[0] == 0)
        return;
    /* 铁匠 → 蜀山南门 */
    if (strstr(npcKeyword, "\xcc\xfa\xbd\xb3") != NULL)
        sceneId = "c14\xca\xf7\xc9\xbd_01";
    /* 药师 → 蓬莱（铜雀台；04 图可能不在 sMap） */
    else if (strstr(npcKeyword, "\xd2\xa9\xca\xa6") != NULL)
        sceneId = "c00\xc5\xec\xc0\xb4\xcf\xc9\xb5\xba_01";
    /* 店小二 → 雁门市集 */
    else if (strstr(npcKeyword, "\xb5\xea\xd0\xa1\xb6\xfe") != NULL)
        sceneId = "c08\xd1\xe3\xc3\xc5\xb9\xd8_03";
    else
        return;
    vm_mock_ring_scene_label(sceneId, out, outCap);
}

static void vm_mock_ring_item_name(u32 itemId, char *nameOut, size_t nameCap)
{
    const vm_net_mock_shop_catalog_item *item;
    u32 aliasId = itemId;

    if (nameOut == NULL || nameCap == 0)
        return;
    nameOut[0] = 0;
    /* 雁门旧默认 850/851 不在 item.dsh，对应 木材/石料 */
    if (itemId == 850u)
        aliasId = 3u;
    else if (itemId == 851u)
        aliasId = 5u;
    item = vm_net_mock_find_shop_catalog_item(aliasId);
    if (item == NULL && aliasId != itemId)
        item = vm_net_mock_find_shop_catalog_item(itemId);
    if (item != NULL && item->name[0] != 0)
    {
        snprintf(nameOut, nameCap, "%s", item->name);
        return;
    }
    if (itemId == 850u || aliasId == 3u)
    {
        snprintf(nameOut, nameCap, "\xc4\xbe\xb2\xc4"); /* 木材 */
        return;
    }
    if (itemId == 851u || aliasId == 5u)
    {
        snprintf(nameOut, nameCap, "\xca\xaf\xc1\xcf"); /* 石料 */
        return;
    }
    snprintf(nameOut, nameCap, "#%u", itemId);
}

/* 击杀/收集环：须有可走进地图；世界Boss 配置怪一律不进池。 */
static bool vm_mock_ring_monster_eligible_kill(const vm_net_mock_monster_admin_row *row,
                                              u32 worldBossMonsterId)
{
    if (row == NULL || row->enemyId == 0)
        return false;
    if (worldBossMonsterId != 0 && row->enemyId == worldBossMonsterId)
        return false;
    return vm_mock_ring_first_scene_is_playable_map(row->firstScene);
}

static u32 vm_mock_ring_pick_monster_id(bool bossOnly)
{
    enum
    {
        VM_MOCK_RING_MOB_POOL = 192
    };
    vm_net_mock_monster_admin_row rows[VM_MOCK_RING_MOB_POOL];
    u32 matchIds[VM_MOCK_RING_MOB_POOL];
    char seenNames[VM_MOCK_RING_MOB_POOL][32];
    u32 matchCount = 0;
    u32 total;
    u32 i;
    u32 pass;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleLevel = (role != NULL && role->level != 0) ? role->level : 30u;
    const vm_mock_world_boss_config *wbCfg = vm_net_mock_world_boss_config_get();
    u32 worldBossId = (wbCfg != NULL) ? wbCfg->monsterId : 0u;

    total = vm_net_mock_monster_admin_list(rows, VM_MOCK_RING_MOB_POOL);
    if (total > VM_MOCK_RING_MOB_POOL)
        total = VM_MOCK_RING_MOB_POOL;

    /*
     * 同名怪（如 白光虎 id=83/99）各占一席会抬高该名出现率。
     * 按显示名去重；优先选与角色等级接近、且有地图名的条目。
     * 无地图名 / 世界Boss 配置怪：两轮均跳过。
     */
    for (pass = 0; pass < 2 && matchCount == 0; ++pass)
    {
        matchCount = 0;
        for (i = 0; i < total; ++i)
        {
            bool isBoss = rows[i].family == (u8)VM_NET_MOCK_MONSTER_BOSS;
            char name[32];
            u32 j;
            u32 lvl = rows[i].level;
            u32 diff;

            if (bossOnly ? !isBoss : isBoss)
                continue;
            if (!vm_mock_ring_monster_eligible_kill(&rows[i], worldBossId))
                continue;
            if (rows[i].displayName[0] != 0)
                snprintf(name, sizeof(name), "%s", rows[i].displayName);
            else
                vm_mock_ring_monster_name(rows[i].enemyId, name, sizeof(name));
            if (name[0] == 0 || (name[0] == 'I' && name[1] == 'D'))
                continue;
            if (pass == 0)
            {
                diff = lvl > roleLevel ? (lvl - roleLevel) : (roleLevel - lvl);
                if (diff > 25u)
                    continue;
            }
            for (j = 0; j < matchCount; ++j)
            {
                if (strcmp(seenNames[j], name) == 0)
                    break;
            }
            if (j < matchCount)
                continue;
            snprintf(seenNames[matchCount], sizeof(seenNames[matchCount]), "%s",
                     name);
            matchIds[matchCount++] = rows[i].enemyId;
            if (matchCount >= VM_MOCK_RING_MOB_POOL)
                break;
        }
    }

    if (matchCount == 0)
    {
        printf("[warn][mock-service] ring_pick_monster empty boss=%u role_lv=%u "
               "wboss=%u evidence=require-map-skip-task-dsh-wboss\n",
               bossOnly ? 1u : 0u, roleLevel, worldBossId);
        return 0;
    }
    printf("[info][mock-service] ring_pick_monster boss=%u pool=%u role_lv=%u "
           "wboss_skip=%u pick_from_unique_names evidence=map+dedupe\n",
           bossOnly ? 1u : 0u, matchCount, roleLevel, worldBossId);
    return matchIds[vm_mock_activities_rng_next() % matchCount];
}

static u32 vm_mock_ring_pick_collect_item(void)
{
    u32 items[96];
    u32 itemCount = 0;
    u32 i;
    u32 d;

    vm_mock_yanmen_load_config(false);
#define VM_MOCK_RING_COLLECT_ADD(itemIdValue)                                  \
    do                                                                         \
    {                                                                          \
        u32 _collectId = (itemIdValue);                                        \
        u32 _collectIdx;                                                       \
        char _name[32];                                                        \
        if (_collectId == 0)                                                   \
            break;                                                             \
        vm_mock_ring_item_name(_collectId, _name, sizeof(_name));              \
        if (_name[0] == '#' || _name[0] == 0)                                  \
            break;                                                             \
        for (_collectIdx = 0; _collectIdx < itemCount; ++_collectIdx)           \
        {                                                                      \
            if (items[_collectIdx] == _collectId)                              \
                break;                                                         \
        }                                                                      \
        if (_collectIdx == itemCount && itemCount < 96)                        \
            items[itemCount++] = _collectId;                                   \
    } while (0)

    VM_MOCK_RING_COLLECT_ADD(g_vm_mock_yanmen_config.woodItemId);
    VM_MOCK_RING_COLLECT_ADD(g_vm_mock_yanmen_config.stoneItemId);
    {
        vm_net_mock_monster_admin_row rows[96];
        u32 total = vm_net_mock_monster_admin_list(rows, 96);
        const vm_mock_world_boss_config *wbCfg =
            vm_net_mock_world_boss_config_get();
        u32 worldBossId = (wbCfg != NULL) ? wbCfg->monsterId : 0u;

        for (i = 0; i < total && i < 96; ++i)
        {
            /* 与击杀环同一契约：task.dsh 溯源怪不进收集池 */
            if (!vm_mock_ring_monster_eligible_kill(&rows[i], worldBossId))
                continue;
            for (d = 0; d < rows[i].dropCount; ++d)
                VM_MOCK_RING_COLLECT_ADD(rows[i].drops[d].itemId);
        }
    }
#undef VM_MOCK_RING_COLLECT_ADD
    if (itemCount == 0)
    {
        /* item.dsh: 3=木材 */
        u32 wood = g_vm_mock_yanmen_config.woodItemId;
        if (wood == 0 || wood == 850u)
            wood = 3u;
        return wood;
    }
    return items[vm_mock_activities_rng_next() % itemCount];
}

static void vm_mock_ring_collect_drop_hint(u32 itemId, char *hintOut, size_t hintCap)
{
    vm_net_mock_monster_admin_row rows[96];
    u32 total;
    u32 i;
    u32 d;
    char sceneLabel[40];
    char mobName[32];

    if (hintOut == NULL || hintCap == 0)
        return;
    hintOut[0] = 0;
    vm_mock_yanmen_load_config(false);
    if (itemId == g_vm_mock_yanmen_config.woodItemId ||
        itemId == g_vm_mock_yanmen_config.stoneItemId || itemId == 850u ||
        itemId == 851u || itemId == 3u || itemId == 5u)
    {
        /* 雁门市集打怪 */
        sceneLabel[0] = 0;
        vm_mock_ring_scene_label("c08\xd1\xe3\xc3\xc5\xb9\xd8_03", sceneLabel,
                                 sizeof(sceneLabel));
        if (sceneLabel[0] != 0)
            snprintf(hintOut, hintCap, "%s\xb4\xf2\xb9\xd6", sceneLabel);
        else
            snprintf(hintOut, hintCap, "\xd1\xe3\xc3\xc5\xb9\xd8\xb4\xf2\xb9\xd6");
        return;
    }
    total = vm_net_mock_monster_admin_list(rows, 96);
    for (i = 0; i < total && i < 96; ++i)
    {
        if (!vm_mock_ring_first_scene_is_playable_map(rows[i].firstScene))
            continue;
        for (d = 0; d < rows[i].dropCount; ++d)
        {
            if (rows[i].drops[d].itemId != itemId)
                continue;
            sceneLabel[0] = 0;
            mobName[0] = 0;
            vm_mock_ring_scene_label(rows[i].firstScene, sceneLabel,
                                     sizeof(sceneLabel));
            if (rows[i].displayName[0] != 0)
                snprintf(mobName, sizeof(mobName), "%s", rows[i].displayName);
            else
                vm_mock_ring_monster_name(rows[i].enemyId, mobName,
                                          sizeof(mobName));
            if (sceneLabel[0] != 0 && mobName[0] != 0)
            {
                /* 蜀山南门·黑熊 */
                snprintf(hintOut, hintCap, "%s\xa1\xa2%s", sceneLabel, mobName);
                return;
            }
            if (mobName[0] != 0)
            {
                snprintf(hintOut, hintCap, "%s", mobName);
                return;
            }
            if (sceneLabel[0] != 0)
            {
                snprintf(hintOut, hintCap, "%s", sceneLabel);
                return;
            }
        }
    }
    snprintf(hintOut, hintCap, "\xb4\xf2\xb9\xd6"); /* 打怪 */
}

static u32 vm_mock_ring_pick_medicine_item(void)
{
    u32 ids[64];
    u32 pickCount = 0;
    u32 total = vm_net_mock_load_item_effect_catalog();
    u32 i;

    for (i = 0; i < total; ++i)
    {
        u32 itemId = g_vm_net_mock_item_effect_catalog[i].itemId;

        if (!vm_net_mock_item_is_npc_medicine(itemId))
            continue;
        if (vm_net_mock_find_shop_catalog_item(itemId) == NULL)
            continue;
        ids[pickCount++] = itemId;
        if (pickCount >= 64)
            break;
    }
    if (pickCount == 0)
        return 301u;
    return ids[vm_mock_activities_rng_next() % pickCount];
}

static u32 vm_mock_ring_pick_equipment_item(void)
{
    u32 ids[96];
    u32 pickCount = 0;
    u32 total = vm_net_mock_load_equipment_catalog();
    u32 i;

    for (i = 0; i < total; ++i)
    {
        const vm_net_mock_equipment_catalog_item *equip =
            &g_vm_net_mock_equipment_catalog[i];

        if (equip->itemId == 0 || equip->quality != 0)
            continue;
        if (equip->levelRequired == 0 || equip->levelRequired > 70)
            continue;
        if (vm_net_mock_find_shop_catalog_item(equip->itemId) == NULL)
            continue;
        ids[pickCount++] = equip->itemId;
        if (pickCount >= 96)
            break;
    }
    if (pickCount == 0)
        return 1u;
    return ids[vm_mock_activities_rng_next() % pickCount];
}

static void vm_mock_ring_item_acquire_hint(u32 itemId, char *hintOut, size_t hintCap)
{
    if (hintOut == NULL || hintCap == 0)
        return;
    hintOut[0] = 0;
    if (vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
    {
        snprintf(hintOut, hintCap, "\xcc\xfa\xbd\xb3\xb4\xa6\xb9\xba\xc2\xf2");
        return;
    }
    if (vm_net_mock_item_is_npc_medicine(itemId))
    {
        snprintf(hintOut, hintCap, "\xd2\xa9\xca\xa6\xb4\xa6\xb9\xba\xc2\xf2");
        return;
    }
    snprintf(hintOut, hintCap, "\xc9\xcc\xb5\xea"); /* 商店 */
}

/*
 * 进程内只有一份 g_vm_mock_ring_rt：多玩家时会被其他账号的 WT47/接环冲掉。
 * 进度判定一律以 account_role_ring_daily 为准；仅当活跃会话正是该角色时回写缓存。
 */
static void vm_mock_ring_rt_sync_from_row_if_active(const char *accountId,
                                                    u32 roleId,
                                                    const vm_mock_ring_daily_ctx *row)
{
    const char *activeAccount = g_vm_mock_service_active_account_id;
    vm_net_mock_role_state *activeRole = vm_net_mock_active_role();

    if (row == NULL || accountId == NULL || accountId[0] == 0)
        return;
    if (activeAccount == NULL || activeAccount[0] == 0 ||
        strcmp(activeAccount, accountId) != 0 || activeRole == NULL ||
        activeRole->roleId != roleId)
        return;
    g_vm_mock_ring_rt.taskType = row->taskType;
    g_vm_mock_ring_rt.targetId = row->targetId;
    g_vm_mock_ring_rt.targetCount = row->targetCount ? row->targetCount : 1;
    g_vm_mock_ring_rt.progress = row->progress;
    g_vm_mock_ring_rt.active = row->taskActive;
    snprintf(g_vm_mock_ring_rt.targetNpc, sizeof(g_vm_mock_ring_rt.targetNpc),
             "%s", row->targetNpc);
    if (g_vm_mock_ring_rt.active)
        vm_mock_ring_format_desc();
}

/* 同名怪接环去重后，战斗胜利可能报到另一 catalog id —— 按显示名对齐。 */
static void vm_mock_ring_monster_match_label(u32 enemyId, char *nameOut,
                                             size_t nameCap)
{
    vm_net_mock_monster_admin_row rows[96];
    u32 total;
    u32 i;

    if (nameOut == NULL || nameCap == 0)
        return;
    nameOut[0] = 0;
    if (enemyId == 0)
        return;
    total = vm_net_mock_monster_admin_list(rows, 96);
    for (i = 0; i < total && i < 96; ++i)
    {
        if (rows[i].enemyId != enemyId)
            continue;
        if (rows[i].displayName[0] != 0)
        {
            snprintf(nameOut, nameCap, "%s", rows[i].displayName);
            return;
        }
        break;
    }
    vm_mock_ring_monster_name(enemyId, nameOut, nameCap);
}

static bool vm_mock_ring_enemy_matches_target(u32 targetId, u32 enemyId)
{
    char want[32];
    char got[32];

    if (targetId == 0 || targetId == enemyId)
        return true;
    if (enemyId == 0)
        return false;
    want[0] = 0;
    got[0] = 0;
    vm_mock_ring_monster_match_label(targetId, want, sizeof(want));
    vm_mock_ring_monster_match_label(enemyId, got, sizeof(got));
    if (want[0] == 0 || got[0] == 0)
        return false;
    if (want[0] == 'I' && want[1] == 'D')
        return false;
    if (got[0] == 'I' && got[1] == 'D')
        return false;
    return strcmp(want, got) == 0;
}

static bool vm_mock_ring_db_set_progress(const char *accountHex, u32 roleId,
                                         u32 dayYmd, u32 progress)
{
    char query[512];

    if (accountHex == NULL || accountHex[0] == 0 || roleId == 0)
        return false;
    snprintf(query, sizeof(query),
             "UPDATE account_role_ring_daily SET progress=%u "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u "
             "AND task_active=1",
             progress, accountHex, roleId, dayYmd);
    return vm_mysql_exec(query);
}

void vm_mock_activities_on_pvp_kill(vm_net_mock_role_state *role,
                                    const char *accountId, u8 victimJob,
                                    u32 victimLevel)
{
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();
    u8 reqJob = 0;
    u16 reqBracket = 0;
    u16 victimBracket = 0;
    u32 newProgress;

    if (role == NULL)
        return;
    vm_mock_ring_load_config(false);
    /* 必须用胜者账号，不能用当前请求的 active_account（切磋结算时常串号） */
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, role->roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    if (!row.taskActive || row.taskType != VM_MOCK_RING_TYPE_KILL_PLAYER)
        return;
    if (row.targetCount == 0)
        row.targetCount = 1;
    if (row.targetId != 0)
    {
        vm_mock_ring_decode_pvp_target(row.targetId, &reqJob, &reqBracket);
        if (reqJob == 0 || reqJob > 3 || reqBracket == 0)
            return;
        if (victimJob == 0 || victimJob > 3)
            victimJob = 1;
        victimBracket = vm_mock_ring_bracket_for_level(victimLevel);
        if (victimBracket == 0 || victimJob != reqJob ||
            victimBracket != reqBracket)
        {
            printf("[info][mock-service] ring_pvp_kill_skip role=%u req_job=%u "
                   "req_bracket=%u victim_job=%u victim_lv=%u victim_bracket=%u "
                   "evidence=job-or-bracket-mismatch\n",
                   role->roleId, (u32)reqJob, (u32)reqBracket, (u32)victimJob,
                   victimLevel, (u32)victimBracket);
            return;
        }
    }
    newProgress = row.progress + 1;
    if (newProgress > row.targetCount)
        newProgress = row.targetCount;
    if (!vm_mock_ring_db_set_progress(accountHex, role->roleId, dayYmd,
                                      newProgress))
        return;
    row.progress = newProgress;
    vm_mock_ring_rt_sync_from_row_if_active(accountId, role->roleId, &row);
    printf("[info][mock-service] ring_pvp_kill_progress role=%u job=%u lv=%u "
           "progress=%u/%u evidence=winner-account-db-only\n",
           role->roleId, (u32)victimJob, victimLevel, row.progress,
           row.targetCount);
}

/*
 * Apply ring kill progress for one account/role from DB (not the process-global
 * g_vm_mock_ring_rt alone). Multi-client sessions share that global, so team
 * credit must key by winner account like vm_mock_activities_on_pvp_kill.
 * When the credited role is the current active account, also refresh runtime.
 */
static bool vm_mock_ring_apply_pve_kill_for_account(const char *accountId,
                                                     u32 roleId, u32 enemyId,
                                                     bool isBoss)
{
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();
    u32 newProgress;

    if (accountId == NULL || accountId[0] == 0 || roleId == 0)
        return false;
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return false;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    if (!row.taskActive)
        return false;
    if (row.targetCount == 0)
        row.targetCount = 1;
    if (row.taskType == VM_MOCK_RING_TYPE_KILL_MOB && !isBoss)
    {
        if (!vm_mock_ring_enemy_matches_target(row.targetId, enemyId))
            return false;
        newProgress = row.progress + 1;
        if (newProgress > row.targetCount)
            newProgress = row.targetCount;
    }
    else if (row.taskType == VM_MOCK_RING_TYPE_KILL_BOSS && isBoss)
    {
        if (!vm_mock_ring_enemy_matches_target(row.targetId, enemyId))
            return false;
        newProgress = row.targetCount;
    }
    else
        return false;
    if (!vm_mock_ring_db_set_progress(accountHex, roleId, dayYmd, newProgress))
        return false;
    row.progress = newProgress;
    vm_mock_ring_rt_sync_from_row_if_active(accountId, roleId, &row);
    printf("[info][mock-service] ring_pve_kill_progress role=%u enemy=%u "
           "boss=%u progress=%u/%u target=%u evidence=account-db+name-match\n",
           roleId, enemyId, isBoss ? 1u : 0u, row.progress, row.targetCount,
           row.targetId);
    return true;
}

/* Ring collect tasks need task-material drop gating even when the player has
 * not accepted a matching catalog task row in account_role_tasks. */
static bool vm_mock_ring_collect_material_remaining(u32 roleId, u32 itemId,
                                                    u32 *remainingOut)
{
    char query[512];
    vm_mock_ring_daily_ctx row;
    vm_net_mock_role_state *role;
    u32 held;
    u32 need;
    u32 dayYmd;

    if (remainingOut)
        *remainingOut = 0;
    if (roleId == 0 || itemId == 0)
        return false;
    if (!vm_mock_activities_ensure_tables())
        return false;

    dayYmd = vm_mock_activities_day_ymd();
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE role_id=%u AND day_ymd=%u LIMIT 1",
             roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    if (!row.found || !row.taskActive)
        return true;
    if (row.taskType != VM_MOCK_RING_TYPE_COLLECT || row.targetId != itemId)
        return true;

    role = vm_mock_activities_role_by_id(roleId);
    held = role ? vm_mock_activities_backpack_count(role, itemId) : 0;
    if (row.targetCount == 0)
        row.targetCount = 1;
    if (held >= row.targetCount)
        need = 0;
    else
        need = row.targetCount - held;
    if (remainingOut)
        *remainingOut = need;
    return true;
}

void vm_mock_activities_on_kill_progress(u32 enemyId, bool isBoss,
                                         const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_team *team = NULL;
    bool shareWithParty =
        reason != NULL && strncmp(reason, "team-battle", 11) == 0;
    u8 i;
    u32 creditEnemyId = enemyId;

    if (shareWithParty && session != NULL)
        team = vm_mock_service_team_find_for_client(session->clientId);

    /* Prefer team-owned enemy id when global battle slot was overwritten. */
    if (team != NULL && team->active && team->battleEnemyId != 0)
        creditEnemyId = team->battleEnemyId;
    if (creditEnemyId == 0)
        creditEnemyId = enemyId;
    if (creditEnemyId != 0)
        isBoss = vm_net_mock_monster_family_for_enemy(creditEnemyId) ==
                 (u8)VM_NET_MOCK_MONSTER_BOSS;

    if (team != NULL && team->active && team->battleMemberCount >= 2)
    {
        if (team->battleSerial != 0 &&
            team->ringKillRewardedSerial == team->battleSerial)
            return;
        for (i = 0; i < team->battleMemberCount; ++i)
        {
            vm_mock_service_client_session *member =
                vm_mock_service_find_client_session(
                    team->battleMemberClientIds[i]);
            if (member == NULL || member->accountId[0] == 0 ||
                member->onlineRoleId == 0)
                continue;
            (void)vm_mock_ring_apply_pve_kill_for_account(
                member->accountId, member->onlineRoleId, creditEnemyId, isBoss);
        }
        team->ringKillRewardedSerial =
            team->battleSerial ? team->battleSerial : 1;
        return;
    }

    /* Solo / non-team-battle: credit active role from DB. */
    if (session != NULL && session->accountId[0] != 0 &&
        session->onlineRoleId != 0)
    {
        (void)vm_mock_ring_apply_pve_kill_for_account(
            session->accountId, session->onlineRoleId, creditEnemyId, isBoss);
        return;
    }
    if (role != NULL && g_vm_mock_service_active_account_id != NULL &&
        g_vm_mock_service_active_account_id[0] != 0)
    {
        (void)vm_mock_ring_apply_pve_kill_for_account(
            g_vm_mock_service_active_account_id, role->roleId, creditEnemyId,
            isBoss);
    }
}

void vm_mock_activities_on_npc_talk(const char *npcName)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();
    const char *accountId = NULL;
    u32 roleId = 0;

    if (npcName == NULL || npcName[0] == 0)
        return;
    if (session != NULL && session->accountId[0] != 0 &&
        session->onlineRoleId != 0)
    {
        accountId = session->accountId;
        roleId = session->onlineRoleId;
    }
    else if (role != NULL && g_vm_mock_service_active_account_id != NULL &&
             g_vm_mock_service_active_account_id[0] != 0)
    {
        accountId = g_vm_mock_service_active_account_id;
        roleId = role->roleId;
    }
    if (accountId == NULL || roleId == 0)
        return;
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    if (!row.taskActive || row.taskType != VM_MOCK_RING_TYPE_VISIT)
        return;
    if (row.targetNpc[0] == 0 || strstr(npcName, row.targetNpc) == NULL)
        return;
    if (row.progress >= 1)
        return;
    if (!vm_mock_ring_db_set_progress(accountHex, roleId, dayYmd, 1))
        return;
    row.progress = 1;
    if (row.targetCount == 0)
        row.targetCount = 1;
    vm_mock_ring_rt_sync_from_row_if_active(accountId, roleId, &row);
    printf("[info][mock-service] ring_visit_progress role=%u npc=%s "
           "evidence=account-db-not-global-rt\n",
           roleId, npcName);
}

/*
 * 世界真心话：发言者成功发出世界频道消息且全文匹配目标句后 progress=1。
 * 只 UPDATE progress，禁止再经 save_runtime 用全局 rt 整行回写。
 */
void vm_mock_activities_on_world_chat(const char *accountId, u32 roleId,
                                      const char *messageGbk)
{
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    char sent[96];
    const char *need;
    u32 dayYmd = vm_mock_activities_day_ymd();
    u32 done = 1;

    if (accountId == NULL || accountId[0] == 0 || roleId == 0 ||
        messageGbk == NULL || messageGbk[0] == 0)
        return;
    vm_mock_ring_load_config(false);
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    if (!row.taskActive || row.taskType != VM_MOCK_RING_TYPE_WORLD_TRUTH)
        return;
    done = row.targetCount ? row.targetCount : 1;
    if (row.progress >= done)
        return;

    snprintf(sent, sizeof(sent), "%s", messageGbk);
    vm_mock_ring_truth_trim_inplace(sent);
    need = vm_mock_ring_truth_message(row.targetId);
    if (need == NULL || need[0] == 0 || strcmp(sent, need) != 0)
    {
        printf("[info][mock-service] ring_truth_chat_miss role=%u targetId=%u "
               "sent_len=%u need_len=%u evidence=exact-world-match\n",
               roleId, row.targetId, (u32)strlen(sent),
               need ? (u32)strlen(need) : 0u);
        return;
    }

    if (!vm_mock_ring_db_set_progress(accountHex, roleId, dayYmd, done))
        return;
    row.progress = done;
    row.targetCount = done;
    vm_mock_ring_rt_sync_from_row_if_active(accountId, roleId, &row);
    printf("[info][mock-service] ring_truth_chat_ok role=%u targetId=%u "
           "evidence=world-chat-db-only\n",
           roleId, row.targetId);
}

static void vm_mock_ring_format_desc(void)
{
    u32 count;
    u32 medNeed;
    char mobName[32];
    char itemName[40];
    char dropHint[64];
    char sceneName[40];
    u8 pvpJob = 0;
    u16 pvpBracket = 0;

    g_vm_mock_ring_rt.desc[0] = 0;
    if (!g_vm_mock_ring_rt.active)
        return;
    count = g_vm_mock_ring_rt.targetCount ? g_vm_mock_ring_rt.targetCount : 1;
    switch (g_vm_mock_ring_rt.taskType)
    {
    case VM_MOCK_RING_TYPE_VISIT:
    {
        char mapHint[40];

        mapHint[0] = 0;
        vm_mock_ring_visit_scene_label(g_vm_mock_ring_rt.targetNpc, mapHint,
                                       sizeof(mapHint));
        if (mapHint[0] != 0)
        {
            /* 拜访蜀山南门铁匠(对话即可完成) */
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xb0\xdd\xb7\xc3%s%s(\xb6\xd4\xbb\xb0\xbc\xb4\xbf\xc9\xcd\xea\xb3\xc9)",
                     mapHint, g_vm_mock_ring_rt.targetNpc);
        }
        else
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xb0\xdd\xb7\xc3%s(\xb6\xd4\xbb\xb0\xbc\xb4\xbf\xc9\xcd\xea\xb3\xc9)",
                     g_vm_mock_ring_rt.targetNpc);
        }
        break;
    }
    case VM_MOCK_RING_TYPE_KILL_MOB:
        vm_mock_ring_monster_name(g_vm_mock_ring_rt.targetId, mobName,
                                  sizeof(mobName));
        vm_mock_ring_monster_scene(g_vm_mock_ring_rt.targetId, sceneName,
                                   sizeof(sceneName));
        if (sceneName[0] != 0)
        {
            /* 击杀N场怪物(地图) */
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xbb\xf7\xc9\xb1%u\xb3\xa1%s(%s)", count, mobName, sceneName);
        }
        else
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xbb\xf7\xc9\xb1%u\xb3\xa1%s", count, mobName);
        }
        break;
    case VM_MOCK_RING_TYPE_KILL_BOSS:
        vm_mock_ring_monster_name(g_vm_mock_ring_rt.targetId, mobName,
                                  sizeof(mobName));
        vm_mock_ring_monster_scene(g_vm_mock_ring_rt.targetId, sceneName,
                                   sizeof(sceneName));
        if (sceneName[0] != 0)
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xbb\xf7\xc9\xb1%u\xb3\xa1%s(%s)", count, mobName, sceneName);
        }
        else
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xbb\xf7\xc9\xb1%u\xb3\xa1%s", count, mobName);
        }
        break;
    case VM_MOCK_RING_TYPE_KILL_PLAYER:
        /* 切磋战胜…玩家（友好切磋胜利计进度，非强制PK） */
        if (g_vm_mock_ring_rt.targetId == 0)
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xc7\xd0\xb4\xe8\xd5\xbd\xca\xa4\xc8\xce\xd2\xe2\xcd\xe6\xbc\xd2");
            break;
        }
        vm_mock_ring_decode_pvp_target(g_vm_mock_ring_rt.targetId, &pvpJob,
                                       &pvpBracket);
        snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                 "\xc7\xd0\xb4\xe8\xd5\xbd\xca\xa4%u\xc3\xfb%s(%s)\xcd\xe6\xbc\xd2",
                 count, vm_mock_ring_job_name_gbk(pvpJob),
                 vm_mock_ring_bracket_label_gbk(pvpBracket));
        break;
    case VM_MOCK_RING_TYPE_COLLECT:
        vm_mock_ring_item_name(g_vm_mock_ring_rt.targetId, itemName,
                               sizeof(itemName));
        vm_mock_ring_collect_drop_hint(g_vm_mock_ring_rt.targetId, dropHint,
                                       sizeof(dropHint));
        /* 收集N个材料名，蜀山·黑熊掉落 */
        snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                 "\xca\xd5\xbc\xaf%u\xb8\xf6%s\xa3\xac%s\xb5\xf4\xc2\xe4", count,
                 itemName, dropHint);
        break;
    case VM_MOCK_RING_TYPE_SUBMIT_MED:
        medNeed = vm_mock_ring_med_need_count();
        if (g_vm_mock_ring_rt.targetId == 0)
        {
            /* 提交N组药品(每组10个,共M个) */
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xcc\xe1\xbd\xbb%u\xd7\xe9\xd2\xa9\xc6\xb7(\xc3\xbf\xd7\xe9%u\xb8\xf6,"
                     "\xb9\xb2%u\xb8\xf6)",
                     count, (u32)VM_MOCK_RING_MED_PER_GROUP, medNeed);
            break;
        }
        vm_mock_ring_item_name(g_vm_mock_ring_rt.targetId, itemName,
                               sizeof(itemName));
        vm_mock_ring_item_acquire_hint(g_vm_mock_ring_rt.targetId, dropHint,
                                       sizeof(dropHint));
        /* 提交N组xxx(每组10个,共M个)，药师处购买 */
        snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                 "\xcc\xe1\xbd\xbb%u\xd7\xe9%s(\xc3\xbf\xd7\xe9%u\xb8\xf6,\xb9\xb2%u\xb8\xf6)"
                 "\xa3\xac%s",
                 count, itemName, (u32)VM_MOCK_RING_MED_PER_GROUP, medNeed,
                 dropHint);
        break;
    case VM_MOCK_RING_TYPE_SUBMIT_EQUIP:
    {
        const vm_net_mock_equipment_catalog_item *equip =
            vm_net_mock_find_equipment_catalog_item(g_vm_mock_ring_rt.targetId);

        if (g_vm_mock_ring_rt.targetId == 0)
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xcc\xe1\xbd\xbb%u\xbc\xfe\xd7\xb0\xb1\xb8", count);
            break;
        }
        vm_mock_ring_item_name(g_vm_mock_ring_rt.targetId, itemName,
                               sizeof(itemName));
        vm_mock_ring_item_acquire_hint(g_vm_mock_ring_rt.targetId, dropHint,
                                       sizeof(dropHint));
        if (equip != NULL && equip->levelRequired > 0)
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xcc\xe1\xbd\xbb%u\xbc\xfe%s(Lv.%u)\xa3\xac%s", count,
                     itemName, (u32)equip->levelRequired, dropHint);
        }
        else
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xcc\xe1\xbd\xbb%u\xbc\xfe%s\xa3\xac%s", count, itemName,
                     dropHint);
        }
        break;
    }
    case VM_MOCK_RING_TYPE_WORLD_TRUTH:
    {
        const char *truth = vm_mock_ring_truth_message(g_vm_mock_ring_rt.targetId);

        if (truth != NULL && truth[0] != 0)
        {
            /* 世界真心话：世界频道发送「msg」(用小喇叭，发完可提交) */
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xca\xc0\xbd\xe7\xd5\xe6\xd0\xc4\xbb\xb0\xa3\xba"
                     "\xca\xc0\xbd\xe7\xc6\xb5\xb5\xc0\xb7\xa2\xcb\xcd\xa1\xb8%s\xa1\xb9"
                     "(\xd3\xc3\xd0\xa1\xc0\xad\xb0\xc7\xa3\xac\xb7\xa2\xcd\xea\xbf\xc9"
                     "\xcc\xe1\xbd\xbb)",
                     truth);
        }
        else
        {
            snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                     "\xca\xc0\xbd\xe7\xd5\xe6\xd0\xc4\xbb\xb0");
        }
        break;
    }
    default:
        snprintf(g_vm_mock_ring_rt.desc, sizeof(g_vm_mock_ring_rt.desc),
                 "\xbb\xb7\xc8\xce\xce\xf1"); /* 环任务 */
        break;
    }
}

static void vm_mock_ring_build_desc(void)
{
    static const char *visitPool[] = {
        "\xcc\xfa\xbd\xb3", /* 铁匠 */
        "\xd2\xa9\xca\xa6", /* 药师 */
        "\xb5\xea\xd0\xa1\xb6\xfe" /* 店小二 */
    };
    static const u16 pvpBrackets[] = {20, 50, 70};
    u32 type;
    u32 count;
    u8 pvpJob;

    vm_mock_ring_load_config(false);
    type = 1 + (vm_mock_activities_rng_next() % VM_MOCK_RING_TYPE_COUNT);
    g_vm_mock_ring_rt.taskType = type;
    g_vm_mock_ring_rt.progress = 0;
    g_vm_mock_ring_rt.targetId = 0;
    g_vm_mock_ring_rt.targetCount = 1;
    g_vm_mock_ring_rt.desc[0] = 0;
    g_vm_mock_ring_rt.targetNpc[0] = 0;
    switch (type)
    {
    case VM_MOCK_RING_TYPE_VISIT:
        snprintf(g_vm_mock_ring_rt.targetNpc, sizeof(g_vm_mock_ring_rt.targetNpc),
                 "%s", visitPool[vm_mock_activities_rng_next() % 3]);
        g_vm_mock_ring_rt.targetCount = 1;
        break;
    case VM_MOCK_RING_TYPE_KILL_MOB:
        /* 按战斗场次计数（胜利结算 +1），随机 10–30 场 */
        count = 10 + (vm_mock_activities_rng_next() % 21);
        g_vm_mock_ring_rt.targetCount = count;
        g_vm_mock_ring_rt.targetId = vm_mock_ring_pick_monster_id(false);
        if (g_vm_mock_ring_rt.targetId == 0)
        {
            /* 无可用地图名小怪：改拜访，避免无地图/世界Boss 空池发环 */
            type = VM_MOCK_RING_TYPE_VISIT;
            g_vm_mock_ring_rt.taskType = type;
            snprintf(g_vm_mock_ring_rt.targetNpc,
                     sizeof(g_vm_mock_ring_rt.targetNpc), "%s",
                     visitPool[vm_mock_activities_rng_next() % 3]);
            g_vm_mock_ring_rt.targetCount = 1;
            g_vm_mock_ring_rt.targetId = 0;
        }
        break;
    case VM_MOCK_RING_TYPE_KILL_BOSS:
        g_vm_mock_ring_rt.targetCount = 1;
        g_vm_mock_ring_rt.targetId = vm_mock_ring_pick_monster_id(true);
        if (g_vm_mock_ring_rt.targetId == 0)
        {
            type = VM_MOCK_RING_TYPE_VISIT;
            g_vm_mock_ring_rt.taskType = type;
            snprintf(g_vm_mock_ring_rt.targetNpc,
                     sizeof(g_vm_mock_ring_rt.targetNpc), "%s",
                     visitPool[vm_mock_activities_rng_next() % 3]);
            g_vm_mock_ring_rt.targetCount = 1;
            g_vm_mock_ring_rt.targetId = 0;
        }
        break;
    case VM_MOCK_RING_TYPE_KILL_PLAYER:
        pvpJob = (u8)(1 + (vm_mock_activities_rng_next() % 3));
        g_vm_mock_ring_rt.targetCount = 1;
        g_vm_mock_ring_rt.targetId = vm_mock_ring_encode_pvp_target(
            pvpJob, pvpBrackets[vm_mock_activities_rng_next() % 3]);
        break;
    case VM_MOCK_RING_TYPE_COLLECT:
        g_vm_mock_ring_rt.targetId = vm_mock_ring_pick_collect_item();
        count = 2 + (vm_mock_activities_rng_next() % 5);
        g_vm_mock_ring_rt.targetCount = count;
        break;
    case VM_MOCK_RING_TYPE_SUBMIT_MED:
        g_vm_mock_ring_rt.targetId = vm_mock_ring_pick_medicine_item();
        g_vm_mock_ring_rt.targetCount = 1 + (vm_mock_activities_rng_next() % 3);
        break;
    case VM_MOCK_RING_TYPE_SUBMIT_EQUIP:
        g_vm_mock_ring_rt.targetId = vm_mock_ring_pick_equipment_item();
        g_vm_mock_ring_rt.targetCount = 1;
        break;
    case VM_MOCK_RING_TYPE_WORLD_TRUTH:
        g_vm_mock_ring_rt.targetCount = 1;
        g_vm_mock_ring_rt.targetId = vm_mock_ring_pick_truth_id();
        if (g_vm_mock_ring_rt.targetId == 0)
        {
            type = VM_MOCK_RING_TYPE_VISIT;
            g_vm_mock_ring_rt.taskType = type;
            snprintf(g_vm_mock_ring_rt.targetNpc,
                     sizeof(g_vm_mock_ring_rt.targetNpc), "%s",
                     visitPool[vm_mock_activities_rng_next() % 3]);
            g_vm_mock_ring_rt.targetCount = 1;
        }
        break;
    default:
        break;
    }
    g_vm_mock_ring_rt.active = 1;
    vm_mock_ring_format_desc();
}

static u32 vm_mock_activities_finish_packet(u8 *out, u32 pos, u8 objectCount)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = objectCount;
    return pos;
}

static u32 vm_mock_activities_build_op_result(u8 *out, u32 outCap, u8 wtKind,
                                              u8 subtype, u32 result,
                                              const char *message)
{
    u32 pos = 5;
    u32 objectStart = 0;
    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, wtKind, subtype,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message",
                                       message ? message : ""))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_activities_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_activities_status_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 vitality = 0;
    u32 vitalityMax = 0;
    u32 wood = 0;
    u32 stone = 0;
    u8 exchangeBlob[512];
    u32 exchangeLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 i;

    vm_mock_festival_load_config(false);
    vm_mock_yanmen_load_config(false);
    if (role != NULL)
    {
        vm_mock_activities_vitality_wire(role, &vitality, &vitalityMax);
        wood = vm_mock_activities_backpack_count(role,
                                                 g_vm_mock_yanmen_config.woodItemId);
        stone = vm_mock_activities_backpack_count(role,
                                                  g_vm_mock_yanmen_config.stoneItemId);
    }
    if (vitalityMax < (u32)VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT)
        vitalityMax = VM_MOCK_ACTIVITIES_VITALITY_MAX_DEFAULT;
    for (i = 0; i < g_vm_mock_festival_exchange_count; ++i)
    {
        const vm_mock_festival_exchange *ex = &g_vm_mock_festival_exchanges[i];
        if (!ex->enabled)
            continue;
        if (!vm_net_mock_seq_put_u32(exchangeBlob, sizeof(exchangeBlob),
                                     &exchangeLen, ex->exchangeId) ||
            !vm_net_mock_seq_put_u32(exchangeBlob, sizeof(exchangeBlob),
                                     &exchangeLen, ex->vitalityCost) ||
            !vm_net_mock_seq_put_u32(exchangeBlob, sizeof(exchangeBlob),
                                     &exchangeLen, ex->itemId) ||
            !vm_net_mock_seq_put_u32(exchangeBlob, sizeof(exchangeBlob),
                                     &exchangeLen, ex->itemCount) ||
            !vm_net_mock_seq_put_string(exchangeBlob, sizeof(exchangeBlob),
                                        &exchangeLen,
                                        ex->label[0] ? ex->label : "-"))
            break;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1,
                                     VM_MOCK_ACTIVITIES_WT_KIND,
                                     VM_MOCK_ACTIVITIES_WT_STATUS,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "vitality", vitality) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "vitalityMax",
                                    vitalityMax) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "woodCount", wood) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "stoneCount", stone) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "woodItemId",
                                    g_vm_mock_yanmen_config.woodItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "stoneItemId",
                                    g_vm_mock_yanmen_config.stoneItemId) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "festivalList",
                                    exchangeBlob, (u16)exchangeLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_activities_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_yanmen_list_response(u8 *out, u32 outCap)
{
    u8 blob[512];
    u32 blobLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 i;

    vm_mock_yanmen_load_config(false);
    for (i = 0; i < g_vm_mock_yanmen_exchange_count; ++i)
    {
        const vm_mock_yanmen_exchange *ex = &g_vm_mock_yanmen_exchanges[i];
        if (!ex->enabled)
            continue;
        if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, ex->exchangeId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, ex->woodCost) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, ex->stoneCost) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, ex->itemId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, ex->itemCount) ||
            !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen,
                                        ex->label[0] ? ex->label : "-"))
            break;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ACTIVITIES_WT_KIND,
                                     VM_MOCK_ACTIVITIES_WT_YANMEN_LIST,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "yanmenList", blob,
                                    (u16)blobLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_activities_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_festival_exchange_response(u8 *out, u32 outCap,
                                                        u32 exchangeId)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const vm_mock_festival_exchange *ex = NULL;
    u32 i;
    u16 seq = 0;

    vm_mock_festival_load_config(false);
    for (i = 0; i < g_vm_mock_festival_exchange_count; ++i)
    {
        if (g_vm_mock_festival_exchanges[i].exchangeId == exchangeId &&
            g_vm_mock_festival_exchanges[i].enabled)
        {
            ex = &g_vm_mock_festival_exchanges[i];
            break;
        }
    }
    if (role == NULL || ex == NULL)
        return vm_mock_activities_build_op_result(
            out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
            VM_MOCK_ACTIVITIES_WT_FESTIVAL_EXCHANGE, 2, "invalid");
    if (!vm_mock_activities_vitality_spend(role, ex->vitalityCost))
        return vm_mock_activities_build_op_result(
            out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
            VM_MOCK_ACTIVITIES_WT_FESTIVAL_EXCHANGE, 3, "novital");
    if (!vm_net_mock_role_add_backpack_item_to_role(
            role, ex->itemId, ex->itemCount, 0, &seq, "festival-exchange", NULL,
            0))
    {
        (void)vm_mock_activities_vitality_add(role, ex->vitalityCost);
        return vm_mock_activities_build_op_result(
            out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
            VM_MOCK_ACTIVITIES_WT_FESTIVAL_EXCHANGE, 4, "bagfull");
    }
    vm_net_mock_role_db_save("festival-exchange");
    return vm_mock_activities_build_op_result(
        out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
        VM_MOCK_ACTIVITIES_WT_FESTIVAL_EXCHANGE, 1, "ok");
}

static u32 vm_net_mock_build_yanmen_exchange_do_response(u8 *out, u32 outCap,
                                                         u32 exchangeId, u32 doExchange)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const vm_mock_yanmen_exchange *ex = NULL;
    vm_net_mock_consume_sync_row sync[VM_NET_MOCK_CONSUME_SYNC_MAX];
    u32 syncCount = 0;
    u32 i;
    u16 seq = 0;

    vm_mock_yanmen_load_config(false);
    if (!doExchange)
        return vm_net_mock_build_yanmen_list_response(out, outCap);
    for (i = 0; i < g_vm_mock_yanmen_exchange_count; ++i)
    {
        if (g_vm_mock_yanmen_exchanges[i].exchangeId == exchangeId &&
            g_vm_mock_yanmen_exchanges[i].enabled)
        {
            ex = &g_vm_mock_yanmen_exchanges[i];
            break;
        }
    }
    if (role == NULL || ex == NULL)
        return vm_mock_activities_build_op_result(
            out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
            VM_MOCK_ACTIVITIES_WT_YANMEN_EXCHANGE, 2, "invalid");
    if (vm_mock_activities_backpack_count(role, g_vm_mock_yanmen_config.woodItemId) <
            ex->woodCost ||
        vm_mock_activities_backpack_count(role, g_vm_mock_yanmen_config.stoneItemId) <
            ex->stoneCost)
        return vm_mock_activities_build_op_result(
            out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
            VM_MOCK_ACTIVITIES_WT_YANMEN_EXCHANGE, 3, "nomat");
    if (ex->woodCost &&
        !vm_net_mock_role_consume_backpack_item_amount(
            role, g_vm_mock_yanmen_config.woodItemId, ex->woodCost, sync,
            VM_NET_MOCK_CONSUME_SYNC_MAX, &syncCount))
        return vm_mock_activities_build_op_result(
            out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
            VM_MOCK_ACTIVITIES_WT_YANMEN_EXCHANGE, 3, "nomat");
    if (ex->stoneCost &&
        !vm_net_mock_role_consume_backpack_item_amount(
            role, g_vm_mock_yanmen_config.stoneItemId, ex->stoneCost, sync,
            VM_NET_MOCK_CONSUME_SYNC_MAX, &syncCount))
        return vm_mock_activities_build_op_result(
            out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
            VM_MOCK_ACTIVITIES_WT_YANMEN_EXCHANGE, 3, "nomat");
    if (!vm_net_mock_role_add_backpack_item_to_role(
            role, ex->itemId, ex->itemCount, 0, &seq, "yanmen-exchange", NULL, 0))
        return vm_mock_activities_build_op_result(
            out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
            VM_MOCK_ACTIVITIES_WT_YANMEN_EXCHANGE, 4, "bagfull");
    vm_net_mock_role_db_save("yanmen-exchange");
    return vm_mock_activities_build_op_result(
        out, outCap, VM_MOCK_ACTIVITIES_WT_KIND,
        VM_MOCK_ACTIVITIES_WT_YANMEN_EXCHANGE, 1, "ok");
}

static u32 vm_net_mock_build_ring_status_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();
    u32 pos = 5;
    u32 objectStart = 0;

    vm_mock_ring_load_config(false);
    memset(&row, 0, sizeof(row));
    if (role != NULL && vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
    {
        snprintf(query, sizeof(query),
                 "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
                 "progress,target_npc,task_active FROM account_role_ring_daily "
                 "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
                 accountHex, role->roleId, dayYmd);
        (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    }
    g_vm_mock_ring_rt.taskType = row.taskType;
    g_vm_mock_ring_rt.targetId = row.targetId;
    g_vm_mock_ring_rt.targetCount = row.targetCount ? row.targetCount : 1;
    g_vm_mock_ring_rt.progress = row.progress;
    g_vm_mock_ring_rt.active = row.taskActive;
    snprintf(g_vm_mock_ring_rt.targetNpc, sizeof(g_vm_mock_ring_rt.targetNpc), "%s",
             row.targetNpc);
    vm_mock_ring_format_desc();
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_RING_WT_KIND,
                                     VM_MOCK_RING_WT_STATUS, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "ringCount", row.ringCount) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "dailyMax",
                                    g_vm_mock_ring_config.dailyMax) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "bonusClaimed",
                                    row.bonusClaimed) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "taskType", row.taskType) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "targetId", row.targetId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "targetCount",
                                    row.targetCount) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "progress", row.progress) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "targetNpc",
                                       g_vm_mock_ring_rt.targetNpc) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "desc",
                                       g_vm_mock_ring_rt.desc) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "taskActive", row.taskActive))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_activities_finish_packet(out, pos, 1);
}

static bool vm_mock_ring_task_ready(vm_net_mock_role_state *role)
{
    u32 i;
    u32 medCount = 0;
    u32 equipCount = 0;
    if (role == NULL || !g_vm_mock_ring_rt.active)
        return false;
    switch (g_vm_mock_ring_rt.taskType)
    {
    case VM_MOCK_RING_TYPE_VISIT:
        return g_vm_mock_ring_rt.progress >= 1;
    case VM_MOCK_RING_TYPE_KILL_MOB:
    case VM_MOCK_RING_TYPE_KILL_BOSS:
    case VM_MOCK_RING_TYPE_KILL_PLAYER:
    case VM_MOCK_RING_TYPE_WORLD_TRUTH:
        return g_vm_mock_ring_rt.progress >= g_vm_mock_ring_rt.targetCount;
    case VM_MOCK_RING_TYPE_COLLECT:
        return vm_mock_activities_backpack_count(role, g_vm_mock_ring_rt.targetId) >=
               g_vm_mock_ring_rt.targetCount;
    case VM_MOCK_RING_TYPE_SUBMIT_MED:
        if (g_vm_mock_ring_rt.targetId != 0)
        {
            return vm_mock_activities_backpack_count(
                       role, g_vm_mock_ring_rt.targetId) >=
                   vm_mock_ring_med_need_count();
        }
        for (i = 0; i < role->backpackItemCount &&
                    i < VM_NET_MOCK_BACKPACK_MAX_ITEMS;
             ++i)
        {
            if (vm_mock_ring_item_matches_submit_med(role->backpackItems[i].itemId))
                medCount += role->backpackItems[i].count;
        }
        return medCount >= vm_mock_ring_med_need_count();
    case VM_MOCK_RING_TYPE_SUBMIT_EQUIP:
        if (g_vm_mock_ring_rt.targetId != 0)
        {
            return vm_mock_activities_backpack_count(
                       role, g_vm_mock_ring_rt.targetId) >=
                   g_vm_mock_ring_rt.targetCount;
        }
        for (i = 0; i < role->backpackItemCount &&
                    i < VM_NET_MOCK_BACKPACK_MAX_ITEMS;
             ++i)
        {
            if (vm_mock_ring_item_matches_submit_equip(
                    role->backpackItems[i].itemId))
                equipCount += role->backpackItems[i].count;
        }
        return equipCount >= g_vm_mock_ring_rt.targetCount;
    default:
        return false;
    }
}

static u32 vm_net_mock_build_ring_accept_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();

    vm_mock_ring_load_config(false);
    if (role == NULL || !g_vm_mock_ring_config.enabled)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_ACCEPT, 2,
                                                  "disabled");
    memset(&row, 0, sizeof(row));
    if (vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
    {
        snprintf(query, sizeof(query),
                 "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
                 "progress,target_npc,task_active FROM account_role_ring_daily "
                 "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
                 accountHex, role->roleId, dayYmd);
        (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    }
    if (row.ringCount >= g_vm_mock_ring_config.dailyMax)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_ACCEPT, 3, "dailyfull");
    if (row.taskActive)
        return vm_net_mock_build_ring_status_response(out, outCap);
    vm_mock_yanmen_load_config(false);
    vm_mock_ring_build_desc();
    (void)vm_mock_ring_save_runtime(role, row.ringCount, row.bonusClaimed);
    return vm_net_mock_build_ring_status_response(out, outCap);
}

static u32 vm_net_mock_build_ring_submit_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();
    u32 rewardExp;
    u32 rewardMoney;
    u16 seq = 0;
    u32 keyIdx;

    vm_mock_ring_load_config(false);
    vm_mock_ring_sync_runtime_from_db(role);
    if (role == NULL)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_SUBMIT, 2, "norole");
    memset(&row, 0, sizeof(row));
    if (vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
    {
        snprintf(query, sizeof(query),
                 "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
                 "progress,target_npc,task_active FROM account_role_ring_daily "
                 "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
                 accountHex, role->roleId, dayYmd);
        (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    }
    if (!row.taskActive)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_SUBMIT, 3, "noactive");
    if (!vm_mock_ring_task_ready(role))
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_SUBMIT, 4, "notready");
    if (g_vm_mock_ring_rt.taskType == VM_MOCK_RING_TYPE_COLLECT)
    {
        vm_net_mock_consume_sync_row sync[VM_NET_MOCK_CONSUME_SYNC_MAX];
        u32 syncCount = 0;
        (void)vm_net_mock_role_consume_backpack_item_amount(
            role, g_vm_mock_ring_rt.targetId, g_vm_mock_ring_rt.targetCount, sync,
            VM_NET_MOCK_CONSUME_SYNC_MAX, &syncCount);
    }
    else if (g_vm_mock_ring_rt.taskType == VM_MOCK_RING_TYPE_SUBMIT_MED)
    {
        u32 medNeed = vm_mock_ring_med_need_count();

        if (g_vm_mock_ring_rt.targetId != 0)
        {
            vm_net_mock_consume_sync_row sync[VM_NET_MOCK_CONSUME_SYNC_MAX];
            u32 syncCount = 0;

            if (!vm_net_mock_role_consume_backpack_item_amount(
                    role, g_vm_mock_ring_rt.targetId, medNeed, sync,
                    VM_NET_MOCK_CONSUME_SYNC_MAX, &syncCount))
                return vm_mock_activities_build_op_result(
                    out, outCap, VM_MOCK_RING_WT_KIND, VM_MOCK_RING_WT_SUBMIT, 5,
                    "consume");
        }
        else if (!vm_mock_ring_consume_matching_items(
                     role, medNeed, vm_mock_ring_item_matches_submit_med))
            return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                      VM_MOCK_RING_WT_SUBMIT, 5,
                                                      "consume");
    }
    else if (g_vm_mock_ring_rt.taskType == VM_MOCK_RING_TYPE_SUBMIT_EQUIP)
    {
        if (g_vm_mock_ring_rt.targetId != 0)
        {
            u32 need = g_vm_mock_ring_rt.targetCount;
            u32 guard = 0;

            while (need > 0 && guard < 256)
            {
                vm_net_mock_backpack_item_state *row = NULL;
                u32 instanceId = 0;
                u16 seq = 0;
                u32 itemId = g_vm_mock_ring_rt.targetId;
                u32 i;

                ++guard;
                for (i = 0; i < role->backpackItemCount &&
                            i < VM_NET_MOCK_BACKPACK_MAX_ITEMS;
                     ++i)
                {
                    if (role->backpackItems[i].itemId == itemId &&
                        role->backpackItems[i].count != 0)
                    {
                        row = &role->backpackItems[i];
                        break;
                    }
                }
                if (row == NULL)
                    return vm_mock_activities_build_op_result(
                        out, outCap, VM_MOCK_RING_WT_KIND, VM_MOCK_RING_WT_SUBMIT,
                        5, "consume");
                instanceId = row->enhanceInstanceId;
                seq = row->seq;
                if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1,
                                                            NULL))
                    return vm_mock_activities_build_op_result(
                        out, outCap, VM_MOCK_RING_WT_KIND, VM_MOCK_RING_WT_SUBMIT,
                        5, "consume");
                (void)vm_net_mock_equipment_instance_retire(role->roleId,
                                                            instanceId, seq);
                need -= 1;
            }
            if (need != 0)
                return vm_mock_activities_build_op_result(
                    out, outCap, VM_MOCK_RING_WT_KIND, VM_MOCK_RING_WT_SUBMIT, 5,
                    "consume");
        }
        else if (!vm_mock_ring_consume_matching_items(
                     role, g_vm_mock_ring_rt.targetCount,
                     vm_mock_ring_item_matches_submit_equip))
            return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                      VM_MOCK_RING_WT_SUBMIT, 5,
                                                      "consume");
    }
    rewardExp = g_vm_mock_ring_config.baseExp +
                g_vm_mock_ring_config.expPerRing * (u32)(row.ringCount + 1);
    rewardMoney = g_vm_mock_ring_config.baseMoney +
                  g_vm_mock_ring_config.moneyPerRing * (u32)(row.ringCount + 1);
    (void)vm_net_mock_role_add_exp(role, rewardExp);
    role->money = vm_net_mock_add_capped_u32(role->money, rewardMoney);
    vm_net_mock_role_mark_inventory_dirty("ring-reward-money");
    if (g_vm_mock_ring_config.keyDropRate > 0 &&
        (vm_mock_activities_rng_next() % 100) < g_vm_mock_ring_config.keyDropRate)
    {
        keyIdx = vm_mock_activities_rng_next() % 3;
        (void)vm_net_mock_role_add_backpack_item_to_role(
            role, g_vm_mock_ring_config.keyItemIds[keyIdx], 1, 0, &seq,
            "ring-key", NULL, 0);
    }
    row.ringCount += 1;
    g_vm_mock_ring_rt.active = 0;
    g_vm_mock_ring_rt.progress = 0;
    (void)vm_mock_activities_vitality_add(role,
                                          VM_MOCK_ACTIVITIES_RING_VITALITY_REWARD);
    (void)vm_mock_ring_save_runtime(role, row.ringCount, row.bonusClaimed);
    vm_net_mock_role_db_save("ring-submit");
    return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                              VM_MOCK_RING_WT_SUBMIT, 1, "ok");
}

static u32 vm_net_mock_build_ring_claim_daily_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();
    u16 seq = 0;

    vm_mock_ring_load_config(false);
    if (role == NULL)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_CLAIM_DAILY, 2,
                                                  "norole");
    memset(&row, 0, sizeof(row));
    if (vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
    {
        snprintf(query, sizeof(query),
                 "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
                 "progress,target_npc,task_active FROM account_role_ring_daily "
                 "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
                 accountHex, role->roleId, dayYmd);
        (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    }
    if (row.bonusClaimed)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_CLAIM_DAILY, 3,
                                                  "claimed");
    if (row.ringCount < g_vm_mock_ring_config.dailyMax)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_CLAIM_DAILY, 4,
                                                  "notfull");
    if (!vm_net_mock_role_add_backpack_item_to_role(
            role, g_vm_mock_ring_config.dailyBonusItemId,
            g_vm_mock_ring_config.dailyBonusCount, 0, &seq, "ring-daily-bonus",
            NULL, 0))
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_CLAIM_DAILY, 5,
                                                  "bagfull");
    row.bonusClaimed = 1;
    (void)vm_mock_ring_save_runtime(role, row.ringCount, row.bonusClaimed);
    vm_net_mock_role_db_save("ring-daily-bonus");
    return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                              VM_MOCK_RING_WT_CLAIM_DAILY, 1, "ok");
}

/*
 * 消耗酷宝刷新当前进行中的环任务：不增减当日环数，只重抽目标。
 * result: 1=ok 2=norole 3=disabled 4=noactive 5=nowcoin 6=savefail
 */
static u32 vm_net_mock_build_ring_refresh_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_ring_daily_ctx row;
    char accountHex[160];
    char query[640];
    u32 dayYmd = vm_mock_activities_day_ymd();
    u32 wcoinBefore = 0;
    vm_mock_ring_runtime prevRt;

    vm_mock_ring_load_config(false);
    if (role == NULL)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_REFRESH, 2,
                                                  "norole");
    if (!g_vm_mock_ring_config.enabled)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_REFRESH, 3,
                                                  "disabled");
    memset(&row, 0, sizeof(row));
    if (vm_mock_activities_account_hex(accountHex, sizeof(accountHex)))
    {
        snprintf(query, sizeof(query),
                 "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
                 "progress,target_npc,task_active FROM account_role_ring_daily "
                 "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
                 accountHex, role->roleId, dayYmd);
        (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    }
    if (!row.taskActive)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_REFRESH, 4,
                                                  "noactive");

    wcoinBefore = role->wcoin;
    if (wcoinBefore < (u32)VM_MOCK_RING_REFRESH_WCOIN)
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_REFRESH, 5,
                                                  "nowcoin");

    prevRt = g_vm_mock_ring_rt;
    role->wcoin = wcoinBefore - (u32)VM_MOCK_RING_REFRESH_WCOIN;
    vm_mock_yanmen_load_config(false);
    vm_mock_ring_build_desc();
    if (!vm_mock_ring_save_runtime(role, row.ringCount, row.bonusClaimed) ||
        !vm_net_mock_role_db_save("ring-refresh-wcoin"))
    {
        role->wcoin = wcoinBefore;
        g_vm_mock_ring_rt = prevRt;
        (void)vm_mock_ring_save_runtime(role, row.ringCount, row.bonusClaimed);
        return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                                  VM_MOCK_RING_WT_REFRESH, 6,
                                                  "savefail");
    }
    printf("[info][mock-service] ring_refresh role=%u cost=%u wcoin=%u->%u "
           "ring=%u evidence=wcoin-refresh-keep-count\n",
           role->roleId, (u32)VM_MOCK_RING_REFRESH_WCOIN, wcoinBefore, role->wcoin,
           (u32)row.ringCount);
    return vm_mock_activities_build_op_result(out, outCap, VM_MOCK_RING_WT_KIND,
                                              VM_MOCK_RING_WT_REFRESH, 1, "ok");
}

static bool vm_net_mock_is_activities46_request(const u8 *request, u32 requestLen,
                                                u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != VM_MOCK_ACTIVITIES_WT_KIND)
        return false;
    if (subtypeOut)
        *subtypeOut = object.subtype;
    return !vm_net_mock_next_request_object(request, requestLen, &offset, &object);
}

static u32 vm_net_mock_parse_activities_exchange_id(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 exchangeId = 0;

    if (request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return 0;
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                           "exchangeId", &exchangeId);
    return exchangeId;
}

u32 vm_net_mock_build_activities_protocol_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u8 subtype = 0;
    u32 exchangeId = 0;

    if (!vm_net_mock_is_activities46_request(request, requestLen, &subtype))
        return 0;
    if (subtype == VM_MOCK_ACTIVITIES_WT_STATUS)
        return vm_net_mock_build_activities_status_response(out, outCap);
    if (subtype == VM_MOCK_ACTIVITIES_WT_FESTIVAL_EXCHANGE)
    {
        exchangeId = vm_net_mock_parse_activities_exchange_id(request, requestLen);
        return vm_net_mock_build_festival_exchange_response(out, outCap, exchangeId);
    }
    if (subtype == VM_MOCK_ACTIVITIES_WT_YANMEN_LIST)
        return vm_net_mock_build_yanmen_list_response(out, outCap);
    if (subtype == VM_MOCK_ACTIVITIES_WT_YANMEN_EXCHANGE)
    {
        exchangeId = vm_net_mock_parse_activities_exchange_id(request, requestLen);
        return vm_net_mock_build_yanmen_exchange_do_response(out, outCap, exchangeId, 1);
    }
    return 0;
}

static bool vm_net_mock_is_ring47_request(const u8 *request, u32 requestLen,
                                          u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != VM_MOCK_RING_WT_KIND)
        return false;
    if (subtypeOut)
        *subtypeOut = object.subtype;
    return !vm_net_mock_next_request_object(request, requestLen, &offset, &object);
}

u32 vm_net_mock_build_ring_protocol_response(const u8 *request, u32 requestLen,
                                             u8 *out, u32 outCap)
{
    u8 subtype = 0;
    if (!vm_net_mock_is_ring47_request(request, requestLen, &subtype))
        return 0;
    if (subtype == VM_MOCK_RING_WT_STATUS)
        return vm_net_mock_build_ring_status_response(out, outCap);
    if (subtype == VM_MOCK_RING_WT_ACCEPT)
        return vm_net_mock_build_ring_accept_response(out, outCap);
    if (subtype == VM_MOCK_RING_WT_SUBMIT)
        return vm_net_mock_build_ring_submit_response(out, outCap);
    if (subtype == VM_MOCK_RING_WT_CLAIM_DAILY)
        return vm_net_mock_build_ring_claim_daily_response(out, outCap);
    if (subtype == VM_MOCK_RING_WT_REFRESH)
        return vm_net_mock_build_ring_refresh_response(out, outCap);
    return 0;
}

const vm_mock_festival_config *vm_mock_festival_config_get(void)
{
    vm_mock_festival_load_config(false);
    return &g_vm_mock_festival_config;
}

const char *vm_mock_festival_welcome_text(void)
{
    vm_mock_festival_load_config(false);
    if (g_vm_mock_festival_config.welcomeText[0])
        return g_vm_mock_festival_config.welcomeText;
    return "\xbb\xd6\xd3\xad\xc0\xb4\xb5\xbd\xbd\xda\xc8\xd5\xca\xb9\xd5\xdf"
           "\xa3\xac\xbf\xc9\xd3\xc3\xbb\xee\xc1\xa6\xb6\xd2\xbb\xbb\xb5\xc0"
           "\xbe\xdf\xa1\xa3"; /* 欢迎来到节日使者，可用活力兑换道具。 */
}

u32 vm_mock_festival_exchange_admin_count(void)
{
    vm_mock_festival_load_config(false);
    return g_vm_mock_festival_exchange_count;
}

const vm_mock_festival_exchange *vm_mock_festival_exchange_admin_get(u32 index)
{
    if (index >= g_vm_mock_festival_exchange_count)
        return NULL;
    return &g_vm_mock_festival_exchanges[index];
}

const vm_mock_yanmen_config *vm_mock_yanmen_config_get(void)
{
    vm_mock_yanmen_load_config(false);
    return &g_vm_mock_yanmen_config;
}

u32 vm_mock_yanmen_exchange_admin_count(void)
{
    vm_mock_yanmen_load_config(false);
    return g_vm_mock_yanmen_exchange_count;
}

const vm_mock_yanmen_exchange *vm_mock_yanmen_exchange_admin_get(u32 index)
{
    if (index >= g_vm_mock_yanmen_exchange_count)
        return NULL;
    return &g_vm_mock_yanmen_exchanges[index];
}

const vm_mock_ring_config *vm_mock_ring_config_get(void)
{
    vm_mock_ring_load_config(false);
    return &g_vm_mock_ring_config;
}

bool vm_mock_festival_admin_save_config(u8 enabled, const char *npcKeyword,
                                        const char *welcomeText)
{
    char kwHex[96];
    char welcomeHex[320];
    char query[768];

    if (!vm_mock_activities_ensure_tables())
        return false;
    vm_mysql_hex_encode(npcKeyword ? npcKeyword : "", npcKeyword ? strlen(npcKeyword) : 0,
                        kwHex, sizeof(kwHex));
    vm_mysql_hex_encode(welcomeText ? welcomeText : "",
                        welcomeText ? strlen(welcomeText) : 0, welcomeHex,
                        sizeof(welcomeHex));
    snprintf(query, sizeof(query),
             "INSERT INTO server_festival_config "
             "(id,enabled,npc_keyword,welcome_text) VALUES "
             "(1,%u,X'%s',X'%s') ON DUPLICATE KEY UPDATE "
             "enabled=VALUES(enabled),npc_keyword=VALUES(npc_keyword),"
             "welcome_text=VALUES(welcome_text)",
             (u32)enabled, kwHex, welcomeHex);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_festival_config_loaded = false;
    vm_mock_festival_load_config(true);
    return true;
}

bool vm_mock_festival_admin_save_exchange(u32 exchangeId, u32 vitalityCost,
                                          u32 itemId, u32 itemCount,
                                          const char *label, u16 sortOrder,
                                          u8 enabled)
{
    char labelHex[128];
    char query[512];

    if (exchangeId == 0 || !vm_mock_activities_ensure_tables())
        return false;
    {
        char clippedLabel[48];

        memset(clippedLabel, 0, sizeof(clippedLabel));
        if (label != NULL && label[0] != 0)
            snprintf(clippedLabel, sizeof(clippedLabel), "%s", label);
        vm_mock_activities_clip_binary_label(clippedLabel, sizeof(clippedLabel));
        vm_mysql_hex_encode(clippedLabel, strlen(clippedLabel), labelHex,
                            sizeof(labelHex));
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_festival_exchange "
             "(exchange_id,vitality_cost,item_id,item_count,label,sort_order,enabled) "
             "VALUES (%u,%u,%u,%u,X'%s',%u,%u) ON DUPLICATE KEY UPDATE "
             "vitality_cost=VALUES(vitality_cost),item_id=VALUES(item_id),"
             "item_count=VALUES(item_count),label=VALUES(label),"
             "sort_order=VALUES(sort_order),enabled=VALUES(enabled)",
             exchangeId, vitalityCost, itemId, itemCount ? itemCount : 1, labelHex,
             (u32)sortOrder, (u32)enabled);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_festival_config_loaded = false;
    vm_mock_festival_load_config(true);
    return true;
}

bool vm_mock_yanmen_admin_save_config(u8 enabled, const char *sceneKeyword,
                                      const char *npcKeyword, u32 woodItemId,
                                      u32 stoneItemId, u8 woodMin, u8 woodMax,
                                      u8 stoneMin, u8 stoneMax)
{
    char sceneHex[96];
    char npcHex[96];
    char query[768];

    if (!vm_mock_activities_ensure_tables())
        return false;
    vm_mysql_hex_encode(sceneKeyword ? sceneKeyword : "",
                        sceneKeyword ? strlen(sceneKeyword) : 0, sceneHex,
                        sizeof(sceneHex));
    vm_mysql_hex_encode(npcKeyword ? npcKeyword : "",
                        npcKeyword ? strlen(npcKeyword) : 0, npcHex,
                        sizeof(npcHex));
    snprintf(query, sizeof(query),
             "INSERT INTO server_yanmen_config "
             "(id,enabled,scene_keyword,npc_keyword,wood_item_id,stone_item_id,"
             "wood_drop_min,wood_drop_max,stone_drop_min,stone_drop_max) VALUES "
             "(1,%u,X'%s',X'%s',%u,%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "enabled=VALUES(enabled),scene_keyword=VALUES(scene_keyword),"
             "npc_keyword=VALUES(npc_keyword),wood_item_id=VALUES(wood_item_id),"
             "stone_item_id=VALUES(stone_item_id),wood_drop_min=VALUES(wood_drop_min),"
             "wood_drop_max=VALUES(wood_drop_max),stone_drop_min=VALUES(stone_drop_min),"
             "stone_drop_max=VALUES(stone_drop_max)",
             (u32)enabled, sceneHex, npcHex, woodItemId, stoneItemId,
             (u32)woodMin, (u32)woodMax, (u32)stoneMin, (u32)stoneMax);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_yanmen_config_loaded = false;
    vm_mock_yanmen_load_config(true);
    return true;
}

bool vm_mock_yanmen_admin_save_exchange(u32 exchangeId, u32 woodCost, u32 stoneCost,
                                        u32 itemId, u32 itemCount, const char *label,
                                        u16 sortOrder, u8 enabled)
{
    char labelHex[128];
    char query[512];

    if (exchangeId == 0 || !vm_mock_activities_ensure_tables())
        return false;
    {
        char clippedLabel[48];

        memset(clippedLabel, 0, sizeof(clippedLabel));
        if (label != NULL && label[0] != 0)
            snprintf(clippedLabel, sizeof(clippedLabel), "%s", label);
        vm_mock_activities_clip_binary_label(clippedLabel, sizeof(clippedLabel));
        vm_mysql_hex_encode(clippedLabel, strlen(clippedLabel), labelHex,
                            sizeof(labelHex));
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_yanmen_exchange "
             "(exchange_id,wood_cost,stone_cost,item_id,item_count,label,sort_order,enabled) "
             "VALUES (%u,%u,%u,%u,%u,X'%s',%u,%u) ON DUPLICATE KEY UPDATE "
             "wood_cost=VALUES(wood_cost),stone_cost=VALUES(stone_cost),"
             "item_id=VALUES(item_id),item_count=VALUES(item_count),label=VALUES(label),"
             "sort_order=VALUES(sort_order),enabled=VALUES(enabled)",
             exchangeId, woodCost, stoneCost, itemId, itemCount ? itemCount : 1,
             labelHex, (u32)sortOrder, (u32)enabled);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_yanmen_config_loaded = false;
    vm_mock_yanmen_load_config(true);
    return true;
}

bool vm_mock_ring_admin_save_config(u8 enabled, const char *npcKeyword, u16 dailyMax,
                                    u32 bonusItemId, u32 bonusCount, u8 keyRate,
                                    u32 key1, u32 key2, u32 key3, u32 baseExp,
                                    u32 baseMoney, u32 expPerRing, u32 moneyPerRing)
{
    char npcHex[96];
    char query[768];

    if (!vm_mock_activities_ensure_tables())
        return false;
    vm_mysql_hex_encode(npcKeyword ? npcKeyword : "",
                        npcKeyword ? strlen(npcKeyword) : 0, npcHex, sizeof(npcHex));
    snprintf(query, sizeof(query),
             "INSERT INTO server_ring_config "
             "(id,enabled,npc_keyword,daily_max,daily_bonus_item_id,daily_bonus_count,"
             "key_drop_rate,key_item_id1,key_item_id2,key_item_id3,base_exp,base_money,"
             "exp_per_ring,money_per_ring) VALUES "
             "(1,%u,X'%s',%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "enabled=VALUES(enabled),npc_keyword=VALUES(npc_keyword),"
             "daily_max=VALUES(daily_max),daily_bonus_item_id=VALUES(daily_bonus_item_id),"
             "daily_bonus_count=VALUES(daily_bonus_count),key_drop_rate=VALUES(key_drop_rate),"
             "key_item_id1=VALUES(key_item_id1),key_item_id2=VALUES(key_item_id2),"
             "key_item_id3=VALUES(key_item_id3),base_exp=VALUES(base_exp),"
             "base_money=VALUES(base_money),exp_per_ring=VALUES(exp_per_ring),"
             "money_per_ring=VALUES(money_per_ring)",
             (u32)enabled, npcHex, (u32)dailyMax, bonusItemId,
             bonusCount ? bonusCount : 1, (u32)keyRate, key1, key2, key3, baseExp,
             baseMoney, expPerRing, moneyPerRing);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_ring_config_loaded = false;
    return true;
}

/* 后台：清除角色当日已接跑环（保留 ring_count / 满环奖励状态）。 */
bool vm_mock_ring_admin_clear_active_task(const char *accountId, u32 roleId,
                                          const char **errorOut)
{
    char accountHex[160];
    char query[768];
    u32 dayYmd;
    vm_mock_ring_daily_ctx row;
    vm_mock_service_client_session *session;

    if (errorOut)
        *errorOut = NULL;
    if (accountId == NULL || accountId[0] == 0 || roleId == 0)
    {
        if (errorOut)
            *errorOut = "账号或角色无效";
        return false;
    }
    if (!vm_mock_activities_ensure_tables())
    {
        if (errorOut)
            *errorOut = "数据库不可用";
        return false;
    }
    if (!vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
    {
        if (errorOut)
            *errorOut = "账号编码失败";
        return false;
    }
    dayYmd = vm_mock_activities_day_ymd();
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    if (!row.taskActive)
    {
        if (errorOut)
            *errorOut = "当前没有进行中的跑环任务";
        return false;
    }
    snprintf(query, sizeof(query),
             "UPDATE account_role_ring_daily SET "
             "task_type=0,target_id=0,target_count=0,progress=0,"
             "target_npc=X'',task_active=0 "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u",
             accountHex, roleId, dayYmd);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = "清除跑环任务失败";
        return false;
    }
    /* 在线同角色：清掉进程内环运行时，避免击杀进度把旧任务写回 */
    session = g_vm_mock_service_client_sessions;
    while (session != NULL)
    {
        if (session->accountId[0] != 0 &&
            strcmp(session->accountId, accountId) == 0 &&
            session->onlineRoleId == roleId)
        {
            memset(&g_vm_mock_ring_rt, 0, sizeof(g_vm_mock_ring_rt));
            break;
        }
        session = session->next;
    }
    printf("[info][mock-service] ring_admin_clear_active account=%s role=%u "
           "day=%u type=%u progress=%u/%u evidence=admin-clear\n",
           accountId, roleId, dayYmd, (u32)row.taskType, row.progress,
           row.targetCount);
    return true;
}

/* 后台角色页：当日是否有进行中环，以及已完成环数。 */
bool vm_mock_ring_admin_peek_active(const char *accountId, u32 roleId,
                                    u8 *activeOut, u16 *ringCountOut)
{
    char accountHex[160];
    char query[640];
    u32 dayYmd;
    vm_mock_ring_daily_ctx row;

    if (activeOut)
        *activeOut = 0;
    if (ringCountOut)
        *ringCountOut = 0;
    if (accountId == NULL || accountId[0] == 0 || roleId == 0)
        return false;
    if (!vm_mock_activities_ensure_tables() ||
        !vm_mock_activities_account_hex_of(accountId, accountHex, sizeof(accountHex)))
        return false;
    dayYmd = vm_mock_activities_day_ymd();
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT ring_count,bonus_claimed,task_type,target_id,target_count,"
             "progress,target_npc,task_active FROM account_role_ring_daily "
             "WHERE account_id=UNHEX('%s') AND role_id=%u AND day_ymd=%u LIMIT 1",
             accountHex, roleId, dayYmd);
    (void)vm_mysql_query(query, vm_mock_ring_daily_row_cb, &row);
    if (activeOut)
        *activeOut = row.taskActive ? 1u : 0u;
    if (ringCountOut)
        *ringCountOut = row.ringCount;
    return true;
}
