# 世界BOSS：刷新后多图同时有 Boss

日期：2026-08-31

## 现象

新一轮世界 BOSS 刷新后，上一只仍出现在旧地图；多张户外图可同时看到 Boss。客户端侧表现为「旧 Boss 又刷回来」。

## 预期

全服同一时间只应有一只世界 BOSS：换点/整点刷新时旧 SCE kind=3 行必须删掉，且旧图客户端不再画出该怪。

## 根因

`vm_mock_world_boss_tick` 整点前预告把运行时写成：

1. `status = PREVIEW`
2. `scene = 预告随机图`（覆盖上一只 ACTIVE 的真实刷怪图）

随后整点 `spawn_now` → `despawn_active`：

- 仅当 `status == ACTIVE` 才 `runtime_remove_combat_spawn_at`
- 此时已是 `PREVIEW`，且 `scene` 已不是旧图 → **旧图 SCE 注入点成为孤儿**
- 再向新图 inject → 多图残留

Admin「发送预告」同样覆盖 `scene`/`status`，路径一致。

首次偏离：预告改写 live spawn 锚点，而不是「仅广播文案」。

## 修改

- 预告（整点 / Admin）只写 `previewScene` + 广播；**不**在 ACTIVE/CLAIMED 时改 `status`/`scene`/`x`/`y`
- 每次 despawn/respawn：清 remembered inject 列表，并扫描 `server_scene_combat_spawns` 中 `real==配置怪且 wire≠real` 的 remap 行，逐点删 SCE + 广播「世界Boss 已刷新」
- inject 成功后 `orphan_remember`；claim 删点后 `orphan_forget`
- Pixi：`removeSceneMonsterPermanent` 记录 home，同图 `setSceneMonsters`/forceReload 不再把已 despawn 坐标画回

## 验证

- [ ] 有活跃 Boss 时发预告：Admin 运行时 scene/坐标仍是旧 Boss；旧图 SCE 仍在；预告聊天正常
- [ ] 随后「立即刷BOSS」或等到整点：旧图 Boss 消失，仅新图有一只；日志含 `world_boss_orphan_clear`
- [ ] 对已污染环境：Admin「清除BOSS」或再强制刷新一次，应扫掉绑定表里的 remap 残留
- [ ] `make -j2`
