# 管理后台「游戏服务繁忙」+ 登录失败 + 进程 CPU 0% 假活

日期：2026-08-16

## 症状

1. 管理后台返回：`游戏服务繁忙，管理操作请稍后重试。`
2. 玩家登录不上。
3. 服务进程仍在，CPU ≈ 0%，日志停在：
   - `account_lookup_query_failed ... MySQL socket send failed`
   - `protocol_lock_timeout ... /api/monster-levels wait_ms=8000`
4. MySQL（`192.1.1.3`）当时可连、几乎空闲；`wait_timeout=120`，`Aborted_clients` 极高。

## 根因

| 环节 | 契约 / 行为 | 证据 |
|------|-------------|------|
| 文案出处 | admin 抢不到 `g_vm_mock_service_protocol_mutex`（8s）回 503 | `web_admin_server.c` `protocol_lock_timeout` |
| 登录失败 | 查 `accounts` 时持久连接已死 | `MySQL socket send failed` |
| 假活 | 某线程持协议锁阻塞（旧路径 `connect()` 无超时可达分钟级） | CPU 0% + 日志停 + MySQL 侧无锁等待 |
| 放大 | `role-not-active` 在锁内高频 `printf` | 同窗大量 reject 日志 |

MySQL InnoDB 锁**看不到**该协议锁；库侧当时不是根因，是连接被踢后服务端持锁重连/卡死。

## 修改

1. **`mysql-client.c`**：非阻塞 connect + `CBE_MYSQL_TIMEOUT_MS` 限时；SO_KEEPALIVE；传输失败且未跑 row callback 时自动关连接并重试一次（`query_reconnect_retry` / `exec_reconnect_retry`）。
2. **协议锁取证**：持锁线程写入 `holder`；admin 超时额外打 `protocol_lock_holder holder=... hold_ms=...`。
3. **`role-not-active`**：2s 窗口限频，避免锁内刷屏。

## 验证

- [x] `make -j2`
- [ ] 部署后空闲 >2min 再登录：应见最多一次 reconnect，不应卡死整进程
- [ ] 再出现 503 时日志应有 `protocol_lock_holder`，能指出当时占锁路径
- [ ] `role-not-active` 不再每帧刷屏

## 运维建议

把 MySQL `wait_timeout` / `interactive_timeout` 调大（例如 28800），减少空闲踢连接；与本次服务端修复互补。

## 仍未知

若限时 connect 后仍出现 CPU 0% 假活，用 `gdb thread apply all bt` 对照 `protocol_lock_holder` 继续取证（可能另有锁序问题）。
