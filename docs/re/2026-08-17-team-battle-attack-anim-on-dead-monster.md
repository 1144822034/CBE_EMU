# 组队战斗：怪已死后仍播后手攻击动画

日期：2026-08-17

## 症状

组队打怪时，队友先出手把怪打死（含死亡 splash），后出手玩家仍会对已死怪播放冲刺/普攻或仙术施法动画。

## 触发与链路

1. 各席 `4/2` 提交时服务端按当时存活目标结算并 `capture` 本席 `actioninfo`（伤害已写入共享 HP）。
2. 末席 `merge` 按**冻结花名册顺序**拼接各席 actioninfo，再统一下发共享 `4/6`。
3. Pixi `BattleScreen.playActionScript` 按合并顺序回放：先席致死 delta（及 type-3 死亡）后，本地位面 `hp<=0` / `dead` / `hidden`。
4. 后席记录仍带对原 wire 的负 delta；客户端原先只跳过**已死 actor** 的攻击，不检查**目标**是否已败 → 仍 `playAttackMotion` / eidolon FX。

第一次偏离在客户端回放契约：对已败目标仍播进攻演出。服务端合并顺序与“提交时目标仍存活”一致，不是伪造打尸伤害；问题是本地时序下不应再演出。

## 根因

`playActionScript` 未按回放时刻的目标存活状态门闩进攻动画。`playHitMotion` 虽跳过 `dead/hidden`，但冲刺/施法在 HP 结算与死亡记录之前就已开始。

## 修改

`PixiJS_web/src/ui/BattleScreen.ts`：

- `isUnitDefeated`：`dead` / `hidden` / `side.hp <= 0`
- 进攻 child（`delta < 0` 且非 flag=3）若全部已败：只提交 HP/MP delta，跳过攻击/施法/特效/飘字与尾部等待
- 部分目标存活（AOE）：朝向与 FX 仅落在存活 wire
- 单目标已败但本 record 混有治疗等：仅对已败进攻目标跳过 hit/飘字

## 验证

1. 双人以上组队打单怪/多怪；一人致死后，后手席不应再对该尸冲刺或放尸脚特效。
2. 同回合 AOE：已死 wire 无 FX，存活怪仍有受击与飘字。
3. 治疗/药品/己方 buff 路径不受影响。
4. 正常单人回合与未致死攻击动画不变。

## unresolved

- 是否在 merge 时按回放顺序重算后席目标（服务端 retarget）属另案；当前以客户端回放门闩对齐可见契约。
