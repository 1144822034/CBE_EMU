/*
 * Pixi 娱乐「猜猜看」(WT kind=45): 30-minute dice rounds, 大/小/单/双/豹子 bets.
 * Included after mock_server_ladder.c.
 */

enum
{
    VM_MOCK_GUESS_WT_KIND = 45,
    VM_MOCK_GUESS_WT_STATUS = 1,
    VM_MOCK_GUESS_WT_BET = 2,
    VM_MOCK_GUESS_WT_HISTORY = 3,
    VM_MOCK_GUESS_WT_CLAIM = 4,
    VM_MOCK_GUESS_ROUND_SEC = 1800,
    VM_MOCK_GUESS_LOCK_SEC = 30,
    VM_MOCK_GUESS_FEE_PERCENT = 8,
    VM_MOCK_GUESS_MIN_GOLD = 1,
    VM_MOCK_GUESS_MAX_GOLD = 999,
    VM_MOCK_GUESS_PAGE_SIZE = 8,
    VM_MOCK_GUESS_CHOICE_BIG = 1,
    VM_MOCK_GUESS_CHOICE_SMALL = 2,
    VM_MOCK_GUESS_CHOICE_ODD = 3,
    VM_MOCK_GUESS_CHOICE_EVEN = 4,
    VM_MOCK_GUESS_CHOICE_BAOZI = 5,
    VM_MOCK_GUESS_ROUND_OPEN = 0,
    VM_MOCK_GUESS_ROUND_DRAWN = 1,
    VM_MOCK_GUESS_EVEN_MULT = 2,
    VM_MOCK_GUESS_BAOZI_MULT = 30,
    VM_MOCK_GUESS_DRAW_BATCH = 4
};

typedef struct
{
    u32 roundId;
    u32 periodStart;
    u32 periodEnd;
    u8 dice1;
    u8 dice2;
    u8 dice3;
    u8 status;
} vm_mock_guess_round_row;

typedef struct
{
    u32 betId;
    u32 roundId;
    u8 choice;
    u32 stakeCopper;
    u8 dice1;
    u8 dice2;
    u8 dice3;
    u8 win;
    u32 payoutCopper;
    u8 claimed;
    u8 roundDrawn;
} vm_mock_guess_history_row;

typedef struct
{
    vm_mock_guess_history_row *rows;
    u32 cap;
    u32 count;
} vm_mock_guess_history_ctx;

typedef struct
{
    u32 roleId;
    char name[32];
    u32 winCopper;
} vm_mock_guess_winner_row;

typedef struct
{
    vm_mock_guess_winner_row rows[3];
    u32 count;
} vm_mock_guess_winner_ctx;

static u32 g_vm_mock_guess_last_tick_sec = 0;
static u32 g_vm_mock_guess_rng = 0;

typedef struct
{
    vm_mock_guess_round_row rows[VM_MOCK_GUESS_DRAW_BATCH];
    u32 count;
} vm_mock_guess_due_ctx;

typedef struct
{
    u32 betId;
    u32 payoutCopper;
    bool found;
} vm_mock_guess_claim_row_ctx;

static bool vm_mock_guess_u32_row(void *contextValue, unsigned int columnCount,
                                  const char *const *values, const size_t *lengths)
{
    u32 *out = (u32 *)contextValue;

    (void)columnCount;
    if (out == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    return vm_mock_mysql_parse_u32(values[0], lengths[0], out);
}

static bool vm_mock_guess_claim_row_cb(void *contextValue, unsigned int columnCount,
                                       const char *const *values, const size_t *lengths)
{
    vm_mock_guess_claim_row_ctx *ctx = (vm_mock_guess_claim_row_ctx *)contextValue;

    (void)columnCount;
    if (ctx == NULL || values == NULL || lengths == NULL)
        return false;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &ctx->betId))
        return false;
    if (!vm_mock_mysql_parse_u32(values[1], lengths[1], &ctx->payoutCopper))
        return false;
    ctx->found = true;
    return true;
}

static bool vm_mock_guess_exec_affected(const char *sql, u32 *affectedOut)
{
    u32 affected = 0;

    if (affectedOut != NULL)
        *affectedOut = 0;
    if (sql == NULL || !vm_mysql_exec(sql))
        return false;
    if (!vm_mysql_query("SELECT ROW_COUNT()", vm_mock_guess_u32_row, &affected))
        return false;
    if (affectedOut != NULL)
        *affectedOut = affected;
    return true;
}

