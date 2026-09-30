/* 藏宝阁 + 酷币：HTTP API + 赞助成交结算。Included from web_admin_server.c. */

enum
{
    VM_MOCK_ZANGBAO_ST_ACTIVE = 0,
    VM_MOCK_ZANGBAO_ST_RESERVED = 1,
    VM_MOCK_ZANGBAO_ST_SOLD = 2,
    VM_MOCK_ZANGBAO_ST_CANCELLED = 3,
    VM_MOCK_ZANGBAO_MIN_PRICE = 10,
    VM_MOCK_ZANGBAO_FEE_PCT = 5,
    VM_MOCK_ZANGBAO_RESERVE_SEC = 360,
    VM_MOCK_KCOIN_WITHDRAW_MIN = 50,
    VM_MOCK_ZANGBAO_PAGE_SIZE = 20,
    /* Default per-role concurrent listings (active+reserved); admin-overridable. */
    VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE = 10,
    VM_MOCK_ZANGBAO_WD_PENDING = 0,
    VM_MOCK_ZANGBAO_WD_APPROVED = 1,
    VM_MOCK_ZANGBAO_WD_REJECTED = 2
};

typedef struct
{
    bool found;
    bool enabled;
    u32 kcoinPerYuan;
    u32 maxActiveListings;
} vm_mock_kcoin_config;

typedef struct
{
    bool found;
    u32 listingId;
    u32 sellerRoleId;
    char sellerAccountId[64];
    char sellerName[32];
    u32 itemId;
    u32 itemCount;
    u16 enhanceLevel;
    u32 itemInstanceId;
    vm_net_mock_enhance_wash_state enhanceWash;
    u8 gemReflect;
    u8 gemCombo;
    u8 gemRevive;
    u32 priceKcoin;
    u32 status;
    u32 buyerRoleId;
    char buyerAccountId[64];
    u32 reserveExpireUnix;
    char payId[64];
} vm_mock_zangbao_listing;

typedef struct
{
    bool found;
    bool credited;
    char payId[64];
    char requestParam[64];
    char accountId[64];
    u32 roleId;
    char sellerAccountId[64];
    u32 sellerRoleId;
    u32 listingId;
    u32 payType;
    u32 priceCents;
    u32 reallyPriceCents;
    u32 priceKcoin;
    u32 sellerCreditKcoin;
    u32 feeKcoin;
    char payUrl[1025];
    u32 status;
    u32 timeoutMinutes;
} vm_mock_zangbao_market_order;

static void vm_mock_zangbao_sync_cached_kcoin(const char *accountId, u32 roleId,
                                              u32 balance)
{
    vm_mock_service_account_state *state = g_vm_mock_service_accounts;

    while (state != NULL)
    {
        if (strcmp(state->accountId, accountId) == 0)
        {
            if (state->roleDbValid)
            {
                for (u32 i = 0; i < state->roleDb.roleCount; ++i)
                {
                    if (state->roleDb.roles[i].roleId == roleId)
                        state->roleDb.roles[i].kcoin = balance;
                }
            }
            break;
        }
        state = state->next;
    }
    if (g_vm_mock_service_active_account_id != NULL &&
        strcmp(g_vm_mock_service_active_account_id, accountId) == 0)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            if (g_vm_net_mock_role_db.roles[i].roleId == roleId)
                g_vm_net_mock_role_db.roles[i].kcoin = balance;
        }
    }
}

static bool vm_mock_zangbao_ensure_schema(void)
{
    static bool ensured = false;
    static bool ok = false;

    if (ensured)
        return ok;
    ensured = true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_kcoin_config ("
            "config_id TINYINT UNSIGNED NOT NULL,"
            "kcoin_per_yuan INT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "max_active_listings INT UNSIGNED NOT NULL DEFAULT 10,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(config_id)) ENGINE=InnoDB"))
        goto fail;
    if (!vm_net_mock_role_table_ensure_column(
            "server_kcoin_config", "max_active_listings",
            "ALTER TABLE server_kcoin_config ADD COLUMN max_active_listings "
            "INT UNSIGNED NOT NULL DEFAULT 10 AFTER enabled",
            "kcoin_max_active_listings"))
        goto fail;
    (void)vm_mysql_exec(
        "INSERT IGNORE INTO server_kcoin_config("
        "config_id,kcoin_per_yuan,enabled,max_active_listings) "
        "VALUES(1,1,1,10)");
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_zangbao_listings ("
            "listing_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "seller_role_id INT UNSIGNED NOT NULL,"
            "seller_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "seller_name VARBINARY(32) NOT NULL DEFAULT '',"
            "item_id INT UNSIGNED NOT NULL,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 1,"
            "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "item_instance_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "enhance_wash VARCHAR(96) NOT NULL DEFAULT "
            "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',"
            "gem_reflect TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "gem_combo TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "gem_revive TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "price_kcoin INT UNSIGNED NOT NULL,"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "buyer_role_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "buyer_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "reserve_expire_unix INT UNSIGNED NOT NULL DEFAULT 0,"
            "pay_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(listing_id),"
            "KEY idx_zangbao_listings_status(status,created_at),"
            "KEY idx_zangbao_listings_seller(seller_role_id,status),"
            "KEY idx_zangbao_listings_pay(pay_id)) ENGINE=InnoDB"))
        goto fail;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS kcoin_market_orders ("
            "pay_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "provider_order_id VARCHAR(127) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "seller_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "seller_role_id INT UNSIGNED NOT NULL,"
            "listing_id INT UNSIGNED NOT NULL,"
            "pay_type TINYINT UNSIGNED NOT NULL,"
            "price_cents INT UNSIGNED NOT NULL,"
            "really_price_cents INT UNSIGNED NOT NULL DEFAULT 0,"
            "price_kcoin INT UNSIGNED NOT NULL,"
            "seller_credit_kcoin INT UNSIGNED NOT NULL,"
            "fee_kcoin INT UNSIGNED NOT NULL,"
            "request_param VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "pay_url VARBINARY(1024) NOT NULL DEFAULT '',"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "provider_state INT NOT NULL DEFAULT 0,"
            "timeout_minutes SMALLINT UNSIGNED NOT NULL DEFAULT 5,"
            "credited TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "paid_at TIMESTAMP NULL DEFAULT NULL,"
            "credited_at TIMESTAMP NULL DEFAULT NULL,"
            "last_checked_at TIMESTAMP NULL DEFAULT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(pay_id),"
            "KEY idx_kcoin_market_listing(listing_id,status),"
            "KEY idx_kcoin_market_buyer(account_id,created_at),"
            "KEY idx_kcoin_market_pending(status,created_at)) ENGINE=InnoDB"))
        goto fail;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_zangbao_claims ("
            "claim_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "role_id INT UNSIGNED NOT NULL,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "listing_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "item_id INT UNSIGNED NOT NULL,"
            "item_count INT UNSIGNED NOT NULL DEFAULT 1,"
            "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "item_instance_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "enhance_wash VARCHAR(96) NOT NULL DEFAULT "
            "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',"
            "gem_reflect TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "gem_combo TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "gem_revive TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "claimed TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(claim_id),"
            "KEY idx_zangbao_claims_role(role_id,claimed)) ENGINE=InnoDB"))
        goto fail;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_kcoin_payee ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "pay_channel TINYINT UNSIGNED NOT NULL,"
            "image_path VARCHAR(255) NOT NULL,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id)) ENGINE=InnoDB"))
        goto fail;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS kcoin_withdraw_orders ("
            "withdraw_id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "role_name VARBINARY(32) NOT NULL DEFAULT '',"
            "amount INT UNSIGNED NOT NULL,"
            "pay_channel TINYINT UNSIGNED NOT NULL,"
            "qr_image_path VARCHAR(255) NOT NULL,"
            "status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "admin_note VARCHAR(255) NOT NULL DEFAULT '',"
            "reviewed_at TIMESTAMP NULL DEFAULT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(withdraw_id),"
            "KEY idx_kcoin_withdraw_status(status,created_at),"
            "KEY idx_kcoin_withdraw_role(role_id,created_at)) ENGINE=InnoDB"))
        goto fail;
    (void)vm_net_mock_role_table_ensure_column(
        "account_roles", "kcoin",
        "ALTER TABLE account_roles ADD COLUMN kcoin INT UNSIGNED NOT NULL DEFAULT 0 "
        "AFTER wcoin",
        "role_kcoin_column");
    ok = true;
    return true;
fail:
    printf("[error][zangbao] schema ensure failed error=%s\n", vm_mysql_last_error());
    ok = false;
    return false;
}

static bool vm_mock_kcoin_config_row(void *ctx, unsigned int cols,
                                     const char *const *vals, const size_t *lens)
{
    vm_mock_kcoin_config *cfg = (vm_mock_kcoin_config *)ctx;
    u32 rate = 1;
    u32 en = 1;
    u32 maxActive = VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE;

    (void)lens;
    if (cfg == NULL || cols < 2)
        return true;
    (void)vm_mock_mysql_parse_u32(vals[0], vals[0] ? strlen(vals[0]) : 0, &rate);
    (void)vm_mock_mysql_parse_u32(vals[1], vals[1] ? strlen(vals[1]) : 0, &en);
    if (cols >= 3)
        (void)vm_mock_mysql_parse_u32(vals[2], vals[2] ? strlen(vals[2]) : 0,
                                      &maxActive);
    cfg->found = true;
    cfg->kcoinPerYuan = rate ? rate : 1;
    cfg->enabled = en != 0;
    cfg->maxActiveListings =
        maxActive ? maxActive : (u32)VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE;
    return true;
}

static bool vm_mock_kcoin_load_config(vm_mock_kcoin_config *out)
{
    if (out == NULL)
        return false;
    memset(out, 0, sizeof(*out));
    out->kcoinPerYuan = 1;
    out->enabled = true;
    out->maxActiveListings = VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE;
    if (!vm_mock_zangbao_ensure_schema())
        return false;
    (void)vm_mysql_query(
        "SELECT kcoin_per_yuan,enabled,max_active_listings "
        "FROM server_kcoin_config WHERE config_id=1 LIMIT 1",
        vm_mock_kcoin_config_row, out);
    if (!out->found)
    {
        out->found = true;
        out->kcoinPerYuan = 1;
        out->enabled = true;
        out->maxActiveListings = VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE;
    }
    else if (out->maxActiveListings == 0)
        out->maxActiveListings = VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE;
    return true;
}

static u32 vm_mock_zangbao_seller_credit(u32 priceKcoin)
{
    u32 fee = (priceKcoin * VM_MOCK_ZANGBAO_FEE_PCT) / 100u;
    return priceKcoin > fee ? priceKcoin - fee : 0;
}

static u32 vm_mock_zangbao_yuan_for_price(u32 priceKcoin, u32 kcoinPerYuan)
{
    u32 rate = kcoinPerYuan ? kcoinPerYuan : 1;
    return (priceKcoin + rate - 1u) / rate;
}

static bool vm_mock_zangbao_bag_has_space(const vm_net_mock_role_state *role)
{
    if (role == NULL)
        return false;
    return role->backpackItemCount < role->backpackCapacity;
}

static bool vm_mock_zangbao_auth(const char *body, char *accountOut, size_t accountCap,
                                u32 *roleIdOut, const char **errOut)
{
    char account[64];
    char password[64];
    char accountGbk[64];
    char passwordGbk[64];
    char roleText[32];
    u32 roleId = 0;
    u32 scratch = 0;
    bool authed = false;

    if (errOut)
        *errOut = "参数无效";
    if (accountOut == NULL || accountCap == 0 || roleIdOut == NULL)
        return false;
    accountOut[0] = 0;
    *roleIdOut = 0;
    memset(account, 0, sizeof(account));
    memset(password, 0, sizeof(password));
    memset(accountGbk, 0, sizeof(accountGbk));
    memset(passwordGbk, 0, sizeof(passwordGbk));
    memset(roleText, 0, sizeof(roleText));
    if (!vm_mock_admin_form_value(body, "account", account, sizeof(account)) ||
        !vm_mock_admin_form_value(body, "password", password, sizeof(password)) ||
        !vm_mock_admin_form_value(body, "role_id", roleText, sizeof(roleText)) ||
        !vm_net_mock_parse_u32_strict(roleText, &roleId) || roleId == 0 ||
        account[0] == 0)
        return false;
    (void)vm_mock_admin_utf8_to_gbk_text(account, accountGbk, sizeof(accountGbk),
                                          false);
    (void)vm_mock_admin_utf8_to_gbk_text(password, passwordGbk, sizeof(passwordGbk),
                                          false);
    if (password[0] != 0 &&
        vm_mock_service_account_verify_credentials(account, password))
    {
        snprintf(accountOut, accountCap, "%s", account);
        authed = true;
    }
    else if (password[0] != 0 && accountGbk[0] && passwordGbk[0] &&
             vm_mock_service_account_verify_credentials(accountGbk, passwordGbk))
    {
        snprintf(accountOut, accountCap, "%s", accountGbk);
        authed = true;
    }
    else if (password[0] != 0 && accountGbk[0] &&
             vm_mock_service_account_verify_credentials(accountGbk, password))
    {
        snprintf(accountOut, accountCap, "%s", accountGbk);
        authed = true;
    }
    else if (vm_mock_payment_role_balance(account, roleId, false, &scratch))
    {
        snprintf(accountOut, accountCap, "%s", account);
        authed = true;
    }
    else if (accountGbk[0] &&
             vm_mock_payment_role_balance(accountGbk, roleId, false, &scratch))
    {
        snprintf(accountOut, accountCap, "%s", accountGbk);
        authed = true;
    }
    memset(password, 0, sizeof(password));
    memset(passwordGbk, 0, sizeof(passwordGbk));
    if (!authed)
    {
        if (errOut)
            *errOut = "账号或密码错误";
        return false;
    }
    *roleIdOut = roleId;
    if (errOut)
        *errOut = NULL;
    return true;
}

static bool vm_mock_zangbao_auth_query(const char *query, char *accountOut,
                                       size_t accountCap, u32 *roleIdOut,
                                       const char **errOut)
{
    /* GET: account/password/role_id in query string (same fields as POST body). */
    return vm_mock_zangbao_auth(query, accountOut, accountCap, roleIdOut, errOut);
}

