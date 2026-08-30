# 战斗播报并行踩踏：三怪第一只后己方又动手

日期：2026-08-30

## 症状

对面 3 只怪时，第一只怪动作结束后己方又开始冲刺/施法，其余怪反击被盖住或像被跳过。把记录间等待加长后更严重。

## 根因

`applyUpdate` 对每次 `4/6` 都 `void playActionScript`，并用 `playbackGen++` 丢弃上一包的 `finally`。播报变长后，自动/poll 下一包更容易在上一包未播完时到达 → **两段 actioninfo 并行**（文档已有同类：`2026-07-28-battle-auto-stomp-counter-playback.md`）。体感即「第一只怪后己方出手」。

记录间超长 `waitBattleVisualSettle`（等 pose）加剧了叠包窗口，不是本席与后手怪同脚本内的顺序问题。

## 修改

`BattleScreen.ts`：

1. `playbackChain` 串行排队；`playbackDepth` 未清零前不开放菜单/`onAutoReady`。
2. `hide`/`show` 重置 chain/depth。
3. `waitBattleVisualSettle` 只排空 `fxHost` + 短拍，不再空转等 pose。

保留：受击 `await` 满 hold、施法等满 hold、同包 settlement 不在播前清 left HP。

## 续：串行后「报错 + 提前退出」

症状：`monster-vs-monster dash actorWire=1 targetWire=1`（及 2→2），随后 `world-boss ... post-battle` 提前回地图。

根因两层：

1. **误报**：subtype-10 下怪自疗/自 buff 为 `actor==target` 左席；`console.error` 不抛异常，不是退场原因。
2. **真退场**：播报变长后，延迟单独 `4/8`（`finished`、无 settlement、无 actions）在 `main.onBattleUpdate` 里立刻 `endBattleToWorld`，把仍在 `playbackChain` 里的多怪 `4/6` 撕掉。

修改：

- `main.ts`：战斗中且 `isPlaybackBusy()` 时不因光杆 `4/8` 立刻离场；交给队列播完 → `onExitReady`。
- `finishActionPlayback`：`finished` 仅在 `drained` 时 `onExitReady`。
- 怪打怪日志：仅异 wire + 进攻 child 才 `console.error`。
