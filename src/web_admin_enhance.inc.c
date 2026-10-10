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
        ".wrap{max-width:1280px;margin:0 auto;padding:28px 18px}"
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
        "background:#fff;max-width:96px}button{border:0;border-radius:6px;"
        "padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer}"
        "button:disabled{opacity:.6;cursor:not-allowed}"
        ".toolbar{display:flex;flex-wrap:wrap;gap:10px;align-items:center;"
        "justify-content:space-between;margin:0 0 12px}"
        ".muted{color:#98a2b3}.chip{display:inline-block;background:#eef4ff;"
        "color:#175cd3;border-radius:999px;padding:2px 8px;font-size:12px;"
        "margin-right:6px}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));"
        "gap:12px;align-items:end}"
        "label.field{display:block;font-size:12px;color:#667085}"
        "label.field select{display:block;width:100%%;max-width:none;margin-top:4px}"
        "</style>"
        "<script src=\"/admin.js?v=7\" defer></script>"
        "</head><body><main class=\"wrap\"><header><div>"
        "<h1>强化配置</h1>"
        "<p class=\"sub\">配置强化基础缩放、强化金币消耗、各部位 +4/+8/+12/+16 默认词条，以及洗练属性池。"
        "未洗练装备显示默认档词条；洗练从属性池随机类型并在区间内取值。"
        "保存后立即生效（装备详情需重新打开人物信息/重登后同步客户端显示）。</p></div></header>");
    vm_mock_admin_append_tabs(&page, "enhance");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(
            &page, "<div class=\"notice %s\">",
            strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }

    {
        u8 qualityScaleOn =
            vm_net_mock_enhance_wash_quality_scale_enabled();
        u8 firstSeedPercent = vm_net_mock_enhance_wash_first_seed_percent();
        u8 transferWashOn = vm_net_mock_enhance_wash_transfer_enabled();
        u8 crossStoneOn = vm_net_mock_enhance_cross_quality_stone_required();
        u8 transferCrystalOn = vm_net_mock_enhance_transfer_crystal_enabled();
        u8 transferMoneyOn = vm_net_mock_enhance_transfer_money_enabled();
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\">"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" "
            "value=\"enhance-wash-settings-save\">"
            "<div class=\"toolbar\"><h2 style=\"margin:0\">洗练 / 重铸规则</h2>"
            "<button type=\"submit\">保存</button></div>"
            "<div class=\"grid\">"
            "<label class=\"field\"><span>按装备品质缩放洗练上限</span>"
            "<select name=\"quality_scale_enabled\">"
            "<option value=\"1\"%s>开（Q0=1/5 … Q4=满值）</option>"
            "<option value=\"0\"%s>关（一律用属性池 value_max）</option>"
            "</select></label>"
            "<label class=\"field\"><span>首次获得随机洗练概率 %%（0–100）</span>"
            "<input name=\"first_seed_percent\" type=\"number\" min=\"0\" "
            "max=\"100\" value=\"%u\" style=\"max-width:none;width:100%%;"
            "margin-top:4px\"></label>"
            "<label class=\"field\"><span>重铸是否转移洗练属性</span>"
            "<select name=\"transfer_wash_enabled\">"
            "<option value=\"1\"%s>开（源洗练随强化转到目标，源清空）</option>"
            "<option value=\"0\"%s>关（仅转强化等级，源与目标各自保留洗练）</option>"
            "</select></label>"
            "<label class=\"field\"><span>跨品质重铸是否需要跨品重铸石</span>"
            "<select name=\"cross_quality_stone_required\">"
            "<option value=\"1\"%s>需要（无石不可跨品质，确认时扣1个）</option>"
            "<option value=\"0\"%s>不需要（同部位即可跨品质，不扣石）</option>"
            "</select></label>"
            "<label class=\"field\"><span>重铸是否消耗玄晶</span>"
            "<select name=\"transfer_crystal_enabled\">"
            "<option value=\"1\"%s>开（+N 扣1个(N-1)级玄晶；+1免费）</option>"
            "<option value=\"0\"%s>关（不扣玄晶）</option>"
            "</select></label>"
            "<label class=\"field\"><span>重铸是否消耗金币</span>"
            "<select name=\"transfer_money_enabled\">"
            "<option value=\"1\"%s>开（固定 48金65银）</option>"
            "<option value=\"0\"%s>关（不扣金币）</option>"
            "</select></label>"
            "</div>"
            "<p class=\"muted\" style=\"margin:12px 0 0\">"
            "质量分档开启时：首获 seed / 玩家洗练 / 29/7 pool 上限为 "
            "<code>value_max × (品质+1) / 5</code>。"
            "首获概率仅作用于新掉落/商店等首次入包（不重随交易穿脱）。"
            "0=永不随机，100=必定随机四档（wash_count 仍为 0；"
            "Pixi 装备名不再标「洗」）。"
            "重铸转移洗练：影响 29/6；关时只转强化等级，两侧洗练各自保留。"
            "跨品重铸石：影响 29/5|6 不同品质；关时仍限同部位，但不检查/不消耗 855。"
            "玄晶/金币消耗可独立开关，可同时开启。"
            "表：<code>server_enhance_wash_settings</code>。"
            "</p></form></section>",
            qualityScaleOn ? " selected" : "",
            qualityScaleOn ? "" : " selected", (u32)firstSeedPercent,
            transferWashOn ? " selected" : "",
            transferWashOn ? "" : " selected",
            crossStoneOn ? " selected" : "",
            crossStoneOn ? "" : " selected",
            transferCrystalOn ? " selected" : "",
            transferCrystalOn ? "" : " selected",
            transferMoneyOn ? " selected" : "",
            transferMoneyOn ? "" : " selected");
    }

    {
        u32 goldPerStep = vm_net_mock_enhance_gold_per_step();
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\">"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" "
            "value=\"enhance-gold-cost-save\">"
            "<div class=\"toolbar\"><h2 style=\"margin:0\">强化消耗金币</h2>"
            "<button type=\"submit\">保存</button></div>"
            "<p class=\"muted\" style=\"margin:0 0 12px\">"
            "装备强化（29/2 预览、29/3 确认）扣费："
            "<code>(当前强化等级+1) × 每档金币</code>。"
            "默认每档 5 金：+0→+1 扣 5 金，+15→+16 扣 80 金。"
            "填 0 则不扣金币。范围 0–10000。洗练/重铸费用不受此项影响。"
            "表：<code>server_enhance_wash_settings.enhance_gold_per_step</code>。"
            "</p>"
            "<div class=\"grid\">"
            "<label class=\"field\"><span>每档金币（默认 5）</span>"
            "<input name=\"enhance_gold_per_step\" type=\"number\" min=\"0\" "
            "max=\"10000\" value=\"%u\" style=\"max-width:none;width:100%%;"
            "margin-top:4px\"></label>"
            "</div></form></section>",
            goldPerStep);
    }

    {
        const vm_net_mock_enhance_base_scale *sc =
            vm_net_mock_enhance_base_scale_get();
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\">"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" "
            "value=\"enhance-base-scale-save\">"
            "<div class=\"toolbar\"><h2 style=\"margin:0\">强化基础缩放</h2>"
            "<button type=\"submit\">保存</button></div>"
            "<p class=\"muted\" style=\"margin:0 0 12px\">"
            "满量公式 <code>M(L)=基值+基值×(每级%%×L + 里程碑附加%%×⌊L/4⌋)/100</code>。"
            "各属性取增量的应用%%：100=满量，50=增量减半，0=不随强化放大。上限 1000。"
            "表：<code>server_enhance_base_scale</code>。"
            "</p>"
            "<div class=\"grid\">"
            "<label class=\"field\"><span>每级加成 %%（默认 10）</span>"
            "<input name=\"step_percent\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>+4 档额外 %%（默认 35）</span>"
            "<input name=\"tier_extra_percent\" type=\"number\" min=\"0\" "
            "max=\"1000\" value=\"%u\" style=\"max-width:none;width:100%%;"
            "margin-top:4px\"></label>"
            "<label class=\"field\"><span>物攻应用 %%（默认 100）</span>"
            "<input name=\"apply_attack\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>护甲应用 %%（默认 50）</span>"
            "<input name=\"apply_armor\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>力量应用 %%（默认 50）</span>"
            "<input name=\"apply_strength\" type=\"number\" min=\"0\" "
            "max=\"1000\" value=\"%u\" style=\"max-width:none;width:100%%;"
            "margin-top:4px\"></label>"
            "<label class=\"field\"><span>敏捷应用 %%（默认 50）</span>"
            "<input name=\"apply_agility\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>智慧应用 %%（默认 50）</span>"
            "<input name=\"apply_wisdom\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>气血应用 %%（默认 50）</span>"
            "<input name=\"apply_hp\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>法力应用 %%（默认 50）</span>"
            "<input name=\"apply_mp\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>躲闪应用 %%（默认 0）</span>"
            "<input name=\"apply_dodge\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>命中应用 %%（默认 0）</span>"
            "<input name=\"apply_hit\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>暴击应用 %%（默认 0）</span>"
            "<input name=\"apply_crit\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "<label class=\"field\"><span>抗性应用 %%（默认 0）</span>"
            "<input name=\"apply_resist\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\" style=\"max-width:none;width:100%%;margin-top:4px\">"
            "</label>"
            "</div></form></section>",
            sc ? sc->stepPercent : 10u, sc ? sc->tierExtraPercent : 35u,
            sc ? sc->applyAttack : 100u, sc ? sc->applyArmor : 50u,
            sc ? sc->applyStrength : 50u, sc ? sc->applyAgility : 50u,
            sc ? sc->applyWisdom : 50u, sc ? sc->applyHp : 50u,
            sc ? sc->applyMp : 50u, sc ? sc->applyDodge : 0u,
            sc ? sc->applyHit : 0u, sc ? sc->applyCrit : 0u,
            sc ? sc->applyResist : 0u);
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
            "<section class=\"card\">"
            "<form method=\"post\" action=\"/action\" id=\"enhance-milestone-form\">"
            "<input type=\"hidden\" name=\"action\" value=\"enhance-milestone-save-all\">"
            "<input type=\"hidden\" name=\"row_count\" value=\"%u\">"
            "<div class=\"toolbar\"><h2 style=\"margin:0\">默认档词条（未洗练）</h2>"
            "<button type=\"submit\">一键保存全部</button></div>"
            "<table><thead><tr><th>部位</th><th>档次</th><th>属性</th>"
            "<th>%%</th><th>Q0</th><th>Q1</th><th>Q2</th><th>Q3</th><th>Q4</th>"
            "<th>启用</th></tr></thead><tbody>",
            milestoneCount);
        for (i = 0; i < milestoneCount; ++i)
        {
            const vm_net_mock_enhance_milestone_cfg *row =
                vm_net_mock_enhance_milestone_admin_row_at(i);
            if (row == NULL)
                continue;
            vm_mock_admin_text_appendf(
                &page,
                "<tr>"
                "<td>%s"
                "<input type=\"hidden\" name=\"slot_%u\" value=\"%u\">"
                "<input type=\"hidden\" name=\"unlock_%u\" value=\"%u\"></td>"
                "<td>+%u</td>"
                "<td><select name=\"attr_type_%u\">",
                vm_mock_admin_enhance_slot_name(row->equipSlot), i,
                row->equipSlot, i, row->unlockLevel, row->unlockLevel, i);
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
                "<td><select name=\"flag_%u\">"
                "<option value=\"0\"%s>值</option>"
                "<option value=\"1\"%s>%%</option></select></td>"
                "<td><input name=\"value_q0_%u\" type=\"number\" min=\"0\" "
                "max=\"65535\" value=\"%u\"></td>"
                "<td><input name=\"value_q1_%u\" type=\"number\" min=\"0\" "
                "max=\"65535\" value=\"%u\"></td>"
                "<td><input name=\"value_q2_%u\" type=\"number\" min=\"0\" "
                "max=\"65535\" value=\"%u\"></td>"
                "<td><input name=\"value_q3_%u\" type=\"number\" min=\"0\" "
                "max=\"65535\" value=\"%u\"></td>"
                "<td><input name=\"value_q4_%u\" type=\"number\" min=\"0\" "
                "max=\"65535\" value=\"%u\"></td>"
                "<td><select name=\"enabled_%u\">"
                "<option value=\"1\"%s>是</option>"
                "<option value=\"0\"%s>否</option></select></td>"
                "</tr>",
                i, row->flag == 0 ? " selected" : "",
                row->flag != 0 ? " selected" : "", i, row->valueQ[0], i,
                row->valueQ[1], i, row->valueQ[2], i, row->valueQ[3], i,
                row->valueQ[4], i, row->enabled ? " selected" : "",
                !row->enabled ? " selected" : "");
        }
        vm_mock_admin_text_appendf(
            &page,
            "</tbody></table>"
            "<div class=\"toolbar\" style=\"margin-top:14px\">"
            "<p class=\"muted\" style=\"margin:0\">表：MySQL "
            "<code>server_enhance_milestones</code>。"
            "Q0..Q4 为装备品质 0..4 的默认档数值（未洗练）；"
            "武器 +12 气血 / +16 三围等特例直接改对应行。"
            "改完点「一键保存全部」立即生效。</p>"
            "<button type=\"submit\">一键保存全部</button></div>"
            "</form></section>");
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
            "实际上限是否按品质分档见上方「洗练质量分档」开关。"
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

    if (strcmp(action, "enhance-base-scale-save") == 0)
    {
        vm_net_mock_enhance_base_scale cfg;
        const vm_net_mock_enhance_base_scale *cur =
            vm_net_mock_enhance_base_scale_get();
        char okMsg[160];

        if (cur == NULL)
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "强化基础缩放未加载");
            return;
        }
        cfg = *cur;
        if (!vm_mock_admin_form_u32_or_default(body, "step_percent", 1000u,
                                               cfg.stepPercent,
                                               &cfg.stepPercent) ||
            !vm_mock_admin_form_u32_or_default(body, "tier_extra_percent",
                                               1000u, cfg.tierExtraPercent,
                                               &cfg.tierExtraPercent) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_attack", 1000u,
                                               cfg.applyAttack,
                                               &cfg.applyAttack) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_armor", 1000u,
                                               cfg.applyArmor,
                                               &cfg.applyArmor) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_strength", 1000u,
                                               cfg.applyStrength,
                                               &cfg.applyStrength) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_agility", 1000u,
                                               cfg.applyAgility,
                                               &cfg.applyAgility) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_wisdom", 1000u,
                                               cfg.applyWisdom,
                                               &cfg.applyWisdom) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_hp", 1000u,
                                               cfg.applyHp, &cfg.applyHp) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_mp", 1000u,
                                               cfg.applyMp, &cfg.applyMp) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_dodge", 1000u,
                                               cfg.applyDodge,
                                               &cfg.applyDodge) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_hit", 1000u,
                                               cfg.applyHit, &cfg.applyHit) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_crit", 1000u,
                                               cfg.applyCrit,
                                               &cfg.applyCrit) ||
            !vm_mock_admin_form_u32_or_default(body, "apply_resist", 1000u,
                                               cfg.applyResist,
                                               &cfg.applyResist))
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "强化基础缩放字段无效（0–1000）");
            return;
        }
        if (!vm_net_mock_enhance_base_scale_admin_save(&cfg))
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "保存强化基础缩放失败");
            return;
        }
        snprintf(okMsg, sizeof(okMsg),
                 "基础缩放已保存（每级 %u%%，档外 +%u%%；物攻 %u%% / 护甲 %u%%）",
                 cfg.stepPercent, cfg.tierExtraPercent, cfg.applyAttack,
                 cfg.applyArmor);
        vm_mock_admin_redirect_enhance(client, "milestones", "ok", okMsg);
        return;
    }

    if (strcmp(action, "enhance-wash-settings-save") == 0 ||
        strcmp(action, "enhance-wash-quality-scale-save") == 0)
    {
        u32 qualityScale = 1;
        u32 firstSeedPercent = 20;
        u32 transferWash = 1;
        u32 crossStone = 1;
        u32 transferCrystal = 1;
        u32 transferMoney = 0;
        char okMsg[220];

        if (!vm_mock_admin_form_u32(body, "quality_scale_enabled", 1u,
                                    &qualityScale))
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "质量分档开关无效");
            return;
        }
        if (!vm_mock_admin_form_u32(body, "first_seed_percent", 100u,
                                    &firstSeedPercent))
        {
            /* Older form without the field: keep current runtime value. */
            firstSeedPercent = vm_net_mock_enhance_wash_first_seed_percent();
        }
        if (!vm_mock_admin_form_u32(body, "transfer_wash_enabled", 1u,
                                    &transferWash))
        {
            transferWash = vm_net_mock_enhance_wash_transfer_enabled();
        }
        if (!vm_mock_admin_form_u32(body, "cross_quality_stone_required", 1u,
                                    &crossStone))
        {
            crossStone = vm_net_mock_enhance_cross_quality_stone_required();
        }
        if (!vm_mock_admin_form_u32(body, "transfer_crystal_enabled", 1u,
                                    &transferCrystal))
        {
            transferCrystal = vm_net_mock_enhance_transfer_crystal_enabled();
        }
        if (!vm_mock_admin_form_u32(body, "transfer_money_enabled", 1u,
                                    &transferMoney))
        {
            transferMoney = vm_net_mock_enhance_transfer_money_enabled();
        }
        if (!vm_net_mock_enhance_wash_settings_admin_save(
                (u8)qualityScale, (u8)firstSeedPercent, (u8)transferWash,
                (u8)crossStone, (u8)transferCrystal, (u8)transferMoney))
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "保存洗练规则失败");
            return;
        }
        snprintf(okMsg, sizeof(okMsg),
                 "规则已保存（质量分档%s，首获 %u%%，转移洗练%s，跨品需石%s，"
                 "扣玄晶%s，扣金币%s）",
                 qualityScale ? "开" : "关", firstSeedPercent,
                 transferWash ? "开" : "关", crossStone ? "是" : "否",
                 transferCrystal ? "开" : "关", transferMoney ? "开" : "关");
        vm_mock_admin_redirect_enhance(client, "milestones", "ok", okMsg);
        return;
    }

    if (strcmp(action, "enhance-gold-cost-save") == 0)
    {
        u32 goldPerStep = 5;
        char okMsg[160];

        if (!vm_mock_admin_form_u32(body, "enhance_gold_per_step", 10000u,
                                    &goldPerStep))
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "强化金币字段无效（0–10000）");
            return;
        }
        if (!vm_net_mock_enhance_gold_per_step_admin_save(goldPerStep))
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "保存强化金币失败");
            return;
        }
        snprintf(okMsg, sizeof(okMsg),
                 "强化金币已保存（每档 %u 金；+0→+1=%u 金，+15→+16=%u 金）",
                 goldPerStep, goldPerStep, goldPerStep * 16u);
        vm_mock_admin_redirect_enhance(client, "milestones", "ok", okMsg);
        return;
    }

    if (strcmp(action, "enhance-milestone-upsert") == 0 ||
        strcmp(action, "enhance-milestone-save-all") == 0)
    {
        char qText[5][16];
        u32 qv[5] = {0, 0, 0, 0, 0};
        u16 valueQ[5];
        u8 qi;

        if (strcmp(action, "enhance-milestone-save-all") == 0)
        {
            vm_net_mock_enhance_milestone_cfg rows[VM_NET_MOCK_ENHANCE_MILESTONE_CFG_MAX];
            u32 saved = 0;

            memset(rows, 0, sizeof(rows));
            (void)vm_mock_admin_form_value(body, "row_count", countText,
                                           sizeof(countText));
            if (!vm_net_mock_parse_u32_strict(countText, &rowCount) ||
                rowCount == 0 ||
                rowCount > VM_NET_MOCK_ENHANCE_MILESTONE_CFG_MAX)
            {
                vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                               "默认档行数无效");
                return;
            }
            for (i = 0; i < rowCount; ++i)
            {
                snprintf(key, sizeof(key), "slot_%u", i);
                memset(slotText, 0, sizeof(slotText));
                if (!vm_mock_admin_form_value(body, key, slotText,
                                               sizeof(slotText)))
                {
                    vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                                   "缺少部位字段");
                    return;
                }
                snprintf(key, sizeof(key), "unlock_%u", i);
                memset(unlockText, 0, sizeof(unlockText));
                (void)vm_mock_admin_form_value(body, key, unlockText,
                                               sizeof(unlockText));
                snprintf(key, sizeof(key), "attr_type_%u", i);
                memset(typeText, 0, sizeof(typeText));
                (void)vm_mock_admin_form_value(body, key, typeText,
                                               sizeof(typeText));
                snprintf(key, sizeof(key), "flag_%u", i);
                memset(flagText, 0, sizeof(flagText));
                (void)vm_mock_admin_form_value(body, key, flagText,
                                               sizeof(flagText));
                snprintf(key, sizeof(key), "enabled_%u", i);
                memset(enabledText, 0, sizeof(enabledText));
                (void)vm_mock_admin_form_value(body, key, enabledText,
                                               sizeof(enabledText));
                for (qi = 0; qi < 5; ++qi)
                {
                    snprintf(key, sizeof(key), "value_q%u_%u", qi, i);
                    memset(qText[qi], 0, sizeof(qText[qi]));
                    (void)vm_mock_admin_form_value(body, key, qText[qi],
                                                   sizeof(qText[qi]));
                }
                if (!vm_net_mock_parse_u32_strict(slotText, &slot) ||
                    !vm_net_mock_parse_u32_strict(unlockText, &unlock) ||
                    !vm_net_mock_parse_u32_strict(typeText, &type) ||
                    !vm_net_mock_parse_u32_strict(flagText, &flag) ||
                    !vm_net_mock_parse_u32_strict(enabledText, &enabled) ||
                    type < 1 || type > 10 || flag > 1)
                {
                    vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                                   "默认档字段无效");
                    return;
                }
                for (qi = 0; qi < 5; ++qi)
                {
                    if (!vm_net_mock_parse_u32_strict(qText[qi], &qv[qi]) ||
                        qv[qi] > 65535u)
                    {
                        vm_mock_admin_redirect_enhance(
                            client, "milestones", "error", "默认档数值无效");
                        return;
                    }
                    valueQ[qi] = (u16)qv[qi];
                }
                rows[saved].equipSlot = (u8)slot;
                rows[saved].unlockLevel = (u8)unlock;
                rows[saved].attrType = (u8)type;
                rows[saved].flag = (u8)flag;
                rows[saved].enabled = enabled != 0 ? 1 : 0;
                for (qi = 0; qi < 5; ++qi)
                    rows[saved].valueQ[qi] = valueQ[qi];
                ++saved;
            }
            if (!vm_net_mock_enhance_milestone_admin_save_all(rows, saved))
            {
                vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                               "一键保存默认档失败");
                return;
            }
            {
                char okMsg[96];
                snprintf(okMsg, sizeof(okMsg), "已保存全部 %u 条默认档", saved);
                vm_mock_admin_redirect_enhance(client, "milestones", "ok",
                                               okMsg);
            }
            return;
        }

        (void)vm_mock_admin_form_value(body, "slot", slotText, sizeof(slotText));
        (void)vm_mock_admin_form_value(body, "unlock", unlockText,
                                       sizeof(unlockText));
        (void)vm_mock_admin_form_value(body, "attr_type", typeText,
                                       sizeof(typeText));
        (void)vm_mock_admin_form_value(body, "flag", flagText, sizeof(flagText));
        (void)vm_mock_admin_form_value(body, "enabled", enabledText,
                                       sizeof(enabledText));
        for (qi = 0; qi < 5; ++qi)
        {
            snprintf(key, sizeof(key), "value_q%u", qi);
            memset(qText[qi], 0, sizeof(qText[qi]));
            (void)vm_mock_admin_form_value(body, key, qText[qi],
                                           sizeof(qText[qi]));
        }
        if (!vm_net_mock_parse_u32_strict(slotText, &slot) ||
            !vm_net_mock_parse_u32_strict(unlockText, &unlock) ||
            !vm_net_mock_parse_u32_strict(typeText, &type) ||
            !vm_net_mock_parse_u32_strict(flagText, &flag) ||
            !vm_net_mock_parse_u32_strict(enabledText, &enabled))
        {
            vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                           "保存默认档词条失败");
            return;
        }
        for (qi = 0; qi < 5; ++qi)
        {
            if (!vm_net_mock_parse_u32_strict(qText[qi], &qv[qi]) ||
                qv[qi] > 65535u)
            {
                vm_mock_admin_redirect_enhance(client, "milestones", "error",
                                               "默认档数值无效");
                return;
            }
            valueQ[qi] = (u16)qv[qi];
        }
        if (!vm_net_mock_enhance_milestone_admin_upsert(
                (u8)slot, (u8)unlock, (u8)type, (u8)flag, (u8)enabled, valueQ))
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
