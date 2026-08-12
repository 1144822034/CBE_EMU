# PixiJS_web 协作约定

改码安全（大改先提交、禁止 PowerShell 改源码）遵循仓库根目录 [`AGENTS.md`](../AGENTS.md)。

## 字体（强制）

**LCD 内文字仅 UC3 硬点阵**（`BitmapLabel`，`smooth: false`）。  
默认 `font_16_sky.uc3`；可在 **菜单 → 系统设置 → 字体切换** 选：

| id | 文件 | 说明 |
|----|------|------|
| sky | `font_16_sky.uc3` | 天空点阵（默认） |
| gb16 | `gb16.uc2.uc3` | 全字库 |
| gb | `font_gb.uc3` | 经典 CBE 子集 |

偏好写入 `localStorage.pix_uc3_font`。HTML 软键走 DOM 系统字体栈。

- 入口：`BitmapLabel` / `makePixelText`；偏好 `src/resources/uiFontPreference.ts`
- 加载：启动 `loadUc3Font()`；切换 `applyUc3FontId` + `refreshAllUc3Labels`
- 显示字号：`UC3_CELL=14`（源 16 缩小一号）

## 地图名条

地点条背景为 **LCD 全宽**（240px），居中 UC3 白字；见 `drawLocBanner`。

## Android APK

出包 / 改 gateway / 息屏断网：遵循仓库技能
[`.codex/skills/pixijs-android-apk/SKILL.md`](../.codex/skills/pixijs-android-apk/SKILL.md)
与短记 [`docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md`](../docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md)。

默认：`npm run build:android` → `android/app/build/outputs/apk/debug/app-debug.apk`。

竖屏 Windows 便携包：`npm run build:exe` → `electron-release/江湖OL-*-portable.exe`（见 `docs/re/2026-08-11-pixijs-electron-portrait-exe.md`）。
