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
    /* wager subtypes 10-16 live in mock_server_arena_wager.inc.c */
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

/* Defined in mock_server_arena_wager.inc.c (included below). */
static bool vm_mock_wager_ensure_member_table(void);
static u32 vm_net_mock_build_wager_protocol_response(const u8 *request,
                                                     u32 requestLen, u8 *out,
                                                     u32 outCap, u8 subtype);
static bool vm_net_mock_append_wager_event_object(u8 *out, u32 outCap, u32 *pos);
static void vm_mock_arena_on_wager_duel_finished(vm_mock_service_duel *duel);

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
    if (!vm_mock_wager_ensure_member_table())
        return false;
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
    char query[1024];

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

static bool vm_mock_arena_wager_npc_offers(const char *npcName)
{
    size_t kwLen;

    (void)vm_mock_arena_load_config(false);
    if (!g_vm_mock_arena_config.wagerEnabled || npcName == NULL || npcName[0] == 0)
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
    (void)vm_mock_arena_load_config(false);
    if (!vm_mock_arena_is_arena_scene(observer->sceneVisibleScene))
        return true;
    /* 仅比武进场/比赛期间按职业×段位隔离；结算后恢复同图互见。 */
    if (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_ENTRY &&
        g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE)
        return true;
    if (!vm_mock_arena_is_arena_scene(peer->sceneVisibleScene))
        return false;
    a = vm_mock_arena_find_part(observer->onlineRoleId);
    b = vm_mock_arena_find_part(peer->onlineRoleId);
    if (a == NULL || b == NULL || !a->inArenaScene || !b->inArenaScene)
        return false;
    return vm_mock_arena_same_bucket(a->job, a->bracket, a->level, b->job,
                                     b->bracket, b->level);
}

static bool vm_mock_arena_team_forbidden_for_session(
    const vm_mock_service_client_session *session)
{
    if (session == NULL)
        return false;
    (void)vm_mock_arena_load_config(false);
    if (!vm_mock_arena_is_arena_scene(session->sceneVisibleScene))
        return false;
    return g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_ENTRY ||
           g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_LIVE;
}

static void vm_mock_arena_disband_team_for_session(
    vm_mock_service_client_session *session, const char *reason)
{
    static const char arenaTeamBanGbk[] =
        "\xb1\xc8\xce\xe4\xc6\xda\xbc\xe4\xbd\xfb\xd6\xb9\xd7\xe9\xb6\xd3"; /* 比武期间禁止组队 */

    if (session == NULL)
        return;
    if (vm_mock_service_team_find_for_client(session->clientId) == NULL)
        return;
    (void)vm_mock_service_session_enqueue_system_message(session, arenaTeamBanGbk);
    (void)vm_mock_service_team_remove_member(session,
                                              reason ? reason : "arena-no-team");
}

/* 离线再上线：仍在擂台场景且本场未结束 → 重绑 clientId / inArenaScene。 */
static void vm_mock_arena_rebind_session_if_needed(
    vm_mock_service_client_session *session, vm_net_mock_role_state *role)
{
    vm_mock_arena_participant *p;
    const char *scene;

    if (session == NULL || role == NULL || role->roleId == 0)
        return;
    (void)vm_mock_arena_tick();
    if (g_vm_mock_arena_rt.sessionId == 0 ||
        (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_ENTRY &&
         g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE))
        return;
    scene = session->sceneVisibleScene[0] ? session->sceneVisibleScene : role->scene;
    if (!vm_mock_arena_is_arena_scene(scene) &&
        !vm_mock_arena_is_arena_scene(role->scene))
        return;
    p = vm_mock_arena_find_part(role->roleId);
    if (p == NULL)
        return;
    p->clientId = session->clientId;
    p->inArenaScene = 1;
    p->job = role->job ? role->job : session->onlineJob;
    p->bracket = vm_mock_arena_bracket_for_level(role->level);
    p->level = (u16)(role->level ? role->level : session->onlineLevel);
    printf("[info][mock-service] arena_rebind role=%u client=%08x scene=%s "
           "status=%u score=%u evidence=offline-rejoin-keep-venue\n",
           p->roleId, p->clientId, scene ? scene : "-", g_vm_mock_arena_rt.status,
           p->score);
}

static void vm_mock_arena_note_result(vm_mock_service_client_session *session,
                                      vm_mock_arena_participant *p, const char *kind,
                                      u32 delta)
{
    /* GBK: 比武胜利/失败/平局 积分+N 当前积分M */
    static const char winGbk[] = "\xb1\xc8\xce\xe4\xca\xa4\xc0\xfb";
    static const char loseGbk[] = "\xb1\xc8\xce\xe4\xca\xa7\xb0\xdc";
    static const char drawGbk[] = "\xb1\xc8\xce\xe4\xc6\xbd\xbe\xd3";
    const char *label = loseGbk;

    if (session == NULL || p == NULL)
        return;
    if (kind != NULL)
    {
        if (strcmp(kind, "win") == 0 || strcmp(kind, "bye") == 0)
            label = winGbk;
        else if (strcmp(kind, "draw") == 0)
            label = drawGbk;
        else
            label = loseGbk;
    }
    snprintf(session->arenaResultFdata, sizeof(session->arenaResultFdata),
             "%s \xbb\xfd\xb7\xd6+%u \xb5\xb1\xc7\xb0\xbb\xfd\xb7\xd6%u", label,
             delta, p->score);
    session->arenaResultPending = true;
    printf("[info][mock-service] arena_result_note role=%u kind=%s delta=%u "
           "total=%u fdata=%s\n",
           p->roleId, kind ? kind : "-", delta, p->score, session->arenaResultFdata);
}

