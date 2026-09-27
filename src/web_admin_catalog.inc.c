/* Custom item / equip / skill catalog admin UI.
 * Included by web_admin_server.c after form helpers. */

static void vm_mock_admin_redirect_catalog(vm_mock_service_socket client,
                                          const char *kind, u32 id,
                                          const char *status,
                                          const char *message)
{
    char location[512];
    char encoded[384];

    if (kind == NULL || kind[0] == 0)
        kind = "item";
    memset(encoded, 0, sizeof(encoded));
    if (message != NULL && message[0] != 0)
        vm_mock_admin_url_encode(message, encoded, sizeof(encoded));
    if (id != 0)
    {
        snprintf(location, sizeof(location),
                 VM_MOCK_ADMIN_ROOT_PATH
                 "?tab=catalog&kind=%s&id=%u&status=%s&message=%s",
                 kind, id, status ? status : "ok", encoded);
    }
    else
    {
        snprintf(location, sizeof(location),
                 VM_MOCK_ADMIN_ROOT_PATH
                 "?tab=catalog&kind=%s&status=%s&message=%s",
                 kind, status ? status : "ok", encoded);
    }
    vm_mock_admin_send_location(client, location, NULL);
}

static const char *vm_mock_admin_catalog_kind_label(const char *kind)
{
    if (kind != NULL && strcmp(kind, "equip") == 0)
        return "装备";
    if (kind != NULL && strcmp(kind, "skill") == 0)
        return "技能";
    return "道具";
}

