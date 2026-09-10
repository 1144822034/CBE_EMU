# queue-full 假死：GameThread 占用 + 取证失真（2026-09-05）

## 触发 / 症状

日志形态（节选）：

```text
queue_full_worker ... phase=handle age_ms≈2.1M–2.8M frames=0
  last_source=malformed state_wait_ms=0 state_hold_ms=0 process_ms=0
connection_rejected kind=game reason=queue-full
queue_full_forensics ... workers=32 queued=256 evidence=worker-occupancy
```

- 32 个 game worker 全部 `phase=handle`，`age_ms` 同步涨到数十分钟。
- 冻结 job 序号连续（约 `11087963`–`11087994`）。
- 表现为服务「假死」：进程在、新连接被拒。

## 链路与第一次偏离

| 环节 | 契约 | 现场 |
|------|------|------|
| accept | 入队，`SO_RCVTIMEO/SNDTIMEO=10s` | 队列满后拒连（症状） |
| worker | `phase=handle` → `vm_mock_game_thread_handle_client` | 32 槽全占满 |
| Architecture B | 单 GameThread FIFO 跑 `handle_client` | **放大点**：一卡全堵 |
| handle 前半 | `recv_all` header/body（锁外） | 若卡在此：`holder=-`，帧取证未写 |
| handle 后半 | 阻塞 `pthread_mutex_lock(protocol)` + 持锁业务 | 若卡在此：`holder=game-handle` 或 admin 标签 |

**第一次偏离**：某个（通常唯一）GameThread 上的 `handle_client` 长时间不返回；其余 31 个 worker 堵在 `g_vm_mock_game_job.occupied` 等待上，accept 队列随后饱和。  
`queue-full` / `connection_rejected` 是占用后果，不是根因。

## 日志误导（已确认）

1. **`last_source=malformed` 在默认 `game_thread=1` 下几乎无意义**  
   帧取证写在 `__thread` TLS 上（GameThread），worker 在返回后读的是**本线程** TLS，跨线程拷贝失败。失败回落路径还会把空 `lastSource` 写成 `malformed`。因此全员 `malformed` **不能**证明当前卡在畸形包解析。

2. **`frames=0` / `process_ms=0`**  
   worker 侧只在帧返回后更新；卡死期间只能说明「当前帧未返回」，不能区分 recv / 等锁 / 持锁 MySQL。

3. **旧 `queue_full_forensics` 未打印**  
   `protocol_lock_holder` / `protocol_hold_ms` / `game_occupied` / `game_job_age_ms`，无法在拒连瞬间二分。

## 候选根因（仍 unresolved，需下轮快照）

| 候选 | 机制 | 下次快照判据 |
|------|------|----------------|
| A. 锁外 `recv_all` 拖死 | ~~单次 SO_RCVTIMEO 可被慢速重置~~ → **已加整帧绝对 deadline** | 若仍假死且 `holder=-`：看是否还有非 IO 路径；应出现 `socket_io_deadline` |
| B. 等协议锁 | GameThread 阻塞 `pthread_mutex_lock`；holder 为 admin/其它 | `game_occupied=1`，`protocol_holder=admin-…`，`hold_ms` 大 |
| C. 持锁内阻塞 | `holder=game-handle`，锁内 MySQL/同步 I/O 不归 | `protocol_holder=game-handle`，`hold_ms≈game_job_age_ms` |

历史同类：`docs/re/2026-08-30-admin-dispatch-protocol-lock-mysql.md`、`docs/re/2026-08-16-admin-busy-mysql-protocol-lock-hang.md`。

## 修改

### 取证（此前）

1. GameThread 完成后把 TLS 帧取证 **export → job → import** 回调用方 TLS。  
2. 新 job 开始时清空 worker `lastSource` / timing。  
3. `queue_full_forensics` 增加：`protocol_holder`、`protocol_hold_ms`、`game_thread`、`game_occupied`、`game_job_age_ms`、`game_last_source`。

### 契约修复（2026-09-05 续）

4. **`recv_all` / `send_all` 整帧绝对 deadline** = `VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS`（10s）。  
   - 违反点：仅靠 per-call `SO_RCVTIMEO`，慢速/半开可把单次 `handle_client`（进而 GameThread FIFO）拖到数十分钟。  
   - 行为：超时返回失败（与原 recv/send 失败相同 → 关连接、释放 FIFO）；限频日志 `socket_io_deadline op=recv|send ... evidence=absolute-frame-io-timeout`。  
   - 最坏仍可能多约一个 SO_*TIMEO（检查后进入阻塞 call），远小于假死窗口。

## 明确不做

- 不抬队列当根因修复、不重试、不伪造成功。  
- GameThread 等协议锁限时 / hang watchdog：等新快照确认候选 B/C 后再改。

## 验证

- [x] `make -j2`（部署前再跑）
- [ ] 正常路径：完整帧应在 10s 内完成；`last_source` 为真实 handler
- [ ] 慢速/半开：应见 `socket_io_deadline`，不应再出现数十分钟 `game_job_age_ms` 且 `holder=-`
- [ ] 若仍假死：用 `queue_full_forensics` 判 B/C

## 仍未知

那次现场是 A/B/C 哪条仍 unresolved；本修复覆盖源码可证的候选 A 契约漏洞。