static bool vm_mock_arena_spar_forbidden_on_scene(const char *scene)
{
    return vm_mock_arena_is_arena_scene(scene);
}

static bool vm_mock_arena_entry_window_open(void)
{
    u32 dayYmd = 0;
    u32 minute = 0;
    u32 weekdayBit = 0;

    (void)vm_mock_arena_load_config(false);
    if (!g_vm_mock_arena_config.enabled)
        return false;
    vm_mock_arena_wall_clock(&dayYmd, &minute, &weekdayBit, NULL);
    if ((g_vm_mock_arena_config.weekdayMask & weekdayBit) == 0)
        return false;
    return minute >= g_vm_mock_arena_config.entryStartMinute &&
           minute < g_vm_mock_arena_config.entryEndMinute;
}

static u8 vm_mock_arena_desired_status(u32 minute)
{
    if (minute >= g_vm_mock_arena_config.matchEndMinute)
        return VM_MOCK_ARENA_STATUS_SETTLED;
    if (minute >= g_vm_mock_arena_config.matchStartMinute)
        return VM_MOCK_ARENA_STATUS_LIVE;
    if (minute >= g_vm_mock_arena_config.entryStartMinute &&
        minute < g_vm_mock_arena_config.matchStartMinute)
        return VM_MOCK_ARENA_STATUS_ENTRY;
    return VM_MOCK_ARENA_STATUS_IDLE;
}

static bool vm_mock_arena_session_id_row(void *contextValue, unsigned int columnCount,
                                         const char *const *values, const size_t *lengths)
{
    u32 *idOut = (u32 *)contextValue;
    (void)columnCount;
    if (idOut == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    return vm_mock_mysql_parse_u32(values[0], lengths[0], idOut);
}

static void vm_mock_arena_ensure_session(u32 dayYmd, u8 slot, u32 unixTs)
{
    char query[512];
    u32 sessionId = 0;
    u32 entryTs;
    u32 matchTs;
    u32 endTs;
    u32 dayBase;
    struct tm tmDay;
    time_t dayStart;

    if (dayYmd == 0)
        return;
    memset(&tmDay, 0, sizeof(tmDay));
    tmDay.tm_year = (int)(dayYmd / 10000) - 1900;
    tmDay.tm_mon = (int)((dayYmd / 100) % 100) - 1;
    tmDay.tm_mday = (int)(dayYmd % 100);
    tmDay.tm_isdst = -1;
    dayStart = mktime(&tmDay);
    dayBase = dayStart > 0 ? (u32)dayStart : unixTs;
    entryTs = dayBase + (u32)g_vm_mock_arena_config.entryStartMinute * 60u;
    matchTs = dayBase + (u32)g_vm_mock_arena_config.matchStartMinute * 60u;
    endTs = dayBase + (u32)g_vm_mock_arena_config.matchEndMinute * 60u;

    snprintf(query, sizeof(query),
             "SELECT session_id FROM server_arena_sessions WHERE day_ymd=%u AND slot=%u LIMIT 1",
             dayYmd, (u32)slot);
    sessionId = 0;
    (void)vm_mysql_query(query, vm_mock_arena_session_id_row, &sessionId);
    if (sessionId == 0)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_arena_sessions (day_ymd,slot,status,entry_start_ts,"
                 "match_start_ts,match_end_ts) VALUES (%u,%u,%u,%u,%u,%u)",
                 dayYmd, (u32)slot, VM_MOCK_ARENA_STATUS_ENTRY, entryTs, matchTs, endTs);
        if (!vm_mysql_exec(query))
            return;
        sessionId = 0;
        (void)vm_mysql_query(
            "SELECT LAST_INSERT_ID()", vm_mock_arena_session_id_row, &sessionId);
    }
    if (sessionId == 0)
        return;
    if (g_vm_mock_arena_rt.sessionId != sessionId)
    {
        memset(&g_vm_mock_arena_rt, 0, sizeof(g_vm_mock_arena_rt));
        g_vm_mock_arena_part_count = 0;
        g_vm_mock_arena_rt.sessionId = sessionId;
        g_vm_mock_arena_rt.dayYmd = dayYmd;
        g_vm_mock_arena_rt.slot = slot;
        g_vm_mock_arena_rt.status = VM_MOCK_ARENA_STATUS_ENTRY;
        g_vm_mock_arena_rt.entryStartTs = entryTs;
        g_vm_mock_arena_rt.matchStartTs = matchTs;
        g_vm_mock_arena_rt.matchEndTs = endTs;
        g_vm_mock_arena_rt.lastMatchRound = 0xffffffffu;
    }
}

static void vm_mock_arena_set_status(u8 status)
{
    char query[256];
    if (g_vm_mock_arena_rt.sessionId == 0 || g_vm_mock_arena_rt.status == status)
        return;
    g_vm_mock_arena_rt.status = status;
    snprintf(query, sizeof(query),
             "UPDATE server_arena_sessions SET status=%u WHERE session_id=%u",
             (u32)status, g_vm_mock_arena_rt.sessionId);
    (void)vm_mysql_exec(query);
    printf("[info][mock-service] arena_status session=%u status=%u\n",
           g_vm_mock_arena_rt.sessionId, (u32)status);
}

static int vm_mock_arena_cmp_score_desc(const void *a, const void *b)
{
    const vm_mock_arena_participant *pa = (const vm_mock_arena_participant *)a;
    const vm_mock_arena_participant *pb = (const vm_mock_arena_participant *)b;
    if (pa->score != pb->score)
        return pa->score > pb->score ? -1 : 1;
    if (pa->wins != pb->wins)
        return pa->wins > pb->wins ? -1 : 1;
    return pa->roleId < pb->roleId ? -1 : (pa->roleId > pb->roleId ? 1 : 0);
}

