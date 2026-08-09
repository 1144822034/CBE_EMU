/*
 * PixiJS_web 比武擂台 — schedule, enter, bucket visibility, matchmaking,
 * scoring, settle/claim.  Included from mock-server.c after guild (duel_begin).
 */

enum
{
    VM_MOCK_ARENA_STATUS_IDLE = 0,
    VM_MOCK_ARENA_STATUS_ENTRY = 1,
    VM_MOCK_ARENA_STATUS_LIVE = 2,
    VM_MOCK_ARENA_STATUS_SETTLED = 3,
    VM_MOCK_ARENA_PARTICIPANT_MAX = 256,
    VM_MOCK_ARENA_REWARD_MAX = 512,
    VM_MOCK_ARENA_WT_KIND = 41,
    VM_MOCK_ARENA_WT_STATUS = 1,
    VM_MOCK_ARENA_WT_RANK = 2,
    VM_MOCK_ARENA_WT_CLAIM = 3,
    VM_MOCK_ARENA_WT_LEAVE = 4,
    VM_MOCK_ARENA_WT_HUD = 5
};

/* GBK: c04比武擂台 / 比武 */
static const char VM_MOCK_ARENA_DEFAULT_SCENE[] =
    "c04\xb1\xc8\xce\xe4\xc0\xde\xcc\xa8";
static const char VM_MOCK_ARENA_DEFAULT_NPC_KEYWORD[] = "\xb1\xc8\xce\xe4";

typedef struct
{
    u8 enabled;
    char scene[64];
    u16 spawnX;
    u16 spawnY;
    u32 weekdayMask;
    u16 entryStartMinute;
    u16 entryEndMinute;
    u16 matchStartMinute;
    u16 matchEndMinute;
    u32 roundSeconds;
    u32 battleTimeoutSeconds;
    u32 scoreWin;
    u32 scoreBye;
    u32 scoreDraw;
    char npcNameKeyword[32];
    u8 wagerEnabled;
    u8 wagerFeePercent;
    u8 wagerCurrency;
} vm_mock_arena_config;

typedef struct
{
    u8 job;
    u16 bracket;
    u8 rankPos;
    u32 rewardExp;
    u32 rewardMoney;
    u32 rewardWcoin;
    u32 itemId;
    u32 itemCount;
    u32 equipId;
} vm_mock_arena_reward;

typedef struct
{
    u32 clientId;
    u32 roleId;
    char accountId[64];
    char roleName[32];
    u8 job;
    u16 bracket;
    u16 level;
    u32 score;
    u32 wins;
    u32 losses;
    u32 draws;
    u32 byes;
    u32 rankPos;
    u8 claimed;
    u8 inArenaScene;
    u8 matchedThisRound;
    char returnScene[64];
    u16 returnX;
    u16 returnY;
} vm_mock_arena_participant;

typedef struct
{
    u32 sessionId;
    u32 dayYmd;
    u8 slot;
    u8 status;
    u32 entryStartTs;
    u32 matchStartTs;
    u32 matchEndTs;
    u32 roundIndex;
    u32 roundStartedTs;
    u32 lastMatchRound;
} vm_mock_arena_runtime;

static vm_mock_arena_config g_vm_mock_arena_config;
static bool g_vm_mock_arena_config_loaded = false;
static vm_mock_arena_reward g_vm_mock_arena_rewards[VM_MOCK_ARENA_REWARD_MAX];
static u32 g_vm_mock_arena_reward_count = 0;
static vm_mock_arena_runtime g_vm_mock_arena_rt;
static vm_mock_arena_participant g_vm_mock_arena_parts[VM_MOCK_ARENA_PARTICIPANT_MAX];
static u32 g_vm_mock_arena_part_count = 0;
static u32 g_vm_mock_arena_last_tick_sec = 0;

static u16 vm_mock_arena_bracket_for_level(u32 level)
{
    if (level >= 70)
        return 70;
    if (level >= 50)
        return 50;
    if (level >= 40)
        return 40;
    if (level >= 30)
        return 30;
    if (level >= 20)
        return 20;
    if (level >= 10)
        return 10;
    return 0;
}

