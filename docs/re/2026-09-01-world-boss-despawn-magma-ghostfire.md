# 世界BOSS 消失后原地变成 #18 岩浆石火

日期：2026-09-01

## 现象

世界 BOSS 消失（claim / 整点刷新 / 强制清除）后，原坐标会出现「#18 · 岩浆石火」（或同名牌）。精灵仍是鬼火，像「Boss 没了又刷了一只岩浆石火」。

## 预期

Boss 点应从 SCE kind=3 去掉；同图客户端不再画出该坐标战斗怪。库存 `#18` 名牌不得被借用来顶替。

## 根因

1. 世界 BOSS（自定义目录怪）户外注入时，地图线号为 keep-id `#250`（历史实现曾借用同 `.actor` 的库存 `#18`）。名牌「世界BOSS」依赖 `server_scene_combat_spawns` 的 wire→real，以及 SCE `displayName`。
2. Boss 与 `#18 岩浆石火` 共用 `e_ghostfiresR`（或同类鬼火）地图 Actor，肉眼无法区分物种，只能靠名牌。
3. Despawn / claim 会改写绑定表。若 SCE 行仍短暂残留（发布竞态、旧 wire `#18` 污染、或仅清了 binding），客户端重拉 SCE 后：
   - Pixi `resolveCombatEnemyRealId` **先按 `.actor` 再按名**（与服务端 `label_enemy_for_wire` 相反）；
   - keep-id `#250` + 鬼火 actor → 命中目录第一只同 actor 怪 → **`#18 岩浆石火`**；
   - 或 wire 已是 `#18` 且 binding 已无：`formatMonsterMapName` 用目录名覆盖 SCE 上的「世界BOSS」。
4. 首次偏离：权威数据上 Boss 身份（binding / 名牌归属）先于/脱离 SCE 行删除；客户端把同一格解释成库存鬼火。

## 修改

| 点 | 行为 |
|----|------|
| Pixi `resolveCombatEnemyRealId` | 无 binding 时与服务端一致：**先 SCE 名、再唯一 `.actor`**；多 actor 命中则退回 wire |
| Pixi `refreshSceneCombatSpawns` | 世界 Boss 通知时先 `loadMonsterLevelOverrides` 刷新 `reals` |
| 服务端 `runtime_remove_combat_spawns_matching_boss` | despawn/claim 后按 Boss 目录名 / real id 清扫同图残留 kind=3 |

## 验证

- [ ] 刷 Boss（自定义鬼火外观）→ 开战 claim 或 Admin 清除：原地不再出现「岩浆石火」
- [ ] 整点换图刷新：旧坐标空，新图仅一只 Boss
- [ ] 普通 `#18` 野怪战后约 10s 仍回出生点
- [ ] `make -j2`

## 已污染环境

若某图仍残留 wire `#18` + 名牌「世界BOSS」：Admin 打开该图删点重存，或再执行一次世界 Boss「清除/强制刷新」触发 plate residual 清扫。
