# 2026-09-30 商品管理：商城价 / NPC 价独立

## 需求

后台「商品管理」中，商城上架价（酷宝）与 NPC 上架价（铜币）可分别配置，不再共用同一 `price`。

## 旧契约

| 通道 | 上架开关 | 价格字段 |
| --- | --- | --- |
| 商城 14/5 秘宝 / coolmall | `enabled` | `price` |
| NPC 药商 / 武器防具 | `npc_enabled`（药）或白装目录 | **同** `price` |

上架已于 `2026-09-29-shop-mall-npc-shelf-split.md` 拆开；价格仍共用导致双通道同价。

## 修改

- 表 `server_shop_items` 增加 `npc_price`（默认 0）。
- 一次性迁移 `shop-npc-price-v1`：`npc_price = price`（保留现网两边同价）。
- 内存目录增加 `npcPrice`；DSH 默认两通道同源。
- 后台每行两个输入：商城价（`price`）/ NPC 价（`npc_price`）。
- 商城列表与 14/3 购买继续用 `price`；NPC 列表/详情/铜币购买用 `npc_price`（0 时回落 `price`）。
- coolmall（酷宝会话）仍走商城价。
- 自定义目录：酷宝价 → 商城 `price`，铜价格 → `npc_price`。

## 验证

1. `make -j2` 通过；重启 server（会自动 ALTER + backfill）。
2. 同一药品：商城价=50、NPC 价=100 → 秘宝显示 50 酷宝、药商显示 100 铜币，购买分别扣对应货币。
3. 仅改其一价格不影响另一通道。
4. 武器/防具 NPC 显示/扣费用 `npc_price`。
5. 未改过的旧行迁移后两边仍同价。
