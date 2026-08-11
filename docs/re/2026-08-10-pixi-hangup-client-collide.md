# Pixi 挂机 = 客户端模拟碰怪（2026-08-10）

## 契约

| 层 | 行为 |
|----|------|
| Pixi 挂机 | 客户端定时 `4/1`（`startBattleNearPlayer`），与碰怪同路径 |
| Mock 挂机 | **保留** CBE `2/10` Type=2 + hangup loop（原契约不动） |
| Gateway 保活 | **保留** idle scene-poll 代发（默认 10000ms） |

挂机与手动碰撞共用同一开战路径（Pixi 侧）：

| 环节 | 行为 |
|------|------|
| 选怪 | 当前场景 `sceneMonsters` / catalog |
| 开战 | `1/4/1` → 正常 subtype-5 |
| 战内出手 | 客户端 `4/2`（`hangupSkillOperate`） |
| 连场 | 回地图后客户端定时再调 |
| 停挂 | HUD / 战内「关闭」→ `stopHangup()` 本地清 `hangupActive`+`hangupDesired`+遇怪定时器；**不发** `2/10`；tip/聊天「已停止挂机」 |

## UI

- 战内条关闭：`BattleScreen.closeHangup` 立即 `setHangupActive(false)`（不再等服端 chat）。
- 不再展示「请求停止挂机…」（旧 2/10 停环文案）。

## 验证

1. 户外 Pixi 挂机：聊天「已开始挂机」，进战 subtype-5。
2. 挂机中点 HUD / 战内关闭：立刻「已停止挂机」，本场后不再自动遇怪；网络无 `1/2/10`。
3. CBE/模拟器点挂机：仍走服端 `2/10` hangup start（未拆）。
4. Gateway 日志：`idle scene-poll keepalive 10000ms`。
5. `npx tsc --noEmit`
