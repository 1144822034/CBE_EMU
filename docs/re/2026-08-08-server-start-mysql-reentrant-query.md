# 服务端启动失败：MySQL 结果回调重入取证

## 触发步骤

在项目根目录执行与 `bin/multiplayer/start-server.bat` 相同的命令：

```text
bin/jh-online-server.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19090
```

服务端在 MySQL 权威校验后退出，尾部错误为：

```text
shop_item_db_load failed error=Malformed MySQL result-set header
task_catalog_db_load failed error=MySQL socket receive failed
xse/task resource validation failed
```

## 第一处偏离

MySQL `server_task_reward_items` 查询正在向
`vm_net_mock_task_reward_catalog_db_row()` 交付一行时，该回调通过
`vm_net_mock_find_shop_catalog_item()` 触发了冷启动的商城目录加载。
商城目录又调用 `vm_net_mock_shop_admin_db_load()` 并在**同一条尚未读完结果集的
连接**上发送新的 `CREATE/SELECT`。

启用 `CBE_MYSQL_PROTOCOL_TRACE=1` 后，证据为：

```text
protocol_query phase=begin id=15 operation=SELECT       # 任务奖励查询
mock_shop_catalog ...                                   # 回调内懒加载
protocol_query phase=begin id=16 operation=CREATE       # 嵌套查询
protocol_packet phase=result-header seq=9 len=5 bytes=fe00000200000000
```

`FE 00 00 02 00` 是前一个结果集的 EOF 包，而不是 `CREATE` 的结果头。此前读取器将
它报为“Malformed result-set header”；更晚的任务查询继续读同一受污染流，因此得到
socket receive failed。MySQL 5.7 本身可通过 PHP mysqli 正常执行相同的商品和任务查询，
故不是表、账号、端口或 SQL 方言问题。

## 修复

1. `vm_net_mock_task_catalog_apply_db()` 在开始任务/任务奖励 SQL 前预加载商城目录，使
   回调只做内存查找。
2. `mysql-client.c` 追踪“正在执行结果回调”的连接状态，拒绝任何嵌套
   `vm_mysql_query/vm_mysql_exec`，防止未来出现同类协议流交错。
3. 结果头、列终止符或行无法解析时关闭该连接，禁止把未知的剩余包交给下一条查询。
4. `CBE_MYSQL_PROTOCOL_TRACE=1` 仅在需要时输出查询序号、操作类型和异常包的固定八字节
   前缀；默认无日志，不输出 SQL 参数或行数据。

## 验证

`make -j2` 成功后，用服务端自有、临时 PID 启动同一参数：

```text
mock-admin shop_item_db_load rows=1500 skipped=0
mock-admin task_catalog_db_load rows=17 overridden=15 custom=2 skipped=1 reward_rows=1
mock-service listening=127.0.0.1:19090
mock-admin listening=http://0.0.0.0:19091/
```

该临时进程确认监听后已由测试自身停止；没有终止任何用户进程。
