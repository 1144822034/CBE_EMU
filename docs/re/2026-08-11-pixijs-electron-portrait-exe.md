# PixiJS 竖屏 Electron exe（2026-08-11）

## 目标

把 `PixiJS_web` 打成 Windows **竖屏**桌面客户端（手机布局：LCD + 底栏触控键），离线资源与 Live gateway 与 APK 一致。

## 命令

```bash
cd PixiJS_web
npm run build:exe
```

首次装 Electron 若官方源超时：

```bash
set ELECTRON_MIRROR=https://npmmirror.com/mirrors/electron/
set ELECTRON_BUILDER_BINARIES_MIRROR=https://npmmirror.com/mirrors/electron-builder-binaries/
node node_modules\electron\install.js
```

`build:exe` 经 `scripts/build-electron-exe.mjs`：默认 Electron 镜像；在 **TEMP 新目录** 打包（规避工作区内 `win-unpacked` 被占用导致的 EPERM），再拷回 `electron-release/`。

产物目录：`PixiJS_web/electron-release/`  
便携包：`江湖OL-<version>-portable.exe`（约 90–100MB，含 Chromium）。

开发态（已有 `dist/`）：

```bash
npm run electron
```

## 契约

- 窗口内容区默认 **360×800**，`setAspectRatio` 锁竖屏；不可最大化/全屏
- 加载 `dist/index.html`（`vite base: './'`），含同步后的 `JHOnlineData` / `client_code_version.txt`
- Electron 用自定义协议 `pix://app/…` 托管 `dist/`（避免 `file://` 下 `/JHOnlineData` 指到盘符根导致状态栏 `Failed to fetch`）；客户端资源 URL 亦改为相对/`publicAssetUrl`
- **打包便携 exe**（`app.isPackaged`）冷启动走与 Android 相同的 dist 热更契约，但实现只在 Electron：`electron/dist-hot.cjs` + `main.cjs`。覆盖层在 `%APPDATA%/江湖OL/dist-hot/`（`userData`）。开发态 `npm run electron` **不**拉热更，继续用本机 `dist/`
- Gateway 仍为 `FIXED_LIVE_GATEWAY`（`ws://…`）；与 APK 相同，明文 WS
- 不改玩法协议；无 Android FGS / WakeLock（桌面不需要）；**不改** `android/` 下 `DistHotUpdate` / `MainActivity`

## 实现

| 路径 | 作用 |
|------|------|
| `electron/main.cjs` | BrowserWindow 竖屏壳 + `pix://` 静态资源（CJS，勿写 TS 注解）；打包态启动热更 |
| `electron/dist-hot.cjs` | PC 端清单比对 / 差量下载（与 APK 同 BASE_URL，独立实现） |
| `package.json` → `build` / `build:exe` | electron-builder Windows portable |

## 体积

Electron 自带 Chromium，便携 exe 通常 **100MB+**，远大于 APK。
