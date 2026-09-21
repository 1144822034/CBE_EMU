# 拍卖行按类别拦截开关

## 触发

拍卖行已开启时上架玄晶，返回 `not-tradeable`。根因是上架复用了交易校验：物品 `901–916` 与 `item.dsh` 类别 23（玄晶）、类别 11（任务物品）一律拒绝。

## 配置

后台「拍卖行」基础配置增加「按类别拦截」（`server_auction_config.category_gate`，默认 0）。

| 开关 | 上架 |
|------|------|
| 关 | 不按类别拒绝。玄晶可上架 |
| 开 | 与交易相同：玄晶、任务物品类别拒绝，`message=not-tradeable` |

无论开关：绑定（`bindAttr`）、关联任务（`questLink`）、已强化、梦境徽章、宝石/碎片、已镶嵌装备仍拒绝。玩家交易与帮派仍用 `vm_net_mock_item_is_tradeable`。

已有库由服务启动时 `ALTER TABLE ... ADD COLUMN category_gate` 补列，默认关。

## 验证

1. 拦截关 → 上架未绑定玄晶成功
2. 拦截开 → 玄晶上架 `not-tradeable`；改回关后可上架
3. 宝石、已强化、已镶嵌装备两种开关下都不可上架
4. 玩家面对面交易仍不能放入玄晶
