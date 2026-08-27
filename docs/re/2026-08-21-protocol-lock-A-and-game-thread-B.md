# 2026-08-21 协议锁人多卡：A 减负 + B 单 GameThread

## 主诉与证据

- 在线一多全服卡；比武 LIVE 开打放大。
- 服务器 4 核，`jh-online` 约 **10% CPU** → 等锁/串行，不是算力不够。
- 根因：多 worker 抢 `g_vm_mock_service_protocol_mutex`。

## 本轮交付

### A 减负

| 项 | 行为 |
|----|------|
| 比武每秒新开 | **K=6**（`VM_MOCK_ARENA_MAX_STARTS_PER_TICK`） |
| 比武同时开战 | 仍 **24** |
| 全服 DUEL_MAX | 仍 **64** |
| auction tick | `ensure_tables` 只做一次 |
| wager hydrate | 本就 once；tick 1Hz 早退 |

### B 单 GameThread

- 默认 **开启**：所有 game `handle_client` 经 FIFO 单线程执行。
- 回滚：环境变量 **`CBE_MOCK_GAME_THREAD=0`**（恢复多 worker 直接抢协议锁）。
- 日志：`game_thread=1 evidence=FIFO-world-mutations-on-one-thread`。
- Admin / persist 仍可用协议锁；与 GameThread 偶发争用，远小于 32 worker 互抢。

## 验收目标

- 本轮：**80～120 在线可玩**；256 为愿景（下轮多逻辑/分片）。
- 比武开打可能分秒铺开（K=6），最终仍应配满。
- `make -j2`；部署后看 `state_wait_ms` / admin 503 / `arena_tick_slow` / `arena_match_start_throttle`。

## 明确不做（本轮）

- Redis、按场景多逻辑线程、多进程分片。
- 盲目抬 K/并发到 64。

## 下轮候选

- 按场景多逻辑线程（吃满 4 核；对比武同图帮助有限）。
- recv/process/send 拆分，使 IO 与逻辑进一步重叠。
