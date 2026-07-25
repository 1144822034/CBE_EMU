# 队长战斗死亡后购买复活石闪退

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 队长（战斗死亡进商城）购买复活石后闪退 | 商城返回 WT6/1 把仍属 Battle.cbm 死亡确认链的角色当成地图死亡：立刻消费 `801`，并往已近 10 对象上限的 shop-return 包塞入未验证的 `1/1/1 actorinfo`，打断后续 `1/7/14` | 战斗确认未完成时跳过地图复活/惩罚；去掉 shop-return 中的 actorinfo；仅地图死亡（无战斗确认）才消费 801 |

## 契约

- 战斗死亡：`14/3` 只入库；确认仍是 `1/7/14(result=1)` → `4/7+4/8+4/11+4/9`。
- 地图死亡（无 Battle.cbm / operate 未武装）：商城返回可消费 `801` 并恢复 durable/online HP，响应仍是既有 `resources+30/2`。
- shop-return 包不得额外塞登录态 `actorinfo`。

## 验证

重启服务。

1. 战斗死亡 → 进商城买 801 → 退出 → 确认使用：不闪退，走 `4/7+4/8…`；日志 `mock_shop_return_skip_map_revival`。
2. 纯地图 HP=0 + 有石：进出商城仍可 `mock_shop_return_map_revival`。
