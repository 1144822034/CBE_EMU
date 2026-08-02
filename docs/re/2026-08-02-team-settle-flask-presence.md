# 组队结算自动用壶：显示回血蓝但未生效 / 壶量暴扣

## 触发

组队战结束结算显示回蓝（或回血），角色看起来补上了，但下一场开战仍是残血蓝；
有壶的队长/队员储量几场就被扣光。

## 运行时证据（session `3a0fd2`）

1. `battle_flat_charge`：`803` 仅回复 `b=35`，却扣 `c=16666`（结算 auto-flask 误用战斗手
   动固定扣费）。
2. `team_settle_after_flask` role=456：`restoredMp=35`，`roleMp=775`，但
   `onlineMp=740` / `teamMp=740` 未同步。
3. 下一场 `team_begin_seed`：仍用 `onlineMp=740` 播种，盖过 durable `roleMp=775`。

## 根因

| 项 | 说明 |
| --- | --- |
| 首个偏离 | 组队 peer 结算重建 `4/7` 时跑 auto-flask，只写 role/globals，不经 `finish_operation` |
| 契约违反 | 下场 `team_battle_queue` 优先 `onlineHp/Mp`；未回写则恢复不粘着 |
| 暴扣 | 结算 auto-flask 误走手动战内 `battle_flask_charge`（固定 `50000/3`） |

## 修复

1. `vm_net_mock_battle_sync_team_presence_after_settle`：组队结算后把 settled HP/MP
   写回 `online*` 与 `battleMember*`。
2. 结算 auto-flask 按实际回复量扣储；固定 `50000/3` 仅保留给战斗内手动用壶。

## 验证

1. 组队缺蓝收场 → `team_settle_after_flask` 中 `roleMp == onlineMp == teamMp`。
2. `settle_restore_charge` 的 `c`（扣储）等于 `b`（回复），不是 16666（除非回复本身到帽）。
3. 下一场 `team_begin_seed` 的 `onlineMp` 为结算后值。
