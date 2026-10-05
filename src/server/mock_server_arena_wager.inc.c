/*
 * PixiJS_web 赏金擂台 (wager): solo or team (max 3, equal sides), per-member
 * stake, host-side reconfirm, ordered pair duels, pot split after fee.
 * Included from mock_server_arena.c.
 */

enum
{
    VM_MOCK_ARENA_WT_WAGER_LIST = 10,
    VM_MOCK_ARENA_WT_WAGER_CREATE = 11,
    VM_MOCK_ARENA_WT_WAGER_CANCEL = 12,
    VM_MOCK_ARENA_WT_WAGER_CHALLENGE = 13,
    VM_MOCK_ARENA_WT_WAGER_CONFIRM = 14,
    VM_MOCK_ARENA_WT_WAGER_EVENT = 15,
    VM_MOCK_ARENA_WT_WAGER_MINE = 16,
    VM_MOCK_WAGER_ST_OPEN = 0,
    VM_MOCK_WAGER_ST_AWAIT = 1,
    VM_MOCK_WAGER_ST_FIGHT = 2,
    VM_MOCK_WAGER_ST_DONE = 3,
    VM_MOCK_WAGER_ST_CANCEL = 4,
    VM_MOCK_WAGER_SIDE_HOST = 0,
    VM_MOCK_WAGER_SIDE_CHALL = 1,
    VM_MOCK_WAGER_MEMBER_MAX = 3,
    VM_MOCK_WAGER_CHALLENGE_MAX = 64,
    VM_MOCK_WAGER_NOTICE_MAX = 48,
    VM_MOCK_WAGER_EVT_AWAIT = 1,
    VM_MOCK_WAGER_EVT_FIGHT = 2,
    VM_MOCK_WAGER_EVT_SETTLE = 3,
    VM_MOCK_WAGER_EVT_REJECT = 4,
    VM_MOCK_WAGER_EVT_CANCEL = 5,
    /* 1金=10000铜；最低押金默认 100金（后台 wager_min_stake_gold 可改） */
    VM_MOCK_WAGER_COPPER_PER_GOLD = 10000u,
    VM_MOCK_WAGER_DEFAULT_MIN_STAKE_GOLD = 100u,
    VM_MOCK_WAGER_MIN_STAKE_COPPER =
        VM_MOCK_WAGER_DEFAULT_MIN_STAKE_GOLD * VM_MOCK_WAGER_COPPER_PER_GOLD,
    /* OPEN 无人应战 / AWAIT 无人确认或拒绝：10 分钟空闲自动撤擂 */
    VM_MOCK_WAGER_IDLE_TIMEOUT_SEC = 600u,
    VM_MOCK_WAGER_STREAK_ANNOUNCE = 3,
    VM_MOCK_WAGER_STREAK_TRACK_MAX = 128
};

typedef struct
{
    u32 roleId;
    u32 clientId;
    char accountId[64];
    char name[32];
    u8 job;   /* 1..3 */
    u16 level;
    u8 paid;
    u8 accepted;
} vm_mock_wager_member;

typedef struct
{
    u32 challengeId;
    u8 status;
    u32 stake;
    u8 feePercent;
    char scene[64];
    u32 hostLeaderRoleId;
    u32 challLeaderRoleId;
    u8 hostCount;
    u8 challCount;
    vm_mock_wager_member host[VM_MOCK_WAGER_MEMBER_MAX];
    vm_mock_wager_member chall[VM_MOCK_WAGER_MEMBER_MAX];
    u8 pairIndex;
    u8 hostPairWins;
    u8 challPairWins;
    u8 active;
    /* Wall-clock unix sec of last create / challenge / reject / confirm. */
    u32 idleSinceUnix;
} vm_mock_wager_challenge;

typedef struct
{
    u32 clientId;
    u32 challengeId;
    u8 eventKind;
    u32 stake;
    u32 payout;
    u8 result; /* 0 draw/info, 1 win, 2 lose */
} vm_mock_wager_notice;

static vm_mock_wager_challenge g_vm_mock_wagers[VM_MOCK_WAGER_CHALLENGE_MAX];
static u32 g_vm_mock_wager_count = 0;
static vm_mock_wager_notice g_vm_mock_wager_notices[VM_MOCK_WAGER_NOTICE_MAX];
static u32 g_vm_mock_wager_notice_count = 0;

typedef struct
{
    u32 roleId;
    u32 streak;
} vm_mock_wager_streak_entry;

static vm_mock_wager_streak_entry
    g_vm_mock_wager_streaks[VM_MOCK_WAGER_STREAK_TRACK_MAX];
static u32 g_vm_mock_wager_streak_count = 0;

static u32 *vm_mock_wager_streak_slot(u32 roleId)
{
    u32 i;
    if (roleId == 0)
        return NULL;
    for (i = 0; i < g_vm_mock_wager_streak_count; ++i)
    {
        if (g_vm_mock_wager_streaks[i].roleId == roleId)
            return &g_vm_mock_wager_streaks[i].streak;
    }
    if (g_vm_mock_wager_streak_count >= VM_MOCK_WAGER_STREAK_TRACK_MAX)
    {
        memmove(&g_vm_mock_wager_streaks[0], &g_vm_mock_wager_streaks[1],
                sizeof(g_vm_mock_wager_streaks[0]) *
                    (VM_MOCK_WAGER_STREAK_TRACK_MAX - 1));
        g_vm_mock_wager_streak_count = VM_MOCK_WAGER_STREAK_TRACK_MAX - 1;
    }
    g_vm_mock_wager_streaks[g_vm_mock_wager_streak_count].roleId = roleId;
    g_vm_mock_wager_streaks[g_vm_mock_wager_streak_count].streak = 0;
    return &g_vm_mock_wager_streaks[g_vm_mock_wager_streak_count++].streak;
}

static void vm_mock_wager_streak_set(u32 roleId, u32 streak)
{
    u32 *slot = vm_mock_wager_streak_slot(roleId);
    if (slot)
        *slot = streak;
}

static u32 vm_mock_wager_streak_get(u32 roleId)
{
    u32 *slot = vm_mock_wager_streak_slot(roleId);
    return slot ? *slot : 0;
}

static void vm_mock_wager_announce_streak(const char *playerName, u32 streak)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    char message[82];
    int wrote;
    u32 recipients;

    if (playerName == NULL || playerName[0] == 0 ||
        streak < VM_MOCK_WAGER_STREAK_ANNOUNCE)
        return;
    /* 恭喜{玩家}在赏金擂台连胜{N}场，威震江湖！ */
    memset(message, 0, sizeof(message));
    wrote = snprintf(
        message, sizeof(message),
        "\xB9\xA7\xCF\xB2%s\xD4\xDA\xC9\xCD\xBD\xF0\xC0\xDE\xCC\xA8\xC1\xAC\xCA\xA4"
        "%u\xB3\xA1\xA3\xAC\xCD\xFE\xD5\xF0\xBD\xAD\xBA\xFE\xA3\xA1",
        playerName, streak);
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
    {
        memset(message, 0, sizeof(message));
        wrote = snprintf(message, sizeof(message),
                         "\xB9\xA7\xCF\xB2%s\xC9\xCD\xBD\xF0\xC1\xAC\xCA\xA4%u"
                         "\xB3\xA1\xA3\xA1",
                         playerName, streak);
    }
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
        return;
    recipients = vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][mock-service] wager_streak_announce player=%s streak=%u "
           "recipients=%u\n",
           playerName, streak, recipients);
}

static void vm_mock_wager_apply_streaks(vm_mock_wager_challenge *ch, int winnerSide)
{
    u8 i;
    const vm_mock_wager_member *winners;
    const vm_mock_wager_member *losers;
    u8 winCount;
    u8 loseCount;

    if (ch == NULL)
        return;
    if (winnerSide < 0)
    {
        for (i = 0; i < ch->hostCount; ++i)
            vm_mock_wager_streak_set(ch->host[i].roleId, 0);
        for (i = 0; i < ch->challCount; ++i)
            vm_mock_wager_streak_set(ch->chall[i].roleId, 0);
        return;
    }
    if (winnerSide == VM_MOCK_WAGER_SIDE_HOST)
    {
        winners = ch->host;
        winCount = ch->hostCount;
        losers = ch->chall;
        loseCount = ch->challCount;
    }
    else
    {
        winners = ch->chall;
        winCount = ch->challCount;
        losers = ch->host;
        loseCount = ch->hostCount;
    }
    for (i = 0; i < loseCount; ++i)
        vm_mock_wager_streak_set(losers[i].roleId, 0);
    for (i = 0; i < winCount; ++i)
    {
        u32 streak = vm_mock_wager_streak_get(winners[i].roleId) + 1;
        vm_mock_wager_streak_set(winners[i].roleId, streak);
        if (streak >= VM_MOCK_WAGER_STREAK_ANNOUNCE)
            vm_mock_wager_announce_streak(winners[i].name, streak);
    }
}

