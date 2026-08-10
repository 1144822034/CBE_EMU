# 2026-08-05：组队自动终局包重复投递导致队友卡掉线

## 症状

三人组队（阿狸队长挂机 / 蔡文姬 / 相互）自动连打：

- 胜后队长正确 `hangup_loop_poll_hold blocker=相互 reason=settle-pending`
- 随后 `team_remove ... reason=heartbeat-timeout` / `session_offline ... 相互 ... reason=heartbeat-timeout`
- 体感「队友卡住后掉线」，挂机再入缩成 2 人队

## 触发与日志锚点

`team_battle` serial=2 终局：

1. 相互 auto-poll 收刀：`mock_battle_auto_synth via=auto-poll ... resp=639`
2. `team_battle_synth_terminal_settle_defer` + `action_queue ... terminal=1 delivered=00`
3. 同包再投：`team_battle_action_deliver ... observer=1709c770 ... terminal=1 resp=613`
4. 相互此后无 scene_sync_poll，直到 heartbeat-timeout
5. 队长反复 `party-settle-pending blocker=1709c770`

## 根因（第一次偏离）

`queue_action` / 终局 settle 把 **所有** `AutoSynthInProgress` 当成「本地缓冲已丢弃」：

| 路径 | 响应去向 | 旧行为 |
|------|----------|--------|
| timeout / terminal-wipe autofill | 本地 `synthResp` 丢弃 | 不标 delivered、不 arm `settlement_exit`（正确） |
| auto-poll synth | 写入 poll/`out`，**已上线** | 同样不标 delivered、defer settle（错误） |

相互已在 `resp=639` 消费完整终局 `4/6+4/7`，却仍欠 `battleEvents` 投递；下一 poll 再拿一遍终局包并二次 arm `settlement_exit`。客户端卡在重复结算路径后停 poll → 队长被 `settle-pending` 挡住 → 心跳踢人。

契约：线上已交付的合成席必须标 delivered，并按真实收刀一样 arm `settlement_exit`；仅丢弃缓冲的 timeout 合成才 defer。

次要：`team_member_same_scene_presence` 在场景仍可见时不要求近期心跳，停 poll 的席仍会挡住挂机直到 timeout。

## 修改

1. `g_mockBattleAutoSynthWireDeliver`：auto-poll synth=1；timeout autofill 显式=0。
2. `queue_action`：仅 `synth && !wire` 时不预标合成席；wire synth 与真实 4/2 一样标 delivered。
3. 终局 release：仅 `synth && !wire` 时 `synth_terminal_settle_defer`；wire synth arm `settlement_exit`（日志 `team_battle_synth_terminal_wire_settle`）。
4. `session_team_battle_not_ready_reason`：`settle-pending` 要求 `presence_is_recent`，避免已停 poll 的席永久挡挂机。

## 验证

- 组队三人挂机连打 ≥3 场：终局收刀席日志为 `wire_settle` + `delivered` 含合成席 bit，且**不应**再对该席 `action_deliver ... terminal=1`。
- 队友持续 poll 拿到单次 `4/8`；队长 `settle-pending` 仅持续 playback+panel 窗，不应拖到 heartbeat-timeout。
- `make -j2` 通过。
