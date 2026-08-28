static u32 vm_net_mock_build_direct_scene_challenge_unready_response(
    u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos) ||
        !vm_net_mock_append_info_banner_text11_object(
            out, outCap, &pos,
            "\xCC\xF4\xD5\xBD\xC4\xBF\xB1\xEA\xC9\xD0\xCE\xB4\xBC\xD3\xD4\xD8"
            "\xA3\xAC\xC7\xEB\xD6\xD8\xD0\xC2\xBD\xF8\xC8\xEB\xB3\xA1\xBE\xB0"
            "\xBA\xF3\xD6\xD8\xCA\xD4\xA1\xA3")) /* 挑战目标尚未加载，请重新进入场景后重试。 */
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos, 2);
    return pos;
}

static u32 vm_net_mock_build_challenge_interaction_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    const char *scene = vm_net_mock_current_scene_name();
    u32 requestedEnemyId = 0;

    if (session != NULL && session->instanceChallengeDirectPending &&
        vm_net_mock_is_challenge_interaction_request(request, requestLen) &&
        vm_net_mock_get_object_u32_field(request, requestLen, "id",
                                         &requestedEnemyId))
    {
        u32 ageTicks = g_schedulerTick - session->instanceChallengeTick;
        u32 requestedSceneIndex = 0;
        u32 configuredSceneIndex = 0;
        u32 expectedSceneX = 0;
        u32 expectedSceneY = 0;
        bool directSceneMonster =
            session->instanceChallengeDirectSceneMonster;
        bool valid = ageTicks <= (60u * 1000u / VM_SCHED_FRAME_MS) &&
                     requestedEnemyId == session->instanceChallengeEnemyId &&
                     vm_net_mock_scene_name_is_safe(scene) &&
                     vm_net_mock_scene_names_equal_exact(
                         scene, session->instanceChallengeScene);

        if (directSceneMonster)
        {
            bool requestCarriesSceneIndex =
                vm_net_mock_get_object_u32_field(request, requestLen, "index",
                                                 &requestedSceneIndex);

            valid = valid && requestCarriesSceneIndex &&
                    requestedSceneIndex != 0 && requestedSceneIndex < 25 &&
                    vm_net_mock_select_sce_combat_spawn(
                        scene, requestedEnemyId, &configuredSceneIndex,
                        &expectedSceneX, &expectedSceneY) &&
                    expectedSceneX != 0 && expectedSceneX <= UINT16_MAX &&
                    expectedSceneY != 0 && expectedSceneY <= UINT16_MAX;
        }

        if (valid)
        {
            u32 responseLen = 0;

            if (directSceneMonster)
            {
                /* action13 deliberately serializes zero coordinates.  Bind
                 * its live node index to the SCE coordinates before replying
                 * with the native scene-node start. A 30/9 confirmation here
                 * returns control to mmGame; its later 2/2 + 4/5 poll is then
                 * consumed by that module instead of mmBattle. */
                session->instanceChallengeSceneIndex = requestedSceneIndex;
                session->instanceChallengeX = (u16)expectedSceneX;
                session->instanceChallengeY = (u16)expectedSceneY;
                responseLen =
                    vm_net_mock_build_direct_scene_challenge_battle_response(
                        requestedEnemyId, requestedSceneIndex,
                        (u16)expectedSceneX, (u16)expectedSceneY, out,
                        outCap);
            }
            else
            {
                responseLen = vm_net_mock_build_challenge_interaction_response_ex(
                    request, requestLen, out, outCap, true, false);
                session->instanceChallengeDirectPending = false;
                session->instanceChallengeDirectSceneMonster = false;
            }
            printf("[info][network] mock_npc_instance_challenge_native client=%08x actor=%u enemy=%u age_ticks=%u scene=%s request=4/1(action13) req_index=%u config_index=%u response=%s resp=%u evidence=JianghuOL.CBE:0x01037ED4+mmBattle:0x66CC/0x67AC\n",
                   session->clientId, session->instanceChallengeActorId,
                   requestedEnemyId, ageTicks, scene, requestedSceneIndex,
                   configuredSceneIndex,
                   directSceneMonster ? "2/2+4/5-scene-direct" : "4/10-direct",
                   responseLen);
            if (responseLen != 0)
            {
                session->instanceChallengeDirectPending = false;
                session->instanceChallengeDirectSceneMonster = false;
                session->instanceChallengeActorId = 0;
                session->instanceChallengeEnemyId = 0;
                session->instanceChallengeSceneIndex = 0;
                session->instanceChallengeX = 0;
                session->instanceChallengeY = 0;
                session->instanceChallengeTick = 0;
                session->instanceChallengeScene[0] = 0;
            }
            return responseLen;
        }
        if (directSceneMonster)
        {
            u32 responseLen = vm_net_mock_build_direct_scene_challenge_unready_response(
                out, outCap);
            printf("[warn][network] mock_direct_scene_challenge_reject client=%08x actor=%u expected_enemy=%u requested_enemy=%u req_index=%u config_index=%u age_ticks=%u scene=%s reason=live-node-unready response=2/10+25/11 resp=%u evidence=JianghuOL.CBE:0x01037ED4+mmBattle:0x66CC\n",
                   session->clientId, session->instanceChallengeActorId,
                   session->instanceChallengeEnemyId, requestedEnemyId,
                   requestedSceneIndex, configuredSceneIndex, ageTicks,
                   scene ? scene : "-", responseLen);
            session->instanceChallengeDirectPending = false;
            session->instanceChallengeDirectSceneMonster = false;
            session->instanceChallengeActorId = 0;
            session->instanceChallengeEnemyId = 0;
            session->instanceChallengeSceneIndex = 0;
            session->instanceChallengeX = 0;
            session->instanceChallengeY = 0;
            session->instanceChallengeTick = 0;
            session->instanceChallengeScene[0] = 0;
            return responseLen;
        }
        printf("[warn][network] mock_npc_instance_challenge_native_drop client=%08x actor=%u expected_enemy=%u requested_enemy=%u age_ticks=%u current_scene=%s pending_scene=%s reason=stale-or-mismatch\n",
               session->clientId, session->instanceChallengeActorId,
               session->instanceChallengeEnemyId, requestedEnemyId, ageTicks,
               scene ? scene : "-", session->instanceChallengeScene);
        session->instanceChallengeDirectPending = false;
        session->instanceChallengeDirectSceneMonster = false;
        session->instanceChallengeSceneIndex = 0;
    }
    return vm_net_mock_build_challenge_interaction_response_ex(
        request, requestLen, out, outCap, false, false);
}

static u32 vm_net_mock_build_special_scene_interaction_response(const u8 *request, u32 requestLen,
                                                                u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 id = 0;
    u32 index = 0;
    u32 posx = 0;
    u32 posy = 0;
    const char *scene = vm_net_mock_current_scene_name();
    const char sourceScene01[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_01.sce */
    const char sourceScene03[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_03.sce */
    const char targetScene02[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_02.sce */
    const char targetScene04[] = "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x34\x2e\x73\x63\x65"; /* GBK: 01TaoHuaDao_04.sce */
    const char *targetScene = NULL;
    u16 targetX = 0;
    u16 targetY = 0;
    bool saveImmediately = false;
    vm_net_mock_scene_change_target target;
    char sourceScene[64];

    if (outCap < pos || !vm_net_mock_is_challenge_interaction_request(request, requestLen))
        return 0;
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "id", &id);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "index", &index);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posx", &posx);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "posy", &posy);

    /*
     * Runtime evidence on 2026-06-13: id=105 is also used by monsters. A broad
     * id/index/posy gate steals battle challenges, so only answer the known
     * portal rectangles recovered from the same-class scene table.
     */
    if (id == 105 && index == 4 &&
        posx >= 208 && posx <= 256 && posy >= 432 && posy <= 448 &&
        strcmp(scene, sourceScene01) == 0)
    {
        targetScene = targetScene02;
        targetX = 80;
        targetY = 60;
        saveImmediately = true;
    }
    else if (id == 4 && index == 4 &&
             posx >= 160 && posx <= 240 && posy >= 553 && posy <= 570 &&
             strcmp(scene, sourceScene03) == 0)
    {
        targetScene = targetScene04;
        targetX = 42;
        targetY = 60;
    }
    else
    {
        return 0;
    }
    snprintf(sourceScene, sizeof(sourceScene), "%s", scene ? scene : "");

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_scene_fields_with(out, outCap, &pos, false, false, 0, targetScene, targetX, targetY))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    /* `30/1 {scene,posinfo}` is the native scene-enter contract
     * (EnterSceneByMapName), not an in-place coordinate update.  The target
     * position is persisted below, but the source must stop being eligible for
     * old-scene nearby baselines before the response reaches the client.  This
     * special portal previously skipped the ordinary scene-target lifecycle,
     * leaving sceneVisibleScene on the old TaoHuaDao map until a later
     * follow-up happened to promote it. */
    memset(&target, 0, sizeof(target));
    snprintf(target.scene, sizeof(target.scene), "%s", targetScene);
    target.x = targetX;
    target.y = targetY;
    target.mapType = 2;
    target.hasSceEntry = true;
    target.needsSceneDownload = false;
    vm_mock_service_mark_active_session_scene_pending(
        &target, "special-scene-interaction-30-1");
    if (saveImmediately)
        vm_net_mock_save_player_pos_state(targetScene, targetX, targetY, "special-scene-interaction");
    else
        vm_net_mock_mark_pending_scene_pos_save(targetScene, targetX, targetY, "special-scene-interaction");
    printf("[info][mock-service] special_scene_portal_pending client=%08x source=%s target=%s pos=(%u,%u) response=30/1 evidence=JianghuOL.CBE:0x010396D6+0x01018150\n",
           g_vm_mock_service_active_client_id,
           sourceScene[0] ? sourceScene : "-", target.scene, target.x, target.y);
    return pos;
}

static u32 vm_net_mock_build_scene_interaction_followup_response(const u8 *request, u32 requestLen,
                                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u32 page5Index = 0;
    u32 page6Index = 0;
    u32 totalItems = 0;
    u32 pageRows = 0;
    u32 itemInfoLen = 0;
    u32 page6TotalItems = 0;
    u32 page6Rows = 0;
    u32 page6ItemInfoLen = 0;
    char page5Ids[160];
    char page6Ids[160];
    bool needActorOther = false;
    bool needFb11 = false;
    bool needFb4 = false;
    bool needBooks = false;
    u8 fb4Type = 1;

    if (outCap < pos || !vm_net_mock_is_scene_interaction_followup_request(request, requestLen))
        return 0;
    /*
     * The NPC dialog / mmShop init path can batch the 14/14,14/4,14/5,14/6
     * requests into one WT packet.  Answer the whole family in the same response:
     * mmShop:0x9DE increments its local completion counter across these objects
     * and closes loading after all four are seen.
     */
    needActorOther = vm_net_mock_request_contains_object(request, requestLen, 1, 2, 10);
    needFb11 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 11);
    needFb4 = vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4);
    needBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    fb4Type = vm_net_mock_get_request_fb4_type(request, requestLen, 1);
    page5Index = vm_net_mock_get_shop_page_index_in_request(request, requestLen, 5, 0);
    page6Index = vm_net_mock_get_shop_page_index_in_request(request, requestLen, 6, 0);

    if (needActorOther)
    {
        if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    if (needFb11)
    {
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    if (needFb4)
    {
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos, fb4Type, ""))
            return 0;
        ++objectCount;
    }
    if (needBooks)
    {
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        ++objectCount;
    }
    if (!vm_net_mock_append_shop_open_status14_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_shop_money4_object(out, outCap, &pos))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_shop_catalog_page_object(out, outCap, &pos, 5, page5Index,
                                                    &totalItems, &pageRows, &itemInfoLen))
        return 0;
    ++objectCount;
    if (!vm_net_mock_append_shop_catalog_page_object(out, outCap, &pos, 6, page6Index,
                                                    &page6TotalItems, &page6Rows,
                                                    &page6ItemInfoLen))
        return 0;
    ++objectCount;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_netMockShop17ListPending = 1;
    g_netMockShopCatalogDeliveredBeforeActorQuery = 1;
    /*
     * `14/*` is mmShop's ordinary data-initialisation family.  In particular,
     * it is also emitted when the player opens the mall from Backpack.  Its
     * native module initialiser (mmShop:sub_1038) has no scene-enter call, so
     * it cannot authorize a server-side 30/1 scene re-entry.  Arming that
     * lifecycle here used to replace the client-owned Backpack screen with a
     * newly constructed mmGame shell when the next ordinary scene request
     * arrived.  Keep the existing screen stack and item manager intact.
     *
     * A following `1/1/14(actorId)` status query is handled separately so its
     * mandatory final ActorInfo object remains ordered for mmShop:sub_9DE;
     * it is still not a scene-return authorization.
     */
    vm_net_mock_format_shop_page_ids(5, page5Index, 8, page5Ids, sizeof(page5Ids));
    vm_net_mock_format_shop_page_ids(6, page6Index, 8, page6Ids, sizeof(page6Ids));
    printf("[info][network] mock_shop_scene_interaction_combo page5=%u page6=%u secret_total=%u secret_rows=%u secret_iteminfo_len=%u secret_ids=%s weapon_total=%u weapon_rows=%u weapon_iteminfo_len=%u weapon_ids=%s actorOther=%u fb11=%u fb4=%u books=%u owner=client-stack grid_reseed=0\n",
           page5Index,
           page6Index,
           totalItems,
           pageRows,
           itemInfoLen,
           page5Ids,
           page6TotalItems,
           page6Rows,
           page6ItemInfoLen,
           page6Ids,
           needActorOther ? 1 : 0,
           needFb11 ? 1 : 0,
           needFb4 ? 1 : 0,
           needBooks ? 1 : 0);
    vm_autotest_note("mock_shop_scene_interaction_combo page5=%u page6=%u secret_total=%u secret_rows=%u secret_iteminfo_len=%u weapon_total=%u weapon_rows=%u weapon_iteminfo_len=%u actorOther=%u fb11=%u fb4=%u books=%u owner=client-stack grid_reseed=0 evidence=mmShop:0x1038/0x618/0x9DE\n",
                     page5Index,
                     page6Index,
                     totalItems,
                     pageRows,
                     itemInfoLen,
                     page6TotalItems,
                     page6Rows,
                     page6ItemInfoLen,
                     needActorOther ? 1 : 0,
                     needFb11 ? 1 : 0,
                     needFb4 ? 1 : 0,
                     needBooks ? 1 : 0);
    return pos;
}

static bool vm_net_mock_append_info_banner_clear12_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 12, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 4))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_info_banner_result5_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x19, 5, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 4))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_scene_resource_followup_objects_ex(u8 *out, u32 outCap, u32 *pos,
                                                                  u8 *objectCount, const char *sceneOverride,
                                                                  bool includeSkillBooks,
                                                                  bool includeTaskLists,
                                                                  bool includeActorOther, bool includeInfoBanner,
                                                                  bool includeFbTargetClear,
                                                                  bool includeFbTargetSeedOnly,
                                                                  bool preferSceneNpcOther,
                                                                  bool preferSceneRoleOther)
{
    /*
     * In the first scene-enter dispatch window we have a confirmed practical
     * limit of 10 objects.  Keep the skill/book pair plus empty 6/1 taskinfo
     * in this window; omitting either causes the client to immediately request
     * the large 12/1+7/42+6/*+2/10+25/5 follow-up after 30/1 closes the gate.
     */
    if (includeSkillBooks)
    {
        u8 added = 0;
        if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, pos, &added,
                                                          true))
            return false;
        *objectCount = (u8)(*objectCount + added);
    }

    /*
     * Create/refresh scene nodes before dispatching task rows.  The client only
     * recomputes node+326 from task response cases (6/1, 6/6 and 6/14); sending
     * those rows before 2/10 leaves a newly created NPC without its prompt icon
     * until a later task refresh.
     */
    if (includeActorOther)
    {
        if (preferSceneRoleOther)
        {
            const char *scene = vm_net_mock_scene_name_is_safe(sceneOverride)
                                    ? sceneOverride
                                    : vm_net_mock_current_scene_name();
            u32 savedPos = *pos;
            u32 roleCount = 0;
            u32 otherInfoLen = 0;

            if (!vm_net_mock_append_actor_other_scene_roles10_object(out, outCap, pos,
                                                                     scene,
                                                                     &roleCount,
                                                                     &otherInfoLen) ||
                roleCount == 0)
            {
                *pos = savedPos;
                if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, pos))
                    return false;
            }
        }
        else if (preferSceneNpcOther || vm_net_mock_env_u32("CBE_SCENE_NPC_OTHERINFO", 0) != 0)
        {
            const char *scene = vm_net_mock_scene_name_is_safe(sceneOverride)
                                    ? sceneOverride
                                    : vm_net_mock_current_scene_name();
            u32 npcCount = 0;
            u32 otherInfoLen = 0;
            if (!vm_net_mock_append_actor_other_scene_npcs10_object(out, outCap, pos,
                                                                    scene,
                                                                    preferSceneNpcOther,
                                                                    &npcCount, &otherInfoLen))
                return false;
        }
        else if (!vm_net_mock_append_actor_other_empty10_object(out, outCap, pos))
        {
            return false;
        }
        *objectCount += 1;
    }

    if (!vm_net_mock_append_taskinfo_empty1_object(out, outCap, pos, sceneOverride))
        return false;
    *objectCount += 1;

    /*
     * Case 13 loops over six tasktype slots. Each slot is a tagged i8 type id
     * followed by a len16 C string; use raw field payload so the stream cursor
     * starts directly at the first record.
     */
    if (includeTaskLists)
    {
        if (!vm_net_mock_append_tasktypes_empty13_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_taskaction14_object(out, outCap, pos, sceneOverride))
            return false;
        *objectCount += 1;
    }

    if (includeInfoBanner)
    {
        if (!vm_net_mock_append_info_banner_result5_object(out, outCap, pos))
            return false;
        *objectCount += 1;
    }

    /*
     * Earlier experiments showed the 27/12+27/11 family suppresses the large
     * WT49 follow-up, while 27/11 alone does not set the needed fb state. Pair
     * 27/12 and 27/11 with 27/4 ready/finalize so the gate is restored in the
     * same follow-up scan.
     */
    if (includeFbTargetClear)
    {
        if (!vm_net_mock_append_fb_target_result12_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, pos, 1, ""))
            return false;
        *objectCount += 1;
    }
    else if (includeFbTargetSeedOnly)
    {
        /*
         * Hypothesis under test: after the requested WT49 resource/task/other
         * family is answered, run the complete fb-target trio in one follow-up
         * scan. 27/11 alone leaves sceneGateA at 0, but 27/4 immediately after
         * it restores the gate pair; use a non-empty info string so case 27/4
         * also exercises its scene-object info side effects.
         */
        if (!vm_net_mock_append_fb_target_result12_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, pos))
            return false;
        *objectCount += 1;

        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, pos, 1,
                                                        vm_net_mock_fb_target_info_text()))
            return false;
        *objectCount += 1;
    }

    return true;
}

static bool vm_net_mock_append_scene_resource_followup_objects(u8 *out, u32 outCap, u32 *pos,
                                                               u8 *objectCount, const char *sceneOverride,
                                                               bool includeSkillBooks,
                                                               bool includeTaskLists,
                                                               bool includeActorOther, bool includeInfoBanner,
                                                               bool includeFbTargetClear,
                                                               bool includeFbTargetSeedOnly,
                                                               bool preferSceneNpcOther)
{
    return vm_net_mock_append_scene_resource_followup_objects_ex(out, outCap, pos,
                                                                 objectCount, sceneOverride,
                                                                 includeSkillBooks,
                                                                 includeTaskLists,
                                                                 includeActorOther,
                                                                 includeInfoBanner,
                                                                 includeFbTargetClear,
                                                                 includeFbTargetSeedOnly,
                                                                 preferSceneNpcOther,
                                                                 false);
}

static bool vm_net_mock_append_scene_npc_lifecycle_seed(u8 *out, u32 outCap,
                                                         u32 *pos, u8 *objectCount,
                                                         const char *currentScene,
                                                         bool allowStartupSeed,
                                                         bool allowShopReturnSeed)
{
    vm_mock_service_client_session *activeSession =
        vm_mock_service_get_active_client_session();
    u8 npcCount = 0;
    bool startupSeed = false;
    bool shopReturnSeed = false;
    bool bootstrapOnlySeed = false;
    const char *phase = NULL;

    if (out == NULL || pos == NULL || objectCount == NULL ||
        !vm_net_mock_scene_name_is_safe(currentScene))
    {
        return true;
    }

    if (activeSession != NULL && activeSession->shopSceneNpcReseedPending)
    {
        if (activeSession->shopSceneNpcReseedScene[0] != 0 &&
            vm_net_mock_scene_names_equal_exact(activeSession->shopSceneNpcReseedScene,
                                                currentScene))
        {
            /* A backpack-return bootstrap has already rebuilt mmGame's scene
             * tables.  Its first scene follow-up must therefore recreate the
             * NPC directory even when that particular follow-up is also
             * completing an unrelated deferred target.  Unlike the legacy
             * shop-return marker, it never authorizes a 30/2 scene re-enter;
             * only the 27/11 lifecycle payload is consumed here. */
            bootstrapOnlySeed =
                vm_mock_service_shop_scene_npc_reseed_is_bootstrap_only();
            shopReturnSeed = allowShopReturnSeed || bootstrapOnlySeed;
        }
        else
        {
            printf("[info][mock-service] scene_npc_reseed_cancel client=%08x armed_scene=%s current_scene=%s reason=scene-changed-before-shop-return\n",
                   activeSession->clientId,
                   activeSession->shopSceneNpcReseedScene,
                   currentScene);
            activeSession->shopSceneNpcReseedPending = false;
            activeSession->shopSceneNpcReseedMode = 0;
            activeSession->shopSceneNpcReseedScene[0] = 0;
        }
    }

    startupSeed = allowStartupSeed &&
                  !g_vm_net_mock_scene_moveinfo_npc_seeded &&
                  g_vm_net_mock_scene_moveinfo_npc_pending &&
                  g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] != 0 &&
                  vm_net_mock_scene_names_equal_exact(
                      g_vm_net_mock_scene_moveinfo_npc_pending_scene,
                      currentScene);
    if (!startupSeed && !shopReturnSeed)
        return true;

    npcCount = vm_net_mock_scene_room_npc_seed_count(currentScene);
    if (npcCount == 0)
    {
        /* The shop-return marker also owns the trailing 30/2 completion.
         * A scene with no NPC rows still needs that completion, so NPC
         * availability must never consume the lifecycle marker. */
        if (bootstrapOnlySeed)
        {
            vm_mock_service_clear_shop_scene_npc_reseed_pending(
                "backpack-grid-bootstrap-no-npc");
        }
        return true;
    }

    phase = startupSeed
                ? "startup-scene-followup-immediate"
                : (bootstrapOnlySeed
                       ? "backpack-grid-bootstrap-reseed"
                       : "shop-return-scene-followup-reseed");
    if (shopReturnSeed)
        vm_net_mock_mark_scene_moveinfo_npc_seed_pending(currentScene);
    if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, pos,
                                                       currentScene, phase))
    {
        return false;
    }
    *objectCount += 1;
    if (bootstrapOnlySeed)
    {
        vm_mock_service_clear_shop_scene_npc_reseed_pending(
            "backpack-grid-bootstrap-npc-seeded");
    }
    printf("[info][mock-service] scene_npc_lifecycle_seed client=%08x scene=%s phase=%s npcnum=%u objects=%u evidence=JianghuOL.CBE:0x01012FB4+0x01037998\n",
           activeSession ? activeSession->clientId : 0,
           currentScene,
           phase,
           (u32)npcCount,
           (u32)*objectCount);
    return true;
}

