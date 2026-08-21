# APK 化妆品黑屏：方案深入分析（对照 PixiJS / WebGL 业界实践）

日期：2026-08-18

相关：

- 本仓症状：`docs/re/2026-08-17-pixi-apk-cosmetics-black-screen.md`
- 姿态 churn 先例：`docs/re/2026-08-16-pixi-apk-screenoff-white-screen.md`
- 运行时：PixiJS **8.9.x** + Capacitor Android WebView

## 1. 现象与根因分层

| 层 | 内容 |
|----|------|
| 症状 | LCD 黑/空，软键可能仍在；会话往往仍在线 |
| 直接原因 | Android WebView `webglcontextlost` → GPU 资源句柄失效 |
| 诱因（本仓） | **不止**化妆品：姿态/UC3/地图/战斗 FX churn + 化妆品大图尖峰；「自己没翅/骑」仍可能因附近 peer 妆容或非妆容路径丢 GL |
| 非根因 | 玩法逻辑假死、单纯「软键无响应」、本机是否装备翅/骑（仅相关，非充分必要） |

黑屏是 **GL 上下文丢失后的显示症状**。业界共识（Three/Pixi/SO）：WebView 上 WebGL 内存预算远小于桌面；大纹理 + 泄漏/ churn 会触发 context loss；恢复比预防难得多。

## 2. 外部方案检索摘要

### 2.1 Context loss 处理（Pixi / WebGL 通用）

- 必须对 `webglcontextlost` 调 `preventDefault()`，否则浏览器可能永不 `contextrestored`。
- 恢复时需重建 GPU 资源（纹理/缓冲/着色器）。Pixi 会尝试内部恢复，但 **复杂场景常不完全**（Graphics 失效、多次丢失后无法渲染等，见 Pixi discussions #11196、issue #7232）。
- 业界兜底常见两档：
  1. **软恢复**：`source.update()` / 重建 sprite / 重绑贴图（本仓 `recoverDisplayAfterForeground`）。
  2. **硬恢复**：整页 `location.reload()` 或销毁并重建 `Application`（SO / 多款商业 Pixi 应用在连续丢失后采用）。

参考：

