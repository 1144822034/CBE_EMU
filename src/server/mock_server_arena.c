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
    /* Cap concurrent LIVE arena fights (protocol-lock / battle fan-out).
     * Excess seats defer until a slot frees — not bye. Global DUEL_MAX=64. */
    VM_MOCK_ARENA_MAX_CONCURRENT_DUELS = 24,
    /* Per 1Hz tick: max new arena duel_begin calls (spread lock/GameThread load). */
    VM_MOCK_ARENA_MAX_STARTS_PER_TICK = 6,
    VM_MOCK_ARENA_WT_KIND = 41,
    VM_MOCK_ARENA_WT_STATUS = 1,
    VM_MOCK_ARENA_WT_RANK = 2,
    VM_MOCK_ARENA_WT_CLAIM = 3,
    VM_MOCK_ARENA_WT_LEAVE = 4,
    VM_MOCK_ARENA_WT_HUD = 5,
    /* Weekend 70+ free-for-all (三职业同场); rewards use job=0. */
    VM_MOCK_ARENA_BRACKET_FFA70 = 77
    /* wager subtypes 10-16 live in mock_server_arena_wager.inc.c */
};

/* GBK: c04比武擂台 / 比武 */
static const char VM_MOCK_ARENA_DEFAULT_SCENE[] =
    "c04\xb1\xc8\xce\xe4\xc0\xde\xcc\xa8";
static const char VM_MOCK_ARENA_DEFAULT_LEAVE_SCENE[] =
    "c04\xc1\xd9\xb0\xb2\xb8\xae_01"; /* c04临安府_01 */
static const char VM_MOCK_ARENA_DEFAULT_NPC_KEYWORD[] = "\xb1\xc8\xce\xe4";

