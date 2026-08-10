# 动态 NPC 可绑多个可接任务

Date: 2026-08-09

Status: implemented

```text
phase: admin content + server_dynamic_npc_tasks + 26/1 / 6/14
trigger: one NPC needs several distinct accept bindings
```

## 行为

- 表 `server_dynamic_npc_tasks` 主键改为 `(scene, actor_id, task_id)`，同一 NPC 可有多行。
- 内存 `seed.boundTasks[8]`；`taskId` 等字段仍镜像第一项，供副本向导等单任务调用点。
- 后台「游戏内容」每个 NPC 提供最多 8 行：任务 / 重复 / 每天次数。
- `26/1`、`6/14`、优先级、每日上限查找均遍历全部绑定；对话选项仍受合计 7 槽限制。

## 修改点

- `mock_server_equipment_npc.c`：bound task 结构与 helper
- `mock_server_scene_task.c`：CREATE/迁移 PK、JOIN 合并加载、DELETE+INSERT 保存
- `mock_server_scene_sync.c` / `mock_server_social.c`：遍历绑定
- `web_admin_server.c`：多行表单
- `server/mysql/schema.sql` + `migrate_dynamic_npc_multi_tasks.sql`

## 验证

1. `make -j2`
2. 重启服务（或执行 migrate）；日志可有 `migration=multi-task-primary-key`
3. 后台给同一 NPC 绑两个不同任务并保存；再进图对话应出现两个「接受…」（未超 7 选项时）
4. 旧单绑定行数据保持有效
