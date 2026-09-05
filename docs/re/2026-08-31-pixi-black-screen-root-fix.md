# Pixi APK 黑屏：根因闭合（全量软恢复 + 会话硬重载）

日期：2026-08-31

相关：

- [`2026-08-18-pixi-apk-black-screen-plan.md`](2026-08-18-pixi-apk-black-screen-plan.md)
- [`2026-08-30-pixi-battle-black-recover-incomplete.md`](2026-08-30-pixi-battle-black-recover-incomplete.md)
- [`2026-08-31-pixi-chat-panel-black-recover.md`](2026-08-31-pixi-chat-panel-black-recover.md)
- [`2026-08-29-pixi-recover-soft-reload-to-splash.md`](2026-08-29-pixi-recover-soft-reload-to-splash.md)

外部对照：

- Khronos [HandlingContextLost](https://wikis.khronos.org/webgl/HandlingContextLost) — `preventDefault` + 重建全部 GL 资源
- [Pixi Discussion #11196](https://github.com/pixijs/pixijs/discussions/11196) — 真·contextlost 后 Graphics 常不可用
- Issue #7232 评论 — 多次 lost 后软恢复不可靠，商业应用选择重启

## 根因陈述

| 项 | 内容 |
|----|------|
| 触发 | Android WebView 纹理压力 → `webglcontextlost` → `contextrestored` / stall recover |
| 违反契约 | 恢复后仍挂在 stage 上的 **Texture + Graphics + mask** 须全部具备有效 GPU 资源 |
| 首个错误状态（历史） | 地图/姿态假活黑精灵；Graphics mask 裁空；overlay chrome 空白；软重载回 splash |
| 证据 | 六轮战斗/城镇 recover 文档；聊天面板裸字截图；soft-reload→标题 |

黑屏是 **显示症状**，不是协议假死。业界共识：真·lost 无法保证 Graphics 软恢复 100%；可靠兜底是硬重启，但本仓硬重启必须 **保留会话重进世界**。

## 本轮闭合（三轨）

### Track A — 全量 overlay 软恢复

- `gpuResume.remountGraphicsThen` 约定：`replaceGraphics` → `redraw`
- 凡长寿命 `paintCbePanel` / `Graphics` 面板补 `nudgeAfterGpuResume`（菜单/对话/系统/交易/玩法面板/鉴权屏/小地图/任务追踪等）
- `main.nudgeOverlayPanelsAfterGpuResume` 对所有可见实例调用（仍挂在 recover / late 280ms / alphaMode strip；战斗优先分路不变）

### Track B — 会话保留硬重载

- 新模块 `PixiJS_web/src/net/glSessionResume.ts`：`sessionStorage` 快照（TTL 90s）含 user/pass/serverId/roleId/hangupDesired/autoBattle
- `softReloadGl` 在 `location.reload()` 前写入快照
- 冷启动 `tryResumeAfterGlSoftReload`：login → confirmServer → selectRole → 世界（失败/过期仍回 splash）
- **禁止**持久化战斗帧/地图像素；战斗靠进角后服务端状态

### Track C — 预防尖峰

- `app.init`：`backgroundAlpha: 1`；native `preference: "webgl"`；保持 low-power
- 已有：native/全平台 wing·mount 跳过全像素采样；plist PNG refcount + `source.unload`（禁共享 `Assets.unload`）
- native `PEER_HEAVY_CAP` 6 → 4（最近 N 同乡才挂翅/骑/动称号）

## 明确不做（仍有效）

- `forceContextLoss` 当省显存
- 无 refcount LRU destroy 共享妆容 PNG
- 中途销毁重建整个 `Application`
- 扩大 soft-reload 触发条件

## 验证矩阵

1. 城镇挂机黑屏 → recover-ok：地图+角色+状态条色板
2. 战斗挂机黑屏 → 场景带+血槽填充
3. 打开聊天/菜单/对话时黑屏 → 面板底框恢复
4. 软恢复连续失败 → 硬重载后自动回世界（**不回标题**）；控制台可见 `[gl] session resume`
5. 浏览器回归：附近翅/骑/称号仍可见（refcount 路径未改坏）
6. `PixiJS_web`：`npx tsc --noEmit` 或既有 build

## 修改点索引

| 路径 | 行为 |
|------|------|
| `src/render/gpuResume.ts` | remount helper 注释 |
| `src/ui/*Panel*.ts` 等 | `nudgeAfterGpuResume` |
| `src/main.ts` | overlay 注册、softReload 快照、冷启动 resume、init GL 参数 |
| `src/net/glSessionResume.ts` | 快照读写 |
| `src/game/ActorLayer.ts` | native peer heavy cap 4 |
