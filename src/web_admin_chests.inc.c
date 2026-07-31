/* Admin UI for server_chest_rewards — included from web_admin_server.c. */

static void vm_mock_admin_redirect_chests(vm_mock_service_socket client,
                                          u32 chestItemId,
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
             "?tab=chests&chest=%u&status=%s&message=%s",
             chestItemId ? chestItemId : VM_NET_MOCK_GOLD_CHEST_ITEM_ID,
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_render_chest_page(char *response, size_t responseCap,
                                            const char *query)
{
    vm_mock_admin_text page;
    char chestText[16];
    char status[16];
    char message[256];
    char itemNameUtf8[128];
    char pageText[32];
    u32 chestItemId = VM_NET_MOCK_GOLD_CHEST_ITEM_ID;
    u32 rowCount = 0;
    u32 enabledCount = 0;
    u32 pageNumber = 1;
    u32 pageSize = 40;
    u32 pageCount = 1;
    u32 rowStart = 0;
    u32 rowEnd = 0;
    static const u32 chestIds[] = {
        VM_NET_MOCK_BRONZE_CHEST_ITEM_ID,
        VM_NET_MOCK_SILVER_CHEST_ITEM_ID,
        VM_NET_MOCK_GOLD_CHEST_ITEM_ID
    };

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(chestText, 0, sizeof(chestText));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(pageText, 0, sizeof(pageText));
    (void)vm_mock_admin_form_value(query, "chest", chestText, sizeof(chestText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    (void)vm_mock_admin_form_value(query, "page", pageText, sizeof(pageText));
    if (chestText[0] != 0)
        (void)vm_net_mock_parse_u32_strict(chestText, &chestItemId);
    if (!vm_net_mock_chest_box_is_supported(chestItemId))
        chestItemId = VM_NET_MOCK_GOLD_CHEST_ITEM_ID;
    if (pageText[0] != 0 &&
        (!vm_net_mock_parse_u32_strict(pageText, &pageNumber) || pageNumber == 0))
        pageNumber = 1;

    (void)vm_net_mock_load_shop_catalog();
    (void)vm_net_mock_chest_reward_db_load();
    rowCount = vm_net_mock_chest_reward_admin_count(chestItemId);
    enabledCount = vm_net_mock_chest_reward_admin_enabled_count(chestItemId);
    pageCount = rowCount == 0 ? 1 : ((rowCount + pageSize - 1) / pageSize);
    if (pageNumber > pageCount)
        pageNumber = pageCount;
    rowStart = (pageNumber - 1) * pageSize;
    rowEnd = rowStart + pageSize;
    if (rowEnd > rowCount)
        rowEnd = rowCount;

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 宝箱奖励</title><style>"
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
        "background:#fff}button{border:0;border-radius:6px;padding:8px 12px;"
        "background:#175cd3;color:#fff;cursor:pointer}button.danger{background:#b42318}"
        ".inline{display:flex;flex-wrap:wrap;gap:8px;align-items:end}"
        ".muted{color:#98a2b3}.chip{display:inline-block;background:#eef4ff;"
        "color:#175cd3;border-radius:999px;padding:2px 8px;font-size:12px;"
        "margin-right:6px}"
        "</style></head><body><main class=\"wrap\"><header><div>"
        "<h1>宝箱奖励管理</h1>"
        "<p class=\"sub\">配置青铜/白银/黄金宝箱开出物品；保存后立即生效，无需重启。"
        "权重越大越容易抽中。黄金宝箱在未配置任何启用行时仍走旧默认规则；"
        "银/铜箱必须配置后才能开启。</p></div></header>"
        "<nav class=\"tabs\">"
        "<a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab on\" href=\"/?tab=chests\">宝箱奖励</a>"
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

    vm_mock_admin_text_appendf(&page, "<section class=\"card\"><div class=\"inline\">");
    for (u32 i = 0; i < sizeof(chestIds) / sizeof(chestIds[0]); ++i)
    {
        u32 id = chestIds[i];
        vm_mock_admin_text_appendf(
            &page,
            "<a class=\"tab%s\" href=\"/?tab=chests&amp;chest=%u\">%s #%u</a>",
            id == chestItemId ? " on" : "", id,
            vm_net_mock_chest_box_label_utf8(id), id);
    }
    vm_mock_admin_text_appendf(
        &page,
        "</div><p><span class=\"chip\">当前：%s</span>"
        "<span class=\"chip\">钥匙 #%u</span>"
        "<span class=\"chip\">配置 %u 行</span>"
        "<span class=\"chip\">启用 %u</span>"
        "<span class=\"chip\">来源 %s</span></p>",
        vm_net_mock_chest_box_label_utf8(chestItemId),
        vm_net_mock_chest_key_for_box(chestItemId),
        rowCount, enabledCount,
        enabledCount != 0 ? "MySQL server_chest_rewards" :
            (chestItemId == VM_NET_MOCK_GOLD_CHEST_ITEM_ID ?
                 "旧默认规则（尚未导入）" : "空（银/铜需配置）"));

    vm_mock_admin_text_appendf(
        &page,
        "<form class=\"inline\" method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"chest-import-legacy\">"
        "<input type=\"hidden\" name=\"chest\" value=\"%u\">"
        "<button type=\"submit\">导入旧黄金默认池到当前宝箱</button></form>"
        "<form class=\"inline\" method=\"post\" action=\"/action\" "
        "onsubmit=\"return confirm('清空当前宝箱全部奖励？');\">"
        "<input type=\"hidden\" name=\"action\" value=\"chest-clear\">"
        "<input type=\"hidden\" name=\"chest\" value=\"%u\">"
        "<button class=\"danger\" type=\"submit\">清空当前宝箱配置</button></form>"
        "</section>",
        chestItemId, chestItemId);

    {
        bool itemCats[256];
        bool equipCats[256];
        bool qualities[256];
        u32 poolRows = vm_net_mock_chest_reward_admin_count(chestItemId);

        memset(itemCats, 0, sizeof(itemCats));
        memset(equipCats, 0, sizeof(equipCats));
        memset(qualities, 0, sizeof(qualities));
        (void)vm_net_mock_load_equipment_catalog();
        for (u32 i = 0; i < poolRows; ++i)
        {
            const vm_net_mock_chest_reward_row *row =
                vm_net_mock_chest_reward_admin_row_at(chestItemId, i);
            const vm_net_mock_shop_catalog_item *shop = NULL;
            const vm_net_mock_equipment_catalog_item *equip = NULL;

            if (row == NULL)
                continue;
            shop = vm_net_mock_find_shop_catalog_item(row->rewardItemId);
            if (shop == NULL)
                continue;
            if (shop->isEquip)
            {
                equipCats[shop->category] = true;
                equip = vm_net_mock_find_equipment_catalog_item(row->rewardItemId);
                if (equip != NULL)
                    qualities[equip->quality] = true;
            }
            else
            {
                itemCats[shop->category] = true;
            }
        }

        vm_mock_admin_text_appendf(
            &page,
            "<section class=\"card\"><h2>批量启用 / 禁用</h2>"
            "<p class=\"muted\">只作用于当前宝箱已配置的奖励行；按分类或装备品质筛选后批量改状态。</p>"
            "<form class=\"inline\" method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"chest-batch-category\">"
            "<input type=\"hidden\" name=\"chest\" value=\"%u\">"
            "<label><span class=\"muted\">物品 / 装备分类</span><br>"
            "<select name=\"scope\" required>"
            "<option value=\"\" disabled selected>选择分类</option>",
            chestItemId);
        for (u32 c = 0; c < 256; ++c)
        {
            if (!itemCats[c])
                continue;
            vm_mock_admin_text_appendf(
                &page, "<option value=\"i%u\">道具 · %s（%u）</option>", c,
                vm_mock_admin_item_category_name(false, (u8)c), c);
        }
        for (u32 c = 0; c < 256; ++c)
        {
            if (!equipCats[c])
                continue;
            vm_mock_admin_text_appendf(
                &page, "<option value=\"e%u\">装备 · %s（%u）</option>", c,
                vm_mock_admin_item_category_name(true, (u8)c), c);
        }
        vm_mock_admin_text_appendf(
            &page,
            "</select></label>"
            "<button type=\"submit\" name=\"enabled\" value=\"1\">批量启用</button>"
            "<button class=\"danger\" type=\"submit\" name=\"enabled\" value=\"0\">"
            "批量禁用</button></form>"
            "<form class=\"inline\" method=\"post\" action=\"/action\" "
            "style=\"margin-top:12px\">"
            "<input type=\"hidden\" name=\"action\" value=\"chest-batch-quality\">"
            "<input type=\"hidden\" name=\"chest\" value=\"%u\">"
            "<label><span class=\"muted\">装备品质</span><br>"
            "<select name=\"quality\" required>"
            "<option value=\"\" disabled selected>选择品质</option>",
            chestItemId);
        for (u32 q = 0; q < 256; ++q)
        {
            if (!qualities[q])
                continue;
            vm_mock_admin_text_appendf(
                &page, "<option value=\"%u\">品质 %u</option>", q, q);
        }
        vm_mock_admin_text_appendf(
            &page,
            "</select></label>"
            "<button type=\"submit\" name=\"enabled\" value=\"1\">批量启用</button>"
            "<button class=\"danger\" type=\"submit\" name=\"enabled\" value=\"0\">"
            "批量禁用</button></form></section>");
    }

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>添加 / 更新奖励</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"chest-upsert\">"
        "<input type=\"hidden\" name=\"chest\" value=\"%u\">"
        "<div class=\"inline\">",
        chestItemId);
    vm_mock_admin_render_item_picker_field(
        &page, "chest-reward", "reward", "奖励物品", 0, true);
    vm_mock_admin_text_appendf(
        &page,
        "<label><span class=\"muted\">权重</span><br>"
        "<input type=\"number\" name=\"weight\" min=\"1\" max=\"1000000\" "
        "value=\"1\" required style=\"width:110px\"></label>"
        "<label><span class=\"muted\">状态</span><br>"
        "<select name=\"enabled\"><option value=\"1\" selected>启用</option>"
        "<option value=\"0\">禁用</option></select></label>"
        "<button type=\"submit\">保存</button></div></form>");
    vm_mock_admin_render_item_picker_modal(&page);
    vm_mock_admin_text_appendf(&page, "</section>");

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>当前奖励池</h2>"
        "<table><thead><tr><th>物品</th><th>分类/品质</th><th>权重</th><th>状态</th>"
        "<th>操作</th></tr></thead><tbody>");
    if (rowCount == 0)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<tr><td colspan=\"5\" class=\"muted\">尚无配置行。"
            "可点「导入旧黄金默认池」或手动添加。</td></tr>");
    }
    for (u32 i = rowStart; i < rowEnd; ++i)
    {
        const vm_net_mock_chest_reward_row *row =
            vm_net_mock_chest_reward_admin_row_at(chestItemId, i);
        const vm_net_mock_shop_catalog_item *shop = NULL;
        const vm_net_mock_equipment_catalog_item *equip = NULL;
        const char *meta = "-";

        if (row == NULL)
            continue;
        shop = vm_net_mock_find_shop_catalog_item(row->rewardItemId);
        memset(itemNameUtf8, 0, sizeof(itemNameUtf8));
        if (shop != NULL && shop->name[0] != 0)
            vm_net_mock_gbk_label_to_utf8(shop->name, itemNameUtf8,
                                          sizeof(itemNameUtf8));
        vm_mock_admin_text_appendf(&page, "<tr><td>#%u ", row->rewardItemId);
        if (itemNameUtf8[0] != 0)
            vm_mock_admin_text_append_html(&page, itemNameUtf8);
        else
            vm_mock_admin_text_appendf(&page, "<span class=\"muted\">未知</span>");
        vm_mock_admin_text_appendf(&page, "</td><td>");
        if (shop != NULL && shop->isEquip)
        {
            equip = vm_net_mock_find_equipment_catalog_item(row->rewardItemId);
            vm_mock_admin_text_appendf(
                &page, "%s · 品质%u",
                vm_mock_admin_item_category_name(true, shop->category),
                equip != NULL ? equip->quality : 0);
        }
        else if (shop != NULL)
        {
            vm_mock_admin_text_appendf(
                &page, "%s",
                vm_mock_admin_item_category_name(false, shop->category));
        }
        else
        {
            vm_mock_admin_text_appendf(&page, "%s", meta);
        }
        vm_mock_admin_text_appendf(
            &page,
            "</td><td>"
            "<form class=\"inline\" method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"chest-upsert\">"
            "<input type=\"hidden\" name=\"chest\" value=\"%u\">"
            "<input type=\"hidden\" name=\"reward\" value=\"%u\">"
            "<input type=\"number\" name=\"weight\" min=\"1\" max=\"1000000\" "
            "value=\"%u\" style=\"width:90px\" required>"
            "<select name=\"enabled\">"
            "<option value=\"1\"%s>启用</option>"
            "<option value=\"0\"%s>禁用</option></select>"
            "<button type=\"submit\">更新</button></form></td><td>%s</td><td>"
            "<form method=\"post\" action=\"/action\" "
            "onsubmit=\"return confirm('删除该奖励？');\">"
            "<input type=\"hidden\" name=\"action\" value=\"chest-delete\">"
            "<input type=\"hidden\" name=\"chest\" value=\"%u\">"
            "<input type=\"hidden\" name=\"reward\" value=\"%u\">"
            "<button class=\"danger\" type=\"submit\">删除</button></form>"
            "</td></tr>",
            chestItemId, row->rewardItemId, row->weight,
            row->enabled ? " selected" : "",
            row->enabled ? "" : " selected",
            row->enabled ? "启用" : "禁用",
            chestItemId, row->rewardItemId);
    }
    vm_mock_admin_text_appendf(
        &page,
        "</tbody></table><p class=\"muted\">第 %u / %u 页，本页 %u 行。",
        pageNumber, pageCount, rowEnd > rowStart ? (rowEnd - rowStart) : 0);
    if (pageNumber > 1)
        vm_mock_admin_text_appendf(
            &page,
            " <a href=\"/?tab=chests&amp;chest=%u&amp;page=%u\">上一页</a>",
            chestItemId, pageNumber - 1);
    if (pageNumber < pageCount)
        vm_mock_admin_text_appendf(
            &page,
            " <a href=\"/?tab=chests&amp;chest=%u&amp;page=%u\">下一页</a>",
            chestItemId, pageNumber + 1);
    vm_mock_admin_text_appendf(
        &page,
        "</p><p class=\"muted\">表：MySQL <code>server_chest_rewards</code>。"
        "开箱协议仍是 1/7/15；箱/钥配对 522↔813、523↔814、524↔815。</p>"
        "</section></main><script src=\"/admin.js?v=3\" defer></script></body></html>");

    if (page.truncated)
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\">"
                 "<p>宝箱奖励页面超过大小限制。</p>");
}