static void vm_mock_service_complete_shop_scene_return(const char *scene,
                                                       const char *source)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL ||
        !vm_mock_service_shop_scene_npc_reseed_matches(scene))
    {
        return;
    }
    printf("[info][mock-service] shop_scene_return_complete client=%08x "
           "scene=%s source=%s completion=30/2-posinfo-reenter\n",
           session->clientId, scene, source ? source : "-");
    session->shopSceneNpcReseedPending = false;
    session->shopSceneNpcReseedMode = 0;
    session->shopSceneNpcReseedScene[0] = 0;
}

/* A shop return is a scene re-entry, not a teleport.  The client consumes the
 * coordinate-bearing 30/2 result through
 * scene_handle_change_result_scene_pos(0x01039770), which unconditionally
 * installs those coordinates into the new scene shell.  Therefore this path
 * may use only the active role's persisted position in the same exact .sce
 * scene; a scene default/spawn point would silently reset the player. */
static bool vm_net_mock_get_shop_return_persisted_position(const char *scene,
                                                           u16 *xOut, u16 *yOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (xOut == NULL || yOut == NULL || role == NULL ||
        !vm_net_mock_scene_name_is_persistable(scene) ||
        !vm_net_mock_scene_name_is_persistable(role->scene) ||
        !vm_net_mock_scene_names_equal_exact(role->scene, scene) ||
        role->x == 0 || role->y == 0)
    {
        return false;
    }

    *xOut = role->x;
    *yOut = role->y;
    return true;
}

/*
 * mmShop returns to mmGame through the normal scene-enter lifecycle.  Do not
 * use a position-bearing 30/2 here: its client parser recreates the scene and
 * immediately resets the download state, which leaves the freshly allocated
 * map-layer table holding encoded resource ids (for example 0x00010001).
 *
 * 30/1 owns the new scene shell.  The matching post-enter request is the
 * first point at which the server may send the final position-bearing 30/2
 * completion: that request has otherwise restored the SCE default landing.
 * This is the same ordering used by resource-backed scene transitions.
 */
static u32 vm_net_mock_build_shop_scene_return_enter_response(
    u8 *out, u32 outCap, const char *scene, u16 x, u16 y,
    const char *source)
{
    u32 pos = 5;
    u8 objectCount = 0;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_append_scene_enter_object_for_scene(
            out, outCap, &pos, scene, x, y))
    {
        return 0;
    }
    objectCount = 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    vm_mock_service_arm_shop_scene_return_post_enter(
        scene, x, y, source ? source : "shop-return-30-1");
    /* The 30/1 helper armed the new shell's NPC catalog.  Retire only the
     * shop-open provenance; the post-enter marker remains authoritative. */
    vm_mock_service_complete_shop_scene_return(scene,
                                               source ? source : "shop-return-30-1");
    printf("[info][network] mock_shop_return_scene_enter scene=%s "
           "pos=(%u,%u) objects=%u resp=%u source=%s "
           "contract=30/1-posinfo-then-post-enter-30/2-posinfo\n",
           scene, (u32)x, (u32)y, (u32)objectCount, pos,
           source ? source : "-");
    return pos;
}

static int vm_net_mock_append_scene_ready_chat_objects(u8 *out,
                                                        u32 outCap,
                                                        u32 *pos,
                                                        const char *scene,
                                                        const char *reason,
                                                        u8 maxObjects)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    int appended = 0;

    if (out == NULL || pos == NULL || session == NULL ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return 0;
    }

    /* The first resource/task follow-up is emitted by
     * scene_runtime_init_and_sync after the map, actor and chat managers exist.
     * Promote the session before finalizing that response so mark_scene_ready
     * can queue the one-shot welcome message and the same event-7 packet can
     * deliver it alongside the 27/11 NPC catalog. */
    if (!vm_mock_service_session_scene_is_visible(session, scene) &&
        !vm_mock_service_mark_active_session_scene_ready_from_role(scene, reason))
    {
        return 0;
    }
    /* The main business callback allocates exactly ten WT object slots.  When
     * first-scene bootstrap has filled them, mark the session ready (which
     * queues the welcome notice) but leave that notice for the next poll. */
    appended = vm_net_mock_append_scene_sync_chat_objects_limited(
        out, outCap, pos, session, maxObjects);
    if (appended > 0)
    {
        printf("[info][mock-service] scene_ready_messages client=%08x scene=%s objects=%d reason=%s delivery=same-scene-completion evidence=JianghuOL.CBE:0x01012FB4+0x010126C6\n",
               session->clientId,
               scene,
               appended,
               reason ? reason : "-");
    }
    return appended;
}

/* Role-select has already created its first scene shell from actorinfo. Once
 * that exact SCE has completed WT18/7, its manifest Actor/effect entries can
 * be recorded as cache hits for the runtime request. This only reconciles
 * content state; it must not construct another scene+posinfo object. */
static void vm_net_mock_note_startup_sce_runtime_ready(const char *scene)
{
    vm_net_mock_role_state *role = NULL;

    if (!g_vm_net_mock_title_role_scene_followup_pending ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        vm_net_mock_content_client_resource_install_generation(
            g_vm_mock_service_active_client_id, scene) == 0)
    {
        return;
    }
    role = vm_net_mock_active_role();
    if (role == NULL ||
        !vm_net_mock_scene_names_equal_exact(role->scene, scene) ||
        role->x == 0 || role->y == 0)
    {
        return;
    }
    (void)vm_net_mock_scene_client_note_runtime_ready(scene);
    vm_net_mock_arm_startup_sce_install_scene_enter(scene);
}

static u32 vm_net_mock_build_scene_resource_followup_response(const u8 *request, u32 requestLen,
                                                              u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u8 actorOtherRequestType = 0;
    bool includeSkillBooks = false;
    bool keepBusinessGate = false;
    bool appendSceneRoomNpcAfterEnter = false;
    bool hasActorOtherRequestType = false;
    bool sceneSupportsActorOtherNpcSeed = false;
    bool actorOtherNpcSeedInFollowup = false;
    bool preferActorOtherNpcRows = false;
    bool startupNearbyInRequestedObject = false;
    bool probeActorOtherNpcInFollowup = false;
    bool sceneNpcInfo11SeedInFollowup = false;
    bool sceneNpcActorInfoSeedInFollowup = false;
    u32 sceneNpcActorInfoLen = 0;
    u32 sceneNpcActorId = 0;
    const char *currentScene = NULL;
    bool startupSceneAlreadyEntered = false;
    bool recentCompletedScene = false;
    bool currentSceneReload = false;
    bool sceneShellAlreadyEntered = false;
    bool shopReturnReload = false;
    u16 shopReturnX = 0;
    u16 shopReturnY = 0;
    bool deferredTeleportNpcSeedAfterCurrentCompletion = false;
    bool positionedPortalEnterPending = false;
    bool completeTeleportResourceEnter = false;
    bool completePositionedPortalEnter = false;
    vm_net_mock_scene_change_target downloadedTarget;
    vm_net_mock_scene_change_target positionedPortalTarget;
    char positionedPortalMissingResource[64];
    bool useDownloadedTarget = false;
    u32 timingStartMs = 0;
    u32 timingLifecycleMs = 0;
    u32 timingObjectsMs = 0;
    u32 timingTailMs = 0;
    u32 timingReadyMs = 0;
    u32 readyNearbyRoleCount = 0;
    if (outCap < pos || !vm_net_mock_is_scene_resource_followup_request(request, requestLen))
        return 0;

    currentScene = vm_net_mock_current_scene_name();
    vm_net_mock_note_startup_sce_runtime_ready(currentScene);
    includeSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                        vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    if (g_vm_net_mock_last_scene_change_target_valid &&
        g_vm_net_mock_last_scene_change_target.sceneEnterPosinfoSent)
    {
        (void)vm_net_mock_scene_client_note_runtime_ready(
            g_vm_net_mock_last_scene_change_target.scene);
    }
    completeTeleportResourceEnter =
        g_vm_net_mock_teleport_stone_direct_enter_pending &&
        g_vm_net_mock_last_scene_change_target_valid &&
        currentScene != NULL &&
        vm_net_mock_scene_names_equal_exact(
            currentScene,
            g_vm_net_mock_last_scene_change_target.scene);

    /*
     * Ordinary portal WT2/3 has already supplied the one coordinate-bearing
     * 30/2 and therefore created the destination scene shell.  The next
     * WT6/1 resource request must be built for that recorded target and close
     * with a no-posinfo acknowledgement.  Using the still-persisted source
     * scene here either loaded the old resource set or appended another 30/1,
     * both of which restart/stall the new scene lifecycle.
     */
    if (g_vm_net_mock_last_scene_change_target_valid &&
        g_vm_net_mock_last_scene_change_target.sceneEnterPosinfoSent)
    {
        positionedPortalTarget = g_vm_net_mock_last_scene_change_target;
        positionedPortalEnterPending = true;
        completePositionedPortalEnter =
            vm_net_mock_prepare_scene_enter_resources(&positionedPortalTarget,
                                                      positionedPortalMissingResource,
                                                      sizeof(positionedPortalMissingResource));
        g_vm_net_mock_last_scene_change_target = positionedPortalTarget;
    }

    if (completeTeleportResourceEnter)
    {
        vm_net_mock_scene_change_target target =
            g_vm_net_mock_last_scene_change_target;
        u32 objectStart = 0;

        /*
         * This WT6/1 is emitted by scene_runtime_init_and_sync only after the
         * resource queue's final WT18/7 install callback has returned.  It is
         * therefore the first packet-visible point where the teleport can be
         * closed without racing DrawSceneMapLayer.  Complete with a no-posinfo
         * 30/2 so ResetDownloadState runs without a second scene-position
         * entry.
         */
        /*
         * `append_scene_enter_object_for_scene()` armed this exact target's
         * pending one-shot directory with the preceding 30/1.  This WT6/1 is
         * the first parser-safe request after the resource queue completed,
         * so it must be allowed to consume that pending seed before 30/2
         * closes the scene transition.  Passing false here silently omitted
         * 27/11 for every resource-download teleport, leaving the fresh scene
         * shell without NPC nodes.
         */
        if (!vm_net_mock_append_scene_npc_lifecycle_seed(out, outCap, &pos,
                                                         &objectCount,
                                                         target.scene,
                                                         true, true))
            return 0;
        if (!vm_net_mock_append_scene_resource_followup_objects(
                out, outCap, &pos, &objectCount, target.scene,
                includeSkillBooks, true, true, true,
                false, false, false))
            return 0;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2,
                                         &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2,
                                                       target.scene))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);

        target.sceneCompletionSent = true;
        vm_net_mock_mark_completed_scene_change_target(&target);
        vm_net_mock_save_player_pos_state(target.scene, target.x, target.y,
                                          "teleport-resource-followup");
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
        g_vm_net_mock_teleport_stone_direct_enter_pending = false;
        g_vm_net_mock_teleport_stone_map_enter_pending = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        (void)vm_mock_service_mark_active_session_scene_ready_from_role(
            target.scene, "teleport-resource-followup");
        printf("[info][network] mock_teleport_resource_followup_complete scene=%s pos=(%u,%u) objects=%u resp=%u completion=30/2-no-posinfo-after-WT18/7 evidence=JianghuOL.CBE:0x01037000+0x01039770\n",
               target.scene, target.x, target.y, objectCount, pos);
        vm_autotest_note("mock_teleport_resource_followup_complete scene=%s pos=(%u,%u) objects=%u response=resources+30/2-no-posinfo evidence=WT6/1-after-final-WT18/7\n",
                         target.scene, target.x, target.y, objectCount);
        return pos;
    }
    if (positionedPortalEnterPending && !completePositionedPortalEnter)
    {
        /* A 30/1 (direct instance entry) or position-bearing 30/2 has already
         * created this target's shell.  While an invalidated SCE/Actor is still
         * pending, acknowledge only the families requested by WT6/1.  Neither
         * another 30/1 nor the closing 30/2 is valid before the final WT18/7. */
        if (!vm_net_mock_append_scene_resource_followup_objects_ex(
                out, outCap, &pos, &objectCount,
                positionedPortalTarget.scene, includeSkillBooks,
                true, true, true, false, false, false, false))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        vm_net_mock_defer_scene_enter_completion(
            &positionedPortalTarget, "scene-resource-positioned-enter",
            positionedPortalMissingResource);
        printf("[info][network] mock_scene_resource_positioned_enter_deferred scene=%s missing=%s objects=%u resp=%u completion=none evidence=30/1-or-30/2-shell+WT18/7-pending\n",
               positionedPortalTarget.scene,
               positionedPortalMissingResource[0] ?
                   positionedPortalMissingResource : "-",
               objectCount, pos);
        vm_autotest_note("mock_scene_resource_positioned_enter_deferred scene=%s missing=%s objects=%u response=resources-no-30/1-no-30/2 evidence=WT6/1-before-final-WT18/7\n",
                         positionedPortalTarget.scene,
                         positionedPortalMissingResource[0] ?
                             positionedPortalMissingResource : "-",
                         objectCount);
        return pos;
    }
    if (completePositionedPortalEnter)
    {
        u32 objectStart = 0;

        if (!vm_net_mock_append_scene_npc_lifecycle_seed(
                out, outCap, &pos, &objectCount,
                positionedPortalTarget.scene, true, true))
        {
            return 0;
        }
        if (!vm_net_mock_append_scene_resource_followup_objects(
                out, outCap, &pos, &objectCount,
                positionedPortalTarget.scene,
                includeSkillBooks, true, true, true,
                false, false, false))
        {
            return 0;
        }
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2,
                                         &objectStart))
        {
            return 0;
        }
        if (!vm_net_mock_put_scene_ack_without_posinfo(
                out, outCap, &pos, 2, positionedPortalTarget.scene))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);

        positionedPortalTarget.sceneCompletionSent = true;
        vm_net_mock_mark_completed_scene_change_target(&positionedPortalTarget);
        vm_net_mock_save_player_pos_state(positionedPortalTarget.scene,
                                          positionedPortalTarget.x,
                                          positionedPortalTarget.y,
                                          "scene-resource-positioned-portal-followup");
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
        g_vm_net_mock_teleport_stone_map_enter_pending = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        (void)vm_mock_service_mark_active_session_scene_ready_from_role(
            positionedPortalTarget.scene,
            "scene-resource-positioned-portal-followup");
        printf("[info][network] mock_scene_resource_positioned_portal_complete scene=%s pos=(%u,%u) objects=%u resp=%u completion=resources+30/2-no-posinfo\n",
               positionedPortalTarget.scene, positionedPortalTarget.x,
               positionedPortalTarget.y, objectCount, pos);
        vm_autotest_note("mock_scene_resource_positioned_portal_complete scene=%s pos=(%u,%u) objects=%u response=resources+30/2-no-posinfo evidence=JianghuOL.CBE:0x01039770\n",
                         positionedPortalTarget.scene, positionedPortalTarget.x,
                         positionedPortalTarget.y, objectCount);
        return pos;
    }
    startupSceneAlreadyEntered = g_vm_net_mock_title_role_scene_followup_pending ||
                                 (!g_vm_net_mock_last_scene_change_target_valid &&
                                  !g_vm_net_mock_last_completed_scene_change_target_valid);
    recentCompletedScene =
        !g_vm_net_mock_last_scene_change_target_valid &&
        currentScene != NULL &&
        vm_net_mock_is_recent_completed_scene_name(currentScene, 90);
    /*
     * current-scene-reload has already sent its 30/1.  Its next WT 12/1 is
     * therefore the first parser-safe point to deliver 27/11: the new mmGame
     * scene shell exists, while sending another position-bearing enter would
     * restart loading.  This provenance must not be inferred from the scene
     * name or from the old completed target, because both can refer to the
     * shell that was just discarded.
     */
    currentSceneReload =
        !g_vm_net_mock_last_scene_change_target_valid &&
        currentScene != NULL &&
        vm_net_mock_is_recent_current_scene_reload(currentScene, 90);
    sceneShellAlreadyEntered = startupSceneAlreadyEntered || currentSceneReload;
    /*
     * This is session-scoped provenance from the actual shop-open request.
     * Unlike the generic completed-scene reuse guard, it remains valid until
     * this same scene has received the one matching mmShop -> mmGame return
     * completion (or the session changes scenes).  A player can legitimately
     * spend longer than the 90-tick reuse window in the shop.
     */
    /* A returned mmShop shell has discarded its mmGame scene tables even when
     * the map name did not change.  Both the legacy marker and the grid
     * bootstrap marker must therefore enter through the normal 30/1 ->
     * post-enter 30/2 lifecycle.  Delivering 27/11 plus a no-posinfo 30/2 to
     * the discarded shell leaves the new scene on its loading page. */
    shopReturnReload =
        !g_vm_net_mock_last_scene_change_target_valid &&
        currentScene != NULL &&
        (vm_mock_service_shop_scene_npc_reseed_requires_scene_enter(currentScene) ||
         (vm_mock_service_shop_scene_npc_reseed_matches(currentScene) &&
          vm_mock_service_shop_scene_npc_reseed_is_bootstrap_only()));
    if (shopReturnReload &&
        !vm_net_mock_get_shop_return_persisted_position(
            currentScene, &shopReturnX, &shopReturnY))
    {
        printf("[error][network] mock_shop_return_reenter_rejected scene=%s "
               "reason=missing-exact-persisted-role-position contract=30/2-posinfo\n",
               currentScene ? currentScene : "-");
        return 0;
    }
    if (shopReturnReload)
    {
        /* The scene shell does not exist yet.  In particular, no NPC/task or
         * actor object may be delivered before this 30/1 has installed it. */
        return vm_net_mock_build_shop_scene_return_enter_response(
            out, outCap, currentScene, shopReturnX, shopReturnY,
            "scene-resource-followup");
    }
    /* The paired completed-scene response sends the required empty 27/11 gate
     * object and leaves this one-shot catalog pending. WT6/1 is the first
     * client-requested scene-runtime phase after that completion, so it owns
     * the non-empty 27/11 NPC creation data.
     *
     * Do not scope this by a destination name or transfer mechanism. The
     * contract is identified by the matching pending-but-unseeded catalog for
     * the just-completed current scene.
     * Ordinary completions have already consumed that catalog, and a real shop
     * return retains its own explicit session marker. */
    vm_net_mock_reset_scene_moveinfo_npc_seed_if_needed(currentScene);
    deferredTeleportNpcSeedAfterCurrentCompletion =
        recentCompletedScene &&
        !shopReturnReload &&
        currentScene != NULL &&
        !g_vm_net_mock_scene_moveinfo_npc_seeded &&
        g_vm_net_mock_scene_moveinfo_npc_pending &&
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] != 0 &&
        vm_net_mock_scene_names_equal_exact(
            g_vm_net_mock_scene_moveinfo_npc_pending_scene, currentScene);
    if (!g_vm_net_mock_last_scene_change_target_valid &&
        currentScene != NULL &&
        (recentCompletedScene || shopReturnReload))
    {
        /*
         * Runtime repeat after visible scene entry:
         * - the client can re-emit the full WT49 resource/task/other request
         *   after the same scene has already completed;
         * - appending another trailing 30/1 here re-enters the same scene
         *   shell and shows the extra loading pass the user observes.
         *
         * Keep the requested resource/task objects, but acknowledge this as a
         * post-enter refresh only. The scene is already live on screen.
         *
         * mmShop is different: returning from it constructs a fresh mmGame
         * shell. Its session-scoped marker is handled by the early 30/1
         * return above, before any current-shell objects are serialized.
         */
        if (deferredTeleportNpcSeedAfterCurrentCompletion)
        {
            if (!vm_net_mock_append_scene_npcs11_once_or_empty(
                    out, outCap, &pos, currentScene,
                    "completed-scene-runtime-followup"))
            {
                return 0;
            }
            objectCount += 1;
            printf("[info][network] mock_scene_npc_seed_deliver scene=%s phase=WT6/1 after=completed-scene-runtime-init\n",
                   currentScene);
        }
        else if (!vm_net_mock_append_scene_npc_lifecycle_seed(
                     out, outCap, &pos, &objectCount, currentScene, false, true))
        {
            return 0;
        }
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               currentScene,
                                                               includeSkillBooks, true, true, true,
                                                               false, false, false))
        {
            return 0;
        }
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[info][network] mock_scene_resource_followup_repeat_ack scene=%s objects=%u resp=%u age=%u recent=%u shop_return=%u completion=none\n",
               currentScene,
               objectCount,
               pos,
               g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick,
               recentCompletedScene ? 1u : 0u,
               0u);
        vm_autotest_note("mock_scene_resource_followup_repeat_ack scene=%s objects=%u response=%s age=%u evidence=JianghuOL.CBE:0x01039770+0x0103993C\n",
                          currentScene,
                          objectCount,
                          "resource-followup-no-30/1",
                          g_schedulerTick - g_vm_net_mock_last_completed_scene_change_tick);
        return pos;
    }
    hasActorOtherRequestType = vm_net_mock_get_object_u8_field(request, requestLen, "Type", &actorOtherRequestType);
    if (g_vm_net_mock_last_scene_change_target_valid)
    {
        downloadedTarget = g_vm_net_mock_last_scene_change_target;
        if (downloadedTarget.needsSceneDownload &&
            vm_net_mock_refresh_downloaded_scene_change_target(&downloadedTarget))
        {
            useDownloadedTarget = true;
        }
    }
    if (useDownloadedTarget)
    {
        if (!vm_net_mock_append_scene_resource_followup_objects(out, outCap, &pos, &objectCount,
                                                               downloadedTarget.scene,
                                                               includeSkillBooks, true, true, true,
                                                               false, false, false))
            return 0;
        if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                            downloadedTarget.scene,
                                                            downloadedTarget.x,
                                                            downloadedTarget.y))
            return 0;
        objectCount += 1;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        g_vm_net_mock_last_scene_change_target = downloadedTarget;
        g_vm_net_mock_last_scene_change_target_valid = true;
        printf("[info][network] mock_scene_download_enter_followup scene=%s pos=(%u,%u) objects=%u resp=%u\n",
               downloadedTarget.scene, downloadedTarget.x, downloadedTarget.y,
               objectCount, pos);
        vm_autotest_note("mock_scene_download_enter_followup scene=%s pos=(%u,%u) response=resource-followup+30/1\n",
                         downloadedTarget.scene, downloadedTarget.x, downloadedTarget.y);
        return pos;
    }
    sceneSupportsActorOtherNpcSeed = vm_net_mock_scene_supports_actor_other_npc_seed(currentScene);
    /*
     * Runtime evidence from the latest manual runs:
     * - keepBusinessGate=1 emits only trailing 30/2 ack and never reaches
     *   LoadSceneDataFromStream(0x01006204) or ParseMinfoAndSpawnNPCs(0x010159DA).
     * - Without those parsers, scene-owned NPC resources never materialize.
     *
     * Default back to real 30/1 scene enter so the client can load the scene
     * and drive NPC spawning through normal scene-resource callbacks. Keep the
     * env knob for quick rollback while we validate downstream HUD effects.
     */
    keepBusinessGate = includeSkillBooks &&
                       vm_net_mock_env_u8("CBE_FIRST_SCENE_KEEP_BUSINESS_GATE", 0) != 0;
    /*
     * The client's standalone 2/10 Type=1 request arrives after the business
     * scene dispatch gate closes, so it is decoded into the shared container
     * but does not create visible scene nodes.  A first-scene follow-up seed is
     * useful as an explicit probe while the gate is still open, but latest
     * runtime evidence showed the default-on Type101 row can create a scene
     * node with a null draw callback (pc=0, lr=01014597, lastPc=01014594), so
     * keep it env-gated until the actor visual/callback contract is proven.
     * Penglai _02 remains gated off by vm_net_mock_scene_supports_actor_other_npc_seed().
     */
    probeActorOtherNpcInFollowup = vm_net_mock_env_u8("CBE_SCENE_FOLLOWUP_NPC_OTHERINFO", 0) != 0;
    actorOtherNpcSeedInFollowup = !keepBusinessGate &&
                                  includeSkillBooks &&
                                  hasActorOtherRequestType &&
                                  actorOtherRequestType == 101 &&
                                  sceneSupportsActorOtherNpcSeed &&
                                  probeActorOtherNpcInFollowup;
    preferActorOtherNpcRows = actorOtherNpcSeedInFollowup;
    startupNearbyInRequestedObject = startupSceneAlreadyEntered &&
                                       !preferActorOtherNpcRows;
    sceneNpcInfo11SeedInFollowup = !keepBusinessGate &&
                                   includeSkillBooks &&
                                   vm_net_mock_env_u8("CBE_SCENE_FOLLOWUP_NPCINFO11", 0) != 0 &&
                                   vm_net_mock_scene_room_npc_seed_count(currentScene) > 0;
    sceneNpcActorInfoSeedInFollowup = !keepBusinessGate &&
                                      includeSkillBooks &&
                                      sceneSupportsActorOtherNpcSeed &&
                                      vm_net_mock_env_u8("CBE_SCENE_FOLLOWUP_NPC_ACTORINFO", 0) != 0;
    /*
     * Narrow first-scene contract:
     * - answer the objects the client actually asked for in WT 49
     *   (12/1, 7/42, 6/1, 6/13, 6/14, 2/10, 25/5),
     * - then append scene enter 30/1.
     *
     * Do not append 30/3 room+npc data by default here. IDA evidence shows
     * scene-channel subtype 3 (0x01039222) consumes curpage/pagenum/roomnum/
     * roomlist and does not read npcnum/npcinfo. 27/11 remains an env-gated
     * probe only: runtime confirms it reaches scene_parse_npcinfo_and_spawn_npcs
     * (0x01037998), but both _02 and first-scene non-empty rows crashed in the
     * actor visual resolver before scene_npcinfo_create_return.
     */
    timingStartMs = scheduler_get_tick_ms();
    if (!vm_net_mock_append_scene_npc_lifecycle_seed(out, outCap, &pos, &objectCount,
                                                    currentScene,
                                                    sceneShellAlreadyEntered,
                                                    !g_vm_net_mock_last_scene_change_target_valid))
    {
        return 0;
    }
    timingLifecycleMs = scheduler_get_tick_ms();
    if (!vm_net_mock_append_scene_resource_followup_objects_ex(out, outCap, &pos, &objectCount,
                                                              currentScene,
                                                              includeSkillBooks, true, true, true,
                                                              false, false,
                                                              preferActorOtherNpcRows,
                                                              startupNearbyInRequestedObject))
        return 0;
    timingObjectsMs = scheduler_get_tick_ms();
    appendSceneRoomNpcAfterEnter = !keepBusinessGate &&
                                   vm_net_mock_scene_room_npc_seed_count(currentScene) > 0 &&
                                   vm_net_mock_env_u8("CBE_SCENE_FOLLOWUP_ROOM_NPC", 0) != 0;
    if (keepBusinessGate || sceneShellAlreadyEntered)
    {
        u32 objectStart = 0;
        /*
         * scene_runtime_init_and_sync(0x01012FB4) constructs WT 12/1 only at
         * 0x010137CA, after the first scene screen has already loaded its map,
         * actor and HUD state from the role-select subtype-6 actorinfo.  A
         * trailing 30/1 with posinfo is consumed by
         * scene_handle_enter_with_scene_pos(0x010396D6) and starts the same
         * scene lifecycle again. Keep a 30/2 completion ack without posinfo so
         * the request closes without calling the scene-enter path.
         */
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2, &objectStart))
            return 0;
        if (!vm_net_mock_put_scene_ack_without_posinfo(out, outCap, &pos, 2, vm_net_mock_scene_key_name()))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
    }
    else if (!vm_net_mock_append_scene_enter_object(out, outCap, &pos))
    {
        return 0;
    }
    objectCount += 1;

    if (sceneNpcInfo11SeedInFollowup)
    {
        /*
         * Runtime evidence:
         * - SCE-local nodes are created with draw/step callbacks but
         *   visualRes=0 because visual=0,0 makes scene_node_refresh_visual()
         *   read node-4.
         * - 27/11 npcinfo is the server packet path that calls the resource
         *   resolver and writes node+0x24. On transfer it follows 30/1; on
         *   first login subtype-6 has already initialized the same scene shell,
         *   so the trailing object is a 30/2 completion ack.
         */
        if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                           currentScene,
                                                           "scene-resource-followup-probe"))
            return 0;
        objectCount += 1;
    }
    if (sceneNpcActorInfoSeedInFollowup)
    {
        /*
         * Runtime negative: placing 2/5 actorinfo before 30/1 leaves no visible
         * NPC. IDA shows 30/1 enters the scene-channel loader path, so seed
         * actorinfo after scene enter in the same open dispatch window.
         */
        if (!vm_net_mock_append_scene_actorinfo_npc_object(out, outCap, &pos,
                                                          currentScene,
                                                          &sceneNpcActorInfoLen,
                                                          &sceneNpcActorId))
            return 0;
        objectCount += 1;
    }
    if (appendSceneRoomNpcAfterEnter)
    {
        if (!vm_net_mock_append_scene_room_npc_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    timingTailMs = scheduler_get_tick_ms();

    if (startupSceneAlreadyEntered)
    {
        u8 readyChatCapacity =
            objectCount < VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX
                ? (u8)(VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX - objectCount)
                : 0;
        int readyChatObjects = vm_net_mock_append_scene_ready_chat_objects(
            out, outCap, &pos, currentScene, "scene-resource-followup",
            readyChatCapacity);
        if (readyChatObjects < 0)
            return 0;
        objectCount = (u8)(objectCount + readyChatObjects);

        if (startupNearbyInRequestedObject)
        {
            readyNearbyRoleCount =
                vm_mock_service_mark_scene_nearby_role_baseline_visible(currentScene);
            if (readyNearbyRoleCount > 0)
            {
                printf("[info][network] mock_scene_resource_ready_nearby scene=%s nearby_roles=%u objects=%u resp=%u delivery=requested-2/10 history_replay=0 evidence=JianghuOL.CBE:0x01012958\n",
                       currentScene ? currentScene : "-",
                       readyNearbyRoleCount,
                       objectCount,
                       pos);
            }
        }
    }
    timingReadyMs = scheduler_get_tick_ms();

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    if (shopReturnReload)
    {
        vm_mock_service_arm_shop_scene_return_post_enter(
            currentScene, shopReturnX, shopReturnY,
            "scene-resource-followup");
        vm_mock_service_complete_shop_scene_return(
            currentScene, "scene-resource-followup");
    }
    if (currentSceneReload)
    {
        vm_net_mock_consume_current_scene_reload(currentScene);
        printf("[info][network] mock_scene_current_reload_npc_seed scene=%s catalog=%u completion=30/2-no-posinfo evidence=JianghuOL.CBE:0x01037998+0x01039770\n",
               currentScene ? currentScene : "-",
               g_vm_net_mock_scene_moveinfo_npc_seeded ? 1u : 0u);
    }
    if (startupSceneAlreadyEntered)
        vm_net_mock_complete_startup_scene_followup(currentScene,
                                                    "scene-resource-followup",
                                                    objectCount,
                                                    pos);
    /* complete_startup_scene_followup intentionally performs a final
     * pending->ready transition and clears peerSync[].visible.  The baseline
     * is already in this response, so restore the observer cursor after that
     * lifecycle transition instead of making the next poll send it again. */
    if (readyNearbyRoleCount > 0)
        (void)vm_mock_service_mark_scene_nearby_role_baseline_visible(currentScene);
    {
        u32 timingEndMs = scheduler_get_tick_ms();
        printf("[debug][mock-service] scene_resource_followup_timing scene=%s total_ms=%u lifecycle_ms=%u objects_ms=%u tail_ms=%u ready_ms=%u complete_ms=%u\n",
               currentScene ? currentScene : "-",
               timingEndMs - timingStartMs,
               timingLifecycleMs - timingStartMs,
               timingObjectsMs - timingLifecycleMs,
               timingTailMs - timingObjectsMs,
               timingReadyMs - timingTailMs,
               timingEndMs - timingReadyMs);
    }
    if (actorOtherNpcSeedInFollowup)
    {
        g_vm_net_mock_scene_moveinfo_npc_pending = false;
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
        g_vm_net_mock_scene_moveinfo_npc_seeded = true;
        snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                 sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene),
                 "%s", currentScene ? currentScene : "");
    }
    else if (!keepBusinessGate &&
        includeSkillBooks &&
        !appendSceneRoomNpcAfterEnter &&
        !preferActorOtherNpcRows &&
        vm_net_mock_env_u8("CBE_POST_SCENE_MOVEINFO_NPC_SEED", 0) != 0)
    {
        const char *pendingScene = currentScene;
        if (vm_net_mock_scene_supports_actor_other_npc_seed(pendingScene))
        {
            vm_net_mock_mark_scene_moveinfo_npc_seed_pending(pendingScene);
        }
    }
    return pos;
}

