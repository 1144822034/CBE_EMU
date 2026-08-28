/* Admin UI for ladder — included from web_admin_server.c. */

static void vm_mock_admin_redirect_ladder(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=ladder&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_render_ladder_page(char *response, size_t responseCap,
                                             const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_mock_ladder_config *cfg;
    u32 rewardCount;
    u32 i;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    cfg = vm_net_mock_ladder_config_get();
    rewardCount = vm_net_mock_ladder_reward_admin_count();

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>江湖OL 天梯</title><style>"
        "body{font:14px system-ui;margin:28px;background:#f3f5f7}"
        ".card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px}"
        ".tab{padding:8px 12px;margin:4px;background:#fff;border:1px solid #ddd;"
        "text-decoration:none;color:#333;border-radius:6px}"
        ".tab.on{background:#175cd3;color:#fff}"
        "input,select{padding:8px;width:100%%;box-sizing:border-box}"
        "label{font-size:12px;color:#666;display:block;margin-bottom:4px}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}"
        "table{border-collapse:collapse;width:100%%}th,td{padding:8px;border-bottom:1px solid #eee}"
        "button{background:#175cd3;color:#fff;border:0;padding:10px 14px;border-radius:6px}"
        "</style></head><body><h1>天梯挑战</h1>"
        "<p>每日挑战（次数与层数进度均按自然日重置）；对手为天机/幻剑/鬼道三人。层数越高属性与伤害越高；每10层发里程碑奖励。</p>"
        "<nav>"
        "<a class=\"tab\" href=\"/?tab=accounts\">账号</a>"
        "<a class=\"tab\" href=\"/?tab=worldboss\">世界BOSS</a>"
        "<a class=\"tab on\" href=\"/?tab=ladder\">天梯</a>"
        "<a class=\"tab\" href=\"/?tab=arena\">比武</a>"
        "</nav>");

    if (status[0] && message[0])
        vm_mock_admin_text_appendf(&page, "<p>%s: %s</p>", status, message);

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>基础配置</h2>"
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-ladder-config\">"
        "<div class=\"grid\">"
        "<div><label>启用</label><select name=\"enabled\">"
        "<option value=\"1\"%s>开</option><option value=\"0\"%s>关</option></select></div>"
        "<div><label>每日次数</label><input name=\"daily_limit\" value=\"%u\"></div>"
        "<div><label>天机怪ID</label><input name=\"enemy_tianji\" value=\"%u\"></div>"
        "<div><label>幻剑怪ID</label><input name=\"enemy_huanjian\" value=\"%u\"></div>"
        "<div><label>鬼道怪ID</label><input name=\"enemy_guidao\" value=\"%u\"></div>"
        "<div><label>基础HP</label><input name=\"base_hp\" value=\"%u\"></div>"
        "<div><label>基础攻击</label><input name=\"base_atk\" value=\"%u\"></div>"
        "<div><label>基础防御</label><input name=\"base_def\" value=\"%u\"></div>"
        "<div><label>基础MP</label><input name=\"base_mp\" value=\"%u\"></div>"
        "<div><label>每层+HP</label><input name=\"hp_per_floor\" value=\"%u\"></div>"
        "<div><label>每层+攻击</label><input name=\"atk_per_floor\" value=\"%u\"></div>"
        "<div><label>每层+防御</label><input name=\"def_per_floor\" value=\"%u\"></div>"
        "<div><label>每层伤害+%%</label><input name=\"damage_scale\" value=\"%u\"></div>"
        "<div><label>怪物放技能</label><select name=\"cast_skill\">"
        "<option value=\"1\"%s>是</option><option value=\"0\"%s>否</option></select></div>"
        "</div><button type=\"submit\">保存配置</button></form></section>",
        cfg && cfg->enabled ? " selected" : "",
        cfg && !cfg->enabled ? " selected" : "",
        cfg ? cfg->dailyLimit : 1,
        cfg ? cfg->enemyIdTianji : 920001,
        cfg ? cfg->enemyIdHuanjian : 920002,
        cfg ? cfg->enemyIdGuidao : 920003,
        cfg ? cfg->baseHp : 800,
        cfg ? cfg->baseAtk : 120,
        cfg ? cfg->baseDef : 80,
        cfg ? cfg->baseMp : 200,
        cfg ? cfg->hpPerFloor : 150,
        cfg ? cfg->atkPerFloor : 25,
        cfg ? cfg->defPerFloor : 15,
        cfg ? cfg->damageScalePerFloor : 5,
        cfg && cfg->castSkill ? " selected" : "",
        cfg && !cfg->castSkill ? " selected" : "");

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>里程碑奖励（每N层，N=里程碑值）</h2>"
        "<table><tr><th>层数</th><th>经验</th><th>金钱</th><th>物品</th><th>数量</th></tr>");
    for (i = 0; i < rewardCount; ++i)
    {
        const vm_mock_ladder_reward *rw = vm_net_mock_ladder_reward_admin_row(i);
        if (rw == NULL)
            continue;
        vm_mock_admin_text_appendf(
            &page,
            "<tr><td>%u</td><td>%u</td><td>%u</td><td>%u</td><td>%u</td></tr>",
            (u32)rw->floorMilestone, rw->rewardExp, rw->rewardMoney,
            rw->rewardItemId, rw->rewardItemCount);
    }
    vm_mock_admin_text_appendf(
        &page,
        "</table>"
        "<form method=\"post\" action=\"/action\" style=\"margin-top:12px\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-ladder-reward\">"
        "<div class=\"grid\">"
        "<div><label>里程碑层数(如10)</label><input name=\"floor_milestone\"></div>"
        "<div><label>经验</label><input name=\"reward_exp\"></div>"
        "<div><label>金钱</label><input name=\"reward_money\"></div>"
        "<div><label>物品ID</label><input name=\"reward_item_id\"></div>"
        "<div><label>数量</label><input name=\"reward_item_count\"></div>"
        "</div><button type=\"submit\">添加/更新奖励</button></form></section>"
        "</body></html>");
}

