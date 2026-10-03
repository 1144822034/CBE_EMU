# 客户端账号多开（同页切换，APK/PC）

日期: 2026-09-28

## 需求

1. 同页多账号：多个 `GameSession` + `LiveTransport` 并存，只画当前槽。
2. APK / 浏览器 / Electron 同一套（废弃多窗口 `window.open`）。
3. 管理后台「客户端UI」配置多开上限 `multi_open_limit`（1–8）。

## 实现

### 配置 / 下发

同前：`server_client_ui_config.multi_open_limit` → 人物面板 `1/1/21` 字段 `multiOpenLimit`。

### 客户端

| 模块 | 作用 |
|------|------|
| `net/multiOpen.ts` | 槽位凭证键、`allocMultiOpenSlotId`、上限 clamp |
| `main.ts` | `accountSlots` + `gateUi`（非活跃槽屏蔽 UI 回调）+ `switchAccountSlot` / `createAccountSlot` |
| 菜单「账号多开」 | 对话框：切换已有槽 / 新开账号 / 取消 |

- 非活跃槽继续 poll / 挂机；切号时快照/恢复槽内 UI 状态。
- 挂机续环：后台战结束与切回世界均 `ensureHangupLoopArmed`；后台挂机-alone 无 UI 时客户端补 4/2（见 `2026-09-30-multi-open-hangup-rearm.md`）。
- `setResourceExchange` 随活跃槽重绑。
- 「离开游戏」只注销当前槽；有其他槽则切过去。
- 同账号被踢只拆该槽；后台槽被踢仅 tip。

### 按账号隔离（切号 snapshot / 后台缓冲）

| 状态 | 行为 |
|------|------|
| 世界聊天 | `chatEntries` 每槽独立；HUD 切换替换 |
| 战斗入场包 / 4/6 更新 | `pendingBattle` + `pendingBattleUpdates`（后台入队，切回 flush） |
| 切磋/赏金地图血蓝闩 | `sparMapVitalsLatch` / `wagerMapHealLatch` / `mapVitalsAtSparEnter` |
| 拍卖缓存 +「拍」闪 | `auctionCache` + `paiVisible`/`paiBlink`；后台 `onAuctionEvent` 写槽 |
| 赏金「擂」闪 | `leiBlink`；后台 `onWagerEvent` 写槽并入聊天 |
| 环任务 tracker | `cachedRingStatus` |
| 挂机结算累计 | `hangupTally`（`BattleScreen` 切号 reset/restore） |
| 交易摊位 | `tradeUiSnap`：切号不取消协议交易；后台/`accountSwitchInFlight` 期 `onTradeSession` 只 fold 进槽（避免写脏共享 TradePanel）；切回 restore；活跃更新后同步 snap |
| 洗炼/确认框/自动臂 | 切号 `washUi.close` + `clearTransientUiPending`（丢弃/宝石/好友删等 pending） |

仍共享（单 Pixi 壳）：`SceneView`/`BattleScreen` 实例本身；切号时关 overlay 再按目标槽重建展示。

战斗切号补丁（已死怪仍显示存活）：见 `2026-09-28-multi-open-battle-dead-enemy-still-alive.md`
（`applyUpdate` 在 `show` 前入队、`spawnUnit` 按 `hp<=0` 隐藏、后台折 HP 进 `pendingBattle`）。

### 废弃

- 多窗口 / `BroadcastChannel` / Electron 第二窗 / Capacitor「不支持多开」提示。

## 验证

1. 后台多开上限改为 ≥2 → 进游戏 → 账号多开 → 新开账号登录另一号
2. 再开多开列表切回第一号 → 不断线、地图恢复
3. 第二号离开游戏 → 回到第一号
4. APK 与 PC 行为一致
5. 上限=1 时列表无「新开账号」或提示未开放
