# 连续挂机约 20 分钟卡在结算界面

Date: 2026-07-27

Status: implemented (server)

```text
phase: hangup victory -> inline 4/7 panel -> delayed 4/8+4/11+4/9 exit
       -> hangup poll (only after AwaitingSettlement cleared)
trigger: 连续挂机约 20 分钟后停在结算框；此前多场可正常循环
```

## 根因摘要

1. 胜利路径默认只内联 `4/7`（同包 `4/8` 会空白结算框，见
   `2026-06-25-battle-server-flow.md`）。
2. 远程服务上 `sceneVisibleReady` **战斗期间仍为 true**，挂机 poll 的
   「等 mmGame 拆完」注释实际不成立。
3. 挂机 poll 只判断 `OperateSessionArmed`，**不判断** `AwaitingSettlement`，
   可在结算 UI 仍打开时再投下一场 `4/5`。
4. 结算后续 operate 若走 `4/11 type=0`，会清 hangup prefer，且缺 `4/8`
   时 Battle.cbm 拆不干净，长时间循环后易卡在结算框。
5. 掉落 `7/7` 构建失败曾 `return false` 整包胜利响应，也会留下残缺结算态。

## 修改

1. 胜利后武装 `settlementExitPending`（默认 2500ms，
   `CBE_BATTLE_SETTLEMENT_EXIT_DELAY_MS`）。
2. scene-sync poll（在 `sceneVisibleReady` 之前）投递
   `4/8 + 4/11 type=0 + 4/9` 拆场（**禁止**二次 `4/7` / `4/4`；见
   `2026-07-28-pve-settlement-blank-prompt.md`），清 `AwaitingSettlement`；
   服务端 keep prefer，避免 prefer 时 exit type=1 留下地图空白壳（见
   `2026-07-28-hangup-settlement-blank-shell.md`）。
3. 挂机 poll：`AwaitingSettlement` 或 exit pending 时 hold。
4. pending settlement：已发过 `4/7` 时，exit delay 内对 follow-up `4/2` **hold**
   （empty-ack），到期后再 `4/8…`；未发过 `4/7` 时只发 `4/7` 并武装延迟离场，
   **禁止**同包 `4/7+4/8`（崩/空白）。见
   `2026-07-27-hangup-settlement-panel-missing.md`。
5. 掉落 `7/7` 失败改为 skip + warn，不打断 `4/7`/离场链。

## 验证

1. 挂机多场：日志 `mock_battle_settlement_exit_arm` →
   （可选 `hold-exit-for-panel`）→
   `mock_battle_settlement_exit phase=poll-delayed` →
   之后才有 `mock_hangup_loop_poll_deliver`。
2. 结算框约 2.5s 可见后再自动收起，回到地图再进下一场。
3. 长时间挂机（20 分钟+）不再停在结算界面，也不再「完全没结算框」。
4. 显式关自动 / 逃跑仍清 hangup 循环。
