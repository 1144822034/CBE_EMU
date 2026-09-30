/* Admin UI for 藏宝阁 / 酷币提现 — included from web_admin_server.c. */

static bool vm_mock_zangbao_admin_path_row(void *ctx, unsigned int cols,
                                           const char *const *vals,
                                           const size_t *lens)
{
    char *out = (char *)ctx;

    if (out == NULL || cols < 1 || vals == NULL || vals[0] == NULL)
        return true;
    {
        size_t copy = lens[0] < 255 ? lens[0] : 255;
        memcpy(out, vals[0], copy);
        out[copy] = 0;
    }
    return true;
}

static void vm_mock_admin_redirect_zangbao(vm_mock_service_socket client,
                                            const char *status,
                                            const char *message)
{
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1100];

    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH "?tab=zangbao&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

typedef struct
{
    vm_mock_admin_text *page;
    u32 count;
} vm_mock_zangbao_admin_wd_ctx;

static bool vm_mock_zangbao_admin_wd_row(void *ctx, unsigned int cols,
                                         const char *const *vals,
                                         const size_t *lens)
{
    vm_mock_zangbao_admin_wd_ctx *c = (vm_mock_zangbao_admin_wd_ctx *)ctx;
    u32 id = 0;
    u32 roleId = 0;
    u32 amount = 0;
    u32 channel = 0;
    u32 status = 0;
    char account[64];
    char name[64];
    char nameUtf8[96];
    size_t nlen = 0;
    const char *statusText = "-";
    const char *channelText = "-";

    if (c == NULL || c->page == NULL || cols < 8)
        return true;
    (void)vm_mock_mysql_parse_u32(vals[0], lens[0], &id);
    memset(account, 0, sizeof(account));
    if (vals[1] && lens[1])
    {
        size_t copy = lens[1] < sizeof(account) - 1 ? lens[1] : sizeof(account) - 1;
        memcpy(account, vals[1], copy);
    }
    (void)vm_mock_mysql_parse_u32(vals[2], lens[2], &roleId);
    memset(name, 0, sizeof(name));
    if (vals[3])
        (void)vm_mysql_hex_decode(vals[3], lens[3], name, sizeof(name) - 1, &nlen);
    name[nlen] = 0;
    memset(nameUtf8, 0, sizeof(nameUtf8));
    gbk_to_utf8((u8 *)name, (u8 *)nameUtf8, sizeof(nameUtf8));
    if (nameUtf8[0] == 0)
        snprintf(nameUtf8, sizeof(nameUtf8), "%s", name);
    (void)vm_mock_mysql_parse_u32(vals[4], lens[4], &amount);
    (void)vm_mock_mysql_parse_u32(vals[5], lens[5], &channel);
    (void)vals[6];
    (void)vm_mock_mysql_parse_u32(vals[7], lens[7], &status);
    if (status == 0)
        statusText = "待审";
    else if (status == 1)
        statusText = "已同意";
    else if (status == 2)
        statusText = "已拒绝";
    if (channel == 1)
        channelText = "微信";
    else if (channel == 2)
        channelText = "支付宝";
    vm_mock_admin_text_appendf(
        c->page,
        "<tr><td>%u</td><td>%s</td><td>%u</td><td>%s</td><td>%u</td><td>%s</td>"
        "<td>%s</td><td><a href=\"/kcoin-qr?id=%u\" target=\"_blank\">查看</a></td><td>",
        id, account, roleId, nameUtf8[0] ? nameUtf8 : "-", amount, channelText,
        statusText, id);
    if (status == 0)
    {
        vm_mock_admin_text_appendf(
            c->page,
            "<form method=\"post\" action=\"/action\" style=\"display:inline\">"
            "<input type=\"hidden\" name=\"action\" value=\"zangbao-withdraw-approve\">"
            "<input type=\"hidden\" name=\"withdraw_id\" value=\"%u\">"
            "<button type=\"submit\">同意</button></form> "
            "<form method=\"post\" action=\"/action\" style=\"display:inline\">"
            "<input type=\"hidden\" name=\"action\" value=\"zangbao-withdraw-reject\">"
            "<input type=\"hidden\" name=\"withdraw_id\" value=\"%u\">"
            "<input name=\"note\" placeholder=\"拒绝原因\" style=\"width:120px\">"
            "<button type=\"submit\">拒绝</button></form>",
            id, id);
    }
    else
        vm_mock_admin_text_appendf(c->page, "-");
    vm_mock_admin_text_appendf(c->page, "</td></tr>");
    c->count++;
    return true;
}