static bool vm_mock_wager_ensure_member_table(void)
{
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_arena_wager_members ("
            "challenge_id INT UNSIGNED NOT NULL,"
            "side TINYINT UNSIGNED NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_name VARBINARY(32) NOT NULL DEFAULT '',"
            "paid TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "accepted TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "payout INT UNSIGNED NOT NULL DEFAULT 0,"
            "PRIMARY KEY(challenge_id,role_id),"
            "KEY idx_wager_member_role(role_id)) ENGINE=InnoDB"))
        return false;
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_wager_members "
        "ADD COLUMN job TINYINT UNSIGNED NOT NULL DEFAULT 0");
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_wager_members "
        "ADD COLUMN level SMALLINT UNSIGNED NOT NULL DEFAULT 0");
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_wager_challenges "
        "ADD COLUMN host_count TINYINT UNSIGNED NOT NULL DEFAULT 1");
    (void)vm_mysql_exec(
        "ALTER TABLE server_arena_wager_challenges "
        "ADD COLUMN challenger_count TINYINT UNSIGNED NOT NULL DEFAULT 0");
    return true;
}

static void vm_mock_wager_push_notice(u32 clientId, u32 challengeId, u8 kind,
                                      u32 stake, u32 payout, u8 result)
{
    vm_mock_wager_notice *n;
    u32 i;

    if (clientId == 0)
        return;
    for (i = 0; i < g_vm_mock_wager_notice_count; ++i)
    {
        if (g_vm_mock_wager_notices[i].clientId == clientId &&
            g_vm_mock_wager_notices[i].challengeId == challengeId &&
            g_vm_mock_wager_notices[i].eventKind == kind)
        {
            g_vm_mock_wager_notices[i].stake = stake;
            g_vm_mock_wager_notices[i].payout = payout;
            g_vm_mock_wager_notices[i].result = result;
            return;
        }
    }
    if (g_vm_mock_wager_notice_count >= VM_MOCK_WAGER_NOTICE_MAX)
    {
        memmove(&g_vm_mock_wager_notices[0], &g_vm_mock_wager_notices[1],
                sizeof(g_vm_mock_wager_notices[0]) *
                    (VM_MOCK_WAGER_NOTICE_MAX - 1));
        g_vm_mock_wager_notice_count = VM_MOCK_WAGER_NOTICE_MAX - 1;
    }
    n = &g_vm_mock_wager_notices[g_vm_mock_wager_notice_count++];
    memset(n, 0, sizeof(*n));
    n->clientId = clientId;
    n->challengeId = challengeId;
    n->eventKind = kind;
    n->stake = stake;
    n->payout = payout;
    n->result = result;
}

static void vm_mock_wager_notify_side(const vm_mock_wager_challenge *ch, u8 side,
                                     u8 kind, u32 payout, u8 result)
{
    const vm_mock_wager_member *list;
    u8 count;
    u8 i;

    if (ch == NULL)
        return;
    list = side == VM_MOCK_WAGER_SIDE_HOST ? ch->host : ch->chall;
    count = side == VM_MOCK_WAGER_SIDE_HOST ? ch->hostCount : ch->challCount;
    for (i = 0; i < count; ++i)
        vm_mock_wager_push_notice(list[i].clientId, ch->challengeId, kind,
                                  ch->stake, payout, result);
}

static vm_mock_wager_challenge *vm_mock_wager_find(u32 challengeId)
{
    u32 i;
    for (i = 0; i < g_vm_mock_wager_count; ++i)
    {
        if (g_vm_mock_wagers[i].active &&
            g_vm_mock_wagers[i].challengeId == challengeId)
            return &g_vm_mock_wagers[i];
    }
    return NULL;
}

static vm_mock_wager_challenge *vm_mock_wager_find_open_by_host(u32 hostRoleId)
{
    u32 i;
    for (i = 0; i < g_vm_mock_wager_count; ++i)
    {
        if (g_vm_mock_wagers[i].active &&
            g_vm_mock_wagers[i].hostLeaderRoleId == hostRoleId &&
            (g_vm_mock_wagers[i].status == VM_MOCK_WAGER_ST_OPEN ||
             g_vm_mock_wagers[i].status == VM_MOCK_WAGER_ST_AWAIT))
            return &g_vm_mock_wagers[i];
    }
    return NULL;
}

/* List/create/challenge must resolve scene the same way; empty
 * sceneVisibleScene alone used to hide open rows so peers never saw
 * [可挑战]. Prefer visible scene, else role->scene. */
static const char *vm_mock_wager_viewer_scene(
    const vm_mock_service_client_session *session,
    const vm_net_mock_role_state *role)
{
    if (session != NULL && session->sceneVisibleScene[0] != 0)
        return session->sceneVisibleScene;
    if (role != NULL && role->scene[0] != 0)
        return role->scene;
    return "";
}

/* 摆擂仅限「擂台宗主」所在地图（与 NPC 入口关键字一致）. */
static bool vm_mock_wager_npc_name_is_host(const char *npcName)
{
    if (npcName == NULL || npcName[0] == 0)
        return false;
    if (strstr(npcName, "\xc0\xde\xcc\xa8\xd7\xda\xd6\xf7") != NULL)
        return true;
    return strstr(npcName, "\xe6\x93\x82\xe5\x8f\xb0\xe5\xae\x97\xe4\xb8\xbb") !=
           NULL;
}

static bool vm_mock_wager_scene_has_host_npc(const char *scene)
{
    vm_net_mock_scene_npcinfo_seed seeds[VM_NET_MOCK_SCENE_NPC_CATALOG_MAX];
    u32 count;
    u32 i;
    const char *name;

    if (scene == NULL || scene[0] == 0)
        return false;
    count = vm_net_mock_collect_scene_npcinfo_seeds(
        scene, seeds, VM_NET_MOCK_SCENE_NPC_CATALOG_MAX, NULL, NULL);
    for (i = 0; i < count; ++i)
    {
        name = seeds[i].displayName[0] ? seeds[i].displayName
                                       : seeds[i].scriptName;
        if (vm_mock_wager_npc_name_is_host(name))
            return true;
    }
    return false;
}

static u32 vm_mock_wager_client_id_for_role(u32 roleId)
{
    vm_mock_service_client_session *s = g_vm_mock_service_client_sessions;
    if (roleId == 0)
        return 0;
    for (; s != NULL; s = s->next)
    {
        if (s->roleOnline && s->onlineRoleId == roleId)
            return s->clientId;
    }
    return 0;
}

static void vm_mock_wager_rebind_member_clients(vm_mock_wager_challenge *ch)
{
    u8 i;
    if (ch == NULL)
        return;
    for (i = 0; i < ch->hostCount; ++i)
        ch->host[i].clientId = vm_mock_wager_client_id_for_role(ch->host[i].roleId);
    for (i = 0; i < ch->challCount; ++i)
        ch->chall[i].clientId =
            vm_mock_wager_client_id_for_role(ch->chall[i].roleId);
}

static void vm_mock_wager_fill_member_stats(vm_mock_wager_member *m,
                                           const vm_net_mock_role_state *role)
{
    if (m == NULL || role == NULL)
        return;
    m->job = role->job >= 1 && role->job <= 3 ? role->job : 1;
    m->level = (u16)(role->level ? role->level : 1);
}

static void vm_mock_wager_refresh_member_stats(vm_mock_wager_member *m)
{
    vm_mock_service_client_session *s;
    vm_net_mock_role_state *role;
    if (m == NULL || m->roleId == 0)
        return;
    s = g_vm_mock_service_client_sessions;
    for (; s != NULL; s = s->next)
    {
        if (!s->roleOnline || s->onlineRoleId != m->roleId)
            continue;
        role = vm_mock_service_trade_role_for_session(s, NULL);
        if (role != NULL)
            vm_mock_wager_fill_member_stats(m, role);
        return;
    }
}

