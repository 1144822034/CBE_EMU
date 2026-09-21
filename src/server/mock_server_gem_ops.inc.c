/* Pixi-only gem socket ops: 29/17 embed, 29/18 remove, 29/19 combine,
 * 29/20 type exchange, 29/21 crystal16→gems, 29/22 fragment combine.
 * Included after crystal extract in mock_server_equipment_npc.c. */

static vm_mock_service_trade *vm_mock_service_trade_find_for_client(u32 clientId,
                                                                    int *indexOut);

static bool vm_net_mock_gem_ops_trade_busy(void)
{
    int index = -1;
    vm_mock_service_trade *trade = NULL;

    if (g_vm_mock_service_active_client_id == 0)
        return false;
    trade = vm_mock_service_trade_find_for_client(g_vm_mock_service_active_client_id,
                                                   &index);
    (void)index;
    return trade != NULL && trade->active;
}

static bool vm_net_mock_gem_bag_can_accept(vm_net_mock_role_state *role, u32 itemId)
{
    u32 itemCount = 0;
    u32 freeSlots = 0;

    if (role == NULL || itemId == 0)
        return false;
    if (vm_net_mock_role_find_backpack_item(role, itemId, 0) != NULL)
        return true;
    itemCount = vm_net_mock_role_backpack_count(role);
    freeSlots = role->backpackCapacity > itemCount
                    ? (u32)role->backpackCapacity - itemCount
                    : 0;
    return freeSlots > 0;
}

static bool vm_net_mock_gem_bag_can_accept_n_stacks(vm_net_mock_role_state *role,
                                                    u32 needNewStacks)
{
    u32 itemCount = 0;
    u32 freeSlots = 0;

    if (role == NULL)
        return false;
    if (needNewStacks == 0)
        return true;
    itemCount = vm_net_mock_role_backpack_count(role);
    freeSlots = role->backpackCapacity > itemCount
                    ? (u32)role->backpackCapacity - itemCount
                    : 0;
    return freeSlots >= needNewStacks;
}

typedef struct
{
    bool worn;
    u8 slot;
    vm_net_mock_backpack_item_state *bag;
    u32 itemId;
    u8 *gemReflect;
    u8 *gemCombo;
    u8 *gemRevive;
} vm_net_mock_gem_equip_ref;

/* loc: 0 = bag then worn (legacy), 1 = backpack only, 2 = worn slot only.
 * Worn body seq is slot+1 and can collide with a backpack seq. */
static u8 vm_net_mock_gem_loc_from_payload(const u8 *payload, u32 payloadLen)
{
    u32 worn = 0;

    if (payload == NULL || payloadLen == 0 ||
        !vm_net_mock_get_object_number_field(payload, payloadLen, "worn", &worn))
        return 0;
    return worn ? 2 : 1;
}

static bool vm_net_mock_gem_resolve_equip(vm_net_mock_role_state *role, u16 equipSeq,
                                          u8 loc,
                                          vm_net_mock_gem_equip_ref *refOut,
                                          const char **reasonOut)
{
    vm_net_mock_backpack_item_state *bag = NULL;
    u8 slot = 0;

    if (refOut)
        memset(refOut, 0, sizeof(*refOut));
    if (reasonOut)
        *reasonOut = "ok";
    if (role == NULL || equipSeq == 0 || refOut == NULL)
    {
        if (reasonOut)
            *reasonOut = "bad-args";
        return false;
    }

    if (loc != 2)
    {
        bag = vm_net_mock_role_find_backpack_item(role, 0, equipSeq);
        if (bag != NULL && bag->count != 0 &&
            vm_net_mock_find_equipment_catalog_item(bag->itemId) != NULL)
        {
            refOut->worn = false;
            refOut->bag = bag;
            refOut->itemId = bag->itemId;
            refOut->gemReflect = &bag->gemReflectLevel;
            refOut->gemCombo = &bag->gemComboLevel;
            refOut->gemRevive = &bag->gemReviveLevel;
            return true;
        }
        if (loc == 1)
        {
            if (reasonOut)
                *reasonOut = "equip-not-found";
            return false;
        }
    }

    if (equipSeq >= 1 && equipSeq <= (u16)VM_NET_MOCK_EQUIP_SLOT_COUNT)
    {
        slot = (u8)(equipSeq - 1u);
        if (role->equippedItemIds[slot] != 0 &&
            vm_net_mock_find_equipment_catalog_item(
                role->equippedItemIds[slot]) != NULL)
        {
            refOut->worn = true;
            refOut->slot = slot;
            refOut->itemId = role->equippedItemIds[slot];
            refOut->gemReflect = &role->equippedGemReflect[slot];
            refOut->gemCombo = &role->equippedGemCombo[slot];
            refOut->gemRevive = &role->equippedGemRevive[slot];
            return true;
        }
    }

    if (reasonOut)
        *reasonOut = "equip-not-found";
    return false;
}

