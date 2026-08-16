# PixiJS：经 WT18/7 拉取副本 .sce（2026-08-16）

## 问题

Pixi 原先只从 Vite/`public/JHOnlineData`（APK/Electron 为包内静态）读 `.sce`，
**不走** CBE 的 WT `18/7` clientmiss。后台 publish / 服务端权威 `web/fs` 副本
SCE（刷怪点等）进图时客户端仍用旧打包字节，表现为「没有从服务端获取副本资源」。

## 契约

- 登录后 `GameSession` 把 live `exchange` 交给 `setResourceExchange`。
- 进图加载 `.sce`/`.map`（及 extensionless 场景键、`e_*.actor|gif`）时：
  1. 先读本地/包内字节（可缺失）
  2. 用本地 content-version（FNV，与 mock `update_named_content_version` 一致）
     发 `18/7` `{name,type=0,start,version,clientmiss=1}`
  3. `totalsize=0` → 沿用本地；否则按 0x1000 分片拼装并写入内存缓存
- Wire `name`：**保留** `.sce` / `.map` 后缀，避免两者争用同一叶名。

## 2026-08-16 进副本黑屏

### 根因

`resourceWireName` 曾把 `29*_01.sce` 与 `29*_01.map` 都剥成 `29*_01`，且
`cachePut` 把安装结果别名到 stem。后拉的 MAP 字节盖住 SCE（或装上无后缀
stock 小文件）→ 解出的地图宽高/贴图全空 → 黑屏。

### 修复

- Wire/cache 按完整叶名（含后缀）隔离
- 安装前 `unwrapResource` 校验，失败则保留本地包

## 验证

1. 连 live gateway 登录，进梦境副本；地图应正常，非黑屏。
2. 控制台 `[resource] wt18/7 install …sce` / `…map` 叶名带后缀。
3. 改后台 SCE 再进应重新 install。
