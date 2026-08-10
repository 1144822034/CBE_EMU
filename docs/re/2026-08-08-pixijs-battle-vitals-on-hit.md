# 2026-08-08 Pixi 战斗底栏血蓝与动作落地同步

## 现象

回合 `4/6` 到达后，底部 `BattlePartyHud`（及世界 HUD vitals）在玩家出手包一到就跳到整回合结算后的血量；怪物反击命中时条不再动。技能扣蓝也绑在同一时刻，体感像「只有自己行动后血蓝才变」。

## 根因

`BattleScreen.applyUpdate` 在 `playActionScript` 之前一次性把全部 `hpDeltas` + `teaminfo` MP 写入 `state.right`，再播动画。契约上 HP 来自 actioninfo child `valueA`、施法后 MP 来自 `teaminfo`（CBE `InitActionSlot_B` 缓存，`DrawBattleSceneBg` 在 type-1 播完提交）——见 `docs/re/2026-06-25-battle-server-flow.md`。

## 修改

- 有 `actions` 脚本时：延后写血蓝；每条记录动作/飘字落地时 `applyWireHpDelta`，type-1/2 再 `applyCasterMpFromTeaminfo`，并 `onVitalsChanged`。
- 无脚本的旧包：仍在 `applyUpdate` 立即结算。
- 播放结束再扫一遍 `teaminfo` MP（不覆盖 actioninfo 已结算的 HP）。

## 验证

1. 普攻：己方出手后怪血下降；怪物反击飘字出现时底栏己方 HP 再降。
2. 仙术：技能动作/特效结束后立刻扣蓝（及治疗加血）；怪物反击时再掉血。
3. 组队/自动回合：播完后菜单与死亡提示仍按最终 HP。
