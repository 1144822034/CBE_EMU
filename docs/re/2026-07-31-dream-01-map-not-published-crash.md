# 梦境进图：`29*_01.map` not-published → ScreenInit 崩（2026-07-31）

## 触发

选角后直接进上次场景 `29梦境空间_01`（本次未再走临安→梦境传送）。

## 首次偏离（非崩溃 PC）

服务端：

```
mock_update_client_miss_reject file=29梦境空间_01.map reason=not-published
mock_update_chunk_missing … file=29梦境空间_01.map
mock_update_client_miss_reject file=.gif reason=not-published
```

客户端：

```
resource_cache_version_check file=29梦境空间_01.map local_version=3527 exists=1
resource_cache_download_failed … received=0
resource_cache_version_check file=.gif …
stream_data_result … result=00000000（大量）
地址无法访问 … pc:103661e … SCR_Init异常
```

崩溃 PC `0x0103661e` 是资源流失败后的 ScreenInit 症状；根因在更早的 WT18/7 拒包。

## 契约

- 磁盘权威 walkable map 是 SCE2 头里的 **`29梦境空间_03.map`**（`_01`/`_02`/`_03` 共用）。
- 服务端 `publish_scene_resource` 只发布场景叶 + 内嵌 `_03.map`，**从不**发布 `29*_01.map`（`web/fs` 也无此文件）。
- 客户端 `bin/JHOnlineData/` 存在孤儿 **`29梦境空间_01.map`**（与 `_03.map` 同 MD5），属错误本地叶。

## 根因

`vm_file_try_resolve_map_path`：对无后缀 `JHOnlineData/29梦境空间_01`，在非 `c*`/`b*` 时若存在同名 `.map` 就改写为 `.map`。

于是打开场景键变成打开孤儿 `_01.map` → 版本探测 → `not-published` → 下载空包 → 后续空名 `.gif` / `stream_data` 失败 → ScreenInit 崩。

## 修复

1. Host：扩展名解析优先 **已存在的无后缀 SCE**，再 `.sce`，最后才 `.map`（`src/vmFunc.c` + Android `cbeEmu/vmFunc.c`）。
2. 本地可删孤儿 `bin/JHOnlineData/29梦境空间_01.map`（可选；修解析后不再被场景键劫持）。

## 验证

- 重编桌面客户端后进梦境：应先 version-check **无后缀** `29梦境空间_01` 或 `_03.map`，不再出现 `_01.map not-published`。
- 不应再刷 `file=.gif`。
