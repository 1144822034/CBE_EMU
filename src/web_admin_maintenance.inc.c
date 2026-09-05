/* Admin UI for server maintenance — included from web_admin_server.c. */

static void vm_mock_admin_redirect_maintenance(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=maintenance&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_render_maintenance_page(char *response,
                                                  size_t responseCap,
                                                  const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    u8 active = 0;
    u8 kickDone = 0;
    u32 lastNoticeUnix = 0;
    u32 kickAtUnix = 0;
    u32 onlineCount = 0;
    u32 secondsLeft = 0;

    char countdownText[64];

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(countdownText, 0, sizeof(countdownText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    vm_mock_maintenance_admin_get(&active, &lastNoticeUnix, &kickDone, &kickAtUnix,
                                  &onlineCount, &secondsLeft);
    if (active && kickDone)
        snprintf(countdownText, sizeof(countdownText), "已完成踢人");
    else if (active && secondsLeft > 0)
        snprintf(countdownText, sizeof(countdownText), "%u 秒", secondsLeft);
    else
        snprintf(countdownText, sizeof(countdownText), "-");

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 服务器维护</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px}"
        ".tabs{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 16px}.tab{padding:8px 12px;margin:4px;background:#fff;border:1px solid #ddd;"
        "text-decoration:none;color:#333;border-radius:6px}"
        ".tab.on{background:#175cd3;color:#fff}"
        ".btn-row{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}"
        "button{background:#175cd3;color:#fff;border:0;padding:10px 14px;border-radius:6px;cursor:pointer}"
        ".btn-warn{background:#b54708}.btn-muted{background:#667085}"
        ".muted{color:#667085;font-size:13px}"
        ".ok{color:#027a48}.warn{color:#b54708}"
        "</style></head><body><h1>服务器维护</h1>"
        "<p>开启后倒计时期间每 30 秒全服公告，2 分钟后踢出所有在线玩家并保存数据，同时禁止新登录。</p>");
    vm_mock_admin_append_tabs(&page, "maintenance");

    if (status[0] && message[0])
        vm_mock_admin_text_appendf(&page, "<p>%s: %s</p>", status, message);

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>当前状态</h2>"
        "<p>维护模式：<strong class=\"%s\">%s</strong></p>"
        "<p>在线玩家：%u</p>"
        "<p>踢人倒计时：%s</p>"
        "<p class=\"muted\">上次公告=%u · 踢人完成=%u · kick_at=%u</p>"
        "<div class=\"btn-row\">"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"maintenance-start\">"
        "<button type=\"submit\" class=\"btn-warn\"%s>开启维护（2分钟后踢人）</button></form>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"maintenance-stop\">"
        "<button type=\"submit\" class=\"btn-muted\"%s>关闭维护</button></form>"
        "</div></section>"
        "</body></html>",
        active ? "warn" : "ok",
        active ? "已开启" : "未开启",
        (u32)onlineCount,
        countdownText,
        lastNoticeUnix, (u32)kickDone, kickAtUnix,
        active ? " disabled" : "",
        active ? "" : " disabled");
}

static void vm_mock_admin_handle_maintenance_action(vm_mock_service_socket client,
                                                    const char *action,
                                                    const char *body)
{
    char msg[256];

    (void)body;
    memset(msg, 0, sizeof(msg));
    if (strcmp(action, "maintenance-start") == 0)
    {
        if (vm_mock_maintenance_admin_start(msg, sizeof(msg)))
            vm_mock_admin_redirect_maintenance(client, "ok", msg);
        else
            vm_mock_admin_redirect_maintenance(client, "error", msg);
        return;
    }
    if (strcmp(action, "maintenance-stop") == 0)
    {
        if (vm_mock_maintenance_admin_stop(msg, sizeof(msg)))
            vm_mock_admin_redirect_maintenance(client, "ok", msg);
        else
            vm_mock_admin_redirect_maintenance(client, "error", msg);
        return;
    }
    vm_mock_admin_redirect_maintenance(client, "error", "未知维护操作");
}
