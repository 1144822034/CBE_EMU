/* Admin UI for 比武擂台 — included from web_admin_server.c. */

static void vm_mock_admin_redirect_arena(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=arena&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}


static void vm_mock_admin_arena_append_hour_options(vm_mock_admin_text *page,
                                                   u32 selectedHour)
{
    u32 h;
    for (h = 0; h < 24; ++h)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%02u</option>", h,
            h == selectedHour ? " selected" : "", h);
    }
}

static void vm_mock_admin_arena_append_minute_options(vm_mock_admin_text *page,
                                                     u32 selectedMinute)
{
    u32 m;
    for (m = 0; m < 60; ++m)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%02u</option>", m,
            m == selectedMinute ? " selected" : "", m);
    }
}

static void vm_mock_admin_arena_append_hm_select(vm_mock_admin_text *page,
                                                const char *label,
                                                const char *nameHour,
                                                const char *nameMinute,
                                                u32 minuteOfDay)
{
    u32 hour = minuteOfDay / 60u;
    u32 minute = minuteOfDay % 60u;

    if (hour > 23u)
        hour = 23u;
    if (page == NULL || nameHour == NULL || nameMinute == NULL)
        return;
    vm_mock_admin_text_appendf(
        page,
        "<div><label>%s</label>"
        "<div class=\"hm\">"
        "<select name=\"%s\">",
        label ? label : "", nameHour);
    vm_mock_admin_arena_append_hour_options(page, hour);
    vm_mock_admin_text_appendf(
        page, "</select><span>:</span><select name=\"%s\">", nameMinute);
    vm_mock_admin_arena_append_minute_options(page, minute);
    vm_mock_admin_text_appendf(page, "</select></div></div>");
}

static u32 vm_mock_admin_arena_form_hm(const char *body, const char *nameHour,
                                       const char *nameMinute, u32 fallback)
{
    u32 hour = fallback / 60u;
    u32 minute = fallback % 60u;

    (void)vm_mock_admin_form_u32(body, nameHour, 23u, &hour);
    (void)vm_mock_admin_form_u32(body, nameMinute, 59u, &minute);
    if (hour > 23u)
        hour = 23u;
    if (minute > 59u)
        minute = 59u;
    return hour * 60u + minute;
}

static u32 vm_mock_admin_arena_form_weekday_mask(const char *body, u32 fallback)
{
    u32 mask = 0;
    u32 i;
    char key[16];
    char value[16];
    bool any = false;

    for (i = 0; i < 7; ++i)
    {
        snprintf(key, sizeof(key), "weekday_%u", i);
        memset(value, 0, sizeof(value));
        if (vm_mock_admin_form_value(body, key, value, sizeof(value)) &&
            value[0] != 0 && strcmp(value, "0") != 0)
        {
            mask |= (1u << i);
            any = true;
        }
    }
    return any ? mask : fallback;
}

