# 手动遇怪 vs 挂机遇怪（2026-08-10 修订）

## 对照（Pixi）

| 环节 | 手动（碰撞 / F / 点怪） | 挂机 |
|------|-------------------------|------|
| 选怪 | 碰撞到的 SCE / 点选 id | 当前场景最近怪（同 `startBattleNearPlayer` 池） |
| 开战 | `1/4/1` | **同左**（客户端定时再调，服务端不下发） |
| 开战包 | subtype 5 | subtype 5 |
| 战内出手 | 玩家或 `[自动]` 发 `4/2` | 挂机技能 `4/2`（`shouldInBattleAuto`） |

已废弃：挂机专用 `2/10` 开战、subtype-10、服务端 hangup poll 下发、播报 wire remap。

详见 `2026-08-10-pixi-hangup-client-collide.md`。
