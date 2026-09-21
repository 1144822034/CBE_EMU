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
    VM_MOCK_AUCTION_WT_BUYOUT = 9,
    /* Pending-claims preview (does not grant). */
    VM_MOCK_AUCTION_WT_CLAIMS_LIST = 10,
    /* Pixi: issue buyout captcha for listing (anti-snipe). */
    VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE = 11,
    VM_MOCK_AUCTION_CAPTCHA_LEN = 4,
    /* Min human delay between challenge and buyout accept (ms). */
    VM_MOCK_AUCTION_CAPTCHA_MIN_MS = 1500,
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
    VM_MOCK_AUCTION_EXPIRE_BATCH = 32,
    /* Server-wide cap on concurrent OPEN listings. */
    VM_MOCK_AUCTION_MAX_GLOBAL_OPEN = 50,
    /* Per-seller cap on concurrent OPEN listings. */
    VM_MOCK_AUCTION_DEFAULT_MAX_ACTIVE = 2
};

typedef struct
{
    u8 enabled;
    u8 feePercentBase;
    u8 feePercentPerTier;
    u32 minBidStep;
    u8 currencyMask;
    u32 maxActiveListings;
    /* 1: reject 玄晶 / 任务物品类别 on list. 0: no category gate. */
    u8 categoryGate;
} vm_mock_auction_config;

typedef struct
{
    u32 clientId;
    u32 listingId;
    u8 eventKind;
    u32 amount;
    u8 blink;
} vm_mock_auction_notice;

/* Per-role buyout captcha (Pixi anti-instant-snipe). */
typedef struct
{
    u32 roleId;
    u32 listingId;
    char code[VM_MOCK_AUCTION_CAPTCHA_LEN + 1];
    u32 issuedMs;
} vm_mock_auction_buyout_captcha;

static vm_mock_auction_buyout_captcha g_auctionBuyoutCaptcha;

typedef struct
{
    u32 listingId;
    u32 sellerRoleId;
    char sellerName[32];
    u32 itemId;
    u32 itemCount;
    u16 enhanceLevel;
    u32 itemInstanceId;
    vm_net_mock_enhance_wash_state enhanceWash;
    u8 gemReflectLevel;
    u8 gemComboLevel;
    u8 gemReviveLevel;
    u32 startPrice;
    u32 currentPrice;
    u32 buyoutPrice;
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
    u32 itemInstanceId;
    vm_net_mock_enhance_wash_state enhanceWash;
    u8 gemReflectLevel;
    u8 gemComboLevel;
    u8 gemReviveLevel;
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
    cfg->maxActiveListings = VM_MOCK_AUCTION_DEFAULT_MAX_ACTIVE;
    cfg->categoryGate = 0;
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
            "max_active_listings INT UNSIGNED NOT NULL DEFAULT 2,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(id)) ENGINE=InnoDB"))
        return false;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_auction_listings ("
            "listing_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "seller_role_id INT UNSIGNED NOT NULL,"
            "seller_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "seller_name VARBINARY(32) NOT NULL DEFAULT '',"
            "item_id INT UNSIGNED NOT NULL,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 1,"
            "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "enhance_wash VARCHAR(96) NOT NULL DEFAULT "
            "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',"
            "start_price INT UNSIGNED NOT NULL DEFAULT 0,"
            "current_price INT UNSIGNED NOT NULL DEFAULT 0,"
            "buyout_price INT UNSIGNED NOT NULL DEFAULT 0,"
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
            "KEY idx_auction_listings_seller(seller_role_id,status)) ENGINE=InnoDB"))
        return false;
    /* Existing DBs created before buyout: ignore duplicate-column errors. */
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_listings ADD COLUMN buyout_price "
        "INT UNSIGNED NOT NULL DEFAULT 0 AFTER current_price");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_listings ADD COLUMN enhance_wash "
        "VARCHAR(96) NOT NULL DEFAULT "
        "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0' AFTER enhance_level");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_claims ADD COLUMN enhance_wash "
        "VARCHAR(96) NOT NULL DEFAULT "
        "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0' AFTER enhance_level");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_config ADD COLUMN category_gate "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_listings ADD COLUMN item_instance_id "
        "INT UNSIGNED NOT NULL DEFAULT 0 AFTER enhance_level");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_claims ADD COLUMN item_instance_id "
        "INT UNSIGNED NOT NULL DEFAULT 0 AFTER enhance_level");
    /* Socketed gear must round-trip; empty sockets stay 0. */
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_listings ADD COLUMN gem_reflect "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER enhance_wash");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_listings ADD COLUMN gem_combo "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER gem_reflect");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_listings ADD COLUMN gem_revive "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER gem_combo");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_claims ADD COLUMN gem_reflect "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER enhance_wash");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_claims ADD COLUMN gem_combo "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER gem_reflect");
    (void)vm_mysql_exec(
        "ALTER TABLE server_auction_claims ADD COLUMN gem_revive "
        "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER gem_combo");
    (void)vm_net_mock_enhance_wash_record_ensure_table();
    if (!vm_mysql_exec(
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
            "KEY idx_auction_bids_bidder(bidder_role_id)) ENGINE=InnoDB"))
        return false;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_auction_claims ("
            "claim_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "role_id INT UNSIGNED NOT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "kind TINYINT UNSIGNED NOT NULL,"
            "listing_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "enhance_wash VARCHAR(96) NOT NULL DEFAULT "
            "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',"
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
        cfg->maxActiveListings =
            tmp ? tmp : (u32)VM_MOCK_AUCTION_DEFAULT_MAX_ACTIVE;
    if (values[6] && lengths[6])
        cfg->categoryGate = (u8)(values[6][0] == '0' ? 0 : 1);
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
        "currency_mask,max_active_listings) VALUES (1,1,2,2,1,3,2) "
        "ON DUPLICATE KEY UPDATE "
        "fee_percent_base=2,fee_percent_per_tier=2,"
        "max_active_listings=2");
    (void)vm_mysql_query(
        "SELECT enabled,fee_percent_base,fee_percent_per_tier,min_bid_step,"
        "currency_mask,max_active_listings,category_gate "
        "FROM server_auction_config WHERE id=1 LIMIT 1",
        vm_mock_auction_config_row, &g_vm_mock_auction_config);
    g_vm_mock_auction_config_loaded = true;
    return true;
}

const vm_mock_auction_config *vm_net_mock_auction_config_get(void)
{
    (void)vm_mock_auction_load_config(false);
    return &g_vm_mock_auction_config;
}

