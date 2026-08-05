# 铜雀台白展堂未出现任务接取提示排查（2026-08-03）

## 触发条件

角色 `10001` 进入 `c00蓬莱仙岛_01.sce`（蓬莱-铜雀台），与动态 NPC
`30000`「白展堂」对话。后台已为该 NPC 绑定一个“可接取任务”，但对话只有普通文本，
没有任务接取选项或头顶任务标识。

## 预期与实际

- 预期：满足条件的绑定任务出现在场景任务候选列表中；客户端据此显示头顶标识，并在
  `26/1` NPC 对话响应中收到 `action=4` 的任务接取选项。
- 实际：服务端已加载并匹配白展堂，但 `task_offer=0`、场景任务候选数为零，因此客户端
  正确地只显示普通对话。

## 运行时证据

`bin/server_out.txt` 中同一次进入场景和对话的记录：

```text
mock_scene_npc_catalog ... scene=c00蓬莱仙岛_01.sce ... actors=1 selected=1 rows=1 dynamic=1
mock_task_candidates role=10001 scene=c00蓬莱仙岛_01.sce tasknum=0 ... npc_rows=1 npc_total=1
mock_npc_dialog actor=30000 ... name=白展堂 ... catalog_match=1 ... task_offer=0 ...
```

这排除了场景名不一致、动态 NPC 未下发、actor 未匹配以及 NPC 对话请求未到达的假设。

只读查询当前数据得到：

```text
server_dynamic_npcs:      scene=c00蓬莱仙岛_01, actor_id=30000, name=白展堂, enabled=1
server_dynamic_npc_tasks: scene=c00蓬莱仙岛_01, actor_id=30000, task_id=2, repeatable=0
account_role_tasks:       role_id=10001 没有任务 1 或任务 2 的状态记录
```

按 `web/fs/JHOnlineData/task.dsh` 的真实任务表格式解析：

| 任务 ID | 名称 | 发布者 | 交付者 | 前置任务 |
| --- | --- | --- | --- | --- |
| 1 | 初来乍到 | 大侠郭靖 | 白展堂 | 无 |
| 2 | 关于任务 | 白展堂 | 郭芙蓉 | 1（初来乍到） |

因此当前配置实际绑定的是任务 2。角色尚未完成任务 1，故任务 2 不可接取。

## 服务端与客户端链路

1. 场景初始化的 `25/5 + 6/1 + 6/14` 处理会选择场景动态 NPC，并通过
   `vm_net_mock_task_definition_available()` 过滤其绑定任务，再构造场景任务候选。
2. NPC 对话 `26/1` 由 `vm_net_mock_build_npc_dialog_response()` 使用同一可用性判定；只有
   判定成功时才下发 `action=4` 的任务选项。
3. `vm_net_mock_task_definition_available()` 对带前置任务的定义要求
   `account_role_tasks` 中前置任务状态为 `3`（已完成）。当前任务 2 的前置为 1，条件不
   成立，所以两条路径一致地排除了任务。
4. 客户端 `ParseNPCDialogData`（`0x010380E8`）会解析服务端的选项动作；
   `task_hall_activate_selected_entry`（`0x010492B0`）仅在动作 `4` 存在时进入原生的任务
   接取流程。`scene_refresh_interact_prompt_types`（`0x01017C6C`）同样依赖已下发的场景
   任务候选决定 NPC 的任务标识。

IDA 实例按 `binary_name=江湖OL.CBE` 动态选择；未在代码或本文档中固定实例 ID。

## 根因

这是动态 NPC 的任务绑定与任务前置条件不匹配，不是 NPC 显示、对话协议或客户端提示
逻辑缺失。任务 2「关于任务」是一条任务链的后续任务，服务端按真实任务定义将其排除，
首个错误状态是将尚未满足前置的任务误作“立即可接取任务”绑定。

## 正确处理方式

二选一：

1. 保留当前任务链：先从大侠郭靖接取并完成任务 1「初来乍到」，随后白展堂会正常提供
   任务 2「关于任务」。
2. 需要白展堂现在立即提供任务：在动态 NPC 管理中改绑一个没有前置任务的任务，例如
   任务 1「初来乍到」，或新建一个前置为空的任务。

不应为了让任务 2 立即显示而绕开 `task_definition_available`、伪造已完成状态，或在对话
响应中强行塞入任务选项；这些都会破坏任务链和客户端的场景标识契约。

## 管理界面防误配

已调整 `src/web_admin_server.c` 的动态 NPC 任务下拉框：每个任务现在展示任务 ID、
名称、要求等级，以及（如有）前置任务 ID 和名称；字段标签也明确说明绑定任务仍会校验
等级与前置任务。这只暴露原有任务定义，不改变任务可用性或下行协议。

## 验证状态与风险

- 已通过运行日志、数据库绑定和原始 `task.dsh` 数据交叉确认根因。
- 已完成管理界面静态修改；没有操作运行中的服务端。
- `make -j2` 已成功编译本次服务端源文件，但链接 `bin/jh-online-server.exe` 时被正在运行
  的服务端锁定（`Permission denied`），因此未能完成最终链接；未通过停止或替换运行中
  的服务进程绕过该限制。
- 调整后台绑定后，应由用户手动验证：进入场景即出现相应标识、对话出现接取项、接取后
  标识变为进行中状态、完成前置后后续任务出现。
