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

/* Per-day bracket: 0=rest, 10/30/40/50/60/70, 77=70大乱斗. Builds mask from non-rest days. */
static void vm_mock_admin_arena_form_weekday_brackets(const char *body,
                                                      u16 outBrackets[7],
                                                      u32 *maskOut)
{
    u32 i;
    char key[24];
    u32 value = 0;
    u32 mask = 0;

    if (outBrackets == NULL)
        return;
    for (i = 0; i < 7; ++i)
        outBrackets[i] = 0;
    if (body == NULL)
    {
        if (maskOut)
            *maskOut = 0;
        return;
    }
    for (i = 0; i < 7; ++i)
    {
        snprintf(key, sizeof(key), "bracket_wd_%u", i);
        value = 0;
        (void)vm_mock_admin_form_u32(body, key, 77u, &value);
        if (value != 10u && value != 30u && value != 40u && value != 50u &&
            value != 60u && value != 70u && value != 77u)
            value = 0;
        outBrackets[i] = (u16)value;
        if (value != 0)
            mask |= (1u << i);
    }
    if (maskOut)
        *maskOut = mask;
}

static void vm_mock_admin_arena_append_bracket_options(vm_mock_admin_text *page,
                                                       u16 selected)
{
    vm_mock_admin_text_appendf(
        page,
        "<option value=\"0\"%s>休息</option>"
        "<option value=\"10\"%s>10-29级</option>"
        "<option value=\"30\"%s>30-39级</option>"
        "<option value=\"40\"%s>40-49级</option>"
        "<option value=\"50\"%s>50-59级</option>"
        "<option value=\"60\"%s>60-69级</option>"
        "<option value=\"70\"%s>70级+(分职业)</option>"
        "<option value=\"77\"%s>70大乱斗(三职业同场)</option>",
        selected == 0 ? " selected" : "", selected == 10 ? " selected" : "",
        selected == 30 ? " selected" : "", selected == 40 ? " selected" : "",
        selected == 50 ? " selected" : "", selected == 60 ? " selected" : "",
        selected == 70 ? " selected" : "", selected == 77 ? " selected" : "");
}

