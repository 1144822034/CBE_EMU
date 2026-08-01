# 具名资源本地版本对比（2026-07-31）

## 问题

后台 `publish` 只登记 catalog，WT18/7 按需拉取。客户端本地已有同名 SCE/Actor/GIF 时不会再请求，看起来像「publish 了但客户端没收到」。

## 做法

内容版本算法与 `vm_net_mock_update_named_content_version` 相同：FNV-1a(name + 文件字节) → `u16`。

1. **模拟器打开只读 `JHOnlineData/` 资源时**（`.actor`/`.gif`/`.sce`/`.map`/无后缀场景键）：
   - 计算本地 content-version
   - 发 WT18/7 `clientmiss=1` + `version=local`
2. **服务端版本权威**（`mock_server_core.c`）：
   - 已 publish（catalog enabled）：以 `catalog.version` 为准
   - **未登记名 = 磁盘权威**：`JHOnlineData` 上存在该文件时，用
     `vm_net_mock_update_named_content_version` 与 `request.version` 对比；
     **不得**仅因未进 ledger 就 `not-published`（户外存量图不必 publish）
   - 版本相等 → `result=1 totalsize=0`（`mock_update_chunk_uptodate`），保留缓存
   - 仅当 catalog 未登记 **且** 磁盘也读不到时才 `not-published`
3. 版本不一致或本地缺失 → 正常下发分片并覆盖本地文件

## 日志

- 客户端：`resource_cache_version_check` / `resource_cache_uptodate` / `resource_cache_miss_download_begin reason=version-stale|cache-miss`
- 服务端：`mock_update_chunk_uptodate` 或 `mock_update_chunk subtype=7`

## 验证

1. `make -j2` 编服务端与桌面客户端；Android 需重编 `JianghuOL` NDK（`cbeEmu/main.c` + `vmFunc.c` 已同步）  
2. 后台保存梦境刷怪点（publish SCE + Actor）  
3. **不必手动删缓存**；再进梦境  
4. 应见 version-stale 下载或 uptodate；地图为新 SCE  

## Android

JNI 副本路径：

- `JianghuOL/app/src/main/jni/src/cbeEmu/main.c`
- `JianghuOL/app/src/main/jni/src/cbeEmu/vmFunc.c`

与桌面 `src/main.c` / `src/vmFunc.c` 同源逻辑；改资源下载后必须两边一起改。

## 回归（2026-07-31）

1. 曾误把无后缀叶子一律当成可 publish 资源，导致 `mmorpgTempdata` 等刷 WT18/7。已拒绝 `mmorpg*`，无后缀仅中文/`*_NN`。
2. **商城退出卡住**：对已存在的每个 `.actor`/`.gif` 同步做版本探测，mmGame 重建时上百次 `not-published` reject 堵死主线程。现改为：
   - 已存在文件：仅对 `.sce`/`.map`/无后缀场景，以及 `e_*.actor`/`e_*.gif`（地图战斗 Actor）做版本对比
   - UI/角色外观等 `.actor`/`.gif`：仅在**本地缺失**时走原 clientmiss 下载
3. **进场景闪退 / SCE 装不上（Windows）**：
   - 版本陈旧下载写到 `*.cbe-download.tmp` 后 `rename(tmp, target)`，目标已存在时 Win32 直接失败；且探测时原 `FILE*` 仍打开，无法 `remove` 覆盖。
   - 证据：服务端已 `mock_update_chunk_complete`/`client-install-callback`，但客户端仍按旧 SCE 打开 `e_ghostfireB`，并反复整包重下同一 `550` 字节场景。
   - 修复：只读打开前先 `fclose`；安装时先 `remove(target)` 再 `rename`；失败打 `resource_cache_install_replace_failed`。
4. **户外图 not-published（梦境 publish 后）**：见
   [`2026-07-31-outdoor-clientmiss-disk-authority.md`](2026-07-31-outdoor-clientmiss-disk-authority.md)。
   未登记名改回磁盘权威，户外存量图不必进 catalog。
