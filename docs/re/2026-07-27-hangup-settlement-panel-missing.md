# 挂机胜利看不到结算画面

Date: 2026-07-27

Status: implemented (server)

```text
phase: killing 4/6+inline 4/7 -> hangup continuous 4/2 (AwaitingSettlement)
       -> pending_settlement operate-followup 4/8 (too early)
trigger: 自动挂机打赢后结算框一闪没有 / 完全不显示；紧接地图或下一场
```

## 根因摘要

1. 胜利默认内联 `4/7`，并武装延迟 `4/8`（`settlement_exit`，约 2.5s）。
2. `2026-07-27-hangup-auto-group-skill-stall` 去掉了对真实 `4/2` 的
   `turn_gap_wait(4/11)` 后，挂机连续 `4/2` 在结算窗内立刻进
   `pending_settlement`。
3. **首个偏离**：`settlement_sent_serial` 已匹配时，`operate-followup` **马上**
   发 `4/8+4/11+4/9`，Battle.cbm 在面板绘出前被拆掉 → 玩家感觉「没结算」。
4. 旧 `turn_gap_wait` 碰巧挡住这波 `4/2` 约 3s，掩盖了该竞态；修群攻卡死时暴露。

## 修复

1. 已发 `4/7` 且 exit delay 未到：对 follow-up `4/2` 回 **empty-ack**，不发 `4/8`。
2. delay 到期后的 follow-up / poll 仍发完整 exit 包。
3. 尚未发过 `4/7` 的 pending 路径：只下发 `4/7`（+可选掉落），**同包不再夹**
   `4/8`（同包会空白结算框）；改武装 poll 延迟离场。

## 验证

- [x] `make -j2 server`
- [ ] 挂机胜利：日志先有含 `4/7` 的 `mock_battle_operate` / inline，再有
      `settlement_exit_arm`；约 2.5s 内 follow-up 可见
      `hold-exit-for-panel`；之后 `settlement_exit phase=poll-delayed`
- [ ] 结算框可见约 2.5s 再回地图；长时间挂机仍不卡死在结算框
