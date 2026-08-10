# PixiJS Capacitor APK 离线资源 + 息屏保活（2026-08-08）

## 已验证结论

`BUILD SUCCESSFUL`（约 4min，JDK 21 + 本地 Gradle 8.11.1）。  
APK：`PixiJS_web/android/app/build/outputs/apk/debug/app-debug.apk`（约 6.2MB，含 `web/fs/JHOnlineData`）。

Agent 复用：`.codex/skills/pixijs-android-apk/SKILL.md`。

## 打包契约

- 资源：`web/fs/JHOnlineData`（否则 `bin/JHOnlineData`）→ `public/` → `dist` → APK
- Gateway 固定：`ws://47.108.210.191:19092`（无 Demo）
- 登录 `codeVersion` ← `client_code_version.txt`；服端比对 `server_code_version.txt`
- 命令：`cd PixiJS_web && npm run build:android`（内部用 `scripts/assemble-android.mjs`）

## 工具链（成功机）

| 项 | 值 |
|----|-----|
| JDK | `%USERPROFILE%\.gradle\jdks\eclipse_adoptium-21-amd64-windows.2`（**21**，Capacitor 7 必需） |
| 失败例 | `D:\installsoft\JDK\jdk11` = Zulu **17** → `无效的源发行版：21` |
| Gradle | 已解压 `...\gradle-8.11.1-all\6gcpoccneql1b0krsle0llw37\`；**勿**改腾讯镜像 URL（落到 `2qik…` 空目录超时） |
| SDK | `ANDROID_HOME=D:\installsoft\JDK\android-sdk` |

绕过 wrapper 下载：直接调本地 `gradle-8.11.1\bin\gradle.bat assembleDebug`，或 `node scripts/assemble-android.mjs`。

## 息屏不断网

根因：WebView `onPause` 停定时器 → gateway WS 断。

1. `MainActivity`：`PARTIAL_WAKE_LOCK`；`onPause`/`onStop` 后 `onResume` + `resumeTimers`
2. 首次弹「忽略电池优化」
3. 前端 `visibilitychange`/`focus` → `session.wakeFromBackground()`
4. 进图即拉起 `HangupKeepAlive` 前台服务（`online` 文案；挂机意图武装时为 `hangup`），见 [2026-08-10-pixi-android-background-disconnect.md](2026-08-10-pixi-android-background-disconnect.md)、[2026-08-10-pixi-hangup-keepalive-resume.md](2026-08-10-pixi-hangup-keepalive-resume.md)

Manifest：`WAKE_LOCK`、`FOREGROUND_SERVICE`/`FOREGROUND_SERVICE_DATA_SYNC`、`POST_NOTIFICATIONS`、`REQUEST_IGNORE_BATTERY_OPTIMIZATIONS`、`usesCleartextTraffic`