static void vm_mock_admin_render_arena_page(char *response, size_t responseCap,
                                            const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_mock_arena_config *cfg;
    const vm_mock_arena_reward *editReward = NULL;
    u32 rewardCount;
    u32 i;
    u32 editJob = 0;
    u32 editBracket = 0;
    u32 editRank = 0;
    char sceneUtf8[128];
    char kwUtf8[64];

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    (void)vm_mock_admin_form_u32(query, "edit_job", 3u, &editJob);
    (void)vm_mock_admin_form_u32(query, "edit_bracket", 77u, &editBracket);
    (void)vm_mock_admin_form_u32(query, "edit_rank", 5u, &editRank);
    cfg = vm_net_mock_arena_config_get();
    rewardCount = vm_net_mock_arena_reward_admin_count();
    if (editRank != 0)
    {
        for (i = 0; i < rewardCount; ++i)
        {
            const vm_mock_arena_reward *rw = vm_net_mock_arena_reward_admin_at(i);
            if (rw != NULL && (u32)rw->job == editJob &&
                (u32)rw->bracket == editBracket && (u32)rw->rankPos == editRank)
            {
                editReward = rw;
                break;
            }
        }
    }
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
        "a.btn-edit{display:inline-block;padding:6px 10px;border-radius:6px;background:#175cd3;color:#fff;"
        "text-decoration:none;font-size:12px}a.btn-edit:hover{background:#0f4a9c}"
        "a.btn-cancel{margin-left:10px;color:#667085;font-size:13px;text-decoration:none}"
        ".muted{color:#98a2b3}</style></head><body><main class=\"wrap\">"
        "<header><h1>比武管理</h1>"
        "<p class=\"sub\">配置比武擂台日程、计分与排行奖励；赏金擂台（押金对决）已接入；下方可开关。</p></header>");
    vm_mock_admin_append_tabs(&page, "arena");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(
            &page, "<div class=\"notice %s\">%s</div>",
            strcmp(status, "ok") == 0 ? "ok" : "error", message);
    }

    {
        u16 todayBracket = 0;
        time_t now = time(NULL);
        struct tm *tmNow = localtime(&now);
        if (cfg != NULL && tmNow != NULL)
            todayBracket = cfg->weekdayBrackets[(u32)tmNow->tm_wday % 7u];
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\"><h2>运行状态</h2>"
            "<p>session=%u status=%u 参与者=%u 今日段位=%u</p>"
            "<p class=\"muted\">status: 0空闲 1进场 2比赛 3已结算；段位 0=休息 / "
            "10=10-29 / 30=30-39 / 40=40-49 / 50=50-59 / 60=60-69 / "
            "70=70分职业 / 77=70大乱斗</p></section>",
            vm_net_mock_arena_runtime_session_id(),
            (u32)vm_net_mock_arena_runtime_status(),
            vm_net_mock_arena_runtime_participant_count(), (u32)todayBracket);
    }

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>活动配置</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-arena-config\">"
        "<div class=\"grid2\">"
        "<div><label>启用</label><select name=\"enabled\">"
        "<option value=\"1\"%s>开启</option><option value=\"0\"%s>关闭</option>"
        "</select></div>"
        "<div><label>基准场景(天机用；幻剑+50/鬼道+70)</label>"
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
        static const u16 defaultBrackets[7] = {77, 70, 60, 50, 40, 30, 77};
        u32 wd;
        vm_mock_admin_text_appendf(
            &page,
            "<div style=\"margin:12px 0 4px\"><label>每日开放段位（每天仅一段）</label>"
            "<div class=\"grid2\">");
        for (wd = 0; wd < 7; ++wd)
        {
            u16 selected = cfg ? cfg->weekdayBrackets[wd] : defaultBrackets[wd];
            vm_mock_admin_text_appendf(
                &page, "<div><label>%s</label><select name=\"bracket_wd_%u\">",
                weekdayNames[wd], wd);
            vm_mock_admin_arena_append_bracket_options(&page, selected);
            vm_mock_admin_text_appendf(&page, "</select></div>");
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
        "<div><label>回合伤害加深</label><select name=\"damage_scale_enabled\">"
        "<option value=\"1\"%s>开启(每回合×2)</option>"
        "<option value=\"0\"%s>关闭</option></select></div>"
        "<div><label>是否允许回血</label><select name=\"allow_heal\">"
        "<option value=\"0\"%s>禁止</option>"
        "<option value=\"1\"%s>允许(每方每场各一次)</option></select></div>"
        "<div><label>是否允许闪躲</label><select name=\"allow_dodge\">"
        "<option value=\"1\"%s>允许(仅普攻)</option>"
        "<option value=\"0\"%s>禁止(普攻必中)</option></select></div>"
        "<div><label>神力丸/大力丸</label><select name=\"allow_combat_pill\">"
        "<option value=\"0\"%s>不生效</option>"
        "<option value=\"1\"%s>生效(+ATK%%)</option></select></div>"
        "</div><button type=\"submit\">保存配置</button></form>"
        "<p class=\"muted\">每天只开一个日程键；普通日场地按职业分流：天机→基准图，"
        "幻剑→基准50，鬼道→基准70。<b>77=周末大乱斗</b>：仅70级+，三人职业同进"
        "基准70图、互见互打，机制同70级比武（含连杀播报），奖励用职业0×段位77单独配。"
        "默认周一70/周二60-69/周三50-59/周四40-49/周五30-39/周六日大乱斗；"
        "10-29 可在后台单独排日。"
        " 进场 20:20–20:29，比赛 20:30–21:00。"
        "战斗规则默认：伤害加深开、禁回血、允许闪躲、神力丸不生效；"
        "开启回血后每方每场可各用一次回血/复活、增益、封魔、驱散；"
        "药品和壶仍固定禁用。"
        "</p></section>",
        cfg ? cfg->roundSeconds : 300,
        cfg ? cfg->battleTimeoutSeconds : 240,
        cfg ? cfg->scoreWin : 20,
        cfg ? cfg->scoreBye : 20,
        cfg ? cfg->scoreDraw : 10,
        kwUtf8[0] ? kwUtf8 : "比武",
        cfg && cfg->damageScaleEnabled ? " selected" : "",
        cfg && !cfg->damageScaleEnabled ? " selected" : "",
        cfg && !cfg->allowHeal ? " selected" : "",
        cfg && cfg->allowHeal ? " selected" : "",
        cfg && cfg->allowDodge ? " selected" : "",
        cfg && !cfg->allowDodge ? " selected" : "",
        cfg && !cfg->allowCombatPill ? " selected" : "",
        cfg && cfg->allowCombatPill ? " selected" : "");


    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>排行奖励</h2>"
        "<p class=\"muted\">普通：职业1-3 × 段位10/30/40/50/60/70 × 名次1-5。"
        "大乱斗：职业填 <b>0</b>、段位填 <b>77</b>、名次1-5（全职业共用）。</p>"
        "<table><thead><tr><th>职业</th><th>段位</th><th>名次</th>"
        "<th>经验</th><th>铜币</th><th>活力</th><th>物品</th><th>装备</th>"
        "<th></th></tr></thead><tbody>");

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
            "<td>%u</td><td>%s</td><td>%s</td>"
            "<td><a class=\"btn-edit\" href=\"" VM_MOCK_ADMIN_ROOT_PATH
            "?tab=arena&amp;edit_job=%u&amp;edit_bracket=%u&amp;edit_rank=%u"
            "#arena-reward-form\">编辑</a></td></tr>",
            r->job, r->bracket, r->rankPos, r->rewardExp, r->rewardMoney,
            r->rewardVitality, itemCell, equipCell, (u32)r->job,
            (u32)r->bracket, (u32)r->rankPos);
    }
    if (rewardCount == 0)
        vm_mock_admin_text_appendf(
            &page,
            "<tr><td colspan=\"9\" class=\"muted\">暂无奖励配置</td></tr>");

    vm_mock_admin_text_appendf(
        &page,
        "</tbody></table>"
        "<form method=\"post\" action=\"/action\" class=\"grid2\" "
        "style=\"margin-top:12px\" id=\"arena-reward-form\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-arena-reward\">");
    if (editReward != NULL)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<div><label>职业(1天机2幻剑3鬼道；大乱斗填0)</label>"
            "<input name=\"job\" value=\"%u\" readonly></div>"
            "<div><label>段位(10/30/40/50/60/70；大乱斗77)</label>"
            "<input name=\"bracket\" value=\"%u\" readonly></div>"
            "<div><label>名次(1-5)</label>"
            "<input name=\"rank_pos\" value=\"%u\" readonly></div>"
            "<div><label>经验</label><input name=\"reward_exp\" value=\"%u\"></div>"
            "<div><label>铜币</label><input name=\"reward_money\" value=\"%u\"></div>"
            "<div><label>活力</label><input name=\"reward_vitality\" value=\"%u\"></div>"
            "<div><label>物品ID</label><input name=\"item_id\" value=\"%u\"></div>"
            "<div><label>物品数量</label><input name=\"item_count\" value=\"%u\"></div>"
            "<div><label>装备ID</label><input name=\"equip_id\" value=\"%u\"></div>"
            "<div><button type=\"submit\">保存奖励</button>"
            "<a class=\"btn-cancel\" href=\"" VM_MOCK_ADMIN_ROOT_PATH
            "?tab=arena#arena-reward-form\">取消编辑</a></div>",
            (u32)editReward->job, (u32)editReward->bracket,
            (u32)editReward->rankPos, editReward->rewardExp,
            editReward->rewardMoney, editReward->rewardVitality,
            editReward->itemId, editReward->itemCount, editReward->equipId);
    }
    else
    {
        vm_mock_admin_text_appendf(
            &page,
            "<div><label>职业(1天机2幻剑3鬼道；大乱斗填0)</label>"
            "<input name=\"job\" value=\"1\"></div>"
            "<div><label>段位(10/30/40/50/60/70；大乱斗77)</label>"
            "<input name=\"bracket\" value=\"10\"></div>"
            "<div><label>名次(1-5)</label><input name=\"rank_pos\" value=\"1\"></div>"
            "<div><label>经验</label><input name=\"reward_exp\" value=\"0\"></div>"
            "<div><label>铜币</label><input name=\"reward_money\" value=\"0\"></div>"
            "<div><label>活力</label><input name=\"reward_vitality\" value=\"0\"></div>"
            "<div><label>物品ID</label><input name=\"item_id\" value=\"0\"></div>"
            "<div><label>物品数量</label><input name=\"item_count\" value=\"0\"></div>"
            "<div><label>装备ID</label><input name=\"equip_id\" value=\"0\"></div>"
            "<div><button type=\"submit\">添加/更新奖励</button></div>");
    }

    vm_mock_admin_text_appendf(
        &page,
        "</form></section>"
        "<section class=\"card\"><h2>赏金擂台</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-arena-wager\">"
        "<div class=\"grid2\">"
        "<div><label>启用押金擂台</label><select name=\"wager_enabled\">"
        "<option value=\"0\"%s>关闭</option>"
        "<option value=\"1\"%s>开启</option></select></div>"
        "<div><label>摆擂消耗活力</label>"
        "<input name=\"wager_vitality\" value=\"%u\"></div>"
        "<div><label>系统抽成%%（胜方结算）</label>"
        "<input name=\"wager_fee\" value=\"%u\"></div>"
        "<div><label>货币(0铜币)</label>"
        "<input name=\"wager_currency\" value=\"%u\"></div></div>"
        "<button type=\"submit\">保存赏金配置</button></form>"
        "<p class=\"muted\">入口挂在显示名含「擂台宗主」的 NPC；仅可在该 NPC 所在地图摆擂。"
        "摆擂扣押金并消耗活力（后台可配，撤擂只退押金不退活力）；等人数挑战队压同额后，擂主方全员确认开战。"
        "1v1直接决斗；2v2/3v3按座位逐对决斗、胜场多者获胜；胜方均分奖池（扣系统抽成）；平局全额退押。</p>"
        "</section>",
        cfg && !cfg->wagerEnabled ? " selected" : "",
        cfg && cfg->wagerEnabled ? " selected" : "",
        cfg ? cfg->wagerVitalityCost : 50,
        cfg ? cfg->wagerFeePercent : 10,
        cfg ? cfg->wagerCurrency : 0);

    /* 天下第一雕像（宣阳广场） */
    {
        const vm_mock_tianxia_config *tx = vm_mock_tianxia_config_get();
        char titleUtf8[64];
        char otherUtf8[64];
        char champUtf8[64];
        char sigUtf8[160];

        memset(titleUtf8, 0, sizeof(titleUtf8));
        memset(otherUtf8, 0, sizeof(otherUtf8));
        memset(champUtf8, 0, sizeof(champUtf8));
        memset(sigUtf8, 0, sizeof(sigUtf8));
        if (tx != NULL)
        {
            vm_net_mock_gbk_label_to_utf8(tx->titleText, titleUtf8,
                                          sizeof(titleUtf8));
            vm_net_mock_gbk_label_to_utf8(tx->optionOther, otherUtf8,
                                          sizeof(otherUtf8));
            vm_net_mock_gbk_label_to_utf8(tx->optionChamp, champUtf8,
                                          sizeof(champUtf8));
            if (tx->signature[0] != 0)
                vm_net_mock_gbk_label_to_utf8(tx->signature, sigUtf8,
                                              sizeof(sigUtf8));
        }
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\"><h2>天下第一雕像（宣阳广场）</h2>"
            "<p class=\"muted\">展示最近一次 70 大乱斗第一名，雕像面朝左。坐标/放大/文案/翅膀·动态称号·坐骑可配；"
            "特效取冠军当前装备。未装备动态称号时，显示角色当前称号。头顶文字显示在名字上方。"
            "江湖签名显示在雕像对话框主文；留空则显示默认"
            "「这个冠军很高冷,什么也没说!」。冠军本人也可在游戏内设置。Pixi 生效；改坐标后需重新进图。</p>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-tianxia-statue\">"
            "<div class=\"grid2\">"
            "<div><label>启用</label><select name=\"tx_enabled\">"
            "<option value=\"0\"%s>关闭</option>"
            "<option value=\"1\"%s>开启</option></select></div>"
            "<div><label>坐标 X</label><input name=\"tx_pos_x\" value=\"%u\"></div>"
            "<div><label>坐标 Y</label><input name=\"tx_pos_y\" value=\"%u\"></div>"
            "<div><label>放大×100（200=2倍）</label>"
            "<input name=\"tx_scale\" value=\"%u\"></div>"
            "<div><label>头顶文字</label><input name=\"tx_title\" value=\"%s\"></div>"
            "<div><label>交互选项（其他人）</label>"
            "<input name=\"tx_opt_other\" value=\"%s\"></div>"
            "<div><label>交互选项（冠军本人）</label>"
            "<input name=\"tx_opt_champ\" value=\"%s\"></div>"
            "<div><label>江湖签名（≤32字，可空）</label>"
            "<input name=\"tx_signature\" value=\"%s\" maxlength=\"32\"></div>"
            "<div><label>显示翅膀</label><select name=\"tx_wing\">"
            "<option value=\"0\"%s>否</option>"
            "<option value=\"1\"%s>是</option></select></div>"
            "<div><label>显示动态称号</label><select name=\"tx_chenghao\">"
            "<option value=\"0\"%s>否</option>"
            "<option value=\"1\"%s>是</option></select></div>"
            "<div><label>显示坐骑</label><select name=\"tx_mount\">"
            "<option value=\"0\"%s>否</option>"
            "<option value=\"1\"%s>是</option></select></div>"
            "</div>"
            "<button type=\"submit\">保存雕像配置</button></form></section>"
            "</main></body></html>",
            tx && !tx->enabled ? " selected" : "",
            tx && tx->enabled ? " selected" : "",
            tx ? (u32)tx->posX : 168u, tx ? (u32)tx->posY : 200u,
            tx ? (u32)tx->scaleX100 : 200u, titleUtf8, otherUtf8, champUtf8,
            sigUtf8,
            tx && !tx->showWing ? " selected" : "",
            tx && tx->showWing ? " selected" : "",
            tx && !tx->showChenghao ? " selected" : "",
            tx && tx->showChenghao ? " selected" : "",
            tx && !tx->showMount ? " selected" : "",
            tx && tx->showMount ? " selected" : "");
    }
}

