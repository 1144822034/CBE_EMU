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
    VM_MOCK_WAGER_EVT_CANCEL = 5
};

typedef struct
{
    u32 roleId;
    u32 clientId;
    char accountId[64];
    char name[32];
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
        if (strcmp(ms->sceneVisibleScene, leader->sceneVisibleScene) != 0)
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
                 "(challenge_id,side,role_id,account_id,role_name,paid,accepted) "
                 "VALUES (%u,0,%u,UNHEX('%s'),UNHEX('%s'),1,1)",
                 newId, ch->host[i].roleId, accHex, nameHex);
        (void)vm_mysql_exec(query);
    }
    return true;
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
                     "(challenge_id,side,role_id,account_id,role_name,paid,accepted) "
                     "VALUES (%u,1,%u,UNHEX('%s'),UNHEX('%s'),1,0) "
                     "ON DUPLICATE KEY UPDATE paid=1",
                     ch->challengeId, ch->chall[i].roleId, accHex, nameHex);
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
    duel = vm_mock_service_duel_begin(a, b);
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
    u32 pos = 5;
    u32 objectStart = 0;
    u8 rows[2048];
    u32 rowsPos = 0;
    u32 count = 0;
    u32 i;
    u8 m;

    (void)request;
    (void)requestLen;
    (void)vm_mock_arena_load_config(false);
    if (session == NULL || out == NULL ||
        !g_vm_mock_arena_config.wagerEnabled)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_LIST, 0, 0,
                                             "disabled");
    for (i = 0; i < g_vm_mock_wager_count && count < 32; ++i)
    {
        vm_mock_wager_challenge *ch = &g_vm_mock_wagers[i];
        if (!ch->active)
            continue;
        if (ch->status != VM_MOCK_WAGER_ST_OPEN &&
            ch->status != VM_MOCK_WAGER_ST_AWAIT)
            continue;
        if (strcmp(ch->scene, session->sceneVisibleScene) != 0)
            continue;
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
                                     ch->hostLeaderRoleId))
            break;
        for (m = 0; m < ch->hostCount; ++m)
        {
            if (!vm_net_mock_seq_put_string(rows, sizeof(rows), &rowsPos,
                                            ch->host[m].name))
                break;
        }
        if (m < ch->hostCount)
            break;
        count += 1;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_ARENA_WT_KIND,
                                     VM_MOCK_ARENA_WT_WAGER_LIST, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "count", count) ||
        !vm_net_mock_put_object_blob(out, outCap, &pos, "rows", rows, rowsPos))
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
        stake == 0)
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
    count = vm_mock_wager_collect_party(session, members, VM_MOCK_WAGER_MEMBER_MAX,
                                       &err);
    if (count == 0)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             err ? err : "party");
    if (!vm_mock_wager_charge_members(members, count, stake))
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             "money");
    if (g_vm_mock_wager_count >= VM_MOCK_WAGER_CHALLENGE_MAX)
    {
        vm_mock_wager_refund_members(members, count, stake);
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             "full");
    }
    ch = &g_vm_mock_wagers[g_vm_mock_wager_count++];
    memset(ch, 0, sizeof(*ch));
    ch->active = 1;
    ch->status = VM_MOCK_WAGER_ST_OPEN;
    ch->stake = stake;
    ch->feePercent = g_vm_mock_arena_config.wagerFeePercent
                         ? g_vm_mock_arena_config.wagerFeePercent
                         : 10;
    snprintf(ch->scene, sizeof(ch->scene), "%s", session->sceneVisibleScene);
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
        ch->active = 0;
        g_vm_mock_wager_count -= 1;
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CREATE, 0, 0,
                                             "db");
    }
    printf("[info][mock-service] wager_create id=%u host=%u n=%u stake=%u scene=%s\n",
           ch->challengeId, ch->hostLeaderRoleId, (u32)ch->hostCount, stake,
           ch->scene);
    return vm_net_mock_build_wager_result(out, outCap,
                                         VM_MOCK_ARENA_WT_WAGER_CREATE, 1,
                                         ch->challengeId, "ok");
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
    if (strcmp(ch->scene, session->sceneVisibleScene) != 0)
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CHALLENGE, 0,
                                             challengeId, "scene");
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
    vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_HOST, VM_MOCK_WAGER_EVT_AWAIT,
                             0, 0);
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
        vm_mock_wager_refund_members(ch->chall, ch->challCount, ch->stake);
        ch->challCount = 0;
        ch->challLeaderRoleId = 0;
        memset(ch->chall, 0, sizeof(ch->chall));
        for (i = 0; i < ch->hostCount; ++i)
            ch->host[i].accepted = 1;
        ch->status = VM_MOCK_WAGER_ST_OPEN;
        vm_mock_wager_persist_status(ch);
        vm_mock_wager_notify_side(ch, VM_MOCK_WAGER_SIDE_CHALL,
                                 VM_MOCK_WAGER_EVT_REJECT, ch->stake, 0);
        return vm_net_mock_build_wager_result(out, outCap,
                                             VM_MOCK_ARENA_WT_WAGER_CONFIRM, 1,
                                             challengeId, "rejected");
    }
    ch->host[hostIndex].accepted = 1;
    vm_mock_wager_persist_accept(challengeId, role->roleId, 1);
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
