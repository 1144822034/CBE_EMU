# 确认使用复活石后不回血、不扣石

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 战死买石后提示「是否使用复活石」，选是：血蓝不回、石头不扣 | 1) 战死标记只靠 session，且 arm 依赖 process-global HP，买石仍可能走地图满血不入库；2) `result=1` 在 durable HP≠0 时 `apply_revival_stone` 直接拒绝，desync 分支又只在 HP==0 时回血；3) 成功扣石后未下发 `7/11 remaining=0`，客户端背包行不消失 | 账户级 `g_mockBattleAwaitsRevivalConfirm` + 按 durable HP arm；买石/确认用更宽的 awaiting 判定；有权威 801 时强制对齐死亡再消费；无石也强制满血并 `4/7+4/8` 离场；扣石时附带 `7/11` |

## 验证

重启服务。战死 → 商城买 801 → 确认使用：

1. 日志 `battle_revival_confirm_armed`，买石 `map_revived=0`。
2. `action=revival-stone`，`stone_seq!=0`，响应含 `4/7+7/11+4/8…`。
3. 客户端血恢复、背包 801 消失；无「当前无需复活」。
