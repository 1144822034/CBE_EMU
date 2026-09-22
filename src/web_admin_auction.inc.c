/* Admin UI for 拍卖行 — included from web_admin_server.c. */

static void vm_mock_admin_redirect_auction(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=auction&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_render_auction_page(char *response, size_t responseCap,
                                              const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_mock_auction_config *cfg;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    cfg = vm_net_mock_auction_config_get();

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 拍卖行</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px}"
        ".tabs{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 16px}.tab{padding:8px 12px;margin:4px;background:#fff;border:1px solid #ddd;"
        "text-decoration:none;color:#333;border-radius:6px}"
        ".tab.on{background:#175cd3;color:#fff}"
        "input,select{padding:6px;width:100%%;box-sizing:border-box}"
        "label{font-size:12px;color:#666;display:block;margin-bottom:4px}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}"
        "button{background:#175cd3;color:#fff;border:0;padding:10px 14px;border-radius:6px;margin-top:8px}"
        ".hint{color:#667085;font-size:13px;line-height:1.5}"
        "</style></head><body><h1>拍卖行</h1>"
        "<p class=\"hint\">关闭后玩家不可上架、出价或一口价；已上架拍品仍可浏览，"
        "卖家可撤拍（无出价时），买卖双方可继续领取待领物/退款。到期结算仍照常进行。</p>");
    vm_mock_admin_append_tabs(&page, "auction");

    if (status[0] && message[0])
        vm_mock_admin_text_appendf(&page, "<p>%s: %s</p>", status, message);

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>基础配置</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-auction-config\">"
        "<div class=\"grid\">"
        "<div><label>启用</label><select name=\"enabled\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
        "<div><label>按类别拦截</label><select name=\"category_gate\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
        "</div>"
        "<p class=\"hint\">按类别拦截开启时，与玩家交易相同：玄晶、任务物品、绑定、"
        "已强化、宝石和碎片不可上架。关闭后上述物品可拍卖。"
        "已镶嵌装备无论开关都需先拆除宝石。玩家交易始终受交易限制。</p>"
        "<button type=\"submit\">保存配置</button></form></section>"
        "</body></html>",
        cfg && cfg->enabled ? " selected" : "",
        cfg && !cfg->enabled ? " selected" : "",
        cfg && cfg->categoryGate ? " selected" : "",
        cfg && !cfg->categoryGate ? " selected" : "");
}

static void vm_mock_admin_handle_auction_action(vm_mock_service_socket client,
                                                const char *action,
                                                const char *body)
{
    if (strcmp(action, "save-auction-config") == 0)
    {
        u32 enabled = 1;
        u32 categoryGate = 0;
        char errMsg[384];

        if (!vm_mock_admin_form_u32(body, "enabled", 1u, &enabled) ||
            !vm_mock_admin_form_u32(body, "category_gate", 1u, &categoryGate))
        {
            vm_mock_admin_redirect_auction(client, "error", "表单数值无效");
            return;
        }
        if (vm_mock_auction_admin_save_config((u8)enabled, (u8)categoryGate))
        {
            vm_mock_admin_redirect_auction(client, "ok", "拍卖行配置已保存");
            return;
        }
        memset(errMsg, 0, sizeof(errMsg));
        {
            const char *mysqlErr = vm_mysql_last_error();
            if (mysqlErr != NULL && mysqlErr[0] != 0 &&
                strcmp(mysqlErr, "no MySQL error detail") != 0)
                snprintf(errMsg, sizeof(errMsg), "保存失败：%s", mysqlErr);
            else
                snprintf(errMsg, sizeof(errMsg), "%s", "保存失败");
        }
        vm_mock_admin_redirect_auction(client, "error", errMsg);
        return;
    }
}