static bool vm_mock_zangbao_u32_row(void *ctx, unsigned int cols,
                                    const char *const *vals, const size_t *lens)
{
    u32 *out = (u32 *)ctx;
    (void)cols;
    if (out == NULL || vals == NULL || vals[0] == NULL)
        return true;
    (void)vm_mock_mysql_parse_u32(vals[0], lens ? lens[0] : strlen(vals[0]), out);
    return true;
}

static bool vm_mock_zangbao_listing_row_cb(void *ctx, unsigned int cols,
                                           const char *const *vals,
                                           const size_t *lens)
{
    vm_mock_zangbao_listing *row = (vm_mock_zangbao_listing *)ctx;
    u32 tmp = 0;
    size_t nlen = 0;
    char wash[96];

    if (row == NULL || cols < 18)
        return true;
    memset(row, 0, sizeof(*row));
    row->found = true;
    (void)vm_mock_mysql_parse_u32(vals[0], lens[0], &row->listingId);
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &row->sellerRoleId);
    if (vals[2] && lens[2])
    {
        size_t copy = lens[2] < sizeof(row->sellerAccountId) - 1
                          ? lens[2]
                          : sizeof(row->sellerAccountId) - 1;
        memcpy(row->sellerAccountId, vals[2], copy);
        row->sellerAccountId[copy] = 0;
    }
    if (vals[3])
        (void)vm_mysql_hex_decode(vals[3], lens[3], row->sellerName,
                                  sizeof(row->sellerName) - 1, &nlen);
    row->sellerName[nlen] = 0;
    (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &row->itemId);
    (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &row->itemCount);
    (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &tmp);
    row->enhanceLevel = (u16)tmp;
    (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &row->itemInstanceId);
    memset(wash, 0, sizeof(wash));
    if (vals[8] && lens[8])
    {
        size_t copy = lens[8] < sizeof(wash) - 1 ? lens[8] : sizeof(wash) - 1;
        memcpy(wash, vals[8], copy);
    }
    (void)vm_net_mock_enhance_wash_decode(wash, strlen(wash), &row->enhanceWash);
    (void)vm_mock_mysql_parse_u32(vals[9], lens[9], &tmp);
    row->gemReflect = (u8)tmp;
    (void)vm_mock_mysql_parse_u32(vals[10], lens[10], &tmp);
    row->gemCombo = (u8)tmp;
    (void)vm_mock_mysql_parse_u32(vals[11], lens[11], &tmp);
    row->gemRevive = (u8)tmp;
    (void)vm_mock_mysql_parse_u32(vals[12], lens[12], &row->priceKcoin);
    (void)vm_mock_mysql_parse_u32(vals[13], lens[13], &row->status);
    (void)vm_mock_mysql_parse_u32(vals[14], lens[14], &row->buyerRoleId);
    if (vals[15] && lens[15])
    {
        size_t copy = lens[15] < sizeof(row->buyerAccountId) - 1
                          ? lens[15]
                          : sizeof(row->buyerAccountId) - 1;
        memcpy(row->buyerAccountId, vals[15], copy);
        row->buyerAccountId[copy] = 0;
    }
    (void)vm_mock_mysql_parse_u32(vals[16], lens[16], &row->reserveExpireUnix);
    if (vals[17] && lens[17])
    {
        size_t copy = lens[17] < sizeof(row->payId) - 1 ? lens[17]
                                                        : sizeof(row->payId) - 1;
        memcpy(row->payId, vals[17], copy);
        row->payId[copy] = 0;
    }
    return true;
}

static bool vm_mock_zangbao_load_listing(u32 listingId, vm_mock_zangbao_listing *out)
{
    char query[640];

    if (out == NULL || listingId == 0)
        return false;
    memset(out, 0, sizeof(*out));
    snprintf(query, sizeof(query),
             "SELECT listing_id,seller_role_id,seller_account_id,HEX(seller_name),"
             "item_id,item_count,enhance_level,item_instance_id,enhance_wash,"
             "gem_reflect,gem_combo,gem_revive,price_kcoin,status,buyer_role_id,"
             "buyer_account_id,reserve_expire_unix,pay_id "
             "FROM server_zangbao_listings WHERE listing_id=%u LIMIT 1",
             listingId);
    (void)vm_mysql_query(query, vm_mock_zangbao_listing_row_cb, out);
    return out->found;
}

static void vm_mock_zangbao_release_expired_reserves(void)
{
    char query[384];
    u32 now = (u32)time(NULL);

    snprintf(query, sizeof(query),
             "UPDATE server_zangbao_listings SET status=%u,buyer_role_id=0,"
             "buyer_account_id='',reserve_expire_unix=0,pay_id='' "
             "WHERE status=%u AND reserve_expire_unix>0 AND reserve_expire_unix<%u",
             VM_MOCK_ZANGBAO_ST_ACTIVE, VM_MOCK_ZANGBAO_ST_RESERVED, now);
    (void)vm_mysql_exec(query);
}

static int vm_mock_zangbao_json_ok(vm_mock_service_socket client, const char *json)
{
    return vm_mock_admin_send_response(
        client, "200 OK", "application/json; charset=utf-8",
        "Access-Control-Allow-Origin: *\r\n", json ? json : "{\"ok\":true}\n");
}

static int vm_mock_zangbao_json_err(vm_mock_service_socket client, const char *status,
                                    const char *msg)
{
    char esc[256];
    char json[384];

    if (!vm_mock_admin_json_escape(msg ? msg : "失败", esc, sizeof(esc)))
        snprintf(esc, sizeof(esc), "失败");
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}\n", esc);
    return vm_mock_admin_send_response(
        client, status ? status : "400 Bad Request",
        "application/json; charset=utf-8", "Access-Control-Allow-Origin: *\r\n",
        json);
}

/* ---- list / cancel / browse ------------------------------------------------ */

static int vm_mock_zangbao_api_list_create(vm_mock_service_socket client,
                                           const char *body)
{
    char account[64];
    char bagText[32];
    char priceText[32];
    char countText[32];
    char roleSel[32];
    const char *err = NULL;
    u32 roleId = 0;
    u32 bagSeq = 0;
    u32 price = 0;
    u32 count = 0;
    u32 remaining = 0;
    u32 listingId = 0;
    vm_mock_kcoin_config cfg;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *bag = NULL;
    vm_net_mock_role_state before;
    char accountHex[160];
    char nameHex[96];
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char query[1400];
    char json[256];

    if (!vm_mock_zangbao_ensure_schema() || !vm_mock_kcoin_load_config(&cfg) ||
        !cfg.enabled)
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "藏宝阁未开放");
    if (!vm_mock_zangbao_auth(body, account, sizeof(account), &roleId, &err))
        return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                        err ? err : "鉴权失败");
    memset(bagText, 0, sizeof(bagText));
    memset(priceText, 0, sizeof(priceText));
    memset(countText, 0, sizeof(countText));
    if (!vm_mock_admin_form_value(body, "bag_seq", bagText, sizeof(bagText)) ||
        !vm_mock_admin_form_value(body, "price_kcoin", priceText,
                                  sizeof(priceText)) ||
        !vm_net_mock_parse_u32_strict(bagText, &bagSeq) || bagSeq == 0 ||
        !vm_net_mock_parse_u32_strict(priceText, &price) ||
        price < VM_MOCK_ZANGBAO_MIN_PRICE)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        "价格至少10酷币");
    count = 0;
    if (vm_mock_admin_form_value(body, "count", countText, sizeof(countText)))
        (void)vm_net_mock_parse_u32_strict(countText, &count);

    state = vm_mock_service_open_account_role_db_for_management(account, &err);
    if (state == NULL)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        err ? err : "角色加载失败");
    snprintf(roleSel, sizeof(roleSel), "%u", roleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
    if (role == NULL)
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "角色不存在");
    }
    bag = vm_net_mock_role_find_backpack_item(role, 0, (u16)bagSeq);
    if (bag == NULL || bag->itemId == 0 || bag->count == 0)
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "背包物品不存在");
    }
    if (bag->itemId == VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID)
    {
        if (bag->enhanceInstanceId == 0)
        {
            vm_mock_service_close_account_role_db_for_management(state, true);
            return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                            "空白卷不可上架");
        }
        if (!vm_net_mock_role_seal_instance_exists(bag->enhanceInstanceId))
        {
            vm_mock_service_close_account_role_db_for_management(state, true);
            return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                            "封印数据缺失不可上架");
        }
    }
    if (count == 0 || count > bag->count)
        count = bag->count;
    {
        u32 activeCount = 0;
        char limitMsg[64];

        /* Occupied slots: on-sale + payment-reserved (not yet sold/cancelled). */
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM server_zangbao_listings "
                 "WHERE seller_role_id=%u AND status IN (%u,%u)",
                 role->roleId, VM_MOCK_ZANGBAO_ST_ACTIVE,
                 VM_MOCK_ZANGBAO_ST_RESERVED);
        (void)vm_mysql_query(query, vm_mock_zangbao_u32_row, &activeCount);
        if (activeCount >= cfg.maxActiveListings)
        {
            vm_mock_service_close_account_role_db_for_management(state, true);
            snprintf(limitMsg, sizeof(limitMsg), "每人最多同时上架%u件",
                     cfg.maxActiveListings);
            return vm_mock_zangbao_json_err(client, "400 Bad Request", limitMsg);
        }
    }
    {
        vm_net_mock_backpack_item_state snapCopy = *bag;
        before = *role;
        if (!vm_net_mock_role_consume_backpack_item(role, snapCopy.itemId,
                                                    (u16)bagSeq, count, &remaining))
        {
            *role = before;
            vm_mock_service_close_account_role_db_for_management(state, true);
            return vm_mock_zangbao_json_err(client, "400 Bad Request", "扣除物品失败");
        }
        if (!vm_net_mock_role_db_save_role("zangbao-list", role->roleId))
        {
            *role = before;
            (void)vm_net_mock_role_db_save_role("zangbao-list-rollback", role->roleId);
            vm_mock_service_close_account_role_db_for_management(state, false);
            return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                            "保存失败");
        }
        if (vm_mysql_hex_encode(account, strlen(account), accountHex,
                                sizeof(accountHex)) == 0 ||
            vm_mysql_hex_encode(role->name, strlen(role->name), nameHex,
                                sizeof(nameHex)) == 0)
        {
            *role = before;
            (void)vm_net_mock_role_db_save_role("zangbao-list-rollback", role->roleId);
            vm_mock_service_close_account_role_db_for_management(state, false);
            return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                            "编码失败");
        }
        if (!vm_net_mock_enhance_wash_encode(&snapCopy.enhanceWash, washText,
                                             sizeof(washText)))
            snprintf(washText, sizeof(washText),
                     "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
        snprintf(query, sizeof(query),
                 "INSERT INTO server_zangbao_listings ("
                 "seller_role_id,seller_account_id,seller_name,item_id,item_count,"
                 "enhance_level,item_instance_id,enhance_wash,gem_reflect,gem_combo,"
                 "gem_revive,price_kcoin,status) VALUES ("
                 "%u,CAST(X'%s' AS CHAR),UNHEX('%s'),%u,%u,%u,%u,'%s',%u,%u,%u,%u,%u)",
                 role->roleId, accountHex, nameHex, snapCopy.itemId, count,
                 (u32)snapCopy.enhanceLevel, snapCopy.enhanceInstanceId, washText,
                 (u32)snapCopy.gemReflectLevel, (u32)snapCopy.gemComboLevel,
                 (u32)snapCopy.gemReviveLevel, price, VM_MOCK_ZANGBAO_ST_ACTIVE);
        if (!vm_mysql_exec(query))
        {
            *role = before;
            (void)vm_net_mock_role_db_save_role("zangbao-list-rollback", role->roleId);
            vm_mock_service_close_account_role_db_for_management(state, false);
            return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                            "上架写入失败");
        }
    }
    listingId = 0;
    (void)vm_mysql_query("SELECT LAST_INSERT_ID()", vm_mock_zangbao_u32_row,
                         &listingId);
    vm_mock_service_account_capture(state);
    vm_mock_service_close_account_role_db_for_management(state, false);
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"listingId\":%u,\"priceKcoin\":%u}\n", listingId, price);
    return vm_mock_zangbao_json_ok(client, json);
}

static int vm_mock_zangbao_api_cancel(vm_mock_service_socket client, const char *body)
{
    char account[64];
    char idText[32];
    char roleSel[32];
    const char *err = NULL;
    u32 roleId = 0;
    u32 listingId = 0;
    vm_mock_zangbao_listing row;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    char query[320];
    u16 seq = 0;

    if (!vm_mock_zangbao_ensure_schema())
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "服务不可用");
    if (!vm_mock_zangbao_auth(body, account, sizeof(account), &roleId, &err))
        return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                        err ? err : "鉴权失败");
    memset(idText, 0, sizeof(idText));
    if (!vm_mock_admin_form_value(body, "listing_id", idText, sizeof(idText)) ||
        !vm_net_mock_parse_u32_strict(idText, &listingId) || listingId == 0)
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "缺少上架编号");
    vm_mock_zangbao_release_expired_reserves();
    if (!vm_mock_zangbao_load_listing(listingId, &row) ||
        row.status != VM_MOCK_ZANGBAO_ST_ACTIVE ||
        row.sellerRoleId != roleId ||
        strcmp(row.sellerAccountId, account) != 0)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        "无法下架该物品");
    state = vm_mock_service_open_account_role_db_for_management(account, &err);
    if (state == NULL)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        err ? err : "角色加载失败");
    snprintf(roleSel, sizeof(roleSel), "%u", roleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
    if (role == NULL)
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "角色不存在");
    }
    if (!vm_mock_auction_return_item_to_bag(
            role, row.itemId, row.itemCount, row.enhanceLevel, row.itemInstanceId,
            &row.enhanceWash, &seq, "zangbao-cancel", row.gemReflect, row.gemCombo,
            row.gemRevive))
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "背包已满无法下架");
    }
    if (!vm_net_mock_role_db_save_role("zangbao-cancel", role->roleId))
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "保存失败");
    }
    snprintf(query, sizeof(query),
             "UPDATE server_zangbao_listings SET status=%u WHERE listing_id=%u "
             "AND status=%u AND seller_role_id=%u",
             VM_MOCK_ZANGBAO_ST_CANCELLED, listingId, VM_MOCK_ZANGBAO_ST_ACTIVE,
             roleId);
    if (!vm_mysql_exec(query))
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "下架失败");
    }
    vm_mock_service_account_capture(state);
    vm_mock_service_close_account_role_db_for_management(state, false);
    return vm_mock_zangbao_json_ok(client, "{\"ok\":true}\n");
}