static void vm_mock_admin_render_arena_page(char *response, size_t responseCap,
                                            const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_mock_arena_config *cfg;
    u32 rewardCount;
    u32 i;
    char sceneUtf8[128];
    char kwUtf8[64];

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    cfg = vm_net_mock_arena_config_get();
    rewardCount = vm_net_mock_arena_reward_admin_count();
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(kwUtf8, 0, sizeof(kwUtf8));
    if (cfg)
    {
        vm_net_mock_gbk_label_to_utf8(cfg->scene, sceneUtf8, sizeof(sceneUtf8));
        vm_net_mock_gbk_label_to_utf8(cfg->npcNameKeyword, kwUtf8, sizeof(kwUtf8));
    }

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 比武</title><style>"
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
        "label{display:block;font-size:12px;color:#667085;margin:0 0 4px}"
        ".grid2{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}"
        "input,select{border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;"
        "background:#fff;width:100%%}.hm{display:flex;align-items:center;gap:6px}.hm select{width:auto;min-width:64px}.hm span{color:#667085;font-weight:600}.days{display:flex;flex-wrap:wrap;gap:8px 12px;padding:8px 0}.days label{display:inline-flex;align-items:center;gap:4px;margin:0;font-size:13px;color:#344054;font-weight:500}.days input{width:auto;margin:0}button{border:0;border-radius:6px;padding:8px 12px;"
        "background:#175cd3;color:#fff;cursor:pointer;margin-top:12px}"
        "table{border-collapse:collapse;width:100%%}th,td{text-align:left;"
        "padding:8px;border-bottom:1px solid #eaecf0}"
        ".muted{color:#98a2b3}</style></head><body><main class=\"wrap\">"
        "<header><h1>比武管理</h1>"
        "<p class=\"sub\">配置比武擂台日程、计分与排行奖励；赏金擂台（押金对决）已接入；下方可开关。</p></header>"
        "<nav class=\"tabs\">"
        "<a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱奖励</a>"
        "<a class=\"tab\" href=\"/?tab=enhance\">强化配置</a>"
        "<a class=\"tab\" href=\"/?tab=combat\">战斗公式</a>"
        "<a class=\"tab on\" href=\"/?tab=arena\">比武</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "</nav>");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(
            &page, "<div class=\"notice %s\">%s</div>",
            strcmp(status, "ok") == 0 ? "ok" : "error", message);
    }

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>运行状态</h2>"
        "<p>session=%u status=%u 参与者=%u</p>"
        "<p class=\"muted\">status: 0空闲 1进场 2比赛 3已结算</p></section>",
        vm_net_mock_arena_runtime_session_id(),
        (u32)vm_net_mock_arena_runtime_status(),
        vm_net_mock_arena_runtime_participant_count());

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>活动配置</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-arena-config\">"
        "<div class=\"grid2\">"
        "<div><label>启用</label><select name=\"enabled\">"
        "<option value=\"1\"%s>开启</option><option value=\"0\"%s>关闭</option>"
        "</select></div>"
        "<div><label>场景名(GBK文件名)</label>"
        "<input name=\"scene\" value=\"%s\"></div>"
        "<div><label>出生点X</label><input name=\"spawn_x\" value=\"%u\"></div>"
        "<div><label>出生点Y</label><input name=\"spawn_y\" value=\"%u\"></div>"
        "</div>",
        cfg && cfg->enabled ? " selected" : "",
        cfg && !cfg->enabled ? " selected" : "",
        sceneUtf8[0] ? sceneUtf8 : "c04比武擂台",
        cfg ? cfg->spawnX : 120,
        cfg ? cfg->spawnY : 120);

    {
        static const char *const weekdayNames[7] = {
            "周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        u32 wdMask = cfg ? cfg->weekdayMask : 72u;
        u32 wd;
        vm_mock_admin_text_appendf(
            &page, "<div style=\"margin:12px 0 4px\"><label>活动星期</label>"
                   "<div class=\"days\">");
        for (wd = 0; wd < 7; ++wd)
        {
            vm_mock_admin_text_appendf(
                &page,
                "<label><input type=\"checkbox\" name=\"weekday_%u\" value=\"1\"%s> "
                "%s</label>",
                wd, (wdMask & (1u << wd)) != 0 ? " checked" : "",
                weekdayNames[wd]);
        }
        vm_mock_admin_text_appendf(&page, "</div></div>");
    }

    vm_mock_admin_text_appendf(&page, "<div class=\"grid2\">");
    vm_mock_admin_arena_append_hm_select(
        &page, "进场开始", "entry_start_h", "entry_start_m",
        cfg ? cfg->entryStartMinute : 1220u);
    vm_mock_admin_arena_append_hm_select(
        &page, "进场结束(不含)", "entry_end_h", "entry_end_m",
        cfg ? cfg->entryEndMinute : 1229u);
    vm_mock_admin_arena_append_hm_select(
        &page, "比赛开始", "match_start_h", "match_start_m",
        cfg ? cfg->matchStartMinute : 1230u);
    vm_mock_admin_arena_append_hm_select(
        &page, "比赛结束", "match_end_h", "match_end_m",
        cfg ? cfg->matchEndMinute : 1260u);
    vm_mock_admin_text_appendf(
        &page,
        "<div><label>回合秒数</label>"
        "<input name=\"round_seconds\" value=\"%u\"></div>"
        "<div><label>战斗超时秒</label>"
        "<input name=\"battle_timeout\" value=\"%u\"></div>"
        "<div><label>胜分</label><input name=\"score_win\" value=\"%u\"></div>"
        "<div><label>轮空分</label><input name=\"score_bye\" value=\"%u\"></div>"
        "<div><label>平局分</label><input name=\"score_draw\" value=\"%u\"></div>"
        "<div><label>NPC名关键字</label>"
        "<input name=\"npc_keyword\" value=\"%s\"></div>"
        "</div><button type=\"submit\">保存配置</button></form>"
        "<p class=\"muted\">星期可多选。时间用时:分选择；进场结束为不含该时刻"
        "（例如结束选 20:29 表示最晚可进到 20:28）。默认周三/周六，"
        "进场 20:20–20:29，比赛 20:30–21:00。</p></section>",
        cfg ? cfg->roundSeconds : 300,
        cfg ? cfg->battleTimeoutSeconds : 240,
        cfg ? cfg->scoreWin : 20,
        cfg ? cfg->scoreBye : 20,
        cfg ? cfg->scoreDraw : 10,
        kwUtf8[0] ? kwUtf8 : "比武");


    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>排行奖励 (职业×段位×名次1-5)</h2>"
        "<form method=\"post\" action=\"/action\" class=\"grid2\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-arena-reward\">"
        "<div><label>职业(1天机2幻剑3鬼道)</label>"
        "<input name=\"job\" value=\"1\"></div>"
        "<div><label>段位(10=10-49 / 50=50-69 / 70=70+)</label>"
        "<input name=\"bracket\" value=\"10\"></div>"
        "<div><label>名次(1-5)</label><input name=\"rank_pos\" value=\"1\"></div>"
        "<div><label>经验</label><input name=\"reward_exp\" value=\"0\"></div>"
        "<div><label>铜币</label><input name=\"reward_money\" value=\"0\"></div>"
        "<div><label>酷宝</label><input name=\"reward_wcoin\" value=\"0\"></div>"
        "<div><label>物品ID</label><input name=\"item_id\" value=\"0\"></div>"
        "<div><label>物品数量</label><input name=\"item_count\" value=\"0\"></div>"
        "<div><label>装备ID</label><input name=\"equip_id\" value=\"0\"></div>"
        "<div><button type=\"submit\">保存奖励行</button></div></form>"
        "<table><thead><tr><th>职业</th><th>段位</th><th>名次</th>"
        "<th>经验</th><th>铜币</th><th>酷宝</th><th>物品</th><th>装备</th>"
        "</tr></thead><tbody>");

    for (i = 0; i < rewardCount; ++i)
    {
        const vm_mock_arena_reward *r = vm_net_mock_arena_reward_admin_at(i);
        char itemCell[96];
        char equipCell[96];
        char nameUtf8[64];
        const vm_net_mock_shop_catalog_item *shop;

        if (r == NULL)
            continue;
        snprintf(itemCell, sizeof(itemCell), "-");
        snprintf(equipCell, sizeof(equipCell), "-");
        if (r->itemId != 0 && r->itemCount != 0)
        {
            nameUtf8[0] = 0;
            shop = vm_net_mock_find_shop_catalog_item(r->itemId);
            if (shop != NULL && shop->name[0] != 0)
                vm_net_mock_gbk_label_to_utf8(shop->name, nameUtf8,
                                              sizeof(nameUtf8));
            if (nameUtf8[0] == 0)
                snprintf(nameUtf8, sizeof(nameUtf8), "#%u", r->itemId);
            snprintf(itemCell, sizeof(itemCell), "%s×%u", nameUtf8,
                     r->itemCount);
        }
        if (r->equipId != 0)
        {
            nameUtf8[0] = 0;
            shop = vm_net_mock_find_shop_catalog_item(r->equipId);
            if (shop != NULL && shop->name[0] != 0)
                vm_net_mock_gbk_label_to_utf8(shop->name, nameUtf8,
                                              sizeof(nameUtf8));
            if (nameUtf8[0] == 0)
                snprintf(nameUtf8, sizeof(nameUtf8), "#%u", r->equipId);
            snprintf(equipCell, sizeof(equipCell), "%s×1", nameUtf8);
        }
        vm_mock_admin_text_appendf(
            &page,
            "<tr><td>%u</td><td>%u</td><td>%u</td><td>%u</td><td>%u</td>"
            "<td>%u</td><td>%s</td><td>%s</td></tr>",
            r->job, r->bracket, r->rankPos, r->rewardExp, r->rewardMoney,
            r->rewardWcoin, itemCell, equipCell);
    }
    if (rewardCount == 0)
        vm_mock_admin_text_appendf(&page,
                                    "<tr><td colspan=\"8\" class=\"muted\">暂无奖励配置</td></tr>");

    vm_mock_admin_text_appendf(
        &page,
        "</tbody></table></section>"
        "<section class=\"card\"><h2>赏金擂台</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-arena-wager\">"
        "<div class=\"grid2\">"
        "<div><label>启用押金擂台</label><select name=\"wager_enabled\">"
        "<option value=\"0\"%s>关闭</option>"
        "<option value=\"1\"%s>开启(尚未接战斗)</option></select></div>"
        "<div><label>系统抽成%%</label>"
        "<input name=\"wager_fee\" value=\"%u\"></div>"
        "<div><label>货币(0铜币)</label>"
        "<input name=\"wager_currency\" value=\"%u\"></div></div>"
        "<button type=\"submit\">保存赏金配置</button></form>"
        "<p class=\"muted\">玩家在含关键字NPC打开赏金擂台：摆擂扣全队押金；等人数挑战队压同额后，擂主方全员确认开战。1v1直接决斗；2v2/3v3按座位逐对决斗、胜场多者获胜；胜方均分奖池（扣系统抽成）；平局全额退押。</p>"
        "</section></main></body></html>",
        cfg && !cfg->wagerEnabled ? " selected" : "",
        cfg && cfg->wagerEnabled ? " selected" : "",
        cfg ? cfg->wagerFeePercent : 10,
        cfg ? cfg->wagerCurrency : 0);
}