static void vm_mock_admin_render_zangbao_page(char *response, size_t responseCap,
                                              const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    vm_mock_kcoin_config cfg;
    vm_mock_zangbao_admin_wd_ctx wd;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    (void)vm_mock_zangbao_ensure_schema();
    (void)vm_mock_kcoin_load_config(&cfg);

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 藏宝阁</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px}"
        "table{border-collapse:collapse;width:100%%}th,td{border-bottom:1px solid #eee;"
        "padding:8px;text-align:left;vertical-align:top}"
        "input,select{padding:6px}button{background:#175cd3;color:#fff;border:0;"
        "padding:8px 12px;border-radius:6px;cursor:pointer}"
        ".hint{color:#667085;font-size:13px;line-height:1.5}"
        "</style></head><body><h1>藏宝阁 / 酷币</h1>"
        "<p class=\"hint\">买家赞助付款后卖家到账 95%% 酷币；提现满 50 且为 10 的倍数，"
        "申请时扣币，同意结单，拒绝退币。</p>");
    vm_mock_admin_append_tabs(&page, "zangbao");
    if (status[0] && message[0])
        vm_mock_admin_text_appendf(&page, "<p>%s: %s</p>", status, message);

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>基础配置</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-zangbao-config\">"
        "<label>启用 <select name=\"enabled\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option>"
        "</select></label> "
        "<label>酷币/元 <input name=\"kcoin_per_yuan\" type=\"number\" min=\"1\" "
        "value=\"%u\" style=\"width:100px\"></label> "
        "<label>每人同时上架上限 <input name=\"max_active_listings\" type=\"number\" "
        "min=\"1\" max=\"999\" value=\"%u\" style=\"width:80px\"></label> "
        "<span class=\"hint\">含在售与付款锁定中的寄售</span> "
        "<button type=\"submit\">保存</button></form></section>",
        cfg.enabled ? " selected" : "", cfg.enabled ? "" : " selected",
        cfg.kcoinPerYuan ? cfg.kcoinPerYuan : 1u,
        cfg.maxActiveListings ? cfg.maxActiveListings
                              : (u32)VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE);

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>提现申请</h2>"
        "<table><thead><tr><th>单号</th><th>账号</th><th>角色</th><th>名</th>"
        "<th>金额</th><th>渠道</th><th>状态</th><th>收款码</th><th>操作</th>"
        "</tr></thead><tbody>");
    memset(&wd, 0, sizeof(wd));
    wd.page = &page;
    (void)vm_mysql_query(
        "SELECT withdraw_id,account_id,role_id,HEX(role_name),amount,pay_channel,"
        "qr_image_path,status FROM kcoin_withdraw_orders "
        "ORDER BY status ASC, withdraw_id DESC LIMIT 100",
        vm_mock_zangbao_admin_wd_row, &wd);
    if (wd.count == 0)
        vm_mock_admin_text_appendf(&page, "<tr><td colspan=\"9\">暂无申请</td></tr>");
    vm_mock_admin_text_appendf(&page, "</tbody></table></section></body></html>");
}

