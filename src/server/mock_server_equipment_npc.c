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
    backpackItem->itemId = oldItemId;
    backpackItem->count = 1;
    backpackItem->enhanceLevel = 0;
    role->equippedItemIds[slot] = newItemId;
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

    if (!vm_net_mock_role_add_backpack_item(itemId, 1, &seq))
    {
        if (reasonOut)
            *reasonOut = "bag-full";
        return false;
    }

    role->equippedItemIds[slot] = 0;
    vm_net_mock_role_sync_derived_vitals(role);
    vm_net_mock_role_db_save("item-unequip");

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
    if (rowSize != 9 && rowSize != 5)
        return false;
    for (u32 i = 0; i < parsed->materialRows; ++i)
    {
        const u8 *row = parsed->occultInfo + i * rowSize;
        u32 itemId = 0;
        u8 count = 0;

        if (rowSize == 9)
        {
            if (row[0] != 0 || row[1] != 4 ||
                row[6] != 0 || row[7] != 1)
            {
                return false;
            }
            itemId = ((u32)row[2] << 24) | ((u32)row[3] << 16) |
                     ((u32)row[4] << 8) | (u32)row[5];
            count = row[8];
        }
        else
        {
            itemId = ((u32)row[0] << 24) | ((u32)row[1] << 16) |
                     ((u32)row[2] << 8) | (u32)row[3];
            count = row[4];
        }
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
        if (0xffffffffu - power < tier * 100u * counts[i])
            power = 0xffffffffu;
        else
            power += tier * 100u * counts[i];
    }
    if (powerOut)
        *powerOut = power;
    return true;
}

static u32 vm_net_mock_equipment_enhance_success_rate(u8 level, u32 power)
{
    u32 required = ((u32)level + 1u) * 100u;
    u32 rate = 0;

    if (required == 0)
        return 0;
    if (power >= required)
        return 100;
    rate = (power * 100u) / required;
    return rate > 100 ? 100 : rate;
}

