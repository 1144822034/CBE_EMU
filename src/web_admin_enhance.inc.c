/* Admin UI for enhance milestones + wash attr pool — included from web_admin_server.c. */

static const char *vm_mock_admin_enhance_attr_name(u8 type, u8 flag)
{
    switch (type)
    {
    case 1:
        return "力量";
    case 2:
        return "敏捷";
    case 3:
        return "智慧";
    case 4:
        return "物攻";
    case 5:
        return "护甲";
    case 6:
        return "躲闪";
    case 7:
        return "命中";
    case 8:
        return "暴击";
    case 9:
        return flag ? "气血%" : "气血";
    case 10:
        return flag ? "法力%" : "法力";
    default:
        return "未知";
    }
}

static const char *vm_mock_admin_enhance_slot_name(u8 slot)
{
    static const char *names[] = {
        "武器", "帽子", "上衣", "披风", "束腰", "裤子", "鞋子", "饰品"
    };
    if (slot >= 8)
        return "?";
    return names[slot];
}

static void vm_mock_admin_redirect_enhance(vm_mock_service_socket client,
                                           const char *section,
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
             "?tab=enhance&section=%s&status=%s&message=%s",
             section && section[0] ? section : "milestones", statusEncoded,
             messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_render_enhance_page(char *response, size_t responseCap,
                                              const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    char section[32];
    u32 milestoneCount = 0;
    u32 poolCount = 0;
    u32 i = 0;
    bool showPool = false;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(section, 0, sizeof(section));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    (void)vm_mock_admin_form_value(query, "section", section, sizeof(section));
    if (section[0] == 0)
        snprintf(section, sizeof(section), "%s", "milestones");
    showPool = strcmp(section, "pool") == 0;

    (void)vm_net_mock_enhance_config_db_load();
    milestoneCount = vm_net_mock_enhance_milestone_admin_count();
    poolCount = vm_net_mock_enhance_attr_pool_admin_count();

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 强化配置</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#1f2937;"
        "font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1120px;margin:0 auto;padding:28px 18px}"
        "h1{font-size:24px;margin:0}.sub{color:#667085;margin:4px 0 20px}"
        ".tabs{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 16px}"
        ".tab{padding:9px 14px;border-radius:7px;color:#475467;text-decoration:none;"
        "background:#fff;border:1px solid #e4e7ec}"
        ".tab.on{background:#175cd3;color:#fff;border-color:#175cd3}"
        ".card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;"
        "padding:18px;box-shadow:0 1px 2px #1018280d;margin-bottom:16px}"
        ".notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}"
        ".ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}"
        "table{border-collapse:collapse;width:100%%}th,td{text-align:left;"
        "padding:10px 8px;border-bottom:1px solid #eaecf0;vertical-align:middle}"
        "th{color:#667085;font-weight:600}"
        "input,select{border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;"
        "background:#fff;max-width:110px}button{border:0;border-radius:6px;"
        "padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer}"
        "button:disabled{opacity:.6;cursor:not-allowed}"
        ".toolbar{display:flex;flex-wrap:wrap;gap:10px;align-items:center;"
        "justify-content:space-between;margin:0 0 12px}"
        ".muted{color:#98a2b3}.chip{display:inline-block;background:#eef4ff;"
        "color:#175cd3;border-radius:999px;padding:2px 8px;font-size:12px;"
        "margin-right:6px}"
        "</style>"
        "<script src=\"/admin.js?v=7\" defer></script>"
        "</head><body><main class=\"wrap\"><header><div>"
        "<h1>强化配置</h1>"
        "<p class=\"sub\">配置各部位 +4/+8/+12/+16 默认词条，以及洗练可随机的属性区间。"
        "未洗练装备显示默认档词条；洗练从属性池随机类型并在区间内取值。"
        "保存后立即生效。</p></div></header>"
        "<nav class=\"tabs\">"
        "<a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱奖励</a>"
        "<a class=\"tab on\" href=\"/?tab=enhance\">强化配置</a>"
        "<a class=\"tab\" href=\"/?tab=arena\">比武</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "</nav>");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(
            &page, "<div class=\"notice %s\">",
            strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><div class=\"tabs\">"
        "<a class=\"tab%s\" href=\"/?tab=enhance&amp;section=milestones\">默认档词条</a>"
        "<a class=\"tab%s\" href=\"/?tab=enhance&amp;section=pool\">洗练属性池</a>"
        "</div>"
        "<p><span class=\"chip\">默认档 %u</span>"
        "<span class=\"chip\">属性池 %u</span></p></section>",
        showPool ? "" : " on", showPool ? " on" : "", milestoneCount, poolCount);

    if (!showPool)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\"><h2>默认档词条（未洗练）</h2>"
            "<table><thead><tr><th>部位</th><th>档次</th><th>属性</th>"
            "<th>%%</th><th>启用</th><th></th></tr></thead><tbody>");
        for (i = 0; i < milestoneCount; ++i)
        {
            const vm_net_mock_enhance_milestone_cfg *row =
                vm_net_mock_enhance_milestone_admin_row_at(i);
            if (row == NULL)
                continue;
            vm_mock_admin_text_appendf(
                &page,
                "<tr><form method=\"post\" action=\"/action\">"
                "<td>%s<input type=\"hidden\" name=\"action\" "
                "value=\"enhance-milestone-upsert\">"
                "<input type=\"hidden\" name=\"slot\" value=\"%u\">"
                "<input type=\"hidden\" name=\"unlock\" value=\"%u\"></td>"
                "<td>+%u</td>"
                "<td><select name=\"attr_type\">",
                vm_mock_admin_enhance_slot_name(row->equipSlot), row->equipSlot,
                row->unlockLevel, row->unlockLevel);
            {
                u8 t;
                for (t = 1; t <= 10; ++t)
                {
                    vm_mock_admin_text_appendf(
                        &page, "<option value=\"%u\"%s>%u %s</option>", t,
                        t == row->attrType ? " selected" : "", t,
                        vm_mock_admin_enhance_attr_name(t, 0));
                }
            }
            vm_mock_admin_text_appendf(
                &page,
                "</select></td>"
                "<td><select name=\"flag\">"
                "<option value=\"0\"%s>值</option>"
                "<option value=\"1\"%s>%%</option></select></td>"
                "<td><select name=\"enabled\">"
                "<option value=\"1\"%s>是</option>"
                "<option value=\"0\"%s>否</option></select></td>"
                "<td><button type=\"submit\">保存</button></td>"
                "</form></tr>",
                row->flag == 0 ? " selected" : "",
                row->flag != 0 ? " selected" : "",
                row->enabled ? " selected" : "",
                !row->enabled ? " selected" : "");
        }
        vm_mock_admin_text_appendf(
            &page,
            "</tbody></table>"
            "<p class=\"muted\">表：MySQL <code>server_enhance_milestones</code>。"
            "未洗练数值仍按装备品质公式计算；仅 type/flag 可配。</p></section>");
    }
    else
    {
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\">"
            "<form method=\"post\" action=\"/action\" id=\"enhance-pool-form\">"
            "<input type=\"hidden\" name=\"action\" value=\"enhance-pool-save-all\">"
            "<input type=\"hidden\" name=\"row_count\" value=\"%u\">"
            "<div class=\"toolbar\"><h2 style=\"margin:0\">洗练属性池</h2>"
            "<button type=\"submit\">一键保存全部</button></div>"
            "<table><thead><tr><th>属性</th><th>%%</th><th>最小</th><th>最大</th>"
            "<th>权重</th><th>启用</th></tr></thead><tbody>",
            poolCount);
        for (i = 0; i < poolCount; ++i)
        {
            const vm_net_mock_enhance_attr_pool_cfg *row =
                vm_net_mock_enhance_attr_pool_admin_row_at(i);
            if (row == NULL)
                continue;
            vm_mock_admin_text_appendf(
                &page,
                "<tr>"
                "<td>%s"
                "<input type=\"hidden\" name=\"attr_type_%u\" value=\"%u\">"
                "<input type=\"hidden\" name=\"flag_%u\" value=\"%u\"></td>"
                "<td>%s</td>"
                "<td><input name=\"value_min_%u\" type=\"number\" min=\"0\" "
                "max=\"65535\" value=\"%u\"></td>"
                "<td><input name=\"value_max_%u\" type=\"number\" min=\"0\" "
                "max=\"65535\" value=\"%u\"></td>"
                "<td><input name=\"weight_%u\" type=\"number\" min=\"1\" "
                "value=\"%u\"></td>"
                "<td><select name=\"enabled_%u\">"
                "<option value=\"1\"%s>是</option>"
                "<option value=\"0\"%s>否</option></select></td>"
                "</tr>",
                vm_mock_admin_enhance_attr_name(row->attrType, row->flag), i,
                row->attrType, i, row->flag, row->flag ? "%" : "值", i,
                row->valueMin, i, row->valueMax, i, row->weight, i,
                row->enabled ? " selected" : "",
                !row->enabled ? " selected" : "");
        }
        vm_mock_admin_text_appendf(
            &page,
            "</tbody></table>"
            "<div class=\"toolbar\" style=\"margin-top:14px\">"
            "<p class=\"muted\" style=\"margin:0\">表：MySQL "
            "<code>server_enhance_attr_pool</code>。"
            "改完点「一键保存全部」，页面不刷新，仅提示结果。</p>"
            "<button type=\"submit\">一键保存全部</button></div>"
            "</form></section>");
    }

    vm_mock_admin_text_appendf(&page, "</main></body></html>");
}