static void vm_mock_admin_handle_zangbao_action(vm_mock_service_socket client,
                                                const char *action,
                                                const char *body)
{
    if (strcmp(action, "save-zangbao-config") == 0)
    {
        char enText[16];
        char rateText[32];
        char maxText[32];
        u32 enabled = 1;
        u32 rate = 1;
        u32 maxActive = VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE;
        char query[320];

        memset(enText, 0, sizeof(enText));
        memset(rateText, 0, sizeof(rateText));
        memset(maxText, 0, sizeof(maxText));
        (void)vm_mock_zangbao_ensure_schema();
        (void)vm_mock_admin_form_value(body, "enabled", enText, sizeof(enText));
        (void)vm_mock_admin_form_value(body, "kcoin_per_yuan", rateText,
                                        sizeof(rateText));
        (void)vm_mock_admin_form_value(body, "max_active_listings", maxText,
                                        sizeof(maxText));
        (void)vm_net_mock_parse_u32_strict(enText, &enabled);
        if (!vm_net_mock_parse_u32_strict(rateText, &rate) || rate == 0)
            rate = 1;
        if (!vm_net_mock_parse_u32_strict(maxText, &maxActive) || maxActive == 0)
            maxActive = VM_MOCK_ZANGBAO_DEFAULT_MAX_ACTIVE;
        if (maxActive > 999u)
            maxActive = 999u;
        snprintf(query, sizeof(query),
                 "INSERT INTO server_kcoin_config("
                 "config_id,kcoin_per_yuan,enabled,max_active_listings) "
                 "VALUES(1,%u,%u,%u) ON DUPLICATE KEY UPDATE "
                 "kcoin_per_yuan=VALUES(kcoin_per_yuan),"
                 "enabled=VALUES(enabled),"
                 "max_active_listings=VALUES(max_active_listings)",
                 rate, enabled ? 1u : 0u, maxActive);
        if (!vm_mysql_exec(query))
            vm_mock_admin_redirect_zangbao(client, "error", "保存失败");
        else
            vm_mock_admin_redirect_zangbao(client, "ok", "配置已保存");
        return;
    }
    if (strcmp(action, "zangbao-withdraw-approve") == 0)
    {
        char idText[32];
        u32 wid = 0;
        char query[320];

        memset(idText, 0, sizeof(idText));
        (void)vm_mock_admin_form_value(body, "withdraw_id", idText, sizeof(idText));
        if (!vm_net_mock_parse_u32_strict(idText, &wid) || wid == 0)
        {
            vm_mock_admin_redirect_zangbao(client, "error", "单号无效");
            return;
        }
        snprintf(query, sizeof(query),
                 "UPDATE kcoin_withdraw_orders SET status=1,"
                 "reviewed_at=CURRENT_TIMESTAMP WHERE withdraw_id=%u AND status=0",
                 wid);
        if (!vm_mysql_exec(query))
            vm_mock_admin_redirect_zangbao(client, "error", "同意失败");
        else
            vm_mock_admin_redirect_zangbao(client, "ok", "已同意（请按收款码打款）");
        return;
    }
    if (strcmp(action, "zangbao-withdraw-reject") == 0)
    {
        char idText[32];
        char note[200];
        char noteEsc[400];
        u32 wid = 0;
        char query[640];
        size_t i;
        size_t j = 0;

        memset(idText, 0, sizeof(idText));
        memset(note, 0, sizeof(note));
        memset(noteEsc, 0, sizeof(noteEsc));
        (void)vm_mock_admin_form_value(body, "withdraw_id", idText, sizeof(idText));
        (void)vm_mock_admin_form_value(body, "note", note, sizeof(note));
        if (!vm_net_mock_parse_u32_strict(idText, &wid) || wid == 0)
        {
            vm_mock_admin_redirect_zangbao(client, "error", "单号无效");
            return;
        }
        for (i = 0; note[i] && j + 2 < sizeof(noteEsc); ++i)
        {
            if (note[i] == '\'' || note[i] == '\\')
                noteEsc[j++] = '\\';
            noteEsc[j++] = note[i];
        }
        if (!vm_mysql_exec("START TRANSACTION"))
        {
            vm_mock_admin_redirect_zangbao(client, "error", "事务失败");
            return;
        }
        snprintf(query, sizeof(query),
                 "UPDATE account_roles ar "
                 "INNER JOIN kcoin_withdraw_orders w "
                 "ON w.account_id=ar.account_id AND w.role_id=ar.role_id "
                 "SET ar.kcoin=ar.kcoin+w.amount "
                 "WHERE w.withdraw_id=%u AND w.status=0",
                 wid);
        if (!vm_mysql_exec(query))
        {
            (void)vm_mysql_exec("ROLLBACK");
            vm_mock_admin_redirect_zangbao(client, "error", "退币失败");
            return;
        }
        snprintf(query, sizeof(query),
                 "UPDATE kcoin_withdraw_orders SET status=2,admin_note='%s',"
                 "reviewed_at=CURRENT_TIMESTAMP WHERE withdraw_id=%u AND status=0",
                 noteEsc, wid);
        if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        {
            (void)vm_mysql_exec("ROLLBACK");
            vm_mock_admin_redirect_zangbao(client, "error", "拒绝失败");
            return;
        }
        vm_mock_admin_redirect_zangbao(client, "ok", "已拒绝并退回酷币");
        return;
    }
    vm_mock_admin_redirect_zangbao(client, "error", "未知操作");
}

