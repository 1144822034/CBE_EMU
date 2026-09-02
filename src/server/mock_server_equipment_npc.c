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
    u8 itemCount = 0;
    vm_net_mock_role_state before;
    vm_net_mock_equipped_item_state newInstance;
    vm_net_mock_equipped_item_state oldInstance;

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

    memset(&newInstance, 0, sizeof(newInstance));
    newInstance.itemId = backpackItem->itemId;
    newInstance.enhanceLevel = backpackItem->enhanceLevel;
    newInstance.enhanceAffixes = backpackItem->enhanceAffixes;
    newInstance.durability = backpackItem->durability;
    newInstance.durabilityMax = backpackItem->durabilityMax;

    slot = newEquip->slot;
    oldInstance = role->equippedItems[slot];
    oldItemId = oldInstance.itemId;
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
    backpackItem->itemId = oldInstance.itemId;
    backpackItem->count = 1;
    backpackItem->enhanceLevel = oldInstance.enhanceLevel;
    backpackItem->enhanceAffixes = oldInstance.enhanceAffixes;
    backpackItem->durability = oldInstance.durability;
    backpackItem->durabilityMax = oldInstance.durabilityMax;
    role->equippedItems[slot] = newInstance;
    vm_net_mock_role_sync_derived_vitals(role);
    if (!vm_net_mock_role_db_save("item-equip-swap"))
    {
        *role = before;
        if (reasonOut)
            *reasonOut = "persistence-failed";
        return false;
    }

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
    vm_net_mock_equipped_item_state instance;
    vm_net_mock_role_state before;

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

    /*
     * Equipped rows have a different identity namespace from backpack rows:
     * their client-facing sequence is exactly slot + 1 (see the login iteminfo
     * contract and the 7/9 replacement request's `body` field).  The native
     * 7/8 type=4 request normally carries only that sequence.  It therefore
     * identifies one slot directly; it is neither a backpack instance id nor
     * an item id.  Treating it as an unqualified selector used to make every
     * unload fail as soon as the character wore a second item.
     */
    if (requestedSeq != 0)
    {
        if (requestedSeq > VM_NET_MOCK_EQUIP_SLOT_COUNT)
        {
            if (reasonOut)
                *reasonOut = "equipped-seq-out-of-range";
            return false;
        }
        slot = (u8)(requestedSeq - 1u);
        itemId = role->equippedItems[slot].itemId;
        if (itemId == 0)
        {
            if (reasonOut)
                *reasonOut = "equipped-slot-empty";
            return false;
        }
        if (requestedItemId != 0 && requestedItemId != itemId)
        {
            if (reasonOut)
                *reasonOut = "equipped-selector-mismatch";
            return false;
        }
    }
    else if (requestedItemId != 0)
    {
        /* Keep the explicit-id variant for callers that genuinely omit the
         * native slot selector; duplicate item ids still resolve by catalog
         * slot when that slot currently contains an item. */
        for (u8 i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
        {
            if (role->equippedItems[i].itemId == requestedItemId)
            {
                slot = i;
                itemId = requestedItemId;
                break;
            }
        }
        if (slot == 0xff)
        {
            equip = vm_net_mock_find_equipment_catalog_item(requestedItemId);
            if (equip != NULL && equip->slot < VM_NET_MOCK_EQUIP_SLOT_COUNT &&
                role->equippedItems[equip->slot].itemId != 0)
            {
                slot = equip->slot;
                itemId = role->equippedItems[slot].itemId;
            }
        }
    }

    if (slot == 0xff || itemId == 0)
    {
        if (reasonOut)
            *reasonOut = "equipped-item-not-found";
        return false;
    }

    before = *role;
    instance = role->equippedItems[slot];
    if (!vm_net_mock_role_append_backpack_equipment_instance(role, &instance, &seq))
    {
        if (reasonOut)
            *reasonOut = "bag-full";
        return false;
    }

    memset(&role->equippedItems[slot], 0, sizeof(role->equippedItems[slot]));
    vm_net_mock_role_sync_derived_vitals(role);
    if (!vm_net_mock_role_db_save("item-unequip"))
    {
        *role = before;
        if (reasonOut)
            *reasonOut = "persistence-failed";
        return false;
    }

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
    vm_net_mock_finish_wt_packet(out, pos, 1);

    printf("[info][network] mock_item_equip type=%u requested_item=%u requested_seq=%u item=%u seq=%u slot=%u old=%u result=%u reason=%s resp=7/8 evidence=JianghuOL.CBE:0x01033544\n",
           parsed.type,
           parsed.itemId,
           parsed.seq,
           itemId,
           seq ? seq : parsed.seq,
           slot,
           oldItemId,
           result,
           reason ? reason : "-");
    vm_autotest_note("mock_item_equip type=%u requested_item=%u requested_seq=%u item=%u seq=%u slot=%u old=%u result=%u reason=%s response=7/8 evidence=JianghuOL.CBE:0x01033544(type3-result-seq)\n",
                     parsed.type,
                     parsed.itemId,
                     parsed.seq,
                     itemId,
                     seq ? seq : parsed.seq,
                     slot,
                     oldItemId,
                     result,
                     reason ? reason : "-");
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
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 9, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    printf("[info][network] mock_item_equip_swap body=%u bag=%u companion_2_10=%u item=%u old=%u slot=%u result=%u reason=%s resp=7/9 evidence=JianghuOL.CBE:0x010328D4+0x01033544\n",
           parsed.bodySeq, parsed.backpackSeq,
           parsed.hasActorOtherCompanion ? 1u : 0u, itemId, oldItemId, slot, result,
           reason ? reason : "-");
    vm_autotest_note("mock_item_equip_swap body=%u bag=%u companion_2_10=%u item=%u old=%u slot=%u result=%u reason=%s response=7/9 evidence=JianghuOL.CBE:0x010328D4(body-bag)+0x01033544(result-only)\n",
                     parsed.bodySeq, parsed.backpackSeq,
                     parsed.hasActorOtherCompanion ? 1u : 0u, itemId, oldItemId, slot,
                     result, reason ? reason : "-");
    return pos;
}

typedef enum
{
    VM_NET_MOCK_EQUIP_ENHANCE_REJECT_EQUIPMENT_NOT_FOUND = 1,
    VM_NET_MOCK_EQUIP_ENHANCE_REJECT_MONEY_INSUFFICIENT,
    VM_NET_MOCK_EQUIP_ENHANCE_REJECT_CRYSTAL_INSUFFICIENT,
    VM_NET_MOCK_EQUIP_ENHANCE_REJECT_MAX_LEVEL
} vm_net_mock_equipment_enhance_reject_reason;

/* HandleItemUseAndEquip(0x01028C7C) switches the 29/3 result directly.
 * Raw JianghuOL.CBE GBK strings at 0x01027B90/0x010296A4/0x010296B0 prove
 * this exact contract: 4=money insufficient, 5=crystal insufficient,
 * 6=enhancement level cap.  Do not reuse a server-local error ordering here:
 * doing so presents the wrong client message while rejecting valid work. */
static u8 vm_net_mock_equipment_enhance_reject_result(
    u8 subtype,
    vm_net_mock_equipment_enhance_reject_reason reason)
{
    if (subtype == 1)
    {
        if (reason == VM_NET_MOCK_EQUIP_ENHANCE_REJECT_EQUIPMENT_NOT_FOUND)
            return 2;
        if (reason == VM_NET_MOCK_EQUIP_ENHANCE_REJECT_MAX_LEVEL)
            return 3;
        return 0;
    }

    switch (reason)
    {
    case VM_NET_MOCK_EQUIP_ENHANCE_REJECT_EQUIPMENT_NOT_FOUND:
        return 3;
    case VM_NET_MOCK_EQUIP_ENHANCE_REJECT_MONEY_INSUFFICIENT:
        return 4;
    case VM_NET_MOCK_EQUIP_ENHANCE_REJECT_CRYSTAL_INSUFFICIENT:
        return 5;
    case VM_NET_MOCK_EQUIP_ENHANCE_REJECT_MAX_LEVEL:
        return 6;
    default:
        return 0;
    }
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

/* A target-tier crystal represents one full attempt.  Each lower tier has one
 * third of the immediately higher tier's contribution: with target +16, a
 * level-15 crystal is 1/3 and a level-14 crystal is 1/9.  Powers of three
 * retain those ratios exactly without floating point.  The 29/1 tables,
 * 29/2 preview and 29/3 roll all use these helpers so the display cannot
 * diverge from the committed result. */
static u32 vm_net_mock_equipment_enhance_level_power(u32 level)
{
    u32 power = 1;

    if (level == 0 || level > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL + 1u)
        return 0;
    for (u32 exponent = 1; exponent < level; ++exponent)
        power *= 3u;
    return power;
}

static u32 vm_net_mock_equipment_enhance_crystal_power(u32 tier)
{
    if (tier == 0 || tier > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        return 0;
    return vm_net_mock_equipment_enhance_level_power(tier);
}

static u32 vm_net_mock_equipment_enhance_required_power(u8 currentLevel)
{
    u32 targetLevel = (u32)currentLevel + 1u;

    return vm_net_mock_equipment_enhance_level_power(targetLevel);
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

        for (u32 j = 0; j < parsed->materialRows; ++j)
        {
            if (itemIds[j] == itemIds[i])
                requestedCount += counts[j];
        }
        material = vm_net_mock_role_find_backpack_item(role, itemIds[i], 0);
        if (material == NULL || material->count < requestedCount)
            return false;
        u32 crystalPower = vm_net_mock_equipment_enhance_crystal_power(tier);
        uint64_t addedPower = (uint64_t)crystalPower * counts[i];

        if (addedPower > 0xffffffffull - power)
            power = 0xffffffffu;
        else
            power += (u32)addedPower;
    }
    if (powerOut)
        *powerOut = power;
    return true;
}

static u32 vm_net_mock_equipment_enhance_success_rate(u8 level, u32 power)
{
    u32 required = vm_net_mock_equipment_enhance_required_power(level);
    uint64_t rate = 0;

    if (required == 0)
        return 0;
    if (power >= required)
        return 100;
    rate = ((uint64_t)power * 100u) / required;
    return rate > 100 ? 100 : rate;
}

/* 29/2.value is an integral percentage, but 29/3 keeps the exact fractional
 * probability by drawing from the same power denominator.  Mix the existing
 * deterministic scheduler/sequence inputs before the modulus: using the raw
 * (small) tick directly would bias high-denominator attempts toward success. */
static u32 vm_net_mock_equipment_enhance_roll(u16 equipSeq, u8 currentLevel,
                                              u32 denominator)
{
    u32 state = g_schedulerTick;

    if (denominator == 0)
        return 0;
    state ^= (u32)equipSeq * 0x9e3779b9u;
    state ^= (u32)currentLevel * 0x85ebca6bu;
    state ^= state >> 16;
    state *= 0x7feb352du;
    state ^= state >> 15;
    state *= 0x846ca68bu;
    state ^= state >> 16;
    return state % denominator;
}

static u32 vm_net_mock_equipment_enhance_money_cost(u8 level)
{
    return ((u32)level + 1u) * 100u;
}

typedef bool (*vm_net_mock_equipment_enhance_save_callback)(const char *reason);

/* Enhancement spends several independent fields in one request: crystals,
 * copper, level, and generated affixes.  The role snapshot is the transaction
 * boundary at this layer; a failed durable save must not leave any of those
 * fields changed in the live session while the database still has the old
 * instance.  `result=0` is deliberately reserved for this infrastructure
 * failure because the native 29/3 parser treats it as a cancelled action and
 * does not apply the material-consuming result=1/2 branch. */
static bool vm_net_mock_equipment_enhance_persist_or_rollback(
    vm_net_mock_role_state *role,
    const vm_net_mock_role_state *before,
    const char *saveReason,
    vm_net_mock_equipment_enhance_save_callback saveCallback)
{
    if (saveReason == NULL || saveReason[0] == 0)
        saveReason = "equipment-enhance";
    bool saved = saveCallback != NULL
                     ? saveCallback(saveReason)
                     : vm_net_mock_role_db_save(saveReason);

    if (saved)
        return true;
    if (role != NULL && before != NULL)
        *role = *before;
    return false;
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

static u32 vm_net_mock_build_equipment_enhance_response_with_save_callback(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap,
    vm_net_mock_equipment_enhance_save_callback saveCallback)
{
    vm_net_mock_equipment_enhance_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *equipment = NULL;
    const vm_net_mock_equipment_catalog_item *catalog = NULL;
    u32 itemIds[5];
    u8 counts[5];
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
    u8 result = 1;
    u8 currentLevel = 0;
    u8 responseObjectCount = 1;
    bool materialsValid = false;
    bool enhancementSucceeded = false;
    bool roleSnapshotValid = false;
    vm_net_mock_role_state roleBeforeEnhancement;
    const char *reason = "ok";

    memset(&parsed, 0, sizeof(parsed));
    memset(itemIds, 0, sizeof(itemIds));
    memset(counts, 0, sizeof(counts));
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
        result = vm_net_mock_equipment_enhance_reject_result(
            parsed.subtype, VM_NET_MOCK_EQUIP_ENHANCE_REJECT_EQUIPMENT_NOT_FOUND);
        reason = "equipment-not-found";
    }
    else
    {
        currentLevel = (u8)SDL_min(
            equipment->enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
        if (currentLevel >= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        {
            result = vm_net_mock_equipment_enhance_reject_result(
                parsed.subtype, VM_NET_MOCK_EQUIP_ENHANCE_REJECT_MAX_LEVEL);
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
            result = vm_net_mock_equipment_enhance_reject_result(
                parsed.subtype,
                VM_NET_MOCK_EQUIP_ENHANCE_REJECT_CRYSTAL_INSUFFICIENT);
            reason = "crystal-insufficient";
        }
        else
        {
            successRate = vm_net_mock_equipment_enhance_success_rate(
                currentLevel, materialPower);
            moneyCost = vm_net_mock_equipment_enhance_money_cost(currentLevel);
        }
    }

    if (parsed.subtype == 3 && result == 1)
    {
        if (role->money < moneyCost)
        {
            result = vm_net_mock_equipment_enhance_reject_result(
                parsed.subtype,
                VM_NET_MOCK_EQUIP_ENHANCE_REJECT_MONEY_INSUFFICIENT);
            reason = "money-insufficient";
        }
        else
        {
            roleBeforeEnhancement = *role;
            roleSnapshotValid = true;
            for (u32 i = 0; i < parsed.materialRows; ++i)
            {
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
                if (!vm_net_mock_role_consume_backpack_item(
                        role, itemIds[i], 0, consumeCount, &remaining))
                {
                    if (roleSnapshotValid)
                        *role = roleBeforeEnhancement;
                    result = vm_net_mock_equipment_enhance_reject_result(
                        parsed.subtype,
                        VM_NET_MOCK_EQUIP_ENHANCE_REJECT_CRYSTAL_INSUFFICIENT);
                    reason = "crystal-consume-failed";
                    break;
                }
            }
            if (result == 1)
            {
                u32 roll = vm_net_mock_equipment_enhance_roll(
                    parsed.equipSeq, currentLevel,
                    vm_net_mock_equipment_enhance_required_power(currentLevel));
                role->money -= moneyCost;
                enhancementSucceeded = roll < materialPower;
                result = enhancementSucceeded ? 1 : 2;
                equipment = vm_net_mock_role_find_backpack_item(
                    role, 0, parsed.equipSeq);
                if (enhancementSucceeded && equipment != NULL)
                {
                    u8 primaryType = 0;
                    u32 primaryBase = 0;
                    u32 primaryBefore = 0;
                    u32 primaryAfter = 0;
                    u8 stageThreshold = 0;
                    u8 stageType = 0;
                    u16 stageValue = 0;

                    equipment->enhanceLevel = (u16)(currentLevel + 1);
                    (void)vm_net_mock_equipment_enhancement_ensure_affixes(
                        catalog, (u8)equipment->enhanceLevel,
                        &equipment->enhanceAffixes,
                        role->roleId ^ equipment->itemId ^
                        ((u32)equipment->seq * 0x9e3779b9u));
                    if (vm_net_mock_equipment_enhancement_resolve_primary(
                            catalog, &primaryType, &primaryBase))
                    {
                        primaryBefore =
                            vm_net_mock_equipment_enhancement_bonus_from_base(
                                primaryBase, currentLevel);
                        primaryAfter =
                            vm_net_mock_equipment_enhancement_bonus_from_base(
                                primaryBase, (u8)equipment->enhanceLevel);
                    }
                    if ((equipment->enhanceLevel % 4u) == 0)
                    {
                        u8 stage = (u8)(equipment->enhanceLevel / 4u - 1u);

                        stageThreshold = (u8)equipment->enhanceLevel;
                        stageType = equipment->enhanceAffixes.type[stage];
                        stageValue = equipment->enhanceAffixes.value[stage];
                    }
                    printf("[info][network] mock_equipment_enhance_effect seq=%u item=%u level_before=%u level_after=%u primary_type=%u primary_base=%u primary_bonus_before=%u primary_bonus_after=%u primary_step=%u stage_threshold=%u stage_type=%u stage_value=%u\n",
                           equipment->seq, equipment->itemId, currentLevel,
                           equipment->enhanceLevel, primaryType, primaryBase,
                           primaryBefore, primaryAfter,
                           primaryAfter - primaryBefore, stageThreshold,
                           stageType, stageValue);
                    vm_autotest_note("mock_equipment_enhance_effect seq=%u item=%u level_before=%u level_after=%u primary_type=%u primary_base=%u primary_bonus_before=%u primary_bonus_after=%u primary_step=%u stage_threshold=%u stage_type=%u stage_value=%u\n",
                                     equipment->seq, equipment->itemId,
                                     currentLevel, equipment->enhanceLevel,
                                     primaryType, primaryBase, primaryBefore,
                                     primaryAfter, primaryAfter - primaryBefore,
                                     stageThreshold, stageType, stageValue);
                }
                if (!vm_net_mock_equipment_enhance_persist_or_rollback(
                        role, &roleBeforeEnhancement,
                        enhancementSucceeded ? "equipment-enhance-success"
                                              : "equipment-enhance-failed",
                        saveCallback))
                {
                    enhancementSucceeded = false;
                    result = 0;
                    reason = "persistence-failed";
                    printf("[error][network] mock_equipment_enhance_persist_failed "
                           "seq=%u item=%u level_before=%u action=rollback "
                           "response_result=0 evidence=JianghuOL.CBE:0x01028C7C\n",
                           parsed.equipSeq, equipmentItemId, currentLevel);
                    vm_autotest_note(
                        "mock_equipment_enhance_persist_failed seq=%u item=%u "
                        "level_before=%u action=rollback response_result=0 "
                        "evidence=JianghuOL.CBE:0x01028C7C\n",
                        parsed.equipSeq, equipmentItemId, currentLevel);
                }
                else
                {
                    reason = enhancementSucceeded ? "success" : "failed-roll";
                }
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
        if (result == 1)
        {
            /* 29/1's two arrays are the enhancement screen's material
             * requirement and crystal-power tables.  They are not the
             * CalcEquipStatBonus primary-rule table: CBE reads that table
             * from its separately initialized controller field +0x584. */
            for (u32 level = 0;
                 level <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++level)
            {
                if (!vm_net_mock_seq_put_u32(data1, sizeof(data1), &data1Len,
                                             vm_net_mock_equipment_enhance_required_power(
                                                 (u8)level)))
                    return 0;
            }
            for (u32 tier = 1;
                 tier <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++tier)
            {
                if (!vm_net_mock_seq_put_u32(data2, sizeof(data2), &data2Len,
                                             vm_net_mock_equipment_enhance_crystal_power(
                                                 tier)))
                    return 0;
            }
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
    else if ((result == 1 || result == 2) &&
             (!vm_net_mock_build_equipment_enhance_material_blob(
                  occult, sizeof(occult), &parsed, itemIds, counts,
                  &occultLen) ||
              !vm_net_mock_put_object_u8(out, outCap, &pos, "tnum",
                                         parsed.materialRows) ||
              !vm_net_mock_put_object_u16(out, outCap, &pos, "equipseq",
                                          parsed.equipSeq) ||
              !vm_net_mock_put_object_raw(out, outCap, &pos, "occult", occult,
                                          (u16)occultLen)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);

    printf("[info][network] mock_equipment_enhance phase=%u seq=%u item=%u level=%u result=%u crystals=%u power=%u rate=%u money=%u success=%u reason=%s resp=29/%u evidence=JianghuOL.CBE:0x0101CD1E+0x0101DD1E+0x01028C7C\n",
           parsed.subtype, parsed.equipSeq,
           equipmentItemId, currentLevel, result,
           parsed.materialRows, materialPower, successRate, moneyCost,
           enhancementSucceeded ? 1 : 0, reason, parsed.subtype);
    vm_autotest_note("mock_equipment_enhance phase=%u seq=%u item=%u level=%u result=%u crystals=%u power=%u rate=%u money=%u success=%u reason=%s response=29/%u evidence=JianghuOL.CBE:0x0101CD1E+0x0101DD1E+0x01028C7C\n",
                     parsed.subtype, parsed.equipSeq,
                     equipmentItemId, currentLevel, result,
                     parsed.materialRows, materialPower, successRate, moneyCost,
                     enhancementSucceeded ? 1 : 0, reason, parsed.subtype);
    return pos;
}

static u32 vm_net_mock_build_equipment_enhance_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    return vm_net_mock_build_equipment_enhance_response_with_save_callback(
        request, requestLen, out, outCap, NULL);
}

typedef struct
{
    u8 result;
    u8 sourceLevel;
    u8 destinationLevel;
    u8 crystalFlag;
    u8 crystalConsumed;
    u32 destinationItemId;
    u32 sourceItemId;
    u32 crystalItemId;
    u32 moneyCost;
    u16 crystalSeq;
    const char *crystalName;
    const char *reason;
} vm_net_mock_equipment_transfer_result;

typedef bool (*vm_net_mock_equipment_transfer_save_callback)(const char *reason);

static u32 vm_net_mock_equipment_transfer_money_cost(u8 sourceLevel)
{
    /* The firmware receives, stores and later subtracts this server field; it
     * contains no native price table.  Keep the policy explicit and on the
     * same 100-copper scale as ordinary enhancement. */
    return (u32)sourceLevel * 100u;
}

static u8 vm_net_mock_equipment_transfer_preview(
    vm_net_mock_role_state *role,
    const vm_net_mock_equipment_transfer_request *parsed,
    vm_net_mock_equipment_transfer_result *state)
{
    vm_net_mock_backpack_item_state *destination = NULL;
    vm_net_mock_backpack_item_state *source = NULL;
    vm_net_mock_backpack_item_state *crystal = NULL;
    const vm_net_mock_shop_catalog_item *crystalCatalog = NULL;

    if (state == NULL)
        return 0;
    memset(state, 0, sizeof(*state));
    state->result = 2;
    state->reason = "equipment-not-found";
    if (role == NULL || parsed == NULL)
        return state->result;

    destination = vm_net_mock_role_find_backpack_item(
        role, 0, parsed->destinationSeq);
    source = vm_net_mock_role_find_backpack_item(role, 0, parsed->sourceSeq);
    if (destination == NULL || source == NULL ||
        vm_net_mock_find_equipment_catalog_item(destination->itemId) == NULL ||
        vm_net_mock_find_equipment_catalog_item(source->itemId) == NULL)
    {
        return state->result;
    }
    state->destinationItemId = destination->itemId;
    state->sourceItemId = source->itemId;
    if (source->enhanceLevel == 0 ||
        source->enhanceLevel > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL ||
        destination->enhanceLevel >= source->enhanceLevel)
    {
        state->reason = "not-transferable-preview";
        return state->result;
    }

    state->sourceLevel = (u8)source->enhanceLevel;
    state->destinationLevel = (u8)destination->enhanceLevel;
    state->moneyCost =
        vm_net_mock_equipment_transfer_money_cost(state->sourceLevel);
    if (state->sourceLevel == 1)
    {
        /* HandleItemUseAndEquip treats result 3 as the normal no-crystal
         * preview and still reads money/level. */
        state->result = 3;
        state->reason = "no-crystal-required";
        return state->result;
    }

    state->crystalItemId = 900u + state->sourceLevel;
    crystalCatalog =
        vm_net_mock_find_shop_catalog_item(state->crystalItemId);
    if (crystalCatalog == NULL)
    {
        state->result = 0;
        state->reason = "crystal-catalog-unresolved";
        return 0;
    }
    crystal = vm_net_mock_role_find_backpack_item(
        role, state->crystalItemId, 0);
    state->crystalFlag = crystal != NULL && crystal->count != 0 ? 1 : 2;
    state->crystalSeq = state->crystalFlag == 1 ? crystal->seq : 0;
    state->crystalName = crystalCatalog->name;
    state->result = 1;
    state->reason = state->crystalFlag == 1 ? "ok" : "crystal-missing-preview";
    return state->result;
}

/* Apply one transfer to a role snapshot.  This helper owns no persistence so
 * deterministic regressions can exercise the exact mutation without opening
 * a database connection; the production wrapper below always persists. */
static u8 vm_net_mock_equipment_transfer_commit_in_memory(
    vm_net_mock_role_state *role,
    const vm_net_mock_equipment_transfer_request *parsed,
    vm_net_mock_equipment_transfer_result *state)
{
    vm_net_mock_backpack_item_state *destination = NULL;
    vm_net_mock_backpack_item_state *source = NULL;
    vm_net_mock_backpack_item_state *crystal = NULL;
    const vm_net_mock_equipment_catalog_item *destinationCatalog = NULL;
    const vm_net_mock_equipment_catalog_item *sourceCatalog = NULL;
    vm_net_mock_role_state before;
    u32 crystalRemaining = 0;
    bool crystalRequired = false;

    if (state == NULL)
        return 0;
    memset(state, 0, sizeof(*state));
    state->result = 2;
    state->reason = "equipment-not-found";
    if (role == NULL || parsed == NULL)
        return state->result;

    destination = vm_net_mock_role_find_backpack_item(
        role, 0, parsed->destinationSeq);
    source = vm_net_mock_role_find_backpack_item(role, 0, parsed->sourceSeq);
    if (destination != NULL)
        destinationCatalog =
            vm_net_mock_find_equipment_catalog_item(destination->itemId);
    if (source != NULL)
        sourceCatalog = vm_net_mock_find_equipment_catalog_item(source->itemId);
    if (destination == NULL || source == NULL ||
        destinationCatalog == NULL || sourceCatalog == NULL)
    {
        return state->result;
    }

    state->destinationItemId = destination->itemId;
    state->sourceItemId = source->itemId;
    state->destinationLevel = (u8)SDL_min(
        destination->enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
    state->sourceLevel = (u8)SDL_min(
        source->enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
    if (source->enhanceLevel == 0 ||
        source->enhanceLevel > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL ||
        destination->enhanceLevel >= source->enhanceLevel)
    {
        state->result = 5;
        state->reason = "not-transferable";
        return state->result;
    }

    state->moneyCost =
        vm_net_mock_equipment_transfer_money_cost(state->sourceLevel);
    if (state->sourceLevel > 1)
    {
        state->crystalItemId = 900u + state->sourceLevel;
        crystal = vm_net_mock_role_find_backpack_item(
            role, state->crystalItemId, 0);
        if (crystal == NULL || crystal->count == 0)
        {
            state->result = 3;
            state->reason = "crystal-insufficient";
            return state->result;
        }
        state->crystalSeq = crystal->seq;
        crystalRequired = true;
    }
    if (role->money < state->moneyCost)
    {
        state->result = 4;
        state->reason = "money-insufficient";
        return state->result;
    }

    before = *role;
    if (crystalRequired &&
        !vm_net_mock_role_consume_backpack_item(
            role, state->crystalItemId, state->crystalSeq, 1,
            &crystalRemaining))
    {
        *role = before;
        state->result = 3;
        state->reason = "crystal-consume-failed";
        return state->result;
    }
    state->crystalConsumed = crystalRequired ? 1 : 0;

    /* Consuming a one-item crystal row compacts backpackItems and invalidates
     * all earlier pointers.  Resolve both equipment instances again by their
     * stable sequences before changing either level. */
    destination = vm_net_mock_role_find_backpack_item(
        role, 0, parsed->destinationSeq);
    source = vm_net_mock_role_find_backpack_item(role, 0, parsed->sourceSeq);
    if (destination == NULL || source == NULL)
    {
        *role = before;
        state->result = 6;
        state->reason = "equipment-reacquire-failed";
        return state->result;
    }

    role->money -= state->moneyCost;
    destination->enhanceLevel = state->sourceLevel;
    source->enhanceLevel = 0;
    (void)vm_net_mock_equipment_enhancement_ensure_affixes(
        destinationCatalog, state->sourceLevel, &destination->enhanceAffixes,
        role->roleId ^ destination->itemId ^
            ((u32)destination->seq * 0x9e3779b9u));
    state->result = 1;
    state->reason = "success";
    return state->result;
}

static u8 vm_net_mock_equipment_transfer_commit(
    vm_net_mock_role_state *role,
    const vm_net_mock_equipment_transfer_request *parsed,
    vm_net_mock_equipment_transfer_result *state,
    vm_net_mock_equipment_transfer_save_callback saveCallback)
{
    vm_net_mock_role_state before;
    u8 result = 0;

    if (role == NULL)
        return vm_net_mock_equipment_transfer_commit_in_memory(
            role, parsed, state);
    before = *role;
    result = vm_net_mock_equipment_transfer_commit_in_memory(
        role, parsed, state);
    if (result == 1 &&
        (saveCallback == NULL || !saveCallback("equipment-transfer")))
    {
        *role = before;
        state->result = 6;
        state->reason = "persistence-failed";
        result = state->result;
    }
    return result;
}

static u32 vm_net_mock_build_equipment_transfer_packet(
    const vm_net_mock_equipment_transfer_request *parsed,
    const vm_net_mock_equipment_transfer_result *state,
    u8 *out,
    u32 outCap)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (parsed == NULL || state == NULL || state->result == 0 ||
        out == NULL || outCap < pos ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29,
                                     parsed->subtype, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result",
                                   state->result))
    {
        return 0;
    }
    if (parsed->subtype == 5 && state->result == 1)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "flag",
                                       state->crystalFlag) ||
            !vm_net_mock_put_object_u16(out, outCap, &pos, "id",
                                        (u16)state->crystalItemId) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "seq",
                                        state->crystalSeq) ||
            !vm_net_mock_put_object_string(
                out, outCap, &pos, "name",
                state->crystalName ? state->crystalName : ""))
        {
            return 0;
        }
    }
    if (parsed->subtype == 5 &&
        (state->result == 1 || state->result == 3))
    {
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "money",
                                        state->moneyCost) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "level",
                                       state->sourceLevel))
        {
            return 0;
        }
    }
    if (parsed->subtype == 6 && state->result == 1)
    {
        if (!vm_net_mock_put_object_u16(out, outCap, &pos, "seq",
                                        state->crystalSeq) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "num",
                                       state->crystalConsumed) ||
            !vm_net_mock_put_object_u16(out, outCap, &pos, "seqd",
                                        parsed->destinationSeq) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "curleveld",
                                       state->sourceLevel) ||
            !vm_net_mock_put_object_u16(out, outCap, &pos, "seqs",
                                        parsed->sourceSeq) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "curlevels", 0))
        {
            return 0;
        }
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_equipment_transfer_response(
    const u8 *request,
    u32 requestLen,
    u8 *out,
    u32 outCap)
{
    vm_net_mock_equipment_transfer_request parsed;
    vm_net_mock_equipment_transfer_result state;
    vm_net_mock_role_state *role = NULL;
    u32 pos = 0;
    u8 result = 0;

    memset(&parsed, 0, sizeof(parsed));
    memset(&state, 0, sizeof(state));
    if (out == NULL || outCap < 5 ||
        !vm_net_mock_parse_equipment_transfer_request(
            request, requestLen, &parsed))
    {
        return 0;
    }

    role = vm_net_mock_active_role();
    result = parsed.subtype == 5
                 ? vm_net_mock_equipment_transfer_preview(role, &parsed,
                                                          &state)
                 : vm_net_mock_equipment_transfer_commit(
                       role, &parsed, &state, vm_net_mock_role_db_save);
    if (result == 0)
    {
        printf("[error][network] mock_equipment_transfer phase=%u seqd=%u seqs=%u result=0 reason=%s action=unresolved-no-response evidence=JianghuOL.CBE:0x0101DAA0+0x01028C7C\n",
               parsed.subtype, parsed.destinationSeq, parsed.sourceSeq,
               state.reason ? state.reason : "builder-failed");
        return 0;
    }

    pos = vm_net_mock_build_equipment_transfer_packet(
        &parsed, &state, out, outCap);
    if (pos == 0)
        return 0;

    printf("[info][network] mock_equipment_transfer phase=%u seqd=%u itemd=%u leveld=%u seqs=%u items=%u levels=%u crystal=%u crystal_seq=%u crystal_flag=%u crystal_num=%u money=%u result=%u reason=%s resp=29/%u evidence=JianghuOL.CBE:0x0101DAA0+0x0101E54C+0x01028C7C\n",
           parsed.subtype, parsed.destinationSeq, state.destinationItemId,
           state.destinationLevel, parsed.sourceSeq, state.sourceItemId,
           state.sourceLevel, state.crystalItemId, state.crystalSeq,
           state.crystalFlag, state.crystalConsumed, state.moneyCost, result,
           state.reason ? state.reason : "-", parsed.subtype);
    vm_autotest_note("mock_equipment_transfer phase=%u seqd=%u itemd=%u leveld=%u seqs=%u items=%u levels=%u crystal=%u crystal_seq=%u crystal_flag=%u crystal_num=%u money=%u result=%u reason=%s response=29/%u evidence=JianghuOL.CBE:0x0101DAA0+0x0101E54C+0x01028C7C\n",
                     parsed.subtype, parsed.destinationSeq,
                     state.destinationItemId, state.destinationLevel,
                     parsed.sourceSeq, state.sourceItemId, state.sourceLevel,
                     state.crystalItemId, state.crystalSeq,
                     state.crystalFlag, state.crystalConsumed,
                     state.moneyCost, result,
                     state.reason ? state.reason : "-", parsed.subtype);
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

    if (!useSceneMonsterStart)
        return 1;
    if (forcedSpec != NULL && forcedSpec[0] != 0)
    {
        u32 forced = vm_net_mock_env_u32("CBE_BATTLE_ENEMY_COUNT", 1);
        if (forced < 1)
            forced = 1;
        if (forced > 3)
            forced = 3;
        return (u8)forced;
    }

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
    vm_net_mock_battle_sync_enemy_hp_totals();
}

