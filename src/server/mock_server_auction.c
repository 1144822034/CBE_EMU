/*
 * PixiJS_web 物品竞拍 — list from bag, bid copper/wcoin, expire/settle claims.
 * Included from mock-server.c after arena. WT kind=42.
 */

enum
{
    VM_MOCK_AUCTION_WT_KIND = 42,
    VM_MOCK_AUCTION_WT_STATUS = 1,
    VM_MOCK_AUCTION_WT_LIST = 2,
    VM_MOCK_AUCTION_WT_MINE = 3,
    VM_MOCK_AUCTION_WT_CREATE = 4,
    VM_MOCK_AUCTION_WT_BID = 5,
    VM_MOCK_AUCTION_WT_CANCEL = 6,
    VM_MOCK_AUCTION_WT_CLAIM = 7,
    VM_MOCK_AUCTION_WT_EVENT = 8,
    VM_MOCK_AUCTION_ST_OPEN = 0,
    VM_MOCK_AUCTION_ST_SOLD = 1,
    VM_MOCK_AUCTION_ST_EXPIRED = 2,
    VM_MOCK_AUCTION_ST_CANCELLED = 3,
    VM_MOCK_AUCTION_BID_ACTIVE = 0,
    VM_MOCK_AUCTION_BID_OUTBID = 1,
    VM_MOCK_AUCTION_BID_WON = 2,
    VM_MOCK_AUCTION_BID_REFUNDED = 3,
    VM_MOCK_AUCTION_CLAIM_ITEM_RETURN = 1,
    VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT = 2,
    VM_MOCK_AUCTION_CLAIM_BID_REFUND = 3,
    VM_MOCK_AUCTION_CLAIM_WINNER_ITEM = 4,
    VM_MOCK_AUCTION_CUR_COPPER = 0,
    VM_MOCK_AUCTION_CUR_WCOIN = 1,
    VM_MOCK_AUCTION_EVT_CLAIM = 1,
    VM_MOCK_AUCTION_EVT_OUTBID = 2,
    VM_MOCK_AUCTION_EVT_SOLD = 3,
    VM_MOCK_AUCTION_EVT_EXPIRED = 4,
    VM_MOCK_AUCTION_PAGE_SIZE = 12,
    VM_MOCK_AUCTION_NOTICE_MAX = 48,
    VM_MOCK_AUCTION_EXPIRE_BATCH = 32
};

typedef struct
{
    u8 enabled;
    u8 feePercentBase;
    u8 feePercentPerTier;
    u32 minBidStep;
    u8 currencyMask;
    u32 maxActiveListings;
} vm_mock_auction_config;

typedef struct
{
    u32 clientId;
    u32 listingId;
    u8 eventKind;
    u32 amount;
    u8 blink;
} vm_mock_auction_notice;

typedef struct
{
    u32 listingId;
    u32 sellerRoleId;
    char sellerName[32];
    u32 itemId;
    u32 itemCount;
    u16 enhanceLevel;
    u32 startPrice;
    u32 currentPrice;
    u8 currency;
    u8 feePercent;
    u8 durationTier;
    u8 status;
    u32 highBidderRoleId;
    u32 expireUnix;
    u32 remainSec;
} vm_mock_auction_listing_row;

typedef struct
{
    u32 claimId;
    u8 kind;
    u32 listingId;
    u32 itemId;
    u32 itemCount;
    u16 enhanceLevel;
    u32 moneyAmount;
    u32 wcoinAmount;
} vm_mock_auction_claim_row;

static vm_mock_auction_config g_vm_mock_auction_config;
static bool g_vm_mock_auction_config_loaded = false;
static u32 g_vm_mock_auction_last_tick_sec = 0;
static vm_mock_auction_notice g_vm_mock_auction_notices[VM_MOCK_AUCTION_NOTICE_MAX];
static u32 g_vm_mock_auction_notice_count = 0;

static const u32 VM_MOCK_AUCTION_DURATION_SEC[4] = {
    3600u, 21600u, 43200u, 86400u};

static void vm_mock_auction_config_set_defaults(vm_mock_auction_config *cfg)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = 1;
    cfg->feePercentBase = 2;
    cfg->feePercentPerTier = 2;
    cfg->minBidStep = 1;
    cfg->currencyMask = 3;
    cfg->maxActiveListings = 20;
}

static bool vm_mock_auction_ensure_tables(void)
{
    static bool ready = false;
    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_auction_config ("
            "id TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "fee_percent_base TINYINT UNSIGNED NOT NULL DEFAULT 2,"
            "fee_percent_per_tier TINYINT UNSIGNED NOT NULL DEFAULT 2,"
            "min_bid_step INT UNSIGNED NOT NULL DEFAULT 1,"
            "currency_mask TINYINT UNSIGNED NOT NULL DEFAULT 3,"
            "max_active_listings INT UNSIGNED NOT NULL DEFAULT 20,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_auction_listings ("
            "listing_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "seller_role_id INT UNSIGNED NOT NULL,"
            "seller_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "seller_name VARBINARY(32) NOT NULL DEFAULT '',"
            "item_id INT UNSIGNED NOT NULL,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 1,"
            "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "start_price INT UNSIGNED NOT NULL DEFAULT 0,"
            "current_price INT UNSIGNED NOT NULL DEFAULT 0,"
            "currency TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "fee_paid INT UNSIGNED NOT NULL DEFAULT 0,"
            "fee_percent TINYINT UNSIGNED NOT NULL DEFAULT 2,"
            "duration_tier TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "high_bidder_role_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "high_bidder_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "expire_unix INT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(listing_id),"
            "KEY idx_auction_listings_status_expire(status,expire_unix),"
            "KEY idx_auction_listings_seller(seller_role_id,status)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_auction_bids ("
            "bid_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "listing_id INT UNSIGNED NOT NULL,"
            "bidder_role_id INT UNSIGNED NOT NULL,"
            "bidder_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "amount INT UNSIGNED NOT NULL DEFAULT 0,"
            "currency TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(bid_id),"
            "KEY idx_auction_bids_listing(listing_id,status),"
            "KEY idx_auction_bids_bidder(bidder_role_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_auction_claims ("
            "claim_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "role_id INT UNSIGNED NOT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "kind TINYINT UNSIGNED NOT NULL,"
            "listing_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "money_amount INT UNSIGNED NOT NULL DEFAULT 0,"
            "wcoin_amount INT UNSIGNED NOT NULL DEFAULT 0,"
            "claimed TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(claim_id),"
            "KEY idx_auction_claims_role(role_id,claimed)) ENGINE=InnoDB"))
        return false;
    ready = true;
    return true;
}