static u32 vm_net_mock_build_compact_scene_skill_default_response(const u8 *request, u32 requestLen,
                                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;

    if (outCap < pos || !vm_net_mock_is_compact_scene_resource_followup_request(request, requestLen))
        return 0;
    if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos, &objectCount,
                                                      false))
        return 0;
    if (!vm_net_mock_append_info_banner_result5_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_scene_compact_skill_default objects=%u resp=%u\n",
           objectCount, pos);
    vm_autotest_note("mock_scene_compact_skill_default objects=%u response=12/1+7/42+17/1+25/5 evidence=runtime:wt12/1-len19-12/1+7/42+25/5\n",
                     objectCount);
    return pos;
}

static u32 vm_net_mock_build_scene_task_subset_followup_response(const u8 *request, u32 requestLen,
                                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    bool completeDeferredScene = g_vm_net_mock_last_scene_change_target_valid;
    bool appendDeferredSceneCompletion = false;
    bool startupSceneAlreadyEntered = g_vm_net_mock_title_role_scene_followup_pending;
    const char *currentScene = NULL;
    const char *nearbyScene = NULL;
    bool seedSubsetNpcOther = false;
    bool seedSubsetNpcActorInfo = false;
    bool startupNearbyInRequestedObject = false;
    bool includeSkillBooks = false;
    bool primaryTaskSubsetNeedsFb11Ack = false;
    bool sceneNpcLifecycleAppended = false;
    bool shopReturnReload = false;
    u16 shopReturnX = 0;
    u16 shopReturnY = 0;
    const char *responseScene = NULL;
    u32 subsetNpcActorInfoLen = 0;
    u32 subsetNpcActorId = 0;
    u32 nearbyRoleCount = 0;
    u32 nearbyOtherInfoLen = 0;
    u8 nearbyMoveinfoCount = 0;
    char missingResource[64];
    if (outCap < pos || !vm_net_mock_is_scene_task_subset_followup_request(request, requestLen))
        return 0;

    primaryTaskSubsetNeedsFb11Ack =
        vm_net_mock_is_primary_scene_task_subset_with_fb11_request(request,
                                                                     requestLen);

    /*
     * This post-scene-change request is the WT49 task/other/banner subset
     * without skill/book objects. Runtime shows this is the first scene
     * follow-up after the 30/2 scene-change ack where dispatchGate is still 1,
     * while the later standalone 2/10 Type=1 response arrives with
     * dispatchGate=0 and is only decoded into the shared container.
     *
     * Keep NPC Type101 rows env-gated for the same reason as the first-scene
     * follow-up: the current row shape is parsed, but can leave the scene draw
     * callback at +0x148 unset and crash at scene_draw_actor_pass(0x01014594).
     */
    currentScene = vm_net_mock_current_scene_name();
    vm_net_mock_note_startup_sce_runtime_ready(currentScene);
    includeSkillBooks = vm_net_mock_request_contains_object(request, requestLen, 1, 0x0c, 1) &&
                        vm_net_mock_request_contains_object(request, requestLen, 1, 7, 42);
    if (completeDeferredScene)
    {
        vm_net_mock_scene_change_target target = g_vm_net_mock_last_scene_change_target;

        /* This composite request is emitted from the same runtime-init phase
         * as standalone WT6/1. Once the target SCE itself is no longer
         * pending, any remaining counted Actor/effect entries that the client
         * did not request are cache hits, not a reason to hold the target
         * forever. Do not make that inference while the SCE is still pending:
         * that would falsely treat a missing scene as already installed. */
        if (!vm_net_mock_content_client_resource_pending(
                g_vm_mock_service_active_client_id, target.scene))
        {
            (void)vm_net_mock_scene_client_note_runtime_ready(target.scene);
        }
        if (!vm_net_mock_prepare_scene_enter_resources(&target,
                                                       missingResource,
                                                       sizeof(missingResource)))
        {
            u8 deferredObjectCount = 0;

            vm_net_mock_defer_scene_enter_completion(&target,
                                                     "scene-task-subset-followup",
                                                     missingResource);
            /* This callback belongs to the destination loader, but its SCE or
             * Actor dependency is still being installed. Acknowledge the
             * requested families against the destination without seeding
             * scene nodes and without closing the loader through WT30/2. */
            if (!vm_net_mock_append_scene_resource_followup_objects_ex(
                    out, outCap, &pos, &deferredObjectCount, target.scene,
                    includeSkillBooks, true, true, true, false, false,
                    false, false))
            {
                return 0;
            }
            if (primaryTaskSubsetNeedsFb11Ack)
            {
                if (!vm_net_mock_append_fb_target_empty11_object(
                        out, outCap, &pos))
                {
                    return 0;
                }
                deferredObjectCount += 1;
            }
            vm_net_mock_finish_wt_packet(out, pos, deferredObjectCount);
            printf("[info][network] mock_scene_task_subset_deferred "
                   "scene=%s missing=%s objects=%u resp=%u completion=none "
                   "evidence=WT18/7-dependency-pending\n",
                   target.scene, missingResource[0] ? missingResource : "-",
                   deferredObjectCount, pos);
            vm_autotest_note("mock_scene_task_subset_deferred scene=%s "
                             "missing=%s objects=%u completion=none\n",
                             target.scene,
                             missingResource[0] ? missingResource : "-",
                             deferredObjectCount);
            return pos;
        }
        else
        {
            g_vm_net_mock_last_scene_change_target = target;
            appendDeferredSceneCompletion = !target.sceneCompletionSent;
        }
    }
    if (completeDeferredScene &&
        currentScene != NULL &&
        vm_net_mock_scene_is_penglai03(currentScene) &&
        vm_net_mock_scene_names_equal_exact(currentScene, g_vm_net_mock_last_scene_change_target.scene))
    {
        /*
         * Runtime _02 -> _03 already consumed the real scene-enter callback from
         * the preceding WT 2/3 response. Repeating a second 30/2 completion
         * here drives scene_handle_change_result_scene_pos() again and produces
         * the visible extra loading-bar loop the user still sees on first entry.
         *
         * Let the later same-target WT 2/3 repeat-ack close the deferred scene
         * transition; this 6/1 subset should stay on the lighter task/banner
         * path only.
         */
        completeDeferredScene = false;
    }
    /*
     * A pending target is authoritative for a scene-change completion.  The
     * role/session still names the source scene until this response completes,
     * so using currentScene below would serialize the old map's task/resource
     * rows into the destination scene callback.  This is especially important
     * now that composite 25/5 requests remain in their own 6/* handler rather
     * than being claimed by the standalone mmGame default-event handler.
     */
    responseScene = completeDeferredScene
                        ? g_vm_net_mock_last_scene_change_target.scene
                        : currentScene;
    shopReturnReload =
        !completeDeferredScene &&
        currentScene != NULL &&
        (vm_mock_service_shop_scene_npc_reseed_requires_scene_enter(currentScene) ||
         (vm_mock_service_shop_scene_npc_reseed_matches(currentScene) &&
          vm_mock_service_shop_scene_npc_reseed_is_bootstrap_only()));
    if (shopReturnReload &&
        !vm_net_mock_get_shop_return_persisted_position(
            responseScene, &shopReturnX, &shopReturnY))
    {
        printf("[error][network] mock_shop_return_reenter_rejected scene=%s "
               "reason=missing-exact-persisted-role-position contract=30/2-posinfo\n",
               responseScene ? responseScene : "-");
        return 0;
    }
    if (shopReturnReload)
    {
        /* See vm_net_mock_build_shop_scene_return_enter_response(): this
         * request is still owned by the outgoing shop shell, so it may only
         * start the new scene.  Its post-enter request owns all scene data. */
        return vm_net_mock_build_shop_scene_return_enter_response(
            out, outCap, responseScene, shopReturnX, shopReturnY,
            "scene-task-subset-followup");
    }
    /* scene_runtime_init_and_sync(0x01012FB4) issues this subset only after
     * rebuilding the 25-node scene table. Put 27/11 before 6/1 and 6/14 so
     * their prompt refresh sees the freshly created NPC nodes. */
    {
        u8 objectCountBeforeLifecycle = objectCount;

        if (!vm_net_mock_append_scene_npc_lifecycle_seed(
                out, outCap, &pos, &objectCount, responseScene,
                startupSceneAlreadyEntered && !completeDeferredScene,
                !completeDeferredScene))
        {
            return 0;
        }
        sceneNpcLifecycleAppended = objectCount != objectCountBeforeLifecycle;
    }
    if (primaryTaskSubsetNeedsFb11Ack && !sceneNpcLifecycleAppended)
    {
        /*
         * The preceding 2/3 already delivered the one-shot NPC directory for
         * this scene shell.  A second non-empty catalog would recreate those
         * nodes, but the explicit 27/11 request still needs its parser-safe
         * acknowledgement before the coalesced 25/5 callback can complete.
         */
        if (!vm_net_mock_append_fb_target_empty11_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        printf("[info][network] mock_scene_task_subset_fb11_ack scene=%s request=25/5+6/1+6/13+6/14+2/10+27/11 response=empty-27/11 reason=npc-catalog-already-delivered\n",
               responseScene ? responseScene : "-");
    }
    seedSubsetNpcOther = vm_net_mock_scene_supports_actor_other_npc_seed(responseScene) &&
                         vm_net_mock_env_u8("CBE_SCENE_TASK_SUBSET_NPC_OTHERINFO", 0) != 0;
    seedSubsetNpcActorInfo = vm_net_mock_scene_supports_actor_other_npc_seed(responseScene) &&
                         vm_net_mock_env_u8("CBE_SCENE_TASK_SUBSET_NPC_ACTORINFO", 0) != 0;
    startupNearbyInRequestedObject = startupSceneAlreadyEntered &&
                                       !completeDeferredScene &&
                                       !seedSubsetNpcOther;
    if (!vm_net_mock_append_scene_resource_followup_objects_ex(out, outCap, &pos, &objectCount,
                                                              responseScene,
                                                              includeSkillBooks, true, true, true,
                                                              false, false,
                                                              seedSubsetNpcOther,
                                                              startupNearbyInRequestedObject))
        return 0;

    if (completeDeferredScene)
    {
        const vm_net_mock_scene_change_target *target = &g_vm_net_mock_last_scene_change_target;
        if (!vm_net_mock_append_fb_target_result12_for_scene(out, outCap, &pos, target->scene, target->x, target->y))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_scene_npcs11_once_or_empty(out, outCap, &pos,
                                                           target->scene,
                                                           "scene-task-subset-completion"))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_fb_target_result4_object(out, outCap, &pos,
                                                        g_vm_net_mock_last_scene_change_fb4_type,
                                                        vm_net_mock_fb_target_info_text()))
            return 0;
        objectCount += 1;
        if (appendDeferredSceneCompletion)
        {
            u32 sceneObjectStart = 0;

            if (objectCount >= VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX)
            {
                printf("[error][network] mock_scene_task_subset_completion "
                       "scene=%s objects=%u action=reject-over-dispatch-limit\n",
                       target->scene, objectCount);
                return 0;
            }
            if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x1e, 2,
                                             &sceneObjectStart) ||
                !vm_net_mock_put_scene_ack_without_posinfo(
                    out, outCap, &pos, 2, target->scene))
            {
                return 0;
            }
            vm_net_mock_finish_wt_object(out, sceneObjectStart, pos);
            objectCount += 1;
            g_vm_net_mock_last_scene_change_target.sceneCompletionSent = true;
            printf("[info][network] mock_scene_task_subset_completion "
                   "scene=%s response=30/2-no-posinfo reason=scene-shell-runtime-prerequisites-ready\n",
                   target->scene);
        }
        vm_net_mock_mark_completed_scene_change_target(target);
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_teleport_stone_map_enter_pending = false;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        nearbyScene = target->scene;
        if (g_vm_net_mock_teleport_stone_direct_enter_pending)
        {
            g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
            g_vm_net_mock_teleport_stone_direct_enter_pending = false;
            printf("[info][network] mock_teleport_stone_direct_enter_followup scene=%s pos=(%u,%u) objects=%u resp=%u\n",
                   target->scene, target->x, target->y, objectCount, pos);
            vm_autotest_note("mock_teleport_stone_direct_enter_followup scene=%s pos=(%u,%u) objects=%u response=scene-task-subset evidence=runtime:post-30/1-task-subset\n",
                             target->scene, target->x, target->y, objectCount);
        }
    }
    else
    {
        nearbyScene = currentScene;
        if (!g_vm_net_mock_last_scene_change_target_valid &&
            !vm_net_mock_scene_runtime_pending_without_target())
        {
            (void)vm_mock_service_mark_active_session_scene_ready_from_role(
                currentScene,
                "scene-task-subset-followup");
        }
    }
    if (seedSubsetNpcActorInfo)
    {
        /*
         * Keep the same phase ordering as first scene enter: complete the
         * deferred scene/channel object first, then let actor_move case 5 create
         * NPC nodes after the scene table is initialized.
         */
        if (!vm_net_mock_append_scene_actorinfo_npc_object(out, outCap, &pos,
                                                          responseScene,
                                                          &subsetNpcActorInfoLen,
                                                          &subsetNpcActorId))
            return 0;
        objectCount += 1;
    }
    if (!startupNearbyInRequestedObject)
    {
        u8 addedNearbyObjects = vm_net_mock_append_scene_nearby_role_objects(out,
                                                                             outCap,
                                                                             &pos,
                                                                             nearbyScene,
                                                                             &nearbyRoleCount,
                                                                             &nearbyOtherInfoLen,
                                                                             &nearbyMoveinfoCount);
        if (addedNearbyObjects > 0)
        {
            objectCount = (u8)(objectCount + addedNearbyObjects);
            printf("[info][network] mock_scene_task_subset_nearby scene=%s nearby_roles=%u otherinfo_len=%u moveinfo=%u resp=%u\n",
                   nearbyScene ? nearbyScene : "-",
                   nearbyRoleCount,
                   nearbyOtherInfoLen,
                   nearbyMoveinfoCount,
                   pos);
        }
    }
    else
    {
        nearbyRoleCount =
            vm_mock_service_mark_scene_nearby_role_baseline_visible(nearbyScene);
        if (nearbyRoleCount > 0)
        {
            printf("[info][network] mock_scene_task_subset_nearby scene=%s nearby_roles=%u moveinfo=0 objects=%u resp=%u delivery=requested-2/10 history_replay=0 evidence=JianghuOL.CBE:0x01012958\n",
                   nearbyScene ? nearbyScene : "-",
                   nearbyRoleCount,
                   objectCount,
                   pos);
        }
    }

    if (!completeDeferredScene)
    {
        u8 readyChatCapacity =
            objectCount < VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX
                ? (u8)(VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX - objectCount)
                : 0;
        int readyChatObjects = vm_net_mock_append_scene_ready_chat_objects(
            out, outCap, &pos, currentScene, "scene-task-subset-followup",
            readyChatCapacity);
        if (readyChatObjects < 0)
            return 0;
        objectCount = (u8)(objectCount + readyChatObjects);
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    if (startupSceneAlreadyEntered && !completeDeferredScene)
        vm_net_mock_complete_startup_scene_followup(currentScene,
                                                    "scene-task-subset-followup",
                                                    objectCount,
                                                    pos);
    if (startupNearbyInRequestedObject && nearbyRoleCount > 0)
        (void)vm_mock_service_mark_scene_nearby_role_baseline_visible(currentScene);
    if (seedSubsetNpcOther)
    {
        g_vm_net_mock_scene_moveinfo_npc_pending = false;
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
        g_vm_net_mock_scene_moveinfo_npc_seeded = true;
        snprintf(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                 sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene),
                 "%s", currentScene ? currentScene : "");
    }
    return pos;
}