static bool vm_net_mock_battle_roll_drop_rate_basis_points(u32 rateBasisPoints)
{
    if (rateBasisPoints == 0)
        return false;
    if (rateBasisPoints >= VM_NET_MOCK_DROP_RATE_BASIS_POINTS_MAX)
        return true;
    return (vm_net_mock_battle_reward_rand() %
            VM_NET_MOCK_DROP_RATE_BASIS_POINTS_MAX) < rateBasisPoints;
}

/* Escape and other non-drop paths still express their chance as an integer
 * percent.  Keep that contract separate from decimal equipment-drop rates. */
static bool vm_net_mock_battle_roll_percent(u32 percent)
{
    if (percent >= 100u)
        return vm_net_mock_battle_roll_drop_rate_basis_points(
            VM_NET_MOCK_DROP_RATE_BASIS_POINTS_MAX);
    return vm_net_mock_battle_roll_drop_rate_basis_points(
        percent * VM_NET_MOCK_DROP_RATE_BASIS_POINTS_PER_PERCENT);
}

/* A configured drop rate belongs to the battle, not to each enemy row.
 * Enemy count scales only the awarded quantity after that one roll succeeds.
 * Keeping this rule in one helper makes it impossible for a 5% drop to become
 * three independent 5% chances merely because the encounter spawned three
 * copies of the same monster. */
static u32 vm_net_mock_battle_drop_count_for_battle(u32 rateBasisPoints,
                                                    u32 enemyCount)
{
    if (enemyCount == 0)
        return 0;
    return vm_net_mock_battle_roll_drop_rate_basis_points(rateBasisPoints)
               ? enemyCount
               : 0;
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

/* Cap the one-monster base reward before any experience-card or insight
 * modifier.  Keeping this as a small pure rule makes the recipient-level
 * contract explicit and prevents a caller from accidentally applying a card
 * before the cap. */
static u32 vm_net_mock_battle_base_exp_cap_for_role_level(
    u32 roleLevel, vm_net_mock_monster_family family)
{
    u32 normalCap = vm_net_mock_normal_monster_exp_for_level(roleLevel);

    return family == VM_NET_MOCK_MONSTER_BOSS ?
               vm_net_mock_mul_capped_u32(normalCap, 5u) :
               normalCap;
}

static u32 vm_net_mock_battle_grant_reward_once(u32 *dropItemIdOut,
                                                u16 *dropSeqOut,
                                                u32 *dropCountOut,
                                                bool *dropGrantedOut,
                                                bool *rewardGrantedOut,
                                                u32 *rewardGoldOut)
{
    u32 rewardExp = 0;
    u32 rewardGold = 0;
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
    u32 baseRewardGold = 0;
    u32 expCardMultiplier = 1;
    u32 battleInsightBonusPercent = 0;
    u32 normalExpCap = 0;
    u32 normalGoldCap = 0;
    u32 perEnemyExp = 0;
    u32 perEnemyGold = 0;
    u32 rewardUnits = 0;
    u32 usedRewardUnits = 0;
    bool dailyRewardGranted = false;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (dropItemIdOut)
        *dropItemIdOut = 0;
    if (dropSeqOut)
        *dropSeqOut = 0;
    if (dropCountOut)
        *dropCountOut = 0;
    if (dropGrantedOut)
        *dropGrantedOut = false;
    if (rewardGrantedOut)
        *rewardGrantedOut = false;
    if (rewardGoldOut)
        *rewardGoldOut = 0;

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

    /* A battle session may only award once, but every distinct legal battle
     * awards normally.  Rapid-entry recording is deliberately independent of
     * this settlement path so a database audit failure can never create an
     * unrepresentable zero-delta 4/7 result. */
    if (rewardGrantedOut)
        *rewardGrantedOut = true;

    /* Count a defeated monster once per newly settled victory, independently
     * of whether any configured item happened to drop.  Loot progress is
     * recorded separately inside the loop below. */
    vm_net_mock_task_progress_after_battle(
        g_vm_net_mock_battle_enemy_id_current, enemyCount, 0, 0);

    {
        u32 roleLevel =
            role != NULL ? vm_net_mock_role_level_from_exp(role->exp) : 1u;

        /* A reward's level ceiling belongs to the recipient's current level,
         * not to the monster selected by the scene.  It prevents a high-level
         * target from bypassing the progression band while still allowing an
         * equal-level boss to be worth five normal kills.  Multipliers from
         * experience cards (and 战斗心得) remain downstream of this base cap. */
        normalExpCap = vm_net_mock_battle_base_exp_cap_for_role_level(
            roleLevel,
            vm_net_mock_monster_family_for_enemy(
                g_vm_net_mock_battle_enemy_id_current));
        normalGoldCap = vm_net_mock_normal_monster_gold_for_level(roleLevel);
    }
    perEnemyExp = vm_net_mock_env_u32_if_set(
        "CBE_BATTLE_REWARD_EXP",
        vm_net_mock_battle_reward_exp_for_enemy(
            g_vm_net_mock_battle_enemy_id_current));
    perEnemyGold = vm_net_mock_env_u32_if_set(
        "CBE_BATTLE_REWARD_GOLD",
        vm_net_mock_battle_reward_gold_for_enemy(
            g_vm_net_mock_battle_enemy_id_current));
    if (perEnemyExp > normalExpCap)
        perEnemyExp = normalExpCap;
    if (perEnemyGold > normalGoldCap)
        perEnemyGold = normalGoldCap;
    rewardUnits = enemyCount;
    baseRewardExp = vm_net_mock_mul_capped_u32(perEnemyExp, enemyCount);
    baseRewardGold = vm_net_mock_mul_capped_u32(perEnemyGold, enemyCount);
    rewardExp = baseRewardExp;
    if (role != NULL)
    {
        /* 战斗心得's resource wording is "experience +20%", not another
         * multiplier. Apply its bonus to the unmodified monster reward so it
         * remains a separately auditable base-reward increment when an
         * experience card is also active. */
        battleInsightBonusPercent = vm_net_mock_role_active_battle_exp_bonus_percent(role);
        if (rewardExp != 0)
        {
            expCardMultiplier = vm_net_mock_role_active_exp_card_multiplier(role);
            if (expCardMultiplier > 1)
                rewardExp = vm_net_mock_mul_capped_u32(rewardExp, expCardMultiplier);
            if (battleInsightBonusPercent != 0)
            {
                uint64_t bonus = ((uint64_t)baseRewardExp * battleInsightBonusPercent) / 100u;
                rewardExp = vm_net_mock_add_capped_u32(
                    rewardExp, bonus > 0xffffffffull ? 0xffffffffu : (u32)bonus);
            }
        }
        if (!vm_net_mock_role_consume_monster_reward_units(
                role, rewardUnits, &dailyRewardGranted, &usedRewardUnits) ||
            !dailyRewardGranted)
        {
            printf("[info][network] mock_battle_reward_quota enemy=%u role=%u units=%u used=%u cap=%u action=%s\n",
                   g_vm_net_mock_battle_enemy_id_current, role->roleId,
                   rewardUnits, usedRewardUnits,
                   VM_NET_MOCK_MONSTER_REWARD_DAILY_UNIT_CAP,
                   dailyRewardGranted ? "unexpected" : "money-suppressed");
            /* The player-facing change restores monster EXP without a daily
             * cap. Keep the separately requested money budget intact. */
            baseRewardGold = 0;
        }
    }
    if (rewardExp != baseRewardExp)
    {
        printf("[info][network] mock_battle_exp_modifier enemy=%u role=%u base_exp=%u card_multiplier=%u insight_bonus_percent=%u reward_exp=%u\n",
               g_vm_net_mock_battle_enemy_id_current, role ? role->roleId : 0,
               baseRewardExp, expCardMultiplier, battleInsightBonusPercent,
               rewardExp);
    }
    rewardGold = baseRewardGold;
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
            if (getenv("CBE_BATTLE_CHANGMING_SAN_DROP_RATE") != NULL)
            {
                u32 ratePercent = vm_net_mock_env_u32_if_set(
                    "CBE_BATTLE_CHANGMING_SAN_DROP_RATE", 0);

                if (ratePercent <= 100u)
                {
                    overrideDrop.rateBasisPoints = (u16)(
                        ratePercent *
                        VM_NET_MOCK_DROP_RATE_BASIS_POINTS_PER_PERCENT);
                }
            }
        }
        overrideDrop.itemId = vm_net_mock_env_u32_if_set(
            "CBE_BATTLE_DROP_ITEM_ID", overrideDrop.itemId);
        if (getenv("CBE_BATTLE_DROP_RATE") != NULL)
        {
            u32 ratePercent = vm_net_mock_env_u32_if_set(
                "CBE_BATTLE_DROP_RATE", 0);

            if (ratePercent <= 100u)
            {
                overrideDrop.rateBasisPoints = (u16)(
                    ratePercent *
                    VM_NET_MOCK_DROP_RATE_BASIS_POINTS_PER_PERCENT);
            }
        }
        configuredDrops[0] = overrideDrop;
        configuredDropCount = overrideDrop.itemId != 0 &&
                              overrideDrop.rateBasisPoints != 0 ? 1 : 0;
    }

    for (u8 dropIndex = 0;
         dropIndex < configuredDropCount &&
         resultCount < VM_NET_MOCK_BATTLE_DROP_RESULT_MAX;
         ++dropIndex)
    {
        const vm_net_mock_monster_drop *configured =
            &configuredDrops[dropIndex];
        u32 rolledDropCount = 0;
        u32 grantedCount = 0;
        u32 taskMaterialRemaining = 0;
        u16 grantedSeq = 0;
        bool dropIsTaskMaterial = false;
        bool dropPolicyOk = false;
        bool dropEligible = false;
        bool dropRollHit = false;

        if (configured->itemId != 0 && configured->rateBasisPoints != 0 &&
            configured->rateBasisPoints <=
                VM_NET_MOCK_DROP_RATE_BASIS_POINTS_MAX && role != NULL)
        {
            dropPolicyOk = vm_net_mock_task_material_drop_policy(
                role->roleId, configured->itemId, &dropIsTaskMaterial,
                &taskMaterialRemaining);
            dropEligible = dropPolicyOk &&
                           (!dropIsTaskMaterial || taskMaterialRemaining != 0);
        }
        if (dropEligible)
        {
            rolledDropCount = vm_net_mock_battle_drop_count_for_battle(
                configured->rateBasisPoints, enemyCount);
            dropRollHit = rolledDropCount != 0;
            grantedCount = rolledDropCount;
            if (dropIsTaskMaterial && grantedCount > taskMaterialRemaining)
                grantedCount = taskMaterialRemaining;
        }
        printf("[info][network] mock_battle_drop_gate enemy=%u role=%u slot=%u item=%u rate_bp=%u "
               "task_material=%u remaining=%u policy=%s eligible=%u rolls=%u "
               "roll_hit=%u quantity_multiplier=%u rolled=%u grant=%u\n",
               g_vm_net_mock_battle_enemy_id_current, role ? role->roleId : 0,
               (u32)dropIndex + 1u, configured->itemId,
               configured->rateBasisPoints,
               dropIsTaskMaterial ? 1u : 0u, taskMaterialRemaining,
               dropPolicyOk ? "ok" : "unavailable", dropEligible ? 1u : 0u,
               dropEligible ? 1u : 0u, dropRollHit ? 1u : 0u, enemyCount,
               rolledDropCount, grantedCount);
        if (grantedCount == 0)
        {
            continue;
        }
        if (!vm_net_mock_role_add_backpack_item(configured->itemId, grantedCount,
                                                &grantedSeq))
        {
            vm_net_mock_role_state beforeSale;
            u32 unitSalePrice = 0;
            u32 saleTotal = 0;

            /* A full bag converts only this unreceived reward to copper.  It
             * never frees a row by selling an existing item, and a failed DB
             * write is not treated as a full-bag condition. */
            if (battleInsightBonusPercent == 0 ||
                !vm_net_mock_battle_insight_overflow_drop_requires_sale(
                    role, configured->itemId, grantedCount))
            {
                continue;
            }
            beforeSale = *role;
            if (!vm_net_mock_battle_insight_apply_overflow_drop_sale(
                    role, configured->itemId, grantedCount, &unitSalePrice,
                    &saleTotal))
            {
                *role = beforeSale;
                printf("[warn][network] mock_battle_insight_overflow_drop_sale role=%u drop_item=%u drop_count=%u action=not-sold-unpriced-or-invalid\n",
                       role->roleId, configured->itemId, grantedCount);
                continue;
            }
            if (!vm_net_mock_role_db_save("battle-insight-overflow-drop-sale"))
            {
                *role = beforeSale;
                printf("[error][network] mock_battle_insight_overflow_drop_sale role=%u drop_item=%u drop_count=%u sale=%u action=rollback-persist-failed error=%s\n",
                       role->roleId, configured->itemId, grantedCount, saleTotal,
                       vm_mysql_last_error());
                continue;
            }
            printf("[info][network] mock_battle_insight_overflow_drop_sale role=%u drop_item=%u drop_count=%u unit_sale=%u sale=%u action=drop-sold-backpack-unchanged\n",
                   role->roleId, configured->itemId, grantedCount,
                   unitSalePrice, saleTotal);
            vm_autotest_note("mock_battle_insight_overflow_drop_sale role=%u drop_item=%u drop_count=%u unit_sale=%u sale=%u action=drop-sold-backpack-unchanged\n",
                             role->roleId, configured->itemId, grantedCount,
                             unitSalePrice, saleTotal);
            continue;
        }
        results[resultCount].itemId = configured->itemId;
        results[resultCount].seq = grantedSeq;
        results[resultCount].count = grantedCount;
        ++resultCount;
        vm_net_mock_task_progress_after_battle(
            0, 0,
            configured->itemId, grantedCount);
    }

    g_vm_net_mock_battle_rewarded_serial = g_mockBattleOperateSessionSerial;
    g_vm_net_mock_battle_rewarded_exp = rewardExp;
    memcpy(g_vm_net_mock_battle_rewarded_drops, results, sizeof(results));
    g_vm_net_mock_battle_rewarded_drop_result_count = resultCount;

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
    if (rewardGoldOut)
        *rewardGoldOut = rewardGold;
    return rewardExp;
}

