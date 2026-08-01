# 户外地图 clientmiss 磁盘权威（2026-07-31）

## 触发

梦境 / 副本相关 publish 收紧 ledger 后，从梦境用地图石进户外图（如 `01桃花岛_01.sce`）时大量：

```text
mock_update_client_miss_reject file=01桃花岛_01.sce reason=not-published
… 01.map / b_01* / e_mucusP.* / e_ghostfireR.* …
```

桃花岛等户外图此前可进、客户端本地已有缓存，**不必**被强制加入 `server_update_catalog.tsv`。

## 预期 vs 实际

- **预期**：未改过的户外资源走磁盘权威；本地 version 与服务器文件 content-version 一致则 `uptodate`，不一致才分片。
- **实际**：`clientmiss` 在 catalog 未登记时一律 reject，梦境 publish 子集把户外探测路径拦死。

## 根因

[`2026-07-31-named-resource-version-compare.md`](2026-07-31-named-resource-version-compare.md) 把门禁写成「必须 catalog enabled」。与「后台未 publish 的存量资源仍可按磁盘字节服务」冲突。

## 修改

[`src/server/mock_server_core.c`](../../src/server/mock_server_core.c)：

1. `load_resource_update_payload`：clientmiss 仅在 **磁盘也读不到** 时 `not-published`。
2. `uptodate`：catalog 未命中时用磁盘 content-version 比较。
3. `update_response_version`：未 publish 时回落磁盘 content-version，便于陈旧缓存覆盖后写入正确版本。

不在地图石进图路径自动 `publish_scene_resource`，不扩大 ledger。

## 验证

1. `make -j2`，重启 mock。
2. 梦境 → 地图石 → 桃花岛：应见 `mock_update_chunk_uptodate file=01桃花岛_01…`（或正常 chunk），无 `not-published`。
3. 梦境自定义 SCE 仍可经 catalog 版本差触发下载。
4. Catalog 行数不因进桃花岛暴增。

## 仍未知风险

若客户端本地文件与服务器磁盘同名但字节不同且 content-version 算法未覆盖某种路径解析差异，可能误判 uptodate；需用具体 `path=` 日志对照。