bool vm_mock_auction_admin_save_config(u8 enabled, u8 categoryGate)
{
    char query[512];

    if (!vm_mock_auction_ensure_tables())
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO server_auction_config ("
             "id,enabled,fee_percent_base,fee_percent_per_tier,min_bid_step,"
             "currency_mask,max_active_listings,category_gate) "
             "VALUES (1,%u,2,2,1,3,2,%u) "
             "ON DUPLICATE KEY UPDATE enabled=VALUES(enabled),"
             "category_gate=VALUES(category_gate)",
             enabled ? 1u : 0u, categoryGate ? 1u : 0u);
    if (!vm_mysql_exec(query))
        return false;
    g_vm_mock_auction_config_loaded = false;
    return vm_mock_auction_load_config(true);
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

/** Bid raise step = floor(startPrice / 10), at least 1. */
static u32 vm_mock_auction_bid_step(u32 startPrice)
{
    u32 step = startPrice / 10u;
    return step == 0 ? 1u : step;
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

/* UPDATE/INSERT success alone is not enough — 0-row updates still return true. */
static bool vm_mock_auction_exec_affected(const char *sql, u32 *affectedOut)
{
    u32 affected = 0;

    if (affectedOut != NULL)
        *affectedOut = 0;
    if (sql == NULL || !vm_mysql_exec(sql))
        return false;
    if (!vm_mysql_query("SELECT ROW_COUNT()", vm_mock_auction_u32_row, &affected))
        return false;
    if (affectedOut != NULL)
        *affectedOut = affected;
    return true;
}

static bool vm_mock_auction_tx_begin(void)
{
    return vm_mysql_exec("START TRANSACTION");
}

static bool vm_mock_auction_tx_commit(void)
{
    return vm_mysql_exec("COMMIT");
}

static void vm_mock_auction_tx_rollback(void)
{
    (void)vm_mysql_exec("ROLLBACK");
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

static bool vm_mock_auction_wash_sql_text(const vm_net_mock_enhance_wash_state *wash,
                                          char *out, size_t outCap)
{
    size_t i;
    if (out == NULL || outCap < 8)
        return false;
    if (!vm_net_mock_enhance_wash_encode(wash, out, outCap) || out[0] == 0)
        snprintf(out, outCap, "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    for (i = 0; out[i] != 0; ++i)
    {
        char c = out[i];
        if (!((c >= '0' && c <= '9') || c == ',' || c == ';'))
        {
            snprintf(out, outCap, "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
            return false;
        }
    }
    return true;
}

static u32 vm_mock_auction_bag_item_total(const vm_net_mock_role_state *role,
                                          u32 itemId)
{
    u32 total = 0;
    u32 n = 0;
    u32 i = 0;

    if (role == NULL || itemId == 0)
        return 0;
    n = vm_net_mock_role_backpack_count(role);
    for (i = 0; i < n; ++i)
    {
        if (role->backpackItems[i].itemId == itemId)
            total += role->backpackItems[i].count;
    }
    return total;
}

/*
 * Restore auction item to bag.
 * 802/803: listing/claim item_count is raw HP/MP reservoir (same as warehouse
 * retrieve). add_backpack_item_to_role treats count as flask *units* and would
 * multiply by 50000 again → FLASK_FULL / false bag-full on cancel/claim.
 * 834/839: item_count is remaining durability on one pass row. add_backpack
 * treats count as NEW full-pass quantity → durability 50 becomes 50×50.
 */
static void vm_mock_auction_stamp_bag_gems(vm_net_mock_role_state *role, u16 seq,
                                              u8 gemReflect, u8 gemCombo, u8 gemRevive)
{
    vm_net_mock_backpack_item_state *bagRow;

    if (role == NULL || seq == 0 || (gemReflect == 0 && gemCombo == 0 && gemRevive == 0))
        return;
    bagRow = vm_net_mock_role_find_backpack_item(role, 0, seq);
    if (bagRow == NULL)
        return;
    bagRow->gemReflectLevel = gemReflect;
    bagRow->gemComboLevel = gemCombo;
    bagRow->gemReviveLevel = gemRevive;
}

static bool vm_mock_auction_return_item_to_bag(
    vm_net_mock_role_state *role, u32 itemId, u32 itemCount, u16 enhanceLevel,
    u32 itemInstanceId, const vm_net_mock_enhance_wash_state *wash, u16 *seqOut,
    const char *reason, u8 gemReflect, u8 gemCombo, u8 gemRevive)
{
    u16 seq = 0;
    bool ok = false;
    u32 bagBefore = 0;
    u32 bagAfter = 0;
    bool durablePass = false;

    if (seqOut)
        *seqOut = 0;
    if (role == NULL || itemId == 0 || itemCount == 0)
        return false;
    durablePass = vm_net_mock_backpack_item_id_is_durable_mall_pass(itemId);
    bagBefore = vm_mock_auction_bag_item_total(role, itemId);
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(itemId))
        ok = vm_net_mock_role_grant_flask_reservoir_points(role, itemId, itemCount,
                                                           &seq, reason);
    else if (durablePass)
        ok = vm_net_mock_role_grant_durable_pass_remaining(role, itemId, itemCount,
                                                            &seq, reason);
    else
        ok = vm_net_mock_role_add_backpack_item_to_role(role, itemId, itemCount,
                                                        enhanceLevel, &seq, reason,
                                                        wash, itemInstanceId);
    if (!ok)
        return false;
    bagAfter = vm_mock_auction_bag_item_total(role, itemId);
    printf("[info][mock-service] auction_return_item reason=%s role=%u item=%u "
           "grant=%u bag_before=%u bag_after=%u seq=%u "
           "evidence=stack-merge-not-set-overwrite\n",
           reason ? reason : "-", role->roleId, itemId, itemCount, bagBefore,
           bagAfter, (u32)seq);
    if (!vm_net_mock_backpack_item_id_uses_reservoir_count(itemId) &&
        !durablePass &&
        vm_net_mock_find_equipment_catalog_item(itemId) == NULL &&
        bagAfter < bagBefore + itemCount)
    {
        printf("[warn][mock-service] auction_return_item_short reason=%s role=%u "
               "item=%u grant=%u bag_before=%u bag_after=%u "
               "evidence=expected-bag_before+grant\n",
               reason ? reason : "-", role->roleId, itemId, itemCount, bagBefore,
               bagAfter);
    }
    if (durablePass && bagAfter != bagBefore + itemCount)
    {
        printf("[warn][mock-service] auction_return_durable_pass_mismatch "
               "reason=%s role=%u item=%u grant=%u bag_before=%u bag_after=%u "
               "evidence=expected-one-row-remaining-sum\n",
               reason ? reason : "-", role->roleId, itemId, itemCount, bagBefore,
               bagAfter);
    }
    if (seq != 0)
        vm_mock_auction_stamp_bag_gems(role, seq, gemReflect, gemCombo, gemRevive);
    if (seqOut)
        *seqOut = seq;
    return true;
}

/* Pixi claim/cancel: same-packet 17/1+30/21(+7/11) so openBag is not the only
 * authority path after grant (avoids stale client bag if follow-up fails). */
static u8 vm_mock_auction_append_pixi_bag_objects(u8 *out, u32 outCap, u32 *pos,
                                                   u8 objectCount)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 gridCount = 0;
    bool appendedReservoir = false;

    if (out == NULL || pos == NULL || role == NULL)
        return objectCount;
    if (!vm_net_mock_append_backpack_items_object(out, outCap, pos))
        return objectCount;
    objectCount = (u8)(objectCount + 1);
    gridCount = vm_net_mock_role_backpack_client_grid_count(role);
    if (gridCount == 0)
        return objectCount;
    if (!vm_net_mock_append_backpack_grid_object(out, outCap, pos))
        return objectCount;
    objectCount = (u8)(objectCount + 1);
    if (!vm_net_mock_append_backpack_reservoir_counts_object(out, outCap, pos,
                                                             &appendedReservoir))
        return objectCount;
    if (appendedReservoir)
        objectCount = (u8)(objectCount + 1);
    return objectCount;
}

static bool vm_mock_auction_insert_claim_ex(u32 roleId, const char *accountId, u8 kind,
                                            u32 listingId, u32 itemId, u32 itemCount,
                                            u16 enhanceLevel, u32 itemInstanceId,
                                            const vm_net_mock_enhance_wash_state *wash,
                                            u8 gemReflect, u8 gemCombo, u8 gemRevive,
                                            u32 moneyAmount, u32 wcoinAmount,
                                            bool notify)
{
    char accountEsc[160];
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char query[1024];

    if (roleId == 0 || accountId == NULL || accountId[0] == 0)
        return false;
    if (vm_mysql_hex_encode(accountId, strlen(accountId), accountEsc,
                            sizeof(accountEsc)) == 0)
        return false;
    (void)vm_mock_auction_wash_sql_text(wash, washText, sizeof(washText));
    snprintf(query, sizeof(query),
             "INSERT INTO server_auction_claims ("
             "role_id,account_id,kind,listing_id,item_id,item_count,enhance_level,"
             "item_instance_id,enhance_wash,gem_reflect,gem_combo,gem_revive,"
             "money_amount,wcoin_amount,claimed) VALUES ("
             "%u,CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u,'%s',%u,%u,%u,%u,%u,0)",
             roleId, accountEsc, (u32)kind, listingId, itemId, itemCount,
             (u32)enhanceLevel, itemInstanceId, washText, (u32)gemReflect,
             (u32)gemCombo, (u32)gemRevive, moneyAmount, wcoinAmount);
    if (!vm_mysql_exec(query))
        return false;
    if (notify)
        vm_mock_auction_notify_role(roleId, listingId, VM_MOCK_AUCTION_EVT_CLAIM, 0);
    return true;
}

static bool vm_mock_auction_insert_claim(u32 roleId, const char *accountId, u8 kind,
                                         u32 listingId, u32 itemId, u32 itemCount,
                                         u16 enhanceLevel, u32 itemInstanceId,
                                         const vm_net_mock_enhance_wash_state *wash,
                                         u8 gemReflect, u8 gemCombo, u8 gemRevive,
                                         u32 moneyAmount, u32 wcoinAmount)
{
    return vm_mock_auction_insert_claim_ex(roleId, accountId, kind, listingId, itemId,
                                            itemCount, enhanceLevel, itemInstanceId, wash,
                                            gemReflect, gemCombo, gemRevive, moneyAmount,
                                            wcoinAmount, true);
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
    vm_net_mock_enhance_wash_clear(&row->enhanceWash);
    if (columnCount >= 20)
    {
        if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
            row->itemInstanceId = tmp;
        if (values[7] != NULL)
            (void)vm_net_mock_enhance_wash_decode(values[7], lengths[7],
                                                  &row->enhanceWash);
        if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp) &&
            tmp <= 255u)
            row->gemReflectLevel = (u8)tmp;
        if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp) &&
            tmp <= 255u)
            row->gemComboLevel = (u8)tmp;
        if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp) &&
            tmp <= 255u)
            row->gemReviveLevel = (u8)tmp;
        if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
            row->startPrice = tmp;
        if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
            row->currentPrice = tmp;
        if (values[13] && vm_mock_mysql_parse_u32(values[13], lengths[13], &tmp))
            row->buyoutPrice = tmp;
        if (values[14] && vm_mock_mysql_parse_u32(values[14], lengths[14], &tmp))
            row->currency = (u8)tmp;
        if (values[15] && vm_mock_mysql_parse_u32(values[15], lengths[15], &tmp))
            row->feePercent = (u8)tmp;
        if (values[16] && vm_mock_mysql_parse_u32(values[16], lengths[16], &tmp))
            row->durationTier = (u8)tmp;
        if (values[17] && vm_mock_mysql_parse_u32(values[17], lengths[17], &tmp))
            row->status = (u8)tmp;
        if (values[18] && vm_mock_mysql_parse_u32(values[18], lengths[18], &tmp))
            row->highBidderRoleId = tmp;
        if (values[19] && vm_mock_mysql_parse_u32(values[19], lengths[19], &tmp))
            row->expireUnix = tmp;
    }
    else if (columnCount >= 17)
    {
        if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
            row->itemInstanceId = tmp;
        if (values[7] != NULL)
            (void)vm_net_mock_enhance_wash_decode(values[7], lengths[7],
                                                  &row->enhanceWash);
        if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
            row->startPrice = tmp;
        if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
            row->currentPrice = tmp;
        if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
            row->buyoutPrice = tmp;
        if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
            row->currency = (u8)tmp;
        if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
            row->feePercent = (u8)tmp;
        if (values[13] && vm_mock_mysql_parse_u32(values[13], lengths[13], &tmp))
            row->durationTier = (u8)tmp;
        if (values[14] && vm_mock_mysql_parse_u32(values[14], lengths[14], &tmp))
            row->status = (u8)tmp;
        if (values[15] && vm_mock_mysql_parse_u32(values[15], lengths[15], &tmp))
            row->highBidderRoleId = tmp;
        if (values[16] && vm_mock_mysql_parse_u32(values[16], lengths[16], &tmp))
            row->expireUnix = tmp;
    }
    else if (columnCount >= 16)
    {
        if (values[6] != NULL)
            (void)vm_net_mock_enhance_wash_decode(values[6], lengths[6],
                                                  &row->enhanceWash);
        if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
            row->startPrice = tmp;
        if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
            row->currentPrice = tmp;
        if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
            row->buyoutPrice = tmp;
        if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
            row->currency = (u8)tmp;
        if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
            row->feePercent = (u8)tmp;
        if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
            row->durationTier = (u8)tmp;
        if (values[13] && vm_mock_mysql_parse_u32(values[13], lengths[13], &tmp))
            row->status = (u8)tmp;
        if (values[14] && vm_mock_mysql_parse_u32(values[14], lengths[14], &tmp))
            row->highBidderRoleId = tmp;
        if (values[15] && vm_mock_mysql_parse_u32(values[15], lengths[15], &tmp))
            row->expireUnix = tmp;
    }
    else
    {
        if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
            row->startPrice = tmp;
        if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
            row->currentPrice = tmp;
        if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
            row->buyoutPrice = tmp;
        if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
            row->currency = (u8)tmp;
        if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp))
            row->feePercent = (u8)tmp;
        if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
            row->durationTier = (u8)tmp;
        if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
            row->status = (u8)tmp;
        if (values[13] && vm_mock_mysql_parse_u32(values[13], lengths[13], &tmp))
            row->highBidderRoleId = tmp;
        if (values[14] && vm_mock_mysql_parse_u32(values[14], lengths[14], &tmp))
            row->expireUnix = tmp;
    }
    if (row->expireUnix > ctx->now)
        row->remainSec = row->expireUnix - ctx->now;
    else
        row->remainSec = 0;
    ctx->count += 1;
    return true;
}

