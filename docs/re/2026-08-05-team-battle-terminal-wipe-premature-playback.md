# 组队战斗：击杀后 terminal-wipe 抢先播放

日期：2026-08-05

## 症状

三人组队进战：第 1、第 2 名队友操作后怪已死，共享行动动画立刻开始播放；第 3 名队友尚未操作（未提交 `4/2`）。

## 触发与证据

队长 `c0f3b0dc` / 蔡文姬 → 相互 `e6d02b38` 收刀 → 阿狸 `7bb7878c` 尚未出手：

```text
team_battle_round_capture ... source=c0f3b0dc actor=0 ... acted=00
team_battle_round_defer ... resp=5
team_battle_round_terminal_capture ... source=e6d02b38 actor=2 acted=01
team_battle_round_autofill_begin ... via=terminal-wipe acted=05
team_battle_round_prepare ... source=7bb7878c actor=1 ... resolve=1
team_battle_round_terminal_release ... source=7bb7878c actor=1
team_battle_action_deliver ... observer=e6d02b38 ... terminal=1 resp=689
```

第一次偏离：`terminal_capture` 后立刻 `via=terminal-wipe`，替空闲存活席合成操作并 `terminal_release`，合并 `4/6` 在第三人真实 `4/2` 之前投递。

## 根因

`peer-action-starve` 曾把「终局立刻 wipe 空闲席」当作缩短 defer→投递窗口的手段。该契约与回合屏障冲突：

1. 非末席 `4/2` 必须 `defer`，共享 `4/6` 只能在**全部存活席提交后**（或 `CBE_TEAM_BATTLE_ROUND_TIMEOUT_MS` 超时 autofill）再 release。
2. 击杀只设置 `battleRoundTerminalPending`；后续席位走 `terminal_wait`（计选择、不打已死目标），末席 `terminal_release`。
3. 立刻 `terminal-wipe` 把在线、可操作席位当成 AFK，破坏「等人操作」契约。

崩溃/提前播放是症状；被违反的契约是**存活席操作屏障**。

## 修改

1. 删除 `terminal_capture` / `terminal_wait` 路径上的即时 `try_round_autofill(..., "terminal-wipe")`。
2. 击杀后仍 `finish_operation(..., false)` + `deferred_ack`（`resp=5`），不把 pending 合并包塞进本响应。
3. AFK 仍由既有 `round-timeout` autofill 推进；在线席由玩家 `4/2` 或超时合成。
4. `terminal_wait`：若本席补齐后 `acted==alive`，直接 `terminal_release`（末席安全网）。

## 验证

1. `make -j2`
2. 三人组队：两人快速出手打死怪，第三人故意慢几秒再操作 → 不应出现 `via=terminal-wipe`；第三人操作后才有 `terminal_release` / `action_deliver`。
3. 第三人停手 > `CBE_TEAM_BATTLE_ROUND_TIMEOUT_MS`（默认 20s）→ 仍应出现 `timeout_autofill`，随后合并播放。
4. 无人超时的齐射路径不应出现任何 autofill。

## 相关

- `docs/re/2026-08-01-team-battle-round-timeout-autofill.md`
- `docs/re/2026-08-05-team-battle-peer-action-starve.md`（第 3 条终局 wipe 已作废）
- `docs/re/2026-08-05-team-battle-terminal-synth-exit-starve.md`