static bool vm_mock_wager_hydrate_challenge_row(void *contextValue,
                                               unsigned int columnCount,
                                               const char *const *values,
                                               const size_t *lengths)
{
    vm_mock_wager_challenge *ch;
    u32 tmp = 0;
    u32 challengeId = 0;

    (void)contextValue;
    (void)columnCount;
    if (values == NULL || lengths == NULL ||
        g_vm_mock_wager_count >= VM_MOCK_WAGER_CHALLENGE_MAX)
        return false;
    if (!values[0] || !vm_mock_mysql_parse_u32(values[0], lengths[0], &challengeId) ||
        challengeId == 0)
        return false;
    if (vm_mock_wager_find(challengeId) != NULL)
        return true;
    ch = &g_vm_mock_wagers[g_vm_mock_wager_count++];
    memset(ch, 0, sizeof(*ch));
    ch->active = 1;
    ch->challengeId = challengeId;
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        ch->hostLeaderRoleId = tmp;
    if (values[2] && lengths[2] > 0 && lengths[2] < sizeof(ch->scene))
    {
        memcpy(ch->scene, values[2], lengths[2]);
        ch->scene[lengths[2]] = 0;
    }
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        ch->stake = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        ch->feePercent = (u8)tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        ch->status = (u8)tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        ch->hostCount = (u8)tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        ch->challCount = (u8)tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        ch->challLeaderRoleId = tmp;
    if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp) &&
        tmp != 0)
        ch->idleSinceUnix = tmp;
    else
        ch->idleSinceUnix = (u32)time(NULL);
    /* Counts filled from members; keep DB values as upper bound only. */
    ch->hostCount = 0;
    ch->challCount = 0;
    return true;
}

static bool vm_mock_wager_hydrate_member_row(void *contextValue,
                                            unsigned int columnCount,
                                            const char *const *values,
                                            const size_t *lengths)
{
    vm_mock_wager_challenge *ch;
    vm_mock_wager_member *slot;
    u32 challengeId = 0;
    u32 side = 0;
    u32 roleId = 0;
    u32 tmp = 0;

    (void)contextValue;
    (void)columnCount;
    if (values == NULL || lengths == NULL)
        return false;
    if (!values[0] || !vm_mock_mysql_parse_u32(values[0], lengths[0], &challengeId))
        return false;
    ch = vm_mock_wager_find(challengeId);
    if (ch == NULL)
        return true;
    if (!values[1] || !vm_mock_mysql_parse_u32(values[1], lengths[1], &side))
        return false;
    if (!values[2] || !vm_mock_mysql_parse_u32(values[2], lengths[2], &roleId) ||
        roleId == 0)
        return false;
    if (side == 0)
    {
        if (ch->hostCount >= VM_MOCK_WAGER_MEMBER_MAX)
            return false;
        slot = &ch->host[ch->hostCount++];
    }
    else
    {
        if (ch->challCount >= VM_MOCK_WAGER_MEMBER_MAX)
            return false;
        slot = &ch->chall[ch->challCount++];
    }
    memset(slot, 0, sizeof(*slot));
    slot->roleId = roleId;
    if (values[3] && lengths[3] > 0 && lengths[3] < sizeof(slot->accountId))
    {
        memcpy(slot->accountId, values[3], lengths[3]);
        slot->accountId[lengths[3]] = 0;
    }
    if (values[4] && lengths[4] > 0 && lengths[4] < sizeof(slot->name))
    {
        memcpy(slot->name, values[4], lengths[4]);
        slot->name[lengths[4]] = 0;
    }
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        slot->paid = (u8)tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        slot->accepted = (u8)tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        slot->job = (u8)(tmp >= 1 && tmp <= 3 ? tmp : 0);
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        slot->level = (u16)tmp;
    slot->clientId = vm_mock_wager_client_id_for_role(roleId);
    vm_mock_wager_refresh_member_stats(slot);
    if (side == 0 && ch->hostLeaderRoleId == 0)
        ch->hostLeaderRoleId = roleId;
    if (side != 0 && ch->challLeaderRoleId == 0)
        ch->challLeaderRoleId = roleId;
    return true;
}

static void vm_mock_wager_persist_clear_challengers(u32 challengeId);
static void vm_mock_wager_persist_status(const vm_mock_wager_challenge *ch);
static void vm_mock_wager_persist_host_accepted_all(u32 challengeId, u8 accepted);

/* Open/await rows live in process memory; restart must rehydrate from DB or
 * peers only see empty boards despite server_arena_wager_challenges rows. */
static void vm_mock_wager_hydrate_from_db(void)
{
    static bool attempted = false;
    u32 i;

    if (attempted)
        return;
    attempted = true;
    if (g_vm_mock_wager_count > 0)
        return;
    if (!vm_mock_wager_ensure_member_table())
        return;
    (void)vm_mysql_query(
        "SELECT challenge_id,host_role_id,scene,stake_money,fee_percent,status,"
        "host_count,challenger_count,challenger_role_id,"
        "UNIX_TIMESTAMP(updated_at) "
        "FROM server_arena_wager_challenges WHERE status IN (0,1) "
        "ORDER BY challenge_id",
        vm_mock_wager_hydrate_challenge_row, NULL);
    if (g_vm_mock_wager_count == 0)
        return;
    (void)vm_mysql_query(
        "SELECT m.challenge_id,m.side,m.role_id,m.account_id,m.role_name,"
        "m.paid,m.accepted,m.job,m.level FROM server_arena_wager_members m "
        "INNER JOIN server_arena_wager_challenges c "
        "ON c.challenge_id=m.challenge_id WHERE c.status IN (0,1) "
        /* OPEN: host rows only — stale side=1 after reject must not load. */
        "AND (c.status<>0 OR m.side=0) "
        "ORDER BY m.challenge_id,m.side,m.role_id",
        vm_mock_wager_hydrate_member_row, NULL);
    for (i = 0; i < g_vm_mock_wager_count; ++i)
    {
        vm_mock_wager_challenge *ch = &g_vm_mock_wagers[i];
        if (ch->status == VM_MOCK_WAGER_ST_OPEN &&
            (ch->challCount > 0 || ch->challLeaderRoleId != 0))
        {
            ch->challCount = 0;
            ch->challLeaderRoleId = 0;
            memset(ch->chall, 0, sizeof(ch->chall));
            vm_mock_wager_persist_clear_challengers(ch->challengeId);
            vm_mock_wager_persist_status(ch);
        }
        if (ch->status == VM_MOCK_WAGER_ST_AWAIT && ch->hostCount > 0)
        {
            /* Entering AWAIT must clear host accepted; old rows kept accepted=1
             * so reconnect needConfirm stayed 0 and reject/confirm vanished. */
            u8 pending = 0;
            u8 hi;
            for (hi = 0; hi < ch->hostCount; ++hi)
            {
                if (!ch->host[hi].accepted)
                    pending = 1;
            }
            if (!pending)
            {
                for (hi = 0; hi < ch->hostCount; ++hi)
                    ch->host[hi].accepted = 0;
                vm_mock_wager_persist_host_accepted_all(ch->challengeId, 0);
            }
        }
        vm_mock_wager_rebind_member_clients(ch);
    }
    printf("[info][mock-service] wager_hydrate count=%u "
           "evidence=reload-open-await-from-db\n",
           g_vm_mock_wager_count);
}

static bool vm_mock_wager_role_busy(vm_mock_service_client_session *session)
{
    vm_mock_service_team *team;

    if (session == NULL)
        return true;
    if (vm_mock_service_duel_find_for_client(session->clientId, NULL) != NULL)
        return true;
    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team != NULL && vm_mock_service_team_battle_in_progress(team))
        return true;
    return false;
}

static bool vm_mock_wager_charge(vm_net_mock_role_state *role, u32 amount)
{
    if (role == NULL || amount == 0 || role->money < amount)
        return false;
    role->money -= amount;
    vm_net_mock_role_mark_inventory_dirty("wager-charge");
    return vm_net_mock_role_db_save_role("wager-charge", role->roleId);
}

static bool vm_mock_wager_credit(vm_net_mock_role_state *role, u32 amount)
{
    if (role == NULL || amount == 0)
        return true;
    role->money = vm_net_mock_add_capped_u32(role->money, amount);
    vm_net_mock_role_mark_inventory_dirty("wager-credit");
    return vm_net_mock_role_db_save_role("wager-credit", role->roleId);
}

