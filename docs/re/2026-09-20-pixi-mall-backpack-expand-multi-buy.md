# 商城背包扩容一次可买多份

## 触发与现象

商城「背包扩容」(806) 详情 ←→ 可调数量，但一次买多份会失败（或服务端拒 `count>1`）；只能反复买 1 份。壶类已支持多份。

## 业务链路

1. Pixi `1/14/3 { type=2, id=806, num=N }`。
2. 服务端 `directExpand`：旧逻辑 **仅 `count<=1`** 调 `expand_backpack_capacity(role,1)`，否则 `result=3`。
3. `vm_net_mock_role_expand_backpack_capacity` / `usable_count` 本身已支持多份（每份 +4，顶 256）。

## 根因

商城直扩路径人为禁止 `num>1`；与背包使用多张 806、以及壶类多份购买不一致。

## 修改

| 层 | 改动 |
|----|------|
| `mock_server_interaction_login.c` | 按 `usable=min(num,剩余份数)` 扩容，按 `单价×usable` 扣酷宝 |
| `itemCatalog.ts` | `BACKPACK_EXPAND_STEP` + `maxBackpackExpandBuyUnits` |
| `SystemPanel.maxBuyQty` | 806 按剩余格数/余额夹上限；满则提示不可再扩 |
| `GameSession.buyMallItem` | 成功后 `openCharacterStats` 刷新容量 |

## 验证

- [ ] 容量 20、酷宝充足：可一次买多份（如 10），容量 +40，扣费 = 单价×份数。
- [ ] 选超过升到 256 所需份数：实际只扩到 256，只扣实际份数。
- [ ] 已满 256：详情「不能再扩容」，购买拒 `result=3`。
- [ ] 单次买 1 份行为与改前一致。
- [ ] `make -j2`（服务端）通过。
