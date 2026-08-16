# Pixi 十连开箱改服务端（1/7/24）

日期：2026-08-13

## 触发与现象

客户端循环 10 次 `1/7/15` 时，稀有世界祝贺按次刷屏，且 tip/背包数量易与 `[世]` 不一致（见 `2026-08-13-pixi-chest-batch-crystal-stack.md`）。

## 业务链路（改后）

1. Pixi「批量开启」发 **一次** `1/7/24 { box, key, times }`（`times` 1..10）。
2. 服务端在同一次请求内：校验箱/钥数量与空位 → 循环 roll/扣箱钥/入包 → `iteminfo` 按 `itemId` 汇总（装备各一行）→ 回 `7/24 { result, opened, total, iteminfo }`。
3. 稀有祝贺：对每个稀有 `itemId` **立刻**播一条「获得N个…」（`announce_rare_reward_count`），不再按次 debounce。
4. Pixi 成功后 `openBag()`，tip 按背包增量「获得N个」。

单次开箱仍走 CBE/兼容路径 `1/7/15`。

## 根因（相对旧十连）

十连是客户端协议放大，不是服务端原子操作；世界祝贺绑定每次 `7/15` 成功事件，与最终堆叠增量分裂。

## 修改

| 位置 | 内容 |
| --- | --- |
| `mock_server_catalog.c` | `vm_net_mock_build_chest_open_batch_response` |
| `mock_server_equipment_npc.c` | `vm_net_mock_gold_chest_announce_rare_reward_count` |
| `mock_server_dispatch.c` | 挂钩 `builtin-chest-open-batch` |
| `protocolExtras.ts` / `GameSession.ts` | 请求/解析/`openChestBatch` 改为单次 7/24 |

## 验证

1. 批量开启 10：一条连开 tip；背包增量与 tip 一致；稀有十二级×N → 世界 **一条**「获得N个十二级玄晶」。
2. 箱/钥/空位不足：对应 result 2/3/4，不扣或不开。
3. 单次开启 `7/15` 仍可用；CBE 不受 7/24 影响。