static u32 vm_net_mock_equipment_enhance_money_cost(u8 level)
{
    return ((u32)level + 1u) * 100u;
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
    bool materialsValid = false;
    bool enhancementSucceeded = false;
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
                currentLevel, materialPower);
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
                    result = 4;
                    reason = "crystal-consume-failed";
                    break;
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
                vm_net_mock_role_db_save(enhancementSucceeded
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
        if (result == 1)
        {
            for (u32 level = 0;
                 level <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++level)
            {
                if (!vm_net_mock_seq_put_u32(data1, sizeof(data1), &data1Len,
                                             (level + 1u) * 100u))
                    return 0;
            }
            for (u32 tier = 1;
                 tier <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++tier)
            {
                if (!vm_net_mock_seq_put_u32(data2, sizeof(data2), &data2Len,
                                             tier * 100u))
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
    vm_net_mock_finish_wt_packet(out, pos, 1);

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

static bool vm_net_mock_battle_roll_percent(u32 percent)
{
    if (percent == 0)
        return false;
    if (percent >= 100)
        return true;
    return (vm_net_mock_battle_reward_rand() % 100u) < percent;
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
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(g_vm_net_mock_battle_enemy_id_current);
    u32 enemyCount = vm_net_mock_battle_enemy_count_current();
    u32 dropRate = stats.dropRatePercent;
    u32 dropItemIdDefault = stats.dropItemId;

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
        if (dropItemIdOut)
            *dropItemIdOut = g_vm_net_mock_battle_rewarded_drop_item;
        if (dropSeqOut)
            *dropSeqOut = g_vm_net_mock_battle_rewarded_drop_seq;
        if (dropCountOut)
            *dropCountOut = g_vm_net_mock_battle_rewarded_drop_count;
        if (dropGrantedOut)
            *dropGrantedOut = g_vm_net_mock_battle_rewarded_drop_item != 0;
        return 0;
    }

    rewardExp = vm_net_mock_mul_capped_u32(
        vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_EXP",
                                   vm_net_mock_battle_reward_exp_for_enemy(g_vm_net_mock_battle_enemy_id_current)),
        enemyCount);
    if (g_vm_net_mock_battle_enemy_id_current == VM_NET_MOCK_BATTLE_POISON_SLIME_ID)
    {
        dropRate = vm_net_mock_env_u32_if_set("CBE_BATTLE_CHANGMING_SAN_DROP_RATE", dropRate);
        dropItemIdDefault = vm_net_mock_env_u32_if_set("CBE_BATTLE_CHANGMING_SAN_ITEM_ID",
                                                       dropItemIdDefault);
    }
    dropRate = vm_net_mock_env_u32_if_set("CBE_BATTLE_DROP_RATE", dropRate);
    dropItemIdDefault = vm_net_mock_env_u32_if_set("CBE_BATTLE_DROP_ITEM_ID", dropItemIdDefault);
    if (dropItemIdDefault != 0)
    {
        for (u32 i = 0; i < enemyCount; ++i)
        {
            if (vm_net_mock_battle_roll_percent(dropRate))
                ++dropCount;
        }
    }
    if (dropItemIdDefault != 0 && dropCount != 0)
    {
        dropItemId = dropItemIdDefault;
        dropGranted = vm_net_mock_role_add_backpack_item(dropItemId, dropCount, &dropSeq);
        if (!dropGranted)
        {
            dropItemId = 0;
            dropSeq = 0;
            dropCount = 0;
        }
    }

    g_vm_net_mock_battle_rewarded_serial = g_mockBattleOperateSessionSerial;
    g_vm_net_mock_battle_rewarded_exp = rewardExp;
    g_vm_net_mock_battle_rewarded_drop_item = dropItemId;
    g_vm_net_mock_battle_rewarded_drop_seq = dropSeq;
    g_vm_net_mock_battle_rewarded_drop_count = dropCount;
    vm_net_mock_task_progress_after_battle(g_vm_net_mock_battle_enemy_id_current,
                                           enemyCount,
                                           dropItemId,
                                           dropCount);

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
    vm_net_mock_role_service_apply_battle_wear(role);
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

static void vm_net_mock_battle_save_terminal_role_state(const char *reason)
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
    bool victory = g_mockBattleEnemyHpCurrent == 0 && roleHp > 0;
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
        if (!rewardAlreadyGranted)
            rewardGold = vm_net_mock_mul_capped_u32(
                vm_net_mock_env_u32_if_set("CBE_BATTLE_REWARD_GOLD",
                                           vm_net_mock_battle_reward_gold_for_enemy(g_vm_net_mock_battle_enemy_id_current)),
                vm_net_mock_battle_enemy_count_current());
    }
    roleMp = vm_net_mock_battle_apply_mp_recovery_once(role, roleMp, recoverMp,
                                                       &mpRecoveryApplied);
    vm_net_mock_role_apply_battle_settlement(roleHp, roleMp, rewardExp, rewardGold,
                                             &statusLastExp, &statusCurExp,
                                             &statusPercentExp, &statusLevel,
                                             &statusGold, &roleHp, &roleMp);
    vm_autotest_note("mock_battle_terminal_save reason=%s enemy=%u enemies=%u victory=%u apply_exp=%u gold=%u total_exp=%u level=%u hp=%u mp=%u recover_mp=%u recovered=%u drop=%u seq=%u count=%u\n",
                     reason ? reason : "terminal",
                     g_vm_net_mock_battle_enemy_id_current,
                     vm_net_mock_battle_enemy_count_current(),
                     victory ? 1 : 0,
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

static u32 vm_net_mock_role_revive_floor_after_death(const char *reason)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 reviveHp = 1;

    if (role == NULL)
        return 0;
    vm_net_mock_role_sync_derived_vitals(role);
    if (role->hpMax == 0)
        role->hpMax = VM_NET_MOCK_ROLE_DEFAULT_HP;
    if (reviveHp > role->hpMax)
        reviveHp = role->hpMax;
    if (role->hp == 0)
    {
        role->hp = reviveHp;
        vm_net_mock_role_db_save(reason ? reason : "death-revive-floor");
    }
    g_mockBattleRoleHpCurrent = role->hp;
    g_mockBattleRoleHpMax = role->hpMax;
    return role->hp;
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
    const char *respawnScene = vm_net_mock_role_initial_scene_name();
    u16 respawnX = VM_NET_MOCK_ROLE_INITIAL_X;
    u16 respawnY = VM_NET_MOCK_ROLE_INITIAL_Y;
    u32 levelStartExp = 0;
    u32 levelProgressExp = 0;
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
    if (role == NULL)
        return 0;

    vm_net_mock_role_sync_derived_vitals(role);
    levelStartExp = vm_net_mock_role_last_level_exp(role->exp);
    if (role->exp > levelStartExp)
        levelProgressExp = role->exp - levelStartExp;
    expPenalty = vm_net_mock_percent_ceil_u32(levelProgressExp,
                                             VM_NET_MOCK_ROLE_DEATH_EXP_PENALTY_PERCENT);
    if (expPenalty > levelProgressExp)
        expPenalty = levelProgressExp;
    role->exp -= expPenalty;
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

    if (!vm_net_mock_scene_name_is_safe(respawnScene))
        respawnScene = vm_net_mock_default_scene_name();
    (void)vm_net_mock_get_scene_reasonable_spawn_from_sce(respawnScene,
                                                          &respawnX,
                                                          &respawnY,
                                                          NULL);
    vm_net_mock_adjust_safe_player_pos_for_scene(respawnScene, &respawnX, &respawnY);
    snprintf(role->scene, sizeof(role->scene), "%s", respawnScene);
    role->x = respawnX;
    role->y = respawnY;
    vm_net_mock_role_normalize(role);
    vm_net_mock_role_db_save(reason ? reason : "battle-death");

    g_mockBattleRoleHpCurrent = role->hp;
    g_mockBattleRoleHpMax = role->hpMax;
    g_mockBattleRoleMpCurrent = role->mp;
    g_mockBattleRoleMpMax = role->mpMax;

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

static void vm_net_mock_save_player_pos_state(const char *scene, u16 x, u16 y, const char *reason)
{
    char runtimeScene[64];
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (x == 0 || y == 0)
        return;

    if (!vm_net_mock_scene_name_is_safe(scene))
    {
        if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
            scene = runtimeScene;
        else if (role != NULL && vm_net_mock_scene_name_is_safe(role->scene))
            scene = role->scene;
        else
            scene = vm_net_mock_default_scene_name();
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
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
    const char *overrideName = vm_net_mock_env_str("CBE_SCENE_KEY", "");
    static char runtimeScene[64];
    if (vm_net_mock_read_runtime_scene_name(runtimeScene, sizeof(runtimeScene)))
        return vm_net_mock_normalize_scene_name_for_enter(runtimeScene);
    if (overrideName != NULL && overrideName[0] != 0)
        return overrideName;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role != NULL && vm_net_mock_scene_name_is_safe(role->scene))
        return vm_net_mock_normalize_scene_name_for_enter(role->scene);
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
    u32 selectedGuildId;
    bool pendingGuildCreateNameValid;
    char pendingGuildCreateName[VM_NET_MOCK_GUILD_NAME_SIZE];

    u32 battleRewardedSerial;
    u32 battleRewardedExp;
    u32 battleRewardedDropItem;
    u16 battleRewardedDropSeq;
    u32 battleRewardedDropCount;
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

enum
{
    VM_MOCK_SERVICE_PEER_SYNC_MAX = 16,
    VM_MOCK_SERVICE_SOCIAL_NOTICE_MAX = 4,
    VM_MOCK_SERVICE_CHAT_NOTICE_MAX = 64,
    VM_MOCK_SERVICE_CHAT_POLL_MAX = 4,
    VM_MOCK_SERVICE_WORLD_CHAT_HISTORY_MAX = 30,
    VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX = 10,
    VM_MOCK_SERVICE_TEAM_MAX = 16,
    VM_MOCK_SERVICE_TEAM_MEMBER_MAX = 3,
    VM_MOCK_SERVICE_TEAM_BATTLE_EVENT_MAX = 8,
    VM_MOCK_SERVICE_TEAM_BATTLE_OBJECT_MAX = 2048,
    VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX = 512,
    VM_MOCK_SERVICE_DUEL_MAX = 16,
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
    VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_RESULT = 13
};

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
    char message[82];
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
    bool terminal;
    u32 serial;
    u8 sourceIndex;
    u8 deliveredMask;
    u32 operate;
    u32 damage;
    u32 sourceMpAfter;
    u32 targetHpAfter;
} vm_mock_service_duel_event;

typedef struct
{
    u32 itemId;
    u16 sourceSeq;
    u16 destinationSeq;
    u32 count;
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

typedef struct vm_mock_service_client_session
{
    u32 clientId;
    char accountId[64];
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
    u32 pendingTeamBattleSerial;
    /* HandleChallengeResponse(0x010395AA) first consumes 30/9 and its
     * confirmation callback sends 30/10 {agree}.  Keep the target on the
     * service session because 30/10 carries no actor/enemy fields. */
    bool instanceChallengePending;
    bool instanceChallengeBattlePending;
    bool instanceChallengeDirectPending;
    u32 instanceChallengeActorId;
    u32 instanceChallengeEnemyId;
    u16 instanceChallengeX;
    u16 instanceChallengeY;
    u32 instanceChallengeTick;
    char instanceChallengeScene[64];
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
    u8 battleRoundActedMask;
    u32 battleRoundSerial;
    bool battleRoundTerminalPending;
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
    u8 startPendingMask;
    u8 startedMask;
    u8 turnIndex;
    u8 terminalPendingMask;
    u32 terminalNotBeforeTick;
    u32 actionSerial;
    vm_mock_service_duel_event events[VM_MOCK_SERVICE_DUEL_EVENT_MAX];
} vm_mock_service_duel;

static vm_mock_service_team g_vm_mock_service_teams[VM_MOCK_SERVICE_TEAM_MAX];
static vm_mock_service_duel g_vm_mock_service_duels[VM_MOCK_SERVICE_DUEL_MAX];
static u32 g_vm_mock_service_duel_serial = 0;
static vm_mock_service_trade g_vm_mock_service_trades[VM_MOCK_SERVICE_TRADE_MAX];

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
    state->netMockShop17ListPending = g_netMockShop17ListPending;
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
    state->battleRewardedDropItem = g_vm_net_mock_battle_rewarded_drop_item;
    state->battleRewardedDropSeq = g_vm_net_mock_battle_rewarded_drop_seq;
    state->battleRewardedDropCount = g_vm_net_mock_battle_rewarded_drop_count;
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

    g_vm_net_mock_battle_rewarded_serial = state->battleRewardedSerial;
    g_vm_net_mock_battle_rewarded_exp = state->battleRewardedExp;
    g_vm_net_mock_battle_rewarded_drop_item = state->battleRewardedDropItem;
    g_vm_net_mock_battle_rewarded_drop_seq = state->battleRewardedDropSeq;
    g_vm_net_mock_battle_rewarded_drop_count = state->battleRewardedDropCount;
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
    printf("[info][mock-service] account_init id=%s\n", state->accountId);
    return state;
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
        duel->startPendingMask == 0 && duel->terminalPendingMask == 0)
    {
        for (u8 i = 0; i < VM_MOCK_SERVICE_DUEL_EVENT_MAX; ++i)
        {
            if (duel->events[i].valid)
                return;
        }
        printf("[info][mock-service] duel_release serial=%u first=%08x second=%08x\n",
               duel->serial, duel->clientIds[0], duel->clientIds[1]);
        memset(duel, 0, sizeof(*duel));
    }
}

static void vm_mock_service_duel_cancel_for_client(u32 clientId,
                                                    const char *reason)
{
    int index = -1;
    vm_mock_service_duel *duel = vm_mock_service_duel_find_for_client(clientId,
                                                                      &index);
    u8 peerBit = 0;

    if (duel == NULL || index < 0)
        return;
    peerBit = (u8)(1u << (1 - index));
    printf("[info][mock-service] duel_cancel serial=%u client=%08x peer=%08x "
           "started=%02x reason=%s\n",
           duel->serial, clientId, duel->clientIds[1 - index],
           duel->startedMask, reason ? reason : "cancel");
    duel->finished = true;
    duel->startPendingMask = 0;
    memset(duel->events, 0, sizeof(duel->events));
    duel->terminalPendingMask = (u8)(duel->startedMask & peerBit);
    duel->terminalNotBeforeTick = g_schedulerTick;
    vm_mock_service_duel_release_if_done(duel);
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
              !vm_net_mock_scene_names_equal_loose(session->shopSceneNpcReseedScene,
                                                   scene);
    session->shopSceneNpcReseedPending = true;
    snprintf(session->shopSceneNpcReseedScene,
             sizeof(session->shopSceneNpcReseedScene), "%s", scene);
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

static bool vm_mock_service_trade_role_add_item(vm_net_mock_role_state *role,
                                                u32 itemId,
                                                u32 count,
                                                u16 *destinationSeqOut)
{
    u8 itemCount = 0;

    if (destinationSeqOut)
        *destinationSeqOut = 0;
    if (role == NULL || itemId == 0 || count == 0)
        return false;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
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
    if (itemCount >= role->backpackCapacity ||
        itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
    {
        return false;
    }
    vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->seq = role->nextBackpackSeq ? role->nextBackpackSeq : 1;
    item->count = count;
    role->backpackItemCount = (u8)(itemCount + 1);
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
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
        "INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level) VALUES");
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
                "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u)",
                bulkRows ? "," : "", accountHex[side], roles[side].roleId,
                slot, item->itemId, item->seq, item->count,
                item->enhanceLevel);
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
    default:
        return "unknown";
    }
}

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
    if (accountLen == 0 || accountLen >= sizeof(source->accountId) ||
        sourceNameLen == 0 || sourceNameLen > 15 ||
        messageLen == 0 || messageLen > 81 ||
        !vm_mock_world_chat_table_ensure() ||
        vm_mysql_hex_encode(accountId, accountLen,
                            accountHex, sizeof(accountHex)) == 0 ||
        vm_mysql_hex_encode(sourceName, sourceNameLen,
                            sourceNameHex, sizeof(sourceNameHex)) == 0 ||
        vm_mysql_hex_encode(message, messageLen,
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
    printf("[info][mock-service] moveinfo_store client=%08x kind=%s len=%u pos=(%u,%u) reason=%s scene=%s\n",
           session->clientId,
           formatText,
           (u32)moveInfoLen,
           x,
           y,
           reason ? reason : "-",
           scene ? scene : "-");
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
    vm_mock_service_session_clear_moveinfo(session, "scene-pending");
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
              !vm_net_mock_scene_names_equal_loose(session->sceneVisibleScene, scene) ||
              session->sceneVisibleX != x ||
              session->sceneVisibleY != y;
    session->sceneVisibleReady = true;
    session->sceneVisiblePending = false;
    snprintf(session->sceneVisibleScene, sizeof(session->sceneVisibleScene), "%s", scene);
    session->sceneVisibleX = x;
    session->sceneVisibleY = y;
    session->sceneVisibleTick = g_schedulerTick;
    session->scenePendingScene[0] = 0;
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
        !vm_net_mock_scene_names_equal_loose(session->sceneVisibleScene, scene))
    {
        vm_mock_service_session_mark_scene_ready(session, scene, x, y, "moveinfo-upload");
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
    session->shopSceneNpcReseedPending = false;
    session->shopSceneNpcReseedScene[0] = 0;
    session->taskPromptRefreshPending = false;
    session->taskPromptRefreshScene[0] = 0;
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
    session->pendingTeamBattleSerial = 0;
    session->instanceChallengePending = false;
    session->instanceChallengeBattlePending = false;
    session->instanceChallengeDirectPending = false;
    session->instanceChallengeActorId = 0;
    session->instanceChallengeEnemyId = 0;
    session->instanceChallengeX = 0;
    session->instanceChallengeY = 0;
    session->instanceChallengeTick = 0;
    session->instanceChallengeScene[0] = 0;
    vm_mock_service_session_clear_moveinfo(session, reason ? reason : "offline");
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

static void vm_mock_service_bind_session_account(u32 clientId, const char *accountId)
{
    vm_mock_service_client_session *session = NULL;
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
    snprintf(session->accountId, sizeof(session->accountId), "%s", accountId);
    printf("[info][mock-service] session_bind client=%08x account=%s\n", clientId, accountId);
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
    if (captureState && state != NULL)
        vm_mock_service_account_capture(state);
    vm_mock_service_account_restore(NULL);
    g_vm_mock_service_active_client_id = 0;
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


static bool vm_mock_service_account_add_role_wcoin(const char *accountId,
                                                   const char *roleSelector,
                                                   u32 amount,
                                                   const char **messageOut)
{
    vm_mock_service_account_state *state =
        vm_mock_service_open_account_role_db_for_management(accountId, messageOut);
    vm_net_mock_role_state *role = NULL;
    u32 before = 0;
    u32 after = 0;

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
    before = role->wcoin;
    after = vm_net_mock_role_add_wcoin(role, amount);
    if (!vm_net_mock_role_db_save("admin-wcoin-add"))
    {
        role->wcoin = before;
        vm_mock_service_account_capture(state);
        if (messageOut)
            *messageOut = "role persistence failed";
        vm_mock_service_close_account_role_db_for_management(state, false);
        return false;
    }
    vm_mock_service_account_capture(state);
    printf("[info][mock-service] account_wcoin_add user=%s role=%s id=%u add=%u before=%u after=%u\n",
           accountId,
           role->name[0] ? role->name : "-",
           role->roleId,
           amount,
           before,
           after);
    vm_mock_service_close_account_role_db_for_management(state, false);
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
    if (!vm_net_mock_role_db_save("admin-money-add"))
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
    u32 challengeEnemyId;
    u16 x;
    u16 y;
    u16 kind;
    u16 orientation;
    u16 instanceX;
    u16 instanceY;
    u16 instanceMinLevel;
    char actorResource[64];
    char displayName[32];
    char scriptName[64];
    char instanceScene[64];
} vm_net_mock_scene_npcinfo_seed;

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

