# 仓库数据延后落库丢失（2026-08-01）

## 现象

仓库存入后，偶发重登仓库为空，且背包中对应物品也不见（双端丢失）。

## 根因

1. **写库读错对象**：`role_db_save_relational_ex` 虽接收仓库快照参数，但
   `vm_net_mock_warehouse_write_sql` 实际读写的是进程全局
   `g_vm_net_mock_warehouse`。延后 flush 在协议锁外跑 MySQL 时，其它请求的
   `session_mark_offline` 可能已把 `loaded=false`，于是：
   - 背包 DELETE+INSERT 按快照提交（存入物已从背包删除）；
   - 仓库 write 因全局未 loaded 被跳过（仍返回 true）；
   - 事务 COMMIT → 物品两边都没有。

2. **capture 冲掉快照**：offline 清全局 `loaded` 后，下一次
   `account_capture` 把空壳写回 `accountState->warehouse`，后续 deferred flush
   的 `warehouseIncluded=false`，同样只落背包。

3. **回标题未先 flush**：`title-login-rebind` / `account-rebind` 直接
   `mark_offline`，未像 takeover/heartbeat 那样先
   `account_flush_for_session`。

## 修复

| 点 | 行为 |
| --- | --- |
| `warehouse_write_sql` | 必须写传入的 `warehouse` 快照，禁止读全局 |
| `account_capture` | 仅当全局 warehouse 属于本账号且 `loaded` 时才覆盖快照 |
| `session_mark_offline` | 仅清本账号的全局 warehouse view |
| `bind_session_account` | rebind 前先 `account_flush_for_session` |

## 验证

- [x] `make -j2`
- [ ] 存入仓库后立即回标题再进游戏：仓库有货，背包无该行
- [ ] 存入后断线/心跳超时：同上
- [ ] 多人同服一人存仓库、另一人进出：不得互相清空仓库