typedef struct
{
    char *json;
    size_t cap;
    size_t len;
    u32 count;
    bool first;
    bool invalid;
} vm_mock_zangbao_list_json_ctx;

static bool vm_mock_zangbao_browse_row(void *ctx, unsigned int cols,
                                       const char *const *vals, const size_t *lens)
{
    vm_mock_zangbao_list_json_ctx *c = (vm_mock_zangbao_list_json_ctx *)ctx;
    u32 listingId = 0, sellerRoleId = 0, itemId = 0, itemCount = 0, enhance = 0;
    u32 price = 0, status = 0, itemInstanceId = 0;
    u32 gemReflect = 0, gemCombo = 0, gemRevive = 0;
    u32 sealedRoleId = 0, sealedLevel = 0, sealedJob = 0, sealedSex = 0;
    char sellerName[64];
    char sellerEsc[128];
    char nameUtf8[96];
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char washEsc[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX * 2 + 8];
    char sealedName[32];
    char sealedNameUtf8[96];
    char sealedEsc[128];
    char sealedJson[256];
    size_t nlen = 0;
    int n;

    if (c == NULL || c->json == NULL || cols < 8)
        return true;
    (void)vm_mock_mysql_parse_u32(vals[0], lens[0], &listingId);
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &sellerRoleId);
    memset(sellerName, 0, sizeof(sellerName));
    if (vals[2])
        (void)vm_mysql_hex_decode(vals[2], lens[2], sellerName,
                                  sizeof(sellerName) - 1, &nlen);
    sellerName[nlen] = 0;
    memset(nameUtf8, 0, sizeof(nameUtf8));
    gbk_to_utf8((u8 *)sellerName, (u8 *)nameUtf8, sizeof(nameUtf8));
    if (nameUtf8[0] == 0)
        snprintf(nameUtf8, sizeof(nameUtf8), "%s", sellerName);
    if (!vm_mock_admin_json_escape(nameUtf8, sellerEsc, sizeof(sellerEsc)))
        snprintf(sellerEsc, sizeof(sellerEsc), "-");
    (void)vm_mock_mysql_parse_u32(vals[3], lens[3], &itemId);
    (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &itemCount);
    (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &enhance);
    (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &price);
    (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &status);
    sealedJson[0] = 0;
    memset(washText, 0, sizeof(washText));
    snprintf(washText, sizeof(washText),
             "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    if (cols >= 9)
        (void)vm_mock_mysql_parse_u32(vals[8], lens[8], &itemInstanceId);
    /* Listing wash/gems: authority for equip detail (same as auction common-extra). */
    if (cols >= 10 && vals[9] != NULL && lens[9] > 0)
    {
        size_t copy = lens[9] < sizeof(washText) - 1 ? lens[9] : sizeof(washText) - 1;
        memcpy(washText, vals[9], copy);
        washText[copy] = 0;
    }
    if (cols >= 13)
    {
        (void)vm_mock_mysql_parse_u32(vals[10], lens[10], &gemReflect);
        (void)vm_mock_mysql_parse_u32(vals[11], lens[11], &gemCombo);
        (void)vm_mock_mysql_parse_u32(vals[12], lens[12], &gemRevive);
    }
    if (itemId == VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID && cols >= 18 &&
        vals[13] != NULL && lens[13] > 0)
    {
        nlen = 0;
        memset(sealedName, 0, sizeof(sealedName));
        (void)vm_mock_mysql_parse_u32(vals[13], lens[13], &sealedRoleId);
        if (vals[14])
            (void)vm_mysql_hex_decode(vals[14], lens[14], sealedName,
                                      sizeof(sealedName) - 1, &nlen);
        sealedName[nlen] = 0;
        (void)vm_mock_mysql_parse_u32(vals[15], lens[15], &sealedLevel);
        (void)vm_mock_mysql_parse_u32(vals[16], lens[16], &sealedJob);
        (void)vm_mock_mysql_parse_u32(vals[17], lens[17], &sealedSex);
        memset(sealedNameUtf8, 0, sizeof(sealedNameUtf8));
        gbk_to_utf8((u8 *)sealedName, (u8 *)sealedNameUtf8,
                    sizeof(sealedNameUtf8));
        if (sealedNameUtf8[0] == 0)
            snprintf(sealedNameUtf8, sizeof(sealedNameUtf8), "%s", sealedName);
        if (!vm_mock_admin_json_escape(sealedNameUtf8, sealedEsc,
                                        sizeof(sealedEsc)))
            snprintf(sealedEsc, sizeof(sealedEsc), "-");
        snprintf(sealedJson, sizeof(sealedJson),
                 ",\"sealedRole\":{\"roleId\":%u,\"name\":\"%s\",\"level\":%u,"
                 "\"job\":%u,\"sex\":%u}",
                 sealedRoleId, sealedEsc, sealedLevel, sealedJob, sealedSex);
    }
    if (!vm_mock_admin_json_escape(washText, washEsc, sizeof(washEsc)))
        snprintf(washEsc, sizeof(washEsc),
                 "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    n = snprintf(c->json + c->len, c->cap > c->len ? c->cap - c->len : 0,
                 "%s{\"listingId\":%u,\"sellerRoleId\":%u,\"sellerName\":\"%s\","
                 "\"itemId\":%u,\"itemCount\":%u,\"enhanceLevel\":%u,"
                 "\"itemInstanceId\":%u,\"enhanceWash\":\"%s\","
                 "\"gemReflect\":%u,\"gemCombo\":%u,\"gemRevive\":%u,"
                 "\"priceKcoin\":%u,\"status\":%u%s}",
                 c->first ? "" : ",", listingId, sellerRoleId, sellerEsc, itemId,
                 itemCount, enhance, itemInstanceId, washEsc, gemReflect,
                 gemCombo, gemRevive, price, status, sealedJson);
    if (n < 0 || (size_t)n >= (c->cap > c->len ? c->cap - c->len : 0))
    {
        c->invalid = true;
        return false;
    }
    c->len += (size_t)n;
    c->first = false;
    c->count++;
    return true;
}

static int vm_mock_zangbao_api_browse(vm_mock_service_socket client, const char *query,
                                      bool mineOnly)
{
    char account[64];
    char pageText[16];
    const char *err = NULL;
    u32 roleId = 0;
    u32 page = 1;
    u32 offset = 0;
    char sql[960];
    char *json = NULL;
    vm_mock_zangbao_list_json_ctx ctx;
    char accountHex[160];

    if (!vm_mock_zangbao_ensure_schema())
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "服务不可用");
    vm_mock_zangbao_release_expired_reserves();
    if (mineOnly)
    {
        if (!vm_mock_zangbao_auth_query(query, account, sizeof(account), &roleId,
                                         &err))
            return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                            err ? err : "鉴权失败");
    }
    else
    {
        account[0] = 0;
        /* optional auth for browse */
        (void)vm_mock_zangbao_auth_query(query, account, sizeof(account), &roleId,
                                          &err);
    }
    memset(pageText, 0, sizeof(pageText));
    if (vm_mock_admin_form_value(query, "page", pageText, sizeof(pageText)))
        (void)vm_net_mock_parse_u32_strict(pageText, &page);
    if (page == 0)
        page = 1;
    offset = (page - 1) * VM_MOCK_ZANGBAO_PAGE_SIZE;
    json = (char *)malloc(65536);
    if (json == NULL)
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "内存不足");
    memset(&ctx, 0, sizeof(ctx));
    ctx.json = json;
    ctx.cap = 65536;
    ctx.first = true;
    ctx.len = (size_t)snprintf(json, 65536, "{\"ok\":true,\"page\":%u,\"items\":[",
                               page);
    if (mineOnly)
    {
        if (vm_mysql_hex_encode(account, strlen(account), accountHex,
                                sizeof(accountHex)) == 0)
        {
            free(json);
            return vm_mock_zangbao_json_err(client, "400 Bad Request", "账号无效");
        }
        (void)vm_net_mock_role_seal_ensure_schema();
        snprintf(sql, sizeof(sql),
                 "SELECT z.listing_id,z.seller_role_id,HEX(z.seller_name),z.item_id,"
                 "z.item_count,z.enhance_level,z.price_kcoin,z.status,"
                 "z.item_instance_id,z.enhance_wash,z.gem_reflect,z.gem_combo,"
                 "z.gem_revive,sr.sealed_role_id,HEX(sr.preview_name),"
                 "sr.preview_level,sr.preview_job,sr.preview_sex "
                 "FROM server_zangbao_listings z "
                 "LEFT JOIN server_sealed_roles sr "
                 "ON z.item_id=%u AND z.item_instance_id=sr.item_instance_id "
                 "WHERE z.seller_role_id=%u AND z.seller_account_id=CAST(X'%s' AS CHAR) "
                 "AND z.status IN (%u,%u) ORDER BY z.listing_id DESC LIMIT %u OFFSET %u",
                 VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID, roleId, accountHex,
                 VM_MOCK_ZANGBAO_ST_ACTIVE, VM_MOCK_ZANGBAO_ST_RESERVED,
                 VM_MOCK_ZANGBAO_PAGE_SIZE, offset);
    }
    else
    {
        (void)vm_net_mock_role_seal_ensure_schema();
        snprintf(sql, sizeof(sql),
                 "SELECT z.listing_id,z.seller_role_id,HEX(z.seller_name),z.item_id,"
                 "z.item_count,z.enhance_level,z.price_kcoin,z.status,"
                 "z.item_instance_id,z.enhance_wash,z.gem_reflect,z.gem_combo,"
                 "z.gem_revive,sr.sealed_role_id,HEX(sr.preview_name),"
                 "sr.preview_level,sr.preview_job,sr.preview_sex "
                 "FROM server_zangbao_listings z "
                 "LEFT JOIN server_sealed_roles sr "
                 "ON z.item_id=%u AND z.item_instance_id=sr.item_instance_id "
                 "WHERE z.status=%u ORDER BY z.listing_id DESC LIMIT %u OFFSET %u",
                 VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID, VM_MOCK_ZANGBAO_ST_ACTIVE,
                 VM_MOCK_ZANGBAO_PAGE_SIZE, offset);
    }
    (void)vm_mysql_query(sql, vm_mock_zangbao_browse_row, &ctx);
    if (ctx.invalid || ctx.len + 8 >= ctx.cap)
    {
        free(json);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "列表过长");
    }
    snprintf(json + ctx.len, ctx.cap - ctx.len, "]}\n");
    {
        int rc = vm_mock_zangbao_json_ok(client, json);
        free(json);
        return rc;
    }
}

/* ---- buy / settle / claims / kcoin (part2) ------------------------------- */

static bool vm_mock_zangbao_market_order_row(void *ctx, unsigned int cols,
                                             const char *const *vals,
                                             const size_t *lens)
{
    vm_mock_zangbao_market_order *o = (vm_mock_zangbao_market_order *)ctx;
    u32 tmp = 0;
    size_t decoded = 0;

    if (o == NULL || cols < 15)
        return true;
    memset(o, 0, sizeof(*o));
    o->found = true;
    if (vals[0] == NULL ||
        !vm_mysql_hex_decode(vals[0], lens[0], o->payId, sizeof(o->payId) - 1,
                             &decoded))
    {
        o->found = false;
        return true;
    }
    o->payId[decoded] = 0;
    if (vals[1] && lens[1])
    {
        size_t c = lens[1] < sizeof(o->accountId) - 1 ? lens[1]
                                                       : sizeof(o->accountId) - 1;
        memcpy(o->accountId, vals[1], c);
    }
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &o->roleId);
    if (vals[3] && lens[3])
    {
        size_t c = lens[3] < sizeof(o->sellerAccountId) - 1
                       ? lens[3]
                       : sizeof(o->sellerAccountId) - 1;
        memcpy(o->sellerAccountId, vals[3], c);
    }
    (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &o->sellerRoleId);
    (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &o->listingId);
    (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &o->payType);
    (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &o->priceCents);
    (void)vm_mock_mysql_parse_u32(vals[8], lens[8], &o->reallyPriceCents);
    (void)vm_mock_mysql_parse_u32(vals[9], lens[9], &o->priceKcoin);
    (void)vm_mock_mysql_parse_u32(vals[10], lens[10], &o->sellerCreditKcoin);
    (void)vm_mock_mysql_parse_u32(vals[11], lens[11], &o->feeKcoin);
    decoded = 0;
    if (vals[12] == NULL ||
        !vm_mysql_hex_decode(vals[12], lens[12], o->requestParam,
                             sizeof(o->requestParam) - 1, &decoded))
    {
        o->found = false;
        return true;
    }
    o->requestParam[decoded] = 0;
    (void)vm_mock_mysql_parse_u32(vals[13], lens[13], &o->status);
    (void)vm_mock_mysql_parse_u32(vals[14], lens[14], &tmp);
    o->credited = tmp != 0;
    if (cols > 15)
        (void)vm_mock_mysql_parse_u32(vals[15], lens[15], &o->timeoutMinutes);
    return true;
}

static bool vm_mock_zangbao_load_market_order(const char *payId, bool forUpdate,
                                              vm_mock_zangbao_market_order *out)
{
    char query[900];

    if (out == NULL || payId == NULL || payId[0] == 0)
        return false;
    for (const char *p = payId; *p != 0; ++p)
    {
        unsigned char ch = (unsigned char)*p;
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z')))
            return false;
    }
    memset(out, 0, sizeof(*out));
    /* pay_id is ascii token (ZB…); LIMIT must precede FOR UPDATE on MySQL 5.7. */
    snprintf(query, sizeof(query),
             "SELECT HEX(pay_id),account_id,role_id,seller_account_id,seller_role_id,"
             "listing_id,pay_type,price_cents,really_price_cents,price_kcoin,"
             "seller_credit_kcoin,fee_kcoin,HEX(request_param),"
             "status,credited,timeout_minutes FROM kcoin_market_orders "
             "WHERE pay_id='%s' LIMIT 1%s",
             payId, forUpdate ? " FOR UPDATE" : "");
    if (!vm_mysql_query(query, vm_mock_zangbao_market_order_row, out))
    {
        printf("[warn][zangbao] load_market_order query_failed pay_id=%s err=%s\n",
               payId, vm_mysql_last_error());
        return false;
    }
    if (!out->found)
    {
        printf("[warn][zangbao] load_market_order miss pay_id=%s for_update=%d\n",
               payId, forUpdate ? 1 : 0);
    }
    return out->found;
}