- [SO: handle CONTEXT_LOST in PixiJS](https://stackoverflow.com/questions/61020416/how-to-handle-webgl-context-lost-webgl-errors-more-gracefully-in-pixijs)
- [Matt DesLauriers: Non-Intrusive WebGL — Context Loss](https://medium.com/@mattdesl/non-intrusive-webgl-cebd176c281d)
- [Pixi GlContextSystem.forceContextLoss](https://pixijs.download/v8.18.1/docs/rendering.GlContextSystem.html)

### 2.2 PixiJS 8 显存管理（官方）

官方 [Garbage Collection](https://pixijs.download/dev/docs/core-concepts-6.html)：

1. `destroy({ texture: true, textureSource: true })` — 确认不再复用时。
2. `texture.source.unload()` — 卸 GPU，保留 JS 对象（可再上传）。
3. `Assets.unload(url)` — 卸缓存 + GPU。
4. **TextureGCSystem**（默认开）：约 3600 帧空闲后回收未用纹理；可配 `textureGCMaxIdle` / `textureGCCheckCountMax`。

注意：Pixi 8 仍有 GC / unload 时序 bug 报告（如 #12022：unload 前 null 导致 GPU 纹理删不掉）。**手动 LRU + Assets.unload 共享图** 在本仓已实证会弄丢附近妆容。

### 2.3 对本仓的关键约束

| 做法 | 评价 |
|------|------|
| 对仍在显示的共享 wing/mount PNG `Assets.unload` / `destroy` | **禁止**（已回归：浏览器妆容全空白） |
| 坐骑多 face 探测失败就 unload 同 PNG | **禁止**（多 prefix 共享一张图） |
| 原生跳过翅膀/坐骑全像素 `getImageData` | **安全、高收益**（不碰共享图集生命周期） |
| 仅对 **refcount=0** 的 sheet 延迟 unload | 理论可行，实现成本高，易与 Pixi GC 打架 |
| 依赖 TextureGC 自动收未用图 | 可作辅助；显示中的 sheet 不应被标 idle |
| contextlost 后仅 nudge | 已有；化妆品 atlas 需纳入 nudge（不 unload） |
| 连续丢失 N 次后整页 reload | 合理兜底，需保活会话/重登策略 |

## 3. 本仓压力来源（按优先级）

1. **`zuoqi` 大图**：单张 2–4.5MB 文件，解码后 RGBA 常为数倍～十几 MB；`dir` 坐骑转向加载多 face/act sheet。
2. **质心采样**：`createImageBitmap` + 全图 `getImageData` — 与 GPU 上传叠加，易在 WebView 上尖峰 OOM / context loss。
3. **无界缓存**：`plistAtlas` Promise 缓存成功路径永不淘汰（正确共享，但人数↑显存↑）。
4. **恢复缺口**：`recoverDisplayAfterForeground` 主路径 nudge 姿态；化妆品 base 未系统化 `source.update()`（可补，禁止 unload）。

姿态路径已用 `poseTextureCache` + live refcount，证明「限额淘汰」可以做，但必须 **只淘汰无人引用的 key**。

## 4. 推荐方案（分阶段，妆容优先）

### Phase A — 只减尖峰，不改生命周期（优先落地）

目标：降低 context loss **发生率**，零风险弄丢妆容。

1. **仅 Capacitor native**：翅膀/坐骑（`!stableFace`）跳过全像素采样，用框中心；称号 `stableFace` 仍采样。
2. **禁止**任何「前缀无帧 / 探测失败」路径上的 `Assets.unload(共享 pngUrl)`。
3. `Assets.load` **自身失败**才可 unload（纹理从未成功交给显示层）。
4. 原生可选：`autoGenerateMipmaps = false`（仅 native）。
5. 像素采样若保留在浏览器：可并行；native 若仍采样则串行。

验收：APK 控制台可见 `[plist] skip pixel sample (native)`；浏览器妆容与现网一致。

### Phase B — 恢复路径补齐（仍不 unload 在用图）

1. `nudgePlistAtlasesAfterGpuResume()`：对 cache 内已 resolve 的 atlas 调 `base.source.update()`。
2. `SceneView` / `ActorLayer`：本地与 peer 的 wing/mount/chenghao sprite `source.update()`。
3. 若 `contextrestored` 后连续 N 次（如 3）仍 `isContextLost` 或渲染全黑 → **受控整页 reload**（记录日志；可选：先断 WS 再 reload，或依赖现有重登）。

参考 Pixi 社区：多次 context loss 后软恢复不可靠，reload 是务实兜底。

### Phase C — 真正限额（高风险，需 refcount）

仅在 A+B 仍不够时：

1. `plistAtlas` 维护 **refcount**（wing/mount/chenghao `set*Id` retain，`clear`/换 sheet release）。
2. Soft/hard 上限只针对 **refcount=0** 的条目；淘汰时 `source.unload()` 或 `Assets.unload`，且 **不得** 卸 refcount>0。
3. 坐骑 `dir`：限制同时缓存的 face 数（例如只保留当前 face + 左右邻面），换向时再加载；旧 face 在 refcount=0 后入淘汰队列。
4. 与 Pixi `textureGCActive` 协调：对化妆品 `TextureSource` 设 `autoGarbageCollect: false`，改由本仓 refcount 管，避免 GC 误卸。

**禁止**再做「无 refcount 的 LRU destroy」。

### Phase D — 资源侧（中长期）

1. 出包前把超大 `zuoqi` PNG 压到合理上限（例如长边 ≤1024、或 ASTC/ETC 若管线允许）。
2. 城镇人数多时客户端策略：远处 peer 只画身体+名牌，进入近距再加载翅/骑（玩法可接受时）。
3. 评估 Pixi 小版本升级（关注 TextureGC / unload 修复）。

## 5. 明确不推荐

- WebView 里追求「永不丢上下文」而不控纹理预算。
- 用 `forceContextLoss` 当常规省显存手段（会放大恢复问题）。
- 浏览器与 APK 同一套激进 unload（浏览器已证共享 PNG unload 毁妆容）。
- 把黑屏当成「妆容协议没下发」去改服务端（协议与黑屏是两条线）。

## 6. 验证计划

1. **浏览器回归**：硬刷新 → 同图多名玩家翅/骑/特效称号；控制台有非 empty 的 `31/5` 与 `cosmetics paint`。
2. **APK Phase A**：城镇挂机 + 多坐骑转向 20–40 分钟；统计 `webglcontextlost` 次数应下降；妆容始终可见。
3. **人为丢上下文**：`WEBGL_lose_context.loseContext()` 后应走 recover；若软恢复失败，Phase B reload 兜底可测。
4. **对比基线**：记录改前/改后同场景 GPU 内存（Android Studio Profiler / `chrome://gpu` 若可连）。

## 7. 建议落地顺序

```
Phase A（native skip sample，禁共享 unload）
    → 打 APK 验黑屏频率 + 妆容
Phase B（cosmetics nudge + 连续丢失 reload）
    → 再验
Phase C（refcount 限额）仅当 A+B 不足
Phase D（压图 / 远距降级）并行资源
```

当前仓库状态：已试跑官方 **GC + `source.unload`（png refcount）+ native skip sample**；禁止成功路径 `Assets.unload`。

2026-08-18 晚：截图确认「HTML 软键在、LCD 全黑」= 假恢复。已改：等 `contextrestored`、像素探针、8s watchdog 软重载、息屏停 TextureGC、地图贴图禁 GC。浏览器务必回归附近翅/骑/称号后再打 APK。