static void vm_mock_admin_handle_ladder_action(vm_mock_service_socket client,
                                               const char *action,
                                               const char *body)
{
    if (strcmp(action, "save-ladder-config") == 0)
    {
        u32 enabled = 0;
        u32 daily = 1;
        u32 tj = 920001, hj = 920002, gd = 920003;
        u32 baseHp = 800, baseAtk = 120, baseDef = 80, baseMp = 200;
        u32 hpPf = 150, atkPf = 25, defPf = 15, dmgScale = 5;
        u32 castSkill = 1;
        char errMsg[384];

        if (!vm_mock_admin_form_u32(body, "enabled", 1u, &enabled) ||
            !vm_mock_admin_form_u32(body, "daily_limit", 100u, &daily) ||
            !vm_mock_admin_form_u32(body, "enemy_tianji", 0xffffffffu, &tj) ||
            !vm_mock_admin_form_u32(body, "enemy_huanjian", 0xffffffffu, &hj) ||
            !vm_mock_admin_form_u32(body, "enemy_guidao", 0xffffffffu, &gd) ||
            !vm_mock_admin_form_u32(body, "base_hp", 0xffffffffu, &baseHp) ||
            !vm_mock_admin_form_u32(body, "base_atk", 0xffffffffu, &baseAtk) ||
            !vm_mock_admin_form_u32(body, "base_def", 0xffffffffu, &baseDef) ||
            !vm_mock_admin_form_u32(body, "base_mp", 0xffffffffu, &baseMp) ||
            !vm_mock_admin_form_u32(body, "hp_per_floor", 0xffffffffu, &hpPf) ||
            !vm_mock_admin_form_u32(body, "atk_per_floor", 0xffffffffu, &atkPf) ||
            !vm_mock_admin_form_u32(body, "def_per_floor", 0xffffffffu, &defPf) ||
            !vm_mock_admin_form_u32(body, "damage_scale", 65535u, &dmgScale) ||
            !vm_mock_admin_form_u32(body, "cast_skill", 1u, &castSkill))
        {
            vm_mock_admin_redirect_ladder(
                client, "error",
                "表单数值无效；每层伤害+% 请填写 0-65535 的整数（不要带 % 号）");
            return;
        }
        if (vm_mock_ladder_admin_save_config(
                (u8)enabled, (u16)daily, tj, hj, gd, baseHp, baseAtk, baseDef,
                baseMp, hpPf, atkPf, defPf, (u16)dmgScale, (u8)castSkill))
        {
            vm_mock_admin_redirect_ladder(client, "ok", "天梯配置已保存");
            return;
        }
        memset(errMsg, 0, sizeof(errMsg));
        {
            const char *mysqlErr = vm_mysql_last_error();
            if (mysqlErr != NULL && mysqlErr[0] != 0 &&
                strcmp(mysqlErr, "no MySQL error detail") != 0)
                snprintf(errMsg, sizeof(errMsg), "保存失败：%s", mysqlErr);
            else
                snprintf(errMsg, sizeof(errMsg), "%s", "保存失败");
        }
        vm_mock_admin_redirect_ladder(client, "error", errMsg);
        return;
    }
    if (strcmp(action, "save-ladder-reward") == 0)
    {
        u32 milestone = 0;
        u32 exp = 0;
        u32 money = 0;
        u32 itemId = 0;
        u32 itemCount = 0;

        (void)vm_mock_admin_form_u32(body, "floor_milestone", 10000u, &milestone);
        (void)vm_mock_admin_form_u32(body, "reward_exp", 0xffffffffu, &exp);
        (void)vm_mock_admin_form_u32(body, "reward_money", 0xffffffffu, &money);
        (void)vm_mock_admin_form_u32(body, "reward_item_id", 0xffffffffu, &itemId);
        (void)vm_mock_admin_form_u32(body, "reward_item_count", 0xffffffffu,
                                     &itemCount);
        if (milestone == 0)
            vm_mock_admin_redirect_ladder(client, "error", "里程碑层数无效");
        else if (vm_mock_ladder_admin_save_reward((u16)milestone, exp, money,
                                                  itemId, itemCount))
            vm_mock_admin_redirect_ladder(client, "ok", "天梯奖励已保存");
        else
            vm_mock_admin_redirect_ladder(client, "error", "保存失败");
        return;
    }
    vm_mock_admin_redirect_ladder(client, "error", "未知操作");
}