static bool vm_mock_zangbao_deliver_or_claim(const vm_mock_zangbao_listing *row,
                                             const char *buyerAccount,
                                             u32 buyerRoleId)
{
    char roleSel[32];
    const char *err = NULL;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    u16 seq = 0;
    char accountHex[160];
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char query[900];

    if (row == NULL || buyerAccount == NULL)
        return false;
    state = vm_mock_service_open_account_role_db_for_management(buyerAccount, &err);
    if (state == NULL)
        goto claim;
    snprintf(roleSel, sizeof(roleSel), "%u", buyerRoleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
    if (role == NULL)
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        goto claim;
    }
    if (vm_mock_auction_return_item_to_bag(
            role, row->itemId, row->itemCount, row->enhanceLevel,
            row->itemInstanceId, &row->enhanceWash, &seq, "zangbao-buy",
            row->gemReflect, row->gemCombo, row->gemRevive) &&
        vm_net_mock_role_db_save_role("zangbao-buy", role->roleId))
    {
        vm_mock_service_account_capture(state);
        vm_mock_service_close_account_role_db_for_management(state, false);
        return true;
    }
    vm_mock_service_close_account_role_db_for_management(state, true);
claim:
    if (vm_mysql_hex_encode(buyerAccount, strlen(buyerAccount), accountHex,
                            sizeof(accountHex)) == 0)
        return false;
    if (!vm_net_mock_enhance_wash_encode(&row->enhanceWash, washText,
                                          sizeof(washText)))
        snprintf(washText, sizeof(washText),
                 "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    snprintf(query, sizeof(query),
             "INSERT INTO server_zangbao_claims ("
             "role_id,account_id,listing_id,item_id,item_count,enhance_level,"
             "item_instance_id,enhance_wash,gem_reflect,gem_combo,gem_revive,claimed) "
             "VALUES(%u,CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,'%s',%u,%u,%u,0)",
             buyerRoleId, accountHex, row->listingId, row->itemId, row->itemCount,
             (u32)row->enhanceLevel, row->itemInstanceId, washText,
             (u32)row->gemReflect, (u32)row->gemCombo, (u32)row->gemRevive);
    return vm_mysql_exec(query);
}

static bool vm_mock_zangbao_credit_seller_kcoin(const char *sellerAccount,
                                                u32 sellerRoleId, u32 amount)
{
    char roleSel[32];
    const char *err = NULL;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    u32 after = 0;

    if (sellerAccount == NULL || amount == 0)
        return true;
    state = vm_mock_service_open_account_role_db_for_management(sellerAccount, &err);
    if (state == NULL)
        return false;
    snprintf(roleSel, sizeof(roleSel), "%u", sellerRoleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
    if (role == NULL)
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    after = vm_net_mock_role_add_kcoin(role, amount);
    if (!vm_net_mock_role_db_save_role("zangbao-seller-credit", role->roleId))
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    vm_mock_service_account_capture(state);
    vm_mock_zangbao_sync_cached_kcoin(sellerAccount, sellerRoleId, after);
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}

static bool vm_mock_zangbao_insert_claim_sql(const vm_mock_zangbao_listing *row,
                                             const char *buyerAccount,
                                             u32 buyerRoleId)
{
    char accountHex[160];
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char washEsc[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX * 2 + 8];
    char query[1200];
    size_t i;
    size_t j = 0;

    if (row == NULL || buyerAccount == NULL)
        return false;
    if (vm_mysql_hex_encode(buyerAccount, strlen(buyerAccount), accountHex,
                            sizeof(accountHex)) == 0)
        return false;
    if (!vm_net_mock_enhance_wash_encode(&row->enhanceWash, washText,
                                          sizeof(washText)))
        snprintf(washText, sizeof(washText),
                 "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    memset(washEsc, 0, sizeof(washEsc));
    for (i = 0; washText[i] != 0 && j + 2 < sizeof(washEsc); ++i)
    {
        char ch = washText[i];
        if (ch == '\'' || ch == '\\')
            washEsc[j++] = '\\';
        washEsc[j++] = ch;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_zangbao_claims ("
             "role_id,account_id,listing_id,item_id,item_count,enhance_level,"
             "item_instance_id,enhance_wash,gem_reflect,gem_combo,gem_revive,claimed) "
             "VALUES(%u,CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,'%s',%u,%u,%u,0)",
             buyerRoleId, accountHex, row->listingId, row->itemId, row->itemCount,
             (u32)row->enhanceLevel, row->itemInstanceId, washEsc,
             (u32)row->gemReflect, (u32)row->gemCombo, (u32)row->gemRevive);
    return vm_mysql_exec(query);
}

static bool vm_mock_zangbao_credit_seller_kcoin_sql(const char *sellerAccount,
                                                    u32 sellerRoleId, u32 amount,
                                                    u32 *afterOut)
{
    char accountHex[160];
    char query[512];
    u32 balance = 0;
    u32 after = 0;
    vm_mock_payment_u32_row row;

    if (afterOut)
        *afterOut = 0;
    if (sellerAccount == NULL || amount == 0)
    {
        if (afterOut)
            *afterOut = 0;
        return true;
    }
    if (vm_mysql_hex_encode(sellerAccount, strlen(sellerAccount), accountHex,
                            sizeof(accountHex)) == 0)
        return false;
    memset(&row, 0, sizeof(row));
    snprintf(query, sizeof(query),
             "SELECT kcoin FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u FOR UPDATE",
             accountHex, sellerRoleId);
    if (!vm_mysql_query(query, vm_mock_payment_u32_row_callback, &row) ||
        row.invalid || !row.found)
        return false;
    balance = row.value;
    if (UINT32_MAX - balance < amount)
        return false;
    after = balance + amount;
    snprintf(query, sizeof(query),
             "UPDATE account_roles SET kcoin=%u WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u",
             after, accountHex, sellerRoleId);
    if (!vm_mysql_exec(query))
        return false;
    if (afterOut)
        *afterOut = after;
    return true;
}

static void vm_mock_zangbao_try_deliver_after_settle(const vm_mock_zangbao_listing *row,
                                                     const char *buyerAccount,
                                                     u32 buyerRoleId)
{
    char roleSel[32];
    const char *err = NULL;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    u16 seq = 0;
    char query[256];

    /* Claim already inserted in the payment txn. Only try bag + mark claimed. */
    if (row == NULL || buyerAccount == NULL)
        return;
    state = vm_mock_service_open_account_role_db_for_management(buyerAccount, &err);
    if (state == NULL)
        return;
    snprintf(roleSel, sizeof(roleSel), "%u", buyerRoleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
    if (role == NULL)
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return;
    }
    if (!vm_mock_auction_return_item_to_bag(
            role, row->itemId, row->itemCount, row->enhanceLevel,
            row->itemInstanceId, &row->enhanceWash, &seq, "zangbao-settle",
            row->gemReflect, row->gemCombo, row->gemRevive) ||
        !vm_net_mock_role_db_save_role("zangbao-settle", role->roleId))
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return;
    }
    vm_mock_service_account_capture(state);
    vm_mock_service_close_account_role_db_for_management(state, false);
    snprintf(query, sizeof(query),
             "UPDATE server_zangbao_claims SET claimed=1 WHERE listing_id=%u "
             "AND role_id=%u AND claimed=0",
             row->listingId, buyerRoleId);
    (void)vm_mysql_exec(query);
}

static vm_mock_payment_settle_result vm_mock_zangbao_try_settle_verified(
    const vm_mock_payment_callback *callback)
{
    vm_mock_zangbao_market_order order;
    vm_mock_zangbao_listing listing;
    char payHex[127];
    char query[640];
    bool transaction = false;
    u32 sellerAfter = 0;

    if (callback == NULL || !vm_mock_zangbao_ensure_schema())
        return VM_MOCK_PAYMENT_SETTLE_NOT_MARKET;
    if (vm_mysql_hex_encode(callback->payId, strlen(callback->payId), payHex,
                            sizeof(payHex)) == 0)
        return VM_MOCK_PAYMENT_SETTLE_INVALID;
    /* Cheap existence probe — avoid START TRANSACTION on every W 币回调. */
    {
        vm_mock_zangbao_market_order probe;

        if (!vm_mock_zangbao_load_market_order(callback->payId, false, &probe))
            return VM_MOCK_PAYMENT_SETTLE_NOT_MARKET;
    }
    if (!vm_mysql_exec("START TRANSACTION"))
        return VM_MOCK_PAYMENT_SETTLE_INVALID;
    transaction = true;
    if (!vm_mock_zangbao_load_market_order(callback->payId, true, &order))
    {
        (void)vm_mysql_exec("ROLLBACK");
        return VM_MOCK_PAYMENT_SETTLE_NOT_MARKET;
    }
    if (strcmp(order.requestParam, callback->param) != 0 ||
        order.payType != callback->payType ||
        order.priceCents != callback->priceCents)
    {
        printf("[warn][zangbao] settle_mismatch pay_id=%s param_ok=%d type=%u/%u "
               "price=%u/%u\n",
               callback->payId,
               strcmp(order.requestParam, callback->param) == 0,
               order.payType, callback->payType, order.priceCents,
               callback->priceCents);
        goto invalid;
    }
    if (order.credited)
    {
        if (!vm_mysql_exec("COMMIT"))
            goto invalid_nr;
        return VM_MOCK_PAYMENT_SETTLE_CREDITED;
    }
    snprintf(query, sizeof(query),
             "UPDATE kcoin_market_orders SET status=2,really_price_cents=%u,"
             "paid_at=COALESCE(paid_at,CURRENT_TIMESTAMP) "
             "WHERE pay_id=CAST(X'%s' AS CHAR) AND credited=0",
             callback->reallyPriceCents, payHex);
    if (!vm_mysql_exec(query))
        goto invalid;
    if (!vm_mock_zangbao_load_listing(order.listingId, &listing) || !listing.found)
    {
        printf("[warn][zangbao] settle_missing_listing pay_id=%s listing=%u\n",
               order.payId, order.listingId);
        goto invalid;
    }
    /* Durable item path first — never call role_db_save inside this txn
     * (nested START TRANSACTION would commit early and make notify look like
     * error_sign). */
    if (!vm_mock_zangbao_insert_claim_sql(&listing, order.accountId, order.roleId))
        goto invalid;
    if (!vm_mock_zangbao_credit_seller_kcoin_sql(order.sellerAccountId,
                                                 order.sellerRoleId,
                                                 order.sellerCreditKcoin,
                                                 &sellerAfter))
        goto invalid;
    snprintf(query, sizeof(query),
             "UPDATE server_zangbao_listings SET status=%u WHERE listing_id=%u",
             VM_MOCK_ZANGBAO_ST_SOLD, order.listingId);
    if (!vm_mysql_exec(query))
        goto invalid;
    snprintf(query, sizeof(query),
             "UPDATE kcoin_market_orders SET status=3,credited=1,"
             "credited_at=CURRENT_TIMESTAMP WHERE pay_id=CAST(X'%s' AS CHAR) "
             "AND credited=0",
             payHex);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto invalid_nr;
    transaction = false;
    if (order.sellerCreditKcoin != 0)
        vm_mock_zangbao_sync_cached_kcoin(order.sellerAccountId, order.sellerRoleId,
                                          sellerAfter);
    /* Attempt bag delivery; claim remains if bag is full / role offline. */
    vm_mock_zangbao_try_deliver_after_settle(&listing, order.accountId, order.roleId);
    printf("[info][zangbao] settled pay_id=%s listing=%u seller=%u credit=%u "
           "seller_kcoin=%u\n",
           order.payId, order.listingId, order.sellerRoleId,
           order.sellerCreditKcoin, sellerAfter);
    return VM_MOCK_PAYMENT_SETTLE_CREDITED;
invalid:
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    return VM_MOCK_PAYMENT_SETTLE_INVALID;
invalid_nr:
    (void)vm_mysql_exec("ROLLBACK");
    return VM_MOCK_PAYMENT_SETTLE_INVALID;
}

static int vm_mock_zangbao_api_buy(vm_mock_service_socket client, const char *body)
{
    char account[64];
    char idText[32];
    char typeText[16];
    char roleSel[32];
    const char *err = NULL;
    u32 roleId = 0;
    u32 listingId = 0;
    u32 payType = 0;
    u32 yuan = 0;
    vm_mock_kcoin_config kcfg;
    vm_mock_payment_config pcfg;
    vm_mock_zangbao_listing row;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_mock_payment_create_result provider;
    char payId[64];
    char requestParam[64];
    char accountHex[160];
    char sellerHex[160];
    char paramHex[127];
    char payHex[127];
    char priceText[32];
    char signInput[512];
    char sign[33];
    char form[4096];
    char notifyUrl[512];
    char returnUrl[512];
    char notifyEncoded[1536];
    char returnEncoded[1536];
    char response[VM_MOCK_PAYMENT_HTTP_RESPONSE_MAX + 1];
    char query[1600];
    char json[4096];
    char payUrlEsc[2048];
    char payIdEsc[128];
    u32 credit = 0;
    u32 fee = 0;
    u32 expire = 0;
    int httpStatus = 0;
    bool httpOk = false;

    if (!vm_mock_zangbao_ensure_schema() || !vm_mock_kcoin_load_config(&kcfg) ||
        !kcfg.enabled)
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "藏宝阁未开放");
    if (!vm_mock_zangbao_auth(body, account, sizeof(account), &roleId, &err))
        return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                        err ? err : "鉴权失败");
    memset(idText, 0, sizeof(idText));
    memset(typeText, 0, sizeof(typeText));
    if (!vm_mock_admin_form_value(body, "listing_id", idText, sizeof(idText)) ||
        !vm_mock_admin_form_value(body, "pay_type", typeText, sizeof(typeText)) ||
        !vm_net_mock_parse_u32_strict(idText, &listingId) || listingId == 0 ||
        !vm_net_mock_parse_u32_strict(typeText, &payType) ||
        (payType != 1 && payType != 2))
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "购买参数无效");
    vm_mock_zangbao_release_expired_reserves();
    if (!vm_mock_zangbao_load_listing(listingId, &row) ||
        row.status != VM_MOCK_ZANGBAO_ST_ACTIVE)
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "商品不可购买");
    if (row.sellerRoleId == roleId || strcmp(row.sellerAccountId, account) == 0)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        "不能购买自己的物品");
    state = vm_mock_service_open_account_role_db_for_management(account, &err);
    if (state == NULL)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        err ? err : "角色加载失败");
    snprintf(roleSel, sizeof(roleSel), "%u", roleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
    if (role == NULL || !vm_mock_zangbao_bag_has_space(role))
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "背包空间不足");
    }
    vm_mock_service_close_account_role_db_for_management(state, true);
    if (!vm_mock_payment_load_config(&pcfg) || !pcfg.enabled)
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "赞助通道不可用");
    yuan = vm_mock_zangbao_yuan_for_price(row.priceKcoin, kcfg.kcoinPerYuan);
    if (yuan < pcfg.minimumYuan)
        yuan = pcfg.minimumYuan;
    if (yuan > pcfg.maximumYuan)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        "金额超出赞助上限");
    credit = vm_mock_zangbao_seller_credit(row.priceKcoin);
    fee = row.priceKcoin - credit;
    expire = (u32)time(NULL) + VM_MOCK_ZANGBAO_RESERVE_SEC;
    vm_mock_payment_generate_token("ZB", payId, sizeof(payId));
    vm_mock_payment_generate_token("P", requestParam, sizeof(requestParam));
    if (vm_mysql_hex_encode(account, strlen(account), accountHex,
                            sizeof(accountHex)) == 0 ||
        vm_mysql_hex_encode(row.sellerAccountId, strlen(row.sellerAccountId),
                            sellerHex, sizeof(sellerHex)) == 0 ||
        vm_mysql_hex_encode(requestParam, strlen(requestParam), paramHex,
                            sizeof(paramHex)) == 0 ||
        vm_mysql_hex_encode(payId, strlen(payId), payHex, sizeof(payHex)) == 0)
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "编码失败");
    snprintf(query, sizeof(query),
             "UPDATE server_zangbao_listings SET status=%u,buyer_role_id=%u,"
             "buyer_account_id=CAST(X'%s' AS CHAR),reserve_expire_unix=%u,"
             "pay_id=CAST(X'%s' AS CHAR) WHERE listing_id=%u AND status=%u",
             VM_MOCK_ZANGBAO_ST_RESERVED, roleId, accountHex, expire, payHex,
             listingId, VM_MOCK_ZANGBAO_ST_ACTIVE);
    if (!vm_mysql_exec(query))
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "锁定商品失败");
    snprintf(query, sizeof(query),
             "INSERT INTO kcoin_market_orders ("
             "pay_id,account_id,role_id,seller_account_id,seller_role_id,listing_id,"
             "pay_type,price_cents,price_kcoin,seller_credit_kcoin,fee_kcoin,"
             "request_param,status) VALUES ("
             "CAST(X'%s' AS CHAR),CAST(X'%s' AS CHAR),%u,CAST(X'%s' AS CHAR),%u,%u,"
             "%u,%u,%u,%u,%u,CAST(X'%s' AS CHAR),0)",
             payHex, accountHex, roleId, sellerHex, row.sellerRoleId, listingId,
             payType, yuan * 100u, row.priceKcoin, credit, fee, paramHex);
    if (!vm_mysql_exec(query))
    {
        snprintf(query, sizeof(query),
                 "UPDATE server_zangbao_listings SET status=%u,buyer_role_id=0,"
                 "buyer_account_id='',reserve_expire_unix=0,pay_id='' "
                 "WHERE listing_id=%u",
                 VM_MOCK_ZANGBAO_ST_ACTIVE, listingId);
        (void)vm_mysql_exec(query);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "订单创建失败");
    }
    snprintf(priceText, sizeof(priceText), "%u.00", yuan);
    snprintf(signInput, sizeof(signInput), "%s%s%u%s%s", payId, requestParam,
             payType, priceText, pcfg.secretKey);
    vm_md5_hex(signInput, strlen(signInput), sign);
    snprintf(form, sizeof(form),
             "payId=%s&type=%u&price=%s&sign=%s&param=%s&isHtml=0", payId, payType,
             priceText, sign, requestParam);
    if (vm_mock_payment_build_callback_url(pcfg.callbackBaseUrl,
                                           "/payment/cbhub/notify", notifyUrl,
                                           sizeof(notifyUrl)) &&
        vm_mock_payment_build_callback_url(pcfg.callbackBaseUrl,
                                           "/payment/cbhub/return", returnUrl,
                                           sizeof(returnUrl)))
    {
        vm_mock_admin_url_encode(notifyUrl, notifyEncoded, sizeof(notifyEncoded));
        vm_mock_admin_url_encode(returnUrl, returnEncoded, sizeof(returnEncoded));
        snprintf(form + strlen(form), sizeof(form) - strlen(form),
                 "&notifyUrl=%s&returnUrl=%s", notifyEncoded, returnEncoded);
    }
    memset(&provider, 0, sizeof(provider));
    memset(response, 0, sizeof(response));
    httpOk = vm_mock_payment_http_post_locked(pcfg.apiBaseUrl, "/createOrder", form,
                                              response, sizeof(response),
                                              &httpStatus);
    if (!httpOk || !vm_mock_payment_parse_create_result(response, &provider) ||
        strcmp(provider.payId, payId) != 0)
    {
        snprintf(query, sizeof(query),
                 "UPDATE kcoin_market_orders SET status=5 WHERE pay_id=CAST(X'%s' AS CHAR)",
                 payHex);
        (void)vm_mysql_exec(query);
        snprintf(query, sizeof(query),
                 "UPDATE server_zangbao_listings SET status=%u,buyer_role_id=0,"
                 "buyer_account_id='',reserve_expire_unix=0,pay_id='' "
                 "WHERE listing_id=%u",
                 VM_MOCK_ZANGBAO_ST_ACTIVE, listingId);
        (void)vm_mysql_exec(query);
        memset(pcfg.secretKey, 0, sizeof(pcfg.secretKey));
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "支付下单失败");
    }
    {
        char orderHex[127];
        char urlHex[2051];

        if (vm_mysql_hex_encode(provider.orderId, strlen(provider.orderId),
                                orderHex, sizeof(orderHex)) == 0 ||
            vm_mysql_hex_encode(provider.payUrl, strlen(provider.payUrl), urlHex,
                                sizeof(urlHex)) == 0)
        {
            memset(pcfg.secretKey, 0, sizeof(pcfg.secretKey));
            return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                            "支付链接保存失败");
        }
        snprintf(query, sizeof(query),
                 "UPDATE kcoin_market_orders SET provider_order_id=CAST(X'%s' AS CHAR),"
                 "pay_url=CAST(X'%s' AS CHAR),really_price_cents=%u,provider_state=%d,"
                 "timeout_minutes=%u,status=1 WHERE pay_id=CAST(X'%s' AS CHAR)",
                 orderHex, urlHex, provider.reallyPriceCents, provider.state,
                 provider.timeoutMinutes ? provider.timeoutMinutes : 5u, payHex);
        (void)vm_mysql_exec(query);
    }
    memset(pcfg.secretKey, 0, sizeof(pcfg.secretKey));
    if (!vm_mock_admin_json_escape(provider.payUrl, payUrlEsc, sizeof(payUrlEsc)) ||
        !vm_mock_admin_json_escape(payId, payIdEsc, sizeof(payIdEsc)))
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "响应编码失败");
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"payId\":\"%s\",\"payType\":%u,"
             "\"priceCents\":%u,\"reallyPriceCents\":%u,"
             "\"priceKcoin\":%u,\"sellerCreditKcoin\":%u,"
             "\"payUrl\":\"%s\",\"status\":1,\"timeoutMinutes\":%u}\n",
             payIdEsc, payType, yuan * 100u,
             provider.reallyPriceCents ? provider.reallyPriceCents : yuan * 100u,
             row.priceKcoin, credit, payUrlEsc,
             provider.timeoutMinutes ? provider.timeoutMinutes : 5u);
    return vm_mock_zangbao_json_ok(client, json);
}