typedef struct
{
    u8 enabled;
    char scene[64];
    u16 spawnX;
    u16 spawnY;
    u32 weekdayMask; /* derived from weekdayBrackets[i]!=0 */
    /* Sun..Sat: 0=rest, 10/50/70, 77=70大乱斗(三职业同场). One key per day. */
    u16 weekdayBrackets[7];
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
    /* Timed-arena duel rules (admin). Defaults preserve historical contract. */
    u8 damageScaleEnabled; /* round damage/hit 2^n */
    u8 allowHeal;          /* 1: each seat, one heal/buff/silence/dispel; items stay banned */
    u8 allowDodge;         /* physical hit vs dodge roll */
    u8 allowCombatPill;    /* 大力丸/神力丸 ATK% in arena duel */
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
    u32 lastOpponentRoleId; /* 上场对局对手；匹配时优先避开（池≥3） */
    /* LIVE 连杀（胜且未死）；平/轮空不计，负清零。仅内存，不落库。 */
    u32 killStreak;
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
static u32 g_vm_mock_arena_rng = 0;
/* Last status we world-announced (avoids repeat spam on every tick). */
static u8 g_vm_mock_arena_announced_status = 0xffu;
/* LIVE 场次内：连胜档位 1..11 各只由全场首个达成者播报（bit0=敌将讨伐 … bit10=天下无敌）。 */
static u32 g_vm_mock_arena_kill_streak_announced_mask = 0;
/* Unmatched waiters left after last match pass (slot-full / retryable fail). */
static u32 g_vm_mock_arena_deferred_unmatched = 0;
/* Past matchEndMinute: same job×bracket×score>0 ties stay LIVE until resolved. */
static bool g_vm_mock_arena_playoff = false;
static bool g_vm_mock_arena_playoff_announced = false;
/* GBK tip when NPC enter is refused (wrong day bracket / closed). */
static char g_vm_mock_arena_enter_refuse_tip[96];

/* Dedicated matchmaking RNG. Server-only builds never srand() libc rand();
 * relying on rand() alone yields a fixed sequence and sticky pairings. */
static void vm_mock_arena_rng_mix(u32 entropy)
{
    g_vm_mock_arena_rng ^= entropy + 0x9e3779b9u + (g_vm_mock_arena_rng << 6) +
                           (g_vm_mock_arena_rng >> 2);
    if (g_vm_mock_arena_rng == 0)
        g_vm_mock_arena_rng = 0xA5A5A5A5u ^ entropy ^ 1u;
}

static u32 vm_mock_arena_rng_u32(void)
{
    if (g_vm_mock_arena_rng == 0)
        vm_mock_arena_rng_mix((u32)time(NULL) ^ 0xC0FFEEu);
    g_vm_mock_arena_rng = g_vm_mock_arena_rng * 1664525u + 1013904223u;
    return g_vm_mock_arena_rng;
}

static u32 vm_mock_arena_rng_below(u32 bound)
{
    if (bound <= 1u)
        return 0;
    return vm_mock_arena_rng_u32() % bound;
}

/* Defined in mock_server_arena_wager.inc.c (included below). */
static bool vm_mock_wager_ensure_member_table(void);
static void vm_mock_wager_tick_expire(void);
static u32 vm_net_mock_build_wager_protocol_response(const u8 *request,
                                                     u32 requestLen, u8 *out,
                                                     u32 outCap, u8 subtype);
static bool vm_net_mock_append_wager_event_object(u8 *out, u32 outCap, u32 *pos);
static void vm_mock_arena_on_wager_duel_finished(vm_mock_service_duel *duel);
static vm_mock_arena_participant *vm_mock_arena_ensure_participant(
    vm_mock_service_client_session *session, vm_net_mock_role_state *role);

/* 等级段：10–49 → 10；50–69 → 50；70+ → 70。匹配仍要求同职业。 */
static u16 vm_mock_arena_bracket_for_level(u32 level)
{
    if (level >= 70)
        return 70;
    if (level >= 50)
        return 50;
    if (level >= 10)
        return 10;
    return 0;
}

static u16 vm_mock_arena_normalize_bracket_key(u32 value)
{
    if (value == 10u || value == 50u || value == 70u ||
        value == (u32)VM_MOCK_ARENA_BRACKET_FFA70)
        return (u16)value;
    return 0;
}

static bool vm_mock_arena_is_ffa_bracket(u16 bracket)
{
    return bracket == (u16)VM_MOCK_ARENA_BRACKET_FFA70;
}

/* Kill-streak announce / 文字称号: normal 70 and weekend FFA. */
static bool vm_mock_arena_is_kill_streak_bracket(u16 bracket)
{
    return bracket == 70 || vm_mock_arena_is_ffa_bracket(bracket);
}

/* Player level qualifies for today's open schedule key. */
static bool vm_mock_arena_level_matches_active(u32 level, u16 active)
{
    u16 levelBracket = vm_mock_arena_bracket_for_level(level);

    if (active == 0 || levelBracket == 0)
        return false;
    if (vm_mock_arena_is_ffa_bracket(active))
        return levelBracket == 70;
    return levelBracket == active;
}

static u16 vm_mock_arena_active_bracket_today(void);

static void vm_mock_arena_sync_weekday_mask(vm_mock_arena_config *cfg)
{
    u32 i;
    u32 mask = 0;

    if (cfg == NULL)
        return;
    for (i = 0; i < 7u; ++i)
    {
        cfg->weekdayBrackets[i] =
            vm_mock_arena_normalize_bracket_key(cfg->weekdayBrackets[i]);
        if (cfg->weekdayBrackets[i] != 0)
            mask |= (1u << i);
    }
    cfg->weekdayMask = mask;
}

static void vm_mock_arena_parse_weekday_brackets(vm_mock_arena_config *cfg,
                                                 const char *text, size_t len)
{
    char buf[64];
    char *cursor;
    char *tok;
    u32 idx = 0;

    if (cfg == NULL)
        return;
    memset(cfg->weekdayBrackets, 0, sizeof(cfg->weekdayBrackets));
    if (text == NULL || len == 0)
    {
        vm_mock_arena_sync_weekday_mask(cfg);
        return;
    }
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1u;
    memcpy(buf, text, len);
    buf[len] = 0;
    cursor = buf;
    while (idx < 7u && cursor != NULL && *cursor != 0)
    {
        unsigned long parsed = 0;
        char *end = NULL;
        tok = cursor;
        while (*cursor != 0 && *cursor != ',')
            ++cursor;
        if (*cursor == ',')
        {
            *cursor = 0;
            ++cursor;
        }
        else
            cursor = NULL;
        parsed = strtoul(tok, &end, 10);
        if (end != tok)
            cfg->weekdayBrackets[idx] = vm_mock_arena_normalize_bracket_key(
                (u32)parsed);
        ++idx;
    }
    vm_mock_arena_sync_weekday_mask(cfg);
}

static void vm_mock_arena_format_weekday_brackets(const vm_mock_arena_config *cfg,
                                                  char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    if (cfg == NULL)
    {
        out[0] = 0;
        return;
    }
    snprintf(out, outCap, "%u,%u,%u,%u,%u,%u,%u",
             (u32)cfg->weekdayBrackets[0], (u32)cfg->weekdayBrackets[1],
             (u32)cfg->weekdayBrackets[2], (u32)cfg->weekdayBrackets[3],
             (u32)cfg->weekdayBrackets[4], (u32)cfg->weekdayBrackets[5],
             (u32)cfg->weekdayBrackets[6]);
}

static bool vm_mock_arena_load_config(bool force);

/* Today's open bracket (0=rest). Uses local tm_wday (0=Sun..6=Sat). */
static u16 vm_mock_arena_active_bracket_today(void)
{
    time_t now = time(NULL);
    struct tm *tmNow = localtime(&now);

    (void)vm_mock_arena_load_config(false);
    if (tmNow == NULL)
        return 0;
    return g_vm_mock_arena_config.weekdayBrackets[(u32)tmNow->tm_wday % 7u];
}

/* Store schedule key on participant (77 on FFA days, else 10/50/70). */
static u16 vm_mock_arena_enrollment_bracket(u32 level)
{
    u16 active = vm_mock_arena_active_bracket_today();
    u16 levelBracket = vm_mock_arena_bracket_for_level(level);

    if (levelBracket == 0)
        return 0;
    if (vm_mock_arena_is_ffa_bracket(active) && levelBracket == 70)
        return active;
    return levelBracket;
}

static const char *vm_mock_arena_enter_refuse_tip(void)
{
    if (g_vm_mock_arena_enter_refuse_tip[0] != 0)
        return g_vm_mock_arena_enter_refuse_tip;
    /* 本次比武未开放或已关闭进场。 */
    return "\xb1\xbe\xb4\xce\xb1\xc8\xce\xe4\xce\xb4\xbf\xaa\xb7\xc5\xbb\xf2"
           "\xd2\xd1\xb9\xd8\xb1\xd5\xbd\xf8\xb3\xa1\xa1\xa3";
}

static void vm_mock_arena_set_enter_refuse_tip_closed(void)
{
    /* 本次比武未开放或已关闭进场。 */
    snprintf(g_vm_mock_arena_enter_refuse_tip,
             sizeof(g_vm_mock_arena_enter_refuse_tip), "%s",
             "\xb1\xbe\xb4\xce\xb1\xc8\xce\xe4\xce\xb4\xbf\xaa\xb7\xc5\xbb\xf2"
             "\xd2\xd1\xb9\xd8\xb1\xd5\xbd\xf8\xb3\xa1\xa1\xa3");
}

static void vm_mock_arena_set_enter_refuse_tip_bracket(u16 activeBracket)
{
    if (activeBracket == 0)
    {
        /* 今日比武休息。 */
        snprintf(g_vm_mock_arena_enter_refuse_tip,
                 sizeof(g_vm_mock_arena_enter_refuse_tip), "%s",
                 "\xbd\xf1\xc8\xd5\xb1\xc8\xce\xe4\xd0\xdd\xcf\xa2\xa1\xa3");
        return;
    }
    if (vm_mock_arena_is_ffa_bracket(activeBracket) || activeBracket >= 70)
    {
        /* 今日开放70级段，你的等级不符。 (FFA uses same tip) */
        snprintf(g_vm_mock_arena_enter_refuse_tip,
                 sizeof(g_vm_mock_arena_enter_refuse_tip), "%s",
                 "\xbd\xf1\xc8\xd5\xbf\xaa\xb7\xc5"
                 "70"
                 "\xbc\xb6\xb6\xce\xa3\xac\xc4\xe3\xb5\xc4\xb5\xc8\xbc\xb6\xb2"
                 "\xbb\xb7\xfb\xa1\xa3");
        return;
    }
    if (activeBracket >= 50)
    {
        /* 今日开放50-69级段，你的等级不符。 */
        snprintf(g_vm_mock_arena_enter_refuse_tip,
                 sizeof(g_vm_mock_arena_enter_refuse_tip), "%s",
                 "\xbd\xf1\xc8\xd5\xbf\xaa\xb7\xc5"
                 "50-69"
                 "\xbc\xb6\xb6\xce\xa3\xac\xc4\xe3\xb5\xc4\xb5\xc8\xbc\xb6\xb2"
                 "\xbb\xb7\xfb\xa1\xa3");
        return;
    }
    /* 今日开放10-49级段，你的等级不符。 */
    snprintf(g_vm_mock_arena_enter_refuse_tip,
             sizeof(g_vm_mock_arena_enter_refuse_tip), "%s",
             "\xbd\xf1\xc8\xd5\xbf\xaa\xb7\xc5"
             "10-49"
             "\xbc\xb6\xb6\xce\xa3\xac\xc4\xe3\xb5\xc4\xb5\xc8\xbc\xb6\xb2"
             "\xbb\xb7\xfb\xa1\xa3");
}

/*
 * Job venues (same spawn): 天机→base, 幻剑→base+"50", 鬼道→base+"70".
 * FFA (77): everyone uses the 70 map (base+"70"), all three jobs together.
 */
static void vm_mock_arena_scene_for_job(u8 job, char *out, size_t outCap)
{
    const char *base;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    (void)vm_mock_arena_load_config(false);
    base = g_vm_mock_arena_config.scene;
    if (base == NULL || base[0] == 0)
        base = VM_MOCK_ARENA_DEFAULT_SCENE;
    if (job == 3)
        snprintf(out, outCap, "%s70", base);
    else if (job == 2)
        snprintf(out, outCap, "%s50", base);
    else
        snprintf(out, outCap, "%s", base);
}

static void vm_mock_arena_scene_for_entry(u8 job, u16 bracket, char *out,
                                          size_t outCap)
{
    if (vm_mock_arena_is_ffa_bracket(bracket) ||
        vm_mock_arena_is_ffa_bracket(vm_mock_arena_active_bracket_today()))
    {
        /* 70-level map only (same as 鬼道 venue suffix). */
        vm_mock_arena_scene_for_job(3, out, outCap);
        return;
    }
    vm_mock_arena_scene_for_job(job, out, outCap);
}

static bool vm_mock_arena_on_job_venue(const char *sceneVisible,
                                       const char *onlineScene, u8 job)
{
    char want[80];

    vm_mock_arena_scene_for_job(job, want, sizeof(want));
    if (want[0] == 0)
        return false;
    if (sceneVisible != NULL && sceneVisible[0] != 0 &&
        vm_net_mock_scene_names_equal_loose(sceneVisible, want))
        return true;
    if (onlineScene != NULL && onlineScene[0] != 0 &&
        vm_net_mock_scene_names_equal_loose(onlineScene, want))
        return true;
    return false;
}

static bool vm_mock_arena_on_expected_venue(const char *sceneVisible,
                                            const char *onlineScene, u8 job,
                                            u16 bracket)
{
    char want[80];

    vm_mock_arena_scene_for_entry(job, bracket, want, sizeof(want));
    if (want[0] == 0)
        return false;
    if (sceneVisible != NULL && sceneVisible[0] != 0 &&
        vm_net_mock_scene_names_equal_loose(sceneVisible, want))
        return true;
    if (onlineScene != NULL && onlineScene[0] != 0 &&
        vm_net_mock_scene_names_equal_loose(onlineScene, want))
        return true;
    return false;
}

static bool vm_mock_arena_same_bucket(u8 jobA, u16 bracketA, u16 levelA,
                                       u8 jobB, u16 bracketB, u16 levelB)
{
    (void)levelA;
    (void)levelB;
    if (bracketA == 0 || bracketB == 0 || bracketA != bracketB)
        return false;
    /* FFA: ignore job — all three professions share one pool. */
    if (vm_mock_arena_is_ffa_bracket(bracketA))
        return true;
    if (jobA == 0 || jobB == 0 || jobA != jobB)
        return false;
    return true;
}

static bool vm_mock_arena_same_score_group(const vm_mock_arena_participant *a,
                                           const vm_mock_arena_participant *b)
{
    if (a == NULL || b == NULL)
        return false;
    if (a->bracket == 0 || a->bracket != b->bracket || a->score != b->score)
        return false;
    if (vm_mock_arena_is_ffa_bracket(a->bracket))
        return true;
    return a->job != 0 && a->job == b->job;
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
    /* Mon70 Tue50 Wed10 Thu70 Fri50 SatFFA SunFFA — weekend 70 大乱斗. */
    cfg->weekdayBrackets[0] = (u16)VM_MOCK_ARENA_BRACKET_FFA70;
    cfg->weekdayBrackets[1] = 70;
    cfg->weekdayBrackets[2] = 50;
    cfg->weekdayBrackets[3] = 10;
    cfg->weekdayBrackets[4] = 70;
    cfg->weekdayBrackets[5] = 50;
    cfg->weekdayBrackets[6] = (u16)VM_MOCK_ARENA_BRACKET_FFA70;
    vm_mock_arena_sync_weekday_mask(cfg);
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
    cfg->damageScaleEnabled = 1;
    cfg->allowHeal = 0;
    cfg->allowDodge = 1;
    cfg->allowCombatPill = 0;
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
            "weekday_mask INT UNSIGNED NOT NULL DEFAULT 127,"
            "weekday_brackets VARCHAR(32) NOT NULL DEFAULT '77,70,50,10,70,50,77',"
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
            "damage_scale_enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "allow_heal TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "allow_dodge TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "allow_combat_pill TINYINT UNSIGNED NOT NULL DEFAULT 0,"
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
    /* Existing installs: add per-day bracket schedule (ignore if already present). */
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_config ADD COLUMN weekday_brackets "
        "VARCHAR(32) NOT NULL DEFAULT '77,70,50,10,70,50,77' AFTER weekday_mask");
    /* Timed-arena duel rule toggles (ignore if already present). */
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_config ADD COLUMN damage_scale_enabled "
        "TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER wager_currency");
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_config ADD COLUMN allow_heal "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER damage_scale_enabled");
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_config ADD COLUMN allow_dodge "
        "TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER allow_heal");
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_config ADD COLUMN allow_combat_pill "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER allow_dodge");
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
    if (values[5] && lengths[5] > 0)
        vm_mock_arena_parse_weekday_brackets(cfg, values[5], lengths[5]);
    else
        vm_mock_arena_sync_weekday_mask(cfg);
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        cfg->entryStartMinute = (u16)tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        cfg->entryEndMinute = (u16)tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        cfg->matchStartMinute = (u16)tmp;
    if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
        cfg->matchEndMinute = (u16)tmp;
    if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
        cfg->roundSeconds = tmp ? tmp : 300;
    if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
        cfg->battleTimeoutSeconds = tmp ? tmp : 240;
    if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
        cfg->scoreWin = tmp;
    if (values[13] && vm_mock_mysql_parse_u32(values[13], lengths[13], &tmp))
        cfg->scoreBye = tmp;
    if (values[14] && vm_mock_mysql_parse_u32(values[14], lengths[14], &tmp))
        cfg->scoreDraw = tmp;
    if (values[15] && lengths[15] > 0 && lengths[15] < sizeof(cfg->npcNameKeyword))
    {
        memcpy(cfg->npcNameKeyword, values[15], lengths[15]);
        cfg->npcNameKeyword[lengths[15]] = 0;
    }
    if (values[16] && lengths[16])
        cfg->wagerEnabled = (u8)(values[16][0] == '0' ? 0 : 1);
    if (values[17] && vm_mock_mysql_parse_u32(values[17], lengths[17], &tmp))
        cfg->wagerFeePercent = (u8)tmp;
    if (values[18] && vm_mock_mysql_parse_u32(values[18], lengths[18], &tmp))
        cfg->wagerCurrency = (u8)tmp;
    if (values[19] && lengths[19])
        cfg->damageScaleEnabled = (u8)(values[19][0] == '0' ? 0 : 1);
    if (values[20] && lengths[20])
        cfg->allowHeal = (u8)(values[20][0] == '0' ? 0 : 1);
    if (values[21] && lengths[21])
        cfg->allowDodge = (u8)(values[21][0] == '0' ? 0 : 1);
    if (values[22] && lengths[22])
        cfg->allowCombatPill = (u8)(values[22][0] == '0' ? 0 : 1);
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
            "SELECT enabled,scene,spawn_x,spawn_y,weekday_mask,weekday_brackets,"
            "entry_start_minute,entry_end_minute,match_start_minute,match_end_minute,"
            "round_seconds,battle_timeout_seconds,score_win,score_bye,score_draw,"
            "npc_name_keyword,wager_enabled,wager_fee_percent,wager_currency,"
            "damage_scale_enabled,allow_heal,allow_dodge,allow_combat_pill "
            "FROM server_arena_config WHERE id=1 LIMIT 1",
            vm_mock_arena_config_row, &g_vm_mock_arena_config))
    {
        /* loaded or empty */
    }
    if (g_vm_mock_arena_config.scene[0] == 0)
    {
        char bracketsText[48];
        vm_mock_arena_config_set_defaults(&g_vm_mock_arena_config);
        vm_mock_arena_format_weekday_brackets(&g_vm_mock_arena_config, bracketsText,
                                              sizeof(bracketsText));
        if (vm_mysql_hex_encode(g_vm_mock_arena_config.scene,
                                strlen(g_vm_mock_arena_config.scene), sceneHex,
                                sizeof(sceneHex)) != 0 &&
            vm_mysql_hex_encode(g_vm_mock_arena_config.npcNameKeyword,
                                strlen(g_vm_mock_arena_config.npcNameKeyword),
                                kwHex, sizeof(kwHex)) != 0)
        {
            snprintf(query, sizeof(query),
                     "INSERT INTO server_arena_config ("
                     "id,enabled,scene,spawn_x,spawn_y,weekday_mask,weekday_brackets,"
                     "entry_start_minute,entry_end_minute,match_start_minute,"
                     "match_end_minute,round_seconds,battle_timeout_seconds,"
                     "score_win,score_bye,score_draw,npc_name_keyword,"
                     "wager_enabled,wager_fee_percent,wager_currency,"
                     "damage_scale_enabled,allow_heal,allow_dodge,allow_combat_pill) "
                     "VALUES ("
                     "1,1,UNHEX('%s'),%u,%u,%u,'%s',%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                     "UNHEX('%s'),0,10,0,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE id=id",
                     sceneHex, g_vm_mock_arena_config.spawnX,
                     g_vm_mock_arena_config.spawnY,
                     g_vm_mock_arena_config.weekdayMask, bracketsText,
                     g_vm_mock_arena_config.entryStartMinute,
                     g_vm_mock_arena_config.entryEndMinute,
                     g_vm_mock_arena_config.matchStartMinute,
                     g_vm_mock_arena_config.matchEndMinute,
                     g_vm_mock_arena_config.roundSeconds,
                     g_vm_mock_arena_config.battleTimeoutSeconds,
                     g_vm_mock_arena_config.scoreWin,
                     g_vm_mock_arena_config.scoreBye,
                     g_vm_mock_arena_config.scoreDraw, kwHex,
                     (u32)g_vm_mock_arena_config.damageScaleEnabled,
                     (u32)g_vm_mock_arena_config.allowHeal,
                     (u32)g_vm_mock_arena_config.allowDodge,
                     (u32)g_vm_mock_arena_config.allowCombatPill);
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
    if (scene == NULL || scene[0] == 0)
        return false;
    if (vm_net_mock_scene_names_equal_loose(scene, g_vm_mock_arena_config.scene))
        return true;
    /* Encoding / suffix fallbacks — fail closed for isolation must still fire.
     * Also accept c04比武擂台50 / c04比武擂台70 job venues. */
    if (strstr(scene, "\xb1\xc8\xce\xe4\xc0\xde\xcc\xa8") != NULL) /* GBK 比武擂台 */
        return true;
    /* UTF-8 比武擂台 (擂=U+64C2 → E6 93 82) */
    if (strstr(scene, "\xe6\xaf\x94\xe6\xad\xa6\xe6\x93\x82\xe5\x8f\xb0") != NULL)
        return true;
    /* Legacy mistyped UTF-8 擂 (E6 92 92) kept for old clients/logs. */
    if (strstr(scene, "\xe6\xaf\x94\xe6\xad\xa6\xe6\x92\x92\xe5\x8f\xb0") != NULL)
        return true;
    return false;
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

/* 比武排行榜：挂在「英雄榜」NPC，不挂比武使者进场/离场对话。 */
static bool vm_mock_arena_npc_offers_rank(const char *npcName)
{
    (void)vm_mock_arena_load_config(false);
    if (!g_vm_mock_arena_config.enabled || npcName == NULL || npcName[0] == 0)
        return false;
    /* GBK: 英雄榜 */
    return strstr(npcName, "\xd3\xa2\xd0\xdb\xb0\xf1") != NULL;
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

/*
 * 进场/比赛期间擂台同图隔离：同职业×同段位互见（地图 2/10 与周围列表 2/7
 * 共用 build_scene_role_seeds）。
 *
 * 根因（2026-08-14）：旧逻辑强制双方都有内存 participant 且 inArenaScene。
 * ensure_session 换 session 会 part_count=0，进程重启也不 hydrate scores，
 * find_part 失败 → 一律不可见；或阶段已 ENTRY/LIVE 但分桶数据缺失时表现错乱。
 * 契约是按当前职业/等级段限制，不是按「报名表行是否还在」。
 *
 * 2026-08-14b：observer 场景名判定失败时曾 fail-open（return true），隔离整段失效；
 * 改为 onlineScene 回退，且 ENTRY/LIVE 下缺职业/段位 fail-closed。
 */
static bool vm_mock_arena_peer_visible(const vm_mock_service_client_session *observer,
                                        const vm_mock_service_client_session *peer)
{
    const vm_mock_arena_participant *a;
    const vm_mock_arena_participant *b;
    const char *obsScene;
    const char *peerScene;
    u8 jobA;
    u8 jobB;
    u16 levelA;
    u16 levelB;
    u16 bracketA;
    u16 bracketB;

    if (observer == NULL || peer == NULL)
        return true;
    /* Keep ENTRY/LIVE status current even when 2/7 opens before scene-sync. */
    (void)vm_mock_arena_tick();
    if (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_ENTRY &&
        g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE)
        return true;

    obsScene = observer->sceneVisibleScene[0] ? observer->sceneVisibleScene
                                              : observer->onlineScene;
    peerScene = peer->sceneVisibleScene[0] ? peer->sceneVisibleScene
                                            : peer->onlineScene;
    if (!vm_mock_arena_is_arena_scene(obsScene))
        return true;
    if (!vm_mock_arena_is_arena_scene(peerScene))
        return false;

    jobA = observer->onlineJob;
    jobB = peer->onlineJob;
    levelA = observer->onlineLevel;
    levelB = peer->onlineLevel;
    bracketA = vm_mock_arena_bracket_for_level(levelA);
    bracketB = vm_mock_arena_bracket_for_level(levelB);

    /* Enter-time snapshot wins when present (matches matchmaking buckets). */
    a = vm_mock_arena_find_part(observer->onlineRoleId);
    b = vm_mock_arena_find_part(peer->onlineRoleId);
    if (a != NULL && a->job != 0 && a->bracket != 0)
    {
        jobA = a->job;
        levelA = a->level ? a->level : levelA;
        bracketA = a->bracket;
    }
    if (b != NULL && b->job != 0 && b->bracket != 0)
    {
        jobB = b->job;
        levelB = b->level ? b->level : levelB;
        bracketB = b->bracket;
    }
    if (jobA == 0 || jobB == 0 || bracketA == 0 || bracketB == 0)
        return false;
    {
        u16 active = vm_mock_arena_active_bracket_today();
        if (vm_mock_arena_is_ffa_bracket(active))
        {
            bool aOk =
                vm_mock_arena_is_ffa_bracket(bracketA) || bracketA == 70;
            bool bOk =
                vm_mock_arena_is_ffa_bracket(bracketB) || bracketB == 70;
            return aOk && bOk;
        }
    }
    return vm_mock_arena_same_bucket(jobA, bracketA, levelA, jobB, bracketB,
                                     levelB);
}

/* Status flip ENTRY↔LIVE↔SETTLED：清擂台内 peerSync，迫使下次 poll 重 baseline/1/2/6。 */
static void vm_mock_arena_refresh_nearby_for_arena_clients(const char *reason)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;

    while (session != NULL)
    {
        if (session->clientId != 0 &&
            vm_mock_arena_is_arena_scene(session->sceneVisibleScene))
        {
            vm_mock_service_session_invalidate_nearby_observer_cursors(
                session, reason ? reason : "arena-nearby-refresh");
        }
        session = session->next;
    }
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

/* 离线：清 clientId，避免旧座位被匹配；若断线时仍在擂台则保留 inArenaScene 供重登回场。 */
static void vm_mock_arena_on_session_offline(vm_mock_service_client_session *session)
{
    vm_mock_arena_participant *p;

    if (session == NULL || session->onlineRoleId == 0)
        return;
    p = vm_mock_arena_find_part(session->onlineRoleId);
    if (p == NULL)
        return;
    if (p->clientId == session->clientId)
        p->clientId = 0;
    if (vm_mock_arena_is_arena_scene(session->sceneVisibleScene) ||
        vm_mock_arena_is_arena_scene(session->onlineScene))
    {
        p->inArenaScene = 1;
        return;
    }
    if (session->sceneVisibleScene[0] != 0 &&
        !vm_mock_arena_is_arena_scene(session->sceneVisibleScene))
    {
        p->inArenaScene = 0;
    }
}

/* 选角登录：本场已进场且活动未结束时，强制 role->scene 回擂台（修复离线后落到原图仍被匹配）。 */
static void vm_mock_arena_restore_venue_scene_if_needed(vm_net_mock_role_state *role)
{
    vm_mock_arena_participant *p;
    u16 x;
    u16 y;

    if (role == NULL || role->roleId == 0)
        return;
    (void)vm_mock_arena_tick();
    if (g_vm_mock_arena_rt.sessionId == 0 ||
        (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_ENTRY &&
         g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE))
        return;
    p = vm_mock_arena_find_part(role->roleId);
    if (p == NULL || !p->inArenaScene)
        return;
    (void)vm_mock_arena_load_config(false);
    {
        char venue[80];
        u8 job = role->job ? role->job : (p->job ? p->job : 1);
        if (p->job != 0)
            job = p->job;
        vm_mock_arena_scene_for_entry(job, p->bracket, venue, sizeof(venue));
        if (venue[0] != 0 &&
            vm_net_mock_scene_names_equal_loose(role->scene, venue))
            return;
        x = g_vm_mock_arena_config.spawnX ? g_vm_mock_arena_config.spawnX : 120;
        y = g_vm_mock_arena_config.spawnY ? g_vm_mock_arena_config.spawnY : 120;
        printf("[info][mock-service] arena_restore_venue role=%u from=%s to=%s "
               "job=%u bracket=%u pos=(%u,%u) evidence=offline-rejoin-keep-venue\n",
               role->roleId, role->scene[0] ? role->scene : "-", venue,
               (u32)job, (u32)p->bracket, x, y);
        vm_net_mock_role_set_position(venue, x, y, "arena-restore-venue");
    }
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
    p = vm_mock_arena_find_part(role->roleId);
    if (p == NULL)
        return;
    p->clientId = session->clientId;
    p->job = role->job ? role->job : session->onlineJob;
    p->level = (u16)(role->level ? role->level : session->onlineLevel);
    p->bracket = vm_mock_arena_enrollment_bracket(p->level);
    scene = session->sceneVisibleScene[0] ? session->sceneVisibleScene : role->scene;
    if (vm_mock_arena_is_arena_scene(scene) ||
        vm_mock_arena_is_arena_scene(role->scene))
    {
        p->inArenaScene = 1;
        printf("[info][mock-service] arena_rebind role=%u client=%08x scene=%s "
               "status=%u score=%u evidence=offline-rejoin-keep-venue\n",
               p->roleId, p->clientId, scene ? scene : "-", g_vm_mock_arena_rt.status,
               p->score);
        return;
    }
    /* 已离开擂台地图：禁止再被匹配拉入战斗。 */
    if (p->inArenaScene)
    {
        p->inArenaScene = 0;
        printf("[info][mock-service] arena_matching_drop role=%u client=%08x "
               "scene=%s evidence=left-venue-no-force-battle\n",
               p->roleId, session->clientId, scene ? scene : "-");
    }
}

static vm_mock_service_client_session *vm_mock_arena_live_session_for_part(
    vm_mock_arena_participant *p)
{
    vm_mock_service_client_session *session;

    if (p == NULL || p->roleId == 0)
        return NULL;
    if (p->clientId != 0)
    {
        session = vm_mock_service_find_client_session(p->clientId);
        if (session != NULL && session->roleOnline &&
            session->onlineRoleId == p->roleId)
            return session;
    }
    session = g_vm_mock_service_client_sessions;
    while (session != NULL)
    {
        if (session->clientId != 0 && session->roleOnline &&
            session->onlineRoleId == p->roleId)
        {
            p->clientId = session->clientId;
            return session;
        }
        session = session->next;
    }
    return NULL;
}

static void vm_mock_arena_note_result(vm_mock_service_client_session *session,
                                      vm_mock_arena_participant *p, const char *kind,
                                      u32 delta)
{
    /* GBK: 比武胜利/失败/平局/轮空 积分+N 当前积分M n胜/n负/n平/n轮空 */
    static const char winGbk[] = "\xb1\xc8\xce\xe4\xca\xa4\xc0\xfb";
    static const char loseGbk[] = "\xb1\xc8\xce\xe4\xca\xa7\xb0\xdc";
    static const char drawGbk[] = "\xb1\xc8\xce\xe4\xc6\xbd\xbe\xd3";
    static const char byeGbk[] = "\xb1\xc8\xce\xe4\xc2\xd6\xbf\xd5"; /* 比武轮空 */
    const char *label = loseGbk;

    if (session == NULL || p == NULL)
        return;
    if (kind != NULL)
    {
        if (strcmp(kind, "bye") == 0)
            label = byeGbk;
        else if (strcmp(kind, "win") == 0)
            label = winGbk;
        else if (strcmp(kind, "draw") == 0)
            label = drawGbk;
        else
            label = loseGbk;
    }
    snprintf(session->arenaResultFdata, sizeof(session->arenaResultFdata),
             "%s \xbb\xfd\xb7\xd6+%u \xb5\xb1\xc7\xb0\xbb\xfd\xb7\xd6%u "
             "%u\xca\xa4/%u\xb8\xba/%u\xc6\xbd/%u\xc2\xd6\xbf\xd5",
             label, delta, p->score, p->wins, p->losses, p->draws, p->byes);
    session->arenaResultPending = true;
    printf("[info][mock-service] arena_result_note role=%u kind=%s delta=%u "
           "total=%u wldb=%u/%u/%u/%u fdata=%s\n",
           p->roleId, kind ? kind : "-", delta, p->score, p->wins, p->losses,
           p->draws, p->byes, session->arenaResultFdata);
}

/* 1敌将讨伐 2双杀 3大杀特杀 … 10举世无双 11+天下无敌 */
static const char *vm_mock_arena_kill_streak_title_gbk(u32 streak)
{
    static const char t1[] = "\xb5\xd0\xbd\xab\xcc\xd6\xb7\xa5"; /* 敌将讨伐 */
    static const char t2[] = "\xcb\xab\xc9\xb1";                 /* 双杀 */
    static const char t3[] = "\xb4\xf3\xc9\xb1\xcc\xd8\xc9\xb1"; /* 大杀特杀 */
    static const char t4[] = "\xcb\xf9\xcf\xf2\xc5\xfb\xc3\xd2"; /* 所向披靡 */
    static const char t5[] = "\xbb\xeb\xc9\xed\xca\xc7\xb5\xa8"; /* 浑身是胆 */
    static const char t6[] = "\xca\xc6\xc8\xe7\xc6\xc6\xd6\xf1"; /* 势如破竹 */
    static const char t7[] = "\xd3\xc2\xb9\xda\xc8\xfd\xbe\xfc"; /* 勇冠三军 */
    static const char t8[] = "\xd2\xbb\xc6\xef\xb5\xb1\xc7\xa7"; /* 一骑当千 */
    static const char t9[] = "\xcd\xf2\xb7\xf2\xc4\xaa\xb5\xd0"; /* 万夫莫敌 */
    static const char t10[] = "\xbe\xd9\xca\xc0\xce\xde\xcb\xab"; /* 举世无双 */
    static const char t11[] = "\xcc\xec\xcf\xc2\xce\xde\xb5\xd0"; /* 天下无敌 */

    if (streak == 0)
        return NULL;
    if (streak == 1)
        return t1;
    if (streak == 2)
        return t2;
    if (streak == 3)
        return t3;
    if (streak == 4)
        return t4;
    if (streak == 5)
        return t5;
    if (streak == 6)
        return t6;
    if (streak == 7)
        return t7;
    if (streak == 8)
        return t8;
    if (streak == 9)
        return t9;
    if (streak == 10)
        return t10;
    return t11;
}

/* streak 1..10 → bit0..9；11+ → bit10。未播过则占位并返回 true。 */
static bool vm_mock_arena_kill_streak_claim_first(u32 streak)
{
    u32 tier;
    u32 bit;

    if (streak == 0)
        return false;
    tier = streak >= 11u ? 11u : streak;
    bit = 1u << (tier - 1u);
    if ((g_vm_mock_arena_kill_streak_announced_mask & bit) != 0)
        return false;
    g_vm_mock_arena_kill_streak_announced_mask |= bit;
    return true;
}

static void vm_mock_arena_announce_kill_streak(const vm_mock_arena_participant *p)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    const char *title;
    const char *name;
    char stage[16];
    char message[82];
    int wrote;
    u32 recipients;

    if (p == NULL)
        return;
    title = vm_mock_arena_kill_streak_title_gbk(p->killStreak);
    if (title == NULL)
        return;
    name = p->roleName[0] ? p->roleName : NULL;
    if (name == NULL)
        return;
    /* 阶段=段位：10-49 / 50-69 / 70级 / 大乱斗 */
    if (vm_mock_arena_is_ffa_bracket(p->bracket))
        snprintf(stage, sizeof(stage),
                 "\xb4\xf3\xc2\xd2\xb6\xb7"); /* 大乱斗 */
    else if (p->bracket == 70)
        snprintf(stage, sizeof(stage), "70\xbc\xb6"); /* 70级 */
    else if (p->bracket == 50)
        snprintf(stage, sizeof(stage), "50-69");
    else if (p->bracket == 10)
        snprintf(stage, sizeof(stage), "10-49");
    else if (p->bracket != 0)
        snprintf(stage, sizeof(stage), "%u", (u32)p->bracket);
    else
        snprintf(stage, sizeof(stage), "?");
    /* {玩家} 比武 {阶段} 连胜N人,已经{称号} ! */
    memset(message, 0, sizeof(message));
    wrote = snprintf(
        message, sizeof(message),
        "%s \xb1\xc8\xce\xe4 %s \xc1\xac\xca\xa4%u\xc8\xcb,\xd2\xd1\xbe\xad%s !",
        name, stage, p->killStreak, title);
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
    {
        /* {玩家} 比武 连胜N人,已经{称号} ! */
        memset(message, 0, sizeof(message));
        wrote = snprintf(
            message, sizeof(message),
            "%s \xb1\xc8\xce\xe4 \xc1\xac\xca\xa4%u\xc8\xcb,\xd2\xd1\xbe\xad%s !",
            name, p->killStreak, title);
    }
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
        return;
    recipients = vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][mock-service] arena_kill_streak role=%u streak=%u bracket=%u "
           "recipients=%u tip=%s\n",
           p->roleId, p->killStreak, (u32)p->bracket, recipients, message);
}