static bool vm_mock_auction_config_row(void *contextValue, unsigned int columnCount,
                                       const char *const *values, const size_t *lengths)
{
    vm_mock_auction_config *cfg = (vm_mock_auction_config *)contextValue;
    u32 tmp = 0;

    (void)columnCount;
    if (cfg == NULL || values == NULL || lengths == NULL)
        return false;
    vm_mock_auction_config_set_defaults(cfg);
    if (values[0] && lengths[0])
        cfg->enabled = (u8)(values[0][0] == '0' ? 0 : 1);
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        cfg->feePercentBase = (u8)tmp;
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        cfg->feePercentPerTier = (u8)tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        cfg->minBidStep = tmp ? tmp : 1;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        cfg->currencyMask = (u8)tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        cfg->maxActiveListings = tmp ? tmp : 20;
    return true;
}

static bool vm_mock_auction_load_config(bool force)
{
    if (g_vm_mock_auction_config_loaded && !force)
        return true;
    if (!vm_mock_auction_ensure_tables())
        return false;
    vm_mock_auction_config_set_defaults(&g_vm_mock_auction_config);
    (void)vm_mysql_exec(
        "INSERT INTO server_auction_config ("
        "id,enabled,fee_percent_base,fee_percent_per_tier,min_bid_step,"
        "currency_mask,max_active_listings) VALUES (1,1,2,2,1,3,20) "
        "ON DUPLICATE KEY UPDATE "
        "fee_percent_base=2,fee_percent_per_tier=2");
    (void)vm_mysql_query(
        "SELECT enabled,fee_percent_base,fee_percent_per_tier,min_bid_step,"
        "currency_mask,max_active_listings FROM server_auction_config "
        "WHERE id=1 LIMIT 1",
        vm_mock_auction_config_row, &g_vm_mock_auction_config);
    g_vm_mock_auction_config_loaded = true;
    return true;
}

static u32 vm_mock_auction_duration_sec(u8 tier)
{
    if (tier > 3)
        tier = 3;
    return VM_MOCK_AUCTION_DURATION_SEC[tier];
}

static u8 vm_mock_auction_fee_percent(u8 tier)
{
    u32 pct;
    (void)vm_mock_auction_load_config(false);
    if (tier > 3)
        tier = 3;
    pct = (u32)g_vm_mock_auction_config.feePercentBase +
          (u32)tier * (u32)g_vm_mock_auction_config.feePercentPerTier;
    if (pct > 90)
        pct = 90;
    if (pct == 0)
        pct = 10;
    return (u8)pct;
}

static u32 vm_mock_auction_calc_fee(u32 startPrice, u8 tier)
{
    u64 fee;
    u8 pct = vm_mock_auction_fee_percent(tier);
    if (startPrice == 0)
        return 0;
    fee = ((u64)startPrice * (u64)pct) / 100ull;
    if (fee == 0)
        fee = 1;
    if (fee > 0xffffffffull)
        fee = 0xffffffffull;
    return (u32)fee;
}

static void vm_mock_auction_item_name(u32 itemId, char *out, u32 outCap)
{
    const vm_net_mock_shop_catalog_item *shop;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    shop = vm_net_mock_find_shop_catalog_item(itemId);
    if (shop != NULL && shop->name[0] != 0)
    {
        snprintf(out, outCap, "%s", shop->name);
        return;
    }
    snprintf(out, outCap, "#%u", itemId);
}

static void vm_mock_auction_push_notice(u32 clientId, u32 listingId, u8 kind,
                                        u32 amount, u8 blink)
{
    vm_mock_auction_notice *n;
    u32 i;

    if (clientId == 0)
        return;
    for (i = 0; i < g_vm_mock_auction_notice_count; ++i)
    {
        if (g_vm_mock_auction_notices[i].clientId == clientId &&
            g_vm_mock_auction_notices[i].listingId == listingId &&
            g_vm_mock_auction_notices[i].eventKind == kind)
        {
            g_vm_mock_auction_notices[i].amount = amount;
            g_vm_mock_auction_notices[i].blink = blink;
            return;
        }
    }
    if (g_vm_mock_auction_notice_count >= VM_MOCK_AUCTION_NOTICE_MAX)
    {
        memmove(&g_vm_mock_auction_notices[0], &g_vm_mock_auction_notices[1],
                sizeof(g_vm_mock_auction_notices[0]) *
                    (VM_MOCK_AUCTION_NOTICE_MAX - 1));
        g_vm_mock_auction_notice_count = VM_MOCK_AUCTION_NOTICE_MAX - 1;
    }
    n = &g_vm_mock_auction_notices[g_vm_mock_auction_notice_count++];
    memset(n, 0, sizeof(*n));
    n->clientId = clientId;
    n->listingId = listingId;
    n->eventKind = kind;
    n->amount = amount;
    n->blink = blink;
}