static u8 *vm_net_mock_gem_slot_ptr(vm_net_mock_gem_equip_ref *ref, u8 gemType)
{
    if (ref == NULL)
        return NULL;
    switch (gemType)
    {
    case VM_NET_MOCK_GEM_TYPE_REFLECT:
        return ref->gemReflect;
    case VM_NET_MOCK_GEM_TYPE_COMBO:
        return ref->gemCombo;
    case VM_NET_MOCK_GEM_TYPE_REVIVE:
        return ref->gemRevive;
    default:
        return NULL;
    }
}

static u32 vm_net_mock_gem_random_lv1_item_id(u32 seed)
{
    u8 type = (u8)((seed % 3u) + 1u);
    return vm_net_mock_gem_item_id_for(type, 1);
}

static u32 vm_net_mock_gem_ops_seed(vm_net_mock_role_state *role, u16 seq)
{
    return (u32)g_schedulerTick ^ (role ? role->roleId * 0x9e3779b9u : 0u) ^
           ((u32)seq * 0xc2b2ae35u);
}

/*
 * 29/17 镶嵌: { equipseq, gemseq }
 * result: 1=ok 2=bad 3=trade 4=slot occupied 5=consume fail
 */
static u32 vm_net_mock_build_gem_embed_response(const u8 *request, u32 requestLen,
                                                u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 equipSeqValue = 0;
    u32 gemSeqValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 equipSeq = 0;
    u16 gemSeq = 0;
    u32 gemItemId = 0;
    u32 gemRemaining = 0;
    u8 gemType = 0;
    u8 gemLevel = 0;
    const char *reason = "ok";
    const char *message = "";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *gem = NULL;
    vm_net_mock_gem_equip_ref equip;
    u8 *slotPtr = NULL;

    memset(&equip, 0, sizeof(equip));
    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 17 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "equipseq", &equipSeqValue) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "gemseq", &gemSeqValue) ||
        equipSeqValue == 0 || equipSeqValue > 0xffffu || gemSeqValue == 0 ||
        gemSeqValue > 0xffffu)
    {
        return 0;
    }
    equipSeq = (u16)equipSeqValue;
    gemSeq = (u16)gemSeqValue;

    role = vm_net_mock_active_role();
    if (role == NULL)
    {
        result = 2;
        reason = "no-role";
        message = "角色无效";
    }
    else if (vm_net_mock_gem_ops_trade_busy())
    {
        result = 3;
        reason = "trade-busy";
        message = "交易中无法镶嵌";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        gem = vm_net_mock_role_find_backpack_item(role, 0, gemSeq);
        if (gem == NULL || gem->count == 0 ||
            !vm_net_mock_gem_parse_item(gem->itemId, &gemType, &gemLevel))
        {
            result = 2;
            reason = "gem-not-found";
            message = "宝石不存在";
        }
        else if (gemLevel == 0 || gemLevel > vm_net_mock_gem_max_level())
        {
            result = 2;
            reason = "gem-level-invalid";
            message = "宝石等级无效";
        }
        else if (!vm_net_mock_gem_resolve_equip(
                     role, equipSeq,
                     vm_net_mock_gem_loc_from_payload(object.payload,
                                                      object.payloadLen),
                     &equip, &reason))
        {
            result = 2;
            message = "装备不存在";
        }
        else if (!equip.worn)
        {
            result = 2;
            reason = "not-worn";
            message = "只能镶嵌穿戴中的装备";
        }
        else if (role->equippedEnhanceLevels[equip.slot] <
                 (u16)VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        {
            result = 2;
            reason = "enhance-too-low";
            message = "装备需强化到+16";
        }
        else
        {
            slotPtr = vm_net_mock_gem_slot_ptr(&equip, gemType);
            if (slotPtr == NULL)
            {
                result = 2;
                reason = "bad-gem-type";
                message = "宝石类型无效";
            }
            else if (*slotPtr != 0)
            {
                result = 4;
                reason = "slot-occupied";
                message = "该属性孔已镶嵌";
            }
            else
            {
                gemItemId = gem->itemId;
                if (!vm_net_mock_role_consume_backpack_item(role, gemItemId, gemSeq,
                                                             1u, &gemRemaining))
                {
                    result = 5;
                    reason = "consume-failed";
                    message = "扣除宝石失败";
                }
                else
                {
                    *slotPtr = gemLevel;
                    if (!vm_net_mock_role_db_save("gem-embed"))
                        vm_net_mock_role_mark_inventory_dirty("gem-embed");
                    reason = "embed-ok";
                    message = "镶嵌成功";
                }
            }
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 17, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "equipseq", equipSeq) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "gemseq", gemSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", gemItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining", gemRemaining) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "gemtype", gemType) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "gemlevel", gemLevel) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message", message))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_gem_embed equip=%u gem=%u item=%u type=%u "
           "level=%u remain=%u result=%u reason=%s resp=29/17\n",
           equipSeq, gemSeq, gemItemId, gemType, gemLevel, gemRemaining, result,
           reason);
    vm_autotest_note(
        "mock_gem_embed equip=%u gem=%u result=%u reason=%s evidence=pixijs-only-29/17\n",
        equipSeq, gemSeq, result, reason);
    return pos;
}

