# Pixi 私聊（本频道）

## 需求

1. 聊天页签「聊」改为「本」（本地 type4 + 私聊 type7 展示）。
2. 玩家私聊：好友、世界/频道消息点击、周围玩家（含地图点头像菜单）均可发起。
3. 上行走原生 `1/3/2 { sendTo, data }`；服务端既有私聊投递不变。

## 契约

| 路径 | 行为 |
|------|------|
| 发送 | `1/3/2`；在线空 ACK + 本机 type7 回显；离线响应 type5「对方不在线」 |
| 接收 | 轮询 `1/3/3 type=7`；`chatinfo` 带 `senderRoleId` |
| `sendTo` | 持久角色 id（好友）/ 附近 actor id（周围）/ 消息行 senderRoleId（世界等） |

证据：`docs/re/2026-07-16-chat-message-system.md`、`docs/re/2026-07-11-nearby-player-actions.md`（CBE「发起私聊」为本地切 UI）。

## 修改

- `protocolExtras`: `buildPrivateChatRequest`；`ChatLine` 保留 `senderRoleId`/`senderName`
- `GameSession.sendChat`/`sendPrivateChat`
- `ChatPanel`: 页签「本」、`pmTarget`、消息行点击开私聊
- `SocialPanel`: 「发起私聊」；地图点头像 → 同一操作菜单
- `WorldHud`/`main`: 聊天条目携带发送者 id

服务端：`mock_server_social.c` 私聊路径已实现，本轮无改。

## 验证

1. 页签显示「本」；无目标时发送仍走本地 type4。
2. 好友 / 周围 / 头像菜单 →「发起私聊」→ 输入条 `[私]`，发送 `1/3/2`。
3. 世界消息点玩家行 → 切到「本」并对该 `senderRoleId` 私聊。
4. 对方在线可见 `[私]`；离线 toast + `[系]对方不在线`。
