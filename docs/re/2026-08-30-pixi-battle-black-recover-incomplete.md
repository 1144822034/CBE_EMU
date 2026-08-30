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

## 验证

1. 战斗挂机中复现黑屏恢复：场景地图与角色应重新可见；血槽有红/绿填充且与数字一致。
2. 城镇恢复：地图仍靠既有 SceneView nudge；自身 tiao 血槽不应空洞。
3. 正常无 contextlost 路径：不应额外销毁/重建战斗单位或打断 playback。
4. 恢复后连续多回合挂机：地图不闪绿、姿态不长期黑影。