static int vm_mock_zangbao_api_order_status(vm_mock_service_socket client,
                                            const char *query)
{
    char account[64];
    char payId[64];
    const char *err = NULL;
    u32 roleId = 0;
    vm_mock_zangbao_market_order order;
    char json[256];

    if (!vm_mock_zangbao_auth_query(query, account, sizeof(account), &roleId, &err))
        return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                        err ? err : "鉴权失败");
    memset(payId, 0, sizeof(payId));
    if (!vm_mock_admin_form_value(query, "pay_id", payId, sizeof(payId)) ||
        payId[0] == 0)
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "缺少订单号");
    if (!vm_mock_zangbao_load_market_order(payId, false, &order) ||
        strcmp(order.accountId, account) != 0)
        return vm_mock_zangbao_json_err(client, "404 Not Found", "订单不存在");
    if (order.status == 1 && !order.credited)
        vm_mock_zangbao_release_expired_reserves();
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"payId\":\"%s\",\"status\":%u,\"credited\":%s}\n",
             order.payId, order.status, order.credited ? "true" : "false");
    return vm_mock_zangbao_json_ok(client, json);
}

static int vm_mock_zangbao_api_balance(vm_mock_service_socket client,
                                       const char *query)
{
    char account[64];
    char roleSel[32];
    const char *err = NULL;
    u32 roleId = 0;
    vm_mock_kcoin_config cfg;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    char accountHex[160];
    char sql[320];
    u32 channel = 0;
    u32 hasPayee = 0;
    char json[320];

    if (!vm_mock_zangbao_ensure_schema() || !vm_mock_kcoin_load_config(&cfg))
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "服务不可用");
    if (!vm_mock_zangbao_auth_query(query, account, sizeof(account), &roleId, &err))
        return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                        err ? err : "鉴权失败");
    state = vm_mock_service_open_account_role_db_for_management(account, &err);
    if (state == NULL)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        err ? err : "角色加载失败");
    snprintf(roleSel, sizeof(roleSel), "%u", roleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
    if (role == NULL)
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "角色不存在");
    }
    if (vm_mysql_hex_encode(account, strlen(account), accountHex,
                            sizeof(accountHex)) != 0)
    {
        snprintf(sql, sizeof(sql),
                 "SELECT pay_channel FROM account_role_kcoin_payee "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u LIMIT 1",
                 accountHex, roleId);
        (void)vm_mysql_query(sql, vm_mock_zangbao_u32_row, &channel);
        if (channel != 0)
            hasPayee = 1;
    }
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"kcoin\":%u,\"kcoinPerYuan\":%u,\"enabled\":%s,"
             "\"hasPayee\":%s,\"payChannel\":%u}\n",
             role->kcoin, cfg.kcoinPerYuan, cfg.enabled ? "true" : "false",
             hasPayee ? "true" : "false", channel);
    vm_mock_service_close_account_role_db_for_management(state, true);
    return vm_mock_zangbao_json_ok(client, json);
}

static int vm_mock_zangbao_api_payee_upload(vm_mock_service_socket client,
                                            const char *body)
{
    char account[64];
    char channelText[16];
    char *b64 = NULL;
    const char *err = NULL;
    u32 roleId = 0;
    u32 channel = 0;
    u8 *raw = NULL;
    u32 rawLen = 0;
    char relPath[256];
    char accountHex[160];
    char pathEsc[512];
    char query[900];
    FILE *fp = NULL;
    const char *payload = NULL;
    size_t b64Cap = 400000;

    if (!vm_mock_zangbao_ensure_schema())
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "服务不可用");
    if (!vm_mock_zangbao_auth(body, account, sizeof(account), &roleId, &err))
        return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                        err ? err : "鉴权失败");
    memset(channelText, 0, sizeof(channelText));
    b64 = (char *)malloc(b64Cap);
    if (b64 == NULL)
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "内存不足");
    memset(b64, 0, b64Cap);
    if (!vm_mock_admin_form_value(body, "pay_channel", channelText,
                                  sizeof(channelText)) ||
        !vm_net_mock_parse_u32_strict(channelText, &channel) ||
        (channel != 1 && channel != 2) ||
        !vm_mock_admin_form_value(body, "image_b64", b64, b64Cap) ||
        b64[0] == 0)
    {
        free(b64);
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        "请上传微信或支付宝收款码");
    }
    payload = b64;
    if (strncmp(payload, "data:", 5) == 0)
    {
        const char *comma = strchr(payload, ',');
        if (comma)
            payload = comma + 1;
    }
    raw = (u8 *)malloc(300000);
    if (raw == NULL)
    {
        free(b64);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "内存不足");
    }
    if (!vm_mock_admin_base64_decode(payload, raw, 300000, &rawLen) || rawLen < 32)
    {
        free(raw);
        free(b64);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "图片无效");
    }
#ifdef _WIN32
    CreateDirectoryA("uploads", NULL);
    CreateDirectoryA("uploads\\kcoin_qr", NULL);
#else
    (void)mkdir("uploads", 0755);
    (void)mkdir("uploads/kcoin_qr", 0755);
#endif
    snprintf(relPath, sizeof(relPath), "uploads/kcoin_qr/r%u_%u.jpg", roleId,
             (u32)time(NULL));
    fp = vm_net_mock_fopen_game_path(relPath, "wb");
    if (fp == NULL)
    {
        free(raw);
        free(b64);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "无法保存图片");
    }
    if (fwrite(raw, 1, rawLen, fp) != rawLen)
    {
        fclose(fp);
        free(raw);
        free(b64);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "写图失败");
    }
    fclose(fp);
    free(raw);
    free(b64);
    b64 = NULL;
    if (vm_mysql_hex_encode(account, strlen(account), accountHex,
                            sizeof(accountHex)) == 0 ||
        !vm_mock_admin_json_escape(relPath, pathEsc, sizeof(pathEsc)))
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "编码失败");
    /* Store path as plain ASCII in SQL string (escaped). */
    {
        char pathSql[320];
        size_t i;
        size_t j = 0;
        memset(pathSql, 0, sizeof(pathSql));
        for (i = 0; relPath[i] && j + 2 < sizeof(pathSql); ++i)
        {
            char ch = relPath[i];
            if (ch == '\'' || ch == '\\')
                pathSql[j++] = '\\';
            pathSql[j++] = ch;
        }
        snprintf(query, sizeof(query),
                 "INSERT INTO account_role_kcoin_payee "
                 "(account_id,role_id,pay_channel,image_path) VALUES "
                 "(CAST(X'%s' AS CHAR),%u,%u,'%s') "
                 "ON DUPLICATE KEY UPDATE pay_channel=VALUES(pay_channel),"
                 "image_path=VALUES(image_path)",
                 accountHex, roleId, channel, pathSql);
    }
    if (!vm_mysql_exec(query))
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "保存收款码失败");
    return vm_mock_zangbao_json_ok(client, "{\"ok\":true}\n");
}

