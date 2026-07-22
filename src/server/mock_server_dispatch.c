static u32 vm_net_mock_build_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap);

static void vm_net_mock_clear_followup_response(void)
{
    g_vmNetMockFollowupResponseLen = 0;
    g_vmNetMockFollowupResponseEventType = 7;
}

static u32 vm_net_mock_build_single_object_request(const vm_net_mock_request_object *object, u8 *out, u32 outCap)
{
    u32 len = 0;
    u32 objectLen = 0;
    if (object == NULL || out == NULL)
        return 0;
    objectLen = (u32)object->payloadLen + 5;
    len = (u32)object->payloadLen + 9;
    if (len > outCap || len > 0xffff)
        return 0;
    out[0] = 'W';
    out[1] = 'T';
    out[2] = (u8)(len >> 8);
    out[3] = (u8)len;
    out[4] = object->major;
    out[5] = object->kind;
    out[6] = object->subtype;
    out[7] = (u8)(objectLen >> 8);
    out[8] = (u8)objectLen;
    if (object->payloadLen != 0)
        memcpy(out + 9, object->payload, object->payloadLen);
    return len;
}

static bool vm_net_mock_validate_response_packet(const u8 *response,
                                                  u32 responseLen,
                                                  u8 *objectCountOut)
{
    u32 offset = 5;
    u8 expectedCount = 0;
    u8 parsedCount = 0;

    if (objectCountOut)
        *objectCountOut = 0;
    if (response == NULL || responseLen < 5 ||
        response[0] != 'W' || response[1] != 'T' ||
        (((u32)response[2] << 8) | (u32)response[3]) != responseLen)
    {
        return false;
    }

    expectedCount = response[4];
    while (parsedCount < expectedCount)
    {
        u32 objectLen = 0;

        if (offset + 6 > responseLen)
            return false;
        objectLen = ((u32)response[offset + 4] << 8) |
                    (u32)response[offset + 5];
        if (objectLen < 6 || objectLen > responseLen - offset)
            return false;
        offset += objectLen;
        ++parsedCount;
    }

    if (offset != responseLen)
        return false;
    if (objectCountOut)
        *objectCountOut = parsedCount;
    return true;
}

static bool vm_net_mock_append_response_objects(u8 *out, u32 outCap, u32 *pos, u8 *objectCount,
                                                const u8 *response, u32 responseLen)
{
    u8 appendedCount = 0;

    if (out == NULL || pos == NULL || objectCount == NULL ||
        !vm_net_mock_validate_response_packet(response, responseLen, &appendedCount) ||
        appendedCount > 0xffu - *objectCount ||
        *pos > outCap ||
        responseLen - 5 > 0xffffu - *pos ||
        responseLen - 5 > outCap - *pos)
    {
        return false;
    }

    if (responseLen > 5)
        memcpy(out + *pos, response + 5, responseLen - 5);
    *pos += responseLen - 5;
    *objectCount = (u8)(*objectCount + appendedCount);
    return true;
}

static u32 vm_net_mock_build_independent_combo_response(const u8 *request,
                                                        u32 requestLen,
                                                        u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 validationOffset = 4;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 subCount = 0;
    u32 validatedCount = 0;
    vm_net_mock_request_object object;
    u8 subRequest[512];
    u8 subResponse[8192];

    if (g_netMockSplitProbe || request == NULL || requestLen < 9 || outCap < pos)
        return 0;
    if (request[0] != 'W' || request[1] != 'T')
        return 0;

    /* Validate the complete object set before invoking any single-object
     * builder.  Small-horn world chat is emitted as
     * 3/1 + 2/10 + 7/1 + 2/10; the old one-pass splitter persisted the chat
     * and then abandoned the packet when it reached an unsupported object. */
    while (vm_net_mock_next_request_object(request, requestLen,
                                           &validationOffset, &object))
    {
        if (!vm_net_mock_object_is_independent_combo_candidate(&object))
            return 0;
        ++validatedCount;
    }
    if (validationOffset != requestLen || validatedCount < 2)
        return 0;

    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        u32 subRequestLen = 0;
        u32 subResponseLen = 0;
        if (!vm_net_mock_object_is_independent_combo_candidate(&object))
            return 0;
        subRequestLen = vm_net_mock_build_single_object_request(&object, subRequest, sizeof(subRequest));
        if (subRequestLen == 0)
            return 0;
        g_netMockSplitProbe = true;
        subResponseLen = vm_net_mock_build_response(subRequest, subRequestLen, subResponse, sizeof(subResponse));
        g_netMockSplitProbe = false;
        if (subResponseLen == 0)
            return 0;
        if (!vm_net_mock_append_response_objects(out, outCap, &pos, &objectCount, subResponse, subResponseLen))
            return 0;
        ++subCount;
    }

    if (offset != requestLen || subCount != validatedCount || objectCount == 0)
        return 0;
    if (!vm_net_mock_append_battle_drop_refresh7_if_needed(out, outCap, &pos,
                                                           &objectCount,
                                                           "independent-combo",
                                                           false))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_independent_combo request_objects=%u response_objects=%u resp=%u evidence=JianghuOL.CBE:0x01012E4C(entry-loop)\n",
           validatedCount, objectCount, pos);
    vm_autotest_note("mock_independent_combo request_objects=%u response_objects=%u response=%u evidence=JianghuOL.CBE:0x01012E4C(entry-loop)\n",
                     validatedCount, objectCount, pos);
    return pos;
}

