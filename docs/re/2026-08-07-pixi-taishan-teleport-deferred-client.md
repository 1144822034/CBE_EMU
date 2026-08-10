# Pixi 泰山地图石失败而 CBE 成功（同账号）

日期：2026-08-07

## 现象

- Pixi 世界/区域图对泰山子图（如天都峰 `07泰山_02` / exit=67）传送失败（常见「进图超时」或被误报成「落点不符」）。
- 同 mock 服上 CBE 可传送泰山。

## 契约

地图石：`16/4` 确认 → 同包 `16/2+16/3+7/1` 扣石 → **仅 item ack** → 下一帧 scene-poll 投递 deferred `30/1`。

## 根因

deferred `30/1` 存在 **账号级** globals（`capture/restore` 镜像），未绑定发起端 `clientId`。

同账号 CBE + Pixi 同时在线时：

1. Pixi combo 武装 deferred 并写入 account state。
2. CBE 的 scene-poll 先 restore → 消费/清空 deferred，或 `stale_map_transfer_clear` 在 CBE 场景已 ready 时抹掉 deferred。
3. Pixi 后续 poll 拿不到 `30/1` → 进图超时。

CBE 单端传送不受影响，表现为「只有 Pixi / 只有泰山（测的时候常两边开着）不行」。

## 修改

- `g_vm_net_mock_teleport_stone_deferred_enter_client_id` + account 镜像字段。
- arm 时写入 `g_vm_mock_service_active_client_id`。
- deliver 时 owner≠active 则 skip（poll 继续走普通逻辑，不饿死另一端）。
- `stale_map_transfer_clear` / login clear / `inflight_scene_transfer_clear` 仅清自己的 deferred。

## 验证

1. **关掉 CBE**，仅 Pixi 传泰山 → 应成功。
2. CBE + Pixi 同账号同时在线，Pixi 传泰山 → Pixi 应收自己的 `30/1`，日志含 `owner_client=` / `deferred_enter_skip`。
3. `make -j2` 后重启 `jh-online-server`。