static int vm_mock_zangbao_api_withdraw_create(vm_mock_service_socket client,
                                               const char *body)
{
    char account[64];
    char amountText[32];
    char roleSel[32];
    const char *err = NULL;
    u32 roleId = 0;
    u32 amount = 0;
    u32 channel = 0;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    char accountHex[160];
    char nameHex[96];
    char sql[400];
    char query[900];

    if (!vm_mock_zangbao_ensure_schema())
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "服务不可用");
    if (!vm_mock_zangbao_auth(body, account, sizeof(account), &roleId, &err))
        return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                        err ? err : "鉴权失败");
    memset(amountText, 0, sizeof(amountText));
    if (!vm_mock_admin_form_value(body, "amount", amountText, sizeof(amountText)) ||
        !vm_net_mock_parse_u32_strict(amountText, &amount) ||
        amount < VM_MOCK_KCOIN_WITHDRAW_MIN || (amount % 10u) != 0)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        "提现金额须>=50且为10的倍数");
    if (vm_mysql_hex_encode(account, strlen(account), accountHex,
                            sizeof(accountHex)) == 0)
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "账号无效");
    snprintf(sql, sizeof(sql),
             "SELECT pay_channel FROM account_role_kcoin_payee "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u LIMIT 1",
             accountHex, roleId);
    (void)vm_mysql_query(sql, vm_mock_zangbao_u32_row, &channel);
    if (channel == 0)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        "请先上传收款码");
    state = vm_mock_service_open_account_role_db_for_management(account, &err);
    if (state == NULL)
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        err ? err : "角色加载失败");
    snprintf(roleSel, sizeof(roleSel), "%u", roleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
    if (role == NULL)
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "角色不存在");
    }
    if (!vm_net_mock_role_try_sub_kcoin(role, amount))
    {
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "酷币不足");
    }
    if (!vm_net_mock_role_db_save_role("kcoin-withdraw", role->roleId))
    {
        role->kcoin += amount;
        vm_mock_service_close_account_role_db_for_management(state, true);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "扣币保存失败");
    }
    if (vm_mysql_hex_encode(role->name, strlen(role->name), nameHex,
                            sizeof(nameHex)) == 0)
        nameHex[0] = 0;
    snprintf(query, sizeof(query),
             "INSERT INTO kcoin_withdraw_orders "
             "(account_id,role_id,role_name,amount,pay_channel,qr_image_path,status) "
             "SELECT CAST(X'%s' AS CHAR),%u,UNHEX('%s'),%u,pay_channel,image_path,0 "
             "FROM account_role_kcoin_payee "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u LIMIT 1",
             accountHex, roleId, nameHex, amount, accountHex, roleId);
    if (!vm_mysql_exec(query))
    {
        role->kcoin += amount;
        (void)vm_net_mock_role_db_save_role("kcoin-withdraw-rollback", role->roleId);
        vm_mock_service_close_account_role_db_for_management(state, false);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "提现申请失败");
    }
    vm_mock_zangbao_sync_cached_kcoin(account, roleId, role->kcoin);
    vm_mock_service_account_capture(state);
    vm_mock_service_close_account_role_db_for_management(state, false);
    return vm_mock_zangbao_json_ok(client, "{\"ok\":true}\n");
}

static bool vm_mock_zangbao_withdraw_list_row(void *ctx, unsigned int cols,
                                              const char *const *vals,
                                              const size_t *lens)
{
    vm_mock_zangbao_list_json_ctx *c = (vm_mock_zangbao_list_json_ctx *)ctx;
    u32 id = 0, amount = 0, channel = 0, status = 0, created = 0, reviewed = 0;
    int n;

    if (c == NULL || c->json == NULL || cols < 6)
        return true;
    (void)vm_mock_mysql_parse_u32(vals[0], lens[0], &id);
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &amount);
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &channel);
    (void)vm_mock_mysql_parse_u32(vals[3], lens[3], &status);
    (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &created);
    (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &reviewed);
    n = snprintf(c->json + c->len, c->cap > c->len ? c->cap - c->len : 0,
                 "%s{\"withdrawId\":%u,\"amount\":%u,\"payChannel\":%u,"
                 "\"status\":%u,\"createdUnix\":%u,\"reviewedUnix\":%u}",
                 c->first ? "" : ",", id, amount, channel, status, created,
                 reviewed);
    if (n < 0 || (size_t)n >= (c->cap > c->len ? c->cap - c->len : 0))
    {
        c->invalid = true;
        return false;
    }
    c->len += (size_t)n;
    c->first = false;
    return true;
}

static int vm_mock_zangbao_api_withdraw_list(vm_mock_service_socket client,
                                             const char *query)
{
    char account[64];
    char accountHex[160];
    const char *err = NULL;
    u32 roleId = 0;
    char *json = NULL;
    char sql[480];
    vm_mock_zangbao_list_json_ctx ctx;

    if (!vm_mock_zangbao_auth_query(query, account, sizeof(account), &roleId, &err))
        return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                        err ? err : "鉴权失败");
    if (vm_mysql_hex_encode(account, strlen(account), accountHex,
                            sizeof(accountHex)) == 0)
        return vm_mock_zangbao_json_err(client, "400 Bad Request", "账号无效");
    json = (char *)malloc(32768);
    if (json == NULL)
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "内存不足");
    memset(&ctx, 0, sizeof(ctx));
    ctx.json = json;
    ctx.cap = 32768;
    ctx.first = true;
    ctx.len = (size_t)snprintf(json, 32768, "{\"ok\":true,\"items\":[");
    snprintf(sql, sizeof(sql),
             "SELECT withdraw_id,amount,pay_channel,status,"
             "UNIX_TIMESTAMP(created_at),IFNULL(UNIX_TIMESTAMP(reviewed_at),0) "
             "FROM kcoin_withdraw_orders WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u ORDER BY withdraw_id DESC LIMIT 50",
             accountHex, roleId);
    (void)vm_mysql_query(sql, vm_mock_zangbao_withdraw_list_row, &ctx);
    if (ctx.invalid || ctx.len + 8 >= ctx.cap)
    {
        free(json);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "列表过长");
    }
    snprintf(json + ctx.len, ctx.cap - ctx.len, "]}\n");
    {
        int rc = vm_mock_zangbao_json_ok(client, json);
        free(json);
        return rc;
    }
}

/* ---- seal-detail (bound 857 listing → escrow role snapshot) -------------- */

typedef struct
{
    char *json;
    size_t cap;
    size_t len;
    bool first;
    bool invalid;
    bool found;
} vm_mock_zangbao_seal_json_ctx;

static bool vm_mock_zangbao_seal_json_append(vm_mock_zangbao_seal_json_ctx *c,
                                             const char *fmt, ...);
static void vm_mock_zangbao_seal_fill_guild_by_role(u32 roleId, char *nameOut,
                                                    size_t nameOutSize);
static bool vm_mock_zangbao_seal_copy_wash(const char *val, size_t len,
                                           char *washOut, size_t washCap);

static bool vm_mock_zangbao_seal_json_append(vm_mock_zangbao_seal_json_ctx *c,
                                             const char *fmt, ...)
{
    va_list ap;
    int n;

    if (c == NULL || c->json == NULL || c->invalid)
        return false;
    va_start(ap, fmt);
    n = vsnprintf(c->json + c->len, c->cap > c->len ? c->cap - c->len : 0, fmt,
                  ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= (c->cap > c->len ? c->cap - c->len : 0))
    {
        c->invalid = true;
        return false;
    }
    c->len += (size_t)n;
    return true;
}

static bool vm_mock_zangbao_seal_gbk_json_esc(const char *gbk, char *esc,
                                              size_t escCap)
{
    char utf8[128];

    memset(utf8, 0, sizeof(utf8));
    if (gbk != NULL && gbk[0] != 0)
        gbk_to_utf8((u8 *)gbk, (u8 *)utf8, sizeof(utf8));
    if (utf8[0] == 0 && gbk != NULL && gbk[0] != 0)
        snprintf(utf8, sizeof(utf8), "%s", gbk);
    if (utf8[0] == 0)
        snprintf(utf8, sizeof(utf8), "-");
    if (!vm_mock_admin_json_escape(utf8, esc, escCap))
        snprintf(esc, escCap, "-");
    return true;
}

/* Mirror Pixi 1/1/21 panel fields so seal buyers see the same attr rows. */
static bool vm_mock_zangbao_seal_append_role_panel(
    vm_mock_zangbao_seal_json_ctx *c, vm_net_mock_role_state *role)
{
    vm_net_mock_player_stats stats;
    u32 hp = 0, hpMax = 0, mp = 0, mpMax = 0;
    u32 vitality = 0, vitalityMax = 0;
    const char *title = "";
    const char *spouse = "";
    char guildName[VM_NET_MOCK_GUILD_NAME_SIZE];
    char nameEsc[192];
    char titleEsc[192];
    char spouseEsc[192];
    char guildEsc[192];
    char sceneEsc[256];

    if (c == NULL || role == NULL)
        return false;
    memset(&stats, 0, sizeof(stats));
    vm_net_mock_role_sync_derived_vitals(role);
    vm_net_mock_role_build_player_stats(role, &stats);
    vm_net_mock_role_default_vitals(role, &hp, &hpMax, &mp, &mpMax);
    vitality = 0;
    vitalityMax = 0;
    vm_mock_activities_vitality_wire(role, &vitality, &vitalityMax);
    title = vm_net_mock_role_title(role);
    if (title == NULL || title[0] == 0)
        title = "\xce\xde"; /* GBK: 无 */
    spouse = vm_net_mock_role_spouse_name(role);
    if (spouse == NULL || spouse[0] == 0)
        spouse = "\xce\xde"; /* GBK: 无 */
    memset(guildName, 0, sizeof(guildName));
    vm_mock_zangbao_seal_fill_guild_by_role(role->roleId, guildName,
                                             sizeof(guildName));
    if (guildName[0] == 0)
        (void)vm_net_mock_role_guild_info(role, guildName, sizeof(guildName));
    (void)vm_mock_zangbao_seal_gbk_json_esc(role->name, nameEsc, sizeof(nameEsc));
    (void)vm_mock_zangbao_seal_gbk_json_esc(title, titleEsc, sizeof(titleEsc));
    (void)vm_mock_zangbao_seal_gbk_json_esc(spouse, spouseEsc, sizeof(spouseEsc));
    (void)vm_mock_zangbao_seal_gbk_json_esc(
        guildName[0] ? guildName : "\xce\xde\xb0\xef\xc5\xc9", /* 无帮派 */
        guildEsc, sizeof(guildEsc));
    if (role->scene[0] != 0)
        (void)vm_mock_zangbao_seal_gbk_json_esc(role->scene, sceneEsc,
                                                 sizeof(sceneEsc));
    else
        snprintf(sceneEsc, sizeof(sceneEsc), "");
    if (!vm_mock_zangbao_seal_json_append(
            c,
            "\"role\":{\"name\":\"%s\",\"level\":%u,\"job\":%u,\"sex\":%u,"
            "\"title\":\"%s\",\"spouse\":\"%s\",\"guild\":\"%s\","
            "\"hp\":%u,\"hpMax\":%u,\"mp\":%u,\"mpMax\":%u,"
            "\"charm\":%u,\"strength\":%u,\"agility\":%u,\"wisdom\":%u,"
            "\"attack\":%u,\"armor\":%u,\"dodge\":%u,\"hit\":%u,"
            "\"crit\":%u,\"resist\":%u,\"reflectPct\":%u,\"comboPct\":%u,"
            "\"revivePct\":%u,\"vitality\":%u,\"vitalityMax\":%u,"
            "\"money\":%u,\"exp\":%u,\"wcoin\":%u,\"kcoin\":%u,"
            "\"scene\":\"%s\"}",
            nameEsc, stats.level, stats.job, (u32)role->sex, titleEsc,
            spouseEsc, guildEsc, hp, stats.maxHp ? stats.maxHp : hpMax, mp,
            stats.maxMp ? stats.maxMp : mpMax, stats.charm, stats.strength,
            stats.agility, stats.wisdom, stats.attack, stats.defense,
            stats.dodge, stats.hit, stats.crit, stats.resist, stats.reflectPct,
            stats.comboPct, stats.revivePct, vitality, vitalityMax, role->money,
            role->exp, role->wcoin, role->kcoin, sceneEsc))
        return false;
    c->found = true;
    return true;
}

typedef struct
{
    vm_net_mock_role_state *role;
    bool found;
    bool invalid;
} vm_mock_zangbao_seal_scratch_ctx;

