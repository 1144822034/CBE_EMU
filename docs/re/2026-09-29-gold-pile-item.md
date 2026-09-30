# 金币堆（道具 856）

日期：2026-09-29

## 概述

自定义消耗品「金币堆」：1 堆 = **1 万金**（`10000 * COPPER_PER_GOLD` 铜）。玩家可在仓库界面用随身金兑换堆，或使用背包堆兑回随身金。

## 契约

| 路径 | 行为 |
|------|------|
| 使用 `7/1` | 消耗 1 堆 → `role.money += 1万金`（溢出钳到 `0xffffffff`）→ `7/1+7/7+7/11+10/26` |
| 仓库根菜单 | 新增「兑换金币堆」→ opcode `0xee070000` |
| 兑换确认 | `0xee080000 \| piles`（`0xffff`=全部可兑）扣随身金、背包 +N 堆 |

## 关键文件

- `src/server/mock_server_core.c` — ID / opcode
- `src/server/mock_server_catalog.c` — catalog + 使用分支
- `src/server/mock_server_role.c` — 堆叠发放 + `warehouse_gold_pile_exchange`
- `src/server/mock_server_scene_sync.c` — 仓库菜单
- `PixiJS_web/src/resources/itemCatalog.ts` / `protocolExtras.ts`

## 商城目录持久化

- 856 不在 `item.dsh`：启动时 `shop_admin_db_load` 先于 `ensure_gold_pile`，旧逻辑直接 `add` 后 return，MySQL 行被 skip → 每次重启价格变 1、上架状态丢失。
- 2026-09-30 修复：`ensure` 注入后 `SELECT server_shop_items` 回绑；无行则 seed `enabled=0`（不默认上架）。启动日志应见 `mock_gold_pile_catalog_bound`。

## 复测

1. 仓库兑 1 堆：随身金 -1万、背包 +1
2. 使用堆：金 +1万、堆 -1
3. 金不足 / 背包满：失败 tip，不扣错账
4. 后台改 856 价格/上下架 → 重启服务端 → 管理端与商城仍保持该值
