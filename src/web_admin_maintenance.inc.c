/* Admin UI for server maintenance — included from web_admin_server.c. */

static void vm_mock_admin_trim_inplace(char *text)
{
    char *start;
    size_t len;

    if (text == NULL || text[0] == 0)
        return;
    start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;
    if (start != text)
        memmove(text, start, strlen(start) + 1);
    len = strlen(text);
    while (len > 0 &&
           (text[len - 1] == ' ' || text[len - 1] == '\t' ||
            text[len - 1] == '\r' || text[len - 1] == '\n'))
    {
        text[len - 1] = 0;
        --len;
    }
}

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
        "button:disabled{opacity:.45;cursor:not-allowed}"
        ".btn-warn{background:#b54708}.btn-muted{background:#667085}"
        ".btn-danger{background:#b42318}"
        ".muted{color:#667085;font-size:13px}"
        ".ok{color:#027a48}.warn{color:#b54708}.danger{color:#b42318}"
        ".notice{padding:12px 14px;border-radius:8px;margin:12px 0;font-weight:600}"
        ".notice.ok{background:#ecfdf3;color:#027a48;border:1px solid #abefc6}"
        ".notice.error{background:#fef3f2;color:#b42318;border:1px solid #fecdca}"
        "input[type=text]{padding:8px;border:1px solid #d0d5dd;border-radius:6px;min-width:180px}"
        "</style></head><body><h1>服务器维护</h1>"
        "<p>开启后倒计时期间每 30 秒全服公告，2 分钟后踢出所有在线玩家并保存数据，同时禁止新登录。</p>");
    vm_mock_admin_append_tabs(&page, "maintenance");

    if (status[0] && message[0])
    {
        vm_mock_admin_text_appendf(
            &page, "<div class=\"notice %s\" role=\"status\">",
            strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>当前状态</h2>"
        "<p>维护模式：<strong class=\"%s\">%s</strong></p>"
        "<p>在线玩家：%u</p>"
        "<p>踢人倒计时：%s</p>"
        "<p class=\"muted\">上次公告=%u · 踢人完成=%u · kick_at=%u</p>"
        "<div class=\"btn-row\">"
        "<form method=\"post\" action=\"/action\" data-admin-ajax=\"off\">"
        "<input type=\"hidden\" name=\"action\" value=\"maintenance-start\">"
        "<button type=\"submit\" class=\"btn-warn\"%s>开启维护（2分钟后踢人）</button></form>"
        "<form method=\"post\" action=\"/action\" data-admin-ajax=\"off\">"
        "<input type=\"hidden\" name=\"action\" value=\"maintenance-stop\">"
        "<button type=\"submit\" class=\"btn-muted\"%s>关闭维护</button></form>"
        "</div></section>",
        active ? "warn" : "ok",
        active ? "已开启" : "未开启",
        (u32)onlineCount,
        countdownText,
        lastNoticeUnix, (u32)kickDone, kickAtUnix,
        active ? " disabled" : "",
        active ? "" : " disabled");

    /* Wipe button stays enabled so clicks always give feedback; gate in
     * onsubmit + server.  data-admin-ajax=off forces full reload so status
     * and notice are visible (maintenance page has no admin.js toast). */
    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2 class=\"danger\">清档（危险）</h2>"
        "<p class=\"danger\">清除所有玩家相关数据：账号、角色、装备/背包/仓库、任务、"
        "好友/仇敌、公会、聊天、充值订单、比武/天梯/竞拍/水果机/猜猜看进度等。"
        "服务器配置、怪物/任务/NPC/商城等内容表保留。</p>"
        "<p class=\"muted\">须先开启维护（禁止登录）。执行时会立即踢出残留在线连接且不再写回脏数据；"
        "角色 ID 序列重置为从 10001 起。</p>"
        "<form method=\"post\" action=\"/action\" accept-charset=\"UTF-8\" "
        "data-admin-ajax=\"off\" "
        "onsubmit=\"var a=%u;var c=(this.confirm.value||'').replace(/^\\s+|\\s+$/g,'');"
        "this.confirm.value=c;"
        "if(!a){alert('\\u8bf7\\u5148\\u5f00\\u542f\\u7ef4\\u62a4\\u6a21\\u5f0f\\uff0c\\u518d\\u6267\\u884c\\u6e05\\u6863');"
        "return false;}"
        "if(c!=='\\u6e05\\u6863'){alert('\\u8bf7\\u5728\\u786e\\u8ba4\\u6846\\u8f93\\u5165\\u300c\\u6e05\\u6863\\u300d\\u540e\\u518d\\u6267\\u884c');"
        "return false;}"
        "return confirm('\\u786e\\u8ba4\\u6e05\\u9664\\u5168\\u90e8\\u73a9\\u5bb6\\u6570\\u636e\\uff1f\\u6b64\\u64cd\\u4f5c\\u4e0d\\u53ef\\u6062\\u590d\\u3002');\">"
        "<input type=\"hidden\" name=\"action\" value=\"wipe-all-player-data\">"
        "<label class=\"muted\">请输入「清档」确认：</label> "
        "<input type=\"text\" name=\"confirm\" placeholder=\"清档\" autocomplete=\"off\" required> "
        "<button type=\"submit\" class=\"btn-danger\">执行清档</button>"
        "</form></section>"
        "</body></html>",
        active ? 1u : 0u);
}

static void vm_mock_admin_handle_maintenance_action(vm_mock_service_socket client,
                                                    const char *action,
                                                    const char *body)
{
    char msg[256];
    char confirm[32];

    memset(msg, 0, sizeof(msg));
    memset(confirm, 0, sizeof(confirm));
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
    if (strcmp(action, "wipe-all-player-data") == 0)
    {
        (void)vm_mock_admin_form_value(body, "confirm", confirm, sizeof(confirm));
        vm_mock_admin_trim_inplace(confirm);
        if (strcmp(confirm, "清档") != 0)
        {
            vm_mock_admin_redirect_maintenance(
                client, "error", "请在确认框输入「清档」后再执行");
            return;
        }
        if (vm_mock_wipe_all_player_data_admin(msg, sizeof(msg)))
            vm_mock_admin_redirect_maintenance(client, "ok", msg);
        else
            vm_mock_admin_redirect_maintenance(client, "error", msg);
        return;
    }
    vm_mock_admin_redirect_maintenance(client, "error", "未知维护操作");
}
