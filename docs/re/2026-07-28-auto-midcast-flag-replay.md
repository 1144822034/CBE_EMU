# 有蓝时自动战斗重复播放施法动作

Date: 2026-07-28

Status: implemented (server)

```text
phase: hangup/challenge auto skill (operate=3, mp>0)
       -> 4/6 actioninfo playing
       -> client 4/12
       -> auto12-keep-prefer arms flag_pending
       -> poll 4/11 type=1 mid-hold
trigger: 挂机或跨场 prefer=1；有 MP 放技能时施法动画播两遍
```

## 日志证据（用户提供，lxh001）

1. `mock_challenge/hangup_battle_start ... rolemp=328/328` — 开战已有蓝，开战包带自动。
2. `mock_battle_operate ... skill=1 mpcost=10 actions=3|4` + `playback_hold hold_ms≥4200`。
3. 同场立刻：`mock_battle_auto_arm_flag reason=auto12-keep-prefer` →
   `mock_battle_auto12_cancel ... flag_pending=1` →
   `mock_battle_auto_flag_poll_deliver`（仍在 hold 窗口内）。
4. 无第二包 `mock_battle_operate`，但客户端在播 `actioninfo` 时又收到开战同款
   `4/11 type=1`，体感为施法动作重播。无蓝时多走普攻，该重播不明显，故表现为
   「有蓝时还会重复播放施法」。

## 根因

| 环节 | 问题 |
| --- | --- |
| 开战 | hangup/challenge prefer：**开战同包** `4/11 type=1`（隐藏操作栏；取消窗只挡 synth，见 `2026-07-28-hangup-start-countdown-1-flash.md`） |
| 每刀后 `4/12` | `auto12-keep-prefer` **无条件再** `arm_flag_pending` |
| poll | `flag_poll_deliver` 在 `playback_hold` 内仍投 `type=1` |

中途按钮开自动仍需要**一次** poll 补旗（见 `2026-07-25-battle-auto-flag-stall.md`）；
开战已带旗的路径不能每回合再补。

## 修改

1. `g_mockBattleAutoHangupStyleFlagOk`：开战已带 `type=1`，或本场已成功
   `flag_poll_deliver` 一次后置位；随账号 battle 状态持久化。
2. `arm_flag_pending` / `auto12-keep-prefer`：`HangupStyleFlagOk=1` 时不再武装。
3. `flag_poll_deliver`：若仍在 `playback_hold`，推迟 `not_before`，禁止中途投递。

## 验证

1. 挂机有蓝放技能：一刀内只应见一次 `mock_battle_operate`；**不应**再出现
   `auto12-keep-prefer` → `flag_poll_deliver`（可有 `arm_flag ... action=skip`）。
2. 手动首场再点自动：仍应有一次 `flag_poll_deliver`（中途对齐开战态）。
3. 多怪 hold 结束后才下一刀；`make -j2 server` 通过。
