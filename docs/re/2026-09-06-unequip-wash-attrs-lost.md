# 脱装后洗练属性丢失（2026-09-06）

## 症状

穿戴已洗练装备 → 卸载回背包后，里程碑行恢复为默认黄字（洗练红字丢失）。

## 根因

`docs/re/2026-08-26-enhance-wash-transfer-fix.md` 记录的服务端契约未完全落地：

1. **卸载**走 `add_backpack_item_enhanced(..., enhanceInstanceId=0)`，每次脱装铸造新
   `item_instance_id`，与 `account_role_enhance_wash_record` 脱钩；内存 wash 若短暂为空，
   `coalesce_from_record` 无法按旧实例找回。
2. **7/9 替换**不交换 `enhanceInstanceId`，穿戴槽与背包行实例串位。
3. **洗练成功**只 `mark_inventory_dirty`，不 upsert wash record，兜底表长期空。
4. **7/8 type=4** 仍只回 `seq`，无同包 7/7 wash bit7；Pixi `openBag` 合并 17/1 时若
   wire 无 washed，仅靠 `pendingBagEquipEnhance` 勉强兜底。

## 修改

- 卸载：保留 `equippedEnhanceInstanceIds[slot]` → 背包行；`coalesce_from_record`；
  relocate wash record；清空槽位实例 id。
- 7/9 替换：双向搬运 instance id + coalesce + record upsert/relocate。
- 穿戴：旧装回包 relocate；新装 upsert。
- 洗练成功：ensure instance id + `enhance_wash_record_upsert`。
- 7/8 卸载成功：同包追加 7/7 type=1（`build_item_use_iteminfo_blob` 带 live wash）。

## 验证

- [x] `make -j2`
- [ ] 背包洗练（红字）→ 穿戴 → 卸载 → 背包详情仍为洗练红字
- [ ] 替换穿戴：卸下的旧装仍带原洗练；新装洗练不串
- [ ] 重登后背包/穿戴洗练仍在
