/* Admin UI for combat formula config — included from web_admin_server.c. */

static void vm_mock_admin_redirect_combat(vm_mock_service_socket client,
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
             VM_MOCK_ADMIN_ROOT_PATH "?tab=combat&status=%s&message=%s",
             statusEncoded, messageEncoded);
    vm_mock_admin_send_location(client, location, NULL);
}

static void vm_mock_admin_combat_field(vm_mock_admin_text *page, const char *label,
                                       const char *name, u32 value,
                                       const char *hint)
{
    vm_mock_admin_text_appendf(
        page,
        "<div><label>%s</label><input name=\"%s\" value=\"%u\">"
        "<p class=\"muted\">%s</p></div>",
        label ? label : "", name ? name : "", value, hint ? hint : "");
}

static void vm_mock_admin_render_combat_page(char *response, size_t responseCap,
                                             const char *query)
{
    vm_mock_admin_text page;
    char status[16];
    char message[256];
    const vm_net_mock_combat_formula_config *cfg;

    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status));
    memset(message, 0, sizeof(message));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    cfg = vm_net_mock_combat_formula_config_get();

    vm_mock_admin_text_appendf(
        &page,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>江湖OL 战斗公式</title><style>"
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
        ".grid2{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px}"
        "input{border:1px solid #d0d5dd;border-radius:6px;padding:8px 9px;"
        "background:#fff;width:100%%}"
        "button{border:0;border-radius:6px;padding:8px 12px;"
        "background:#175cd3;color:#fff;cursor:pointer;margin-top:12px}"
        "h2{font-size:16px;margin:0 0 12px}.muted{color:#98a2b3;font-size:12px;margin:4px 0 0}"
        "code{background:#f2f4f7;padding:1px 4px;border-radius:4px;font-size:12px}"
        "</style></head><body><main class=\"wrap\">"
        "<header><h1>战斗公式</h1>"
        "<p class=\"sub\">配置主属性物攻系数、普攻软护甲、命中/闪躲、暴击、抗性、技能加成、决斗浮动与 Boss 技能倍率。"
        "保存后立即影响服务端结算；环境变量 <code>CBE_BATTLE_*</code> 仍可覆盖调试项。</p></header>"
        "<nav class=\"tabs\">"
        "<a class=\"tab\" href=\"/?tab=accounts\">账号管理</a>"
        "<a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a>"
        "<a class=\"tab\" href=\"/?tab=tasks\">任务管理</a>"
        "<a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a>"
        "<a class=\"tab\" href=\"/?tab=shop\">商品管理</a>"
        "<a class=\"tab\" href=\"/?tab=chests\">宝箱奖励</a>"
        "<a class=\"tab\" href=\"/?tab=enhance\">强化配置</a>"
        "<a class=\"tab on\" href=\"/?tab=combat\">战斗公式</a>"
        "<a class=\"tab\" href=\"/?tab=arena\">比武</a>"
        "<a class=\"tab\" href=\"/?tab=updates\">游戏内容更新管理</a>"
        "<a class=\"tab\" href=\"/?tab=servers\">服务器列表</a>"
        "</nav>");

    if (status[0] != 0 && message[0] != 0)
    {
        vm_mock_admin_text_appendf(
            &page, "<div class=\"notice %s\">%s</div>",
            strcmp(status, "ok") == 0 ? "ok" : "error", message);
    }

    vm_mock_admin_text_appendf(
        &page,
        "<form method=\"post\" action=\"/action\">"
        "<input type=\"hidden\" name=\"action\" value=\"save-combat-formula\">");

    vm_mock_admin_text_appendf(
        &page, "<section class=\"card\"><h2>面板属性</h2><div class=\"grid2\">");
    vm_mock_admin_combat_field(
        &page, "护甲基础值", "panel_defense_base",
        cfg ? cfg->panelDefenseBase : 4,
        "defense = 基础 + 等级×系数 + 耐力/除数 + 装备护甲");
    vm_mock_admin_combat_field(
        &page, "护甲等级系数", "panel_defense_level_coeff",
        cfg ? cfg->panelDefenseLevelCoeff : 1, "等级乘到面板护甲的系数");
    vm_mock_admin_combat_field(
        &page, "耐力除数", "panel_defense_endurance_div",
        cfg ? cfg->panelDefenseEnduranceDiv : 2, "耐力贡献 = 耐力 ÷ 此值（最小 1）");
    vm_mock_admin_combat_field(
        &page, "命中基础值", "panel_hit_base",
        cfg ? cfg->panelHitBase : 101, "hit = 基础 + 装备命中");
    vm_mock_admin_text_appendf(&page, "</div></section>");

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>主属性物攻加成系数</h2><div class=\"grid2\">");
    vm_mock_admin_combat_field(
        &page, "天机主属性→物攻%%", "primary_attack_coeff_tianji_percent",
        cfg ? cfg->primaryAttackCoeffTianjiPercent : 100,
        "attack = 力量×此%%/100 + 装备物攻；100=全额");
    vm_mock_admin_combat_field(
        &page, "幻剑主属性→物攻%%", "primary_attack_coeff_huanjian_percent",
        cfg ? cfg->primaryAttackCoeffHuanjianPercent : 100,
        "attack = 敏捷×此%%/100 + 装备物攻；100=全额");
    vm_mock_admin_combat_field(
        &page, "鬼道主属性→物攻%%", "primary_attack_coeff_guidao_percent",
        cfg ? cfg->primaryAttackCoeffGuidaoPercent : 100,
        "attack = 智慧×此%%/100 + 装备物攻；100=全额");
    vm_mock_admin_text_appendf(&page, "</div></section>");

    vm_mock_admin_text_appendf(
        &page, "<section class=\"card\"><h2>普攻护甲减伤</h2><div class=\"grid2\">");
    vm_mock_admin_combat_field(
        &page, "软护甲曲线 K", "soft_armor_k",
        cfg ? cfg->softArmorK : 2000,
        "damage = (攻击×K + 防御/2) / (K + 防御)");
    vm_mock_admin_combat_field(
        &page, "装备护甲除数", "equip_armor_divisor",
        cfg ? cfg->equipArmorDivisor : 5,
        "战斗减伤用 bare + 装备护甲/除数；面板仍显示满护甲");
    vm_mock_admin_text_appendf(&page, "</div></section>");

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>命中 / 闪躲 / 暴击 / 抗性</h2><div class=\"grid2\">");
    vm_mock_admin_combat_field(
        &page, "命中率下限%%", "hit_chance_min",
        cfg ? cfg->hitChanceMin : 5, "hitChance = clamp(命中-闪躲, 下限, 上限)");
    vm_mock_admin_combat_field(
        &page, "命中率上限%%", "hit_chance_max",
        cfg ? cfg->hitChanceMax : 95, "百分比 0–100");
    vm_mock_admin_combat_field(
        &page, "暴击伤害%%", "crit_damage_percent",
        cfg ? cfg->critDamagePercent : 150, "暴击倍率，100=无加成，150=1.5 倍");
    vm_mock_admin_combat_field(
        &page, "抗性减伤上限%%", "resist_cap_percent",
        cfg ? cfg->resistCapPercent : 70, "技能抗性百分比硬顶");
    vm_mock_admin_text_appendf(&page, "</div></section>");

    vm_mock_admin_text_appendf(
        &page, "<section class=\"card\"><h2>技能与决斗</h2><div class=\"grid2\">");
    vm_mock_admin_combat_field(
        &page, "PvE 技能职业加成%%", "job_skill_attack_bonus_percent",
        cfg ? cfg->jobSkillAttackBonusPercent : 5, "玩家技能最终伤害额外加成");
    vm_mock_admin_combat_field(
        &page, "决斗普攻浮动±%%", "na_variance_percent",
        cfg ? cfg->naVariancePercent : 5, "决斗普攻伤害随机浮动幅度");
    vm_mock_admin_combat_field(
        &page, "决斗技能浮动±%%", "skill_variance_percent",
        cfg ? cfg->skillVariancePercent : 8, "决斗技能伤害随机浮动幅度");
    vm_mock_admin_combat_field(
        &page, "决斗天机主属性加成%%", "duel_job_bonus_tianji_percent",
        cfg ? cfg->duelJobBonusTianjiPercent : 10, "职业 1");
    vm_mock_admin_combat_field(
        &page, "决斗幻剑主属性加成%%", "duel_job_bonus_huanjian_percent",
        cfg ? cfg->duelJobBonusHuanjianPercent : 8, "职业 2");
    vm_mock_admin_combat_field(
        &page, "决斗鬼道主属性加成%%", "duel_job_bonus_guidao_percent",
        cfg ? cfg->duelJobBonusGuidaoPercent : 12, "职业 3");
    vm_mock_admin_combat_field(
        &page, "单体技能相对普攻保底%%", "skill_vs_na_floor_percent",
        cfg ? cfg->skillVsNaFloorPercent : 90, "单目标技能不低于普攻参考伤害的此比例");
    vm_mock_admin_text_appendf(&page, "</div></section>");

    vm_mock_admin_text_appendf(
        &page,
        "<section class=\"card\"><h2>怪物副属性默认值</h2><div class=\"grid2\">");
    vm_mock_admin_combat_field(
        &page, "怪物默认命中", "monster_hit_default",
        cfg ? cfg->monsterHitDefault : 100, "可被 CBE_BATTLE_MONSTER_HIT 覆盖");
    vm_mock_admin_combat_field(
        &page, "怪物默认闪躲", "monster_dodge_default",
        cfg ? cfg->monsterDodgeDefault : 0, "可被 CBE_BATTLE_MONSTER_DODGE 覆盖");
    vm_mock_admin_combat_field(
        &page, "怪物默认暴击", "monster_crit_default",
        cfg ? cfg->monsterCritDefault : 0, "可被 CBE_BATTLE_MONSTER_CRIT 覆盖");
    vm_mock_admin_combat_field(
        &page, "怪物默认抗性", "monster_resist_default",
        cfg ? cfg->monsterResistDefault : 0, "可被 CBE_BATTLE_MONSTER_RESIST 覆盖");
    vm_mock_admin_text_appendf(&page, "</div></section>");

    vm_mock_admin_text_appendf(
        &page, "<section class=\"card\"><h2>Boss 技能反击</h2><div class=\"grid2\">");
    vm_mock_admin_combat_field(
        &page, "Boss 放技能概率%%", "boss_skill_chance_percent",
        cfg ? cfg->bossSkillChancePercent : 45,
        "可被 CBE_BATTLE_BOSS_SKILL_CHANCE 覆盖");
    vm_mock_admin_combat_field(
        &page, "Boss 技能伤害%%", "boss_skill_damage_percent",
        cfg ? cfg->bossSkillDamagePercent : 300,
        "相对抗性减伤后基础伤害的倍率；可被 CBE_BATTLE_BOSS_SKILL_DAMAGE_PCT 覆盖");
    vm_mock_admin_text_appendf(
        &page,
        "</div><button type=\"submit\">保存战斗公式</button>"
        "<p class=\"muted\">表：server_combat_formula_config。未改动的项保持当前值；"
        "非法值会在服务端钳位。</p></section></form></main></body></html>");
}

