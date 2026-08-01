# 异步 role 落库队列（2026-08-01）

## 代码现状（2026-08-01 补回后）

**专用 persist worker 队列已合入当前树。** 详见 `2026-08-01-server-baseline-audit.md`。

| 项 | 现状 |
| --- | --- |
| 锁内拷贝 `roleDb`+`warehouse`，锁外 MySQL | **仍在**（经 persist worker） |
| `persistGeneration` 清脏门闩 | **仍在** |
| 2 persist worker / `persistDirtySerial` / `persistReflushWanted` | **已合入** |
| 日志 `persist_workers=2` / `async_flush=1` / `role_deferred_flush_queued` | **会出现** |
| `g_vm_mock_persist_write_mutex` | **已实现** |
| warehouse 快照参数 | **已合入**（见 warehouse-deferred-flush-lost） |

下文「修改 / 验证」为已落地方案。

## 问题

B-lite 已把 MySQL 移出协议锁（`off_lock=1`），但 `flush_deferred_role_db`
仍在 **game worker** 上同步等待 `flush_ms`。挂机 settle / 背包脏写密集时
占满 worker，抬高他人 `queue_wait_ms`。

## 修改（已落地）

1. CBMR 后 `flush_deferred` 只入队账号快照（roleDb + warehouse），立即返回。
2. 专用 **2** 个 persist worker 写库；`persistGeneration` + `persistDirtySerial`
   防止清脏过早；busy 期间再脏则 `persistReflushWanted` 再入队。
3. `g_vm_mock_persist_write_mutex` 与 offline sync flush 串行化写库；
   `account_flush_for_session` 在 MySQL 期间释放协议锁。
4. 协议锁声明上移到 `mock_server_core.c`，供 equipment_npc 掉锁写库。
5. `account_release_if_idle` 在 dirty/busy 时不释放账号堆对象。

## 验证

1. 重启 `jh-online-server`：日志含 `persist_workers=2 ... async_flush=1`。
2. 挂机一轮：出现 `role_deferred_flush_queued` 与
   `role_deferred_flush ... async=1`；game 请求不再被同线程 `flush_ms` 拉长。
3. 仓库存入后断线/回标题：仓库有货（对照 warehouse-deferred-flush-lost）。
4. 多人一人挂机：他人 `queue_wait_ms` 不应随 `flush_ms` 同量级爬升。
