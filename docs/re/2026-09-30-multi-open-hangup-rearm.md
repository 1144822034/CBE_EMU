# 多开切号后挂机不继续

日期: 2026-09-30

## 触发

1. A 号挂机 → 切到 B 号挂机 → 再切回 A：A 挂机环停住。
2. 再切回 B：B 偶发也不继续。

## 预期 / 实际

- 预期：非活跃槽继续挂机（遇怪环 + 战内 4/2）；切回时若在地图且 `hangupDesired`，应继续下一场。
- 实际：`hangupDesired` 仍在，但下一场 4/1 定时器不武装；后台战结束也不走 `onLeftBattleForHangup`。

## 根因

1. **重武装只在活跃 UI**：`endBattleToWorld` → `onLeftBattleForHangup`。后台 `gateUi` 只 stash `pendingBattle*`，`GameSession.inBattle=false` 后不续环。
2. **切回世界不续挂**：`switchAccountSlot` 恢复地图时未调用挂机续环；若后台已结束战斗（`!inBattle` 仍留死 stash）直接进世界，环仍断。
3. **后台无 4/2**：挂机-alone 依赖 `BattleScreen`/`fireInBattleAuto`；切走后战内出手停，战斗易卡死或仅靠尾包结束。

## 修改

| 位置 | 改动 |
|------|------|
| `GameSession.ensureHangupLoopArmed` | 保留已有 timer；否则 `onLeftBattleForHangup` |
| `main.ts` `gateUi` 后台 `onBattle`/`onBattleUpdate` | 结束则清 stash + `rearmHangupForSlot`；进行中 `scheduleInactiveHangupOperate` |
| `main.ts` `switchAccountSlot` | 切回世界 / flush 后已结束 → `rearmHangupForSlot`；切走挂机战 → 立刻后台 4/2 |
| 槽字段 `hangupBgOpTimer` / `hangupBgOpInFlight` | 无 UI 的挂机 4/2 节流 |

## 验证

1. A 挂机 → 切 B 挂机数场 → 切回 A：A 应在约 8s 内再遇怪或已在战。
2. A 挂机切走后不切回：后台应继续打完并进入下一场。
3. 切回仍在战中的号：战斗 UI + 挂机出手恢复。
4. 单号挂机无回归。

## 风险

- 后台 4/2 无 `canSubmitOperate`，靠短间隔重试；过密时服务器可能忽略，已用 in-flight + 450–900ms 退避。
- 后台结算累计 `hangupTally` 与活跃 UI 路径并行，切回 `restoreHangupTally` 以槽快照为准。
