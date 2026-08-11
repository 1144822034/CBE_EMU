# Pixi 掉线后商城/背包等拉不到数据

日期：2026-08-11

## 现象

WS 短暂断开或息屏后，地图看起来还能动（或重连成功），但打开商城、背包、装备、人物属性时列表空或无响应。

## 根因

1. `LiveTransport.exchange` 仅在发包前 `ensureConnected()`，重开 **同一 `clientId` 的 WS**。
2. 服端若已 `heartbeat-timeout` → `session_mark_offline` + `session_unbind_account`，会话上无 `onlineRoleId`/账号绑定。
3. 商城 `14/4`、背包 `30/21` 等依赖在线角色会话；仅重连 WS **不会**自动选角，请求常得到空包。
4. 面板打开路径原先无 try/catch，失败时 UI 只显示空列表，用户以为「拉不到数据」。

首个偏离：presence 超时解绑会话；UI 仍当在线去拉面板。

## 修改（客户端）

| 点 | 行为 |
|----|------|
| `prepareLiveExchange` | 面板拉取前：`ensureConnected` +（在图时）一次 scene-poll 刷新 presence |
| `openMall` / `openBag` / `openWornEquipment` / `openCharacterStats` / `refreshLearnedSkillsFromServer` | 走上述准备；失败 chat 提示；商城 goods 仍空时提示重试/重新选角 |

不伪造商城行、不在客户端硬编码余额。

## 验证

1. 进图正常 → 开商城有货。
2. 人为断网几秒再通 → 开商城：应先重连+poll，有货或明确「拉取失败/无数据」提示。
3. 停心跳至服务端踢下线 → 开商城应提示无数据/重选角，不应静默空窗。

## 残留风险

- 已被 unbind 的会话仍须重新选角进图；本修不自动 `selectRole`（避免误绑错角）。
- Gateway idle keepalive 仍是防踢主路径；本修是踢后/半掉线时的面板体验。
