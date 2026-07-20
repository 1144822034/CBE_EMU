# 2026-07-20 小喇叭世界发言与历史同步

## 问题与运行证据

客户端使用 `item.dsh` 的 `807 小喇叭` 发送世界消息时，不会只提交聊天对象。真实请求是一个 4 对象 WT 组合包：

```text
1/3/1  {type=0,data=<message>}
1/2/10 {Type=101}
1/7/1  {type=1,id=807,num=1}
1/2/10 {Type=101}
```

6 字节正文时完整请求为 102 字节，各对象 payload 长度依次是
`25,10,33,10`。旧的 split-safe 处理器先执行了聊天对象，随后因
`7/1` 不在允许集合而放弃整个组合包；末尾再次探测时又执行一次聊天，
最终记录两条 `mock_chat_send` 后进入 `unhandled wt=3/1` 断言。客户端因此
既收不到完整响应，也不能完成小喇叭扣除。

## 客户端证据

- `江湖OL.CBE:SendChatDataEvent(0x0103516C)` 构造
  `1/3/1 {type,data}`；世界请求 type 为 0。
- `江湖OL.CBE:DispatchSceneTransition(0x01015FAC)` 在发送聊天后，若输入
  来自物品模式，会调用背包操作并设置 `R9+38036` 的待处理状态。这解释了
  同一次 flush 中出现 `3/1` 与 `7/1`。
- `江湖OL.CBE:HandleItemOperationResponse(0x01033544)` 消费下行 `7/1`，
  用 `result,type,id` 完成客户端背包扣除；`7/11` 再按 seq 刷新最终数量。
- `item.dsh` 行 807 的说明是“可以向全服务器发布消息，每组10个”，类别为
  14。其 `7/1 num=1` 是使用数量，不是 HP 效果。
- 世界消息下行使用客户端原生的 `1/3/3 type=0`。解析器
  `net_handle_type_payload_detail(0x010126C6)` 在进入频道 switch 前，会先把
  每行及外层 type 原样加入总览列表；随后
  `BuildChatChannelStr(0x01034D84)` 为 type 0 生成 `[世]`，
  `RenderColoredTextLines(0x01034DF6)` 使用 RGB `0xFFDE00`。旧 type 6
  对应 `[系]` 和绿色 `0x29FF2E`。

IDA 实例通过 `list_instances` 按 `binary_name=江湖OL.CBE` 选择，本轮使用的
实例 id 只作为会话内工具参数，没有写入代码或文档协议常量。

## 服务端实现

### 组合包和道具扣除

- split-safe 处理改为两遍：第一遍验证完整对象集合，第二遍才调用可能修改
  状态的单对象 builder，避免“处理一半后放弃”。
- `7/1` 加入可拆分集合，使真实的
  `3/1 + 2/10 + 7/1 + 2/10` 返回一个完整组合响应。
- item 807 仍复用普通物品的服务端扣减逻辑并只扣 1 个；显式清除由
  `num=1` 产生的通用 HP fallback，防止小喇叭被误当作 `HP+1` 药品。
- 世界消息成功写库后，在同一个组合响应内给发送者回送 type 0 的
  `1/3/3`。实际单客户端日志曾显示 `recipients=0 send_to=0`，证明不能依赖
  其他在线 session 或普通聊天的本地插入；其余在线账号仍通过场景轮询 FIFO
  接收，不会把发送者重复放进队列。
- item 807 不返回普通道具的 `7/1 result=1`：客户端
  `HandleItemOperationResponse(0x01033544)` 会由该分支调用
  `ui_show_message_box("使用成功",...,10)`，聊天屏幕没有运行场景 toast 倒计时，
  因而形成无法消失的条状提示。小喇叭改用 `7/7 + 7/11` 完成行移除、数量刷新
  及 `R9+38036` 等待标志清除；其他道具仍保留 `7/1` 成功响应。

### MySQL 世界消息表

新增 `world_chat_messages`：

```sql
CREATE TABLE IF NOT EXISTS world_chat_messages (
  message_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  source_account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  source_role_id INT UNSIGNED NOT NULL,
  source_name VARBINARY(15) NOT NULL,
  message VARBINARY(81) NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (message_id),
  KEY idx_world_chat_source (source_account_id, source_role_id)
) ENGINE=InnoDB;
```

建表语句同时存在于运行时 lazy schema ensure 和
`server/mysql/schema.sql`。世界消息必须先成功 INSERT 才会广播；写库失败时
发送者收到 type 5 的“世界消息发送失败”，不会产生只广播不落库的记录。

### 登录历史

- session 首次进入可见场景时查询 `message_id DESC LIMIT 30`，再按
  `message_id ASC` 入队，所以客户端看到的是最近 30 条、旧到新的顺序。
- 历史项保留持久角色 id、发送者 GBK 名字和正文，通过现有
  `1/3/3 type=0` 场景轮询分批下发。
- 每个登录生命周期只加载一次；返回标题重新登录时 offline reset 会重新
  允许加载。
- chat FIFO 从 16 提升到 64，能同时容纳 30 条历史、欢迎消息和登录期间的
  实时消息；单次轮询仍最多发送 4 条，并受主回调 10 对象上限约束。

## 验证

本地服务：`127.0.0.1:19090`。

`tmp/world-chat-regression.php` 使用与真实日志相同的 4 对象签名完成三账号
回归：

```text
world chat regression passed combo_objects=5 wire_type=0 horn_remaining=1
live_stored=1 live_delivered=1 history=30 order=oldest-to-newest
```

关键服务日志：

```text
world_chat_store ... storage=mysql
mock_chat_send ... delivery_type=world ack=server-echo sender_echo=1 ... recipients=1
mock_item_use item=807 ... consumed=1 ... refresh=7/7+7/11-small-horn-no-popup
net_send ... source=builtin-chat-message-combo resp=215
world_chat_history_queue ... queued=30 skipped=0 limit=30
```

- `make -j2`：通过。
- PHP fixture 语法检查：通过。
- 服务 stderr：0 字节。
- 回归测试数据已清理，`world_chat_messages` 当前没有残留的 Codex 测试行。
