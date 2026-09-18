# PixiJS_web 协作约定

改码安全（大改先提交、禁止 PowerShell 改源码）遵循仓库根目录 [`AGENTS.md`](../AGENTS.md)。

## 字体（强制）

**LCD 默认系统宋体**（与参考站「字体切换」打开后一致：`SimSun`）。  
可在 **菜单 → 系统设置 → 字体切换** 选：

| id | 文件 | 说明 |
|----|------|------|
| cn | 系统 `SimSun` / Songti | 高清（OS 宋体栈；**不**打包 simsun.ttf） |
| sky | `font_16_sky.uc3` | 天空点阵（缺省） |
| gb16 | `gb16.uc2.uc3` | 全字库 |
| gb | `font_gb.uc3` | 经典 CBE 子集 |

偏好写入 `localStorage.pix_uc3_font`（缺省 = `sky`）。无系统宋体的设备请用点阵；勿再把 `simsun.ttf` 打进 APK。

- 入口：`BitmapLabel` / `makePixelText`；偏好 `src/resources/uiFontPreference.ts`
- **全部 LCD 文字跟随字体切换**；禁止再写死 `uc3: true`
- **系统宋体（对照 [参考站](http://162.14.112.35:18880/)）**：参考站面板是 DOM `SimSun`，画布仍 `pixelated`。我们同样用 `#lcd-dom-text` HTML 叠字（`BitmapLabel` cn 路径），**禁止**再对整块 LCD 开 `image-rendering: auto`，也禁止把宋体硬折进 canvas（雾/糊/毁形都试过）
- **标题菜单**：点阵用 `title2.gif`；`cn` 用 DOM 宋体描边文案（选中 `menuHi` + `menuHiStroke`）
- 显示字号：均为 `UC3_CELL=14`
- 黑屏防护：见 `docs/re/2026-09-16-equip-detail-alphamode-bitmaplabel-destroy.md`

## 地图名条

地点条背景为 **LCD 全宽**（240px），居中 UC3 白字；见 `drawLocBanner`。

## Android APK

出包 / 改 gateway / 息屏断网：遵循仓库技能
[`.codex/skills/pixijs-android-apk/SKILL.md`](../.codex/skills/pixijs-android-apk/SKILL.md)
与短记 [`docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md`](../docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md)。

默认：`npm run build:android` → `android/app/release/jh_ol.apk`。

竖屏 Windows 便携包：`npm run build:exe` → `electron-release/江湖OL-*-portable.exe`（见 `docs/re/2026-08-11-pixijs-electron-portrait-exe.md`）。
