/* Chest reward editor included after the common item picker is available.
 * A chest selects exactly one configured row by relative weight; it does not
 * use the monster editor's independent-percent drop semantics. */

static u32 vm_mock_admin_chest_total_weight(
    const vm_net_mock_chest_admin_row *chest)
{
    u32 total = 0;

    if (chest == NULL || chest->rewardCount > VM_NET_MOCK_CHEST_REWARD_MAX)
        return 0;
    for (u8 i = 0; i < chest->rewardCount; ++i)
    {
        if (0xffffffffu - total < chest->rewards[i].weight)
            return 0;
        total += chest->rewards[i].weight;
    }
    return total;
}

static void vm_mock_admin_render_chest_reward_rows(
    vm_mock_admin_text *page, const vm_net_mock_chest_admin_row *chest)
{
    u32 totalWeight = vm_mock_admin_chest_total_weight(chest);
    u32 visibleCount;

    if (page == NULL || chest == NULL)
        return;
    /* All rows are rendered once so the common item picker can bind at page
     * initialization.  The editor exposes configured rows plus one blank row. */
    visibleCount = chest->rewardCount == 0 ? 1u : chest->rewardCount;
    vm_mock_admin_text_appendf(
        page,
        "<div class=\"chest-reward-list\" data-chest-reward-list "
        "data-chest-reward-max=\"%u\"><div class=\"chest-reward-head\">"
        "<span>序号</span><span>物品</span><span>数量</span><span>权重</span>"
        "<span>当前概率</span><span>世界播报</span><span>操作</span></div>",
        VM_NET_MOCK_CHEST_REWARD_MAX);
    for (u8 slot = 0; slot < VM_NET_MOCK_CHEST_REWARD_MAX; ++slot)
    {
        char pickerId[64];
        char itemField[64];
        u32 itemId = slot < chest->rewardCount ? chest->rewards[slot].itemId : 0;
        u32 count = slot < chest->rewardCount ? chest->rewards[slot].count : 0;
        u32 weight = slot < chest->rewardCount ? chest->rewards[slot].weight : 0;
        bool worldBroadcast = slot < chest->rewardCount &&
            chest->rewards[slot].worldBroadcast != 0;
        double probability = totalWeight != 0
                                 ? (100.0 * (double)weight / (double)totalWeight)
                                 : 0.0;

        snprintf(pickerId, sizeof(pickerId), "chest-%u-reward-%u",
                 chest->chestItemId, (u32)slot);
        snprintf(itemField, sizeof(itemField), "reward_item_id_%u", (u32)slot);
        vm_mock_admin_text_appendf(
            page,
            "<div class=\"chest-reward-row\" data-chest-reward-row%s>"
            "<span class=\"slot\" data-chest-reward-index>#%u</span>",
            (u32)slot < visibleCount ? "" : " hidden", (u32)slot + 1u);
        vm_mock_admin_render_item_picker_field(page, pickerId, itemField,
                                               "奖池物品", itemId, false);
        vm_mock_admin_text_appendf(
            page,
            "<label class=\"field\"><span>数量</span><input type=\"number\" "
            "name=\"reward_count_%u\" min=\"0\" max=\"%u\" value=\"%u\" "
            "data-chest-reward-count></label>"
            "<label class=\"field\"><span>相对权重</span><input type=\"number\" "
            "name=\"reward_weight_%u\" min=\"0\" max=\"%u\" value=\"%u\" "
            "data-chest-reward-weight></label>"
            "<span class=\"probability\" data-chest-reward-probability>%.2f%%</span>"
            "<label class=\"broadcast\"><input type=\"checkbox\" "
            "name=\"reward_world_broadcast_%u\" value=\"1\" "
            "data-chest-reward-broadcast%s><span>播报</span></label>"
            "<button class=\"secondary compact\" type=\"button\" "
            "data-chest-reward-remove>移除</button></div>",
            (u32)slot, VM_NET_MOCK_CHEST_REWARD_COUNT_MAX, count,
            (u32)slot, VM_NET_MOCK_CHEST_REWARD_WEIGHT_MAX, weight,
            probability, (u32)slot, worldBroadcast ? " checked" : "");
    }
    vm_mock_admin_text_appendf(page, "</div>");
}

