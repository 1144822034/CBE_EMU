# 组队仅队长可挂机

日期：2026-08-01

## 契约

组队（`memberCount>=2`）时：

- **队长**可以挂机连战：开战走与 `4/1` 相同的 `team_begin_battle`，循环跨场保留。
- **队员**不可以挂机：Type=2 / poll / start-delay 跳过；提示「只有队长可以挂机。」。
- **注意**：挂机**软键**在组队时被客户端硬拦（「你在队伍中，不能使用挂机战斗」），见 `2026-08-01-party-hangup-button-client-block.md`。队长入口改为战内自动 / prefer 武装 `HangupLoop*`。

## 根因（此前队长挂机不稳）

1. `team_start_prepare_auto` 在组队开战时 `hangup_loop_clear`，队长点挂机后第一场组队战就把地图挂机环清掉。
2. 挂机开战只走单人 `battleinfo`，未 `team_begin_battle`，队友进不了同一场。
3. 组队胜利路径只 `note_victory_settlement`，未 `hangup_loop_note_victory_reentry`（与单人 operate 胜利不对称）。

队员拦截本身已有（`team_encounter_blocked`），见 `2026-08-01-team-leader-only-encounter.md`。

## 修改

1. `team_start_prepare_auto`：保留 HangupLoop*（`hangup_kept`），不再清环。
2. 挂机 `useSceneMonsterStart` + 队长 + 人数≥2：team battleinfo → `team_begin_battle` → `team_start_prepare_auto("team-battle-hangup-start")`；prefer arm 在 begin 之后。
3. 组队 round-merge / terminal-release 胜利补 `hangup_loop_note_victory_reentry`。
4. 队员拒绝文案改为「只有队长可以挂机。」。
5. 队员 poll / start-delay / settle-exit **不得** `hangup_loop_clear`（环是进程全局，清了会拆掉队长挂机）；仅跳过投递。
6. `hangup_loop_note_victory_reentry`：当前 active 为队员时 skip，由队长 settle/exit 调度下一场。

## 验证

1. `make -j2 server`
2. 两人组队：队员点挂机 → `hangup-reject-non-leader`，提示挂机专用文案，不启循环。
3. 队长点挂机 → `mock_team_battle_start ... source=leader-hangup`，队友入队开战；胜后继续下一场挂机。
4. 单人挂机 / 无队不受影响；队员走怪仍「只有队长可以遇怪。」。
