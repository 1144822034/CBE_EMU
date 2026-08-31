# admin-dispatch 长持协议锁 → 全服 queue-full

日期：2026-08-30

相关：`docs/re/2026-08-16-admin-busy-mysql-protocol-lock-hang.md`

## 症状

1. `protocol_lock_timeout`：`GET /api/monster-levels`、`/api/npc-scenes`、`/admin-418yz6/`（`wait_ms=8000`）
2. `protocol_lock_holder holder=admin-dispatch hold_ms` 涨到数十万毫秒
3. `queue_full_worker ... phase=handle age_ms≈hold_ms`，`connection_rejected reason=queue-full`
4. MySQL `PROCESSLIST`：仅有业务连接 `Sleep`（无长 `Query` / `Info`）——库侧定不到具体 SQL

## 根因

| 环节 | 契约 / 行为 | 证据 |
|------|-------------|------|
| 共享锁 | admin 与 `game-handle` 共用 `g_vm_mock_service_protocol_mutex` | `web_admin_server.c` / `mock_server_transport.c` |
| 占锁标签 | 旧日志只有 `admin-dispatch`，看不到 METHOD/target | `protocol_lock_note("admin-dispatch")` |
| Pixi 热路径 | `/api/monster-levels`、`/api/npc-scenes` 在持锁下调用 `*_db_load()` | dispatch 内直接 `vm_net_mock_*_db_load()` |
| 放大 | 持锁阻塞 → 游戏 worker 堵在 handle → 队列满拒连；其它 admin 8s 503 | `queue_full_*` + `hold_ms` 同量级 |
| 库侧空闲 | 死连接 / 客户端 I/O 卡住时服务器常为 Sleep | `SHOW FULL PROCESSLIST` |

崩溃/假活 PC 不在 MySQL InnoDB；第一次偏离是 **admin 路径在协议锁上做可阻塞 I/O（或其它长操作）且不释放**。

## 修改

1. **持锁取证**：`holder` 改为 `admin-dispatch METHOD /target`；`io_resume` 恢复同一标签。
2. **Pixi 公共 GET 不抢协议锁**：`/api/monster-levels`、`/api/npc-scenes` 在 `protocol_lock_wait` 之前单独 dispatch（`lock_depth=0`）。
3. **catalog load 自带 mutex**：`monster_db_load` / `dynamic_npc_db_load` 内部序列化，避免无协议锁时与游戏线程交错写目录；MySQL 连接本就 TLS，可并行建连。

## 验证

- [ ] `make -j2`
- [ ] 部署后 Pixi 轮询 `/api/monster-levels`、`/api/npc-scenes` 不再出现在 `protocol_lock_timeout` 受害者/占锁者上
- [ ] 再出现 503 时 `holder=` 应带具体路由（例如 `admin-dispatch GET /admin-418yz6/`）
- [ ] 游戏连接在仅有 Pixi 轮询压力下不应再因这两条 API 进入长时间 `queue-full`

## 仍未知

其它 admin 页若在持锁下跑重 MySQL/磁盘，仍可能长持锁；下次用新 `holder` 标签点名后再按路由做 `io_pause` 或锁外加载。
