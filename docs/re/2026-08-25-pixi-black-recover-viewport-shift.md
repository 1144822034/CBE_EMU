# 黑屏恢复后画面偏移 / 卡死

日期：2026-08-25（2026-08-28 / 08-29 迭代）

## 现象演进

| 阶段 | 行为 |
|------|------|
| 更早 | 恢复后 LCD **左上**漂移，键可用 |
| 2026-08-28 | `inset+margin` + 强制 `repair`(canvas hide) → 左上没了，**上移后卡死** |
| 2026-08-29a | soft reset → **不卡死**，左上又回 |
| 2026-08-29b | `#stage` **flex 居中** `#phone-wrap`（无 absolute / `-50%`） |

## 根因对照

| 方案 | 问题 |
|------|------|
| `left/top:50%` + `translate(-50%,-50%)` | GL restore 后合成层粘住 → 左上 |
| `inset:0` + `margin:auto` | 若只残留 `left/top:0` 无 right/bottom → 钉左上；强 kick+canvas hide → 卡死 |
| flex `align/justify:center` | 居中不依赖 transform / 四边 inset，恢复时只清 IME pan translate |

卡死另线：`lost=false` + 死贴图 alphaMode 吞帧 → 见 `2026-08-29-pixi-blank-alive-alphamode-recover.md`。

## 修改（2026-08-29b）

- `index.html`：`#stage` flex 居中；`#phone-wrap` `position:relative`
- `forceShellWrapCenteringAnchors`：清 absolute inset，钉 relative
- `tryRestoreGlContext`：仅 `isContextLost` 时调用（消 `context not lost` 噪音）
- alphaMode forensic：先挡 `extract.pixels`，避免探针二次崩

## 验证

1. 息屏亮屏 / focus recover：LCD 居中，无左上黑边；D-pad 可用。
2. 不应再刷 `restoreContext: context not lost`。
3. 偶发 alphaMode：strip 或 soft reload，不应永久黑。

## 续（2026-09-02）fitPhoneWrap 抖动跳过未修 shell

| 现象 | 画面缩到左上，右侧/下方大块黑 |
|------|------------------------------|
| 根因 | `heightJitterOnly` 提前 return 时未清 IME `translate`、未重钉 canvas `inset:0` + `100%` |
| 修复 | `stabilizeShellViewport`：`ensureStageFlexCenter` + `ensureCanvasFillsPhoneWrap` + 可选 `repairShellViewportAlignment`；`fitPhoneWrap` 每次结束/抖动跳过都调用；`showPhase` 进世界/战斗等再稳一次 |