static bool vm_mock_auction_load_listing(u32 listingId, vm_mock_auction_listing_row *out)
{
    char query[640];
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
             "enhance_level,item_instance_id,enhance_wash,gem_reflect,gem_combo,gem_revive,start_price,current_price,buyout_price,currency,fee_percent,"
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
    u32 affected = 0;
    bool sold = false;

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

    if (sellerAccount[0] == 0)
        return false;

    sold = (row.highBidderRoleId != 0 && row.currentPrice > 0);
    if (sold && highAccount[0] == 0)
        return false;

    if (!vm_mock_auction_tx_begin())
        return false;

    if (sold)
    {
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET status=%u WHERE listing_id=%u "
                 "AND status=%u AND high_bidder_role_id=%u AND current_price=%u",
                 VM_MOCK_AUCTION_ST_SOLD, listingId, VM_MOCK_AUCTION_ST_OPEN,
                 row.highBidderRoleId, row.currentPrice);
        if (!vm_mock_auction_exec_affected(query, &affected) || affected != 1)
        {
            vm_mock_auction_tx_rollback();
            return false;
        }
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_bids SET status=%u WHERE listing_id=%u "
                 "AND bidder_role_id=%u AND status=%u",
                 VM_MOCK_AUCTION_BID_WON, listingId, row.highBidderRoleId,
                 VM_MOCK_AUCTION_BID_ACTIVE);
        (void)vm_mysql_exec(query);

        if (row.currency == VM_MOCK_AUCTION_CUR_WCOIN)
        {
            if (!vm_mock_auction_insert_claim_ex(
                    row.sellerRoleId, sellerAccount,
                    VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT, listingId, 0, 0, 0, 0, NULL, 0,
                    0, 0, 0, row.currentPrice, false))
            {
                vm_mock_auction_tx_rollback();
                return false;
            }
        }
        else if (!vm_mock_auction_insert_claim_ex(
                     row.sellerRoleId, sellerAccount,
                     VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT, listingId, 0, 0, 0, 0, NULL,
                     0, 0, 0, row.currentPrice, 0, false))
        {
            vm_mock_auction_tx_rollback();
            return false;
        }

        if (!vm_mock_auction_insert_claim_ex(
                row.highBidderRoleId, highAccount,
                VM_MOCK_AUCTION_CLAIM_WINNER_ITEM, listingId, row.itemId,
                row.itemCount, row.enhanceLevel, row.itemInstanceId, &row.enhanceWash,
                row.gemReflectLevel, row.gemComboLevel, row.gemReviveLevel, 0, 0,
                false))
        {
            vm_mock_auction_tx_rollback();
            return false;
        }

        if (!vm_mock_auction_tx_commit())
        {
            vm_mock_auction_tx_rollback();
            return false;
        }

        vm_mock_auction_notify_role(row.sellerRoleId, listingId,
                                    VM_MOCK_AUCTION_EVT_CLAIM, 0);
        vm_mock_auction_notify_role(row.highBidderRoleId, listingId,
                                    VM_MOCK_AUCTION_EVT_CLAIM, 0);
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
                 "AND status=%u AND high_bidder_role_id=0",
                 VM_MOCK_AUCTION_ST_EXPIRED, listingId, VM_MOCK_AUCTION_ST_OPEN);
        if (!vm_mock_auction_exec_affected(query, &affected) || affected != 1)
        {
            vm_mock_auction_tx_rollback();
            return false;
        }
        if (!vm_mock_auction_insert_claim_ex(
                row.sellerRoleId, sellerAccount, VM_MOCK_AUCTION_CLAIM_ITEM_RETURN,
                listingId, row.itemId, row.itemCount, row.enhanceLevel,
                row.itemInstanceId, &row.enhanceWash, row.gemReflectLevel,
                row.gemComboLevel, row.gemReviveLevel, 0, 0, false))
        {
            vm_mock_auction_tx_rollback();
            return false;
        }
        if (!vm_mock_auction_tx_commit())
        {
            vm_mock_auction_tx_rollback();
            return false;
        }
        vm_mock_auction_notify_role(row.sellerRoleId, listingId,
                                    VM_MOCK_AUCTION_EVT_CLAIM, 0);
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
    static bool s_tablesReady = false;

    if (now == g_vm_mock_auction_last_tick_sec)
        return;
    g_vm_mock_auction_last_tick_sec = now;
    if (!s_tablesReady)
    {
        if (!vm_mock_auction_ensure_tables())
            return;
        s_tablesReady = true;
    }
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
    char query[768];
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
        /* 我的：在售上架 + 当前最高出价（仍 OPEN 的参与中拍品）. */
        snprintf(query, sizeof(query),
                 "SELECT listing_id,seller_role_id,seller_name,item_id,item_count,"
                 "enhance_level,item_instance_id,enhance_wash,gem_reflect,gem_combo,gem_revive,start_price,current_price,buyout_price,currency,fee_percent,"
                 "duration_tier,status,high_bidder_role_id,expire_unix "
                 "FROM server_auction_listings WHERE status=%u AND "
                 "(seller_role_id=%u OR high_bidder_role_id=%u) "
                 "ORDER BY listing_id DESC LIMIT %u OFFSET %u",
                 VM_MOCK_AUCTION_ST_OPEN, role->roleId, role->roleId,
                 VM_MOCK_AUCTION_PAGE_SIZE, pageIndex * VM_MOCK_AUCTION_PAGE_SIZE);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "SELECT listing_id,seller_role_id,seller_name,item_id,item_count,"
                 "enhance_level,item_instance_id,enhance_wash,gem_reflect,gem_combo,gem_revive,start_price,current_price,buyout_price,currency,fee_percent,"
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
        /* After enhanceLevel: same common-extra as bag (milestones + wash bit7). */
        if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].listingId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].sellerRoleId) ||
            !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen, rows[i].sellerName) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].itemId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].itemCount) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].enhanceLevel) ||
            !vm_net_mock_seq_put_item_common_extra(
                blob, sizeof(blob), &blobLen,
                (u8)SDL_min(rows[i].itemCount, 255u),
                (u8)SDL_min(rows[i].enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                rows[i].itemId, &rows[i].enhanceWash, rows[i].gemReflectLevel,
                rows[i].gemComboLevel, rows[i].gemReviveLevel) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].startPrice) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].currentPrice) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].buyoutPrice) ||
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
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pageSize",
                                    VM_MOCK_AUCTION_PAGE_SIZE) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "hasMore",
                                    i >= VM_MOCK_AUCTION_PAGE_SIZE ? 1 : 0) ||
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
    u32 buyoutPrice = 0;
    u32 currency = 0;
    u32 durationTier = 0;
    u32 fee = 0;
    u8 feePct = 0;
    u32 expireUnix = 0;
    u32 listingId = 0;
    u32 activeCount = 0;
    u32 globalCount = 0;
    char accountEsc[160];
    char nameHex[96];
    char query[1024];
    u32 remaining = 0;
    u16 enhance = 0;
    u8 listGemR = 0;
    u8 listGemC = 0;
    u8 listGemV = 0;
    vm_net_mock_enhance_wash_state listWash;
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    u32 itemId = 0;
    u32 listInstanceId = 0;
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
                                              "buyoutPrice", &buyoutPrice);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "currency", &currency);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "durationTier", &durationTier);
    if (bagSeq == 0 || startPrice == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   4, 0, "bad-args");
    if (buyoutPrice > 0 && buyoutPrice < startPrice)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   4, 0, "buyout-low");
    if (buyoutPrice > 0 &&
        (u64)buyoutPrice > (u64)startPrice * 10ull)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   4, 0, "buyout-high");
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
    listGemR = bag->gemReflectLevel;
    listGemC = bag->gemComboLevel;
    listGemV = bag->gemReviveLevel;
    /* Socketed gear must remove gems first — no auction round-trip for holes. */
    if (vm_net_mock_gem_sockets_occupied(listGemR, listGemC, listGemV))
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   4, 0, "socketed");
    /*
     * category_gate=1: same item rules as player trade (incl. loose gems).
     * category_gate=0: auction does not reject by category, bind, quest,
     * enhance, or loose gems/fragments. Player trade stays on is_tradeable.
     */
    if (g_vm_mock_auction_config.categoryGate &&
        !vm_net_mock_item_is_tradeable(itemId, enhance))
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   4, 0, "not-tradeable");
    listInstanceId = vm_net_mock_role_ensure_equipment_instance_id(
        role, bag->enhanceInstanceId, itemId);
    vm_net_mock_enhance_wash_copy(&listWash, &bag->enhanceWash);
    vm_net_mock_enhance_wash_coalesce_from_record(
        role->roleId, listInstanceId, itemId, enhance, (u16)bagSeq, &listWash);
    if (count == 0 || count > bag->count)
        count = bag->count;
    if (vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
        count = 1;
    /*
     * 802/803：item_count 是储量而非堆叠。可见数量恒为 1，上架必须整行移出
     * （与交易/仓库合同一致）；禁止按储量点数部分上架。
     */
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(itemId))
        count = bag->count;
    /*
     * 834/839：item_count 是剩余耐久（打开次数），不是凭证张数。整行上架，
     * 避免客户端传 count=1 只扣 1 耐久却把整行语义写进 listing。
     */
    if (vm_net_mock_backpack_item_id_is_durable_mall_pass(itemId))
        count = bag->count;
    if (count == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   4, 0, "bad-args");
    globalCount = 0;
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM server_auction_listings WHERE status=%u",
             VM_MOCK_AUCTION_ST_OPEN);
    (void)vm_mysql_query(query, vm_mock_auction_u32_row, &globalCount);
    if (globalCount >= VM_MOCK_AUCTION_MAX_GLOBAL_OPEN)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   7, 0, "global-limit");
    activeCount = 0;
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
    (void)vm_mock_auction_wash_sql_text(&listWash, washText, sizeof(washText));
    snprintf(query, sizeof(query),
             "INSERT INTO server_auction_listings ("
             "seller_role_id,seller_account_id,seller_name,item_id,item_count,"
             "enhance_level,item_instance_id,enhance_wash,gem_reflect,gem_combo,"
             "gem_revive,start_price,current_price,buyout_price,currency,fee_paid,"
             "fee_percent,duration_tier,status,high_bidder_role_id,"
             "high_bidder_account_id,expire_unix) VALUES ("
             "%u,CAST(X'%s' AS CHAR),UNHEX('%s'),%u,%u,%u,%u,'%s',%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,0,'',"
             "%u)",
             role->roleId, accountEsc, nameHex, itemId, count, (u32)enhance,
             listInstanceId, washText, (u32)listGemR, (u32)listGemC, (u32)listGemV,
             startPrice, startPrice, buyoutPrice, currency, fee,
             (u32)feePct, durationTier, VM_MOCK_AUCTION_ST_OPEN, expireUnix);
    if (!vm_mysql_exec(query))
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-list-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CREATE,
                                                   12, 0, "insert");
    }
    listingId = 0;
    (void)vm_mysql_query("SELECT LAST_INSERT_ID()", vm_mock_auction_u32_row, &listingId);
    (void)vm_net_mock_enhance_wash_record_upsert(role->roleId, listInstanceId, 0,
                                                 itemId, enhance, &listWash, 0);
    session->backpackListResyncPending = false;
    session->backpackListResyncPhase = 0;
    session->backpackListResyncListOnly = false;
    session->backpackListResyncTick = 0;
    session->backpackResyncRemoveSeq = 0;
    session->backpackResyncRemoveItemId = 0;
    g_netMockBackpackGridSeededRoleId = 0;
    printf("[info][mock-service] auction_list listing=%u role=%u item=%u x%u "
           "price=%u buyout=%u cur=%u fee=%u(%u%%) tier=%u\n",
           listingId, role->roleId, itemId, count, startPrice, buyoutPrice, currency,
           fee, feePct, durationTier);
    {
        u32 pos = 5;
        u32 objectStart = 0;
        u8 objectCount = 0;

        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                         VM_MOCK_AUCTION_WT_CREATE, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "listingId", listingId) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "message", "ok"))
            return vm_net_mock_build_auction_op_result(out, outCap,
                                                       VM_MOCK_AUCTION_WT_CREATE, 1,
                                                       listingId, "ok");
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 1;
        objectCount = vm_mock_auction_append_pixi_bag_objects(out, outCap, &pos,
                                                              objectCount);
        return vm_mock_auction_finish_packet(out, pos, objectCount);
    }
}

