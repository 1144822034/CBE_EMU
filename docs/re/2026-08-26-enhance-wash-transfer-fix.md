# 洗练属性转移丢失修复（2026-08-26）

## 症状

1. 背包洗练后上架拍卖行，列表/撤拍/领取后洗练红字消失。
2. 穿戴洗练装备后卸载到背包，里程碑洗练行恢复为默认黄字。

## 根因

- **卸载**：7/8 仅返回 `seq`，无同包 7/7 wash bit7；Pixi 随后 `openBag` 合并 17/1+30/21 时，若 wire 与本地 worn 快照短暂不一致，`mergeBagItemUpdateDeltas` 会用无 washed 行覆盖旧 `enhanceAttrs`。
- **拍卖**：上架前若实例 `enhance_wash` 已空（历史/竞态），listing 写入默认 wash；撤拍/领取虽从 DB 读 wash，但无兜底来源。
- **客户端**：仅有 `pendingBagEquipDurability`，未保留 worn 的 `enhanceAttrs`；7/7 增量合并不 `preferEnhanceAttrs`。

## 修改

### 服务端

- 新表 `account_role_enhance_wash_record`（迁移 `migrate_add_enhance_wash_record.sql`）：按 `(role_id, item_instance_id)` 存最后一次洗练快照；`bag_seq` 仅作 legacy 回退。
- 每件装备实例分配唯一 `item_instance_id`（`enhanceInstanceId`），持久化于背包/装备栏/拍卖 listing·claim；转移时随实例拷贝，避免同 `item_id+level` 多件装备洗练串数据。
- 迁移 `migrate_add_enhance_instance_id.sql` 为相关表增加 `item_instance_id` 列。
- 洗练成功 / 穿脱 / 互换 / 拍卖上架·撤拍·领取：upsert 或 relocate 记录；转移前 `enhance_wash_coalesce_from_record(roleId, instanceId, …)` 补空 wash。
- 7/8 成功（穿/卸）同包追加 7/7 type=1（`build_item_use_iteminfo_blob` 带 live wash）。
- 拍卖上架/撤拍成功响应内联 17/1+30/21（与 claim 一致）。

### 客户端（Pixi）

- `pendingBagEquipEnhance`：卸载/互换时从 worn 暂存洗练行到新 bag seq。
- `applyBagEquipDurability` 合并 washed attrs；`mergeBagItemUpdateDeltas` 对装备行 `preferEnhanceAttrs`。

## 验证

- [x] `make -j2` 服务端通过
- [ ] 背包洗练 → 上架 → 列表详情见红字 → 撤拍回包仍带 wash
- [ ] 背包洗练 → 穿戴 → 卸载 → 背包详情仍为洗练红字
- [ ] 两把同 item_id 装备分别洗练 → 穿脱/拍卖不串属性

## 后续（2026-09-06）

脱装路径仍丢洗练：见 `docs/re/2026-09-06-unequip-wash-attrs-lost.md`
（instance id 未随脱装保留、洗练成功未 upsert record、7/8 未带 7/7）。
