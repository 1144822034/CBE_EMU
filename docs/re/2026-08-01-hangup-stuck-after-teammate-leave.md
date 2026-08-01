# 队友离队后队长挂机进战斗无行动

日期：2026-08-01

## 代码现状（2026-08-01 再合入）

曾在细锁回滚中丢失；现已按本文再次合入：

- `active_session_in_team_battle`：`battleActive && !battleFinished`
- `team_clear_battle_state` + `team_remove_member` 离队 `leftMask`、
  `memberCount<2` 时 `roster-below-two` 清战斗快照

队友下线走 `mark_offline` → `team_remove_member`，与主动离队同一路径。

## 症状

组队打过至少一场后，队友脱离队伍（或下线），队长开挂机/再进战斗：有进战 UI，
但没有任何行动（挂机无 4/6，手动操作也被 finished-fight 分支吃掉），一直卡住。

## 根因（运行时 + 代码交叉验证）

1. 组队战胜利只置 `team->battleFinished=1`，**从不**清 `battleActive`
   （全仓库仅 `team_begin_battle` 写 `battleActive=true`，离队 `memset` 除外）。
2. `vm_net_mock_active_session_in_team_battle()` 只看 `battleActive`，不看
   `battleFinished`。
3. 挂机 prefer 的 poll synth（`build_pending_solo_auto_operate`）因此走
   `team_seat_can_act`，而该函数在 `battleFinished` 时恒为 false →
   `pendingArmed` 永不 rearm → 无 4/6。

日志证据（修复前 `debug-6d7cbd.log`）：组队 H4 `battle=1/2` 之后，lxh001
长期 `prefer=1 hangup=1 pendingArmed=0`。

队友离队未清战斗快照，放大了「一人挂机仍被当成未结束组队战」的窗口。

## 修改

1. `active_session_in_team_battle`：要求 `battleActive && !battleFinished`。
2. `team_remove_member`：离队席位置 `battleMemberLeftMask`；`memberCount<2`
   时 `team_clear_battle_state`。
3. 取证：`auto_seat_blocked` / `team_battle_clear` → `debug-6d7cbd.log` (T1)。

## 验证（2026-08-01 post-fix `debug-6d7cbd.log`）

1. 离队：`team_battle_clear serial=3 reason=roster-below-two roster=1
   battleMembers=2 finished=1`（T1）。
2. 清战后队长挂机：`prefer=1 hangup=1 pendingArmed=1` 多次出现，并有
   `nextActMs` 推进（playback hold）——不再是清战前那种长期 `pendingArmed=0`。
3. 仍见少量 `auto_seat_blocked`，但均为 `inTeam=0 armed=0 teamActive=0`：
   地图间隔等待下一场挂机开打，**不是**陈旧组队战屏障。