#ifndef CBE_SERVER_SPLIT_OBJECTS
static u32 vm_net_mock_build_empty_wt_ack_response(u8 *out, u32 outCap)
{
    if (outCap < 5)
        return 0;
    vm_net_mock_finish_wt_packet(out, 5, 0);
    return 5;
}

u32 vm_net_mock_build_short_wt_control_ack_response(const u8 *request, u32 requestLen,
                                                     u8 kind, u8 subtype,
                                                     u8 *out, u32 outCap)
{
    (void)request;
    (void)requestLen;
    (void)kind;
    (void)subtype;
    /*
     * 0x63/1 sits on the startup/login bridge. The old echo copied the
     * request-side short-object layout back to the client, but response parsing
     * uses the normal WT object layout. That malformed echo trips
     * event_packet_init() and shows the unpack-error popup. A zero-object WT ack
     * still delivers the network event without feeding business dispatch an
     * unsupported 0x63 object.
     */
    return vm_net_mock_build_empty_wt_ack_response(out, outCap);
}
#endif

#ifndef CBE_SERVER_SPLIT_OBJECTS
u32 vm_net_mock_build_practise_info18_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_practise_info info;

    if (outCap < pos || role == NULL ||
        !vm_net_mock_practise_get_info(role, &info))
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 18, &objectStart))
        return 0;
    /*
     * sub_102CB46 handles the active practise-info panel. For subtype 18 it
     * reads todaypasthour, todaypastmin, getexp, todaylasthour,
     * todaylastmin, alllasthour, alllastmin, then isgold.
     */
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaypasthour",
                                    info.todayPastHours))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaypastmin",
                                    info.todayPastMinutes))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "getexp", info.gainedExp))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaylasthour",
                                    info.todayRemainingHours))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "todaylastmin",
                                    info.todayRemainingMinutes))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "alllasthour",
                                    info.allRemainingHours))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "alllastmin",
                                    info.allRemainingMinutes))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "isgold", info.goldEnabled))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}
#endif

#ifndef CBE_SERVER_SPLIT_OBJECTS
/* JianghuOL.CBE:HandleTradeInput(0x0102C3D6) sends this exact one-object
 * request when the cultivation panel's help action is selected.  The
 * response parser (HandleExpBattleResponse, 0x0102CB46 case 19) reads only
 * `helpinfo` as a GBK byte string and then releases the pending dialog. */
static bool vm_net_mock_is_practise_help19_request(const u8 *request,
                                                   u32 requestLen)
{
    vm_net_mock_request_object object;
    u32 offset = 4;
    u32 requestType = 0;

    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                         &object) ||
        offset != requestLen || object.major != 1 || object.kind != 7 ||
        object.subtype != 19 ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "type", &requestType))
    {
        return false;
    }
    return requestType == 0;
}

u32 vm_net_mock_build_practise_help19_response(const u8 *request,
                                               u32 requestLen,
                                               u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    /* GBK: 修炼帮助：\r\n使用修炼丹可增加修炼时间，每颗增加1小时。\r\n
     * 角色离线后自动修炼。普通修炼每日最多8小时；黄金修炼每日最多4小时，经验翻倍。\r\n
     * 修炼时间最多累计100小时。 */
    static const char helpInfoGbk[] =
        "\xD0\xDE\xC1\xB6\xB0\xEF\xD6\xFA\xA3\xBA\x0D\x0A"
        "\xCA\xB9\xD3\xC3\xD0\xDE\xC1\xB6\xB5\xA4\xBF\xC9\xD4\xF6\xBC\xD3"
        "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xA3\xAC\xC3\xBF\xBF\xC5\xD4\xF6\xBC\xD3"
        "\x31\xD0\xA1\xCA\xB1\xA1\xA3\x0D\x0A"
        "\xBD\xC7\xC9\xAB\xC0\xEB\xCF\xDF\xBA\xF3\xD7\xD4\xB6\xAF\xD0\xDE\xC1\xB6"
        "\xA1\xA3\xC6\xD5\xCD\xA8\xD0\xDE\xC1\xB6\xC3\xBF\xC8\xD5\xD7\xEE\xB6\xE0"
        "\x38\xD0\xA1\xCA\xB1\xA3\xBB\xBB\xC6\xBD\xF0\xD0\xDE\xC1\xB6\xC3\xBF\xC8"
        "\xD5\xD7\xEE\xB6\xE0\x34\xD0\xA1\xCA\xB1\xA3\xAC\xBE\xAD\xD1\xE9\xB7\xAD"
        "\xB1\xB6\xA1\xA3\x0D\x0A"
        "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xD7\xEE\xB6\xE0\xC0\xDB\xBC\xC6\x31\x30"
        "\x30\xD0\xA1\xCA\xB1\xA1\xA3";

    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_practise_help19_request(request, requestLen) ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 19,
                                     &objectStart) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "helpinfo",
                                       helpInfoGbk))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_practise_help19 type=0 response=%u evidence=JianghuOL.CBE:0x0102C3D6+0x0102CB46(case19)\n",
           pos);
    return pos;
}
#endif

static bool vm_net_mock_is_battle_death_prompt_choice_request(const u8 *request, u32 requestLen,
                                                               u32 *choiceOut)
{
    u32 offset = 4;
    u32 choice = 0;
    vm_net_mock_request_object object;

    if (choiceOut)
        *choiceOut = 0;
    if (request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 14)
        return false;
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "result", &choice))
        return false;

    if (choiceOut)
        *choiceOut = choice;
    return true;
}

#ifndef CBE_SERVER_SPLIT_OBJECTS
u32 vm_net_mock_build_battle_death_prompt_error_response(u8 *out, u32 outCap,
                                                         const char *info)
{
    u32 pos = 5;
    u32 objectStart = 0;
    /* 20/1.info is consumed by the same CBE text renderer as other game
     * notices, therefore both the fallback and all callers use GBK bytes. */
    static const char revivalStoneUnavailableGbk[] =
        "\xB8\xB4\xBB\xEE\xCA\xAF\xB2\xBB\xBF\xC9\xD3\xC3"; /* 复活石不可用 */

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 20, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos,
                                       "info", info ? info : revivalStoneUnavailableGbk))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}
#endif

#ifndef CBE_SERVER_SPLIT_OBJECTS
/* JianghuOL.CBE:HandleExpBattleAction(0x0102C3D6) emits precisely one
 * 1/7/21 object with `opengold` when the cultivation setting is confirmed.
 * HandleExpBattleResponse(0x0102CB46) waits for this matching subtype and
 * reads only `result`; an empty acknowledgement leaves the loading dialog
 * active. */
u32 vm_net_mock_build_practise_setting21_response(const u8 *request,
                                                  u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    u32 offset = 4;
    u32 openGold = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    bool success = false;

    if (request == NULL || requestLen < 9 || out == NULL || outCap < pos ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 7 ||
        object.subtype != 21 ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "opengold", &openGold) ||
        openGold > 1)
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    success = vm_net_mock_practise_set_gold(role, openGold != 0);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 21, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 0))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_practise_setting21 role=%u opengold=%u result=%u response=%u evidence=JianghuOL.CBE:0x0102C3D6+0x0102CB46\n",
           role ? role->roleId : 0, openGold, success ? 1u : 0u, pos);
    return pos;
}
#endif

static u32 vm_net_mock_build_battle_death_prompt_followup_response(const u8 *request, u32 requestLen,
                                                                    u8 *out, u32 outCap)
{
    u32 choice = 0;
    u32 reviveHp = 0;
    u32 reviveMp = 0;
    u32 expPenalty = 0;
    u32 moneyPenalty = 0;
    u32 pos = 5;
    u8 objectCount = 0;
    char respawnScene[64];
    u16 respawnX = 0;
    u16 respawnY = 0;
    u16 consumedStoneSeq = 0;
    u32 remainingStoneCount = 0;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_scene_change_target respawnTarget;

    /*
     * Latest confirmed runtime:
     * - subtype-6 attack now reaches Battle.cbm action parser at tick 216.
     * - the client then shows the prompt "您已经死亡，是否进入商城购买复活石?".
     * - confirmation after a locally found 801 resurrection stone emits the
     *   same one-object request with `result=1` (mmBattle:0x2F10);
     * - declining the stone/shop route emits `result=2`.
     *
     * Echoing the request back is invalid: the main business dispatcher first
     * runs event_packet_init(..., 10, 19), and a request-shaped 7/14 packet
     * trips the generic unpack-error branch before any handler can use it.
     * For either completed death branch, return a main-business simple-result
     * object. net_handle_simple_result_info(kind=20/subtype=1) clears the
     * scene/network wait flag at JianghuOL.CBE:0x101145C. An empty WT packet
     * avoids the unpack error but leaves the loading progress active. Append a
     * normal scene-channel enter object so the client reloads through its
     * normal scene state machine instead of staying in the battle death state.
     */
    if (out == NULL || outCap < pos ||
        !vm_net_mock_is_battle_death_prompt_choice_request(request, requestLen, &choice))
        return 0;
    if (choice == 1)
    {
        role = vm_net_mock_active_role();
        if (!vm_net_mock_role_apply_revival_stone(&consumedStoneSeq,
                                                  &remainingStoneCount) ||
            role == NULL)
        {
            printf("[warn][network] mock_battle_death_prompt_choice result=1 action=reject-revival-stone role=%u hp=%u reason=missing-stone-or-not-dead\n",
                   role ? role->roleId : 0,
                   role ? role->hp : 0);
            return vm_net_mock_build_battle_death_prompt_error_response(
                out, outCap, "\xB8\xB4\xBB\xEE\xCA\xAF\xB2\xBB\xBF\xC9\xD3\xC3"); /* 复活石不可用 */
        }
        reviveHp = role->hp;
        reviveMp = role->mp;
        /*
         * The client still has an active Battle.cbm character with HP=0 at
         * this point.  A main-scene 20/1 + 30/1 response re-enters the map but
         * never feeds that cache the HP recovery, so the map UI remains dead.
         * Use the same 4/7 -> 4/8 terminal path as a completed battle: 4/7
         * carries the full HP delta, and 4/8 applies it before the battle
         * screen returns to the already-current scene.  Do not add 30/1 here;
         * that would race a second scene lifecycle against the terminal one.
         */
        pos = vm_net_mock_build_battle_revival_stone_completion_response(out, outCap);
        if (pos == 0)
        {
            printf("[error][network] mock_battle_death_prompt_choice result=1 action=revival-terminal-build-failed role=%u hp=%u mp=%u\n",
                   role->roleId, reviveHp, reviveMp);
            return 0;
        }
        g_mockBattleOperateSessionArmed = 0;
        g_mockBattleOperateSessionFinished = 0;
        g_mockBattlePendingEnemyTurn = 0;
        g_mockBattleAwaitingSettlement = 0;
        printf("[info][network] mock_battle_death_prompt_choice result=1 action=revival-stone stone_seq=%u stone_remaining=%u exp_penalty=0 money_penalty=0 revive=%u/%u scene=%s pos=(%u,%u) response=4/7-battle-status+4/8-terminal+4/11+4/9 resp=%u evidence=item.dsh:801,mmBattle:0x743C+0x7DF6+0x2C50\n",
               consumedStoneSeq,
               remainingStoneCount,
               reviveHp,
               reviveMp,
               role->scene,
               role->x,
               role->y,
               pos);
        vm_autotest_note("mock_battle_death_prompt_choice result=1 action=revival-stone stone_seq=%u stone_remaining=%u revive=%u/%u scene=%s pos=(%u,%u) response=4/7+4/8+4/11+4/9 evidence=item.dsh:801 mmBattle:0x743C/0x7DF6/0x2C50\n",
                         consumedStoneSeq,
                         remainingStoneCount,
                         reviveHp,
                         reviveMp,
                         role->scene,
                         role->x,
                         role->y);
        return pos;
    }
    else if (choice == 2)
    {
        reviveHp = vm_net_mock_role_apply_death_penalty("battle-death-prompt-no",
                                                        &expPenalty,
                                                        &moneyPenalty,
                                                        &reviveMp,
                                                        respawnScene,
                                                        sizeof(respawnScene),
                                                        &respawnX,
                                                        &respawnY);
        if (reviveHp == 0)
        {
            printf("[warn][network] mock_battle_death_prompt_choice result=2 action=reject-ordinary-respawn reason=not-dead-or-state-unavailable\n");
            return vm_net_mock_build_battle_death_prompt_error_response(
                out, outCap, "\xB5\xB1\xC7\xB0\xCE\xDE\xD0\xE8\xB8\xB4\xBB\xEE"); /* 当前无需复活 */
        }
    }
    else
    {
        printf("[warn][network] mock_battle_death_prompt_choice result=%u action=reject-unknown-choice\n",
               choice);
        return vm_net_mock_build_battle_death_prompt_error_response(
            out, outCap, "\xB8\xB4\xBB\xEE\xC7\xEB\xC7\xF3\xCE\xDE\xD0\xA7"); /* 复活请求无效 */
    }
    g_mockBattleOperateSessionArmed = 0;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    memset(&respawnTarget, 0, sizeof(respawnTarget));
    snprintf(respawnTarget.scene, sizeof(respawnTarget.scene), "%s",
             respawnScene[0] ? respawnScene : vm_net_mock_role_initial_scene_name());
    respawnTarget.x = respawnX ? respawnX : VM_NET_MOCK_ROLE_INITIAL_X;
    respawnTarget.y = respawnY ? respawnY : VM_NET_MOCK_ROLE_INITIAL_Y;
    respawnTarget.exitId = 0;
    respawnTarget.mapType = 2;
    respawnTarget.hasSceEntry = true;
    respawnTarget.needsSceneDownload = false;
    {
        u32 objectStart = 0;

        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 20, 1, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 0))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    if (!vm_net_mock_append_scene_enter_object_for_scene(out, outCap, &pos,
                                                        respawnTarget.scene,
                                                        respawnTarget.x,
                                                        respawnTarget.y))
        return 0;
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    vm_net_mock_mark_direct_scene_enter_completed(&respawnTarget, "battle-death-respawn");
    g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
    g_vm_net_mock_last_scene_change_fb4_type = 1;

    printf("[info][network] mock_battle_death_prompt_choice result=%u action=ordinary-respawn exp_penalty=%u money_penalty=%u revive=%u/%u scene=%s pos=(%u,%u) response=20/1-simple-result+30/1-respawn resp=%u evidence=JianghuOL.CBE:0x1011434+0x010396D6\n",
           choice,
           expPenalty,
           moneyPenalty,
           reviveHp,
           reviveMp,
           respawnTarget.scene,
           respawnTarget.x,
           respawnTarget.y,
           pos);
    vm_autotest_note("mock_battle_death_prompt_choice result=%u action=ordinary-respawn exp_penalty=%u money_penalty=%u revive=%u/%u scene=%s pos=(%u,%u) response=20/1+30/1-respawn evidence=JianghuOL.CBE:0x1011434/0x010396D6\n",
                     choice,
                     expPenalty,
                     moneyPenalty,
                     reviveHp,
                     reviveMp,
                     respawnTarget.scene,
                     respawnTarget.x,
                     respawnTarget.y);
    return pos;
}

static u32 vm_net_mock_build_shop_actor_query14_response(const u8 *request, u32 requestLen,
                                                         u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 actorId = 0;
    u32 pos = 5;
    u32 page5Index = 0;
    u32 totalItems = 0;
    u32 pageRows = 0;
    u32 itemInfoLen = 0;
    u32 secretTotalItems = 0;
    u32 secretRows = 0;
    u32 secretItemInfoLen = 0;
    u32 actorInfoLen = 0;
    vm_net_mock_role_state *activeRole = NULL;
    u32 wcoin = 0;
    bool catalogDeliveredBeforeActorQuery = false;
    char page5Ids[160];
    char secretIds[160];
    vm_net_mock_request_object object;

    /*
     * mmShopMstarWqvga.cbm:sub_11F0 sends WT 1/1/14(actorId) when the NPC
     * shop "buy" branch opens. Runtime return-from-shop evidence shows this
     * request can arrive without the sub_1038/sub_618 14/5+14/6 follow-up page
     * requests. sub_162C only marks the shop loading flag and calls sub_11F0,
     * so return the first buy page inline with status/money for that path.
     */
    if (request == NULL || requestLen < 9 || out == NULL || outCap < pos)
        return 0;
    if (request[0] != 'W' || request[1] != 'T')
        return 0;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return 0;
    if (offset != requestLen)
        return 0;
    if (object.major != 1 || object.kind != 1 || object.subtype != 14)
        return 0;
    if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "actorId", &actorId))
        return 0;

    catalogDeliveredBeforeActorQuery =
        g_netMockShopCatalogDeliveredBeforeActorQuery != 0;
    if (!catalogDeliveredBeforeActorQuery)
    {
        activeRole = vm_net_mock_active_role();
        wcoin = vm_net_mock_role_wcoin_balance(activeRole);
        if (!vm_net_mock_append_shop_open_status14_object(out, outCap, &pos))
            return 0;
        if (!vm_net_mock_append_shop_money4_object(out, outCap, &pos))
            return 0;
        if (!vm_net_mock_append_shop_catalog_page_object(out, outCap, &pos, 5, page5Index,
                                                        &totalItems, &pageRows, &itemInfoLen))
            return 0;
        if (!vm_net_mock_append_shop_catalog_page_object(out, outCap, &pos, 6, 0,
                                                        &secretTotalItems, &secretRows,
                                                        &secretItemInfoLen))
            return 0;
    }
    /*
     * mmShop:0x9DE returns immediately after the 1/1/14 actor-state branch and
     * clears the shop loading flag there, so keep this object last.
     */
    if (!vm_net_mock_append_shop_actor_state14_object(out, outCap, &pos, &actorInfoLen))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos,
                                 catalogDeliveredBeforeActorQuery ? 1 : 5);
    g_netMockShop17ListPending = 1;
    g_netMockShopCatalogDeliveredBeforeActorQuery = 0;
    /*
     * `1/1/14(actorId)` is a mmShop status query, not a scene-enter request.
     * Runtime proves that an ordinary mall open sends the current role id in
     * this field, so it cannot distinguish an NPC source or authorize a
     * server-created mmGame shell.  Keep the parent screen and its item
     * manager client-owned; a successful buy independently triggers its
     * normal backpack refresh path.
     */
    if (catalogDeliveredBeforeActorQuery)
    {
        /* This query is the first packet on the mall-return path.  The
         * following 5/10 + 7/7(type=1) is a new mmGame bootstrap; arm one
         * same-role 30/21 delivery for that boundary without treating the
         * ActorInfo query as a scene transition. */
        g_netMockBackpackGridReseedPendingRoleId = actorId;
        printf("[info][network] mock_shop_actor_query14 actorId=%u catalog=prior response=actorinfo-only actorinfo_len=%u\n",
               actorId, actorInfoLen);
        vm_autotest_note("mock_shop_actor_query14 actorId=%u catalog=prior response=1/1/14 actorinfo_len=%u grid_reseed_pending=%u evidence=mmShop:0x11F0/0x9DE->group-type1\n",
                         actorId, actorInfoLen,
                         g_netMockBackpackGridReseedPendingRoleId);
        return pos;
    }

    vm_net_mock_format_shop_page_ids(5, page5Index, 8, page5Ids, sizeof(page5Ids));
    vm_net_mock_format_shop_page_ids(6, 0, 8, secretIds, sizeof(secretIds));
    printf("[info][network] mock_shop_open14 actorId=%u role=%u wcoin=%u pages=inline actor_state=last page5=%u page6=0 secret_total=%u secret_rows=%u secret_iteminfo_len=%u weapon_total=%u weapon_rows=%u weapon_iteminfo_len=%u actorinfo_len=%u owner=client-stack grid_reseed=0 secret_ids=%s weapon_ids=%s\n",
           actorId,
           activeRole ? activeRole->roleId : 0,
           wcoin,
           page5Index,
           totalItems,
           pageRows,
           itemInfoLen,
           secretTotalItems,
           secretRows,
           secretItemInfoLen,
           actorInfoLen,
           page5Ids,
           secretIds);
    vm_autotest_note("mock_shop_open14 actorId=%u role=%u wcoin=%u pages=inline actor_state=last page5=%u secret_total=%u secret_rows=%u secret_iteminfo_len=%u weapon_total=%u weapon_rows=%u weapon_iteminfo_len=%u actorinfo_len=%u owner=client-stack grid_reseed=0 evidence=mmShop:0x162C/0x11F0/0x9DE/0x7BC\n",
                     actorId,
                     activeRole ? activeRole->roleId : 0,
                     wcoin,
                     page5Index,
                     totalItems,
                     pageRows,
                     itemInfoLen,
                     secretTotalItems,
                     secretRows,
                     secretItemInfoLen,
                     actorInfoLen);
    return pos;
}

