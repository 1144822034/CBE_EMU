# 战斗确认使用复活石无反应、再遇怪无提示

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 死亡确认用复活石无任何反馈；再遇怪无提示（多为 `reject-dead`） | 上一版在服务端无权威 `801` 时把 `result=1` 回落成 `20/1+30/1`。Battle.cbm 仍占用死亡 UI，主场景包无法结算离场 → 确认“没反应”；durable HP 常仍为 0 → 再遇怪被拒且看似无提示 | `result=1` **始终**走 `4/7+4/8+4/11+4/9`；无石时按本地/背包不同步做满血恢复并退出战斗；复活后恢复 battle globals / onlineHp；`awaits_battle_revival_confirm` 仅针对死亡座位 |

## 契约

- `1/7/14(result=1)`：Battle 结算终结链，不得单独回 `20/1+30/1`。
- `1/7/14(result=2)`：普通复活才是 `20/1+30/1`。
- 客户端有幽灵石、服务端无行时，仍须用结算链退出，不能卡死确认框。

## 验证

重启 `bin/jh-online-server.exe`。看日志：

1. `action=revival-stone` 或 `action=revival-desync-no-stone`，且 `response=4/7+4/8…`
2. 不应再出现 `missing-stone-fallback-ordinary-respawn`
3. 出战后遇怪不应再 `reject-dead rolehp=0`
