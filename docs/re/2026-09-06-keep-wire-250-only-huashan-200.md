# keep-wire 仅保留 #250（华山洞窟 #200 名牌误匹配）

日期：2026-09-06

## 症状

`25华山洞窟_01` 后台 / SCE / automonster 均为 `#200 炼狱鬼火`，地图刷新名牌却显示 `56级蓝灯鬼`（`#51`）。

## 根因

1. `#200` 与 `#51` 共用战斗 Actor `e_ghostfireB.actor`。
2. Pixi `KEEP_WIRE_IDS` 与服务端 `label_enemy_for_wire` 把 **`#200` 也当成 keep-wire**（历史梦境占位约定）。
3. 无 `server_scene_combat_spawns` 绑定时，按 Actor 反查会 **跳过 #200 自身**，唯一命中 `#51`。

权威数据：`web/fs` SCE 三点均为 `wire=200` +「炼狱鬼火」；`automonster.dsh` 槽为 200。偏离点在名牌 real-id 解析，不是后台保存失败。

## 修复

- keep-wire **只保留 `#250`**（与 `VM_NET_MOCK_SCE_COMBAT_MAP_WIRE_FB` / `pick_map_wire_id` 一致）。
- `#200` 恢复为库存怪：无绑定时名牌/标签直接用 wire=200。
- 旧梦境 `wire=200 → real=自定义` 仍靠绑定表；无绑定的脏 SCE 需重存为 `#250`。

改动：

- `PixiJS_web/src/resources/monsterLevels.ts`
- `src/server/mock_server_scene_task.c`（`label_enemy_for_wire`）
- 相关注释（battle / social / web_admin）
- `PixiJS_web/scripts/probe-nameplate-real.ts` 增加华山回归

## 验证

1. `npx tsx PixiJS_web/scripts/probe-nameplate-real.ts`：华山 `realId=200`，名含炼狱鬼火；青龙王 `#250` 路径仍 OK。
2. `make -j2`
3. 重启 mock + 刷新 Pixi：进华山洞窟名牌应为约 `35级炼狱鬼火`（以后台等级为准），不是蓝灯鬼。
4. 梦境 / 世界 Boss（wire=`#250` + binding）名牌与开战仍按 real。
