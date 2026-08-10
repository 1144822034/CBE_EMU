# queue-full：约 80 人容量调整（2026-08-05）

## 背景

现场 `connection_rejected kind=game reason=queue-full`；进程曾以 `workers=4` 运行。  
约 80 人在线时，短连 + `SESSION_IDLE` 占槽会使 accept 队列（原 128）易满。

## 修改（`mock_server_transport.c`）

| 项 | 原值 | 新值 |
| --- | --- | --- |
| `VM_MOCK_SERVICE_WORKER_DEFAULT` | 12 | **32** |
| `VM_MOCK_SERVICE_WORKER_MAX` | 32 | **64** |
| `VM_MOCK_SERVICE_CONNECTION_QUEUE_MAX` | 128 | **256** |
| `VM_MOCK_SERVICE_SESSION_IDLE_MS` | 3000 → 800（试用） | **5000**（800 易卡住，回退加长） |

仍可用 `CBE_MOCK_SERVICE_WORKERS` 覆盖（2..64）。未设环境变量时默认 32。

## 权衡

- 5000ms idle：优先保证连点/NPC 复用，避免短 idle 导致客户端卡住；靠 worker=32（上限 64）与 queue=256 扛占槽。
- 协议构建仍串行（`g_vm_mock_service_protocol_mutex`）；加 worker 主要缓解 I/O / idle 占槽。
- 若再出现 `queue-full`，优先 `CBE_MOCK_SERVICE_WORKERS=64`，勿再盲目缩短 idle。

## 验证

1. 重启后日志：`concurrency workers=32 queue=256`（或 env 指定值）。
2. 80 人高峰：`queue-full` 应明显减少；若仍出现，看 `queue_full_worker` 快照（见同日 forensics 文档）。
3. 进场景连点 NPC/对话：偶发多一次 reconnect 可接受；不应大面积登录失败。