static void vm_mock_admin_render_catalog_page(char *response, size_t responseCap,
                                             const char *query)
{
    vm_mock_admin_text page;
    char kind[16];
    char idText[32];
    char newText[16];
    char status[16];
    char message[256];
    char nameUtf8[128];
    char descUtf8[384];
    u32 selectedId = 0;
    bool createNew = false;
    bool isEquip = false;
    bool isSkill = false;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(kind, 0, sizeof(kind));
    memset(idText, 0, sizeof(idText));
    memset(newText, 0, sizeof(newText));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "kind", kind, sizeof(kind));
    (void)vm_mock_admin_form_value(query, "id", idText, sizeof(idText));
    (void)vm_mock_admin_form_value(query, "new", newText, sizeof(newText));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    if (strcmp(kind, "equip") != 0 && strcmp(kind, "skill") != 0)
        snprintf(kind, sizeof(kind), "%s", "item");
    isEquip = strcmp(kind, "equip") == 0;
    isSkill = strcmp(kind, "skill") == 0;
    createNew = strcmp(newText, "1") == 0;
    if (!createNew && idText[0] != 0)
        (void)vm_net_mock_parse_u32_strict(idText, &selectedId);

    (void)vm_net_mock_catalog_extra_db_force_reload();
    (void)vm_net_mock_load_shop_catalog();
    (void)vm_net_mock_load_skill_catalog();

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 目录管理</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#1f2937;"
        "font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}"
        ".wrap{max-width:1200px;margin:auto;padding:22px 18px}"
        ".head{display:flex;justify-content:space-between;gap:16px}"
        ".head h1{font-size:24px;margin:0}.sub{color:#667085;margin:4px 0 14px}"
        ".logout{background:#fff;color:#667085;border:1px solid #d0d5dd;"
        "border-radius:6px;padding:8px 12px;cursor:pointer}"
        ".grid{display:grid;grid-template-columns:300px minmax(0,1fr);gap:16px}"
        ".card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;"
        "padding:15px}.list{display:flex;flex-direction:column;gap:4px;"
        "max-height:70vh;overflow:auto}.row{padding:8px 9px;border-radius:6px;"
        "color:#344054;text-decoration:none;border:1px solid transparent}"
        ".row:hover,.row.on{background:#eef4ff;color:#175cd3}"
        ".row small{display:block;color:#667085}"
        ".notice{padding:10px 12px;border-radius:7px;margin-bottom:13px}"
        ".ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}"
        ".fields{display:grid;grid-template-columns:repeat(3,minmax(120px,1fr));gap:10px}"
        ".field{display:grid;gap:4px}.field span{font-size:12px;color:#667085}"
        "input,select,textarea{width:100%%;padding:8px 9px;border:1px solid #d0d5dd;"
        "border-radius:6px}textarea{min-height:72px}"
        "button,.button{border:0;border-radius:6px;padding:8px 12px;background:#175cd3;"
        "color:#fff;cursor:pointer;text-decoration:none;display:inline-block}"
        ".danger{background:#b42318}.actions{display:flex;gap:8px;justify-content:flex-end;"
        "margin-top:14px}.hint{color:#667085;font-size:12px;margin-top:8px}"
        ".kind-tabs{display:flex;gap:8px;margin:0 0 12px}"
        ".kind-tabs a{padding:6px 10px;border-radius:6px;border:1px solid #e4e7ec;"
        "text-decoration:none;color:#475467;background:#fff}"
        ".kind-tabs a.on{background:#175cd3;color:#fff;border-color:#175cd3}"
        "@media(max-width:900px){.grid{grid-template-columns:1fr}.fields{grid-template-columns:1fr 1fr}}"
        "</style></head><body><main class=\"wrap\"><div class=\"head\"><div>"
        "<h1>江湖OL 后台管理</h1><p class=\"sub\">自定义道具 / 装备 / 技能"
        "（ID 自动 max+1；仅 Pixi）</p></div>"
        "<form method=\"post\" action=\"/logout\"><button class=\"logout\">退出登录</button>"
        "</form></div>");
    vm_mock_admin_append_tabs(&page, "catalog");
    vm_mock_admin_text_appendf(
        &page,
        "<div class=\"kind-tabs\">"
        "<a class=\"%s\" href=\"/?tab=catalog&amp;kind=item\">道具</a>"
        "<a class=\"%s\" href=\"/?tab=catalog&amp;kind=equip\">装备</a>"
        "<a class=\"%s\" href=\"/?tab=catalog&amp;kind=skill\">技能</a></div>",
        !isEquip && !isSkill ? "on" : "", isEquip ? "on" : "",
        isSkill ? "on" : "");
    vm_mock_admin_text_appendf(
        &page,
        "<div class=\"grid\"><aside class=\"card\"><a class=\"button\" "
        "href=\"/?tab=catalog&amp;kind=%s&amp;new=1\">＋ 新增%s</a>"
        "<p class=\"hint\">仅列出自定义行；内置 DSH 请用商品管理改价。</p>"
        "<strong>%s目录</strong><div class=\"list\">",
        kind, vm_mock_admin_catalog_kind_label(kind),
        vm_mock_admin_catalog_kind_label(kind));

    if (!isEquip && !isSkill)
    {
        for (u32 i = 0; i < g_vm_net_mock_item_catalog_extra_count; ++i)
        {
            const vm_net_mock_item_catalog_extra_row *row =
                &g_vm_net_mock_item_catalog_extra[i];
            memset(nameUtf8, 0, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<a class=\"row%s\" href=\"/?tab=catalog&amp;kind=item&amp;id=%u\">"
                "<strong>#%u · ",
                (!createNew && selectedId == row->itemId) ? " on" : "",
                row->itemId, row->itemId);
            vm_mock_admin_text_append_html(&page, nameUtf8[0] ? nameUtf8 : "未命名");
            vm_mock_admin_text_appendf(
                &page, "</strong><small>类别 %u · 堆叠 %u</small></a>",
                row->category, row->stack);
        }
    }
    else if (isEquip)
    {
        for (u32 i = 0; i < g_vm_net_mock_equip_catalog_extra_count; ++i)
        {
            const vm_net_mock_equip_catalog_extra_row *row =
                &g_vm_net_mock_equip_catalog_extra[i];
            memset(nameUtf8, 0, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<a class=\"row%s\" href=\"/?tab=catalog&amp;kind=equip&amp;id=%u\">"
                "<strong>#%u · ",
                (!createNew && selectedId == row->itemId) ? " on" : "",
                row->itemId, row->itemId);
            vm_mock_admin_text_append_html(&page, nameUtf8[0] ? nameUtf8 : "未命名");
            vm_mock_admin_text_appendf(
                &page, "</strong><small>类别 %u · 品质 %u</small></a>",
                row->category, row->quality);
        }
    }
    else
    {
        for (u32 i = 0; i < g_vm_net_mock_skill_catalog_extra_count; ++i)
        {
            const vm_net_mock_skill_catalog_extra_row *row =
                &g_vm_net_mock_skill_catalog_extra[i];
            memset(nameUtf8, 0, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<a class=\"row%s\" href=\"/?tab=catalog&amp;kind=skill&amp;id=%u\">"
                "<strong>#%u · ",
                (!createNew && selectedId == row->skillId) ? " on" : "",
                row->skillId, row->skillId);
            vm_mock_admin_text_append_html(&page, nameUtf8[0] ? nameUtf8 : "未命名");
            vm_mock_admin_text_appendf(
                &page, "</strong><small>职业 %u · MP %u</small></a>",
                row->rawJob, row->mpCost);
        }
    }

    vm_mock_admin_text_appendf(&page, "</div></aside><section class=\"card\">");
    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }

    if (createNew && !isEquip && !isSkill)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<h2>新增道具</h2><p class=\"hint\">保存后自动分配物品 ID（当前目录 "
            "max+1）。特殊协议道具仍需改码。</p>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"create-catalog-item\">"
            "<div class=\"fields\">"
            "<label class=\"field\"><span>名称</span><input name=\"name\" required maxlength=\"15\"></label>"
            "<label class=\"field\"><span>类别</span><input name=\"category\" type=\"number\" min=\"0\" max=\"255\" value=\"10\" required></label>"
            "<label class=\"field\"><span>需求等级</span><input name=\"level_required\" type=\"number\" min=\"1\" max=\"255\" value=\"1\" required></label>"
            "<label class=\"field\"><span>堆叠</span><input name=\"stack\" type=\"number\" min=\"1\" max=\"255\" value=\"99\" required></label>"
            "<label class=\"field\"><span>消耗模式</span><input name=\"consume_mode\" type=\"number\" min=\"0\" max=\"255\" value=\"1\" required></label>"
            "<label class=\"field\"><span>持续(分)</span><input name=\"duration_minutes\" type=\"number\" min=\"0\" max=\"255\" value=\"0\"></label>"
            "<label class=\"field\"><span>HP</span><input name=\"hp\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>MP</span><input name=\"mp\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>EXP</span><input name=\"exp\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>任务关联</span><input name=\"quest_link\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>绑定</span><input name=\"bind_attr\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>图标 visual</span><input name=\"visual\" type=\"number\" min=\"1\" max=\"255\" value=\"1\"></label>"
            "<label class=\"field\"><span>铜价格</span><input name=\"price\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>酷宝价格</span><input name=\"cool_price\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>上架商城</span><select name=\"shop_enabled\"><option value=\"0\">否</option><option value=\"1\">是</option></select></label>"
            "</div>"
            "<label class=\"field\" style=\"margin-top:10px\"><span>说明</span>"
            "<textarea name=\"description\" maxlength=\"90\"></textarea></label>"
            "<div class=\"actions\"><button type=\"submit\">创建</button></div></form>");
    }
    else if (createNew && isEquip)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<h2>新增装备</h2><p class=\"hint\">类别：0帽 1衣 2披风 3腰带 4腿 5鞋 6饰 "
            "7剑 8匕 9杖。ID 自动分配。</p>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"create-catalog-equip\">"
            "<div class=\"fields\">"
            "<label class=\"field\"><span>名称</span><input name=\"name\" required maxlength=\"15\"></label>"
            "<label class=\"field\"><span>类别</span><input name=\"category\" type=\"number\" min=\"0\" max=\"9\" value=\"0\" required></label>"
            "<label class=\"field\"><span>需求等级</span><input name=\"level_required\" type=\"number\" min=\"1\" max=\"255\" value=\"1\" required></label>"
            "<label class=\"field\"><span>品质</span><input name=\"quality\" type=\"number\" min=\"0\" max=\"255\" value=\"0\"></label>"
            "<label class=\"field\"><span>耐久上限</span><input name=\"durability_max\" type=\"number\" min=\"1\" max=\"65535\" value=\"50\" required></label>"
            "<label class=\"field\"><span>攻击</span><input name=\"attack\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>防御</span><input name=\"armor\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>HP</span><input name=\"hp\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>MP</span><input name=\"mp\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>力量</span><input name=\"strength\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>敏捷</span><input name=\"agility\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>智慧</span><input name=\"wisdom\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>闪躲</span><input name=\"dodge\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>命中</span><input name=\"hit\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>暴击</span><input name=\"crit\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>抗性</span><input name=\"resist\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>铜价格</span><input name=\"price\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>上架商城</span><select name=\"shop_enabled\"><option value=\"0\">否</option><option value=\"1\">是</option></select></label>"
            "</div>"
            "<label class=\"field\" style=\"margin-top:10px\"><span>说明</span>"
            "<textarea name=\"description\" maxlength=\"90\"></textarea></label>"
            "<div class=\"actions\"><button type=\"submit\">创建</button></div></form>");
    }
    else if (createNew && isSkill)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<h2>新增技能</h2><p class=\"hint\">职业 0天机 1幻剑 2鬼道；目标指向 "
            "3单敌/2友群等。特效复用已有 effect_index。</p>"
            "<form method=\"post\" action=\"/action\">"
            "<input type=\"hidden\" name=\"action\" value=\"create-catalog-skill\">"
            "<div class=\"fields\">"
            "<label class=\"field\"><span>名称</span><input name=\"name\" required maxlength=\"15\"></label>"
            "<label class=\"field\"><span>职业</span><input name=\"raw_job\" type=\"number\" min=\"0\" max=\"2\" value=\"0\" required></label>"
            "<label class=\"field\"><span>需求等级</span><input name=\"level_required\" type=\"number\" min=\"1\" max=\"255\" value=\"1\" required></label>"
            "<label class=\"field\"><span>特效索引</span><input name=\"effect_index\" type=\"number\" min=\"0\" value=\"1\" required></label>"
            "<label class=\"field\"><span>学习铜价</span><input name=\"learn_price\" type=\"number\" min=\"0\" value=\"50\"></label>"
            "<label class=\"field\"><span>耗蓝</span><input name=\"mp_cost\" type=\"number\" min=\"0\" value=\"10\"></label>"
            "<label class=\"field\"><span>生命变化</span><input name=\"hp_change\" type=\"number\" value=\"-50\"></label>"
            "<label class=\"field\"><span>目标指向</span><input name=\"target_direction\" type=\"number\" min=\"0\" max=\"4\" value=\"3\"></label>"
            "<label class=\"field\"><span>使用限制</span><input name=\"use_limit\" type=\"number\" min=\"0\" max=\"255\" value=\"0\"></label>"
            "<label class=\"field\"><span>力量系数</span><input name=\"strength_coeff\" type=\"number\" min=\"0\" value=\"50\"></label>"
            "<label class=\"field\"><span>敏捷系数</span><input name=\"agility_coeff\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>智慧系数</span><input name=\"wisdom_coeff\" type=\"number\" min=\"0\" value=\"0\"></label>"
            "<label class=\"field\"><span>效果种类</span><input name=\"effect_kind\" type=\"number\" min=\"0\" max=\"255\" value=\"0\"></label>"
            "<label class=\"field\"><span>持续回合</span><input name=\"duration_rounds\" type=\"number\" min=\"0\" max=\"255\" value=\"0\"></label>"
            "</div>"
            "<label class=\"field\" style=\"margin-top:10px\"><span>说明</span>"
            "<textarea name=\"description\" maxlength=\"90\"></textarea></label>"
            "<div class=\"actions\"><button type=\"submit\">创建</button></div></form>");
    }
    else if (!isEquip && !isSkill && selectedId != 0)
    {
        int idx = vm_net_mock_item_catalog_extra_index(selectedId);
        const vm_net_mock_item_catalog_extra_row *row =
            idx >= 0 ? &g_vm_net_mock_item_catalog_extra[idx] : NULL;
        if (row == NULL)
        {
            vm_mock_admin_text_appendf(&page, "<p>未找到自定义道具 #%u。</p>",
                                       selectedId);
        }
        else
        {
            memset(nameUtf8, 0, sizeof(nameUtf8));
            memset(descUtf8, 0, sizeof(descUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->description, descUtf8,
                                          sizeof(descUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<h2>编辑道具 #%u</h2><p class=\"hint\">ID 不可改。</p>"
                "<form method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"save-catalog-item\">"
                "<input type=\"hidden\" name=\"item_id\" value=\"%u\">"
                "<div class=\"fields\">"
                "<label class=\"field\"><span>名称</span><input name=\"name\" required maxlength=\"15\" value=\"",
                row->itemId, row->itemId);
            vm_mock_admin_text_append_html(&page, nameUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "\"></label>"
                "<label class=\"field\"><span>类别</span><input name=\"category\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>需求等级</span><input name=\"level_required\" type=\"number\" min=\"1\" max=\"255\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>堆叠</span><input name=\"stack\" type=\"number\" min=\"1\" max=\"255\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>消耗模式</span><input name=\"consume_mode\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>持续(分)</span><input name=\"duration_minutes\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\"></label>"
                "<label class=\"field\"><span>HP</span><input name=\"hp\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>MP</span><input name=\"mp\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>EXP</span><input name=\"exp\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>任务关联</span><input name=\"quest_link\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>绑定</span><input name=\"bind_attr\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>图标 visual</span><input name=\"visual\" type=\"number\" min=\"1\" max=\"255\" value=\"%u\"></label>"
                "<label class=\"field\"><span>铜价格</span><input name=\"price\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>酷宝价格</span><input name=\"cool_price\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>上架商城</span><select name=\"shop_enabled\">"
                "<option value=\"0\"%s>否</option><option value=\"1\"%s>是</option></select></label>"
                "</div>"
                "<label class=\"field\" style=\"margin-top:10px\"><span>说明</span><textarea name=\"description\" maxlength=\"90\">",
                row->category, row->levelRequired, row->stack, row->consumeMode,
                row->durationMinutes, row->hp, row->mp, row->exp, row->questLink,
                row->bindAttr, row->visual, row->price, row->coolPrice,
                row->shopEnabled ? "" : " selected",
                row->shopEnabled ? " selected" : "");
            vm_mock_admin_text_append_html(&page, descUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "</textarea></label>"
                "<div class=\"actions\">"
                "<button type=\"submit\">保存</button></div></form>"
                "<form method=\"post\" action=\"/action\" style=\"margin-top:10px\" "
                "onsubmit=\"return confirm('确认删除该自定义道具？');\">"
                "<input type=\"hidden\" name=\"action\" value=\"delete-catalog-item\">"
                "<input type=\"hidden\" name=\"item_id\" value=\"%u\">"
                "<button class=\"danger\" type=\"submit\">删除</button></form>",
                row->itemId);
        }
    }
    else if (isEquip && selectedId != 0)
    {
        int idx = vm_net_mock_equip_catalog_extra_index(selectedId);
        const vm_net_mock_equip_catalog_extra_row *row =
            idx >= 0 ? &g_vm_net_mock_equip_catalog_extra[idx] : NULL;
        if (row == NULL)
        {
            vm_mock_admin_text_appendf(&page, "<p>未找到自定义装备 #%u。</p>",
                                       selectedId);
        }
        else
        {
            memset(nameUtf8, 0, sizeof(nameUtf8));
            memset(descUtf8, 0, sizeof(descUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->description, descUtf8,
                                          sizeof(descUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<h2>编辑装备 #%u</h2>"
                "<form method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"save-catalog-equip\">"
                "<input type=\"hidden\" name=\"item_id\" value=\"%u\">"
                "<div class=\"fields\">"
                "<label class=\"field\"><span>名称</span><input name=\"name\" required maxlength=\"15\" value=\"",
                row->itemId, row->itemId);
            vm_mock_admin_text_append_html(&page, nameUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "\"></label>"
                "<label class=\"field\"><span>类别</span><input name=\"category\" type=\"number\" min=\"0\" max=\"9\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>需求等级</span><input name=\"level_required\" type=\"number\" min=\"1\" max=\"255\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>品质</span><input name=\"quality\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\"></label>"
                "<label class=\"field\"><span>耐久上限</span><input name=\"durability_max\" type=\"number\" min=\"1\" max=\"65535\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>攻击</span><input name=\"attack\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>防御</span><input name=\"armor\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>HP</span><input name=\"hp\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>MP</span><input name=\"mp\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>力量</span><input name=\"strength\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>敏捷</span><input name=\"agility\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>智慧</span><input name=\"wisdom\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>闪躲</span><input name=\"dodge\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>命中</span><input name=\"hit\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>暴击</span><input name=\"crit\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>抗性</span><input name=\"resist\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>铜价格</span><input name=\"price\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>上架商城</span><select name=\"shop_enabled\">"
                "<option value=\"0\"%s>否</option><option value=\"1\"%s>是</option></select></label>"
                "</div>"
                "<label class=\"field\" style=\"margin-top:10px\"><span>说明</span><textarea name=\"description\" maxlength=\"90\">",
                row->category, row->levelRequired, row->quality,
                row->durabilityMax, row->bonus.attack, row->bonus.armor,
                row->bonus.hp, row->bonus.mp, row->bonus.strength,
                row->bonus.agility, row->bonus.wisdom, row->bonus.dodge,
                row->bonus.hit, row->bonus.crit, row->bonus.resist, row->price,
                row->shopEnabled ? "" : " selected",
                row->shopEnabled ? " selected" : "");
            vm_mock_admin_text_append_html(&page, descUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "</textarea></label>"
                "<div class=\"actions\"><button type=\"submit\">保存</button></div></form>"
                "<form method=\"post\" action=\"/action\" style=\"margin-top:10px\" "
                "onsubmit=\"return confirm('确认删除该自定义装备？');\">"
                "<input type=\"hidden\" name=\"action\" value=\"delete-catalog-equip\">"
                "<input type=\"hidden\" name=\"item_id\" value=\"%u\">"
                "<button class=\"danger\" type=\"submit\">删除</button></form>",
                row->itemId);
        }
    }
    else if (isSkill && selectedId != 0)
    {
        int idx = vm_net_mock_skill_catalog_extra_index(selectedId);
        const vm_net_mock_skill_catalog_extra_row *row =
            idx >= 0 ? &g_vm_net_mock_skill_catalog_extra[idx] : NULL;
        if (row == NULL)
        {
            vm_mock_admin_text_appendf(&page, "<p>未找到自定义技能 #%u。</p>",
                                       selectedId);
        }
        else
        {
            memset(nameUtf8, 0, sizeof(nameUtf8));
            memset(descUtf8, 0, sizeof(descUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8, sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->description, descUtf8,
                                          sizeof(descUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<h2>编辑技能 #%u</h2>"
                "<form method=\"post\" action=\"/action\">"
                "<input type=\"hidden\" name=\"action\" value=\"save-catalog-skill\">"
                "<input type=\"hidden\" name=\"skill_id\" value=\"%u\">"
                "<div class=\"fields\">"
                "<label class=\"field\"><span>名称</span><input name=\"name\" required maxlength=\"15\" value=\"",
                row->skillId, row->skillId);
            vm_mock_admin_text_append_html(&page, nameUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "\"></label>"
                "<label class=\"field\"><span>职业</span><input name=\"raw_job\" type=\"number\" min=\"0\" max=\"2\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>需求等级</span><input name=\"level_required\" type=\"number\" min=\"1\" max=\"255\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>特效索引</span><input name=\"effect_index\" type=\"number\" min=\"0\" value=\"%u\" required></label>"
                "<label class=\"field\"><span>学习铜价</span><input name=\"learn_price\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>耗蓝</span><input name=\"mp_cost\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>生命变化</span><input name=\"hp_change\" type=\"number\" value=\"%d\"></label>"
                "<label class=\"field\"><span>目标指向</span><input name=\"target_direction\" type=\"number\" min=\"0\" max=\"4\" value=\"%u\"></label>"
                "<label class=\"field\"><span>使用限制</span><input name=\"use_limit\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\"></label>"
                "<label class=\"field\"><span>力量系数</span><input name=\"strength_coeff\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>敏捷系数</span><input name=\"agility_coeff\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>智慧系数</span><input name=\"wisdom_coeff\" type=\"number\" min=\"0\" value=\"%u\"></label>"
                "<label class=\"field\"><span>效果种类</span><input name=\"effect_kind\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\"></label>"
                "<label class=\"field\"><span>持续回合</span><input name=\"duration_rounds\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\"></label>"
                "</div>"
                "<label class=\"field\" style=\"margin-top:10px\"><span>说明</span><textarea name=\"description\" maxlength=\"90\">",
                row->rawJob, row->levelRequired, row->effectIndex,
                row->learnPrice, row->mpCost, row->hpChange,
                row->targetDirection, row->useLimit, row->strengthCoeff,
                row->agilityCoeff, row->wisdomCoeff, row->effectKind,
                row->durationRounds);
            vm_mock_admin_text_append_html(&page, descUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "</textarea></label>"
                "<div class=\"actions\"><button type=\"submit\">保存</button></div></form>"
                "<form method=\"post\" action=\"/action\" style=\"margin-top:10px\" "
                "onsubmit=\"return confirm('确认删除该自定义技能？');\">"
                "<input type=\"hidden\" name=\"action\" value=\"delete-catalog-skill\">"
                "<input type=\"hidden\" name=\"skill_id\" value=\"%u\">"
                "<button class=\"danger\" type=\"submit\">删除</button></form>",
                row->skillId);
        }
    }
    else
    {
        vm_mock_admin_text_appendf(
            &page,
            "<p>从左侧选择一条自定义%s，或点击「新增」。</p>",
            vm_mock_admin_catalog_kind_label(kind));
    }

    vm_mock_admin_text_appendf(&page, "</section></div></main></body></html>");
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>目录管理页面过大。</p>");
    }
}

static bool vm_mock_admin_form_i32(const char *body, const char *field,
                                   int32_t *valueOut)
{
    char text[64];
    int parsed = 0;

    memset(text, 0, sizeof(text));
    if (!vm_mock_admin_form_value(body, field, text, sizeof(text)))
        return false;
    if (sscanf(text, "%d", &parsed) != 1)
        return false;
    if (valueOut)
        *valueOut = (int32_t)parsed;
    return true;
}

static bool vm_mock_admin_catalog_read_name_desc(const char *body,
                                                char *nameGbk, size_t nameCap,
                                                char *descGbk, size_t descCap)
{
    char nameUtf8[128];
    char descUtf8[384];

    memset(nameUtf8, 0, sizeof(nameUtf8));
    memset(descUtf8, 0, sizeof(descUtf8));
    if (!vm_mock_admin_form_value(body, "name", nameUtf8, sizeof(nameUtf8)) ||
        nameUtf8[0] == 0 ||
        !vm_mock_admin_utf8_to_gbk_text(nameUtf8, nameGbk, nameCap, false))
        return false;
    (void)vm_mock_admin_form_value(body, "description", descUtf8,
                                   sizeof(descUtf8));
    if (descUtf8[0] == 0)
    {
        if (descGbk != NULL && descCap > 0)
            descGbk[0] = 0;
        return true;
    }
    return vm_mock_admin_utf8_to_gbk_text(descUtf8, descGbk, descCap, true);
}

static void vm_mock_admin_handle_catalog_action(vm_mock_service_socket client,
                                                 const char *action,
                                                 const char *body)
{
    const char *error = NULL;
    u32 tmp = 0;

    if (strcmp(action, "create-catalog-item") == 0 ||
        strcmp(action, "save-catalog-item") == 0)
    {
        vm_net_mock_item_catalog_extra_row row;
        bool creating = strcmp(action, "create-catalog-item") == 0;

        memset(&row, 0, sizeof(row));
        if (!creating)
        {
            if (!vm_mock_admin_form_u32(body, "item_id", 0xffffffffu,
                                       &row.itemId) ||
                row.itemId == 0)
            {
                vm_mock_admin_redirect_catalog(client, "item", 0, "error",
                                               "道具 ID 无效");
                return;
            }
        }
        if (!vm_mock_admin_catalog_read_name_desc(body, row.name, sizeof(row.name),
                                                  row.description,
                                                  sizeof(row.description)) ||
            !vm_mock_admin_form_u32(body, "category", 0xffu, &tmp))
        {
            vm_mock_admin_redirect_catalog(client, "item", row.itemId, "error",
                                           "名称或类别无效");
            return;
        }
        row.category = (u8)tmp;
        if (!vm_mock_admin_form_u32(body, "level_required", 0xffu, &tmp))
            tmp = 1;
        row.levelRequired = (u8)(tmp ? tmp : 1);
        if (!vm_mock_admin_form_u32(body, "stack", 0xffu, &tmp))
            tmp = 99;
        row.stack = (u8)(tmp ? tmp : 1);
        if (!vm_mock_admin_form_u32(body, "consume_mode", 0xffu, &tmp))
            tmp = 1;
        row.consumeMode = (u8)tmp;
        if (vm_mock_admin_form_u32(body, "duration_minutes", 0xffu, &tmp))
            row.durationMinutes = (u8)tmp;
        (void)vm_mock_admin_form_u32(body, "hp", 0xffffffffu, &row.hp);
        (void)vm_mock_admin_form_u32(body, "mp", 0xffffffffu, &row.mp);
        (void)vm_mock_admin_form_u32(body, "exp", 0xffffffffu, &row.exp);
        (void)vm_mock_admin_form_u32(body, "quest_link", 0xffffffffu,
                                     &row.questLink);
        (void)vm_mock_admin_form_u32(body, "bind_attr", 0xffffffffu,
                                     &row.bindAttr);
        if (!vm_mock_admin_form_u32(body, "visual", 0xffu, &tmp))
            tmp = 1;
        row.visual = (u8)(tmp ? tmp : 1);
        (void)vm_mock_admin_form_u32(body, "price", 0xffffffffu, &row.price);
        (void)vm_mock_admin_form_u32(body, "cool_price", 0xffffffffu,
                                     &row.coolPrice);
        if (vm_mock_admin_form_u32(body, "shop_enabled", 1u, &tmp))
            row.shopEnabled = tmp ? 1u : 0u;
        if (creating)
        {
            if (!vm_net_mock_item_catalog_extra_create(&row, &error))
            {
                vm_mock_admin_redirect_catalog(
                    client, "item", 0, "error",
                    error ? error : "创建道具失败");
                return;
            }
            vm_mock_admin_redirect_catalog(client, "item", row.itemId, "ok",
                                           "自定义道具已创建");
            return;
        }
        if (!vm_net_mock_item_catalog_extra_save(&row, &error))
        {
            vm_mock_admin_redirect_catalog(client, "item", row.itemId, "error",
                                           error ? error : "保存道具失败");
            return;
        }
        vm_mock_admin_redirect_catalog(client, "item", row.itemId, "ok",
                                       "道具已保存");
        return;
    }

    if (strcmp(action, "delete-catalog-item") == 0)
    {
        u32 itemId = 0;
        if (!vm_mock_admin_form_u32(body, "item_id", 0xffffffffu, &itemId) ||
            itemId == 0 ||
            !vm_net_mock_item_catalog_extra_delete(itemId, &error))
        {
            vm_mock_admin_redirect_catalog(client, "item", itemId, "error",
                                           error ? error : "删除失败");
            return;
        }
        vm_mock_admin_redirect_catalog(client, "item", 0, "ok", "道具已删除");
        return;
    }

    if (strcmp(action, "create-catalog-equip") == 0 ||
        strcmp(action, "save-catalog-equip") == 0)
    {
        vm_net_mock_equip_catalog_extra_row row;
        bool creating = strcmp(action, "create-catalog-equip") == 0;

        memset(&row, 0, sizeof(row));
        if (!creating)
        {
            if (!vm_mock_admin_form_u32(body, "item_id", 0xffffffffu,
                                       &row.itemId) ||
                row.itemId == 0)
            {
                vm_mock_admin_redirect_catalog(client, "equip", 0, "error",
                                               "装备 ID 无效");
                return;
            }
        }
        if (!vm_mock_admin_catalog_read_name_desc(body, row.name, sizeof(row.name),
                                                  row.description,
                                                  sizeof(row.description)) ||
            !vm_mock_admin_form_u32(body, "category", 9u, &tmp))
        {
            vm_mock_admin_redirect_catalog(client, "equip", row.itemId, "error",
                                           "名称或类别无效");
            return;
        }
        row.category = (u8)tmp;
        if (!vm_mock_admin_form_u32(body, "level_required", 0xffu, &tmp))
            tmp = 1;
        row.levelRequired = (u8)(tmp ? tmp : 1);
        if (vm_mock_admin_form_u32(body, "quality", 0xffu, &tmp))
            row.quality = (u8)tmp;
        if (!vm_mock_admin_form_u32(body, "durability_max", 0xffffu, &tmp))
            tmp = 50;
        row.durabilityMax = (u16)(tmp ? tmp : 50);
        (void)vm_mock_admin_form_u32(body, "attack", 0xffffffffu,
                                     &row.bonus.attack);
        (void)vm_mock_admin_form_u32(body, "armor", 0xffffffffu,
                                     &row.bonus.armor);
        (void)vm_mock_admin_form_u32(body, "hp", 0xffffffffu, &row.bonus.hp);
        (void)vm_mock_admin_form_u32(body, "mp", 0xffffffffu, &row.bonus.mp);
        (void)vm_mock_admin_form_u32(body, "strength", 0xffffffffu,
                                     &row.bonus.strength);
        (void)vm_mock_admin_form_u32(body, "agility", 0xffffffffu,
                                     &row.bonus.agility);
        (void)vm_mock_admin_form_u32(body, "wisdom", 0xffffffffu,
                                     &row.bonus.wisdom);
        (void)vm_mock_admin_form_u32(body, "dodge", 0xffffffffu,
                                     &row.bonus.dodge);
        (void)vm_mock_admin_form_u32(body, "hit", 0xffffffffu, &row.bonus.hit);
        (void)vm_mock_admin_form_u32(body, "crit", 0xffffffffu,
                                     &row.bonus.crit);
        (void)vm_mock_admin_form_u32(body, "resist", 0xffffffffu,
                                     &row.bonus.resist);
        (void)vm_mock_admin_form_u32(body, "price", 0xffffffffu, &row.price);
        if (vm_mock_admin_form_u32(body, "shop_enabled", 1u, &tmp))
            row.shopEnabled = tmp ? 1u : 0u;
        if (creating)
        {
            if (!vm_net_mock_equip_catalog_extra_create(&row, &error))
            {
                vm_mock_admin_redirect_catalog(
                    client, "equip", 0, "error",
                    error ? error : "创建装备失败");
                return;
            }
            vm_mock_admin_redirect_catalog(client, "equip", row.itemId, "ok",
                                           "自定义装备已创建");
            return;
        }
        if (!vm_net_mock_equip_catalog_extra_save(&row, &error))
        {
            vm_mock_admin_redirect_catalog(client, "equip", row.itemId, "error",
                                           error ? error : "保存装备失败");
            return;
        }
        vm_mock_admin_redirect_catalog(client, "equip", row.itemId, "ok",
                                       "装备已保存");
        return;
    }

    if (strcmp(action, "delete-catalog-equip") == 0)
    {
        u32 itemId = 0;
        if (!vm_mock_admin_form_u32(body, "item_id", 0xffffffffu, &itemId) ||
            itemId == 0 ||
            !vm_net_mock_equip_catalog_extra_delete(itemId, &error))
        {
            vm_mock_admin_redirect_catalog(client, "equip", itemId, "error",
                                           error ? error : "删除失败");
            return;
        }
        vm_mock_admin_redirect_catalog(client, "equip", 0, "ok", "装备已删除");
        return;
    }

    if (strcmp(action, "create-catalog-skill") == 0 ||
        strcmp(action, "save-catalog-skill") == 0)
    {
        vm_net_mock_skill_catalog_extra_row row;
        bool creating = strcmp(action, "create-catalog-skill") == 0;
        int32_t signedTmp = 0;

        memset(&row, 0, sizeof(row));
        if (!creating)
        {
            if (!vm_mock_admin_form_u32(body, "skill_id", 0xffffffffu,
                                       &row.skillId) ||
                row.skillId == 0)
            {
                vm_mock_admin_redirect_catalog(client, "skill", 0, "error",
                                               "技能 ID 无效");
                return;
            }
        }
        if (!vm_mock_admin_catalog_read_name_desc(body, row.name, sizeof(row.name),
                                                  row.description,
                                                  sizeof(row.description)) ||
            !vm_mock_admin_form_u32(body, "raw_job", 2u, &tmp))
        {
            vm_mock_admin_redirect_catalog(client, "skill", row.skillId, "error",
                                           "名称或职业无效");
            return;
        }
        row.rawJob = (u8)tmp;
        if (!vm_mock_admin_form_u32(body, "level_required", 0xffu, &tmp))
            tmp = 1;
        row.levelRequired = (u8)(tmp ? tmp : 1);
        (void)vm_mock_admin_form_u32(body, "effect_index", 0xffffffffu,
                                     &row.effectIndex);
        (void)vm_mock_admin_form_u32(body, "learn_price", 0xffffffffu,
                                     &row.learnPrice);
        (void)vm_mock_admin_form_u32(body, "mp_cost", 0xffffffffu, &row.mpCost);
        if (vm_mock_admin_form_i32(body, "hp_change", &signedTmp))
            row.hpChange = signedTmp;
        if (!vm_mock_admin_form_u32(body, "target_direction", 4u, &tmp))
            tmp = 3;
        row.targetDirection = (u8)tmp;
        if (vm_mock_admin_form_u32(body, "use_limit", 0xffu, &tmp))
            row.useLimit = (u8)tmp;
        (void)vm_mock_admin_form_u32(body, "strength_coeff", 0xffffffffu,
                                     &row.strengthCoeff);
        (void)vm_mock_admin_form_u32(body, "agility_coeff", 0xffffffffu,
                                     &row.agilityCoeff);
        (void)vm_mock_admin_form_u32(body, "wisdom_coeff", 0xffffffffu,
                                     &row.wisdomCoeff);
        if (vm_mock_admin_form_u32(body, "effect_kind", 0xffu, &tmp))
            row.effectKind = (u8)tmp;
        if (vm_mock_admin_form_u32(body, "duration_rounds", 0xffu, &tmp))
            row.durationRounds = (u8)tmp;
        if (creating)
        {
            if (!vm_net_mock_skill_catalog_extra_create(&row, &error))
            {
                vm_mock_admin_redirect_catalog(
                    client, "skill", 0, "error",
                    error ? error : "创建技能失败");
                return;
            }
            vm_mock_admin_redirect_catalog(client, "skill", row.skillId, "ok",
                                           "自定义技能已创建");
            return;
        }
        if (!vm_net_mock_skill_catalog_extra_save(&row, &error))
        {
            vm_mock_admin_redirect_catalog(client, "skill", row.skillId, "error",
                                           error ? error : "保存技能失败");
            return;
        }
        vm_mock_admin_redirect_catalog(client, "skill", row.skillId, "ok",
                                       "技能已保存");
        return;
    }

    if (strcmp(action, "delete-catalog-skill") == 0)
    {
        u32 skillId = 0;
        if (!vm_mock_admin_form_u32(body, "skill_id", 0xffffffffu, &skillId) ||
            skillId == 0 ||
            !vm_net_mock_skill_catalog_extra_delete(skillId, &error))
        {
            vm_mock_admin_redirect_catalog(client, "skill", skillId, "error",
                                           error ? error : "删除失败");
            return;
        }
        vm_mock_admin_redirect_catalog(client, "skill", 0, "ok", "技能已删除");
        return;
    }

    vm_mock_admin_redirect_catalog(client, "item", 0, "error", "未知目录操作");
}