/*
 * 29/18 拆除: { equipseq, gemtype }
 * result: 1=ok 2=bad 3=trade 4=empty 5=bag full 6=grant fail
 * message: 背包空间不足 / …
 */
static u32 vm_net_mock_build_gem_remove_response(const u8 *request, u32 requestLen,
                                                  u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 equipSeqValue = 0;
    u32 gemTypeValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 equipSeq = 0;
    u8 gemType = 0;
    u8 gemLevel = 0;
    u32 grantItemId = 0;
    u16 grantSeq = 0;
    u32 grantCount = 0;
    const char *reason = "ok";
    const char *message = "";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_gem_equip_ref equip;
    u8 *slotPtr = NULL;

    memset(&equip, 0, sizeof(equip));
    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 18 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "equipseq", &equipSeqValue) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "gemtype", &gemTypeValue) ||
        equipSeqValue == 0 || equipSeqValue > 0xffffu)
    {
        return 0;
    }
    equipSeq = (u16)equipSeqValue;
    gemType = (u8)gemTypeValue;

    role = vm_net_mock_active_role();
    if (role == NULL)
    {
        result = 2;
        reason = "no-role";
        message = "角色无效";
    }
    else if (vm_net_mock_gem_ops_trade_busy())
    {
        result = 3;
        reason = "trade-busy";
        message = "交易中无法拆除";
    }
    else if (gemType < VM_NET_MOCK_GEM_TYPE_REFLECT ||
             gemType > VM_NET_MOCK_GEM_TYPE_REVIVE)
    {
        result = 2;
        reason = "bad-gem-type";
        message = "宝石类型无效";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        if (!vm_net_mock_gem_resolve_equip(
                role, equipSeq,
                vm_net_mock_gem_loc_from_payload(object.payload, object.payloadLen),
                &equip, &reason))
        {
            result = 2;
            message = "装备不存在";
        }
        else
        {
            slotPtr = vm_net_mock_gem_slot_ptr(&equip, gemType);
            gemLevel = slotPtr ? *slotPtr : 0;
            grantItemId = vm_net_mock_gem_item_id_for(gemType, gemLevel);
            if (slotPtr == NULL || gemLevel == 0 || grantItemId == 0)
            {
                result = 4;
                reason = "slot-empty";
                message = "该孔未镶嵌宝石";
            }
            else if (!vm_net_mock_gem_bag_can_accept(role, grantItemId))
            {
                result = 5;
                reason = "bag-full";
                message = "背包空间不足";
            }
            else
            {
                *slotPtr = 0;
                if (!vm_net_mock_role_add_backpack_item_to_role(
                        role, grantItemId, 1, 0, &grantSeq, "gem-remove", NULL, 0))
                {
                    *slotPtr = gemLevel;
                    result = 6;
                    reason = "grant-failed";
                    message = "退回宝石失败";
                }
                else
                {
                    vm_net_mock_backpack_item_state *granted =
                        vm_net_mock_role_find_backpack_item(role, grantItemId,
                                                            grantSeq);
                    grantCount = granted ? granted->count : 1;
                    if (!vm_net_mock_role_db_save("gem-remove"))
                        vm_net_mock_role_mark_inventory_dirty("gem-remove");
                    reason = "remove-ok";
                    message = "拆除成功";
                }
            }
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 18, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "equipseq", equipSeq) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "gemtype", gemType) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "gemlevel", gemLevel) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", grantItemId) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", grantSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destcount", grantCount) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message", message))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_gem_remove equip=%u type=%u level=%u item=%u "
           "result=%u reason=%s resp=29/18\n",
           equipSeq, gemType, gemLevel, grantItemId, result, reason);
    vm_autotest_note(
        "mock_gem_remove equip=%u type=%u result=%u reason=%s evidence=pixijs-only-29/18\n",
        equipSeq, gemType, result, reason);
    return pos;
}

