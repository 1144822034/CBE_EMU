# 2026-09-11：拖动 NPC 坐标误走完整保存 / 绑定被清

## 触发

管理后台地图拖动 NPC 保存坐标后：

- 后台可能报保存失败（外键 / MySQL），或
- 进游戏交任务仍见 `功能暂未实现(1/25/5)` / 头顶与对话分裂

此前执行的 `server_dynamic_npc_tasks` → `server_dynamic_npcs` CASCADE 外键
**本身不是 tip 根因**；拖动保存却会改写任务绑定表，放大 FK 与绑定丢失。

## 根因

`save-npc-position` 旧路径调用完整 `dynamic_npc_admin_save`：

1. `DELETE FROM server_dynamic_npc_tasks` 再按 `boundTasks[]` 重插
2. 若 `taskId==0` 却已有 `boundTasks`，旧逻辑先 `boundTaskCount=0` 清空再同步 →
   **每次拖动静默清掉可接取任务绑定**
3. 客户端再提交时仍发 `25/5+6/4`，与「提交应答 return 0 → tip」问题叠在一起

首次偏离：坐标拖动不应改写任务绑定行。

## 修改

1. 新增 `vm_net_mock_dynamic_npc_admin_save_position`：只
   `UPDATE server_dynamic_npcs SET pos_x,pos_y`，不碰 tasks/instances。
2. `save-npc-position`：已落库 NPC 走 pos-only；纯 SCE 内建首次拖动仍完整
   `admin_save` 以创建 `server_dynamic_npcs` 行。
3. `admin_save`：`taskId==0` 且已有 `boundTasks` 时只 `sync_primary`，不再清空绑定。

相关：`docs/re/2026-09-11-task-submit-25-5-unimplemented-stale-prompt.md`（提交 tip）。

## 验证

1. `make -j2`；重启服务端。
2. 拖动已落库 NPC：后台成功；日志
   `dynamic_npc_save_position … action=pos-only`；
   `server_dynamic_npc_tasks` 行数不变。
3. 该 NPC 任务绑定仍在；交任务无 `功能暂未实现(1/25/5)`（需同时带上提交应答修复）。
