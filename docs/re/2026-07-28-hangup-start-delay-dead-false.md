# 5 秒后开始挂机被 start-delay-dead 误清

Date: 2026-07-28

Status: implemented (server)

```text
trigger: 地图点挂机（未打过仗 / battle HP 未种子）→「5秒后开始挂机」
symptom: 未进战斗；日志 start-delay 后立刻 mock_hangup_loop_clear start-delay-dead
root: start-delay poll 用 g_mockBattleRoleHpCurrent==0 当死亡；地图侧开战前该值常为 0
fix: start-delay 死亡门改看 durable role->hp
```

## 证据

```text
mock_hangup_battle_start action=start-delay delay_ms=2000 ...
mock_hangup_loop_clear reason=start-delay-dead was_active=1 ... start_pending=1
# 场景 01桃花岛_01，moveinfo 仍在走 — 非战死
```

## 契约

- 首次挂机：`HangupStartPending` 保留至 `not_before`，再 poll 合成 hangup start
- 仅 durable `role->hp==0`（地图死亡）可 `start-delay-dead`
- 战中 / 场间仍可用 battle HP（operate / loop poll）

## 验证

```text
点挂机 → action=start-delay
# 约 5s 内无 start-delay-dead
mock_hangup_start_delay_deliver
# 进战斗；系统「已开始挂机」
```