static const char *vm_mock_arena_place_title_gbk(u8 place)
{
    static const char champ[] = "\xb9\xda\xbe\xfc";   /* 冠军 */
    static const char second[] = "\xd1\xc7\xbe\xfc"; /* 亚军 */
    static const char third[] = "\xbc\xbe\xbe\xfc";  /* 季军 */
    if (place == 1)
        return champ;
    if (place == 2)
        return second;
    if (place == 3)
        return third;
    return "";
}

static void vm_mock_arena_apply_title(vm_mock_arena_participant *p, u8 place)
{
    char accountEsc[160];
    char nameHex[96];
    char query[640];
    vm_mock_service_client_session *session;
    const char *title;

    if (p == NULL || place < 1 || place > 3)
        return;
    title = vm_mock_arena_place_title_gbk(place);
    if (vm_mysql_hex_encode(p->accountId, strlen(p->accountId), accountEsc,
                            sizeof(accountEsc)) == 0 ||
        vm_mysql_hex_encode(p->roleName, strlen(p->roleName), nameHex,
                            sizeof(nameHex)) == 0)
        return;
    snprintf(query, sizeof(query),
             "INSERT INTO server_arena_titles (job,bracket,place,role_id,account_id,"
             "role_name,session_id) VALUES (%u,%u,%u,%u,CAST(X'%s' AS CHAR),UNHEX('%s'),%u) "
             "ON DUPLICATE KEY UPDATE role_id=%u,account_id=CAST(X'%s' AS CHAR),"
             "role_name=UNHEX('%s'),session_id=%u",
             p->job, p->bracket, (u32)place, p->roleId, accountEsc, nameHex,
             g_vm_mock_arena_rt.sessionId, p->roleId, accountEsc, nameHex,
             g_vm_mock_arena_rt.sessionId);
    (void)vm_mysql_exec(query);
    session = vm_mock_service_find_client_session(p->clientId);
    if (session != NULL && title[0] != 0)
        snprintf(session->onlineRoleTitle, sizeof(session->onlineRoleTitle), "%s", title);
}

