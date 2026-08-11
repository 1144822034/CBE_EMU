# 梦境挑战怪物形象变成 NPC / 挑战按挂机遇怪

日期：2026-08-11

## 触发

梦境副本点向导「挑战」进战后，左侧怪物立绘是 **NPC（如 FB_lotus）**，不是
目录配置的怪（204=`e_fireG.actor`）。

## 契约 / 实际 / 首次偏离

| 预期 | 实际（修前） |
|------|----------------|
| 按 `challenge_enemy_id`（一层=204）加载怪物 `.actor` | 加载挑战向导 NPC 的 `FB_lotus*.actor` |
| Pixi 挑战开战与挂机同路径（客户端 `4/1`） | 走 `30/10` → 服端 subtype-10 `4/10`，无碰怪种子 |

首次偏离：Pixi `confirmChallenge` / `resolvePveEnemyActor`。

## 证据

- `_01` 向导 `32903.challenge_enemy_id=204`，`server_monsters.204.actor_resource=e_fireG.actor`。
- 旧路径：挑战开战为 subtype-10（`4/10`），无踩怪 `pendingEncounterVisual`。
- SCE 线号为 `#200`，`sceneMonsters` 的 `battleActorId` 对不上 catalog `204`。
- 旧回退：`pendingChallengeGuideActorId` → 向导 NPC → **FB_lotus**。
- 挂机：`startBattleNearPlayer` → `4/1` → 正常 subtype-5。

## 根因

1. 挑战 PvE 用向导 actor 做立绘回退；梦境向导是场景 NPC（`FB_*`）。
2. Pixi 确认挑战发 `30/10 agree`，吃服端 subtype-10，未把 `challenge_enemy_id` 赋进挂机同款 `4/1`。

## 修正

1. `30/9` 增加 `enemy`（=`challenge_enemy_id`）；Pixi 记入 `pendingChallengeEnemyId`。
2. `confirmChallenge(agree)`：取消发 `30/10 agree=0` 清 pending；同意则先清 pending，再
   `startBattleNearPlayer(x,y, pendingChallengeEnemyId)`（与挂机相同，只多赋挑战怪 id）。
3. Mock：`30/10 agree=0` 只清 pending，不 arm `4/10` followup（CBE 同意仍走原 followup）。
4. `resolvePveEnemyActor` / `/api/monster-levels` `actors`：目录 `.actor` 优先于向导 `FB_*`。

## 验证

1. 停服 `make -j2`，重启 mock + gateway；Pixi 硬刷新。
2. 梦境一层挑战：客户端发 `4/1 id=204`（或等价），进战 subtype-5；左侧 `e_fireG`，非莲花 NPC。
3. 取消挑战：仅 `30/10 agree=0`，无开战。
4. 野外小猴子挑战：仍可进战；挂机路径不变。
5. CBE 模拟器确认挑战：仍可走 `30/10` → followup `4/10`。

## 2026-08-11 续：属性/立绘变成地图怪

### 首次偏离

`armEncounterVisualLikeCollide(preferNpcId=challenge_enemy)` 在找不到同 id 场景刷怪时：

1. 借最近 SCE 刷怪的 **坐标** → 服端 `sce_combat_spawn_remap` **按 pos 绑定** 盖成地板怪 real id（HP/掉落错）。
2. 借最近刷怪的 **`.actor`** → `pendingEncounterVisual` 立绘错。

### 修正

- Pixi：显式挑战 id 用目录 `actor_resource`；无同 id 刷怪时坐标用 `1,1`，不借地图刷怪。
- Mock remap：pos 命中仅当请求 id 为 0 / 该点 wire / 该点 real；已知目录 id 在 split 场景 `catalog-keep`。
