# 2026-09-11：提交任务提示「功能暂未实现(1/25/5)」后黄问号分裂

## 触发

1. 交任务（对话「提交」或任务面板）→ 聊天出现 `功能暂未实现(1/25/5)`。
2. 服务端日志：`unhandled wt=25/5 len=29 … first=1/25/5:0,1/6/4:15`。
3. 头顶可提交黄问号仍在；再点该 NPC **无「提交」**；退出对话后黄问号消失。

与 `server_dynamic_npc_tasks` 外键 `ALTER`（指向 `server_dynamic_npcs` CASCADE）**无关**：提交读的是 `account_role_tasks` + `server_tasks`。

## 根因

上行是已实现的提交复合包 `25/5(空) + 6/4{taskid}`。  
`vm_net_mock_build_task_response` 在识别该形后若对未知任务 / 无角色 / **`awardinfo` 编码失败** 等路径 `return 0`，分派落到
`unimplemented-handler-notice`，tip 按**首对象**写成 `(1/25/5)`。

首次偏离：`commit_reward` 可能已把 `task_state` 写成 3（并改背包），但没有下发合法
`6/4 result=1`：

| 观察 | 解释 |
|------|------|
| tip `1/25/5` | 首对象是条幅 `25/5`，不是“场景 25/5 未实现” |
| 对话无「提交」 | `26/1` 读库，state 已非 2 |
| tip 后黄? 仍在 | 客户端未走 case-4 清活动任务；提交成功才 `arm` 的 `6/1+6/14` 没发出 |
| 退出对话黄? 消 | `mock_npc_dialog` 末尾 `arm_task_prompt_refresh` 补发列表 |

日志里 `objects=1` 是误报：打印的是 `request[4]`（首对象 major），不是对象个数。

## 修改

`vm_net_mock_build_task_response` subtype 4：

- 一旦匹配 `6/4`（含 `25/5` 前缀），**必须**回 `6/4 result`，禁止因目录缺失等直接 `return 0`。
- `awardinfo` 失败时回退为仅 exp/money、itemcount=0，仍 `result=1` 并 `arm` 提示刷新。
- 编码仍失败时降级为 `result=0` 短应答，避免 unimplemented tip。
- 失败原因打 `mock_task_commit_miss` / `mock_task_awardinfo_fallback`。

## 验证

1. `make -j2`；重启服务端。
2. 可提交任务点提交：应见「任务提交成功」或「任务提交失败」，**无** `功能暂未实现(1/25/5)`。
3. 成功路径：黄? 在提交后随 `task_prompt_refresh` 消失，无需再进对话。
4. 日志应有 `mock_task action=commit …`，不应再有同包 `unimplemented_handler_notice … 25/5`。