/*
 * 29/19 宝石合成: { seq, times? }  每次 3×同级同属性 → 1×Lv+1
 * times 缺省为 1。批量合成把本格能凑满的次数一次做完（只升 1 级，不连跳）。
 * result: 1=ok 2=bad 3=bag full 4=count 5=max 6=trade
 */
static u32 vm_net_mock_build_gem_combine_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    u32 offset = 4;
    u32 seqValue = 0;
    u32 timesValue = 0;
    u32 times = 1;
    u32 needCost = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 srcSeq = 0;
    u16 destSeq = 0;
    u32 srcItemId = 0;
    u32 destItemId = 0;
    u32 srcRemaining = 0;
    u32 destCount = 0;
    u8 gemType = 0;
    u8 gemLevel = 0;
    const u32 cost = (u32)VM_NET_MOCK_GEM_COMBINE_COST;
    const char *reason = "ok";
    const char *message = "";
    char messageStore[48];
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *src = NULL;
    vm_net_mock_backpack_item_state *dest = NULL;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 19 || object.payloadLen == 0)
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
    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                            "times", &timesValue))
        times = timesValue;

    role = vm_net_mock_active_role();
    if (times == 0 || cost == 0 || times > (0xffffffffu / cost))
    {
        result = 2;
        reason = "bad-times";
        message = "合成数量无效";
    }
    else if (role == NULL)
    {
        result = 2;
        reason = "no-role";
        message = "角色无效";
    }
    else if (vm_net_mock_gem_ops_trade_busy())
    {
        result = 6;
        reason = "trade-busy";
        message = "交易中无法合成";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        src = vm_net_mock_role_find_backpack_item(role, 0, srcSeq);
        if (src == NULL || src->count == 0 ||
            !vm_net_mock_gem_parse_item(src->itemId, &gemType, &gemLevel))
        {
            result = 2;
            reason = "gem-not-found";
            message = "不是宝石";
        }
        else if (gemLevel >= vm_net_mock_gem_max_level())
        {
            result = 5;
            reason = "max-level";
            message = "已达最高等级";
        }
        else if ((needCost = cost * times), src->count < needCost)
        {
            result = 4;
            reason = "count-lt-3";
            message = times > 1 ? "宝石不足" : "宝石不足3颗";
        }
        else
        {
            u32 itemCount = vm_net_mock_role_backpack_count(role);
            u32 freeSlots = role->backpackCapacity > itemCount
                                ? (u32)role->backpackCapacity - itemCount
                                : 0;
            bool freesSlot = (src->count == needCost);
            srcItemId = src->itemId;
            destItemId = vm_net_mock_gem_item_id_for(gemType, (u8)(gemLevel + 1u));
            dest = vm_net_mock_role_find_backpack_item(role, destItemId, 0);
            if (destItemId == 0)
            {
                result = 2;
                reason = "dest-invalid";
                message = "合成目标无效";
            }
            else if (dest == NULL && freeSlots == 0 && !freesSlot)
            {
                result = 3;
                reason = "bag-full";
                message = "背包空间不足";
            }
        }
    }

    if (result == 1 && role != NULL && src != NULL)
    {
        if (!vm_net_mock_role_consume_backpack_item(role, srcItemId, srcSeq,
                                                     needCost, &srcRemaining))
        {
            result = 4;
            reason = "consume-failed";
            message = "扣除失败";
        }
        else if (!vm_net_mock_role_add_backpack_item_to_role(
                     role, destItemId, times, 0, &destSeq, "gem-combine", NULL,
                     0))
        {
            (void)vm_net_mock_role_add_backpack_item_to_role(
                role, srcItemId, needCost, 0, NULL, "gem-combine-rollback", NULL,
                0);
            result = 3;
            reason = "grant-failed";
            message = "背包空间不足";
            srcRemaining = srcRemaining + needCost;
        }
        else
        {
            dest = vm_net_mock_role_find_backpack_item(role, destItemId, destSeq);
            destCount = dest ? dest->count : times;
            if (!vm_net_mock_role_db_save("gem-combine"))
                vm_net_mock_role_mark_inventory_dirty("gem-combine");
            reason = "combine-ok";
            if (times > 1)
            {
                snprintf(messageStore, sizeof(messageStore),
                         "合成成功，获得%u颗", times);
                message = messageStore;
            }
            else
                message = "合成成功";
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 19, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", srcSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", srcItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destid", destItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining", srcRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destcount", destCount) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "times", times) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message", message))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_gem_combine seq=%u src=%u dest=%u times=%u "
           "result=%u reason=%s resp=29/19\n",
           srcSeq, srcItemId, destItemId, times, result, reason);
    vm_autotest_note(
        "mock_gem_combine seq=%u times=%u result=%u reason=%s evidence=pixijs-only-29/19\n",
        srcSeq, times, result, reason);
    return pos;
}

