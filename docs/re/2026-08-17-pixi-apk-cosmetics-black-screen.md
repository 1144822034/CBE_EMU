# APK 黑屏：翅膀/称号/坐骑图集 GPU 压力

日期：2026-08-17

## 症状

新加翅膀 / 称号 / 坐骑后，玩家反馈 APK 游戏界面**频繁黑屏**（LCD 空/黑，软键可能仍在）。与先前姿态贴图 churn 白屏同类，但触发面换成化妆品图集。

## 触发与证据

- `zuoqi` 单张 PNG 可达 **2–4.5MB**（解码后 GPU 常为数倍）；`dir` 坐骑转向会按 face/act 加载多张 sheet。
- `plistAtlas.loadPlistAtlas` 原先：
  1. `Assets.load` 后**永不淘汰**；
  2. 每张 sheet 全图 `createImageBitmap` + canvas `getImageData` 做质心对齐（大坐骑一次数 MB～十几 MB CPU）；
  3. 坐骑 part 探测失败时 PNG 可能仍留在 Pixi `Assets` 缓存。
- 城市场景多名玩家各戴不同坐骑/翅膀 → 图集条目无上限堆积 → Android WebView `webglcontextlost` → 黑屏。
- 既有 `recoverDisplayAfterForeground` 只 nudge 地图姿态，**未**刷新化妆品 atlas base。

对照：`docs/re/2026-08-16-pixi-apk-screenoff-white-screen.md`（姿态 rgba churn）；本次是化妆品路径叠加。

## 根因

第一次偏离在客户端资源契约：化妆品 plist 图集无引用计数/LRU，且原生端对大图做全像素采样，超出 WebView GL 预算导致上下文丢失。黑屏是 `webglcontextlost` 后的症状，不是玩法逻辑假死。

## 修改

`PixiJS_web/src/resources/plistAtlas.ts`：

- 原生（Capacitor）跳过翅膀/坐骑全像素采样（称号 `stableFace` 仍采样，sheet 小）。
- 图集 cache：**refcount + LRU**（native soft≈8 / hard≈14）；淘汰时 `destroy` + `Assets.unload`。
- 加载失败卸载 PNG；`nudgePlistAtlasesAfterGpuResume` 供 GL 恢复。

`wingFx` / `mountFx` / `chenghaoFx`：显示时 `retain`，卸下/换 sheet 时 `release`。

`ActorLayer.destroyMarker`：清理 peer 化妆品引用；GPU resume 时 nudge plist。

`SceneView.nudgeAfterGpuResume`：同样 nudge plist。

## 验证

1. APK 装备坐骑（尤其 `dir` 龙）在城镇走动转向；长时间前台不应频繁黑屏。
2. 多名玩家互相可见翅膀/坐骑；离场后图集应可淘汰（控制台偶发 `[plist] skip pixel sample (native)`）。
3. 人为/自然 `webglcontextlost` 后数秒内 `[gl] recover`，翅膀/坐骑/称号应重新可见。
4. PC/浏览器仍可对翅膀做质心对齐（非 native 仍采样）。

## 2026-08-17 按用户反馈回退

用户确认：黑屏修复**之前**附近翅膀/坐骑/动态称号正常；修复后才不显示。协议下发不是本次回归点。

已撤回黑屏修补引入的客户端破坏项：

- `plistAtlas` LRU / retain / destroy / native 跳过像素采样
- 破坏性 `destroyMarker` 清妆容

## 2026-08-17 附近妆容（仅客户端）

浏览器 CDP 取证（`http://localhost:5173/` → live gateway）：

- 控制台有 `[session] cosmetics 31/5 peer=…`（服务端在下发）
- 但 peer marker 上 `eqWing/eqChenghao` 常为 0、`wingVisible=false`
- 根因：`2/6` 短暂移除再 `2/10` 重建 marker 时丢掉已应用的 31/5；notice 不会自动重发

客户端修复（不动服务端协议）：

- `GameSession.scenePeerCosmeticsById` 缓存 31/5；每次 `onPeers` / 附近列表同步后重刷
- `ActorLayer.pendingPeerCosmetics`：31/5 早于 marker 时暂存；peer remove 不再清 pending
- 验证后 dump：`蔡文姬` `eqWing=88019 wingVisible=true`，`荒丶姚神` `chenghao:86037`

## 2026-08-17 少部分附近妆容仍缺失

浏览器对照：多数 `31/5` 能刷上，但仍有 peer 无翅/称号。

根因（服务端队列，非黑屏图集）：

- `cosmeticNotices` 原只有 **8** 槽，同图附近上限 **64**
- 城镇人多时 enqueue 静默失败 → 少部分玩家永远收不到 `31/5`
- 另：`31/5` 须用地图 `actorId`（含 `0x6A…` 合成 id），不能只用 `onlineRoleId`

修复：

- 通知队列 **8→64**；满时覆盖而非丢弃
- 附近可见 / moveinfo 路径 enqueue + poll 冲刷 `31/5`
- broadcast / enqueue 使用观察者侧的 map `actorId`
- 客户端：未收录翅/骑/称号 id 仍尝试加载图集

## 2026-08-18 安全黑屏优化（已再次回退）

浏览器反馈妆容全无后，已将 `plistAtlas` / SceneView·ActorLayer nudge **整段回退到** `3fc36cc`（化妆品可显示的检查点）。

根因之一：失败路径 `Assets.unload` 卸掉坐骑多 face 共享的 PNG，已显示贴图变空白。

黑屏需另开、更窄的 native-only 方案（跳过采样即可，禁止 unload 共享图）。

客户端补充：忽略「尚无缓存时的全 0 `31/5`」，避免 visibility enqueue 早于 `onlineEq*` 同步时冲掉后续真妆容。
