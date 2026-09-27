# 挂机/自动：敌人数量变化后群体技能变普攻

日期：2026-09-27

## 症状

挂机或战内 `[自动]` 选用敌群技能（雷震八方 / 荒魂劫火 / 天火熔身 等）后，遭遇敌数量从 3→1（或 insight 固定 3 与随机 1..3 切换、subtype-10↔5）后，出手变成普攻；蓝够、非故意选普攻。

## 根因

1. **subtype-10 wire 表用 `enemyCount > 1` 门闸**  
   `enemy_wire_to_index` / `enemy_wire_for_index` / `first_alive_enemy_wire` 仅在多怪时走「玩家 wire0、怪 1..N」。单怪时退回 fallback-only，与多怪路径不一致。跨场 3→1 后 auto `LastIndex` / 群攻 collect 对不上存活槽，operate 构建 `no-targets` 失败或落到错误 wire。

2. **挑战开战未像挂机那样重绑 `LastIndex`**  
   `reset_last_operate_target` 后 `LastIndex=0`；subtype-10 下 0 是玩家。auto synth 带坏 index 时群攻 abort，客户端侧表现为下一拍普攻。

3. **武装中清 stale scene wire maps**  
   `apply_player_attack_targets` 在 `SceneMonsterStartActive==0` 时清 `StartUsesSceneWireMaps`，武装中的 subtype-5 多怪场会被翻成非场景表，群攻候选被 self-hit 滤空。

4. **客户端**  
   `livingEnemyWires` 仅信 `candidateWires`；数量/subtype 错位时为空，`battleTargetWire` 可能落到玩家 wire，服端 abort 后挂机/自动再出手像普攻。

## 修改

| 层 | 行为 |
|----|------|
| `mock_server_core.c` | subtype-10 怪 wire 对 `enemyCount >= 1` 一律用 1..N |
| `mock_server_battle.c` | 挑战开战重绑 `LastIndex`；武装中不清 scene maps；群攻无候选时强制 retarget 存活怪 |
| `BattleScreen.ts` | `livingEnemyWires` 座位回退；auto/手动进攻不提交玩家 wire |

## 验证

1. `make -j2` 通过。
2. 挂机选群攻：先打到 `enemies=3`，再打到 `enemies=1`（或开关战斗心得），日志应持续 `mock_battle_operate ... target_mode=4` / `operate=skillId+2`，不应长期 `operate=0`（蓝够时）。
3. 仅 `[自动]` 手动放群攻后跨场：同上；可出现 `mock_challenge_rebind_last_index`。
4. 故意选挂机「普通攻击」：仍稳定普攻。
