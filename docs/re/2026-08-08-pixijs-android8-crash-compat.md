# PixiJS APK：Android 8.0 启动闪退兼容（2026-08-08）

## 现象

Android 8.0（API 26）设备安装 `com.jianghu.pixijs` 后启动闪退；较新系统可进标题页。

## 取证 / 假设

无该机 logcat 时按 Capacitor 7 + SplashScreen + WebView 契约排查：

1. **启动主题**：`AppTheme.NoActionBarLaunch` 原为 `Theme.SplashScreen` + `android:background=@drawable/splash`，未设 `postSplashScreenTheme`，也未 `installSplashScreen()`。AndroidX SplashScreen 在部分 API 26 OEM 上会因 drawable/ColorStateList 解析失败闪退。
2. **Capacitor Bridge NPE**：`Bridge.isMinimumWebViewInstalled()` 在 API≥26 直接对 `WebView.getCurrentWebViewPackage()` / `versionName` 解引用；部分 Android 8 OEM 返回 `null` → 启动期 NPE。
3. **电池优化 Intent**：`ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` 在 `onCreate` 同步拉起，个别 OEM 不稳定（已改为延迟 + `resolveActivity`）。
4. **WebView 过旧**：PixiJS 8 需要较新 Chromium；未更新的 Android 8 可能无法跑 JS，应显示说明页而非白屏/崩。

## 修改

| 点 | 位置 |
|----|------|
| 启动主题改经典 `windowBackground` | `android/.../values/styles.xml` |
| 补 `colors.xml`、硬件加速、cleartext `network_security_config` | Manifest / res |
| Bridge null-guard（脚本可重打） | `scripts/patch-capacitor-android8.mjs` → `cap:sync` |
| 电池优化延迟弹出 | `MainActivity.java` |
| `errorPath` + `minWebViewVersion: 69` | `capacitor.config.ts` + `public/webview-update.html` |
| Vite `target: chrome69` | `vite.config.ts` |

## 验证

1. Android 8.0：应能进 WebView；若系统 WebView &lt; 69，见「需要更新系统 WebView」页，不闪退。
2. Android 10+：登录 / 进图 / 息屏不断网回归。
3. 出包：`cd PixiJS_web && npm run build:android` → APK `1.0.17+`。

## 仍未知

- 具体闪退机 logcat / tombstone（若仍崩请抓 `AndroidRuntime` FATAL）。
- 极旧 WebView 即使不闪退也无法玩；需用户更新 System WebView / Chrome。