static vm_mock_service_client_session *vm_mock_auction_session_for_role(u32 roleId)
{
    vm_mock_service_client_session *s = g_vm_mock_service_client_sessions;
    for (; s != NULL; s = s->next)
    {
        if (s->roleOnline && s->onlineRoleId == roleId)
            return s;
    }
    return NULL;
}

static void vm_mock_auction_notify_role(u32 roleId, u32 listingId, u8 kind,
                                        u32 amount)
{
    vm_mock_service_client_session *s = vm_mock_auction_session_for_role(roleId);
    if (s != NULL)
        vm_mock_auction_push_notice(s->clientId, listingId, kind, amount, 1);
}

static bool vm_mock_auction_u32_row(void *contextValue, unsigned int columnCount,
                                    const char *const *values, const size_t *lengths)
{
    u32 *idOut = (u32 *)contextValue;
    (void)columnCount;
    if (idOut == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    return vm_mock_mysql_parse_u32(values[0], lengths[0], idOut);
}

static bool vm_mock_auction_charge(vm_net_mock_role_state *role, u8 currency,
                                   u32 amount, const char *reason)
{
    if (role == NULL || amount == 0)
        return amount == 0;
    if (currency == VM_MOCK_AUCTION_CUR_WCOIN)
    {
        if (role->wcoin < amount)
            return false;
        role->wcoin -= amount;
    }
    else
    {
        if (role->money < amount)
            return false;
        role->money -= amount;
    }
    vm_net_mock_role_mark_inventory_dirty(reason ? reason : "auction-charge");
    return vm_net_mock_role_db_save_role(reason ? reason : "auction-charge",
                                         role->roleId);
}

static bool vm_mock_auction_credit(vm_net_mock_role_state *role, u8 currency,
                                   u32 amount, const char *reason)
{
    if (role == NULL || amount == 0)
        return true;
    if (currency == VM_MOCK_AUCTION_CUR_WCOIN)
        (void)vm_net_mock_role_add_wcoin(role, amount);
    else
        role->money = vm_net_mock_add_capped_u32(role->money, amount);
    vm_net_mock_role_mark_inventory_dirty(reason ? reason : "auction-credit");
    return vm_net_mock_role_db_save_role(reason ? reason : "auction-credit",
                                         role->roleId);
}

static bool vm_mock_auction_insert_claim(u32 roleId, const char *accountId, u8 kind,
                                         u32 listingId, u32 itemId, u32 itemCount,
                                         u16 enhanceLevel, u32 moneyAmount,
                                         u32 wcoinAmount)
{
    char accountEsc[160];
    char query[768];

    if (roleId == 0 || accountId == NULL || accountId[0] == 0)
        return false;
    if (vm_mysql_hex_encode(accountId, strlen(accountId), accountEsc,
                            sizeof(accountEsc)) == 0)
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_auction_claims ("
             "role_id,account_id,kind,listing_id,item_id,item_count,enhance_level,"
             "money_amount,wcoin_amount,claimed) VALUES ("
             "%u,CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u,%u,0)",
             roleId, accountEsc, (u32)kind, listingId, itemId, itemCount,
             (u32)enhanceLevel, moneyAmount, wcoinAmount);
    if (!vm_mysql_exec(query))
        return false;
    vm_mock_auction_notify_role(roleId, listingId, VM_MOCK_AUCTION_EVT_CLAIM, 0);
    return true;
}

static u32 vm_mock_auction_pending_claim_count(u32 roleId)
{
    char query[192];
    u32 count = 0;
    if (roleId == 0)
        return 0;
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM server_auction_claims "
             "WHERE role_id=%u AND claimed=0",
             roleId);
    (void)vm_mysql_query(query, vm_mock_auction_u32_row, &count);
    return count;
}

typedef struct
{
    vm_mock_auction_listing_row *rows;
    u32 cap;
    u32 count;
    u32 now;
} vm_mock_auction_list_ctx;

