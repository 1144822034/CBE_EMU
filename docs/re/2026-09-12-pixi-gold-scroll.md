# 打金心得（2026-09-12）

## 范围

PixiJS 自定义商城道具 **853 打金心得**：类似战斗心得（828）的 1 小时时效增益。

| 字段 | 值 |
| --- | --- |
| itemId | 853 |
| 名称 | 打金心得 |
| 说明 | 使用后获得1小时打金心得状态，获得钱币增加20%，获得经验降为0（战斗只获得钱币）。耐久1。 |
| 类别 | 10 |
| 酷宝价 | 50（可后台改价/下架） |
| 耐久 | 1；背包 `count` 为剩余次数（可叠加） |
| 时长 | 60 分钟；可续时叠加；离线暂停计时（同经验卡/战斗心得） |

## 效果契约

生效期间（怪物战胜利结算）：

1. **钱币 +20%**：相对本场基础战斗钱币（`gold_for_enemy × enemyCount`）加算 20%，再与姻缘世界倍数并存。
2. **经验 = 0**：在经验卡 / 战斗心得 / 姻缘加成之后强制清零；战斗结果只发钱币（及掉落），不发经验。

不改变战斗心得的自动修装、固定 3 怪、满包出售等副作用。可与心得同时生效：心得其它效果仍在，经验仍被本道具清零。

## 协议

| WT | 作用 |
| --- | --- |
| `1/7/1` | Pixi 背包使用 → 扣 1、写入 `effect_kind=4` 时效，回 `7/1+7/7+7/11+7/32+7/31`（+可选 `16/2` tip） |
| `1/7/31` | 「修」状态列表含「打金心得生效中(剩余…)」 |

禁止当普通药品消耗；`item_requires_special_use_protocol(853)` 门闩后走专用 7/1 builder。

## 持久化

`account_role_item_effects`：`effect_kind=4`（`GOLD_SCROLL`），`multiplier=20`（钱币加成百分比），`item_id=853`。

## 代码落点

- `mock_server_core.c`：`GOLD_SCROLL_*`、`ITEM_EFFECT_GOLD_SCROLL`
- `mock_server_role.c`：校验 / 暂停恢复 / 查询 / 背包耐久1 叠加
- `mock_server_catalog.c`：商城+时效目录注入、7/1 使用、buff tip
- `mock_server_equipment_npc.c`：经验清零、钱币 +20%、logout pause
- `mock_server_interaction_login.c`：login resume
- `PixiJS_web`：`itemCatalog` / `protocolExtras` / `GameSession.useItem` tip

## 验证

- [ ] 商城可见「打金心得」，购买后背包数量 +1
- [ ] 使用 → tip「钱币增加20%，经验为0」，「修」可见生效中
- [ ] 打怪：钱币约为原 1.2 倍，经验增量 0
- [ ] 再使用一张可续时；下线再上线剩余时间不流逝
- [ ] `make -j2` 通过