static void vm_mock_arena_settle(void)
{
    u32 i;
    u32 j;

    if (g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_SETTLED)
        return;
    vm_mock_arena_set_status(VM_MOCK_ARENA_STATUS_SETTLED);
    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
        g_vm_mock_arena_parts[i].rankPos = 0;
    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        vm_mock_arena_participant bucket[VM_MOCK_ARENA_PARTICIPANT_MAX];
        u32 bucketCount = 0;
        u8 job = g_vm_mock_arena_parts[i].job;
        u16 bracket = g_vm_mock_arena_parts[i].bracket;
        bool seen = false;

        for (j = 0; j < i; ++j)
        {
            if (g_vm_mock_arena_parts[j].job == job &&
                g_vm_mock_arena_parts[j].bracket == bracket)
            {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        for (j = 0; j < g_vm_mock_arena_part_count; ++j)
        {
            if (g_vm_mock_arena_parts[j].job == job &&
                g_vm_mock_arena_parts[j].bracket == bracket &&
                g_vm_mock_arena_parts[j].score > 0)
                bucket[bucketCount++] = g_vm_mock_arena_parts[j];
        }
        if (bucketCount == 0)
            continue;
        qsort(bucket, bucketCount, sizeof(bucket[0]), vm_mock_arena_cmp_score_desc);
        for (j = 0; j < bucketCount; ++j)
        {
            vm_mock_arena_participant *live =
                vm_mock_arena_find_part(bucket[j].roleId);
            if (live == NULL)
                continue;
            live->rankPos = j + 1;
            (void)vm_mock_arena_persist_score(live);
            if (live->rankPos <= 3)
                vm_mock_arena_apply_title(live, (u8)live->rankPos);
        }
    }
    printf("[info][mock-service] arena_settled session=%u parts=%u\n",
           g_vm_mock_arena_rt.sessionId, g_vm_mock_arena_part_count);
}

static void vm_mock_arena_force_duel_draw(vm_mock_service_duel *duel)
{
    vm_mock_arena_participant *a;
    vm_mock_arena_participant *b;
    vm_mock_service_client_session *s0;
    vm_mock_service_client_session *s1;
    u32 drawPts;

    if (duel == NULL || !duel->active || duel->finished || !duel->arenaMode)
        return;
    drawPts = g_vm_mock_arena_config.scoreDraw;
    s0 = vm_mock_service_find_client_session(duel->clientIds[0]);
    s1 = vm_mock_service_find_client_session(duel->clientIds[1]);
    a = s0 ? vm_mock_arena_find_part(s0->onlineRoleId) : NULL;
    b = s1 ? vm_mock_arena_find_part(s1->onlineRoleId) : NULL;
    if (a != NULL)
    {
        a->draws += 1;
        vm_mock_arena_add_score(a, drawPts, "draw");
        if (s0)
            vm_mock_arena_note_result(s0, a, "draw", drawPts);
    }
    if (b != NULL)
    {
        b->draws += 1;
        vm_mock_arena_add_score(b, drawPts, "draw");
        if (s1)
            vm_mock_arena_note_result(s1, b, "draw", drawPts);
    }
    duel->arenaSettled = true;
    duel->finished = true;
    duel->terminalPendingMask = duel->startedMask ? duel->startedMask : 3u;
    duel->terminalNotBeforeTick = g_schedulerTick;
    vm_mock_service_duel_arm_both_exits(duel);
    printf("[info][mock-service] arena_duel_timeout_draw serial=%u\n", duel->serial);
}

static void vm_mock_arena_on_duel_finished(vm_mock_service_duel *duel)
{
    vm_mock_service_client_session *s0;
    vm_mock_service_client_session *s1;
    vm_mock_arena_participant *a;
    vm_mock_arena_participant *b;
    bool aDead;
    bool bDead;
    u32 winPts;
    u32 drawPts;

    if (duel == NULL || !duel->arenaMode || duel->arenaSettled)
        return;
    duel->arenaSettled = true;
    winPts = g_vm_mock_arena_config.scoreWin;
    drawPts = g_vm_mock_arena_config.scoreDraw;
    s0 = vm_mock_service_find_client_session(duel->clientIds[0]);
    s1 = vm_mock_service_find_client_session(duel->clientIds[1]);
    a = s0 ? vm_mock_arena_find_part(s0->onlineRoleId) : NULL;
    b = s1 ? vm_mock_arena_find_part(s1->onlineRoleId) : NULL;
    aDead = duel->hp[0] == 0;
    bDead = duel->hp[1] == 0;
    if (aDead && bDead)
    {
        if (a)
        {
            a->draws += 1;
            vm_mock_arena_add_score(a, drawPts, "draw");
            if (s0)
                vm_mock_arena_note_result(s0, a, "draw", drawPts);
        }
        if (b)
        {
            b->draws += 1;
            vm_mock_arena_add_score(b, drawPts, "draw");
            if (s1)
                vm_mock_arena_note_result(s1, b, "draw", drawPts);
        }
        return;
    }
    if (aDead && !bDead)
    {
        if (a)
        {
            a->losses += 1;
            if (s0)
                vm_mock_arena_note_result(s0, a, "lose", 0);
        }
        if (b)
        {
            b->wins += 1;
            vm_mock_arena_add_score(b, winPts, "win");
            if (s1)
                vm_mock_arena_note_result(s1, b, "win", winPts);
        }
        return;
    }
    if (bDead && !aDead)
    {
        if (b)
        {
            b->losses += 1;
            if (s1)
                vm_mock_arena_note_result(s1, b, "lose", 0);
        }
        if (a)
        {
            a->wins += 1;
            vm_mock_arena_add_score(a, winPts, "win");
            if (s0)
                vm_mock_arena_note_result(s0, a, "win", winPts);
        }
    }
}

static void vm_mock_arena_match_round(void)
{
    vm_mock_arena_participant *pool[VM_MOCK_ARENA_PARTICIPANT_MAX];
    u32 poolCount = 0;
    u32 i;
    u32 j;
    u32 unixTs = 0;
    u32 roundIndex;
    u32 elapsed;

    vm_mock_arena_wall_clock(NULL, NULL, NULL, &unixTs);
    if (g_vm_mock_arena_rt.matchStartTs == 0 || unixTs < g_vm_mock_arena_rt.matchStartTs)
        return;
    elapsed = unixTs - g_vm_mock_arena_rt.matchStartTs;
    roundIndex = 1u + elapsed / (g_vm_mock_arena_config.roundSeconds
                                     ? g_vm_mock_arena_config.roundSeconds
                                     : 300u);
    if (unixTs >= g_vm_mock_arena_rt.matchEndTs)
        return;
    if (g_vm_mock_arena_rt.lastMatchRound == roundIndex)
        return;
    g_vm_mock_arena_rt.roundIndex = roundIndex;
    g_vm_mock_arena_rt.roundStartedTs = unixTs;
    g_vm_mock_arena_rt.lastMatchRound = roundIndex;

    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
        g_vm_mock_arena_parts[i].matchedThisRound = 0;

    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        vm_mock_arena_participant *seed = &g_vm_mock_arena_parts[i];
        bool alreadyGrouped = false;

        if (!seed->inArenaScene)
            continue;
        for (j = 0; j < i; ++j)
        {
            vm_mock_arena_participant *prev = &g_vm_mock_arena_parts[j];
            if (prev->inArenaScene &&
                vm_mock_arena_same_bucket(prev->job, prev->bracket, prev->level,
                                         seed->job, seed->bracket, seed->level))
            {
                alreadyGrouped = true;
                break;
            }
        }
        if (alreadyGrouped)
            continue;
        poolCount = 0;
        for (j = 0; j < g_vm_mock_arena_part_count; ++j)
        {
            vm_mock_arena_participant *cand = &g_vm_mock_arena_parts[j];
            vm_mock_service_client_session *sess;
            if (!cand->inArenaScene || cand->matchedThisRound)
                continue;
            if (!vm_mock_arena_same_bucket(seed->job, seed->bracket, seed->level,
                                          cand->job, cand->bracket, cand->level))
                continue;
            sess = vm_mock_service_find_client_session(cand->clientId);
            if (sess == NULL || !sess->roleOnline || !sess->sceneVisibleReady)
                continue;
            if (vm_mock_service_duel_find_for_client(cand->clientId, NULL) != NULL)
                continue;
            pool[poolCount++] = cand;
        }
        for (j = poolCount; j > 1; --j)
        {
            u32 k = (u32)(rand() % (int)j);
            vm_mock_arena_participant *tmp = pool[j - 1];
            pool[j - 1] = pool[k];
            pool[k] = tmp;
        }
        for (j = 0; j + 1 < poolCount; j += 2)
        {
            vm_mock_service_client_session *inv =
                vm_mock_service_find_client_session(pool[j]->clientId);
            vm_mock_service_client_session *resp =
                vm_mock_service_find_client_session(pool[j + 1]->clientId);
            vm_mock_service_duel *duel;
            if (inv == NULL || resp == NULL)
                continue;
            duel = vm_mock_service_duel_begin(inv, resp);
            if (duel == NULL)
                continue;
            duel->arenaMode = true;
            duel->arenaSettled = false;
            duel->arenaStartTick = g_schedulerTick;
            pool[j]->matchedThisRound = 1;
            pool[j + 1]->matchedThisRound = 1;
            printf("[info][mock-service] arena_match round=%u a=%u b=%u serial=%u\n",
                   roundIndex, pool[j]->roleId, pool[j + 1]->roleId, duel->serial);
        }
        if ((poolCount % 2u) == 1u && poolCount > 0)
        {
            vm_mock_arena_participant *bye = pool[poolCount - 1];
            if (!bye->matchedThisRound)
            {
                vm_mock_service_client_session *byeSess =
                    vm_mock_service_find_client_session(bye->clientId);
                u32 byePts = g_vm_mock_arena_config.scoreBye;
                bye->matchedThisRound = 1;
                bye->byes += 1;
                vm_mock_arena_add_score(bye, byePts, "bye");
                if (byeSess != NULL)
                {
                    vm_mock_arena_note_result(byeSess, bye, "bye", byePts);
                    if (byeSess->arenaResultFdata[0] != 0)
                        (void)vm_mock_service_session_enqueue_system_message(
                            byeSess, byeSess->arenaResultFdata);
                    byeSess->arenaResultPending = false;
                }
            }
        }
    }
}

