/* Gateway zone admin tab + action handlers. Included from web_admin_server.c. */

static void vm_mock_admin_redirect_gateways(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=gateways&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static bool vm_mock_admin_copy_utf8_field(const char *src, char *dst,
                                          size_t dstCap)
{
    size_t len = 0;

    if (dst == NULL || dstCap == 0)
        return false;
    dst[0] = 0;
    if (src == NULL)
        return true;
    len = strlen(src);
    if (len >= dstCap)
        return false;
    memcpy(dst, src, len + 1);
    return true;
}

static void vm_mock_admin_render_gateways_page(char *response,
                                               size_t responseCap,
                                               const char *query)
{
    vm_mock_admin_text page;
    vm_net_mock_gateway_zone rows[VM_NET_MOCK_GATEWAY_ZONE_MAX];
    char status[16];
    char message[256];
    u32 rowCount = 0;
    u32 enabledCount = 0;

    memset(rows, 0, sizeof(rows));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    rowCount = vm_net_mock_gateway_zone_admin_list(
        rows, VM_NET_MOCK_GATEWAY_ZONE_MAX);
    for (u32 i = 0; i < rowCount; ++i)
    {
        if (rows[i].enabled)
            ++enabledCount;
    }

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 区服网关</title><style>"
        "*{box-sizing:border-box}html,body{min-height:100%%}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1240px;margin:0 auto;padding:24px 18px 42px}header{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}h1{font-size:24px;margin:0}h2{font-size:18px;margin:0 0 12px}.sub,.muted{color:#667085}.sub{margin:4px 0 16px}.logout{background:none;color:#667085;border:1px solid #d0d5dd}.summary{display:flex;gap:9px;flex-wrap:wrap;margin-bottom:14px}.badge{padding:3px 8px;border-radius:999px;background:#eef4ff;color:#175cd3}.badge.off{background:#fef3f2;color:#b42318}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d;margin-bottom:16px}.notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.server-list{display:grid;gap:12px}.server{border:1px solid #e4e7ec;border-radius:9px;padding:13px}.server-head{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-bottom:10px}.server-head h3{font-size:16px;margin:0}.state{font-size:12px;font-weight:650}.state.on{color:#027a48}.state.off{color:#b42318}.fields{display:grid;grid-template-columns:100px minmax(140px,1fr) minmax(100px,.8fr) minmax(220px,1.4fr) minmax(180px,1.1fr) 90px 100px 100px auto;gap:9px;align-items:end}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}input,select{width:100%%;min-width:0;border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer;white-space:nowrap}.danger{background:#b42318}.actions{display:flex;gap:8px;align-items:end}.create-fields{display:grid;grid-template-columns:100px minmax(140px,1fr) minmax(100px,.8fr) minmax(220px,1.4fr) minmax(180px,1.1fr) 90px 100px 100px auto;gap:10px;align-items:end}.hint{margin:11px 0 0;color:#667085;font-size:12px;line-height:1.65}@media(max-width:1100px){.fields,.create-fields{grid-template-columns:1fr 1fr}.actions{align-items:stretch}}@media(max-width:620px){.wrap{padding:16px 10px}.fields,.create-fields{grid-template-columns:1fr}.actions{display:grid;grid-template-columns:1fr 1fr}}</style>"
        "</head><body><main class=\"wrap\"><header><div><h1>江湖OL 后台管理</h1>"
        "<p class=\"sub\">区服网关 · Pixi 进游戏前选区连 WS；与标题服务器列表无关</p>"
        "</div><form method=\"post\" action=\"/logout\">"
        "<button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_append_tabs(&page, "gateways");
    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><div class=\"summary\">"
        "<span class=\"badge\">已配置 %u / %u</span>"
        "<span class=\"badge\">已启用 %u</span>"
        "<span class=\"badge off\">已停用 %u</span></div>",
        rowCount, VM_NET_MOCK_GATEWAY_ZONE_MAX, enabledCount,
        rowCount >= enabledCount ? rowCount - enabledCount : 0);
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (rowCount == 0)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<p class=\"muted\">区服网关目录为空或无法从 MySQL 读取；"
            "Pixi 将回退 FIXED_LIVE_GATEWAY。</p>");
    }
    vm_mock_admin_text_appendf(&page, "<div class=\"server-list\">");
    for (u32 i = 0; i < rowCount; ++i)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<article class=\"server\"><div class=\"server-head\">"
            "<h3>区服 #%u%s</h3><span class=\"state %s\">%s</span></div>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-gateway-zone\">"
            "<div class=\"fields\">"
            "<label class=\"field\"><span>区服 ID</span>"
            "<input name=\"zone_id\" type=\"number\" value=\"%u\" readonly></label>"
            "<label class=\"field\"><span>显示名称（UTF-8）</span>"
            "<input name=\"display_name\" maxlength=\"63\" value=\"",
            rows[i].zoneId, rows[i].isDefault ? " · 默认" : "",
            rows[i].enabled ? "on" : "off",
            rows[i].enabled ? "已启用" : "已停用", rows[i].zoneId);
        vm_mock_admin_text_append_html(&page, rows[i].displayName);
        vm_mock_admin_text_appendf(
            &page,
            "\" required></label>"
            "<label class=\"field\"><span>状态标签</span>"
            "<input name=\"status_label\" maxlength=\"63\" value=\"");
        vm_mock_admin_text_append_html(&page, rows[i].label);
        vm_mock_admin_text_appendf(
            &page,
            "\"></label>"
            "<label class=\"field\"><span>WS 网关 URL</span>"
            "<input name=\"ws_url\" maxlength=\"191\" value=\"");
        vm_mock_admin_text_append_html(&page, rows[i].wsUrl);
        vm_mock_admin_text_appendf(
            &page,
            "\" required></label>"
            "<label class=\"field\"><span>账号页 URL（可选）</span>"
            "<input name=\"account_web_url\" maxlength=\"191\" value=\"");
        vm_mock_admin_text_append_html(&page, rows[i].accountWebUrl);
        vm_mock_admin_text_appendf(
            &page,
            "\"></label>"
            "<label class=\"field\"><span>排序</span>"
            "<input name=\"sort_order\" type=\"number\" min=\"0\" "
            "max=\"4294967295\" value=\"%u\" required></label>"
            "<label class=\"field\"><span>状态</span>"
            "<select name=\"enabled\">"
            "<option value=\"1\"%s>启用</option>"
            "<option value=\"0\"%s>停用</option></select></label>"
            "<label class=\"field\"><span>默认区</span>"
            "<select name=\"is_default\">"
            "<option value=\"1\"%s>是</option>"
            "<option value=\"0\"%s>否</option></select></label>"
            "<div class=\"actions\"><button type=\"submit\">保存</button></div>"
            "</div></form>"
            "<form class=\"actions\" method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"delete-gateway-zone\">"
            "<input type=\"hidden\" name=\"zone_id\" value=\"%u\">"
            "<button class=\"danger\" type=\"submit\">删除</button></form>"
            "</article>",
            rows[i].sortOrder,
            rows[i].enabled ? " selected" : "",
            rows[i].enabled ? "" : " selected",
            rows[i].isDefault ? " selected" : "",
            rows[i].isDefault ? "" : " selected", rows[i].zoneId);
    }
    vm_mock_admin_text_appendf(
        &page,
        "</div></section><section class=\"card\"><h2>新增区服网关</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"create-gateway-zone\">"
        "<div class=\"create-fields\">"
        "<label class=\"field\"><span>区服 ID</span>"
        "<input name=\"zone_id\" type=\"number\" min=\"1\" max=\"4294967295\" required></label>"
        "<label class=\"field\"><span>显示名称</span>"
        "<input name=\"display_name\" maxlength=\"63\" required></label>"
        "<label class=\"field\"><span>状态标签</span>"
        "<input name=\"status_label\" maxlength=\"63\" value=\"推荐\"></label>"
        "<label class=\"field\"><span>WS 网关 URL</span>"
        "<input name=\"ws_url\" maxlength=\"191\" "
        "placeholder=\"ws://host:19092\" required></label>"
        "<label class=\"field\"><span>账号页 URL</span>"
        "<input name=\"account_web_url\" maxlength=\"191\" "
        "placeholder=\"http://host:19091\"></label>"
        "<label class=\"field\"><span>排序</span>"
        "<input name=\"sort_order\" type=\"number\" min=\"0\" "
        "max=\"4294967295\" value=\"0\" required></label>"
        "<label class=\"field\"><span>状态</span>"
        "<select name=\"enabled\"><option value=\"1\">启用</option>"
        "<option value=\"0\">停用</option></select></label>"
        "<label class=\"field\"><span>默认区</span>"
        "<select name=\"is_default\"><option value=\"0\">否</option>"
        "<option value=\"1\">是</option></select></label>"
        "<div class=\"actions\"><button type=\"submit\">新增区服</button></div>"
        "</div></form>"
        "<p class=\"hint\">此目录经公开 GET /api/zones（gateway 代理）发给 Pixi；"
        "选区后连接对应 ws_url，不会改标题 WT 1/1/12。"
        "最多 16 项。空目录时客户端回退固定引导网关。</p>"
        "</section></main></body></html>");
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\">"
                 "<p>区服网关管理页面超过大小限制。</p>");
    }
}

