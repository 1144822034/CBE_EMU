static bool vm_net_mock_role_swap_equipped_backpack_item(
    vm_net_mock_role_state *role,
    u16 bodySeq,
    u16 backpackSeq,
    u32 *equippedItemIdOut,
    u32 *oldItemIdOut,
    u8 *slotOut,
    const char **reasonOut)
{
    vm_net_mock_backpack_item_state *backpackItem = NULL;
    const vm_net_mock_equipment_catalog_item *newEquip = NULL;
    u32 newItemId = 0;
    u32 oldItemId = 0;
    u8 slot = 0xff;
    u32 itemCount = 0;
    vm_net_mock_role_state before;

    if (equippedItemIdOut)
        *equippedItemIdOut = 0;
    if (oldItemIdOut)
        *oldItemIdOut = 0;
    if (slotOut)
        *slotOut = 0xff;
    if (reasonOut)
        *reasonOut = "ok";

    if (role == NULL)
    {
        if (reasonOut)
            *reasonOut = "no-role";
        return false;
    }

    before = *role;
    (void)before;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        if (role->backpackItems[i].seq == backpackSeq)
        {
            backpackItem = &role->backpackItems[i];
            break;
        }
    }
    if (backpackItem == NULL || backpackItem->count != 1)
    {
        if (reasonOut)
            *reasonOut = backpackItem == NULL ? "backpack-seq-not-found" : "backpack-not-single-equipment";
        return false;
    }

    newItemId = backpackItem->itemId;
    newEquip = vm_net_mock_find_equipment_catalog_item(newItemId);
    if (newEquip == NULL || newEquip->slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
    {
        if (reasonOut)
            *reasonOut = "not-equipment";
        return false;
    }
    if (role->level == 0)
        role->level = vm_net_mock_role_level_from_exp(role->exp);
    if (role->level < newEquip->levelRequired)
    {
        if (reasonOut)
            *reasonOut = "level-too-low";
        return false;
    }
    if (!vm_net_mock_role_job_allows_weapon_category(role->job, newEquip->category))
    {
        if (reasonOut)
            *reasonOut = "wrong-job-weapon";
        return false;
    }

    slot = newEquip->slot;
    oldItemId = role->equippedItemIds[slot];
    /* Equipment list rows are encoded as seq=slot+1; accepting any other
     * body value would make the persistent swap disagree with the client's
     * pending equipped-item pointer. */
    if (oldItemId == 0)
    {
        if (reasonOut)
            *reasonOut = "slot-empty-use-7-8";
        return false;
    }
    if (bodySeq != (u16)(slot + 1))
    {
        if (reasonOut)
            *reasonOut = "body-seq-slot-mismatch";
        return false;
    }

    /* HandleItemOperationResponse(7/9) removes the selected backpack item
     * and reuses its sequence for the former equipped item locally.  Mirror
     * that exact replacement in the saved role instead of consume+append:
     * append would allocate a different sequence and make the next operation
     * target the wrong item. */
    {
        u16 newEnhance = backpackItem->enhanceLevel;
        u16 oldEnhance = role->equippedEnhanceLevels[slot];
        u32 newInstanceId = backpackItem->enhanceInstanceId;
        u32 oldInstanceId = role->equippedEnhanceInstanceIds[slot];
        vm_net_mock_enhance_wash_state newWash;
        vm_net_mock_enhance_wash_state oldWash;

        vm_net_mock_enhance_wash_copy(&newWash, &backpackItem->enhanceWash);
        vm_net_mock_enhance_wash_copy(&oldWash, &role->equippedEnhanceWash[slot]);
        newInstanceId = vm_net_mock_role_ensure_equipment_instance_id(
            role, newInstanceId, newItemId);
        oldInstanceId = vm_net_mock_role_ensure_equipment_instance_id(
            role, oldInstanceId, oldItemId);
        vm_net_mock_enhance_wash_coalesce_from_record(
            role->roleId, newInstanceId, newItemId, newEnhance, backpackSeq,
            &newWash);
        vm_net_mock_enhance_wash_coalesce_from_record(
            role->roleId, oldInstanceId, oldItemId, oldEnhance, 0, &oldWash);
        if (newEnhance > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            newEnhance = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        if (oldEnhance > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            oldEnhance = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        backpackItem->itemId = oldItemId;
        backpackItem->count = 1;
        backpackItem->enhanceLevel = oldEnhance;
        backpackItem->enhanceInstanceId = oldInstanceId;
        vm_net_mock_enhance_wash_copy(&backpackItem->enhanceWash, &oldWash);
        role->equippedItemIds[slot] = newItemId;
        role->equippedEnhanceLevels[slot] = newEnhance;
        role->equippedEnhanceInstanceIds[slot] = newInstanceId;
        vm_net_mock_enhance_wash_copy(&role->equippedEnhanceWash[slot], &newWash);
        if (!vm_net_mock_enhance_wash_is_empty(&oldWash))
        {
            (void)vm_net_mock_enhance_wash_record_relocate_seq(
                role->roleId, oldInstanceId, 0, backpackSeq, oldItemId,
                oldEnhance, &oldWash);
        }
        if (!vm_net_mock_enhance_wash_is_empty(&newWash))
        {
            (void)vm_net_mock_enhance_wash_record_upsert(
                role->roleId, newInstanceId, 0, newItemId, newEnhance, &newWash,
                0);
        }
    }
    vm_net_mock_role_sync_derived_vitals(role);
    vm_net_mock_role_mark_inventory_dirty("item-equip-swap");
    (void)vm_net_mock_role_refresh_special_designations(role);

    if (equippedItemIdOut)
        *equippedItemIdOut = newItemId;
    if (oldItemIdOut)
        *oldItemIdOut = oldItemId;
    if (slotOut)
        *slotOut = slot;
    return true;
}

static bool vm_net_mock_role_unequip_item(vm_net_mock_role_state *role,
                                          u32 requestedItemId,
                                          u16 requestedSeq,
                                          u32 *unequippedItemIdOut,
                                          u16 *backpackSeqOut,
                                          u8 *slotOut,
                                          const char **reasonOut)
{
    u8 slot = 0xff;
    u32 itemId = 0;
    u16 seq = 0;
    const vm_net_mock_equipment_catalog_item *equip = NULL;

    if (unequippedItemIdOut)
        *unequippedItemIdOut = 0;
    if (backpackSeqOut)
        *backpackSeqOut = 0;
    if (slotOut)
        *slotOut = 0xff;
    if (reasonOut)
        *reasonOut = "ok";

    if (role == NULL)
    {
        if (reasonOut)
            *reasonOut = "no-role";
        return false;
    }

    if (requestedItemId != 0)
    {
        for (u8 i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
        {
            if (role->equippedItemIds[i] == requestedItemId)
            {
                slot = i;
                itemId = requestedItemId;
                break;
            }
        }
    }

    if (slot == 0xff && requestedItemId != 0)
    {
        equip = vm_net_mock_find_equipment_catalog_item(requestedItemId);
        if (equip != NULL && equip->slot < VM_NET_MOCK_EQUIP_SLOT_COUNT &&
            role->equippedItemIds[equip->slot] != 0)
        {
            slot = equip->slot;
            itemId = role->equippedItemIds[slot];
        }
    }

    if (slot == 0xff && requestedSeq != 0)
    {
        /*
         * Equipment bootstrap / worn rows use seq = slotIndex + 1
         * (docs/re/2026-07-21-equipment-login-restore.md).  Prefer that
         * deterministic mapping before the single-occupied-slot fallback —
         * multi-slot wear + seq-only unequip otherwise fails as ambiguous.
         */
        if (requestedSeq >= 1 &&
            requestedSeq <= (u16)VM_NET_MOCK_EQUIP_SLOT_COUNT)
        {
            u8 trySlot = (u8)(requestedSeq - 1u);

            if (role->equippedItemIds[trySlot] != 0)
            {
                slot = trySlot;
                itemId = role->equippedItemIds[slot];
            }
        }
    }

    if (slot == 0xff && requestedSeq != 0)
    {
        /*
         * The server state keeps equipped item ids, while the client request may
         * only carry the item seq assigned when it was equipped.  If there is a
         * single equipped item, the selector is still unambiguous.
         */
        u8 foundSlot = 0xff;
        for (u8 i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
        {
            if (role->equippedItemIds[i] == 0)
                continue;
            if (foundSlot != 0xff)
            {
                if (reasonOut)
                    *reasonOut = "ambiguous-seq";
                return false;
            }
            foundSlot = i;
        }
        if (foundSlot != 0xff)
        {
            slot = foundSlot;
            itemId = role->equippedItemIds[slot];
        }
    }

    if (slot == 0xff || itemId == 0)
    {
        if (reasonOut)
            *reasonOut = "equipped-item-not-found";
        return false;
    }

    {
        u16 unequipEnhance = role->equippedEnhanceLevels[slot];
        u32 unequipInstanceId = role->equippedEnhanceInstanceIds[slot];
        vm_net_mock_enhance_wash_state unequipWash;

        vm_net_mock_enhance_wash_copy(&unequipWash, &role->equippedEnhanceWash[slot]);
        unequipInstanceId = vm_net_mock_role_ensure_equipment_instance_id(
            role, unequipInstanceId, itemId);
        /*
         * Keep the same item_instance_id across wear↔bag.  The old
         * add_backpack_item_enhanced(..., instanceId=0) path minted a new id
         * and dropped the wash-record key, so a later empty in-memory wash
         * could not coalesce and 17/1 reopened as default yellow milestones.
         */
        vm_net_mock_enhance_wash_coalesce_from_record(
            role->roleId, unequipInstanceId, itemId, unequipEnhance, 0,
            &unequipWash);
        if (!vm_net_mock_role_add_backpack_item_to_role(
                role, itemId, 1, unequipEnhance, &seq, NULL, &unequipWash,
                unequipInstanceId))
        {
            if (reasonOut)
                *reasonOut = "bag-full";
            return false;
        }
        if (!vm_net_mock_enhance_wash_is_empty(&unequipWash))
        {
            (void)vm_net_mock_enhance_wash_record_relocate_seq(
                role->roleId, unequipInstanceId, 0, seq, itemId, unequipEnhance,
                &unequipWash);
        }
    }

    role->equippedItemIds[slot] = 0;
    role->equippedEnhanceLevels[slot] = 0;
    role->equippedEnhanceInstanceIds[slot] = 0;
    vm_net_mock_enhance_wash_clear(&role->equippedEnhanceWash[slot]);
    vm_net_mock_role_sync_derived_vitals(role);
    vm_net_mock_role_mark_inventory_dirty("item-unequip");
    (void)vm_net_mock_role_refresh_special_designations(role);

    if (unequippedItemIdOut)
        *unequippedItemIdOut = itemId;
    if (backpackSeqOut)
        *backpackSeqOut = seq;
    if (slotOut)
        *slotOut = slot;
    if (reasonOut)
        *reasonOut = "ok";
    return true;
}

/* Defined after vm_mock_service_client_session (equip response is earlier). */
static void vm_net_mock_item_equip_arm_vitals_after_gear_change(void);
/* #region agent log */
/* Count non-fresh map 1/1/14 delivers (equip path arms these). */
static u32 g_agent_dbg_equip_vitals_delivered = 0;
/* #endregion */

static u32 vm_net_mock_build_item_equip_response(const u8 *request, u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    vm_net_mock_item_equip_request parsed;
    vm_net_mock_role_state *role = NULL;
    u32 itemId = 0;
    u16 seq = 0;
    u8 slot = 0xff;
    u32 oldItemId = 0;
    const char *reason = "not-matched";
    u8 result = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    bool vitalsArmed = false;
    bool actorinfoAppended = false;
    u32 actorInfoLen = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_item_equip_request(request, requestLen, &parsed))
        return 0;

    role = vm_net_mock_active_role();
    if (parsed.type == 3 && parsed.haveItemSelector &&
        vm_net_mock_role_equip_backpack_item(role, parsed.itemId, parsed.seq,
                                             &itemId, &seq, &slot, &oldItemId,
                                             &reason))
    {
        result = 1;
    }
    else if (parsed.type == 4 &&
             vm_net_mock_role_unequip_item(role, parsed.itemId, parsed.seq,
                                           &itemId, &seq, &slot, &reason))
    {
        result = 1;
    }
    else if (!parsed.haveItemSelector)
    {
        reason = "missing-selector";
    }

    /*
     * JianghuOL.CBE:0x01033544 subtype 8 reads type first.  For type=3,
     * result=1 moves the pending backpack item to the equipment manager. For
     * type=4, it moves the pending equipped item back into the backpack. Both
     * branches read the server-provided seq into item+276 before clearing wait.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 8, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", parsed.type))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    if (!vm_net_mock_put_object_u16(out, outCap, &pos, "seq", seq ? seq : parsed.seq))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount = 1;

    /*
     * Unequip (type=4): same-packet 7/7 type=1 carries live wash bit7 so Pixi
     * openBag merge does not briefly paint default yellow milestones.
     * Keep under event-7 object cap; skip rather than fail the 7/8 ack.
     */
    /*
     * Unequip (type=4): same-packet 7/7 type=1 carries live wash bit7 so Pixi
     * openBag merge does not briefly paint default yellow milestones.
     * Cap is the same event-7 limit as enhance sync (10 objects).
     */
    if (result == 1 && parsed.type == 4 && seq != 0 && itemId != 0 &&
        (u32)objectCount + 1u <= 10u)
    {
        u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
        u32 itemInfoLen = 0;
        u16 enhanceLevel = 0;
        vm_net_mock_backpack_item_state *bagRow =
            role != NULL ? vm_net_mock_role_find_backpack_item(role, itemId, seq)
                         : NULL;

        if (bagRow != NULL)
            enhanceLevel = bagRow->enhanceLevel;
        if (vm_net_mock_build_item_use_iteminfo_blob(
                itemInfo, sizeof(itemInfo), seq, itemId, 1, enhanceLevel,
                &itemInfoLen) &&
            vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 7,
                                         &objectStart) &&
            vm_net_mock_put_object_u8(out, outCap, &pos, "type", 1) &&
            vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo", itemInfo,
                                       (u16)itemInfoLen))
        {
            vm_net_mock_finish_wt_object(out, objectStart, pos);
            objectCount = (u8)(objectCount + 1u);
        }
    }

    /*
     * 7/8 moves the row and re-runs wear-apply (0x010337c0 → 0x0100FED8).
     * Do NOT append same-packet live 1/1/14: that object is mmShop:0x9DE's
     * actor-state family.  Injecting it on mmGame then opening the mall
     * flashes the client (debug-a1d304; docs/re/2026-07-28-post-battle-
     * shop-open-vitals-crash.md).  Panel strings come from actor halfwords
     * after wear-apply (no host overlay file).
     */
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    {
        const char *respTag =
            actorinfoAppended ? "7/8+1/1/14"
                              : (objectCount > 1 ? "7/8+7/7" : "7/8");
        printf("[info][network] mock_item_equip type=%u requested_item=%u requested_seq=%u item=%u seq=%u slot=%u old=%u result=%u reason=%s vitals_arm=%u actorinfo=%u actorinfo_len=%u resp=%s evidence=JianghuOL.CBE:0x01033544+0x0100FED8+0x0100FA88(live)\n",
               parsed.type,
               parsed.itemId,
               parsed.seq,
               itemId,
               seq ? seq : parsed.seq,
               slot,
               oldItemId,
               result,
               reason ? reason : "-",
               vitalsArmed ? 1u : 0u,
               actorinfoAppended ? 1u : 0u,
               actorInfoLen,
               respTag);
        vm_autotest_note("mock_item_equip type=%u requested_item=%u requested_seq=%u item=%u seq=%u slot=%u old=%u result=%u reason=%s vitals_arm=%u actorinfo=%u response=%s evidence=JianghuOL.CBE:0x01033544+0x0100FA88(live)\n",
                         parsed.type,
                         parsed.itemId,
                         parsed.seq,
                         itemId,
                         seq ? seq : parsed.seq,
                         slot,
                         oldItemId,
                         result,
                         reason ? reason : "-",
                         vitalsArmed ? 1u : 0u,
                         actorinfoAppended ? 1u : 0u,
                         respTag);
    }
    return pos;
}

static u32 vm_net_mock_build_item_equip_swap_response(const u8 *request,
                                                      u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    vm_net_mock_item_equip_swap_request parsed;
    vm_net_mock_role_state *role = NULL;
    u32 itemId = 0;
    u32 oldItemId = 0;
    u8 slot = 0xff;
    const char *reason = "not-matched";
    u8 result = 0;
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_item_equip_swap_request(request, requestLen, &parsed))
    {
        return 0;
    }

    role = vm_net_mock_active_role();
    if (vm_net_mock_role_swap_equipped_backpack_item(role,
                                                      parsed.bodySeq,
                                                      parsed.backpackSeq,
                                                      &itemId,
                                                      &oldItemId,
                                                      &slot,
                                                      &reason))
    {
        result = 1;
    }

    /* JianghuOL.CBE:0x01033544 subtype 9 reads only result.  On success the
     * client itself moves body -> backpack (with bag's sequence), moves bag
     * into the equipment slot, invokes the UI callback, and clears the wait. */
    {
        u8 objectCount = 0;
        bool actorinfoAppended = false;
        u32 actorInfoLen = 0;

        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 9,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 1;
        /* Same as 7/8: no mmGame 1/1/14 after gear swap. */
        vm_net_mock_finish_wt_packet(out, pos, objectCount);

        printf("[info][network] mock_item_equip_swap body=%u bag=%u companion_2_10=%u item=%u old=%u slot=%u result=%u reason=%s actorinfo=%u actorinfo_len=%u resp=%s evidence=JianghuOL.CBE:0x010328D4+0x01033544+0x0100FA88(live)\n",
               parsed.bodySeq, parsed.backpackSeq,
               parsed.hasActorOtherCompanion ? 1u : 0u, itemId, oldItemId, slot, result,
               reason ? reason : "-",
               actorinfoAppended ? 1u : 0u, actorInfoLen,
               actorinfoAppended ? "7/9+1/1/14" : "7/9");
        vm_autotest_note("mock_item_equip_swap body=%u bag=%u companion_2_10=%u item=%u old=%u slot=%u result=%u reason=%s actorinfo=%u response=%s evidence=JianghuOL.CBE:0x010328D4+0x0100FA88(live)\n",
                         parsed.bodySeq, parsed.backpackSeq,
                         parsed.hasActorOtherCompanion ? 1u : 0u, itemId, oldItemId, slot,
                         result, reason ? reason : "-",
                         actorinfoAppended ? 1u : 0u,
                         actorinfoAppended ? "7/9+1/1/14" : "7/9");
    }
    return pos;
}

static bool vm_net_mock_equipment_enhance_decode_materials(
    const vm_net_mock_equipment_enhance_request *parsed,
    u32 itemIds[5],
    u8 counts[5])
{
    u32 rowSize = 0;

    if (parsed == NULL || parsed->occultInfo == NULL ||
        parsed->materialRows == 0 || parsed->materialRows > 5)
    {
        return false;
    }
    rowSize = parsed->occultInfoLen / parsed->materialRows;
    if (rowSize != 9)
        return false;
    for (u32 i = 0; i < parsed->materialRows; ++i)
    {
        const u8 *row = parsed->occultInfo + i * rowSize;
        u32 itemId = 0;
        u8 count = 0;

        if (row[0] != 0 || row[1] != 4 ||
            row[6] != 0 || row[7] != 1)
        {
            return false;
        }
        itemId = ((u32)row[2] << 24) | ((u32)row[3] << 16) |
                 ((u32)row[4] << 8) | (u32)row[5];
        count = row[8];
        if (itemId < VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_FIRST ||
            itemId > VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_LAST || count == 0)
        {
            return false;
        }
        itemIds[i] = itemId;
        counts[i] = count;
    }
    return true;
}

/*
 * Enhance success policy (2026-08-01):
 *   +L → +(L+1), target = L+1:
 *     crystal tier >= target → 100% per crystal
 *     each tier below target → floor(/3) once more (33%, 11%, 3%, ...)
 *   Example: +5→+6, ≥六级 100%, 五级 33%, 四级 11%, 三级 3%.
 *   Multiple crystals sum unit rates, capped at 100.
 *
 * Client local display (29/1): rate ≈ min(100, Σpower×100 / required[L]).
 * To show the same ladder without huge exponential data1 values (which
 * previously clamped usable maxlevel to 12), encode for the equipment's
 * current L only:
 *   required[any] = 100
 *   power[tier]   = unit_rate(L, tier)   // 100/33/11/3…
 * so local % equals unit_rate. After a successful +1, append a fresh 29/1
 * with the new L so the UI tables stay aligned.
 * Money stays on a separate compact curve (not the display required).
 */
static u32 vm_net_mock_equipment_enhance_crystal_unit_rate(u8 level, u32 tier)
{
    u32 rate = 100;
    u32 steps;

    if (tier == 0)
        return 0;
    /* Target tier (level+1) and above → 100%. */
    if (tier > (u32)level)
        return 100;
    /* Each step below target: floor(/3). */
    steps = (u32)level - tier + 1u;
    while (steps-- != 0)
        rate = rate / 3u;
    return rate;
}

static u32 vm_net_mock_equipment_enhance_success_rate(
    u8 level,
    u32 materialRows,
    const u32 itemIds[5],
    const u8 counts[5])
{
    u32 total = 0;

    if (itemIds == NULL || counts == NULL || materialRows == 0 ||
        materialRows > 5)
    {
        return 0;
    }
    for (u32 i = 0; i < materialRows; ++i)
    {
        u32 tier = itemIds[i] - VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_FIRST + 1;
        u32 unit = vm_net_mock_equipment_enhance_crystal_unit_rate(level, tier);
        u32 add = 0;

        if (counts[i] != 0 && unit > 0xffffffffu / counts[i])
            add = 0xffffffffu;
        else
            add = unit * counts[i];
        if (0xffffffffu - total < add)
            total = 0xffffffffu;
        else
            total += add;
    }
    return total > 100u ? 100u : total;
}

static u32 vm_net_mock_equipment_enhance_money_cost(u8 level)
{
    /* Product: 5 金 per step to next level (level is current before attempt).
     * +0→+1: 5金, +1→+2: 10金, …  role->money is copper (1金=10000铜). */
    u32 gold = ((u32)level + 1u) * 5u;
    return gold * (u32)VM_NET_MOCK_MONEY_COPPER_PER_GOLD;
}

static bool vm_net_mock_equipment_enhance_fill_rate_tables(
    u8 currentLevel,
    u8 *data1,
    u32 data1Cap,
    u32 *data1Len,
    u8 *data2,
    u32 data2Cap,
    u32 *data2Len)
{
    if (data1 == NULL || data2 == NULL || data1Len == NULL || data2Len == NULL)
        return false;
    *data1Len = 0;
    *data2Len = 0;
    /* required=100 → local displayed % equals crystal unit rate. */
    for (u32 level = 0; level <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++level)
    {
        if (!vm_net_mock_seq_put_u32(data1, data1Cap, data1Len, 100u))
            return false;
    }
    for (u32 tier = 1; tier <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++tier)
    {
        u32 unit =
            vm_net_mock_equipment_enhance_crystal_unit_rate(currentLevel, tier);
        if (!vm_net_mock_seq_put_u32(data2, data2Cap, data2Len, unit))
            return false;
    }
    return true;
}

static bool vm_net_mock_append_equipment_enhance_rate_table_refresh(
    u8 *out,
    u32 outCap,
    u32 *pos,
    u8 *objectCount,
    u8 currentLevel)
{
    u8 data1[128];
    u8 data2[128];
    u32 data1Len = 0;
    u32 data2Len = 0;
    u32 objectStart = 0;
    u32 savedPos = 0;
    u8 savedObjects = 0;

    if (out == NULL || pos == NULL || objectCount == NULL)
        return false;
    savedPos = *pos;
    savedObjects = *objectCount;
    memset(data1, 0, sizeof(data1));
    memset(data2, 0, sizeof(data2));
    if (!vm_net_mock_equipment_enhance_fill_rate_tables(
            currentLevel, data1, sizeof(data1), &data1Len, data2, sizeof(data2),
            &data2Len) ||
        !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 29, 1, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
        !vm_net_mock_put_object_u16(out, outCap, pos, "curlevel", currentLevel) ||
        !vm_net_mock_put_object_u16(out, outCap, pos, "maxlevel",
                                    VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "num1",
                                    VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL + 1) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "data1", data1,
                                    (u16)data1Len) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "num2",
                                    VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "data2", data2,
                                    (u16)data2Len))
    {
        *pos = savedPos;
        *objectCount = savedObjects;
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    *objectCount += 1;
    return true;
}

static bool vm_net_mock_equipment_enhance_validate_materials(
    vm_net_mock_role_state *role,
    const vm_net_mock_equipment_enhance_request *parsed,
    const u32 itemIds[5],
    const u8 counts[5],
    u32 *powerOut)
{
    u32 power = 0;

    if (powerOut)
        *powerOut = 0;
    if (role == NULL || parsed == NULL || parsed->materialRows == 0)
        return false;
    for (u32 i = 0; i < parsed->materialRows; ++i)
    {
        vm_net_mock_backpack_item_state *material = NULL;
        u32 requestedCount = 0;
        u32 tier = itemIds[i] - VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_FIRST + 1;
        u32 unit;
        u32 add = 0;

        for (u32 j = 0; j < parsed->materialRows; ++j)
        {
            if (itemIds[j] == itemIds[i])
                requestedCount += counts[j];
        }
        material = vm_net_mock_role_find_backpack_item(role, itemIds[i], 0);
        if (material == NULL || material->count < requestedCount)
            return false;
        /* Log helper only; display/roll use unit_rate independently. */
        unit = tier * 100u;
        if (counts[i] != 0 && unit > 0xffffffffu / counts[i])
            add = 0xffffffffu;
        else
            add = unit * counts[i];
        if (0xffffffffu - power < add)
            power = 0xffffffffu;
        else
            power += add;
    }
    if (powerOut)
        *powerOut = power;
    return true;
}

static bool vm_net_mock_build_equipment_enhance_material_blob(
    u8 *out,
    u32 outCap,
    const vm_net_mock_equipment_enhance_request *parsed,
    const u32 itemIds[5],
    const u8 counts[5],
    u32 *blobLenOut)
{
    u32 pos = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || parsed == NULL || blobLenOut == NULL)
        return false;
    for (u32 i = 0; i < parsed->materialRows; ++i)
    {
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, itemIds[i]) ||
            !vm_net_mock_seq_put_u8(out, outCap, &pos, counts[i]))
        {
            return false;
        }
    }
    *blobLenOut = pos;
    return true;
}

/*
 * After 29/3 deducts crystals, JianghuOL.CBE:0x010293F0 calls
 * 0x0102147C(r2=0): local occult consume updates the item-manager but skips
 * the 0x900 crystal-bag list redraw.  Main backpack UI still needs the same
 * stack-mutate contract as item-use (7/7 type=2 remaining + 7/11.info) or the
 * grid keeps the pre-enhance counts after leaving the enhance screen.
 * 1/10/26 mirrors discard/shop money refresh for the copper label.
 *
 * On result=1, 0x010287C0 only patches the enhance-UI item list using session
 * +0xa/+0xc (curlevel+1 / maxlevel).  Main backpack name draw at
 * 0x0103222E still reads the bag row's ParseEquipAttributes enhance
 * (common-extra second i16 → item+0xe).  Without a bag-row 7/7 type=2 rewrite
 * carrying the new enhance, opening the backpack shows (+0).
 *
 * Equipment 7/7 type=2 (mmGame:0xD04 → item-manager +104 with r2=-1) does not
 * reliably rewrite an existing bag row's full common-extra when attr_count
 * grows (L>=4 强化附加).  Authoritative 17/1(+7/42) rebuilds the manager, but
 * must NOT ride inside the 29/3 WT: event_packet_parse_WT caps objects at 10,
 * and 17/1 re-arms DF_DataPackage_DoLoading without a same-packet 26/0/30/2.
 * list-only poll (no 26/0) left the progress bar up after success
 * (docs/re/2026-08-05-enhance-success-progress-stuck.md).  Same-packet keeps
 * 29/3+7/7+7/11(+money); poll pushes 17/1 then lone 26/0.
 */
#define VM_NET_MOCK_WT_EVENT7_OBJECT_CAP 10u
static void vm_net_mock_arm_enhance_backpack_list_resync(u16 equipSeq,
                                                         u32 equipItemId,
                                                         u16 enhanceLevel,
                                                         bool inlineListOk);

static bool vm_net_mock_append_equipment_enhance_inventory_sync(
    u8 *out,
    u32 outCap,
    u32 *pos,
    u8 *objectCount,
    const u32 itemIds[5],
    const u16 seqs[5],
    const u32 remainings[5],
    u8 syncRows,
    u16 equipSeq,
    u32 equipItemId,
    u16 equipEnhanceLevel,
    bool syncEquipment,
    bool syncMoney,
    bool includeBackpackList,
    u32 money,
    bool *listResyncedOut)
{
    u32 savedPos = 0;
    u8 savedObjects = 0;

    if (listResyncedOut)
        *listResyncedOut = false;
    if (out == NULL || pos == NULL || objectCount == NULL)
        return false;
    savedPos = *pos;
    savedObjects = *objectCount;

    {
        u8 reserve = 0;

        if (syncEquipment && equipSeq != 0 && equipItemId != 0)
            reserve = (u8)(reserve + 2u);
        if (syncMoney)
            reserve = (u8)(reserve + 1u);
        /*
         * Inline 17/1(+7/42) is deferred to poll+26/0.  Keep crystal/equip/money
         * under the event-7 object cap so 29/3 itself is still parseable.
         */
        if (includeBackpackList)
            reserve = (u8)(reserve + 2u);

        for (u32 i = 0; i < syncRows; ++i)
        {
            u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
            u8 countInfo[32];
            u32 itemInfoLen = 0;
            u32 countInfoLen = 0;
            u32 objectStart = 0;

            if (seqs[i] == 0 || itemIds[i] == 0)
                continue;
            if ((u32)(*objectCount) + 2u + reserve > VM_NET_MOCK_WT_EVENT7_OBJECT_CAP)
            {
                printf("[warn][network] mock_equipment_enhance crystal-sync-budget "
                       "seq=%u objects=%u reserve=%u action=defer-via-17/1-poll "
                       "evidence=event_packet_parse_WT-object-cap-10\n",
                       seqs[i], *objectCount, reserve);
                break;
            }
            if (!vm_net_mock_build_item_use_iteminfo_blob(
                    itemInfo, sizeof(itemInfo), seqs[i], itemIds[i], remainings[i],
                    0, &itemInfoLen) ||
                !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 7,
                                             &objectStart) ||
                !vm_net_mock_put_object_u8(out, outCap, pos, "type", 2) ||
                !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo,
                                            (u16)itemInfoLen))
            {
                *pos = savedPos;
                *objectCount = savedObjects;
                return false;
            }
            vm_net_mock_finish_wt_object(out, objectStart, *pos);
            *objectCount += 1;

            if (!vm_net_mock_build_item_use_count_info_blob(
                    countInfo, sizeof(countInfo), seqs[i], remainings[i],
                    &countInfoLen) ||
                !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 11,
                                             &objectStart) ||
                !vm_net_mock_put_object_raw(out, outCap, pos, "info", countInfo,
                                            (u16)countInfoLen))
            {
                *pos = savedPos;
                *objectCount = savedObjects;
                return false;
            }
            vm_net_mock_finish_wt_object(out, objectStart, *pos);
            *objectCount += 1;
        }
    }

    if (syncEquipment && equipSeq != 0 && equipItemId != 0)
    {
        /* L>=4 common-extra includes attr slots; shared scratch covers max row. */
        u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
        u8 countInfo[32];
        u32 itemInfoLen = 0;
        u32 countInfoLen = 0;
        u32 objectStart = 0;
        u8 reserve = syncMoney ? 1u : 0u;

        if (includeBackpackList)
            reserve = (u8)(reserve + 2u);
        if ((u32)(*objectCount) + 2u + reserve > VM_NET_MOCK_WT_EVENT7_OBJECT_CAP)
        {
            printf("[warn][network] mock_equipment_enhance equip-sync-budget "
                   "seq=%u objects=%u action=skip-inline-equip-7/7 "
                   "evidence=event_packet_parse_WT-object-cap-10\n",
                   equipSeq, *objectCount);
        }
        /* Backpack row count stays instance 1; enhance is in common-extra. */
        else if (!vm_net_mock_build_item_use_iteminfo_blob(
                itemInfo, sizeof(itemInfo), equipSeq, equipItemId, 1,
                equipEnhanceLevel, &itemInfoLen) ||
            !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 7,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "type", 2) ||
            !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo,
                                        (u16)itemInfoLen))
        {
            *pos = savedPos;
            *objectCount = savedObjects;
            return false;
        }
        else
        {
            vm_net_mock_finish_wt_object(out, objectStart, *pos);
            *objectCount += 1;

            if (!vm_net_mock_build_item_use_count_info_blob(
                    countInfo, sizeof(countInfo), equipSeq, 1, &countInfoLen) ||
                !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 11,
                                             &objectStart) ||
                !vm_net_mock_put_object_raw(out, outCap, pos, "info", countInfo,
                                            (u16)countInfoLen))
            {
                *pos = savedPos;
                *objectCount = savedObjects;
                return false;
            }
            vm_net_mock_finish_wt_object(out, objectStart, *pos);
            *objectCount += 1;
        }
    }

    if (syncMoney)
    {
        u32 objectStart = 0;
        u8 reserve = includeBackpackList ? 2u : 0u;

        if ((u32)(*objectCount) + 1u + reserve > VM_NET_MOCK_WT_EVENT7_OBJECT_CAP)
        {
            printf("[warn][network] mock_equipment_enhance money-sync-budget "
                   "objects=%u action=skip-10/26 "
                   "evidence=event_packet_parse_WT-object-cap-10\n",
                   *objectCount);
        }
        else if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 0x0a, 0x1a,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "type", 1) ||
            !vm_net_mock_put_object_u8(out, outCap, pos, "npcnum", 0) ||
            !vm_net_mock_put_object_string(out, outCap, pos, "name",
                                           "\xce\xde") ||
            !vm_net_mock_put_object_u32(out, outCap, pos, "money", money))
        {
            *pos = savedPos;
            *objectCount = savedObjects;
            return false;
        }
        else
        {
            vm_net_mock_finish_wt_object(out, objectStart, *pos);
            *objectCount += 1;
        }
    }

    /*
     * Authoritative bag list rebuilds L>=4 强化附加, but inline 17/1 in the
     * 29/3 WT both burns the object cap and re-arms DoLoading.  Callers that
     * still pass includeBackpackList=true must fit under the remaining budget;
     * enhance success prefers poll 17/1 + 26/0 instead.
     */
    if (syncEquipment && includeBackpackList)
    {
        u32 listPos = *pos;
        u8 listObjects = *objectCount;

        if ((u32)(*objectCount) + 1u > VM_NET_MOCK_WT_EVENT7_OBJECT_CAP)
        {
            printf("[warn][network] mock_equipment_enhance bag-list-budget "
                   "objects=%u action=skip-inline-17/1 "
                   "evidence=event_packet_parse_WT-object-cap-10\n",
                   *objectCount);
        }
        else if (vm_net_mock_append_backpack_items_object(out, outCap, pos))
        {
            *objectCount += 1;
            if ((u32)(*objectCount) < VM_NET_MOCK_WT_EVENT7_OBJECT_CAP &&
                vm_net_mock_append_books42_object(out, outCap, pos))
            {
                *objectCount += 1;
            }
            if (listResyncedOut)
                *listResyncedOut = true;
        }
        else
        {
            *pos = listPos;
            *objectCount = listObjects;
        }
    }
    return true;
}

static u32 vm_mock_service_broadcast_world_chat_live(const char *sourceName,
                                                     const char *message);
static void vm_net_mock_equipment_enhance_maybe_world_announce(u32 itemId,
                                                              u16 newLevel);
static u32 vm_net_mock_marriage_world_buff_multiplier(void);
static void vm_mock_service_marriage_celebrate_and_arm_world_buff(
    const char *nameA,
    const char *nameB);

static u32 vm_net_mock_build_equipment_enhance_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_net_mock_equipment_enhance_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *equipment = NULL;
    const vm_net_mock_equipment_catalog_item *catalog = NULL;
    u32 itemIds[5];
    u8 counts[5];
    u16 consumedSeqs[5];
    u32 consumedItemIds[5];
    u32 consumedRemainings[5];
    u8 data1[128];
    u8 data2[128];
    u8 occult[64];
    u32 data1Len = 0;
    u32 data2Len = 0;
    u32 occultLen = 0;
    u32 materialPower = 0;
    u32 successRate = 0;
    u32 moneyCost = 0;
    u32 equipmentItemId = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    u8 result = 1;
    u8 currentLevel = 0;
    u8 consumedRows = 0;
    bool materialsValid = false;
    bool enhancementSucceeded = false;
    bool inventorySynced = false;
    const char *reason = "ok";

    memset(&parsed, 0, sizeof(parsed));
    memset(itemIds, 0, sizeof(itemIds));
    memset(counts, 0, sizeof(counts));
    memset(consumedSeqs, 0, sizeof(consumedSeqs));
    memset(consumedItemIds, 0, sizeof(consumedItemIds));
    memset(consumedRemainings, 0, sizeof(consumedRemainings));
    memset(data1, 0, sizeof(data1));
    memset(data2, 0, sizeof(data2));
    memset(occult, 0, sizeof(occult));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_equipment_enhance_request(request, requestLen,
                                                      &parsed))
    {
        return 0;
    }

    role = vm_net_mock_active_role();
    if (role != NULL)
        equipment = vm_net_mock_role_find_backpack_item(role, 0, parsed.equipSeq);
    if (equipment != NULL)
    {
        equipmentItemId = equipment->itemId;
        catalog = vm_net_mock_find_equipment_catalog_item(equipment->itemId);
    }
    if (equipment == NULL || catalog == NULL)
    {
        result = parsed.subtype == 1 ? 2 : 3;
        reason = "equipment-not-found";
    }
    else
    {
        currentLevel = (u8)SDL_min(
            equipment->enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
        if (currentLevel >= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        {
            result = parsed.subtype == 1 ? 3 : 5;
            reason = "max-level";
        }
    }

    if (parsed.subtype != 1 && result == 1)
    {
        materialsValid = vm_net_mock_equipment_enhance_decode_materials(
                             &parsed, itemIds, counts) &&
                         vm_net_mock_equipment_enhance_validate_materials(
                             role, &parsed, itemIds, counts, &materialPower);
        if (!materialsValid)
        {
            result = 4;
            reason = "crystal-insufficient";
        }
        else
        {
            successRate = vm_net_mock_equipment_enhance_success_rate(
                currentLevel, parsed.materialRows, itemIds, counts);
            moneyCost = vm_net_mock_equipment_enhance_money_cost(currentLevel);
        }
    }

    if (parsed.subtype == 3 && result == 1)
    {
        if (role->money < moneyCost)
        {
            result = 6;
            reason = "money-insufficient";
        }
        else
        {
            for (u32 i = 0; i < parsed.materialRows; ++i)
            {
                vm_net_mock_backpack_item_state *material = NULL;
                u32 remaining = 0;
                u32 consumeCount = counts[i];
                bool alreadyConsumed = false;

                for (u32 j = 0; j < i; ++j)
                {
                    if (itemIds[j] == itemIds[i])
                    {
                        alreadyConsumed = true;
                        break;
                    }
                }
                if (alreadyConsumed)
                    continue;
                for (u32 j = i + 1; j < parsed.materialRows; ++j)
                {
                    if (itemIds[j] == itemIds[i])
                        consumeCount += counts[j];
                }
                material = vm_net_mock_role_find_backpack_item(
                    role, itemIds[i], 0);
                if (material == NULL)
                {
                    result = 4;
                    reason = "crystal-consume-failed";
                    break;
                }
                {
                    u16 materialSeq = material->seq;

                    if (!vm_net_mock_role_consume_backpack_item(
                            role, itemIds[i], materialSeq, consumeCount,
                            &remaining))
                    {
                        result = 4;
                        reason = "crystal-consume-failed";
                        break;
                    }
                    if (consumedRows < 5)
                    {
                        consumedSeqs[consumedRows] = materialSeq;
                        consumedItemIds[consumedRows] = itemIds[i];
                        consumedRemainings[consumedRows] = remaining;
                        consumedRows += 1;
                    }
                }
            }
            if (result == 1)
            {
                u32 roll = (g_schedulerTick +
                            (u32)parsed.equipSeq * 17u +
                            (u32)currentLevel * 31u) % 100u;
                role->money -= moneyCost;
                enhancementSucceeded = roll < successRate;
                result = enhancementSucceeded ? 1 : 2;
                equipment = vm_net_mock_role_find_backpack_item(
                    role, 0, parsed.equipSeq);
                if (enhancementSucceeded && equipment != NULL)
                    equipment->enhanceLevel = (u16)(currentLevel + 1);
                vm_net_mock_role_mark_inventory_dirty(enhancementSucceeded
                                             ? "equipment-enhance-success"
                                             : "equipment-enhance-failed");
                reason = enhancementSucceeded ? "success" : "failed-roll";
            }
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29,
                                     parsed.subtype, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    if (parsed.subtype == 1)
    {
        if (result == 1 &&
            !vm_net_mock_equipment_enhance_fill_rate_tables(
                currentLevel, data1, sizeof(data1), &data1Len, data2,
                sizeof(data2), &data2Len))
        {
            return 0;
        }
        if (!vm_net_mock_put_object_u16(out, outCap, &pos, "curlevel",
                                        currentLevel) ||
            !vm_net_mock_put_object_u16(
                out, outCap, &pos, "maxlevel",
                VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL) ||
            !vm_net_mock_put_object_u8(
                out, outCap, &pos, "num1",
                result == 1 ? VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL + 1 : 0) ||
            (result == 1 &&
             !vm_net_mock_put_object_raw(out, outCap, &pos, "data1", data1,
                                         (u16)data1Len)) ||
            !vm_net_mock_put_object_u8(
                out, outCap, &pos, "num2",
                result == 1 ? VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL : 0) ||
            (result == 1 &&
             !vm_net_mock_put_object_raw(out, outCap, &pos, "data2", data2,
                                         (u16)data2Len)))
        {
            return 0;
        }
    }
    else if (parsed.subtype == 2)
    {
        /* HandleItemUseAndEquip(0x01028C7C) reads both fields through the
         * response object's 32-bit numeric accessor (+68). */
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "value",
                                        successRate) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "money",
                                        moneyCost))
        {
            return 0;
        }
    }
    else if (result == 1 || result == 2)
    {
        /* Prefer echoing the client's occultinfo bytes so 0x0102147C parses
         * the exact tagged stream it flushed on the request. */
        if (parsed.occultInfo != NULL && parsed.occultInfoLen > 0 &&
            parsed.occultInfoLen <= sizeof(occult))
        {
            memcpy(occult, parsed.occultInfo, parsed.occultInfoLen);
            occultLen = parsed.occultInfoLen;
        }
        else if (!vm_net_mock_build_equipment_enhance_material_blob(
                      occult, sizeof(occult), &parsed, itemIds, counts,
                      &occultLen))
        {
            return 0;
        }
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "tnum",
                                        parsed.materialRows) ||
            !vm_net_mock_put_object_u16(out, outCap, &pos, "equipseq",
                                        parsed.equipSeq) ||
            !vm_net_mock_put_object_raw(out, outCap, &pos, "occult", occult,
                                        (u16)occultLen))
        {
            return 0;
        }
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    if (parsed.subtype == 3 && (result == 1 || result == 2) && role != NULL)
    {
        u16 syncedEnhance = 0;
        bool syncEquipment = false;
        bool listResynced = false;

        if (enhancementSucceeded)
        {
            equipment = vm_net_mock_role_find_backpack_item(
                role, 0, parsed.equipSeq);
            if (equipment != NULL)
            {
                syncedEnhance = equipment->enhanceLevel;
                syncEquipment = true;
                if (equipmentItemId == 0)
                    equipmentItemId = equipment->itemId;
            }
        }
        /*
         * Same-packet: crystal/equip 7/7+7/11 + money only.  Do not inline
         * 17/1 here — it re-opens DoLoading and often exceeds the 10-object
         * event-7 cap with multi-crystal.  Poll delivers 17/1 then 26/0.
         */
        inventorySynced = vm_net_mock_append_equipment_enhance_inventory_sync(
            out, outCap, &pos, &objectCount, consumedItemIds, consumedSeqs,
            consumedRemainings, consumedRows, parsed.equipSeq, equipmentItemId,
            syncedEnhance, syncEquipment, true, false, role->money,
            &listResynced);
        if (!inventorySynced)
        {
            printf("[warn][network] mock_equipment_enhance inventory-sync-failed "
                   "seq=%u crystals=%u equip_sync=%u evidence=keep-29/3-only\n",
                   parsed.equipSeq, consumedRows, syncEquipment ? 1 : 0);
        }
        else if (enhancementSucceeded && syncEquipment && equipmentItemId != 0)
        {
            vm_net_mock_arm_enhance_backpack_list_resync(
                parsed.equipSeq, equipmentItemId, syncedEnhance, listResynced);
            vm_net_mock_equipment_enhance_maybe_world_announce(
                equipmentItemId, syncedEnhance);
        }
        /* Keep client local power/required tables on the new enhance level. */
        if (enhancementSucceeded && syncEquipment &&
            syncedEnhance <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        {
            if ((u32)objectCount + 1u > VM_NET_MOCK_WT_EVENT7_OBJECT_CAP)
            {
                printf("[warn][network] mock_equipment_enhance rate-table-budget "
                       "seq=%u level=%u objects=%u action=skip-inline-29/1 "
                       "evidence=event_packet_parse_WT-object-cap-10\n",
                       parsed.equipSeq, syncedEnhance, objectCount);
            }
            else if (!vm_net_mock_append_equipment_enhance_rate_table_refresh(
                    out, outCap, &pos, &objectCount, (u8)syncedEnhance))
            {
                printf("[warn][network] mock_equipment_enhance rate-table-refresh-failed "
                       "seq=%u level=%u evidence=keep-prior-objects\n",
                       parsed.equipSeq, syncedEnhance);
            }
        }
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    printf("[info][network] mock_equipment_enhance phase=%u seq=%u item=%u level=%u result=%u crystals=%u power=%u rate=%u money=%u success=%u inv_sync=%u reason=%s resp=29/%u%s evidence=JianghuOL.CBE:0x0101CD1E+0x0101DD1E+0x01028C7C+0x0102147C+0x010287C0+0x0103222E\n",
           parsed.subtype, parsed.equipSeq,
           equipmentItemId, currentLevel, result,
           parsed.materialRows, materialPower, successRate, moneyCost,
           enhancementSucceeded ? 1 : 0, inventorySynced ? 1 : 0, reason,
           parsed.subtype,
           inventorySynced
               ? (enhancementSucceeded
                      ? "+7/7-type2+7/11-crystal+equip+10/26+29/1"
                      : "+7/7-type2+7/11-crystal+10/26")
               : "");
    vm_autotest_note("mock_equipment_enhance phase=%u seq=%u item=%u level=%u result=%u crystals=%u power=%u rate=%u money=%u success=%u inv_sync=%u reason=%s response=29/%u%s evidence=JianghuOL.CBE:0x0101CD1E+0x0101DD1E+0x01028C7C+0x0102147C+0x010287C0+0x0103222E\n",
                     parsed.subtype, parsed.equipSeq,
                     equipmentItemId, currentLevel, result,
                     parsed.materialRows, materialPower, successRate, moneyCost,
                     enhancementSucceeded ? 1 : 0, inventorySynced ? 1 : 0,
                     reason, parsed.subtype,
                     inventorySynced
                         ? (enhancementSucceeded
                                ? "+7/7-type2+7/11-crystal+equip+10/26+29/1"
                                : "+7/7-type2+7/11-crystal+10/26")
                         : "");
    return pos;
}

/*
 * Transfer fee (product):
 *   29/5 wire money fixed at 7 → UI「2.金钱」≈48金65银
 *     (calibrated: display铜 ≈ 27664 + money*65536).
 *   29/6 charges the same 48金65银 (copper). No name `(N金)` suffix.
 *   level=0 keeps materials+target UI (level==1 drops both).
 */
#define VM_NET_MOCK_EQUIP_ENHANCE_TRANSFER_MONEY_WIRE 7u
#define VM_NET_MOCK_EQUIP_ENHANCE_TRANSFER_FEE_GOLD 48u
#define VM_NET_MOCK_EQUIP_ENHANCE_TRANSFER_FEE_SILVER 65u
#define VM_NET_MOCK_MONEY_COPPER_PER_SILVER 100u

static u32 vm_net_mock_equipment_enhance_transfer_money_cost(void)
{
    return VM_NET_MOCK_EQUIP_ENHANCE_TRANSFER_FEE_GOLD *
               (u32)VM_NET_MOCK_MONEY_COPPER_PER_GOLD +
           VM_NET_MOCK_EQUIP_ENHANCE_TRANSFER_FEE_SILVER *
               VM_NET_MOCK_MONEY_COPPER_PER_SILVER;
}

/* Always 0: materials UI (level!=1); do not inflate halfword fee path. */
static u8 vm_net_mock_equipment_enhance_transfer_wire_level(u8 sourceLevel)
{
    (void)sourceLevel;
    return 0;
}

/*
 * 强化转移 / 重铸 (WT 29/5 select/preview, 29/6 confirm).
 *
 * Request: 1/29/{5|6} { seqd, seqs } — seqs=source (当前), seqd=dest (目标).
 * 29/5: result=1 + flag/id/seq/name/money=7/level=0 opens fee UI; no charge.
 * 29/6: source=0, dest=sourceLevel, charge 48金65银; result=4 金钱不足.
 * Parser: HandleItemUseAndEquip 0x010294AA (29/5) / 0x01029690 (29/6).
 */
static u32 vm_net_mock_build_equipment_enhance_transfer_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_net_mock_equipment_enhance_transfer_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *source = NULL;
    vm_net_mock_backpack_item_state *dest = NULL;
    const vm_net_mock_equipment_catalog_item *sourceCatalog = NULL;
    const vm_net_mock_equipment_catalog_item *destCatalog = NULL;
    const vm_net_mock_shop_catalog_item *destShop = NULL;
    const char *destName = "\xD7\xB0\xB1\xB8"; /* 装备 */
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    u8 result = 1;
    u8 sourceLevel = 0;
    u8 destLevelBefore = 0;
    u8 destLevelAfter = 0;
    u8 sourceLevelAfter = 0;
    u32 moneyCost = 0;
    u32 feeCopper = vm_net_mock_equipment_enhance_transfer_money_cost();
    u8 wireLevel = 0;
    bool transferred = false;
    bool alreadyDone = false;
    bool inventorySynced = false;
    const char *reason = "ok";

    memset(&parsed, 0, sizeof(parsed));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_equipment_enhance_transfer_request(
            request, requestLen, &parsed))
    {
        return 0;
    }

    role = vm_net_mock_active_role();
    if (role != NULL)
    {
        source = vm_net_mock_role_find_backpack_item(role, 0, parsed.sourceSeq);
        dest = vm_net_mock_role_find_backpack_item(role, 0, parsed.destSeq);
    }
    if (source != NULL)
        sourceCatalog = vm_net_mock_find_equipment_catalog_item(source->itemId);
    if (dest != NULL)
        destCatalog = vm_net_mock_find_equipment_catalog_item(dest->itemId);

    if (source == NULL || dest == NULL ||
        sourceCatalog == NULL || destCatalog == NULL)
    {
        result = 2; /* 29/5 tip: 装备不存在！ */
        reason = "equipment-not-found";
    }
    else if (sourceCatalog->slot != destCatalog->slot)
    {
        /* 29/6: 不可转移; 29/5: clear wait without「装备不存在」 */
        result = 5;
        reason = "slot-mismatch";
    }
    else if (sourceCatalog->quality != destCatalog->quality)
    {
        /* 不同品质不可互相转移（equip.dsh 品质列） */
        result = 5;
        reason = "quality-mismatch";
    }
    else
    {
        sourceLevel = (u8)SDL_min(source->enhanceLevel,
                                  VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
        destLevelBefore = (u8)SDL_min(dest->enhanceLevel,
                                      VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
        sourceLevelAfter = sourceLevel;
        destLevelAfter = destLevelBefore;
        wireLevel =
            vm_net_mock_equipment_enhance_transfer_wire_level(sourceLevel);

        if (sourceLevel == 0)
        {
            if (parsed.subtype == 6 && destLevelBefore > 0)
            {
                /* Confirm replay after success: ACK, no second charge. */
                alreadyDone = true;
                reason = "already-transferred";
            }
            else
            {
                result = 5;
                reason = "source-not-enhanced";
            }
        }
        else if (destLevelBefore >= sourceLevel)
        {
            result = 5;
            reason = "dest-level-not-lower";
        }
        else if (parsed.subtype == 5)
        {
            /*
             * Select/preview only: open fee confirm UI.  Client shows 扣金币
             * after this ACK; charge happens on 29/6.
             */
            destLevelAfter = sourceLevel; /* expected after confirm */
            sourceLevelAfter = 0;
            reason = "preview-ok";
        }
        else if (role->money < feeCopper)
        {
            result = 4; /* 29/6 native「金钱不足」 */
            reason = "money-insufficient";
        }
        else
        {
            moneyCost = feeCopper;
            destLevelAfter = sourceLevel; /* no level loss */
            sourceLevelAfter = 0;
            source->enhanceLevel = sourceLevelAfter;
            dest->enhanceLevel = destLevelAfter;
            vm_net_mock_enhance_wash_copy(&dest->enhanceWash, &source->enhanceWash);
            vm_net_mock_enhance_wash_clear(&source->enhanceWash);
            role->money -= moneyCost;
            transferred = true;
            vm_net_mock_role_mark_inventory_dirty("equipment-enhance-transfer");
            reason = "transferred";
        }
    }

    if (dest != NULL)
    {
        destShop = vm_net_mock_find_shop_catalog_item(dest->itemId);
        if (destShop != NULL && destShop->name[0] != 0)
            destName = destShop->name;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29,
                                     parsed.subtype, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }

    if (parsed.subtype == 5)
    {
        u16 moneyWire = (u16)VM_NET_MOCK_EQUIP_ENHANCE_TRANSFER_MONEY_WIRE;

        if (result == 1 &&
            (!vm_net_mock_put_object_u8(out, outCap, &pos, "flag", 1) ||
             !vm_net_mock_put_object_u16(
                 out, outCap, &pos, "id",
                 dest != NULL ? (u16)(dest->itemId & 0xffffu) : 0) ||
             !vm_net_mock_put_object_u16(out, outCap, &pos, "seq",
                                         parsed.destSeq) ||
             !vm_net_mock_put_object_string(out, outCap, &pos, "name",
                                            destName) ||
             !vm_net_mock_put_object_u16(out, outCap, &pos, "money",
                                         moneyWire) ||
             !vm_net_mock_put_object_u8(out, outCap, &pos, "level",
                                        wireLevel)))
        {
            return 0;
        }
    }
    else if (result == 1)
    {
        /* 0x01029690 success: seqs/curlevels then seqd/curleveld. */
        if (!vm_net_mock_put_object_u16(out, outCap, &pos, "seqs",
                                         parsed.sourceSeq) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "curlevels",
                                       sourceLevelAfter) ||
            !vm_net_mock_put_object_u16(out, outCap, &pos, "seqd",
                                         parsed.destSeq) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "curleveld",
                                       destLevelAfter))
        {
            return 0;
        }
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    /* Inventory/money sync only after confirm mutation (not 29/5 preview). */
    if (result == 1 && role != NULL && (transferred || alreadyDone) &&
        parsed.subtype == 6 && source != NULL && dest != NULL)
    {
        bool listResynced = false;
        u16 emptySeqs[5];
        u32 emptyIds[5];
        u32 emptyRemainings[5];

        memset(emptySeqs, 0, sizeof(emptySeqs));
        memset(emptyIds, 0, sizeof(emptyIds));
        memset(emptyRemainings, 0, sizeof(emptyRemainings));

        /*
         * Narrow transfer sync: never double-inline 17/1 (解包错误).  Prefer
         * 7/7+7/11 for both seats + money; bag list + 26/0 on poll.
         */
        inventorySynced = vm_net_mock_append_equipment_enhance_inventory_sync(
            out, outCap, &pos, &objectCount, emptyIds, emptySeqs,
            emptyRemainings, 0, parsed.sourceSeq, source->itemId,
            sourceLevelAfter, true, false, false, role->money, &listResynced);
        if (inventorySynced)
        {
            bool destListOk = false;
            if (!vm_net_mock_append_equipment_enhance_inventory_sync(
                    out, outCap, &pos, &objectCount, emptyIds, emptySeqs,
                    emptyRemainings, 0, parsed.destSeq, dest->itemId,
                    destLevelAfter, true, true, false, role->money,
                    &destListOk))
            {
                inventorySynced = false;
            }
            else
            {
                vm_net_mock_arm_enhance_backpack_list_resync(
                    parsed.destSeq, dest->itemId, destLevelAfter, false);
            }
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_equipment_enhance_transfer phase=%u "
           "source_seq=%u dest_seq=%u source_item=%u dest_item=%u "
           "source_level=%u->%u dest_before=%u dest_after=%u money_cost=%u "
           "money_after=%u result=%u transferred=%u inv_sync=%u reason=%s "
           "resp=29/%u evidence=JianghuOL.CBE:0x0101DB3A+0x010294AA+"
           "0x01029690+0x010287C0+0x010289E2\n",
           parsed.subtype, parsed.sourceSeq, parsed.destSeq,
           source ? source->itemId : 0, dest ? dest->itemId : 0,
           sourceLevel, sourceLevelAfter, destLevelBefore, destLevelAfter,
           moneyCost, role ? role->money : 0, result,
           transferred ? 1 : 0, inventorySynced ? 1 : 0, reason,
           parsed.subtype);
    vm_autotest_note("mock_equipment_enhance_transfer phase=%u source_seq=%u "
                     "dest_seq=%u source_level=%u->%u dest_after=%u "
                     "money_cost=%u result=%u reason=%s response=29/%u\n",
                     parsed.subtype, parsed.sourceSeq, parsed.destSeq,
                     sourceLevel, sourceLevelAfter, destLevelAfter, moneyCost,
                     result, reason, parsed.subtype);
    return pos;
}

/*
 * Wash crystal success rate.
 * Base: unit_rate(unlock, tier) × count, capped at 100.
 * Special (+16 + 十六级玄晶): 1→33%, 2→66%, 3→100% (count*100/3).
 */
static u32 vm_net_mock_equipment_enhance_wash_success_rate(
    u8 unlockLevel,
    u32 crystalTier,
    u8 crystalCount)
{
    u32 unit;
    u32 total;

    if (crystalCount == 0 || crystalTier == 0)
        return 0;
    if (unlockLevel == 16 && crystalTier == 16)
    {
        if (crystalCount >= 3u)
            return 100u;
        return (crystalCount * 100u) / 3u;
    }
    unit = vm_net_mock_equipment_enhance_crystal_unit_rate(unlockLevel,
                                                           crystalTier);
    if (unit == 0)
        return 0;
    if (crystalCount > 1u &&
        unit > 0xffffffffu / (u32)crystalCount)
        total = 0xffffffffu;
    else
        total = unit * (u32)crystalCount;
    return total > 100u ? 100u : total;
}

/*
 * PixiJS-only 强化洗练 (WT 29/7 open, 29/8 preview, 29/9 confirm).
 * Crystal rate: unit_rate(unlock, tier) — tier > unlock → 100%.
 * +16 档可用 1..3 颗十六级玄晶叠成功率（3 颗 = 100%）.
 * Gold: 5*(washCount+1) 金 per attempt on that equip+tier.
 * Success (29/9): consume + wash_count++ + roll; attrs stay pending until 29/14.
 */
static u32 vm_net_mock_build_equipment_enhance_wash_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_net_mock_equipment_enhance_wash_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *equipment = NULL;
    const vm_net_mock_equipment_catalog_item *catalog = NULL;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    u8 result = 1;
    u8 currentLevel = 0;
    u32 successRate = 0;
    u32 moneyCost = 0;
    u8 tierIndex = 0;
    int unlockIndex = -1;
    bool washSucceeded = false;
    bool inventorySynced = false;
    u8 rolledType = 0;
    u8 rolledFlag = 0;
    u16 rolledValue = 0;
    u8 oldType = 0;
    u8 oldFlag = 0;
    u16 oldValue = 0;
    u8 oldWashed = 0;
    const char *reason = "ok";
    u8 tiersBlob[4 * 12];
    u32 tiersLen = 0;
    u8 poolBlob[VM_NET_MOCK_ENHANCE_ATTR_POOL_MAX * 6];
    u32 poolLen = 0;

    memset(&parsed, 0, sizeof(parsed));
    memset(tiersBlob, 0, sizeof(tiersBlob));
    memset(poolBlob, 0, sizeof(poolBlob));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_equipment_enhance_wash_request(request, requestLen,
                                                          &parsed))
    {
        return 0;
    }

    (void)vm_net_mock_enhance_config_db_load();
    role = vm_net_mock_active_role();
    if (role != NULL)
        equipment = vm_net_mock_role_find_backpack_item(role, 0, parsed.equipSeq);
    if (equipment != NULL)
        catalog = vm_net_mock_find_equipment_catalog_item(equipment->itemId);
    if (equipment == NULL || catalog == NULL)
    {
        result = 3;
        reason = "equipment-not-found";
    }
    else
    {
        currentLevel = (u8)SDL_min(equipment->enhanceLevel,
                                   VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
    }

    if (parsed.subtype != 7 && result == 1)
    {
        unlockIndex = vm_net_mock_enhance_index_for_unlock(parsed.unlockLevel);
        if (unlockIndex < 0)
        {
            result = 5;
            reason = "bad-unlock";
        }
        else if (currentLevel < parsed.unlockLevel)
        {
            result = 5;
            reason = "tier-locked";
        }
        else
        {
            u32 crystalTier =
                parsed.crystalItemId - VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_FIRST +
                1u;
            u16 washCount =
                equipment->enhanceWash.slots[unlockIndex].washCount;
            moneyCost = vm_net_mock_enhance_wash_next_copper(washCount);
            successRate = vm_net_mock_equipment_enhance_wash_success_rate(
                parsed.unlockLevel, crystalTier, parsed.crystalCount);
            if (successRate == 0)
            {
                result = 4;
                reason = "crystal-rate-zero";
            }
        }
    }

    if (parsed.subtype == 9 && result == 1)
    {
        vm_net_mock_backpack_item_state *crystal = NULL;
        u32 remaining = 0;
        u8 needCount =
            parsed.crystalCount != 0 ? parsed.crystalCount : 1u;

        if (role->money < moneyCost)
        {
            result = 6;
            reason = "money-insufficient";
        }
        else
        {
            crystal = vm_net_mock_role_find_backpack_item(
                role, parsed.crystalItemId, 0);
            if (crystal == NULL || crystal->count < needCount ||
                !vm_net_mock_role_consume_backpack_item(
                    role, parsed.crystalItemId, crystal->seq, needCount,
                    &remaining))
            {
                result = 4;
                reason = "crystal-insufficient";
            }
            else
            {
                u32 roll = 0;
                u16 priorWashCount = 0;

                unlockIndex =
                    vm_net_mock_enhance_index_for_unlock(parsed.unlockLevel);
                if (unlockIndex >= 0)
                    priorWashCount =
                        equipment->enhanceWash.slots[unlockIndex].washCount;
                /* Mix per-attempt entropy so 2nd+ washes are not tick-stuck. */
                vm_net_mock_enhance_wash_rand_mix(
                    g_schedulerTick ^ ((u32)parsed.equipSeq << 16) ^
                    ((u32)parsed.unlockLevel << 8) ^
                    ((u32)priorWashCount * 0x85ebca6bu) ^
                    ((u32)parsed.crystalItemId * 0xc2b2ae35u) ^
                    ((u32)needCount * 0x27d4eb2du));
                roll = vm_net_mock_enhance_wash_rand_below(100u);
                role->money -= moneyCost;
                washSucceeded = roll < successRate;
                result = washSucceeded ? 1 : 2;
                equipment = vm_net_mock_role_find_backpack_item(
                    role, 0, parsed.equipSeq);
                if (equipment == NULL)
                {
                    result = 3;
                    reason = "equipment-missing-after-consume";
                }
                else
                {
                    unlockIndex =
                        vm_net_mock_enhance_index_for_unlock(parsed.unlockLevel);
                    /* New roll replaces any undecided pending (cost already paid). */
                    vm_net_mock_enhance_wash_pending_clear(
                        &role->enhanceWashPending);
                    if (unlockIndex >= 0)
                    {
                        oldWashed =
                            equipment->enhanceWash.slots[unlockIndex].washed
                                ? 1u
                                : 0u;
                        (void)vm_net_mock_equip_enhance_resolve_milestone(
                            catalog, (u8)unlockIndex, &equipment->enhanceWash,
                            &oldType, &oldFlag, &oldValue);
                    }
                    if (washSucceeded && unlockIndex >= 0)
                    {
                        if (!vm_net_mock_enhance_wash_roll_attr(
                                catalog != NULL ? catalog->quality : 0,
                                &rolledType, &rolledFlag, &rolledValue))
                        {
                            result = 5;
                            reason = "pool-empty";
                            washSucceeded = false;
                        }
                        else
                        {
                            vm_net_mock_enhance_wash_slot *slot =
                                &equipment->enhanceWash.slots[unlockIndex];
                            /* Consume attempt: count++ now; attrs wait for 29/14. */
                            if (slot->washCount < 0xffffu)
                                slot->washCount += 1;
                            role->enhanceWashPending.pending = 1;
                            role->enhanceWashPending.equipSeq = parsed.equipSeq;
                            role->enhanceWashPending.unlockLevel =
                                parsed.unlockLevel;
                            role->enhanceWashPending.newType = rolledType;
                            role->enhanceWashPending.newFlag = rolledFlag;
                            role->enhanceWashPending.newValue = rolledValue;
                            reason = "wash-ok-pending-replace";
                        }
                    }
                    else if (!washSucceeded)
                    {
                        /* Fail still increments wash_count for gold ladder. */
                        if (unlockIndex >= 0)
                        {
                            vm_net_mock_enhance_wash_slot *slot =
                                &equipment->enhanceWash.slots[unlockIndex];
                            if (slot->washCount < 0xffffu)
                                slot->washCount += 1;
                        }
                        reason = "wash-failed";
                    }
                    /* wash_count / gold spend must land even if replace is pending. */
                    if (!vm_net_mock_role_db_save("equipment-enhance-wash"))
                        vm_net_mock_role_mark_inventory_dirty("equipment-enhance-wash");
                }
            }
        }
    }

    /* Build tier snapshot for open / confirm. */
    if (equipment != NULL && catalog != NULL)
    {
        u8 i;
        for (i = 0; i < VM_NET_MOCK_ENHANCE_WASH_SLOT_COUNT; ++i)
        {
            u8 unlock = vm_net_mock_enhance_unlock_for_index(i);
            u8 type = 0;
            u8 flag = 0;
            u16 value = 0;
            u16 washCount = equipment->enhanceWash.slots[i].washCount;
            u8 washed = equipment->enhanceWash.slots[i].washed ? 1u : 0u;
            u32 nextGold = vm_net_mock_enhance_wash_next_gold(washCount);

            (void)vm_net_mock_equip_enhance_resolve_milestone(
                catalog, i, &equipment->enhanceWash, &type, &flag, &value);
            if (tiersLen + 12u > sizeof(tiersBlob))
                break;
            tiersBlob[tiersLen++] = unlock;
            tiersBlob[tiersLen++] = washed;
            tiersBlob[tiersLen++] = type;
            tiersBlob[tiersLen++] = flag;
            tiersBlob[tiersLen++] = (u8)((value >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(value & 0xff);
            tiersBlob[tiersLen++] = (u8)((washCount >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(washCount & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 24) & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 16) & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(nextGold & 0xff);
        }
        {
            u32 i;
            u32 poolCount = vm_net_mock_enhance_attr_pool_admin_count();
            u8 poolQuality = catalog != NULL ? catalog->quality : 0;
            for (i = 0; i < poolCount; ++i)
            {
                const vm_net_mock_enhance_attr_pool_cfg *row =
                    vm_net_mock_enhance_attr_pool_admin_row_at(i);
                u32 scaledMax;
                u32 wireMin;
                if (row == NULL || !row->enabled)
                    continue;
                if (poolLen + 6u > sizeof(poolBlob))
                    break;
                scaledMax = vm_net_mock_enhance_wash_pool_scaled_max(
                    row->valueMax, poolQuality);
                wireMin = row->valueMin;
                if (wireMin > scaledMax)
                    wireMin = scaledMax;
                poolBlob[poolLen++] = row->attrType;
                poolBlob[poolLen++] = row->flag;
                poolBlob[poolLen++] = (u8)((wireMin >> 8) & 0xff);
                poolBlob[poolLen++] = (u8)(wireMin & 0xff);
                poolBlob[poolLen++] = (u8)((scaledMax >> 8) & 0xff);
                poolBlob[poolLen++] = (u8)(scaledMax & 0xff);
            }
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29,
                                     parsed.subtype, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }

    if (parsed.subtype == 7)
    {
        if (result == 1 &&
            (!vm_net_mock_put_object_u16(out, outCap, &pos, "seq",
                                         parsed.equipSeq) ||
             !vm_net_mock_put_object_u8(out, outCap, &pos, "curlevel",
                                        currentLevel) ||
             !vm_net_mock_put_object_raw(out, outCap, &pos, "tiers", tiersBlob,
                                         (u16)tiersLen) ||
             !vm_net_mock_put_object_raw(out, outCap, &pos, "pool", poolBlob,
                                         (u16)poolLen)))
        {
            return 0;
        }
    }
    else
    {
        if (!vm_net_mock_put_object_u16(out, outCap, &pos, "equipseq",
                                         parsed.equipSeq) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "unlock",
                                       parsed.unlockLevel) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "rate",
                                        successRate) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "money", moneyCost))
        {
            return 0;
        }
        if (parsed.subtype == 9 && result == 1 && washSucceeded)
        {
            if (!vm_net_mock_put_object_u8(out, outCap, &pos, "attrtype",
                                           rolledType) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "attrflag",
                                           rolledFlag) ||
                !vm_net_mock_put_object_u16(out, outCap, &pos, "attrvalue",
                                           rolledValue) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "oldwashed",
                                           oldWashed) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "oldtype",
                                           oldType) ||
                !vm_net_mock_put_object_u8(out, outCap, &pos, "oldflag",
                                           oldFlag) ||
                !vm_net_mock_put_object_u16(out, outCap, &pos, "oldvalue",
                                           oldValue) ||
                !vm_net_mock_put_object_raw(out, outCap, &pos, "tiers",
                                            tiersBlob, (u16)tiersLen))
            {
                return 0;
            }
        }
        else if (parsed.subtype == 9 && (result == 1 || result == 2) &&
                 tiersLen > 0)
        {
            (void)vm_net_mock_put_object_raw(out, outCap, &pos, "tiers",
                                              tiersBlob, (u16)tiersLen);
        }
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    if (parsed.subtype == 9 && role != NULL && equipment != NULL &&
        (result == 1 || result == 2))
    {
        bool listResynced = false;
        u16 crystalSeqs[5];
        u32 crystalIds[5];
        u32 crystalRemainings[5];
        u8 crystalRows = 0;
        vm_net_mock_backpack_item_state *crystalLeft =
            vm_net_mock_role_find_backpack_item(role, parsed.crystalItemId, 0);

        memset(crystalSeqs, 0, sizeof(crystalSeqs));
        memset(crystalIds, 0, sizeof(crystalIds));
        memset(crystalRemainings, 0, sizeof(crystalRemainings));
        if (crystalLeft != NULL)
        {
            crystalSeqs[0] = crystalLeft->seq;
            crystalIds[0] = crystalLeft->itemId;
            crystalRemainings[0] = crystalLeft->count;
            crystalRows = 1;
        }
        else
        {
            crystalIds[0] = parsed.crystalItemId;
            crystalRows = 1;
        }
        inventorySynced = vm_net_mock_append_equipment_enhance_inventory_sync(
            out, outCap, &pos, &objectCount, crystalIds, crystalSeqs,
            crystalRemainings, crystalRows, parsed.equipSeq, equipment->itemId,
            (u8)SDL_min(equipment->enhanceLevel,
                        VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
            true, true, false, role->money, &listResynced);
        if (inventorySynced)
        {
            vm_net_mock_arm_enhance_backpack_list_resync(
                parsed.equipSeq, equipment->itemId,
                (u8)SDL_min(equipment->enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                listResynced);
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_equipment_enhance_wash phase=%u seq=%u "
           "item=%u level=%u unlock=%u crystal=%u count=%u rate=%u money=%u "
           "result=%u success=%u type=%u flag=%u value=%u inv_sync=%u "
           "reason=%s resp=29/%u\n",
           parsed.subtype, parsed.equipSeq,
           equipment ? equipment->itemId : 0, currentLevel, parsed.unlockLevel,
           parsed.crystalItemId, parsed.crystalCount, successRate, moneyCost,
           result, washSucceeded ? 1 : 0, rolledType, rolledFlag, rolledValue,
           inventorySynced ? 1 : 0, reason, parsed.subtype);
    return pos;
}

/*
 * PixiJS-only 洗练属性替换确认: 1/29/14 { equipseq, unlock, apply }.
 * apply=1 → write pending rolled attrs; apply=0 → keep current attrs.
 * Cost and wash_count already applied on the preceding 29/9 success.
 * result: 1=ok, 2=no pending, 3=equip missing, 5=mismatch/bad unlock.
 */
static u32 vm_net_mock_build_equipment_enhance_wash_apply_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    u32 offset = 4;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    u8 result = 1;
    u32 equipSeqValue = 0;
    u32 unlockValue = 0;
    u32 applyValue = 0;
    u16 equipSeq = 0;
    u8 unlockLevel = 0;
    u8 apply = 0;
    int unlockIndex = -1;
    const char *reason = "ok";
    u8 tiersBlob[4 * 12];
    u32 tiersLen = 0;
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *equipment = NULL;
    const vm_net_mock_equipment_catalog_item *catalog = NULL;

    memset(tiersBlob, 0, sizeof(tiersBlob));
    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                          &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 14 || object.payloadLen == 0)
    {
        return 0;
    }

    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "equipseq", &equipSeqValue) &&
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "seq", &equipSeqValue))
    {
        return 0;
    }
    if (equipSeqValue == 0 || equipSeqValue > 0xffffu)
        return 0;
    equipSeq = (u16)equipSeqValue;
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "unlock", &unlockValue) ||
        (unlockValue != 4 && unlockValue != 8 && unlockValue != 12 &&
         unlockValue != 16))
    {
        return 0;
    }
    unlockLevel = (u8)unlockValue;
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "apply", &applyValue) ||
        applyValue > 1u)
    {
        return 0;
    }
    apply = (u8)applyValue;

    role = vm_net_mock_active_role();
    if (role == NULL)
    {
        result = 3;
        reason = "no-role";
    }
    else if (!role->enhanceWashPending.pending)
    {
        result = 2;
        reason = "no-pending";
    }
    else if (role->enhanceWashPending.equipSeq != equipSeq ||
             role->enhanceWashPending.unlockLevel != unlockLevel)
    {
        result = 5;
        reason = "pending-mismatch";
    }
    else
    {
        equipment = vm_net_mock_role_find_backpack_item(role, 0, equipSeq);
        if (equipment != NULL)
            catalog = vm_net_mock_find_equipment_catalog_item(equipment->itemId);
        unlockIndex = vm_net_mock_enhance_index_for_unlock(unlockLevel);
        if (equipment == NULL || catalog == NULL || unlockIndex < 0)
        {
            result = 3;
            reason = "equipment-not-found";
            vm_net_mock_enhance_wash_pending_clear(&role->enhanceWashPending);
        }
        else if (apply)
        {
            vm_net_mock_enhance_wash_slot *slot =
                &equipment->enhanceWash.slots[unlockIndex];
            slot->washed = 1;
            slot->type = role->enhanceWashPending.newType;
            slot->flag = role->enhanceWashPending.newFlag;
            slot->value = role->enhanceWashPending.newValue;
            {
                u32 washInstanceId =
                    vm_net_mock_role_ensure_equipment_instance_id(
                        role, equipment->enhanceInstanceId, equipment->itemId);
                equipment->enhanceInstanceId = washInstanceId;
                (void)vm_net_mock_enhance_wash_record_upsert(
                    role->roleId, washInstanceId, equipment->seq,
                    equipment->itemId, equipment->enhanceLevel,
                    &equipment->enhanceWash, unlockLevel);
            }
            vm_net_mock_enhance_wash_pending_clear(&role->enhanceWashPending);
            /* Sync save: wash attrs must survive kill+relogin before deferred flush. */
            if (!vm_net_mock_role_db_save("equipment-enhance-wash-apply"))
                vm_net_mock_role_mark_inventory_dirty("equipment-enhance-wash-apply");
            reason = "applied";
        }
        else
        {
            vm_net_mock_enhance_wash_pending_clear(&role->enhanceWashPending);
            reason = "kept-current";
        }
    }

    if (equipment != NULL && catalog != NULL)
    {
        u8 i;
        for (i = 0; i < VM_NET_MOCK_ENHANCE_WASH_SLOT_COUNT; ++i)
        {
            u8 unlock = vm_net_mock_enhance_unlock_for_index(i);
            u8 type = 0;
            u8 flag = 0;
            u16 value = 0;
            u16 washCount = equipment->enhanceWash.slots[i].washCount;
            u8 washed = equipment->enhanceWash.slots[i].washed ? 1u : 0u;
            u32 nextGold = vm_net_mock_enhance_wash_next_gold(washCount);

            (void)vm_net_mock_equip_enhance_resolve_milestone(
                catalog, i, &equipment->enhanceWash, &type, &flag, &value);
            if (tiersLen + 12u > sizeof(tiersBlob))
                break;
            tiersBlob[tiersLen++] = unlock;
            tiersBlob[tiersLen++] = washed;
            tiersBlob[tiersLen++] = type;
            tiersBlob[tiersLen++] = flag;
            tiersBlob[tiersLen++] = (u8)((value >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(value & 0xff);
            tiersBlob[tiersLen++] = (u8)((washCount >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(washCount & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 24) & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 16) & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(nextGold & 0xff);
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 14,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "equipseq", equipSeq) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "unlock", unlockLevel) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "apply", apply))
    {
        return 0;
    }
    if (result == 1 && tiersLen > 0)
    {
        if (!vm_net_mock_put_object_raw(out, outCap, &pos, "tiers", tiersBlob,
                                         (u16)tiersLen))
            return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    if (result == 1 && apply && role != NULL && equipment != NULL)
    {
        bool listResynced = false;
        u16 crystalSeqs[1];
        u32 crystalIds[1];
        u32 crystalRemainings[1];

        memset(crystalSeqs, 0, sizeof(crystalSeqs));
        memset(crystalIds, 0, sizeof(crystalIds));
        memset(crystalRemainings, 0, sizeof(crystalRemainings));
        (void)vm_net_mock_append_equipment_enhance_inventory_sync(
            out, outCap, &pos, &objectCount, crystalIds, crystalSeqs,
            crystalRemainings, 0, equipSeq, equipment->itemId,
            (u8)SDL_min(equipment->enhanceLevel,
                        VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
            true, false, false, role->money, &listResynced);
        if (listResynced)
        {
            vm_net_mock_arm_enhance_backpack_list_resync(
                equipSeq, equipment->itemId,
                (u8)SDL_min(equipment->enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                true);
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_equipment_enhance_wash_apply seq=%u unlock=%u "
           "apply=%u result=%u reason=%s resp=29/14\n",
           equipSeq, unlockLevel, apply, result, reason);
    return pos;
}

/*
 * PixiJS-only 玄晶合成: 1/29/10 { seq }.
 * Consume 3 of crystal tier N (901..915) → grant 1 of N+1.
 * If dest tier already in bag, stack +1; else need a free slot (consuming a
 * full stack of exactly 3 frees one slot before grant).
 * result: 1=ok, 2=bad item, 3=bag full, 4=count<3, 5=max tier(16).
 */
static u32 vm_net_mock_build_crystal_combine_response(const u8 *request,
                                                      u32 requestLen, u8 *out,
                                                      u32 outCap)
{
    u32 offset = 4;
    u32 seqValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u32 srcItemId = 0;
    u32 destItemId = 0;
    u16 srcSeq = 0;
    u16 destSeq = 0;
    u32 srcRemaining = 0;
    u32 destCount = 0;
    const char *reason = "ok";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *src = NULL;
    vm_net_mock_backpack_item_state *dest = NULL;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                          &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 10 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "seq", &seqValue) ||
        seqValue == 0 || seqValue > 0xffffu)
    {
        return 0;
    }
    srcSeq = (u16)seqValue;

    role = vm_net_mock_active_role();
    if (role == NULL)
    {
        result = 2;
        reason = "no-role";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        src = vm_net_mock_role_find_backpack_item(role, 0, srcSeq);
        if (src == NULL || src->count == 0)
        {
            result = 2;
            reason = "crystal-not-found";
        }
        else
        {
            srcItemId = src->itemId;
            if (srcItemId < VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_FIRST ||
                srcItemId > VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_LAST)
            {
                result = 2;
                reason = "not-crystal";
            }
            else if (srcItemId >= VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_LAST)
            {
                result = 5;
                reason = "max-tier";
            }
            else if (src->count < 3)
            {
                result = 4;
                reason = "count-lt-3";
            }
            else
            {
                u32 itemCount = vm_net_mock_role_backpack_count(role);
                u32 freeSlots = role->backpackCapacity > itemCount
                                    ? (u32)role->backpackCapacity - itemCount
                                    : 0;
                bool freesSlot = (src->count == 3);
                destItemId = srcItemId + 1;
                dest = vm_net_mock_role_find_backpack_item(role, destItemId, 0);
                if (dest == NULL && freeSlots == 0 && !freesSlot)
                {
                    result = 3;
                    reason = "bag-full";
                }
            }
        }
    }

    if (result == 1 && role != NULL && src != NULL)
    {
        if (!vm_net_mock_role_consume_backpack_item(role, srcItemId, srcSeq, 3,
                                                     &srcRemaining))
        {
            result = 4;
            reason = "consume-failed";
        }
        else if (!vm_net_mock_role_add_backpack_item_to_role(
                     role, destItemId, 1, 0, &destSeq, "crystal-combine", NULL, 0))
        {
            /* Should be rare after pre-check; restore 3 source crystals. */
            (void)vm_net_mock_role_add_backpack_item_to_role(
                role, srcItemId, 3, 0, NULL, "crystal-combine-rollback", NULL, 0);
            result = 3;
            reason = "grant-failed";
            srcRemaining = srcRemaining + 3;
        }
        else
        {
            dest = vm_net_mock_role_find_backpack_item(role, destItemId, destSeq);
            destCount = dest ? dest->count : 1;
            reason = "combine-ok";
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 10,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", srcSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", srcItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destid", destItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining",
                                    srcRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destcount", destCount))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_crystal_combine seq=%u src=%u dest=%u "
           "src_remain=%u dest_count=%u result=%u reason=%s resp=29/10\n",
           srcSeq, srcItemId, destItemId, srcRemaining, destCount, result,
           reason);
    vm_autotest_note(
        "mock_crystal_combine seq=%u src=%u dest=%u result=%u reason=%s "
        "evidence=pixijs-only-29/10\n",
        srcSeq, srcItemId, destItemId, result, reason);
    return pos;
}

/*
 * PixiJS-only 玄晶碎片合成: 1/29/11 { seq }.
 * Consume 10× item 900 → grant 1× 一级玄晶(901).
 * If 901 already in bag, stack +1; else need a free slot (consuming a full
 * stack of exactly 10 frees one slot before grant).
 * result: 1=ok, 2=bad item, 3=bag full, 4=count<10.
 */
static u32 vm_net_mock_build_crystal_fragment_combine_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 seqValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u32 srcItemId = 0;
    u32 destItemId = (u32)VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_FIRST;
    u16 srcSeq = 0;
    u16 destSeq = 0;
    u32 srcRemaining = 0;
    u32 destCount = 0;
    const u32 cost = (u32)VM_NET_MOCK_CRYSTAL_FRAGMENT_COMBINE_COST;
    const char *reason = "ok";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *src = NULL;
    vm_net_mock_backpack_item_state *dest = NULL;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                          &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 11 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "seq", &seqValue) ||
        seqValue == 0 || seqValue > 0xffffu)
    {
        return 0;
    }
    srcSeq = (u16)seqValue;

    role = vm_net_mock_active_role();
    if (role == NULL)
    {
        result = 2;
        reason = "no-role";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        src = vm_net_mock_role_find_backpack_item(role, 0, srcSeq);
        if (src == NULL || src->count == 0)
        {
            result = 2;
            reason = "fragment-not-found";
        }
        else
        {
            srcItemId = src->itemId;
            if (srcItemId != (u32)VM_NET_MOCK_CRYSTAL_FRAGMENT_ITEM_ID)
            {
                result = 2;
                reason = "not-fragment";
            }
            else if (src->count < cost)
            {
                result = 4;
                reason = "count-lt-10";
            }
            else
            {
                u32 itemCount = vm_net_mock_role_backpack_count(role);
                u32 freeSlots = role->backpackCapacity > itemCount
                                    ? (u32)role->backpackCapacity - itemCount
                                    : 0;
                bool freesSlot = (src->count == cost);
                dest = vm_net_mock_role_find_backpack_item(role, destItemId, 0);
                if (dest == NULL && freeSlots == 0 && !freesSlot)
                {
                    result = 3;
                    reason = "bag-full";
                }
            }
        }
    }

    if (result == 1 && role != NULL && src != NULL)
    {
        if (!vm_net_mock_role_consume_backpack_item(role, srcItemId, srcSeq,
                                                     cost, &srcRemaining))
        {
            result = 4;
            reason = "consume-failed";
        }
        else if (!vm_net_mock_role_add_backpack_item_to_role(
                     role, destItemId, 1, 0, &destSeq,
                     "crystal-fragment-combine", NULL, 0))
        {
            (void)vm_net_mock_role_add_backpack_item_to_role(
                role, srcItemId, cost, 0, NULL,
                "crystal-fragment-combine-rollback", NULL, 0);
            result = 3;
            reason = "grant-failed";
            srcRemaining = srcRemaining + cost;
        }
        else
        {
            dest = vm_net_mock_role_find_backpack_item(role, destItemId, destSeq);
            destCount = dest ? dest->count : 1;
            reason = "fragment-combine-ok";
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 11,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", srcSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", srcItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destid", destItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining",
                                    srcRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destcount", destCount))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_crystal_fragment_combine seq=%u src=%u "
           "dest=%u src_remain=%u dest_count=%u result=%u reason=%s "
           "resp=29/11\n",
           srcSeq, srcItemId, destItemId, srcRemaining, destCount, result,
           reason);
    vm_autotest_note(
        "mock_crystal_fragment_combine seq=%u src=%u dest=%u result=%u "
        "reason=%s evidence=pixijs-only-29/11\n",
        srcSeq, srcItemId, destItemId, result, reason);
    return pos;
}

/*
 * PixiJS-only 梦境徽章合成: 1/29/12 { seq, destid }.
 * Consume 300× item 840 → grant 1× player-chosen dream equip (40065..40088).
 * Equipment never stacks — need a free bag slot (exact-300 consume frees one).
 * result: 1=ok, 2=bad item, 3=bag full, 4=count<300, 5=unknown dest,
 *         6=dest not in pool / missing destid.
 */
static bool vm_net_mock_dream_badge_equip_allowed(u32 itemId)
{
    static const u32 kDreamEquips[] = {
        40066u, 40067u, 40068u, 40069u, 40070u, 40071u, 40072u, 40073u,
        40074u, 40075u, 40076u, 40077u, 40078u, 40079u, 40080u, 40081u,
        40082u, 40083u, 40084u, 40085u, 40086u, 40087u, 40088u, 40065u,
    };
    const u32 n = (u32)(sizeof(kDreamEquips) / sizeof(kDreamEquips[0]));
    u32 i = 0;

    for (i = 0; i < n; ++i)
    {
        if (kDreamEquips[i] == itemId)
            return true;
    }
    return false;
}

static u32 vm_net_mock_build_dream_badge_combine_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 seqValue = 0;
    u32 destReq = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u32 srcItemId = 0;
    u32 destItemId = 0;
    u16 srcSeq = 0;
    u16 destSeq = 0;
    u32 srcRemaining = 0;
    u32 destCount = 0;
    const u32 cost = (u32)VM_NET_MOCK_DREAM_BADGE_COMBINE_COST;
    const char *reason = "ok";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *src = NULL;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                          &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 12 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "seq", &seqValue) ||
        seqValue == 0 || seqValue > 0xffffu)
    {
        return 0;
    }
    srcSeq = (u16)seqValue;
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "destid", &destReq) ||
        destReq == 0)
    {
        /* Still ACK so the client can show a tip; do not consume badges. */
        result = 6;
        reason = "missing-destid";
    }
    else if (!vm_net_mock_dream_badge_equip_allowed(destReq))
    {
        result = 6;
        reason = "dest-not-in-pool";
        destItemId = destReq;
    }
    else
        destItemId = destReq;

    role = vm_net_mock_active_role();
    if (result == 1 && role == NULL)
    {
        result = 2;
        reason = "no-role";
    }
    else if (result == 1)
    {
        vm_net_mock_role_normalize_backpack(role);
        src = vm_net_mock_role_find_backpack_item(role, 0, srcSeq);
        if (src == NULL || src->count == 0)
        {
            result = 2;
            reason = "badge-not-found";
        }
        else
        {
            srcItemId = src->itemId;
            if (srcItemId != (u32)VM_NET_MOCK_DREAM_BADGE_ITEM_ID)
            {
                result = 2;
                reason = "not-dream-badge";
            }
            else if (src->count < cost)
            {
                result = 4;
                reason = "count-lt-300";
            }
            else
            {
                u32 itemCount = vm_net_mock_role_backpack_count(role);
                u32 freeSlots = role->backpackCapacity > itemCount
                                    ? (u32)role->backpackCapacity - itemCount
                                    : 0;
                bool freesSlot = (src->count == cost);
                if (vm_net_mock_find_equipment_catalog_item(destItemId) == NULL)
                {
                    result = 5;
                    reason = "dest-not-in-equip-dsh";
                }
                else if (freeSlots == 0 && !freesSlot)
                {
                    result = 3;
                    reason = "bag-full";
                }
            }
        }
    }

    if (result == 1 && role != NULL && src != NULL && destItemId != 0)
    {
        if (!vm_net_mock_role_consume_backpack_item(role, srcItemId, srcSeq,
                                                     cost, &srcRemaining))
        {
            result = 4;
            reason = "consume-failed";
        }
        else if (!vm_net_mock_role_add_backpack_item_to_role(
                     role, destItemId, 1, 0, &destSeq,
                     "dream-badge-combine", NULL, 0))
        {
            (void)vm_net_mock_role_add_backpack_item_to_role(
                role, srcItemId, cost, 0, NULL,
                "dream-badge-combine-rollback", NULL, 0);
            result = 3;
            reason = "grant-failed";
            srcRemaining = srcRemaining + cost;
        }
        else
        {
            destCount = 1;
            reason = "dream-badge-combine-ok";
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 12,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", srcSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", srcItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destid", destItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining",
                                    srcRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destcount", destCount))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_dream_badge_combine seq=%u src=%u "
           "dest=%u src_remain=%u dest_count=%u result=%u reason=%s "
           "resp=29/12\n",
           srcSeq, srcItemId, destItemId, srcRemaining, destCount, result,
           reason);
    vm_autotest_note(
        "mock_dream_badge_combine seq=%u src=%u dest=%u result=%u "
        "reason=%s evidence=pixijs-only-29/12-player-pick\n",
        srcSeq, srcItemId, destItemId, result, reason);
    return pos;
}

/*
 * PixiJS-only 星空石重置洗练次数: 1/29/13 { stoneseq, equipseq, unlock }.
 * Consume 1× item 849 → set enhance_wash.slots[tier].washCount = 0.
 * Does not clear washed attrs. Tier must be unlocked (enhance >= unlock).
 * result: 1=ok, 2=stone missing/wrong, 3=equip missing, 4=tier locked,
 *         5=bad unlock, 6=consume failed, 7=count already 0.
 */
static u32 vm_net_mock_build_star_sky_stone_wash_reset_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 stoneSeqValue = 0;
    u32 equipSeqValue = 0;
    u32 unlockValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 stoneSeq = 0;
    u16 equipSeq = 0;
    u8 unlockLevel = 0;
    int unlockIndex = -1;
    u32 stoneRemaining = 0;
    u16 washCount = 0;
    const char *reason = "ok";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *stone = NULL;
    vm_net_mock_backpack_item_state *equipment = NULL;
    const vm_net_mock_equipment_catalog_item *catalog = NULL;
    u8 tiersBlob[4 * 12];
    u32 tiersLen = 0;

    memset(tiersBlob, 0, sizeof(tiersBlob));
    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                          &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 13 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "stoneseq", &stoneSeqValue) ||
        stoneSeqValue == 0 || stoneSeqValue > 0xffffu)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "equipseq", &equipSeqValue) ||
        equipSeqValue == 0 || equipSeqValue > 0xffffu)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "unlock", &unlockValue) ||
        (unlockValue != 4 && unlockValue != 8 && unlockValue != 12 &&
         unlockValue != 16))
    {
        /* Still ACK with result so client can tip. */
        result = 5;
        reason = "bad-unlock";
        unlockLevel = (u8)(unlockValue & 0xffu);
    }
    else
        unlockLevel = (u8)unlockValue;

    stoneSeq = (u16)stoneSeqValue;
    equipSeq = (u16)equipSeqValue;
    unlockIndex = vm_net_mock_enhance_index_for_unlock(unlockLevel);

    role = vm_net_mock_active_role();
    if (result == 1 && role == NULL)
    {
        result = 2;
        reason = "no-role";
    }
    else if (result == 1)
    {
        vm_net_mock_role_normalize_backpack(role);
        stone = vm_net_mock_role_find_backpack_item(role, 0, stoneSeq);
        equipment = vm_net_mock_role_find_backpack_item(role, 0, equipSeq);
        if (stone == NULL || stone->count == 0 ||
            stone->itemId != VM_NET_MOCK_STAR_SKY_STONE_ITEM_ID)
        {
            result = 2;
            reason = "stone-not-found";
        }
        else if (equipment == NULL)
        {
            result = 3;
            reason = "equipment-not-found";
        }
        else
        {
            catalog = vm_net_mock_find_equipment_catalog_item(equipment->itemId);
            if (catalog == NULL)
            {
                result = 3;
                reason = "not-equipment";
            }
            else if (unlockIndex < 0)
            {
                result = 5;
                reason = "bad-unlock-index";
            }
            else if (equipment->enhanceLevel < unlockLevel)
            {
                result = 4;
                reason = "tier-locked";
            }
            else
            {
                washCount = equipment->enhanceWash.slots[unlockIndex].washCount;
                if (washCount == 0)
                {
                    result = 7;
                    reason = "count-already-zero";
                }
            }
        }
    }

    if (result == 1 && role != NULL && stone != NULL && equipment != NULL &&
        unlockIndex >= 0)
    {
        if (!vm_net_mock_role_consume_backpack_item(
                role, VM_NET_MOCK_STAR_SKY_STONE_ITEM_ID, stoneSeq, 1u,
                &stoneRemaining))
        {
            result = 6;
            reason = "consume-failed";
        }
        else
        {
            equipment = vm_net_mock_role_find_backpack_item(role, 0, equipSeq);
            if (equipment == NULL)
            {
                (void)vm_net_mock_role_add_backpack_item_to_role(
                    role, VM_NET_MOCK_STAR_SKY_STONE_ITEM_ID, 1, 0, NULL,
                    "star-sky-stone-reset-rollback", NULL, 0);
                result = 3;
                reason = "equipment-missing-after-consume";
                stoneRemaining += 1u;
            }
            else
            {
                equipment->enhanceWash.slots[unlockIndex].washCount = 0;
                washCount = 0;
                reason = "wash-count-reset-ok";
                if (role->enhanceWashPending.pending &&
                    role->enhanceWashPending.equipSeq == equipSeq &&
                    role->enhanceWashPending.unlockLevel == unlockLevel)
                {
                    vm_net_mock_enhance_wash_pending_clear(
                        &role->enhanceWashPending);
                }
                vm_net_mock_role_mark_inventory_dirty(
                    "star-sky-stone-wash-reset");
            }
        }
    }

    if (equipment != NULL && catalog != NULL)
    {
        u8 i;
        for (i = 0; i < VM_NET_MOCK_ENHANCE_WASH_SLOT_COUNT; ++i)
        {
            u8 unlock = vm_net_mock_enhance_unlock_for_index(i);
            u8 type = 0;
            u8 flag = 0;
            u16 value = 0;
            u16 slotWash = equipment->enhanceWash.slots[i].washCount;
            u8 washed = equipment->enhanceWash.slots[i].washed ? 1u : 0u;
            u32 nextGold = vm_net_mock_enhance_wash_next_gold(slotWash);

            (void)vm_net_mock_equip_enhance_resolve_milestone(
                catalog, i, &equipment->enhanceWash, &type, &flag, &value);
            if (tiersLen + 12u > sizeof(tiersBlob))
                break;
            tiersBlob[tiersLen++] = unlock;
            tiersBlob[tiersLen++] = washed;
            tiersBlob[tiersLen++] = type;
            tiersBlob[tiersLen++] = flag;
            tiersBlob[tiersLen++] = (u8)((value >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(value & 0xff);
            tiersBlob[tiersLen++] = (u8)((slotWash >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(slotWash & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 24) & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 16) & 0xff);
            tiersBlob[tiersLen++] = (u8)((nextGold >> 8) & 0xff);
            tiersBlob[tiersLen++] = (u8)(nextGold & 0xff);
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 13,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "stoneseq", stoneSeq) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "equipseq", equipSeq) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "unlock", unlockLevel) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining",
                                    stoneRemaining) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "washcount", washCount))
    {
        return 0;
    }
    if (tiersLen > 0 &&
        !vm_net_mock_put_object_raw(out, outCap, &pos, "tiers", tiersBlob,
                                    (u16)tiersLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_star_sky_stone_wash_reset stone_seq=%u "
           "equip_seq=%u unlock=%u washcount=%u remain=%u result=%u reason=%s "
           "resp=29/13\n",
           stoneSeq, equipSeq, (u32)unlockLevel, (u32)washCount, stoneRemaining,
           result, reason);
    vm_autotest_note(
        "mock_star_sky_stone_wash_reset stone=%u equip=%u unlock=%u "
        "result=%u reason=%s evidence=pixijs-only-29/13\n",
        stoneSeq, equipSeq, (u32)unlockLevel, result, reason);
    return pos;
}

static u32 vm_net_mock_battle_reward_rand(void)
{
    if (g_vm_net_mock_battle_reward_rng == 0)
    {
        g_vm_net_mock_battle_reward_rng =
            0x6d2b79f5u ^
            (g_schedulerTick * 1664525u) ^
            (g_mockBattleOperateSessionSerial * 1013904223u) ^
            (g_vm_net_mock_role_db.activeRoleId << 1);
        if (g_vm_net_mock_battle_reward_rng == 0)
            g_vm_net_mock_battle_reward_rng = 0x9e3779b9u;
    }

    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng << 13;
    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng >> 17;
    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng << 5;
    return g_vm_net_mock_battle_reward_rng;
}

static u8 vm_net_mock_battle_roll_enemy_count(bool useSceneMonsterStart)
{
    u32 minCount = 1;
    u32 maxCount = 3;
    const char *forcedSpec = getenv("CBE_BATTLE_ENEMY_COUNT");
    const vm_net_mock_role_state *role;

    if (forcedSpec != NULL && forcedSpec[0] != 0)
    {
        u32 forced = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_COUNT", 1);
        if (forced < 1)
            forced = 1;
        if (forced > 3)
            forced = 3;
        return (u8)forced;
    }

    /*
     * Battle insight (item 828): while active, hangup/challenge encounters are
     * fixed at 3 — including non-scene subtype-10 hangup (Pixi/APK has no
     * emulator live-node, so most hangup starts take that path).  Env force
     * above still wins for autotest.  Team starts re-check the leader role in
     * challenge/hangup after the party gate (see battle.c).
     */
    role = vm_net_mock_active_role();
    if (role != NULL &&
        vm_net_mock_role_active_battle_exp_bonus_percent(role) != 0)
    {
        printf("[info][network] mock_battle_enemy_count_fixed role=%u count=3 "
               "reason=battle-insight%s evidence=item.dsh:828\n",
               role->roleId,
               useSceneMonsterStart ? "" : "+subtype10");
        return 3;
    }

    if (!useSceneMonsterStart)
        return 1;

    minCount = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_COUNT_MIN", minCount);
    maxCount = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_COUNT_MAX", maxCount);
    if (minCount < 1)
        minCount = 1;
    if (maxCount < 1)
        maxCount = 1;
    if (minCount > 3)
        minCount = 3;
    if (maxCount > 3)
        maxCount = 3;
    if (minCount > maxCount)
    {
        u32 tmp = minCount;
        minCount = maxCount;
        maxCount = tmp;
    }
    return (u8)(minCount + (vm_net_mock_battle_reward_rand() % (maxCount - minCount + 1)));
}

static u32 vm_net_mock_battle_enemy_id_for_index(u8 enemyIndex)
{
    if (enemyIndex < 3 && g_mockBattleEnemyIdSlots[enemyIndex] != 0)
        return g_mockBattleEnemyIdSlots[enemyIndex];
    return g_vm_net_mock_battle_enemy_id_current;
}

static void vm_net_mock_battle_reset_enemy_id_slots(u32 primaryEnemyId, u8 enemyCount)
{
    for (u8 i = 0; i < 3; ++i)
    {
        if (i < enemyCount && primaryEnemyId != 0)
            g_mockBattleEnemyIdSlots[i] = primaryEnemyId;
        else
            g_mockBattleEnemyIdSlots[i] = 0;
        g_mockBattleEnemyJobSlots[i] = 0xff;
    }
}

static void vm_net_mock_battle_set_enemy_id_slots(const u32 enemyIdsBySlot[3],
                                                  u8 enemyCount)
{
    u32 fallback = g_vm_net_mock_battle_enemy_id_current;

    for (u8 i = 0; i < 3; ++i)
    {
        if (i >= enemyCount)
            g_mockBattleEnemyIdSlots[i] = 0;
        else if (enemyIdsBySlot != NULL && enemyIdsBySlot[i] != 0)
            g_mockBattleEnemyIdSlots[i] = enemyIdsBySlot[i];
        else if (fallback != 0)
            g_mockBattleEnemyIdSlots[i] = fallback;
        else
            g_mockBattleEnemyIdSlots[i] = 0;
    }
}

static void vm_net_mock_battle_set_enemy_job_slots(const u8 jobsBySlot[3],
                                                   u8 enemyCount)
{
    for (u8 i = 0; i < 3; ++i)
    {
        if (i < enemyCount && jobsBySlot != NULL && jobsBySlot[i] <= 2)
            g_mockBattleEnemyJobSlots[i] = jobsBySlot[i];
        else
            g_mockBattleEnemyJobSlots[i] = 0xff;
    }
}

static u8 vm_net_mock_battle_enemy_job_for_index(u8 enemyIndex)
{
    if (enemyIndex < 3)
        return g_mockBattleEnemyJobSlots[enemyIndex];
    return 0xff;
}

static void vm_net_mock_battle_reset_enemy_hp_from_stats(u32 enemyId)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    u8 enemyCount = vm_net_mock_battle_enemy_count_current();
    u32 perEnemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
    u32 perEnemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", perEnemyHp);

    if (perEnemyMaxHp < perEnemyHp)
        perEnemyMaxHp = perEnemyHp;
    for (u8 i = 0; i < 3; ++i)
    {
        if (i < enemyCount)
        {
            g_mockBattleEnemyHpSlots[i] = perEnemyHp;
            g_mockBattleEnemyHpMaxSlots[i] = perEnemyMaxHp;
        }
        else
        {
            g_mockBattleEnemyHpSlots[i] = 0;
            g_mockBattleEnemyHpMaxSlots[i] = 0;
        }
    }
    vm_net_mock_battle_reset_enemy_id_slots(enemyId, enemyCount);
    vm_net_mock_battle_sync_enemy_hp_totals();
}

static void vm_net_mock_battle_reset_enemy_hp_from_slot_values(
    const u32 hpBySlot[3], u8 enemyCount)
{
    for (u8 i = 0; i < 3; ++i)
    {
        if (i < enemyCount)
        {
            u32 hp = hpBySlot[i] != 0 ? hpBySlot[i] : 1u;

            g_mockBattleEnemyHpSlots[i] = hp;
            g_mockBattleEnemyHpMaxSlots[i] = hp;
        }
        else
        {
            g_mockBattleEnemyHpSlots[i] = 0;
            g_mockBattleEnemyHpMaxSlots[i] = 0;
        }
    }
    vm_net_mock_battle_reset_enemy_id_slots(g_vm_net_mock_battle_enemy_id_current,
                                            enemyCount);
    vm_net_mock_battle_sync_enemy_hp_totals();
}

/*
 * Subtype-10 multi-monster solo PvE (ladder / insight triple).  Mirrors the
 * hangup battle-start server snapshot so the next 4/2 bundles monster counters.
 */
static void vm_net_mock_battle_arm_subtype10_multi_pve_start(
    u32 roleId, u32 roleHp, u32 roleMaxHp, u32 roleMp, u32 roleMaxMp,
    u32 primaryEnemyId, u8 enemyCount, const u32 enemyHpBySlot[3],
    const u32 enemyIdsBySlot[3])
{
    if (enemyCount < 1)
        enemyCount = 1;
    if (enemyCount > 3)
        enemyCount = 3;

    g_mockBattleOperateSessionArmed = 1;
    g_mockBattleOperateSessionFinished = 0;
    g_mockBattlePendingEnemyTurn = 0;
    g_mockBattleAwaitingSettlement = 0;
    g_mockBattleSettlementExitPending = 0;
    g_mockBattleSettlementExitNotBeforeMs = 0;
    g_mockBattlePostExitSettlePending = 0;
    g_mockBattlePostExitSettleNotBeforeMs = 0;
    g_mockBattleSceneMonsterStartActive = 0;
    g_mockBattleStartUsesSceneWireMaps = 0;
    g_mockBattleEnemyCountCurrent = enemyCount;
    g_mockBattleOperateTurnCounter = 0;
    memset(&g_vm_net_mock_battle_solo_modifier, 0,
           sizeof(g_vm_net_mock_battle_solo_modifier));
    memset(&g_vm_net_mock_battle_active_modifier_current, 0,
           sizeof(g_vm_net_mock_battle_active_modifier_current));
    memset(g_mockBattleEnemyAilments, 0, sizeof(g_mockBattleEnemyAilments));
    g_vm_net_mock_battle_formula_enemy_index = 0xff;
    ++g_mockBattleOperateSessionSerial;
    g_mockBattleLastIndex = 0;
    g_vm_net_mock_battle_rewarded_serial = 0;
    g_vm_net_mock_battle_rewarded_exp = 0;
    g_vm_net_mock_battle_rewarded_auto_sell_gold = 0;
    memset(g_vm_net_mock_battle_rewarded_drops, 0,
           sizeof(g_vm_net_mock_battle_rewarded_drops));
    g_vm_net_mock_battle_rewarded_drop_result_count = 0;
    g_vm_net_mock_battle_settlement_sent_serial = 0;
    g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
    g_vm_net_mock_battle_recovered_serial = 0;
    g_mockBattleSettleWireRecoverHp = 0;
    g_mockBattleSettleWireRecoverMp = 0;
    g_vm_net_mock_battle_role_id_current = roleId;
    g_vm_net_mock_battle_enemy_id_current = primaryEnemyId;
    g_mockBattleRoleHpCurrent = roleHp;
    g_mockBattleRoleHpMax = roleMaxHp;
    if (g_mockBattleRoleHpMax < g_mockBattleRoleHpCurrent)
        g_mockBattleRoleHpMax = g_mockBattleRoleHpCurrent;
    g_mockBattleRoleMpCurrent = roleMp;
    g_mockBattleRoleMpMax = roleMaxMp;
    if (g_mockBattleRoleMpMax < g_mockBattleRoleMpCurrent)
        g_mockBattleRoleMpMax = g_mockBattleRoleMpCurrent;
    g_mockBattleMonsterHealUsedSerialSlots[0] = 0;
    g_mockBattleMonsterHealUsedSerialSlots[1] = 0;
    g_mockBattleMonsterHealUsedSerialSlots[2] = 0;
    g_vm_net_mock_team_battle_party_count_current = 0;
    g_vm_net_mock_team_battle_member_count_current = 0;
    g_vm_net_mock_team_battle_resolve_monsters_current = 0;
    vm_net_mock_battle_reset_enemy_hp_from_slot_values(enemyHpBySlot, enemyCount);
    vm_net_mock_battle_set_enemy_id_slots(enemyIdsBySlot, enemyCount);
    g_mockBattleAutoSuppressNext12 = 0;
    g_mockBattleAutoClientDriven = 0;
    g_mockBattleAutoFlagPendingArmed = 0;
    g_mockBattleAutoFlagPendingNotBeforeMs = 0;
    g_mockBattleAutoHangupStyleFlagOk = 0;
    vm_net_mock_hangup_loop_clear("ladder-battle-start");
    printf("[info][network] mock_ladder_battle_arm role=%u enemy=%u count=%u "
           "slots=%u/%u/%u rolehp=%u/%u armed=1 "
           "evidence=subtype10-operate-session\n",
           roleId, primaryEnemyId, enemyCount,
           g_mockBattleEnemyHpSlots[0],
           g_mockBattleEnemyHpSlots[1],
           g_mockBattleEnemyHpSlots[2],
           g_mockBattleRoleHpCurrent, g_mockBattleRoleHpMax);
}

/*
 * battleinfo may advertise enemies=2|3 while only slot0 was ever seeded (count
 * briefly 1 at reset, or scene-start flag lost).  Fill never-seeded slots in
 * place without reviving already-dead (max>0, hp=0) monsters.
 */
static void vm_net_mock_battle_ensure_multi_enemy_slots_seeded(u32 enemyId)
{
    vm_net_mock_monster_stats stats;
    u8 enemyCount = vm_net_mock_battle_enemy_count_current();
    u32 perEnemyHp = 0;
    u32 perEnemyMaxHp = 0;
    u8 filled = 0;

    if (enemyCount <= 1)
        return;
    /*
     * Wire maps are owned by battle-start (subtype-5 vs subtype-10).  Do not
     * infer scene maps from enemy count — hangup insight forces enemies=3 on
     * subtype-10 (player wire 0, monsters 1..N).
     */

    stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    perEnemyHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_HP", stats.hp);
    perEnemyMaxHp = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_MAX_HP", perEnemyHp);
    if (perEnemyMaxHp < perEnemyHp)
        perEnemyMaxHp = perEnemyHp;
    if (perEnemyHp == 0)
        perEnemyHp = 1;
    if (perEnemyMaxHp == 0)
        perEnemyMaxHp = perEnemyHp;

    for (u8 i = 0; i < enemyCount && i < 3; ++i)
    {
        if (g_mockBattleEnemyHpMaxSlots[i] != 0)
            continue;
        g_mockBattleEnemyHpSlots[i] = perEnemyHp;
        g_mockBattleEnemyHpMaxSlots[i] = perEnemyMaxHp;
        if (g_mockBattleEnemyIdSlots[i] == 0)
            g_mockBattleEnemyIdSlots[i] = enemyId;
        ++filled;
    }
    if (filled != 0)
    {
        vm_net_mock_battle_sync_enemy_hp_totals();
        printf("[warn][network] mock_battle_enemy_slots_backfill enemies=%u "
               "filled=%u slots=%u/%u/%u max=%u/%u/%u enemyhp=%u "
               "evidence=multi-monster-underseed\n",
               enemyCount,
               filled,
               g_mockBattleEnemyHpSlots[0],
               g_mockBattleEnemyHpSlots[1],
               g_mockBattleEnemyHpSlots[2],
               g_mockBattleEnemyHpMaxSlots[0],
               g_mockBattleEnemyHpMaxSlots[1],
               g_mockBattleEnemyHpMaxSlots[2],
               g_mockBattleEnemyHpCurrent);
    }
}

static bool vm_net_mock_battle_roll_percent(u32 percent)
{
    if (percent == 0)
        return false;
    if (percent >= 100)
        return true;
    return (vm_net_mock_battle_reward_rand() % 100u) < percent;
}

/* Commit a successful drop roll into the battle result cache / backpack.
 * Equipment units are one backpack row each; stackables share one row.
 * When battle insight is active and the bag cannot accept a unit, auto-sell
 * that unit for catalog copper (see vm_net_mock_battle_insight_try_auto_sell_drop). */
static u8 vm_net_mock_battle_commit_drop_grant(
    u32 itemId, u32 grantedCount, u32 enemyCount,
    vm_net_mock_battle_drop_result *results, u8 resultCount)
{
    u16 grantedSeq = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 resolvedUnits = 0;

    if (itemId == 0 || grantedCount == 0 || results == NULL ||
        resultCount >= VM_NET_MOCK_BATTLE_DROP_RESULT_MAX)
    {
        return resultCount;
    }
    if (vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
    {
        u32 grantedEquipUnits = 0;

        for (u32 unit = 0;
             unit < grantedCount &&
             resultCount < VM_NET_MOCK_BATTLE_DROP_RESULT_MAX;
             ++unit)
        {
            u16 oneSeq = 0;
            u32 soldGold = 0;

            if (vm_net_mock_role_add_backpack_item(itemId, 1, &oneSeq))
            {
                results[resultCount].itemId = itemId;
                results[resultCount].seq = oneSeq;
                results[resultCount].count = 1;
                results[resultCount].enhanceLevel = 0;
                ++resultCount;
                ++grantedEquipUnits;
                ++resolvedUnits;
                continue;
            }
            if (!vm_net_mock_battle_insight_try_auto_sell_drop(
                    role, itemId, 1, &soldGold))
            {
                break;
            }
            g_vm_net_mock_battle_rewarded_auto_sell_gold =
                vm_net_mock_add_capped_u32(
                    g_vm_net_mock_battle_rewarded_auto_sell_gold, soldGold);
            ++grantedEquipUnits;
            ++resolvedUnits;
        }
        if (grantedEquipUnits != 0)
        {
            vm_net_mock_task_progress_after_battle(
                g_vm_net_mock_battle_enemy_id_current, enemyCount, itemId,
                grantedEquipUnits);
        }
        return resultCount;
    }
    if (vm_net_mock_role_add_backpack_item(itemId, grantedCount, &grantedSeq))
    {
        results[resultCount].itemId = itemId;
        results[resultCount].seq = grantedSeq;
        results[resultCount].count = grantedCount;
        results[resultCount].enhanceLevel = 0;
        ++resultCount;
        resolvedUnits = grantedCount;
    }
    else
    {
        u32 soldGold = 0;

        if (vm_net_mock_battle_insight_try_auto_sell_drop(
                role, itemId, grantedCount, &soldGold))
        {
            g_vm_net_mock_battle_rewarded_auto_sell_gold =
                vm_net_mock_add_capped_u32(
                    g_vm_net_mock_battle_rewarded_auto_sell_gold, soldGold);
            resolvedUnits = grantedCount;
        }
    }
    if (resolvedUnits != 0)
    {
        vm_net_mock_task_progress_after_battle(
            g_vm_net_mock_battle_enemy_id_current, enemyCount, itemId,
            resolvedUnits);
    }
    return resultCount;
}

static u32 vm_net_mock_battle_reward_exp_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    return stats.exp;
}

static u32 vm_net_mock_battle_reward_gold_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    return stats.gold;
}

static bool vm_mock_service_battle_reward_allowed(u32 *remainingMsOut);
static void vm_mock_service_battle_reward_mark(void);

static u32 vm_net_mock_battle_grant_reward_once(u32 *dropItemIdOut,
                                                u16 *dropSeqOut,
                                                u32 *dropCountOut,
                                                bool *dropGrantedOut)
{
    u32 rewardExp = 0;
    u32 dropItemId = 0;
    u16 dropSeq = 0;
    u32 dropCount = 0;
    bool dropGranted = false;
    u32 enemyCount = vm_net_mock_battle_enemy_count_current();
    vm_net_mock_monster_drop configuredDrops[VM_NET_MOCK_MONSTER_DROP_MAX];
    vm_net_mock_battle_drop_result results[VM_NET_MOCK_BATTLE_DROP_RESULT_MAX];
    u8 configuredDropCount = 0;
    u8 resultCount = 0;
    u32 baseRewardExp = 0;
    u32 expCardMultiplier = 1;
    u32 battleInsightBonusPercent = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    memset(results, 0, sizeof(results));

    if (dropItemIdOut)
        *dropItemIdOut = 0;
    if (dropSeqOut)
        *dropSeqOut = 0;
    if (dropCountOut)
        *dropCountOut = 0;
    if (dropGrantedOut)
        *dropGrantedOut = false;

    if (g_mockBattleOperateSessionSerial == 0)
        return 0;

    if (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial)
    {
        if (g_vm_net_mock_battle_rewarded_drop_result_count != 0)
        {
            if (dropItemIdOut)
                *dropItemIdOut = g_vm_net_mock_battle_rewarded_drops[0].itemId;
            if (dropSeqOut)
                *dropSeqOut = g_vm_net_mock_battle_rewarded_drops[0].seq;
            if (dropCountOut)
                *dropCountOut = g_vm_net_mock_battle_rewarded_drops[0].count;
            if (dropGrantedOut)
                *dropGrantedOut = true;
        }
        return 0;
    }

    if (!vm_mock_service_battle_reward_allowed(NULL))
    {
        /*
         * Victory 4/7 {result=1} with all-zero EXP/gold deltas crashes the
         * result-panel renderer (mmBattle DrawBattleCharacter/HP bar path;
         * see docs/re/2026-07-24-team-battle-terminal-peer-crash.md).  Keep the
         * enter/exit lifecycle intact and pay a 1-EXP consolation so the panel
         * contract stays non-zero while gold/drops stay suppressed.
         */
        g_vm_net_mock_battle_rewarded_serial = g_mockBattleOperateSessionSerial;
        g_vm_net_mock_battle_rewarded_exp = 1;
        g_vm_net_mock_battle_rewarded_auto_sell_gold = 0;
        g_vm_net_mock_battle_reward_rate_suppressed_serial =
            g_mockBattleOperateSessionSerial;
        memset(g_vm_net_mock_battle_rewarded_drops, 0,
               sizeof(g_vm_net_mock_battle_rewarded_drops));
        g_vm_net_mock_battle_rewarded_drop_result_count = 0;
        printf("[info][network] mock_battle_reward_suppressed enemy=%u role=%u "
               "serial=%u consolation_exp=1 reason=reward-interval\n",
               g_vm_net_mock_battle_enemy_id_current,
               role ? role->roleId : 0,
               g_mockBattleOperateSessionSerial);
        /* Victory still happened — advance kill tasks even when drops/EXP
         * are rate-limited. */
        vm_net_mock_task_progress_after_battle(
            g_vm_net_mock_battle_enemy_id_current, enemyCount, 0, 0);
        return 1;
    }

    baseRewardExp = vm_net_mock_mul_capped_u32(
        vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_EXP",
                                   vm_net_mock_battle_reward_exp_for_enemy(g_vm_net_mock_battle_enemy_id_current)),
        enemyCount);
    rewardExp = baseRewardExp;
    if (rewardExp != 0 && role != NULL)
    {
        expCardMultiplier = vm_net_mock_role_active_exp_card_multiplier(role);
        if (expCardMultiplier > 1)
            rewardExp = vm_net_mock_mul_capped_u32(rewardExp, expCardMultiplier);
        /* 战斗心得's resource wording is "experience +20%", not another
         * multiplier. Apply its bonus to the unmodified monster reward so it
         * remains a separately auditable base-reward increment when an
         * experience card is also active. */
        battleInsightBonusPercent = vm_net_mock_role_active_battle_exp_bonus_percent(role);
        if (battleInsightBonusPercent != 0)
        {
            uint64_t bonus = ((uint64_t)baseRewardExp * battleInsightBonusPercent) / 100u;
            rewardExp = vm_net_mock_add_capped_u32(
                rewardExp, bonus > 0xffffffffull ? 0xffffffffu : (u32)bonus);
        }
        {
            u32 marriageMult = vm_net_mock_marriage_world_buff_multiplier();
            if (marriageMult > 1u)
                rewardExp = vm_net_mock_mul_capped_u32(rewardExp, marriageMult);
        }
        /* 打金心得：战斗经验强制为 0（在卡/心得/姻缘加成之后清零）. */
        if (vm_net_mock_role_active_gold_scroll_suppresses_exp(role))
        {
            printf("[info][network] mock_battle_exp_suppressed enemy=%u role=%u "
                   "pre_exp=%u reason=gold-scroll evidence=item:853\n",
                   g_vm_net_mock_battle_enemy_id_current, role->roleId,
                   rewardExp);
            rewardExp = 0;
        }
    }
    if (rewardExp != baseRewardExp)
    {
        printf("[info][network] mock_battle_exp_modifier enemy=%u role=%u base_exp=%u card_multiplier=%u insight_bonus_percent=%u marriage_mult=%u reward_exp=%u\n",
               g_vm_net_mock_battle_enemy_id_current, role ? role->roleId : 0,
               baseRewardExp, expCardMultiplier, battleInsightBonusPercent,
               vm_net_mock_marriage_world_buff_multiplier(),
               rewardExp);
    }
    memset(configuredDrops, 0, sizeof(configuredDrops));
    memset(results, 0, sizeof(results));
    configuredDropCount = vm_net_mock_monster_drops_for_enemy(
        g_vm_net_mock_battle_enemy_id_current, configuredDrops,
        VM_NET_MOCK_MONSTER_DROP_MAX);
    if (configuredDropCount > VM_NET_MOCK_MONSTER_DROP_MAX)
        configuredDropCount = VM_NET_MOCK_MONSTER_DROP_MAX;

    /* These environment variables predate the editable table and are kept as
     * a narrow single-row test override.  They replace the configured list
     * rather than partially modifying an arbitrary multi-drop configuration. */
    if (getenv("CBE_BATTLE_DROP_ITEM_ID") != NULL ||
        getenv("CBE_BATTLE_DROP_RATE") != NULL ||
        (g_vm_net_mock_battle_enemy_id_current == VM_NET_MOCK_BATTLE_POISON_SLIME_ID &&
         (getenv("CBE_BATTLE_CHANGMING_SAN_ITEM_ID") != NULL ||
          getenv("CBE_BATTLE_CHANGMING_SAN_DROP_RATE") != NULL)))
    {
        vm_net_mock_monster_drop overrideDrop;

        memset(&overrideDrop, 0, sizeof(overrideDrop));
        if (configuredDropCount != 0)
            overrideDrop = configuredDrops[0];
        if (g_vm_net_mock_battle_enemy_id_current == VM_NET_MOCK_BATTLE_POISON_SLIME_ID)
        {
            overrideDrop.itemId = vm_net_mock_env_u32_if_set(
                "CBE_BATTLE_CHANGMING_SAN_ITEM_ID", overrideDrop.itemId);
            overrideDrop.ratePercent = (u8)vm_net_mock_env_u32_if_set(
                "CBE_BATTLE_CHANGMING_SAN_DROP_RATE", overrideDrop.ratePercent);
        }
        overrideDrop.itemId = vm_net_mock_env_u32_if_set(
            "CBE_BATTLE_DROP_ITEM_ID", overrideDrop.itemId);
        overrideDrop.ratePercent = (u8)vm_net_mock_env_u32_if_set(
            "CBE_BATTLE_DROP_RATE", overrideDrop.ratePercent);
        configuredDrops[0] = overrideDrop;
        configuredDropCount = overrideDrop.itemId != 0 &&
                              overrideDrop.ratePercent != 0 ? 1 : 0;
    }

    /*
     * Drop modes per battle (still one attempt per selected row, not per
     * defeated enemy; EXP/gold still scale with enemyCount above):
     * - Task-material rows and 100% rows each keep an independent roll.
     * - All other eligible rows form one lottery: pick a single row at
     *   random, then roll that row's configured percent once.
     */
    {
        typedef struct
        {
            bool isTaskMaterial;
            bool policyOk;
            bool eligible;
            bool multiRow;
            u32 remaining;
        } vm_net_mock_battle_drop_gate;
        vm_net_mock_battle_drop_gate gates[VM_NET_MOCK_MONSTER_DROP_MAX];
        u8 lotterySlots[VM_NET_MOCK_MONSTER_DROP_MAX];
        u8 lotteryCount = 0;
        u8 lotteryPickSlot = 0;
        bool lotteryHasPick = false;

        memset(gates, 0, sizeof(gates));
        memset(lotterySlots, 0, sizeof(lotterySlots));

        for (u8 dropIndex = 0; dropIndex < configuredDropCount; ++dropIndex)
        {
            const vm_net_mock_monster_drop *configured =
                &configuredDrops[dropIndex];
            vm_net_mock_battle_drop_gate *gate = &gates[dropIndex];

            if (configured->itemId != 0 && configured->ratePercent != 0 &&
                configured->ratePercent <= 100u && role != NULL)
            {
                gate->policyOk = vm_net_mock_task_material_drop_policy(
                    role->roleId, configured->itemId, &gate->isTaskMaterial,
                    &gate->remaining);
                gate->eligible = gate->policyOk &&
                                 (!gate->isTaskMaterial || gate->remaining != 0);
            }
            /* Task materials stay independent at any configured rate so quest
             * progress is not starved by competing rare loot rows.  100% rows
             * are guaranteed when eligible and likewise stay independent. */
            gate->multiRow =
                gate->isTaskMaterial || configured->ratePercent >= 100u;
            if (gate->eligible && !gate->multiRow &&
                lotteryCount < VM_NET_MOCK_MONSTER_DROP_MAX)
            {
                lotterySlots[lotteryCount++] = dropIndex;
            }
        }

        if (lotteryCount != 0)
        {
            lotteryPickSlot =
                lotterySlots[vm_net_mock_battle_reward_rand() % lotteryCount];
            lotteryHasPick = true;
            printf("[info][network] mock_battle_drop_lottery enemy=%u role=%u "
                   "pool=%u pick_slot=%u\n",
                   g_vm_net_mock_battle_enemy_id_current,
                   role ? role->roleId : 0, lotteryCount,
                   (u32)lotteryPickSlot + 1u);
        }

        for (u8 dropIndex = 0;
             dropIndex < configuredDropCount &&
             resultCount < VM_NET_MOCK_BATTLE_DROP_RESULT_MAX;
             ++dropIndex)
        {
            const vm_net_mock_monster_drop *configured =
                &configuredDrops[dropIndex];
            const vm_net_mock_battle_drop_gate *gate = &gates[dropIndex];
            u32 rolledDropCount = 0;
            u32 grantedCount = 0;
            const char *mode = "ineligible";
            bool selected = false;

            if (gate->eligible)
            {
                if (gate->multiRow)
                {
                    mode = "multi";
                    selected = true;
                }
                else if (lotteryHasPick && dropIndex == lotteryPickSlot)
                {
                    mode = "lottery";
                    selected = true;
                }
                else
                {
                    mode = "lottery_skip";
                }
            }
            if (selected)
            {
                if (vm_net_mock_battle_roll_percent(configured->ratePercent))
                    grantedCount = 1;
                rolledDropCount = grantedCount;
                if (gate->isTaskMaterial && grantedCount > gate->remaining)
                    grantedCount = gate->remaining;
                {
                    u32 marriageMult = vm_net_mock_marriage_world_buff_multiplier();
                    if (marriageMult > 1u && grantedCount > 0)
                        grantedCount =
                            vm_net_mock_mul_capped_u32(grantedCount, marriageMult);
                }
            }
            printf("[info][network] mock_battle_drop_gate enemy=%u role=%u "
                   "slot=%u item=%u rate=%u mode=%s task_material=%u "
                   "remaining=%u policy=%s eligible=%u selected=%u rolled=%u "
                   "grant=%u\n",
                   g_vm_net_mock_battle_enemy_id_current,
                   role ? role->roleId : 0, (u32)dropIndex + 1u,
                   configured->itemId, configured->ratePercent, mode,
                   gate->isTaskMaterial ? 1u : 0u, gate->remaining,
                   gate->policyOk ? "ok" : "unavailable",
                   gate->eligible ? 1u : 0u, selected ? 1u : 0u,
                   rolledDropCount, grantedCount);
            if (grantedCount == 0)
                continue;
            resultCount = vm_net_mock_battle_commit_drop_grant(
                configured->itemId, grantedCount, enemyCount, results,
                resultCount);
        }
    }

    /* Exactly one kill-progress update per victory (independent of drops). */
    vm_net_mock_task_progress_after_battle(
        g_vm_net_mock_battle_enemy_id_current, enemyCount, 0, 0);

    g_vm_net_mock_battle_rewarded_serial = g_mockBattleOperateSessionSerial;
    g_vm_net_mock_battle_rewarded_exp = rewardExp;
    if (g_vm_net_mock_battle_reward_rate_suppressed_serial ==
        g_mockBattleOperateSessionSerial)
    {
        g_vm_net_mock_battle_reward_rate_suppressed_serial = 0;
    }
    memcpy(g_vm_net_mock_battle_rewarded_drops, results, sizeof(results));
    g_vm_net_mock_battle_rewarded_drop_result_count = resultCount;
    vm_mock_service_battle_reward_mark();

    if (resultCount != 0)
    {
        dropItemId = results[0].itemId;
        dropSeq = results[0].seq;
        dropCount = results[0].count;
        dropGranted = true;
    }

    if (dropItemIdOut)
        *dropItemIdOut = dropItemId;
    if (dropSeqOut)
        *dropSeqOut = dropSeq;
    if (dropCountOut)
        *dropCountOut = dropCount;
    if (dropGrantedOut)
        *dropGrantedOut = dropGranted;
    return rewardExp;
}

static void vm_net_mock_role_apply_battle_settlement(u32 hp, u32 mp,
                                                     u32 rewardExp, u32 rewardGold,
                                                     u32 *lastExpOut, u32 *curExpOut,
                                                     u32 *percentExpOut, u32 *levelOut,
                                                     u32 *goldOut, u32 *hpOut, u32 *mpOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 settleHp = hp;
    u32 settleMp = mp;
    u32 levelBefore = 0;
    bool leveledUp = false;

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    levelBefore = role->level;
    if (settleHp > role->hpMax)
        settleHp = role->hpMax;
    if (settleMp > role->mpMax)
        settleMp = role->mpMax;
    role->hp = settleHp;
    role->mp = settleMp;
    leveledUp = vm_net_mock_role_add_exp(role, rewardExp);
    /*
     * Team victory settles dead observers with forceTeamVictory while keeping
     * their real battle HP=0 (docs/re/2026-07-24-team-battle-terminal-peer-crash).
     * add_exp refills HP/MP on level-up; with 10x/30x exp cards that refill is
     * common and leaves Battle.cbm with a zero unit + full durable vitals —
     * the settle/exit path then stalls.  Re-assert the settlement vitals.
     * Living fighters still keep the level-up full refill.
     */
    if (leveledUp && settleHp == 0)
    {
        role->hp = 0;
        role->mp = settleMp > role->mpMax ? role->mpMax : settleMp;
        printf("[info][network] mock_battle_settle_keep_dead_hp role=%u "
               "level=%u->%u reward_exp=%u evidence=team-victory-hp0+"
               "exp-card-levelup\n",
               role->roleId, levelBefore, role->level, rewardExp);
    }
    role->money = (0xffffffffu - role->money < rewardGold) ? 0xffffffffu : role->money + rewardGold;
    vm_net_mock_role_normalize(role);
    if (settleHp == 0 && role->hp != 0)
    {
        role->hp = 0;
        if (settleMp <= role->mpMax)
            role->mp = settleMp;
    }
    vm_net_mock_role_mark_inventory_dirty("battle-settle");

    if (lastExpOut)
        *lastExpOut = vm_net_mock_role_last_level_exp(role->exp);
    if (curExpOut)
        *curExpOut = vm_net_mock_role_next_level_start_exp(role->exp);
    if (percentExpOut)
        *percentExpOut = vm_net_mock_role_exp_percent(role->exp);
    if (levelOut)
        *levelOut = role->level;
    if (goldOut)
        *goldOut = role->money;
    if (hpOut)
        *hpOut = role->hp;
    if (mpOut)
        *mpOut = role->mp;
}

static u32 vm_net_mock_battle_recover_mp_value(void)
{
    return vm_net_mock_env_u32_if_set("CBE_BATTLE_RECOVER_MP", 0);
}

static u32 vm_net_mock_battle_apply_mp_recovery_once(vm_net_mock_role_state *role,
                                                     u32 roleMp,
                                                     u32 recoverMp,
                                                     bool *appliedOut)
{
    u32 mpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;

    if (appliedOut)
        *appliedOut = false;
    if (role == NULL || recoverMp == 0 || g_mockBattleOperateSessionSerial == 0)
        return roleMp;
    if (g_vm_net_mock_battle_recovered_serial == g_mockBattleOperateSessionSerial)
        return roleMp;

    vm_net_mock_role_sync_derived_vitals(role);
    mpMax = role->mpMax ? role->mpMax : VM_NET_MOCK_ROLE_DEFAULT_MP;
    roleMp = vm_net_mock_min_u32(vm_net_mock_add_capped_u32(roleMp, recoverMp), mpMax);
    g_mockBattleRoleMpMax = mpMax;
    g_mockBattleRoleMpCurrent = roleMp;
    g_vm_net_mock_battle_recovered_serial = g_mockBattleOperateSessionSerial;
    if (appliedOut)
        *appliedOut = true;
    return roleMp;
}

static u32 vm_net_mock_role_current_hp_for_battle(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 hp = 0;
    u32 hpMax = 0;
    vm_net_mock_role_default_vitals(role, &hp, &hpMax, NULL, NULL);
    (void)hpMax;
    return hp;
}

static void vm_net_mock_battle_save_terminal_role_state(const char *reason,
                                                        bool forceTeamVictory)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 roleHp = g_mockBattleRoleHpMax != 0 ? g_mockBattleRoleHpCurrent :
                 (role ? role->hp : VM_NET_MOCK_ROLE_DEFAULT_HP);
    /* Same contract as status7: durable role->mp is not the battle bar. */
    u32 roleMp = g_mockBattleRoleMpMax != 0 ? g_mockBattleRoleMpCurrent :
                 (role ? role->mp : VM_NET_MOCK_ROLE_DEFAULT_MP);
    u32 rewardExp = 0;
    u32 rewardGold = 0;
    u32 statusLastExp = 0;
    u32 statusCurExp = 0;
    u32 statusPercentExp = 0;
    u32 statusLevel = 0;
    u32 statusGold = 0;
    u32 dropItemId = 0;
    u16 dropSeq = 0;
    u32 dropCount = 0;
    bool dropGranted = false;
    /* A shared party victory is not invalidated because this particular
     * observer was knocked out earlier in the same battle.  Preserve its
     * actual zero HP, but settle the victory/reward once under its own role
     * state.  Solo callers keep the normal living-player requirement. */
    bool victory = vm_net_mock_battle_all_enemies_defeated() &&
                   (forceTeamVictory || roleHp > 0);
    bool rewardAlreadyGranted = false;
    u32 recoverMp = vm_net_mock_battle_recover_mp_value();
    bool mpRecoveryApplied = false;

    if (role == NULL)
        return;
    if (victory)
    {
        rewardAlreadyGranted = (g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial);
        rewardExp = vm_net_mock_battle_grant_reward_once(&dropItemId,
                                                         &dropSeq,
                                                         &dropCount,
                                                         &dropGranted);
        if (!rewardAlreadyGranted &&
            g_vm_net_mock_battle_reward_rate_suppressed_serial !=
                g_mockBattleOperateSessionSerial)
        {
            rewardGold = vm_net_mock_mul_capped_u32(
                vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_GOLD",
                                           vm_net_mock_battle_reward_gold_for_enemy(g_vm_net_mock_battle_enemy_id_current)),
                vm_net_mock_battle_enemy_count_current());
            {
                u32 marriageMult = vm_net_mock_marriage_world_buff_multiplier();
                u32 goldScrollBonusPct = 0;
                u32 baseRewardGold = rewardGold;

                if (marriageMult > 1u)
                    rewardGold = vm_net_mock_mul_capped_u32(rewardGold, marriageMult);
                /* 打金心得：相对基础战斗钱币 +20%（可审计，叠在姻缘倍数之后）. */
                goldScrollBonusPct =
                    vm_net_mock_role_active_gold_scroll_money_bonus_percent(role);
                if (goldScrollBonusPct != 0)
                {
                    uint64_t bonus =
                        ((uint64_t)baseRewardGold * goldScrollBonusPct) / 100u;
                    rewardGold = vm_net_mock_add_capped_u32(
                        rewardGold,
                        bonus > 0xffffffffull ? 0xffffffffu : (u32)bonus);
                    printf("[info][network] mock_battle_gold_modifier enemy=%u "
                           "role=%u base_gold=%u scroll_bonus_percent=%u "
                           "marriage_mult=%u reward_gold=%u evidence=item:853\n",
                           g_vm_net_mock_battle_enemy_id_current, role->roleId,
                           baseRewardGold, goldScrollBonusPct, marriageMult,
                           rewardGold);
                }
            }
        }
        if (g_vm_net_mock_battle_rewarded_auto_sell_gold != 0)
        {
            rewardGold = vm_net_mock_add_capped_u32(
                rewardGold, g_vm_net_mock_battle_rewarded_auto_sell_gold);
        }
    }
    roleMp = vm_net_mock_battle_apply_mp_recovery_once(role, roleMp, recoverMp,
                                                       &mpRecoveryApplied);
    vm_net_mock_role_apply_battle_settlement(roleHp, roleMp, rewardExp, rewardGold,
                                             &statusLastExp, &statusCurExp,
                                             &statusPercentExp, &statusLevel,
                                             &statusGold, &roleHp, &roleMp);
    /* Reward settlement is only one terminal outcome.  Account for wear here
     * for the victory path; death and a successful escape use the companion
     * completed-state helper below.  The durable-state serial guard makes a
     * repeated terminal response harmless. */
    vm_net_mock_role_service_apply_battle_wear(role);
    vm_autotest_note("mock_battle_terminal_save reason=%s enemy=%u enemies=%u victory=%u team_victory=%u apply_exp=%u gold=%u total_exp=%u level=%u hp=%u mp=%u recover_mp=%u recovered=%u drop=%u seq=%u count=%u\n",
                     reason ? reason : "terminal",
                     g_vm_net_mock_battle_enemy_id_current,
                     vm_net_mock_battle_enemy_count_current(),
                     victory ? 1 : 0,
                     forceTeamVictory ? 1 : 0,
                     rewardExp,
                     statusGold,
                     role->exp,
                     statusLevel,
                     roleHp,
                     roleMp,
                     recoverMp,
                     mpRecoveryApplied ? 1 : 0,
                     dropGranted ? dropItemId : 0,
                     dropSeq,
                     dropCount);
}

static void vm_net_mock_battle_save_current_role_state(const char *reason)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (g_mockBattleRoleHpMax != 0)
    {
        u32 hpMax = role->hpMax ? role->hpMax : g_mockBattleRoleHpMax;
        role->hp = vm_net_mock_min_u32(g_mockBattleRoleHpCurrent, hpMax);
    }
    if (g_mockBattleRoleMpMax != 0)
    {
        u32 mpMax = role->mpMax ? role->mpMax : g_mockBattleRoleMpMax;
        role->mp = vm_net_mock_min_u32(g_mockBattleRoleMpCurrent, mpMax);
    }
    vm_net_mock_role_mark_inventory_dirty(reason ? reason : "battle-state");
}

/* Keep the ordinary state-save helper usable for non-terminal actions such as
 * a failed escape.  Only callers that have ended the battle may charge the
 * one-per-session durability wear. */
static void vm_net_mock_battle_save_completed_current_role_state(const char *reason)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    vm_net_mock_battle_save_current_role_state(reason);
    if (role != NULL)
        vm_net_mock_role_service_apply_battle_wear(role);
}

/* item.dsh row 801: "使用后原地满血复活，免除死亡经验惩罚。"
 *
 * The battle client sends its explicit 1/7/14(result=1) confirmation only
 * after it has found this category-14 item locally.  Keep the authoritative
 * consumption and the persistent HP transition together here; the matching
 * response builder owns the client-facing scene re-entry packet. */
static bool vm_net_mock_role_apply_revival_stone(u16 *consumedSeqOut,
                                                 u32 *remainingOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_backpack_item_state *stone = NULL;
    u16 stoneSeq = 0;
    u32 remaining = 0;

    if (consumedSeqOut)
        *consumedSeqOut = 0;
    if (remainingOut)
        *remainingOut = 0;
    if (role == NULL)
        return false;

    vm_net_mock_role_sync_derived_vitals(role);
    if (role->hp != 0)
        return false;
    stone = vm_net_mock_role_find_backpack_item(role, 801, 0);
    if (stone == NULL || stone->seq == 0 || stone->count == 0)
        return false;

    stoneSeq = stone->seq;
    if (!vm_net_mock_role_consume_backpack_item(role, 801, stoneSeq, 1,
                                                &remaining))
    {
        return false;
    }

    /* 801 restores full combat readiness.  item.dsh text is "原地满血复活";
     * runtime expects both bars full after revival or the map HUD stays empty
     * while the next battle start reseeds from durable vitals. */
    role->hp = role->hpMax;
    role->mp = role->mpMax;
    g_mockBattleRoleHpCurrent = role->hp;
    g_mockBattleRoleHpMax = role->hpMax;
    g_mockBattleRoleMpCurrent = role->mp;
    g_mockBattleRoleMpMax = role->mpMax;
    vm_net_mock_role_mark_inventory_dirty("battle-revival-stone");

    if (consumedSeqOut)
        *consumedSeqOut = stoneSeq;
    if (remainingOut)
        *remainingOut = remaining;
    return true;
}

static u32 vm_net_mock_percent_ceil_u32(u32 value, u32 percent)
{
    uint64_t scaled = (uint64_t)value * (uint64_t)percent;

    if (value == 0 || percent == 0)
        return 0;
    scaled = (scaled + 99ull) / 100ull;
    if (scaled == 0)
        scaled = 1;
    if (scaled > value)
        scaled = value;
    return (u32)scaled;
}

static u32 vm_net_mock_role_apply_death_penalty_to(
    vm_net_mock_role_state *role,
    const char *reason,
    u32 *expPenaltyOut,
    u32 *moneyPenaltyOut,
    u32 *reviveMpOut,
    char *sceneOut,
    size_t sceneOutCap,
    u16 *xOut,
    u16 *yOut)
{
    char sourceScene[64];
    char nearestStoneScene[64];
    const char *respawnScene = NULL;
    const char *respawnRoute = "unresolved";
    u16 respawnX = VM_NET_MOCK_ROLE_INITIAL_X;
    u16 respawnY = VM_NET_MOCK_ROLE_INITIAL_Y;
    u32 sourceSmapRow = 0;
    u32 targetSmapRow = 0;
    u32 respawnDistance = 0;
    u32 expBefore = 0;
    u32 levelBefore = 1;
    u32 levelStartExp = 0;
    u32 levelProgressExp = 0;
    u32 targetExp = 0;
    u32 expPenalty = 0;
    u32 moneyPenalty = 0;
    u32 reviveHp = 0;
    u32 reviveMp = 0;
    vm_net_mock_role_state *activeRole = NULL;

    if (expPenaltyOut)
        *expPenaltyOut = 0;
    if (moneyPenaltyOut)
        *moneyPenaltyOut = 0;
    if (reviveMpOut)
        *reviveMpOut = 0;
    if (sceneOut && sceneOutCap != 0)
        sceneOut[0] = 0;
    if (xOut)
        *xOut = 0;
    if (yOut)
        *yOut = 0;
    if (role == NULL || role->hp != 0)
        return 0;

    memset(sourceScene, 0, sizeof(sourceScene));
    memset(nearestStoneScene, 0, sizeof(nearestStoneScene));
    if (vm_net_mock_scene_name_is_safe(role->scene))
        snprintf(sourceScene, sizeof(sourceScene), "%s", role->scene);

    /* The ordinary death choice must return the player to an authored
     * teleport-stone scene.  Keep an unresolved source explicit and retain the
     * current valid scene rather than silently turning a data failure into a
     * bootstrap-map respawn. */
    if (sourceScene[0] != 0 && vm_net_mock_resolve_nearest_teleport_stone_respawn(
            sourceScene, nearestStoneScene, sizeof(nearestStoneScene),
            &respawnX, &respawnY, &sourceSmapRow, &targetSmapRow,
            &respawnDistance, &respawnRoute))
    {
        respawnScene = nearestStoneScene;
    }
    else if (vm_net_mock_scene_name_is_safe(sourceScene))
    {
        respawnScene = sourceScene;
        respawnRoute = "unresolved-keep-current-scene";
        (void)vm_net_mock_get_scene_reasonable_spawn_from_sce(respawnScene,
                                                              &respawnX,
                                                              &respawnY,
                                                              NULL);
        vm_net_mock_adjust_safe_player_pos_for_scene(respawnScene, &respawnX, &respawnY);
        printf("[error][network] mock_death_respawn_nearest_telestone_unresolved source_scene=%s action=keep-current-scene reason=sMap-wMap-or-SCE-data\n",
               sourceScene);
    }
    else
    {
        respawnScene = vm_net_mock_role_initial_scene_name();
        respawnRoute = "unresolved-invalid-source-initial-scene";
        (void)vm_net_mock_get_scene_reasonable_spawn_from_sce(respawnScene,
                                                              &respawnX,
                                                              &respawnY,
                                                              NULL);
        vm_net_mock_adjust_safe_player_pos_for_scene(respawnScene, &respawnX, &respawnY);
        printf("[error][network] mock_death_respawn_nearest_telestone_unresolved source_scene=- action=initial-scene reason=invalid-role-scene\n");
    }

    vm_net_mock_role_sync_derived_vitals(role);
    expBefore = role->exp;
    levelBefore = vm_net_mock_role_level_from_exp(expBefore);
    /* Penalty is 10% of progress inside the current level only.  Players at
     * exact level-start EXP lose nothing; grade never drops from this path. */
    levelStartExp = vm_net_mock_role_level_start_exp(levelBefore);
    if (levelStartExp == 0xffffffffu || levelStartExp > expBefore)
        levelStartExp = 0;
    levelProgressExp = expBefore - levelStartExp;
    {
        u32 expPercent = (u32)VM_NET_MOCK_ROLE_DEATH_EXP_PENALTY_PERCENT;
        u32 moneyPercent = (u32)VM_NET_MOCK_ROLE_DEATH_MONEY_PENALTY_PERCENT;
        bool redName = vm_net_mock_role_is_red_name(role);

        if (redName)
        {
            expPercent *= 2u;
            moneyPercent *= 2u;
        }
        expPenalty = vm_net_mock_percent_ceil_u32(levelProgressExp, expPercent);
        targetExp = expBefore - expPenalty;
        role->exp = targetExp;
        role->level = vm_net_mock_role_level_from_exp(role->exp);
        moneyPenalty = vm_net_mock_percent_ceil_u32(role->money, moneyPercent);
        role->money -= moneyPenalty;
        if (redName)
        {
            printf("[info][network] mock_death_penalty_red_name role=%u pk=%u "
                   "exp_pct=%u money_pct=%u\n",
                   role->roleId, role->pkPoints, expPercent, moneyPercent);
        }
    }

    vm_net_mock_role_sync_derived_vitals(role);
    reviveHp = vm_net_mock_percent_ceil_u32(role->hpMax,
                                           VM_NET_MOCK_ROLE_DEATH_REVIVE_HP_PERCENT);
    reviveMp = vm_net_mock_percent_ceil_u32(role->mpMax,
                                           VM_NET_MOCK_ROLE_DEATH_REVIVE_MP_PERCENT);
    if (reviveHp == 0 && role->hpMax != 0)
        reviveHp = 1;
    if (reviveMp == 0 && role->mpMax != 0)
        reviveMp = 1;
    role->hp = vm_net_mock_min_u32(reviveHp, role->hpMax);
    role->mp = vm_net_mock_min_u32(reviveMp, role->mpMax);

    snprintf(role->scene, sizeof(role->scene), "%s", respawnScene);
    role->x = respawnX;
    role->y = respawnY;
    vm_net_mock_role_normalize(role);
    vm_net_mock_role_mark_inventory_dirty(reason ? reason : "battle-death");

    activeRole = vm_net_mock_active_role();
    if (activeRole == role)
    {
        g_mockBattleRoleHpCurrent = role->hp;
        g_mockBattleRoleHpMax = role->hpMax;
        g_mockBattleRoleMpCurrent = role->mp;
        g_mockBattleRoleMpMax = role->mpMax;
    }

    printf("[info][network] mock_death_penalty reason=%s level=%u->%u exp=%u->%u exp_penalty=%u money_penalty=%u respawn_scene=%s source_smap=%u target_smap=%u route=%s hops=%u pos=(%u,%u)\n",
           reason ? reason : "battle-death", levelBefore, role->level,
           expBefore, role->exp, expPenalty, moneyPenalty, role->scene,
           sourceSmapRow, targetSmapRow, respawnRoute ? respawnRoute : "-",
           respawnDistance, role->x, role->y);
    vm_autotest_note("mock_death_penalty reason=%s level=%u->%u exp=%u->%u exp_penalty=%u money_penalty=%u respawn_scene=%s source_smap=%u target_smap=%u route=%s hops=%u pos=(%u,%u) evidence=sMap.dsh/wMap.dsh/SCE:n_telestone\n",
                     reason ? reason : "battle-death", levelBefore, role->level,
                     expBefore, role->exp, expPenalty, moneyPenalty, role->scene,
                     sourceSmapRow, targetSmapRow, respawnRoute ? respawnRoute : "-",
                     respawnDistance, role->x, role->y);

    if (expPenaltyOut)
        *expPenaltyOut = expPenalty;
    if (moneyPenaltyOut)
        *moneyPenaltyOut = moneyPenalty;
    if (reviveMpOut)
        *reviveMpOut = role->mp;
    if (sceneOut && sceneOutCap != 0)
        snprintf(sceneOut, sceneOutCap, "%s", role->scene);
    if (xOut)
        *xOut = role->x;
    if (yOut)
        *yOut = role->y;
    return role->hp;
}

static u32 vm_net_mock_role_apply_death_penalty(const char *reason,
                                                u32 *expPenaltyOut,
                                                u32 *moneyPenaltyOut,
                                                u32 *reviveMpOut,
                                                char *sceneOut,
                                                size_t sceneOutCap,
                                                u16 *xOut,
                                                u16 *yOut)
{
    return vm_net_mock_role_apply_death_penalty_to(
        vm_net_mock_active_role(),
        reason,
        expPenaltyOut,
        moneyPenaltyOut,
        reviveMpOut,
        sceneOut,
        sceneOutCap,
        xOut,
        yOut);
}

static void vm_net_mock_save_player_pos_state(const char *scene, u16 x, u16 y, const char *reason)
{
    char runtimeScene[64];
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (x == 0 || y == 0)
        return;

    /* A scene transition can name an SCE that the server has not yet resolved
     * locally.  Its exact key remains authoritative for this durable position;
     * substituting a runtime/default scene here changes the next 16/2 target. */
    if (!vm_net_mock_scene_name_is_persistable(scene))
    {
        if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
            scene = runtimeScene;
        else if (role != NULL && vm_net_mock_scene_name_is_persistable(role->scene))
            scene = role->scene;
        else
            scene = vm_net_mock_default_scene_name();
    }
    if (!vm_net_mock_scene_name_is_safe(scene))
    {
        printf("[warn][network] mock_scene_position_unresolved scene=%s action=preserve-exact-key reason=server-sce-not-found save_reason=%s\n",
               scene ? scene : "-", reason ? reason : "position");
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
    if (vm_net_mock_role_position_needs_edge_rescue(x, y))
    {
        u16 rescuedX = x;
        u16 rescuedY = y;
        if (role != NULL &&
            vm_net_mock_scene_names_equal_loose(role->scene, scene) &&
            vm_net_mock_role_rescue_edge_position(role, reason))
        {
            x = role->x;
            y = role->y;
        }
        else if (vm_net_mock_get_scene_reasonable_spawn_from_sce(scene,
                                                                 &rescuedX,
                                                                 &rescuedY,
                                                                 NULL))
        {
            x = rescuedX;
            y = rescuedY;
            vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
        }
        if (vm_net_mock_role_position_needs_edge_rescue(x, y))
            return;
    }
    vm_net_mock_role_set_position(scene, x, y, reason);
}

static void vm_net_mock_mark_pending_scene_pos_save(const char *scene, u16 x, u16 y, const char *reason)
{
    if (!vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    snprintf(g_vm_net_mock_pending_scene_save_scene, sizeof(g_vm_net_mock_pending_scene_save_scene),
             "%s", scene);
    snprintf(g_vm_net_mock_pending_scene_save_reason, sizeof(g_vm_net_mock_pending_scene_save_reason),
             "%s", reason ? reason : "pending-scene-load");
    g_vm_net_mock_pending_scene_save_x = x;
    g_vm_net_mock_pending_scene_save_y = y;
    g_vm_net_mock_pending_scene_save_valid = true;
}

static const char *vm_net_mock_current_scene_name(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *overrideName = vm_net_mock_env_str("CBE_SCENE_KEY", "");
    static char runtimeScene[64];

    /*
     * This helper is used while serving a network request.  Global_R9 is the
     * scene of the emulator process hosting the mock service, not necessarily
     * the scene of the remote client that issued that request.  In particular,
     * the settings "unstuck" flow persists the returned scene immediately.
     * Reading Global_R9 first therefore lets one client's (often initial-map)
     * scene overwrite another authenticated role's location.
     *
     * The selected role is the request-scoped authority once one exists.  The
     * environment key and local runtime scene remain useful only before a
     * request has selected a role (local/offline diagnostics).
     */
    if (role != NULL && vm_net_mock_scene_name_is_persistable(role->scene))
        return vm_net_mock_normalize_scene_name_for_enter(role->scene);
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
        return vm_net_mock_normalize_scene_name_for_enter(runtimeScene);
    return vm_net_mock_default_scene_name();
}

static u16 vm_net_mock_scene_spawn_x(void)
{
    if (getenv("CBE_SCENE_POS_X") != NULL)
        return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_X", VM_NET_MOCK_ROLE_INITIAL_X);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role != NULL && role->x != 0)
        return role->x;
    return VM_NET_MOCK_ROLE_INITIAL_X;
}

static u16 vm_net_mock_scene_spawn_y(void)
{
    if (getenv("CBE_SCENE_POS_Y") != NULL)
        return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_Y", VM_NET_MOCK_ROLE_INITIAL_Y);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role != NULL && role->y != 0)
        return role->y;
    return VM_NET_MOCK_ROLE_INITIAL_Y;
}

static const char *vm_net_mock_default_scene_name(void)
{
    /*
     * Fresh roles start on the Penglai TongQueTai island. The actual landing
     * point is resolved from this scene's SCE edge-portal spawn and then moved
     * away from the trigger rectangle; VM_NET_MOCK_ROLE_INITIAL_X/Y are only a
     * last-resort fallback when the SCE cannot be read.
     */
    return "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"; /* GBK: c00PenglaiXiandao_01.sce */
}

static const char *vm_net_mock_scene_key_name(void)
{
    const char *overrideName = vm_net_mock_env_str("CBE_SCENE_KEY", "");
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    /*
     * This key is copied by parse_actorinfo_response() into R9+0x5E46 and later
     * reused as the mode-10 descriptor name by scene_rebuild_runtime_nodes().
     * The ASCII experiment proved the update path, but also bypassed the local
     * .sce scene descriptor and left the map background black when cached.
     * Keep the default aligned with 30/1.scene and use CBE_SCENE_KEY for
     * non-colliding descriptor experiments.
     */
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    if (role != NULL && vm_net_mock_scene_name_is_safe(role->scene))
        return vm_net_mock_normalize_scene_name_for_enter(role->scene);
    return vm_net_mock_current_scene_name();
}

static const char *vm_net_mock_fb_target_info_text(void)
{
    const char *overrideInfo = vm_net_mock_env_str("CBE_FB_TARGET_INFO", "");
    if (overrideInfo != NULL && overrideInfo[0] != 0)
        return overrideInfo;
    return "";
}

static bool vm_net_mock_put_scene_fields_with(u8 *out, u32 outCap, u32 *pos,
                                               bool includeResult, bool includeType, u8 requestType,
                                               const char *sceneName, u16 spawnX, u16 spawnY)
{
    u8 posInfo[8];
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    if (posInfoLen == 0)
        return false;
    if (includeResult && !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (includeType && !vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "scene", sceneName ? sceneName : vm_net_mock_default_scene_name()))
        return false;
    return vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen);
}

static bool vm_net_mock_put_scene_ack_without_posinfo(u8 *out, u32 outCap, u32 *pos,
                                                      u8 requestType, const char *sceneName)
{
    /*
     * Validated no-posinfo close: 30/2 {result=1,type,scene} reaches
     * ResetDownloadState without EnterScene (docs/re/2026-07-22-teleport-
     * penglai-mijing-progress-stall.md).  result=2 was tried 2026-07-27 and
     * left login→shop-exit stuck on sustain poll with only 25/5 — revert.
     * result=1 + posinfo is the separate enter path; this helper never
     * writes posinfo.
     */
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    return vm_net_mock_put_object_string(out, outCap, pos, "scene",
                                         sceneName ? sceneName : vm_net_mock_default_scene_name());
}

static bool vm_net_mock_put_scene_fields(u8 *out, u32 outCap, u32 *pos, bool includeResult, bool includeType, u8 requestType)
{
    return vm_net_mock_put_scene_fields_with(out, outCap, pos,
                                             includeResult, includeType, requestType,
                                             vm_net_mock_scene_key_name(),
                                             vm_net_mock_scene_spawn_x(),
                                             vm_net_mock_scene_spawn_y());
}

typedef struct
{
    char scene[64];
    u16 x;
    u16 y;
    u32 exitId;
    u8 mapType;
    bool hasSceEntry;
    bool needsSceneDownload;
} vm_net_mock_scene_change_target;

typedef struct
{
    char targetScene[64];
    u16 entryId;
    u16 targetEntryId;
    u16 left;
    u16 top;
    u16 right;
    u16 bottom;
    u16 spawnX;
    u16 spawnY;
} vm_net_mock_sce_edge_portal;

static vm_net_mock_scene_change_target g_vm_net_mock_last_scene_change_target;
static bool g_vm_net_mock_last_scene_change_target_valid = false;
static u32 g_vm_net_mock_last_scene_change_target_serial = 0;
static bool g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
static bool g_vm_net_mock_teleport_stone_direct_enter_pending = false;
static bool g_vm_net_mock_teleport_stone_map_enter_pending = false;
static u32 g_vm_net_mock_last_teleport_stone_list_tick = 0;
static vm_net_mock_scene_change_target g_vm_net_mock_teleport_stone_confirm_target;
static bool g_vm_net_mock_teleport_stone_confirm_target_valid = false;
/*
 * A confirmed map-stone request batches 16/2 + 16/3 + optional 7/1 in one WT
 * packet.  The inventory acknowledgement must finish its callback before the
 * main-business 30/1 scene entry is delivered: entering the scene in that same
 * callback exposes the still-live CBM confirmation screen underneath the scene
 * screen and its loading widget keeps a destroyed image owner.  Arm the target
 * here and let the next service poll deliver 30/1 as a separate network event.
 */
static vm_net_mock_scene_change_target g_vm_net_mock_teleport_stone_deferred_enter_target;
static bool g_vm_net_mock_teleport_stone_deferred_enter_valid = false;
static u32 g_vm_net_mock_teleport_stone_deferred_enter_tick = 0;
/*
 * Owner of the armed deferred 30/1.  Account-mirrored globals are shared by
 * every sticky session on the same login; without an owner id the other
 * client's scene-poll (or stale_map_transfer_clear) can steal/clear Pixi's
 * map-stone enter while CBE still looks fine — observed on 泰山 while CBE
 * remained online on the same account.
 */
static u32 g_vm_net_mock_teleport_stone_deferred_enter_client_id = 0;
/*
 * Confirmed map-stone whose final scene matches the player's current scene.
 * Same-scene transfers often never emit WT6/1; wait_wt6 must not defer 27/11.
 */
static bool g_vm_net_mock_teleport_stone_same_scene = false;
/* Armed when task-transport select returns 16/6 confirm UI; consumed by the
 * later inbound 16/6 confirm request (result/taskid/transid). */
static vm_net_mock_scene_change_target g_vm_net_mock_task_transport_pending_target;
static bool g_vm_net_mock_task_transport_pending_valid = false;
static u32 g_vm_net_mock_task_transport_pending_task_id = 0;
static bool g_vm_net_mock_last_scene_change_from_actor_other_portal = false;
static u8 g_vm_net_mock_last_scene_change_fb4_type = 1;
static vm_net_mock_scene_change_target g_vm_net_mock_last_completed_scene_change_target;
static bool g_vm_net_mock_last_completed_scene_change_target_valid = false;
static u32 g_vm_net_mock_last_completed_scene_change_tick = 0;
/*
 * A title role-select starts the first scene screen from the subtype-6
 * actorinfo object.  The later WT 12/1 is emitted near the end of that same
 * scene initialization and is only a resource/business follow-up; it must not
 * carry another scene+posinfo enter object.
 */
static bool g_vm_net_mock_title_role_scene_followup_pending = false;
static char g_vm_net_mock_last_current_scene_reload_scene[64];
static bool g_vm_net_mock_last_current_scene_reload_valid = false;
static u32 g_vm_net_mock_last_current_scene_reload_tick = 0;
static char g_vm_net_mock_last_moveinfo_source_scene[64];
static u16 g_vm_net_mock_last_moveinfo_source_x = 0;
static u16 g_vm_net_mock_last_moveinfo_source_y = 0;
static u32 g_vm_net_mock_last_moveinfo_source_tick = 0;
static bool g_vm_net_mock_last_moveinfo_source_valid = false;

typedef struct vm_mock_service_account_state
{
    char accountId[64];
    struct vm_mock_service_account_state *next;
    /* Admin/management opens pin the node so offline release cannot free it. */
    u32 pinCount;

    bool netMockSplitProbe;
    u8 netMockUpdateDelivered;
    u32 netMockEnterGameOffset;
    u32 netMockEnterGameChecksum;

    bool pendingSceneSaveValid;
    char pendingSceneSaveScene[64];
    char pendingSceneSaveReason[64];
    u16 pendingSceneSaveX;
    u16 pendingSceneSaveY;

    u32 mockBattleOperateSessionSerial;
    u32 mockBattleMonsterHealUsedSerialSlots[3];
    u32 mockBattleOperateTurnCounter;
    u8 mockBattleOperateSessionArmed;
    u8 mockBattleAwaitsRevivalConfirm;
    u8 mockBattleOperateSessionFinished;
    u8 mockBattlePendingEnemyTurn;
    u8 mockBattleAwaitingSettlement;
    u8 mockBattleSettlementExitPending;
    u32 mockBattleSettlementExitNotBeforeMs;
    u32 mockBattleEncounterNotBeforeMs;
    u8 mockBattleEncounterCooldownClearPending;
    u32 mockBattlePostExitSuppressSceneDefaultUntilMs;
    u8 mockBattlePostExitSettlePending;
    u32 mockBattlePostExitSettleNotBeforeMs;
    u8 mockBattleSceneMonsterStartActive;
    u8 mockBattleStartUsesSceneWireMaps;
    u8 mockBattleLastOperateValid;
    u32 mockBattleLastOperate;
    u32 mockBattleLastIndex;
    u8 mockBattleAutoPrefer;
    u8 mockBattleAutoPreferNoHangup;
    u8 mockBattleAutoPendingArmed;
    u32 mockBattleAutoPendingNotBeforeTick;
    u32 mockBattleAutoNextActNotBeforeMs;
    u8 mockBattleLastRoundActionCount;
    u8 mockBattleAutoFlagPendingArmed;
    u32 mockBattleAutoFlagPendingNotBeforeMs;
    u8 mockBattleAutoHangupStyleFlagOk;
    u8 mockBattleAutoClientDriven; /* deprecated, always 0 */
    u8 mockBattleAutoSuppressNext12;
    u8 mockHangupLoopActive;
    u8 mockHangupLoopScheduleAfterExit;
    u8 mockHangupLoopPendingArmed;
    u32 mockHangupLoopNotBeforeMs;
    u8 mockHangupStartPendingArmed;
    u32 mockHangupStartNotBeforeMs;
    u8 mockHangupStopAfterBattle;
    u32 mockBattleRoleHpCurrent;
    u32 mockBattleRoleHpMax;
    u32 mockBattleRoleMpCurrent;
    u32 mockBattleRoleMpMax;
    u8 mockBattleEnemyCountCurrent;
    u32 mockBattleEnemyIdSlots[3];
    u8 mockBattleEnemyJobSlots[3];
    u32 mockBattleEnemyHpSlots[3];
    u32 mockBattleEnemyHpMaxSlots[3];
    u32 mockBattleEnemyHpCurrent;
    u32 mockBattleEnemyHpMax;
    vm_net_mock_battle_enemy_ailment mockBattleEnemyAilments[3];
    vm_net_mock_battle_stat_modifier mockBattleSoloModifier;

    u8 netMockTitleServerListPending;
    u8 netMockTitleServerSelectConfirmed;
    u32 netMockBackpackGridSeededRoleId;
    u8 netMockShop17ListPending;
    u32 netMockTitleServerListTick;
    u32 netMockTitleServerSelectTick;
    u32 netMockTitleSelectedServerId;
    u8 netMockBackpackPreferRoleListAfterShopBuy;
    bool updateCompletedReenterPending;
    char updateCompletedName[64];

    vm_net_mock_role_db_file roleDb;
    bool roleDbLoaded;
    bool roleDbValid;
    bool rolePositionDirty;
    bool roleInventoryDirty;
    /* Copied with roleDb so deferred MySQL can run without protocol lock. */
    vm_net_mock_warehouse_state warehouse;
    u32 persistGeneration;
    bool persistFlushBusy;
    u32 persistDirtySerial;
    bool persistReflushWanted;
    u32 selectedGuildId;
    bool pendingGuildCreateNameValid;
    char pendingGuildCreateName[VM_NET_MOCK_GUILD_NAME_SIZE];

    u32 battleRewardedSerial;
    u32 battleRewardedExp;
    vm_net_mock_battle_drop_result
        battleRewardedDrops[VM_NET_MOCK_BATTLE_DROP_RESULT_MAX];
    u8 battleRewardedDropResultCount;
    u32 battleEnemyIdCurrent;
    u32 battleRoleIdCurrent;
    u32 battleRewardRng;
    u32 battleSettlementSentSerial;
    u32 battleDropRefreshSentSerial;
    u32 battleRecoveredSerial;
    u32 battleRewardRateSuppressedSerial;
    u32 mockBattleSettleWireRecoverHp;
    u32 mockBattleSettleWireRecoverMp;
    u8 offlinePractiseLoginFlag;
    char offlinePractiseLoginInfo[160];
    u32 remoteCompletedSceneTargetSerial;

    char sceneMoveinfoNpcPendingScene[64];
    bool sceneMoveinfoNpcPending;
    bool sceneMoveinfoNpcWaitPostEnter;
    bool sceneMoveinfoNpcWaitWt6;
    char sceneMoveinfoNpcSeededScene[64];
    bool sceneMoveinfoNpcSeeded;

    vm_net_mock_scene_change_target lastSceneChangeTarget;
    bool lastSceneChangeTargetValid;
    u32 lastSceneChangeTargetSerial;
    bool teleportStoneSubtype3AckSent;
    bool teleportStoneDirectEnterPending;
    bool teleportStoneMapEnterPending;
    u32 lastTeleportStoneListTick;
    vm_net_mock_scene_change_target teleportStoneConfirmTarget;
    bool teleportStoneConfirmTargetValid;
    vm_net_mock_scene_change_target teleportStoneDeferredEnterTarget;
    bool teleportStoneDeferredEnterValid;
    u32 teleportStoneDeferredEnterTick;
    u32 teleportStoneDeferredEnterClientId;
    bool teleportStoneSameScene;
    vm_net_mock_scene_change_target taskTransportPendingTarget;
    bool taskTransportPendingValid;
    u32 taskTransportPendingTaskId;
    bool lastSceneChangeFromActorOtherPortal;
    u8 lastSceneChangeFb4Type;

    vm_net_mock_scene_change_target lastCompletedSceneChangeTarget;
    bool lastCompletedSceneChangeTargetValid;
    u32 lastCompletedSceneChangeTick;
    bool titleRoleSceneFollowupPending;

    char lastCurrentSceneReloadScene[64];
    bool lastCurrentSceneReloadValid;
    u32 lastCurrentSceneReloadTick;

    char lastMoveinfoSourceScene[64];
    u16 lastMoveinfoSourceX;
    u16 lastMoveinfoSourceY;
    u32 lastMoveinfoSourceTick;
    bool lastMoveinfoSourceValid;
} vm_mock_service_account_state;

static vm_mock_service_account_state *g_vm_mock_service_accounts = NULL;
static vm_mock_service_account_state *g_vm_mock_service_active_account = NULL;
/* Which account_state the process globals currently mirror.  Consecutive
 * requests for the same pointer can skip the heavy roleDb memcpy on restore
 * (phase B).  Cleared on restore(NULL) or when a different account is loaded. */
static vm_mock_service_account_state *g_vm_mock_service_globals_account = NULL;
static u32 g_vm_mock_service_active_client_id = 0;

enum
{
    VM_MOCK_SERVICE_PEER_SYNC_MAX = 16,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX = 4,
    VM_MOCK_SERVICE_CHAT_NOTICE_MAX = 64,
    /* Per scene-sync/login packet.  Raised 4→10 so login world-history
     * (up to 10) reaches the bottom faster within MAIN_BUSINESS_OBJECT_MAX. */
    VM_MOCK_SERVICE_CHAT_POLL_MAX = 10,
    VM_MOCK_SERVICE_WORLD_CHAT_HISTORY_MAX = 10,
    VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX = 10,
    VM_MOCK_SERVICE_TEAM_MAX = 16,
    VM_MOCK_SERVICE_TEAM_MEMBER_MAX = 3,
    VM_MOCK_SERVICE_TEAM_BATTLE_EVENT_MAX = 8,
    VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX = 2048,
    VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX = 512,
    VM_MOCK_SERVICE_DUEL_MAX = 64,
    VM_MOCK_SERVICE_DUEL_EVENT_MAX = 8,
    VM_MOCK_SERVICE_TRADE_MAX = 16,
    VM_MOCK_SERVICE_TRADE_ITEM_MAX = 10
};

enum
{
    VM_MOCK_CHAT_TYPE_WORLD = 0,
    VM_MOCK_CHAT_TYPE_TEAM = 2,
    VM_MOCK_CHAT_TYPE_GUILD = 3,
    VM_MOCK_CHAT_TYPE_LOCAL = 4,
    VM_MOCK_CHAT_TYPE_SYSTEM = 5,
    VM_MOCK_CHAT_TYPE_PRIVATE = 7,
    VM_MOCK_CHAT_TYPE_TEAM_NOTICE = 8,
    VM_MOCK_CHAT_TYPE_INVALID = 0xFF
};

enum
{
    VM_MOCK_CHAT_REQUEST_WORLD = 0,
    VM_MOCK_CHAT_REQUEST_TEAM = 2,
    VM_MOCK_CHAT_REQUEST_GUILD = 3,
    VM_MOCK_CHAT_REQUEST_LOCAL = 4
};

typedef struct
{
    u32 sourceClientId;
    u32 actorId;
    u32 lastMoveSerial;
    bool visible;
} vm_mock_service_peer_sync;

enum
{
    VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE = 0,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_INVITE = 1,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_INVITE = 2,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_RESULT = 3,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_RESULT = 4,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_INVITE = 5,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_RESULT = 6,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_MEMBER_JOIN = 7,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_LEAVE = 8,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_HSP = 9,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED = 10,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_REJECTED = 11,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_INVITE = 12,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_RESULT = 13,
    /* Target poll delivers 10/13 {id,fname,name}; reply is 1/10/15.
     * Evidence: JianghuOL.CBE HandleGuildBusinessDispatch jump table
     * subtype 13 → invite confirm (0x0103C61E); subtype 15 → result tip. */
    VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_INVITE = 14,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_INVITE_RESULT = 15
};

typedef struct
{
    u8 type;
    u8 result;
    u32 sourceClientId;
    u32 sourceRoleId;
    /* Observer-scoped wire id frozen at enqueue (0x6Axxxxxx on roleId
     * collision).  TEAM_LEAVE must prefer this over a later live lookup:
     * mark_offline clears onlineRoleId, and session reuse can bind a new
     * identity before the peer's scene-sync poll delivers 5/7. */
    u32 sourceWireId;
    u16 sourceLevel;
    u8 sourceJob;
    u8 sourceSex;
    char sourceAccountId[64];
    char sourceName[32];
    u32 guildId;
    u16 guildStatus;
    char guildName[VM_NET_MOCK_GUILD_NAME_SIZE];
    u32 queuedTick;
} vm_mock_service_social_notice;

typedef struct
{
    bool valid;
    u8 type;
    u32 sourceClientId;
    u32 sourceRoleId;
    char sourceName[16];
    char message[82];
    u32 queuedTick;
    /* 0 = deliver as soon as polled; else hold until g_schedulerTick. */
    u32 notBeforeTick;
} vm_mock_service_chat_notice;

typedef struct
{
    bool valid;
    bool terminalVictory;
    u32 serial;
    u32 sourceClientId;
    u8 deliveredMask;
    /* actionnum from the queued 4/6; peer settle exit must wait this playback. */
    u8 actionCount;
    u16 objectLen;
    u8 objectData[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX];
} vm_mock_service_team_battle_event;

typedef struct
{
    bool valid;
    u32 serial;
    u32 sourceClientId;
    u8 memberIndex;
    u8 actionCount;
    u16 actionInfoLen;
    u8 actionInfo[VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX];
} vm_mock_service_team_battle_round_action;

typedef struct
{
    bool valid;
    u8 sourceIndex;
    u32 operate;
    /* Magnitude for actioninfo valueA: damage amount, heal amount, or 0. */
    u32 damage;
    u32 sourceMpAfter;
    /* HP of the affected seat after this hit (self for heal; foe for damage). */
    u32 targetHpAfter;
    /* PvE support-skill contract: heal/buff play on caster wire; status on foe. */
    bool targetSelf;
    /* When set, valueA is positive/zero (heal/buff/status), never a damage delta. */
    bool supportNoDamage;
    /* mmBattle child_flag: 2=暴击, 3=闪躲; 0=normal numeric float. */
    u8 childFlag;
} vm_mock_service_duel_hit;

typedef struct
{
    bool valid;
    bool terminal;
    u32 serial;
    u8 deliveredMask;
    u8 hitCount;
    vm_mock_service_duel_hit hits[2];
} vm_mock_service_duel_event;

typedef struct
{
    u32 itemId;
    u16 sourceSeq;
    u16 destinationSeq;
    u32 count;
    u16 enhanceLevel;
    u32 enhanceInstanceId;
    vm_net_mock_enhance_wash_state enhanceWash;
} vm_mock_service_trade_item;

typedef struct
{
    bool submitted;
    u8 itemCount;
    u32 money;
    vm_mock_service_trade_item items[VM_MOCK_SERVICE_TRADE_ITEM_MAX];
} vm_mock_service_trade_offer;

typedef struct
{
    bool used;
    bool active;
    u32 clientIds[2];
    vm_mock_service_trade_offer offers[2];
    u8 confirmedMask;
    u8 offerPendingMask;
    u8 terminalPendingMask;
    u8 terminalSubtype;
    u8 terminalResult;
    u32 finalMoney[2];
    vm_mock_service_trade_offer receipts[2];
} vm_mock_service_trade;

enum
{
    VM_MOCK_SERVICE_TASK_OFFER_CONTEXT_MAX = 10
};

typedef struct
{
    u32 roleId;
    u32 taskId;
    u32 actorId;
    bool repeatable;
    u16 dailyAcceptLimit;
    char scene[64];
} vm_mock_service_task_offer_context;

typedef struct vm_mock_service_client_session
{
    u32 clientId;
    /*
     * Captured from pre-login WT 18/9 (field codeVersion).  Login rejects when
     * CBE_CODE_VERSION / server_code_version.txt is set and does not match.
     */
    bool haveClientCodeVersion;
    u32 clientCodeVersion;
    char accountId[64];
    bool roleOnline;
    bool onlinePresenceValid;
    /*
     * Set by account-login takeover: next poll/data from this clientId gets a
     * force-offline tip (1/3/3) then the TCP session ends.  accountId is
     * already cleared so drop-no-account alone would leave CBE/Pixi hanging.
     */
    bool accountKickedPending;
    /* Set by maintenance mass-kick: next poll delivers tip then closes TCP. */
    bool maintenanceKickPending;
    u32 onlineRoleId;
    char onlineRoleName[32];
    char onlineRoleTitle[32];
    char onlineRoleTitleBadge[32];
    u8 onlineJob;
    u8 onlineSex;
    u16 onlineLevel;
    u32 onlineEquippedItemIds[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    u16 onlineEquippedEnhanceLevels[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    /* Mirrored from role->pkPoints for nearby Pixi pkinfo companion. */
    u32 onlinePkPoints;
    /* Pixi cosmetics equipped (WT 1/31); mirrored from role on presence capture. */
    u32 onlineEqChenghaoId;
    u32 onlineEqWingId;
    u32 onlineEqMountId;
    char onlineGuildName[VM_NET_MOCK_GUILD_NAME_SIZE];
    struct
    {
        bool valid;
        u32 actorId;
        u32 eqChenghao;
        u32 eqWing;
        u32 eqMount;
        char guildName[VM_NET_MOCK_GUILD_NAME_SIZE];
    } cosmeticNotices[64];
    /* Admin unlock / grant: next poll delivers fresh 31/2 for this session. */
    bool cosmeticsStatePushPending;
    /* scheduler tick ms of last peer 31/5 drain (scene_sync_poll rate limit). */
    u32 cosmeticsPeerFlushLastMs;
    u32 onlineHp;
    u32 onlineHpMax;
    u32 onlineMp;
    u32 onlineMpMax;
    char onlineScene[64];
    u16 onlineX;
    u16 onlineY;
    u32 onlineTick;
    bool sceneVisibleReady;
    bool sceneVisiblePending;
    char sceneVisibleScene[64];
    u16 sceneVisibleX;
    u16 sceneVisibleY;
    u32 sceneVisibleTick;
    bool shopSceneNpcReseedPending;
    char shopSceneNpcReseedScene[64];
    /*
     * True from shop-open until shop-return rehydrate.  Deferred kind-2 30/1
     * must not fire while mmShop owns the screen.
     */
    bool mmShopShellActive;
    /*
     * Count of shop-actor-query14 acks while mmShopShellActive.
     * mmShop exit (0xdc0c) re-calls sub_162C and waits for *[r9+0x2868]+0x10==0.
     * Early acks (trailing after scene-combo / double open14): status+money only.
     * Late acks after shell age: capped live 1/1/14 loading-clear for exit.
     */
    u8 shopActorQueryAckCount;
    /* scheduler tick when mmShopShellActive rose; ages trailing vs exit. */
    u32 mmShopShellArmedTick;
    /*
     * 1 = marked by scene-interaction combo (trailing open14 expects
     * status+money).  2 = marked by shop-actor-query14 first open (next
     * already_open is exit 0x162C and needs actor loading-clear).
     */
    u8 mmShopOpenMarkSource;
    /*
     * Nonempty type-21 catalog deferred until after shop-return shell poll
     * 30/2.  Same-packet 27/11 on WT6/1 races mmGame ScreenInit (临安府_01
     * 2026-07-28: clear×5+26/0 still stuck; empty-npc maps skip 27/11 and OK).
     */
    bool shopReturnNpcCatalogPending;
    char shopReturnNpcCatalogScene[64];
    /* Set when shell 30/2 finishes; catalog poll waits a settle gap. */
    u32 shopReturnNpcCatalogReadyTick;
    /*
     * Warm priorSeeded shop-return already restored nonempty 27/11 on type27
     * or the first follow-up.  WT6/1 must not empty-gate + defer a second
     * catalog (传送后二次 DoLoading 卡住 2026-08-01).
     */
    bool shopReturnWarmNpcDelivered;
    /*
     * Per-client: nonempty 27/11 already delivered for this scene visit.
     * Shop-open warm capture and poll once-seed must NOT use process-global
     * g_vm_net_mock_scene_moveinfo_npc_seeded — that flag is shared across
     * sticky account mirrors and races multi-client / mark_pending (梦境出商城
     * 2026-08-05: early-shell then startup-scene-sync-poll third DoLoading).
     */
    bool sceneNpcCatalogSeeded;
    char sceneNpcCatalogSeededScene[64];
    /*
     * Scene already had type-21 seed this visit when mmShop opened (from
     * sceneNpcCatalogSeeded).  Client map/NPC *resources* stay warm; shop-
     * return force re-emits 27/11 without clearing a shared global seeded bit.
     */
    bool shopReturnNpcResourcesWarm;
    /*
     * Dream / warm-resource shop-return: restore type-21 on the FINAL shell
     * 30/2 packet (27/11+30/2) — never after the map is already visible.
     * Deferred catalog poll is a forced second DoLoading (用户 2026-08-05).
     */
    bool shopReturnNpcInlineOnShell;
    /*
     * Dream mmShop→mmGame drops SCE type4 battle mapctrl; same-scene kind-2
     * 30/1 is host-suppressed (same-suppressed-dream-reenter).  Foot 4/1 must
     * use HAS_FOLLOWUP lone 4/10 until the player leaves 29* (2026-08-05).
     */
    bool shopReturnDreamNeedNonSceneBattle;
    /*
     * Dream shop-return skipped kind-2 EnterScene (match outdoor warm).  Next
     * PvE settlement_exit must tear down with 4/8 only — full 4/11 type=0
     * reopens DoLoading on that fragile shell and later poll 30/2 cannot
     * recover (6ba47803 2026-08-05 idle-timeout after arm-poll-30/2).
     */
    bool shopReturnDreamNeedSettleBarClear;
    /*
     * Legacy: 续 12 used this to arm deferred 30/1 for instance timer after
     * warm kind2_skip.  That third EnterScene multi-loaded / stuck the bar
     * (2026-08-05).  note_dream_shop_return_kind2_skip now keeps this false;
     * busy_ack only drops a stale bit.  Timer-without-EnterScene unresolved.
     */
    bool shopReturnDreamNeedHudRebuild;
    /*
     * Armed while map-stone 30/2 runs for dream-hud-timer-rebuild.  Forces the
     * kind2DreamShellClear hold on target_valid (no catalog pending on this
     * warm path) so ResetDownloadState waits for ScreenInit.
     */
    bool shopReturnDreamHudMapStoneHold;
    /* Second clear window after deferred 27/11 (longer gap/remaining). */
    bool shopReturnLoadingClearPostCatalog;
    /*
     * Scene already had type-21 this visit (map-stone / login / portal seed)
     * before shop-return re-poll 27/11.  Client map assets are warm; use a
     * short post-catalog 30/2 window (map-stone wait_wt6 rearm shape) instead
     * of remaining=8 — long standing drips can leave DF stuck (蓬莱 after
     * map-stone 2026-07-28).  Cold first catalog keeps the heavy window.
     */
    bool shopReturnLoadingClearLite;
    /*
     * mmShop→mmGame often emits 5/10 before shop-return 30/1.  A full team
     * roster there draws teammate HUD avatars on a shell that is not scene-
     * ready yet (null callback at 0x01014388).  Solo self 5/10 sets this;
     * finish_shop_return_rehydrate then queues 5/5 peer rows.
     */
    bool shopReturnTeamPeersPending;
    /*
     * Empty type-21 catalogs (npcnum=0) never reseed combat nodes via 27/11.
     * After shop-return loading clear finishes (or moveinfo proves the map is
     * live), arm a one-shot deferred 30/1 at the current role position so
     * ParseMinfoAndSpawnNPCs rebuilds kind-2.  Never put that 30/1 in the
     * crowded shop-return packet; never arm for NPC-rich scenes (30/1 drops
     * type-21 historically).
     */
    bool pendingShopReturnSceneEnter;
    u32 pendingShopReturnSceneEnterEarliestTick;
    char pendingShopReturnSceneEnterScene[64];
    u16 pendingShopReturnSceneEnterX;
    u16 pendingShopReturnSceneEnterY;
    /*
     * Leader scene transfer fans out a forced 30/1 to other seats.  Armed here
     * so auto-leave skips in-transit members and poll/dispatch can deliver the
     * enter on that client's own sticky session (not the leader's globals).
     */
    bool pendingTeamFollowSceneEnter;
    u32 pendingTeamFollowSceneEnterEarliestTick;
    char pendingTeamFollowSceneEnterScene[64];
    u16 pendingTeamFollowSceneEnterX;
    u16 pendingTeamFollowSceneEnterY;
    /* One kind-2 30/1 per mmShop exit; blocks re-arm after rehydrate loops. */
    bool shopReturnKind2Completed;
    /* After map-side revival outside the shop-return WT6/1 object budget,
     * deliver one login-shaped actorinfo on the next scene poll. */
    bool pendingMapActorVitalsSync;
    /*
     * Map-stone deferred 30/1 rebuilds mmGame without the title subtype-6
     * actor create path.  1/1/14 only refreshes an existing live actor
     * (HUD vitals); fresh-shell reseed must use 1/1/6 with actorinfo
     * (parse_actorinfo_response a2==0 / scene_runtime_init_and_sync).
     */
    bool pendingMapActorVitalsSyncFreshShell;
    /* Battle.cbm still owns the screen for several ticks after 4/8.  A poll
     * that fires too early can clear this flag while mmGame never applies
     * the 1/1/1, leaving the map HUD at HP=0. */
    u32 pendingMapActorVitalsSyncEarliestTick;
    u8 pendingMapActorVitalsSyncRemaining;
    /*
     * Battle death confirm may consume 801 inside a kind-4 terminal packet
     * that Battle.cbm tears down before kind-7 count sync runs.  Defer the
     * authoritative 7/11 remaining update to the next mmGame scene poll.
     */
    u16 pendingRevivalBagClearSeq;
    u32 pendingRevivalBagClearRemaining;
    /*
     * Battle.cbm death UI still owes 1/7/14 after the operate session is
     * disarmed on HP=0.  Keep this per-session until confirm or map revive.
     */
    bool awaitsBattleRevivalConfirm;
    bool taskPromptRefreshPending;
    char taskPromptRefreshScene[64];
    bool lastMoveinfoValid;
    u16 lastMoveinfoLen;
    u8 lastMoveinfoFormat;
    u8 lastMoveinfoBlob[512];
    u32 lastMoveinfoTick;
    bool pendingDirQueueValid;
    u16 pendingDirQueueLen;
    u16 pendingDirQueueStartX;
    u16 pendingDirQueueStartY;
    u16 pendingDirQueueEndX;
    u16 pendingDirQueueEndY;
    u8 pendingDirQueueBlob[32];
    u32 pendingDirQueueTick;
    u32 pendingDirQueueSerial;
    /* Server-time movement authority.  The client produces one direction
     * frame per 100 ms, but a sped-up client can upload those frames faster.
     * Credits are milliseconds rather than client ticks so only the service
     * clock can authorize spatial progress. */
    bool movementRateActive;
    u32 movementRateAnchorMs;
    u32 movementRateCreditMs;
    u32 movementRateViolationCount;
    u32 movementRateDeniedSteps;
    u32 movementRateLastViolationMs;
    /* Server-time battle-reward authority.  Battle enter packets must always
     * complete (hangup already sets client state=3); accelerators are limited
     * by suppressing EXP/gold/drops when settlements arrive too quickly. */
    bool battleRewardRateActive;
    u32 battleRewardLastMs;
    u32 battleRewardRateViolationCount;
    u32 battleRewardRateLastViolationMs;
    vm_mock_service_social_notice socialNotices[VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX];
    vm_mock_service_chat_notice chatNotices[VM_MOCK_SERVICE_CHAT_NOTICE_MAX];
    u8 chatNoticeHead;
    u8 chatNoticeCount;
    /*
     * Ten-pull / rapid 7/15 rares: coalesce same chest+reward into one world
     * line with count so tip「获得2个十四级玄晶」matches [世]祝贺.
     */
    u32 chestAnnounceRewardItemId;
    u32 chestAnnounceChestItemId;
    u32 chestAnnounceCount;
    u32 chestAnnounceFlushTick;
    bool systemWelcomeQueued;
    bool worldChatHistoryQueued;
    bool friendInviteReplyActive;
    u32 friendInviteSourceClientId;
    u32 friendInviteSourceRoleId;
    bool guildInviteReplyActive;
    u32 guildInviteSourceClientId;
    u32 guildInviteSourceRoleId;
    u32 guildInviteGuildId;
    bool tradeInviteReplyActive;
    u32 tradeInviteSourceClientId;
    u32 tradeInviteSourceRoleId;
    bool teamInviteReplyActive;
    u32 teamInviteSourceClientId;
    u32 teamInviteSourceWireId;
    bool sparInviteReplyActive;
    u32 sparInviteSourceClientId;
    u32 sparInviteSourceWireId;
    bool sparBattleReadyPending;
    u32 sparBattlePeerClientId;
    u32 sparBattlePeerWireId;
    /* After spar HP=0 / escape, keep redelivering the full Battle.cbm exit
     * sequence for late 4/2 until this tick so empty-ack cannot stick the UI. */
    u32 sparExitRedeliverUntilTick;
    u32 sparExitRecoverHp;
    u32 sparExitRecoverMp;
    bool sparExitWasDead;
    bool sparExitMessageQueued;
    /* Experiment: grant +1 durable EXP once so 4/7 is not a zero-exp delta. */
    bool sparExitExpGranted;
    /* True after spar-lose death penalty (same as PvE 1/7/14 ordinary path). */
    bool sparExitPenaltyApplied;
    /* Amounts applied by sparDeathPenalty — shown on 4/7 fdata / 1/3/3 tip. */
    u32 sparDeathExpPenalty;
    u32 sparDeathMoneyPenalty;
    /* After 4/8 tear-down, deliver 30/1 to nearest telestone for the loser. */
    bool sparDeathRespawnPending;
    char sparDeathRespawnScene[64];
    u16 sparDeathRespawnX;
    u16 sparDeathRespawnY;
    /* True after 4/7 settle delivered; next phase is 4/8+4/11+4/9. */
    bool sparSettleDelivered;
    /* Set on 4/8 tear-down; delivered after sparResultNotBeforeTick via
     * map 25/5 or scene-sync as 25/12 clear + 1/3/3 (no unsolicited 25/11). */
    bool sparResultMessageArmed;
    u32 sparResultNotBeforeTick;
    /* Pixi 比武擂台: 4/7 fdata + map tip「胜/败/平 积分+N 当前积分」。 */
    bool arenaResultPending;
    char arenaResultFdata[96];
    /* Last arena phase tip delivered to this client (ENTRY/LIVE/SETTLED). */
    u8 arenaPhaseTipStatus;
    /* SETTLED HUD (41/5 status=3) delivered once — stop idle poll body churn. */
    bool arenaHudSettlePushed;
    u32 pendingTeamBattleSerial;
    /* HandleChallengeResponse(0x010395AA) first consumes 30/9 and its
     * confirmation callback sends 30/10 {agree}.  Keep the target on the
     * service session because 30/10 carries no actor/enemy fields. */
    bool instanceChallengePending;
    bool instanceChallengeBattlePending;
    bool instanceChallengeDirectPending;
    /* Same-tick confirm HAS_FOLLOWUP may be ignored (临安); allow one retry
     * at age_ticks>=1 on a later event=7.  Cleared when client enters battle. */
    u8 instanceChallengeBattleWireCount;
    u32 instanceChallengeActorId;
    u32 instanceChallengeEnemyId;
    u16 instanceChallengeX;
    u16 instanceChallengeY;
    u32 instanceChallengeTick;
    char instanceChallengeScene[64];
    /* Quota key for challenge_daily_limit (target instance scene, else outdoor). */
    char instanceChallengeQuotaScene[64];
    /*
     * Pixi historically cleared 30/9 with agree=0 then sent hangup-style 4/1.
     * Keep a short arm so that path still charges challenge_daily_limit.
     */
    bool instanceChallengeHangupStyleArm;
    u32 instanceChallengeHangupStyleTick;
    /*
     * Instance-guide enter binds challenge_enemy_id to the target scene so
     * hangup / auto battle inside empty SCE stubs (e.g. 29梦境空间_01) can
     * select that enemy without automonster.dsh or map combat actors.
     */
    bool instanceHangupEnemyValid;
    u32 instanceHangupEnemyId;
    u32 instanceHangupActorId;
    char instanceHangupScene[64];
    /*
     * Last client-selected scene-monster live-node tuple from a successful
     * WT 4/1 challenge.  Hangup subtype-5 must reuse this (or an emulator live
     * scan); SCE combat-spawn ordinals are not the client's 25-row indices and
     * copy a null visual into battle units (JianghuOL.CBE:0x01004EA8).
     */
    bool lastSceneMonsterLiveValid;
    char lastSceneMonsterLiveScene[64];
    u32 lastSceneMonsterLiveActorId;
    u32 lastSceneMonsterLiveIndex;
    u32 lastSceneMonsterLiveX;
    u32 lastSceneMonsterLiveY;
    /*
     * NPC service buy: response is kind-26 only (clears busy).  7/7 is peeled by
     * mmGame:sub_11CE and can re-arm r9+21808 AFTER DispatchItemEvent clears it,
     * whether same-packet or same poll as 26/0.  Two poll phases:
     *   1 = deliver 7/7 (+7/11), 2 = deliver lone 26/0 after item path settles.
     */
    bool npcPurchaseBackpackPending;
    u8 npcPurchaseBackpackPhase;
    u16 npcPurchaseBackpackSeq;
    u32 npcPurchaseBackpackItemId;
    u32 npcPurchaseBackpackCount;
    u16 npcPurchaseBackpackEnhance;
    u32 npcPurchaseBackpackTick;
    /*
     * Mall flask buy (802/803): 14/3 alone does not leave a durable main-bag
     * row after mmShop→mmGame rebuild.  Peel 7/7 type=1 + 7/11 once mmGame is
     * sceneVisibleReady (NPC medicine contract).  Legacy shopFlaskLoadingClear*
     * was an mmShop-targeted lone 7/11 that dropped the new row.
     */
    bool shopFlaskLoadingClearPending; /* legacy; always cleared on arm */
    u16 shopFlaskLoadingClearSeq;
    u32 shopFlaskLoadingClearItemId;
    u32 shopFlaskLoadingClearReservoir;
    u32 shopFlaskLoadingClearTick;
    bool shopFlaskBackpackDeliverPending;
    u16 shopFlaskBackpackDeliverSeq;
    u32 shopFlaskBackpackDeliverItemId;
    u32 shopFlaskBackpackDeliverReservoir;
    u32 shopFlaskBackpackDeliverTick;
    /*
     * Map-stone deferred 30/1 EnterSceneByMapName is async.  A same-packet
     * 30/2 on the following 2/3 can ResetDownloadState before a later
     * ScreenInit (runtime: after resp=201 still caller=01018150), leaving
     * DF_DataPackage stuck.  Arm delayed poll 30/2 after 2/3; wait_wt6
     * follow-up rearms the tick; multi-shot covers late 27/12 ScreenInit.
     */
    bool mapStoneLoadingClearPending;
    u8 mapStoneLoadingClearRemaining;
    u32 mapStoneLoadingClearTick;
    char mapStoneLoadingClearScene[64];
    /*
     * Dream 29* only: after instance-exit to outdoor, require outdoor
     * map_stone_loading_clear to finish before reenter.  Non-29 paths ignore.
     */
    bool dreamExitReenterSettleRequired;
    /*
     * Dream HUD countdown contract (debug-0b451a):
     * client remain at obj+0x10 is in *seconds units* (MM:SS paint); poke/init
     * uses 60 so HUD shows 01:00.  Tick burns ~1 unit per wall minute, so the
     * server treats the same duration as **60 minutes** wall TTL and ejects
     * via instance-guide 31000 when it expires.
     *
     * Per-role: armed against dreamHudTtlRoleId so another character on the
     * same clientId (title rebind) cannot inherit or be ejected by this timer.
     */
    bool dreamHudTtlArmed;
    u32 dreamHudTtlRoleId;
    u32 dreamHudExpireAtMs;
    /* One-shot 1-minute remaining tip while dreamHudTtlArmed. */
    bool dreamHudWarnSent;
    /*
     * Dream 29* instance enter: next WT18/7 for 29* SCE must not return
     * uptodate — force a cold payload so DF uses 502* allocation instead of
     * intermittent hot 518c ParseMinfo (r6=01ffffff, debug-191169).
     */
    bool dreamForceColdSceResync;
    /*
     * mmShop→mmGame ScreenInit may still be settling when WT6/1 delivers
     * nonempty 27/11 (second DoLoading).  Same-packet 30/2 races that
     * ScreenInit (蓬莱 2026-07-27).  Bounded poll 30/2 owns ResetDownloadState;
     * after clears finish, lone 26/0 covers 5/10+7/7 re-arm of r9+21808.
     * Never 30/1, never 12s sustain.
     */
    bool shopReturnLoadingClearPending;
    u8 shopReturnLoadingClearRemaining;
    u32 shopReturnLoadingClearTick;
    u32 shopReturnLoadingClearArmTick;
    char shopReturnLoadingClearScene[64];
    bool shopReturnBusyAckPending;
    u32 shopReturnBusyAckTick;
    /* Last equipment shop list page, so VIEW→返回 restores selector/band/page. */
    bool npcShopBrowseValid;
    u8 npcShopBrowseSelector;
    u8 npcShopBrowseLevelBand;
    u16 npcShopBrowsePage;
    /* Warehouse list page for 存取后继续选择. */
    bool warehouseBrowseValid;
    u8 warehouseBrowseKind; /* 1=retrieve 2=deposit */
    u16 warehouseBrowsePage;
    /*
     * Mall pass 834: per-client arm + pending 26/1.  Must not be process-global —
     * another session offline cleared the old globals and made 取回/存入 show
     * 「请重新购买」while this bag still had durability (2026-07-28).
     */
    bool warehouseSessionArmed;
    bool warehouseDialogPending;
    u32 warehouseDialogTick;
    /* Mall pass 839: per-client arm + pending 26/1 (mirror warehouse 834). */
    bool equipSellSessionArmed;
    bool equipSellDialogPending;
    u32 equipSellDialogTick;
    bool equipSellBrowseValid;
    u16 equipSellBrowsePage;
    /*
     * Coolmall (商城按钮 → 26/1, no pass item, no mmShop browse).
     * Mirror 839 wire/poll; currency is wcoin / 酷宝 text.
     */
    bool coolmallSessionArmed;
    bool coolmallDialogPending;
    bool coolmallOpenWaitMap;
    u32 coolmallDialogTick;
    /*
     * mmShop exit peel-to-root: deliver title role-select parity
     * (1/1/6+1/1/15) on poll — bare 30/1 does not ScreenInit outdoor.
     */
    bool pendingMmshopExitRoleRelogin;
    u32 pendingMmshopExitRoleReloginTick;
    u8 coolmallBrowseSubtype; /* 5..13 mall page subtype */
    u16 coolmallBrowsePage;
    /* Skill trainer list page (26/1 OPEN_SKILLS_BASE | page). */
    u16 skillBrowsePage;
    /* After warehouse deposit, client main bag still holds the row until a
     * scene poll peel: flask/consumable mutation, or equipment 17/1+7/42
     * backpack-list push; then lone 26/0.  Enhance success also arms the
     * equipment 17/1 path (listOnly=1 skips the warehouse 26/0). */
    bool backpackListResyncPending;
    u8 backpackListResyncPhase; /* 1=peel/list 2=26/0 */
    bool backpackListResyncListOnly;
    u32 backpackListResyncTick;
    u16 backpackResyncRemoveSeq;
    u32 backpackResyncRemoveItemId;
    /*
     * 7/29 quote then 7/13 confirm (mmGame:0x4986 / CBE:0x0101A8A0).
     * repairnum is piece COUNT only — never copper cost (cost≈25455 was
     * mis-shown as「您身上有25455件装备」).  coolmoney is flat 5 酷宝.
     */
    bool quickRepairQuotePending;
    u8 quickRepairQuoteType;
    u16 quickRepairQuoteSeq;
    u32 quickRepairQuoteItemId;
    bool quickRepairQuoteHasItemId;
    u32 quickRepairQuoteTick;
    /*
     * After 7/13 result=1, equipment 7/7 is poll-deferred so it cannot re-arm
     * r9+21808 on the completion event.
     */
    bool quickRepairEquipSyncPending;
    u8 quickRepairEquipSyncPhase; /* 1=7/7-type2 2=26/0 */
    u32 quickRepairEquipSyncTick;
    /*
     * Login wear sync: bare actorinfo + 7/7 rows first; after ~2s drip one
     * simulated 7/8 per equipped slot, then system tip + full HP/MP live 1/1/14.
     */
    bool loginEquipWearSyncPending;
    u32 loginEquipWearSyncEarliestTick;
    u8 loginEquipWearSyncNextSlot; /* 0..SLOT_COUNT, then finalize */
    u8 loginEquipWearSyncRows;
    /* Practise panel (修炼信息): 7/21 opengold result is stored as the client
     * isgold byte (JianghuOL.CBE:0x0102CBFE).  Session-scoped until offline
     * cultivation hours are modeled in the role DB. */
    u8 practiseIsGold;
    /*
     * Auto / hangup playback timers: session is the sole durable authority for
     * the connected client (solo and team-member seats).  Process globals are
     * request scratch only.  account_state keeps legacy fields for one-shot
     * migrate on first seed, then capture zeros them (phase D 2026-08-01).
     */
    bool battleAutoTimersSeeded;
    u8 mockBattleAutoPrefer;
    u8 mockBattleAutoPreferNoHangup;
    u8 mockBattleAutoPendingArmed;
    u32 mockBattleAutoPendingNotBeforeTick;
    u32 mockBattleAutoNextActNotBeforeMs;
    u8 mockBattleLastRoundActionCount;
    u8 mockBattleAutoFlagPendingArmed;
    u32 mockBattleAutoFlagPendingNotBeforeMs;
    u8 mockBattleAutoHangupStyleFlagOk;
    u8 mockBattleAutoSuppressNext12;
    u8 mockHangupLoopActive;
    u8 mockHangupLoopScheduleAfterExit;
    u8 mockHangupLoopPendingArmed;
    u32 mockHangupLoopNotBeforeMs;
    u8 mockHangupStartPendingArmed;
    u32 mockHangupStartNotBeforeMs;
    u8 mockHangupStopAfterBattle;
    /* The native action=4 task path carries only task_id after the NPC dialog.
     * Retain the server-observed offer source so a completed task cannot be
     * reaccepted by forging the later 6/11 request. */
    vm_mock_service_task_offer_context
        taskOfferContexts[VM_MOCK_SERVICE_TASK_OFFER_CONTEXT_MAX];
    char scenePendingScene[64];
    vm_mock_service_peer_sync peerSync[VM_MOCK_SERVICE_PEER_SYNC_MAX];
    struct vm_mock_service_client_session *next;
} vm_mock_service_client_session;

static vm_mock_service_client_session *vm_mock_service_get_active_client_session(void);
static void vm_mock_service_session_arm_map_actor_vitals_sync(
    vm_mock_service_client_session *session,
    u16 bagClearSeq,
    u32 bagClearRemaining,
    bool freshShellEnter);

static bool vm_mock_service_session_dream_force_cold_sce(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    return session != NULL && session->dreamForceColdSceResync;
}

static void vm_mock_service_session_dream_force_cold_sce_clear(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    if (session != NULL)
        session->dreamForceColdSceResync = false;
}

/*
 * Map shell the active client session still owns for nearby / NPC / map-stone
 * follow-ups.  role->scene can lag or be restored from a stale account snapshot
 * while sceneVisible* on the connection remains the EnterScene authority
 * (map-stone 临安→桃花岛 type27 delivering 临安 catalog, 2026-08-01).
 */
static const char *vm_net_mock_client_visible_scene_name(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session != NULL &&
        vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) &&
        (session->sceneVisibleReady || session->sceneVisiblePending))
    {
        return session->sceneVisibleScene;
    }
    return vm_net_mock_current_scene_name();
}

static vm_mock_service_client_session *g_vm_mock_service_client_sessions = NULL;

/*
 * A team is deliberately service-local rather than persisted in the role DB.
 * The original client receives membership changes as online 1/5 packets and
 * clears the roster when a member disconnects, so retaining a stale offline
 * party across a service restart would only create phantom HUD rows.
 */
typedef struct
{
    bool active;
    u32 leaderClientId;
    u8 memberCount;
    u32 memberClientIds[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    bool battleActive;
    u32 battleSerial;
    u32 intimacyRewardedSerial;
    /* Latch ring PvE kill credit once per team battleSerial (shared party). */
    u32 ringKillRewardedSerial;
    u32 battleLeaderClientId;
    u32 battleEnemyId;
    u32 battleSceneMonsterIndex;
    u32 battleSceneMonsterX;
    u32 battleSceneMonsterY;
    u8 battleMonsterCount;
    u8 battleSide;
    u8 battleMemberCount;
    u32 battleMemberClientIds[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    char battleScene[64];
    bool battleFinished;
    u32 battleTurnCounter;
    u32 battleEnemyHpSlots[3];
    u32 battleEnemyHpMaxSlots[3];
    u32 battleEnemyHpCurrent;
    u32 battleEnemyHpMax;
    u32 battleMemberHp[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u32 battleMemberHpMax[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u32 battleMemberMp[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u32 battleMemberMpMax[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    vm_net_mock_battle_stat_modifier
        battleMemberModifiers[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    /* Shared enemy DoT / silence / debuff — must not live only in per-account
     * globals or each seat gets a private “先手” copy of 毒/沉默/破甲. */
    vm_net_mock_battle_enemy_ailment battleEnemyAilments[3];
    u8 battleRoundActedMask;
    /* Members who successfully fled (or otherwise left) while the party fight
     * continues.  They stay in the frozen battleMember* roster for teaminfo
     * row count, but must not block the alive-mask turn barrier. */
    u8 battleMemberLeftMask;
    /*
     * Round-open deadline (scheduler ticks).  0 = unarmed / disabled.  Lazy-
     * armed when a living seat still owes an action; idle seats are filled
     * with Operate=0 after CBE_TEAM_BATTLE_ROUND_TIMEOUT_MS (default 20s).
     */
    u32 battleRoundActDeadlineTick;
    u32 battleRoundSerial;
    bool battleRoundTerminalPending;
    /*
     * Member index of the seat whose captured 4/6 first wiped all enemies.
     * Merge playback is roster-ordered: seats after this index must not append
     * their baked attacks (would animate into already-dead targets / 打空气).
     * 0xff = unset. Evidence: docs/re/2026-08-21-team-battle-post-wipe-air-swing.md
     */
    u8 battleRoundTerminalActorIndex;
    u32 battleRoundActionSerial;
    vm_mock_service_team_battle_round_action
        battleRoundActions[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u32 battleActionSerial;
    vm_mock_service_team_battle_event battleEvents[VM_MOCK_SERVICE_TEAM_BATTLE_EVENT_MAX];
} vm_mock_service_team;

/* A spar is service-local and intentionally keeps its combat HP/MP separate
 * from durable role HP/MP.  A friendly duel must not leave either player dead
 * or consume persistent MP after the battle screen closes. */
typedef struct
{
    bool active;
    bool finished;
    u32 serial;
    u32 clientIds[2];
    char scene[64];
    u32 hp[2];
    u32 hpMax[2];
    u32 mp[2];
    u32 mpMax[2];
    /* Frozen at duel_begin from durable role (fallback: session presence).
     * Subtype-10 battleinfo left-row visual bytes are visual_group=sexGroup,
     * visual_variant=jobIndex; HandleBattleStartMsg passes them to
     * sub_23F6(variant, group). */
    u8 jobIndex[2];
    u8 sexGroup[2];
    /* Battle-local timed buffs (金刚不坏/神臂担山); never touch durable roles. */
    vm_net_mock_battle_stat_modifier modifiers[2];
    /* 神堂静默 etc.: blocks skill operate while >0 (aged each duel round). */
    u8 silenceRounds[2];
    u8 startPendingMask;
    u8 startedMask;
    /* firstTurnIndex: agility winner — refreshed each resolved round (seat
     * modifiers included); duel_begin seeds the initial value for logs only.
     * Playback order applies after both seats commit Operate (or timeout NA). */
    u8 firstTurnIndex;
    u8 roundCommitMask;
    u32 roundCommitOperate[2];
    /* Item/flask commit (4/3): heal amounts applied on round resolve. */
    u8 roundCommitIsItem[2];
    u32 roundCommitItemHealHp[2];
    u32 roundCommitItemHealMp[2];
    u32 roundCommitDeadlineTick;
    u8 terminalPendingMask;
    u32 terminalNotBeforeTick;
    /* Keep the finished duel briefly so late client 4/2 after 4/11+4/9
     * still empty-acks here instead of falling into PvE settle. */
    u32 releaseNotBeforeTick;
    u32 actionSerial;
    vm_mock_service_duel_event events[VM_MOCK_SERVICE_DUEL_EVENT_MAX];
    /* Pixi 比武擂台: skip spar death/token rewards; score via arena settle. */
    bool arenaMode;
    bool arenaSettled;
    u32 arenaStartTick;
    /* Arena-only: completed resolve rounds; damage/hit scale 2^n. */
    u16 resolvedRoundCount;
    /* Pixi 赏金擂台: stake settle via wager; same death exemption as arena. */
    bool wagerMode;
    bool wagerSettled;
    u32 wagerChallengeId;
    /*
     * Friendly 切磋 (invite/consent): no death penalty, PK, enmity, substitute
     * doll, or initiate cooldown. Ring kill-player progress credits on win.
     * Force-PK keeps false (no ring credit).
     */
    bool friendlyMode;
} vm_mock_service_duel;

static vm_mock_service_team g_vm_mock_service_teams[VM_MOCK_SERVICE_TEAM_MAX];
static vm_mock_service_duel g_vm_mock_service_duels[VM_MOCK_SERVICE_DUEL_MAX];
static u32 g_vm_mock_service_duel_serial = 0;
static vm_mock_service_trade g_vm_mock_service_trades[VM_MOCK_SERVICE_TRADE_MAX];

enum
{
    /* 300 ticks × 100ms = 30s scene-poll / gateway keepalive budget. */
    VM_MOCK_SERVICE_ONLINE_PRESENCE_MAX_AGE_TICKS = 300,
    VM_MOCK_SERVICE_SESSION_MOVEINFO_MAX = 512
};

enum
{
    VM_MOCK_SERVICE_MOVEINFO_FORMAT_NONE = 0,
    VM_MOCK_SERVICE_MOVEINFO_FORMAT_RESPONSE_ENTRY = 1,
    VM_MOCK_SERVICE_MOVEINFO_FORMAT_TIMELINE = 2,
    VM_MOCK_SERVICE_MOVEINFO_FORMAT_OPAQUE_SMALL = 3
};

static void vm_mock_service_account_state_init(vm_mock_service_account_state *state, const char *accountId)
{
    if (state == NULL)
        return;
    memset(state, 0, sizeof(*state));
    if (accountId && accountId[0] != 0)
        snprintf(state->accountId, sizeof(state->accountId), "%s", accountId);
    state->mockBattleEnemyCountCurrent = 1;
    state->lastSceneChangeFb4Type = 1;
}

static void vm_mock_service_account_clear_auto_timers(vm_mock_service_account_state *state)
{
    if (state == NULL)
        return;
    state->mockBattleAutoPrefer = 0;
    state->mockBattleAutoPreferNoHangup = 0;
    state->mockBattleAutoPendingArmed = 0;
    state->mockBattleAutoPendingNotBeforeTick = 0;
    state->mockBattleAutoNextActNotBeforeMs = 0;
    state->mockBattleLastRoundActionCount = 0;
    state->mockBattleAutoFlagPendingArmed = 0;
    state->mockBattleAutoFlagPendingNotBeforeMs = 0;
    state->mockBattleAutoHangupStyleFlagOk = 0;
    state->mockBattleAutoClientDriven = 0;
    state->mockBattleAutoSuppressNext12 = 0;
    state->mockHangupLoopActive = 0;
    state->mockHangupLoopScheduleAfterExit = 0;
    state->mockHangupLoopPendingArmed = 0;
    state->mockHangupLoopNotBeforeMs = 0;
    state->mockHangupStartPendingArmed = 0;
    state->mockHangupStartNotBeforeMs = 0;
    state->mockHangupStopAfterBattle = 0;
}

static void vm_mock_service_session_clear_auto_timers(vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->battleAutoTimersSeeded = true;
    session->mockBattleAutoPrefer = 0;
    session->mockBattleAutoPreferNoHangup = 0;
    session->mockBattleAutoPendingArmed = 0;
    session->mockBattleAutoPendingNotBeforeTick = 0;
    session->mockBattleAutoNextActNotBeforeMs = 0;
    session->mockBattleLastRoundActionCount = 0;
    session->mockBattleAutoFlagPendingArmed = 0;
    session->mockBattleAutoFlagPendingNotBeforeMs = 0;
    session->mockBattleAutoHangupStyleFlagOk = 0;
    session->mockBattleAutoSuppressNext12 = 0;
    session->mockHangupLoopActive = 0;
    session->mockHangupLoopScheduleAfterExit = 0;
    session->mockHangupLoopPendingArmed = 0;
    session->mockHangupLoopNotBeforeMs = 0;
    session->mockHangupStartPendingArmed = 0;
    session->mockHangupStartNotBeforeMs = 0;
    session->mockHangupStopAfterBattle = 0;
}

static void vm_mock_service_session_store_auto_timers(vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->battleAutoTimersSeeded = true;
    session->mockBattleAutoPrefer = g_mockBattleAutoPrefer;
    session->mockBattleAutoPreferNoHangup = g_mockBattleAutoPreferNoHangup;
    session->mockBattleAutoPendingArmed = g_mockBattleAutoPendingArmed;
    session->mockBattleAutoPendingNotBeforeTick = g_mockBattleAutoPendingNotBeforeTick;
    session->mockBattleAutoNextActNotBeforeMs = g_mockBattleAutoNextActNotBeforeMs;
    session->mockBattleLastRoundActionCount = g_mockBattleLastRoundActionCount;
    session->mockBattleAutoFlagPendingArmed = g_mockBattleAutoFlagPendingArmed;
    session->mockBattleAutoFlagPendingNotBeforeMs =
        g_mockBattleAutoFlagPendingNotBeforeMs;
    session->mockBattleAutoHangupStyleFlagOk = g_mockBattleAutoHangupStyleFlagOk;
    session->mockBattleAutoSuppressNext12 = g_mockBattleAutoSuppressNext12;
    session->mockHangupLoopActive = g_mockHangupLoopActive;
    session->mockHangupLoopScheduleAfterExit = g_mockHangupLoopScheduleAfterExit;
    session->mockHangupLoopPendingArmed = g_mockHangupLoopPendingArmed;
    session->mockHangupLoopNotBeforeMs = g_mockHangupLoopNotBeforeMs;
    session->mockHangupStartPendingArmed = g_mockHangupStartPendingArmed;
    session->mockHangupStartNotBeforeMs = g_mockHangupStartNotBeforeMs;
    session->mockHangupStopAfterBattle = g_mockHangupStopAfterBattle;
}

static void vm_mock_service_session_load_auto_timers(
    vm_mock_service_client_session *session,
    vm_mock_service_account_state *legacyAccount)
{
    u8 migrated = 0;

    if (session == NULL)
    {
        if (legacyAccount == NULL)
            return;
        g_mockBattleAutoPrefer = legacyAccount->mockBattleAutoPrefer;
        g_mockBattleAutoPreferNoHangup = legacyAccount->mockBattleAutoPreferNoHangup;
        g_mockBattleAutoPendingArmed = legacyAccount->mockBattleAutoPendingArmed;
        g_mockBattleAutoPendingNotBeforeTick =
            legacyAccount->mockBattleAutoPendingNotBeforeTick;
        g_mockBattleAutoNextActNotBeforeMs =
            legacyAccount->mockBattleAutoNextActNotBeforeMs;
        g_mockBattleLastRoundActionCount =
            legacyAccount->mockBattleLastRoundActionCount;
        g_mockBattleAutoFlagPendingArmed =
            legacyAccount->mockBattleAutoFlagPendingArmed;
        g_mockBattleAutoFlagPendingNotBeforeMs =
            legacyAccount->mockBattleAutoFlagPendingNotBeforeMs;
        g_mockBattleAutoHangupStyleFlagOk =
            legacyAccount->mockBattleAutoHangupStyleFlagOk;
        g_mockBattleAutoClientDriven = 0;
        g_mockBattleAutoSuppressNext12 = legacyAccount->mockBattleAutoSuppressNext12;
        g_mockHangupLoopActive = legacyAccount->mockHangupLoopActive;
        g_mockHangupLoopScheduleAfterExit =
            legacyAccount->mockHangupLoopScheduleAfterExit;
        g_mockHangupLoopPendingArmed = legacyAccount->mockHangupLoopPendingArmed;
        g_mockHangupLoopNotBeforeMs = legacyAccount->mockHangupLoopNotBeforeMs;
        g_mockHangupStartPendingArmed = legacyAccount->mockHangupStartPendingArmed;
        g_mockHangupStartNotBeforeMs = legacyAccount->mockHangupStartNotBeforeMs;
        g_mockHangupStopAfterBattle = legacyAccount->mockHangupStopAfterBattle;
        return;
    }

    if (!session->battleAutoTimersSeeded && legacyAccount != NULL)
    {
        session->mockBattleAutoPrefer = legacyAccount->mockBattleAutoPrefer;
        session->mockBattleAutoPreferNoHangup =
            legacyAccount->mockBattleAutoPreferNoHangup;
        session->mockBattleAutoPendingArmed = legacyAccount->mockBattleAutoPendingArmed;
        session->mockBattleAutoPendingNotBeforeTick =
            legacyAccount->mockBattleAutoPendingNotBeforeTick;
        session->mockBattleAutoNextActNotBeforeMs =
            legacyAccount->mockBattleAutoNextActNotBeforeMs;
        session->mockBattleLastRoundActionCount =
            legacyAccount->mockBattleLastRoundActionCount;
        session->mockBattleAutoFlagPendingArmed =
            legacyAccount->mockBattleAutoFlagPendingArmed;
        session->mockBattleAutoFlagPendingNotBeforeMs =
            legacyAccount->mockBattleAutoFlagPendingNotBeforeMs;
        session->mockBattleAutoHangupStyleFlagOk =
            legacyAccount->mockBattleAutoHangupStyleFlagOk;
        session->mockBattleAutoSuppressNext12 =
            legacyAccount->mockBattleAutoSuppressNext12;
        session->mockHangupLoopActive = legacyAccount->mockHangupLoopActive;
        session->mockHangupLoopScheduleAfterExit =
            legacyAccount->mockHangupLoopScheduleAfterExit;
        session->mockHangupLoopPendingArmed =
            legacyAccount->mockHangupLoopPendingArmed;
        session->mockHangupLoopNotBeforeMs = legacyAccount->mockHangupLoopNotBeforeMs;
        session->mockHangupStartPendingArmed =
            legacyAccount->mockHangupStartPendingArmed;
        session->mockHangupStartNotBeforeMs =
            legacyAccount->mockHangupStartNotBeforeMs;
        session->mockHangupStopAfterBattle = legacyAccount->mockHangupStopAfterBattle;
        session->battleAutoTimersSeeded = true;
        vm_mock_service_account_clear_auto_timers(legacyAccount);
        migrated = 1;
    }
    else if (!session->battleAutoTimersSeeded)
    {
        session->battleAutoTimersSeeded = true;
    }

    g_mockBattleAutoPrefer = session->mockBattleAutoPrefer;
    g_mockBattleAutoPreferNoHangup = session->mockBattleAutoPreferNoHangup;
    g_mockBattleAutoPendingArmed = session->mockBattleAutoPendingArmed;
    g_mockBattleAutoPendingNotBeforeTick = session->mockBattleAutoPendingNotBeforeTick;
    g_mockBattleAutoNextActNotBeforeMs = session->mockBattleAutoNextActNotBeforeMs;
    g_mockBattleLastRoundActionCount = session->mockBattleLastRoundActionCount;
    g_mockBattleAutoFlagPendingArmed = session->mockBattleAutoFlagPendingArmed;
    g_mockBattleAutoFlagPendingNotBeforeMs =
        session->mockBattleAutoFlagPendingNotBeforeMs;
    g_mockBattleAutoHangupStyleFlagOk = session->mockBattleAutoHangupStyleFlagOk;
    g_mockBattleAutoClientDriven = 0;
    g_mockBattleAutoSuppressNext12 = session->mockBattleAutoSuppressNext12;
    g_mockHangupLoopActive = session->mockHangupLoopActive;
    g_mockHangupLoopScheduleAfterExit = session->mockHangupLoopScheduleAfterExit;
    g_mockHangupLoopPendingArmed = session->mockHangupLoopPendingArmed;
    g_mockHangupLoopNotBeforeMs = session->mockHangupLoopNotBeforeMs;
    g_mockHangupStartPendingArmed = session->mockHangupStartPendingArmed;
    g_mockHangupStartNotBeforeMs = session->mockHangupStartNotBeforeMs;
    g_mockHangupStopAfterBattle = session->mockHangupStopAfterBattle;

    if (migrated != 0 ||
        g_mockBattleAutoPrefer != 0 ||
        g_mockHangupLoopActive != 0 ||
        g_mockBattleAutoPendingArmed != 0)
    {
        printf("[info][mock-service] auto_timers_session_load client=%08x "
               "prefer=%u pending=%u next_act_ms=%u hangup=%u migrated=%u "
               "evidence=phase-d-session-authority\n",
               session->clientId,
               g_mockBattleAutoPrefer ? 1 : 0,
               g_mockBattleAutoPendingArmed ? 1 : 0,
               g_mockBattleAutoNextActNotBeforeMs,
               g_mockHangupLoopActive ? 1 : 0,
               migrated ? 1 : 0);
    }
}

/*
 * Globals mirror this account only when both the sticky pointer and the
 * active account id string agree.  Request teardown NULLs active_account_id
 * while leaving globals_account set for sticky reuse; temporary account
 * switches must never capture roleDb into the wrong account_state.
 */
static bool vm_mock_service_account_globals_mirror(
    const vm_mock_service_account_state *state)
{
    if (state == NULL || state->accountId[0] == 0)
        return false;
    if (g_vm_mock_service_globals_account != state)
        return false;
    if (g_vm_mock_service_active_account_id == NULL ||
        g_vm_mock_service_active_account_id[0] == 0 ||
        vm_mock_service_account_id_equal(g_vm_mock_service_active_account_id,
                                         state->accountId) == false)
    {
        return false;
    }
    return true;
}

static void vm_mock_service_account_capture(vm_mock_service_account_state *state)
{
    vm_mock_service_client_session *session = NULL;
    int agentRoleDbSkip = 0;
    bool globalsMirror = false;

    if (state == NULL)
        return;
    globalsMirror = vm_mock_service_account_globals_mirror(state);

    state->netMockSplitProbe = g_netMockSplitProbe;
    state->netMockUpdateDelivered = g_netMockUpdateDelivered;
    state->netMockEnterGameOffset = g_netMockEnterGameOffset;
    state->netMockEnterGameChecksum = g_netMockEnterGameChecksum;

    state->pendingSceneSaveValid = g_vm_net_mock_pending_scene_save_valid;
    memcpy(state->pendingSceneSaveScene, g_vm_net_mock_pending_scene_save_scene,
           sizeof(state->pendingSceneSaveScene));
    memcpy(state->pendingSceneSaveReason, g_vm_net_mock_pending_scene_save_reason,
           sizeof(state->pendingSceneSaveReason));
    state->pendingSceneSaveX = g_vm_net_mock_pending_scene_save_x;
    state->pendingSceneSaveY = g_vm_net_mock_pending_scene_save_y;

    state->mockBattleOperateSessionSerial = g_mockBattleOperateSessionSerial;
    memcpy(state->mockBattleMonsterHealUsedSerialSlots,
           g_mockBattleMonsterHealUsedSerialSlots,
           sizeof(state->mockBattleMonsterHealUsedSerialSlots));
    state->mockBattleOperateTurnCounter = g_mockBattleOperateTurnCounter;
    state->mockBattleOperateSessionArmed = g_mockBattleOperateSessionArmed;
    state->mockBattleAwaitsRevivalConfirm = g_mockBattleAwaitsRevivalConfirm;
    state->mockBattleOperateSessionFinished = g_mockBattleOperateSessionFinished;
    state->mockBattlePendingEnemyTurn = g_mockBattlePendingEnemyTurn;
    state->mockBattleAwaitingSettlement = g_mockBattleAwaitingSettlement;
    state->mockBattleSettlementExitPending = g_mockBattleSettlementExitPending;
    state->mockBattleSettlementExitNotBeforeMs =
        g_mockBattleSettlementExitNotBeforeMs;
    state->mockBattleEncounterNotBeforeMs = g_mockBattleEncounterNotBeforeMs;
    state->mockBattleEncounterCooldownClearPending =
        g_mockBattleEncounterCooldownClearPending;
    state->mockBattlePostExitSuppressSceneDefaultUntilMs =
        g_mockBattlePostExitSuppressSceneDefaultUntilMs;
    state->mockBattlePostExitSettlePending = g_mockBattlePostExitSettlePending;
    state->mockBattlePostExitSettleNotBeforeMs =
        g_mockBattlePostExitSettleNotBeforeMs;
    state->mockBattleSceneMonsterStartActive = g_mockBattleSceneMonsterStartActive;
    state->mockBattleStartUsesSceneWireMaps = g_mockBattleStartUsesSceneWireMaps;
    state->mockBattleLastOperateValid = g_mockBattleLastOperateValid;
    state->mockBattleLastOperate = g_mockBattleLastOperate;
    state->mockBattleLastIndex = g_mockBattleLastIndex;
    /*
     * Auto/hangup timers: session authority.  Store globals onto the active
     * session and keep account_state fields zero so restore cannot resurrect
     * a second clock (phase D).  No session → legacy account stash.
     */
    session = vm_mock_service_get_active_client_session();
    if (session != NULL)
    {
        vm_mock_service_session_store_auto_timers(session);
        vm_mock_service_account_clear_auto_timers(state);
    }
    else
    {
        state->mockBattleAutoPrefer = g_mockBattleAutoPrefer;
        state->mockBattleAutoPreferNoHangup = g_mockBattleAutoPreferNoHangup;
        state->mockBattleAutoPendingArmed = g_mockBattleAutoPendingArmed;
        state->mockBattleAutoPendingNotBeforeTick = g_mockBattleAutoPendingNotBeforeTick;
        state->mockBattleAutoNextActNotBeforeMs = g_mockBattleAutoNextActNotBeforeMs;
        state->mockBattleLastRoundActionCount = g_mockBattleLastRoundActionCount;
        state->mockBattleAutoFlagPendingArmed = g_mockBattleAutoFlagPendingArmed;
        state->mockBattleAutoFlagPendingNotBeforeMs =
            g_mockBattleAutoFlagPendingNotBeforeMs;
        state->mockBattleAutoHangupStyleFlagOk = g_mockBattleAutoHangupStyleFlagOk;
        state->mockBattleAutoClientDriven = 0;
        state->mockBattleAutoSuppressNext12 = g_mockBattleAutoSuppressNext12;
        state->mockHangupLoopActive = g_mockHangupLoopActive;
        state->mockHangupLoopScheduleAfterExit = g_mockHangupLoopScheduleAfterExit;
        state->mockHangupLoopPendingArmed = g_mockHangupLoopPendingArmed;
        state->mockHangupLoopNotBeforeMs = g_mockHangupLoopNotBeforeMs;
        state->mockHangupStartPendingArmed = g_mockHangupStartPendingArmed;
        state->mockHangupStartNotBeforeMs = g_mockHangupStartNotBeforeMs;
        state->mockHangupStopAfterBattle = g_mockHangupStopAfterBattle;
    }
    state->mockBattleRoleHpCurrent = g_mockBattleRoleHpCurrent;
    state->mockBattleRoleHpMax = g_mockBattleRoleHpMax;
    state->mockBattleRoleMpCurrent = g_mockBattleRoleMpCurrent;
    state->mockBattleRoleMpMax = g_mockBattleRoleMpMax;
    state->mockBattleEnemyCountCurrent = g_mockBattleEnemyCountCurrent;
    memcpy(state->mockBattleEnemyIdSlots, g_mockBattleEnemyIdSlots,
           sizeof(state->mockBattleEnemyIdSlots));
    memcpy(state->mockBattleEnemyJobSlots, g_mockBattleEnemyJobSlots,
           sizeof(state->mockBattleEnemyJobSlots));
    memcpy(state->mockBattleEnemyHpSlots, g_mockBattleEnemyHpSlots, sizeof(state->mockBattleEnemyHpSlots));
    memcpy(state->mockBattleEnemyHpMaxSlots, g_mockBattleEnemyHpMaxSlots, sizeof(state->mockBattleEnemyHpMaxSlots));
    state->mockBattleEnemyHpCurrent = g_mockBattleEnemyHpCurrent;
    state->mockBattleEnemyHpMax = g_mockBattleEnemyHpMax;
    memcpy(state->mockBattleEnemyAilments, g_mockBattleEnemyAilments,
           sizeof(state->mockBattleEnemyAilments));
    state->mockBattleSoloModifier = g_vm_net_mock_battle_solo_modifier;

    state->netMockTitleServerListPending = g_netMockTitleServerListPending;
    state->netMockTitleServerSelectConfirmed = g_netMockTitleServerSelectConfirmed;
    state->netMockBackpackGridSeededRoleId = g_netMockBackpackGridSeededRoleId;
    state->netMockShop17ListPending = g_netMockShop17ListPending;
    state->netMockTitleServerListTick = g_netMockTitleServerListTick;
    state->netMockTitleServerSelectTick = g_netMockTitleServerSelectTick;
    state->netMockTitleSelectedServerId = g_netMockTitleSelectedServerId;
    state->netMockBackpackPreferRoleListAfterShopBuy = g_netMockBackpackPreferRoleListAfterShopBuy;
    state->updateCompletedReenterPending = g_vm_net_mock_update_completed_reenter_pending;
    memcpy(state->updateCompletedName, g_vm_net_mock_update_completed_name, sizeof(state->updateCompletedName));

    state->rolePositionDirty = g_vm_net_mock_role_position_dirty;
    state->roleInventoryDirty = g_vm_net_mock_role_inventory_dirty;
    /*
     * roleDb ownership: never copy globals into account_state unless the
     * process mirror is this account (pointer + active id).  Cross-account
     * temporary restore that forgot to switch back used to seal another
     * player's roles into this heap cache (admin/client then showed the
     * wrong roleId until process restart; MySQL account_roles stayed correct).
     *
     * Skip memcpy only when globals still mirror this account and no dirty
     * bit is set.  Sync MySQL paths call note_durable_role_write first so
     * account roleDb already matches the committed authority.
     */
    if (!globalsMirror)
    {
        printf("[error][mock-service] role_db_capture rejected account=%s "
               "globals=%s active_id=%s evidence=cross-account-roledb-guard\n",
               state->accountId[0] ? state->accountId : "-",
               g_vm_mock_service_globals_account
                   ? g_vm_mock_service_globals_account->accountId
                   : "-",
               g_vm_mock_service_active_account_id
                   ? g_vm_mock_service_active_account_id
                   : "-");
    }
    else if (state->rolePositionDirty || state->roleInventoryDirty ||
             !state->roleDbLoaded || !g_vm_net_mock_role_db_loaded)
    {
        state->roleDb = g_vm_net_mock_role_db;
        state->roleDbLoaded = g_vm_net_mock_role_db_loaded;
        state->roleDbValid = g_vm_net_mock_role_db_valid;
    }
    else
    {
        agentRoleDbSkip = 1;
        printf("[info][mock-service] role_db_capture skipped=1 account=%s "
               "pos_dirty=%u inv_dirty=%u evidence=sticky-restore\n",
               state->accountId[0] ? state->accountId : "-",
               state->rolePositionDirty ? 1u : 0u,
               state->roleInventoryDirty ? 1u : 0u);
    }
    (void)agentRoleDbSkip;
    if (state->persistFlushBusy &&
        (state->rolePositionDirty || state->roleInventoryDirty))
    {
        state->persistReflushWanted = true;
        state->persistDirtySerial += 1u;
        if (state->persistDirtySerial == 0u)
            state->persistDirtySerial = 1u;
    }
    /* session_mark_offline clears the process warehouse loaded flag.  Do not
     * let that empty shell overwrite a previously captured account snapshot. */
    if (g_vm_net_mock_warehouse.loaded &&
        g_vm_net_mock_warehouse.accountId[0] != 0 &&
        vm_mock_service_account_id_equal(g_vm_net_mock_warehouse.accountId,
                                         state->accountId))
    {
        state->warehouse = g_vm_net_mock_warehouse;
    }

    state->battleRewardedSerial = g_vm_net_mock_battle_rewarded_serial;
    state->battleRewardedExp = g_vm_net_mock_battle_rewarded_exp;
    memcpy(state->battleRewardedDrops, g_vm_net_mock_battle_rewarded_drops,
           sizeof(state->battleRewardedDrops));
    state->battleRewardedDropResultCount =
        g_vm_net_mock_battle_rewarded_drop_result_count;
    state->battleEnemyIdCurrent = g_vm_net_mock_battle_enemy_id_current;
    state->battleRoleIdCurrent = g_vm_net_mock_battle_role_id_current;
    state->battleRewardRng = g_vm_net_mock_battle_reward_rng;
    state->battleSettlementSentSerial = g_vm_net_mock_battle_settlement_sent_serial;
    state->battleDropRefreshSentSerial = g_vm_net_mock_battle_drop_refresh_sent_serial;
    state->battleRecoveredSerial = g_vm_net_mock_battle_recovered_serial;
    state->battleRewardRateSuppressedSerial =
        g_vm_net_mock_battle_reward_rate_suppressed_serial;
    state->mockBattleSettleWireRecoverHp = g_mockBattleSettleWireRecoverHp;
    state->mockBattleSettleWireRecoverMp = g_mockBattleSettleWireRecoverMp;
    state->offlinePractiseLoginFlag = vm_net_mock_role_offline_practise_login_flag();
    snprintf(state->offlinePractiseLoginInfo, sizeof(state->offlinePractiseLoginInfo),
             "%s", vm_net_mock_role_offline_practise_login_info());
    state->remoteCompletedSceneTargetSerial =
        g_vm_net_mock_remote_completed_scene_target_serial;

    memcpy(state->sceneMoveinfoNpcPendingScene, g_vm_net_mock_scene_moveinfo_npc_pending_scene,
           sizeof(state->sceneMoveinfoNpcPendingScene));
    state->sceneMoveinfoNpcPending = g_vm_net_mock_scene_moveinfo_npc_pending;
    state->sceneMoveinfoNpcWaitPostEnter = g_vm_net_mock_scene_moveinfo_npc_wait_post_enter;
    state->sceneMoveinfoNpcWaitWt6 = g_vm_net_mock_scene_moveinfo_npc_wait_wt6;
    memcpy(state->sceneMoveinfoNpcSeededScene, g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
           sizeof(state->sceneMoveinfoNpcSeededScene));
    state->sceneMoveinfoNpcSeeded = g_vm_net_mock_scene_moveinfo_npc_seeded;

    state->lastSceneChangeTarget = g_vm_net_mock_last_scene_change_target;
    state->lastSceneChangeTargetValid = g_vm_net_mock_last_scene_change_target_valid;
    state->lastSceneChangeTargetSerial = g_vm_net_mock_last_scene_change_target_serial;
    state->teleportStoneSubtype3AckSent = g_vm_net_mock_teleport_stone_subtype3_ack_sent;
    state->teleportStoneDirectEnterPending = g_vm_net_mock_teleport_stone_direct_enter_pending;
    state->teleportStoneMapEnterPending = g_vm_net_mock_teleport_stone_map_enter_pending;
    state->lastTeleportStoneListTick = g_vm_net_mock_last_teleport_stone_list_tick;
    state->teleportStoneConfirmTarget = g_vm_net_mock_teleport_stone_confirm_target;
    state->teleportStoneConfirmTargetValid = g_vm_net_mock_teleport_stone_confirm_target_valid;
    state->teleportStoneDeferredEnterTarget = g_vm_net_mock_teleport_stone_deferred_enter_target;
    state->teleportStoneDeferredEnterValid = g_vm_net_mock_teleport_stone_deferred_enter_valid;
    state->teleportStoneDeferredEnterTick = g_vm_net_mock_teleport_stone_deferred_enter_tick;
    state->teleportStoneDeferredEnterClientId =
        g_vm_net_mock_teleport_stone_deferred_enter_client_id;
    state->teleportStoneSameScene = g_vm_net_mock_teleport_stone_same_scene;
    state->taskTransportPendingTarget = g_vm_net_mock_task_transport_pending_target;
    state->taskTransportPendingValid = g_vm_net_mock_task_transport_pending_valid;
    state->taskTransportPendingTaskId = g_vm_net_mock_task_transport_pending_task_id;
    state->lastSceneChangeFromActorOtherPortal = g_vm_net_mock_last_scene_change_from_actor_other_portal;
    state->lastSceneChangeFb4Type = g_vm_net_mock_last_scene_change_fb4_type;

    state->lastCompletedSceneChangeTarget = g_vm_net_mock_last_completed_scene_change_target;
    state->lastCompletedSceneChangeTargetValid = g_vm_net_mock_last_completed_scene_change_target_valid;
    state->lastCompletedSceneChangeTick = g_vm_net_mock_last_completed_scene_change_tick;
    state->titleRoleSceneFollowupPending = g_vm_net_mock_title_role_scene_followup_pending;
    memcpy(state->lastCurrentSceneReloadScene, g_vm_net_mock_last_current_scene_reload_scene,
           sizeof(state->lastCurrentSceneReloadScene));
    state->lastCurrentSceneReloadValid = g_vm_net_mock_last_current_scene_reload_valid;
    state->lastCurrentSceneReloadTick = g_vm_net_mock_last_current_scene_reload_tick;
    memcpy(state->lastMoveinfoSourceScene, g_vm_net_mock_last_moveinfo_source_scene,
           sizeof(state->lastMoveinfoSourceScene));
    state->lastMoveinfoSourceX = g_vm_net_mock_last_moveinfo_source_x;
    state->lastMoveinfoSourceY = g_vm_net_mock_last_moveinfo_source_y;
    state->lastMoveinfoSourceTick = g_vm_net_mock_last_moveinfo_source_tick;
    state->lastMoveinfoSourceValid = g_vm_net_mock_last_moveinfo_source_valid;
}

static void vm_mock_service_account_restore(vm_mock_service_account_state *state)
{
    vm_mock_service_clear_request_local_scratch(
        state ? "account_restore" : "account_restore_null");

    g_vm_mock_service_active_account = state;
    g_vm_mock_service_active_account_id = state ? state->accountId : NULL;

    if (state == NULL)
    {
        g_vm_mock_service_globals_account = NULL;
        return;
    }

    /*
     * Sticky skip only when globals already mirror this account AND the heap
     * roleDb is loaded.  Otherwise fall through and re-hydrate from state /
     * MySQL so a polluted in-place roleDb cannot stick across requests.
     */
    if (state == g_vm_mock_service_globals_account &&
        state->roleDbLoaded &&
        state->roleDbValid &&
        g_vm_net_mock_role_db_loaded &&
        g_vm_net_mock_role_db_valid &&
        g_vm_mock_service_active_account_id != NULL &&
        vm_mock_service_account_id_equal(g_vm_mock_service_active_account_id,
                                         state->accountId))
    {
        vm_mock_service_client_session *autoSession =
            vm_mock_service_get_active_client_session();
        vm_mock_service_session_load_auto_timers(autoSession, state);
        g_mockBattleAwaitsRevivalConfirm = state->mockBattleAwaitsRevivalConfirm;
        if (autoSession != NULL)
            autoSession->awaitsBattleRevivalConfirm =
                state->mockBattleAwaitsRevivalConfirm != 0;
        printf("[info][mock-service] sticky_restore skipped=1 account=%s "
               "client=%08x prefer=%u hangup=%u evidence=phase-b-sticky\n",
               state->accountId[0] ? state->accountId : "-",
               g_vm_mock_service_active_client_id,
               g_mockBattleAutoPrefer ? 1u : 0u,
               g_mockHangupLoopActive ? 1u : 0u);
        return;
    }
    if (state == g_vm_mock_service_globals_account)
    {
        printf("[warn][mock-service] sticky_restore forced_full account=%s "
               "roleDbLoaded=%u globals_loaded=%u evidence=roledb-ownership-reload\n",
               state->accountId[0] ? state->accountId : "-",
               state->roleDbLoaded ? 1u : 0u,
               g_vm_net_mock_role_db_loaded ? 1u : 0u);
    }

    g_netMockSplitProbe = state->netMockSplitProbe;
    g_netMockUpdateDelivered = state->netMockUpdateDelivered;
    g_netMockEnterGameOffset = state->netMockEnterGameOffset;
    g_netMockEnterGameChecksum = state->netMockEnterGameChecksum;

    g_vm_net_mock_pending_scene_save_valid = state->pendingSceneSaveValid;
    memcpy(g_vm_net_mock_pending_scene_save_scene, state->pendingSceneSaveScene,
           sizeof(g_vm_net_mock_pending_scene_save_scene));
    memcpy(g_vm_net_mock_pending_scene_save_reason, state->pendingSceneSaveReason,
           sizeof(g_vm_net_mock_pending_scene_save_reason));
    g_vm_net_mock_pending_scene_save_x = state->pendingSceneSaveX;
    g_vm_net_mock_pending_scene_save_y = state->pendingSceneSaveY;

    g_mockBattleOperateSessionSerial = state->mockBattleOperateSessionSerial;
    memcpy(g_mockBattleMonsterHealUsedSerialSlots,
           state->mockBattleMonsterHealUsedSerialSlots,
           sizeof(g_mockBattleMonsterHealUsedSerialSlots));
    g_mockBattleOperateTurnCounter = state->mockBattleOperateTurnCounter;
    g_mockBattleOperateSessionArmed = state->mockBattleOperateSessionArmed;
    g_mockBattleAwaitsRevivalConfirm = state->mockBattleAwaitsRevivalConfirm;
    g_mockBattleOperateSessionFinished = state->mockBattleOperateSessionFinished;
    g_mockBattlePendingEnemyTurn = state->mockBattlePendingEnemyTurn;
    g_mockBattleAwaitingSettlement = state->mockBattleAwaitingSettlement;
    g_mockBattleSettlementExitPending = state->mockBattleSettlementExitPending;
    g_mockBattleSettlementExitNotBeforeMs =
        state->mockBattleSettlementExitNotBeforeMs;
    g_mockBattleEncounterNotBeforeMs = state->mockBattleEncounterNotBeforeMs;
    g_mockBattleEncounterCooldownClearPending =
        state->mockBattleEncounterCooldownClearPending;
    /* Cooldown gate disabled: never restore a blocking NotBefore. */
    g_mockBattleEncounterNotBeforeMs = 0;
    if (state->mockBattleEncounterNotBeforeMs != 0 ||
        state->mockBattleEncounterCooldownClearPending != 0)
    {
        g_mockBattleEncounterCooldownClearPending = 1;
    }
    g_mockBattlePostExitSuppressSceneDefaultUntilMs =
        state->mockBattlePostExitSuppressSceneDefaultUntilMs;
    g_mockBattlePostExitSettlePending = state->mockBattlePostExitSettlePending;
    g_mockBattlePostExitSettleNotBeforeMs =
        state->mockBattlePostExitSettleNotBeforeMs;
    g_mockBattleSceneMonsterStartActive = state->mockBattleSceneMonsterStartActive;
    g_mockBattleStartUsesSceneWireMaps = state->mockBattleStartUsesSceneWireMaps;
    g_mockBattleLastOperateValid = state->mockBattleLastOperateValid;
    g_mockBattleLastOperate = state->mockBattleLastOperate;
    g_mockBattleLastIndex = state->mockBattleLastIndex;
    g_mockBattleRoleHpCurrent = state->mockBattleRoleHpCurrent;
    g_mockBattleRoleHpMax = state->mockBattleRoleHpMax;
    g_mockBattleRoleMpCurrent = state->mockBattleRoleMpCurrent;
    g_mockBattleRoleMpMax = state->mockBattleRoleMpMax;
    g_mockBattleEnemyCountCurrent = state->mockBattleEnemyCountCurrent;
    memcpy(g_mockBattleEnemyIdSlots, state->mockBattleEnemyIdSlots,
           sizeof(g_mockBattleEnemyIdSlots));
    memcpy(g_mockBattleEnemyJobSlots, state->mockBattleEnemyJobSlots,
           sizeof(g_mockBattleEnemyJobSlots));
    memcpy(g_mockBattleEnemyHpSlots, state->mockBattleEnemyHpSlots, sizeof(g_mockBattleEnemyHpSlots));
    memcpy(g_mockBattleEnemyHpMaxSlots, state->mockBattleEnemyHpMaxSlots, sizeof(g_mockBattleEnemyHpMaxSlots));
    g_mockBattleEnemyHpCurrent = state->mockBattleEnemyHpCurrent;
    g_mockBattleEnemyHpMax = state->mockBattleEnemyHpMax;
    memcpy(g_mockBattleEnemyAilments, state->mockBattleEnemyAilments,
           sizeof(g_mockBattleEnemyAilments));
    g_vm_net_mock_battle_solo_modifier = state->mockBattleSoloModifier;
    g_vm_net_mock_battle_active_modifier_current = state->mockBattleSoloModifier;

    g_netMockTitleServerListPending = state->netMockTitleServerListPending;
    g_netMockTitleServerSelectConfirmed = state->netMockTitleServerSelectConfirmed;
    g_netMockBackpackGridSeededRoleId = state->netMockBackpackGridSeededRoleId;
    g_netMockShop17ListPending = state->netMockShop17ListPending;
    g_netMockTitleServerListTick = state->netMockTitleServerListTick;
    g_netMockTitleServerSelectTick = state->netMockTitleServerSelectTick;
    g_netMockTitleSelectedServerId = state->netMockTitleSelectedServerId;
    g_netMockBackpackPreferRoleListAfterShopBuy = state->netMockBackpackPreferRoleListAfterShopBuy;
    g_vm_net_mock_update_completed_reenter_pending = state->updateCompletedReenterPending;
    memcpy(g_vm_net_mock_update_completed_name, state->updateCompletedName,
           sizeof(g_vm_net_mock_update_completed_name));

    g_vm_net_mock_role_db = state->roleDb;
    g_vm_net_mock_role_db_loaded = state->roleDbLoaded;
    g_vm_net_mock_role_db_valid = state->roleDbValid;
    g_vm_net_mock_role_position_dirty = state->rolePositionDirty;
    g_vm_net_mock_role_inventory_dirty = state->roleInventoryDirty;
    g_vm_net_mock_warehouse = state->warehouse;

    g_vm_net_mock_battle_rewarded_serial = state->battleRewardedSerial;
    g_vm_net_mock_battle_rewarded_exp = state->battleRewardedExp;
    memcpy(g_vm_net_mock_battle_rewarded_drops, state->battleRewardedDrops,
           sizeof(g_vm_net_mock_battle_rewarded_drops));
    g_vm_net_mock_battle_rewarded_drop_result_count =
        state->battleRewardedDropResultCount;
    g_vm_net_mock_battle_enemy_id_current = state->battleEnemyIdCurrent;
    g_vm_net_mock_battle_role_id_current = state->battleRoleIdCurrent;
    g_vm_net_mock_battle_reward_rng = state->battleRewardRng;
    g_vm_net_mock_battle_settlement_sent_serial = state->battleSettlementSentSerial;
    g_vm_net_mock_battle_drop_refresh_sent_serial = state->battleDropRefreshSentSerial;
    g_vm_net_mock_battle_recovered_serial = state->battleRecoveredSerial;
    g_vm_net_mock_battle_reward_rate_suppressed_serial =
        state->battleRewardRateSuppressedSerial;
    g_mockBattleSettleWireRecoverHp = state->mockBattleSettleWireRecoverHp;
    g_mockBattleSettleWireRecoverMp = state->mockBattleSettleWireRecoverMp;
    g_vm_net_mock_offline_practise_login_flag = state->offlinePractiseLoginFlag;
    memcpy(g_vm_net_mock_offline_practise_login_info, state->offlinePractiseLoginInfo,
           sizeof(g_vm_net_mock_offline_practise_login_info));
    g_vm_net_mock_remote_completed_scene_target_serial =
        state->remoteCompletedSceneTargetSerial;

    memcpy(g_vm_net_mock_scene_moveinfo_npc_pending_scene, state->sceneMoveinfoNpcPendingScene,
           sizeof(g_vm_net_mock_scene_moveinfo_npc_pending_scene));
    g_vm_net_mock_scene_moveinfo_npc_pending = state->sceneMoveinfoNpcPending;
    g_vm_net_mock_scene_moveinfo_npc_wait_post_enter = state->sceneMoveinfoNpcWaitPostEnter;
    g_vm_net_mock_scene_moveinfo_npc_wait_wt6 = state->sceneMoveinfoNpcWaitWt6;
    memcpy(g_vm_net_mock_scene_moveinfo_npc_seeded_scene, state->sceneMoveinfoNpcSeededScene,
           sizeof(g_vm_net_mock_scene_moveinfo_npc_seeded_scene));
    g_vm_net_mock_scene_moveinfo_npc_seeded = state->sceneMoveinfoNpcSeeded;

    g_vm_net_mock_last_scene_change_target = state->lastSceneChangeTarget;
    g_vm_net_mock_last_scene_change_target_valid = state->lastSceneChangeTargetValid;
    g_vm_net_mock_last_scene_change_target_serial = state->lastSceneChangeTargetSerial;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = state->teleportStoneSubtype3AckSent;
    g_vm_net_mock_teleport_stone_direct_enter_pending = state->teleportStoneDirectEnterPending;
    g_vm_net_mock_teleport_stone_map_enter_pending = state->teleportStoneMapEnterPending;
    g_vm_net_mock_last_teleport_stone_list_tick = state->lastTeleportStoneListTick;
    g_vm_net_mock_teleport_stone_confirm_target = state->teleportStoneConfirmTarget;
    g_vm_net_mock_teleport_stone_confirm_target_valid = state->teleportStoneConfirmTargetValid;
    g_vm_net_mock_teleport_stone_deferred_enter_target = state->teleportStoneDeferredEnterTarget;
    g_vm_net_mock_teleport_stone_deferred_enter_valid = state->teleportStoneDeferredEnterValid;
    g_vm_net_mock_teleport_stone_deferred_enter_tick = state->teleportStoneDeferredEnterTick;
    g_vm_net_mock_teleport_stone_deferred_enter_client_id =
        state->teleportStoneDeferredEnterClientId;
    g_vm_net_mock_teleport_stone_same_scene = state->teleportStoneSameScene;
    g_vm_net_mock_task_transport_pending_target = state->taskTransportPendingTarget;
    g_vm_net_mock_task_transport_pending_valid = state->taskTransportPendingValid;
    g_vm_net_mock_task_transport_pending_task_id = state->taskTransportPendingTaskId;
    g_vm_net_mock_last_scene_change_from_actor_other_portal = state->lastSceneChangeFromActorOtherPortal;
    g_vm_net_mock_last_scene_change_fb4_type = state->lastSceneChangeFb4Type;

    g_vm_net_mock_last_completed_scene_change_target = state->lastCompletedSceneChangeTarget;
    g_vm_net_mock_last_completed_scene_change_target_valid = state->lastCompletedSceneChangeTargetValid;
    g_vm_net_mock_last_completed_scene_change_tick = state->lastCompletedSceneChangeTick;
    g_vm_net_mock_title_role_scene_followup_pending = state->titleRoleSceneFollowupPending;
    memcpy(g_vm_net_mock_last_current_scene_reload_scene, state->lastCurrentSceneReloadScene,
           sizeof(g_vm_net_mock_last_current_scene_reload_scene));
    g_vm_net_mock_last_current_scene_reload_valid = state->lastCurrentSceneReloadValid;
    g_vm_net_mock_last_current_scene_reload_tick = state->lastCurrentSceneReloadTick;
    memcpy(g_vm_net_mock_last_moveinfo_source_scene, state->lastMoveinfoSourceScene,
           sizeof(g_vm_net_mock_last_moveinfo_source_scene));
    g_vm_net_mock_last_moveinfo_source_x = state->lastMoveinfoSourceX;
    g_vm_net_mock_last_moveinfo_source_y = state->lastMoveinfoSourceY;
    g_vm_net_mock_last_moveinfo_source_tick = state->lastMoveinfoSourceTick;
    g_vm_net_mock_last_moveinfo_source_valid = state->lastMoveinfoSourceValid;

    {
        vm_mock_service_client_session *autoSession =
            vm_mock_service_get_active_client_session();
        vm_mock_service_session_load_auto_timers(autoSession, state);
    }
    g_vm_mock_service_globals_account = state;
}

static vm_mock_service_account_state *vm_mock_service_account_find(const char *accountId)
{
    vm_mock_service_account_state *state = g_vm_mock_service_accounts;

    if (accountId == NULL || accountId[0] == 0)
        return NULL;
    while (state)
    {
        if (vm_mock_service_account_id_equal(state->accountId, accountId))
            return state;
        state = state->next;
    }
    return NULL;
}

/*
 * After a sync MySQL commit that is newer than any queued deferred snapshot:
 * refresh account_state.roleDb (so sticky capture skip cannot restore stale
 * scene/pos/bag) and bump persistGeneration (so persist workers supersede the
 * in-flight job instead of rewriting MySQL with the old copy).
 *
 * Partial commits set only the matching durable* flag.  Remaining dirty bits
 * keep the account dirty and request reflush once the superseded job finishes.
 */
static void vm_mock_service_account_note_durable_role_write(
    const char *accountId,
    const vm_net_mock_role_db_file *db,
    bool dbValid,
    const vm_net_mock_warehouse_state *warehouse,
    bool durableInventoryCommitted,
    bool durablePositionCommitted,
    const char *reason)
{
    vm_mock_service_account_state *account = NULL;
    u32 prevGen = 0;
    bool needReflush = false;

    if (accountId == NULL || accountId[0] == 0)
        return;
    if (!durableInventoryCommitted && !durablePositionCommitted &&
        (db == NULL || !dbValid) && warehouse == NULL)
    {
        return;
    }
    account = vm_mock_service_account_find(accountId);
    if (account == NULL)
        return;

    prevGen = account->persistGeneration;
    account->persistGeneration += 1u;
    if (account->persistGeneration == 0u)
        account->persistGeneration = 1u;

    if (db != NULL && dbValid)
    {
        /*
         * Refuse to seal a process roleDb into account_state when the caller
         * account id does not own the active globals mirror.  Deferred jobs
         * pass their own snapshot+accountId pair and are unaffected.
         */
        if (db == &g_vm_net_mock_role_db &&
            (g_vm_mock_service_active_account_id == NULL ||
             !vm_mock_service_account_id_equal(g_vm_mock_service_active_account_id,
                                               account->accountId)))
        {
            printf("[error][mock-service] durable_role_write rejected account=%s "
                   "active_id=%s reason=%s evidence=cross-account-roledb-guard\n",
                   account->accountId[0] ? account->accountId : "-",
                   g_vm_mock_service_active_account_id
                       ? g_vm_mock_service_active_account_id
                       : "-",
                   reason ? reason : "-");
        }
        else
        {
            account->roleDb = *db;
            account->roleDbLoaded = true;
            account->roleDbValid = true;
        }
    }
    if (warehouse != NULL && warehouse->loaded &&
        warehouse->accountId[0] != 0 &&
        vm_mock_service_account_id_equal(warehouse->accountId, account->accountId))
    {
        account->warehouse = *warehouse;
    }

    if (durableInventoryCommitted)
    {
        account->roleInventoryDirty = false;
        if (account == g_vm_mock_service_active_account)
            g_vm_net_mock_role_inventory_dirty = false;
    }
    if (durablePositionCommitted)
    {
        account->rolePositionDirty = false;
        if (account == g_vm_mock_service_active_account)
            g_vm_net_mock_role_position_dirty = false;
    }

    needReflush = account->roleInventoryDirty || account->rolePositionDirty;
    if (needReflush)
    {
        account->persistDirtySerial += 1u;
        if (account->persistDirtySerial == 0u)
            account->persistDirtySerial = 1u;
        if (account->persistFlushBusy)
            account->persistReflushWanted = true;
    }

    printf("[info][mock-service] durable_role_write_invalidate account=%s "
           "reason=%s gen=%u->%u inv_committed=%u pos_committed=%u "
           "still_dirty_inv=%u still_dirty_pos=%u busy=%u "
           "evidence=supersede-stale-deferred-flush\n",
           account->accountId[0] ? account->accountId : "-",
           reason ? reason : "-",
           prevGen,
           account->persistGeneration,
           durableInventoryCommitted ? 1u : 0u,
           durablePositionCommitted ? 1u : 0u,
           account->roleInventoryDirty ? 1u : 0u,
           account->rolePositionDirty ? 1u : 0u,
           account->persistFlushBusy ? 1u : 0u);
}

static vm_mock_service_account_state *vm_mock_service_account_find_or_create(const char *accountId)
{
    const char *resolvedId = (accountId && accountId[0]) ? accountId : NULL;
    vm_mock_service_account_state *state = NULL;
    char canonical[64];

    if (resolvedId == NULL)
        return NULL;
    state = vm_mock_service_account_find(resolvedId);
    if (state != NULL)
        return state;

    /*
     * Prefer the spelling stored in accounts (ascii_bin).  Creating a heap row
     * under typed lowercase would later flush role_id into a foreign account_id
     * and collide on uk_account_roles_role_id.
     */
    canonical[0] = 0;
    {
        vm_mock_service_account_record *record =
            vm_mock_service_account_find_record(resolvedId);
        if (record != NULL && record->username[0] != 0)
            snprintf(canonical, sizeof(canonical), "%s", record->username);
    }
    if (canonical[0] != 0)
        resolvedId = canonical;
    state = vm_mock_service_account_find(resolvedId);
    if (state != NULL)
        return state;

    state = (vm_mock_service_account_state *)calloc(1, sizeof(*state));
    if (state == NULL)
        return NULL;
    vm_mock_service_account_state_init(state, resolvedId);
    state->next = g_vm_mock_service_accounts;
    g_vm_mock_service_accounts = state;
    printf("[info][mock-service] account_init id=%s\n", state->accountId);
    return state;
}

static bool vm_mock_service_account_has_bound_session(const char *accountId)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;

    if (accountId == NULL || accountId[0] == 0)
        return false;
    while (session != NULL)
    {
        if (session->accountId[0] != 0 &&
            vm_mock_service_account_id_equal(session->accountId, accountId))
        {
            return true;
        }
        session = session->next;
    }
    return false;
}

/* Drop heap roleDb/session cache when no client session still owns the account
 * and no admin open has it pinned.  Next login reloads from MySQL on demand. */
static void vm_mock_service_account_release_if_idle(const char *accountId)
{
    vm_mock_service_account_state *state = NULL;
    vm_mock_service_account_state *prev = NULL;

    if (accountId == NULL || accountId[0] == 0)
        return;
    if (vm_mock_service_account_has_bound_session(accountId))
        return;
    state = g_vm_mock_service_accounts;
    while (state != NULL)
    {
        if (vm_mock_service_account_id_equal(state->accountId, accountId))
            break;
        prev = state;
        state = state->next;
    }
    if (state == NULL)
        return;
    if (state->pinCount > 0)
        return;
    if (state == g_vm_mock_service_active_account ||
        (g_vm_mock_service_active_account_id != NULL &&
         vm_mock_service_account_id_equal(g_vm_mock_service_active_account_id,
                                          accountId)))
    {
        return;
    }
    if (prev != NULL)
        prev->next = state->next;
    else
        g_vm_mock_service_accounts = state->next;
    if (g_vm_mock_service_globals_account == state)
        g_vm_mock_service_globals_account = NULL;
    printf("[info][mock-service] account_release id=%s reason=idle\n", state->accountId);
    free(state);
}

static void vm_mock_service_session_unbind_account(vm_mock_service_client_session *session)
{
    char accountId[64];

    if (session == NULL || session->accountId[0] == 0)
        return;
    snprintf(accountId, sizeof(accountId), "%s", session->accountId);
    session->accountId[0] = 0;
    vm_mock_service_account_release_if_idle(accountId);
}

/* Flush dirty role rows for an offline session without permanently stealing the
 * currently restored active account (scene-sync expire path). */
static void vm_mock_service_account_flush_for_session(
    const vm_mock_service_client_session *session, const char *reason)
{
    vm_mock_service_account_state *state = NULL;
    vm_mock_service_account_state *savedActive = g_vm_mock_service_active_account;
    const char *savedActiveId = g_vm_mock_service_active_account_id;
    u32 savedClientId = g_vm_mock_service_active_client_id;

    if (session == NULL || session->accountId[0] == 0)
        return;
    state = vm_mock_service_account_find(session->accountId);
    if (state == NULL)
        return;
    if (savedActive != NULL)
        vm_mock_service_account_capture(savedActive);
    vm_mock_service_account_restore(state);
    g_vm_mock_service_active_client_id = session->clientId;
    if (g_vm_net_mock_role_position_dirty || g_vm_net_mock_role_inventory_dirty)
    {
        const char *flushReason = reason ? reason : "session-offline";
        if (g_vm_net_mock_role_inventory_dirty && !g_vm_net_mock_role_position_dirty)
            flushReason = "session-offline-inventory";
        else if (g_vm_net_mock_role_position_dirty && !g_vm_net_mock_role_inventory_dirty)
            flushReason = reason ? reason : "session-offline-position";
        else
            flushReason = "session-offline-position+inventory";
        vm_net_mock_role_db_save(flushReason);
    }
    vm_mock_service_account_capture(state);
    if (savedActive != NULL)
        vm_mock_service_account_restore(savedActive);
    else
        vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_account_id = savedActiveId;
    g_vm_mock_service_active_client_id = savedClientId;
}

static bool vm_mock_service_account_ensure_role_db_loaded(
    vm_mock_service_account_state *account)
{
    vm_mock_service_account_state *savedActive = g_vm_mock_service_active_account;
    const char *savedActiveId = g_vm_mock_service_active_account_id;
    u32 savedClientId = g_vm_mock_service_active_client_id;

    if (account == NULL)
        return false;
    if (account == g_vm_mock_service_active_account)
    {
        if (!g_vm_net_mock_role_db_loaded)
            vm_net_mock_role_db_load();
        return g_vm_net_mock_role_db_valid;
    }
    if (account->roleDbLoaded && account->roleDbValid)
        return true;
    if (savedActive != NULL)
        vm_mock_service_account_capture(savedActive);
    vm_mock_service_account_restore(account);
    g_vm_mock_service_active_client_id = 0;
    vm_net_mock_role_db_load();
    vm_mock_service_account_capture(account);
    if (savedActive != NULL)
        vm_mock_service_account_restore(savedActive);
    else
        vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_account_id = savedActiveId;
    g_vm_mock_service_active_client_id = savedClientId;
    return account->roleDbLoaded && account->roleDbValid;
}

static vm_mock_service_client_session *vm_mock_service_find_client_session(u32 clientId)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;
    if (clientId == 0)
        return NULL;
    while (session)
    {
        if (session->clientId == clientId)
            return session;
        session = session->next;
    }
    return NULL;
}

static vm_mock_service_client_session *vm_mock_service_get_or_create_client_session(u32 clientId)
{
    vm_mock_service_client_session *session = NULL;
    if (clientId == 0)
        return NULL;
    session = vm_mock_service_find_client_session(clientId);
    if (session != NULL)
        return session;
    session = (vm_mock_service_client_session *)calloc(1, sizeof(*session));
    if (session == NULL)
        return NULL;
    session->clientId = clientId;
    session->next = g_vm_mock_service_client_sessions;
    g_vm_mock_service_client_sessions = session;
    return session;
}

static vm_mock_service_client_session *vm_mock_service_get_active_client_session(void)
{
    if (g_vm_mock_service_active_client_id == 0)
        return NULL;
    return vm_mock_service_find_client_session(g_vm_mock_service_active_client_id);
}

/*
 * PixiJS-only 角色改名卡: 1/29/15 { cardseq, name }.
 * Consume 1× item 852 → set role->name + onlineRoleName (GBK ≤31).
 * result: 1=ok, 2=card missing/wrong, 3=empty name, 4=too long,
 *         5=same name, 6=name taken, 7=consume failed, 8=persist failed.
 * Placed after session struct so onlineRoleName sync can dereference fields.
 */
static u32 vm_net_mock_build_rename_card_response(const u8 *request,
                                                   u32 requestLen, u8 *out,
                                                   u32 outCap)
{
    u32 offset = 4;
    u32 cardSeqValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 cardSeq = 0;
    u32 cardRemaining = 0;
    char newName[32];
    char before[32];
    size_t nameLen = 0;
    const char *reason = "ok";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *card = NULL;
    vm_mock_service_client_session *session = NULL;
    u32 i = 0;

    newName[0] = 0;
    before[0] = 0;
    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset,
                                          &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 15 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "cardseq", &cardSeqValue) ||
        cardSeqValue == 0 || cardSeqValue > 0xffffu)
    {
        return 0;
    }
    cardSeq = (u16)cardSeqValue;
    if (!vm_net_mock_get_object_string_field(object.payload, object.payloadLen,
                                              "name", newName, sizeof(newName)))
    {
        result = 3;
        reason = "missing-name";
    }
    else
    {
        nameLen = strlen(newName);
        while (nameLen > 0 &&
               (newName[nameLen - 1] == ' ' || newName[nameLen - 1] == '\t'))
        {
            newName[--nameLen] = 0;
        }
        {
            size_t lead = 0;
            while (newName[lead] == ' ' || newName[lead] == '\t')
                ++lead;
            if (lead > 0)
            {
                memmove(newName, newName + lead, nameLen - lead + 1);
                nameLen -= lead;
            }
        }
        if (nameLen == 0)
        {
            result = 3;
            reason = "empty-name";
        }
        else if (nameLen >= 32u)
        {
            result = 4;
            reason = "name-too-long";
        }
    }

    role = vm_net_mock_active_role();
    if (result == 1 && role == NULL)
    {
        result = 2;
        reason = "no-role";
    }
    else if (result == 1)
    {
        vm_net_mock_role_normalize_backpack(role);
        card = vm_net_mock_role_find_backpack_item(role, 0, cardSeq);
        if (card == NULL || card->count == 0 ||
            card->itemId != VM_NET_MOCK_RENAME_CARD_ITEM_ID)
        {
            result = 2;
            reason = "card-not-found";
        }
        else if (strcmp(role->name, newName) == 0)
        {
            result = 5;
            reason = "same-name";
        }
        else
        {
            bool nameTaken = false;

            for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
            {
                if (g_vm_net_mock_role_db.roles[i].roleId == role->roleId)
                    continue;
                if (g_vm_net_mock_role_db.roles[i].name[0] != 0 &&
                    strcmp(g_vm_net_mock_role_db.roles[i].name, newName) == 0)
                {
                    result = 6;
                    reason = "name-taken-account";
                    break;
                }
            }
            if (result == 1)
            {
                if (!vm_net_mock_account_roles_name_taken(newName, role->roleId,
                                                          &nameTaken))
                {
                    result = 8;
                    reason = "name-check-failed";
                }
                else if (nameTaken)
                {
                    result = 6;
                    reason = "name-taken-global";
                }
            }
            if (result == 1)
            {
                session = g_vm_mock_service_client_sessions;
                while (session != NULL)
                {
                    if (session->onlineRoleId != 0 &&
                        session->onlineRoleId != role->roleId &&
                        session->onlineRoleName[0] != 0 &&
                        strcmp(session->onlineRoleName, newName) == 0)
                    {
                        result = 6;
                        reason = "name-taken-online";
                        break;
                    }
                    session = session->next;
                }
            }
        }
    }

    if (result == 1 && role != NULL && card != NULL)
    {
        snprintf(before, sizeof(before), "%s", role->name);
        if (!vm_net_mock_role_consume_backpack_item(
                role, VM_NET_MOCK_RENAME_CARD_ITEM_ID, cardSeq, 1u,
                &cardRemaining))
        {
            result = 7;
            reason = "consume-failed";
        }
        else
        {
            memset(role->name, 0, sizeof(role->name));
            memcpy(role->name, newName, nameLen);
            role->name[nameLen] = 0;
            if (!vm_net_mock_role_db_save_role("rename-card", role->roleId))
            {
                snprintf(role->name, sizeof(role->name), "%s", before);
                (void)vm_net_mock_role_add_backpack_item_to_role(
                    role, VM_NET_MOCK_RENAME_CARD_ITEM_ID, 1, 0, NULL,
                    "rename-card-rollback", NULL, 0);
                cardRemaining += 1u;
                result = 8;
                reason = "persist-failed";
            }
            else
            {
                session = vm_mock_service_get_active_client_session();
                if (session != NULL && session->onlineRoleId == role->roleId)
                {
                    snprintf(session->onlineRoleName,
                             sizeof(session->onlineRoleName), "%s", role->name);
                }
                vm_net_mock_role_mark_inventory_dirty("rename-card");
                reason = "rename-ok";
            }
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 15,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "cardseq", cardSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining",
                                    cardRemaining) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "name",
                                       result == 1 ? newName : ""))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_rename_card card_seq=%u remain=%u result=%u "
           "reason=%s name=%s before=%s resp=29/15\n",
           cardSeq, cardRemaining, result, reason,
           result == 1 ? newName : "-", before[0] ? before : "-");
    vm_autotest_note(
        "mock_rename_card card=%u result=%u reason=%s evidence=pixijs-only-29/15\n",
        cardSeq, result, reason);
    return pos;
}

/* GBK: 请更新客户端 */
static const char g_vm_net_mock_login_update_required_gbk[] =
    "\xc7\xeb\xb8\xfc\xd0\xc2\xbf\xcd\xbb\xa7\xb6\xcb";

static bool vm_net_mock_parse_request_code_version(const u8 *request,
                                                   u32 requestLen,
                                                   u32 *codeVersionOut)
{
    u16 v16 = 0;
    u32 v32 = 0;
    u8 v8 = 0;

    if (codeVersionOut)
        *codeVersionOut = 0;
    if (request == NULL || requestLen == 0)
        return false;
    if (vm_net_mock_get_object_u16_field(request, requestLen, "codeVersion", &v16))
    {
        if (codeVersionOut)
            *codeVersionOut = v16;
        return true;
    }
    if (vm_net_mock_get_object_u32_field(request, requestLen, "codeVersion", &v32))
    {
        if (codeVersionOut)
            *codeVersionOut = v32;
        return true;
    }
    if (vm_net_mock_get_object_u8_field(request, requestLen, "codeVersion", &v8))
    {
        if (codeVersionOut)
            *codeVersionOut = v8;
        return true;
    }
    return false;
}

static u32 vm_net_mock_expected_code_version(void)
{
    const char *spec = getenv("CBE_CODE_VERSION");
    char path[1200];
    FILE *fp = NULL;
    unsigned long parsed = 0;
    static const char *fallbackPaths[] = {
        "web/fs/JHOnlineData/server_code_version.txt",
        "../web/fs/JHOnlineData/server_code_version.txt",
        "bin/JHOnlineData/server_code_version.txt",
        "../bin/JHOnlineData/server_code_version.txt",
        "JHOnlineData/server_code_version.txt"
    };

    if (spec != NULL && spec[0] != 0)
        return vm_net_mock_env_u32("CBE_CODE_VERSION", 0);

    if (vm_net_mock_build_configured_resource_path("server_code_version.txt",
                                                   path, sizeof(path)))
        fp = fopen(path, "rb");
    for (u32 i = 0; fp == NULL &&
         i < sizeof(fallbackPaths) / sizeof(fallbackPaths[0]); ++i)
        fp = fopen(fallbackPaths[i], "rb");
    if (fp == NULL)
        return 0;
    if (fscanf(fp, "%lu", &parsed) != 1)
        parsed = 0;
    fclose(fp);
    return (u32)parsed;
}

static void vm_mock_service_session_note_code_version(const u8 *request,
                                                      u32 requestLen)
{
    u32 codeVersion = 0;
    vm_mock_service_client_session *session = NULL;
    u32 clientId = g_vm_mock_service_active_client_id;

    if (!vm_net_mock_parse_request_code_version(request, requestLen, &codeVersion))
        return;
    if (clientId == 0)
        return;
    session = vm_mock_service_get_or_create_client_session(clientId);
    if (session == NULL)
        return;
    session->haveClientCodeVersion = true;
    session->clientCodeVersion = codeVersion;
    printf("[info][network] mock_code_version_seen client=%08x codeVersion=%u "
           "expected=%u protocol=WT18 evidence=JianghuOL.CBE:codeVersion\n",
           clientId, codeVersion, vm_net_mock_expected_code_version());
}

/*
 * When expected codeVersion is configured (env CBE_CODE_VERSION or
 * JHOnlineData/server_code_version.txt), login requires a matching value
 * previously captured from WT 18/9.  Unconfigured expected=0 disables the gate.
 */
static bool vm_net_mock_login_code_version_ok(const char **errorOut)
{
    u32 expected = vm_net_mock_expected_code_version();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 clientCode = 0;
    bool haveClient = false;

    if (expected == 0)
        return true;
    if (session != NULL && session->haveClientCodeVersion)
    {
        haveClient = true;
        clientCode = session->clientCodeVersion;
    }
    if (haveClient && clientCode == expected)
    {
        printf("[info][network] mock_login_code_version_ok client=%08x "
               "codeVersion=%u expected=%u action=allow-server-list\n",
               session ? session->clientId : g_vm_mock_service_active_client_id,
               clientCode, expected);
        return true;
    }
    if (errorOut)
        *errorOut = g_vm_net_mock_login_update_required_gbk;
    printf("[warn][network] mock_login_code_version_reject client=%08x "
           "client_code=%u have=%u expected=%u action=login-fail "
           "evidence=JianghuOL.CBE:codeVersion+mmTitle:result2\n",
           session ? session->clientId : g_vm_mock_service_active_client_id,
           clientCode, haveClient ? 1u : 0u, expected);
    return false;
}

static void vm_net_mock_arm_enhance_backpack_list_resync(u16 equipSeq,
                                                         u32 equipItemId,
                                                         u16 enhanceLevel,
                                                         bool inlineListOk)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || equipSeq == 0 || equipItemId == 0)
        return;
    /*
     * Poll 17/1 rebuilds bag attrs after enhance.  Always follow with 26/0 —
     * listOnly skipped the busy-ack and left DF_DataPackage_DoLoading up
     * (docs/re/2026-08-05-enhance-success-progress-stuck.md).
     */
    session->backpackListResyncPending = true;
    session->backpackListResyncPhase = 1;
    session->backpackListResyncListOnly = false;
    session->backpackListResyncTick = g_schedulerTick;
    session->backpackResyncRemoveSeq = equipSeq;
    session->backpackResyncRemoveItemId = equipItemId;
    g_netMockBackpackGridSeededRoleId = 0;
    printf("[info][network] mock_equipment_enhance bag-list-arm "
           "seq=%u item=%u enhance=%u inline_17=%u "
           "evidence=poll-17/1+26/0-after-enhance-attrs\n",
           equipSeq, equipItemId, enhanceLevel, inlineListOk ? 1 : 0);
}

static void vm_mock_service_session_arm_warehouse_pass_dialog(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL)
        return;
    session->warehouseSessionArmed = true;
    session->warehouseDialogPending = true;
    session->warehouseDialogTick = g_schedulerTick;
    printf("[info][mock-service] warehouse_pass_dialog_arm client=%08x "
           "evidence=834-use-per-session-arm\n",
           session->clientId);
}

static void vm_mock_service_session_arm_equip_sell_pass_dialog(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL)
        return;
    session->equipSellSessionArmed = true;
    session->equipSellDialogPending = true;
    session->equipSellDialogTick = g_schedulerTick;
    printf("[info][mock-service] equip_sell_pass_dialog_arm client=%08x "
           "evidence=839-use-per-session-arm\n",
           session->clientId);
}

static void vm_mock_service_session_arm_coolmall_dialog(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    FILE *fp;

    if (session == NULL)
        return;
    session->coolmallSessionArmed = true;
    session->coolmallDialogPending = true;
    session->coolmallDialogTick = g_schedulerTick;
    session->coolmallBrowseSubtype = 0;
    session->coolmallBrowsePage = 0;
    /*
     * Host reads this flag to reject mmShop AddScreen so the guest never
     * stays on the W-coin mall UI (debug-a1d304 exit stuck / SCR).
     */
    fp = fopen("d:/works/my/jianghu/CBE_EMU_pay/bin/cbe_coolmall_redirect.flag", "wb");
    if (fp != NULL)
    {
        fputs("1\n", fp);
        fclose(fp);
    }
    printf("[info][mock-service] coolmall_dialog_arm client=%08x "
           "evidence=mall-button-no-pass-26/1\n",
           session->clientId);
}

static void vm_mock_service_session_arm_task_prompt_refresh(const char *scene)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene))
        return;
    session->taskPromptRefreshPending = true;
    snprintf(session->taskPromptRefreshScene,
             sizeof(session->taskPromptRefreshScene), "%s", scene);
    printf("[info][mock-service] task_prompt_refresh_arm client=%08x scene=%s evidence=JianghuOL.CBE:0x01037998->0x01017C6C\n",
           session->clientId, scene);
}

static void vm_mock_service_session_note_scene_npc_catalog_seeded(
    vm_mock_service_client_session *session,
    const char *scene,
    u8 npcNum)
{
    if (session == NULL || npcNum == 0u || !vm_net_mock_scene_name_is_safe(scene))
        return;
    session->sceneNpcCatalogSeeded = true;
    snprintf(session->sceneNpcCatalogSeededScene,
             sizeof(session->sceneNpcCatalogSeededScene), "%s", scene);
    /*
     * mmShop exit window: first nonempty type-21 restore (type27 once_or_empty,
     * early-shell force, or catalog deliver) owns warm delivery.  Mark here so
     * a later early-shell / startup-poll cannot emit a second nonempty 27/11.
     */
    if (session->shopReturnLoadingClearPending ||
        session->shopReturnNpcInlineOnShell ||
        session->shopReturnNpcCatalogPending)
    {
        session->shopReturnWarmNpcDelivered = true;
        session->shopReturnNpcInlineOnShell = false;
    }
}

static void vm_mock_service_session_clear_scene_npc_catalog_seeded(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL || !session->sceneNpcCatalogSeeded)
        return;
    printf("[info][mock-service] scene_npc_catalog_seeded_clear client=%08x "
           "scene=%s reason=%s evidence=per-client-seed-not-process-global\n",
           session->clientId,
           session->sceneNpcCatalogSeededScene[0]
               ? session->sceneNpcCatalogSeededScene
               : "-",
           reason ? reason : "-");
    session->sceneNpcCatalogSeeded = false;
    session->sceneNpcCatalogSeededScene[0] = 0;
}

static bool vm_mock_service_session_scene_npc_catalog_seeded_for(
    const vm_mock_service_client_session *session,
    const char *scene)
{
    if (session == NULL || !session->sceneNpcCatalogSeeded ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        session->sceneNpcCatalogSeededScene[0] == 0)
    {
        return false;
    }
    return vm_net_mock_scene_names_equal_loose(
        session->sceneNpcCatalogSeededScene, scene);
}

static int vm_mock_service_duel_client_index(const vm_mock_service_duel *duel,
                                             u32 clientId)
{
    if (duel == NULL || !duel->active || clientId == 0)
        return -1;
    if (duel->clientIds[0] == clientId)
        return 0;
    if (duel->clientIds[1] == clientId)
        return 1;
    return -1;
}

static vm_mock_service_duel *vm_mock_service_duel_find_for_client(u32 clientId,
                                                                   int *indexOut)
{
    if (indexOut)
        *indexOut = -1;
    for (u32 i = 0; i < VM_MOCK_SERVICE_DUEL_MAX; ++i)
    {
        int index = vm_mock_service_duel_client_index(&g_vm_mock_service_duels[i],
                                                      clientId);
        if (index >= 0)
        {
            if (indexOut)
                *indexOut = index;
            return &g_vm_mock_service_duels[i];
        }
    }
    return NULL;
}

static void vm_mock_service_duel_release_if_done(vm_mock_service_duel *duel)
{
    if (duel != NULL && duel->active && duel->finished &&
        duel->startPendingMask == 0 && duel->terminalPendingMask == 0)
    {
        for (u8 i = 0; i < VM_MOCK_SERVICE_DUEL_EVENT_MAX; ++i)
        {
            if (duel->events[i].valid)
                return;
        }
        if (duel->releaseNotBeforeTick != 0 &&
            g_schedulerTick < duel->releaseNotBeforeTick)
        {
            return;
        }
        printf("[info][mock-service] duel_release serial=%u first=%08x second=%08x\n",
               duel->serial, duel->clientIds[0], duel->clientIds[1]);
        memset(duel, 0, sizeof(*duel));
    }
}

static bool vm_mock_service_session_try_consume_spar_substitute_doll(
    vm_mock_service_client_session *session)
{
    vm_mock_service_account_state *account = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_mock_service_account_state *savedActive = g_vm_mock_service_active_account;
    const char *savedActiveId = g_vm_mock_service_active_account_id;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    bool consumed = false;

    if (session == NULL || session->accountId[0] == 0 || session->onlineRoleId == 0)
        return false;

    if (savedActive != NULL)
        vm_mock_service_account_capture(savedActive);
    account = vm_mock_service_account_find_or_create(session->accountId);
    if (account == NULL)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return false;
    }
    vm_mock_service_account_restore(account);
    g_vm_mock_service_active_client_id = session->clientId;
    if (!vm_mock_service_account_ensure_role_db_loaded(account) ||
        !g_vm_net_mock_role_db_valid)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return false;
    }

    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == session->onlineRoleId)
        {
            role = &g_vm_net_mock_role_db.roles[i];
            break;
        }
    }
    if (role != NULL &&
        vm_net_mock_role_find_backpack_item(
            role, VM_NET_MOCK_SPAR_SUBSTITUTE_DOLL_ITEM_ID, 0) != NULL &&
        vm_net_mock_role_consume_backpack_item_amount(
            role, VM_NET_MOCK_SPAR_SUBSTITUTE_DOLL_ITEM_ID, 1, NULL, 0, NULL))
    {
        vm_net_mock_role_mark_inventory_dirty("spar-substitute-doll");
        if (g_vm_net_mock_role_position_dirty || g_vm_net_mock_role_inventory_dirty)
            vm_net_mock_role_db_save("spar-substitute-doll");
        session->backpackListResyncPending = true;
        session->backpackListResyncPhase = 1;
        session->backpackListResyncListOnly = true;
        session->backpackListResyncTick = g_schedulerTick;
        g_netMockBackpackGridSeededRoleId = 0;
        consumed = true;
        printf("[info][mock-service] spar_substitute_doll_consume client=%08x "
               "role=%u item=%u durability=%u evidence=item.dsh:846\n",
               session->clientId, role->roleId,
               VM_NET_MOCK_SPAR_SUBSTITUTE_DOLL_ITEM_ID,
               VM_NET_MOCK_SPAR_SUBSTITUTE_DOLL_DURABILITY);
    }

    vm_mock_service_account_capture(account);
    if (savedActive != NULL)
        vm_mock_service_account_restore(savedActive);
    else
        vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_account_id = savedActiveId;
    g_vm_mock_service_active_client_id = savedClientId;
    return consumed;
}

static bool vm_mock_service_session_has_force_pk_proof(
    vm_mock_service_client_session *session)
{
    vm_mock_service_account_state *account = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_mock_service_account_state *savedActive = g_vm_mock_service_active_account;
    const char *savedActiveId = g_vm_mock_service_active_account_id;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    bool found = false;

    if (session == NULL || session->accountId[0] == 0 || session->onlineRoleId == 0)
        return false;

    if (savedActive != NULL)
        vm_mock_service_account_capture(savedActive);
    account = vm_mock_service_account_find_or_create(session->accountId);
    if (account == NULL)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return false;
    }
    vm_mock_service_account_restore(account);
    g_vm_mock_service_active_client_id = session->clientId;
    if (vm_mock_service_account_ensure_role_db_loaded(account) &&
        g_vm_net_mock_role_db_valid)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            if (g_vm_net_mock_role_db.roles[i].roleId == session->onlineRoleId)
            {
                role = &g_vm_net_mock_role_db.roles[i];
                break;
            }
        }
        found = role != NULL &&
                vm_net_mock_role_find_backpack_item(
                    role, VM_NET_MOCK_FORCE_PK_PROOF_ITEM_ID, 0) != NULL;
    }
    vm_mock_service_account_capture(account);
    if (savedActive != NULL)
        vm_mock_service_account_restore(savedActive);
    else
        vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_account_id = savedActiveId;
    g_vm_mock_service_active_client_id = savedClientId;
    return found;
}

static bool vm_mock_service_session_try_consume_force_pk_proof(
    vm_mock_service_client_session *session)
{
    vm_mock_service_account_state *account = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_mock_service_account_state *savedActive = g_vm_mock_service_active_account;
    const char *savedActiveId = g_vm_mock_service_active_account_id;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    bool consumed = false;

    if (session == NULL || session->accountId[0] == 0 || session->onlineRoleId == 0)
        return false;

    if (savedActive != NULL)
        vm_mock_service_account_capture(savedActive);
    account = vm_mock_service_account_find_or_create(session->accountId);
    if (account == NULL)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return false;
    }
    vm_mock_service_account_restore(account);
    g_vm_mock_service_active_client_id = session->clientId;
    if (!vm_mock_service_account_ensure_role_db_loaded(account) ||
        !g_vm_net_mock_role_db_valid)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return false;
    }

    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == session->onlineRoleId)
        {
            role = &g_vm_net_mock_role_db.roles[i];
            break;
        }
    }
    if (role != NULL &&
        vm_net_mock_role_find_backpack_item(
            role, VM_NET_MOCK_FORCE_PK_PROOF_ITEM_ID, 0) != NULL &&
        vm_net_mock_role_consume_backpack_item_amount(
            role, VM_NET_MOCK_FORCE_PK_PROOF_ITEM_ID, 1, NULL, 0, NULL))
    {
        vm_net_mock_role_mark_inventory_dirty("force-pk-proof");
        if (g_vm_net_mock_role_position_dirty || g_vm_net_mock_role_inventory_dirty)
            vm_net_mock_role_db_save("force-pk-proof");
        session->backpackListResyncPending = true;
        session->backpackListResyncPhase = 1;
        session->backpackListResyncListOnly = true;
        session->backpackListResyncTick = g_schedulerTick;
        g_netMockBackpackGridSeededRoleId = 0;
        consumed = true;
        printf("[info][mock-service] force_pk_proof_consume client=%08x "
               "role=%u item=%u durability=%u evidence=item:848\n",
               session->clientId, role->roleId,
               VM_NET_MOCK_FORCE_PK_PROOF_ITEM_ID,
               VM_NET_MOCK_FORCE_PK_PROOF_DURABILITY);
    }

    vm_mock_service_account_capture(account);
    if (savedActive != NULL)
        vm_mock_service_account_restore(savedActive);
    else
        vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_account_id = savedActiveId;
    g_vm_mock_service_active_client_id = savedClientId;
    return consumed;
}

static void vm_mock_service_session_apply_spar_death_penalty(
    vm_mock_service_client_session *session,
    const char *reason)
{
    vm_mock_service_account_state *account = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_mock_service_account_state *savedActive = g_vm_mock_service_active_account;
    const char *savedActiveId = g_vm_mock_service_active_account_id;
    u32 savedClientId = g_vm_mock_service_active_client_id;
    char respawnScene[64];
    u16 respawnX = 0;
    u16 respawnY = 0;
    u32 expPenalty = 0;
    u32 moneyPenalty = 0;
    u32 reviveMp = 0;
    u32 reviveHp = 0;
    u32 i = 0;

    if (session == NULL || session->sparExitPenaltyApplied)
        return;
    if (session->accountId[0] == 0 || session->onlineRoleId == 0)
        return;
    account = vm_mock_service_account_find_or_create(session->accountId);
    if (account == NULL)
        return;

    if (savedActive != NULL)
        vm_mock_service_account_capture(savedActive);
    vm_mock_service_account_restore(account);
    g_vm_mock_service_active_client_id = session->clientId;
    if (!vm_mock_service_account_ensure_role_db_loaded(account) ||
        !g_vm_net_mock_role_db_valid)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return;
    }

    for (i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == session->onlineRoleId)
        {
            role = &g_vm_net_mock_role_db.roles[i];
            break;
        }
    }
    if (role == NULL)
    {
        if (savedActive != NULL)
            vm_mock_service_account_restore(savedActive);
        else
            vm_mock_service_account_restore(NULL);
        g_vm_mock_service_active_account_id = savedActiveId;
        g_vm_mock_service_active_client_id = savedClientId;
        return;
    }

    g_vm_net_mock_role_db.activeRoleId = role->roleId;
    /* Death penalty requires durable HP==0 (PvE 1/7/14 ordinary path). */
    role->hp = 0;
    memset(respawnScene, 0, sizeof(respawnScene));
    reviveHp = vm_net_mock_role_apply_death_penalty_to(
        role,
        reason ? reason : "spar-defeat",
        &expPenalty,
        &moneyPenalty,
        &reviveMp,
        respawnScene,
        sizeof(respawnScene),
        &respawnX,
        &respawnY);
    if (reviveHp == 0)
    {
        printf("[warn][mock-service] spar_death_penalty_skip client=%08x role=%u "
               "reason=%s detail=apply-returned-0\n",
               session->clientId, session->onlineRoleId,
               reason ? reason : "spar-defeat");
    }
    else
    {
        session->onlineHp = role->hp;
        session->onlineHpMax = role->hpMax ? role->hpMax : role->hp;
        session->onlineMp = role->mp;
        session->onlineMpMax = role->mpMax ? role->mpMax : role->mp;
        if (vm_net_mock_scene_name_is_safe(role->scene))
        {
            snprintf(session->onlineScene, sizeof(session->onlineScene), "%s",
                     role->scene);
            session->onlineX = role->x;
            session->onlineY = role->y;
        }
        session->sparDeathRespawnPending = true;
        snprintf(session->sparDeathRespawnScene,
                 sizeof(session->sparDeathRespawnScene),
                 "%s",
                 respawnScene[0] ? respawnScene : role->scene);
        session->sparDeathRespawnX = respawnX ? respawnX : role->x;
        session->sparDeathRespawnY = respawnY ? respawnY : role->y;
        session->sparDeathExpPenalty = expPenalty;
        session->sparDeathMoneyPenalty = moneyPenalty;
        /* Block winner-style +1exp/+1gold on status7. */
        session->sparExitExpGranted = true;
        session->sparExitPenaltyApplied = true;
        printf("[info][mock-service] spar_death_penalty client=%08x role=%u "
               "exp_penalty=%u money_penalty=%u revive=%u/%u scene=%s "
               "pos=(%u,%u) reason=%s evidence=PvE-death-parity\n",
               session->clientId, role->roleId, expPenalty, moneyPenalty,
               role->hp, role->mp,
               session->sparDeathRespawnScene,
               session->sparDeathRespawnX, session->sparDeathRespawnY,
               reason ? reason : "spar-defeat");
    }

    if (g_vm_net_mock_role_position_dirty || g_vm_net_mock_role_inventory_dirty)
        vm_net_mock_role_db_save(reason ? reason : "spar-defeat");
    vm_mock_service_account_capture(account);
    if (savedActive != NULL)
        vm_mock_service_account_restore(savedActive);
    else
        vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_account_id = savedActiveId;
    g_vm_mock_service_active_client_id = savedClientId;
}

static void vm_mock_service_duel_arm_observer_exit(
    vm_mock_service_duel *duel,
    int observerIndex)
{
    vm_mock_service_client_session *observer = NULL;
    u32 targetHp = 0;
    u32 targetMp = 0;
    u32 battleHp = 0;
    u32 battleMp = 0;
    u32 holdTicks = 0;

    if (duel == NULL || observerIndex < 0 || observerIndex > 1)
        return;
    observer = vm_mock_service_find_client_session(duel->clientIds[observerIndex]);
    if (observer == NULL)
        return;
    battleHp = duel->hp[observerIndex];
    battleMp = duel->mp[observerIndex];
    targetHp = observer->onlineHp;
    targetMp = observer->onlineMp;
    if (targetHp == 0)
        targetHp = duel->hpMax[observerIndex];
    /*
     * Spar end: two-phase with settle 4/7 then 4/8+4/11+4/9 tear-down.
     * Loser (battle HP=0) takes the same death penalty as PvE ordinary
     * 1/7/14 (10% level progress EXP, 5% copper, 30% revive, telestone).
     */
    observer->sparExitWasDead = (battleHp == 0);
    observer->sparExitRecoverHp =
        targetHp > battleHp ? (targetHp - battleHp) : 0;
    if (observer->sparExitWasDead && observer->sparExitRecoverHp == 0)
        observer->sparExitRecoverHp = targetHp ? targetHp : 1;
    observer->sparExitRecoverMp = 0;
    observer->sparExitMessageQueued = false;
    observer->sparExitExpGranted = false;
    observer->sparExitPenaltyApplied = false;
    observer->sparDeathExpPenalty = 0;
    observer->sparDeathMoneyPenalty = 0;
    observer->sparDeathRespawnPending = false;
    observer->sparDeathRespawnScene[0] = 0;
    observer->sparDeathRespawnX = 0;
    observer->sparDeathRespawnY = 0;
    observer->sparSettleDelivered = false;
    observer->sparResultMessageArmed = false;
    observer->sparResultNotBeforeTick = 0;
    (void)battleMp;
    (void)targetMp;
    if (observer->sparExitWasDead && duel->friendlyMode)
    {
        /* Friendly 切磋: keep lose tip, but no exp/money/respawn penalty. */
        observer->sparDeathExpPenalty = 0;
        observer->sparDeathMoneyPenalty = 0;
        observer->sparExitRecoverHp =
            targetHp > battleHp ? (targetHp - battleHp) : (targetHp ? targetHp : 1);
    }
    else if (observer->sparExitWasDead && !(duel->arenaMode) && !(duel->wagerMode))
    {
        vm_mock_service_session_apply_spar_death_penalty(
            observer, "spar-defeat");
        /* Recover display delta after penalty revive (30% max). */
        if (observer->onlineHp > battleHp)
            observer->sparExitRecoverHp = observer->onlineHp - battleHp;
        else if (observer->sparExitRecoverHp == 0)
            observer->sparExitRecoverHp = observer->onlineHp ? observer->onlineHp : 1;
    }
    else if (observer->sparExitWasDead && (duel->arenaMode || duel->wagerMode))
    {
        /* Arena: no PvE death penalty; restore display HP for settle banner. */
        observer->sparExitRecoverHp =
            targetHp > battleHp ? (targetHp - battleHp) : (targetHp ? targetHp : 1);
        observer->sparExitWasDead = false;
    }
    holdTicks = vm_net_mock_env_u32("CBE_DUEL_EXIT_REDELIVER_TICKS", 300);
    if (holdTicks < 90)
        holdTicks = 90;
    observer->sparExitRedeliverUntilTick = g_schedulerTick + holdTicks;
    printf("[info][mock-service] duel_exit_arm observer=%08x serial=%u "
           "battle_hp=%u/%u dead=%u penalty=%u recover_hp=%u until=%u "
           "exit=4/6+type3+4/7 then 4/8+4/11+4/9%s\n",
           observer->clientId, duel->serial, battleHp, duel->hpMax[observerIndex],
           observer->sparExitWasDead ? 1u : 0u,
           observer->sparExitPenaltyApplied ? 1u : 0u,
           observer->sparExitRecoverHp,
           observer->sparExitRedeliverUntilTick,
           observer->sparExitWasDead ? " (+death-penalty)" : " (+1exp win)");
}

static void vm_mock_service_duel_arm_both_exits(vm_mock_service_duel *duel)
{
    if (duel == NULL)
        return;
    vm_mock_service_duel_arm_observer_exit(duel, 0);
    vm_mock_service_duel_arm_observer_exit(duel, 1);
}

static void vm_mock_service_duel_cancel_for_client(u32 clientId,
                                                    const char *reason)
{
    int index = -1;
    int peerIndex = -1;
    vm_mock_service_duel *duel = vm_mock_service_duel_find_for_client(clientId,
                                                                      &index);
    u8 peerBit = 0;
    u8 startedMask = 0;

    if (duel == NULL || index < 0)
        return;
    peerIndex = 1 - index;
    peerBit = (u8)(1u << peerIndex);
    startedMask = duel->startedMask;
    printf("[info][mock-service] duel_cancel serial=%u client=%08x peer=%08x "
           "started=%02x finished=%u reason=%s\n",
           duel->serial, clientId, duel->clientIds[peerIndex],
           startedMask, duel->finished ? 1u : 0u,
           reason ? reason : "cancel");

    if (duel->finished)
    {
        /* Departing seat drops; keep peer terminal if still owed. */
        duel->terminalPendingMask = (u8)(duel->terminalPendingMask & peerBit);
        duel->startPendingMask = 0;
        vm_mock_service_duel_release_if_done(duel);
        return;
    }

    /*
     * Pre-start invite/slot cancel: neither seat entered Battle.cbm — drop
     * without death penalty.
     */
    if (startedMask == 0)
    {
        memset(duel, 0, sizeof(*duel));
        return;
    }

    /*
     * Mid-spar disconnect / quit: departing seat forfeits (HP=0), peer wins.
     * Same death penalty as a normal spar kill for the loser.
     */
    duel->hp[index] = 0;
    duel->finished = true;
    duel->startPendingMask = 0;
    duel->roundCommitMask = 0;
    duel->roundCommitOperate[0] = 0;
    duel->roundCommitOperate[1] = 0;
    memset(duel->events, 0, sizeof(duel->events));
    vm_mock_service_duel_arm_observer_exit(duel, index);
    vm_mock_service_duel_arm_observer_exit(duel, peerIndex);
    duel->terminalPendingMask = (u8)(startedMask & peerBit);
    duel->terminalNotBeforeTick = g_schedulerTick;
    printf("[info][mock-service] duel_forfeit serial=%u loser=%08x winner=%08x "
           "peer_terminal=%02x reason=%s evidence=offline-quit-lose+PvE-death\n",
           duel->serial, clientId, duel->clientIds[peerIndex],
           duel->terminalPendingMask, reason ? reason : "cancel");
    vm_mock_service_duel_release_if_done(duel);
}

static void vm_mock_service_session_cancel_shop_return_loading_clear(
    vm_mock_service_client_session *session,
    const char *reason);
static void vm_mock_service_session_cancel_shop_return_kind2_reenter(
    vm_mock_service_client_session *session,
    const char *reason);
static void vm_mock_service_session_cancel_shop_return_npc_catalog(
    vm_mock_service_client_session *session,
    const char *reason);
static void vm_mock_service_session_arm_shop_return_kind2_reenter(
    vm_mock_service_client_session *session,
    const char *scene,
    const char *reason);
static void vm_mock_service_session_arm_shop_return_busy_ack(
    vm_mock_service_client_session *session);
static void vm_mock_service_session_note_dream_shop_return_kind2_skip(
    vm_mock_service_client_session *session,
    const char *scene);
static void vm_mock_service_session_cancel_map_actor_vitals_sync(
    vm_mock_service_client_session *session,
    const char *reason);
static void vm_mock_service_session_cancel_map_stone_loading_clear(
    vm_mock_service_client_session *session,
    const char *reason);
static u8 vm_net_mock_scene_room_npc_seed_count(const char *scene);

/*
 * mmShop open/exit is not a map-stone transfer.  Leftover teleport lifecycle
 * bits (direct/deferred/map pending, last_scene_change_target_valid, armed
 * map-stone poll 30/2) after the session is already scene-ready hold
 * shop-return post-catalog 30/2 forever, then expire the clear → loading bar
 * stuck (传送后进商城返回卡死).
 */
static void vm_mock_service_clear_stale_map_transfer_for_shop(
    vm_mock_service_client_session *session,
    const char *scene,
    const char *reason)
{
    bool sceneReady = false;
    bool hadTeleport = false;
    bool hadMapStoneClear = false;

    if (session == NULL)
        return;
    sceneReady = session->sceneVisibleReady &&
                 !session->sceneVisiblePending &&
                 (scene == NULL ||
                  !vm_net_mock_scene_name_is_safe(scene) ||
                  !vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) ||
                  vm_net_mock_scene_names_equal_loose(session->sceneVisibleScene,
                                                      scene));
    if (!sceneReady)
        return;

    hadTeleport =
        g_vm_net_mock_teleport_stone_deferred_enter_valid ||
        g_vm_net_mock_teleport_stone_direct_enter_pending ||
        g_vm_net_mock_teleport_stone_map_enter_pending ||
        g_vm_net_mock_last_scene_change_target_valid;
    hadMapStoneClear = session->mapStoneLoadingClearPending;
    if (!hadTeleport && !hadMapStoneClear)
        return;

    printf("[info][mock-service] stale_map_transfer_clear client=%08x "
           "scene=%s reason=%s deferred=%u deferred_owner=%08x direct=%u map=%u target=%u "
           "map_stone_clear=%u map_stone_remaining=%u "
           "evidence=mmShop-scene-already-ready\n",
           session->clientId,
           (scene && scene[0]) ? scene
                               : (session->sceneVisibleScene[0]
                                      ? session->sceneVisibleScene
                                      : "-"),
           reason ? reason : "-",
           g_vm_net_mock_teleport_stone_deferred_enter_valid ? 1u : 0u,
           g_vm_net_mock_teleport_stone_deferred_enter_client_id,
           g_vm_net_mock_teleport_stone_direct_enter_pending ? 1u : 0u,
           g_vm_net_mock_teleport_stone_map_enter_pending ? 1u : 0u,
           g_vm_net_mock_last_scene_change_target_valid ? 1u : 0u,
           hadMapStoneClear ? 1u : 0u,
           hadMapStoneClear ? (u32)session->mapStoneLoadingClearRemaining : 0u);

    /* Do not erase another client's armed map-stone 30/1. */
    if (g_vm_net_mock_teleport_stone_deferred_enter_client_id == 0 ||
        g_vm_net_mock_teleport_stone_deferred_enter_client_id == session->clientId)
    {
        g_vm_net_mock_teleport_stone_deferred_enter_valid = false;
        g_vm_net_mock_teleport_stone_deferred_enter_tick = 0;
        g_vm_net_mock_teleport_stone_deferred_enter_client_id = 0;
    }
    g_vm_net_mock_teleport_stone_direct_enter_pending = false;
    g_vm_net_mock_teleport_stone_map_enter_pending = false;
    g_vm_net_mock_teleport_stone_subtype3_ack_sent = false;
    g_vm_net_mock_last_scene_change_target_valid = false;
    /*
     * Do NOT cancel unfinished map-stone poll 30/2 here.  shop-open with
     * remaining>0 dropped the last ResetDownloadState after map-stone→蓬莱
     * (2026-08-01: remaining=1 reason=shop-open), then shop-return 27/11
     * reopened DoLoading with no owed map-stone clear left → stuck bar.
     * Hold delivery while mmShopShellActive; resume after rehydrate.
     */
    if (hadMapStoneClear)
    {
        printf("[info][mock-service] map_stone_loading_clear_preserve "
               "client=%08x scene=%s remaining=%u reason=%s "
               "evidence=finish-ResetDownloadState-after-mmShop\n",
               session->clientId,
               session->mapStoneLoadingClearScene[0]
                   ? session->mapStoneLoadingClearScene
                   : "-",
               (u32)session->mapStoneLoadingClearRemaining,
               reason ? reason : "-");
    }
}

/* After poll 30/2 / moveinfo-live: let ScreenInit settle before kind-2 30/1. */
#define VM_MOCK_SHOP_RETURN_KIND2_DELAY_TICKS 8u

static bool vm_mock_service_mmshop_shell_is_active(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    return session != NULL && session->mmShopShellActive;
}

/* Returns 1-based ack ordinal while shop shell is active; 0 if inactive. */
static u32 vm_mock_service_shop_actor_query_ack_ordinal(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->mmShopShellActive)
        return 0;
    if (session->shopActorQueryAckCount < 255)
        session->shopActorQueryAckCount++;
    return session->shopActorQueryAckCount;
}

/*
 * First open14 family-4 already cleared loading.  Seed ack=1 so the exit
 * 0x162C query is ordinal>=2 and may carry 1/1/14 loading-clear
 * (session field comment: first ack status+money; later acks actor clear).
 */
static void vm_mock_service_shop_actor_query_seed_first_open_ack(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->mmShopShellActive)
        return;
    if (session->shopActorQueryAckCount < 1)
        session->shopActorQueryAckCount = 1;
}

/* Trailing open14 only after scene-combo mark (not sole open14→exit). */
static int vm_mock_service_mmshop_open14_is_combo_trailing(u32 ackOrd)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 shellAge = 0;

    if (session == NULL || !session->mmShopShellActive)
        return 0;
    if (session->mmShopOpenMarkSource != 1 || ackOrd != 1)
        return 0;
    /*
     * post-fix14: exit was ack=1/markSrc=1 at shellAge=361 (enter open14
     * missing).  Combo trailing is only the near-arm first ack.
     */
    if (session->mmShopShellArmedTick != 0 &&
        g_schedulerTick >= session->mmShopShellArmedTick)
        shellAge = g_schedulerTick - session->mmShopShellArmedTick;
    if (shellAge > 90u)
        return 0;
    return 1;
}

static void vm_mock_service_shop_open14_trace(const char *line)
{
    FILE *fp;

    if (line == NULL || line[0] == 0)
        return;
    fp = fopen("d:/works/my/jianghu/CBE_EMU_pay/bin/shop_open14_trace.log", "a");
    if (fp == NULL)
        return;
    fprintf(fp, "%s\n", line);
    fclose(fp);
}

static void vm_mock_service_mark_shop_scene_npc_reseed_pending(const char *source)
{
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();
    const char *scene = vm_net_mock_current_scene_name();
    bool changed = false;
    bool firstOpen = false;

    if (session == NULL)
        return;
    if (!vm_net_mock_scene_name_is_safe(scene) &&
        session->sceneVisibleReady && !session->sceneVisiblePending &&
        vm_net_mock_scene_name_is_safe(session->sceneVisibleScene))
    {
        scene = session->sceneVisibleScene;
    }
    if (!vm_net_mock_scene_name_is_safe(scene))
        return;
    changed = !session->shopSceneNpcReseedPending ||
              session->shopSceneNpcReseedScene[0] == 0 ||
              !vm_net_mock_scene_names_equal_loose(session->shopSceneNpcReseedScene,
                                                   scene);
    session->shopSceneNpcReseedPending = true;
    snprintf(session->shopSceneNpcReseedScene,
             sizeof(session->shopSceneNpcReseedScene), "%s", scene);
    firstOpen = !session->mmShopShellActive;
    /* Only reset on inactive→active; re-mark (info14/page) must keep ack ordinal. */
    if (firstOpen)
    {
        session->shopActorQueryAckCount = 0;
        session->mmShopShellArmedTick = g_schedulerTick;
        if (source != NULL &&
            (strstr(source, "scene-interaction") != NULL ||
             strstr(source, "scene-interaction-followup") != NULL))
            session->mmShopOpenMarkSource = 1;
        else
            session->mmShopOpenMarkSource = 2;
        /*
         * Capture resource warmth once from THIS client's seed ledger.
         * Remakes (open14/info14) must not overwrite to 0.
         *
         * mmShop clears client type-21 for THIS player — clear the per-client
         * catalog ledger after capture so shop-return once_or_empty / force can
         * re-emit nonempty 27/11.  Leaving the ledger set made type27 return
         * empty-11 first (poison) then early-shell / poll-hold left NPCs missing
         * while the bar stayed clear (2026-08-05 user: 场景正常 NPC 不刷新).
         * Do not clear process-global seeded (multi-client).
         */
        session->shopReturnNpcResourcesWarm =
            vm_mock_service_session_scene_npc_catalog_seeded_for(session, scene);
        if (session->sceneNpcCatalogSeeded)
        {
            vm_mock_service_session_clear_scene_npc_catalog_seeded(
                session, "shop-open-mmShop-clears-type21");
        }
        printf("[info][mock-service] shop_return_resources_warm_capture "
               "client=%08x scene=%s warm=%u source=%s "
               "evidence=per-client-warm-then-clear-ledger-for-reemit\n",
               session->clientId, scene,
               session->shopReturnNpcResourcesWarm ? 1u : 0u,
               source ? source : "-");
    }
    session->mmShopShellActive = true;
    /*
     * Hold login wear drip while mmShop is up; poll builder already no-ops
     * when mmShopShellActive.  Do not cancel — resume after shop-return.
     */
    /*
     * Stale coolmall unstuck / overnight peel can leave waitMap armed and
     * steal exit into kind2 DoLoading.  Normal 江湖商城 open owns the shell.
     */
    session->coolmallOpenWaitMap = false;
    /*
     * Opening mmShop proves the map shell is walkable enough for the client.
     * Drop leftover map-stone / teleport lifecycle so exit post-catalog 30/2
     * is not held as liveTeleport after a completed teleport.
     */
    vm_mock_service_clear_stale_map_transfer_for_shop(session, scene,
                                                     "shop-open");
    /*
     * Re-entering mmShop while a prior shop-return poll clear is still armed
     * injects lone 30/2 into the open storm (临安府 2026-07-27).  Drop it.
     */
    vm_mock_service_session_cancel_shop_return_loading_clear(session,
                                                            "shop-open");
    vm_mock_service_session_cancel_shop_return_kind2_reenter(session,
                                                            "shop-open");
    vm_mock_service_session_cancel_shop_return_npc_catalog(session,
                                                          "shop-open");
    session->shopReturnWarmNpcDelivered = false;
    session->shopReturnKind2Completed = false;
    session->shopReturnDreamNeedNonSceneBattle = false;
    session->shopReturnDreamNeedSettleBarClear = false;
    session->shopReturnDreamNeedHudRebuild = false;
    session->shopReturnDreamHudMapStoneHold = false;
    
    vm_mock_service_session_cancel_map_actor_vitals_sync(session,
                                                        "shop-open");
    if (changed)
    {
        printf("[info][mock-service] scene_npc_reseed_arm client=%08x scene=%s trigger=shop-open source=%s delivery=next-scene-followup\n",
               session->clientId, scene, source ? source : "-");
    }
}

static bool vm_mock_service_shop_scene_npc_reseed_matches(const char *scene)
{
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();

    return session != NULL &&
           session->shopSceneNpcReseedPending &&
           session->shopSceneNpcReseedScene[0] != 0 &&
           vm_net_mock_scene_name_is_safe(scene) &&
           vm_net_mock_scene_names_equal_loose(session->shopSceneNpcReseedScene,
                                               scene);
}

static int vm_mock_service_trade_client_index(const vm_mock_service_trade *trade,
                                              u32 clientId)
{
    if (trade == NULL || !trade->used || clientId == 0)
        return -1;
    if (trade->clientIds[0] == clientId)
        return 0;
    if (trade->clientIds[1] == clientId)
        return 1;
    return -1;
}

static vm_mock_service_trade *vm_mock_service_trade_find_for_client(u32 clientId,
                                                                    int *indexOut)
{
    if (indexOut)
        *indexOut = -1;
    for (u32 i = 0; i < VM_MOCK_SERVICE_TRADE_MAX; ++i)
    {
        int index = vm_mock_service_trade_client_index(&g_vm_mock_service_trades[i],
                                                       clientId);
        if (index >= 0)
        {
            if (indexOut)
                *indexOut = index;
            return &g_vm_mock_service_trades[i];
        }
    }
    return NULL;
}

static void vm_mock_service_trade_release_if_delivered(vm_mock_service_trade *trade)
{
    if (trade != NULL && trade->used && !trade->active &&
        trade->offerPendingMask == 0 && trade->terminalPendingMask == 0)
    {
        memset(trade, 0, sizeof(*trade));
    }
}

static vm_mock_service_trade *vm_mock_service_trade_begin(
    vm_mock_service_client_session *first,
    vm_mock_service_client_session *second)
{
    vm_mock_service_trade *slot = NULL;

    if (first == NULL || second == NULL || first == second ||
        first->clientId == 0 || second->clientId == 0 ||
        first->onlineRoleId == 0 || second->onlineRoleId == 0)
    {
        return NULL;
    }
    for (u32 i = 0; i < VM_MOCK_SERVICE_TRADE_MAX; ++i)
    {
        vm_mock_service_trade *trade = &g_vm_mock_service_trades[i];
        int firstIndex = vm_mock_service_trade_client_index(trade, first->clientId);
        int secondIndex = vm_mock_service_trade_client_index(trade, second->clientId);
        if (firstIndex >= 0 || secondIndex >= 0)
            return NULL;
        if (slot == NULL && !trade->used)
            slot = trade;
    }
    if (slot == NULL)
        return NULL;
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->active = true;
    slot->clientIds[0] = first->clientId;
    slot->clientIds[1] = second->clientId;
    printf("[info][mock-service] trade_session_begin first=%08x/%u second=%08x/%u\n",
           first->clientId, first->onlineRoleId,
           second->clientId, second->onlineRoleId);
    return slot;
}

static void vm_mock_service_trade_set_terminal(vm_mock_service_trade *trade,
                                               u8 subtype,
                                               u8 result,
                                               u8 pendingMask)
{
    if (trade == NULL || !trade->used)
        return;
    trade->active = false;
    trade->confirmedMask = 0;
    trade->offerPendingMask = 0;
    trade->terminalSubtype = subtype;
    trade->terminalResult = result;
    trade->terminalPendingMask = (u8)(pendingMask & 3u);
    vm_mock_service_trade_release_if_delivered(trade);
}

static void vm_mock_service_trade_cancel_for_client(u32 clientId, const char *reason)
{
    int index = -1;
    vm_mock_service_trade *trade = vm_mock_service_trade_find_for_client(clientId, &index);
    u32 peerClientId = 0;

    if (trade == NULL || index < 0)
        return;
    peerClientId = trade->clientIds[1 - index];
    printf("[info][mock-service] trade_session_cancel client=%08x peer=%08x reason=%s\n",
           clientId, peerClientId, reason ? reason : "cancel");
    if (trade->active)
        vm_mock_service_trade_set_terminal(trade, 7, 2, (u8)(1u << (1 - index)));
    else
    {
        trade->terminalPendingMask &= (u8)~(1u << index);
        vm_mock_service_trade_release_if_delivered(trade);
    }
}

static vm_net_mock_role_state *vm_mock_service_trade_role_for_session(
    const vm_mock_service_client_session *session,
    vm_mock_service_account_state **accountOut)
{
    vm_mock_service_account_state *account = NULL;
    vm_net_mock_role_db_file *database = NULL;

    if (accountOut)
        *accountOut = NULL;
    if (session == NULL || session->accountId[0] == 0 || session->onlineRoleId == 0)
        return NULL;
    account = vm_mock_service_account_find_or_create(session->accountId);
    if (account == NULL)
        return NULL;
    if (!vm_mock_service_account_ensure_role_db_loaded(account))
        return NULL;
    if (account == g_vm_mock_service_active_account)
    {
        if (!g_vm_net_mock_role_db_valid)
            return NULL;
        database = &g_vm_net_mock_role_db;
    }
    else
    {
        database = &account->roleDb;
    }
    for (u32 i = 0; i < database->roleCount; ++i)
    {
        if (database->roles[i].roleId == session->onlineRoleId)
        {
            if (accountOut)
                *accountOut = account;
            return &database->roles[i];
        }
    }
    return NULL;
}

static bool vm_mock_service_trade_role_add_item(
    vm_net_mock_role_state *role,
    u32 itemId,
    u32 count,
    u16 enhanceLevel,
    const vm_net_mock_enhance_wash_state *wash,
    u32 enhanceInstanceId,
    u16 *destinationSeqOut)
{
    u32 itemCount = 0;
    bool isEquipment = false;

    if (destinationSeqOut)
        *destinationSeqOut = 0;
    if (role == NULL || itemId == 0 || count == 0)
        return false;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    isEquipment = vm_net_mock_find_equipment_catalog_item(itemId) != NULL;
    if (!isEquipment)
    {
        for (u32 i = 0; i < itemCount; ++i)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
            if (item->itemId != itemId)
                continue;
            if (0xffffffffu - item->count < count)
                return false;
            item->count += count;
            if (destinationSeqOut)
                *destinationSeqOut = item->seq;
            return true;
        }
    }
    if (isEquipment)
    {
        u32 freeSlots = role->backpackCapacity > itemCount
                            ? (u32)role->backpackCapacity - itemCount
                            : 0;
        u16 firstSeq = 0;

        if (count > freeSlots ||
            count > (u32)(VM_NET_MOCK_BACKPACK_MAX_ITEMS - itemCount))
            return false;
        for (u32 unit = 0; unit < count; ++unit)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount + unit];
            memset(item, 0, sizeof(*item));
            item->itemId = itemId;
            item->seq = role->nextBackpackSeq ? role->nextBackpackSeq : 1;
            item->count = 1;
            item->enhanceLevel = enhanceLevel;
            /* Keep traded instance identity (and first-obtain wash) — do not
             * re-seed.  Counter refresh after commit avoids later collisions. */
            item->enhanceInstanceId = vm_net_mock_role_ensure_equipment_instance_id(
                role, enhanceInstanceId, itemId);
            vm_net_mock_enhance_wash_copy(&item->enhanceWash, wash);
            if (firstSeq == 0)
                firstSeq = item->seq;
            role->nextBackpackSeq = (u16)(item->seq + 1);
            if (role->nextBackpackSeq == 0)
                role->nextBackpackSeq = 1;
        }
        role->backpackItemCount = (u16)(itemCount + count);
        if (destinationSeqOut)
            *destinationSeqOut = firstSeq;
        return true;
    }
    if (itemCount >= role->backpackCapacity ||
        itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
    {
        return false;
    }
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount];
        memset(item, 0, sizeof(*item));
        item->itemId = itemId;
        item->seq = role->nextBackpackSeq ? role->nextBackpackSeq : 1;
        item->count = count;
        item->enhanceLevel = enhanceLevel;
        vm_net_mock_enhance_wash_copy(&item->enhanceWash, wash);
        role->backpackItemCount = (u16)(itemCount + 1);
        role->nextBackpackSeq = (u16)(item->seq + 1);
        if (role->nextBackpackSeq == 0)
            role->nextBackpackSeq = 1;
        if (destinationSeqOut)
            *destinationSeqOut = item->seq;
    }
    return true;
}

static bool vm_mock_service_trade_account_hex(const char *accountId,
                                              char *hexOut,
                                              size_t hexOutCap)
{
    size_t len = accountId ? strlen(accountId) : 0;
    if (len == 0 || len >= 64 || hexOut == NULL ||
        vm_mysql_hex_encode(accountId, len, hexOut, hexOutCap) == 0)
    {
        return false;
    }
    return true;
}

static bool vm_mock_service_trade_persist_pair(
    const vm_mock_service_client_session *sessions[2],
    const vm_net_mock_role_state roles[2])
{
    char accountHex[2][129];
    char query[1024];
    char *bulkQuery = NULL;
    size_t bulkCapacity = 131072;
    size_t bulkLen = 0;
    u32 bulkRows = 0;
    bool transactionStarted = false;
    bool ok = false;
    const char *stage = "prepare";

    if (sessions == NULL || sessions[0] == NULL || sessions[1] == NULL)
        return false;
    if (!vm_mock_service_trade_account_hex(sessions[0]->accountId,
                                           accountHex[0], sizeof(accountHex[0])) ||
        !vm_mock_service_trade_account_hex(sessions[1]->accountId,
                                           accountHex[1], sizeof(accountHex[1])))
    {
        return false;
    }
    /* Same columns as role_db_save backpack insert — omitting enhance_wash
     * wiped first-obtain / washed milestone attrs after trade_commit. */
    if (!vm_net_mock_role_equipment_ensure_enhance_column())
        return false;
    bulkQuery = (char *)malloc(bulkCapacity);
    stage = "start";
    if (bulkQuery == NULL || !vm_mysql_exec("START TRANSACTION"))
        goto done;
    transactionStarted = true;
    for (u32 side = 0; side < 2; ++side)
    {
        snprintf(query, sizeof(query),
                 "UPDATE account_roles SET money=%u,backpack_item_count=%u,next_backpack_seq=%u "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
                 roles[side].money, roles[side].backpackItemCount,
                 roles[side].nextBackpackSeq,
                 accountHex[side], roles[side].roleId);
        stage = side == 0 ? "money-first" : "money-second";
        if (!vm_mysql_exec(query))
            goto done;
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
                 accountHex[side], roles[side].roleId);
        stage = side == 0 ? "backpack-delete-first" : "backpack-delete-second";
        if (!vm_mysql_exec(query))
            goto done;
    }
    bulkLen = (size_t)snprintf(
        bulkQuery, bulkCapacity,
        "INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,"
        "item_seq,item_count,enhance_level,item_instance_id,enhance_wash) VALUES");
    for (u32 side = 0; side < 2; ++side)
    {
        u32 count = vm_net_mock_role_backpack_count(&roles[side]);
        for (u32 slot = 0; slot < count; ++slot)
        {
            const vm_net_mock_backpack_item_state *item = &roles[side].backpackItems[slot];
            char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
            int written = 0;
            if (item->itemId == 0 || item->seq == 0 || item->count == 0)
                continue;
            (void)vm_net_mock_enhance_wash_sql_text(&item->enhanceWash, washText,
                                                    sizeof(washText));
            written = snprintf(
                bulkQuery + bulkLen, bulkCapacity - bulkLen,
                "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u,%u,'%s')",
                bulkRows ? "," : "", accountHex[side], roles[side].roleId,
                slot, item->itemId, item->seq, item->count,
                item->enhanceLevel, item->enhanceInstanceId, washText);
            if (written < 0 || (size_t)written >= bulkCapacity - bulkLen)
                goto done;
            bulkLen += (size_t)written;
            ++bulkRows;
        }
    }
    stage = "backpack-insert";
    if (bulkRows != 0 && !vm_mysql_exec(bulkQuery))
        goto done;
    stage = "commit";
    if (!vm_mysql_exec("COMMIT"))
        goto done;
    transactionStarted = false;
    ok = true;

done:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    if (!ok)
    {
        printf("[error][mock-service] trade_mysql_commit_failed stage=%s rows=%u query_len=%u error=%s\n",
               stage, bulkRows, (u32)bulkLen, vm_mysql_last_error());
    }
    free(bulkQuery);
    return ok;
}

static const char *vm_mock_service_social_notice_name(u8 type)
{
    switch (type)
    {
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_INVITE:
        return "friend-invite";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_INVITE:
        return "trade-invite";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_FRIEND_RESULT:
        return "friend-result";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TRADE_RESULT:
        return "trade-result";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_INVITE:
        return "team-invite";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_RESULT:
        return "team-result";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_MEMBER_JOIN:
        return "team-member-join";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_LEAVE:
        return "team-leave";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_HSP:
        return "team-hsp";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED:
        return "guild-application-approved";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_REJECTED:
        return "guild-application-rejected";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_INVITE:
        return "spar-invite";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_RESULT:
        return "spar-result";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_INVITE:
        return "guild-invite";
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_INVITE_RESULT:
        return "guild-invite-result";
    default:
        return "unknown";
    }
}

/* Defined with roster builders in mock_server_scene_task.c; needed here so
 * social-notice enqueue can freeze the observer-scoped leave/join wire id. */
static u32 vm_mock_service_team_member_wire_id(
    const vm_mock_service_client_session *observer,
    const vm_mock_service_client_session *member);

static bool vm_mock_service_session_enqueue_social_notice(
    vm_mock_service_client_session *target,
    u8 type,
    u8 result,
    const vm_mock_service_client_session *source,
    const vm_net_mock_role_state *sourceRole,
    const char *sourceAccountId)
{
    vm_mock_service_social_notice *slot = NULL;
    u32 sourceRoleId = 0;
    u16 sourceLevel = 1;
    u8 sourceJob = 1;
    u8 sourceSex = 0;
    const char *sourceName = NULL;

    if (sourceRole != NULL)
    {
        sourceRoleId = sourceRole->roleId;
        sourceLevel = (u16)(sourceRole->level ? sourceRole->level : 1);
        sourceJob = sourceRole->job ? sourceRole->job : 1;
        sourceSex = sourceRole->sex <= 1 ? sourceRole->sex : 0;
        /* Presence capture already mirrored role->name as GBK onlineRoleName.
         * Prefer that copy so invite/result toasts match the map name plate. */
        sourceName = (source != NULL && source->onlineRoleName[0]) ?
                     source->onlineRoleName : sourceRole->name;
    }
    else if (source != NULL)
    {
        sourceRoleId = source->onlineRoleId;
        sourceLevel = source->onlineLevel ? source->onlineLevel : 1;
        sourceJob = source->onlineJob ? source->onlineJob : 1;
        sourceSex = source->onlineSex <= 1 ? source->onlineSex : 0;
        sourceName = source->onlineRoleName;
    }

    if (target == NULL || source == NULL ||
        type == VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE || sourceRoleId == 0)
    {
        return false;
    }
    for (u32 i = 0; i < VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX; ++i)
    {
        vm_mock_service_social_notice *entry = &target->socialNotices[i];
        if (entry->type == type && entry->sourceClientId == source->clientId &&
            entry->sourceRoleId == sourceRoleId)
        {
            /* Duplicate button presses must not create multiple modal prompts. */
            return true;
        }
        if (slot == NULL && entry->type == VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE)
            slot = entry;
    }
    if (slot == NULL)
    {
        printf("[warn][mock-service] social_notice_drop target=%08x action=%s source=%08x/%u reason=queue-full\n",
               target->clientId,
               vm_mock_service_social_notice_name(type),
               source->clientId,
               sourceRoleId);
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    slot->type = type;
    slot->result = result;
    slot->sourceClientId = source->clientId;
    slot->sourceRoleId = sourceRoleId;
    slot->sourceWireId = vm_mock_service_team_member_wire_id(target, source);
    slot->sourceLevel = sourceLevel;
    slot->sourceJob = sourceJob;
    slot->sourceSex = sourceSex;
    snprintf(slot->sourceAccountId, sizeof(slot->sourceAccountId), "%s",
             sourceAccountId && sourceAccountId[0] ? sourceAccountId : source->accountId);
    /* Prefer the live presence name (already GBK from role capture).  Role DB
     * name is the fallback; copy on GBK character boundaries so a mid-pair cut
     * cannot poison the invite confirm dialog. */
    {
        const char *rawName = sourceName && sourceName[0] ? sourceName :
                              (source->onlineRoleName[0] ? source->onlineRoleName : "Player");
        size_t srcLen = strlen(rawName);
        size_t offset = 0;
        size_t outLen = 0;
        const size_t outCap = sizeof(slot->sourceName) - 1;

        while (offset < srcLen && outLen < outCap)
        {
            unsigned char lead = (unsigned char)rawName[offset];
            size_t charLen = 1;

            if (lead >= 0x80u)
            {
                if (offset + 1 >= srcLen)
                    break;
                charLen = 2;
            }
            if (outLen + charLen > outCap)
                break;
            memcpy(slot->sourceName + outLen, rawName + offset, charLen);
            outLen += charLen;
            offset += charLen;
        }
        slot->sourceName[outLen] = 0;
        if (slot->sourceName[0] == 0)
            snprintf(slot->sourceName, sizeof(slot->sourceName), "%s", "Player");
    }
    slot->queuedTick = g_schedulerTick;
    printf("[info][mock-service] social_notice_queue target=%08x action=%s source=%08x/%u wire=%u name=%s result=%u\n",
           target->clientId,
           vm_mock_service_social_notice_name(type),
           source->clientId,
           sourceRoleId,
           slot->sourceWireId,
           slot->sourceName,
           result);
    return true;
}

static const char *vm_mock_service_chat_type_name(u8 type)
{
    switch (type)
    {
    case VM_MOCK_CHAT_TYPE_WORLD:
        return "world";
    case VM_MOCK_CHAT_TYPE_GUILD:
        return "guild";
    case VM_MOCK_CHAT_TYPE_SYSTEM:
        return "system";
    case VM_MOCK_CHAT_TYPE_LOCAL:
        return "local";
    case VM_MOCK_CHAT_TYPE_TEAM:
        return "team";
    case VM_MOCK_CHAT_TYPE_PRIVATE:
        return "private";
    case VM_MOCK_CHAT_TYPE_TEAM_NOTICE:
        return "team-notice";
    default:
        return "unknown";
    }
}

static bool vm_mock_service_session_enqueue_chat_notice_identity(
    vm_mock_service_client_session *target,
    u8 type,
    u32 sourceClientId,
    u32 sourceRoleId,
    const char *sourceName,
    const char *message)
{
    vm_mock_service_chat_notice *slot = NULL;
    u8 slotIndex = 0;

    if (target == NULL || message == NULL || message[0] == 0 ||
        (type != VM_MOCK_CHAT_TYPE_WORLD &&
         (type < VM_MOCK_CHAT_TYPE_TEAM || type > VM_MOCK_CHAT_TYPE_TEAM_NOTICE)))
    {
        return false;
    }
    if (target->chatNoticeCount >= VM_MOCK_SERVICE_CHAT_NOTICE_MAX)
    {
        printf("[warn][mock-service] chat_notice_drop target=%08x type=%s reason=queue-full\n",
               target->clientId, vm_mock_service_chat_type_name(type));
        return false;
    }

    slotIndex = (u8)((target->chatNoticeHead + target->chatNoticeCount) %
                     VM_MOCK_SERVICE_CHAT_NOTICE_MAX);
    slot = &target->chatNotices[slotIndex];
    memset(slot, 0, sizeof(*slot));
    slot->valid = true;
    slot->type = type;
    slot->sourceClientId = sourceClientId;
    slot->sourceRoleId = sourceRoleId;
    snprintf(slot->sourceName, sizeof(slot->sourceName), "%s",
             sourceName && sourceName[0] ? sourceName : "System");
    snprintf(slot->message, sizeof(slot->message), "%s", message);
    slot->queuedTick = g_schedulerTick;
    slot->notBeforeTick = 0;
    ++target->chatNoticeCount;
    printf("[info][mock-service] chat_notice_queue target=%08x type=%s source=%08x/%u bytes=%u depth=%u\n",
           target->clientId,
           vm_mock_service_chat_type_name(type),
           slot->sourceClientId,
           slot->sourceRoleId,
           (u32)strlen(slot->message),
           target->chatNoticeCount);
    return true;
}

static bool vm_mock_service_session_enqueue_chat_notice(
    vm_mock_service_client_session *target,
    u8 type,
    const vm_mock_service_client_session *source,
    const char *sourceName,
    const char *message)
{
    return vm_mock_service_session_enqueue_chat_notice_identity(
        target,
        type,
        source ? source->clientId : 0,
        source ? source->onlineRoleId : 0,
        sourceName && sourceName[0] ? sourceName :
            (source && source->onlineRoleName[0] ? source->onlineRoleName : "System"),
        message);
}

static bool vm_mock_service_session_enqueue_system_message(
    vm_mock_service_client_session *target,
    const char *message)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    return vm_mock_service_session_enqueue_chat_notice(
        target, VM_MOCK_CHAT_TYPE_SYSTEM, NULL, systemNameGbk, message);
}

static void vm_net_mock_notify_flask_reservoir_system_tip(u32 itemId,
                                                          u32 remaining)
{
    char tip[48];
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    (void)itemId;
    snprintf(tip, sizeof(tip),
             "\xb5\xb1\xc7\xb0\xb4\xa2\xc1\xbf\xa3\xba%u/%u",
             remaining, VM_NET_MOCK_FLASK_MAX_RESERVOIR); /* 当前储量：N/MAX */
    if (session != NULL &&
        vm_mock_service_session_enqueue_system_message(session, tip))
    {
        printf("[info][network] mock_flask_reservoir_tip item=%u remaining=%u/%u "
               "delivery=1/3/3-system-chat-queued\n",
               itemId, remaining, VM_NET_MOCK_FLASK_MAX_RESERVOIR);
    }
}

/*
 * 7/29 quote + 7/13 confirm (快速修理 / 单件修理).
 *
 * Request 7/29: JianghuOL.CBE:0x0101CADC → 1/7/29 {type[,seq,id]}.
 * HandleRepairResponse(0x01028C9A) shows「您身上有 N 件…花费 M 币」using
 * repairnum=piece COUNT and coolmoney=fee.  N must never be copper cost
 * (missing-durability sum); that produced「25455件装备」when cost≈25455.
 *
 * Confirm sends 1/7/13 (CBE:0x0101A8A0).  mmGame:0x4986 parses result:
 * result=1 → 修理完成 + clear wait; result=2 → insufficient funds.
 * Product fee: flat 5 酷宝.  Equipment 7/7 is poll-deferred after success.
 */
#define VM_NET_MOCK_QUICK_REPAIR_WCOIN_COST 5u

typedef struct vm_net_mock_equipment_repair_request
{
    u8 type;
    u16 seq;
    u32 itemId;
    bool hasSeq;
    bool hasItemId;
} vm_net_mock_equipment_repair_request;

static void vm_net_mock_quick_repair_clear_quote(
    vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->quickRepairQuotePending = false;
    session->quickRepairQuoteType = 0;
    session->quickRepairQuoteSeq = 0;
    session->quickRepairQuoteItemId = 0;
    session->quickRepairQuoteHasItemId = false;
    session->quickRepairQuoteTick = 0;
}

static void vm_net_mock_quick_repair_arm_quote(
    vm_mock_service_client_session *session,
    const vm_net_mock_equipment_repair_request *parsed)
{
    if (session == NULL || parsed == NULL)
        return;
    session->quickRepairQuotePending = true;
    session->quickRepairQuoteType = parsed->type;
    session->quickRepairQuoteSeq = parsed->hasSeq ? parsed->seq : 0;
    session->quickRepairQuoteItemId = parsed->hasItemId ? parsed->itemId : 0;
    session->quickRepairQuoteHasItemId = parsed->hasItemId;
    session->quickRepairQuoteTick = g_schedulerTick;
}

static bool vm_net_mock_parse_equipment_repair_request(
    const u8 *request,
    u32 requestLen,
    vm_net_mock_equipment_repair_request *parsedOut)
{
    u32 offset = 4;
    u32 typeValue = 0;
    u32 seqValue = 0;
    u32 itemId = 0;
    vm_net_mock_request_object object;
    vm_net_mock_equipment_repair_request parsed;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));
    if (request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 7 ||
        object.subtype != 29 || object.payloadLen == 0)
    {
        return false;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             "type", &typeValue) ||
        (typeValue != 1 && typeValue != 2))
    {
        return false;
    }
    parsed.type = (u8)typeValue;
    if (typeValue == 2)
    {
        if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                 "seq", &seqValue) ||
            seqValue == 0 || seqValue > 0xffffu)
        {
            return false;
        }
        parsed.seq = (u16)seqValue;
        parsed.hasSeq = true;
        if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                "id", &itemId))
        {
            parsed.itemId = itemId;
            parsed.hasItemId = true;
        }
    }
    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_parse_equipment_repair_confirm_request(
    const u8 *request,
    u32 requestLen,
    vm_net_mock_equipment_repair_request *parsedOut)
{
    u32 offset = 4;
    u32 typeValue = 0;
    u32 seqValue = 0;
    u32 itemId = 0;
    vm_net_mock_request_object object;
    vm_net_mock_equipment_repair_request parsed;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));
    if (request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        object.major != 1 || object.kind != 7 || object.subtype != 13)
    {
        return false;
    }
    /* Confirm may carry type/flag/seq/id (0x0101A8A0) or be nearly empty. */
    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                            "type", &typeValue) &&
        (typeValue == 1 || typeValue == 2))
    {
        parsed.type = (u8)typeValue;
    }
    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                            "seq", &seqValue) &&
        seqValue > 0 && seqValue <= 0xffffu)
    {
        parsed.seq = (u16)seqValue;
        parsed.hasSeq = true;
    }
    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                            "id", &itemId))
    {
        parsed.itemId = itemId;
        parsed.hasItemId = true;
    }
    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static u16 vm_net_mock_quick_repair_piece_count(
    vm_net_mock_role_service_state *serviceState,
    const vm_net_mock_role_state *role,
    const vm_net_mock_equipment_repair_request *parsed)
{
    u16 pieceCount = 0;

    if (role == NULL || parsed == NULL)
        return 0;
    /*
     * Count worn slots with durability deficit only.  Never use
     * repair_cost()'s copper return value — that sum (~25455) was once
     * written to 7/29.repairnum and shown as「25455件装备」.
     */
    if (parsed->type == 1)
    {
        if (serviceState == NULL)
            return 0;
        vm_net_mock_role_service_sync_equipment(serviceState, role);
        for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
        {
            if (role->equippedItemIds[slot] == 0 ||
                serviceState->durability[slot] >=
                    serviceState->durabilityMax[slot])
            {
                continue;
            }
            ++pieceCount;
        }
        return pieceCount;
    }
    if (!parsed->hasSeq)
        return 0;
    {
        u32 slot = (u32)parsed->seq - 1u;
        if (serviceState != NULL &&
            slot < VM_NET_MOCK_EQUIP_SLOT_COUNT &&
            role->equippedItemIds[slot] != 0 &&
            (!parsed->hasItemId ||
             role->equippedItemIds[slot] == parsed->itemId) &&
            serviceState->durability[slot] < serviceState->durabilityMax[slot])
        {
            return 1;
        }
    }
    return 0;
}

static u32 vm_net_mock_build_equipment_repair_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_net_mock_equipment_repair_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_service_state *serviceState = NULL;
    vm_mock_service_client_session *session = NULL;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    u16 pieceCount = 0;
    u32 wcoinBefore = 0;
    u16 coolmoneyOut = 0;
    const char *reason = "ok";

    memset(&parsed, 0, sizeof(parsed));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_equipment_repair_request(request, requestLen, &parsed))
    {
        return 0;
    }

    role = vm_net_mock_active_role();
    session = vm_mock_service_get_active_client_session();
    if (session != NULL)
        vm_net_mock_quick_repair_clear_quote(session);

    if (role == NULL)
    {
        reason = "no-role";
        pieceCount = 0;
    }
    else
    {
        serviceState = vm_net_mock_role_service_state_get(role);
        wcoinBefore = role->wcoin;
        pieceCount = vm_net_mock_quick_repair_piece_count(serviceState, role,
                                                          &parsed);
        if (pieceCount == 0)
            reason = "nothing-to-repair";
        else
        {
            reason = "quote";
            if (session != NULL)
                vm_net_mock_quick_repair_arm_quote(session, &parsed);
        }
    }

    /*
     * Quote: repairnum=piece count, coolmoney=5 酷宝 (never copper cost).
     * Fields are u32 — u16 mis-aligned HandleRepairResponse into "coolmoney"
     * name bytes (0x636F=25455 displayed as piece count).
     */
    coolmoneyOut =
        pieceCount > 0 ? (u16)VM_NET_MOCK_QUICK_REPAIR_WCOIN_COST : 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 29, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", parsed.type) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "repairnum",
                                    (u32)pieceCount) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "coolmoney",
                                    (u32)coolmoneyOut))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_equipment_repair phase=quote type=%u seq=%u "
           "id=%u repairnum=%u coolmoney=%u wcoin=%u quote_pending=%u "
           "resp=%u reason=%s evidence=JianghuOL.CBE:0x0101CADC/0x01028C9A+"
           "repairnum=u32-piece-count\n",
           parsed.type, parsed.seq, parsed.itemId, pieceCount,
           coolmoneyOut,
           wcoinBefore, session && session->quickRepairQuotePending ? 1 : 0,
           pos, reason);
    vm_autotest_note("mock_equipment_repair phase=quote type=%u repairnum=%u "
                     "coolmoney=%u reason=%s evidence=piece-count-not-cost\n",
                     parsed.type, pieceCount, coolmoneyOut, reason);
    return pos;
}

static u32 vm_net_mock_build_equipment_repair_confirm_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_net_mock_equipment_repair_request parsed;
    vm_net_mock_equipment_repair_request effective;
    vm_net_mock_role_state *role = NULL;
    vm_mock_service_client_session *session = NULL;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    u8 result = 2;
    u16 repaired = 0;
    u32 wcoinBefore = 0;
    u32 wcoinAfter = 0;
    bool executed = false;
    bool equipSyncArmed = false;
    const char *reason = "ok";

    memset(&parsed, 0, sizeof(parsed));
    memset(&effective, 0, sizeof(effective));
    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_equipment_repair_confirm_request(request, requestLen,
                                                            &parsed))
    {
        return 0;
    }

    role = vm_net_mock_active_role();
    session = vm_mock_service_get_active_client_session();
    /*
     * Only claim 1/7/13 when a 7/29 quote armed this session; otherwise leave
     * the request for other handlers (7/13 is not exclusive to quick repair).
     */
    if (session == NULL || !session->quickRepairQuotePending)
        return 0;
    if (role == NULL)
    {
        reason = "no-role";
        result = 2;
        vm_net_mock_quick_repair_clear_quote(session);
    }
    else
    {
        /*
         * Armed 7/29 quote owns the operation.  Confirm may carry type/flag=2
         * that is not quote type; do not let it replace type=1 all-repair.
         */
        effective.type = session->quickRepairQuoteType;
        effective.seq = session->quickRepairQuoteSeq;
        effective.hasSeq = session->quickRepairQuoteSeq != 0;
        effective.itemId = session->quickRepairQuoteItemId;
        effective.hasItemId = session->quickRepairQuoteHasItemId;
        if (effective.type == 2)
        {
            if (parsed.hasSeq)
            {
                effective.seq = parsed.seq;
                effective.hasSeq = true;
            }
            if (parsed.hasItemId)
            {
                effective.itemId = parsed.itemId;
                effective.hasItemId = true;
            }
        }

        wcoinBefore = role->wcoin;
        wcoinAfter = role->wcoin;
        if (wcoinBefore < VM_NET_MOCK_QUICK_REPAIR_WCOIN_COST)
        {
            reason = "wcoin-insufficient";
            result = 2;
        }
        else
        {
            bool repairedOk = false;

            if (effective.type == 1)
            {
                repairedOk = vm_net_mock_role_service_repair_all_free(
                    role, &repaired);
            }
            else if (effective.hasSeq)
            {
                repairedOk = vm_net_mock_role_service_repair_one_free(
                    role, effective.seq,
                    effective.hasItemId ? effective.itemId : 0, &repaired);
            }
            if (!repairedOk || repaired == 0)
            {
                reason = "repair-apply-failed";
                result = 2;
            }
            else
            {
                role->wcoin = wcoinBefore - VM_NET_MOCK_QUICK_REPAIR_WCOIN_COST;
                wcoinAfter = role->wcoin;
                vm_net_mock_role_mark_inventory_dirty("quick-repair-wcoin");
                executed = true;
                result = 1;
                reason = "repaired";
            }
        }
        vm_net_mock_quick_repair_clear_quote(session);
    }

    /* result is u8 (+0x4c); keep 7/13 for wait-clear and append 26/0. */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 13, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);

    {
        u32 ackObjectStart = 0;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 26, 0,
                                         &ackObjectStart))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, ackObjectStart, pos);
        objectCount = 2;
    }

    if (executed && session != NULL)
    {
        g_netMockBackpackGridSeededRoleId = 0;
        session->quickRepairEquipSyncPending = true;
        session->quickRepairEquipSyncPhase = 1;
        session->quickRepairEquipSyncTick = g_schedulerTick;
        equipSyncArmed = true;
        printf("[info][network] mock_equipment_repair equip-sync-arm "
               "client=%08x evidence=poll-7/7-type2-then-26/0\n",
               session->clientId);
    }

    /* mmGame tip is unreliable; surface completion via system chat. */
    if (session != NULL)
    {
        if (result == 1)
        {
            (void)vm_mock_service_session_enqueue_system_message(
                session,
                "\xd0\xde\xc0\xed\xcd\xea\xb3\xc9"); /* 修理完成 */
        }
        else if (strcmp(reason, "wcoin-insufficient") == 0)
        {
            (void)vm_mock_service_session_enqueue_system_message(
                session,
                "\xbf\xe1\xb1\xa6\xb2\xbb\xd7\xe3\xa3\xac\xce\xde\xb7\xa8\xd0\xde"
                "\xc0\xed\xa1\xa3"); /* 酷宝不足，无法修理。 */
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_equipment_repair phase=confirm type=%u seq=%u "
           "id=%u result=%u wcoin=%u/%u cost=%u executed=%u repaired=%u "
           "equip_sync_armed=%u resp=%u reason=%s "
           "evidence=JianghuOL.CBE:0x0101A8A0+mmGame:0x4986+"
           "quick-repair-5-wcoin+system-chat\n",
           effective.type, effective.seq, effective.itemId, result,
           wcoinBefore, wcoinAfter, VM_NET_MOCK_QUICK_REPAIR_WCOIN_COST,
           executed ? 1 : 0, repaired, equipSyncArmed ? 1 : 0, pos, reason);
    vm_autotest_note("mock_equipment_repair phase=confirm result=%u "
                     "wcoin=%u/%u executed=%u reason=%s "
                     "evidence=mmGame:0x4986\n",
                     result, wcoinBefore, wcoinAfter, executed ? 1 : 0, reason);
    return pos;
}


static bool vm_mock_service_session_presence_is_recent(
    const vm_mock_service_client_session *session);

/* Live world-channel fanout (type 0 / [世]).  Not persisted into
 * world_chat_messages; loot spam must not rewrite login history. */
static u32 vm_mock_service_broadcast_world_chat_live(
    const char *sourceName,
    const char *message)
{
    vm_mock_service_client_session *target = g_vm_mock_service_client_sessions;
    u32 recipients = 0;

    if (message == NULL || message[0] == 0)
        return 0;
    while (target != NULL)
    {
        if (target->roleOnline && target->onlinePresenceValid &&
            vm_mock_service_session_presence_is_recent(target) &&
            vm_mock_service_session_enqueue_chat_notice(
                target, VM_MOCK_CHAT_TYPE_WORLD, NULL, sourceName, message))
        {
            ++recipients;
        }
        target = target->next;
    }
    return recipients;
}

/*
 * World congratulate when enhance reaches +8 or higher (live [世] only;
 * same fanout as gold-chest rare loot, not persisted to login history).
 */
static void vm_net_mock_equipment_enhance_maybe_world_announce(u32 itemId,
                                                              u16 newLevel)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    const vm_net_mock_shop_catalog_item *shop = NULL;
    const vm_net_mock_role_state *role = NULL;
    vm_mock_service_client_session *session = NULL;
    const char *playerName = NULL;
    const char *itemName = NULL;
    char message[82];
    int wrote = 0;
    u32 recipients = 0;

    if (itemId == 0 || newLevel < 8)
        return;

    shop = vm_net_mock_find_shop_catalog_item(itemId);
    itemName = (shop != NULL && shop->name[0] != 0) ? shop->name : NULL;

    role = vm_net_mock_active_role();
    session = vm_mock_service_get_active_client_session();
    if (session != NULL && session->onlineRoleName[0] != 0)
        playerName = session->onlineRoleName;
    else if (role != NULL && role->name[0] != 0)
        playerName = role->name;
    if (playerName == NULL || playerName[0] == 0)
        return;

    /* 恭喜{玩家}将{装备}强化至+N，祝贺！ — 81-byte GBK wire limit. */
    memset(message, 0, sizeof(message));
    if (itemName != NULL)
    {
        wrote = snprintf(
            message, sizeof(message),
            "\xB9\xA7\xCF\xB2%s\xBD\xAB%s\xC7\xBF\xBB\xAF\xD6\xC1+%u"
            "\xA3\xAC\xD7\xA3\xBA\xD8\xA3\xA1",
            playerName, itemName, (u32)newLevel);
    }
    if (itemName == NULL || wrote <= 0 || (size_t)wrote >= sizeof(message))
    {
        /* 恭喜{玩家}强化成功，装备达到+N！ */
        memset(message, 0, sizeof(message));
        wrote = snprintf(
            message, sizeof(message),
            "\xB9\xA7\xCF\xB2%s\xC7\xBF\xBB\xAF\xB3\xC9\xB9\xA6\xA3\xAC"
            "\xD7\xB0\xB1\xB8\xB4\xEF\xB5\xBD+%u\xA3\xA1",
            playerName, (u32)newLevel);
    }
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
    {
        printf("[warn][network] mock_enhance_world_announce item=%u level=%u "
               "reason=message-overflow\n",
               itemId, (u32)newLevel);
        return;
    }

    recipients = vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][network] mock_enhance_world_announce item=%u level=%u "
           "player=%s recipients=%u evidence=1/3/3-type0-world enhance>=8\n",
           itemId, (u32)newLevel, playerName, recipients);
}

/* Server-wide marriage celebration buff: 2x wild battle rewards for 3 hours. */
enum
{
    VM_MOCK_MARRIAGE_WORLD_BUFF_SECONDS = 3u * 60u * 60u,
    VM_MOCK_MARRIAGE_WORLD_BUFF_MULT = 2u,
    VM_MOCK_MARRIAGE_WORLD_ANNOUNCE_TIMES = 3u
};

static u32 g_vm_mock_marriage_world_buff_expires_unix = 0;

static u32 vm_net_mock_marriage_world_buff_multiplier(void)
{
    u32 now = (u32)time(NULL);

    if (g_vm_mock_marriage_world_buff_expires_unix == 0)
        return 1u;
    if (now >= g_vm_mock_marriage_world_buff_expires_unix)
    {
        g_vm_mock_marriage_world_buff_expires_unix = 0;
        return 1u;
    }
    return VM_MOCK_MARRIAGE_WORLD_BUFF_MULT;
}

static void vm_mock_service_marriage_celebrate_and_arm_world_buff(
    const char *nameA,
    const char *nameB)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    char message[96];
    const char *a = (nameA != NULL && nameA[0] != 0) ? nameA : "Player";
    const char *b = (nameB != NULL && nameB[0] != 0) ? nameB : "Player";
    u32 now = (u32)time(NULL);
    u32 recipients = 0;
    u32 i = 0;
    int wrote = 0;

    g_vm_mock_marriage_world_buff_expires_unix =
        now + VM_MOCK_MARRIAGE_WORLD_BUFF_SECONDS;

    /* 恭喜{A}和{B}喜结连理 */
    memset(message, 0, sizeof(message));
    wrote = snprintf(
        message, sizeof(message),
        "\xb9\xa7\xcf\xb2%s\xba\xcd%s\xcf\xb2\xbd\xe1\xc1\xac\xc0\xed",
        a, b);
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
    {
        memset(message, 0, sizeof(message));
        wrote = snprintf(
            message, sizeof(message),
            "\xb9\xa7\xcf\xb2\xcf\xb2\xbd\xe1\xc1\xac\xc0\xed"); /* 恭喜喜结连理 */
    }
    if (wrote > 0 && (size_t)wrote < sizeof(message))
    {
        for (i = 0; i < VM_MOCK_MARRIAGE_WORLD_ANNOUNCE_TIMES; ++i)
            recipients +=
                vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    }

    printf("[info][mock-service] marriage_world_buff armed expires=%u mult=%u "
           "a=%s b=%s recipients=%u announce_times=%u "
           "evidence=marry-ok-world-3x+battle-reward-2x-3h\n",
           g_vm_mock_marriage_world_buff_expires_unix,
           VM_MOCK_MARRIAGE_WORLD_BUFF_MULT, a, b, recipients,
           VM_MOCK_MARRIAGE_WORLD_ANNOUNCE_TIMES);
}

static bool vm_net_mock_gold_chest_reward_warrants_world_announce(u32 itemId)
{
    static const char tianshuGbk[] = "\xCC\xEC\xCA\xE9"; /* 天书 */
    const vm_net_mock_equipment_catalog_item *equip = NULL;
    const vm_net_mock_shop_catalog_item *shop = NULL;

    equip = vm_net_mock_find_equipment_catalog_item(itemId);
    if (equip != NULL && equip->quality == 3)
        return true;

    shop = vm_net_mock_find_shop_catalog_item(itemId);
    if (shop == NULL)
        return false;

    /* 五级玄晶及以上: item 905..916 (一级=901 … 十六级=916). */
    if (shop->category == 23 &&
        itemId >= (u32)VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_FIRST + 4u &&
        itemId <= (u32)VM_NET_MOCK_EQUIP_ENHANCE_CRYSTAL_LAST)
    {
        return true;
    }

    /* 十倍经验卡 811 / 三十倍经验卡 845（分类 10）。 */
    if (itemId == 811 || itemId == 845)
        return true;

    /* 修炼天书 920/921，或名称含「天书」。 */
    if (itemId == 920 || itemId == 921)
        return true;
    if (shop->name[0] != 0 && strstr(shop->name, tianshuGbk) != NULL)
        return true;
    return false;
}

static void vm_net_mock_chest_announce_flush_pending(
    vm_mock_service_client_session *session)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    const vm_net_mock_shop_catalog_item *shop = NULL;
    const char *playerName = NULL;
    const char *itemName = NULL;
    const char *chestNameGbk = NULL;
    char message[82];
    int wrote = 0;
    u32 recipients = 0;
    u32 rewardItemId = 0;
    u32 chestItemId = 0;
    u32 count = 0;

    if (session == NULL || session->chestAnnounceCount == 0 ||
        session->chestAnnounceRewardItemId == 0)
    {
        return;
    }
    rewardItemId = session->chestAnnounceRewardItemId;
    chestItemId = session->chestAnnounceChestItemId;
    count = session->chestAnnounceCount;
    session->chestAnnounceRewardItemId = 0;
    session->chestAnnounceChestItemId = 0;
    session->chestAnnounceCount = 0;
    session->chestAnnounceFlushTick = 0;

    shop = vm_net_mock_find_shop_catalog_item(rewardItemId);
    itemName = (shop != NULL && shop->name[0] != 0) ? shop->name : NULL;
    if (itemName == NULL)
        return;
    if (session->onlineRoleName[0] != 0)
        playerName = session->onlineRoleName;
    else
    {
        const vm_net_mock_role_state *role = vm_net_mock_active_role();
        if (role != NULL && role->name[0] != 0)
            playerName = role->name;
    }
    if (playerName == NULL || playerName[0] == 0)
        return;

    switch (chestItemId)
    {
    case 522: /* 青铜宝箱 */
        chestNameGbk = "\xC7\xE0\xCD\xAD\xB1\xA6\xCF\xE4";
        break;
    case 523: /* 白银宝箱 */
        chestNameGbk = "\xB0\xD7\xD2\xF8\xB1\xA6\xCF\xE4";
        break;
    case 524: /* 黄金宝箱 */
        chestNameGbk = "\xBB\xC6\xBD\xF0\xB1\xA6\xCF\xE4";
        break;
    case 532: /* 玄晶宝箱 */
        chestNameGbk = "\xD0\xFE\xBE\xA7\xB1\xA6\xCF\xE4";
        break;
    case 534: /* 幻彩宝箱 */
        chestNameGbk = "\xBB\xC3\xB2\xCA\xB1\xA6\xCF\xE4";
        break;
    default: /* 宝箱 */
        chestNameGbk = "\xB1\xA6\xCF\xE4";
        break;
    }

    /* 恭喜{玩家}开启{箱}获得{N}个{物品}，祝贺！ — match Pixi tip 获得N个. */
    memset(message, 0, sizeof(message));
    wrote = snprintf(
        message, sizeof(message),
        "\xB9\xA7\xCF\xB2%s\xBF\xAA\xC6\xF4%s\xBB\xF1\xB5\xC3%u\xB8\xF6%s"
        "\xA3\xAC\xD7\xA3\xBA\xD8\xA3\xA1",
        playerName, chestNameGbk, count, itemName);
    if (wrote <= 0 || (size_t)wrote >= sizeof(message))
    {
        printf("[warn][network] mock_chest_open_world_announce item=%u count=%u "
               "reason=message-overflow\n",
               rewardItemId, count);
        return;
    }

    recipients = vm_mock_service_broadcast_world_chat_live(systemNameGbk, message);
    printf("[info][network] mock_chest_open_world_announce item=%u count=%u "
           "chest=%u player=%s recipients=%u evidence=1/3/3-type0-world "
           "coalesce-ten-pull-tip-match\n",
           rewardItemId, count, chestItemId, playerName, recipients);
}

static void vm_net_mock_gold_chest_maybe_announce_rare_reward(u32 chestItemId,
                                                             u32 rewardItemId)
{
    vm_mock_service_client_session *session = NULL;
    u32 debounceTicks = 5u; /* 500ms @ VM_SCHED_FRAME_MS=100 — covers rapid single opens */

    if (!vm_net_mock_gold_chest_reward_warrants_world_announce(rewardItemId))
        return;
    if (vm_net_mock_find_shop_catalog_item(rewardItemId) == NULL)
        return;

    session = vm_mock_service_get_active_client_session();
    if (session == NULL)
        return;

    if (session->chestAnnounceCount > 0 &&
        session->chestAnnounceRewardItemId == rewardItemId &&
        session->chestAnnounceChestItemId == chestItemId)
    {
        if (session->chestAnnounceCount < 99u)
            session->chestAnnounceCount += 1u;
        session->chestAnnounceFlushTick = g_schedulerTick + debounceTicks;
        printf("[info][network] mock_chest_open_world_announce_coalesce item=%u "
               "chest=%u count=%u flush_tick=%u\n",
               rewardItemId, chestItemId, session->chestAnnounceCount,
               session->chestAnnounceFlushTick);
        return;
    }

    if (session->chestAnnounceCount > 0)
        vm_net_mock_chest_announce_flush_pending(session);

    session->chestAnnounceRewardItemId = rewardItemId;
    session->chestAnnounceChestItemId = chestItemId;
    session->chestAnnounceCount = 1u;
    session->chestAnnounceFlushTick = g_schedulerTick + debounceTicks;
    printf("[info][network] mock_chest_open_world_announce_pending item=%u "
           "chest=%u count=1 flush_tick=%u\n",
           rewardItemId, chestItemId, session->chestAnnounceFlushTick);
}

/* Deliver overdue coalesce pending even when this open's reward is not rare. */
static void vm_net_mock_gold_chest_announce_flush_if_due(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || session->chestAnnounceCount == 0 ||
        session->chestAnnounceFlushTick == 0 ||
        g_schedulerTick < session->chestAnnounceFlushTick)
    {
        return;
    }
    vm_net_mock_chest_announce_flush_pending(session);
}

/*
 * PixiJS-only 1/7/24 ten-pull: one [世] line per rare itemId with total count.
 * Flushes immediately (no debounce) so bag tip「获得N个」matches world.
 */
static void vm_net_mock_gold_chest_announce_rare_reward_count(u32 chestItemId,
                                                             u32 rewardItemId,
                                                             u32 count)
{
    vm_mock_service_client_session *session = NULL;

    if (count == 0)
        return;
    if (!vm_net_mock_gold_chest_reward_warrants_world_announce(rewardItemId))
        return;
    if (vm_net_mock_find_shop_catalog_item(rewardItemId) == NULL)
        return;

    session = vm_mock_service_get_active_client_session();
    if (session == NULL)
        return;

    if (session->chestAnnounceCount > 0)
        vm_net_mock_chest_announce_flush_pending(session);

    session->chestAnnounceRewardItemId = rewardItemId;
    session->chestAnnounceChestItemId = chestItemId;
    session->chestAnnounceCount = count > 99u ? 99u : count;
    session->chestAnnounceFlushTick = 0;
    vm_net_mock_chest_announce_flush_pending(session);
    printf("[info][network] mock_chest_open_world_announce_batch item=%u "
           "chest=%u count=%u evidence=pixijs-only-7/24\n",
           rewardItemId, chestItemId, count > 99u ? 99u : count);
}

static bool vm_mock_service_session_enqueue_system_message_delayed(
    vm_mock_service_client_session *target,
    const char *message,
    u32 delayTicks)
{
    vm_mock_service_chat_notice *slot = NULL;
    u8 slotIndex = 0;
    u8 depthBefore = 0;

    if (target == NULL || message == NULL || message[0] == 0)
        return false;
    depthBefore = target->chatNoticeCount;
    if (!vm_mock_service_session_enqueue_system_message(target, message))
        return false;
    if (target->chatNoticeCount <= depthBefore)
        return false;
    slotIndex = (u8)((target->chatNoticeHead + target->chatNoticeCount - 1) %
                     VM_MOCK_SERVICE_CHAT_NOTICE_MAX);
    slot = &target->chatNotices[slotIndex];
    if (!slot->valid)
        return false;
    if (delayTicks == 0)
        delayTicks = 1;
    slot->notBeforeTick = g_schedulerTick + delayTicks;
    printf("[info][mock-service] chat_notice_delay target=%08x type=system "
           "delay_ticks=%u not_before=%u\n",
           target->clientId, delayTicks, slot->notBeforeTick);
    return true;
}

/*
 * Spar win/lose copy must wait until after 4/8's blank settle strip is gone.
 * Arm on 4/8 with notBefore; later map 25/5 / scene-sync delivers 25/12 clear
 * + 1/3/3 chat.  Never push unsolicited 25/11 (client then polls empty 25/11).
 */
static void vm_mock_service_session_arm_spar_result_message(
    vm_mock_service_client_session *session)
{
    u32 delayTicks = 0;

    if (session == NULL)
        return;
    /*
     * Blank 4/8 strip appears on tear-down; keep system tip well after it.
     * Default ~4.0s (40 * 100ms) after 4/8.  4/8 itself is scheduled earlier
     * (TERMINAL_DELAY+BANNER_HOLD defaults 4+1).
     */
    delayTicks = vm_net_mock_env_u32("CBE_DUEL_RESULT_MSG_DELAY_TICKS", 40);
    if (delayTicks < 30)
        delayTicks = 30;
    if (delayTicks > 90)
        delayTicks = 90;
    session->sparResultMessageArmed = true;
    session->sparResultNotBeforeTick = g_schedulerTick + delayTicks;
    /* Duel over: drop any leftover spar-ready handshake so map UI cannot keep
     * a pending 斗/confirm state after leave. */
    session->sparBattleReadyPending = false;
    session->sparBattlePeerClientId = 0;
    session->sparBattlePeerWireId = 0;
    session->sparInviteReplyActive = false;
    session->sparInviteSourceClientId = 0;
    session->sparInviteSourceWireId = 0;
    printf("[info][mock-service] spar_result_arm observer=%08x dead=%u "
           "delay_ticks=%u not_before=%u delivery=wait-after-blank-4/8\n",
           session->clientId, session->sparExitWasDead ? 1u : 0u,
           delayTicks, session->sparResultNotBeforeTick);
}

static const char *vm_mock_service_session_take_spar_result_text(
    vm_mock_service_client_session *session,
    const char *phase)
{
    /* GBK: 挑战成功 / 挑战失败 经验-%u 金钱-%u */
    static const char sparWinGbk[] =
        "\xcc\xf8\xd5\xbd\xb3\xc9\xb9\xa6";
    static char sparLoseBuf[96];
    static char arenaBuf[96];
    const char *text = NULL;

    if (session == NULL || !session->sparResultMessageArmed)
        return NULL;
    if (session->sparResultNotBeforeTick != 0 &&
        g_schedulerTick < session->sparResultNotBeforeTick)
    {
        return NULL;
    }
    session->sparResultMessageArmed = false;
    session->sparResultNotBeforeTick = 0;
    if (session->sparExitMessageQueued)
        return NULL;
    session->sparExitMessageQueued = true;
    if (session->arenaResultPending && session->arenaResultFdata[0] != 0)
    {
        snprintf(arenaBuf, sizeof(arenaBuf), "%s", session->arenaResultFdata);
        session->arenaResultPending = false;
        text = arenaBuf;
    }
    else if (session->sparExitWasDead)
    {
        if (session->sparDeathExpPenalty == 0 &&
            session->sparDeathMoneyPenalty == 0)
        {
            /* Friendly 切磋失败：无经验/金钱损失文案。 */
            static const char sparLosePlainGbk[] =
                "\xc7\xd0\xb4\xe8\xca\xa7\xb0\xdc"; /* 切磋失败 */
            text = sparLosePlainGbk;
        }
        else
        {
            snprintf(sparLoseBuf, sizeof(sparLoseBuf),
                     "\xcc\xf8\xd5\xbd\xca\xa7\xb0\xdc"
                     " \xbe\xad\xd1\xe9-%u"
                     " \xbd\xf0\xc7\xae-%u",
                     session->sparDeathExpPenalty,
                     session->sparDeathMoneyPenalty);
            text = sparLoseBuf;
        }
    }
    else
        text = sparWinGbk;
    printf("[info][mock-service] spar_result_take observer=%08x dead=%u "
           "exp_penalty=%u money_penalty=%u phase=%s bytes=%u\n",
           session->clientId, session->sparExitWasDead ? 1u : 0u,
           session->sparDeathExpPenalty, session->sparDeathMoneyPenalty,
           phase ? phase : "-", (u32)strlen(text));
    return text;
}

typedef struct
{
    vm_mock_service_client_session *target;
    u32 queued;
    u32 skipped;
} vm_mock_world_chat_history_context;

static bool g_vm_mock_world_chat_table_checked = false;
static bool g_vm_mock_world_chat_table_valid = false;

static bool vm_mock_world_chat_table_ensure(void)
{
    if (g_vm_mock_world_chat_table_checked)
        return g_vm_mock_world_chat_table_valid;
    g_vm_mock_world_chat_table_checked = true;
    g_vm_mock_world_chat_table_valid = vm_mysql_exec(
        "CREATE TABLE IF NOT EXISTS world_chat_messages ("
        "message_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        "source_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
        "source_role_id INT UNSIGNED NOT NULL,"
        "source_name VARBINARY(15) NOT NULL,"
        "message VARBINARY(81) NOT NULL,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(message_id),"
        "KEY idx_world_chat_source(source_account_id,source_role_id)) ENGINE=InnoDB");
    if (!g_vm_mock_world_chat_table_valid)
    {
        printf("[error][mock-service] world_chat_schema error=%s\n",
               vm_mysql_last_error());
    }
    return g_vm_mock_world_chat_table_valid;
}

static bool vm_mock_world_chat_store(
    const vm_mock_service_client_session *source,
    const char *sourceName,
    const char *message)
{
    char accountHex[129];
    char sourceNameHex[31];
    char messageHex[163];
    char query[768];
    const char *accountId = NULL;
    size_t accountLen = 0;
    size_t sourceNameLen = 0;
    size_t messageLen = 0;

    if (source == NULL || source->onlineRoleId == 0 || sourceName == NULL ||
        message == NULL)
    {
        return false;
    }
    accountId = source->accountId[0] ? source->accountId : "-";
    accountLen = strlen(accountId);
    sourceNameLen = strlen(sourceName);
    messageLen = strlen(message);
    /* Columns are VARBINARY(15)/(81): clamp instead of rejecting long GBK names
     * (onlineRoleName is 32 bytes) or slightly-overlong chat payloads. */
    if (sourceNameLen > 15)
        sourceNameLen = 15;
    if (messageLen > 81)
        messageLen = 81;
    if (accountLen == 0 || accountLen >= sizeof(source->accountId) ||
        sourceNameLen == 0 || messageLen == 0 ||
        !vm_mock_world_chat_table_ensure() ||
        vm_mysql_hex_encode(accountId, accountLen,
                            accountHex, sizeof(accountHex)) == 0 ||
        vm_mysql_hex_encode(sourceName, sourceNameLen,
                            sourceNameHex, sizeof(sourceNameHex)) == 0 ||
        vm_mysql_hex_encode(message, messageLen,
                            messageHex, sizeof(messageHex)) == 0)
    {
        printf("[warn][mock-service] world_chat_store_reject source=%08x/%u "
               "account_len=%u name_len=%u msg_len=%u\n",
               source->clientId, source->onlineRoleId,
               (u32)accountLen, (u32)strlen(sourceName), (u32)strlen(message));
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO world_chat_messages("
             "source_account_id,source_role_id,source_name,message) "
             "VALUES(CAST(X'%s' AS CHAR),%u,X'%s',X'%s')",
             accountHex, source->onlineRoleId, sourceNameHex, messageHex);
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-service] world_chat_store source=%08x/%u error=%s\n",
               source->clientId, source->onlineRoleId, vm_mysql_last_error());
        return false;
    }
    printf("[info][mock-service] world_chat_store source=%08x/%u bytes=%u storage=mysql\n",
           source->clientId, source->onlineRoleId, (u32)messageLen);
    return true;
}

static bool vm_mock_world_chat_history_row(
    void *contextValue,
    unsigned int columnCount,
    const char *const *values,
    const size_t *lengths)
{
    vm_mock_world_chat_history_context *context =
        (vm_mock_world_chat_history_context *)contextValue;
    char roleIdText[32];
    char sourceName[16];
    char message[82];
    size_t decodedLen = 0;
    u32 sourceRoleId = 0;

    if (context == NULL || context->target == NULL || columnCount < 3 ||
        values == NULL || lengths == NULL || values[0] == NULL ||
        values[1] == NULL || values[2] == NULL ||
        lengths[0] == 0 || lengths[0] >= sizeof(roleIdText))
    {
        if (context != NULL)
            ++context->skipped;
        return true;
    }
    memset(roleIdText, 0, sizeof(roleIdText));
    memcpy(roleIdText, values[0], lengths[0]);
    sourceRoleId = (u32)strtoul(roleIdText, NULL, 10);
    memset(sourceName, 0, sizeof(sourceName));
    if (sourceRoleId == 0 ||
        !vm_mysql_hex_decode(values[1], lengths[1], sourceName,
                             sizeof(sourceName) - 1, &decodedLen) ||
        decodedLen == 0 || decodedLen > 15)
    {
        ++context->skipped;
        return true;
    }
    sourceName[decodedLen] = 0;
    memset(message, 0, sizeof(message));
    if (!vm_mysql_hex_decode(values[2], lengths[2], message,
                             sizeof(message) - 1, &decodedLen) ||
        decodedLen == 0 || decodedLen > 81)
    {
        ++context->skipped;
        return true;
    }
    message[decodedLen] = 0;
    if (!vm_mock_service_session_enqueue_chat_notice_identity(
            context->target, VM_MOCK_CHAT_TYPE_WORLD, 0, sourceRoleId,
            sourceName, message))
    {
        ++context->skipped;
        return true;
    }
    ++context->queued;
    return true;
}

static bool vm_mock_world_chat_queue_recent(
    vm_mock_service_client_session *target,
    u32 *queuedOut)
{
    vm_mock_world_chat_history_context context;
    char query[512];

    if (queuedOut)
        *queuedOut = 0;
    if (target == NULL || !vm_mock_world_chat_table_ensure())
        return false;
    snprintf(query, sizeof(query),
             "SELECT source_role_id,HEX(source_name),HEX(message) FROM ("
             "SELECT message_id,source_role_id,source_name,message "
             "FROM world_chat_messages ORDER BY message_id DESC LIMIT %u"
             ") AS recent ORDER BY message_id ASC",
             (unsigned)VM_MOCK_SERVICE_WORLD_CHAT_HISTORY_MAX);
    memset(&context, 0, sizeof(context));
    context.target = target;
    if (!vm_mysql_query(query, vm_mock_world_chat_history_row, &context))
    {
        printf("[error][mock-service] world_chat_history_load target=%08x error=%s\n",
               target->clientId, vm_mysql_last_error());
        return false;
    }
    if (queuedOut)
        *queuedOut = context.queued;
    printf("[info][mock-service] world_chat_history_queue target=%08x queued=%u skipped=%u limit=%u\n",
           target->clientId, context.queued, context.skipped,
           VM_MOCK_SERVICE_WORLD_CHAT_HISTORY_MAX);
    return true;
}

static vm_mock_service_team *vm_mock_service_team_find_for_client(u32 clientId)
{
    if (clientId == 0)
        return NULL;
    for (u32 i = 0; i < VM_MOCK_SERVICE_TEAM_MAX; ++i)
    {
        vm_mock_service_team *team = &g_vm_mock_service_teams[i];
        if (!team->active)
            continue;
        for (u8 member = 0; member < team->memberCount; ++member)
        {
            if (team->memberClientIds[member] == clientId)
                return team;
        }
    }
    return NULL;
}

static bool vm_mock_service_team_contains_client(const vm_mock_service_team *team, u32 clientId)
{
    if (team == NULL || !team->active || clientId == 0)
        return false;
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        if (team->memberClientIds[member] == clientId)
            return true;
    }
    return false;
}

static bool vm_mock_service_team_is_leader(const vm_mock_service_team *team, u32 clientId)
{
    return team != NULL && team->active && team->leaderClientId == clientId;
}

/*
 * Multi-member parties: only the leader may start map encounters (4/1) or
 * hangup.  Instance-challenge 30/9 already gates via isleader; this covers
 * walk-in monsters and hangup so members cannot peel into a solo fight.
 * Leader hangup starts team battles (see team-leader-only-hangup RE note).
 */
static bool vm_mock_service_active_session_team_encounter_blocked(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = NULL;

    if (session == NULL || session->clientId == 0)
        return false;
    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team == NULL || !team->active || team->memberCount < 2)
        return false;
    return !vm_mock_service_team_is_leader(team, session->clientId);
}

/*
 * Multi-member parties: only the leader may portal / map-stone / spouse-warp.
 * Members stay on the leader's map via team-follow 30/1 (see team-follow RE).
 */
static bool vm_mock_service_active_session_team_transfer_blocked(void)
{
    return vm_mock_service_active_session_team_encounter_blocked();
}

static void vm_mock_service_session_clear_team_follow_scene_enter(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL || !session->pendingTeamFollowSceneEnter)
        return;
    session->pendingTeamFollowSceneEnter = false;
    session->pendingTeamFollowSceneEnterEarliestTick = 0;
    session->pendingTeamFollowSceneEnterScene[0] = 0;
    session->pendingTeamFollowSceneEnterX = 0;
    session->pendingTeamFollowSceneEnterY = 0;
    printf("[info][mock-service] team_follow_clear client=%08x reason=%s\n",
           session->clientId,
           reason ? reason : "-");
}

static void vm_mock_service_team_queue_follow_leader_scene(
    vm_mock_service_client_session *leader,
    const vm_net_mock_scene_change_target *target,
    const char *reason);

static bool vm_mock_service_session_on_dream_instance(const vm_mock_service_client_session *session)
{
    return session != NULL &&
           vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) &&
           session->sceneVisibleScene[0] == '2' &&
           session->sceneVisibleScene[1] == '9';
}

/* Purge dream material 841 on leave; arm list-only 17/1 poll for UI sync. */
static u32 vm_net_mock_dream_purge_item_841_and_resync(vm_net_mock_role_state *role,
                                                       const char *reason)
{
    vm_mock_service_client_session *session = NULL;
    u32 removed = vm_net_mock_dream_purge_item_841(role, reason);

    if (removed == 0)
        return 0;
    session = vm_mock_service_get_active_client_session();
    if (session != NULL)
    {
        session->backpackListResyncPending = true;
        session->backpackListResyncPhase = 1;
        session->backpackListResyncListOnly = true;
        session->backpackListResyncTick = g_schedulerTick;
        g_netMockBackpackGridSeededRoleId = 0;
    }
    printf("[info][network] mock_dream_purge_841_resync role=%u removed=%u "
           "reason=%s action=17/1-list-resync\n",
           role ? role->roleId : 0, removed, reason ? reason : "-");
    return removed;
}

static vm_mock_service_team *vm_mock_service_team_create(vm_mock_service_client_session *leader)
{
    vm_mock_service_team *team = NULL;
    static const char dreamTeamBanGbk[] =
        "\xc3\xce\xbe\xb5\xb8\xb1\xb1\xbe\xbd\xfb\xd6\xb9\xd7\xe9\xb6\xd3"; /* 梦境副本禁止组队 */
    static const char arenaTeamBanGbk[] =
        "\xb1\xc8\xce\xe4\xc6\xda\xbc\xe4\xbd\xfb\xd6\xb9\xd7\xe9\xb6\xd3"; /* 比武期间禁止组队 */

    if (leader == NULL || leader->clientId == 0 || leader->onlineRoleId == 0)
        return NULL;
    if (vm_mock_service_session_on_dream_instance(leader))
    {
        (void)vm_mock_service_session_enqueue_system_message(leader,
                                                             dreamTeamBanGbk);
        printf("[info][mock-service] team_create_blocked_dream leader=%08x/%u "
               "evidence=dream-ops-no-team\n",
               leader->clientId, leader->onlineRoleId);
        return NULL;
    }
    if (vm_mock_arena_team_forbidden_for_session(leader))
    {
        (void)vm_mock_service_session_enqueue_system_message(leader,
                                                             arenaTeamBanGbk);
        printf("[info][mock-service] team_create_blocked_arena leader=%08x/%u "
               "evidence=arena-no-team\n",
               leader->clientId, leader->onlineRoleId);
        return NULL;
    }
    team = vm_mock_service_team_find_for_client(leader->clientId);
    if (team != NULL)
        return team;
    for (u32 i = 0; i < VM_MOCK_SERVICE_TEAM_MAX; ++i)
    {
        if (!g_vm_mock_service_teams[i].active)
        {
            team = &g_vm_mock_service_teams[i];
            break;
        }
    }
    if (team == NULL)
    {
        printf("[warn][mock-service] team_create_drop leader=%08x/%u reason=team-table-full\n",
               leader->clientId, leader->onlineRoleId);
        return NULL;
    }
    memset(team, 0, sizeof(*team));
    team->active = true;
    team->leaderClientId = leader->clientId;
    team->memberCount = 1;
    team->memberClientIds[0] = leader->clientId;
    leader->pendingTeamBattleSerial = 0;
    printf("[info][mock-service] team_create leader=%08x/%u\n",
           leader->clientId, leader->onlineRoleId);
    return team;
}

static bool vm_mock_service_team_add_member(vm_mock_service_team *team,
                                            vm_mock_service_client_session *member)
{
    static const char dreamTeamBanGbk[] =
        "\xc3\xce\xbe\xb5\xb8\xb1\xb1\xbe\xbd\xfb\xd6\xb9\xd7\xe9\xb6\xd3"; /* 梦境副本禁止组队 */

    if (team == NULL || !team->active || member == NULL || member->clientId == 0 ||
        member->onlineRoleId == 0 || team->memberCount >= VM_MOCK_SERVICE_TEAM_MEMBER_MAX ||
        vm_mock_service_team_contains_client(team, member->clientId) ||
        vm_mock_service_team_find_for_client(member->clientId) != NULL)
    {
        return false;
    }
    {
        vm_mock_service_client_session *leader =
            vm_mock_service_find_client_session(team->leaderClientId);
        if (vm_mock_service_session_on_dream_instance(member) ||
            vm_mock_service_session_on_dream_instance(leader))
        {
            (void)vm_mock_service_session_enqueue_system_message(member,
                                                                 dreamTeamBanGbk);
            if (leader != NULL && leader->clientId != member->clientId)
                (void)vm_mock_service_session_enqueue_system_message(
                    leader, dreamTeamBanGbk);
            printf("[info][mock-service] team_add_blocked_dream leader=%08x "
                   "member=%08x/%u evidence=dream-ops-no-team\n",
                   team->leaderClientId, member->clientId, member->onlineRoleId);
            return false;
        }
        if (vm_mock_arena_team_forbidden_for_session(member) ||
            vm_mock_arena_team_forbidden_for_session(leader))
        {
            static const char arenaTeamBanGbk[] =
                "\xb1\xc8\xce\xe4\xc6\xda\xbc\xe4\xbd\xfb\xd6\xb9\xd7\xe9\xb6\xd3"; /* 比武期间禁止组队 */
            (void)vm_mock_service_session_enqueue_system_message(member,
                                                                 arenaTeamBanGbk);
            if (leader != NULL && leader->clientId != member->clientId)
                (void)vm_mock_service_session_enqueue_system_message(
                    leader, arenaTeamBanGbk);
            printf("[info][mock-service] team_add_blocked_arena leader=%08x "
                   "member=%08x/%u evidence=arena-no-team\n",
                   team->leaderClientId, member->clientId, member->onlineRoleId);
            return false;
        }
    }
    member->pendingTeamBattleSerial = 0;
    team->memberClientIds[team->memberCount++] = member->clientId;
    /* Joiner becomes a non-leader seat: drop solo hangup so poll cannot
     * start a fight that the 4/1 gate would now reject. */
    if (member->clientId == g_vm_mock_service_active_client_id)
        vm_net_mock_hangup_loop_clear("team-join-member");
    printf("[info][mock-service] team_add leader=%08x member=%08x/%u count=%u\n",
           team->leaderClientId, member->clientId, member->onlineRoleId, team->memberCount);
    return true;
}

static void vm_mock_service_team_notify_leave(vm_mock_service_team *team,
                                              vm_mock_service_client_session *leaver)
{
    if (team == NULL || leaver == NULL)
        return;
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        vm_mock_service_client_session *peer =
            vm_mock_service_find_client_session(team->memberClientIds[member]);
        if (peer != NULL && peer->clientId != leaver->clientId)
        {
            peer->pendingTeamBattleSerial = 0;
            (void)vm_mock_service_session_enqueue_social_notice(
                peer, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_LEAVE, 0,
                leaver, NULL, leaver->accountId);
        }
    }
}

/* Drop a finished/orphan shared fight so solo hangup prefer cannot be routed
 * into team_seat_can_act (always false when battleFinished), and so operate
 * does not hit the finished-fight branch with a stale party snapshot.
 * See docs/re/2026-08-01-hangup-stuck-after-teammate-leave.md. */
static void vm_mock_service_team_clear_battle_state(vm_mock_service_team *team,
                                                    const char *reason)
{
    if (team == NULL || (!team->battleActive && !team->battleFinished))
        return;
    printf("[info][mock-service] team_battle_clear serial=%u reason=%s "
           "roster=%u battle_members=%u finished=%u "
           "evidence=stale-team-battle-blocks-solo-hangup\n",
           team->battleSerial,
           reason ? reason : "-",
           team->memberCount,
           team->battleMemberCount,
           team->battleFinished ? 1 : 0);
    for (u8 i = 0; i < team->memberCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->memberClientIds[i]);
        if (member != NULL)
            member->pendingTeamBattleSerial = 0;
    }
    team->battleActive = false;
    team->battleFinished = false;
    team->battleMemberCount = 0;
    team->battleRoundActedMask = 0;
    team->battleMemberLeftMask = 0;
    team->battleRoundActDeadlineTick = 0;
    team->battleRoundTerminalPending = false;
    team->battleRoundTerminalActorIndex = 0xff;
    team->battleRoundActionSerial = 0;
    team->battleActionSerial = 0;
    memset(team->battleMemberClientIds, 0, sizeof(team->battleMemberClientIds));
    memset(team->battleRoundActions, 0, sizeof(team->battleRoundActions));
    memset(team->battleEvents, 0, sizeof(team->battleEvents));
}

/* Returns true when the member was part of an active team.  Leader departure
 * deliberately dissolves the party: group subtype 5/7 clears every client
 * roster when the removed id is the leader id. */
static bool vm_mock_service_team_remove_member(vm_mock_service_client_session *leaver,
                                               const char *reason)
{
    vm_mock_service_team *team = NULL;
    u8 memberIndex = VM_MOCK_SERVICE_TEAM_MEMBER_MAX;
    bool leaderLeaves = false;

    if (leaver == NULL || leaver->clientId == 0)
        return false;
    team = vm_mock_service_team_find_for_client(leaver->clientId);
    if (team == NULL)
        return false;
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        if (team->memberClientIds[member] == leaver->clientId)
        {
            memberIndex = member;
            break;
        }
    }
    if (memberIndex >= team->memberCount)
        return false;

    leaderLeaves = team->leaderClientId == leaver->clientId;
    leaver->pendingTeamBattleSerial = 0;
    vm_mock_service_session_clear_team_follow_scene_enter(leaver,
                                                          reason ? reason : "team-leave");
    vm_mock_service_team_notify_leave(team, leaver);
    if (team->battleActive)
        vm_mock_service_team_battle_note_roster_disconnect(team, leaver, reason);
    if (leaderLeaves)
    {
        /*
         * Mid-fight leader disconnect must not memset the shared battle while
         * other seats are still in Battle.cbm — promote a survivor instead.
         */
        if (team->battleActive && team->memberCount > 1)
        {
            for (u8 member = memberIndex + 1; member < team->memberCount; ++member)
                team->memberClientIds[member - 1] = team->memberClientIds[member];
            --team->memberCount;
            team->memberClientIds[team->memberCount] = 0;
            team->leaderClientId = team->memberClientIds[0];
            printf("[info][mock-service] team_promote_leader battle=%u "
                   "new_leader=%08x count=%u reason=%s\n",
                   team->battleSerial,
                   team->leaderClientId,
                   team->memberCount,
                   reason ? reason : "-");
            if (team->memberCount < 2 &&
                (!team->battleActive || team->battleFinished))
            {
                vm_mock_service_team_clear_battle_state(team, "roster-below-two");
            }
            return true;
        }
        printf("[info][mock-service] team_disband leader=%08x/%u reason=%s\n",
               leaver->clientId, leaver->onlineRoleId, reason ? reason : "-");
        memset(team, 0, sizeof(*team));
        return true;
    }

    for (u8 member = memberIndex + 1; member < team->memberCount; ++member)
        team->memberClientIds[member - 1] = team->memberClientIds[member];
    --team->memberCount;
    team->memberClientIds[team->memberCount] = 0;
    if (team->memberCount < 2 &&
        (!team->battleActive || team->battleFinished))
    {
        vm_mock_service_team_clear_battle_state(team, "roster-below-two");
    }
    printf("[info][mock-service] team_remove leader=%08x member=%08x/%u count=%u reason=%s\n",
           team->leaderClientId, leaver->clientId, leaver->onlineRoleId,
           team->memberCount, reason ? reason : "-");
    return true;
}

static void vm_mock_service_team_enqueue_hsp_for_members(vm_mock_service_client_session *source)
{
    vm_mock_service_team *team = NULL;

    if (source == NULL || source->clientId == 0 || source->onlineRoleId == 0)
        return;
    team = vm_mock_service_team_find_for_client(source->clientId);
    if (team == NULL)
        return;
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        vm_mock_service_client_session *peer =
            vm_mock_service_find_client_session(team->memberClientIds[member]);
        if (peer != NULL)
        {
            (void)vm_mock_service_session_enqueue_social_notice(
                peer, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_HSP, 0,
                source, NULL, source->accountId);
        }
    }
}

static bool vm_net_mock_is_actor_moveinfo_timeline(const u8 *moveInfo, u16 moveInfoLen);

static vm_mock_service_peer_sync *vm_mock_service_get_peer_sync(vm_mock_service_client_session *observer,
                                                                u32 sourceClientId,
                                                                u32 actorId,
                                                                u32 sourceMoveSerial,
                                                                bool create,
                                                                bool *createdOut)
{
    vm_mock_service_peer_sync *freeEntry = NULL;

    if (createdOut)
        *createdOut = false;
    if (observer == NULL || sourceClientId == 0)
        return NULL;
    for (u32 i = 0; i < VM_MOCK_SERVICE_PEER_SYNC_MAX; ++i)
    {
        vm_mock_service_peer_sync *entry = &observer->peerSync[i];
        if (entry->sourceClientId == sourceClientId)
        {
            if (actorId != 0)
                entry->actorId = actorId;
            return entry;
        }
        if (freeEntry == NULL && entry->sourceClientId == 0)
            freeEntry = entry;
    }
    if (!create || freeEntry == NULL)
        return NULL;
    memset(freeEntry, 0, sizeof(*freeEntry));
    freeEntry->sourceClientId = sourceClientId;
    freeEntry->actorId = actorId;
    /*
     * A newly observed peer is created by otherinfo at its authoritative
     * current position. Baseline the source serial here so an old movement
     * burst is not replayed immediately after spawning the node.
     */
    freeEntry->lastMoveSerial = sourceMoveSerial;
    if (createdOut)
        *createdOut = true;
    return freeEntry;
}

static void vm_mock_service_session_clear_moveinfo(vm_mock_service_client_session *session,
                                                   const char *reason)
{
    bool hadMoveinfo = false;

    if (session == NULL)
        return;
    hadMoveinfo = session->lastMoveinfoValid || session->pendingDirQueueValid;
    session->lastMoveinfoValid = false;
    session->lastMoveinfoLen = 0;
    session->lastMoveinfoFormat = VM_MOCK_SERVICE_MOVEINFO_FORMAT_NONE;
    memset(session->lastMoveinfoBlob, 0, sizeof(session->lastMoveinfoBlob));
    session->lastMoveinfoTick = g_schedulerTick;
    session->pendingDirQueueValid = false;
    session->pendingDirQueueLen = 0;
    session->pendingDirQueueStartX = 0;
    session->pendingDirQueueStartY = 0;
    session->pendingDirQueueEndX = 0;
    session->pendingDirQueueEndY = 0;
    memset(session->pendingDirQueueBlob, 0, sizeof(session->pendingDirQueueBlob));
    session->pendingDirQueueTick = g_schedulerTick;
    if (hadMoveinfo)
    {
        printf("[debug][mock-service] moveinfo_clear client=%08x reason=%s\n",
               session->clientId,
               reason ? reason : "-");
    }
}

/* Client evidence in scene_runtime_tick(0x01014EE0) shows ten 100ms frames
 * per 2/1 direction-timeline upload.  Keep at most two seconds of server-time
 * credit so normal packet jitter can recover without allowing an idle client
 * to bank unlimited travel. */
#define VM_MOCK_SERVICE_MOVE_STEP_MS 100u
#define VM_MOCK_SERVICE_MOVE_MAX_CREDIT_MS 2000u

static void vm_mock_service_session_reset_movement_rate(vm_mock_service_client_session *session,
                                                        const char *reason)
{
    if (session == NULL)
        return;
    session->movementRateActive = true;
    session->movementRateAnchorMs = scheduler_get_tick_ms();
    session->movementRateCreditMs = 0;
    session->movementRateViolationCount = 0;
    session->movementRateDeniedSteps = 0;
    session->movementRateLastViolationMs = 0;
    printf("[debug][mock-service] movement_rate_reset client=%08x reason=%s\n",
           session->clientId, reason ? reason : "-");
}

/* Copy only the server-time-authorized prefix of an already validated 2/1
 * direction timeline.  The caller keeps the ordinary empty ACK contract: a
 * rate rejection changes server authority, not the client packet parser. */
static u16 vm_mock_service_session_limit_timeline_by_rate(
    vm_mock_service_client_session *session, const u8 *moveInfo, u16 moveInfoLen,
    u8 *acceptedOut, u16 acceptedCap, u16 *requestedStepsOut,
    u16 *acceptedStepsOut, u16 *deniedStepsOut, u32 *elapsedMsOut,
    u32 *creditBeforeMsOut, u32 *creditAfterMsOut)
{
    u32 nowMs = scheduler_get_tick_ms();
    u32 elapsedMs = 0;
    u32 creditBeforeMs = 0;
    u16 requestedSteps = 0;
    u16 acceptedSteps = 0;
    u16 acceptedLen = 0;

    if (requestedStepsOut)
        *requestedStepsOut = 0;
    if (acceptedStepsOut)
        *acceptedStepsOut = 0;
    if (deniedStepsOut)
        *deniedStepsOut = 0;
    if (elapsedMsOut)
        *elapsedMsOut = 0;
    if (creditBeforeMsOut)
        *creditBeforeMsOut = 0;
    if (creditAfterMsOut)
        *creditAfterMsOut = 0;
    if (session == NULL || moveInfo == NULL || moveInfoLen == 0 ||
        acceptedOut == NULL || acceptedCap < moveInfoLen)
    {
        return 0;
    }
    for (u16 i = 0; i < moveInfoLen; ++i)
    {
        if (moveInfo[i] != 0)
            ++requestedSteps;
    }
    if (!session->movementRateActive)
    {
        /* A 2/1 before scene-ready is discarded by the caller.  This branch
         * only covers an old/incomplete session lifecycle and starts with no
         * free movement credit. */
        session->movementRateActive = true;
        session->movementRateAnchorMs = nowMs;
        session->movementRateCreditMs = 0;
    }
    else
    {
        elapsedMs = nowMs - session->movementRateAnchorMs;
        if (elapsedMs >= VM_MOCK_SERVICE_MOVE_MAX_CREDIT_MS ||
            session->movementRateCreditMs >= VM_MOCK_SERVICE_MOVE_MAX_CREDIT_MS - elapsedMs)
        {
            session->movementRateCreditMs = VM_MOCK_SERVICE_MOVE_MAX_CREDIT_MS;
        }
        else
        {
            session->movementRateCreditMs += elapsedMs;
        }
        session->movementRateAnchorMs = nowMs;
    }
    creditBeforeMs = session->movementRateCreditMs;
    for (u16 i = 0; i < moveInfoLen; ++i)
    {
        if (moveInfo[i] != 0 && session->movementRateCreditMs < VM_MOCK_SERVICE_MOVE_STEP_MS)
            break;
        acceptedOut[acceptedLen++] = moveInfo[i];
        if (moveInfo[i] != 0)
        {
            session->movementRateCreditMs -= VM_MOCK_SERVICE_MOVE_STEP_MS;
            ++acceptedSteps;
        }
    }
    /* A zero-only prefix has no movement or valid timeline semantics. */
    if (acceptedSteps == 0)
        acceptedLen = 0;
    if (requestedStepsOut)
        *requestedStepsOut = requestedSteps;
    if (acceptedStepsOut)
        *acceptedStepsOut = acceptedSteps;
    if (deniedStepsOut)
        *deniedStepsOut = requestedSteps - acceptedSteps;
    if (elapsedMsOut)
        *elapsedMsOut = elapsedMs;
    if (creditBeforeMsOut)
        *creditBeforeMsOut = creditBeforeMs;
    if (creditAfterMsOut)
        *creditAfterMsOut = session->movementRateCreditMs;
    return acceptedLen;
}

static void vm_mock_service_session_store_pending_timeline(vm_mock_service_client_session *session,
                                                           const u8 *moveInfo,
                                                           u16 moveInfoLen,
                                                           u16 startX,
                                                           u16 startY,
                                                           u16 endX,
                                                           u16 endY)
{
    if (session == NULL ||
        moveInfo == NULL ||
        moveInfoLen == 0 ||
        moveInfoLen > sizeof(session->pendingDirQueueBlob) ||
        !vm_net_mock_is_actor_moveinfo_timeline(moveInfo, moveInfoLen) ||
        startX == 0 || startY == 0 ||
        endX == 0 || endY == 0)
    {
        return;
    }
    memcpy(session->pendingDirQueueBlob, moveInfo, moveInfoLen);
    session->pendingDirQueueLen = moveInfoLen;
    session->pendingDirQueueStartX = startX;
    session->pendingDirQueueStartY = startY;
    session->pendingDirQueueEndX = endX;
    session->pendingDirQueueEndY = endY;
    session->pendingDirQueueValid = true;
    session->pendingDirQueueTick = g_schedulerTick;
    ++session->pendingDirQueueSerial;
    if (session->pendingDirQueueSerial == 0)
        session->pendingDirQueueSerial = 1;
}

static void vm_mock_service_session_cancel_map_stone_loading_clear(
    vm_mock_service_client_session *session,
    const char *reason);
static void vm_mock_service_session_arm_map_stone_loading_clear(
    vm_mock_service_client_session *session,
    const char *scene);
static void vm_mock_service_session_rearm_map_stone_loading_clear(
    vm_mock_service_client_session *session,
    const char *reason);
static void vm_mock_service_session_cancel_shop_return_loading_clear(
    vm_mock_service_client_session *session,
    const char *reason);
static void vm_mock_service_session_arm_shop_return_loading_clear(
    vm_mock_service_client_session *session,
    const char *scene);
static void vm_mock_service_session_arm_shop_return_loading_clear_after_catalog(
    vm_mock_service_client_session *session,
    const char *scene,
    bool priorSeeded);

static void vm_mock_service_session_store_moveinfo(vm_mock_service_client_session *session,
                                                   const char *scene,
                                                   const u8 *moveInfo,
                                                   u16 moveInfoLen,
                                                   u16 x,
                                                   u16 y,
                                                   const char *reason)
{
    u8 format = VM_MOCK_SERVICE_MOVEINFO_FORMAT_NONE;
    const char *formatText = "drop";

    if (session == NULL ||
        moveInfo == NULL ||
        moveInfoLen == 0 ||
        moveInfoLen > VM_MOCK_SERVICE_SESSION_MOVEINFO_MAX)
    {
        return;
    }
    if (moveInfoLen >= 16 &&
        moveInfo[0] == 0 && moveInfo[1] == 2 &&
        moveInfo[4] == 0 && moveInfo[5] == 2 &&
        moveInfo[8] == 0 && moveInfo[9] == 4)
    {
        format = VM_MOCK_SERVICE_MOVEINFO_FORMAT_RESPONSE_ENTRY;
        formatText = "response-entry";
    }
    else if (vm_net_mock_is_actor_moveinfo_timeline(moveInfo, moveInfoLen))
    {
        format = VM_MOCK_SERVICE_MOVEINFO_FORMAT_TIMELINE;
        formatText = "timeline";
    }
    if (format == VM_MOCK_SERVICE_MOVEINFO_FORMAT_NONE &&
        moveInfoLen <= 32)
    {
        format = VM_MOCK_SERVICE_MOVEINFO_FORMAT_OPAQUE_SMALL;
        formatText = "opaque-small";
    }
    if (format == VM_MOCK_SERVICE_MOVEINFO_FORMAT_NONE)
        return;
    memcpy(session->lastMoveinfoBlob, moveInfo, moveInfoLen);
    session->lastMoveinfoLen = moveInfoLen;
    session->lastMoveinfoValid = true;
    session->lastMoveinfoFormat = format;
    session->lastMoveinfoTick = g_schedulerTick;
    if (format != VM_MOCK_SERVICE_MOVEINFO_FORMAT_TIMELINE)
    {
        session->pendingDirQueueValid = false;
        session->pendingDirQueueLen = 0;
        session->pendingDirQueueStartX = 0;
        session->pendingDirQueueStartY = 0;
        session->pendingDirQueueEndX = 0;
        session->pendingDirQueueEndY = 0;
        memset(session->pendingDirQueueBlob, 0, sizeof(session->pendingDirQueueBlob));
        session->pendingDirQueueTick = g_schedulerTick;
    }
    printf("[info][mock-service] moveinfo_store client=%08x kind=%s len=%u pos=(%u,%u) reason=%s scene=%s\n",
           session->clientId,
           formatText,
           (u32)moveInfoLen,
           x,
           y,
           reason ? reason : "-",
           scene ? scene : "-");
    if (format == VM_MOCK_SERVICE_MOVEINFO_FORMAT_TIMELINE &&
        session->shopReturnLoadingClearPending)
    {
        char clearScene[64];
        bool wasPostCatalog = session->shopReturnLoadingClearPostCatalog;
        u8 rem = session->shopReturnLoadingClearRemaining;
        const char *kind2Scene = NULL;

        snprintf(clearScene, sizeof(clearScene), "%s",
                 session->shopReturnLoadingClearScene);
        kind2Scene = clearScene[0] ? clearScene : scene;
        /*
         * Post-catalog never dripped (cold arm remaining=8, lite=3): timeline
         * moveinfo does NOT prove the second DoLoading closed — canceling here
         * skips all ResetDownloadState and leaves the bar up (梦境 2026-08-05:
         * catalog cold then moveinfo-live-post-catalog remaining=8).
         */
        if (wasPostCatalog &&
            rem >= (session->shopReturnLoadingClearLite ? 3u : 8u))
        {
            printf("[info][mock-service] shop_return_loading_clear_keep "
                   "client=%08x scene=%s remaining=%u lite=%u "
                   "reason=moveinfo-before-any-post-catalog-30/2 "
                   "evidence=second-DoLoading-still-needs-drip\n",
                   session->clientId,
                   kind2Scene != NULL ? kind2Scene : "-",
                   (u32)rem,
                   session->shopReturnLoadingClearLite ? 1u : 0u);
        }
        else
        {
        /*
         * Walkable moveinfo means DoLoading already closed enough for menus.
         * Cancel remaining poll 30/2.  NPC maps: catalog 27/11 already restored
         * visible NPCs — do NOT arm kind-2 30/1 here (临安 2026-07-28: second
         * EnterScene after first walk hurt UX; portals stay from prior enter).
         * Empty maps still need deferred 30/1 for combat nodes.
         */
        vm_mock_service_session_cancel_shop_return_loading_clear(
            session, wasPostCatalog ? "moveinfo-live-post-catalog"
                                    : "moveinfo-live");
        if (session->shopReturnNpcCatalogPending)
        {
            if (session->shopReturnNpcCatalogReadyTick == 0)
            {
                session->shopReturnNpcCatalogReadyTick = g_schedulerTick;
                printf("[info][mock-service] shop_return_npc_catalog_ready "
                       "client=%08x scene=%s via=moveinfo-live "
                       "evidence=shell-walkable-poll-27/11-before-kind2\n",
                       session->clientId,
                       kind2Scene != NULL ? kind2Scene : "-");
            }
        }
        else if (wasPostCatalog)
        {
            vm_mock_service_session_arm_shop_return_busy_ack(session);
            vm_mock_service_session_note_dream_shop_return_kind2_skip(
                session, kind2Scene);
            printf("[info][mock-service] shop_return_kind2_skip "
                   "client=%08x scene=%s reason=npc-catalog-live-no-reenter "
                   "via=moveinfo-live-post-catalog "
                   "evidence=27/11-visible-skip-second-30/1\n",
                   session->clientId,
                   kind2Scene != NULL ? kind2Scene : "-");
        }
        else if (kind2Scene != NULL &&
                 vm_net_mock_scene_room_npc_seed_count(kind2Scene) == 0)
        {
            vm_mock_service_session_arm_shop_return_kind2_reenter(
                session, kind2Scene, "moveinfo-live");
        }
        else
        {
            vm_mock_service_session_note_dream_shop_return_kind2_skip(
                session, kind2Scene);
            printf("[info][mock-service] shop_return_kind2_skip "
                   "client=%08x scene=%s reason=npc-shell-only-no-catalog "
                   "via=moveinfo-live "
                   "evidence=shell-only-no-kind2\n",
                   session->clientId,
                   kind2Scene != NULL ? kind2Scene : "-");
        }
        } /* else: allow moveinfo-live cancel */
    }
}

static void vm_mock_service_session_mark_scene_pending(vm_mock_service_client_session *session,
                                                       const vm_net_mock_scene_change_target *target,
                                                       const char *reason)
{
    bool changed = false;
    const char *scene = NULL;

    if (session == NULL)
        return;
    scene = (target != NULL && vm_net_mock_scene_name_is_safe(target->scene)) ? target->scene : NULL;
    changed = !session->sceneVisiblePending ||
              !session->sceneVisibleReady ||
              ((scene != NULL || session->scenePendingScene[0] != 0) &&
               !vm_net_mock_scene_names_equal_loose(session->scenePendingScene, scene));
    session->sceneVisibleReady = false;
    session->sceneVisiblePending = true;
    session->sceneVisibleTick = g_schedulerTick;
    /*
     * Scene transfer (map-stone / portal) owns its own 30/1+30/2 lifecycle.
     * Leftover map-stone or shop-return poll 30/2 must not inject into a NEW
     * transfer.  Exception: same-scene shop-return clear (shell or post-
     * catalog) must survive client follow-up 2/3.
     *   - 临安府_05: post-catalog remaining=8 cancelled → stuck
     *   - 梦境 early-shell 27/11: shell remaining=5 cancelled + SCE spawn
     *     (256,160) → third DoLoading stuck (2026-08-05)
     */
    if (session->shopReturnLoadingClearPending &&
        scene != NULL &&
        vm_net_mock_scene_name_is_safe(session->shopReturnLoadingClearScene) &&
        vm_net_mock_scene_names_equal_loose(session->shopReturnLoadingClearScene,
                                            scene))
    {
        session->shopReturnLoadingClearTick = g_schedulerTick;
        if (session->shopReturnLoadingClearRemaining < 2u)
            session->shopReturnLoadingClearRemaining = 2;
        printf("[info][mock-service] shop_return_loading_clear_hold client=%08x "
               "scene=%s remaining=%u post_catalog=%u "
               "reason=same-scene-shop-return-2/3 "
               "evidence=keep-shell-or-post-catalog-30/2\n",
               session->clientId,
               session->shopReturnLoadingClearScene,
               (u32)session->shopReturnLoadingClearRemaining,
               session->shopReturnLoadingClearPostCatalog ? 1u : 0u);
    }
    else
    {
        vm_mock_service_session_cancel_shop_return_loading_clear(
            session, reason ? reason : "scene-pending");
    }
    /*
     * Same-scene shop-return 2/3 is noise from early 27/11 / kind-2, not a
     * real leave.  Keep armed kind-2 so dream battle mapctrl can still fire.
     */
    if (!(session->shopReturnLoadingClearPending &&
          scene != NULL &&
          vm_net_mock_scene_name_is_safe(session->shopReturnLoadingClearScene) &&
          vm_net_mock_scene_names_equal_loose(session->shopReturnLoadingClearScene,
                                              scene)))
    {
        vm_mock_service_session_cancel_shop_return_kind2_reenter(
            session, reason ? reason : "scene-pending");
        vm_mock_service_session_cancel_shop_return_npc_catalog(
            session, reason ? reason : "scene-pending");
    }
    if (session->shopReturnDreamNeedNonSceneBattle &&
        (scene == NULL || scene[0] != '2' || scene[1] != '9'))
    {
        session->shopReturnDreamNeedNonSceneBattle = false;
        printf("[info][mock-service] shop_return_dream_nonscene_battle_clear "
               "client=%08x new_scene=%s via=%s "
               "evidence=leave-29-restore-subtype5\n",
               session->clientId, scene ? scene : "-",
               reason ? reason : "scene-pending");
    }
    /*
     * Cross-scene leave: drop this client's NPC catalog ledger so the next
     * enter can deliver nonempty 27/11.  Same-scene shop-return 2/3 must keep
     * the ledger (warm early-shell already restored type-21).
     */
    if (session->sceneNpcCatalogSeeded &&
        (scene == NULL ||
         session->sceneNpcCatalogSeededScene[0] == 0 ||
         !vm_net_mock_scene_names_equal_loose(session->sceneNpcCatalogSeededScene,
                                              scene)))
    {
        vm_mock_service_session_clear_scene_npc_catalog_seeded(
            session, reason ? reason : "scene-pending");
    }
    /*
     * Kind-2 30/1 arms a short map-stone clear for EnterScene settle.  Same-
     * scene client 2/3 must not drop it (蓬莱 shop-return after teleport).
     */
    if (session->mapStoneLoadingClearPending &&
        scene != NULL &&
        vm_net_mock_scene_name_is_safe(session->mapStoneLoadingClearScene) &&
        vm_net_mock_scene_names_equal_loose(session->mapStoneLoadingClearScene,
                                            scene))
    {
        session->mapStoneLoadingClearTick = g_schedulerTick;
        if (session->mapStoneLoadingClearRemaining < 2u)
            session->mapStoneLoadingClearRemaining = 2;
        printf("[info][mock-service] map_stone_loading_clear_hold client=%08x "
               "scene=%s remaining=%u reason=same-scene-kind2-reenter "
               "evidence=keep-30/2-after-shop-return-kind2\n",
               session->clientId,
               session->mapStoneLoadingClearScene,
               (u32)session->mapStoneLoadingClearRemaining);
    }
    else
    {
        vm_mock_service_session_cancel_map_stone_loading_clear(
            session, reason ? reason : "scene-pending");
    }
    vm_mock_service_session_clear_moveinfo(session, "scene-pending");
    vm_mock_service_session_reset_movement_rate(session, "scene-pending");
    for (u32 i = 0; i < VM_MOCK_SERVICE_PEER_SYNC_MAX; ++i)
        session->peerSync[i].visible = false;
    if (scene != NULL)
        snprintf(session->scenePendingScene, sizeof(session->scenePendingScene), "%s", scene);
    else
        session->scenePendingScene[0] = 0;
    if (changed)
    {
        printf("[info][mock-service] scene_pending client=%08x account=%s scene=%s pos=(%u,%u) reason=%s\n",
               session->clientId,
               session->accountId[0] ? session->accountId : "-",
               scene ? scene : "-",
               target ? target->x : 0,
               target ? target->y : 0,
               reason ? reason : "-");
    }
    /*
     * Leader-led transfer: pull teammates onto the same landing instead of
     * dissolving the party when scenes diverge.  Skip the fan-out when this
     * pending mark itself is already a follow arm (avoid recursion).
     */
    if (target != NULL &&
        (reason == NULL || strcmp(reason, "team-follow") != 0))
    {
        vm_mock_service_team_queue_follow_leader_scene(session, target, reason);
    }
}

static void vm_mock_service_team_auto_leave_off_leader_scene(
    vm_mock_service_client_session *session,
    const char *reason);

static void vm_mock_service_session_mark_scene_ready(vm_mock_service_client_session *session,
                                                     const char *scene,
                                                     u16 x,
                                                     u16 y,
                                                     const char *reason)
{
    bool changed = false;
    bool becameOnline = false;
    bool sceneNameChanged = false;
    char previousScene[64];

    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
    previousScene[0] = 0;
    if (vm_net_mock_scene_name_is_safe(session->sceneVisibleScene))
        snprintf(previousScene, sizeof(previousScene), "%s", session->sceneVisibleScene);
    sceneNameChanged = previousScene[0] == 0 ||
                       !vm_net_mock_scene_names_equal_loose(previousScene, scene);
    changed = !session->sceneVisibleReady ||
              session->sceneVisiblePending ||
              sceneNameChanged ||
              session->sceneVisibleX != x ||
              session->sceneVisibleY != y;
    if (changed && sceneNameChanged)
    {
        session->lastSceneMonsterLiveValid = false;
        session->lastSceneMonsterLiveScene[0] = 0;
        session->lastSceneMonsterLiveActorId = 0;
        session->lastSceneMonsterLiveIndex = 0;
        session->lastSceneMonsterLiveX = 0;
        session->lastSceneMonsterLiveY = 0;
        vm_net_mock_hangup_loop_clear("scene-changed");
    }
    session->sceneVisibleReady = true;
    session->sceneVisiblePending = false;
    snprintf(session->sceneVisibleScene, sizeof(session->sceneVisibleScene), "%s", scene);
    session->sceneVisibleX = x;
    session->sceneVisibleY = y;
    session->sceneVisibleTick = g_schedulerTick;
    session->scenePendingScene[0] = 0;
    if (session->pendingTeamFollowSceneEnter &&
        vm_net_mock_scene_names_equal_loose(session->pendingTeamFollowSceneEnterScene,
                                             scene))
    {
        vm_mock_service_session_clear_team_follow_scene_enter(session,
                                                              "scene-ready");
    }
    if (changed)
        vm_mock_service_session_reset_movement_rate(session, "scene-ready");
    becameOnline = !session->roleOnline;
    session->roleOnline = true;
    if (becameOnline)
    {
        static const char welcomeMessageGbk[] =
            "\xBB\xB6\xD3\xAD\xBD\xF8\xC8\xEB\xBD\xAD\xBA\xFE\xCA\xC0\xBD\xE7";
        u32 worldHistoryQueued = 0;
        printf("[info][mock-service] session_online client=%08x account=%s role=%u name=%s scene=%s pos=(%u,%u) reason=%s\n",
               session->clientId,
               session->accountId[0] ? session->accountId : "-",
               session->onlineRoleId,
               session->onlineRoleName[0] ? session->onlineRoleName : "-",
               session->sceneVisibleScene,
               session->sceneVisibleX,
               session->sceneVisibleY,
               reason ? reason : "-");
        if (!session->worldChatHistoryQueued)
        {
            (void)vm_mock_world_chat_queue_recent(session, &worldHistoryQueued);
            session->worldChatHistoryQueued = true;
        }
        if (!session->systemWelcomeQueued &&
            vm_mock_service_session_enqueue_system_message(session, welcomeMessageGbk))
        {
            session->systemWelcomeQueued = true;
        }
        {
            const char *practiseInfo =
                vm_net_mock_role_offline_practise_login_info();
            if (practiseInfo != NULL && practiseInfo[0] != '\0' &&
                vm_mock_service_session_enqueue_system_message_delayed(
                    session, practiseInfo, 20))
            {
                printf("[info][mock-service] practise_settle_system_chat "
                       "client=%08x role=%u\n",
                       session->clientId, session->onlineRoleId);
                vm_net_mock_role_offline_practise_clear_login_notice();
            }
        }
    }
    if (changed)
    {
        printf("[info][mock-service] scene_ready client=%08x account=%s scene=%s pos=(%u,%u) reason=%s\n",
               session->clientId,
               session->accountId[0] ? session->accountId : "-",
               session->sceneVisibleScene,
               session->sceneVisibleX,
               session->sceneVisibleY,
               reason ? reason : "-");
    }
    /*
     * Party follow: when this seat's visible scene name changes, reconcile
     * roster.  Leader landing pulls mates (team_queue_follow); only a
     * non-leader who is stranded off the leader map without a follow arm
     * still auto-leaves (5/7).
     */
    if (sceneNameChanged)
        vm_mock_service_team_auto_leave_off_leader_scene(session, "scene-mismatch");
    {
        vm_net_mock_role_state *role = vm_mock_service_trade_role_for_session(session, NULL);
        if (role == NULL)
            role = (session->clientId == g_vm_mock_service_active_client_id)
                       ? vm_net_mock_active_role()
                       : NULL;
        vm_mock_arena_rebind_session_if_needed(session, role);
        if (vm_mock_arena_team_forbidden_for_session(session))
        {
            static const char arenaTeamBanGbk[] =
                "\xb1\xc8\xce\xe4\xc6\xda\xbc\xe4\xbd\xfb\xd6\xb9\xd7\xe9\xb6\xd3";
            if (vm_mock_service_team_find_for_client(session->clientId) != NULL)
            {
                (void)vm_mock_service_session_enqueue_system_message(session,
                                                                     arenaTeamBanGbk);
                (void)vm_mock_service_team_remove_member(session, "arena-scene-ready");
            }
        }
    }
}

static u32 vm_net_mock_build_map_actor_fresh_shell_sync_response(u8 *out,
                                                                 u32 outCap);
static u32 vm_net_mock_build_active_role_relogin_response(u8 *out, u32 outCap);

static void vm_mock_service_session_cancel_map_actor_vitals_sync(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL || !session->pendingMapActorVitalsSync)
        return;
    printf("[info][mock-service] map_actor_vitals_sync_cancel client=%08x "
           "remaining=%u fresh_shell=%u reason=%s\n",
           session->clientId,
           (u32)session->pendingMapActorVitalsSyncRemaining,
           session->pendingMapActorVitalsSyncFreshShell ? 1u : 0u,
           reason ? reason : "-");
    session->pendingMapActorVitalsSync = false;
    session->pendingMapActorVitalsSyncFreshShell = false;
    session->pendingMapActorVitalsSyncEarliestTick = 0;
    session->pendingMapActorVitalsSyncRemaining = 0;
    session->pendingRevivalBagClearSeq = 0;
    session->pendingRevivalBagClearRemaining = 0;
}

static void vm_net_mock_item_equip_arm_vitals_after_gear_change(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (role == NULL || session == NULL)
        return;
    session->onlineHp = role->hp;
    session->onlineHpMax = role->hpMax ? role->hpMax : role->hp;
    session->onlineMp = role->mp;
    session->onlineMpMax = role->mpMax ? role->mpMax : role->mp;
    vm_mock_service_session_arm_map_actor_vitals_sync(session, 0, 0, false);
}

static bool vm_mock_service_session_append_login_equip_wear_sync(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount, u8 equipmentRows)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats st;
    u32 liveLen = 0;

    if (out == NULL || pos == NULL || objectCount == NULL || equipmentRows == 0)
        return false;
    if (role == NULL)
        return false;

    /*
     * Unsolicited 7/8 stuck login.  Do NOT append live 1/1/14 here: that
     * object is mmShop:0x9DE's actor-state family.  Login→mmGame 1/1/14 then
     * mall open flashes (debug-a1d304).  Tip + HP/MP sync only; panel uses
     * actorinfo / wear-apply halfwords (no host overlay file).
     */
    memset(&st, 0, sizeof(st));
    vm_net_mock_role_build_player_stats(role, &st);
    if (st.maxHp > 0)
        role->hp = st.maxHp;
    if (st.maxMp > 0)
        role->mp = st.maxMp;
    role->hpMax = st.maxHp ? st.maxHp : role->hp;
    role->mpMax = st.maxMp ? st.maxMp : role->mp;
    if (session != NULL)
    {
        session->onlineHp = role->hp;
        session->onlineHpMax = role->hpMax;
        session->onlineMp = role->mp;
        session->onlineMpMax = role->mpMax;
        session->loginEquipWearSyncPending = false;
        session->loginEquipWearSyncEarliestTick = 0;
        session->loginEquipWearSyncNextSlot = 0;
        session->loginEquipWearSyncRows = 0;
        (void)vm_mock_service_session_enqueue_system_message(
            session,
            /* 装备属性已同步 */
            "\xd7\xb0\xb1\xb8\xca\xf4\xd0\xd4\xd2\xd1\xcd\xac\xb2\xbd");
    }
    (void)liveLen;
    (void)out;
    (void)outCap;
    printf("[info][mock-service] login_equip_wear_sync_once client=%08x "
           "rows=%u wear_7_8=0 live_len=0 hp=%u/%u mp=%u/%u str=%u atk=%u "
           "tip=equip-attrs-synced evidence=no-host-overlay-file\n",
           session ? session->clientId : 0, (unsigned)equipmentRows, role->hp,
           role->hpMax, role->mp, role->mpMax, st.strength, st.attack);
    
    return true;
}

static void vm_mock_service_session_arm_map_actor_vitals_sync(
    vm_mock_service_client_session *session,
    u16 bagClearSeq,
    u32 bagClearRemaining,
    bool freshShellEnter)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 delayTicks = vm_net_mock_env_u32(
        freshShellEnter ? "CBE_MAP_ACTOR_VISUAL_DELAY_TICKS"
                        : "CBE_MAP_VITALS_SYNC_DELAY_TICKS",
        freshShellEnter ? 8u : 3u);

    if (session == NULL)
        return;
    if (delayTicks == 0)
        delayTicks = 1;
    session->pendingMapActorVitalsSync = true;
    session->pendingMapActorVitalsSyncFreshShell = freshShellEnter;
    session->pendingMapActorVitalsSyncEarliestTick = g_schedulerTick + delayTicks;
    /*
     * Deliver twice: first after Battle teardown / ScreenInit, second a few
     * ticks later in case mmGame was still reconstructing on the first packet.
     */
    session->pendingMapActorVitalsSyncRemaining = 2;
    if (bagClearSeq != 0)
    {
        session->pendingRevivalBagClearSeq = bagClearSeq;
        session->pendingRevivalBagClearRemaining = bagClearRemaining;
    }
    if (role != NULL &&
        vm_net_mock_scene_name_is_safe(role->scene) &&
        role->x != 0 && role->y != 0 &&
        (!session->sceneVisibleReady || session->sceneVisiblePending))
    {
        vm_mock_service_session_mark_scene_ready(session,
                                                 role->scene,
                                                 role->x,
                                                 role->y,
                                                 "revival-map-vitals-arm");
    }
    printf("[info][mock-service] map_actor_vitals_sync_arm client=%08x role=%u "
           "hp=%u/%u mp=%u/%u bag_clear_seq=%u earliest_tick=%u remaining=%u "
           "fresh_shell=%u\n",
           session->clientId,
           session->onlineRoleId,
           session->onlineHp,
           session->onlineHpMax,
           session->onlineMp,
           session->onlineMpMax,
           bagClearSeq,
           session->pendingMapActorVitalsSyncEarliestTick,
           session->pendingMapActorVitalsSyncRemaining,
           freshShellEnter ? 1u : 0u);
}

static void vm_mock_service_session_note_map_vitals_changed(const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (session != NULL && session->onlinePresenceValid &&
        session->onlineRoleId == role->roleId)
    {
        session->onlineHp = role->hp;
        session->onlineHpMax = role->hpMax ? role->hpMax : role->hp;
        session->onlineMp = role->mp;
        session->onlineMpMax = role->mpMax ? role->mpMax : role->mp;
    }
    vm_mock_service_session_arm_map_actor_vitals_sync(session, 0, 0, false);
    printf("[info][mock-service] map_vitals_note reason=%s role=%u hp=%u/%u "
           "mp=%u/%u\n",
           reason ? reason : "-", role->roleId, role->hp, role->hpMax, role->mp,
           role->mpMax);
}

static u32 vm_net_mock_try_deliver_pending_map_actor_vitals_sync(
    u8 *out,
    u32 outCap,
    const char *via)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u16 bagClearSeq = 0;
    u32 bagClearRemaining = 0;
    u32 responseLen = 0;
    u32 gapTicks = 0;

    if (out == NULL || outCap == 0 || session == NULL ||
        !session->pendingMapActorVitalsSync ||
        g_schedulerTick < session->pendingMapActorVitalsSyncEarliestTick)
    {
        return 0;
    }
    /*
     * Hold while mmShop owns the shell — wt/poll 1/1/14 or fresh_shell 1/1/6
     * on top of mmShop is SCR_Render NULL+0x10 (client_out resp=1156,
     * 2026-07-28).  shop-open already cancels pending; this covers races.
     */
    if (session->mmShopShellActive)
        return 0;
    /*
     * Dream drain bar clear: do not let poll/wt 1/1/6 steal seats from
     * armed 29* ResetDownloadState (debug-191169 clear_rem=3 at offline).
     */
    if (session->mapStoneLoadingClearPending &&
        session->mapStoneLoadingClearScene[0] == '2' &&
        session->mapStoneLoadingClearScene[1] == '9' &&
        session->mapStoneLoadingClearRemaining > 0)
    {
        return 0;
    }
    /*
     * Do not push 1/1/14 while Battle.cbm still owns the settle panel.  Log
     * showed vitals poll before delayed 4/8 on multi-monster victory, which
     * blanked the result UI (same class as early 4/8 tear-down).
     */
    if (g_mockBattleAwaitingSettlement != 0 ||
        g_mockBattleSettlementExitPending != 0 ||
        g_mockBattlePostExitSettlePending != 0 ||
        g_mockBattleOperateSessionArmed != 0)
    {
        return 0;
    }
    /*
     * Map-stone wait_wt6: the next type27 / WT6/1 / task-subset owns the
     * nonempty 27/11.  wt-dispatch must not replace those requests with
     * 1/1/6 or 1/1/14 — runtime stole 25/5 type27 as builtin-map-actor-
     * vitals-sync resp=440, then endless mock_scene_npc_poll_hold wait-wt6
     * with no seed_deliver (铜雀台 2026-07-27).  Scene-poll still delivers
     * the visual/vitals after the delay.
     */
    if (via != NULL &&
        strcmp(via, "wt-dispatch") == 0 &&
        g_vm_net_mock_scene_moveinfo_npc_wait_wt6 &&
        g_vm_net_mock_scene_moveinfo_npc_pending &&
        !g_vm_net_mock_scene_moveinfo_npc_seeded)
    {
        return 0;
    }
    bagClearSeq = session->pendingRevivalBagClearSeq;
    bagClearRemaining = session->pendingRevivalBagClearRemaining;
    {
        bool freshShell = session->pendingMapActorVitalsSyncFreshShell;

        if (freshShell)
        {
            responseLen =
                vm_net_mock_build_map_actor_fresh_shell_sync_response(out, outCap);
        }
        else
        {
            responseLen = vm_net_mock_build_map_actor_vitals_sync_response_ex(
                out, outCap, bagClearSeq, bagClearRemaining);
        }
        if (responseLen == 0)
        {
            printf("[warn][mock-service] map_actor_vitals_sync_build_failed "
                   "client=%08x role=%u keep_pending=1 fresh_shell=%u via=%s\n",
                   session->clientId,
                   session->onlineRoleId,
                   freshShell ? 1u : 0u,
                   via ? via : "-");
            return 0;
        }
        if (session->pendingMapActorVitalsSyncRemaining > 1)
        {
            session->pendingMapActorVitalsSyncRemaining -= 1;
            gapTicks = vm_net_mock_env_u32(
                freshShell ? "CBE_MAP_ACTOR_VISUAL_REPEAT_TICKS"
                           : "CBE_MAP_VITALS_SYNC_REPEAT_TICKS",
                freshShell ? 8u : 5u);
            if (gapTicks == 0)
                gapTicks = 1;
            session->pendingMapActorVitalsSyncEarliestTick =
                g_schedulerTick + gapTicks;
            session->pendingRevivalBagClearSeq = 0;
            session->pendingRevivalBagClearRemaining = 0;
        }
        else
        {
            session->pendingMapActorVitalsSync = false;
            session->pendingMapActorVitalsSyncFreshShell = false;
            session->pendingMapActorVitalsSyncRemaining = 0;
            session->pendingMapActorVitalsSyncEarliestTick = 0;
            session->pendingRevivalBagClearSeq = 0;
            session->pendingRevivalBagClearRemaining = 0;
        }
        printf("[info][mock-service] map_actor_vitals_sync client=%08x "
               "role=%u hp=%u/%u mp=%u/%u bag_clear_seq=%u remaining=%u "
               "fresh_shell=%u response=%s via=%s resp=%u\n",
               session->clientId,
               session->onlineRoleId,
               session->onlineHp,
               session->onlineHpMax,
               session->onlineMp,
               session->onlineMpMax,
               bagClearSeq,
               session->pendingMapActorVitalsSyncRemaining,
               freshShell ? 1u : 0u,
               freshShell ? "1/1/6-actorinfo" : "1/1/14",
               via ? via : "-",
               responseLen);
        
        return responseLen;
    }
}

static void vm_mock_service_session_update_move_position(vm_mock_service_client_session *session,
                                                         const char *scene,
                                                         u16 x,
                                                         u16 y)
{
    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    if (!session->sceneVisibleReady ||
        session->sceneVisiblePending ||
        !vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) ||
        !vm_net_mock_scene_names_equal_loose(session->sceneVisibleScene, scene))
    {
        /* A movement upload has no scene-enter completion semantics.  Only
         * the target scene's own follow-up may transition pending/not-ready
         * sessions to visible; otherwise a stale source-scene queue can leak
         * its actor into the destination scene. */
        printf("[warn][mock-service] scene_move_position_reject client=%08x "
               "visible_ready=%u visible_pending=%u visible_scene=%s "
               "move_scene=%s action=no-ready-promotion\n",
               session->clientId,
               session->sceneVisibleReady ? 1u : 0u,
               session->sceneVisiblePending ? 1u : 0u,
               session->sceneVisibleScene[0] ? session->sceneVisibleScene : "-",
               scene);
        return;
    }
    session->sceneVisibleX = x;
    session->sceneVisibleY = y;
    session->sceneVisibleTick = g_schedulerTick;
}

static void vm_mock_service_session_mark_offline(vm_mock_service_client_session *session,
                                                 const char *reason)
{
    bool wasOnline = false;

    if (session == NULL)
        return;
    wasOnline = session->roleOnline || session->onlinePresenceValid || session->sceneVisibleReady;
    /* Notify the remaining clients before clearing the departing session's
     * cached role identity; subtype 5/7 needs that id to remove its HUD row. */
    (void)vm_mock_service_team_remove_member(session, reason ? reason : "offline");
    vm_mock_service_trade_cancel_for_client(session->clientId,
                                            reason ? reason : "offline");
    vm_mock_service_duel_cancel_for_client(session->clientId,
                                           reason ? reason : "offline");
    /* Capture venue presence before sceneVisible* is cleared. */
    vm_mock_arena_on_session_offline(session);
    if (wasOnline)
    {
        if (session->accountId[0] != '\0' && session->onlineRoleId != 0)
        {
            (void)vm_net_mock_role_offline_practise_mark_logout(
                session->accountId, session->onlineRoleId);
            (void)vm_net_mock_role_exp_card_pause_on_logout(
                session->accountId, session->onlineRoleId);
            (void)vm_net_mock_role_battle_insight_pause_on_logout(
                session->accountId, session->onlineRoleId);
            (void)vm_net_mock_role_gold_scroll_pause_on_logout(
                session->accountId, session->onlineRoleId);
        }
        printf("[info][mock-service] session_offline client=%08x account=%s role=%u name=%s scene=%s pos=(%u,%u) reason=%s\n",
               session->clientId,
               session->accountId[0] ? session->accountId : "-",
               session->onlineRoleId,
               session->onlineRoleName[0] ? session->onlineRoleName : "-",
               session->sceneVisibleScene[0] ? session->sceneVisibleScene : "-",
               session->sceneVisibleX,
               session->sceneVisibleY,
               reason ? reason : "-");
        vm_net_mock_dream_sce_hot_reenter_restore_admin();
    }
    session->roleOnline = false;
    session->onlinePresenceValid = false;
    
    session->onlineRoleId = 0;
    session->onlineRoleName[0] = 0;
    session->onlineRoleTitle[0] = 0;
    session->onlineRoleTitleBadge[0] = 0;
    session->onlinePkPoints = 0;
    session->onlineEqChenghaoId = 0;
    session->onlineEqWingId = 0;
    session->onlineEqMountId = 0;
    session->onlineGuildName[0] = 0;
    session->onlineScene[0] = 0;
    session->onlineX = 0;
    session->onlineY = 0;
    session->sceneVisibleReady = false;
    session->sceneVisiblePending = false;
    session->sceneVisibleScene[0] = 0;
    session->sceneVisibleX = 0;
    session->sceneVisibleY = 0;
    session->sceneVisibleTick = g_schedulerTick;
    session->scenePendingScene[0] = 0;
    session->shopSceneNpcReseedPending = false;
    session->shopSceneNpcReseedScene[0] = 0;
    session->mmShopShellActive = false;
    session->shopActorQueryAckCount = 0;
    session->mmShopShellArmedTick = 0;
    session->mmShopOpenMarkSource = 0;
    session->shopReturnTeamPeersPending = false;
    session->pendingShopReturnSceneEnter = false;
    session->pendingShopReturnSceneEnterEarliestTick = 0;
    session->pendingShopReturnSceneEnterScene[0] = 0;
    session->pendingShopReturnSceneEnterX = 0;
    session->pendingShopReturnSceneEnterY = 0;
    session->pendingTeamFollowSceneEnter = false;
    session->pendingTeamFollowSceneEnterEarliestTick = 0;
    session->pendingTeamFollowSceneEnterScene[0] = 0;
    session->pendingTeamFollowSceneEnterX = 0;
    session->pendingTeamFollowSceneEnterY = 0;
    session->shopReturnKind2Completed = false;
    session->pendingMapActorVitalsSync = false;
    session->pendingMapActorVitalsSyncFreshShell = false;
    session->pendingMapActorVitalsSyncEarliestTick = 0;
    session->pendingMapActorVitalsSyncRemaining = 0;
    session->pendingRevivalBagClearSeq = 0;
    session->pendingRevivalBagClearRemaining = 0;
    session->awaitsBattleRevivalConfirm = false;
    session->taskPromptRefreshPending = false;
    session->taskPromptRefreshScene[0] = 0;
    if (session->chestAnnounceCount > 0)
        vm_net_mock_chest_announce_flush_pending(session);
    memset(session->socialNotices, 0, sizeof(session->socialNotices));
    memset(session->chatNotices, 0, sizeof(session->chatNotices));
    session->chatNoticeHead = 0;
    session->chatNoticeCount = 0;
    session->systemWelcomeQueued = false;
    session->worldChatHistoryQueued = false;
    session->friendInviteReplyActive = false;
    session->friendInviteSourceClientId = 0;
    session->friendInviteSourceRoleId = 0;
    session->guildInviteReplyActive = false;
    session->guildInviteSourceClientId = 0;
    session->guildInviteSourceRoleId = 0;
    session->guildInviteGuildId = 0;
    session->tradeInviteReplyActive = false;
    session->tradeInviteSourceClientId = 0;
    session->tradeInviteSourceRoleId = 0;
    session->teamInviteReplyActive = false;
    session->teamInviteSourceClientId = 0;
    session->teamInviteSourceWireId = 0;
    session->sparInviteReplyActive = false;
    session->sparInviteSourceClientId = 0;
    session->sparInviteSourceWireId = 0;
    session->sparBattleReadyPending = false;
    session->sparBattlePeerClientId = 0;
    session->sparBattlePeerWireId = 0;
    session->sparExitRedeliverUntilTick = 0;
    session->sparExitRecoverHp = 0;
    session->sparExitRecoverMp = 0;
    session->sparExitWasDead = false;
    session->sparExitMessageQueued = false;
    session->sparExitExpGranted = false;
    session->sparExitPenaltyApplied = false;
    session->sparDeathExpPenalty = 0;
    session->sparDeathMoneyPenalty = 0;
    session->sparDeathRespawnPending = false;
    session->sparDeathRespawnScene[0] = 0;
    session->sparDeathRespawnX = 0;
    session->sparDeathRespawnY = 0;
    session->sparSettleDelivered = false;
    session->sparResultMessageArmed = false;
    session->sparResultNotBeforeTick = 0;
    session->arenaResultPending = false;
    session->arenaResultFdata[0] = 0;
    session->pendingTeamBattleSerial = 0;
    session->instanceChallengePending = false;
    session->instanceChallengeBattlePending = false;
    session->instanceChallengeDirectPending = false;
    session->instanceChallengeBattleWireCount = 0;
    session->instanceChallengeActorId = 0;
    session->instanceChallengeEnemyId = 0;
    session->instanceChallengeX = 0;
    session->instanceChallengeY = 0;
    session->instanceChallengeTick = 0;
    session->instanceChallengeScene[0] = 0;
    session->instanceChallengeQuotaScene[0] = 0;
    session->instanceChallengeHangupStyleArm = false;
    session->instanceChallengeHangupStyleTick = 0;
    session->instanceHangupEnemyValid = false;
    session->instanceHangupEnemyId = 0;
    session->instanceHangupActorId = 0;
    session->instanceHangupScene[0] = 0;
    session->lastSceneMonsterLiveValid = false;
    session->lastSceneMonsterLiveScene[0] = 0;
    session->lastSceneMonsterLiveActorId = 0;
    session->lastSceneMonsterLiveIndex = 0;
    session->lastSceneMonsterLiveX = 0;
    session->lastSceneMonsterLiveY = 0;
    session->npcPurchaseBackpackPending = false;
    session->npcPurchaseBackpackPhase = 0;
    session->npcPurchaseBackpackSeq = 0;
    session->npcPurchaseBackpackItemId = 0;
    session->npcPurchaseBackpackCount = 0;
    session->npcPurchaseBackpackEnhance = 0;
    session->npcPurchaseBackpackTick = 0;
    session->shopFlaskLoadingClearPending = false;
    session->shopFlaskLoadingClearSeq = 0;
    session->shopFlaskLoadingClearItemId = 0;
    session->shopFlaskLoadingClearReservoir = 0;
    session->shopFlaskLoadingClearTick = 0;
    session->shopFlaskBackpackDeliverPending = false;
    session->shopFlaskBackpackDeliverSeq = 0;
    session->shopFlaskBackpackDeliverItemId = 0;
    session->shopFlaskBackpackDeliverReservoir = 0;
    session->shopFlaskBackpackDeliverTick = 0;
    session->mapStoneLoadingClearPending = false;
    session->mapStoneLoadingClearRemaining = 0;
    session->mapStoneLoadingClearTick = 0;
    session->mapStoneLoadingClearScene[0] = 0;
    session->dreamExitReenterSettleRequired = false;
    session->dreamHudTtlArmed = false;
    session->dreamHudTtlRoleId = 0;
    session->dreamHudExpireAtMs = 0;
    session->dreamHudWarnSent = false;
    session->dreamForceColdSceResync = false;
    session->shopReturnLoadingClearPending = false;
    session->shopReturnLoadingClearRemaining = 0;
    session->shopReturnLoadingClearTick = 0;
    session->shopReturnLoadingClearArmTick = 0;
    session->shopReturnLoadingClearScene[0] = 0;
    session->shopReturnBusyAckPending = false;
    session->shopReturnBusyAckTick = 0;
    session->shopReturnNpcCatalogPending = false;
    session->shopReturnNpcCatalogScene[0] = 0;
    session->shopReturnNpcCatalogReadyTick = 0;
    session->shopReturnWarmNpcDelivered = false;
    session->shopReturnNpcResourcesWarm = false;
    session->shopReturnNpcInlineOnShell = false;
    session->sceneNpcCatalogSeeded = false;
    session->sceneNpcCatalogSeededScene[0] = 0;
    session->shopReturnDreamNeedNonSceneBattle = false;
    session->shopReturnDreamNeedSettleBarClear = false;
    session->shopReturnDreamNeedHudRebuild = false;
    session->shopReturnDreamHudMapStoneHold = false;
    session->shopReturnLoadingClearPostCatalog = false;
    session->shopReturnLoadingClearLite = false;
    session->npcShopBrowseValid = false;
    session->npcShopBrowseSelector = 0;
    session->npcShopBrowseLevelBand = 0;
    session->npcShopBrowsePage = 0;
    session->warehouseBrowseValid = false;
    session->warehouseBrowseKind = 0;
    session->warehouseBrowsePage = 0;
    session->warehouseSessionArmed = false;
    session->warehouseDialogPending = false;
    session->warehouseDialogTick = 0;
    session->equipSellSessionArmed = false;
    session->equipSellDialogPending = false;
    session->coolmallSessionArmed = false;
    session->coolmallDialogPending = false;
    session->coolmallDialogTick = 0;
    session->coolmallOpenWaitMap = false;
    session->pendingMmshopExitRoleRelogin = false;
    session->pendingMmshopExitRoleReloginTick = 0;
    session->coolmallBrowseSubtype = 0;
    session->coolmallBrowsePage = 0;
    session->equipSellDialogTick = 0;
    session->equipSellBrowseValid = false;
    session->equipSellBrowsePage = 0;
    session->skillBrowsePage = 0;
    session->backpackListResyncPending = false;
    session->backpackListResyncPhase = 0;
    session->backpackListResyncListOnly = false;
    session->backpackListResyncTick = 0;
    session->backpackResyncRemoveSeq = 0;
    session->backpackResyncRemoveItemId = 0;
    session->quickRepairQuotePending = false;
    session->quickRepairQuoteType = 0;
    session->quickRepairQuoteSeq = 0;
    session->quickRepairQuoteItemId = 0;
    session->quickRepairQuoteHasItemId = false;
    session->quickRepairQuoteTick = 0;
    session->quickRepairEquipSyncPending = false;
    session->quickRepairEquipSyncPhase = 0;
    session->quickRepairEquipSyncTick = 0;
    session->loginEquipWearSyncPending = false;
    session->loginEquipWearSyncEarliestTick = 0;
    session->loginEquipWearSyncNextSlot = 0;
    session->loginEquipWearSyncRows = 0;
    vm_mock_service_session_clear_auto_timers(session);
    if (session->accountId[0] != 0 &&
        g_vm_net_mock_warehouse.loaded &&
        g_vm_net_mock_warehouse.accountId[0] != 0 &&
        vm_mock_service_account_id_equal(g_vm_net_mock_warehouse.accountId,
                                         session->accountId))
    {
        g_vm_net_mock_warehouse.loaded = false;
    }
    vm_mock_service_session_clear_moveinfo(session, reason ? reason : "offline");
    vm_mock_service_session_reset_movement_rate(session, reason ? reason : "offline");
    for (u32 i = 0; i < VM_MOCK_SERVICE_PEER_SYNC_MAX; ++i)
        session->peerSync[i].visible = false;
}

static void vm_mock_service_mark_active_session_scene_pending(const vm_net_mock_scene_change_target *target,
                                                              const char *reason)
{
    vm_mock_service_session_mark_scene_pending(vm_mock_service_get_active_client_session(),
                                               target,
                                               reason);
}

static void vm_mock_service_mark_active_session_scene_ready(const char *scene,
                                                            u16 x,
                                                            u16 y,
                                                            const char *reason)
{
    vm_mock_service_session_mark_scene_ready(vm_mock_service_get_active_client_session(),
                                             scene,
                                             x,
                                             y,
                                             reason);
}

static const char *vm_mock_service_find_session_account(u32 clientId)
{
    vm_mock_service_client_session *session = vm_mock_service_find_client_session(clientId);
    if (clientId == 0)
        return NULL;
    return session ? session->accountId : NULL;
}

/* A CBE/Pixi client restart allocates a new transport client id.  A direct
 * process exit does not necessarily emit CBMS CLIENT_DISCONNECT, so waiting for
 * the heartbeat timeout would leave the prior incarnation visible as a second
 * copy of the same role during the next scene's 2/10 baseline.  Authentication
 * is the authoritative ownership boundary: one account has one live service
 * session.  Reuse the normal offline transition so team/trade/duel state is
 * removed consistently before the new client enters a scene — and arm
 * accountKickedPending so the displaced client is told to leave on its next
 * request instead of silently sharing account globals. */
static u32 vm_mock_service_session_take_over_account(u32 authenticatedClientId,
                                                     const char *accountId)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;
    u32 displacedCount = 0;

    if (authenticatedClientId == 0 || accountId == NULL || accountId[0] == 0)
        return 0;
    while (session != NULL)
    {
        vm_mock_service_client_session *next = session->next;

        if (session->clientId != authenticatedClientId &&
            session->accountId[0] != 0 &&
            vm_mock_service_account_id_equal(session->accountId, accountId))
        {
            printf("[info][mock-service] session_account_takeover account=%s new_client=%08x old_client=%08x old_role=%u old_scene=%s role_online=%u presence=%u scene_ready=%u action=kick-offline\n",
                   accountId,
                   authenticatedClientId,
                   session->clientId,
                   session->onlineRoleId,
                   session->sceneVisibleScene[0] ? session->sceneVisibleScene : "-",
                   session->roleOnline ? 1u : 0u,
                   session->onlinePresenceValid ? 1u : 0u,
                   session->sceneVisibleReady ? 1u : 0u);
            session->accountKickedPending = true;
            vm_mock_service_account_flush_for_session(session, "account-login-takeover");
            vm_mock_service_session_mark_offline(session, "account-login-takeover");
            vm_mock_service_session_unbind_account(session);
            ++displacedCount;
        }
        session = next;
    }
    return displacedCount;
}

static void vm_mock_service_bind_session_account(u32 clientId, const char *accountId)
{
    vm_mock_service_client_session *session = NULL;
    u32 displacedCount = 0;
    if (clientId == 0 || accountId == NULL || accountId[0] == 0)
        return;
    session = vm_mock_service_get_or_create_client_session(clientId);
    if (session == NULL)
        return;
    if (session->accountId[0] != 0)
    {
        bool sameAccount =
            vm_mock_service_account_id_equal(session->accountId, accountId);
        printf("[info][mock-service] session_login_lifecycle_reset client=%08x old_account=%s new_account=%s same=%u scene_ready=%u role_online=%u evidence=runtime:return-title-relogin-before-scene-init\n",
               clientId,
               session->accountId,
               accountId,
               sameAccount ? 1u : 0u,
               session->sceneVisibleReady ? 1u : 0u,
               session->roleOnline ? 1u : 0u);
        vm_mock_service_account_flush_for_session(
            session,
            sameAccount ? "title-login-rebind" : "account-rebind");
        vm_mock_service_session_mark_offline(
            session,
            sameAccount ? "title-login-rebind" : "account-rebind");
        /* Different account: drop the old binding so its roleDb can be freed.
         * Same-account title rebind keeps the binding (and heap cache) alive. */
        if (!sameAccount)
            vm_mock_service_session_unbind_account(session);
    }
    displacedCount = vm_mock_service_session_take_over_account(clientId, accountId);
    snprintf(session->accountId, sizeof(session->accountId), "%s", accountId);
    printf("[info][mock-service] session_bind client=%08x account=%s displaced=%u\n",
           clientId, accountId, displacedCount);
}

static void vm_mock_service_capture_session_presence(u32 clientId)
{
    vm_mock_service_client_session *session = NULL;
    vm_net_mock_role_state *role = NULL;
    const char *scene = NULL;
    const char *roleScene = NULL;
    const vm_net_mock_designation_entry *designation = NULL;
    u16 x = 0;
    u16 y = 0;
    u32 hp = 0;
    u32 hpMax = 0;
    u32 mp = 0;
    u32 mpMax = 0;
    bool visiblePosChanged = false;
    bool hadPresence = false;
    bool vitalsChanged = false;

    if (clientId == 0)
        return;
    session = vm_mock_service_get_or_create_client_session(clientId);
    if (session == NULL)
        return;
    hadPresence = session->onlinePresenceValid;
    session->onlinePresenceValid = false;
    role = vm_net_mock_active_role();
    if (role == NULL)
        return;
    designation = vm_net_mock_role_designation(role);
    roleScene = vm_net_mock_scene_name_is_safe(role->scene) ? role->scene : vm_net_mock_current_scene_name();
    scene = roleScene;
    x = role->x;
    y = role->y;
    vm_net_mock_role_default_vitals(role, &hp, &hpMax, &mp, &mpMax);
    /*
     * During a shared team battle the service-local battleMember* snapshot is
     * authoritative.  Presence capture previously rebuilt vitals from the
     * durable role row / per-account battle globals, which could still hold
     * pre-skill max MP and let the next teaminfo row refill the caster bar.
     */
    {
        vm_mock_service_team *team =
            vm_mock_service_team_find_for_client(session->clientId);
        int memberIndex = -1;

        if (team != NULL && team->battleActive && session->clientId != 0)
        {
            for (u8 i = 0; i < team->battleMemberCount; ++i)
            {
                if (team->battleMemberClientIds[i] == session->clientId)
                {
                    memberIndex = (int)i;
                    break;
                }
            }
        }
        if (memberIndex >= 0 && memberIndex < team->battleMemberCount)
        {
            u8 memberBit = (u8)(1u << memberIndex);

            /*
             * Revival/escape exit keeps battleMemberHp at 0 on purpose.  That
             * snapshot must not replace durable presence HP or the next
             * team_begin_battle seeds the seat as dead.
             */
            if ((team->battleMemberLeftMask & memberBit) == 0)
            {
                hpMax = team->battleMemberHpMax[memberIndex]
                            ? team->battleMemberHpMax[memberIndex]
                            : hpMax;
                hp = vm_net_mock_min_u32(team->battleMemberHp[memberIndex], hpMax);
                mpMax = team->battleMemberMpMax[memberIndex]
                            ? team->battleMemberMpMax[memberIndex]
                            : mpMax;
                mp = vm_net_mock_min_u32(team->battleMemberMp[memberIndex], mpMax);
            }
        }
        else if (g_mockBattleOperateSessionArmed != 0 &&
                 g_mockBattleSceneMonsterStartActive != 0)
        {
            if (g_mockBattleRoleHpMax != 0)
            {
                hpMax = g_mockBattleRoleHpMax;
                hp = vm_net_mock_min_u32(g_mockBattleRoleHpCurrent, hpMax);
            }
            if (g_mockBattleRoleMpMax != 0)
            {
                mpMax = g_mockBattleRoleMpMax;
                mp = vm_net_mock_min_u32(g_mockBattleRoleMpCurrent, mpMax);
            }
        }
    }
    /*
     * Nearby-player visibility is per client session. A single global
     * last-moveinfo source is still useful for local scene-transition
     * heuristics, but reusing it here cross-contaminates positions between
     * two online clients in the same scene. Prefer this session's own latest
     * visible scene/pos instead.
     */
    if (session->sceneVisibleReady &&
        !session->sceneVisiblePending &&
        vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) &&
        (roleScene == NULL ||
         roleScene[0] == 0 ||
         vm_net_mock_scene_names_equal_loose(session->sceneVisibleScene, roleScene)))
    {
        scene = session->sceneVisibleScene;
        x = session->sceneVisibleX;
        y = session->sceneVisibleY;
    }
    if (!vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    vitalsChanged = hadPresence && session->onlineRoleId == role->roleId &&
                    (session->onlineHp != hp ||
                     session->onlineHpMax != hpMax ||
                     session->onlineMp != mp ||
                     session->onlineMpMax != mpMax);
    session->onlinePresenceValid = true;
    session->onlineRoleId = role->roleId;
    snprintf(session->onlineRoleName, sizeof(session->onlineRoleName), "%s",
             role->name[0] ? role->name : vm_net_mock_default_role_name());
    snprintf(session->onlineRoleTitle, sizeof(session->onlineRoleTitle), "%s",
             vm_net_mock_role_title(role));
    snprintf(session->onlineRoleTitleBadge, sizeof(session->onlineRoleTitleBadge), "%s",
             designation ? designation->overheadResource : "");
    session->onlineJob = role->job;
    session->onlineSex = role->sex;
    session->onlineLevel = (u16)(role->level ? role->level : 1);
    memcpy(session->onlineEquippedItemIds, role->equippedItemIds,
           sizeof(session->onlineEquippedItemIds));
    memcpy(session->onlineEquippedEnhanceLevels, role->equippedEnhanceLevels,
           sizeof(session->onlineEquippedEnhanceLevels));
    session->onlinePkPoints = role->pkPoints;
    session->onlineEqChenghaoId = role->equippedChenghaoId;
    session->onlineEqWingId = role->equippedWingId;
    session->onlineEqMountId = role->equippedMountId;
    /*
     * Peer nameplate「门派-xxx」reads 1/31/5.guild from onlineGuildName.
     * Earlier captures only mirrored eq* — peers with wing/mount never got a
     * guild field on the first 31/5.  Fill here (role_guild_info is GBK).
     */
    {
        char guildName[VM_NET_MOCK_GUILD_NAME_SIZE];

        memset(session->onlineGuildName, 0, sizeof(session->onlineGuildName));
        memset(guildName, 0, sizeof(guildName));
        (void)vm_net_mock_role_guild_info(role, guildName, sizeof(guildName));
        if (guildName[0] != 0 &&
            strcmp(guildName, "\xce\xde\xb0\xef\xc5\xc9") != 0)
            snprintf(session->onlineGuildName, sizeof(session->onlineGuildName),
                     "%s", guildName);
    }
    session->onlineHp = hp;
    session->onlineHpMax = hpMax;
    session->onlineMp = mp;
    session->onlineMpMax = mpMax;
    snprintf(session->onlineScene, sizeof(session->onlineScene), "%s", scene);
    session->onlineX = x;
    session->onlineY = y;
    session->onlineTick = g_schedulerTick;
    if (session->sceneVisibleReady &&
        !session->sceneVisiblePending &&
        vm_net_mock_scene_name_is_safe(session->sceneVisibleScene) &&
        vm_net_mock_scene_names_equal_loose(session->sceneVisibleScene, scene))
    {
        visiblePosChanged = session->sceneVisibleX != x || session->sceneVisibleY != y;
        session->sceneVisibleX = x;
        session->sceneVisibleY = y;
        session->sceneVisibleTick = g_schedulerTick;
        if (visiblePosChanged)
        {
            printf("[debug][mock-service] scene_visible_pos client=%08x scene=%s pos=(%u,%u)\n",
                   session->clientId,
                   session->sceneVisibleScene,
                   session->sceneVisibleX,
                   session->sceneVisibleY);
        }
    }
    if (vitalsChanged && session->roleOnline)
    {
        /* Group subtype 5/11 is the client-owned HP/MP update path. Queue it
         * for the next existing poll instead of pushing into an unsolicited
         * emulator socket. */
        vm_mock_service_team_enqueue_hsp_for_members(session);
    }
}

static bool vm_mock_service_mark_active_session_scene_ready_from_role(const char *sceneHint,
                                                                      const char *reason)
{
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *roleScene = NULL;

    if (session == NULL || role == NULL || role->x == 0 || role->y == 0)
        return false;
    roleScene = vm_net_mock_scene_name_is_safe(role->scene) ? role->scene : sceneHint;
    if (!vm_net_mock_scene_name_is_safe(roleScene) ||
        (vm_net_mock_scene_name_is_safe(sceneHint) &&
         !vm_net_mock_scene_names_equal_loose(roleScene, sceneHint)))
    {
        return false;
    }
    /*
     * The role DB is the authoritative server-side restore point. This helper
     * is called only after the client's post-enter task subset request, so the
     * coordinates are neither inferred from another player nor guessed from a
     * scene default.
     */
    vm_mock_service_capture_session_presence(session->clientId);
    vm_mock_service_session_mark_scene_ready(session,
                                             roleScene,
                                             role->x,
                                             role->y,
                                             reason);
    return session->sceneVisibleReady;
}

/*
 * mmShop discards the live mmGame shell.  Server peerSync[].visible often still
 * says nearby roles were already delivered, so the next scene-sync poll skips
 * the 2/10 baseline and the fresh client never recreates peer nodes.
 */
static void vm_mock_service_session_invalidate_nearby_observer_cursors(
    vm_mock_service_client_session *session,
    const char *reason)
{
    u32 cleared = 0;

    if (session == NULL)
        return;
    for (u32 i = 0; i < VM_MOCK_SERVICE_PEER_SYNC_MAX; ++i)
    {
        if (!session->peerSync[i].visible)
            continue;
        session->peerSync[i].visible = false;
        ++cleared;
    }
    printf("[info][mock-service] nearby_observer_cursors_invalidate client=%08x cleared=%u reason=%s evidence=mmShop-return-fresh-mmGame\n",
           session->clientId,
           cleared,
           reason ? reason : "-");
}

static void vm_mock_service_team_enqueue_hsp_resync_for_client(
    vm_mock_service_client_session *session)
{
    vm_mock_service_team *team = NULL;

    if (session == NULL || session->clientId == 0 || session->onlineRoleId == 0)
        return;
    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team == NULL)
        return;

    /* Teammates relearn this member's map vitals. */
    vm_mock_service_team_enqueue_hsp_for_members(session);
    /* Returning client relearns every other member's map vitals. */
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        vm_mock_service_client_session *peer =
            vm_mock_service_find_client_session(team->memberClientIds[member]);
        if (peer == NULL || peer->clientId == session->clientId ||
            peer->onlineRoleId == 0)
        {
            continue;
        }
        (void)vm_mock_service_session_enqueue_social_notice(
            session, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_HSP, 0,
            peer, NULL, peer->accountId);
    }
}

static void vm_mock_service_team_enqueue_shop_return_peer_joins(
    vm_mock_service_client_session *session)
{
    vm_mock_service_team *team = NULL;
    u8 queued = 0;

    if (session == NULL || !session->shopReturnTeamPeersPending)
        return;
    session->shopReturnTeamPeersPending = false;
    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team == NULL || !team->active)
        return;

    /*
     * Fresh mmGame already has the local row from the deferred solo 5/10.
     * Subtype 5/5 appends each peer exactly as the invite-accept path does;
     * do not re-emit a full 5/10 (AddRoleToList would duplicate).
     */
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        vm_mock_service_client_session *peer =
            vm_mock_service_find_client_session(team->memberClientIds[member]);
        if (peer == NULL || peer->clientId == session->clientId ||
            peer->onlineRoleId == 0)
        {
            continue;
        }
        if (vm_mock_service_session_enqueue_social_notice(
                session, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_MEMBER_JOIN, 0,
                peer, NULL, peer->accountId))
        {
            ++queued;
        }
    }
    printf("[info][mock-service] shop_return_team_peers_queue client=%08x "
           "peers=%u delivery=5/5-after-scene-enter "
           "evidence=JianghuOL.CBE:0x01011E1E+0x01014388\n",
           session->clientId, queued);
}

/*
 * Shop-return WT6/1 / task-subset must not carry login actorinfo.  After the
 * same-packet 27/11 + no-posinfo 30/2 closes download state, rehydrate
 * poll-side state only.
 */
static void vm_mock_service_session_cancel_map_stone_loading_clear(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL || !session->mapStoneLoadingClearPending)
        return;
    printf("[info][mock-service] map_stone_loading_clear_cancel client=%08x "
           "armed_scene=%s remaining=%u reason=%s settle=%u\n",
           session->clientId,
           session->mapStoneLoadingClearScene[0]
               ? session->mapStoneLoadingClearScene
               : "-",
           (u32)session->mapStoneLoadingClearRemaining,
           reason ? reason : "-",
           session->dreamExitReenterSettleRequired ? 1u : 0u);
    session->mapStoneLoadingClearPending = false;
    session->mapStoneLoadingClearRemaining = 0;
    session->mapStoneLoadingClearTick = 0;
    session->mapStoneLoadingClearScene[0] = 0;
    session->shopReturnDreamHudMapStoneHold = false;
    /* Drop reenter gate when the owed outdoor clear is cancelled/expired. */
    session->dreamExitReenterSettleRequired = false;
}

static void vm_mock_service_session_arm_map_stone_loading_clear(
    vm_mock_service_client_session *session,
    const char *scene)
{
    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene))
        return;
    session->mapStoneLoadingClearPending = true;
    session->mapStoneLoadingClearRemaining = 3;
    session->mapStoneLoadingClearTick = g_schedulerTick;
    snprintf(session->mapStoneLoadingClearScene,
             sizeof(session->mapStoneLoadingClearScene),
             "%s",
             scene);
    printf("[info][mock-service] map_stone_loading_clear_arm client=%08x scene=%s "
           "remaining=3 evidence=30/2-no-posinfo-after-async-EnterScene\n",
           session->clientId,
           scene);
}

/*
 * type27 / WT6/1 / task-subset just delivered wait_wt6 27/11 without trailing
 * 30/2.  Restart the age clock so poll ResetDownloadState lands after that
 * follow-up and the late 27/12+posinfo ScreenInit, not against the stale 2/3 arm.
 */
static void vm_mock_service_session_rearm_map_stone_loading_clear(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL || !session->mapStoneLoadingClearPending)
        return;
    session->mapStoneLoadingClearTick = g_schedulerTick;
    if (session->mapStoneLoadingClearRemaining < 2u)
        session->mapStoneLoadingClearRemaining = 2;
    printf("[info][mock-service] map_stone_loading_clear_rearm client=%08x "
           "scene=%s remaining=%u reason=%s evidence=wait_wt6-followup-owns-no-30/2\n",
           session->clientId,
           session->mapStoneLoadingClearScene[0]
               ? session->mapStoneLoadingClearScene
               : "-",
           (u32)session->mapStoneLoadingClearRemaining,
           reason ? reason : "-");
}

static void vm_mock_service_session_cancel_shop_return_busy_ack(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL || !session->shopReturnBusyAckPending)
        return;
    printf("[info][mock-service] shop_return_busy_ack_cancel client=%08x reason=%s\n",
           session->clientId,
           reason ? reason : "-");
    session->shopReturnBusyAckPending = false;
    session->shopReturnBusyAckTick = 0;
}

static void vm_mock_service_session_cancel_shop_return_npc_catalog(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL)
        return;
    if (session->shopReturnNpcCatalogPending)
    {
        printf("[info][mock-service] shop_return_npc_catalog_cancel client=%08x "
               "scene=%s reason=%s\n",
               session->clientId,
               session->shopReturnNpcCatalogScene[0]
                   ? session->shopReturnNpcCatalogScene
                   : "-",
               reason ? reason : "-");
    }
    session->shopReturnNpcCatalogPending = false;
    session->shopReturnNpcCatalogScene[0] = 0;
    session->shopReturnNpcCatalogReadyTick = 0;
    session->shopReturnWarmNpcDelivered = false;
    session->shopReturnNpcResourcesWarm = false;
    session->shopReturnNpcInlineOnShell = false;
}

static void vm_mock_service_session_note_dream_shop_return_kind2_skip(
    vm_mock_service_client_session *session,
    const char *scene)
{
    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene))
        return;
    if (scene[0] != '2' || scene[1] != '9')
        return;
    /*
     * Outdoor-warm contract (续 10): NPC maps skip kind-2 30/1 entirely.
     * 续 12 armed NeedHudRebuild → busy_ack deferred 30/1 for the instance
     * timer; that third EnterScene reopened multi-load + stuck bar
     * (588cdfc4 2026-08-05).  Keep settle-bar-only; HUD timer without
     * EnterScene remains unresolved (client-local ctor, no WT).
     */
    session->shopReturnDreamNeedSettleBarClear = true;
    session->shopReturnDreamNeedHudRebuild = false;
    session->shopReturnDreamHudMapStoneHold = false;
    session->shopReturnKind2Completed = true;
    if (session->pendingShopReturnSceneEnter)
    {
        vm_mock_service_session_cancel_shop_return_kind2_reenter(
            session, "kind2-skip-warm-no-hud-30/1");
    }
    printf("[info][mock-service] shop_return_dream_settle_bar_arm "
           "client=%08x scene=%s "
           "evidence=kind2-skip-shell-settlement-exit-4/8-only\n",
           session->clientId, scene);
    printf("[info][mock-service] shop_return_dream_hud_rebuild_skip "
           "client=%08x scene=%s "
           "evidence=warm-single-load-forbids-deferred-30/1 "
           "unresolved=instance-timer-without-EnterScene\n",
           session->clientId, scene);
}

static void vm_mock_service_session_arm_shop_return_busy_ack(
    vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->shopReturnBusyAckPending = true;
    session->shopReturnBusyAckTick = g_schedulerTick;
    printf("[info][mock-service] shop_return_busy_ack_arm client=%08x "
           "evidence=26/0-after-7/7-shop-return-30/2\n",
           session->clientId);
}

static void vm_mock_service_session_cancel_shop_return_kind2_reenter(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL || !session->pendingShopReturnSceneEnter)
        return;
    printf("[info][mock-service] shop_return_kind2_reenter_cancel client=%08x "
           "scene=%s pos=(%u,%u) reason=%s\n",
           session->clientId,
           session->pendingShopReturnSceneEnterScene[0]
               ? session->pendingShopReturnSceneEnterScene
               : "-",
           session->pendingShopReturnSceneEnterX,
           session->pendingShopReturnSceneEnterY,
           reason ? reason : "-");
    session->pendingShopReturnSceneEnter = false;
    session->pendingShopReturnSceneEnterEarliestTick = 0;
    session->pendingShopReturnSceneEnterScene[0] = 0;
    session->pendingShopReturnSceneEnterX = 0;
    session->pendingShopReturnSceneEnterY = 0;
    if (session->shopReturnDreamNeedHudRebuild)
    {
        session->shopReturnDreamNeedHudRebuild = false;
        printf("[info][mock-service] shop_return_dream_hud_rebuild_cancel "
               "client=%08x reason=%s\n",
               session->clientId, reason ? reason : "-");
    }
}

static void vm_mock_service_session_arm_shop_return_kind2_reenter(
    vm_mock_service_client_session *session,
    const char *scene,
    const char *reason)
{
    vm_net_mock_role_state *role = NULL;
    char targetScene[64];
    u16 x = 0;
    u16 y = 0;
    u8 npcCount = 0;

    /*
     * Shop-return kind-2 rebuild (portals + combat nodes).  Empty maps need
     * this after shell 30/2.  NPC maps arm it only AFTER poll 27/11 +
     * post-catalog 30/2 so loading is live before same-scene 30/1
     * (docs/re/2026-07-28-penglai-shop-return-after-teleport-stall.md).
     * Set CBE_SHOP_RETURN_KIND2=0 only to isolate mall/loading regressions.
     */
    if (vm_net_mock_env_u32("CBE_SHOP_RETURN_KIND2", 1) == 0)
    {
        printf("[info][mock-service] shop_return_kind2_reenter_skip client=%08x "
               "reason=disabled-env via=%s scene=%s\n",
               session ? session->clientId : 0,
               reason ? reason : "-",
               scene ? scene : "-");
        return;
    }
    if (session == NULL)
        return;
    if (session->shopReturnKind2Completed)
    {
        printf("[info][mock-service] shop_return_kind2_reenter_skip client=%08x "
               "reason=already-completed via=%s scene=%s "
               "evidence=one-kind2-per-mmShop-exit\n",
               session->clientId,
               reason ? reason : "-",
               scene ? scene : "-");
        return;
    }
    if (session->pendingShopReturnSceneEnter)
        return;
    targetScene[0] = 0;
    if (vm_net_mock_scene_name_is_safe(scene))
        snprintf(targetScene, sizeof(targetScene), "%s", scene);
    else if (vm_net_mock_scene_name_is_safe(session->sceneVisibleScene))
        snprintf(targetScene, sizeof(targetScene), "%s",
                 session->sceneVisibleScene);
    if (!vm_net_mock_scene_name_is_safe(targetScene))
    {
        printf("[warn][mock-service] shop_return_kind2_reenter_skip client=%08x "
               "reason=bad-scene via=%s\n",
               session->clientId,
               reason ? reason : "-");
        return;
    }

    npcCount = vm_net_mock_scene_room_npc_seed_count(targetScene);

    role = vm_mock_service_trade_role_for_session(session, NULL);
    if (role != NULL && role->x != 0 && role->y != 0)
    {
        x = role->x;
        y = role->y;
        if (vm_net_mock_scene_name_is_safe(role->scene) &&
            vm_net_mock_scene_names_equal_loose(role->scene, targetScene))
        {
            snprintf(targetScene, sizeof(targetScene), "%s", role->scene);
        }
    }
    else if (session->sceneVisibleX != 0 && session->sceneVisibleY != 0)
    {
        x = session->sceneVisibleX;
        y = session->sceneVisibleY;
    }
    else if (session->onlineX != 0 && session->onlineY != 0)
    {
        x = session->onlineX;
        y = session->onlineY;
    }
    if (x == 0 || y == 0)
    {
        printf("[warn][mock-service] shop_return_kind2_reenter_skip client=%08x "
               "scene=%s reason=no-pos via=%s\n",
               session->clientId,
               targetScene,
               reason ? reason : "-");
        return;
    }

    vm_net_mock_adjust_safe_player_pos_for_scene(targetScene, &x, &y);
    session->pendingShopReturnSceneEnter = true;
    /*
     * Moveinfo already proved the map walkable — deliver 30/1 on the next poll
     * instead of waiting another 8 ticks (menu-open window + "late reload").
     */
    if (reason != NULL &&
        (strcmp(reason, "moveinfo-live-post-catalog") == 0 ||
         strcmp(reason, "moveinfo-live") == 0 ||
         strcmp(reason, "loading-clear-timeout") == 0 ||
         strcmp(reason, "dream-battle-mapctrl-moveinfo") == 0 ||
         strcmp(reason, "dream-battle-mapctrl") == 0 ||
         strcmp(reason, "dream-hud-timer-rebuild") == 0))
    {
        session->pendingShopReturnSceneEnterEarliestTick = g_schedulerTick;
    }
    else
    {
        session->pendingShopReturnSceneEnterEarliestTick =
            g_schedulerTick + VM_MOCK_SHOP_RETURN_KIND2_DELAY_TICKS;
    }
    snprintf(session->pendingShopReturnSceneEnterScene,
             sizeof(session->pendingShopReturnSceneEnterScene),
             "%s",
             targetScene);
    session->pendingShopReturnSceneEnterX = x;
    session->pendingShopReturnSceneEnterY = y;
    printf("[info][mock-service] shop_return_kind2_reenter_arm client=%08x "
           "scene=%s pos=(%u,%u) delay_ticks=%u npcnum=%u via=%s "
           "evidence=shop-return-deferred-30/1-then-27/11\n",
           session->clientId,
           targetScene,
           x,
           y,
           (session->pendingShopReturnSceneEnterEarliestTick > g_schedulerTick)
               ? (u32)(session->pendingShopReturnSceneEnterEarliestTick -
                       g_schedulerTick)
               : 0u,
           (u32)npcCount,
           reason ? reason : "-");
}

static void vm_mock_service_session_cancel_shop_return_loading_clear(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL)
        return;
    if (session->shopReturnLoadingClearPending)
    {
        printf("[info][mock-service] shop_return_loading_clear_cancel client=%08x "
               "armed_scene=%s remaining=%u reason=%s\n",
               session->clientId,
               session->shopReturnLoadingClearScene[0]
                   ? session->shopReturnLoadingClearScene
                   : "-",
               (u32)session->shopReturnLoadingClearRemaining,
               reason ? reason : "-");
        session->shopReturnLoadingClearPending = false;
        session->shopReturnLoadingClearRemaining = 0;
        session->shopReturnLoadingClearTick = 0;
        session->shopReturnLoadingClearArmTick = 0;
        session->shopReturnLoadingClearScene[0] = 0;
        session->shopReturnLoadingClearPostCatalog = false;
        session->shopReturnLoadingClearLite = false;
    }
    /*
     * Clear may already be finished while 26/0 is still pending (armed on
     * remaining=0).  moveinfo-live / shop-open / scene-pending must still
     * drop that busy ack so it cannot leak into the next flow.
     */
    vm_mock_service_session_cancel_shop_return_busy_ack(session, reason);
}

static void vm_mock_service_session_arm_shop_return_loading_clear(
    vm_mock_service_client_session *session,
    const char *scene)
{
    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene))
        return;
    vm_mock_service_session_cancel_shop_return_busy_ack(session, "re-arm-clear");
    session->shopReturnLoadingClearPending = true;
    session->shopReturnLoadingClearRemaining = 5;
    session->shopReturnLoadingClearPostCatalog = false;
    session->shopReturnLoadingClearLite = false;
    session->shopReturnLoadingClearTick = g_schedulerTick;
    session->shopReturnLoadingClearArmTick = g_schedulerTick;
    snprintf(session->shopReturnLoadingClearScene,
             sizeof(session->shopReturnLoadingClearScene),
             "%s",
             scene);
    printf("[info][mock-service] shop_return_loading_clear_arm client=%08x scene=%s "
           "remaining=5 post_catalog=0 "
           "evidence=poll-30/2-after-shop-return-27/11-no-same-packet\n",
           session->clientId,
           scene);
}

static void vm_mock_service_session_arm_shop_return_loading_clear_after_catalog(
    vm_mock_service_client_session *session,
    const char *scene,
    bool priorSeeded)
{
    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene))
        return;
    vm_mock_service_session_cancel_shop_return_busy_ack(session, "re-arm-clear");
    session->shopReturnLoadingClearPending = true;
    session->shopReturnLoadingClearPostCatalog = true;
    session->shopReturnLoadingClearLite = priorSeeded;
    /*
     * Cold catalog (no prior type-21 this visit): heavy window — 蓬莱_02
     * npcnum=3 outlived remaining=5 + gap=8 (2026-07-28).
     * Warm prior-seeded: short window like map-stone wait_wt6 rearm; long
     * standing 30/2 drips after map-stone→shop left DF stuck (蓬莱_01).
     */
    session->shopReturnLoadingClearRemaining = priorSeeded ? 3u : 8u;
    /*
     * Catalog response already consumed this poll/25/5 slot.  Backdate so the
     * next keep-alive can drip the first post-catalog 30/2 without waiting a
     * full minAge after arm (蓬莱站立出店：catalog 后仅见 25/5).
     */
    {
        u32 minAge = priorSeeded ? 8u : 12u;

        session->shopReturnLoadingClearTick =
            (g_schedulerTick > minAge) ? (g_schedulerTick - minAge)
                                       : 0u;
    }
    session->shopReturnLoadingClearArmTick = g_schedulerTick;
    snprintf(session->shopReturnLoadingClearScene,
             sizeof(session->shopReturnLoadingClearScene),
             "%s",
             scene);
    printf("[info][mock-service] shop_return_loading_clear_arm client=%08x scene=%s "
           "remaining=%u post_catalog=1 lite=%u prior_seeded=%u "
           "evidence=poll-30/2-after-deferred-27/11\n",
           session->clientId,
           scene,
           (u32)session->shopReturnLoadingClearRemaining,
           priorSeeded ? 1u : 0u,
           priorSeeded ? 1u : 0u);
}

static void vm_mock_service_finish_shop_return_rehydrate(
    vm_mock_service_client_session *session,
    const char *scene,
    bool awaitsBattleRevivalConfirm)
{
    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene))
        return;


    /*
     * Shop open arms g_netMockShop17ListPending for mall page sync.  Leaving it
     * set after mmShop closes hijacks the next backpack 17/1 with shop rows so
     * the rebuilt item manager looks empty until relogin
     * (docs/re/2026-06-26-npc-shop-purchase.md).
     */
    g_netMockShop17ListPending = 0;
    /*
     * mmGame shell is rebuilt after mmShop.  If 30/21 was already marked seeded
     * from before the mall visit, the next 5/10+7/7 type-1 suppresses grid
     * restore and the main bag stays empty of mall purchases (especially
     * 802/803 which cannot rely on mmShop local insert alone).
     */
    g_netMockBackpackGridSeededRoleId = 0;
    
    /*
     * Drop stale deferred kind-2 arms.  Empty-NPC maps re-arm after loading
     * clear finishes (or moveinfo-live cancels the clear early).
     */
    vm_mock_service_session_cancel_shop_return_kind2_reenter(
        session, "shop-return-rehydrate");
    session->mmShopShellActive = false;
    session->shopActorQueryAckCount = 0;
    session->mmShopShellArmedTick = 0;
    session->mmShopOpenMarkSource = 0;
    /*
     * One-shot shop-return provenance.  Empty type-21 maps deliberately leave
     * shopSceneNpcReseedPending set through lifecycle seed (npcnum==0), so
     * THIS must clear it.  Leaving it armed made every post-kind2 type27 /
     * task-subset look like another mmShop exit → rehydrate → kind2 →
     * current-scene reload loop (01桃花岛_01 2026-07-28, >2 loads).
     */
    if (session->shopSceneNpcReseedPending)
    {
        u8 npcCount = vm_net_mock_scene_room_npc_seed_count(scene);

        /*
         * post-test23/24: mmShop clears type-21.  If follow-up never forced a
         * nonempty 27/11 (warm=0), arm poll catalog before consuming reseed —
         * otherwise NPCs stay missing after a successful exit.
         */
        if (!session->shopReturnWarmNpcDelivered && npcCount > 0u &&
            !session->shopReturnNpcCatalogPending &&
            !session->shopReturnNpcInlineOnShell)
        {
            bool dreamScene = scene[0] == '2' && scene[1] == '9';

            /*
             * Warm outdoor / dream 29*: early-shell nonempty 27/11 in the
             * shell 30/2 window (type27-inject; no same-packet 30/2).  One
             * DoLoading only — kind-2 same-scene EnterScene is outdoor's
             * empty-map path, not NPC maps (user 2026-08-05: match 野外).
             *
             * Prior dream defer_after_kind2 skipped early 27/11 then armed
             * dream-battle-mapctrl 30/1 → second bar + WT18/7 stuck.
             */
            if (dreamScene || session->shopReturnNpcResourcesWarm)
            {
                session->shopReturnNpcInlineOnShell = true;
                printf("[info][mock-service] shop_return_npc_inline_on_shell "
                       "client=%08x scene=%s npcnum=%u dream=%u warm=%u "
                       "evidence=early-shell-27/11-before-30/2\n",
                       session->clientId, scene, (u32)npcCount,
                       dreamScene ? 1u : 0u,
                       session->shopReturnNpcResourcesWarm ? 1u : 0u);
            }
            else
            {
                session->shopReturnNpcCatalogPending = true;
                snprintf(session->shopReturnNpcCatalogScene,
                         sizeof(session->shopReturnNpcCatalogScene), "%s",
                         scene);
                session->shopReturnNpcCatalogReadyTick = g_schedulerTick;
                printf("[info][mock-service] shop_return_npc_catalog_defer "
                       "client=%08x scene=%s npcnum=%u via=rehydrate-missed-warm "
                       "evidence=post-test24-mmShop-clears-type21\n",
                       session->clientId, scene, (u32)npcCount);
            }
        }
        printf("[info][mock-service] scene_npc_reseed_consume client=%08x "
               "scene=%s reason=shop-return-rehydrate warm=%u catalog_pending=%u "
               "inline_shell=%u evidence=one-shot-mmShop-exit\n",
               session->clientId,
               session->shopSceneNpcReseedScene[0]
                   ? session->shopSceneNpcReseedScene
                   : scene,
               session->shopReturnWarmNpcDelivered ? 1u : 0u,
               session->shopReturnNpcCatalogPending ? 1u : 0u,
               session->shopReturnNpcInlineOnShell ? 1u : 0u);
        session->shopSceneNpcReseedPending = false;
        session->shopSceneNpcReseedScene[0] = 0;
    }
    /*
     * Catalog is on type27-inject or follow-up without same-packet 30/2.
     * Poll 30/2 clears DF (26/0 alone cannot — 2026-07-27 / 2026-08-01).
     */
    if (!awaitsBattleRevivalConfirm)
        vm_mock_service_session_arm_shop_return_loading_clear(session, scene);
    else
        vm_mock_service_session_cancel_shop_return_loading_clear(
            session, "battle-confirm");
    /*
     * mmShop→mmGame is not a packet-visible scene transfer.  Clear any leftover
     * map-stone / teleport lifecycle (not only last_scene_change_target_valid)
     * once this session is already scene-ready; otherwise post-catalog 30/2
     * is held as liveTeleport until the clear expires and loading sticks.
     */
    vm_mock_service_clear_stale_map_transfer_for_shop(session, scene,
                                                     "shop-return-rehydrate");
    vm_mock_service_session_invalidate_nearby_observer_cursors(session,
                                                                "shop-return");
    (void)vm_mock_service_mark_active_session_scene_ready_from_role(
        scene, "shop-return");
    /*
     * Queue peer 5/5 BEFORE HSP.  Scene-sync delivers one social notice per
     * poll; HSP-first let loading-clear/kind2 starve JOIN forever
     * (docs/re/2026-08-01-shop-return-team-peer-join-starve.md).
     */
    vm_mock_service_team_enqueue_shop_return_peer_joins(session);
    vm_mock_service_team_enqueue_hsp_resync_for_client(session);

    printf("[info][mock-service] shop_return_rehydrate client=%08x scene=%s "
           "battle_confirm=%u shop17_pending=0 loading_clear=%s warm_npc=%u "
           "evidence=mmShop->mmGame-shell+nearby+hsp\n",
           session->clientId,
           scene,
           awaitsBattleRevivalConfirm ? 1u : 0u,
           session->shopReturnLoadingClearPending ? "poll-30/2" : "0",
           session->shopReturnWarmNpcDelivered ? 1u : 0u);
}

static bool vm_mock_service_session_presence_is_recent(const vm_mock_service_client_session *session)
{
    u32 age = 0;

    if (session == NULL || !session->onlinePresenceValid)
        return false;
    if (g_schedulerTick < session->onlineTick)
        return true;
    age = g_schedulerTick - session->onlineTick;
    return age <= VM_MOCK_SERVICE_ONLINE_PRESENCE_MAX_AGE_TICKS;
}

static void vm_mock_service_expire_stale_online_sessions(void)
{
    static u32 s_lastExpireMs = 0;
    u32 nowMs = scheduler_get_tick_ms();
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;

    /*
     * Every scene-sync poll used to walk all sessions (and possibly MySQL
     * flush).  With N clients × ~10 Hz that dominated protocol-lock hold time
     * even when nobody was stale.  Cap to once per 2s globally.
     */
    if (s_lastExpireMs != 0 &&
        nowMs >= s_lastExpireMs &&
        (nowMs - s_lastExpireMs) < 2000u)
    {
        return;
    }
    s_lastExpireMs = nowMs;

    while (session != NULL)
    {
        vm_mock_service_client_session *next = session->next;
        if (session->roleOnline &&
            !vm_mock_service_session_presence_is_recent(session))
        {
            vm_mock_service_account_flush_for_session(session, "heartbeat-timeout");
            vm_mock_service_session_mark_offline(session, "heartbeat-timeout");
            vm_mock_service_session_unbind_account(session);
        }
        session = next;
    }
}

static bool vm_mock_service_session_scene_is_visible(const vm_mock_service_client_session *session,
                                                     const char *scene)
{
    if (session == NULL ||
        !session->roleOnline ||
        !vm_mock_service_session_presence_is_recent(session) ||
        !session->sceneVisibleReady ||
        session->sceneVisiblePending ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_name_is_safe(session->sceneVisibleScene))
    {
        return false;
    }
    return vm_net_mock_scene_names_equal_loose(session->sceneVisibleScene, scene);
}

static bool vm_mock_service_team_member_has_nonzero_battle_hp(
    vm_mock_service_client_session *member)
{
    vm_net_mock_role_state *role = NULL;
    u32 seedHp = 0;
    u32 seedHpMax = 0;
    u32 seedMp = 0;
    u32 seedMpMax = 0;

    if (member == NULL)
        return false;
    /*
     * Mirror team_begin_battle seeding: durable role wins when online presence
     * was wiped to 0 after a prior revival/escape exit; a truly dead seat has
     * both online and durable HP at 0 and must not be pulled into Battle.cbm.
     */
    role = vm_mock_service_trade_role_for_session(member, NULL);
    if (role != NULL)
        vm_net_mock_role_default_vitals(role, &seedHp, &seedHpMax, &seedMp,
                                        &seedMpMax);
    if (member->onlineHp != 0)
        return true;
    return seedHp != 0;
}

/*
 * Same-scene living seats must be able to enter Battle.cbm before the leader's
 * 4/1 / hangup can arm team battle.  Block only while the mall shell owns the
 * screen or the seat is not scene-ready.
 *
 * Do NOT block on shop-return housekeeping (loading-clear / busy_ack /
 * deferred 27/11 / kind2 30/1 / map-stone clear drips): those run after
 * mmShopShellActive is cleared and sceneVisibleReady is set.  Blocking on
 * them left captains stuck with reject-party-not-ready after the teammate
 * was already walking (empty-NPC 桃花岛 kind2-pending; long post-catalog
 * 30/2).  Evidence: docs/re/2026-08-01-party-not-ready-after-shop-exit.md.
 */
static const char *vm_mock_service_session_team_battle_not_ready_reason(
    const vm_mock_service_client_session *session)
{
    const vm_mock_service_account_state *account = NULL;

    if (session == NULL)
        return "missing-session";
    if (session->mmShopShellActive)
        return "mmShop-shell";
    /*
     * shopSceneNpcReseedPending is armed at shop-open and cleared in
     * rehydrate.  While still set with no live scene ready, the seat has not
     * finished leaving the mall path.
     */
    if (session->shopSceneNpcReseedPending &&
        (!session->sceneVisibleReady || session->sceneVisiblePending))
    {
        return "shop-npc-reseed-pending";
    }
    if (!session->sceneVisibleReady || session->sceneVisiblePending)
        return "scene-not-ready";
    /*
     * Prior-fight settle panel still owns Battle.cbm until delayed 4/8.
     * Hangup/challenge must not arm the next shared 1/4/5 while any same-scene
     * seat still owes tear-down — otherwise the seat misses start and the
     * round barrier waits on an un-entered actor
     * (docs/re/2026-08-05-team-hangup-peer-settle-stall.md).
     *
     * Do NOT key off AwaitingSettlement alone: hangup skip-4/8 keeps that bit
     * set on the leader until the next start clears Battle.cbm.
     */
    if (session->accountId[0] != 0)
        account = vm_mock_service_account_find(session->accountId);
    if (account != NULL &&
        (account->mockBattleSettlementExitPending != 0 ||
         account->mockBattlePostExitSettlePending != 0))
    {
        /*
         * A seat that stopped polling (frozen / disconnecting) still carries
         * settle-pending until heartbeat-timeout.  Do not block team hangup
         * forever on a non-recent presence — that seat will be removed or
         * will not join the next start either way.
         */
        if (!vm_mock_service_session_presence_is_recent(session))
            return NULL;
        return "settle-pending";
    }
    return NULL;
}

static bool vm_mock_service_team_member_same_scene_presence(
    const vm_mock_service_client_session *member,
    const char *scene)
{
    if (vm_mock_service_session_scene_is_visible(member, scene))
        return true;
    /*
     * Shop / shop-return seats often keep onlineScene on the party map while
     * sceneVisible* is cleared or pending.  Those seats still owe a team
     * battle operate if the leader starts — treat them as same-scene.
     */
    if (member == NULL ||
        !member->roleOnline ||
        !vm_mock_service_session_presence_is_recent(member) ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_name_is_safe(member->onlineScene))
    {
        return false;
    }
    return vm_net_mock_scene_names_equal_loose(member->onlineScene, scene);
}

/* Visible/online scene used to decide whether a seat still shares the leader's
 * map.  Pending transitions return NULL so mid-teleport seats are not kicked. */
static const char *vm_mock_service_session_team_presence_scene(
    const vm_mock_service_client_session *session)
{
    if (session == NULL)
        return NULL;
    if (session->sceneVisibleReady && !session->sceneVisiblePending &&
        vm_net_mock_scene_name_is_safe(session->sceneVisibleScene))
    {
        return session->sceneVisibleScene;
    }
    if (vm_net_mock_scene_name_is_safe(session->onlineScene))
        return session->onlineScene;
    return NULL;
}

/*
 * When any seat finishes entering a map, remove non-leader members who are no
 * longer on the leader's scene — unless they are mid team-follow (pending or
 * sceneVisiblePending).  Leader-led transfers keep the roster and pull mates
 * via pendingTeamFollowSceneEnter instead of dissolving the party.
 */
static void vm_mock_service_team_auto_leave_off_leader_scene(
    vm_mock_service_client_session *session,
    const char *reason)
{
    vm_mock_service_team *team = NULL;
    vm_mock_service_client_session *leader = NULL;
    const char *leaderScene = NULL;
    u32 removeClientIds[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u8 removeCount = 0;

    if (session == NULL || session->clientId == 0)
        return;
    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team == NULL || !team->active || team->memberCount < 2)
        return;
    leader = vm_mock_service_find_client_session(team->leaderClientId);
    if (leader == NULL)
        return;

    /*
     * Leader just landed on a new map: pull mates instead of kicking them.
     * Historical map-local dissolve treated this as scene-mismatch and sent
     * 5/7 (client tip「成员离开 <id>」) — that is exactly what follow replaces.
     */
    if (vm_mock_service_team_is_leader(team, session->clientId))
    {
        vm_net_mock_scene_change_target followTarget;

        memset(&followTarget, 0, sizeof(followTarget));
        snprintf(followTarget.scene, sizeof(followTarget.scene), "%s",
                 session->sceneVisibleScene);
        followTarget.x = session->sceneVisibleX;
        followTarget.y = session->sceneVisibleY;
        followTarget.mapType = 2;
        followTarget.hasSceEntry = true;
        vm_mock_service_team_queue_follow_leader_scene(
            session, &followTarget, reason ? reason : "leader-scene-ready");
        return;
    }

    leaderScene = vm_mock_service_session_team_presence_scene(leader);
    if (leaderScene == NULL)
        return;

    for (u8 i = 0; i < team->memberCount && removeCount < VM_MOCK_SERVICE_TEAM_MEMBER_MAX; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->memberClientIds[i]);
        const char *memberScene = NULL;

        if (member == NULL || member->clientId == team->leaderClientId)
            continue;
        /* In-transit follow / any pending enter: do not kick yet. */
        if (member->pendingTeamFollowSceneEnter || member->sceneVisiblePending)
            continue;
        memberScene = vm_mock_service_session_team_presence_scene(member);
        if (memberScene == NULL)
            continue;
        if (vm_mock_service_team_member_same_scene_presence(member, leaderScene))
            continue;
        removeClientIds[removeCount++] = member->clientId;
    }

    for (u8 i = 0; i < removeCount; ++i)
    {
        vm_mock_service_client_session *leaver =
            vm_mock_service_find_client_session(removeClientIds[i]);
        const char *memberScene = NULL;
        u32 leaderClientId = 0;

        if (leaver == NULL)
            continue;
        memberScene = vm_mock_service_session_team_presence_scene(leaver);
        leaderClientId = team->leaderClientId;
        if (!vm_mock_service_team_remove_member(leaver, reason ? reason : "scene-mismatch"))
            continue;
        /*
         * Voluntary leave returns 5/7 on the request; this path has no request
         * response, so the leaver must also receive TEAM_LEAVE for HUD clear.
         */
        (void)vm_mock_service_session_enqueue_social_notice(
            leaver, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_LEAVE, 0,
            leaver, NULL, leaver->accountId);
        printf("[info][mock-service] team_auto_leave client=%08x/%u "
               "leader=%08x leader_scene=%s member_scene=%s reason=%s "
               "evidence=cross-scene-party-dissolve\n",
               leaver->clientId,
               leaver->onlineRoleId,
               leaderClientId,
               leaderScene,
               memberScene ? memberScene : "-",
               reason ? reason : "scene-mismatch");
    }
}

/*
 * Arm a poll/dispatch 30/1 for every non-leader seat that is not already on
 * the leader's destination.  Marks them scene-pending so auto-leave skips
 * them until they land.
 */
static void vm_mock_service_team_queue_follow_leader_scene(
    vm_mock_service_client_session *leader,
    const vm_net_mock_scene_change_target *target,
    const char *reason)
{
    vm_mock_service_team *team = NULL;
    vm_net_mock_scene_change_target followTarget;
    u16 x = 0;
    u16 y = 0;
    /* GBK: 跟随队长切图 */
    static const char followTipGbk[] =
        "\xb8\xfa\xcb\xe6\xb6\xd3\xb3\xa4\xc7\xd0\xcd\xbc";

    if (leader == NULL || target == NULL ||
        !vm_net_mock_scene_name_is_safe(target->scene))
    {
        return;
    }
    team = vm_mock_service_team_find_for_client(leader->clientId);
    if (team == NULL || !team->active || team->memberCount < 2)
        return;
    if (!vm_mock_service_team_is_leader(team, leader->clientId))
        return;

    followTarget = *target;
    x = target->x;
    y = target->y;
    /*
     * Portal 2/3 often marks pending before entry coords resolve (x=y=0).
     * Still arm follow: fall back to SCE spawn, then leader footfall, then
     * a safe default — otherwise scene-ready auto-leave kicks mates (5/7).
     */
    if (x == 0 || y == 0)
    {
        u16 spawnX = 0;
        u16 spawnY = 0;
        if (vm_net_mock_get_scene_reasonable_spawn_from_sce(followTarget.scene,
                                                             &spawnX,
                                                             &spawnY,
                                                             NULL) &&
            spawnX != 0 && spawnY != 0)
        {
            x = spawnX;
            y = spawnY;
        }
        else if (leader->sceneVisibleX != 0 && leader->sceneVisibleY != 0 &&
                 vm_net_mock_scene_names_equal_loose(leader->sceneVisibleScene,
                                                      followTarget.scene))
        {
            x = leader->sceneVisibleX;
            y = leader->sceneVisibleY;
        }
        else
        {
            x = 120;
            y = 120;
        }
        printf("[info][mock-service] team_follow_resolve_pos leader=%08x "
               "scene=%s pos=(%u,%u) reason=%s evidence=zero-target-fallback\n",
               leader->clientId,
               followTarget.scene,
               x,
               y,
               reason ? reason : "-");
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(followTarget.scene, &x, &y);
    if (x == 0 || y == 0)
    {
        printf("[warn][mock-service] team_follow_skip leader=%08x scene=%s "
               "reason=unresolved-pos via=%s\n",
               leader->clientId,
               followTarget.scene,
               reason ? reason : "-");
        return;
    }
    followTarget.x = x;
    followTarget.y = y;
    followTarget.mapType = followTarget.mapType ? followTarget.mapType : 2;
    followTarget.hasSceEntry = true;

    for (u8 i = 0; i < team->memberCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->memberClientIds[i]);

        if (member == NULL || member->clientId == team->leaderClientId)
            continue;
        if (vm_mock_service_team_member_same_scene_presence(member,
                                                            followTarget.scene) &&
            !member->sceneVisiblePending &&
            !member->pendingTeamFollowSceneEnter)
        {
            continue;
        }

        /*
         * Refresh an already-armed follow to the latest leader landing (coords
         * may have been provisional on the first pending mark).
         */
        vm_mock_service_session_mark_scene_pending(member, &followTarget,
                                                   "team-follow");
        member->pendingTeamFollowSceneEnter = true;
        member->pendingTeamFollowSceneEnterEarliestTick = g_schedulerTick;
        snprintf(member->pendingTeamFollowSceneEnterScene,
                 sizeof(member->pendingTeamFollowSceneEnterScene),
                 "%s",
                 followTarget.scene);
        member->pendingTeamFollowSceneEnterX = followTarget.x;
        member->pendingTeamFollowSceneEnterY = followTarget.y;
        (void)vm_mock_service_session_enqueue_system_message(member, followTipGbk);
        printf("[info][mock-service] team_follow_queue leader=%08x member=%08x/%u "
               "scene=%s pos=(%u,%u) reason=%s evidence=leader-transfer-keep-party\n",
               leader->clientId,
               member->clientId,
               member->onlineRoleId,
               followTarget.scene,
               followTarget.x,
               followTarget.y,
               reason ? reason : "-");
    }
}

static bool vm_mock_service_team_party_not_battle_ready(
    const vm_mock_service_team *team,
    const char *scene,
    u32 *blockerClientIdOut,
    const char **reasonOut)
{
    if (blockerClientIdOut != NULL)
        *blockerClientIdOut = 0;
    if (reasonOut != NULL)
        *reasonOut = NULL;
    if (team == NULL || !team->active || team->memberCount < 2 ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return false;
    }
    for (u8 i = 0; i < team->memberCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->memberClientIds[i]);
        const char *reason = NULL;

        if (!vm_mock_service_team_member_same_scene_presence(member, scene))
            continue;
        if (!vm_mock_service_team_member_has_nonzero_battle_hp(member))
            continue;
        reason = vm_mock_service_session_team_battle_not_ready_reason(member);
        if (reason == NULL)
            continue;
        if (blockerClientIdOut != NULL)
            *blockerClientIdOut = member->clientId;
        if (reasonOut != NULL)
            *reasonOut = reason;
        return true;
    }
    return false;
}

static bool vm_mock_service_active_session_team_party_not_battle_ready(
    u32 *blockerClientIdOut,
    const char **reasonOut)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_mock_service_team *team = NULL;
    const char *scene = NULL;

    if (blockerClientIdOut != NULL)
        *blockerClientIdOut = 0;
    if (reasonOut != NULL)
        *reasonOut = NULL;
    if (session == NULL || session->clientId == 0)
        return false;
    team = vm_mock_service_team_find_for_client(session->clientId);
    if (team == NULL || !team->active || team->memberCount < 2)
        return false;
    if (!vm_mock_service_team_is_leader(team, session->clientId))
        return false;
    if (session->sceneVisibleReady && !session->sceneVisiblePending &&
        vm_net_mock_scene_name_is_safe(session->sceneVisibleScene))
    {
        scene = session->sceneVisibleScene;
    }
    else if (vm_net_mock_scene_name_is_safe(session->onlineScene))
    {
        scene = session->onlineScene;
    }
    else
    {
        scene = vm_net_mock_current_scene_name();
    }
    return vm_mock_service_team_party_not_battle_ready(team,
                                                       scene,
                                                       blockerClientIdOut,
                                                       reasonOut);
}

static u8 vm_mock_service_team_collect_battle_members(
    const vm_mock_service_team *team,
    const char *scene,
    u32 memberClientIds[VM_MOCK_SERVICE_TEAM_MEMBER_MAX])
{
    u8 count = 0;

    if (memberClientIds != NULL)
        memset(memberClientIds, 0, sizeof(u32) * VM_MOCK_SERVICE_TEAM_MEMBER_MAX);
    if (team == NULL || !team->active || memberClientIds == NULL ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return 0;
    }
    for (u8 i = 0; i < team->memberCount && count < VM_MOCK_SERVICE_TEAM_MEMBER_MAX; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->memberClientIds[i]);
        if (!vm_mock_service_session_scene_is_visible(member, scene))
            continue;
        if (!vm_mock_service_team_member_has_nonzero_battle_hp(member))
        {
            printf("[info][mock-service] team_battle_skip_dead_member "
                   "leader=%08x member=%08x/%u scene=%s online_hp=%u "
                   "reason=map-dead-not-pulled-into-battle\n",
                   team->leaderClientId,
                   member ? member->clientId : 0,
                   member ? member->onlineRoleId : 0,
                   scene,
                   member ? member->onlineHp : 0);
            continue;
        }
        memberClientIds[count++] = member->clientId;
    }
    return count;
}

static bool vm_mock_service_team_battle_contains_client(const vm_mock_service_team *team,
                                                        u32 clientId)
{
    if (team == NULL || !team->battleActive || clientId == 0)
        return false;
    for (u8 i = 0; i < team->battleMemberCount; ++i)
    {
        if (team->battleMemberClientIds[i] == clientId)
            return true;
    }
    return false;
}

static int vm_mock_service_team_battle_member_index(const vm_mock_service_team *team,
                                                    u32 clientId)
{
    if (team == NULL || !team->battleActive || clientId == 0)
        return -1;
    for (u8 i = 0; i < team->battleMemberCount; ++i)
    {
        if (team->battleMemberClientIds[i] == clientId)
            return i;
    }
    return -1;
}

/* One paid battle settlement (EXP/gold/drops) per this much server wall-clock
 * time.  Battle enter packets are never blocked: hangup already sets client
 * state=3 and any fail/defer ACK leaves a stuck progress bar.  Override with
 * CBE_BATTLE_REWARD_MIN_INTERVAL_MS (legacy CBE_BATTLE_START_MIN_INTERVAL_MS
 * still accepted); set CBE_BATTLE_REWARD_RATE_LIMIT=0 to disable.
 * Default 8000 keeps reward pacing independent of hangup map-side re-entry
 * (CBE_HANGUP_LOOP_INTERVAL_MS, currently default 2000).  This gate only
 * suppresses EXP/gold/drops; it does not block battle enter or skip counters. */
#define VM_MOCK_SERVICE_BATTLE_REWARD_INTERVAL_MS_DEFAULT 8000u

static u32 vm_mock_service_battle_reward_interval_ms(void)
{
    u32 intervalMs = vm_net_mock_env_u32("CBE_BATTLE_REWARD_MIN_INTERVAL_MS", 0);
    if (intervalMs == 0)
        intervalMs = vm_net_mock_env_u32("CBE_BATTLE_START_MIN_INTERVAL_MS",
                                         VM_MOCK_SERVICE_BATTLE_REWARD_INTERVAL_MS_DEFAULT);
    return intervalMs == 0 ? VM_MOCK_SERVICE_BATTLE_REWARD_INTERVAL_MS_DEFAULT
                           : intervalMs;
}

static bool vm_mock_service_battle_reward_rate_limit_enabled(void)
{
    if (vm_net_mock_env_u8("CBE_BATTLE_REWARD_RATE_LIMIT", 1) == 0)
        return false;
    /* Legacy kill-switch from the start-gate experiments. */
    if (getenv("CBE_BATTLE_START_RATE_LIMIT") != NULL &&
        vm_net_mock_env_u8("CBE_BATTLE_START_RATE_LIMIT", 1) == 0)
    {
        return false;
    }
    return true;
}

static u32 vm_mock_service_session_battle_reward_remaining_ms(
    const vm_mock_service_client_session *session, u32 nowMs)
{
    u32 intervalMs;
    u32 elapsedMs;

    if (session == NULL || !session->battleRewardRateActive ||
        session->battleRewardLastMs == 0)
    {
        return 0;
    }
    intervalMs = vm_mock_service_battle_reward_interval_ms();
    elapsedMs = nowMs - session->battleRewardLastMs;
    if (elapsedMs >= intervalMs)
        return 0;
    return intervalMs - elapsedMs;
}

/* True when this character may receive EXP/gold/drops for a new settlement. */
static bool vm_mock_service_battle_reward_allowed(u32 *remainingMsOut)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 nowMs = scheduler_get_tick_ms();
    u32 remainingMs = 0;

    if (remainingMsOut)
        *remainingMsOut = 0;
    if (!vm_mock_service_battle_reward_rate_limit_enabled() || session == NULL)
        return true;
    remainingMs = vm_mock_service_session_battle_reward_remaining_ms(session, nowMs);
    if (remainingMsOut)
        *remainingMsOut = remainingMs;
    if (remainingMs == 0)
        return true;
    ++session->battleRewardRateViolationCount;
    if (session->battleRewardRateViolationCount == 1 ||
        nowMs - session->battleRewardRateLastViolationMs >= 1000)
    {
        printf("[warn][network] battle_reward_rate_limited client=%08x role=%u "
               "remaining_ms=%u interval_ms=%u last_reward_ms=%u violations=%u\n",
               session->clientId,
               session->onlineRoleId,
               remainingMs,
               vm_mock_service_battle_reward_interval_ms(),
               session->battleRewardLastMs,
               session->battleRewardRateViolationCount);
        session->battleRewardRateLastViolationMs = nowMs;
    }
    return false;
}

static void vm_mock_service_battle_reward_mark(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    u32 nowMs = scheduler_get_tick_ms();

    if (session == NULL)
        return;
    session->battleRewardRateActive = true;
    session->battleRewardLastMs = nowMs;
    printf("[info][network] battle_reward_rate_mark client=%08x role=%u "
           "interval_ms=%u\n",
           session->clientId,
           session->onlineRoleId,
           vm_mock_service_battle_reward_interval_ms());
}

/* Freeze the same-scene party at the instant the leader's 4/1 request is
 * accepted.  Each passive client later receives its own observer-specific
 * 1/4/5 packet from the ordinary scene poll path. */
static u8 vm_mock_service_team_begin_battle(vm_mock_service_team *team,
                                            vm_mock_service_client_session *leader,
                                            const char *scene,
                                            u32 enemyId,
                                            u32 sceneMonsterIndex,
                                            u32 sceneMonsterX,
                                            u32 sceneMonsterY,
                                            u8 monsterCount,
                                            u8 side,
                                            u8 *queuedOut)
{
    u32 participantIds[VM_MOCK_SERVICE_TEAM_MEMBER_MAX] = {0};
    u8 participantCount = 0;
    u8 queued = 0;

    if (queuedOut)
        *queuedOut = 0;
    if (team == NULL || leader == NULL ||
        !vm_mock_service_team_is_leader(team, leader->clientId))
    {
        return 0;
    }
    {
        u32 blockerClientId = 0;
        const char *notReadyReason = NULL;

        if (vm_mock_service_team_party_not_battle_ready(team,
                                                       scene,
                                                       &blockerClientId,
                                                       &notReadyReason))
        {
            printf("[info][mock-service] team_battle_begin_blocked "
                   "leader=%08x blocker=%08x scene=%s reason=%s "
                   "evidence=party-not-ready-no-team-battle\n",
                   leader->clientId,
                   blockerClientId,
                   scene ? scene : "-",
                   notReadyReason ? notReadyReason : "-");
            return 0;
        }
    }
    participantCount = vm_mock_service_team_collect_battle_members(team, scene, participantIds);
    if (participantCount < 2 || participantIds[0] != leader->clientId)
        return participantCount;

    for (u8 i = 0; i < team->memberCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(team->memberClientIds[i]);
        if (member != NULL)
            member->pendingTeamBattleSerial = 0;
    }
    ++team->battleSerial;
    if (team->battleSerial == 0)
        team->battleSerial = 1;
    team->battleActive = true;
    team->battleLeaderClientId = leader->clientId;
    team->battleEnemyId = enemyId;
    team->battleSceneMonsterIndex = sceneMonsterIndex;
    team->battleSceneMonsterX = sceneMonsterX;
    team->battleSceneMonsterY = sceneMonsterY;
    team->battleMonsterCount = monsterCount;
    team->battleSide = side;
    team->battleMemberCount = participantCount;
    memcpy(team->battleMemberClientIds, participantIds,
           sizeof(team->battleMemberClientIds));
    snprintf(team->battleScene, sizeof(team->battleScene), "%s", scene);
    team->battleFinished = false;
    team->battleTurnCounter = 0;
    memcpy(team->battleEnemyHpSlots, g_mockBattleEnemyHpSlots,
           sizeof(team->battleEnemyHpSlots));
    memcpy(team->battleEnemyHpMaxSlots, g_mockBattleEnemyHpMaxSlots,
           sizeof(team->battleEnemyHpMaxSlots));
    team->battleEnemyHpCurrent = g_mockBattleEnemyHpCurrent;
    team->battleEnemyHpMax = g_mockBattleEnemyHpMax;
    /*
     * Defense: a finished fight can leave globals/team at enemyhp=0.  Never
     * open a new shared encounter already defeated (prefer arm + seat_can_act
     * both refuse → stuck until round timeout).
     */
    if (monsterCount > 0 && team->battleEnemyHpCurrent == 0)
    {
        u8 savedEnemyCount = g_mockBattleEnemyCountCurrent;

        g_mockBattleEnemyCountCurrent = monsterCount;
        vm_net_mock_battle_reset_enemy_hp_from_stats(enemyId);
        if (savedEnemyCount != 0)
            g_mockBattleEnemyCountCurrent = savedEnemyCount;
        memcpy(team->battleEnemyHpSlots, g_mockBattleEnemyHpSlots,
               sizeof(team->battleEnemyHpSlots));
        memcpy(team->battleEnemyHpMaxSlots, g_mockBattleEnemyHpMaxSlots,
               sizeof(team->battleEnemyHpMaxSlots));
        team->battleEnemyHpCurrent = g_mockBattleEnemyHpCurrent;
        team->battleEnemyHpMax = g_mockBattleEnemyHpMax;
        printf("[warn][mock-service] team_battle_enemy_hp_reseed serial=%u "
               "enemy=%u enemies=%u enemyhp=%u/%u "
               "evidence=stale-zero-hp-before-begin\n",
               team->battleSerial,
               enemyId,
               monsterCount,
               team->battleEnemyHpCurrent,
               team->battleEnemyHpMax);
    }
    team->battleRoundActedMask = 0;
    team->battleMemberLeftMask = 0;
    team->battleRoundActDeadlineTick = 0;
    team->battleRoundSerial = 1;
    team->battleRoundTerminalPending = false;
    team->battleRoundTerminalActorIndex = 0xff;
    team->battleRoundActionSerial = 0;
    memset(team->battleRoundActions, 0, sizeof(team->battleRoundActions));
    team->battleActionSerial = 0;
    memset(team->battleEvents, 0, sizeof(team->battleEvents));
    memset(team->battleMemberHp, 0, sizeof(team->battleMemberHp));
    memset(team->battleMemberHpMax, 0, sizeof(team->battleMemberHpMax));
    memset(team->battleMemberMp, 0, sizeof(team->battleMemberMp));
    memset(team->battleMemberMpMax, 0, sizeof(team->battleMemberMpMax));
    /* Timed skill effects are scoped to this battle instance.  A new battle
     * must never inherit a previous encounter's modifier rows. */
    memset(team->battleMemberModifiers, 0, sizeof(team->battleMemberModifiers));
    memset(team->battleEnemyAilments, 0, sizeof(team->battleEnemyAilments));
    for (u8 i = 0; i < participantCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(participantIds[i]);
        vm_net_mock_role_state *memberRole = NULL;
        u32 seedHp = 0;
        u32 seedHpMax = 0;
        u32 seedMp = 0;
        u32 seedMpMax = 0;

        if (member == NULL)
            continue;
        memberRole = vm_mock_service_trade_role_for_session(member, NULL);
        if (memberRole != NULL)
            vm_net_mock_role_default_vitals(memberRole, &seedHp, &seedHpMax,
                                            &seedMp, &seedMpMax);
        if (seedHpMax == 0)
            seedHpMax = member->onlineHpMax ? member->onlineHpMax : 1;
        if (seedMpMax == 0)
            seedMpMax = member->onlineMpMax;
        /*
         * Prefer durable role vitals when online presence was wiped to 0 by a
         * prior shared-battle seat that had already revived/fled out.
         */
        if (member->onlineHp != 0)
            seedHp = vm_net_mock_min_u32(member->onlineHp, seedHpMax ? seedHpMax : member->onlineHp);
        if (member->onlineMp != 0 || memberRole == NULL)
            seedMp = vm_net_mock_min_u32(member->onlineMp, seedMpMax ? seedMpMax : member->onlineMp);
        if (member->onlineHpMax != 0)
            seedHpMax = member->onlineHpMax;
        if (member->onlineMpMax != 0)
            seedMpMax = member->onlineMpMax;
        team->battleMemberHpMax[i] = seedHpMax ? seedHpMax : 1;
        team->battleMemberHp[i] = vm_net_mock_min_u32(seedHp, team->battleMemberHpMax[i]);
        team->battleMemberMpMax[i] = seedMpMax;
        team->battleMemberMp[i] = vm_net_mock_min_u32(seedMp, team->battleMemberMpMax[i]
                                                                  ? team->battleMemberMpMax[i]
                                                                  : seedMp);
        member->onlineHp = team->battleMemberHp[i];
        member->onlineHpMax = team->battleMemberHpMax[i];
        member->onlineMp = team->battleMemberMp[i];
        member->onlineMpMax = team->battleMemberMpMax[i];
    }

    for (u8 i = 0; i < participantCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(participantIds[i]);
        if (member != NULL && member->clientId != leader->clientId)
        {
            member->pendingTeamBattleSerial = team->battleSerial;
            ++queued;
        }
    }
    if (queuedOut)
        *queuedOut = queued;
    printf("[info][mock-service] team_battle_queue serial=%u leader=%08x enemy=%u "
           "scene=%s members=%u queued=%u index=%u pos=(%u,%u)\n",
           team->battleSerial,
           leader->clientId,
           enemyId,
           team->battleScene,
           participantCount,
           queued,
           sceneMonsterIndex,
           sceneMonsterX,
           sceneMonsterY);
    return participantCount;
}

static vm_mock_service_account_state *vm_mock_service_open_account_role_db_for_management(const char *accountId,
                                                                                          const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;

    if (messageOut)
        *messageOut = "ok";
    if (accountId == NULL || accountId[0] == 0)
    {
        if (messageOut)
            *messageOut = "account cannot be empty";
        return NULL;
    }
    if (vm_mock_service_account_find_record(accountId) == NULL)
    {
        if (messageOut)
            *messageOut = "account not found";
        return NULL;
    }
    state = vm_mock_service_account_find_or_create(accountId);
    if (state == NULL)
    {
        if (messageOut)
            *messageOut = "account state unavailable";
        return NULL;
    }
    ++state->pinCount;
    g_vm_mock_service_active_client_id = 0;
    /*
     * Admin / account-center must not trust sticky heap roleDb.  Break the
     * sticky pointer and force a relational reload so a cross-account
     * pollution cannot be shown as this account's roles.
     */
    if (g_vm_mock_service_globals_account == state)
        g_vm_mock_service_globals_account = NULL;
    state->roleDbLoaded = false;
    state->roleDbValid = false;
    g_vm_net_mock_role_db_loaded = false;
    vm_mock_service_account_restore(state);
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid)
    {
        if (messageOut)
            *messageOut = "role db unavailable";
        if (state->pinCount > 0)
            --state->pinCount;
        vm_mock_service_account_restore(NULL);
        vm_mock_service_account_release_if_idle(accountId);
        return NULL;
    }
    return state;
}

static void vm_mock_service_close_account_role_db_for_management(vm_mock_service_account_state *state,
                                                                 bool captureState)
{
    char accountId[64];

    accountId[0] = 0;
    if (state != NULL)
        snprintf(accountId, sizeof(accountId), "%s", state->accountId);
    if (captureState && state != NULL)
        vm_mock_service_account_capture(state);
    if (state != NULL && state->pinCount > 0)
        --state->pinCount;
    vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_client_id = 0;
    if (accountId[0] != 0)
        vm_mock_service_account_release_if_idle(accountId);
}

static bool vm_mock_service_migrate_account_role_databases(void)
{
    vm_mock_service_account_id_list accounts;
    bool ok = true;

    /* After mysql-authoritative-v1 is sealed, relational rows are the sole
     * authority.  Walking every account here only re-hydrates roleDb into
     * g_vm_mock_service_accounts; login/admin paths already load on demand. */
    if (vm_mock_service_mysql_authority_is_sealed())
    {
        printf("[info][mock-service] mysql role migration skipped reason=already-sealed\n");
        return true;
    }

    if (!vm_mock_service_account_collect_ids(&accounts))
        return false;
    for (u32 i = 0; i < accounts.count; ++i)
    {
        const char *account_id = accounts.ids[i];
        const char *message = NULL;
        vm_mock_service_account_state *state =
            vm_mock_service_open_account_role_db_for_management(account_id, &message);
        if (state == NULL)
        {
            printf("[error][mock-service] mysql role migration failed account=%s reason=%s\n",
                   account_id[0] ? account_id : "-", message ? message : "-");
            ok = false;
            break;
        }
        vm_mock_service_close_account_role_db_for_management(state, true);
    }
    vm_mock_service_account_id_list_free(&accounts);
    return ok;
}


static void vm_mock_service_account_sync_online_role_admin_fields(
    const char *accountId, const vm_net_mock_role_state *role)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;

    if (accountId == NULL || accountId[0] == 0 || role == NULL || role->roleId == 0)
        return;
    while (session != NULL)
    {
        if (session->accountId[0] != 0 &&
            vm_mock_service_account_id_equal(session->accountId, accountId) &&
            session->onlineRoleId == role->roleId)
        {
            snprintf(session->onlineRoleName, sizeof(session->onlineRoleName), "%s",
                     role->name[0] ? role->name : vm_net_mock_default_role_name());
            session->onlineLevel = (u16)(role->level ? role->level : 1);
            session->onlineHp = role->hp;
            session->onlineHpMax = role->hpMax;
            session->onlineMp = role->mp;
            session->onlineMpMax = role->mpMax;
            break;
        }
        session = session->next;
    }
}

static bool vm_mock_service_account_set_role_name(const char *accountId,
                                                  const char *roleSelector,
                                                  const char *nameGbk,
                                                  const char **messageOut)
{
    vm_mock_service_account_state *state =
        vm_mock_service_open_account_role_db_for_management(accountId, messageOut);
    vm_net_mock_role_state *role = NULL;
    char before[32];
    size_t nameLen = 0;

    before[0] = 0;
    if (state == NULL)
        return false;
    if (nameGbk == NULL || nameGbk[0] == 0)
    {
        if (messageOut)
            *messageOut = "角色名不能为空";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    nameLen = strlen(nameGbk);
    if (nameLen >= 32u)
    {
        if (messageOut)
            *messageOut = "角色名过长（GBK 最多 31 字节）";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut)
            *messageOut = "role not found";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    if (strcmp(role->name, nameGbk) != 0)
    {
        bool nameTaken = false;

        if (!vm_net_mock_account_roles_name_taken(nameGbk, role->roleId,
                                                  &nameTaken))
        {
            if (messageOut)
                *messageOut = "角色名检查失败";
            vm_mock_service_close_account_role_db_for_management(state, true);
            return false;
        }
        if (nameTaken)
        {
            if (messageOut)
                *messageOut = "角色名已被占用";
            vm_mock_service_close_account_role_db_for_management(state, true);
            return false;
        }
    }
    snprintf(before, sizeof(before), "%s", role->name);
    memset(role->name, 0, sizeof(role->name));
    memcpy(role->name, nameGbk, nameLen);
    role->name[nameLen] = 0;
    if (!vm_net_mock_role_db_save_role("admin-rename", role->roleId))
    {
        snprintf(role->name, sizeof(role->name), "%s", before);
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "role persistence failed";
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }
    vm_mock_service_account_sync_online_role_admin_fields(accountId, role);
    vm_mock_service_account_capture(state);
    printf("[info][mock-service] account_role_rename user=%s id=%u before=%s after=%s\n",
           accountId,
           role->roleId,
           before[0] ? before : "-",
           role->name[0] ? role->name : "-");
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}

static bool vm_mock_service_account_set_role_exp(const char *accountId,
                                                 const char *roleSelector,
                                                 u32 exp,
                                                 const char **messageOut)
{
    vm_mock_service_account_state *state =
        vm_mock_service_open_account_role_db_for_management(accountId, messageOut);
    vm_net_mock_role_state *role = NULL;
    u32 beforeExp = 0;
    u32 beforeLevel = 1;
    u32 afterLevel = 1;

    if (state == NULL)
        return false;
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut)
            *messageOut = "role not found";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    beforeExp = role->exp;
    beforeLevel = role->level ? role->level : 1;
    role->exp = exp;
    afterLevel = vm_net_mock_role_level_from_exp(role->exp);
    role->level = afterLevel;
    vm_net_mock_role_sync_derived_vitals(role);
    if (afterLevel > beforeLevel)
    {
        role->hp = role->hpMax;
        role->mp = role->mpMax;
    }
    if (!vm_net_mock_role_db_save_role("admin-exp-set", role->roleId))
    {
        role->exp = beforeExp;
        role->level = beforeLevel;
        vm_net_mock_role_sync_derived_vitals(role);
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "role persistence failed";
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }
    vm_mock_service_account_sync_online_role_admin_fields(accountId, role);
    vm_mock_service_account_capture(state);
    printf("[info][mock-service] account_role_exp_set user=%s role=%s id=%u "
           "exp=%u->%u level=%u->%u\n",
           accountId,
           role->name[0] ? role->name : "-",
           role->roleId,
           beforeExp,
           role->exp,
           beforeLevel,
           role->level);
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}

static bool vm_mock_service_account_add_role_wcoin(const char *accountId,
                                                   const char *roleSelector,
                                                   int32_t delta,
                                                   const char **messageOut)
{
    vm_mock_service_account_state *state =
        vm_mock_service_open_account_role_db_for_management(accountId, messageOut);
    vm_net_mock_role_state *role = NULL;
    u32 before = 0;
    u32 after = 0;
    uint64_t deduct = 0;

    if (state == NULL)
        return false;
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut)
            *messageOut = "role not found";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    if (delta == 0)
    {
        if (messageOut)
            *messageOut = "amount must be non-zero";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    before = role->wcoin;
    if (delta > 0)
    {
        after = vm_net_mock_role_add_wcoin(role, (u32)delta);
    }
    else
    {
        deduct = (uint64_t)(-(int64_t)delta);
        if ((uint64_t)before < deduct)
        {
            if (messageOut)
                *messageOut = "W 币不足，无法扣除";
            vm_mock_service_close_account_role_db_for_management(state, true);
            return false;
        }
        role->wcoin = before - (u32)deduct;
        after = role->wcoin;
    }
    if (!vm_net_mock_role_db_save_role(
            delta > 0 ? "admin-wcoin-add" : "admin-wcoin-deduct", role->roleId))
    {
        role->wcoin = before;
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "role persistence failed";
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }
    vm_mock_service_account_capture(state);
    printf("[info][mock-service] account_wcoin_adjust user=%s role=%s id=%u delta=%d before=%u after=%u\n",
           accountId,
           role->name[0] ? role->name : "-",
           role->roleId,
           (int)delta,
           before,
           after);
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}

static bool vm_mock_service_account_add_role_money(const char *accountId,
                                                   const char *roleSelector,
                                                   int32_t delta,
                                                   u32 *beforeOut,
                                                   u32 *afterOut,
                                                   const char **messageOut)
{
    vm_mock_service_account_state *state =
        vm_mock_service_open_account_role_db_for_management(accountId, messageOut);
    vm_net_mock_role_state *role = NULL;
    u32 before = 0;
    u32 after = 0;
    uint64_t deduct = 0;

    if (beforeOut)
        *beforeOut = 0;
    if (afterOut)
        *afterOut = 0;
    if (state == NULL)
        return false;
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut)
            *messageOut = "role not found";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    if (delta == 0)
    {
        if (messageOut)
            *messageOut = "amount must be non-zero";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    before = role->money;
    if (delta > 0)
    {
        after = (0xffffffffu - before < (u32)delta) ? 0xffffffffu : before + (u32)delta;
    }
    else
    {
        deduct = (uint64_t)(-(int64_t)delta);
        if ((uint64_t)before < deduct)
        {
            if (messageOut)
                *messageOut = "铜钱不足，无法扣除";
            vm_mock_service_close_account_role_db_for_management(state, true);
            return false;
        }
        after = before - (u32)deduct;
    }
    role->money = after;
    if (!vm_net_mock_role_db_save_role(
            delta > 0 ? "admin-money-add" : "admin-money-deduct", role->roleId))
    {
        role->money = before;
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "role persistence failed";
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }
    vm_mock_service_account_capture(state);
    printf("[info][mock-service] account_money_adjust user=%s role=%s id=%u delta=%d before=%u after=%u\n",
           accountId,
           role->name[0] ? role->name : "-",
           role->roleId,
           (int)delta,
           before,
           after);
    if (beforeOut)
        *beforeOut = before;
    if (afterOut)
        *afterOut = after;
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}

static bool vm_mock_service_account_grant_role_item(const char *accountId,
                                                     const char *roleSelector,
                                                     u32 itemId,
                                                     u32 count,
                                                     u16 *seqOut,
                                                     const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *existing = NULL;
    const vm_net_mock_shop_catalog_item *catalogItem = NULL;
    u32 before = 0;
    u32 after = 0;
    u16 seq = 0;

    if (seqOut)
        *seqOut = 0;
    if (messageOut)
        *messageOut = "物品给予失败";
    if (itemId == 0 || count == 0 || count > 255)
    {
        if (messageOut)
            *messageOut = "物品或数量无效";
        return false;
    }
    catalogItem = vm_net_mock_find_shop_catalog_item(itemId);
    if (catalogItem == NULL)
    {
        if (messageOut)
            *messageOut = "物品目录中不存在该物品";
        return false;
    }
    state = vm_mock_service_open_account_role_db_for_management(accountId,
                                                                 messageOut);
    if (state == NULL)
        return false;
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut)
            *messageOut = "角色不存在";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    existing = vm_net_mock_role_find_backpack_item(role, itemId, 0);
    before = existing ? existing->count : 0;
    if (!vm_net_mock_role_add_backpack_item_to_role(role, itemId, count, 0, &seq,
                                                     "admin-item-grant", NULL, 0))
    {
        if (messageOut)
            *messageOut = "角色背包已满，无法加入新物品";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    existing = vm_net_mock_role_find_backpack_item(role, itemId, seq);
    after = existing ? existing->count : count;
    /*
     * Same scoped persist as admin money/wcoin/equip-wash. Capture alone is
     * dropped by release_if_idle when the account is offline, so MySQL must
     * own account_role_backpack before close.
     */
    if (!vm_net_mock_role_db_save_role("admin-item-grant", role->roleId))
    {
        if (messageOut)
            *messageOut = "物品给予持久化失败";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    vm_mock_service_account_capture(state);
    printf("[info][mock-service] account_item_grant user=%s role=%s id=%u item=%u count=%u seq=%u stack=%u/%u\n",
           accountId,
           role->name[0] ? role->name : "-",
           role->roleId,
           itemId,
           count,
           seq,
           before,
           after);
    if (seqOut)
        *seqOut = seq;
    if (messageOut)
        *messageOut = "物品给予成功";
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}

/*
 * Admin: set one +4/+8/+12/+16 wash override on bag (by seq) or worn (by slot).
 * washed=0 clears the override (config default); wash_count may still be set.
 */
static bool vm_mock_service_account_set_role_equip_wash(
    const char *accountId,
    const char *roleSelector,
    const char *where,
    u32 loc,
    u8 unlockLevel,
    u8 washed,
    u8 type,
    u8 flag,
    u16 value,
    u16 washCount,
    const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_enhance_wash_state *wash = NULL;
    vm_net_mock_enhance_wash_slot *slot = NULL;
    vm_net_mock_backpack_item_state *bagItem = NULL;
    u32 itemId = 0;
    u16 enhanceLevel = 0;
    u32 instanceId = 0;
    u16 bagSeq = 0;
    int unlockIndex = -1;
    bool worn = false;

    if (messageOut)
        *messageOut = "洗练属性调整失败";
    if (where == NULL || (strcmp(where, "bag") != 0 && strcmp(where, "equip") != 0))
    {
        if (messageOut)
            *messageOut = "位置无效（bag/equip）";
        return false;
    }
    worn = strcmp(where, "equip") == 0;
    unlockIndex = vm_net_mock_enhance_index_for_unlock(unlockLevel);
    if (unlockIndex < 0)
    {
        if (messageOut)
            *messageOut = "档位无效（仅 +4/+8/+12/+16）";
        return false;
    }
    if (washed > 1u || flag > 1u)
    {
        if (messageOut)
            *messageOut = "washed/flag 参数无效";
        return false;
    }
    if (washed != 0 && (type < 1u || type > 10u))
    {
        if (messageOut)
            *messageOut = "已洗练时属性类型必须是 1..10";
        return false;
    }
    if (worn)
    {
        if (loc >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
        {
            if (messageOut)
                *messageOut = "穿戴槽位无效";
            return false;
        }
    }
    else if (loc == 0 || loc > 65535u)
    {
        if (messageOut)
            *messageOut = "背包 seq 无效";
        return false;
    }

    state = vm_mock_service_open_account_role_db_for_management(accountId,
                                                                 messageOut);
    if (state == NULL)
        return false;
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut)
            *messageOut = "角色不存在";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }

    if (worn)
    {
        itemId = role->equippedItemIds[loc];
        if (itemId == 0 ||
            vm_net_mock_find_equipment_catalog_item(itemId) == NULL)
        {
            if (messageOut)
                *messageOut = "该穿戴槽无装备";
            vm_mock_service_close_account_role_db_for_management(state, true);
            return false;
        }
        enhanceLevel = role->equippedEnhanceLevels[loc];
        instanceId = role->equippedEnhanceInstanceIds[loc];
        wash = &role->equippedEnhanceWash[loc];
        bagSeq = 0;
    }
    else
    {
        bagItem = vm_net_mock_role_find_backpack_item(role, 0, (u16)loc);
        if (bagItem == NULL || bagItem->itemId == 0)
        {
            if (messageOut)
                *messageOut = "背包中找不到该 seq";
            vm_mock_service_close_account_role_db_for_management(state, true);
            return false;
        }
        if (vm_net_mock_find_equipment_catalog_item(bagItem->itemId) == NULL)
        {
            if (messageOut)
                *messageOut = "该背包物品不是装备";
            vm_mock_service_close_account_role_db_for_management(state, true);
            return false;
        }
        itemId = bagItem->itemId;
        enhanceLevel = bagItem->enhanceLevel;
        instanceId = bagItem->enhanceInstanceId;
        wash = &bagItem->enhanceWash;
        bagSeq = bagItem->seq;
    }

    slot = &wash->slots[unlockIndex];
    if (washed == 0)
    {
        slot->washed = 0;
        slot->type = 0;
        slot->flag = 0;
        slot->value = 0;
        slot->washCount = washCount;
    }
    else
    {
        slot->washed = 1;
        slot->type = type;
        slot->flag = flag;
        slot->value = value;
        slot->washCount = washCount;
    }

    if (instanceId == 0)
    {
        instanceId = vm_net_mock_role_ensure_equipment_instance_id(
            role, 0, itemId);
        if (worn)
            role->equippedEnhanceInstanceIds[loc] = instanceId;
        else if (bagItem != NULL)
            bagItem->enhanceInstanceId = instanceId;
    }

    vm_net_mock_role_mark_inventory_dirty("admin-equip-wash");
    if (!vm_net_mock_role_db_save_role("admin-equip-wash", role->roleId))
    {
        if (messageOut)
            *messageOut = "洗练属性持久化失败";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    if (!vm_net_mock_enhance_wash_is_empty(wash))
    {
        (void)vm_net_mock_enhance_wash_record_upsert(
            role->roleId, instanceId, bagSeq, itemId, enhanceLevel, wash,
            unlockLevel);
    }
    vm_mock_service_account_capture(state);
    printf("[info][mock-service] account_equip_wash_set user=%s role=%s id=%u "
           "where=%s loc=%u item=%u unlock=%u washed=%u type=%u flag=%u "
           "value=%u count=%u instance=%u\n",
           accountId,
           role->name[0] ? role->name : "-",
           role->roleId,
           where,
           loc,
           itemId,
           (u32)unlockLevel,
           (u32)slot->washed,
           (u32)slot->type,
           (u32)slot->flag,
           (u32)slot->value,
           (u32)slot->washCount,
           instanceId);
    if (messageOut)
        *messageOut = "洗练属性已更新";
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}


enum
{
    VM_NET_MOCK_COMPLETED_SCENE_REUSE_TICKS = 120
};

static bool vm_net_mock_should_rearm_send_ready(void)
{
    return g_vm_net_mock_last_scene_change_target_valid ||
           g_vm_net_mock_last_completed_scene_change_target_valid;
}

static u16 vm_net_mock_read_le16_at(const u8 *data, u32 off)
{
    return (u16)((u16)data[off] | ((u16)data[off + 1] << 8));
}

static u16 vm_net_mock_read_be16_at(const u8 *data, u32 off)
{
    return (u16)(((u16)data[off] << 8) | (u16)data[off + 1]);
}

static u32 vm_net_mock_read_be32_at(const u8 *data, u32 off)
{
    return ((u32)data[off] << 24) |
           ((u32)data[off + 1] << 16) |
           ((u32)data[off + 2] << 8) |
           (u32)data[off + 3];
}

static u32 vm_net_mock_scene_payload_start(const u8 *data, u32 len)
{
    u32 base = 0;
    u32 mapNameLen = 0;
    u32 start = 0;

    if (data == NULL || len < 15)
        return 0;

    for (base = 0; base + 15 <= len && base < 32; ++base)
    {
        if (memcmp(data + base, "SCE2", 4) == 0)
            break;
    }
    if (base + 15 > len || base >= 32)
        return 0;

    mapNameLen = data[base + 10];
    start = base + 10 + 1 + mapNameLen + 4;
    if (start >= len)
        return 0;
    return start;
}

enum
{
    /* RegisterDisplayName(0x0100EEE0) owns four 36-byte dynamic label slots. */
    VM_NET_MOCK_SCENE_NPCINFO_MAX = 4,
    /* Keep the complete server-side scene catalog separate from the four rows
     * that the client can safely instantiate. */
    VM_NET_MOCK_SCENE_NPC_CATALOG_MAX = 32,
    /* Max server-managed accept bindings per dynamic NPC
     * (server_dynamic_npc_tasks rows).  Dialog still shares the 7-option
     * 26/1 budget with service entries. */
    VM_NET_MOCK_NPC_BOUND_TASK_MAX = 8,
    VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID = 20022,
    VM_NET_MOCK_TEST_TASK_ID = 900001
};

typedef struct
{
    u32 taskId;
    /* Binding-scoped: authorize re-offer after state=3 for this NPC only. */
    bool repeatable;
    /* 0 = unlimited daily accepts for this binding. */
    u16 dailyAcceptLimit;
} vm_net_mock_npc_bound_task;

typedef struct
{
    u32 actorId;
    /*
     * Server-managed accept bindings (0..BOUND_TASK_MAX).  SCE/XSE-only actors
     * keep boundTaskCount=0 and discover tasks from the script resource.
     * taskId/taskRepeatable/taskDailyAcceptLimit mirror boundTasks[0] for
     * instance-guide / legacy single-binding call sites.
     */
    u32 boundTaskCount;
    vm_net_mock_npc_bound_task boundTasks[VM_NET_MOCK_NPC_BOUND_TASK_MAX];
    u32 taskId;
    bool taskRepeatable;
    u16 taskDailyAcceptLimit;
    u32 challengeEnemyId;
    u16 x;
    u16 y;
    u16 kind;
    u16 orientation;
    u16 instanceX;
    u16 instanceY;
    u16 instanceMinLevel;
    /* Optional task gate before instance teleport (0 = none). */
    u32 teleportRequireTaskId;
    /* Bits 1..KIND_MAX: multi-service root entries on 26/1. */
    u32 serviceMask;
    char actorResource[64];
    char displayName[32];
    char scriptName[64];
    char instanceScene[64];
} vm_net_mock_scene_npcinfo_seed;

static void vm_net_mock_seed_sync_primary_bound_task(
    vm_net_mock_scene_npcinfo_seed *seed)
{
    if (seed == NULL)
        return;
    if (seed->boundTaskCount == 0)
    {
        seed->taskId = 0;
        seed->taskRepeatable = false;
        seed->taskDailyAcceptLimit = 0;
        return;
    }
    seed->taskId = seed->boundTasks[0].taskId;
    seed->taskRepeatable = seed->boundTasks[0].repeatable;
    seed->taskDailyAcceptLimit = seed->boundTasks[0].dailyAcceptLimit;
}

static bool vm_net_mock_seed_add_bound_task(vm_net_mock_scene_npcinfo_seed *seed,
                                            u32 taskId, bool repeatable,
                                            u16 dailyAcceptLimit)
{
    if (seed == NULL || taskId == 0)
        return false;
    for (u32 i = 0; i < seed->boundTaskCount; ++i)
    {
        if (seed->boundTasks[i].taskId == taskId)
        {
            seed->boundTasks[i].repeatable = repeatable;
            seed->boundTasks[i].dailyAcceptLimit = dailyAcceptLimit;
            vm_net_mock_seed_sync_primary_bound_task(seed);
            return true;
        }
    }
    if (seed->boundTaskCount >= VM_NET_MOCK_NPC_BOUND_TASK_MAX)
        return false;
    seed->boundTasks[seed->boundTaskCount].taskId = taskId;
    seed->boundTasks[seed->boundTaskCount].repeatable = repeatable;
    seed->boundTasks[seed->boundTaskCount].dailyAcceptLimit = dailyAcceptLimit;
    seed->boundTaskCount += 1;
    vm_net_mock_seed_sync_primary_bound_task(seed);
    return true;
}

static const vm_net_mock_npc_bound_task *vm_net_mock_seed_find_bound_task(
    const vm_net_mock_scene_npcinfo_seed *seed, u32 taskId)
{
    if (seed == NULL || taskId == 0)
        return NULL;
    for (u32 i = 0; i < seed->boundTaskCount; ++i)
    {
        if (seed->boundTasks[i].taskId == taskId)
            return &seed->boundTasks[i];
    }
    return NULL;
}

typedef struct
{
    bool active;
    bool loaded;
    char scene[64];
    vm_net_mock_scene_npcinfo_seed seeds[VM_NET_MOCK_SCENE_NPCINFO_MAX];
    u32 selectedCount;
    u32 totalCount;
    u32 dynamicCount;
} vm_net_mock_scene_npc_request_cache;

static vm_net_mock_scene_npc_request_cache g_vm_net_mock_scene_npc_request_cache;

static void vm_net_mock_scene_npc_request_cache_begin(void)
{
    memset(&g_vm_net_mock_scene_npc_request_cache, 0,
           sizeof(g_vm_net_mock_scene_npc_request_cache));
    g_vm_net_mock_scene_npc_request_cache.active = true;
}

static void vm_net_mock_scene_npc_request_cache_end(void)
{
    g_vm_net_mock_scene_npc_request_cache.active = false;
}

static bool vm_net_mock_ensure_actor_resource_published(
    const char *actorResource, const char **errorOut);

static u32 vm_net_mock_select_scene_npcinfo_seeds(
    const char *scene,
    vm_net_mock_scene_npcinfo_seed *seeds,
    u32 seedCap,
    u32 *totalOut,
    u32 *dynamicOut);

static u32 vm_net_mock_decode_lzss_resource_stream(const u8 *res, u32 resLen,
                                                   u8 *out, u32 outCap)
{
    u32 compressedLen = 0;
    u32 decodedLen = 0;
    u32 srcPos = 0;
    u32 dstPos = 0;
    const u8 *src = NULL;

    if (res == NULL || out == NULL || resLen < 9 || outCap == 0)
        return 0;
    if (res[0] != 1 && res[0] != 2)
        return 0;

    compressedLen = vm_net_mock_read_be32_at(res, 1);
    decodedLen = vm_net_mock_read_be32_at(res, 5) & 0x7fffffffu;
    if (compressedLen == 0 || decodedLen == 0 ||
        decodedLen > outCap || 9u + compressedLen > resLen)
    {
        return 0;
    }

    src = res + 9;
    memset(out, 0, decodedLen);
    while (srcPos < compressedLen && dstPos < decodedLen)
    {
        u8 token = src[srcPos];
        if ((token & 0x80) != 0)
        {
            u32 count = (u32)(token & 0x7f);
            u32 avail = decodedLen - dstPos;
            if (count > avail)
                count = avail;
            if (count == 0 || srcPos + 1u + count > compressedLen)
                return 0;
            memcpy(out + dstPos, src + srcPos + 1, count);
            srcPos += 1u + count;
            dstPos += count;
        }
        else
        {
            u32 count = (u32)(token >> 1);
            u32 avail = decodedLen - dstPos;
            u32 distance = 0;
            u32 copySrc = 0;
            if (count > avail)
                count = avail;
            if (count == 0 || srcPos + 1u >= compressedLen)
                return 0;
            distance = (((u32)token << 8) & 0x1ffu) | (u32)src[srcPos + 1];
            if (distance == 0 || distance > dstPos)
                return 0;
            copySrc = dstPos - distance;
            for (u32 i = 0; i < count; ++i)
                out[dstPos + i] = out[copySrc + i];
            srcPos += 2;
            dstPos += count;
        }
    }

    return dstPos;
}

static bool vm_net_mock_read_sce_len_string(const u8 *data, u32 len, u32 *pos,
                                            char *out, size_t outCap)
{
    u32 stringLen = 0;

    if (data == NULL || pos == NULL || out == NULL || outCap == 0 || *pos >= len)
        return false;
    stringLen = data[*pos];
    if (*pos + 1 + stringLen > len || stringLen >= outCap)
        return false;
    memcpy(out, data + *pos + 1, stringLen);
    out[stringLen] = 0;
    *pos += 1 + stringLen;
    return true;
}

static bool vm_net_mock_read_sce_scalar_field(const u8 *data, u32 len, u32 *pos,
                                              u16 expectedField, u16 *valueOut)
{
    if (data == NULL || pos == NULL || valueOut == NULL || *pos + 6 > len)
        return false;
    if (vm_net_mock_read_le16_at(data, *pos) != 1 ||
        vm_net_mock_read_le16_at(data, *pos + 2) != expectedField)
    {
        return false;
    }
    *valueOut = vm_net_mock_read_le16_at(data, *pos + 4);
    *pos += 6;
    return true;
}

static bool vm_net_mock_parse_sce_edge_portal_at(const u8 *data, u32 len, u32 off,
                                                 vm_net_mock_sce_edge_portal *portal,
                                                 u32 *endOut)
{
    u32 pos = off;
    u16 kind = 0;
    u16 field = 0;

    if (data == NULL || portal == NULL || off + 18 > len)
        return false;
    memset(portal, 0, sizeof(*portal));

    kind = vm_net_mock_read_le16_at(data, pos);
    pos += 2;
    if (kind == 2)
    {
        if (pos + 4 > len)
            return false;
        portal->spawnX = vm_net_mock_read_le16_at(data, pos);
        portal->spawnY = vm_net_mock_read_le16_at(data, pos + 2);
        pos += 4;
    }
    else
    {
        if (pos + 6 > len || vm_net_mock_read_le16_at(data, pos) != 2)
            return false;
        portal->spawnX = vm_net_mock_read_le16_at(data, pos + 2);
        portal->spawnY = vm_net_mock_read_le16_at(data, pos + 4);
        pos += 6;
    }

    if (pos + 8 > len || vm_net_mock_read_le16_at(data, pos) != 8)
        return false;
    pos += 8;

    if (pos + 5 > len || vm_net_mock_read_le16_at(data, pos) != 3)
        return false;
    field = vm_net_mock_read_le16_at(data, pos + 2);
    pos += 4;
    if (field != 6 ||
        !vm_net_mock_read_sce_len_string(data, len, &pos, portal->targetScene, sizeof(portal->targetScene)) ||
        !vm_net_mock_str_ends_with(portal->targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(portal->targetScene))
    {
        return false;
    }

    if (!vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x07, &portal->entryId) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0a, &portal->left) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0b, &portal->top) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0c, &portal->right) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x0d, &portal->bottom) ||
        !vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x13, &portal->targetEntryId))
    {
        return false;
    }

    if (portal->right < portal->left || portal->bottom < portal->top)
        return false;
    if (endOut)
        *endOut = pos;
    return true;
}

static u32 vm_net_mock_load_scene_resource(const char *scene, u8 *out, u32 outCap)
{
    char path[256];
    u8 raw[8192];
    u32 rawLen = 0;
    u32 decodedLen = 0;

    if (scene == NULL || scene[0] == 0 || out == NULL || outCap == 0 ||
        vm_net_mock_scene_name_has_path_separator(scene))
    {
        return 0;
    }

    if (!vm_net_mock_open_server_scene_resource(scene, NULL, path, sizeof(path)))
        return 0;
    rawLen = vm_net_mock_load_response_file(path, raw, sizeof(raw));
    if (rawLen == 0)
        return 0;

    if (rawLen > 4)
    {
        u32 declaredLen = vm_net_mock_read_le16_at(raw, 0) |
                          ((u32)vm_net_mock_read_le16_at(raw, 2) << 16);
        if (declaredLen != 0 && declaredLen <= rawLen - 4 &&
            (raw[4] == 1 || raw[4] == 2))
        {
            decodedLen = vm_net_mock_decode_lzss_resource_stream(raw + 4,
                                                                 declaredLen,
                                                                 out,
                                                                 outCap);
            if (decodedLen != 0 && vm_net_mock_scene_payload_start(out, decodedLen) != 0)
                return decodedLen;
            return 0;
        }
    }

    if (vm_net_mock_scene_payload_start(raw, rawLen) != 0)
    {
        if (rawLen > outCap)
            return 0;
        memcpy(out, raw, rawLen);
        return rawLen;
    }

    decodedLen = vm_net_mock_decode_lzss_resource_stream(raw, rawLen, out, outCap);
    if (decodedLen != 0 && vm_net_mock_scene_payload_start(out, decodedLen) != 0)
        return decodedLen;

    if (rawLen > outCap)
        return 0;
    memcpy(out, raw, rawLen);
    return rawLen;
}

#define VM_NET_MOCK_SCE_EDIT_DECODE_MAX 32768u
#define VM_NET_MOCK_SCE_EDIT_ENCODE_MAX 49152u
#define VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX 64u

typedef struct
{
    vm_net_mock_sce_edge_portal portal;
    u32 offset;
    u32 end;
    u16 rawKind;
    u8 header8[8];
} vm_net_mock_sce_edge_portal_located;

static void vm_net_mock_store_le16_at(u8 *out, u32 offset, u16 value)
{
    out[offset] = (u8)(value & 0xffu);
    out[offset + 1] = (u8)((value >> 8) & 0xffu);
}

static u32 vm_net_mock_encode_lzss_literals(const u8 *src, u32 srcLen,
                                            u8 *out, u32 outCap)
{
    u32 srcPos = 0;
    u32 dstPos = 0;

    if (src == NULL || out == NULL || srcLen == 0)
        return 0;
    while (srcPos < srcLen)
    {
        u32 chunk = srcLen - srcPos;
        if (chunk > 127u)
            chunk = 127u;
        if (dstPos + 1u + chunk > outCap)
            return 0;
        out[dstPos++] = (u8)(0x80u | chunk);
        memcpy(out + dstPos, src + srcPos, chunk);
        dstPos += chunk;
        srcPos += chunk;
    }
    return dstPos;
}

static bool vm_net_mock_write_scene_resource_bytes(const char *scene,
                                                   const u8 *decoded,
                                                   u32 decodedLen,
                                                   const char **errorOut)
{
    char path[1200];
    u8 *encoded = NULL;
    u8 *fileBuf = NULL;
    u32 compressedLen = 0;
    u32 blockLen = 0;
    FILE *fp = NULL;
    bool ok = false;

    if (errorOut)
        *errorOut = "写入场景失败";
    if (scene == NULL || scene[0] == 0 || decoded == NULL || decodedLen == 0 ||
        vm_net_mock_scene_payload_start(decoded, decodedLen) == 0)
    {
        return false;
    }
    if (!vm_net_mock_open_server_scene_resource(scene, NULL, path, sizeof(path)) &&
        !vm_net_mock_update_resource_path(scene, path, sizeof(path)))
    {
        if (errorOut)
            *errorOut = "找不到场景资源路径";
        return false;
    }
    encoded = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_ENCODE_MAX);
    fileBuf = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_ENCODE_MAX + 16u);
    if (encoded == NULL || fileBuf == NULL)
    {
        if (errorOut)
            *errorOut = "场景写入内存不足";
        goto done;
    }
    compressedLen = vm_net_mock_encode_lzss_literals(
        decoded, decodedLen, encoded, VM_NET_MOCK_SCE_EDIT_ENCODE_MAX);
    if (compressedLen == 0)
    {
        if (errorOut)
            *errorOut = "场景 LZSS 编码失败";
        goto done;
    }
    blockLen = 1u + 4u + 4u + compressedLen;
    if (4u + blockLen > VM_NET_MOCK_SCE_EDIT_ENCODE_MAX + 16u)
    {
        if (errorOut)
            *errorOut = "场景编码后超过大小上限";
        goto done;
    }
    vm_net_mock_store_le32(fileBuf, 0, blockLen);
    fileBuf[4] = 2;
    vm_net_mock_store_be32(fileBuf, 5, compressedLen);
    vm_net_mock_store_be32(fileBuf, 9, decodedLen);
    memcpy(fileBuf + 13, encoded, compressedLen);
    fp = vm_net_mock_fopen_game_path(path, "wb");
    if (fp == NULL)
    {
        if (errorOut)
            *errorOut = "无法打开场景文件写入";
        goto done;
    }
    if (fwrite(fileBuf, 1, 4u + blockLen, fp) != 4u + blockLen)
    {
        if (errorOut)
            *errorOut = "写入场景文件失败";
        goto done;
    }
    fclose(fp);
    fp = NULL;
    /*
     * Dream scenes keep both extensionless and *.sce twins
     * (29梦境空间_01 + 29梦境空间_01.sce). open_server_scene_resource prefers
     * the extensionless leaf; mirror the same bytes to the sibling so publish
     * and client WT18/7 (+.sce fallback) stay consistent.
     */
    {
        char sibling[1200];
        size_t pathLen = strlen(path);
        bool pathIsSce = vm_net_mock_str_ends_with(path, ".sce");
        bool shouldMirror = false;

        memset(sibling, 0, sizeof(sibling));
        if (pathIsSce)
        {
            if (pathLen > 4u && pathLen - 4u < sizeof(sibling))
            {
                memcpy(sibling, path, pathLen - 4u);
                sibling[pathLen - 4u] = 0;
                {
                    FILE *probe = vm_net_mock_fopen_game_path(sibling, "rb");
                    shouldMirror = probe != NULL;
                    if (probe != NULL)
                        fclose(probe);
                }
            }
        }
        else if (pathLen + 4u < sizeof(sibling))
        {
            snprintf(sibling, sizeof(sibling), "%s.sce", path);
            shouldMirror = true;
        }
        if (shouldMirror && sibling[0] != 0)
        {
            FILE *siblingFp = vm_net_mock_fopen_game_path(sibling, "wb");
            if (siblingFp != NULL)
            {
                (void)fwrite(fileBuf, 1, 4u + blockLen, siblingFp);
                fclose(siblingFp);
                printf("[info][mock-admin] scene_resource_write_mirror "
                       "scene=%s sibling=%s\n",
                       scene, sibling);
            }
        }
    }
    ok = true;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] scene_resource_write scene=%s path=%s decoded=%u compressed=%u\n",
           scene, path, decodedLen, compressedLen);

done:
    if (fp != NULL)
        fclose(fp);
    free(encoded);
    free(fileBuf);
    return ok;
}

/*
 * Write an unwrapped .map payload as LZSS type-2 JHOnlineData resource.
 * Unlike scene writes, this does not require SCE2 magic and does not mirror
 * extensionless twins.
 */
static bool vm_net_mock_write_map_resource_bytes(const char *mapLeaf,
                                                 const u8 *decoded,
                                                 u32 decodedLen,
                                                 const char **errorOut)
{
    char path[1200];
    u8 *encoded = NULL;
    u8 *fileBuf = NULL;
    u32 compressedLen = 0;
    u32 blockLen = 0;
    FILE *fp = NULL;
    bool ok = false;

    if (errorOut)
        *errorOut = "写入地图失败";
    if (mapLeaf == NULL || mapLeaf[0] == 0 || decoded == NULL ||
        decodedLen == 0 || !vm_net_mock_update_name_is_safe(mapLeaf) ||
        !vm_net_mock_str_ends_with(mapLeaf, ".map"))
    {
        return false;
    }
    memset(path, 0, sizeof(path));
    if (!vm_net_mock_update_resource_path(mapLeaf, path, sizeof(path)))
    {
        if (errorOut)
            *errorOut = "找不到地图资源路径";
        return false;
    }
    encoded = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_ENCODE_MAX);
    fileBuf = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_ENCODE_MAX + 16u);
    if (encoded == NULL || fileBuf == NULL)
    {
        if (errorOut)
            *errorOut = "地图写入内存不足";
        goto done;
    }
    compressedLen = vm_net_mock_encode_lzss_literals(
        decoded, decodedLen, encoded, VM_NET_MOCK_SCE_EDIT_ENCODE_MAX);
    if (compressedLen == 0)
    {
        if (errorOut)
            *errorOut = "地图 LZSS 编码失败";
        goto done;
    }
    blockLen = 1u + 4u + 4u + compressedLen;
    if (4u + blockLen > VM_NET_MOCK_SCE_EDIT_ENCODE_MAX + 16u)
    {
        if (errorOut)
            *errorOut = "地图编码后超过大小上限";
        goto done;
    }
    vm_net_mock_store_le32(fileBuf, 0, blockLen);
    fileBuf[4] = 2;
    vm_net_mock_store_be32(fileBuf, 5, compressedLen);
    vm_net_mock_store_be32(fileBuf, 9, decodedLen);
    memcpy(fileBuf + 13, encoded, compressedLen);
    fp = vm_net_mock_fopen_game_path(path, "wb");
    if (fp == NULL)
    {
        if (errorOut)
            *errorOut = "无法打开地图文件写入";
        goto done;
    }
    if (fwrite(fileBuf, 1, 4u + blockLen, fp) != 4u + blockLen)
    {
        if (errorOut)
            *errorOut = "写入地图文件失败";
        goto done;
    }
    fclose(fp);
    fp = NULL;
    ok = true;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] map_resource_write map=%s path=%s decoded=%u compressed=%u\n",
           mapLeaf, path, decodedLen, compressedLen);

done:
    if (fp != NULL)
        fclose(fp);
    free(encoded);
    free(fileBuf);
    return ok;
}

/*
 * SCE2 header embeds the walkable .map leaf (e.g. dream 29*_01 → 29*_03.map).
 * Layout after magic+size: optional u16 tag 1, then u8 length + name.
 */
static bool vm_net_mock_sce_embedded_map_leaf(const u8 *data, u32 len,
                                              char *out, size_t outCap)
{
    u32 base = 0;
    u32 pos = 0;
    u8 nameLen = 0;

    if (out == NULL || outCap < 8u || data == NULL || len < 24u)
        return false;
    out[0] = 0;
    for (base = 0; base + 12u <= len && base < 32u; ++base)
    {
        if (memcmp(data + base, "SCE2", 4) == 0)
            break;
    }
    if (base + 12u > len || base >= 32u)
        return false;
    pos = base + 8u;
    if (pos + 3u <= len && vm_net_mock_read_le16_at(data, pos) == 1u)
        pos += 2u;
    if (pos >= len)
        return false;
    nameLen = data[pos++];
    if (nameLen < 5u || nameLen + 1u > outCap || pos + nameLen > len)
        return false;
    memcpy(out, data + pos, nameLen);
    out[nameLen] = 0;
    return vm_net_mock_str_ends_with(out, ".map") &&
           vm_net_mock_update_name_is_safe(out);
}

/*
 * Locate SCE2 embedded map leaf as [nameLen byte .. name bytes).
 * Used when cloning a scene onto a private .map so it no longer shares tiles.
 */
static bool vm_net_mock_sce_embedded_map_range(const u8 *data, u32 len,
                                               u32 *rangeStartOut,
                                               u32 *rangeEndOut,
                                               char *out, size_t outCap)
{
    u32 base = 0;
    u32 pos = 0;
    u8 nameLen = 0;

    if (rangeStartOut)
        *rangeStartOut = 0;
    if (rangeEndOut)
        *rangeEndOut = 0;
    if (out != NULL && outCap > 0)
        out[0] = 0;
    if (data == NULL || len < 24u)
        return false;
    for (base = 0; base + 12u <= len && base < 32u; ++base)
    {
        if (memcmp(data + base, "SCE2", 4) == 0)
            break;
    }
    if (base + 12u > len || base >= 32u)
        return false;
    pos = base + 8u;
    if (pos + 3u <= len && vm_net_mock_read_le16_at(data, pos) == 1u)
        pos += 2u;
    if (pos >= len)
        return false;
    nameLen = data[pos];
    if (nameLen < 5u || pos + 1u + nameLen > len)
        return false;
    if (out != NULL)
    {
        if (nameLen + 1u > outCap)
            return false;
        memcpy(out, data + pos + 1u, nameLen);
        out[nameLen] = 0;
        if (!vm_net_mock_str_ends_with(out, ".map") ||
            !vm_net_mock_update_name_is_safe(out))
            return false;
    }
    if (rangeStartOut)
        *rangeStartOut = pos;
    if (rangeEndOut)
        *rangeEndOut = pos + 1u + nameLen;
    return true;
}

/* Rewrite SCE2 embedded .map leaf; does not publish (caller owns publish). */
static bool vm_net_mock_sce_payload_replace_range(
    u8 *data, u32 *lenInOut, u32 lenCap, u32 offset, u32 oldEnd,
    const u8 *replacement, u32 replacementLen);
static bool vm_net_mock_sce_retarget_embedded_map_leaf(
    const char *scene, const char *newMapLeaf, const char **errorOut)
{
    u8 *data = NULL;
    u32 len = 0;
    u32 rangeStart = 0;
    u32 rangeEnd = 0;
    char oldLeaf[64];
    u8 replacement[1 + 63];
    size_t newLen = 0;
    const char *writeError = NULL;

    if (errorOut)
        *errorOut = "改写场景地图引用失败";
    if (scene == NULL || scene[0] == 0 || newMapLeaf == NULL ||
        newMapLeaf[0] == 0 || !vm_net_mock_str_ends_with(newMapLeaf, ".map") ||
        !vm_net_mock_update_name_is_safe(newMapLeaf))
    {
        return false;
    }
    newLen = strlen(newMapLeaf);
    if (newLen == 0 || newLen > 63u)
    {
        if (errorOut)
            *errorOut = "新地图文件名过长";
        return false;
    }
    data = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    if (data == NULL)
    {
        if (errorOut)
            *errorOut = "改写地图引用内存不足";
        return false;
    }
    len = vm_net_mock_load_scene_resource(scene, data,
                                          VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    memset(oldLeaf, 0, sizeof(oldLeaf));
    if (len == 0 ||
        !vm_net_mock_sce_embedded_map_range(data, len, &rangeStart, &rangeEnd,
                                             oldLeaf, sizeof(oldLeaf)))
    {
        free(data);
        if (errorOut)
            *errorOut = "场景缺少可改写的内嵌 .map 引用";
        return false;
    }
    if (strcmp(oldLeaf, newMapLeaf) == 0)
    {
        free(data);
        if (errorOut)
            *errorOut = "ok";
        return true;
    }
    replacement[0] = (u8)newLen;
    memcpy(replacement + 1, newMapLeaf, newLen);
    if (!vm_net_mock_sce_payload_replace_range(
            data, &len, VM_NET_MOCK_SCE_EDIT_DECODE_MAX, rangeStart, rangeEnd,
            replacement, (u32)(1u + newLen)) ||
        !vm_net_mock_write_scene_resource_bytes(scene, data, len, &writeError))
    {
        free(data);
        if (errorOut)
            *errorOut = writeError ? writeError : "写入场景地图引用失败";
        return false;
    }
    free(data);
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] scene_map_retarget scene=%s from=%s to=%s\n",
           scene, oldLeaf, newMapLeaf);
    return true;
}

static bool vm_net_mock_publish_scene_resource(const char *scene,
                                               const char **errorOut)
{
    char path[1200];
    char leaf[256];
    char twin[256];
    char mapLeaf[256];
    const char *names[4];
    u32 nameCount = 0;
    const char *slash = NULL;
    u8 *sceData = NULL;
    u32 sceLen = 0;

    if (scene == NULL || scene[0] == 0)
    {
        if (errorOut)
            *errorOut = "场景名无效";
        return false;
    }
    /*
     * Admin paths pass the runtime scene key (often without .sce).  Resolve
     * the on-disk leaf via open_server_scene_resource.  Dream maps may live as
     * extensionless names; WT18/7 accepts those and also resolves +.sce.
     * Also publish the SCE2-embedded .map (梦境 _01/_02 share _03.map); otherwise
     * clientmiss gets not-published and EnterScene can die after a SCE update.
     */
    memset(path, 0, sizeof(path));
    memset(leaf, 0, sizeof(leaf));
    memset(twin, 0, sizeof(twin));
    memset(mapLeaf, 0, sizeof(mapLeaf));
    /*
     * Dream 29*: heal web/fs↔bin SCE twin drift before resolve/publish so
     * WT18/7 does not advertise stock-278 while bin/*.sce still has kind=3
     * (debug-191169: 使者再进 uptodate → 无刷怪).
     */
    if (scene[0] == '2' && scene[1] == '9')
        vm_net_mock_dream_sce_heal_authoritative_kind3(scene);
    if (!vm_net_mock_open_server_scene_resource(scene, NULL, path,
                                                sizeof(path)) ||
        path[0] == 0)
    {
        if (errorOut)
            *errorOut = "场景资源不存在，无法发布";
        return false;
    }
    slash = strrchr(path, '/');
#ifdef _WIN32
    {
        const char *bslash = strrchr(path, '\\');
        if (bslash != NULL && (slash == NULL || bslash > slash))
            slash = bslash;
    }
#endif
    snprintf(leaf, sizeof(leaf), "%s", slash != NULL ? slash + 1 : path);
    if (!vm_net_mock_update_name_is_safe(leaf))
    {
        if (errorOut)
            *errorOut = "场景资源文件名无效";
        return false;
    }
    names[nameCount++] = leaf;
    if (vm_net_mock_str_ends_with(leaf, ".sce"))
    {
        size_t leafLen = strlen(leaf);
        if (leafLen > 4u && leafLen - 4u < sizeof(twin))
        {
            memcpy(twin, leaf, leafLen - 4u);
            twin[leafLen - 4u] = 0;
            if (twin[0] != 0 && vm_net_mock_update_name_is_safe(twin) &&
                vm_net_mock_update_file_size(twin) > 0)
                names[nameCount++] = twin;
        }
    }
    else if (strlen(leaf) + 4u < sizeof(twin))
    {
        snprintf(twin, sizeof(twin), "%s.sce", leaf);
        if (vm_net_mock_update_name_is_safe(twin) &&
            vm_net_mock_update_file_size(twin) > 0)
            names[nameCount++] = twin;
    }
    sceData = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    if (sceData != NULL)
    {
        sceLen = vm_net_mock_load_scene_resource(scene, sceData,
                                                 VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
        if (sceLen != 0 &&
            vm_net_mock_sce_embedded_map_leaf(sceData, sceLen, mapLeaf,
                                              sizeof(mapLeaf)) &&
            vm_net_mock_update_file_size(mapLeaf) > 0)
        {
            bool already = false;
            for (u32 i = 0; i < nameCount; ++i)
            {
                if (strcmp(names[i], mapLeaf) == 0)
                {
                    already = true;
                    break;
                }
            }
            if (!already && nameCount < 4u)
                names[nameCount++] = mapLeaf;
        }
        free(sceData);
    }
    return vm_net_mock_update_admin_publish_named_files(names, nameCount,
                                                       errorOut);
}

static u32 vm_net_mock_sce_edge_portal_encode(
    const vm_net_mock_sce_edge_portal *portal, u16 rawKind,
    const u8 header8[8], u8 *buf, u32 bufCap)
{
    static const u8 kDefaultHeader8[8] = {
        0x08, 0x00, 0x01, 0x00, 0x05, 0x00, 0x01, 0x00};
    u32 pos = 0;
    size_t nameLen = 0;
    u16 kind = rawKind != 0 ? rawKind : 3u;
    const u8 *hdr = header8 != NULL ? header8 : kDefaultHeader8;

    if (portal == NULL || buf == NULL || bufCap < 64u ||
        portal->targetScene[0] == 0 ||
        !vm_net_mock_str_ends_with(portal->targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(portal->targetScene) ||
        portal->right < portal->left || portal->bottom < portal->top)
    {
        return 0;
    }
    nameLen = strlen(portal->targetScene);
    if (nameLen == 0 || nameLen > 255u)
        return 0;
    if (kind == 2u)
    {
        if (pos + 6u + 8u + 4u + 1u + nameLen + 30u > bufCap)
            return 0;
        vm_net_mock_store_le16_at(buf, pos, 2);
        pos += 2;
        vm_net_mock_store_le16_at(buf, pos, portal->spawnX);
        vm_net_mock_store_le16_at(buf, pos + 2, portal->spawnY);
        pos += 4;
    }
    else
    {
        if (pos + 8u + 8u + 4u + 1u + nameLen + 30u > bufCap)
            return 0;
        vm_net_mock_store_le16_at(buf, pos, kind);
        pos += 2;
        vm_net_mock_store_le16_at(buf, pos, 2);
        pos += 2;
        vm_net_mock_store_le16_at(buf, pos, portal->spawnX);
        vm_net_mock_store_le16_at(buf, pos + 2, portal->spawnY);
        pos += 4;
    }
    memcpy(buf + pos, hdr, 8);
    pos += 8;
    vm_net_mock_store_le16_at(buf, pos, 3);
    vm_net_mock_store_le16_at(buf, pos + 2, 6);
    pos += 4;
    buf[pos++] = (u8)nameLen;
    memcpy(buf + pos, portal->targetScene, nameLen);
    pos += (u32)nameLen;
    vm_net_mock_store_le16_at(buf, pos, 1);
    vm_net_mock_store_le16_at(buf, pos + 2, 0x07);
    vm_net_mock_store_le16_at(buf, pos + 4, portal->entryId);
    pos += 6;
    vm_net_mock_store_le16_at(buf, pos, 1);
    vm_net_mock_store_le16_at(buf, pos + 2, 0x0a);
    vm_net_mock_store_le16_at(buf, pos + 4, portal->left);
    pos += 6;
    vm_net_mock_store_le16_at(buf, pos, 1);
    vm_net_mock_store_le16_at(buf, pos + 2, 0x0b);
    vm_net_mock_store_le16_at(buf, pos + 4, portal->top);
    pos += 6;
    vm_net_mock_store_le16_at(buf, pos, 1);
    vm_net_mock_store_le16_at(buf, pos + 2, 0x0c);
    vm_net_mock_store_le16_at(buf, pos + 4, portal->right);
    pos += 6;
    vm_net_mock_store_le16_at(buf, pos, 1);
    vm_net_mock_store_le16_at(buf, pos + 2, 0x0d);
    vm_net_mock_store_le16_at(buf, pos + 4, portal->bottom);
    pos += 6;
    vm_net_mock_store_le16_at(buf, pos, 1);
    vm_net_mock_store_le16_at(buf, pos + 2, 0x13);
    vm_net_mock_store_le16_at(buf, pos + 4, portal->targetEntryId);
    pos += 6;
    return pos;
}

static u32 vm_net_mock_sce_edge_portal_locate_in(
    const u8 *data, u32 len, vm_net_mock_sce_edge_portal_located *out, u32 cap,
    u32 *totalOut)
{
    u32 start = 0;
    u32 count = 0;
    u32 total = 0;

    if (totalOut)
        *totalOut = 0;
    if (data == NULL || len == 0)
        return 0;
    start = vm_net_mock_scene_payload_start(data, len);
    if (start == 0)
        return 0;
    for (u32 off = start; off + 12u <= len; ++off)
    {
        vm_net_mock_sce_edge_portal portal;
        u32 end = 0;
        u32 headerPos = 0;

        memset(&portal, 0, sizeof(portal));
        if (!vm_net_mock_parse_sce_edge_portal_at(data, len, off, &portal, &end))
            continue;
        ++total;
        if (out != NULL && count < cap)
        {
            u16 kind = vm_net_mock_read_le16_at(data, off);
            memset(&out[count], 0, sizeof(out[count]));
            out[count].portal = portal;
            out[count].offset = off;
            out[count].end = end;
            out[count].rawKind = kind;
            if (kind == 2u)
                headerPos = off + 2u + 4u;
            else
                headerPos = off + 2u + 6u;
            if (headerPos + 8u <= len)
                memcpy(out[count].header8, data + headerPos, 8);
            ++count;
        }
        if (end > off + 1u)
            off = end - 1u;
    }
    if (totalOut)
        *totalOut = total;
    return count;
}

static u32 vm_net_mock_sce_edge_portal_locate_all(
    const char *scene, vm_net_mock_sce_edge_portal_located *out, u32 cap,
    u32 *totalOut)
{
    u8 *data = NULL;
    u32 len = 0;
    u32 count = 0;

    if (totalOut)
        *totalOut = 0;
    data = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    if (data == NULL)
        return 0;
    len = vm_net_mock_load_scene_resource(scene, data,
                                          VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    count = vm_net_mock_sce_edge_portal_locate_in(data, len, out, cap, totalOut);
    free(data);
    return count;
}

static bool vm_net_mock_sce_payload_replace_range(
    u8 *data, u32 *lenInOut, u32 lenCap, u32 offset, u32 oldEnd,
    const u8 *replacement, u32 replacementLen)
{
    u32 len = 0;
    u32 oldLen = 0;
    u32 newLen = 0;
    u32 tail = 0;

    if (data == NULL || lenInOut == NULL ||
        (replacement == NULL && replacementLen != 0))
        return false;
    len = *lenInOut;
    if (offset > len || oldEnd > len || oldEnd < offset)
        return false;
    oldLen = oldEnd - offset;
    newLen = len - oldLen + replacementLen;
    if (newLen > lenCap)
        return false;
    tail = len - oldEnd;
    if (replacementLen != oldLen && tail != 0)
        memmove(data + offset + replacementLen, data + oldEnd, tail);
    if (replacementLen != 0)
        memcpy(data + offset, replacement, replacementLen);
    *lenInOut = newLen;
    return true;
}

static bool vm_net_mock_sce_edge_portal_patch_landing(
    const char *targetScene, u16 entryId, u16 landingX, u16 landingY,
    const char **errorOut)
{
    u8 *data = NULL;
    vm_net_mock_sce_edge_portal_located *located = NULL;
    u32 len = 0;
    u32 count = 0;
    u32 total = 0;
    int match = -1;
    u8 record[256];
    u32 recordLen = 0;

    if (errorOut)
        *errorOut = "更新落脚点失败";
    if (targetScene == NULL || targetScene[0] == 0)
        return false;
    data = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    located = (vm_net_mock_sce_edge_portal_located *)calloc(
        VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX, sizeof(*located));
    if (data == NULL || located == NULL)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "落脚点更新内存不足";
        return false;
    }
    len = vm_net_mock_load_scene_resource(targetScene, data,
                                          VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    count = vm_net_mock_sce_edge_portal_locate_in(
        data, len, located, VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX, &total);
    for (u32 i = 0; i < count; ++i)
    {
        if (located[i].portal.entryId == entryId)
        {
            match = (int)i;
            break;
        }
    }
    if (match < 0)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "目标场景缺少对应 entryId 的入口传送点";
        return false;
    }
    located[match].portal.spawnX = landingX;
    located[match].portal.spawnY = landingY;
    recordLen = vm_net_mock_sce_edge_portal_encode(
        &located[match].portal, located[match].rawKind, located[match].header8,
        record, sizeof(record));
    if (recordLen == 0 ||
        !vm_net_mock_sce_payload_replace_range(
            data, &len, VM_NET_MOCK_SCE_EDIT_DECODE_MAX, located[match].offset,
            located[match].end, record, recordLen) ||
        !vm_net_mock_write_scene_resource_bytes(targetScene, data, len,
                                                errorOut))
    {
        free(data);
        free(located);
        return false;
    }
    free(data);
    free(located);
    if (errorOut)
        *errorOut = "ok";
    return true;
}

static bool vm_net_mock_sce_edge_portal_admin_save(
    const char *scene, u32 portalIndex,
    const vm_net_mock_sce_edge_portal *portal, u16 landingX, u16 landingY,
    bool updateLanding, const char **errorOut)
{
    u8 *data = NULL;
    vm_net_mock_sce_edge_portal_located *located = NULL;
    u8 record[256];
    u32 len = 0;
    u32 count = 0;
    u32 total = 0;
    u32 recordLen = 0;
    const char *writeError = NULL;
    const char *publishError = NULL;

    if (errorOut)
        *errorOut = "传送点保存失败";
    if (scene == NULL || portal == NULL || portal->targetScene[0] == 0 ||
        portal->right < portal->left || portal->bottom < portal->top ||
        !vm_net_mock_str_ends_with(portal->targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(portal->targetScene))
    {
        return false;
    }
    data = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    located = (vm_net_mock_sce_edge_portal_located *)calloc(
        VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX, sizeof(*located));
    if (data == NULL || located == NULL)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "传送点保存内存不足";
        return false;
    }
    len = vm_net_mock_load_scene_resource(scene, data,
                                          VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    count = vm_net_mock_sce_edge_portal_locate_in(
        data, len, located, VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX, &total);
    if (portalIndex >= count)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "传送点序号无效";
        return false;
    }
    recordLen = vm_net_mock_sce_edge_portal_encode(
        portal, located[portalIndex].rawKind, located[portalIndex].header8,
        record, sizeof(record));
    if (recordLen == 0 ||
        !vm_net_mock_sce_payload_replace_range(
            data, &len, VM_NET_MOCK_SCE_EDIT_DECODE_MAX,
            located[portalIndex].offset, located[portalIndex].end, record,
            recordLen) ||
        !vm_net_mock_write_scene_resource_bytes(scene, data, len, &writeError) ||
        !vm_net_mock_publish_scene_resource(scene, &publishError))
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = publishError ? publishError :
                        (writeError ? writeError : "传送点写入或发布失败");
        return false;
    }
    free(data);
    free(located);
    if (updateLanding)
    {
        if (!vm_net_mock_sce_edge_portal_patch_landing(
                portal->targetScene, portal->entryId, landingX, landingY,
                &writeError))
        {
            if (errorOut)
                *errorOut = writeError ? writeError :
                            "源传送点已保存，但目标落脚点更新失败";
            return false;
        }
    }
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] scene_portal_save scene=%s index=%u target=%s "
           "rect=(%u,%u)-(%u,%u) spawn=(%u,%u) entry=%u targetEntry=%u "
           "landing=%u\n",
           scene, portalIndex, portal->targetScene, portal->left, portal->top,
           portal->right, portal->bottom, portal->spawnX, portal->spawnY,
           portal->entryId, portal->targetEntryId, updateLanding ? 1u : 0u);
    return true;
}

static bool vm_net_mock_sce_edge_portal_admin_add(
    const char *scene, const vm_net_mock_sce_edge_portal *portal,
    const char **errorOut)
{
    static const u8 kDefaultHeader8[8] = {
        0x08, 0x00, 0x01, 0x00, 0x05, 0x00, 0x01, 0x00};
    u8 *data = NULL;
    vm_net_mock_sce_edge_portal_located *located = NULL;
    u8 record[256];
    u32 len = 0;
    u32 count = 0;
    u32 total = 0;
    u32 recordLen = 0;
    u32 insertAt = 0;
    const char *writeError = NULL;
    const char *publishError = NULL;

    if (errorOut)
        *errorOut = "新增传送点失败";
    if (scene == NULL || portal == NULL)
        return false;
    data = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    located = (vm_net_mock_sce_edge_portal_located *)calloc(
        VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX, sizeof(*located));
    if (data == NULL || located == NULL)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "新增传送点内存不足";
        return false;
    }
    len = vm_net_mock_load_scene_resource(scene, data,
                                          VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    if (len == 0 || vm_net_mock_scene_payload_start(data, len) == 0)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "场景资源无法解码";
        return false;
    }
    count = vm_net_mock_sce_edge_portal_locate_in(
        data, len, located, VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX, &total);
    if (count >= VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "场景边缘传送点数量已达上限";
        return false;
    }
    insertAt = count != 0 ? located[count - 1u].end : len;
    recordLen = vm_net_mock_sce_edge_portal_encode(portal, 3, kDefaultHeader8,
                                                   record, sizeof(record));
    if (recordLen == 0 ||
        !vm_net_mock_sce_payload_replace_range(
            data, &len, VM_NET_MOCK_SCE_EDIT_DECODE_MAX, insertAt, insertAt,
            record, recordLen) ||
        !vm_net_mock_write_scene_resource_bytes(scene, data, len, &writeError) ||
        !vm_net_mock_publish_scene_resource(scene, &publishError))
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = publishError ? publishError :
                        (writeError ? writeError : "新增传送点写入或发布失败");
        return false;
    }
    free(data);
    free(located);
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] scene_portal_add scene=%s target=%s "
           "rect=(%u,%u)-(%u,%u) spawn=(%u,%u) entry=%u\n",
           scene, portal->targetScene, portal->left, portal->top, portal->right,
           portal->bottom, portal->spawnX, portal->spawnY, portal->entryId);
    return true;
}

static bool vm_net_mock_sce_edge_portal_admin_delete(const char *scene,
                                                     u32 portalIndex,
                                                     const char **errorOut)
{
    u8 *data = NULL;
    vm_net_mock_sce_edge_portal_located *located = NULL;
    u32 len = 0;
    u32 count = 0;
    u32 total = 0;
    const char *writeError = NULL;
    const char *publishError = NULL;

    if (errorOut)
        *errorOut = "删除传送点失败";
    data = (u8 *)malloc(VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    located = (vm_net_mock_sce_edge_portal_located *)calloc(
        VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX, sizeof(*located));
    if (data == NULL || located == NULL)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "删除传送点内存不足";
        return false;
    }
    len = vm_net_mock_load_scene_resource(scene, data,
                                          VM_NET_MOCK_SCE_EDIT_DECODE_MAX);
    count = vm_net_mock_sce_edge_portal_locate_in(
        data, len, located, VM_NET_MOCK_SCE_EDGE_PORTAL_ADMIN_MAX, &total);
    if (portalIndex >= count)
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = "传送点序号无效";
        return false;
    }
    if (!vm_net_mock_sce_payload_replace_range(
            data, &len, VM_NET_MOCK_SCE_EDIT_DECODE_MAX,
            located[portalIndex].offset, located[portalIndex].end, NULL, 0) ||
        !vm_net_mock_write_scene_resource_bytes(scene, data, len, &writeError) ||
        !vm_net_mock_publish_scene_resource(scene, &publishError))
    {
        free(data);
        free(located);
        if (errorOut)
            *errorOut = publishError ? publishError :
                        (writeError ? writeError : "删除传送点写入或发布失败");
        return false;
    }
    free(data);
    free(located);
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] scene_portal_delete scene=%s index=%u\n", scene,
           portalIndex);
    return true;
}