static u8 vm_mock_wager_collect_party(vm_mock_service_client_session *leader,
                                      vm_mock_wager_member *out, u8 maxOut,
                                      const char **errOut)
{
    vm_mock_service_team *team;
    u8 count = 0;
    u8 i;

    if (errOut)
        *errOut = NULL;
    if (leader == NULL || out == NULL || maxOut == 0 ||
        !leader->roleOnline || leader->onlineRoleId == 0)
    {
        if (errOut)
            *errOut = "no-session";
        return 0;
    }
    team = vm_mock_service_team_find_for_client(leader->clientId);
    if (team == NULL || team->memberCount < 2)
    {
        vm_net_mock_role_state *role =
            vm_mock_service_trade_role_for_session(leader, NULL);
        if (role == NULL)
        {
            if (errOut)
                *errOut = "no-role";
            return 0;
        }
        memset(&out[0], 0, sizeof(out[0]));
        out[0].roleId = role->roleId;
        out[0].clientId = leader->clientId;
        snprintf(out[0].accountId, sizeof(out[0].accountId), "%s",
                 leader->accountId);
        snprintf(out[0].name, sizeof(out[0].name), "%s", role->name);
        vm_mock_wager_fill_member_stats(&out[0], role);
        return 1;
    }
    if (!vm_mock_service_team_is_leader(team, leader->clientId))
    {
        if (errOut)
            *errOut = "not-leader";
        return 0;
    }
    for (i = 0; i < team->memberCount && count < maxOut; ++i)
    {
        vm_mock_service_client_session *ms =
            vm_mock_service_find_client_session(team->memberClientIds[i]);
        vm_net_mock_role_state *role;
        if (ms == NULL || !ms->roleOnline || ms->onlineRoleId == 0)
        {
            if (errOut)
                *errOut = "member-offline";
            return 0;
        }
        if (!vm_net_mock_scene_names_equal_loose(ms->sceneVisibleScene,
                                                 leader->sceneVisibleScene))
        {
            if (errOut)
                *errOut = "member-scene";
            return 0;
        }
        if (vm_mock_wager_role_busy(ms))
        {
            if (errOut)
                *errOut = "member-busy";
            return 0;
        }
        role = vm_mock_service_trade_role_for_session(ms, NULL);
        if (role == NULL)
        {
            if (errOut)
                *errOut = "member-role";
            return 0;
        }
        memset(&out[count], 0, sizeof(out[count]));
        out[count].roleId = role->roleId;
        out[count].clientId = ms->clientId;
        snprintf(out[count].accountId, sizeof(out[count].accountId), "%s",
                 ms->accountId);
        snprintf(out[count].name, sizeof(out[count].name), "%s", role->name);
        vm_mock_wager_fill_member_stats(&out[count], role);
        count += 1;
    }
    return count;
}

static bool vm_mock_wager_charge_members(vm_mock_wager_member *list, u8 count,
                                         u32 stake)
{
    u8 i;
    for (i = 0; i < count; ++i)
    {
        vm_mock_service_client_session *ms =
            vm_mock_service_find_client_session(list[i].clientId);
        vm_net_mock_role_state *role =
            ms ? vm_mock_service_trade_role_for_session(ms, NULL) : NULL;
        if (!vm_mock_wager_charge(role, stake))
        {
            u8 j;
            for (j = 0; j < i; ++j)
            {
                vm_mock_service_client_session *rs =
                    vm_mock_service_find_client_session(list[j].clientId);
                vm_net_mock_role_state *rr =
                    rs ? vm_mock_service_trade_role_for_session(rs, NULL) : NULL;
                (void)vm_mock_wager_credit(rr, stake);
                list[j].paid = 0;
            }
            return false;
        }
        list[i].paid = 1;
    }
    return true;
}

static void vm_mock_wager_refund_members(vm_mock_wager_member *list, u8 count,
                                         u32 stake)
{
    u8 i;
    for (i = 0; i < count; ++i)
    {
        if (!list[i].paid)
            continue;
        {
            vm_mock_service_client_session *ms =
                vm_mock_service_find_client_session(list[i].clientId);
            vm_net_mock_role_state *role =
                ms ? vm_mock_service_trade_role_for_session(ms, NULL) : NULL;
            (void)vm_mock_wager_credit(role, stake);
            list[i].paid = 0;
        }
    }
}

static bool vm_mock_wager_persist_new(vm_mock_wager_challenge *ch)
{
    char sceneHex[192];
    char hostAccHex[160];
    char query[1024];
    u32 newId = 0;
    u8 i;

    if (ch == NULL || !vm_mock_wager_ensure_member_table())
        return false;
    if (vm_mysql_hex_encode(ch->scene, strlen(ch->scene), sceneHex,
                            sizeof(sceneHex)) == 0)
        return false;
    if (vm_mysql_hex_encode(ch->host[0].accountId, strlen(ch->host[0].accountId),
                            hostAccHex, sizeof(hostAccHex)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_arena_wager_challenges "
             "(host_role_id,host_account_id,scene,stake_money,fee_percent,status,"
             "host_count,challenger_count) VALUES (%u,UNHEX('%s'),UNHEX('%s'),%u,"
             "%u,%u,%u,0)",
             ch->hostLeaderRoleId, hostAccHex, sceneHex, ch->stake,
             (u32)ch->feePercent, (u32)ch->status, (u32)ch->hostCount);
    if (!vm_mysql_exec(query))
        return false;
    if (!vm_mysql_query("SELECT LAST_INSERT_ID()", vm_mock_arena_session_id_row,
                        &newId) ||
        newId == 0)
        return false;
    ch->challengeId = newId;
    for (i = 0; i < ch->hostCount; ++i)
    {
        char accHex[160];
        char nameHex[96];
        if (vm_mysql_hex_encode(ch->host[i].accountId,
                                strlen(ch->host[i].accountId), accHex,
                                sizeof(accHex)) == 0)
            continue;
        if (vm_mysql_hex_encode(ch->host[i].name, strlen(ch->host[i].name),
                                nameHex, sizeof(nameHex)) == 0)
            nameHex[0] = 0;
        snprintf(query, sizeof(query),
                 "INSERT INTO server_arena_wager_members "
                 "(challenge_id,side,role_id,account_id,role_name,paid,accepted,"
                 "job,level) "
                 "VALUES (%u,0,%u,UNHEX('%s'),UNHEX('%s'),1,1,%u,%u)",
                 newId, ch->host[i].roleId, accHex, nameHex,
                 (u32)ch->host[i].job, (u32)ch->host[i].level);
        (void)vm_mysql_exec(query);
    }
    return true;
}

static void vm_mock_wager_persist_clear_challengers(u32 challengeId)
{
    char query[192];
    if (challengeId == 0)
        return;
    /* Reject/cancel-back-to-OPEN must drop side=1 rows or hydrate
     * reloads them as challCount>0 and OPEN boards show under 进行中. */
    snprintf(query, sizeof(query),
             "DELETE FROM server_arena_wager_members WHERE challenge_id=%u AND "
             "side=1",
             challengeId);
    (void)vm_mysql_exec(query);
}

static void vm_mock_wager_persist_status(const vm_mock_wager_challenge *ch)
{
    char query[512];
    char challAccHex[160];
    u8 i;

    if (ch == NULL || ch->challengeId == 0)
        return;
    challAccHex[0] = 0;
    if (ch->challCount > 0 &&
        vm_mysql_hex_encode(ch->chall[0].accountId, strlen(ch->chall[0].accountId),
                            challAccHex, sizeof(challAccHex)) == 0)
        challAccHex[0] = 0;
    snprintf(query, sizeof(query),
             "UPDATE server_arena_wager_challenges SET status=%u,"
             "challenger_role_id=%u,challenger_account_id=%s%s%s,"
             "challenger_count=%u WHERE challenge_id=%u",
             (u32)ch->status, ch->challLeaderRoleId,
             challAccHex[0] ? "UNHEX('" : "''",
             challAccHex[0] ? challAccHex : "",
             challAccHex[0] ? "')" : "",
             (u32)ch->challCount, ch->challengeId);
    (void)vm_mysql_exec(query);
    if (ch->status == VM_MOCK_WAGER_ST_OPEN || ch->challCount == 0)
    {
        vm_mock_wager_persist_clear_challengers(ch->challengeId);
        return;
    }
    if (ch->status == VM_MOCK_WAGER_ST_AWAIT ||
        ch->status == VM_MOCK_WAGER_ST_FIGHT)
    {
        for (i = 0; i < ch->challCount; ++i)
        {
            char accHex[160];
            char nameHex[96];
            if (vm_mysql_hex_encode(ch->chall[i].accountId,
                                    strlen(ch->chall[i].accountId), accHex,
                                    sizeof(accHex)) == 0)
                continue;
            if (vm_mysql_hex_encode(ch->chall[i].name, strlen(ch->chall[i].name),
                                    nameHex, sizeof(nameHex)) == 0)
                nameHex[0] = 0;
            snprintf(query, sizeof(query),
                     "INSERT INTO server_arena_wager_members "
                     "(challenge_id,side,role_id,account_id,role_name,paid,accepted,"
                     "job,level) "
                     "VALUES (%u,1,%u,UNHEX('%s'),UNHEX('%s'),1,0,%u,%u) "
                     "ON DUPLICATE KEY UPDATE paid=1,job=VALUES(job),level=VALUES(level)",
                     ch->challengeId, ch->chall[i].roleId, accHex, nameHex,
                     (u32)ch->chall[i].job, (u32)ch->chall[i].level);
            (void)vm_mysql_exec(query);
        }
    }
}

static void vm_mock_wager_persist_accept(u32 challengeId, u32 roleId, u8 accepted)
{
    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE server_arena_wager_members SET accepted=%u "
             "WHERE challenge_id=%u AND role_id=%u AND side=0",
             (u32)accepted, challengeId, roleId);
    (void)vm_mysql_exec(query);
}

