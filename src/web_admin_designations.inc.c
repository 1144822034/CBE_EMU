enum
{
    VM_MOCK_ADMIN_DESIGNATION_MAX = 64,
};

/* Keep the directories tied to the recovered title catalog, not to the
 * administrator's editable unlock condition.  A wealth title remains in the
 * wealth directory even while its temporary eligibility condition is being
 * changed to a level threshold. */
static const char *vm_mock_admin_designation_directory(
    const vm_net_mock_designation_admin_row *row)
{
    if (row != NULL && row->special)
        return "special";
    return row != NULL && row->designationId <= 9 ? "money" : "level";
}

static const char *vm_mock_admin_designation_directory_label(
    const char *directory)
{
    if (directory != NULL && strcmp(directory, "money") == 0)
        return "金钱称号";
    if (directory != NULL && strcmp(directory, "level") == 0)
        return "等级称号";
    return "特殊称号";
}

static void vm_mock_admin_redirect_designations(vm_mock_service_socket client,
                                                const char *status,
                                                const char *message)
{
    char statusEncoded[64];
    char messageEncoded[768];
    char location[1024];

    vm_mock_admin_url_encode(status ? status : "error", statusEncoded,
                             sizeof(statusEncoded));
    vm_mock_admin_url_encode(message ? message : "操作失败", messageEncoded,
                             sizeof(messageEncoded));
    snprintf(location, sizeof(location),
             VM_MOCK_ADMIN_ROOT_PATH
             "?tab=designations&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_designation_render_nav(vm_mock_admin_text *page)
{
    vm_mock_admin_text_appendf(
        page,
        "<nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=global-rewards\">奖励邮件管理</a>"
        "<a class=\"tab on\" href=\"/?tab=designations\">称号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=scene-monsters\">场景战斗怪</a>"
        "<a class=\"tab\" href=\"/?tab=actors\">Actor 资源</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱管理</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "<a class=\"tab\" href=\"/?tab=risk\">风险管理</a></nav>");
}

static void vm_mock_admin_render_designation_condition_options(
    vm_mock_admin_text *page, u8 selected)
{
    vm_mock_admin_text_appendf(
        page,
        "<option value=\"%u\"%s>持有铜钱（按金）</option>"
        "<option value=\"%u\"%s>角色等级</option>",
        VM_NET_MOCK_DESIGNATION_CONDITION_MONEY,
        selected == VM_NET_MOCK_DESIGNATION_CONDITION_MONEY ? " selected" : "",
        VM_NET_MOCK_DESIGNATION_CONDITION_LEVEL,
        selected == VM_NET_MOCK_DESIGNATION_CONDITION_LEVEL ? " selected" : "");
}

static void vm_mock_admin_render_designations_page(char *response,
                                                   size_t responseCap,
                                                   const char *query)
{
    vm_mock_admin_text page;
    vm_net_mock_designation_admin_row rows[VM_MOCK_ADMIN_DESIGNATION_MAX];
    char status[16];
    char message[256];
    u32 count = 0;

    memset(rows, 0, sizeof(rows));
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    count = vm_net_mock_designation_admin_list(rows,
                                                VM_MOCK_ADMIN_DESIGNATION_MAX);

    vm_mock_admin_text_init(&page, response, responseCap);
    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 称号管理</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#1f2937;font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}.wrap{max-width:1380px;margin:auto;padding:24px 18px}.head{display:flex;justify-content:space-between;gap:16px}.head h1{margin:0}.sub{color:#667085;margin:4px 0 16px}.tabs{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:14px}.tab{padding:8px 12px;border:1px solid #e4e7ec;border-radius:7px;background:#fff;color:#475467;text-decoration:none}.tab.on{background:#175cd3;color:#fff}.notice{padding:10px 12px;border-radius:7px;margin-bottom:13px}.notice.ok{background:#ecfdf3;color:#027a48}.notice.error{background:#fef3f2;color:#b42318}.card{background:#fff;border:1px solid #e4e7ec;border-radius:10px;padding:16px;margin-bottom:16px}.hint{color:#667085;font-size:12px}.directory-tabs{display:flex;gap:8px;flex-wrap:wrap;margin:16px 0 14px}.directory-tabs button{border:1px solid #d0d5dd;border-radius:999px;padding:7px 13px;background:#fff;color:#475467;font:inherit;cursor:pointer}.directory-tabs button.on{border-color:#175cd3;background:#175cd3;color:#fff}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(390px,1fr));gap:14px}.designation{border:1px solid #dbe3ef;border-radius:9px;padding:14px;background:#fff}.designation.special{border-color:#b9a7ff;background:#fbfaff}.designation-head{display:flex;align-items:start;justify-content:space-between;gap:10px}.designation h2{font-size:17px;margin:0}.badge{display:inline-block;padding:2px 8px;border-radius:999px;background:#eef4ff;color:#175cd3;font-size:12px}.badge.level{background:#ecfdf3;color:#027a48}.badge.special{background:#f0ebff;color:#6941c6}.fields{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:12px}.field{display:grid;gap:4px;color:#475467;font-size:12px}.wide{grid-column:1/-1}input,select{width:100%%;border:1px solid #d0d5dd;border-radius:7px;padding:8px;background:#fff;font:inherit;color:#1f2937}.badge-preview{display:grid;place-items:center;width:112px;height:62px;margin-top:12px;border:1px solid #dbe3ef;border-radius:8px;background:linear-gradient(135deg,#f8fafc,#eef4ff);overflow:hidden}.badge-preview img{display:block;max-width:104px;max-height:54px;object-fit:contain;image-rendering:pixelated}.badge-preview-empty{padding:8px;color:#667085;text-align:center;font-size:12px;background:#fafafa;border-style:dashed}.actions{display:flex;justify-content:flex-end;margin-top:12px}button{border:0;border-radius:7px;padding:8px 13px;background:#175cd3;color:#fff;cursor:pointer}.logout{background:#fff;color:#475467;border:1px solid #d0d5dd}.muted{color:#667085;font-size:12px}@media(max-width:720px){.grid{grid-template-columns:1fr}.fields{grid-template-columns:1fr}.wide{grid-column:auto}}</style>"
        "<script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><header class=\"head\"><div><h1>江湖 OL 后台管理</h1><p class=\"sub\">称号管理 · 配置解锁条件与特殊运营称号</p></div><form method=\"post\" action=\"/logout\"><button class=\"logout\" type=\"submit\">退出登录</button></form></header>");
    vm_mock_admin_designation_render_nav(&page);
    if (message[0] != 0)
    {
        vm_mock_admin_text_appendf(&page, "<div class=\"notice %s\">",
                                   strcmp(status, "ok") == 0 ? "ok" : "error");
        vm_mock_admin_text_append_html(&page, message);
        vm_mock_admin_text_appendf(&page, "</div>");
    }
    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>称号达成条件</h2><p class=\"hint\">按称号目录筛选后配置是否启用和达成门槛。目录按已恢复的称号图鉴固定分类；铜钱条件按游戏显示的“金”填写（服务端自动换算）。圣诞骑士与武林传奇的条件固定为角色实际穿戴全套对应装备，后台只能启用或停用，不能改成等级或铜钱门槛。下方预览图直接由已验证的客户端徽章资源生成。特殊称号没有已恢复的专属徽章资源，会保持安全空资源，不会把中文名称当作文件名发送给客户端。</p>");
    if (count == 0)
    {
        vm_mock_admin_text_appendf(
            &page,
            "<p class=\"hint\">称号配置数据库暂不可用，未显示可编辑配置。</p>");
    }
    else
    {
        vm_mock_admin_text_appendf(
            &page,
            "<div class=\"designation-directory\" data-designation-directory data-active-category=\"money\"><div class=\"directory-tabs\" role=\"group\" aria-label=\"称号目录\"><button type=\"button\" data-designation-filter=\"money\">金钱称号</button><button type=\"button\" data-designation-filter=\"level\">等级称号</button><button type=\"button\" data-designation-filter=\"special\">特殊称号</button></div><div class=\"grid\">");
        for (u32 i = 0; i < count; ++i)
        {
            const vm_net_mock_designation_admin_row *row = &rows[i];
            const char *directory = vm_mock_admin_designation_directory(row);
            const char *directoryLabel =
                vm_mock_admin_designation_directory_label(directory);
            u32 equipmentSet =
                vm_net_mock_designation_equipment_set_for_id(
                    row->designationId);
            bool fixedEquipmentSet = equipmentSet != 0 &&
                                     row->conditionKind ==
                                         VM_NET_MOCK_DESIGNATION_CONDITION_EQUIPMENT_SET;
            char nameUtf8[96];
            char descriptionUtf8[256];
            u32 displayThreshold = row->conditionKind ==
                                           VM_NET_MOCK_DESIGNATION_CONDITION_MONEY
                                       ? row->conditionValue /
                                             VM_NET_MOCK_DESIGNATION_MONEY_PER_GOLD
                                       : row->conditionValue;

            memset(nameUtf8, 0, sizeof(nameUtf8));
            memset(descriptionUtf8, 0, sizeof(descriptionUtf8));
            vm_net_mock_gbk_label_to_utf8(row->name, nameUtf8,
                                           sizeof(nameUtf8));
            vm_net_mock_gbk_label_to_utf8(row->description, descriptionUtf8,
                                           sizeof(descriptionUtf8));
            vm_mock_admin_text_appendf(
                &page,
                "<form class=\"designation%s\" data-designation-category=\"%s\" method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"save-designation\"><input type=\"hidden\" name=\"designation_id\" value=\"%u\"><div class=\"designation-head\"><div><h2>#%u · ",
                row->special ? " special" : "", directory,
                row->designationId, row->designationId);
            vm_mock_admin_text_append_html(&page, nameUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "</h2><p class=\"muted\">");
            vm_mock_admin_text_append_html(&page, descriptionUtf8);
            vm_mock_admin_text_appendf(
                &page,
                "</p></div><span class=\"badge %s\">%s</span></div><div class=\"fields\"><label class=\"field\"><span>状态</span><select name=\"enabled\"><option value=\"1\"%s>启用</option><option value=\"0\"%s>停用</option></select></label>",
                directory, directoryLabel,
                row->enabled ? " selected" : "",
                row->enabled ? "" : " selected");
            if (fixedEquipmentSet)
            {
                vm_mock_admin_text_appendf(
                    &page,
                    "<label class=\"field wide\"><span>达成条件</span><input type=\"hidden\" name=\"condition_kind\" value=\"%u\"><input type=\"hidden\" name=\"condition_value\" value=\"%u\"><strong>%s</strong><span class=\"hint\">武器槽可使用本职业对应的剑、匕首或魔杖；其余七个装备槽必须全部穿戴同系列装备。只检查已穿戴且满足角色等级要求的装备，不检查背包。</span></label></div>",
                    VM_NET_MOCK_DESIGNATION_CONDITION_EQUIPMENT_SET,
                    equipmentSet,
                    vm_net_mock_designation_equipment_set_label(equipmentSet));
            }
            else
            {
                vm_mock_admin_text_appendf(
                    &page,
                    "<label class=\"field\"><span>达成条件</span><select name=\"condition_kind\">");
                vm_mock_admin_render_designation_condition_options(
                    &page, row->conditionKind);
                vm_mock_admin_text_appendf(
                    &page,
                    "</select></label><label class=\"field wide\"><span>条件数值（%s）</span><input type=\"number\" name=\"condition_value\" min=\"%u\" max=\"%u\" value=\"%u\" required></label></div>",
                    row->conditionKind == VM_NET_MOCK_DESIGNATION_CONDITION_MONEY
                        ? "持有铜钱，单位：金" : "角色等级",
                    row->conditionKind == VM_NET_MOCK_DESIGNATION_CONDITION_MONEY
                        ? 0u : 1u,
                    row->conditionKind == VM_NET_MOCK_DESIGNATION_CONDITION_MONEY
                        ? 4294967u : VM_NET_MOCK_ROLE_LEVEL_CAP,
                    displayThreshold);
            }
            if (row->overheadResource[0] != 0)
            {
                vm_mock_admin_text_appendf(
                    &page,
                    "<div class=\"badge-preview\"><img src=\"/gif-preview.bmp?gif=%s\" alt=\"",
                    row->overheadResource);
                vm_mock_admin_text_append_html(&page, nameUtf8);
                vm_mock_admin_text_appendf(
                    &page, " 徽章预览\" loading=\"lazy\"></div>");
            }
            else
            {
                vm_mock_admin_text_appendf(
                    &page,
                    "<div class=\"badge-preview badge-preview-empty\">暂无专属徽章预览</div>");
            }
            vm_mock_admin_text_appendf(
                &page,
                "<div class=\"actions\"><button type=\"submit\">保存条件</button></div></form>");
        }
        vm_mock_admin_text_appendf(&page, "</div></div>");
    }
    vm_mock_admin_text_appendf(&page, "</section></main></body></html>");
}

