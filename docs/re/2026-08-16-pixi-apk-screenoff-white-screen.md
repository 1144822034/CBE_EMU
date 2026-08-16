# Pixi APK 长时间在线白屏（含不息屏）

日期：2026-08-16

## 现象

APK 挂机或前台久挂后 LCD **白/空**，软键仍在；**不息屏也会出现**。杀进程重进可恢复；gateway 保活下账号往往仍在线。

## 根因

不单是息屏丢上下文。首个偏离在 **GPU 贴图 churn**：

1. 地图角色 / 玩家每动画帧 `rgbaToTexture` 再 `destroy(true)`，Android WebView 上易耗尽 GL → `webglcontextlost`（亮屏挂机同样会发生）。
2. UC3 / crisp 文字缓存无上限，聊天与名牌久跑膨胀。
3. 原先只在 `visibility`/`focus` 做显示恢复；**前台不切页时丢 GL 不会自动恢复**。

## 修改

| 点 | 行为 |
|----|------|
| `poseTextureCache` | 地图 NPC/怪/玩家姿态共享缓存 + 上限淘汰 |
| `BitmapLabel` | UC3/crisp 缓存 soft/hard 上限，保留 live label key |
| `recoverDisplayAfterForeground` | `contextlost` 延迟恢复、ticker 每 2s 检测 `isContextLost`、debounce |
| 亮屏路径 | 仍走 visibility / focus / contextrestored |

## 验证

1. 亮屏挂机 20–40 分钟：地图不应长期白屏；偶发丢失应在数秒内 `[gl] recover` 回来。
2. 息屏久挂再亮屏：同前。
3. 控制台可出现 `webglcontextlost` / `recover display ticker-lost`（有则说明恢复路径在工作）。

## 残留

- 进程强杀 / WebView 整页重载仍需重启客户端。
- 极端 ROM 永不 `contextrestored` 且 `restoreContext` 失败时，仍可能需手动杀进程（未做整页 reload 兜底）。

## 后续修补（同日）

`alphaMode` / `StencilMaskPipe` 崩溃：淘汰仍保留，但 SceneView 与 ActorLayer 原先只保护各自 in-use key，会互删对方仍挂在 stencil mask 下的姿态贴图。改为全局 `livePoseKeys` 引用计数；传送门帧也改走同一缓存，去掉每 tick `rgbaToTexture`+`destroy(true)`。
