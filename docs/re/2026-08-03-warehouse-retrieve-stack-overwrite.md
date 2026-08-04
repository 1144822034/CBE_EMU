# 仓库取回可堆叠物覆盖背包原数量

日期：2026-08-03

## 触发与现象

背包已有玄晶 1 个，仓库同级玄晶 2 个；取回后背包只剩 2 个（原 1 被顶掉），
正常应为 3。

## 是否收壶影响

否。收壶（802/803）走 `warehouse-retrieve-flask` / `grant_flask_reservoir_points`；
玄晶（901–916）走普通 `warehouse-retrieve-bag` + `add_backpack` 按 `itemId` 堆叠。
`backpack_item_id_uses_reservoir_count` 仅对 802/803 为真。

## 业务链路

1. `26/1 action=warehouse-retrieve` → `vm_net_mock_warehouse_retrieve_slot`
2. 非壶：`add_backpack(..., moved.count)` 合并到已有行（1+2→3）
3. 旧逻辑：对非装备行执行 `added->count = moved.count`（写成仓库量 2）
4. `arm_npc_purchase_backpack_pending` 旧逻辑 peel `added->count`（绝对量）
5. poll 投递 `7/7 type=1` → `TimerControl_ProcessItem`（增量合并）

## 首次偏离

`retrieve_slot` 在堆叠合并成功后，用仓库数量覆盖合并结果。

次要偏离：取回 peel 使用合并后绝对数量，而 shop-buy / ProcessItem 契约是
**本次增量**（见 `2026-07-22-npc-service-purchase-backpack-update.md`）。

## 根因

1. 「restore moved values」误把可堆叠 `count` 写成仓库侧绝对值，毁掉 bag 侧原堆叠。
2. 取回 `7/7` peel 误用绝对剩余，与增量契约不一致（在覆盖 bug 下 peel=2；
   若只修内存不修 peel，客户端会按绝对量再叠一次）。

## 修改

1. `mock_server_role.c`：取回后只恢复 `enhanceLevel`，不再写 `added->count`。
2. `mock_server_scene_sync.c`：取回前记下仓库 `count` 作为 peel delta；壶仍 peel
   合并后储量绝对值。

## 验证

1. `make -j2`
2. 背包玄晶 1、仓库同级 2 → 取回 → 背包 3；日志
   `mock_backpack_add ... delta=2`，重登仍为 3。
3. 背包无该玄晶、仓库 2 → 取回 → 背包 2。
4. 取回装备 / 收壶：行为与取回前一致（装备 enhance 不变；壶走储量合并）。