static void vm_mock_guess_rng_mix(u32 entropy)
{
    g_vm_mock_guess_rng ^= entropy + 0x9e3779b9u + (g_vm_mock_guess_rng << 6) +
                           (g_vm_mock_guess_rng >> 2);
    if (g_vm_mock_guess_rng == 0)
        g_vm_mock_guess_rng = 0xC0DEC0DEu ^ entropy ^ 1u;
}

static u32 vm_mock_guess_rng_u32(void)
{
    if (g_vm_mock_guess_rng == 0)
        vm_mock_guess_rng_mix((u32)time(NULL) ^ 0x5151u);
    g_vm_mock_guess_rng = g_vm_mock_guess_rng * 1664525u + 1013904223u;
    return g_vm_mock_guess_rng;
}

static u8 vm_mock_guess_roll_die(void)
{
    return (u8)(1u + (vm_mock_guess_rng_u32() % 6u));
}

static bool vm_mock_guess_account_hex(char *hexOut, size_t hexCap)
{
    const char *accountId = g_vm_mock_service_active_account_id;

    if (accountId == NULL || accountId[0] == 0 || hexOut == NULL || hexCap == 0)
        return false;
    return vm_mysql_hex_encode(accountId, strlen(accountId), hexOut, hexCap) != 0;
}

static bool vm_mock_guess_ensure_tables(void)
{
    static bool ready = false;

    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_guess_rounds ("
            "round_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "period_start_unix INT UNSIGNED NOT NULL,"
            "period_end_unix INT UNSIGNED NOT NULL,"
            "dice1 TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "dice2 TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "dice3 TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(round_id),"
            "UNIQUE KEY idx_guess_rounds_period(period_start_unix),"
            "KEY idx_guess_rounds_end(status,period_end_unix)) ENGINE=InnoDB"))
        return false;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_guess_bets ("
            "bet_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "round_id INT UNSIGNED NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_name VARBINARY(32) NOT NULL DEFAULT '',"
            "choice TINYINT UNSIGNED NOT NULL,"
            "stake_copper INT UNSIGNED NOT NULL,"
            "payout_copper INT UNSIGNED NOT NULL DEFAULT 0,"
            "win TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "claimed TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(bet_id),"
            "KEY idx_guess_bets_role(role_id,bet_id DESC),"
            "KEY idx_guess_bets_round(round_id,win,payout_copper)) ENGINE=InnoDB"))
        return false;
    ready = true;
    return true;
}

static u32 vm_mock_guess_period_start(u32 now)
{
    return (now / VM_MOCK_GUESS_ROUND_SEC) * VM_MOCK_GUESS_ROUND_SEC;
}

static bool vm_mock_guess_is_triple(u8 d1, u8 d2, u8 d3)
{
    return d1 == d2 && d2 == d3;
}

static u8 vm_mock_guess_dice_sum(u8 d1, u8 d2, u8 d3)
{
    return (u8)(d1 + d2 + d3);
}

static bool vm_mock_guess_choice_wins(u8 choice, u8 d1, u8 d2, u8 d3)
{
    u8 sum = vm_mock_guess_dice_sum(d1, d2, d3);

    if (vm_mock_guess_is_triple(d1, d2, d3))
        return choice == VM_MOCK_GUESS_CHOICE_BAOZI;
    switch (choice)
    {
    case VM_MOCK_GUESS_CHOICE_BIG:
        return sum >= 11 && sum <= 17;
    case VM_MOCK_GUESS_CHOICE_SMALL:
        return sum >= 4 && sum <= 10;
    case VM_MOCK_GUESS_CHOICE_ODD:
        return (sum & 1u) != 0;
    case VM_MOCK_GUESS_CHOICE_EVEN:
        return (sum & 1u) == 0;
    default:
        return false;
    }
}

static u32 vm_mock_guess_payout_multiplier(u8 choice)
{
    return choice == VM_MOCK_GUESS_CHOICE_BAOZI ? VM_MOCK_GUESS_BAOZI_MULT
                                                : VM_MOCK_GUESS_EVEN_MULT;
}

static u32 vm_mock_guess_net_payout(u32 stakeCopper, u8 choice, bool win)
{
    u32 gross;
    u32 fee;

    if (!win || stakeCopper == 0)
        return 0;
    gross = stakeCopper * vm_mock_guess_payout_multiplier(choice);
    if (gross < stakeCopper)
        return 0;
    fee = gross * VM_MOCK_GUESS_FEE_PERCENT / 100u;
    if (gross <= fee)
        return 0;
    return gross - fee;
}

