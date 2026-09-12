# 2026-09-11：开自动后再点挂机提示「功能暂未实现(1/4/11)」

## 现象

开启战内 `[自动]` 后再点 HUD「挂机」（或跨场/息屏恢复重发 prefer）时，聊天出现系统 tip：`功能暂未实现(1/4/11)`。

服务端：

```
[error][network] unhandled wt=4/11 len=53 objects=1 first=1/4/11:44
[warn][network] mock_unimplemented_handler_notice tip=功能暂未实现(1/4/11) ...
```

常伴 `auto_timers_session_load … prefer=1` / `sticky_restore … prefer=1`，且前一条多为地图侧 `18/7` 或 move，而非武装战斗。

## 根因

1. Pixi `setBattleAutoPrefer(true)` 发已知契约 `1/4/11 type=1`（可带 `Operate` + `hangup=0`），**不是**缺实现。
2. `vm_net_mock_build_battle_auto11_flag_response` 在 `!inBattle`（无 `g_mockBattleOperateSessionArmed` 且非 CBE 战斗屏）时对 `type=1` **return 0**。
3. Dispatch 落到 `mock_unimplemented_handler_notice`。

典型触发：

- 上场已开自动（`prefer`/`autoBattleActive` 保留）→ 地图点挂机遇怪 → 开战回调再发 `4/11`，但本请求处理时会话未武装（或 sticky 未还原 armed）。
- 息屏软恢复在进战前就发 `4/11 type=1`。

首次偏离：已知 `4/11` 被当成 unknown，而同条件的 `4/4` 早已 empty-ack。

## 修改

`mock_server_battle.c` `vm_net_mock_build_battle_auto11_flag_response`：

- 地图侧 `type=1`：仍种 `prefer` / 可选 `Operate` / `hangup=0`，**ACK `4/11`**，不 arm poll synth（`map-side-prefer-seed`）。
- 地图侧空闲 `type=0`（无 prefer/挂机）：**empty-ack**，不再 tip。
- 战内 `type=1` 路径不变（hangup-style-arm）。

## 验证

1. 战中开 `[自动]` → 正常出手；无 tip。
2. 保留自动 → 地图点挂机 → 选技能开始：应见「已开始挂机」/遇怪，**无**「功能暂未实现(1/4/11)」；日志 `mock_battle_auto11_flag type=1 action=map-side-prefer-seed` 或开战后再 `hangup-style-arm`。
3. 仅自动、不开挂机：打完不自动再遇怪；再遇怪仍自动出手。
4. 关自动 `type=0` 仍清 prefer；无武装时连点关自动应 empty-ack，无 tip。