static void vm_mock_wager_persist_host_accepted_all(u32 challengeId, u8 accepted)
{
    char query[256];
    if (challengeId == 0)
        return;
    snprintf(query, sizeof(query),
             "UPDATE server_arena_wager_members SET accepted=%u "
             "WHERE challenge_id=%u AND side=0",
             (u32)accepted, challengeId);
    (void)vm_mysql_exec(query);
}

static void vm_mock_wager_touch_idle(vm_mock_wager_challenge *ch)
{
    if (ch == NULL)
        return;
    ch->idleSinceUnix = (u32)time(NULL);
}

/* OPEN 无人应战 / AWAIT 无人确认拒绝：退押、CANCEL、提示擂主超时. */
static void vm_mock_wager_expire_idle(vm_mock_wager_challenge *ch)
{
    u8 i;
    /* 擂主超时，已自动撤擂 */
    static const char hostTipGbk[] =
        "\xC0\xDE\xD6\xF7\xB3\xAC\xCA\xB1\xA3\xAC\xD2\xD1\xD7\xD4\xB6\xAF"
        "\xB3\xB7\xC0\xDE";
    /* 对方擂台超时已撤销 */
    static const char challTipGbk[] =
        "\xB6\xD4\xB7\xBD\xC0\xDE\xCC\xA8\xB3\xAC\xCA\xB1\xD2\xD1\xB3\xB7"
        "\xCF\xFA";

    if (ch == NULL || !ch->active)
        return;
    if (ch->status != VM_MOCK_WAGER_ST_OPEN &&
        ch->status != VM_MOCK_WAGER_ST_AWAIT)
        return;

    vm_mock_wager_rebind_member_clients(ch);
    if (ch->status == VM_MOCK_WAGER_ST_AWAIT && ch->challCount > 0)
    {
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_CHALL,
                                 VM_MOCK_WAGER_EVT_CANCEL, ch->stake, 0);
        for (i = 0; i < ch->challCount; ++i)
        {
            vm_mock_service_client_session *s =
                vm_mock_service_find_client_session(ch->chall[i].clientId);
            if (s != NULL)
                (void)vm_mock_service_session_enqueue_system_message(
                    s, challTipGbk);
        }
        vm_mock_wager_refund_members(ch->chall, ch->challCount, ch->stake);
        ch->challCount = 0;
        ch->challLeaderRoleId = 0;
        memset(ch->chall, 0, sizeof(ch->chall));
    }

    vm_mock_wager_refund_members(ch->host, ch->hostCount, ch->stake);
    ch->status = VM_MOCK_WAGER_ST_CANCEL;
    vm_mock_wager_persist_status(ch);
    vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_HOST,
                             VM_MOCK_WAGER_EVT_CANCEL, ch->stake, 0);
    for (i = 0; i < ch->hostCount; ++i)
    {
        vm_mock_service_client_session *s =
            vm_mock_service_find_client_session(ch->host[i].clientId);
        if (s != NULL)
            (void)vm_mock_service_session_enqueue_system_message(s, hostTipGbk);
    }
    printf("[info][mock-service] wager_idle_timeout id=%u host=%u stake=%u "
           "idle_since=%u evidence=open-or-await-10min\n",
           ch->challengeId, ch->hostLeaderRoleId, ch->stake, ch->idleSinceUnix);
    ch->active = 0;
}

static void vm_mock_wager_tick_expire(void)
{
    static u32 lastExpireSec = 0;
    u32 now = (u32)time(NULL);
    u32 i;

    if (now == 0 || now == lastExpireSec)
        return;
    lastExpireSec = now;
    (void)vm_mock_arena_load_config(false);
    if (!g_vm_mock_arena_config.wagerEnabled)
        return;
    vm_mock_wager_hydrate_from_db();
    for (i = 0; i < g_vm_mock_wager_count; ++i)
    {
        vm_mock_wager_challenge *ch = &g_vm_mock_wagers[i];
        if (!ch->active)
            continue;
        if (ch->status != VM_MOCK_WAGER_ST_OPEN &&
            ch->status != VM_MOCK_WAGER_ST_AWAIT)
            continue;
        if (ch->idleSinceUnix == 0)
            ch->idleSinceUnix = now;
        if (now < ch->idleSinceUnix + VM_MOCK_WAGER_IDLE_TIMEOUT_SEC)
            continue;
        vm_mock_wager_expire_idle(ch);
    }
}

static bool vm_mock_wager_start_pair(vm_mock_wager_challenge *ch)
{
    vm_mock_service_client_session *a;
    vm_mock_service_client_session *b;
    vm_mock_service_duel *duel;

    if (ch == NULL || ch->pairIndex >= ch->hostCount)
        return false;
    a = vm_mock_service_find_client_session(ch->host[ch->pairIndex].clientId);
    b = vm_mock_service_find_client_session(ch->chall[ch->pairIndex].clientId);
    if (a == NULL || b == NULL || vm_mock_wager_role_busy(a) ||
        vm_mock_wager_role_busy(b))
        return false;
    duel = vm_mock_service_duel_begin(a, b, NULL);
    if (duel == NULL)
        return false;
    duel->wagerMode = true;
    duel->wagerSettled = false;
    duel->wagerChallengeId = ch->challengeId;
    duel->arenaMode = false;
    duel->arenaSettled = false;
    ch->status = VM_MOCK_WAGER_ST_FIGHT;
    vm_mock_wager_persist_status(ch);
    vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_HOST, VM_MOCK_WAGER_EVT_FIGHT,
                             0, 0);
    vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_CHALL, VM_MOCK_WAGER_EVT_FIGHT,
                             0, 0);
    printf("[info][mock-service] wager_pair_start id=%u pair=%u host=%u chall=%u "
           "serial=%u\n",
           ch->challengeId, (u32)ch->pairIndex, ch->host[ch->pairIndex].roleId,
           ch->chall[ch->pairIndex].roleId, duel->serial);
    return true;
}