static bool vm_mock_guess_round_row_cb(void *contextValue, unsigned int columnCount,
                                       const char *const *values, const size_t *lengths)
{
    vm_mock_guess_round_row *row = (vm_mock_guess_round_row *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (row == NULL || values == NULL || lengths == NULL)
        return false;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->roundId))
        return false;
    if (!vm_mock_mysql_parse_u32(values[1], lengths[1], &row->periodStart))
        return false;
    if (!vm_mock_mysql_parse_u32(values[2], lengths[2], &row->periodEnd))
        return false;
    if (!vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        return false;
    row->dice1 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        return false;
    row->dice2 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        return false;
    row->dice3 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        return false;
    row->status = (u8)tmp;
    return true;
}

static bool vm_mock_guess_load_round_by_period(u32 periodStart,
                                               vm_mock_guess_round_row *rowOut)
{
    char query[256];

    if (rowOut == NULL)
        return false;
    memset(rowOut, 0, sizeof(*rowOut));
    snprintf(query, sizeof(query),
             "SELECT round_id,period_start_unix,period_end_unix,dice1,dice2,dice3,"
             "status FROM server_guess_rounds WHERE period_start_unix=%u LIMIT 1",
             periodStart);
    return vm_mysql_query(query, vm_mock_guess_round_row_cb, rowOut) &&
           rowOut->roundId != 0;
}

static bool vm_mock_guess_load_round_by_id(u32 roundId, vm_mock_guess_round_row *rowOut)
{
    char query[256];

    if (rowOut == NULL)
        return false;
    memset(rowOut, 0, sizeof(*rowOut));
    snprintf(query, sizeof(query),
             "SELECT round_id,period_start_unix,period_end_unix,dice1,dice2,dice3,"
             "status FROM server_guess_rounds WHERE round_id=%u LIMIT 1",
             roundId);
    return vm_mysql_query(query, vm_mock_guess_round_row_cb, rowOut) &&
           rowOut->roundId != 0;
}

static bool vm_mock_guess_ensure_round(u32 periodStart, vm_mock_guess_round_row *rowOut)
{
    char query[256];
    vm_mock_guess_round_row row;
    u32 periodEnd = periodStart + VM_MOCK_GUESS_ROUND_SEC;

    if (!vm_mock_guess_ensure_tables())
        return false;
    if (vm_mock_guess_load_round_by_period(periodStart, &row))
    {
        if (rowOut != NULL)
            *rowOut = row;
        return true;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_guess_rounds "
             "(period_start_unix,period_end_unix,status) VALUES (%u,%u,%u)",
             periodStart, periodEnd, VM_MOCK_GUESS_ROUND_OPEN);
    if (!vm_mysql_exec(query))
        return false;
    return vm_mock_guess_load_round_by_period(periodStart, &row) &&
           (rowOut == NULL || (*rowOut = row, true));
}

static bool vm_mock_guess_winner_row_cb(void *contextValue, unsigned int columnCount,
                                        const char *const *values, const size_t *lengths)
{
    vm_mock_guess_winner_ctx *ctx = (vm_mock_guess_winner_ctx *)contextValue;
    vm_mock_guess_winner_row *row;
    u32 tmp = 0;

    (void)columnCount;
    if (ctx == NULL || values == NULL || lengths == NULL || ctx->count >= 3)
        return false;
    row = &ctx->rows[ctx->count];
    memset(row, 0, sizeof(*row));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->roleId))
        return false;
    if (values[1] != NULL && lengths[1] > 0 && lengths[1] < sizeof(row->name))
    {
        memcpy(row->name, values[1], lengths[1]);
        row->name[lengths[1]] = 0;
    }
    if (!vm_mock_mysql_parse_u32(values[2], lengths[2], &row->winCopper))
        return false;
    ctx->count += 1;
    return true;
}

static void vm_mock_guess_announce_top_winners(u32 roundId, u8 d1, u8 d2, u8 d3)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    vm_mock_guess_winner_ctx ctx;
    char query[384];
    char message[160];
    u32 i;

    memset(&ctx, 0, sizeof(ctx));
    snprintf(query, sizeof(query),
             "SELECT b.role_id,b.role_name,SUM(b.payout_copper) AS win_sum "
             "FROM server_guess_bets b WHERE b.round_id=%u AND b.win=1 "
             "GROUP BY b.role_id,b.role_name ORDER BY win_sum DESC LIMIT 3",
             roundId);
    (void)vm_mysql_query(query, vm_mock_guess_winner_row_cb, &ctx);
    if (ctx.count == 0)
        return;
    for (i = 0; i < ctx.count; ++i)
    {
        u32 gold = ctx.rows[i].winCopper / VM_NET_MOCK_COPPER_PER_GOLD;
        int wrote;

        wrote = snprintf(message, sizeof(message),
                         "\xB2\xC2\xB2\xC2\xBF\xB4 \xB5\xDA%u\xC3\xFB %s "
                         "\xD6\xD0\xBD\xB1 %u\xBD\xF0 "
                         "(\xB5\xE3\xCA\xFD %u,%u,%u)",
                         (u32)(i + 1u), ctx.rows[i].name, gold, (u32)d1, (u32)d2,
                         (u32)d3);
        if (wrote <= 0 || (size_t)wrote >= sizeof(message))
            continue;
        (void)vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    }
}

