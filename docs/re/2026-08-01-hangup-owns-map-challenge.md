# 挂机循环期间拒绝地图点怪（4/1）

日期：2026-08-01

## 问题

挂机状态在地图走动时仍会撞到场景怪，客户端发 `4/1` challenge。挑战开战路径会
`challenge_battle_auto_reset prefer=0`，与挂机 poll 开战互相踩踏。

## 策略

挂机循环拥有遇怪权期间（`HangupLoopActive` / `ScheduleAfterExit` /
`PendingArmed` / `StartPending` / `StopAfterBattle`）：

- 拒绝普通 `4/1`：`2/10 + 25/11`「挂机中」，**不清** hangup/prefer
- `forceNonSceneStart` 挑战：`response=0`
- 仍由挂机 poll / hangup start 开战

关闭挂机后地图点怪恢复正常。

## 验证

1. 开挂机连打：撞到地图怪应见 `action=reject-hangup-owns`，不应出现
   `mock_challenge_battle_auto_reset` / 普通 `mock_challenge_battle_start`。
2. 挂机循环继续：`mock_hangup_loop_poll_deliver` / `mock_hangup_battle_start`。
3. 关挂机后再点怪：正常 `mock_challenge_battle_start`。