static void vm_mock_wager_settle(vm_mock_wager_challenge *ch, int winnerSide)
{
    u32 pot;
    u32 fee;
    u32 payoutEach = 0;
    u8 i;

    if (ch == NULL || ch->status == VM_MOCK_WAGER_ST_DONE ||
        ch->status == VM_MOCK_WAGER_ST_CANCEL)
        return;
    pot = ch->stake * ((u32)ch->hostCount + (u32)ch->challCount);
    if (winnerSide < 0)
    {
        vm_mock_wager_refund_members(ch->host, ch->hostCount, ch->stake);
        vm_mock_wager_refund_members(ch->chall, ch->challCount, ch->stake);
        ch->status = VM_MOCK_WAGER_ST_DONE;
        vm_mock_wager_persist_status(ch);
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_HOST,
                                 VM_MOCK_WAGER_EVT_SETTLE, ch->stake, 0);
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_CHALL,
                                 VM_MOCK_WAGER_EVT_SETTLE, ch->stake, 0);
        vm_mock_wager_apply_streaks(ch, -1);
        printf("[info][mock-service] wager_settle id=%u draw pot=%u\n",
               ch->challengeId, pot);
        return;
    }
    fee = (pot * (u32)ch->feePercent) / 100u;
    if (fee > pot)
        fee = pot;
    {
        u32 net = pot - fee;
        u8 winCount =
            winnerSide == VM_MOCK_WAGER_SIDE_HOST ? ch->hostCount : ch->challCount;
        if (winCount == 0)
            winCount = 1;
        payoutEach = net / winCount;
    }
    if (winnerSide == VM_MOCK_WAGER_SIDE_HOST)
    {
        for (i = 0; i < ch->hostCount; ++i)
        {
            vm_mock_service_client_session *ms =
                vm_mock_service_find_client_session(ch->host[i].clientId);
            vm_net_mock_role_state *role =
                ms ? vm_mock_service_trade_role_for_session(ms, NULL) : NULL;
            (void)vm_mock_wager_credit(role, payoutEach);
            ch->host[i].paid = 0;
        }
        for (i = 0; i < ch->challCount; ++i)
            ch->chall[i].paid = 0;
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_HOST,
                                 VM_MOCK_WAGER_EVT_SETTLE, payoutEach, 1);
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_CHALL,
                                 VM_MOCK_WAGER_EVT_SETTLE, 0, 2);
    }
    else
    {
        for (i = 0; i < ch->challCount; ++i)
        {
            vm_mock_service_client_session *ms =
                vm_mock_service_find_client_session(ch->chall[i].clientId);
            vm_net_mock_role_state *role =
                ms ? vm_mock_service_trade_role_for_session(ms, NULL) : NULL;
            (void)vm_mock_wager_credit(role, payoutEach);
            ch->chall[i].paid = 0;
        }
        for (i = 0; i < ch->hostCount; ++i)
            ch->host[i].paid = 0;
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_CHALL,
                                 VM_MOCK_WAGER_EVT_SETTLE, payoutEach, 1);
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_HOST,
                                 VM_MOCK_WAGER_EVT_SETTLE, 0, 2);
    }
    ch->status = VM_MOCK_WAGER_ST_DONE;
    vm_mock_wager_persist_status(ch);
    vm_mock_wager_apply_streaks(ch, winnerSide);
    printf("[info][mock-service] wager_settle id=%u winnerSide=%d pot=%u fee=%u "
           "each=%u\n",
           ch->challengeId, winnerSide, pot, fee, payoutEach);
}

static void vm_mock_arena_on_wager_duel_finished(vm_mock_service_duel *duel)
{
    vm_mock_wager_challenge *ch;
    bool hostDead;
    bool challDead;
    u8 hostSeat;

    if (duel == NULL || !duel->wagerMode || duel->wagerSettled)
        return;
    duel->wagerSettled = true;
    ch = vm_mock_wager_find(duel->wagerChallengeId);
    if (ch == NULL || ch->status != VM_MOCK_WAGER_ST_FIGHT)
        return;
    hostSeat = ch->pairIndex;
    if (hostSeat >= ch->hostCount)
        return;
    if (duel->clientIds[0] == ch->host[hostSeat].clientId)
    {
        hostDead = duel->hp[0] == 0;
        challDead = duel->hp[1] == 0;
    }
    else
    {
        hostDead = duel->hp[1] == 0;
        challDead = duel->hp[0] == 0;
    }
    if (hostDead && challDead)
    {
        /* pair draw — no pair point */
    }
    else if (challDead && !hostDead)
        ch->hostPairWins += 1;
    else if (hostDead && !challDead)
        ch->challPairWins += 1;
    else
    {
        /* timeout / unfinished → treat as pair draw */
    }
    ch->pairIndex += 1;
    if (ch->pairIndex < ch->hostCount)
    {
        if (!vm_mock_wager_start_pair(ch))
        {
            vm_mock_wager_settle(ch, -1);
        }
        return;
    }
    if (ch->hostPairWins > ch->challPairWins)
        vm_mock_wager_settle(ch, VM_MOCK_WAGER_SIDE_HOST);
    else if (ch->challPairWins > ch->hostPairWins)
        vm_mock_wager_settle(ch, VM_MOCK_WAGER_SIDE_CHALL);
    else
        vm_mock_wager_settle(ch, -1);
}

static u32 vm_net_mock_build_wager_result(u8 *out, u32 outCap, u8 subtype,
                                         u32 result, u32 challengeId,
                                         const char *message)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < 16)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     subtype, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "challengeId",
                                    challengeId))
        return 0;
    if (message != NULL && message[0] != 0)
    {
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "message", message))
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

static u32 vm_net_mock_build_wager_list_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 pos = 5;
    u32 objectStart = 0;
    u8 rows[2048];
    u32 rowsPos = 0;
    u32 count = 0;
    u32 i;
    u8 m;
    const char *viewerScene;

    (void)request;
    (void)requestLen;
    (void)vm_mock_arena_load_config(false);
    vm_mock_wager_tick_expire();
    vm_mock_wager_hydrate_from_db();
    if (session == NULL || out == NULL ||
        !g_vm_mock_arena_config.wagerEnabled)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_LIST, 0, 0,
                                             "disabled");
    viewerScene = vm_mock_wager_viewer_scene(session, role);
    /* List is global; challenge/create still enforce 擂台宗主 map. */
    for (i = 0; i < g_vm_mock_wager_count && count < 32; ++i)
    {
        vm_mock_wager_challenge *ch = &g_vm_mock_wagers[i];
        if (!ch->active)
            continue;
        /* OPEN=摆擂；AWAIT/FIGHT=进行中（客户端分栏展示） */
        if (ch->status != VM_MOCK_WAGER_ST_OPEN &&
            ch->status != VM_MOCK_WAGER_ST_AWAIT &&
            ch->status != VM_MOCK_WAGER_ST_FIGHT)
            continue;
        vm_mock_wager_rebind_member_clients(ch);
        for (m = 0; m < ch->hostCount; ++m)
            vm_mock_wager_refresh_member_stats(&ch->host[m]);
        for (m = 0; m < ch->challCount; ++m)
            vm_mock_wager_refresh_member_stats(&ch->chall[m]);
        if (!vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                     ch->challengeId) ||
            !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos, ch->stake) ||
            !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos, ch->status) ||
            !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                     ch->hostCount) ||
            !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                     ch->challCount) ||
            !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                     ch->feePercent) ||
            !vm_net_mock_seq_put_string(rows, sizeof(rows), &rowsPos,
                                        ch->host[0].name) ||
            !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                     ch->hostLeaderRoleId) ||
            !vm_net_mock_seq_put_string(rows, sizeof(rows), &rowsPos, ch->scene))
            break;
        /* After scene: host then chall — each name + job u32 + level u32. */
        for (m = 0; m < ch->hostCount; ++m)
        {
            if (!vm_net_mock_seq_put_string(rows, sizeof(rows), &rowsPos,
                                            ch->host[m].name) ||
                !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                         ch->host[m].job) ||
                !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                         ch->host[m].level))
                break;
        }
        if (m < ch->hostCount)
            break;
        for (m = 0; m < ch->challCount; ++m)
        {
            if (!vm_net_mock_seq_put_string(rows, sizeof(rows), &rowsPos,
                                            ch->chall[m].name) ||
                !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                         ch->chall[m].job) ||
                !vm_net_mock_seq_put_u32(rows, sizeof(rows), &rowsPos,
                                         ch->chall[m].level))
                break;
        }
        if (m < ch->challCount)
            break;
        count += 1;
    }
    printf("[info][mock-service] wager_list viewer=%s mem=%u out=%u "
           "same_scene_hint=%s\n",
           viewerScene[0] ? viewerScene : "-", g_vm_mock_wager_count, count,
           viewerScene);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_WAGER_LIST, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "count", count) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "vitalityCost",
                                    g_vm_mock_arena_config.wagerVitalityCost) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "minStakeGold",
                                    vm_mock_wager_min_stake_gold()) ||
        /* entry not blob: Pixi SeqReader expects naked seq (same as auction/rank). */
        !vm_net_mock_put_object_entry(out, outCap, &pos, "rows", rows,
                                      (u16)rowsPos))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    return pos;
}