static bool vm_mock_guess_draw_round(vm_mock_guess_round_row *round)
{
    u8 d1;
    u8 d2;
    u8 d3;
    char query[512];

    if (round == NULL || round->roundId == 0 ||
        round->status != VM_MOCK_GUESS_ROUND_OPEN)
        return false;
    d1 = vm_mock_guess_roll_die();
    d2 = vm_mock_guess_roll_die();
    d3 = vm_mock_guess_roll_die();
    snprintf(query, sizeof(query),
             "UPDATE server_guess_rounds SET dice1=%u,dice2=%u,dice3=%u,status=%u "
             "WHERE round_id=%u AND status=%u",
             (u32)d1, (u32)d2, (u32)d3, VM_MOCK_GUESS_ROUND_DRAWN, round->roundId,
             VM_MOCK_GUESS_ROUND_OPEN);
    if (!vm_mysql_exec(query))
        return false;
    snprintf(query, sizeof(query),
             "UPDATE server_guess_bets b "
             "INNER JOIN server_guess_rounds r ON r.round_id=b.round_id "
             "SET b.win=CASE "
             "WHEN (%u=%u AND %u=%u AND %u=%u AND b.choice=%u) THEN 1 "
             "WHEN (%u=%u AND %u=%u AND NOT (%u=%u AND %u=%u AND %u=%u) AND "
             "((b.choice=%u AND (%u+%u+%u) BETWEEN 11 AND 17) OR "
             "(b.choice=%u AND (%u+%u+%u) BETWEEN 4 AND 10) OR "
             "(b.choice=%u AND ((%u+%u+%u)%%2)=1) OR "
             "(b.choice=%u AND ((%u+%u+%u)%%2)=0))) THEN 1 "
             "ELSE 0 END, "
             "b.payout_copper=CASE "
             "WHEN (%u=%u AND %u=%u AND %u=%u AND b.choice=%u) THEN "
             "GREATEST(0,b.stake_copper*%u-b.stake_copper*%u*%u/100) "
             "WHEN NOT (%u=%u AND %u=%u AND %u=%u) AND "
             "((b.choice=%u AND (%u+%u+%u) BETWEEN 11 AND 17) OR "
             "(b.choice=%u AND (%u+%u+%u) BETWEEN 4 AND 10) OR "
             "(b.choice=%u AND ((%u+%u+%u)%%2)=1) OR "
             "(b.choice=%u AND ((%u+%u+%u)%%2)=0)) THEN "
             "GREATEST(0,b.stake_copper*%u-b.stake_copper*%u*%u/100) "
             "ELSE 0 END "
             "WHERE b.round_id=%u",
             (u32)d1, (u32)d1, (u32)d2, (u32)d2, (u32)d3, (u32)d3,
             VM_MOCK_GUESS_CHOICE_BAOZI,
             (u32)d1, (u32)d1, (u32)d2, (u32)d2, (u32)d1, (u32)d2, (u32)d3,
             (u32)d3,
             VM_MOCK_GUESS_CHOICE_BIG, (u32)d1, (u32)d2, (u32)d3,
             VM_MOCK_GUESS_CHOICE_SMALL, (u32)d1, (u32)d2, (u32)d3,
             VM_MOCK_GUESS_CHOICE_ODD, (u32)d1, (u32)d2, (u32)d3,
             VM_MOCK_GUESS_CHOICE_EVEN, (u32)d1, (u32)d2, (u32)d3,
             (u32)d1, (u32)d1, (u32)d2, (u32)d2, (u32)d3, (u32)d3,
             VM_MOCK_GUESS_CHOICE_BAOZI, VM_MOCK_GUESS_BAOZI_MULT,
             VM_MOCK_GUESS_BAOZI_MULT, VM_MOCK_GUESS_FEE_PERCENT,
             (u32)d1, (u32)d1, (u32)d2, (u32)d2, (u32)d3, (u32)d3,
             VM_MOCK_GUESS_CHOICE_BIG, (u32)d1, (u32)d2, (u32)d3,
             VM_MOCK_GUESS_CHOICE_SMALL, (u32)d1, (u32)d2, (u32)d3,
             VM_MOCK_GUESS_CHOICE_ODD, (u32)d1, (u32)d2, (u32)d3,
             VM_MOCK_GUESS_CHOICE_EVEN, (u32)d1, (u32)d2, (u32)d3,
             VM_MOCK_GUESS_EVEN_MULT, VM_MOCK_GUESS_EVEN_MULT,
             VM_MOCK_GUESS_FEE_PERCENT, round->roundId);
    if (!vm_mysql_exec(query))
        return false;
    round->dice1 = d1;
    round->dice2 = d2;
    round->dice3 = d3;
    round->status = VM_MOCK_GUESS_ROUND_DRAWN;
    vm_mock_guess_announce_top_winners(round->roundId, d1, d2, d3);
    printf("[info][mock-service] guess_draw round=%u dice=%u/%u/%u sum=%u\n",
           round->roundId, (u32)d1, (u32)d2, (u32)d3,
           (u32)vm_mock_guess_dice_sum(d1, d2, d3));
    return true;
}

