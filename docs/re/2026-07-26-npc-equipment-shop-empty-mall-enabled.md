# NPC 装备商人「暂无可购买的商品」

日期：2026-07-26

## 症状

武器 / 防具 NPC：选子类 → 等级段后提示「暂无可购买的商品」。

## 证据

`server_shop_items`（`192.1.1.3` / `jh_online`）：

| 集合 | enabled=1 | enabled=0 |
| --- | ---: | ---: |
| 白装（equip.dsh 品质=0，432） | 0 | 432 |
| 彩装（品质≠0，1053） | 1053 | 0 |

NPC 商店只列出白装（`2026-07-25-npc-equipment-white-level-bands.md`），匹配函数又要求 `item->enabled`，因此等级段菜单与商品页全部为空。

## 根因

`server_shop_items.enabled` 是商城上下架开关；后台把白装从「神兵利器」下架后，同一标志误伤了 NPC 装备目录。首次偏离在 `vm_net_mock_npc_shop_item_matches_selector()` 对装备行也检查 `enabled`。

## 修复

装备类 selector（1..10）不再读取 `enabled`；药品商人仍尊重下架。价格覆盖继续来自 `server_shop_items.price`。

## 验证

- 复测武器 / 防具 NPC：子类 → 等级段 → 出现白装条目并可购买。
- 商城神兵利器仍可不展示已下架白装。