static bool vm_mock_arena_same_bucket(u8 jobA, u16 bracketA, u16 levelA,
                                       u8 jobB, u16 bracketB, u16 levelB)
{
    if (jobA == 0 || jobB == 0 || jobA != jobB)
        return false;
    if (bracketA == 0 || bracketB == 0 || bracketA != bracketB)
        return false;
    if (bracketA == 70)
        return levelA == levelB && levelA >= 70;
    return true;
}

static void vm_mock_arena_config_set_defaults(vm_mock_arena_config *cfg)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1;
    snprintf(cfg->scene, sizeof(cfg->scene), "%s", VM_MOCK_ARENA_DEFAULT_SCENE);
    cfg->spawnX = 120;
    cfg->spawnY = 120;
    cfg->weekdayMask = (1u << 3) | (1u << 6); /* Wed + Sat */
    cfg->entryStartMinute = 20 * 60 + 20;
    cfg->entryEndMinute = 20 * 60 + 29;
    cfg->matchStartMinute = 20 * 60 + 30;
    cfg->matchEndMinute = 21 * 60;
    cfg->roundSeconds = 300;
    cfg->battleTimeoutSeconds = 240;
    cfg->scoreWin = 20;
    cfg->scoreBye = 20;
    cfg->scoreDraw = 10;
    snprintf(cfg->npcNameKeyword, sizeof(cfg->npcNameKeyword), "%s",
             VM_MOCK_ARENA_DEFAULT_NPC_KEYWORD);
    cfg->wagerEnabled = 0;
    cfg->wagerFeePercent = 10;
    cfg->wagerCurrency = 0;
}

static bool vm_mock_arena_ensure_tables(void)
{
    static bool ready = false;
    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_arena_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "scene VARBINARY(64) NOT NULL,"
            "spawn_x SMALLINT UNSIGNED NOT NULL DEFAULT 120,"
            "spawn_y SMALLINT UNSIGNED NOT NULL DEFAULT 120,"
            "weekday_mask INT UNSIGNED NOT NULL DEFAULT 72,"
            "entry_start_minute SMALLINT UNSIGNED NOT NULL DEFAULT 1220,"
            "entry_end_minute SMALLINT UNSIGNED NOT NULL DEFAULT 1229,"
            "match_start_minute SMALLINT UNSIGNED NOT NULL DEFAULT 1230,"
            "match_end_minute SMALLINT UNSIGNED NOT NULL DEFAULT 1260,"
            "round_seconds INT UNSIGNED NOT NULL DEFAULT 300,"
            "battle_timeout_seconds INT UNSIGNED NOT NULL DEFAULT 240,"
            "score_win INT UNSIGNED NOT NULL DEFAULT 20,"
            "score_bye INT UNSIGNED NOT NULL DEFAULT 20,"
            "score_draw INT UNSIGNED NOT NULL DEFAULT 10,"
            "npc_name_keyword VARBINARY(32) NOT NULL DEFAULT '',"
            "wager_enabled TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "wager_fee_percent TINYINT UNSIGNED NOT NULL DEFAULT 10,"
            "wager_currency TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_arena_rewards ("
            "job TINYINT UNSIGNED NOT NULL,"
            "bracket SMALLINT UNSIGNED NOT NULL,"
            "rank_pos TINYINT UNSIGNED NOT NULL,"
            "reward_exp INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_money INT UNSIGNED NOT NULL DEFAULT 0,"
            "reward_wcoin INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "equip_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(job,bracket,rank_pos)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_arena_sessions ("
            "session_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "day_ymd INT UNSIGNED NOT NULL,"
            "slot TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "entry_start_ts INT UNSIGNED NOT NULL DEFAULT 0,"
            "match_start_ts INT UNSIGNED NOT NULL DEFAULT 0,"
            "match_end_ts INT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(session_id),"
            "UNIQUE KEY uk_arena_sessions_day_slot(day_ymd,slot)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_arena_scores ("
            "session_id INT UNSIGNED NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_name VARBINARY(32) NOT NULL,"
            "job TINYINT UNSIGNED NOT NULL,"
            "bracket SMALLINT UNSIGNED NOT NULL,"
            "level INT UNSIGNED NOT NULL,"
            "score INT UNSIGNED NOT NULL DEFAULT 0,"
            "wins INT UNSIGNED NOT NULL DEFAULT 0,"
            "losses INT UNSIGNED NOT NULL DEFAULT 0,"
            "draws INT UNSIGNED NOT NULL DEFAULT 0,"
            "byes INT UNSIGNED NOT NULL DEFAULT 0,"
            "rank_pos INT UNSIGNED NOT NULL DEFAULT 0,"
            "claimed TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(session_id,role_id),"
            "KEY idx_arena_scores_bucket(session_id,job,bracket,score)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_arena_titles ("
            "job TINYINT UNSIGNED NOT NULL,"
            "bracket SMALLINT UNSIGNED NOT NULL,"
            "place TINYINT UNSIGNED NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_name VARBINARY(32) NOT NULL,"
            "session_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(job,bracket,place)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_arena_wager_challenges ("
            "challenge_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "host_role_id INT UNSIGNED NOT NULL,"
            "host_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "challenger_role_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "challenger_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "scene VARBINARY(64) NOT NULL,"
            "stake_money INT UNSIGNED NOT NULL DEFAULT 0,"
            "fee_percent TINYINT UNSIGNED NOT NULL DEFAULT 10,"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(challenge_id),"
            "KEY idx_arena_wager_host(host_role_id,status)) ENGINE=InnoDB"))
    {
        return false;
    }
    ready = true;
    return true;
}