static bool vm_net_mock_is_shop_info14_request(const u8 *request, u32 requestLen, u8 *subtypeOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (subtypeOut)
        *subtypeOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 14)
        return false;
    if (object.subtype != 14 && object.subtype != 4)
        return false;

    if (subtypeOut)
        *subtypeOut = object.subtype;
    return true;
}

static u32 vm_net_mock_build_shop_info14_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 subtype = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_is_shop_info14_request(request, requestLen, &subtype))
        return 0;

    if (subtype == 14)
    {
        if (!vm_net_mock_append_shop_open_status14_object(out, outCap, &pos))
            return 0;
    }
    else
    {
        if (!vm_net_mock_append_shop_money4_object(out, outCap, &pos))
            return 0;
    }

    vm_net_mock_finish_wt_packet(out, pos, 1);
    g_netMockShop17ListPending = 1;
    /* A standalone 14/14 is the same generic mmShop refresh family as the
     * combined request above.  It must not manufacture a scene return and
     * thereby discard a Backpack parent screen. */
    printf("[info][network] mock_shop_info14 subtype=%u response=14/%u owner=client-stack\n",
           subtype, subtype);
    vm_autotest_note("mock_shop_info14 subtype=%u response=14/%u owner=client-stack evidence=mmShop:0x6D6/0x6BC/0x9DE\n",
                     subtype, subtype);
    return pos;
}

enum
{
    /* Runtime capture: mmShop can batch two consecutive 14/<subtype>(index)
     * requests into one WT packet while the user turns catalog pages. */
    VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS = 2
};

static bool vm_net_mock_collect_shop_page14_request(
    const u8 *request, u32 requestLen,
    u8 subtypes[VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS],
    u32 indexes[VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS],
    u8 *objectCountOut)
{
    u32 offset = 4;
    u8 objectCount = 0;
    vm_net_mock_request_object object;

    if (objectCountOut)
        *objectCountOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' ||
        request[1] != 'T' || subtypes == NULL || indexes == NULL ||
        objectCountOut == NULL)
    return false;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        u32 index = 0;
        u8 index8 = 0;

        if (objectCount >= VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS ||
            object.major != 1 || object.kind != 14 || object.subtype < 5 ||
            object.subtype > 13)
        {
            return false;
        }
        if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen,
                                              "index", &index))
        {
            if (!vm_net_mock_get_object_u8_field(object.payload,
                                                  object.payloadLen, "index",
                                                  &index8))
            {
                return false;
            }
            index = index8;
        }
        subtypes[objectCount] = object.subtype;
        indexes[objectCount] = index;
        objectCount += 1;
    }

    if (offset != requestLen || objectCount == 0)
        return false;
    *objectCountOut = objectCount;
    return true;
}

static u32 vm_net_mock_build_shop_page14_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 subtypes[VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS];
    u32 pageIndexes[VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS];
    u32 totalItems[VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS];
    u32 pageRows[VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS];
    u32 itemInfoLens[VM_NET_MOCK_SHOP_PAGE14_BATCH_MAX_OBJECTS];
    u8 objectCount = 0;

    if (out == NULL || outCap < pos)
        return 0;
    memset(subtypes, 0, sizeof(subtypes));
    memset(pageIndexes, 0, sizeof(pageIndexes));
    memset(totalItems, 0, sizeof(totalItems));
    memset(pageRows, 0, sizeof(pageRows));
    memset(itemInfoLens, 0, sizeof(itemInfoLens));
    if (!vm_net_mock_collect_shop_page14_request(request, requestLen, subtypes,
                                                  pageIndexes, &objectCount))
        return 0;

    for (u8 i = 0; i < objectCount; ++i)
    {
        if (!vm_net_mock_append_shop_catalog_page_object(
                out, outCap, &pos, subtypes[i], pageIndexes[i],
                &totalItems[i], &pageRows[i], &itemInfoLens[i]))
        {
            return 0;
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_netMockShop17ListPending = 1;
    for (u8 i = 0; i < objectCount; ++i)
    {
        char ids[160];

        ids[0] = 0;
        vm_net_mock_format_shop_page_ids(subtypes[i], pageIndexes[i], 8, ids,
                                         sizeof(ids));
        printf("[info][network] mock_shop_page14 subtype=%u category=%s index=%u total=%u rows=%u iteminfo_len=%u first=%u ids=%s request_objects=%u object_index=%u\n",
               subtypes[i],
               vm_net_mock_shop_page_subtype_name(subtypes[i]),
               pageIndexes[i], totalItems[i], pageRows[i], itemInfoLens[i],
               g_vm_net_mock_shop_catalog_count > 0 ?
                   g_vm_net_mock_shop_catalog[0].itemId : 0,
               ids, objectCount, i);
        vm_autotest_note("mock_shop_page14 subtype=%u category=%s index=%u items_total=%u page_rows=%u iteminfo_len=%u request_objects=%u object_index=%u evidence=mmShop:0x618/0x7BC\n",
                         subtypes[i],
                         vm_net_mock_shop_page_subtype_name(subtypes[i]),
                         pageIndexes[i], totalItems[i], pageRows[i],
                         itemInfoLens[i], objectCount, i);
    }
    return pos;
}

static bool vm_net_mock_is_shop_buy14_request(const u8 *request, u32 requestLen,
                                              u8 *typeOut, u32 *itemIdOut,
                                              u8 *countOut)
{
    u32 offset = 4;
    u32 itemId = 0;
    u32 count = 1;
    u32 type = 0;
    vm_net_mock_request_object object;

    if (typeOut)
        *typeOut = 0;
    if (itemIdOut)
        *itemIdOut = 0;
    if (countOut)
        *countOut = 1;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 14 || object.subtype != 3)
        return false;
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "id", &itemId))
        return false;
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "num", &count);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "type", &type);
    if (itemId == 0)
        return false;
    if (count == 0)
        count = 1;
    if (count > 255)
        count = 255;

    if (typeOut)
        *typeOut = (u8)(type > 255 ? 255 : type);
    if (itemIdOut)
        *itemIdOut = itemId;
    if (countOut)
        *countOut = (u8)count;
    return true;
}

static bool vm_net_mock_shop_calculate_cost(u8 type, u32 itemId, u32 count,
                                            u32 *unitPriceOut, u32 *costOut)
{
    const vm_net_mock_shop_catalog_item *item = vm_net_mock_find_shop_catalog_item(itemId);
    u32 unitPrice = vm_net_mock_shop_effective_unit_price(
        itemId,
        item ? item->price : VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE);
    u32 cost = 0;

    if (count == 0)
        count = 1;
    if (unitPrice != 0 && count > 0xffffffffu / unitPrice)
        cost = 0xffffffffu;
    else
        cost = unitPrice * count;

    if (unitPriceOut)
        *unitPriceOut = unitPrice;
    if (costOut)
        *costOut = cost;
    /* `type` is a client-side purchase-mode selector.  sub_2F6C serializes
     * it unchanged with the selected row id, and the client can send type=2
     * for equipment-page rows as well as secret-treasure rows.  Authorize
     * against the server-owned, enabled catalog entry that was exposed to the
     * client, rather than imposing a page/category restriction not present in
     * the wire contract. */
    (void)type;
    return item != NULL && item->enabled;
}

typedef struct
{
    u32 itemId;
    u32 count;
} vm_net_mock_shop_direct_grant;

typedef struct
{
    u32 itemId;
    u32 moneyPerPurchase;
    const vm_net_mock_shop_direct_grant *grants;
    u8 grantCount;
    const char *name;
} vm_net_mock_shop_direct_effect;

/*
 * `item.dsh` is authoritative for the contents.  mmShop:0x9DE specifically
 * omits 808 and 817..819 from the client item manager after a type-2 purchase,
 * so these values are server-side purchase effects, not backpack rows.
 */
static const vm_net_mock_shop_direct_grant g_vm_net_mock_shop_bundle_817[] = {
    {815, 5}, /* 黄金钥匙 * 5 */
};
static const vm_net_mock_shop_direct_grant g_vm_net_mock_shop_bundle_818[] = {
    {800, 5},  /* 传送石 * 5 */
    {807, 5},  /* 小喇叭 * 5 */
    {809, 10}, /* 双倍经验卡 * 10 */
    {827, 10}, /* 修炼丹 * 10 */
};
static const vm_net_mock_shop_direct_grant g_vm_net_mock_shop_bundle_819[] = {
    {815, 10}, /* 黄金钥匙 * 10 */
    {828, 5},  /* 战斗心得 * 5 */
    {810, 5},  /* 四倍经验卡 * 5 */
};
static const vm_net_mock_shop_direct_effect g_vm_net_mock_shop_direct_effects[] = {
    /* 金元宝的 `购买个数=100`，一金=10000 铜钱。 */
    {808, 100u * 10000u, NULL, 0, "gold-ingot"},
    {817, 0, g_vm_net_mock_shop_bundle_817,
     (u8)(sizeof(g_vm_net_mock_shop_bundle_817) / sizeof(g_vm_net_mock_shop_bundle_817[0])),
     "ordinary-bundle"},
    {818, 0, g_vm_net_mock_shop_bundle_818,
     (u8)(sizeof(g_vm_net_mock_shop_bundle_818) / sizeof(g_vm_net_mock_shop_bundle_818[0])),
     "advanced-bundle"},
    {819, 0, g_vm_net_mock_shop_bundle_819,
     (u8)(sizeof(g_vm_net_mock_shop_bundle_819) / sizeof(g_vm_net_mock_shop_bundle_819[0])),
     "supreme-bundle"},
};

static const vm_net_mock_shop_direct_effect *
vm_net_mock_shop_find_direct_effect(u8 type, u32 itemId)
{
    if (type != 2)
        return NULL;
    for (u32 i = 0;
         i < sizeof(g_vm_net_mock_shop_direct_effects) /
                 sizeof(g_vm_net_mock_shop_direct_effects[0]);
         ++i)
    {
        if (g_vm_net_mock_shop_direct_effects[i].itemId == itemId)
            return &g_vm_net_mock_shop_direct_effects[i];
    }
    return NULL;
}

/*
 * Commit a direct-store purchase as one role snapshot.  The common one-item
 * helper persists on success, which is correct for an ordinary purchase but
 * would make a multi-content gift pack observable in partial states.  This
 * path composes its in-memory variant.  Its paired wallet debit is committed
 * by the role writer in the same transaction only after every grant succeeds.
 */
static bool vm_net_mock_shop_apply_direct_effect(
    vm_net_mock_role_state *role, const vm_net_mock_shop_direct_effect *effect,
    u32 purchaseCount, u32 *moneyBeforeOut, u32 *moneyGrantOut,
    u8 *grantEntriesOut)
{
    vm_net_mock_role_state before;
    u32 moneyGrant = 0;
    u8 grantEntries = 0;

    if (moneyBeforeOut)
        *moneyBeforeOut = 0;
    if (moneyGrantOut)
        *moneyGrantOut = 0;
    if (grantEntriesOut)
        *grantEntriesOut = 0;
    if (role == NULL || effect == NULL || purchaseCount == 0)
    {
        return false;
    }

    before = *role;
    if (effect->moneyPerPurchase != 0)
    {
        moneyGrant = vm_net_mock_mul_capped_u32(effect->moneyPerPurchase,
                                                 purchaseCount);
        if (moneyGrant == 0 || role->money > 0xffffffffu - moneyGrant)
            return false;
        role->money += moneyGrant;
    }
    else
    {
        if (effect->grants == NULL || effect->grantCount == 0)
            return false;
        for (u32 i = 0; i < effect->grantCount; ++i)
        {
            u32 grantCount = vm_net_mock_mul_capped_u32(effect->grants[i].count,
                                                         purchaseCount);
            u16 ignoredSeq = 0;

            if (grantCount == 0 ||
                !vm_net_mock_role_add_backpack_item_to_role_in_memory(
                    role, effect->grants[i].itemId, grantCount, &ignoredSeq))
            {
                *role = before;
                return false;
            }
            ++grantEntries;
        }
    }

    if (moneyBeforeOut)
        *moneyBeforeOut = before.money;
    if (moneyGrantOut)
        *moneyGrantOut = moneyGrant;
    if (grantEntriesOut)
        *grantEntriesOut = grantEntries;
    return true;
}

static bool vm_net_mock_shop_commit_purchase(const char *reason,
                                             u32 wcoin_before, u32 cost,
                                             u32 *wcoin_after_out)
{
    vm_net_mock_account_wallet_debit debit;

    if (wcoin_after_out)
        *wcoin_after_out = wcoin_before;
    if (cost == 0 || wcoin_before < cost)
        return false;
    memset(&debit, 0, sizeof(debit));
    debit.expectedBalance = wcoin_before;
    debit.debit = cost;
    if (!vm_net_mock_role_db_save_relational(
            reason, NULL, NULL, 0, false, NULL, &debit, NULL, NULL))
    {
        return false;
    }
    if (wcoin_after_out)
        *wcoin_after_out = debit.committedBalance;
    return true;
}

static u32 vm_net_mock_build_shop_buy14_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 type = 0;
    u32 itemId = 0;
    u8 count = 1;
    u32 unitPrice = 0;
    u32 cost = 0;
    u32 wcoinBefore = 0;
    u32 wcoinAfter = 0;
    u8 capacityBefore = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    u8 capacityAfter = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    u16 seq = 0;
    u8 result = 0;
    bool knownItem = false;
    bool directExpand = false;
    bool directExpandRejectedCount = false;
    bool insufficientFunds = false;
    bool revivalStoneConfirmPending = false;
    bool directActorStateRefresh = false;
    u32 directExpandApplied = 0;
    u32 directMoneyBefore = 0;
    u32 directMoneyGrant = 0;
    u8 directGrantEntries = 0;
    u32 actorInfoLen = 0;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state roleBefore;
    const vm_net_mock_shop_direct_effect *directEffect = NULL;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_is_shop_buy14_request(request, requestLen, &type, &itemId, &count))
        return 0;

    role = vm_net_mock_active_role();
    knownItem = vm_net_mock_shop_calculate_cost(type, itemId, count,
                                                &unitPrice, &cost);
    wcoinBefore = vm_net_mock_shop_wcoin_balance();
    wcoinAfter = wcoinBefore;
    directExpand = vm_net_mock_shop_item_is_direct_backpack_expand(type, itemId);
    directEffect = vm_net_mock_shop_find_direct_effect(type, itemId);
    if (role != NULL)
    {
        capacityBefore = role->backpackCapacity;
        capacityAfter = role->backpackCapacity;
    }

    if (role != NULL && knownItem)
    {
        if (wcoinBefore < cost)
        {
            insufficientFunds = true;
            result = vm_net_mock_shop_buy14_failure_result(type);
        }
        else if (directExpand)
        {
            /*
             * mmShopMstarWqvga.cbm:sub_9DE handles type=2 + item 806 as a
             * direct capacity update. That success branch ignores seq and does
             * not route through the normal backpack add/sync path.
             */
            if (count <= 1)
            {
                roleBefore = *role;
                directExpandApplied = vm_net_mock_role_expand_backpack_capacity(role, 1);
                if (directExpandApplied != 0 &&
                    vm_net_mock_shop_commit_purchase("shop-buy14-expand",
                                                     wcoinBefore, cost,
                                                     &wcoinAfter))
                {
                    g_netMockShop17ListPending = 0;
                    g_netMockBackpackPreferRoleListAfterShopBuy = 0;
                    g_netMockBackpackGridSeededRoleId = 0;
                    capacityAfter = role->backpackCapacity;
                    result = 1;
                }
                else
                {
                    *role = roleBefore;
                    directExpandApplied = 0;
                    capacityAfter = role->backpackCapacity;
                    result = vm_net_mock_shop_buy14_failure_result(type);
                }
            }
            else
            {
                directExpandRejectedCount = true;
                result = vm_net_mock_shop_buy14_failure_result(type);
            }
        }
        else if (directEffect != NULL)
        {
            roleBefore = *role;
            if (vm_net_mock_shop_apply_direct_effect(role, directEffect, count,
                                                     &directMoneyBefore,
                                                     &directMoneyGrant,
                                                     &directGrantEntries) &&
                vm_net_mock_shop_commit_purchase("shop-buy14-direct",
                                                 wcoinBefore, cost,
                                                 &wcoinAfter))
            {
                g_netMockShop17ListPending = 0;
                /* Bundle contents are normal backpack rows, but the package
                 * itself is not.  Let the next native list request render the
                 * contents through its owning `17/1`/`7/42` parser. */
                g_netMockBackpackPreferRoleListAfterShopBuy =
                    directEffect->grantCount != 0 ? 1 : 0;
                if (directEffect->grantCount != 0)
                    g_netMockBackpackGridSeededRoleId = 0;
                directActorStateRefresh = directMoneyGrant != 0;
                result = 1;
            }
            else
            {
                *role = roleBefore;
                directMoneyBefore = 0;
                directMoneyGrant = 0;
                directGrantEntries = 0;
                result = vm_net_mock_shop_buy14_failure_result(type);
            }
        }
        else
        {
            roleBefore = *role;
            if (vm_net_mock_role_add_backpack_item_to_role_in_memory(
                    role, itemId, count, &seq) &&
                vm_net_mock_shop_commit_purchase("shop-buy14-item",
                                                 wcoinBefore, cost,
                                                 &wcoinAfter))
            {
                g_netMockShop17ListPending = 0;
                g_netMockBackpackPreferRoleListAfterShopBuy = 1;
                g_netMockBackpackGridSeededRoleId = 0;
                result = 1;
            }
            else
            {
                *role = roleBefore;
                seq = 0;
                result = vm_net_mock_shop_buy14_failure_result(type);
            }
        }
    }

    /* Shop success only makes 801 available locally.  The battle module must
     * subsequently confirm consumption with 1/7/14(result=1); never revive
     * merely because the purchase response was delivered. */
    revivalStoneConfirmPending = result == 1 && itemId == 801 &&
        role != NULL && role->hp == 0;

    /* The wallet debit has already committed through the same relational
     * purchase transaction that persisted the item/capacity/direct effect.
     * Audit only that durable success; a log-write failure must never turn a
     * completed player purchase into a failed client response. */
    if (result == 1 && cost != 0 && role != NULL &&
        g_vm_mock_service_active_account_id != NULL &&
        g_vm_mock_service_active_account_id[0] != 0)
    {
        char operationDetail[256];

        snprintf(operationDetail, sizeof(operationDetail),
                 "游戏内商城消费 W币 %u，购买商品 ID %u × %u，余额 %u→%u",
                 cost, itemId, count, wcoinBefore, wcoinAfter);
        if (!vm_mock_admin_operation_log_record(
                "spend-wcoin-shop", g_vm_mock_service_active_account_id,
                role->roleId, itemId, count, cost, operationDetail, NULL))
        {
            printf("[error][mock-service] operation_log_game_wcoin_failed source=shop account=%s role=%u item=%u count=%u cost=%u error=%s\n",
                   g_vm_mock_service_active_account_id, role->roleId, itemId,
                   count, cost, vm_mysql_last_error());
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 14, 3, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "seq", seq))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    /*
     * `sub_9DE` reads the purchase result first, then treats a trailing
     * 1/1/14 actorinfo object as an ordinary actor-state update and returns.
     * This is the parser-owned money refresh for 808; do not synthesize a
     * backpack row for a zero-stack direct-store value.
     */
    if (result == 1 && directActorStateRefresh &&
        !vm_net_mock_append_shop_actor_state14_object(out, outCap, &pos, &actorInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_packet(out, pos,
                                 directActorStateRefresh && result == 1 ? 2 : 1);

    printf("[info][network] mock_shop_buy14 type=%u item=%u count=%u unit=%u cost=%u wcoin=%u/%u seq=%u result=%u known=%u direct_expand=%u direct_applied=%u direct_reject_count=%u direct=%s direct_money=%u+%u direct_grants=%u actorinfo_refresh=%u actorinfo_len=%u insufficient=%u revival_confirm_pending=%u capacity=%u/%u shop_pending=%u backpack_prefer_role=%u grid_reseed=%u resp=14/3\n",
           type, itemId, count, unitPrice, cost, wcoinBefore, wcoinAfter, seq,
           result, knownItem ? 1 : 0, directExpand ? 1 : 0, directExpandApplied,
           directExpandRejectedCount ? 1 : 0,
           directEffect ? directEffect->name : "none",
           directMoneyBefore, directMoneyGrant, directGrantEntries,
           directActorStateRefresh ? 1 : 0, actorInfoLen, insufficientFunds ? 1 : 0,
           revivalStoneConfirmPending ? 1 : 0,
           capacityBefore, capacityAfter,
           g_netMockShop17ListPending,
           g_netMockBackpackPreferRoleListAfterShopBuy,
           result ? 1 : 0);
    vm_autotest_note("mock_shop_buy14 type=%u item=%u count=%u unit=%u cost=%u wcoin=%u/%u seq=%u result=%u direct_expand=%u direct_applied=%u direct_reject_count=%u direct=%s direct_money=%u+%u direct_grants=%u actorinfo_refresh=%u actorinfo_len=%u insufficient=%u revival_confirm_pending=%u capacity=%u/%u shop_pending=%u backpack_prefer_role=%u grid_reseed=%u response=14/3 evidence=mmShop:0x9DE(type2-direct-store)+item.dsh\n",
                     type, itemId, count, unitPrice, cost, wcoinBefore, wcoinAfter,
                     seq, result, directExpand ? 1 : 0, directExpandApplied,
                     directExpandRejectedCount ? 1 : 0,
                     directEffect ? directEffect->name : "none",
                     directMoneyBefore, directMoneyGrant, directGrantEntries,
                     directActorStateRefresh ? 1 : 0, actorInfoLen,
                     insufficientFunds ? 1 : 0,
                     revivalStoneConfirmPending ? 1 : 0,
                     capacityBefore, capacityAfter,
                     g_netMockShop17ListPending,
                     g_netMockBackpackPreferRoleListAfterShopBuy,
                     result ? 1 : 0);
    return pos;
}

static bool vm_net_mock_is_shop_buy17_request(const u8 *request, u32 requestLen,
                                              u32 *shopIdOut, u8 *countOut)
{
    u32 offset = 4;
    u32 shopId = 0;
    u8 count = 1;
    vm_net_mock_request_object object;

    if (shopIdOut)
        *shopIdOut = 0;
    if (countOut)
        *countOut = 1;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 17 || object.subtype != 2)
        return false;
    if (!vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "shopId", &shopId))
        return false;

    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "num", &count);
    if (count == 0)
        count = 1;
    if (shopIdOut)
        *shopIdOut = shopId;
    if (countOut)
        *countOut = count;
    return true;
}