/* Unambiguous A-Z/2-9 (skip 0/O/1/I/L) for LCD captcha. */
static char vm_mock_auction_captcha_alphabet_char(u32 n)
{
    static const char kAlpha[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    return kAlpha[n % (sizeof(kAlpha) - 1u)];
}

static void vm_mock_auction_captcha_clear(void)
{
    memset(&g_auctionBuyoutCaptcha, 0, sizeof(g_auctionBuyoutCaptcha));
}

static void vm_mock_auction_captcha_issue(u32 roleId, u32 listingId)
{
    u32 seed;
    u32 i;

    vm_mock_auction_captcha_clear();
    g_auctionBuyoutCaptcha.roleId = roleId;
    g_auctionBuyoutCaptcha.listingId = listingId;
    g_auctionBuyoutCaptcha.issuedMs = (u32)SDL_GetTicks();
    seed = g_auctionBuyoutCaptcha.issuedMs ^ (roleId * 0x9e3779b9u) ^
           (listingId * 0x85ebca6bu) ^ 0xa5a5a5a5u;
    for (i = 0; i < VM_MOCK_AUCTION_CAPTCHA_LEN; ++i)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        if (seed == 0)
            seed = 0x9e3779b9u + i;
        g_auctionBuyoutCaptcha.code[i] =
            vm_mock_auction_captcha_alphabet_char(seed);
    }
    g_auctionBuyoutCaptcha.code[VM_MOCK_AUCTION_CAPTCHA_LEN] = 0;
}

static void vm_mock_auction_captcha_normalize(char *dst, u32 dstCap, const char *src)
{
    u32 n = 0;
    if (dst == NULL || dstCap == 0)
        return;
    dst[0] = 0;
    if (src == NULL)
        return;
    while (*src != 0 && n + 1u < dstCap)
    {
        char c = *src++;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            dst[n++] = c;
    }
    dst[n] = 0;
}

