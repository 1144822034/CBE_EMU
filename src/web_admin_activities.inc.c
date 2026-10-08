/* Admin UI: 节日使者兑换 / 雁门关 / 跑环 — included from web_admin_server.c. */

static void vm_mock_admin_redirect_activities(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=activities&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_redirect_activities_mysql_fail(vm_mock_service_socket client,
                                                         const char *prefix)
{
    char msg[384];
    const char *mysqlErr = vm_mysql_last_error();

    memset(msg, 0, sizeof(msg));
    if (mysqlErr != NULL && mysqlErr[0] != 0 &&
        strcmp(mysqlErr, "no MySQL error detail") != 0)
        snprintf(msg, sizeof(msg), "%s：%s", prefix ? prefix : "保存失败", mysqlErr);
    else
        snprintf(msg, sizeof(msg), "%s", prefix ? prefix : "保存失败");
    vm_mock_admin_redirect_activities(client, "error", msg);
}

static void vm_mock_admin_trim_ascii(char *text)
{
    char *start;
    char *end;

    if (text == NULL || text[0] == 0)
        return;
    start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;
    if (start != text)
        memmove(text, start, strlen(start) + 1);
    end = text + strlen(text);
    while (end > text &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        --end;
    *end = 0;
}

static bool vm_mock_admin_form_u32_trimmed(const char *body, const char *field,
                                           u32 maximum, u32 *valueOut, bool *foundOut)
{
    char textValue[32];
    u32 parsed = 0;

    if (foundOut)
        *foundOut = false;
    if (!vm_mock_admin_form_value(body, field, textValue, sizeof(textValue)))
        return false;
    if (foundOut)
        *foundOut = true;
    vm_mock_admin_trim_ascii(textValue);
    if (textValue[0] == 0)
        return false;
    if (!vm_net_mock_parse_u32_strict(textValue, &parsed) || parsed > maximum)
        return false;
    if (valueOut)
        *valueOut = parsed;
    return true;
}

static const vm_mock_festival_exchange *
vm_mock_admin_festival_exchange_find(u32 exchangeId)
{
    u32 i;
    u32 count = vm_mock_festival_exchange_admin_count();

    for (i = 0; i < count; ++i)
    {
        const vm_mock_festival_exchange *ex = vm_mock_festival_exchange_admin_get(i);
        if (ex != NULL && ex->exchangeId == exchangeId)
            return ex;
    }
    return NULL;
}

static const vm_mock_yanmen_exchange *vm_mock_admin_yanmen_exchange_find(u32 exchangeId)
{
    u32 i;
    u32 count = vm_mock_yanmen_exchange_admin_count();

    for (i = 0; i < count; ++i)
    {
        const vm_mock_yanmen_exchange *ex = vm_mock_yanmen_exchange_admin_get(i);
        if (ex != NULL && ex->exchangeId == exchangeId)
            return ex;
    }
    return NULL;
}

static void vm_mock_admin_render_activities_page(char *response, size_t responseCap,
                                                 const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_mock_festival_config *fest;
    const vm_mock_yanmen_config *yanmen;
    const vm_mock_ring_config *ring;
    char kwUtf8[64];
    char welcomeUtf8[160];
    char festExText[16];
    char yanExText[16];
    u32 festEditId = 0;
    u32 yanEditId = 0;
    const vm_mock_festival_exchange *festEdit = NULL;
    const vm_mock_yanmen_exchange *yanEdit = NULL;
    char festEditLabelUtf8[64];
    char yanEditLabelUtf8[64];
    u32 i;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(festExText, 0, sizeof(festExText));
    memset(yanExText, 0, sizeof(yanExText));
    memset(festEditLabelUtf8, 0, sizeof(festEditLabelUtf8));
    memset(yanEditLabelUtf8, 0, sizeof(yanEditLabelUtf8));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    fest = vm_mock_festival_config_get();
    yanmen = vm_mock_yanmen_config_get();
    ring = vm_mock_ring_config_get();
    vm_net_mock_gbk_label_to_utf8(fest ? fest->npcKeyword : "", kwUtf8,
                                  sizeof(kwUtf8));
    vm_net_mock_gbk_label_to_utf8(fest ? fest->welcomeText : "", welcomeUtf8,
                                  sizeof(welcomeUtf8));
    (void)vm_mock_admin_form_value(query, "fest_ex", festExText, sizeof(festExText));
    (void)vm_mock_admin_form_value(query, "yan_ex", yanExText, sizeof(yanExText));
    if (festExText[0] != 0 &&
        vm_net_mock_parse_u32_strict(festExText, &festEditId) && festEditId > 0)
        festEdit = vm_mock_admin_festival_exchange_find(festEditId);
    if (yanExText[0] != 0 &&
        vm_net_mock_parse_u32_strict(yanExText, &yanEditId) && yanEditId > 0)
        yanEdit = vm_mock_admin_yanmen_exchange_find(yanEditId);
    if (festEdit != NULL)
        vm_net_mock_gbk_label_to_utf8(festEdit->label, festEditLabelUtf8,
                                      sizeof(festEditLabelUtf8));
    if (yanEdit != NULL)
        vm_net_mock_gbk_label_to_utf8(yanEdit->label, yanEditLabelUtf8,
                                      sizeof(yanEditLabelUtf8));

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 节日/跑环</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7;max-width:1100px}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px}"
        ".tabs{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 16px}.tab{padding:8px 12px;margin:4px;background:#fff;border:1px solid #ddd;"
        "text-decoration:none;color:#333;border-radius:6px;display:inline-block}"
        ".tab.on{background:#175cd3;color:#fff}"
        "input,select,textarea{padding:8px;width:100%%;box-sizing:border-box}"
        "label{font-size:12px;color:#666;display:block;margin-bottom:4px}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}"
        "table{border-collapse:collapse;width:100%%}th,td{padding:8px;border-bottom:1px solid #eee;font-size:13px}"
        "button{background:#175cd3;color:#fff;border:0;padding:10px 14px;border-radius:6px;cursor:pointer}"
        "h2{font-size:17px;margin:0 0 12px}"
        "</style></head><body><h1>节日使者 / 雁门关 / 跑环</h1>"
        "<p>比武每回合+20活力；跑环每完成一环+5活力；天梯挑战成功+5活力；"
        "在线每满1小时可加活力（下方可配）；"
        "节日使者在场景内对话选「活力兑换」；雁门关打怪掉木料石材；跑环使接随机环任务。</p>");
    vm_mock_admin_append_tabs(&page, "activities");

    if (status[0] && message[0])
        vm_mock_admin_text_appendf(&page, "<p><strong>%s</strong>: %s</p>", status,
                                   message);

    {
        u32 onlinePerHour = vm_mock_vitality_online_per_hour_get();
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\"><h2>在线活力</h2>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-vitality-online\">"
            "<div class=\"grid\">"
            "<div><label>每在线1小时获得活力（0=关闭）</label>"
            "<input name=\"online_per_hour\" type=\"number\" min=\"0\" "
            "max=\"100000\" value=\"%u\"></div>"
            "</div>"
            "<button type=\"submit\" style=\"margin-top:10px\">保存在线活力</button>"
            "</form>"
            "<p class=\"muted\" style=\"margin-top:10px\">累计在线满3600秒发放一次；"
            "不足1小时的进度下线会保存，下次上线继续累计。</p>"
            "</section>",
            onlinePerHour);
    }

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>节日使者（活力兑换）</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-festival-config\">"
        "<div class=\"grid\">"
        "<div><label>启用</label><select name=\"enabled\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
        "<div><label>NPC名关键字（GBK/中文，如 节日使者）</label>"
        "<input name=\"npc_keyword\" value=\"",
        fest && fest->enabled ? " selected" : "",
        fest && !fest->enabled ? " selected" : "");
    vm_mock_admin_text_append_html(&page, kwUtf8);
    vm_mock_admin_text_appendf(
        &page,
        "\"></div>"
        "</div>"
        "<label style=\"margin-top:10px\">欢迎语（对话首屏）</label>"
        "<textarea name=\"welcome_text\" rows=\"2\">");
    vm_mock_admin_text_append_html(&page, welcomeUtf8);
    vm_mock_admin_text_appendf(
        &page,
        "</textarea>"
        "<button type=\"submit\" style=\"margin-top:10px\">保存节日配置</button></form>"
        "<table style=\"margin-top:16px\"><tr><th>ID</th><th>活力</th><th>物品</th><th>数量</th><th>标签</th><th>序</th><th></th></tr>");

    for (i = 0; i < vm_mock_festival_exchange_admin_count(); ++i)
    {
        const vm_mock_festival_exchange *ex = vm_mock_festival_exchange_admin_get(i);
        char labelUtf8[64];
        if (ex == NULL)
            continue;
        vm_net_mock_gbk_label_to_utf8(ex->label, labelUtf8, sizeof(labelUtf8));
        vm_mock_admin_text_appendf(
            &page,
            "<tr><td>%u</td><td>%u</td><td>%u</td><td>%u</td><td>",
            ex->exchangeId, ex->vitalityCost, ex->itemId, ex->itemCount);
        vm_mock_admin_text_append_html(&page, labelUtf8);
        vm_mock_admin_text_appendf(&page, "</td><td>%u</td><td>",
                                    (u32)ex->sortOrder);
        vm_mock_admin_text_appendf(
            &page, "<a href=\"/?tab=activities&amp;fest_ex=%u\">编辑</a>",
            ex->exchangeId);
        vm_mock_admin_text_appendf(&page, "</td></tr>");
    }

    if (festEdit != NULL)
    {
        vm_mock_admin_text_appendf(
            &page,
            "</table>"
            "<form method=\"post\" action=\"/action\" style=\"margin-top:12px\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-festival-exchange\">"
            "<div class=\"grid\">"
            "<div><label>兑换ID</label><input name=\"exchange_id\" type=\"number\" "
            "min=\"1\" value=\"%u\" readonly></div>"
            "<div><label>消耗活力</label><input name=\"vitality_cost\" type=\"number\" "
            "min=\"0\" value=\"%u\"></div>"
            "<div><label>物品ID</label><input name=\"item_id\" type=\"number\" "
            "min=\"1\" value=\"%u\"></div>"
            "<div><label>数量</label><input name=\"item_count\" type=\"number\" "
            "min=\"1\" value=\"%u\"></div>"
            "<div><label>显示名</label><input name=\"label\" value=\"",
            festEdit->exchangeId, festEdit->vitalityCost, festEdit->itemId,
            festEdit->itemCount);
        vm_mock_admin_text_append_html(&page, festEditLabelUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"></div>"
            "<div><label>排序</label><input name=\"sort_order\" type=\"number\" "
            "min=\"0\" max=\"10000\" value=\"%u\"></div>"
            "<div><label>启用</label><select name=\"fest_ex_enabled\">"
            "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
            "</div><button type=\"submit\">更新兑换项</button></form></section>",
            (u32)festEdit->sortOrder,
            festEdit->enabled ? " selected" : "",
            !festEdit->enabled ? " selected" : "");
    }
    else
    {
        vm_mock_admin_text_appendf(
            &page,
            "</table>"
            "<form method=\"post\" action=\"/action\" style=\"margin-top:12px\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-festival-exchange\">"
            "<div class=\"grid\">"
            "<div><label>兑换ID</label><input name=\"exchange_id\" type=\"number\" "
            "min=\"1\" placeholder=\"1\"></div>"
            "<div><label>消耗活力</label><input name=\"vitality_cost\" type=\"number\" "
            "min=\"0\" placeholder=\"50\"></div>"
            "<div><label>物品ID</label><input name=\"item_id\" type=\"number\" "
            "min=\"1\" placeholder=\"813\"></div>"
            "<div><label>数量</label><input name=\"item_count\" type=\"number\" "
            "min=\"1\" value=\"1\"></div>"
            "<div><label>显示名</label><input name=\"label\" "
            "placeholder=\"玄晶钥匙\"></div>"
            "<div><label>排序</label><input name=\"sort_order\" type=\"number\" "
            "min=\"0\" max=\"10000\" value=\"0\"></div>"
            "<div><label>启用</label><select name=\"fest_ex_enabled\">"
            "<option value=\"1\" selected>开</option>"
            "<option value=\"0\">关</option></select></div>"
            "</div><button type=\"submit\">添加兑换项</button></form></section>");
    }

    {
        char sceneUtf8[64];
        char yanNpcUtf8[64];
        vm_net_mock_gbk_label_to_utf8(yanmen ? yanmen->sceneKeyword : "", sceneUtf8,
                                      sizeof(sceneUtf8));
        vm_net_mock_gbk_label_to_utf8(yanmen ? yanmen->npcKeyword : "", yanNpcUtf8,
                                      sizeof(yanNpcUtf8));
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\"><h2>雁门关副本材料</h2>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-yanmen-config\">"
            "<div class=\"grid\">"
            "<div><label>启用</label><select name=\"enabled\">"
            "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
            "<div><label>场景关键字</label><input name=\"scene_keyword\" value=\"",
            yanmen && yanmen->enabled ? " selected" : "",
            yanmen && !yanmen->enabled ? " selected" : "");
        vm_mock_admin_text_append_html(&page, sceneUtf8);
        vm_mock_admin_text_appendf(&page, "\"></div>"
                                       "<div><label>兑换NPC关键字</label>"
                                       "<input name=\"npc_keyword\" value=\"");
        vm_mock_admin_text_append_html(&page, yanNpcUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"></div>"
            "<div><label>木料物品ID</label><input name=\"wood_item_id\" value=\"%u\"></div>"
            "<div><label>石材物品ID</label><input name=\"stone_item_id\" value=\"%u\"></div>"
            "<div><label>木料掉落min</label><input name=\"wood_min\" value=\"%u\"></div>"
            "<div><label>木料掉落max</label><input name=\"wood_max\" value=\"%u\"></div>"
            "<div><label>石材掉落min</label><input name=\"stone_min\" value=\"%u\"></div>"
            "<div><label>石材掉落max</label><input name=\"stone_max\" value=\"%u\"></div>"
            "</div><button type=\"submit\" style=\"margin-top:10px\">保存雁门关配置</button></form>"
            "<table style=\"margin-top:16px\"><tr><th>ID</th><th>木</th><th>石</th><th>物品</th><th>数量</th><th>标签</th><th></th></tr>",
            yanmen ? yanmen->woodItemId : 3u,
            yanmen ? yanmen->stoneItemId : 5u,
            yanmen ? yanmen->woodDropMin : 1u,
            yanmen ? yanmen->woodDropMax : 3u,
            yanmen ? yanmen->stoneDropMin : 1u,
            yanmen ? yanmen->stoneDropMax : 2u);
    }

    for (i = 0; i < vm_mock_yanmen_exchange_admin_count(); ++i)
    {
        const vm_mock_yanmen_exchange *ex = vm_mock_yanmen_exchange_admin_get(i);
        char labelUtf8[64];
        if (ex == NULL)
            continue;
        vm_net_mock_gbk_label_to_utf8(ex->label, labelUtf8, sizeof(labelUtf8));
        vm_mock_admin_text_appendf(
            &page,
            "<tr><td>%u</td><td>%u</td><td>%u</td><td>%u</td><td>%u</td><td>",
            ex->exchangeId, ex->woodCost, ex->stoneCost, ex->itemId, ex->itemCount);
        vm_mock_admin_text_append_html(&page, labelUtf8);
        vm_mock_admin_text_appendf(
            &page, "</td><td><a href=\"/?tab=activities&amp;yan_ex=%u\">编辑</a></td></tr>",
            ex->exchangeId);
    }

    if (yanEdit != NULL)
    {
        vm_mock_admin_text_appendf(
            &page,
            "</table>"
            "<form method=\"post\" action=\"/action\" style=\"margin-top:12px\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-yanmen-exchange\">"
            "<div class=\"grid\">"
            "<div><label>兑换ID</label><input name=\"exchange_id\" type=\"number\" "
            "min=\"1\" value=\"%u\" readonly></div>"
            "<div><label>消耗木料</label><input name=\"wood_cost\" type=\"number\" "
            "min=\"0\" value=\"%u\"></div>"
            "<div><label>消耗石材</label><input name=\"stone_cost\" type=\"number\" "
            "min=\"0\" value=\"%u\"></div>"
            "<div><label>物品ID</label><input name=\"item_id\" type=\"number\" "
            "min=\"1\" value=\"%u\"></div>"
            "<div><label>数量</label><input name=\"item_count\" type=\"number\" "
            "min=\"1\" value=\"%u\"></div>"
            "<div><label>显示名</label><input name=\"label\" value=\"",
            yanEdit->exchangeId, yanEdit->woodCost, yanEdit->stoneCost,
            yanEdit->itemId, yanEdit->itemCount);
        vm_mock_admin_text_append_html(&page, yanEditLabelUtf8);
        vm_mock_admin_text_appendf(
            &page,
            "\"></div>"
            "<div><label>排序</label><input name=\"sort_order\" type=\"number\" "
            "min=\"0\" max=\"10000\" value=\"%u\"></div>"
            "<div><label>启用</label><select name=\"yan_ex_enabled\">"
            "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
            "</div><button type=\"submit\">更新雁门兑换</button></form></section>",
            (u32)yanEdit->sortOrder,
            yanEdit->enabled ? " selected" : "",
            !yanEdit->enabled ? " selected" : "");
    }
    else
    {
        vm_mock_admin_text_appendf(
            &page,
            "</table>"
            "<form method=\"post\" action=\"/action\" style=\"margin-top:12px\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-yanmen-exchange\">"
            "<div class=\"grid\">"
            "<div><label>兑换ID</label><input name=\"exchange_id\" type=\"number\" "
            "min=\"1\"></div>"
            "<div><label>消耗木料</label><input name=\"wood_cost\" type=\"number\" "
            "min=\"0\"></div>"
            "<div><label>消耗石材</label><input name=\"stone_cost\" type=\"number\" "
            "min=\"0\"></div>"
            "<div><label>物品ID</label><input name=\"item_id\" type=\"number\" "
            "min=\"1\"></div>"
            "<div><label>数量</label><input name=\"item_count\" type=\"number\" "
            "min=\"1\" value=\"1\"></div>"
            "<div><label>显示名</label><input name=\"label\"></div>"
            "<div><label>排序</label><input name=\"sort_order\" type=\"number\" "
            "min=\"0\" max=\"10000\" value=\"0\"></div>"
            "<div><label>启用</label><select name=\"yan_ex_enabled\">"
            "<option value=\"1\" selected>开</option>"
            "<option value=\"0\">关</option></select></div>"
            "</div><button type=\"submit\">添加雁门兑换</button></form></section>");
    }

    {
        char ringNpcUtf8[64];
        vm_net_mock_gbk_label_to_utf8(ring ? ring->npcKeyword : "", ringNpcUtf8,
                                      sizeof(ringNpcUtf8));
        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\"><h2>跑环任务</h2>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"save-ring-config\">"
            "<div class=\"grid\">"
            "<div><label>启用</label><select name=\"enabled\">"
            "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
            "<div><label>NPC关键字（跑环使）</label>"
            "<input name=\"npc_keyword\" value=\"%s\"></div>"
            "<div><label>每日环数上限</label><input name=\"daily_max\" value=\"%u\"></div>"
            "<div><label>每日免费刷新次数</label>"
            "<input name=\"free_refresh_daily\" type=\"number\" min=\"0\" max=\"1000\" "
            "value=\"%u\"></div>"
            "<div><label>满环奖励物品(5玄=905)</label><input name=\"bonus_item_id\" value=\"%u\"></div>"
            "<div><label>满环奖励数量</label><input name=\"bonus_count\" value=\"%u\"></div>"
            "<div><label>钥匙掉落%%</label><input name=\"key_rate\" value=\"%u\"></div>"
            "<div><label>钥匙池1/2/3</label><input name=\"key1\" value=\"%u\"></div>"
            "<div><label></label><input name=\"key2\" value=\"%u\"></div>"
            "<div><label></label><input name=\"key3\" value=\"%u\"></div>"
            "<div><label>基础经验</label><input name=\"base_exp\" value=\"%u\"></div>"
            "<div><label>基础金钱</label><input name=\"base_money\" value=\"%u\"></div>"
            "<div><label>每环+经验</label><input name=\"exp_per_ring\" value=\"%u\"></div>"
            "<div><label>每环+金钱</label><input name=\"money_per_ring\" value=\"%u\"></div>"
            "</div>"
            "<p class=\"muted\" style=\"margin-top:10px\">免费刷新用尽后仍可花10酷宝刷新；"
            "不消耗当日环数。填0则仅酷宝刷新。</p>"
            "<button type=\"submit\" style=\"margin-top:10px\">保存跑环配置</button></form>"
            "</section></body></html>",
            ring && ring->enabled ? " selected" : "",
            ring && !ring->enabled ? " selected" : "",
            ringNpcUtf8,
            ring ? ring->dailyMax : 20u,
            ring ? ring->freeRefreshDaily : 3u,
            ring ? ring->dailyBonusItemId : 905u,
            ring ? ring->dailyBonusCount : 1u,
            ring ? ring->keyDropRate : 5u,
            ring ? ring->keyItemIds[0] : 813u,
            ring ? ring->keyItemIds[1] : 814u,
            ring ? ring->keyItemIds[2] : 815u,
            ring ? ring->baseExp : 500u,
            ring ? ring->baseMoney : 1000u,
            ring ? ring->expPerRing : 200u,
            ring ? ring->moneyPerRing : 500u);
    }
}

