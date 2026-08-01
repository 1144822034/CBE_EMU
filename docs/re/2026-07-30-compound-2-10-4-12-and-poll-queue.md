# 2/10+4/12 同包空响应 + poll 占 worker 抬高 queue_wait（2026-07-30）

## 证据（`bin/server_out.txt`）

```text
unhandled wt=2/10 len=24 first=1/2/10:10,1/4/12:0
account=fl326586 request=24 response=0 source=ignored-unhandled-server-only
```

同日志几乎每条：

```text
queue_wait_ms=580..950  state_wait_ms=0  process_ms=0..1
session_reuse ... hold_ms=560..640
mock_battle_auto_poll_deliver ... evidence=scene-sync-poll
```

## 根因

1. **同包未处理**：`actor-other-only10` 要求单独 `2/10`；`auto12` 要求 WT 头
   `4/12`。头是 `2/10` 且尾随 `4/12` 时两边都拒 → `response=0`。
2. **队列等待**：scene-poll 回包后 worker 仍 `SESSION_IDLE` 空等（原 400ms）。
   poll 间隔 ~3s，复用接不住下一轮 poll，只占坑；多端并发时 `queue_wait_ms`
   到 0.6–0.9s。挂机出手又走 poll，体感“隔一会儿才动”。

## 修改

1. `auto12`：包内含 `1/4/12` 即认（不限包头）；战斗中优先处理。
2. `actor-other-only10`：允许尾随空 `4/12`（非战斗 / unarmed 回退）。
3. scene-sync poll 成功后 `return 2` 结束 TCP，不再 idle 占 worker。
4. 默认 worker **12**；data 复用 idle **200ms**。

## 验证

1. `make -j2`，重启服务端。
2. 不应再出现 `unhandled ... 1/2/10:10,1/4/12:0` / `response=0`。
3. 多人在线时 `queue_wait_ms` 应明显低于原先 ~600–900；poll 少见长
   `session_reuse hold_ms`。