static bool vm_mock_guess_due_round_row_cb(void *contextValue, unsigned int columnCount,
                                           const char *const *values, const size_t *lengths)
{
    vm_mock_guess_due_ctx *ctx = (vm_mock_guess_due_ctx *)contextValue;
    vm_mock_guess_round_row *row;
    u32 tmp = 0;

    (void)columnCount;
    if (ctx == NULL || values == NULL || lengths == NULL ||
        ctx->count >= VM_MOCK_GUESS_DRAW_BATCH)
        return false;
    row = &ctx->rows[ctx->count];
    memset(row, 0, sizeof(*row));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->roundId))
        return false;
    if (!vm_mock_mysql_parse_u32(values[1], lengths[1], &row->periodStart))
        return false;
    if (!vm_mock_mysql_parse_u32(values[2], lengths[2], &row->periodEnd))
        return false;
    if (!vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        return false;
    row->dice1 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        return false;
    row->dice2 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        return false;
    row->dice3 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        return false;
    row->status = (u8)tmp;
    ctx->count += 1;
    return true;
}

static void vm_mock_guess_tick(void)
{
    u32 now = (u32)time(NULL);
    u32 periodStart;
    vm_mock_guess_due_ctx due;
    char query[256];
    u32 i;
    static bool s_tablesReady = false;

    if (now == g_vm_mock_guess_last_tick_sec)
        return;
    g_vm_mock_guess_last_tick_sec = now;
    if (!s_tablesReady)
    {
        if (!vm_mock_guess_ensure_tables())
            return;
        s_tablesReady = true;
    }
    periodStart = vm_mock_guess_period_start(now);
    (void)vm_mock_guess_ensure_round(periodStart, NULL);
    memset(&due, 0, sizeof(due));
    snprintf(query, sizeof(query),
             "SELECT round_id,period_start_unix,period_end_unix,dice1,dice2,dice3,"
             "status FROM server_guess_rounds WHERE status=%u AND period_end_unix<=%u "
             "ORDER BY period_end_unix ASC LIMIT %u",
             VM_MOCK_GUESS_ROUND_OPEN, now, VM_MOCK_GUESS_DRAW_BATCH);
    (void)vm_mysql_query(query, vm_mock_guess_due_round_row_cb, &due);
    for (i = 0; i < due.count; ++i)
        (void)vm_mock_guess_draw_round(&due.rows[i]);
}

static u32 vm_mock_guess_finish_packet(u8 *out, u32 pos, u8 objectCount)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = objectCount;
    return pos;
}

static u32 vm_mock_guess_pending_claim_count(u32 roleId)
{
    char query[192];
    u32 count = 0;

    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM server_guess_bets WHERE role_id=%u AND win=1 "
             "AND claimed=0 AND payout_copper>0",
             roleId);
    (void)vm_mysql_query(query, vm_mock_guess_u32_row, &count);
    return count;
}

static bool vm_mock_guess_betting_open(const vm_mock_guess_round_row *round, u32 now,
                                       u32 *remainSecOut, u32 *lockRemainOut)
{
    u32 remain;
    u32 lockRemain;

    if (round == NULL || round->status != VM_MOCK_GUESS_ROUND_OPEN)
        return false;
    if (now >= round->periodEnd)
        return false;
    remain = round->periodEnd - now;
    lockRemain = remain > VM_MOCK_GUESS_LOCK_SEC ? remain - VM_MOCK_GUESS_LOCK_SEC : 0;
    if (remain <= VM_MOCK_GUESS_LOCK_SEC)
    {
        if (remainSecOut)
            *remainSecOut = remain;
        if (lockRemainOut)
            *lockRemainOut = 0;
        return false;
    }
    if (remainSecOut)
        *remainSecOut = remain;
    if (lockRemainOut)
        *lockRemainOut = lockRemain;
    return true;
}