static void vm_mock_admin_handle_combat_action(vm_mock_service_socket client,
                                               const char *action,
                                               const char *body)
{
    vm_net_mock_combat_formula_config cfg;
    const vm_net_mock_combat_formula_config *cur;

    if (strcmp(action, "save-combat-formula") != 0)
    {
        vm_mock_admin_redirect_combat(client, "error", "未知操作");
        return;
    }

    cur = vm_net_mock_combat_formula_config_get();
    if (cur == NULL)
    {
        vm_mock_admin_redirect_combat(client, "error", "配置未加载");
        return;
    }
    cfg = *cur;

    (void)vm_mock_admin_form_u32(body, "panel_defense_base", 100000u,
                                 &cfg.panelDefenseBase);
    (void)vm_mock_admin_form_u32(body, "panel_defense_level_coeff", 1000u,
                                 &cfg.panelDefenseLevelCoeff);
    (void)vm_mock_admin_form_u32(body, "panel_defense_endurance_div", 1000u,
                                 &cfg.panelDefenseEnduranceDiv);
    (void)vm_mock_admin_form_u32(body, "panel_hit_base", 100000u,
                                 &cfg.panelHitBase);
    (void)vm_mock_admin_form_u32(body, "primary_attack_coeff_tianji_percent",
                                 1000u, &cfg.primaryAttackCoeffTianjiPercent);
    (void)vm_mock_admin_form_u32(body, "primary_attack_coeff_huanjian_percent",
                                 1000u, &cfg.primaryAttackCoeffHuanjianPercent);
    (void)vm_mock_admin_form_u32(body, "primary_attack_coeff_guidao_percent",
                                 1000u, &cfg.primaryAttackCoeffGuidaoPercent);
    (void)vm_mock_admin_form_u32(body, "soft_armor_k", 1000000u, &cfg.softArmorK);
    (void)vm_mock_admin_form_u32(body, "equip_armor_divisor", 1000u,
                                 &cfg.equipArmorDivisor);
    (void)vm_mock_admin_form_u32(body, "hit_chance_min", 100u, &cfg.hitChanceMin);
    (void)vm_mock_admin_form_u32(body, "hit_chance_max", 100u, &cfg.hitChanceMax);
    (void)vm_mock_admin_form_u32(body, "crit_damage_percent", 1000u,
                                 &cfg.critDamagePercent);
    (void)vm_mock_admin_form_u32(body, "resist_cap_percent", 100u,
                                 &cfg.resistCapPercent);
    (void)vm_mock_admin_form_u32(body, "job_skill_attack_bonus_percent", 500u,
                                 &cfg.jobSkillAttackBonusPercent);
    (void)vm_mock_admin_form_u32(body, "na_variance_percent", 50u,
                                 &cfg.naVariancePercent);
    (void)vm_mock_admin_form_u32(body, "skill_variance_percent", 50u,
                                 &cfg.skillVariancePercent);
    (void)vm_mock_admin_form_u32(body, "duel_job_bonus_tianji_percent", 500u,
                                 &cfg.duelJobBonusTianjiPercent);
    (void)vm_mock_admin_form_u32(body, "duel_job_bonus_huanjian_percent", 500u,
                                 &cfg.duelJobBonusHuanjianPercent);
    (void)vm_mock_admin_form_u32(body, "duel_job_bonus_guidao_percent", 500u,
                                 &cfg.duelJobBonusGuidaoPercent);
    (void)vm_mock_admin_form_u32(body, "skill_vs_na_floor_percent", 200u,
                                 &cfg.skillVsNaFloorPercent);
    (void)vm_mock_admin_form_u32(body, "monster_hit_default", 1000u,
                                 &cfg.monsterHitDefault);
    (void)vm_mock_admin_form_u32(body, "monster_dodge_default", 1000u,
                                 &cfg.monsterDodgeDefault);
    (void)vm_mock_admin_form_u32(body, "monster_crit_default", 100u,
                                 &cfg.monsterCritDefault);
    (void)vm_mock_admin_form_u32(body, "monster_resist_default", 100u,
                                 &cfg.monsterResistDefault);
    (void)vm_mock_admin_form_u32(body, "boss_skill_chance_percent", 100u,
                                 &cfg.bossSkillChancePercent);
    (void)vm_mock_admin_form_u32(body, "boss_skill_damage_percent", 1000u,
                                 &cfg.bossSkillDamagePercent);

    if (vm_net_mock_combat_formula_admin_save(&cfg))
        vm_mock_admin_redirect_combat(client, "ok", "战斗公式已保存");
    else
        vm_mock_admin_redirect_combat(client, "error", "保存战斗公式失败");
}
