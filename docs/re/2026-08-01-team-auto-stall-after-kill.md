# 组队自动：队长击杀后按 1 卡住等超时普攻

日期：2026-08-01

## 症状

组队战中队长已用技能打死怪并 `round_defer`，队友按 1 开自动后无操作，约 20s 后
`team_battle_round_timeout_autofill operate=0`。

## 运行时证据

```text
team_battle_round_defer ... acted=01 ... enemyhp=0/20
mock_battle_auto_synth ... operate=23 ... team=1
mock_battle_auto11_flag ... hangup-style-arm last=0 operate=0
  enemyhp=0 rolehp=484 prefer=1 pending=0 team=1
mock_battle_auto12_cancel ... pending=0 ... hangup-style-no-inline-synth
... polls prefer=1 pending=0 next_act_ms=0 ...
team_battle_round_timeout_autofill ... operate=0
```

队长技能记忆与自动合成正常；队友 `seat_can_act` 仍为真（terminal pending），
但 `arm_pending` 因 `all_enemies_defeated` 清掉 pending，poll 永不 synth。

## 根因

`vm_net_mock_battle_auto_arm_pending_ex`（及 flag arm）把「敌方 HP=0」当成
solo 终局，组队回合屏障下仍欠座位操作时错误拒武装。

## 修改

敌方已灭时，若处于组队且 `auto_team_seat_can_act`，仍允许 `arm_pending` /
`arm_flag_pending`，由随后 scene-poll `auto_synth` 提交座位操作（普攻或记忆技能）。

## 验证

1. 组队：队长技能秒杀 → 队友按 1 → 应见 `auto_arm_pending`/`auto_synth`，
   **不应**再等 `round_timeout_autofill`。
2. 双方均未按 1 时，超时普攻门闩仍有效。
