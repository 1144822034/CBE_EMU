static bool vm_net_mock_append_battle_terminal_status_objects(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
    bool forceTeamVictory, bool *statusAppendedOut);
static bool vm_net_mock_append_battle_terminal_case9_object(
    u8 *out, u32 outCap, u32 *pos);
static void vm_net_mock_battle_project_terminal_vitals(
    u32 currentHp, u32 currentHpMax, u32 currentMp, u32 currentMpMax,
    u32 *finalHpOut, u32 *finalMpOut);
static bool vm_net_mock_scene_hangup_result_panel_active(void);
static void vm_net_mock_battle_auto_reset(void);
static void vm_net_mock_battle_auto_arm(void);
static bool vm_net_mock_battle_terminal_close_should_defer(bool battleEndsThisRound);
static void vm_net_mock_battle_schedule_terminal_close_after_actions(
    u8 actionCount, const char *source);
static u32 vm_net_mock_build_hangup_battle_start_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap);

/* Battle.cbm's text helper at 0x2456 decodes these child-action flags:
 * 2 -> "暴击"; 3 -> "闪躲".  A zero HP delta is the numerical outcome;
 * this flag chooses its native presentation. */
enum
{
    VM_NET_MOCK_BATTLE_CHILD_FLAG_CRITICAL = 2,
    VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE = 3
};

static u32 vm_net_mock_build_battle_scene_start_info_blob(u8 *out, u32 outCap,
                                                          u32 sceneMonsterIndex,
                                                          u32 sceneMonsterX,
                                                          u32 sceneMonsterY,
                                                          u8 monsterCount,
                                                          u32 roleId)
{
    u32 pos = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleIdDefault = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleHp = 0;
    u32 roleMaxHp = 0;
    u32 roleMp = 0;
    u32 roleMaxMp = 0;

    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    roleHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", roleHpDefault);
    roleMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", roleMaxHpDefault);
    roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", roleMpDefault);
    roleMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_MP", roleMaxMpDefault);
    if (roleId == 0)
        roleId = roleIdDefault;
    if (roleMaxHp < roleMaxHpDefault)
        roleMaxHp = roleMaxHpDefault;
    if (roleMaxMp < roleMaxMpDefault)
        roleMaxMp = roleMaxMpDefault;
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;

    /*
     * Battle.cbm HandleBattleStartMsg(0x66CC), subtype 5, is the native
     * scene-monster entry path. After the first count it reads scene index,
     * posx, and posy, then copies the left fighter from the Battle.cbm scene
     * actor table at *(R9+13476) once for every left-side unit. Counts 1..3
     * are positioned by the client itself.
     */
    if (monsterCount < 1)
        monsterCount = 1;
    if (monsterCount > 3)
        monsterCount = 3;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, monsterCount))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterIndex))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterX))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterY))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMaxHp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMp))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleMaxMp))
        return 0;
    return pos;
}

static u32 vm_net_mock_build_team_battle_scene_start_info_blob(
    u8 *out,
    u32 outCap,
    u32 sceneMonsterIndex,
    u32 sceneMonsterX,
    u32 sceneMonsterY,
    u8 monsterCount,
    vm_mock_service_team *team,
    vm_mock_service_client_session *observer,
    const char *scene,
    u8 *partyCountOut)
{
    u32 memberClientIds[VM_MOCK_SERVICE_TEAM_MEMBER_MAX] = {0};
    u32 pos = 0;
    u8 partyCount = 0;

    if (partyCountOut)
        *partyCountOut = 0;
    if (team == NULL || observer == NULL ||
        !vm_mock_service_team_contains_client(team, observer->clientId))
    {
        return vm_net_mock_build_battle_scene_start_info_blob(
            out, outCap, sceneMonsterIndex, sceneMonsterX, sceneMonsterY,
            monsterCount, observer ? observer->onlineRoleId : 0);
    }

    if (observer->pendingTeamBattleSerial != 0 &&
        team->battleActive &&
        observer->pendingTeamBattleSerial == team->battleSerial)
    {
        partyCount = team->battleMemberCount;
        memcpy(memberClientIds, team->battleMemberClientIds, sizeof(memberClientIds));
    }
    else
    {
        partyCount = vm_mock_service_team_collect_battle_members(
            team, scene, memberClientIds);
    }
    if (partyCount < 2)
    {
        return vm_net_mock_build_battle_scene_start_info_blob(
            out, outCap, sceneMonsterIndex, sceneMonsterX, sceneMonsterY,
            monsterCount, observer->onlineRoleId);
    }
    if (partyCount > VM_MOCK_SERVICE_TEAM_MEMBER_MAX)
        partyCount = VM_MOCK_SERVICE_TEAM_MEMBER_MAX;
    if (monsterCount < 1)
        monsterCount = 1;
    if (monsterCount > 3)
        monsterCount = 3;

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, monsterCount) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterIndex) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterX) ||
        !vm_net_mock_seq_put_u32(out, outCap, &pos, sceneMonsterY) ||
        !vm_net_mock_seq_put_u8(out, outCap, &pos, partyCount))
    {
        return 0;
    }

    for (u8 i = 0; i < partyCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(memberClientIds[i]);
        u32 wireId = vm_mock_service_team_member_wire_id(observer, member);
        u32 hpMax = member && member->onlineHpMax ? member->onlineHpMax : 1;
        u32 hp = member ? member->onlineHp : 0;
        u32 mpMax = member ? member->onlineMpMax : 0;
        u32 mp = member ? member->onlineMp : 0;

        if (member == NULL || wireId == 0)
            return 0;
        if (hp > hpMax)
            hp = hpMax;
        if (mp > mpMax)
            mp = mpMax;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, wireId) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, hp) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, hpMax) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, mp) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, mpMax))
        {
            return 0;
        }
        printf("[info][network] mock_team_battle_member_row observer=%08x "
               "member=%08x/%u wire=%u hp=%u/%u mp=%u/%u\n",
               observer->clientId,
               member->clientId,
               member->onlineRoleId,
               wireId,
               hp, hpMax, mp, mpMax);
    }
    if (partyCountOut)
        *partyCountOut = partyCount;
    return pos;
}

static u32 vm_net_mock_build_pending_team_battle_start_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer)
{
    vm_mock_service_team *team = NULL;
    u8 battleInfo[192];
    u32 battleInfoLen = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u32 pendingSerial = 0;
    u32 hp = 0;
    u32 hpMax = 1;
    u32 mp = 0;
    u32 mpMax = 0;
    u8 partyCount = 0;
    int rosterPreambleCount = 0;

    if (out == NULL || outCap < pos || observer == NULL ||
        observer->pendingTeamBattleSerial == 0)
    {
        return 0;
    }
    pendingSerial = observer->pendingTeamBattleSerial;
    team = vm_mock_service_team_find_for_client(observer->clientId);
    if (team == NULL || !team->battleActive ||
        team->battleSerial != pendingSerial ||
        !vm_mock_service_team_battle_contains_client(team, observer->clientId) ||
        !vm_mock_service_session_scene_is_visible(observer, team->battleScene))
    {
        printf("[warn][mock-service] team_battle_drop observer=%08x serial=%u "
               "reason=stale-or-scene-changed\n",
               observer->clientId, pendingSerial);
        observer->pendingTeamBattleSerial = 0;
        return 0;
    }

    memset(battleInfo, 0, sizeof(battleInfo));
    battleInfoLen = vm_net_mock_build_team_battle_scene_start_info_blob(
        battleInfo, sizeof(battleInfo),
        team->battleSceneMonsterIndex,
        team->battleSceneMonsterX,
        team->battleSceneMonsterY,
        team->battleMonsterCount,
        team,
        observer,
        team->battleScene,
        &partyCount);
    if (battleInfoLen == 0 || battleInfoLen > 0xffff)
        return 0;
    rosterPreambleCount = vm_net_mock_append_team_battle_roster_preamble(
        out, outCap, &pos, observer, team);
    if (rosterPreambleCount < 0 || rosterPreambleCount > 0xfd)
        return 0;
    if (!vm_net_mock_append_scene_monster_moveinfo2_object(
            out, outCap, &pos,
            team->battleEnemyId,
            team->battleSceneMonsterX,
            team->battleSceneMonsterY))
    {
        return 0;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 5, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "side", team->battleSide) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "battleinfo",
                                    battleInfo, (u16)battleInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, (u8)(2 + rosterPreambleCount));

    hpMax = observer->onlineHpMax ? observer->onlineHpMax : 1;
    hp = observer->onlineHp;
    mpMax = observer->onlineMpMax;
    mp = observer->onlineMp;
    if (hp > hpMax)
        hp = hpMax;
    if (mp > mpMax)
        mp = mpMax;
    g_mockBattleOperateSessionArmed = 1;
    vm_net_mock_battle_auto_reset();
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleEnemyCountCurrent = team->battleMonsterCount;
    g_mockBattleOperateTurnCounter = 0;
    memset(&g_vm_net_mock_battle_solo_modifier, 0,
           sizeof(g_vm_net_mock_battle_solo_modifier));
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    memset(g_vm_net_mock_battle_enemy_effects_current, 0,
           sizeof(g_vm_net_mock_battle_enemy_effects_current));
    g_vm_net_mock_battle_enemy_effect_changed_mask = 0;
    memset(&g_vm_net_mock_battle_active_enemy_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_enemy_modifier_current));
    ++g_mockBattleOperateSessionSerial;
    g_vm_net_mock_battle_rewarded_exp = 0;
    memset(g_vm_net_mock_battle_rewarded_drops, 0,
           sizeof(g_vm_net_mock_battle_rewarded_drops));
    g_vm_net_mock_battle_rewarded_drop_result_count = 0;
    g_vm_net_mock_battle_settlement_sent_serial = 0;
    g_vm_net_mock_battle_terminal_close_not_before_tick = 0;
    g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
    g_vm_net_mock_battle_recovered_serial = 0;
    g_vm_net_mock_battle_role_id_current = observer->onlineRoleId;
    g_vm_net_mock_battle_enemy_id_current = team->battleEnemyId;
    g_mockBattleRoleHpCurrent = hp;
    g_mockBattleRoleHpMax = hpMax;
    g_mockBattleRoleMpCurrent = mp;
    g_mockBattleRoleMpMax = mpMax;
    vm_net_mock_battle_reset_enemy_hp_from_stats(team->battleEnemyId);

    observer->pendingTeamBattleSerial = 0;
    printf("[info][mock-service] team_battle_deliver serial=%u observer=%08x "
           "leader=%08x enemy=%u scene=%s party=%u subtype=5 side=%u "
           "roster_preamble=%d objects=%u resp=%u evidence=mmBattle:0x7BD0->0x66CC\n",
           pendingSerial,
           observer->clientId,
           team->battleLeaderClientId,
           team->battleEnemyId,
           team->battleScene,
           partyCount,
           team->battleSide,
           rosterPreambleCount,
           (u32)(2 + rosterPreambleCount),
           pos);
    return pos;
}

static u32 vm_net_mock_normalize_battle_enemy_id(u32 requestedId)
{
    u32 defaultEnemyId = vm_net_mock_env_u32("CBE_BATTLE_DEFAULT_ENEMY_ID", 105);
    if (defaultEnemyId == 0)
        defaultEnemyId = 105;
    if (requestedId == 0 || requestedId == 10001)
        return defaultEnemyId;
    return requestedId;
}

static u32 vm_net_mock_resolve_battle_enemy_id(u32 requestedId, u32 *tableBaseOut, u32 tableIds[4])
{
#ifdef CBE_SERVER_ONLY
    /* The battle CBM/template table belongs to the remote CBE client.  The
     * service must never inspect it: the collision WT request is normalized
     * against server monster data and that authoritative id is returned on the
     * wire.  A client whose resources lack that template must request a
     * resource update through the normal protocol, not borrow a template from
     * a locally embedded emulator. */
    if (tableBaseOut)
        *tableBaseOut = 0;
    if (tableIds)
        memset(tableIds, 0, sizeof(u32) * 4);
    return requestedId;
#else
    u32 loaderR9 = vm_screen_stack_lookup_module_base(vmAddedScreen);
    u32 battleBase = 0;
    u32 enemyTable = 0;
    u32 firstNonzero = 0;

    if (tableBaseOut)
        *tableBaseOut = 0;
    if (tableIds)
        memset(tableIds, 0, sizeof(u32) * 4);
    if (loaderR9 == 0)
        loaderR9 = g_currentScreenModuleBase;
    if (loaderR9 == 0)
        return requestedId;

    battleBase = loaderR9 + 0x3450u;
    if (uc_mem_read(MTK, battleBase + 0x50u, &enemyTable, 4) != UC_ERR_OK || enemyTable == 0)
        return requestedId;
    if (tableBaseOut)
        *tableBaseOut = enemyTable;

    for (u32 i = 0; i < 4; ++i)
    {
        u32 id = 0;
        (void)uc_mem_read(MTK, enemyTable + i * 0x4Cu + 0x24u, &id, 4);
        if (tableIds)
            tableIds[i] = id;
        if (id == requestedId)
            return requestedId;
        if (firstNonzero == 0 && id != 0)
            firstNonzero = id;
    }

    /*
     * A request id that is not already present in Battle.cbm's enemy template
     * table is not render-safe: sub_66CC takes the lookup-failed path and the
     * later fighter draw loop calls a zero callback. Keep the table id as the
     * default crash-safe server contract; force the request id only for focused
     * experiments while recovering the upstream template-population contract.
     */
    if (requestedId != 0 && vm_net_mock_env_u32("CBE_BATTLE_FORCE_REQUEST_ENEMY_ID", 0) != 0)
        return requestedId;
    return firstNonzero ? firstNonzero : requestedId;
#endif
}

static bool vm_net_mock_is_battle_operate_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 4 &&
           object.subtype == 2 &&
           vm_net_mock_request_contains(request, requestLen, "index") &&
           vm_net_mock_request_contains(request, requestLen, "Operate");
}

static bool vm_net_mock_parse_battle_item_use_request(const u8 *request, u32 requestLen,
                                                      vm_net_mock_battle_item_use_request *parsedOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_battle_item_use_request parsed;
    u32 index = 0;
    u32 seq = 0;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 4 || object.subtype != 3 ||
        object.payloadLen == 0)
    {
        return false;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "index", &index))
        return false;
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "seq", &seq) &&
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemseq", &seq) &&
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemSeq", &seq))
    {
        return false;
    }
    if (seq == 0 || seq > 0xffffu)
        return false;

    parsed.index = index;
    parsed.seq = (u16)seq;
    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_is_battle_operate_request_relaxed(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9)
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return object.major == 1 &&
           object.kind == 4 &&
           object.subtype == 2;
}

static bool vm_net_mock_current_screen_is_battle(void)
{
#ifdef CBE_SERVER_ONLY
    /* Battle ownership is recorded by the authoritative service session.  A
     * remote request must not probe an emulator screen stack that belongs to
     * neither this process nor the requesting client. */
    return false;
#else
    u32 inferredCodeBase = 0;
    u32 inferredModuleR9 = 0;

    if (vmAddedScreen != 0 &&
        vm_infer_battle_module_from_screen(vmAddedScreen, &inferredCodeBase, &inferredModuleR9))
        return true;
    if (g_currentScreenThis != 0)
    {
        u32 screen = g_currentScreenThis + 0x18u;
        if (vm_infer_battle_module_from_screen(screen, &inferredCodeBase, &inferredModuleR9))
            return true;
    }
    return false;
#endif
}

static bool vm_net_mock_battle_operate_is_skill(u32 operate)
{
    return operate > 2;
}

static bool vm_net_mock_battle_inline_settlement_enabled(void)
{
    /*
     * If 4/7 arrives only on the next request, the panel has already copied
     * zeroed settlement caches. Keep 4/7 in the terminal response by default
     * and make hp/mp recovery deltas explicit zero unless an env override opts
     * in.
     */
    return vm_net_mock_env_u32("CBE_BATTLE_INLINE_SETTLEMENT", 1) != 0;
}

static bool vm_net_mock_battle_terminal_action_enabled(void)
{
    /*
     * Runtime negatives showed that appending a separate type=3 terminal action
     * after the final player hit can swallow the visible last attack or disturb
     * later target selection in multi-monster battles. The settlement object is
     * the authoritative end-of-battle signal; keep the terminal action as an
     * explicit experiment only.
     */
    return vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTION_ENABLED", 0) != 0;
}

static u32 vm_net_mock_battle_operate_skill_id(u32 operate)
{
    return operate > 2 ? operate - 2 : 0;
}

static const vm_net_mock_skill_catalog_item *vm_net_mock_battle_operate_skill(u32 operate)
{
    u32 skillId = vm_net_mock_battle_operate_skill_id(operate);

    if (skillId == 0)
        return NULL;
    return vm_net_mock_find_skill_catalog_item(skillId);
}

static u32 vm_net_mock_battle_operate_skill_effect(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    if (skill != NULL && skill->effectIndex != 0)
        return skill->effectIndex;
    return 0;
}

static bool vm_net_mock_battle_operate_skill_targets_enemy_group(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    /* skill.dsh `目标指向` 4: group of opposing battle units. */
    return skill != NULL && skill->targetDirection == 4;
}

static bool vm_net_mock_battle_operate_skill_targets_friendly_group_heal(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    /* skill.dsh `目标指向` 2 is the whole friendly side.  Limit this branch
     * to actual positive-HP skills: buffs share the target direction, but do
     * not have an HP delta to apply. */
    return skill != NULL && skill->targetDirection == 2 && skill->hpChange > 0;
}

static bool vm_net_mock_battle_operate_skill_targets_friendly_single_heal(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    /* `效果=3` is the one-target revive branch, not an ordinary heal. */
    return skill != NULL && skill->targetDirection == 1 && skill->hpChange > 0 &&
           skill->effectKind != 3;
}

static bool vm_net_mock_battle_operate_skill_targets_friendly_single_revive(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    return skill != NULL && skill->targetDirection == 1 && skill->hpChange > 0 &&
           skill->effectKind == 3;
}

static u32 vm_net_mock_battle_player_skill_heal_to_role(u32 operate,
                                                        u32 hpCurrent,
                                                        u32 hpMax)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats playerStats;
    uint64_t scaled = 0;
    uint64_t amount = 0;

    if (skill == NULL || skill->hpChange <= 0 || hpCurrent >= hpMax || hpMax == 0)
        return 0;

    memset(&playerStats, 0, sizeof(playerStats));
    vm_net_mock_role_build_player_stats(role, &playerStats);
    vm_net_mock_battle_apply_active_stat_modifier(&playerStats);
    scaled += (uint64_t)playerStats.strength * skill->strengthCoeff;
    scaled += (uint64_t)playerStats.agility * skill->agilityCoeff;
    scaled += (uint64_t)playerStats.wisdom * skill->wisdomCoeff;
    scaled = (scaled + 50u) / 100u;
    amount = (uint64_t)(u32)skill->hpChange + scaled;
    if (amount > (uint64_t)(hpMax - hpCurrent))
        amount = hpMax - hpCurrent;
    return amount > 0xffffffffull ? 0xffffffffu : (u32)amount;
}

/* Build the actioninfo child list and update only the active operation's
 * snapshot.  Team HP is committed later in finish_operation, after the
 * request has passed the round-barrier validation. */
static u8 vm_net_mock_battle_apply_player_friendly_group_heal_targets(
    u32 operate, u8 playerWireSlot, u8 targetWireSlots[3], u32 healValues[3])
{
    u8 targetCount = 0;

    if (targetWireSlots == NULL || healValues == NULL)
        return 0;

    if (g_vm_net_mock_team_battle_party_count_current >= 2 &&
        g_vm_net_mock_team_battle_member_count_current >= 2)
    {
        u8 memberCount = g_vm_net_mock_team_battle_member_count_current;

        if (memberCount > 3)
            memberCount = 3;
        for (u8 member = 0; member < memberCount; ++member)
        {
            u32 hpCurrent = g_vm_net_mock_team_battle_member_hp_current[member];
            u32 hpMax = g_vm_net_mock_team_battle_member_hp_max_current[member];
            u32 healed = 0;

            /* 三花聚顶 heals allies that are alive; it is not a revive. */
            if (hpCurrent == 0 || hpMax == 0)
                continue;
            healed = vm_net_mock_battle_player_skill_heal_to_role(
                operate, hpCurrent, hpMax);
            targetWireSlots[targetCount] = vm_net_mock_team_battle_display_to_wire_slot(member);
            healValues[targetCount] = healed;
            ++targetCount;
            if (healed != 0)
            {
                g_vm_net_mock_team_battle_member_hp_current[member] = hpCurrent + healed;
                g_vm_net_mock_team_battle_group_hp_changed_mask = (u8)(
                    g_vm_net_mock_team_battle_group_hp_changed_mask | (u8)(1u << member));
            }
        }
        if (g_vm_net_mock_team_battle_actor_slot_current < memberCount)
        {
            g_mockBattleRoleHpCurrent = g_vm_net_mock_team_battle_member_hp_current[
                g_vm_net_mock_team_battle_actor_slot_current];
            g_mockBattleRoleHpMax = g_vm_net_mock_team_battle_member_hp_max_current[
                g_vm_net_mock_team_battle_actor_slot_current];
        }
        return targetCount;
    }

    targetWireSlots[0] = playerWireSlot;
    healValues[0] = vm_net_mock_battle_player_skill_heal_to_role(
        operate, g_mockBattleRoleHpCurrent, g_mockBattleRoleHpMax);
    g_mockBattleRoleHpCurrent += healValues[0];
    return 1;
}

/* The 4/2 `index` field is already a battle wire slot (mmBattle sub_2B26).
 * For target-direction 1 use that slot as an ally selection; unlike an enemy
 * operation it must never go through target_wire_slot_from_request(), which
 * intentionally remaps scene-monster requests onto an enemy slot. */
static bool vm_net_mock_battle_friendly_member_for_wire_slot(u8 wireSlot,
                                                             u8 *memberOut)
{
    u8 memberCount = g_vm_net_mock_team_battle_member_count_current;

    if (memberOut != NULL)
        *memberOut = 0;
    if (g_vm_net_mock_team_battle_party_count_current < 2 || memberCount < 2)
        return false;
    if (memberCount > 3)
        memberCount = 3;
    for (u8 member = 0; member < memberCount; ++member)
    {
        if (vm_net_mock_team_battle_display_to_wire_slot(member) == wireSlot)
        {
            if (memberOut != NULL)
                *memberOut = member;
            return true;
        }
    }
    return false;
}

static u8 vm_net_mock_battle_apply_player_friendly_single_heal_target(
    u32 operate, u8 requestedWireSlot, u8 playerWireSlot, bool revive,
    u8 targetWireSlots[3], u32 healValues[3])
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    u8 member = 0;
    u32 hpCurrent = 0;
    u32 hpMax = 0;
    u32 healed = 0;

    if (targetWireSlots == NULL || healValues == NULL || skill == NULL ||
        skill->targetDirection != 1 || skill->hpChange <= 0)
    {
        return 0;
    }

    if (g_vm_net_mock_team_battle_party_count_current >= 2 &&
        vm_net_mock_battle_friendly_member_for_wire_slot(requestedWireSlot, &member))
    {
        hpCurrent = g_vm_net_mock_team_battle_member_hp_current[member];
        hpMax = g_vm_net_mock_team_battle_member_hp_max_current[member];
        if (revive)
        {
            if (hpCurrent == 0 && hpMax != 0)
                healed = vm_net_mock_min_u32((u32)skill->hpChange, hpMax);
        }
        else if (hpCurrent != 0)
        {
            healed = vm_net_mock_battle_player_skill_heal_to_role(
                operate, hpCurrent, hpMax);
        }
        targetWireSlots[0] = requestedWireSlot;
        healValues[0] = healed;
        if (healed != 0)
        {
            g_vm_net_mock_team_battle_member_hp_current[member] = hpCurrent + healed;
            g_vm_net_mock_team_battle_group_hp_changed_mask = (u8)(
                g_vm_net_mock_team_battle_group_hp_changed_mask | (u8)(1u << member));
        }
        if (g_vm_net_mock_team_battle_actor_slot_current <
            g_vm_net_mock_team_battle_member_count_current)
        {
            member = g_vm_net_mock_team_battle_actor_slot_current;
            g_mockBattleRoleHpCurrent = g_vm_net_mock_team_battle_member_hp_current[member];
            g_mockBattleRoleHpMax = g_vm_net_mock_team_battle_member_hp_max_current[member];
        }
        return 1;
    }

    /* In a solo battle the caster is the only legal friendly target.  The
     * previous branch treated that as "no selected ally" and emitted a zero
     * delta, so 清风拂体 spent MP and played its effect without recovering HP.
     * Keep the target on the caster, but apply the same max-HP-clamped healing
     * formula as the party path.  A revive remains a party-only action: a
     * defeated solo actor cannot issue this operation. */
    targetWireSlots[0] = playerWireSlot;
    if (!revive)
    {
        healValues[0] = vm_net_mock_battle_player_skill_heal_to_role(
            operate, g_mockBattleRoleHpCurrent, g_mockBattleRoleHpMax);
        g_mockBattleRoleHpCurrent += healValues[0];
    }
    else
    {
        healValues[0] = 0;
    }
    return 1;
}

static bool vm_net_mock_battle_skill_has_timed_stat_modifier(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL && skill->durationRounds != 0 &&
           (skill->strengthChange != 0 || skill->agilityChange != 0 ||
            skill->wisdomChange != 0 || skill->attackChange != 0 ||
            skill->defenseChange != 0 || skill->critChange != 0 ||
            skill->hitChange != 0 || skill->dodgeChange != 0 ||
            skill->resistChange != 0);
}

static bool vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    return skill != NULL && skill->targetDirection == 2 && skill->hpChange == 0 &&
           vm_net_mock_battle_skill_has_timed_stat_modifier(skill);
}

static bool vm_net_mock_battle_operate_skill_targets_self_modifier(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    return skill != NULL && skill->targetDirection == 0 && skill->hpChange == 0 &&
           vm_net_mock_battle_skill_has_timed_stat_modifier(skill);
}

static bool vm_net_mock_battle_operate_skill_targets_enemy_control(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    /* `神堂静默` has no HP delta.  It must still play on the selected enemy,
     * but it must not inherit the normal-attack fallback merely because there
     * is no numerical damage term in the resource row. */
    return skill != NULL && (skill->targetDirection == 3 || skill->targetDirection == 4) &&
           skill->hpChange == 0 && skill->effectKind != 0;
}

/* skill.dsh's only percentage-dodge rows are 幻剑's 混沌鬼气 1..5.  Their
 * description explicitly includes `%`, unlike equipment and every other
 * additive dodge source.  Keep this mapping narrow and resource-backed;
 * `dodgeChange` otherwise remains a normal fixed combat rating. */
static bool vm_net_mock_battle_skill_uses_percent_dodge(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL && skill->skillId >= 111u && skill->skillId <= 115u &&
           skill->rawJob == 1u && skill->targetDirection == 0u &&
           skill->durationRounds == 5u && skill->agilityChange > 0 &&
           skill->dodgeChange > 0;
}

static void vm_net_mock_battle_modifier_set_from_skill(
    vm_net_mock_battle_stat_modifier *modifier,
    const vm_net_mock_skill_catalog_item *skill)
{
    if (modifier == NULL || skill == NULL)
        return;
    modifier->remainingRounds = skill->durationRounds;
    modifier->strength = skill->strengthChange;
    modifier->agility = skill->agilityChange;
    modifier->wisdom = skill->wisdomChange;
    modifier->attack = skill->attackChange;
    modifier->defense = skill->defenseChange;
    modifier->crit = skill->critChange;
    modifier->hit = skill->hitChange;
    modifier->dodge = vm_net_mock_battle_skill_uses_percent_dodge(skill)
                          ? 0
                          : skill->dodgeChange;
    modifier->dodgePercent = vm_net_mock_battle_skill_uses_percent_dodge(skill)
                                 ? skill->dodgeChange
                                 : 0;
    modifier->resist = skill->resistChange;
}

/* `神臂担山` is a target-direction=2 group effect.  Its type-1 children carry
 * zero HP/MP deltas, yet are still required for Battle.cbm to play the spell
 * for each ally.  The actual stat state belongs to the server battle model. */
static u8 vm_net_mock_battle_apply_player_friendly_group_modifier_targets(
    u32 operate, u8 playerWireSlot, u8 targetWireSlots[3], u32 values[3])
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    u8 targetCount = 0;

    if (targetWireSlots == NULL || values == NULL ||
        !vm_net_mock_battle_skill_has_timed_stat_modifier(skill))
    {
        return 0;
    }
    if (g_vm_net_mock_team_battle_party_count_current >= 2 &&
        g_vm_net_mock_team_battle_member_count_current >= 2)
    {
        u8 memberCount = g_vm_net_mock_team_battle_member_count_current;

        if (memberCount > 3)
            memberCount = 3;
        for (u8 member = 0; member < memberCount; ++member)
        {
            if (g_vm_net_mock_team_battle_member_hp_current[member] == 0)
                continue;
            targetWireSlots[targetCount] = vm_net_mock_team_battle_display_to_wire_slot(member);
            values[targetCount] = 0;
            ++targetCount;
            vm_net_mock_battle_modifier_set_from_skill(
                &g_vm_net_mock_team_battle_member_modifiers_current[member], skill);
            g_vm_net_mock_team_battle_group_modifier_changed_mask = (u8)(
                g_vm_net_mock_team_battle_group_modifier_changed_mask | (u8)(1u << member));
        }
        if (g_vm_net_mock_team_battle_actor_slot_current < memberCount)
        {
            g_vm_net_mock_battle_active_modifier_current =
                g_vm_net_mock_team_battle_member_modifiers_current[
                    g_vm_net_mock_team_battle_actor_slot_current];
        }
        return targetCount;
    }

    targetWireSlots[0] = playerWireSlot;
    values[0] = 0;
    vm_net_mock_battle_modifier_set_from_skill(&g_vm_net_mock_battle_solo_modifier, skill);
    g_vm_net_mock_battle_active_modifier_current = g_vm_net_mock_battle_solo_modifier;
    return 1;
}

/* skill.dsh target-direction 0 is not an implicit enemy target.  The client
 * still needs a type-1 child so that the self-cast effect plays, but the child
 * deliberately carries no HP/MP delta. */
static u8 vm_net_mock_battle_apply_player_self_modifier_target(
    u32 operate, u8 playerWireSlot, u8 targetWireSlots[3], u32 values[3])
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    if (targetWireSlots == NULL || values == NULL ||
        !vm_net_mock_battle_skill_has_timed_stat_modifier(skill))
    {
        return 0;
    }

    targetWireSlots[0] = playerWireSlot;
    values[0] = 0;
    if (g_vm_net_mock_team_battle_party_count_current >= 2 &&
        g_vm_net_mock_team_battle_actor_slot_current <
            g_vm_net_mock_team_battle_member_count_current)
    {
        u8 member = g_vm_net_mock_team_battle_actor_slot_current;

        vm_net_mock_battle_modifier_set_from_skill(
            &g_vm_net_mock_team_battle_member_modifiers_current[member], skill);
        g_vm_net_mock_team_battle_group_modifier_changed_mask = (u8)(
            g_vm_net_mock_team_battle_group_modifier_changed_mask | (u8)(1u << member));
        g_vm_net_mock_battle_active_modifier_current =
            g_vm_net_mock_team_battle_member_modifiers_current[member];
        return 1;
    }

    vm_net_mock_battle_modifier_set_from_skill(&g_vm_net_mock_battle_solo_modifier, skill);
    g_vm_net_mock_battle_active_modifier_current = g_vm_net_mock_battle_solo_modifier;
    return 1;
}

static u8 vm_net_mock_battle_collect_live_enemy_wires(bool playerOnRight,
                                                      u8 battleSide,
                                                      u8 fallbackEnemySlot,
                                                      u8 wireSlots[3]);
static void vm_net_mock_battle_modifier_advance_round(
    vm_net_mock_battle_stat_modifier *modifier);

static vm_net_mock_battle_enemy_effect *vm_net_mock_battle_enemy_effect_for_wire(
    u8 wireSlot, bool playerOnRight, u8 battleSide, u8 fallbackEnemySlot)
{
    u8 enemyIndex = 0;

    if (!vm_net_mock_battle_enemy_wire_to_index(wireSlot, playerOnRight, battleSide,
                                                fallbackEnemySlot, &enemyIndex) ||
        enemyIndex >= 3)
    {
        return NULL;
    }
    return &g_vm_net_mock_battle_enemy_effects_current[enemyIndex];
}

static void vm_net_mock_battle_select_enemy_modifier_for_wire(
    u8 wireSlot, bool playerOnRight, u8 battleSide, u8 fallbackEnemySlot)
{
    vm_net_mock_battle_enemy_effect *effect =
        vm_net_mock_battle_enemy_effect_for_wire(wireSlot, playerOnRight,
                                                  battleSide, fallbackEnemySlot);

    memset(&g_vm_net_mock_battle_active_enemy_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_enemy_modifier_current));
    if (effect != NULL)
        g_vm_net_mock_battle_active_enemy_modifier_current = effect->modifier;
}

static bool vm_net_mock_battle_operate_skill_targets_enemy_dot(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    return skill != NULL && (skill->targetDirection == 3 || skill->targetDirection == 4) &&
           skill->hpChange < 0 && skill->durationRounds != 0 &&
           !vm_net_mock_battle_skill_has_timed_stat_modifier(skill);
}

static bool vm_net_mock_battle_operate_skill_targets_enemy_modifier(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    return skill != NULL && (skill->targetDirection == 3 || skill->targetDirection == 4) &&
           vm_net_mock_battle_skill_has_timed_stat_modifier(skill);
}

static void vm_net_mock_battle_apply_enemy_effect_to_wire(
    const vm_net_mock_skill_catalog_item *skill, u8 wireSlot, bool playerOnRight,
    u8 battleSide, u8 fallbackEnemySlot, u32 dotDamage)
{
    vm_net_mock_battle_enemy_effect *effect = NULL;

    if (skill == NULL)
        return;
    effect = vm_net_mock_battle_enemy_effect_for_wire(
        wireSlot, playerOnRight, battleSide, fallbackEnemySlot);
    if (effect == NULL)
        return;
    if (skill->effectKind == 2)
    {
        memset(effect, 0, sizeof(*effect));
        return;
    }
    if (vm_net_mock_battle_skill_has_timed_stat_modifier(skill))
        vm_net_mock_battle_modifier_set_from_skill(&effect->modifier, skill);
    if (skill->effectKind == 1)
        effect->silenceRounds = skill->durationRounds;
    if (vm_net_mock_battle_operate_skill_targets_enemy_dot(skill->skillId + 2))
    {
        effect->dotRounds = skill->durationRounds;
        effect->dotDamage = dotDamage;
        effect->effectIndex = skill->effectIndex;
    }
    {
        u8 enemyIndex = 0;
        if (vm_net_mock_battle_enemy_wire_to_index(wireSlot, playerOnRight, battleSide,
                                                    fallbackEnemySlot, &enemyIndex) &&
            enemyIndex < 3)
        {
            g_vm_net_mock_battle_enemy_effect_changed_mask = (u8)(
                g_vm_net_mock_battle_enemy_effect_changed_mask | (u8)(1u << enemyIndex));
        }
    }
}

static void vm_net_mock_battle_advance_enemy_effects_current(void)
{
    for (u8 enemy = 0; enemy < 3; ++enemy)
    {
        vm_net_mock_battle_enemy_effect *effect =
            &g_vm_net_mock_battle_enemy_effects_current[enemy];

        if ((g_vm_net_mock_battle_enemy_effect_changed_mask & (u8)(1u << enemy)) != 0)
            continue;
        vm_net_mock_battle_modifier_advance_round(&effect->modifier);
        if (effect->silenceRounds != 0)
            --effect->silenceRounds;
    }
    g_vm_net_mock_battle_enemy_effect_changed_mask = 0;
}

/* A poison tick occurs at the end of a completed player/monster exchange.
 * It is emitted as its own type-1 action so the client receives the same HP
 * transition that the server applies; changing only the aggregate enemy HP
 * would desynchronise Battle.cbm's per-unit records. Freshly applied poison
 * is deliberately skipped until the next completed exchange. */
static u8 vm_net_mock_battle_apply_due_enemy_dot_damage(
    bool playerOnRight, u8 battleSide, u8 fallbackEnemySlot,
    u8 targetWireSlots[3], u32 damageValues[3], u8 deathWireSlots[3],
    u8 *deathCountInOut, u32 *effectIndexOut)
{
    u8 targetCount = 0;
    u8 deathCount = deathCountInOut ? *deathCountInOut : 0;

    if (targetWireSlots == NULL || damageValues == NULL || deathWireSlots == NULL)
        return 0;
    if (effectIndexOut != NULL)
        *effectIndexOut = 0;
    for (u8 enemy = 0; enemy < g_mockBattleEnemyCountCurrent && enemy < 3; ++enemy)
    {
        vm_net_mock_battle_enemy_effect *effect =
            &g_vm_net_mock_battle_enemy_effects_current[enemy];
        u8 wireSlot = 0;
        u32 hp = 0;
        u32 damage = 0;

        if (effect->dotRounds == 0 || effect->dotDamage == 0 ||
            (g_vm_net_mock_battle_enemy_effect_changed_mask & (u8)(1u << enemy)) != 0)
        {
            continue;
        }
        wireSlot = vm_net_mock_battle_enemy_wire_for_index(
            enemy, playerOnRight, battleSide, fallbackEnemySlot);
        hp = vm_net_mock_battle_enemy_hp_for_wire(
            wireSlot, playerOnRight, battleSide, fallbackEnemySlot);
        if (hp == 0)
            continue;
        damage = vm_net_mock_min_u32(effect->dotDamage, hp);
        targetWireSlots[targetCount] = wireSlot;
        damageValues[targetCount] = damage;
        ++targetCount;
        if (effectIndexOut != NULL && *effectIndexOut == 0)
            *effectIndexOut = effect->effectIndex;
        vm_net_mock_battle_damage_enemy_wire(
            wireSlot, playerOnRight, battleSide, fallbackEnemySlot, damage);
        if (vm_net_mock_battle_enemy_hp_for_wire(
                wireSlot, playerOnRight, battleSide, fallbackEnemySlot) == 0 &&
            deathCount < 3)
        {
            deathWireSlots[deathCount++] = wireSlot;
        }
        if (--effect->dotRounds == 0)
        {
            effect->dotDamage = 0;
            effect->effectIndex = 0;
        }
    }
    if (deathCountInOut != NULL)
        *deathCountInOut = deathCount;
    return targetCount;
}

static u8 vm_net_mock_battle_apply_player_enemy_control_targets(
    u32 operate, bool targetsEnemyGroup, u8 requestedTargetSlot, bool playerOnRight,
    u8 battleSide, u8 fallbackEnemySlot, u8 targetWireSlots[3], u32 values[3])
{
    u8 candidateSlots[3] = {0, 0, 0};
    u8 targetCount = 0;
    u8 candidateCount = 0;

    if (targetWireSlots == NULL || values == NULL ||
        !vm_net_mock_battle_operate_skill_targets_enemy_control(operate))
    {
        return 0;
    }
    if (targetsEnemyGroup)
    {
        candidateCount = vm_net_mock_battle_collect_live_enemy_wires(
            playerOnRight, battleSide, fallbackEnemySlot, candidateSlots);
    }
    if (candidateCount == 0)
        candidateSlots[candidateCount++] = requestedTargetSlot;
    for (u8 i = 0; i < candidateCount && i < 3; ++i)
    {
        targetWireSlots[targetCount] = candidateSlots[i];
        values[targetCount] = 0;
        ++targetCount;
    }
    return targetCount;
}

static u8 vm_net_mock_battle_apply_player_enemy_dot_targets(
    u32 operate, bool targetsEnemyGroup, u8 requestedTargetSlot, bool playerOnRight,
    u8 battleSide, u8 fallbackEnemySlot, u8 targetWireSlots[3], u32 values[3])
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    u8 candidateSlots[3] = {0, 0, 0};
    u8 candidateCount = 0;
    u8 targetCount = 0;

    if (targetWireSlots == NULL || values == NULL || skill == NULL ||
        !vm_net_mock_battle_operate_skill_targets_enemy_dot(operate))
    {
        return 0;
    }
    if (targetsEnemyGroup)
    {
        candidateCount = vm_net_mock_battle_collect_live_enemy_wires(
            playerOnRight, battleSide, fallbackEnemySlot, candidateSlots);
    }
    if (candidateCount == 0)
        candidateSlots[candidateCount++] = requestedTargetSlot;
    for (u8 i = 0; i < candidateCount && i < 3; ++i)
    {
        u32 hp = vm_net_mock_battle_enemy_hp_for_wire(candidateSlots[i], playerOnRight,
                                                       battleSide, fallbackEnemySlot);
        u32 damage = 0;

        vm_net_mock_battle_select_enemy_modifier_for_wire(
            candidateSlots[i], playerOnRight, battleSide, fallbackEnemySlot);
        damage = vm_net_mock_battle_player_skill_damage_to_enemy(
            operate, g_vm_net_mock_battle_enemy_id_current, hp,
            0xd07u ^ ((u32)candidateSlots[i] << 8), NULL, NULL);
        targetWireSlots[targetCount] = candidateSlots[i];
        values[targetCount] = 0;
        ++targetCount;
        vm_net_mock_battle_apply_enemy_effect_to_wire(
            skill, candidateSlots[i], playerOnRight, battleSide, fallbackEnemySlot, damage);
    }
    return targetCount;
}

static void vm_net_mock_battle_modifier_advance_round(
    vm_net_mock_battle_stat_modifier *modifier)
{
    if (modifier != NULL && modifier->remainingRounds != 0)
        --modifier->remainingRounds;
}

static u8 vm_net_mock_battle_collect_live_enemy_wires(bool playerOnRight,
                                                      u8 battleSide,
                                                      u8 fallbackEnemySlot,
                                                      u8 wireSlots[3])
{
    u8 enemyCount = vm_net_mock_battle_enemy_count_current();
    u8 wireCount = 0;

    if (wireSlots == NULL)
        return 0;
    for (u8 enemyIndex = 0; enemyIndex < enemyCount && enemyIndex < 3; ++enemyIndex)
    {
        if (g_mockBattleEnemyHpSlots[enemyIndex] == 0)
            continue;
        wireSlots[wireCount++] = vm_net_mock_battle_enemy_wire_for_index(
            enemyIndex, playerOnRight, battleSide, fallbackEnemySlot);
    }
    return wireCount;
}

/* Apply the authoritative hit to every target chosen by skill.dsh.  The
 * returned slots and deltas are used verbatim by the single actioninfo record
 * which Battle.cbm parses, keeping model state and playback in lock-step. */
static u8 vm_net_mock_battle_apply_player_attack_targets(
    u32 operate, bool operateIsSkill, bool targetsEnemyGroup,
    u8 requestedTargetSlot, bool playerOnRight, u8 battleSide, u8 fallbackEnemySlot,
    u8 targetWireSlots[3], u32 damageValues[3], u8 deathWireSlots[3],
    u8 *deathCountOut, u8 hitFlags[3], u8 criticalFlags[3])
{
    u8 targetCount = 0;
    u8 deathCount = 0;
    u8 candidateSlots[3] = {0, 0, 0};
    u8 candidateCount = 0;

    if (targetWireSlots == NULL || damageValues == NULL || deathWireSlots == NULL)
        return 0;
    if (targetsEnemyGroup && g_mockBattleEnemyHpCurrent > 0)
        candidateCount = vm_net_mock_battle_collect_live_enemy_wires(
            playerOnRight, battleSide, fallbackEnemySlot, candidateSlots);
    if (candidateCount == 0)
        candidateSlots[candidateCount++] = requestedTargetSlot;

    for (u8 i = 0; i < candidateCount && i < 3; ++i)
    {
        u8 targetWireSlot = candidateSlots[i];
        u32 targetEnemyHp = vm_net_mock_battle_enemy_hp_for_wire(
            targetWireSlot, playerOnRight, battleSide, fallbackEnemySlot);
        const vm_net_mock_skill_catalog_item *skill =
            operateIsSkill ? vm_net_mock_battle_operate_skill(operate) : NULL;
        bool hit = false;
        bool critical = false;
        vm_net_mock_battle_select_enemy_modifier_for_wire(
            targetWireSlot, playerOnRight, battleSide, fallbackEnemySlot);
        u32 damage = operateIsSkill
                         ? vm_net_mock_battle_player_skill_damage_to_enemy(
                               operate, g_vm_net_mock_battle_enemy_id_current,
                               targetEnemyHp,
                               ((u32)targetWireSlot << 8) ^ ((u32)i << 16) ^ operate,
                               &hit, &critical)
                         : vm_net_mock_battle_player_damage_to_enemy(
                               g_vm_net_mock_battle_enemy_id_current, targetEnemyHp,
                               ((u32)targetWireSlot << 8) ^ ((u32)i << 16),
                               &hit, &critical);

        targetWireSlots[targetCount] = targetWireSlot;
        damageValues[targetCount] = damage;
        if (hitFlags != NULL)
            hitFlags[targetCount] = hit ? 1 : 0;
        if (criticalFlags != NULL)
            criticalFlags[targetCount] = critical ? 1 : 0;
        ++targetCount;
        vm_net_mock_battle_damage_enemy_wire(targetWireSlot, playerOnRight, battleSide,
                                             fallbackEnemySlot, damage);
        if (skill != NULL && (vm_net_mock_battle_operate_skill_targets_enemy_modifier(operate) ||
                              vm_net_mock_battle_operate_skill_targets_enemy_dot(operate) ||
                              skill->effectKind == 2))
        {
            vm_net_mock_battle_apply_enemy_effect_to_wire(
                skill, targetWireSlot, playerOnRight, battleSide, fallbackEnemySlot, damage);
        }
        if (targetEnemyHp != 0 &&
            vm_net_mock_battle_enemy_hp_for_wire(targetWireSlot, playerOnRight,
                                                 battleSide, fallbackEnemySlot) == 0)
        {
            deathWireSlots[deathCount++] = targetWireSlot;
        }
    }
    if (deathCountOut != NULL)
        *deathCountOut = deathCount;
    return targetCount;
}

static u32 vm_net_mock_battle_item_effect_index(u32 hpEffect, u32 mpEffect)
{
    const char *override = getenv("CBE_BATTLE_ITEM_EFFECT_INDEX");
    u32 effectIndex = 0;

    if (override != NULL && override[0] != 0)
        return vm_net_mock_env_u32("CBE_BATTLE_ITEM_EFFECT_INDEX", 0);
    /* `f_renew1.actor` is the native recovery actor (`回复.gif`).  Both
     * 神仙壶 and 逍遥壶 restore a vital stat; letting MP-only recovery fall
     * through to effect 0 instead selected `f_blood1.actor`. */
    if ((hpEffect != 0 || mpEffect != 0) &&
        vm_net_mock_eidolon_heal_effect_index(&effectIndex))
        return effectIndex;
    return 0;
}

static void vm_net_mock_battle_sync_role_mp_from_role(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    g_mockBattleRoleMpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
    g_mockBattleRoleMpCurrent = vm_net_mock_min_u32(role->mp, g_mockBattleRoleMpMax);
}

static u32 vm_net_mock_battle_role_mp_current(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (g_mockBattleRoleMpMax == 0 && role != NULL)
        vm_net_mock_battle_sync_role_mp_from_role(role);
    return g_mockBattleRoleMpCurrent;
}

static void vm_net_mock_battle_set_role_mp_current(u32 mp)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role != NULL)
    {
        vm_net_mock_role_sync_derived_vitals(role);
        if (g_mockBattleRoleMpMax == 0)
            g_mockBattleRoleMpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
        if (g_mockBattleRoleMpMax == 0)
            g_mockBattleRoleMpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;
        g_mockBattleRoleMpCurrent = vm_net_mock_min_u32(mp, g_mockBattleRoleMpMax);
        role->mp = g_mockBattleRoleMpCurrent;
        return;
    }
    g_mockBattleRoleMpCurrent = mp;
}

static bool vm_net_mock_battle_prepare_skill_mp(u32 operate,
                                                u32 *mpBeforeOut,
                                                u32 *mpAfterOut,
                                                u32 *mpCostOut)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 mpBefore = 0;
    u32 mpCost = skill ? skill->mpCost : 0;
    u32 mpAfter = 0;

    if (mpBeforeOut)
        *mpBeforeOut = 0;
    if (mpAfterOut)
        *mpAfterOut = 0;
    if (mpCostOut)
        *mpCostOut = mpCost;
    if (role == NULL || !vm_net_mock_battle_operate_is_skill(operate))
        return false;

    if (g_mockBattleRoleMpMax == 0)
        vm_net_mock_battle_sync_role_mp_from_role(role);
    mpBefore = g_mockBattleRoleMpCurrent;
    mpAfter = mpBefore;
    if (mpBefore >= mpCost)
        mpAfter = mpBefore - mpCost;
    if (mpBeforeOut)
        *mpBeforeOut = mpBefore;
    if (mpAfterOut)
        *mpAfterOut = mpAfter;
    return mpBefore >= mpCost;
}

static void vm_net_mock_battle_commit_skill_mp(u32 mpAfter)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (role->mp != mpAfter)
    {
        vm_net_mock_battle_set_role_mp_current(mpAfter);
        vm_net_mock_role_db_save("battle-skill-use");
    }
    else
    {
        vm_net_mock_battle_set_role_mp_current(mpAfter);
    }
}

/* Battle action builders advance their own authoritative HP/MP counters before
 * the response is returned.  Publish those current values into the active role
 * immediately so the normal post-request presence capture can broadcast group
 * subtype 5/11 during the battle, instead of waiting for terminal settlement. */
static void vm_net_mock_battle_publish_role_vitals(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (g_mockBattleRoleHpMax != 0)
        role->hp = vm_net_mock_min_u32(g_mockBattleRoleHpCurrent, role->hpMax);
    if (g_mockBattleRoleMpMax != 0)
        role->mp = vm_net_mock_min_u32(g_mockBattleRoleMpCurrent, role->mpMax);
}

static bool vm_net_mock_append_battle_actioninfo_child(u8 *actionInfo, u32 actionInfoCap,
                                                       u32 *actionInfoLen,
                                                       u8 mappedTargetWireSlot,
                                                       u8 childFlag,
                                                       u32 valueA,
                                                       u32 valueBSeed)
{
    return vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen,
                                  mappedTargetWireSlot) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen,
                                  childFlag) &&
           vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen,
                                   valueA) &&
           vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen,
                                   valueBSeed);
}

/*
 * Battle.cbm HandleBattleActionMsg (0x6EB0) reads one action's child count
 * and then decodes that many target/value entries (accepting 1..6).  A group
 * skill is therefore one type-1 action with one child for every affected
 * monster, not a sequence of unrelated single-target skill animations.
 */
static bool vm_net_mock_append_battle_actioninfo_record_children(
    u8 *actionInfo, u32 actionInfoCap, u32 *actionInfoLen, u8 actionType,
    u8 mappedActorWireSlot, const u8 *targetWireSlots, const u8 *childFlags,
    const u32 *valueAs, const u32 *valueBs, u8 childCount, u32 effectIndex,
    u8 tail0, u8 tail1, u8 tail2)
{
    char valueText[16];
    const char *blobText = "";

    if (actionType != 3 && actionType != 4 &&
        (targetWireSlots == NULL || childFlags == NULL || valueAs == NULL ||
         valueBs == NULL || childCount == 0 || childCount > 6))
    {
        return false;
    }
    if (actionType == 1)
    {
        snprintf(valueText, sizeof(valueText), "%u",
                 vm_net_mock_battle_delta_display_value(valueAs[0]));
        blobText = valueText;
    }
    if (!vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, actionType) ||
        !vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen,
                                mappedActorWireSlot))
    {
        return false;
    }
    if (actionType == 3 || actionType == 4)
        return true;
    if (!vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, childCount))
        return false;
    for (u8 i = 0; i < childCount; ++i)
    {
        if (!vm_net_mock_append_battle_actioninfo_child(actionInfo, actionInfoCap,
                                                        actionInfoLen,
                                                        targetWireSlots[i],
                                                        childFlags[i], valueAs[i],
                                                        valueBs[i]))
        {
            return false;
        }
    }
    if (actionType != 1 && actionType != 2)
        return true;
    return vm_net_mock_seq_put_u32(actionInfo, actionInfoCap, actionInfoLen, effectIndex) &&
           vm_net_mock_seq_put_string(actionInfo, actionInfoCap, actionInfoLen, blobText) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail0) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail1) &&
           vm_net_mock_seq_put_u8(actionInfo, actionInfoCap, actionInfoLen, tail2);
}

static bool vm_net_mock_append_battle_actioninfo_record_ex(u8 *actionInfo, u32 actionInfoCap,
                                                           u32 *actionInfoLen, u8 actionType,
                                                           u8 mappedActorWireSlot,
                                                           u8 mappedTargetWireSlot,
                                                           u8 childFlag, u32 valueA,
                                                           u32 valueBSeed,
                                                           bool includeSecondChild,
                                                           u8 secondTargetWireSlot,
                                                           u8 secondChildFlag,
                                                           u32 secondValueA,
                                                           u32 secondValueB,
                                                           u32 effectIndex,
                                                           u8 tail0, u8 tail1, u8 tail2)
{
    u8 targetWireSlots[2] = {mappedTargetWireSlot, secondTargetWireSlot};
    u8 childFlags[2] = {childFlag, secondChildFlag};
    u32 valueAs[2] = {valueA, secondValueA};
    u32 valueBs[2] = {valueBSeed, secondValueB};
    u8 childCount = includeSecondChild ? 2 : 1;

    return vm_net_mock_append_battle_actioninfo_record_children(
        actionInfo, actionInfoCap, actionInfoLen, actionType, mappedActorWireSlot,
        targetWireSlots, childFlags, valueAs, valueBs, childCount, effectIndex,
        tail0, tail1, tail2);
}

static bool vm_net_mock_append_battle_actioninfo_record(u8 *actionInfo, u32 actionInfoCap,
                                                        u32 *actionInfoLen, u8 actionType,
                                                        u8 mappedActorWireSlot,
                                                        u8 mappedTargetWireSlot,
                                                        u8 childFlag, u32 valueA,
                                                        u32 valueBSeed, u32 effectIndex,
                                                        u8 tail0, u8 tail1, u8 tail2)
{
    return vm_net_mock_append_battle_actioninfo_record_ex(actionInfo, actionInfoCap,
                                                         actionInfoLen, actionType,
                                                         mappedActorWireSlot,
                                                         mappedTargetWireSlot,
                                                         childFlag, valueA,
                                                         valueBSeed,
                                                         false, 0, 0, 0, 0,
                                                         effectIndex,
                                                         tail0, tail1, tail2);
}

static bool vm_net_mock_build_battle_teaminfo_blob(u8 *out, u32 outCap,
                                                   u32 *teamInfoLenOut,
                                                   u32 roleId, u32 roleHp,
                                                   u32 roleMp)
{
    u32 pos = 0;

    if (teamInfoLenOut)
        *teamInfoLenOut = 0;
    if (out == NULL || roleId == 0)
        return false;

    /*
     * mmBattle InitActionSlot_B(0x6DBC) calls the tagged-i32 reader three
     * times, but rewinds the stream cursor by two bytes after the first two
     * calls.  The resulting row is an overlapped tagged-i32 sequence:
     *
     *   00 04, id32, hp32, mp32
     *
     * Call starts are row+0, row+4, and row+8, so the returned values are id,
     * hp (ignored by current client code), and mp.  Sending three normal tagged
     * u32 values makes the third read return hp_low16 + next tag header, which
     * was observed as 0x210004 and crashes the battle renderer.
     */
    if (!vm_net_mock_put_u8(out, outCap, &pos, 0))
        return false;
    if (!vm_net_mock_put_u8(out, outCap, &pos, 4))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, &pos, roleId))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, &pos, roleHp))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, &pos, roleMp))
        return false;

    if (teamInfoLenOut)
        *teamInfoLenOut = pos;
    return true;
}

static bool vm_net_mock_put_battle_action_companion_fields(u8 *out, u32 outCap, u32 *pos,
                                                           bool includeTeamInfo,
                                                           u32 teamRoleId,
                                                           u32 teamRoleHp,
                                                           u32 teamRoleMp)
{
    u8 teamInfo[64];
    u32 teamInfoLen = 0;

    if (!includeTeamInfo)
        return true;

    memset(teamInfo, 0, sizeof(teamInfo));
    if (!vm_net_mock_build_battle_teaminfo_blob(teamInfo, sizeof(teamInfo),
                                                &teamInfoLen, teamRoleId,
                                                teamRoleHp, teamRoleMp))
        return false;
    if (teamInfoLen == 0 || teamInfoLen > 0xffff)
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "teaminfo",
                                    teamInfo, (u16)teamInfoLen))
        return false;
    return true;
}

static bool vm_net_mock_append_battle_case11_auto_flag_object(u8 *out, u32 outCap,
                                                              u32 *pos, u8 type)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 11, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", type))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_action6_object_ex(u8 *out, u32 outCap, u32 *pos,
                                                        const u8 *actionInfo,
                                                        u32 actionInfoLen,
                                                        u8 actionCount,
                                                        bool includeTeamInfo,
                                                        u32 teamRoleId,
                                                        u32 teamRoleHp,
                                                        u32 teamRoleMp)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 6, &objectStart))
        return false;
    if (!vm_net_mock_put_battle_action_companion_fields(out, outCap, pos,
                                                       includeTeamInfo,
                                                       teamRoleId,
                                                       teamRoleHp,
                                                       teamRoleMp))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "actionnum", actionCount))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "actioninfo",
                                    actionInfo, (u16)actionInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    /* HandleBattleActionMsg(0x6EB0) enqueues each actioninfo record
     * separately.  The automatic scheduler reads this immediately after its
     * internal 4/2 has been built, so its next delivery cannot overwrite an
     * in-flight multi-record client action queue. */
    g_vm_net_mock_battle_action6_emitted_count = actionCount;
    return true;
}

/* `WT 4/12` contains no new choice: auto battle replays the remembered
 * `Operate` through the ordinary 4/2 builder.  MP eligibility belongs here,
 * before target/effect/actioninfo selection.  Otherwise an unaffordable
 * spell is still encoded as a spell and only its MP value is clamped to zero.
 * Falling back to Operate=0 preserves the native physical-attack action
 * contract without inventing a client error or altering the remembered skill.
 */
static bool vm_net_mock_battle_fallback_unaffordable_skill(u32 *operate,
                                                            const char *source)
{
    const vm_net_mock_skill_catalog_item *skill = NULL;
    vm_net_mock_role_state *role = NULL;
    u32 requested = 0;
    u32 mpBefore = 0;
    u32 mpAfter = 0;
    u32 mpCost = 0;
    const char *reason = "skill-unresolved";

    if (operate == NULL || !vm_net_mock_battle_operate_is_skill(*operate))
        return false;
    requested = *operate;
    skill = vm_net_mock_battle_operate_skill(requested);
    role = vm_net_mock_active_role();
    /* Only the verified "known skill, current MP below cost" contract may
     * turn into a physical attack.  Keep malformed/unknown requests on their
     * ordinary path so this guard cannot mask a different protocol problem. */
    if (skill == NULL || role == NULL)
        return false;
    if (vm_net_mock_battle_prepare_skill_mp(requested, &mpBefore, &mpAfter,
                                            &mpCost))
    {
        return false;
    }
    if (mpBefore >= mpCost)
        return false;
    reason = "mp-insufficient";

    *operate = 0;
    printf("[info][network] mock_battle_skill_fallback role=%u operate=%u "
           "fallback=0 mp=%u cost=%u reason=%s source=%s "
           "evidence=skill.dsh:耗费法力+WT4/2\n",
           role ? role->roleId : 0, requested, mpBefore, mpCost, reason,
           source ? source : "-");
    vm_autotest_note("mock_battle_skill_fallback role=%u operate=%u "
                     "fallback=0 mp=%u cost=%u reason=%s source=%s "
                     "evidence=skill.dsh:耗费法力+WT4/2\n",
                     role ? role->roleId : 0, requested, mpBefore, mpCost,
                     reason, source ? source : "-");
    return true;
}

static bool vm_net_mock_build_battle_teaminfo_rows_blob(
    u8 *out,
    u32 outCap,
    u32 *teamInfoLenOut,
    const u32 *roleIds,
    const u32 *roleHps,
    const u32 *roleMps,
    u8 rowCount)
{
    u32 pos = 0;

    if (teamInfoLenOut)
        *teamInfoLenOut = 0;
    if (out == NULL || roleIds == NULL || roleHps == NULL || roleMps == NULL ||
        rowCount == 0 || rowCount > VM_MOCK_SERVICE_TEAM_MEMBER_MAX)
    {
        return false;
    }
    for (u8 row = 0; row < rowCount; ++row)
    {
        u32 rowLen = 0;

        if (roleIds[row] == 0 ||
            !vm_net_mock_build_battle_teaminfo_blob(
                out + pos, outCap - pos, &rowLen,
                roleIds[row], roleHps[row], roleMps[row]) ||
            rowLen == 0 || pos + rowLen > outCap)
        {
            return false;
        }
        pos += rowLen;
    }
    if (teamInfoLenOut)
        *teamInfoLenOut = pos;
    return true;
}

static bool vm_net_mock_append_battle_action6_object(u8 *out, u32 outCap, u32 *pos,
                                                     const u8 *actionInfo,
                                                     u32 actionInfoLen,
                                                     u8 actionCount)
{
    return vm_net_mock_append_battle_action6_object_ex(out, outCap, pos,
                                                      actionInfo, actionInfoLen,
                                                      actionCount,
                                                      false, 0, 0, 0);
}

static bool vm_net_mock_append_battle_action6_object_with_teaminfo_blob(
    u8 *out,
    u32 outCap,
    u32 *pos,
    const u8 *actionInfo,
    u32 actionInfoLen,
    u8 actionCount,
    const u8 *teamInfo,
    u32 teamInfoLen)
{
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || actionInfo == NULL || actionInfoLen == 0 ||
        teamInfo == NULL || teamInfoLen == 0 || teamInfoLen > 0xffff ||
        !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 6, &objectStart) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "teaminfo",
                                    teamInfo, (u16)teamInfoLen) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "actionnum", actionCount) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "actioninfo",
                                    actionInfo, (u16)actionInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    g_vm_net_mock_battle_action6_emitted_count = actionCount;
    return true;
}

static u32 vm_net_mock_build_battle_single_action_response_ex(u8 *out, u32 outCap,
                                                              u8 actionType, u8 actorWireSlot,
                                                              u8 targetWireSlot, u8 childFlag,
                                                              u32 valueA, u32 valueB,
                                                              bool includeAutoFlag, u8 autoFlagType)
{
    u32 pos = 5;
    u8 actionInfo[128];
    u32 actionInfoLen = 0;
    u32 objectCount = 0;
    u32 effectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos)
        return 0;
    memset(actionInfo, 0, sizeof(actionInfo));
    if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                     &actionInfoLen, actionType,
                                                     actorWireSlot, targetWireSlot,
                                                     childFlag, valueA, valueB,
                                                     (actionType == 1 || actionType == 2) ? effectIndex : 0,
                                                     (actionType == 1 || actionType == 2) ? tail0 : 0,
                                                     (actionType == 1 || actionType == 2) ? tail1 : 0,
                                                     (actionType == 1 || actionType == 2) ? tail2 : 0))
        return 0;
    if (includeAutoFlag)
    {
        if (!vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, autoFlagType))
            return 0;
        ++objectCount;
    }
    if (!vm_net_mock_append_battle_action6_object(out, outCap, &pos,
                                                 actionInfo, actionInfoLen, 1))
        return 0;
    ++objectCount;
    vm_net_mock_finish_wt_packet(out, pos, (u8)objectCount);
    return pos;
}

static u32 vm_net_mock_build_battle_single_action_response(u8 *out, u32 outCap,
                                                           u8 actionType, u8 actorWireSlot,
                                                           u8 targetWireSlot, u8 childFlag,
                                                           u32 valueA, u32 valueB)
{
    return vm_net_mock_build_battle_single_action_response_ex(out, outCap,
                                                              actionType,
                                                              actorWireSlot,
                                                              targetWireSlot,
                                                              childFlag,
                                                              valueA,
                                                              valueB,
                                                              false,
                                                              0);
}

static u32 vm_net_mock_build_battle_enemy_turn_response(u8 *out, u32 outCap,
                                                        u8 actionType, u8 actorWireSlot,
                                                        u8 targetWireSlot, u8 childFlag,
                                                        u32 valueA, u32 valueB,
                                                        u8 playerSlot)
{
    u32 pos = 5;
    u8 actionInfo[128];
    u32 actionInfoLen = 0;
    u8 actionCount = 1;
    u8 objectCount = 0;
    u32 effectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos)
        return 0;
    memset(actionInfo, 0, sizeof(actionInfo));
    if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                     &actionInfoLen, actionType,
                                                     actorWireSlot, targetWireSlot,
                                                     childFlag, valueA, valueB,
                                                     (actionType == 1 || actionType == 2) ? effectIndex : 0,
                                                     (actionType == 1 || actionType == 2) ? tail0 : 0,
                                                     (actionType == 1 || actionType == 2) ? tail1 : 0,
                                                     (actionType == 1 || actionType == 2) ? tail2 : 0))
        return 0;
    if (g_mockBattleRoleHpCurrent == 0)
    {
        u8 deathActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_DEATH_ACTION_TYPE", 3);

        if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                         &actionInfoLen, deathActionType,
                                                         playerSlot, 0, 0,
                                                         0, 0, 0, 0, 0, 0))
            return 0;
        ++actionCount;
    }
    if (!vm_net_mock_append_battle_action6_object(out, outCap, &pos,
                                                 actionInfo, actionInfoLen,
                                                 actionCount))
        return 0;
    ++objectCount;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_battle_case11_auto_off_response(u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (outCap < pos ||
        !vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, 0))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_battle_item_use_response(const u8 *request, u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    vm_net_mock_battle_item_use_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    const vm_net_mock_item_effect_catalog_item *effect = NULL;
    u32 itemId = 0;
    u16 itemSeq = 0;
    u32 remaining = 0;
    u32 hpEffect = 0;
    u32 mpEffect = 0;
    u32 expEffect = 0;
    u32 hpApplied = 0;
    u32 mpApplied = 0;
    u32 expApplied = 0;
    u32 hpPlanned = 0;
    u32 mpPlanned = 0;
    u32 reservoirBefore = 0;
    u32 reservoirConsumed = 0;
    u32 counterDamageValue = 0;
    u32 counterHpDelta = 0;
    bool consumed = false;
    bool applied = false;
    bool reservoirItem = false;
    bool includeCounterattack = false;
    bool bundleWholeRound = false;
    bool teamBattleOperation = false;
    bool battleEndsThisRound = false;
    bool deathActionNeeded = false;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 playerSlot = 0;
    u8 enemySlot = 0;
    u8 itemActionType = 2;
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", 0);
    u8 counterChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    u32 itemEffectIndex = 0;
    u8 itemTail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TAIL0", 0);
    u8 itemTail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TAIL1", 0);
    u8 itemTail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TAIL2", 0);
    u8 actionInfo[192];
    u32 actionInfoLen = 0;
    u8 actionCount = 0;
    u8 countInfo[32];
    u32 countInfoLen = 0;
    u8 counterWireSlots[3] = {0, 0, 0};
    u32 counterDamageValues[3] = {0, 0, 0};
    u8 counterChildFlags[3] = {0, 0, 0};
    u8 counterWireCount = 0;
    u8 deathActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_DEATH_ACTION_TYPE", 3);
    bool itemTeamInfoEnabled = false;
    u32 itemTeamRoleId = 0;
    u32 itemTeamHp = 0;
    u32 itemTeamMp = 0;
    bool includeBackpackSync = false;
    bool responseIsNoop = false;
    bool terminalStatusAppended = false;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_battle_item_use_request(request, requestLen, &parsed))
        return 0;

    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT", defaultPlayerSlot);
    enemySlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT", defaultEnemySlot);
    bundleWholeRound = g_mockBattleOperateSessionArmed != 0 &&
                       (g_vm_net_mock_team_battle_party_count_current >= 2
                            ? g_vm_net_mock_team_battle_resolve_monsters_current != 0
                            : vm_net_mock_env_u32("CBE_BATTLE_BUNDLE_ROUND", 1) != 0);
    teamBattleOperation = g_vm_net_mock_team_battle_party_count_current >= 2;

    if (g_mockBattleAwaitingSettlement != 0)
        return vm_net_mock_build_battle_pending_settlement_response(out, outCap);

    role = vm_net_mock_active_role();
    if (role != NULL)
    {
        vm_net_mock_role_sync_derived_vitals(role);
        if (g_mockBattleRoleHpCurrent == 0)
            g_mockBattleRoleHpCurrent = role->hp;
        if (g_mockBattleRoleHpMax == 0)
            g_mockBattleRoleHpMax = role->hpMax ? role->hpMax : VM_NET_MOCK_ROLE_DEFAULT_HP;
        if (g_mockBattleRoleHpMax < g_mockBattleRoleHpCurrent)
            g_mockBattleRoleHpMax = g_mockBattleRoleHpCurrent;
        item = vm_net_mock_role_find_backpack_item(role, 0, parsed.seq);
    }
    if (item != NULL)
    {
        itemId = item->itemId;
        itemSeq = item->seq;
        effect = vm_net_mock_find_item_effect_catalog_item(itemId);
        if (vm_net_mock_item_effect_is_usable(effect))
        {
            hpEffect = effect->hp;
            mpEffect = effect->mp;
            expEffect = effect->exp;
            reservoirItem = vm_net_mock_item_effect_is_reservoir(effect);
            if (reservoirItem)
            {
                u32 mpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
                u32 mpCurrent = vm_net_mock_battle_role_mp_current();
                u32 missingHp = g_mockBattleRoleHpMax > g_mockBattleRoleHpCurrent
                                    ? g_mockBattleRoleHpMax - g_mockBattleRoleHpCurrent
                                    : 0;
                u32 missingMp = mpMax > mpCurrent ? mpMax - mpCurrent : 0;

                reservoirBefore = item->count;
                reservoirConsumed = vm_net_mock_item_effect_plan_reservoir_restore(
                    effect, reservoirBefore, missingHp, missingMp,
                    &hpPlanned, &mpPlanned);
                remaining = reservoirBefore;
                if (reservoirConsumed != 0)
                    consumed = vm_net_mock_role_consume_backpack_item(
                        role, itemId, parsed.seq, reservoirConsumed, &remaining);
                else
                    consumed = true;
            }
            else
            {
                consumed = vm_net_mock_role_consume_backpack_item(
                    role, itemId, parsed.seq, 1, &remaining);
            }
        }
    }

    if (role != NULL && consumed)
    {
        u32 mpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
        u32 beforeHp = 0;
        u32 beforeMp = vm_net_mock_battle_role_mp_current();
        u32 addHp = reservoirItem ? hpPlanned : vm_net_mock_mul_capped_u32(hpEffect, 1);
        u32 addMp = reservoirItem ? mpPlanned : vm_net_mock_mul_capped_u32(mpEffect, 1);
        u32 addExp = reservoirItem ? 0 : vm_net_mock_mul_capped_u32(expEffect, 1);

        beforeHp = g_mockBattleRoleHpCurrent;

        if (addHp != 0)
        {
            g_mockBattleRoleHpCurrent =
                vm_net_mock_min_u32(vm_net_mock_add_capped_u32(g_mockBattleRoleHpCurrent, addHp),
                                    g_mockBattleRoleHpMax);
            hpApplied = g_mockBattleRoleHpCurrent >= beforeHp
                            ? g_mockBattleRoleHpCurrent - beforeHp
                            : 0;
        }
        if (addMp != 0)
        {
            u32 afterMp = vm_net_mock_min_u32(vm_net_mock_add_capped_u32(beforeMp, addMp), mpMax);
            vm_net_mock_battle_set_role_mp_current(afterMp);
            mpApplied = afterMp >= beforeMp ? afterMp - beforeMp : 0;
        }
        if (addExp != 0)
        {
            bool leveledUp = vm_net_mock_role_add_exp(role, addExp);
            if (role->hpMax > g_mockBattleRoleHpMax)
                g_mockBattleRoleHpMax = role->hpMax;
            if (leveledUp)
            {
                g_mockBattleRoleHpCurrent = role->hp;
                g_mockBattleRoleMpMax = role->mpMax;
                g_mockBattleRoleMpCurrent = role->mp;
            }
            if (g_mockBattleRoleHpCurrent > g_mockBattleRoleHpMax)
                g_mockBattleRoleHpCurrent = g_mockBattleRoleHpMax;
            expApplied = addExp;
        }

        role->hp = vm_net_mock_min_u32(g_mockBattleRoleHpCurrent,
                                       role->hpMax ? role->hpMax : g_mockBattleRoleHpMax);
        applied = hpApplied != 0 || mpApplied != 0 || expApplied != 0;
    }

    memset(actionInfo, 0, sizeof(actionInfo));
    if (consumed)
    {
        u8 itemActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_ACTOR_WIRE_SLOT",
                                                       playerSlot);
        u8 itemTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TARGET_WIRE_SLOT",
                                                        playerSlot);
        itemActionType = (hpEffect != 0 || hpApplied != 0)
                             ? (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_HEAL_ACTION_TYPE", 1)
                             : (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_ACTION_TYPE", 2);
        itemEffectIndex = vm_net_mock_battle_item_effect_index(hpEffect, mpEffect);
        if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                         &actionInfoLen, itemActionType,
                                                         itemActorWireSlot,
                                                         itemTargetWireSlot,
                                                         0, hpApplied, mpApplied,
                                                         itemEffectIndex,
                                                         itemTail0, itemTail1, itemTail2))
        {
            return 0;
        }
        actionCount = 1;
        includeBackpackSync = itemId != 0 && itemSeq != 0;
        if (itemActionType == 1 && role != NULL)
        {
            itemTeamInfoEnabled = true;
            itemTeamRoleId = g_vm_net_mock_battle_role_id_current != 0
                                 ? g_vm_net_mock_battle_role_id_current
                                 : role->roleId;
        }

        includeCounterattack = bundleWholeRound &&
                               vm_net_mock_env_u32("CBE_BATTLE_ITEM_USE_COUNTER", 1) != 0 &&
                               g_mockBattleEnemyHpCurrent > 0 &&
                               g_mockBattleRoleHpCurrent > 0;
        if (includeCounterattack)
        {
            u8 enemyCount = vm_net_mock_battle_enemy_count_current();

            for (u8 enemyIndex = 0; enemyIndex < enemyCount && enemyIndex < 3; ++enemyIndex)
            {
                if (g_mockBattleEnemyHpSlots[enemyIndex] != 0)
                {
                    counterWireSlots[counterWireCount++] =
                        vm_net_mock_battle_enemy_wire_for_index(enemyIndex, playerOnRight,
                                                                battleSide, enemySlot);
                }
            }
        }

        if (includeCounterattack && counterWireCount != 0)
        {
            u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
            u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
            u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
            u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

            for (u8 i = 0; i < counterWireCount && i < 3 && g_mockBattleRoleHpCurrent > 0; ++i)
            {
                u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                                 counterWireSlots[i]);
                u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                                  playerSlot);
                u32 oneCounterDamage = vm_net_mock_battle_apply_damage_to_role(
                    vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                            g_mockBattleRoleHpCurrent,
                                                            counterWireSlots[i]));

                if (counterActionType == 1)
                {
                    counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                                  counterActorWireSlot);
                    counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                                   counterTargetWireSlot);
                }
                counterDamageValues[i] = oneCounterDamage;
                counterChildFlags[i] = oneCounterDamage == 0
                                           ? VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE
                                           : counterChildFlag;
                /* A miss is still a monster action.  Battle.cbm builds its
                 * playback queue from actioninfo records, not from HP
                 * changes, so omitting this record makes the monster appear
                 * to lose its turn.  Keep the zero-delta record and only
                 * exclude it from the aggregate-damage accounting. */
                if (oneCounterDamage != 0)
                {
                    counterDamageValue = vm_net_mock_add_capped_u32(counterDamageValue,
                                                                    oneCounterDamage);
                }
                counterHpDelta = vm_net_mock_battle_negative_delta_u32(oneCounterDamage);
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, counterActionType,
                                                                 counterActorWireSlot,
                                                                 counterTargetWireSlot,
                                                                 counterChildFlags[i],
                                                                 counterHpDelta,
                                                                 vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0),
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1EffectIndex : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail0 : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail1 : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail2 : 0))
                {
                    return 0;
                }
            }
            if (role != NULL)
                role->hp = g_mockBattleRoleHpCurrent;
        }
        if (itemTeamInfoEnabled)
        {
            itemTeamHp = g_mockBattleRoleHpCurrent;
            itemTeamMp = vm_net_mock_battle_role_mp_current();
        }

        battleEndsThisRound = g_mockBattleRoleHpCurrent == 0;
        if (battleEndsThisRound)
        {
            deathActionNeeded = true;
            if (actionCount < 6)
                ++actionCount;
            else
                return 0;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, deathActionType,
                                                             playerSlot, 0, 0,
                                                             0, 0, 0, 0, 0, 0))
            {
                return 0;
            }
        }
    }
    else
    {
        /*
         * Battle.cbm HandleServerBattleCmd(0x7BD0) uses subtype 4/4 for escape
         * results, and action type 4 is not a neutral item-use acknowledgement.
         * When the client sends a stale zero-count row, keep the response as an
         * empty 4/6 action packet so the battle state machine stays in place.
         */
        actionCount = 0;
        responseIsNoop = true;
    }

    if (itemTeamInfoEnabled)
    {
        if (!vm_net_mock_append_battle_action6_object_ex(out, outCap, &pos,
                                                         actionInfo, actionInfoLen,
                                                         actionCount,
                                                         true,
                                                         itemTeamRoleId,
                                                         itemTeamHp,
                                                         itemTeamMp))
        {
            return 0;
        }
    }
    else if (!vm_net_mock_append_battle_action6_object(out, outCap, &pos,
                                                      actionInfo, actionInfoLen,
                                                      actionCount))
    {
        return 0;
    }
    ++objectCount;
    if (!teamBattleOperation && battleEndsThisRound &&
        vm_net_mock_battle_inline_settlement_enabled())
    {
        if (g_mockBattleEnemyHpCurrent == 0 && g_mockBattleRoleHpCurrent > 0)
        {
            if (!vm_net_mock_append_battle_terminal_status_objects(
                    out, outCap, &pos, &objectCount, false,
                    &terminalStatusAppended))
                return 0;
            if (terminalStatusAppended)
                g_vm_net_mock_battle_settlement_sent_serial =
                    g_mockBattleOperateSessionSerial;
            if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                                   &objectCount,
                                                                   "battle-item-use-inline",
                                                                   true))
                return 0;
        }
    }
    if (includeBackpackSync)
    {
        /*
         * The battle item branch can already mutate the active battle row on
         * the client side. Sending the scene item-use 7/7 type=2 path here
         * double-consumes visible stacks in battle. Keep only 7/11, which the
         * main kind-7 dispatcher uses as the row-count sync path.
         */
        if (!vm_net_mock_build_item_use_count_info_blob(countInfo, sizeof(countInfo),
                                                        itemSeq, remaining,
                                                        &countInfoLen))
        {
            return 0;
        }
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_raw(out, outCap, &pos, "info",
                                        countInfo, (u16)countInfoLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        ++objectCount;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    if (consumed && (!battleEndsThisRound || teamBattleOperation))
        vm_net_mock_role_db_save("battle-item-use");
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound && !teamBattleOperation)
    {
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = (g_mockBattleRoleHpCurrent == 0) ? 0 : 1;
        if (g_mockBattleRoleHpCurrent == 0)
        {
            vm_net_mock_battle_save_completed_current_role_state(
                "battle-item-use-death");
        }
        else if (!terminalStatusAppended)
        {
            /* A zero-reward terminal deliberately omitted 4/7, so the
             * normal status-side persistence did not run. */
            vm_net_mock_battle_save_terminal_role_state(
                "battle-item-use-no-result", false);
        }
    }
    else
    {
        g_mockBattlePendingEnemyTurn = 0;
    }

    printf("[info][network] mock_battle_item_use index=%u seq=%u item=%u itemSeq=%u mode=%u reserve=%u->%u reserve_used=%u hp=%u/%u mp=%u/%u exp=%u effect=%u action=%u consumed=%u applied=%u counters=%u counterdmg=%u death=%u armed=%u bundle=%u enemies=%u slots=%u/%u/%u sync=%u noop=%u resp=%u evidence=mmBattle:0x2B50->4/3,0x7BD0/0x6EB0->4/6,JianghuOL.CBE:0x1033544,item.dsh:consumeMode\n",
           parsed.index, parsed.seq, itemId, itemSeq,
           reservoirItem ? 2u : (effect ? effect->consumeMode : 0u),
           reservoirBefore, remaining, reservoirConsumed,
           hpApplied, hpEffect, mpApplied, mpEffect, expApplied,
           itemEffectIndex, itemActionType, consumed ? 1 : 0, applied ? 1 : 0,
           counterWireCount, counterDamageValue, deathActionNeeded ? 1 : 0,
           g_mockBattleOperateSessionArmed ? 1 : 0, bundleWholeRound ? 1 : 0,
           vm_net_mock_battle_enemy_count_current(),
           g_mockBattleEnemyHpSlots[0], g_mockBattleEnemyHpSlots[1], g_mockBattleEnemyHpSlots[2],
           includeBackpackSync ? 1 : 0, responseIsNoop ? 1 : 0, pos);
    vm_autotest_note("mock_battle_item_use index=%u seq=%u item=%u itemSeq=%u mode=%u reserve=%u->%u reserve_used=%u hp=%u/%u mp=%u/%u exp=%u effect=%u action=%u consumed=%u applied=%u counters=%u counterdmg=%u death=%u armed=%u bundle=%u enemies=%u slots=%u/%u/%u sync=%u noop=%u response=%s evidence=mmBattle:0x2B50,0x6EB0,JianghuOL.CBE:0x1033544,item.dsh:consumeMode\n",
                     parsed.index, parsed.seq, itemId, itemSeq,
                     reservoirItem ? 2u : (effect ? effect->consumeMode : 0u),
                     reservoirBefore, remaining, reservoirConsumed,
                     hpApplied, hpEffect, mpApplied, mpEffect, expApplied,
                     itemEffectIndex, itemActionType, consumed ? 1 : 0, applied ? 1 : 0,
                     counterWireCount, counterDamageValue, deathActionNeeded ? 1 : 0,
                     g_mockBattleOperateSessionArmed ? 1 : 0, bundleWholeRound ? 1 : 0,
                     vm_net_mock_battle_enemy_count_current(),
                     g_mockBattleEnemyHpSlots[0], g_mockBattleEnemyHpSlots[1], g_mockBattleEnemyHpSlots[2],
                     includeBackpackSync ? 1 : 0, responseIsNoop ? 1 : 0,
                     includeBackpackSync ? (itemActionType == 1
                                                ? "4/6+7/11-actionType1"
                                                : "4/6+7/11-actionType2")
                                         : (responseIsNoop ? "4/6-actionnum0"
                                                           : (itemActionType == 1
                                                                  ? "4/6-actionType1"
                                                                  : "4/6-actionType2")));
    return pos;
}

/* `BattleAutoAction_TimerTick(0x2952)` sends an empty 4/12 request after the
 * client-visible auto delay.  Its payload cannot express a new choice, so the
 * server is the authority for the last choice which *successfully* produced a
 * 4/6 action.  Keep this per account/role (captured by the active-account
 * state) rather than per battle session: scene hangup deliberately starts a
 * fresh battle while retaining the user's selected automatic skill. */
static void vm_net_mock_battle_auto_remember_operation(
    u32 roleId,
    u32 index,
    u32 operate,
    const char *source)
{
    if ((operate != 0 && !vm_net_mock_battle_operate_is_skill(operate)) ||
        roleId == 0)
    {
        return;
    }

    g_vm_net_mock_battle_auto_last_operation_valid = 1;
    g_vm_net_mock_battle_auto_last_operation_role_id = roleId;
    g_vm_net_mock_battle_auto_last_operation_index = index;
    g_vm_net_mock_battle_auto_last_operation_operate = operate;
    printf("[info][network] mock_battle_auto_remember role=%u index=%u "
           "operate=%u source=%s evidence=accepted-WT4/2\n",
           g_vm_net_mock_battle_auto_last_operation_role_id,
           index,
           operate,
           source ? source : "-");
    vm_autotest_note("mock_battle_auto_remember role=%u index=%u operate=%u "
                     "source=%s evidence=accepted-WT4/2\n",
                     g_vm_net_mock_battle_auto_last_operation_role_id,
                     index,
                     operate,
                     source ? source : "-");
}

static u32 vm_net_mock_build_battle_operate_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 index = 0;
    u32 operate = 0;
    u8 index8 = 0;
    u8 operate8 = 0;
    /* A final party member can contribute one three-target player action and
     * three monster counter actions in the same 4/6.  The captured-round
     * contract already permits up to VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX;
     * keeping this local staging buffer smaller made the second member's
     * otherwise valid WT 4/2 fall through as unhandled before the round could
     * be released. */
    u8 actionInfo[VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX];
    u32 actionInfoLen = 0;
    u32 responseObjectCount = 1;
    u8 actorSlot = 0;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 playerSlot = 0;
    u8 enemySlot = 0;
    u8 requestedTargetSlot = enemySlot;
    u8 firstRecordWireActorUsed = 0;
    u8 firstRecordWireTargetUsed = 0;
    u32 attackDamageValue = 12;
    u32 counterDamageValue = 0;
    u32 attackHpDelta = 0;
    u32 counterHpDelta = 0;
    u8 actionCount = 1;
    bool bundleWholeRound = false;
    bool teamBattleOperation = false;
    u8 firstRecordActorWireSlot = 0;
    u8 firstRecordChildFlag = 0;
    u8 counterRecordChildFlag = 0;
    u32 firstRecordMpDelta = 0;
    u32 counterRecordMpDelta = 0;
    bool battleEndsThisRound = false;
    bool allowCounterattack = false;
    bool deathActionNeeded = false;
    u8 deathActionWireSlot = 0;
    u8 deathActionCount = 0;
    bool terminalActionEnabled = vm_net_mock_battle_terminal_action_enabled();
    bool terminalFollowup = false;
    bool terminalStatusAppended = false;
    bool terminalCloseDeferred = false;
    bool operateIsSkill = false;
    bool operateConsumesTurn = false;
    bool skillMpPrepared = false;
    u32 skillMpCost = 0;
    u32 skillMpBefore = 0;
    u32 skillMpAfter = 0;
    u32 skillMpDelta = 0;
    bool skillTeamInfoEnabled = false;
    u32 skillTeamRoleId = 0;
    u32 skillTeamHp = 0;
    u32 skillTeamMp = 0;
    u32 skillCostValueA = 0;
    u32 skillCostValueB = 0;
    bool skillCostActionEnabled = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_ACTION_ENABLED", 0) != 0;
    u8 skillCostActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_ACTION_TYPE", 0);
    u32 skillCostEffectIndex = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_EFFECT_INDEX", 0);
    bool skillTargetsEnemyGroup = false;
    bool skillTargetsFriendlyGroupHeal = false;
    bool skillTargetsFriendlyGroupModifier = false;
    bool skillTargetsFriendlySingleHeal = false;
    bool skillTargetsFriendlySingleRevive = false;
    bool skillTargetsSelfModifier = false;
    bool skillTargetsEnemyControl = false;
    bool skillTargetsEnemyDot = false;
    bool skillTargetsFriendlyHp = false;
    bool skillTargetsTimedModifier = false;
    bool attackChecksHit = false;
    u8 attackWireSlots[3] = {0, 0, 0};
    u32 attackDamageValues[3] = {0, 0, 0};
    u8 attackHitFlags[3] = {0, 0, 0};
    u8 attackCriticalFlags[3] = {0, 0, 0};
    u8 attackChildFlags[3] = {0, 0, 0};
    u32 attackChildValueAs[3] = {0, 0, 0};
    u32 attackChildValueBs[3] = {0, 0, 0};
    u8 attackTargetCount = 0;
    u8 deathActionWireSlots[3] = {0, 0, 0};
    u8 deathActionTargetCount = 0;
    u8 counterWireSlots[3] = {0, 0, 0};
    u32 counterDamageValues[3] = {0, 0, 0};
    u8 counterChildFlags[3] = {0, 0, 0};
    u8 counterWireCount = 0;
    u8 dotWireSlots[3] = {0, 0, 0};
    u32 dotDamageValues[3] = {0, 0, 0};
    u8 dotTargetCount = 0;
    u32 dotEffectIndex = 0;
    u8 actionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_ACTION_TYPE", 0);
    u8 firstActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTION_TYPE", actionType);
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", actionType);
    u8 deathActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_DEATH_ACTION_TYPE", 3);
    u8 terminalActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTION_TYPE", 3);
    u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos || !vm_net_mock_is_battle_operate_request(request, requestLen))
        return 0;
    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT", defaultPlayerSlot);
    enemySlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT", defaultEnemySlot);

    if (!vm_net_mock_get_object_u32_field(request, requestLen, "index", &index) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "index", &index8))
        index = index8;
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "Operate", &operate) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "Operate", &operate8))
        operate = operate8;
    (void)vm_net_mock_battle_fallback_unaffordable_skill(&operate,
                                                          "battle-operate");
    operateIsSkill = vm_net_mock_battle_operate_is_skill(operate);
    operateConsumesTurn = operate == 0 || operateIsSkill;
    skillTargetsEnemyGroup = operateIsSkill &&
                             vm_net_mock_battle_operate_skill_targets_enemy_group(operate);
    skillTargetsFriendlyGroupHeal = operateIsSkill &&
                                    vm_net_mock_battle_operate_skill_targets_friendly_group_heal(operate);
    skillTargetsFriendlyGroupModifier = operateIsSkill &&
                                        vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(operate);
    skillTargetsFriendlySingleHeal = operateIsSkill &&
                                      vm_net_mock_battle_operate_skill_targets_friendly_single_heal(operate);
    skillTargetsFriendlySingleRevive = operateIsSkill &&
                                        vm_net_mock_battle_operate_skill_targets_friendly_single_revive(operate);
    skillTargetsSelfModifier = operateIsSkill &&
                              vm_net_mock_battle_operate_skill_targets_self_modifier(operate);
    skillTargetsEnemyControl = operateIsSkill &&
                                vm_net_mock_battle_operate_skill_targets_enemy_control(operate);
    skillTargetsEnemyDot = operateIsSkill &&
                           vm_net_mock_battle_operate_skill_targets_enemy_dot(operate);
    skillTargetsFriendlyHp = skillTargetsFriendlyGroupHeal ||
                             skillTargetsFriendlySingleHeal ||
                             skillTargetsFriendlySingleRevive;
    skillTargetsTimedModifier = skillTargetsFriendlyGroupModifier ||
                                skillTargetsSelfModifier;
    if (operateIsSkill)
    {
        firstActionType = 1;
        type1EffectIndex = vm_net_mock_battle_operate_skill_effect(operate);
        skillMpPrepared = vm_net_mock_battle_prepare_skill_mp(operate,
                                                              &skillMpBefore,
                                                              &skillMpAfter,
                                                              &skillMpCost);
    }

    /*
     * Static/runtime Battle.cbm evidence now converges on one stronger claim:
     * - subtype-6 field "actioninfo" is not consumed as a naked byte array.
     * - the header reads at 0x05188E58/0x05188E64/0x05188E7A use the reader
     *   callback at reader+0x28, and runtime cross-reference shows that slot
     *   resolves to 0x01033AAD -> stream_read_i8_tagged.
     * - similarly, the child/value reads later in sub_6EB0 use the reader
     *   callback table's tagged u32/u16/string helpers, not raw direct bytes.
     *
     * Therefore the next parser-faithful experiment is to keep subtype-6 and
     * the same two-record player/enemy round shell, but encode the inner stream
     * with the repository's existing tagged sequence helpers instead of the old
     * hand-written raw 43-byte layout.
     */
    actorSlot = (u8)(index & 0xFFu);
    requestedTargetSlot = vm_net_mock_battle_target_wire_slot_from_request(actorSlot,
                                                                            playerOnRight,
                                                                            battleSide,
                                                                            enemySlot);
    if (requestedTargetSlot == playerSlot || requestedTargetSlot > 5)
        requestedTargetSlot = enemySlot;
    if (g_mockBattleOperateSessionFinished != 0)
        g_mockBattleOperateSessionFinished = 0;
    terminalFollowup = false;
    bundleWholeRound = g_mockBattleOperateSessionArmed != 0 &&
                       (g_vm_net_mock_team_battle_party_count_current >= 2
                            ? g_vm_net_mock_team_battle_resolve_monsters_current != 0
                            : (operateConsumesTurn &&
                               vm_net_mock_env_u32("CBE_BATTLE_BUNDLE_ROUND", 1) != 0));
    teamBattleOperation = g_vm_net_mock_team_battle_party_count_current >= 2;
    firstRecordActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_ACTOR_WIRE_SLOT",
                                                       playerSlot);
    firstRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_CHILD_FLAG", 0);
    counterRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    if (g_mockBattleAwaitingSettlement != 0)
    {
        (void)requestedTargetSlot;
        if (g_vm_net_mock_battle_settlement_sent_serial != g_mockBattleOperateSessionSerial)
            return vm_net_mock_build_battle_pending_settlement_response(out, outCap);
        return vm_net_mock_build_battle_case11_auto_off_response(out, outCap);
    }
    if (!terminalFollowup && g_mockBattleEnemyHpCurrent == 0)
    {
        vm_net_mock_battle_reset_enemy_hp_from_stats(g_vm_net_mock_battle_enemy_id_current);
    }
    /* A zero battle HP is a terminal state, not a missing initial value.
     * The lethal 4/6 already carries the matching type-3 death action.  Do
     * not reload HP here when a later input reaches the same battle session. */
    if (!terminalFollowup && g_mockBattleEnemyHpCurrent > 0)
        requestedTargetSlot = vm_net_mock_battle_select_live_enemy_wire(requestedTargetSlot,
                                                                        playerOnRight,
                                                                        battleSide,
                                                                        enemySlot);
    if (!terminalFollowup && g_mockBattlePendingEnemyTurn != 0 &&
        g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0)
    {
        u8 counterEnemyWireSlot = vm_net_mock_battle_first_alive_enemy_wire(
            playerOnRight, battleSide, enemySlot);
        u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                         counterEnemyWireSlot);
        u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                          playerSlot);
        if (counterActionType == 1)
        {
            counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                          counterActorWireSlot);
            counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                           counterTargetWireSlot);
        }
        vm_net_mock_battle_select_enemy_modifier_for_wire(
            counterEnemyWireSlot, playerOnRight, battleSide, enemySlot);
        counterDamageValue = vm_net_mock_battle_apply_damage_to_role(
            vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                    g_mockBattleRoleHpCurrent,
                                                    counterEnemyWireSlot));
        if (counterDamageValue == 0)
            counterRecordChildFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE;
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
        counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
        g_mockBattlePendingEnemyTurn = 0;
        ++g_mockBattleOperateTurnCounter;
        {
            u32 pendingLen = vm_net_mock_build_battle_enemy_turn_response(out, outCap,
                                                                          counterActionType,
                                                                          counterActorWireSlot,
                                                                          counterTargetWireSlot,
                                                                          counterRecordChildFlag,
                                                                          counterHpDelta,
                                                                          counterRecordMpDelta,
                                                                          playerSlot);
            if (g_mockBattleRoleHpCurrent == 0)
            {
                g_mockBattleOperateSessionArmed = 0;
                g_mockBattleOperateSessionFinished = 0;
                g_mockBattlePendingEnemyTurn = 0;
                g_mockBattleAwaitingSettlement = 0;
                vm_net_mock_battle_save_completed_current_role_state(
                    "battle-pending-enemy-death");
            }
            printf("[info][network] mock_battle_pending_enemy_turn actor=%u target=%u damage=%u enemyhp=%u slots=%u/%u/%u rolehp=%u resp=%u evidence=mmBattle:0x6EB0\n",
                   counterActorWireSlot,
                   counterTargetWireSlot,
                   counterDamageValue,
                   g_mockBattleEnemyHpCurrent,
                   g_mockBattleEnemyHpSlots[0],
                   g_mockBattleEnemyHpSlots[1],
                   g_mockBattleEnemyHpSlots[2],
                   g_mockBattleRoleHpCurrent,
                   pendingLen);
            return pendingLen;
        }
    }
    if (terminalFollowup)
    {
        attackDamageValue = 0;
        counterDamageValue = 0;
        actionCount = 0;
    }
    else
    {
        if (skillTargetsFriendlyGroupHeal)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_friendly_group_heal_targets(
                operate, playerSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsFriendlyGroupModifier)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_friendly_group_modifier_targets(
                operate, playerSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsSelfModifier)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_self_modifier_target(
                operate, playerSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsFriendlySingleHeal || skillTargetsFriendlySingleRevive)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_friendly_single_heal_target(
                operate, actorSlot, playerSlot, skillTargetsFriendlySingleRevive,
                attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsEnemyControl)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_enemy_control_targets(
                operate, skillTargetsEnemyGroup, requestedTargetSlot, playerOnRight,
                battleSide, enemySlot, attackWireSlots, attackDamageValues);
            if (attackTargetCount != 0)
            {
                const vm_net_mock_skill_catalog_item *skill =
                    vm_net_mock_battle_operate_skill(operate);
                for (u8 i = 0; i < attackTargetCount; ++i)
                {
                    vm_net_mock_battle_apply_enemy_effect_to_wire(
                        skill, attackWireSlots[i], playerOnRight, battleSide, enemySlot, 0);
                }
            }
        }
        else if (skillTargetsEnemyDot)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_enemy_dot_targets(
                operate, skillTargetsEnemyGroup, requestedTargetSlot, playerOnRight,
                battleSide, enemySlot, attackWireSlots, attackDamageValues);
        }
        else
        {
            attackChecksHit = true;
            attackTargetCount = vm_net_mock_battle_apply_player_attack_targets(
                operate, operateIsSkill, skillTargetsEnemyGroup, requestedTargetSlot,
                playerOnRight, battleSide, enemySlot, attackWireSlots, attackDamageValues,
                deathActionWireSlots, &deathActionTargetCount,
                attackHitFlags, attackCriticalFlags);
        }
        if (attackTargetCount == 0)
            return 0;
        attackDamageValue = attackDamageValues[0];
        deathActionNeeded = deathActionTargetCount != 0;
        deathActionWireSlot = deathActionNeeded ? deathActionWireSlots[0] : 0;
        if (bundleWholeRound && g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0)
        {
            counterWireCount = vm_net_mock_battle_collect_live_enemy_wires(
                playerOnRight, battleSide, enemySlot, counterWireSlots);
        }
        allowCounterattack = bundleWholeRound && counterWireCount != 0 &&
                             g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0;
        if (allowCounterattack)
        {
            for (u8 i = 0; i < counterWireCount && i < 3 && g_mockBattleRoleHpCurrent > 0; ++i)
            {
                vm_net_mock_battle_select_enemy_modifier_for_wire(
                    counterWireSlots[i], playerOnRight, battleSide, enemySlot);
                u32 oneCounterDamage = vm_net_mock_battle_apply_damage_to_role(
                    vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                            g_mockBattleRoleHpCurrent,
                                                            counterWireSlots[i]));
                counterDamageValues[i] = oneCounterDamage;
                counterChildFlags[i] = oneCounterDamage == 0
                                           ? VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE
                                           : counterRecordChildFlag;
                /* Preserve a zero-damage counter as an actioninfo record:
                 * the client needs the record to animate and complete the
                 * monster turn. */
                if (oneCounterDamage != 0)
                {
                    counterDamageValue = vm_net_mock_add_capped_u32(counterDamageValue,
                                                                    oneCounterDamage);
                }
            }
        }
        if (bundleWholeRound && g_mockBattleEnemyHpCurrent > 0)
        {
            dotTargetCount = vm_net_mock_battle_apply_due_enemy_dot_damage(
                playerOnRight, battleSide, enemySlot, dotWireSlots, dotDamageValues,
                deathActionWireSlots, &deathActionTargetCount, &dotEffectIndex);
            if (deathActionTargetCount != 0)
            {
                deathActionNeeded = true;
                deathActionWireSlot = deathActionWireSlots[0];
            }
        }
        battleEndsThisRound = (g_mockBattleEnemyHpCurrent == 0 || g_mockBattleRoleHpCurrent == 0);
    }
    if (!terminalFollowup)
    {
        attackHpDelta = skillTargetsFriendlyHp ? attackDamageValue :
                        (skillTargetsTimedModifier || skillTargetsEnemyControl ||
                         skillTargetsEnemyDot ? 0 :
                         vm_net_mock_battle_negative_delta_u32(attackDamageValue));
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
    }
    /* HP deltas in actioninfo must be the exact values already applied to
     * the authoritative battle state.  Test overrides here made the client
     * animate HP below zero even though the server had clamped the hit. */
    skillMpDelta = vm_net_mock_env_u32("CBE_BATTLE_SKILL_MP_VALUE_B",
                                       skillMpPrepared ? skillMpAfter :
                                                         vm_net_mock_battle_role_mp_current());
    firstRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_B", 0);
    for (u8 i = 0; i < attackTargetCount && i < 3; ++i)
    {
        attackChildFlags[i] = firstRecordChildFlag;
        if (attackChecksHit)
        {
            attackChildFlags[i] = !attackHitFlags[i]
                                      ? VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE
                                      : (attackCriticalFlags[i]
                                             ? VM_NET_MOCK_BATTLE_CHILD_FLAG_CRITICAL
                                             : firstRecordChildFlag);
        }
        attackChildValueAs[i] = skillTargetsFriendlyHp ? attackDamageValues[i] :
                                (skillTargetsTimedModifier || skillTargetsEnemyControl ||
                                 skillTargetsEnemyDot ? 0 :
                                 vm_net_mock_battle_negative_delta_u32(attackDamageValues[i]));
        attackChildValueBs[i] = firstRecordMpDelta;
    }
    if (attackTargetCount != 0)
        attackChildValueAs[0] = attackHpDelta;
    skillCostValueA = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_VALUE_A",
                                          g_mockBattleRoleHpCurrent);
    skillCostValueB = skillMpDelta;
    counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
    if (operateIsSkill && skillMpPrepared)
        vm_net_mock_battle_commit_skill_mp(skillMpAfter);
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        bool terminalInlineSettlement = !teamBattleOperation &&
                                       battleEndsThisRound &&
                                       g_mockBattleEnemyHpCurrent == 0 &&
                                       g_mockBattleRoleHpCurrent > 0 &&
                                       vm_net_mock_battle_inline_settlement_enabled();

        if (terminalInlineSettlement)
        {
            /*
             * InitActionSlot_B(0x6DBC) installs teaminfo's final value in
             * the battle actor cache.  The later 4/7 only updates the main
             * actor by its HP/MP deltas.  Thus a terminal 4/6 must already
             * contain the same post-flask/post-recovery vitals that 4/7 will
             * settle, otherwise the stale battle cache wins when the result
             * panel returns to the scene.
             */
            skillTeamRoleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
            vm_net_mock_battle_project_terminal_vitals(
                g_mockBattleRoleHpCurrent, g_mockBattleRoleHpMax,
                vm_net_mock_battle_role_mp_current(), g_mockBattleRoleMpMax,
                &skillTeamHp, &skillTeamMp);
            skillTeamInfoEnabled = skillTeamRoleId != 0;
            printf("[info][network] mock_battle_terminal_action_vitals "
                   "role=%u hp=%u/%u mp=%u/%u source=4/6-teaminfo-before-4/7\n",
                   skillTeamRoleId,
                   skillTeamHp,
                   g_mockBattleRoleHpMax,
                   skillTeamMp,
                   g_mockBattleRoleMpMax);
        }
        else if (operateIsSkill && skillMpPrepared)
        {
            skillTeamRoleId = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_ROLE_ID",
                                                  role ? role->roleId :
                                                         VM_NET_MOCK_ROLE_DEFAULT_ID);
            skillTeamHp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_HP",
                                              g_mockBattleRoleHpCurrent);
            skillTeamMp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_MP",
                                              vm_net_mock_battle_role_mp_current());
            skillTeamInfoEnabled = skillTeamRoleId != 0;
        }
    }
    if (terminalFollowup)
    {
        u8 terminalObjectCount = 0;
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &terminalObjectCount, false,
                &terminalStatusAppended))
            return 0;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &terminalObjectCount,
                                                               "battle-operate-terminal",
                                                               true))
            return 0;
        /* A successful 4/7 owns the client result panel for every battle
         * flavor.  Its native input path emits 25/5 and cleans the battle
         * screen; appending 4/11(type=0)+4/9 here would clear that phase
         * before the client gets that chance.  Only an omitted 4/7 has no
         * such lifecycle and continues through the legacy close contract. */
        if (!terminalStatusAppended)
        {
            if (!vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
                !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            {
                return 0;
            }
            terminalObjectCount += 2;
        }
        vm_net_mock_finish_wt_packet(out, pos, terminalObjectCount);
        g_mockBattleOperateSessionFinished = 0;
        return pos;
    }
    memset(actionInfo, 0, sizeof(actionInfo));
    /*
     * The default wire slots come from the active battle-start flavor. For
     * subtype 5 scene-monster battles with side=1, runtime action playback
     * requires player actor wire 1 and monster target wire 0 for the common
     * one-monster case.
     */
    {
        u8 mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTOR_WIRE_SLOT",
                                                         firstRecordActorWireSlot);
        u8 mappedTargetWireSlot = attackTargetCount != 0 ? attackWireSlots[0] :
                                                           requestedTargetSlot;

        if (firstActionType == 1)
        {
            mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_ACTOR_WIRE_SLOT",
                                                         mappedActorWireSlot);
            if (!skillTargetsEnemyGroup && !skillTargetsFriendlyGroupHeal &&
                !skillTargetsFriendlyGroupModifier && !skillTargetsSelfModifier &&
                !skillTargetsFriendlySingleHeal && !skillTargetsFriendlySingleRevive &&
                !skillTargetsEnemyControl && !skillTargetsEnemyDot)
                mappedTargetWireSlot = (u8)vm_net_mock_env_u32(
                    "CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT", mappedTargetWireSlot);
        }
        else
        {
            mappedTargetWireSlot = (u8)vm_net_mock_env_u32(
                "CBE_BATTLE_FIRST_TARGET_WIRE_SLOT", mappedTargetWireSlot);
        }
        firstRecordWireActorUsed = mappedActorWireSlot;
        firstRecordWireTargetUsed = mappedTargetWireSlot;

        /* record 0 stays on the current live no-crash baseline. */
        if (!terminalFollowup)
        {
            if (attackTargetCount > 1 && firstActionType == 1)
            {
                if (!vm_net_mock_append_battle_actioninfo_record_children(
                        actionInfo, sizeof(actionInfo), &actionInfoLen, firstActionType,
                        mappedActorWireSlot, attackWireSlots, attackChildFlags,
                        attackChildValueAs, attackChildValueBs, attackTargetCount,
                        type1EffectIndex, type1Tail0, type1Tail1, type1Tail2))
                {
                    return 0;
                }
            }
            else if (!vm_net_mock_append_battle_actioninfo_record(
                         actionInfo, sizeof(actionInfo), &actionInfoLen, firstActionType,
                         mappedActorWireSlot, mappedTargetWireSlot, attackChildFlags[0],
                         attackHpDelta, firstRecordMpDelta,
                         (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail0 : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail1 : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail2 : 0))
            {
                return 0;
            }
            /*
             * Disabled by default. Runtime negatives showed that a separate
             * MP-cost action is still animated as a normal target update:
             * valueA=0 shows a 0 HP line and valueA=current HP shows a heal.
             * Keep this branch as an explicit experiment only.
             */
            if (skillCostActionEnabled &&
                operateIsSkill && skillMpPrepared && skillMpCost != 0 && firstActionType == 1)
            {
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, skillCostActionType,
                                                                 mappedActorWireSlot,
                                                                 mappedActorWireSlot,
                                                                 0, skillCostValueA,
                                                                 skillCostValueB,
                                                                 (skillCostActionType == 1 || skillCostActionType == 2) ? skillCostEffectIndex : 0,
                                                                 0, 0, 0))
                    return 0;
            }
            if (deathActionNeeded)
            {
                for (u8 i = 0; i < deathActionTargetCount && i < 3; ++i)
                {
                    if (actionCount < 6)
                        ++actionCount;
                    else
                        return 0;
                    if (!vm_net_mock_append_battle_actioninfo_record(
                            actionInfo, sizeof(actionInfo), &actionInfoLen, deathActionType,
                            deathActionWireSlots[i], 0, 0, 0, 0, 0, 0, 0, 0))
                    {
                        return 0;
                    }
                    ++deathActionCount;
                }
            }
        }

        /*
         * Plain player-vs-monster rounds stay in subtype 6. Do not arm subtype
         * 11 here: HandleServerBattleCmd(0x7BD0) treats 4/11 type=1 as the
         * auto-battle path and shows the auto battle UI.
         */
            if (allowCounterattack)
            {
            for (u8 i = 0; i < counterWireCount && i < 3; ++i)
            {
                u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                                 counterWireSlots[i]);
                u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                                  playerSlot);
                if (counterActionType == 1)
                {
                    counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                                  counterActorWireSlot);
                    counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                                   counterTargetWireSlot);
                }
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, counterActionType,
                                                                 counterActorWireSlot, counterTargetWireSlot,
                                                                 counterChildFlags[i],
                                                                 vm_net_mock_battle_negative_delta_u32(counterDamageValues[i]),
                                                                 counterRecordMpDelta,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1EffectIndex : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail0 : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail1 : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail2 : 0))
                    return 0;
                }
            }
            if (dotTargetCount != 0)
            {
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (dotTargetCount > 1)
                {
                    u8 dotFlags[3] = {0, 0, 0};
                    u32 dotValueAs[3] = {0, 0, 0};
                    u32 dotValueBs[3] = {0, 0, 0};
                    for (u8 i = 0; i < dotTargetCount; ++i)
                        dotValueAs[i] = vm_net_mock_battle_negative_delta_u32(dotDamageValues[i]);
                    if (!vm_net_mock_append_battle_actioninfo_record_children(
                            actionInfo, sizeof(actionInfo), &actionInfoLen, 1,
                            dotWireSlots[0], dotWireSlots, dotFlags, dotValueAs,
                            dotValueBs, dotTargetCount, dotEffectIndex, 0, 0, 0))
                    {
                        return 0;
                    }
                }
                else if (!vm_net_mock_append_battle_actioninfo_record(
                             actionInfo, sizeof(actionInfo), &actionInfoLen, 1,
                             dotWireSlots[0], dotWireSlots[0], 0,
                             vm_net_mock_battle_negative_delta_u32(dotDamageValues[0]), 0,
                             dotEffectIndex, 0, 0, 0))
                {
                    return 0;
                }
            }
            if (g_mockBattleRoleHpCurrent == 0)
        {
            if (actionCount < 6)
                ++actionCount;
            else
                return 0;
            deathActionWireSlot = playerSlot;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, deathActionType,
                                                             playerSlot, 0, 0,
                                                             0, 0, 0, 0, 0, 0))
                return 0;
            ++deathActionCount;
        }
        if (battleEndsThisRound &&
            g_mockBattleEnemyHpCurrent == 0 &&
            terminalActionEnabled &&
            !deathActionNeeded)
        {
            u8 terminalActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTOR_WIRE_SLOT",
                                                              (g_mockBattleEnemyHpCurrent == 0)
                                                                  ? requestedTargetSlot
                                                                  : playerSlot);
            if (actionCount < 6)
                ++actionCount;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, terminalActionType,
                                                             terminalActorWireSlot, 0, 0,
                                                             0, 0, 0, 0, 0, 0))
                return 0;
        }
    }

    if (!vm_net_mock_append_battle_action6_object_ex(out, outCap, &pos,
                                                    actionInfo, actionInfoLen,
                                                    actionCount,
                                                    skillTeamInfoEnabled,
                                                    skillTeamRoleId,
                                                    skillTeamHp,
                                                    skillTeamMp))
        return 0;
    if (!teamBattleOperation && battleEndsThisRound &&
        g_mockBattleEnemyHpCurrent == 0 &&
        g_mockBattleRoleHpCurrent > 0 &&
        vm_net_mock_battle_inline_settlement_enabled())
    {
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &responseObjectCount, false,
                &terminalStatusAppended))
            return 0;
        if (terminalStatusAppended)
            g_vm_net_mock_battle_settlement_sent_serial =
                g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &responseObjectCount,
                                                               "battle-operate-inline",
                                                               true))
            return 0;
    }
    terminalCloseDeferred = !teamBattleOperation && !terminalStatusAppended &&
        vm_net_mock_battle_terminal_close_should_defer(battleEndsThisRound);
    if (terminalCloseDeferred)
    {
        vm_net_mock_battle_schedule_terminal_close_after_actions(
            actionCount, "battle-operate-no-result-panel");
    }
    vm_net_mock_finish_wt_packet(out, pos, (u8)responseObjectCount);
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound && !teamBattleOperation)
    {
        if (g_mockBattleRoleHpCurrent == 0)
        {
            vm_net_mock_battle_save_completed_current_role_state(
                "battle-operate-death");
        }
        else if (!terminalStatusAppended)
            vm_net_mock_battle_save_terminal_role_state("battle-operate", false);
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = (g_mockBattleRoleHpCurrent == 0) ? 0 : 1;
    }
    else if (!battleEndsThisRound && g_mockBattleOperateSessionArmed != 0 &&
             operateConsumesTurn && !bundleWholeRound)
    {
        g_mockBattlePendingEnemyTurn = 1;
    }
    if (g_vm_net_mock_team_battle_party_count_current < 2 && operateConsumesTurn &&
        !skillTargetsTimedModifier)
    {
        vm_net_mock_battle_modifier_advance_round(&g_vm_net_mock_battle_solo_modifier);
        g_vm_net_mock_battle_active_modifier_current = g_vm_net_mock_battle_solo_modifier;
    }
    if (g_vm_net_mock_team_battle_party_count_current < 2 && operateConsumesTurn)
        vm_net_mock_battle_advance_enemy_effects_current();
    if (operateConsumesTurn && g_vm_net_mock_battle_auto_replay_inflight == 0)
        vm_net_mock_battle_auto_remember_operation(
            g_vm_net_mock_battle_role_id_current, index, operate, "battle-operate");
    printf("[info][network] mock_battle_operate index=%u operate=%u skill=%u target_mode=%u targets=%u wires=%u/%u/%u amount=%u/%u/%u hit=%u/%u/%u crit=%u/%u/%u action=%u actions=%u effect=%u actor=%u target=%u enemyhp=%u slots=%u/%u/%u rolehp=%u counters=%u deaths=%u deathActor=%u counterdmg=%u mpcost=%u valueB=%u teaminfo=%u:%u/%u bundle=%u pending=%u order=%s terminal=%u costAction=%u costHp=%u costMp=%u mp=%u/%u resp=%u evidence=skill.dsh:目标指向,mmBattle:0x6EB0\n",
           index, operate, operateIsSkill ? 1 : 0,
           skillTargetsEnemyGroup ? 4 :
               ((skillTargetsFriendlyGroupHeal || skillTargetsFriendlyGroupModifier) ? 2 : 0),
           attackTargetCount,
           attackWireSlots[0], attackWireSlots[1], attackWireSlots[2],
           attackDamageValues[0], attackDamageValues[1], attackDamageValues[2],
           attackHitFlags[0], attackHitFlags[1], attackHitFlags[2],
           attackCriticalFlags[0], attackCriticalFlags[1], attackCriticalFlags[2],
           firstActionType, actionCount,
           (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
           firstRecordWireActorUsed, firstRecordWireTargetUsed,
           g_mockBattleEnemyHpCurrent,
           g_mockBattleEnemyHpSlots[0],
           g_mockBattleEnemyHpSlots[1],
           g_mockBattleEnemyHpSlots[2],
           g_mockBattleRoleHpCurrent,
           allowCounterattack ? counterWireCount : 0,
           deathActionCount,
           deathActionCount ? deathActionWireSlot : 0,
           counterDamageValue,
           skillMpCost, firstRecordMpDelta,
           skillTeamInfoEnabled ? skillTeamRoleId : 0,
           skillTeamInfoEnabled ? skillTeamHp : 0,
           skillTeamInfoEnabled ? skillTeamMp : 0,
           bundleWholeRound ? 1 : 0,
           g_mockBattlePendingEnemyTurn ? 1 : 0,
           battleEndsThisRound ? "action6-first" : "action6-only",
           terminalActionEnabled ? 1 : 0,
           (operateIsSkill && skillCostActionEnabled) ? skillCostActionType : 0,
           (operateIsSkill && skillCostActionEnabled) ? skillCostValueA : 0,
           (operateIsSkill && skillCostActionEnabled) ? skillCostValueB : 0,
           skillMpBefore, skillMpPrepared ? skillMpAfter : skillMpBefore, pos);
    vm_autotest_note("mock_battle_operate index=%u operate=%u skill=%u target_mode=%u targets=%u wires=%u/%u/%u amount=%u/%u/%u hit=%u/%u/%u crit=%u/%u/%u action=%u actions=%u effect=%u actor=%u target=%u enemyhp=%u slots=%u/%u/%u rolehp=%u counters=%u deaths=%u deathActor=%u counterdmg=%u mpcost=%u valueB=%u teaminfo=%u:%u/%u bundle=%u pending=%u order=%s terminal=%u costAction=%u costHp=%u costMp=%u mp=%u/%u response=4/6 evidence=skill.dsh:目标指向,mmBattle:0x6EB0\n",
                     index, operate, operateIsSkill ? 1 : 0,
                     skillTargetsEnemyGroup ? 4 :
                         ((skillTargetsFriendlyGroupHeal || skillTargetsFriendlyGroupModifier) ? 2 : 0),
                     attackTargetCount,
                     attackWireSlots[0], attackWireSlots[1], attackWireSlots[2],
                     attackDamageValues[0], attackDamageValues[1], attackDamageValues[2],
                     attackHitFlags[0], attackHitFlags[1], attackHitFlags[2],
                     attackCriticalFlags[0], attackCriticalFlags[1], attackCriticalFlags[2],
                     firstActionType, actionCount,
                     (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
                     firstRecordWireActorUsed, firstRecordWireTargetUsed,
                     g_mockBattleEnemyHpCurrent,
                     g_mockBattleEnemyHpSlots[0],
                     g_mockBattleEnemyHpSlots[1],
                     g_mockBattleEnemyHpSlots[2],
                     g_mockBattleRoleHpCurrent,
                     allowCounterattack ? counterWireCount : 0,
                     deathActionCount,
                     deathActionCount ? deathActionWireSlot : 0,
                     counterDamageValue,
                     skillMpCost, firstRecordMpDelta,
                     skillTeamInfoEnabled ? skillTeamRoleId : 0,
                     skillTeamInfoEnabled ? skillTeamHp : 0,
                     skillTeamInfoEnabled ? skillTeamMp : 0,
                     bundleWholeRound ? 1 : 0,
                     g_mockBattlePendingEnemyTurn ? 1 : 0,
                     battleEndsThisRound ? "action6-first" : "action6-only",
                     terminalActionEnabled ? 1 : 0,
                     (operateIsSkill && skillCostActionEnabled) ? skillCostActionType : 0,
                     (operateIsSkill && skillCostActionEnabled) ? skillCostValueA : 0,
                     (operateIsSkill && skillCostActionEnabled) ? skillCostValueB : 0,
                     skillMpBefore, skillMpPrepared ? skillMpAfter : skillMpBefore);
    return pos;
}

static u32 vm_net_mock_build_battle_operate_response_fallback(const u8 *request, u32 requestLen,
                                                              u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 index = 0;
    u32 operate = 0;
    u8 index8 = 0;
    u8 operate8 = 0;
    /* Keep the relaxed 4/2 builder on the same actioninfo bound as the
     * normal builder.  It is a wire-layout adapter, not a lower-capacity
     * battle implementation. */
    u8 actionInfo[VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX];
    u32 actionInfoLen = 0;
    u32 responseObjectCount = 1;
    u8 requestKind = 0;
    u8 requestSubtype = 0;
    u8 actorSlot = 0;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 playerSlot = 0;
    u8 enemySlot = 0;
    u8 requestedTargetSlot = enemySlot;
    u8 firstRecordWireActorUsed = 0;
    u8 firstRecordWireTargetUsed = 0;
    u32 attackDamageValue = 12;
    u32 counterDamageValue = 0;
    u32 attackHpDelta = 0;
    u32 counterHpDelta = 0;
    u8 actionCount = 1;
    bool bundleWholeRound = false;
    bool teamBattleOperation = false;
    u8 firstRecordActorWireSlot = 0;
    u8 firstRecordChildFlag = 0;
    u8 counterRecordChildFlag = 0;
    u32 firstRecordMpDelta = 0;
    u32 counterRecordMpDelta = 0;
    bool battleEndsThisRound = false;
    bool allowCounterattack = false;
    bool deathActionNeeded = false;
    u8 deathActionWireSlot = 0;
    u8 deathActionCount = 0;
    bool terminalActionEnabled = vm_net_mock_battle_terminal_action_enabled();
    bool terminalFollowup = false;
    bool terminalStatusAppended = false;
    bool terminalCloseDeferred = false;
    bool operateIsSkill = false;
    bool operateConsumesTurn = false;
    bool skillMpPrepared = false;
    u32 skillMpCost = 0;
    u32 skillMpBefore = 0;
    u32 skillMpAfter = 0;
    u32 skillMpDelta = 0;
    bool skillTeamInfoEnabled = false;
    u32 skillTeamRoleId = 0;
    u32 skillTeamHp = 0;
    u32 skillTeamMp = 0;
    u32 skillCostValueA = 0;
    u32 skillCostValueB = 0;
    bool skillCostActionEnabled = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_ACTION_ENABLED", 0) != 0;
    u8 skillCostActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_ACTION_TYPE", 0);
    u32 skillCostEffectIndex = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_EFFECT_INDEX", 0);
    bool skillTargetsEnemyGroup = false;
    bool skillTargetsFriendlyGroupHeal = false;
    bool skillTargetsFriendlyGroupModifier = false;
    bool skillTargetsFriendlySingleHeal = false;
    bool skillTargetsFriendlySingleRevive = false;
    bool skillTargetsSelfModifier = false;
    bool skillTargetsEnemyControl = false;
    bool skillTargetsEnemyDot = false;
    bool skillTargetsFriendlyHp = false;
    bool skillTargetsTimedModifier = false;
    bool attackChecksHit = false;
    u8 attackWireSlots[3] = {0, 0, 0};
    u32 attackDamageValues[3] = {0, 0, 0};
    u8 attackHitFlags[3] = {0, 0, 0};
    u8 attackCriticalFlags[3] = {0, 0, 0};
    u8 attackChildFlags[3] = {0, 0, 0};
    u32 attackChildValueAs[3] = {0, 0, 0};
    u32 attackChildValueBs[3] = {0, 0, 0};
    u8 attackTargetCount = 0;
    u8 deathActionWireSlots[3] = {0, 0, 0};
    u8 deathActionTargetCount = 0;
    u8 counterWireSlots[3] = {0, 0, 0};
    u32 counterDamageValues[3] = {0, 0, 0};
    u8 counterChildFlags[3] = {0, 0, 0};
    u8 counterWireCount = 0;
    u8 dotWireSlots[3] = {0, 0, 0};
    u32 dotDamageValues[3] = {0, 0, 0};
    u8 dotTargetCount = 0;
    u32 dotEffectIndex = 0;
    u8 actionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_ACTION_TYPE", 0);
    u8 firstActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTION_TYPE", actionType);
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", actionType);
    u8 deathActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_DEATH_ACTION_TYPE", 3);
    u8 terminalActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTION_TYPE", 3);
    u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_is_battle_operate_request_relaxed(request, requestLen))
    {
        if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &requestKind, &requestSubtype) ||
            requestKind != 4 || requestSubtype != 2)
            return 0;
    }
    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT", defaultPlayerSlot);
    enemySlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT", defaultEnemySlot);

    if (!vm_net_mock_get_object_u32_field(request, requestLen, "index", &index) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "index", &index8))
        index = index8;
    if (!vm_net_mock_get_object_u32_field(request, requestLen, "Operate", &operate) &&
        vm_net_mock_get_object_u8_field(request, requestLen, "Operate", &operate8))
        operate = operate8;
    (void)vm_net_mock_battle_fallback_unaffordable_skill(&operate,
                                                          "battle-operate-fallback");
    operateIsSkill = vm_net_mock_battle_operate_is_skill(operate);
    operateConsumesTurn = operate == 0 || operateIsSkill;
    skillTargetsEnemyGroup = operateIsSkill &&
                             vm_net_mock_battle_operate_skill_targets_enemy_group(operate);
    skillTargetsFriendlyGroupHeal = operateIsSkill &&
                                    vm_net_mock_battle_operate_skill_targets_friendly_group_heal(operate);
    skillTargetsFriendlyGroupModifier = operateIsSkill &&
                                        vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(operate);
    skillTargetsFriendlySingleHeal = operateIsSkill &&
                                      vm_net_mock_battle_operate_skill_targets_friendly_single_heal(operate);
    skillTargetsFriendlySingleRevive = operateIsSkill &&
                                        vm_net_mock_battle_operate_skill_targets_friendly_single_revive(operate);
    skillTargetsSelfModifier = operateIsSkill &&
                              vm_net_mock_battle_operate_skill_targets_self_modifier(operate);
    skillTargetsEnemyControl = operateIsSkill &&
                                vm_net_mock_battle_operate_skill_targets_enemy_control(operate);
    skillTargetsEnemyDot = operateIsSkill &&
                           vm_net_mock_battle_operate_skill_targets_enemy_dot(operate);
    skillTargetsFriendlyHp = skillTargetsFriendlyGroupHeal ||
                             skillTargetsFriendlySingleHeal ||
                             skillTargetsFriendlySingleRevive;
    skillTargetsTimedModifier = skillTargetsFriendlyGroupModifier ||
                                skillTargetsSelfModifier;
    if (operateIsSkill)
    {
        firstActionType = 1;
        type1EffectIndex = vm_net_mock_battle_operate_skill_effect(operate);
        skillMpPrepared = vm_net_mock_battle_prepare_skill_mp(operate,
                                                              &skillMpBefore,
                                                              &skillMpAfter,
                                                              &skillMpCost);
    }

    actorSlot = (u8)(index & 0xFFu);
    requestedTargetSlot = vm_net_mock_battle_target_wire_slot_from_request(actorSlot,
                                                                            playerOnRight,
                                                                            battleSide,
                                                                            enemySlot);
    if (requestedTargetSlot == playerSlot || requestedTargetSlot > 5)
        requestedTargetSlot = enemySlot;
    if (g_mockBattleOperateSessionFinished != 0)
        g_mockBattleOperateSessionFinished = 0;
    terminalFollowup = false;
    bundleWholeRound = g_mockBattleOperateSessionArmed != 0 &&
                       (g_vm_net_mock_team_battle_party_count_current >= 2
                            ? g_vm_net_mock_team_battle_resolve_monsters_current != 0
                            : (operateConsumesTurn &&
                               vm_net_mock_env_u32("CBE_BATTLE_BUNDLE_ROUND", 1) != 0));
    teamBattleOperation = g_vm_net_mock_team_battle_party_count_current >= 2;
    firstRecordActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_ACTOR_WIRE_SLOT",
                                                       playerSlot);
    firstRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_CHILD_FLAG", 0);
    counterRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    if (g_mockBattleAwaitingSettlement != 0)
    {
        (void)requestedTargetSlot;
        if (g_vm_net_mock_battle_settlement_sent_serial != g_mockBattleOperateSessionSerial)
            return vm_net_mock_build_battle_pending_settlement_response(out, outCap);
        return vm_net_mock_build_battle_case11_auto_off_response(out, outCap);
    }
    if (!terminalFollowup && g_mockBattleEnemyHpCurrent == 0)
    {
        vm_net_mock_battle_reset_enemy_hp_from_stats(g_vm_net_mock_battle_enemy_id_current);
    }
    /* Keep zero HP terminal in the lenient 4/2 parser as well; it must have
     * the same lifecycle as the strict parser above. */
    if (!terminalFollowup && g_mockBattleEnemyHpCurrent > 0)
        requestedTargetSlot = vm_net_mock_battle_select_live_enemy_wire(requestedTargetSlot,
                                                                        playerOnRight,
                                                                        battleSide,
                                                                        enemySlot);
    if (!terminalFollowup && g_mockBattlePendingEnemyTurn != 0 &&
        g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0)
    {
        u8 counterEnemyWireSlot = vm_net_mock_battle_first_alive_enemy_wire(
            playerOnRight, battleSide, enemySlot);
        u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                         counterEnemyWireSlot);
        u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                          playerSlot);
        if (counterActionType == 1)
        {
            counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                          counterActorWireSlot);
            counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                           counterTargetWireSlot);
        }
        vm_net_mock_battle_select_enemy_modifier_for_wire(
            counterEnemyWireSlot, playerOnRight, battleSide, enemySlot);
        counterDamageValue = vm_net_mock_battle_apply_damage_to_role(
            vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                    g_mockBattleRoleHpCurrent,
                                                    counterEnemyWireSlot));
        if (counterDamageValue == 0)
            counterRecordChildFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE;
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
        counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
        g_mockBattlePendingEnemyTurn = 0;
        ++g_mockBattleOperateTurnCounter;
        {
            u32 pendingLen = vm_net_mock_build_battle_enemy_turn_response(out, outCap,
                                                                          counterActionType,
                                                                          counterActorWireSlot,
                                                                          counterTargetWireSlot,
                                                                          counterRecordChildFlag,
                                                                          counterHpDelta,
                                                                          counterRecordMpDelta,
                                                                          playerSlot);
            if (g_mockBattleRoleHpCurrent == 0)
            {
                g_mockBattleOperateSessionArmed = 0;
                g_mockBattleOperateSessionFinished = 0;
                g_mockBattlePendingEnemyTurn = 0;
                g_mockBattleAwaitingSettlement = 0;
                vm_net_mock_battle_save_completed_current_role_state(
                    "battle-pending-enemy-fallback-death");
            }
            printf("[info][network] mock_battle_pending_enemy_turn actor=%u target=%u damage=%u enemyhp=%u slots=%u/%u/%u rolehp=%u resp=%u evidence=mmBattle:0x6EB0\n",
                   counterActorWireSlot,
                   counterTargetWireSlot,
                   counterDamageValue,
                   g_mockBattleEnemyHpCurrent,
                   g_mockBattleEnemyHpSlots[0],
                   g_mockBattleEnemyHpSlots[1],
                   g_mockBattleEnemyHpSlots[2],
                   g_mockBattleRoleHpCurrent,
                   pendingLen);
            return pendingLen;
        }
    }
    if (terminalFollowup)
    {
        attackDamageValue = 0;
        counterDamageValue = 0;
        actionCount = 0;
    }
    else
    {
        if (skillTargetsFriendlyGroupHeal)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_friendly_group_heal_targets(
                operate, playerSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsFriendlyGroupModifier)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_friendly_group_modifier_targets(
                operate, playerSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsSelfModifier)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_self_modifier_target(
                operate, playerSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsFriendlySingleHeal || skillTargetsFriendlySingleRevive)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_friendly_single_heal_target(
                operate, actorSlot, playerSlot, skillTargetsFriendlySingleRevive,
                attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsEnemyControl)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_enemy_control_targets(
                operate, skillTargetsEnemyGroup, requestedTargetSlot, playerOnRight,
                battleSide, enemySlot, attackWireSlots, attackDamageValues);
            if (attackTargetCount != 0)
            {
                const vm_net_mock_skill_catalog_item *skill =
                    vm_net_mock_battle_operate_skill(operate);
                for (u8 i = 0; i < attackTargetCount; ++i)
                {
                    vm_net_mock_battle_apply_enemy_effect_to_wire(
                        skill, attackWireSlots[i], playerOnRight, battleSide, enemySlot, 0);
                }
            }
        }
        else if (skillTargetsEnemyDot)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_enemy_dot_targets(
                operate, skillTargetsEnemyGroup, requestedTargetSlot, playerOnRight,
                battleSide, enemySlot, attackWireSlots, attackDamageValues);
        }
        else
        {
            attackChecksHit = true;
            attackTargetCount = vm_net_mock_battle_apply_player_attack_targets(
                operate, operateIsSkill, skillTargetsEnemyGroup, requestedTargetSlot,
                playerOnRight, battleSide, enemySlot, attackWireSlots, attackDamageValues,
                deathActionWireSlots, &deathActionTargetCount,
                attackHitFlags, attackCriticalFlags);
        }
        if (attackTargetCount == 0)
            return 0;
        attackDamageValue = attackDamageValues[0];
        deathActionNeeded = deathActionTargetCount != 0;
        deathActionWireSlot = deathActionNeeded ? deathActionWireSlots[0] : 0;
        if (bundleWholeRound && g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0)
        {
            counterWireCount = vm_net_mock_battle_collect_live_enemy_wires(
                playerOnRight, battleSide, enemySlot, counterWireSlots);
        }
        allowCounterattack = bundleWholeRound && counterWireCount != 0 &&
                             g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0;
        if (allowCounterattack)
        {
            for (u8 i = 0; i < counterWireCount && i < 3 && g_mockBattleRoleHpCurrent > 0; ++i)
            {
                vm_net_mock_battle_select_enemy_modifier_for_wire(
                    counterWireSlots[i], playerOnRight, battleSide, enemySlot);
                u32 oneCounterDamage = vm_net_mock_battle_apply_damage_to_role(
                    vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                            g_mockBattleRoleHpCurrent,
                                                            counterWireSlots[i]));
                counterDamageValues[i] = oneCounterDamage;
                counterChildFlags[i] = oneCounterDamage == 0
                                           ? VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE
                                           : counterRecordChildFlag;
                /* A zero HP delta is a valid miss record, not an absent
                 * monster turn. */
                if (oneCounterDamage != 0)
                {
                    counterDamageValue = vm_net_mock_add_capped_u32(counterDamageValue,
                                                                    oneCounterDamage);
                }
            }
        }
        if (bundleWholeRound && g_mockBattleEnemyHpCurrent > 0)
        {
            dotTargetCount = vm_net_mock_battle_apply_due_enemy_dot_damage(
                playerOnRight, battleSide, enemySlot, dotWireSlots, dotDamageValues,
                deathActionWireSlots, &deathActionTargetCount, &dotEffectIndex);
            if (deathActionTargetCount != 0)
            {
                deathActionNeeded = true;
                deathActionWireSlot = deathActionWireSlots[0];
            }
        }
        battleEndsThisRound = (g_mockBattleEnemyHpCurrent == 0 || g_mockBattleRoleHpCurrent == 0);
    }
    if (!terminalFollowup)
    {
        attackHpDelta = skillTargetsFriendlyHp ? attackDamageValue :
                        (skillTargetsTimedModifier || skillTargetsEnemyControl ||
                         skillTargetsEnemyDot ? 0 :
                         vm_net_mock_battle_negative_delta_u32(attackDamageValue));
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
    }
    /* Never let a response-only override disagree with the clamped damage
     * that was applied to g_mockBattleRoleHpCurrent. */
    skillMpDelta = vm_net_mock_env_u32("CBE_BATTLE_SKILL_MP_VALUE_B",
                                       skillMpPrepared ? skillMpAfter :
                                                         vm_net_mock_battle_role_mp_current());
    firstRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_B", 0);
    for (u8 i = 0; i < attackTargetCount && i < 3; ++i)
    {
        attackChildFlags[i] = firstRecordChildFlag;
        if (attackChecksHit)
        {
            attackChildFlags[i] = !attackHitFlags[i]
                                      ? VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE
                                      : (attackCriticalFlags[i]
                                             ? VM_NET_MOCK_BATTLE_CHILD_FLAG_CRITICAL
                                             : firstRecordChildFlag);
        }
        attackChildValueAs[i] = skillTargetsFriendlyHp ? attackDamageValues[i] :
                                (skillTargetsTimedModifier || skillTargetsEnemyControl ||
                                 skillTargetsEnemyDot ? 0 :
                                 vm_net_mock_battle_negative_delta_u32(attackDamageValues[i]));
        attackChildValueBs[i] = firstRecordMpDelta;
    }
    if (attackTargetCount != 0)
        attackChildValueAs[0] = attackHpDelta;
    skillCostValueA = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_VALUE_A",
                                          g_mockBattleRoleHpCurrent);
    skillCostValueB = skillMpDelta;
    counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
    if (operateIsSkill && skillMpPrepared)
        vm_net_mock_battle_commit_skill_mp(skillMpAfter);
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        bool terminalInlineSettlement = !teamBattleOperation &&
                                       battleEndsThisRound &&
                                       g_mockBattleEnemyHpCurrent == 0 &&
                                       g_mockBattleRoleHpCurrent > 0 &&
                                       vm_net_mock_battle_inline_settlement_enabled();

        if (terminalInlineSettlement)
        {
            skillTeamRoleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
            vm_net_mock_battle_project_terminal_vitals(
                g_mockBattleRoleHpCurrent, g_mockBattleRoleHpMax,
                vm_net_mock_battle_role_mp_current(), g_mockBattleRoleMpMax,
                &skillTeamHp, &skillTeamMp);
            skillTeamInfoEnabled = skillTeamRoleId != 0;
            printf("[info][network] mock_battle_terminal_action_vitals "
                   "role=%u hp=%u/%u mp=%u/%u source=4/6-teaminfo-before-4/7-fallback\n",
                   skillTeamRoleId,
                   skillTeamHp,
                   g_mockBattleRoleHpMax,
                   skillTeamMp,
                   g_mockBattleRoleMpMax);
        }
        else if (operateIsSkill && skillMpPrepared)
        {
            skillTeamRoleId = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_ROLE_ID",
                                                  role ? role->roleId :
                                                         VM_NET_MOCK_ROLE_DEFAULT_ID);
            skillTeamHp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_HP",
                                              g_mockBattleRoleHpCurrent);
            skillTeamMp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_MP",
                                              vm_net_mock_battle_role_mp_current());
            skillTeamInfoEnabled = skillTeamRoleId != 0;
        }
    }
    if (terminalFollowup)
    {
        u8 terminalObjectCount = 0;
        bool terminalStatusAppended = false;
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &terminalObjectCount, false,
                &terminalStatusAppended))
            return 0;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &terminalObjectCount,
                                                               "battle-operate-fallback-terminal",
                                                               true))
            return 0;
        if (!terminalStatusAppended)
        {
            if (!vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
                !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            {
                return 0;
            }
            terminalObjectCount += 2;
        }
        vm_net_mock_finish_wt_packet(out, pos, terminalObjectCount);
        g_mockBattleOperateSessionFinished = 0;
        return pos;
    }
    memset(actionInfo, 0, sizeof(actionInfo));
    {
        u8 mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_ACTOR_WIRE_SLOT",
                                                         firstRecordActorWireSlot);
        u8 mappedTargetWireSlot = attackTargetCount != 0 ? attackWireSlots[0] :
                                                          requestedTargetSlot;
        if (firstActionType == 1)
        {
            mappedActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_FIRST_ACTOR_WIRE_SLOT",
                                                         mappedActorWireSlot);
            if (!skillTargetsEnemyGroup && !skillTargetsFriendlyGroupHeal &&
                !skillTargetsFriendlyGroupModifier && !skillTargetsSelfModifier &&
                !skillTargetsFriendlySingleHeal && !skillTargetsFriendlySingleRevive &&
                !skillTargetsEnemyControl && !skillTargetsEnemyDot)
                mappedTargetWireSlot = (u8)vm_net_mock_env_u32(
                    "CBE_BATTLE_TYPE1_FIRST_TARGET_WIRE_SLOT", mappedTargetWireSlot);
        }
        else
        {
            mappedTargetWireSlot = (u8)vm_net_mock_env_u32(
                "CBE_BATTLE_FIRST_TARGET_WIRE_SLOT", mappedTargetWireSlot);
        }
        firstRecordWireActorUsed = mappedActorWireSlot;
        firstRecordWireTargetUsed = mappedTargetWireSlot;
        if (!terminalFollowup)
        {
            if (attackTargetCount > 1 && firstActionType == 1)
            {
                if (!vm_net_mock_append_battle_actioninfo_record_children(
                        actionInfo, sizeof(actionInfo), &actionInfoLen, firstActionType,
                        mappedActorWireSlot, attackWireSlots, attackChildFlags,
                        attackChildValueAs, attackChildValueBs, attackTargetCount,
                        type1EffectIndex, type1Tail0, type1Tail1, type1Tail2))
                {
                    return 0;
                }
            }
            else if (!vm_net_mock_append_battle_actioninfo_record(
                         actionInfo, sizeof(actionInfo), &actionInfoLen, firstActionType,
                         mappedActorWireSlot, mappedTargetWireSlot, attackChildFlags[0],
                         attackHpDelta, firstRecordMpDelta,
                         (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail0 : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail1 : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail2 : 0))
            {
                return 0;
            }
            /*
             * Disabled by default. Runtime negatives showed that a separate
             * MP-cost action is still animated as a normal target update:
             * valueA=0 shows a 0 HP line and valueA=current HP shows a heal.
             * Keep this branch as an explicit experiment only.
             */
            if (skillCostActionEnabled &&
                operateIsSkill && skillMpPrepared && skillMpCost != 0 && firstActionType == 1)
            {
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, skillCostActionType,
                                                                 mappedActorWireSlot,
                                                                 mappedActorWireSlot,
                                                                 0, skillCostValueA,
                                                                 skillCostValueB,
                                                                 (skillCostActionType == 1 || skillCostActionType == 2) ? skillCostEffectIndex : 0,
                                                                 0, 0, 0))
                {
                    return 0;
                }
            }
            if (deathActionNeeded)
            {
                for (u8 i = 0; i < deathActionTargetCount && i < 3; ++i)
                {
                    if (actionCount < 6)
                        ++actionCount;
                    else
                        return 0;
                    if (!vm_net_mock_append_battle_actioninfo_record(
                            actionInfo, sizeof(actionInfo), &actionInfoLen, deathActionType,
                            deathActionWireSlots[i], 0, 0, 0, 0, 0, 0, 0, 0))
                    {
                        return 0;
                    }
                    ++deathActionCount;
                }
            }
        }
            if (allowCounterattack)
            {
            for (u8 i = 0; i < counterWireCount && i < 3; ++i)
            {
                u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                                 counterWireSlots[i]);
                u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                                  playerSlot);
                if (counterActionType == 1)
                {
                    counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                                  counterActorWireSlot);
                    counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                                   counterTargetWireSlot);
                }
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, counterActionType,
                                                                 counterActorWireSlot, counterTargetWireSlot,
                                                                 counterChildFlags[i],
                                                                 vm_net_mock_battle_negative_delta_u32(counterDamageValues[i]),
                                                                 counterRecordMpDelta,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1EffectIndex : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail0 : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail1 : 0,
                                                                 (counterActionType == 1 || counterActionType == 2) ? type1Tail2 : 0))
                    return 0;
                }
            }
            if (dotTargetCount != 0)
            {
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (dotTargetCount > 1)
                {
                    u8 dotFlags[3] = {0, 0, 0};
                    u32 dotValueAs[3] = {0, 0, 0};
                    u32 dotValueBs[3] = {0, 0, 0};
                    for (u8 i = 0; i < dotTargetCount; ++i)
                        dotValueAs[i] = vm_net_mock_battle_negative_delta_u32(dotDamageValues[i]);
                    if (!vm_net_mock_append_battle_actioninfo_record_children(
                            actionInfo, sizeof(actionInfo), &actionInfoLen, 1,
                            dotWireSlots[0], dotWireSlots, dotFlags, dotValueAs,
                            dotValueBs, dotTargetCount, dotEffectIndex, 0, 0, 0))
                    {
                        return 0;
                    }
                }
                else if (!vm_net_mock_append_battle_actioninfo_record(
                             actionInfo, sizeof(actionInfo), &actionInfoLen, 1,
                             dotWireSlots[0], dotWireSlots[0], 0,
                             vm_net_mock_battle_negative_delta_u32(dotDamageValues[0]), 0,
                             dotEffectIndex, 0, 0, 0))
                {
                    return 0;
                }
            }
            if (g_mockBattleRoleHpCurrent == 0)
        {
            if (actionCount < 6)
                ++actionCount;
            else
                return 0;
            deathActionWireSlot = playerSlot;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, deathActionType,
                                                             playerSlot, 0, 0,
                                                             0, 0, 0, 0, 0, 0))
            {
                return 0;
            }
            ++deathActionCount;
        }
        if (battleEndsThisRound &&
            g_mockBattleEnemyHpCurrent == 0 &&
            terminalActionEnabled &&
            !deathActionNeeded)
        {
            u8 terminalActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTOR_WIRE_SLOT",
                                                              (g_mockBattleEnemyHpCurrent == 0)
                                                                  ? requestedTargetSlot
                                                                  : playerSlot);
            if (actionCount < 6)
                ++actionCount;
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, terminalActionType,
                                                             terminalActorWireSlot, 0, 0,
                                                             0, 0, 0, 0, 0, 0))
            {
                return 0;
            }
        }
    }

    if (!vm_net_mock_append_battle_action6_object_ex(out, outCap, &pos,
                                                    actionInfo, actionInfoLen,
                                                    actionCount,
                                                    skillTeamInfoEnabled,
                                                    skillTeamRoleId,
                                                    skillTeamHp,
                                                    skillTeamMp))
        return 0;
    if (!teamBattleOperation && battleEndsThisRound &&
        g_mockBattleEnemyHpCurrent == 0 &&
        g_mockBattleRoleHpCurrent > 0 &&
        vm_net_mock_battle_inline_settlement_enabled())
    {
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &responseObjectCount, false,
                &terminalStatusAppended))
            return 0;
        if (terminalStatusAppended)
            g_vm_net_mock_battle_settlement_sent_serial =
                g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &responseObjectCount,
                                                               "battle-operate-fallback-inline",
                                                               true))
            return 0;
    }
    terminalCloseDeferred = !teamBattleOperation && !terminalStatusAppended &&
        vm_net_mock_battle_terminal_close_should_defer(battleEndsThisRound);
    if (terminalCloseDeferred)
    {
        vm_net_mock_battle_schedule_terminal_close_after_actions(
            actionCount, "battle-operate-fallback-no-result-panel");
    }
    vm_net_mock_finish_wt_packet(out, pos, (u8)responseObjectCount);
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound && !teamBattleOperation)
    {
        if (g_mockBattleRoleHpCurrent == 0)
        {
            vm_net_mock_battle_save_completed_current_role_state(
                "battle-operate-fallback-death");
        }
        else if (!terminalStatusAppended)
            vm_net_mock_battle_save_terminal_role_state("battle-operate-fallback", false);
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = (g_mockBattleRoleHpCurrent == 0) ? 0 : 1;
    }
    else if (!battleEndsThisRound && g_mockBattleOperateSessionArmed != 0 &&
             operateConsumesTurn && !bundleWholeRound)
    {
        g_mockBattlePendingEnemyTurn = 1;
    }
    if (g_vm_net_mock_team_battle_party_count_current < 2 && operateConsumesTurn &&
        !skillTargetsTimedModifier)
    {
        vm_net_mock_battle_modifier_advance_round(&g_vm_net_mock_battle_solo_modifier);
        g_vm_net_mock_battle_active_modifier_current = g_vm_net_mock_battle_solo_modifier;
    }
    if (g_vm_net_mock_team_battle_party_count_current < 2 && operateConsumesTurn)
        vm_net_mock_battle_advance_enemy_effects_current();
    if (operateConsumesTurn && g_vm_net_mock_battle_auto_replay_inflight == 0)
        vm_net_mock_battle_auto_remember_operation(g_vm_net_mock_battle_role_id_current, index, operate,
                                                   "battle-operate-fallback");
    return pos;
}

static u32 vm_net_mock_build_battle_operate_response_raw82(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u32 len = vm_net_mock_build_battle_operate_response_fallback(request, requestLen, out, outCap);

    if (len != 0)
    {
        return len;
    }

    return 0;
}

typedef struct
{
    bool active;
    bool duplicateAction;
    bool resolvesRound;
    vm_mock_service_team *team;
    vm_mock_service_client_session *session;
    u8 memberIndex;
    u8 memberBit;
    u8 aliveMask;
} vm_mock_service_team_battle_operation_context;

static bool vm_net_mock_build_team_battle_teaminfo_blob(
    u8 *out,
    u32 outCap,
    u32 *teamInfoLenOut,
    const vm_mock_service_team_battle_operation_context *context)
{
    vm_mock_service_team *team = context ? context->team : NULL;
    u32 wireIds[VM_MOCK_SERVICE_TEAM_MEMBER_MAX] = {0};
    u32 hps[VM_MOCK_SERVICE_TEAM_MEMBER_MAX] = {0};
    u32 mps[VM_MOCK_SERVICE_TEAM_MEMBER_MAX] = {0};

    if (teamInfoLenOut)
        *teamInfoLenOut = 0;
    if (out == NULL || context == NULL || context->session == NULL || team == NULL ||
        team->battleMemberCount < 2 ||
        team->battleMemberCount > VM_MOCK_SERVICE_TEAM_MEMBER_MAX)
    {
        return false;
    }
    for (u8 memberIndex = 0; memberIndex < team->battleMemberCount; ++memberIndex)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->battleMemberClientIds[memberIndex]);
        u32 hp = memberIndex == context->memberIndex ?
                     g_mockBattleRoleHpCurrent : team->battleMemberHp[memberIndex];
        u32 hpMax = memberIndex == context->memberIndex ?
                        g_mockBattleRoleHpMax : team->battleMemberHpMax[memberIndex];
        u32 mp = memberIndex == context->memberIndex ?
                     g_mockBattleRoleMpCurrent : team->battleMemberMp[memberIndex];
        u32 mpMax = memberIndex == context->memberIndex ?
                        g_mockBattleRoleMpMax : team->battleMemberMpMax[memberIndex];

        wireIds[memberIndex] = vm_mock_service_team_member_wire_id(context->session, member);
        if (member == NULL || wireIds[memberIndex] == 0)
            return false;
        if (hpMax != 0 && hp > hpMax)
            hp = hpMax;
        if (mpMax != 0 && mp > mpMax)
            mp = mpMax;
        hps[memberIndex] = hp;
        mps[memberIndex] = mp;
    }
    return vm_net_mock_build_battle_teaminfo_rows_blob(
        out, outCap, teamInfoLenOut, wireIds, hps, mps, team->battleMemberCount);
}

enum
{
    VM_MOCK_TEAM_BATTLE_BUILD_OPERATE = 1,
    VM_MOCK_TEAM_BATTLE_BUILD_OPERATE_FALLBACK = 2,
    VM_MOCK_TEAM_BATTLE_BUILD_ITEM = 3
};

static u16 vm_net_mock_copy_response_object(const u8 *packet,
                                            u32 packetLen,
                                            u8 kind,
                                            u8 subtype,
                                            u8 *objectOut,
                                            u32 objectCap)
{
    u32 pos = 5;

    if (packet == NULL || packetLen < 11 || packet[0] != 'W' || packet[1] != 'T')
        return 0;
    while (pos + 6 <= packetLen)
    {
        u16 objectLen = (u16)(((u16)packet[pos + 4] << 8) | packet[pos + 5]);
        if (objectLen < 6 || pos + objectLen > packetLen)
            return 0;
        if (packet[pos] == 1 && packet[pos + 1] == kind && packet[pos + 2] == subtype)
        {
            if (objectOut == NULL || objectLen > objectCap)
                return 0;
            memcpy(objectOut, packet + pos, objectLen);
            return objectLen;
        }
        pos += objectLen;
    }
    return 0;
}

/* Response objects built by vm_net_mock_begin_wt_object have a six-byte
 * header, followed by object-entry fields encoded as:
 *
 *   name_len:u8, name, value_len:be16, value[value_len]
 *
 * This is deliberately separate from vm_net_mock_get_object_blob_field().
 * The latter decodes vm_net_mock_put_object_blob()'s nested length wrapper,
 * while battle actioninfo is written with vm_net_mock_put_object_raw() and
 * therefore has only the entry's single value_len. */
static bool vm_net_mock_get_response_object_entry_field(
    const u8 *packet,
    u32 packetLen,
    u8 kind,
    u8 subtype,
    const char *field,
    const u8 **valueOut,
    u16 *valueLenOut)
{
    u32 objectPos = 5;
    u32 fieldNameLen = field ? (u32)strlen(field) : 0;

    if (valueOut)
        *valueOut = NULL;
    if (valueLenOut)
        *valueLenOut = 0;
    if (packet == NULL || packetLen < 11 ||
        packet[0] != 'W' || packet[1] != 'T' ||
        fieldNameLen == 0 || fieldNameLen > 0xff)
    {
        return false;
    }

    while (objectPos + 6 <= packetLen)
    {
        u16 objectLen = (u16)(((u16)packet[objectPos + 4] << 8) |
                              packet[objectPos + 5]);
        u32 entryPos = objectPos + 6;
        u32 objectEnd = objectPos + objectLen;

        if (objectLen < 6 || objectEnd > packetLen)
            return false;
        if (packet[objectPos] != 1 || packet[objectPos + 1] != kind ||
            packet[objectPos + 2] != subtype)
        {
            objectPos = objectEnd;
            continue;
        }

        while (entryPos < objectEnd)
        {
            u8 nameLen = packet[entryPos++];
            u16 valueLen = 0;
            const u8 *name = NULL;
            const u8 *value = NULL;

            if (nameLen == 0 || entryPos + nameLen + 2 > objectEnd)
                return false;
            name = packet + entryPos;
            entryPos += nameLen;
            valueLen = (u16)(((u16)packet[entryPos] << 8) |
                             packet[entryPos + 1]);
            entryPos += 2;
            if (entryPos + valueLen > objectEnd)
                return false;
            value = packet + entryPos;
            if (nameLen == fieldNameLen &&
                memcmp(name, field, fieldNameLen) == 0)
            {
                if (valueOut)
                    *valueOut = value;
                if (valueLenOut)
                    *valueLenOut = valueLen;
                return true;
            }
            entryPos += valueLen;
        }
        return false;
    }
    return false;
}

/* `1/4/6.teaminfo` is not a vm_net_mock_put_object_blob() value.  The
 * mmBattle parser consumes its entry payload directly as 14-byte overlapped
 * rows (00 04, role-id, hp, mp), so every service-side reader must use the
 * response-entry accessor rather than the nested-blob accessor. */
static bool vm_net_mock_get_battle_teaminfo_raw_field(
    const u8 *packet,
    u32 packetLen,
    const u8 **teamInfoOut,
    u16 *teamInfoLenOut)
{
    const u8 *teamInfo = NULL;
    u16 teamInfoLen = 0;

    if (teamInfoOut)
        *teamInfoOut = NULL;
    if (teamInfoLenOut)
        *teamInfoLenOut = 0;
    if (!vm_net_mock_get_response_object_entry_field(
            packet, packetLen, 4, 6, "teaminfo", &teamInfo, &teamInfoLen) ||
        teamInfo == NULL || teamInfoLen == 0 || (teamInfoLen % 14u) != 0)
    {
        return false;
    }
    if (teamInfoOut)
        *teamInfoOut = teamInfo;
    if (teamInfoLenOut)
        *teamInfoLenOut = teamInfoLen;
    return true;
}

static u8 vm_mock_service_team_battle_alive_mask(const vm_mock_service_team *team)
{
    u8 mask = 0;

    if (team == NULL)
        return 0;
    for (u8 i = 0; i < team->battleMemberCount && i < 8; ++i)
    {
        if (team->battleMemberHp[i] != 0)
            mask = (u8)(mask | (u8)(1u << i));
    }
    return mask;
}

static u32 vm_net_mock_build_team_battle_round_wait_response(
    u8 *out,
    u32 outCap,
    const vm_mock_service_team_battle_operation_context *context,
    const char *reason)
{
    u32 pos = 5;

    if (out == NULL || outCap < pos || context == NULL || context->team == NULL)
        return 0;
    /* A duplicate/dead-member acknowledgement must not be a zero-action 4/6.
     * HandleBattleActionMsg still treats subtype 6 as an action-list boundary;
     * a valid zero-object WT packet completes the request without advancing the
     * battle module's local action phase. */
    vm_net_mock_finish_wt_packet(out, pos, 0);
    printf("[info][mock-service] team_battle_round_wait battle=%u round=%u "
           "source=%08x actor=%u acted=%02x alive=%02x reason=%s resp=%u\n",
           context->team->battleSerial,
           context->team->battleRoundSerial,
           context->session ? context->session->clientId : 0,
           context->memberIndex,
           context->team->battleRoundActedMask,
           context->aliveMask,
           reason ? reason : "wait",
           pos);
    return pos;
}

static bool vm_net_mock_copy_non_battle_action_objects(
    const u8 *packet,
    u32 packetLen,
    u8 *objectsOut,
    u32 objectsCap,
    u32 *objectsLenOut,
    u8 *objectCountOut)
{
    u32 readPos = 5;
    u32 writePos = 0;
    u8 objectCount = 0;

    if (objectsLenOut)
        *objectsLenOut = 0;
    if (objectCountOut)
        *objectCountOut = 0;
    if (packet == NULL || packetLen < 5 || packet[0] != 'W' || packet[1] != 'T')
        return false;
    while (readPos + 6 <= packetLen)
    {
        u16 objectLen = (u16)(((u16)packet[readPos + 4] << 8) |
                              packet[readPos + 5]);

        if (objectLen < 6 || readPos + objectLen > packetLen)
            return false;
        /* No battle-module command may escape before the round barrier.  In
         * particular, a non-final killing blow produces both 4/6 and 4/7;
         * forwarding that settlement object would end the requester's battle
         * while the other living members are still choosing actions.  Item and
         * inventory companion objects from other kinds remain safe to return. */
        if (!(packet[readPos] == 1 && packet[readPos + 1] == 4))
        {
            if (objectsOut == NULL || writePos + objectLen > objectsCap ||
                objectCount == 0xff)
            {
                return false;
            }
            memcpy(objectsOut + writePos, packet + readPos, objectLen);
            writePos += objectLen;
            ++objectCount;
        }
        readPos += objectLen;
    }
    if (readPos != packetLen)
        return false;
    if (objectsLenOut)
        *objectsLenOut = writePos;
    if (objectCountOut)
        *objectCountOut = objectCount;
    return true;
}

static bool vm_mock_service_team_battle_capture_round_action(
    const vm_mock_service_team_battle_operation_context *context,
    const u8 *response,
    u32 responseLen)
{
    vm_mock_service_team *team = context ? context->team : NULL;
    vm_mock_service_team_battle_round_action *pending = NULL;
    const u8 *actionInfo = NULL;
    const u8 *teamInfo = NULL;
    u16 actionInfoLen = 0;
    u16 teamInfoLen = 0;
    u8 actionCount = 0;

    if (context == NULL || !context->active || team == NULL ||
        context->memberIndex >= VM_MOCK_SERVICE_TEAM_MEMBER_MAX ||
        !vm_net_mock_get_object_u8_field(response, responseLen,
                                         "actionnum", &actionCount) ||
        actionCount == 0 ||
        !vm_net_mock_get_response_object_entry_field(
            response, responseLen, 4, 6,
            "actioninfo", &actionInfo, &actionInfoLen) ||
        actionInfo == NULL || actionInfoLen == 0 ||
        actionInfoLen > VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX)
    {
        return false;
    }

    pending = &team->battleRoundActions[context->memberIndex];
    ++team->battleRoundActionSerial;
    if (team->battleRoundActionSerial == 0)
        team->battleRoundActionSerial = 1;
    memset(pending, 0, sizeof(*pending));
    pending->valid = true;
    pending->serial = team->battleRoundActionSerial;
    pending->sourceClientId = context->session ? context->session->clientId : 0;
    pending->memberIndex = context->memberIndex;
    pending->actionCount = actionCount;
    pending->includesTeamInfo = vm_net_mock_get_battle_teaminfo_raw_field(
        response, responseLen, &teamInfo, &teamInfoLen);
    pending->actionInfoLen = actionInfoLen;
    memcpy(pending->actionInfo, actionInfo, actionInfoLen);
    printf("[info][mock-service] team_battle_round_capture battle=%u round=%u "
           "source=%08x actor=%u order=%u actions=%u info=%u teaminfo=%u acted=%02x alive=%02x\n",
           team->battleSerial,
           team->battleRoundSerial,
           pending->sourceClientId,
           pending->memberIndex,
           pending->serial,
           pending->actionCount,
           pending->actionInfoLen,
           pending->includesTeamInfo ? 1 : 0,
           team->battleRoundActedMask,
           context->aliveMask);
    return true;
}

static void vm_mock_service_team_battle_clear_round_actions(
    vm_mock_service_team *team)
{
    if (team == NULL)
        return;
    memset(team->battleRoundActions, 0, sizeof(team->battleRoundActions));
    team->battleRoundTerminalPending = false;
}

static u32 vm_net_mock_build_team_battle_deferred_ack(
    u8 *out,
    u32 outCap,
    const u8 *response,
    u32 responseLen,
    const vm_mock_service_team_battle_operation_context *context)
{
    u8 extraObjects[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX * 2];
    u32 extraObjectsLen = 0;
    u8 extraObjectCount = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_copy_non_battle_action_objects(
            response, responseLen,
            extraObjects, sizeof(extraObjects),
            &extraObjectsLen, &extraObjectCount) ||
        pos + extraObjectsLen > outCap)
    {
        return 0;
    }
    if (extraObjectsLen != 0)
    {
        memcpy(out + pos, extraObjects, extraObjectsLen);
        pos += extraObjectsLen;
    }
    vm_net_mock_finish_wt_packet(out, pos, extraObjectCount);
    printf("[info][mock-service] team_battle_round_defer battle=%u round=%u "
           "source=%08x actor=%u acted=%02x alive=%02x ack_objects=%u resp=%u\n",
           context && context->team ? context->team->battleSerial : 0,
           context && context->team ? context->team->battleRoundSerial : 0,
           context && context->session ? context->session->clientId : 0,
           context ? context->memberIndex : 0,
           context && context->team ? context->team->battleRoundActedMask : 0,
           context ? context->aliveMask : 0,
           extraObjectCount,
           pos);
    return pos;
}

static u32 vm_net_mock_merge_team_battle_round_response(
    u8 *out,
    u32 outCap,
    const u8 *currentResponse,
    u32 currentResponseLen,
    const vm_mock_service_team_battle_operation_context *context)
{
    vm_mock_service_team *team = context ? context->team : NULL;
    u8 combinedActionInfo[
        VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX *
        VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u8 extraObjects[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX * 2];
    const u8 *currentActionInfo = NULL;
    const u8 *currentTeamInfo = NULL;
    u16 currentActionInfoLen = 0;
    u16 currentTeamInfoLen = 0;
    u8 currentActionCount = 0;
    u32 combinedActionInfoLen = 0;
    u32 totalActionCount = 0;
    u32 extraObjectsLen = 0;
    u8 teamInfo[VM_MOCK_SERVICE_TEAM_MEMBER_MAX * 14];
    u32 teamInfoLen = 0;
    u8 extraObjectCount = 0;
    u8 pendingCount = 0;
    u8 responseObjectCount = 0;
    u32 lastSerial = 0;
    bool includesTeamInfo = false;
    bool terminalStatusAppended = false;
    u32 pos = 5;

    if (out == NULL || outCap < pos || team == NULL ||
        !vm_net_mock_get_object_u8_field(currentResponse, currentResponseLen,
                                         "actionnum", &currentActionCount) ||
        currentActionCount == 0 ||
        !vm_net_mock_get_response_object_entry_field(
            currentResponse, currentResponseLen, 4, 6,
            "actioninfo", &currentActionInfo, &currentActionInfoLen) ||
        currentActionInfo == NULL || currentActionInfoLen == 0 ||
        !vm_net_mock_copy_non_battle_action_objects(
            currentResponse, currentResponseLen,
            extraObjects, sizeof(extraObjects),
            &extraObjectsLen, &extraObjectCount))
    {
        return 0;
    }
    includesTeamInfo = vm_net_mock_get_battle_teaminfo_raw_field(
        currentResponse, currentResponseLen,
        &currentTeamInfo, &currentTeamInfoLen);

    for (;;)
    {
        vm_mock_service_team_battle_round_action *next = NULL;

        for (u8 i = 0; i < team->battleMemberCount; ++i)
        {
            vm_mock_service_team_battle_round_action *candidate =
                &team->battleRoundActions[i];

            if (!candidate->valid || candidate->serial <= lastSerial)
                continue;
            if (next == NULL || candidate->serial < next->serial)
                next = candidate;
        }
        if (next == NULL)
            break;
        if (combinedActionInfoLen + next->actionInfoLen >
                sizeof(combinedActionInfo) ||
            totalActionCount + next->actionCount > 0xff)
        {
            return 0;
        }
        memcpy(combinedActionInfo + combinedActionInfoLen,
               next->actionInfo, next->actionInfoLen);
        combinedActionInfoLen += next->actionInfoLen;
        totalActionCount += next->actionCount;
        includesTeamInfo = includesTeamInfo || next->includesTeamInfo;
        lastSerial = next->serial;
        ++pendingCount;
    }
    if (combinedActionInfoLen + currentActionInfoLen > sizeof(combinedActionInfo) ||
        totalActionCount + currentActionCount > 0xff)
    {
        return 0;
    }
    memcpy(combinedActionInfo + combinedActionInfoLen,
           currentActionInfo, currentActionInfoLen);
    combinedActionInfoLen += currentActionInfoLen;
    totalActionCount += currentActionCount;

    if (includesTeamInfo &&
        !vm_net_mock_build_team_battle_teaminfo_blob(
            teamInfo, sizeof(teamInfo), &teamInfoLen, context))
    {
        return 0;
    }
    if ((includesTeamInfo &&
         !vm_net_mock_append_battle_action6_object_with_teaminfo_blob(
             out, outCap, &pos, combinedActionInfo, combinedActionInfoLen,
             (u8)totalActionCount, teamInfo, teamInfoLen)) ||
        (!includesTeamInfo &&
         !vm_net_mock_append_battle_action6_object(
             out, outCap, &pos, combinedActionInfo, combinedActionInfoLen,
             (u8)totalActionCount)))
    {
        return 0;
    }
    responseObjectCount = 1;
    /* Generic action builders must not settle an individual party member:
     * this is the first point where the complete round is visible to the
     * requester, so this is also the only ordinary-round owner of 4/7. */
    if (team->battleEnemyHpCurrent == 0)
    {
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &responseObjectCount, true,
                &terminalStatusAppended))
        {
            return 0;
        }
        if (terminalStatusAppended)
            g_vm_net_mock_battle_settlement_sent_serial =
                g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(
                out, outCap, &pos, &responseObjectCount,
                "team-battle-round-release", true))
        {
            return 0;
        }
    }
    if (pos + extraObjectsLen > outCap ||
        (u32)responseObjectCount + extraObjectCount > 0xff)
    {
        return 0;
    }
    if (extraObjectsLen != 0)
    {
        memcpy(out + pos, extraObjects, extraObjectsLen);
        pos += extraObjectsLen;
    }
    responseObjectCount = (u8)(responseObjectCount + extraObjectCount);
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
    if (team->battleEnemyHpCurrent == 0)
    {
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = 1;
        if (!terminalStatusAppended)
        {
            vm_net_mock_battle_save_terminal_role_state(
                "team-battle-round-release-no-result", true);
        }
    }
    printf("[info][mock-service] team_battle_round_release battle=%u round=%u "
           "source=%08x actor=%u pending=%u actions=%u info=%u teaminfo=%u "
           "terminal=%u extras=%u resp=%u\n",
           team->battleSerial,
           team->battleRoundSerial,
           context && context->session ? context->session->clientId : 0,
           context ? context->memberIndex : 0,
           pendingCount,
           (u8)totalActionCount,
           combinedActionInfoLen,
           teamInfoLen,
           terminalStatusAppended ? 1 : 0,
           extraObjectCount,
           pos);
    return pos;
}

static u32 vm_net_mock_build_team_battle_terminal_release_response(
    u8 *out,
    u32 outCap,
    const vm_mock_service_team_battle_operation_context *context)
{
    vm_mock_service_team *team = context ? context->team : NULL;
    u8 combinedActionInfo[
        VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX *
        VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u8 teamInfo[VM_MOCK_SERVICE_TEAM_MEMBER_MAX * 14];
    u32 combinedActionInfoLen = 0;
    u32 teamInfoLen = 0;
    u32 totalActionCount = 0;
    u32 lastSerial = 0;
    u8 pendingCount = 0;
    u8 objectCount = 0;
    bool includesTeamInfo = false;
    bool terminalStatusAppended = false;
    u32 pos = 5;

    if (out == NULL || outCap < pos || team == NULL ||
        !team->battleRoundTerminalPending || team->battleEnemyHpCurrent != 0)
    {
        return 0;
    }

    for (;;)
    {
        vm_mock_service_team_battle_round_action *next = NULL;

        for (u8 i = 0; i < team->battleMemberCount; ++i)
        {
            vm_mock_service_team_battle_round_action *candidate =
                &team->battleRoundActions[i];

            if (!candidate->valid || candidate->serial <= lastSerial)
                continue;
            if (next == NULL || candidate->serial < next->serial)
                next = candidate;
        }
        if (next == NULL)
            break;
        if (combinedActionInfoLen + next->actionInfoLen >
                sizeof(combinedActionInfo) ||
            totalActionCount + next->actionCount > 0xff)
        {
            return 0;
        }
        memcpy(combinedActionInfo + combinedActionInfoLen,
               next->actionInfo, next->actionInfoLen);
        combinedActionInfoLen += next->actionInfoLen;
        totalActionCount += next->actionCount;
        includesTeamInfo = includesTeamInfo || next->includesTeamInfo;
        lastSerial = next->serial;
        ++pendingCount;
    }
    if (pendingCount == 0 || totalActionCount == 0 ||
        (includesTeamInfo &&
         !vm_net_mock_build_team_battle_teaminfo_blob(
             teamInfo, sizeof(teamInfo), &teamInfoLen, context)) ||
        (includesTeamInfo &&
         !vm_net_mock_append_battle_action6_object_with_teaminfo_blob(
             out, outCap, &pos, combinedActionInfo, combinedActionInfoLen,
             (u8)totalActionCount, teamInfo, teamInfoLen)) ||
        (!includesTeamInfo &&
         !vm_net_mock_append_battle_action6_object(
             out, outCap, &pos, combinedActionInfo, combinedActionInfoLen,
             (u8)totalActionCount)))
    {
        return 0;
    }
    ++objectCount;
    if (!vm_net_mock_append_battle_terminal_status_objects(
            out, outCap, &pos, &objectCount, true,
            &terminalStatusAppended))
        return 0;
    if (terminalStatusAppended)
        g_vm_net_mock_battle_settlement_sent_serial =
            g_mockBattleOperateSessionSerial;
    if (!vm_net_mock_append_battle_drop_refresh7_if_needed(
            out, outCap, &pos, &objectCount,
            "team-battle-terminal-release", true))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    g_mockBattleOperateSessionArmed = 0;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 1;
    if (!terminalStatusAppended)
    {
        vm_net_mock_battle_save_terminal_role_state(
            "team-battle-terminal-release-no-result", true);
    }
    printf("[info][mock-service] team_battle_round_terminal_release "
           "battle=%u round=%u source=%08x actor=%u pending=%u "
           "actions=%u info=%u teaminfo=%u objects=%u resp=%u\n",
           team->battleSerial,
           team->battleRoundSerial,
           context && context->session ? context->session->clientId : 0,
           context ? context->memberIndex : 0,
           pendingCount,
           (u8)totalActionCount,
           combinedActionInfoLen,
           teamInfoLen,
           objectCount,
           pos);
    return pos;
}

static vm_mock_service_team_battle_operation_context
vm_mock_service_team_battle_prepare_operation(void)
{
    vm_mock_service_team_battle_operation_context context;
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = session ?
        vm_mock_service_team_find_for_client(session->clientId) : NULL;
    int memberIndex = vm_mock_service_team_battle_member_index(
        team, session ? session->clientId : 0);

    memset(&context, 0, sizeof(context));
    if (session == NULL || team == NULL || !team->battleActive ||
        memberIndex < 0 || memberIndex >= team->battleMemberCount)
    {
        return context;
    }

    context.active = true;
    context.team = team;
    context.session = session;
    context.memberIndex = (u8)memberIndex;
    context.memberBit = (u8)(1u << memberIndex);
    context.aliveMask = vm_mock_service_team_battle_alive_mask(team);
    context.duplicateAction = (team->battleRoundActedMask & context.memberBit) != 0;
    context.resolvesRound = !context.duplicateAction &&
                            (context.aliveMask & context.memberBit) != 0 &&
                            (u8)(team->battleRoundActedMask | context.memberBit) == context.aliveMask;
    g_vm_net_mock_team_battle_party_count_current = team->battleMemberCount;
    g_vm_net_mock_team_battle_actor_slot_current = (u8)memberIndex;
    g_vm_net_mock_team_battle_resolve_monsters_current = context.resolvesRound ? 1 : 0;
    g_vm_net_mock_team_battle_member_count_current = team->battleMemberCount;
    memcpy(g_vm_net_mock_team_battle_member_hp_current, team->battleMemberHp,
           sizeof(g_vm_net_mock_team_battle_member_hp_current));
    memcpy(g_vm_net_mock_team_battle_member_hp_max_current, team->battleMemberHpMax,
           sizeof(g_vm_net_mock_team_battle_member_hp_max_current));
    g_vm_net_mock_team_battle_group_hp_changed_mask = 0;
    memcpy(g_vm_net_mock_team_battle_member_modifiers_current,
           team->battleMemberModifiers,
           sizeof(g_vm_net_mock_team_battle_member_modifiers_current));
    memcpy(g_vm_net_mock_battle_enemy_effects_current,
           team->battleEnemyEffects,
           sizeof(g_vm_net_mock_battle_enemy_effects_current));
    g_vm_net_mock_battle_enemy_effect_changed_mask = 0;
    g_vm_net_mock_team_battle_group_modifier_changed_mask = 0;
    g_vm_net_mock_battle_active_modifier_current =
        g_vm_net_mock_team_battle_member_modifiers_current[memberIndex];
    g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleEnemyCountCurrent = team->battleMonsterCount;
    g_mockBattleOperateTurnCounter = team->battleTurnCounter;
    g_vm_net_mock_battle_enemy_id_current = team->battleEnemyId;
    memcpy(g_mockBattleEnemyHpSlots, team->battleEnemyHpSlots,
           sizeof(g_mockBattleEnemyHpSlots));
    memcpy(g_mockBattleEnemyHpMaxSlots, team->battleEnemyHpMaxSlots,
           sizeof(g_mockBattleEnemyHpMaxSlots));
    g_mockBattleEnemyHpCurrent = team->battleEnemyHpCurrent;
    g_mockBattleEnemyHpMax = team->battleEnemyHpMax;
    g_mockBattleRoleHpCurrent = team->battleMemberHp[memberIndex];
    g_mockBattleRoleHpMax = team->battleMemberHpMax[memberIndex];
    g_mockBattleRoleMpCurrent = team->battleMemberMp[memberIndex];
    g_mockBattleRoleMpMax = team->battleMemberMpMax[memberIndex];
    /* A non-final team action must never arm the solo builder's deferred
     * enemy-turn fallback in this account's restored battle state. */
    g_mockBattlePendingEnemyTurn = 0;
    printf("[info][mock-service] team_battle_round_prepare battle=%u round=%u "
           "source=%08x actor=%u acted=%02x alive=%02x duplicate=%u resolve=%u\n",
           team->battleSerial,
           team->battleRoundSerial,
           session->clientId,
           context.memberIndex,
           team->battleRoundActedMask,
           context.aliveMask,
           context.duplicateAction ? 1 : 0,
           context.resolvesRound ? 1 : 0);
    return context;
}

static void vm_mock_service_team_battle_clear_operation_context(void)
{
    g_vm_net_mock_team_battle_party_count_current = 0;
    g_vm_net_mock_team_battle_actor_slot_current = 0;
    g_vm_net_mock_team_battle_resolve_monsters_current = 0;
    g_vm_net_mock_team_battle_member_count_current = 0;
    memset(g_vm_net_mock_team_battle_member_hp_current, 0,
           sizeof(g_vm_net_mock_team_battle_member_hp_current));
    memset(g_vm_net_mock_team_battle_member_hp_max_current, 0,
           sizeof(g_vm_net_mock_team_battle_member_hp_max_current));
    g_vm_net_mock_team_battle_group_hp_changed_mask = 0;
    memset(g_vm_net_mock_team_battle_member_modifiers_current, 0,
           sizeof(g_vm_net_mock_team_battle_member_modifiers_current));
    memset(g_vm_net_mock_battle_enemy_effects_current, 0,
           sizeof(g_vm_net_mock_battle_enemy_effects_current));
    g_vm_net_mock_battle_enemy_effect_changed_mask = 0;
    memset(&g_vm_net_mock_battle_active_enemy_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_enemy_modifier_current));
    g_vm_net_mock_team_battle_group_modifier_changed_mask = 0;
}

static void vm_mock_service_team_battle_queue_action(
    vm_mock_service_team_battle_operation_context *context,
    const u8 *response,
    u32 responseLen)
{
    vm_mock_service_team *team = context ? context->team : NULL;
    vm_mock_service_team_battle_event *event = NULL;
    u16 actionObjectLen = 0;
    u32 nextSerial = 0;
    u32 slot = 0;
    u8 fullMask = 0;
    u8 actionObject[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX];

    if (context == NULL || !context->active || team == NULL ||
        response == NULL || responseLen == 0)
    {
        return;
    }
    actionObjectLen = vm_net_mock_copy_response_object(
        response, responseLen, 4, 6,
        actionObject, sizeof(actionObject));
    if (actionObjectLen == 0)
        return;
    ++team->battleActionSerial;
    if (team->battleActionSerial == 0)
        team->battleActionSerial = 1;
    nextSerial = team->battleActionSerial;
    slot = (nextSerial - 1) % VM_MOCK_SERVICE_TEAM_BATTLE_EVENT_MAX;
    event = &team->battleEvents[slot];
    fullMask = (u8)((1u << team->battleMemberCount) - 1u);
    if (event->valid && event->deliveredMask != fullMask)
    {
        printf("[warn][mock-service] team_battle_action_overwrite old=%u "
               "delivered=%02x expected=%02x\n",
               event->serial, event->deliveredMask, fullMask);
    }
    memset(event, 0, sizeof(*event));
    memcpy(event->objectData, actionObject, actionObjectLen);
    event->valid = true;
    event->terminalVictory = team->battleEnemyHpCurrent == 0;
    event->serial = nextSerial;
    event->sourceClientId = context->session->clientId;
    event->deliveredMask = (u8)(1u << context->memberIndex);
    event->objectLen = actionObjectLen;
    printf("[info][mock-service] team_battle_action_queue battle=%u action=%u "
           "source=%08x actor=%u enemyhp=%u/%u terminal=%u object=%u "
           "delivered=%02x\n",
           team->battleSerial,
           event->serial,
           event->sourceClientId,
           context->memberIndex,
           team->battleEnemyHpCurrent,
           team->battleEnemyHpMax,
           event->terminalVictory ? 1 : 0,
           event->objectLen,
           event->deliveredMask);
}

static void vm_mock_service_team_battle_finish_operation(
    vm_mock_service_team_battle_operation_context *context,
    const u8 *response,
    u32 responseLen,
    bool publishAction)
{
    vm_mock_service_team *team = context ? context->team : NULL;
    u8 actionCount = 0;
    bool actionAccepted = false;
    bool vitalsChanged = false;
    u8 hspSourceCount = 0;

    if (context == NULL || !context->active || team == NULL)
    {
        vm_mock_service_team_battle_clear_operation_context();
        return;
    }
    actionAccepted = response != NULL && responseLen != 0 &&
                     vm_net_mock_get_object_u8_field(response, responseLen,
                                                     "actionnum", &actionCount) &&
                     actionCount != 0;
    memcpy(team->battleEnemyHpSlots, g_mockBattleEnemyHpSlots,
           sizeof(team->battleEnemyHpSlots));
    memcpy(team->battleEnemyHpMaxSlots, g_mockBattleEnemyHpMaxSlots,
           sizeof(team->battleEnemyHpMaxSlots));
    team->battleEnemyHpCurrent = g_mockBattleEnemyHpCurrent;
    team->battleEnemyHpMax = g_mockBattleEnemyHpMax;
    if (actionAccepted)
    {
        memcpy(team->battleEnemyEffects, g_vm_net_mock_battle_enemy_effects_current,
               sizeof(team->battleEnemyEffects));
    }
    team->battleTurnCounter = g_mockBattleOperateTurnCounter;
    if (actionAccepted && g_vm_net_mock_team_battle_group_hp_changed_mask != 0)
    {
        for (u8 member = 0; member < team->battleMemberCount && member < 3; ++member)
        {
            if ((g_vm_net_mock_team_battle_group_hp_changed_mask & (u8)(1u << member)) == 0)
                continue;
            team->battleMemberHp[member] =
                g_vm_net_mock_team_battle_member_hp_current[member];
            team->battleMemberHpMax[member] =
                g_vm_net_mock_team_battle_member_hp_max_current[member];
        }
    }
    /* Timed group stat effects have no HSP delta.  Commit their independent
     * shared rows here so the next teammate evaluates the same battle state. */
    if (actionAccepted && g_vm_net_mock_team_battle_group_modifier_changed_mask != 0)
    {
        for (u8 member = 0; member < team->battleMemberCount && member < 3; ++member)
        {
            if ((g_vm_net_mock_team_battle_group_modifier_changed_mask &
                 (u8)(1u << member)) == 0)
            {
                continue;
            }
            team->battleMemberModifiers[member] =
                g_vm_net_mock_team_battle_member_modifiers_current[member];
        }
    }
    team->battleMemberHp[context->memberIndex] = g_mockBattleRoleHpCurrent;
    team->battleMemberHpMax[context->memberIndex] = g_mockBattleRoleHpMax;
    team->battleMemberMp[context->memberIndex] = g_mockBattleRoleMpCurrent;
    team->battleMemberMpMax[context->memberIndex] = g_mockBattleRoleMpMax;
    g_mockBattlePendingEnemyTurn = 0;
    if (actionAccepted)
    {
        team->battleRoundActedMask = (u8)(team->battleRoundActedMask | context->memberBit);
        if (publishAction)
        {
            /* The release action closes a complete party round.  Age every
             * pre-existing buff once, but not one cast by that same last
             * action: a fresh duration must survive until a later round. */
            for (u8 member = 0; member < team->battleMemberCount && member < 3; ++member)
            {
                if ((g_vm_net_mock_team_battle_group_modifier_changed_mask &
                     (u8)(1u << member)) != 0)
                {
                    continue;
                }
                vm_net_mock_battle_modifier_advance_round(
                    &team->battleMemberModifiers[member]);
            }
            vm_net_mock_battle_advance_enemy_effects_current();
            memcpy(team->battleEnemyEffects, g_vm_net_mock_battle_enemy_effects_current,
                   sizeof(team->battleEnemyEffects));
            team->battleRoundActedMask = 0;
            ++team->battleRoundSerial;
            if (team->battleRoundSerial == 0)
                team->battleRoundSerial = 1;
        }
    }
    if (publishAction && team->battleEnemyHpCurrent == 0)
        team->battleFinished = true;
    if (actionAccepted && publishAction)
        vm_mock_service_team_battle_queue_action(context, response, responseLen);
    if (publishAction)
        vm_mock_service_team_battle_clear_round_actions(team);

    /* Publish the shared battle snapshot straight into the service presence
     * before the next poll.  In particular HP=0 is a real value here, not an
     * absent/default value; the resulting subtype 5/11 update keeps every
     * party HUD in lockstep with the death action in 4/6. */
    for (u8 member = 0; member < team->battleMemberCount; ++member)
    {
        vm_mock_service_client_session *memberSession =
            vm_mock_service_find_client_session(team->battleMemberClientIds[member]);

        if (memberSession == NULL)
            continue;
        vitalsChanged = memberSession->onlineHp != team->battleMemberHp[member] ||
                        memberSession->onlineHpMax != team->battleMemberHpMax[member] ||
                        memberSession->onlineMp != team->battleMemberMp[member] ||
                        memberSession->onlineMpMax != team->battleMemberMpMax[member];
        memberSession->onlineHp = team->battleMemberHp[member];
        memberSession->onlineHpMax = team->battleMemberHpMax[member];
        memberSession->onlineMp = team->battleMemberMp[member];
        memberSession->onlineMpMax = team->battleMemberMpMax[member];
        if (vitalsChanged && memberSession->roleOnline)
        {
            vm_mock_service_team_enqueue_hsp_for_members(memberSession);
            ++hspSourceCount;
        }
    }
    printf("[info][mock-service] team_battle_state battle=%u source=%08x "
           "actor=%u turn=%u enemyhp=%u/%u slots=%u/%u/%u "
           "rolehp=%u/%u rolemp=%u/%u buff_str=%d buff_rounds=%u "
           "buffmask=%02x round=%u acted=%02x alive=%02x "
           "resolve=%u accepted=%u release=%u hsp=%u finished=%u\n",
           team->battleSerial,
           context->session->clientId,
           context->memberIndex,
           team->battleTurnCounter,
           team->battleEnemyHpCurrent,
           team->battleEnemyHpMax,
           team->battleEnemyHpSlots[0],
           team->battleEnemyHpSlots[1],
           team->battleEnemyHpSlots[2],
           team->battleMemberHp[context->memberIndex],
           team->battleMemberHpMax[context->memberIndex],
           team->battleMemberMp[context->memberIndex],
           team->battleMemberMpMax[context->memberIndex],
           team->battleMemberModifiers[context->memberIndex].strength,
           team->battleMemberModifiers[context->memberIndex].remainingRounds,
           g_vm_net_mock_team_battle_group_modifier_changed_mask,
           team->battleRoundSerial,
           team->battleRoundActedMask,
           vm_mock_service_team_battle_alive_mask(team),
           context->resolvesRound ? 1 : 0,
           actionAccepted ? 1 : 0,
           publishAction ? 1 : 0,
           hspSourceCount,
           team->battleFinished ? 1 : 0);
    vm_mock_service_team_battle_clear_operation_context();
}

static u32 vm_net_mock_build_synchronized_team_battle_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap,
    u8 buildType)
{
    vm_mock_service_team_battle_operation_context context;
    u32 responseLen = 0;
    u32 mergedResponseLen = 0;
    u32 deferredAckLen = 0;
    u8 actionCount = 0;
    bool actionAccepted = false;
    bool releaseRound = false;

    memset(&context, 0, sizeof(context));
    if (buildType == VM_MOCK_TEAM_BATTLE_BUILD_ITEM)
    {
        if (!vm_net_mock_parse_battle_item_use_request(request, requestLen, NULL))
            return 0;
    }
    else if (buildType == VM_MOCK_TEAM_BATTLE_BUILD_OPERATE_FALLBACK)
    {
        if (!vm_net_mock_is_battle_operate_request_relaxed(request, requestLen))
            return 0;
    }
    else if (!vm_net_mock_is_battle_operate_request(request, requestLen))
    {
        return 0;
    }
    context = vm_mock_service_team_battle_prepare_operation();
    if (context.active && context.team->battleFinished)
    {
        responseLen = vm_net_mock_build_pending_team_battle_action_response(
            out, outCap, context.session);
        vm_mock_service_team_battle_clear_operation_context();
        if (responseLen != 0)
            return responseLen;
        return vm_net_mock_build_battle_case11_auto_off_response(out, outCap);
    }
    if (context.active &&
        (context.duplicateAction || (context.aliveMask & context.memberBit) == 0))
    {
        responseLen = vm_net_mock_build_team_battle_round_wait_response(
            out, outCap, &context,
            context.duplicateAction ? "already-acted" : "member-dead");
        vm_mock_service_team_battle_clear_operation_context();
        return responseLen;
    }
    if (context.active && context.team->battleRoundTerminalPending)
    {
        if (context.resolvesRound)
        {
            responseLen = vm_net_mock_build_team_battle_terminal_release_response(
                out, outCap, &context);
            if (responseLen == 0)
            {
                printf("[error][mock-service] team_battle_round_terminal_release_failed "
                       "battle=%u round=%u source=%08x actor=%u acted=%02x alive=%02x\n",
                       context.team->battleSerial,
                       context.team->battleRoundSerial,
                       context.session ? context.session->clientId : 0,
                       context.memberIndex,
                       context.team->battleRoundActedMask,
                       context.aliveMask);
                vm_mock_service_team_battle_clear_operation_context();
                return 0;
            }
            vm_mock_service_team_battle_finish_operation(
                &context, out, responseLen, true);
            return responseLen;
        }

        /* The monsters are already dead in the shared snapshot, but the
         * killing action has not been exposed to any client yet.  Count this
         * living member's submitted choice toward the frozen round without
         * manufacturing an attack against a dead target. */
        context.team->battleRoundActedMask = (u8)(
            context.team->battleRoundActedMask | context.memberBit);
        printf("[info][mock-service] team_battle_round_terminal_wait "
               "battle=%u round=%u source=%08x actor=%u acted=%02x alive=%02x\n",
               context.team->battleSerial,
               context.team->battleRoundSerial,
               context.session ? context.session->clientId : 0,
               context.memberIndex,
               context.team->battleRoundActedMask,
               context.aliveMask);
        responseLen = vm_net_mock_build_team_battle_round_wait_response(
            out, outCap, &context, "terminal-pending");
        vm_mock_service_team_battle_clear_operation_context();
        return responseLen;
    }
    if (buildType == VM_MOCK_TEAM_BATTLE_BUILD_ITEM)
        responseLen = vm_net_mock_build_battle_item_use_response(request, requestLen, out, outCap);
    else if (buildType == VM_MOCK_TEAM_BATTLE_BUILD_OPERATE_FALLBACK)
        responseLen = vm_net_mock_build_battle_operate_response_fallback(request, requestLen,
                                                                         out, outCap);
    else
        responseLen = vm_net_mock_build_battle_operate_response(request, requestLen, out, outCap);

    if (context.active)
    {
        if (responseLen != 0)
        {
            actionAccepted = vm_net_mock_get_object_u8_field(
                                 out, responseLen, "actionnum", &actionCount) &&
                             actionCount != 0;
            releaseRound = actionAccepted && context.resolvesRound;
            if (actionAccepted && !releaseRound &&
                g_mockBattleEnemyHpCurrent == 0)
            {
                context.team->battleRoundTerminalPending = true;
                printf("[info][mock-service] team_battle_round_terminal_capture "
                       "battle=%u round=%u source=%08x actor=%u acted=%02x alive=%02x\n",
                       context.team->battleSerial,
                       context.team->battleRoundSerial,
                       context.session ? context.session->clientId : 0,
                       context.memberIndex,
                       context.team->battleRoundActedMask,
                       context.aliveMask);
            }
            if (actionAccepted && !releaseRound &&
                !vm_mock_service_team_battle_capture_round_action(
                    &context, out, responseLen))
            {
                /* Never strand a live battle behind a server-side capture
                 * failure.  Publishing the one action is less faithful, but
                 * preserves a recoverable client state and leaves a loud log. */
                printf("[error][mock-service] team_battle_round_capture_failed "
                       "battle=%u round=%u source=%08x actor=%u actionnum=%u\n",
                       context.team->battleSerial,
                       context.team->battleRoundSerial,
                       context.session ? context.session->clientId : 0,
                       context.memberIndex,
                       actionCount);
                releaseRound = true;
            }
            if (releaseRound)
            {
                mergedResponseLen = vm_net_mock_merge_team_battle_round_response(
                    out, outCap, out, responseLen, &context);
                if (mergedResponseLen == 0)
                {
                    printf("[error][mock-service] team_battle_round_merge_failed "
                           "battle=%u round=%u source=%08x actor=%u resp=%u\n",
                           context.team->battleSerial,
                           context.team->battleRoundSerial,
                           context.session ? context.session->clientId : 0,
                           context.memberIndex,
                           responseLen);
                    mergedResponseLen = responseLen;
                }
                responseLen = mergedResponseLen;
                vm_mock_service_team_battle_finish_operation(
                    &context, out, responseLen, true);
            }
            else if (actionAccepted)
            {
                /* Commit the server-side player action now, but do not expose
                 * its 4/6 object.  HandleBattleActionMsg consumes each 4/6 as
                 * one local action list, so exposing it early makes that client
                 * enter the enemy phase before its peers have submitted. */
                vm_mock_service_team_battle_finish_operation(
                    &context, out, responseLen, false);
                deferredAckLen = vm_net_mock_build_team_battle_deferred_ack(
                    out, outCap, out, responseLen, &context);
                if (deferredAckLen == 0)
                    deferredAckLen = responseLen;
                responseLen = deferredAckLen;
            }
            else
            {
                vm_mock_service_team_battle_finish_operation(
                    &context, out, responseLen, false);
            }
        }
        else
            vm_mock_service_team_battle_clear_operation_context();
    }
    return responseLen;
}

typedef struct
{
    u16 seq;
    u32 remaining;
} vm_net_mock_battle_flask_count_update;

typedef struct
{
    vm_net_mock_battle_flask_count_update updates[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
    u8 updateCount;
    u32 hpRestored;
    u32 mpRestored;
} vm_net_mock_battle_auto_flask_result;

static void vm_net_mock_battle_auto_use_vitality_flasks(
    vm_net_mock_battle_auto_flask_result *result)
{
    static const u32 flaskItemIds[] = {802, 803};
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHp = 0;
    u32 roleHpMax = 0;
    u32 roleMp = 0;
    u32 roleMpMax = 0;

    if (result == NULL)
        return;
    memset(result, 0, sizeof(*result));

    /* A defeated role is handled by the normal death/revive flow.  A flask
     * is a recovery reservoir, not an implicit resurrection item. */
    if (role == NULL || g_mockBattleOperateSessionSerial == 0 ||
        g_mockBattleRoleHpCurrent == 0)
    {
        return;
    }

    vm_net_mock_role_sync_derived_vitals(role);
    roleHpMax = g_mockBattleRoleHpMax != 0 ? g_mockBattleRoleHpMax : role->hpMax;
    roleMpMax = g_mockBattleRoleMpMax != 0 ? g_mockBattleRoleMpMax : role->mpMax;
    if (roleHpMax == 0)
        roleHpMax = VM_NET_MOCK_ROLE_DEFAULT_HP;
    if (roleMpMax == 0)
        roleMpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;
    roleHp = vm_net_mock_min_u32(g_mockBattleRoleHpCurrent, roleHpMax);
    roleMp = vm_net_mock_min_u32(g_mockBattleRoleMpMax != 0 ?
                                      g_mockBattleRoleMpCurrent : role->mp,
                                  roleMpMax);

    for (u32 typeIndex = 0; typeIndex < sizeof(flaskItemIds) / sizeof(flaskItemIds[0]);
         ++typeIndex)
    {
        for (;;)
        {
            vm_net_mock_backpack_item_state *item = NULL;
            const vm_net_mock_item_effect_catalog_item *effect = NULL;
            u32 missingHp = roleHpMax > roleHp ? roleHpMax - roleHp : 0;
            u32 missingMp = roleMpMax > roleMp ? roleMpMax - roleMp : 0;
            u32 plannedHp = 0;
            u32 plannedMp = 0;
            u32 consumed = 0;
            u32 remaining = 0;
            u16 seq = 0;

            if (missingHp == 0 && missingMp == 0 ||
                result->updateCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
            {
                break;
            }
            item = vm_net_mock_role_find_backpack_item(role, flaskItemIds[typeIndex], 0);
            if (item == NULL)
                break;
            effect = vm_net_mock_find_item_effect_catalog_item(item->itemId);
            if (!vm_net_mock_item_effect_is_reservoir(effect))
                break;

            seq = item->seq;
            consumed = vm_net_mock_item_effect_plan_reservoir_restore(
                effect, item->count, missingHp, missingMp, &plannedHp, &plannedMp);
            if (consumed == 0 ||
                !vm_net_mock_role_consume_backpack_item(role, item->itemId, seq,
                                                        consumed, &remaining))
            {
                break;
            }

            roleHp = vm_net_mock_min_u32(
                vm_net_mock_add_capped_u32(roleHp, plannedHp), roleHpMax);
            roleMp = vm_net_mock_min_u32(
                vm_net_mock_add_capped_u32(roleMp, plannedMp), roleMpMax);
            result->hpRestored = vm_net_mock_add_capped_u32(result->hpRestored, plannedHp);
            result->mpRestored = vm_net_mock_add_capped_u32(result->mpRestored, plannedMp);
            result->updates[result->updateCount].seq = seq;
            result->updates[result->updateCount].remaining = remaining;
            ++result->updateCount;
        }
    }

    if (result->updateCount != 0)
    {
        g_mockBattleRoleHpMax = roleHpMax;
        g_mockBattleRoleHpCurrent = roleHp;
        g_mockBattleRoleMpMax = roleMpMax;
        g_mockBattleRoleMpCurrent = roleMp;
        role->hp = roleHp;
        role->mp = roleMp;
    }
}

/*
 * Build-time projection for the terminal 4/6.  The packet order is 4/6 then
 * 4/7, but the client keeps teaminfo in its battle-actor cache while 4/7 only
 * applies deltas to the main actor.  Projecting with a private role copy lets
 * the earlier 4/6 name the exact post-settlement values without consuming an
 * item twice or mutating any live server state before 4/7 owns settlement.
 */
static void vm_net_mock_battle_project_terminal_vitals(
    u32 currentHp, u32 currentHpMax, u32 currentMp, u32 currentMpMax,
    u32 *finalHpOut, u32 *finalMpOut)
{
    static const u32 flaskItemIds[] = {802, 803};
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_role_state projected;
    u32 roleHp = currentHp;
    u32 roleMp = currentMp;
    u32 roleHpMax = currentHpMax;
    u32 roleMpMax = currentMpMax;
    u8 updateCount = 0;

    if (finalHpOut)
        *finalHpOut = currentHp;
    if (finalMpOut)
        *finalMpOut = currentMp;
    if (role == NULL || g_mockBattleOperateSessionSerial == 0 || currentHp == 0)
        return;

    projected = *role;
    vm_net_mock_role_sync_derived_vitals(&projected);
    if (roleHpMax == 0)
        roleHpMax = projected.hpMax;
    if (roleMpMax == 0)
        roleMpMax = projected.mpMax;
    if (roleHpMax == 0)
        roleHpMax = VM_NET_MOCK_ROLE_DEFAULT_HP;
    if (roleMpMax == 0)
        roleMpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;
    roleHp = vm_net_mock_min_u32(roleHp, roleHpMax);
    roleMp = vm_net_mock_min_u32(roleMp, roleMpMax);

    for (u32 typeIndex = 0;
         typeIndex < sizeof(flaskItemIds) / sizeof(flaskItemIds[0]); ++typeIndex)
    {
        for (;;)
        {
            vm_net_mock_backpack_item_state *item = NULL;
            const vm_net_mock_item_effect_catalog_item *effect = NULL;
            u32 missingHp = roleHpMax > roleHp ? roleHpMax - roleHp : 0;
            u32 missingMp = roleMpMax > roleMp ? roleMpMax - roleMp : 0;
            u32 plannedHp = 0;
            u32 plannedMp = 0;
            u32 consumed = 0;

            if ((missingHp == 0 && missingMp == 0) ||
                updateCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
            {
                break;
            }
            item = vm_net_mock_role_find_backpack_item(&projected,
                                                        flaskItemIds[typeIndex], 0);
            if (item == NULL)
                break;
            effect = vm_net_mock_find_item_effect_catalog_item(item->itemId);
            if (!vm_net_mock_item_effect_is_reservoir(effect))
                break;

            consumed = vm_net_mock_item_effect_plan_reservoir_restore(
                effect, item->count, missingHp, missingMp, &plannedHp, &plannedMp);
            if (consumed == 0 ||
                !vm_net_mock_role_consume_backpack_item(&projected, item->itemId,
                                                        item->seq, consumed, NULL))
            {
                break;
            }
            roleHp = vm_net_mock_min_u32(
                vm_net_mock_add_capped_u32(roleHp, plannedHp), roleHpMax);
            roleMp = vm_net_mock_min_u32(
                vm_net_mock_add_capped_u32(roleMp, plannedMp), roleMpMax);
            ++updateCount;
        }
    }

    roleHp = vm_net_mock_min_u32(
        vm_net_mock_add_capped_u32(roleHp,
                                   vm_net_mock_env_u32_if_set("CBE_BATTLE_RECOVER_HP", 0)),
        roleHpMax);
    if (g_vm_net_mock_battle_recovered_serial != g_mockBattleOperateSessionSerial)
    {
        roleMp = vm_net_mock_min_u32(
            vm_net_mock_add_capped_u32(roleMp, vm_net_mock_battle_recover_mp_value()),
            roleMpMax);
    }
    if (finalHpOut)
        *finalHpOut = roleHp;
    if (finalMpOut)
        *finalMpOut = roleMp;
}

/* `1/4/7` is the one terminal-state writer for a result that the client will
 * parse.  Its HP/MP fields are display deltas, but the role, active team
 * snapshot, and later 5/11 HSP rows must all describe the same post-recovery
 * absolute vitals.  In particular, leave no pre-flask team row behind for a
 * later scene poll to overwrite the client's native 4/7 result handling. */
static void vm_net_mock_battle_sync_terminal_team_vitals(
    const vm_net_mock_role_state *role)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = NULL;
    bool vitalsChanged = false;

    if (role == NULL || session == NULL || session->clientId == 0 ||
        session->onlineRoleId != role->roleId)
    {
        return;
    }

    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team == NULL || !team->battleActive)
        return;

    for (u8 member = 0; member < team->battleMemberCount; ++member)
    {
        if (team->battleMemberClientIds[member] != session->clientId)
            continue;

        vitalsChanged = session->onlineHp != role->hp ||
                        session->onlineHpMax != role->hpMax ||
                        session->onlineMp != role->mp ||
                        session->onlineMpMax != role->mpMax;
        team->battleMemberHp[member] = role->hp;
        team->battleMemberHpMax[member] = role->hpMax;
        team->battleMemberMp[member] = role->mp;
        team->battleMemberMpMax[member] = role->mpMax;
        session->onlineHp = role->hp;
        session->onlineHpMax = role->hpMax;
        session->onlineMp = role->mp;
        session->onlineMpMax = role->mpMax;
        if (vitalsChanged && session->roleOnline)
            vm_mock_service_team_enqueue_hsp_for_members(session);

        printf("[info][mock-service] team_battle_terminal_vitals role=%u member=%u "
               "hp=%u/%u mp=%u/%u hsp=%u source=4/7-terminal-settlement\n",
               role->roleId,
               member,
               role->hp,
               role->hpMax,
               role->mp,
               role->mpMax,
               vitalsChanged ? 1u : 0u);
        return;
    }
}

static bool vm_net_mock_append_battle_auto_flask_counts_object(
    u8 *out, u32 outCap, u32 *pos,
    const vm_net_mock_battle_auto_flask_result *result,
    bool *appendedOut)
{
    /* Each typed stream value has a two-byte tag: row_count is 3 bytes and
     * every `i16 seq + u32 remaining` pair occupies 4 + 6 bytes. */
    u8 info[3 + VM_NET_MOCK_BACKPACK_MAX_ITEMS * 10];
    u32 infoLen = 0;
    u32 objectStart = 0;

    if (appendedOut)
        *appendedOut = false;
    if (out == NULL || pos == NULL || result == NULL)
        return false;
    if (result->updateCount == 0)
        return true;
    if (!vm_net_mock_seq_put_u8(info, sizeof(info), &infoLen, result->updateCount))
        return false;
    for (u8 i = 0; i < result->updateCount; ++i)
    {
        if (!vm_net_mock_seq_put_i16(info, sizeof(info), &infoLen, result->updates[i].seq) ||
            !vm_net_mock_seq_put_u32(info, sizeof(info), &infoLen,
                                     result->updates[i].remaining))
        {
            return false;
        }
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 11, &objectStart) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "info", info, (u16)infoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (appendedOut)
        *appendedOut = true;
    return true;
}

/* BattleMenu_SelectOption(0x5F78) enables the native auto UI after a 4/11
 * acknowledgement.  The overlay owns its 1000 ms cadence and sends 4/12;
 * retain the duration only for terminal action-display ordering, where a
 * 4/6 can carry multiple queued animation records. */
enum
{
    VM_NET_MOCK_BATTLE_AUTO_ACTION_INTERVAL_TICKS = 1000 / VM_SCHED_FRAME_MS,
    /* A 4/6 arriving on the exact frame that the last queued animation
     * completes can be parsed before that completion callback clears the
     * action slot.  Deliver only on the following scheduler frame. */
    VM_NET_MOCK_BATTLE_AUTO_ACTION_BOUNDARY_GUARD_TICKS = 1,
    /* 25/5 proves that the client has left the result renderer.  Keep the
     * next scene-monster start on a later poll instead of re-entering Battle
     * from that close acknowledgement's callback stack.  The player-visible
     * hangup cadence is five seconds between completed rounds. */
    VM_NET_MOCK_SCENE_HANGUP_RESTART_DELAY_MS = 5000,
    VM_NET_MOCK_SCENE_HANGUP_RESTART_DELAY_TICKS =
        VM_NET_MOCK_SCENE_HANGUP_RESTART_DELAY_MS / VM_SCHED_FRAME_MS,
    /* item.dsh explicitly gives 战斗心得 a 200-battle continuous-hangup
     * ceiling.  It does not contain the ordinary-mode ceiling, so do not
     * invent one here: the established ordinary hangup lifecycle remains
     * unchanged while the timed effect is active. */
    VM_NET_MOCK_SCENE_HANGUP_INSIGHT_MAX_BATTLES = 200
};

static void vm_net_mock_battle_auto_reset(void)
{
    g_vm_net_mock_battle_auto_enabled = 0;
}

static void vm_net_mock_battle_auto_arm(void)
{
    g_vm_net_mock_battle_auto_enabled = 1;
}

static u32 vm_net_mock_battle_auto_action_delay_ticks(u8 actionCount)
{
    /* The native auto overlay's observable action phase is 1000 ms.  A 4/6
     * packet carries actionCount queue entries, not one atomic animation:
     * reserve one client phase for every entry, then cross the frame boundary
     * after the final completion callback.  A zero-record 4/6 remains a
     * one-phase no-op, matching the prior scheduler behavior. */
    u32 phases = actionCount != 0 ? actionCount : 1;

    return phases * VM_NET_MOCK_BATTLE_AUTO_ACTION_INTERVAL_TICKS +
           VM_NET_MOCK_BATTLE_AUTO_ACTION_BOUNDARY_GUARD_TICKS;
}

static bool vm_net_mock_battle_terminal_close_should_defer(bool battleEndsThisRound)
{
    return battleEndsThisRound &&
           g_mockBattleEnemyHpCurrent == 0 &&
           g_mockBattleRoleHpCurrent > 0 &&
           g_vm_net_mock_battle_auto_enabled != 0;
}

static void vm_net_mock_battle_schedule_terminal_close_after_actions(
    u8 actionCount,
    const char *source)
{
    u32 delayTicks = vm_net_mock_battle_auto_action_delay_ticks(actionCount);

    g_vm_net_mock_battle_terminal_close_not_before_tick =
        g_schedulerTick + delayTicks;
    printf("[info][network] mock_battle_terminal_close_deferred session=%u "
           "actionnum=%u delay_ticks=%u not_before_tick=%u source=%s "
           "response=4/6+4/7-then-terminal-boundary\n",
           g_mockBattleOperateSessionSerial,
           actionCount,
           delayTicks,
           g_vm_net_mock_battle_terminal_close_not_before_tick,
           source ? source : "-");
}

static bool vm_net_mock_battle_pending_settlement_is_deliverable(
    const vm_mock_service_client_session *observer)
{
    if (observer == NULL || observer->onlineRoleId == 0 ||
        g_mockBattleAwaitingSettlement == 0 ||
        g_vm_net_mock_battle_role_id_current != observer->onlineRoleId)
    {
        return false;
    }
    /* A terminal settlement is emitted together with the winning 4/6 and
     * always advances through the native 4/7 -> 25/5 lifecycle. */
    if (g_vm_net_mock_battle_settlement_sent_serial !=
            g_mockBattleOperateSessionSerial &&
        vm_net_mock_battle_inline_settlement_enabled())
    {
        return false;
    }
    return g_vm_net_mock_battle_terminal_close_not_before_tick != 0 &&
           g_schedulerTick >=
               g_vm_net_mock_battle_terminal_close_not_before_tick;
}

static bool vm_net_mock_append_battle_status7_object(
    u8 *out, u32 outCap, u32 *pos, bool forceTeamVictory,
    vm_net_mock_battle_auto_flask_result *autoFlaskOut,
    bool *statusAppendedOut)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHp = g_mockBattleRoleHpMax != 0 ? g_mockBattleRoleHpCurrent :
                 (role ? role->hp : VM_NET_MOCK_ROLE_DEFAULT_HP);
    u32 roleMp = role ? role->mp : VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 statusExp = 0;
    u32 totalExp = role ? role->exp : 0;
    u32 statusCurExp = vm_net_mock_role_next_level_start_exp(totalExp);
    u32 statusLastExp = vm_net_mock_role_last_level_exp(totalExp);
    u32 statusPercentExp = vm_net_mock_role_exp_percent(totalExp);
    u32 statusGold = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 statusLevel = role ? role->level : 1;
    u32 vitality = 0;
    u32 vitalityMax = 0;
    u32 recoverHp = 0;
    u32 recoverMp = 0;
    u32 configuredRecoverHp = vm_net_mock_env_u32_if_set("CBE_BATTLE_RECOVER_HP", 0);
    u32 configuredRecoverMp = vm_net_mock_battle_recover_mp_value();
    u32 configuredHpApplied = 0;
    u32 configuredMpApplied = 0;
    u32 dropItemId = 0;
    u16 dropSeq = 0;
    u32 dropCount = 0;
    bool dropGranted = false;
    bool rewardGranted = false;
    char dropInfo[VM_NET_MOCK_SHOP_NAME_BYTES + 16];
    bool haveDropInfo = false;
    u32 applyRewardExp = 0;
    u32 displayExpGain = 0;
    u32 rewardGold = 0;
    bool victory = g_mockBattleEnemyHpCurrent == 0 &&
                   (forceTeamVictory || roleHp > 0);
    bool rewardAlreadyGranted = false;
    bool mpRecoveryApplied = false;

    if (autoFlaskOut != NULL)
        memset(autoFlaskOut, 0, sizeof(*autoFlaskOut));
    if (statusAppendedOut != NULL)
        *statusAppendedOut = false;

    if (victory)
    {
        rewardAlreadyGranted = (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial);
        applyRewardExp = vm_net_mock_battle_grant_reward_once(&dropItemId,
                                                               &dropSeq,
                                                               &dropCount,
                                                               &dropGranted,
                                                               &rewardGranted);
        displayExpGain = (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial)
                             ? g_vm_net_mock_battle_rewarded_exp
                             : applyRewardExp;
    }
    rewardGold = (victory && !rewardAlreadyGranted && rewardGranted)
                     ? vm_net_mock_mul_capped_u32(
                           vm_net_mock_env_u32_if_set(
                               "CBE_BATTLE_REWARD_GOLD",
                               vm_net_mock_battle_reward_gold_for_enemy(
                                   g_vm_net_mock_battle_enemy_id_current)),
                           vm_net_mock_battle_enemy_count_current())
                     : 0;

    if (autoFlaskOut != NULL)
        vm_net_mock_battle_auto_use_vitality_flasks(autoFlaskOut);
    if (role != NULL)
    {
        u32 hpMax = 0;
        u32 mpMax = 0;
        u32 hpBeforeConfiguredRecovery = 0;
        u32 mpBeforeConfiguredRecovery = 0;

        /* The flask helper above owns its direct mutation.  Re-read the
         * battle counters after it rather than carrying the pre-flask local
         * values into durable settlement. */
        vm_net_mock_role_sync_derived_vitals(role);
        hpMax = role->hpMax ? role->hpMax : VM_NET_MOCK_ROLE_DEFAULT_HP;
        mpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
        roleHp = g_mockBattleRoleHpMax != 0
                     ? vm_net_mock_min_u32(g_mockBattleRoleHpCurrent, hpMax)
                     : vm_net_mock_min_u32(role->hp, hpMax);
        roleMp = g_mockBattleRoleMpMax != 0
                     ? vm_net_mock_min_u32(g_mockBattleRoleMpCurrent, mpMax)
                     : vm_net_mock_min_u32(role->mp, mpMax);

        hpBeforeConfiguredRecovery = roleHp;
        roleHp = vm_net_mock_min_u32(
            vm_net_mock_add_capped_u32(roleHp, configuredRecoverHp), hpMax);
        configuredHpApplied = roleHp - hpBeforeConfiguredRecovery;
        g_mockBattleRoleHpMax = hpMax;
        g_mockBattleRoleHpCurrent = roleHp;

        mpBeforeConfiguredRecovery = roleMp;
        roleMp = vm_net_mock_battle_apply_mp_recovery_once(
            role, roleMp, configuredRecoverMp, &mpRecoveryApplied);
        configuredMpApplied = roleMp >= mpBeforeConfiguredRecovery
                                  ? roleMp - mpBeforeConfiguredRecovery
                                  : 0;

        /* The client applies only these deltas at BattleSettle_UpdateCharAttrs
         * (mmBattle 0x2D46/0x2D4E).  The absolute values below are persisted
         * separately by the same terminal settlement transaction. */
        recoverHp = vm_net_mock_add_capped_u32(
            autoFlaskOut ? autoFlaskOut->hpRestored : 0, configuredHpApplied);
        recoverMp = vm_net_mock_add_capped_u32(
            autoFlaskOut ? autoFlaskOut->mpRestored : 0, configuredMpApplied);
        vm_net_mock_role_apply_battle_settlement(roleHp, roleMp, applyRewardExp, rewardGold,
                                                 &statusLastExp, &statusCurExp,
                                                 &statusPercentExp, &statusLevel,
                                                 &statusGold, &roleHp, &roleMp);
        g_mockBattleRoleHpMax = role->hpMax;
        g_mockBattleRoleHpCurrent = roleHp;
        g_mockBattleRoleMpMax = role->mpMax;
        g_mockBattleRoleMpCurrent = roleMp;
        vm_net_mock_battle_sync_terminal_team_vitals(role);
        statusExp = role->exp;
    }
    else
    {
        recoverHp = configuredRecoverHp;
        recoverMp = configuredRecoverMp;
        statusExp = totalExp + applyRewardExp;
    }
    statusLastExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_LAST_EXP", statusLastExp);
    statusCurExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_CUR_EXP", statusCurExp);
    statusPercentExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_PERCENT_EXP",
                                                  statusPercentExp);
    statusLevel = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_LEVEL", statusLevel);
    if (role == NULL || !vm_net_mock_vitality_snapshot(role, &vitality,
                                                        &vitalityMax))
    {
        printf("[error][mock-service] battle_vitality_snapshot_failed role=%u error=%s\n",
               role ? role->roleId : 0, vm_mysql_last_error());
        return false;
    }
    /* 4/7 carries EXP and money in its numeric fields.  fdata is the
     * optional textual result row rendered by mmBattle at 0x7B08/0x4462;
     * keep that row exclusively for a safe, display-only drop summary.  The
     * regular iteminfo path cannot describe ordinary consumables safely. */
    dropInfo[0] = 0;
    if (dropGranted && role != NULL)
    {
        const vm_net_mock_shop_catalog_item *dropItem = vm_net_mock_find_shop_catalog_item(dropItemId);
        if (dropItem != NULL && dropItem->name[0] != 0)
        {
            int written = snprintf(dropInfo, sizeof(dropInfo), "%s x%u",
                                   dropItem->name, dropCount);
            haveDropInfo = written > 0 && (u32)written < sizeof(dropInfo);
        }
    }
    printf("[info][network] mock_battle_settle enemy=%u enemies=%u victory=%u team_victory=%u reward_claimed=%u exp_gain=%u exp_total=%u gold=%u level=%u vitals=%u/%u,%u/%u recover=%u/%u auto_recover=%u/%u configured_recover=%u/%u drop=%u seq=%u count=%u role=%u battle_role=%u fdata_len=%u\n",
           g_vm_net_mock_battle_enemy_id_current,
           vm_net_mock_battle_enemy_count_current(),
           victory ? 1 : 0,
           forceTeamVictory ? 1 : 0,
            rewardGranted ? 1 : 0,
           displayExpGain,
           statusExp,
           statusGold,
           statusLevel,
           roleHp,
           role ? role->hpMax : 0,
           roleMp,
           role ? role->mpMax : 0,
           recoverHp,
           recoverMp,
           autoFlaskOut ? autoFlaskOut->hpRestored : 0,
           autoFlaskOut ? autoFlaskOut->mpRestored : 0,
           configuredHpApplied,
           configuredMpApplied,
           dropGranted ? dropItemId : 0,
           dropSeq,
           dropCount,
           role ? role->roleId : 0,
           g_vm_net_mock_battle_role_id_current,
           haveDropInfo ? (u32)strlen(dropInfo) : 0);
    vm_autotest_note("mock_battle_settle enemy=%u enemies=%u victory=%u team_victory=%u reward_claimed=%u exp_gain=%u exp_total=%u gold=%u level=%u hp=%u/%u mp=%u/%u recover=%u/%u auto_recover=%u/%u configured_recover=%u/%u recovered=%u drop=%u seq=%u count=%u role=%u battle_role=%u fdata_len=%u\n",
                     g_vm_net_mock_battle_enemy_id_current,
                     vm_net_mock_battle_enemy_count_current(),
                      victory ? 1 : 0,
                      forceTeamVictory ? 1 : 0,
                      rewardGranted ? 1 : 0,
                      displayExpGain,
                     statusExp,
                      statusGold,
                      statusLevel,
                      roleHp,
                      role ? role->hpMax : 0,
                      roleMp,
                      role ? role->mpMax : 0,
                      recoverHp,
                      recoverMp,
                      autoFlaskOut ? autoFlaskOut->hpRestored : 0,
                      autoFlaskOut ? autoFlaskOut->mpRestored : 0,
                      configuredHpApplied,
                      configuredMpApplied,
                      mpRecoveryApplied ? 1 : 0,
                     dropGranted ? dropItemId : 0,
                     dropSeq,
                     dropCount,
                     role ? role->roleId : 0,
                     g_vm_net_mock_battle_role_id_current,
                     haveDropInfo ? (u32)strlen(dropInfo) : 0);

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 7, &objectStart))
        return false;
    /*
     * Battle.cbm HandleBattleSettleMsg(0x743C) reads exp before lastexp,
     * curexp, and persentexp. Main CBE property rendering uses actor+0xB0
     * as total EXP, lastexp as the level-start threshold, and curexp as the
     * next level-start threshold. persentexp is parsed through the integer
     * getter too, then narrowed into a separate progress cache.
     */
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "exp", statusExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", statusLastExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "curexp", statusCurExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", statusPercentExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "energy", vitality))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "energymax", vitalityMax))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "gold", statusGold))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "level", statusLevel))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "bagstatus", 0))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "hp", recoverHp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "mp", recoverMp))
        return false;
    /*
     * The result parser has two display paths. The iteminfo reward-type 1 path
     * enters an equipment/detail registration helper and crashes with ordinary
     * consumable rows; reward-type 2 parses without crashing but only reserves
     * an empty item row in the current client. fdata is a normal settlement
     * text field rendered by mmBattle at 0x7B08/0x4462, so use it only for
     * the visible dropped-item line while the durable item is already
     * persisted in the role backpack.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "itemnum", 0))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", NULL, 0))
        return false;
    if (haveDropInfo &&
        !vm_net_mock_put_object_string(out, outCap, pos, "fdata", dropInfo))
    {
        return false;
    }
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (statusAppendedOut != NULL)
        *statusAppendedOut = true;
    return true;
}

static bool vm_net_mock_append_battle_terminal_status_objects(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
    bool forceTeamVictory, bool *statusAppendedOut)
{
    vm_net_mock_battle_auto_flask_result autoFlask;
    bool appendedCounts = false;
    bool statusAppended = false;

    if (out == NULL || pos == NULL || objectCount == NULL || *objectCount == 0xff)
        return false;
    if (statusAppendedOut != NULL)
        *statusAppendedOut = false;
    if (!vm_net_mock_append_battle_status7_object(out, outCap, pos,
                                                  forceTeamVictory,
                                                  &autoFlask,
                                                  &statusAppended))
    {
        return false;
    }
    if (!statusAppended)
        return true;
    if (statusAppendedOut != NULL)
        *statusAppendedOut = true;
    ++*objectCount;
    if (!vm_net_mock_append_battle_auto_flask_counts_object(out, outCap, pos,
                                                            &autoFlask,
                                                            &appendedCounts))
    {
        return false;
    }
    if (appendedCounts)
    {
        if (*objectCount == 0xff)
            return false;
        ++*objectCount;
    }
    if (autoFlask.updateCount != 0)
    {
        printf("[info][network] mock_battle_auto_flask role=%u hp=%u mp=%u rows=%u response=4/7+7/11\n",
               vm_net_mock_active_role() ? vm_net_mock_active_role()->roleId : 0,
               autoFlask.hpRestored, autoFlask.mpRestored, autoFlask.updateCount);
        vm_autotest_note("mock_battle_auto_flask role=%u hp=%u mp=%u rows=%u response=4/7+7/11 evidence=item.dsh:802/803 JianghuOL.CBE:0x1033544\n",
                         vm_net_mock_active_role() ? vm_net_mock_active_role()->roleId : 0,
                         autoFlask.hpRestored, autoFlask.mpRestored,
                         autoFlask.updateCount);
    }
    return true;
}

static bool vm_net_mock_append_battle_drop_refresh7_if_needed(u8 *out, u32 outCap,
                                                              u32 *pos, u8 *objectCount,
                                                              const char *phase,
                                                              bool allowActiveSession)
{
    u32 objectStart = 0;
    u8 dropRowCount = g_vm_net_mock_battle_rewarded_drop_result_count;
    u8 itemInfo[VM_NET_MOCK_BATTLE_DROP_ITEMINFO_MAX_BYTES];
    u32 itemInfoLen = 0;

    if (g_mockBattleOperateSessionSerial == 0 ||
        g_vm_net_mock_battle_rewarded_serial != g_mockBattleOperateSessionSerial ||
        g_vm_net_mock_battle_settlement_sent_serial != g_mockBattleOperateSessionSerial ||
        (!allowActiveSession && g_mockBattleOperateSessionArmed != 0) ||
        dropRowCount == 0 ||
        dropRowCount > VM_NET_MOCK_BATTLE_DROP_RESULT_MAX ||
        g_vm_net_mock_battle_drop_refresh_sent_serial == g_mockBattleOperateSessionSerial)
    {
        return true;
    }
    if (objectCount != NULL && *objectCount == 0xff)
        return false;

    if (!vm_net_mock_build_item_use_iteminfo_rows_blob(
            itemInfo, sizeof(itemInfo), g_vm_net_mock_battle_rewarded_drops,
            dropRowCount, &itemInfoLen) ||
        itemInfoLen == 0 || itemInfoLen > 0xffffu)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 7, &objectStart))
        return false;
    /*
     * mmGame:sub_D04(0x0D04) consumes 7/7 rows as seq,itemId,count,extra.
     * type=1 is the add/update path and has no user-facing msg field.  Defer
     * it until the scene follow-up packet after battle so the visible kill and
     * settlement flow is not interrupted by the global 7/37 acquire dialog.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    g_vm_net_mock_battle_drop_refresh_sent_serial = g_mockBattleOperateSessionSerial;
    if (objectCount)
        *objectCount = (u8)(*objectCount + 1);
    printf("[info][network] mock_battle_drop_refresh rows=%u first_item=%u first_seq=%u first_delta=%u iteminfo_len=%u phase=%s response=7/7-type1 evidence=mmGame:0x0D04\n",
           dropRowCount,
           g_vm_net_mock_battle_rewarded_drops[0].itemId,
           g_vm_net_mock_battle_rewarded_drops[0].seq,
           g_vm_net_mock_battle_rewarded_drops[0].count,
           itemInfoLen,
           phase ? phase : "-");
    vm_autotest_note("mock_battle_drop_refresh rows=%u first_item=%u first_seq=%u first_delta=%u phase=%s response=7/7-type1 evidence=mmGame:0x0D04\n",
                     dropRowCount,
                     g_vm_net_mock_battle_rewarded_drops[0].itemId,
                     g_vm_net_mock_battle_rewarded_drops[0].seq,
                     g_vm_net_mock_battle_rewarded_drops[0].count,
                     phase ? phase : "-");
    return true;
}

static bool vm_net_mock_append_battle_terminal_subtype8_object(u8 *out, u32 outCap, u32 *pos)
{
    static const u8 terminalInfo[12] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01,
    };
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 8, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 1))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "info", terminalInfo, sizeof(terminalInfo)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_escape4_object(u8 *out, u32 outCap, u32 *pos,
                                                     u8 result)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", result))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_case9_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 9, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_battle_terminal_case11_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, pos, 0);
}

/*
 * A rewarded scene-hangup victory owns a visible 4/7 result panel. Manual
 * panel dismissal sends 25/5; automatic hangup uses this same exact-session
 * predicate to perform its verified delayed 4/5 continuation. The scope
 * keeps ordinary battles and the separate revival path on their own terminal
 * contracts.
 */
static bool vm_net_mock_scene_hangup_result_panel_active(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    return session != NULL &&
           session->sceneHangupEnabled &&
           !session->sceneHangupRestartPending &&
           session->sceneHangupBattleSessionSerial ==
               g_mockBattleOperateSessionSerial &&
           session->onlineRoleId != 0 &&
           session->onlineRoleId == g_vm_net_mock_battle_role_id_current;
}

/*
 * BattleScene_ExitAndCleanup(0x60C8) emits an empty 25/5 only after a normal
 * 4/7 result panel has been dismissed.  This is the authoritative lifecycle
 * boundary for an ordinary rewarded battle: before it, the client still owns
 * the battle screen; after it, a stale AwaitingSettlement flag must not
 * affect a later scene action or battle session.  Scene-hangup deliberately
 * keeps ownership of the same acknowledgement so it can schedule its next
 * 4/5 on a later poll.
 */
static void vm_net_mock_battle_on_scene_default_event(void)
{
    if (g_mockBattleOperateSessionArmed != 0 ||
        g_mockBattleAwaitingSettlement == 0 ||
        g_mockBattleEnemyHpCurrent != 0 ||
        g_mockBattleRoleHpCurrent == 0 ||
        g_vm_net_mock_battle_settlement_sent_serial !=
            g_mockBattleOperateSessionSerial ||
        vm_net_mock_scene_hangup_result_panel_active())
    {
        return;
    }

    vm_net_mock_battle_auto_reset();
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    g_mockBattleSceneMonsterStartActive = 0;
    g_vm_net_mock_battle_terminal_close_not_before_tick = 0;
    printf("[info][mock-service] battle_reward_panel_closed "
           "battle=%u role=%u source=native-25/5 action=release-ordinary-state\n",
           g_mockBattleOperateSessionSerial,
           g_vm_net_mock_battle_role_id_current);
    vm_autotest_note("mock_battle_reward_panel_closed battle=%u role=%u "
                     "source=native-25/5 action=release-ordinary-state "
                     "evidence=mmBattle:0x60C8\n",
                     g_mockBattleOperateSessionSerial,
                     g_vm_net_mock_battle_role_id_current);
}

/*
 * A resurrection-stone confirmation happens after the battle-side HP has
 * already reached zero.  Scene 30/1 only changes the map/position; it does
 * not replace the Battle.cbm character cache which still owns the displayed
 * HP bar.  Reuse the established battle-terminal object family, but keep the
 * status object isolated from victory rewards and automatic flask effects.
 *
 * HandleBattleSettleMsg(0x743C) treats hp/mp as pending changes.  Therefore
 * hpRecovery is the full current max HP for a dead player (0 + max -> max),
 * while MP recovery remains zero because item.dsh row 801 has no MP effect.
 */
static bool vm_net_mock_append_battle_revival_status7_object(u8 *out, u32 outCap,
                                                             u32 *pos,
                                                             u32 hpRecovery)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 totalExp = role ? role->exp : 0;
    u32 lastExp = vm_net_mock_role_last_level_exp(totalExp);
    u32 nextExp = vm_net_mock_role_next_level_start_exp(totalExp);
    u32 percentExp = vm_net_mock_role_exp_percent(totalExp);
    u32 gold = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 level = role ? role->level : 1;
    u32 vitality = 0;
    u32 vitalityMax = 0;

    if (role == NULL || hpRecovery == 0 ||
        !vm_net_mock_vitality_snapshot(role, &vitality, &vitalityMax))
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 7, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "exp", totalExp) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", lastExp) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "curexp", nextExp) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", percentExp) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "energy", vitality) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "energymax", vitalityMax) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "gold", gold) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "level", level) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "bagstatus", 0) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "hp", hpRecovery) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "mp", 0) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "itemnum", 0) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", NULL, 0) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 0))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_battle_revival_stone_completion_response(u8 *out,
                                                                       u32 outCap)
{
    u32 pos = 5;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 hpRecovery = role ? role->hp : 0;

    if (out == NULL || outCap < pos || role == NULL || hpRecovery == 0)
        return 0;
    if (!vm_net_mock_append_battle_revival_status7_object(out, outCap, &pos,
                                                           hpRecovery) ||
        !vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos) ||
        !vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
        !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 4);
    printf("[info][network] mock_battle_revival_terminal hp_recovery=%u mp_recovery=0 role=%u response=4/7+4/8+4/11+4/9 resp=%u evidence=mmBattle:0x743C+0x7DF6+0x2C50\n",
           hpRecovery, role->roleId, pos);
    vm_autotest_note("mock_battle_revival_terminal hp_recovery=%u mp_recovery=0 role=%u response=4/7+4/8+4/11+4/9 evidence=mmBattle:0x743C/0x7DF6/0x2C50\n",
                     hpRecovery, role->roleId);
    return pos;
}

static u32 vm_mock_service_duel_damage(vm_mock_service_duel *duel,
                                       int sourceIndex,
                                       u32 operate,
                                       u32 *sourceMpAfterOut)
{
    vm_mock_service_client_session *source = NULL;
    vm_mock_service_client_session *target = NULL;
    vm_net_mock_role_state *sourceRole = NULL;
    vm_net_mock_role_state *targetRole = NULL;
    vm_net_mock_player_stats sourceStats;
    vm_net_mock_player_stats targetStats;
    const vm_net_mock_skill_catalog_item *skill = NULL;
    u32 rawDamage = 1;
    u32 defense = 0;
    u32 mpAfter = 0;

    if (sourceMpAfterOut)
        *sourceMpAfterOut = 0;
    if (duel == NULL || sourceIndex < 0 || sourceIndex > 1)
        return 0;
    source = vm_mock_service_find_client_session(duel->clientIds[sourceIndex]);
    target = vm_mock_service_find_client_session(duel->clientIds[1 - sourceIndex]);
    sourceRole = vm_mock_service_trade_role_for_session(source, NULL);
    targetRole = vm_mock_service_trade_role_for_session(target, NULL);
    memset(&sourceStats, 0, sizeof(sourceStats));
    memset(&targetStats, 0, sizeof(targetStats));
    vm_net_mock_role_build_player_stats(sourceRole, &sourceStats);
    vm_net_mock_role_build_player_stats(targetRole, &targetStats);
    defense = targetStats.defense;
    mpAfter = duel->mp[sourceIndex];

    skill = vm_net_mock_battle_operate_skill(operate);
    if (operate > 2 && skill != NULL && mpAfter >= skill->mpCost)
    {
        mpAfter -= skill->mpCost;
        /* A spell operation is not necessarily an attack.  In particular,
         * all three professions own heals or timed self/friendly effects.
         * The old duel-only branch converted their zero base damage to a
         * physical strike against the opponent.  Preserve the native spell
         * playback and turn progression, but never manufacture an HP loss
         * for a non-hostile skill. */
        if (!vm_net_mock_battle_skill_is_hostile_damage(skill))
        {
            if (sourceMpAfterOut)
                *sourceMpAfterOut = mpAfter;
            return 0;
        }
        rawDamage = vm_net_mock_battle_skill_raw_damage_from_stats(skill, &sourceStats);
        if (rawDamage == 0)
            rawDamage = sourceStats.attack ? sourceStats.attack : 1;
    }
    else
    {
        rawDamage = sourceStats.attack ? sourceStats.attack : 1;
    }
    rawDamage = vm_net_mock_env_u32_if_set(
        operate > 2 ? "CBE_DUEL_SKILL_DAMAGE" : "CBE_DUEL_DAMAGE",
        vm_net_mock_damage_after_defense(rawDamage, defense));
    if (rawDamage == 0)
        rawDamage = 1;
    if (sourceMpAfterOut)
        *sourceMpAfterOut = mpAfter;
    return vm_net_mock_min_u32(rawDamage, duel->hp[1 - sourceIndex]);
}

static u32 vm_net_mock_build_duel_action_packet(
    u8 *out,
    u32 outCap,
    vm_mock_service_duel *duel,
    const vm_mock_service_duel_event *event,
    vm_mock_service_client_session *observer,
    int observerIndex)
{
    vm_mock_service_client_session *source = NULL;
    u8 actionInfo[256];
    u32 actionInfoLen = 0;
    u32 pos = 5;
    u8 actionCount = 1;
    u8 actorWire = 0;
    u8 targetWire = 1;
    u8 deadWire = 1;
    u8 actionType = 0;
    u32 effectIndex = 0;
    u32 sourceWireId = 0;
    bool includeTeamInfo = false;

    if (out == NULL || outCap < pos || duel == NULL || event == NULL ||
        !event->valid || observer == NULL || observerIndex < 0 || observerIndex > 1)
    {
        return 0;
    }
    source = vm_mock_service_find_client_session(duel->clientIds[event->sourceIndex]);
    if (source == NULL)
        return 0;
    if (observerIndex != event->sourceIndex)
    {
        actorWire = 1;
        targetWire = 0;
        deadWire = 0;
    }
    actionType = event->operate > 2 ? 1 : 0;
    /* The duel event stores a zero HP delta for a non-hostile skill.  Use the
     * source wire as its target so Battle.cbm plays the selected effect on the
     * caster rather than showing a harmless-but-wrong effect on the opponent.
     * A normal physical action can never reach this branch. */
    if (actionType == 1 && event->damage == 0)
        targetWire = actorWire;
    effectIndex = actionType == 1 ?
        vm_net_mock_battle_operate_skill_effect(event->operate) : 0;
    memset(actionInfo, 0, sizeof(actionInfo));
    if (!vm_net_mock_append_battle_actioninfo_record(
            actionInfo, sizeof(actionInfo), &actionInfoLen,
            actionType, actorWire, targetWire, 0,
            vm_net_mock_battle_negative_delta_u32(event->damage), 0,
            effectIndex, 0, 0, 0))
    {
        return 0;
    }
    if (event->terminal)
    {
        u8 deathActionType = (u8)vm_net_mock_env_u32(
            "CBE_BATTLE_DEATH_ACTION_TYPE", 3);
        if (!vm_net_mock_append_battle_actioninfo_record(
                actionInfo, sizeof(actionInfo), &actionInfoLen,
                deathActionType, deadWire, 0, 0, 0, 0, 0, 0, 0, 0))
        {
            return 0;
        }
        ++actionCount;
    }
    includeTeamInfo = actionType == 1 && observerIndex == event->sourceIndex;
    sourceWireId = vm_mock_service_team_member_wire_id(observer, source);
    if (!vm_net_mock_append_battle_action6_object_ex(
            out, outCap, &pos, actionInfo, actionInfoLen, actionCount,
            includeTeamInfo, sourceWireId,
            duel->hp[event->sourceIndex], event->sourceMpAfter))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_duel_operate_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_mock_service_client_session *source = vm_mock_service_get_active_client_session();
    vm_mock_service_duel *duel = NULL;
    vm_mock_service_duel_event *event = NULL;
    int sourceIndex = -1;
    int targetIndex = -1;
    u32 index = 0;
    u8 index8 = 0;
    u32 operate = 0;
    u8 operate8 = 0;
    u32 damage = 0;
    u32 sourceMpAfter = 0;
    u32 responseLen = 0;
    u32 nextSerial = 0;
    u8 sourceBit = 0;
    u8 sourceSlot = 0;

    if (out == NULL || outCap < 5 || source == NULL ||
        !vm_net_mock_is_battle_operate_request(request, requestLen))
    {
        return 0;
    }
    duel = vm_mock_service_duel_find_for_client(source->clientId, &sourceIndex);
    if (duel == NULL || sourceIndex < 0)
    {
        return 0;
    }
    if (duel->finished)
    {
        responseLen = vm_net_mock_build_pending_duel_terminal_response(
            out, outCap, source);
        if (responseLen != 0)
            return responseLen;
        vm_net_mock_finish_wt_packet(out, 5, 0);
        return 5;
    }
    if ((duel->startedMask & (1u << sourceIndex)) == 0)
    {
        vm_net_mock_finish_wt_packet(out, 5, 0);
        return 5;
    }
    if (!vm_net_mock_get_object_u32_field(request, requestLen,
                                          "Operate", &operate) &&
        vm_net_mock_get_object_u8_field(request, requestLen,
                                        "Operate", &operate8))
    {
        operate = operate8;
    }
    if (!vm_net_mock_get_object_u32_field(request, requestLen,
                                          "index", &index) &&
        vm_net_mock_get_object_u8_field(request, requestLen,
                                        "index", &index8))
    {
        index = index8;
    }
    if (operate != 0 && operate <= 2)
    {
        /* Item/escape have their own packet types.  Unknown 4/2 operations
         * are downgraded to a physical spar hit instead of entering the
         * battle-item branch with a null selected-item pointer. */
        operate = 0;
    }
    if (duel->turnIndex != 0xff && duel->turnIndex != sourceIndex)
    {
        vm_net_mock_finish_wt_packet(out, 5, 0);
        printf("[info][mock-service] duel_action_wait serial=%u source=%08x "
               "actor=%d expected=%u action=empty-ack\n",
               duel->serial, source->clientId, sourceIndex, duel->turnIndex);
        return 5;
    }

    targetIndex = 1 - sourceIndex;
    if (operate > 2)
    {
        const vm_net_mock_skill_catalog_item *skill =
            vm_net_mock_battle_operate_skill(operate);
        if (skill == NULL || duel->mp[sourceIndex] < skill->mpCost)
            operate = 0;
    }
    damage = vm_mock_service_duel_damage(duel, sourceIndex, operate,
                                         &sourceMpAfter);
    nextSerial = duel->actionSerial + 1;
    if (nextSerial == 0)
        ++nextSerial;
    sourceSlot = (u8)((nextSerial - 1) % VM_MOCK_SERVICE_DUEL_EVENT_MAX);
    event = &duel->events[sourceSlot];
    if (event->valid)
    {
        vm_net_mock_finish_wt_packet(out, 5, 0);
        printf("[warn][mock-service] duel_action_wait serial=%u source=%08x "
               "reason=event-ring-full slot=%u action=empty-ack\n",
               duel->serial, source->clientId, sourceSlot);
        return 5;
    }
    memset(event, 0, sizeof(*event));
    event->valid = true;
    event->serial = nextSerial;
    event->sourceIndex = (u8)sourceIndex;
    event->operate = operate;
    event->damage = damage;
    event->sourceMpAfter = sourceMpAfter;
    event->targetHpAfter = duel->hp[targetIndex] - damage;
    event->terminal = event->targetHpAfter == 0;

    responseLen = vm_net_mock_build_duel_action_packet(
        out, outCap, duel, event, source, sourceIndex);
    if (responseLen == 0)
    {
        memset(event, 0, sizeof(*event));
        return 0;
    }
    sourceBit = (u8)(1u << sourceIndex);
    event->deliveredMask = sourceBit;
    duel->actionSerial = nextSerial;
    duel->hp[targetIndex] = event->targetHpAfter;
    duel->mp[sourceIndex] = sourceMpAfter;
    duel->turnIndex = event->terminal ? 0xff : (u8)targetIndex;
    if (event->terminal)
    {
        duel->finished = true;
        duel->startPendingMask = 0;
        duel->terminalPendingMask = duel->startedMask;
        duel->terminalNotBeforeTick = g_schedulerTick + 25;
    }
    if (g_vm_net_mock_battle_auto_replay_inflight == 0)
        vm_net_mock_battle_auto_remember_operation(
            source->onlineRoleId, index, operate, "duel-operate");
    printf("[info][mock-service] duel_action serial=%u action=%u source=%08x "
           "actor=%d target=%08x operate=%u damage=%u target_hp=%u/%u "
           "source_mp=%u/%u terminal=%u delivered=%02x resp=%u "
           "mapping=source(0->1),peer(1->0) evidence=mmBattle:0x2B26/0x6CE8/0x6EB0\n",
           duel->serial, event->serial, source->clientId, sourceIndex,
           duel->clientIds[targetIndex], operate, damage,
           duel->hp[targetIndex], duel->hpMax[targetIndex],
           duel->mp[sourceIndex], duel->mpMax[sourceIndex],
           event->terminal ? 1u : 0u, event->deliveredMask, responseLen);
    if (event->terminal)
    {
        printf("[info][mock-service] duel_terminal_arm serial=%u pending=%02x "
               "now=%u not_before=%u delay_ticks=25\n",
               duel->serial, duel->terminalPendingMask,
               g_schedulerTick, duel->terminalNotBeforeTick);
    }
    return responseLen;
}

static u32 vm_net_mock_build_pending_duel_action_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer)
{
    vm_mock_service_duel *duel = NULL;
    vm_mock_service_duel_event *event = NULL;
    int observerIndex = -1;
    u8 observerBit = 0;
    u32 oldestSerial = 0xffffffffu;
    u32 responseLen = 0;

    if (out == NULL || outCap < 5 || observer == NULL)
        return 0;
    duel = vm_mock_service_duel_find_for_client(observer->clientId,
                                                &observerIndex);
    if (duel == NULL || observerIndex < 0 ||
        (duel->startedMask & (1u << observerIndex)) == 0)
    {
        return 0;
    }
    observerBit = (u8)(1u << observerIndex);
    for (u8 i = 0; i < VM_MOCK_SERVICE_DUEL_EVENT_MAX; ++i)
    {
        vm_mock_service_duel_event *candidate = &duel->events[i];
        if (!candidate->valid || (candidate->deliveredMask & observerBit) != 0)
            continue;
        if (candidate->serial < oldestSerial)
        {
            oldestSerial = candidate->serial;
            event = candidate;
        }
    }
    if (event == NULL)
        return 0;
    responseLen = vm_net_mock_build_duel_action_packet(
        out, outCap, duel, event, observer, observerIndex);
    if (responseLen == 0)
        return 0;
    event->deliveredMask |= observerBit;
    printf("[info][mock-service] duel_action_deliver serial=%u action=%u "
           "observer=%08x source=%08x actor=%u damage=%u terminal=%u "
           "delivered=%02x resp=%u\n",
           duel->serial, event->serial, observer->clientId,
           duel->clientIds[event->sourceIndex], event->sourceIndex,
           event->damage, event->terminal ? 1u : 0u,
           event->deliveredMask, responseLen);
    if (event->deliveredMask == 3)
        memset(event, 0, sizeof(*event));
    return responseLen;
}

static u32 vm_net_mock_build_pending_duel_terminal_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer)
{
    vm_mock_service_duel *duel = NULL;
    int observerIndex = -1;
    u8 observerBit = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos || observer == NULL)
        return 0;
    duel = vm_mock_service_duel_find_for_client(observer->clientId,
                                                &observerIndex);
    if (duel == NULL || observerIndex < 0 || !duel->finished)
        return 0;
    observerBit = (u8)(1u << observerIndex);
    if ((duel->terminalPendingMask & observerBit) == 0)
        return 0;
    if (g_schedulerTick < duel->terminalNotBeforeTick)
        return 0;
    for (u8 i = 0; i < VM_MOCK_SERVICE_DUEL_EVENT_MAX; ++i)
    {
        if (duel->events[i].valid &&
            (duel->events[i].deliveredMask & observerBit) == 0)
        {
            return 0;
        }
    }
    if (!vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
        !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
    {
        printf("[error][mock-service] duel_terminal_build_failed serial=%u "
               "observer=%08x pos=%u cap=%u\n",
               duel->serial, observer->clientId, pos, outCap);
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 2);
    duel->terminalPendingMask &= (u8)~observerBit;
    printf("[info][mock-service] duel_terminal_deliver serial=%u observer=%08x "
           "remaining=%02x resp=%u response=4/11+4/9\n",
           duel->serial, observer->clientId,
           duel->terminalPendingMask, pos);
    vm_mock_service_duel_release_if_done(duel);
    return pos;
}

static u32 vm_net_mock_build_battle_pending_settlement_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    bool terminalStatusAppended = false;
    bool terminalStatusAlreadyInline =
        g_vm_net_mock_battle_settlement_sent_serial ==
        g_mockBattleOperateSessionSerial;
    bool closeDeferred =
        g_vm_net_mock_battle_terminal_close_not_before_tick != 0;
    u32 closeNotBeforeTick =
        g_vm_net_mock_battle_terminal_close_not_before_tick;
    bool terminalResultPanelActive = false;

    if (outCap < pos)
        return 0;
    /* HandleBattleActionMsg(0x6EB0) has already received the final action,
     * and the winning 4/7 must have been parsed in that same response.
     * Rebuilding it here would duplicate the reward panel and arrive after the
     * type-3 death callback. Keep the older pending-settlement path only for
     * non-inline experiment responses. */
    if (!terminalStatusAlreadyInline)
    {
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &objectCount, false,
                &terminalStatusAppended))
        {
            return 0;
        }
    }
    if (terminalStatusAppended)
        g_vm_net_mock_battle_settlement_sent_serial =
            g_mockBattleOperateSessionSerial;
    terminalResultPanelActive =
        terminalStatusAlreadyInline || terminalStatusAppended;
    if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                           &objectCount,
                                                           "battle-pending-settlement",
                                                           false))
        return 0;
    if (terminalResultPanelActive)
    {
        /*
         * 4/7 is the winning result panel for both ordinary battles and
         * scene-hangup rounds.  Do not overwrite its phase with 4/11(type=0)
         * or with revival-only 4/8 fields.  A previously-inline 4/7 needs no
         * response here; when 4/7 was built by this fallback path, return
         * only that result object. Both ordinary and scene-hangup panels
         * close through the client's native 25/5. A new 4/5 must never be
         * injected while this result panel still owns the BattleScreen.
         */
        if (closeDeferred)
        {
            g_vm_net_mock_battle_terminal_close_not_before_tick = 0;
            printf("[info][mock-service] battle_reward_panel_wait "
                   "battle=%u action_boundary=%u mode=%s "
                   "next_step=native-25/5\n",
                   g_mockBattleOperateSessionSerial, closeNotBeforeTick,
                   vm_net_mock_scene_hangup_result_panel_active() ?
                       "scene-hangup" : "ordinary");
        }
        if (objectCount == 0)
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[info][mock-service] battle_reward_panel_deliver "
               "battle=%u objects=%u mode=%s response=4/7-await-native-25/5\n",
               g_mockBattleOperateSessionSerial, objectCount,
               vm_net_mock_scene_hangup_result_panel_active() ?
                   "scene-hangup" : "ordinary");
        return pos;
    }
    /* Ordinary monster victories have
     * no legal terminal without a 4/7 result panel.  4/11+4/9 belongs to
     * distinct control/revival paths and must never replace this settlement. */
    return 0;
}

static void vm_net_mock_rewrite_team_battle_teaminfo_wire_ids(
    u8 *packet,
    u32 packetLen,
    const vm_mock_service_team *team,
    vm_mock_service_client_session *observer)
{
    const u8 *teamInfo = NULL;
    u16 teamInfoLen = 0;
    u8 *mutableInfo = NULL;

    if (packet == NULL || team == NULL || observer == NULL ||
        team->battleMemberCount < 2 ||
        team->battleMemberCount > VM_MOCK_SERVICE_TEAM_MEMBER_MAX ||
        !vm_net_mock_get_battle_teaminfo_raw_field(packet, packetLen,
                                                    &teamInfo, &teamInfoLen) ||
        teamInfo == NULL || teamInfoLen != (u16)(team->battleMemberCount * 14u))
    {
        return;
    }
    mutableInfo = (u8 *)teamInfo;
    for (u8 memberIndex = 0; memberIndex < team->battleMemberCount; ++memberIndex)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->battleMemberClientIds[memberIndex]);
        u32 wireId = vm_mock_service_team_member_wire_id(observer, member);
        u32 offset = (u32)memberIndex * 14u;

        if (member == NULL || wireId == 0 || mutableInfo[offset] != 0 ||
            mutableInfo[offset + 1] != 4)
        {
            return;
        }
        mutableInfo[offset + 2] = (u8)(wireId >> 24);
        mutableInfo[offset + 3] = (u8)(wireId >> 16);
        mutableInfo[offset + 4] = (u8)(wireId >> 8);
        mutableInfo[offset + 5] = (u8)wireId;
    }
}

static u32 vm_net_mock_build_pending_team_battle_action_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer)
{
    vm_mock_service_team *team = observer ?
        vm_mock_service_team_find_for_client(observer->clientId) : NULL;
    vm_mock_service_team_battle_event *event = NULL;
    vm_mock_service_client_session *source = NULL;
    int memberIndex = vm_mock_service_team_battle_member_index(
        team, observer ? observer->clientId : 0);
    u32 oldestSerial = 0xffffffffu;
    u32 pos = 5;
    u8 objectCount = 0;
    u8 memberBit = 0;
    u8 fullMask = 0;
    u32 sourceWireId = 0;

    if (out == NULL || outCap < pos || observer == NULL || team == NULL ||
        !team->battleActive || memberIndex < 0 ||
        memberIndex >= team->battleMemberCount)
    {
        return 0;
    }
    memberBit = (u8)(1u << memberIndex);
    fullMask = (u8)((1u << team->battleMemberCount) - 1u);
    for (u8 i = 0; i < VM_MOCK_SERVICE_TEAM_BATTLE_EVENT_MAX; ++i)
    {
        vm_mock_service_team_battle_event *candidate = &team->battleEvents[i];
        if (!candidate->valid || (candidate->deliveredMask & memberBit) != 0)
            continue;
        if (candidate->serial < oldestSerial)
        {
            oldestSerial = candidate->serial;
            event = candidate;
        }
    }
    if (event == NULL || event->objectLen < 6 ||
        event->objectLen > sizeof(event->objectData) ||
        pos + event->objectLen > outCap)
    {
        return 0;
    }

    g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleEnemyCountCurrent = team->battleMonsterCount;
    g_mockBattleOperateTurnCounter = team->battleTurnCounter;
    g_vm_net_mock_battle_enemy_id_current = team->battleEnemyId;
    memcpy(g_mockBattleEnemyHpSlots, team->battleEnemyHpSlots,
           sizeof(g_mockBattleEnemyHpSlots));
    memcpy(g_mockBattleEnemyHpMaxSlots, team->battleEnemyHpMaxSlots,
           sizeof(g_mockBattleEnemyHpMaxSlots));
    g_mockBattleEnemyHpCurrent = team->battleEnemyHpCurrent;
    g_mockBattleEnemyHpMax = team->battleEnemyHpMax;
    g_mockBattleRoleHpCurrent = team->battleMemberHp[memberIndex];
    g_mockBattleRoleHpMax = team->battleMemberHpMax[memberIndex];
    g_mockBattleRoleMpCurrent = team->battleMemberMp[memberIndex];
    g_mockBattleRoleMpMax = team->battleMemberMpMax[memberIndex];
    g_vm_net_mock_battle_role_id_current = observer->onlineRoleId;

    memcpy(out + pos, event->objectData, event->objectLen);
    pos += event->objectLen;
    ++objectCount;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    source = vm_mock_service_find_client_session(event->sourceClientId);
    sourceWireId = vm_mock_service_team_member_wire_id(observer, source);
    vm_net_mock_rewrite_team_battle_teaminfo_wire_ids(out, pos, team, observer);

    if (event->terminalVictory)
    {
        bool terminalStatusAppended = false;
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &objectCount, true,
                &terminalStatusAppended))
            return 0;
        if (terminalStatusAppended)
            g_vm_net_mock_battle_settlement_sent_serial =
                g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(
                out, outCap, &pos, &objectCount,
                "team-battle-peer-inline", true))
        {
            return 0;
        }
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = 1;
        if (!terminalStatusAppended)
        {
            vm_net_mock_battle_save_terminal_role_state(
                "team-battle-peer-no-result", true);
        }
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    event->deliveredMask = (u8)(event->deliveredMask | memberBit);
    printf("[info][mock-service] team_battle_action_deliver battle=%u action=%u "
           "observer=%08x source=%08x source_wire=%u actor=%u "
           "enemyhp=%u/%u terminal=%u objects=%u resp=%u delivered=%02x/%02x "
           "evidence=mmBattle:0x6CE8/0x6EB0\n",
           team->battleSerial,
           event->serial,
           observer->clientId,
           event->sourceClientId,
           sourceWireId,
           memberIndex,
           team->battleEnemyHpCurrent,
           team->battleEnemyHpMax,
           event->terminalVictory ? 1 : 0,
           objectCount,
           pos,
           event->deliveredMask,
           fullMask);
    if (event->deliveredMask == fullMask)
        event->valid = false;
    return pos;
}

static bool vm_net_mock_is_battle_escape_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 4 &&
           object.subtype == 4 &&
           object.payloadLen == 0;
}

static u32 vm_net_mock_build_duel_escape_response(const u8 *request,
                                                   u32 requestLen,
                                                   u8 *out,
                                                   u32 outCap)
{
    vm_mock_service_client_session *source = vm_mock_service_get_active_client_session();
    vm_mock_service_duel *duel = NULL;
    int sourceIndex = -1;
    u8 sourceBit = 0;
    u8 peerBit = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos || source == NULL ||
        !vm_net_mock_is_battle_escape_request(request, requestLen))
    {
        return 0;
    }
    duel = vm_mock_service_duel_find_for_client(source->clientId, &sourceIndex);
    if (duel == NULL || sourceIndex < 0)
        return 0;
    sourceBit = (u8)(1u << sourceIndex);
    peerBit = (u8)(1u << (1 - sourceIndex));
    if (!vm_net_mock_append_battle_escape4_object(out, outCap, &pos, 1))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    duel->finished = true;
    duel->startPendingMask = 0;
    memset(duel->events, 0, sizeof(duel->events));
    duel->terminalPendingMask = (u8)(duel->startedMask & peerBit);
    duel->terminalNotBeforeTick = g_schedulerTick + 5;
    printf("[info][mock-service] duel_escape serial=%u source=%08x actor=%d "
           "direct=%02x peer_terminal=%02x resp=%u response=4/4\n",
           duel->serial, source->clientId, sourceIndex,
           sourceBit, duel->terminalPendingMask, pos);
    vm_mock_service_duel_release_if_done(duel);
    return pos;
}

static u32 vm_net_mock_build_battle_escape_response(const u8 *request, u32 requestLen,
                                                    u8 *out, u32 outCap)
{
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 playerSlot = 0;
    u8 enemySlot = 0;
    u8 actionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_ACTION_TYPE", 0);
    u8 counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE", actionType);
    u8 counterChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    u8 deathActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_DEATH_ACTION_TYPE", 3);
    u32 counterValueB = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
    u32 type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    u8 type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    u8 type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    u8 type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);
    u8 actionInfo[192];
    u32 actionInfoLen = 0;
    u8 actionCount = 0;
    u32 totalDamage = 0;
    u32 escapeRate = vm_net_mock_env_u32_if_set("CBE_BATTLE_ESCAPE_RATE", 50);
    bool success = false;
    bool battleEndsThisRound = false;
    u32 pos = 5;
    u8 objectCount = 0;

    if (out == NULL || outCap < pos || !vm_net_mock_is_battle_escape_request(request, requestLen))
        return 0;
    if (g_mockBattleOperateSessionArmed == 0 && !vm_net_mock_current_screen_is_battle())
        return 0;

    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT", defaultPlayerSlot);
    enemySlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT", defaultEnemySlot);

    if (g_mockBattleRoleHpCurrent == 0)
        g_mockBattleRoleHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP",
                                                        vm_net_mock_role_current_hp_for_battle());
    success = vm_net_mock_battle_roll_percent(escapeRate);

    if (!vm_net_mock_append_battle_escape4_object(out, outCap, &pos, success ? 1 : 0))
        return 0;
    ++objectCount;

    if (success)
    {
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = 0;
        vm_net_mock_battle_save_completed_current_role_state(
            "battle-escape-success");
        printf("[info][network] mock_battle_escape result=success rate=%u enemyhp=%u slots=%u/%u/%u rolehp=%u resp=%u evidence=mmBattle:0x7BD0 case4 result=1\n",
               escapeRate,
               g_mockBattleEnemyHpCurrent,
               g_mockBattleEnemyHpSlots[0],
               g_mockBattleEnemyHpSlots[1],
               g_mockBattleEnemyHpSlots[2],
               g_mockBattleRoleHpCurrent,
               pos);
        vm_autotest_note("mock_battle_escape result=success response=4/4 evidence=mmBattle:0x7BD0 case4 result=1\n");
        return pos;
    }

    memset(actionInfo, 0, sizeof(actionInfo));
    if (g_mockBattleEnemyHpCurrent > 0 && g_mockBattleRoleHpCurrent > 0)
    {
        u8 enemyCount = vm_net_mock_battle_enemy_count_current();

        for (u8 enemyIndex = 0; enemyIndex < enemyCount && enemyIndex < 3 &&
                               g_mockBattleRoleHpCurrent > 0; ++enemyIndex)
        {
            u8 enemyWire = 0;
            u8 counterActorWireSlot = 0;
            u8 counterTargetWireSlot = 0;
            u32 oneCounterDamage = 0;

            if (g_mockBattleEnemyHpSlots[enemyIndex] == 0)
                continue;
            enemyWire = vm_net_mock_battle_enemy_wire_for_index(enemyIndex, playerOnRight,
                                                                battleSide, enemySlot);
            counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                          enemyWire);
            counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                           playerSlot);
            if (counterActionType == 1)
            {
                counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                              counterActorWireSlot);
                counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                               counterTargetWireSlot);
            }

            oneCounterDamage = vm_net_mock_battle_apply_damage_to_role(
                vm_net_mock_battle_enemy_damage_to_role(g_vm_net_mock_battle_enemy_id_current,
                                                        g_mockBattleRoleHpCurrent,
                                                        enemyWire));
            /* Escape failure still resolves every living enemy.  A miss is
             * encoded as a zero-delta action so its turn remains visible. */
            if (oneCounterDamage != 0)
                totalDamage = vm_net_mock_add_capped_u32(totalDamage, oneCounterDamage);
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, counterActionType,
                                                             counterActorWireSlot,
                                                             counterTargetWireSlot,
                                                             oneCounterDamage == 0
                                                                 ? VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE
                                                                 : counterChildFlag,
                                                             vm_net_mock_battle_negative_delta_u32(oneCounterDamage),
                                                             counterValueB,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1EffectIndex : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail0 : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail1 : 0,
                                                             (counterActionType == 1 || counterActionType == 2) ? type1Tail2 : 0))
            {
                return 0;
            }
            ++actionCount;
        }
    }

    if (g_mockBattleRoleHpCurrent == 0)
    {
        if (actionCount >= 6)
            return 0;
        if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                         &actionInfoLen, deathActionType,
                                                         playerSlot, 0, 0,
                                                         0, 0, 0, 0, 0, 0))
            return 0;
        ++actionCount;
    }

    if (actionCount != 0)
    {
        if (!vm_net_mock_append_battle_action6_object(out, outCap, &pos,
                                                     actionInfo, actionInfoLen,
                                                     actionCount))
            return 0;
        ++objectCount;
    }

    battleEndsThisRound = g_mockBattleRoleHpCurrent == 0;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    g_mockBattlePendingEnemyTurn = 0;
    if (battleEndsThisRound)
    {
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattleAwaitingSettlement = 0;
    }
    if (battleEndsThisRound)
    {
        vm_net_mock_battle_save_completed_current_role_state(
            "battle-escape-failed-death");
    }
    else
    {
        vm_net_mock_battle_save_current_role_state("battle-escape-failed");
    }

    printf("[info][network] mock_battle_escape result=failed rate=%u actions=%u damage=%u enemyhp=%u slots=%u/%u/%u rolehp=%u terminal=%u resp=%u evidence=mmBattle:0x7BD0 case4 result=0 + 0x6EB0 action6\n",
           escapeRate,
           actionCount,
           totalDamage,
           g_mockBattleEnemyHpCurrent,
           g_mockBattleEnemyHpSlots[0],
           g_mockBattleEnemyHpSlots[1],
           g_mockBattleEnemyHpSlots[2],
           g_mockBattleRoleHpCurrent,
           battleEndsThisRound ? 1 : 0,
           pos);
    vm_autotest_note("mock_battle_escape result=failed actions=%u damage=%u terminal=%u response=4/4+4/6 evidence=mmBattle:0x7BD0/0x6EB0\n",
                     actionCount,
                     totalDamage,
                     battleEndsThisRound ? 1 : 0);
    return pos;
}

/*
 * The battle menu's Auto entry sends exactly one 1/4/11 object with its
 * requested state in `type`: BattleMenu_SelectOption(0x5F78) writes type=1,
 * while BattleScene_HandleInput(0x6258) writes type=0 when cancelling.
 *
 * HandleServerBattleCmd(0x7BD0) consumes the matching response only when
 * `result=1`; it uses `type=1` to enter its native automatic-action phase and
 * `type=0` to return to the ordinary battle phase.  This is a toggle
 * acknowledgement, not an action result, so it must not advance the server
 * round or clear a pending monster turn.  With type=1 the native client
 * timer will later issue the empty 4/12 replay request; type=0 is the only
 * explicit cancellation path.
 */
static u32 vm_net_mock_build_battle_auto11_toggle_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u8 kind = 0;
    u8 subtype = 0;
    u8 requestedType = 0;
    u32 offset = 4;
    u32 pos = 5;
    vm_net_mock_request_object object;

    if (request == NULL || out == NULL ||
        !vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 4 || subtype != 11 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen ||
        object.major != 1 || object.kind != 4 || object.subtype != 11 ||
        !vm_net_mock_get_object_u8_field(object.payload, object.payloadLen,
                                         "type", &requestedType) ||
        requestedType > 1)
    {
        return 0;
    }

    /* In standalone-service mode battle ownership is the authoritative
     * per-session operation state; there is no emulator screen stack to query.
     */
    if (g_mockBattleOperateSessionArmed == 0 && !vm_net_mock_current_screen_is_battle())
    {
        if (requestedType == 0)
        {
            printf("[warn][network] mock_battle_auto_cancel_rejected "
                   "reason=no-live-battle session=%u armed=%u awaiting=%u\n",
                   g_mockBattleOperateSessionSerial,
                   g_mockBattleOperateSessionArmed,
                   g_mockBattleAwaitingSettlement);
        }
        return 0;
    }
    if (requestedType == 0)
    {
        printf("[info][network] mock_battle_auto_cancel_request session=%u "
               "turn=%u armed=%u awaiting=%u hangup=%u\n",
               g_mockBattleOperateSessionSerial,
               g_mockBattleOperateTurnCounter,
               g_mockBattleOperateSessionArmed,
               g_mockBattleAwaitingSettlement,
               vm_net_mock_scene_hangup_result_panel_active() ? 1u : 0u);
    }
    if (outCap < pos ||
        !vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos,
                                                            requestedType))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (requestedType != 0)
        vm_net_mock_battle_auto_arm();
    else
    {
        vm_net_mock_battle_auto_reset();
        /* 4/11(type=0) is the client-native explicit cancellation action
         * while an automatic hangup battle is active.  It must stop the
         * server's cross-battle continuation as well; otherwise the player
         * would return to scene and be pulled into another 4/5. */
        vm_mock_service_session_clear_scene_hangup(
            vm_mock_service_get_active_client_session(), "battle-auto-cancel");
    }

    printf("[info][network] mock_battle_auto_toggle type=%u session=%u turn=%u "
           "pending_enemy=%u enabled=%u next_request=client-4/12 resp=%u "
           "evidence=mmBattle:0x5F78/0x6258->0x7BD0(case11)\n",
           requestedType,
           g_mockBattleOperateSessionSerial,
           g_mockBattleOperateTurnCounter,
           g_mockBattlePendingEnemyTurn,
           g_vm_net_mock_battle_auto_enabled,
           pos);
    vm_autotest_note("mock_battle_auto_toggle type=%u session=%u turn=%u "
                     "enabled=%u next_request=client-4/12 response=4/11 evidence=mmBattle:0x5F78/0x6258/0x7BD0\n",
                     requestedType,
                     g_mockBattleOperateSessionSerial,
                     g_mockBattleOperateTurnCounter,
                     g_vm_net_mock_battle_auto_enabled);
    return pos;
}

/* The native auto timer sends exactly one empty 1/4/12 object.  It is not a
 * cancellation packet: BattleScene_HandleInput(0x6258) sends 4/11(type=0)
 * for that.  Reconstruct the action only after this real client request so
 * the natural timer, parser wait state, and normal battle builders retain
 * ownership of the round transition. */
static u32 vm_net_mock_build_battle_auto12_replay_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    vm_net_mock_request_object object;
    vm_mock_service_client_session *source =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = NULL;
    vm_mock_service_duel *duel = NULL;
    u8 kind = 0;
    u8 subtype = 0;
    u8 replayIntent[96];
    u32 offset = 4;
    const u32 intentObjectStart = 4;
    u32 intentPos = 9;
    u32 index = 0;
    u32 operate = 0;
    u32 responseLen = 0;
    bool saved = false;
    bool activeTeamReplay = false;
    bool activeSoloReplay = false;
    bool activeLocalBattleScreen = false;
    bool resumedFromClientTimer = false;
    const char *selection = "bootstrap-physical";
    const char *mode = "solo";
    u8 previousReplayInflight = 0;

    if (request == NULL || out == NULL || outCap < 5 || source == NULL ||
        requestLen != 9 ||
        !vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 4 || subtype != 12 ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 4 ||
        object.subtype != 12 || object.payloadLen != 0 ||
        source->onlineRoleId == 0)
    {
        return 0;
    }

    duel = vm_mock_service_duel_find_for_client(source->clientId, NULL);
    team = vm_mock_service_team_find_for_client(source->clientId);
    if (duel != NULL && duel->active && !duel->finished)
    {
        mode = "duel";
    }
    else if (team != NULL && team->battleActive && !team->battleFinished)
    {
        if (!vm_mock_service_team_battle_contains_client(team, source->clientId))
            return 0;
        mode = "team";
        activeTeamReplay = true;
    }
    else
    {
        /*
         * `BattleAutoAction_TimerTick(0x2952)` emits 4/12 from the
         * client's own automatic-state byte.  A new ordinary scene battle
         * starts without a 4/11 object, so the server-side flag is reset at
         * its 4/5 boundary.  The client can nevertheless still hold a
         * previously enabled automatic mode and then proves that fact with
         * this empty 4/12 request.  Do not leave such a live battle waiting
         * forever just because the server has no local 4/11 record.
         *
         * The proof is deliberately narrow: it must be the current role's
         * armed, non-terminal solo battle.  Result panels, dead battles,
         * another account's battle and an arbitrary scene request remain
         * outside this path, so an old timer can never replay an action into
         * a completed or unrelated session.
         */
        activeSoloReplay =
            g_mockBattleOperateSessionArmed != 0 &&
            g_mockBattleAwaitingSettlement == 0 &&
            g_vm_net_mock_battle_role_id_current == source->onlineRoleId &&
            g_mockBattleEnemyHpCurrent != 0 &&
            g_mockBattleRoleHpCurrent != 0;
        /* Preserve the original in-process emulator path: it can observe a
         * real BattleScreen directly.  The standalone server deliberately
         * has no such screen and therefore accepts only the narrower
         * authority-state predicate above. */
        activeLocalBattleScreen = vm_net_mock_current_screen_is_battle();
        if (!activeSoloReplay && !activeLocalBattleScreen)
            return 0;
    }

    /* The client keeps its own automatic-action state across a completed
     * team battle.  Its next BattleAutoAction_TimerTick(0x2952) therefore
     * emits this exact empty 4/12 after the new 4/5 start.  The process-wide
     * mock flag is deliberately reset at an ordinary result-panel close and
     * cannot veto that newer client proof.  Scope this recovery only to an
     * active team member; solo and duel paths still require the explicit
     * 4/11(type=1) acknowledgement state below. */
    if (!activeTeamReplay && g_vm_net_mock_battle_auto_enabled == 0)
    {
        if (!activeSoloReplay)
            return 0;
        vm_net_mock_battle_auto_arm();
        resumedFromClientTimer = true;
        printf("[info][network] mock_battle_auto_resume source=%08x "
               "role=%u session=%u reason=client-4/12-live-solo-proof "
               "evidence=mmBattle:0x2952\n",
               source->clientId, source->onlineRoleId,
               g_mockBattleOperateSessionSerial);
    }
    if (activeTeamReplay && g_vm_net_mock_battle_auto_enabled == 0)
    {
        vm_net_mock_battle_auto_arm();
        printf("[info][network] mock_team_battle_auto_resume source=%08x "
               "role=%u battle=%u evidence=client-4/12-after-4/5\n",
               source->clientId, source->onlineRoleId, team->battleSerial);
    }

    saved = g_vm_net_mock_battle_auto_last_operation_valid != 0 &&
            g_vm_net_mock_battle_auto_last_operation_role_id ==
                source->onlineRoleId &&
            (g_vm_net_mock_battle_auto_last_operation_operate == 0 ||
             vm_net_mock_battle_operate_is_skill(
                 g_vm_net_mock_battle_auto_last_operation_operate));
    if (saved)
    {
        index = g_vm_net_mock_battle_auto_last_operation_index;
        operate = g_vm_net_mock_battle_auto_last_operation_operate;
        selection = "saved-last-operation";
    }

    /* Incoming WT packets have no response object-count byte.  Construct the
     * same narrow 4/2 intent the existing authoritative builders consume, but
     * only as an implementation detail of the already-received 4/12 request.
     * It is never queued or emitted to the client. */
    memset(replayIntent, 0, sizeof(replayIntent));
    replayIntent[0] = 'W';
    replayIntent[1] = 'T';
    replayIntent[intentObjectStart] = 1;
    replayIntent[intentObjectStart + 1] = 4;
    replayIntent[intentObjectStart + 2] = 2;
    if (!vm_net_mock_put_object_u32(replayIntent, sizeof(replayIntent),
                                    &intentPos, "index", index) ||
        !vm_net_mock_put_object_u32(replayIntent, sizeof(replayIntent),
                                    &intentPos, "Operate", operate))
    {
        return 0;
    }
    replayIntent[2] = (u8)(intentPos >> 8);
    replayIntent[3] = (u8)intentPos;
    replayIntent[intentObjectStart + 3] =
        (u8)((intentPos - intentObjectStart) >> 8);
    replayIntent[intentObjectStart + 4] =
        (u8)(intentPos - intentObjectStart);
    if (!vm_net_mock_is_battle_operate_request(replayIntent, intentPos))
    {
        printf("[error][mock-service] battle_auto_replay_intent_invalid "
               "source=%08x len=%u index=%u operate=%u\n",
               source->clientId, intentPos, index, operate);
        return 0;
    }

    previousReplayInflight = g_vm_net_mock_battle_auto_replay_inflight;
    g_vm_net_mock_battle_auto_replay_inflight = 1;
    if (duel != NULL && duel->active && !duel->finished)
    {
        responseLen = vm_net_mock_build_duel_operate_response(
            replayIntent, intentPos, out, outCap);
    }
    else if (team != NULL && team->battleActive && !team->battleFinished)
    {
        responseLen = vm_net_mock_build_synchronized_team_battle_response(
            replayIntent, intentPos, out, outCap,
            VM_MOCK_TEAM_BATTLE_BUILD_OPERATE);
    }
    else
    {
        responseLen = vm_net_mock_build_battle_operate_response(
            replayIntent, intentPos, out, outCap);
    }
    g_vm_net_mock_battle_auto_replay_inflight = previousReplayInflight;

    if (responseLen == 0)
    {
        printf("[error][mock-service] battle_auto_replay_build_failed "
               "source=%08x role=%u mode=%s session=%u turn=%u "
               "selection=%s index=%u operate=%u\n",
               source->clientId, source->onlineRoleId, mode,
               g_mockBattleOperateSessionSerial, g_mockBattleOperateTurnCounter,
               selection, index, operate);
        return 0;
    }

    printf("[info][network] mock_battle_auto_replay source=%08x role=%u "
           "mode=%s session=%u turn=%u selection=%s saved_index=%u "
           "saved_operate=%u resumed=%u response=%u evidence=mmBattle:0x2952->4/12->0x7BD0(case6)\n",
           source->clientId, source->onlineRoleId, mode,
           g_mockBattleOperateSessionSerial, g_mockBattleOperateTurnCounter,
           selection, index, operate, resumedFromClientTimer ? 1u : 0u,
           responseLen);
    vm_autotest_note("mock_battle_auto_replay role=%u mode=%s selection=%s "
                     "index=%u operate=%u response=4/6 "
                     "evidence=mmBattle:0x2952/0x7BD0/0x6EB0\n",
                     source->onlineRoleId, mode, selection, index, operate);
    return responseLen;
}

static bool vm_net_mock_parse_hangup_battle_start_request(
    const u8 *request, u32 requestLen,
    vm_net_mock_request_object *moveUploadOut,
    bool *hasMoveUploadOut)
{
    u8 kind = 0;
    u8 subtype = 0;
    u8 requestType = 0;
    u32 offset = 4;
    vm_net_mock_request_object actorOther;
    vm_net_mock_request_object hangup;
    vm_net_mock_request_object extra;
    const u8 *moveInfo = NULL;
    u16 moveInfoLen = 0;

    if (moveUploadOut)
        memset(moveUploadOut, 0, sizeof(*moveUploadOut));
    if (hasMoveUploadOut)
        *hasMoveUploadOut = false;

    if (request == NULL || requestLen < 24 ||
        !vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 2 || subtype != 10)
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &actorOther))
        return false;
    if (actorOther.major != 1 ||
        actorOther.kind != 2 ||
        actorOther.subtype != 10 ||
        actorOther.payloadLen != 10)
    {
        return false;
    }
    if (!vm_net_mock_get_object_u8_field(actorOther.payload, actorOther.payloadLen,
                                         "Type", &requestType) ||
        requestType != 2)
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &hangup))
        return false;
    if (hangup.major != 1 ||
        hangup.kind != 0x19 ||
        hangup.subtype != 3 ||
        hangup.payloadLen != 0)
    {
        return false;
    }
    if (vm_net_mock_next_request_object(request, requestLen, &offset, &extra))
    {
        /* Runtime packets can flush the pending ten-direction movement queue
         * after the two-object hangup marker.  Accept only that exact upload;
         * other trailing objects remain outside this handler. */
        if (extra.major != 1 || extra.kind != 2 || extra.subtype != 1 ||
            extra.payloadLen != 23 ||
            !vm_net_mock_get_object_blob_field(extra.payload, extra.payloadLen,
                                               "moveinfo", &moveInfo,
                                               &moveInfoLen) ||
            !vm_net_mock_is_actor_moveinfo_timeline(moveInfo, moveInfoLen))
        {
            return false;
        }
        if (vm_net_mock_next_request_object(request, requestLen, &offset, &actorOther))
            return false;
        if (moveUploadOut)
            *moveUploadOut = extra;
        if (hasMoveUploadOut)
            *hasMoveUploadOut = true;
    }
    return offset == requestLen;
}

static bool vm_net_mock_is_hangup_battle_start_request(const u8 *request,
                                                         u32 requestLen)
{
    return vm_net_mock_parse_hangup_battle_start_request(request, requestLen,
                                                         NULL, NULL);
}

/* A completed hangup battle owns the following five-second scene interval.
 * A new `2/10(Type=2)+25/3` from the native client before its deadline is not
 * a fresh user challenge: it is the client's regular post-result automatic
 * start probe.  Acknowledge its actor-other leg and leave the existing armed
 * session intact; a later scene poll then emits the documented 2/2+4/5+4/11
 * continuation. */
static bool vm_net_mock_scene_hangup_restart_waiting(
    const vm_mock_service_client_session *session, const char *scene)
{
    return session != NULL && scene != NULL &&
           session->sceneHangupEnabled &&
           session->sceneHangupRestartPending &&
           session->sceneHangupBattleSessionSerial != 0 &&
           session->sceneHangupBattleSessionSerial ==
               g_mockBattleOperateSessionSerial &&
           session->onlineRoleId != 0 &&
           g_vm_net_mock_battle_role_id_current == session->onlineRoleId &&
           g_mockBattleOperateSessionArmed == 0 &&
           g_mockBattleAwaitingSettlement != 0 &&
           g_mockBattleSceneMonsterStartActive != 0 &&
           g_mockBattleEnemyHpCurrent == 0 &&
           g_mockBattleRoleHpCurrent != 0 &&
           strcmp(session->sceneHangupScene, scene) == 0 &&
           vm_mock_service_session_scene_is_visible(session, scene) &&
           g_schedulerTick < session->sceneHangupRestartNotBeforeTick;
}

static void vm_net_mock_scene_hangup_record_battle_start(
    vm_mock_service_client_session *session,
    const char *scene,
    const char *source)
{
    if (session == NULL || session->onlineRoleId == 0 ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_mock_service_session_scene_is_visible(session, scene) ||
        g_vm_net_mock_battle_role_id_current != session->onlineRoleId)
    {
        return;
    }
    if (!session->sceneHangupEnabled)
        session->sceneHangupCompletedBattles = 0;
    session->sceneHangupEnabled = true;
    session->sceneHangupRestartPending = false;
    session->sceneHangupBattleSessionSerial = g_mockBattleOperateSessionSerial;
    session->sceneHangupRestartNotBeforeTick = 0;
    snprintf(session->sceneHangupScene, sizeof(session->sceneHangupScene),
             "%s", scene);
    printf("[info][mock-service] scene_hangup_start client=%08x role=%u "
           "scene=%s battle=%u completed=%u source=%s\n",
           session->clientId,
           session->onlineRoleId,
           session->sceneHangupScene,
           session->sceneHangupBattleSessionSerial,
           session->sceneHangupCompletedBattles,
            source ? source : "-");
}

static bool vm_net_mock_append_info_banner_text11_object(u8 *out, u32 outCap,
                                                          u32 *pos,
                                                         const char *info)
{
    u32 objectStart = 0;

    /* JianghuOL.CBE's main business dispatcher consumes 25/11.info as GBK.
     * Callers may pass ASCII or explicit GBK bytes, but must never forward a
     * UTF-8 source literal unchanged. */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 11, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 8))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "info", info ? info : ""))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_hangup_battle_start_response(const u8 *request, u32 requestLen,
                                                          u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u32 objectStart = 0;
    const char *scene = NULL;
    const char *matchedScene = NULL;
    u32 requestedEnemyId = 0;
    u32 sceneMonsterIndex = 0;
    u32 sceneMonsterPosX = 0;
    u32 sceneMonsterPosY = 0;
    u8 battleInfo[160];
    u32 battleInfoLen = 0;
    vm_net_mock_request_object moveUpload;
    bool hasMoveUpload = false;
    u8 moveRequest[128];
    u32 moveRequestLen = 0;
    u8 moveResponse[2048];
    u32 moveResponseLen = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleId = 0;
    u32 roleHp = 0;
    u32 roleMaxHp = 0;
    u32 roleMp = 0;
    u32 roleMaxMp = 0;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    bool continuation = request == NULL && requestLen == 0;
    bool requestContractValid = continuation;
    vm_mock_service_client_session *activeSession =
        vm_mock_service_get_active_client_session();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 battleEnemyCount = 1;
    u8 autoFlagType = (u8)vm_net_mock_env_u32("CBE_HANGUP_BATTLE_AUTO_FLAG", 1);

    if (!continuation)
    {
        requestContractValid = vm_net_mock_parse_hangup_battle_start_request(
            request, requestLen, &moveUpload, &hasMoveUpload);
    }

    if (outCap < pos || !requestContractValid)
        return 0;

    if (hasMoveUpload)
    {
        moveRequestLen = vm_net_mock_build_single_object_request(
            &moveUpload, moveRequest, sizeof(moveRequest));
        if (moveRequestLen == 0)
            return 0;
        moveResponseLen = vm_net_mock_build_actor_moveinfo_ack_response(
            moveRequest, moveRequestLen, moveResponse, sizeof(moveResponse));
        if (moveResponseLen == 0)
            return 0;
    }

    scene = vm_net_mock_current_scene_name();
    if (!continuation &&
        vm_net_mock_scene_hangup_restart_waiting(activeSession, scene))
    {
        u32 remainingTicks = activeSession->sceneHangupRestartNotBeforeTick -
                            g_schedulerTick;

        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
            return 0;
        ++objectCount;
        if (moveResponseLen != 0 &&
            !vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount,
                                                 moveResponse, moveResponseLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[info][mock-service] scene_hangup_restart_wait client=%08x "
               "role=%u scene=%s remaining_ticks=%u remaining_ms=%u "
               "response=2/10%s evidence=post-25/5-native-25/3->scene-poll\n",
               activeSession->clientId,
               activeSession->onlineRoleId,
               scene,
               remainingTicks,
               remainingTicks * VM_SCHED_FRAME_MS,
               moveResponseLen != 0 ? "+2/1" : "");
        return pos;
    }
    if (!vm_net_mock_select_auto_monster_for_scene(scene, &requestedEnemyId, &matchedScene))
    {
        if (continuation)
            return 0;
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
            return 0;
        ++objectCount;
        if (!vm_net_mock_append_info_banner_text11_object(out, outCap, &pos,
                                                         "No hangup monster"))
            return 0;
        ++objectCount;
        if (moveResponseLen != 0 &&
            !vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount,
                                                 moveResponse, moveResponseLen))
            return 0;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[warn][network] mock_hangup_battle_start scene=%s action=no-monster move_upload=%u response=2/10+25/11%s resp=%u evidence=JianghuOL.CBE:0x01015E14 Type=2 runtime=2/10+25/3\n",
               scene ? scene : "-", hasMoveUpload ? 1u : 0u,
               moveResponseLen != 0 ? "+2/1" : "", pos);
        vm_autotest_note("mock_hangup_battle_start scene=%s action=no-monster response=2/10+25/11 evidence=JianghuOL.CBE:0x01015E14+0x01010C7E\n",
                         scene ? scene : "-");
        return pos;
    }

    requestedEnemyId = vm_net_mock_normalize_battle_enemy_id(requestedEnemyId);
    /*
     * A 4/10 start has no arbitrary actor-resource field.  Its two visual
     * bytes are consumed by mmBattle 0x66CC as the same job/sex selector used
     * for a remote player in a duel.  The former hangup response supplied the
     * generic 0/1 pair, so the client correctly rendered a player-shaped
     * opponent instead of the selected monster.
     *
     * Hangup has no live-node tuple in its request, but it is still a
     * scene-monster encounter.  Select only a combat spawn from the
     * authoritative server scene resource and derive its runtime scene-node
     * index from that resource's static placement order.  The client receives
     * the same resource through the normal update path.  Passing the ordinal
     * among combat records is invalid: mmBattle 0x66CC indexes the complete
     * scene table, whose earlier rows include prop placements.  Do not fall
     * back to 4/10 if no matching scene spawn is available: that would change
     * the monster encounter into a player-template battle.
     */
    if (!vm_net_mock_select_sce_combat_spawn(scene, requestedEnemyId,
                                             &sceneMonsterIndex,
                                             &sceneMonsterPosX,
                                             &sceneMonsterPosY))
    {
        if (continuation)
            return 0;
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos) ||
            !vm_net_mock_append_info_banner_text11_object(
                out, outCap, &pos, "Monster scene node unavailable"))
        {
            return 0;
        }
        objectCount += 2;
        if (moveResponseLen != 0 &&
            !vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount,
                                                 moveResponse, moveResponseLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[warn][network] mock_hangup_battle_start scene=%s table_scene=%s "
               "enemy=%u action=no-verified-scene-spawn move_upload=%u "
               "response=2/10+25/11%s resp=%u evidence=4/5-requires-live-scene-node\n",
               scene ? scene : "-", matchedScene ? matchedScene : "-",
               requestedEnemyId, hasMoveUpload ? 1u : 0u,
               moveResponseLen != 0 ? "+2/1" : "", pos);
        vm_autotest_note("mock_hangup_battle_start scene=%s enemy=%u "
                         "action=no-verified-scene-spawn response=2/10+25/11\n",
                         scene ? scene : "-", requestedEnemyId);
        return pos;
    }

    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    roleId = vm_net_mock_env_u32("CBE_BATTLE_ROLE_ID",
                                 role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID);
    roleHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", roleHpDefault);
    roleMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", roleMaxHpDefault);
    roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", roleMpDefault);
    roleMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_MP", roleMaxMpDefault);
    if (roleMaxHp < roleMaxHpDefault)
        roleMaxHp = roleMaxHpDefault;
    if (roleMaxMp < roleMaxMpDefault)
        roleMaxMp = roleMaxMpDefault;
    if (roleHp == 0)
    {
        if (continuation)
            return 0;
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos) ||
            !vm_net_mock_append_info_banner_text11_object(out, outCap, &pos,
                                                          "\xC4\xFA\xD2\xD1\xBE\xAD\xCB\xC0\xCD\xF6\xA3\xAC\xC7\xEB\xCF\xC8\xCA\xB9\xD3\xC3\xB8\xB4\xBB\xEE\xCA\xAF")) /* 您已经死亡，请先使用复活石 */
        {
            return 0;
        }
        objectCount += 2;
        if (moveResponseLen != 0 &&
            !vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount,
                                                 moveResponse, moveResponseLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[info][network] mock_hangup_battle_start roleid=%u action=reject-dead rolehp=0 response=2/10+25/11%s\n",
               roleId, moveResponseLen != 0 ? "+2/1" : "");
        return pos;
    }
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;

    /* `HandleBattleEnterReq(0x01015E14)` sends this request only after
     * writing its battle-entry state.  There is no matching response-side
     * failure handler for the request marker: `25/11` only updates a banner
     * and leaves that state at “fetching data”.  Reward eligibility is a
     * settlement concern, not a battle-start gate.  Always complete a valid
     * `2/10(Type=2)+25/3` request with the proven scene start family below;
     * the transactional reward claim still suppresses duplicate awards in
     * the eight-second window. */

    /*
     * Use the same scene-monster grammar as a normal collision battle.  The
     * selected spawn carries its complete runtime scene-node index, not merely
     * a combat-record ordinal.  It is also the first active actor-id match
     * used by the 2/2 moveinfo parser, so its HP/MP seed and the 4/5 visual
     * source refer to the same node.
     */
    if (!continuation)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    battleEnemyCount = vm_net_mock_battle_roll_enemy_count(true);
    battleInfoLen = vm_net_mock_build_battle_scene_start_info_blob(
        battleInfo, sizeof(battleInfo), sceneMonsterIndex,
        sceneMonsterPosX, sceneMonsterPosY, battleEnemyCount, roleId);
    if (battleInfoLen == 0 || battleInfoLen > 0xffff)
        return 0;

    if (!vm_net_mock_append_scene_monster_moveinfo2_object(
            out, outCap, &pos, requestedEnemyId,
            sceneMonsterPosX, sceneMonsterPosY))
    {
        return 0;
    }
    ++objectCount;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, 5, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "side", battleSide))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "battleinfo", battleInfo, (u16)battleInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    ++objectCount;
    if (autoFlagType != 0)
    {
        if (!vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, autoFlagType))
            return 0;
        ++objectCount;
    }
    if (moveResponseLen != 0 &&
        !vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount,
                                             moveResponse, moveResponseLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    g_mockBattleOperateSessionArmed = 1;
    if (autoFlagType != 0)
        vm_net_mock_battle_auto_arm();
    else
        vm_net_mock_battle_auto_reset();
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleEnemyCountCurrent = battleEnemyCount;
    g_mockBattleOperateTurnCounter = 0;
    memset(&g_vm_net_mock_battle_solo_modifier, 0,
           sizeof(g_vm_net_mock_battle_solo_modifier));
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    memset(g_vm_net_mock_battle_enemy_effects_current, 0,
           sizeof(g_vm_net_mock_battle_enemy_effects_current));
    g_vm_net_mock_battle_enemy_effect_changed_mask = 0;
    memset(&g_vm_net_mock_battle_active_enemy_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_enemy_modifier_current));
    ++g_mockBattleOperateSessionSerial;
    g_vm_net_mock_battle_rewarded_exp = 0;
    memset(g_vm_net_mock_battle_rewarded_drops, 0,
           sizeof(g_vm_net_mock_battle_rewarded_drops));
    g_vm_net_mock_battle_rewarded_drop_result_count = 0;
    g_vm_net_mock_battle_settlement_sent_serial = 0;
    g_vm_net_mock_battle_terminal_close_not_before_tick = 0;
    g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
    g_vm_net_mock_battle_recovered_serial = 0;
    g_vm_net_mock_battle_role_id_current = roleId != 0 ? roleId :
                                           (role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID);
    g_mockBattleRoleHpCurrent = roleHp;
    g_mockBattleRoleHpMax = roleMaxHp;
    if (g_mockBattleRoleHpMax < g_mockBattleRoleHpCurrent)
        g_mockBattleRoleHpMax = g_mockBattleRoleHpCurrent;
    g_mockBattleRoleMpCurrent = roleMp;
    g_mockBattleRoleMpMax = roleMaxMp;
    if (g_mockBattleRoleMpMax < g_mockBattleRoleMpCurrent)
        g_mockBattleRoleMpMax = g_mockBattleRoleMpCurrent;
    g_vm_net_mock_battle_enemy_id_current = requestedEnemyId;
    vm_net_mock_battle_reset_enemy_hp_from_stats(requestedEnemyId);
    vm_net_mock_role_record_rapid_battle_entry(
        role, continuation ? "scene-hangup-continuation" : "scene-hangup-request",
        scene, requestedEnemyId);
    vm_net_mock_scene_hangup_record_battle_start(activeSession, scene,
                                                 continuation ? "scene-poll" : "request");

    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(requestedEnemyId);
        u32 perEnemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
        u32 perEnemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", perEnemyHp);
        if (perEnemyMaxHp < perEnemyHp)
            perEnemyMaxHp = perEnemyHp;
        printf("[info][network] mock_hangup_battle_start source=%s tick=%u scene=%s table_scene=%s enemy=%u enemies=%u roleid=%u rolehp=%u/%u rolemp=%u/%u enemyhp=%u/%u per_enemy_hp=%u/%u subtype=5 runtime_index=%u pos=(%u,%u) target_source=sce-static-node-order auto=%u move_upload=%u objects=%u resp=%u evidence=JianghuOL.CBE:0x01015E14 Type=2 + runtime:2/10+25/3(+2/1) + automonster.dsh + SCE2 + mmBattle:0x66CC\n",
               continuation ? "scene-poll" : "request",
               g_schedulerTick,
               scene ? scene : "-",
               matchedScene ? matchedScene : "-",
               requestedEnemyId,
               battleEnemyCount,
               g_vm_net_mock_battle_role_id_current,
               g_mockBattleRoleHpCurrent,
               g_mockBattleRoleHpMax,
               g_mockBattleRoleMpCurrent,
               g_mockBattleRoleMpMax,
               g_mockBattleEnemyHpCurrent,
               g_mockBattleEnemyHpMax,
               perEnemyHp,
               perEnemyMaxHp,
               sceneMonsterIndex,
               sceneMonsterPosX,
               sceneMonsterPosY,
               autoFlagType,
               hasMoveUpload ? 1u : 0u,
               objectCount,
               pos);
        vm_autotest_note("mock_hangup_battle_start source=%s tick=%u scene=%s enemy=%u enemies=%u subtype=5 runtime_index=%u pos=(%u,%u) target_source=sce-static-node-order auto=%u response=%s evidence=JianghuOL.CBE:0x01015E14 SCE2 mmBattle:0x66CC\n",
                          continuation ? "scene-poll" : "request",
                          g_schedulerTick,
                          scene ? scene : "-",
                         requestedEnemyId,
                         battleEnemyCount,
                         sceneMonsterIndex,
                         sceneMonsterPosX,
                         sceneMonsterPosY,
                         autoFlagType,
                         continuation ? "2/2+4/5+4/11" : "2/10+2/2+4/5+4/11");
    }
    return pos;
}

/* The client sends its empty scene-default 25/5 only after dismissing the
 * native rewarded-result panel and removing the battle screen.  It is the
 * first point where a new 4/5 can be delivered safely. */
static void vm_net_mock_scene_hangup_on_scene_default_event(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->sceneHangupEnabled)
        return;
    if (!vm_net_mock_scene_name_is_safe(session->sceneHangupScene) ||
        !vm_mock_service_session_scene_is_visible(session,
                                                  session->sceneHangupScene))
    {
        vm_mock_service_session_clear_scene_hangup(session, "scene-changed");
        return;
    }
    if (session->sceneHangupRestartPending)
        return;
    if (session->sceneHangupBattleSessionSerial !=
            g_mockBattleOperateSessionSerial ||
        session->onlineRoleId == 0 ||
        g_vm_net_mock_battle_role_id_current != session->onlineRoleId)
    {
        vm_mock_service_session_clear_scene_hangup(session,
                                                   "battle-session-mismatch");
        return;
    }
    /* A generic scene 25/5 can occur while a battle is still live.  Only the
     * terminal scene-monster state is a completed hangup round. */
    if (g_mockBattleOperateSessionArmed != 0 ||
        g_mockBattleAwaitingSettlement == 0)
    {
        return;
    }
    if (g_mockBattleSceneMonsterStartActive == 0 ||
        g_mockBattleEnemyHpCurrent != 0 || g_mockBattleRoleHpCurrent == 0)
    {
        vm_mock_service_session_clear_scene_hangup(
            session,
            g_mockBattleRoleHpCurrent == 0 ? "battle-lost" :
                                               "battle-not-victory");
        return;
    }

    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        bool battleInsightActive = false;

        if (role != NULL && role->roleId == session->onlineRoleId &&
            vm_net_mock_role_active_battle_exp_bonus_percent(role) != 0)
        {
            battleInsightActive = true;
        }
        if (session->sceneHangupCompletedBattles < 0xffffu)
            ++session->sceneHangupCompletedBattles;
        if (battleInsightActive &&
            session->sceneHangupCompletedBattles >=
                VM_NET_MOCK_SCENE_HANGUP_INSIGHT_MAX_BATTLES)
        {
            printf("[info][mock-service] scene_hangup_limit_reached client=%08x role=%u "
                   "scene=%s completed=%u max=%u insight=%u action=stop-after-native-result-close\n",
                   session->clientId, session->onlineRoleId,
                   session->sceneHangupScene,
                   session->sceneHangupCompletedBattles,
                   VM_NET_MOCK_SCENE_HANGUP_INSIGHT_MAX_BATTLES, 1u);
            vm_mock_service_session_clear_scene_hangup(session, "battle-limit");
            return;
        }
    }

    session->sceneHangupRestartPending = true;
    session->sceneHangupRestartNotBeforeTick =
        g_schedulerTick + VM_NET_MOCK_SCENE_HANGUP_RESTART_DELAY_TICKS;
    printf("[info][mock-service] scene_hangup_round_complete client=%08x "
           "role=%u scene=%s battle=%u completed=%u next_tick=%u delay_ms=%u "
           "evidence=4/7-panel->25/5->poll\n",
           session->clientId,
           session->onlineRoleId,
           session->sceneHangupScene,
           session->sceneHangupBattleSessionSerial,
           session->sceneHangupCompletedBattles,
           session->sceneHangupRestartNotBeforeTick,
           VM_NET_MOCK_SCENE_HANGUP_RESTART_DELAY_MS);
}

static u32 vm_net_mock_build_pending_scene_hangup_battle_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer)
{
    u32 responseLen = 0;

    if (out == NULL || outCap < 5 || observer == NULL ||
        !observer->sceneHangupEnabled ||
        !observer->sceneHangupRestartPending)
    {
        return 0;
    }
    if (!vm_net_mock_scene_name_is_safe(observer->sceneHangupScene) ||
        !vm_mock_service_session_scene_is_visible(observer,
                                                  observer->sceneHangupScene))
    {
        vm_mock_service_session_clear_scene_hangup(observer, "scene-changed");
        return 0;
    }
    if (observer->onlineRoleId == 0 ||
        observer->sceneHangupBattleSessionSerial == 0 ||
        observer->sceneHangupBattleSessionSerial !=
            g_mockBattleOperateSessionSerial ||
        g_vm_net_mock_battle_role_id_current != observer->onlineRoleId)
    {
        vm_mock_service_session_clear_scene_hangup(observer,
                                                   "battle-session-mismatch");
        return 0;
    }
    if (g_schedulerTick < observer->sceneHangupRestartNotBeforeTick ||
        g_mockBattleOperateSessionArmed != 0)
    {
        return 0;
    }

    /* This is not a replayed 2/10 request.  The direct builder has one shared
     * continuation mode that emits only the native scene-side start objects:
     * 2/2 seed, 4/5 start, and 4/11 automatic flag. */
    responseLen = vm_net_mock_build_hangup_battle_start_response(
        NULL, 0, out, outCap);
    if (responseLen == 0)
    {
        vm_mock_service_session_clear_scene_hangup(observer,
                                                   "next-scene-monster-unavailable");
        return 0;
    }
    return responseLen;
}

/*
 * Once BattleScene has sent its empty 25/5, a following normal scene
 * collision request is an equally valid native continuation boundary as a
 * later scene-sync poll.  It must inherit the armed scene-hangup session;
 * otherwise the generic collision builder increments the battle serial and
 * the next poll sees a false session mismatch.
 *
 * Keep this deliberately stricter than a generic "hangup enabled" check:
 * it accepts only the completed, rewarded scene-monster session that the
 * 25/5 handler has already marked restart-pending.  The collision itself is
 * already a native BattleScene entry boundary, so it must be adopted even
 * when it arrives before the poll-only five-second deadline; deferring it
 * would leave the client waiting for a 4/5 while replacing the saved hangup
 * session with a generic battle.  The five-second delay remains the fallback
 * only when no collision request arrives.  Scene names are compared exactly
 * so similarly named c00/00 resources can never share a continuation.
 */
static bool vm_net_mock_scene_hangup_can_adopt_native_challenge(
    vm_mock_service_client_session *session,
    const char *scene,
    u32 roleId,
    bool useSceneMonsterStart)
{
    if (!useSceneMonsterStart || session == NULL || scene == NULL ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !session->sceneHangupEnabled ||
        !session->sceneHangupRestartPending ||
        !session->sceneVisibleReady || session->sceneVisiblePending ||
        session->onlineRoleId == 0 || roleId == 0 ||
        session->onlineRoleId != roleId ||
        session->sceneHangupBattleSessionSerial == 0 ||
        session->sceneHangupBattleSessionSerial !=
            g_mockBattleOperateSessionSerial ||
        g_vm_net_mock_battle_role_id_current != roleId ||
        g_mockBattleOperateSessionArmed != 0 ||
        g_mockBattleAwaitingSettlement == 0 ||
        g_mockBattleSceneMonsterStartActive == 0 ||
        g_mockBattleEnemyHpCurrent != 0 || g_mockBattleRoleHpCurrent == 0 ||
        strcmp(session->sceneHangupScene, scene) != 0 ||
        strcmp(session->sceneVisibleScene, scene) != 0)
    {
        return false;
    }

    /* Team battles own a different start/turn contract.  The scene-hangup
     * loop is currently a solo scene-monster feature, so never retrofit this
     * continuation onto an existing team state. */
    return vm_mock_service_team_find_for_client(session->clientId) == NULL;
}

/*
 * A 2/2+4/5+4/11 continuation is emitted from the scene-poll channel.  A
 * scene collision that was already queued before BattleScene consumed that
 * start may then arrive as a normal 4/1.  It is not a second encounter: the
 * first 4/5 has already armed this exact session and the client has already
 * scheduled its automatic 4/12.  Treating the late 4/1 as a fresh battle
 * used to replace the live state with a non-auto session; the queued 4/12
 * consequently became unhandled and BattleScene lost its right-side actor.
 *
 * This predicate is deliberately narrower than generic battle-in-progress:
 * it applies only to a visible, solo, live scene-hangup battle owned by the
 * same role and scene.  It never suppresses a terminal panel, a restart, a
 * manual collision outside hangup, or team battle handling.
 */
static bool vm_net_mock_scene_hangup_live_auto_battle(
    const vm_mock_service_client_session *session,
    const char *scene,
    u32 roleId,
    bool useSceneMonsterStart)
{
    return useSceneMonsterStart && session != NULL && scene != NULL &&
           vm_net_mock_scene_name_is_safe(scene) &&
           session->sceneHangupEnabled &&
           !session->sceneHangupRestartPending &&
           session->sceneVisibleReady && !session->sceneVisiblePending &&
           session->onlineRoleId != 0 && roleId != 0 &&
           session->onlineRoleId == roleId &&
           session->sceneHangupBattleSessionSerial != 0 &&
           session->sceneHangupBattleSessionSerial ==
               g_mockBattleOperateSessionSerial &&
           g_vm_net_mock_battle_role_id_current == roleId &&
           g_mockBattleOperateSessionArmed != 0 &&
           g_mockBattleOperateSessionFinished == 0 &&
           g_mockBattleAwaitingSettlement == 0 &&
           g_mockBattleSceneMonsterStartActive != 0 &&
           g_mockBattleEnemyHpCurrent != 0 &&
           g_mockBattleRoleHpCurrent != 0 &&
           g_vm_net_mock_battle_auto_enabled != 0 &&
           strcmp(session->sceneHangupScene, scene) == 0 &&
           strcmp(session->sceneVisibleScene, scene) == 0 &&
           vm_mock_service_team_find_for_client(session->clientId) == NULL;
}

static u32 vm_net_mock_build_challenge_interaction_response_ex(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap,
    bool forceNonSceneStart, bool forceSceneMonsterStart)
{
    u32 pos = 5;
    u32 objectStart = 0;
    const u8 *moveInfo = NULL;
    u16 moveInfoLen = 0;
    bool hasMoveinfo = false;
    u8 battleInfo[160];
    u32 battleInfoLen = 0;
    u32 id = 0;
    u32 requestedEnemyId = 0;
    u32 enemyWireId = 0;
    u32 enemyTable = 0;
    u32 enemyTableIds[4] = {0};
    u32 index = 0;
    u32 posx = 0;
    u32 posy = 0;
    u32 sceneMonsterIndex = 0;
    u32 sceneMonsterPosX = 0;
    u32 sceneMonsterPosY = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleId = 0;
    u32 roleHp = 0;
    u32 roleMaxHp = 0;
    u32 roleMp = 0;
    u32 roleMaxMp = 0;
    vm_mock_service_client_session *activeSession =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *activeTeam = NULL;
    const char *teamBattleScene = NULL;
    u8 teamBattlePartyCount = 0;
    u8 teamBattleQueuedCount = 0;
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    bool useSceneMonsterStart = forceSceneMonsterStart ||
                                (!forceNonSceneStart && playerOnRight &&
                                 vm_net_mock_env_u8("CBE_BATTLE_SCENE_MONSTER_START", 1) != 0);
    u8 battleStartSubtype = useSceneMonsterStart ? 5 : 10;
    bool seedSceneMonsterMoveinfo = useSceneMonsterStart &&
                                    vm_net_mock_env_u8("CBE_BATTLE_SCENE_MONSTER_MOVEINFO", 1) != 0;
    const char *sceneMonsterTargetSource = useSceneMonsterStart
                                               ? "request-live-node"
                                               : "not-applicable";
    u8 battleEnemyCount = 1;
    bool prefillEnemyTemplate = false;
    bool prefillPlayerTemplate = false;
    bool continueSceneHangup = false;
    bool continueSceneHangupBeforePollDeadline = false;
    int teamBattleRosterPreambleCount = 0;
    u8 hangupAutoFlagType = (u8)vm_net_mock_env_u32(
        "CBE_HANGUP_BATTLE_AUTO_FLAG", 1);
    u32 responseObjectCount = 1;
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();

    if (outCap < pos || (forceNonSceneStart && forceSceneMonsterStart) ||
        !vm_net_mock_is_challenge_interaction_request(request, requestLen))
        return 0;
    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    roleId = vm_net_mock_env_u32("CBE_BATTLE_ROLE_ID",
                                 role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID);
    roleHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP", roleHpDefault);
    roleMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_HP", roleMaxHpDefault);
    roleMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MP", roleMpDefault);
    roleMaxMp = vm_net_mock_env_u32("CBE_BATTLE_ROLE_MAX_MP", roleMaxMpDefault);
    if (roleMaxHp < roleMaxHpDefault)
        roleMaxHp = roleMaxHpDefault;
    if (roleMaxMp < roleMaxMpDefault)
        roleMaxMp = roleMaxMpDefault;
    if (roleHp == 0)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos) ||
            !vm_net_mock_append_info_banner_text11_object(out, outCap, &pos,
                                                          "\xC4\xFA\xD2\xD1\xBE\xAD\xCB\xC0\xCD\xF6\xA3\xAC\xC7\xEB\xCF\xC8\xCA\xB9\xD3\xC3\xB8\xB4\xBB\xEE\xCA\xAF")) /* 您已经死亡，请先使用复活石 */
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, 2);
        printf("[info][network] mock_challenge_battle_start roleid=%u action=reject-dead rolehp=0 response=2/10+25/11\n",
               roleId);
        return pos;
    }
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;
    battleEnemyCount = vm_net_mock_battle_roll_enemy_count(useSceneMonsterStart);
    hasMoveinfo = vm_net_mock_get_object_blob_field(request, requestLen, "moveinfo", &moveInfo, &moveInfoLen);
    if (hasMoveinfo)
        (void)vm_net_mock_snapshot_current_player_pos("moveinfo-upload-combo");
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "id", &id);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "index", &index);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posx", &posx);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posy", &posy);

    continueSceneHangup =
        vm_net_mock_scene_hangup_can_adopt_native_challenge(
            activeSession, vm_net_mock_current_scene_name(), roleId,
            useSceneMonsterStart);
    if (continueSceneHangup &&
        g_schedulerTick < activeSession->sceneHangupRestartNotBeforeTick)
    {
        continueSceneHangupBeforePollDeadline = true;
    }

    if (vm_net_mock_scene_hangup_live_auto_battle(
            activeSession, vm_net_mock_current_scene_name(), roleId,
            useSceneMonsterStart))
    {
        /* `HandleServerBattleCmd(0x7BD0)` accepts 4/11 without rebuilding
         * battle slots.  Reaffirm the already parsed automatic state instead
         * of serialising another 4/5 for a queued scene collision. */
        if (!vm_net_mock_append_battle_case11_auto_flag_object(
                out, outCap, &pos, 1))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, 1);
        printf("[info][network] mock_scene_hangup_duplicate_challenge_reaffirm "
               "client=%08x role=%u scene=%s session=%u enemy=%u "
               "index=%u pos=(%u,%u) request=4/1 response=4/11(type=1) "
               "action=retain-live-auto-battle evidence=mmBattle:0x7BD0(case11)\n",
               activeSession ? activeSession->clientId : 0,
               roleId,
               vm_net_mock_current_scene_name(),
               g_mockBattleOperateSessionSerial,
               id,
               index,
               posx,
               posy);
        return pos;
    }

    sceneMonsterIndex = index;
    sceneMonsterPosX = posx;
    sceneMonsterPosY = posy;
    requestedEnemyId = vm_net_mock_normalize_battle_enemy_id(id);
    /* A genuine non-scene instance challenge has no client-owned scene row
     * and stays on 4/10.  The action13 scene-monster path reaches this builder
     * only after its client index was checked against the current SCE spawn;
     * its synthetic request restores that exact spawn's x/y and forces 4/5.
     * A generic collision already supplies the same live tuple directly. */
    if (forceNonSceneStart)
    {
        printf("[info][network] mock_direct_challenge_start client=%08x scene=%s "
               "enemy=%u action=retain-4/10 reason=isolated-instance-contract "
               "evidence=mmBattle:0x67AC\n",
               activeSession != NULL ? activeSession->clientId : 0,
               activeSession != NULL ? activeSession->sceneVisibleScene : "-",
               requestedEnemyId);
    }
    else
    {
        /* A collision supplies the live tuple itself.  The validated action13
         * path reconstructs that same tuple; both remain authoritative because
         * duplicate monster ids are legal. */
        sceneMonsterIndex = index;
        sceneMonsterPosX = posx;
        sceneMonsterPosY = posy;
    }
    /* A boss is one durable opponent.  Group play is encouraged by its
     * authoritative stats rather than by refusing an otherwise valid combat
     * request; never let the normal random encounter roll clone it. */
    if (vm_net_mock_monster_family_for_enemy(requestedEnemyId) ==
        VM_NET_MOCK_MONSTER_BOSS)
    {
        battleEnemyCount = 1;
    }
    g_vm_net_mock_battle_enemy_id_current = requestedEnemyId;
    enemyWireId = vm_net_mock_resolve_battle_enemy_id(requestedEnemyId, &enemyTable, enemyTableIds);
    /*
     * The normal collision request's index/posx/posy is the client's live
     * scene-node tuple consumed by mmBattle subtype 5.  Do not replace it with
     * the first same-actor-id SCE row: duplicate actor ids are legal, and the
     * offline row can be absent from the client's current 25-node table.
     *
     * The preceding 2/2 object still seeds HP/MP through the client's
     * actor-id lookup.  Its outer x/y updates node+24/+26 and cannot establish
     * the node+240/+244 coordinate contract used by HandleBattleStartMsg.
     */
    if (playerOnRight && !useSceneMonsterStart)
        enemyWireId = requestedEnemyId;
    prefillEnemyTemplate = !playerOnRight &&
                           vm_net_mock_env_u8("CBE_BATTLE_PREFILL_ENEMY_TEMPLATE", 0) != 0 &&
                           requestedEnemyId != 0 &&
                           (enemyWireId != requestedEnemyId || requestedEnemyId != id);
    if (prefillEnemyTemplate)
        enemyWireId = requestedEnemyId;
    /* A late 5/5 here is parsed by the scene group handler before battle
     * transition completes and can expose a transient row to the team HUD.
     * Login 5/10 also stays empty for a role that is not actually in a team;
     * keep this experiment off until a battle-only template contract exists. */
    prefillPlayerTemplate = playerOnRight &&
                            vm_net_mock_env_u8("CBE_BATTLE_PREFILL_PLAYER_TEMPLATE", 0) != 0 &&
                            roleId != 0;
    if (useSceneMonsterStart)
    {
        if (activeSession != NULL &&
            activeSession->sceneVisibleReady &&
            !activeSession->sceneVisiblePending &&
            vm_net_mock_scene_name_is_safe(activeSession->sceneVisibleScene))
        {
            activeTeam = vm_mock_service_team_find_for_client(activeSession->clientId);
            if (vm_mock_service_team_is_leader(activeTeam, activeSession->clientId))
                teamBattleScene = activeSession->sceneVisibleScene;
        }
        if (activeTeam != NULL && teamBattleScene != NULL)
        {
            battleInfoLen = vm_net_mock_build_team_battle_scene_start_info_blob(
                battleInfo, sizeof(battleInfo),
                sceneMonsterIndex,
                sceneMonsterPosX,
                sceneMonsterPosY,
                battleEnemyCount,
                activeTeam,
                activeSession,
                teamBattleScene,
                &teamBattlePartyCount);
        }
        else
        {
            battleInfoLen = vm_net_mock_build_battle_scene_start_info_blob(
                battleInfo, sizeof(battleInfo),
                sceneMonsterIndex,
                sceneMonsterPosX,
                sceneMonsterPosY,
                battleEnemyCount,
                roleId);
        }
    }
    else
    {
        battleInfoLen = vm_net_mock_build_battle_start_info_blob(battleInfo, sizeof(battleInfo),
                                                                 roleId, enemyWireId,
                                                                 playerOnRight);
    }
    if (battleInfoLen == 0 || battleInfoLen > 0xffff)
        return 0;

    /* The right-side ids in subtype-5 battleinfo are resolved only from the
     * CBE's imported group roster.  If the accepted-invite 5/4 -> 5/5 delta
     * is still queued, serialize that exact native delta before 2/2 + 4/5;
     * otherwise this 4/1 can overtake it and leave an unresolved right slot. */
    if (useSceneMonsterStart && activeTeam != NULL && activeSession != NULL &&
        teamBattleScene != NULL && teamBattlePartyCount >= 2)
    {
        teamBattleRosterPreambleCount =
            vm_net_mock_append_team_battle_roster_preamble(
                out, outCap, &pos, activeSession, activeTeam);
        if (teamBattleRosterPreambleCount < 0 ||
            teamBattleRosterPreambleCount > (int)(0xffu - responseObjectCount))
        {
            return 0;
        }
        responseObjectCount += (u32)teamBattleRosterPreambleCount;
    }

    if (prefillEnemyTemplate)
    {
        if (!vm_net_mock_append_battle_enemy_template_prefill_object(out, outCap, &pos, requestedEnemyId))
            return 0;
        ++responseObjectCount;
    }
    if (prefillPlayerTemplate)
    {
        u8 playerTemplateByte34 = vm_net_mock_env_u8(
            "CBE_BATTLE_PLAYER_TEMPLATE_BYTE34",
            role != NULL && role->sex <= 1 ? (u8)(role->sex + 1) : 1);
        u8 playerTemplateByte35 = vm_net_mock_env_u8(
            "CBE_BATTLE_PLAYER_TEMPLATE_BYTE35",
            role != NULL && role->job >= 1 && role->job <= 3 ? (u8)(role->job - 1) : 0);
        const char *playerTemplateName = vm_net_mock_env_str("CBE_BATTLE_PLAYER_TEMPLATE_NAME", roleName);

        if (!vm_net_mock_append_battle_template_prefill_object_ex(out, outCap, &pos,
                                                                  roleId,
                                                                  playerTemplateName,
                                                                  playerTemplateByte34,
                                                                  playerTemplateByte35,
                                                                  roleHp,
                                                                  roleMaxHp,
                                                                  roleMp,
                                                                  roleMaxMp))
            return 0;
        ++responseObjectCount;
    }
    if (seedSceneMonsterMoveinfo)
    {
        if (!vm_net_mock_append_scene_monster_moveinfo2_object(out, outCap, &pos,
                                                              requestedEnemyId,
                                                              sceneMonsterPosX,
                                                              sceneMonsterPosY))
            return 0;
        ++responseObjectCount;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, battleStartSubtype, &objectStart))
        return 0;
    /*
     * In the default player-on-right layout, side=1 makes wire slot 0 remap to
     * the right-side role record and wire slot 1 remap to the left monster.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "side", battleSide))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "battleinfo", battleInfo, (u16)battleInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (continueSceneHangup && hangupAutoFlagType != 0)
    {
        if (!vm_net_mock_append_battle_case11_auto_flag_object(
                out, outCap, &pos, hangupAutoFlagType))
        {
            return 0;
        }
        ++responseObjectCount;
    }
    if (hasMoveinfo)
    {
        if (!vm_net_mock_append_actor_moveinfo_empty_ack_object(out, outCap, &pos))
            return 0;
        ++responseObjectCount;
    }
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
    g_mockBattleOperateSessionArmed = 1;
    if (continueSceneHangup && hangupAutoFlagType != 0)
        vm_net_mock_battle_auto_arm();
    else
        vm_net_mock_battle_auto_reset();
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    g_mockBattleSceneMonsterStartActive = useSceneMonsterStart ? 1 : 0;
    g_mockBattleEnemyCountCurrent = useSceneMonsterStart ? battleEnemyCount : 1;
    g_mockBattleOperateTurnCounter = 0;
    memset(&g_vm_net_mock_battle_solo_modifier, 0,
           sizeof(g_vm_net_mock_battle_solo_modifier));
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    memset(g_vm_net_mock_battle_enemy_effects_current, 0,
           sizeof(g_vm_net_mock_battle_enemy_effects_current));
    g_vm_net_mock_battle_enemy_effect_changed_mask = 0;
    memset(&g_vm_net_mock_battle_active_enemy_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_enemy_modifier_current));
    ++g_mockBattleOperateSessionSerial;
    g_vm_net_mock_battle_rewarded_exp = 0;
    memset(g_vm_net_mock_battle_rewarded_drops, 0,
           sizeof(g_vm_net_mock_battle_rewarded_drops));
    g_vm_net_mock_battle_rewarded_drop_result_count = 0;
    g_vm_net_mock_battle_settlement_sent_serial = 0;
    g_vm_net_mock_battle_terminal_close_not_before_tick = 0;
    g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
    g_vm_net_mock_battle_recovered_serial = 0;
    g_vm_net_mock_battle_role_id_current = roleId != 0 ? roleId :
                                           (role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID);
    g_mockBattleRoleHpCurrent = roleHp;
    g_mockBattleRoleHpMax = roleMaxHp;
    if (g_mockBattleRoleHpMax < g_mockBattleRoleHpCurrent)
        g_mockBattleRoleHpMax = g_mockBattleRoleHpCurrent;
    g_mockBattleRoleMpCurrent = roleMp;
    g_mockBattleRoleMpMax = roleMaxMp;
    if (g_mockBattleRoleMpMax < g_mockBattleRoleMpCurrent)
        g_mockBattleRoleMpMax = g_mockBattleRoleMpCurrent;
    if (continueSceneHangup)
    {
        vm_net_mock_scene_hangup_record_battle_start(
            activeSession, vm_net_mock_current_scene_name(),
            "native-challenge-continuation");
    }
    vm_net_mock_role_record_rapid_battle_entry(
        role, continueSceneHangup ? "scene-hangup-challenge" : "scene-challenge",
        vm_net_mock_current_scene_name(), requestedEnemyId);
    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(requestedEnemyId);
        u32 perEnemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
        u32 perEnemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", perEnemyHp);
        if (perEnemyMaxHp < perEnemyHp)
            perEnemyMaxHp = perEnemyHp;
        vm_net_mock_battle_reset_enemy_hp_from_stats(requestedEnemyId);
        printf("[info][network] mock_challenge_battle_start id=%u requested=%u roleid=%u enemies=%u rolehp=%u/%u rolemp=%u/%u enemyhp=%u/%u per_enemy_hp=%u/%u enemymp=%u subtype=%u side=%u scene_start=%u index=%u pos=(%u,%u) req_index=%u req_pos=(%u,%u) target_source=%s prefill_player=%u prefill_enemy=%u hangup_continue=%u before_poll_deadline=%u auto=%u objects=%u\n",
               id, requestedEnemyId,
               g_vm_net_mock_battle_role_id_current,
               vm_net_mock_battle_enemy_count_current(),
               g_mockBattleRoleHpCurrent,
               g_mockBattleRoleHpMax,
               g_mockBattleRoleMpCurrent,
               g_mockBattleRoleMpMax,
               g_mockBattleEnemyHpCurrent,
               g_mockBattleEnemyHpMax,
               perEnemyHp,
               perEnemyMaxHp,
               vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", stats.mp),
               battleStartSubtype, battleSide, useSceneMonsterStart ? 1 : 0,
               sceneMonsterIndex, sceneMonsterPosX, sceneMonsterPosY,
               index, posx, posy,
               sceneMonsterTargetSource,
               prefillPlayerTemplate ? 1u : 0u,
               prefillEnemyTemplate ? 1u : 0u,
               continueSceneHangup ? 1u : 0u,
               continueSceneHangupBeforePollDeadline ? 1u : 0u,
               continueSceneHangup ? (u32)hangupAutoFlagType : 0u,
               responseObjectCount);
        vm_autotest_note("mock_challenge_battle_start id=%u requested=%u roleid=%u enemies=%u wire=%u level=%u hp=%u/%u perhp=%u/%u rolemp=%u/%u enemymp=%u atk=%u def=%u exp=%u gold=%u index=%u pos=(%u,%u) reqIndex=%u reqPos=(%u,%u) target_source=%s subtype=%u side=%u scene_start=%u hangup_continue=%u auto=%u table=%08x ids=%u/%u/%u/%u\n",
                         id, requestedEnemyId,
                         g_vm_net_mock_battle_role_id_current,
                         vm_net_mock_battle_enemy_count_current(),
                         enemyWireId,
                         stats.level,
                         g_mockBattleEnemyHpCurrent,
                         g_mockBattleEnemyHpMax,
                         perEnemyHp,
                         perEnemyMaxHp,
                         g_mockBattleRoleMpCurrent,
                         g_mockBattleRoleMpMax,
                         vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MP", stats.mp),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_ATTACK", stats.attack),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_DEFENSE", stats.defense),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_EXP", stats.exp),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_GOLD", stats.gold),
                         sceneMonsterIndex, sceneMonsterPosX, sceneMonsterPosY,
                         index, posx, posy,
                         sceneMonsterTargetSource,
                         battleStartSubtype, battleSide, useSceneMonsterStart ? 1 : 0,
                         continueSceneHangup ? 1u : 0u,
                         continueSceneHangup ? (u32)hangupAutoFlagType : 0u,
                          enemyTable, enemyTableIds[0], enemyTableIds[1],
                          enemyTableIds[2], enemyTableIds[3]);
    }
    if (useSceneMonsterStart && activeTeam != NULL && activeSession != NULL &&
        teamBattleScene != NULL && teamBattlePartyCount >= 2)
    {
        (void)vm_mock_service_team_begin_battle(
            activeTeam,
            activeSession,
            teamBattleScene,
            requestedEnemyId,
            sceneMonsterIndex,
            sceneMonsterPosX,
            sceneMonsterPosY,
            battleEnemyCount,
            battleSide,
            &teamBattleQueuedCount);
        printf("[info][network] mock_team_battle_start leader=%08x party=%u "
               "queued=%u roster_preamble=%d response=%u source=leader-4/1\n",
               activeSession->clientId,
               teamBattlePartyCount,
               teamBattleQueuedCount,
               teamBattleRosterPreambleCount,
               pos);
    }
    return pos;
}