static void vm_mock_admin_render_chest_page(char *response,
                                            size_t responseCap,
                                            const char *query)
{
    vm_mock_admin_text page;
    vm_net_mock_chest_admin_row chests[VM_NET_MOCK_CHEST_KIND_COUNT];
    char status[16];
    char message[256];
    u32 chestCount = 0;
    u32 requestedChest = 0;
    u32 selectedChest = 0;

    memset(chests, 0, sizeof(chests));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    chestCount = vm_net_mock_chest_admin_list(chests,
                                               VM_NET_MOCK_CHEST_KIND_COUNT);
    if (chestCount != 0)
        selectedChest = chests[0].chestItemId;
    if (vm_mock_admin_form_u32(query, "chest", 0xffffffffu, &requestedChest))
    {
        for (u32 i = 0; i < chestCount; ++i)
        {
            if (chests[i].chestItemId == requestedChest)
            {
                selectedChest = requestedChest;
                break;
            }
        }
    }

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 宝箱管理</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1320px;margin:auto;padding:22px 18px 42px}.head{display:flex;justify-content:space-between;gap:16px;align-items:flex-start}.head h1{font-size:24px;margin:0}.sub{color:#667085;margin:4px 0 14px}.tabs{display:flex;gap:6px;margin-bottom:16px;flex-wrap:wrap}.tab{padding:8px 13px;border:1px solid #e4e7ec;border-radius:7px;background:#fff;color:#475467;text-decoration:none}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:#fff;color:#667085;border:1px solid #d0d5dd}.notice{padding:10px 12px;border-radius:7px;margin-bottom:13px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.chests{display:grid;gap:16px}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;box-shadow:0 1px 2px #1018280d}.card-head{display:flex;justify-content:space-between;gap:14px;align-items:flex-start}.card h2{font-size:18px;margin:0}.badge{display:inline-block;margin-left:7px;padding:2px 7px;border-radius:999px;background:#eef4ff;color:#175cd3;font-size:12px}.hint{color:#667085;font-size:12px;margin:7px 0 0}.chest-reward-list{display:grid;gap:8px;margin-top:14px}.chest-reward-head,.chest-reward-row{display:grid;grid-template-columns:44px minmax(220px,1fr) 100px 125px 85px 82px 70px;gap:9px;align-items:end}.chest-reward-head{padding:0 9px;color:#667085;font-size:12px}.chest-reward-row{padding:10px;border:1px solid #e4e7ec;border-radius:8px}.slot{color:#667085;padding-bottom:9px}.field,.item-field{display:grid;gap:4px}.field span,.item-field>span{font-size:12px;color:#667085}input{width:100%%;min-width:0;padding:8px 9px;border:1px solid #d0d5dd;border-radius:6px;background:#fff}.broadcast{display:flex;align-items:center;gap:5px;min-height:39px;padding:8px 0;color:#475467;white-space:nowrap}.broadcast input{width:auto;padding:0}.probability{padding:8px 0;color:#175cd3;font-weight:650;white-space:nowrap}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}.actions button,button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer}.danger{background:#b42318}.item-picker-trigger{width:100%%;min-height:39px;padding:6px 10px;border:1px solid #d0d5dd;background:#fff;color:#344054;text-align:left;display:flex;align-items:center;justify-content:space-between;gap:12px}.item-picker-trigger small{color:#667085;font-weight:400}.item-picker-head-actions{display:flex;gap:8px;align-items:center}.item-picker-head-actions #item-picker-clear{background:#f2f4f7;color:#475467}.item-modal{position:fixed;inset:0;z-index:1000;display:grid;place-items:center;padding:20px;background:#10182899}.item-picker-panel{width:min(780px,100%%);max-height:calc(100vh - 40px);display:flex;flex-direction:column;overflow:hidden;border:1px solid #d0d5dd;border-radius:14px;background:#fff;box-shadow:0 24px 64px #10182840}.item-picker-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;padding:18px 20px 14px;border-bottom:1px solid #eaecf0}.item-picker-head h3{font-size:19px;margin:0}.item-picker-head p{margin:2px 0 0;color:#667085}.item-picker-close{width:34px;height:34px;padding:0;border-radius:8px;background:#f2f4f7;color:#475467;font-size:24px;line-height:1}.item-picker-tools{display:grid;grid-template-columns:minmax(200px,.8fr) minmax(260px,1.2fr);gap:10px;padding:14px 20px 10px}.item-picker-tools label{display:grid;gap:4px}.item-picker-tools label>span{font-size:12px;color:#667085}.item-result-bar{display:flex;justify-content:space-between;gap:12px;padding:0 20px 9px;color:#667085;font-size:12px}.item-picker-error{color:#b42318;font-weight:600}.item-picker-list{display:grid;grid-template-columns:1fr 1fr;gap:8px;min-height:140px;overflow:auto;padding:0 20px 20px}.item-choice{display:grid;gap:2px;padding:10px 12px;border:1px solid #e4e7ec;background:#fff;color:#344054;text-align:left;white-space:normal}.item-choice:hover{border-color:#84adff;background:#f5f8ff}.item-choice strong{font-size:14px}.item-choice span{color:#667085;font-size:12px}.item-picker-empty{margin:12px 20px 24px;padding:24px;border:1px dashed #d0d5dd;border-radius:9px;color:#98a2b3;text-align:center}[hidden]{display:none!important}.modal-open{overflow:hidden}@media(max-width:900px){.wrap{padding:18px 10px}.chest-reward-head{display:none}.chest-reward-row{grid-template-columns:48px 1fr}.chest-reward-row>.item-field{grid-column:2}.chest-reward-row>.field,.chest-reward-row>.broadcast{grid-column:2}.probability{grid-column:2;padding:0}.item-picker-tools,.item-picker-list{grid-template-columns:1fr}}</style><script src=\"/admin.js\" defer></script></head>"
        "<style>.chest-layout{display:grid;grid-template-columns:250px minmax(0,1fr);gap:16px;align-items:start}.chest-sidebar{position:sticky;top:14px;background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:14px;box-shadow:0 1px 2px #1018280d}.chest-sidebar h2{font-size:16px;margin:0}.chest-tabs{display:grid;gap:7px;margin-top:12px}.chest-select{display:flex;justify-content:space-between;align-items:center;gap:8px;width:100%%;border:1px solid #e4e7ec;border-radius:8px;padding:10px;background:#fff;color:#344054;text-align:left;text-decoration:none;cursor:pointer}.chest-select.on{border-color:#175cd3;background:#eef4ff;color:#175cd3}.chest-select small{color:#667085}.chest-reward-head,.chest-reward-row{grid-template-columns:44px minmax(220px,1fr) 100px 125px 85px 82px 70px}.chest-reward-actions{justify-content:flex-start}.secondary{background:#475467!important}.compact{padding:8px 9px!important;font-size:12px}.danger{background:#b42318!important}@media(max-width:900px){.chest-layout{grid-template-columns:1fr}.chest-sidebar{position:static}.chest-tabs{grid-template-columns:repeat(3,minmax(0,1fr))}.chest-select{display:grid;gap:2px;padding:8px}.chest-reward-row>button{grid-column:2;justify-self:start}}</style>"
        "<body><main class=\"wrap\"><div class=\"head\"><div><h1>江湖OL 后台管理</h1><p class=\"sub\">宝箱奖池与加权概率</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\">退出登录</button></form></div>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab on\" href=\"/?tab=chests\">宝箱管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a><a class=\"tab\" href=\"/?tab=servers\">服务器列表</a><a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav>");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(
        &page,
        "<p class=\"hint\">每次开启只抽取一项奖池物品，实际概率 = 该项权重 ÷ 当前宝箱全部权重。物品 DSH 未保存官方奖池或概率，因此新宝箱默认未配置；未配置时不会消耗宝箱或钥匙。</p><section class=\"chest-layout\"><aside class=\"chest-sidebar\"><h2>宝箱列表</h2><p class=\"hint\">选择宝箱后在右侧维护其独立奖池。</p><div class=\"chest-tabs\">");
    if (chestCount != VM_NET_MOCK_CHEST_KIND_COUNT)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"notice error\">宝箱奖池数据库不可用，未显示或保存任何配置。</div>");
    }
    for (u32 i = 0; i < chestCount; ++i)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<a class=\"chest-select%s\" href=\"/?tab=chests&amp;chest=%u\" "
            "aria-current=\"%s\"><span>%s</span><small>%u 项</small></a>",
            chests[i].chestItemId == selectedChest ? " on" : "",
            chests[i].chestItemId,
            chests[i].chestItemId == selectedChest ? "page" : "false",
            g_vm_net_mock_chest_kinds[i].name, chests[i].rewardCount);
    }
    vm_mock_admin_text_appendf(
        &page,
        "</div></aside><section class=\"chest-editor\">");
    for (u32 i = 0; i < chestCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *key =
            vm_net_mock_find_shop_catalog_item(chests[i].keyItemId);
        char keyNameUtf8[128];

        /* A hidden editor still serializes all 120 rows and their picker
         * controls.  Three such forms plus the shared 1,700-item picker can
         * exceed the bounded HTTP response even though only one editor is
         * actionable.  The selected chest is therefore the sole form in the
         * response; sidebar links request the other form when selected. */
        if (chests[i].chestItemId != selectedChest)
            continue;

        memset(keyNameUtf8, 0, sizeof(keyNameUtf8));
        if (key != NULL)
            vm_net_mock_gbk_label_to_utf8(key->name, keyNameUtf8,
                                          sizeof(keyNameUtf8));
        vm_mock_admin_text_appendf(
            &page,
            "<article id=\"chest-%u\" class=\"card chest-card\"><div class=\"card-head\"><div><h2>%s <span class=\"badge\">ID %u</span></h2><p class=\"hint\">开启条件：消耗 1 个 %s（ID %u）。%s</p></div><span class=\"badge\">已配置 %u 项</span></div><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-chest-rewards\"><input type=\"hidden\" name=\"chest_item_id\" value=\"%u\"><input type=\"hidden\" name=\"key_item_id\" value=\"%u\">",
            chests[i].chestItemId,
            g_vm_net_mock_chest_kinds[i].name, chests[i].chestItemId,
            keyNameUtf8[0] ? keyNameUtf8 : g_vm_net_mock_chest_kinds[i].keyName,
            chests[i].keyItemId,
            chests[i].rewardCount == 0 ? "当前未配置，开箱会明确失败且不消耗物品。" :
                                         "保存后立即影响后续开箱。",
            chests[i].rewardCount, chests[i].chestItemId,
            chests[i].keyItemId);
        vm_mock_admin_render_chest_reward_rows(&page, &chests[i]);
        vm_mock_admin_text_appendf(
            &page,
            "<p class=\"hint\">每行需同时填写物品、数量和权重；三项都为 0 的行会忽略。勾选“世界播报”后，开出该项且奖励已落库时会以系统身份发布公告。最多 %u 项；装备、神仙壶和逍遥壶的数量必须为 1；同一物品不能重复出现。</p><div class=\"actions chest-reward-actions\"><button type=\"button\" data-chest-reward-add>＋ 添加奖励</button></div><div class=\"actions\"><button type=\"submit\">保存奖池</button></div></form>",
            VM_NET_MOCK_CHEST_REWARD_MAX);
        if (chests[i].rewardCount != 0)
        {
            vm_mock_admin_text_appendf(
                &page,
                "<form class=\"actions\" method=\"post\" action=\"/action\" onsubmit=\"return confirm('确定清空该宝箱的全部奖池吗？开箱将不再消耗物品。');\"><input type=\"hidden\" name=\"action\" value=\"reset-chest-rewards\"><input type=\"hidden\" name=\"chest_item_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">清空奖池</button></form>",
                chests[i].chestItemId);
        }
        vm_mock_admin_text_appendf(&page, "</article>");
    }
    vm_mock_admin_text_appendf(&page, "</section></section>");
    vm_mock_admin_render_item_picker_modal(&page);
    vm_mock_admin_text_appendf(&page, "</main></body></html>");
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>宝箱管理页面超过大小限制。</p>");
    }
}
