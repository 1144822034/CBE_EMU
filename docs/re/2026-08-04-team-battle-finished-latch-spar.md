# 2026-08-04：组队胜后 battleActive 残留挡切磋 / 脏 presence

## 问题

组队战胜利只置 `battleFinished=1`，历史上长期保留 `battleActive=1`（便于未投完的
共享 `4/6` 继续 poll）。副作用：

1. 切磋 `duel_begin` 只看 `team->battleActive` → 刚打完组队再切磋会
   `accepted=0`（与切磋 release hold、交易占槽同类）。
2. presence 在 `battleActive` 时仍读战斗快照 HP，胜后 0 血可能污染下场种子。
3. 挂机路径此前已用 `!battleFinished` 规避，但 latch 本身不释放。

## 修改

- `team_battle_in_progress` = `battleActive && !battleFinished`。
- 切磋门禁、presence 权威 HP、auto vitals pull、`in_team_battle` 均用该判定。
- `team_battle_release_if_settled`：`battleFinished` 且无未投递 `battleEvents`、
  无 pending `4/5` 时 `team_battle_clear`（`reason=finished-settled` 等）。
- 在 action 投递、缺席退休、finish/flush/abort 路径调用 release。

未投完终局 `4/6` 前仍保持 `battleActive`，不截断队友结算包。

## 复测

1. 组队打完一场：日志可见 `team_battle_clear ... reason=...settled`（或 action 投完后）。
2. 立刻再邀切磋：应 `accepted=1` + `duel_begin`，不得因组队残留失败。
3. 组队再遇怪 / 挂机下一场仍正常。