static void vm_mock_admin_handle_chest_action(vm_mock_service_socket client,
                                              const char *action,
                                              const char *body)
{
    const char *error = NULL;
    u32 chestItemId = VM_NET_MOCK_GOLD_CHEST_ITEM_ID;
    u32 rewardItemId = 0;
    u32 weight = 1;
    char enabledText[8];
    bool enabled = true;

    memset(enabledText, 0, sizeof(enabledText));
    (void)vm_mock_admin_form_u32(body, "chest", 0xffffffffu, &chestItemId);
    if (!vm_net_mock_chest_box_is_supported(chestItemId))
        chestItemId = VM_NET_MOCK_GOLD_CHEST_ITEM_ID;

    if (strcmp(action, "chest-import-legacy") == 0)
    {
        if (!vm_net_mock_chest_reward_admin_import_legacy_defaults(chestItemId,
                                                                   &error))
        {
            vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                          error ? error : "导入失败");
            return;
        }
        vm_mock_admin_redirect_chests(client, chestItemId, "ok",
                                      "已导入旧默认池并热加载");
        return;
    }
    if (strcmp(action, "chest-clear") == 0)
    {
        if (!vm_net_mock_chest_reward_admin_clear(chestItemId, &error))
        {
            vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                          error ? error : "清空失败");
            return;
        }
        vm_mock_admin_redirect_chests(client, chestItemId, "ok",
                                      "已清空当前宝箱配置");
        return;
    }
    if (strcmp(action, "chest-delete") == 0)
    {
        if (!vm_mock_admin_form_u32(body, "reward", 0xffffffffu, &rewardItemId) ||
            rewardItemId == 0 ||
            !vm_net_mock_chest_reward_admin_delete(chestItemId, rewardItemId,
                                                   &error))
        {
            vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                          error ? error : "删除失败");
            return;
        }
        vm_mock_admin_redirect_chests(client, chestItemId, "ok", "已删除奖励");
        return;
    }
    if (strcmp(action, "chest-upsert") == 0)
    {
        if (!vm_mock_admin_form_u32(body, "reward", 0xffffffffu, &rewardItemId) ||
            rewardItemId == 0)
        {
            vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                          "请选择奖励物品");
            return;
        }
        if (!vm_mock_admin_form_u32(body, "weight", 0xffffffffu, &weight) ||
            weight == 0)
            weight = 1;
        if (vm_mock_admin_form_value(body, "enabled", enabledText,
                                     sizeof(enabledText)) &&
            strcmp(enabledText, "0") == 0)
            enabled = false;
        if (!vm_net_mock_chest_reward_admin_upsert(chestItemId, rewardItemId,
                                                   weight, enabled, &error))
        {
            vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                          error ? error : "保存失败");
            return;
        }
        vm_mock_admin_redirect_chests(client, chestItemId, "ok",
                                      "奖励已保存并热加载");
        return;
    }
    if (strcmp(action, "chest-batch-category") == 0 ||
        strcmp(action, "chest-batch-quality") == 0)
    {
        char scope[16];
        char msg[128];
        u32 quality = 0;
        u32 changed = 0;
        bool matchCategory = false;
        bool matchQuality = false;
        bool categoryIsEquip = false;
        u8 category = 0;

        memset(scope, 0, sizeof(scope));
        if (vm_mock_admin_form_value(body, "enabled", enabledText,
                                     sizeof(enabledText)) &&
            strcmp(enabledText, "0") == 0)
            enabled = false;
        else
            enabled = true;

        if (strcmp(action, "chest-batch-category") == 0)
        {
            if (!vm_mock_admin_form_value(body, "scope", scope, sizeof(scope)) ||
                (scope[0] != 'i' && scope[0] != 'e') ||
                !vm_net_mock_parse_u32_strict(scope + 1, &rewardItemId) ||
                rewardItemId > 255)
            {
                vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                              "分类参数无效");
                return;
            }
            matchCategory = true;
            categoryIsEquip = (scope[0] == 'e');
            category = (u8)rewardItemId;
        }
        else
        {
            if (!vm_mock_admin_form_u32(body, "quality", 255u, &quality))
            {
                vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                              "品质参数无效");
                return;
            }
            matchQuality = true;
        }

        if (!vm_net_mock_chest_reward_admin_batch_set_enabled(
                chestItemId, matchCategory, categoryIsEquip, category,
                matchQuality, (u8)quality, enabled, &changed, &error))
        {
            vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                          error ? error : "批量操作失败");
            return;
        }
        snprintf(msg, sizeof(msg), "已%s %u 条奖励",
                 enabled ? "启用" : "禁用", changed);
        vm_mock_admin_redirect_chests(client, chestItemId, "ok", msg);
        return;
    }
    vm_mock_admin_redirect_chests(client, chestItemId, "error",
                                  "不支持的宝箱操作");
}
