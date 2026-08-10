# WT18/7 clientmiss：磁盘有资源仍 not-published（2026-08-05）

## 触发与证据

`dlog.txt`：`web/fs/JHOnlineData` 已有 `11终南山_01.sce/.map`、`c04临安府_06.sce` 等，但刷：

```text
mock_update_client_miss_reject file=… reason=not-published
mock_update_chunk_missing …
```

同文件另有成功 `mock_update_chunk` / `uptodate`（`path=../web/fs/JHOnlineData/…`）。  
reject / 成功两行文件名十六进制不同：失败侧经日志转换后**不是**正常 GBK「终南山_01」字节。

## 根因

1. WT18/7 `name` 契约为 **GBK**；部分客户端（常见 Android）写入 **UTF-8**。  
   `fopen` 按 GBK/再误做 `gbk→utf8` 打不开已存在的叶 → 旧日志误标 `not-published`。  
2. 无后缀名（如 `b_11终南山`）在补 `.sce/.map` **之前**做 `file_size`，裸名不存在即拒。

## 修改

`mock_server_core.c` 更新通道：

- UTF-8 wire 名 round-trip 校验后转为 GBK 再打开；  
- 无后缀依次试 `.sce` / `.map` / `.actor` / `.gif`；  
- content-version 对**解析后的磁盘叶**哈希，UTF-8/GBK 客户端共用版本；  
- 拒因改为 `disk-open-failed`（真缺文件），不再冒充未发布台账。

## 回归（2026-08-05 过图卡死）

修复打开后，`dlog` 出现同一 `17无情谷_01.sce` **反复全量 chunk**（同 client 数十次、`version` 始终 38279、几乎无 uptodate）。

原因：content-version 曾对 **GBK 解析叶名** 哈希，而客户端 version probe 用的是 **线上 UTF-8 name** → 永远不相等 → 每次进图/传送都重下资源，加载条卡住。

纠正：仍用解析叶 **打开文件**，version 哈希改回 **wire name**（与客户端一致）。首次下载后应走 `uptodate`。
