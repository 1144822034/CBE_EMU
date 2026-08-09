# PixiJS_web 协作约定

## 字体（强制）

**LCD 内文字默认 `gb16.uc2.uc3` 硬点阵（`smooth: false`，Unicode 索引）。**  
回退 CBE 子集：把 `UC3_FONT_URL` 改回 `/font_gb.uc3`。PixiJS **没有**单独「移动端字体」；HTML 软键清晰因走 DOM。

| 用途 | 实现 | 说明 |
|------|------|------|
| 聊天、背包、面板、战斗 UI | `BitmapLabel` / `makePixelText` 默认 | `{ uc3: true, smooth: false, outline: false }` |
| 多色/逐字文案 | `layoutSameColorRuns` + UC3 | 同色同行合并 |
| 地图头顶名（己方/玩家/怪/NPC） | UC3 + `outline: true, outlineThin: true` | 细描边；传送石名无描边 |
| 称号头顶条、战斗倒计时 DigitStrip | GIF | 图片字体，不动 |
| 仅实验：系统字体 | `{ uc3: false, crisp: true }` | 不推荐上正式包 |

- 入口：`BitmapLabel`（`src/ui/BitmapLabel.ts`）、`makePixelText`（`src/ui/constants.ts`）
- 字库：`/gb16.uc2.uc3`（源 16×16 Unicode，~860KB；启动 `loadUc3Font()`）
- 显示字号：`UC3_CELL=14`（源 16 缩小一号）；换行按显示宽：`wrapUiText` / `UC3_CELL`
- 可见软键是 HTML `#sk-left`；画布 `SoftkeyBar` 默认隐藏

## 地图名条

地点条背景为 **LCD 全宽**（240px），居中 UC3 白字；见 `drawLocBanner`。

## Android APK

出包 / 改 gateway / 息屏断网：遵循仓库技能
[`.codex/skills/pixijs-android-apk/SKILL.md`](../.codex/skills/pixijs-android-apk/SKILL.md)
与短记 [`docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md`](../docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md)。

默认：`npm run build:android` → `android/app/build/outputs/apk/debug/app-debug.apk`。