/* Returns NULL on ok; otherwise short reason for op_result message. */
static const char *vm_mock_auction_captcha_consume(u32 roleId, u32 listingId,
                                                   const char *answerIn)
{
    char answer[VM_MOCK_AUCTION_CAPTCHA_LEN + 1];
    u32 now;
    u32 elapsed;

    if (g_auctionBuyoutCaptcha.roleId != roleId ||
        g_auctionBuyoutCaptcha.listingId != listingId ||
        g_auctionBuyoutCaptcha.code[0] == 0)
        return "captcha-need";
    vm_mock_auction_captcha_normalize(answer, sizeof(answer), answerIn);
    if (answer[0] == 0 ||
        strcmp(answer, g_auctionBuyoutCaptcha.code) != 0)
        return "captcha";
    now = (u32)SDL_GetTicks();
    elapsed = now - g_auctionBuyoutCaptcha.issuedMs;
    if (elapsed < (u32)VM_MOCK_AUCTION_CAPTCHA_MIN_MS)
        return "captcha-wait";
    vm_mock_auction_captcha_clear();
    return NULL;
}

static u32 vm_net_mock_build_auction_buyout_challenge_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 listingId = 0;
    vm_mock_auction_listing_row row;
    u32 pos = 5;
    u32 objectStart = 0;

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE)
        return 0;
    (void)vm_mock_auction_tick();
    (void)vm_mock_auction_load_config(false);
    if (!g_vm_mock_auction_config.enabled)
        return vm_net_mock_build_auction_op_result(
            out, outCap, VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE, 2, 0, "disabled");
    if (role == NULL || session == NULL)
        return vm_net_mock_build_auction_op_result(
            out, outCap, VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE, 3, 0, "no-role");
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "listingId", &listingId);
    if (listingId == 0 || !vm_mock_auction_load_listing(listingId, &row) ||
        row.status != VM_MOCK_AUCTION_ST_OPEN)
        return vm_net_mock_build_auction_op_result(
            out, outCap, VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE, 4, listingId,
            "not-open");
    if (row.buyoutPrice == 0)
        return vm_net_mock_build_auction_op_result(
            out, outCap, VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE, 14, listingId,
            "no-buyout");
    if (row.sellerRoleId == role->roleId)
        return vm_net_mock_build_auction_op_result(
            out, outCap, VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE, 4, listingId,
            "self");
    vm_mock_auction_captcha_issue(role->roleId, listingId);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                     VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE,
                                     &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "listingId", listingId) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "captcha",
                                       g_auctionBuyoutCaptcha.code) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message", "ok"))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    printf("[info][mock-service] auction_buyout_challenge listing=%u role=%u\n",
           listingId, role->roleId);
    return vm_mock_auction_finish_packet(out, pos, 1);
}

/*
 * Instant buyout: charge buyer (delta if already high), refund prior bidder,
 * mark SOLD, enqueue seller payout + winner item claims.
 * Caller must already verify buyout captcha (42/11 + matching answer).
 */
static u32 vm_net_mock_build_auction_buyout_core(
    u8 *out, u32 outCap, u8 subtype, u32 listingId,
    const vm_mock_auction_listing_row *rowIn, vm_net_mock_role_state *role,
    vm_mock_service_client_session *session)
{
    vm_mock_auction_listing_row row;
    char accountEsc[160];
    char sellerAccount[64];
    char prevAccount[64];
    char query[768];
    u32 payAmount = 0;
    u32 affected = 0;
    u32 prevRoleId = 0;
    u32 prevPrice = 0;
    bool selfWasHigh = false;
    vm_net_mock_role_state before;

    if (out == NULL || role == NULL || session == NULL || listingId == 0)
        return 0;
    if (rowIn != NULL)
        row = *rowIn;
    else if (!vm_mock_auction_load_listing(listingId, &row))
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 3, listingId,
                                                   "not-open");
    if (row.status != VM_MOCK_AUCTION_ST_OPEN)
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 3, listingId,
                                                   "not-open");
    if (row.buyoutPrice == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 14, listingId,
                                                   "no-buyout");
    if (row.currentPrice > row.buyoutPrice)
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 15, listingId,
                                                   "buyout-gone");
    if (row.sellerRoleId == role->roleId)
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 4, listingId,
                                                   "self");
    sellerAccount[0] = 0;
    snprintf(query, sizeof(query),
             "SELECT seller_account_id FROM server_auction_listings "
             "WHERE listing_id=%u LIMIT 1",
             listingId);
    (void)vm_mysql_query(query, vm_mock_auction_copy_str_row, sellerAccount);
    if (sellerAccount[0] != 0 && strcmp(sellerAccount, session->accountId) == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 4, listingId,
                                                   "self-account");
    if (vm_mysql_hex_encode(session->accountId, strlen(session->accountId), accountEsc,
                            sizeof(accountEsc)) == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 8, listingId,
                                                   "encode");

    selfWasHigh = (row.highBidderRoleId == role->roleId);
    payAmount = row.buyoutPrice;
    if (selfWasHigh)
    {
        if (row.currentPrice >= row.buyoutPrice)
            payAmount = 0;
        else
            payAmount = row.buyoutPrice - row.currentPrice;
    }

    before = *role;
    if (payAmount > 0 &&
        !vm_mock_auction_charge(role, row.currency, payAmount, "auction-buyout"))
    {
        *role = before;
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 7, listingId,
                                                   "balance");
    }

    prevRoleId = row.highBidderRoleId;
    prevPrice = row.currentPrice;
    prevAccount[0] = 0;
    if (prevRoleId != 0 && !selfWasHigh)
    {
        snprintf(query, sizeof(query),
                 "SELECT high_bidder_account_id FROM server_auction_listings "
                 "WHERE listing_id=%u LIMIT 1",
                 listingId);
        (void)vm_mysql_query(query, vm_mock_auction_copy_str_row, prevAccount);
    }

    if (!vm_mock_auction_tx_begin())
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-buyout-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 9, listingId,
                                                   "tx");
    }

    /*
     * Atomic take: still OPEN, same buyout, same high/price snapshot.
     * Sets SOLD immediately with buyer as winner at buyout price.
     */
    if (prevRoleId == 0)
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET status=%u,current_price=%u,"
                 "high_bidder_role_id=%u,high_bidder_account_id=CAST(X'%s' AS CHAR) "
                 "WHERE listing_id=%u AND status=%u AND buyout_price=%u AND "
                 "high_bidder_role_id=0 AND current_price=%u",
                 VM_MOCK_AUCTION_ST_SOLD, row.buyoutPrice, role->roleId, accountEsc,
                 listingId, VM_MOCK_AUCTION_ST_OPEN, row.buyoutPrice, row.currentPrice);
    else
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET status=%u,current_price=%u,"
                 "high_bidder_role_id=%u,high_bidder_account_id=CAST(X'%s' AS CHAR) "
                 "WHERE listing_id=%u AND status=%u AND buyout_price=%u AND "
                 "high_bidder_role_id=%u AND current_price=%u",
                 VM_MOCK_AUCTION_ST_SOLD, row.buyoutPrice, role->roleId, accountEsc,
                 listingId, VM_MOCK_AUCTION_ST_OPEN, row.buyoutPrice, prevRoleId,
                 prevPrice);

    if (!vm_mock_auction_exec_affected(query, &affected) || affected != 1)
    {
        vm_mock_auction_tx_rollback();
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-buyout-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 10, listingId,
                                                   "conflict");
    }

    if (prevRoleId != 0 && !selfWasHigh && prevAccount[0] != 0 && prevPrice > 0)
    {
        if (row.currency == VM_MOCK_AUCTION_CUR_WCOIN)
        {
            if (!vm_mock_auction_insert_claim_ex(
                    prevRoleId, prevAccount, VM_MOCK_AUCTION_CLAIM_BID_REFUND, listingId,
                    0, 0, 0, 0, NULL, 0, 0, 0, 0, prevPrice, false))
            {
                vm_mock_auction_tx_rollback();
                *role = before;
                (void)vm_net_mock_role_db_save_role("auction-buyout-rollback",
                                                    role->roleId);
                return vm_net_mock_build_auction_op_result(out, outCap, subtype, 11,
                                                           listingId, "refund");
            }
        }
        else if (!vm_mock_auction_insert_claim_ex(
                     prevRoleId, prevAccount, VM_MOCK_AUCTION_CLAIM_BID_REFUND,
                     listingId, 0, 0, 0, 0, NULL, 0, 0, 0, prevPrice, 0, false))
        {
            vm_mock_auction_tx_rollback();
            *role = before;
            (void)vm_net_mock_role_db_save_role("auction-buyout-rollback", role->roleId);
            return vm_net_mock_build_auction_op_result(out, outCap, subtype, 11, listingId,
                                                       "refund");
        }
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_bids SET status=%u WHERE listing_id=%u "
                 "AND bidder_role_id=%u AND status=%u",
                 VM_MOCK_AUCTION_BID_OUTBID, listingId, prevRoleId,
                 VM_MOCK_AUCTION_BID_ACTIVE);
        (void)vm_mysql_exec(query);
    }
    else if (selfWasHigh)
    {
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_bids SET status=%u WHERE listing_id=%u "
                 "AND bidder_role_id=%u AND status=%u",
                 VM_MOCK_AUCTION_BID_WON, listingId, role->roleId,
                 VM_MOCK_AUCTION_BID_ACTIVE);
        (void)vm_mysql_exec(query);
    }

    if (!selfWasHigh)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_auction_bids ("
                 "listing_id,bidder_role_id,bidder_account_id,amount,currency,status) "
                 "VALUES (%u,%u,CAST(X'%s' AS CHAR),%u,%u,%u)",
                 listingId, role->roleId, accountEsc, row.buyoutPrice, (u32)row.currency,
                 VM_MOCK_AUCTION_BID_WON);
        if (!vm_mysql_exec(query))
        {
            vm_mock_auction_tx_rollback();
            *role = before;
            (void)vm_net_mock_role_db_save_role("auction-buyout-rollback", role->roleId);
            return vm_net_mock_build_auction_op_result(out, outCap, subtype, 12, listingId,
                                                       "bid-row");
        }
    }

    if (row.currency == VM_MOCK_AUCTION_CUR_WCOIN)
    {
        if (!vm_mock_auction_insert_claim_ex(
                row.sellerRoleId, sellerAccount, VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT,
                listingId, 0, 0, 0, 0, NULL, 0, 0, 0, 0, row.buyoutPrice, false))
        {
            vm_mock_auction_tx_rollback();
            *role = before;
            (void)vm_net_mock_role_db_save_role("auction-buyout-rollback", role->roleId);
            return vm_net_mock_build_auction_op_result(out, outCap, subtype, 16, listingId,
                                                       "payout");
        }
    }
    else if (!vm_mock_auction_insert_claim_ex(
                 row.sellerRoleId, sellerAccount, VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT,
                 listingId, 0, 0, 0, 0, NULL, 0, 0, 0, row.buyoutPrice, 0, false))
    {
        vm_mock_auction_tx_rollback();
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-buyout-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 16, listingId,
                                                   "payout");
    }

    if (!vm_mock_auction_insert_claim_ex(
            role->roleId, session->accountId, VM_MOCK_AUCTION_CLAIM_WINNER_ITEM,
            listingId, row.itemId, row.itemCount, row.enhanceLevel, row.itemInstanceId,
            &row.enhanceWash, row.gemReflectLevel, row.gemComboLevel,
            row.gemReviveLevel, 0, 0, false))
    {
        vm_mock_auction_tx_rollback();
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-buyout-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 17, listingId,
                                                   "winner-item");
    }

    if (!vm_mock_auction_tx_commit())
    {
        vm_mock_auction_tx_rollback();
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-buyout-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, subtype, 13, listingId,
                                                   "commit");
    }

    if (prevRoleId != 0 && !selfWasHigh && prevAccount[0] != 0)
    {
        vm_mock_auction_notify_role(prevRoleId, listingId, VM_MOCK_AUCTION_EVT_CLAIM, 0);
        vm_mock_auction_notify_role(prevRoleId, listingId, VM_MOCK_AUCTION_EVT_OUTBID,
                                    prevPrice);
    }
    vm_mock_auction_notify_role(row.sellerRoleId, listingId, VM_MOCK_AUCTION_EVT_CLAIM, 0);
    vm_mock_auction_notify_role(role->roleId, listingId, VM_MOCK_AUCTION_EVT_CLAIM, 0);
    vm_mock_auction_notify_role(row.sellerRoleId, listingId, VM_MOCK_AUCTION_EVT_SOLD,
                                row.buyoutPrice);
    vm_mock_auction_notify_role(role->roleId, listingId, VM_MOCK_AUCTION_EVT_SOLD,
                                row.buyoutPrice);
    printf("[info][mock-service] auction_buyout listing=%u role=%u price=%u cur=%u "
           "paid=%u\n",
           listingId, role->roleId, row.buyoutPrice, row.currency, payAmount);
    return vm_net_mock_build_auction_op_result(out, outCap, subtype, 1, listingId, "ok");
}

