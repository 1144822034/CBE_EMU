# 副本向导「可接取任务」前置任务未拦住

## 触发

梦境 `29梦境空间_01` 挑战 BOSS（actor `32901`）：

- 绑定可接取任务 `100005`（`server_tasks.prerequisite_task_id=100004`）
- 同 NPC 还开着副本向导传送／挑战

前置 `100004` 未提交（甚至未接）时，对话仍像可接取：接取对白被换上，且
`lxh001/role=1` 实测在 `100004` 尚无完成行时就写入了 `100005 state=1`
（`100005` 接取时间早于 `100004`）。

## 根因

1. **接取对白未跟选项门闩对齐**：绑定任务在 `state==0` 时无条件换成
   `offerDialog`，即使 `vm_net_mock_task_definition_available()` 因前置未
   `state=3` 已拒绝添加「接受」选项。副本向导同时还有传送／挑战入口，玩家会
   感觉「前置没完成也能显示接取」。
2. **前置判定只信本次请求的状态快照**：`available()` 仅在
   `account_role_tasks` 列表快照里找前置行。快照按 `updated_at` LIMIT，历史很长
   时可能漏掉已完成前置；接取路径也依赖同一函数，需要权威单行读取兜底。

## 修改

- `available()`：快照里已是 `state=3` 则通过；否则对
  `prerequisite_task_id` 做一次 `task_state_load` 权威确认。
- 绑定任务对话：只有 `canOffer==true` 才追加「接受」并切换 `offerDialog`；
  前置未完成时保留 idle／服务对白，并打
  `mock_npc_dialog_task_prereq_gate`。
- 接取拒绝时打 `mock_task_accept_prereq_gate`。

## 验证

- `make -j2`
- 角色无 `100004 state=3`：点 `32901` 不应出现「接受…」，对白不是接取文案；
  伪造 `6/11 task=100005` 应失败并见 `mock_task_accept_prereq_gate`
- 提交完 `100004` 后：应出现接取项，接取成功
