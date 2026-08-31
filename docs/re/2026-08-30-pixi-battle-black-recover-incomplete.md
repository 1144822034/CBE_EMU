# Pixi 黑屏恢复后战斗场景 / 血槽未恢复

日期：2026-08-30

相关：`2026-08-18-pixi-apk-black-screen-plan.md`、`2026-08-19-pixi-apk-black-recover-freeze.md`、`2026-08-29-pixi-blank-alive-alphamode-recover.md`

## 现象

挂机战斗中遭遇黑屏后「恢复」：

- 顶栏 / 「挂机战斗中…」/ 角色名 / HP 数字（UC3 / 部分 UI 贴图）可见
- **战斗地图与站位角色黑**（场景带空白）
- 底部 `xuetiao` 外框在，**血槽 / 蓝槽 Graphics 填充空洞**（或已恢复填充而场景仍黑）
- 个别头像黑框

会话仍在线，不是协议断线。浮动伤害字（「技-1705」）仍可出现在黑底上。

## 第一次偏离

不在挂机逻辑，而在 **GL 恢复契约不完整**：

1. `recoverDisplayAfterForeground` / alphaMode strip 只调用 `SceneView.nudgeAfterGpuResume()`（城镇地图 + ActorLayer）。
2. **战斗**走独立的 `BattleScreen`（`mapHost` / `unitsHost` / `BattlePartyHud`），恢复路径曾从未 nudge。
3. UC3 `refreshAllUc3Labels` 解释了文字为何回来。
4. Pixi 8 在 `webglcontextrestored` 后 **Texture** 常可用 `source.update()` 救回；**Graphics** GPU 几何常仍失效（既有文档已记），仅 `clear()+fill()` 往往不够，需替换 DisplayObject 再画。

权威对照：`2026-08-18-pixi-apk-black-screen-plan.md` §2.1「复杂场景常不完全（Graphics 失效）」。

## 根因陈述（第一轮）

| 项 | 内容 |
|----|------|
| 触发 | Android WebView `webglcontextlost` → `contextrestored`（或 stall recover） |
| 违反契约 | 恢复后所有仍挂在 stage 上的可见层应重新具备有效 GPU 资源 |
| 首个错误状态 | 战斗 map/unit 贴图未 `update`；血槽 `Graphics` 缓冲空白；世界 HUD 同类 fill 同样缺口 |
| 证据 | 截图：框+字在、填充与场景无；代码路径无 `battle.nudge*` |

## 修改（第一轮）

| 点 | 行为 |
|----|------|
| `render/gpuResume.ts` | `nudgeTextureSource` / `replaceGraphics` |
| `BattleScreen.nudgeAfterGpuResume` | 地图 + pose 缓存 `source.update`；脚底阴影 Graphics 替换；`partyHud.nudge` |
| `BattlePartyHud.nudgeAfterGpuResume` | 替换 HP/SP Graphics → `setParty` 重画 + 头像 remount |
| `WorldHud.nudgeAfterGpuResume` | 缓存 vitals/party/肖像；同样替换 fill 并重画（城镇路径） |
| `main` recover / alphaMode strip | `view` 之后调用 `battle` + `hud` nudge |

## 续记：第一轮后场景仍黑（同日）

用户复测截图：血槽数字 / 浮动字 / 挂机条已出，**战斗地图与角色站位仍全黑**。

### 第二次偏离

不在「未调用 battle nudge」，而在 **恢复手段对 BufferImageSource 无效**：

1. 战斗地图与姿态走 `rgbaToTexture` → Pixi `BufferImageSource`（非 canvas）。
2. 城镇 `SceneView` 地图是 `Texture.from(canvas)`，`source.update()` 往往够用；战斗路径相同 API 在 WebView restore 后常得到 **宽高仍有效、GPU 槽位空** 的黑精灵。
3. `nudgeTextureSource` + `paintUnitSprite` **复用** `poseTexCache` 里的旧 `Texture`：即便未 destroy，sprite 仍绑死纹理；key 未变时甚至不换绑。
4. `stripBrokenTextureSprites` 只隐藏 `source==null`；此类「假活」贴图不会被 strip。

### 根因陈述（第二轮）

| 项 | 内容 |
|----|------|
| 触发 | 同上；第一轮 nudge 已跑 |
| 违反契约 | 失效的 BufferImageSource 必须从 CPU RGBA **新建** Texture，不能只 `update` / 缓存命中 |
| 首个错误状态 | `mapSprite` / unit sprites 仍指向 restore 前的 GPU 句柄；LCD 中部黑、HUD 字仍在 |
| 证据 | 截图场景带黑 + 伤害字；`rgbaToTexture` 与 `poseTexCache` 复用路径 |

### 修改（第二轮）

| 点 | 行为 |
|----|------|
| `BattleScreen.mapPixels` | `paintMap` 保留 CPU 像素副本 |
| `applyMapTexture` | 加载与 resume 共用：销毁旧 sprite，新 `rgbaToTexture` + 新 letterbox Graphics |
| `nudgeAfterGpuResume` | 用地图像素重建；**清空并 destroy** `poseTexCache`；`paintUnitSprite` 强制换绑；阴影 `replaceGraphics` |
| `paintUnitSprite` | texture destroyed / 对象不一致时也重新赋值 |
| `main` recover late 280ms | 再 nudge `view` / `battle` / `hud`（WebView 常晚一帧才可用） |

## 续记：战斗已恢复、城镇地图仍黑（同日）

用户确认战斗层正常后，城镇 `SceneView` 仍黑。

### 第三次偏离

与战斗同类：**城镇地图 / 共享姿态缓存仍只做 `source.update()`**：