static u32 vm_net_mock_build_wager_create_response(const u8 *request,
                                                   u32 requestLen, u8 *out,
                                                   u32 outCap)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_wager_challenge *ch;
    vm_mock_wager_member members[VM_MOCK_WAGER_MEMBER_MAX];
    u8 count;
    u32 stake = 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    const char *err = NULL;
    u8 i;

    (void)vm_mock_arena_load_config(false);
    vm_mock_wager_hydrate_from_db();
    if (!g_vm_mock_arena_config.wagerEnabled)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             "disabled");
    if (session == NULL || role == NULL)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             "no-session");
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                          "stake", &stake) ||
        stake < vm_mock_wager_min_stake_copper())
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             "bad-stake");
    if (vm_mock_wager_find_open_by_host(role->roleId) != NULL)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             "already-open");
    if (vm_mock_wager_role_busy(session))
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             "busy");
    {
        const char *viewerScene = vm_mock_wager_viewer_scene(session, role);
        if (!vm_mock_wager_scene_has_host_npc(viewerScene))
            return vm_net_mock_build_wager_result(
                out, outCap, VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0, "npc-map");
    }
    count = vm_mock_wager_collect_party(session, members, VM_MOCK_WAGER_MEMBER_MAX,
                                       &err);
    if (count == 0)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             err ? err : "party");
    {
        u32 vitalityCost = g_vm_mock_arena_config.wagerVitalityCost;
        if (vitalityCost > 0 &&
            !vm_mock_activities_vitality_spend(role, vitalityCost))
            return vm_net_mock_build_wager_result(
                out, outCap, VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0, "vitality");
        if (!vm_mock_wager_charge_members(members, count, stake))
        {
            if (vitalityCost > 0)
                (void)vm_mock_activities_vitality_add(role, vitalityCost);
            return vm_net_mock_build_wager_result(
                out, outCap, VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0, "money");
        }
        if (g_vm_mock_wager_count >= VM_MOCK_WAGER_CHALLENGE_MAX)
        {
            vm_mock_wager_refund_members(members, count, stake);
            if (vitalityCost > 0)
                (void)vm_mock_activities_vitality_add(role, vitalityCost);
            return vm_net_mock_build_wager_result(
                out, outCap, VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0, "full");
        }
        ch = &g_vm_mock_wagers[g_vm_mock_wager_count++];
        memset(ch, 0, sizeof(*ch));
        ch->active = 1;
        ch->status = VM_MOCK_WAGER_ST_OPEN;
        ch->stake = stake;
        ch->feePercent = g_vm_mock_arena_config.wagerFeePercent
                             ? g_vm_mock_arena_config.wagerFeePercent
                             : 10;
        snprintf(ch->scene, sizeof(ch->scene), "%s",
                 vm_mock_wager_viewer_scene(session, role));
        ch->hostLeaderRoleId = role->roleId;
        ch->hostCount = count;
        for (i = 0; i < count; ++i)
        {
            ch->host[i] = members[i];
            ch->host[i].accepted = 1; /* host posting = consent to fight */
        }
        if (!vm_mock_wager_persist_new(ch))
        {
            vm_mock_wager_refund_members(ch->host, ch->hostCount, stake);
            if (vitalityCost > 0)
                (void)vm_mock_activities_vitality_add(role, vitalityCost);
            ch->active = 0;
            g_vm_mock_wager_count -= 1;
            return vm_net_mock_build_wager_result(
                out, outCap, VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0, "db");
        }
        vm_mock_wager_touch_idle(ch);
        printf("[info][mock-service] wager_create id=%u host=%u n=%u stake=%u "
               "vitality=%u scene=%s\n",
               ch->challengeId, ch->hostLeaderRoleId, (u32)ch->hostCount, stake,
               vitalityCost, ch->scene);
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 1,
                                             ch->challengeId, "ok");
    }
}

static u32 vm_net_mock_build_wager_cancel_response(const u8 *request,
                                                   u32 requestLen, u8 *out,
                                                   u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_wager_challenge *ch;
    u32 challengeId = 0;
    u32 offset = 4;
    vm_net_mock_request_object object;

    vm_mock_wager_hydrate_from_db();
    if (role == NULL)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CANCEL, 0, 0,
                                             "no-session");
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                          "challengeId", &challengeId))
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CANCEL, 0, 0,
                                             "bad-id");
    ch = vm_mock_wager_find(challengeId);
    if (ch == NULL || ch->status != VM_MOCK_WAGER_ST_OPEN ||
        ch->hostLeaderRoleId != role->roleId)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CANCEL, 0,
                                             challengeId, "denied");
    vm_mock_wager_refund_members(ch->host, ch->hostCount, ch->stake);
    /* 撤擂只退押金，摆擂已扣活力不退. */
    ch->status = VM_MOCK_WAGER_ST_CANCEL;
    vm_mock_wager_persist_status(ch);
    vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_HOST,
                             VM_MOCK_WAGER_EVT_CANCEL, ch->stake, 0);
    ch->active = 0;
    return vm_net_mock_build_wager_result(out, outCap,
                                         VM_MOCK_ARENA_WT_WAGER_CANCEL, 1,
                                         challengeId, "ok");
}

static u32 vm_net_mock_build_wager_challenge_response(const u8 *request,
                                                      u32 requestLen, u8 *out,
                                                      u32 outCap)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_wager_challenge *ch;
    vm_mock_wager_member members[VM_MOCK_WAGER_MEMBER_MAX];
    u8 count;
    u32 challengeId = 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    const char *err = NULL;
    u8 i;

    (void)vm_mock_arena_load_config(false);
    vm_mock_wager_hydrate_from_db();
    if (!g_vm_mock_arena_config.wagerEnabled || session == NULL || role == NULL)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0,
                                             0, "disabled");
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                          "challengeId", &challengeId))
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0,
                                             0, "bad-id");
    ch = vm_mock_wager_find(challengeId);
    if (ch == NULL || ch->status != VM_MOCK_WAGER_ST_OPEN)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0,
                                             challengeId, "not-open");
    if (!vm_net_mock_scene_names_equal_loose(
            ch->scene, vm_mock_wager_viewer_scene(session, role)))
        return vm_net_mock_build_wager_result(
            out, outCap, VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0, challengeId,
            "scene");
    if (ch->hostLeaderRoleId == role->roleId)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0,
                                             challengeId, "self");
    for (i = 0; i < ch->hostCount; ++i)
    {
        if (ch->host[i].roleId == role->roleId)
            return vm_net_mock_build_wager_result(
                out, outCap, VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0, challengeId,
                "self");
    }
    count = vm_mock_wager_collect_party(session, members, VM_MOCK_WAGER_MEMBER_MAX,
                                       &err);
    if (count == 0 || count != ch->hostCount)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0,
                                             challengeId,
                                             count == 0 ? (err ? err : "party")
                                                        : "size");
    if (!vm_mock_wager_charge_members(members, count, ch->stake))
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0,
                                             challengeId, "money");
    ch->challLeaderRoleId = role->roleId;
    ch->challCount = count;
    for (i = 0; i < count; ++i)
    {
        ch->chall[i] = members[i];
        ch->chall[i].accepted = 0;
    }
    for (i = 0; i < ch->hostCount; ++i)
        ch->host[i].accepted = 0; /* require host reconfirm */
    ch->status = VM_MOCK_WAGER_ST_AWAIT;
    vm_mock_wager_persist_status(ch);
    vm_mock_wager_persist_host_accepted_all(ch->challengeId, 0);
    vm_mock_wager_touch_idle(ch);
    /* 有人应战你的赏金擂台，请打开确认 */
    {
        static const char awaitTipGbk[] =
            "\xD3\xD0\xC8\xCB\xD3\xA6\xD5\xBD\xC4\xE3\xB5\xC4\xC9\xCD\xBD\xF0"
            "\xC0\xDE\xCC\xA8\xA3\xAC\xC7\xEB\xB4\xF2\xBF\xAA\xC8\xB7\xC8\xCF";
        vm_mock_wager_rebind_member_clients(ch);
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_HOST,
                                 VM_MOCK_WAGER_EVT_AWAIT, 0, 0);
        for (i = 0; i < ch->hostCount; ++i)
        {
            vm_mock_service_client_session *hostSession =
                vm_mock_service_find_client_session(ch->host[i].clientId);
            if (hostSession != NULL)
                (void)vm_mock_service_session_enqueue_system_message(
                    hostSession, awaitTipGbk);
        }
    }
    printf("[info][mock-service] wager_challenge id=%u chall=%u n=%u\n",
           ch->challengeId, ch->challLeaderRoleId, (u32)count);
    return vm_net_mock_build_wager_result(out, outCap,
                                         VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 1,
                                         challengeId, "ok");
}