static void vm_mock_arena_tick(void)
{
    u32 dayYmd = 0;
    u32 minute = 0;
    u32 weekdayBit = 0;
    u32 unixTs = 0;
    u8 desired;
    u32 i;
    u32 timeoutSeconds;

    (void)vm_mock_arena_load_config(false);
    if (!g_vm_mock_arena_config.enabled)
        return;
    vm_mock_arena_wall_clock(&dayYmd, &minute, &weekdayBit, &unixTs);
    if (unixTs == g_vm_mock_arena_last_tick_sec)
        return;
    g_vm_mock_arena_last_tick_sec = unixTs;

    if ((g_vm_mock_arena_config.weekdayMask & weekdayBit) != 0 &&
        minute >= g_vm_mock_arena_config.entryStartMinute &&
        minute < g_vm_mock_arena_config.matchEndMinute + 120u)
    {
        u8 slot = ((weekdayBit & (1u << 6)) != 0) ? 1 : 0;
        vm_mock_arena_ensure_session(dayYmd, slot, unixTs);
    }

    desired = vm_mock_arena_desired_status(minute);
    if (g_vm_mock_arena_rt.sessionId != 0)
    {
        if (desired == VM_MOCK_ARENA_STATUS_LIVE &&
            g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_SETTLED)
            vm_mock_arena_set_status(VM_MOCK_ARENA_STATUS_LIVE);
        if (desired == VM_MOCK_ARENA_STATUS_SETTLED ||
            (unixTs >= g_vm_mock_arena_rt.matchEndTs &&
             g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_SETTLED &&
             g_vm_mock_arena_rt.matchEndTs != 0))
            vm_mock_arena_settle();
        if (g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_LIVE)
            vm_mock_arena_match_round();
    }

    timeoutSeconds = g_vm_mock_arena_config.battleTimeoutSeconds
                         ? g_vm_mock_arena_config.battleTimeoutSeconds
                         : 240u;
    for (i = 0; i < VM_MOCK_SERVICE_DUEL_MAX; ++i)
    {
        vm_mock_service_duel *duel = &g_vm_mock_service_duels[i];
        u32 ageSec;
        if (!duel->active || !duel->arenaMode || duel->finished)
            continue;
        if (duel->arenaStartTick == 0)
            continue;
        ageSec = (g_schedulerTick - duel->arenaStartTick) / 10u;
        if (ageSec >= timeoutSeconds)
            vm_mock_arena_force_duel_draw(duel);
    }
}

static vm_mock_arena_participant *vm_mock_arena_ensure_participant(
    vm_mock_service_client_session *session, vm_net_mock_role_state *role)
{
    vm_mock_arena_participant *p;
    u16 bracket;

    if (session == NULL || role == NULL || role->roleId == 0)
        return NULL;
    bracket = vm_mock_arena_bracket_for_level(role->level);
    if (bracket == 0)
        return NULL;
    p = vm_mock_arena_find_part(role->roleId);
    if (p == NULL)
    {
        if (g_vm_mock_arena_part_count >= VM_MOCK_ARENA_PARTICIPANT_MAX)
            return NULL;
        p = &g_vm_mock_arena_parts[g_vm_mock_arena_part_count++];
        memset(p, 0, sizeof(*p));
        p->roleId = role->roleId;
        snprintf(p->accountId, sizeof(p->accountId), "%s", session->accountId);
        snprintf(p->roleName, sizeof(p->roleName), "%s",
                 role->name[0] ? role->name : session->onlineRoleName);
    }
    p->clientId = session->clientId;
    p->job = role->job ? role->job : session->onlineJob;
    p->bracket = bracket;
    p->level = (u16)(role->level ? role->level : session->onlineLevel);
    return p;
}

