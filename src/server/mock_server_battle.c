static bool vm_net_mock_append_battle_terminal_status_objects(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
    bool forceTeamVictory);
static void vm_mock_service_session_arm_battle_revival_confirm_for_death(
    const char *reason);
static void vm_net_mock_battle_clear_last_operate(void);
static void vm_net_mock_battle_reset_last_operate_target(void);
static void vm_net_mock_battle_suspend_solo_auto_for_team(const char *reason);
static bool vm_net_mock_active_session_in_team_battle(void);
static void vm_net_mock_battle_auto_note_client_operate(void);
static bool vm_net_mock_battle_operate_is_skill(u32 operate);
static u32 vm_net_mock_battle_operate_skill_id(u32 operate);
static const vm_net_mock_skill_catalog_item *vm_net_mock_battle_operate_skill(u32 operate);
static bool vm_net_mock_battle_operate_skill_targets_friendly_group_heal(u32 operate);
static bool vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(u32 operate);
static bool vm_net_mock_battle_operate_skill_targets_enemy_status_no_damage(u32 operate);
static void vm_net_mock_battle_sync_role_mp_from_role(vm_net_mock_role_state *role);
static void vm_net_mock_battle_enemy_ailments_clear(void);
static u8 vm_net_mock_battle_enemy_ailments_advance_round(void);

/* Set while auto synth drives synchronized operate so real-4/2 note is skipped. */
static u8 g_mockBattleAutoSynthInProgress = 0;

/*
 * Solo/group 4/2 actioninfo must fit one type-1 multi-child skill plus up to
 * three death actions and three bundled counterattacks.  Tagged encoding for
 * a non-lethal 3-target group skill + 3 counters is ~232 bytes; the old 128
 * stack buffer overflowed, returned 0, and left the client on a frozen battle
 * UI with ignored-unhandled-server-only.
 */
enum
{
    VM_NET_MOCK_BATTLE_OPERATE_ACTIONINFO_CAP = 512
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

    /* #region agent log */
    {
        char data[320];
        snprintf(data, sizeof(data),
                 "{\"path\":\"scene-subtype5\",\"roleId\":%u,"
                 "\"roleHp\":%u,\"roleMaxHp\":%u,\"roleMp\":%u,\"roleMaxMp\":%u,"
                 "\"vitalsDefaultMaxHp\":%u,\"vitalsDefaultMaxMp\":%u}",
                 roleId, roleHp, roleMaxHp, roleMp, roleMaxMp,
                 roleMaxHpDefault, roleMaxMpDefault);
        agent_dbg_hp_log("A", "mock_server_battle.c:scene_start_blob",
                         "battle-wire-hp-mp", data);
    }
    /* #endregion */

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
    /*
     * A map-dead seat must never open Battle.cbm: alive_mask excludes HP=0, so
     * the client would sit in the battle UI with no action turns and no death
     * prompt.  Drop the pending start; the member stays on the map.
     */
    if (!vm_mock_service_team_member_has_nonzero_battle_hp(observer))
    {
        printf("[warn][mock-service] team_battle_drop observer=%08x serial=%u "
               "reason=map-dead-hp0 online_hp=%u\n",
               observer->clientId, pendingSerial, observer->onlineHp);
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
    vm_net_mock_finish_wt_packet(out, pos, 2);

    hpMax = observer->onlineHpMax ? observer->onlineHpMax : 1;
    hp = observer->onlineHp;
    mpMax = observer->onlineMpMax;
    mp = observer->onlineMp;
    if (hp > hpMax)
        hp = hpMax;
    if (mp > mpMax)
        mp = mpMax;
    g_mockBattleOperateSessionArmed = 1;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleStartUsesSceneWireMaps = 1;
    g_mockBattleEnemyCountCurrent = team->battleMonsterCount;
    g_mockBattleOperateTurnCounter = 0;
    memset(&g_vm_net_mock_battle_solo_modifier, 0,
           sizeof(g_vm_net_mock_battle_solo_modifier));
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    vm_net_mock_battle_enemy_ailments_clear();
    g_vm_net_mock_battle_formula_enemy_index = 0xff;
    ++g_mockBattleOperateSessionSerial;
    vm_net_mock_battle_reset_last_operate_target();
    g_vm_net_mock_battle_rewarded_serial = 0;
    g_vm_net_mock_battle_rewarded_exp = 0;
    memset(g_vm_net_mock_battle_rewarded_drops, 0,
           sizeof(g_vm_net_mock_battle_rewarded_drops));
    g_vm_net_mock_battle_rewarded_drop_result_count = 0;
    g_vm_net_mock_battle_settlement_sent_serial = 0;
    g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
    g_vm_net_mock_battle_recovered_serial = 0;
    g_mockBattleSettleWireRecoverHp = 0;
    g_mockBattleSettleWireRecoverMp = 0;
    g_vm_net_mock_battle_role_id_current = observer->onlineRoleId;
    g_vm_net_mock_battle_enemy_id_current = team->battleEnemyId;
    g_mockBattleRoleHpCurrent = hp;
    g_mockBattleRoleHpMax = hpMax;
    g_mockBattleRoleMpCurrent = mp;
    g_mockBattleRoleMpMax = mpMax;
    vm_net_mock_battle_reset_enemy_hp_from_stats(team->battleEnemyId);
    /*
     * Solo hangup prefer/auto synth must not survive into a shared team fight:
     * it bypasses round_defer and can resolve the whole round on one seat.
     */
    vm_net_mock_battle_suspend_solo_auto_for_team("team-battle-deliver");

    observer->pendingTeamBattleSerial = 0;
    printf("[info][mock-service] team_battle_deliver serial=%u observer=%08x "
           "leader=%08x enemy=%u scene=%s party=%u subtype=5 side=%u "
           "objects=2 resp=%u evidence=mmBattle:0x7BD0->0x66CC\n",
           pendingSerial,
           observer->clientId,
           team->battleLeaderClientId,
           team->battleEnemyId,
           team->battleScene,
           partyCount,
           team->battleSide,
           pos);
    /* #region agent log */
    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "{\"observer\":\"%08x\",\"serial\":%u,\"party\":%u,\"enemy\":%u,"
                 "\"hp\":%u,\"hpMax\":%u,\"mp\":%u,\"mpMax\":%u,\"resp\":%u}",
                 observer->clientId, pendingSerial, partyCount,
                 team->battleEnemyId, hp, hpMax, mp, mpMax, pos);
        agent_dbg_hp_log("T0", "mock_server_battle.c:team_battle_deliver",
                         "team_battle_start_deliver", dbg);
    }
    /* #endregion */
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

static void vm_net_mock_battle_clear_last_operate(void)
{
    g_mockBattleLastOperateValid = 0;
    g_mockBattleLastOperate = 0;
    g_mockBattleLastIndex = 0;
}

/* New encounter: keep last skill, drop the previous fight's wire cursor so
 * auto_choose re-selects a live enemy in the new formation. */
static void vm_net_mock_battle_reset_last_operate_target(void)
{
    g_mockBattleLastIndex = 0;
}

static void vm_net_mock_battle_remember_last_operate(u32 index, u32 operate,
                                                     bool operateConsumesTurn)
{
    if (!operateConsumesTurn)
        return;
    /* Mirror Callback_Unknown2(0x2CB5): only 普攻 (0) or skill (id+2) are
     * turn actions worth replaying under auto. */
    if (operate != 0 && !vm_net_mock_battle_operate_is_skill(operate))
        return;
    /*
     * Support skills (heal/buff/status) consume the turn but must not replace
     * the remembered offensive operate/target.  Storing the self-heal wire as
     * LastIndex made the next auto tick retarget poorly, and remembering the
     * heal operate itself replayed a full-HP 清风拂体 as amount=0 (looks like
     * "普通攻击伤害 0").
     */
    if (vm_net_mock_battle_operate_is_skill(operate) &&
        (vm_net_mock_battle_operate_skill_targets_friendly_group_heal(operate) ||
         vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(operate) ||
         vm_net_mock_battle_operate_skill_targets_enemy_status_no_damage(operate)))
    {
        return;
    }
    /*
     * Auto synth may fall back to 普攻 for one tick when MP < skill cost.
     * Keep the preferred skill so later ticks resume it after flask / regen.
     */
    if (g_mockBattleAutoSynthInProgress != 0 &&
        operate == 0 &&
        g_mockBattleLastOperateValid != 0 &&
        vm_net_mock_battle_operate_is_skill(g_mockBattleLastOperate))
    {
        g_mockBattleLastIndex = index;
        return;
    }
    g_mockBattleLastOperateValid = 1;
    g_mockBattleLastOperate = operate;
    g_mockBattleLastIndex = index;
}

static u32 vm_net_mock_battle_auto_choose_operate(u32 *indexOut)
{
    bool playerOnRight = vm_net_mock_battle_player_on_right();
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 preferred = 0;
    u8 live = 0;
    u32 operate = 0;

    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    preferred = g_mockBattleLastOperateValid
                    ? (u8)g_mockBattleLastIndex
                    : (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT",
                                              defaultEnemySlot);
    /*
     * Multi-monster / after a kill: last wire may be dead.  Retarget like
     * operate builder's select_live_enemy_wire so AOE and single-target both
     * land on a living unit instead of replaying a corpse index.
     * Wire 0 is a valid subtype-5 slot — never treat return 0 as "unset".
     */
    live = vm_net_mock_battle_select_live_enemy_wire(preferred,
                                                     playerOnRight,
                                                     battleSide,
                                                     defaultEnemySlot);
    if (!vm_net_mock_battle_enemy_wire_is_alive(live, playerOnRight, battleSide,
                                                defaultEnemySlot))
    {
        live = vm_net_mock_battle_first_alive_enemy_wire(playerOnRight,
                                                         battleSide,
                                                         defaultEnemySlot);
    }
    if (vm_net_mock_battle_enemy_wire_is_alive(live, playerOnRight, battleSide,
                                               defaultEnemySlot))
        g_mockBattleLastIndex = live;
    if (indexOut)
    {
        if (vm_net_mock_battle_enemy_wire_is_alive((u8)g_mockBattleLastIndex,
                                                    playerOnRight, battleSide,
                                                    defaultEnemySlot))
            *indexOut = g_mockBattleLastIndex;
        else
            *indexOut = (u32)defaultEnemySlot;
    }

    operate = g_mockBattleLastOperateValid ? g_mockBattleLastOperate : 0;
    if (vm_net_mock_battle_operate_is_skill(operate))
    {
        const vm_net_mock_skill_catalog_item *skill =
            vm_net_mock_battle_operate_skill(operate);
        u32 mpCost = skill ? skill->mpCost : 0;
        u32 mpNow;

        if (g_mockBattleRoleMpMax == 0)
        {
            vm_net_mock_role_state *role = vm_net_mock_active_role();
            if (role != NULL)
                vm_net_mock_battle_sync_role_mp_from_role(role);
        }
        mpNow = g_mockBattleRoleMpCurrent;
        if (mpCost != 0 && mpNow < mpCost)
        {
            printf("[info][network] mock_battle_auto_mp_fallback "
                   "operate=%u skill=%u mpcost=%u rolemp=%u "
                   "action=normal-attack evidence=skill.dsh:mpCost\n",
                   operate,
                   vm_net_mock_battle_operate_skill_id(operate),
                   mpCost,
                   mpNow);
            operate = 0;
        }
    }
    return operate;
}

static u32 vm_net_mock_build_synth_battle_operate_request(u8 *out, u32 outCap,
                                                          u32 index, u32 operate)
{
    u32 pos = 4;
    u32 objectStart = 4;
    u32 objectLen = 0;

    /*
     * Client battle requests use a 5-byte object header:
     *   major, kind, subtype, len_be16
     * with the first object starting at byte 4 (so major==1 doubles as
     * objectCount).  begin_wt_object() writes the 6-byte *response* header and
     * cannot feed vm_net_mock_next_request_object().
     */
    if (out == NULL || outCap < 16)
        return 0;
    out[pos++] = 1;
    out[pos++] = 4;
    out[pos++] = 2;
    out[pos++] = 0;
    out[pos++] = 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "index", index))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "Operate", operate))
        return 0;
    objectLen = pos - objectStart;
    if (objectLen > 0xffffu)
        return 0;
    out[objectStart + 3] = (u8)(objectLen >> 8);
    out[objectStart + 4] = (u8)objectLen;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    /* out[4] remains major=1 == objectCount for this single-object request. */
    return pos;
}

static void vm_net_mock_battle_auto_clear_pending(void);
static void vm_net_mock_battle_auto_arm_pending(const char *reason);
static void vm_net_mock_battle_auto_arm_pending_after_act(const char *reason);
static void vm_net_mock_battle_note_round_playback_hold(u8 actionCount,
                                                        const char *reason);
static void vm_net_mock_battle_auto_arm_flag_pending(const char *reason);
static bool vm_net_mock_battle_auto_in_turn_gap(void);
static u32 vm_net_mock_build_pending_solo_auto_flag_response(u8 *out, u32 outCap);
static u32 vm_net_mock_build_pending_solo_auto_operate_response(u8 *out, u32 outCap);
static u32 vm_net_mock_build_pending_hangup_loop_battle_response(u8 *out, u32 outCap);
static u32 vm_net_mock_build_pending_hangup_start_delay_response(u8 *out, u32 outCap);
static void vm_net_mock_hangup_loop_clear(const char *reason);
static void vm_net_mock_hangup_loop_note_victory_reentry(const char *reason);
static void vm_net_mock_hangup_loop_schedule_next(const char *reason);
static void vm_net_mock_battle_settlement_exit_clear(const char *reason);
static void vm_net_mock_battle_settlement_exit_arm(const char *reason);
static void vm_net_mock_battle_arm_encounter_cooldown(const char *reason);
static bool vm_net_mock_battle_encounter_cooldown_active(u32 *remainMsOut);
static u32 vm_net_mock_build_pending_encounter_cooldown_clear_response(u8 *out,
                                                                       u32 outCap);
static void vm_net_mock_battle_post_exit_settle_clear(const char *reason);
static void vm_net_mock_battle_note_victory_settlement(const char *reason);
static u32 vm_net_mock_build_pending_battle_settlement_exit_response(u8 *out,
                                                                     u32 outCap);
static u32 vm_net_mock_build_pending_battle_post_exit_settle_response(u8 *out,
                                                                      u32 outCap);
static u32 vm_net_mock_build_battle_pending_settlement_response(u8 *out,
                                                                u32 outCap);
static u32 vm_net_mock_build_battle_settlement_exit_packet(u8 *out, u32 outCap,
                                                           const char *phase);
static bool vm_net_mock_append_battle_terminal_subtype8_object(u8 *out, u32 outCap,
                                                               u32 *pos);
static bool vm_net_mock_append_battle_case11_auto_flag_object(u8 *out, u32 outCap,
                                                              u32 *pos, u8 type);
static bool vm_net_mock_append_battle_terminal_case9_object(u8 *out, u32 outCap,
                                                            u32 *pos);

/*
 * Challenge (4/1) / hangup start must not be answered with settle-only packets.
 *
 * Negative evidence 2026-07-28:
 *   - empty hold resp=5 → client interaction stuck, map walks, no re-challenge
 *   - early-exit resp=104 (4/8+4/11+4/9 only) → same stuck (4/1 got tear-down,
 *     not 1/4/5 battle start); log: challenge resp=104 then only moveinfo
 *
 * 2026-07-30: clear-only also skips the pending 4/8 while 4/7 already painted
 * the settle shell.  The next fight's delayed 4/8 then flashes an empty box
 * (user: first fight immediately re-challenges → empty box on second exit).
 *
 * Challenge reenter: prepend authentic 4/8+4/11 type0+4/9 into THIS start
 * response, then continue with 1/4/5.  Hangup continuous used to clear-only
 * (next start owns tear-down), but starting while the settle panel is still
 * painted left Battle.cbm on 4/7 and the following 4/12 type=0 reopened the
 * operate menu on top — intermittent unclickable settle.  Hangup now also
 * prepends tear-down when settle/exit is pending.
 */
static bool vm_net_mock_battle_release_settle_for_start(u8 *out,
                                                        u32 outCap,
                                                        u32 *pos,
                                                        u8 *objectCount,
                                                        const char *via,
                                                        bool prependExit)
{
    u8 autoType = 0;

    if (g_mockBattleAwaitingSettlement == 0 &&
        g_mockBattleSettlementExitPending == 0 &&
        g_mockBattlePostExitSettlePending == 0)
    {
        return true;
    }

    if (prependExit)
    {
        if (out == NULL || outCap == 0 || pos == NULL || objectCount == NULL)
            return false;
        if (!vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, pos) ||
            *objectCount == 0xff)
        {
            return false;
        }
        ++(*objectCount);
        if (!vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, pos,
                                                               autoType) ||
            *objectCount == 0xff)
        {
            return false;
        }
        ++(*objectCount);
        if (!vm_net_mock_append_battle_terminal_case9_object(out, outCap, pos) ||
            *objectCount == 0xff)
        {
            return false;
        }
        ++(*objectCount);
        printf("[info][network] mock_battle_start_blocked_by_settle via=%s "
               "action=prepend-exit-reenter awaiting=%u exit_pending=%u "
               "post_exit_settle=%u armed=%u objects=%u "
               "evidence=4/8-before-1/4/5-same-wt\n",
               via ? via : "-",
               g_mockBattleAwaitingSettlement ? 1 : 0,
               g_mockBattleSettlementExitPending ? 1 : 0,
               g_mockBattlePostExitSettlePending ? 1 : 0,
               g_mockBattleOperateSessionArmed ? 1 : 0,
               *objectCount);
        vm_autotest_note("mock_battle_start_blocked_by_settle via=%s "
                         "action=prepend-exit-reenter\n",
                         via ? via : "-");
    }
    else
    {
        printf("[info][network] mock_battle_start_blocked_by_settle via=%s "
               "action=clear-allow-reenter awaiting=%u exit_pending=%u "
               "post_exit_settle=%u armed=%u "
               "evidence=challenge-needs-battle-start-not-exit-only\n",
               via ? via : "-",
               g_mockBattleAwaitingSettlement ? 1 : 0,
               g_mockBattleSettlementExitPending ? 1 : 0,
               g_mockBattlePostExitSettlePending ? 1 : 0,
               g_mockBattleOperateSessionArmed ? 1 : 0);
    }

    g_mockBattleOperateSessionArmed = 0;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    vm_net_mock_battle_settlement_exit_clear(
        prependExit ? "reenter-prepend-exit" : "reenter-clear");
    vm_net_mock_battle_post_exit_settle_clear(
        prependExit ? "reenter-prepend-exit" : "reenter-clear");
    if (g_mockHangupLoopScheduleAfterExit != 0)
        vm_net_mock_hangup_loop_schedule_next(
            prependExit ? "reenter-prepend-exit" : "reenter-clear");
    /*
     * Victory from the prior fight armed map 1/1/14.  Drop leftover vitals so
     * they cannot steal in-battle 2/10 / moveinfo (mall flash-crash 2026-07-28).
     */
    {
        vm_mock_service_client_session *session =
            vm_mock_service_get_active_client_session();
        vm_mock_service_session_cancel_map_actor_vitals_sync(
            session,
            prependExit ? "reenter-prepend-exit" : "reenter-clear");
    }
    return true;
}
static u32 vm_net_mock_build_hangup_battle_start_response(const u8 *request, u32 requestLen,
                                                          u8 *out, u32 outCap);
static void vm_net_mock_battle_auto_note_client_operate(void);

static bool vm_net_mock_battle_inline_settlement_enabled(void)
{
    /*
     * Keep 4/7 in the killing response by default.  Exit-then-4/7 failed:
     * lone 4/8 blanks UpdateCharAttrs; 4/7 after tear-down is not map settle.
     * Set CBE_BATTLE_INLINE_SETTLEMENT=0 only for narrow experiments.
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

/*
 * Boss/首领 active skill on the monster counter turn.
 * Reuses skill.dsh effectIndex for type-1 playback; damage is an amplified
 * normal counter strike (hit/dodge/crit already resolved).
 * Play contract: skill damage +200% (300% of NA); when HP < 60% may self-heal
 * once per battle instead of striking.
 */
static u32 vm_net_mock_battle_boss_skill_pick_id(u32 enemyId)
{
    /* Offensive rows with non-zero 技能图片 in skill.dsh. */
    static const u32 kBossSkills[] = {
        1u,   /* 万剑诛仙 */
        21u,  /* 雷震八方 */
        121u, /* 荒魂劫火 */
        231u, /* 天火熔身 */
        201u, /* 绯炎术 */
    };
    u32 index = (enemyId + g_mockBattleOperateTurnCounter) %
                (u32)(sizeof(kBossSkills) / sizeof(kBossSkills[0]));

    return kBossSkills[index];
}

static u32 vm_net_mock_battle_boss_heal_skill_pick_id(void)
{
    /* 三花聚顶1: positive 生命变化 + non-zero 技能图片. */
    u32 skillId = vm_net_mock_env_u32("CBE_BATTLE_BOSS_HEAL_SKILL_ID", 261u);
    const vm_net_mock_skill_catalog_item *skill =
        vm_net_mock_find_skill_catalog_item(skillId);

    if (skill != NULL && skill->hpChange > 0 && skill->effectIndex != 0)
        return skillId;
    return 261u;
}

static u8 vm_net_mock_battle_live_enemy_index_for_strike(u8 strikeIndex)
{
    u8 seen = 0;
    u8 enemyCount = vm_net_mock_battle_enemy_count_current();

    for (u8 i = 0; i < enemyCount && i < 3; ++i)
    {
        if (g_mockBattleEnemyHpSlots[i] == 0)
            continue;
        if (seen == strikeIndex)
            return i;
        ++seen;
    }
    return 0xff;
}

static u32 vm_net_mock_battle_resolve_enemy_counter_damage(
    u32 enemyId,
    u32 roleHpCurrent,
    u8 strikeIndex,
    u8 *actionTypeOut,
    u32 *effectIndexOut,
    u32 *skillIdOut,
    bool *healOut,
    u32 *healAmountOut)
{
    u32 damage = 0;
    u32 chance = 0;
    u32 roll = 0;
    u32 mult = 0;
    u32 skillId = 0;
    const vm_net_mock_skill_catalog_item *skill = NULL;
    u8 enemyIndex = 0xff;

    if (actionTypeOut)
        *actionTypeOut = 0;
    if (effectIndexOut)
        *effectIndexOut = 0;
    if (skillIdOut)
        *skillIdOut = 0;
    if (healOut)
        *healOut = false;
    if (healAmountOut)
        *healAmountOut = 0;

    if (vm_net_mock_env_u32("CBE_BATTLE_BOSS_SKILL", 1) == 0)
    {
        damage = vm_net_mock_battle_enemy_damage_to_role(enemyId, roleHpCurrent);
        return damage;
    }
    if (!vm_net_mock_monster_casts_active_skill(enemyId))
    {
        damage = vm_net_mock_battle_enemy_damage_to_role(enemyId, roleHpCurrent);
        return damage;
    }
    /* Only the lead living enemy casts; extras keep normal attacks. */
    if (strikeIndex != 0)
    {
        damage = vm_net_mock_battle_enemy_damage_to_role(enemyId, roleHpCurrent);
        return damage;
    }

    enemyIndex = vm_net_mock_battle_live_enemy_index_for_strike(strikeIndex);
    if (enemyIndex < 3 && !g_mockBattleMonsterHealUsed &&
        g_mockBattleEnemyHpMaxSlots[enemyIndex] != 0 &&
        (uint64_t)g_mockBattleEnemyHpSlots[enemyIndex] * 100ull <
            (uint64_t)g_mockBattleEnemyHpMaxSlots[enemyIndex] * 60ull)
    {
        chance = vm_net_mock_env_u32("CBE_BATTLE_BOSS_HEAL_CHANCE", 40);
        if (chance > 100u)
            chance = 100u;
        roll = (enemyId * 97u + g_mockBattleOperateTurnCounter * 31u +
                g_mockBattleOperateSessionSerial * 13u) %
               100u;
        if (roll < chance)
        {
            u32 hp = g_mockBattleEnemyHpSlots[enemyIndex];
            u32 hpMax = g_mockBattleEnemyHpMaxSlots[enemyIndex];
            u32 missing = hpMax > hp ? hpMax - hp : 0;
            u32 healAmount = 0;
            u32 floorHeal = 0;

            skillId = vm_net_mock_battle_boss_heal_skill_pick_id();
            skill = vm_net_mock_find_skill_catalog_item(skillId);
            if (skill != NULL && skill->effectIndex != 0 && missing != 0)
            {
                if (skill->hpChange > 0)
                    healAmount = (u32)skill->hpChange;
                floorHeal = hpMax / 4u; /* at least ~25% max HP */
                if (healAmount < floorHeal)
                    healAmount = floorHeal;
                if (healAmount > missing)
                    healAmount = missing;
                g_mockBattleEnemyHpSlots[enemyIndex] =
                    vm_net_mock_add_capped_u32(hp, healAmount);
                if (g_mockBattleEnemyHpSlots[enemyIndex] > hpMax)
                    g_mockBattleEnemyHpSlots[enemyIndex] = hpMax;
                vm_net_mock_battle_sync_enemy_hp_totals();
                g_mockBattleMonsterHealUsed = true;
                vm_net_mock_battle_clear_outcome_child_flag();
                if (actionTypeOut)
                    *actionTypeOut = 1;
                if (effectIndexOut)
                    *effectIndexOut = skill->effectIndex;
                if (skillIdOut)
                    *skillIdOut = skillId;
                if (healOut)
                    *healOut = true;
                if (healAmountOut)
                    *healAmountOut = healAmount;
                printf("[info][network] mock_battle_boss_heal enemy=%u skill=%u "
                       "effect=%u heal=%u hp=%u/%u turn=%u chance=%u roll=%u "
                       "evidence=hp<60+once-per-battle+skill.dsh:生命变化\n",
                       enemyId, skillId, skill->effectIndex, healAmount,
                       g_mockBattleEnemyHpSlots[enemyIndex], hpMax,
                       g_mockBattleOperateTurnCounter, chance, roll);
                return 0;
            }
        }
    }

    damage = vm_net_mock_battle_enemy_damage_to_role(enemyId, roleHpCurrent);
    if (damage == 0 || roleHpCurrent == 0)
        return 0;

    chance = vm_net_mock_env_u32("CBE_BATTLE_BOSS_SKILL_CHANCE", 45);
    if (chance > 100u)
        chance = 100u;
    roll = (enemyId * 131u + g_mockBattleOperateTurnCounter * 17u +
            g_mockBattleOperateSessionSerial * 7u) %
           100u;
    /* Also guarantee a cast every 3rd armed turn so bosses feel active. */
    if (roll >= chance && (g_mockBattleOperateTurnCounter % 3u) != 0u)
        return damage;

    skillId = vm_net_mock_env_u32("CBE_BATTLE_BOSS_SKILL_ID", 0);
    if (skillId == 0)
        skillId = vm_net_mock_battle_boss_skill_pick_id(enemyId);
    skill = vm_net_mock_find_skill_catalog_item(skillId);
    if (skill == NULL || skill->effectIndex == 0)
        return damage;

    /* +200% attack bonus → 300% of normal-attack counter damage. */
    mult = vm_net_mock_env_u32("CBE_BATTLE_BOSS_SKILL_DAMAGE_PCT", 300);
    if (mult < 100u)
        mult = 100u;
    if (mult > 500u)
        mult = 500u;
    {
        uint64_t boosted = (uint64_t)damage * (uint64_t)mult / 100ull;
        if (boosted < 1ull)
            boosted = 1ull;
        if (boosted > (uint64_t)roleHpCurrent)
            boosted = roleHpCurrent;
        damage = (u32)boosted;
    }

    if (actionTypeOut)
        *actionTypeOut = 1;
    if (effectIndexOut)
        *effectIndexOut = skill->effectIndex;
    if (skillIdOut)
        *skillIdOut = skillId;
    printf("[info][network] mock_battle_boss_skill enemy=%u skill=%u effect=%u "
           "damage=%u turn=%u chance=%u roll=%u mult=%u "
           "evidence=family=BOSS+skill.dsh:技能图片+200pct\n",
           enemyId, skillId, skill->effectIndex, damage,
           g_mockBattleOperateTurnCounter, chance, roll, mult);
    return damage;
}

/* Apply one monster counter; boss casts may upgrade actionType/effectIndex. */
static u32 vm_net_mock_battle_apply_enemy_counter_strike(
    u32 enemyId,
    u8 strikeIndex,
    u8 defaultActionType,
    u32 defaultEffectIndex,
    u8 *actionTypeOut,
    u32 *effectIndexOut,
    bool *healOut,
    u32 *healAmountOut)
{
    u8 bossActionType = 0;
    u32 bossEffectIndex = 0;
    u32 planned = 0;
    bool healed = false;
    u32 healAmount = 0;

    if (healOut)
        *healOut = false;
    if (healAmountOut)
        *healAmountOut = 0;

    planned = vm_net_mock_battle_resolve_enemy_counter_damage(
        enemyId, g_mockBattleRoleHpCurrent, strikeIndex,
        &bossActionType, &bossEffectIndex, NULL, &healed, &healAmount);
    if (bossActionType != 0)
    {
        if (actionTypeOut)
            *actionTypeOut = bossActionType;
        if (effectIndexOut)
            *effectIndexOut = bossEffectIndex;
    }
    else
    {
        if (actionTypeOut)
            *actionTypeOut = defaultActionType;
        if (effectIndexOut)
            *effectIndexOut = defaultEffectIndex;
    }
    if (healed)
    {
        if (healOut)
            *healOut = true;
        if (healAmountOut)
            *healAmountOut = healAmount;
        return 0;
    }
    return vm_net_mock_battle_apply_damage_to_role(planned);
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

    /* skill.dsh `目标指向` 1=single ally, 2=all allies.  Positive hpChange is heal. */
    return skill != NULL && skill->hpChange > 0 &&
           (skill->targetDirection == 1 || skill->targetDirection == 2);
}

/*
 * Map 4/2 `index` (Callback_Unknown2 / sub_2B26 selected unit) onto a party
 * member seat.  Accept member wire 0..party-1, party display (monsters+m), or
 * the legacy display_to_wire(m) encoding used by prior heal emitters.
 */
static u8 vm_net_mock_battle_resolve_friendly_heal_member(u8 requestIndex,
                                                           u8 actorMember)
{
    u8 memberCount = g_vm_net_mock_team_battle_member_count_current;
    u8 monsterCount = vm_net_mock_battle_enemy_count_current();
    u8 member = 0;

    if (memberCount < 2)
        return actorMember;
    if (memberCount > 3)
        memberCount = 3;
    if (actorMember >= memberCount)
        actorMember = 0;
    if (requestIndex < memberCount)
        return requestIndex;
    for (member = 0; member < memberCount; ++member)
    {
        if ((u8)(monsterCount + member) == requestIndex)
            return member;
        if (vm_net_mock_team_battle_display_to_wire_slot(member) == requestIndex)
            return member;
    }
    return actorMember;
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
    u32 operate, u8 playerWireSlot, u8 requestedTargetIndex,
    u8 targetWireSlots[3], u32 healValues[3])
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    u8 targetCount = 0;
    bool singleTarget = skill != NULL && skill->targetDirection == 1;
    bool canRevive = skill != NULL && skill->effectKind == 3 && skill->hpChange > 0;

    if (targetWireSlots == NULL || healValues == NULL)
        return 0;

    if (g_vm_net_mock_team_battle_party_count_current >= 2 &&
        g_vm_net_mock_team_battle_member_count_current >= 2)
    {
        u8 memberCount = g_vm_net_mock_team_battle_member_count_current;
        u8 actor = g_vm_net_mock_team_battle_actor_slot_current;
        u8 preferredMember = actor;

        if (memberCount > 3)
            memberCount = 3;
        if (actor >= memberCount)
            actor = 0;
        preferredMember = vm_net_mock_battle_resolve_friendly_heal_member(
            requestedTargetIndex, actor);
        for (u8 member = 0; member < memberCount; ++member)
        {
            u32 hpCurrent = g_vm_net_mock_team_battle_member_hp_current[member];
            u32 hpMax = g_vm_net_mock_team_battle_member_hp_max_current[member];
            u32 healed = 0;

            /* td=1 清风拂面等: heal the selected living ally (not caster-only).
             * 尸鬼召唤(效果=3): may revive a dead ally. */
            if (singleTarget && !canRevive && member != preferredMember)
                continue;
            if (canRevive && singleTarget)
            {
                /* Prefer a dead ally; else heal the selected/living seat. */
                if (hpCurrent != 0)
                    continue;
            }
            else if (hpCurrent == 0 || hpMax == 0)
            {
                continue;
            }
            if (hpMax == 0)
                continue;
            if (canRevive && hpCurrent == 0)
            {
                healed = vm_net_mock_battle_player_skill_heal_to_role(
                    operate, 0, hpMax);
                if (healed == 0 && skill->hpChange > 0)
                    healed = (u32)skill->hpChange;
                if (healed > hpMax)
                    healed = hpMax;
            }
            else
            {
                healed = vm_net_mock_battle_player_skill_heal_to_role(
                    operate, hpCurrent, hpMax);
            }
            targetWireSlots[targetCount] = vm_net_mock_team_battle_display_to_wire_slot(member);
            healValues[targetCount] = healed;
            ++targetCount;
            if (healed != 0)
            {
                g_vm_net_mock_team_battle_member_hp_current[member] =
                    (canRevive && hpCurrent == 0) ? healed : (hpCurrent + healed);
                if (g_vm_net_mock_team_battle_member_hp_current[member] > hpMax)
                    g_vm_net_mock_team_battle_member_hp_current[member] = hpMax;
                g_vm_net_mock_team_battle_group_hp_changed_mask = (u8)(
                    g_vm_net_mock_team_battle_group_hp_changed_mask | (u8)(1u << member));
            }
            if (singleTarget)
                break;
        }
        if (canRevive && targetCount == 0)
        {
            /* No dead ally: fall back to healing the caster if wounded. */
            u32 hpCurrent = g_vm_net_mock_team_battle_member_hp_current[actor];
            u32 hpMax = g_vm_net_mock_team_battle_member_hp_max_current[actor];
            u32 healed = vm_net_mock_battle_player_skill_heal_to_role(
                operate, hpCurrent, hpMax);
            if (healed != 0)
            {
                targetWireSlots[0] = vm_net_mock_team_battle_display_to_wire_slot(actor);
                healValues[0] = healed;
                targetCount = 1;
                g_vm_net_mock_team_battle_member_hp_current[actor] = hpCurrent + healed;
                g_vm_net_mock_team_battle_group_hp_changed_mask = (u8)(
                    g_vm_net_mock_team_battle_group_hp_changed_mask | (u8)(1u << actor));
            }
        }
        if (singleTarget && targetCount == 0 && preferredMember < memberCount)
        {
            /* Selected ally full or dead (non-revive): fall back to self. */
            u32 hpCurrent = g_vm_net_mock_team_battle_member_hp_current[actor];
            u32 hpMax = g_vm_net_mock_team_battle_member_hp_max_current[actor];
            u32 healed = vm_net_mock_battle_player_skill_heal_to_role(
                operate, hpCurrent, hpMax);
            if (healed != 0)
            {
                targetWireSlots[0] = vm_net_mock_team_battle_display_to_wire_slot(actor);
                healValues[0] = healed;
                targetCount = 1;
                g_vm_net_mock_team_battle_member_hp_current[actor] = hpCurrent + healed;
                g_vm_net_mock_team_battle_group_hp_changed_mask = (u8)(
                    g_vm_net_mock_team_battle_group_hp_changed_mask | (u8)(1u << actor));
            }
        }
        if (actor < memberCount)
        {
            g_mockBattleRoleHpCurrent = g_vm_net_mock_team_battle_member_hp_current[actor];
            g_mockBattleRoleHpMax = g_vm_net_mock_team_battle_member_hp_max_current[actor];
        }
        if (singleTarget && targetCount != 0)
        {
            printf("[info][network] mock_battle_single_ally_heal operate=%u "
                   "request_index=%u actor=%u target_member=%u wire=%u amount=%u "
                   "evidence=skill.dsh:目标指向1\n",
                   operate, requestedTargetIndex, actor, preferredMember,
                   targetWireSlots[0], healValues[0]);
        }
        return targetCount;
    }

    targetWireSlots[0] = playerWireSlot;
    healValues[0] = vm_net_mock_battle_player_skill_heal_to_role(
        operate, g_mockBattleRoleHpCurrent, g_mockBattleRoleHpMax);
    g_mockBattleRoleHpCurrent += healValues[0];
    return 1;
}

static bool vm_net_mock_battle_skill_has_stat_delta(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL &&
           (skill->strengthChange != 0 || skill->agilityChange != 0 ||
            skill->wisdomChange != 0 || skill->attackChange != 0 ||
            skill->defenseChange != 0 || skill->critChange != 0 ||
            skill->hitChange != 0 || skill->dodgeChange != 0 ||
            skill->resistChange != 0);
}

static bool vm_net_mock_battle_skill_has_timed_stat_modifier(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL && skill->durationRounds != 0 &&
           vm_net_mock_battle_skill_has_stat_delta(skill);
}

static bool vm_net_mock_battle_skill_is_dot(
    const vm_net_mock_skill_catalog_item *skill)
{
    /* 毒入膏肓: duration + negative hp, no attribute columns, 效果=0. */
    return skill != NULL && skill->effectKind == 0 &&
           skill->durationRounds != 0 && skill->hpChange < 0 &&
           (skill->targetDirection == 3 || skill->targetDirection == 4) &&
           !vm_net_mock_battle_skill_has_stat_delta(skill);
}

static bool vm_net_mock_battle_skill_is_silence(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL && skill->effectKind == 1 &&
           skill->durationRounds != 0;
}

static bool vm_net_mock_battle_skill_is_dispel(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL && skill->effectKind == 2;
}

static bool vm_net_mock_battle_skill_is_revive(
    const vm_net_mock_skill_catalog_item *skill)
{
    return skill != NULL && skill->effectKind == 3 && skill->hpChange > 0;
}

static bool vm_net_mock_battle_skill_applies_enemy_debuff(
    const vm_net_mock_skill_catalog_item *skill)
{
    /* 雷震八方 / 破甲烈刃 / 天火熔身: damage + timed enemy attribute delta. */
    return skill != NULL && skill->hpChange < 0 &&
           skill->durationRounds != 0 &&
           (skill->targetDirection == 3 || skill->targetDirection == 4) &&
           vm_net_mock_battle_skill_has_stat_delta(skill);
}

static void vm_net_mock_battle_enemy_ailments_clear(void)
{
    memset(g_mockBattleEnemyAilments, 0, sizeof(g_mockBattleEnemyAilments));
}

static void vm_net_mock_battle_modifier_advance_round(
    vm_net_mock_battle_stat_modifier *modifier);

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
    modifier->dodge = skill->dodgeChange;
    modifier->resist = skill->resistChange;
}

static void vm_net_mock_battle_apply_skill_to_enemy_ailment(
    u8 enemyIndex, const vm_net_mock_skill_catalog_item *skill, u32 dotTickDamage)
{
    vm_net_mock_battle_enemy_ailment *ailment = NULL;

    if (enemyIndex >= 3 || skill == NULL)
        return;
    ailment = &g_mockBattleEnemyAilments[enemyIndex];
    if (vm_net_mock_battle_skill_is_silence(skill))
    {
        ailment->silenceRounds = skill->durationRounds;
        return;
    }
    if (vm_net_mock_battle_skill_is_dispel(skill))
    {
        memset(ailment, 0, sizeof(*ailment));
        return;
    }
    if (vm_net_mock_battle_skill_is_dot(skill))
    {
        ailment->dotDamagePerRound = dotTickDamage ? dotTickDamage :
            (u32)(0 - skill->hpChange);
        ailment->dotRounds = skill->durationRounds > 0 ?
            (u8)(skill->durationRounds - 1) : 0;
        return;
    }
    if (vm_net_mock_battle_skill_applies_enemy_debuff(skill))
        vm_net_mock_battle_modifier_set_from_skill(&ailment->stat, skill);
}

static u32 vm_net_mock_battle_enemy_modified_defense(u32 baseDefense, u8 enemyIndex)
{
    if (enemyIndex >= 3 ||
        g_mockBattleEnemyAilments[enemyIndex].stat.remainingRounds == 0)
        return baseDefense;
    return vm_net_mock_battle_apply_signed_stat_change(
        baseDefense, g_mockBattleEnemyAilments[enemyIndex].stat.defense);
}

static u32 vm_net_mock_battle_enemy_modified_attack(u32 baseAttack, u8 enemyIndex)
{
    if (enemyIndex >= 3 ||
        g_mockBattleEnemyAilments[enemyIndex].stat.remainingRounds == 0)
        return baseAttack;
    return vm_net_mock_battle_apply_signed_stat_change(
        baseAttack, g_mockBattleEnemyAilments[enemyIndex].stat.attack +
                        g_mockBattleEnemyAilments[enemyIndex].stat.strength / 2);
}

static u32 vm_net_mock_battle_enemy_modified_hit(u32 baseHit, u8 enemyIndex)
{
    if (enemyIndex >= 3 ||
        g_mockBattleEnemyAilments[enemyIndex].stat.remainingRounds == 0)
        return baseHit;
    return vm_net_mock_battle_apply_signed_stat_change(
        baseHit, g_mockBattleEnemyAilments[enemyIndex].stat.hit +
                     g_mockBattleEnemyAilments[enemyIndex].stat.agility * 2);
}

static u32 vm_net_mock_battle_enemy_modified_resist(u32 baseResist, u8 enemyIndex)
{
    if (enemyIndex >= 3 ||
        g_mockBattleEnemyAilments[enemyIndex].stat.remainingRounds == 0)
        return baseResist;
    return vm_net_mock_battle_apply_signed_stat_change(
        baseResist, g_mockBattleEnemyAilments[enemyIndex].stat.resist +
                        g_mockBattleEnemyAilments[enemyIndex].stat.wisdom / 2);
}

/* Tick DoTs and age enemy silence/debuff. Returns bitmask of slots that died. */
static u8 vm_net_mock_battle_enemy_ailments_advance_round(void)
{
    u8 deathMask = 0;

    for (u8 i = 0; i < 3; ++i)
    {
        vm_net_mock_battle_enemy_ailment *ailment = &g_mockBattleEnemyAilments[i];

        if (g_mockBattleEnemyHpSlots[i] == 0)
        {
            memset(ailment, 0, sizeof(*ailment));
            continue;
        }
        if (ailment->dotRounds != 0 && ailment->dotDamagePerRound != 0)
        {
            u32 dmg = ailment->dotDamagePerRound;
            if (dmg > g_mockBattleEnemyHpSlots[i])
                dmg = g_mockBattleEnemyHpSlots[i];
            g_mockBattleEnemyHpSlots[i] -= dmg;
            --ailment->dotRounds;
            if (g_mockBattleEnemyHpSlots[i] == 0)
            {
                deathMask = (u8)(deathMask | (u8)(1u << i));
                memset(ailment, 0, sizeof(*ailment));
                continue;
            }
        }
        if (ailment->silenceRounds != 0)
            --ailment->silenceRounds;
        vm_net_mock_battle_modifier_advance_round(&ailment->stat);
    }
    vm_net_mock_battle_sync_enemy_hp_totals();
    return deathMask;
}

static bool vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    /* td=0 self buffs (金刚不坏/金钟罩/轻身术); td=2 party buffs (神臂担山). */
    return skill != NULL && skill->hpChange == 0 &&
           (skill->targetDirection == 0 || skill->targetDirection == 2) &&
           vm_net_mock_battle_skill_has_timed_stat_modifier(skill);
}

static bool vm_net_mock_battle_operate_skill_targets_enemy_status_no_damage(u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);

    /* 神堂静默 (效果=1) and legacy duration-only rows with no HP delta. */
    if (vm_net_mock_battle_skill_is_silence(skill))
        return true;
    return skill != NULL && skill->hpChange == 0 &&
           skill->durationRounds != 0 &&
           (skill->targetDirection == 0 || skill->targetDirection == 3) &&
           !vm_net_mock_battle_skill_has_timed_stat_modifier(skill) &&
           skill->effectKind == 0;
}

/* `神臂担山` is a target-direction=2 group effect.  Self buffs (td=0) only
 * touch the caster.  Type-1 children carry zero HP/MP deltas. */
static u8 vm_net_mock_battle_apply_player_friendly_group_modifier_targets(
    u32 operate, u8 playerWireSlot, u8 targetWireSlots[3], u32 values[3])
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    u8 targetCount = 0;
    bool selfOnly = skill != NULL && skill->targetDirection == 0;

    if (targetWireSlots == NULL || values == NULL ||
        !vm_net_mock_battle_skill_has_timed_stat_modifier(skill))
    {
        return 0;
    }
    if (g_vm_net_mock_team_battle_party_count_current >= 2 &&
        g_vm_net_mock_team_battle_member_count_current >= 2)
    {
        u8 memberCount = g_vm_net_mock_team_battle_member_count_current;
        u8 actor = g_vm_net_mock_team_battle_actor_slot_current;

        if (memberCount > 3)
            memberCount = 3;
        for (u8 member = 0; member < memberCount; ++member)
        {
            if (selfOnly && member != actor)
                continue;
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
        if (actor < memberCount)
        {
            g_vm_net_mock_battle_active_modifier_current =
                g_vm_net_mock_team_battle_member_modifiers_current[actor];
        }
        return targetCount;
    }

    targetWireSlots[0] = playerWireSlot;
    values[0] = 0;
    vm_net_mock_battle_modifier_set_from_skill(&g_vm_net_mock_battle_solo_modifier, skill);
    g_vm_net_mock_battle_active_modifier_current = g_vm_net_mock_battle_solo_modifier;
    return 1;
}

static u8 vm_net_mock_battle_apply_player_enemy_status_targets(
    u32 operate, u8 requestedTargetSlot, bool playerOnRight, u8 battleSide,
    u8 fallbackEnemySlot, u8 targetWireSlots[3], u32 values[3])
{
    const vm_net_mock_skill_catalog_item *skill =
        vm_net_mock_battle_operate_skill(operate);
    u8 enemyIndex = 0;

    if (targetWireSlots == NULL || values == NULL)
        return 0;
    targetWireSlots[0] = requestedTargetSlot;
    values[0] = 0;
    if (vm_net_mock_battle_enemy_wire_to_index(requestedTargetSlot, playerOnRight,
                                               battleSide, fallbackEnemySlot,
                                               &enemyIndex) &&
        enemyIndex < 3)
    {
        vm_net_mock_battle_apply_skill_to_enemy_ailment(enemyIndex, skill, 0);
    }
    return 1;
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
    u8 targetWireSlots[3], u32 damageValues[3], u8 childFlags[3],
    u8 deathWireSlots[3], u8 *deathCountOut)
{
    u8 targetCount = 0;
    u8 deathCount = 0;
    u8 candidateSlots[3] = {0, 0, 0};
    u8 candidateCount = 0;
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 1;
    u8 playerSlot = 0;

    if (targetWireSlots == NULL || damageValues == NULL || deathWireSlots == NULL)
        return 0;
    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT",
                                         defaultPlayerSlot);
    if (targetsEnemyGroup && g_mockBattleEnemyHpCurrent > 0)
        candidateCount = vm_net_mock_battle_collect_live_enemy_wires(
            playerOnRight, battleSide, fallbackEnemySlot, candidateSlots);
    if (candidateCount == 0 &&
        requestedTargetSlot != playerSlot &&
        vm_net_mock_battle_enemy_wire_is_alive(requestedTargetSlot, playerOnRight,
                                                battleSide, fallbackEnemySlot))
    {
        candidateSlots[candidateCount++] = requestedTargetSlot;
    }
    if (candidateCount == 0)
    {
        u8 live = vm_net_mock_battle_first_alive_enemy_wire(
            playerOnRight, battleSide, fallbackEnemySlot);
        if (live != playerSlot &&
            vm_net_mock_battle_enemy_wire_is_alive(live, playerOnRight, battleSide,
                                                    fallbackEnemySlot))
        {
            candidateSlots[candidateCount++] = live;
        }
    }

    for (u8 i = 0; i < candidateCount && i < 3; ++i)
    {
        u8 targetWireSlot = candidateSlots[i];
        u32 targetEnemyHp = 0;
        u32 damage = 0;

        /* Offensive hits must never land on the player actor wire.  Runtime
         * evidence (battle-server-flow): actor/target swap makes the skill VFX
         * and negative valueA apply to the player and look like self-kill. */
        if (targetWireSlot == playerSlot ||
            !vm_net_mock_battle_enemy_wire_is_alive(targetWireSlot, playerOnRight,
                                                    battleSide, fallbackEnemySlot))
        {
            printf("[warn][network] mock_battle_attack_skip_non_enemy "
                   "operate=%u skill=%u wire=%u player_wire=%u evidence=self-hit-guard\n",
                   operate, operateIsSkill ? 1u : 0u, targetWireSlot, playerSlot);
            continue;
        }
        targetEnemyHp = vm_net_mock_battle_enemy_hp_for_wire(
            targetWireSlot, playerOnRight, battleSide, fallbackEnemySlot);
        {
            u8 enemyIndex = 0;
            if (vm_net_mock_battle_enemy_wire_to_index(targetWireSlot, playerOnRight,
                                                       battleSide, fallbackEnemySlot,
                                                       &enemyIndex) &&
                enemyIndex < 3)
                g_vm_net_mock_battle_formula_enemy_index = enemyIndex;
            else
                g_vm_net_mock_battle_formula_enemy_index = 0xff;
        }
        damage = operateIsSkill
                     ? vm_net_mock_battle_player_skill_damage_to_enemy(
                           operate, g_vm_net_mock_battle_enemy_id_current,
                           targetEnemyHp)
                     : vm_net_mock_battle_player_damage_to_enemy(
                           g_vm_net_mock_battle_enemy_id_current, targetEnemyHp);

        /* damage==0 is a miss: still emit a child with valueA=0 and
         * child_flag=3 (「闪躲」) so the client advances the action queue. */
        targetWireSlots[targetCount] = targetWireSlot;
        damageValues[targetCount] = damage;
        if (childFlags != NULL)
            childFlags[targetCount] = vm_net_mock_battle_take_outcome_child_flag();
        ++targetCount;
        if (damage != 0)
        {
            u8 enemyIndex = 0;
            const vm_net_mock_skill_catalog_item *skill =
                operateIsSkill ? vm_net_mock_battle_operate_skill(operate) : NULL;

            vm_net_mock_battle_damage_enemy_wire(targetWireSlot, playerOnRight,
                                                 battleSide, fallbackEnemySlot,
                                                 damage);
            if (skill != NULL &&
                vm_net_mock_battle_enemy_wire_to_index(targetWireSlot, playerOnRight,
                                                       battleSide, fallbackEnemySlot,
                                                       &enemyIndex) &&
                enemyIndex < 3)
            {
                u32 tickStore = damage;
                if (vm_net_mock_battle_skill_is_dot(skill))
                {
                    /* Store non-crit baseline for later ticks when possible. */
                    tickStore = (u32)(0 - skill->hpChange);
                    {
                        vm_net_mock_player_stats ps;
                        vm_net_mock_battle_role_stats_current(&ps);
                        tickStore += (u32)(((uint64_t)ps.strength * skill->strengthCoeff +
                                            (uint64_t)ps.agility * skill->agilityCoeff +
                                            (uint64_t)ps.wisdom * skill->wisdomCoeff +
                                            50ull) /
                                           100ull);
                    }
                }
                vm_net_mock_battle_apply_skill_to_enemy_ailment(enemyIndex, skill,
                                                               tickStore);
            }
            if (targetEnemyHp != 0 &&
                vm_net_mock_battle_enemy_hp_for_wire(targetWireSlot, playerOnRight,
                                                     battleSide,
                                                     fallbackEnemySlot) == 0)
            {
                deathWireSlots[deathCount++] = targetWireSlot;
            }
        }
        else if (operateIsSkill)
        {
            /* Miss still may apply silence-only skills; damage skills skip. */
        }
        g_vm_net_mock_battle_formula_enemy_index = 0xff;
    }
    if (deathCountOut != NULL)
        *deathCountOut = deathCount;
    return targetCount;
}

static u32 vm_net_mock_battle_item_effect_index(u32 hpEffect)
{
    const char *override = getenv("CBE_BATTLE_ITEM_EFFECT_INDEX");
    u32 effectIndex = 0;

    if (override != NULL && override[0] != 0)
        return vm_net_mock_env_u32("CBE_BATTLE_ITEM_EFFECT_INDEX", 0);
    if (hpEffect != 0 && vm_net_mock_eidolon_heal_effect_index(&effectIndex))
        return effectIndex;
    return 0;
}

static void vm_net_mock_battle_sync_role_mp_max_from_role(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (g_mockBattleRoleMpMax == 0)
        g_mockBattleRoleMpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
    if (g_mockBattleRoleMpMax == 0)
        g_mockBattleRoleMpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;
}

/*
 * Solo battles may seed current MP from the durable role row.  Team battles
 * keep authoritative current MP in the shared battleMemberMp[] snapshot;
 * copying role->mp over that snapshot re-fills spent MP (observed as a second
 * skill restoring the bar to max after the first cast deducted correctly).
 */
static void vm_net_mock_battle_sync_role_mp_from_role(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    vm_net_mock_battle_sync_role_mp_max_from_role(role);
    if (g_vm_net_mock_team_battle_party_count_current >= 2)
        return;
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
        vm_net_mock_battle_sync_role_mp_max_from_role(role);
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
    mpAfter = (mpBefore > mpCost) ? (mpBefore - mpCost) : 0;
    if (mpBeforeOut)
        *mpBeforeOut = mpBefore;
    if (mpAfterOut)
        *mpAfterOut = mpAfter;
    return true;
}

static void vm_net_mock_battle_commit_skill_mp(u32 mpAfter)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    /* Team battles must still advance the shared battle MP counter even when
     * the durable role row is temporarily unavailable. */
    if (role == NULL)
    {
        g_mockBattleRoleMpCurrent = mpAfter;
        return;
    }
    vm_net_mock_role_sync_derived_vitals(role);
    if (role->mp != mpAfter)
    {
        vm_net_mock_battle_set_role_mp_current(mpAfter);
        vm_net_mock_role_mark_inventory_dirty("battle-skill-use");
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
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = NULL;
    int memberIndex = -1;
    bool seatAlreadyLeft = false;

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (session != NULL)
    {
        team = vm_mock_service_team_find_for_client(session->clientId);
        memberIndex = vm_mock_service_team_battle_member_index(
            team, session->clientId);
        if (team != NULL && memberIndex >= 0 &&
            memberIndex < team->battleMemberCount &&
            (team->battleMemberLeftMask & (u8)(1u << memberIndex)) != 0)
        {
            seatAlreadyLeft = true;
        }
    }
    /*
     * Skip writing battle HP=0 only when this seat already left via revival
     * or escape (durable HP may already be restored).  A real in-fight death
     * must still persist HP=0 so 1/7/14 can consume the revival stone.
     */
    if (g_mockBattleRoleHpMax != 0)
    {
        if (!(seatAlreadyLeft && g_mockBattleRoleHpCurrent == 0 &&
              role->hp != 0))
        {
            role->hp = vm_net_mock_min_u32(g_mockBattleRoleHpCurrent,
                                           role->hpMax);
        }
    }
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

static bool vm_net_mock_append_battle_teaminfo_row(u8 *out, u32 outCap, u32 *pos,
                                                   u32 roleId, u32 roleHp,
                                                   u32 roleMp)
{
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
    if (out == NULL || pos == NULL || roleId == 0)
        return false;
    if (!vm_net_mock_put_u8(out, outCap, pos, 0))
        return false;
    if (!vm_net_mock_put_u8(out, outCap, pos, 4))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, pos, roleId))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, pos, roleHp))
        return false;
    if (!vm_net_mock_put_be32(out, outCap, pos, roleMp))
        return false;
    return true;
}

static bool vm_net_mock_build_battle_teaminfo_blob(u8 *out, u32 outCap,
                                                   u32 *teamInfoLenOut,
                                                   u32 roleId, u32 roleHp,
                                                   u32 roleMp)
{
    u32 pos = 0;

    if (teamInfoLenOut)
        *teamInfoLenOut = 0;
    if (!vm_net_mock_append_battle_teaminfo_row(out, outCap, &pos,
                                                roleId, roleHp, roleMp))
        return false;
    if (teamInfoLenOut)
        *teamInfoLenOut = pos;
    return true;
}

/*
 * InitActionSlot_B(0x6DBC) repeats one overlapped teaminfo row per
 * current_team_count.  A merged party 4/6 therefore needs one row for every
 * frozen battle member: earlier casters already committed post-cost MP into
 * battleMemberMp[], and the releasing actor still lives in g_mockBattleRole*.
 * Sending only the last caster's row leaves other seats' unit+1344 at 0, so
 * their type-1 playback restores MP to 0.  (Prior enter-battle crashes were
 * from merge writing over the same out buffer used as currentResponse, not
 * from multi-row teaminfo itself.)
 */
static bool vm_net_mock_build_team_battle_party_teaminfo_blob(
    u8 *out,
    u32 outCap,
    u32 *teamInfoLenOut,
    const vm_mock_service_client_session *observer,
    const vm_mock_service_team *team,
    u8 liveMemberIndex,
    bool useLiveMemberVitals)
{
    u32 pos = 0;
    char dbg[320];
    u32 mp0 = 0;
    u32 mp1 = 0;
    u32 mp2 = 0;
    u32 id0 = 0;
    u32 id1 = 0;
    u32 id2 = 0;

    if (teamInfoLenOut)
        *teamInfoLenOut = 0;
    if (out == NULL || observer == NULL || team == NULL ||
        team->battleMemberCount < 2)
    {
        return false;
    }

    for (u8 i = 0; i < team->battleMemberCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->battleMemberClientIds[i]);
        u32 wireId = vm_mock_service_team_member_wire_id(observer, member);
        u32 hp = team->battleMemberHp[i];
        u32 mp = team->battleMemberMp[i];

        if (member == NULL || wireId == 0)
            return false;
        if (useLiveMemberVitals && i == liveMemberIndex)
        {
            hp = g_mockBattleRoleHpCurrent;
            mp = g_mockBattleRoleMpCurrent;
            /* Same refill guard as finish_operation: live globals can still
             * briefly hold a stale max before the shared snapshot is clamped. */
            if (team->battleMemberMp[i] != 0 &&
                mp > team->battleMemberMp[i] &&
                g_vm_net_mock_battle_mp_increase_allowed == 0)
            {
                mp = team->battleMemberMp[i];
            }
        }
        if (!vm_net_mock_append_battle_teaminfo_row(out, outCap, &pos,
                                                    wireId, hp, mp))
            return false;
        if (i == 0)
        {
            id0 = wireId;
            mp0 = mp;
        }
        else if (i == 1)
        {
            id1 = wireId;
            mp1 = mp;
        }
        else if (i == 2)
        {
            id2 = wireId;
            mp2 = mp;
        }
    }

    if (teamInfoLenOut)
        *teamInfoLenOut = pos;
    /* #region agent log */
    snprintf(dbg, sizeof(dbg),
             "{\"observer\":\"%08x\",\"members\":%u,\"bytes\":%u,"
             "\"liveIdx\":%u,\"ids\":[%u,%u,%u],\"mps\":[%u,%u,%u]}",
             observer->clientId, team->battleMemberCount, pos,
             useLiveMemberVitals ? liveMemberIndex : 0xffu,
             id0, id1, id2, mp0, mp1, mp2);
    agent_dbg_hp_log("T1", "mock_server_battle.c:party_teaminfo",
                     "team_battle_party_teaminfo", dbg);
    /* #endregion */
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

static bool vm_net_mock_put_battle_action_companion_teaminfo_blob(
    u8 *out, u32 outCap, u32 *pos,
    const u8 *teamInfo, u32 teamInfoLen)
{
    if (teamInfo == NULL || teamInfoLen == 0)
        return true;
    if (teamInfoLen > 0xffff)
        return false;
    return vm_net_mock_put_object_raw(out, outCap, pos, "teaminfo",
                                      teamInfo, (u16)teamInfoLen);
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
    return true;
}

static bool vm_net_mock_append_battle_action6_object_teaminfo_blob(
    u8 *out, u32 outCap, u32 *pos,
    const u8 *actionInfo,
    u32 actionInfoLen,
    u8 actionCount,
    const u8 *teamInfo,
    u32 teamInfoLen)
{
    u32 objectStart = 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 6, &objectStart))
        return false;
    if (!vm_net_mock_put_battle_action_companion_teaminfo_blob(
            out, outCap, pos, teamInfo, teamInfoLen))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "actionnum", actionCount))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "actioninfo",
                                    actionInfo, (u16)actionInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
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
                                                        u8 playerSlot,
                                                        u32 effectIndexOverride)
{
    u32 pos = 5;
    u8 actionInfo[128];
    u32 actionInfoLen = 0;
    u8 actionCount = 1;
    u8 objectCount = 0;
    u32 effectIndex = effectIndexOverride != 0 ?
                      effectIndexOverride :
                      vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
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
            g_vm_net_mock_battle_mp_increase_allowed = 1;
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
        /*
         * Battle HP/MP medicines are self-only.  Request `index` is the battle
         * cursor (often an enemy wire after attacking) and must never become
         * the heal target.  Also reject a flipped wire table where playerSlot
         * lands on a live enemy — that is the subtype-5/10 desync that paints
         * renew VFX / +HP/+MP onto the monster.
         */
        u8 itemActorWireSlot = playerSlot;
        u8 itemTargetWireSlot = playerSlot;
        if (vm_net_mock_battle_enemy_wire_is_alive(itemTargetWireSlot, playerOnRight,
                                                    battleSide, enemySlot) ||
            itemTargetWireSlot == enemySlot)
        {
            u8 fixedSelf =
                (g_mockBattleStartUsesSceneWireMaps != 0 ||
                 g_mockBattleSceneMonsterStartActive != 0 ||
                 vm_net_mock_battle_enemy_count_current() > 1)
                    ? 1u
                    : (playerOnRight ? 0u : 1u);
            if (vm_net_mock_battle_enemy_wire_is_alive(fixedSelf, playerOnRight,
                                                       battleSide, enemySlot) ||
                fixedSelf == enemySlot)
            {
                fixedSelf = (fixedSelf == 0) ? 1u : 0u;
            }
            printf("[warn][network] mock_battle_item_heal_retarget "
                   "req_index=%u bad_player_wire=%u enemy_wire=%u -> self=%u "
                   "scene_maps=%u/%u evidence=subtype5-wire-heal-on-monster\n",
                   parsed.index, playerSlot, enemySlot, fixedSelf,
                   g_mockBattleStartUsesSceneWireMaps,
                   g_mockBattleSceneMonsterStartActive);
            itemActorWireSlot = fixedSelf;
            itemTargetWireSlot = fixedSelf;
            playerSlot = fixedSelf;
            enemySlot = (fixedSelf == 0) ? 1u : 0u;
        }
        itemActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_ACTOR_WIRE_SLOT",
                                                     itemActorWireSlot);
        itemTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_TARGET_WIRE_SLOT",
                                                      itemTargetWireSlot);
        if (vm_net_mock_battle_enemy_wire_is_alive(itemTargetWireSlot, playerOnRight,
                                                    battleSide, enemySlot))
        {
            printf("[warn][network] mock_battle_item_heal_reject_enemy_target "
                   "req_index=%u env_target=%u -> self=%u evidence=self-only-medicine\n",
                   parsed.index, itemTargetWireSlot, playerSlot);
            itemTargetWireSlot = playerSlot;
            itemActorWireSlot = playerSlot;
        }
        itemActionType = (hpEffect != 0 || hpApplied != 0)
                             ? (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_HEAL_ACTION_TYPE", 1)
                             : (u8)vm_net_mock_env_u32("CBE_BATTLE_ITEM_ACTION_TYPE", 2);
        itemEffectIndex = vm_net_mock_battle_item_effect_index(hpEffect);
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
                u8 strikeActionType = counterActionType;
                u32 strikeEffectIndex = type1EffectIndex;
                bool strikeIsHeal = false;
                u32 strikeHealAmount = 0;
                u32 oneCounterDamage = vm_net_mock_battle_apply_enemy_counter_strike(
                    g_vm_net_mock_battle_enemy_id_current, i, counterActionType,
                    type1EffectIndex, &strikeActionType, &strikeEffectIndex, &strikeIsHeal, &strikeHealAmount);
                u8 oneCounterChildFlag = vm_net_mock_battle_child_flag_with_env(
                    "CBE_BATTLE_COUNTER_CHILD_FLAG",
                    vm_net_mock_battle_take_outcome_child_flag());

                if (strikeActionType == 1)
                {
                    counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                                  counterActorWireSlot);
                    counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                                   counterTargetWireSlot);
                }
                if (strikeIsHeal)
                {
                    counterTargetWireSlot = counterActorWireSlot;
                    oneCounterChildFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL;
                    counterHpDelta = strikeHealAmount;
                }
                else
                {
                    /* oneCounterDamage==0 is a dodge/miss: still emit valueA=0 + 闪躲. */
                    counterHpDelta = vm_net_mock_battle_negative_delta_u32(oneCounterDamage);
                }
                counterDamageValues[i] = oneCounterDamage;
                counterDamageValue = vm_net_mock_add_capped_u32(counterDamageValue,
                                                                oneCounterDamage);
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, strikeActionType,
                                                                 counterActorWireSlot,
                                                                 counterTargetWireSlot,
                                                                 oneCounterChildFlag,
                                                                 counterHpDelta,
                                                                 vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0),
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? strikeEffectIndex : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail0 : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail1 : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail2 : 0))
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
    /*
     * Team battles must not flask/settle before the shared round merge reads
     * post-cost MP into party teaminfo.  See battle-operate inline note.
     */
    if (battleEndsThisRound &&
        vm_net_mock_battle_all_enemies_defeated() &&
        g_mockBattleRoleHpCurrent > 0 &&
        g_vm_net_mock_team_battle_party_count_current < 2 &&
        vm_net_mock_battle_inline_settlement_enabled())
    {
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &objectCount, false))
            return 0;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &objectCount,
                                                               "battle-item-use-inline",
                                                               true))
            return 0;
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

    if (consumed && !battleEndsThisRound)
        vm_net_mock_role_mark_inventory_dirty("battle-item-use");
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (battleEndsThisRound)
    {
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        if (g_mockBattleRoleHpCurrent == 0)
        {
            g_mockBattleAwaitingSettlement = 0;
            vm_net_mock_battle_settlement_exit_clear("battle-item-use-death");
            vm_net_mock_battle_post_exit_settle_clear("battle-item-use-death");
            vm_net_mock_battle_save_completed_current_role_state(
                "battle-item-use-death");
            vm_mock_service_session_arm_battle_revival_confirm_for_death(
                "battle-item-use-death");
            vm_net_mock_hangup_loop_clear("battle-item-use-death");
        }
        else
        {
            vm_net_mock_battle_note_victory_settlement("battle-item-use-victory");
            vm_net_mock_hangup_loop_note_victory_reentry("battle-item-use-victory");
        }
    }
    else
    {
        g_mockBattlePendingEnemyTurn = 0;
    }

    printf("[info][network] mock_battle_item_use index=%u seq=%u item=%u itemSeq=%u mode=%u reserve=%u->%u reserve_used=%u hp=%u/%u mp=%u/%u exp=%u effect=%u action=%u actor=%u target=%u player=%u enemy=%u scene_maps=%u consumed=%u applied=%u counters=%u counterdmg=%u death=%u armed=%u bundle=%u enemies=%u slots=%u/%u/%u sync=%u noop=%u resp=%u evidence=mmBattle:0x2B50->4/3,0x7BD0/0x6EB0->4/6,JianghuOL.CBE:0x1033544,item.dsh:consumeMode\n",
           parsed.index, parsed.seq, itemId, itemSeq,
           reservoirItem ? 2u : (effect ? effect->consumeMode : 0u),
           reservoirBefore, remaining, reservoirConsumed,
           hpApplied, hpEffect, mpApplied, mpEffect, expApplied,
           itemEffectIndex, itemActionType,
           playerSlot, playerSlot, playerSlot, enemySlot,
           g_mockBattleStartUsesSceneWireMaps,
           consumed ? 1 : 0, applied ? 1 : 0,
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

static u32 vm_net_mock_build_battle_operate_response(const u8 *request, u32 requestLen,
                                                     u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 index = 0;
    u32 operate = 0;
    u8 index8 = 0;
    u8 operate8 = 0;
    u8 actionInfo[VM_NET_MOCK_BATTLE_OPERATE_ACTIONINFO_CAP];
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
    bool skillTargetsEnemyStatus = false;
    bool skillSupportNoDamage = false;
    u8 attackWireSlots[3] = {0, 0, 0};
    u32 attackDamageValues[3] = {0, 0, 0};
    u8 attackChildFlags[3] = {0, 0, 0};
    u32 attackChildValueAs[3] = {0, 0, 0};
    u32 attackChildValueBs[3] = {0, 0, 0};
    u8 attackTargetCount = 0;
    u8 deathActionWireSlots[3] = {0, 0, 0};
    u8 deathActionTargetCount = 0;
    u8 counterWireSlots[3] = {0, 0, 0};
    u32 counterDamageValues[3] = {0, 0, 0};
    bool counterIsHeal[3] = {false, false, false};
    u32 counterHealAmounts[3] = {0, 0, 0};
    u8 counterChildFlags[3] = {0, 0, 0};
    u8 counterActionTypes[3] = {0, 0, 0};
    u32 counterEffectIndices[3] = {0, 0, 0};
    u8 counterWireCount = 0;
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
    /*
     * Spar exits with settle tear-down and never arms the PvE operate session.
     * Late client 4/2 must not invent a victory settle outside duel exit —
     * a zero-delta 4/7 crashes DrawBattleHpBar (team-battle-terminal peer crash).
     */
    if (g_mockBattleOperateSessionArmed == 0 &&
        g_mockBattleAwaitingSettlement == 0)
    {
        vm_net_mock_finish_wt_packet(out, pos, 0);
        printf("[info][network] mock_battle_operate_ignore reason=no-armed-session "
               "awaiting_settle=0 action=empty-ack\n");
        return pos;
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
    operateIsSkill = vm_net_mock_battle_operate_is_skill(operate);
    operateConsumesTurn = operate == 0 || operateIsSkill;
    skillTargetsEnemyGroup = operateIsSkill &&
                             vm_net_mock_battle_operate_skill_targets_enemy_group(operate);
    skillTargetsFriendlyGroupHeal = operateIsSkill &&
                                    vm_net_mock_battle_operate_skill_targets_friendly_group_heal(operate);
    skillTargetsFriendlyGroupModifier = operateIsSkill &&
                                        vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(operate);
    skillTargetsEnemyStatus = operateIsSkill &&
                              vm_net_mock_battle_operate_skill_targets_enemy_status_no_damage(operate);
    skillSupportNoDamage = skillTargetsFriendlyGroupHeal ||
                           skillTargetsFriendlyGroupModifier ||
                           skillTargetsEnemyStatus;
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
    /*
     * Offensive skills: never keep a self/party wire as the attack target.
     * Friendly heal/buff: keep the raw 4/2 index so td=1 can heal the selected
     * ally (清风拂面).  target_wire_slot_from_request remaps non-enemy indices
     * onto the first live monster and would destroy ally selection.
     */
    if (skillTargetsFriendlyGroupHeal || skillTargetsFriendlyGroupModifier)
        requestedTargetSlot = actorSlot;
    else if (requestedTargetSlot == playerSlot || requestedTargetSlot > 5)
        requestedTargetSlot = enemySlot;
    if (g_mockBattleOperateSessionFinished != 0)
        g_mockBattleOperateSessionFinished = 0;
    terminalFollowup = false;
    bundleWholeRound = g_mockBattleOperateSessionArmed != 0 &&
                       (g_vm_net_mock_team_battle_party_count_current >= 2
                            ? g_vm_net_mock_team_battle_resolve_monsters_current != 0
                            : (operateConsumesTurn &&
                               vm_net_mock_env_u32("CBE_BATTLE_BUNDLE_ROUND", 1) != 0));
    firstRecordActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_ACTOR_WIRE_SLOT",
                                                       playerSlot);
    firstRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_CHILD_FLAG", 0);
    counterRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    if (g_mockBattleAwaitingSettlement != 0)
    {
        (void)requestedTargetSlot;
        return vm_net_mock_build_battle_pending_settlement_response(out, outCap);
    }
    if (!terminalFollowup)
    {
        vm_net_mock_battle_ensure_multi_enemy_slots_seeded(
            g_vm_net_mock_battle_enemy_id_current);
        /* Only reseeds when no armed fight owns the slots.  Blind reset on
         * aggregate==0 used to revive (or re-clear) after a one-of-N kill when
         * only slot0 had ever been seeded. */
        if (vm_net_mock_battle_all_enemies_defeated() &&
            g_mockBattleOperateSessionArmed == 0 &&
            g_mockBattleAwaitingSettlement == 0)
        {
            vm_net_mock_battle_reset_enemy_hp_from_stats(
                g_vm_net_mock_battle_enemy_id_current);
        }
    }
    if (!terminalFollowup && g_mockBattleRoleHpCurrent == 0)
        g_mockBattleRoleHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP",
                                                        vm_net_mock_role_current_hp_for_battle());
    if (!terminalFollowup && !vm_net_mock_battle_all_enemies_defeated())
        requestedTargetSlot = vm_net_mock_battle_select_live_enemy_wire(requestedTargetSlot,
                                                                        playerOnRight,
                                                                        battleSide,
                                                                        enemySlot);
    if (!terminalFollowup && g_mockBattlePendingEnemyTurn != 0 &&
        !vm_net_mock_battle_all_enemies_defeated() && g_mockBattleRoleHpCurrent > 0)
    {
        u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                         vm_net_mock_battle_first_alive_enemy_wire(playerOnRight,
                                                                                                   battleSide,
                                                                                                   enemySlot));
        u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                          playerSlot);
        u8 strikeActionType = counterActionType;
        u32 strikeEffectIndex = type1EffectIndex;
        bool strikeIsHeal = false;
        u32 strikeHealAmount = 0;
        counterDamageValue = vm_net_mock_battle_apply_enemy_counter_strike(
            g_vm_net_mock_battle_enemy_id_current, 0, counterActionType, type1EffectIndex,
            &strikeActionType, &strikeEffectIndex, &strikeIsHeal, &strikeHealAmount);
        if (strikeActionType == 1)
        {
            counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                          counterActorWireSlot);
            counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                           counterTargetWireSlot);
        }
        if (strikeIsHeal)
        {
            counterTargetWireSlot = counterActorWireSlot;
            counterRecordChildFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL;
            counterHpDelta = strikeHealAmount;
        }
        else
        {
            counterRecordChildFlag = vm_net_mock_battle_child_flag_with_env(
                "CBE_BATTLE_COUNTER_CHILD_FLAG",
                vm_net_mock_battle_take_outcome_child_flag());
            counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
            counterHpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_A", counterHpDelta);
        }
        counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
        g_mockBattlePendingEnemyTurn = 0;
        ++g_mockBattleOperateTurnCounter;
        {
            u32 pendingLen = vm_net_mock_build_battle_enemy_turn_response(out, outCap,
                                                                          strikeActionType,
                                                                          counterActorWireSlot,
                                                                          counterTargetWireSlot,
                                                                          counterRecordChildFlag,
                                                                          counterHpDelta,
                                                                          counterRecordMpDelta,
                                                                          playerSlot,
                                                                          strikeEffectIndex);
            if (g_mockBattleRoleHpCurrent == 0)
            {
                g_mockBattleOperateSessionArmed = 0;
                g_mockBattleOperateSessionFinished = 0;
                g_mockBattlePendingEnemyTurn = 0;
                g_mockBattleAwaitingSettlement = 0;
                vm_net_mock_battle_settlement_exit_clear("battle-pending-enemy-death");
                vm_net_mock_battle_post_exit_settle_clear("battle-pending-enemy-death");
                vm_net_mock_battle_save_completed_current_role_state(
                    "battle-pending-enemy-death");
                vm_mock_service_session_arm_battle_revival_confirm_for_death(
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
                operate, playerSlot, requestedTargetSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsFriendlyGroupModifier)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_friendly_group_modifier_targets(
                operate, playerSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsEnemyStatus)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_enemy_status_targets(
                operate, requestedTargetSlot, playerOnRight, battleSide, enemySlot,
                attackWireSlots, attackDamageValues);
        }
        else
        {
            attackTargetCount = vm_net_mock_battle_apply_player_attack_targets(
                operate, operateIsSkill, skillTargetsEnemyGroup, requestedTargetSlot,
                playerOnRight, battleSide, enemySlot, attackWireSlots, attackDamageValues,
                attackChildFlags, deathActionWireSlots, &deathActionTargetCount);
        }
        if (attackTargetCount == 0)
        {
            printf("[error][network] mock_battle_operate_abort reason=no-targets index=%u operate=%u skill=%u group=%u enemyhp=%u slots=%u/%u/%u evidence=actioninfo-targets\n",
                   index,
                   operate,
                   operateIsSkill ? 1u : 0u,
                   skillTargetsEnemyGroup ? 1u : 0u,
                   g_mockBattleEnemyHpCurrent,
                   g_mockBattleEnemyHpSlots[0],
                   g_mockBattleEnemyHpSlots[1],
                   g_mockBattleEnemyHpSlots[2]);
            return 0;
        }
        attackDamageValue = attackDamageValues[0];
        deathActionNeeded = deathActionTargetCount != 0;
        deathActionWireSlot = deathActionNeeded ? deathActionWireSlots[0] : 0;
        if (bundleWholeRound && !vm_net_mock_battle_all_enemies_defeated() &&
            g_mockBattleRoleHpCurrent > 0)
        {
            counterWireCount = vm_net_mock_battle_collect_live_enemy_wires(
                playerOnRight, battleSide, enemySlot, counterWireSlots);
        }
        allowCounterattack = bundleWholeRound && counterWireCount != 0 &&
                             !vm_net_mock_battle_all_enemies_defeated() &&
                             g_mockBattleRoleHpCurrent > 0;
        if (allowCounterattack)
        {
            for (u8 i = 0; i < counterWireCount && i < 3 && g_mockBattleRoleHpCurrent > 0; ++i)
            {
                u8 strikeActionType = counterActionType;
                u32 strikeEffectIndex = type1EffectIndex;
                bool strikeIsHeal = false;
                u32 strikeHealAmount = 0;
                u32 oneCounterDamage = vm_net_mock_battle_apply_enemy_counter_strike(
                    g_vm_net_mock_battle_enemy_id_current, i, counterActionType,
                    type1EffectIndex, &strikeActionType, &strikeEffectIndex, &strikeIsHeal, &strikeHealAmount);
                /* Miss (0) still fills the slot so encode emits valueA=0 + 闪躲. */
                counterDamageValues[i] = oneCounterDamage;
                counterIsHeal[i] = strikeIsHeal;
                counterHealAmounts[i] = strikeHealAmount;
                counterActionTypes[i] = strikeActionType;
                counterEffectIndices[i] = strikeEffectIndex;
                counterChildFlags[i] = strikeIsHeal
                    ? VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL
                    : vm_net_mock_battle_take_outcome_child_flag();
                counterDamageValue = vm_net_mock_add_capped_u32(counterDamageValue,
                                                                oneCounterDamage);
            }
        }
        battleEndsThisRound = (vm_net_mock_battle_all_enemies_defeated() ||
                               g_mockBattleRoleHpCurrent == 0);
    }
    if (!terminalFollowup)
    {
        attackHpDelta = skillSupportNoDamage ? attackDamageValue :
                        vm_net_mock_battle_negative_delta_u32(attackDamageValue);
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
    }
    attackHpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_A", attackHpDelta);
    counterHpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_A", counterHpDelta);
    skillMpDelta = vm_net_mock_env_u32("CBE_BATTLE_SKILL_MP_VALUE_B",
                                       skillMpPrepared ? skillMpAfter :
                                                         vm_net_mock_battle_role_mp_current());
    firstRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_B", 0);
    for (u8 i = 0; i < attackTargetCount && i < 3; ++i)
    {
        attackChildFlags[i] = vm_net_mock_battle_child_flag_with_env(
            "CBE_BATTLE_FIRST_CHILD_FLAG",
            skillSupportNoDamage ? firstRecordChildFlag : attackChildFlags[i]);
        attackChildValueAs[i] = skillSupportNoDamage ? attackDamageValues[i] :
                                vm_net_mock_battle_negative_delta_u32(attackDamageValues[i]);
        attackChildValueBs[i] = firstRecordMpDelta;
    }
    if (attackTargetCount != 0)
        attackChildValueAs[0] = attackHpDelta;
    if (operateIsSkill && skillMpPrepared)
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        skillTeamRoleId = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_ROLE_ID",
                                              role ? role->roleId :
                                                     VM_NET_MOCK_ROLE_DEFAULT_ID);
        skillTeamHp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_HP",
                                          g_mockBattleRoleHpCurrent);
        skillTeamMp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_MP",
                                          skillMpDelta);
        skillTeamInfoEnabled = skillTeamRoleId != 0;
    }
    skillCostValueA = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_VALUE_A",
                                          g_mockBattleRoleHpCurrent);
    skillCostValueB = skillMpDelta;
    counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
    if (operateIsSkill && skillMpPrepared)
        vm_net_mock_battle_commit_skill_mp(skillMpAfter);
    if (terminalFollowup)
    {
        u8 terminalObjectCount = 0;
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &terminalObjectCount, false))
            return 0;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &terminalObjectCount,
                                                               "battle-operate-terminal",
                                                               true))
            return 0;
        if (!vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos))
            return 0;
        ++terminalObjectCount;
        if (!vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos))
            return 0;
        ++terminalObjectCount;
        if (!vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            return 0;
        ++terminalObjectCount;
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
            if (!skillTargetsEnemyGroup && !skillSupportNoDamage)
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
        if (!skillSupportNoDamage && mappedTargetWireSlot == playerSlot)
        {
            printf("[error][network] mock_battle_operate_abort reason=self-target "
                   "index=%u operate=%u actor=%u target=%u player=%u evidence=self-hit-guard\n",
                   index, operate, mappedActorWireSlot, mappedTargetWireSlot,
                   playerSlot);
            return 0;
        }

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
                    printf("[error][network] mock_battle_operate_abort reason=actioninfo-overflow phase=group-skill targets=%u used=%u cap=%u operate=%u evidence=HandleBattleActionMsg:0x6EB0\n",
                           attackTargetCount,
                           actionInfoLen,
                           (u32)sizeof(actionInfo),
                           operate);
                    return 0;
                }
            }
            else if (!vm_net_mock_append_battle_actioninfo_record(
                         actionInfo, sizeof(actionInfo), &actionInfoLen, firstActionType,
                         mappedActorWireSlot, mappedTargetWireSlot,
                         attackTargetCount > 0 ? attackChildFlags[0] : firstRecordChildFlag,
                         attackHpDelta, firstRecordMpDelta,
                         (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail0 : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail1 : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail2 : 0))
            {
                printf("[error][network] mock_battle_operate_abort reason=actioninfo-overflow phase=first-action used=%u cap=%u operate=%u evidence=HandleBattleActionMsg:0x6EB0\n",
                       actionInfoLen,
                       (u32)sizeof(actionInfo),
                       operate);
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
                u8 strikeActionType = counterActionTypes[i];
                u32 strikeEffectIndex = counterEffectIndices[i];
                u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                                 counterWireSlots[i]);
                u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                                  playerSlot);
                if (strikeActionType == 1)
                {
                    counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                                  counterActorWireSlot);
                    counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                                   counterTargetWireSlot);
                }
                if (counterIsHeal[i])
                    counterTargetWireSlot = counterActorWireSlot;
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, strikeActionType,
                                                                 counterActorWireSlot, counterTargetWireSlot,
                                                                 vm_net_mock_battle_child_flag_with_env(
                                                                     "CBE_BATTLE_COUNTER_CHILD_FLAG",
                                                                     counterChildFlags[i]),
                                                                 counterIsHeal[i]
                                                                     ? counterHealAmounts[i]
                                                                     : vm_net_mock_battle_negative_delta_u32(counterDamageValues[i]),
                                                                 counterRecordMpDelta,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? strikeEffectIndex : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail0 : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail1 : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail2 : 0))
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
            vm_net_mock_battle_all_enemies_defeated() &&
            terminalActionEnabled &&
            !deathActionNeeded)
        {
            u8 terminalActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTOR_WIRE_SLOT",
                                                              vm_net_mock_battle_all_enemies_defeated()
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
    /*
     * Solo battles may inline 4/7 with the killing 4/6.  Team battles must not:
     * auto-flask mutates g_mockBattleRoleMpCurrent before the round merge reads
     * it for party teaminfo, so the killing skill's type-1 playback restores MP
     * to full while the later settlement UI still shows recover_mp=0 (the merged
     * path strips kind=4 companions).  Team victory settlement is appended after
     * teaminfo in merge/terminal_release instead.
     */
    if (battleEndsThisRound &&
        vm_net_mock_battle_all_enemies_defeated() &&
        g_mockBattleRoleHpCurrent > 0 &&
        g_vm_net_mock_team_battle_party_count_current < 2 &&
        vm_net_mock_battle_inline_settlement_enabled())
    {
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &responseObjectCount, false))
            return 0;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &responseObjectCount,
                                                               "battle-operate-inline",
                                                               true))
            return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, (u8)responseObjectCount);
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    /*
     * Arm playback hold before victory exit so 4/8 waits for this round's
     * actioninfo (hit+death[+counters]) plus the settle panel paint window.
     */
    if (operateConsumesTurn && actionCount != 0)
        vm_net_mock_battle_note_round_playback_hold(actionCount, "battle-operate");
    if (battleEndsThisRound)
    {
        if (g_mockBattleRoleHpCurrent == 0)
        {
            vm_net_mock_battle_save_completed_current_role_state(
                "battle-operate-death");
            vm_mock_service_session_arm_battle_revival_confirm_for_death(
                "battle-operate-death");
        }
        else if (g_vm_net_mock_team_battle_party_count_current < 2)
        {
            vm_net_mock_battle_save_terminal_role_state("battle-operate", false);
        }
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        if (g_mockBattleRoleHpCurrent == 0)
        {
            g_mockBattleAwaitingSettlement = 0;
            vm_net_mock_battle_settlement_exit_clear("battle-operate-death");
            vm_net_mock_battle_post_exit_settle_clear("battle-operate-death");
            vm_net_mock_hangup_loop_clear("battle-operate-death");
        }
        else
        {
            vm_net_mock_battle_note_victory_settlement("battle-operate-victory");
            vm_net_mock_hangup_loop_note_victory_reentry("battle-operate-victory");
        }
    }
    else if (g_mockBattleOperateSessionArmed != 0 && operateConsumesTurn && !bundleWholeRound)
    {
        g_mockBattlePendingEnemyTurn = 1;
    }
    if (g_vm_net_mock_team_battle_party_count_current < 2 && operateConsumesTurn &&
        !skillTargetsFriendlyGroupModifier)
    {
        vm_net_mock_battle_modifier_advance_round(&g_vm_net_mock_battle_solo_modifier);
        g_vm_net_mock_battle_active_modifier_current = g_vm_net_mock_battle_solo_modifier;
        (void)vm_net_mock_battle_enemy_ailments_advance_round();
    }
    vm_net_mock_battle_remember_last_operate(
        firstRecordWireTargetUsed,
        operate,
        operateConsumesTurn);
    printf("[info][network] mock_battle_operate index=%u operate=%u skill=%u target_mode=%u targets=%u wires=%u/%u/%u amount=%u/%u/%u action=%u actions=%u effect=%u actor=%u target=%u enemyhp=%u slots=%u/%u/%u rolehp=%u counters=%u deaths=%u deathActor=%u counterdmg=%u mpcost=%u valueB=%u teaminfo=%u:%u/%u bundle=%u pending=%u order=%s terminal=%u costAction=%u costHp=%u costMp=%u mp=%u/%u resp=%u evidence=skill.dsh:目标指向,mmBattle:0x6EB0\n",
           index, operate, operateIsSkill ? 1 : 0,
           skillTargetsEnemyGroup ? 4 :
               ((skillTargetsFriendlyGroupHeal || skillTargetsFriendlyGroupModifier) ? 2 : 0),
           attackTargetCount,
           attackWireSlots[0], attackWireSlots[1], attackWireSlots[2],
           attackDamageValues[0], attackDamageValues[1], attackDamageValues[2],
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
    vm_autotest_note("mock_battle_operate index=%u operate=%u skill=%u target_mode=%u targets=%u wires=%u/%u/%u amount=%u/%u/%u action=%u actions=%u effect=%u actor=%u target=%u enemyhp=%u slots=%u/%u/%u rolehp=%u counters=%u deaths=%u deathActor=%u counterdmg=%u mpcost=%u valueB=%u teaminfo=%u:%u/%u bundle=%u pending=%u order=%s terminal=%u costAction=%u costHp=%u costMp=%u mp=%u/%u response=4/6 evidence=skill.dsh:目标指向,mmBattle:0x6EB0\n",
                     index, operate, operateIsSkill ? 1 : 0,
                     skillTargetsEnemyGroup ? 4 :
                         ((skillTargetsFriendlyGroupHeal || skillTargetsFriendlyGroupModifier) ? 2 : 0),
                     attackTargetCount,
                     attackWireSlots[0], attackWireSlots[1], attackWireSlots[2],
                     attackDamageValues[0], attackDamageValues[1], attackDamageValues[2],
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
    u8 actionInfo[VM_NET_MOCK_BATTLE_OPERATE_ACTIONINFO_CAP];
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
    bool skillTargetsEnemyStatus = false;
    bool skillSupportNoDamage = false;
    u8 attackWireSlots[3] = {0, 0, 0};
    u32 attackDamageValues[3] = {0, 0, 0};
    u8 attackChildFlags[3] = {0, 0, 0};
    u32 attackChildValueAs[3] = {0, 0, 0};
    u32 attackChildValueBs[3] = {0, 0, 0};
    u8 attackTargetCount = 0;
    u8 deathActionWireSlots[3] = {0, 0, 0};
    u8 deathActionTargetCount = 0;
    u8 counterWireSlots[3] = {0, 0, 0};
    u32 counterDamageValues[3] = {0, 0, 0};
    bool counterIsHeal[3] = {false, false, false};
    u32 counterHealAmounts[3] = {0, 0, 0};
    u8 counterChildFlags[3] = {0, 0, 0};
    u8 counterActionTypes[3] = {0, 0, 0};
    u32 counterEffectIndices[3] = {0, 0, 0};
    u8 counterWireCount = 0;
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
    if (g_mockBattleOperateSessionArmed == 0 &&
        g_mockBattleAwaitingSettlement == 0)
    {
        vm_net_mock_finish_wt_packet(out, pos, 0);
        printf("[info][network] mock_battle_operate_fallback_ignore "
               "reason=no-armed-session action=empty-ack\n");
        return pos;
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
    operateIsSkill = vm_net_mock_battle_operate_is_skill(operate);
    operateConsumesTurn = operate == 0 || operateIsSkill;
    skillTargetsEnemyGroup = operateIsSkill &&
                             vm_net_mock_battle_operate_skill_targets_enemy_group(operate);
    skillTargetsFriendlyGroupHeal = operateIsSkill &&
                                    vm_net_mock_battle_operate_skill_targets_friendly_group_heal(operate);
    skillTargetsFriendlyGroupModifier = operateIsSkill &&
                                        vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(operate);
    skillTargetsEnemyStatus = operateIsSkill &&
                              vm_net_mock_battle_operate_skill_targets_enemy_status_no_damage(operate);
    skillSupportNoDamage = skillTargetsFriendlyGroupHeal ||
                           skillTargetsFriendlyGroupModifier ||
                           skillTargetsEnemyStatus;
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
    /*
     * Offensive skills: never keep a self/party wire as the attack target.
     * Friendly heal/buff: keep the raw 4/2 index so td=1 can heal the selected
     * ally (清风拂面).  target_wire_slot_from_request remaps non-enemy indices
     * onto the first live monster and would destroy ally selection.
     */
    if (skillTargetsFriendlyGroupHeal || skillTargetsFriendlyGroupModifier)
        requestedTargetSlot = actorSlot;
    else if (requestedTargetSlot == playerSlot || requestedTargetSlot > 5)
        requestedTargetSlot = enemySlot;
    if (g_mockBattleOperateSessionFinished != 0)
        g_mockBattleOperateSessionFinished = 0;
    terminalFollowup = false;
    bundleWholeRound = g_mockBattleOperateSessionArmed != 0 &&
                       (g_vm_net_mock_team_battle_party_count_current >= 2
                            ? g_vm_net_mock_team_battle_resolve_monsters_current != 0
                            : (operateConsumesTurn &&
                               vm_net_mock_env_u32("CBE_BATTLE_BUNDLE_ROUND", 1) != 0));
    firstRecordActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_ACTOR_WIRE_SLOT",
                                                       playerSlot);
    firstRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_FIRST_CHILD_FLAG", 0);
    counterRecordChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    if (g_mockBattleAwaitingSettlement != 0)
    {
        (void)requestedTargetSlot;
        return vm_net_mock_build_battle_pending_settlement_response(out, outCap);
    }
    if (!terminalFollowup)
    {
        vm_net_mock_battle_ensure_multi_enemy_slots_seeded(
            g_vm_net_mock_battle_enemy_id_current);
        if (vm_net_mock_battle_all_enemies_defeated() &&
            g_mockBattleOperateSessionArmed == 0 &&
            g_mockBattleAwaitingSettlement == 0)
        {
            vm_net_mock_battle_reset_enemy_hp_from_stats(
                g_vm_net_mock_battle_enemy_id_current);
        }
    }
    if (!terminalFollowup && g_mockBattleRoleHpCurrent == 0)
        g_mockBattleRoleHpCurrent = vm_net_mock_env_u32("CBE_BATTLE_ROLE_HP",
                                                        vm_net_mock_role_current_hp_for_battle());
    if (!terminalFollowup && !vm_net_mock_battle_all_enemies_defeated())
        requestedTargetSlot = vm_net_mock_battle_select_live_enemy_wire(requestedTargetSlot,
                                                                        playerOnRight,
                                                                        battleSide,
                                                                        enemySlot);
    if (!terminalFollowup && g_mockBattlePendingEnemyTurn != 0 &&
        !vm_net_mock_battle_all_enemies_defeated() && g_mockBattleRoleHpCurrent > 0)
    {
        u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                         vm_net_mock_battle_first_alive_enemy_wire(playerOnRight,
                                                                                                   battleSide,
                                                                                                   enemySlot));
        u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                          playerSlot);
        u8 strikeActionType = counterActionType;
        u32 strikeEffectIndex = type1EffectIndex;
        bool strikeIsHeal = false;
        u32 strikeHealAmount = 0;
        counterDamageValue = vm_net_mock_battle_apply_enemy_counter_strike(
            g_vm_net_mock_battle_enemy_id_current, 0, counterActionType, type1EffectIndex,
            &strikeActionType, &strikeEffectIndex, &strikeIsHeal, &strikeHealAmount);
        if (strikeActionType == 1)
        {
            counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                          counterActorWireSlot);
            counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                           counterTargetWireSlot);
        }
        if (strikeIsHeal)
        {
            counterTargetWireSlot = counterActorWireSlot;
            counterRecordChildFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL;
            counterHpDelta = strikeHealAmount;
        }
        else
        {
            counterRecordChildFlag = vm_net_mock_battle_child_flag_with_env(
                "CBE_BATTLE_COUNTER_CHILD_FLAG",
                vm_net_mock_battle_take_outcome_child_flag());
            counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
            counterHpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_A", counterHpDelta);
        }
        counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
        g_mockBattlePendingEnemyTurn = 0;
        ++g_mockBattleOperateTurnCounter;
        {
            u32 pendingLen = vm_net_mock_build_battle_enemy_turn_response(out, outCap,
                                                                          strikeActionType,
                                                                          counterActorWireSlot,
                                                                          counterTargetWireSlot,
                                                                          counterRecordChildFlag,
                                                                          counterHpDelta,
                                                                          counterRecordMpDelta,
                                                                          playerSlot,
                                                                          strikeEffectIndex);
            if (g_mockBattleRoleHpCurrent == 0)
            {
                g_mockBattleOperateSessionArmed = 0;
                g_mockBattleOperateSessionFinished = 0;
                g_mockBattlePendingEnemyTurn = 0;
                g_mockBattleAwaitingSettlement = 0;
                vm_net_mock_battle_settlement_exit_clear(
                    "battle-pending-enemy-fallback-death");
                vm_net_mock_battle_post_exit_settle_clear(
                    "battle-pending-enemy-fallback-death");
                vm_net_mock_battle_save_completed_current_role_state(
                    "battle-pending-enemy-fallback-death");
                vm_mock_service_session_arm_battle_revival_confirm_for_death(
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
                operate, playerSlot, requestedTargetSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsFriendlyGroupModifier)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_friendly_group_modifier_targets(
                operate, playerSlot, attackWireSlots, attackDamageValues);
        }
        else if (skillTargetsEnemyStatus)
        {
            attackTargetCount = vm_net_mock_battle_apply_player_enemy_status_targets(
                operate, requestedTargetSlot, playerOnRight, battleSide, enemySlot,
                attackWireSlots, attackDamageValues);
        }
        else
        {
            attackTargetCount = vm_net_mock_battle_apply_player_attack_targets(
                operate, operateIsSkill, skillTargetsEnemyGroup, requestedTargetSlot,
                playerOnRight, battleSide, enemySlot, attackWireSlots, attackDamageValues,
                attackChildFlags, deathActionWireSlots, &deathActionTargetCount);
        }
        if (attackTargetCount == 0)
        {
            printf("[error][network] mock_battle_operate_abort reason=no-targets index=%u operate=%u skill=%u group=%u enemyhp=%u slots=%u/%u/%u evidence=actioninfo-targets\n",
                   index,
                   operate,
                   operateIsSkill ? 1u : 0u,
                   skillTargetsEnemyGroup ? 1u : 0u,
                   g_mockBattleEnemyHpCurrent,
                   g_mockBattleEnemyHpSlots[0],
                   g_mockBattleEnemyHpSlots[1],
                   g_mockBattleEnemyHpSlots[2]);
            return 0;
        }
        attackDamageValue = attackDamageValues[0];
        deathActionNeeded = deathActionTargetCount != 0;
        deathActionWireSlot = deathActionNeeded ? deathActionWireSlots[0] : 0;
        if (bundleWholeRound && !vm_net_mock_battle_all_enemies_defeated() &&
            g_mockBattleRoleHpCurrent > 0)
        {
            counterWireCount = vm_net_mock_battle_collect_live_enemy_wires(
                playerOnRight, battleSide, enemySlot, counterWireSlots);
        }
        allowCounterattack = bundleWholeRound && counterWireCount != 0 &&
                             !vm_net_mock_battle_all_enemies_defeated() &&
                             g_mockBattleRoleHpCurrent > 0;
        if (allowCounterattack)
        {
            for (u8 i = 0; i < counterWireCount && i < 3 && g_mockBattleRoleHpCurrent > 0; ++i)
            {
                u8 strikeActionType = counterActionType;
                u32 strikeEffectIndex = type1EffectIndex;
                bool strikeIsHeal = false;
                u32 strikeHealAmount = 0;
                u32 oneCounterDamage = vm_net_mock_battle_apply_enemy_counter_strike(
                    g_vm_net_mock_battle_enemy_id_current, i, counterActionType,
                    type1EffectIndex, &strikeActionType, &strikeEffectIndex, &strikeIsHeal, &strikeHealAmount);
                /* Miss (0) still fills the slot so encode emits valueA=0 + 闪躲. */
                counterDamageValues[i] = oneCounterDamage;
                counterIsHeal[i] = strikeIsHeal;
                counterHealAmounts[i] = strikeHealAmount;
                counterActionTypes[i] = strikeActionType;
                counterEffectIndices[i] = strikeEffectIndex;
                counterChildFlags[i] = strikeIsHeal
                    ? VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL
                    : vm_net_mock_battle_take_outcome_child_flag();
                counterDamageValue = vm_net_mock_add_capped_u32(counterDamageValue,
                                                                oneCounterDamage);
            }
        }
        battleEndsThisRound = (vm_net_mock_battle_all_enemies_defeated() ||
                               g_mockBattleRoleHpCurrent == 0);
    }
    if (!terminalFollowup)
    {
        attackHpDelta = skillSupportNoDamage ? attackDamageValue :
                        vm_net_mock_battle_negative_delta_u32(attackDamageValue);
        counterHpDelta = vm_net_mock_battle_negative_delta_u32(counterDamageValue);
    }
    attackHpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_A", attackHpDelta);
    counterHpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_A", counterHpDelta);
    skillMpDelta = vm_net_mock_env_u32("CBE_BATTLE_SKILL_MP_VALUE_B",
                                       skillMpPrepared ? skillMpAfter :
                                                         vm_net_mock_battle_role_mp_current());
    firstRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_FIRST_VALUE_B", 0);
    for (u8 i = 0; i < attackTargetCount && i < 3; ++i)
    {
        attackChildFlags[i] = vm_net_mock_battle_child_flag_with_env(
            "CBE_BATTLE_FIRST_CHILD_FLAG",
            skillSupportNoDamage ? firstRecordChildFlag : attackChildFlags[i]);
        attackChildValueAs[i] = skillSupportNoDamage ? attackDamageValues[i] :
                                vm_net_mock_battle_negative_delta_u32(attackDamageValues[i]);
        attackChildValueBs[i] = firstRecordMpDelta;
    }
    if (attackTargetCount != 0)
        attackChildValueAs[0] = attackHpDelta;
    if (operateIsSkill && skillMpPrepared)
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        skillTeamRoleId = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_ROLE_ID",
                                              role ? role->roleId :
                                                     VM_NET_MOCK_ROLE_DEFAULT_ID);
        skillTeamHp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_HP",
                                          g_mockBattleRoleHpCurrent);
        skillTeamMp = vm_net_mock_env_u32("CBE_BATTLE_TEAMINFO_MP",
                                          skillMpDelta);
        skillTeamInfoEnabled = skillTeamRoleId != 0;
    }
    skillCostValueA = vm_net_mock_env_u32("CBE_BATTLE_SKILL_COST_VALUE_A",
                                          g_mockBattleRoleHpCurrent);
    skillCostValueB = skillMpDelta;
    counterRecordMpDelta = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
    if (operateIsSkill && skillMpPrepared)
        vm_net_mock_battle_commit_skill_mp(skillMpAfter);
    if (terminalFollowup)
    {
        u8 terminalObjectCount = 0;
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &terminalObjectCount, false))
            return 0;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &terminalObjectCount,
                                                               "battle-operate-fallback-terminal",
                                                               true))
            return 0;
        if (!vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos))
            return 0;
        ++terminalObjectCount;
        if (!vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos))
            return 0;
        ++terminalObjectCount;
        if (!vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            return 0;
        ++terminalObjectCount;
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
            if (!skillTargetsEnemyGroup && !skillSupportNoDamage)
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
        if (!skillSupportNoDamage && mappedTargetWireSlot == playerSlot)
        {
            printf("[error][network] mock_battle_operate_abort reason=self-target "
                   "index=%u operate=%u actor=%u target=%u player=%u evidence=self-hit-guard\n",
                   index, operate, mappedActorWireSlot, mappedTargetWireSlot,
                   playerSlot);
            return 0;
        }
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
                    printf("[error][network] mock_battle_operate_abort reason=actioninfo-overflow phase=group-skill targets=%u used=%u cap=%u operate=%u evidence=HandleBattleActionMsg:0x6EB0\n",
                           attackTargetCount,
                           actionInfoLen,
                           (u32)sizeof(actionInfo),
                           operate);
                    return 0;
                }
            }
            else if (!vm_net_mock_append_battle_actioninfo_record(
                         actionInfo, sizeof(actionInfo), &actionInfoLen, firstActionType,
                         mappedActorWireSlot, mappedTargetWireSlot,
                         attackTargetCount > 0 ? attackChildFlags[0] : firstRecordChildFlag,
                         attackHpDelta, firstRecordMpDelta,
                         (firstActionType == 1 || firstActionType == 2) ? type1EffectIndex : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail0 : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail1 : 0,
                         (firstActionType == 1 || firstActionType == 2) ? type1Tail2 : 0))
            {
                printf("[error][network] mock_battle_operate_abort reason=actioninfo-overflow phase=first-action used=%u cap=%u operate=%u evidence=HandleBattleActionMsg:0x6EB0\n",
                       actionInfoLen,
                       (u32)sizeof(actionInfo),
                       operate);
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
                u8 strikeActionType = counterActionTypes[i];
                u32 strikeEffectIndex = counterEffectIndices[i];
                u8 counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                                 counterWireSlots[i]);
                u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                                  playerSlot);
                if (strikeActionType == 1)
                {
                    counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                                  counterActorWireSlot);
                    counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                                   counterTargetWireSlot);
                }
                if (counterIsHeal[i])
                    counterTargetWireSlot = counterActorWireSlot;
                if (actionCount < 6)
                    ++actionCount;
                else
                    return 0;
                if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                                 &actionInfoLen, strikeActionType,
                                                                 counterActorWireSlot, counterTargetWireSlot,
                                                                 vm_net_mock_battle_child_flag_with_env(
                                                                     "CBE_BATTLE_COUNTER_CHILD_FLAG",
                                                                     counterChildFlags[i]),
                                                                 counterIsHeal[i]
                                                                     ? counterHealAmounts[i]
                                                                     : vm_net_mock_battle_negative_delta_u32(counterDamageValues[i]),
                                                                 counterRecordMpDelta,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? strikeEffectIndex : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail0 : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail1 : 0,
                                                                 (strikeActionType == 1 || strikeActionType == 2) ? type1Tail2 : 0))
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
            vm_net_mock_battle_all_enemies_defeated() &&
            terminalActionEnabled &&
            !deathActionNeeded)
        {
            u8 terminalActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TERMINAL_ACTOR_WIRE_SLOT",
                                                              vm_net_mock_battle_all_enemies_defeated()
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
    if (battleEndsThisRound &&
        vm_net_mock_battle_all_enemies_defeated() &&
        g_mockBattleRoleHpCurrent > 0 &&
        g_vm_net_mock_team_battle_party_count_current < 2 &&
        vm_net_mock_battle_inline_settlement_enabled())
    {
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &responseObjectCount, false))
            return 0;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                               &responseObjectCount,
                                                               "battle-operate-fallback-inline",
                                                               true))
            return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, (u8)responseObjectCount);
    if (g_mockBattleOperateSessionArmed != 0)
        ++g_mockBattleOperateTurnCounter;
    if (operateConsumesTurn && actionCount != 0)
        vm_net_mock_battle_note_round_playback_hold(actionCount,
                                                    "battle-operate-fallback");
    if (battleEndsThisRound)
    {
        if (g_mockBattleRoleHpCurrent == 0)
        {
            vm_net_mock_battle_save_completed_current_role_state(
                "battle-operate-fallback-death");
            vm_mock_service_session_arm_battle_revival_confirm_for_death(
                "battle-operate-fallback-death");
        }
        else if (g_vm_net_mock_team_battle_party_count_current < 2)
        {
            vm_net_mock_battle_save_terminal_role_state("battle-operate-fallback",
                                                        false);
        }
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        if (g_mockBattleRoleHpCurrent == 0)
        {
            g_mockBattleAwaitingSettlement = 0;
            vm_net_mock_battle_settlement_exit_clear(
                "battle-operate-fallback-death");
            vm_net_mock_battle_post_exit_settle_clear(
                "battle-operate-fallback-death");
            vm_net_mock_hangup_loop_clear("battle-operate-fallback-death");
        }
        else
        {
            vm_net_mock_battle_note_victory_settlement(
                "battle-operate-fallback-victory");
            vm_net_mock_hangup_loop_note_victory_reentry(
                "battle-operate-fallback-victory");
        }
    }
    else if (g_mockBattleOperateSessionArmed != 0 && operateConsumesTurn && !bundleWholeRound)
    {
        g_mockBattlePendingEnemyTurn = 1;
    }
    if (g_vm_net_mock_team_battle_party_count_current < 2 && operateConsumesTurn &&
        !skillTargetsFriendlyGroupModifier)
    {
        vm_net_mock_battle_modifier_advance_round(&g_vm_net_mock_battle_solo_modifier);
        g_vm_net_mock_battle_active_modifier_current = g_vm_net_mock_battle_solo_modifier;
        (void)vm_net_mock_battle_enemy_ailments_advance_round();
    }
    vm_net_mock_battle_remember_last_operate(
        firstRecordWireTargetUsed,
        operate,
        operateConsumesTurn);
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

static u8 vm_mock_service_team_battle_alive_mask(const vm_mock_service_team *team)
{
    u8 mask = 0;

    if (team == NULL)
        return 0;
    for (u8 i = 0; i < team->battleMemberCount && i < 8; ++i)
    {
        if ((team->battleMemberLeftMask & (u8)(1u << i)) != 0)
            continue;
        if (team->battleMemberHp[i] != 0)
            mask = (u8)(mask | (u8)(1u << i));
    }
    return mask;
}

static u8 vm_mock_service_team_battle_absent_mask(const vm_mock_service_team *team)
{
    u8 mask = 0;

    if (team == NULL)
        return 0;
    mask = team->battleMemberLeftMask;
    for (u8 i = 0; i < team->battleMemberCount && i < 8; ++i)
    {
        if (team->battleMemberHp[i] == 0)
            mask = (u8)(mask | (u8)(1u << i));
    }
    return mask;
}

static void vm_mock_service_team_battle_mark_member_events_delivered(
    vm_mock_service_team *team,
    u8 memberBit)
{
    u8 fullMask = 0;

    if (team == NULL || memberBit == 0)
        return;
    fullMask = (u8)((1u << team->battleMemberCount) - 1u);
    for (u8 i = 0; i < VM_MOCK_SERVICE_TEAM_BATTLE_EVENT_MAX; ++i)
    {
        vm_mock_service_team_battle_event *event = &team->battleEvents[i];

        if (!event->valid)
            continue;
        event->deliveredMask = (u8)(event->deliveredMask | memberBit);
        if (event->deliveredMask == fullMask)
            event->valid = false;
    }
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
    u16 actionInfoLen = 0;
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
    pending->actionInfoLen = actionInfoLen;
    memcpy(pending->actionInfo, actionInfo, actionInfoLen);
    printf("[info][mock-service] team_battle_round_capture battle=%u round=%u "
           "source=%08x actor=%u order=%u actions=%u info=%u acted=%02x alive=%02x\n",
           team->battleSerial,
           team->battleRoundSerial,
           pending->sourceClientId,
           pending->memberIndex,
           pending->serial,
           pending->actionCount,
           pending->actionInfoLen,
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
    u8 teamInfo[64];
    u8 merged[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX * 2];
    const u8 *currentActionInfo = NULL;
    u16 currentActionInfoLen = 0;
    u8 currentActionCount = 0;
    u32 combinedActionInfoLen = 0;
    u32 totalActionCount = 0;
    u32 extraObjectsLen = 0;
    u8 extraObjectCount = 0;
    u8 pendingCount = 0;
    u32 lastSerial = 0;
    u32 teamInfoLen = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos || team == NULL ||
        context->session == NULL ||
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

    memset(teamInfo, 0, sizeof(teamInfo));
    if (!vm_net_mock_build_team_battle_party_teaminfo_blob(
            teamInfo, sizeof(teamInfo), &teamInfoLen,
            context->session, team, context->memberIndex, true))
    {
        return 0;
    }
    memset(merged, 0, sizeof(merged));
    if (!vm_net_mock_append_battle_action6_object_teaminfo_blob(
            merged, sizeof(merged), &pos,
            combinedActionInfo, combinedActionInfoLen,
            (u8)totalActionCount, teamInfo, teamInfoLen) ||
        pos + extraObjectsLen > sizeof(merged) ||
        pos + extraObjectsLen > outCap)
    {
        return 0;
    }
    if (extraObjectsLen != 0)
    {
        memcpy(merged + pos, extraObjects, extraObjectsLen);
        pos += extraObjectsLen;
    }
    {
        u8 objectCount = (u8)(1 + extraObjectCount);

        /*
         * Killing blows that also close the party round used to settle inside
         * the per-actor operate builder.  That ran auto-flask before this merge
         * snapshot, so teaminfo carried post-flask MP and type-1 playback
         * restored the bar immediately; merge also strips kind=4 companions, so
         * the settlement UI later showed recover_mp=0.  Append 4/7 only after
         * the post-cost party teaminfo row is already in the packet.
         */
        if (vm_net_mock_battle_all_enemies_defeated())
        {
            if (!vm_net_mock_append_battle_terminal_status_objects(
                    merged, sizeof(merged), &pos, &objectCount, true))
            {
                return 0;
            }
            g_vm_net_mock_battle_settlement_sent_serial =
                g_mockBattleOperateSessionSerial;
            if (!vm_net_mock_append_battle_drop_refresh7_if_needed(
                    merged, sizeof(merged), &pos, &objectCount,
                    "team-battle-round-merge", true))
            {
                return 0;
            }
            g_mockBattleOperateSessionArmed = 0;
            g_mockBattleOperateSessionFinished = 0;
            g_mockBattlePendingEnemyTurn = 0;
            vm_net_mock_battle_note_victory_settlement("team-battle-round-merge");
            vm_net_mock_battle_save_terminal_role_state("team-battle-round-merge",
                                                        true);
        }
        if (pos > outCap)
            return 0;
        vm_net_mock_finish_wt_packet(merged, pos, objectCount);
    }
    memcpy(out, merged, pos);
    printf("[info][mock-service] team_battle_round_release battle=%u round=%u "
           "source=%08x actor=%u pending=%u actions=%u info=%u teaminfo=%u "
           "extras=%u resp=%u\n",
           team->battleSerial,
           team->battleRoundSerial,
           context && context->session ? context->session->clientId : 0,
           context ? context->memberIndex : 0,
           pendingCount,
           (u8)totalActionCount,
           combinedActionInfoLen,
           teamInfoLen,
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
    u8 teamInfo[64];
    u8 merged[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX * 2];
    u32 combinedActionInfoLen = 0;
    u32 totalActionCount = 0;
    u32 lastSerial = 0;
    u8 pendingCount = 0;
    u8 objectCount = 0;
    u32 teamInfoLen = 0;
    u32 pos = 5;

    if (out == NULL || outCap < pos || team == NULL ||
        context->session == NULL ||
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
        lastSerial = next->serial;
        ++pendingCount;
    }
    memset(teamInfo, 0, sizeof(teamInfo));
    if (!vm_net_mock_build_team_battle_party_teaminfo_blob(
            teamInfo, sizeof(teamInfo), &teamInfoLen,
            context->session, team, context->memberIndex, true))
    {
        return 0;
    }
    memset(merged, 0, sizeof(merged));
    if (pendingCount == 0 || totalActionCount == 0 ||
        !vm_net_mock_append_battle_action6_object_teaminfo_blob(
            merged, sizeof(merged), &pos,
            combinedActionInfo, combinedActionInfoLen,
            (u8)totalActionCount, teamInfo, teamInfoLen))
    {
        return 0;
    }
    ++objectCount;
    if (!vm_net_mock_append_battle_terminal_status_objects(
            merged, sizeof(merged), &pos, &objectCount, true))
        return 0;
    g_vm_net_mock_battle_settlement_sent_serial =
        g_mockBattleOperateSessionSerial;
    if (!vm_net_mock_append_battle_drop_refresh7_if_needed(
            merged, sizeof(merged), &pos, &objectCount,
            "team-battle-terminal-release", true))
    {
        return 0;
    }
    if (pos > outCap)
        return 0;
    vm_net_mock_finish_wt_packet(merged, pos, objectCount);
    memcpy(out, merged, pos);

    g_mockBattleOperateSessionArmed = 0;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    vm_net_mock_battle_note_victory_settlement("team-battle-terminal-release");
    vm_net_mock_battle_save_terminal_role_state("team-battle-terminal-release", true);
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
    /*
     * Dead/fled members may still have bits in actedMask from an earlier
     * submit in this round.  Only living bits may participate in the barrier,
     * otherwise one sacrifice leaves survivors permanently short of equality.
     */
    context.resolvesRound = !context.duplicateAction &&
                            (context.aliveMask & context.memberBit) != 0 &&
                            (u8)((team->battleRoundActedMask | context.memberBit) &
                                 context.aliveMask) == context.aliveMask;
    g_vm_net_mock_team_battle_party_count_current = team->battleMemberCount;
    g_vm_net_mock_team_battle_actor_slot_current = (u8)memberIndex;
    g_vm_net_mock_team_battle_resolve_monsters_current = context.resolvesRound ? 1 : 0;
    g_vm_net_mock_team_battle_member_count_current = team->battleMemberCount;
    g_vm_net_mock_battle_mp_increase_allowed = 0;
    memcpy(g_vm_net_mock_team_battle_member_hp_current, team->battleMemberHp,
           sizeof(g_vm_net_mock_team_battle_member_hp_current));
    memcpy(g_vm_net_mock_team_battle_member_hp_max_current, team->battleMemberHpMax,
           sizeof(g_vm_net_mock_team_battle_member_hp_max_current));
    g_vm_net_mock_team_battle_group_hp_changed_mask = 0;
    memcpy(g_vm_net_mock_team_battle_member_modifiers_current,
           team->battleMemberModifiers,
           sizeof(g_vm_net_mock_team_battle_member_modifiers_current));
    g_vm_net_mock_team_battle_group_modifier_changed_mask = 0;
    g_vm_net_mock_battle_active_modifier_current =
        g_vm_net_mock_team_battle_member_modifiers_current[memberIndex];
    g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleStartUsesSceneWireMaps = 1;
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
    g_vm_net_mock_team_battle_group_modifier_changed_mask = 0;
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    g_vm_net_mock_battle_mp_increase_allowed = 0;
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
    /*
     * Dead and fled observers already left the battle UI (death prompt or
     * escape/revival exit).  Pre-mark them delivered so later survivor rounds
     * are not injected into a client that is no longer parsing 4/6.
     */
    event->deliveredMask = (u8)((1u << context->memberIndex) |
                                vm_mock_service_team_battle_absent_mask(team));
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
    {
        u32 prevMp = team->battleMemberMp[context->memberIndex];
        u32 newMp = g_mockBattleRoleMpCurrent;

        /*
         * Shared team MP is monotonic unless an item/flask explicitly restored
         * it.  Runtime showed unexplained 357->367 refills between rounds; the
         * next party teaminfo row then made InitActionSlot_B restore the bar
         * to max on the following skill playback.
         */
        if (prevMp != 0 && newMp > prevMp &&
            g_vm_net_mock_battle_mp_increase_allowed == 0)
        {
            printf("[warn][mock-service] team_battle_mp_refill_blocked battle=%u "
                   "actor=%u prev=%u new=%u max=%u role_mp=%u party=%u\n",
                   team->battleSerial,
                   context->memberIndex,
                   prevMp,
                   newMp,
                   g_mockBattleRoleMpMax,
                   vm_net_mock_active_role() ? vm_net_mock_active_role()->mp : 0,
                   team->battleMemberCount);
            newMp = prevMp;
            g_mockBattleRoleMpCurrent = prevMp;
        }
        team->battleMemberMp[context->memberIndex] = newMp;
    }
    team->battleMemberMpMax[context->memberIndex] = g_mockBattleRoleMpMax;
    if (team->battleMemberHp[context->memberIndex] == 0)
    {
        /* Death prompt owns this client now; drop undelivered shared 4/6. */
        vm_mock_service_team_battle_mark_member_events_delivered(
            team, context->memberBit);
    }
    /* Keep the durable role row aligned with the shared battle snapshot before
     * the transport-level presence capture runs.  Otherwise a later
     * role-default vitals read can publish max MP into onlineMp and the next
     * teaminfo row refills the caster bar. */
    vm_net_mock_battle_publish_role_vitals();
    g_vm_net_mock_battle_mp_increase_allowed = 0;
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
            (void)vm_net_mock_battle_enemy_ailments_advance_round();
            memcpy(team->battleEnemyHpSlots, g_mockBattleEnemyHpSlots,
                   sizeof(team->battleEnemyHpSlots));
            team->battleEnemyHpCurrent = g_mockBattleEnemyHpCurrent;
            team->battleRoundActedMask = 0;
            ++team->battleRoundSerial;
            if (team->battleRoundSerial == 0)
                team->battleRoundSerial = 1;
        }
    }
    if (publishAction && team->battleEnemyHpCurrent == 0)
        team->battleFinished = true;
    if (vm_mock_service_team_battle_alive_mask(team) == 0)
        team->battleFinished = true;
    if (actionAccepted && publishAction)
        vm_mock_service_team_battle_queue_action(context, response, responseLen);
    if (publishAction)
        vm_mock_service_team_battle_clear_round_actions(team);

    /* Publish the shared battle snapshot straight into the service presence
     * before the next poll.  In particular HP=0 is a real value here, not an
     * absent/default value; the resulting subtype 5/11 update keeps every
     * party HUD in lockstep with the death action in 4/6.  Seats that already
     * fled/revived-out keep the online vitals from their exit path. */
    for (u8 member = 0; member < team->battleMemberCount; ++member)
    {
        vm_mock_service_client_session *memberSession =
            vm_mock_service_find_client_session(team->battleMemberClientIds[member]);
        u8 memberBit = (u8)(1u << member);

        if (memberSession == NULL)
            continue;
        if ((team->battleMemberLeftMask & memberBit) != 0)
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

static int vm_mock_service_team_battle_pick_flush_member(
    const vm_mock_service_team *team)
{
    int best = -1;
    u32 bestSerial = 0;

    if (team == NULL)
        return -1;
    for (u8 i = 0; i < team->battleMemberCount; ++i)
    {
        u8 bit = (u8)(1u << i);
        const vm_mock_service_team_battle_round_action *action = NULL;

        if ((team->battleMemberLeftMask & bit) != 0 ||
            team->battleMemberHp[i] == 0)
        {
            continue;
        }
        action = &team->battleRoundActions[i];
        if (action->valid && action->serial >= bestSerial)
        {
            best = (int)i;
            bestSerial = action->serial;
        }
        else if (best < 0)
        {
            best = (int)i;
        }
    }
    return best;
}

static void vm_mock_service_team_battle_queue_orphan_action(
    vm_mock_service_team *team,
    const u8 *response,
    u32 responseLen,
    u32 sourceClientId,
    u8 extraDeliveredMask)
{
    vm_mock_service_team_battle_event *event = NULL;
    u16 actionObjectLen = 0;
    u32 nextSerial = 0;
    u32 slot = 0;
    u8 actionObject[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX];

    if (team == NULL || response == NULL || responseLen == 0)
        return;
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
    memset(event, 0, sizeof(*event));
    memcpy(event->objectData, actionObject, actionObjectLen);
    event->valid = true;
    event->terminalVictory = team->battleEnemyHpCurrent == 0;
    event->serial = nextSerial;
    event->sourceClientId = sourceClientId;
    /* Escaped/dead observers already left the shared action wait; mark them
     * delivered so the ring entry can retire once survivors poll it. */
    event->deliveredMask = (u8)(vm_mock_service_team_battle_absent_mask(team) |
                                extraDeliveredMask);
    event->objectLen = actionObjectLen;
    printf("[info][mock-service] team_battle_action_queue_orphan battle=%u "
           "action=%u source=%08x enemyhp=%u/%u terminal=%u object=%u "
           "delivered=%02x\n",
           team->battleSerial,
           event->serial,
           sourceClientId,
           team->battleEnemyHpCurrent,
           team->battleEnemyHpMax,
           event->terminalVictory ? 1 : 0,
           event->objectLen,
           event->deliveredMask);
}

static u32 vm_net_mock_build_team_battle_orphan_round_release(
    u8 *out,
    u32 outCap,
    vm_mock_service_team *team,
    u8 counterMemberIndex)
{
    u8 combinedActionInfo[
        VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX *
        VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u8 teamInfo[64];
    u8 merged[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX * 2];
    u32 combinedActionInfoLen = 0;
    u32 totalActionCount = 0;
    u32 lastSerial = 0;
    u8 pendingCount = 0;
    u32 teamInfoLen = 0;
    u32 pos = 5;
    bool playerOnRight = false;
    u8 battleSide = 0;
    u8 defaultPlayerSlot = 0;
    u8 defaultEnemySlot = 0;
    u8 playerSlot = 0;
    u8 enemySlot = 0;
    u8 counterActionType = 0;
    u8 counterChildFlag = 0;
    u8 deathActionType = 0;
    u32 counterValueB = 0;
    u32 type1EffectIndex = 0;
    u8 type1Tail0 = 0;
    u8 type1Tail1 = 0;
    u8 type1Tail2 = 0;
    u8 counterWireSlots[3];
    u8 counterWireCount = 0;
    vm_mock_service_client_session *counterSession = NULL;

    if (out == NULL || outCap < pos || team == NULL ||
        counterMemberIndex >= team->battleMemberCount)
    {
        return 0;
    }

    counterSession = vm_mock_service_find_client_session(
        team->battleMemberClientIds[counterMemberIndex]);
    if (counterSession == NULL)
        return 0;

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
        lastSerial = next->serial;
        ++pendingCount;
    }

    g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleStartUsesSceneWireMaps = 1;
    g_mockBattleEnemyCountCurrent = team->battleMonsterCount;
    g_mockBattleOperateTurnCounter = team->battleTurnCounter;
    g_vm_net_mock_battle_enemy_id_current = team->battleEnemyId;
    memcpy(g_mockBattleEnemyHpSlots, team->battleEnemyHpSlots,
           sizeof(g_mockBattleEnemyHpSlots));
    memcpy(g_mockBattleEnemyHpMaxSlots, team->battleEnemyHpMaxSlots,
           sizeof(g_mockBattleEnemyHpMaxSlots));
    g_mockBattleEnemyHpCurrent = team->battleEnemyHpCurrent;
    g_mockBattleEnemyHpMax = team->battleEnemyHpMax;
    g_mockBattleRoleHpCurrent = team->battleMemberHp[counterMemberIndex];
    g_mockBattleRoleHpMax = team->battleMemberHpMax[counterMemberIndex];
    g_mockBattleRoleMpCurrent = team->battleMemberMp[counterMemberIndex];
    g_mockBattleRoleMpMax = team->battleMemberMpMax[counterMemberIndex];
    g_vm_net_mock_battle_role_id_current = counterSession->onlineRoleId;
    g_vm_net_mock_team_battle_party_count_current = team->battleMemberCount;
    g_vm_net_mock_team_battle_actor_slot_current = counterMemberIndex;
    g_vm_net_mock_team_battle_member_count_current = team->battleMemberCount;

    playerOnRight = vm_net_mock_battle_player_on_right();
    battleSide = team->battleSide ? team->battleSide :
                 (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                         vm_net_mock_battle_default_side(playerOnRight));
    vm_net_mock_battle_default_wire_slots(playerOnRight, battleSide,
                                          &defaultPlayerSlot, &defaultEnemySlot);
    playerSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_PLAYER_WIRE_SLOT", defaultPlayerSlot);
    enemySlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_ENEMY_WIRE_SLOT", defaultEnemySlot);
    counterActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTION_TYPE",
                                                vm_net_mock_env_u32("CBE_BATTLE_ACTION_TYPE", 0));
    counterChildFlag = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_CHILD_FLAG", 0);
    deathActionType = (u8)vm_net_mock_env_u32("CBE_BATTLE_DEATH_ACTION_TYPE", 3);
    counterValueB = vm_net_mock_env_u32("CBE_BATTLE_COUNTER_VALUE_B", 0);
    type1EffectIndex = vm_net_mock_env_u32("CBE_BATTLE_TYPE1_EFFECT_INDEX", 0);
    type1Tail0 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL0", 0);
    type1Tail1 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL1", 0);
    type1Tail2 = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_TAIL2", 0);

    if (!team->battleRoundTerminalPending &&
        g_mockBattleEnemyHpCurrent > 0 &&
        g_mockBattleRoleHpCurrent > 0)
    {
        counterWireCount = vm_net_mock_battle_collect_live_enemy_wires(
            playerOnRight, battleSide, enemySlot, counterWireSlots);
        for (u8 i = 0; i < counterWireCount && i < 3 &&
                       g_mockBattleRoleHpCurrent > 0; ++i)
        {
            u8 counterActorWireSlot = (u8)vm_net_mock_env_u32(
                "CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT", counterWireSlots[i]);
            u8 counterTargetWireSlot = (u8)vm_net_mock_env_u32(
                "CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT", playerSlot);
            u8 strikeActionType = counterActionType;
            u32 strikeEffectIndex = type1EffectIndex;
            bool strikeIsHeal = false;
            u32 strikeHealAmount = 0;
            u32 oneCounterDamage = vm_net_mock_battle_apply_enemy_counter_strike(
                g_vm_net_mock_battle_enemy_id_current, i, counterActionType,
                type1EffectIndex, &strikeActionType, &strikeEffectIndex, &strikeIsHeal, &strikeHealAmount);

            if (strikeActionType == 1)
            {
                counterActorWireSlot = (u8)vm_net_mock_env_u32(
                    "CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                    counterActorWireSlot);
                counterTargetWireSlot = (u8)vm_net_mock_env_u32(
                    "CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                    counterTargetWireSlot);
            }
            if (strikeIsHeal)
                counterTargetWireSlot = counterActorWireSlot;
            /* Miss keeps valueA=0 + child_flag=3 (闪躲); still append the counter. */
            if (totalActionCount >= 0xff ||
                !vm_net_mock_append_battle_actioninfo_record(
                    combinedActionInfo, sizeof(combinedActionInfo),
                    &combinedActionInfoLen, strikeActionType,
                    counterActorWireSlot, counterTargetWireSlot,
                    vm_net_mock_battle_child_flag_with_env(
                        "CBE_BATTLE_COUNTER_CHILD_FLAG",
                        strikeIsHeal ? VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL
                                     : vm_net_mock_battle_take_outcome_child_flag()),
                    strikeIsHeal ? strikeHealAmount
                                 : vm_net_mock_battle_negative_delta_u32(oneCounterDamage),
                    counterValueB,
                    (strikeActionType == 1 || strikeActionType == 2) ?
                        strikeEffectIndex : 0,
                    (strikeActionType == 1 || strikeActionType == 2) ?
                        type1Tail0 : 0,
                    (strikeActionType == 1 || strikeActionType == 2) ?
                        type1Tail1 : 0,
                    (strikeActionType == 1 || strikeActionType == 2) ?
                        type1Tail2 : 0))
            {
                return 0;
            }
            ++totalActionCount;
        }
        if (g_mockBattleRoleHpCurrent == 0)
        {
            if (totalActionCount >= 0xff ||
                !vm_net_mock_append_battle_actioninfo_record(
                    combinedActionInfo, sizeof(combinedActionInfo),
                    &combinedActionInfoLen, deathActionType,
                    playerSlot, 0, 0, 0, 0, 0, 0, 0, 0))
            {
                return 0;
            }
            ++totalActionCount;
        }
    }

    if (pendingCount == 0 && totalActionCount == 0)
        return 0;

    team->battleMemberHp[counterMemberIndex] = g_mockBattleRoleHpCurrent;
    team->battleMemberMp[counterMemberIndex] = g_mockBattleRoleMpCurrent;
    memcpy(team->battleEnemyHpSlots, g_mockBattleEnemyHpSlots,
           sizeof(team->battleEnemyHpSlots));
    team->battleEnemyHpCurrent = g_mockBattleEnemyHpCurrent;
    team->battleTurnCounter = g_mockBattleOperateTurnCounter;

    memset(teamInfo, 0, sizeof(teamInfo));
    if (!vm_net_mock_build_team_battle_party_teaminfo_blob(
            teamInfo, sizeof(teamInfo), &teamInfoLen,
            counterSession, team, counterMemberIndex, true))
    {
        return 0;
    }
    memset(merged, 0, sizeof(merged));
    if (!vm_net_mock_append_battle_action6_object_teaminfo_blob(
            merged, sizeof(merged), &pos,
            combinedActionInfo, combinedActionInfoLen,
            (u8)totalActionCount, teamInfo, teamInfoLen) ||
        pos > outCap)
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(merged, pos, 1);
    memcpy(out, merged, pos);
    printf("[info][mock-service] team_battle_round_orphan_release battle=%u "
           "round=%u actor=%u pending=%u actions=%u info=%u teaminfo=%u "
           "enemyhp=%u rolehp=%u resp=%u\n",
           team->battleSerial,
           team->battleRoundSerial,
           counterMemberIndex,
           pendingCount,
           (u8)totalActionCount,
           combinedActionInfoLen,
           teamInfoLen,
           team->battleEnemyHpCurrent,
           team->battleMemberHp[counterMemberIndex],
           pos);
    return pos;
}

static void vm_mock_service_team_battle_publish_member_vitals(
    vm_mock_service_team *team)
{
    if (team == NULL)
        return;
    for (u8 member = 0; member < team->battleMemberCount; ++member)
    {
        vm_mock_service_client_session *memberSession =
            vm_mock_service_find_client_session(team->battleMemberClientIds[member]);
        bool vitalsChanged = false;
        u8 memberBit = (u8)(1u << member);

        if (memberSession == NULL)
            continue;
        /*
         * Fled/revival-exited seats keep durable/online HP from their exit
         * path.  Copying battleMemberHp (forced 0) would wipe a successful
         * revival and make the next encounter reject-dead.
         */
        if ((team->battleMemberLeftMask & memberBit) != 0)
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
            vm_mock_service_team_enqueue_hsp_for_members(memberSession);
    }
}

static void vm_mock_service_team_battle_flush_round_if_ready(
    vm_mock_service_team *team,
    u8 extraDeliveredMask,
    const char *reason)
{
    u8 aliveMask = 0;
    int flushMember = -1;
    u8 response[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX * 2];
    u32 responseLen = 0;
    vm_mock_service_client_session *source = NULL;

    if (team == NULL || !team->battleActive)
        return;
    aliveMask = vm_mock_service_team_battle_alive_mask(team);
    if (aliveMask == 0)
    {
        team->battleFinished = true;
        team->battleRoundActedMask = 0;
        vm_mock_service_team_battle_clear_round_actions(team);
        printf("[info][mock-service] team_battle_round_abort_no_alive battle=%u "
               "round=%u left=%02x reason=%s\n",
               team->battleSerial,
               team->battleRoundSerial,
               team->battleMemberLeftMask,
               reason ? reason : "none");
        return;
    }
    if ((u8)(team->battleRoundActedMask & aliveMask) != aliveMask)
        return;

    flushMember = vm_mock_service_team_battle_pick_flush_member(team);
    if (flushMember < 0)
        return;
    source = vm_mock_service_find_client_session(
        team->battleMemberClientIds[flushMember]);
    responseLen = vm_net_mock_build_team_battle_orphan_round_release(
        response, sizeof(response), team, (u8)flushMember);
    g_vm_net_mock_team_battle_party_count_current = 0;
    g_vm_net_mock_team_battle_actor_slot_current = 0;
    g_vm_net_mock_team_battle_member_count_current = 0;
    if (responseLen == 0)
    {
        printf("[error][mock-service] team_battle_round_orphan_release_failed "
               "battle=%u round=%u actor=%u acted=%02x alive=%02x reason=%s\n",
               team->battleSerial,
               team->battleRoundSerial,
               flushMember,
               team->battleRoundActedMask,
               aliveMask,
               reason ? reason : "none");
        return;
    }
    if (team->battleEnemyHpCurrent == 0)
        team->battleFinished = true;
    vm_mock_service_team_battle_queue_orphan_action(
        team, response, responseLen,
        source ? source->clientId : team->battleMemberClientIds[flushMember],
        extraDeliveredMask);
    for (u8 member = 0; member < team->battleMemberCount && member < 3; ++member)
    {
        if ((aliveMask & (u8)(1u << member)) == 0)
            continue;
        vm_net_mock_battle_modifier_advance_round(
            &team->battleMemberModifiers[member]);
    }
    team->battleRoundActedMask = 0;
    ++team->battleRoundSerial;
    if (team->battleRoundSerial == 0)
        team->battleRoundSerial = 1;
    vm_mock_service_team_battle_clear_round_actions(team);
    vm_mock_service_team_battle_publish_member_vitals(team);
    printf("[info][mock-service] team_battle_round_flush battle=%u round=%u "
           "actor=%u acted=%02x alive=%02x left=%02x finished=%u reason=%s "
           "resp=%u\n",
           team->battleSerial,
           team->battleRoundSerial,
           flushMember,
           team->battleRoundActedMask,
           aliveMask,
           team->battleMemberLeftMask,
           team->battleFinished ? 1 : 0,
           reason ? reason : "none",
           responseLen);
}

static void vm_mock_service_team_battle_note_member_exit(
    vm_mock_service_client_session *session,
    bool fled,
    bool syncVitalsFromGlobals,
    u8 extraDeliveredMask,
    const char *reason)
{
    vm_mock_service_team *team = NULL;
    int memberIndex = -1;
    u8 memberBit = 0;

    if (session == NULL)
        return;
    team = vm_mock_service_team_find_for_client(session->clientId);
    memberIndex = vm_mock_service_team_battle_member_index(
        team, session->clientId);
    if (team == NULL || !team->battleActive ||
        memberIndex < 0 || memberIndex >= team->battleMemberCount)
    {
        return;
    }

    memberBit = (u8)(1u << memberIndex);
    if (syncVitalsFromGlobals)
    {
        team->battleMemberHp[memberIndex] = g_mockBattleRoleHpCurrent;
        team->battleMemberHpMax[memberIndex] = g_mockBattleRoleHpMax;
        team->battleMemberMp[memberIndex] = g_mockBattleRoleMpCurrent;
        team->battleMemberMpMax[memberIndex] = g_mockBattleRoleMpMax;
        memcpy(team->battleEnemyHpSlots, g_mockBattleEnemyHpSlots,
               sizeof(team->battleEnemyHpSlots));
        memcpy(team->battleEnemyHpMaxSlots, g_mockBattleEnemyHpMaxSlots,
               sizeof(team->battleEnemyHpMaxSlots));
        team->battleEnemyHpCurrent = g_mockBattleEnemyHpCurrent;
        team->battleEnemyHpMax = g_mockBattleEnemyHpMax;
        team->battleTurnCounter = g_mockBattleOperateTurnCounter;
    }
    if (fled)
        team->battleMemberLeftMask = (u8)(team->battleMemberLeftMask | memberBit);
    /* A fled/dead member must not keep the barrier waiting on its bit. */
    team->battleRoundActedMask = (u8)(team->battleRoundActedMask | memberBit);
    vm_mock_service_team_battle_publish_member_vitals(team);
    printf("[info][mock-service] team_battle_member_exit battle=%u actor=%u "
           "fled=%u hp=%u left=%02x acted=%02x alive=%02x reason=%s\n",
           team->battleSerial,
           memberIndex,
           fled ? 1 : 0,
           team->battleMemberHp[memberIndex],
           team->battleMemberLeftMask,
           team->battleRoundActedMask,
           vm_mock_service_team_battle_alive_mask(team),
           reason ? reason : "exit");
    vm_mock_service_team_battle_flush_round_if_ready(
        team, (u8)(extraDeliveredMask | memberBit), reason);
}

/*
 * Revival-stone exit: the durable role is full HP again, but this shared fight
 * must keep the seat out of alive_mask and must not inject later 4/6 into the
 * client that just consumed 4/8 and left Battle.cbm.
 */
static void vm_mock_service_team_battle_note_revival_exit(
    vm_mock_service_client_session *session)
{
    vm_mock_service_team *team = NULL;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    int memberIndex = -1;
    u8 memberBit = 0;

    if (session == NULL)
        return;
    team = vm_mock_service_team_find_for_client(session->clientId);
    memberIndex = vm_mock_service_team_battle_member_index(
        team, session->clientId);
    if (team == NULL || !team->battleActive ||
        memberIndex < 0 || memberIndex >= team->battleMemberCount)
    {
        return;
    }

    memberBit = (u8)(1u << memberIndex);
    team->battleMemberLeftMask = (u8)(team->battleMemberLeftMask | memberBit);
    team->battleMemberHp[memberIndex] = 0;
    team->battleRoundActedMask = (u8)(team->battleRoundActedMask | memberBit);
    vm_mock_service_team_battle_mark_member_events_delivered(team, memberBit);
    if (role != NULL)
    {
        session->onlineHp = role->hp;
        session->onlineHpMax = role->hpMax ? role->hpMax : role->hp;
        session->onlineMp = role->mp;
        session->onlineMpMax = role->mpMax ? role->mpMax : role->mp;
        if (session->roleOnline)
            vm_mock_service_team_enqueue_hsp_for_members(session);
        /*
         * Flush may load another seat into process battle globals.  Restore
         * this revived role before any later publish/save can persist 0 HP.
         */
        g_mockBattleRoleHpCurrent = role->hp;
        g_mockBattleRoleHpMax = role->hpMax ? role->hpMax : role->hp;
        g_mockBattleRoleMpCurrent = role->mp;
        g_mockBattleRoleMpMax = role->mpMax ? role->mpMax : role->mp;
    }
    printf("[info][mock-service] team_battle_member_exit battle=%u actor=%u "
           "fled=0 revival=1 hp_battle=0 hp_online=%u left=%02x acted=%02x "
           "alive=%02x reason=revival-stone\n",
           team->battleSerial,
           memberIndex,
           session->onlineHp,
           team->battleMemberLeftMask,
           team->battleRoundActedMask,
           vm_mock_service_team_battle_alive_mask(team));
    vm_mock_service_team_battle_flush_round_if_ready(
        team, memberBit, "revival-stone");
    if (role != NULL)
    {
        g_mockBattleRoleHpCurrent = role->hp;
        g_mockBattleRoleHpMax = role->hpMax ? role->hpMax : role->hp;
        g_mockBattleRoleMpCurrent = role->mp;
        g_mockBattleRoleMpMax = role->mpMax ? role->mpMax : role->mp;
    }
}

/*
 * Battle death → mmShop → return must keep 801 for the later 1/7/14 confirm.
 * Map-side HP=0 (no Battle.cbm for this role) may consume on buy/return instead.
 *
 * g_mockBattleOperateSessionArmed is cleared when the killing 4/6 ends, but
 * Battle.cbm still shows the death prompt.  The per-session flag is the durable
 * "still owes 1/7/14" marker across shop buy/return.
 */
static void vm_mock_service_session_clear_battle_revival_confirm(
    vm_mock_service_client_session *session)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    bool hadSession = session != NULL && session->awaitsBattleRevivalConfirm;
    bool hadRole = role != NULL && role->roleId != 0 &&
                   g_vm_net_mock_battle_role_id_current == role->roleId &&
                   g_mockBattleAwaitsRevivalConfirm != 0;

    if (session != NULL)
        session->awaitsBattleRevivalConfirm = false;
    g_mockBattleAwaitsRevivalConfirm = 0;
    if (!hadSession && !hadRole)
        return;
    printf("[info][mock-service] battle_revival_confirm_cleared client=%08x role=%u\n",
           session ? session->clientId : 0,
           session ? session->onlineRoleId : (role ? role->roleId : 0));
}

static void vm_mock_service_session_arm_battle_revival_confirm_for_death(
    const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    /*
     * Prefer durable role HP after death save. Process battle globals can be
     * swapped by another account before the shop buy of 801 arrives.
     */
    if (role == NULL || role->hp != 0)
        return;
    g_mockBattleAwaitsRevivalConfirm = 1;
    if (session != NULL)
        session->awaitsBattleRevivalConfirm = true;
    printf("[info][mock-service] battle_revival_confirm_armed client=%08x "
           "role=%u reason=%s\n",
           session ? session->clientId : 0,
           role->roleId,
           reason ? reason : "-");
}

static bool vm_mock_service_session_awaits_battle_revival_confirm(
    vm_mock_service_client_session *session)
{
    vm_mock_service_team *team = NULL;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    int memberIndex = -1;
    u8 memberBit = 0;

    if (session != NULL && session->awaitsBattleRevivalConfirm)
        return true;
    if (g_mockBattleAwaitsRevivalConfirm != 0 &&
        role != NULL && role->hp == 0 &&
        (session == NULL || session->onlineRoleId == 0 ||
         session->onlineRoleId == role->roleId))
    {
        return true;
    }
    if (session == NULL)
        return false;
    if (g_mockBattleOperateSessionArmed != 0 &&
        session->onlineRoleId != 0 &&
        g_vm_net_mock_battle_role_id_current == session->onlineRoleId)
    {
        return true;
    }
    /*
     * After the killing packet disarms operate, the bound battle role id plus
     * durable/online HP=0 still means Battle.cbm owes 1/7/14 for this seat.
     */
    if (role != NULL && role->hp == 0 &&
        session->onlineRoleId != 0 &&
        session->onlineHp == 0 &&
        g_vm_net_mock_battle_role_id_current == session->onlineRoleId)
    {
        return true;
    }
    team = vm_mock_service_team_find_for_client(session->clientId);
    memberIndex = vm_mock_service_team_battle_member_index(team, session->clientId);
    if (team == NULL || !team->battleActive ||
        memberIndex < 0 || memberIndex >= team->battleMemberCount)
    {
        return false;
    }
    memberBit = (u8)(1u << memberIndex);
    if ((team->battleMemberLeftMask & memberBit) != 0)
        return false;
    if (team->battleFinished)
        return false;
    /*
     * Only the dead seat still owed 1/7/14 must keep a purchased 801.
     * Living teammates are not awaiting revival confirm.
     */
    return team->battleMemberHp[memberIndex] == 0;
}

static void vm_net_mock_battle_restore_role_vitals_to_globals(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 hp = 0;
    u32 hpMax = 0;
    u32 mp = 0;
    u32 mpMax = 0;

    if (role == NULL)
        return;
    vm_net_mock_role_default_vitals(role, &hp, &hpMax, &mp, &mpMax);
    g_mockBattleRoleHpCurrent = hp;
    g_mockBattleRoleHpMax = hpMax;
    g_mockBattleRoleMpCurrent = mp;
    g_mockBattleRoleMpMax = mpMax;
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
        {
            vm_mock_service_client_session *active =
                vm_mock_service_get_active_client_session();
            if (active != NULL &&
                vm_mock_service_duel_find_for_client(active->clientId, NULL) != NULL)
            {
                /* Spar item use is unresolved; PvE 4/3 must not desync duel HP. */
                vm_net_mock_finish_wt_packet(out, 5, 0);
                printf("[info][mock-service] duel_item_use_defer client=%08x "
                       "action=empty-ack reason=spar-item-unresolved "
                       "evidence=docs/re/2026-07-11-nearby-player-actions.md\n",
                       active->clientId);
                return 5;
            }
        }
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
        /*
         * prepare_operation may have loaded battleMemberHp=0 for a seat that
         * already revived-out.  Restore durable vitals before the dispatch
         * wrapper publishes role HP, or the next encounter permanently
         * reject-deads the account.
         */
        vm_net_mock_battle_restore_role_vitals_to_globals();
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
        vm_net_mock_battle_restore_role_vitals_to_globals();
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
    else if (buildType == VM_MOCK_TEAM_BATTLE_BUILD_OPERATE_FALLBACK ||
             buildType == VM_MOCK_TEAM_BATTLE_BUILD_OPERATE)
    {
        /*
         * Prefer auto cancel window only gates poll synth (see in_turn_gap in
         * pending solo auto).  Never answer a real client 4/2 with 4/11: the
         * client is waiting for 4/6 actioninfo, and AOE/group skills stall with
         * no actor motion when that contract is broken (hangup continuous 4/2
         * after arm_pending was the common trigger).
         */
        if (buildType == VM_MOCK_TEAM_BATTLE_BUILD_OPERATE_FALLBACK)
            responseLen = vm_net_mock_build_battle_operate_response_fallback(
                request, requestLen, out, outCap);
        else
            responseLen = vm_net_mock_build_battle_operate_response(
                request, requestLen, out, outCap);
    }
    else
        responseLen = vm_net_mock_build_battle_operate_response(request, requestLen, out, outCap);

    /*
     * Real client 4/2 after a resolved turn: start the cancel window so the
     * next accepted operate / poll synth waits ~3s.
     */
    if (responseLen != 0 &&
        (buildType == VM_MOCK_TEAM_BATTLE_BUILD_OPERATE ||
         buildType == VM_MOCK_TEAM_BATTLE_BUILD_OPERATE_FALLBACK) &&
        g_mockBattleAutoPrefer != 0 &&
        g_mockBattleAutoSynthInProgress == 0 &&
        !context.active)
    {
        vm_net_mock_battle_auto_note_client_operate();
    }

    if (context.active)
    {
        if (responseLen != 0)
        {
            actionAccepted = vm_net_mock_get_object_u8_field(
                                 out, responseLen, "actionnum", &actionCount) &&
                             actionCount != 0;
            releaseRound = actionAccepted && context.resolvesRound;
            if (actionAccepted && !releaseRound &&
                vm_net_mock_battle_all_enemies_defeated())
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
        if (result->mpRestored != 0)
            g_vm_net_mock_battle_mp_increase_allowed = 1;
        g_mockBattleRoleHpMax = roleHpMax;
        g_mockBattleRoleHpCurrent = roleHp;
        g_mockBattleRoleMpMax = roleMpMax;
        g_mockBattleRoleMpCurrent = roleMp;
        role->hp = roleHp;
        role->mp = roleMp;
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

static bool vm_net_mock_append_battle_status7_object(u8 *out, u32 outCap, u32 *pos,
                                                     u32 autoRecoverHp, u32 autoRecoverMp,
                                                     bool forceTeamVictory,
                                                     u8 *objectCount,
                                                     bool seedMapBaseline)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHp = g_mockBattleRoleHpMax != 0 ? g_mockBattleRoleHpCurrent :
                 (role ? role->hp : VM_NET_MOCK_ROLE_DEFAULT_HP);
    /* Mirror HP: settlement must seed from battle current MP, not durable
     * role->mp.  Durable MP can still hold the pre-battle full value while the
     * live bar is post-cost; seeding from role->mp makes flask see missingMp=0
     * and 4/7 recover_mp stay 0. */
    u32 roleMp = g_mockBattleRoleMpMax != 0 ? g_mockBattleRoleMpCurrent :
                 (role ? role->mp : VM_NET_MOCK_ROLE_DEFAULT_MP);
    u32 statusExp = 0;
    u32 totalExp = role ? role->exp : 0;
    u32 statusCurExp = vm_net_mock_role_next_level_start_exp(totalExp);
    u32 statusLastExp = vm_net_mock_role_last_level_exp(totalExp);
    u32 statusPercentExp = vm_net_mock_role_exp_percent(totalExp);
    u32 statusGold = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 statusLevel = role ? role->level : 1;
    u32 recoverHp = vm_net_mock_add_capped_u32(
        vm_net_mock_env_u32_if_set("CBE_BATTLE_RECOVER_HP", 0), autoRecoverHp);
    u32 recoverMp = vm_net_mock_add_capped_u32(
        vm_net_mock_battle_recover_mp_value(), autoRecoverMp);
    u32 dropItemId = 0;
    u16 dropSeq = 0;
    u32 dropCount = 0;
    bool dropGranted = false;
    char dropInfo[VM_NET_MOCK_SHOP_NAME_BYTES + 16];
    bool haveDropInfo = false;
    u32 applyRewardExp = 0;
    u32 displayExpGain = 0;
    u32 displayGoldGain = 0;
    u32 wireExp = 0;
    u32 wireGold = 0;
    u32 preExp = role ? role->exp : 0;
    u32 preGold = role ? role->money : 0;
    bool victory = vm_net_mock_battle_all_enemies_defeated() &&
                   (forceTeamVictory || roleHp > 0);
    bool rewardAlreadyGranted = false;
    bool mpRecoveryApplied = false;
    bool seededBaseline = false;

    if (objectCount == NULL || *objectCount == 0xff)
        return false;

    if (victory)
    {
        rewardAlreadyGranted = (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial);
        applyRewardExp = vm_net_mock_battle_grant_reward_once(&dropItemId,
                                                              &dropSeq,
                                                              &dropCount,
                                                              &dropGranted);
        displayExpGain = (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial)
                             ? g_vm_net_mock_battle_rewarded_exp
                             : applyRewardExp;
        if (g_vm_net_mock_battle_reward_rate_suppressed_serial !=
            g_mockBattleOperateSessionSerial)
        {
            displayGoldGain = vm_net_mock_mul_capped_u32(
                vm_net_mock_env_u32_if_set(
                    "CBE_BATTLE_REWARD_GOLD",
                    vm_net_mock_battle_reward_gold_for_enemy(
                        g_vm_net_mock_battle_enemy_id_current)),
                vm_net_mock_battle_enemy_count_current());
        }
        if (rewardAlreadyGranted && role != NULL)
        {
            preExp = (role->exp >= displayExpGain) ? (role->exp - displayExpGain)
                                                   : role->exp;
            preGold = (role->money >= displayGoldGain)
                          ? (role->money - displayGoldGain)
                          : role->money;
        }
    }
    if (role != NULL)
    {
        u32 rewardGold = (victory && !rewardAlreadyGranted) ? displayGoldGain : 0;
        if (!rewardAlreadyGranted)
        {
            preExp = role->exp;
            preGold = role->money;
        }
        roleMp = vm_net_mock_battle_apply_mp_recovery_once(role, roleMp, recoverMp,
                                                           &mpRecoveryApplied);
        vm_net_mock_role_apply_battle_settlement(roleHp, roleMp, applyRewardExp, rewardGold,
                                                 &statusLastExp, &statusCurExp,
                                                 &statusPercentExp, &statusLevel,
                                                 &statusGold, &roleHp, &roleMp);
        statusExp = role->exp;
    }
    else
    {
        statusExp = totalExp + applyRewardExp;
        statusGold = preGold + displayGoldGain;
    }
    statusLastExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_LAST_EXP", statusLastExp);
    statusCurExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_CUR_EXP", statusCurExp);
    statusPercentExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_PERCENT_EXP",
                                                  statusPercentExp);
    statusLevel = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_LEVEL", statusLevel);
    /*
     * 4/7.exp/gold must stay post-reward ABSOLUTE totals.  A prior experiment
     * that wired this-battle gains made HandleBattleSettleMsg overwrite actor
     * money/EXP with the tiny per-kill values, so players saw copper collapse
     * after battle and blamed discard/sell.  Panel 获得 = new-old still needs a
     * correct pre-reward baseline (seeded below when possible).
     */
    wireExp = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_WIRE_EXP", statusExp);
    wireGold = vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_WIRE_GOLD", statusGold);
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
    /*
     * Hangup / prefer: put [挂机中] on 4/7.fdata so the settle banner (and the
     * brief 4/8 UpdateCharAttrs refresh, when cache still holds fdata) is not
     * an empty shell.  Drop name still wins when a drop line is present.
     *
     * Non-hangup: same empty-shell class — user 2026-07-30 saw a click-to-
     * dismiss empty prompt after 4/8 when fdata_len=0 (25/12 clear failed).
     * Seed a short victory line so the refresh is not blank.
     * GBK: [挂机中] / 战斗胜利
     */
    if (!haveDropInfo && victory)
    {
        if (g_mockHangupLoopActive != 0 || g_mockBattleAutoPrefer != 0)
        {
            static const char hangupFdataGbk[] =
                "\x5b\xb9\xd2\xbb\xfa\xd6\xd0\x5d"; /* GBK: [挂机中] */
            memcpy(dropInfo, hangupFdataGbk, sizeof(hangupFdataGbk));
            haveDropInfo = true;
        }
        else
        {
            static const char victoryFdataGbk[] =
                "\xd5\xbd\xb6\xb7\xca\xa4\xc0\xfb"; /* GBK: 战斗胜利 */
            memcpy(dropInfo, victoryFdataGbk, sizeof(victoryFdataGbk));
            haveDropInfo = true;
        }
    }
    printf("[info][network] mock_battle_settle enemy=%u enemies=%u victory=%u team_victory=%u exp_gain=%u exp_total=%u gold_gain=%u gold_total=%u level=%u recover=%u/%u drop=%u seq=%u count=%u role=%u battle_role=%u fdata_len=%u\n",
           g_vm_net_mock_battle_enemy_id_current,
           vm_net_mock_battle_enemy_count_current(),
           victory ? 1 : 0,
           forceTeamVictory ? 1 : 0,
           displayExpGain,
           statusExp,
           displayGoldGain,
           statusGold,
           statusLevel,
           recoverHp,
           recoverMp,
           dropGranted ? dropItemId : 0,
           dropSeq,
           dropCount,
           role ? role->roleId : 0,
           g_vm_net_mock_battle_role_id_current,
           haveDropInfo ? (u32)strlen(dropInfo) : 0);
    vm_autotest_note("mock_battle_settle enemy=%u enemies=%u victory=%u team_victory=%u exp_gain=%u exp_total=%u gold_gain=%u gold_total=%u level=%u hp=%u mp=%u recover=%u/%u recovered=%u drop=%u seq=%u count=%u role=%u battle_role=%u fdata_len=%u\n",
                     g_vm_net_mock_battle_enemy_id_current,
                     vm_net_mock_battle_enemy_count_current(),
                     victory ? 1 : 0,
                     forceTeamVictory ? 1 : 0,
                     displayExpGain,
                     statusExp,
                     displayGoldGain,
                     statusGold,
                     statusLevel,
                     roleHp,
                     roleMp,
                     recoverHp,
                     recoverMp,
                     mpRecoveryApplied ? 1 : 0,
                     dropGranted ? dropItemId : 0,
                     dropSeq,
                     dropCount,
                     role ? role->roleId : 0,
           g_vm_net_mock_battle_role_id_current,
           haveDropInfo ? (u32)strlen(dropInfo) : 0);
    g_mockBattleSettleWireRecoverHp = recoverHp;
    g_mockBattleSettleWireRecoverMp = recoverMp;

    /*
     * Seed pre-reward absolute EXP/money so HandleBattleSettleMsg's
     * 获得 = (4/7 total) - old uses a non-zero baseline.  Without this, old=0
     * and the panel shows lifetime totals as the gain.
     * Exit-prime replay must NOT emit 1/1/14 inside Battle.cbm — same-packet
     * tear-down with that seed crashed (addr fault on scene_poll resp=623).
     */
    if (seedMapBaseline &&
        victory && role != NULL && (displayExpGain != 0 || displayGoldGain != 0))
    {
        u32 savedExp = role->exp;
        u32 savedMoney = role->money;

        role->exp = preExp;
        role->money = preGold;
        if (*objectCount < 0xff &&
            vm_net_mock_append_shop_actor_state14_object(out, outCap, pos, NULL))
        {
            ++*objectCount;
            seededBaseline = true;
        }
        role->exp = savedExp;
        role->money = savedMoney;
    }
    (void)seededBaseline;

    if (*objectCount == 0xff)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 7, &objectStart))
        return false;
    /*
     * Battle.cbm HandleBattleSettleMsg(0x743C) reads exp before lastexp,
     * curexp, and persentexp. 4/7.exp/gold are post-reward absolute totals.
     */
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "exp", wireExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", statusLastExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "curexp", statusCurExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", statusPercentExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "energy", 100))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "energymax", 100))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "gold", wireGold))
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
     * text field rendered by mmBattle at 0x7B08/0x4462, so use it for the
     * visible drop line while the durable item is already persisted in the
     * role backpack.
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
    ++*objectCount;
    if (victory)
    {
        /*
         * Do NOT arm map 1/1/14 after ordinary victory.  That object is the
         * mmShop:0x9DE actor-state shape; delivering it on mmGame after battle
         * then opening the mall flash-crashes (A/B 2026-07-28: skip → shop
         * opens; with scene-poll 1/1/14 → resp=1156 crash).  Map HUD/MP after
         * tear-down is covered by battle 4/7 + later group-type1 / shop-return
         * 5/10.  Revival / fresh-shell paths still arm vitals elsewhere.
         * Opt-in only: CBE_MAP_VITALS_AFTER_BATTLE=1.
         */
        if (vm_net_mock_env_u32("CBE_MAP_VITALS_AFTER_BATTLE", 0) != 0)
        {
            vm_mock_service_session_arm_map_actor_vitals_sync(
                vm_mock_service_get_active_client_session(), 0, 0, false);
        }
    }
    return true;
}

static bool vm_net_mock_append_battle_terminal_status_objects(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
    bool forceTeamVictory)
{
    vm_net_mock_battle_auto_flask_result autoFlask;
    bool appendedCounts = false;

    if (out == NULL || pos == NULL || objectCount == NULL || *objectCount == 0xff)
        return false;
    vm_net_mock_battle_auto_use_vitality_flasks(&autoFlask);
    if (!vm_net_mock_append_battle_status7_object(out, outCap, pos,
                                                  autoFlask.hpRestored,
                                                  autoFlask.mpRestored,
                                                  forceTeamVictory,
                                                  objectCount,
                                                  true))
    {
        return false;
    }
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
    u8 itemInfo[8 + (VM_NET_MOCK_BATTLE_DROP_RESULT_MAX *
                     VM_NET_MOCK_ITEMINFO_ROW_WIRE_MAX)];
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
    {
        printf("[warn][network] mock_battle_drop_refresh_skip rows=%u phase=%s "
               "reason=iteminfo-build-failed evidence=keep-4/7-exit-intact\n",
               dropRowCount, phase ? phase : "-");
        return true;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 7, &objectStart))
    {
        printf("[warn][network] mock_battle_drop_refresh_skip rows=%u phase=%s "
               "reason=begin-7/7-failed evidence=keep-4/7-exit-intact\n",
               dropRowCount, phase ? phase : "-");
        return true;
    }
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

static bool vm_net_mock_append_battle_terminal_case4_object(u8 *out, u32 outCap, u32 *pos)
{
    return vm_net_mock_append_battle_escape4_object(out, outCap, pos, 1);
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
 * A resurrection-stone confirmation happens after the battle-side HP has
 * already reached zero.  Scene 30/1 only changes the map/position; it does
 * not replace the Battle.cbm character cache which still owns the displayed
 * HP bar.  Reuse the established battle-terminal object family, but keep the
 * status object isolated from victory rewards and automatic flask effects.
 *
 * HandleBattleSettleMsg(0x743C) treats hp/mp as pending changes.  Therefore
 * hpRecovery is the full current max HP for a dead player (0 + max -> max).
 * mpRecovery is the delta needed to reach durable post-revive MP (usually max).
 */
static bool vm_net_mock_append_battle_revival_status7_object(u8 *out, u32 outCap,
                                                             u32 *pos,
                                                             u32 hpRecovery,
                                                             u32 mpRecovery)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 totalExp = role ? role->exp : 0;
    u32 lastExp = vm_net_mock_role_last_level_exp(totalExp);
    u32 nextExp = vm_net_mock_role_next_level_start_exp(totalExp);
    u32 percentExp = vm_net_mock_role_exp_percent(totalExp);
    u32 gold = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 level = role ? role->level : 1;

    if (role == NULL || hpRecovery == 0)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 7, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "exp", totalExp) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", lastExp) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "curexp", nextExp) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", percentExp) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "energy", 100) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "energymax", 100) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "gold", gold) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "level", level) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "bagstatus", 0) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "hp", hpRecovery) ||
        !vm_net_mock_put_object_u32(out, outCap, pos, "mp", mpRecovery) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "itemnum", 0) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", NULL, 0) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 0))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_battle_revival_stone_completion_response(
    u8 *out,
    u32 outCap,
    u16 consumedStoneSeq,
    u32 remainingStoneCount,
    u32 mpRecovery)
{
    u32 pos = 5;
    u8 objectCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 hpRecovery = role ? role->hp : 0;
    u8 countInfo[16];
    u32 countInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos || role == NULL || hpRecovery == 0)
        return 0;
    if (!vm_net_mock_append_battle_revival_status7_object(out, outCap, &pos,
                                                           hpRecovery,
                                                           mpRecovery))
    {
        return 0;
    }
    objectCount += 1;
    /*
     * Client battle item rows sync through 7/11 (JianghuOL.CBE:0x1033544).
     * Without remaining=0 the local 801 stays visible after a successful
     * authoritative consume.  Also defer the same clear onto the post-battle
     * map actor sync in case Battle tears down before kind-7 runs.
     */
    if (consumedStoneSeq != 0)
    {
        if (!vm_net_mock_build_item_use_count_info_blob(countInfo, sizeof(countInfo),
                                                        consumedStoneSeq,
                                                        remainingStoneCount,
                                                        &countInfoLen) ||
            !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11,
                                         &objectStart) ||
            !vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo,
                                        (u16)countInfoLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    if (!vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos) ||
        !vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
        !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
    {
        return 0;
    }
    objectCount += 3;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_battle_revival_terminal hp_recovery=%u "
           "mp_recovery=%u stone_seq=%u stone_remaining=%u role=%u "
           "response=4/7%s+4/8+4/11+4/9 resp=%u "
           "evidence=mmBattle:0x743C+0x7DF6+0x2C50,JianghuOL.CBE:0x1033544\n",
           hpRecovery,
           mpRecovery,
           consumedStoneSeq,
           remainingStoneCount,
           role->roleId,
           consumedStoneSeq != 0 ? "+7/11" : "",
           pos);
    vm_autotest_note("mock_battle_revival_terminal hp_recovery=%u mp_recovery=%u "
                     "stone_seq=%u stone_remaining=%u role=%u "
                     "response=4/7%s+4/8+4/11+4/9 "
                     "evidence=mmBattle:0x743C/0x7DF6/0x2C50 JianghuOL.CBE:0x1033544\n",
                     hpRecovery,
                     mpRecovery,
                     consumedStoneSeq,
                     remainingStoneCount,
                     role->roleId,
                     consumedStoneSeq != 0 ? "+7/11" : "");
    return pos;
}

/* Apply a seat's timed buff without mutating the durable role row. */
static void vm_mock_service_duel_apply_seat_modifier(
    vm_mock_service_duel *duel,
    int seatIndex,
    vm_net_mock_player_stats *stats)
{
    vm_net_mock_battle_stat_modifier saved;

    if (duel == NULL || stats == NULL || seatIndex < 0 || seatIndex > 1)
        return;
    saved = g_vm_net_mock_battle_active_modifier_current;
    g_vm_net_mock_battle_active_modifier_current = duel->modifiers[seatIndex];
    vm_net_mock_battle_apply_active_stat_modifier(stats);
    g_vm_net_mock_battle_active_modifier_current = saved;
}

static u32 vm_mock_service_duel_skill_heal_amount(
    vm_mock_service_duel *duel,
    int sourceIndex,
    u32 operate)
{
    const vm_net_mock_skill_catalog_item *skill =
        vm_net_mock_battle_operate_skill(operate);
    vm_mock_service_client_session *source = NULL;
    vm_net_mock_role_state *sourceRole = NULL;
    vm_net_mock_player_stats sourceStats;
    uint64_t scaled = 0;
    uint64_t amount = 0;
    u32 hpCurrent = 0;
    u32 hpMax = 0;

    if (duel == NULL || sourceIndex < 0 || sourceIndex > 1 || skill == NULL ||
        skill->hpChange <= 0)
    {
        return 0;
    }
    hpCurrent = duel->hp[sourceIndex];
    hpMax = duel->hpMax[sourceIndex];
    if (hpCurrent >= hpMax || hpMax == 0)
        return 0;
    source = vm_mock_service_find_client_session(duel->clientIds[sourceIndex]);
    sourceRole = vm_mock_service_trade_role_for_session(source, NULL);
    memset(&sourceStats, 0, sizeof(sourceStats));
    vm_net_mock_role_build_player_stats(sourceRole, &sourceStats);
    vm_mock_service_duel_apply_seat_modifier(duel, sourceIndex, &sourceStats);
    scaled += (uint64_t)sourceStats.strength * skill->strengthCoeff;
    scaled += (uint64_t)sourceStats.agility * skill->agilityCoeff;
    scaled += (uint64_t)sourceStats.wisdom * skill->wisdomCoeff;
    scaled = (scaled + 50u) / 100u;
    amount = (uint64_t)(u32)skill->hpChange + scaled;
    if (amount > (uint64_t)(hpMax - hpCurrent))
        amount = hpMax - hpCurrent;
    return amount > 0xffffffffull ? 0xffffffffu : (u32)amount;
}

/*
 * Resolve one committed Operate into a duel hit.
 * Mirrors PvE skill.dsh branches: heal (td=1/2 +hp), self/party buff
 * (td=0/2 timed stats), enemy status-no-damage (td=0 timed no stats),
 * else damage.  Support skills must never fall back to ATK vs the foe.
 */
static bool vm_mock_service_duel_apply_operate(vm_mock_service_duel *duel,
                                              int sourceIndex,
                                              u32 operate,
                                              vm_mock_service_duel_hit *hitOut)
{
    vm_mock_service_client_session *source = NULL;
    vm_mock_service_client_session *target = NULL;
    vm_net_mock_role_state *sourceRole = NULL;
    vm_net_mock_role_state *targetRole = NULL;
    vm_net_mock_player_stats sourceStats;
    vm_net_mock_player_stats targetStats;
    const vm_net_mock_skill_catalog_item *skill = NULL;
    u32 mpAfter = 0;
    u8 targetIndex = 0;
    bool operateIsSkill = false;
    bool skillHeal = false;
    bool skillFriendlyMod = false;
    bool skillEnemyStatus = false;

    if (hitOut == NULL)
        return false;
    memset(hitOut, 0, sizeof(*hitOut));
    if (duel == NULL || sourceIndex < 0 || sourceIndex > 1)
        return false;

    targetIndex = (u8)(1u - (u8)sourceIndex);
    mpAfter = duel->mp[sourceIndex];
    operateIsSkill = operate > 2;
    /* Silenced seats may only normal-attack. */
    if (operateIsSkill && duel->silenceRounds[sourceIndex] != 0)
    {
        operate = 0;
        operateIsSkill = false;
    }
    if (operateIsSkill)
    {
        skill = vm_net_mock_battle_operate_skill(operate);
        if (skill == NULL || mpAfter < skill->mpCost)
            operate = 0;
        else
        {
            skillHeal = vm_net_mock_battle_operate_skill_targets_friendly_group_heal(operate);
            skillFriendlyMod =
                vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(operate);
            skillEnemyStatus =
                vm_net_mock_battle_operate_skill_targets_enemy_status_no_damage(operate);
            mpAfter -= skill->mpCost;
        }
    }

    hitOut->valid = true;
    hitOut->sourceIndex = (u8)sourceIndex;
    hitOut->operate = operate;
    hitOut->sourceMpAfter = mpAfter;

    if (skillHeal)
    {
        u32 healed = vm_mock_service_duel_skill_heal_amount(duel, sourceIndex, operate);
        bool canRevive = skill != NULL && skill->effectKind == 3;

        if (canRevive && duel->hp[targetIndex] == 0)
        {
            /* 尸鬼召唤 in duel: revive opponent seat is wrong; revive self. */
            healed = vm_mock_service_duel_skill_heal_amount(duel, sourceIndex, operate);
            if (healed == 0 && skill->hpChange > 0)
                healed = (u32)skill->hpChange;
            if (duel->hp[sourceIndex] == 0)
            {
                if (healed > duel->hpMax[sourceIndex])
                    healed = duel->hpMax[sourceIndex];
                hitOut->targetSelf = true;
                hitOut->supportNoDamage = true;
                hitOut->damage = healed;
                hitOut->targetHpAfter = healed;
                return true;
            }
        }
        hitOut->targetSelf = true;
        hitOut->supportNoDamage = true;
        hitOut->damage = healed;
        hitOut->targetHpAfter = duel->hp[sourceIndex] + healed;
        return true;
    }
    if (skillFriendlyMod)
    {
        hitOut->targetSelf = true;
        hitOut->supportNoDamage = true;
        hitOut->damage = 0;
        hitOut->targetHpAfter = duel->hp[sourceIndex];
        return true;
    }
    if (skillEnemyStatus)
    {
        if (vm_net_mock_battle_skill_is_silence(skill))
            duel->silenceRounds[targetIndex] = skill->durationRounds;
        hitOut->targetSelf = false;
        hitOut->supportNoDamage = true;
        hitOut->damage = 0;
        hitOut->targetHpAfter = duel->hp[targetIndex];
        return true;
    }

    /* Damage path: normal attack or offensive skill (negative hpChange). */
    {
        u32 rawDamage = 1;
        u32 mitigation = 0;
        bool magical = false;
        bool critical = false;

        source = vm_mock_service_find_client_session(duel->clientIds[sourceIndex]);
        target = vm_mock_service_find_client_session(duel->clientIds[targetIndex]);
        sourceRole = vm_mock_service_trade_role_for_session(source, NULL);
        targetRole = vm_mock_service_trade_role_for_session(target, NULL);
        memset(&sourceStats, 0, sizeof(sourceStats));
        memset(&targetStats, 0, sizeof(targetStats));
        vm_net_mock_role_build_player_stats(sourceRole, &sourceStats);
        vm_net_mock_role_build_player_stats(targetRole, &targetStats);
        vm_mock_service_duel_apply_seat_modifier(duel, sourceIndex, &sourceStats);
        vm_mock_service_duel_apply_seat_modifier(duel, targetIndex, &targetStats);

        if (operate > 2 && skill != NULL)
            magical = vm_net_mock_battle_skill_is_magical(skill);
        /* 鬼道法术忽略闪躲；物理普攻/物技仍检定。 */
        if (!magical &&
            !vm_net_mock_battle_roll_hit(sourceStats.hit, targetStats.dodge))
        {
            hitOut->targetSelf = false;
            hitOut->supportNoDamage = false;
            hitOut->damage = 0;
            hitOut->childFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE;
            hitOut->targetHpAfter = duel->hp[targetIndex];
            return true;
        }

        if (operate > 2 && skill != NULL)
        {
            uint64_t coeffDamage = 0;
            u32 baseDamage = vm_net_mock_battle_skill_min_hp_damage(skill);

            /* Same guard as PvE: support rows must not fall back to ATK. */
            if (baseDamage == 0 &&
                (skill->targetDirection <= 2 || skill->hpChange >= 0))
            {
                hitOut->targetSelf = (skill->targetDirection <= 2);
                hitOut->supportNoDamage = true;
                hitOut->damage = 0;
                hitOut->targetHpAfter = hitOut->targetSelf ?
                    duel->hp[sourceIndex] : duel->hp[targetIndex];
                return true;
            }
            coeffDamage += (uint64_t)sourceStats.strength * skill->strengthCoeff;
            coeffDamage += (uint64_t)sourceStats.agility * skill->agilityCoeff;
            coeffDamage += (uint64_t)sourceStats.wisdom * skill->wisdomCoeff;
            coeffDamage = (coeffDamage + 50u) / 100u;
            rawDamage = baseDamage;
            if (coeffDamage > 0xffffffffull - rawDamage)
                rawDamage = 0xffffffffu;
            else
                rawDamage += (u32)coeffDamage;
            if (rawDamage == 0)
                rawDamage = sourceStats.attack ? sourceStats.attack : 1;
            {
                uint64_t skillBoosted =
                    (uint64_t)rawDamage *
                    (100ull + VM_NET_MOCK_BATTLE_JOB_SKILL_ATTACK_BONUS_PERCENT) /
                    100ull;
                rawDamage = skillBoosted > 0xffffffffull ? 0xffffffffu
                                                         : (u32)skillBoosted;
            }
        }
        else
        {
            rawDamage = sourceStats.attack ? sourceStats.attack : 1;
        }
        if (magical)
        {
            mitigation = targetStats.resist;
            rawDamage = vm_net_mock_env_u32_if_set(
                "CBE_DUEL_SKILL_DAMAGE",
                vm_net_mock_damage_after_resist(rawDamage, mitigation));
        }
        else
        {
            mitigation = targetStats.defense;
            rawDamage = vm_net_mock_env_u32_if_set(
                operate > 2 ? "CBE_DUEL_SKILL_DAMAGE" : "CBE_DUEL_DAMAGE",
                vm_net_mock_damage_after_defense(rawDamage, mitigation));
        }
        if (rawDamage == 0)
            rawDamage = 1;
        critical = vm_net_mock_battle_roll_crit(sourceStats.crit);
        if (critical)
        {
            rawDamage = vm_net_mock_battle_apply_crit_damage(rawDamage);
            hitOut->childFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_CRIT;
        }
        rawDamage = vm_net_mock_min_u32(rawDamage, duel->hp[targetIndex]);
        if (rawDamage != 0 && skill != NULL && operate > 2)
        {
            if (vm_net_mock_battle_skill_is_silence(skill))
                duel->silenceRounds[targetIndex] = skill->durationRounds;
            else if (vm_net_mock_battle_skill_applies_enemy_debuff(skill))
                vm_net_mock_battle_modifier_set_from_skill(
                    &duel->modifiers[targetIndex], skill);
            else if (vm_net_mock_battle_skill_is_dispel(skill))
            {
                memset(&duel->modifiers[targetIndex], 0,
                       sizeof(duel->modifiers[targetIndex]));
                duel->silenceRounds[targetIndex] = 0;
            }
        }
        hitOut->targetSelf = false;
        hitOut->supportNoDamage = false;
        hitOut->damage = rawDamage;
        hitOut->targetHpAfter = duel->hp[targetIndex] - rawDamage;
        return true;
    }
}

static u32 vm_net_mock_build_pending_duel_action_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer);
static u32 vm_net_mock_build_pending_duel_terminal_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer);
static u32 vm_net_mock_merge_wt_packets(u8 *out, u32 outCap,
                                         const u8 *first, u32 firstLen,
                                         const u8 *second, u32 secondLen);
static u32 vm_net_mock_build_duel_exit_packet(u8 *out, u32 outCap,
                                               vm_mock_service_client_session *observer);
static u32 vm_net_mock_build_duel_settle_packet(u8 *out, u32 outCap,
                                                 vm_mock_service_client_session *observer);
static bool vm_net_mock_append_duel_spar_status7_object(
    u8 *out,
    u32 outCap,
    u32 *pos,
    vm_mock_service_client_session *observer);
static void vm_mock_service_duel_queue_result_message(
    vm_mock_service_client_session *observer);

static u32 vm_net_mock_build_duel_action_packet(
    u8 *out,
    u32 outCap,
    vm_mock_service_duel *duel,
    const vm_mock_service_duel_event *event,
    vm_mock_service_client_session *observer,
    int observerIndex)
{
    u8 actionInfo[256];
    u32 actionInfoLen = 0;
    u32 pos = 5;
    u8 actionCount = 0;
    const vm_mock_service_duel_hit *hit = NULL;
    u8 actorWire = 0;
    u8 targetWire = 1;
    u8 actionType = 0;
    u32 effectIndex = 0;
    vm_mock_service_client_session *caster = NULL;
    u32 casterWireId = 0;
    bool appendedDeathAction = false;
    u8 deathWireLogged = 0xff;

    if (out == NULL || outCap < pos || duel == NULL || event == NULL ||
        !event->valid || event->hitCount == 0 || observer == NULL ||
        observerIndex < 0 || observerIndex > 1)
    {
        return 0;
    }
    /*
     * Spar has current_team_count=1.  InitActionSlot_B reads exactly one
     * teaminfo row; stuffing two skill casters into one 4/6 leaves the local
     * caster's unit+1344 at 0 (MP snaps to 0 after type-1) and can desync the
     * actioninfo stream.  Each duel event is therefore one hit / one 4/6.
     */
    hit = &event->hits[0];
    if (!hit->valid)
        return 0;
    if ((int)hit->sourceIndex != observerIndex)
    {
        actorWire = 1;
        targetWire = 0;
    }
    if (hit->targetSelf)
        targetWire = actorWire;
    actionType = hit->operate > 2 ? 1 : 0;
    if (actionType == 1)
        effectIndex = vm_net_mock_battle_operate_skill_effect(hit->operate);
    memset(actionInfo, 0, sizeof(actionInfo));
    if (!vm_net_mock_append_battle_actioninfo_record(
            actionInfo, sizeof(actionInfo), &actionInfoLen,
            actionType, actorWire, targetWire,
            vm_net_mock_battle_child_flag_with_env(
                "CBE_BATTLE_FIRST_CHILD_FLAG", hit->childFlag),
            hit->supportNoDamage ? hit->damage :
                vm_net_mock_battle_negative_delta_u32(hit->damage),
            0,
            effectIndex, 0, 0, 0))
    {
        return 0;
    }
    actionCount = 1;
    /*
     * PvE final kill embeds type-3 death on the fallen fighter in the same
     * 4/6; that action completion opens the settlement panel with co-packet
     * 4/7 caches.  4/8 alone only shows a blank prompt.  Both seats need the
     * death record on the fallen wire (winner: opp wire 1; loser: local
     * wire 0) so each client composes its own settle banner.
     */
    if (event->terminal)
    {
        int deadSeat = ((int)hit->sourceIndex == 0) ? 1 : 0;
        u8 deadWire = (deadSeat == observerIndex) ? 0 : 1;
        u8 deathActionType = (u8)vm_net_mock_env_u32(
            "CBE_BATTLE_DEATH_ACTION_TYPE", 3);

        if (!vm_net_mock_append_battle_actioninfo_record(
                actionInfo, sizeof(actionInfo), &actionInfoLen,
                deathActionType, deadWire, 0, 0, 0, 0, 0, 0, 0, 0))
        {
            return 0;
        }
        ++actionCount;
        appendedDeathAction = true;
        deathWireLogged = deadWire;
    }
    if (actionType == 1)
    {
        caster = vm_mock_service_find_client_session(
            duel->clientIds[hit->sourceIndex]);
        if (caster == NULL)
            return 0;
        casterWireId = vm_mock_service_team_member_wire_id(observer, caster);
        if (casterWireId == 0 ||
            !vm_net_mock_append_battle_action6_object_ex(
                out, outCap, &pos, actionInfo, actionInfoLen, actionCount,
                true, casterWireId,
                duel->hp[hit->sourceIndex], hit->sourceMpAfter))
        {
            return 0;
        }
    }
    else if (!vm_net_mock_append_battle_action6_object(
                 out, outCap, &pos, actionInfo, actionInfoLen, actionCount))
    {
        return 0;
    }
    {
        u8 objectCount = 1;

        /*
         * PvE solo victory: append 4/7 into the same WT as the killing 4/6
         * before finish (not a merged second packet).  Panel opens when the
         * terminal action ends and reads caches filled by that co-packet 4/7.
         */
        if (event->terminal && duel->finished && observer != NULL &&
            !observer->sparSettleDelivered)
        {
            u32 bannerHold = 0;

            if (!vm_net_mock_append_duel_spar_status7_object(out, outCap, &pos,
                                                              observer))
            {
                return 0;
            }
            ++objectCount;
            observer->sparSettleDelivered = true;
            /*
             * 4/7(+fdata) is already in this packet.  Hold 5s before 4/8 so the
             * settle banner can open after the death anim and show fdata.
             */
            bannerHold = vm_net_mock_env_u32("CBE_DUEL_BANNER_HOLD_TICKS", 50);
            if (bannerHold < 50)
                bannerHold = 50;
            if (bannerHold > 100)
                bannerHold = 100;
            duel->terminalNotBeforeTick = g_schedulerTick + bannerHold;
            printf("[info][mock-service] duel_settle_inline serial=%u "
                   "observer=%08x resp=%u dead=%u objects=%u "
                   "death=%u wire=%u exp_granted=%u "
                   "exit=4/6(+type3)+4/7(pve-append,+1exp,+1gold,fdata) "
                   "banner_hold=%u not_before=%u\n",
                   duel->serial, observer->clientId, pos,
                   observer->sparExitWasDead ? 1u : 0u, objectCount,
                   appendedDeathAction ? 1u : 0u, deathWireLogged,
                   observer->sparExitExpGranted ? 1u : 0u,
                   bannerHold, duel->terminalNotBeforeTick);
        }
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        return pos;
    }
}

static bool vm_mock_service_duel_has_undelivered_action(
    const vm_mock_service_duel *duel)
{
    if (duel == NULL)
        return false;
    for (u8 i = 0; i < VM_MOCK_SERVICE_DUEL_EVENT_MAX; ++i)
    {
        if (duel->events[i].valid && duel->events[i].deliveredMask != 3)
            return true;
    }
    return false;
}

static void vm_mock_service_duel_clear_round_commits(vm_mock_service_duel *duel)
{
    if (duel == NULL)
        return;
    duel->roundCommitMask = 0;
    duel->roundCommitOperate[0] = 0;
    duel->roundCommitOperate[1] = 0;
    duel->roundCommitDeadlineTick = 0;
}

static u32 vm_mock_service_duel_round_commit_timeout_ticks(void)
{
    u32 ms = vm_net_mock_env_u32("CBE_DUEL_ROUND_COMMIT_TIMEOUT_MS", 20000);
    u32 ticks = 0;

    if (ms < 3000)
        ms = 3000;
    ticks = (ms + (u32)VM_SCHED_FRAME_MS - 1u) / (u32)VM_SCHED_FRAME_MS;
    if (ticks == 0)
        ticks = 1;
    return ticks;
}

static bool vm_mock_service_duel_resolve_committed_round(
    vm_mock_service_duel *duel)
{
    u8 order[2];
    u8 created = 0;
    u32 firstSerial = 0;
    bool terminal = false;
    u8 freshModifierMask = 0;

    if (duel == NULL || duel->finished || duel->roundCommitMask != 3)
        return false;
    if (vm_mock_service_duel_has_undelivered_action(duel))
        return false;

    order[0] = duel->firstTurnIndex;
    order[1] = (u8)(1u - duel->firstTurnIndex);
    for (u8 i = 0; i < 2; ++i)
    {
        u8 sourceIndex = order[i];
        u8 foeIndex = (u8)(1u - sourceIndex);
        u32 operate = duel->roundCommitOperate[sourceIndex];
        u32 nextSerial = 0;
        u8 sourceSlot = 0;
        vm_mock_service_duel_event *event = NULL;
        vm_mock_service_duel_hit hit;

        if (duel->hp[sourceIndex] == 0 || duel->hp[foeIndex] == 0)
            break;
        if (operate != 0 && operate <= 2)
            operate = 0;
        if (operate > 2)
        {
            const vm_net_mock_skill_catalog_item *skill =
                vm_net_mock_battle_operate_skill(operate);
            if (skill == NULL || duel->mp[sourceIndex] < skill->mpCost)
                operate = 0;
        }
        if (!vm_mock_service_duel_apply_operate(duel, sourceIndex, operate, &hit))
            continue;
        /*
         * Offensive hits with 0 damage are skipped (nothing to play).
         * Support skills always emit a 4/6 even when heal amount is 0
         * (full HP) so the client plays the cast/buff FX like PvE.
         */
        if (!hit.supportNoDamage && hit.damage == 0)
            continue;

        nextSerial = duel->actionSerial + 1;
        if (nextSerial == 0)
            ++nextSerial;
        sourceSlot = (u8)((nextSerial - 1) % VM_MOCK_SERVICE_DUEL_EVENT_MAX);
        event = &duel->events[sourceSlot];
        if (event->valid)
        {
            printf("[warn][mock-service] duel_round_resolve_ring_full serial=%u "
                   "slot=%u created=%u\n",
                   duel->serial, sourceSlot, created);
            break;
        }
        memset(event, 0, sizeof(*event));
        event->valid = true;
        event->serial = nextSerial;
        event->hitCount = 1;
        event->hits[0] = hit;
        event->terminal = (!hit.supportNoDamage && !hit.targetSelf &&
                           hit.targetHpAfter == 0);
        event->deliveredMask = 0;
        duel->actionSerial = nextSerial;
        duel->mp[sourceIndex] = hit.sourceMpAfter;
        if (hit.targetSelf)
            duel->hp[sourceIndex] = hit.targetHpAfter;
        else if (!hit.supportNoDamage)
            duel->hp[foeIndex] = hit.targetHpAfter;
        if (hit.supportNoDamage && hit.targetSelf &&
            vm_net_mock_battle_operate_skill_targets_friendly_group_modifier(
                hit.operate))
        {
            const vm_net_mock_skill_catalog_item *modSkill =
                vm_net_mock_battle_operate_skill(hit.operate);

            vm_net_mock_battle_modifier_set_from_skill(
                &duel->modifiers[sourceIndex], modSkill);
            freshModifierMask = (u8)(freshModifierMask | (u8)(1u << sourceIndex));
        }
        if (firstSerial == 0)
            firstSerial = nextSerial;
        ++created;
        printf("[info][mock-service] duel_hit serial=%u actor=%u operate=%u "
               "support=%u self=%u amount=%u hp_after=%u mp_after=%u "
               "evidence=skill.dsh:目标指向+PvE-support-branches\n",
               duel->serial, sourceIndex, hit.operate,
               hit.supportNoDamage ? 1u : 0u, hit.targetSelf ? 1u : 0u,
               hit.damage, hit.targetHpAfter, hit.sourceMpAfter);
        if (event->terminal)
        {
            terminal = true;
            break;
        }
    }
    if (created == 0)
    {
        u32 operate0 = duel->roundCommitOperate[0];
        u32 operate1 = duel->roundCommitOperate[1];

        vm_mock_service_duel_clear_round_commits(duel);
        printf("[warn][mock-service] duel_round_resolve_empty serial=%u "
               "operate=%u/%u first=%u\n",
               duel->serial, operate0, operate1, duel->firstTurnIndex);
        return false;
    }
    /* Age pre-existing buffs once per resolved round; freshly cast duration
     * survives until a later round (team-battle contract). */
    for (u8 seat = 0; seat < 2; ++seat)
    {
        if ((freshModifierMask & (u8)(1u << seat)) != 0)
            continue;
        vm_net_mock_battle_modifier_advance_round(&duel->modifiers[seat]);
        if (duel->silenceRounds[seat] != 0)
            --duel->silenceRounds[seat];
    }
    vm_mock_service_duel_clear_round_commits(duel);
    if (terminal)
    {
        /*
         * Play-window delay is in scheduler ticks (VM_SCHED_FRAME_MS=100).
         * Default 4 (~0.4s): let killing 4/6(+type3)+4/7 start, then tear
         * down early so the blank 4/8 strip is gone before center tip.
         * Env 0 is allowed for debug.
         */
        u32 delayTicks = vm_net_mock_env_u32("CBE_DUEL_TERMINAL_DELAY_TICKS", 4);

        duel->finished = true;
        duel->startPendingMask = 0;
        duel->terminalPendingMask = duel->startedMask;
        duel->terminalNotBeforeTick = g_schedulerTick + delayTicks;
        vm_mock_service_duel_arm_both_exits(duel);
    }
    printf("[info][mock-service] duel_round_resolve serial=%u first_action=%u "
           "events=%u first=%u terminal=%u hp=%u/%u,%u/%u mp=%u/%u,%u/%u "
           "evidence=spar-team_count1-one-4/6-per-hit+InitActionSlot_B:0x6DBC\n",
           duel->serial, firstSerial, created, duel->firstTurnIndex,
           terminal ? 1u : 0u,
           duel->hp[0], duel->hpMax[0], duel->hp[1], duel->hpMax[1],
           duel->mp[0], duel->mpMax[0], duel->mp[1], duel->mpMax[1]);
    if (terminal)
    {
        printf("[info][mock-service] duel_terminal_arm serial=%u pending=%02x "
               "now=%u not_before=%u delay_ticks=%u\n",
               duel->serial, duel->terminalPendingMask,
               g_schedulerTick, duel->terminalNotBeforeTick,
               duel->terminalNotBeforeTick - g_schedulerTick);
    }
    return true;
}

static u32 vm_net_mock_build_duel_undelivered_action_bundle(
    u8 *out,
    u32 outCap,
    vm_mock_service_duel *duel,
    vm_mock_service_client_session *observer,
    int observerIndex)
{
    u8 observerBit = 0;
    u32 responseLen = 0;
    u32 deliveredCount = 0;

    if (out == NULL || outCap < 5 || duel == NULL || observer == NULL ||
        observerIndex < 0 || observerIndex > 1)
    {
        return 0;
    }
    observerBit = (u8)(1u << observerIndex);
    for (;;)
    {
        vm_mock_service_duel_event *event = NULL;
        u32 oldestSerial = 0xffffffffu;
        u8 packet[512];
        u32 packetLen = 0;

        for (u8 i = 0; i < VM_MOCK_SERVICE_DUEL_EVENT_MAX; ++i)
        {
            vm_mock_service_duel_event *candidate = &duel->events[i];
            if (!candidate->valid ||
                (candidate->deliveredMask & observerBit) != 0)
            {
                continue;
            }
            if (candidate->serial < oldestSerial)
            {
                oldestSerial = candidate->serial;
                event = candidate;
            }
        }
        if (event == NULL)
            break;
        memset(packet, 0, sizeof(packet));
        packetLen = vm_net_mock_build_duel_action_packet(
            packet, sizeof(packet), duel, event, observer, observerIndex);
        if (packetLen == 0)
            return 0;
        if (responseLen == 0)
        {
            if (packetLen > outCap)
                return 0;
            memcpy(out, packet, packetLen);
            responseLen = packetLen;
        }
        else
        {
            u8 merged[1024];
            u32 mergedLen = vm_net_mock_merge_wt_packets(
                merged, sizeof(merged), out, responseLen, packet, packetLen);
            if (mergedLen == 0 || mergedLen > outCap)
                return 0;
            memcpy(out, merged, mergedLen);
            responseLen = mergedLen;
        }
        event->deliveredMask = (u8)(event->deliveredMask | observerBit);
        ++deliveredCount;
        printf("[info][mock-service] duel_action_deliver serial=%u action=%u "
               "observer=%08x actor=%u operate=%u damage=%u mp_after=%u "
               "terminal=%u delivered=%02x bundle=%u settle=%u\n",
               duel->serial, event->serial, observer->clientId,
               event->hits[0].sourceIndex, event->hits[0].operate,
               event->hits[0].damage, event->hits[0].sourceMpAfter,
               event->terminal ? 1u : 0u, event->deliveredMask, deliveredCount,
               observer->sparSettleDelivered ? 1u : 0u);
        if (event->deliveredMask == 3)
            memset(event, 0, sizeof(*event));
    }
    /* Terminal 4/7 is appended inside build_duel_action_packet (PvE-style). */
    return responseLen;
}

static bool vm_mock_service_duel_try_timeout_fill(
    vm_mock_service_duel *duel)
{
    u8 filledMask = 0;

    if (duel == NULL || duel->finished || duel->roundCommitMask == 0 ||
        duel->roundCommitMask == 3 || duel->roundCommitDeadlineTick == 0)
    {
        return false;
    }
    if (g_schedulerTick < duel->roundCommitDeadlineTick)
        return false;
    for (u8 seat = 0; seat < 2; ++seat)
    {
        u8 bit = (u8)(1u << seat);
        if ((duel->roundCommitMask & bit) != 0)
            continue;
        duel->roundCommitOperate[seat] = 0;
        duel->roundCommitMask = (u8)(duel->roundCommitMask | bit);
        filledMask = (u8)(filledMask | bit);
    }
    if (filledMask == 0 || duel->roundCommitMask != 3)
        return false;
    printf("[info][mock-service] duel_round_timeout_autofill serial=%u "
           "filled=%02x commit=%02x deadline=%u now=%u default=operate0 "
           "evidence=CBE_DUEL_ROUND_COMMIT_TIMEOUT_MS\n",
           duel->serial, filledMask, duel->roundCommitMask,
           duel->roundCommitDeadlineTick, g_schedulerTick);
    return vm_mock_service_duel_resolve_committed_round(duel);
}

static void vm_mock_service_duel_queue_result_message(
    vm_mock_service_client_session *observer)
{
    /*
     * Do not enqueue 1/3/3 here. Scene-sync can dequeue the notice while
     * Battle.cbm still owns the UI and the map chat layer never shows it.
     * Arm on 4/8; flush from map-side 25/5 (or after duel release).
     */
    if (observer == NULL || observer->sparExitMessageQueued)
        return;
    vm_mock_service_session_arm_spar_result_message(observer);
}

static bool vm_net_mock_append_duel_spar_status7_object(
    u8 *out,
    u32 outCap,
    u32 *pos,
    vm_mock_service_client_session *observer)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = NULL;
    u32 totalExp = 0;
    u32 lastExp = 0;
    u32 nextExp = 0;
    u32 percentExp = 0;
    u32 gold = VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 level = 1;
    u32 recoverHp = 0;
    u32 recoverMp = 0;

    if (out == NULL || outCap == 0 || pos == NULL || observer == NULL)
        return false;
    role = vm_mock_service_trade_role_for_session(observer, NULL);
    if (role != NULL && !observer->sparExitExpGranted)
    {
        u32 expBefore = role->exp;
        u32 goldBefore = role->money;

        (void)vm_net_mock_role_add_exp(role, 1);
        role->money = vm_net_mock_add_capped_u32(role->money, 1);
        vm_net_mock_role_mark_inventory_dirty("spar-settle-reward");
        observer->sparExitExpGranted = true;
        printf("[info][mock-service] duel_exit_reward_grant observer=%08x "
               "role=%u exp=%u->%u gold=%u->%u gain=1/1\n",
               observer->clientId, role->roleId, expBefore, role->exp,
               goldBefore, role->money);
    }
    if (role != NULL)
    {
        totalExp = role->exp;
        gold = role->money;
        level = role->level ? role->level : 1;
    }
    lastExp = vm_net_mock_role_last_level_exp(totalExp);
    nextExp = vm_net_mock_role_next_level_start_exp(totalExp);
    percentExp = vm_net_mock_role_exp_percent(totalExp);
    /*
     * Match PvE victory 4/7 defaults: hp/mp recovery display deltas are 0.
     * Spar never overwrote durable vitals.  Trial: include win/lose fdata so
     * the battle settle banner can show 挑战成功/失败 before 4/8 refresh.
     * Post-leave 1/3/3 remains the map-side backup copy.
     */
    recoverHp = 0;
    recoverMp = 0;
    (void)observer->sparExitRecoverHp;
    (void)observer->sparExitRecoverMp;
    {
        /* GBK: 挑战成功 / 挑战失败 */
        static const char sparWinGbk[] =
            "\xcc\xf8\xd5\xbd\xb3\xc9\xb9\xa6";
        static const char sparLoseGbk[] =
            "\xcc\xf8\xd5\xbd\xca\xa7\xb0\xdc";
        const char *fdataGbk =
            observer->sparExitWasDead ? sparLoseGbk : sparWinGbk;

        if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 4, 7, &objectStart))
            return false;
        if (!vm_net_mock_put_object_u32(out, outCap, pos, "exp", totalExp) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", lastExp) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "curexp", nextExp) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", percentExp) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "energy", 100) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "energymax", 100) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "gold", gold) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "level", level) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "bagstatus", 0) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "hp", recoverHp) ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "mp", recoverMp) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "itemnum", 0) ||
            !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", NULL, 0) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "fdata", fdataGbk) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "autorevive", 0))
        {
            return false;
        }
        printf("[info][mock-service] duel_settle_fdata observer=%08x dead=%u "
               "fdata_bytes=%u text=%s\n",
               observer->clientId, observer->sparExitWasDead ? 1u : 0u,
               (u32)strlen(fdataGbk),
               observer->sparExitWasDead ? "lose" : "win");
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_duel_settle_packet(u8 *out, u32 outCap,
                                                 vm_mock_service_client_session *observer)
{
    u32 pos = 5;

    if (out == NULL || outCap < pos || observer == NULL)
        return 0;
    if (!vm_net_mock_append_duel_spar_status7_object(out, outCap, &pos, observer))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_duel_exit_packet(u8 *out, u32 outCap,
                                               vm_mock_service_client_session *observer)
{
    u32 pos = 5;

    if (out == NULL || outCap < pos || observer == NULL)
        return 0;
    /*
     * 4/8 clears Battle.cbm marks (spar stuck on 4/11+4/9 alone).  Win/lose
     * copy (挑战成功/失败) is queued as a delayed system message after this
     * tear-down so it is not overwritten by the 4/8 settle refresh.
     */
    (void)observer;
    if (!vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos) ||
        !vm_net_mock_append_battle_terminal_case11_object(out, outCap, &pos) ||
        !vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 3);
    return pos;
}

static u32 vm_net_mock_build_duel_terminal_phase_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_duel *duel,
    vm_mock_service_client_session *observer,
    int observerIndex)
{
    u8 observerBit = 0;
    u32 pos = 0;
    u32 holdTicks = 0;
    u32 bannerHold = 0;

    if (out == NULL || outCap < 5 || duel == NULL || observer == NULL ||
        observerIndex < 0 || observerIndex > 1)
    {
        return 0;
    }
    observerBit = (u8)(1u << observerIndex);
    if ((duel->terminalPendingMask & observerBit) == 0)
        return 0;
    if (!observer->sparSettleDelivered)
    {
        pos = vm_net_mock_build_duel_settle_packet(out, outCap, observer);
        if (pos == 0)
            return 0;
        observer->sparSettleDelivered = true;
        /* Late 4/7 also needs a 5s read window before 4/8 blanks fdata. */
        bannerHold = vm_net_mock_env_u32("CBE_DUEL_BANNER_HOLD_TICKS", 50);
        if (bannerHold < 50)
            bannerHold = 50;
        if (bannerHold > 100)
            bannerHold = 100;
        /*
         * Keep this seat in terminalPendingMask so the next poll delivers
         * tear-down after the banner hold.  Extend not_before for both seats'
         * play/hold window.
         */
        if (duel->terminalNotBeforeTick < g_schedulerTick + bannerHold)
            duel->terminalNotBeforeTick = g_schedulerTick + bannerHold;
        printf("[info][mock-service] duel_settle_deliver serial=%u observer=%08x "
               "resp=%u dead=%u exp_granted=%u "
               "exit=4/7(late,+1exp,+1gold,fdata) banner_hold=%u "
               "not_before=%u\n",
               duel->serial, observer->clientId, pos,
               observer->sparExitWasDead ? 1u : 0u,
               observer->sparExitExpGranted ? 1u : 0u,
               bannerHold, duel->terminalNotBeforeTick);
        return pos;
    }
    pos = vm_net_mock_build_duel_exit_packet(out, outCap, observer);
    if (pos == 0)
        return 0;
    vm_mock_service_duel_queue_result_message(observer);
    duel->terminalPendingMask &= (u8)~observerBit;
    if (duel->terminalPendingMask == 0)
    {
        holdTicks = vm_net_mock_env_u32("CBE_DUEL_EXIT_REDELIVER_TICKS", 300);
        if (holdTicks < 90)
            holdTicks = 90;
        duel->releaseNotBeforeTick = g_schedulerTick + holdTicks;
    }
    printf("[info][mock-service] duel_terminal_deliver serial=%u observer=%08x "
           "remaining=%02x resp=%u dead=%u exit=4/8+4/11+4/9 recover_hp=%u "
           "release_not_before=%u\n",
           duel->serial, observer->clientId,
           duel->terminalPendingMask, pos,
           observer->sparExitWasDead ? 1u : 0u,
           observer->sparExitRecoverHp,
           duel->releaseNotBeforeTick);
    vm_mock_service_duel_release_if_done(duel);
    return pos;
}

static u32 vm_net_mock_build_duel_post_release_exit_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_mock_service_client_session *source = vm_mock_service_get_active_client_session();
    u32 responseLen = 0;

    if (out == NULL || outCap < 5 || source == NULL ||
        !vm_net_mock_is_battle_operate_request(request, requestLen))
    {
        return 0;
    }
    if (source->sparExitRedeliverUntilTick == 0 ||
        g_schedulerTick >= source->sparExitRedeliverUntilTick)
    {
        return 0;
    }
    if (vm_mock_service_duel_find_for_client(source->clientId, NULL) != NULL)
        return 0;
    /*
     * First 4/8 already armed/delivered the result path.  Re-sending
     * 4/8+4/11+4/9 here recreates the blank settle strip after the map
     * center tip — do not redeliver.
     */
    if (source->sparResultMessageArmed || source->sparExitMessageQueued)
        return 0;
    if (!source->sparSettleDelivered)
    {
        responseLen = vm_net_mock_build_duel_settle_packet(out, outCap, source);
        if (responseLen == 0)
            return 0;
        source->sparSettleDelivered = true;
        printf("[info][mock-service] duel_operate_post_release source=%08x "
               "dead=%u action=settle-redeliver resp=%u exit=4/7(+1exp) "
               "until=%u\n",
               source->clientId, source->sparExitWasDead ? 1u : 0u, responseLen,
               source->sparExitRedeliverUntilTick);
        return responseLen;
    }
    responseLen = vm_net_mock_build_duel_exit_packet(out, outCap, source);
    if (responseLen == 0)
        return 0;
    vm_mock_service_duel_queue_result_message(source);
    printf("[info][mock-service] duel_operate_post_release source=%08x "
           "dead=%u action=exit-redeliver resp=%u exit=4/8+4/11+4/9 "
           "until=%u\n",
           source->clientId, source->sparExitWasDead ? 1u : 0u, responseLen,
           source->sparExitRedeliverUntilTick);
    return responseLen;
}

static u32 vm_net_mock_merge_wt_packets(u8 *out, u32 outCap,
                                         const u8 *first, u32 firstLen,
                                         const u8 *second, u32 secondLen)
{
    u32 bodyFirst = 0;
    u32 bodySecond = 0;
    u32 total = 0;
    u8 objectCount = 0;

    if (out == NULL || first == NULL || second == NULL ||
        firstLen < 5 || secondLen < 5 ||
        first[0] != 'W' || first[1] != 'T' ||
        second[0] != 'W' || second[1] != 'T')
    {
        return 0;
    }
    bodyFirst = firstLen - 5;
    bodySecond = secondLen - 5;
    total = 5 + bodyFirst + bodySecond;
    objectCount = (u8)(first[4] + second[4]);
    if (total > outCap || (u32)first[4] + (u32)second[4] > 255u)
        return 0;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(total >> 8);
    out[3] = (u8)total;
    out[4] = objectCount;
    memcpy(out + 5, first + 5, bodyFirst);
    memcpy(out + 5 + bodyFirst, second + 5, bodySecond);
    return total;
}

static u32 vm_net_mock_build_duel_operate_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_mock_service_client_session *source = vm_mock_service_get_active_client_session();
    vm_mock_service_duel *duel = NULL;
    int sourceIndex = -1;
    u32 operate = 0;
    u8 operate8 = 0;
    u32 responseLen = 0;
    u32 prefixLen = 0;
    u8 sourceBit = 0;
    u8 prefix[512];

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
        if (vm_mock_service_duel_has_undelivered_action(duel))
        {
            responseLen = vm_net_mock_build_pending_duel_action_response(
                out, outCap, source);
            if (responseLen != 0)
                return responseLen;
            vm_net_mock_finish_wt_packet(out, 5, 0);
            return 5;
        }
        if (g_schedulerTick < duel->terminalNotBeforeTick)
        {
            /* Play window or banner hold. */
            vm_net_mock_finish_wt_packet(out, 5, 0);
            printf("[info][mock-service] duel_operate_terminal_wait serial=%u "
                   "source=%08x now=%u not_before=%u settle=%u action=empty-ack\n",
                   duel->serial, source->clientId, g_schedulerTick,
                   duel->terminalNotBeforeTick,
                   source->sparSettleDelivered ? 1u : 0u);
            return 5;
        }
        /*
         * One tear-down per seat.  Re-calling phase after terminalPendingMask
         * cleared re-sent 4/11+4/9 forever while Battle.cbm stayed up
         * (winner stuck, release_not_before kept extending).
         */
        if ((duel->terminalPendingMask & (1u << sourceIndex)) != 0)
        {
            responseLen = vm_net_mock_build_duel_terminal_phase_response(
                out, outCap, duel, source, sourceIndex);
            if (responseLen != 0)
                return responseLen;
        }
        vm_mock_service_duel_release_if_done(duel);
        vm_net_mock_finish_wt_packet(out, 5, 0);
        return 5;
    }
    if ((duel->startedMask & (1u << sourceIndex)) == 0)
    {
        vm_net_mock_finish_wt_packet(out, 5, 0);
        return 5;
    }

    memset(prefix, 0, sizeof(prefix));
    vm_mock_service_duel_try_timeout_fill(duel);

    /*
     * Prior resolved round must finish delivery before a new commit window.
     * Drain owed 4/6 first; never open a second commit while actioninfo is
     * still outstanding (team-battle barrier contract).
     */
    if (vm_mock_service_duel_has_undelivered_action(duel))
    {
        prefixLen = vm_net_mock_build_pending_duel_action_response(
            prefix, sizeof(prefix), source);
        if (prefixLen != 0)
        {
            if (prefixLen > outCap)
                return 0;
            memcpy(out, prefix, prefixLen);
            printf("[info][mock-service] duel_action_drain serial=%u "
                   "source=%08x actor=%d resp=%u reason=undelivered-before-commit\n",
                   duel->serial, source->clientId, sourceIndex, prefixLen);
            return prefixLen;
        }
        vm_net_mock_finish_wt_packet(out, 5, 0);
        printf("[info][mock-service] duel_action_wait serial=%u source=%08x "
               "actor=%d commit=%02x action=empty-ack reason=undelivered-peer\n",
               duel->serial, source->clientId, sourceIndex,
               duel->roundCommitMask);
        return 5;
    }

    if (!vm_net_mock_get_object_u32_field(request, requestLen,
                                          "Operate", &operate) &&
        vm_net_mock_get_object_u8_field(request, requestLen,
                                        "Operate", &operate8))
    {
        operate = operate8;
    }
    if (operate != 0 && operate <= 2)
        operate = 0;

    sourceBit = (u8)(1u << sourceIndex);
    if ((duel->roundCommitMask & sourceBit) != 0)
    {
        vm_net_mock_finish_wt_packet(out, 5, 0);
        printf("[info][mock-service] duel_round_commit_dup serial=%u "
               "source=%08x actor=%d commit=%02x action=empty-ack\n",
               duel->serial, source->clientId, sourceIndex,
               duel->roundCommitMask);
        return 5;
    }

    if (operate > 2)
    {
        const vm_net_mock_skill_catalog_item *skill =
            vm_net_mock_battle_operate_skill(operate);
        if (skill == NULL || duel->mp[sourceIndex] < skill->mpCost)
            operate = 0;
    }

    duel->roundCommitOperate[sourceIndex] = operate;
    duel->roundCommitMask = (u8)(duel->roundCommitMask | sourceBit);
    if ((duel->roundCommitMask & (u8)~sourceBit) == 0)
    {
        duel->roundCommitDeadlineTick =
            g_schedulerTick + vm_mock_service_duel_round_commit_timeout_ticks();
    }
    printf("[info][mock-service] duel_round_commit serial=%u source=%08x "
           "actor=%d operate=%u commit=%02x deadline=%u first=%u\n",
           duel->serial, source->clientId, sourceIndex, operate,
           duel->roundCommitMask, duel->roundCommitDeadlineTick,
           duel->firstTurnIndex);

    if (duel->roundCommitMask != 3)
    {
        /* Non-final submit: zero-object WT only — never early 4/6. */
        vm_net_mock_finish_wt_packet(out, 5, 0);
        printf("[info][mock-service] duel_round_defer serial=%u source=%08x "
               "actor=%d commit=%02x action=empty-ack resp=5 "
               "evidence=team_battle_round_defer\n",
               duel->serial, source->clientId, sourceIndex,
               duel->roundCommitMask);
        return 5;
    }

    if (!vm_mock_service_duel_resolve_committed_round(duel))
    {
        vm_net_mock_finish_wt_packet(out, 5, 0);
        return 5;
    }
    responseLen = vm_net_mock_build_duel_undelivered_action_bundle(
        out, outCap, duel, source, sourceIndex);
    if (responseLen == 0)
    {
        vm_net_mock_finish_wt_packet(out, 5, 0);
        return 5;
    }
    printf("[info][mock-service] duel_round_release serial=%u source=%08x "
           "actor=%d resp=%u mapping=source(0->1),peer(1->0) "
           "evidence=one-4/6-per-hit-merged\n",
           duel->serial, source->clientId, sourceIndex, responseLen);
    return responseLen;
}

static u32 vm_net_mock_build_pending_duel_action_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer)
{
    vm_mock_service_duel *duel = NULL;
    int observerIndex = -1;

    if (out == NULL || outCap < 5 || observer == NULL)
        return 0;
    duel = vm_mock_service_duel_find_for_client(observer->clientId,
                                                &observerIndex);
    if (duel == NULL || observerIndex < 0 ||
        (duel->startedMask & (1u << observerIndex)) == 0)
    {
        return 0;
    }
    vm_mock_service_duel_try_timeout_fill(duel);
    return vm_net_mock_build_duel_undelivered_action_bundle(
        out, outCap, duel, observer, observerIndex);
}

static u32 vm_net_mock_build_pending_duel_terminal_response(
    u8 *out,
    u32 outCap,
    vm_mock_service_client_session *observer)
{
    vm_mock_service_duel *duel = NULL;
    int observerIndex = -1;
    u8 observerBit = 0;
    u32 pos = 0;
    u32 holdTicks = 0;

    if (out == NULL || outCap < 5 || observer == NULL)
        return 0;
    duel = vm_mock_service_duel_find_for_client(observer->clientId,
                                                &observerIndex);
    if (duel == NULL || observerIndex < 0 || !duel->finished)
        return 0;
    observerBit = (u8)(1u << observerIndex);
    if ((duel->terminalPendingMask & observerBit) == 0)
    {
        vm_mock_service_duel_release_if_done(duel);
        return 0;
    }
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
    /* arm_both already ran at terminal; do not re-arm (would clear settle). */
    return vm_net_mock_build_duel_terminal_phase_response(
        out, outCap, duel, observer, observerIndex);
}

static u32 vm_net_mock_build_battle_pending_settlement_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u32 nowMs;

    if (outCap < pos)
        return 0;
    /*
     * Inline 4/7 already painted (or will paint) the panel.  Hangup/auto may
     * immediately fire another 4/2; answering with tear-down here races the
     * panel off-screen.  Hold tear-down until the delayed poll window.
     */
    if (g_vm_net_mock_battle_settlement_sent_serial == g_mockBattleOperateSessionSerial)
    {
        nowMs = scheduler_get_tick_ms();
        if (g_mockBattleSettlementExitPending != 0 &&
            nowMs < g_mockBattleSettlementExitNotBeforeMs)
        {
            vm_net_mock_finish_wt_packet(out, pos, 0);
            printf("[info][network] mock_battle_pending_settlement "
                   "action=hold-exit-for-panel remain_ms=%u serial=%u "
                   "evidence=4/7-then-delayed-exit\n",
                   g_mockBattleSettlementExitNotBeforeMs - nowMs,
                   g_mockBattleOperateSessionSerial);
            return pos;
        }
        return vm_net_mock_build_battle_settlement_exit_packet(
            out, outCap, "operate-followup");
    }
    if (!vm_net_mock_append_battle_terminal_status_objects(
            out, outCap, &pos, &objectCount, false))
        return 0;
    g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
    if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                           &objectCount,
                                                           "battle-pending-settlement",
                                                           false))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_battle_note_victory_settlement("pending-settlement-4/7-only");
    printf("[info][network] mock_battle_pending_settlement serial=%u objects=%u "
           "response=4/7+optional-7/7 exit=poll-delayed "
           "evidence=mmBattle:0x743C\n",
           g_mockBattleOperateSessionSerial,
           objectCount);
    vm_autotest_note("mock_battle_pending_settlement serial=%u objects=%u "
                     "response=4/7 exit=poll-delayed evidence=mmBattle:0x743C\n",
                     g_mockBattleOperateSessionSerial,
                     objectCount);
    return pos;
}

static void vm_net_mock_rewrite_battle_teaminfo_role_id(u8 *packet,
                                                        u32 packetLen,
                                                        u32 roleId)
{
    const u8 *teamInfo = NULL;
    u16 teamInfoLen = 0;
    u8 *mutableInfo = NULL;

    if (packet == NULL || roleId == 0 ||
        !vm_net_mock_get_response_object_entry_field(
            packet, packetLen, 4, 6, "teaminfo", &teamInfo, &teamInfoLen) ||
        teamInfo == NULL || teamInfoLen < 6 ||
        teamInfo[0] != 0 || teamInfo[1] != 4)
    {
        return;
    }
    mutableInfo = (u8 *)teamInfo;
    mutableInfo[2] = (u8)(roleId >> 24);
    mutableInfo[3] = (u8)(roleId >> 16);
    mutableInfo[4] = (u8)(roleId >> 8);
    mutableInfo[5] = (u8)roleId;
}

static void vm_net_mock_rewrite_battle_teaminfo_for_observer(
    u8 *packet,
    u32 packetLen,
    vm_mock_service_client_session *observer,
    vm_mock_service_team *team,
    u32 fallbackSourceWireId)
{
    const u8 *teamInfo = NULL;
    u16 teamInfoLen = 0;
    u8 *mutableInfo = NULL;
    u32 expectedLen = 0;
    char dbg[192];

    if (packet == NULL || observer == NULL || team == NULL ||
        team->battleMemberCount < 2 ||
        !vm_net_mock_get_response_object_entry_field(
            packet, packetLen, 4, 6, "teaminfo", &teamInfo, &teamInfoLen) ||
        teamInfo == NULL)
    {
        return;
    }
    expectedLen = (u32)team->battleMemberCount * 14u;
    if (teamInfoLen != expectedLen)
    {
        /* #region agent log */
        snprintf(dbg, sizeof(dbg),
                 "{\"observer\":\"%08x\",\"have\":%u,\"want\":%u,\"members\":%u,"
                 "\"fallback\":%u}",
                 observer->clientId, teamInfoLen, expectedLen,
                 team->battleMemberCount, fallbackSourceWireId);
        agent_dbg_hp_log("T2", "mock_server_battle.c:rewrite_teaminfo",
                         "teaminfo_len_mismatch", dbg);
        /* #endregion */
        /* Legacy single-row blobs still rewrite only the caster id. */
        if (fallbackSourceWireId != 0 &&
            teamInfoLen >= 6 && teamInfo[0] == 0 && teamInfo[1] == 4)
        {
            vm_net_mock_rewrite_battle_teaminfo_role_id(
                packet, packetLen, fallbackSourceWireId);
        }
        return;
    }
    mutableInfo = (u8 *)teamInfo;
    for (u8 i = 0; i < team->battleMemberCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->battleMemberClientIds[i]);
        u32 wireId = vm_mock_service_team_member_wire_id(observer, member);
        u32 hp = team->battleMemberHp[i];
        u32 mp = team->battleMemberMp[i];
        u32 row = (u32)i * 14u;

        if (member == NULL || wireId == 0 ||
            mutableInfo[row] != 0 || mutableInfo[row + 1] != 4)
        {
            return;
        }
        mutableInfo[row + 2] = (u8)(wireId >> 24);
        mutableInfo[row + 3] = (u8)(wireId >> 16);
        mutableInfo[row + 4] = (u8)(wireId >> 8);
        mutableInfo[row + 5] = (u8)wireId;
        mutableInfo[row + 6] = (u8)(hp >> 24);
        mutableInfo[row + 7] = (u8)(hp >> 16);
        mutableInfo[row + 8] = (u8)(hp >> 8);
        mutableInfo[row + 9] = (u8)hp;
        mutableInfo[row + 10] = (u8)(mp >> 24);
        mutableInfo[row + 11] = (u8)(mp >> 16);
        mutableInfo[row + 12] = (u8)(mp >> 8);
        mutableInfo[row + 13] = (u8)mp;
    }
    /* #region agent log */
    snprintf(dbg, sizeof(dbg),
             "{\"observer\":\"%08x\",\"members\":%u,\"bytes\":%u}",
             observer->clientId, team->battleMemberCount, teamInfoLen);
    agent_dbg_hp_log("T2", "mock_server_battle.c:rewrite_teaminfo",
                     "team_battle_teaminfo_rewritten", dbg);
    /* #endregion */
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
    /*
     * Observers who died, fled, or already left via revival stone must not
     * receive further shared 4/6 while their client is on the death prompt or
     * already tearing down Battle.cbm.  Silently retire their deliveries.
     */
    if ((vm_mock_service_team_battle_absent_mask(team) & memberBit) != 0)
    {
        vm_mock_service_team_battle_mark_member_events_delivered(team, memberBit);
        return 0;
    }
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
    g_mockBattleStartUsesSceneWireMaps = 1;
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
    /* Merged skill rounds carry one overlapped teaminfo row per frozen
     * member.  Remap every row into this observer's wire-id space; the
     * legacy single-row rewriter only patched the first id and left the
     * earlier casters' unit+1344 cache at 0. */
    vm_net_mock_rewrite_battle_teaminfo_for_observer(
        out, pos, observer, team, sourceWireId);

    if (event->terminalVictory)
    {
        if (!vm_net_mock_append_battle_terminal_status_objects(
                out, outCap, &pos, &objectCount, true))
            return 0;
        g_vm_net_mock_battle_settlement_sent_serial = g_mockBattleOperateSessionSerial;
        if (!vm_net_mock_append_battle_drop_refresh7_if_needed(
                out, outCap, &pos, &objectCount,
                "team-battle-peer-inline", true))
        {
            return 0;
        }
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        vm_net_mock_battle_note_victory_settlement("team-battle-peer");
        vm_net_mock_battle_save_terminal_role_state("team-battle-peer", true);
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
    vm_mock_service_duel_arm_both_exits(duel);
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
        vm_net_mock_battle_settlement_exit_clear("battle-escape-success");
        vm_net_mock_battle_post_exit_settle_clear("battle-escape-success");
        vm_net_mock_battle_auto_clear_pending();
        vm_net_mock_hangup_loop_clear("battle-escape-success");
        /* Arm after helpers are in scope via settlement-exit path; escape
         * returns to map the same way — call the cooldown helper defined
         * with settlement exit. */
        vm_net_mock_battle_arm_encounter_cooldown("battle-escape-success");
        vm_net_mock_battle_save_completed_current_role_state(
            "battle-escape-success");
        vm_mock_service_team_battle_note_member_exit(
            vm_mock_service_get_active_client_session(),
            true,
            true,
            0,
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
            u8 strikeActionType = counterActionType;
            u32 strikeEffectIndex = type1EffectIndex;
            u32 oneCounterDamage = 0;
            bool strikeIsHeal = false;
            u32 strikeHealAmount = 0;
            u8 strikeIndex = 0;

            if (g_mockBattleEnemyHpSlots[enemyIndex] == 0)
                continue;
            enemyWire = vm_net_mock_battle_enemy_wire_for_index(enemyIndex, playerOnRight,
                                                                battleSide, enemySlot);
            counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT",
                                                          enemyWire);
            counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_COUNTER_TARGET_WIRE_SLOT",
                                                           playerSlot);
            strikeIndex = (u8)actionCount;
            oneCounterDamage = vm_net_mock_battle_apply_enemy_counter_strike(
                g_vm_net_mock_battle_enemy_id_current, strikeIndex, counterActionType,
                type1EffectIndex, &strikeActionType, &strikeEffectIndex, &strikeIsHeal, &strikeHealAmount);
            if (strikeActionType == 1)
            {
                counterActorWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_ACTOR_WIRE_SLOT",
                                                              counterActorWireSlot);
                counterTargetWireSlot = (u8)vm_net_mock_env_u32("CBE_BATTLE_TYPE1_COUNTER_TARGET_WIRE_SLOT",
                                                               counterTargetWireSlot);
            }
            if (strikeIsHeal)
                counterTargetWireSlot = counterActorWireSlot;

            /* Miss keeps valueA=0 + child_flag=3 (闪躲); still append the counter. */
            totalDamage = vm_net_mock_add_capped_u32(totalDamage, oneCounterDamage);
            if (!vm_net_mock_append_battle_actioninfo_record(actionInfo, sizeof(actionInfo),
                                                             &actionInfoLen, strikeActionType,
                                                             counterActorWireSlot,
                                                             counterTargetWireSlot,
                                                             vm_net_mock_battle_child_flag_with_env(
                                                                 "CBE_BATTLE_COUNTER_CHILD_FLAG",
                                                                 strikeIsHeal
                                                                     ? VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL
                                                                     : vm_net_mock_battle_take_outcome_child_flag()),
                                                             strikeIsHeal
                                                                 ? strikeHealAmount
                                                                 : vm_net_mock_battle_negative_delta_u32(oneCounterDamage),
                                                             counterValueB,
                                                             (strikeActionType == 1 || strikeActionType == 2) ? strikeEffectIndex : 0,
                                                             (strikeActionType == 1 || strikeActionType == 2) ? type1Tail0 : 0,
                                                             (strikeActionType == 1 || strikeActionType == 2) ? type1Tail1 : 0,
                                                             (strikeActionType == 1 || strikeActionType == 2) ? type1Tail2 : 0))
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
        vm_mock_service_session_arm_battle_revival_confirm_for_death(
            "battle-escape-failed-death");
    }
    else
    {
        vm_net_mock_battle_save_current_role_state("battle-escape-failed");
    }
    vm_mock_service_team_battle_note_member_exit(
        vm_mock_service_get_active_client_session(),
        false,
        true,
        0,
        battleEndsThisRound ? "battle-escape-failed-death" :
                              "battle-escape-failed");

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

static void vm_net_mock_battle_auto_pull_team_vitals(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = NULL;
    int memberIndex = -1;

    if (session == NULL)
        return;
    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team == NULL || !team->battleActive ||
        !vm_mock_service_team_battle_contains_client(team, session->clientId))
    {
        return;
    }
    memberIndex = vm_mock_service_team_battle_member_index(team, session->clientId);
    if (memberIndex < 0 || memberIndex >= team->battleMemberCount)
        return;

    /*
     * Auto wire selection and arm_pending HP gates read per-account globals.
     * Peers mutate the shared team snapshot; restore alone can leave this seat
     * with stale/zero enemyhp so prefer stays set but synth never arms.
     */
    g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleStartUsesSceneWireMaps = 1;
    g_mockBattleEnemyCountCurrent = team->battleMonsterCount;
    g_vm_net_mock_team_battle_party_count_current = team->battleMemberCount;
    g_vm_net_mock_team_battle_member_count_current = team->battleMemberCount;
    g_vm_net_mock_team_battle_actor_slot_current = (u8)memberIndex;
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
    g_vm_net_mock_battle_enemy_id_current = team->battleEnemyId;
}

static void vm_net_mock_battle_suspend_solo_auto_for_team(const char *reason)
{
    if (g_mockBattleAutoPrefer == 0 &&
        g_mockHangupLoopActive == 0 &&
        g_mockHangupLoopScheduleAfterExit == 0 &&
        g_mockHangupLoopPendingArmed == 0 &&
        g_mockBattleAutoPendingArmed == 0 &&
        g_mockBattleAutoFlagPendingArmed == 0)
    {
        return;
    }
    printf("[info][network] mock_battle_suspend_solo_auto reason=%s prefer=%u "
           "hangup_loop=%u evidence=team-battle-barrier\n",
           reason ? reason : "-",
           g_mockBattleAutoPrefer ? 1 : 0,
           g_mockHangupLoopActive ? 1 : 0);
    g_mockBattleAutoPrefer = 0;
    g_mockBattleAutoSuppressNext12 = 0;
    vm_net_mock_battle_auto_clear_pending();
    vm_net_mock_hangup_loop_clear(reason ? reason : "team-battle");
}

static bool vm_net_mock_active_session_in_team_battle(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = NULL;

    if (session == NULL)
        return false;
    team = vm_mock_service_team_find_for_client(session->clientId);
    return team != NULL && team->battleActive &&
           vm_mock_service_team_battle_contains_client(team, session->clientId);
}

static bool vm_net_mock_battle_auto_team_seat_can_act(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = NULL;
    int memberIndex = -1;
    u8 memberBit = 0;

    if (session == NULL)
        return false;
    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team == NULL || !team->battleActive || team->battleFinished)
        return false;
    memberIndex = vm_mock_service_team_battle_member_index(team, session->clientId);
    if (memberIndex < 0 || memberIndex >= team->battleMemberCount)
        return false;
    memberBit = (u8)(1u << memberIndex);
    if ((team->battleMemberLeftMask & memberBit) != 0)
        return false;
    if (team->battleMemberHp[memberIndex] == 0)
        return false;
    if ((team->battleRoundActedMask & memberBit) != 0)
        return false;
    if (team->battleEnemyHpCurrent == 0 && !team->battleRoundTerminalPending)
        return false;
    return true;
}

static bool vm_net_mock_battle_auto_seat_can_act(void)
{
    /*
     * Team auto reuses the same prefer/poll synth loop as solo, but operate
     * must go through synchronized_team_battle so round_defer owns the barrier.
     * A seat may synth only while it still owes an action this round.
     */
    if (g_mockBattleOperateSessionArmed == 0 ||
        g_mockBattleAwaitingSettlement != 0)
    {
        return false;
    }
    if (vm_net_mock_active_session_in_team_battle())
        return vm_net_mock_battle_auto_team_seat_can_act();
    return g_mockBattlePendingEnemyTurn == 0 &&
           !vm_net_mock_battle_all_enemies_defeated() &&
           g_mockBattleRoleHpCurrent > 0;
}

static u32 vm_net_mock_battle_auto_turn_gap_ms(void)
{
    /*
     * Optional extra pause after playback before the next poll synth.
     * Default 0: no cancel window — synth as soon as actioninfo hold ends.
     * Set CBE_BATTLE_AUTO_TURN_GAP_MS>0 to restore a post-play cancel gap.
     */
    return vm_net_mock_env_u32("CBE_BATTLE_AUTO_TURN_GAP_MS", 0);
}

/* First synth after hangup/challenge start.  Independent of mid-fight turn gap. */
static u32 vm_net_mock_battle_auto_entry_gap_ms(void)
{
    return vm_net_mock_env_u32("CBE_BATTLE_AUTO_ENTRY_GAP_MS", 0);
}

/* Hold long enough for actioninfo (player + death + counters) to finish
 * playing before the next synth.  Optional turn-gap env adds cancel time. */
static u32 vm_net_mock_battle_auto_playback_hold_ms(u8 actionCount)
{
    u32 cancelGapMs = vm_net_mock_battle_auto_turn_gap_ms();
    u32 perActionMs = vm_net_mock_env_u32("CBE_BATTLE_AUTO_ACTION_PLAYBACK_MS", 1400);
    u32 playMs;
    u32 holdMs;

    if (actionCount == 0)
        actionCount = 1;
    playMs = perActionMs * (u32)actionCount;
    if (playMs < 500)
        playMs = 500;
    holdMs = playMs + cancelGapMs;
    return holdMs;
}

/* Convert remaining wall-clock ms into scheduler ticks (at least 1 when ms>0). */
static u32 vm_net_mock_delay_ms_to_ticks(u32 delayMs)
{
    u32 ticks;

    if (delayMs == 0)
        return 0;
    ticks = (delayMs + (u32)VM_SCHED_FRAME_MS - 1u) / (u32)VM_SCHED_FRAME_MS;
    if (ticks == 0)
        ticks = 1;
    return ticks;
}

/*
 * Authoritative auto hold is wall-clock ms.  PendingNotBeforeTick is derived
 * so poll cadence cannot drift ahead of playback / cancel windows.
 */
static void vm_net_mock_battle_auto_set_hold_until_ms(u32 notBeforeMs)
{
    u32 nowMs = scheduler_get_tick_ms();
    u32 remainMs;

    g_mockBattleAutoNextActNotBeforeMs = notBeforeMs;
    if (notBeforeMs <= nowMs)
    {
        g_mockBattleAutoPendingNotBeforeTick = g_schedulerTick;
        return;
    }
    remainMs = notBeforeMs - nowMs;
    g_mockBattleAutoPendingNotBeforeTick =
        g_schedulerTick + vm_net_mock_delay_ms_to_ticks(remainMs);
}

static void vm_net_mock_battle_note_round_playback_hold(u8 actionCount,
                                                        const char *reason)
{
    u32 holdMs;
    u32 nowMs;

    if (actionCount == 0)
        return;
    g_mockBattleLastRoundActionCount = actionCount;
    holdMs = vm_net_mock_battle_auto_playback_hold_ms(actionCount);
    nowMs = scheduler_get_tick_ms();
    vm_net_mock_battle_auto_set_hold_until_ms(nowMs + holdMs);
    if (g_mockBattleAutoPrefer != 0 &&
        g_mockBattleOperateSessionArmed != 0 &&
        g_mockBattleAwaitingSettlement == 0 &&
        !vm_net_mock_battle_all_enemies_defeated() &&
        g_mockBattleRoleHpCurrent > 0)
    {
        g_mockBattleAutoPendingArmed = 1;
    }
    printf("[info][network] mock_battle_playback_hold reason=%s actions=%u "
           "hold_ms=%u not_before_ms=%u now_ms=%u prefer=%u pending=%u "
           "evidence=actioninfo-playback-before-next-synth\n",
           reason ? reason : "-",
           actionCount,
           holdMs,
           g_mockBattleAutoNextActNotBeforeMs,
           nowMs,
           g_mockBattleAutoPrefer ? 1 : 0,
           g_mockBattleAutoPendingArmed ? 1 : 0);
}

static bool vm_net_mock_battle_auto_in_turn_gap(void)
{
    u32 nowMs;

    /* Prefer may be off when a manual 4/2 just armed the hold; auto11 must
     * still see it so enabling auto mid-playback cannot stomp counters. */
    if (g_mockBattleAutoNextActNotBeforeMs == 0)
        return false;
    nowMs = scheduler_get_tick_ms();
    return nowMs < g_mockBattleAutoNextActNotBeforeMs;
}

static void vm_net_mock_battle_auto_clear_pending(void)
{
    g_mockBattleAutoPendingArmed = 0;
    g_mockBattleAutoPendingNotBeforeTick = 0;
    g_mockBattleAutoNextActNotBeforeMs = 0;
    g_mockBattleAutoFlagPendingArmed = 0;
    g_mockBattleAutoFlagPendingNotBeforeMs = 0;
    /* Keep HangupStyleFlagOk while prefer stays on — victory synth / settle
     * clear_pending must not force the next 4/12 onto type=0 (that reopens
     * the manual operate menu over the settle panel). */
    if (g_mockBattleAutoPrefer == 0)
        g_mockBattleAutoHangupStyleFlagOk = 0;
    g_mockBattleAutoClientDriven = 0;
}

static u32 vm_net_mock_hangup_loop_interval_ms(void)
{
    /* Default 2s pure map-side wait after settlement exit (tune via env). */
    u32 intervalMs = vm_net_mock_env_u32("CBE_HANGUP_LOOP_INTERVAL_MS", 2000);
    if (intervalMs < 1500)
        intervalMs = 1500;
    return intervalMs;
}

static void vm_net_mock_hangup_loop_clear(const char *reason)
{
    if (g_mockHangupLoopActive == 0 &&
        g_mockHangupLoopScheduleAfterExit == 0 &&
        g_mockHangupLoopPendingArmed == 0 &&
        g_mockHangupStartPendingArmed == 0 &&
        g_mockHangupStopAfterBattle == 0)
        return;
    printf("[info][network] mock_hangup_loop_clear reason=%s was_active=%u "
           "after_exit=%u pending=%u start_pending=%u stop_after=%u\n",
           reason ? reason : "-",
           g_mockHangupLoopActive ? 1 : 0,
           g_mockHangupLoopScheduleAfterExit ? 1 : 0,
           g_mockHangupLoopPendingArmed ? 1 : 0,
           g_mockHangupStartPendingArmed ? 1 : 0,
           g_mockHangupStopAfterBattle ? 1 : 0);
    g_mockHangupLoopActive = 0;
    g_mockHangupLoopScheduleAfterExit = 0;
    g_mockHangupLoopPendingArmed = 0;
    g_mockHangupLoopNotBeforeMs = 0;
    g_mockHangupStartPendingArmed = 0;
    g_mockHangupStartNotBeforeMs = 0;
    g_mockHangupStopAfterBattle = 0;
}

static u32 vm_net_mock_hangup_start_delay_ms(void)
{
    /* Default 0: first hangup tap starts the fight immediately.
     * Set CBE_HANGUP_START_DELAY_MS>0 to restore map-side wait + cue chat. */
    return vm_net_mock_env_u32("CBE_HANGUP_START_DELAY_MS", 0);
}

/* System chat (1/3/3 type=5) so the player sees hangup start/stop without
 * relying on button caption.  Prefer enqueue (scene poll) + optional inline. */
static void vm_net_mock_hangup_notify_system_chat(const char *messageGbk)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (messageGbk == NULL || messageGbk[0] == '\0')
        return;
    if (session != NULL &&
        vm_mock_service_session_enqueue_system_message(session, messageGbk))
    {
        printf("[info][network] mock_hangup_system_chat queued=1 text_bytes=%u "
               "evidence=1/3/3-type5\n",
               (u32)strlen(messageGbk));
    }
}

static bool vm_net_mock_hangup_append_system_chat_object(u8 *out, u32 outCap,
                                                         u32 *pos,
                                                         u8 *objectCount,
                                                         const char *messageGbk)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */

    if (out == NULL || pos == NULL || objectCount == NULL ||
        messageGbk == NULL || messageGbk[0] == '\0')
        return false;
    if (!vm_net_mock_append_chat_message_object(out, outCap, pos,
                                                VM_MOCK_CHAT_TYPE_SYSTEM, 0,
                                                systemNameGbk, messageGbk))
        return false;
    ++(*objectCount);
    return true;
}

static u32 vm_net_mock_battle_settlement_exit_delay_ms(void)
{
    /* Settle-panel read window before delayed 4/8 tear-down.
     * Default 2500 matches multi-monster settle docs; 100 made the content
     * panel almost invisible before the sticky empty 4/8 shell. */
    u32 delayMs = vm_net_mock_env_u32("CBE_BATTLE_SETTLEMENT_EXIT_DELAY_MS", 2500);
    if (delayMs < 100)
        delayMs = 100;
    return delayMs;
}

static u32 vm_net_mock_battle_encounter_cooldown_ms(void)
{
    /* Disabled by default: cooldown reject used unsolicited 25/11 and left a
     * sticky 斗 icon that blocked further encounters.  Keep env hook at 0. */
    return vm_net_mock_env_u32("CBE_BATTLE_ENCOUNTER_COOLDOWN_MS", 0);
}

static void vm_net_mock_battle_arm_encounter_cooldown(const char *reason)
{
    (void)reason;
    /* Restriction cancelled: never gate challenge 4/1.  Still arm a one-shot
     * 25/12 so a leftover 斗/banner from older builds can clear on poll/start. */
    g_mockBattleEncounterNotBeforeMs = 0;
    g_mockBattleEncounterCooldownClearPending = 1;
}

static bool vm_net_mock_battle_encounter_cooldown_active(u32 *remainMsOut)
{
    if (remainMsOut != NULL)
        *remainMsOut = 0;
    g_mockBattleEncounterNotBeforeMs = 0;
    return false;
}

static u32 vm_net_mock_build_pending_encounter_cooldown_clear_response(u8 *out,
                                                                       u32 outCap)
{
    u32 pos = 5;

    if (out == NULL || outCap < pos)
        return 0;
    if (g_mockBattleEncounterCooldownClearPending == 0)
        return 0;
    if (g_mockBattleOperateSessionArmed != 0 ||
        g_mockBattleAwaitingSettlement != 0 ||
        g_mockBattleSettlementExitPending != 0)
    {
        return 0;
    }
    /* One-shot recovery clear for sessions that already got stuck 斗 from the
     * former cooldown 25/11 reject path. */
    if (!vm_net_mock_append_info_banner_clear12_object(out, outCap, &pos))
        return 0;
    g_mockBattleEncounterNotBeforeMs = 0;
    g_mockBattleEncounterCooldownClearPending = 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_battle_encounter_cooldown_clear "
           "response=25/12 resp=%u evidence=cooldown-disabled-recovery\n",
           pos);
    return pos;
}

static void vm_net_mock_battle_settlement_exit_clear(const char *reason)
{
    if (g_mockBattleSettlementExitPending == 0 &&
        g_mockBattleSettlementExitNotBeforeMs == 0)
        return;
    printf("[info][network] mock_battle_settlement_exit_clear reason=%s "
           "was_pending=%u\n",
           reason ? reason : "-",
           g_mockBattleSettlementExitPending ? 1 : 0);
    g_mockBattleSettlementExitPending = 0;
    g_mockBattleSettlementExitNotBeforeMs = 0;
}

static void vm_net_mock_battle_post_exit_settle_clear(const char *reason)
{
    if (g_mockBattlePostExitSettlePending == 0 &&
        g_mockBattlePostExitSettleNotBeforeMs == 0)
        return;
    printf("[info][network] mock_battle_post_exit_settle_clear reason=%s "
           "was_pending=%u\n",
           reason ? reason : "-",
           g_mockBattlePostExitSettlePending ? 1 : 0);
    g_mockBattlePostExitSettlePending = 0;
    g_mockBattlePostExitSettleNotBeforeMs = 0;
}

static void vm_net_mock_battle_settlement_exit_arm(const char *reason)
{
    u32 panelMs = vm_net_mock_battle_settlement_exit_delay_ms();
    u32 nowMs = scheduler_get_tick_ms();
    u32 playMs = 0;
    u32 totalMs;

    /*
     * Inline 4/6+4/7: client plays actioninfo first, then paints the result
     * panel.  Exit must wait for BOTH.
     */
    if (g_mockBattleAutoNextActNotBeforeMs > nowMs)
        playMs = g_mockBattleAutoNextActNotBeforeMs - nowMs;
    else if (g_mockBattleLastRoundActionCount != 0)
        playMs = vm_net_mock_battle_auto_playback_hold_ms(
            g_mockBattleLastRoundActionCount);
    totalMs = playMs + panelMs;
    g_mockBattleSettlementExitPending = 1;
    g_mockBattleSettlementExitNotBeforeMs = nowMs + totalMs;
    vm_net_mock_battle_post_exit_settle_clear("exit-rearm");
    printf("[info][network] mock_battle_settlement_exit_arm reason=%s "
           "play_ms=%u panel_ms=%u delay_ms=%u not_before_ms=%u serial=%u "
           "prefer=%u actions_last=%u evidence=4/8-after-playback+4/7-panel\n",
           reason ? reason : "-",
           playMs,
           panelMs,
           totalMs,
           g_mockBattleSettlementExitNotBeforeMs,
           g_mockBattleOperateSessionSerial,
           g_mockBattleAutoPrefer ? 1 : 0,
           g_mockBattleLastRoundActionCount);
}

static void vm_net_mock_battle_note_victory_settlement(const char *reason)
{
    g_mockBattleAwaitingSettlement = 1;
    /* Stop further auto synth while the settle panel is up; keep prefer so
     * hangup can re-enter after exit.  Do not wipe HangupStyleFlagOk (see
     * clear_pending) so a late 4/12 cannot reopen the operate menu. */
    g_mockBattleAutoPendingArmed = 0;
    g_mockBattleAutoPendingNotBeforeTick = 0;
    g_mockBattleAutoFlagPendingArmed = 0;
    g_mockBattleAutoFlagPendingNotBeforeMs = 0;
    vm_net_mock_battle_settlement_exit_arm(reason);
}

static void vm_net_mock_hangup_loop_schedule_next(const char *reason)
{
    u32 intervalMs;
    u32 nowMs;

    if (g_mockHangupStopAfterBattle != 0)
    {
        /* GBK: 已停止挂机 */
        static const char stopHangupGbk[] =
            "\xd2\xd1\xcd\xa3\xd6\xb9\xb9\xd2\xbb\xfa";
        g_mockBattleAutoPrefer = 0;
        g_mockBattleAutoSuppressNext12 = 0;
        vm_net_mock_battle_auto_clear_pending();
        vm_net_mock_hangup_loop_clear(reason ? reason : "stop-after-battle");
        vm_net_mock_hangup_notify_system_chat(stopHangupGbk);
        printf("[info][network] mock_hangup_loop_schedule reason=%s "
               "action=stop-after-complete evidence=hangup-button-stop-after\n",
               reason ? reason : "-");
        vm_autotest_note("mock_hangup_loop_schedule action=stop-after-complete\n");
        return;
    }
    if (g_mockHangupLoopActive == 0 || g_mockBattleAutoPrefer == 0)
    {
        g_mockHangupLoopScheduleAfterExit = 0;
        return;
    }
    if (g_mockBattleRoleHpCurrent == 0)
    {
        vm_net_mock_hangup_loop_clear("schedule-dead");
        return;
    }
    intervalMs = vm_net_mock_hangup_loop_interval_ms();
    nowMs = scheduler_get_tick_ms();
    g_mockHangupLoopScheduleAfterExit = 0;
    g_mockHangupLoopPendingArmed = 1;
    g_mockHangupLoopNotBeforeMs = nowMs + intervalMs;
    printf("[info][network] mock_hangup_loop_schedule reason=%s interval_ms=%u "
           "not_before_ms=%u prefer=1 evidence=hangup-loop-after-exit\n",
           reason ? reason : "-",
           intervalMs,
           g_mockHangupLoopNotBeforeMs);
    vm_autotest_note("mock_hangup_loop_schedule reason=%s interval_ms=%u\n",
                     reason ? reason : "-", intervalMs);
}

/*
 * Victory only marks intent.  Map-side interval starts when delayed
 * settlement exit lands so panel time does not eat into the hangup gap.
 */
static void vm_net_mock_hangup_loop_note_victory_reentry(const char *reason)
{
    if (g_mockHangupLoopActive == 0 || g_mockBattleAutoPrefer == 0)
        return;
    if (g_mockBattleRoleHpCurrent == 0)
    {
        vm_net_mock_hangup_loop_clear("victory-dead");
        return;
    }
    g_mockHangupLoopScheduleAfterExit = 1;
    g_mockHangupLoopPendingArmed = 0;
    g_mockHangupLoopNotBeforeMs = 0;
    printf("[info][network] mock_hangup_loop_note_victory reason=%s prefer=1 "
           "after_exit=1 evidence=hangup-loop-after-exit\n",
           reason ? reason : "-");
    vm_autotest_note("mock_hangup_loop_note_victory reason=%s after_exit=1\n",
                     reason ? reason : "-");
}

static u32 vm_net_mock_build_battle_settlement_exit_packet(u8 *out, u32 outCap,
                                                           const char *phase)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u8 autoType = 0;
    /*
     * Hangup re-enters via hangup start (clears Battle without 4/8) — skip the
     * exit packet and keep AwaitingSettlement so map vitals cannot land on
     * settle UI.  Non-hangup must use authentic tear-down 4/8+4/11+4/9
     * (mmBattle:0x7DF6).  Do NOT substitute 4/4: HandleServerBattleCmd case 4
     * is escape-success and paints「逃跑成功」(2026-07-28 user repro after
     * trial escape-exit).
     */
    if (out == NULL || outCap < pos)
        return 0;
    /*
     * Hangup re-enter: never push 4/11 type=0 while the 4/7 settle panel is
     * still the live Battle.cbm UI — type=0 can block settle clicks even
     * without a visible operate menu (2026-07-30).  Stay on settle/[挂机中]
     * until hangup start prepends authentic 4/8 tear-down.
     *
     * Require a real hangup-loop bit.  Do not key off HangupStyleFlagOk alone:
     * mid-button prefer can leave FlagOk set and empty-skip forever (stuck).
     */
    if (g_mockBattleAutoPrefer != 0 &&
        g_mockHangupStopAfterBattle == 0 &&
        (g_mockHangupLoopScheduleAfterExit != 0 ||
         g_mockHangupLoopActive != 0 ||
         g_mockHangupLoopPendingArmed != 0 ||
         g_mockHangupStartPendingArmed != 0))
    {
        g_mockBattleAwaitingSettlement = 1;
        vm_net_mock_battle_settlement_exit_clear(phase ? phase : "hangup-skip-4/8");
        vm_net_mock_battle_post_exit_settle_clear(phase ? phase : "hangup-skip-4/8");
        if (g_mockHangupLoopPendingArmed == 0 &&
            g_mockHangupStartPendingArmed == 0)
        {
            vm_net_mock_hangup_loop_schedule_next(phase ? phase : "hangup-skip-4/8");
        }
        /* Empty WT: safe for operate-followup 4/2; poll may also push this. */
        vm_net_mock_finish_wt_packet(out, pos, 0);
        printf("[info][network] mock_battle_settlement_exit phase=%s auto=0 "
               "prefer_kept=1 objects=0 resp=%u response=empty "
               "evidence=skip-type0-menu-on-settle-hangup-reenter\n",
               phase ? phase : "-",
               pos);
        vm_autotest_note("mock_battle_settlement_exit phase=%s response=empty "
                         "evidence=skip-type0-on-settle-hangup\n",
                         phase ? phase : "-");
        return pos;
    }
    if (g_mockHangupStopAfterBattle != 0 &&
        g_mockHangupLoopScheduleAfterExit != 0)
    {
        /*
         * Stop-after cannot skip 4/8: there will be no next hangup start to
         * clear Battle.cbm.  Tear down with 4/8 then clear hangup below.
         */
        g_mockHangupLoopScheduleAfterExit = 0;
    }
    /*
     * Non-hangup must tear down with authentic 4/8+4/11 type0+4/9
     * (mmBattle:0x7DF6).  A 2026-07-30 trial defaulted SKIP_SUBTYPE8=1 to
     * avoid a post-exit empty click prompt from 4/8 UpdateCharAttrs; manual
     * PvE then stuck on the 4/7 settle panel with no operable clicks and no
     * visible operate menu (Battle.cbm never cleared).  Default is therefore
     * full tear-down; set CBE_BATTLE_EXIT_SKIP_SUBTYPE8=1 only to retry the
     * empty-prompt experiment.
     */
    if (vm_net_mock_env_u8("CBE_BATTLE_EXIT_SKIP_SUBTYPE8", 0) != 0)
    {
        if (!vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos,
                                                               autoType))
            return 0;
        ++objectCount;
        if (!vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
            return 0;
        ++objectCount;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        g_mockBattleAwaitingSettlement = 0;
        vm_net_mock_battle_settlement_exit_clear(phase ? phase : "exit-no-4/8");
        vm_net_mock_battle_post_exit_settle_clear(phase ? phase : "exit-no-4/8");
        vm_net_mock_battle_arm_encounter_cooldown(phase ? phase : "exit-no-4/8");
        g_mockBattlePostExitSuppressSceneDefaultUntilMs =
            scheduler_get_tick_ms() + 3000u;
        if (g_mockHangupStopAfterBattle != 0)
        {
            static const char stopHangupGbk[] =
                "\xd2\xd1\xcd\xa3\xd6\xb9\xb9\xd2\xbb\xfa";
            g_mockBattleAutoPrefer = 0;
            g_mockBattleAutoSuppressNext12 = 0;
            vm_net_mock_battle_auto_clear_pending();
            vm_net_mock_hangup_loop_clear(phase ? phase : "stop-after-exit");
            vm_net_mock_hangup_notify_system_chat(stopHangupGbk);
        }
        printf("[info][network] mock_battle_settlement_exit phase=%s auto=%u "
               "prefer_kept=%u objects=%u resp=%u response=4/11+4/9 "
               "evidence=skip-4/8-empty-click-prompt-optin\n",
               phase ? phase : "-",
               autoType,
               g_mockBattleAutoPrefer ? 1 : 0,
               objectCount,
               pos);
        vm_autotest_note("mock_battle_settlement_exit phase=%s "
                         "response=4/11+4/9 evidence=skip-4/8-optin\n",
                         phase ? phase : "-");
        return pos;
    }
    if (!vm_net_mock_append_battle_terminal_subtype8_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos,
                                                           autoType))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_battle_terminal_case9_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_mockBattleAwaitingSettlement = 0;
    vm_net_mock_battle_settlement_exit_clear(phase ? phase : "exit-delivered");
    vm_net_mock_battle_post_exit_settle_clear(phase ? phase : "exit-delivered");
    vm_net_mock_battle_arm_encounter_cooldown(phase ? phase : "exit-delivered");
    g_mockBattlePostExitSuppressSceneDefaultUntilMs =
        scheduler_get_tick_ms() + 3000u;
    if (g_mockHangupStopAfterBattle != 0)
    {
        /* GBK: 已停止挂机 — stop-after used real 4/8; clear loop now. */
        static const char stopHangupGbk[] =
            "\xd2\xd1\xcd\xa3\xd6\xb9\xb9\xd2\xbb\xfa";
        g_mockBattleAutoPrefer = 0;
        g_mockBattleAutoSuppressNext12 = 0;
        vm_net_mock_battle_auto_clear_pending();
        vm_net_mock_hangup_loop_clear(phase ? phase : "stop-after-exit");
        vm_net_mock_hangup_notify_system_chat(stopHangupGbk);
    }
    printf("[info][network] mock_battle_settlement_exit phase=%s auto=%u "
           "prefer_kept=%u objects=%u resp=%u response=4/8+4/11+4/9 "
           "evidence=mmBattle:0x7DF6-tear-down-not-escape-4/4\n",
           phase ? phase : "-",
           autoType,
           g_mockBattleAutoPrefer ? 1 : 0,
           objectCount,
           pos);
    vm_autotest_note("mock_battle_settlement_exit phase=%s auto=%u prefer_kept=%u "
                     "response=4/8+4/11+4/9\n",
                     phase ? phase : "-",
                     autoType,
                     g_mockBattleAutoPrefer ? 1 : 0);
    return pos;
}

static u32 vm_net_mock_build_battle_post_exit_settle_packet(u8 *out, u32 outCap,
                                                            const char *phase)
{
    /* Path rejected: 4/7 after 4/8 is not a valid map settle contract. */
    (void)out;
    (void)outCap;
    (void)phase;
    vm_net_mock_battle_post_exit_settle_clear("disabled");
    return 0;
}

static u32 vm_net_mock_build_pending_battle_settlement_exit_response(u8 *out,
                                                                     u32 outCap)
{
    u32 nowMs;

    if (out == NULL || outCap < 5)
        return 0;
    if (g_mockBattleSettlementExitPending == 0 ||
        g_mockBattleAwaitingSettlement == 0)
    {
        return 0;
    }
    if (g_mockBattleOperateSessionArmed != 0)
        return 0;
    nowMs = scheduler_get_tick_ms();
    if (nowMs < g_mockBattleSettlementExitNotBeforeMs)
        return 0;
    return vm_net_mock_build_battle_settlement_exit_packet(
        out, outCap, "poll-delayed");
}

static u32 vm_net_mock_build_pending_battle_post_exit_settle_response(u8 *out,
                                                                      u32 outCap)
{
    (void)out;
    (void)outCap;
    if (g_mockBattlePostExitSettlePending != 0)
        vm_net_mock_battle_post_exit_settle_clear("disabled-poll");
    return 0;
}

static u32 vm_net_mock_build_pending_hangup_start_delay_response(u8 *out, u32 outCap)
{
    u32 nowMs;
    u32 len = 0;

    if (out == NULL || outCap < 5)
        return 0;
    if (g_mockHangupLoopActive == 0 ||
        g_mockHangupStartPendingArmed == 0 ||
        g_mockHangupStopAfterBattle != 0)
        return 0;
    if (vm_net_mock_active_session_in_team_battle())
        return 0;
    if (g_mockBattleOperateSessionArmed != 0)
        return 0;
    if (g_mockBattleSettlementExitPending != 0 ||
        g_mockBattlePostExitSettlePending != 0)
        return 0;
    /*
     * Map-side start-delay runs before any hangup fight seeds
     * g_mockBattleRoleHpCurrent.  Battle HP stays 0 after login/map-stone and
     * falsely cleared the armed delay (桃花岛 2026-07-28: start-delay →
     * start-delay-dead same second).  Gate on durable role HP instead.
     */
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();

        if (role == NULL || role->hp == 0)
        {
            vm_net_mock_hangup_loop_clear("start-delay-dead");
            return 0;
        }
    }
    nowMs = scheduler_get_tick_ms();
    if (nowMs < g_mockHangupStartNotBeforeMs)
        return 0;

    g_mockHangupStartPendingArmed = 0;
    g_mockHangupStartNotBeforeMs = 0;
    len = vm_net_mock_build_hangup_battle_start_response(NULL, 0, out, outCap);
    if (len == 0)
    {
        g_mockHangupStartPendingArmed = 1;
        g_mockHangupStartNotBeforeMs = nowMs + 1000;
        printf("[warn][network] mock_hangup_start_delay_deliver failed "
               "resp=0 retry_ms=1000\n");
        return 0;
    }
    {
        /* GBK: 已开始挂机 */
        static const char startHangupGbk[] =
            "\xd2\xd1\xbf\xaa\xca\xbc\xb9\xd2\xbb\xfa";
        vm_net_mock_hangup_notify_system_chat(startHangupGbk);
    }
    printf("[info][network] mock_hangup_start_delay_deliver resp=%u "
           "evidence=button-delay-then-hangup-start\n",
           len);
    vm_autotest_note("mock_hangup_start_delay_deliver resp=%u\n", len);
    return len;
}

static u32 vm_net_mock_build_pending_hangup_loop_battle_response(u8 *out, u32 outCap)
{
    u32 nowMs;
    u32 len = 0;

    if (out == NULL || outCap < 5)
        return 0;
    if (g_mockHangupLoopActive == 0 ||
        g_mockHangupLoopPendingArmed == 0 ||
        g_mockBattleAutoPrefer == 0)
    {
        return 0;
    }
    if (vm_net_mock_active_session_in_team_battle())
        return 0;
    if (g_mockBattleOperateSessionArmed != 0)
        return 0;
    /*
     * Hold while delayed exit is still armed, or while hangup re-entry has not
     * been scheduled yet.  After hangup skip-4/8, AwaitingSettlement stays set
     * (blocks map vitals) but PendingArmed must still be allowed to deliver the
     * next hangup start — that start clears Battle.cbm without a blank 4/8.
     */
    if (g_mockBattleSettlementExitPending != 0 ||
        g_mockBattlePostExitSettlePending != 0 ||
        g_mockHangupLoopScheduleAfterExit != 0)
        return 0;
    if (g_mockBattleAwaitingSettlement != 0 &&
        g_mockHangupLoopPendingArmed == 0)
        return 0;
    if (g_mockBattleRoleHpCurrent == 0)
    {
        vm_net_mock_hangup_loop_clear("poll-dead");
        return 0;
    }
    nowMs = scheduler_get_tick_ms();
    if (nowMs < g_mockHangupLoopNotBeforeMs)
        return 0;

    g_mockHangupLoopPendingArmed = 0;
    len = vm_net_mock_build_hangup_battle_start_response(NULL, 0, out, outCap);
    if (len == 0)
    {
        /* Keep the loop armed and retry on the next poll cadence. */
        g_mockHangupLoopPendingArmed = 1;
        g_mockHangupLoopNotBeforeMs = nowMs + 1000;
        printf("[warn][network] mock_hangup_loop_poll_deliver failed resp=0 "
               "retry_ms=1000 prefer=1\n");
        return 0;
    }
    printf("[info][network] mock_hangup_loop_poll_deliver resp=%u prefer=1 "
           "evidence=hangup-loop-after-exit\n",
           len);
    vm_autotest_note("mock_hangup_loop_poll_deliver response=hangup-start resp=%u\n",
                     len);
    return len;
}

static u32 vm_net_mock_battle_auto_flag_delay_ms(void)
{
    /* Short delay so the mid-button 4/12 race finishes before hangup-style
     * unsolicited 4/11 type=1 (same contract as challenge/hangup start). */
    u32 delayTicks = vm_net_mock_env_u32("CBE_BATTLE_AUTO_FLAG_DELAY_TICKS", 8);
    u32 delayMs;

    if (delayTicks == 0)
        delayTicks = 1;
    delayMs = delayTicks * (u32)VM_SCHED_FRAME_MS;
    if (delayMs < (u32)VM_SCHED_FRAME_MS)
        delayMs = (u32)VM_SCHED_FRAME_MS;
    return delayMs;
}

static void vm_net_mock_battle_auto_arm_pending_ex(const char *reason,
                                                    bool startCancelWindow)
{
    /* Cancel window is pure turn-gap.  Playback time is owned by
     * note_round_playback_hold (play + gap). */
    u32 gapMs = vm_net_mock_battle_auto_turn_gap_ms();
    u32 nowMs = scheduler_get_tick_ms();
    bool inTeam = vm_net_mock_active_session_in_team_battle();

    if (inTeam)
        vm_net_mock_battle_auto_pull_team_vitals();
    if (g_mockBattleAutoPrefer == 0 ||
        g_mockBattleOperateSessionArmed == 0 ||
        g_mockBattleAwaitingSettlement != 0 ||
        vm_net_mock_battle_all_enemies_defeated() ||
        g_mockBattleRoleHpCurrent == 0)
    {
        g_mockBattleAutoPendingArmed = 0;
        g_mockBattleAutoPendingNotBeforeTick = 0;
        return;
    }

    /*
     * Do not reset an already-running playback / cancel window.  prefer-poll
     * rearm used cancel_window=0 and wiped NextActNotBeforeMs while pending
     * was briefly 0, letting the next synth stomp still-playing counters.
     */
    if (g_mockBattleAutoNextActNotBeforeMs != 0 &&
        nowMs < g_mockBattleAutoNextActNotBeforeMs)
    {
        if (!startCancelWindow)
        {
            g_mockBattleAutoPendingArmed = 1;
            return;
        }
        if (g_mockBattleAutoPendingArmed != 0)
            return;
    }

    g_mockBattleAutoPendingArmed = 1;
    if (startCancelWindow && gapMs != 0)
        vm_net_mock_battle_auto_set_hold_until_ms(nowMs + gapMs);
    else
    {
        /*
         * No cancel gap (default): poll may synth on the next tick once any
         * playback hold already set by note_round_playback_hold has expired.
         */
        if (startCancelWindow ||
            g_mockBattleAutoNextActNotBeforeMs == 0 ||
            nowMs >= g_mockBattleAutoNextActNotBeforeMs)
            vm_net_mock_battle_auto_set_hold_until_ms(0);
    }
    printf("[info][network] mock_battle_auto_arm_pending reason=%s gap_ms=%u "
           "not_before_ms=%u now_ms=%u cancel_window=%u prefer=1 "
           "enemyhp=%u rolehp=%u team=%u evidence=cancel-window-ms-authority\n",
           reason ? reason : "-",
           gapMs,
           g_mockBattleAutoNextActNotBeforeMs,
           nowMs,
           (startCancelWindow && gapMs != 0) ? 1 : 0,
           g_mockBattleEnemyHpCurrent,
           g_mockBattleRoleHpCurrent,
           inTeam ? 1 : 0);
}

static void vm_net_mock_battle_auto_arm_pending(const char *reason)
{
    vm_net_mock_battle_auto_arm_pending_ex(reason, false);
}

/* Hangup / challenge prefer start: arm synth after entry gap (default 500ms). */
static void vm_net_mock_battle_auto_arm_pending_at_start(const char *reason)
{
    u32 entryMs;
    u32 nowMs;

    vm_net_mock_battle_auto_arm_pending_ex(reason, false);
    if (g_mockBattleAutoPendingArmed == 0)
        return;
    entryMs = vm_net_mock_battle_auto_entry_gap_ms();
    if (entryMs == 0)
        return;
    nowMs = scheduler_get_tick_ms();
    vm_net_mock_battle_auto_set_hold_until_ms(nowMs + entryMs);
    printf("[info][network] mock_battle_auto_entry_gap reason=%s entry_ms=%u "
           "not_before_ms=%u now_ms=%u evidence=hangup-start-entry-gap\n",
           reason ? reason : "-",
           entryMs,
           g_mockBattleAutoNextActNotBeforeMs,
           nowMs);
    vm_autotest_note("mock_battle_auto_entry_gap reason=%s entry_ms=%u\n",
                     reason ? reason : "-", entryMs);
}

static void vm_net_mock_battle_auto_arm_pending_after_act(const char *reason)
{
    u32 preservedHoldMs = g_mockBattleAutoNextActNotBeforeMs;

    /* Restart cancel window from now, but never shorten an active playback
     * hold already set by note_round_playback_hold. */
    g_mockBattleAutoPendingArmed = 0;
    g_mockBattleAutoPendingNotBeforeTick = 0;
    vm_net_mock_battle_auto_arm_pending_ex(reason, true);
    if (preservedHoldMs > g_mockBattleAutoNextActNotBeforeMs)
        vm_net_mock_battle_auto_set_hold_until_ms(preservedHoldMs);
}

static void vm_net_mock_battle_auto_arm_flag_pending(const char *reason)
{
    u32 delayMs = vm_net_mock_battle_auto_flag_delay_ms();
    u32 nowMs = scheduler_get_tick_ms();

    if (vm_net_mock_active_session_in_team_battle())
        vm_net_mock_battle_auto_pull_team_vitals();
    if (g_mockBattleAutoPrefer == 0 ||
        g_mockBattleOperateSessionArmed == 0 ||
        g_mockBattleAwaitingSettlement != 0 ||
        vm_net_mock_battle_all_enemies_defeated() ||
        g_mockBattleRoleHpCurrent == 0)
    {
        g_mockBattleAutoFlagPendingArmed = 0;
        g_mockBattleAutoFlagPendingNotBeforeMs = 0;
        return;
    }
    /* Hangup/challenge start already sent type=1; mid-playback redelivery
     * restarts the skill cast visual.  Mid-button auto11 still needs one. */
    if (g_mockBattleAutoHangupStyleFlagOk != 0)
    {
        g_mockBattleAutoFlagPendingArmed = 0;
        g_mockBattleAutoFlagPendingNotBeforeMs = 0;
        printf("[info][network] mock_battle_auto_arm_flag reason=%s action=skip "
               "hangup_style_ok=1 evidence=start-or-poll-already-armed\n",
               reason ? reason : "-");
        return;
    }

    g_mockBattleAutoFlagPendingArmed = 1;
    g_mockBattleAutoFlagPendingNotBeforeMs = nowMs + delayMs;
    printf("[info][network] mock_battle_auto_arm_flag reason=%s delay_ms=%u "
           "not_before_ms=%u now_ms=%u prefer=1 "
           "evidence=mmBattle:0x7cb7 hangup-style-mid-fight\n",
           reason ? reason : "-",
           delayMs,
           g_mockBattleAutoFlagPendingNotBeforeMs,
           nowMs);
}

static void vm_net_mock_battle_auto_note_client_operate(void)
{
    if (g_mockBattleAutoPrefer == 0)
        return;
    /*
     * Real 4/2 still counts as a turn.  Start the 3s cancel window and keep
     * poll armed for the next tick — do not freeze into client_driven (that
     * skipped the gap and denied cancel time).
     */
    g_mockBattleAutoFlagPendingArmed = 0;
    g_mockBattleAutoFlagPendingNotBeforeMs = 0;
    vm_net_mock_battle_auto_arm_pending_after_act("client-4/2-turn-gap");
    printf("[info][network] mock_battle_auto_client_operate prefer=1 gap_ms=%u "
           "evidence=real-4/2-then-cancel-window\n",
           vm_net_mock_battle_auto_turn_gap_ms());
}

/*
 * Mid-battle auto cannot rely on client 4/2: after button 4/11 type=1 the
 * client immediately 4/12-cancels, and type=1 re-ACK only loops 4/12 without
 * ever reaching the auto-fire path (countdown ends with no skill).  When the
 * seat can act, answer with the same operate payload a real 4/2 would
 * produce — never combine it with 4/11 in either order (prefix → cancel;
 * suffix AOE → stall).  Continuous ticks are re-armed and delivered on the
 * next scene-sync poll once not_before elapses.
 */
static u32 vm_net_mock_battle_auto_synth_operate_response(u8 *out, u32 outCap,
                                                          const char *via)
{
    u32 autoIndex = 0;
    u32 autoOperate = 0;
    u8 synthReq[96];
    u32 synthLen = 0;
    u32 operateRespLen = 0;
    bool inTeam = false;

    if (out == NULL || outCap == 0 || !vm_net_mock_battle_auto_seat_can_act())
        return 0;

    inTeam = vm_net_mock_active_session_in_team_battle();
    if (inTeam)
        vm_net_mock_battle_auto_pull_team_vitals();
    autoOperate = vm_net_mock_battle_auto_choose_operate(&autoIndex);
    synthLen = vm_net_mock_build_synth_battle_operate_request(synthReq,
                                                              sizeof(synthReq),
                                                              autoIndex,
                                                              autoOperate);
    if (synthLen == 0)
        return 0;

    /*
     * Always go through the synchronized wrapper: solo falls through to the
     * normal operate builder; team fights keep round_defer / peer deliver.
     * Mark synth so a prefer-driven real-4/2 note is not falsely armed.
     */
    g_mockBattleAutoSynthInProgress = 1;
    operateRespLen = vm_net_mock_build_synchronized_team_battle_response(
        synthReq, synthLen, out, outCap, VM_MOCK_TEAM_BATTLE_BUILD_OPERATE);
    if (operateRespLen == 0)
    {
        operateRespLen = vm_net_mock_build_synchronized_team_battle_response(
            synthReq, synthLen, out, outCap,
            VM_MOCK_TEAM_BATTLE_BUILD_OPERATE_FALLBACK);
    }
    g_mockBattleAutoSynthInProgress = 0;
    if (operateRespLen == 0)
    {
        printf("[warn][network] mock_battle_auto_synth_fail via=%s index=%u "
               "operate=%u synth=%u team=%u\n",
               via ? via : "-",
               autoIndex,
               autoOperate,
               synthLen,
               inTeam ? 1 : 0);
        return 0;
    }

    if (g_mockBattleAutoPrefer != 0 &&
        g_mockBattleOperateSessionArmed != 0 &&
        g_mockBattleAwaitingSettlement == 0 &&
        (inTeam ||
         (!vm_net_mock_battle_all_enemies_defeated() &&
          g_mockBattleRoleHpCurrent > 0)))
    {
        /* Team: re-arm even after round_defer so the seat retries next round. */
        vm_net_mock_battle_auto_arm_pending_after_act(via);
    }
    else
    {
        vm_net_mock_battle_auto_clear_pending();
    }

    printf("[info][network] mock_battle_auto_synth via=%s index=%u operate=%u "
           "last=%u armed=%u enemyhp=%u rolehp=%u prefer=%u pending=%u team=%u "
           "resp=%u evidence=mmBattle:0x2CB5+0x6EB0 shape=operate-only\n",
           via ? via : "-",
           autoIndex,
           autoOperate,
           g_mockBattleLastOperateValid ? 1 : 0,
           g_mockBattleOperateSessionArmed ? 1 : 0,
           g_mockBattleEnemyHpCurrent,
           g_mockBattleRoleHpCurrent,
           g_mockBattleAutoPrefer ? 1 : 0,
           g_mockBattleAutoPendingArmed ? 1 : 0,
           inTeam ? 1 : 0,
           operateRespLen);
    vm_autotest_note("mock_battle_auto_synth via=%s index=%u operate=%u "
                     "pending=%u team=%u response=operate-only "
                     "evidence=mmBattle:0x2CB5+0x6EB0\n",
                     via ? via : "-",
                     autoIndex,
                     autoOperate,
                     g_mockBattleAutoPendingArmed ? 1 : 0,
                     inTeam ? 1 : 0);
    return operateRespLen;
}

/*
 * Unsolicited 4/11 type=1 via scene poll — same object challenge/hangup start
 * appends.  Mid-button ACK cannot leave the client in that state (4/12 race);
 * delivering it after the race settles arms auto for THIS fight.
 */
static u32 vm_net_mock_build_pending_solo_auto_flag_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 nowMs;

    if (g_mockBattleAutoPrefer == 0 ||
        g_mockBattleAutoFlagPendingArmed == 0)
        return 0;
    nowMs = scheduler_get_tick_ms();
    if (nowMs < g_mockBattleAutoFlagPendingNotBeforeMs)
        return 0;
    /*
     * Never inject type=1 while actioninfo is still playing — mid-cast 4/11
     * restarts the skill visual (log: operate skill then flag_poll within hold).
     */
    if (vm_net_mock_battle_auto_in_turn_gap())
    {
        u32 remainMs = 0;

        if (nowMs < g_mockBattleAutoNextActNotBeforeMs)
            remainMs = g_mockBattleAutoNextActNotBeforeMs - nowMs;
        g_mockBattleAutoFlagPendingNotBeforeMs = nowMs + remainMs;
        if (g_mockBattleAutoFlagPendingNotBeforeMs == nowMs)
            g_mockBattleAutoFlagPendingNotBeforeMs = nowMs + (u32)VM_SCHED_FRAME_MS;
        printf("[info][network] mock_battle_auto_flag_poll_defer remain_ms=%u "
               "actions_last=%u evidence=playback-hold-before-type1\n",
               remainMs,
               g_mockBattleLastRoundActionCount);
        return 0;
    }
    if (vm_net_mock_active_session_in_team_battle())
        vm_net_mock_battle_auto_pull_team_vitals();
    if (g_mockBattleOperateSessionArmed == 0 ||
        g_mockBattleAwaitingSettlement != 0 ||
        vm_net_mock_battle_all_enemies_defeated() ||
        g_mockBattleRoleHpCurrent == 0)
    {
        /* Keep flag pending while a shared team fight is still live — globals
         * alone can be stale between account switches. */
        if (!vm_net_mock_active_session_in_team_battle() ||
            !vm_net_mock_battle_auto_team_seat_can_act())
        {
            g_mockBattleAutoFlagPendingArmed = 0;
        }
        return 0;
    }
    if (outCap < pos ||
        !vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, 1))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    g_mockBattleAutoFlagPendingArmed = 0;
    g_mockBattleAutoFlagPendingNotBeforeMs = 0;
    g_mockBattleAutoHangupStyleFlagOk = 1;
    /* Fallback operate ticks if the client still does not emit 4/2. */
    if (g_mockBattleAutoPendingArmed == 0)
        vm_net_mock_battle_auto_arm_pending("after-flag-poll");
    printf("[info][network] mock_battle_auto_flag_poll_deliver resp=%u prefer=1 "
           "enemyhp=%u rolehp=%u "
           "evidence=mmBattle:0x7cb7 hangup-style-mid-fight\n",
           pos,
           g_mockBattleEnemyHpCurrent,
           g_mockBattleRoleHpCurrent);
    vm_autotest_note("mock_battle_auto_flag_poll_deliver response=4/11 "
                     "evidence=mmBattle:0x7cb7 hangup-style-mid-fight\n");
    return pos;
}

/* Scene-sync poll delivery for prefer-driven continuous solo/team auto. */
static u32 vm_net_mock_build_pending_solo_auto_operate_response(u8 *out, u32 outCap)
{
    u32 len = 0;
    bool inTeam = false;

    if (g_mockBattleAutoPrefer == 0)
        return 0;
    inTeam = vm_net_mock_active_session_in_team_battle();
    if (inTeam)
        vm_net_mock_battle_auto_pull_team_vitals();
    /*
     * Account restore intentionally clears pendingArmed (stale not_before).
     * With prefer still set, re-arm so multi-client team polls can synth; solo
     * also relies on this when 4/12 is not looping.
     */
    if (g_mockBattleAutoPendingArmed == 0)
    {
        if (!vm_net_mock_battle_auto_seat_can_act())
            return 0;
        vm_net_mock_battle_auto_arm_pending("prefer-poll-rearm");
        if (g_mockBattleAutoPendingArmed == 0)
            return 0;
    }
    /*
     * Solo: yield to hangup-style flag first.
     * Team: once prefer is on, operate owns the poll — flag ACK is UI-only and
     * must not starve round actions.
     */
    if (!inTeam &&
        g_mockBattleAutoFlagPendingArmed != 0 &&
        scheduler_get_tick_ms() >= g_mockBattleAutoFlagPendingNotBeforeMs)
    {
        return 0;
    }
    /* ms is authoritative; derived tick is only a secondary fence. */
    if (vm_net_mock_battle_auto_in_turn_gap())
        return 0;
    if (g_schedulerTick < g_mockBattleAutoPendingNotBeforeTick)
        return 0;
    if (!vm_net_mock_battle_auto_seat_can_act())
        return 0;

    len = vm_net_mock_battle_auto_synth_operate_response(out, outCap, "auto-poll");
    if (len == 0)
    {
        /* Keep pending armed so a later poll retries after the seat frees. */
        vm_net_mock_battle_auto_set_hold_until_ms(
            scheduler_get_tick_ms() + vm_net_mock_battle_auto_turn_gap_ms());
        g_mockBattleAutoPendingArmed = 1;
        return 0;
    }
    printf("[info][network] mock_battle_auto_poll_deliver resp=%u prefer=1 "
           "enemyhp=%u rolehp=%u team=%u evidence=scene-sync-poll+operate-only\n",
           len,
           g_mockBattleEnemyHpCurrent,
           g_mockBattleRoleHpCurrent,
           inTeam ? 1 : 0);
    return len;
}

/*
 * In-battle auto toggle from Callback_Unknown2(0x2BF1): subtype 11 writes only
 * "type".  HandleServerBattleCmd case 11 (0x7cb7) reads result==1 then type.
 *
 * Runtime matrix:
 *   flag-ack only          → client 4/12; with rearm=type=1 → 4/12 loop, no 4/2
 *   4/11+4/6 same packet   → multi-target cancel
 *   4/6+4/11 same packet   → AOE net-wait stall
 *   operate-only + poll    → first tick now; later ticks via scene-sync poll
 */
static u32 vm_net_mock_build_battle_auto11_flag_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u8 kind = 0;
    u8 subtype = 0;
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 typeValue = 0;
    u8 type = 0;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 synthLen = 0;
    bool inBattle = false;
    bool hangupCancelOk = false;

    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 4 || subtype != 11)
        return 0;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return 0;
    if (offset != requestLen ||
        object.major != 1 ||
        object.kind != 4 ||
        object.subtype != 11 ||
        object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                            "type", &typeValue))
    {
        return 0;
    }
    type = (u8)typeValue;
    inBattle = (g_mockBattleOperateSessionArmed != 0) ||
               vm_net_mock_current_screen_is_battle();
    hangupCancelOk =
        (type == 0) &&
        (g_mockHangupLoopActive != 0 ||
         g_mockHangupLoopScheduleAfterExit != 0 ||
         g_mockHangupLoopPendingArmed != 0 ||
         g_mockHangupStartPendingArmed != 0 ||
         g_mockHangupStopAfterBattle != 0 ||
         g_mockBattleAutoPrefer != 0);
    /*
     * Explicit off (type=0) must work for the whole fight and on the map
     * while hangup is still armed between fights.  Restricting to battle
     * screen left map-side type=0 ignored after settlement_exit prefer_kept.
     */
    if (!inBattle && !hangupCancelOk)
        return 0;

    if (type == 1)
    {
        if (!inBattle)
            return 0;
        g_mockBattleAutoPrefer = 1;

        /*
         * A prior 4/6 may still be playing (player + death + counters).  Synth
         * here stomps that script — log showed counters=2 then immediate auto11
         * operate, which the user reads as "monsters never acted".  Defer to
         * poll after playback hold; only ACK the flag now.
         */
        if (vm_net_mock_battle_auto_in_turn_gap())
        {
            u32 remainMs = 0;
            u32 nowMs = scheduler_get_tick_ms();

            if (nowMs < g_mockBattleAutoNextActNotBeforeMs)
                remainMs = g_mockBattleAutoNextActNotBeforeMs - nowMs;
            g_mockBattleAutoPendingArmed = 1;
            vm_net_mock_battle_auto_arm_flag_pending("auto11-playback-hold");
            printf("[info][network] mock_battle_auto11_flag type=1 action=defer-synth "
                   "remain_ms=%u actions_last=%u armed=%u enemyhp=%u rolehp=%u "
                   "prefer=1 pending=1 evidence=playback-hold-before-synth\n",
                   remainMs,
                   g_mockBattleLastRoundActionCount,
                   g_mockBattleOperateSessionArmed ? 1 : 0,
                   g_mockBattleEnemyHpCurrent,
                   g_mockBattleRoleHpCurrent);
        }
        else
        {
            synthLen = vm_net_mock_battle_auto_synth_operate_response(out, outCap, "auto11");
            if (synthLen != 0)
            {
                /* Button path still posts 4/12 after enable; do not double-cast. */
                g_mockBattleAutoSuppressNext12 = 1;
                /* After 4/12 settles, poll a hangup-style 4/11 so THIS fight
                 * enters the same auto state the next battle-start already had. */
                vm_net_mock_battle_auto_arm_flag_pending("auto11-operate-only");
                printf("[info][network] mock_battle_auto11_flag type=1 last=%u operate=%u "
                       "armed=%u enemyhp=%u rolehp=%u prefer=%u pending=%u flag_pending=%u "
                       "team=%u resp=%u evidence=mmBattle:0x2CB5+0x6EB0 shape=operate-only\n",
                       g_mockBattleLastOperateValid ? 1 : 0,
                       g_mockBattleLastOperateValid ? g_mockBattleLastOperate : 0,
                       g_mockBattleOperateSessionArmed ? 1 : 0,
                       g_mockBattleEnemyHpCurrent,
                       g_mockBattleRoleHpCurrent,
                       g_mockBattleAutoPrefer ? 1 : 0,
                       g_mockBattleAutoPendingArmed ? 1 : 0,
                       g_mockBattleAutoFlagPendingArmed ? 1 : 0,
                       vm_net_mock_active_session_in_team_battle() ? 1 : 0,
                       synthLen);
                vm_autotest_note("mock_battle_auto11_flag type=1 prefer=%u "
                                 "response=operate-only evidence=mmBattle:0x2CB5+0x6EB0\n",
                                 g_mockBattleAutoPrefer ? 1 : 0);
                return synthLen;
            }
            /* Seat busy (solo enemy turn / team already-acted): keep prefer. */
            vm_net_mock_battle_auto_arm_flag_pending("auto11-flag-ack");
            vm_net_mock_battle_auto_arm_pending("auto11-flag-ack");
        }
    }
    else
    {
        /*
         * Explicit off: stop auto synth for the rest of this fight and clear
         * hangup re-entry (本场可打完，结束后不再挂机).  Accepted any time —
         * playback hold, settlement panel, or map while hangup pending.
         */
        bool wasHangupLoop =
            (g_mockHangupLoopActive != 0 ||
             g_mockHangupLoopScheduleAfterExit != 0 ||
             g_mockHangupLoopPendingArmed != 0 ||
             g_mockHangupStartPendingArmed != 0 ||
             g_mockHangupStopAfterBattle != 0);
        bool settlePanelUp =
            (g_mockBattleAwaitingSettlement != 0 ||
             g_mockBattleSettlementExitPending != 0);

        g_mockBattleAutoPrefer = 0;
        g_mockBattleAutoSuppressNext12 = 0;
        vm_net_mock_battle_auto_clear_pending();
        vm_net_mock_hangup_loop_clear(inBattle ? "auto11-type0" : "auto11-type0-map");
        if (wasHangupLoop)
        {
            /* GBK: 已停止挂机 — same cue as second hangup-button tap. */
            static const char stopHangupGbk[] =
                "\xd2\xd1\xcd\xa3\xd6\xb9\xb9\xd2\xbb\xfa";
            (void)vm_net_mock_hangup_append_system_chat_object(
                out, outCap, &pos, &objectCount, stopHangupGbk);
        }
        /*
         * Echoing type=0 while 4/7 is up reopens the operate menu over settle.
         * Prefer/hangup are already cleared; ACK without the type=0 object.
         */
        if (settlePanelUp)
        {
            if (objectCount != 0)
                vm_net_mock_finish_wt_packet(out, pos, objectCount);
            else
                vm_net_mock_finish_wt_packet(out, pos, 0);
            printf("[info][network] mock_battle_auto11_flag type=0 "
                   "action=hold-settle-no-menu prefer=0 hangup=0 "
                   "objects=%u resp=%u evidence=settle-suppress-type0-echo\n",
                   objectCount,
                   pos);
            vm_autotest_note("mock_battle_auto11_flag type=0 "
                             "response=settle-no-menu evidence=settle-suppress-type0\n");
            return pos;
        }
    }

    if (outCap < pos ||
        !vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, type))
        return 0;
    objectCount = (objectCount == 0) ? 1 : (u8)(objectCount + 1);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_battle_auto11_flag type=%u last=%u operate=%u "
           "armed=%u enemyhp=%u rolehp=%u prefer=%u hangup=%u in_battle=%u resp=%u "
           "evidence=mmBattle:HandleServerBattleCmd:0x7cb7 shape=flag-ack\n",
           type,
           g_mockBattleLastOperateValid ? 1 : 0,
           g_mockBattleLastOperateValid ? g_mockBattleLastOperate : 0,
           g_mockBattleOperateSessionArmed ? 1 : 0,
           g_mockBattleEnemyHpCurrent,
           g_mockBattleRoleHpCurrent,
           g_mockBattleAutoPrefer ? 1 : 0,
           g_mockHangupLoopActive ? 1 : 0,
           inBattle ? 1 : 0,
           pos);
    vm_autotest_note("mock_battle_auto11_flag type=%u prefer=%u hangup=%u "
                     "response=4/11 evidence=mmBattle:0x7cb7 shape=flag-ack\n",
                     type,
                     g_mockBattleAutoPrefer ? 1 : 0,
                     g_mockHangupLoopActive ? 1 : 0);
    return pos;
}

static bool vm_net_mock_request_has_battle_auto12(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;

    if (vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) &&
        kind == 4 && subtype == 12)
    {
        return true;
    }
    /* Runtime: map/battle flush may glue 1/2/10 + 1/4/12 in one WT while the
     * header still reads as 2/10 (server_out: len=24 first=1/2/10:10,1/4/12:0). */
    return vm_net_mock_request_contains_object(request, requestLen, 1, 4, 12);
}

static u32 vm_net_mock_build_battle_auto12_cancel_response(const u8 *request, u32 requestLen,
                                                           u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectCount = 0;
    u32 synthLen = 0;

    if (!vm_net_mock_request_has_battle_auto12(request, requestLen))
        return 0;
    if (g_mockBattleOperateSessionArmed == 0 && !vm_net_mock_current_screen_is_battle())
        return 0;

    /*
     * Settlement panel is up (or exit armed).  Never ACK type=0 here — that
     * reopens the skill/item operate menu on top of 4/7 and intermittently
     * freezes click handling until the client is reset.
     */
    if (g_mockBattleAwaitingSettlement != 0 ||
        g_mockBattleSettlementExitPending != 0)
    {
        vm_net_mock_finish_wt_packet(out, pos, 0);
        printf("[info][network] mock_battle_auto12_cancel reply_type=- "
               "action=hold-settle-no-menu awaiting=%u exit_pending=%u "
               "prefer=%u armed=%u evidence=settle-panel-suppress-operate-menu\n",
               g_mockBattleAwaitingSettlement ? 1 : 0,
               g_mockBattleSettlementExitPending ? 1 : 0,
               g_mockBattleAutoPrefer ? 1 : 0,
               g_mockBattleOperateSessionArmed ? 1 : 0);
        return pos;
    }

    /*
     * Negative evidence 2026-07-25: hangup-style type=1 rearm on 4/12 loops
     * without client 4/2.  Never reply type=1 here.
     *
     * While prefer is set, 4/12 is the mid-button toggle race / UI sync — ACK
     * type=0 but KEEP prefer so scene-poll continuous ticks continue.  Explicit
     * off is 4/11 type=0 (clears prefer+pending there).
     * Team fights keep the same prefer loop; synth goes through round_defer.
     */
    if (g_mockBattleAutoPrefer != 0)
    {
        bool inTeam = vm_net_mock_active_session_in_team_battle();

        if (inTeam)
            vm_net_mock_battle_auto_pull_team_vitals();
        if (g_mockBattleAutoSuppressNext12 != 0)
            g_mockBattleAutoSuppressNext12 = 0;
        if (g_mockBattleAutoPendingArmed == 0 &&
            vm_net_mock_battle_auto_seat_can_act())
        {
            synthLen = vm_net_mock_battle_auto_synth_operate_response(out, outCap,
                                                                      "auto12");
            if (synthLen != 0)
            {
                g_mockBattleAutoSuppressNext12 = 1;
                printf("[info][network] mock_battle_auto12_cancel reply_type=- "
                       "rearm=0 prefer=1 pending=%u team=%u resp=%u "
                       "evidence=mmBattle:0x2CB5+0x6EB0 shape=operate-only\n",
                       g_mockBattleAutoPendingArmed ? 1 : 0,
                       inTeam ? 1 : 0,
                       synthLen);
                vm_autotest_note("mock_battle_auto12_cancel rearm=0 "
                                 "response=operate-only evidence=mmBattle:0x2CB5+0x6EB0\n");
                return synthLen;
            }
        }
        /*
         * Solo: re-arm hangup-style 4/11 once after mid-button enable.
         * Hangup/challenge start already sent type=1 (HangupStyleFlagOk); do
         * not re-arm on every 4/12 — mid-playback type=1 restarts skill cast.
         * Team: do NOT re-arm flag on every 4/12 — that starves operate poll
         * (flag delay 8 < operate delay 30) and looks like "auto on, no fight".
         */
        if (!inTeam &&
            g_mockBattleAutoHangupStyleFlagOk == 0 &&
            g_mockBattleAutoFlagPendingArmed == 0)
        {
            vm_net_mock_battle_auto_arm_flag_pending("auto12-keep-prefer");
        }
        if (g_mockBattleAutoPendingArmed == 0)
            vm_net_mock_battle_auto_arm_pending("auto12-keep-prefer");
        /*
         * prefer=1: never ACK 4/12 with type=0.  HandleServerBattleCmd case 11
         * type=0 reopens the manual skill/item menu — on settle that overlays
         * 4/7 and stalls clicks; mid-fight it undoes hangup/auto UI until the
         * next type=1.  Empty ACK keeps the last type=1 auto UI; mid-button
         * still gets hangup-style type=1 from flag_poll when FlagOk is clear.
         */
        if (g_mockBattleAutoHangupStyleFlagOk == 0)
            g_mockBattleAutoHangupStyleFlagOk = 1;
        vm_net_mock_finish_wt_packet(out, pos, 0);
        printf("[info][network] mock_battle_auto12_cancel reply_type=- "
               "rearm=0 prefer=1 hangup_style=1 pending=%u flag_pending=%u "
               "armed=%u enemyhp=%u rolehp=%u team=%u resp=%u "
               "evidence=keep-auto-ui-no-type0\n",
               g_mockBattleAutoPendingArmed ? 1 : 0,
               g_mockBattleAutoFlagPendingArmed ? 1 : 0,
               g_mockBattleOperateSessionArmed ? 1 : 0,
               g_mockBattleEnemyHpCurrent,
               g_mockBattleRoleHpCurrent,
               inTeam ? 1 : 0,
               pos);
        vm_autotest_note("mock_battle_auto12_cancel prefer_kept=1 "
                         "response=empty evidence=keep-auto-ui-no-type0\n");
        return pos;
    }

    g_mockBattleAutoSuppressNext12 = 0;
    vm_net_mock_battle_auto_clear_pending();
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleOperateSessionFinished = 0;
    if (vm_net_mock_battle_all_enemies_defeated())
    {
        g_mockBattleOperateSessionArmed = 0;
        vm_net_mock_battle_note_victory_settlement("auto12-cancel-enemy-dead");
    }

    if (outCap < pos ||
        !vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, 0))
        return 0;
    ++objectCount;

    vm_net_mock_finish_wt_packet(out, pos, (u8)objectCount);
    printf("[info][network] mock_battle_auto12_cancel reply_type=0 rearm=0 prefer=0 "
           "armed=%u enemyhp=%u rolehp=%u resp=%u "
           "evidence=mmBattle:HandleServerBattleCmd:0x7cb7 cancel-ack\n",
           g_mockBattleOperateSessionArmed ? 1 : 0,
           g_mockBattleEnemyHpCurrent,
           g_mockBattleRoleHpCurrent,
           pos);
    vm_autotest_note("mock_battle_auto12_cancel reply_type=0 rearm=0 response=4/11 "
                     "evidence=mmBattle:0x7cb7\n");
    return pos;
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

static const char vm_net_mock_battle_dead_banner_gbk[] =
    "\xc4\xfa\xd2\xd1\xbe\xad\xcb\xc0\xcd\xf6\xa3\xac"
    "\xc7\xeb\xcf\xc8\xca\xb9\xd3\xc3\xb8\xb4\xbb\xee\xca\xaf"; /* GBK: 您已经死亡，请先使用复活石 */

static bool vm_net_mock_append_info_banner_text11_object(u8 *out, u32 outCap,
                                                         u32 *pos,
                                                         const char *info)
{
    u32 objectStart = 0;

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
    u8 battleSide = (u8)vm_net_mock_env_u32("CBE_BATTLE_SIDE",
                                            vm_net_mock_battle_default_side(playerOnRight));
    u8 battleEnemyCount = 1;
    u8 autoFlagType = (u8)vm_net_mock_env_u32("CBE_HANGUP_BATTLE_AUTO_FLAG", 1);
    vm_mock_service_client_session *activeSession =
        vm_mock_service_get_active_client_session();
    bool useSceneMonsterStart = false;
    u8 battleStartSubtype = 10;
    const char *targetSource = "none";
    u32 hangupMoveinfoWireId = 0;

    if (g_mockBattleAutoPrefer != 0)
        autoFlagType = 1;

    if (outCap < pos ||
        (request != NULL && requestLen != 0 &&
         !vm_net_mock_parse_hangup_battle_start_request(request, requestLen,
                                                        &moveUpload,
                                                        &hasMoveUpload)))
        return 0;
    /* Synthetic poll re-entry (request=NULL) mirrors a button hangup without
     * a trailing movement flush. */
    if (request == NULL || requestLen == 0)
    {
        hasMoveUpload = false;
        memset(&moveUpload, 0, sizeof(moveUpload));
    }
    else if (g_mockHangupLoopActive != 0 ||
             g_mockHangupLoopScheduleAfterExit != 0 ||
             g_mockHangupLoopPendingArmed != 0 ||
             g_mockHangupStartPendingArmed != 0 ||
             g_mockHangupStopAfterBattle != 0)
    {
        /*
         * Second hangup tap while armed: stop after this/next fight.
         * Soft-ACK alone leaves HandleBattleEnterReq on「获取数据」
         * (needs 4/5 or 4/10).  Mid-fight: keep current battle + StopAfter.
         * Map / settle / start-countdown: fall through into one last start
         * so the Type=2 enter completes, then StopAfter ends the loop.
         */
        bool inFight = (g_mockBattleOperateSessionArmed != 0 &&
                        g_mockBattleAwaitingSettlement == 0);
        /* GBK: 下一场完成后挂机停止 */
        static const char stopAfterGbk[] =
            "\xcf\xc2\xd2\xbb\xb3\xa1\xcd\xea\xb3\xc9\xba\xf3"
            "\xb9\xd2\xbb\xfa\xcd\xa3\xd6\xb9";

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

        g_mockHangupStopAfterBattle = 1;
        g_mockHangupStartPendingArmed = 0;
        g_mockHangupStartNotBeforeMs = 0;
        g_mockHangupLoopScheduleAfterExit = 0;
        g_mockHangupLoopPendingArmed = 0;
        g_mockHangupLoopNotBeforeMs = 0;
        g_mockHangupLoopActive = 1;
        if (!inFight)
            vm_net_mock_hangup_notify_system_chat(stopAfterGbk);

        if (inFight)
        {
            /*
             * Keep auto for this fight.  Soft 2/10 may briefly show loading;
             * 4/8 stop-after exit must clear it when the fight ends.
             */
            if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
                return 0;
            ++objectCount;
            (void)vm_net_mock_hangup_append_system_chat_object(
                out, outCap, &pos, &objectCount, stopAfterGbk);
            if (moveResponseLen != 0 &&
                !vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount,
                                                     moveResponse, moveResponseLen))
                return 0;
            vm_net_mock_finish_wt_packet(out, pos, objectCount);
            printf("[info][network] mock_hangup_battle_start action=stop-after "
                   "phase=in-fight move_upload=%u objects=%u resp=%u "
                   "response=2/10+1/3/3 evidence=finish-current-then-stop\n",
                   hasMoveUpload ? 1u : 0u,
                   objectCount,
                   pos);
            vm_autotest_note("mock_hangup_battle_start action=stop-after "
                             "phase=in-fight\n");
            return pos;
        }

        printf("[info][network] mock_hangup_battle_start action=stop-after "
               "phase=last-start evidence=fallthrough-hangup-start\n");
        vm_autotest_note("mock_hangup_battle_start action=stop-after "
                         "phase=last-start\n");
        /* Fall through: one last 4/5|4/10 enter clears Type=2 loading. */
    }
    else if (request != NULL && requestLen != 0)
    {
        u32 delayMs = vm_net_mock_hangup_start_delay_ms();

        g_mockHangupLoopActive = 1;
        g_mockHangupStopAfterBattle = 0;
        g_mockHangupStartPendingArmed = 0;
        g_mockHangupStartNotBeforeMs = 0;
        g_mockHangupLoopScheduleAfterExit = 0;
        g_mockHangupLoopPendingArmed = 0;
        g_mockHangupLoopNotBeforeMs = 0;

        if (delayMs != 0)
        {
            /*
             * Optional map-side wait before first fight.  Client already set
             * battle-enter state=3, so「获取数据」shows until poll synth
             * delivers real hangup start.
             */
            u32 nowMs = scheduler_get_tick_ms();
            char startDelayGbk[48];
            u32 delaySec;

            if (delayMs < 1000)
                delayMs = 1000;
            delaySec = (delayMs + 999u) / 1000u;
            if (delaySec > 9u)
                delaySec = 9u;
            /* GBK: N秒后开始挂机 (N = ASCII '1'..'9') */
            startDelayGbk[0] = (char)('0' + (char)delaySec);
            memcpy(startDelayGbk + 1,
                   "\xc3\xeb\xba\xf3\xbf\xaa\xca\xbc\xb9\xd2\xbb\xfa",
                   12);
            startDelayGbk[13] = '\0';

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

            g_mockHangupStartPendingArmed = 1;
            g_mockHangupStartNotBeforeMs = nowMs + delayMs;

            if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
                return 0;
            ++objectCount;
            (void)vm_net_mock_hangup_append_system_chat_object(
                out, outCap, &pos, &objectCount, startDelayGbk);
            if (moveResponseLen != 0 &&
                !vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount,
                                                     moveResponse, moveResponseLen))
                return 0;
            vm_net_mock_finish_wt_packet(out, pos, objectCount);
            printf("[info][network] mock_hangup_battle_start action=start-delay "
                   "delay_ms=%u not_before_ms=%u move_upload=%u objects=%u resp=%u "
                   "response=2/10+1/3/3 evidence=poll-synth-after-delay\n",
                   delayMs,
                   g_mockHangupStartNotBeforeMs,
                   hasMoveUpload ? 1u : 0u,
                   objectCount,
                   pos);
            vm_autotest_note("mock_hangup_battle_start action=start-delay delay_ms=%u\n",
                             delayMs);
            return pos;
        }

        printf("[info][network] mock_hangup_battle_start action=immediate-start "
               "evidence=no-start-delay\n");
        vm_autotest_note("mock_hangup_battle_start action=immediate-start\n");
        /* Fall through: same-packet 4/5|4/10 hangup enter. */
    }

    if (!vm_net_mock_battle_release_settle_for_start(out, outCap, &pos,
                                                     &objectCount,
                                                     "hangup-battle-start",
                                                     true))
    {
        return 0;
    }

    if (hasMoveUpload && moveResponseLen == 0)
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
    if (!vm_net_mock_select_auto_monster_for_scene(scene, &requestedEnemyId, &matchedScene) &&
        !vm_net_mock_select_instance_challenge_enemy_for_scene(
            scene, &requestedEnemyId, &matchedScene))
    {
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
        vm_net_mock_hangup_loop_clear("hangup-no-monster");
        return pos;
    }

    requestedEnemyId = vm_net_mock_normalize_battle_enemy_id(requestedEnemyId);
    /*
     * Subtype-5 requires the client's live 25-row scene-node tuple.  SCE
     * combat-spawn ordinals (source=SCE2-combat-spawn) are not that contract:
     * runtime on 01桃花岛_02 showed challenge live index=6 pos=(102,287) while
     * SCE returned index=1 for the same coords, and HandleBattleStartMsg then
     * built a null-visual unit that crashed at JianghuOL.CBE:0x01004EA8.
     *
     * Resolve only from (1) this session's last challenge live-node or
     * (2) an in-process emulator live-table scan.  Otherwise fall back to
     * non-scene subtype 10, which embeds the enemy template directly.
     */
    if (activeSession != NULL &&
        activeSession->lastSceneMonsterLiveValid &&
        vm_net_mock_scene_name_is_safe(activeSession->lastSceneMonsterLiveScene) &&
        vm_net_mock_scene_names_equal_loose(activeSession->lastSceneMonsterLiveScene,
                                           scene ? scene : "") &&
        activeSession->lastSceneMonsterLiveIndex < 25 &&
        activeSession->lastSceneMonsterLiveX != 0 &&
        activeSession->lastSceneMonsterLiveY != 0)
    {
        u32 liveWire = activeSession->lastSceneMonsterLiveActorId;
        u32 liveRemap = vm_net_mock_sce_combat_spawn_remap_battle_enemy(
            activeSession->lastSceneMonsterLiveScene, liveWire,
            activeSession->lastSceneMonsterLiveX,
            activeSession->lastSceneMonsterLiveY);
        /*
         * Challenge stores the ParseMinfo-safe wire id on the live node.
         * Hangup automonster / instance bind may request the remapped catalog
         * id (#203).  Match either wire==requested or remap(wire)==requested.
         */
        if (liveWire == requestedEnemyId || liveRemap == requestedEnemyId)
        {
            sceneMonsterIndex = activeSession->lastSceneMonsterLiveIndex;
            sceneMonsterPosX = activeSession->lastSceneMonsterLiveX;
            sceneMonsterPosY = activeSession->lastSceneMonsterLiveY;
            hangupMoveinfoWireId = liveWire;
            useSceneMonsterStart = true;
            targetSource = "session-live-node";
        }
    }
    if (!useSceneMonsterStart)
    {
        u32 lookWire = vm_net_mock_sce_combat_spawn_wire_for_real(
            scene, requestedEnemyId);
        if (lookWire == 0)
            lookWire = requestedEnemyId;
        if (vm_net_mock_select_scene_actor_moveinfo_target(
                lookWire, &sceneMonsterIndex, &sceneMonsterPosX,
                &sceneMonsterPosY, false))
        {
            hangupMoveinfoWireId = lookWire;
            useSceneMonsterStart = true;
            targetSource = "emulator-live-node";
        }
        else if (lookWire != requestedEnemyId &&
                 vm_net_mock_select_scene_actor_moveinfo_target(
                     requestedEnemyId, &sceneMonsterIndex, &sceneMonsterPosX,
                     &sceneMonsterPosY, false))
        {
            hangupMoveinfoWireId = requestedEnemyId;
            useSceneMonsterStart = true;
            targetSource = "emulator-live-node";
        }
    }
    if (!useSceneMonsterStart)
    {
        useSceneMonsterStart = false;
        targetSource = "non-scene-subtype10";
        sceneMonsterIndex = 0;
        sceneMonsterPosX = 0;
        sceneMonsterPosY = 0;
        hangupMoveinfoWireId =
            vm_net_mock_sce_combat_spawn_wire_for_real(scene, requestedEnemyId);
        if (hangupMoveinfoWireId == 0)
            hangupMoveinfoWireId = requestedEnemyId;
    }
    /*
     * Dream/FB 29* only: wire≠real hangup must not use subtype-5 live index.
     * Outdoor hangup keeps scene 2/2+4/5.
     */
    if (useSceneMonsterStart && hangupMoveinfoWireId != 0 &&
        hangupMoveinfoWireId != requestedEnemyId &&
        scene != NULL &&
        ((scene[0] == '2' && scene[1] == '9') ||
         (scene[0] == 'b' && scene[1] == '_' &&
          scene[2] == '2' && scene[3] == '9')))
    {
        useSceneMonsterStart = false;
        targetSource = "remap-non-scene-subtype10";
        sceneMonsterIndex = 0;
        sceneMonsterPosX = 0;
        sceneMonsterPosY = 0;
        printf("[info][network] mock_hangup_battle_remap_subtype10 "
               "wire=%u real=%u scene=%s evidence="
               "dream-fb-only-duplicate-wire-moveinfo-vs-live-index\n",
               hangupMoveinfoWireId, requestedEnemyId,
               scene ? scene : "-");
    }
    battleStartSubtype = useSceneMonsterStart ? 5 : 10;

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
                                                          vm_net_mock_battle_dead_banner_gbk))
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
        vm_net_mock_hangup_loop_clear("hangup-reject-dead");
        return pos;
    }
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;

    battleEnemyCount = vm_net_mock_battle_roll_enemy_count(useSceneMonsterStart);
    if (useSceneMonsterStart)
    {
        battleInfoLen = vm_net_mock_build_battle_scene_start_info_blob(
            battleInfo, sizeof(battleInfo),
            sceneMonsterIndex,
            sceneMonsterPosX,
            sceneMonsterPosY,
            battleEnemyCount,
            roleId);
    }
    else
    {
        battleInfoLen = vm_net_mock_build_battle_start_info_blob(
            battleInfo, sizeof(battleInfo),
            roleId, requestedEnemyId, playerOnRight);
    }
    if (battleInfoLen == 0 || battleInfoLen > 0xffff)
        return 0;

    if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    if (useSceneMonsterStart)
    {
        u32 moveWire = hangupMoveinfoWireId != 0 ? hangupMoveinfoWireId
                                                 : requestedEnemyId;
        if (!vm_net_mock_append_scene_monster_moveinfo2_object_ex(
                out, outCap, &pos, moveWire, sceneMonsterPosX, sceneMonsterPosY,
                requestedEnemyId))
            return 0;
        ++objectCount;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 4, battleStartSubtype,
                                     &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "side", battleSide))
        return 0;
    if (!vm_net_mock_put_object_raw(out, outCap, &pos, "battleinfo", battleInfo,
                                    (u16)battleInfoLen))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    ++objectCount;
    /*
     * Prefer auto must carry 4/11 type=1 on the start packet so the client
     * hides the manual operate panel (skills/items).  Delaying type=1 until
     * after the cancel gap left that panel up and covered the auto button.
     * Cancel window still only gates poll synth, not this UI flag.
     */
    if (autoFlagType != 0)
    {
        if (!vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos,
                                                               autoFlagType))
            return 0;
        ++objectCount;
    }
    /* Real button only: tell the player hangup started (poll synth skips).
     * Stop-after last-start already queued「下一场完成后挂机停止」. */
    if (request != NULL && requestLen != 0 && autoFlagType != 0 &&
        g_mockHangupStopAfterBattle == 0)
    {
        /* GBK: 已开始挂机 */
        static const char startHangupGbk[] =
            "\xd2\xd1\xbf\xaa\xca\xbc\xb9\xd2\xbb\xfa";
        (void)vm_net_mock_hangup_append_system_chat_object(out, outCap, &pos,
                                                           &objectCount,
                                                           startHangupGbk);
    }
    if (moveResponseLen != 0 &&
        !vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount,
                                             moveResponse, moveResponseLen))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    g_mockBattleOperateSessionArmed = 1;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    vm_net_mock_battle_settlement_exit_clear("hangup-battle-start");
    vm_net_mock_battle_post_exit_settle_clear("hangup-battle-start");
    g_mockBattleSceneMonsterStartActive = useSceneMonsterStart ? 1 : 0;
    g_mockBattleEnemyCountCurrent = useSceneMonsterStart ? battleEnemyCount : 1;
    if (g_mockBattleEnemyCountCurrent > 1)
        g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleStartUsesSceneWireMaps = g_mockBattleSceneMonsterStartActive;
    g_mockBattleOperateTurnCounter = 0;
    memset(&g_vm_net_mock_battle_solo_modifier, 0,
           sizeof(g_vm_net_mock_battle_solo_modifier));
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    vm_net_mock_battle_enemy_ailments_clear();
    g_vm_net_mock_battle_formula_enemy_index = 0xff;
    ++g_mockBattleOperateSessionSerial;
    vm_net_mock_battle_reset_last_operate_target();
    g_vm_net_mock_battle_rewarded_serial = 0;
    g_vm_net_mock_battle_rewarded_exp = 0;
    memset(g_vm_net_mock_battle_rewarded_drops, 0,
           sizeof(g_vm_net_mock_battle_rewarded_drops));
    g_vm_net_mock_battle_rewarded_drop_result_count = 0;
    g_vm_net_mock_battle_settlement_sent_serial = 0;
    g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
    g_vm_net_mock_battle_recovered_serial = 0;
    g_mockBattleSettleWireRecoverHp = 0;
    g_mockBattleSettleWireRecoverMp = 0;
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
    g_mockBattleAutoSuppressNext12 = 0;
    g_mockBattleAutoClientDriven = 0;
    g_mockBattleAutoFlagPendingArmed = 0;
    g_mockBattleAutoFlagPendingNotBeforeMs = 0;
    g_mockBattleAutoHangupStyleFlagOk = 0;
    if (autoFlagType != 0)
    {
        g_mockBattleAutoPrefer = 1;
        g_mockHangupLoopActive = 1;
        g_mockHangupLoopScheduleAfterExit = 0;
        g_mockHangupLoopPendingArmed = 0;
        g_mockHangupLoopNotBeforeMs = 0;
        /* Start packet already carries type=1; arm with entry gap. */
        g_mockBattleAutoHangupStyleFlagOk = 1;
        vm_net_mock_battle_auto_arm_pending_at_start("hangup-start-auto");
    }
    else if (g_mockBattleAutoPrefer != 0)
    {
        g_mockHangupLoopActive = 1;
        g_mockHangupLoopScheduleAfterExit = 0;
        g_mockHangupLoopPendingArmed = 0;
        g_mockHangupLoopNotBeforeMs = 0;
        /* Prefer-only start still hid the menu via prior type=1 / keep auto UI. */
        g_mockBattleAutoHangupStyleFlagOk = 1;
        vm_net_mock_battle_auto_arm_pending_at_start("hangup-start-prefer");
    }
    else
    {
        vm_net_mock_hangup_loop_clear("hangup-start-no-auto");
        vm_net_mock_battle_auto_clear_pending();
    }

    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(requestedEnemyId);
        u32 perEnemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
        u32 perEnemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", perEnemyHp);
        if (perEnemyMaxHp < perEnemyHp)
            perEnemyMaxHp = perEnemyHp;
        printf("[info][network] mock_hangup_battle_start scene=%s table_scene=%s enemy=%u enemies=%u roleid=%u rolehp=%u/%u rolemp=%u/%u enemyhp=%u/%u per_enemy_hp=%u/%u subtype=%u scene_start=%u index=%u pos=(%u,%u) auto=%u move_upload=%u target_source=%s objects=%u resp=%u evidence=JianghuOL.CBE:0x01015E14 Type=2 + runtime:2/10+25/3(+2/1) + automonster.dsh + mmBattle:0x66CC\n",
               scene ? scene : "-",
               matchedScene ? matchedScene : "-",
               requestedEnemyId,
               vm_net_mock_battle_enemy_count_current(),
               g_vm_net_mock_battle_role_id_current,
               g_mockBattleRoleHpCurrent,
               g_mockBattleRoleHpMax,
               g_mockBattleRoleMpCurrent,
               g_mockBattleRoleMpMax,
               g_mockBattleEnemyHpCurrent,
               g_mockBattleEnemyHpMax,
               perEnemyHp,
               perEnemyMaxHp,
               battleStartSubtype,
               useSceneMonsterStart ? 1u : 0u,
               sceneMonsterIndex,
               sceneMonsterPosX,
               sceneMonsterPosY,
               autoFlagType,
               hasMoveUpload ? 1u : 0u,
               targetSource,
               objectCount,
               pos);
        vm_autotest_note("mock_hangup_battle_start scene=%s enemy=%u enemies=%u subtype=%u index=%u pos=(%u,%u) auto=%u target_source=%s response=2/10+%s evidence=JianghuOL.CBE:0x01015E14 mmBattle:0x66CC\n",
                         scene ? scene : "-",
                         requestedEnemyId,
                         vm_net_mock_battle_enemy_count_current(),
                         battleStartSubtype,
                         sceneMonsterIndex,
                         sceneMonsterPosX,
                         sceneMonsterPosY,
                         autoFlagType,
                         targetSource,
                         useSceneMonsterStart ? "2/2+4/5+4/11" : "4/10+4/11");
    }
    return pos;
}

static u32 vm_net_mock_build_challenge_interaction_response_ex(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap,
    bool forceNonSceneStart)
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
    u32 moveinfoEnemyId = 0;
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
    bool useSceneMonsterStart = !forceNonSceneStart && playerOnRight &&
                                vm_net_mock_env_u8("CBE_BATTLE_SCENE_MONSTER_START", 1) != 0;
    u8 battleStartSubtype = useSceneMonsterStart ? 5 : 10;
    bool seedSceneMonsterMoveinfo = useSceneMonsterStart &&
                                    vm_net_mock_env_u8("CBE_BATTLE_SCENE_MONSTER_MOVEINFO", 1) != 0;
    const char *sceneMonsterTargetSource = useSceneMonsterStart
                                               ? "request-live-node"
                                               : "not-applicable";
    u8 battleEnemyCount = 1;
    bool prefillEnemyTemplate = false;
    bool prefillPlayerTemplate = false;
    u32 responseObjectCount = 1;
    u8 encounterClearPrepended = 0;
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();

    if (outCap < pos || !vm_net_mock_is_challenge_interaction_request(request, requestLen))
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
        (void)vm_net_mock_battle_release_settle_for_start(
            NULL, 0, NULL, NULL, "challenge-reject-dead", false);
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos) ||
            !vm_net_mock_append_info_banner_text11_object(out, outCap, &pos,
                                                          vm_net_mock_battle_dead_banner_gbk))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, 2);
        printf("[info][network] mock_challenge_battle_start roleid=%u action=reject-dead rolehp=0 response=2/10+25/11\n",
               roleId);
        return pos;
    }
    /* No encounter-cooldown gate.  Prefixed 25/12 clears a leftover 斗/banner
     * wait from older rejects on the normal 4/1 path.  Non-scene instance
     * challenge (forceNonSceneStart) must stay a lone 4/10: 2026-07-30
     * delivered 25/12+4/10 and the client never entered mmBattle.  A later
     * hangup-shaped 2/10+4/10 trial also failed to advance 临安; hangup's
     * leading 2/10 is the Type=2 request ack, not the post-30/10 challenge
     * followup contract (warehouse/equip-sell HAS_FOLLOWUP = lone semantic
     * object).  Keep objects=1 pure 4/10 here. */
    g_mockBattleEncounterNotBeforeMs = 0;
    g_mockBattleEncounterCooldownClearPending = 0;
    hasMoveinfo = vm_net_mock_get_object_blob_field(request, requestLen, "moveinfo",
                                                    &moveInfo, &moveInfoLen);
    if (hasMoveinfo)
        (void)vm_net_mock_snapshot_current_player_pos("moveinfo-upload-combo");
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "id", &id);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "index", &index);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posx", &posx);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posy", &posy);
    sceneMonsterIndex = index;
    sceneMonsterPosX = posx;
    sceneMonsterPosY = posy;
    {
        u32 wireEnemyId = vm_net_mock_normalize_battle_enemy_id(id);
        const char *remapScene =
            (activeSession != NULL && activeSession->sceneVisibleReady &&
             vm_net_mock_scene_name_is_safe(activeSession->sceneVisibleScene))
                ? activeSession->sceneVisibleScene
                : vm_net_mock_current_scene_name();

        requestedEnemyId = vm_net_mock_sce_combat_spawn_remap_battle_enemy(
            remapScene, wireEnemyId, posx, posy);
        enemyWireId = vm_net_mock_resolve_battle_enemy_id(
            wireEnemyId, &enemyTable, enemyTableIds);
        /*
         * Dream/FB 29* only: duplicate wire #200 + real remap cannot use
         * subtype-5 (wrong live row / SetMapCtrlViewport null / no mmBattle).
         * Outdoor scenes keep normal 25/12+4/5 even when bindings remap.
         *
         * Same-packet lone 4/10 on the 4/1 data response is also gated
         * (JianghuOL.CBE:0x01012F8E) — client stays on the map shell and
         * SCR_Render hits SetMapCtrlViewport null (0x01046C48 / access 0x40).
         * Deliver via existing instance-challenge HAS_FOLLOWUP take (primary
         * moveinfo-ack or empty WT, followup pure 4/10).  Enter-scene paths
         * untouched.
         */
        if (useSceneMonsterStart && wireEnemyId != 0 &&
            requestedEnemyId != wireEnemyId &&
            remapScene != NULL &&
            ((remapScene[0] == '2' && remapScene[1] == '9') ||
             (remapScene[0] == 'b' && remapScene[1] == '_' &&
              remapScene[2] == '2' && remapScene[3] == '9')))
        {
            if (activeSession != NULL)
            {
                u32 primaryPos = 5;
                u8 primaryObjects = 0;

                activeSession->instanceChallengeBattlePending = true;
                activeSession->instanceChallengeBattleWireCount = 0;
                activeSession->instanceChallengeTick = g_schedulerTick;
                activeSession->instanceChallengeActorId = wireEnemyId;
                activeSession->instanceChallengeEnemyId = requestedEnemyId;
                activeSession->instanceChallengeX =
                    (u16)((posx != 0) ? posx : 1u);
                activeSession->instanceChallengeY =
                    (u16)((posy != 0) ? posy : 1u);
                snprintf(activeSession->instanceChallengeScene,
                         sizeof(activeSession->instanceChallengeScene),
                         "%s", remapScene);

                if (hasMoveinfo)
                {
                    if (!vm_net_mock_append_actor_moveinfo_empty_ack_object(
                            out, outCap, &primaryPos))
                    {
                        return 0;
                    }
                    primaryObjects = 1;
                }
                vm_net_mock_finish_wt_packet(out, primaryPos, primaryObjects);
                printf("[info][network] mock_challenge_battle_remap_pure_subtype10 "
                       "wire=%u real=%u scene=%s pos=(%u,%u) primary=%u "
                       "battle_delivery=data-followup "
                       "evidence=dream-fb-only-same-packet-4/10-gated-"
                       "0x01012F8E-SetMapCtrlViewport-0x40\n",
                       wireEnemyId, requestedEnemyId, remapScene,
                       activeSession->instanceChallengeX,
                       activeSession->instanceChallengeY,
                       primaryPos);
                vm_autotest_note("mock_challenge_battle_remap_pure_subtype10 "
                                "wire=%u real=%u battle_delivery=data-followup\n",
                                wireEnemyId, requestedEnemyId);
                return primaryPos;
            }
            useSceneMonsterStart = false;
            battleStartSubtype = 10;
            seedSceneMonsterMoveinfo = false;
            sceneMonsterTargetSource = "remap-pure-subtype10";
            printf("[info][network] mock_challenge_battle_remap_pure_subtype10 "
                   "wire=%u real=%u scene=%s evidence="
                   "fallback-same-packet-no-session "
                   "dream-fb-only\n",
                   wireEnemyId, requestedEnemyId,
                   remapScene ? remapScene : "-");
        }
        if (useSceneMonsterStart && activeSession != NULL && wireEnemyId != 0 &&
            index < 25 && posx != 0 && posy != 0)
        {
            const char *liveScene =
                (activeSession->sceneVisibleReady &&
                 vm_net_mock_scene_name_is_safe(
                     activeSession->sceneVisibleScene))
                    ? activeSession->sceneVisibleScene
                    : vm_net_mock_current_scene_name();
            if (vm_net_mock_scene_name_is_safe(liveScene))
            {
                activeSession->lastSceneMonsterLiveValid = true;
                snprintf(activeSession->lastSceneMonsterLiveScene,
                         sizeof(activeSession->lastSceneMonsterLiveScene),
                         "%s", liveScene);
                activeSession->lastSceneMonsterLiveActorId = wireEnemyId;
                activeSession->lastSceneMonsterLiveIndex = index;
                activeSession->lastSceneMonsterLiveX = posx;
                activeSession->lastSceneMonsterLiveY = posy;
            }
        }
        moveinfoEnemyId = wireEnemyId;
    }
    /*
     * Remapped pure 4/10 must not carry 25/12 (or trailing moveinfo ack):
     * same lone-object contract as forceNonSceneStart.
     */
    if (!forceNonSceneStart && useSceneMonsterStart)
    {
        if (!vm_net_mock_append_info_banner_clear12_object(out, outCap, &pos))
            return 0;
        encounterClearPrepended = 1;
    }
    {
        u8 settleObjects = 0;

        /*
         * responseObjectCount starts at 1 for the upcoming 1/4/{5|10}.
         * Prepended tear-down (prior fight's delayed 4/8) adds on top so 4/1
         * still gets a real battle start after authentic exit objects.
         */
        if (!vm_net_mock_battle_release_settle_for_start(out, outCap, &pos,
                                                         &settleObjects,
                                                         "challenge-battle-start",
                                                         true))
        {
            return 0;
        }
        responseObjectCount =
            (u32)settleObjects + (u32)encounterClearPrepended + 1u;
    }
    if (roleHp > roleMaxHp)
        roleHp = roleMaxHp;
    if (roleMp > roleMaxMp)
        roleMp = roleMaxMp;
    battleEnemyCount = vm_net_mock_battle_roll_enemy_count(useSceneMonsterStart);
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

    g_vm_net_mock_battle_enemy_id_current = requestedEnemyId;

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
        u32 moveWire =
            moveinfoEnemyId != 0 ? moveinfoEnemyId : requestedEnemyId;
        /*
         * Live-node actor field stays on the ParseMinfo wire id; HP/MP come
         * from the remapped catalog/custom id so subtype-5 reads real vitals.
         */
        if (!vm_net_mock_append_scene_monster_moveinfo2_object_ex(
                out, outCap, &pos, moveWire, sceneMonsterPosX, sceneMonsterPosY,
                requestedEnemyId))
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
    /*
     * Player moveinfo empty-ack only on scene subtype-5 starts.  Remapped /
     * forceNonScene pure 4/10 must stay objects=1 (no trailing 2/1).
     */
    if (hasMoveinfo && useSceneMonsterStart)
    {
        if (!vm_net_mock_append_actor_moveinfo_empty_ack_object(out, outCap, &pos))
            return 0;
        ++responseObjectCount;
    }
    /*
     * Challenge start must not inherit prior-battle auto/skill prefer
     * (临安 log: cross-battle-auto-skill after 蓬莱).  Clear before optional
     * 4/11 append so a fresh challenge opens with a manual operate panel.
     */
    if (g_mockBattleAutoPrefer != 0 || g_mockBattleLastOperateValid != 0)
    {
        printf("[info][network] mock_challenge_battle_auto_reset prefer=%u last=%u "
               "operate=%u evidence=challenge-start-clear-cross-battle\n",
               g_mockBattleAutoPrefer ? 1 : 0,
               g_mockBattleLastOperateValid ? 1 : 0,
               g_mockBattleLastOperate);
    }
    g_mockBattleAutoPrefer = 0;
    g_mockBattleLastOperateValid = 0;
    g_mockBattleLastOperate = 0;
    /*
     * Cross-battle auto: re-arm UI with 4/11 type=1 so the client hides the
     * manual operate panel and the next tick / client auto path can replay
     * the preserved last Operate.  Cancel window still gates synth only.
     * Never into a shared team fight — solo synth would bypass round_defer.
     * (Prefer is cleared above for challenge starts; hangup paths re-set it.)
     */
    if (g_mockBattleAutoPrefer != 0 && teamBattlePartyCount < 2)
    {
        if (!vm_net_mock_append_battle_case11_auto_flag_object(out, outCap, &pos, 1))
            return 0;
        ++responseObjectCount;
    }
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
    g_mockBattleOperateSessionArmed = 1;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    vm_net_mock_battle_settlement_exit_clear("hangup-battle-start");
    vm_net_mock_battle_post_exit_settle_clear("hangup-battle-start");
    g_mockBattleSceneMonsterStartActive = useSceneMonsterStart ? 1 : 0;
    g_mockBattleEnemyCountCurrent = useSceneMonsterStart ? battleEnemyCount : 1;
    if (g_mockBattleEnemyCountCurrent > 1)
        g_mockBattleSceneMonsterStartActive = 1;
    g_mockBattleStartUsesSceneWireMaps = g_mockBattleSceneMonsterStartActive;
    g_mockBattleOperateTurnCounter = 0;
    memset(&g_vm_net_mock_battle_solo_modifier, 0,
           sizeof(g_vm_net_mock_battle_solo_modifier));
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    vm_net_mock_battle_enemy_ailments_clear();
    g_vm_net_mock_battle_formula_enemy_index = 0xff;
    ++g_mockBattleOperateSessionSerial;
    vm_net_mock_battle_reset_last_operate_target();
    g_vm_net_mock_battle_rewarded_serial = 0;
    g_vm_net_mock_battle_rewarded_exp = 0;
    memset(g_vm_net_mock_battle_rewarded_drops, 0,
           sizeof(g_vm_net_mock_battle_rewarded_drops));
    g_vm_net_mock_battle_rewarded_drop_result_count = 0;
    g_vm_net_mock_battle_settlement_sent_serial = 0;
    g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
    g_vm_net_mock_battle_recovered_serial = 0;
    g_mockBattleSettleWireRecoverHp = 0;
    g_mockBattleSettleWireRecoverMp = 0;
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
    {
        vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(requestedEnemyId);
        u32 perEnemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
        u32 perEnemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", perEnemyHp);
        if (perEnemyMaxHp < perEnemyHp)
            perEnemyMaxHp = perEnemyHp;
        vm_net_mock_battle_reset_enemy_hp_from_stats(requestedEnemyId);
        g_mockBattleAutoSuppressNext12 = 0;
        g_mockBattleAutoClientDriven = 0;
        g_mockBattleAutoFlagPendingArmed = 0;
        g_mockBattleAutoFlagPendingNotBeforeMs = 0;
        g_mockBattleAutoHangupStyleFlagOk = 0;
        if (teamBattlePartyCount >= 2)
        {
            /* Shared team fights own the turn barrier; solo prefer/synth must not. */
            vm_net_mock_battle_suspend_solo_auto_for_team("challenge-team-start");
        }
        else if (g_mockBattleAutoPrefer != 0)
        {
            /* Start packet already carries 4/11; arm with entry gap. */
            g_mockBattleAutoHangupStyleFlagOk = 1;
            vm_net_mock_battle_auto_arm_pending_at_start("challenge-start-prefer");
        }
        else
        {
            vm_net_mock_battle_auto_clear_pending();
        }
        printf("[info][network] mock_challenge_battle_start id=%u requested=%u roleid=%u enemies=%u rolehp=%u/%u rolemp=%u/%u enemyhp=%u/%u per_enemy_hp=%u/%u enemymp=%u subtype=%u side=%u scene_start=%u index=%u pos=(%u,%u) req_index=%u req_pos=(%u,%u) target_source=%s prefill_player=%u prefill_enemy=%u objects=%u\n",
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
               responseObjectCount);
        if (g_mockBattleAutoPrefer != 0 || g_mockBattleLastOperateValid != 0)
        {
            printf("[info][network] mock_challenge_battle_auto prefer=%u last=%u operate=%u "
                   "evidence=unexpected-prefer-after-challenge-reset\n",
                   g_mockBattleAutoPrefer ? 1 : 0,
                   g_mockBattleLastOperateValid ? 1 : 0,
                   g_mockBattleLastOperate);
        }
        vm_autotest_note("mock_challenge_battle_start id=%u requested=%u roleid=%u enemies=%u wire=%u level=%u hp=%u/%u perhp=%u/%u rolemp=%u/%u enemymp=%u atk=%u def=%u exp=%u gold=%u index=%u pos=(%u,%u) reqIndex=%u reqPos=(%u,%u) target_source=%s subtype=%u side=%u scene_start=%u table=%08x ids=%u/%u/%u/%u\n",
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
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_DEFENSE", 0),
                         /* effective combat def; catalog stats.defense unused */
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_EXP", stats.exp),
                         vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_GOLD", stats.gold),
                         sceneMonsterIndex, sceneMonsterPosX, sceneMonsterPosY,
                         index, posx, posy,
                         sceneMonsterTargetSource,
                         battleStartSubtype, battleSide, useSceneMonsterStart ? 1 : 0,
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
        vm_net_mock_battle_suspend_solo_auto_for_team("team-battle-leader-start");
        printf("[info][network] mock_team_battle_start leader=%08x party=%u "
               "queued=%u response=%u source=leader-4/1\n",
               activeSession->clientId,
               teamBattlePartyCount,
               teamBattleQueuedCount,
               pos);
    }
    return pos;
}