static bool vm_mock_auction_listing_row_cb(void *contextValue, unsigned int columnCount,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_mock_auction_list_ctx *ctx = (vm_mock_auction_list_ctx *)contextValue;
    vm_mock_auction_listing_row *row;
    u32 tmp = 0;

    (void)columnCount;
    if (ctx == NULL || ctx->rows == NULL || ctx->count >= ctx->cap || values == NULL ||
        lengths == NULL)
        return false;
    row = &ctx->rows[ctx->count];
    memset(row, 0, sizeof(*row));
    if (!values[0] || !vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        return false;
    row->listingId = tmp;
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        row->sellerRoleId = tmp;
    if (values[2] && lengths[2] > 0)
    {
        u32 n = lengths[2] < sizeof(row->sellerName) - 1
                    ? (u32)lengths[2]
                    : (u32)sizeof(row->sellerName) - 1;
        memcpy(row->sellerName, values[2], n);
        row->sellerName[n] = 0;
    }
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        row->itemId = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        row->itemCount = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        row->enhanceLevel = (u16)tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        row->startPrice = tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        row->currentPrice = tmp;
    if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
        row->currency = (u8)tmp;
    if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
        row->feePercent = (u8)tmp;
    if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
        row->durationTier = (u8)tmp;
    if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
        row->status = (u8)tmp;
    if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
        row->highBidderRoleId = tmp;
    if (values[13] && vm_mock_mysql_parse_u32(values[13], lengths[13], &tmp))
        row->expireUnix = tmp;
    if (row->expireUnix > ctx->now)
        row->remainSec = row->expireUnix - ctx->now;
    else
        row->remainSec = 0;
    ctx->count += 1;
    return true;
}

static bool vm_mock_auction_load_listing(u32 listingId, vm_mock_auction_listing_row *out)
{
    char query[384];
    vm_mock_auction_list_ctx ctx;
    u32 now = (u32)time(NULL);

    if (out == NULL || listingId == 0)
        return false;
    memset(&ctx, 0, sizeof(ctx));
    ctx.rows = out;
    ctx.cap = 1;
    ctx.now = now;
    snprintf(query, sizeof(query),
             "SELECT listing_id,seller_role_id,seller_name,item_id,item_count,"
             "enhance_level,start_price,current_price,currency,fee_percent,"
             "duration_tier,status,high_bidder_role_id,expire_unix "
             "FROM server_auction_listings WHERE listing_id=%u LIMIT 1",
             listingId);
    (void)vm_mysql_query(query, vm_mock_auction_listing_row_cb, &ctx);
    return ctx.count == 1;
}

static bool vm_mock_auction_copy_str_row(void *contextValue, unsigned int columnCount,
                                         const char *const *values,
                                         const size_t *lengths)
{
    char *out = (char *)contextValue;
    u32 n;

    (void)columnCount;
    if (out == NULL || values == NULL || lengths == NULL || values[0] == NULL)
        return false;
    n = lengths[0] < 63 ? (u32)lengths[0] : 63;
    memcpy(out, values[0], n);
    out[n] = 0;
    return true;
}

static bool vm_mock_auction_settle_listing(u32 listingId)
{
    vm_mock_auction_listing_row row;
    char sellerAccount[64];
    char highAccount[64];
    char query[320];

    if (!vm_mock_auction_load_listing(listingId, &row) ||
        row.status != VM_MOCK_AUCTION_ST_OPEN)
        return false;

    sellerAccount[0] = 0;
    highAccount[0] = 0;
    snprintf(query, sizeof(query),
             "SELECT seller_account_id FROM server_auction_listings "
             "WHERE listing_id=%u LIMIT 1",
             listingId);
    (void)vm_mysql_query(query, vm_mock_auction_copy_str_row, sellerAccount);
    snprintf(query, sizeof(query),
             "SELECT high_bidder_account_id FROM server_auction_listings "
             "WHERE listing_id=%u LIMIT 1",
             listingId);
    (void)vm_mysql_query(query, vm_mock_auction_copy_str_row, highAccount);

    if (row.highBidderRoleId != 0 && row.currentPrice > 0)
    {
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET status=%u WHERE listing_id=%u "
                 "AND status=%u",
                 VM_MOCK_AUCTION_ST_SOLD, listingId, VM_MOCK_AUCTION_ST_OPEN);
        if (!vm_mysql_exec(query))
            return false;
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_bids SET status=%u WHERE listing_id=%u "
                 "AND bidder_role_id=%u AND status=%u",
                 VM_MOCK_AUCTION_BID_WON, listingId, row.highBidderRoleId,
                 VM_MOCK_AUCTION_BID_ACTIVE);
        (void)vm_mysql_exec(query);

        if (row.currency == VM_MOCK_AUCTION_CUR_WCOIN)
            (void)vm_mock_auction_insert_claim(
                row.sellerRoleId, sellerAccount, VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT,
                listingId, 0, 0, 0, 0, row.currentPrice);
        else
            (void)vm_mock_auction_insert_claim(
                row.sellerRoleId, sellerAccount, VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT,
                listingId, 0, 0, 0, row.currentPrice, 0);

        if (highAccount[0] != 0)
            (void)vm_mock_auction_insert_claim(
                row.highBidderRoleId, highAccount,
                VM_MOCK_AUCTION_CLAIM_WINNER_ITEM, listingId, row.itemId,
                row.itemCount, row.enhanceLevel, 0, 0);

        vm_mock_auction_notify_role(row.sellerRoleId, listingId,
                                    VM_MOCK_AUCTION_EVT_SOLD, row.currentPrice);
        vm_mock_auction_notify_role(row.highBidderRoleId, listingId,
                                    VM_MOCK_AUCTION_EVT_SOLD, row.currentPrice);
        printf("[info][mock-service] auction_sold listing=%u price=%u cur=%u "
               "seller=%u winner=%u\n",
               listingId, row.currentPrice, row.currency, row.sellerRoleId,
               row.highBidderRoleId);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET status=%u WHERE listing_id=%u "
                 "AND status=%u",
                 VM_MOCK_AUCTION_ST_EXPIRED, listingId, VM_MOCK_AUCTION_ST_OPEN);
        if (!vm_mysql_exec(query))
            return false;
        (void)vm_mock_auction_insert_claim(
            row.sellerRoleId, sellerAccount, VM_MOCK_AUCTION_CLAIM_ITEM_RETURN,
            listingId, row.itemId, row.itemCount, row.enhanceLevel, 0, 0);
        vm_mock_auction_notify_role(row.sellerRoleId, listingId,
                                    VM_MOCK_AUCTION_EVT_EXPIRED, 0);
        printf("[info][mock-service] auction_expired listing=%u seller=%u item=%u\n",
               listingId, row.sellerRoleId, row.itemId);
    }
    return true;
}

typedef struct
{
    u32 ids[VM_MOCK_AUCTION_EXPIRE_BATCH];
    u32 count;
} vm_mock_auction_id_list;

static bool vm_mock_auction_id_list_row(void *contextValue, unsigned int columnCount,
                                        const char *const *values,
                                        const size_t *lengths)
{
    vm_mock_auction_id_list *list = (vm_mock_auction_id_list *)contextValue;
    u32 id = 0;
    (void)columnCount;
    if (list == NULL || list->count >= VM_MOCK_AUCTION_EXPIRE_BATCH || values == NULL ||
        lengths == NULL || !values[0] ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &id))
        return false;
    list->ids[list->count++] = id;
    return true;
}

