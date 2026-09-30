/* Admin UI for Pixi client UI toggles — included from web_admin_server.c. */

static void vm_mock_admin_redirect_client_ui(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=clientui&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_render_client_ui_page(char *response, size_t responseCap,
                                                const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_net_mock_client_ui_config *cfg;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    cfg = vm_net_mock_client_ui_config_get();

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 客户端UI</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7;color:#1f2937}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px;"
        "border:1px solid #e4e7ec}"
        ".notice{padding:10px 12px;border-radius:7px;margin-bottom:14px}"
        ".ok{background:#ecfdf3;color:#027a48}.error{background:#fef3f2;color:#b42318}"
        "select,input[type=number]{padding:8px;width:160px;box-sizing:border-box}"
        "label{font-size:12px;color:#667085;display:block;margin-bottom:4px}"
        "button{background:#175cd3;color:#fff;border:0;padding:10px 14px;"
        "border-radius:6px;margin-top:12px;cursor:pointer}"
        ".hint{color:#667085;font-size:13px;line-height:1.55}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));"
        "gap:12px}"
        "code{background:#f2f4f7;padding:1px 4px;border-radius:4px}"
        "</style></head><body><h1>客户端UI</h1>"
        "<p class=\"hint\">控制 Pixi 客户端是否显示装备「洗练」按钮、地图角"
        "状态栏「翅」「骑」入口、本机同页账号多开槽位上限，以及无战斗心得时"
        "是否限制挂机 64 场。保存后写入 "
        "<code>server_client_ui_config</code>；玩家下次拉人物面板"
        "（<code>1/1/21</code>）后生效。"
        "关闭洗练时服务端亦拒绝 29/7–9、29/14、29/13。</p>");
    vm_mock_admin_append_tabs(&page, "clientui");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(
            &page, "<div class=\"notice %s\">%s</div>",
            strcmp(status, "ok") == 0 ? "ok" : "error", message);
    }

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>显示开关</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-client-ui-config\">"
        "<div class=\"grid\">"
        "<div><label>装备洗练按钮</label><select name=\"wash_button_enabled\">"
        "<option value=\"1\"%s>显示</option><option value=\"0\"%s>隐藏</option>"
        "</select></div>"
        "<div><label>翅膀状态栏（翅）</label><select name=\"wing_status_enabled\">"
        "<option value=\"1\"%s>显示</option><option value=\"0\"%s>隐藏</option>"
        "</select></div>"
        "<div><label>坐骑状态栏（骑）</label><select name=\"mount_status_enabled\">"
        "<option value=\"1\"%s>显示</option><option value=\"0\"%s>隐藏</option>"
        "</select></div>"
        "<div><label>账号多开上限（1–8）</label>"
        "<input type=\"number\" name=\"multi_open_limit\" min=\"1\" max=\"8\" "
        "value=\"%u\"></div>"
        "<div><label>无战斗心得挂机限额</label>"
        "<select name=\"hangup_quota_without_insight\">"
        "<option value=\"1\"%s>限制64场</option>"
        "<option value=\"0\"%s>不限制</option>"
        "</select></div>"
        "</div>"
        "<p class=\"hint\">隐藏翅膀/坐骑状态栏只影响地图角入口，不影响已装备外观"
        "与管理端解锁。隐藏洗练按钮后背包/强化面板不再出现「洗练」。"
        "多开上限=1 时客户端菜单「账号多开」不可再开新槽；同页多会话切换，"
        "其他账号长连接保持。默认均为显示，多开上限默认 1。"
        "「无战斗心得挂机限额」默认限制 64 场；选「不限制」后无心得也可无限挂机"
        "（有战斗心得时始终不限次）。</p>"
        "<button type=\"submit\">保存</button></form></section></body></html>",
        cfg && cfg->washButtonEnabled ? " selected" : "",
        cfg && !cfg->washButtonEnabled ? " selected" : "",
        cfg && cfg->wingStatusEnabled ? " selected" : "",
        cfg && !cfg->wingStatusEnabled ? " selected" : "",
        cfg && cfg->mountStatusEnabled ? " selected" : "",
        cfg && !cfg->mountStatusEnabled ? " selected" : "",
        (u32)(cfg ? cfg->multiOpenLimit : 1u),
        cfg && cfg->hangupQuotaWithoutInsight ? " selected" : "",
        cfg && !cfg->hangupQuotaWithoutInsight ? " selected" : "");
    if (page.truncated)
    {
        snprintf(response, responseCap,
                 "<!doctype html><meta charset=\"utf-8\"><p>客户端UI页面超过大小限制。</p>");
    }
}

static void vm_mock_admin_handle_client_ui_action(vm_mock_service_socket client,
                                                  const char *action,
                                                  const char *body)
{
    u32 washButton = 1;
    u32 wingStatus = 1;
    u32 mountStatus = 1;
    u32 multiOpenLimit = 1;
    u32 hangupQuota = 1;

    if (strcmp(action, "save-client-ui-config") != 0)
    {
        vm_mock_admin_redirect_client_ui(client, "error", "未知操作");
        return;
    }
    if (!vm_mock_admin_form_u32(body, "wash_button_enabled", 1u, &washButton) ||
        !vm_mock_admin_form_u32(body, "wing_status_enabled", 1u, &wingStatus) ||
        !vm_mock_admin_form_u32(body, "mount_status_enabled", 1u, &mountStatus) ||
        !vm_mock_admin_form_u32(body, "multi_open_limit", 8u, &multiOpenLimit) ||
        multiOpenLimit < 1u ||
        !vm_mock_admin_form_u32(body, "hangup_quota_without_insight", 1u,
                                &hangupQuota))
    {
        vm_mock_admin_redirect_client_ui(client, "error", "表单参数无效");
        return;
    }
    if (vm_net_mock_client_ui_config_admin_save(
            (u8)washButton, (u8)wingStatus, (u8)mountStatus, (u8)multiOpenLimit,
            (u8)hangupQuota))
        vm_mock_admin_redirect_client_ui(client, "ok", "客户端UI配置已保存");
    else
        vm_mock_admin_redirect_client_ui(client, "error", "保存客户端UI配置失败");
}
