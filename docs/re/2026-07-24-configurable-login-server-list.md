# 可配置标题服务器列表（2026-07-24）

## 触发与原始偏差

登录标题页会发出 `WT 1/1/12`。旧 mock-server 的
`vm_net_mock_build_login_alt12_server_list_response`（以及主登录的同阶段
builder）总是写入一个 `serverinfo` 记录“测试一区”，并把 `servernum` 固定为
`1`。因此后台没有任何数据源能够改变客户端实际看到或随后选择的服务器。

选择列表项后，客户端发 `WT 1/1/4`，其中包含 `serverID` 与 `moneytype`。
旧 `servconf` 也没有按这个 `serverID` 取配置。

## 已确认的客户端契约

- `mmTitleMstarWqvga.cbm:net_handle_login_response(0x16DC)` 在成功的登录响应
  中读取 `serverinfo`、`color`、`servernum` 和 `newVer`；运行时已确认
  `servernum` 必须按 `u16` 写入。
- 已工作的单个 `serverinfo` 记录的字节顺序是：GBK 字符串名称、GBK 字符串状态
  标签、`u32 serverID`、`u24` 显示颜色。
- `WT 1/1/4` 的既有 handler 会读 `serverID`；选择成功后的 `servconf` 被标题
  的颜色配置读取。

## 本次数据与处理链

```
MySQL server_login_servers
       │（后台 /admin-418yz6/?tab=servers 保存后同步内存目录）
       ▼
WT 1/1/12: enabled 行按 sort_order/server_id 写入 serverinfo，servernum=u16 行数
       ▼
客户端标题服务器选择
       ▼
WT 1/1/4: 用请求 serverID 查询当前已启用行，返回该行的 servconf 颜色
```

`server_login_servers` 的字段限定为客户端已接收的名称、状态标签、ID、24 位颜色、
排序和启用状态。它不是网络端点目录：游戏连接的 CBMS 主机和端口由客户端已经建立
的连接决定，选择列表项不会切换地址。

空库会由迁移/服务启动时的 `INSERT IGNORE` 写入“江湖一区 / 推荐”初始数据。它是
数据库引导数据，而不是 response builder 的本地默认值；目录读取失败或没有启用行时
builder 返回失败并记录 `login_server_catalog_load` 错误，不伪造列表响应。

## 多行编码的证据等级与验证

单行记录顺序与 `servernum=u16` 是运行时已确认的契约。将该记录连续写入多次、并以
`servernum` 指出行数，是为了启用客户端原生列表路径所作的**有界推断**，而不是把
未确认字段塞进协议。实现限制为最多 8 行，按稳定排序输出。

需要在真实标题客户端完成以下回归，才将多行迭代视为完全确认：

1. 在后台新增一个启用的第二行，使用不同名称、ID、标签和颜色；退出到标题后重新
   发起登录，确认两行均显示且顺序正确。
2. 选择每一行，确认客户端发出的 `WT 1/1/4.serverID` 等于被选择的 ID，随后不出现
   解包错误或加载停滞。
3. 停用第二行后重新登录，确认它不再显示；尝试删除/停用最后一个启用行应被后台拒绝。

服务端侧可检查日志 `mock_login_alt12_server_list ... servernum=N`，以及
`mock_title_server_select ... server_id=N`。若客户端对多行的真实解析顺序与这里不同，
应保留原始请求/响应和标题运行日志，回到该 parser 分支取证；不得通过客户端补丁或
固定默认服务器掩盖问题。

## 修改点

- `src/server/mock_server_interaction_login.c`：MySQL 目录加载、后台保存/删除、
  `1/1/12` 列表序列化、`1/1/4` 对选中 ID 的 `servconf` 查询。
- `src/web_admin_server.c`：服务器列表后台页与动作路由。
- `server/mysql/schema.sql` 与 `server/mysql/migrate_add_login_servers.sql`：表和
  非覆盖式初始数据。

## 本地验证（2026-07-24）

- `make -j2` 成功完成，并以 `127.0.0.1:19090/19091` 重启服务；启动后的
  `login_server_catalog_load` 从 MySQL 读取 1 个启用项。
- 通过后台登录后提交 `create-login-server`，临时新增 ID `900002`、颜色
  `0x113355` 的启用服务器，HTTP 操作返回 `303`。后台页随即显示 2 项。
- `tmp/login-server-list-regression.php 900002 0x113355` 发送真实的
  `WT 1/1/12` / `WT 1/1/4`：断言响应为 `servernum=u16(2)`、两条
  `serverinfo` 顺序正确，且选中 `900002` 的 `servconf` 为 `0x113355`。
- 通过后台 `delete-login-server` 删除该临时行后，下一次 `1/1/12` 日志为
  `servernum=1`，数据库只余默认 ID `1` 行。测试使用的游戏会话已显式断开。

## 已知边界

后台在“已经收到列表、尚未点击选择”的极短窗口把该行停用/删除时，没有反向的
`1/1/4` 失败包契约证据；服务端会记录该未解析选择而不伪造成功。正常流程中，标题
只会提交它刚收到的启用项。若需要支持这种管理竞态，必须先取证客户端对选择失败的
真实 response 语义。