static void vm_mock_auction_tick(void)
{
    u32 now = (u32)time(NULL);
    vm_mock_auction_id_list list;
    char query[256];
    u32 i;

    if (now == g_vm_mock_auction_last_tick_sec)
        return;
    g_vm_mock_auction_last_tick_sec = now;
    if (!vm_mock_auction_ensure_tables())
        return;
    memset(&list, 0, sizeof(list));
    snprintf(query, sizeof(query),
             "SELECT listing_id FROM server_auction_listings "
             "WHERE status=%u AND expire_unix>0 AND expire_unix<=%u "
             "ORDER BY expire_unix ASC LIMIT %u",
             VM_MOCK_AUCTION_ST_OPEN, now, VM_MOCK_AUCTION_EXPIRE_BATCH);
    (void)vm_mysql_query(query, vm_mock_auction_id_list_row, &list);
    for (i = 0; i < list.count; ++i)
        (void)vm_mock_auction_settle_listing(list.ids[i]);
}

static u32 vm_mock_auction_finish_packet(u8 *out, u32 pos, u8 objectCount)
{
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = objectCount;
    return pos;
}

static u32 vm_net_mock_build_auction_op_result(u8 *out, u32 outCap, u8 subtype,
                                               u32 result, u32 listingId,
                                               const char *message)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < 5)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                     subtype, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "listingId", listingId) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message",
                                       message ? message : ""))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_auction_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_auction_status_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 blink = 0;
    u32 pending = 0;
    u8 tier;
    u8 fees[4];

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_STATUS)
        return 0;
    (void)vm_mock_auction_tick();
    (void)vm_mock_auction_load_config(false);
    if (role != NULL)
        pending = vm_mock_auction_pending_claim_count(role->roleId);
    blink = pending > 0 ? 1 : 0;
    for (tier = 0; tier < 4; ++tier)
        fees[tier] = vm_mock_auction_fee_percent(tier);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                     VM_MOCK_AUCTION_WT_STATUS, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "enabled",
                                    g_vm_mock_auction_config.enabled) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "blink", blink) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pendingClaims", pending) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "fee0", fees[0]) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "fee1", fees[1]) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "fee2", fees[2]) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "fee3", fees[3]) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "minBidStep",
                                    g_vm_mock_auction_config.minBidStep) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "currencyMask",
                                    g_vm_mock_auction_config.currencyMask))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_auction_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_auction_browse_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap, u8 subtype)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 pageIndex = 0;
    u8 blob[3072];
    u32 blobLen = 0;
    u32 i;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_auction_listing_row rows[VM_MOCK_AUCTION_PAGE_SIZE];
    vm_mock_auction_list_ctx ctx;
    char query[512];
    u32 now = (u32)time(NULL);
    char itemName[48];

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != subtype)
        return 0;
    (void)vm_mock_auction_tick();
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "pageIndex", &pageIndex);
    memset(&ctx, 0, sizeof(ctx));
    ctx.rows = rows;
    ctx.cap = VM_MOCK_AUCTION_PAGE_SIZE;
    ctx.now = now;
    if (subtype == VM_MOCK_AUCTION_WT_MINE)
    {
        if (role == NULL)
            return vm_net_mock_build_auction_op_result(out, outCap, subtype, 2, 0,
                                                       "no-role");
        snprintf(query, sizeof(query),
                 "SELECT listing_id,seller_role_id,seller_name,item_id,item_count,"
                 "enhance_level,start_price,current_price,currency,fee_percent,"
                 "duration_tier,status,high_bidder_role_id,expire_unix "
                 "FROM server_auction_listings WHERE seller_role_id=%u "
                 "AND status=%u ORDER BY listing_id DESC LIMIT %u OFFSET %u",
                 role->roleId, VM_MOCK_AUCTION_ST_OPEN, VM_MOCK_AUCTION_PAGE_SIZE,
                 pageIndex * VM_MOCK_AUCTION_PAGE_SIZE);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "SELECT listing_id,seller_role_id,seller_name,item_id,item_count,"
                 "enhance_level,start_price,current_price,currency,fee_percent,"
                 "duration_tier,status,high_bidder_role_id,expire_unix "
                 "FROM server_auction_listings WHERE status=%u "
                 "ORDER BY expire_unix ASC LIMIT %u OFFSET %u",
                 VM_MOCK_AUCTION_ST_OPEN, VM_MOCK_AUCTION_PAGE_SIZE,
                 pageIndex * VM_MOCK_AUCTION_PAGE_SIZE);
    }
    (void)vm_mysql_query(query, vm_mock_auction_listing_row_cb, &ctx);
    for (i = 0; i < ctx.count; ++i)
    {
        vm_mock_auction_item_name(rows[i].itemId, itemName, sizeof(itemName));
        if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].listingId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].sellerRoleId) ||
            !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen, rows[i].sellerName) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].itemId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].itemCount) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].enhanceLevel) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].startPrice) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].currentPrice) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].currency) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].feePercent) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].durationTier) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].remainSec) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen,
                                     rows[i].highBidderRoleId) ||
            !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen, itemName))
            break;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                     subtype, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "count", i) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pageIndex", pageIndex) ||
        !vm_net_mock_put_object_entry(out, outCap, &pos, "rows", blob, (u16)blobLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_auction_finish_packet(out, pos, 1);
}

