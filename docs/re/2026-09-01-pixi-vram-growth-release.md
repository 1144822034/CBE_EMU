# Pixi 显存持续上涨与可释放资源

日期：2026-09-01

相关：`2026-09-01-pixi-disable-cosmetic-atlas.md`、`2026-08-31-pixi-black-screen-root-cause.md`

## 结论

关掉翅/骑/动态称号 atlas 后，显存仍会涨。黑屏链：WebView VRAM 压力 → `webglcontextlost` → 软恢复不全。

仅改 PixiJS（不碰 CBE）。

## 涨因与状态

| 优先级 | 来源 | 状态 |
|--------|------|------|
| P0 | `BattleScreen.hide` 不释放 `poseTexCache` / `mapSprite` | **已修** |
| P0 | 头像 / 组队头像 `Texture.from` remount 不毁 texture | **已修** |
| P0 | `SystemPanel.equipActorTex` 换新前不 destroy | **已修** |
| P1 | 地图同伴姿态 live 键永不淘汰 | **已修（视口 LOD）** |
| P1 | SCE 装饰每实例一张 texture | **已修（按 actor 共享）** |
| P2 | UiAssets 无软上限 | **已修（LRU + pin）** |
| P2 | 关面板 UC3 仍 pin live | **已修** |
| P2 | 选角 / 创角 bodyCache 进世界后仍占 | **已修** |

## 释放点

1. [`BattleScreen.hide`](../../PixiJS_web/src/ui/BattleScreen.ts)：清战斗 pose / map GPU。
2. [`WorldHud`](../../PixiJS_web/src/ui/WorldHud.ts) / [`BattlePartyHud`](../../PixiJS_web/src/ui/BattlePartyHud.ts) / [`SystemPanel.setEquipActor`](../../PixiJS_web/src/ui/SystemPanel.ts)：旧肖像 `destroy({ texture: true })`。
3. [`ActorLayer`](../../PixiJS_web/src/game/ActorLayer.ts)：视口外同伴 `poseLod` — 拆 body Sprite、`releasePoseTextureKey`，保留名条；[`trimPoseTextures`](../../PixiJS_web/src/render/poseTextureCache.ts) 回收 idle。
4. [`SceneView`](../../PixiJS_web/src/render/SceneView.ts)：`decorTexByActor` 同资源共享；离场统一销毁。
5. [`UiAssets`](../../PixiJS_web/src/ui/UiAssets.ts)：full/crop soft-hard LRU；HUD 底图 pin。
6. System / Chat / Dialog / Enhance / Menu `hide`：销毁 labels（unpin UC3）；System 顺带丢 `equipActorTex`。
7. Role / CreateRole：离场清 `bodyCache`。

## 特效稳定版（1.0.61+）

在 **保留本地翅/骑/称号 atlas** 前提下压低 VRAM：

| 项 | 值 |
|----|-----|
| 附近玩家特效 | 默认关（`peerCosmeticsPref`，仅显式 `1` 开启） |
| plist GPU 延迟卸载 | 原生 **8s**（原 30s） |
| 进战斗 | `showPhase("battle")` → `releaseMapCosmeticGpu` + `releaseAllPeerCosmeticGpu` |
| 回世界 | `resumeMapCosmeticGpu` + `refreshPeerCosmeticsVisibility` |
| 同屏重特效同伴 | `PEER_HEAVY_CAP` 原生 **2** / 浏览器 **3** |

## 验证

1. 连续进退战斗 10+ 次；城镇挂机有远处玩家时黑屏不应随时间明显恶化。
2. 反复开关装备 / 聊天 / 菜单：再开内容正常。
3. 选角 → 进世界 → 回选角：预览重载正常。
4. `npx tsc --noEmit`。
