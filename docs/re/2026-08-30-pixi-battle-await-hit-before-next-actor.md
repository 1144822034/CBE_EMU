# 战斗播报：对面特效未完本方已出手

日期：2026-08-30

## 症状

同一回合（或反击接本方）里，对面攻击/技能特效或受击反应尚未结束，本方冲刺/施法已经开始。

## 根因

`playActionScript` 在动作与 eidolon 之后对受击使用 `void playHitMotion`，仅尾随 `sleep(450)`。`playHitMotion` 约 520ms，且多目标并行时更依赖真实 await；未等完就进入下一条 `actioninfo`，或 `playActionScript` resolve 后菜单/`onAutoReady` 放行下一手。

## 修改

`BattleScreen.playActionScript`：收集 hit promises → `await Promise.all`，再短间隔 `sleep(220)` 作为记录间拍点。

续（仍抢一点）：

- `playHitMotion` 只等了 520ms，pose hold 700ms → 下一条叠在残留受击上；改为等满 hold 并回 idle。
- 施法 `await cycleMs` 短于 `hold=cycleMs+120`，特效起播时施法姿未收完。
- eidolon 750ms 偏短；改为 ~1100ms。
- 记录尾改为 `waitBattleVisualSettle`：等到非 death 的 attack/cast/hit pose 与 `fxHost` 子节点清空，再 +320ms 拍点。

## 验证

1. 怪先手普攻/技能：受击与特效收干净后再见玩家出手。
2. 玩家先手后怪反击：反击播完再出菜单或自动下一手。
3. 群体技能多目标受击并行，整段结束后才下一条。
4. 连续多段反击不应再「抢攻一点」。