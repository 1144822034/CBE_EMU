# 挂机结算后空白显示

Date: 2026-07-28

Status: implemented (server)

```text
phase: hangup victory -> 4/7 settle -> delayed 4/8+4/11+4/9
       -> map idle until hangup_loop_poll_deliver
trigger: 挂机打怪结算后、回地图到下一场前，界面空白
```

## 根因摘要

1. `settlement_exit` 在 `prefer=1` 时附带 **`4/11 type=1`**。`4/8` 已拆掉
   Battle.cbm 结算壳后，type=1 仍把客户端拨进自动态，但场上已无战斗 UI，
   表现为结算后的空白壳，直到下一场挂机开战再发 type=1。
2. 旁证：`prefer-poll-rearm`（`cancel_window=0`）在 `pendingArmed` 短暂为 0
   时会把仍未到期的 `NextActNotBeforeMs` 清零，可能提前 synth、打乱
   actioninfo 播放与结算时序（与
   `2026-07-28-battle-auto-stomp-counter-playback.md` 同族）。

权威：`mmBattle` tear-down `0x7DF6`（`4/8`）；`HandleServerBattleCmd` case 11
`0x7cb7`（type 开关自动 UI）。挂机循环由服务端 `g_mockBattleAutoPrefer` +
`hangup_loop` 驱动，开战包会再次附带 `4/11 type=1`。

## 修改

1. `mock_battle_settlement_exit` 固定 **`4/11 type=0`** 做干净拆场；**不**清
   服务端 `g_mockBattleAutoPrefer` / hangup loop。日志增加 `prefer_kept`。
2. `arm_pending_ex`：只要 `now < NextActNotBeforeMs`，非 `startCancelWindow`
   的 rearm 只补 `pendingArmed=1`，禁止把 hold 抹成 `not_before_ms=0`。

## 验证

1. 挂机胜利：`settlement_exit ... auto=0 prefer_kept=1`，回地图无空白壳。
2. 约 hangup-loop 间隔后再见 `hangup_loop_poll_deliver` / `hangup_battle_start`
  （含 `auto=1`）。
3. 中途开自动后反击动作仍完整播放（无 `prefer-poll-rearm ... not_before_ms=0`
   踩掉进行中的 playback hold）。
4. `make -j2 server`。

## 相关

- `2026-07-27-hangup-settlement-stuck.md`（延迟 `4/8`、AwaitingSettlement gate）
- `2026-07-28-multi-monster-empty-settlement.md`（play_ms+panel_ms）
- `2026-07-25-battle-auto-flag-stall.md`（客户端请求 type=0 清 prefer；与本处
  **推送** exit type=0 且服务端 keep prefer 不同路径）
