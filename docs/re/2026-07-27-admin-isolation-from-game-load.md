# 管理后台与游戏负载隔离

## 症状

玩家在线变多后，管理后台（`:19091`）操作经常响应超时或失败；游戏口繁忙时后台几乎不可用。

## 根因

管理与游戏共用同一套连接 worker 队列，并共用 `g_vm_mock_service_protocol_mutex`：

1. **队列争用**：管理 accept 与游戏 accept 都入同一 `g_vmMockServiceWorkerPool`。游戏连接堆积时，管理请求长时间排队，浏览器超时。
2. **锁持有过长**：`vm_mock_admin_handle_client` 在整段 `dispatch`（含大页 HTML 生成与 HTTP `send`）期间持有协议锁。游戏侧 MySQL/战斗处理也会长时间占锁，管理端阻塞；反过来管理端慢客户端发送也会堵住游戏。

此前已将 HTTP `recv` 移出协议锁（见 `2026-07-26-admin-incomplete-request-timeout.md`），但仍未隔离 worker，也未在发送路径释放锁。

## 修改

1. **独立管理 worker 池** `g_vmMockAdminWorkerPool`：默认 2 个线程（`CBE_MOCK_ADMIN_WORKERS`，范围 1–4），自有队列；游戏池只处理 CBMS。管理不再与游戏抢同一队列槽位。
2. **发送/本地文件读释放协议锁**：admin 线程本地 `g_vm_mock_admin_protocol_lock_depth`；`send_response` / `send_binary` / 二维码脚本读盘 / 支付远端 HTTP 在 I/O 期间 `pause`/`resume`，避免大包发送拖死游戏。
3. **协议锁限时获取**：管理 dispatch 用 `trylock` 轮询，最长约 8s；超时返回 `503`「游戏服务繁忙，管理操作请稍后重试。」，避免浏览器无限挂起。
4. 管理队列满时立即回 `503`「管理请求队列已满」。

## 仍存在的契约

账号/角色等内存态读写仍在协议锁内完成（与游戏序列化模型一致）。超大页面的 **HTML 拼装** 仍可能短暂占锁；发送与远端支付等待不再占锁。

## 验证

1. `make -j2` 通过。
2. 启动后日志应有两行 concurrency：`mock-service`（`allocate_game_buffers=1`）与 `mock-admin`（`allocate_game_buffers=0`），以及 `admin_isolated=1`。
3. 多客户端压游戏口时刷新后台首页/保存表单：应能较快返回；若游戏持锁过久，应看到 `protocol_lock_wait` / `protocol_lock_timeout` 与 503，而不是连接挂死。
4. 充值下单路径仍应在远端 HTTP 期间释放协议锁（支付 `pause`/`resume`）。
