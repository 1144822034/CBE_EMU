# 世界 Boss：DB/runtime 叠层，不写磁盘 `.sce`

## 触发

世界 Boss 整点刷新 / Admin 强制刷 / claim / 超时刷新后：

- 旧路径会 `inject` / `remove` 战斗怪并 `admin_save_all` 写盘，导致多图孤儿、具名资源上限、以及消失后残留 wire→#18「岩浆石火」名牌。
- 目标：库存 `.sce` 永不因世界 Boss 改写；活跃点只在 MySQL + 进程 runtime。

## 证据 / 根因

- 画怪权威原为 SCE kind=3；仅写 binding 不会画怪。
- Claim 清 binding 后若 SCE 仍留 keep-id / 借 wire，Pixi `resolveCombatEnemyRealId` 会落到库存 #18（岩浆石火 actor 复用）。详见 `2026-09-01-world-boss-despawn-magma-ghostfire.md`。

## 契约

| 层 | 行为 |
| --- | --- |
| `server_world_boss_runtime` | status/scene/x/y/wire_id/hour_key/spawn_unix/… 单行权威 |
| spawn/claim/despawn | 只改 DB + `g_vm_mock_world_boss_rt`；**不** `inject`/`remove` 写盘 |
| WT18 | ACTIVE 同图时内存 splice 一行 kind=3（wire=250）；不落盘；content_version stamp 打破 clientmiss |
| Pixi | `GET /api/world-boss-active` + `[WBOSS:*]` 聊天 overlay；Boss 不再 `forceReload` SCE |
| 一次性清理 | 启用后首次 tick：`legacy-db-overlay` **只**清 MySQL wire≠real binding（`sce_write=0`）；**不**改磁盘 `.sce` |

## 改点

- `server/mysql/schema.sql`：`server_world_boss_runtime`
- `src/server/mock_server_world_boss.c`：load/save runtime、`spawn_db`、stamp、overlay、legacy cleanup
- `src/server/mock_server_core.c`：WT18 load 后 overlay；content version stamp
- `src/web_admin_server.c`：`GET /api/world-boss-active`（无 protocol lock）
- `PixiJS_web/scripts/ws-gateway.mjs`：代理同路径
- `PixiJS_web/src/main.ts`：overlay upsert/drop；进图 API sync
- `PixiJS_web/src/game/ActorLayer.ts`：`clearPermanentHomeDrop` 供再刷

## 验证

- Admin 强制刷 → 该图磁盘 `.sce` 哈希不变；runtime 有点；Pixi 同图见 Boss。
- Claim 后 overlay 消失；再拉 SCE 不出现 `#18 岩浆石火`。
- 整点换图：旧图空、新图一只；无 `scene_combat_spawn_save` 因 Boss。
- CBE：进 Boss 图 WT18 叠包可见；despawn 后重下无残留。
- `make -j2`。

## 回归（2026-09-01）

曾漏网：`clear_orphan_spawns` 仍调 `runtime_remove_*` → `admin_save_all`，despawn/legacy
会改写户外图（例：`27深渊沼泽_04.sce`）。已改为 binding-only purge；`remove_at` 在
dropped==0 时也不再写盘。

## 仍开放

- 历史已污染的户外 `.sce` **不会**被自动回写修复；需从库存 `bin/JHOnlineData` 拷回
  `web/fs/JHOnlineData`，或 Admin 手工编辑战斗点后发布。
