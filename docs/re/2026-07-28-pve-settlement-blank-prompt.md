# PvE 结算后空白提示 / 「逃跑成功」误弹

Date: 2026-07-28

Status: fixed (server) — 非挂机恢复 `4/8` 拆场；挂机仍 skip 延迟 `4/8`

```text
phase: victory inline 4/7 (+[挂机中]) -> panel hold
       -> hangup: no exit packet; schedule next hangup start
       -> non-hangup: 4/8+4/11 type0+4/9
trigger: 曾用 4/4 躲空框 → 普通遇怪结算后弹「逃跑成功」
```

## 根因

1. 胜利只内联 `4/7`，延迟拆场曾试验 `4/4+4/11+4/9`（切磋曾用 escape
   清标记、不刷 `UpdateCharAttrs`）。
2. `1/4/4 result=1` 走 `HandleServerBattleCmd` **逃跑成功**分支，固定文案
   「逃跑成功」——不是胜利离场契约（见切磋负向证据、
   `2026-06-28-battle-item-use.md`）。
3. 权威非挂机拆场仍是 `4/8`（`mmBattle:0x7DF6`）+ `4/11 type=0` + `4/9`
   （`2026-07-27-hangup-settlement-stuck.md`：**禁止**用 `4/4` 代替）。

## 修改

1. **挂机续场**：到期仍不发 `4/8`；保持 `AwaitingSettlement`；
   `hangup_loop_schedule`；下一场 `hangup start` 清场。
2. **非挂机离场**：恢复 `4/8+4/11+4/9`。
3. **停止挂机且需拆场**：同包 `2/10` + `4/8+4/11+4/9`（同样禁止 `4/4`）。

## 验证

1. 普通触怪胜利：日志 `settlement_exit ... response=4/8+4/11+4/9`，
   **无**「逃跑成功」。
2. 挂机：`response=empty evidence=skip-4/8-hangup-reenter`，约 interval 后
   `hangup_loop_poll_deliver`。
3. 若 `4/8` 再闪空结算壳：另开取证（timing / fdata），**不得**再改回 `4/4`。

## 相关

- `2026-07-27-hangup-settlement-stuck.md`
- `2026-07-28-hangup-settlement-blank-shell.md`
- `2026-07-28-hangup-button-toggle-off.md`
