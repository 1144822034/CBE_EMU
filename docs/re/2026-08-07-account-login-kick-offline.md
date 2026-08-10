# 同账号登录顶号踢下线

日期：2026-08-07

## 需求

同一账号新登录后，先前仍在线的会话（CBE / Pixi）应被直接踢下线，避免同账号多端共享 `account_state`（传送 deferred 抢包等）。

## 既有行为

`vm_mock_service_bind_session_account` → `session_take_over_account` 已对同账号其它会话做 `mark_offline` + `unbind`，但：

1. 仅当旧会话 `roleOnline|presence|sceneVisible` 时才踢（标题页已绑账号未进图会漏）。
2. 旧端没有提示包，只是下次请求 `drop-no-account`，客户端仍像在线。

## 修改

1. **takeover 范围**：凡同 `accountId` 且 `clientId` 不同即踢（含仅绑账号未进图）。
2. **`accountKickedPending`**：踢下时置位；旧端下次 scene-poll / 无账号 data 请求返回 `25/12+1/3/3`「账号已在其他地方登录，你被强制下线」并 `CLOSE_AFTER_DATA`。
3. **Pixi**：修好 `1/3/3` 解析（跳过 `senderRoleId`）；识别踢下提示后停 poll、关 transport、回标题页。

## 验证

1. `make -j2` 后重启 `jh-online-server`。
2. CBE 登录进图，再 Pixi 同账号登录 → CBE 应收到系统提示/断线；服务日志 `session_account_takeover ... action=kick-offline` 与 `account_kick_deliver`。
3. 反过来 Pixi 先在线、CBE 再登 → Pixi 回标题并显示踢下提示。