static u32 vm_net_mock_build_auction_buyout_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 listingId = 0;
    char captchaIn[16];
    const char *captchaErr = NULL;

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_BUYOUT)
        return 0;
    (void)vm_mock_auction_tick();
    (void)vm_mock_auction_load_config(false);
    if (!g_vm_mock_auction_config.enabled)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BUYOUT,
                                                   2, 0, "disabled");
    if (role == NULL || session == NULL)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BUYOUT,
                                                   3, 0, "no-role");
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "listingId", &listingId);
    if (listingId == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BUYOUT,
                                                   4, 0, "not-open");
    captchaIn[0] = 0;
    (void)vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                              "captcha", captchaIn, sizeof(captchaIn));
    captchaErr = vm_mock_auction_captcha_consume(role->roleId, listingId, captchaIn);
    if (captchaErr != NULL)
    {
        u32 code = 16;
        if (strcmp(captchaErr, "captcha-need") == 0)
            code = 18;
        else if (strcmp(captchaErr, "captcha-wait") == 0)
            code = 17;
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BUYOUT,
                                                   code, listingId, captchaErr);
    }
    return vm_net_mock_build_auction_buyout_core(out, outCap, VM_MOCK_AUCTION_WT_BUYOUT,
                                                 listingId, NULL, role, session);
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
    char sellerAccount[64];
    char prevAccount[64];
    char query[768];
    u32 minNeed = 0;
    u32 affected = 0;
    vm_net_mock_role_state before;

    if (request == NULL || out == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_BID)
        return 0;
    (void)vm_mock_auction_tick();
    (void)vm_mock_auction_load_config(false);
    if (!g_vm_mock_auction_config.enabled)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 2,
                                                   0, "disabled");
    if (role == NULL || session == NULL)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 3,
                                                   0, "no-role");
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "listingId", &listingId);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "amount", &amount);
    if (listingId == 0 || amount == 0 ||
        !vm_mock_auction_load_listing(listingId, &row) ||
        row.status != VM_MOCK_AUCTION_ST_OPEN)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 4,
                                                   listingId, "not-open");
    if (row.sellerRoleId == role->roleId)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 5,
                                                   listingId, "self");
    sellerAccount[0] = 0;
    snprintf(query, sizeof(query),
             "SELECT seller_account_id FROM server_auction_listings "
             "WHERE listing_id=%u LIMIT 1",
             listingId);
    (void)vm_mysql_query(query, vm_mock_auction_copy_str_row, sellerAccount);
    if (sellerAccount[0] != 0 &&
        strcmp(sellerAccount, session->accountId) == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 4,
                                                   listingId, "self-account");
    if (row.highBidderRoleId == role->roleId)
    {
        /* Already high: topping to buyout must use 一口价+验证码. */
        if (row.buyoutPrice > 0 && amount >= row.buyoutPrice)
            return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID,
                                                       19, listingId, "use-buyout");
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 5,
                                                   listingId, "already-high");
    }
    minNeed = row.currentPrice;
    if (row.highBidderRoleId != 0)
        minNeed = row.currentPrice + vm_mock_auction_bid_step(row.startPrice);
    if (amount < minNeed)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 6,
                                                   listingId, "low");
    /* Bid at/above buyout → must use 一口价+验证码 path. */
    if (row.buyoutPrice > 0 && amount >= row.buyoutPrice)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 19,
                                                   listingId, "use-buyout");

    if (vm_mysql_hex_encode(session->accountId, strlen(session->accountId), accountEsc,
                            sizeof(accountEsc)) == 0)
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 8,
                                                   listingId, "encode");

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
    }

    if (!vm_mock_auction_tx_begin())
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-bid-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 9,
                                                   listingId, "tx");
    }

    /*
     * Conditional takeover: only the first bidder that matches the observed
     * high/price snapshot wins. Refund claim is written AFTER this update.
     */
    if (row.highBidderRoleId == 0)
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET current_price=%u,"
                 "high_bidder_role_id=%u,high_bidder_account_id=CAST(X'%s' AS CHAR) "
                 "WHERE listing_id=%u AND status=%u AND high_bidder_role_id=0 AND "
                 "current_price=%u",
                 amount, role->roleId, accountEsc, listingId, VM_MOCK_AUCTION_ST_OPEN,
                 row.currentPrice);
    else
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET current_price=%u,"
                 "high_bidder_role_id=%u,high_bidder_account_id=CAST(X'%s' AS CHAR) "
                 "WHERE listing_id=%u AND status=%u AND high_bidder_role_id=%u AND "
                 "current_price=%u",
                 amount, role->roleId, accountEsc, listingId, VM_MOCK_AUCTION_ST_OPEN,
                 row.highBidderRoleId, row.currentPrice);

    if (!vm_mock_auction_exec_affected(query, &affected) || affected != 1)
    {
        vm_mock_auction_tx_rollback();
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-bid-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 10,
                                                   listingId, "conflict");
    }

    if (row.highBidderRoleId != 0 && prevAccount[0] != 0)
    {
        if (row.currency == VM_MOCK_AUCTION_CUR_WCOIN)
        {
            if (!vm_mock_auction_insert_claim_ex(
                    row.highBidderRoleId, prevAccount, VM_MOCK_AUCTION_CLAIM_BID_REFUND,
                    listingId, 0, 0, 0, 0, NULL, 0, 0, 0, 0, row.currentPrice, false))
            {
                vm_mock_auction_tx_rollback();
                *role = before;
                (void)vm_net_mock_role_db_save_role("auction-bid-rollback", role->roleId);
                return vm_net_mock_build_auction_op_result(
                    out, outCap, VM_MOCK_AUCTION_WT_BID, 11, listingId, "refund");
            }
        }
        else if (!vm_mock_auction_insert_claim_ex(
                     row.highBidderRoleId, prevAccount, VM_MOCK_AUCTION_CLAIM_BID_REFUND,
                     listingId, 0, 0, 0, 0, NULL, 0, 0, 0, row.currentPrice, 0, false))
        {
            vm_mock_auction_tx_rollback();
            *role = before;
            (void)vm_net_mock_role_db_save_role("auction-bid-rollback", role->roleId);
            return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID,
                                                       11, listingId, "refund");
        }
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_bids SET status=%u WHERE listing_id=%u "
                 "AND bidder_role_id=%u AND status=%u",
                 VM_MOCK_AUCTION_BID_OUTBID, listingId, row.highBidderRoleId,
                 VM_MOCK_AUCTION_BID_ACTIVE);
        (void)vm_mysql_exec(query);
    }

    snprintf(query, sizeof(query),
             "INSERT INTO server_auction_bids ("
             "listing_id,bidder_role_id,bidder_account_id,amount,currency,status) "
             "VALUES (%u,%u,CAST(X'%s' AS CHAR),%u,%u,%u)",
             listingId, role->roleId, accountEsc, amount, (u32)row.currency,
             VM_MOCK_AUCTION_BID_ACTIVE);
    if (!vm_mysql_exec(query))
    {
        vm_mock_auction_tx_rollback();
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-bid-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 12,
                                                   listingId, "bid-row");
    }

    if (!vm_mock_auction_tx_commit())
    {
        vm_mock_auction_tx_rollback();
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-bid-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_BID, 13,
                                                   listingId, "commit");
    }

    if (row.highBidderRoleId != 0 && prevAccount[0] != 0)
    {
        vm_mock_auction_notify_role(row.highBidderRoleId, listingId,
                                    VM_MOCK_AUCTION_EVT_CLAIM, 0);
        vm_mock_auction_notify_role(row.highBidderRoleId, listingId,
                                    VM_MOCK_AUCTION_EVT_OUTBID, row.currentPrice);
    }
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
    u32 affected = 0;
    vm_net_mock_role_state before;

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

    /* Return item first; only then mark cancelled so bag-full cannot destroy items.
     * Listing enhance_wash is authority — do not coalesce_from_record (instance
     * ids are per-role counters; stale wash_record can resurrect foreign attrs). */
    before = *role;
    if (!vm_mock_auction_return_item_to_bag(role, row.itemId, row.itemCount,
                                            row.enhanceLevel, row.itemInstanceId,
                                            &row.enhanceWash, &seq, "auction-cancel",
                                            row.gemReflectLevel, row.gemComboLevel,
                                            row.gemReviveLevel))
    {
        const char *fail =
            g_vm_net_mock_backpack_add_fail_kind ==
                    VM_NET_MOCK_BACKPACK_ADD_FAIL_FLASK_FULL
                ? "flask-full"
                : "bag-full";
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   7, listingId, fail);
    }
    if (seq != 0)
    {
        if (!vm_net_mock_enhance_wash_is_empty(&row.enhanceWash))
        {
            (void)vm_net_mock_enhance_wash_record_relocate_seq(
                role->roleId, row.itemInstanceId, 0, seq, row.itemId,
                row.enhanceLevel, &row.enhanceWash);
        }
        else
        {
            (void)vm_net_mock_enhance_wash_record_delete(
                role->roleId, row.itemInstanceId, seq);
        }
    }

    snprintf(query, sizeof(query),
             "UPDATE server_auction_listings SET status=%u WHERE listing_id=%u "
             "AND status=%u AND seller_role_id=%u AND high_bidder_role_id=0",
             VM_MOCK_AUCTION_ST_CANCELLED, listingId, VM_MOCK_AUCTION_ST_OPEN,
             role->roleId);
    if (!vm_mock_auction_exec_affected(query, &affected) || affected != 1)
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-cancel-rollback", role->roleId);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   6, listingId, "update");
    }
    if (!vm_net_mock_role_db_save_role("auction-cancel", role->roleId))
    {
        *role = before;
        (void)vm_net_mock_role_db_save_role("auction-cancel-rollback", role->roleId);
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_listings SET status=%u WHERE listing_id=%u "
                 "AND status=%u",
                 VM_MOCK_AUCTION_ST_OPEN, listingId, VM_MOCK_AUCTION_ST_CANCELLED);
        (void)vm_mysql_exec(query);
        return vm_net_mock_build_auction_op_result(out, outCap, VM_MOCK_AUCTION_WT_CANCEL,
                                                   8, listingId, "save");
    }
    session->backpackListResyncPending = false;
    session->backpackListResyncPhase = 0;
    session->backpackListResyncListOnly = false;
    session->backpackListResyncTick = 0;
    session->backpackResyncRemoveSeq = 0;
    session->backpackResyncRemoveItemId = 0;
    g_netMockBackpackGridSeededRoleId = 0;
    {
        u32 pos = 5;
        u32 objectStart = 0;
        u8 objectCount = 0;

        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                         VM_MOCK_AUCTION_WT_CANCEL, &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "listingId", listingId) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "message", "ok"))
            return vm_net_mock_build_auction_op_result(out, outCap,
                                                       VM_MOCK_AUCTION_WT_CANCEL, 1,
                                                       listingId, "ok");
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 1;
        objectCount = vm_mock_auction_append_pixi_bag_objects(out, outCap, &pos,
                                                              objectCount);
        return vm_mock_auction_finish_packet(out, pos, objectCount);
    }
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
    vm_net_mock_enhance_wash_clear(&row->enhanceWash);
    if (columnCount >= 13)
    {
        if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
            row->itemInstanceId = tmp;
        if (values[7] != NULL)
            (void)vm_net_mock_enhance_wash_decode(values[7], lengths[7],
                                                  &row->enhanceWash);
        if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp) &&
            tmp <= 255u)
            row->gemReflectLevel = (u8)tmp;
        if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp) &&
            tmp <= 255u)
            row->gemComboLevel = (u8)tmp;
        if (values[10] && vm_mock_mysql_parse_u32(values[10], lengths[10], &tmp) &&
            tmp <= 255u)
            row->gemReviveLevel = (u8)tmp;
        if (values[11] && vm_mock_mysql_parse_u32(values[11], lengths[11], &tmp))
            row->moneyAmount = tmp;
        if (values[12] && vm_mock_mysql_parse_u32(values[12], lengths[12], &tmp))
            row->wcoinAmount = tmp;
    }
    else if (columnCount >= 10)
    {
        if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
            row->itemInstanceId = tmp;
        if (values[7] != NULL)
            (void)vm_net_mock_enhance_wash_decode(values[7], lengths[7],
                                                  &row->enhanceWash);
        if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
            row->moneyAmount = tmp;
        if (values[9] && vm_mock_mysql_parse_u32(values[9], lengths[9], &tmp))
            row->wcoinAmount = tmp;
    }
    else if (columnCount >= 9)
    {
        if (values[6] != NULL)
            (void)vm_net_mock_enhance_wash_decode(values[6], lengths[6],
                                                  &row->enhanceWash);
        if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
            row->moneyAmount = tmp;
        if (values[8] && vm_mock_mysql_parse_u32(values[8], lengths[8], &tmp))
            row->wcoinAmount = tmp;
    }
    else
    {
        if (values[6] && vm_mock_mysql_parse_u32(values[6], lengths[6], &tmp))
            row->moneyAmount = tmp;
        if (values[7] && vm_mock_mysql_parse_u32(values[7], lengths[7], &tmp))
            row->wcoinAmount = tmp;
    }
    ctx->count += 1;
    return true;
}