static bool vm_mock_zangbao_seal_scratch_role_row(void *ctx, unsigned int cols,
                                                  const char *const *vals,
                                                  const size_t *lens)
{
    vm_mock_zangbao_seal_scratch_ctx *c =
        (vm_mock_zangbao_seal_scratch_ctx *)ctx;
    vm_net_mock_role_state *role;
    size_t nlen = 0;
    u32 tmp = 0;

    if (c == NULL || c->role == NULL || cols < 13)
    {
        if (c != NULL)
            c->invalid = true;
        return true;
    }
    role = c->role;
    if (vals[0])
        (void)vm_mysql_hex_decode(vals[0], lens[0], role->name,
                                  sizeof(role->name) - 1, &nlen);
    role->name[nlen] = 0;
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &role->level);
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &tmp);
    role->job = (u8)tmp;
    (void)vm_mock_mysql_parse_u32(vals[3], lens[3], &tmp);
    role->sex = (u8)tmp;
    (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &role->hp);
    (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &role->hpMax);
    (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &role->mp);
    (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &role->mpMax);
    (void)vm_mock_mysql_parse_u32(vals[8], lens[8], &role->money);
    (void)vm_mock_mysql_parse_u32(vals[9], lens[9], &role->exp);
    (void)vm_mock_mysql_parse_u32(vals[10], lens[10], &role->wcoin);
    (void)vm_mock_mysql_parse_u32(vals[11], lens[11], &role->kcoin);
    nlen = 0;
    if (vals[12])
        (void)vm_mysql_hex_decode(vals[12], lens[12], role->scene,
                                  sizeof(role->scene) - 1, &nlen);
    role->scene[nlen] = 0;
    if (cols >= 15)
    {
        (void)vm_mock_mysql_parse_u32(vals[13], lens[13], &role->spouseRoleId);
        nlen = 0;
        if (vals[14])
            (void)vm_mysql_hex_decode(vals[14], lens[14], role->spouseName,
                                      sizeof(role->spouseName) - 1, &nlen);
        role->spouseName[nlen] = 0;
    }
    if (cols >= 16)
    {
        (void)vm_mock_mysql_parse_u32(vals[15], lens[15], &tmp);
        role->designationId = (u8)tmp;
    }
    if (cols >= 18)
    {
        (void)vm_mock_mysql_parse_u32(vals[16], lens[16],
                                      &role->arenaDesignationMask);
        (void)vm_mock_mysql_parse_u32(vals[17], lens[17],
                                      &role->specialDesignationMask);
    }
    if (role->level == 0 && role->exp > 0)
        role->level = vm_net_mock_role_level_from_exp(role->exp);
    if (role->level == 0)
        role->level = 1;
    if (role->job == 0 || role->job > 3)
        role->job = 1;
    c->found = true;
    return true;
}

static bool vm_mock_zangbao_seal_scratch_equip_row(void *ctx, unsigned int cols,
                                                   const char *const *vals,
                                                   const size_t *lens)
{
    vm_mock_zangbao_seal_scratch_ctx *c =
        (vm_mock_zangbao_seal_scratch_ctx *)ctx;
    vm_net_mock_role_state *role;
    u32 slot = 0, itemId = 0, enhance = 0;
    u32 gemReflect = 0, gemCombo = 0, gemRevive = 0;
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];

    if (c == NULL || c->role == NULL || cols < 3)
        return true;
    role = c->role;
    (void)vm_mock_mysql_parse_u32(vals[0], lens[0], &slot);
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &itemId);
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &enhance);
    if (itemId == 0 || slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
        return true;
    if (enhance > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        enhance = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
    role->equippedItemIds[slot] = itemId;
    role->equippedEnhanceLevels[slot] = (u16)enhance;
    vm_net_mock_enhance_wash_clear(&role->equippedEnhanceWash[slot]);
    (void)vm_mock_zangbao_seal_copy_wash(cols >= 4 ? vals[3] : NULL,
                                          cols >= 4 ? lens[3] : 0, washText,
                                          sizeof(washText));
    (void)vm_net_mock_enhance_wash_decode(washText, strlen(washText),
                                          &role->equippedEnhanceWash[slot]);
    if (cols >= 7)
    {
        (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &gemReflect);
        (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &gemCombo);
        (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &gemRevive);
    }
    role->equippedGemReflect[slot] = (u8)gemReflect;
    role->equippedGemCombo[slot] = (u8)gemCombo;
    role->equippedGemRevive[slot] = (u8)gemRevive;
    return true;
}

/* Escrow account is not in login accounts — cannot open_account_role_db.
 * Build a scratch role from MySQL (role row + worn gear) for panel stats. */
static vm_net_mock_role_state *vm_mock_zangbao_seal_load_scratch(
    const char *escrowHex, u32 sealedRoleId)
{
    vm_net_mock_role_state *role = NULL;
    vm_mock_zangbao_seal_scratch_ctx ctx;
    char sql[640];

    if (escrowHex == NULL || escrowHex[0] == 0 || sealedRoleId == 0)
        return NULL;
    role = (vm_net_mock_role_state *)calloc(1, sizeof(*role));
    if (role == NULL)
        return NULL;
    role->roleId = sealedRoleId;
    memset(&ctx, 0, sizeof(ctx));
    ctx.role = role;
    snprintf(sql, sizeof(sql),
             "SELECT HEX(role_name),level,job,sex,hp,hp_max,mp,mp_max,money,exp,"
             "wcoin,kcoin,HEX(scene),spouse_role_id,HEX(IFNULL(spouse_name,'')),"
             "designation_id,arena_designation_mask,special_designation_mask "
             "FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u LIMIT 1",
             escrowHex, sealedRoleId);
    (void)vm_mysql_query(sql, vm_mock_zangbao_seal_scratch_role_row, &ctx);
    if (ctx.invalid || !ctx.found)
    {
        free(role);
        return NULL;
    }
    snprintf(sql, sizeof(sql),
             "SELECT slot_index,item_id,enhance_level,enhance_wash,"
             "gem_reflect,gem_combo,gem_revive "
             "FROM account_role_equipment WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u AND item_id<>0 ORDER BY slot_index",
             escrowHex, sealedRoleId);
    (void)vm_mysql_query(sql, vm_mock_zangbao_seal_scratch_equip_row, &ctx);
    if (ctx.invalid)
    {
        free(role);
        return NULL;
    }
    return role;
}

typedef struct
{
    char name[VM_NET_MOCK_GUILD_NAME_SIZE];
    bool found;
} vm_mock_zangbao_seal_guild_ctx;

static bool vm_mock_zangbao_seal_guild_row(void *ctx, unsigned int cols,
                                           const char *const *vals,
                                           const size_t *lens)
{
    vm_mock_zangbao_seal_guild_ctx *c = (vm_mock_zangbao_seal_guild_ctx *)ctx;
    size_t nlen = 0;

    if (c == NULL || cols < 1 || vals[0] == NULL)
        return true;
    memset(c->name, 0, sizeof(c->name));
    (void)vm_mysql_hex_decode(vals[0], lens[0], c->name, sizeof(c->name) - 1,
                              &nlen);
    c->name[nlen] = 0;
    c->found = c->name[0] != 0;
    return true;
}

static void vm_mock_zangbao_seal_fill_guild_by_role(u32 roleId, char *nameOut,
                                                    size_t nameOutSize)
{
    char sql[320];
    vm_mock_zangbao_seal_guild_ctx gctx;

    if (nameOut == NULL || nameOutSize == 0)
        return;
    nameOut[0] = 0;
    if (roleId == 0)
        return;
    memset(&gctx, 0, sizeof(gctx));
    snprintf(sql, sizeof(sql),
             "SELECT HEX(g.guild_name) FROM guild_members gm "
             "JOIN guilds g ON g.guild_id=gm.guild_id "
             "WHERE gm.role_id=%u LIMIT 1",
             roleId);
    (void)vm_mysql_query(sql, vm_mock_zangbao_seal_guild_row, &gctx);
    if (gctx.found)
        snprintf(nameOut, nameOutSize, "%s", gctx.name);
}

static bool vm_mock_zangbao_seal_role_row(void *ctx, unsigned int cols,
                                          const char *const *vals,
                                          const size_t *lens)
{
    vm_mock_zangbao_seal_json_ctx *c = (vm_mock_zangbao_seal_json_ctx *)ctx;
    char nameGbk[32];
    char sceneGbk[64];
    char nameUtf8[96];
    char sceneUtf8[128];
    char nameEsc[192];
    char sceneEsc[256];
    size_t nlen = 0;
    u32 level = 0, job = 0, sex = 0, hp = 0, hpMax = 0, mp = 0, mpMax = 0;
    u32 money = 0, exp = 0, wcoin = 0, kcoin = 0;

    if (c == NULL || cols < 13)
    {
        if (c != NULL)
            c->invalid = true;
        return true;
    }
    memset(nameGbk, 0, sizeof(nameGbk));
    memset(sceneGbk, 0, sizeof(sceneGbk));
    if (vals[0])
        (void)vm_mysql_hex_decode(vals[0], lens[0], nameGbk, sizeof(nameGbk) - 1,
                                  &nlen);
    nameGbk[nlen] = 0;
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &level);
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &job);
    (void)vm_mock_mysql_parse_u32(vals[3], lens[3], &sex);
    (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &hp);
    (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &hpMax);
    (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &mp);
    (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &mpMax);
    (void)vm_mock_mysql_parse_u32(vals[8], lens[8], &money);
    (void)vm_mock_mysql_parse_u32(vals[9], lens[9], &exp);
    (void)vm_mock_mysql_parse_u32(vals[10], lens[10], &wcoin);
    (void)vm_mock_mysql_parse_u32(vals[11], lens[11], &kcoin);
    nlen = 0;
    if (vals[12])
        (void)vm_mysql_hex_decode(vals[12], lens[12], sceneGbk,
                                  sizeof(sceneGbk) - 1, &nlen);
    sceneGbk[nlen] = 0;
    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    gbk_to_utf8((u8 *)nameGbk, (u8 *)nameUtf8, sizeof(nameUtf8));
    gbk_to_utf8((u8 *)sceneGbk, (u8 *)sceneUtf8, sizeof(sceneUtf8));
    if (nameUtf8[0] == 0)
        snprintf(nameUtf8, sizeof(nameUtf8), "%s", nameGbk);
    if (sceneUtf8[0] == 0)
        snprintf(sceneUtf8, sizeof(sceneUtf8), "%s", sceneGbk);
    if (!vm_mock_admin_json_escape(nameUtf8, nameEsc, sizeof(nameEsc)))
        snprintf(nameEsc, sizeof(nameEsc), "-");
    if (!vm_mock_admin_json_escape(sceneUtf8, sceneEsc, sizeof(sceneEsc)))
        snprintf(sceneEsc, sizeof(sceneEsc), "");
    if (!vm_mock_zangbao_seal_json_append(
            c,
            "\"role\":{\"name\":\"%s\",\"level\":%u,\"job\":%u,\"sex\":%u,"
            "\"title\":\"\",\"spouse\":\"无\",\"guild\":\"无帮派\","
            "\"hp\":%u,\"hpMax\":%u,\"mp\":%u,\"mpMax\":%u,"
            "\"charm\":0,\"strength\":0,\"agility\":0,\"wisdom\":0,"
            "\"attack\":0,\"armor\":0,\"dodge\":0,\"hit\":0,"
            "\"crit\":0,\"resist\":0,\"reflectPct\":0,\"comboPct\":0,"
            "\"revivePct\":0,\"vitality\":0,\"vitalityMax\":0,"
            "\"money\":%u,\"exp\":%u,\"wcoin\":%u,\"kcoin\":%u,"
            "\"scene\":\"%s\"}",
            nameEsc, level, job, sex, hp, hpMax, mp, mpMax, money, exp, wcoin,
            kcoin, sceneEsc))
        return false;
    c->found = true;
    return true;
}

static bool vm_mock_zangbao_seal_copy_wash(const char *val, size_t len,
                                           char *washOut, size_t washCap)
{
    size_t copy;

    if (washOut == NULL || washCap == 0)
        return false;
    snprintf(washOut, washCap, "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    if (val == NULL || len == 0)
        return true;
    copy = len < washCap - 1 ? len : washCap - 1;
    memcpy(washOut, val, copy);
    washOut[copy] = 0;
    return true;
}

static bool vm_mock_zangbao_seal_equip_row(void *ctx, unsigned int cols,
                                           const char *const *vals,
                                           const size_t *lens)
{
    vm_mock_zangbao_seal_json_ctx *c = (vm_mock_zangbao_seal_json_ctx *)ctx;
    u32 slot = 0, itemId = 0, enhance = 0, instanceId = 0;
    u32 gemReflect = 0, gemCombo = 0, gemRevive = 0;
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char washEsc[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX * 2 + 8];

    if (c == NULL || cols < 4)
        return true;
    (void)vm_mock_mysql_parse_u32(vals[0], lens[0], &slot);
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &itemId);
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &enhance);
    (void)vm_mock_mysql_parse_u32(vals[3], lens[3], &instanceId);
    if (itemId == 0)
        return true;
    (void)vm_mock_zangbao_seal_copy_wash(cols >= 5 ? vals[4] : NULL,
                                          cols >= 5 ? lens[4] : 0, washText,
                                          sizeof(washText));
    if (cols >= 8)
    {
        (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &gemReflect);
        (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &gemCombo);
        (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &gemRevive);
    }
    if (!vm_mock_admin_json_escape(washText, washEsc, sizeof(washEsc)))
        snprintf(washEsc, sizeof(washEsc),
                 "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    if (!vm_mock_zangbao_seal_json_append(
            c,
            "%s{\"slot\":%u,\"itemId\":%u,\"enhanceLevel\":%u,\"instanceId\":%u,"
            "\"enhanceWash\":\"%s\",\"gemReflect\":%u,\"gemCombo\":%u,"
            "\"gemRevive\":%u}",
            c->first ? "" : ",", slot, itemId, enhance, instanceId, washEsc,
            gemReflect, gemCombo, gemRevive))
        return false;
    c->first = false;
    return true;
}

static bool vm_mock_zangbao_seal_bag_row(void *ctx, unsigned int cols,
                                         const char *const *vals,
                                         const size_t *lens)
{
    vm_mock_zangbao_seal_json_ctx *c = (vm_mock_zangbao_seal_json_ctx *)ctx;
    u32 itemId = 0, count = 0, enhance = 0, seq = 0;
    u32 gemReflect = 0, gemCombo = 0, gemRevive = 0;
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char washEsc[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX * 2 + 8];

    if (c == NULL || cols < 4)
        return true;
    (void)vm_mock_mysql_parse_u32(vals[0], lens[0], &itemId);
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &count);
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &enhance);
    (void)vm_mock_mysql_parse_u32(vals[3], lens[3], &seq);
    if (itemId == 0 || count == 0)
        return true;
    (void)vm_mock_zangbao_seal_copy_wash(cols >= 5 ? vals[4] : NULL,
                                          cols >= 5 ? lens[4] : 0, washText,
                                          sizeof(washText));
    if (cols >= 8)
    {
        (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &gemReflect);
        (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &gemCombo);
        (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &gemRevive);
    }
    if (!vm_mock_admin_json_escape(washText, washEsc, sizeof(washEsc)))
        snprintf(washEsc, sizeof(washEsc),
                 "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    if (!vm_mock_zangbao_seal_json_append(
            c,
            "%s{\"itemId\":%u,\"count\":%u,\"enhanceLevel\":%u,\"seq\":%u,"
            "\"enhanceWash\":\"%s\",\"gemReflect\":%u,\"gemCombo\":%u,"
            "\"gemRevive\":%u}",
            c->first ? "" : ",", itemId, count, enhance, seq, washEsc,
            gemReflect, gemCombo, gemRevive))
        return false;
    c->first = false;
    return true;
}