static void vm_net_mock_role_apply_battle_settlement(u32 hp, u32 mp,
                                                     u32 rewardExp, u32 rewardGold,
                                                     u32 *lastExpOut, u32 *curExpOut,
                                                     u32 *percentExpOut, u32 *levelOut,
                                                     u32 *goldOut, u32 *hpOut, u32 *mpOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL)
        return;
    vm_net_mock_role_sync_derived_vitals(role);
    if (hp > role->hpMax)
        hp = role->hpMax;
    if (mp > role->mpMax)
        mp = role->mpMax;
    role->hp = hp;
    role->mp = mp;
    vm_net_mock_role_add_exp(role, rewardExp);
    role->money = (0xffffffffu - role->money < rewardGold) ? 0xffffffffu : role->money + rewardGold;
    vm_net_mock_role_normalize(role);
    vm_net_mock_role_db_save("battle-settle");

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
    u32 roleMp = role ? role->mp : VM_NET_MOCK_ROLE_DEFAULT_MP;
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
    bool rewardGranted = false;
    /* A shared party victory is not invalidated because this particular
     * observer was knocked out earlier in the same battle.  Preserve its
     * actual zero HP, but settle the victory/reward once under its own role
     * state.  Solo callers keep the normal living-player requirement. */
    bool victory = g_mockBattleEnemyHpCurrent == 0 &&
                   (forceTeamVictory || roleHp > 0);
    /* Automatic MP recovery uses the same persistent/displayed-state contract
     * as every other victory. */
    u32 recoverMp = vm_net_mock_battle_recover_mp_value();
    bool mpRecoveryApplied = false;

    if (role == NULL)
        return;
    if (victory)
    {
        rewardExp = vm_net_mock_battle_grant_reward_once(&dropItemId,
                                                         &dropSeq,
                                                         &dropCount,
                                                         &dropGranted,
                                                         &rewardGranted,
                                                         &rewardGold);
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
    vm_autotest_note("mock_battle_terminal_save reason=%s enemy=%u enemies=%u victory=%u team_victory=%u reward_claimed=%u apply_exp=%u gold=%u total_exp=%u level=%u hp=%u mp=%u recover_mp=%u recovered=%u drop=%u seq=%u count=%u\n",
                     reason ? reason : "terminal",
                     g_vm_net_mock_battle_enemy_id_current,
                     vm_net_mock_battle_enemy_count_current(),
                      victory ? 1 : 0,
                      forceTeamVictory ? 1 : 0,
                      rewardGranted ? 1 : 0,
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
    vm_net_mock_role_db_save(reason ? reason : "battle-state");
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

    /* 801 restores HP only.  Its DSH MP effect is zero, so preserve current
     * MP instead of turning the shop purchase into an undocumented full heal. */
    role->hp = role->hpMax;
    g_mockBattleRoleHpCurrent = role->hp;
    g_mockBattleRoleHpMax = role->hpMax;
    g_mockBattleRoleMpCurrent = role->mp;
    g_mockBattleRoleMpMax = role->mpMax;
    vm_net_mock_role_db_save("battle-revival-stone");

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

static u32 vm_net_mock_role_apply_death_penalty(const char *reason,
                                                u32 *expPenaltyOut,
                                                u32 *moneyPenaltyOut,
                                                u32 *reviveMpOut,
                                                char *sceneOut,
                                                size_t sceneOutCap,
                                                u16 *xOut,
                                                u16 *yOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    char sourceScene[64];
    char nearestTownScene[64];
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
    u32 nextLevelStartExp = 0;
    u32 levelExpRequired = 0;
    u32 availableLevelExp = 0;
    u32 expPenalty = 0;
    u32 moneyPenalty = 0;
    u32 reviveHp = 0;
    u32 reviveMp = 0;

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
    memset(nearestTownScene, 0, sizeof(nearestTownScene));
    if (vm_net_mock_scene_name_is_safe(role->scene))
        snprintf(sourceScene, sizeof(sourceScene), "%s", role->scene);

    /* Ordinary death returns to the nearest authored local safe scene, then
     * to a town centre only when its local component has none. Keep an
     * unresolved source explicit rather than silently using the bootstrap map. */
    if (sourceScene[0] != 0 && vm_net_mock_resolve_nearest_safe_respawn(
            sourceScene, nearestTownScene, sizeof(nearestTownScene),
            &respawnX, &respawnY, &sourceSmapRow, &targetSmapRow,
            &respawnDistance, &respawnRoute))
    {
        respawnScene = nearestTownScene;
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
        (void)vm_net_mock_adjust_recovery_landing_to_map_safe(respawnScene,
                                                               &respawnX, &respawnY);
        printf("[error][network] mock_death_respawn_nearest_town_unresolved source_scene=%s action=keep-current-scene reason=sMap-wMap-or-SCE-data\n",
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
        (void)vm_net_mock_adjust_recovery_landing_to_map_safe(respawnScene,
                                                               &respawnX, &respawnY);
        printf("[error][network] mock_death_respawn_nearest_town_unresolved source_scene=- action=initial-scene reason=invalid-role-scene\n");
    }

    vm_net_mock_role_sync_derived_vitals(role);
    expBefore = role->exp;
    levelBefore = vm_net_mock_role_level_from_exp(expBefore);
    /* EXP is stored as a cumulative value.  The amount needed for the
     * current upgrade is therefore the interval from this level's threshold
     * to the next one, not the accumulated total.  Keep the post-penalty EXP
     * at or above this level's threshold: ordinary revival may reset progress
     * but must never reduce the character's level. */
    levelStartExp = vm_net_mock_role_level_start_exp(levelBefore);
    nextLevelStartExp = vm_net_mock_role_level_start_exp(levelBefore + 1);
    if (levelStartExp != 0xffffffffu &&
        nextLevelStartExp != 0xffffffffu &&
        nextLevelStartExp > levelStartExp)
    {
        levelExpRequired = nextLevelStartExp - levelStartExp;
        expPenalty = vm_net_mock_percent_ceil_u32(
            levelExpRequired, VM_NET_MOCK_ROLE_DEATH_EXP_PENALTY_PERCENT);
        availableLevelExp = expBefore > levelStartExp ? expBefore - levelStartExp : 0;
        if (expPenalty > availableLevelExp)
            expPenalty = availableLevelExp;
    }
    role->exp = expBefore - expPenalty;
    role->level = vm_net_mock_role_level_from_exp(role->exp);
    moneyPenalty = vm_net_mock_percent_ceil_u32(role->money,
                                               VM_NET_MOCK_ROLE_DEATH_MONEY_PENALTY_PERCENT);
    role->money -= moneyPenalty;

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
    vm_net_mock_role_db_save(reason ? reason : "battle-death");

    g_mockBattleRoleHpCurrent = role->hp;
    g_mockBattleRoleHpMax = role->hpMax;
    g_mockBattleRoleMpCurrent = role->mp;
    g_mockBattleRoleMpMax = role->mpMax;

    printf("[info][network] mock_death_penalty reason=%s level=%u->%u exp=%u->%u level_exp_required=%u exp_penalty=%u money_penalty=%u respawn_scene=%s source_smap=%u target_smap=%u route=%s hops=%u pos=(%u,%u)\n",
           reason ? reason : "battle-death", levelBefore, role->level,
           expBefore, role->exp, levelExpRequired, expPenalty, moneyPenalty, role->scene,
           sourceSmapRow, targetSmapRow, respawnRoute ? respawnRoute : "-",
           respawnDistance, role->x, role->y);
    vm_autotest_note("mock_death_penalty reason=%s level=%u->%u exp=%u->%u level_exp_required=%u exp_penalty=%u money_penalty=%u respawn_scene=%s source_smap=%u target_smap=%u route=%s hops=%u pos=(%u,%u) evidence=sMap.dsh/wMap.dsh(monster-level=none)/SCE\n",
                     reason ? reason : "battle-death", levelBefore, role->level,
                     expBefore, role->exp, levelExpRequired, expPenalty, moneyPenalty, role->scene,
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

static const char *vm_mock_service_active_transient_instance_scene(void);
static bool vm_mock_service_active_transient_instance_position(u16 *xOut, u16 *yOut);
static bool vm_mock_service_active_transient_instance_begin(const char *scene,
                                                            u16 x, u16 y,
                                                            const char *reason);
static bool vm_mock_service_active_transient_instance_update_position(
    const char *scene, u16 x, u16 y, const char *reason);
static void vm_mock_service_active_transient_instance_clear_if_departing(
    const char *scene, const char *reason);

static void vm_net_mock_save_player_pos_state(const char *scene, u16 x, u16 y, const char *reason)
{
    char runtimeScene[64];
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *transientScene = NULL;
    if (x == 0 || y == 0)
        return;

    /* A scene transition can name an SCE that the server has not yet resolved
     * locally.  Its exact key remains authoritative for this durable position;
     * substituting a runtime/default scene here changes the next 16/2 target. */
    if (!vm_net_mock_scene_name_is_persistable(scene))
    {
        transientScene = vm_mock_service_active_transient_instance_scene();
        if (transientScene != NULL)
            scene = transientScene;
        else if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
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
    if (vm_mock_service_active_transient_instance_update_position(scene, x, y,
                                                                   reason))
    {
        return;
    }
    vm_mock_service_active_transient_instance_clear_if_departing(scene, reason);
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

const char *vm_net_mock_current_scene_name(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    const char *overrideName = vm_net_mock_env_str("CBE_SCENE_KEY", "");
    const char *transientScene = vm_mock_service_active_transient_instance_scene();
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
    /* An authenticated role owns the scene identity. A legacy row that has
     * not been repaired from sMap.dsh must stay visible as unresolved here;
     * returning the bootstrap scene would silently turn a data error into a
     * cross-map move. Downstream scene handlers require an exact *.sce key
     * and reject that unresolved value. */
    if (transientScene != NULL)
        return transientScene;
    if (role != NULL && vm_net_mock_scene_name_is_download_key(role->scene))
        return role->scene;
    if (overrideName != NULL && vm_net_mock_scene_name_is_persistable(overrideName))
        return overrideName;
    if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
        return runtimeScene;
    return vm_net_mock_default_scene_name();
}

u16 vm_net_mock_scene_spawn_x(void)
{
    u16 transientX = 0;

    if (getenv("CBE_SCENE_POS_X") != NULL)
        return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_X", VM_NET_MOCK_ROLE_INITIAL_X);
    if (vm_mock_service_active_transient_instance_position(&transientX, NULL))
        return transientX;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role != NULL && role->x != 0)
        return role->x;
    return VM_NET_MOCK_ROLE_INITIAL_X;
}

u16 vm_net_mock_scene_spawn_y(void)
{
    u16 transientY = 0;

    if (getenv("CBE_SCENE_POS_Y") != NULL)
        return (u16)vm_net_mock_env_u32("CBE_SCENE_POS_Y", VM_NET_MOCK_ROLE_INITIAL_Y);
    if (vm_mock_service_active_transient_instance_position(NULL, &transientY))
        return transientY;
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
     * This key is copied by parse_actorinfo_response() into R9+0x5E46, then
     * `LoadSceneRes(0x0103130A)` passes it to
     * `LoadMapDataSheet(0x0103581E, mode=4)`.  The latter uses an exact
     * `sMap.dsh` map-name lookup to initialise the world-map current-node
     * controller.  A response-only `cNN... .sce` -> `cNN...` rewrite therefore
     * leaves the actor in the correct scene but keeps the previous map marker
     * (normally Penglai).  Preserve the durable scene resource key byte-for-
     * byte here.  Every downstream scene comparison is strict as well, so the
     * resource key remains one complete identity from persistence to UI.
     */
    if (overrideName != NULL && vm_net_mock_scene_name_is_persistable(overrideName))
        return overrideName;
    if (role != NULL && vm_net_mock_scene_name_is_persistable(role->scene))
        return role->scene;
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

    if (!vm_net_mock_scene_name_is_persistable(sceneName))
        return false;
    u32 posInfoLen = vm_net_mock_build_pos_info(posInfo, sizeof(posInfo), spawnX, spawnY);
    if (posInfoLen == 0)
        return false;
    if (includeResult && !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (includeType && !vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "scene", sceneName))
        return false;
    return vm_net_mock_put_object_entry(out, outCap, pos, "posinfo", posInfo, (u16)posInfoLen);
}

static bool vm_net_mock_put_scene_ack_without_posinfo(u8 *out, u32 outCap, u32 *pos,
                                                      u8 requestType, const char *sceneName)
{
    if (!vm_net_mock_scene_name_is_persistable(sceneName))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", requestType))
        return false;
    return vm_net_mock_put_object_string(out, outCap, pos, "scene", sceneName);
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
/* A role-select can create its first scene shell before a stale target SCE
 * completes WT18/7. Keep one native mmGame 16/2(result=1) direct-enter
 * response scoped to that exact install and its first subsequent WT25/5. */
static vm_net_mock_scene_change_target g_vm_net_mock_startup_sce_enter_target;
static bool g_vm_net_mock_startup_sce_enter_pending = false;
static u32 g_vm_net_mock_startup_sce_enter_install_generation = 0;
static u32 g_vm_net_mock_startup_sce_enter_armed_tick = 0;
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
    u32 mockBattleOperateTurnCounter;
    u8 mockBattleOperateSessionArmed;
    u8 mockBattleAutoEnabled;
    u8 mockBattleAutoLastOperationValid;
    u32 mockBattleAutoLastOperationRoleId;
    u32 mockBattleAutoLastOperationIndex;
    u32 mockBattleAutoLastOperationOperate;
    u32 mockBattleTerminalCloseNotBeforeTick;
    u8 mockBattleOperateSessionFinished;
    u8 mockBattlePendingEnemyTurn;
    u8 mockBattleAwaitingSettlement;
    u8 mockBattleSceneMonsterStartActive;
    u32 mockBattleRoleHpCurrent;
    u32 mockBattleRoleHpMax;
    u32 mockBattleRoleMpCurrent;
    u32 mockBattleRoleMpMax;
    u8 mockBattleEnemyCountCurrent;
    u32 mockBattleEnemyHpSlots[3];
    u32 mockBattleEnemyHpMaxSlots[3];
    u32 mockBattleEnemyHpCurrent;
    u32 mockBattleEnemyHpMax;

    u8 netMockTitleServerListPending;
    u8 netMockTitleServerSelectConfirmed;
    u32 netMockBackpackGridSeededRoleId;
    u32 netMockBackpackGridReseedPendingRoleId;
    u8 netMockShop17ListPending;
    u8 netMockShopCatalogDeliveredBeforeActorQuery;
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

    char sceneMoveinfoNpcPendingScene[64];
    bool sceneMoveinfoNpcPending;
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
    bool lastSceneChangeFromActorOtherPortal;
    u8 lastSceneChangeFb4Type;

    vm_net_mock_scene_change_target lastCompletedSceneChangeTarget;
    bool lastCompletedSceneChangeTargetValid;
    u32 lastCompletedSceneChangeTick;
    bool titleRoleSceneFollowupPending;
    vm_net_mock_scene_change_target startupSceEnterTarget;
    bool startupSceEnterPending;
    u32 startupSceEnterInstallGeneration;
    u32 startupSceEnterArmedTick;

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
static u32 g_vm_mock_service_active_client_id = 0;

const char *vm_mock_service_active_account_id(void)
{
    return g_vm_mock_service_active_account_id;
}

bool vm_mock_service_has_active_account(void)
{
    return g_vm_mock_service_active_account != NULL;
}

void vm_mock_service_guild_set_selected(u32 guildId)
{
    if (g_vm_mock_service_active_account != NULL)
        g_vm_mock_service_active_account->selectedGuildId = guildId;
}

void vm_mock_service_guild_clear_pending_create(void)
{
    if (g_vm_mock_service_active_account == NULL)
        return;
    g_vm_mock_service_active_account->pendingGuildCreateNameValid = false;
    g_vm_mock_service_active_account->pendingGuildCreateName[0] = 0;
}

bool vm_mock_service_guild_set_pending_create_name(const char *name)
{
    if (g_vm_mock_service_active_account == NULL || name == NULL)
        return false;
    snprintf(g_vm_mock_service_active_account->pendingGuildCreateName,
             sizeof(g_vm_mock_service_active_account->pendingGuildCreateName),
             "%s", name);
    g_vm_mock_service_active_account->pendingGuildCreateNameValid = true;
    return true;
}

bool vm_mock_service_guild_pending_create_name_matches(const char *name)
{
    return g_vm_mock_service_active_account != NULL && name != NULL &&
           g_vm_mock_service_active_account->pendingGuildCreateNameValid &&
           strcmp(name, g_vm_mock_service_active_account->pendingGuildCreateName) == 0;
}

u32 vm_mock_service_active_client_id(void)
{
    return g_vm_mock_service_active_client_id;
}

enum
{
    VM_MOCK_SERVICE_PEER_SYNC_MAX = 16,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX = 4,
    VM_MOCK_SERVICE_CHAT_NOTICE_MAX = 64,
    VM_MOCK_SERVICE_CHAT_POLL_MAX = 4,
    VM_MOCK_SERVICE_WORLD_CHAT_HISTORY_MAX = 30,
    VM_MOCK_CHAT_MESSAGE_MAX_BYTES = 79,
    VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX = 10,
    VM_MOCK_SERVICE_TEAM_MAX = 16,
    VM_MOCK_SERVICE_TEAM_MEMBER_MAX = 3,
    VM_MOCK_SERVICE_TEAM_BATTLE_EVENT_MAX = 8,
    VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX = 2048,
    VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX = 512,
    VM_MOCK_SERVICE_DUEL_MAX = 16,
    VM_MOCK_SERVICE_DUEL_EVENT_MAX = 8,
    VM_MOCK_SERVICE_TRADE_MAX = 16
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

typedef struct
{
    u8 type;
    u8 result;
    u32 sourceClientId;
    u32 sourceRoleId;
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
    char message[VM_MOCK_CHAT_MESSAGE_MAX_BYTES + 1];
    u32 queuedTick;
} vm_mock_service_chat_notice;

typedef struct
{
    bool valid;
    bool terminalVictory;
    u32 serial;
    u32 sourceClientId;
    u8 deliveredMask;
    u16 objectLen;
    u8 objectData[VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX];
} vm_mock_service_team_battle_event;

typedef struct
{
    bool valid;
    /* The native mmBattle type-1 playback path consumes the companion
     * teaminfo blob before actioninfo.  Keep whether this queued action
     * required that field so the round merger can preserve it when the last
     * party member releases the shared action list. */
    bool includesTeamInfo;
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
    u32 damage;
    u32 sourceMpAfter;
    u32 targetHpAfter;
    bool terminal;
} vm_mock_service_duel_action;

typedef struct
{
    bool valid;
    u32 serial;
    u32 submitSerial;
    u32 index;
    u32 operate;
} vm_mock_service_duel_intent;

typedef struct
{
    bool valid;
    bool terminal;
    u32 serial;
    u8 deliveredMask;
    u8 actionCount;
    u32 hpAfter[2];
    u32 mpAfter[2];
    vm_mock_service_duel_action actions[2];
} vm_mock_service_duel_event;

struct vm_mock_service_trade
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
};

enum
{
    VM_MOCK_SERVICE_TASK_OFFER_CONTEXT_MAX = 10,
    VM_MOCK_SERVICE_TASK_INTERACTION_OFFER = 1,
    VM_MOCK_SERVICE_TASK_INTERACTION_SUBMIT = 2,
    /* Stored by the server-side NPC binding.  Value 1 is deliberately the
     * legacy meaning of `repeatable=1`, so existing configured NPCs keep
     * allowing immediate re-acceptance after this feature is deployed. */
    VM_NET_MOCK_TASK_REPEAT_NEVER = 0,
    VM_NET_MOCK_TASK_REPEAT_UNLIMITED = 1,
    VM_NET_MOCK_TASK_REPEAT_DAILY = 2,
    VM_NET_MOCK_TASK_REPEAT_WEEKLY = 3,
    VM_NET_MOCK_TASK_REPEAT_MONTHLY = 4
};

typedef struct
{
    u32 roleId;
    u32 taskId;
    u32 actorId;
    bool repeatable;
    u8 repeatPolicy;
    u8 interaction;
    char scene[64];
} vm_mock_service_task_offer_context;

/* A service follow-up (26/1) only contains a private menu value.  Bind it to
 * the actor that produced the preceding native dialog so merchandise and
 * prices cannot bleed between NPCs or clients. */
struct vm_mock_service_npc_context
{
    bool active;
    u32 roleId;
    u32 actorId;
    /* Only the action-1 services actually emitted for this exact NPC dialog
     * are authorized for the private type=2 follow-up request. */
    u32 serviceMask;
    char scene[64];
};

enum
{
    VM_MOCK_SERVICE_NPC_TRANSACTION_NONE = 0,
    VM_MOCK_SERVICE_NPC_TRANSACTION_BUY = 1,
    VM_MOCK_SERVICE_NPC_TRANSACTION_SELL = 2,
    VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_LEARN = 3,
    VM_MOCK_SERVICE_NPC_TRANSACTION_SKILL_FORGET = 4,
    /* A blacksmith synthesis reserves the selected source crystal, requested
     * output quantity, and total material count until action-1 confirmation. */
    VM_MOCK_SERVICE_NPC_TRANSACTION_CRYSTAL_SYNTHESIS = 5,
    /* The quote stores the authoritative candidate count in `selector` and
     * total copper in `quotedPrice`; confirmation rechecks both before the
     * one role transaction removes any quality-zero backpack equipment. */
    VM_MOCK_SERVICE_NPC_TRANSACTION_SELL_QUALITY_ZERO = 6
};

/* A 26/1 action=1 request only carries a private menu value.  Retain the
 * selected merchandise or skill server-side until the immediate confirmation
 * request, then consume this context before any durable role mutation. */
typedef struct
{
    bool active;
    u8 kind;
    u32 roleId;
    u32 actorId;
    u32 serviceMask;
    u32 itemId;
    u16 backpackSeq;
    u32 selector;
    u32 page;
    u32 quotedPrice;
    char scene[64];
} vm_mock_service_npc_transaction_context;

typedef struct vm_mock_service_client_session
{
    u32 clientId;
    char accountId[64];
    /* Same-role return can refresh the backpack grid through the later natural
     * type-2/type-3 requests.  It is deliberately distinct from the selected
     * role's first item-manager construction below. */
    u32 backpackFullBootstrapRoleId;
    u8 backpackFullBootstrapStage;
    /* The selected-role group's first reply owns the native equipped-item
     * construction path.  The marker is session- and role-bound, consumed by
     * that one real group request, and is never armed by shop return. */
    u32 initialEquipmentBootstrapRoleId;
    bool initialEquipmentBootstrapPending;
    bool roleOnline;
    bool onlinePresenceValid;
    u32 onlineRoleId;
    char onlineRoleName[32];
    char onlineRoleTitle[32];
    char onlineRoleTitleBadge[32];
    u8 onlineJob;
    u8 onlineSex;
    u16 onlineLevel;
    u32 onlineEquippedItemIds[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    u16 onlineEquippedEnhanceLevels[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    vm_net_mock_equipment_enhance_affix_state
        onlineEquippedEnhanceAffixes[VM_NET_MOCK_EQUIP_SLOT_COUNT];
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
    /* A dynamic NPC instance is a session destination reached by WT30/1.
     * It may service scene traffic while connected, but it must never replace
     * the role row used by the next ActorInfo bootstrap. */
    bool transientInstanceActive;
    char transientInstanceScene[64];
    u16 transientInstanceX;
    u16 transientInstanceY;
    u32 transientInstanceStartedTick;
    u32 transientInstanceTimerMinutes;
    u32 transientInstanceTimerStartedMs;
    /* Preserve the durable world position from immediately before an NPC
     * instance enters its temporary scene. A timer expiry may only return to
     * this distinct, server-owned anchor through the existing 30/1 path. */
    char transientInstanceReturnScene[64];
    u16 transientInstanceReturnX;
    u16 transientInstanceReturnY;
    /* Expiry makes the transient scene inactive immediately, but its existing
     * 30/1 scene-enter response still has one client-owned resource completion
     * to finish. Keep only that target identity until WT6/1 serializes the
     * matching FB + 30/2 completion; do not retain the expired timer or scene
     * as an active instance. */
    bool transientInstanceExpiryExitCompletionPending;
    char transientInstanceExpiryExitScene[64];
    u16 transientInstanceExpiryExitX;
    u16 transientInstanceExpiryExitY;
    /* The CBE explicitly asks for 27/11 once more after the completed
     * 30/1 -> 6/1 transition.  An expiry return needs that parser-safe
     * request to reseed the world-scene NPC directory once. */
    bool transientInstanceExpiryExitNpcReseedPending;
    char transientInstanceExpiryExitNpcReseedScene[64];
    /* A deadline may occur while the CBE still owns a battle screen.  Keep
     * the expired transient session alive only until that native battle
     * lifecycle returns to the scene; do not inject 30/1 across it. */
    bool transientInstanceExpiryExitAwaitingBattleClose;
    bool shopSceneNpcReseedPending;
    /* 1 = real shop scene return (30/2 may be required), 2 = fresh mmGame
     * bootstrap only (replay 27/11 without re-entering the scene). */
    u8 shopSceneNpcReseedMode;
    char shopSceneNpcReseedScene[64];
    /* mmShop -> mmGame first delivers a coordinate-bearing 30/2 result, then
     * immediately emits the regular scene post-enter combo.  That combo can
     * carry the SCE default landing point even though it is not a teleport.
     * Keep the authoritative return coordinate for that one completion. */
    bool shopSceneReturnPostEnterPending;
    char shopSceneReturnPostEnterScene[64];
    u16 shopSceneReturnPostEnterX;
    u16 shopSceneReturnPostEnterY;
    bool taskPromptRefreshPending;
    char taskPromptRefreshScene[64];
    /* 827's 7/16 response opens the quantity path.  Its separate 7/17
     * request carries the confirmed count, so retain the selected stack,
     * authorized maximum, and an exact committed count for retry replay. */
    bool practisePill17FollowupActive;
    u16 practisePill17FollowupSeq;
    u32 practisePill17FollowupRoleId;
    u32 practisePill17FollowupMaxUse;
    u32 practisePill17FollowupCommittedUse;
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
    vm_mock_service_social_notice socialNotices[VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX];
    vm_mock_service_chat_notice chatNotices[VM_MOCK_SERVICE_CHAT_NOTICE_MAX];
    u8 chatNoticeHead;
    u8 chatNoticeCount;
    bool systemWelcomeQueued;
    bool worldChatHistoryQueued;
    bool friendInviteReplyActive;
    u32 friendInviteSourceClientId;
    u32 friendInviteSourceRoleId;
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
    /* An arena 30/9 prompt has no room id in its eventual 30/10 reply.  The
     * source can be the remote challenger or the local room member after the
     * task-hall has returned to scene.  Bind that reply to this exact prompt
     * so a common scene-channel confirmation cannot be claimed by a room. */
    bool arenaChallengeReplyActive;
    u32 arenaChallengeSourceRoleId;
    /* The room-list 30/8 handler first closes task-hall.  Only its following
     * scene poll may emit the local 30/9 prompt, matching the native instance
     * challenge context in which the mmGame callback can arm battle entry. */
    bool arenaChallengeInitiatorPromptPending;
    u32 pendingTeamBattleSerial;
    /* HandleChallengeResponse(0x010395AA) first consumes 30/9 and its
     * confirmation callback sends 30/10 {agree}.  Keep the target on the
     * service session because 30/10 carries no actor/enemy fields. */
    bool instanceChallengePending;
    bool instanceChallengeBattlePending;
    bool instanceChallengeDirectPending;
    /* action13 can either address a current-scene kind-3 monster (4/5) or
     * begin an isolated instance encounter (4/10).  Retain that origin until
     * the confirmation-owned battle delivery has completed. */
    bool instanceChallengeDirectSceneMonster;
    u32 instanceChallengeActorId;
    u32 instanceChallengeEnemyId;
    /* The action13 packet carries the client-selected current-scene node, but
     * no coordinate.  Preserve that identity through 30/9 -> 30/10 so the
     * later 4/5 can address the same live node table. */
    u32 instanceChallengeSceneIndex;
    u16 instanceChallengeX;
    u16 instanceChallengeY;
    u32 instanceChallengeTick;
    char instanceChallengeScene[64];
    /* The SCE loader first creates static placements, then 27/11 appends the
     * selected NPC rows.  Keep the exact emitted count per visible scene so
     * a later 4/5 battle start can address the same client-owned node table
     * rather than recomputing a role-dependent catalog. */
    u8 sceneNpcNodeCount;
    char sceneNpcNodeScene[64];
    /* Scene hangup is an online-session control, not durable role data. */
    bool sceneHangupEnabled;
    bool sceneHangupRestartPending;
    u32 sceneHangupBattleSessionSerial;
    u32 sceneHangupRestartNotBeforeTick;
    u16 sceneHangupCompletedBattles;
    /* The session-owned ceiling is 64 normally or 200 with Battle Insight.
     * It is a snapshot because 4/7.combatinfo redraws the native panel after
     * every completed battle. */
    u16 sceneHangupMaxBattles;
    u32 sceneHangupTotalExp;
    u32 sceneHangupTotalGold;
    u32 sceneHangupTotalHpRecovered;
    u32 sceneHangupTotalMpRecovered;
    u32 sceneHangupLastAccountedBattleSerial;
    char sceneHangupScene[64];
    /* The native action=4 task path carries only task_id after the NPC dialog.
     * Retain the server-observed offer/submit source so a later 6/11 accept or
     * 6/4 submit cannot silently lose the NPC that authorized it. */
    vm_mock_service_task_offer_context
        taskOfferContexts[VM_MOCK_SERVICE_TASK_OFFER_CONTEXT_MAX];
    vm_mock_service_npc_context npcServiceContext;
    vm_mock_service_npc_transaction_context npcTransactionContext;
    char scenePendingScene[64];
    vm_mock_service_peer_sync peerSync[VM_MOCK_SERVICE_PEER_SYNC_MAX];
    struct vm_mock_service_client_session *next;
} vm_mock_service_client_session;

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
    vm_net_mock_battle_enemy_effect battleEnemyEffects[3];
    u8 battleRoundActedMask;
    u32 battleRoundSerial;
    bool battleRoundTerminalPending;
    u32 battleRoundActionSerial;
    vm_mock_service_team_battle_round_action
        battleRoundActions[VM_MOCK_SERVICE_TEAM_MEMBER_MAX];
    u32 battleActionSerial;
    vm_mock_service_team_battle_event battleEvents[VM_MOCK_SERVICE_TEAM_BATTLE_EVENT_MAX];
} vm_mock_service_team;

enum
{
    VM_MOCK_SERVICE_DUEL_TERMINAL_NONE = 0,
    VM_MOCK_SERVICE_DUEL_TERMINAL_NO_REWARD_CLOSE = 1,
    VM_MOCK_SERVICE_DUEL_TERMINAL_ESCAPE = 2
};

/* A spar is service-local and intentionally keeps its combat HP/MP separate
 * from durable role HP/MP.  A friendly duel must not leave either player dead
 * or consume persistent MP after the battle screen closes. */
struct vm_mock_service_duel
{
    bool active;
    bool finished;
    u32 serial;
    /* Zero is an ordinary nearby-player spar.  A non-zero value records the
     * transient arena room which owns this same validated PvP wire session. */
    u32 arenaRoomId;
    u32 clientIds[2];
    char scene[64];
    u32 hp[2];
    u32 hpMax[2];
    u32 mp[2];
    u32 mpMax[2];
    u8 startPendingMask;
    u8 startedMask;
    u8 roundSubmittedMask;
    u8 terminalPendingMask;
    u8 terminalDeliveredMask;
    u8 terminalExitPendingMask;
    u8 terminalKind;
    u32 terminalNotBeforeTick;
    u32 roundSerial;
    u32 roundIntentSerial;
    u32 actionSerial;
    vm_mock_service_duel_intent intents[2];
    vm_mock_service_duel_event events[VM_MOCK_SERVICE_DUEL_EVENT_MAX];
};

static vm_mock_service_team g_vm_mock_service_teams[VM_MOCK_SERVICE_TEAM_MAX];
static vm_mock_service_duel g_vm_mock_service_duels[VM_MOCK_SERVICE_DUEL_MAX];
static u32 g_vm_mock_service_duel_serial = 0;
static vm_mock_service_trade g_vm_mock_service_trades[VM_MOCK_SERVICE_TRADE_MAX];

u32 vm_mock_service_duel_serial(const vm_mock_service_duel *duel)
{
    return duel != NULL ? duel->serial : 0;
}

enum
{
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

static void vm_mock_service_account_capture(vm_mock_service_account_state *state)
{
    if (state == NULL)
        return;

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
    state->mockBattleOperateTurnCounter = g_mockBattleOperateTurnCounter;
    state->mockBattleOperateSessionArmed = g_mockBattleOperateSessionArmed;
    state->mockBattleAutoEnabled = g_vm_net_mock_battle_auto_enabled;
    state->mockBattleAutoLastOperationValid =
        g_vm_net_mock_battle_auto_last_operation_valid;
    state->mockBattleAutoLastOperationRoleId =
        g_vm_net_mock_battle_auto_last_operation_role_id;
    state->mockBattleAutoLastOperationIndex =
        g_vm_net_mock_battle_auto_last_operation_index;
    state->mockBattleAutoLastOperationOperate =
        g_vm_net_mock_battle_auto_last_operation_operate;
    state->mockBattleTerminalCloseNotBeforeTick =
        g_vm_net_mock_battle_terminal_close_not_before_tick;
    state->mockBattleOperateSessionFinished = g_mockBattleOperateSessionFinished;
    state->mockBattlePendingEnemyTurn = g_mockBattlePendingEnemyTurn;
    state->mockBattleAwaitingSettlement = g_mockBattleAwaitingSettlement;
    state->mockBattleSceneMonsterStartActive = g_mockBattleSceneMonsterStartActive;
    state->mockBattleRoleHpCurrent = g_mockBattleRoleHpCurrent;
    state->mockBattleRoleHpMax = g_mockBattleRoleHpMax;
    state->mockBattleRoleMpCurrent = g_mockBattleRoleMpCurrent;
    state->mockBattleRoleMpMax = g_mockBattleRoleMpMax;
    state->mockBattleEnemyCountCurrent = g_mockBattleEnemyCountCurrent;
    memcpy(state->mockBattleEnemyHpSlots, g_mockBattleEnemyHpSlots, sizeof(state->mockBattleEnemyHpSlots));
    memcpy(state->mockBattleEnemyHpMaxSlots, g_mockBattleEnemyHpMaxSlots, sizeof(state->mockBattleEnemyHpMaxSlots));
    state->mockBattleEnemyHpCurrent = g_mockBattleEnemyHpCurrent;
    state->mockBattleEnemyHpMax = g_mockBattleEnemyHpMax;

    state->netMockTitleServerListPending = g_netMockTitleServerListPending;
    state->netMockTitleServerSelectConfirmed = g_netMockTitleServerSelectConfirmed;
    state->netMockBackpackGridSeededRoleId = g_netMockBackpackGridSeededRoleId;
    state->netMockBackpackGridReseedPendingRoleId =
        g_netMockBackpackGridReseedPendingRoleId;
    state->netMockShop17ListPending = g_netMockShop17ListPending;
    state->netMockShopCatalogDeliveredBeforeActorQuery =
        g_netMockShopCatalogDeliveredBeforeActorQuery;
    state->netMockTitleServerListTick = g_netMockTitleServerListTick;
    state->netMockTitleServerSelectTick = g_netMockTitleServerSelectTick;
    state->netMockTitleSelectedServerId = g_netMockTitleSelectedServerId;
    state->netMockBackpackPreferRoleListAfterShopBuy = g_netMockBackpackPreferRoleListAfterShopBuy;
    state->updateCompletedReenterPending = g_vm_net_mock_update_completed_reenter_pending;
    memcpy(state->updateCompletedName, g_vm_net_mock_update_completed_name, sizeof(state->updateCompletedName));

    state->roleDb = g_vm_net_mock_role_db;
    state->roleDbLoaded = g_vm_net_mock_role_db_loaded;
    state->roleDbValid = g_vm_net_mock_role_db_valid;
    state->rolePositionDirty = g_vm_net_mock_role_position_dirty;

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

    memcpy(state->sceneMoveinfoNpcPendingScene, g_vm_net_mock_scene_moveinfo_npc_pending_scene,
           sizeof(state->sceneMoveinfoNpcPendingScene));
    state->sceneMoveinfoNpcPending = g_vm_net_mock_scene_moveinfo_npc_pending;
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
    state->lastSceneChangeFromActorOtherPortal = g_vm_net_mock_last_scene_change_from_actor_other_portal;
    state->lastSceneChangeFb4Type = g_vm_net_mock_last_scene_change_fb4_type;

    state->lastCompletedSceneChangeTarget = g_vm_net_mock_last_completed_scene_change_target;
    state->lastCompletedSceneChangeTargetValid = g_vm_net_mock_last_completed_scene_change_target_valid;
    state->lastCompletedSceneChangeTick = g_vm_net_mock_last_completed_scene_change_tick;
    state->titleRoleSceneFollowupPending = g_vm_net_mock_title_role_scene_followup_pending;
    state->startupSceEnterTarget = g_vm_net_mock_startup_sce_enter_target;
    state->startupSceEnterPending = g_vm_net_mock_startup_sce_enter_pending;
    state->startupSceEnterInstallGeneration =
        g_vm_net_mock_startup_sce_enter_install_generation;
    state->startupSceEnterArmedTick = g_vm_net_mock_startup_sce_enter_armed_tick;
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
    g_vm_mock_service_active_account = state;
    g_vm_mock_service_active_account_id = state ? state->accountId : NULL;

    if (state == NULL)
        return;

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
    g_mockBattleOperateTurnCounter = state->mockBattleOperateTurnCounter;
    g_mockBattleOperateSessionArmed = state->mockBattleOperateSessionArmed;
    g_vm_net_mock_battle_auto_enabled = state->mockBattleAutoEnabled;
    g_vm_net_mock_battle_auto_last_operation_valid =
        state->mockBattleAutoLastOperationValid;
    g_vm_net_mock_battle_auto_last_operation_role_id =
        state->mockBattleAutoLastOperationRoleId;
    g_vm_net_mock_battle_auto_last_operation_index =
        state->mockBattleAutoLastOperationIndex;
    g_vm_net_mock_battle_auto_last_operation_operate =
        state->mockBattleAutoLastOperationOperate;
    g_vm_net_mock_battle_terminal_close_not_before_tick =
        state->mockBattleTerminalCloseNotBeforeTick;
    g_mockBattleOperateSessionFinished = state->mockBattleOperateSessionFinished;
    g_mockBattlePendingEnemyTurn = state->mockBattlePendingEnemyTurn;
    g_mockBattleAwaitingSettlement = state->mockBattleAwaitingSettlement;
    g_mockBattleSceneMonsterStartActive = state->mockBattleSceneMonsterStartActive;
    g_mockBattleRoleHpCurrent = state->mockBattleRoleHpCurrent;
    g_mockBattleRoleHpMax = state->mockBattleRoleHpMax;
    g_mockBattleRoleMpCurrent = state->mockBattleRoleMpCurrent;
    g_mockBattleRoleMpMax = state->mockBattleRoleMpMax;
    g_mockBattleEnemyCountCurrent = state->mockBattleEnemyCountCurrent;
    memcpy(g_mockBattleEnemyHpSlots, state->mockBattleEnemyHpSlots, sizeof(g_mockBattleEnemyHpSlots));
    memcpy(g_mockBattleEnemyHpMaxSlots, state->mockBattleEnemyHpMaxSlots, sizeof(g_mockBattleEnemyHpMaxSlots));
    g_mockBattleEnemyHpCurrent = state->mockBattleEnemyHpCurrent;
    g_mockBattleEnemyHpMax = state->mockBattleEnemyHpMax;

    g_netMockTitleServerListPending = state->netMockTitleServerListPending;
    g_netMockTitleServerSelectConfirmed = state->netMockTitleServerSelectConfirmed;
    g_netMockBackpackGridSeededRoleId = state->netMockBackpackGridSeededRoleId;
    g_netMockBackpackGridReseedPendingRoleId =
        state->netMockBackpackGridReseedPendingRoleId;
    g_netMockShop17ListPending = state->netMockShop17ListPending;
    g_netMockShopCatalogDeliveredBeforeActorQuery =
        state->netMockShopCatalogDeliveredBeforeActorQuery;
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

    memcpy(g_vm_net_mock_scene_moveinfo_npc_pending_scene, state->sceneMoveinfoNpcPendingScene,
           sizeof(g_vm_net_mock_scene_moveinfo_npc_pending_scene));
    g_vm_net_mock_scene_moveinfo_npc_pending = state->sceneMoveinfoNpcPending;
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
    g_vm_net_mock_last_scene_change_from_actor_other_portal = state->lastSceneChangeFromActorOtherPortal;
    g_vm_net_mock_last_scene_change_fb4_type = state->lastSceneChangeFb4Type;

    g_vm_net_mock_last_completed_scene_change_target = state->lastCompletedSceneChangeTarget;
    g_vm_net_mock_last_completed_scene_change_target_valid = state->lastCompletedSceneChangeTargetValid;
    g_vm_net_mock_last_completed_scene_change_tick = state->lastCompletedSceneChangeTick;
    g_vm_net_mock_title_role_scene_followup_pending = state->titleRoleSceneFollowupPending;
    g_vm_net_mock_startup_sce_enter_target = state->startupSceEnterTarget;
    g_vm_net_mock_startup_sce_enter_pending = state->startupSceEnterPending;
    g_vm_net_mock_startup_sce_enter_install_generation =
        state->startupSceEnterInstallGeneration;
    g_vm_net_mock_startup_sce_enter_armed_tick = state->startupSceEnterArmedTick;
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
}

static vm_mock_service_account_state *vm_mock_service_account_find_or_create(const char *accountId)
{
    const char *resolvedId = (accountId && accountId[0]) ? accountId : NULL;
    vm_mock_service_account_state *state = g_vm_mock_service_accounts;

    if (resolvedId == NULL)
        return NULL;

    while (state)
    {
        if (strcmp(state->accountId, resolvedId) == 0)
            return state;
        state = state->next;
    }

    state = (vm_mock_service_account_state *)calloc(1, sizeof(*state));
    if (state == NULL)
        return NULL;
    vm_mock_service_account_state_init(state, resolvedId);
    state->next = g_vm_mock_service_accounts;
    g_vm_mock_service_accounts = state;
    if (vm_net_mock_verbose_logging_enabled())
        printf("[info][mock-service] account_init id=%s\n", state->accountId);
    return state;
}

/* Account state is a per-live-session working set, not an archival copy of
 * every account that ever touched this process.  All durable role mutations
 * are written by their owning handlers before the offline lifecycle reaches
 * this function.  Keep a state only while at least one live session owns the
 * account (normally exactly one; login takeover enforces that invariant). */
static void vm_mock_service_account_state_release_if_offline(
    const char *accountId, const char *reason)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;
    vm_mock_service_account_state **link = &g_vm_mock_service_accounts;

    if (accountId == NULL || accountId[0] == 0)
        return;
    while (session != NULL)
    {
        if (strcmp(session->accountId, accountId) == 0 &&
            (session->roleOnline || session->onlinePresenceValid ||
             session->sceneVisibleReady))
        {
            return;
        }
        session = session->next;
    }
    while (*link != NULL)
    {
        vm_mock_service_account_state *state = *link;

        if (strcmp(state->accountId, accountId) == 0)
        {
            *link = state->next;
            if (g_vm_mock_service_active_account == state)
            {
                g_vm_mock_service_active_account = NULL;
                g_vm_mock_service_active_account_id = NULL;
            }
            printf("[info][mock-service] account_state_release account=%s reason=%s\n",
                   accountId, reason ? reason : "-");
            free(state);
            return;
        }
        link = &(*link)->next;
    }
}

vm_mock_service_client_session *vm_mock_service_find_client_session(u32 clientId)
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

vm_mock_service_client_session *vm_mock_service_get_active_client_session(void)
{
    if (g_vm_mock_service_active_client_id == 0)
        return NULL;
    return vm_mock_service_find_client_session(g_vm_mock_service_active_client_id);
}

bool vm_mock_service_backpack_full_bootstrap_arm(u32 roleId)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || roleId == 0)
        return false;
    if (session->backpackFullBootstrapStage != 0 &&
        session->backpackFullBootstrapRoleId == roleId)
    {
        return true;
    }
    session->backpackFullBootstrapRoleId = roleId;
    session->backpackFullBootstrapStage = 1;
    printf("[info][network] mock_backpack_full_bootstrap_arm client=%08x role=%u "
           "next=7/7-type2-full-grid\n",
           session->clientId, roleId);
    return true;
}

bool vm_mock_service_backpack_full_bootstrap_matches(u32 roleId, u8 stage)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    return session != NULL && roleId != 0 && stage != 0 &&
           session->backpackFullBootstrapRoleId == roleId &&
           session->backpackFullBootstrapStage == stage;
}

void vm_mock_service_backpack_full_bootstrap_advance(u32 roleId, u8 stage)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || roleId == 0 || stage == 0 ||
        session->backpackFullBootstrapRoleId != roleId ||
        session->backpackFullBootstrapStage != stage)
    {
        return;
    }
    session->backpackFullBootstrapStage = (u8)(stage + 1u);
}

void vm_mock_service_backpack_full_bootstrap_complete(u32 roleId)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || roleId == 0 ||
        session->backpackFullBootstrapRoleId != roleId ||
        session->backpackFullBootstrapStage != 2)
    {
        return;
    }
    printf("[info][network] mock_backpack_full_bootstrap_complete client=%08x role=%u "
           "frames=group+type2+type3\n",
           session->clientId, roleId);
    session->backpackFullBootstrapRoleId = 0;
    session->backpackFullBootstrapStage = 0;
}

void vm_mock_service_initial_equipment_bootstrap_arm(u32 roleId)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || roleId == 0)
        return;

    /* A fresh role selection replaces the CBE item manager.  Any abandoned
     * same-role deferred-grid phase belongs to the prior client lifecycle and
     * must not leak into the selected-role group response. */
    session->backpackFullBootstrapRoleId = 0;
    session->backpackFullBootstrapStage = 0;
    session->initialEquipmentBootstrapRoleId = roleId;
    session->initialEquipmentBootstrapPending = true;
}