static void vm_mock_admin_handle_enhance_action(vm_mock_service_socket client,
                                                const char *action,
                                                const char *body)
{
    char slotText[16];
    char unlockText[16];
    char typeText[16];
    char flagText[16];
    char enabledText[16];
    char minText[16];
    char maxText[16];
    char weightText[16];
    char countText[16];
    char key[48];
    u32 slot = 0, unlock = 0, type = 0, flag = 0, enabled = 1;
    u32 valueMin = 0, valueMax = 0, weight = 1, rowCount = 0;
    u32 i = 0;

    memset(slotText, 0, sizeof(slotText));
    memset(unlockText, 0, sizeof(unlockText));
    memset(typeText, 0, sizeof(typeText));
    memset(flagText, 0, sizeof(flagText));
    memset(enabledText, 0, sizeof(enabledText));
    memset(minText, 0, sizeof(minText));
    memset(maxText, 0, sizeof(maxText));
    memset(weightText, 0, sizeof(weightText));
    memset(countText, 0, sizeof(countText));

    if (strcmp(action, "enhance-milestone-upsert") == 0)
    {
        (void)vm_mock_admin_form_value(body, "slot", slotText, sizeof(slotText));
        (void)vm_mock_admin_form_value(body, "unlock", unlockText,
                                       sizeof(unlockText));
        (void)vm_mock_admin_form_value(body, "attr_type", typeText,
                                       sizeof(typeText));
        (void)vm_mock_admin_form_value(body, "flag", flagText, sizeof(flagText));
        (void)vm_mock_admin_form_value(body, "enabled", enabledText,
                                       sizeof(enabledText));
        if (!vm_net_mock_parse_u32_strict(slotText, &slot) ||
            !vm_net_mock_parse_u32_strict(unlockText, &unlock) ||
            !vm_net_mock_parse_u32_strict(typeText, &type) ||
            !vm_net_mock_parse_u32_strict(flagText, &flag) ||
            !vm_net_mock_parse_u32_strict(enabledText, &enabled) ||
            !vm_net_mock_enhance_milestone_admin_upsert(
                (u8)slot, (u8)unlock, (u8)type, (u8)flag, (u8)enabled))
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "保存默认档词条失败");
            return;
        }
        vm_mock_admin_redirect_enhance(client, "milestones", "ok",
                                       "默认档词条已保存");
        return;
    }

    if (strcmp(action, "enhance-pool-upsert") == 0)
    {
        (void)vm_mock_admin_form_value(body, "attr_type", typeText,
                                       sizeof(typeText));
        (void)vm_mock_admin_form_value(body, "flag", flagText, sizeof(flagText));
        (void)vm_mock_admin_form_value(body, "value_min", minText,
                                       sizeof(minText));
        (void)vm_mock_admin_form_value(body, "value_max", maxText,
                                       sizeof(maxText));
        (void)vm_mock_admin_form_value(body, "weight", weightText,
                                       sizeof(weightText));
        (void)vm_mock_admin_form_value(body, "enabled", enabledText,
                                       sizeof(enabledText));
        if (!vm_net_mock_parse_u32_strict(typeText, &type) ||
            !vm_net_mock_parse_u32_strict(flagText, &flag) ||
            !vm_net_mock_parse_u32_strict(minText, &valueMin) ||
            !vm_net_mock_parse_u32_strict(maxText, &valueMax) ||
            !vm_net_mock_parse_u32_strict(weightText, &weight) ||
            !vm_net_mock_parse_u32_strict(enabledText, &enabled) ||
            !vm_net_mock_enhance_attr_pool_admin_upsert(
                (u8)type, (u8)flag, valueMin, valueMax, weight, (u8)enabled))
        {
            vm_mock_admin_redirect_enhance(client, "pool", "error",
                                           "保存洗练属性池失败");
            return;
        }
        vm_mock_admin_redirect_enhance(client, "pool", "ok", "洗练属性池已保存");
        return;
    }

    if (strcmp(action, "enhance-pool-save-all") == 0)
    {
        vm_net_mock_enhance_attr_pool_cfg rows[VM_NET_MOCK_ENHANCE_ATTR_POOL_MAX];
        u32 saved = 0;

        memset(rows, 0, sizeof(rows));
        (void)vm_mock_admin_form_value(body, "row_count", countText,
                                       sizeof(countText));
        if (!vm_net_mock_parse_u32_strict(countText, &rowCount) ||
            rowCount == 0 || rowCount > VM_NET_MOCK_ENHANCE_ATTR_POOL_MAX)
        {
            vm_mock_admin_redirect_enhance(client, "pool", "error",
                                           "洗练属性池行数无效");
            return;
        }
        for (i = 0; i < rowCount; ++i)
        {
            snprintf(key, sizeof(key), "attr_type_%u", i);
            memset(typeText, 0, sizeof(typeText));
            if (!vm_mock_admin_form_value(body, key, typeText, sizeof(typeText)))
            {
                vm_mock_admin_redirect_enhance(client, "pool", "error",
                                               "缺少属性类型字段");
                return;
            }
            snprintf(key, sizeof(key), "flag_%u", i);
            memset(flagText, 0, sizeof(flagText));
            (void)vm_mock_admin_form_value(body, key, flagText, sizeof(flagText));
            snprintf(key, sizeof(key), "value_min_%u", i);
            memset(minText, 0, sizeof(minText));
            (void)vm_mock_admin_form_value(body, key, minText, sizeof(minText));
            snprintf(key, sizeof(key), "value_max_%u", i);
            memset(maxText, 0, sizeof(maxText));
            (void)vm_mock_admin_form_value(body, key, maxText, sizeof(maxText));
            snprintf(key, sizeof(key), "weight_%u", i);
            memset(weightText, 0, sizeof(weightText));
            (void)vm_mock_admin_form_value(body, key, weightText,
                                           sizeof(weightText));
            snprintf(key, sizeof(key), "enabled_%u", i);
            memset(enabledText, 0, sizeof(enabledText));
            (void)vm_mock_admin_form_value(body, key, enabledText,
                                           sizeof(enabledText));
            if (!vm_net_mock_parse_u32_strict(typeText, &type) ||
                !vm_net_mock_parse_u32_strict(flagText, &flag) ||
                !vm_net_mock_parse_u32_strict(minText, &valueMin) ||
                !vm_net_mock_parse_u32_strict(maxText, &valueMax) ||
                !vm_net_mock_parse_u32_strict(weightText, &weight) ||
                !vm_net_mock_parse_u32_strict(enabledText, &enabled) ||
                type < 1 || type > 10 || flag > 1 || valueMax < valueMin ||
                weight == 0)
            {
                vm_mock_admin_redirect_enhance(client, "pool", "error",
                                               "洗练属性池字段无效");
                return;
            }
            rows[saved].attrType = (u8)type;
            rows[saved].flag = (u8)flag;
            rows[saved].valueMin = valueMin;
            rows[saved].valueMax = valueMax;
            rows[saved].weight = weight;
            rows[saved].enabled = enabled != 0 ? 1 : 0;
            ++saved;
        }
        if (!vm_net_mock_enhance_attr_pool_admin_save_all(rows, saved))
        {
            vm_mock_admin_redirect_enhance(client, "pool", "error",
                                           "一键保存洗练属性池失败");
            return;
        }
        {
            char okMsg[96];
            snprintf(okMsg, sizeof(okMsg), "已保存全部 %u 条洗练属性", saved);
            vm_mock_admin_redirect_enhance(client, "pool", "ok", okMsg);
        }
        return;
    }

    vm_mock_admin_redirect_enhance(client, "milestones", "error", "未知操作");
}
