# PixiJS Android dist 热更新（2026-09-20）

## 结论

可以。Capacitor 把 `dist` 打进 APK 的 `assets/public`，这层只读。冷启动在 WebView 跑游戏前向热更根地址拉清单，只下载哈希不一致的文件到应用私有目录，再由 WebView 拦截优先读这份覆盖层。拉清单或下载失败时继续用已有覆盖层 + 包内资源，不改游戏协议。

热更根地址：`http://106.54.59.92:8081`（`DistHotUpdate.BASE_URL`）。该地址必须是 **Vite `dist` 的根**，能直接打开：

- `http://106.54.59.92:8081/index.html`
- `http://106.54.59.92:8081/hot-manifest.json`
- `http://106.54.59.92:8081/assets/…`、`JHOnlineData/…`

## 清单

`npm run build` / `vite build` 结束时写 `dist/hot-manifest.json`：

```json
{ "files": { "index.html": "<sha256>", "assets/index-….js": "<sha256>" } }
```

实现：`PixiJS_web/scripts/write-hot-manifest.mjs`（清单自身不计入）。自测：`node scripts/write-hot-manifest.mjs --self-test`。

包内同一份文件在 `assets/public/hot-manifest.json`，作为「当前 APK 已经带了什么」的基线。没有这份清单的旧包会跳过下载。

## 启动顺序

1. `versionCode` 变化时删掉 `files/dist-hot`（与清 WebView 一起），新包自己的资源是基线。
2. `MainActivity.load()` 装上 WebViewClient 后立刻 `about:blank`，挡住 Capacitor 自动打开的包内页面。
3. 后台读包内清单、校验已下载文件的 sha256、GET 远程清单。
4. 远程哈希 ≠（覆盖层哈希，否则包内哈希）的路径，并行下载到 staging，校验 sha256 后才换进 `files/dist-hot/files/`。
5. 与包内重新一致、或远程已删除的覆盖层文件删掉。
6. 再 `loadUrl` 游戏。本地请求命中覆盖层则直接返回，否则仍走 APK。

界面在检查/下载期间盖一层「正在检查资源… / 正在下载资源 n/m」。任意异常原先一律提示「网络不可用」；并行下载共享 `JHOnlineData/` 时，`mkdirs` 竞态会返回 false 并被误判成网络失败（2026-09-20 已改为 `ensureDir` + 按异常类型提示）。

## 发布

先 `npm run build`（或 `npm run build:android`），再把 **整个** `PixiJS_web/dist` 发布到 `http://106.54.59.92:8081/`。只上传改过的文件也可以，但远端必须有和这次 dist 一致的 `hot-manifest.json`。

增量目录：构建结束后对照 `PixiJS_web/.hot-last-manifest.json`，把内容哈希变化的文件拷到 `PixiJS_web/dist-hot-changed/`（始终含新的 `hot-manifest.json`，另有 `_changed.txt` / `_removed.txt`；若有删除则还有 `_removed.bat`）。首次构建只建基线、不拷全量；从第二次起可直接把该目录合并上传到热更根，再在服务器根目录运行 `_removed.bat` 清掉过期文件。删除基线文件可重新建基线。

已装的新包下次冷启动会补差量。和包内完全一致时只请求清单，不下载文件。

## PC（Electron）独立实现

Windows 便携包用同一热更根地址与同一份 `hot-manifest.json`，逻辑在 `PixiJS_web/electron/dist-hot.cjs`，**不共用也不修改** Android 的 `DistHotUpdate.java`。开发态 `npm run electron` 关闭热更。详见 [2026-08-11-pixijs-electron-portrait-exe.md](2026-08-11-pixijs-electron-portrait-exe.md)。
