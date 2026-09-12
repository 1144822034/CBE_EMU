# 世界BOSS：后台关闭仍可战 / 挂机踩到 / 手动形态错

日期：2026-09-12

## 现象

1. 管理后台「启用=关闭」后，剑门关仍可进世界 BOSS 战并被秒杀。
2. 挂机/踩怪体感像普通小怪，实际打的是 BOSS 数值。
3. 手动遇 BOSS 时战斗座位仍画户外垃圾怪（如恶臭胶质）外观。

## 证据

- 启动日志：`world_boss_runtime_load status=3 … hour=2026090209`（旧 CLAIMED 残留）。
- `mock_scene_combat_spawn_remap … wire=41 real=213 source=world-boss-runtime`（近距 96px 把 #41 remap 成 #213）。
- `mock_challenge_battle_start … requested=213 wire=41 subtype=5`；客户端拉 `e_mucusY`。
- 配置 `enabled` 只挡 tick/spawn，不挡 remap/claim，也不清 runtime。

## 根因

1. **关闭未清状态**：`enabled=0` 时 tick 直接 return；`remap`/`claim` 不检查 enabled；DB 里 ACTIVE/CLAIMED 继续劫持遇怪。
2. **近距 remap 过宽**：同图 + 96px 内任意 wire（含 #41）都可变成 Boss。
3. **挂机共用 remap**：Pixi 挂机最近点含世界 Boss overlay；服务端又可能 remap。
4. **形态**：subtype-5 仍用 outdoor wire 的 live-node 外观，而数值已是 Boss catalog id。

## 修改

| 点 | 行为 |
|----|------|
| `load_config` / tick / `admin_save` | `enabled=0` 时 `despawn_active` 清 runtime |
| `remap` / `claim` | 必须 `enabled`；且 wire 必须是 Boss 身份（monsterId / runtime wire / #250），禁止仅靠近距劫持其它 catalog wire |
| 挂机 `4/1` | 跳过世界 Boss remap / overlay id；保留 automonster 小怪 |
| Pixi 挂机最近点 | 只排除 overlay `0x70ffffff` / 名含「世界BOSS」（勿用 `≥0x70000000`，SCE 刷怪也是该段） |
| 手动 Boss 战 | claim/remap 到 Boss 后强制 subtype-10 + catalog 模板外观 |
| Pixi `resolvePveEnemyActor` | pending 与战斗 enemyId 不一致时改用 catalog actor |

## 验证

- [ ] 后台关闭世界 BOSS → 重启或等 tick：无 `[WBOSS:spawn]`，踩剑阁普通怪不再 `real=213`
- [ ] 开启并刷 Boss → 挂机只打户外小怪，日志可有 `mock_hangup_skip_world_boss`
- [ ] 手动点 Boss → 战斗座位为配置怪形态（非附近胶质/蝙蝠）
- [ ] `make -j2`

## 续：点开始挂机不进战斗

### 根因

挂机最近点误用 `actorId >= 0x70000000` 过滤；户外 SCE kind=3 也是 `0x70000000+i`，Boss overlay 才是 `0x70ffffff`。池被滤空 → `no-scene-monster` → 不开 4/1。

### 修改

Pixi / 服务端 hangup skip 均改为只认 `0x70ffffff`（及 Boss 名牌）。

### 验证

- [ ] 有户外刷怪图点挂机：应进入战斗
- [ ] 同图有世界 Boss overlay 时挂机仍打普通怪
