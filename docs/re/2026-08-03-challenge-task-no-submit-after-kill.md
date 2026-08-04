# 挑战任务击杀后无「提交」选项（2026-08-03）

## 现象

接取梦境绑定击杀任务 `100005`（挑战守关 boss）后，对 actor `32901` 走
「挑战守关怪」并战胜配置怪，再对话仍只有进行中文案，**没有「提交」选项**。

`lxh001/role=1` 权威行：`task_id=100005 task_state=1 progress1=0 progress2=0`。

## 契约与偏离

提交选项门闩（动态 NPC 绑定路径）：

1. `account_role_tasks.task_state == 2`，或
2. `state == 1` 且 `vm_net_mock_task_requirements_met` 为真时，对话内提升为 2
   再挂 `optionSubmits=true`。

击杀类需求（`requirement_type=2`）的完成条件是**持久化进度**达到
`requirement_count`，且 `requirement_id` 必须等于本场胜利的 `enemyId`。

### 偏离 1：配置 ID 不一致

| 来源 | 值 |
|------|----|
| `server_tasks.100005.requirement_id1`（修复前） | **204**（梦魇） |
| `server_dynamic_npc_instances.32901.challenge_enemy_id` | **203**（长枪鬼卒） |

挑战开战打的是 203；进度匹配要求 204 → 永远不算完成。

### 偏离 2：击杀进度绑在掉落发放上（代码）

`vm_net_mock_battle_grant_reward_once` 仅在某条掉落 `grantedCount != 0` 时调用
`vm_net_mock_task_progress_after_battle`。怪物 **203 无掉落行**（
`server_monster_drops` 空、`drop_item_id=0`），胜利结算循环零次进入 →
即便 ID 已对齐，**击杀进度也不会写入**。

附带问题：多掉落怪若每条 grant 都带 kill 累计，同一次胜利会把 type-2
进度重复加多次。

奖励频率限制分支（consolation EXP）原先也不走击杀进度。

## 根因

1. 任务击杀目标与副本向导 `challenge_enemy_id` 配置错位（204 vs 203）。
2. 类型 2 进度被错误挂在「成功发掉落」之后，无掉落胜利不会推进
   `progress`/`state`，对话门闩因此看不到可提交状态。

## 修改

1. **数据**：`server_tasks.100005.requirement_id1` 改为 `203`，与
   `32901.challenge_enemy_id` 一致。
2. **代码**（`mock_server_scene_sync.c` / `mock_server_equipment_npc.c`）：
   - `dropItemId=dropCount=0` 的调用只推进击杀需求；带掉落数量的调用只推进
     收集需求。
   - 掉落循环结束后**固定**调用一次击杀进度更新。
   - 奖励被 rate-limit 抑制时仍推进击杀进度（胜利已发生）。

## 验证

- `make -j2`
- 重启 mock 服务后，角色对 `100005` 保持 `state=1` 时再挑战 `32901` 战胜
  长枪鬼卒（203）
- 期望日志：`mock_task_battle_progress task=100005 ... enemy=203 ... progress=1/1 ... state=2`
- 再点 `32901`：应出现「提交」；DB `progress1=1 task_state=2`

已有 `progress1=0 state=1` 的角色需再打一场才会推进（不伪造进度行）。
