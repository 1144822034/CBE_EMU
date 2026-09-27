/* Admin UI for gem socket caps — included from web_admin_server.c. */

static void vm_mock_admin_redirect_gem(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=gem&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_render_gem_page(char *response, size_t responseCap,
                                          const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_net_mock_gem_config *cfg;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    cfg = vm_net_mock_gem_config_get();

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 宝石配置</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7;color:#1f2937}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px;"
        "border:1px solid #e4e7ec}"
        ".notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}"
        ".ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}"
        "input{padding:8px;width:160px;box-sizing:border-box}"
        "select{padding:8px;width:160px;box-sizing:border-box}"
        "label{font-size:12px;color:#667085;display:block;margin-bottom:4px}"
        "button{background:#175cd3;color:#fff;border:0;padding:10px 14px;"
        "border-radius:6px;margin-top:12px;cursor:pointer}"
        ".hint{color:#667085;font-size:13px;line-height:1.55}"
        "code{background:#f2f4f7;padding:1px 4px;border-radius:4px}"
        "</style></head><body><h1>宝石配置</h1>"
        "<p class=\"hint\">反伤 / 连击 / 复活宝石共用同一最高等级。"
        "允许 1–16，默认 3。镶嵌与十六级玄晶换宝石可单独开关。"
        "保存后立即写入 <code>server_gem_config</code>。"
        "客户端装备栏「镶嵌」、背包「换宝石」与人物面板字段在下次拉面板后跟随。</p>");
    vm_mock_admin_append_tabs(&page, "gem");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(
            &page, "<div class=\"notice %s\">%s</div>",
            strcmp(status, "ok") == 0 ? "ok" : "error", message);
    }

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>等级与加成</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-gem-config\">"
        "<div><label>宝石最高等级（1–16）</label>"
        "<input name=\"max_gem_level\" value=\"%u\"></div>"
        "<p class=\"hint\">高于上限的散装宝石不能再合成或镶嵌；孔上已有更高等级只按上限计入战斗。</p>"
        "<div style=\"margin-top:12px\"><label>每级加成%%（1–100）</label>"
        "<input name=\"gem_percent_per_level\" value=\"%u\"></div>"
        "<p class=\"hint\">同属性孔等级 × 此值，跨穿戴装备相加，单属性最高 100%%。默认每级 1%%。</p>"
        "<div style=\"margin-top:12px\"><label>允许镶嵌（+16 装备）</label>"
        "<select name=\"socket_embed_enabled\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option>"
        "</select></div>"
        "<p class=\"hint\">关闭时装备栏不显示「镶嵌」，29/17 返回未开放；"
        "已镶嵌装备仍可「拆宝石」。默认开。</p>"
        "<div style=\"margin-top:12px\"><label>允许十六级玄晶兑换宝石</label>"
        "<select name=\"crystal16_exchange_enabled\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option>"
        "</select></div>"
        "<p class=\"hint\">关闭时背包不显示「换宝石」，29/21 请求返回未开放；"
        "默认关。开启后才允许消耗十六级玄晶兑换随机 1 级宝石。</p>"
        "<div style=\"margin-top:12px\"><label>每颗十六级玄晶兑换宝石数（1–20）</label>"
        "<input name=\"crystal16_gem_count\" value=\"%u\"></div>"
        "<p class=\"hint\">消耗 1 颗十六级玄晶（物品 916），随机发放这么多颗 1 级宝石。"
        "默认 5。仅在上方开关为开时生效；背包空位不够会提示空间不足，不会扣晶。</p>"
        "<button type=\"submit\">保存宝石配置</button></form></section></body></html>",
        cfg ? cfg->maxGemLevel : 3u,
        cfg ? cfg->gemPercentPerLevel : 1u,
        cfg && cfg->socketEmbedEnabled ? " selected" : "",
        cfg && !cfg->socketEmbedEnabled ? " selected" : "",
        cfg && cfg->crystal16ExchangeEnabled ? " selected" : "",
        cfg && !cfg->crystal16ExchangeEnabled ? " selected" : "",
        cfg ? cfg->crystal16GemCount : 5u);
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>宝石配置页面超过大小限制。</p>");
    }
}

static void vm_mock_admin_handle_gem_action(vm_mock_service_socket client,
                                            const char *action,
                                            const char *body)
{
    u32 maxLevel = 0;
    u32 percent = 0;
    u32 crystalGrant = 0;
    u32 crystalEnabled = 0;
    u32 socketEmbed = 1;

    if (strcmp(action, "save-gem-config") != 0)
    {
        vm_mock_admin_redirect_gem(client, "error", "未知操作");
        return;
    }
    if (!vm_mock_admin_form_u32(body, "max_gem_level", 16u, &maxLevel) ||
        maxLevel == 0 ||
        !vm_mock_admin_form_u32(body, "gem_percent_per_level", 100u, &percent) ||
        percent == 0 ||
        !vm_mock_admin_form_u32(body, "crystal16_gem_count", 20u, &crystalGrant) ||
        crystalGrant == 0 ||
        !vm_mock_admin_form_u32(body, "crystal16_exchange_enabled", 1u,
                                &crystalEnabled) ||
        !vm_mock_admin_form_u32(body, "socket_embed_enabled", 1u, &socketEmbed))
    {
        vm_mock_admin_redirect_gem(client, "error",
                                   "最高等级须为 1–16，每级加成须为 1–100，"
                                   "十六级玄晶兑换宝石数须为 1–20，"
                                   "镶嵌/兑换开关须为开或关");
        return;
    }
    if (vm_net_mock_gem_config_admin_save((u8)maxLevel, (u8)percent,
                                          (u8)crystalGrant,
                                          (u8)crystalEnabled,
                                          (u8)socketEmbed))
        vm_mock_admin_redirect_gem(client, "ok", "宝石配置已保存");
    else
        vm_mock_admin_redirect_gem(client, "error", "保存宝石配置失败");
}