static void vm_mock_admin_handle_arena_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body)
{
    char text[64];
    u32 enabled = 1;
    u32 spawnX = 120;
    u32 spawnY = 120;
    u32 weekdayMask = 126;
    u16 weekdayBrackets[7];
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
    u32 wagerVitality = 50;
    u32 damageScaleEnabled = 1;
    u32 allowHeal = 0;
    u32 allowDodge = 1;
    u32 allowCombatPill = 0;
    char sceneUtf8[128];
    char kwUtf8[64];
    char sceneGbk[64];
    char kwGbk[32];
    const vm_mock_arena_config *cfg;

    weekdayBrackets[0] = 77;
    weekdayBrackets[1] = 70;
    weekdayBrackets[2] = 50;
    weekdayBrackets[3] = 10;
    weekdayBrackets[4] = 70;
    weekdayBrackets[5] = 50;
    weekdayBrackets[6] = 77;

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
        vm_mock_admin_arena_form_weekday_brackets(body, weekdayBrackets,
                                                   &weekdayMask);
        entryStart = vm_mock_admin_arena_form_hm(body, "entry_start_h",
                                                "entry_start_m", entryStart);
        entryEnd = vm_mock_admin_arena_form_hm(body, "entry_end_h", "entry_end_m",
                                              entryEnd);
        matchStart = vm_mock_admin_arena_form_hm(body, "match_start_h",
                                                "match_start_m", matchStart);
        matchEnd = vm_mock_admin_arena_form_hm(body, "match_end_h", "match_end_m",
                                              matchEnd);
        if (weekdayMask == 0)
        {
            /* All rest is allowed (Sunday-only rest week); keep zeros. */
        }
        if (entryEnd > 24u * 60u)
            entryEnd = 24u * 60u;
        if (matchEnd > 24u * 60u)
            matchEnd = 24u * 60u;
        (void)vm_mock_admin_form_u32(body, "round_seconds", 3600u, &roundSec);
        (void)vm_mock_admin_form_u32(body, "battle_timeout", 3600u, &battleTimeout);
        (void)vm_mock_admin_form_u32(body, "score_win", 1000u, &scoreWin);
        (void)vm_mock_admin_form_u32(body, "score_bye", 1000u, &scoreBye);
        (void)vm_mock_admin_form_u32(body, "score_draw", 1000u, &scoreDraw);
        (void)vm_mock_admin_form_u32(body, "damage_scale_enabled", 1u,
                                     &damageScaleEnabled);
        (void)vm_mock_admin_form_u32(body, "allow_heal", 1u, &allowHeal);
        (void)vm_mock_admin_form_u32(body, "allow_dodge", 1u, &allowDodge);
        (void)vm_mock_admin_form_u32(body, "allow_combat_pill", 1u,
                                     &allowCombatPill);
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
                weekdayBrackets, (u16)entryStart, (u16)entryEnd,
                (u16)matchStart, (u16)matchEnd, roundSec, battleTimeout,
                scoreWin, scoreBye, scoreDraw, kwGbk,
                cfg ? cfg->wagerEnabled : 0, cfg ? cfg->wagerFeePercent : 10,
                cfg ? cfg->wagerCurrency : 0,
                cfg ? cfg->wagerVitalityCost : 50u, (u8)damageScaleEnabled,
                (u8)allowHeal, (u8)allowDodge, (u8)allowCombatPill))
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
        u32 vitality = 0;
        u32 itemId = 0;
        u32 itemCount = 0;
        u32 equipId = 0;
        (void)vm_mock_admin_form_u32(body, "job", 3u, &job);
        (void)vm_mock_admin_form_u32(body, "bracket", 77u, &bracket);
        (void)vm_mock_admin_form_u32(body, "rank_pos", 5u, &rankPos);
        (void)vm_mock_admin_form_u32(body, "reward_exp", 0xffffffffu, &exp);
        (void)vm_mock_admin_form_u32(body, "reward_money", 0xffffffffu, &money);
        if (!vm_mock_admin_form_u32(body, "reward_vitality", 0xffffffffu,
                                    &vitality))
            (void)vm_mock_admin_form_u32(body, "reward_wcoin", 0xffffffffu,
                                         &vitality);
        (void)vm_mock_admin_form_u32(body, "item_id", 0xffffffffu, &itemId);
        (void)vm_mock_admin_form_u32(body, "item_count", 255u, &itemCount);
        (void)vm_mock_admin_form_u32(body, "equip_id", 0xffffffffu, &equipId);
        if (vm_net_mock_arena_admin_save_reward((u8)job, (u16)bracket, (u8)rankPos,
                                                exp, money, vitality, itemId,
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
        (void)vm_mock_admin_form_u32(body, "wager_vitality", 100000u,
                                     &wagerVitality);
        if (vm_net_mock_arena_admin_save_config(
                cfg->enabled, cfg->scene, cfg->spawnX, cfg->spawnY,
                cfg->weekdayMask, cfg->weekdayBrackets, cfg->entryStartMinute,
                cfg->entryEndMinute, cfg->matchStartMinute, cfg->matchEndMinute,
                cfg->roundSeconds, cfg->battleTimeoutSeconds, cfg->scoreWin,
                cfg->scoreBye, cfg->scoreDraw, cfg->npcNameKeyword,
                (u8)wagerEnabled, (u8)wagerFee, (u8)wagerCurrency, wagerVitality,
                cfg->damageScaleEnabled, cfg->allowHeal, cfg->allowDodge,
                cfg->allowCombatPill))
            vm_mock_admin_redirect_arena(client, "ok", "赏金擂台配置已保存");
        else
            vm_mock_admin_redirect_arena(client, "error", "保存失败");
        return;
    }

    if (strcmp(action, "save-tianxia-statue") == 0)
    {
        u32 txEnabled = 1;
        u32 txPosX = 168;
        u32 txPosY = 200;
        u32 txScale = 200;
        u32 txWing = 0;
        u32 txChenghao = 0;
        u32 txMount = 0;
        char titleUtf8[64];
        char otherUtf8[64];
        char champUtf8[64];
        char sigUtf8[160];
        char titleGbk[32];
        char otherGbk[32];
        char champGbk[32];
        char sigGbk[65];

        memset(titleUtf8, 0, sizeof(titleUtf8));
        memset(otherUtf8, 0, sizeof(otherUtf8));
        memset(champUtf8, 0, sizeof(champUtf8));
        memset(sigUtf8, 0, sizeof(sigUtf8));
        memset(titleGbk, 0, sizeof(titleGbk));
        memset(otherGbk, 0, sizeof(otherGbk));
        memset(champGbk, 0, sizeof(champGbk));
        memset(sigGbk, 0, sizeof(sigGbk));
        (void)vm_mock_admin_form_u32(body, "tx_enabled", 1u, &txEnabled);
        (void)vm_mock_admin_form_u32(body, "tx_pos_x", 0xffffu, &txPosX);
        (void)vm_mock_admin_form_u32(body, "tx_pos_y", 0xffffu, &txPosY);
        (void)vm_mock_admin_form_u32(body, "tx_scale", 400u, &txScale);
        (void)vm_mock_admin_form_u32(body, "tx_wing", 1u, &txWing);
        (void)vm_mock_admin_form_u32(body, "tx_chenghao", 1u, &txChenghao);
        (void)vm_mock_admin_form_u32(body, "tx_mount", 1u, &txMount);
        (void)vm_mock_admin_form_value(body, "tx_title", titleUtf8,
                                       sizeof(titleUtf8));
        (void)vm_mock_admin_form_value(body, "tx_opt_other", otherUtf8,
                                       sizeof(otherUtf8));
        (void)vm_mock_admin_form_value(body, "tx_opt_champ", champUtf8,
                                       sizeof(champUtf8));
        (void)vm_mock_admin_form_value(body, "tx_signature", sigUtf8,
                                       sizeof(sigUtf8));
        if (titleUtf8[0] == 0)
            snprintf(titleUtf8, sizeof(titleUtf8), "%s", "天下第一");
        if (otherUtf8[0] == 0)
            snprintf(otherUtf8, sizeof(otherUtf8), "%s", "膜拜大侠");
        if (champUtf8[0] == 0)
            snprintf(champUtf8, sizeof(champUtf8), "%s", "接受膜拜");
        if (!vm_mock_admin_utf8_to_gbk_text(titleUtf8, titleGbk, sizeof(titleGbk),
                                            false))
            snprintf(titleGbk, sizeof(titleGbk), "%s",
                     "\xcc\xec\xcf\xc2\xb5\xda\xd2\xbb");
        if (!vm_mock_admin_utf8_to_gbk_text(otherUtf8, otherGbk, sizeof(otherGbk),
                                            false))
            snprintf(otherGbk, sizeof(otherGbk), "%s",
                     "\xc4\xa4\xb0\xdd\xb4\xf3\xcf\xc0");
        if (!vm_mock_admin_utf8_to_gbk_text(champUtf8, champGbk, sizeof(champGbk),
                                            false))
            snprintf(champGbk, sizeof(champGbk), "%s",
                     "\xbd\xd3\xca\xdc\xc4\xa4\xb0\xdd");
        if (sigUtf8[0] != 0 &&
            !vm_mock_admin_utf8_to_gbk_text(sigUtf8, sigGbk, sizeof(sigGbk),
                                            false))
        {
            vm_mock_admin_redirect_arena(client, "error", "江湖签名编码失败");
            return;
        }
        if (vm_mock_tianxia_admin_save_config(
                (u8)txEnabled, (u16)txPosX, (u16)txPosY, (u16)txScale,
                (u8)txWing, (u8)txChenghao, (u8)txMount, titleGbk, otherGbk,
                champGbk, sigGbk))
            vm_mock_admin_redirect_arena(client, "ok", "天下第一雕像配置已保存");
        else
            vm_mock_admin_redirect_arena(client, "error", "保存雕像配置失败");
        return;
    }

    vm_mock_admin_redirect_arena(client, "error", "未知操作");
}