/* Win while alive → streak++; loss → 0; draw/bye unchanged.
 * 播报+文字称号：仅 70 阶段；本场 LIVE 每档仅全场首个达成者播报并解锁称号。 */
static void vm_mock_arena_unlock_kill_streak_designation(
    vm_mock_arena_participant *winner)
{
    vm_mock_service_client_session *session;
    vm_mock_service_account_state *account = NULL;
    vm_mock_service_account_state *savedActive = g_vm_mock_service_active_account;
    const char *savedActiveId = g_vm_mock_service_active_account_id;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    vm_net_mock_role_state *role = NULL;
    u8 designationId;
    u32 bit;
    u32 i;

    if (winner == NULL || !vm_mock_arena_is_kill_streak_bracket(winner->bracket) ||
        winner->killStreak == 0)
        return;
    designationId =
        vm_net_mock_arena_designation_id_for_streak(winner->killStreak);
    bit = vm_net_mock_arena_designation_mask_bit(designationId);
    if (bit == 0)
        return;

    session = vm_mock_service_find_client_session(winner->clientId);
    if (session == NULL || session->accountId[0] == 0 ||
        session->onlineRoleId == 0)
        return;

    if (savedActive != NULL)
        vm_mock_service_account_capture(savedActive);
    account = vm_mock_service_account_find_or_create(session->accountId);
    if (account == NULL)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return;
    }
    vm_mock_service_account_restore(account);
    g_vm_mock_service_active_client_id = session->clientId;
    if (!vm_mock_service_account_ensure_role_db_loaded(account) ||
        !g_vm_net_mock_role_db_valid)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return;
    }
    for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == winner->roleId)
        {
            role = &g_vm_net_mock_role_db.roles[i];
            break;
        }
    }
    if (role != NULL && (role->arenaDesignationMask & bit) == 0)
    {
        role->arenaDesignationMask |= bit;
        vm_net_mock_role_mark_inventory_dirty("arena-kill-streak-title");
        if (g_vm_net_mock_role_position_dirty || g_vm_net_mock_role_inventory_dirty)
            vm_net_mock_role_db_save("arena-kill-streak-title");
        printf("[info][mock-service] arena_title_unlock role=%u streak=%u "
               "designation=%u mask=0x%x\n",
               role->roleId, winner->killStreak, (u32)designationId,
               role->arenaDesignationMask);
    }
    vm_mock_service_account_capture(account);
    if (savedActive != NULL)
        vm_mock_service_account_restore(savedActive);
    else
        vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_account_id = savedActiveId;
    g_vm_mock_service_active_client_id = savedClientId;
}

static void vm_mock_arena_clear_kill_streak_designations(const char *reason)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;
    vm_mock_service_account_state *savedActive = g_vm_mock_service_active_account;
    const char *savedActiveId = g_vm_mock_service_active_account_id;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    u32 clearedOnline = 0;

    (void)vm_net_mock_role_ensure_arena_designation_column();
    (void)vm_mysql_exec(
        "UPDATE account_roles SET arena_designation_mask=0 "
        "WHERE arena_designation_mask<>0");

    if (savedActive != NULL)
        vm_mock_service_account_capture(savedActive);

    while (session != NULL)
    {
        vm_mock_service_account_state *account = NULL;
        vm_net_mock_role_state *role = NULL;
        u32 i;
        bool changed = false;

        if (session->accountId[0] == 0 || session->onlineRoleId == 0)
        {
            session = session->next;
            continue;
        }
        account = vm_mock_service_account_find_or_create(session->accountId);
        if (account == NULL ||
            !vm_mock_service_account_ensure_role_db_loaded(account) ||
            !g_vm_net_mock_role_db_valid)
        {
            session = session->next;
            continue;
        }
        vm_mock_service_account_restore(account);
        g_vm_mock_service_active_client_id = session->clientId;
        for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            if (g_vm_net_mock_role_db.roles[i].roleId != session->onlineRoleId)
                continue;
            role = &g_vm_net_mock_role_db.roles[i];
            break;
        }
        if (role != NULL)
        {
            if (role->arenaDesignationMask != 0)
            {
                role->arenaDesignationMask = 0;
                changed = true;
            }
            if (vm_net_mock_designation_is_arena_id(role->designationId))
            {
                const vm_net_mock_designation_entry *best =
                    vm_net_mock_role_best_designation(role);
                role->designationId = best ? best->id : 0;
                changed = true;
                snprintf(session->onlineRoleTitle,
                         sizeof(session->onlineRoleTitle), "%s",
                         best ? best->name : "");
                snprintf(session->onlineRoleTitleBadge,
                         sizeof(session->onlineRoleTitleBadge), "%s",
                         best && best->overheadResource
                             ? best->overheadResource
                             : "");
            }
            if (changed)
            {
                vm_net_mock_role_mark_inventory_dirty("arena-title-clear");
                if (g_vm_net_mock_role_position_dirty ||
                    g_vm_net_mock_role_inventory_dirty)
                    vm_net_mock_role_db_save("arena-title-clear");
                ++clearedOnline;
            }
        }
        vm_mock_service_account_capture(account);
        session = session->next;
    }

    if (savedActive != NULL)
        vm_mock_service_account_restore(savedActive);
    else
        vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_account_id = savedActiveId;
    g_vm_mock_service_active_client_id = savedClientId;
    printf("[info][mock-service] arena_title_clear reason=%s online=%u\n",
           reason ? reason : "-", clearedOnline);
}

static void vm_mock_arena_apply_kill_streak(vm_mock_arena_participant *winner,
                                            vm_mock_arena_participant *loser)
{
    if (loser != NULL)
        loser->killStreak = 0;
    if (winner == NULL)
        return;
    if (winner->killStreak < 0xffffffffu)
        winner->killStreak += 1;
    /* 10/50 段位不播报/不发称号；70 与周末大乱斗播报. */
    if (!vm_mock_arena_is_kill_streak_bracket(winner->bracket))
        return;
    /* 仅被世界播报的首个达成者解锁文字称号。 */
    if (!vm_mock_arena_kill_streak_claim_first(winner->killStreak))
        return;
    vm_mock_arena_unlock_kill_streak_designation(winner);
    vm_mock_arena_announce_kill_streak(winner);
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
    u32 dayYmd = 0;
    u32 weekdayBit = 0;

    /* Caller already loaded config; still re-read weekday for safety. */
    vm_mock_arena_wall_clock(&dayYmd, NULL, &weekdayBit, NULL);
    (void)dayYmd;
    if ((g_vm_mock_arena_config.weekdayMask & weekdayBit) == 0)
        return VM_MOCK_ARENA_STATUS_IDLE;
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

typedef struct
{
    u32 sessionId;
    u8 status;
} vm_mock_arena_session_meta;

static bool vm_mock_arena_session_meta_row(void *contextValue, unsigned int columnCount,
                                           const char *const *values, const size_t *lengths)
{
    vm_mock_arena_session_meta *meta = (vm_mock_arena_session_meta *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (meta == NULL || values == NULL || lengths == NULL)
        return false;
    if (!values[0] || !vm_mock_mysql_parse_u32(values[0], lengths[0], &meta->sessionId))
        return false;
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        meta->status = (u8)tmp;
    return true;
}

static bool vm_mock_arena_score_hydrate_row(void *contextValue, unsigned int columnCount,
                                            const char *const *values, const size_t *lengths)
{
    u32 *loaded = (u32 *)contextValue;
    vm_mock_arena_participant *p;
    u32 roleId = 0;
    u32 tmp = 0;

    (void)columnCount;
    if (values == NULL || lengths == NULL)
        return false;
    if (g_vm_mock_arena_part_count >= VM_MOCK_ARENA_PARTICIPANT_MAX)
        return false;
    if (!values[0] || !vm_mock_mysql_parse_u32(values[0], lengths[0], &roleId) ||
        roleId == 0)
        return false;
    if (vm_mock_arena_find_part(roleId) != NULL)
        return true;
    p = &g_vm_mock_arena_parts[g_vm_mock_arena_part_count++];
    memset(p, 0, sizeof(*p));
    p->roleId = roleId;
    if (values[1] && lengths[1] > 0 && lengths[1] < sizeof(p->accountId))
    {
        memcpy(p->accountId, values[1], lengths[1]);
        p->accountId[lengths[1]] = 0;
    }
    if (values[2] && lengths[2] > 0 && lengths[2] < sizeof(p->roleName))
    {
        memcpy(p->roleName, values[2], lengths[2]);
        p->roleName[lengths[2]] = 0;
    }
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        p->job = (u8)tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        p->bracket = (u16)tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        p->level = (u16)tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        p->score = tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        p->wins = tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        p->losses = tmp;
    if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
        p->draws = tmp;
    if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
        p->byes = tmp;
    if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
        p->rankPos = tmp;
    if (values[12] && lengths[12])
        p->claimed = (u8)(values[12][0] == '0' ? 0 : 1);
    /* Venue flag restored when the player is actually online on the map. */
    p->inArenaScene = 0;
    if (loaded)
        *loaded += 1u;
    return true;
}

static void vm_mock_arena_hydrate_parts_from_db(void)
{
    char query[256];
    u32 loaded = 0;

    if (g_vm_mock_arena_rt.sessionId == 0)
        return;
    snprintf(query, sizeof(query),
             "SELECT role_id,account_id,role_name,job,bracket,level,score,wins,"
             "losses,draws,byes,rank_pos,claimed FROM server_arena_scores "
             "WHERE session_id=%u",
             g_vm_mock_arena_rt.sessionId);
    (void)vm_mysql_query(query, vm_mock_arena_score_hydrate_row, &loaded);
    printf("[info][mock-service] arena_hydrate session=%u loaded=%u parts=%u "
           "evidence=restart-or-session-bind\n",
           g_vm_mock_arena_rt.sessionId, loaded, g_vm_mock_arena_part_count);
}

/*
 * Mid-event admin schedule edits must move match_*_ts from config minutes.
 * Otherwise rounds keep a stale clock while status still flips via minute gates.
 */
static void vm_mock_arena_sync_schedule_timestamps(u32 dayYmd, u32 unixTs)
{
    u32 dayBase;
    u32 entryTs;
    u32 matchTs;
    u32 endTs;
    struct tm tmDay;
    time_t dayStart;
    char query[320];
    bool windowChanged;

    if (g_vm_mock_arena_rt.sessionId == 0 || dayYmd == 0)
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
    if (g_vm_mock_arena_rt.entryStartTs == entryTs &&
        g_vm_mock_arena_rt.matchStartTs == matchTs &&
        g_vm_mock_arena_rt.matchEndTs == endTs)
        return;
    windowChanged = g_vm_mock_arena_rt.matchStartTs != matchTs ||
                    g_vm_mock_arena_rt.matchEndTs != endTs;
    printf("[info][mock-service] arena_schedule_resync session=%u "
           "entry=%u->%u match=%u->%u end=%u->%u reset_rounds=%u "
           "evidence=config-minutes\n",
           g_vm_mock_arena_rt.sessionId, g_vm_mock_arena_rt.entryStartTs, entryTs,
           g_vm_mock_arena_rt.matchStartTs, matchTs, g_vm_mock_arena_rt.matchEndTs,
           endTs, windowChanged ? 1u : 0u);
    g_vm_mock_arena_rt.entryStartTs = entryTs;
    g_vm_mock_arena_rt.matchStartTs = matchTs;
    g_vm_mock_arena_rt.matchEndTs = endTs;
    /* Any LIVE window shift must reopen rounds — stale lastMatchRound would
     * skip the new roundIndex computed from the updated matchStartTs. */
    if (windowChanged)
        g_vm_mock_arena_rt.lastMatchRound = 0xffffffffu;
    snprintf(query, sizeof(query),
             "UPDATE server_arena_sessions SET entry_start_ts=%u,match_start_ts=%u,"
             "match_end_ts=%u WHERE session_id=%u",
             entryTs, matchTs, endTs, g_vm_mock_arena_rt.sessionId);
    (void)vm_mysql_exec(query);
}

/* Rewrite ENTRY/LIVE rows for dayYmd from current config (no hardcoded clock). */
static void vm_mock_arena_rewrite_open_sessions_for_day(u32 dayYmd, u32 unixTs)
{
    u32 dayBase;
    u32 entryTs;
    u32 matchTs;
    u32 endTs;
    struct tm tmDay;
    time_t dayStart;
    char query[384];

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
             "UPDATE server_arena_sessions SET entry_start_ts=%u,match_start_ts=%u,"
             "match_end_ts=%u WHERE day_ymd=%u AND status IN (%u,%u)",
             entryTs, matchTs, endTs, dayYmd,
             (u32)VM_MOCK_ARENA_STATUS_ENTRY, (u32)VM_MOCK_ARENA_STATUS_LIVE);
    if (vm_mysql_exec(query))
        printf("[info][mock-service] arena_open_sessions_resync day=%u "
               "entry=%u match=%u end=%u evidence=config-minutes\n",
               dayYmd, entryTs, matchTs, endTs);
}

static void vm_mock_arena_set_status(u8 status);
static void vm_mock_arena_reconcile_status_from_desired(u32 minute);
static void vm_mock_arena_settle(void);
static void vm_mock_arena_try_settle_or_playoff(void);
static void vm_mock_arena_match_playoff_ties(void);
static void vm_mock_arena_grant_bye(vm_mock_arena_participant *bye, u32 roundIndex,
                                   u32 poolCount);
static void vm_mock_arena_evict_offline_from_venue(const char *reason);
static void vm_mock_arena_ensure_session(u32 dayYmd, u8 slot, u32 unixTs);

/* After admin saves minutes: push into runtime + today's open session rows. */
static void vm_mock_arena_apply_config_schedule_now(void)
{
    u32 dayYmd = 0;
    u32 minute = 0;
    u32 unixTs = 0;
    u32 weekdayBit = 0;
    u32 bindDay;

    vm_mock_arena_wall_clock(&dayYmd, &minute, &weekdayBit, &unixTs);
    /*
     * Prefer today's session when the activity window is open. Otherwise a
     * leftover SETTLED row from a prior dayYmd keeps HUD/match dead until the
     * next save (timestamps rewritten onto the stale session).
     */
    if (dayYmd != 0 &&
        (g_vm_mock_arena_config.weekdayMask & weekdayBit) != 0 &&
        minute >= g_vm_mock_arena_config.entryStartMinute &&
        minute < g_vm_mock_arena_config.matchEndMinute + 120u)
    {
        u8 slot = ((weekdayBit & (1u << 6)) != 0) ? 1 : 0;
        vm_mock_arena_ensure_session(dayYmd, slot, unixTs);
    }
    bindDay = g_vm_mock_arena_rt.dayYmd ? g_vm_mock_arena_rt.dayYmd : dayYmd;
    if (g_vm_mock_arena_rt.sessionId != 0 && bindDay != 0)
        vm_mock_arena_sync_schedule_timestamps(bindDay, unixTs);
    if (dayYmd != 0)
        vm_mock_arena_rewrite_open_sessions_for_day(dayYmd, unixTs);
    /* Same-day retest: reopen SETTLED→ENTRY/LIVE when minutes move back into
     * the window so phase tips / HUD fire again. */
    if (g_vm_mock_arena_rt.sessionId != 0)
        vm_mock_arena_reconcile_status_from_desired(minute);
}

/* Anyone already standing on the venue during ENTRY/LIVE must be matchable + HUD.
 * Use session online* fields only — trade_role_for_session can load roleDb and
 * must not run for every online seat on the 1Hz tick (protocol-lock stall). */
static void vm_mock_arena_sync_venue_participants(void)
{
    vm_mock_service_client_session *session;

    if (g_vm_mock_arena_rt.sessionId == 0 ||
        (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_ENTRY &&
         g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE))
        return;
    session = g_vm_mock_service_client_sessions;
    while (session != NULL)
    {
        vm_mock_arena_participant *p;
        const char *scene;

        if (session->clientId != 0 && session->roleOnline &&
            session->onlineRoleId != 0)
        {
            scene = session->sceneVisibleScene[0] ? session->sceneVisibleScene
                                                  : session->onlineScene;
            if (vm_mock_arena_is_arena_scene(scene) ||
                vm_mock_arena_is_arena_scene(session->onlineScene))
            {
                p = vm_mock_arena_ensure_participant(session, NULL);
                if (p != NULL)
                {
                    p->clientId = session->clientId;
                    p->inArenaScene = 1;
                }
            }
        }
        session = session->next;
    }
}

static const char *vm_mock_arena_phase_tip_gbk(u8 status)
{
    /* 比武擂台已开放进场，请尽快前往！ / 比武比赛开始！ / 比武已结束，可查看排行领奖。 */
    static const char entryGbk[] =
        "\xb1\xc8\xce\xe4\xc0\xde\xcc\xa8\xd2\xd1\xbf\xaa\xb7\xc5\xbd\xf8\xb3\xa1"
        "\xa3\xac\xc7\xeb\xbd\xf1\xbf\xec\xc7\xb0\xcd\xf9\xa3\xa1";
    static const char liveGbk[] =
        "\xb1\xc8\xce\xe4\xb1\xc8\xc8\xfc\xbf\xaa\xca\xbc\xa3\xa1";
    static const char settledGbk[] =
        "\xb1\xc8\xce\xe4\xd2\xd1\xbd\xe1\xca\xf8\xa3\xac\xbf\xc9\xb2\xe9\xbf\xb4"
        "\xc5\xc5\xd0\xd0\xc1\xec\xbd\xac\xa1\xa3";

    if (status == VM_MOCK_ARENA_STATUS_ENTRY)
        return entryGbk;
    if (status == VM_MOCK_ARENA_STATUS_LIVE)
        return liveGbk;
    if (status == VM_MOCK_ARENA_STATUS_SETTLED)
        return settledGbk;
    return NULL;
}

static void vm_mock_arena_deliver_phase_tip_to_session(
    vm_mock_service_client_session *session, u8 status, const char *msg)
{
    if (session == NULL || msg == NULL || msg[0] == 0)
        return;
    if (!session->roleOnline || session->onlineRoleId == 0)
        return;
    if (session->arenaPhaseTipStatus == status)
        return;
    if (!vm_mock_service_session_enqueue_system_message(session, msg))
        return;
    session->arenaPhaseTipStatus = status;
}

static void vm_mock_arena_announce_status(u8 status)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    const char *msg;
    vm_mock_service_client_session *session;
    u32 recipients = 0;
    u32 worldRecipients = 0;
    bool statusEdge;

    msg = vm_mock_arena_phase_tip_gbk(status);
    if (msg == NULL)
    {
        g_vm_mock_arena_announced_status = status;
        return;
    }
    statusEdge = (status != g_vm_mock_arena_announced_status);
    /* Always fan out system 1/3/3 so Pixi can toast; also [世] on status edge. */
    session = g_vm_mock_service_client_sessions;
    while (session != NULL)
    {
        if (session->roleOnline && session->onlineRoleId != 0)
        {
            /* Force re-tip on status edge even if client already saw prior phase. */
            if (statusEdge)
                session->arenaPhaseTipStatus = 0xffu;
            vm_mock_arena_deliver_phase_tip_to_session(session, status, msg);
            if (session->arenaPhaseTipStatus == status)
                ++recipients;
        }
        session = session->next;
    }
    if (statusEdge)
        worldRecipients =
            vm_mock_service_broadcast_world_chat_live(systemNameGbk, msg);
    g_vm_mock_arena_announced_status = status;
    if (statusEdge || worldRecipients != 0)
        printf("[info][mock-service] arena_announce status=%u system=%u world=%u\n",
               (u32)status, recipients, worldRecipients);
}

