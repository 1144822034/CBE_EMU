# PvE 封魔/辅助异常与怪物技能（2026-08-01）

## 问题

天机封魔（神堂静默，`效果=1`）在 PvE 已能挂上 `silenceRounds`，但怪物反击
路径不读该标志，仍可回血/放技能。切磋侧「沉默只能普攻」未对齐到 PvE。

未命中（闪躲）分支注释写可挂沉默，实现为空。

敌方 ailment / 自身 buff 未进账号快照，多人会串状态。

## 修复

1. `resolve_enemy_counter_damage`：该槽 `silenceRounds != 0` 时只结算普攻伤害，
   跳过残血回血与进攻技能（日志 `mock_battle_enemy_silenced`）。
2. 进攻未命中：仍对 `enemy_status_no_damage`（含沉默）调用
   `apply_skill_to_enemy_ailment`（日志 `mock_battle_status_on_miss`）。
3. 账号 `capture`/`restore` 增加 `mockBattleEnemyAilments[3]` 与
   `mockBattleSoloModifier`（恢复时同步 `active_modifier`）。

既有 PvE 路径不变：命中后挂 debuff/DoT/沉默/驱散；回合推进跳毒与沉默倒数。

## 验证

- [x] `make -j2`
- [ ] 封魔命中 BOSS 后：反击无技能特效/无回血，仅普攻
- [ ] 封魔对怪闪躲：仍见沉默生效，后续回合怪不能放技能
- [ ] 沉默结束后怪可再放技能/回血（受每场一次限制）
- [ ] 两人同时战斗：A 封魔不影响 B 的怪
