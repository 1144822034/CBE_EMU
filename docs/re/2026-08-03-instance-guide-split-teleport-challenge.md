# 副本向导：传送 / 挑战拆成两个对话选项

## 触发 / 需求

后台「副本向导」原先在 NPC 根对话只给一个「副本传送与挑战」入口，点开后再选传送或挑战。需要：

1. 根对话直接分成「传送」「挑战」两个选项；
2. 传送可配置可选前置任务 ID：完成（`account_role_tasks.task_state=3`）后才显示；ID=0 直接显示；
3. 挑战仅在后台配置了非零 `challenge_enemy_id` 时显示。

## 契约

| 根选项 | 显示条件 | action=1 opcode |
| --- | --- | --- |
| 副本传送 | `target_scene` 非空，且（`teleport_require_task_id==0` 或该任务 state=3） | `EB` + actorId → 既有 `30/1` 进图 |
| 挑战守关怪 | `challenge_enemy_id != 0` | `EC` + actorId → 既有 `26/0+30/9` 确认再 `4/10` |

不再在根对话发 `EA` 合菜单；`EA` 子菜单保留兼容（smoke / 旧路径），并套用相同显示门闩。ENTER 路径也会再校验前置任务，避免伪造 `EB` 绕过。

## 数据

`server_dynamic_npc_instances.teleport_require_task_id`（INT UNSIGNED，默认 0）。

迁移：`server/mysql/migrate_add_instance_teleport_require_task.sql`；启动时
`vm_net_mock_dynamic_npc_instances_ensure_teleport_require_task_column` 会 ALTER。

## 修改点

- Seed：`teleportRequireTaskId`
- 后台：副本字段增加「传送前置任务」下拉；文案标明传送/挑战分项
- 对话：`vm_net_mock_build_npc_dialog_response` 对 kind=6 按门闩追加 0~2 个 action=1
- ENTER：前置未完成时返回提示文案，不发 `30/1`

## 验证

- `make -j2`
- 后台：仅配场景 → 对话只有「副本传送」；仅配怪物 → 只有「挑战守关怪」；两者都配 → 两项都有
- 前置任务 ID=N：未完成不显示传送；完成后显示且可进图；伪造 ENTER 被拦截
- 前置 ID=0：有场景即显示传送
