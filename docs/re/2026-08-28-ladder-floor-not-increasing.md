# 天梯挑战成功但层数不增加（2026-08-28）

## 触发 / 现象

多客户端同时在线时，玩家天梯 PvE 打赢后，面板「今日最高层 / 下一层」经常仍停在开战前层数；日志里常见该角色没有对应的 `ladder_victory`，或胜利结算落在别的角色战斗结束路径上。

## 契约

- 开战：`WT 44/2` → 扣 `server_ladder_daily.attempt_count`，按今日 `best_floor+1` 武装三人 subtype-10。
- 胜利：`note_victory_settlement` → `vm_mock_ladder_on_battle_end(true)` → 写回今日 `best_floor`。
- 失败/死亡：同函数 `victory=0`，不提升层数。
- 设计（后台文案）：次数与层数进度按自然日重置；跨日从第 1 层再爬。

## 根因（首次偏离）

`g_vm_mock_ladder_battle_armed` / `battle_floor` 是**进程全局**。`mock_server_battle.c` 里任意客户端的死亡或胜利都会调用 `vm_mock_ladder_on_battle_end`：

1. 玩家 A 开战 → `armed=true`。
2. 玩家 B 任意战斗结束 → `on_battle_end` 看到全局 `armed`，立刻清掉并（若 B 胜利）可能用 A 的 floor 去更新 B。
3. A 随后胜利 → `armed` 已假 → 直接 return → **不写 `best_floor`**。

次要：胜利路径对 `server_ladder_daily` 仅用 `UPDATE ... WHERE`，行缺失时静默 0 行；`server_ladder_progress` 从未写入。

## 修改

- `src/server/mock_server_ladder.c`：按 `roleId` 槽位武装；仅匹配角色的战斗结束才结算/清理。
- 胜利：`INSERT ... ON DUPLICATE KEY UPDATE GREATEST` 写 daily + progress。
- 占槽在扣次之前，避免“没开战却扣次”。

## 验证

1. 两客户端：A 开天梯未结束时 B 打野/死亡/胜利；A 再打赢 → 日志 `ladder_victory role=<A> floor=N`，状态 `bestFloor=N`。
2. 同角色连续次数（后台 `daily_limit>1`）：第 N 层胜后下一战为 N+1。
3. `make -j2`。

## 仍须注意

- 默认 `daily_limit=1` 时，当日只能打一层；跨日进度按设计重置。要当日连爬请提高后台每日次数。
- `atk_per_floor` / `def_per_floor` 仍未接入战力计算（仅 HP + damage_scale 部分路径）；与层数持久化无关。
