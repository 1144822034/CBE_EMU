# 2026-08-05：组队挂机再开战早于队友结算拆场

## 症状

三人组队（相互队长挂机 / 阿狸 / 蔡文姬）自动连打：

- 第一、二场尚可打完
- 胜后挂机再入第三场时，队长与蔡文姬已进战，阿狸仍停在上一场结算/地图侧
- 回合日志长期 `acted=05 alive=07`（缺阿狸座），表现「自动卡住」

## 触发与日志锚点

`team_battle_clear serial=2` 后：

1. 阿狸 `team_battle_action_deliver ... terminal=1` + `settlement_exit_arm ... delay_ms=5300`
2. 队长 `settlement_exit ... response=empty evidence=skip-type0-menu-on-settle-hangup-reenter`
3. 约 2s 后 `mock_hangup_loop_poll_deliver` → `team_battle_queue serial=3 ... queued=2`
4. 蔡文姬有 `team-battle-deliver`；**阿狸无** `team-battle-deliver` / `round_prepare`
5. `team_battle_round_deadline_arm battle=3 ... acted=05 alive=07`

## 根因（第一次偏离）

1. **挂机再入间隔（默认 2s）短于队友结算拆场窗（playback + panel ≈ 5.3s）**  
   队长 skip-4/8 后即可 `team_begin_battle`，仍把未拆场的队友写入 `pendingTeamBattleSerial`。

2. **scene poll 顺序把 pending team start 放在 settlement_exit 之前**  
   （为修终局 4/6 饿死而把 start 与 action 一并抬到 4/8 前）。  
   当旧场 `battleEvents` 已清空、新场 start 已排队时，队友一旦 poll 会先拿 `1/4/5` 而跳过旧场 `4/8`，Battle.cbm 生命周期错乱；若队友仍在播旧场 actioninfo 而暂不 poll，则整场缺席。

3. 回合屏障按 `alive_mask` 等所有存活座出手，缺席座拖到 `CBE_TEAM_BATTLE_ROUND_TIMEOUT_MS`，体感卡死。

契约：`4/8` 拆上一场 Battle.cbm 必须先于下一场共享 `1/4/5`；组队挂机不得在队友仍欠 `settlement_exit` 时武装下一场。

## 修改

1. `mock_server_social.c` poll：`team action` → `settlement_exit` / `post_exit_settle` → `team start`。  
   action 仍优先于 4/8（保留终局 4/6 投递契约）。
2. `session_team_battle_not_ready_reason`：同场景存活座若 account 上
   `SettlementExitPending` / `PostExitSettlePending` → `settle-pending`  
   （不认单独的 `AwaitingSettlement`：队长 hangup skip-4/8 会留下该位）。
3. 挂机 start-delay / poll：`settle-pending` 时 `hangup_loop_schedule_next` 重试，**不** `hangup_loop_clear`。

## 复测

1. 三人组队，队长挂机连打 ≥3 场。
2. 日志：胜后若队友仍 `settle-pending`，应出现 `mock_hangup_loop_poll_hold ... settle-pending`，
   待队友 `settlement_exit` 后再 `team_battle_queue`；三人皆有 `team-battle-deliver`。
3. 邻接：单人挂机、商城 `reject-party-not-ready`、终局共享 4/6 先于 4/8 仍正常。

## 仍未知 / 风险

- MySQL `socket send failed` / `role_deferred_flush_failed` 与本卡住正交。
- 若客户端在收到终局 4/6 后长时间不 poll，挂机仍会等到其 4/8 或超时；属客户端节奏，服务端只保证不抢先开战。
- 后续复现「队友卡掉线」见 `docs/re/2026-08-05-team-auto-poll-terminal-duplicate.md`
  （auto-poll 终局包重复投递；已与本 settle 门闩配合修复）。