bool vm_mock_service_initial_equipment_bootstrap_matches(u32 roleId)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    return session != NULL && roleId != 0 &&
           session->initialEquipmentBootstrapPending &&
           session->initialEquipmentBootstrapRoleId == roleId;
}

void vm_mock_service_initial_equipment_bootstrap_complete(u32 roleId)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || roleId == 0 ||
        !session->initialEquipmentBootstrapPending ||
        session->initialEquipmentBootstrapRoleId != roleId)
    {
        return;
    }

    session->initialEquipmentBootstrapRoleId = 0;
    session->initialEquipmentBootstrapPending = false;
}

bool vm_mock_service_session_get_online_view(
    const vm_mock_service_client_session *session,
    vm_mock_service_online_session_view *viewOut)
{
    if (viewOut != NULL)
        memset(viewOut, 0, sizeof(*viewOut));
    if (session == NULL || viewOut == NULL)
        return false;
    viewOut->roleOnline = session->roleOnline;
    viewOut->onlinePresenceValid = session->onlinePresenceValid;
    viewOut->sceneVisibleReady = session->sceneVisibleReady;
    viewOut->sceneVisiblePending = session->sceneVisiblePending;
    viewOut->arenaChallengeInitiatorPromptPending =
        session->arenaChallengeInitiatorPromptPending;
    viewOut->arenaChallengeReplyActive = session->arenaChallengeReplyActive;
    viewOut->clientId = session->clientId;
    viewOut->onlineRoleId = session->onlineRoleId;
    viewOut->arenaChallengeSourceRoleId =
        session->arenaChallengeSourceRoleId;
    viewOut->onlineJob = session->onlineJob;
    viewOut->onlineSex = session->onlineSex;
    viewOut->onlineLevel = session->onlineLevel;
    viewOut->onlineX = session->onlineX;
    viewOut->onlineY = session->onlineY;
    viewOut->sceneVisibleX = session->sceneVisibleX;
    viewOut->sceneVisibleY = session->sceneVisibleY;
    viewOut->onlineHp = session->onlineHp;
    viewOut->onlineHpMax = session->onlineHpMax;
    viewOut->onlineMp = session->onlineMp;
    viewOut->onlineMpMax = session->onlineMpMax;
    memcpy(viewOut->onlineEquippedItemIds, session->onlineEquippedItemIds,
           sizeof(viewOut->onlineEquippedItemIds));
    memcpy(viewOut->onlineEquippedEnhanceLevels,
           session->onlineEquippedEnhanceLevels,
           sizeof(viewOut->onlineEquippedEnhanceLevels));
    memcpy(viewOut->onlineEquippedEnhanceAffixes,
           session->onlineEquippedEnhanceAffixes,
           sizeof(viewOut->onlineEquippedEnhanceAffixes));
    snprintf(viewOut->onlineRoleName, sizeof(viewOut->onlineRoleName), "%s",
             session->onlineRoleName);
    snprintf(viewOut->onlineRoleTitle, sizeof(viewOut->onlineRoleTitle), "%s",
             session->onlineRoleTitle);
    snprintf(viewOut->onlineRoleTitleBadge,
             sizeof(viewOut->onlineRoleTitleBadge), "%s",
             session->onlineRoleTitleBadge);
    return true;
}

const char *vm_mock_service_session_account_id(
    const vm_mock_service_client_session *session)
{
    return session != NULL ? session->accountId : NULL;
}

bool vm_mock_service_session_get_team_invite_reply_context(
    const vm_mock_service_client_session *session,
    vm_mock_service_team_invite_reply_context *contextOut)
{
    if (contextOut != NULL)
        memset(contextOut, 0, sizeof(*contextOut));
    if (session == NULL || contextOut == NULL)
        return false;
    contextOut->active = session->teamInviteReplyActive;
    contextOut->sourceClientId = session->teamInviteSourceClientId;
    contextOut->sourceWireId = session->teamInviteSourceWireId;
    return true;
}

void vm_mock_service_session_clear_team_invite_reply_context(
    vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->teamInviteReplyActive = false;
    session->teamInviteSourceClientId = 0;
    session->teamInviteSourceWireId = 0;
}

bool vm_mock_service_session_get_spar_invite_reply_context(
    const vm_mock_service_client_session *session,
    vm_mock_service_spar_invite_reply_context *contextOut)
{
    if (contextOut != NULL)
        memset(contextOut, 0, sizeof(*contextOut));
    if (session == NULL || contextOut == NULL)
        return false;
    contextOut->active = session->sparInviteReplyActive;
    contextOut->sourceClientId = session->sparInviteSourceClientId;
    contextOut->sourceWireId = session->sparInviteSourceWireId;
    return true;
}

void vm_mock_service_session_clear_spar_invite_reply_context(
    vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->sparInviteReplyActive = false;
    session->sparInviteSourceClientId = 0;
    session->sparInviteSourceWireId = 0;
}

bool vm_mock_service_session_get_friend_invite_reply_context(
    const vm_mock_service_client_session *session,
    vm_mock_service_friend_invite_reply_context *contextOut)
{
    if (contextOut != NULL)
        memset(contextOut, 0, sizeof(*contextOut));
    if (session == NULL || contextOut == NULL)
        return false;
    contextOut->active = session->friendInviteReplyActive;
    contextOut->sourceClientId = session->friendInviteSourceClientId;
    contextOut->sourceRoleId = session->friendInviteSourceRoleId;
    return true;
}

void vm_mock_service_session_clear_friend_invite_reply_context(
    vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->friendInviteReplyActive = false;
    session->friendInviteSourceClientId = 0;
    session->friendInviteSourceRoleId = 0;
}

bool vm_mock_service_session_get_trade_invite_reply_context(
    const vm_mock_service_client_session *session,
    vm_mock_service_trade_invite_reply_context *contextOut)
{
    if (contextOut != NULL)
        memset(contextOut, 0, sizeof(*contextOut));
    if (session == NULL || contextOut == NULL)
        return false;
    contextOut->active = session->tradeInviteReplyActive;
    contextOut->sourceClientId = session->tradeInviteSourceClientId;
    contextOut->sourceRoleId = session->tradeInviteSourceRoleId;
    return true;
}

void vm_mock_service_session_clear_trade_invite_reply_context(
    vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->tradeInviteReplyActive = false;
    session->tradeInviteSourceClientId = 0;
    session->tradeInviteSourceRoleId = 0;
}

bool vm_mock_service_spar_invite_can_accept(
    const vm_mock_service_client_session *responder,
    const vm_mock_service_client_session *source)
{
    return responder != NULL && source != NULL && source->roleOnline &&
           source->clientId != responder->clientId &&
           responder->sceneVisibleReady &&
           vm_mock_service_session_scene_is_visible(source,
                                                    responder->sceneVisibleScene);
}

void vm_mock_service_session_set_spar_battle_ready_context(
    vm_mock_service_client_session *session, u32 peerClientId, u32 peerWireId)
{
    if (session == NULL)
        return;
    session->sparBattleReadyPending = true;
    session->sparBattlePeerClientId = peerClientId;
    session->sparBattlePeerWireId = peerWireId;
}

bool vm_mock_service_session_get_spar_battle_ready_context(
    const vm_mock_service_client_session *session,
    vm_mock_service_spar_battle_ready_context *contextOut)
{
    if (contextOut != NULL)
        memset(contextOut, 0, sizeof(*contextOut));
    if (session == NULL || contextOut == NULL)
        return false;
    contextOut->active = session->sparBattleReadyPending;
    contextOut->peerClientId = session->sparBattlePeerClientId;
    contextOut->peerWireId = session->sparBattlePeerWireId;
    return true;
}

void vm_mock_service_session_clear_spar_battle_ready_context(
    vm_mock_service_client_session *session)
{
    if (session == NULL)
        return;
    session->sparBattleReadyPending = false;
    session->sparBattlePeerClientId = 0;
    session->sparBattlePeerWireId = 0;
}

bool vm_mock_service_spar_battle_ready_source_is_valid(
    const vm_mock_service_client_session *responder,
    const vm_mock_service_client_session *source)
{
    return responder != NULL && source != NULL && source->roleOnline &&
           vm_mock_service_session_scene_is_visible(source,
                                                    responder->sceneVisibleScene);
}

void vm_mock_service_session_set_arena_challenge_state(
    vm_mock_service_client_session *session, bool initiatorPromptPending,
    bool replyActive, u32 sourceRoleId)
{
    if (session == NULL)
        return;
    session->arenaChallengeInitiatorPromptPending = initiatorPromptPending;
    session->arenaChallengeReplyActive = replyActive;
    session->arenaChallengeSourceRoleId = sourceRoleId;
}

void vm_mock_service_arm_practise_pill17_followup(
    const vm_net_mock_role_state *role, u16 itemSeq, u32 maxUse)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || role == NULL || role->roleId == 0 || itemSeq == 0 ||
        maxUse == 0)
        return;
    session->practisePill17FollowupActive = true;
    session->practisePill17FollowupSeq = itemSeq;
    session->practisePill17FollowupRoleId = role->roleId;
    session->practisePill17FollowupMaxUse = maxUse;
    session->practisePill17FollowupCommittedUse = 0;
    printf("[info][mock-service] practise_pill17_arm client=%08x role=%u seq=%u max_use=%u source=1/7/16-preflight\n",
           session->clientId, role->roleId, (u32)itemSeq, maxUse);
}

void vm_mock_service_clear_practise_pill17_followup(
    const vm_net_mock_role_state *role, u16 itemSeq, const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->practisePill17FollowupActive ||
        (role != NULL && role->roleId != 0 &&
         session->practisePill17FollowupRoleId != role->roleId) ||
        (itemSeq != 0 && session->practisePill17FollowupSeq != itemSeq))
    {
        return;
    }
    printf("[info][mock-service] practise_pill17_clear client=%08x role=%u seq=%u reason=%s\n",
           session->clientId, session->practisePill17FollowupRoleId,
           (u32)session->practisePill17FollowupSeq, reason ? reason : "-");
    session->practisePill17FollowupActive = false;
    session->practisePill17FollowupSeq = 0;
    session->practisePill17FollowupRoleId = 0;
    session->practisePill17FollowupMaxUse = 0;
    session->practisePill17FollowupCommittedUse = 0;
}

bool vm_mock_service_practise_pill17_followup_matches(
    const vm_net_mock_role_state *role, u16 itemSeq, u32 useNum,
    bool *replayOut, bool *rejectedOut)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (replayOut != NULL)
        *replayOut = false;
    if (rejectedOut != NULL)
        *rejectedOut = false;
    if (session == NULL || role == NULL || role->roleId == 0 || itemSeq == 0 ||
        useNum == 0 || !session->practisePill17FollowupActive ||
        session->practisePill17FollowupRoleId != role->roleId ||
        session->practisePill17FollowupSeq != itemSeq)
    {
        return false;
    }
    if (session->practisePill17FollowupCommittedUse != 0)
    {
        if (session->practisePill17FollowupCommittedUse == useNum)
        {
            if (replayOut != NULL)
                *replayOut = true;
        }
        else if (rejectedOut != NULL)
        {
            *rejectedOut = true;
        }
        return true;
    }
    if (useNum > session->practisePill17FollowupMaxUse &&
        rejectedOut != NULL)
    {
        *rejectedOut = true;
    }
    return true;
}

void vm_mock_service_commit_practise_pill17_followup(
    const vm_net_mock_role_state *role, u16 itemSeq, u32 useNum)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || role == NULL || role->roleId == 0 || itemSeq == 0 ||
        useNum == 0 || !session->practisePill17FollowupActive ||
        session->practisePill17FollowupRoleId != role->roleId ||
        session->practisePill17FollowupSeq != itemSeq ||
        session->practisePill17FollowupCommittedUse != 0)
    {
        return;
    }
    session->practisePill17FollowupCommittedUse = useNum;
    printf("[info][mock-service] practise_pill17_commit client=%08x role=%u seq=%u usenum=%u\n",
           session->clientId, role->roleId, (u32)itemSeq, useNum);
}

static const char *vm_mock_service_active_transient_instance_scene(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->transientInstanceActive ||
        !vm_net_mock_scene_name_is_safe(session->transientInstanceScene) ||
        session->transientInstanceX == 0 || session->transientInstanceY == 0)
    {
        return NULL;
    }
    return session->transientInstanceScene;
}

static bool vm_mock_service_active_transient_instance_position(u16 *xOut, u16 *yOut)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->transientInstanceActive ||
        !vm_net_mock_scene_name_is_safe(session->transientInstanceScene) ||
        session->transientInstanceX == 0 || session->transientInstanceY == 0)
    {
        return false;
    }
    if (xOut != NULL)
        *xOut = session->transientInstanceX;
    if (yOut != NULL)
        *yOut = session->transientInstanceY;
    return true;
}

static bool vm_mock_service_active_transient_instance_begin(const char *scene,
                                                            u16 x, u16 y,
                                                            const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (session == NULL || role == NULL ||
        !vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0 ||
        !vm_net_mock_scene_name_is_persistable(role->scene) ||
        role->x == 0 || role->y == 0)
    {
        return false;
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
    session->transientInstanceActive = true;
    snprintf(session->transientInstanceScene,
             sizeof(session->transientInstanceScene), "%s", scene);
    session->transientInstanceX = x;
    session->transientInstanceY = y;
    session->transientInstanceStartedTick = g_schedulerTick;
    session->transientInstanceTimerMinutes = 0;
    session->transientInstanceTimerStartedMs = 0;
    session->transientInstanceExpiryExitCompletionPending = false;
    session->transientInstanceExpiryExitScene[0] = 0;
    session->transientInstanceExpiryExitX = 0;
    session->transientInstanceExpiryExitY = 0;
    session->transientInstanceExpiryExitNpcReseedPending = false;
    session->transientInstanceExpiryExitNpcReseedScene[0] = 0;
    session->transientInstanceExpiryExitAwaitingBattleClose = false;
    snprintf(session->transientInstanceReturnScene,
             sizeof(session->transientInstanceReturnScene), "%s", role->scene);
    session->transientInstanceReturnX = role->x;
    session->transientInstanceReturnY = role->y;
    printf("[info][mock-service] transient_instance_begin client=%08x role=%u scene=%s pos=(%u,%u) durable_anchor=%s@(%u,%u) reason=%s\n",
           session->clientId, role->roleId, session->transientInstanceScene,
           session->transientInstanceX, session->transientInstanceY,
           role->scene, role->x, role->y, reason ? reason : "instance-enter");
    return true;
}

/* Timer state is deliberately session-owned.  The worker invokes this only
 * after the instance-enter builder has accepted the same NPC configuration
 * that produced WT30/1. */
static bool vm_mock_service_active_transient_instance_configure_timer(
    u32 timerMinutes)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->transientInstanceActive ||
        !vm_net_mock_scene_name_is_safe(session->transientInstanceScene) ||
        timerMinutes > VM_NET_MOCK_INSTANCE_TIMER_MAX_MINUTES)
    {
        return false;
    }
    session->transientInstanceTimerMinutes = timerMinutes;
    session->transientInstanceTimerStartedMs = scheduler_get_tick_ms();
    printf("[info][mock-service] transient_instance_timer_begin client=%08x scene=%s minutes=%u\n",
           session->clientId, session->transientInstanceScene, timerMinutes);
    return true;
}

static u32 vm_mock_service_active_transient_instance_timer_remaining_minutes(
    const vm_mock_service_client_session *session, u32 nowMs)
{
    u32 elapsedMinutes = 0;

    if (session == NULL || !session->transientInstanceActive ||
        session->transientInstanceTimerMinutes == 0)
    {
        return 0;
    }
    elapsedMinutes = (nowMs - session->transientInstanceTimerStartedMs) / 60000u;
    return elapsedMinutes >= session->transientInstanceTimerMinutes ? 0 :
           session->transientInstanceTimerMinutes - elapsedMinutes;
}

static void vm_mock_service_active_transient_instance_expiry_exit_completion_clear(
    const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->transientInstanceExpiryExitCompletionPending)
        return;
    printf("[info][mock-service] transient_instance_expiry_exit_completion_clear "
           "client=%08x scene=%s pos=(%u,%u) reason=%s\n",
           session->clientId, session->transientInstanceExpiryExitScene,
           session->transientInstanceExpiryExitX,
           session->transientInstanceExpiryExitY,
           reason ? reason : "-");
    session->transientInstanceExpiryExitCompletionPending = false;
    session->transientInstanceExpiryExitScene[0] = 0;
    session->transientInstanceExpiryExitX = 0;
    session->transientInstanceExpiryExitY = 0;
    session->transientInstanceExpiryExitNpcReseedPending = false;
    session->transientInstanceExpiryExitNpcReseedScene[0] = 0;
    session->transientInstanceExpiryExitAwaitingBattleClose = false;
}

static void vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_clear(
    const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->transientInstanceExpiryExitNpcReseedPending)
        return;
    printf("[info][mock-service] transient_instance_expiry_exit_npc_reseed_clear "
           "client=%08x scene=%s reason=%s\n",
           session->clientId, session->transientInstanceExpiryExitNpcReseedScene,
           reason ? reason : "-");
    session->transientInstanceExpiryExitNpcReseedPending = false;
    session->transientInstanceExpiryExitNpcReseedScene[0] = 0;
}

static bool vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_begin(
    const char *scene)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !vm_net_mock_scene_name_is_persistable(scene))
        return false;
    vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_clear(
        "replaced-by-expiry-exit");
    session->transientInstanceExpiryExitNpcReseedPending = true;
    snprintf(session->transientInstanceExpiryExitNpcReseedScene,
             sizeof(session->transientInstanceExpiryExitNpcReseedScene), "%s",
             scene);
    printf("[info][mock-service] transient_instance_expiry_exit_npc_reseed_begin "
           "client=%08x scene=%s next=WT25/5+27/11\n",
           session->clientId, session->transientInstanceExpiryExitNpcReseedScene);
    return true;
}

static bool vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_matches(
    const char *scene)
{
    const vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    return session != NULL &&
           session->transientInstanceExpiryExitNpcReseedPending &&
           session->transientInstanceExpiryExitNpcReseedScene[0] != 0 &&
           vm_net_mock_scene_names_equal_exact(
               scene, session->transientInstanceExpiryExitNpcReseedScene);
}

static void vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_discard_if_mismatch(
    const vm_net_mock_scene_change_target *target, const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session != NULL && session->transientInstanceExpiryExitNpcReseedPending &&
        (target == NULL || !vm_net_mock_scene_names_equal_exact(
                               target->scene,
                               session->transientInstanceExpiryExitNpcReseedScene)))
    {
        vm_mock_service_active_transient_instance_expiry_exit_npc_reseed_clear(
            reason ? reason : "scene-target-replaced");
    }
}

static bool vm_mock_service_active_transient_instance_expiry_exit_completion_begin(
    const vm_net_mock_scene_change_target *target)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || target == NULL ||
        !vm_net_mock_scene_name_is_persistable(target->scene) ||
        target->x == 0 || target->y == 0)
    {
        return false;
    }
    vm_mock_service_active_transient_instance_expiry_exit_completion_clear(
        "replaced-by-expiry-exit");
    session->transientInstanceExpiryExitCompletionPending = true;
    snprintf(session->transientInstanceExpiryExitScene,
             sizeof(session->transientInstanceExpiryExitScene), "%s",
             target->scene);
    session->transientInstanceExpiryExitX = target->x;
    session->transientInstanceExpiryExitY = target->y;
    printf("[info][mock-service] transient_instance_expiry_exit_completion_begin "
           "client=%08x scene=%s pos=(%u,%u)\n",
           session->clientId, session->transientInstanceExpiryExitScene,
           session->transientInstanceExpiryExitX,
           session->transientInstanceExpiryExitY);
    return true;
}

