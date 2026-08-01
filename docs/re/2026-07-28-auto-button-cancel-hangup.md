# 自动按钮全程可关挂机

Date: 2026-07-28

Status: implemented (server) — 5s 取消窗已移除（2026-07-28）

```text
phase: hangup/auto prefer on -> player taps auto off (4/11 type=0)
       -> prefer=0 + hangup_loop_clear
       -> current fight may finish; no map-side re-entry
trigger: 自动战斗中希望随时点按钮停挂机；本场结束后不再续挂
```

## 根因

1. 多怪 `playback_hold` 曾用 `max(gap, play)`：当 `play > gap` 时动画一结束
   立刻 synth，几乎没有取消时间（已改为 play+gap；现 gap 默认 0）。
2. `4/11 type=0` 曾要求在战斗屏；地图侧挂机 pending 时点关会被拒（已修）。

## 修改

1. `playback_hold` = 动作播放时长 + 可选 `CBE_BATTLE_AUTO_TURN_GAP_MS`
   （**默认 0**，无取消窗；仅播完 actioninfo 再 synth）。
2. 开战 hangup/challenge prefer：`arm_pending(cancel_window=0)`，开战包内联
   `4/11 type=1`（隐藏操作栏）。
3. `4/11 type=0`：战中 / 结算 / 挂机 pending 的地图侧均可关自动并清 hangup。
4. 若需恢复出手间隔：设 `CBE_BATTLE_AUTO_TURN_GAP_MS=5000`。

## 验证

1. 挂机进战：`arm_pending ... gap_ms=0 cancel_window=0`；开战含 `+4/11`。
2. 进场后可尽快 `auto_synth`（仅受 actioninfo 播放 hold 约束）。
3. 多怪：`playback_hold ... hold_ms` ≈ `actions*1400`（无 +5000）。
4. 关自动：`auto11_flag type=0 ... prefer=0 hangup=0`，无后续 `hangup_loop_schedule`。
