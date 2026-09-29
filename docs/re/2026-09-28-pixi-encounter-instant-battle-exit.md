# 遇怪进战立即退出战斗场景

日期: 2026-09-28

## 触发

1. 打完一场（结算关掉后立刻、或仍在 `settlement_exit` 延时窗内）。
2. 马上再踩怪 / 遇怪进入战斗。
3. 战斗 UI 刚出现就回到地图。

## 预期 / 实际

- 预期：进入新战斗后停留在操作/挂机出手。
- 实际：立刻退出战斗场景。

## 契约 / 首次偏离

| 预期 | 实际（修前） |
|------|----------------|
| 开战包里的 prepend `4/8` 只拆**上一场**结算壳 | Pixi 把该 `finished` 当成**本场**结束 |
| `inBattle` 在同包 `4/1` 后保持 true | `GameSession` 见 `finished` 立刻 `inBattle=false` |

服务端契约（`mock_server_battle.c` `vm_net_mock_battle_release_settle_for_start`）：

- 上一场 `settlement_exit` / settle 未清时，下一场 `4/1` **同 WT prepend lone `4/8`**，再跟 `1/4/5`。
- CBE：先撕旧 Battle.cbm 结算壳，再进新战斗。
- 默认 `settlement_exit` 面板窗 ≈ **2500ms**（`CBE_BATTLE_SETTLEMENT_EXIT_DELAY_MS`），故「打完立马遇怪」必中。

首次偏离：

1. `GameSession` 同包先 `onBattle` 再 `parseBattleAction` → 把 prepend `4/8` 清 `inBattle` 并 `onBattleUpdate(finished)`。
2. `BattleScreen` `show()` drain / `applyUpdate` 对 lone `finished` 调 `onExitReady`。

次要：战后迟到 poll `4/8` 在 `hide` 后若误入 `pendingUpdates`，也会污染下一场（已用 `pendingShowArmed` 收口）。

## 根因

把「上一场 tear-down `4/8`」误当作「本场胜利退出」。多开 `pendingUpdates` 放大了 hide 后迟到包，但「立马再遇怪」的主路径是**同包 prepend**。

## 修改

| 位置 | 改动 |
|------|------|
| `GameSession` 收包 | 同包已有 `parseBattleStart` 且 update 为 lone finished → **跳过**清 `inBattle` / `onBattleUpdate` |
| `BattleScreen.armPendingShow` | `onBattle` 开头武装；非武装时不入队 |
| `BattleScreen.isStaleLoneExit` | 本场尚无 action/settle 且未 `awaitingExit` 时丢弃 lone `4/8`（敌方已全死则放行，供切号进已结束战） |

## 验证

1. 打完一场，结算关掉后**立刻**踩怪 → 应稳定留在战斗，可出手。
2. 等 3s+ 再踩怪 → 同样正常（exit 已 clear，无 prepend）。
3. 正常胜利：`4/6`→`4/7` 结算 → 确认/`4/8` 仍能离场。
4. 多开后台结束再切回：结算/退场不卡死。

## 风险

- 若某路径本场只有 lone `4/8`、无 `4/7`、敌方未折 HP，可能不退场（当前服务端胜利几乎总有 settle 或 actioninfo）。