static u32 vm_net_mock_build_arena_teleport_response(u8 *out, u32 outCap,
                                                     const char *scene, u16 x,
                                                     u16 y)
{
    vm_net_mock_scene_change_target target;
    u32 pos = 5;

    if (out == NULL || outCap < 5 || scene == NULL || scene[0] == 0)
        return 0;
    memset(&target, 0, sizeof(target));
    snprintf(target.scene, sizeof(target.scene), "%s", scene);
    target.x = x ? x : 120;
    target.y = y ? y : 120;
    target.mapType = 2;
    target.hasSceEntry = true;
    if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos, scene,
                                                          target.x, target.y))
        return 0;
    vm_mock_service_mark_active_session_scene_pending(&target, "arena-teleport");
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    return pos;
}

static u32 vm_net_mock_build_arena_enter_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_arena_participant *p;

    (void)vm_mock_arena_tick();
    if (role == NULL || session == NULL || out == NULL)
        return 0;
    if (!vm_mock_arena_entry_window_open() || g_vm_mock_arena_rt.sessionId == 0)
        return 0;
    if (role->level < 10)
        return 0;
    p = vm_mock_arena_ensure_participant(session, role);
    if (p == NULL)
        return 0;
    if (!vm_mock_arena_is_arena_scene(session->sceneVisibleScene))
    {
        snprintf(p->returnScene, sizeof(p->returnScene), "%s",
                 session->sceneVisibleScene[0] ? session->sceneVisibleScene
                                                : role->scene);
        p->returnX = session->sceneVisibleX ? session->sceneVisibleX : role->x;
        p->returnY = session->sceneVisibleY ? session->sceneVisibleY : role->y;
    }
    p->inArenaScene = 1;
    (void)vm_mock_arena_persist_score(p);
    vm_mock_arena_disband_team_for_session(session, "arena-enter");
    return vm_net_mock_build_arena_teleport_response(
        out, outCap, g_vm_mock_arena_config.scene, g_vm_mock_arena_config.spawnX,
        g_vm_mock_arena_config.spawnY);
}

static u32 vm_net_mock_build_arena_leave_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_arena_participant *p;
    char scene[64];
    u16 x = 120;
    u16 y = 120;

    if (role == NULL || session == NULL || out == NULL)
        return 0;
    p = vm_mock_arena_find_part(role->roleId);
    if (p != NULL && p->returnScene[0] != 0)
    {
        snprintf(scene, sizeof(scene), "%s", p->returnScene);
        x = p->returnX ? p->returnX : 120;
        y = p->returnY ? p->returnY : 120;
        p->inArenaScene = 0;
    }
    else
    {
        snprintf(scene, sizeof(scene), "c04\xc1\xd9\xb0\xb2\xb8\xae_01");
        if (p)
            p->inArenaScene = 0;
    }
    return vm_net_mock_build_arena_teleport_response(out, outCap, scene, x, y);
}

static bool vm_net_mock_append_arena_hud_object(u8 *out, u32 outCap, u32 *pos)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_arena_participant *p;
    u32 objectStart = 0;
    u32 unixTs = 0;
    u32 roundRemain = 0;
    u32 eventRemain = 0;
    u32 roundSec;

    if (out == NULL || pos == NULL || role == NULL)
        return false;
    if (g_vm_mock_arena_rt.sessionId == 0 ||
        (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_ENTRY &&
         g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE))
        return false;
    p = vm_mock_arena_find_part(role->roleId);
    if (p == NULL || !p->inArenaScene)
        return false;
    vm_mock_arena_wall_clock(NULL, NULL, NULL, &unixTs);
    roundSec = g_vm_mock_arena_config.roundSeconds
                   ? g_vm_mock_arena_config.roundSeconds
                   : 300u;
    if (g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_LIVE &&
        g_vm_mock_arena_rt.roundStartedTs != 0 &&
        unixTs >= g_vm_mock_arena_rt.roundStartedTs)
    {
        u32 spent = unixTs - g_vm_mock_arena_rt.roundStartedTs;
        roundRemain = spent < roundSec ? (roundSec - spent) : 0;
    }
    if (unixTs < g_vm_mock_arena_rt.matchEndTs)
        eventRemain = g_vm_mock_arena_rt.matchEndTs - unixTs;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_HUD, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "status",
                                    g_vm_mock_arena_rt.status) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "round",
                                    g_vm_mock_arena_rt.roundIndex) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "roundRemain", roundRemain) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "eventRemain", eventRemain) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "score", p->score) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "job", p->job) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "bracket", p->bracket))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_arena_status_response(const u8 *request, u32 requestLen,
                                                    u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 dayYmd = 0;
    u32 minute = 0;
    u32 weekdayBit = 0;
    u8 canEnter = 0;
    u8 status = 0;

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ARENA_WT_KIND ||
        object.subtype != VM_MOCK_ARENA_WT_STATUS)
        return 0;
    (void)vm_mock_arena_tick();
    vm_mock_arena_wall_clock(&dayYmd, &minute, &weekdayBit, NULL);
    canEnter = vm_mock_arena_entry_window_open() ? 1 : 0;
    status = g_vm_mock_arena_rt.sessionId ? g_vm_mock_arena_rt.status
                                          : vm_mock_arena_desired_status(minute);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_STATUS, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "enabled",
                                    g_vm_mock_arena_config.enabled) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "status", status) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "canEnter", canEnter) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "dayYmd", dayYmd) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "minute", minute) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "sessionId",
                                    g_vm_mock_arena_rt.sessionId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "round",
                                    g_vm_mock_arena_rt.roundIndex) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "scene",
                                       g_vm_mock_arena_config.scene))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    return pos;
}

