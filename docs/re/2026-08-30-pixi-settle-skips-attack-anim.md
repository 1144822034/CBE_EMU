# 击杀结算包跳过玩家攻击/技能动画

日期：2026-08-30

## 症状

战斗中玩家动作与技能特效不播，对面直接播死亡动画，随后战斗结束（结算面板）。

## 触发与链路

1. 单人 PvE 致死一击：同一 WT 内联 `4/6 actioninfo`（攻击/仙术 + type-3 死亡）+ `4/7` 结算。
2. `BattleScreen.applyUpdate` 见 `settlement` 后立刻把全部 `state.left[].hp = 0`。
3. `playActionScript` 的 `isUnitDefeated`（`dead` / `hidden` / `hp<=0`）判定进攻目标已败。
4. `allOffensiveDefeated` 分支跳过冲刺/施法/eidolon，只提交 delta；随后 type-3 播死亡；`finally` 出结算。

第一次偏离在客户端：**结算预清零发生在 actioninfo 回放之前**，与「按回放时刻存活状态门闩后手打尸」契约冲突。组队后手打尸门闩本身仍正确；被破坏的是本席致死记录。

## 根因

`isUnitDefeated` 依赖回放过程中逐步落地的 HP；`applyUpdate` 在播报前因 `4/7` 存在而把左侧 HP 清零，使致死记录被当成「已败目标」整段跳过演出。

## 修改

`PixiJS_web/src/ui/BattleScreen.ts`：

- 去掉 `applyUpdate` 入口处的 settlement → left HP 预清零。
- 在 `playActionScript` 结束后的 `finally` 再清零左侧 HP（deltas / type-3 已播完），再出结算面板。

## 验证

1. 单人打怪致死：应先见玩家攻击/施法与特效，再死亡 splash，再结算。
2. 组队一人先杀怪、后手记录仍指向尸：后手仍应跳过冲刺（`isUnitDefeated` 仍有效）。
3. 非致死回合动画与 MP/飘字不变。

## unresolved

- 无。