static bool vm_mock_service_active_transient_instance_expiry_exit_completion_matches(
    const vm_net_mock_scene_change_target *target)
{
    const vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    return target != NULL && session != NULL &&
           session->transientInstanceExpiryExitCompletionPending &&
           session->transientInstanceExpiryExitScene[0] != 0 &&
           session->transientInstanceExpiryExitX != 0 &&
           session->transientInstanceExpiryExitY != 0 &&
           target->x == session->transientInstanceExpiryExitX &&
           target->y == session->transientInstanceExpiryExitY &&
           vm_net_mock_scene_names_equal_exact(
               target->scene, session->transientInstanceExpiryExitScene);
}

static void vm_mock_service_active_transient_instance_expiry_exit_completion_discard_if_mismatch(
    const vm_net_mock_scene_change_target *target, const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session != NULL && session->transientInstanceExpiryExitCompletionPending &&
        !vm_mock_service_active_transient_instance_expiry_exit_completion_matches(
            target))
    {
        vm_mock_service_active_transient_instance_expiry_exit_completion_clear(
            reason ? reason : "scene-target-replaced");
    }
}

static bool vm_mock_service_active_transient_instance_update_position(
    const char *scene, u16 x, u16 y, const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->transientInstanceActive ||
        !vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0 ||
        !vm_net_mock_scene_names_equal_exact(session->transientInstanceScene,
                                             scene))
    {
        return false;
    }
    session->transientInstanceX = x;
    session->transientInstanceY = y;
    if (session->sceneVisibleReady && !session->sceneVisiblePending &&
        vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene, scene))
    {
        session->sceneVisibleX = x;
        session->sceneVisibleY = y;
        session->sceneVisibleTick = g_schedulerTick;
    }
    (void)reason;
    return true;
}

static void vm_mock_service_active_transient_instance_clear_if_departing(
    const char *scene, const char *reason)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->transientInstanceActive ||
        !vm_net_mock_scene_name_is_persistable(scene) ||
        vm_net_mock_scene_names_equal_exact(session->transientInstanceScene,
                                            scene))
    {
        return;
    }
    printf("[info][mock-service] transient_instance_end client=%08x scene=%s pos=(%u,%u) next_scene=%s reason=%s\n",
           session->clientId, session->transientInstanceScene,
           session->transientInstanceX, session->transientInstanceY, scene,
           reason ? reason : "scene-change");
    session->transientInstanceActive = false;
    session->transientInstanceScene[0] = 0;
    session->transientInstanceX = 0;
    session->transientInstanceY = 0;
    session->transientInstanceStartedTick = 0;
    session->transientInstanceTimerMinutes = 0;
    session->transientInstanceTimerStartedMs = 0;
    session->transientInstanceReturnScene[0] = 0;
    session->transientInstanceReturnX = 0;
    session->transientInstanceReturnY = 0;
    session->transientInstanceExpiryExitAwaitingBattleClose = false;
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
        duel->startPendingMask == 0 && duel->terminalPendingMask == 0 &&
        duel->terminalExitPendingMask == 0)
    {
        for (u8 i = 0; i < VM_MOCK_SERVICE_DUEL_EVENT_MAX; ++i)
        {
            if (duel->events[i].valid)
                return;
        }
        printf("[info][mock-service] duel_release serial=%u first=%08x second=%08x\n",
               duel->serial, duel->clientIds[0], duel->clientIds[1]);
        if (duel->arenaRoomId != 0)
            vm_net_mock_arena_on_duel_released(duel->arenaRoomId, duel->serial);
        memset(duel, 0, sizeof(*duel));
    }
}

void vm_mock_service_duel_cancel_for_client(u32 clientId, const char *reason)
{
    int index = -1;
    vm_mock_service_duel *duel = vm_mock_service_duel_find_for_client(clientId,
                                                                      &index);
    u8 peerBit = 0;

    if (duel == NULL || index < 0)
        return;
    peerBit = (u8)(1u << (1 - index));
    if (duel->finished)
    {
        u8 sourceBit = (u8)(1u << index);

        duel->startPendingMask &= (u8)~sourceBit;
        duel->terminalPendingMask &= (u8)~sourceBit;
        duel->terminalExitPendingMask &= (u8)~sourceBit;
        for (u8 i = 0; i < VM_MOCK_SERVICE_DUEL_EVENT_MAX; ++i)
        {
            vm_mock_service_duel_event *event = &duel->events[i];

            if (!event->valid)
                continue;
            event->deliveredMask |= sourceBit;
            if (event->deliveredMask == 3)
                memset(event, 0, sizeof(*event));
        }
        printf("[info][mock-service] duel_terminal_disconnect serial=%u "
               "client=%08x actor=%d terminal=%02x exit=%02x reason=%s\n",
               duel->serial, clientId, index, duel->terminalPendingMask,
               duel->terminalExitPendingMask, reason ? reason : "offline");
        vm_mock_service_duel_release_if_done(duel);
        return;
    }
    printf("[info][mock-service] duel_cancel serial=%u client=%08x peer=%08x "
           "started=%02x reason=%s\n",
           duel->serial, clientId, duel->clientIds[1 - index],
           duel->startedMask, reason ? reason : "cancel");
    duel->finished = true;
    duel->startPendingMask = 0;
    memset(duel->events, 0, sizeof(duel->events));
    duel->terminalPendingMask = (u8)(duel->startedMask & peerBit);
    duel->terminalExitPendingMask = duel->terminalPendingMask;
    duel->terminalKind = VM_MOCK_SERVICE_DUEL_TERMINAL_ESCAPE;
    duel->terminalNotBeforeTick = g_schedulerTick;
    vm_mock_service_duel_release_if_done(duel);
}

static vm_mock_service_team *vm_mock_service_team_find_for_client(u32 clientId);

bool vm_mock_service_duel_get_pending_start(
    const vm_mock_service_client_session *observer,
    vm_mock_service_duel_start_view *viewOut)
{
    vm_mock_service_duel *duel = NULL;
    int observerIndex = -1;
    int peerIndex = -1;
    u8 observerBit = 0;

    if (viewOut != NULL)
        memset(viewOut, 0, sizeof(*viewOut));
    if (observer == NULL || viewOut == NULL)
        return false;
    duel = vm_mock_service_duel_find_for_client(observer->clientId, &observerIndex);
    if (duel == NULL || observerIndex < 0 || observerIndex > 1)
        return false;
    observerBit = (u8)(1u << observerIndex);
    if ((duel->startPendingMask & observerBit) == 0)
        return false;
    peerIndex = 1 - observerIndex;
    viewOut->serial = duel->serial;
    viewOut->peerClientId = duel->clientIds[peerIndex];
    viewOut->observerHp = duel->hp[observerIndex];
    viewOut->observerHpMax = duel->hpMax[observerIndex];
    viewOut->observerMp = duel->mp[observerIndex];
    viewOut->observerMpMax = duel->mpMax[observerIndex];
    viewOut->peerHp = duel->hp[peerIndex];
    viewOut->peerHpMax = duel->hpMax[peerIndex];
    viewOut->peerMp = duel->mp[peerIndex];
    viewOut->peerMpMax = duel->mpMax[peerIndex];
    viewOut->observerIndex = (u8)observerIndex;
    viewOut->arenaRoom = duel->arenaRoomId != 0;
    snprintf(viewOut->scene, sizeof(viewOut->scene), "%s", duel->scene);
    return true;
}

bool vm_mock_service_duel_confirm_start_delivery(
    const vm_mock_service_client_session *observer, u32 serial,
    u8 *startedMaskOut, u8 *pendingMaskOut)
{
    vm_mock_service_duel *duel = NULL;
    int observerIndex = -1;
    u8 observerBit = 0;

    if (startedMaskOut != NULL)
        *startedMaskOut = 0;
    if (pendingMaskOut != NULL)
        *pendingMaskOut = 0;
    if (observer == NULL || serial == 0)
        return false;
    duel = vm_mock_service_duel_find_for_client(observer->clientId, &observerIndex);
    if (duel == NULL || duel->serial != serial || observerIndex < 0 || observerIndex > 1)
        return false;
    observerBit = (u8)(1u << observerIndex);
    if ((duel->startPendingMask & observerBit) == 0)
        return false;
    duel->startPendingMask &= (u8)~observerBit;
    duel->startedMask |= observerBit;
    if (startedMaskOut != NULL)
        *startedMaskOut = duel->startedMask;
    if (pendingMaskOut != NULL)
        *pendingMaskOut = duel->startPendingMask;
    return true;
}

static vm_mock_service_duel *vm_mock_service_duel_begin_ex(
    vm_mock_service_client_session *inviter,
    vm_mock_service_client_session *responder,
    bool requireSameScene, u32 arenaRoomId)
{
    vm_mock_service_duel *slot = NULL;
    vm_mock_service_team *inviterTeam = NULL;
    vm_mock_service_team *responderTeam = NULL;

    if (inviter == NULL || responder == NULL || inviter == responder ||
        inviter->clientId == 0 || responder->clientId == 0 ||
        !inviter->roleOnline || !responder->roleOnline ||
        inviter->onlineRoleId == 0 || responder->onlineRoleId == 0 ||
        !inviter->sceneVisibleReady || !responder->sceneVisibleReady ||
        inviter->sceneVisiblePending || responder->sceneVisiblePending ||
        (requireSameScene &&
         !vm_mock_service_session_scene_is_visible(responder,
                                                   inviter->sceneVisibleScene)) ||
        vm_mock_service_duel_find_for_client(inviter->clientId, NULL) != NULL ||
        vm_mock_service_duel_find_for_client(responder->clientId, NULL) != NULL)
    {
        return NULL;
    }
    inviterTeam = vm_mock_service_team_find_for_client(inviter->clientId);
    responderTeam = vm_mock_service_team_find_for_client(responder->clientId);
    if ((inviterTeam != NULL && inviterTeam->battleActive) ||
        (responderTeam != NULL && responderTeam->battleActive))
    {
        return NULL;
    }
    for (u32 i = 0; i < VM_MOCK_SERVICE_DUEL_MAX; ++i)
    {
        if (!g_vm_mock_service_duels[i].active)
        {
            slot = &g_vm_mock_service_duels[i];
            break;
        }
    }
    if (slot == NULL)
        return NULL;

    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->serial = ++g_vm_mock_service_duel_serial;
    if (slot->serial == 0)
        slot->serial = ++g_vm_mock_service_duel_serial;
    slot->clientIds[0] = inviter->clientId;
    slot->clientIds[1] = responder->clientId;
    slot->arenaRoomId = arenaRoomId;
    snprintf(slot->scene, sizeof(slot->scene), "%s", inviter->sceneVisibleScene);
    slot->hpMax[0] = inviter->onlineHpMax ? inviter->onlineHpMax : 1;
    slot->hpMax[1] = responder->onlineHpMax ? responder->onlineHpMax : 1;
    slot->hp[0] = inviter->onlineHp ?
        vm_net_mock_min_u32(inviter->onlineHp, slot->hpMax[0]) : slot->hpMax[0];
    slot->hp[1] = responder->onlineHp ?
        vm_net_mock_min_u32(responder->onlineHp, slot->hpMax[1]) : slot->hpMax[1];
    slot->mpMax[0] = inviter->onlineMpMax;
    slot->mpMax[1] = responder->onlineMpMax;
    slot->mp[0] = vm_net_mock_min_u32(inviter->onlineMp, slot->mpMax[0]);
    slot->mp[1] = vm_net_mock_min_u32(responder->onlineMp, slot->mpMax[1]);
    slot->startPendingMask = 3;
    slot->roundSerial = 1;
    printf("[info][mock-service] duel_begin serial=%u mode=%s arena_room=%u "
           "inviter=%08x/%u responder=%08x/%u scene=%s hp=%u/%u,%u/%u "
           "mp=%u/%u,%u/%u\n",
           slot->serial,
           arenaRoomId != 0 ? "arena" : "spar", arenaRoomId,
           inviter->clientId, inviter->onlineRoleId,
           responder->clientId, responder->onlineRoleId,
           slot->scene,
           slot->hp[0], slot->hpMax[0], slot->hp[1], slot->hpMax[1],
           slot->mp[0], slot->mpMax[0], slot->mp[1], slot->mpMax[1]);
    return slot;
}

vm_mock_service_duel *vm_mock_service_duel_begin(
    vm_mock_service_client_session *inviter,
    vm_mock_service_client_session *responder)
{
    if (inviter == NULL || responder == NULL)
        return NULL;
    return vm_mock_service_duel_begin_ex(inviter, responder, true, 0);
}

vm_mock_service_duel *vm_mock_service_arena_duel_begin(
    vm_mock_service_client_session *challenger,
    vm_mock_service_client_session *opponent, u32 roomId)
{
    if (roomId == 0)
        return NULL;
    return vm_mock_service_duel_begin_ex(challenger, opponent, false, roomId);
}

static void vm_mock_service_mark_shop_scene_npc_reseed_pending(const char *source)
{
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();
    const char *scene = vm_net_mock_current_scene_name();
    bool changed = false;

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
              !vm_net_mock_scene_names_equal_exact(session->shopSceneNpcReseedScene,
                                                   scene);
    session->shopSceneNpcReseedPending = true;
    session->shopSceneNpcReseedMode = 1;
    snprintf(session->shopSceneNpcReseedScene,
             sizeof(session->shopSceneNpcReseedScene), "%s", scene);
    if (changed)
    {
        printf("[info][mock-service] scene_npc_reseed_arm client=%08x scene=%s trigger=shop-open source=%s delivery=next-scene-followup\n",
               session->clientId, scene, source ? source : "-");
    }
}

static void vm_mock_service_mark_backpack_bootstrap_npc_reseed_pending(
    const char *source)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();
    const char *scene = vm_net_mock_current_scene_name();
    bool changed = false;

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
              session->shopSceneNpcReseedMode != 2 ||
              session->shopSceneNpcReseedScene[0] == 0 ||
              !vm_net_mock_scene_names_equal_exact(session->shopSceneNpcReseedScene,
                                                   scene);
    session->shopSceneNpcReseedPending = true;
    session->shopSceneNpcReseedMode = 2;
    snprintf(session->shopSceneNpcReseedScene,
             sizeof(session->shopSceneNpcReseedScene), "%s", scene);
    if (changed)
    {
        printf("[info][mock-service] scene_npc_reseed_arm client=%08x "
               "scene=%s trigger=backpack-grid-bootstrap source=%s "
               "delivery=next-scene-followup mode=bootstrap-only\n",
               session->clientId, scene, source ? source : "-");
    }
}

static bool vm_mock_service_shop_scene_npc_reseed_is_bootstrap_only(void)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    return session != NULL && session->shopSceneNpcReseedPending &&
           session->shopSceneNpcReseedMode == 2;
}

static bool vm_mock_service_shop_scene_npc_reseed_matches(const char *scene);

static bool vm_mock_service_shop_scene_npc_reseed_requires_scene_enter(
    const char *scene)
{
    return vm_mock_service_shop_scene_npc_reseed_matches(scene) &&
           !vm_mock_service_shop_scene_npc_reseed_is_bootstrap_only();
}

static void vm_mock_service_clear_shop_scene_npc_reseed_pending(
    const char *source)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->shopSceneNpcReseedPending)
        return;
    printf("[info][mock-service] scene_npc_reseed_clear client=%08x "
           "scene=%s source=%s mode=%u\n",
           session->clientId,
           session->shopSceneNpcReseedScene[0] ?
               session->shopSceneNpcReseedScene : "-",
           source ? source : "-", (u32)session->shopSceneNpcReseedMode);
    session->shopSceneNpcReseedPending = false;
    session->shopSceneNpcReseedMode = 0;
    session->shopSceneNpcReseedScene[0] = 0;
}

static bool vm_mock_service_shop_scene_npc_reseed_matches(const char *scene)
{
    vm_mock_service_client_session *session = vm_mock_service_get_active_client_session();

    return session != NULL &&
           session->shopSceneNpcReseedPending &&
           session->shopSceneNpcReseedScene[0] != 0 &&
           vm_net_mock_scene_name_is_safe(scene) &&
           vm_net_mock_scene_names_equal_exact(session->shopSceneNpcReseedScene,
                                               scene);
}

static void vm_mock_service_arm_shop_scene_return_post_enter(const char *scene,
                                                              u16 x, u16 y,
                                                              const char *source)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene) ||
        x == 0 || y == 0)
    {
        return;
    }
    session->shopSceneReturnPostEnterPending = true;
    snprintf(session->shopSceneReturnPostEnterScene,
             sizeof(session->shopSceneReturnPostEnterScene), "%s", scene);
    session->shopSceneReturnPostEnterX = x;
    session->shopSceneReturnPostEnterY = y;
    printf("[info][mock-service] shop_return_post_enter_arm client=%08x "
           "scene=%s pos=(%u,%u) source=%s contract=30/2-then-post-enter\n",
           session->clientId, scene, (u32)x, (u32)y, source ? source : "-");
}

static bool vm_mock_service_shop_scene_return_post_enter_matches(
    const char *scene, u16 *xOut, u16 *yOut)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->shopSceneReturnPostEnterPending ||
        session->shopSceneReturnPostEnterScene[0] == 0 ||
        !vm_net_mock_scene_name_is_safe(scene) ||
        !vm_net_mock_scene_names_equal_exact(
            session->shopSceneReturnPostEnterScene, scene) ||
        session->shopSceneReturnPostEnterX == 0 ||
        session->shopSceneReturnPostEnterY == 0)
    {
        return false;
    }
    if (xOut != NULL)
        *xOut = session->shopSceneReturnPostEnterX;
    if (yOut != NULL)
        *yOut = session->shopSceneReturnPostEnterY;
    return true;
}