static u32 vm_net_mock_build_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    if (request == NULL || requestLen == 0 || outCap == 0)
        return 0;

    /* First principle for protocol research: only emulate server/API behavior.
     * Do not advance CBE state here by forcing return values or writing globals.
     */
    u32 hookedLen = 0;

    /*
     * This is a high-frequency, single-object request with a narrow signature.
     * Dispatch it before unrelated shop/resource/scene detectors; several of
     * those consult real game resources and previously added hundreds of
     * milliseconds before the movement ACK was even built.
     */
    if (vm_net_mock_is_actor_moveinfo_upload_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_actor_moveinfo_ack_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-actor-moveinfo-ack", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_actor_type_control_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_actor_type_control_ack_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-actor-type-control-ack", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    /* Chat is commonly flushed together with the scene's 2/10 refresh object.
     * Handle it before the generic `type` detector, whose unrelated type=2
     * branch would otherwise consume a world/team chat request. */
    if (vm_net_mock_request_contains_chat_object(request, requestLen))
    {
        hookedLen = vm_net_mock_build_chat_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-chat-message", request, requestLen, hookedLen);
            return hookedLen;
        }
        if (!g_netMockSplitProbe)
        {
            hookedLen = vm_net_mock_build_independent_combo_response(request, requestLen,
                                                                      out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-chat-message-independent-combo", request,
                                          requestLen, hookedLen);
                return hookedLen;
            }
        }
    }

    if (vm_net_mock_is_update_version_catalog_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_version_response(request, requestLen,
                                                       out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-update-version-catalog",
                                      request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_update_manifest_chunk_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_update_manifest_chunk_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-update-manifest-chunk", request, requestLen, hookedLen);
            return hookedLen;
        }
        vm_net_log_handled_packet("builtin-update-manifest-chunk-missing", request, requestLen, 0);
        return 0;
    }

    if (vm_net_mock_is_update_chunk_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_update_chunk_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-update-chunk", request, requestLen, hookedLen);
            return hookedLen;
        }
        vm_net_log_handled_packet("builtin-update-chunk-missing", request, requestLen, 0);
        return 0;
    }

    hookedLen = vm_net_mock_build_npc_dialog_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-npc-dialog", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_npc_service_dialog_response(
        request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-npc-service", request, requestLen,
                                  hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_task_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-task", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_task_transport_response(request, requestLen,
                                                          out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-task-transport", request,
                                  requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_interaction_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-interaction-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_actor_query14_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-actor-query14", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_info14_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-info14", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_page14_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-page14", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_buy14_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-buy14", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_shop_buy17_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-shop-buy17", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (g_netMockBackpackPreferRoleListAfterShopBuy)
    {
        hookedLen = vm_net_mock_build_backpack_items_books_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            g_netMockBackpackPreferRoleListAfterShopBuy = 0;
            g_netMockShop17ListPending = 0;
            printf("[info][network] mock_shop_buy_backpack_sync shape=17/1+7/42 response=role-backpack pending=0\n");
            vm_net_log_handled_packet("builtin-shop-buy-backpack-sync-combo", request, requestLen, hookedLen);
            return hookedLen;
        }
        if (vm_net_mock_is_backpack_open_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_backpack_open_response(out, outCap);
            if (hookedLen)
            {
                g_netMockBackpackPreferRoleListAfterShopBuy = 0;
                g_netMockShop17ListPending = 0;
                printf("[info][network] mock_shop_buy_backpack_sync shape=7/42 response=role-backpack pending=0\n");
                vm_net_log_handled_packet("builtin-shop-buy-backpack-sync-open", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (vm_net_mock_is_backpack_items_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_backpack_items_response(out, outCap);
            if (hookedLen)
            {
                g_netMockBackpackPreferRoleListAfterShopBuy = 0;
                g_netMockShop17ListPending = 0;
                printf("[info][network] mock_shop_buy_backpack_sync shape=17/1 response=role-backpack pending=0\n");
                vm_net_log_handled_packet("builtin-shop-buy-backpack-sync-items", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
    }

    if (g_netMockShop17ListPending)
    {
        hookedLen = vm_net_mock_build_shop_items_books_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-shop-items-books-combo", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (g_netMockShop17ListPending && vm_net_mock_is_backpack_open_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_shop_items_books_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-shop-items-books", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (g_netMockShop17ListPending && vm_net_mock_is_backpack_items_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_shop_items17_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-shop-items17", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_backpack_items_books_combo_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-backpack-items-books-combo", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_settings_unstuck_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-settings-unstuck", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_role_designation23_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-role-designation23", request, requestLen, hookedLen);
        return hookedLen;
    }

    /* Leave-guild reuses 10/22 {id=roleId}, so split it before the ordinary
     * 10/22 {id=guildId} detail request. */
    hookedLen = vm_net_mock_build_guild_leave_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-leave", request, requestLen, hookedLen);
        return hookedLen;
    }

    /* Joined guild information uses 10/23 {id=guildId}.  Keep this parser-backed
     * handler before the legacy broad role_action23 fallback, which only returns
     * result+id and leaves HandleFactionInfoResponse without its faction blob. */
    hookedLen = vm_net_mock_build_guild_detail_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-detail", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_role_action23_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-role-action23", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_page_compat_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-page-compat", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_page_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-page", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_dialog_gate_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-dialog-gate", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_rank_compat_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-rank-page", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_member_page_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-member-page", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_create_start_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-create-start", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_create_name_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-create-name", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_create_commit_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-create-commit", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_apply_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-apply", request, requestLen, hookedLen);
        return hookedLen;
    }

    /* 10/32 is the in-scene guild recruitment list, not the title role list.
     * Handle it here before the legacy kind/subtype fallback near login. */
    hookedLen = vm_net_mock_build_guild_application_page_response(request, requestLen,
                                                                   out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-application-page",
                                  request, requestLen, hookedLen);
        return hookedLen;
    }

    /* The generic type fallback rewrites type=1/2 replies to subtype 26/20.
     * Applicant approval/rejection must remain 10/33 for HandleRankingResponse. */
    hookedLen = vm_net_mock_build_guild_application_action_response(request, requestLen,
                                                                     out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-application-action",
                                  request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_slogan_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-slogan", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_rank_action_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-rank-action", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_guild_kick_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-guild-kick", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_ctrl_page_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-ctrl-page", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_nearby_player_info_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-nearby-player-info", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_nearby_equip_view_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-nearby-equip-view", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_nearby_guild_invite_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-nearby-guild-invite", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_trade_offer_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-trade-offer", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_trade_confirm_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-trade-confirm", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_trade_invite_reply_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-nearby-trade-invite-reply", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_nearby_trade_request_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-nearby-trade-request", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_team_invite_reply_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-team-invite-reply", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_team_leave_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-team-leave", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_nearby_team_invite_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-nearby-team-invite", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_nearby_spar_request_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-nearby-spar-request", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_instance_challenge_confirm_response(
        request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-npc-instance-challenge-confirm",
                                  request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_spar_invite_reply_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-spar-invite-reply", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_spar_ready_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-spar-ready", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_friend_invite_reply_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-friend-invite-reply", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_friend_add_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-friend-add", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_friend_page_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-friend-page", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_response_from_rules(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("rule", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_penglai02_repeat_scene_change_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-change-penglai02-repeat", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_current_scene_completion_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-change-current-scene-ack", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_current_scene_repeat_scene_change_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-change-current-scene-repeat", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_scene_change_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_scene_change_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-scene-change", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains_object(request, requestLen, 1, 0x1b, 4))
    {
        hookedLen = vm_net_mock_build_type27_followup_combo_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-type27-followup", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_scene_change_post_enter_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-change-post-enter-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_mmgame_scene_transfer_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-mmgame-scene-transfer-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_teleport_stone_direct_enter_default_ack_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-teleport-stone-direct-enter-ack", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_current_scene_reload_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-current-reload", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x19, 5))
    {
        hookedLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-scene-default-event", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_actor_other_scene_default_combo_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_scene_default_event_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-actor-other-scene-default-combo", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 0x63, 1))
    {
        hookedLen = vm_net_mock_build_short_wt_control_ack_response(request, requestLen, 0x63, 1, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-short-63-1-empty-ack", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_short_wt_control_packet(request, requestLen, 7, 18))
    {
        hookedLen = vm_net_mock_build_practise_info18_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-practise-info18", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_battle_death_prompt_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-battle-death-prompt-choice", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_item_use_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-item-use", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_item_discard_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-item-discard", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_item_equip_swap_response(request, requestLen,
                                                           out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-item-equip-swap", request,
                                  requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_item_equip_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-item-equip", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_equipment_enhance_response(
        request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-equipment-enhance", request,
                                  requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_backpack_open_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_backpack_open_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-backpack-open", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_backpack_items_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_backpack_items_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-backpack-items", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_teleport_stone_list_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_teleport_stone_list_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-teleport-stone-list", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_settings_unstuck_16_2_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-settings-unstuck-16-2", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_teleport_stone_confirmed_exit_combo_response(
        request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-teleport-stone-confirmed-exit-combo",
                                  request, requestLen, hookedLen);
        return hookedLen;
    }

    u8 teleportStoneSubtype = 0;
    if (vm_net_mock_is_teleport_stone_transfer_request(request, requestLen, &teleportStoneSubtype))
    {
        hookedLen = vm_net_mock_build_teleport_stone_transfer_response(request, requestLen, teleportStoneSubtype, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-teleport-stone-transfer", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    hookedLen = vm_net_mock_build_teleport_stone_map_transfer_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-teleport-stone-map-confirm", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_teleport_stone_post_enter_combo_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-teleport-stone-post-enter", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_current_scene_task_subset_followup_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_scene_task_subset_followup_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-scene-task-subset-followup-current-scene", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    hookedLen = vm_net_mock_build_compact_scene_skill_default_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-compact-skill-default", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_resource_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-resource-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_scene_task_subset_followup_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-scene-task-subset-followup", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_actor_moveinfo_name_update_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-actor-moveinfo-name-update", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_special_scene_interaction_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-special-scene-interaction", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_synchronized_team_battle_response(
        request, requestLen, out, outCap, VM_MOCK_TEAM_BATTLE_BUILD_ITEM);
    if (hookedLen)
    {
        vm_net_mock_battle_publish_role_vitals();
        vm_net_log_handled_packet("builtin-battle-item-use", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_duel_escape_response(request, requestLen,
                                                       out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-duel-escape", request, requestLen,
                                  hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_battle_escape_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_mock_battle_publish_role_vitals();
        vm_net_log_handled_packet("builtin-battle-escape", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_duel_operate_response(request, requestLen,
                                                        out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-duel-operate", request, requestLen,
                                  hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_synchronized_team_battle_response(
        request, requestLen, out, outCap, VM_MOCK_TEAM_BATTLE_BUILD_OPERATE);
    if (hookedLen)
    {
        vm_net_mock_battle_publish_role_vitals();
        vm_net_log_handled_packet("builtin-battle-operate", request, requestLen, hookedLen);
        return hookedLen;
    }
    hookedLen = vm_net_mock_build_synchronized_team_battle_response(
        request, requestLen, out, outCap, VM_MOCK_TEAM_BATTLE_BUILD_OPERATE_FALLBACK);
    if (hookedLen)
    {
        vm_net_mock_battle_publish_role_vitals();
        vm_net_log_handled_packet("builtin-battle-operate-fallback", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_battle_auto12_cancel_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-battle-auto12-cancel", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_challenge_interaction_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-challenge-interaction", request, requestLen, hookedLen);
        return hookedLen;
    }

    hookedLen = vm_net_mock_build_hangup_battle_start_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-hangup-battle-start", request, requestLen, hookedLen);
        return hookedLen;
    }

    if (vm_net_mock_is_actor_other_only10_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_actor_other_portal_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-actor-other-portal", request, requestLen, hookedLen);
            return hookedLen;
        }

        hookedLen = vm_net_mock_build_actor_other_only10_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-actor-other-only10", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_login_tail_skill_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_login_tail_skill_response(out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-login-tail-skill", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_is_title_server_select_request(request, requestLen))
    {
        hookedLen = vm_net_mock_build_title_server_select_response(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-title-server-select", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 requestKind = 0;
    u8 requestSubtype = 0;
    if (vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &requestKind, &requestSubtype))
    {
        if (vm_net_mock_is_title_role_create_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_role_create_response(request, requestLen, out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-title-role-create", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (vm_net_mock_is_title_role_delete_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_role_delete_response(request, requestLen, out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-title-role-delete", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (vm_net_mock_is_title_role_select_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_role_select_response(request, requestLen, out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-title-role-select", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (vm_net_mock_is_title_rolelist_stage_request(request, requestLen))
        {
            hookedLen = vm_net_mock_build_title_rolelist_wait_server_select_response(out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-title-rolelist-wait-server-select", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
        if (requestKind == 0x0a && requestSubtype == 0x20)
        {
            hookedLen = vm_net_mock_build_role_list_response(out, outCap);
            if (hookedLen)
            {
                vm_net_log_handled_packet("builtin-role-list", request, requestLen, hookedLen);
                return hookedLen;
            }
        }
    }

    if (vm_net_mock_is_login_validation_request(request, requestLen, &requestSubtype))
    {
        vm_net_ensure_business_callback("login-request");
        hookedLen = vm_net_mock_load_response_file("net_mocks/login.bin", out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("file-login", request, requestLen, hookedLen);
            return hookedLen;
        }
        hookedLen = vm_net_mock_build_login_response(request, requestLen, requestSubtype, out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-login", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    u8 gameRequestType = 0;
    hookedLen = vm_net_mock_build_group_type1_response(request, requestLen, out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-group-type1", request, requestLen, hookedLen);
        return hookedLen;
    }

    /*
     * The type=1/2/3 bootstrap exchange belongs to WT 7/7.  Do not use the
     * field name alone as a detector: actor movement/scene combo packets can
     * also contain a type field.  Rewriting such a WT 2/1 request as 2/26
     * opens the task-hall screen without list data and leaves its touch
     * callback null (JianghuOL.CBE:0x01010C34 -> 0x0104A2C0).
     */
    if (requestKind == 7 && requestSubtype == 7 &&
        vm_net_mock_get_object_u8_field(request, requestLen, "type", &gameRequestType) &&
        (gameRequestType == 1 || gameRequestType == 2 || gameRequestType == 3))
    {
        hookedLen = vm_net_mock_build_game_type_response(request, requestLen, out, outCap, gameRequestType);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-game-type", request, requestLen, hookedLen);
            return hookedLen;
        }
    }

    if (vm_net_mock_request_contains(request, requestLen, "version"))
    {
        vm_net_mock_title_login_phase_reset("version-handshake");
        hookedLen = vm_net_mock_build_version_response(request, requestLen,
                                                       out, outCap);
        if (hookedLen)
        {
            vm_net_log_handled_packet("builtin-version", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    hookedLen = vm_net_mock_build_independent_combo_response(request, requestLen,
                                                              out, outCap);
    if (hookedLen)
    {
        vm_net_log_handled_packet("builtin-independent-combo", request,
                                  requestLen, hookedLen);
        return hookedLen;
    }
    if (requestKind == 4 && requestSubtype == 2)
    {
        hookedLen = vm_net_mock_build_battle_operate_response_fallback(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_mock_battle_publish_role_vitals();
            vm_net_log_handled_packet("builtin-battle-operate-lastchance-fallback", request, requestLen, hookedLen);
            return hookedLen;
        }
        hookedLen = vm_net_mock_build_battle_operate_response_raw82(request, requestLen, out, outCap);
        if (hookedLen)
        {
            vm_net_mock_battle_publish_role_vitals();
            vm_net_log_handled_packet("builtin-battle-operate-raw82", request, requestLen, hookedLen);
            return hookedLen;
        }
    }
    vm_net_log_unhandled_packet(request, requestLen);
#ifdef CBE_SERVER_ONLY
    /*
     * A production/headless server must not terminate because a client sent a
     * packet whose protocol has not been implemented yet.  Keep the detailed
     * unhandled-packet trace above, but complete this service request with an
     * empty response so the connection can continue serving later packets.
     *
     * Emulator builds deliberately retain the assertion below: while reverse
     * engineering locally, an unknown packet should still stop at the exact
     * point where protocol coverage is missing.
     */
    vm_net_log_handled_packet("ignored-unhandled-server-only", request, requestLen, 0);
    return 0;
#else
    assert(0);
    return 0;
#endif
}