static void vm_mock_admin_handle_arena_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body)
{
    char text[64];
    u32 enabled = 1;
    u32 spawnX = 120;
    u32 spawnY = 120;
    u32 weekdayMask = 72;
    u32 entryStart = 1220;
    u32 entryEnd = 1229;
    u32 matchStart = 1230;
    u32 matchEnd = 1260;
    u32 roundSec = 300;
    u32 battleTimeout = 240;
    u32 scoreWin = 20;
    u32 scoreBye = 20;
    u32 scoreDraw = 10;
    u32 wagerEnabled = 0;
    u32 wagerFee = 10;
    u32 wagerCurrency = 0;
    char sceneUtf8[128];
    char kwUtf8[64];
    char sceneGbk[64];
    char kwGbk[32];
    const vm_mock_arena_config *cfg;

    if (strcmp(action, "save-arena-config") == 0)
    {
        memset(sceneUtf8, 0, sizeof(sceneUtf8));
        memset(kwUtf8, 0, sizeof(kwUtf8));
        memset(sceneGbk, 0, sizeof(sceneGbk));
        memset(kwGbk, 0, sizeof(kwGbk));
        (void)vm_mock_admin_form_value(body, "enabled", text, sizeof(text));
        (void)vm_net_mock_parse_u32_strict(text, &enabled);
        (void)vm_mock_admin_form_value(body, "scene", sceneUtf8, sizeof(sceneUtf8));
        (void)vm_mock_admin_form_value(body, "npc_keyword", kwUtf8, sizeof(kwUtf8));
        (void)vm_mock_admin_form_u32(body, "spawn_x", 0xffffu, &spawnX);
        (void)vm_mock_admin_form_u32(body, "spawn_y", 0xffffu, &spawnY);
        weekdayMask = vm_mock_admin_arena_form_weekday_mask(body, weekdayMask);
        entryStart = vm_mock_admin_arena_form_hm(body, "entry_start_h",
                                                "entry_start_m", entryStart);
        entryEnd = vm_mock_admin_arena_form_hm(body, "entry_end_h", "entry_end_m",
                                              entryEnd);
        matchStart = vm_mock_admin_arena_form_hm(body, "match_start_h",
                                                "match_start_m", matchStart);
        matchEnd = vm_mock_admin_arena_form_hm(body, "match_end_h", "match_end_m",
                                              matchEnd);
        if (weekdayMask == 0)
            weekdayMask = 72;
        if (entryEnd > 24u * 60u)
            entryEnd = 24u * 60u;
        if (matchEnd > 24u * 60u)
            matchEnd = 24u * 60u;
        (void)vm_mock_admin_form_u32(body, "round_seconds", 3600u, &roundSec);
        (void)vm_mock_admin_form_u32(body, "battle_timeout", 3600u, &battleTimeout);
        (void)vm_mock_admin_form_u32(body, "score_win", 1000u, &scoreWin);
        (void)vm_mock_admin_form_u32(body, "score_bye", 1000u, &scoreBye);
        (void)vm_mock_admin_form_u32(body, "score_draw", 1000u, &scoreDraw);
        if (sceneUtf8[0] == 0)
            snprintf(sceneUtf8, sizeof(sceneUtf8), "%s", "c04比武擂台");
        if (!vm_mock_admin_utf8_to_gbk_text(sceneUtf8, sceneGbk, sizeof(sceneGbk),
                                            false))
            snprintf(sceneGbk, sizeof(sceneGbk), "%s",
                     "c04\xb1\xc8\xce\xe4\xc0\xde\xcc\xa8");
        if (kwUtf8[0] == 0)
            snprintf(kwUtf8, sizeof(kwUtf8), "%s", "比武");
        if (!vm_mock_admin_utf8_to_gbk_text(kwUtf8, kwGbk, sizeof(kwGbk), false))
            snprintf(kwGbk, sizeof(kwGbk), "%s", "\xb1\xc8\xce\xe4");
        cfg = vm_net_mock_arena_config_get();
        if (vm_net_mock_arena_admin_save_config(
                (u8)enabled, sceneGbk, (u16)spawnX, (u16)spawnY, weekdayMask,
                (u16)entryStart, (u16)entryEnd, (u16)matchStart, (u16)matchEnd,
                roundSec, battleTimeout, scoreWin, scoreBye, scoreDraw, kwGbk,
                cfg ? cfg->wagerEnabled : 0, cfg ? cfg->wagerFeePercent : 10,
                cfg ? cfg->wagerCurrency : 0))
            vm_mock_admin_redirect_arena(client, "ok", "比武配置已保存");
        else
            vm_mock_admin_redirect_arena(client, "error", "保存比武配置失败");
        return;
    }

    if (strcmp(action, "save-arena-reward") == 0)
    {
        u32 job = 1;
        u32 bracket = 10;
        u32 rankPos = 1;
        u32 exp = 0;
        u32 money = 0;
        u32 wcoin = 0;
        u32 itemId = 0;
        u32 itemCount = 0;
        u32 equipId = 0;
        (void)vm_mock_admin_form_u32(body, "job", 3u, &job);
        (void)vm_mock_admin_form_u32(body, "bracket", 70u, &bracket);
        (void)vm_mock_admin_form_u32(body, "rank_pos", 5u, &rankPos);
        (void)vm_mock_admin_form_u32(body, "reward_exp", 0xffffffffu, &exp);
        (void)vm_mock_admin_form_u32(body, "reward_money", 0xffffffffu, &money);
        (void)vm_mock_admin_form_u32(body, "reward_wcoin", 0xffffffffu, &wcoin);
        (void)vm_mock_admin_form_u32(body, "item_id", 0xffffffffu, &itemId);
        (void)vm_mock_admin_form_u32(body, "item_count", 255u, &itemCount);
        (void)vm_mock_admin_form_u32(body, "equip_id", 0xffffffffu, &equipId);
        if (vm_net_mock_arena_admin_save_reward((u8)job, (u16)bracket, (u8)rankPos,
                                                exp, money, wcoin, itemId,
                                                itemCount, equipId))
            vm_mock_admin_redirect_arena(client, "ok", "奖励已保存");
        else
            vm_mock_admin_redirect_arena(client, "error", "保存奖励失败");
        return;
    }

    if (strcmp(action, "save-arena-wager") == 0)
    {
        cfg = vm_net_mock_arena_config_get();
        if (cfg == NULL)
        {
            vm_mock_admin_redirect_arena(client, "error", "配置未加载");
            return;
        }
        (void)vm_mock_admin_form_u32(body, "wager_enabled", 1u, &wagerEnabled);
        (void)vm_mock_admin_form_u32(body, "wager_fee", 100u, &wagerFee);
        (void)vm_mock_admin_form_u32(body, "wager_currency", 10u, &wagerCurrency);
        if (vm_net_mock_arena_admin_save_config(
                cfg->enabled, cfg->scene, cfg->spawnX, cfg->spawnY,
                cfg->weekdayMask, cfg->entryStartMinute, cfg->entryEndMinute,
                cfg->matchStartMinute, cfg->matchEndMinute, cfg->roundSeconds,
                cfg->battleTimeoutSeconds, cfg->scoreWin, cfg->scoreBye,
                cfg->scoreDraw, cfg->npcNameKeyword, (u8)wagerEnabled,
                (u8)wagerFee, (u8)wagerCurrency))
            vm_mock_admin_redirect_arena(client, "ok", "赏金擂台配置已保存");
        else
            vm_mock_admin_redirect_arena(client, "error", "保存失败");
        return;
    }

    vm_mock_admin_redirect_arena(client, "error", "未知操作");
}
