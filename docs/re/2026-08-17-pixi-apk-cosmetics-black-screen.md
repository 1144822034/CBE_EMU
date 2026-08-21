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

## 2026-08-18 试跑 Pixi 官方 GC / unload（保留妆容）

落地（对照官方 [Garbage Collection](https://pixijs.download/dev/docs/core-concepts-6.html)；本仓 Pixi **8.9** 实际以 `GCSystem` 为准，`textureGC*` 为 shim）：

| API | 用法 |
|-----|------|
| `destroy({ texture: false })` | 卸下翅/骑 sprite 时**不**销毁共享图集 |
| `texture.source.unload()` | PNG **refcount=0** 且延迟 2.5s 后卸 GPU；**保留** Assets / 帧 Texture |
| `Assets.unload` | **仅** `Assets.load` 自身失败；成功路径禁止（坐骑多 face 共享 PNG） |
| `gcActive` / `gcMaxUnusedTime` / `gcFrequency` | native 20s / 10s；浏览器默认级 60s / 30s |
| `textureGCActive` / `textureGCMaxIdle` | 同步写入（8.9 shim → `gc.maxUnusedTime`） |
| `autoGarbageCollect` | 化妆品 ImageSource=`true`；姿态 BufferImageSource=`false`（本仓 cache 管） |

另：native 翅膀/坐骑跳过全图像素质心采样；`nudgePlistAtlasesAfterGpuResume` 在 GL recover 时 `source.update`。

验收：浏览器附近妆容不空白；APK 控制台可见 `[gl] TextureGC/GC native`、换装后空闲可见 `[plist] source.unload (idle)`；黑屏频率应下降。

## 2026-08-18 玩家反馈：无本机翅/骑也会黑屏

「自己没装」≠「场景无图集」（附近 peer 仍加载）。非妆容路径（姿态/UC3/战斗 FX）与「丢 GL 回不来」仍可能黑屏。取证快照含 `peersWithCos` / `plist`；连续 3 次 recover 仍 lost → 软重载。

## 2026-08-18 截图：LCD 全黑、HTML 软键仍在

玩家截图：画布区纯黑（Pixi clear `#05070a`），底栏「确定 / 方向 / 右软」与右上角 HTML 白点开关仍在。会话未崩。

第一次偏离在 **恢复契约**，不是玩法假死：

1. `webglcontextlost` 后 **50ms** 就 `recover`：此时上下文多半仍 lost；`restoreContext()` / `isContextLost()` 可能报「已恢复」，但 framebuffer 仍是清屏色。ticker 只看 `isContextLost()`，不再 nudge，于是 **假恢复 + 一直黑**。
2. 息屏保活仍跑 ticker 时，Pixi `GCSystem`（native 20s）会把「未 touch」的地图/HUD ImageSource `unload`；亮屏后若 nudge 不全，同样整屏空。
3. 地图 `Texture.from(canvas)` 默认可被 TextureGC 回收。

修改（仍禁止对在用妆容 PNG `Assets.unload`）：

- 丢上下文后等到 `contextrestored` 再 recover；8s 无 restored → 软重载（每页最多 2 次）。
- recover 后 `renderer.resize` + 对 LCD 顶条 `extract.pixels`：仍接近清屏色则算失败（不单信 `isContextLost`）。
- `document.hidden` 时关掉 `renderer.gc.enabled`；地图贴图 `autoGarbageCollect = false`。

验证：城镇久挂 / 息屏再亮，偶发 `[gl] webglcontextlost` 后应 `[gl] webglcontextrestored` 并出图；若持续 `[gl] recover still blank` 应在约 8s 内软重载回登录，而不是卡死黑屏。

## 2026-08-19 黑屏仍出现

上一轮只修了「假恢复」探测，黑屏仍会发生。第一次偏离在 **GPU 生命周期 + 丢失后不主动 restore**：

1. 附近玩家离开时 `destroyMarker` 直接 `root.destroy`，**没有** `wing/mount/chenghao.clear()` → plist refcount 永不归零 → 城镇见过的翅/骑 PNG 一直占 GPU → `webglcontextlost`。
2. Pixi 8 只对 `forceContextLoss` 调 `restoreContext()`；自然 OOM 后 WebView 经常 **永不** `contextrestored`。上一轮还去掉了 lost 后 50ms recover，等于干等。
3. Pixi ticker 在 lost 后可能停转，2s 检测失效。
4. Native `resolution=3` + `gcMaxUnusedTime=20s` 加重预算与误卸。

修改：

- `destroyMarker` 先 `clear()` 妆容；APK 只给最近 6 名附近玩家上传翅/骑（称号仍全员）。
- 显示中的 plist `autoGarbageCollect=false`；native 闲置卸 GPU 延迟 30s。
- `powerPreference=low-power`、TextureGC 回到 60s。
- `webglcontextlost` 立即 `WEBGL_lose_context.restoreContext()`；`setInterval` 不依赖 ticker；reload 上限 6。

## 2026-08-19 LCD 分辨率与字体

`resolution=1` 会把整块 240×400 画布（字 + 地图）一起拉伸，UC3 变糊。Framebuffer 相对妆容 PNG 很小，不是黑屏主因。

已恢复 native LCD `resolution` 跟 CSS 缩放（上限 3），字体/HUD 与浏览器一致。画面侧 GPU 仍靠：离开玩家 `clear()` 妆容、附近翅/骑最多 6 人、plist 闲置 unload。

验证：城镇地名条 / 菜单字应接近 HTML 软键清晰度；黑屏路径仍走上一节。

## 2026-08-19 同场景 / 同战斗复用

第一次偏离：同图也会 `invalidateMap` + 重解 SCE/MAP/GIF；挂机连战 `show`/`hide` 拆掉战斗底图和姿态贴图。

- `loadSceneMap` 保留最近约 8 张已合成场景；再进同一 `.sce` 不再解包拼图。
- `onEnterScene` 不再无条件拆地图；同图只改坐标，不重置野外怪/装饰/传送门。
- 战斗：同 `b_*.sce` 不重绘底图；进出战斗保留姿态贴图缓存；`resolveBattleSce` 结果记住。

验证：同图走动/战后回图不应再卡一下重绘；挂机连续遇敌战斗底图不闪、角色贴图不重新上传。
