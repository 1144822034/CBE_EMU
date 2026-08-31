/* Admin UI for fruit machine — included from web_admin_server.c. */

static void vm_mock_admin_redirect_fruit(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=fruit&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static const char *vm_mock_admin_fruit_slot_label(u32 slot)
{
    static const char *kLabels[24] = {
        "LUCK", "苹果", "铃S", "橙", "铃", "BARs", "BAR", "苹果",
        "苹果S", "芒", "瓜", "瓜S", "LUCK", "苹果", "橙S", "橙",
        "铃", "77S", "77", "苹果", "芒S", "芒", "星", "星S"};
    if (slot >= 24u)
        return "?";
    return kLabels[slot];
}

static void vm_mock_admin_render_fruit_page(char *response, size_t responseCap,
                                           const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_mock_fruit_config *cfg;
    u32 slot;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    cfg = vm_net_mock_fruit_config_get();

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 水果机</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px}"
        ".tabs{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 16px}.tab{padding:8px 12px;margin:4px;background:#fff;border:1px solid #ddd;"
        "text-decoration:none;color:#333;border-radius:6px}"
        ".tab.on{background:#175cd3;color:#fff}"
        "input,select{padding:6px;width:100%%;box-sizing:border-box}"
        "label{font-size:12px;color:#666;display:block;margin-bottom:4px}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}"
        "table{border-collapse:collapse;width:100%%}th,td{padding:6px;border-bottom:1px solid #eee;font-size:12px}"
        "button{background:#175cd3;color:#fff;border:0;padding:10px 14px;border-radius:6px;margin-top:8px}"
        ".slot-name{color:#667085;white-space:nowrap}"
        "</style></head><body><h1>娱乐 · 水果机</h1>"
        "<p>投币货币与 24 格轮盘倍率/权重可在后台热更新；开奖算法仍在服务端执行，客户端无法改奖。</p>");
    vm_mock_admin_append_tabs(&page, "fruit");

    if (status[0] && message[0])
        vm_mock_admin_text_appendf(&page, "<p>%s: %s</p>", status, message);

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>基础配置</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-fruit-config\">"
        "<div class=\"grid\">"
        "<div><label>启用</label><select name=\"enabled\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
        "<div><label>投币货币</label><select name=\"currency_type\">"
        "<option value=\"0\"%s>金币</option>"
        "<option value=\"1\"%s>酷宝</option></select></div>"
        "<div><label>每单位兑换游戏币</label>"
        "<input name=\"credits_per_unit\" value=\"%u\"></div>"
        "<div><label>最少投币(单位)</label><input name=\"min_insert\" value=\"%u\"></div>"
        "<div><label>最多投币(单位)</label><input name=\"max_insert\" value=\"%u\"></div>"
        "<div><label>送灯概率%%</label><input name=\"bonus_chance\" value=\"%u\"></div>"
        "<div><label>送灯额外+%%</label><input name=\"bonus_extra_percent\" value=\"%u\"></div>"
        "</div><button type=\"submit\">保存基础配置</button></form></section>",
        cfg && cfg->enabled ? " selected" : "",
        cfg && !cfg->enabled ? " selected" : "",
        cfg && cfg->currencyType == 0 ? " selected" : "",
        cfg && cfg->currencyType == 1 ? " selected" : "",
        cfg ? cfg->creditsPerUnit : 10u,
        cfg ? cfg->minInsert : 1u,
        cfg ? cfg->maxInsert : 999u,
        cfg ? cfg->bonusChance : 8u,
        cfg ? cfg->bonusExtraPercent : 50u);

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>轮盘 24 格（倍率 / 权重）</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-fruit-wheel\">"
        "<table><tr><th>格</th><th>名称</th><th>水果</th><th>倍率</th><th>权重</th></tr>");

    for (slot = 0; slot < 24u; ++slot)
    {
        const vm_mock_fruit_wheel_slot *ws = vm_net_mock_fruit_wheel_slot(slot);
        u8 fruitId = ws ? ws->fruitId : 0;
        u32 mult = ws ? ws->multiplier : 0;
        u32 weight = ws ? ws->weight : 1;
        char prefix[16];

        snprintf(prefix, sizeof(prefix), "s%u", slot);
        vm_mock_admin_text_appendf(
            &page,
            "<tr><td>%u</td><td class=\"slot-name\">%s</td>"
            "<td><select name=\"%s_fruit\">"
            "<option value=\"0\"%s>幸运</option>"
            "<option value=\"1\"%s>BAR</option>"
            "<option value=\"2\"%s>77</option>"
            "<option value=\"3\"%s>星</option>"
            "<option value=\"4\"%s>瓜</option>"
            "<option value=\"5\"%s>铃</option>"
            "<option value=\"6\"%s>芒</option>"
            "<option value=\"7\"%s>橙</option>"
            "<option value=\"8\"%s>苹</option></select></td>"
            "<td><input name=\"%s_mult\" value=\"%u\"></td>"
            "<td><input name=\"%s_weight\" value=\"%u\"></td></tr>",
            slot, vm_mock_admin_fruit_slot_label(slot), prefix,
            fruitId == 0 ? " selected" : "",
            fruitId == 1 ? " selected" : "",
            fruitId == 2 ? " selected" : "",
            fruitId == 3 ? " selected" : "",
            fruitId == 4 ? " selected" : "",
            fruitId == 5 ? " selected" : "",
            fruitId == 6 ? " selected" : "",
            fruitId == 7 ? " selected" : "",
            fruitId == 8 ? " selected" : "",
            prefix, mult, prefix, weight);
    }

    vm_mock_admin_text_appendf(
        &page,
        "</table>"
        "<button type=\"submit\">保存轮盘配置</button></form>"
        "<form method=\"post\" action=\"/action\" style=\"margin-top:10px\">"
        "<input type=\"hidden\" name=\"action\" value=\"reset-fruit-wheel\">"
        "<button type=\"submit\" style=\"background:#667085\">恢复默认轮盘</button>"
        "</form></section></body></html>");
}

