# 2026-08-10：遇怪战斗显示种类与属性不一致

phase: pixi-battle-encounter-visual  
status: fixed

## 现象

地图上有多种 SCE 遇怪时，开战常出现：左侧多只怪**外观是不同种类**，但 **HP/攻防同一套**。

## 根因

服务端多怪战是「同一模板克隆 N 份」：`requestedEnemyId` 一份属性写入全部 left 槽。

Pixi 在 subtype-5 `parseBattleInfoScene` 里却把 left 座位写成 `id = index + i`。当地图 wire/catalog id 连续（如 105/106/107）时，每个座位按各自 id 去 `sceneMonsters` 查 `battleActorResource`，就会画出**不同种类**，而 vitals 仍来自同一次遭遇。

次要因素：`onBattle` 对每个 left 座位独立 `resolvePveEnemyActor(u.id)`，未绑定碰撞到的那只 spawn；`.find` 按 id 命中地图上「第一种」同 id 怪。

## 契约（修正后）

```text
碰撞/最近点 SCE spawn
  → 4/1 id=wire, pos=spawn(x,y)
  → session.pendingEncounterVisual = 该 spawn 的 battleActorResource
服务端
  → remap → 一份 stats，monsterCount 克隆
客户端
  → left[].id 均为遭遇 actorId（不再 index+i）
  → 全部 PvE left 共用同一次 resolve 的 .actor / 名
```

## 修改点

| 文件 | 改动 |
|------|------|
| `PixiJS_web/src/net/protocol.ts` | subtype-5 left `id` 固定为遭遇 `actorId` |
| `PixiJS_web/src/game/GameSession.ts` | `pendingEncounterVisual`；`resolvePveEnemyActor` 优先碰撞点/坐标 |
| `PixiJS_web/src/main.ts` | `onBattle` 一次解析，全体 PvE left 同外观 |

## 验证

- 多种类同图：撞 A 开战 → 场上只显示 A（多只时也是 A 的克隆）
- 撞 B 开战 → 只显示 B
- 挂机 subtype-10：无 pending 时按 wire leftId 解析一次，不再按座位漂移
- 切磋：`visual0∈1..2` 仍走玩家职业立绘，不受 PvE 路径影响