static void vm_mock_admin_handle_designation_action(
    vm_mock_service_socket client, const char *action, const char *body)
{
    u32 designationId = 0;
    u32 enabled = 0;
    u32 conditionKind = 0;
    u32 displayValue = 0;
    u32 conditionValue = 0;
    const char *error = NULL;

    if (action == NULL || strcmp(action, "save-designation") != 0 ||
        !vm_mock_admin_form_u32(body, "designation_id", 0xffu,
                                &designationId) ||
        !vm_mock_admin_form_u32(body, "enabled", 1, &enabled) ||
        !vm_mock_admin_form_u32(body, "condition_kind", 3, &conditionKind) ||
        !vm_mock_admin_form_u32(body, "condition_value", 4294967u,
                                &displayValue) ||
        (conditionKind != VM_NET_MOCK_DESIGNATION_CONDITION_MONEY &&
         conditionKind != VM_NET_MOCK_DESIGNATION_CONDITION_LEVEL &&
         conditionKind != VM_NET_MOCK_DESIGNATION_CONDITION_EQUIPMENT_SET))
    {
        vm_mock_admin_redirect_designations(client, "error", "称号条件字段无效");
        return;
    }
    if (vm_net_mock_designation_equipment_set_for_id((u8)designationId) != 0)
    {
        u32 expectedSet =
            vm_net_mock_designation_equipment_set_for_id((u8)designationId);

        if (conditionKind !=
                VM_NET_MOCK_DESIGNATION_CONDITION_EQUIPMENT_SET ||
            displayValue != expectedSet)
        {
            vm_mock_admin_redirect_designations(
                client, "error", "该特殊称号的套装条件不可修改");
            return;
        }
        conditionValue = expectedSet;
    }
    else if (conditionKind == VM_NET_MOCK_DESIGNATION_CONDITION_MONEY)
    {
        if (displayValue > 0xffffffffu /
                               VM_NET_MOCK_DESIGNATION_MONEY_PER_GOLD)
        {
            vm_mock_admin_redirect_designations(client, "error", "铜钱门槛超出范围");
            return;
        }
        conditionValue = displayValue * VM_NET_MOCK_DESIGNATION_MONEY_PER_GOLD;
    }
    else if (conditionKind == VM_NET_MOCK_DESIGNATION_CONDITION_LEVEL)
    {
        conditionValue = displayValue;
    }
    else
    {
        vm_mock_admin_redirect_designations(client, "error", "该称号不支持套装条件");
        return;
    }
    if (!vm_net_mock_designation_admin_save(
            (u8)designationId, enabled != 0, (u8)conditionKind,
            conditionValue, &error))
    {
        vm_mock_admin_redirect_designations(
            client, "error", error ? error : "称号条件保存失败");
        return;
    }
    vm_mock_admin_redirect_designations(client, "ok", "称号达成条件已保存");
}
