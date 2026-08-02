# 非挂机自动战斗：每回合需再点一次

日期：2026-08-01

## 症状

战斗内点「自动」（非挂机）只出手一回合；下一回合操作菜单回来，需再点自动。
显式关闭前应连续 synth / poll 出手。

## 根因

阶段 D 后 auto prefer / pending 的权威在 `session`，`account_state` 同名字段在
capture 时被清零。

契约（`2026-08-01-mock-service-global-isolation.md`）：**restore 前**绑定
`active_client_id`，`session_load_auto_timers` 才能读到连接 session。

实际顺序（poll / data / disconnect）：

```text
account_restore()          ← active_client_id 仍为 0 → session=NULL
  → load_auto_timers(NULL, account) 读已被清零的 account → prefer=0
active_client_id = client  ← 太晚
poll 投递 auto → prefer=0，无 synth
capture 把 prefer=0 写回 session  ← 正确 prefer 被覆盖
```

首击仍在同一次 `4/11 type=1` 请求内 `operate-only` 完成，故体感是「点一下打一下」。

## 修改

`mock_server_transport.c`：disconnect / scene_sync_poll / 主 data 路径在
`account_restore` **之前**设置 `g_vm_mock_service_active_client_id`。

## 验证

1. `make -j2 server`
2. 单人遇怪 → 点自动 → 日志 `prefer=1` + `auto_timers_session_load prefer=1`；
   无需再点，poll 出现 `mock_battle_auto_poll_deliver` / `via=auto-poll`
3. `4/11 type=0` 后不再出手
4. 挂机连续遇怪不受影响
