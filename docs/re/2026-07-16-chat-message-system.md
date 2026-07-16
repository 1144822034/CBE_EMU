# 江湖 OL 消息系统协议证据

## 目标

实现客户端原生网络路径下的世界、本地、队伍、私聊和系统消息。消息必须由服务端响应包驱动，不写客户端全局变量，也不直接调用客户端界面函数。

## 请求证据

### 普通频道

- `JianghuOL.CBE:SendChatDataEvent(0x0103516C)` 调用 `alloc_outgoing_game_event(5, 0, 3, 1)`，生成 `1/3/1` 对象。
- 字段为 `type:u8` 和 `data:string`。
- `StartSceneTransition(0x0101621C)` 保存聊天目标和发送模式，`DispatchSceneTransition(0x01015FAC)` 取输入文本后调用 `SendChatDataEvent`。
- `0x01016350` 开始的 GBK 标签依次是 `(世界)`、`(队伍)`、`(帮派)`、`(本地)`、`(私人)`，对应普通请求 type `0/2/3/4` 和独立私聊模式。

普通频道请求映射：

| 客户端输入 | 请求 | 服务端下行 type |
|---|---|---:|
| 世界 | `1/3/1 { type=0, data }` | 6 |
| 队伍 | `1/3/1 { type=2, data }` | 2 |
| 帮派 | `1/3/1 { type=3, data }` | 3 |
| 本地 | `1/3/1 { type=4, data }` | 4 |

世界请求使用特殊值 0，下行改为 6；队伍、帮派和本地在请求与下行中均保持 2、3、4。

### 私聊

- `SendChatDataEvent(0x0103516C)` 的模式 2 生成 `1/3/2`。
- 字段为 `sendTo:u32` 和 `data:string`。
- `DispatchSceneTransition(0x01015FAC)` 在发送后立即调用 type 7 的本地消息回调，所以成功私聊下行统一使用 type 7。

### 运行证据

旧服务日志捕获到：

```text
unhandled wt=3/1 len=48 first=1/3/1:24,1/2/10:10
```

这说明聊天发送可能和场景 `2/10` 刷新对象在同一 WT 请求中提交。聊天对象因此被加入现有 split-safe 组合处理，两个子请求分别构造响应后重新合并。

## 下行解析器

- `net_business_response_dispatch(0x01012E4C)` 在 event 7 下把 kind 3 交给 `net_handle_type_response(0x01012938)`。
- `net_handle_type_response` 只在 subtype 3 读取 `type`，然后调用 `net_handle_type_payload_detail(0x010126C6)`。
- 下行对象是 `1/3/3 { type:u8, chatinfo:blob }`。
- `DispatchSceneTransition(0x01015FAC)` 是发送端本地回显的关键证据：队伍、帮派、本地、私聊分别以 type 2、3、4、7 调用对应消息管理器。成功发送已经在请求发出后进入本机列表，服务端不能再次回显同一行。
- `chatinfo` 每行依次读取：
  1. 首个 tagged-u8：行数；
  2. len16 字符串：发送者名字；
  3. tagged-u32：发送者角色/场景 actor id；
  4. len16 字符串：消息正文。

`stream_reader_init_from_blob(0x01033B16)` 把 `+0x28` 安装为 `stream_read_i8_tagged`、`+0x1C` 安装为 `stream_read_cstr_len16`、`+0x20` 安装为 `stream_read_i32_be_tagged`、`+0x2C` 安装为非推进的 `stream_peek_i16_be`。

`vm_net_mock_put_object_blob` 的内层 len16 会成为首个 tagged reader 跳过的两字节，因此 `chatinfo` 自身以一个 raw `count` 字节开头；不能再额外写 `00 01`，否则客户端会把 tag 的第一个字节误当消息条数。

下行 type 分派：

| type | 客户端分支 |
|---:|---|
| 2 | 队伍 |
| 3 | 帮派 |
| 4 | 本地/附近 |
| 5 | 系统 |
| 6 | 世界 |
| 7 | 私聊 |
| 8 | 与 type 2 共用队伍消息管理器；本轮五类消息不使用 |

### 运行反证与修正

2026-07-16 双客户端截图和正式服务日志形成了可重复对照：

- 欢迎消息由服务端以旧 type 4 下发，客户端显示前缀 `[本]`；证明 type 4 是本地，不是系统。
- 本地请求 `1/3/1 type=4` 被旧实现映射成下行 type 5，接收端显示前缀 `[系]`；证明 type 5 是系统，不是本地。
- 发送端同时出现客户端本地插入行和服务端 `1/3/3` 回显行，前缀不同；证明成功请求的服务端响应应是空 ACK，而不是把消息再次回给发送端。

这组运行证据否定了旧映射 `世2/系4/本5/队6/私8`，最终映射为 `队2/帮3/本4/系5/世6/私7`。

客户端临时发送者名字缓冲为 16 字节，消息缓冲为 82 字节。服务端分别限制为 15 和 81 个有效字节，避免解析器无界 `mem_copy` 覆盖栈。

## 服务端实现

- 成功发送只返回 5 字节空 WT ACK；客户端已由 `DispatchSceneTransition` 完成本地插入，不再产生重复行。
- 未组队、私聊目标离线和帮派暂未开放时，响应仍携带一个 type 5 系统消息对象。
- 每个在线 session 有 16 条独立 FIFO，最多每次场景轮询下发 4 条，避免阻塞好友/交易/组队通知。
- 世界消息发送给所有在线 session。
- 本地消息只发送给相同可见场景的 session。
- 队伍消息只发送给当前 service-local 队伍成员；未组队时返回系统提示。
- 私聊按在线持久角色 id、兼容 wire id 和附近 actor id 解析目标；目标离线时返回系统提示。
- 系统消息只下行。角色首次进入可见场景时排队一条 GBK 欢迎消息，以验证 type 4 的原生显示路径。
- 帮派频道不属于本次功能范围，当前返回“帮派消息暂未开放”的系统提示，不把消息错误广播到世界频道。

关键日志：

```text
mock_chat_send ... delivery_type=world|local|team|private ack=empty|system-error ...
chat_notice_queue ...
chat_notice_deliver ...
scene_sync_poll ... chat=N ...
```

## 验证

- `make -j2`：通过。
- 正式服务已使用新构建重启，命令行为 `bin/main.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19090`，并确认由该进程监听 `127.0.0.1:19090`。
- 重启时没有存活的游戏客户端进程，因此还需在两个客户端重新登录后，分别验证世界、本地、队伍和私聊的发送端回显、接收端轮询下发，以及登录后的系统欢迎消息。