/* Late joiners / missed edge: tip this seat only (call from scene-sync poll). */
static void vm_mock_arena_catchup_phase_tip_session(
    vm_mock_service_client_session *session)
{
    const char *msg;
    u8 status = g_vm_mock_arena_rt.status;

    if (session == NULL || g_vm_mock_arena_rt.sessionId == 0)
        return;
    if (status != VM_MOCK_ARENA_STATUS_ENTRY &&
        status != VM_MOCK_ARENA_STATUS_LIVE &&
        status != VM_MOCK_ARENA_STATUS_SETTLED)
        return;
    msg = vm_mock_arena_phase_tip_gbk(status);
    if (msg == NULL)
        return;
    vm_mock_arena_deliver_phase_tip_to_session(session, status, msg);
}

static void vm_mock_arena_ensure_session(u32 dayYmd, u8 slot, u32 unixTs)
{
    char query[512];
    vm_mock_arena_session_meta meta;
    u32 entryTs;
    u32 matchTs;
    u32 endTs;
    u32 dayBase;
    struct tm tmDay;
    time_t dayStart;
    bool created = false;

    if (dayYmd == 0)
        return;
    /* Already bound for this day/slot: do not SELECT every tick (ENTRY window
     * used to MySQL-query once/sec under protocol lock → admin 503 繁忙). */
    if (g_vm_mock_arena_rt.sessionId != 0 &&
        g_vm_mock_arena_rt.dayYmd == dayYmd &&
        g_vm_mock_arena_rt.slot == slot)
    {
        vm_mock_arena_sync_schedule_timestamps(dayYmd, unixTs);
        return;
    }
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

    memset(&meta, 0, sizeof(meta));
    snprintf(query, sizeof(query),
             "SELECT session_id,status FROM server_arena_sessions "
             "WHERE day_ymd=%u AND slot=%u LIMIT 1",
             dayYmd, (u32)slot);
    (void)vm_mysql_query(query, vm_mock_arena_session_meta_row, &meta);
    if (meta.sessionId == 0)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_arena_sessions (day_ymd,slot,status,entry_start_ts,"
                 "match_start_ts,match_end_ts) VALUES (%u,%u,%u,%u,%u,%u)",
                 dayYmd, (u32)slot, VM_MOCK_ARENA_STATUS_ENTRY, entryTs, matchTs, endTs);
        if (!vm_mysql_exec(query))
            return;
        created = true;
        meta.sessionId = 0;
        meta.status = VM_MOCK_ARENA_STATUS_ENTRY;
        (void)vm_mysql_query(
            "SELECT LAST_INSERT_ID()", vm_mock_arena_session_id_row, &meta.sessionId);
    }
    if (meta.sessionId == 0)
        return;
    if (g_vm_mock_arena_rt.sessionId != meta.sessionId)
    {
        memset(&g_vm_mock_arena_rt, 0, sizeof(g_vm_mock_arena_rt));
        g_vm_mock_arena_part_count = 0;
        g_vm_mock_arena_announced_status = 0xffu;
        g_vm_mock_arena_kill_streak_announced_mask = 0;
        g_vm_mock_arena_rt.sessionId = meta.sessionId;
        g_vm_mock_arena_rt.dayYmd = dayYmd;
        g_vm_mock_arena_rt.slot = slot;
        /* Keep DB status (LIVE/SETTLED) across restart — do not force ENTRY. */
        g_vm_mock_arena_rt.status =
            meta.status <= VM_MOCK_ARENA_STATUS_SETTLED ? meta.status
                                                       : VM_MOCK_ARENA_STATUS_ENTRY;
        g_vm_mock_arena_rt.entryStartTs = entryTs;
        g_vm_mock_arena_rt.matchStartTs = matchTs;
        g_vm_mock_arena_rt.matchEndTs = endTs;
        g_vm_mock_arena_rt.lastMatchRound = 0xffffffffu;
        g_vm_mock_arena_deferred_unmatched = 0;
        vm_mock_arena_hydrate_parts_from_db();
        vm_mock_arena_refresh_nearby_for_arena_clients("arena-session-begin");
        printf("[info][mock-service] arena_session_bind session=%u status=%u "
               "parts=%u match_ts=%u\n",
               g_vm_mock_arena_rt.sessionId, (u32)g_vm_mock_arena_rt.status,
               g_vm_mock_arena_part_count, g_vm_mock_arena_rt.matchStartTs);
    }
    else
    {
        vm_mock_arena_sync_schedule_timestamps(dayYmd, unixTs);
    }
    /* New day's first ENTRY row: leftover roles may still persist on the venue. */
    if (created && g_vm_mock_arena_rt.sessionId != 0 &&
        g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_ENTRY)
        vm_mock_arena_evict_offline_from_venue("session-create-entry");
}

static void vm_mock_arena_reset_session_scores(const char *reason)
{
    char query[192];
    u32 i;
    u32 n;

    if (g_vm_mock_arena_rt.sessionId == 0)
        return;
    snprintf(query, sizeof(query),
             "DELETE FROM server_arena_scores WHERE session_id=%u",
             g_vm_mock_arena_rt.sessionId);
    (void)vm_mysql_exec(query);
    n = g_vm_mock_arena_part_count;
    for (i = 0; i < n; ++i)
    {
        vm_mock_arena_participant *p = &g_vm_mock_arena_parts[i];
        p->score = 0;
        p->wins = 0;
        p->losses = 0;
        p->draws = 0;
        p->byes = 0;
        p->rankPos = 0;
        p->claimed = 0;
        p->matchedThisRound = 0;
        p->lastOpponentRoleId = 0;
        p->killStreak = 0;
    }
    printf("[info][mock-service] arena_score_reset session=%u parts=%u reason=%s "
           "evidence=new-entry-window\n",
           g_vm_mock_arena_rt.sessionId, n, reason ? reason : "-");
}

enum { VM_MOCK_ARENA_EVICT_MAX = 512 };

typedef struct
{
    char accountId[64];
    u32 roleId;
} vm_mock_arena_evict_row;

typedef struct
{
    u32 count;
    vm_mock_arena_evict_row rows[VM_MOCK_ARENA_EVICT_MAX];
} vm_mock_arena_evict_scan;

static bool vm_mock_arena_role_currently_online(u32 roleId)
{
    vm_mock_service_client_session *session;

    if (roleId == 0)
        return false;
    session = g_vm_mock_service_client_sessions;
    while (session != NULL)
    {
        if (session->roleOnline && session->onlineRoleId == roleId)
            return true;
        session = session->next;
    }
    return false;
}

