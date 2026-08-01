# 多怪胜利空结算界面

Date: 2026-07-28

Status: implemented (server)

```text
phase: multi-monster killing 4/6+inline 4/7 (actions=2+)
       -> settlement_exit 4/8 too early
       -> map_actor_vitals 1/1/14 during settle panel
trigger: 多只怪打完后结算框是空的；单怪有时不明显
```

## 日志证据

`enemies=2` 杀招：

```text
mock_battle_settlement_exit_arm ... delay_ms=2500 not_before_ms=21114
mock_battle_playback_hold ... actions=2 hold_ms=3000 not_before_ms=21614
mock_battle_operate ... order=action6-first resp=673
map_actor_vitals_sync ... via=scene-poll   ← 结算离场前
mock_battle_settlement_exit phase=poll-delayed
```

`4/7` 奖励非零（`exp_gain=14 gold_gain=10`），不是零增量崩溃路径。

## 根因

1. 同包 `4/6+4/7`：客户端先播 `actioninfo`（杀招常 `actions=2` 攻+死 ≈3s），
   再画结算面板。
2. `settlement_exit` 原先固定 `now+2500`，且在 `playback_hold` **之前**武装 →
   `4/8` 在动画未播完/面板未稳时拆 Battle.cbm → **空结算框**。
3. scene-poll 里 `map_actor_vitals_sync` 排在 exit 之前，结算窗内又推 `1/1/14`，
   进一步冲掉面板。

## 修改

1. operate 结束：先 `playback_hold(actions)`，再 `note_victory` / `exit_arm`。
2. `exit_arm`：`not_before = now + play_ms + panel_ms`（play 取当前 hold）。
3. `AwaitingSettlement` / exit pending 时不投递 map vitals。
4. scene-poll：settlement exit 先于 map vitals。

## 验证

1. 两怪/三怪打完：日志
   `settlement_exit_arm ... play_ms=3000 panel_ms=2500 delay_ms=5500`。
2. 结算框应显示经验/金钱约 2.5s+，再 `settlement_exit`；其间无
   `map_actor_vitals_sync`。
3. `make -j2 server` 通过。
