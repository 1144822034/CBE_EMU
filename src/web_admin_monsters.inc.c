/* Monster catalog editor included by web_admin_server.c after the shared
 * mock-server catalogs and HTML helpers are available. */

static const char *vm_mock_admin_monster_family_name(u32 family)
{
    static const char *names[] = {
        "胶质", "野兽", "飞行", "昆虫", "爬行", "亡灵",
        "灵体", "元素", "岩石", "人形", "士兵", "首领"};

    if (family >= sizeof(names) / sizeof(names[0]))
        return "未知";
    return names[family];
}

static void vm_mock_admin_render_monster_family_select(
    vm_mock_admin_text *page, u32 selected)
{
    vm_mock_admin_text_appendf(page, "<select name=\"family\" required>");
    for (u32 family = 0; family <= VM_NET_MOCK_MONSTER_BOSS; ++family)
    {
        vm_mock_admin_text_appendf(
            page, "<option value=\"%u\"%s>%u · %s</option>", family,
            selected == family ? " selected" : "", family,
            vm_mock_admin_monster_family_name(family));
    }
    vm_mock_admin_text_appendf(page, "</select>");
}

static void vm_mock_admin_render_monster_page(char *response,
                                               size_t responseCap,
                                               const char *query)
{
    enum { VM_MOCK_ADMIN_MONSTER_ROWS_MAX = 128 };
    vm_mock_admin_text page;
    vm_net_mock_monster_admin_row monsters[VM_MOCK_ADMIN_MONSTER_ROWS_MAX];
    vm_net_mock_monster_admin_row *edit = NULL;
    char monsterText[32];
    char status[16];
    char message[256];
    char nameUtf8[128];
    char sceneUtf8[192];
    char dropNameUtf8[128];
    u32 monsterCount = 0;
    u32 selectedMonsterId = 0;

    memset(monsters, 0, sizeof(monsters));
    memset(monsterText, 0, sizeof(monsterText));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(sceneUtf8, 0, sizeof(sceneUtf8));
    memset(dropNameUtf8, 0, sizeof(dropNameUtf8));
    (void)vm_mock_admin_form_value(query, "monster", monsterText,
                                   sizeof(monsterText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    if (monsterText[0] != 0)
        (void)vm_net_mock_parse_u32_strict(monsterText, &selectedMonsterId);

    monsterCount = vm_net_mock_monster_admin_list(
        monsters, VM_MOCK_ADMIN_MONSTER_ROWS_MAX);
    if (monsterCount > VM_MOCK_ADMIN_MONSTER_ROWS_MAX)
        monsterCount = VM_MOCK_ADMIN_MONSTER_ROWS_MAX;
    if (selectedMonsterId == 0 && monsterCount != 0)
        selectedMonsterId = monsters[0].enemyId;
    for (u32 i = 0; i < monsterCount; ++i)
    {
        if (monsters[i].enemyId == selectedMonsterId)
        {
            edit = &monsters[i];
            break;
        }
    }

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 怪物管理</title><style>"
        "*{box-sizing:border-box}html,body{height:100%%;overflow:hidden}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1320px;height:100vh;margin:auto;padding:22px 18px;display:flex;flex-direction:column}.head{display:flex;justify-content:space-between;gap:16px;align-items:flex-start}.head h1{font-size:24px;margin:0}.sub{color:#667085;margin:4px 0 14px}.tabs{display:flex;gap:6px;margin-bottom:16px;flex-wrap:wrap}.tab{padding:8px 13px;border:1px solid #e4e7ec;border-radius:7px;background:#fff;color:#475467;text-decoration:none}.tab.on{background:#175cd3;color:#fff;border-color:#175cd3}.logout{background:#fff;color:#667085;border:1px solid #d0d5dd}.grid{display:grid;grid-template-columns:340px minmax(0,1fr);gap:16px;flex:1;min-height:0}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:15px;box-shadow:0 1px 2px #1018280d}.catalog{display:flex;flex-direction:column;min-height:0}.search{margin-bottom:10px}.list{overflow:auto;display:flex;flex-direction:column;gap:4px;margin-top:9px}.monster{padding:8px 9px;border-radius:6px;color:#344054;text-decoration:none;border:1px solid transparent}.monster:hover,.monster.on{background:#eef4ff;color:#175cd3}.monster small{display:block;color:#667085}.monster.override{border-color:#fdb022}.editor{overflow:auto}.badge{font-size:12px;padding:2px 7px;border-radius:999px;background:#eef4ff;color:#175cd3}.badge.override{background:#fffaeb;color:#b54708}.notice{padding:10px 12px;border-radius:7px;margin-bottom:13px}.ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}.summary{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 16px}.chip{padding:3px 8px;border-radius:999px;background:#f2f4f7;color:#475467}.fields{display:grid;grid-template-columns:repeat(4,minmax(110px,1fr));gap:10px}.field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}.group{padding:13px;border:1px solid #e4e7ec;border-radius:8px;margin-top:13px}.group h2{font-size:16px;margin:0 0 10px}input,select{width:100%%;min-width:0;padding:8px 9px;border:1px solid #d0d5dd;border-radius:6px;background:#fff}button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;color:#fff;cursor:pointer}.danger{background:#b42318}.actions{display:flex;justify-content:flex-end;gap:8px;margin-top:13px}.hint{color:#667085;font-size:12px;margin:8px 0 0}@media(max-width:900px){html,body{height:auto;overflow:auto}.wrap{height:auto}.grid{grid-template-columns:1fr}.catalog{max-height:420px}.fields{grid-template-columns:1fr 1fr}}</style>"
        "</head><body><main class=\"wrap\"><div class=\"head\"><div><h1>江湖OL 后台管理</h1><p class=\"sub\">怪物属性、战斗奖励与掉落覆盖</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\">退出登录</button></form></div>"
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab on\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=shop\">商品管理</a><a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a></nav>"
        "<div class=\"grid\"><aside class=\"card catalog\"><input class=\"search\" id=\"monster-search\" placeholder=\"按 ID、名称或场景筛选\"><strong>怪物目录（%u）</strong><div class=\"list\" id=\"monster-list\">",
        monsterCount);

    for (u32 i = 0; i < monsterCount; ++i)
    {
        char rowNameUtf8[128];
        char rowSceneUtf8[192];

        memset(rowNameUtf8, 0, sizeof(rowNameUtf8));
        memset(rowSceneUtf8, 0, sizeof(rowSceneUtf8));
        vm_net_mock_gbk_label_to_utf8(monsters[i].displayName, rowNameUtf8,
                                      sizeof(rowNameUtf8));
        vm_net_mock_gbk_label_to_utf8(monsters[i].firstScene, rowSceneUtf8,
                                      sizeof(rowSceneUtf8));
        vm_mock_admin_text_appendf(
            &page,
            "<a class=\"monster%s%s\" data-key=\"%u ",
            monsters[i].enemyId == selectedMonsterId ? " on" : "",
            monsters[i].overridden ? " override" : "", monsters[i].enemyId);
        vm_mock_admin_text_append_html(&page, rowNameUtf8);
        vm_mock_admin_text_appendf(&page, " ");
        vm_mock_admin_text_append_html(&page, rowSceneUtf8);
        vm_mock_admin_text_appendf(
            &page, "\" href=\"/?tab=monsters&amp;monster=%u\"><strong>#%u · ",
            monsters[i].enemyId, monsters[i].enemyId);
        if (rowNameUtf8[0] != 0)
            vm_mock_admin_text_append_html(&page, rowNameUtf8);
        else
            vm_mock_admin_text_appendf(&page, "未命名怪物");
        vm_mock_admin_text_appendf(
            &page, "</strong><small>Lv.%u · %s%s</small></a>",
            monsters[i].level,
            vm_mock_admin_monster_family_name(monsters[i].family),
            monsters[i].overridden ? " · 已编辑" : "");
    }
    vm_mock_admin_text_appendf(
        &page, "</div></aside><section class=\"card editor\">");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    if (edit == NULL)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<p>没有可编辑的怪物。</p></section></div></main></body></html>");
        return;
    }

    vm_net_mock_gbk_label_to_utf8(edit->displayName, nameUtf8,
                                  sizeof(nameUtf8));
    vm_net_mock_gbk_label_to_utf8(edit->firstScene, sceneUtf8,
                                  sizeof(sceneUtf8));
    if (edit->dropItemId != 0)
    {
        const vm_net_mock_shop_catalog_item *dropItem =
            vm_net_mock_find_shop_catalog_item(edit->dropItemId);
        if (dropItem != NULL)
            vm_net_mock_gbk_label_to_utf8(dropItem->name, dropNameUtf8,
                                          sizeof(dropNameUtf8));
    }
    vm_mock_admin_text_appendf(&page, "<h2>#%u · ", edit->enemyId);
    if (nameUtf8[0] != 0)
        vm_mock_admin_text_append_html(&page, nameUtf8);
    else
        vm_mock_admin_text_appendf(&page, "未命名怪物");
    vm_mock_admin_text_appendf(
        &page, " <span class=\"badge%s\">%s</span></h2><div class=\"summary\"><span class=\"chip\">首次场景：",
        edit->overridden ? " override" : "",
        edit->overridden ? "MySQL 覆盖" : "服务端默认");
    if (sceneUtf8[0] != 0)
        vm_mock_admin_text_append_html(&page, sceneUtf8);
    else
        vm_mock_admin_text_appendf(&page, "任务／特殊挑战目录");
    vm_mock_admin_text_appendf(
        &page,
        "</span><span class=\"chip\">类型：%s</span></div><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-monster\"><input type=\"hidden\" name=\"monster_id\" value=\"%u\"><div class=\"group\"><h2>基础配置</h2><div class=\"fields\"><label class=\"field\"><span>怪物 ID（只读）</span><input value=\"%u\" readonly></label><label class=\"field\"><span>等级</span><input type=\"number\" name=\"level\" min=\"1\" max=\"255\" value=\"%u\" required></label><label class=\"field\"><span>怪物类型</span>",
        vm_mock_admin_monster_family_name(edit->family), edit->enemyId,
        edit->enemyId, edit->level);
    vm_mock_admin_render_monster_family_select(&page, edit->family);
    vm_mock_admin_text_appendf(
        &page,
        "</label><label class=\"field\"><span>属性来源</span><input value=\"%s\" readonly></label></div></div>"
        "<div class=\"group\"><h2>战斗属性</h2><div class=\"fields\"><label class=\"field\"><span>HP</span><input type=\"number\" name=\"hp\" min=\"1\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>MP</span><input type=\"number\" name=\"mp\" min=\"1\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>攻击</span><input type=\"number\" name=\"attack\" min=\"1\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>防御</span><input type=\"number\" name=\"defense\" min=\"0\" max=\"2147483647\" value=\"%u\" required></label></div></div>"
        "<div class=\"group\"><h2>结算与掉落</h2><div class=\"fields\"><label class=\"field\"><span>经验奖励</span><input type=\"number\" name=\"exp\" min=\"0\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>铜钱奖励</span><input type=\"number\" name=\"gold\" min=\"0\" max=\"2147483647\" value=\"%u\" required></label><label class=\"field\"><span>掉落物品 ID</span><input type=\"number\" name=\"drop_item_id\" min=\"0\" max=\"4294967295\" value=\"%u\" required></label><label class=\"field\"><span>掉落概率（%%）</span><input type=\"number\" name=\"drop_rate\" min=\"0\" max=\"100\" value=\"%u\" required></label></div><p class=\"hint\">掉落物品：%s。无掉落时物品 ID 和概率都填 0。</p></div><p class=\"hint\">保存后立即影响普通场景战斗、副本挑战、挂机战斗和结算。怪物名称来自真实 SCE，只读；调整等级或类型不会擅自覆盖手工填写的战斗数值。</p><div class=\"actions\"><button type=\"submit\">保存怪物属性</button></div></form>",
        edit->overridden ? "MySQL 覆盖" : "服务端公式",
        edit->hp, edit->mp, edit->attack, edit->defense, edit->exp, edit->gold,
        edit->dropItemId, edit->dropRatePercent,
        dropNameUtf8[0] != 0 ? dropNameUtf8 :
        (edit->dropItemId != 0 ? "未知物品" : "无"));
    if (edit->overridden)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<form class=\"actions\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"reset-monster\"><input type=\"hidden\" name=\"monster_id\" value=\"%u\"><button class=\"danger\" type=\"submit\">恢复服务端默认</button></form>",
            edit->enemyId);
    }
    vm_mock_admin_text_appendf(
        &page,
        "<script>(()=>{const q=document.getElementById('monster-search'),rows=[...document.querySelectorAll('#monster-list .monster')];if(!q)return;q.addEventListener('input',()=>{const v=q.value.trim().toLowerCase();for(const row of rows)row.hidden=v&&!row.dataset.key.toLowerCase().includes(v);});})();</script></section></div></main></body></html>");
    if (page.truncated)
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>怪物管理页面超过大小限制。</p>");
}