static u32 vm_net_mock_build_auction_create_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_backpack_item_state *bag = NULL;
    u32 bagSeq = 0;
    u32 count = 0;
    u32 startPrice = 0;
    u32 currency = 0;
    u32 durationTier = 0;
    u32 fee = 0;
    u8 feePct = 0;
    u32 expireUnix = 0;
    u32 listingId = 0;
    u32 activeCount = 0;
    char accountEsc[160];
    char nameHex[96];
    char query[1024];
    u32 remaining = 0;
    u16 enhance = 0;
    u32 itemId = 0;
    vm_net_mock_role_state before;

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_CREATE)
        return 0;
    (void)vm_mock_auction_tick();
    (void)vm_mock_auction_load_config(false);
    if (!g_vm_mock_auction_config.enabled)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   2, 0, "disabled");
    if (role == NULL || session == NULL)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   3, 0, "no-role");
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "bagSeq", &bagSeq);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "count", &count);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "startPrice", &startPrice);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "currency", &currency);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "durationTier", &durationTier);
    if (bagSeq == 0 || startPrice == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   4, 0, "bad-args");
    if (durationTier > 3)
        durationTier = 3;
    if (currency > 1)
        currency = 0;
    if ((g_vm_mock_auction_config.currencyMask & (1u << currency)) == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   5, 0, "currency");
    bag = vm_net_mock_role_find_backpack_item(role, 0, (u16)bagSeq);
    if (bag == NULL || bag->count == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   6, 0, "no-item");
    itemId = bag->itemId;
    enhance = bag->enhanceLevel;
    if (count == 0 || count > bag->count)
        count = bag->count;
    if (vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
        count = 1;
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM server_auction_listings "
             "WHERE seller_role_id=%u AND status=%u",
             role->roleId, VM_MOCK_AUCTION_ST_OPEN);
    (void)vm_mysql_query(query, vm_mock_auction_u32_row, &activeCount);
    if (activeCount >= g_vm_mock_auction_config.maxActiveListings)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   7, 0, "limit");
    feePct = vm_mock_auction_fee_percent((u8)durationTier);
    fee = vm_mock_auction_calc_fee(startPrice, (u8)durationTier);
    before = *role;
    if (!vm_mock_auction_charge(role, (u8)currency, fee, "auction-list-fee"))
    {
        *role = before;
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   8, 0, "fee");
    }
    if (!vm_net_mock_role_consume_backpack_item(role, itemId, (u16)bagSeq, count,
                                                &remaining))
    {
        (void)vm_mock_auction_credit(role, (u8)currency, fee, "auction-list-rollback");
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   9, 0, "consume");
    }
    vm_net_mock_role_mark_inventory_dirty("auction-list");
    if (!vm_net_mock_role_db_save_role("auction-list", role->roleId))
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-list-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   10, 0, "save");
    }
    expireUnix = (u32)time(NULL) + vm_mock_auction_duration_sec((u8)durationTier);
    if (vm_mysql_hex_encode(session->accountId, strlen(session->accountId), accountEsc,
                            sizeof(accountEsc)) == 0 ||
        vm_mysql_hex_encode(role->name, strlen(role->name), nameHex, sizeof(nameHex)) ==
            0)
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-list-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   11, 0, "encode");
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_auction_listings ("
             "seller_role_id,seller_account_id,seller_name,item_id,item_count,"
             "enhance_level,start_price,current_price,currency,fee_paid,fee_percent,"
             "duration_tier,status,high_bidder_role_id,high_bidder_account_id,"
             "expire_unix) VALUES ("
             "%u,CAST(X'%s' AS CHAR),UNHEX('%s'),%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,0,'',"
             "%u)",
             role->roleId, accountEsc, nameHex, itemId, count, (u32)enhance, startPrice,
             startPrice, currency, fee, (u32)feePct, durationTier,
             VM_MOCK_AUCTION_ST_OPEN, expireUnix);
    if (!vm_mysql_exec(query))
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-list-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   12, 0, "insert");
    }
    listingId = 0;
    (void)vm_mysql_query("SELECT LAST_INSERT_ID()", vm_mock_auction_u32_row, &listingId);
    session->backpackListResyncPending = true;
    printf("[info][mock-service] auction_list listing=%u role=%u item=%u x%u "
           "price=%u cur=%u fee=%u(%u%%) tier=%u\n",
           listingId, role->roleId, itemId, count, startPrice, currency, fee, feePct,
           durationTier);
    return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE, 1,
                                               listingId, "ok");
}

