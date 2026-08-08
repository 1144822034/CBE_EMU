# MySQL 账号缓存生命周期（2026-08-08）

## 触发与原始行为

服务启动阶段在 MySQL authority 已完成封存后，仍会执行：

```sql
SELECT account_id, HEX(password_value) FROM accounts ORDER BY account_id
```

并把全部账号和口令保留在 `g_vm_mock_service_account_db`。后台账号列表也以
这个进程快照为目录来源。这使账号数量增长时，启动时间和常驻内存都随历史账号
总数增长，而非随在线人数增长。

## 预期契约

- MySQL `accounts` 是唯一账号权威来源。
- 账号口令缓存的拥有者是已认证的游戏 transport session，不是后台页面、用户中心
  cookie，也不是服务器进程本身。
- 登录绑定会精确查询该账号并把它加入在线缓存；显式断开、心跳下线、登录换号和
  账号接管都走同一个离线转换并移除缓存。
- 后台账号列表保持分页与搜索，但必须直接分页查询 MySQL，不能为显示列表重新
  装入完整账号目录。
- `vm_mock_service_account_state` 同样只是在线账号的运行态；离线后在已完成原有
  持久化捕获的前提下释放。角色、背包、装备等权威数据仍由既有的 MySQL 写入器
  逐项提交，不依赖该运行态常驻。

## 首个偏离与根因

首个偏离位于 `vm_net_mock_service_run_forever` 的启动调用链：无条件调用
`vm_mock_service_account_db_load()`，而该函数直接扫描整个 `accounts` 表。随后
`web_admin_server.c` 的账号列表以该全量缓存分页，使“后台需要列表”错误地成为
“服务必须始终加载所有账号”的隐含契约。

这不是 CBE 客户端协议问题；登录请求、响应对象、事件类型和 parser 时序均不需要
改变。问题的拥有层是服务端 MySQL 目录与会话生命周期。

## 修复

1. 正常 `vm_mock_service_account_db_load()` 仅初始化空的运行时缓存。
2. 新增精确账号查询；认证、用户中心 cookie 校验、改密和后台角色管理都直接查询
   MySQL，而不污染在线缓存。
3. `vm_mock_service_bind_session_account()` 在成功绑定后获取该账号缓存；
   `vm_mock_service_session_mark_offline()` 释放账号缓存及无在线 session 的运行态。
4. 后台账号列表以 `ORDER BY account_id LIMIT offset,count` 查询，额外读取一行决定
   “加载更多”状态；账号总数用 `COUNT(*)` 查询。
5. 未封存的旧数据迁移保留唯一例外：为把历史角色数据迁入 MySQL，启动时可临时读取
   全部账号并逐账号迁移。authority marker 成功封存后立即清空该临时目录；已封存的
   正常启动不会扫描账号表。

## 可观察证据与验证

- 正常启动应出现 `mock_account_cache_init entries=0 source=runtime-lifecycle`，不应出现
  全表账号载入日志。
- 登录/退出分别出现 `account_cache_acquire` / `account_cache_release`，条目数随在线账号
  增减。
- 管理页面和用户中心只产生精确或分页 SQL 查询，不增加在线缓存条目。
- 构建验证：`make -j2`。

未自动连接运行中的用户服务或 `jh_online` 数据库；运行时登录/断开验收应在隔离测试
服务中检查以上日志，同时覆盖账号接管、返回标题再登录和后台账号分页。
