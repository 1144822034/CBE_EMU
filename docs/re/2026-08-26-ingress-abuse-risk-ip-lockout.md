# 公网 ingress 异常来源 IP 封锁（2026-08-26）

## 触发与首次偏离

公网服务日志出现了两个不同来源重复建立不完整首帧连接的证据：

```text
ingress_drop ... reason=source-pending-cap pending=5 cap=4 source=<ipv4>
ingress_drop ... reason=peer-closed waited_ms=0 source=<ipv4>
```

这些事件发生在 listener-side ingress：当时尚未收到完整的版本 1 `CBMS`
帧，因此没有 WT kind/subtype、没有客户端 parser/callback，也没有进入游戏
worker 或全局协议锁。`source-pending-cap` 是其中较强的证据：同一 IPv4
同时保留了五个不完整首帧，而服务每来源只保留四个。

单次 `peer-closed`、`frame-timeout` 或其他 socket 错误可能来自网络抖动、
健康检查或服务端局部异常，不能仅凭一次就认定为滥用。

## 契约与修复

`server_login_ip_blocks` 是游戏、用户中心和后台共享的来源 IP 权威封锁表。
新增字段：

- `ingress_violations`：仅累计 `source-pending-cap`；
- `block_reason`：封锁来源，值为 `credential-failure` 或
  `ingress-source-pending-cap`。

同一来源累计三次 `source-pending-cap` 后，服务将该行持久化为
`blocked=1`，写入原因，并加入本进程的封锁缓存。第三次触发仍会关闭当前
异常连接；之后同一 IPv4 的游戏 TCP 连接在**分配 ingress 槽位前**被静默
关闭，既不进入 worker，也不会消耗 32 个不完整帧槽位。

正常的成功凭据登录仍会删除尚未封锁的来源记录；因此未达到三次的偶发异常
不会遗留为永久惩罚。已封锁来源不能自行解除，须由可信后台操作员清除该 IP
的整行记录，才会同时重置登录失败与 ingress 异常计数。

后台“风险管理 → 风险 IP”继续读取同一张 `blocked=1` 表，并新增“入口异常”
和“封锁原因”列。历史封锁行没有原因时按既有“连续凭据失败”展示。

## 入口丢弃日志聚合

公网扫描会让同一来源在极短时间内产生大量 `peer-closed` 或
`source-pending-cap`。这些是同一个 listener-side 事件的重复观测，不应淹没
`login-ip-lock`、MySQL 与协议错误等需要人工处理的日志。

服务按 `source + reason` 维护 10 秒聚合窗口：每个组合的首条
`ingress_drop` 仍保留完整上下文，后续重复事件不逐条打印；连续或空闲窗口结束时
输出一条 `ingress_drop_summary`，其中包含总事件数、被折叠数量、首末 ingress ID、
窗口时长和最大等待时间。不同原因和不同来源不会合并，封锁写入日志也不会被聚合。
聚合表仅属于 listener 线程，不改变 socket、worker、封锁阈值或任何 CBE 客户端协议
行为。

## 数据兼容与已修复缺陷

新库由 `server/mysql/schema.sql` 创建完整字段；已部署库可执行
`server/mysql/migrate_login_ip_blocks.sql`。服务自身还会以
`information_schema` 检查并补齐两列，防止旧库静默缺字段。

加载封锁缓存时，MySQL 回调提供的是 `(pointer,length)`，不是 NUL 结尾文本。
此前直接把该值传给 IPv4 校验器，可能导致合法封锁行被误判并输出
`blocklist_load_failed error=no MySQL error detail`。现在回调先按长度复制到
本地 NUL 结尾缓冲，再校验与缓存；这不改变封锁表的权威数据。

## 验证边界

`scripts/risk-admin-pagination-regression.c` 覆盖 MySQL 长度字段到封锁缓存的
复制、风险 IP 查询字段和后台原因标签。隔离场景
`scripts/run-mock-service-ingress-regression.ps1` 在其自有 loopback 服务、端口、
资源副本和临时数据库中验证：

1. 四个不完整帧不会占用游戏 worker；
2. 第五个同源不完整帧被 ingress 回收；
3. 三次独立 cap 事件后写入共享 IP 封锁；
4. 后续同源 ping 在 ingress 分配前被静默关闭；
5. 重复的同源 cap 在 10 秒窗口后输出一条聚合摘要；
6. 同一场景中正常 ping 与登录仍先走原有 CBMR/登录契约。

该服务端安全边界不修改 CBE/CBM、VM 内存、寄存器、WT 响应、客户端输入或
游戏状态机。
