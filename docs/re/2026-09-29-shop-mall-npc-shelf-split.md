# 2026-09-29 商品管理：商城 / NPC 独立上架

## 需求

后台「商品管理」可分别配置商品上架到**商城**还是**NPC（药商）**，二者不再互斥（旧逻辑用 `enabled` 反义驱动药商）。

## 根因 / 旧契约

| 通道 | 旧闸门 | 问题 |
| --- | --- | --- |
| 商城 14/5 秘宝 | `enabled=1` | 与药商互斥 |
| NPC 药商 | `enabled=0` 且 HP/MP 药品 | 无法「只上架商城」或「双通道」 |
| 武器/防具 NPC | 忽略 `enabled` | 保持不变 |

## 修改

- 表 `server_shop_items` 增加 `npc_enabled`（默认 0）。
- 一次性迁移 `shop-npc-enabled-v1`：原 `enabled=0` 行 → `npc_enabled=1`（保留药商货架）。
- 药商 selector：改为 `npc_enabled && !isEquip`（不再限类别 10 / HP·MP）。
- 后台每行两个下拉：上架商城 / 上架 NPC。

## 后续

价格拆分见 `2026-09-30-shop-mall-npc-price-split.md`。

## 验证

1. `make -j2` 通过；重启 server。
2. 某 HP/MP 药：仅上架商城 → 秘宝可见、药商不可见。
3. 仅上架 NPC → 药商可见、秘宝不可见。
4. 双开 → 两处可见；双关 → 均不可见。
5. 白装武器/防具 NPC 仍有货；商城下架白装不影响 NPC。