static bool vm_mock_arena_persist_offline_exit(const char *accountId, u32 roleId,
                                              const char *scene, u16 x, u16 y)
{
    char accountEsc[160];
    char sceneHex[160];
    char query[640];
    size_t sceneLen;

    if (accountId == NULL || accountId[0] == 0 || roleId == 0 ||
        scene == NULL || !vm_net_mock_scene_name_is_persistable(scene) ||
        x == 0 || y == 0)
        return false;
    sceneLen = strlen(scene);
    if (sceneLen == 0 || sceneLen >= 64 ||
        vm_mysql_hex_encode(accountId, strlen(accountId), accountEsc,
                            sizeof(accountEsc)) == 0 ||
        vm_mysql_hex_encode(scene, sceneLen, sceneHex, sizeof(sceneHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "UPDATE account_roles SET scene=X'%s',pos_x=%u,pos_y=%u "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             sceneHex, (u32)x, (u32)y, accountEsc, roleId);
    return vm_mysql_exec(query);
}

static void vm_mock_arena_patch_account_exit_scene(const char *accountId, u32 roleId,
                                                   const char *scene, u16 x, u16 y)
{
    vm_mock_service_account_state *state;
    u32 i;
    bool changed = false;

    if (accountId == NULL || accountId[0] == 0 || roleId == 0 || scene == NULL)
        return;
    state = vm_mock_service_account_find(accountId);
    if (state == NULL || !state->roleDbValid)
        return;
    for (i = 0; i < state->roleDb.roleCount && i < VM_NET_MOCK_ROLE_DB_MAX_ROLES; ++i)
    {
        vm_net_mock_role_state *role = &state->roleDb.roles[i];
        if (role->roleId != roleId)
            continue;
        if (!vm_mock_arena_is_arena_scene(role->scene))
            continue;
        snprintf(role->scene, sizeof(role->scene), "%s", scene);
        role->x = x;
        role->y = y;
        changed = true;
    }
    if (state->pendingSceneSaveValid &&
        vm_mock_arena_is_arena_scene(state->pendingSceneSaveScene))
    {
        snprintf(state->pendingSceneSaveScene, sizeof(state->pendingSceneSaveScene),
                 "%s", scene);
        state->pendingSceneSaveX = x;
        state->pendingSceneSaveY = y;
        changed = true;
    }
    if (changed)
        vm_mock_service_account_note_durable_role_write(
            state->accountId, &state->roleDb, state->roleDbValid, &state->warehouse,
            false, true, "arena-evict-offline");
}

static bool vm_mock_arena_evict_one_offline(const char *accountId, u32 roleId)
{
    vm_mock_arena_participant *p;
    const char *scene;
    u16 x;
    u16 y;

    if (roleId == 0 || vm_mock_arena_role_currently_online(roleId))
        return false;
    p = vm_mock_arena_find_part(roleId);
    scene = VM_MOCK_ARENA_DEFAULT_LEAVE_SCENE;
    x = 120;
    y = 120;
    if (p != NULL && p->returnScene[0] != 0 &&
        !vm_mock_arena_is_arena_scene(p->returnScene))
    {
        scene = p->returnScene;
        x = p->returnX ? p->returnX : 120;
        y = p->returnY ? p->returnY : 120;
    }
    if (accountId != NULL && accountId[0] != 0)
    {
        if (!vm_mock_arena_persist_offline_exit(accountId, roleId, scene, x, y))
        {
            printf("[warn][mock-service] arena_evict_persist_fail role=%u "
                   "account=%s dest=%s\n",
                   roleId, accountId, scene);
            return false;
        }
        vm_mock_arena_patch_account_exit_scene(accountId, roleId, scene, x, y);
    }
    if (p != NULL)
        p->inArenaScene = 0;
    printf("[info][mock-service] arena_evict_offline role=%u dest=%s pos=(%u,%u) "
           "evidence=entry-kick-stale-venue\n",
           roleId, scene, (u32)x, (u32)y);
    return true;
}

static bool vm_mock_arena_evict_scan_row(void *contextValue, unsigned int columnCount,
                                        const char *const *values, const size_t *lengths)
{
    vm_mock_arena_evict_scan *scan = (vm_mock_arena_evict_scan *)contextValue;
    vm_mock_arena_evict_row *row;
    u32 roleId = 0;
    size_t acctLen;

    (void)columnCount;
    if (scan == NULL || values == NULL || lengths == NULL)
        return false;
    if (scan->count >= VM_MOCK_ARENA_EVICT_MAX)
        return true;
    if (!values[1] || !vm_mock_mysql_parse_u32(values[1], lengths[1], &roleId) ||
        roleId == 0)
        return true;
    row = &scan->rows[scan->count];
    memset(row, 0, sizeof(*row));
    row->roleId = roleId;
    acctLen = lengths[0];
    if (values[0] && acctLen > 0)
    {
        if (acctLen >= sizeof(row->accountId))
            acctLen = sizeof(row->accountId) - 1;
        memcpy(row->accountId, values[0], acctLen);
        row->accountId[acctLen] = 0;
    }
    scan->count++;
    return true;
}

static void vm_mock_arena_evict_offline_from_venue(const char *reason)
{
    vm_mock_arena_evict_scan scan;
    vm_mock_service_account_state *state;
    u32 i;
    u32 n = 0;

    (void)vm_mock_arena_load_config(false);
    memset(&scan, 0, sizeof(scan));
    (void)vm_mysql_query(
        "SELECT account_id,role_id FROM account_roles WHERE "
        "HEX(scene) LIKE '%B1C8CEE4C0DECCA8%' OR "
        "HEX(scene) LIKE '%E6AF94E6ADA6E69292E58FB0%'",
        vm_mock_arena_evict_scan_row, &scan);
    for (i = 0; i < scan.count; ++i)
    {
        if (vm_mock_arena_evict_one_offline(scan.rows[i].accountId,
                                            scan.rows[i].roleId))
            ++n;
    }
    state = g_vm_mock_service_accounts;
    while (state != NULL)
    {
        if (state->roleDbValid)
        {
            for (i = 0; i < state->roleDb.roleCount &&
                        i < VM_NET_MOCK_ROLE_DB_MAX_ROLES; ++i)
            {
                vm_net_mock_role_state *role = &state->roleDb.roles[i];
                if (role->roleId == 0 ||
                    !vm_mock_arena_is_arena_scene(role->scene))
                    continue;
                if (vm_mock_arena_evict_one_offline(state->accountId, role->roleId))
                    ++n;
            }
        }
        state = state->next;
    }
    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        vm_mock_arena_participant *p = &g_vm_mock_arena_parts[i];
        if (!p->inArenaScene || p->roleId == 0)
            continue;
        if (vm_mock_arena_evict_one_offline(p->accountId, p->roleId))
            ++n;
    }
    printf("[info][mock-service] arena_evict_offline_done moved=%u reason=%s "
           "evidence=prevent-stale-login-on-venue\n",
           n, reason ? reason : "-");
}

static void vm_mock_arena_set_status(u8 status)
{
    char query[256];
    u8 prev;
    vm_mock_service_client_session *session;

    if (g_vm_mock_arena_rt.sessionId == 0 || g_vm_mock_arena_rt.status == status)
        return;
    prev = g_vm_mock_arena_rt.status;

    g_vm_mock_arena_rt.status = status;
    snprintf(query, sizeof(query),
             "UPDATE server_arena_sessions SET status=%u WHERE session_id=%u",
             (u32)status, g_vm_mock_arena_rt.sessionId);
    (void)vm_mysql_exec(query);
    printf("[info][mock-service] arena_status session=%u status=%u->%u\n",
           g_vm_mock_arena_rt.sessionId, (u32)prev, (u32)status);
    if (status != VM_MOCK_ARENA_STATUS_SETTLED)
    {
        session = g_vm_mock_service_client_sessions;
        while (session != NULL)
        {
            session->arenaHudSettlePushed = false;
            session = session->next;
        }
    }
    /* Each new entry window starts from 0; same-day SETTLED reopen must not
     * carry last match's score/wins/claimed. LIVE from SETTLED skips ENTRY. */
    if (status == VM_MOCK_ARENA_STATUS_ENTRY &&
        prev != VM_MOCK_ARENA_STATUS_ENTRY)
    {
        vm_mock_arena_reset_session_scores("entry-begin");
        vm_mock_arena_evict_offline_from_venue("entry-begin");
    }
    else if (status == VM_MOCK_ARENA_STATUS_LIVE &&
             prev == VM_MOCK_ARENA_STATUS_SETTLED)
    {
        vm_mock_arena_reset_session_scores("settled-reopen-live");
        vm_mock_arena_evict_offline_from_venue("settled-reopen-live");
    }
    /* Fresh LIVE window must not inherit prior lastMatchRound skips. */
    if (status == VM_MOCK_ARENA_STATUS_LIVE && prev != VM_MOCK_ARENA_STATUS_LIVE)
    {
        g_vm_mock_arena_rt.lastMatchRound = 0xffffffffu;
        g_vm_mock_arena_deferred_unmatched = 0;
        g_vm_mock_arena_playoff = false;
        g_vm_mock_arena_playoff_announced = false;
        /* 新比赛区间：连杀档位首杀播报重新计数；清比武文字称号。 */
        g_vm_mock_arena_kill_streak_announced_mask = 0;
        vm_mock_arena_clear_kill_streak_designations("live-begin");
    }
    /* Re-open after SETTLED (admin moved minutes back into window). */
    if ((status == VM_MOCK_ARENA_STATUS_ENTRY ||
         status == VM_MOCK_ARENA_STATUS_LIVE) &&
        prev == VM_MOCK_ARENA_STATUS_SETTLED)
    {
        g_vm_mock_arena_announced_status = 0xffu;
        g_vm_mock_arena_rt.lastMatchRound = 0xffffffffu;
        g_vm_mock_arena_playoff = false;
        g_vm_mock_arena_playoff_announced = false;
        g_vm_mock_arena_kill_streak_announced_mask = 0;
        if (status == VM_MOCK_ARENA_STATUS_LIVE)
            vm_mock_arena_clear_kill_streak_designations("settled-reopen-live");
    }
    vm_mock_arena_announce_status(status);
    vm_mock_arena_refresh_nearby_for_arena_clients("arena-status-change");
}

/*
 * Drive status from config minutes. Allow SETTLED→ENTRY/LIVE when admin
 * reopens the same-day window; otherwise phase tips never fire again.
 * Clock past matchEnd: try settle, or enter/keep playoff while same
 * job×bracket×score ties remain among online venue seats.
 */
static void vm_mock_arena_reconcile_status_from_desired(u32 minute)
{
    u8 desired;

    if (g_vm_mock_arena_rt.sessionId == 0)
        return;
    desired = vm_mock_arena_desired_status(minute);
    if (desired == VM_MOCK_ARENA_STATUS_ENTRY &&
        g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_ENTRY)
        vm_mock_arena_set_status(VM_MOCK_ARENA_STATUS_ENTRY);
    else if (desired == VM_MOCK_ARENA_STATUS_LIVE &&
             g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE)
        vm_mock_arena_set_status(VM_MOCK_ARENA_STATUS_LIVE);
    else if (desired == VM_MOCK_ARENA_STATUS_SETTLED &&
             g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_SETTLED)
        vm_mock_arena_try_settle_or_playoff();
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
    u8 titleJob;

    if (p == NULL || place < 1 || place > 3)
        return;
    title = vm_mock_arena_place_title_gbk(place);
    /* FFA shares one title slot (job=0 × bracket 77). */
    titleJob = vm_mock_arena_is_ffa_bracket(p->bracket) ? 0 : p->job;
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
             titleJob, p->bracket, (u32)place, p->roleId, accountEsc, nameHex,
             g_vm_mock_arena_rt.sessionId, p->roleId, accountEsc, nameHex,
             g_vm_mock_arena_rt.sessionId);
    (void)vm_mysql_exec(query);
    session = vm_mock_service_find_client_session(p->clientId);
    if (session != NULL && title[0] != 0)
        snprintf(session->onlineRoleTitle, sizeof(session->onlineRoleTitle), "%s", title);
}

/* Force-teardown in-flight arena duels at settle — do not add score. */
static void vm_mock_arena_abort_active_duels_on_settle(void)
{
    u32 i;
    u32 n = 0;

    for (i = 0; i < VM_MOCK_SERVICE_DUEL_MAX; ++i)
    {
        vm_mock_service_duel *duel = &g_vm_mock_service_duels[i];

        if (!duel->active || !duel->arenaMode || duel->finished)
            continue;
        duel->arenaSettled = true;
        duel->finished = true;
        duel->terminalPendingMask = duel->startedMask ? duel->startedMask : 3u;
        duel->terminalNotBeforeTick = g_schedulerTick;
        vm_mock_service_duel_arm_both_exits(duel);
        ++n;
    }
    if (n != 0)
        printf("[info][mock-service] arena_settle_abort_duels count=%u "
               "session=%u evidence=settled-with-active-fights\n",
               n, g_vm_mock_arena_rt.sessionId);
}