static u32 vm_net_mock_build_wager_confirm_response(const u8 *request,
                                                    u32 requestLen, u8 *out,
                                                    u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_wager_challenge *ch;
    u32 challengeId = 0;
    u32 accept = 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    u8 i;
    u8 hostIndex = 0xff;
    u8 allAccepted;

    vm_mock_wager_hydrate_from_db();
    if (role == NULL || session == NULL)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CONFIRM, 0, 0,
                                             "no-session");
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                          "challengeId", &challengeId) ||
        !vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                          "accept", &accept))
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CONFIRM, 0, 0,
                                             "bad-req");
    ch = vm_mock_wager_find(challengeId);
    if (ch != NULL)
        vm_mock_wager_rebind_member_clients(ch);
    if (ch == NULL || ch->status != VM_MOCK_WAGER_ST_AWAIT)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CONFIRM, 0,
                                             challengeId, "not-await");
    for (i = 0; i < ch->hostCount; ++i)
    {
        if (ch->host[i].roleId == role->roleId)
        {
            hostIndex = i;
            break;
        }
    }
    if (hostIndex == 0xff)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CONFIRM, 0,
                                             challengeId, "not-host");
    if (accept == 0)
    {
        /* 擂主已拒绝你的赏金挑战 */
        static const char rejectTipGbk[] =
            "\xC0\xDE\xD6\xF7\xD2\xD1\xBE\xDC\xBE\xF8\xC4\xE3\xB5\xC4"
            "\xC9\xCD\xBD\xF0\xCC\xF4\xD5\xBD";
        /* Notify challengers while chall[] still valid (clientId rebound). */
        vm_mock_wager_rebind_member_clients(ch);
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_CHALL,
                                 VM_MOCK_WAGER_EVT_REJECT, ch->stake, 0);
        for (i = 0; i < ch->challCount; ++i)
        {
            vm_mock_service_client_session *challSession =
                vm_mock_service_find_client_session(ch->chall[i].clientId);
            if (challSession != NULL)
                (void)vm_mock_service_session_enqueue_system_message(
                    challSession, rejectTipGbk);
        }
        vm_mock_wager_refund_members(ch->chall, ch->challCount, ch->stake);
        ch->challCount = 0;
        ch->challLeaderRoleId = 0;
        memset(ch->chall, 0, sizeof(ch->chall));
        for (i = 0; i < ch->hostCount; ++i)
            ch->host[i].accepted = 1;
        ch->status = VM_MOCK_WAGER_ST_OPEN;
        vm_mock_wager_persist_status(ch);
        vm_mock_wager_touch_idle(ch);
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CONFIRM, 1,
                                             challengeId, "rejected");
    }
    ch->host[hostIndex].accepted = 1;
    vm_mock_wager_persist_accept(challengeId, role->roleId, 1);
    vm_mock_wager_touch_idle(ch);
    allAccepted = 1;
    for (i = 0; i < ch->hostCount; ++i)
    {
        if (!ch->host[i].accepted)
            allAccepted = 0;
    }
    if (!allAccepted)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CONFIRM, 1,
                                             challengeId, "pending");
    ch->pairIndex = 0;
    ch->hostPairWins = 0;
    ch->challPairWins = 0;
    if (!vm_mock_wager_start_pair(ch))
    {
        vm_mock_wager_refund_members(ch->chall, ch->challCount, ch->stake);
        ch->challCount = 0;
        ch->challLeaderRoleId = 0;
        memset(ch->chall, 0, sizeof(ch->chall));
        for (i = 0; i < ch->hostCount; ++i)
            ch->host[i].accepted = 1;
        ch->status = VM_MOCK_WAGER_ST_OPEN;
        vm_mock_wager_persist_status(ch);
        vm_mock_wager_touch_idle(ch);
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CONFIRM, 0,
                                             challengeId, "start-fail");
    }
    return vm_net_mock_build_wager_result(out, outCap,
                                         VM_MOCK_ARENA_WT_WAGER_CONFIRM, 1,
                                         challengeId, "fight");
}

static u32 vm_net_mock_build_wager_mine_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 pos = 5;
    u32 objectStart = 0;
    u32 i;
    u32 challengeId = 0;
    u8 status = 0;
    u8 needConfirm = 0;
    u8 hostCount = 0;
    u8 challCount = 0;
    u32 stake = 0;
    u8 acceptedMask = 0;

    (void)request;
    (void)requestLen;
    vm_mock_wager_hydrate_from_db();
    if (role == NULL)
        return 0;
    for (i = 0; i < g_vm_mock_wager_count; ++i)
    {
        vm_mock_wager_challenge *ch = &g_vm_mock_wagers[i];
        u8 m;
        if (!ch->active)
            continue;
        if (ch->status != VM_MOCK_WAGER_ST_OPEN &&
            ch->status != VM_MOCK_WAGER_ST_AWAIT &&
            ch->status != VM_MOCK_WAGER_ST_FIGHT)
            continue;
        for (m = 0; m < ch->hostCount; ++m)
        {
            if (ch->host[m].roleId == role->roleId)
            {
                challengeId = ch->challengeId;
                status = ch->status;
                stake = ch->stake;
                hostCount = ch->hostCount;
                challCount = ch->challCount;
                if (ch->status == VM_MOCK_WAGER_ST_AWAIT && !ch->host[m].accepted)
                    needConfirm = 1;
                acceptedMask = 0;
                {
                    u8 k;
                    for (k = 0; k < ch->hostCount; ++k)
                        if (ch->host[k].accepted)
                            acceptedMask |= (u8)(1u << k);
                }
                goto found;
            }
        }
        for (m = 0; m < ch->challCount; ++m)
        {
            if (ch->chall[m].roleId == role->roleId)
            {
                challengeId = ch->challengeId;
                status = ch->status;
                stake = ch->stake;
                hostCount = ch->hostCount;
                challCount = ch->challCount;
                goto found;
            }
        }
    }
found:
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_WAGER_MINE, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "challengeId",
                                    challengeId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "status", status) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "stake", stake) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "hostCount", hostCount) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "challCount",
                                    challCount) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "needConfirm",
                                    needConfirm) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "acceptedMask",
                                    acceptedMask))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    return pos;
}

static bool vm_net_mock_append_wager_event_object(u8 *out, u32 outCap, u32 *pos)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 objectStart = 0;
    u32 i;
    vm_mock_wager_notice notice;
    bool found = false;

    if (out == NULL || pos == NULL || session == NULL)
        return false;
    for (i = 0; i < g_vm_mock_wager_notice_count; ++i)
    {
        if (g_vm_mock_wager_notices[i].clientId == session->clientId)
        {
            notice = g_vm_mock_wager_notices[i];
            memmove(&g_vm_mock_wager_notices[i], &g_vm_mock_wager_notices[i + 1],
                    sizeof(g_vm_mock_wager_notices[0]) *
                        (g_vm_mock_wager_notice_count - i - 1));
            g_vm_mock_wager_notice_count -= 1;
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_WAGER_EVENT,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "challengeId",
                                    notice.challengeId) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "eventKind",
                                    notice.eventKind) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "stake", notice.stake) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "payout", notice.payout) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "result", notice.result))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_wager_protocol_response(const u8 *request,
                                                     u32 requestLen, u8 *out,
                                                     u32 outCap, u8 subtype)
{
    if (subtype == VM_MOCK_ARENA_WT_WAGER_LIST)
        return vm_net_mock_build_wager_list_response(request, requestLen, out,
                                                     outCap);
    if (subtype == VM_MOCK_ARENA_WT_WAGER_CREATE)
        return vm_net_mock_build_wager_create_response(request, requestLen, out,
                                                       outCap);
    if (subtype == VM_MOCK_ARENA_WT_WAGER_CANCEL)
        return vm_net_mock_build_wager_cancel_response(request, requestLen, out,
                                                       outCap);
    if (subtype == VM_MOCK_ARENA_WT_WAGER_CHALLENGE)
        return vm_net_mock_build_wager_challenge_response(request, requestLen, out,
                                                          outCap);
    if (subtype == VM_MOCK_ARENA_WT_WAGER_CONFIRM)
        return vm_net_mock_build_wager_confirm_response(request, requestLen, out,
                                                        outCap);
    if (subtype == VM_MOCK_ARENA_WT_WAGER_MINE)
        return vm_net_mock_build_wager_mine_response(request, requestLen, out,
                                                     outCap);
    return 0;
}
