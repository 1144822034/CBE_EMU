# Pixi 战斗已选仙术/物品超时仍普攻

日期：2026-09-14

## 触发与现象

玩家反馈：战斗里打开「仙术」或「物品」，技能/药品已具备使用条件（蓝够、有药），确认或选目标过程中仍打出普攻。

## 根因

`BattleScreen.submitTimeoutAttack()` 在倒计时归零时**无条件**清空 `pendingOperate` / `pendingItem` 并 `onAttack`（Operate=0）。

契约本意（`2026-08-08-pixi-battle-timeout-default-attack.md`）是根菜单停手 → 默认普攻。但下列路径已选定技能/物品后仍被误伤：

1. 仙术/物品确认后进入选目标（`menuMode=target` + pending）
2. 停留在仙术/物品列表（光标已落在可用行上）

首个错误状态：超时回调丢掉已选 Operate/seq，改发普攻；UI 可能刚显示过「施展…」/「使用…」或仍停在选单。

次要缺口（同次一并收紧）：

- 手动仙术确认原先不查 HUD 蓝，服端 `mp_fallback via=operate` 会静默改普攻
- 系统面板战内 `skill_use` 绕过 `onSkill` / 不解除 `[自动]`

## 修改

| 点 | 行为 |
|----|------|
| `submitTimeoutAttack` | 有 pending 仙术/物品 → `commitTargetPick`；仙术列表 → `submitAutoOperate`；物品列表 → `commitItemPick`；否则仍普攻 |
| `ensureSkillMp` | 手动确认/施放前拦「法力不足」，不锁回合 |
| `submitManualSkill` | 系统面板战内施放走同一套清自动 + MP + 出手 |
| `commit*` / `fireSkill` | 提交时再 `clearAutoForManualOp` |

## 验证

1. 多怪：选仙术进入选目标，停手至超时 → 应对当前光标目标施放该仙术，日志非 `超时·普攻`，请求 `Operate=skillId+2`
2. 物品列表停手至超时 → `1/4/3`，非普攻
3. 根菜单停手至超时 → 仍普攻
4. 蓝不足点仙术 → 提示「法力不足」，不发 `4/2`；勿出现「施展…」后 type0 普攻
5. 系统面板战内施放：清 `[自动]` 后出手；蓝不足提示无法施放
