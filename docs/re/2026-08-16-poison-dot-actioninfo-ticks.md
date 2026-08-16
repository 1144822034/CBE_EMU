# 2026-08-16 毒入膏肓后续回合无跳伤可见

## 触发

释放「毒入膏肓」时首跳有伤害飘字与掉血；之后若干玩家回合看不到毒伤，血条也不随毒跳下降（服务端实际已扣 HP）。

## 根因

`docs/re/2026-07-29-skill-full-config-effects.md` 已落地 DoT 状态（`dotRounds` / `dotDamagePerRound`），但 round-age 只在 4/6 **组包之后**静默改 `g_mockBattleEnemyHpSlots`：

1. 未写入 type-1 `actioninfo` 子动作 → Pixi / CBE 只从 `valueA` 播飘字并 `applyWireHpDelta`，客户端 UI 无增量。
2. 跳伤发生在响应发出之后 → 本包结算/死亡动作无法覆盖「毒杀死」；客户端与权威 HP 脱节。

契约本身正确：首跳当次伤害，余 `时效-1` 在后续 round advance 跳伤；缺的是把跳伤编进同一回合的 actioninfo。

## 修复

- ailment 增加 `dotEffectIndex`（施放时写入 skill.dsh 效果号）。
- `enemy_ailments_advance_round_collect`：跳伤同时收集 wire/伤害/死亡位。
- 单人 operate / fallback / 药品消耗回合，以及组队末座 resolve：在反击结算后、组 actioninfo 前 age，并追加一条 type-1（可多 child）+ 毒死 type-3。
- 已在组包路径 age 过的行动用 `ailments_aged_this_action` 跳过尾部二次 age，避免双跳。

## 验证

1. `make -j2` 通过。
2. 毒入膏肓命中后普攻/用药：后续回合应有毒伤飘字与怪血下降（时效 4 → 首跳 + 最多 3 次跳伤）。
3. 毒跳致死：应有死亡动作并进入胜利结算，而非客户端仍显示存活。
4. 组队：ailment 进共享 `battleEnemyAilments`（见 `2026-08-16-team-shared-enemy-ailments.md`）；队友收刀/先出手都应延续队长上的毒。

## 仍标 unresolved

- 毒跳 actor wire 取中毒怪自身（播 skill effect + 飘字）；若原生包用其它 actor/无特效，需对照真实包再收紧。
