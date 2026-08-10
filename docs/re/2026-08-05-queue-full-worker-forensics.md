# queue-full worker 占用取证（2026-08-05）

## 触发

`[warn][mock-service] connection_rejected kind=game reason=queue-full`

accept 队列已满（128），新 game 连接在入登录逻辑前被关闭。故障现场日志若已丢失，无法事后点名具体请求/MySQL。

## 取证日志（窄范围）

拒连时额外打快照（同一进程最多每 5 秒一次）：

```text
[warn][mock-service] queue_full_forensics kind=game workers=N queued=128 oldest_queued_age_ms=...
[warn][mock-service] queue_full_worker worker=1 phase=session-idle|handle|idle age_ms=... last_source=... state_hold_ms=...
```

| phase | 含义 |
| --- | --- |
| `idle` | 等待下一 accept job |
| `handle` | 正在 recv / 协议锁 / 构建 / send（含同步 MySQL） |
| `session-idle` | data 帧后空等复用，最多 `SESSION_IDLE_MS`（当前 5000） |

`last_source` 为该 worker 最近一帧的 handler 名（如 `builtin-login`、`scene_sync_poll`、`client_disconnect`）。  
`state_hold_ms` / `process_ms` 高 → 偏协议/同步库；`phase=session-idle` 且 `age_ms` 接近 3000 → 偏连接复用占槽。

## 仍未知

本次用户侧 `queue-full` 刷屏时的完整 stdout 已不可见；需复现后用上述快照定根因。不改业务契约，仅增加拒连时占用快照。
