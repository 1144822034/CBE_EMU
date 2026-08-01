# 挂机自动须开战即 type=1（隐藏操作栏）

Date: 2026-07-28

Status: implemented (server)

```text
phase: hangup/challenge prefer start
trigger: 5s 取消窗内仍显示手动操作界面，挡住自动按钮；正常自动不应出操作栏
```

## 根因

曾为消除开战「1→0 再 19」短钟，把 `4/11 type=1` 延到取消窗之后再 poll。
但 `flag_poll` 在 `in_turn_gap` 内也会 defer，取消窗期间 **无法** 补到 type=1。
客户端一直停在手动操作 UI，盖住自动按钮。

`type=1` 负责自动态 UI（收起技能/道具操作栏）；5s 取消窗只挡 **poll synth**。
二者不能绑在同一延迟上。

## 修改

1. hangup / challenge prefer 开战包 **恢复内联 `4/11 type=1`**。
2. `HangupStyleFlagOk=1`；`arm_pending(cancel_window=1)` 仍只延迟首击 synth。
3. 去掉 `arm_start_delayed_flag`（取消窗后补旗）。

已知代价：开战仍可能先闪客户端约 1s 短钟再进 ~20s 回合钟；优先保证自动 UI
与可点关。

## 验证

1. 开战响应含 `+4/11`；无 `start-delay-type1-after-cancel-gap`。
2. 进场即自动态、无手动操作栏；可点自动关挂机。
3. ~5s 内无 `auto_synth`；之后正常出手。
