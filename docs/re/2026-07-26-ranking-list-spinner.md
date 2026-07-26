# 排行榜 `WT 23/7` 调查与修复（2026-07-26）

## 触发与现象

在已登录角色的场景中打开“排行榜”，客户端持续显示“获取数据”进度条，无法自行结束。

服务端原始日志记录了单对象请求：

```text
[error][network] unhandled wt=23/7 len=34 objects=1 first=1/23/7:25 last_source=- last_resp=0
```

请求的 34 字节结构可由客户端发送函数还原为：

```text
57 54 00 22
01 17 07 00 1E
04 74 79 70 65 00 03 00 01 <type>
09 70 61 67 65 49 6E 64 65 78 00 03 00 01 <pageIndex>
```

其中对象长度是 `0x001e`，两个字段均为 `u8`。

## 客户端证据与链路

1. `JianghuOL.CBE:0x0102ABD4` 创建 `WT 1/23/7`，写入 `type` 和 `pageIndex`。
2. `JianghuOL.CBE:0x0102B9A8`（`HandleRankingList`）只有在收到 `kind=23, subtype=7` 的结果对象后，才清除网络等待标志。
3. 同一 parser 依次读取：
   `ordernum:u8`、`orderlist:raw`、`myorder:u32`、`colnum:u8`、
   `colnames:raw`、`pagemax:u32`、`count:u32`、`topplayerinfo:raw`。
4. `orderlist` 的每行是 `u8 type + len16-string`；`colnames` 是
   `len16-string` 序列；`topplayerinfo` 每行是 `u32 rank + 两个
   len16-string`（仅当 `colnum==4` 时再多读一个字符串）。

此前 `mock_server_dispatch.c` 没有 `23/7` handler，最终进入 server-only
未处理分支并返回零长度响应。因此首次偏离并非 UI 进度条，而是服务端违反了
该请求必须返回同 subtype 排行榜表的协议契约。

## 修复

新增 `src/server/mock_server_ranking.c`：

- 严格识别单对象 `WT 1/23/7 {type,pageIndex}`；
- 从 MySQL `account_roles` 读取全局角色，而非当前会话的角色缓存；
- 提供等级、经验、财富三类排序，均按稳定的 `role_id` 处理同分；
- 按客户端可消费的十行分页，返回全局 `myorder` 和零基的 `pagemax`；
- 以 GBK 文本下发榜单/列标题；每个客户端单元格严格限制在 parser 分配的
  30 字节内；
- 将该只读请求列入显式复合请求白名单，以便它与其他独立请求同包到达时仍能
  分别得到响应。

未使用空排行榜包、伪造成功或客户端进度条抑制；MySQL 查询失败会留下明确的
`mock_ranking_page_query_failed` 取证日志。

## 验证计划

1. 打开排行榜：服务端应记录 `builtin-ranking-page` 和
   `mock_ranking_page ... response=23/7`，客户端应停止等待并显示等级榜。
2. 切换三个榜单及翻页：每次请求的 `type/pageIndex` 均得到同 subtype 结果。
3. 多账号：离线角色仍参与排名；当前角色的 `myorder` 与排序字段一致。
4. 将 `23/7` 与已列入独立能力清单的请求同包发送，确认响应对象数与请求
   语义一致，不发生跨对象吞包。