1. 城镇地图曾用 `Texture.from(canvas)`，resume 只 `update`；现改为与战斗一致的 CPU 像素 + `rgbaToTexture` 整图重建。
2. 玩家 / NPC / peer / 传送门姿态走共享 `poseTextureCache`（`BufferImageSource`）。`ActorLayer.applyFrame` 在缓存命中时**不会**新建 Texture → 黑精灵。
3. SCE 装饰物各自 `rgbaToTexture`，resume 时只 update，不重建。

### 修改（第三轮）

| 点 | 行为 |
|----|------|
| `SceneView.mapPixels` + `mountMapSprite` | 进图保留像素；resume 销毁旧 map sprite 并重建 |
| `nudgeAfterGpuResume` | `clearPoseTextureCache` → 重画 decor / portal → `applyPlayerFrame` → `actors.nudge` |
| `ActorLayer.nudgeAfterGpuResume` | 清空 `fuhao` 缓存并强制任务叹号 remount；`applyFrame` 换绑姿态 |

## 续记：城镇 HUD 在、地图/角色全黑（同日）

用户截图「正存室」：顶栏地点 / 血槽 / 挂机钮 / 聊天在，**地图带全黑**（怪物与玩家也不见）。战斗同症状时浮动字仍可出现。

### 第四次偏离

不在贴图重建本身，而在 **Container.mask 用的 Graphics 失效**：

1. `SceneView.clip.mask` = 白矩形 Graphics；`BattleScreen.stageMask` 同时 mask `mapHost` / `unitsHost` / `fxHost`。
2. 既有文档已记 Graphics 在 restore 后 GPU 几何空白；mask 空白 ⇒ 被裁切层像素为 0。
3. WorldHud / floatHost / UC3 在 mask 外 → 仍可见，与截图一致；此前只重建 map/pose texture，**从未替换 clip/stage mask**，所以场景永远出不来。
4. 次要：单位/玩家仅 `sprite.texture = tex`，post-restore Sprite 仍可能黑；需销毁后新建 Sprite。

### 根因陈述（第四轮）

| 项 | 内容 |
|----|------|
| 触发 | 同上 contextlost/restore 或 stall recover |
| 违反契约 | 作为 mask 的 Graphics 必须与其它 Graphics 一样 `replaceGraphics` 后再画 rect |
| 首个错误状态 | clip/stage mask 几何空 → 地图与单位层被裁没；HUD 仍画 |
| 证据 | 截图 HUD+挂机+系统字；代码 mask 仅构造时创建一次 |

### 修改（第四轮）

| 点 | 行为 |
|----|------|
| `SceneView` | 持有 `clipMask`；nudge 时 replace + 重画；`mapPointer` / `playerFallback` 同步 replace |
| `BattleScreen` | 持有 `stageMask`；nudge **最先** replace 并重新赋给三 host |
| 单位/玩家 | resume 销毁旧 Sprite，再 `paintUnitSprite` / `applyFrame` 新建 |
| wing/mount/chenghao | `remountAfterGpuResume` 清 atlas 强制重载 |

## 验证

1. 战斗挂机中复现黑屏恢复：场景地图与角色应重新可见；血槽有红/绿填充且与数字一致。
2. 城镇恢复：地图与 NPC/自身/装饰应重新可见；自身 tiao 血槽不应空洞。
3. 正常无 contextlost 路径：不应额外销毁/重建战斗单位或打断 playback。
4. 恢复后连续多回合挂机：地图不闪绿、姿态不长期黑影。
5. 城镇黑屏恢复后走动：地图不黑、角色能转向换帧。
6. 复现「正存室」类 HUD 在场景黑：恢复后地图带应出图，不应只剩字与血槽。
7. 挂机战斗中黑屏恢复：地图已能恢复的前提下，战斗场景地图与站位也应出图（第五轮回归）。

## 续记：地图已恢复、战斗又黑（2026-08-31）

用户：战斗曾恢复成功一次；修完城镇 clip mask 后，挂机战斗再黑屏恢复 → 仍「字/血槽在、场景带全黑」。

### 第五次偏离

地图修复引入的两条路径打到战斗：

1. **共享 `stageMask`**：`mapHost` / `unitsHost` / `fxHost` 共用一块 Graphics。Pixi 8 `StencilMask.reset()` 会改 `mask.includeInBuild`；`replaceGraphics` 销毁旧 mask 时多个 MaskEffect 抢同一节点 → 裁切层空，HUD（未 mask）仍在。城镇 `SceneView` 是 **一个 clip + 一块 mask**，所以地图好了、战斗坏了。
2. **recover 总是先 `view.nudgeAfterGpuResume()`**：战斗中仍整城 remount（pose 缓存清空、地图重建、翅膀 atlas），与战斗 BufferImageSource 重建争用恢复后的 GL。

### 根因陈述（第五轮）

| 项 | 内容 |
|----|------|
| 触发 | 同上；城镇 mask 修复已上线 |
| 违反契约 | 每条裁切带一个 mask 所有者；可见场景才 nudge |
| 首个错误状态 | 共享 mask replace 后 stage 带像素为 0；或城镇 nudge 干扰战斗贴图重建 |
| 证据 | 地图可恢复 + 战斗同症状回归；StencilMask.reset / 三 host 共 mask |

### 修改（第五轮）

| 点 | 行为 |
|----|------|
| `BattleScreen.stageHost` | 与 `SceneView.clip` 同构：一层容器 + 一块 mask；map/units/fx 为其子节点 |
| `nudgeAfterGpuResume` | 先 `stageHost.mask = null` 再 `replaceGraphics`，再赋回；强制 host.visible |
| `SceneView` | clip 同样先卸 mask 再 replace（对称稳妥） |
| `main` recover / alpha strip / late 280ms | 战斗可见时只 nudge `battle`；否则 `view`+`hud` |