static void vm_mock_arena_settle(void)
{
    u32 i;
    u32 j;

    if (g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_SETTLED)
        return;
    g_vm_mock_arena_playoff = false;
    g_vm_mock_arena_playoff_announced = false;
    vm_mock_arena_abort_active_duels_on_settle();
    vm_mock_arena_set_status(VM_MOCK_ARENA_STATUS_SETTLED);
    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
        g_vm_mock_arena_parts[i].rankPos = 0;
    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        vm_mock_arena_participant bucket[VM_MOCK_ARENA_PARTICIPANT_MAX];
        u32 bucketCount = 0;
        u8 job = g_vm_mock_arena_parts[i].job;
        u16 bracket = g_vm_mock_arena_parts[i].bracket;
        bool ffa = vm_mock_arena_is_ffa_bracket(bracket);
        bool seen = false;

        if (bracket == 0)
            continue;
        if (!ffa && job == 0)
            continue;
        for (j = 0; j < i; ++j)
        {
            if (g_vm_mock_arena_parts[j].bracket != bracket)
                continue;
            if (ffa || g_vm_mock_arena_parts[j].job == job)
            {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        for (j = 0; j < g_vm_mock_arena_part_count; ++j)
        {
            if (g_vm_mock_arena_parts[j].bracket != bracket ||
                g_vm_mock_arena_parts[j].score == 0)
                continue;
            if (!ffa && g_vm_mock_arena_parts[j].job != job)
                continue;
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
    if (a != NULL && b != NULL)
    {
        a->lastOpponentRoleId = b->roleId;
        b->lastOpponentRoleId = a->roleId;
    }
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
        /* b 胜且未死；平局分支不计连杀。 */
        vm_mock_arena_apply_kill_streak(b, a);
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
        vm_mock_arena_apply_kill_streak(a, b);
    }
}

static void vm_mock_arena_shuffle_u32(u32 *idx, u32 n)
{
    u32 j;
    if (idx == NULL || n < 2)
        return;
    for (j = n; j > 1; --j)
    {
        u32 k = vm_mock_arena_rng_below(j);
        u32 tmp = idx[j - 1];
        idx[j - 1] = idx[k];
        idx[k] = tmp;
    }
}

static u32 vm_mock_arena_active_fight_count(void)
{
    u32 n = 0;
    u32 i;
    for (i = 0; i < VM_MOCK_SERVICE_DUEL_MAX; ++i)
    {
        vm_mock_service_duel *duel = &g_vm_mock_service_duels[i];
        if (duel->active && duel->arenaMode && !duel->finished)
            ++n;
    }
    return n;
}

/* Arena match capacity: min(global free duel slots, arena concurrent remaining). */
static u32 vm_mock_arena_match_slots_available(void)
{
    u32 freeGlobal = vm_mock_service_duel_free_slot_count();
    u32 active = vm_mock_arena_active_fight_count();
    u32 capLeft = (active < (u32)VM_MOCK_ARENA_MAX_CONCURRENT_DUELS)
                      ? ((u32)VM_MOCK_ARENA_MAX_CONCURRENT_DUELS - active)
                      : 0u;
    return freeGlobal < capLeft ? freeGlobal : capLeft;
}

static const char *vm_mock_arena_match_block_reason(void)
{
    if (vm_mock_service_duel_free_slot_count() == 0)
        return "no_slot";
    if (vm_mock_arena_active_fight_count() >= (u32)VM_MOCK_ARENA_MAX_CONCURRENT_DUELS)
        return "arena_cap";
    return "no_slot";
}

static bool vm_mock_arena_part_on_venue_online(vm_mock_arena_participant *p)
{
    vm_mock_service_client_session *session;

    if (p == NULL || p->roleId == 0 || p->score == 0)
        return false;
    session = vm_mock_arena_live_session_for_part(p);
    if (session == NULL || !session->roleOnline)
        return false;
    if (!p->inArenaScene &&
        !vm_mock_arena_is_arena_scene(session->sceneVisibleScene) &&
        !vm_mock_arena_is_arena_scene(session->onlineScene))
        return false;
    p->inArenaScene = 1;
    return true;
}

/* True when ≥2 online venue seats share the same job×bracket×score (>0). */
static bool vm_mock_arena_has_resolvable_score_ties(void)
{
    u32 i;
    u32 j;

    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        vm_mock_arena_participant *a = &g_vm_mock_arena_parts[i];
        u32 sameOnline = 0;

        if (a->score == 0 || a->bracket == 0)
            continue;
        if (!vm_mock_arena_is_ffa_bracket(a->bracket) && a->job == 0)
            continue;
        if (!vm_mock_arena_part_on_venue_online(a))
            continue;
        sameOnline = 1;
        for (j = 0; j < g_vm_mock_arena_part_count; ++j)
        {
            vm_mock_arena_participant *b = &g_vm_mock_arena_parts[j];
            if (j == i || !vm_mock_arena_same_score_group(a, b))
                continue;
            if (!vm_mock_arena_part_on_venue_online(b))
                continue;
            sameOnline++;
            if (sameOnline >= 2)
                return true;
        }
    }
    return false;
}

/*
 * Same job×bracket×score group has ≥2 scored seats but only one still on the
 * venue and free: that waiter gets a bye so ranking is not left to roleId.
 */
static u32 vm_mock_arena_grant_playoff_solo_byes(void)
{
    u32 i;
    u32 j;
    u32 n = 0;
    u32 granted = 0;
    u32 roleIds[VM_MOCK_ARENA_PARTICIPANT_MAX];
    u32 roundIndex;

    roundIndex = g_vm_mock_arena_rt.roundIndex ? g_vm_mock_arena_rt.roundIndex : 1u;
    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        vm_mock_arena_participant *seed = &g_vm_mock_arena_parts[i];
        vm_mock_arena_participant *solo = NULL;
        u32 countAll = 0;
        u32 onlineCount = 0;
        bool seen = false;

        if (seed->score == 0 || seed->bracket == 0)
            continue;
        if (!vm_mock_arena_is_ffa_bracket(seed->bracket) && seed->job == 0)
            continue;
        for (j = 0; j < i; ++j)
        {
            vm_mock_arena_participant *prev = &g_vm_mock_arena_parts[j];
            if (vm_mock_arena_same_score_group(prev, seed))
            {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        for (j = 0; j < g_vm_mock_arena_part_count; ++j)
        {
            vm_mock_arena_participant *cand = &g_vm_mock_arena_parts[j];
            if (!vm_mock_arena_same_score_group(cand, seed) || cand->score == 0)
                continue;
            countAll++;
            if (!vm_mock_arena_part_on_venue_online(cand))
                continue;
            onlineCount++;
            solo = cand;
        }
        if (countAll < 2 || onlineCount != 1 || solo == NULL)
            continue;
        if (vm_mock_service_duel_find_for_client(solo->clientId, NULL) != NULL)
            continue;
        roleIds[n++] = solo->roleId;
    }
    for (i = 0; i < n; ++i)
    {
        vm_mock_arena_participant *p = vm_mock_arena_find_part(roleIds[i]);
        if (p == NULL)
            continue;
        p->matchedThisRound = 0;
        vm_mock_arena_grant_bye(p, roundIndex, 1);
        ++granted;
        printf("[info][mock-service] arena_playoff_solo_bye role=%u "
               "job=%u bracket=%u score=%u evidence=only-one-online-in-tie\n",
               p->roleId, (u32)p->job, (u32)p->bracket, p->score);
    }
    return granted;
}

static void vm_mock_arena_announce_playoff(void)
{
    /* 比武加赛：同分需决出胜负！ */
    static const char tipGbk[] =
        "\xb1\xc8\xce\xe4\xbc\xd3\xc8\xfc\xa3\xba\xcd\xac\xb7\xd6\xd0\xe8\xbe\xf6"
        "\xb3\xf6\xca\xa4\xb8\xba\xa3\xa1";
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    vm_mock_service_client_session *session;
    u32 n = 0;

    if (g_vm_mock_arena_playoff_announced)
        return;
    g_vm_mock_arena_playoff_announced = true;
    session = g_vm_mock_service_client_sessions;
    while (session != NULL)
    {
        if (session->clientId != 0 && session->roleOnline &&
            session->onlineRoleId != 0)
        {
            if (vm_mock_service_session_enqueue_system_message(session, tipGbk))
                ++n;
        }
        session = session->next;
    }
    (void)vm_mock_service_broadcast_world_chat_live(systemNameGbk, tipGbk);
    printf("[info][mock-service] arena_playoff_begin session=%u tips=%u "
           "evidence=same-job-bracket-score-tie\n",
           g_vm_mock_arena_rt.sessionId, n);
}

/*
 * Clock hit SETTLED: if same job×bracket players still share score>0 and ≥2
 * are online on the venue, keep LIVE and run playoff duels. Otherwise settle.
 * Wait for in-flight arena duels before ranking.
 */
static void vm_mock_arena_try_settle_or_playoff(void)
{
    if (g_vm_mock_arena_rt.sessionId == 0)
        return;
    if (g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_SETTLED)
        return;
    vm_mock_arena_sync_venue_participants();
    if (vm_mock_arena_active_fight_count() != 0)
    {
        if (!g_vm_mock_arena_playoff && vm_mock_arena_has_resolvable_score_ties())
        {
            /* set_status(LIVE) clears playoff — set flags after status flip. */
            if (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE)
                vm_mock_arena_set_status(VM_MOCK_ARENA_STATUS_LIVE);
            g_vm_mock_arena_playoff = true;
            vm_mock_arena_announce_playoff();
        }
        return;
    }
    if (vm_mock_arena_has_resolvable_score_ties())
    {
        if (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE)
            vm_mock_arena_set_status(VM_MOCK_ARENA_STATUS_LIVE);
        g_vm_mock_arena_playoff = true;
        vm_mock_arena_announce_playoff();
        return;
    }
    /* Tie group still exists but only one seat is on the venue: bye, then
     * re-check (bye may collide with another score group). */
    if (vm_mock_arena_grant_playoff_solo_byes() != 0)
    {
        if (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE)
            vm_mock_arena_set_status(VM_MOCK_ARENA_STATUS_LIVE);
        g_vm_mock_arena_playoff = true;
        if (vm_mock_arena_has_resolvable_score_ties())
        {
            vm_mock_arena_announce_playoff();
            return;
        }
    }
    if (g_vm_mock_arena_playoff)
        printf("[info][mock-service] arena_playoff_done session=%u "
               "evidence=no-resolvable-tie\n",
               g_vm_mock_arena_rt.sessionId);
    g_vm_mock_arena_playoff = false;
    g_vm_mock_arena_playoff_announced = false;
    vm_mock_arena_settle();
}

/* Pair only within a single score-tie group; no bye points (wait for partner). */
static void vm_mock_arena_match_playoff_ties(void)
{
    u32 i;
    u32 j;
    u32 pairs = 0;
    u32 freeSlots;
    u32 startsThisTick = 0;

    if (!g_vm_mock_arena_playoff ||
        g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE)
        return;
    freeSlots = vm_mock_arena_match_slots_available();
    if (freeSlots == 0)
        return;
    vm_mock_arena_sync_venue_participants();
    vm_mock_arena_rng_mix((u32)time(NULL));
    vm_mock_arena_rng_mix(g_vm_mock_arena_rt.sessionId ^ 0xA11CEu);

    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        vm_mock_arena_participant *seed = &g_vm_mock_arena_parts[i];
        vm_mock_arena_participant *pool[VM_MOCK_ARENA_PARTICIPANT_MAX];
        u32 poolCount = 0;
        bool seen = false;
        u32 p;

        if (seed->score == 0 || seed->bracket == 0)
            continue;
        if (!vm_mock_arena_is_ffa_bracket(seed->bracket) && seed->job == 0)
            continue;
        if (!vm_mock_arena_part_on_venue_online(seed))
            continue;
        if (vm_mock_service_duel_find_for_client(seed->clientId, NULL) != NULL)
            continue;
        for (j = 0; j < i; ++j)
        {
            vm_mock_arena_participant *prev = &g_vm_mock_arena_parts[j];
            if (vm_mock_arena_same_score_group(prev, seed) &&
                vm_mock_arena_part_on_venue_online(prev))
            {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        for (j = 0; j < g_vm_mock_arena_part_count; ++j)
        {
            vm_mock_arena_participant *cand = &g_vm_mock_arena_parts[j];
            if (!vm_mock_arena_same_score_group(cand, seed))
                continue;
            if (!vm_mock_arena_part_on_venue_online(cand))
                continue;
            if (vm_mock_service_duel_find_for_client(cand->clientId, NULL) != NULL)
                continue;
            pool[poolCount++] = cand;
        }
        if (poolCount < 2)
            continue;
        for (j = poolCount; j > 1; --j)
        {
            u32 k = vm_mock_arena_rng_below(j);
            vm_mock_arena_participant *tmp = pool[j - 1];
            pool[j - 1] = pool[k];
            pool[k] = tmp;
        }
        for (p = 0; p + 1 < poolCount; p += 2)
        {
            const char *failReason = NULL;
            vm_mock_service_client_session *inv;
            vm_mock_service_client_session *resp;
            vm_mock_service_duel *duel;
            vm_mock_arena_participant *left = pool[p];
            vm_mock_arena_participant *right = pool[p + 1];

            if (vm_mock_arena_match_slots_available() == 0)
                break;
            if (startsThisTick >= (u32)VM_MOCK_ARENA_MAX_STARTS_PER_TICK)
                break;
            inv = vm_mock_arena_live_session_for_part(left);
            resp = vm_mock_arena_live_session_for_part(right);
            if (inv == NULL || resp == NULL)
                continue;
            duel = vm_mock_service_duel_begin(inv, resp, &failReason);
            if (duel == NULL)
            {
                printf("[info][mock-service] arena_playoff_duel_fail a=%u b=%u "
                       "score=%u reason=%s\n",
                       left->roleId, right->roleId, left->score,
                       failReason ? failReason : "unknown");
                if (failReason != NULL && strcmp(failReason, "no_slot") == 0)
                    break;
                continue;
            }
            duel->arenaMode = true;
            duel->arenaSettled = false;
            duel->arenaStartTick = g_schedulerTick;
            left->lastOpponentRoleId = right->roleId;
            right->lastOpponentRoleId = left->roleId;
            ++pairs;
            ++startsThisTick;
            printf("[info][mock-service] arena_playoff_match a=%u b=%u "
                   "score=%u job=%u bracket=%u serial=%u\n",
                   left->roleId, right->roleId, left->score, (u32)left->job,
                   (u32)left->bracket, duel->serial);
        }
    }
    if (pairs != 0)
        printf("[info][mock-service] arena_playoff_round_done pairs=%u "
               "arena_fights=%u/%u\n",
               pairs, vm_mock_arena_active_fight_count(),
               (u32)VM_MOCK_ARENA_MAX_CONCURRENT_DUELS);
}

static void vm_mock_arena_grant_bye(vm_mock_arena_participant *bye, u32 roundIndex,
                                     u32 poolCount)
{
    vm_mock_service_client_session *byeSess;
    u32 byePts;
    if (bye == NULL || bye->matchedThisRound)
        return;
    byeSess = vm_mock_service_find_client_session(bye->clientId);
    byePts = g_vm_mock_arena_config.scoreBye;
    bye->matchedThisRound = 1;
    bye->byes += 1;
    vm_mock_arena_add_score(bye, byePts, "bye");
    printf("[info][mock-service] arena_bye round=%u role=%u pool=%u\n",
           roundIndex, bye->roleId, poolCount);
    if (byeSess != NULL)
    {
        vm_mock_arena_note_result(byeSess, bye, "bye", byePts);
        /* No battle 4/7 for bye — deliver on next scene-sync like map tip
         * (25/12 + 1/3/3). Immediate; do not use duel blank-4/8 delay. */
        byeSess->sparResultMessageArmed = true;
        byeSess->sparResultNotBeforeTick = 0;
        byeSess->sparExitMessageQueued = false;
    }
}

static void vm_mock_arena_match_round(void)
{
    vm_mock_arena_participant *pool[VM_MOCK_ARENA_PARTICIPANT_MAX];
    u32 poolCount = 0;
    u32 i;
    u32 j;
    u32 unixTs = 0;
    u32 minute = 0;
    u32 dayYmd = 0;
    u32 roundIndex;
    u32 elapsed;
    u32 onlineOnVenue = 0;
    u32 matchedPairs = 0;
    u32 byeCount = 0;
    u32 deferredCount = 0;
    u32 startsThisTick = 0;
    bool overflowRetry = false;
    u32 freeSlotsAtStart = 0;
    bool startBudgetExhausted = false;

    vm_mock_arena_wall_clock(&dayYmd, &minute, NULL, &unixTs);
    /* Config minutes own the LIVE window; always repair stale session ts first. */
    if (dayYmd == 0)
        dayYmd = g_vm_mock_arena_rt.dayYmd;
    if (dayYmd != 0)
        vm_mock_arena_sync_schedule_timestamps(dayYmd, unixTs);

    if (minute < g_vm_mock_arena_config.matchStartMinute ||
        minute >= g_vm_mock_arena_config.matchEndMinute)
    {
        printf("[info][mock-service] arena_match_skip_window minute=%u "
               "start=%u end=%u status=%u\n",
               minute, g_vm_mock_arena_config.matchStartMinute,
               g_vm_mock_arena_config.matchEndMinute,
               (u32)g_vm_mock_arena_rt.status);
        return;
    }
    if (g_vm_mock_arena_rt.matchStartTs == 0 || unixTs < g_vm_mock_arena_rt.matchStartTs)
        return;
    if (unixTs >= g_vm_mock_arena_rt.matchEndTs)
    {
        printf("[info][mock-service] arena_match_skip_endts now=%u end=%u "
               "minute=%u (stale-ts?) evidence=resync-or-config\n",
               unixTs, g_vm_mock_arena_rt.matchEndTs, minute);
        return;
    }
    elapsed = unixTs - g_vm_mock_arena_rt.matchStartTs;
    roundIndex = 1u + elapsed / (g_vm_mock_arena_config.roundSeconds
                                     ? g_vm_mock_arena_config.roundSeconds
                                     : 300u);
    overflowRetry = (g_vm_mock_arena_rt.lastMatchRound == roundIndex);
    if (overflowRetry)
    {
        /* No deferred waiters, or no free duel slots: skip heavy re-pool. */
        if (g_vm_mock_arena_deferred_unmatched == 0)
            return;
        if (vm_mock_arena_match_slots_available() == 0)
            return;
    }
    /* Re-bind anyone standing on the venue before pooling (hydrate leaves
     * inArenaScene=0). Without this, round 1 burns empty and tips already LIVE. */
    vm_mock_arena_sync_venue_participants();

    vm_mock_arena_rng_mix((u32)unixTs);
    vm_mock_arena_rng_mix(g_vm_mock_arena_rt.sessionId);
    vm_mock_arena_rng_mix(roundIndex);
    vm_mock_arena_rng_mix(g_vm_mock_arena_part_count * 0x10001u);
    vm_mock_arena_rng_mix(overflowRetry ? 0xA11CEu : 0x51A7u);

    if (!overflowRetry)
    {
        for (i = 0; i < g_vm_mock_arena_part_count; ++i)
            g_vm_mock_arena_parts[i].matchedThisRound = 0;
        g_vm_mock_arena_rt.roundIndex = roundIndex;
        g_vm_mock_arena_rt.roundStartedTs = unixTs;
    }

    freeSlotsAtStart = vm_mock_arena_match_slots_available();

    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        vm_mock_arena_participant *seed = &g_vm_mock_arena_parts[i];
        bool alreadyGrouped = false;

        if (startBudgetExhausted)
            break;
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
            /* Only today's scheduled bracket may fight (other tiers rest). */
            if (cand->bracket == 0 ||
                cand->bracket != vm_mock_arena_active_bracket_today())
                continue;
            sess = vm_mock_arena_live_session_for_part(cand);
            if (sess == NULL || !sess->roleOnline)
                continue;
            /* Must actually be on the arena map — inArenaScene alone is not enough
             * after offline/relogin drifted to returnScene. */
            if (!vm_mock_arena_is_arena_scene(sess->sceneVisibleScene) &&
                !vm_mock_arena_is_arena_scene(sess->onlineScene))
            {
                cand->inArenaScene = 0;
                printf("[info][mock-service] arena_match_skip role=%u scene=%s "
                       "evidence=not-on-venue\n",
                       cand->roleId,
                       sess->sceneVisibleScene[0] ? sess->sceneVisibleScene
                       : (sess->onlineScene[0] ? sess->onlineScene : "-"));
                continue;
            }
            if (!vm_mock_arena_on_expected_venue(sess->sceneVisibleScene,
                                                 sess->onlineScene, cand->job,
                                                 cand->bracket))
            {
                printf("[info][mock-service] arena_match_skip role=%u "
                       "job=%u bracket=%u scene=%s evidence=wrong-venue\n",
                       cand->roleId, (u32)cand->job, (u32)cand->bracket,
                       sess->sceneVisibleScene[0] ? sess->sceneVisibleScene
                       : (sess->onlineScene[0] ? sess->onlineScene : "-"));
                continue;
            }
            /* sceneVisibleReady can lag right after teleport into venue; do not
             * block the whole bracket for one seat still finishing 30/1. */
            if (!sess->sceneVisibleReady)
            {
                printf("[info][mock-service] arena_match_ready_lag role=%u "
                       "client=%08x evidence=match-anyway\n",
                       cand->roleId, sess->clientId);
            }
            if (vm_mock_service_duel_find_for_client(cand->clientId, NULL) != NULL)
                continue;
            pool[poolCount++] = cand;
            ++onlineOnVenue;
        }
        /* Randomize processing order, then pick partners with rematch avoid. */
        for (j = poolCount; j > 1; --j)
        {
            u32 k = vm_mock_arena_rng_below(j);
            vm_mock_arena_participant *tmp = pool[j - 1];
            pool[j - 1] = pool[k];
            pool[k] = tmp;
        }
        for (j = 0; j < poolCount; ++j)
        {
            u32 candIdx[VM_MOCK_ARENA_PARTICIPANT_MAX];
            u32 preferIdx[VM_MOCK_ARENA_PARTICIPANT_MAX];
            u32 orderIdx[VM_MOCK_ARENA_PARTICIPANT_MAX];
            u32 candCount = 0;
            u32 preferCount = 0;
            u32 orderCount = 0;
            u32 t;
            u32 p;
            u32 c;
            vm_mock_arena_participant *left = pool[j];
            if (left == NULL || left->matchedThisRound)
                continue;
            if (vm_mock_arena_match_slots_available() == 0)
            {
                printf("[info][mock-service] arena_match_defer round=%u role=%u "
                       "pool=%u evidence=%s\n",
                       roundIndex, left->roleId, poolCount,
                       vm_mock_arena_match_block_reason());
                break;
            }
            for (t = 0; t < poolCount; ++t)
            {
                vm_mock_arena_participant *other = pool[t];
                if (t == j || other == NULL || other->matchedThisRound)
                    continue;
                candIdx[candCount++] = t;
                if (left->lastOpponentRoleId != 0 &&
                    (other->roleId == left->lastOpponentRoleId ||
                     other->lastOpponentRoleId == left->roleId))
                    continue;
                preferIdx[preferCount++] = t;
            }
            if (candCount == 0)
                continue;
            vm_mock_arena_shuffle_u32(preferIdx, preferCount);
            vm_mock_arena_shuffle_u32(candIdx, candCount);
            for (p = 0; p < preferCount; ++p)
                orderIdx[orderCount++] = preferIdx[p];
            for (c = 0; c < candCount; ++c)
            {
                bool already = false;
                for (p = 0; p < orderCount; ++p)
                {
                    if (orderIdx[p] == candIdx[c])
                    {
                        already = true;
                        break;
                    }
                }
                if (!already)
                    orderIdx[orderCount++] = candIdx[c];
            }
            for (p = 0; p < orderCount; ++p)
            {
                const char *failReason = NULL;
                vm_mock_arena_participant *right = pool[orderIdx[p]];
                vm_mock_service_client_session *inv;
                vm_mock_service_client_session *resp;
                vm_mock_service_duel *duel;
                if (right == NULL || right->matchedThisRound)
                    continue;
                if (vm_mock_arena_match_slots_available() == 0)
                {
                    printf("[info][mock-service] arena_match_duel_fail a=%u b=%u "
                           "round=%u reason=%s\n",
                           left->roleId, right->roleId, roundIndex,
                           vm_mock_arena_match_block_reason());
                    break;
                }
                if (startsThisTick >= (u32)VM_MOCK_ARENA_MAX_STARTS_PER_TICK)
                {
                    printf("[info][mock-service] arena_match_start_throttle "
                           "round=%u started=%u/%u evidence=defer-next-tick\n",
                           roundIndex, startsThisTick,
                           (u32)VM_MOCK_ARENA_MAX_STARTS_PER_TICK);
                    startBudgetExhausted = true;
                    j = poolCount;
                    break;
                }
                inv = vm_mock_arena_live_session_for_part(left);
                resp = vm_mock_arena_live_session_for_part(right);
                if (inv == NULL || resp == NULL)
                {
                    printf("[info][mock-service] arena_match_duel_fail a=%u b=%u "
                           "round=%u reason=session_missing\n",
                           left->roleId, right->roleId, roundIndex);
                    continue;
                }
                duel = vm_mock_service_duel_begin(inv, resp, &failReason);
                if (duel == NULL)
                {
                    printf("[info][mock-service] arena_match_duel_fail a=%u b=%u "
                           "round=%u reason=%s\n",
                           left->roleId, right->roleId, roundIndex,
                           failReason ? failReason : "unknown");
                    if (failReason != NULL && strcmp(failReason, "no_slot") == 0)
                        break;
                    continue;
                }
                duel->arenaMode = true;
                duel->arenaSettled = false;
                duel->arenaStartTick = g_schedulerTick;
                left->matchedThisRound = 1;
                right->matchedThisRound = 1;
                /* Optimistic so a mid-round rematch path also avoids this pair. */
                left->lastOpponentRoleId = right->roleId;
                right->lastOpponentRoleId = left->roleId;
                ++matchedPairs;
                ++startsThisTick;
                printf("[info][mock-service] arena_match round=%u a=%u b=%u serial=%u "
                       "pool=%u avoided_rematch=%u overflow=%u starts_tick=%u "
                       "evidence=rng+retry-partners\n",
                       roundIndex, left->roleId, right->roleId, duel->serial,
                       poolCount, preferCount > 0 ? 1u : 0u,
                       overflowRetry ? 1u : 0u, startsThisTick);
                break;
            }
        }
        {
            u32 unmatchedCount = 0;
            u32 unmatchedIdx = 0;
            for (j = 0; j < poolCount; ++j)
            {
                if (pool[j] != NULL && !pool[j]->matchedThisRound)
                {
                    unmatchedCount++;
                    unmatchedIdx = j;
                }
            }
            if (unmatchedCount == 1)
            {
                vm_mock_arena_grant_bye(pool[unmatchedIdx], roundIndex, poolCount);
                ++byeCount;
            }
            else if (unmatchedCount > 1)
            {
                deferredCount += unmatchedCount;
                /* Log once per new round pass; overflow retries stay quiet. */
                if (!overflowRetry)
                    printf("[info][mock-service] arena_match_defer_bucket round=%u "
                           "job=%u bracket=%u unmatched=%u pool=%u match_slots=%u "
                           "arena_fights=%u/%u evidence=wait-not-bye\n",
                           roundIndex, (u32)seed->job, (u32)seed->bracket,
                           unmatchedCount, poolCount,
                           vm_mock_arena_match_slots_available(),
                           vm_mock_arena_active_fight_count(),
                           (u32)VM_MOCK_ARENA_MAX_CONCURRENT_DUELS);
            }
        }
    }
    g_vm_mock_arena_deferred_unmatched = deferredCount;
    if (overflowRetry && matchedPairs == 0 && byeCount == 0 && deferredCount == 0)
        return;
    printf("[info][mock-service] arena_match_round_done round=%u parts=%u "
           "venue_pool_seats=%u pairs=%u byes=%u deferred=%u match_slots=%u/%u "
           "arena_fights=%u/%u overflow=%u match_ts=%u end_ts=%u\n",
           roundIndex, g_vm_mock_arena_part_count, onlineOnVenue, matchedPairs,
           byeCount, deferredCount, vm_mock_arena_match_slots_available(),
           freeSlotsAtStart, vm_mock_arena_active_fight_count(),
           (u32)VM_MOCK_ARENA_MAX_CONCURRENT_DUELS, overflowRetry ? 1u : 0u,
           g_vm_mock_arena_rt.matchStartTs, g_vm_mock_arena_rt.matchEndTs);
    /*
     * Only consume the round when at least one online venue seat was eligible.
     * Empty pools used to set lastMatchRound first →「比赛开始」后整场空转到
     * 下一 5 分钟轮次 (2026-08-14 session=3 score 全 0).
     * Deferred waiters still consume so matchedThisRound sticks; overflow ticks
     * keep pairing them without resetting the round clock.
     */
    if (onlineOnVenue > 0 || matchedPairs > 0 || byeCount > 0 || deferredCount > 0)
        g_vm_mock_arena_rt.lastMatchRound = roundIndex;
    else
        printf("[info][mock-service] arena_match_round_retry round=%u "
               "evidence=empty-venue-pool-keep-open\n",
               roundIndex);
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
    u32 tickStartMs;
    u32 tickMs;

    (void)vm_mock_arena_load_config(false);
    /* Wager idle-timeout must run even when tournament arena is disabled. */
    vm_mock_wager_tick_expire();
    if (!g_vm_mock_arena_config.enabled)
        return;
    vm_mock_arena_wall_clock(&dayYmd, &minute, &weekdayBit, &unixTs);
    if (unixTs == g_vm_mock_arena_last_tick_sec)
        return;
    g_vm_mock_arena_last_tick_sec = unixTs;
    tickStartMs = scheduler_get_tick_ms();

    /*
     * After SETTLED: skip reconcile/match/duel-timeout under protocol lock
     * (2026-08-19 busy fix). Same calendar day only — cross-day must still
     * ensure_session when the next ENTRY window opens, otherwise status stays
     * SETTLED, HUD has no timer, and matching never starts until admin save
     * calls apply_config_schedule_now (2026-09-05).
     */
    if (g_vm_mock_arena_rt.sessionId != 0 &&
        g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_SETTLED)
    {
        bool sameDay = (dayYmd == 0 || g_vm_mock_arena_rt.dayYmd == dayYmd);
        bool approaching =
            dayYmd != 0 &&
            (g_vm_mock_arena_config.weekdayMask & weekdayBit) != 0 &&
            minute >= g_vm_mock_arena_config.entryStartMinute &&
            minute < g_vm_mock_arena_config.matchEndMinute + 120u;
        if (sameDay || !approaching)
            return;
        printf("[info][mock-service] arena_cross_day_reopen prev_day=%u "
               "today=%u prev_session=%u evidence=settled-skip-bypass\n",
               g_vm_mock_arena_rt.dayYmd, dayYmd, g_vm_mock_arena_rt.sessionId);
    }

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
        vm_mock_arena_sync_schedule_timestamps(dayYmd, unixTs);
        /*
         * Reconcile ENTRY/LIVE/SETTLED from config minutes. Includes
         * SETTLED→ENTRY when admin moves the same-day window forward for
         * retest (old code only allowed LIVE→ENTRY and blocked SETTLED→LIVE).
         */
        vm_mock_arena_reconcile_status_from_desired(minute);
        vm_mock_arena_sync_venue_participants();
        if (g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_LIVE)
        {
            if (g_vm_mock_arena_playoff)
                vm_mock_arena_match_playoff_ties();
            else
                vm_mock_arena_match_round();
            /* After playoff duels end / ties clear, settle on the same tick. */
            if (desired == VM_MOCK_ARENA_STATUS_SETTLED &&
                g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_SETTLED)
                vm_mock_arena_try_settle_or_playoff();
        }
        /* Only on unreannounced phase (rebound / status edge). Late joiners tip
         * on their own HUD/poll path — do not walk all seats every second. */
        if ((g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_ENTRY ||
             g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_LIVE ||
             g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_SETTLED) &&
            g_vm_mock_arena_announced_status != g_vm_mock_arena_rt.status)
        {
            vm_mock_arena_announce_status(g_vm_mock_arena_rt.status);
        }
        /*
         * Once per ENTRY/LIVE bind: force peer resync so stale cross-bracket
         * sprites get 1/2/6 after filter/invalidate (visible=false alone is not
         * enough — see append_stale_peer_remove6).
         */
        {
            static u32 s_arenaFilterRefreshSession = 0;
            static u8 s_arenaFilterRefreshStatus = 0xffu;
            if ((g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_ENTRY ||
                 g_vm_mock_arena_rt.status == VM_MOCK_ARENA_STATUS_LIVE) &&
                (s_arenaFilterRefreshSession != g_vm_mock_arena_rt.sessionId ||
                 s_arenaFilterRefreshStatus != g_vm_mock_arena_rt.status))
            {
                s_arenaFilterRefreshSession = g_vm_mock_arena_rt.sessionId;
                s_arenaFilterRefreshStatus = g_vm_mock_arena_rt.status;
                vm_mock_arena_refresh_nearby_for_arena_clients(
                    "arena-filter-resync");
            }
            if (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_ENTRY &&
                g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_LIVE)
            {
                s_arenaFilterRefreshSession = 0;
                s_arenaFilterRefreshStatus = 0xffu;
            }
        }
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
    tickMs = scheduler_get_tick_ms() - tickStartMs;
    if (tickMs > 50u)
        printf("[warn][mock-service] arena_tick_slow ms=%u status=%u parts=%u "
               "deferred=%u match_slots=%u arena_fights=%u/%u "
               "evidence=protocol-lock-risk\n",
               tickMs, (u32)g_vm_mock_arena_rt.status, g_vm_mock_arena_part_count,
               g_vm_mock_arena_deferred_unmatched,
               vm_mock_arena_match_slots_available(),
               vm_mock_arena_active_fight_count(),
               (u32)VM_MOCK_ARENA_MAX_CONCURRENT_DUELS);
    (void)desired;
}

static vm_mock_arena_participant *vm_mock_arena_ensure_participant(
    vm_mock_service_client_session *session, vm_net_mock_role_state *role)
{
    vm_mock_arena_participant *p;
    u16 bracket;
    u32 roleId;
    u8 job;
    u32 level;
    const char *name;

    if (session == NULL)
        return NULL;
    if (role != NULL && role->roleId != 0)
    {
        roleId = role->roleId;
        job = role->job ? role->job : session->onlineJob;
        level = role->level ? role->level : session->onlineLevel;
        name = role->name[0] ? role->name : session->onlineRoleName;
    }
    else if (session->onlineRoleId != 0)
    {
        roleId = session->onlineRoleId;
        job = session->onlineJob;
        level = session->onlineLevel;
        name = session->onlineRoleName;
    }
    else
        return NULL;
    bracket = vm_mock_arena_enrollment_bracket(level);
    if (bracket == 0)
        return NULL;
    p = vm_mock_arena_find_part(roleId);
    if (p == NULL)
    {
        if (g_vm_mock_arena_part_count >= VM_MOCK_ARENA_PARTICIPANT_MAX)
            return NULL;
        p = &g_vm_mock_arena_parts[g_vm_mock_arena_part_count++];
        memset(p, 0, sizeof(*p));
        p->roleId = roleId;
        snprintf(p->accountId, sizeof(p->accountId), "%s", session->accountId);
        snprintf(p->roleName, sizeof(p->roleName), "%s", name ? name : "");
    }
    p->clientId = session->clientId;
    if (job != 0)
        p->job = job;
    p->bracket = bracket;
    p->level = (u16)level;
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
    u16 activeBracket;
    u16 playerBracket;

    g_vm_mock_arena_enter_refuse_tip[0] = 0;
    (void)vm_mock_arena_tick();
    if (role == NULL || session == NULL || out == NULL)
        return 0;
    activeBracket = vm_mock_arena_active_bracket_today();
    playerBracket = vm_mock_arena_bracket_for_level(role->level);
    if (!vm_mock_arena_entry_window_open() || g_vm_mock_arena_rt.sessionId == 0)
    {
        if (activeBracket == 0)
            vm_mock_arena_set_enter_refuse_tip_bracket(0);
        else
            vm_mock_arena_set_enter_refuse_tip_closed();
        return 0;
    }
    if (role->level < 10 || playerBracket == 0)
    {
        vm_mock_arena_set_enter_refuse_tip_bracket(activeBracket);
        return 0;
    }
    if (!vm_mock_arena_level_matches_active(role->level, activeBracket))
    {
        vm_mock_arena_set_enter_refuse_tip_bracket(activeBracket);
        return 0;
    }
    p = vm_mock_arena_ensure_participant(session, role);
    if (p == NULL)
    {
        vm_mock_arena_set_enter_refuse_tip_closed();
        return 0;
    }
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
    {
        char venue[80];
        u8 job = p->job ? p->job : (role->job ? role->job : 1);
        vm_mock_arena_scene_for_entry(job, p->bracket, venue, sizeof(venue));
        printf("[info][mock-service] arena_enter role=%u job=%u bracket=%u "
               "scene=%s evidence=%s\n",
               role->roleId, (u32)job, (u32)p->bracket, venue,
               vm_mock_arena_is_ffa_bracket(p->bracket) ? "ffa70-venue"
                                                       : "job-venue-day-bracket");
        return vm_net_mock_build_arena_teleport_response(
            out, outCap, venue, g_vm_mock_arena_config.spawnX,
            g_vm_mock_arena_config.spawnY);
    }
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

/* Scene-sync idle fast-path must not starve arena HUD / settle clear. */
static bool vm_mock_arena_poll_needs_body(vm_mock_service_client_session *session)
{
    vm_mock_arena_participant *p;
    const char *scene;
    u8 status;
    const char *tip;

    if (session == NULL || g_vm_mock_arena_rt.sessionId == 0)
        return false;
    status = g_vm_mock_arena_rt.status;
    if (status != VM_MOCK_ARENA_STATUS_ENTRY &&
        status != VM_MOCK_ARENA_STATUS_LIVE &&
        status != VM_MOCK_ARENA_STATUS_SETTLED)
        return false;
    if (status == VM_MOCK_ARENA_STATUS_SETTLED && session->arenaHudSettlePushed)
        return false;
    scene = session->sceneVisibleScene[0] ? session->sceneVisibleScene
                                          : session->onlineScene;
    if (!vm_mock_arena_is_arena_scene(scene) &&
        !vm_mock_arena_is_arena_scene(session->onlineScene))
        return false;
    /* Late joiner / missed edge: tip this seat only (no full-session walk). */
    tip = vm_mock_arena_phase_tip_gbk(status);
    if (tip != NULL)
        vm_mock_arena_deliver_phase_tip_to_session(session, status, tip);
    p = vm_mock_arena_find_part(session->onlineRoleId);
    if (p == NULL)
    {
        /* Standing on venue during ENTRY/LIVE: enroll so HUD/match can run. */
        if (status == VM_MOCK_ARENA_STATUS_ENTRY ||
            status == VM_MOCK_ARENA_STATUS_LIVE)
            p = vm_mock_arena_ensure_participant(session, NULL);
    }
    if (p == NULL)
        return false;
    if (!p->inArenaScene &&
        (vm_mock_arena_is_arena_scene(scene) ||
         vm_mock_arena_is_arena_scene(session->onlineScene)))
        p->inArenaScene = 1;
    return p->inArenaScene != 0;
}

static bool vm_net_mock_append_arena_hud_object(u8 *out, u32 outCap, u32 *pos)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_arena_participant *p;
    u32 objectStart = 0;
    u32 unixTs = 0;
    u32 roundRemain = 0;
    u32 eventRemain = 0;
    u32 roundSec;
    u8 status;

    if (out == NULL || pos == NULL || session == NULL || session->onlineRoleId == 0)
        return false;
    if (g_vm_mock_arena_rt.sessionId == 0)
        return false;
    status = g_vm_mock_arena_rt.status;
    p = vm_mock_arena_find_part(session->onlineRoleId);
    if (p == NULL || !p->inArenaScene)
    {
        /* Self-heal: poll_needs_body / sync_venue may have just enrolled. */
        if (vm_mock_arena_poll_needs_body(session))
            p = vm_mock_arena_find_part(session->onlineRoleId);
    }
    if (p == NULL || !p->inArenaScene)
        return false;
    /* ENTRY/LIVE: live strip. SETTLED: push status=3 so Pixi clears the top tip.
     * Without this, polls stop appending HUD and the last「比赛」strip sticks. */
    if (status != VM_MOCK_ARENA_STATUS_ENTRY &&
        status != VM_MOCK_ARENA_STATUS_LIVE &&
        status != VM_MOCK_ARENA_STATUS_SETTLED)
        return false;
    vm_mock_arena_wall_clock(NULL, NULL, NULL, &unixTs);
    roundSec = g_vm_mock_arena_config.roundSeconds
                   ? g_vm_mock_arena_config.roundSeconds
                   : 300u;
    if (status == VM_MOCK_ARENA_STATUS_LIVE &&
        g_vm_mock_arena_rt.roundStartedTs != 0 &&
        unixTs >= g_vm_mock_arena_rt.roundStartedTs)
    {
        u32 spent = unixTs - g_vm_mock_arena_rt.roundStartedTs;
        roundRemain = spent < roundSec ? (roundSec - spent) : 0;
    }
    if (status == VM_MOCK_ARENA_STATUS_ENTRY || status == VM_MOCK_ARENA_STATUS_LIVE)
    {
        if (unixTs < g_vm_mock_arena_rt.matchEndTs)
            eventRemain = g_vm_mock_arena_rt.matchEndTs - unixTs;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_HUD, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "status", status) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "round",
                                    g_vm_mock_arena_rt.roundIndex) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "roundRemain", roundRemain) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "eventRemain", eventRemain) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "score", p->score) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "job", p->job) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "bracket", p->bracket) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "playoff",
                                    g_vm_mock_arena_playoff ? 1u : 0u))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (status == VM_MOCK_ARENA_STATUS_SETTLED)
        session->arenaHudSettlePushed = true;
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
    u16 activeBracket = 0;
    vm_net_mock_role_state *role;

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ARENA_WT_KIND ||
        object.subtype != VM_MOCK_ARENA_WT_STATUS)
        return 0;
    (void)vm_mock_arena_tick();
    vm_mock_arena_wall_clock(&dayYmd, &minute, &weekdayBit, NULL);
    activeBracket = vm_mock_arena_active_bracket_today();
    role = vm_net_mock_active_role();
    canEnter = 0;
    if (vm_mock_arena_entry_window_open() && activeBracket != 0 && role != NULL)
    {
        if (vm_mock_arena_level_matches_active(role->level, activeBracket))
            canEnter = 1;
    }
    status = g_vm_mock_arena_rt.sessionId ? g_vm_mock_arena_rt.status
                                          : vm_mock_arena_desired_status(minute);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_STATUS, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "enabled",
                                    g_vm_mock_arena_config.enabled) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "status", status) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "canEnter", canEnter) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "activeBracket",
                                    activeBracket) ||
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