static u32 vm_net_mock_build_auction_claims_list_response(const u8 *request,
                                                          u32 requestLen, u8 *out,
                                                          u32 outCap)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_auction_claim_row rows[VM_MOCK_AUCTION_PAGE_SIZE];
    vm_mock_auction_claim_ctx ctx;
    char query[512];
    u32 pageIndex = 0;
    u8 blob[2048];
    u32 blobLen = 0;
    u32 i;
    u32 pending = 0;
    char itemName[48];

    if (out == NULL || outCap < 5 || request == NULL ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != VM_MOCK_AUCTION_WT_KIND ||
        object.subtype != VM_MOCK_AUCTION_WT_CLAIMS_LIST)
        return 0;
    (void)vm_mock_auction_tick();
    if (role == NULL)
        return vm_net_mock_build_auction_op_result(out, outCap,
                                                   VM_MOCK_AUCTION_WT_CLAIMS_LIST, 2, 0,
                                                   "no-role");
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "pageIndex", &pageIndex);
    memset(&ctx, 0, sizeof(ctx));
    ctx.rows = rows;
    ctx.cap = VM_MOCK_AUCTION_PAGE_SIZE;
    snprintf(query, sizeof(query),
             "SELECT claim_id,kind,listing_id,item_id,item_count,enhance_level,"
             "item_instance_id,enhance_wash,gem_reflect,gem_combo,gem_revive,money_amount,wcoin_amount FROM server_auction_claims "
             "WHERE role_id=%u AND claimed=0 ORDER BY claim_id ASC "
             "LIMIT %u OFFSET %u",
             role->roleId, VM_MOCK_AUCTION_PAGE_SIZE,
             pageIndex * VM_MOCK_AUCTION_PAGE_SIZE);
    (void)vm_mysql_query(query, vm_mock_auction_claim_row_cb, &ctx);
    for (i = 0; i < ctx.count; ++i)
    {
        if (rows[i].itemId)
            vm_mock_auction_item_name(rows[i].itemId, itemName, sizeof(itemName));
        else
            itemName[0] = '\0';
        if (!vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].claimId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].kind) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].listingId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].itemId) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].itemCount) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].enhanceLevel) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].moneyAmount) ||
            !vm_net_mock_seq_put_u32(blob, sizeof(blob), &blobLen, rows[i].wcoinAmount) ||
            !vm_net_mock_seq_put_string(blob, sizeof(blob), &blobLen, itemName))
            break;
    }
    pending = vm_mock_auction_pending_claim_count(role->roleId);
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                     VM_MOCK_AUCTION_WT_CLAIMS_LIST, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "count", i) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pageIndex", pageIndex) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pageSize",
                                    VM_MOCK_AUCTION_PAGE_SIZE) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "hasMore",
                                    i >= VM_MOCK_AUCTION_PAGE_SIZE ? 1 : 0) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pendingClaims", pending) ||
        !vm_net_mock_put_object_entry(out, outCap, &pos, "rows", blob, (u16)blobLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    return vm_mock_auction_finish_packet(out, pos, 1);
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
    char query[512];
    u32 claimed = 0;
    u32 failed = 0;
    u32 i;
    u8 blob[1024];
    u32 blobLen = 0;
    u32 pending = 0;
    u32 affected = 0;
    vm_net_mock_role_state before;
    const char *failCode = NULL;
    char message[48];

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
             "item_instance_id,enhance_wash,gem_reflect,gem_combo,gem_revive,money_amount,wcoin_amount FROM server_auction_claims "
             "WHERE role_id=%u AND claimed=0 ORDER BY claim_id ASC LIMIT 16",
             role->roleId);
    (void)vm_mysql_query(query, vm_mock_auction_claim_row_cb, &ctx);
    for (i = 0; i < ctx.count; ++i)
    {
        bool ok = true;
        u16 seq = 0;

        /*
         * Reserve first (claimed=1) so a crash after credit cannot re-grant.
         * Delivery failure releases the reservation.
         */
        snprintf(query, sizeof(query),
                 "UPDATE server_auction_claims SET claimed=1 WHERE claim_id=%u "
                 "AND role_id=%u AND claimed=0",
                 rows[i].claimId, role->roleId);
        if (!vm_mock_auction_exec_affected(query, &affected) || affected != 1)
        {
            failed += 1;
            if (failCode == NULL)
                failCode = "conflict";
            continue;
        }

        before = *role;
        if (rows[i].kind == VM_MOCK_AUCTION_CLAIM_ITEM_RETURN ||
            rows[i].kind == VM_MOCK_AUCTION_CLAIM_WINNER_ITEM)
        {
            /*
             * Claim/listing enhance_wash is the transfer authority (may be empty
             * when first_seed_percent=0 or seller never washed). Do not
             * coalesce_from_record under the *claimer* roleId: seller instance
             * ids collide with the buyer's own wash_record keys and would paint
             * red wash onto items the auction UI correctly showed as unwashed.
             */
            if (rows[i].itemId == 0 || rows[i].itemCount == 0 ||
                !vm_mock_auction_return_item_to_bag(
                    role, rows[i].itemId, rows[i].itemCount, rows[i].enhanceLevel,
                    rows[i].itemInstanceId, &rows[i].enhanceWash, &seq,
                    "auction-claim-item", rows[i].gemReflectLevel,
                    rows[i].gemComboLevel, rows[i].gemReviveLevel))
            {
                ok = false;
                if (failCode == NULL)
                    failCode =
                        g_vm_net_mock_backpack_add_fail_kind ==
                                VM_NET_MOCK_BACKPACK_ADD_FAIL_FLASK_FULL
                            ? "flask-full"
                            : "bag-full";
            }
            else if (seq != 0)
            {
                if (!vm_net_mock_enhance_wash_is_empty(&rows[i].enhanceWash))
                {
                    (void)vm_net_mock_enhance_wash_record_relocate_seq(
                        role->roleId, rows[i].itemInstanceId, 0, seq,
                        rows[i].itemId, rows[i].enhanceLevel,
                        &rows[i].enhanceWash);
                }
                else
                {
                    (void)vm_net_mock_enhance_wash_record_delete(
                        role->roleId, rows[i].itemInstanceId, seq);
                }
            }
        }
        else if (rows[i].kind == VM_MOCK_AUCTION_CLAIM_SELLER_PAYOUT ||
                 rows[i].kind == VM_MOCK_AUCTION_CLAIM_BID_REFUND)
        {
            if (rows[i].moneyAmount)
            {
                if (!vm_mock_auction_credit(role, VM_MOCK_AUCTION_CUR_COPPER,
                                            rows[i].moneyAmount, "auction-claim-money"))
                {
                    ok = false;
                    if (failCode == NULL)
                        failCode = "credit";
                }
            }
            if (ok && rows[i].wcoinAmount)
            {
                if (!vm_mock_auction_credit(role, VM_MOCK_AUCTION_CUR_WCOIN,
                                            rows[i].wcoinAmount, "auction-claim-wcoin"))
                {
                    ok = false;
                    if (failCode == NULL)
                        failCode = "credit";
                }
            }
        }
        else
        {
            ok = false;
            if (failCode == NULL)
                failCode = "bad-kind";
        }

        if (!ok)
        {
            *role = before;
            (void)vm_net_mock_role_db_save_role("auction-claim-release", role->roleId);
            snprintf(query, sizeof(query),
                     "UPDATE server_auction_claims SET claimed=0 WHERE claim_id=%u "
                     "AND role_id=%u AND claimed=1",
                     rows[i].claimId, role->roleId);
            (void)vm_mysql_exec(query);
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
        /* Inline 17/1+30/21 below; do not leave half-armed poll resync. */
        session->backpackListResyncPending = false;
        session->backpackListResyncPhase = 0;
        session->backpackListResyncListOnly = false;
        session->backpackListResyncTick = 0;
        session->backpackResyncRemoveSeq = 0;
        session->backpackResyncRemoveItemId = 0;
        g_netMockBackpackGridSeededRoleId = 0;
    }
    pending = vm_mock_auction_pending_claim_count(role->roleId);
    if (ctx.count == 0)
        snprintf(message, sizeof(message), "%s", "nothing");
    else if (claimed > 0 && failed == 0)
        snprintf(message, sizeof(message), "%s", "ok");
    else if (claimed > 0 && failed > 0)
        snprintf(message, sizeof(message), "partial:%s",
                 failCode ? failCode : "fail");
    else
        snprintf(message, sizeof(message), "%s",
                 failCode ? failCode : "fail");
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, VM_MOCK_AUCTION_WT_KIND,
                                     VM_MOCK_AUCTION_WT_CLAIM, &objectStart) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "result", claimed ? 1 : 2) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "claimed", claimed) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "failed", failed) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pendingClaims", pending) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "blink", pending ? 1 : 0) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message", message) ||
        !vm_net_mock_put_object_entry(out, outCap, &pos, "rows", blob, (u16)blobLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    {
        u8 objectCount = 1;
        if (claimed)
            objectCount = vm_mock_auction_append_pixi_bag_objects(out, outCap, &pos,
                                                                  objectCount);
        return vm_mock_auction_finish_packet(out, pos, objectCount);
    }
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
    if (object.subtype == VM_MOCK_AUCTION_WT_BUYOUT)
        return vm_net_mock_build_auction_buyout_response(request, requestLen, out,
                                                         outCap);
    if (object.subtype == VM_MOCK_AUCTION_WT_BUYOUT_CHALLENGE)
        return vm_net_mock_build_auction_buyout_challenge_response(
            request, requestLen, out, outCap);
    if (object.subtype == VM_MOCK_AUCTION_WT_CANCEL)
        return vm_net_mock_build_auction_cancel_response(request, requestLen, out,
                                                         outCap);
    if (object.subtype == VM_MOCK_AUCTION_WT_CLAIM)
        return vm_net_mock_build_auction_claim_response(request, requestLen, out,
                                                        outCap);
    if (object.subtype == VM_MOCK_AUCTION_WT_CLAIMS_LIST)
        return vm_net_mock_build_auction_claims_list_response(request, requestLen, out,
                                                              outCap);
    return 0;
}
