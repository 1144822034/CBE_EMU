# 管理后台「请求不完整」

## 症状

发布后访问 `http://<host>:19091/admin-418yz6` 返回：

```text
400 Bad Request
请求不完整。
```

## 根因

### 第一轮（超时过短）

`vm_mock_admin_handle_client` 在收满 HTTP 头+`Content-Length` 声明的正文前，
若 `recv` 失败或超时，就回「请求不完整」。

旧参数：

- `VM_MOCK_ADMIN_SOCKET_TIMEOUT_MS = 100`
- `VM_MOCK_ADMIN_REQUEST_MAX = 8192`

跨机访问或 POST 正文落在后续 TCP 段时极易超时。

### 第二轮（协议锁挡住收包）

即使超时已提到数秒，worker 仍在 **`recv` 之前** 持有
`g_vm_mock_service_protocol_mutex`。游戏请求占用该锁做 MySQL / 战斗逻辑时，
管理连接已 accept 并入队，但迟迟不能读套接字；浏览器中止连接后，`recv`
返回 0，仍被判成「请求不完整」。

POSIX 路径曾用 `int` 毫秒直接 `setsockopt(SO_RCVTIMEO)`（应为 `timeval`），
在 Linux 部署上超时语义不可靠。

## 修改

1. HTTP 收包在协议锁外完成；仅 `dispatch`（读改账号/任务/MySQL）持锁。
2. 管理套接字使用 `timeval`（POSIX）/ 毫秒（Windows），超时 `15000` ms。
3. accept 管理连接时不再套用游戏口 `2500` ms 超时。
4. 请求缓冲保持 `65536`；不完整时打 `incomplete_request` 诊断日志。

## 验证

1. 部署新 `jh-online-server` 后打开 `/admin-418yz6/`，应 200 出登录/首页。
2. 游戏侧有在线请求时反复刷新后台、保存动态 NPC / 任务表单，不再出现「请求不完整」。
3. 若仍失败，日志应有 `incomplete_request received=...`，据此区分真截断与旧锁等待。