static void vm_mock_admin_handle_activities_action(vm_mock_service_socket client,
                                                   const char *action,
                                                   const char *body)
{
    if (strcmp(action, "save-vitality-online") == 0)
    {
        u32 perHour = 10;

        if (!vm_mock_admin_form_u32(body, "online_per_hour", 100000u, &perHour))
        {
            vm_mock_admin_redirect_activities(client, "error",
                                              "每小时活力无效（0-100000）");
            return;
        }
        if (vm_mock_vitality_admin_save_online_per_hour(perHour))
            vm_mock_admin_redirect_activities(client, "ok", "在线活力配置已保存");
        else
            vm_mock_admin_redirect_activities_mysql_fail(client,
                                                        "在线活力配置保存失败");
        return;
    }
    if (strcmp(action, "save-festival-config") == 0)
    {
        char kwUtf8[64];
        char welcomeUtf8[256];
        char kwGbk[64];
        char welcomeGbk[256];
        u32 enabled = 1;

        memset(kwUtf8, 0, sizeof(kwUtf8));
        memset(welcomeUtf8, 0, sizeof(welcomeUtf8));
        (void)vm_mock_admin_form_u32(body, "enabled", 1u, &enabled);
        (void)vm_mock_admin_form_value(body, "npc_keyword", kwUtf8, sizeof(kwUtf8));
        (void)vm_mock_admin_form_value(body, "welcome_text", welcomeUtf8,
                                       sizeof(welcomeUtf8));
        if (kwUtf8[0] == 0)
            snprintf(kwUtf8, sizeof(kwUtf8), "%s", "节日使者");
        if (!vm_mock_admin_utf8_to_gbk_text(kwUtf8, kwGbk, sizeof(kwGbk), false))
            snprintf(kwGbk, sizeof(kwGbk), "%s", "\xbd\xda\xc8\xd5\xca\xb9\xd5\xdf");
        if (!vm_mock_admin_utf8_to_gbk_text(welcomeUtf8, welcomeGbk,
                                            sizeof(welcomeGbk), false))
            welcomeGbk[0] = 0;
        if (vm_mock_festival_admin_save_config((u8)enabled, kwGbk, welcomeGbk))
            vm_mock_admin_redirect_activities(client, "ok", "节日配置已保存");
        else
            vm_mock_admin_redirect_activities_mysql_fail(client, "节日配置保存失败");
        return;
    }
    if (strcmp(action, "save-festival-exchange") == 0)
    {
        u32 id = 0;
        u32 cost = 0;
        u32 itemId = 0;
        u32 count = 1;
        u32 sort = 0;
        u32 enabled = 1;
        bool got = false;
        const vm_mock_festival_exchange *existing = NULL;
        char labelUtf8[64];
        char labelGbk[64];

        memset(labelUtf8, 0, sizeof(labelUtf8));
        memset(labelGbk, 0, sizeof(labelGbk));
        if (!vm_mock_admin_form_u32_trimmed(body, "exchange_id", 0xffffffffu, &id,
                                            &got) ||
            !got || id == 0)
        {
            vm_mock_admin_redirect_activities(client, "error", "兑换ID无效");
            return;
        }
        existing = vm_mock_admin_festival_exchange_find(id);
        if (vm_mock_admin_form_u32_trimmed(body, "vitality_cost", 0xffffffffu, &cost,
                                           &got) && got)
        {
            /* use submitted vitality */
        }
        else if (existing != NULL)
            cost = existing->vitalityCost;
        else
        {
            vm_mock_admin_redirect_activities(client, "error", "请填写消耗活力");
            return;
        }
        if (vm_mock_admin_form_u32_trimmed(body, "item_id", 0xffffffffu, &itemId,
                                           &got) && got)
        {
            /* use submitted item */
        }
        else if (existing != NULL)
            itemId = existing->itemId;
        else
        {
            vm_mock_admin_redirect_activities(client, "error", "请填写物品ID");
            return;
        }
        if (vm_mock_admin_form_u32_trimmed(body, "item_count", 0xffffffffu, &count,
                                           &got) && got && count > 0)
        {
            /* use submitted count */
        }
        else if (existing != NULL)
            count = existing->itemCount;
        else
            count = 1;
        if (vm_mock_admin_form_u32_trimmed(body, "sort_order", 10000u, &sort, &got) &&
            got)
        {
            /* use submitted sort */
        }
        else if (existing != NULL)
            sort = (u32)existing->sortOrder;
        if (vm_mock_admin_form_u32_trimmed(body, "fest_ex_enabled", 1u, &enabled,
                                           &got) && got)
        {
            /* use submitted enabled */
        }
        else if (vm_mock_admin_form_u32_trimmed(body, "enabled", 1u, &enabled,
                                                &got) && got)
        {
            /* legacy field name */
        }
        else if (existing != NULL)
            enabled = (u32)existing->enabled;
        (void)vm_mock_admin_form_value(body, "label", labelUtf8, sizeof(labelUtf8));
        vm_mock_admin_trim_ascii(labelUtf8);
        if (labelUtf8[0] == 0 && existing != NULL)
            snprintf(labelGbk, sizeof(labelGbk), "%s", existing->label);
        else if (!vm_mock_admin_utf8_to_gbk_text(labelUtf8, labelGbk, sizeof(labelGbk),
                                                 false))
            labelGbk[0] = 0;
        if (itemId == 0)
            vm_mock_admin_redirect_activities(client, "error", "物品ID不能为空");
        else if (vm_mock_festival_admin_save_exchange(id, cost, itemId, count,
                                                      labelGbk, (u16)sort,
                                                      (u8)enabled))
            vm_mock_admin_redirect_activities(client, "ok", "节日兑换项已保存");
        else
            vm_mock_admin_redirect_activities_mysql_fail(client, "节日兑换项保存失败");
        return;
    }
    if (strcmp(action, "save-yanmen-config") == 0)
    {
        u32 enabled = 1;
        u32 woodId = 3;
        u32 stoneId = 5;
        u32 wMin = 1;
        u32 wMax = 3;
        u32 sMin = 1;
        u32 sMax = 2;
        char sceneUtf8[64];
        char npcUtf8[64];
        char sceneGbk[64];
        char npcGbk[64];

        (void)vm_mock_admin_form_u32(body, "enabled", 1u, &enabled);
        (void)vm_mock_admin_form_value(body, "scene_keyword", sceneUtf8,
                                       sizeof(sceneUtf8));
        (void)vm_mock_admin_form_value(body, "npc_keyword", npcUtf8, sizeof(npcUtf8));
        (void)vm_mock_admin_form_u32(body, "wood_item_id", 0xffffffffu, &woodId);
        (void)vm_mock_admin_form_u32(body, "stone_item_id", 0xffffffffu, &stoneId);
        (void)vm_mock_admin_form_u32(body, "wood_min", 100u, &wMin);
        (void)vm_mock_admin_form_u32(body, "wood_max", 100u, &wMax);
        (void)vm_mock_admin_form_u32(body, "stone_min", 100u, &sMin);
        (void)vm_mock_admin_form_u32(body, "stone_max", 100u, &sMax);
        if (sceneUtf8[0] == 0)
            snprintf(sceneUtf8, sizeof(sceneUtf8), "%s", "雁门关");
        if (npcUtf8[0] == 0)
            snprintf(npcUtf8, sizeof(npcUtf8), "%s", "关隘使者");
        if (!vm_mock_admin_utf8_to_gbk_text(sceneUtf8, sceneGbk, sizeof(sceneGbk),
                                            false))
            snprintf(sceneGbk, sizeof(sceneGbk), "%s", "\xd1\xe3\xc3\xc5\xb9\xd8");
        if (!vm_mock_admin_utf8_to_gbk_text(npcUtf8, npcGbk, sizeof(npcGbk), false))
            snprintf(npcGbk, sizeof(npcGbk), "%s", "\xb9\xd8\xb0\xac\xca\xb9\xd5\xdf");
        if (vm_mock_yanmen_admin_save_config((u8)enabled, sceneGbk, npcGbk, woodId,
                                             stoneId, (u8)wMin, (u8)wMax, (u8)sMin,
                                             (u8)sMax))
            vm_mock_admin_redirect_activities(client, "ok", "雁门关配置已保存");
        else
            vm_mock_admin_redirect_activities_mysql_fail(client, "雁门关配置保存失败");
        return;
    }
    if (strcmp(action, "save-yanmen-exchange") == 0)
    {
        u32 id = 0;
        u32 wood = 0;
        u32 stone = 0;
        u32 itemId = 0;
        u32 count = 1;
        u32 sort = 0;
        u32 enabled = 1;
        bool got = false;
        const vm_mock_yanmen_exchange *existing = NULL;
        char labelUtf8[64];
        char labelGbk[64];

        memset(labelUtf8, 0, sizeof(labelUtf8));
        memset(labelGbk, 0, sizeof(labelGbk));
        if (!vm_mock_admin_form_u32_trimmed(body, "exchange_id", 0xffffffffu, &id,
                                            &got) ||
            !got || id == 0)
        {
            vm_mock_admin_redirect_activities(client, "error", "兑换ID无效");
            return;
        }
        existing = vm_mock_admin_yanmen_exchange_find(id);
        if (vm_mock_admin_form_u32_trimmed(body, "wood_cost", 0xffffffffu, &wood, &got) &&
            got)
        {
            /* use submitted wood */
        }
        else if (existing != NULL)
            wood = existing->woodCost;
        else
        {
            vm_mock_admin_redirect_activities(client, "error", "请填写消耗木料");
            return;
        }
        if (vm_mock_admin_form_u32_trimmed(body, "stone_cost", 0xffffffffu, &stone,
                                           &got) && got)
        {
            /* use submitted stone */
        }
        else if (existing != NULL)
            stone = existing->stoneCost;
        else
        {
            vm_mock_admin_redirect_activities(client, "error", "请填写消耗石材");
            return;
        }
        if (vm_mock_admin_form_u32_trimmed(body, "item_id", 0xffffffffu, &itemId,
                                           &got) && got)
        {
            /* use submitted item */
        }
        else if (existing != NULL)
            itemId = existing->itemId;
        else
        {
            vm_mock_admin_redirect_activities(client, "error", "请填写物品ID");
            return;
        }
        if (vm_mock_admin_form_u32_trimmed(body, "item_count", 0xffffffffu, &count,
                                           &got) && got && count > 0)
        {
            /* use submitted count */
        }
        else if (existing != NULL)
            count = existing->itemCount;
        else
            count = 1;
        if (vm_mock_admin_form_u32_trimmed(body, "sort_order", 10000u, &sort, &got) &&
            got)
        {
            /* use submitted sort */
        }
        else if (existing != NULL)
            sort = (u32)existing->sortOrder;
        if (vm_mock_admin_form_u32_trimmed(body, "yan_ex_enabled", 1u, &enabled,
                                           &got) && got)
        {
            /* use submitted enabled */
        }
        else if (vm_mock_admin_form_u32_trimmed(body, "enabled", 1u, &enabled,
                                                &got) && got)
        {
            /* legacy field name */
        }
        else if (existing != NULL)
            enabled = (u32)existing->enabled;
        (void)vm_mock_admin_form_value(body, "label", labelUtf8, sizeof(labelUtf8));
        vm_mock_admin_trim_ascii(labelUtf8);
        if (labelUtf8[0] == 0 && existing != NULL)
            snprintf(labelGbk, sizeof(labelGbk), "%s", existing->label);
        else if (!vm_mock_admin_utf8_to_gbk_text(labelUtf8, labelGbk, sizeof(labelGbk),
                                                 false))
            labelGbk[0] = 0;
        if (itemId == 0)
            vm_mock_admin_redirect_activities(client, "error", "物品ID不能为空");
        else if (vm_mock_yanmen_admin_save_exchange(id, wood, stone, itemId, count,
                                                    labelGbk, (u16)sort, (u8)enabled))
            vm_mock_admin_redirect_activities(client, "ok", "雁门兑换项已保存");
        else
            vm_mock_admin_redirect_activities_mysql_fail(client, "雁门兑换项保存失败");
        return;
    }
    if (strcmp(action, "save-ring-config") == 0)
    {
        u32 enabled = 1;
        u32 daily = 20;
        u32 freeRefresh = 3;
        u32 bonusId = 905;
        u32 bonusCount = 1;
        u32 keyRate = 5;
        u32 k1 = 813;
        u32 k2 = 814;
        u32 k3 = 815;
        u32 baseExp = 500;
        u32 baseMoney = 1000;
        u32 expRing = 200;
        u32 moneyRing = 500;
        char npcUtf8[64];
        char npcGbk[64];

        (void)vm_mock_admin_form_u32(body, "enabled", 1u, &enabled);
        (void)vm_mock_admin_form_value(body, "npc_keyword", npcUtf8, sizeof(npcUtf8));
        (void)vm_mock_admin_form_u32(body, "daily_max", 1000u, &daily);
        (void)vm_mock_admin_form_u32(body, "free_refresh_daily", 1000u, &freeRefresh);
        (void)vm_mock_admin_form_u32(body, "bonus_item_id", 0xffffffffu, &bonusId);
        (void)vm_mock_admin_form_u32(body, "bonus_count", 100u, &bonusCount);
        (void)vm_mock_admin_form_u32(body, "key_rate", 100u, &keyRate);
        (void)vm_mock_admin_form_u32(body, "key1", 0xffffffffu, &k1);
        (void)vm_mock_admin_form_u32(body, "key2", 0xffffffffu, &k2);
        (void)vm_mock_admin_form_u32(body, "key3", 0xffffffffu, &k3);
        (void)vm_mock_admin_form_u32(body, "base_exp", 0xffffffffu, &baseExp);
        (void)vm_mock_admin_form_u32(body, "base_money", 0xffffffffu, &baseMoney);
        (void)vm_mock_admin_form_u32(body, "exp_per_ring", 0xffffffffu, &expRing);
        (void)vm_mock_admin_form_u32(body, "money_per_ring", 0xffffffffu, &moneyRing);
        if (npcUtf8[0] == 0)
            snprintf(npcUtf8, sizeof(npcUtf8), "%s", "跑环使");
        if (!vm_mock_admin_utf8_to_gbk_text(npcUtf8, npcGbk, sizeof(npcGbk), false))
            snprintf(npcGbk, sizeof(npcGbk), "%s", "\xc5\xdc\xbb\xb7\xca\xb9\xd5\xdf");
        if (vm_mock_ring_admin_save_config((u8)enabled, npcGbk, (u16)daily, bonusId,
                                           bonusCount, (u8)keyRate, k1, k2, k3,
                                           baseExp, baseMoney, expRing, moneyRing,
                                           (u16)freeRefresh))
            vm_mock_admin_redirect_activities(client, "ok", "跑环配置已保存");
        else
            vm_mock_admin_redirect_activities(client, "error", "保存失败");
        return;
    }
    vm_mock_admin_redirect_activities(client, "error", "未知操作");
}