static int vm_mock_admin_serve_kcoin_qr(vm_mock_service_socket client,
                                        const char *query)
{
    char idText[32];
    u32 wid = 0;
    char sql[320];
    char path[256];
    FILE *fp = NULL;
    long sz = 0;
    char *buf = NULL;
    size_t nread = 0;
    char header[256];

    memset(idText, 0, sizeof(idText));
    memset(path, 0, sizeof(path));
    if (!vm_mock_admin_form_value(query, "id", idText, sizeof(idText)) ||
        !vm_net_mock_parse_u32_strict(idText, &wid) || wid == 0)
        return 0;
    snprintf(sql, sizeof(sql),
             "SELECT qr_image_path FROM kcoin_withdraw_orders WHERE withdraw_id=%u "
             "LIMIT 1",
             wid);
    (void)vm_mysql_query(sql, vm_mock_zangbao_admin_path_row, path);
    if (path[0] == 0)
        return 0;
    fp = vm_net_mock_fopen_game_path(path, "rb");
    if (fp == NULL)
        return 0;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return 0;
    }
    sz = ftell(fp);
    if (sz <= 0 || sz > 2 * 1024 * 1024)
    {
        fclose(fp);
        return 0;
    }
    rewind(fp);
    buf = (char *)malloc((size_t)sz);
    if (buf == NULL)
    {
        fclose(fp);
        return 0;
    }
    nread = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (nread != (size_t)sz)
    {
        free(buf);
        return 0;
    }
    {
        const char *contentType = "application/octet-stream";
        const u8 *bytes = (const u8 *)buf;

        if (nread >= 3 && bytes[0] == 0xFFu && bytes[1] == 0xD8u &&
            bytes[2] == 0xFFu)
            contentType = "image/jpeg";
        else if (nread >= 8 && bytes[0] == 0x89u && bytes[1] == 0x50u &&
                 bytes[2] == 0x4Eu && bytes[3] == 0x47u)
            contentType = "image/png";
        else if (nread >= 12 && bytes[0] == 0x52u && bytes[1] == 0x49u &&
                 bytes[2] == 0x46u && bytes[8] == 0x57u && bytes[9] == 0x45u)
            contentType = "image/webp";
        else if (nread >= 6 &&
                 ((bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F') ||
                  (path[0] && strstr(path, ".gif") != NULL)))
            contentType = "image/gif";
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
                 "Content-Length: %u\r\nCache-Control: private, max-age=60\r\n"
                 "Connection: close\r\n\r\n",
                 contentType, (u32)nread);
    }
    (void)vm_mock_service_send_all(client, (const u8 *)header, (u32)strlen(header));
    (void)vm_mock_service_send_all(client, (const u8 *)buf, (u32)nread);
    free(buf);
    return 1;
}