/*
 * 29/20 属性兑换: { seq, desttype }  2×Lv1 → 1×其它属性 Lv1
 * result: 1=ok 2=bad 3=bag 4=count 5=not-lv1 6=same-type 7=trade
 */
static u32 vm_net_mock_build_gem_type_exchange_response(const u8 *request,
                                                         u32 requestLen, u8 *out,
                                                         u32 outCap)
{
    u32 offset = 4;
    u32 seqValue = 0;
    u32 destTypeValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 srcSeq = 0;
    u16 destSeq = 0;
    u32 srcItemId = 0;
    u32 destItemId = 0;
    u32 srcRemaining = 0;
    u32 destCount = 0;
    u8 gemType = 0;
    u8 gemLevel = 0;
    u8 destType = 0;
    const u32 cost = (u32)VM_NET_MOCK_GEM_TYPE_EXCHANGE_COST;
    const char *reason = "ok";
    const char *message = "";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *src = NULL;
    vm_net_mock_backpack_item_state *dest = NULL;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 20 || object.payloadLen == 0)
    {
        return 0;
    }
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "seq", &seqValue) ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "desttype", &destTypeValue) ||
        seqValue == 0 || seqValue > 0xffffu)
    {
        return 0;
    }
    srcSeq = (u16)seqValue;
    destType = (u8)destTypeValue;

    role = vm_net_mock_active_role();
    if (role == NULL)
    {
        result = 2;
        reason = "no-role";
        message = "角色无效";
    }
    else if (vm_net_mock_gem_ops_trade_busy())
    {
        result = 7;
        reason = "trade-busy";
        message = "交易中无法兑换";
    }
    else if (destType < VM_NET_MOCK_GEM_TYPE_REFLECT ||
             destType > VM_NET_MOCK_GEM_TYPE_REVIVE)
    {
        result = 2;
        reason = "bad-dest-type";
        message = "目标属性无效";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        src = vm_net_mock_role_find_backpack_item(role, 0, srcSeq);
        if (src == NULL || src->count == 0 ||
            !vm_net_mock_gem_parse_item(src->itemId, &gemType, &gemLevel))
        {
            result = 2;
            reason = "gem-not-found";
            message = "不是宝石";
        }
        else if (gemLevel != 1)
        {
            result = 5;
            reason = "not-lv1";
            message = "仅一级宝石可兑换";
        }
        else if (gemType == destType)
        {
            result = 6;
            reason = "same-type";
            message = "请选择其它属性";
        }
        else if (src->count < cost)
        {
            result = 4;
            reason = "count-lt-2";
            message = "宝石不足2颗";
        }
        else
        {
            u32 itemCount = vm_net_mock_role_backpack_count(role);
            u32 freeSlots = role->backpackCapacity > itemCount
                                ? (u32)role->backpackCapacity - itemCount
                                : 0;
            bool freesSlot = (src->count == cost);
            srcItemId = src->itemId;
            destItemId = vm_net_mock_gem_item_id_for(destType, 1);
            dest = vm_net_mock_role_find_backpack_item(role, destItemId, 0);
            if (destItemId == 0)
            {
                result = 2;
                reason = "dest-invalid";
                message = "兑换目标无效";
            }
            else if (dest == NULL && freeSlots == 0 && !freesSlot)
            {
                result = 3;
                reason = "bag-full";
                message = "背包空间不足";
            }
        }
    }

    if (result == 1 && role != NULL && src != NULL)
    {
        if (!vm_net_mock_role_consume_backpack_item(role, srcItemId, srcSeq, cost,
                                                     &srcRemaining))
        {
            result = 4;
            reason = "consume-failed";
            message = "扣除失败";
        }
        else if (!vm_net_mock_role_add_backpack_item_to_role(
                     role, destItemId, 1, 0, &destSeq, "gem-type-exchange", NULL,
                     0))
        {
            (void)vm_net_mock_role_add_backpack_item_to_role(
                role, srcItemId, cost, 0, NULL, "gem-type-exchange-rollback",
                NULL, 0);
            result = 3;
            reason = "grant-failed";
            message = "背包空间不足";
            srcRemaining = srcRemaining + cost;
        }
        else
        {
            dest = vm_net_mock_role_find_backpack_item(role, destItemId, destSeq);
            destCount = dest ? dest->count : 1;
            if (!vm_net_mock_role_db_save("gem-type-exchange"))
                vm_net_mock_role_mark_inventory_dirty("gem-type-exchange");
            reason = "exchange-ok";
            message = "兑换成功";
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 20, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", srcSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", srcItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destid", destItemId) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "desttype", destType) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining", srcRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destcount", destCount) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message", message))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_gem_type_exchange seq=%u src=%u dest=%u "
           "desttype=%u result=%u reason=%s resp=29/20\n",
           srcSeq, srcItemId, destItemId, destType, result, reason);
    vm_autotest_note(
        "mock_gem_type_exchange seq=%u result=%u reason=%s evidence=pixijs-only-29/20\n",
        srcSeq, result, reason);
    return pos;
}