static u32 vm_net_mock_build_auction_bid_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 listingId = 0;
    u32 amount = 0;
    vm_mock_auction_listing_row row;
    char accountEsc[160];
    char prevAccount[64];
    char query[768];
    u32 minNeed = 0;
    vm_net_mock_role_state before;

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_BID)
        return 0;
    (void)vm_mock_auction_tick();
    (void)vm_mock_auction_load_config(false);
    if (role == NULL || session == NULL)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 2,
                                                   0, "no-role");
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "listingId", &listingId);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "amount", &amount);
    if (listingId == 0 || amount == 0 ||
        !vm_mock_auction_load_listing(listingId, &row) ||
        row.status != VM_MOCK_AUCTION_ST_OPEN)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 3,
                                                   listingId, "not-open");
    if (row.sellerRoleId == role->roleId)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 4,
                                                   listingId, "self");
    if (row.highBidderRoleId == role->roleId)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 5,
                                                   listingId, "already-high");
    minNeed = row.currentPrice;
    if (row.highBidderRoleId != 0)
        minNeed = row.currentPrice + g_vm_mock_auction_config.minBidStep;
    if (amount < minNeed)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 6,
                                                   listingId, "low");
    before = *role;
    if (!vm_mock_auction_charge(role, row.currency, amount, "auction-bid"))
    {
        *role = before;
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 7,
                                                   listingId, "balance");
    }
    prevAccount[0] = 0;
    if (row.highBidderRoleId != 0)
    {
        snprintf(query, sizeof(query),
                 "SELECT high_bidder_account_id FROM server_auction_listings "
                 "WHERE listing_id=%u LIMIT 1",
                 listingId);
        (void)vm_mysql_query(query, vm_mock_auction_copy_str_row, prevAccount);
        if (prevAccount[0] != 0)
        {
            if (row.currency == VM_MOCK_AUCTION_CUR_WCOIN)
                (void)vm_mock_auction_insert_claim(
                    row.highBidderRoleId, prevAccount, VM_MOCK_AUCTION_CLAIM_BID_REFUND,
                    listingId, 0, 0, 0, 0, row.currentPrice);
            else
                (void)vm_mock_auction_insert_claim(
                    row.highBidderRoleId, prevAccount, VM_MOCK_AUCTION_CLAIM_BID_REFUND,
                    listingId, 0, 0, 0, row.currentPrice, 0);
            snprintf(query, sizeof(query),
                     "UPDATE server_auction_bids SET status=%u WHERE listing_id=%u "
                     "AND bidder_role_id=%u AND status=%u",
                     VM_MOCK_AUCTION_BID_OUTBID, listingId, row.highBidderRoleId,
                     VM_MOCK_AUCTION_BID_ACTIVE);
            (void)vm_mysql_exec(query);
            vm_mock_auction_notify_role(row.highBidderRoleId, listingId,
                                        VM_MOCK_AUCTION_EVT_OUTBID, row.currentPrice);
        }
    }
    if (vm_mysql_hex_encode(session->accountId, strlen(session->accountId), accountEsc,
                            sizeof(accountEsc)) == 0)
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-bid-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 8,
                                                   listingId, "encode");
    }
    snprintf(query, sizeof(query),
             "UPDATE server_auction_listings SET current_price=%u,"
             "high_bidder_role_id=%u,high_bidder_account_id=CAST(X'%s' AS CHAR) "
             "WHERE listing_id=%u AND status=%u",
             amount, role->roleId, accountEsc, listingId, VM_MOCK_AUCTION_ST_OPEN);
    if (!vm_mysql_exec(query))
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-bid-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 9,
                                                   listingId, "update");
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_auction_bids ("
             "listing_id,bidder_role_id,bidder_account_id,amount,currency,status) "
             "VALUES (%u,%u,CAST(X'%s' AS CHAR),%u,%u,%u)",
             listingId, role->roleId, accountEsc, amount, (u32)row.currency,
             VM_MOCK_AUCTION_BID_ACTIVE);
    (void)vm_mysql_exec(query);
    printf("[info][mock-service] auction_bid listing=%u role=%u amount=%u cur=%u\n",
           listingId, role->roleId, amount, row.currency);
    return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 1,
                                               listingId, "ok");
}

static u32 vm_net_mock_build_auction_cancel_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 listingId = 0;
    vm_mock_auction_listing_row row;
    char query[256];
    u16 seq = 0;

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_CANCEL)
        return 0;
    (void)vm_mock_auction_tick();
    if (role == NULL || session == NULL)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   2, 0, "no-role");
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "listingId", &listingId);
    if (listingId == 0 || !vm_mock_auction_load_listing(listingId, &row) ||
        row.status != VM_MOCK_AUCTION_ST_OPEN)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   3, listingId, "not-open");
    if (row.sellerRoleId != role->roleId)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   4, listingId, "not-owner");
    if (row.highBidderRoleId != 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   5, listingId, "has-bid");
    snprintf(query, sizeof(query),
             "UPDATE server_auction_listings SET status=%u WHERE listing_id=%u "
             "AND status=%u",
             VM_MOCK_AUCTION_ST_CANCELLED, listingId, VM_MOCK_AUCTION_ST_OPEN);
    if (!vm_mysql_exec(query))
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   6, listingId, "update");
    if (!vm_net_mock_role_add_backpack_item_to_role(role, row.itemId, row.itemCount,
                                                    row.enhanceLevel, &seq,
                                                    "auction-cancel"))
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   7, listingId, "bag-full");
    (void)vm_net_mock_role_db_save_role("auction-cancel", role->roleId);
    session->backpackListResyncPending = true;
    return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL, 1,
                                               listingId, "ok");
}

typedef struct
{
    vm_mock_auction_claim_row *rows;
    u32 cap;
    u32 count;
} vm_mock_auction_claim_ctx;

static bool vm_mock_auction_claim_row_cb(void *contextValue, unsigned int columnCount,
                                         const char *const *values,
                                         const size_t *lengths)
{
    vm_mock_auction_claim_ctx *ctx = (vm_mock_auction_claim_ctx *)contextValue;
    vm_mock_auction_claim_row *row;
    u32 tmp = 0;

    (void)columnCount;
    if (ctx == NULL || ctx->rows == NULL || ctx->count >= ctx->cap || values == NULL ||
        lengths == NULL)
        return false;
    row = &ctx->rows[ctx->count];
    memset(row, 0, sizeof(*row));
    if (!values[0] || !vm_mock_mysql_parse_u32(values[0], lengths[0], &tmp))
        return false;
    row->claimId = tmp;
    if (values[1] && vm_mock_mysql_parse_u32(values[1], lengths[1], &tmp))
        row->kind = (u8)tmp;
    if (values[2] && vm_mock_mysql_parse_u32(values[2], lengths[2], &tmp))
        row->listingId = tmp;
    if (values[3] && vm_mock_mysql_parse_u32(values[3], lengths[3], &tmp))
        row->itemId = tmp;
    if (values[4] && vm_mock_mysql_parse_u32(values[4], lengths[4], &tmp))
        row->itemCount = tmp;
    if (values[5] && vm_mock_mysql_parse_u32(values[5], lengths[5], &tmp))
        row->enhanceLevel = (u16)tmp;
    if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
        row->moneyAmount = tmp;
    if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
        row->wcoinAmount = tmp;
    ctx->count += 1;
    return true;
}