static const vm_mock_arena_reward *vm_mock_arena_find_reward(u8 job, u16 bracket,
                                                             u8 rankPos);
static void vm_mock_arena_reward_primary_item(const vm_mock_arena_reward *reward,
                                              char *nameOut, u32 nameCap,
                                              u32 *countOut);
/* Rank UI line: 经验/铜币/酷宝/物品/装备汇总（无领奖前缀）。 */
static void vm_mock_arena_format_reward_summary(const vm_mock_arena_reward *reward,
                                                char *out, u32 outCap);
static bool vm_mock_arena_claim_tip_append(char *out, u32 outCap, u32 *used,
                                           const char *frag);

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
    if (job == 0 && role && !vm_mock_arena_is_ffa_bracket((u16)bracket))
        job = role->job;
    if (bracket == 0 && role)
    {
        bracket = vm_mock_arena_enrollment_bracket(role->level);
        if (bracket == 0)
            bracket = vm_mock_arena_bracket_for_level(role->level);
    }
    /* FFA rank is one cross-job board; ignore requested job. */
    if (vm_mock_arena_is_ffa_bracket((u16)bracket))
        job = 0;
    for (i = 0; i < g_vm_mock_arena_part_count; ++i)
    {
        /* 0 分不进排行榜（含未打过的进场玩家）。 */
        if (g_vm_mock_arena_parts[i].bracket != (u16)bracket ||
            g_vm_mock_arena_parts[i].score == 0)
            continue;
        if (!vm_mock_arena_is_ffa_bracket((u16)bracket) &&
            g_vm_mock_arena_parts[i].job != (u8)job)
            continue;
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
        const vm_mock_arena_reward *rowReward =
            vm_mock_arena_find_reward((u8)job, (u16)bracket, (u8)(i + 1));
        char rewardName[96];
        u32 rewardCount = 0;

        /* Summary for UI; rewardCount kept as primary item qty for old clients. */
        vm_mock_arena_format_reward_summary(rowReward, rewardName,
                                            sizeof(rewardName));
        vm_mock_arena_reward_primary_item(rowReward, NULL, 0, &rewardCount);
        if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, sorted[i].roleId) ||
            !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen,
                                       sorted[i].roleName) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, sorted[i].score) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, i + 1) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, sorted[i].claimed) ||
            !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen, rewardName) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rewardCount))
            break;
        count += 1;
    }
    {
        vm_mock_arena_participant *selfPart =
            role != NULL ? vm_mock_arena_find_part(role->roleId) : NULL;
        const vm_mock_arena_reward *myReward =
            (myRank >= 1 && myRank <= 10)
                ? vm_mock_arena_find_reward((u8)job, (u16)bracket, (u8)myRank)
                : NULL;
        char myRewardName[96];
        u32 myRewardCount = 0;
        u32 myClaimed = 0;

        /* Only mark claimed when viewing own board (FFA ignores job). */
        if (selfPart != NULL && selfPart->claimed &&
            selfPart->bracket == (u16)bracket &&
            (vm_mock_arena_is_ffa_bracket((u16)bracket) ||
             selfPart->job == (u8)job))
            myClaimed = 1;
        vm_mock_arena_format_reward_summary(myReward, myRewardName,
                                            sizeof(myRewardName));
        vm_mock_arena_reward_primary_item(myReward, NULL, 0, &myRewardCount);
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
            !vm_net_mock_put_object_u32(out, outCap, &pos, "myClaimed",
                                        myClaimed) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "myRewardName",
                                           myRewardName) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "myRewardCount",
                                        myRewardCount) ||
            !vm_net_mock_put_object_entry(out, outCap, &pos, "rows", blob,
                                          (u16)blobLen))
            return 0;
    }
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

