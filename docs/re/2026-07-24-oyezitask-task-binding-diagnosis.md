# 欧冶子“初来乍到”任务绑定诊断

日期：2026-07-24

状态：已确认，无服务端协议修改

## 触发

在蓬莱-铸剑谷触碰欧冶子后，客户端没有出现“初来乍到”的可接取入口。

## 配置与运行时证据

- MySQL `server_dynamic_npc_tasks` 存在精确绑定：
  `scene=00蓬莱仙岛_02.sce`、`actor_id=20020`（欧冶子）、`task_id=1`。
- `server_tasks.task_id=1` 的“初来乍到”处于启用状态，等级要求为 1，
  `prerequisite_task_id=0`。
- 独立服务会话中，未接任务的 `guest00019/10019` 与欧冶子交互时，服务端日志为
  `mock_npc_dialog actor=20020 ... task_offer=1 ... task_option_action=4`。
  这对应客户端 `ParseNPCDialogData(0x010380E8)` 选项以及
  `task_hall_activate_selected_entry(0x010492B0)` 的 `action=4 -> 6/10 -> 6/11`
  原生接取路径。

## 根因

实际测试角色 `guest00023/10023` 已经在 `account_role_tasks` 中保存
`task_id=1, task_state=3`。`vm_net_mock_task_definition_available()` 明确拒绝任何已有
该任务状态记录的角色，已完成任务不会重新提供接取入口。

同一角色场景刷新日志中虽然出现 `6/14 tasknum=1`，但该条是历史测试任务 `900001` 的
候选记录；它不是任务 1。不能根据总数推断“初来乍到”仍可接。

## 处理建议

要重新测试“初来乍到”，请使用尚未完成任务 1 的角色，或在确认需要重复测试后，仅删除
该角色的 `account_role_tasks` 中 `task_id=1` 的完成记录，再重新进入场景使 `6/14` 刷新。
这不是 NPC 配置、对话包或客户端交互协议缺失，因此不应通过重复下发或绕过已完成状态来修复。