static u32 vm_net_mock_build_auction_claim_response(const u8 *request, u32 requestLen,
                                                    u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_auction_claim_row rows[16];
    vm_mock_auction_claim_ctx ctx;
    char query[384];
    u32 claimed = 0;
    u32 failed = 0;
    u32 i;
    u8 blob[1024];
    u32 blobLen = 0;
    u32 pending = 0;

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_CLAIM)
        return 0;
    (void)vm_mock_auction_tick();
    if (role == NULL || session == NULL)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CLAIM,
                                                   2, 0, "no-role");
    memset(&ctx, 0, sizeof(ctx));
    ctx.rows = rows;
    ctx.cap = 16;
    snprintf(query, sizeof(query),
             "SELECT claim_id,kind,listing_id,item_id,item_count,enhance_level,"
             "money_amount,wcoin_amount FROM server_auction_claims "
             "WHERE role_id=%u AND claimed=0 ORDER BY claim_id ASC LIMIT 16",
             role->roleId);
    (void)vm_mysql_query(query, vm_mock_auction_claim_row_cb, &ctx);
    for (i = 0; i < ctx.count; ++i)
    {
        bool ok = true;
        u16 seq = 0;
        if (rows[i].kind == VM_MOCK_AUCTION_CLAIM_ITEM_RETURN ||
            rows[i].kind == VM_MOCK_AUCTION_CLAIM_WINNER_ITEM)
        {
            if (rows[i].itemId == 0 || rows[i].itemCount == 0 ||
                !vm_net_mock_role_add_backpack_item_to_role(
                    role, rows[i].itemId, rows[i].itemCount, rows[i].enhanceLevel,
                    &seq, "auction-claim-item"))
                ok = false;
        }
        else if (rows[i].kind == VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT ||
                 rows[i].kind == VM_MOCK_AUCTION_CLAIM_BID_REFUND)
        {
            if (rows[i].moneyAmount)
            {
                if (!vm_mock_auction_credit(role, VM_MOCK_AUCTION_CUR_COPPER,
                                            rows[i].moneyAmount, "auction-claim-money"))
                    ok = false;
            }
            if (ok && rows[i].wcoinAmount)
            {
                if (!vm_mock_auction_credit(role, VM_MOCK_AUCTION_CUR_WCOIN,
                                            rows[i].wcoinAmount, "auction-claim-wcoin"))
                    ok = false;
            }
        }
        else
            ok = false;
        if (!ok)
        {
            failed += 1;
            continue;
        }
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_claims SET claimed=1 WHERE claim_id=%u "
                 "AND claimed=0",
                 rows[i].claimId);
        if (!vm_mysql_exec(query))
        {
            failed += 1;
            continue;
        }
        claimed += 1;
        if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].claimId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].kind) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].listingId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].itemId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].itemCount) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen,
                                     rows[i].moneyAmount) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].wcoinAmount))
            break;
    }
    if (claimed)
    {
        (void)vm_net_mock_role_db_save_role("auction-claim", role->roleId);
        session->backpackListResyncPending = true;
    }
    pending = vm_mock_auction_pending_claim_count(role->roleId);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                     VM_MOCK_AUCTION_WT_CLAIM, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", claimed ? 1 : 2) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "claimed", claimed) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "failed", failed) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pendingClaims", pending) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "blink", pending ? 1 : 0) ||
        !vm_net_mock_put_object_entry(out, outCap, &pos, "rows", blob, (u16)blobLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_auction_finish_packet(out, pos, 1);
}

static bool vm_net_mock_append_auction_event_object(u8 *out, u32 outCap, u32 *pos)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 objectStart = 0;
    u32 i;
    vm_mock_auction_notice notice;
    bool found = false;
    u32 blink = 0;

    if (out == NULL || pos == NULL || session == NULL)
        return false;
    (void)vm_mock_auction_tick();
    for (i = 0; i < g_vm_mock_auction_notice_count; ++i)
    {
        if (g_vm_mock_auction_notices[i].clientId == session->clientId)
        {
            notice = g_vm_mock_auction_notices[i];
            memmove(&g_vm_mock_auction_notices[i], &g_vm_mock_auction_notices[i + 1],
                    sizeof(g_vm_mock_auction_notices[0]) *
                        (g_vm_mock_auction_notice_count - i - 1));
            g_vm_mock_auction_notice_count -= 1;
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    if (session->onlineRoleId)
        blink = vm_mock_auction_pending_claim_count(session->onlineRoleId) > 0 ? 1 : 0;
    if (notice.blink)
        blink = 1;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                     VM_MOCK_AUCTION_WT_EVENT, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "listingId", notice.listingId) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "eventKind", notice.eventKind) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "amount", notice.amount) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "blink", blink))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_auction_protocol_response(const u8 *request, u32 requestLen,
                                                       u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND)
        return 0;
    if (object.subtype == VM_MOCK_AUCTION_WT_STATUS)
        return vm_net_mock_build_auction_status_response(request, requestLen, out,
                                                         outCap);
    if (object.subtype == VM_MOCK_AUCTION_WT_LIST ||
        object.subtype == VM_MOCK_AUCTION_WT_MINE)
        return vm_net_mock_build_auction_browse_response(request, requestLen, out,
                                                         outCap, object.subtype);
    if (object.subtype == VM_MOCK_AUCTION_WT_CREATE)
        return vm_net_mock_build_auction_create_response(request, requestLen, out,
                                                         outCap);
    if (object.subtype == VM_MOCK_AUCTION_WT_BID)
        return vm_net_mock_build_auction_bid_response(request, requestLen, out, outCap);
    if (object.subtype == VM_MOCK_AUCTION_WT_CANCEL)
        return vm_net_mock_build_auction_cancel_response(request, requestLen, out,
                                                         outCap);
    if (object.subtype == VM_MOCK_AUCTION_WT_CLAIM)
        return vm_net_mock_build_auction_claim_response(request, requestLen, out,
                                                        outCap);
    return 0;
}