static void vm_mock_service_clear_shop_scene_return_post_enter(const char *source)
{
    vm_mock_service_client_session *session =
        vm_mock_service_get_active_client_session();

    if (session == NULL || !session->shopSceneReturnPostEnterPending)
        return;
    printf("[info][mock-service] shop_return_post_enter_clear client=%08x "
           "scene=%s pos=(%u,%u) source=%s\n",
           session->clientId, session->shopSceneReturnPostEnterScene,
           (u32)session->shopSceneReturnPostEnterX,
           (u32)session->shopSceneReturnPostEnterY,
           source ? source : "-");
    session->shopSceneReturnPostEnterPending = false;
    session->shopSceneReturnPostEnterScene[0] = 0;
    session->shopSceneReturnPostEnterX = 0;
    session->shopSceneReturnPostEnterY = 0;
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

bool vm_mock_service_trade_begin_pair(
    vm_mock_service_client_session *first,
    vm_mock_service_client_session *second)
{
    return vm_mock_service_trade_begin(first, second) != NULL;
}

void vm_mock_service_trade_abort_pair(
    vm_mock_service_client_session *first,
    vm_mock_service_client_session *second)
{
    vm_mock_service_trade *trade = NULL;

    if (first == NULL || second == NULL)
        return;
    trade = vm_mock_service_trade_find_for_client(first->clientId, NULL);
    if (trade == NULL || !trade->active ||
        !((trade->clientIds[0] == first->clientId &&
           trade->clientIds[1] == second->clientId) ||
          (trade->clientIds[0] == second->clientId &&
           trade->clientIds[1] == first->clientId)))
    {
        return;
    }
    memset(trade, 0, sizeof(*trade));
}

vm_mock_service_trade_submit_status vm_mock_service_trade_submit_offer(
    vm_mock_service_client_session *session,
    const vm_mock_service_trade_offer *offer, bool offerValid,
    vm_mock_service_trade_submit_result *resultOut)
{
    vm_mock_service_trade *trade = NULL;
    vm_mock_service_trade_offer submitted;
    int index = -1;

    if (resultOut != NULL)
    {
        resultOut->side = -1;
        resultOut->peerOfferPending = false;
    }
    if (session == NULL)
        return VM_MOCK_SERVICE_TRADE_SUBMIT_NOT_ACTIVE;
    trade = vm_mock_service_trade_find_for_client(session->clientId, &index);
    if (trade == NULL || index < 0 || !trade->active)
        return VM_MOCK_SERVICE_TRADE_SUBMIT_NOT_ACTIVE;
    if (resultOut != NULL)
        resultOut->side = index;
    if (!offerValid || offer == NULL)
        return VM_MOCK_SERVICE_TRADE_SUBMIT_INVALID;

    submitted = *offer;
    submitted.submitted = true;
    trade->offers[index] = submitted;
    trade->confirmedMask = 0;
    trade->offerPendingMask |= (u8)(1u << (1 - index));
    if (resultOut != NULL)
        resultOut->peerOfferPending =
            ((trade->offerPendingMask >> (1 - index)) & 1u) != 0;
    return VM_MOCK_SERVICE_TRADE_SUBMIT_ACCEPTED;
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
    if (account == g_vm_mock_service_active_account)
    {
        if (!g_vm_net_mock_role_db_valid)
            return NULL;
        database = &g_vm_net_mock_role_db;
    }
    else
    {
        if (!account->roleDbLoaded || !account->roleDbValid)
            return NULL;
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
    const vm_mock_service_trade_item *incoming,
    u16 *destinationSeqOut)
{
    u8 itemCount = 0;
    bool isEquipment = false;
    u16 equipmentDurabilityMax = 0;

    if (destinationSeqOut)
        *destinationSeqOut = 0;
    if (role == NULL || incoming == NULL || incoming->itemId == 0 ||
        incoming->count == 0)
    {
        return false;
    }
    isEquipment = vm_net_mock_find_equipment_catalog_item(incoming->itemId) != NULL;

    /* The client receipt (WT21/8) identifies one incoming row by one
     * destination sequence.  An equipment row is therefore one indivisible
     * instance; accepting a stack here would lose its per-instance state. */
    if (isEquipment && incoming->count != 1)
        return false;
    if (isEquipment)
    {
        equipmentDurabilityMax = vm_net_mock_equipment_durability_max_for_item(
            incoming->itemId);
        if (equipmentDurabilityMax == 0)
            return false;
    }
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    if (!isEquipment)
    {
        for (u32 i = 0; i < itemCount; ++i)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
            if (item->itemId != incoming->itemId)
                continue;
            if (0xffffffffu - item->count < incoming->count)
                return false;
            item->count += incoming->count;
            if (destinationSeqOut)
                *destinationSeqOut = item->seq;
            return true;
        }
    }
    if (itemCount >= role->backpackCapacity ||
        itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
    {
        return false;
    }
    vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount];
    memset(item, 0, sizeof(*item));
    item->itemId = incoming->itemId;
    if (!vm_net_mock_role_allocate_backpack_sequence(
            role, NULL, 0, role->nextBackpackSeq, &item->seq))
    {
        return false;
    }
    item->count = incoming->count;
    if (isEquipment)
    {
    item->enhanceLevel = (u16)SDL_min(
            incoming->enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
        item->enhanceAffixes = incoming->enhanceAffixes;
        item->durabilityMax = equipmentDurabilityMax;
        item->durability = incoming->durability > item->durabilityMax
                               ? item->durabilityMax : incoming->durability;
    }
    role->backpackItemCount = (u8)(itemCount + 1);
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (destinationSeqOut)
        *destinationSeqOut = item->seq;
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
        "INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level,enhance_affix_types,enhance_affix_values,durability,durability_max) VALUES");
    for (u32 side = 0; side < 2; ++side)
    {
        u8 count = vm_net_mock_role_backpack_count(&roles[side]);
        for (u32 slot = 0; slot < count; ++slot)
        {
            const vm_net_mock_backpack_item_state *item = &roles[side].backpackItems[slot];
            int written = 0;
            if (item->itemId == 0 || item->seq == 0 || item->count == 0)
                continue;
            written = snprintf(
                bulkQuery + bulkLen, bulkCapacity - bulkLen,
                "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u,%u,%llu,%u,%u)",
                bulkRows ? "," : "", accountHex[side], roles[side].roleId,
                slot, item->itemId, item->seq, item->count,
                item->enhanceLevel,
                vm_net_mock_equipment_enhancement_pack_affix_types(
                    &item->enhanceAffixes),
                (unsigned long long)
                    vm_net_mock_equipment_enhancement_pack_affix_values(
                        &item->enhanceAffixes),
                item->durability, item->durabilityMax);
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

bool vm_mock_service_trade_validate_offer(vm_mock_service_trade_offer *offer,
                                          vm_net_mock_role_state *role)
{
    if (offer == NULL || role == NULL ||
        offer->itemCount > VM_MOCK_SERVICE_TRADE_ITEM_MAX ||
        offer->money > role->money ||
        (offer->itemCount == 0 && offer->money == 0))
    {
        return false;
    }
    for (u32 i = 0; i < offer->itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = NULL;
        if (offer->items[i].sourceSeq == 0 || offer->items[i].count == 0)
            return false;
        for (u32 previous = 0; previous < i; ++previous)
        {
            if (offer->items[previous].sourceSeq == offer->items[i].sourceSeq)
                return false;
        }
        item = vm_net_mock_role_find_backpack_item(
            role, 0, offer->items[i].sourceSeq);
        if (item == NULL || item->itemId == 0 || item->count < offer->items[i].count)
            return false;
        if (vm_net_mock_find_equipment_catalog_item(item->itemId) != NULL &&
            offer->items[i].count != 1)
        {
            return false;
        }
        offer->items[i].itemId = item->itemId;
        offer->items[i].enhanceLevel = (u16)SDL_min(
            item->enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
        offer->items[i].enhanceAffixes = item->enhanceAffixes;
        offer->items[i].durability = item->durability;
        offer->items[i].durabilityMax = item->durabilityMax;
    }
    return true;
}

static u8 vm_mock_service_trade_commit_pair(vm_mock_service_trade *trade)
{
    vm_mock_service_client_session *sessions[2];
    const vm_mock_service_client_session *persistSessions[2];
    vm_mock_service_account_state *accounts[2];
    vm_net_mock_role_state *liveRoles[2];
    vm_net_mock_role_state roles[2];

    if (trade == NULL || !trade->active ||
        !trade->offers[0].submitted || !trade->offers[1].submitted)
    {
        return VM_MOCK_SERVICE_TRADE_COMMIT_INVALID;
    }
    memset(sessions, 0, sizeof(sessions));
    memset(persistSessions, 0, sizeof(persistSessions));
    memset(accounts, 0, sizeof(accounts));
    memset(liveRoles, 0, sizeof(liveRoles));
    memset(roles, 0, sizeof(roles));
    for (u32 side = 0; side < 2; ++side)
    {
        sessions[side] = vm_mock_service_find_client_session(trade->clientIds[side]);
        persistSessions[side] = sessions[side];
        liveRoles[side] = vm_mock_service_trade_role_for_session(sessions[side],
                                                                  &accounts[side]);
        if (sessions[side] == NULL || !sessions[side]->roleOnline ||
            liveRoles[side] == NULL ||
            !vm_mock_service_trade_validate_offer(&trade->offers[side], liveRoles[side]))
        {
            return VM_MOCK_SERVICE_TRADE_COMMIT_INVALID;
        }
        roles[side] = *liveRoles[side];
        memset(&trade->receipts[side], 0, sizeof(trade->receipts[side]));
    }
    for (u32 side = 0; side < 2; ++side)
    {
        const vm_mock_service_trade_offer *offer = &trade->offers[side];
        for (u32 i = 0; i < offer->itemCount; ++i)
        {
            if (!vm_net_mock_role_consume_backpack_item(
                    &roles[side], offer->items[i].itemId,
                    offer->items[i].sourceSeq, offer->items[i].count, NULL))
            {
                return VM_MOCK_SERVICE_TRADE_COMMIT_INVALID;
            }
        }
        roles[side].money -= offer->money;
    }
    for (u32 side = 0; side < 2; ++side)
    {
        const vm_mock_service_trade_offer *incoming = &trade->offers[1 - side];
        vm_mock_service_trade_offer *receipt = &trade->receipts[side];
        uint64_t finalMoney = (uint64_t)roles[side].money + incoming->money;
        if (finalMoney > 0xffffffffull)
            return VM_MOCK_SERVICE_TRADE_COMMIT_INVALID;
        roles[side].money = (u32)finalMoney;
        receipt->submitted = true;
        receipt->itemCount = incoming->itemCount;
        for (u32 i = 0; i < incoming->itemCount; ++i)
        {
            receipt->items[i] = incoming->items[i];
            if (!vm_mock_service_trade_role_add_item(
                    &roles[side], &incoming->items[i],
                    &receipt->items[i].destinationSeq))
            {
                return VM_MOCK_SERVICE_TRADE_COMMIT_BAG_FULL;
            }
            printf("[info][mock-service] trade_item_transfer receiver_role=%u source_role=%u item=%u source_seq=%u destination_seq=%u count=%u enhance=%u durability=%u/%u equipment=%u evidence=WT21/5+21/6+21/8\n",
                   roles[side].roleId, roles[1 - side].roleId,
                   incoming->items[i].itemId,
                   incoming->items[i].sourceSeq,
                   receipt->items[i].destinationSeq,
                   incoming->items[i].count,
                   incoming->items[i].enhanceLevel,
                   incoming->items[i].durability,
                   incoming->items[i].durabilityMax,
                   vm_net_mock_find_equipment_catalog_item(
                       incoming->items[i].itemId) != NULL ? 1u : 0u);
        }
        vm_net_mock_role_normalize_backpack(&roles[side]);
        trade->finalMoney[side] = roles[side].money;
    }
    if (!vm_mock_service_trade_persist_pair(persistSessions, roles))
    {
        return VM_MOCK_SERVICE_TRADE_COMMIT_STORAGE_FAILED;
    }
    for (u32 side = 0; side < 2; ++side)
        *liveRoles[side] = roles[side];
    for (u32 side = 0; side < 2; ++side)
    {
        const u32 peer = 1u - side;
        char operationDetail[256];

        snprintf(operationDetail, sizeof(operationDetail),
                 "交易对象=%s/%u；付出:钱%u,物品%u；收到:钱%u,物品%u；余额=%u",
                 sessions[peer]->accountId, roles[peer].roleId,
                 trade->offers[side].money, trade->offers[side].itemCount,
                 trade->offers[peer].money, trade->receipts[side].itemCount,
                 roles[side].money);
        if (!vm_mock_admin_operation_log_record(
                "player-trade", sessions[side]->accountId, roles[side].roleId,
                0, trade->offers[side].itemCount, trade->offers[side].money,
                operationDetail, NULL))
        {
            printf("[error][mock-service] operation_log_player_trade_failed "
                   "account=%s role=%u peer=%s/%u error=%s\n",
                   sessions[side]->accountId, roles[side].roleId,
                   sessions[peer]->accountId, roles[peer].roleId,
                   vm_mysql_last_error());
        }
    }
    printf("[info][mock-service] trade_commit first=%08x/%u money=%u items=%u second=%08x/%u money=%u items=%u\n",
           sessions[0]->clientId, roles[0].roleId, roles[0].money,
           trade->receipts[0].itemCount,
           sessions[1]->clientId, roles[1].roleId, roles[1].money,
           trade->receipts[1].itemCount);
    return VM_MOCK_SERVICE_TRADE_COMMIT_OK;
}

void vm_mock_service_trade_confirm(
    vm_mock_service_client_session *session, u8 requestResult,
    vm_mock_service_trade_confirm_result *resultOut)
{
    vm_mock_service_trade *trade = NULL;
    int index = -1;

    if (resultOut == NULL)
        return;
    memset(resultOut, 0, sizeof(*resultOut));
    resultOut->side = -1;
    resultOut->responseSubtype = 7;
    resultOut->responseResult = 2;
    if (session == NULL)
        return;
    trade = vm_mock_service_trade_find_for_client(session->clientId, &index);
    if (trade == NULL || index < 0 || !trade->active)
        return;

    resultOut->side = index;
    if (requestResult == 2)
    {
        vm_mock_service_trade_set_terminal(trade, 7, 2,
                                           (u8)(1u << (1 - index)));
        resultOut->responseResult = 2;
    }
    else if (trade->offers[0].submitted && trade->offers[1].submitted)
    {
        trade->confirmedMask |= (u8)(1u << index);
        resultOut->responseResult = 1;
        if (trade->confirmedMask == 3)
        {
            resultOut->commitResult = vm_mock_service_trade_commit_pair(trade);
            if (resultOut->commitResult == VM_MOCK_SERVICE_TRADE_COMMIT_OK)
            {
                resultOut->responseSubtype = 8;
                resultOut->responseResult = 1;
                vm_mock_service_trade_set_terminal(trade, 8, 1,
                                                   (u8)(1u << (1 - index)));
            }
            else if (resultOut->commitResult == VM_MOCK_SERVICE_TRADE_COMMIT_BAG_FULL)
            {
                resultOut->responseSubtype = 8;
                resultOut->responseResult = 3;
                vm_mock_service_trade_set_terminal(trade, 8, 3,
                                                   (u8)(1u << (1 - index)));
            }
            else if (resultOut->commitResult == VM_MOCK_SERVICE_TRADE_COMMIT_STORAGE_FAILED)
            {
                resultOut->responseSubtype = 8;
                resultOut->responseResult = 2;
                vm_mock_service_trade_set_terminal(trade, 8, 2,
                                                   (u8)(1u << (1 - index)));
            }
            else
            {
                resultOut->responseSubtype = 7;
                resultOut->responseResult = 3;
                vm_mock_service_trade_set_terminal(trade, 7, 3,
                                                   (u8)(1u << (1 - index)));
            }
        }
    }
    else
    {
        resultOut->responseResult = 2;
        vm_mock_service_trade_set_terminal(trade, 7, 2,
                                           (u8)(1u << (1 - index)));
    }
    resultOut->confirmedMask = trade->confirmedMask;
    resultOut->finalMoney = trade->finalMoney[index];
    resultOut->receipt = trade->receipts[index];
    resultOut->releaseAfterDelivery = resultOut->responseSubtype == 8;
}

void vm_mock_service_trade_release_after_direct_terminal_delivery(
    vm_mock_service_client_session *session)
{
    vm_mock_service_trade *trade = NULL;

    if (session == NULL)
        return;
    trade = vm_mock_service_trade_find_for_client(session->clientId, NULL);
    if (trade != NULL)
        vm_mock_service_trade_release_if_delivered(trade);
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
    case VM_MOCK_SERVICE_SOCIAL_NOTICE_ARENA_CHALLENGE:
        return "arena-challenge";
    default:
        return "unknown";
    }
}

bool vm_mock_service_session_enqueue_social_notice(
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
        sourceName = sourceRole->name;
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
    slot->sourceLevel = sourceLevel;
    slot->sourceJob = sourceJob;
    slot->sourceSex = sourceSex;
    snprintf(slot->sourceAccountId, sizeof(slot->sourceAccountId), "%s",
             sourceAccountId && sourceAccountId[0] ? sourceAccountId : source->accountId);
    snprintf(slot->sourceName, sizeof(slot->sourceName), "%s",
             sourceName && sourceName[0] ? sourceName :
             (source->onlineRoleName[0] ? source->onlineRoleName : "Player"));
    slot->queuedTick = g_schedulerTick;
    printf("[info][mock-service] social_notice_queue target=%08x action=%s source=%08x/%u name=%s result=%u\n",
           target->clientId,
           vm_mock_service_social_notice_name(type),
           source->clientId,
           sourceRoleId,
           slot->sourceName,
           result);
    return true;
}

bool vm_mock_service_session_enqueue_arena_challenge_notice(
    vm_mock_service_client_session *target,
    const vm_mock_service_client_session *challenger,
    const vm_net_mock_role_state *challengerRole,
    const char *challengerAccountId)
{
    return vm_mock_service_session_enqueue_social_notice(
        target, VM_MOCK_SERVICE_SOCIAL_NOTICE_ARENA_CHALLENGE, 0,
        challenger, challengerRole, challengerAccountId);
}

bool vm_mock_service_enqueue_guild_application_notice(
    const vm_net_mock_guild_application_record *application,
    const vm_net_mock_guild_record *guild, u8 actionType,
    const vm_net_mock_role_state *requester)
{
    vm_mock_service_client_session *target = NULL;
    vm_mock_service_client_session *source =
        vm_mock_service_get_active_client_session();
    vm_mock_service_social_notice *slot = NULL;
    u8 noticeType = actionType == 1
                        ? VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED
                        : VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_REJECTED;

    if (application == NULL || guild == NULL || guild->guildId == 0 ||
        application->roleId == 0 || application->accountId[0] == 0 ||
        (actionType != 1 && actionType != 2))
    {
        return false;
    }
    target = vm_mock_service_find_online_session_by_role_account(
        application->roleId, application->accountId);
    if (target == NULL)
    {
        printf("[info][mock-service] guild_application_notice_queue "
               "target=%s/%u action=%s guild=%u queued=0 reason=target-offline\n",
               application->accountId, application->roleId,
               vm_mock_service_social_notice_name(noticeType), guild->guildId);
        return false;
    }

    for (u32 i = 0; i < VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX; ++i)
    {
        vm_mock_service_social_notice *entry = &target->socialNotices[i];
        if ((entry->type == VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_APPROVED ||
             entry->type == VM_MOCK_SERVICE_SOCIAL_NOTICE_GUILD_APPLICATION_REJECTED) &&
            entry->guildId == guild->guildId)
        {
            slot = entry;
            break;
        }
        if (slot == NULL && entry->type == VM_MOCK_SERVICE_SOCIAL_NOTICE_NONE)
            slot = entry;
    }
    if (slot == NULL)
    {
        printf("[warn][mock-service] guild_application_notice_queue "
               "target=%08x/%u action=%s guild=%u queued=0 reason=queue-full\n",
               target->clientId, application->roleId,
               vm_mock_service_social_notice_name(noticeType), guild->guildId);
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    slot->type = noticeType;
    slot->result = actionType;
    slot->sourceClientId = source ? source->clientId : 0;
    slot->sourceRoleId = requester ? requester->roleId : 0;
    slot->sourceLevel = (u16)(requester && requester->level ? requester->level : 1);
    slot->sourceJob = requester && requester->job ? requester->job : 1;
    slot->sourceSex = requester && requester->sex <= 1 ? requester->sex : 0;
    snprintf(slot->sourceAccountId, sizeof(slot->sourceAccountId), "%s",
             vm_mock_service_active_account_id() ?
                 vm_mock_service_active_account_id() : "");
    snprintf(slot->sourceName, sizeof(slot->sourceName), "%s",
             requester && requester->name[0] ? requester->name : "Guild");
    slot->guildId = guild->guildId;
    slot->guildStatus = actionType == 1 ? 3 : 0;
    snprintf(slot->guildName, sizeof(slot->guildName), "%s", guild->guildName);
    slot->queuedTick = g_schedulerTick;
    printf("[info][mock-service] guild_application_notice_queue "
           "target=%08x/%u action=%s guild=%u name=%s status=%u queued=1\n",
           target->clientId, application->roleId,
           vm_mock_service_social_notice_name(noticeType), guild->guildId,
           guild->guildName, slot->guildStatus);
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

/* net_handle_type_payload_detail(0x010126C6) copies the wire message into an
 * 80-byte temporary buffer without copying a terminator.  Keep at least one
 * zero byte in that buffer and never split a two-byte GBK character. */
static size_t vm_mock_chat_copy_wire_message(char *out, size_t outCap,
                                             const char *message,
                                             bool *truncatedOut)
{
    size_t sourceLen = 0;
    size_t limit = 0;
    size_t copyLen = 0;

    if (truncatedOut != NULL)
        *truncatedOut = false;
    if (out == NULL || outCap == 0)
        return 0;
    out[0] = 0;
    if (message == NULL || message[0] == 0)
        return 0;
    sourceLen = strlen(message);
    limit = sourceLen;
    if (limit > VM_MOCK_CHAT_MESSAGE_MAX_BYTES)
        limit = VM_MOCK_CHAT_MESSAGE_MAX_BYTES;
    if (limit >= outCap)
        limit = outCap - 1;
    while (copyLen < limit)
    {
        size_t charBytes = (unsigned char)message[copyLen] >= 0x81u ? 2u : 1u;
        if (copyLen + charBytes > limit || copyLen + charBytes > sourceLen)
            break;
        copyLen += charBytes;
    }
    memcpy(out, message, copyLen);
    out[copyLen] = 0;
    if (truncatedOut != NULL)
        *truncatedOut = copyLen < sourceLen;
    return copyLen;
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
    size_t sourceMessageLen = 0;
    size_t wireMessageLen = 0;
    bool messageTruncated = false;

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
    sourceMessageLen = strlen(message);
    wireMessageLen = vm_mock_chat_copy_wire_message(
        slot->message, sizeof(slot->message), message, &messageTruncated);
    if (wireMessageLen == 0)
    {
        memset(slot, 0, sizeof(*slot));
        return false;
    }
    slot->queuedTick = g_schedulerTick;
    ++target->chatNoticeCount;
    printf("[info][mock-service] chat_notice_queue target=%08x type=%s source=%08x/%u bytes=%u source_bytes=%u truncated=%u depth=%u\n",
           target->clientId,
           vm_mock_service_chat_type_name(type),
           slot->sourceClientId,
           slot->sourceRoleId,
           (u32)wireMessageLen,
           (u32)sourceMessageLen,
           messageTruncated ? 1u : 0u,
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
        "message VARBINARY(79) NOT NULL,"
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
    char wireMessage[VM_MOCK_CHAT_MESSAGE_MAX_BYTES + 1];
    char query[768];
    const char *accountId = NULL;
    size_t accountLen = 0;
    size_t sourceNameLen = 0;
    size_t messageLen = 0;
    size_t sourceMessageLen = 0;
    bool messageTruncated = false;

    if (source == NULL || source->onlineRoleId == 0 || sourceName == NULL ||
        message == NULL)
    {
        return false;
    }
    accountId = source->accountId[0] ? source->accountId : "-";
    accountLen = strlen(accountId);
    sourceNameLen = strlen(sourceName);
    sourceMessageLen = strlen(message);
    messageLen = vm_mock_chat_copy_wire_message(
        wireMessage, sizeof(wireMessage), message, &messageTruncated);
    if (accountLen == 0 || accountLen >= sizeof(source->accountId) ||
        sourceNameLen == 0 || sourceNameLen > 15 ||
        messageLen == 0 ||
        !vm_mock_world_chat_table_ensure() ||
        vm_mysql_hex_encode(accountId, accountLen,
                            accountHex, sizeof(accountHex)) == 0 ||
        vm_mysql_hex_encode(sourceName, sourceNameLen,
                            sourceNameHex, sizeof(sourceNameHex)) == 0 ||
        vm_mysql_hex_encode(wireMessage, messageLen,
                            messageHex, sizeof(messageHex)) == 0)
    {
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
    printf("[info][mock-service] world_chat_store source=%08x/%u bytes=%u source_bytes=%u truncated=%u storage=mysql\n",
           source->clientId, source->onlineRoleId, (u32)messageLen,
           (u32)sourceMessageLen, messageTruncated ? 1u : 0u);
    return true;
}

static bool vm_mock_world_chat_build_chest_reward_message(
    const char *openerName, const char *chestNameGbk,
    const char *rewardNameGbk, u32 rewardCount,
    char *messageOut, size_t messageOutCap)
{
    static const char prefixGbk[] =
        "\xB9\xA7\xCF\xB2\xCD\xE6\xBC\xD2\xA1\xBE"; /* 恭喜玩家【 */
    static const char openedGbk[] =
        "\xA1\xBF\xBF\xAA\xC6\xF4"; /* 】开启 */
    static const char receivedGbk[] =
        "\xBB\xF1\xB5\xC3"; /* 获得 */
    static const char multiplierGbk[] = "\xA1\xC1"; /* × */
    int written = 0;

    if (messageOut == NULL || messageOutCap == 0)
        return false;
    messageOut[0] = 0;
    if (openerName == NULL || openerName[0] == 0 ||
        chestNameGbk == NULL || chestNameGbk[0] == 0 ||
        rewardNameGbk == NULL || rewardNameGbk[0] == 0 || rewardCount == 0)
    {
        return false;
    }
    if (rewardCount == 1)
    {
        written = snprintf(messageOut, messageOutCap, "%s%s%s%s%s",
                           prefixGbk, openerName, openedGbk, chestNameGbk,
                           receivedGbk);
        if (written > 0 && (size_t)written < messageOutCap)
            written += snprintf(messageOut + written,
                                messageOutCap - (size_t)written, "%s",
                                rewardNameGbk);
    }
    else
    {
        written = snprintf(messageOut, messageOutCap, "%s%s%s%s%s%s%s%u",
                           prefixGbk, openerName, openedGbk, chestNameGbk,
                           receivedGbk, rewardNameGbk, multiplierGbk,
                           rewardCount);
    }
    if (written <= 0 || (size_t)written >= messageOutCap)
    {
        messageOut[0] = 0;
        return false;
    }
    return true;
}

/* World broadcasts are authored by the system but retained with the opener's
 * role id so the existing recent-world-message query, which rejects a zero
 * role id, can replay the announcement to later logins.  The supplied opener
 * name is deliberately part of the message body: it makes the notice remain
 * unambiguous while the visible sender stays “系统”. */
static bool vm_mock_world_chat_publish_chest_reward(
    const char *openerName, u32 chestItemId, const char *chestNameGbk,
    u32 rewardItemId, const char *rewardNameGbk, u32 rewardCount)
{
    static const char systemNameGbk[] = "\xCF\xB5\xCD\xB3"; /* 系统 */
    vm_mock_service_client_session *source =
        vm_mock_service_get_active_client_session();
    vm_mock_service_client_session *target =
        g_vm_mock_service_client_sessions;
    char message[256];
    u32 recipients = 0;

    if (source == NULL || source->onlineRoleId == 0 ||
        !vm_mock_world_chat_build_chest_reward_message(
            openerName, chestNameGbk, rewardNameGbk, rewardCount,
            message, sizeof(message)))
    {
        return false;
    }
    /* Persist before exposing the event.  This is identical to player world
     * chat's store-before-delivery contract and lets the normal login history
     * replay carry the same announcement to a later observer. */
    if (!vm_mock_world_chat_store(source, systemNameGbk, message))
        return false;

    while (target != NULL)
    {
        if ((target == source ||
             (target->roleOnline && target->onlinePresenceValid &&
              vm_mock_service_session_presence_is_recent(target))) &&
            vm_mock_service_session_enqueue_chat_notice_identity(
                target, VM_MOCK_CHAT_TYPE_WORLD, 0, source->onlineRoleId,
                systemNameGbk, message))
        {
            ++recipients;
        }
        target = target->next;
    }
    printf("[info][network] mock_chest_world_broadcast source=%08x/%u chest=%u reward=%u count=%u recipients=%u storage=mysql delivery=scene-sync-poll\n",
           source->clientId, source->onlineRoleId, chestItemId, rewardItemId,
           rewardCount, recipients);
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
    /* Existing deployments may still contain rows written under the old
     * 81-byte schema.  Decode those rows, then normalize before enqueue. */
    char storedMessage[82];
    char message[VM_MOCK_CHAT_MESSAGE_MAX_BYTES + 1];
    size_t decodedLen = 0;
    size_t wireMessageLen = 0;
    u32 sourceRoleId = 0;
    bool messageTruncated = false;

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
    memset(storedMessage, 0, sizeof(storedMessage));
    if (!vm_mysql_hex_decode(values[2], lengths[2], storedMessage,
                             sizeof(storedMessage) - 1, &decodedLen) ||
        decodedLen == 0 || decodedLen > 81)
    {
        ++context->skipped;
        return true;
    }
    storedMessage[decodedLen] = 0;
    wireMessageLen = vm_mock_chat_copy_wire_message(
        message, sizeof(message), storedMessage, &messageTruncated);
    if (wireMessageLen == 0)
    {
        ++context->skipped;
        return true;
    }
    if (messageTruncated)
    {
        printf("[warn][mock-service] world_chat_history_normalize role=%u source_bytes=%u bytes=%u max=%u evidence=JianghuOL.CBE:0x010126C6\n",
               sourceRoleId, (u32)decodedLen, (u32)wireMessageLen,
               VM_MOCK_CHAT_MESSAGE_MAX_BYTES);
    }
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
    const char query[] =
        "SELECT source_role_id,HEX(source_name),HEX(message) FROM ("
        "SELECT message_id,source_role_id,source_name,message "
        "FROM world_chat_messages ORDER BY message_id DESC LIMIT 30"
        ") AS recent ORDER BY message_id ASC";

    if (queuedOut)
        *queuedOut = 0;
    if (target == NULL || !vm_mock_world_chat_table_ensure())
        return false;
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

vm_mock_service_team_invite_status vm_mock_service_team_validate_invitation(
    const vm_mock_service_client_session *source,
    const vm_mock_service_client_session *target)
{
    vm_mock_service_team *sourceTeam = NULL;

    if (source == NULL || target == NULL || source->clientId == 0 ||
        target->clientId == 0 || source->clientId == target->clientId)
    {
        return VM_MOCK_SERVICE_TEAM_INVITE_INVALID;
    }
    sourceTeam = vm_mock_service_team_find_for_client(source->clientId);
    if (sourceTeam != NULL &&
        !vm_mock_service_team_is_leader(sourceTeam, source->clientId))
    {
        return VM_MOCK_SERVICE_TEAM_INVITE_NOT_LEADER;
    }
    if (sourceTeam != NULL &&
        sourceTeam->memberCount >= VM_MOCK_SERVICE_TEAM_MEMBER_MAX)
    {
        return VM_MOCK_SERVICE_TEAM_INVITE_FULL;
    }
    if (vm_mock_service_team_find_for_client(target->clientId) != NULL)
        return VM_MOCK_SERVICE_TEAM_INVITE_TARGET_IN_TEAM;
    return VM_MOCK_SERVICE_TEAM_INVITE_ALLOWED;
}

static vm_mock_service_team *vm_mock_service_team_create(vm_mock_service_client_session *leader)
{
    vm_mock_service_team *team = NULL;

    if (leader == NULL || leader->clientId == 0 || leader->onlineRoleId == 0)
        return NULL;
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
    if (team == NULL || !team->active || member == NULL || member->clientId == 0 ||
        member->onlineRoleId == 0 || team->memberCount >= VM_MOCK_SERVICE_TEAM_MEMBER_MAX ||
        vm_mock_service_team_contains_client(team, member->clientId) ||
        vm_mock_service_team_find_for_client(member->clientId) != NULL)
    {
        return false;
    }
    member->pendingTeamBattleSerial = 0;
    team->memberClientIds[team->memberCount++] = member->clientId;
    printf("[info][mock-service] team_add leader=%08x member=%08x/%u count=%u\n",
           team->leaderClientId, member->clientId, member->onlineRoleId, team->memberCount);
    return true;
}

static void vm_mock_service_team_enqueue_member_join_for_peers(
    const vm_mock_service_team *team, u32 exceptClientA, u32 exceptClientB,
    const vm_mock_service_client_session *joinedMember)
{
    if (team == NULL || !team->active || joinedMember == NULL)
        return;
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        vm_mock_service_client_session *peer =
            vm_mock_service_find_client_session(team->memberClientIds[member]);
        if (peer != NULL && peer->clientId != exceptClientA &&
            peer->clientId != exceptClientB)
        {
            (void)vm_mock_service_session_enqueue_social_notice(
                peer, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_MEMBER_JOIN, 0,
                joinedMember, NULL, joinedMember->accountId);
        }
    }
}

/* A successful 1/5/3 reply gives the accepting client its own bootstrap row
 * and the leader row. Existing non-leader members still need native 5/5
 * deltas, otherwise the third member's client never learns about them. */
static u8 vm_mock_service_team_enqueue_existing_members_for_joiner(
    const vm_mock_service_team *team,
    const vm_mock_service_client_session *leader,
    vm_mock_service_client_session *joiner)
{
    u8 queued = 0;

    if (team == NULL || !team->active || leader == NULL || joiner == NULL ||
        !vm_mock_service_team_contains_client(team, leader->clientId) ||
        !vm_mock_service_team_contains_client(team, joiner->clientId))
    {
        return 0;
    }
    for (u8 member = 0; member < team->memberCount; ++member)
    {
        vm_mock_service_client_session *existing =
            vm_mock_service_find_client_session(team->memberClientIds[member]);

        if (existing == NULL || existing->clientId == leader->clientId ||
            existing->clientId == joiner->clientId)
        {
            continue;
        }
        if (vm_mock_service_session_enqueue_social_notice(
                joiner, VM_MOCK_SERVICE_SOCIAL_NOTICE_TEAM_MEMBER_JOIN, 0,
                existing, NULL, existing->accountId))
        {
            ++queued;
        }
    }
    return queued;
}

bool vm_mock_service_team_accept_invitation(
    vm_mock_service_client_session *leader,
    vm_mock_service_client_session *joiner,
    vm_mock_service_team_join_result *resultOut)
{
    vm_mock_service_team *team = NULL;

    if (resultOut != NULL)
        memset(resultOut, 0, sizeof(*resultOut));
    if (leader == NULL || joiner == NULL || !leader->roleOnline ||
        leader->clientId == joiner->clientId ||
        vm_mock_service_team_find_for_client(joiner->clientId) != NULL)
    {
        return false;
    }
    team = vm_mock_service_team_find_for_client(leader->clientId);
    if (team == NULL)
        team = vm_mock_service_team_create(leader);
    if (team == NULL || !vm_mock_service_team_is_leader(team, leader->clientId) ||
        !vm_mock_service_team_add_member(team, joiner))
    {
        return false;
    }
    vm_mock_service_team_enqueue_member_join_for_peers(
        team, leader->clientId, joiner->clientId, joiner);
    if (resultOut != NULL)
    {
        resultOut->accepted = true;
        resultOut->memberCount = team->memberCount;
        resultOut->existingMembersQueued =
            vm_mock_service_team_enqueue_existing_members_for_joiner(
                team, leader, joiner);
    }
    else
    {
        (void)vm_mock_service_team_enqueue_existing_members_for_joiner(
            team, leader, joiner);
    }
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

/* Returns true when the member was part of an active team.  Leader departure
 * deliberately dissolves the party: group subtype 5/7 clears every client
 * roster when the removed id is the leader id. */
bool vm_mock_service_team_remove_member(vm_mock_service_client_session *leaver,
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
    vm_mock_service_team_notify_leave(team, leaver);
    if (leaderLeaves)
    {
        printf("[info][mock-service] team_disband leader=%08x/%u reason=%s\n",
               leaver->clientId, leaver->onlineRoleId, reason ? reason : "-");
        memset(team, 0, sizeof(*team));
        return true;
    }

    for (u8 member = memberIndex + 1; member < team->memberCount; ++member)
        team->memberClientIds[member - 1] = team->memberClientIds[member];
    --team->memberCount;
    team->memberClientIds[team->memberCount] = 0;
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
    if (vm_net_mock_verbose_logging_enabled())
    {
        printf("[info][mock-service] moveinfo_store client=%08x kind=%s len=%u pos=(%u,%u) reason=%s scene=%s\n",
               session->clientId,
               formatText,
               (u32)moveInfoLen,
               x,
               y,
               reason ? reason : "-",
               scene ? scene : "-");
    }
}

static void vm_mock_service_session_clear_scene_hangup(
    vm_mock_service_client_session *session,
    const char *reason);

static void vm_mock_service_session_mark_scene_pending(vm_mock_service_client_session *session,
                                                       const vm_net_mock_scene_change_target *target,
                                                       const char *reason)
{
    bool changed = false;
    const char *scene = NULL;

    if (session == NULL)
        return;
    scene = (target != NULL && vm_net_mock_scene_name_is_safe(target->scene)) ? target->scene : NULL;
    if (session->sceneHangupEnabled || session->sceneHangupRestartPending)
    {
        /* A continuation is only valid against the live nodes of its armed
         * scene.  A transfer must not carry it into a different scene. */
        vm_mock_service_session_clear_scene_hangup(session, "scene-pending");
    }
    changed = !session->sceneVisiblePending ||
              !session->sceneVisibleReady ||
              ((scene != NULL || session->scenePendingScene[0] != 0) &&
               !vm_net_mock_scene_names_equal_exact(session->scenePendingScene, scene));
    session->sceneVisibleReady = false;
    session->sceneVisiblePending = true;
    session->sceneVisibleTick = g_schedulerTick;
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
}

static void vm_mock_service_session_mark_scene_ready(vm_mock_service_client_session *session,
                                                     const char *scene,
                                                     u16 x,
                                                     u16 y,
                                                     const char *reason)
{
    bool changed = false;
    bool becameOnline = false;

    if (session == NULL || !vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
    changed = !session->sceneVisibleReady ||
              session->sceneVisiblePending ||
              !vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene, scene) ||
              session->sceneVisibleX != x ||
              session->sceneVisibleY != y;
    session->sceneVisibleReady = true;
    session->sceneVisiblePending = false;
    snprintf(session->sceneVisibleScene, sizeof(session->sceneVisibleScene), "%s", scene);
    session->sceneVisibleX = x;
    session->sceneVisibleY = y;
    session->sceneVisibleTick = g_schedulerTick;
    session->scenePendingScene[0] = 0;
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
        !vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene, scene))
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

static void vm_mock_service_session_clear_scene_hangup(
    vm_mock_service_client_session *session,
    const char *reason)
{
    if (session == NULL ||
        (!session->sceneHangupEnabled && !session->sceneHangupRestartPending))
    {
        return;
    }
    printf("[info][mock-service] scene_hangup_stop client=%08x role=%u scene=%s "
           "battle=%u restart_pending=%u reason=%s\n",
           session->clientId,
           session->onlineRoleId,
           session->sceneHangupScene[0] ? session->sceneHangupScene : "-",
           session->sceneHangupBattleSessionSerial,
           session->sceneHangupRestartPending ? 1u : 0u,
           reason ? reason : "-");
    session->sceneHangupEnabled = false;
    session->sceneHangupRestartPending = false;
    session->sceneHangupBattleSessionSerial = 0;
    session->sceneHangupRestartNotBeforeTick = 0;
    session->sceneHangupCompletedBattles = 0;
    session->sceneHangupMaxBattles = 0;
    session->sceneHangupTotalExp = 0;
    session->sceneHangupTotalGold = 0;
    session->sceneHangupTotalHpRecovered = 0;
    session->sceneHangupTotalMpRecovered = 0;
    session->sceneHangupLastAccountedBattleSerial = 0;
    session->sceneHangupScene[0] = 0;
}

static void vm_mock_service_session_mark_offline(vm_mock_service_client_session *session,
                                                 const char *reason)
{
    bool wasOnline = false;
    bool roleIdentityReassigned = false;
    char accountId[sizeof(session->accountId)];
    u32 offlineRoleId = 0;

    if (session == NULL)
        return;
    snprintf(accountId, sizeof(accountId), "%s", session->accountId);
    offlineRoleId = session->onlineRoleId;
    /* A completed user-center role migration has already removed this source
     * (account, role) parent and recreated it under the target account with a
     * new role id.  Do not let the old session recreate stale offline-timer
     * rows while it is being revoked. */
    roleIdentityReassigned = reason != NULL &&
        strcmp(reason, "user-role-transfer-import-source") == 0;
    /* The client never owns an offline timer.  Mark the exact transport
     * lifecycle boundary before clearing the session's role identity; the
     * next online practise-info request will settle only this interval. */
    if (accountId[0] != 0 && offlineRoleId != 0 && !roleIdentityReassigned)
    {
        vm_net_mock_practise_mark_offline(accountId, offlineRoleId);
        vm_net_mock_offline_exp_mark_offline(accountId, offlineRoleId);
    }
    vm_mock_service_session_clear_scene_hangup(session,
                                               reason ? reason : "offline");
    wasOnline = session->roleOnline || session->onlinePresenceValid || session->sceneVisibleReady;
    /* Notify the remaining clients before clearing the departing session's
     * cached role identity; subtype 5/7 needs that id to remove its HUD row. */
    (void)vm_mock_service_team_remove_member(session, reason ? reason : "offline");
    vm_net_mock_arena_remove_role(session->onlineRoleId,
                                  reason ? reason : "offline");
    vm_mock_service_trade_cancel_for_client(session->clientId,
                                            reason ? reason : "offline");
    vm_mock_service_duel_cancel_for_client(session->clientId,
                                           reason ? reason : "offline");
    if (wasOnline)
    {
        printf("[info][mock-service] session_offline client=%08x account=%s role=%u name=%s scene=%s pos=(%u,%u) reason=%s\n",
               session->clientId,
               session->accountId[0] ? session->accountId : "-",
               session->onlineRoleId,
               session->onlineRoleName[0] ? session->onlineRoleName : "-",
               session->sceneVisibleScene[0] ? session->sceneVisibleScene : "-",
               session->sceneVisibleX,
               session->sceneVisibleY,
               reason ? reason : "-");
    }
    session->roleOnline = false;
    session->onlinePresenceValid = false;
    session->onlineRoleId = 0;
    session->onlineRoleName[0] = 0;
    session->onlineRoleTitle[0] = 0;
    session->onlineRoleTitleBadge[0] = 0;
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
    session->transientInstanceActive = false;
    session->transientInstanceScene[0] = 0;
    session->transientInstanceX = 0;
    session->transientInstanceY = 0;
    session->transientInstanceStartedTick = 0;
    session->transientInstanceTimerMinutes = 0;
    session->transientInstanceTimerStartedMs = 0;
    session->transientInstanceReturnScene[0] = 0;
    session->transientInstanceReturnX = 0;
    session->transientInstanceReturnY = 0;
    session->transientInstanceExpiryExitCompletionPending = false;
    session->transientInstanceExpiryExitScene[0] = 0;
    session->transientInstanceExpiryExitX = 0;
    session->transientInstanceExpiryExitY = 0;
    session->transientInstanceExpiryExitAwaitingBattleClose = false;
    session->transientInstanceExpiryExitNpcReseedPending = false;
    session->transientInstanceExpiryExitNpcReseedScene[0] = 0;
    session->shopSceneNpcReseedPending = false;
    session->shopSceneNpcReseedMode = 0;
    session->shopSceneNpcReseedScene[0] = 0;
    session->shopSceneReturnPostEnterPending = false;
    session->shopSceneReturnPostEnterScene[0] = 0;
    session->shopSceneReturnPostEnterX = 0;
    session->shopSceneReturnPostEnterY = 0;
    session->taskPromptRefreshPending = false;
    session->taskPromptRefreshScene[0] = 0;
    session->practisePill17FollowupActive = false;
    session->practisePill17FollowupSeq = 0;
    session->practisePill17FollowupRoleId = 0;
    session->practisePill17FollowupMaxUse = 0;
    session->practisePill17FollowupCommittedUse = 0;
    memset(session->socialNotices, 0, sizeof(session->socialNotices));
    memset(session->chatNotices, 0, sizeof(session->chatNotices));
    session->chatNoticeHead = 0;
    session->chatNoticeCount = 0;
    session->systemWelcomeQueued = false;
    session->worldChatHistoryQueued = false;
    session->friendInviteReplyActive = false;
    session->friendInviteSourceClientId = 0;
    session->friendInviteSourceRoleId = 0;
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
    session->arenaChallengeReplyActive = false;
    session->arenaChallengeSourceRoleId = 0;
    session->arenaChallengeInitiatorPromptPending = false;
    session->pendingTeamBattleSerial = 0;
    session->instanceChallengePending = false;
    session->instanceChallengeBattlePending = false;
    session->instanceChallengeDirectPending = false;
    session->instanceChallengeDirectSceneMonster = false;
    session->instanceChallengeActorId = 0;
    session->instanceChallengeEnemyId = 0;
    session->instanceChallengeSceneIndex = 0;
    session->instanceChallengeX = 0;
    session->instanceChallengeY = 0;
    session->instanceChallengeTick = 0;
    session->instanceChallengeScene[0] = 0;
    vm_mock_service_session_clear_moveinfo(session, reason ? reason : "offline");
    vm_mock_service_session_reset_movement_rate(session, reason ? reason : "offline");
    for (u32 i = 0; i < VM_MOCK_SERVICE_PEER_SYNC_MAX; ++i)
        session->peerSync[i].visible = false;
    if (accountId[0] != 0)
    {
        vm_mock_service_account_cache_release(accountId,
                                              reason ? reason : "offline");
        vm_mock_service_account_state_release_if_offline(
            accountId, reason ? reason : "offline");
    }
}

/* Account access is owned by a small relational record rather than by the
 * credential row or a role snapshot.  A risk-admin ban must survive process
 * restart, apply to every role of the account, and never be confused with a
 * bad password.  The game transport serializes request processing and admin
 * actions with the same protocol mutex, so clearing all matching sessions
 * after this row commits is an atomic lifecycle boundary from the client's
 * point of view: no later request can retain the old account binding. */
typedef struct
{
    bool found;
    bool invalid;
    char reason[129];
} vm_mock_service_account_ban_query;

static bool g_vm_mock_service_account_ban_schema_prepared = false;

static bool vm_mock_service_account_ban_row(void *contextValue,
                                            unsigned int columnCount,
                                            const char *const *values,
                                            const size_t *lengths)
{
    vm_mock_service_account_ban_query *query =
        (vm_mock_service_account_ban_query *)contextValue;
    size_t reasonLen = 0;

    if (query == NULL || query->found || columnCount != 1 ||
        values == NULL || lengths == NULL || values[0] == NULL ||
        !vm_mysql_hex_decode(values[0], lengths[0], query->reason,
                             sizeof(query->reason) - 1, &reasonLen) ||
        reasonLen == 0 || reasonLen >= sizeof(query->reason))
    {
        if (query != NULL)
            query->invalid = true;
        return true;
    }
    query->reason[reasonLen] = 0;
    query->found = true;
    return true;
}

static bool vm_mock_service_account_ban_schema_prepare(void)
{
    if (g_vm_mock_service_account_ban_schema_prepared)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_access_bans ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "banned_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),"
            "banned_reason VARBINARY(128) NOT NULL,"
            "PRIMARY KEY(account_id),"
            "CONSTRAINT fk_account_access_bans_account "
            "FOREIGN KEY(account_id) REFERENCES accounts(account_id) ON DELETE CASCADE"
            ") ENGINE=InnoDB"))
    {
        printf("[error][mock-service] account_ban_schema_prepare error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_mock_service_account_ban_schema_prepared = true;
    return true;
}

/* Returns false only when authority could not be checked.  Callers must not
 * turn that failure into a successful login, because it would weaken an
 * existing ban if the database connection is unhealthy. */
static bool vm_mock_service_account_access_ban_check(const char *accountId,
                                                     bool *bannedOut,
                                                     char *reasonOut,
                                                     size_t reasonOutCap)
{
    char accountHex[129];
    char sql[512];
    vm_mock_service_account_ban_query query;

    if (bannedOut != NULL)
        *bannedOut = false;
    if (reasonOut != NULL && reasonOutCap != 0)
        reasonOut[0] = 0;
    if (accountId == NULL || accountId[0] == 0 ||
        vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                            sizeof(accountHex)) == 0 ||
        !vm_mock_service_account_ban_schema_prepare())
    {
        return false;
    }
    memset(&query, 0, sizeof(query));
    snprintf(sql, sizeof(sql),
             "SELECT HEX(banned_reason) FROM account_access_bans "
             "WHERE account_id=CAST(X'%s' AS CHAR) LIMIT 1", accountHex);
    if (!vm_mysql_query(sql, vm_mock_service_account_ban_row, &query) ||
        query.invalid)
    {
        return false;
    }
    if (query.found)
    {
        if (bannedOut != NULL)
            *bannedOut = true;
        if (reasonOut != NULL && reasonOutCap != 0)
            snprintf(reasonOut, reasonOutCap, "%s", query.reason);
    }
    return true;
}

static u32 vm_mock_service_account_disconnect_bound_sessions(const char *accountId,
                                                              const char *reason)
{
    vm_mock_service_client_session *session =
        g_vm_mock_service_client_sessions;
    u32 disconnected = 0;

    if (accountId == NULL || accountId[0] == 0)
        return 0;
    while (session != NULL)
    {
        vm_mock_service_client_session *next = session->next;

        if (strcmp(session->accountId, accountId) == 0)
        {
            vm_mock_service_session_mark_offline(
                session, reason ? reason : "account-access-ban");
            /* Marking the role offline is deliberately not enough: a title
             * login request with empty credentials would otherwise reuse the
             * session's account id.  Clear only after the standard offline
             * transition has released all role-owned state. */
            session->accountId[0] = 0;
            ++disconnected;
        }
        session = next;
    }
    return disconnected;
}

/* A selected-role admin repair must not evict a different role that happens
 * to belong to the same account.  There is no retained TCP socket in a
 * service session (each request owns and closes its transport socket), so the
 * established server-side disconnect boundary is: run the normal offline
 * lifecycle, then clear the account binding.  The next packet from this
 * client can no longer restore or save the stale role state. */
static u32 vm_mock_service_account_disconnect_role_sessions(const char *accountId,
                                                            u32 roleId,
                                                            const char *reason)
{
    vm_mock_service_client_session *session =
        g_vm_mock_service_client_sessions;
    u32 disconnected = 0;

    if (accountId == NULL || accountId[0] == 0 || roleId == 0)
        return 0;
    while (session != NULL)
    {
        vm_mock_service_client_session *next = session->next;

        if (strcmp(session->accountId, accountId) == 0 &&
            session->onlineRoleId == roleId)
        {
            vm_mock_service_session_mark_offline(
                session, reason ? reason : "admin-role-position-reset");
            /* Match the existing account-ban disconnect contract: offline
             * state alone is insufficient because empty-credential title
             * requests may otherwise reuse the old session binding. */
            session->accountId[0] = 0;
            ++disconnected;
        }
        session = next;
    }
    return disconnected;
}

/* Account bans belong to the account-access layer.  Every caller first saves
 * the durable denial, then uses the normal offline transition to clear any
 * existing game-session binding; it never fabricates a client kick packet. */
static bool vm_mock_service_account_ban(const char *accountId,
                                        const char *reason,
                                        u32 *disconnectedOut,
                                        const char **errorOut)
{
    char accountHex[129];
    char reasonHex[513];
    char sql[1024];

    if (disconnectedOut != NULL)
        *disconnectedOut = 0;
    if (errorOut != NULL)
        *errorOut = "account ban failed";
    if (accountId == NULL || accountId[0] == 0 || reason == NULL ||
        reason[0] == 0 || strlen(reason) > 255 ||
        !vm_mock_service_account_exists(accountId))
    {
        if (errorOut != NULL)
            *errorOut = "account not found";
        return false;
    }
    if (vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                            sizeof(accountHex)) == 0 ||
        vm_mysql_hex_encode(reason, strlen(reason), reasonHex,
                            sizeof(reasonHex)) == 0 ||
        !vm_mock_service_account_ban_schema_prepare())
    {
        if (errorOut != NULL)
            *errorOut = "account ban storage unavailable";
        return false;
    }
    snprintf(sql, sizeof(sql),
             "INSERT INTO account_access_bans(account_id,banned_at,banned_reason) "
             "VALUES(CAST(X'%s' AS CHAR),CURRENT_TIMESTAMP(3),X'%s') "
             "ON DUPLICATE KEY UPDATE banned_at=VALUES(banned_at),"
             "banned_reason=VALUES(banned_reason)",
             accountHex, reasonHex);
    if (!vm_mysql_exec(sql))
    {
        if (errorOut != NULL)
            *errorOut = "account ban storage failed";
        printf("[error][mock-admin] account_ban_save_failed account=%s error=%s\n",
               accountId, vm_mysql_last_error());
        return false;
    }
    if (disconnectedOut != NULL)
    {
        *disconnectedOut = vm_mock_service_account_disconnect_bound_sessions(
            accountId, reason);
    }
    else
    {
        (void)vm_mock_service_account_disconnect_bound_sessions(
            accountId, reason);
    }
    printf("[warn][mock-admin] account_banned account=%s reason=%s disconnected=%u\n",
           accountId, reason,
           disconnectedOut != NULL ? *disconnectedOut : 0u);
    if (errorOut != NULL)
        *errorOut = NULL;
    return true;
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

/* A CBE client restart allocates a new transport client id.  A direct process
 * exit does not necessarily emit CBMS CLIENT_DISCONNECT, so waiting for the
 * heartbeat timeout would leave the prior incarnation visible as a second
 * copy of the same role during the next scene's 2/10 baseline.  Authentication
 * is the authoritative ownership boundary: one account has one live service
 * session.  Reuse the normal offline transition so team/trade/duel state is
 * removed consistently before the new client enters a scene.
 *
 * Service sockets are request-scoped: there is no retained TCP socket that can
 * be closed while another client is idle.  Therefore a forced kick has two
 * required server-side parts: clear the old online state *and* revoke the old
 * account binding.  Merely calling session_mark_offline leaves an old title or
 * scene client authorized to send a later request and restore the same account
 * snapshot, which violates the single-login boundary. */
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
            strcmp(session->accountId, accountId) == 0)
        {
            bool oldOnline = session->roleOnline || session->onlinePresenceValid ||
                             session->sceneVisibleReady;

            printf("[info][mock-service] session_account_takeover account=%s new_client=%08x old_client=%08x old_role=%u old_scene=%s old_online=%u action=offline-and-unbind\n",
                   accountId,
                   authenticatedClientId,
                   session->clientId,
                   session->onlineRoleId,
                   session->sceneVisibleScene[0] ? session->sceneVisibleScene : "-",
                   oldOnline ? 1u : 0u);
            vm_mock_service_session_mark_offline(session, "account-login-takeover");
            /* Match the account-ban and admin-reset disconnect boundary.  A
             * later request from this client id must not restore or persist
             * the account after a newer authenticated client took ownership. */
            session->accountId[0] = 0;
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
        bool sameAccount = strcmp(session->accountId, accountId) == 0;
        printf("[info][mock-service] session_login_lifecycle_reset client=%08x old_account=%s new_account=%s same=%u scene_ready=%u role_online=%u evidence=runtime:return-title-relogin-before-scene-init\n",
               clientId,
               session->accountId,
               accountId,
               sameAccount ? 1u : 0u,
               session->sceneVisibleReady ? 1u : 0u,
               session->roleOnline ? 1u : 0u);
        vm_mock_service_session_mark_offline(
            session,
            sameAccount ? "title-login-rebind" : "account-rebind");
    }
    displacedCount = vm_mock_service_session_take_over_account(clientId, accountId);
    snprintf(session->accountId, sizeof(session->accountId), "%s", accountId);
    if (!vm_mock_service_account_cache_acquire(accountId, "session-login"))
    {
        /* Authentication has already confirmed the MySQL row.  The cache is
         * not protocol authority, so preserve the client-visible login path
         * but leave a precise operational error if its optional working set
         * could not be allocated/read. */
        printf("[error][mock-service] account_cache_acquire_failed account=%s client=%08x error=%s\n",
               accountId, clientId, vm_mysql_last_error());
    }
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
    if (session->transientInstanceActive &&
        vm_net_mock_scene_name_is_safe(session->transientInstanceScene) &&
        session->transientInstanceX != 0 && session->transientInstanceY != 0)
    {
        roleScene = session->transientInstanceScene;
        scene = session->transientInstanceScene;
        x = session->transientInstanceX;
        y = session->transientInstanceY;
    }
    else
    {
        roleScene = vm_net_mock_scene_name_is_safe(role->scene) ?
                        role->scene : vm_net_mock_current_scene_name();
        scene = roleScene;
        x = role->x;
        y = role->y;
    }
    vm_net_mock_role_default_vitals(role, &hp, &hpMax, &mp, &mpMax);
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
         vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene, roleScene)))
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
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (vm_net_mock_role_equipment_slot_is_usable(role, slot))
        {
            session->onlineEquippedItemIds[slot] = role->equippedItems[slot].itemId;
            session->onlineEquippedEnhanceLevels[slot] =
                role->equippedItems[slot].enhanceLevel;
            session->onlineEquippedEnhanceAffixes[slot] =
                role->equippedItems[slot].enhanceAffixes;
        }
        else
        {
            session->onlineEquippedItemIds[slot] = 0;
            session->onlineEquippedEnhanceLevels[slot] = 0;
            memset(&session->onlineEquippedEnhanceAffixes[slot], 0,
                   sizeof(session->onlineEquippedEnhanceAffixes[slot]));
        }
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
        vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene, scene))
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

    if (session == NULL || role == NULL)
        return false;
    if (session->transientInstanceActive &&
        vm_net_mock_scene_name_is_safe(session->transientInstanceScene) &&
        session->transientInstanceX != 0 && session->transientInstanceY != 0)
    {
        roleScene = session->transientInstanceScene;
    }
    else
    {
        if (role->x == 0 || role->y == 0)
            return false;
        roleScene = vm_net_mock_scene_name_is_safe(role->scene) ? role->scene : sceneHint;
    }
    if (!vm_net_mock_scene_name_is_safe(roleScene) ||
        (vm_net_mock_scene_name_is_safe(sceneHint) &&
         !vm_net_mock_scene_names_equal_exact(roleScene, sceneHint)))
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
                                             session->transientInstanceActive ?
                                                 session->transientInstanceX : role->x,
                                             session->transientInstanceActive ?
                                                 session->transientInstanceY : role->y,
                                             reason);
    return session->sceneVisibleReady;
}

