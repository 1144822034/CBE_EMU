# PixiJS Capacitor APK 离线资源 + 息屏保活（2026-08-08）

## 已验证结论

`BUILD SUCCESSFUL`（约 4min，JDK 21 + 本地 Gradle 8.11.1）。  
APK：`PixiJS_web/android/app/build/outputs/apk/debug/app-debug.apk`（约 6.2MB，含 `web/fs/JHOnlineData`）。  
一键出包后拷贝为 **`android/app/release/jh_ol.apk`**（Gitee 自更新资产名，见 `scripts/assemble-android.mjs`）。

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

1. `MainActivity`：`PARTIAL_WAKE_LOCK`；`onPause` 前后 `resumeTimers`；运行时申请通知权限
2. 未授权电池优化时周期性再弹「忽略电池优化」
3. 前端 `visibilitychange`/`focus` → `session.wakeFromBackground()`；FGS 每 2s 注入 `__pixiNativeKeepAlive`
4. 进图即拉起 `HangupKeepAlive` 前台服务（API34+ `specialUse`；`online`/`hangup` 文案），见 [2026-08-10-pixi-android-background-disconnect.md](2026-08-10-pixi-android-background-disconnect.md)、[2026-08-10-pixi-hangup-keepalive-resume.md](2026-08-10-pixi-hangup-keepalive-resume.md)

Manifest：`WAKE_LOCK`、`FOREGROUND_SERVICE`/`_DATA_SYNC`/`_SPECIAL_USE`、`POST_NOTIFICATIONS`、`REQUEST_IGNORE_BATTERY_OPTIMIZATIONS`、`usesCleartextTraffic`

## 2026-09-01 出包

`versionCode` **55** / `1.0.55`，`client_code_version` **36**，服端 `web/fs` 与 `bin/JHOnlineData/server_code_version.txt` 同为 **36**。  
含黑屏根因闭合（全量 overlay nudge、会话保留 softReload、native peer cap）。  
APK：`PixiJS_web/android/app/release/jh_ol.apk`（约 57.1 MB）。覆盖安装会清 WebView（versionCode 变化）。

## 2026-08-31 出包

`versionCode` **54** / `1.0.54`，`client_code_version` **35**（未升登录门禁；非特别指定不改），服端同为 **35**。  
含黑屏恢复第六轮：城镇/战斗 clip 改 Sprite mask remount、状态条底板重挂、blank 探测避开队伍头像。  
APK：`PixiJS_web/android/app/release/jh_ol.apk`（约 57 MB）。覆盖安装会清 WebView（versionCode 变化）。

## 2026-08-28 出包

`versionCode` **48** / `1.0.48`，`client_code_version` **32**，服端 `web/fs` 与 `bin/JHOnlineData/server_code_version.txt` 同为 **32**。  
含黑屏恢复后 LCD 左上偏移修补（`#phone-wrap` 改 inset+margin:auto，recover 接入 `repairShellViewportAlignment`）。  
APK：`android/app/release/jh_ol.apk`；PC：`electron-release/江湖OL-1.0.48-portable.exe`；gateway：`gateway-release/ws-gateway.exe`。

## 2026-08-21 出包

`versionCode` **44** / `1.0.44`，`client_code_version` **24**，服端 `web/fs` 与 `bin/JHOnlineData/server_code_version.txt` 同为 **24**。  
含比武禁自动/物品 + 20s 超时普攻。APK：`android/app/release/jh_ol.apk`；PC：`electron-release/江湖OL-1.0.44-portable.exe`。

## 2026-08-31 黑屏根因闭合

见 [`2026-08-31-pixi-black-screen-root-fix.md`](2026-08-31-pixi-black-screen-root-fix.md)：全量 overlay Graphics nudge、softReload 会话保留重进世界、native peer 妆容 cap 收紧。出包仍走本文 `build:android` 流程。

## 2026-08-19 出包

`versionCode` **42** / `1.0.42`，`client_code_version` **22**（`bundledCodeVersion.ts` 同步），服端 `web/fs` 与 `bin/JHOnlineData/server_code_version.txt` 同为 **22**。  
`BUILD SUCCESSFUL`；产物 `PixiJS_web/android/app/release/jh_ol.apk`（54.6 MB）。含黑屏 recover 修补。覆盖安装会清 WebView。线上服读到 22 后，旧包登录会「请更新客户端」。