/*
 * 29/21 十六级玄晶→宝石: { seq }  1×916 → N×随机 Lv1
 * N = server_gem_config.crystal16_gem_count（默认 5，后台 1–20）
 * 须 crystal16_exchange_enabled=1，否则 result=7
 * result: 1=ok 2=bad 3=bag 4=trade 5=consume 6=grant 7=disabled
 */
static u32 vm_net_mock_build_gem_from_crystal16_response(const u8 *request,
                                                         u32 requestLen, u8 *out,
                                                         u32 outCap)
{
    u32 offset = 4;
    u32 seqValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 srcSeq = 0;
    u32 srcItemId = 0;
    u32 srcRemaining = 0;
    u32 granted = 0;
    u32 needStacks = 0;
    u32 grantIds[VM_NET_MOCK_GEM_CRYSTAL16_GRANT_MAX];
    u32 i = 0;
    u32 grantN = vm_net_mock_gem_crystal16_grant();
    const char *reason = "ok";
    const char *message = "";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *src = NULL;
    u32 seed = 0;

    if (grantN == 0 || grantN > (u32)VM_NET_MOCK_GEM_CRYSTAL16_GRANT_MAX)
        grantN = (u32)VM_NET_MOCK_GEM_CRYSTAL16_GRANT_DEFAULT;
    memset(grantIds, 0, sizeof(grantIds));
    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 21 || object.payloadLen == 0)
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
        message = "角色无效";
    }
    else if (!vm_net_mock_gem_crystal16_exchange_enabled())
    {
        result = 7;
        reason = "disabled";
        message = "十六级玄晶兑换未开放";
    }
    else if (vm_net_mock_gem_ops_trade_busy())
    {
        result = 4;
        reason = "trade-busy";
        message = "交易中无法兑换";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        src = vm_net_mock_role_find_backpack_item(role, 0, srcSeq);
        if (src == NULL || src->count == 0)
        {
            result = 2;
            reason = "crystal-not-found";
            message = "玄晶不存在";
        }
        else if (src->itemId != (u32)VM_NET_MOCK_GEM_CRYSTAL16_ITEM_ID)
        {
            result = 2;
            reason = "not-crystal16";
            message = "需要十六级玄晶";
        }
        else
        {
            srcItemId = src->itemId;
            seed = vm_net_mock_gem_ops_seed(role, srcSeq);
            for (i = 0; i < grantN; ++i)
                grantIds[i] = vm_net_mock_gem_random_lv1_item_id(seed + i * 17u);
            /* Count new stacks among grants that are not already in bag. */
            for (i = 0; i < grantN; ++i)
            {
                u32 j = 0;
                bool seen = false;
                if (grantIds[i] == 0)
                    continue;
                if (vm_net_mock_role_find_backpack_item(role, grantIds[i], 0) !=
                    NULL)
                    continue;
                for (j = 0; j < i; ++j)
                {
                    if (grantIds[j] == grantIds[i])
                    {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                    needStacks += 1u;
            }
            if (src->count == 1 && needStacks > 0)
                needStacks -= 1u; /* last crystal frees one slot */
            if (!vm_net_mock_gem_bag_can_accept_n_stacks(role, needStacks))
            {
                result = 3;
                reason = "bag-full";
                message = "背包空间不足";
            }
        }
    }

    if (result == 1 && role != NULL && src != NULL)
    {
        if (!vm_net_mock_role_consume_backpack_item(
                role, srcItemId, srcSeq, 1u, &srcRemaining))
        {
            result = 5;
            reason = "consume-failed";
            message = "扣除失败";
        }
        else
        {
            bool grantOk = true;
            for (i = 0; i < grantN; ++i)
            {
                if (grantIds[i] == 0 ||
                    !vm_net_mock_role_add_backpack_item_to_role(
                        role, grantIds[i], 1, 0, NULL, "gem-from-crystal16",
                        NULL, 0))
                {
                    grantOk = false;
                    break;
                }
                granted += 1u;
            }
            if (!grantOk)
            {
                result = 6;
                reason = "grant-failed";
                message = "发放宝石失败";
            }
            else
            {
                if (!vm_net_mock_role_db_save("gem-from-crystal16"))
                    vm_net_mock_role_mark_inventory_dirty("gem-from-crystal16");
                reason = "crystal16-ok";
                message = "兑换成功";
            }
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 21, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", srcSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", srcItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining", srcRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "granted", granted) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message", message))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_gem_from_crystal16 seq=%u granted=%u result=%u "
           "reason=%s resp=29/21\n",
           srcSeq, granted, result, reason);
    vm_autotest_note(
        "mock_gem_from_crystal16 seq=%u result=%u reason=%s evidence=pixijs-only-29/21\n",
        srcSeq, result, reason);
    return pos;
}