static u32 vm_net_mock_build_arena_rank_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 job = 0;
    u32 bracket = 0;
    u32 pageIndex = 0;
    u8 blob[2048];
    u32 blobLen = 0;
    u32 i;
    u32 count = 0;
    u32 myRank = 0;
    u32 myScore = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_arena_participant sorted[VM_MOCK_ARENA_PARTICIPANT_MAX];
    u32 sortedCount = 0;
    u32 start;
    u32 pageSize = 10;

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ARENA_WT_KIND ||
        object.subtype != VM_MOCK_ARENA_WT_RANK)
        return 0;
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "job", &job);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "bracket", &bracket);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "pageIndex", &pageIndex);
    if (job == 0 && role)
        job = role->job;
    if (bracket == 0 && role)
        bracket = vm_mock_arena_bracket_for_level(role->level);
    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        /* 0 分不进排行榜（含未打过的进场玩家）。 */
        if (g_vm_mock_arena_parts[i].job == (u8)job &&
            g_vm_mock_arena_parts[i].bracket == (u16)bracket &&
            g_vm_mock_arena_parts[i].score > 0)
            sorted[sortedCount++] = g_vm_mock_arena_parts[i];
    }
    if (sortedCount > 1)
        qsort(sorted, sortedCount, sizeof(sorted[0]), vm_mock_arena_cmp_score_desc);
    if (role != NULL)
    {
        vm_mock_arena_participant *self = vm_mock_arena_find_part(role->roleId);
        if (self != NULL)
            myScore = self->score;
        for (i = 0; i < sortedCount; ++i)
        {
            if (sorted[i].roleId == role->roleId)
            {
                myRank = i + 1;
                myScore = sorted[i].score;
                break;
            }
        }
    }
    start = pageIndex * pageSize;
    for (i = start; i < sortedCount && count < pageSize; ++i)
    {
        if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, sorted[i].roleId) ||
            !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen,
                                       sorted[i].roleName) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, sorted[i].score) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, i + 1) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, sorted[i].claimed))
            break;
        count += 1;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_RANK, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "job", job) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "bracket", bracket) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "myRank", myRank) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "myScore", myScore) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "count", count) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "settled",
                                    g_vm_mock_arena_rt.status ==
                                            VM_MOCK_ARENA_STATUS_SETTLED
                                        ? 1
                                        : 0) ||
        !vm_net_mock_put_object_entry(out, outCap, &pos, "rows", blob,
                                      (u16)blobLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    return pos;
}

static const vm_mock_arena_reward *vm_mock_arena_find_reward(u8 job, u16 bracket,
                                                             u8 rankPos)
{
    u32 i;
    for (i = 0; i < g_vm_mock_arena_reward_count; ++i)
    {
        if (g_vm_mock_arena_rewards[i].job == job &&
            g_vm_mock_arena_rewards[i].bracket == bracket &&
            g_vm_mock_arena_rewards[i].rankPos == rankPos)
            return &g_vm_mock_arena_rewards[i];
    }
    return NULL;
}

static u32 vm_net_mock_build_arena_claim_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_arena_participant *p;
    const vm_mock_arena_reward *reward;
    u32 result = 0;
    u16 seq = 0;

    if (out == NULL || outCap < 5 || request == NULL || role == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ARENA_WT_KIND ||
        object.subtype != VM_MOCK_ARENA_WT_CLAIM)
        return 0;
    p = vm_mock_arena_find_part(role->roleId);
    if (p == NULL || g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_SETTLED)
        result = 2;
    else if (p->claimed)
        result = 3;
    else if (p->rankPos == 0 || p->rankPos > 10)
        result = 4;
    else
    {
        reward = vm_mock_arena_find_reward(p->job, p->bracket, (u8)p->rankPos);
        if (reward == NULL)
            result = 5;
        else
        {
            if (reward->rewardExp)
                (void)vm_net_mock_role_add_exp(role, reward->rewardExp);
            if (reward->rewardMoney)
            {
                role->money = vm_net_mock_add_capped_u32(role->money,
                                                         reward->rewardMoney);
                vm_net_mock_role_mark_inventory_dirty("arena-claim-money");
            }
            if (reward->rewardWcoin)
                (void)vm_net_mock_role_add_wcoin(role, reward->rewardWcoin);
            if (reward->itemId && reward->itemCount)
                (void)vm_net_mock_role_add_backpack_item_to_role(
                    role, reward->itemId, reward->itemCount, 0, &seq,
                    "arena-claim-item");
            if (reward->equipId)
                (void)vm_net_mock_role_add_backpack_item_to_role(
                    role, reward->equipId, 1, 0, &seq, "arena-claim-equip");
            p->claimed = 1;
            (void)vm_mock_arena_persist_score(p);
            result = 1;
        }
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_CLAIM, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "rank",
                                    p ? p->rankPos : 0) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "claimed",
                                    p && p->claimed ? 1 : 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    return pos;
}

static u32 vm_net_mock_build_arena_protocol_response(const u8 *request, u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ARENA_WT_KIND)
        return 0;
    if (object.subtype == VM_MOCK_ARENA_WT_STATUS)
        return vm_net_mock_build_arena_status_response(request, requestLen, out,
                                                       outCap);
    if (object.subtype == VM_MOCK_ARENA_WT_RANK)
        return vm_net_mock_build_arena_rank_response(request, requestLen, out,
                                                     outCap);
    if (object.subtype == VM_MOCK_ARENA_WT_CLAIM)
        return vm_net_mock_build_arena_claim_response(request, requestLen, out,
                                                      outCap);
    if (object.subtype == VM_MOCK_ARENA_WT_LEAVE)
        return vm_net_mock_build_arena_leave_response(out, outCap);
    if (object.subtype >= 10 && object.subtype <= 16)
        return vm_net_mock_build_wager_protocol_response(
            request, requestLen, out, outCap, object.subtype);
    return 0;
}

