# PixiJS 场景资源版本对比（2026-08-08）

## 问题

PixiJS_web 用内存 `Map` 缓存 `/JHOnlineData/*` 字节。后台或磁盘更新副本 `.sce`/`.map` 后，同会话再进图仍用旧缓存。

CBE 通过 WT18/7 `clientmiss` + FNV content-version 对比；Pixi 不走该下载协议，资源由 Vite 直读 `JHOnlineData`。

## 做法

1. **Vite**（`configureServer` / `configurePreviewServer`）对资源响应加：
   - `X-JH-Resource-Stamp: {mtimeMs}-{size}`
   - `ETag: W/"{stamp}"`
   - 正确处理 `HEAD`；`If-None-Match` 命中时 `304`
2. **`loader.ts`**：缓存条目带 `stamp`；对 `.sce`/`.map`/无后缀场景键/`e_*.actor|gif`（对齐 CBE `should_version_check_existing`）默认 `HEAD` 对比，stamp 变则重新 GET。
3. **`loadSceneMap`**：进图强制 `revalidate: true`（含 map 贴图 gif）。
4. **`loadActor`**：version-check 资源在 stamp 变化时丢弃解析缓存。

日志：`[resource] version-stale …` / `[actor] version-stale …`。

## 非目标

未实现完整 WT18/7 分片下载；权威仍是 Vite 当前磁盘文件。无 stamp 头的静态托管保持旧「只缓存一次」行为。

## 验证

1. `npm run dev`，进某副本一次。
2. 改 `web/fs/JHOnlineData` 对应 `.sce`（或后台 publish 写入同目录）。
3. 不刷新页面，再进该副本。
4. 控制台应见 `version-stale`，地图为新内容。