static bool vm_mock_zangbao_seal_wh_row(void *ctx, unsigned int cols,
                                        const char *const *vals,
                                        const size_t *lens)
{
    vm_mock_zangbao_seal_json_ctx *c = (vm_mock_zangbao_seal_json_ctx *)ctx;
    u32 itemId = 0, count = 0, enhance = 0;
    u32 gemReflect = 0, gemCombo = 0, gemRevive = 0;
    char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
    char washEsc[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX * 2 + 8];

    if (c == NULL || cols < 3)
        return true;
    (void)vm_mock_mysql_parse_u32(vals[0], lens[0], &itemId);
    (void)vm_mock_mysql_parse_u32(vals[1], lens[1], &count);
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &enhance);
    if (itemId == 0 || count == 0)
        return true;
    (void)vm_mock_zangbao_seal_copy_wash(cols >= 4 ? vals[3] : NULL,
                                          cols >= 4 ? lens[3] : 0, washText,
                                          sizeof(washText));
    if (cols >= 7)
    {
        (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &gemReflect);
        (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &gemCombo);
        (void)vm_mock_mysql_parse_u32(vals[6], lens[6], &gemRevive);
    }
    if (!vm_mock_admin_json_escape(washText, washEsc, sizeof(washEsc)))
        snprintf(washEsc, sizeof(washEsc),
                 "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    if (!vm_mock_zangbao_seal_json_append(
            c,
            "%s{\"itemId\":%u,\"count\":%u,\"enhanceLevel\":%u,"
            "\"enhanceWash\":\"%s\",\"gemReflect\":%u,\"gemCombo\":%u,"
            "\"gemRevive\":%u}",
            c->first ? "" : ",", itemId, count, enhance, washEsc, gemReflect,
            gemCombo, gemRevive))
        return false;
    c->first = false;
    return true;
}

static int vm_mock_zangbao_api_seal_detail(vm_mock_service_socket client,
                                           const char *query)
{
    char listingText[24];
    char bagSeqText[24];
    char account[64];
    const char *err = NULL;
    u32 roleId = 0;
    u32 listingId = 0;
    u32 bagSeq = 0;
    u32 instanceId = 0;
    vm_mock_zangbao_listing listing;
    vm_net_mock_role_seal_row_ctx sealed;
    char escrowHex[160];
    char sql[512];
    char *json = NULL;
    vm_mock_zangbao_seal_json_ctx ctx;
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *bag = NULL;
    char roleSel[24];
    bool blankScroll = false;

    if (!vm_mock_zangbao_ensure_schema() || !vm_net_mock_role_seal_ensure_schema())
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "服务不可用");
    account[0] = 0;
    memset(listingText, 0, sizeof(listingText));
    memset(bagSeqText, 0, sizeof(bagSeqText));
    if (vm_mock_admin_form_value(query ? query : "", "listingId", listingText,
                                  sizeof(listingText)) &&
        vm_net_mock_parse_u32_strict(listingText, &listingId) && listingId != 0)
    {
        (void)vm_mock_zangbao_auth_query(query ? query : "", account,
                                          sizeof(account), &roleId, &err);
        if (!vm_mock_zangbao_load_listing(listingId, &listing) || !listing.found)
            return vm_mock_zangbao_json_err(client, "404 Not Found", "商品不存在");
        if (listing.itemId != VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID ||
            listing.itemInstanceId == 0)
            return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                            "非封印卷商品");
        instanceId = listing.itemInstanceId;
    }
    else if (vm_mock_admin_form_value(query ? query : "", "bagSeq", bagSeqText,
                                       sizeof(bagSeqText)) &&
             vm_net_mock_parse_u32_strict(bagSeqText, &bagSeq) && bagSeq != 0)
    {
        /* Bag detail: auth required; resolve bound instance from backpack. */
        if (!vm_mock_zangbao_auth_query(query ? query : "", account,
                                         sizeof(account), &roleId, &err))
            return vm_mock_zangbao_json_err(client, "401 Unauthorized",
                                            err ? err : "未登录");
        state = vm_mock_service_open_account_role_db_for_management(account, &err);
        if (state == NULL)
            return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                            err ? err : "角色加载失败");
        snprintf(roleSel, sizeof(roleSel), "%u", roleId);
        role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSel);
        if (role == NULL)
        {
            vm_mock_service_close_account_role_db_for_management(state, true);
            return vm_mock_zangbao_json_err(client, "400 Bad Request", "角色不存在");
        }
        bag = vm_net_mock_role_find_backpack_item(role, 0, (u16)bagSeq);
        if (bag == NULL || bag->itemId != VM_NET_MOCK_ROLE_SEAL_SCROLL_ITEM_ID ||
            bag->count == 0)
        {
            vm_mock_service_close_account_role_db_for_management(state, true);
            return vm_mock_zangbao_json_err(client, "404 Not Found", "封印卷不存在");
        }
        if (bag->enhanceInstanceId == 0)
        {
            blankScroll = true;
            instanceId = 0;
        }
        else
        {
            instanceId = bag->enhanceInstanceId;
        }
        vm_mock_service_close_account_role_db_for_management(state, true);
        state = NULL;
    }
    else
    {
        return vm_mock_zangbao_json_err(client, "400 Bad Request",
                                        "缺少 listingId 或 bagSeq");
    }

    if (blankScroll)
    {
        char blankJson[384];

        snprintf(blankJson, sizeof(blankJson),
                 "{\"ok\":true,\"listingId\":0,\"bagSeq\":%u,\"blank\":true,"
                 "\"sealedRoleId\":0,\"role\":{\"name\":\"\",\"level\":0,\"job\":0,"
                 "\"sex\":0,\"hp\":0,\"hpMax\":0,\"mp\":0,\"mpMax\":0,\"money\":0,"
                 "\"exp\":0,\"wcoin\":0,\"kcoin\":0,\"scene\":\"\"},"
                 "\"equipment\":[],\"backpack\":[],\"warehouse\":[]}\n",
                 bagSeq);
        return vm_mock_zangbao_json_ok(client, blankJson);
    }

    memset(&sealed, 0, sizeof(sealed));
    if (!vm_net_mock_role_seal_load_by_instance(instanceId, &sealed))
        return vm_mock_zangbao_json_err(client, "404 Not Found", "封印角色不存在");
    if (vm_mysql_hex_encode(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT,
                            strlen(VM_NET_MOCK_ROLE_SEAL_ESCROW_ACCOUNT),
                            escrowHex, sizeof(escrowHex)) == 0)
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "编码失败");

    json = (char *)malloc(196608);
    if (json == NULL)
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "内存不足");
    memset(&ctx, 0, sizeof(ctx));
    ctx.json = json;
    ctx.cap = 196608;
    if (!vm_mock_zangbao_seal_json_append(
            &ctx,
            "{\"ok\":true,\"listingId\":%u,\"bagSeq\":%u,\"blank\":false,"
            "\"sealedRoleId\":%u,",
            listingId, bagSeq, sealed.sealedRoleId))
    {
        free(json);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "输出失败");
    }

    /* Escrow is not a login account — load scratch role + worn gear from MySQL. */
    {
        vm_net_mock_role_state *scratch =
            vm_mock_zangbao_seal_load_scratch(escrowHex, sealed.sealedRoleId);
        if (scratch != NULL)
        {
            if (!vm_mock_zangbao_seal_append_role_panel(&ctx, scratch))
            {
                free(scratch);
                free(json);
                return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                                "输出失败");
            }
            free(scratch);
        }
    }
    if (!ctx.found)
    {
        snprintf(sql, sizeof(sql),
                 "SELECT HEX(role_name),level,job,sex,hp,hp_max,mp,mp_max,money,exp,"
                 "wcoin,kcoin,HEX(scene) FROM account_roles "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u LIMIT 1",
                 escrowHex, sealed.sealedRoleId);
        (void)vm_mysql_query(sql, vm_mock_zangbao_seal_role_row, &ctx);
        if (ctx.invalid)
        {
            free(json);
            return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                            "角色查询失败");
        }
    }
    if (!ctx.found)
    {
        /* Escrow row missing: still show preview so bag/listing detail is not blank. */
        char nameUtf8[96];
        char nameEsc[192];

        memset(nameUtf8, 0, sizeof(nameUtf8));
        gbk_to_utf8((u8 *)sealed.previewName, (u8 *)nameUtf8, sizeof(nameUtf8));
        if (nameUtf8[0] == 0)
            snprintf(nameUtf8, sizeof(nameUtf8), "%s", sealed.previewName);
        if (!vm_mock_admin_json_escape(nameUtf8, nameEsc, sizeof(nameEsc)))
            snprintf(nameEsc, sizeof(nameEsc), "-");
        if (!vm_mock_zangbao_seal_json_append(
                &ctx,
                "\"role\":{\"name\":\"%s\",\"level\":%u,\"job\":%u,\"sex\":%u,"
                "\"title\":\"\",\"spouse\":\"无\",\"guild\":\"无帮派\","
                "\"hp\":0,\"hpMax\":0,\"mp\":0,\"mpMax\":0,"
                "\"charm\":0,\"strength\":0,\"agility\":0,\"wisdom\":0,"
                "\"attack\":0,\"armor\":0,\"dodge\":0,\"hit\":0,"
                "\"crit\":0,\"resist\":0,\"reflectPct\":0,\"comboPct\":0,"
                "\"revivePct\":0,\"vitality\":0,\"vitalityMax\":0,"
                "\"money\":0,\"exp\":0,\"wcoin\":0,\"kcoin\":0,\"scene\":\"\"}",
                nameEsc, sealed.previewLevel, (u32)sealed.previewJob,
                (u32)sealed.previewSex))
        {
            free(json);
            return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                            "输出失败");
        }
        ctx.found = true;
    }

    if (!vm_mock_zangbao_seal_json_append(&ctx, ",\"equipment\":["))
    {
        free(json);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "输出失败");
    }
    ctx.first = true;
    snprintf(sql, sizeof(sql),
             "SELECT slot_index,item_id,enhance_level,item_instance_id,"
             "enhance_wash,gem_reflect,gem_combo,gem_revive "
             "FROM account_role_equipment WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u AND item_id<>0 ORDER BY slot_index",
             escrowHex, sealed.sealedRoleId);
    (void)vm_mysql_query(sql, vm_mock_zangbao_seal_equip_row, &ctx);
    if (ctx.invalid || !vm_mock_zangbao_seal_json_append(&ctx, "],\"backpack\":["))
    {
        free(json);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "输出失败");
    }
    ctx.first = true;
    snprintf(sql, sizeof(sql),
             "SELECT item_id,item_count,enhance_level,item_seq,enhance_wash,"
             "gem_reflect,gem_combo,gem_revive "
             "FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u AND item_id<>0 ORDER BY slot_index",
             escrowHex, sealed.sealedRoleId);
    (void)vm_mysql_query(sql, vm_mock_zangbao_seal_bag_row, &ctx);
    if (ctx.invalid || !vm_mock_zangbao_seal_json_append(&ctx, "],\"warehouse\":["))
    {
        free(json);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "输出失败");
    }
    ctx.first = true;
    snprintf(sql, sizeof(sql),
             "SELECT item_id,item_count,enhance_level,enhance_wash,"
             "gem_reflect,gem_combo,gem_revive "
             "FROM account_role_warehouse WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u AND item_id<>0 ORDER BY slot_index",
             escrowHex, sealed.sealedRoleId);
    (void)vm_mysql_query(sql, vm_mock_zangbao_seal_wh_row, &ctx);
    if (ctx.invalid || !vm_mock_zangbao_seal_json_append(&ctx, "]}\n"))
    {
        free(json);
        return vm_mock_zangbao_json_err(client, "500 Internal Server Error",
                                        "输出失败");
    }
    {
        int rc = vm_mock_zangbao_json_ok(client, json);
        free(json);
        return rc;
    }
}

static int vm_mock_zangbao_handle_http(vm_mock_service_socket client,
                                       const char *method, const char *target,
                                       const char *query, const char *body)
{
    const char *cors =
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n";

    if (method == NULL || target == NULL)
        return 0;
    if (strcmp(method, "OPTIONS") == 0 &&
        (strncmp(target, "/api/zangbao/", 13) == 0 ||
         strncmp(target, "/api/kcoin/", 11) == 0))
    {
        vm_mock_admin_send_response(client, "204 No Content",
                                    "text/plain; charset=utf-8", cors, "");
        return 1;
    }
    if (strcmp(target, "/api/zangbao/browse") == 0)
        return vm_mock_zangbao_api_browse(client, query ? query : "", false);
    if (strcmp(target, "/api/zangbao/mine") == 0)
        return vm_mock_zangbao_api_browse(client, query ? query : "", true);
    if (strcmp(target, "/api/zangbao/seal-detail") == 0)
        return vm_mock_zangbao_api_seal_detail(client, query ? query : "");
    if (strcmp(target, "/api/zangbao/list") == 0 && strcmp(method, "POST") == 0)
        return vm_mock_zangbao_api_list_create(client, body ? body : "");
    if (strcmp(target, "/api/zangbao/cancel") == 0 && strcmp(method, "POST") == 0)
        return vm_mock_zangbao_api_cancel(client, body ? body : "");
    if (strcmp(target, "/api/zangbao/buy") == 0 && strcmp(method, "POST") == 0)
        return vm_mock_zangbao_api_buy(client, body ? body : "");
    if (strcmp(target, "/api/zangbao/order") == 0)
        return vm_mock_zangbao_api_order_status(client, query ? query : "");
    if (strcmp(target, "/api/kcoin/balance") == 0)
        return vm_mock_zangbao_api_balance(client, query ? query : "");
    if (strcmp(target, "/api/kcoin/payee/upload") == 0 &&
        strcmp(method, "POST") == 0)
        return vm_mock_zangbao_api_payee_upload(client, body ? body : "");
    if (strcmp(target, "/api/kcoin/withdraw/create") == 0 &&
        strcmp(method, "POST") == 0)
        return vm_mock_zangbao_api_withdraw_create(client, body ? body : "");
    if (strcmp(target, "/api/kcoin/withdraw/list") == 0)
        return vm_mock_zangbao_api_withdraw_list(client, query ? query : "");
    return 0;
}

