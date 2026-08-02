# 组队结算：队长看不到结算界面（队友正常）

日期：2026-08-01

## 症状

组队胜利后，队友能看到 `4/7` 结算面板；队长偶发直接回地图，无结算 UI。

## 运行时证据（battle=2，队员收刀）

```text
team_battle_round_release ... source=2f13d3ba ... actions=4 resp=752
mock_battle_settlement_exit_arm reason=team-battle-round-merge
  play_ms=2799 panel_ms=2500 delay_ms=5299   ← 队员：含播片

mock_battle_settlement_exit_arm reason=team-battle-peer
  play_ms=0 panel_ms=2500 delay_ms=2500       ← 队长：播片=0
mock_battle_settlement_exit ... response=4/8+4/11+4/9  ← 队长先拆场
```

## 根因

1. 非收刀席位（常为先出手 defer 的队长）经 `team-battle-peer` 轮询拿
   `4/6+4/7`。
2. `settlement_exit_arm` 用本席 `AutoNextActNotBeforeMs` /
   `LastRoundActionCount` 算 `play_ms`；peer 投递时本席 hold 早已过期 →
   `play_ms=0`。
3. 只等 `panel_ms=2500` 就发 `4/8`，而 peer 包里的 `4/6` 播片常需
   ~`1400ms * actionnum`。拆场发生在结算面板之前。

收刀席位若在 merge/terminal_release 时 hold 仍有效，则 `play_ms>0`，故队友正常。

## 修改

1. `team_battle_event` 记录排队 `4/6` 的 `actionCount`。
2. peer 投递 terminal 前：`note_round_playback_hold(event->actionCount)`。
3. `round-merge` / `terminal-release` 在 `note_victory` 前按合并
   `totalActionCount` 重武装 hold（defer 后本席 timer 也常已过期）。

## 验证

1. 组队多怪：队长先出手 defer，队员收刀 → 双方均见结算面板。
2. 日志 peer arm：`play_ms` 约为 `1400 * actionnum`（非 0）。
3. 队长 `4/8` 不早于队员；双方 `settlement_exit` 前有足够 panel 窗。
