# 自动/手动：怪反击动画被下一刀合成打断

Date: 2026-07-28

Status: implemented (server)

```text
phase: 4/2 operate (actions=4, counters=2) -> mid-playback 4/11 type=1
       -> immediate auto11 synth 4/6 -> client skips remaining counters
trigger: 正常打怪或挂机；日志已有 counters>0 但体感「怪没行动就跳出」
```

## 日志证据（用户提供）

首场 `enemies=3`：

1. 手动 `4/2`：`counters=2 deaths=1 actions=4` — 服务端已带两只怪反击。
2. 紧接着 `4/11` → `auto11` **立刻**再合成一刀（`counters=1`）。
3. 再 `auto-poll` 收割最后一只并结算。

契约上怪已出手；客户端还在播上一包 `actioninfo` 时被新的 `4/6` 打断，
反击段看不到，表现为「玩家行动后怪没动就下场」。

次场 `enemies=1` 一技能秒杀 `counters=0` 是合法杀招结算，不是本 bug。

## 根因

1. `auto_note_client_operate` / 回合间隙原先只在 `prefer=1` 时武装。
2. 手动首刀时 prefer 仍为 0 → **不设** `NextActNotBeforeMs`。
3. 中途开自动（`4/11 type=1`）**无条件立即 synth**，踩掉上一回合播放。
4. 固定 3s 间隙也偏短：`actions=4`（攻+死+2 反击）需要更长 hold。

## 修改

1. 每次消耗回合的 operate 结束调用 `mock_battle_playback_hold`：
   - `hold_ms = max(3s, actions * 1400ms)`（可用 env 调）
   - 不依赖 prefer，专挡后续 synth。
2. `in_turn_gap` 不再要求 prefer。
3. `auto11 type=1` 若仍在 hold → 只 ACK `4/11`，`defer-synth`，等 poll。
4. `arm_pending_after_act` 的 cancel 窗同样按上一回合 `actions` 缩放。

## 验证

1. 三怪：手动打死一只，日志 `playback_hold actions=4 hold_ms≥5600`。
2. 立刻开自动：应见 `auto11 ... action=defer-synth remain_ms=...`，**不应**马上又一条 operate。
3. hold 结束后才 `auto_poll` / synth；应能看完怪反击再进下一刀。
4. `make -j2 server` 通过。
