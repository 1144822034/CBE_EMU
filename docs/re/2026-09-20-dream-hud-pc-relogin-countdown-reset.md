# 梦境倒计时：PC 重登从头计时

phase: dream-hud-pc-relogin-countdown-reset  
status: fixed  
date: 2026-09-20

## 症状

PC 端梦境 `29*` 内「离开游戏」再登录后，HUD 倒计时从满 TTL（如 `60:00`）重开；安卓端同路径表现正常。服务端仍 `keep_instance` + `dream_hud_remaining_ms` 冻结。

## 根因

第一次偏离在客户端完整重登路径，不在踢人时钟：

1. `leave_game` 只 `setIdentity("")`，**不**调用 `pauseDreamTimer`。
2. `LiveTransport.close()` 先把 `onConnectionLost` 置空再关 socket，登出断线也**不会**触发冻结回调。
3. HUD 只靠 `localStorage` 镜像剩余；空场景分支曾先把 `dreamSceneKey=""` 再 `clearDreamTimer()`，出本/登出语义混乱；异步 `loadDreamHudTtlMs` 竞态可能在 checkpoint 未命中时武装满 TTL。
4. 安卓更常走进程内 WS `onConnectionLost`→`pause`→内存 frozen→`resume`，不依赖「离开游戏」后的 LS 恢复，故表现为正常。

权威剩余在 `account_roles.dream_hud_remaining_ms`；Pixi HUD 仍无 WT 剩余字段。

## 修改

| 位置 | 改动 |
| --- | --- |
| `main.ts` leave_game / onRoleSwitch | 清身份前先 `pauseDreamTimer`；进场景 `setDreamRoleId` |
| `WorldHud` | 空场景只清内存保留 LS；真出本清 checkpoint；role 作用域 key；sync gen 防竞态 |
| `Transport.close` | 关连前仍触发一次 `onConnectionLost` |
| `mock_server_scene_sync.c` 29* enter | 已武装或 DB remain>0 时不再写满 TTL |

## 续：PC 仍 60:00（未发服 / 2026-09-20）

未发 `jh-online-server` 时安卓仍正常、PC 仍满 TTL：安卓多走断线 pause 内存恢复；PC「离开游戏」路径曾 `pause` 后立刻 `setIdentity("")`→`clearDreamTimer` **清掉内存冻结**，只剩 localStorage，Electron 易 miss → 武装满 TTL。

再修：空场景只藏 UI，**保留** `dreamFrozenRemainMs` + `dreamSceneKey`；同进程再进同叶直接 `resume`。仍须发布 **Pixi 客户端**（PC 热更 / exe），与游戏服无关。