bool vm_mock_service_session_presence_is_recent(const vm_mock_service_client_session *session)
{
    u32 age = 0;

    if (session == NULL || !session->onlinePresenceValid)
        return false;
    if (g_schedulerTick < session->onlineTick)
        return true;
    age = g_schedulerTick - session->onlineTick;
    return age <= VM_MOCK_SERVICE_ONLINE_PRESENCE_MAX_AGE_TICKS;
}

vm_mock_service_client_session *vm_mock_service_find_online_session_by_role_id(
    u32 roleId)
{
    vm_mock_service_client_session *session =
        g_vm_mock_service_client_sessions;

    if (roleId == 0)
        return NULL;
    while (session != NULL)
    {
        if (session->roleOnline && session->onlinePresenceValid &&
            session->onlineRoleId == roleId &&
            vm_mock_service_session_presence_is_recent(session))
        {
            return session;
        }
        session = session->next;
    }
    return NULL;
}

vm_mock_service_client_session *vm_mock_service_find_online_session_by_role_account(
    u32 roleId, const char *accountId)
{
    vm_mock_service_client_session *session =
        g_vm_mock_service_client_sessions;

    if (roleId == 0 || accountId == NULL || accountId[0] == 0)
        return NULL;
    while (session != NULL)
    {
        if (session->roleOnline && session->onlinePresenceValid &&
            session->onlineRoleId == roleId &&
            strcmp(session->accountId, accountId) == 0 &&
            vm_mock_service_session_presence_is_recent(session))
        {
            return session;
        }
        session = session->next;
    }
    return NULL;
}

bool vm_mock_service_is_role_online_by_role_account(u32 roleId,
                                                    const char *accountId)
{
    vm_mock_service_client_session *session =
        g_vm_mock_service_client_sessions;

    if (roleId == 0 || accountId == NULL || accountId[0] == 0)
        return false;
    while (session != NULL)
    {
        if (session->roleOnline && session->onlineRoleId == roleId &&
            strcmp(session->accountId, accountId) == 0)
        {
            return true;
        }
        session = session->next;
    }
    return false;
}

vm_mock_service_client_session *vm_mock_service_find_online_friend_session(
    const vm_mock_service_friend_record *record)
{
    if (record == NULL)
        return NULL;
    return vm_mock_service_find_online_session_by_role_account(
        record->targetRoleId, record->targetAccountId);
}

static void vm_mock_service_expire_stale_online_sessions(void)
{
    vm_mock_service_client_session *session = g_vm_mock_service_client_sessions;

    while (session != NULL)
    {
        vm_mock_service_client_session *next = session->next;
        if (session->roleOnline &&
            !vm_mock_service_session_presence_is_recent(session))
        {
            vm_mock_service_session_mark_offline(session, "heartbeat-timeout");
        }
        session = next;
    }
}

bool vm_mock_service_session_scene_is_visible(const vm_mock_service_client_session *session,
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
    return vm_net_mock_scene_names_equal_exact(session->sceneVisibleScene, scene);
}

u32 vm_mock_service_collect_visible_session_views(
    const char *scene, u32 excludedClientId,
    vm_mock_service_online_session_view *viewsOut,
    vm_mock_service_client_session **sessionsOut, u32 viewCap)
{
    vm_mock_service_client_session *session =
        g_vm_mock_service_client_sessions;
    u32 count = 0;

    if (viewsOut != NULL && viewCap != 0)
        memset(viewsOut, 0, sizeof(*viewsOut) * viewCap);
    if (sessionsOut != NULL && viewCap != 0)
        memset(sessionsOut, 0, sizeof(*sessionsOut) * viewCap);
    if (scene == NULL || viewsOut == NULL || sessionsOut == NULL || viewCap == 0 ||
        !vm_net_mock_scene_name_is_safe(scene))
    {
        return 0;
    }

    while (session != NULL && count < viewCap)
    {
        if (session->clientId != 0 && session->clientId != excludedClientId &&
            vm_mock_service_session_scene_is_visible(session, scene) &&
            vm_mock_service_session_get_online_view(session, &viewsOut[count]))
        {
            sessionsOut[count++] = session;
        }
        session = session->next;
    }
    return count;
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
    team->battleRoundActedMask = 0;
    team->battleRoundSerial = 1;
    team->battleRoundTerminalPending = false;
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
    memset(team->battleEnemyEffects, 0, sizeof(team->battleEnemyEffects));
    for (u8 i = 0; i < participantCount; ++i)
    {
        vm_mock_service_client_session *member =
            vm_mock_service_find_client_session(participantIds[i]);
        if (member == NULL)
            continue;
        team->battleMemberHpMax[i] = member->onlineHpMax ? member->onlineHpMax : 1;
        team->battleMemberHp[i] = vm_net_mock_min_u32(member->onlineHp,
                                                      team->battleMemberHpMax[i]);
        team->battleMemberMpMax[i] = member->onlineMpMax;
        team->battleMemberMp[i] = vm_net_mock_min_u32(member->onlineMp,
                                                      team->battleMemberMpMax[i]);
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
    if (!vm_mock_service_account_exists(accountId))
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
    g_vm_mock_service_active_client_id = 0;
    vm_mock_service_account_restore(state);
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid)
    {
        if (messageOut)
            *messageOut = "role db unavailable";
        vm_mock_service_account_restore(NULL);
        return NULL;
    }
    return state;
}

static void vm_mock_service_close_account_role_db_for_management(vm_mock_service_account_state *state,
                                                                 bool captureState)
{
    char accountId[64];

    memset(accountId, 0, sizeof(accountId));
    if (state != NULL)
        snprintf(accountId, sizeof(accountId), "%s", state->accountId);
    if (captureState && state != NULL)
        vm_mock_service_account_capture(state);
    vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_client_id = 0;
    vm_mock_service_account_state_release_if_offline(
        accountId, "admin-management-close");
}

/* Level changes retain their existing account-wide offline contract because
 * they rebuild the active role's derived vitals.  Position recovery has a
 * narrower, role-specific disconnect path below and must not use this helper. */
static bool vm_mock_service_account_has_live_role_session(const char *accountId)
{
    const vm_mock_service_client_session *session =
        g_vm_mock_service_client_sessions;

    if (accountId == NULL || accountId[0] == 0)
        return false;
    while (session != NULL)
    {
        if (strcmp(session->accountId, accountId) == 0 &&
            (session->roleOnline || session->onlinePresenceValid ||
             session->sceneVisibleReady))
        {
            return true;
        }
        session = session->next;
    }
    return false;
}

/* This is an administrative recovery path for a role whose saved position no
 * longer permits entry into the scene.  The administrator must name an exact
 * server-owned SCE key; its landing coordinate comes from that SCE rather than
 * an invented centre, a scene-name alias, or a global initial-scene fallback. */