static u32 vm_net_mock_build_guess_status_response(u8 *out, u32 outCap)
{
    u32 now = (u32)time(NULL);
    u32 periodStart = vm_mock_guess_period_start(now);
    vm_mock_guess_round_row round;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 remainSec = 0;
    u32 lockRemain = 0;
    u32 bettingOpen = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 pending = 0;

    (void)vm_mock_guess_tick();
    if (!vm_mock_guess_ensure_round(periodStart, &round))
        return 0;
    bettingOpen = vm_mock_guess_betting_open(&round, now, &remainSec, &lockRemain)
                      ? 1u
                      : 0u;
    if (role != NULL)
        pending = vm_mock_guess_pending_claim_count(role->roleId);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_GUESS_WT_KIND,
                                     VM_MOCK_GUESS_WT_STATUS, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "roundId", round.roundId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remainSec", remainSec) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "lockRemainSec", lockRemain) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "bettingOpen", bettingOpen) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "dice1", round.dice1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "dice2", round.dice2) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "dice3", round.dice3) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "roundDrawn",
                                    round.status == VM_MOCK_GUESS_ROUND_DRAWN ? 1u
                                                                              : 0u) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pendingClaims", pending) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "feePercent",
                                    VM_MOCK_GUESS_FEE_PERCENT) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "minGold",
                                    VM_MOCK_GUESS_MIN_GOLD) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxGold",
                                    VM_MOCK_GUESS_MAX_GOLD))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_guess_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_guess_op_result(u8 *out, u32 outCap, u8 subtype,
                                              u32 result, u32 betId,
                                              const char *message)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_GUESS_WT_KIND,
                                     subtype, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "betId", betId) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message",
                                       message ? message : ""))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_guess_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_guess_bet_response(const u8 *request, u32 requestLen,
                                                u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 choice = 0;
    u32 stakeGold = 0;
    u32 stakeCopper = 0;
    u32 now = (u32)time(NULL);
    u32 periodStart = vm_mock_guess_period_start(now);
    vm_mock_guess_round_row round;
    char accountHex[160];
    char nameHex[96];
    char query[512];
    u32 betId = 0;

    if (out == NULL || outCap < 5 || request == NULL || role == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_GUESS_WT_KIND ||
        object.subtype != VM_MOCK_GUESS_WT_BET)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "no-role");
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                           "choice", &choice);
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                           "stakeGold", &stakeGold);
    (void)vm_mock_guess_tick();
    if (choice < VM_MOCK_GUESS_CHOICE_BIG ||
        choice > VM_MOCK_GUESS_CHOICE_BAOZI)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "bad-choice");
    if (stakeGold < VM_MOCK_GUESS_MIN_GOLD || stakeGold > VM_MOCK_GUESS_MAX_GOLD)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "bad-stake");
    if (stakeGold > 0xffffffffu / VM_NET_MOCK_COPPER_PER_GOLD)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "bad-stake");
    stakeCopper = stakeGold * VM_NET_MOCK_COPPER_PER_GOLD;
    if (!vm_mock_guess_ensure_round(periodStart, &round))
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "round");
    if (!vm_mock_guess_betting_open(&round, now, NULL, NULL))
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "locked");
    if (role->money < stakeCopper)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "balance");
    if (!vm_mock_guess_account_hex(accountHex, sizeof(accountHex)) ||
        vm_mysql_hex_encode(role->name, strlen(role->name), nameHex, sizeof(nameHex)) ==
            0)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "encode");
    role->money -= stakeCopper;
    vm_net_mock_role_mark_inventory_dirty("guess-bet");
    snprintf(query, sizeof(query),
             "INSERT INTO server_guess_bets "
             "(round_id,role_id,account_id,role_name,choice,stake_copper) VALUES "
             "(%u,%u,UNHEX('%s'),UNHEX('%s'),%u,%u)",
             round.roundId, role->roleId, accountHex, nameHex, choice, stakeCopper);
    if (!vm_mysql_exec(query))
    {
        role->money += stakeCopper;
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 2,
                                               0, "insert");
    }
    snprintf(query, sizeof(query),
             "SELECT bet_id FROM server_guess_bets WHERE role_id=%u "
             "ORDER BY bet_id DESC LIMIT 1",
             role->roleId);
    (void)vm_mysql_query(query, vm_mock_guess_u32_row, &betId);
    vm_net_mock_role_db_save("guess-bet");
    printf("[info][mock-service] guess_bet role=%u round=%u choice=%u stake=%u "
           "bet=%u\n",
           role->roleId, round.roundId, choice, stakeCopper, betId);
    return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_BET, 1,
                                           betId, "ok");
}

