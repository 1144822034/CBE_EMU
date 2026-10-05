# CBE 挂机首场未结束就进第二场

日期：2026-10-05

## 触发与现象

CBE 模拟器点挂机（host `25/2` → replay `2/10 Type=2`）后，第一场 mmBattle
还在加载或播 `actioninfo`，第二场 `4/5` 已经下发。体感两场叠在一起。

Pixi HUD「斗」仍走本地 `4/1`（`hangup=0`），不经过 `sceneHangupEnabled`。

## 预期 / 实际

- 预期：首场 `4/5` 进 Battle.cbm，打完并看到 `4/7`「挂机中」，`4/8` 回地图后，
  再单独 poll 下一场 `4/5`。
- 实际：`CBE_BATTLE_AUTO_ENTRY_GAP_MS` 默认 0，Type=2 回包里的 `4/5` 之后
  下一拍 scene-poll 立刻 synth `4/6`。高战力一场刀死时，playback hold 只覆盖
  **最后一刀**（挂机节拍 600ms×动作 + 结算窗 1200ms），不含 CBE 冷加载
  Battle.cbm。`cbe-hangup-4/8-to-map` 随后把 `OperateSession` 清掉，约 2s
  `hangup_loop_poll_deliver` 再投下一场 `4/5`。guest 仍停在第一场 → 叠场。

权威：`mmBattle` 必须先消化首场 `4/5`/`4/6`/`4/7`；下一场只能在 `4/8` 回地图之后。
契约见 `2026-10-05-cbe-hangup-settle-combatinfo-panel.md`（禁止结算壳上再叠
`4/8+4/5`）。

## 根因陈述

第一次偏离在 **服务端挂机环时钟**，不在 guest 解析器：

1. 首场开战不给 CBE 进场窗（`entry_gap=0`）。
2. 胜后 `4/8` / 下一场 `4/5` 只按最后一刀 hold + 2s 地图等待，短于冷启动
   Battle.cbm。
3. CBE `4/8-to-map` 清 `OperateSessionArmed`，poll 不再认为「还在打」。

## 修改

仅 `sceneHangupEnabled`（CBE host `25/2`）。Pixi `4/1` 默认不变。

| 位置 | 改动 |
|------|------|
| 首场 synth | tally=0 时 `CBE_BATTLE_AUTO_ENTRY_GAP_MS` 默认 **2000** |
| 首场 `4/8` | tally≤1 时离场总等待至少 **4000ms**（`CBE_HANGUP_FIRST_SETTLE_EXIT_MIN_MS`） |
| 场间 | CBE 地图等待默认 **3500ms**（`CBE_SCENE_HANGUP_LOOP_INTERVAL_MS`） |
| poll | CBE 若 `NextActNotBeforeMs` 未到，hold 下一场 `4/5` |

## 验证

1. 重启服务端 + CBE。点挂机：`mock_battle_auto_entry_gap ... entry_ms=2000`
   （仅首场；`sceneHangupCompletedBattles=0`）。
2. 首场胜后 `settlement_exit_arm ... cbe_first_exit=1 delay_ms` ≥ 4000。
3. `mock_hangup_loop_schedule interval_ms` ≥ 3500，再 `hangup_loop_poll_deliver`。
4. 第一场 `4/7` 留在结算壳（skip-4/8）；下场 prepend `4/8`+`4/5`。结算中 `25/5`
   应 `suppress-battle-settle`，不能 DIV0（`2026-10-05-cbe-hangup-settle-second-fight-div0.md`）。
5. 第二场起 `entry_ms` 回到 0（tally≥1）。Pixi 挂机 `hangup_pace` 与 1400ms
   动作 hold 不变。
6. `make -j2` 通过。

## unresolved

guest 没有「Battle.cbm 已显示」上行；2000/4000/3500 是冷启动代理时钟，慢机
仍可能偏紧，可用上述环境变量加长。
