# 动态 NPC 绑定任务「每天可接取次数」

日期：2026-08-03

状态：已实现（编译验证）；需在目标库应用迁移后做接取路径回归

## 需求与边界

与 `repeatable` 一样，这是 **NPC↔任务绑定** 属性，不是全局任务属性。

- 字段：`server_dynamic_npc_tasks.daily_accept_limit`
- `0`：不按自然日限制（行为与改前一致；完成后是否可再接仍只看 `repeatable`）
- `N>0`：同一账号角色在同一 `(scene, actor_id, task_id)` 绑定上，**服务器本地自然日**内最多成功接取 `N` 次
- `N>0` 同时授权完成后重新提供接取（不必另开 `repeatable=1`）；达上限后当天对话/`6/14` 不再显示接取

计数表：`account_role_npc_task_daily`（`account_id, role_id, scene, actor_id, task_id, day_ymd` → `accept_count`）。日界与副本进入日限共用 `vm_net_mock_local_day_ymd()`。

## 链路与契约

1. `26/1` / 优先级 / `6/14`：在 `task_definition_available` 之外再查当日计数；超限则不提供接取。
2. 会话 `taskOfferContexts` 记录本次提供的 `dailyAcceptLimit` 与 `actorId`（`6/11` 本身不带绑定信息）。
3. `6/11` 成功写入任务后对该绑定 `accept_count+1`；自增失败则回滚接取行，避免“接成功却不计次”。

## 修改点

- Schema / 迁移：`server/mysql/schema.sql`、`migrate_add_npc_task_daily_accept_limit.sql`；启动时补列并 `CREATE TABLE IF NOT EXISTS` 计数表。
- 运行时：`mock_server_scene_task.c`（加载/保存/计数）、`mock_server_scene_sync.c`（对话/接取）、`mock_server_social.c`（`6/14`）。
- 后台：动态 NPC 新增/编辑「每天可接取次数」；无绑定任务时不允许非 0。

## 验证

- `make -j2`
- 绑定 `daily_accept_limit=1`、任务可重复或仅靠日限再接：当日第 1 次接取成功并见 `npc_task_daily_inc`；第 2 次对话无接取，伪造 `6/11` 见 `mock_task_accept_daily_gate`
- `daily_accept_limit=0`：行为与改前一致
- 跨本地自然日：计数重置，可再次接取

## 运维

既有库执行：

`server/mysql/migrate_add_npc_task_daily_accept_limit.sql`

或依赖服务启动时的自动补列/建表后重启 mock 服务。
