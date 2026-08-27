/* Admin UI for world boss — included from web_admin_server.c. */

static void vm_mock_admin_redirect_world_boss(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=worldboss&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_render_world_boss_page(char *response, size_t responseCap,
                                                 const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    char runtimeScene[64];
    char runtimeSceneUtf8[128];
    u8 runtimeStatus = 0;
    u16 runtimeX = 0;
    u16 runtimeY = 0;
    const vm_mock_world_boss_config *cfg;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    memset(runtimeScene, 0, sizeof(runtimeScene));
    memset(runtimeSceneUtf8, 0, sizeof(runtimeSceneUtf8));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    cfg = vm_net_mock_world_boss_config_get();
    vm_mock_world_boss_admin_runtime_get(&runtimeStatus, runtimeScene,
                                         sizeof(runtimeScene), &runtimeX,
                                         &runtimeY);
    if (runtimeScene[0] != 0)
        vm_net_mock_gbk_label_to_utf8(runtimeScene, runtimeSceneUtf8,
                                      sizeof(runtimeSceneUtf8));

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 世界BOSS</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px}"
        ".tab{padding:8px 12px;margin:4px;background:#fff;border:1px solid #ddd;"
        "text-decoration:none;color:#333;border-radius:6px}"
        ".tab.on{background:#175cd3;color:#fff}"
        "input,select{padding:8px;width:100%%;box-sizing:border-box}"
        "label{font-size:12px;color:#666;display:block;margin-bottom:4px}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:10px}"
        ".btn-row{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}"
        "button{background:#175cd3;color:#fff;border:0;padding:10px 14px;border-radius:6px;cursor:pointer}"
        ".btn-warn{background:#b54708}.btn-muted{background:#667085}"
        ".muted{color:#667085;font-size:13px}"
        "</style></head><body><h1>世界BOSS</h1>"
        "<p>每小时整点刷新；提前30秒全服预告地图。首个队长遇怪开战，击杀发配置奖励。</p>"
        "<nav>"
        "<a class=\"tab\" href=\"/?tab=accounts\">账号</a>"
        "<a class=\"tab\" href=\"/?tab=content\">内容</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物</a>"
        "<a class=\"tab on\" href=\"/?tab=worldboss\">世界BOSS</a>"
        "<a class=\"tab\" href=\"/?tab=ladder\">天梯</a>"
        "<a class=\"tab\" href=\"/?tab=arena\">比武</a>"
        "</nav>");

    if (status[0] && message[0])
        vm_mock_admin_text_appendf(&page, "<p>%s: %s</p>", status, message);

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>运行状态</h2>"
        "<p>status=%u</p>"
        "<p class=\"muted\">0=无 1=预告 2=已刷新 3=已被挑战</p>"
        "<p>地图：%s</p>"
        "<p>坐标：(%u,%u)</p>"
        "<div class=\"btn-row\">"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"world-boss-force-preview\">"
        "<button type=\"submit\">立即预告</button></form>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"world-boss-force-spawn\">"
        "<button type=\"submit\" class=\"btn-warn\">立即刷BOSS</button></form>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"world-boss-force-despawn\">"
        "<button type=\"submit\" class=\"btn-muted\">清除当前BOSS</button></form>"
        "</div></section>"
        "<section class=\"card\"><h2>配置</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-world-boss-config\">"
        "<div class=\"grid\">"
        "<div><label>启用</label><select name=\"enabled\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
        "<div><label>BOSS怪物ID</label><input name=\"monster_id\" value=\"%u\"></div>"
        "<div><label>预告秒数</label><input name=\"announce_seconds\" value=\"%u\"></div>"
        "<div><label>击杀经验</label><input name=\"reward_exp\" value=\"%u\"></div>"
        "<div><label>击杀金钱</label><input name=\"reward_money\" value=\"%u\"></div>"
        "<div><label>奖励物品ID</label><input name=\"reward_item_id\" value=\"%u\"></div>"
        "<div><label>奖励数量</label><input name=\"reward_item_count\" value=\"%u\"></div>"
        "</div><button type=\"submit\">保存</button></form></section>"
        "</body></html>",
        (u32)runtimeStatus,
        runtimeSceneUtf8[0] ? runtimeSceneUtf8
                              : (runtimeScene[0] ? runtimeScene : "-"),
        (u32)runtimeX, (u32)runtimeY,
        cfg && cfg->enabled ? " selected" : "",
        cfg && !cfg->enabled ? " selected" : "",
        cfg ? cfg->monsterId : 0,
        cfg ? cfg->announceSeconds : 30,
        cfg ? cfg->rewardExp : 0,
        cfg ? cfg->rewardMoney : 0,
        cfg ? cfg->rewardItemId : 0,
        cfg ? cfg->rewardItemCount : 0);
}

static void vm_mock_admin_handle_world_boss_action(vm_mock_service_socket client,
                                                   const char *action,
                                                   const char *body)
{
    char msg[256];

    (void)body;
    if (strcmp(action, "world-boss-force-preview") == 0)
    {
        memset(msg, 0, sizeof(msg));
        if (vm_mock_world_boss_admin_force_preview(msg, sizeof(msg)))
            vm_mock_admin_redirect_world_boss(client, "ok", msg);
        else
            vm_mock_admin_redirect_world_boss(client, "error",
                                            msg[0] ? msg : "预告失败");
        return;
    }
    if (strcmp(action, "world-boss-force-spawn") == 0)
    {
        memset(msg, 0, sizeof(msg));
        if (vm_mock_world_boss_admin_force_spawn(msg, sizeof(msg)))
            vm_mock_admin_redirect_world_boss(client, "ok", msg);
        else
            vm_mock_admin_redirect_world_boss(client, "error",
                                            msg[0] ? msg : "刷BOSS失败");
        return;
    }
    if (strcmp(action, "world-boss-force-despawn") == 0)
    {
        memset(msg, 0, sizeof(msg));
        if (vm_mock_world_boss_admin_force_despawn(msg, sizeof(msg)))
            vm_mock_admin_redirect_world_boss(client, "ok", msg);
        else
            vm_mock_admin_redirect_world_boss(client, "error",
                                            msg[0] ? msg : "清除失败");
        return;
    }
    if (strcmp(action, "save-world-boss-config") == 0)
    {
        u32 enabled = 0;
        u32 monsterId = 0;
        u32 announce = 30;
        u32 exp = 0;
        u32 money = 0;
        u32 itemId = 0;
        u32 itemCount = 0;

        (void)vm_mock_admin_form_u32(body, "enabled", 1u, &enabled);
        (void)vm_mock_admin_form_u32(body, "monster_id", 0xffffffffu, &monsterId);
        (void)vm_mock_admin_form_u32(body, "announce_seconds", 300u, &announce);
        (void)vm_mock_admin_form_u32(body, "reward_exp", 0xffffffffu, &exp);
        (void)vm_mock_admin_form_u32(body, "reward_money", 0xffffffffu, &money);
        (void)vm_mock_admin_form_u32(body, "reward_item_id", 0xffffffffu, &itemId);
        (void)vm_mock_admin_form_u32(body, "reward_item_count", 0xffffffffu,
                                     &itemCount);
        if (vm_mock_world_boss_admin_save((u8)enabled, monsterId, (u16)announce,
                                          exp, money, itemId, itemCount))
            vm_mock_admin_redirect_world_boss(client, "ok", "世界BOSS配置已保存");
        else
            vm_mock_admin_redirect_world_boss(client, "error", "保存失败");
        return;
    }
    vm_mock_admin_redirect_world_boss(client, "error", "未知操作");
}