static bool vm_mock_arena_config_row(void *contextValue, unsigned int columnCount,
                                      const char *const *values, const size_t *lengths)
{
    vm_mock_arena_config *cfg = (vm_mock_arena_config *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (cfg == NULL || values == NULL || lengths == NULL)
        return false;
    vm_mock_arena_config_set_defaults(cfg);
    if (values[0] && lengths[0])
        cfg->enabled = (u8)(values[0][0] == '0' ? 0 : 1);
    if (values[1] && lengths[1] > 0 && lengths[1] < sizeof(cfg->scene))
    {
        memcpy(cfg->scene, values[1], lengths[1]);
        cfg->scene[lengths[1]] = 0;
    }
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        cfg->spawnX = (u16)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        cfg->spawnY = (u16)tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        cfg->weekdayMask = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        cfg->entryStartMinute = (u16)tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        cfg->entryEndMinute = (u16)tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        cfg->matchStartMinute = (u16)tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        cfg->matchEndMinute = (u16)tmp;
    if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
        cfg->roundSeconds = tmp ? tmp : 300;
    if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
        cfg->battleTimeoutSeconds = tmp ? tmp : 240;
    if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
        cfg->scoreWin = tmp;
    if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
        cfg->scoreBye = tmp;
    if (values[13] && vm_mock_mysql_parse_u32(values[13], lengths[13], &tmp))
        cfg->scoreDraw = tmp;
    if (values[14] && lengths[14] > 0 && lengths[14] < sizeof(cfg->npcNameKeyword))
    {
        memcpy(cfg->npcNameKeyword, values[14], lengths[14]);
        cfg->npcNameKeyword[lengths[14]] = 0;
    }
    if (values[15] && lengths[15])
        cfg->wagerEnabled = (u8)(values[15][0] == '0' ? 0 : 1);
    if (values[16] && vm_mock_mysql_parse_u32(values[16], lengths[16], &tmp))
        cfg->wagerFeePercent = (u8)tmp;
    if (values[17] && vm_mock_mysql_parse_u32(values[17], lengths[17], &tmp))
        cfg->wagerCurrency = (u8)tmp;
    return true;
}

static bool vm_mock_arena_reward_row(void *contextValue, unsigned int columnCount,
                                     const char *const *values, const size_t *lengths)
{
    u32 *count = (u32 *)contextValue;
    vm_mock_arena_reward *row;
    u32 tmp = 0;

    (void)columnCount;
    if (count == NULL || values == NULL || lengths == NULL ||
        *count >= VM_MOCK_ARENA_REWARD_MAX)
        return false;
    row = &g_vm_mock_arena_rewards[*count];
    memset(row, 0, sizeof(*row));
    if (!values[0] || !vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        return false;
    row->job = (u8)tmp;
    if (!values[1] || !vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        return false;
    row->bracket = (u16)tmp;
    if (!values[2] || !vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        return false;
    row->rankPos = (u8)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        row->rewardExp = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        row->rewardMoney = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        row->rewardWcoin = tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        row->itemId = tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        row->itemCount = tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        row->equipId = tmp;
    *count += 1;
    return true;
}

static bool vm_mock_arena_load_config(bool force)
{
    u32 count = 0;
    char sceneHex[192];
    char kwHex[96];
    char query[512];

    if (g_vm_mock_arena_config_loaded && !force)
        return true;
    if (!vm_mock_arena_ensure_tables())
        return false;
    vm_mock_arena_config_set_defaults(&g_vm_mock_arena_config);
    count = 0;
    if (vm_mysql_query(
            "SELECT enabled,scene,spawn_x,spawn_y,weekday_mask,"
            "entry_start_minute,entry_end_minute,match_start_minute,match_end_minute,"
            "round_seconds,battle_timeout_seconds,score_win,score_bye,score_draw,"
            "npc_name_keyword,wager_enabled,wager_fee_percent,wager_currency "
            "FROM server_arena_config WHERE id=1 LIMIT 1",
            vm_mock_arena_config_row, &g_vm_mock_arena_config))
    {
        /* loaded or empty */
    }
    if (g_vm_mock_arena_config.scene[0] == 0)
    {
        vm_mock_arena_config_set_defaults(&g_vm_mock_arena_config);
        if (vm_mysql_hex_encode(g_vm_mock_arena_config.scene,
                                strlen(g_vm_mock_arena_config.scene), sceneHex,
                                sizeof(sceneHex)) != 0 &&
            vm_mysql_hex_encode(g_vm_mock_arena_config.npcNameKeyword,
                                strlen(g_vm_mock_arena_config.npcNameKeyword),
                                kwHex, sizeof(kwHex)) != 0)
        {
            snprintf(query, sizeof(query),
                     "INSERT INTO server_arena_config ("
                     "id,enabled,scene,spawn_x,spawn_y,weekday_mask,"
                     "entry_start_minute,entry_end_minute,match_start_minute,"
                     "match_end_minute,round_seconds,battle_timeout_seconds,"
                     "score_win,score_bye,score_draw,npc_name_keyword,"
                     "wager_enabled,wager_fee_percent,wager_currency) VALUES ("
                     "1,1,UNHEX('%s'),%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                     "UNHEX('%s'),0,10,0) ON DUPLICATE KEY UPDATE id=id",
                     sceneHex, g_vm_mock_arena_config.spawnX,
                     g_vm_mock_arena_config.spawnY,
                     g_vm_mock_arena_config.weekdayMask,
                     g_vm_mock_arena_config.entryStartMinute,
                     g_vm_mock_arena_config.entryEndMinute,
                     g_vm_mock_arena_config.matchStartMinute,
                     g_vm_mock_arena_config.matchEndMinute,
                     g_vm_mock_arena_config.roundSeconds,
                     g_vm_mock_arena_config.battleTimeoutSeconds,
                     g_vm_mock_arena_config.scoreWin,
                     g_vm_mock_arena_config.scoreBye,
                     g_vm_mock_arena_config.scoreDraw, kwHex);
            (void)vm_mysql_exec(query);
        }
    }
    g_vm_mock_arena_reward_count = 0;
    (void)vm_mysql_query(
        "SELECT job,bracket,rank_pos,reward_exp,reward_money,reward_wcoin,"
        "item_id,item_count,equip_id FROM server_arena_rewards "
        "ORDER BY job,bracket,rank_pos",
        vm_mock_arena_reward_row, &g_vm_mock_arena_reward_count);
    g_vm_mock_arena_config_loaded = true;
    return true;
}

static bool vm_mock_arena_is_arena_scene(const char *scene)
{
    (void)vm_mock_arena_load_config(false);
    return scene != NULL && scene[0] != 0 &&
           vm_net_mock_scene_names_equal_loose(scene, g_vm_mock_arena_config.scene);
}

static bool vm_mock_arena_npc_offers_entry(const char *npcName)
{
    size_t kwLen;

    (void)vm_mock_arena_load_config(false);
    if (!g_vm_mock_arena_config.enabled || npcName == NULL || npcName[0] == 0)
        return false;
    kwLen = strlen(g_vm_mock_arena_config.npcNameKeyword);
    if (kwLen == 0)
        return false;
    return strstr(npcName, g_vm_mock_arena_config.npcNameKeyword) != NULL;
}

static void vm_mock_arena_wall_clock(u32 *dayYmdOut, u32 *minuteOfDayOut,
                                     u32 *weekdayBitOut, u32 *unixTsOut)
{
    time_t now = time(NULL);
    struct tm *tmNow = localtime(&now);

    if (dayYmdOut)
        *dayYmdOut = 0;
    if (minuteOfDayOut)
        *minuteOfDayOut = 0;
    if (weekdayBitOut)
        *weekdayBitOut = 0;
    if (unixTsOut)
        *unixTsOut = (u32)now;
    if (tmNow == NULL)
        return;
    if (dayYmdOut)
        *dayYmdOut = (u32)((tmNow->tm_year + 1900) * 10000 +
                           (tmNow->tm_mon + 1) * 100 + tmNow->tm_mday);
    if (minuteOfDayOut)
        *minuteOfDayOut = (u32)(tmNow->tm_hour * 60 + tmNow->tm_min);
    if (weekdayBitOut)
        *weekdayBitOut = 1u << (u32)tmNow->tm_wday;
}

static vm_mock_arena_participant *vm_mock_arena_find_part(u32 roleId)
{
    for (u32 i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        if (g_vm_mock_arena_parts[i].roleId == roleId)
            return &g_vm_mock_arena_parts[i];
    }
    return NULL;
}

static vm_mock_arena_participant *vm_mock_arena_find_part_by_client(u32 clientId)
{
    for (u32 i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        if (g_vm_mock_arena_parts[i].clientId == clientId)
            return &g_vm_mock_arena_parts[i];
    }
    return NULL;
}

static bool vm_mock_arena_persist_score(const vm_mock_arena_participant *p)
{
    char accountEsc[160];
    char nameHex[96];
    char query[768];

    if (p == NULL || g_vm_mock_arena_rt.sessionId == 0)
        return false;
    if (vm_mysql_hex_encode(p->accountId, strlen(p->accountId), accountEsc,
                            sizeof(accountEsc)) == 0 ||
        vm_mysql_hex_encode(p->roleName, strlen(p->roleName), nameHex,
                            sizeof(nameHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_arena_scores ("
             "session_id,role_id,account_id,role_name,job,bracket,level,"
             "score,wins,losses,draws,byes,rank_pos,claimed) VALUES ("
             "%u,%u,CAST(X'%s' AS CHAR),UNHEX('%s'),%u,%u,%u,"
             "%u,%u,%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "score=%u,wins=%u,losses=%u,draws=%u,byes=%u,rank_pos=%u,"
             "claimed=%u,level=%u,job=%u,bracket=%u,"
             "role_name=UNHEX('%s')",
             g_vm_mock_arena_rt.sessionId, p->roleId, accountEsc, nameHex,
             p->job, p->bracket, p->level, p->score, p->wins, p->losses,
             p->draws, p->byes, p->rankPos, p->claimed, p->score, p->wins,
             p->losses, p->draws, p->byes, p->rankPos, p->claimed, p->level,
             p->job, p->bracket, nameHex);
    return vm_mysql_exec(query);
}

static void vm_mock_arena_add_score(vm_mock_arena_participant *p, u32 delta,
                                    const char *kind)
{
    if (p == NULL || delta == 0)
        return;
    p->score = vm_net_mock_add_capped_u32(p->score, delta);
    (void)vm_mock_arena_persist_score(p);
    printf("[info][mock-service] arena_score role=%u +%u kind=%s total=%u\n",
           p->roleId, delta, kind ? kind : "-", p->score);
}

static bool vm_mock_arena_peer_visible(const vm_mock_service_client_session *observer,
                                        const vm_mock_service_client_session *peer)
{
    vm_mock_arena_participant *a;
    vm_mock_arena_participant *b;

    if (observer == NULL || peer == NULL)
        return true;
    if (!vm_mock_arena_is_arena_scene(observer->sceneVisibleScene))
        return true;
    a = vm_mock_arena_find_part(observer->onlineRoleId);
    b = vm_mock_arena_find_part(peer->onlineRoleId);
    if (a == NULL || b == NULL)
        return false;
    return vm_mock_arena_same_bucket(a->job, a->bracket, a->level, b->job,
                                     b->bracket, b->level);
}

static bool vm_mock_arena_ spar_forbidden_on_scene(const char *scene)
{
    return vm_mock_arena_is_arena_scene(scene);
}

/* Fix typo in function name - can't have space */
static bool vm_mock_arena_spar_forbidden_on_scene(const char *scene)
{
    return vm_mock_arena_is_arena_scene(scene);
}