static void vm_mock_admin_handle_gateway_zone_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    vm_net_mock_gateway_zone row;
    char displayUtf8[128];
    char labelUtf8[128];
    char wsUrl[192];
    char accountWeb[192];
    char enabledText[8];
    char defaultText[8];
    const char *error = NULL;

    memset(&row, 0, sizeof(row));
    memset(displayUtf8, 0, sizeof(displayUtf8));
    memset(labelUtf8, 0, sizeof(labelUtf8));
    memset(wsUrl, 0, sizeof(wsUrl));
    memset(accountWeb, 0, sizeof(accountWeb));
    memset(enabledText, 0, sizeof(enabledText));
    memset(defaultText, 0, sizeof(defaultText));
    if (!vm_mock_admin_form_u32(body, "zone_id", 0xffffffffu, &row.zoneId) ||
        row.zoneId == 0)
    {
        vm_mock_admin_redirect_gateways(client, "error", "区服 ID 无效");
        return;
    }
    if (strcmp(action, "delete-gateway-zone") == 0)
    {
        if (!vm_net_mock_gateway_zone_admin_delete(row.zoneId, &error))
        {
            vm_mock_admin_redirect_gateways(
                client, "error", error ? error : "区服删除失败");
            return;
        }
        vm_mock_admin_redirect_gateways(client, "ok", "区服已删除");
        return;
    }
    if ((strcmp(action, "save-gateway-zone") != 0 &&
         strcmp(action, "create-gateway-zone") != 0) ||
        !vm_mock_admin_form_value(body, "display_name", displayUtf8,
                                  sizeof(displayUtf8)) ||
        !vm_mock_admin_form_value(body, "ws_url", wsUrl, sizeof(wsUrl)) ||
        !vm_mock_admin_copy_utf8_field(displayUtf8, row.displayName,
                                       sizeof(row.displayName)) ||
        !vm_mock_admin_copy_utf8_field(wsUrl, row.wsUrl, sizeof(row.wsUrl)) ||
        !vm_mock_admin_form_u32(body, "sort_order", 0xffffffffu,
                                &row.sortOrder) ||
        !vm_mock_admin_form_value(body, "enabled", enabledText,
                                  sizeof(enabledText)) ||
        (strcmp(enabledText, "0") != 0 && strcmp(enabledText, "1") != 0) ||
        !vm_mock_admin_form_value(body, "is_default", defaultText,
                                  sizeof(defaultText)) ||
        (strcmp(defaultText, "0") != 0 && strcmp(defaultText, "1") != 0))
    {
        vm_mock_admin_redirect_gateways(
            client, "error", "区服名称、WS URL、排序或状态无效");
        return;
    }
    (void)vm_mock_admin_form_value(body, "status_label", labelUtf8,
                                   sizeof(labelUtf8));
    (void)vm_mock_admin_form_value(body, "account_web_url", accountWeb,
                                   sizeof(accountWeb));
    if (!vm_mock_admin_copy_utf8_field(labelUtf8, row.label,
                                       sizeof(row.label)) ||
        !vm_mock_admin_copy_utf8_field(accountWeb, row.accountWebUrl,
                                       sizeof(row.accountWebUrl)))
    {
        vm_mock_admin_redirect_gateways(client, "error",
                                        "状态标签或账号页 URL 过长");
        return;
    }
    row.enabled = strcmp(enabledText, "1") == 0;
    row.isDefault = strcmp(defaultText, "1") == 0;
    if (!vm_net_mock_gateway_zone_admin_save(&row, &error))
    {
        vm_mock_admin_redirect_gateways(
            client, "error", error ? error : "区服保存失败");
        return;
    }
    vm_mock_admin_redirect_gateways(
        client, "ok",
        strcmp(action, "create-gateway-zone") == 0 ? "区服已新增"
                                                   : "区服已保存");
}
