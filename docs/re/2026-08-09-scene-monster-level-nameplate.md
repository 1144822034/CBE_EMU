# 场景怪物名牌前缀等级

日期：2026-08-09

## 需求

场景里怪物名字前显示等级（如 `Lv.6 野猪` / `6级野猪`）。

## 约束

- SCE field `0x0F` 显示名不能随意改写：梦境 FB 自定义名牌曾导致 CBE 进图闪退（见 `2026-07-31-admin-scene-combat-spawn-edit.md`）。
- 等级权威在怪物目录（builtin + MySQL `server_monsters` 覆盖），不在 SCE 包内。

## 修改

1. **后台场景预览**（`web_admin_server.c`）  
   红标 / 刷怪点卡片用 `vm_net_mock_monster_stats_for_enemy(realId).level` 前缀 `Lv.N`。绑定表已把预览 ID 换成真实目录 ID，含自定义怪。

2. **PixiJS 地图名牌**  
   - `resources/monsterLevels.ts`：builtin id→等级表 + 运行时 overrides。  
   - `formatMonsterMapName` 生成 `N级名字`；查等级时 **先 `server_monsters` 覆盖，再 builtin**。  
   - `ActorLayer` 怪物名牌截断放宽到 8 字。  
   - 不改 SCE，不影响 CBE。

3. **覆盖下发路径**（公网只开 gateway 时可用）  
   - 管理端：`GET /api/monster-levels`（无需登录）→ `{"overrides":{"id":level,…}}`，只含已加载的 `server_monsters` 行。  
   - Gateway（`:19092`）：同端口 HTTP 代理到本机 admin `:19091` 的该接口（`PIX_ADMIN_HOST` / `PIX_ADMIN_PORT` 可改）。  
   - Pixi 启动进图前：`ws://host:19092` → `http://host:19092/api/monster-levels`；失败则静默回退 builtin。  
   - 调试：`?admin=` / `localStorage.pix_admin` 可覆盖拉取 URL。

## 验证

- `make -j2`；后台打开含刷怪点的场景，红标为 `Lv.N 名字`。  
- 重启 gateway（含 HTTP 代理）；Pixi 进户外图，有覆盖的怪显示 DB 等级，无覆盖仍为 builtin（如野猪 6 级）。  
- 管理端不可达时仍能进图，名牌回退 builtin。

## 风险

- 须同时更新/重建 gateway（`npm run gateway` 或 `gateway:exe`）；旧纯 WS gateway 无 `/api/monster-levels` 时 Pixi 回退 builtin。  
- 梦境 wire `#200` 名牌按线号查等级（幽冥鬼火 Lv.43），开战真实 ID 等级可能不同——与现有「名牌用线号存量名」契约一致。
