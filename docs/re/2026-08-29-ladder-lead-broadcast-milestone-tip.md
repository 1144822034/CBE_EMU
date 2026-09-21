# 天梯：10 倍层日首通世界播报 + 里程碑奖励提示（2026-08-29）

## 需求

1. 每日每个 **10 的倍数层**（10/20/30…），首位通关玩家世界频道播报：`玩家 {名} 领先通过 {N} 层天梯`。
2. 发放里程碑奖励时，给通关玩家系统提示：`恭喜玩家获得经验…、金钱…、物品xN`，避免玩家以为没奖。

## 落点

- `src/server/mock_server_ladder.c` · `vm_mock_ladder_on_battle_end`
- 表 `server_ladder_daily_lead(day_ymd, floor_n, role_id)`：`INSERT IGNORE` + `ROW_COUNT()==1` 判定日首通
- 播报：`vm_mock_service_broadcast_world_chat_live`（与强化/宝箱同款 live [世]）
- 提示：`vm_mock_service_session_enqueue_system_message`（≤81 字节 GBK）

## 触发条件

- 播报：本场 `floor > prevBest` 且 `floor % 10 == 0`，且日首通 claim 成功
- 提示：本场至少发出经验 / 金钱 / 物品之一（按 `server_ladder_rewards` 中 **层数等于本场层数** 的那一行；不按倍数叠加）

## 复测

1. 清空或换日：两角色先后打通第 10 层 → 仅先通者世界播报；后通者无播报但仍可拿里程碑奖+系统提示。
2. 通关配置了里程碑的层 → 系统频道出现「恭喜玩家获得…」。
3. 非 10 倍层胜利 → 无领先播报。
