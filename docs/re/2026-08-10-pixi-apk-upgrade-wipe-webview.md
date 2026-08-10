# Pixi APK 覆盖安装清理旧 WebView 数据

日期：2026-08-10

## 需求

玩家安装新版 APK（覆盖旧包）时，清掉上一版残留的本地数据，避免旧挂机恢复、旧网关覆盖、过期登录缓存干扰新包。

## 行为

- 触发条件：`versionCode` 与 SharedPreferences `pixijs_app.last_version_code` 不一致（含首次安装记档）。
- 时机：`MainActivity.onCreate` 里、`super.onCreate`（Bridge/WebView 启动）**之前**。
- 清理：删除 `app_webview` / `webview` 目录，以及 `cache` / `code_cache`（含 localStorage、Cookie、WebView 缓存）。
- 不清理：电池优化提示标记（`pixijs_keepalive`），避免每次升级都弹系统对话框。
- 同版本重复打开：不清理。

## 注意

- 覆盖安装会清掉记住的账号密码与 `pix_hangup_resume`；需重新登录（符合「清旧数据」）。
- 以后每次要让覆盖安装再清一次，必须抬高 `android/app/build.gradle` 的 `versionCode`（并同步 `versionName` / `package.json`）。
- 卸装重装本身就会清应用数据，无需本逻辑。

## 验证

1. 安装旧包登录并开挂机（或写 `pix_gateway`）→ 覆盖安装新 `versionCode` → 冷启动无挂机自动恢复、登录框为空、默认网关生效。
2. 同版本再开 App：账号若本次已保存应仍在。