static bool vm_mock_guess_history_row_cb(void *contextValue, unsigned int columnCount,
                                         const char *const *values, const size_t *lengths)
{
    vm_mock_guess_history_ctx *ctx = (vm_mock_guess_history_ctx *)contextValue;
    vm_mock_guess_history_row *row;
    u32 tmp = 0;

    (void)columnCount;
    if (ctx == NULL || ctx->rows == NULL || values == NULL || lengths == NULL ||
        ctx->count >= ctx->cap)
        return false;
    row = &ctx->rows[ctx->count];
    memset(row, 0, sizeof(*row));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &row->betId))
        return false;
    if (!vm_mock_mysql_parse_u32(values[1], lengths[1], &row->roundId))
        return false;
    if (!vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        return false;
    row->choice = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[3], lengths[3], &row->stakeCopper))
        return false;
    if (!vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        return false;
    row->dice1 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        return false;
    row->dice2 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        return false;
    row->dice3 = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        return false;
    row->win = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[8], lengths[8], &row->payoutCopper))
        return false;
    if (!vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
        return false;
    row->claimed = (u8)tmp;
    if (!vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
        return false;
    row->roundDrawn = (u8)tmp;
    ctx->count += 1;
    return true;
}

static u32 vm_net_mock_build_guess_history_response(const u8 *request, u32 requestLen,
                                                    u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 pageIndex = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    vm_mock_guess_history_row rows[VM_MOCK_GUESS_PAGE_SIZE + 1];
    vm_mock_guess_history_ctx ctx;
    char query[512];
    u8 blob[2048];
    u32 blobLen = 0;
    u32 i;
    u32 hasMore = 0;
    u32 pending = 0;

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_GUESS_WT_KIND ||
        object.subtype != VM_MOCK_GUESS_WT_HISTORY)
        return 0;
    (void)vm_mock_guess_tick();
    if (role == NULL)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_HISTORY,
                                                 2, 0, "no-role");
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                           "pageIndex", &pageIndex);
    memset(rows, 0, sizeof(rows));
    memset(&ctx, 0, sizeof(ctx));
    ctx.rows = rows;
    ctx.cap = VM_MOCK_GUESS_PAGE_SIZE + 1u;
    snprintf(query, sizeof(query),
             "SELECT b.bet_id,b.round_id,b.choice,b.stake_copper,r.dice1,r.dice2,r.dice3,"
             "b.win,b.payout_copper,b.claimed,r.status "
             "FROM server_guess_bets b "
             "INNER JOIN server_guess_rounds r ON r.round_id=b.round_id "
             "WHERE b.role_id=%u ORDER BY b.bet_id DESC LIMIT %u OFFSET %u",
             role->roleId, VM_MOCK_GUESS_PAGE_SIZE + 1u,
             pageIndex * VM_MOCK_GUESS_PAGE_SIZE);
    (void)vm_mysql_query(query, vm_mock_guess_history_row_cb, &ctx);
    if (ctx.count > VM_MOCK_GUESS_PAGE_SIZE)
    {
        hasMore = 1;
        ctx.count = VM_MOCK_GUESS_PAGE_SIZE;
    }
    for (i = 0; i < ctx.count; ++i)
    {
        if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].betId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].roundId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].choice) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen,
                                     rows[i].stakeCopper) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].dice1) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].dice2) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].dice3) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].win) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen,
                                     rows[i].payoutCopper) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].claimed) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen,
                                     rows[i].roundDrawn))
            return 0;
    }
    pending = vm_mock_guess_pending_claim_count(role->roleId);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_GUESS_WT_KIND,
                                     VM_MOCK_GUESS_WT_HISTORY, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pageIndex", pageIndex) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pageSize",
                                    VM_MOCK_GUESS_PAGE_SIZE) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "hasMore", hasMore) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pendingClaims", pending) ||
        !vm_net_mock_put_object_entry(out, outCap, &pos, "rows", blob, (u16)blobLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_guess_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_guess_claim_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 betId = 0;
    u32 claimed = 0;
    u32 totalCopper = 0;
    char query[384];
    u32 pending = 0;

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_GUESS_WT_KIND ||
        object.subtype != VM_MOCK_GUESS_WT_CLAIM)
        return 0;
    (void)vm_mock_guess_tick();
    if (role == NULL)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_CLAIM, 2,
                                               0, "no-role");
    (void)vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "betId",
                                           &betId);
    if (betId != 0)
    {
        u32 payout = 0;
        u32 affected = 0;

        snprintf(query, sizeof(query),
                 "SELECT payout_copper FROM server_guess_bets WHERE bet_id=%u AND "
                 "role_id=%u AND win=1 AND claimed=0 AND payout_copper>0 LIMIT 1",
                 betId, role->roleId);
        (void)vm_mysql_query(query, vm_mock_guess_u32_row, &payout);
        if (payout == 0)
            return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_CLAIM,
                                                     2, betId, "nothing");
        snprintf(query, sizeof(query),
                 "UPDATE server_guess_bets SET claimed=1 WHERE bet_id=%u AND "
                 "role_id=%u AND win=1 AND claimed=0",
                 betId, role->roleId);
        if (!vm_mock_guess_exec_affected(query, &affected))
            return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_CLAIM,
                                                     2, betId, "update");
        if (affected != 1)
            return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_CLAIM,
                                                     2, betId, "conflict");
        role->money = vm_net_mock_add_capped_u32(role->money, payout);
        vm_net_mock_role_mark_inventory_dirty("guess-claim");
        vm_net_mock_role_db_save("guess-claim");
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_CLAIM, 1,
                                               betId, "ok");
    }
    for (;;)
    {
        vm_mock_guess_claim_row_ctx row;
        u32 affected = 0;

        memset(&row, 0, sizeof(row));
        snprintf(query, sizeof(query),
                 "SELECT bet_id,payout_copper FROM server_guess_bets WHERE role_id=%u "
                 "AND win=1 AND claimed=0 AND payout_copper>0 ORDER BY bet_id ASC "
                 "LIMIT 1",
                 role->roleId);
        if (!vm_mysql_query(query, vm_mock_guess_claim_row_cb, &row) || !row.found ||
            row.betId == 0)
            break;
        snprintf(query, sizeof(query),
                 "UPDATE server_guess_bets SET claimed=1 WHERE bet_id=%u AND "
                 "role_id=%u AND win=1 AND claimed=0",
                 row.betId, role->roleId);
        if (!vm_mock_guess_exec_affected(query, &affected) || affected != 1)
            break;
        totalCopper = vm_net_mock_add_capped_u32(totalCopper, row.payoutCopper);
        claimed += 1;
        if (claimed >= 32)
            break;
    }
    if (claimed == 0)
        return vm_net_mock_build_guess_op_result(out, outCap, VM_MOCK_GUESS_WT_CLAIM, 2,
                                               0, "nothing");
    role->money = vm_net_mock_add_capped_u32(role->money, totalCopper);
    vm_net_mock_role_mark_inventory_dirty("guess-claim-all");
    vm_net_mock_role_db_save("guess-claim-all");
    pending = vm_mock_guess_pending_claim_count(role->roleId);
    {
        u32 pos = 5;
        u32 objectStart = 0;

        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_GUESS_WT_KIND,
                                         VM_MOCK_GUESS_WT_CLAIM, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "betId", 0) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "claimed", claimed) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "pendingClaims", pending) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "message", "ok"))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        return vm_mock_guess_finish_packet(out, pos, 1);
    }
}

static bool vm_net_mock_is_guess45_request(const u8 *request, u32 requestLen,
                                           u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != VM_MOCK_GUESS_WT_KIND)
        return false;
    if (subtypeOut)
        *subtypeOut = object.subtype;
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen;
}

static u32 vm_net_mock_build_guess_protocol_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u8 subtype = 0;

    if (!vm_net_mock_is_guess45_request(request, requestLen, &subtype))
        return 0;
    if (subtype == VM_MOCK_GUESS_WT_STATUS)
        return vm_net_mock_build_guess_status_response(out, outCap);
    if (subtype == VM_MOCK_GUESS_WT_BET)
        return vm_net_mock_build_guess_bet_response(request, requestLen, out, outCap);
    if (subtype == VM_MOCK_GUESS_WT_HISTORY)
        return vm_net_mock_build_guess_history_response(request, requestLen, out, outCap);
    if (subtype == VM_MOCK_GUESS_WT_CLAIM)
        return vm_net_mock_build_guess_claim_response(request, requestLen, out, outCap);
    return 0;
}
