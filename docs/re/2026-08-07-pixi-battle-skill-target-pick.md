# Pixi 战斗单体选目标 + 群体特效多播

## 现象

1. 单体攻击/仙术无法指定对象：伤害/减益应对指定敌人，恢复/增益应对指定队友或自己。
2. 群体技能（`目标指向=4` 敌群 / `2` 友群）`actioninfo` 已有多 child，但 Pixi 只在 `children[0]` 播放 eidolon 特效。

## 契约

`skill.dsh` 列 10「目标指向」：

| 值 | 语义 | 客户端选目标 |
|----|------|--------------|
| 0 | 己 | 自动 self wire |
| 1 | 单友 | 上下键挑存活队友（含自己） |
| 2 | 友群 | 不点选；请求用首个存活友方 wire |
| 3 | 单敌 | 上下键挑存活敌人 |
| 4 | 敌群 | 不点选；请求用首个存活敌 wire |

证据：`docs/re/2026-07-29-skill-full-config-effects.md`、`2026-07-22-battle-group-skill-targeting.md`（群体结算每目标一 child）。

## 修改

- `resources/skill.ts`：解析 col 10 → `targetKind`；`skillNeedsTargetPick(1|3)`。
- `BattleScreen`：`menuMode=target` 挑 wire → `targetIndex()` → 既有 `4/2`；群体 FX 对全部 `rec.children` `playEffect`。
- 右软键在仙术/选目标层作「返回」，根菜单仍为「逃跑」。

## 验证

- 多敌：攻击/单体敌技可切换光标后确认，operate 目标 wire 与所选一致。
- 组队：清风类 `目标指向=1` 可选队友。
- 雷震八方等敌群：每个存活敌脚下均有技能特效。
- `npx tsc --noEmit`（PixiJS_web）。