/* Primary claim item for list UI (normally one of item/equip). */
static void vm_mock_arena_reward_primary_item(const vm_mock_arena_reward *reward,
                                              char *nameOut, u32 nameCap,
                                              u32 *countOut)
{
    const vm_net_mock_shop_catalog_item *shop;
    u32 itemId = 0;
    u32 count = 0;

    if (nameOut != NULL && nameCap > 0)
        nameOut[0] = 0;
    if (countOut != NULL)
        *countOut = 0;
    if (reward == NULL)
        return;
    if (reward->itemId != 0 && reward->itemCount != 0)
    {
        itemId = reward->itemId;
        count = reward->itemCount;
    }
    else if (reward->equipId != 0)
    {
        itemId = reward->equipId;
        count = 1;
    }
    if (itemId == 0 || count == 0)
        return;
    if (countOut != NULL)
        *countOut = count;
    if (nameOut == NULL || nameCap == 0)
        return;
    shop = vm_net_mock_find_shop_catalog_item(itemId);
    if (shop != NULL && shop->name[0] != 0)
        snprintf(nameOut, nameCap, "%s", shop->name);
    else
        snprintf(nameOut, nameCap, "#%u", itemId);
}

/* Rank / self preview: all configured parts, no claim-success prefix. */
static void vm_mock_arena_format_reward_summary(const vm_mock_arena_reward *reward,
                                                char *out, u32 outCap)
{
    static const char sepGbk[] = "\xa1\xa2"; /* 、 */
    char part[64];
    u32 used = 0;
    u32 parts = 0;
    const vm_net_mock_shop_catalog_item *shop;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (reward == NULL)
        return;
    if (reward->rewardExp)
    {
        snprintf(part, sizeof(part), "\xbe\xad\xd1\xe9%u", reward->rewardExp);
        if (vm_mock_arena_claim_tip_append(out, outCap, &used, part))
            ++parts;
    }
    if (reward->rewardMoney)
    {
        if (parts && !vm_mock_arena_claim_tip_append(out, outCap, &used, sepGbk))
            return;
        snprintf(part, sizeof(part), "\xbd\xf0\xc7\xae%u", reward->rewardMoney);
        if (vm_mock_arena_claim_tip_append(out, outCap, &used, part))
            ++parts;
    }
    if (reward->rewardWcoin)
    {
        if (parts && !vm_mock_arena_claim_tip_append(out, outCap, &used, sepGbk))
            return;
        snprintf(part, sizeof(part), "\xbf\xe1\xb1\xa6%u", reward->rewardWcoin);
        if (vm_mock_arena_claim_tip_append(out, outCap, &used, part))
            ++parts;
    }
    if (reward->itemId && reward->itemCount)
    {
        if (parts && !vm_mock_arena_claim_tip_append(out, outCap, &used, sepGbk))
            return;
        shop = vm_net_mock_find_shop_catalog_item(reward->itemId);
        if (shop != NULL && shop->name[0] != 0)
            snprintf(part, sizeof(part), "%sx%u", shop->name, reward->itemCount);
        else
            snprintf(part, sizeof(part), "#%ux%u", reward->itemId,
                     reward->itemCount);
        if (!vm_mock_arena_claim_tip_append(out, outCap, &used, part))
        {
            snprintf(part, sizeof(part), "#%ux%u", reward->itemId,
                     reward->itemCount);
            (void)vm_mock_arena_claim_tip_append(out, outCap, &used, part);
        }
        else
            ++parts;
    }
    if (reward->equipId)
    {
        if (parts && !vm_mock_arena_claim_tip_append(out, outCap, &used, sepGbk))
            return;
        shop = vm_net_mock_find_shop_catalog_item(reward->equipId);
        if (shop != NULL && shop->name[0] != 0)
            snprintf(part, sizeof(part), "%s", shop->name);
        else
            snprintf(part, sizeof(part), "#%u", reward->equipId);
        if (!vm_mock_arena_claim_tip_append(out, outCap, &used, part))
        {
            snprintf(part, sizeof(part), "#%u", reward->equipId);
            (void)vm_mock_arena_claim_tip_append(out, outCap, &used, part);
        }
    }
}

/* GBK chat/wire tip ≤81 bytes: 比武领奖成功：经验N、金钱N、酷宝N、物品… */
static bool vm_mock_arena_claim_tip_append(char *out, u32 outCap, u32 *used,
                                           const char *frag)
{
    size_t n;

    if (out == NULL || used == NULL || frag == NULL || outCap == 0)
        return false;
    n = strlen(frag);
    if (n == 0)
        return true;
    if (*used + n + 1u > outCap)
        return false;
    memcpy(out + *used, frag, n);
    *used += (u32)n;
    out[*used] = 0;
    return true;
}

static void vm_mock_arena_format_claim_tip(const vm_mock_arena_reward *reward,
                                           char *out, u32 outCap)
{
    /* 比武领奖成功： */
    static const char prefixGbk[] =
        "\xb1\xc8\xce\xe4\xc1\xec\xbd\xb1\xb3\xc9\xb9\xa6\xa3\xba";
    static const char sepGbk[] = "\xa1\xa2"; /* 、 */
    char part[64];
    u32 used = 0;
    u32 parts = 0;
    const vm_net_mock_shop_catalog_item *shop;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (reward == NULL)
        return;
    if (!vm_mock_arena_claim_tip_append(out, outCap, &used, prefixGbk))
        return;
    if (reward->rewardExp)
    {
        snprintf(part, sizeof(part), "\xbe\xad\xd1\xe9%u", reward->rewardExp);
        if (vm_mock_arena_claim_tip_append(out, outCap, &used, part))
            ++parts;
    }
    if (reward->rewardMoney)
    {
        if (parts && !vm_mock_arena_claim_tip_append(out, outCap, &used, sepGbk))
            return;
        snprintf(part, sizeof(part), "\xbd\xf0\xc7\xae%u", reward->rewardMoney);
        if (vm_mock_arena_claim_tip_append(out, outCap, &used, part))
            ++parts;
    }
    if (reward->rewardWcoin)
    {
        if (parts && !vm_mock_arena_claim_tip_append(out, outCap, &used, sepGbk))
            return;
        snprintf(part, sizeof(part), "\xbf\xe1\xb1\xa6%u", reward->rewardWcoin);
        if (vm_mock_arena_claim_tip_append(out, outCap, &used, part))
            ++parts;
    }
    if (reward->itemId && reward->itemCount)
    {
        if (parts && !vm_mock_arena_claim_tip_append(out, outCap, &used, sepGbk))
            return;
        shop = vm_net_mock_find_shop_catalog_item(reward->itemId);
        if (shop != NULL && shop->name[0] != 0)
            snprintf(part, sizeof(part), "%sx%u", shop->name, reward->itemCount);
        else
            snprintf(part, sizeof(part), "#%ux%u", reward->itemId,
                     reward->itemCount);
        if (!vm_mock_arena_claim_tip_append(out, outCap, &used, part))
        {
            /* Name too long: fall back to id. */
            snprintf(part, sizeof(part), "#%ux%u", reward->itemId,
                     reward->itemCount);
            (void)vm_mock_arena_claim_tip_append(out, outCap, &used, part);
        }
        else
            ++parts;
    }
    if (reward->equipId)
    {
        if (parts && !vm_mock_arena_claim_tip_append(out, outCap, &used, sepGbk))
            return;
        shop = vm_net_mock_find_shop_catalog_item(reward->equipId);
        if (shop != NULL && shop->name[0] != 0)
            snprintf(part, sizeof(part), "%s", shop->name);
        else
            snprintf(part, sizeof(part), "#%u", reward->equipId);
        if (!vm_mock_arena_claim_tip_append(out, outCap, &used, part))
        {
            snprintf(part, sizeof(part), "#%u", reward->equipId);
            (void)vm_mock_arena_claim_tip_append(out, outCap, &used, part);
        }
    }
    if (parts == 0 && reward->itemId == 0 && reward->equipId == 0)
    {
        /* 无配置明细时仍给固定成功提示。 */
        static const char emptyGbk[] =
            "\xb1\xc8\xce\xe4\xc1\xec\xbd\xb1\xb3\xc9\xb9\xa6";
        snprintf(out, outCap, "%s", emptyGbk);
    }
}

static u32 vm_net_mock_build_arena_claim_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_arena_participant *p;
    const vm_mock_arena_reward *reward = NULL;
    u32 result = 0;
    u16 seq = 0;
    char tipGbk[82];

    tipGbk[0] = 0;
    if (out == NULL || outCap < 5 || request == NULL || role == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_ARENA_WT_KIND ||
        object.subtype != VM_MOCK_ARENA_WT_CLAIM)
        return 0;
    p = vm_mock_arena_find_part(role->roleId);
    if (p == NULL)
    {
        result = 2;
        /* 未参赛，无法领奖 */
        snprintf(tipGbk, sizeof(tipGbk),
                 "\xce\xb4\xb2\xce\xc8\xfc\xa3\xac\xce\xde\xb7\xa8\xc1\xec\xbd\xb1");
    }
    else if (g_vm_mock_arena_rt.status != VM_MOCK_ARENA_STATUS_SETTLED)
    {
        result = 2;
        /* 比赛尚未结束 */
        snprintf(tipGbk, sizeof(tipGbk),
                 "\xb1\xc8\xc8\xfc\xc9\xd0\xce\xb4\xbd\xe1\xca\xf8");
    }
    else if (p->claimed)
    {
        result = 3;
        /* 奖励已领取 */
        snprintf(tipGbk, sizeof(tipGbk),
                 "\xbd\xb1\xc0\xf8\xd2\xd1\xc1\xec\xc8\xa1");
    }
    else if (p->rankPos == 0 || p->rankPos > 5)
    {
        result = 4;
        /* 未进入前五名 */
        snprintf(tipGbk, sizeof(tipGbk),
                 "\xce\xb4\xbd\xf8\xc8\xeb\xc7\xb0\xce\xe5\xc3\xfb");
    }
    else
    {
        reward = vm_mock_arena_is_ffa_bracket(p->bracket)
                     ? vm_mock_arena_find_reward(0, p->bracket, (u8)p->rankPos)
                     : vm_mock_arena_find_reward(p->job, p->bracket,
                                                  (u8)p->rankPos);
        if (reward == NULL)
        {
            result = 5;
            /* 该名次未配置奖励 */
            snprintf(tipGbk, sizeof(tipGbk),
                     "\xb8\xc3\xc3\xfb\xb4\xce\xce\xb4\xc5\xe4\xd6\xc3\xbd\xb1\xc0\xf8");
        }
        else
        {
            bool bagOk = true;
            vm_net_mock_role_state beforeBag;
            bool wasInventoryDirty = false;

            /*
             * Grant bag rows first. If item/equip cannot enter, refuse the
             * whole claim (no exp/money/wcoin, claimed stays 0) so a full
             * backpack cannot burn the reward.
             */
            if ((reward->itemId && reward->itemCount) || reward->equipId)
            {
                wasInventoryDirty = g_vm_net_mock_role_inventory_dirty;
                beforeBag = *role;
                if (reward->itemId && reward->itemCount &&
                    !vm_net_mock_role_add_backpack_item_to_role(
                        role, reward->itemId, reward->itemCount, 0, &seq,
                        "arena-claim-item", NULL, 0))
                    bagOk = false;
                if (bagOk && reward->equipId &&
                    !vm_net_mock_role_add_backpack_item_to_role(
                        role, reward->equipId, 1, 0, &seq, "arena-claim-equip",
                        NULL, 0))
                    bagOk = false;
                if (!bagOk)
                {
                    *role = beforeBag;
                    g_vm_net_mock_role_inventory_dirty = wasInventoryDirty;
                    result = 6;
                    /* 背包已满，请清理后再领奖 */
                    snprintf(tipGbk, sizeof(tipGbk),
                             "\xb1\xb3\xb0\xfc\xd2\xd1\xc2\xfa\xa3\xac"
                             "\xc7\xeb\xc7\xe5\xc0\xed\xba\xf3\xd4\xd9"
                             "\xc1\xec\xbd\xb1");
                    printf("[info][mock-service] arena_claim_bag_full "
                           "role=%u rank=%u item=%u x%u equip=%u\n",
                           role->roleId, (u32)p->rankPos, reward->itemId,
                           reward->itemCount, reward->equipId);
                }
            }

            if (bagOk)
            {
                if (reward->rewardExp)
                    (void)vm_net_mock_role_add_exp(role, reward->rewardExp);
                if (reward->rewardMoney)
                {
                    role->money = vm_net_mock_add_capped_u32(
                        role->money, reward->rewardMoney);
                    vm_net_mock_role_mark_inventory_dirty("arena-claim-money");
                }
                if (reward->rewardWcoin)
                    (void)vm_net_mock_role_add_wcoin(role, reward->rewardWcoin);
                p->claimed = 1;
                (void)vm_mock_arena_persist_score(p);
                result = 1;
                vm_mock_arena_format_claim_tip(reward, tipGbk, sizeof(tipGbk));
                if (tipGbk[0] != 0 && session != NULL)
                {
                    (void)vm_mock_service_session_enqueue_system_message(
                        session, tipGbk);
                    printf("[info][mock-service] arena_claim_tip role=%u "
                           "rank=%u exp=%u money=%u wcoin=%u item=%u x%u "
                           "equip=%u tip=%s\n",
                           role->roleId, (u32)p->rankPos, reward->rewardExp,
                           reward->rewardMoney, reward->rewardWcoin,
                           reward->itemId, reward->itemCount, reward->equipId,
                           tipGbk);
                }
            }
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
    if (tipGbk[0] != 0 &&
        !vm_net_mock_put_object_string(out, outCap, &pos, "tip", tipGbk))
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
    const u16 weekdayBrackets[7], u16 entryStart, u16 entryEnd, u16 matchStart,
    u16 matchEnd, u32 roundSec, u32 battleTimeout, u32 scoreWin, u32 scoreBye,
    u32 scoreDraw, const char *npcKeyword, u8 wagerEnabled, u8 wagerFee,
    u8 wagerCurrency, u8 damageScaleEnabled, u8 allowHeal, u8 allowDodge,
    u8 allowCombatPill)
{
    char sceneHex[192];
    char kwHex[96];
    char bracketsText[48];
    char query[2600];
    int written;
    vm_mock_arena_config tmpCfg;
    u32 i;

    if (!vm_mock_arena_ensure_tables() || scene == NULL || scene[0] == 0)
        return false;
    if (vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) == 0)
        return false;
    if (npcKeyword == NULL)
        npcKeyword = "";
    if (vm_mysql_hex_encode(npcKeyword, strlen(npcKeyword), kwHex, sizeof(kwHex)) == 0)
        return false;
    memset(&tmpCfg, 0, sizeof(tmpCfg));
    if (weekdayBrackets != NULL)
    {
        for (i = 0; i < 7u; ++i)
            tmpCfg.weekdayBrackets[i] = weekdayBrackets[i];
    }
    vm_mock_arena_sync_weekday_mask(&tmpCfg);
    if (tmpCfg.weekdayMask == 0 && weekdayMask != 0)
        tmpCfg.weekdayMask = weekdayMask;
    else
        weekdayMask = tmpCfg.weekdayMask;
    vm_mock_arena_format_weekday_brackets(&tmpCfg, bracketsText,
                                          sizeof(bracketsText));
    written = snprintf(
        query, sizeof(query),
        "INSERT INTO server_arena_config (id,enabled,scene,spawn_x,spawn_y,"
        "weekday_mask,weekday_brackets,entry_start_minute,entry_end_minute,"
        "match_start_minute,match_end_minute,round_seconds,battle_timeout_seconds,"
        "score_win,score_bye,score_draw,npc_name_keyword,wager_enabled,"
        "wager_fee_percent,wager_currency,damage_scale_enabled,allow_heal,"
        "allow_dodge,allow_combat_pill) VALUES (1,%u,UNHEX('%s'),%u,%u,%u,"
        "'%s',%u,%u,%u,%u,%u,%u,%u,%u,%u,UNHEX('%s'),%u,%u,%u,%u,%u,%u,%u) "
        "ON DUPLICATE KEY UPDATE "
        "enabled=VALUES(enabled),scene=VALUES(scene),spawn_x=VALUES(spawn_x),"
        "spawn_y=VALUES(spawn_y),weekday_mask=VALUES(weekday_mask),"
        "weekday_brackets=VALUES(weekday_brackets),"
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
        "wager_currency=VALUES(wager_currency),"
        "damage_scale_enabled=VALUES(damage_scale_enabled),"
        "allow_heal=VALUES(allow_heal),allow_dodge=VALUES(allow_dodge),"
        "allow_combat_pill=VALUES(allow_combat_pill)",
        (u32)enabled, sceneHex, (u32)spawnX, (u32)spawnY, weekdayMask,
        bracketsText, (u32)entryStart, (u32)entryEnd, (u32)matchStart,
        (u32)matchEnd, roundSec, battleTimeout, scoreWin, scoreBye, scoreDraw,
        kwHex, (u32)wagerEnabled, (u32)wagerFee, (u32)wagerCurrency,
        (u32)(damageScaleEnabled ? 1u : 0u), (u32)(allowHeal ? 1u : 0u),
        (u32)(allowDodge ? 1u : 0u), (u32)(allowCombatPill ? 1u : 0u));
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
    if (!vm_mock_arena_load_config(true))
        return false;
    /* Session match_*_ts always follow saved minutes — never leave stale rows. */
    vm_mock_arena_apply_config_schedule_now();
    return true;
}

static bool vm_net_mock_arena_admin_save_reward(u8 job, u16 bracket, u8 rankPos,
                                                u32 exp, u32 money, u32 wcoin,
                                                u32 itemId, u32 itemCount,
                                                u32 equipId)
{
    char query[512];
    if (!vm_mock_arena_ensure_tables() ||
        (job == 0 && !vm_mock_arena_is_ffa_bracket(bracket)) ||
        (job != 0 && job > 3) ||
        (bracket != 10 && bracket != 50 && bracket != 70 &&
         !vm_mock_arena_is_ffa_bracket(bracket)) ||
        rankPos == 0 || rankPos > 5)
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
