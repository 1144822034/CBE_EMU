# PixiJS_web 协作约定

改码安全（大改先提交、禁止 PowerShell 改源码）遵循仓库根目录 [`AGENTS.md`](../AGENTS.md)。

## 字体（强制）

**LCD 内文字默认 `YaHei Consolas Hybrid` TTF**（`BitmapLabel` crisp bake，`smooth: false`）。  
源文件：`web/fs/YaHei Consolas Hybrid.ttf` → `/YaHeiConsolasHybrid.ttf`（约 14.5MB；`sync-offline-assets` 会拷进 `public/` / APK）。  
回退点阵：`?uc3=1` 或 `localStorage.pix_uc3=1` → `gb16.uc2.uc3`。HTML 软键走 DOM，用 `--ui-font`。

| 用途 | 实现 | 说明 |
|------|------|------|
| 聊天、背包、面板、战斗 UI | `BitmapLabel` / `makePixelText` 默认 | 系统 TTF + crisp bake |
| 多色/逐字文案 | `layoutSameColorRuns` + Label | 同色同行合并 |
| 地图头顶名（己方/玩家/怪/NPC） | 系统 TTF + `outline: true, outlineThin: true` | 细描边；传送石名无描边 |
| 称号头顶条、战斗倒计时 DigitStrip | GIF | 图片字体，不动 |
| 调试：UC3 点阵 | `?uc3=1` | 旧 CBE 硬像素 |

- 入口：`BitmapLabel`（`src/ui/BitmapLabel.ts`）、`makePixelText`（`src/ui/constants.ts`）
- 加载：启动 `loadSystemFont()`（`src/resources/systemFont.ts`）
- 系统字号默认 `UI_FONT_SIZE=12`；换行按 `wrapUiText` / 字号估算
- 可见软键是 HTML `#sk-left`；画布 `SoftkeyBar` 默认隐藏

## 地图名条

地点条背景为 **LCD 全宽**（240px），居中白字；见 `drawLocBanner`。

## Android APK

出包 / 改 gateway / 息屏断网：遵循仓库技能
[`.codex/skills/pixijs-android-apk/SKILL.md`](../.codex/skills/pixijs-android-apk/SKILL.md)
与短记 [`docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md`](../docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md)。

默认：`npm run build:android` → `android/app/build/outputs/apk/debug/app-debug.apk`。
