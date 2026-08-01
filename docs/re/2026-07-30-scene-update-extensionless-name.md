# WT18/7 无后缀场景名下载失败（2026-07-30）

## 症状

进入 `29梦境空间_01`（梦境使者副本）后加载卡住。服务端：

```text
mock_update_chunk_missing subtype=7 file=29梦境空间_01 version=1
```

磁盘上已有 `JHOnlineData/29梦境空间_01.sce`（及 `_03.map`、`FB_lotus2.actor` 等）。

## 根因

1. 客户端场景键 / WT18/7 `name` 为 **无扩展名** `29梦境空间_01`。
2. `open_server_scene_resource` 进图校验会重试 `.sce`，故服务端认为场景存在。
3. `load_named_update_payload` 只按**精确叶子名**读文件，找不到无后缀叶子 → `response=0`。

首次偏离在更新通道的名字解析，不是 `.sce` 内容缺失。

## 修改

`mock_server_core.c`：

1. 精确名读失败且请求名无 `.` 时，依次尝试 `.sce` / `.map` / `.actor` / `.gif`。
2. `clientmiss` 发布校验同样认 `name` 与 `name.sce` 等已发布项。
3. 解析成功打 `mock_update_named_resolve request=... resolved=...`。

响应里的 `name` 仍回客户端请求名（无后缀），不改 wire 契约。

## 验证

1. 停旧进程，`make -j2 server`，重启。
2. 再进梦境：应见 `mock_update_named_resolve` + `mock_update_chunk subtype=7 file=29梦境空间_01`，无 `chunk_missing`。
3. 若后续再 miss `.map` / `.actor`，同逻辑应能命中带后缀文件；`clientmiss` 仍要求对应叶子（或带后缀同名项）已发布。