/*
 * 29/22 碎片合成: { seq }  3×978 → 1×随机 Lv1
 * result: 1=ok 2=bad 3=bag 4=count 5=trade
 */
static u32 vm_net_mock_build_gem_fragment_combine_response(const u8 *request,
                                                           u32 requestLen, u8 *out,
                                                           u32 outCap)
{
    u32 offset = 4;
    u32 seqValue = 0;
    u32 objectStart = 0;
    u32 pos = 5;
    u8 objectCount = 1;
    u8 result = 1;
    u16 srcSeq = 0;
    u16 destSeq = 0;
    u32 srcItemId = 0;
    u32 destItemId = 0;
    u32 srcRemaining = 0;
    u32 destCount = 0;
    const u32 cost = (u32)VM_NET_MOCK_GEM_FRAGMENT_COMBINE_COST;
    const char *reason = "ok";
    const char *message = "";
    vm_net_mock_request_object object;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *src = NULL;
    vm_net_mock_backpack_item_state *dest = NULL;

    if (out == NULL || outCap < pos || request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype != 22 || object.payloadLen == 0)
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
        message = "角色无效";
    }
    else if (vm_net_mock_gem_ops_trade_busy())
    {
        result = 5;
        reason = "trade-busy";
        message = "交易中无法合成";
    }
    else
    {
        vm_net_mock_role_normalize_backpack(role);
        src = vm_net_mock_role_find_backpack_item(role, 0, srcSeq);
        if (src == NULL || src->count == 0)
        {
            result = 2;
            reason = "fragment-not-found";
            message = "碎片不存在";
        }
        else if (src->itemId != (u32)VM_NET_MOCK_GEM_FRAGMENT_ITEM_ID)
        {
            result = 2;
            reason = "not-fragment";
            message = "不是宝石碎片";
        }
        else if (src->count < cost)
        {
            result = 4;
            reason = "count-lt-3";
            message = "碎片不足3个";
        }
        else
        {
            u32 itemCount = vm_net_mock_role_backpack_count(role);
            u32 freeSlots = role->backpackCapacity > itemCount
                                ? (u32)role->backpackCapacity - itemCount
                                : 0;
            bool freesSlot = (src->count == cost);
            srcItemId = src->itemId;
            destItemId = vm_net_mock_gem_random_lv1_item_id(
                vm_net_mock_gem_ops_seed(role, srcSeq));
            dest = vm_net_mock_role_find_backpack_item(role, destItemId, 0);
            if (destItemId == 0)
            {
                result = 2;
                reason = "dest-invalid";
                message = "合成失败";
            }
            else if (dest == NULL && freeSlots == 0 && !freesSlot)
            {
                result = 3;
                reason = "bag-full";
                message = "背包空间不足";
            }
        }
    }

    if (result == 1 && role != NULL && src != NULL)
    {
        if (!vm_net_mock_role_consume_backpack_item(role, srcItemId, srcSeq, cost,
                                                     &srcRemaining))
        {
            result = 4;
            reason = "consume-failed";
            message = "扣除失败";
        }
        else if (!vm_net_mock_role_add_backpack_item_to_role(
                     role, destItemId, 1, 0, &destSeq, "gem-fragment-combine",
                     NULL, 0))
        {
            (void)vm_net_mock_role_add_backpack_item_to_role(
                role, srcItemId, cost, 0, NULL, "gem-fragment-combine-rollback",
                NULL, 0);
            result = 3;
            reason = "grant-failed";
            message = "背包空间不足";
            srcRemaining = srcRemaining + cost;
        }
        else
        {
            dest = vm_net_mock_role_find_backpack_item(role, destItemId, destSeq);
            destCount = dest ? dest->count : 1;
            if (!vm_net_mock_role_db_save("gem-fragment-combine"))
                vm_net_mock_role_mark_inventory_dirty("gem-fragment-combine");
            reason = "fragment-combine-ok";
            message = "合成成功";
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 29, 22, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "seq", srcSeq) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "itemid", srcItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destid", destItemId) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "remaining", srcRemaining) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "destcount", destCount) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "message", message))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_gem_fragment_combine seq=%u src=%u dest=%u "
           "result=%u reason=%s resp=29/22\n",
           srcSeq, srcItemId, destItemId, result, reason);
    vm_autotest_note(
        "mock_gem_fragment_combine seq=%u result=%u reason=%s evidence=pixijs-only-29/22\n",
        srcSeq, result, reason);
    return pos;
}
