# 组队仅队长可遇怪

日期：2026-08-01

## 现象 / 契约

组队开战（`team_begin_battle`）本身已要求队长发起，但队员仍可通过：

- 地图走怪 `4/1` → 落入单人开战
- 挂机 Type=2 → 单人挂机循环

与「组队由队长拉怪」的预期不符。副本挑战 `30/9` 已用 `isleader` 拦截。

## 修改

1. `vm_mock_service_active_session_team_encounter_blocked`：`memberCount>=2` 且非队长则拦截。
2. `4/1` challenge：回 `2/10+25/11`（「只有队长可以遇怪。」）；副本 followup `forceNonScene` 回 `0`。
3. 挂机 start / poll / start-delay：同样拦截并 `hangup_loop_clear`；已在战斗中的二次挂机 stop-after 仍放行。
4. `team_add_member`：入队方若为当前 active client，清掉其挂机循环。

## 验证

1. `make -j2`
2. 两人组队：队员走怪 → 日志 `action=reject-non-leader`，不进战斗；队长走怪 → `team_battle_queue` / `mock_team_battle_start`。
3. 队员点挂机 → `hangup-reject-non-leader`，不启循环。
4. 切磋 / 单人（无队或仅自己）不受影响。