static bool vm_mock_service_account_reset_role_to_scene_spawn(
    const char *accountId, const char *roleSelector, const char *targetScene,
    const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state before;
    char sourceScene[64];
    u16 targetX = 0;
    u16 targetY = 0;
    u32 targetRoleId = 0;
    u32 disconnected = 0;

    if (messageOut)
        *messageOut = "角色位置重置失败";
    if (accountId == NULL || accountId[0] == 0 ||
        roleSelector == NULL || roleSelector[0] == 0 ||
        targetScene == NULL || targetScene[0] == 0)
    {
        if (messageOut)
            *messageOut = "账号、角色或目标场景参数无效";
        return false;
    }
    if (!vm_net_mock_str_ends_with(targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(targetScene) ||
        !vm_net_mock_scene_resource_exists(targetScene))
    {
        if (messageOut)
            *messageOut = "目标场景不是服务端存在的精确 SCE 资源";
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
    targetRoleId = role->roleId;
    /* A live role owns the authoritative runtime position. Release this
     * management snapshot before the normal offline lifecycle, because that
     * transition can release the account context it references. Reload only
     * after the selected role is disconnected so no later session save can
     * overwrite the reset position. */
    vm_mock_service_close_account_role_db_for_management(state, true);
    state = NULL;
    disconnected = vm_mock_service_account_disconnect_role_sessions(
        accountId, targetRoleId, "admin-role-position-reset");
    state = vm_mock_service_open_account_role_db_for_management(accountId,
                                                                  messageOut);
    if (state == NULL)
        return false;
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL || role->roleId != targetRoleId)
    {
        if (messageOut)
            *messageOut = "角色离线后无法重新读取，未修改位置";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    snprintf(sourceScene, sizeof(sourceScene), "%s", role->scene);
    if (!vm_net_mock_get_scene_reasonable_spawn_from_sce(
            targetScene, &targetX, &targetY, NULL))
    {
        if (messageOut)
            *messageOut = "无法从目标 SCE 解析安全落点，未修改角色位置";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(targetScene, &targetX,
                                                  &targetY);
    if (targetX == 0 || targetY == 0)
    {
        if (messageOut)
            *messageOut = "目标 SCE 未提供可用安全落点，未修改角色位置";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }

    before = *role;
    snprintf(role->scene, sizeof(role->scene), "%s", targetScene);
    role->x = targetX;
    role->y = targetY;
    /* The selected role is not necessarily active.  Persist the complete
     * account snapshot in the existing transactional role writer, rather than
     * using the active-role-only position fast path. */
    if (!vm_net_mock_role_db_save_relational(
            "admin-reset-selected-scene", NULL, NULL, 0, true, NULL, NULL,
            NULL, NULL))
    {
        *role = before;
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "角色位置保存失败，未修改角色位置";
        printf("[error][mock-admin] role_selected_scene_reset_failed account=%s role=%u source_scene=%s target_scene=%s target_pos=(%u,%u) error=%s\n",
               accountId, before.roleId, sourceScene, targetScene, targetX,
               targetY, vm_mysql_last_error());
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }

    vm_mock_service_account_capture(state);
    /* The selected role was already transitioned through the standard offline
     * lifecycle before this write. Close the management view after commit;
     * a post-save disconnect would allow a live position snapshot to race the
     * reset. */
    vm_mock_service_close_account_role_db_for_management(state, false);
    printf("[info][mock-admin] role_selected_scene_reset account=%s role=%u source_scene=%s source_pos=(%u,%u) target_scene=%s target_pos=(%u,%u) landing_source=SCE disconnected=%u action=commit\n",
           accountId, targetRoleId, sourceScene, before.x, before.y,
           targetScene, targetX, targetY, disconnected);
    if (messageOut)
        *messageOut = disconnected != 0
                          ? "已强制断开该角色连接并重置位置，请重新登录"
                          : "已重置到指定场景的安全落点，重新进入游戏后生效";
    return true;
}

static bool vm_mock_service_migrate_account_role_databases(void)
{
    for (u32 i = 0; i < g_vm_mock_service_account_db.accountCount; ++i)
    {
        const char *account_id = g_vm_mock_service_account_db.accounts[i].username;
        const char *message = NULL;
        vm_mock_service_account_state *state =
            vm_mock_service_open_account_role_db_for_management(account_id, &message);
        if (state == NULL)
        {
            printf("[error][mock-service] mysql role migration failed account=%s reason=%s\n",
                   account_id[0] ? account_id : "-", message ? message : "-");
            return false;
        }
        vm_mock_service_close_account_role_db_for_management(state, true);
    }
    return true;
}


/* W 币的唯一权威是 account_wallets；后台赠送不能再要求任意一个角色
 * 存在或被选中，否则无角色账号及角色目录变动都会错误阻断账号钱包操作。 */
static bool vm_mock_service_account_add_wcoin(const char *accountId,
                                              u32 amount,
                                              const char **messageOut)
{
    u32 before = 0;
    u32 after = 0;

    if (messageOut)
        *messageOut = NULL;
    if (accountId == NULL || accountId[0] == 0 || amount == 0)
    {
        if (messageOut)
            *messageOut = "账号或 W 币数量无效";
        return false;
    }
    if (!vm_mock_service_account_wallet_credit(accountId, amount, &before, &after))
    {
        if (messageOut)
            *messageOut = "账号 W 币钱包写入失败";
        return false;
    }
    printf("[info][mock-service] account_wcoin_add user=%s scope=account add=%u before=%u after=%u\n",
           accountId,
           amount,
           before,
           after);
    return true;
}

static bool vm_mock_service_account_remove_wcoin(const char *accountId,
                                                 u32 amount,
                                                 const char **messageOut)
{
    u32 before = 0;
    u32 after = 0;
    bool insufficient = false;

    if (messageOut)
        *messageOut = NULL;
    if (accountId == NULL || accountId[0] == 0 || amount == 0)
    {
        if (messageOut)
            *messageOut = "账号或 W 币数量无效";
        return false;
    }
    if (!vm_mock_service_account_wallet_debit(accountId, amount, &before, &after,
                                              &insufficient))
    {
        if (messageOut)
            *messageOut = insufficient ? "账号 W 币余额不足"
                                      : "账号 W 币钱包写入失败";
        return false;
    }
    printf("[info][mock-service] account_wcoin_remove user=%s scope=account remove=%u before=%u after=%u\n",
           accountId,
           amount,
           before,
           after);
    return true;
}

static bool vm_mock_service_account_add_role_money(const char *accountId,
                                                   const char *roleSelector,
                                                   u32 amount,
                                                   u32 *beforeOut,
                                                   u32 *afterOut,
                                                   const char **messageOut)
{
    vm_mock_service_account_state *state =
        vm_mock_service_open_account_role_db_for_management(accountId, messageOut);
    vm_net_mock_role_state *role = NULL;
    u32 before = 0;
    u32 after = 0;

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
    before = role->money;
    after = (0xffffffffu - before < amount) ? 0xffffffffu : before + amount;
    role->money = after;
    /* The default writer persists only the active role.  Admin money edits
     * select an arbitrary role by id, so commit the complete account snapshot
     * or a non-active character silently loses its updated balance. */
    if (!vm_net_mock_role_db_save_relational("admin-money-add", NULL, NULL,
                                             0, true, NULL, NULL, NULL, NULL))
    {
        role->money = before;
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "role persistence failed";
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }
    vm_mock_service_account_capture(state);
    printf("[info][mock-service] account_money_add user=%s role=%s id=%u add=%u before=%u after=%u\n",
           accountId,
           role->name[0] ? role->name : "-",
           role->roleId,
           amount,
           before,
           after);
    if (beforeOut)
        *beforeOut = before;
    if (afterOut)
        *afterOut = after;
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}

/* Role names are stored in the account snapshot but also denormalized into
 * social and guild rows for display.  Keep those values in one transaction so
 * a failed rename cannot leave the same role with two visible names. */
static bool vm_mock_service_account_set_role_name(const char *accountId,
                                                  const char *roleSelector,
                                                  const char *newName,
                                                  const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    char accountHex[127];
    char nameHex[sizeof(((vm_net_mock_role_state *)0)->name) * 2 + 1];
    char query[1024];
    char previousName[sizeof(((vm_net_mock_role_state *)0)->name)];
    size_t nameLen = 0;
    u32 roleId = 0;
    bool transactionStarted = false;

    if (messageOut)
        *messageOut = "角色名称修改失败";
    if (accountId == NULL || accountId[0] == 0 || roleSelector == NULL ||
        roleSelector[0] == 0 || newName == NULL ||
        !vm_net_mock_request_text_looks_like_role_name(newName))
    {
        if (messageOut)
            *messageOut = "角色名称必须为 2 至 31 个有效字符";
        return false;
    }
    nameLen = strlen(newName);
    if (nameLen >= sizeof(previousName) ||
        vm_mysql_hex_encode(accountId, strlen(accountId), accountHex,
                            sizeof(accountHex)) == 0 ||
        vm_mysql_hex_encode(newName, nameLen, nameHex, sizeof(nameHex)) == 0)
    {
        if (messageOut)
            *messageOut = "角色名称或账号编码无效";
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
    roleId = role->roleId;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId != roleId &&
            strcmp(g_vm_net_mock_role_db.roles[i].name, newName) == 0)
        {
            if (messageOut)
                *messageOut = "该账号中已经有同名角色";
            vm_mock_service_close_account_role_db_for_management(state, true);
            return false;
        }
    }
    if (strcmp(role->name, newName) == 0)
    {
        if (messageOut)
            *messageOut = "角色名称没有变化";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return true;
    }

    snprintf(previousName, sizeof(previousName), "%s", role->name);
    if (!vm_mysql_exec("START TRANSACTION"))
        goto failed;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "UPDATE account_roles SET role_name=X'%s' "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             nameHex, accountHex, roleId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "UPDATE friendships SET target_role_name=X'%s' "
             "WHERE target_account_id=CAST(X'%s' AS CHAR) AND target_role_id=%u",
             nameHex, accountHex, roleId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "UPDATE guilds SET leader_role_name=X'%s' "
             "WHERE leader_account_id=CAST(X'%s' AS CHAR) AND leader_role_id=%u",
             nameHex, accountHex, roleId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "UPDATE guild_members SET role_name=X'%s' "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             nameHex, accountHex, roleId);
    if (!vm_mysql_exec(query))
        goto failed;
    snprintf(query, sizeof(query),
             "UPDATE guild_applications SET applicant_role_name=X'%s' "
             "WHERE applicant_account_id=CAST(X'%s' AS CHAR) AND applicant_role_id=%u",
             nameHex, accountHex, roleId);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    transactionStarted = false;
    snprintf(role->name, sizeof(role->name), "%s", newName);
    vm_mock_service_account_capture(state);
    vm_mock_service_close_account_role_db_for_management(state, false);
    printf("[info][mock-admin] role_name_set account=%s role=%u old=%s new=%s action=commit\n",
           accountId, roleId, previousName, newName);
    if (messageOut)
        *messageOut = "角色名称已更新";
    return true;

failed:
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-admin] role_name_set_failed account=%s role=%u error=%s\n",
           accountId, roleId, vm_mysql_last_error());
    vm_mock_service_account_capture(state);
    vm_mock_service_close_account_role_db_for_management(state, false);
    if (messageOut)
        *messageOut = "角色名称保存失败，未修改角色数据";
    return false;
}

/* The level displayed by the client is derived from cumulative EXP.  Keep the
 * coherent role mutation separate from its admin persistence boundary so both
 * the HTTP action and its deterministic regression exercise the exact same
 * level/EXP/vitals contract. */
static bool vm_mock_service_role_apply_admin_level(vm_net_mock_role_state *role,
                                                   u32 requestedLevel)
{
    u32 targetExp = 0;

    if (role == NULL || requestedLevel == 0 ||
        requestedLevel > VM_NET_MOCK_ROLE_LEVEL_CAP)
    {
        return false;
    }
    targetExp = vm_net_mock_role_level_start_exp(requestedLevel);
    if (targetExp == 0xffffffffu)
        return false;
    role->level = requestedLevel;
    role->exp = targetExp;
    /* Rebuild level/equipment-derived HP and MP maxima, while preserving the
     * role's current vitality subject to the new maxima.  Setting a level is
     * not a recovery operation. */
    vm_net_mock_role_sync_derived_vitals(role);
    return true;
}

/* An admin level change updates the level and the cumulative EXP in one
 * account snapshot; changing only role->level would be normalized back from
 * the old EXP on the next login.  Keep it offline-only for the same reason as
 * position recovery: a live session owns a separate in-memory role snapshot
 * and could otherwise later overwrite this committed state. */
static bool vm_mock_service_account_set_role_level(const char *accountId,
                                                   const char *roleSelector,
                                                   u32 requestedLevel,
                                                   const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state before;
    u32 targetExp = 0;
    u32 disconnected = 0;

    if (messageOut)
        *messageOut = "角色等级设置失败";
    if (accountId == NULL || accountId[0] == 0 ||
        roleSelector == NULL || roleSelector[0] == 0 ||
        requestedLevel == 0 || requestedLevel > VM_NET_MOCK_ROLE_LEVEL_CAP)
    {
        if (messageOut)
            *messageOut = "角色或等级参数无效";
        return false;
    }
    /* A live session owns a separate role snapshot and could overwrite this
     * edit later.  The admin contract is to complete the normal account
     * offline lifecycle first, then mutate the committed role data. */
    disconnected = vm_mock_service_account_disconnect_bound_sessions(
        accountId, "admin-set-role-level");

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

    before = *role;
    if (!vm_mock_service_role_apply_admin_level(role, requestedLevel))
    {
        if (messageOut)
            *messageOut = "目标等级不在经验曲线范围内";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    targetExp = role->exp;
    if (!vm_net_mock_role_db_save_relational("admin-set-role-level", NULL,
                                             NULL, 0, true, NULL, NULL, NULL,
                                             NULL))
    {
        *role = before;
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "角色等级保存失败，未修改角色数据";
        printf("[error][mock-admin] role_level_set_failed account=%s role=%u target_level=%u target_exp=%u error=%s\n",
               accountId, before.roleId, requestedLevel, targetExp,
               vm_mysql_last_error());
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }

    vm_mock_service_account_capture(state);
    printf("[info][mock-admin] role_level_set account=%s role=%u old_level=%u old_exp=%u new_level=%u new_exp=%u hp=%u/%u mp=%u/%u disconnected=%u action=commit\n",
           accountId, role->roleId, before.level, before.exp, role->level,
           role->exp, role->hp, role->hpMax, role->mp, role->mpMax,
           disconnected);
    if (messageOut)
        *messageOut = disconnected != 0
                          ? "已强制断开在线游戏连接；角色等级已更新，经验已重置为该等级起点"
                          : "角色等级已更新，经验已重置为该等级起点";
    vm_mock_service_close_account_role_db_for_management(state, false);
    return true;
}

static bool vm_mock_service_account_ban_for_rapid_battle(
    const char *accountId, u32 *disconnectedOut, const char **errorOut)
{
    return vm_mock_service_account_ban(
        accountId, "risk:rapid-battle-entry-within-3s", disconnectedOut,
        errorOut);
}

static bool vm_mock_service_account_ban_from_account_management(
    const char *accountId, u32 *disconnectedOut, const char **errorOut)
{
    return vm_mock_service_account_ban(
        accountId, "admin:account-management-ban", disconnectedOut, errorOut);
}

/* Admin equipment edits are an explicit offline maintenance operation.  An
 * equipped item is a durable instance: direct level changes retain its four
 * pre-rolled stage attributes, which are applied only after their +4/+8/+12/
 * +16 thresholds and must not be regenerated on a later relogin. */
static bool vm_mock_service_account_set_equipped_enhance_level(
    const char *accountId, const char *roleSelector, u32 requestedSlot,
    u32 requestedLevel, u32 *itemIdOut, u16 *previousLevelOut,
    const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state before;
    vm_net_mock_equipped_item_state *equipped = NULL;
    const vm_net_mock_equipment_catalog_item *catalog = NULL;
    u32 disconnected = 0;

    if (itemIdOut != NULL)
        *itemIdOut = 0;
    if (previousLevelOut != NULL)
        *previousLevelOut = 0;
    if (messageOut != NULL)
        *messageOut = "穿戴装备强化等级设置失败";
    if (accountId == NULL || accountId[0] == 0 ||
        roleSelector == NULL || roleSelector[0] == 0 ||
        requestedSlot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
        requestedLevel > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
    {
        if (messageOut != NULL)
            *messageOut = "角色、装备位或强化等级参数无效";
        return false;
    }

    /* A bound game session owns a separate role snapshot.  Complete its
     * normal offline lifecycle before changing a persisted equipment instance,
     * otherwise its later save could restore the old enhancement level. */
    disconnected = vm_mock_service_account_disconnect_bound_sessions(
        accountId, "admin-set-equipped-enhance");
    state = vm_mock_service_open_account_role_db_for_management(accountId,
                                                                  messageOut);
    if (state == NULL)
        return false;
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut != NULL)
            *messageOut = "角色不存在";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    equipped = &role->equippedItems[requestedSlot];
    catalog = equipped->itemId != 0 ?
                  vm_net_mock_find_equipment_catalog_item(equipped->itemId) :
                  NULL;
    if (catalog == NULL || catalog->slot != requestedSlot)
    {
        if (messageOut != NULL)
            *messageOut = "该装备位没有可验证的已穿戴装备";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }

    if (itemIdOut != NULL)
        *itemIdOut = equipped->itemId;
    if (previousLevelOut != NULL)
        *previousLevelOut = equipped->enhanceLevel;
    if (equipped->enhanceLevel == requestedLevel)
    {
        if (messageOut != NULL)
            *messageOut = "穿戴装备强化等级没有变化";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return true;
    }

    before = *role;
    equipped->enhanceLevel = (u16)requestedLevel;
    (void)vm_net_mock_equipment_enhancement_ensure_affixes(
        catalog, (u8)equipped->enhanceLevel, &equipped->enhanceAffixes,
        role->roleId ^ equipped->itemId ^
            ((requestedSlot + 1u) * 0x9e3779b9u));
    vm_net_mock_role_sync_derived_vitals(role);
    if (!vm_net_mock_role_db_save_relational(
            "admin-set-equipped-enhance", NULL, NULL, 0, true, NULL, NULL,
            NULL, NULL))
    {
        *role = before;
        vm_mock_service_account_capture(state);
        if (messageOut != NULL)
            *messageOut = "穿戴装备强化等级保存失败，未修改角色数据";
        printf("[error][mock-admin] equipped_enhance_set_failed account=%s role=%u slot=%u item=%u target=%u error=%s\n",
               accountId, before.roleId, requestedSlot,
               before.equippedItems[requestedSlot].itemId, requestedLevel,
               vm_mysql_last_error());
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }

    vm_mock_service_account_capture(state);
    printf("[info][mock-admin] equipped_enhance_set account=%s role=%u slot=%u item=%u old=%u new=%u disconnected=%u action=commit\n",
           accountId, role->roleId, requestedSlot, equipped->itemId,
           before.equippedItems[requestedSlot].enhanceLevel,
           equipped->enhanceLevel, disconnected);
    if (messageOut != NULL)
    {
        *messageOut = disconnected != 0
                          ? "已强制断开在线游戏连接；穿戴装备强化等级已更新"
                          : "穿戴装备强化等级已更新";
    }
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
    if (!vm_net_mock_role_add_backpack_item_to_role(role, itemId, count, &seq,
                                                     "admin-item-grant"))
    {
        if (messageOut)
            *messageOut = "角色背包已满，无法加入新物品";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }
    existing = vm_net_mock_role_find_backpack_item(role, itemId, seq);
    after = existing ? existing->count : count;
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

/* Account pages send an exact persisted backpack identity.  Do not fall back
 * to an item-id-only lookup here: stacks, equipment and reservoir items may
 * legitimately share an item id while having distinct instances. */
static bool vm_mock_service_account_remove_role_backpack_item(
    const char *accountId, u32 roleId, u32 itemId, u16 itemSeq,
    u32 *removedCountOut, const char **messageOut)
{
    vm_mock_service_account_state *state = NULL;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    vm_net_mock_role_state before;
    vm_net_mock_enhancement_removal_authorization enhancementRemoval;
    char roleSelector[32];
    u32 removedCount = 0;

    if (removedCountOut)
        *removedCountOut = 0;
    if (messageOut)
        *messageOut = "删除物品失败";
    memset(&enhancementRemoval, 0, sizeof(enhancementRemoval));
    if (accountId == NULL || accountId[0] == 0 || roleId == 0 || itemId == 0 ||
        itemSeq == 0)
    {
        if (messageOut)
            *messageOut = "物品定位参数无效";
        return false;
    }

    state = vm_mock_service_open_account_role_db_for_management(accountId,
                                                                 messageOut);
    if (state == NULL)
        return false;
    snprintf(roleSelector, sizeof(roleSelector), "%u", roleId);
    role = vm_net_mock_find_role_in_db(&g_vm_net_mock_role_db, roleSelector);
    if (role == NULL)
    {
        if (messageOut)
            *messageOut = "角色不存在";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }

    item = vm_net_mock_role_find_backpack_item(role, itemId, itemSeq);
    if (item == NULL || item->itemId != itemId || item->seq != itemSeq ||
        item->count == 0)
    {
        if (messageOut)
            *messageOut = "该背包物品已不存在或已变更";
        vm_mock_service_close_account_role_db_for_management(state, true);
        return false;
    }

    before = *role;
    removedCount = item->count;
    if (item->enhanceLevel != 0)
    {
        /* The exact account/role/item/sequence tuple above has already been
         * checked against the loaded durable instance.  Pass only this
         * deletion's enhancement loss to the full-snapshot safeguard. */
        enhancementRemoval.enhancedRows = 1;
        enhancementRemoval.enhancementLevelSum = item->enhanceLevel;
    }
    memset(item, 0, sizeof(*item));
    vm_net_mock_role_normalize_backpack(role);

    /* The selected role is not necessarily active.  A normal role save only
     * persists activeRoleId, so use the relational full snapshot transaction
     * and keep the account's active role unchanged. */
    if (!vm_net_mock_role_db_save_relational("account-backpack-delete", NULL,
                                             NULL, 0, true, NULL, NULL, NULL,
                                             &enhancementRemoval))
    {
        *role = before;
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "物品删除未能保存";
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }

    vm_mock_service_account_capture(state);
    if (removedCountOut)
        *removedCountOut = removedCount;
    printf("[info][account-backpack] delete account=%s role=%u item=%u seq=%u count=%u result=success\n",
           accountId, roleId, itemId, itemSeq, removedCount);
    if (messageOut)
        *messageOut = "物品已删除";
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
    VM_NET_MOCK_TEST_TASK_NPC_ACTOR_ID = 20022,
    VM_NET_MOCK_TEST_TASK_ID = 900001
};

typedef struct
{
    u32 actorId;
    /* Optional server-managed task binding.  SCE/XSE actors keep this zero
     * and continue to discover their tasks from the script resource. */
    u32 taskId;
    /* This is a property of the dynamic NPC binding, not a client-side task
     * flag.  It authorizes replacing a persisted completed (state 3) row only
     * after this NPC has actually offered the task to the active session. */
    bool taskRepeatable;
    /* 0=not repeatable, 1=unlimited, 2=daily, 3=weekly, 4=monthly.  The
     * boolean above remains a compatibility projection for older in-memory
     * data and is always derived from this policy for new DB rows. */
    u8 taskRepeatPolicy;
    u32 challengeEnemyId;
    u16 x;
    u16 y;
    /* `kind` is the server service contract (merchant/repair/trainer/etc.).
     * SCE's leading word is an entity/resource record kind, not a service
     * type, and is retained separately for source inspection. */
    u16 kind;
    u16 sceneEntityKind;
    /* Source-only SCE/legacy-DB metadata.  WT 27/11 has no orientation
     * field, so it must never be presented as a client-visible NPC setting. */
    u16 orientation;
    u16 instanceX;
    u16 instanceY;
    /* Optional kind-3 scene battle monster to be shown after entering the
     * configured instance scene.  This is deliberately independent from
     * challengeEnemyId, which belongs to the source-scene guard challenge. */
    u32 instanceSpawnEnemyId;
    /* Server policy applied only at the selected NPC's WT30/1 instance entry.
     * Zero keeps the client HUD at 00:00. */
    /* The persistent column is still named `timer_seconds` for migration
     * compatibility, but its value is the client `min` countdown unit. */
    u32 instanceTimerMinutes;
    /* Per-entry price in the role's authoritative copper unit.  Zero keeps
     * the NPC instance teleport free. */
    u32 instanceEntryCopperCost;
    u16 instanceMinLevel;
    char actorResource[64];
    char displayName[32];
    char scriptName[64];
    /* A non-normal dynamic NPC may override the one parser-backed service
     * entry shown in its 26/1 dialog.  The service kind remains the action
     * authority; these strings only replace the visible title and detail. */
    char serviceOptionName[64];
    char serviceOptionDescription[96];
    char instanceScene[64];
    bool nativeSceneActor;
} vm_net_mock_scene_npcinfo_seed;

/* Old rows persisted only the repeatable boolean.  Preserve their meaning as
 * unlimited repeats, while new rows retain the explicit cadence. */
static u8 vm_net_mock_task_repeat_policy_from_seed(
    const vm_net_mock_scene_npcinfo_seed *seed)
{
    if (seed == NULL)
        return VM_NET_MOCK_TASK_REPEAT_NEVER;
    if (seed->taskRepeatPolicy >= VM_NET_MOCK_TASK_REPEAT_UNLIMITED &&
        seed->taskRepeatPolicy <= VM_NET_MOCK_TASK_REPEAT_MONTHLY)
    {
        return seed->taskRepeatPolicy;
    }
    return seed->taskRepeatable ? VM_NET_MOCK_TASK_REPEAT_UNLIMITED
                                : VM_NET_MOCK_TASK_REPEAT_NEVER;
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

static bool vm_net_mock_ensure_actor_resource_available(
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
    /* Resource type 1 is a literal, uncompressed payload.  Only type 2
     * carries the nine-byte LZSS header consumed below.  Treating type 1 as
     * LZSS used to let a malformed scene deployment round-trip through the
     * server while the client passed the embedded header to its SCE loader. */
    if (res[0] != 2)
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

/* A named SCE portal is deliberately not represented as an edge portal.  Its
 * field 0x15 is the destination entry id used by scene_runtime_init_and_sync,
 * and fields 0x16/0x17 are the client-visible label and the real target scene.
 * The optional 0x12 field is the dungeon/background resource key; it is the
 * stable server configuration key for paid-access rules. */
typedef struct
{
    char backgroundScene[64];
    char displayName[64];
    char targetScene[64];
    u16 targetEntryId;
    u16 left;
    u16 top;
    u16 right;
    u16 bottom;
} vm_net_mock_sce_named_portal;

static bool vm_net_mock_read_sce_named_portal_string_field(const u8 *data, u32 len,
                                                            u32 *pos, u16 expectedField,
                                                            char *out, size_t outCap)
{
    if (data == NULL || pos == NULL || out == NULL || outCap == 0 || *pos + 5 > len ||
        vm_net_mock_read_le16_at(data, *pos) != 3 ||
        vm_net_mock_read_le16_at(data, *pos + 2) != expectedField)
    {
        return false;
    }
    *pos += 4;
    return vm_net_mock_read_sce_len_string(data, len, pos, out, outCap);
}

static bool vm_net_mock_parse_sce_named_portal_at(const u8 *data, u32 len, u32 off,
                                                  vm_net_mock_sce_named_portal *portal,
                                                  u32 *endOut)
{
    u32 pos = off;
    u16 kind = 0;
    u16 tileX = 0;
    u16 tileY = 0;
    u16 tileWidth = 0;
    u16 tileHeight = 0;
    u32 right = 0;
    u32 bottom = 0;

    if (data == NULL || portal == NULL || off + 12 > len)
        return false;
    memset(portal, 0, sizeof(*portal));
    kind = vm_net_mock_read_le16_at(data, pos);
    pos += 2;
    if (kind == 4)
    {
        if (pos + 8 > len)
            return false;
        tileX = vm_net_mock_read_le16_at(data, pos);
        tileY = vm_net_mock_read_le16_at(data, pos + 2);
        tileWidth = vm_net_mock_read_le16_at(data, pos + 4);
        tileHeight = vm_net_mock_read_le16_at(data, pos + 6);
        pos += 8;
    }
    else
    {
        if (pos + 10 > len || vm_net_mock_read_le16_at(data, pos) != 4)
            return false;
        tileX = vm_net_mock_read_le16_at(data, pos + 2);
        tileY = vm_net_mock_read_le16_at(data, pos + 4);
        tileWidth = vm_net_mock_read_le16_at(data, pos + 6);
        tileHeight = vm_net_mock_read_le16_at(data, pos + 8);
        pos += 10;
    }
    if (tileWidth == 0 || tileHeight == 0)
        return false;

    if (kind == 4 && pos + 3 <= len && vm_net_mock_read_le16_at(data, pos) == 0x12)
    {
        pos += 2;
        if (!vm_net_mock_read_sce_len_string(data, len, &pos, portal->backgroundScene,
                                             sizeof(portal->backgroundScene)))
        {
            return false;
        }
    }
    else if (pos + 5 <= len && vm_net_mock_read_le16_at(data, pos) == 3 &&
             vm_net_mock_read_le16_at(data, pos + 2) == 0x12)
    {
        if (!vm_net_mock_read_sce_named_portal_string_field(
                data, len, &pos, 0x12, portal->backgroundScene,
                sizeof(portal->backgroundScene)))
        {
            return false;
        }
    }

    if (!vm_net_mock_read_sce_scalar_field(data, len, &pos, 0x15,
                                           &portal->targetEntryId) ||
        !vm_net_mock_read_sce_named_portal_string_field(
            data, len, &pos, 0x16, portal->displayName, sizeof(portal->displayName)) ||
        !vm_net_mock_read_sce_named_portal_string_field(
            data, len, &pos, 0x17, portal->targetScene, sizeof(portal->targetScene)) ||
        !vm_net_mock_str_ends_with(portal->targetScene, ".sce") ||
        !vm_net_mock_scene_name_is_safe(portal->targetScene))
    {
        return false;
    }

    right = ((u32)tileX + (u32)tileWidth) * 16u;
    bottom = ((u32)tileY + (u32)tileHeight) * 16u;
    if (right > UINT16_MAX || bottom > UINT16_MAX)
        return false;
    portal->left = (u16)((u32)tileX * 16u);
    portal->top = (u16)((u32)tileY * 16u);
    portal->right = (u16)right;
    portal->bottom = (u16)bottom;
    if (endOut != NULL)
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
        if (declaredLen != 0 && declaredLen <= rawLen - 4 && raw[4] == 2)
        {
            decodedLen = vm_net_mock_decode_lzss_resource_stream(raw + 4,
                                                                  declaredLen,
                                                                  out,
                                                                  outCap);
            if (decodedLen != 0 && vm_net_mock_scene_payload_start(out, decodedLen) != 0)
                return decodedLen;
            return 0;
        }
        if (declaredLen > 1 && declaredLen <= rawLen - 4 && raw[4] == 1)
        {
            decodedLen = declaredLen - 1u;
            if (decodedLen > outCap ||
                vm_net_mock_scene_payload_start(raw + 5, decodedLen) == 0)
            {
                return 0;
            }
            memcpy(out, raw + 5, decodedLen);
            return decodedLen;
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