#include "mock_server_arena_wager.inc.c"

static bool vm_net_mock_arena_admin_save_config(
    u8 enabled, const char *scene, u16 spawnX, u16 spawnY, u32 weekdayMask,
    u16 entryStart, u16 entryEnd, u16 matchStart, u16 matchEnd, u32 roundSec,
    u32 battleTimeout, u32 scoreWin, u32 scoreBye, u32 scoreDraw,
    const char *npcKeyword, u8 wagerEnabled, u8 wagerFee, u8 wagerCurrency)
{
    char sceneHex[192];
    char kwHex[96];
    char query[2048];
    int written;

    if (!vm_mock_arena_ensure_tables() || scene == NULL || scene[0] == 0)
        return false;
    if (vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
        return false;
    if (npcKeyword == NULL)
        npcKeyword = "";
    if (vm_mysql_hex_encode(npcKeyword, strlen(npcKeyword), kwHex, sizeof(kwHex)) == 0)
        return false;
    written = snprintf(
        query, sizeof(query),
        "INSERT INTO server_arena_config (id,enabled,scene,spawn_x,spawn_y,"
        "weekday_mask,entry_start_minute,entry_end_minute,match_start_minute,"
        "match_end_minute,round_seconds,battle_timeout_seconds,score_win,"
        "score_bye,score_draw,npc_name_keyword,wager_enabled,wager_fee_percent,"
        "wager_currency) VALUES (1,%u,UNHEX('%s'),%u,%u,%u,%u,%u,%u,%u,%u,%u,"
        "%u,%u,%u,UNHEX('%s'),%u,%u,%u) ON DUPLICATE KEY UPDATE "
        "enabled=VALUES(enabled),scene=VALUES(scene),spawn_x=VALUES(spawn_x),"
        "spawn_y=VALUES(spawn_y),weekday_mask=VALUES(weekday_mask),"
        "entry_start_minute=VALUES(entry_start_minute),"
        "entry_end_minute=VALUES(entry_end_minute),"
        "match_start_minute=VALUES(match_start_minute),"
        "match_end_minute=VALUES(match_end_minute),"
        "round_seconds=VALUES(round_seconds),"
        "battle_timeout_seconds=VALUES(battle_timeout_seconds),"
        "score_win=VALUES(score_win),score_bye=VALUES(score_bye),"
        "score_draw=VALUES(score_draw),npc_name_keyword=VALUES(npc_name_keyword),"
        "wager_enabled=VALUES(wager_enabled),"
        "wager_fee_percent=VALUES(wager_fee_percent),"
        "wager_currency=VALUES(wager_currency)",
        (u32)enabled, sceneHex, (u32)spawnX, (u32)spawnY, weekdayMask,
        (u32)entryStart, (u32)entryEnd, (u32)matchStart, (u32)matchEnd,
        roundSec, battleTimeout, scoreWin, scoreBye, scoreDraw, kwHex,
        (u32)wagerEnabled, (u32)wagerFee, (u32)wagerCurrency);
    if (written < 0 || (size_t)written >= sizeof(query))
    {
        printf("[error][mock-service] arena_admin_save_config query truncated "
               "written=%d cap=%u\n",
               written, (u32)sizeof(query));
        return false;
    }
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-service] arena_admin_save_config mysql failed "
               "error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_mock_arena_config_loaded = false;
    return vm_mock_arena_load_config(true);
}

static bool vm_net_mock_arena_admin_save_reward(u8 job, u16 bracket, u8 rankPos,
                                                u32 exp, u32 money, u32 wcoin,
                                                u32 itemId, u32 itemCount,
                                                u32 equipId)
{
    char query[512];
    if (!vm_mock_arena_ensure_tables() || job == 0 || bracket == 0 ||
        rankPos == 0 || rankPos > 10)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_arena_rewards (job,bracket,rank_pos,reward_exp,"
             "reward_money,reward_wcoin,item_id,item_count,equip_id) VALUES "
             "(%u,%u,%u,%u,%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "reward_exp=VALUES(reward_exp),reward_money=VALUES(reward_money),"
             "reward_wcoin=VALUES(reward_wcoin),item_id=VALUES(item_id),"
             "item_count=VALUES(item_count),equip_id=VALUES(equip_id)",
             (u32)job, (u32)bracket, (u32)rankPos, exp, money, wcoin, itemId,
             itemCount, equipId);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_arena_config_loaded = false;
    return vm_mock_arena_load_config(true);
}

static const vm_mock_arena_config *vm_net_mock_arena_config_get(void)
{
    (void)vm_mock_arena_load_config(false);
    return &g_vm_mock_arena_config;
}

static u32 vm_net_mock_arena_reward_admin_count(void)
{
    (void)vm_mock_arena_load_config(false);
    return g_vm_mock_arena_reward_count;
}

static const vm_mock_arena_reward *vm_net_mock_arena_reward_admin_at(u32 index)
{
    (void)vm_mock_arena_load_config(false);
    if (index >= g_vm_mock_arena_reward_count)
        return NULL;
    return &g_vm_mock_arena_rewards[index];
}

static u32 vm_net_mock_arena_runtime_participant_count(void)
{
    return g_vm_mock_arena_part_count;
}

static u32 vm_net_mock_arena_runtime_session_id(void)
{
    return g_vm_mock_arena_rt.sessionId;
}

static u8 vm_net_mock_arena_runtime_status(void)
{
    return g_vm_mock_arena_rt.status;
}