static void vm_mock_admin_handle_fruit_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body)
{
    if (strcmp(action, "save-fruit-config") == 0)
    {
        u32 enabled = 1;
        u32 currencyType = 0;
        u32 creditsPerUnit = 10;
        u32 minInsert = 1;
        u32 maxInsert = 999;
        u32 bonusChance = 8;
        u32 bonusExtra = 50;
        char errMsg[384];

        if (!vm_mock_admin_form_u32(body, "enabled", 1u, &enabled) ||
            !vm_mock_admin_form_u32(body, "currency_type", 1u, &currencyType) ||
            !vm_mock_admin_form_u32(body, "credits_per_unit", 100000u,
                                    &creditsPerUnit) ||
            !vm_mock_admin_form_u32(body, "min_insert", 999999u, &minInsert) ||
            !vm_mock_admin_form_u32(body, "max_insert", 999999u, &maxInsert) ||
            !vm_mock_admin_form_u32(body, "bonus_chance", 100u, &bonusChance) ||
            !vm_mock_admin_form_u32(body, "bonus_extra_percent", 200u,
                                    &bonusExtra))
        {
            vm_mock_admin_redirect_fruit(client, "error", "表单数值无效");
            return;
        }
        if (minInsert == 0 || maxInsert < minInsert || creditsPerUnit == 0)
        {
            vm_mock_admin_redirect_fruit(client, "error", "投币范围或兑换比例无效");
            return;
        }
        if (vm_mock_fruit_admin_save_config(
                (u8)enabled, (u8)currencyType, creditsPerUnit, minInsert,
                maxInsert, (u8)bonusChance, (u8)bonusExtra))
        {
            vm_mock_admin_redirect_fruit(client, "ok", "水果机基础配置已保存");
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
        vm_mock_admin_redirect_fruit(client, "error", errMsg);
        return;
    }
    if (strcmp(action, "save-fruit-wheel") == 0)
    {
        u32 slot;
        for (slot = 0; slot < 24u; ++slot)
        {
            char field[24];
            u32 fruitId = 0;
            u32 mult = 0;
            u32 weight = 0;

            snprintf(field, sizeof(field), "s%u_fruit", slot);
            if (!vm_mock_admin_form_u32(body, field, 8u, &fruitId))
            {
                vm_mock_admin_redirect_fruit(client, "error", "轮盘水果无效");
                return;
            }
            snprintf(field, sizeof(field), "s%u_mult", slot);
            if (!vm_mock_admin_form_u32(body, field, 65535u, &mult))
            {
                vm_mock_admin_redirect_fruit(client, "error", "轮盘倍率无效");
                return;
            }
            snprintf(field, sizeof(field), "s%u_weight", slot);
            if (!vm_mock_admin_form_u32(body, field, 65535u, &weight) ||
                weight == 0)
            {
                vm_mock_admin_redirect_fruit(client, "error", "轮盘权重须>=1");
                return;
            }
            if (!vm_mock_fruit_admin_save_wheel_slot(
                    (u8)slot, (u8)fruitId, (u16)mult, (u16)weight))
            {
                vm_mock_admin_redirect_fruit(client, "error", "轮盘保存失败");
                return;
            }
        }
        vm_mock_fruit_admin_reload_config();
        vm_mock_admin_redirect_fruit(client, "ok", "轮盘配置已保存");
        return;
    }
    if (strcmp(action, "reset-fruit-wheel") == 0)
    {
        if (vm_mock_fruit_admin_reset_wheel_defaults())
            vm_mock_admin_redirect_fruit(client, "ok", "已恢复默认轮盘");
        else
            vm_mock_admin_redirect_fruit(client, "error", "恢复默认轮盘失败");
        return;
    }
}