static u32 vm_net_mock_build_shop_buy17_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 shopId = 0;
    u8 count = 1;
    u16 seq = 0;
    u8 result = 0;
    const vm_net_mock_shop_catalog_item *catalogItem = NULL;

    if (outCap < pos || out == NULL)
        return 0;
    if (!vm_net_mock_is_shop_buy17_request(request, requestLen, &shopId, &count))
        return 0;
    catalogItem = vm_net_mock_find_shop_catalog_item(shopId);
    result = catalogItem != NULL && catalogItem->enabled &&
                     vm_net_mock_role_add_backpack_item(shopId, count, &seq)
                 ? 1
                 : 0;
    if (result)
    {
        g_netMockShop17ListPending = 0;
        g_netMockBackpackPreferRoleListAfterShopBuy = 1;
        g_netMockBackpackGridSeededRoleId = 0;
    }

    /*
     * The buy button sends WT 17/2 with shopId, but mmShopMstarWqvga.cbm:sub_9DE
     * consumes the server result as kind 14 subtype 3.  That branch reads "seq"
     * first, then "result"; on result==1 it inserts the purchased item through
     * the normal item-manager callback.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 14, 3, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "seq", seq))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_autotest_note("mock_shop_buy17 shopId=%u count=%u resp=14/3 seq=%u result=%u shop_pending=%u backpack_prefer_role=%u grid_reseed=%u evidence=mmShop:0x9DE(seq/result)\n",
                     shopId, count, seq, result, g_netMockShop17ListPending,
                     g_netMockBackpackPreferRoleListAfterShopBuy,
                     result ? 1 : 0);
    return pos;
}

#ifndef CBE_SERVER_SPLIT_OBJECTS
bool vm_net_mock_is_login_tail_skill_request(const u8 *request, u32 requestLen)
{
    if (request == NULL || requestLen != 14)
        return false;
    if (request[0] != 'W' || request[1] != 'T')
        return false;

    u32 offset = 4;
    vm_net_mock_request_object object;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 0x0c || object.subtype != 1 || object.payloadLen != 0)
        return false;

    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 42 || object.payloadLen != 0)
        return false;

    return offset == requestLen;
}

u32 vm_net_mock_build_login_tail_skill_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_login_tail_skill_objects(out, outCap, &pos, &objectCount,
                                                      false))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}
#endif

#ifndef CBE_SERVER_SPLIT_OBJECTS
bool vm_net_mock_is_short_wt_control_packet(const u8 *request, u32 requestLen, u8 kind, u8 subtype)
{
    if (request == NULL || requestLen != 9)
        return false;
    return request[0] == 'W' &&
           request[1] == 'T' &&
           request[2] == 0 &&
           request[3] == 9 &&
           request[4] == 1 &&
           request[5] == kind &&
           request[6] == subtype &&
           request[7] == 0 &&
           request[8] == 5;
}
#endif

static const char *vm_net_mock_actor_resource_name(u8 actorJob, u8 actorSex)
{
    static const char *actorNames[6] = {
        "h_warrior.actor",
        "hW_warrior.actor",
        "h_assassin.actor",
        "hW_assassin.actor",
        "h_mage.actor",
        "hW_mage.actor",
    };
    u32 tableIndex = vm_net_mock_actor_resource_index(actorJob, actorSex);
    if (tableIndex >= sizeof(actorNames) / sizeof(actorNames[0]))
        tableIndex = 0;
    return actorNames[tableIndex];
}

static const char *vm_net_mock_actor_preview_image_name(u8 actorJob, u8 actorSex)
{
    const char *overrideName = vm_net_mock_env_str("CBE_ACTOR_PREVIEW_IMAGE", "");
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    (void)actorJob;
    (void)actorSex;
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    return vm_net_mock_role_designation(role)->overheadResource;
}

/* Fresh ActorInfo's status section is not an arbitrary six-stat array.
 * `parse_actorinfo_playerinfo_blob(0x0100FA88)` stores the scalar after
 * primary/secondary HP/MP at actor+132, then writes its next six values to
 * actor+290, +292, +298, +294, +306 and +300 respectively.  The local
 * equipment application path (`scene_rebuild_status_meter_node` calling
 * `AddActorStatBonus(0x0100FE2C)`) proves the strength/agility/wisdom/hit/
 * defense/resist destinations.  The remaining +294 word is the client speed
 * slot; DSH has no speed column, so zero is the only confirmed server
 * baseline for it.
 *
 * Keep this wire contract separate from `vm_net_mock_player_stats`: that
 * structure can retain unresolved endurance/charm values for server gameplay,
 * but they have no proven place in this ActorInfo status section. */
typedef struct vm_net_mock_actorinfo_status_fields {
    u32 strength;
    u32 words[6]; /* agility, wisdom, hit, speed, defense, resist */
} vm_net_mock_actorinfo_status_fields;

static void vm_net_mock_build_actorinfo_status_fields(
    const vm_net_mock_player_stats *stats,
    vm_net_mock_actorinfo_status_fields *fields)
{
    if (fields == NULL)
        return;

    memset(fields, 0, sizeof(*fields));
    if (stats == NULL)
        return;

    /* ActorInfo stores the three primary attributes as u32.  Keep its
     * unequipped baseline uncapped so the client and server start from the
     * same value before the client applies 1/7/7 equipment bonuses. */
    fields->strength = stats->strength;
    fields->words[0] = stats->agility;
    fields->words[1] = stats->wisdom;
    fields->words[2] = stats->hit;
    fields->words[3] = 0; /* speed: unresolved; equip.dsh has no such column */
    fields->words[4] = stats->defense;
    fields->words[5] = stats->resist;
}

static u32 vm_net_mock_build_actor_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    u32 guildId = 0;
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();
    char guildName[VM_NET_MOCK_GUILD_NAME_SIZE];
    const char *displayNameOverride = vm_net_mock_env_str("CBE_ACTOR_DISPLAY_NAME", "");
    const char *displayName = NULL;
    u32 roleLevel = vm_net_mock_env_u32("CBE_ROLE_LEVEL", role ? role->level : 1);
    u32 roleExp = role ? role->exp : 0;
    u32 actorJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", role ? role->job : 1);
    u32 actorSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", role ? role->sex : 0);
    vm_net_mock_player_stats playerStats;
    u32 roleHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMaxHpDefault = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 roleMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 roleMaxMpDefault = VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 visualVariant = 0;
    u32 visualGroup = 0;
    vm_net_mock_actorinfo_status_fields actorStatusFields;
    u32 actorAttrWords[6];
    u32 primaryCurrent = 0;
    u32 primaryBaseMax = 0;
    u32 secondaryCurrent = 0;
    u32 secondaryBaseMax = 0;
    u32 actorSummaryValue = vm_net_mock_env_u32("CBE_ACTOR_SUMMARY_VALUE", roleExp);
    u32 actorGap09C0 = vm_net_mock_env_u32("CBE_ACTOR_GAP09C0",
                                           role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY);
    u32 actorSummaryStatus = 0;
    u8 actorBackpackCapacity = vm_net_mock_backpack_client_capacity(
        vm_net_mock_env_u8("CBE_ACTOR_BACKPACK_CAPACITY",
                           role ? role->backpackCapacity :
                           VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY));
    u8 actorStateByte1 = vm_net_mock_env_u8("CBE_ACTOR_STATE_BYTE1", 0);
    u8 actorTargetX = 12;
    u8 actorTargetY = 10;
    u8 actorField11E = 0;
    u8 actorField120 = 0;
    const char *shortLabel = NULL;
    const char *actorResource = NULL;
    const char *sceneKey = NULL;
    u16 actorResourceArg = 0;
    u16 actorGridX = 0;
    u16 actorGridY = 0;
    u16 motionResourceArg0 = 0;
    u16 motionResourceArg1 = 0;
    u32 primaryDisplayMax = 0;
    u32 secondaryDisplayMax = 0;
    u32 actorGap0CC0 = 0;
    u32 actorGap0CC4 = 0;
    u32 actorGap0CC8 = 0;

    memset(guildName, 0, sizeof(guildName));
    guildId = vm_net_mock_role_guild_info(role, guildName, sizeof(guildName));
    displayName = displayNameOverride != NULL && displayNameOverride[0] != 0 ?
                  displayNameOverride : guildName;

    /* The client consumes 1/7/7 immediately after ActorInfo and adds the
     * equipment DSH bonuses in scene_rebuild_status_meter_node().  ActorInfo
     * therefore carries the unequipped baseline; battle continues to use the
     * full server-side calculation. */
    vm_net_mock_role_build_base_player_stats(role, &playerStats);
    vm_net_mock_role_default_vitals(role,
                                    &roleHpDefault,
                                    &roleMaxHpDefault,
                                    &roleMpDefault,
                                    &roleMaxMpDefault);
    primaryCurrent = vm_net_mock_env_u32("CBE_ACTOR_HP_CURRENT",
                                         vm_net_mock_env_u32("CBE_ACTOR_HP", roleHpDefault));
    primaryBaseMax = vm_net_mock_env_u32("CBE_ACTOR_HP_MAX", playerStats.maxHp);
    secondaryCurrent = vm_net_mock_env_u32("CBE_ACTOR_MP_CURRENT",
                                           vm_net_mock_env_u32("CBE_ACTOR_MP", roleMpDefault));
    secondaryBaseMax = vm_net_mock_env_u32("CBE_ACTOR_MP_MAX", playerStats.maxMp);
    /* These two later ActorInfo fields populate the HUD meter caps.  Unlike
     * primary/secondaryBaseMax, they are not fed back through the client's
     * 1/7/7 equipment reconstruction.  They must therefore carry the full
     * durable maximum; otherwise a full role is rendered as though it still
     * had its pre-equipment HP/MP after every login. */
    primaryDisplayMax = vm_net_mock_env_u32("CBE_ACTOR_HP_DISPLAY_MAX",
                                            roleMaxHpDefault);
    secondaryDisplayMax = vm_net_mock_env_u32("CBE_ACTOR_MP_DISPLAY_MAX",
                                              roleMaxMpDefault);
    if (roleLevel == 0)
        roleLevel = 1;
    if (actorJob == 0 || actorJob > 3)
        actorJob = 1;
    if (actorSex > 1)
        actorSex = 0;
    if (primaryBaseMax == 0)
        primaryBaseMax = 120;
    if (secondaryBaseMax == 0)
        secondaryBaseMax = 100;
    /* Current HP/MP are durable full-combat values.  The client clamps them
     * after it has added local equipment maxima; clipping to the wire baseline
     * here loses health whenever equipment raises the maximum. */
    if (primaryCurrent > roleMaxHpDefault)
        primaryCurrent = roleMaxHpDefault;
    if (secondaryCurrent > roleMaxMpDefault)
        secondaryCurrent = roleMaxMpDefault;
    if (primaryDisplayMax == 0)
        primaryDisplayMax = roleMaxHpDefault ? roleMaxHpDefault : primaryBaseMax;
    if (secondaryDisplayMax == 0)
        secondaryDisplayMax = roleMaxMpDefault ? roleMaxMpDefault : secondaryBaseMax;
    /* Preserve a valid meter if a diagnostic override is lower than the
     * persisted current value.  Normal service uses the full maxima above. */
    if (primaryDisplayMax < primaryCurrent)
        primaryDisplayMax = primaryCurrent;
    if (secondaryDisplayMax < secondaryCurrent)
        secondaryDisplayMax = secondaryCurrent;

    vm_net_mock_build_actorinfo_status_fields(&playerStats, &actorStatusFields);
    actorStatusFields.strength = vm_net_mock_env_u32("CBE_ACTOR_ATTR_STRENGTH",
                                                      actorStatusFields.strength);
    actorAttrWords[0] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_AGILITY",
                                             actorStatusFields.words[0]);
    actorAttrWords[1] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_WISDOM",
                                             actorStatusFields.words[1]);
    actorAttrWords[2] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_HIT",
                                             actorStatusFields.words[2]);
    actorAttrWords[3] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_SPEED",
                                             actorStatusFields.words[3]);
    actorAttrWords[4] = vm_net_mock_env_u32("CBE_ACTOR_ATTR_DEFENSE",
                                             actorStatusFields.words[4]);
    /* Resistance has no ActorInfo override: the baseline is zero and the
     * client adds resistance solely from the subsequent 1/7/7 worn-equipment
     * records.  Keeping an override here would manufacture a second source
     * that combat does not share. */
    actorAttrWords[5] = actorStatusFields.words[5];
    actorSummaryStatus = vm_net_mock_env_u32("CBE_ACTOR_STATUS_WORD", roleLevel);
    actorGap0CC0 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC0", playerStats.attack);
    actorGap0CC4 = vm_net_mock_env_u32("CBE_ACTOR_GAP0CC4", playerStats.defense);
    actorGap0CC8 = playerStats.resist;
    shortLabel = vm_net_mock_env_str("CBE_ACTOR_SHORT_LABEL", vm_net_mock_role_title(role));

    /*
     * Fresh scene-enter keeps the compact title-facing knobs but the scene-side
     * portrait/widget picker is not fully 0-based. Static `scene_runtime_init_
     * and_sync()` now proves the portrait index is `visualGroup + 2*visualVariant - 1`,
     * so `visualVariant` remains 0-based (`job-1`) while `visualGroup` must stay
     * in the original `1..2` family rather than `0..1`.
     */
    visualVariant = actorJob - 1;
    visualGroup = actorSex + 1;

    actorTargetX = (u8)vm_net_mock_env_u32("CBE_ACTOR_TARGET_X", 12);
    actorTargetY = (u8)vm_net_mock_env_u32("CBE_ACTOR_TARGET_Y", 10);
    actorGridX = (u16)vm_net_mock_env_u32("CBE_ACTOR_GRID_X", role ? role->x : vm_net_mock_scene_spawn_x());
    actorGridY = (u16)vm_net_mock_env_u32("CBE_ACTOR_GRID_Y", role ? role->y : vm_net_mock_scene_spawn_y());
    actorField11E = vm_net_mock_env_u8("CBE_ACTOR_BYTE_11E", actorTargetX);
    actorField120 = vm_net_mock_env_u8("CBE_ACTOR_BYTE_120", actorTargetY);
    actorResource = vm_net_mock_actor_resource_name((u8)actorJob, (u8)actorSex);
    sceneKey = vm_net_mock_scene_key_name();
    if (!vm_net_mock_scene_name_is_persistable(sceneKey))
    {
        printf("[error][network] mock_actorinfo_rejected role=%u scene=%s reason=noncanonical-scene-key contract=exact-sce-key\n",
               roleId, sceneKey && sceneKey[0] ? sceneKey : "-");
        return 0;
    }
    actorResourceArg = (u16)vm_net_mock_env_u32("CBE_ACTOR_RESOURCE_ARG", roleLevel);
    motionResourceArg0 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG0", actorGridX);
    motionResourceArg1 = (u16)vm_net_mock_env_u32("CBE_ACTOR_MOTION_ARG1", actorGridY);

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)visualVariant))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)visualGroup))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    /* parse_actorinfo_playerinfo_blob(0x0100FA88) writes this second scalar to
     * actor+104.  Guild/menu code tests that slot directly: zero means the
     * role has no guild, nonzero is the persistent guild id. */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, guildId))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, displayName))
        return 0;

    /*
     * Fresh scene-enter (`parse_actorinfo_response(a2==0)`) does not match the
     * older guessed "level/current/base/..." order directly. The confirmed
     * stream from the second string onward is:
     *   tagged u32 summaryStatus (stored into a word slot)
     *   u32 primaryCurrent
     *   u32 primaryBaseMax
     *   u32 secondaryCurrent
     *   u32 secondaryBaseMax
     *   u32 strength (actor+132)
     *   six truncated u32 -> actor+290/+292/+298/+294/+306/+300
     *                            (agility/wisdom/hit/speed/defense/resist)
     *   u32 total EXP (actor+176)
     *   u32 gap09C0
     *   u8 backpackCapacity (stored into main item manager +38)
     *   u8 state1
     *   u32 primaryDisplayMax
     *   u32 secondaryDisplayMax
     *   u8 targetPosX
     *   u8 targetPosY
     *   str shortLabel/title
     *   str previewImage
     *   u32 gap0CC0
     *   u32 gap0CC4
     *   u32 gap0CC8
     *   str actorResource
     *   i16 level word
     *   str sceneKey
     *   i16 motionArg0
     *   i16 motionArg1
     */
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorSummaryStatus))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryCurrent))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryBaseMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryCurrent))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryBaseMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorStatusFields.strength))
        return 0;

    for (u32 i = 0; i < 6; ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorAttrWords[i]))
            return 0;
    }

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorSummaryValue))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap09C0))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorBackpackCapacity))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorStateByte1))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, primaryDisplayMax))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, secondaryDisplayMax))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorField11E))
        return 0;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorField120))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, shortLabel))
        return 0;
    /*
     * This 20-byte mid-body string lands at actor+0x10A.  The final scene
     * consumer seen so far is `scene_draw_node_overhead_overlay()`, which treats
     * a non-empty value as an optional named overhead badge/icon. Do not seed it
     * with the actor's walk GIF; that makes body art appear as stray fragments.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos,
                                    vm_net_mock_actor_preview_image_name((u8)actorJob, (u8)actorSex)))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC0))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC4))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, actorGap0CC8))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, actorResource))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, actorResourceArg))
        return 0;
    /*
     * parse_actorinfo_response copies this second trailing string directly into
     * R9+0x5E46, and fresh scene-enter later reuses that buffer as
     * currentMapIdText / the persistent +1141 scene-text slot. Keep it separate
     * from actorResource and the trailing grid/pose-like word pair.
     */
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, sceneKey))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, motionResourceArg0))
        return 0;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, motionResourceArg1))
        return 0;


    return pos;
}

static u32 vm_net_mock_build_role_list_info(u8 *out, u32 outCap)
{
    u32 pos = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *roleName = role ? role->name : vm_net_mock_default_role_name();
    u32 roleId = role ? role->roleId : VM_NET_MOCK_ROLE_DEFAULT_ID;
    u32 roleLevel = vm_net_mock_env_u32("CBE_ROLE_LEVEL", role ? role->level : 1);

    if (roleLevel == 0)
        roleLevel = 1;

    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleId))
        return 0;
    if (!vm_net_mock_seq_put_string(out, outCap, &pos, roleName))
        return 0;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, roleLevel))
        return 0;

    return pos;
}

static u32 vm_net_mock_build_title_role_list_actorinfo(u8 *out, u32 outCap)
{
    u32 pos = 0;
    vm_net_mock_role_db_load();
    u32 roleCount = g_vm_net_mock_role_db_valid ? g_vm_net_mock_role_db.roleCount : 0;
    if (roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        roleCount = VM_NET_MOCK_ROLE_DB_MAX_ROLES;

    /*
     * mmTitleMstarWqvga.cbm sub_3544() reads compact role-list bytes as:
     *   id, jobIndex(0..2), sex(1..2), name, level.
     * The create-success path at 0x5324 writes the same layout into the local
     * title row: row+325 = selected job index, row+324 = selected sex + 1.
     *
     * mmTitleMstarWqvga.cbm sub_3544() parses its actorinfo field as:
     *   count, then repeated (tagged u32, tagged u8, tagged u8, len16+cstr, tagged i16).
     * The field name collides with later in-scene actorinfo, but the title-side
     * payload is a compact role-list entry table instead of the large player blob.
     */
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)roleCount))
        return 0;
    for (u32 i = 0; i < roleCount; ++i)
    {
        vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        u8 actorJobIndex = role->job > 0 ? (u8)(role->job - 1) : 0;
        u8 actorSex = (u8)(role->sex + 1);
        u16 roleLevel = (u16)role->level;
        if (actorJobIndex > 2)
            actorJobIndex = 0;
        if (actorSex < 1 || actorSex > 2)
            actorSex = 1;
        if (roleLevel == 0)
            roleLevel = 1;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, role->roleId))
            return 0;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorJobIndex))
            return 0;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, actorSex))
            return 0;
        if (!vm_net_mock_seq_put_string(out, outCap, &pos, role->name))
            return 0;
        if (!vm_net_mock_seq_put_i16(out, outCap, &pos, roleLevel))
            return 0;
    }

    return pos;
}

