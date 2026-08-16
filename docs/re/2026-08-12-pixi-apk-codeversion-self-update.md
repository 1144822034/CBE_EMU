# APK 登录 codeVersion 自更新（2026-08-12）

## 目标

登录被服端 codeVersion 门闩拒绝（`information`=「请更新客户端」）时，Android APK 弹出确认框；确认后从固定地址下载并调起系统安装器。

## 固定下载地址

```
https://gitee.com/liyuhailianxiying/jianghu-ol/releases/download/v1/jh_ol.apk
```

发新版时覆盖同一 Gitee Release 资产 `jh_ol.apk`（本机出包路径：`PixiJS_web/android/app/release/jh_ol.apk`；并提高包内 `client_code_version` / 服端 `server_code_version`）。

## 流程

1. WT `1/1/12` `result=2` + tip「请更新客户端」→ `doLogin` catch
2. `isClientUpdateRequiredError` 匹配文案
3. `window.confirm` 询问是否更新
4. Capacitor `ApkUpdater.downloadAndInstall`：HTTP 下载到 `cache/apk_updates/jh_ol.apk`，进度经 `downloadProgress` 回写状态栏
5. Android 8+ 若未授权「安装未知应用」→ 跳转设置；返回后 `FileProvider` + `ACTION_VIEW` 安装

## 修改点

| 路径 | 作用 |
|------|------|
| `ApkUpdaterPlugin.java` | 下载 + 安装 |
| `src/net/apkUpdate.ts` | 检测 tip、确认、调插件 |
| `main.ts` `doLogin` catch | 触发更新 |
| Manifest `REQUEST_INSTALL_PACKAGES` | 安装权限 |
| `file_paths.xml` `apk_updates` | FileProvider |

网页 / Electron 不弹原生更新（仅 `Capacitor.isNativePlatform()`）。
