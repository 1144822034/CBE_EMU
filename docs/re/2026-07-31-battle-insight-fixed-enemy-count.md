# 战斗心得：遇怪数量固定为 3

## 需求

战斗心得（item 828）生效期间，场景遇怪 / 挂机开战的怪物数量不再按 `1..3` 随机，固定为 3。

## 契约位置

- 掷点入口：`vm_net_mock_battle_roll_enemy_count`
- 调用方：`mock_challenge_battle_start` / `mock_hangup_battle_start`（仅 `useSceneMonsterStart` 路径会掷点；非场景 subtype-10 仍为 1）
- 激活判定：与经验加成 / 自动修装相同，`vm_net_mock_role_active_battle_exp_bonus_percent(role) != 0`

## 优先级

1. 非场景开战 → 1
2. `CBE_BATTLE_ENEMY_COUNT` 环境强制 → 该值（autotest）
3. 战斗心得激活 → **3**
4. 否则 `CBE_BATTLE_ENEMY_COUNT_MIN/MAX` 随机

## 修改点

- `src/server/mock_server_equipment_npc.c`：`vm_net_mock_battle_roll_enemy_count`

## 验证

1. 使用 828 后挑战/挂机开战：日志 `mock_battle_enemy_count_fixed ... count=3 reason=battle-insight`，且 `mock_challenge_battle_start` / `mock_hangup_battle_start` 的 `enemies=3`。
2. 心得过期后恢复 `1..3` 随机。
3. 未激活心得时行为不变。
4. `make -j2 server`。