static bool vm_net_mock_append_title_equipment_enhance_rules(
    u8 *out, u32 outCap, u32 *pos)
{
    u8 data[VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL * 7];
    u32 dataLen = vm_net_mock_build_equipment_enhance_primary_rule_data(
        data, sizeof(data));

    /* mmTitleMstarWqvga.cbm:
     * title_parse_equipment_enhance_primary_rules(0x1568) is called after
     * subtype-4 actorinfo is accepted.  It allocates itemCtrl+0x584 from
     * `num` and parses `data` as repeated tagged (u8 flat, i16 percent) rows.
     * These fields therefore belong to the title role-list response, not to
     * 29/1 enhancement preview or item.dsh's occult-crystal category. */
    return dataLen != 0 &&
           vm_net_mock_put_object_u8(
               out, outCap, pos, "num",
               VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL) &&
           vm_net_mock_put_object_entry(
               out, outCap, pos, "data", data, (u16)dataLen);
}

#define VM_NET_MOCK_LOGIN_SERVER_MAX 8
#define VM_NET_MOCK_LOGIN_SERVER_NAME_CAP 32
#define VM_NET_MOCK_LOGIN_SERVER_LABEL_CAP 32
#define VM_NET_MOCK_LOGIN_SERVERINFO_CAP 1024

/*
 * The working one-row `serverinfo` contract is
 *   name:string, label:string, serverID:u32, color:u24.
 * `servernum` is the title parser's u16 count field.  Serialising that proven
 * row shape once per enabled row is the bounded multi-row extension used here;
 * its real-title verification steps are recorded in
 * docs/re/2026-07-24-configurable-login-server-list.md.
 *
 * The original mock had one literal record for "测试一区" and hard-coded
 * `servernum=1`, so the client could never exercise its real list path.  Keep
 * this catalog at the protocol boundary: it contains only fields the title
 * parser actually receives and deliberately does not pretend that choosing a
 * row changes the already-established CBMS endpoint.
 */
typedef struct
{
    u32 serverId;
    char displayName[VM_NET_MOCK_LOGIN_SERVER_NAME_CAP];
    char label[VM_NET_MOCK_LOGIN_SERVER_LABEL_CAP];
    u32 displayColor;
    u32 sortOrder;
    bool enabled;
} vm_net_mock_login_server;

typedef struct
{
    u32 loaded;
    u32 skipped;
} vm_net_mock_login_server_load_context;

static vm_net_mock_login_server g_vm_net_mock_login_servers[VM_NET_MOCK_LOGIN_SERVER_MAX];
static u32 g_vm_net_mock_login_server_count = 0;
static bool g_vm_net_mock_login_server_db_loaded = false;
static bool g_vm_net_mock_login_server_db_valid = false;

static void vm_net_mock_login_server_sort(void)
{
    for (u32 i = 1; i < g_vm_net_mock_login_server_count; ++i)
    {
        vm_net_mock_login_server value = g_vm_net_mock_login_servers[i];
        u32 j = i;

        while (j > 0)
        {
            const vm_net_mock_login_server *previous =
                &g_vm_net_mock_login_servers[j - 1];
            if (previous->sortOrder < value.sortOrder ||
                (previous->sortOrder == value.sortOrder &&
                 previous->serverId <= value.serverId))
            {
                break;
            }
            g_vm_net_mock_login_servers[j] = *previous;
            --j;
        }
        g_vm_net_mock_login_servers[j] = value;
    }
}

static bool vm_net_mock_login_server_db_row(
    void *contextValue, unsigned int columnCount,
    const char *const *values, const size_t *lengths)
{
    vm_net_mock_login_server_load_context *context =
        (vm_net_mock_login_server_load_context *)contextValue;
    vm_net_mock_login_server row;
    u32 number[4];
    size_t nameLen = 0;
    size_t labelLen = 0;

    memset(&row, 0, sizeof(row));
    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 6 ||
        g_vm_net_mock_login_server_count >= VM_NET_MOCK_LOGIN_SERVER_MAX ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &number[0]) ||
        number[0] == 0 ||
        !vm_mysql_hex_decode(values[1], lengths[1], row.displayName,
                             sizeof(row.displayName) - 1, &nameLen) ||
        !vm_mysql_hex_decode(values[2], lengths[2], row.label,
                             sizeof(row.label) - 1, &labelLen) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &number[1]) ||
        number[1] > 0x00ffffffu ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &number[2]) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &number[3]) ||
        number[3] > 1 || nameLen == 0 || labelLen == 0)
    {
        if (context != NULL)
            ++context->skipped;
        return true;
    }
    row.displayName[nameLen] = 0;
    row.label[labelLen] = 0;
    row.serverId = number[0];
    row.displayColor = number[1];
    row.sortOrder = number[2];
    row.enabled = number[3] != 0;
    g_vm_net_mock_login_servers[g_vm_net_mock_login_server_count++] = row;
    ++context->loaded;
    return true;
}

static u32 vm_net_mock_login_server_enabled_count(void)
{
    u32 count = 0;

    for (u32 i = 0; i < g_vm_net_mock_login_server_count; ++i)
    {
        if (g_vm_net_mock_login_servers[i].enabled)
            ++count;
    }
    return count;
}

static bool vm_net_mock_login_server_db_load(void)
{
    vm_net_mock_login_server_load_context context;

    if (g_vm_net_mock_login_server_db_loaded)
        return g_vm_net_mock_login_server_db_valid;
    g_vm_net_mock_login_server_db_loaded = true;
    g_vm_net_mock_login_server_db_valid = false;
    g_vm_net_mock_login_server_count = 0;
    memset(g_vm_net_mock_login_servers, 0,
           sizeof(g_vm_net_mock_login_servers));
    memset(&context, 0, sizeof(context));

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_login_servers ("
            "server_id INT UNSIGNED NOT NULL,"
            "display_name VARBINARY(31) NOT NULL,"
            "status_label VARBINARY(31) NOT NULL,"
            "display_color MEDIUMINT UNSIGNED NOT NULL DEFAULT 16777215,"
            "sort_order INT UNSIGNED NOT NULL DEFAULT 0,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(server_id),KEY idx_server_login_servers_visible(enabled,sort_order,server_id)) ENGINE=InnoDB") ||
        /* The seed is database data, not a packet fallback.  The admin cannot
         * delete/disable the final visible row, so a normally administered
         * service always has a parser-safe title list after an upgrade. */
        !vm_mysql_exec(
            "INSERT IGNORE INTO server_login_servers("
            "server_id,display_name,status_label,display_color,sort_order,enabled) VALUES("
            "1,X'BDADBAFED2BBC7F8',X'CDC6BCF6',16777215,0,1)") ||
        !vm_mysql_query(
            "SELECT server_id,HEX(display_name),HEX(status_label),display_color,sort_order,enabled "
            "FROM server_login_servers ORDER BY sort_order,server_id",
            vm_net_mock_login_server_db_row, &context))
    {
        printf("[error][network] login_server_catalog_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    vm_net_mock_login_server_sort();
    if (vm_net_mock_login_server_enabled_count() == 0)
    {
        printf("[error][network] login_server_catalog_load rejected reason=no-enabled-server rows=%u skipped=%u\n",
               context.loaded, context.skipped);
        return false;
    }
    g_vm_net_mock_login_server_db_valid = true;
    printf("[info][network] login_server_catalog_load rows=%u enabled=%u skipped=%u source=mysql\n",
           g_vm_net_mock_login_server_count,
           vm_net_mock_login_server_enabled_count(), context.skipped);
    return true;
}

static const vm_net_mock_login_server *vm_net_mock_login_server_find(
    u32 serverId, bool enabledOnly)
{
    if (!vm_net_mock_login_server_db_load())
        return NULL;
    for (u32 i = 0; i < g_vm_net_mock_login_server_count; ++i)
    {
        const vm_net_mock_login_server *row = &g_vm_net_mock_login_servers[i];
        if (row->serverId == serverId && (!enabledOnly || row->enabled))
            return row;
    }
    return NULL;
}

static u32 vm_net_mock_login_server_admin_list(
    vm_net_mock_login_server *rows, u32 rowCap)
{
    u32 count = 0;

    if (rows == NULL || rowCap == 0 || !vm_net_mock_login_server_db_load())
        return 0;
    count = g_vm_net_mock_login_server_count;
    if (count > rowCap)
        count = rowCap;
    memcpy(rows, g_vm_net_mock_login_servers, count * sizeof(*rows));
    return count;
}

static bool vm_net_mock_login_server_admin_save(
    const vm_net_mock_login_server *row, const char **errorOut)
{
    char nameHex[VM_NET_MOCK_LOGIN_SERVER_NAME_CAP * 2 + 1];
    char labelHex[VM_NET_MOCK_LOGIN_SERVER_LABEL_CAP * 2 + 1];
    char query[768];
    int existing = -1;

    if (errorOut)
        *errorOut = "服务器参数无效";
    if (row == NULL || row->serverId == 0 || row->displayName[0] == 0 ||
        row->label[0] == 0 || row->displayColor > 0x00ffffffu ||
        strlen(row->displayName) >= sizeof(row->displayName) ||
        strlen(row->label) >= sizeof(row->label))
    {
        return false;
    }
    if (!vm_net_mock_login_server_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    for (u32 i = 0; i < g_vm_net_mock_login_server_count; ++i)
    {
        if (g_vm_net_mock_login_servers[i].serverId == row->serverId)
        {
            existing = (int)i;
            break;
        }
    }
    if (existing < 0 &&
        g_vm_net_mock_login_server_count >= VM_NET_MOCK_LOGIN_SERVER_MAX)
    {
        if (errorOut)
            *errorOut = "客户端标题协议最多支持 8 个服务器";
        return false;
    }
    if (existing >= 0 && g_vm_net_mock_login_servers[existing].enabled &&
        !row->enabled && vm_net_mock_login_server_enabled_count() <= 1)
    {
        if (errorOut)
            *errorOut = "至少保留一个启用的服务器";
        return false;
    }
    if (vm_mysql_hex_encode(row->displayName, strlen(row->displayName),
                            nameHex, sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(row->label, strlen(row->label),
                            labelHex, sizeof(labelHex)) == 0)
    {
        if (errorOut)
            *errorOut = "服务器文本编码失败";
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_login_servers(server_id,display_name,status_label,display_color,sort_order,enabled) "
             "VALUES(%u,X'%s',X'%s',%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE display_name=VALUES(display_name),status_label=VALUES(status_label),"
             "display_color=VALUES(display_color),sort_order=VALUES(sort_order),enabled=VALUES(enabled)",
             row->serverId, nameHex, labelHex, row->displayColor,
             row->sortOrder, row->enabled ? 1u : 0u);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (existing >= 0)
        g_vm_net_mock_login_servers[existing] = *row;
    else
        g_vm_net_mock_login_servers[g_vm_net_mock_login_server_count++] = *row;
    vm_net_mock_login_server_sort();
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] login_server_save id=%u enabled=%u order=%u color=%u\n",
           row->serverId, row->enabled ? 1u : 0u, row->sortOrder,
           row->displayColor);
    return true;
}

static bool vm_net_mock_login_server_admin_delete(u32 serverId,
                                                  const char **errorOut)
{
    char query[192];
    int existing = -1;

    if (errorOut)
        *errorOut = "服务器不存在";
    if (serverId == 0 || !vm_net_mock_login_server_db_load())
        return false;
    for (u32 i = 0; i < g_vm_net_mock_login_server_count; ++i)
    {
        if (g_vm_net_mock_login_servers[i].serverId == serverId)
        {
            existing = (int)i;
            break;
        }
    }
    if (existing < 0)
        return false;
    if (g_vm_net_mock_login_server_count <= 1 ||
        (g_vm_net_mock_login_servers[existing].enabled &&
         vm_net_mock_login_server_enabled_count() <= 1))
    {
        if (errorOut)
            *errorOut = "至少保留一个启用的服务器";
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_login_servers WHERE server_id=%u", serverId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if ((u32)existing + 1 < g_vm_net_mock_login_server_count)
    {
        memmove(&g_vm_net_mock_login_servers[existing],
                &g_vm_net_mock_login_servers[existing + 1],
                (g_vm_net_mock_login_server_count - (u32)existing - 1) *
                    sizeof(g_vm_net_mock_login_servers[0]));
    }
    --g_vm_net_mock_login_server_count;
    memset(&g_vm_net_mock_login_servers[g_vm_net_mock_login_server_count], 0,
           sizeof(g_vm_net_mock_login_servers[0]));
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] login_server_delete id=%u\n", serverId);
    return true;
}

static u32 vm_net_mock_build_login_serverinfo_blob(u8 *out, u32 outCap,
                                                   u16 *serverCountOut)
{
    u32 pos = 0;
    u16 count = 0;

    if (serverCountOut)
        *serverCountOut = 0;
    if (out == NULL || !vm_net_mock_login_server_db_load())
        return 0;
    for (u32 i = 0; i < g_vm_net_mock_login_server_count; ++i)
    {
        const vm_net_mock_login_server *row = &g_vm_net_mock_login_servers[i];

        if (!row->enabled)
            continue;
        if (!vm_net_mock_seq_put_string(out, outCap, &pos, row->displayName) ||
            !vm_net_mock_seq_put_string(out, outCap, &pos, row->label) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, row->serverId) ||
            !vm_net_mock_seq_put_u24(out, outCap, &pos, row->displayColor))
        {
            return 0;
        }
        ++count;
    }
    if (count == 0)
        return 0;
    if (serverCountOut)
        *serverCountOut = count;
    return pos;
}

static u32 vm_net_mock_build_title_servconf_blob(u8 *out, u32 outCap,
                                                 u32 serverId)
{
    const vm_net_mock_login_server *row =
        vm_net_mock_login_server_find(serverId, true);
    u32 pos = 0;

    if (row == NULL)
        return 0;
    /* mmTitleMstarWqvga.cbm sub_3544() consumes this tagged 24-bit color
     * blob after its 1/1/4 server selection. */
    if (!vm_net_mock_seq_put_u24(out, outCap, &pos, row->displayColor))
        return 0;
    return pos;
}

static u32 vm_net_mock_build_login_color_blob(u8 *out, u32 outCap)
{
    const vm_net_mock_login_server *row = NULL;
    u32 pos = 0;

    if (!vm_net_mock_login_server_db_load())
        return 0;
    for (u32 i = 0; i < g_vm_net_mock_login_server_count; ++i)
    {
        if (g_vm_net_mock_login_servers[i].enabled)
        {
            row = &g_vm_net_mock_login_servers[i];
            break;
        }
    }
    if (row == NULL ||
        !vm_net_mock_seq_put_u8(out, outCap, &pos, 1) ||
        !vm_net_mock_seq_put_u8(out, outCap, &pos, 1) ||
        !vm_net_mock_seq_put_u24(out, outCap, &pos, row->displayColor))
    {
        return 0;
    }
    return pos;
}

static u8 vm_net_mock_resolve_login_success_subtype(u8 requestSubtype)
{
    const char *rawSubtype = vm_net_mock_env_str("CBE_LOGIN_TOP_TYPE", "");
    u8 actorSubtype = requestSubtype;

    if (rawSubtype != NULL && rawSubtype[0] != 0)
        actorSubtype = vm_net_mock_env_u8("CBE_LOGIN_TOP_TYPE", requestSubtype);

    if (actorSubtype != 1 && actorSubtype != 2 && actorSubtype != 3 && actorSubtype != 6)
        actorSubtype = 6;

    return actorSubtype;
}

static bool vm_net_mock_append_login_success_object(u8 *out,
                                                    u32 outCap,
                                                    u32 *pos,
                                                    u8 actorSubtype,
                                                    bool includeActorInfo,
                                                    u32 *actorInfoLenOut)
{
    u8 actorInfo[512];
    u32 actorInfoLen = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_scene_timed_item_status timedStatus =
        vm_net_mock_scene_timed_item_status_for_active_effects(
            vm_net_mock_role_active_timed_combat_flag(),
            vm_net_mock_role_active_exp_card_flag(),
            vm_net_mock_role_active_battle_insight_flag());
    u32 totalExp = role ? role->exp : 0;
    u32 curExp = vm_net_mock_role_next_level_start_exp(totalExp);
    u32 lastExp = vm_net_mock_role_last_level_exp(totalExp);
    u32 percentExp = vm_net_mock_role_exp_percent(totalExp);

    if (includeActorInfo)
    {
        memset(actorInfo, 0, sizeof(actorInfo));
        actorInfoLen = vm_net_mock_build_actor_info(actorInfo, sizeof(actorInfo));
        if (actorInfoLen == 0)
            return false;
    }

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, actorSubtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "revivetype", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "ruffianflag",
                                   timedStatus.ruffianflag))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 0))
        return false;
    if (actorSubtype == 6)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "practiseflag", 0))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "pcimg",
                                       timedStatus.pcimg))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard",
                                       timedStatus.expcard))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expbook",
                                       timedStatus.expbook))
            return false;
        if (!vm_net_mock_put_object_string(out, outCap, pos, "practiseinfo", ""))
            return false;
    }
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "lastexp", lastExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "curexp", curExp))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "persentexp", percentExp))
        return false;
    if (includeActorInfo)
    {
        if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
            return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    return true;
}

static bool vm_net_mock_append_actorinfo_object(u8 *out, u32 outCap, u32 *pos, u32 *actorInfoLenOut)
{
    return vm_net_mock_append_login_success_object(out, outCap, pos, 1, true, actorInfoLenOut);
}

static u32 vm_net_mock_build_login_primary_validation_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[VM_NET_MOCK_LOGIN_SERVERINFO_CAP];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;
    u16 serverCount = 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(
        serverInfo, sizeof(serverInfo), &serverCount);
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    /*
     * IDA: mmTitle net_handle_login_response(0x16DC) reads "servernum"
     * through accessor slot [18], while "result"/"newVer" use [19].
     * Runtime disproved both the original u8 encoding and a later u32 trial:
     * the client still left serverCount==0 while result/newVer parsed. Use a
     * u16 field here as the next narrow contract probe for accessor[18].
     */
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", serverCount))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (!g_netMockTitleServerSelectConfirmed)
        vm_net_mock_title_login_phase_mark_server_list();
    return pos;
}

