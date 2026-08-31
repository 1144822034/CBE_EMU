# 地图名牌按 real id（2026-08-31）

## 需求

地图怪物名牌（`N级名字`）必须按 **catalog real id** 取等级与名称，不能按 SCE 线号（keep-id `#200`/`#250`）。  
例：朱雀殿青龙王 wire=`250` 曾显示「35级炼狱鬼火」。

## 根因

1. Pixi `formatMonsterMapName(sceName, sceActorId)` 用 **wire** 查等级。  
2. 后台写刷怪点时，wire≠real 且名牌空时会抄 wire 库存名 / 硬编码「炼狱鬼火」。  
3. `#200` 库存名是炼狱鬼火；`#250` 是 FB keep-id。

## 修改

1. **`GET /api/monster-levels`**：下发全量 `overrides`（等级）、`actors`、`names`（UTF-8）、`reals`（`server_scene_combat_spawns` wire→real）。  
2. **Pixi** `resolveCombatEnemyRealId` / `formatCombatSpawnNameplate`：绑定坐标优先 → 同图 wire 唯一 real → keep-id 按 battle `.actor` 或 SCE/catalog 名唯一反查 → 否则 wire；名牌等级/名称一律 real。  
3. **服务端写 SCE**：禁止再用 wire 库存名 / 「炼狱鬼火」填名牌；空名回退「怪」。  
4. **服务端 label 扫描**：keep-id 无绑定时按 SCE 名（task-only）或 `.actor` 落到 real；remap 后允许把 actor 写到 task-only 目录（青龙王 `#63`）。

## 验证

1. 重启 mock；Pixi 拉 `/api/monster-levels` 日志含 `names=` / `reals=`。  
2. 进 `23蟠龙寨_09`：青龙王应显示约 `60级青龙王`（或后台覆盖等级），不是炼狱鬼火。  
3. 梦境 wire≠real 刷怪：名牌等级与后台 real 怪一致。  
4. `npx tsx PixiJS_web/scripts/probe-nameplate-real.ts`：`byActor`/`byName`/`byBind` 均 `realId=63`、名含青龙王。

## 风险

- 无绑定且 actor/名均匹配不到（或名歧义）时，仍回退 wire（旧行为）。  
- 须同时更新 gateway 代理的 admin 构建。
