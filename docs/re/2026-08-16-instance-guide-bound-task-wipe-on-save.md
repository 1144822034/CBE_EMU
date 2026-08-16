# 副本向导保存后「可接取任务」丢失（2026-08-16）

## 触发

动态 NPC 勾选「副本向导（传送／挑战分项）」并绑定「可接取任务」后保存，
后台下拉仍可能短暂显示所选任务，但对话根选项不再出现「接受…」；
`server_dynamic_npc_tasks` 对应行被删空。

## 根因

多任务改造后，落库只认 `seed.boundTasks[]` / `boundTaskCount`：

1. `save-npc` 只写了 legacy `taskId` / `taskRepeatable` / `taskDailyAcceptLimit`
2. `dynamic_npc_admin_save` 先 `DELETE FROM server_dynamic_npc_tasks`，再按
   `boundTaskCount` 重插
3. `boundTaskCount==0` → 不插入任何行 → 绑定被静默清空
4. 随后 `seed_sync_primary_bound_task` 把内存里的 `taskId` 也清成 0

与传送／挑战分项本身无关；任意保存该 NPC（含只改副本字段）都会踩中。

## 修改

`vm_net_mock_dynamic_npc_admin_save`：在写库前把 primary `taskId` 镜像进
`boundTasks`（`taskId==0` 则清空绑定）。

## 验证

1. `make -j2`；重启服务端
2. 对副本向导 NPC 重新选择可接取任务并保存；确认
   `server_dynamic_npc_tasks` 有行，且日志 `bound_tasks≥1`
3. 角色满足前置／日限时，根对话应出现「接受…」，并仍可有传送／挑战分项