static u32 vm_net_mock_build_login_failure_response(u8 requestSubtype, const char *information, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 responseSubtype = requestSubtype ? requestSubtype : 1;
    const char *message = information ? information : "password error";

    if (outCap < pos)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, responseSubtype, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 2))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "information", message))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_login_primary_wait_server_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[VM_NET_MOCK_LOGIN_SERVERINFO_CAP];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;
    u16 serverCount = 0;

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(
        serverInfo, sizeof(serverInfo), &serverCount);
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    /*
     * The primary login parser only accepts result '1' as the success path
     * that continues on to parse serverinfo/servernum/newVer. Keep this reply
     * narrow: server list metadata only, no actorinfo/role payload.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 1, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", serverCount))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_list();
    return pos;
}

static bool vm_net_mock_append_role_list_object(u8 *out, u32 outCap, u32 *pos)
{
    u8 roleInfo[128];
    memset(roleInfo, 0, sizeof(roleInfo));
    u32 roleInfoLen = vm_net_mock_build_role_list_info(roleInfo, sizeof(roleInfo));
    if (roleInfoLen == 0)
        return false;

    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x20, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "roles", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "maxroles", 3))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "allpgs", 1))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "num", 1))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "roleinfo", roleInfo, (u16)roleInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_title_mode15_result_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    /*
     * The active R9+0x29E8 title-local state machine has a narrower confirmed
     * gate: its type=1/subtype=15 success path goes straight to sub_39A4(),
     * which is the same mode<-1 transition we already correlated with the
     * role-selection state.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, 15, &objectStart))
        return false;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, pos, "result", 1))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_role_list_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_title_mode15_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_title_mode15_result_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_title_server_select_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 serverId = 0;
    u32 moneyType = 0;
    u8 servConf[8];
    u32 servConfLen = 0;
    u8 actorInfo[128];
    u32 actorInfoLen = 0;

    if (outCap < pos)
        return 0;

    (void)vm_net_mock_get_object_u32_field(request, requestLen, "serverID", &serverId);
    (void)vm_net_mock_get_object_u32_field(request, requestLen, "moneytype", &moneyType);
    memset(servConf, 0, sizeof(servConf));
    servConfLen = vm_net_mock_build_title_servconf_blob(
        servConf, sizeof(servConf), serverId);
    if (servConfLen == 0)
    {
        printf("[error][network] mock_title_server_select unresolved server_id=%u reason=not-enabled-in-current-catalog\n",
               serverId);
        return 0;
    }
    actorInfoLen = vm_net_mock_build_title_role_list_actorinfo(actorInfo, sizeof(actorInfo));
    if (actorInfoLen == 0)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "servconf", servConf, (u16)servConfLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return 0;
    if (!vm_net_mock_append_title_equipment_enhance_rules(
            out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_select(serverId);
    return pos;
}

static u32 vm_net_mock_build_title_rolelist_stage_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 actorinfo[128];
    u32 actorinfoLen = 0;

    if (outCap < pos)
        return 0;

    actorinfoLen = vm_net_mock_build_title_role_list_actorinfo(actorinfo, sizeof(actorinfo));
    if (actorinfoLen == 0)
        return 0;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 16, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "actorinfo", actorinfo, (u16)actorinfoLen))
        return 0;
    if (!vm_net_mock_append_title_equipment_enhance_rules(
            out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    vm_net_mock_finish_wt_packet(out, pos, 2);
    return pos;
}

static u32 vm_net_mock_build_title_rolelist_wait_server_select_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (outCap < pos)
        return 0;

    /*
     * role_list_screen_render() can emit the 1/1/16 role-list request as soon
     * as primary login success moves into the title list screen. Until the
     * client has sent the explicit 1/1/4 serverID/moneytype confirmation, keep
     * this as a parser-safe no-op reply. The actual actorinfo object is only
     * valid after server selection, and subtype 16 result '1' is the cached
     * success latch that the title parser expects before it accepts subtype 4
     * actorinfo. Keep this reply minimal so it only advances the state machine
     * and does not mix the later role payload into the same packet.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 16, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_title_role_create_response(const u8 *request,
                                                        u32 requestLen,
                                                        u8 *out,
                                                        u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    vm_net_mock_title_role_create_request fields;
    u32 actorId = 0;
    u8 result = 1;
    u32 roleCount = 0;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_parse_title_role_create_request(request, requestLen, &fields))
        return 0;

    if (!vm_net_mock_role_db_create_from_title(&fields, &actorId, &result))
        result = 1;
    if (g_vm_net_mock_role_db_valid)
        roleCount = g_vm_net_mock_role_db.roleCount;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 7, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "actorid", actorId))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_autotest_note("mock_title_role_create result=%u actorid=%u decoded=%u raw_sex=%u raw_job=%u job_index=%u roles=%u name_len=%u response=1/1/7 evidence=mmTitle:0x3E66/0x5324 runtime=wt1/7-len34\n",
                     result,
                     actorId,
                     fields.decoded ? 1 : 0,
                     fields.rawSex,
                     fields.rawJob,
                     fields.rawJobIsIndex ? 1 : 0,
                     roleCount,
                     (u32)strlen(fields.name));
    return pos;
}

static u32 vm_net_mock_build_title_role_delete_response(const u8 *request,
                                                        u32 requestLen,
                                                        u8 *out,
                                                        u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u32 actorId = 0;
    u8 result = 1;
    u32 roleCount = 0;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_parse_title_role_delete_request(request, requestLen, &actorId))
        return 0;

    if (!vm_net_mock_role_db_delete_by_id(actorId, &result, &roleCount))
        result = 1;

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 8, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    vm_autotest_note("mock_title_role_delete result=%u actorid=%u roles=%u response=1/1/8 evidence=mmTitle:0x1F90/0x53EC runtime=wt1/8-len25\n",
                     result,
                     actorId,
                     roleCount);
    return pos;
}

static u32 vm_net_mock_build_title_role_select_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    vm_net_mock_role_state *role = NULL;
    u32 actorId = 0;
    u32 activeActorId = 0;
    u32 actorInfoLen = 0;
    bool selected = false;

    if (outCap < pos)
        return 0;

    if (!vm_net_mock_parse_title_role_select_request(request, requestLen, &actorId))
        return 0;
    selected = vm_net_mock_select_active_role(actorId);
    role = vm_net_mock_active_role();
    activeActorId = role ? role->roleId : 0;

    if (selected && role != NULL)
    {
        u32 offlineExp = 0;
        u32 offlineMinutes = 0;
        u32 offlineItems = 0;

        /* The three seasonal tokens have no client-side “use” request.  Settle
         * only after this exact role becomes active, so the full login actor
         * payload below is already built from the persisted post-offline EXP. */
        if (vm_net_mock_offline_exp_settle(role, &offlineExp, &offlineMinutes,
                                            &offlineItems) &&
            (offlineExp != 0 || offlineItems != 0))
        {
            printf("[info][network] mock_login_offline_exp role=%u exp=%u minutes=%u tokens=%u source=role-select\n",
                   role->roleId, offlineExp, offlineMinutes, offlineItems);
        }
        /*
         * Role select creates a new first-scene lifecycle on the client. Do
         * not inherit the previous connection's completed/pending scene
         * target: doing so makes the post-init WT 2/3 look like a fresh map
         * transition on relogin and can produce a second full bootstrap.
         */
        g_vm_net_mock_title_role_scene_followup_pending = true;
        memset(&g_vm_net_mock_startup_sce_enter_target, 0,
               sizeof(g_vm_net_mock_startup_sce_enter_target));
        g_vm_net_mock_startup_sce_enter_pending = false;
        g_vm_net_mock_startup_sce_enter_install_generation = 0;
        g_vm_net_mock_startup_sce_enter_armed_tick = 0;
        /*
         * HandleItemGridResponse (JianghuOL.CBE:0x01039952) initializes the
         * main item manager from 30/21 during the following 5/10 + 7/7 type-1
         * group request.  The duplicate guard is captured in the per-account
         * mock state, but a successful role select creates a fresh client item
         * manager even when the same account/role reconnects.  Carrying the
         * old role id across that boundary suppresses 30/21 and leaves the
         * newly created manager empty while the persisted 17/1 rows remain
         * intact.  Re-arm the one-shot grid seed at the protocol lifecycle
         * boundary that actually recreates the manager.
         */
        g_netMockBackpackGridSeededRoleId = 0;
        g_netMockBackpackGridReseedPendingRoleId = 0;
        g_netMockShop17ListPending = 0;
        g_netMockShopCatalogDeliveredBeforeActorQuery = 0;
        g_netMockBackpackPreferRoleListAfterShopBuy = 0;
        g_vm_net_mock_last_scene_change_target_valid = false;
        g_vm_net_mock_last_completed_scene_change_target_valid = false;
        g_vm_net_mock_last_current_scene_reload_valid = false;
        g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
        g_vm_net_mock_teleport_stone_direct_enter_pending = false;
        g_vm_net_mock_teleport_stone_map_enter_pending = false;
        g_vm_net_mock_teleport_stone_confirm_target_valid = false;
        g_vm_net_mock_teleport_stone_deferred_enter_valid = false;
        g_vm_net_mock_teleport_stone_deferred_enter_tick = 0;
        g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
        /*
         * The subtype-6 role-select payload has already created the first
         * scene, so it never passes through append_scene_enter_object_for_scene()
         * (the transfer path that normally arms the one-shot 27/11 catalog).
         * Arm the same catalog lifecycle explicitly for the first scene-ready
         * resource/task follow-up; the later sync poll remains a fallback only.
         */
        vm_net_mock_mark_scene_moveinfo_npc_seed_pending(role->scene);
        printf("[info][network] mock_backpack_grid_reseed role=%u reason=title-role-select next=group-type1-30/21 evidence=JianghuOL.CBE:0x01039952\n",
               role->roleId);
    }

    /* A minimal {result, actorID} subtype-6 ack is accepted by
     * role_manage_screen_handle_network(case 6), but as soon as subtype-15
     * pushes the client into the next bootstrap path the main scene-side logic
     * also sees this subtype-6 object. Static scene_runtime_init_and_sync()
     * treats subtype 6 as a full scene-enter family packet and reads fields
     * such as revivetype/type/practiseflag/pcimg/.../actorinfo from it. Reuse
     * the existing full subtype-6 success shell here so the follow-up subtype-15
     * experiment is not blocked by a missing scene-enter payload contract.
     */
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, 6, true, &actorInfoLen))
        return 0;

    if (!vm_net_mock_append_title_mode15_result_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    vm_autotest_note("mock_title_role_select requested=%u selected=%u active=%u name_len=%u actorinfo_len=%u response=1/1/6+1/1/15 evidence=mmTitle:0x39FC/0x1B9C/0x53EC\n",
                     actorId,
                     selected ? 1 : 0,
                     activeActorId,
                     role ? (u32)strlen(role->name) : 0,
                     actorInfoLen);
    return pos;
}

static u32 vm_net_mock_build_login_alt12_server_list_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    u8 serverInfo[VM_NET_MOCK_LOGIN_SERVERINFO_CAP];
    u32 serverInfoLen = 0;
    u8 colorBlob[16];
    u32 colorBlobLen = 0;
    u16 serverCount = 0;
    char userName[64];
    char password[64];
    char informationUtf8[160];
    char informationBuf[160];
    bool haveUserName = false;
    bool havePassword = false;
    u8 resultCode = vm_net_mock_env_u8("CBE_ALT12_SERVERLIST_RESULT", 1);
    bool echoCredentials = vm_net_mock_env_u8("CBE_ALT12_ECHO_CREDENTIALS", 0) != 0;
    bool issuedGuestCredentials = false;
    const char *information = "select server";

    if (outCap < pos)
        return 0;

    memset(serverInfo, 0, sizeof(serverInfo));
    memset(colorBlob, 0, sizeof(colorBlob));
    memset(userName, 0, sizeof(userName));
    memset(password, 0, sizeof(password));
    memset(informationUtf8, 0, sizeof(informationUtf8));
    memset(informationBuf, 0, sizeof(informationBuf));
    serverInfoLen = vm_net_mock_build_login_serverinfo_blob(
        serverInfo, sizeof(serverInfo), &serverCount);
    if (serverInfoLen == 0)
        return 0;
    colorBlobLen = vm_net_mock_build_login_color_blob(colorBlob, sizeof(colorBlob));
    if (colorBlobLen == 0)
        return 0;

    haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "username", userName, sizeof(userName));
    if (!haveUserName)
        haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "userName", userName, sizeof(userName));
    havePassword = vm_net_mock_get_object_string_field(request, requestLen, "password", password, sizeof(password));
    if (g_vm_mock_service_login_issue_username[0] != 0 &&
        g_vm_mock_service_login_issue_password[0] != 0)
    {
        snprintf(userName, sizeof(userName), "%s", g_vm_mock_service_login_issue_username);
        snprintf(password, sizeof(password), "%s", g_vm_mock_service_login_issue_password);
        haveUserName = true;
        havePassword = true;
        issuedGuestCredentials = true;
        resultCode = g_vm_mock_service_login_issue_result ? g_vm_mock_service_login_issue_result : 3;
        snprintf(informationUtf8, sizeof(informationUtf8),
                 "注册账号成功 账号:%s 密码:%s",
                 userName,
                 password);
        utf8_to_gbk((u8 *)informationUtf8, (u8 *)informationBuf, sizeof(informationBuf));
        information = informationBuf;
    }

    if (resultCode != 1 && resultCode != 3 && resultCode != 4)
        resultCode = 1;

    /*
     * mmTitle net_handle_login_response(0x16DC) parses serverinfo/servernum
     * for subtype-12 result '1', then login_alt_result_dispatch(0x19C2)
     * enters the stageFlag==4 server-list target. Result '3'/'4' also parse
     * serverinfo, but they go through sub_10C6() prompt/callback paths first;
     * returning result '4' as the default can leave the title screen resending
     * the same no-account login request.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", resultCode))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "serverinfo", serverInfo, (u16)serverInfoLen))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "color", colorBlob, (u16)colorBlobLen))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "servernum", serverCount))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "newVer", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "information", information))
        return 0;
    if (echoCredentials || issuedGuestCredentials)
    {
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "username", haveUserName ? userName : ""))
            return 0;
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "password", havePassword ? password : ""))
            return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    vm_net_mock_title_login_phase_mark_server_list();
    printf("[info][network] mock_login_alt12_server_list result=%u servernum=%u issued=%u user=%s resp=%u\n",
           resultCode,
           serverCount,
           issuedGuestCredentials ? 1 : 0,
           issuedGuestCredentials ? userName : "-",
           pos);
    return pos;
}

static u32 vm_net_mock_build_login_alt12_success_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;
    if (outCap < pos)
        return 0;

    /*
     * The live login tap currently sends a top-level 1/1/12 request. Static
     * title RE already has a dedicated wrapper for that alternate success path
     * (`sub_2A50 -> login_alt_result_dispatch -> login_stage_success_dispatch`),
     * so keep this response minimal and do not pre-inject later actorinfo here.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 12, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_ascii_digit(out, outCap, &pos, "result", 1))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_login_actor_response(u8 requestSubtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 actorSubtype = vm_net_mock_resolve_login_success_subtype(requestSubtype);
    if (outCap < pos)
        return 0;

    g_netMockEnterGameOffset = 0;
    g_netMockEnterGameChecksum = 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, actorSubtype, true, &actorInfoLen))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    u8 logJob = vm_net_mock_env_u8("CBE_ACTOR_JOB", 1);
    u8 logSex = vm_net_mock_env_u8("CBE_ACTOR_SEX", 0);
    return pos;
}

static u32 vm_net_mock_build_login_role_list_only_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    if (outCap < pos)
        return 0;

    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, 1, false, NULL))
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    return pos;
}

static u32 vm_net_mock_build_login_actor_role_list_response(u8 requestSubtype, u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 actorSubtype = vm_net_mock_resolve_login_success_subtype(requestSubtype);
    if (outCap < pos)
        return 0;

    u32 actorInfoLen = 0;
    if (!vm_net_mock_append_login_success_object(out, outCap, &pos, actorSubtype, true, &actorInfoLen))
        return 0;
    if (!vm_net_mock_append_role_list_object(out, outCap, &pos))
        return 0;

    vm_net_mock_finish_wt_packet(out, pos, 2);
    return pos;
}

static u32 vm_net_mock_build_login_response(const u8 *request, u32 requestLen, u8 requestSubtype, u8 *out, u32 outCap)
{
    const char *mode = vm_net_mock_env_str("CBE_LOGIN_RESPONSE", "");
    vm_mock_service_login_request loginRequest;
    bool noAccountAlt12Login = false;
    const char *authError = NULL;

    memset(&loginRequest, 0, sizeof(loginRequest));
    loginRequest.requestSubtype = requestSubtype;
    loginRequest.haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "userName",
                                                                    loginRequest.userName,
                                                                    sizeof(loginRequest.userName));
    if (!loginRequest.haveUserName)
        loginRequest.haveUserName = vm_net_mock_get_object_string_field(request, requestLen, "username",
                                                                        loginRequest.userName,
                                                                        sizeof(loginRequest.userName));
    loginRequest.havePassword = vm_net_mock_get_object_string_field(request, requestLen, "password",
                                                                    loginRequest.password,
                                                                    sizeof(loginRequest.password));
    noAccountAlt12Login = vm_mock_service_login_is_no_account(&loginRequest);

    if (!vm_mock_service_authenticate_login_request(&loginRequest, &authError,
                                                    NULL))
        return vm_net_mock_build_login_failure_response(requestSubtype,
                                                        authError ? authError : "account or password error",
                                                        out, outCap);


    if (requestSubtype == 1 &&
        mode != NULL &&
        strcmp(mode, "serverlist-hold") == 0)
    {
        return vm_net_mock_build_login_primary_wait_server_response(out, outCap);
    }

    if (requestSubtype == 1 &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0 ||
         strcmp(mode, "primary-validate") == 0 ||
         strcmp(mode, "roles") == 0 ||
         strcmp(mode, "rolelist") == 0))
    {
        return vm_net_mock_build_login_primary_validation_response(out, outCap);
    }

    if (noAccountAlt12Login &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0))
    {
        /*
         * No-account title login sends 1/1/12 with empty credentials when the
         * user taps "start game". This visible request is the server-list phase,
         * not the later role-list success phase: answer it with serverinfo /
         * color / servernum so the title list has rows to render. The subtype-12
         * result=1 success packet remains available through explicit regression
         * modes such as CBE_LOGIN_RESPONSE=alt12-success.
         */
        return vm_net_mock_build_login_alt12_server_list_response(request, requestLen, out, outCap);
    }

    if (requestSubtype == 12 &&
        (mode == NULL || mode[0] == 0 ||
         strcmp(mode, "staged-rolelist") == 0 ||
         strcmp(mode, "staged") == 0))
    {
        return vm_net_mock_build_login_alt12_server_list_response(request, requestLen, out, outCap);
    }

    if (requestSubtype == 12 &&
        (strcmp(mode, "alt12-success") == 0 ||
         strcmp(mode, "roles") == 0 ||
         strcmp(mode, "rolelist") == 0))
    {
        return vm_net_mock_build_login_alt12_success_response(out, outCap);
    }

    /* Current static/runtime reading is staged rather than same-packet mixing:
     * login validation returns only the login/server-list contract, then the
     * client sends the next request (1/1/16 for title role list, 1/1/6 for role
     * select). Retain older mixed object modes only for explicit regression
     * comparison through CBE_LOGIN_RESPONSE.
     */
    if (mode == NULL || mode[0] == 0 ||
        strcmp(mode, "staged-rolelist") == 0 ||
        strcmp(mode, "staged") == 0)
    {
        return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);
    }

    if (strcmp(mode, "roles+actor") == 0 ||
        strcmp(mode, "actor+roles") == 0 ||
        strcmp(mode, "roles-first") == 0)
    {
        return vm_net_mock_build_login_actor_role_list_response(requestSubtype, out, outCap);
    }

    if (strcmp(mode, "actor") == 0)
        return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);

    return vm_net_mock_build_login_actor_response(requestSubtype, out, outCap);
}

static u32 vm_net_mock_build_enter_game_response(u8 *out, u32 outCap)
{
    u32 pos = 11;
    if (outCap < pos)
        return 0;

    u8 payload[65535];
    const char *payloadName = "mmGameMstarWqvga.cbm";
    u32 payloadLen = vm_net_mock_load_response_file("JHOnlineData/mmGameMstarWqvga.cbm", payload, sizeof(payload));
    if (payloadLen == 0)
    {
        payloadName = "MMORPGTempcbm";
        payloadLen = vm_net_mock_load_response_file("JHOnlineData/MMORPGTempcbm", payload, sizeof(payload));
    }
    if (payloadLen == 0)
        return 0;

    if (g_netMockEnterGameOffset >= payloadLen)
    {
        g_netMockEnterGameOffset = 0;
        g_netMockEnterGameChecksum = 0;
    }

    u32 chunkLen = payloadLen - g_netMockEnterGameOffset;
    if (chunkLen > 1024)
        chunkLen = 1024;
    u32 chunkSum = vm_net_mock_signed_byte_sum(payload + g_netMockEnterGameOffset, chunkLen);
    u32 crc = g_netMockEnterGameChecksum + chunkSum;

    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "totalsize", payloadLen))
        return 0;
    if (!vm_net_mock_put_object_u32(out, outCap, &pos, "crc", crc))
        return 0;
    if (!vm_net_mock_put_object_entry(out, outCap, &pos, "version", (const u8 *)"\x00\x02\x00\x01", 4))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 0))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "name", payloadName))
        return 0;
    if (!vm_net_mock_put_object_blob(out, outCap, &pos, "data", payload + g_netMockEnterGameOffset, (u16)chunkLen))
        return 0;

    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(pos >> 8);
    out[3] = (u8)pos;
    out[4] = 1;
    out[5] = 1;
    out[6] = 0x12;
    out[7] = 7;
    out[8] = 0;
    out[9] = (u8)((pos - 5) >> 8);
    out[10] = (u8)(pos - 5);
    g_netMockEnterGameChecksum = crc;
    g_netMockEnterGameOffset += chunkLen;
    return pos;
}

static bool vm_net_mock_append_game_type_response_object(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap, u32 *pos,
    u8 requestType, u8 responseType, u8 responseSub)
{
    u32 objectStart = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;

    if (request == NULL || requestLen < 8 || out == NULL || pos == NULL)
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, request[4], responseType, responseSub, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (requestType == 1)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
            return false;
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", 0))
            return false;
        if (!vm_net_mock_put_object_string(out, outCap, pos, "name", "Codex"))
            return false;
        if (!vm_net_mock_put_object_u32(out, outCap, pos, "money", money))
            return false;
    }
    else if (requestType == 2)
    {
        /* JianghuOL.CBE:net_handle_misc_player_fields(1/7/20) writes this
         * byte straight into the shared badge cache.  Zero displays the
         * legacy fixed ep_icon.gif (“修”); it is not an inactive sentinel. */
        if (!vm_net_mock_put_object_u8(
                out, outCap, pos, "pcimg",
                vm_net_mock_scene_timed_item_status_for_active_effects(
                    false, false, false)
                    .pcimg))
            return false;
    }
    else if (requestType == 3)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, pos, "expcard",
                                       vm_net_mock_role_active_exp_card_flag()))
            return false;
    }

    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_build_game_type_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap, u8 requestType)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u8 requestKind = 0;
    u8 requestSubtype = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (outCap < pos || request == NULL || requestLen < 8)
        return 0;
    u8 responseType = request[6];
    u8 responseSub = request[7];
    if (requestType == 1)
        responseSub = 0x1a;
    else if (requestType == 2)
        responseSub = 20;
    else if (requestType == 3)
        responseSub = 32;

    if (requestType == 2 && role != NULL &&
        vm_mock_service_backpack_full_bootstrap_matches(role->roleId, 1))
    {
        if (!vm_net_mock_append_deferred_full_backpack_grid_objects(
                out, outCap, &pos, &objectCount) ||
            !vm_net_mock_append_game_type_response_object(
                request, requestLen, out, outCap, &pos, requestType,
                responseType, responseSub))
        {
            return 0;
        }
        objectCount += 1;
        vm_mock_service_backpack_full_bootstrap_advance(role->roleId, 1);
    }
    else if (requestType == 3 && role != NULL &&
             vm_mock_service_backpack_full_bootstrap_matches(role->roleId, 2))
    {
        if (!vm_net_mock_append_deferred_equipment_login_objects(
                out, outCap, &pos, &objectCount) ||
            !vm_net_mock_append_game_type_response_object(
                request, requestLen, out, outCap, &pos, requestType,
                responseType, responseSub))
        {
            return 0;
        }
        objectCount += 1;
        vm_mock_service_backpack_full_bootstrap_complete(role->roleId);
    }
    else
    {
        if (!vm_net_mock_append_game_type_response_object(
                request, requestLen, out, outCap, &pos, requestType,
                responseType, responseSub))
        {
            return 0;
        }
        objectCount += 1;
    }

    (void)requestKind;
    (void)requestSubtype;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

/*
 * Only request objects listed here may be decomposed by the generic compound
 * dispatcher.  This is deliberately an explicit protocol capability list, not
 * a fallback for arbitrary WT objects: feature-specific packets whose object
 * order forms one transaction must be handled by their own combo builder
 * before the generic dispatcher runs.
 */
static bool vm_net_mock_object_is_independent_combo_candidate(
    const vm_net_mock_request_object *object)
{
    if (object == NULL || object->major != 1)
        return false;

    if (object->kind == 2 && (object->subtype == 1 || object->subtype == 10))
        return true;
    if (object->kind == 3 && (object->subtype == 1 || object->subtype == 2))
        return true;
    if (object->kind == 4 && object->subtype == 1)
        return true;
    if (object->kind == 7 &&
        (object->subtype == 1 || object->subtype == 7 || object->subtype == 18 ||
         object->subtype == 42))
        return true;
    /* The teleport-stone destination query is read-only: mmGame consumes only
     * its 16/1.exitinfo payload to finish the list-loading phase.  A retry
     * after cancelling the later local confirmation can be flushed alongside
     * an otherwise independent scene-refresh object, so preserve this object
     * when the explicit compound dispatcher splits that packet.  Do not add
     * 16/2..16/4 here: those form the confirmation/transfer transaction. */
    if (object->kind == 0x10 && object->subtype == 1)
        return true;
    if (object->kind == 0x19 && object->subtype == 5)
        return true;
    /* WT 23/7 has no stateful companion: HandleRankingList consumes precisely
     * this page object, so it is safe to split when the client flushes it with
     * another independently composable request. */
    if (object->kind == 23 && object->subtype == 7)
        return true;
    if (object->kind == 0x63 && object->subtype == 1)
        return true;
    return false;
}

