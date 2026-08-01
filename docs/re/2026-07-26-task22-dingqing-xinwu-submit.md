# 定情信物（任务 22）有锦帕仍不能提交

日期：2026-07-26

## 结论

- 任务 22「定情信物」：瑛姑发放、周伯通交付；`task.dsh` 发放道具与交付要求均为
  物品 20「锦帕」×1（`requirementType1=1`）。
- 周伯通脚本 `task16.xse` 含 `！ 定情信物（已完成）`；对话侧交付分支契约正确。
- 根因：道具类任务把「可提交」绑在持久化 `progress1/2` 与 `task_state=2` 上，而
  `progress` 只在战斗掉落路径递增。接取发放的锦帕只进背包，不写进度，状态一直
  停在 1 → NPC 无提交选项，`6/4` 也因非 state=2 失败。
- 同类任务：23 顽童（蚱蜢）、24 踏上征途、85 哪个更好？。

## 证据

| 来源 | 内容 |
|------|------|
| `task.dsh` 行 22 | giver=瑛姑，receiver=周伯通，given=20×1，req type1 id20 count1 |
| `00蓬莱仙岛_03.sce` | 周伯通 `n_boy` → `task16.xse`；瑛姑 `n_girl` → 同场景 |
| `task16.xse` 解码 | completed=`定情信物`；offer/active=`顽童` |
| `task15.xse` 解码 | offer/active=`定情信物`；completed=`顽童` |
| 服务端原逻辑 | `requirementsDone = progress >= count`；提交要求 `state==2` |

首个偏离：接取 `6/11` 成功发放锦帕后，`account_role_tasks.progress1` 仍为 0、
`task_state` 仍为 1。

## 修改

1. 类型 1（道具）需求以背包持有量为权威；类型 2（击杀）仍用持久化进度。
2. `vm_net_mock_task_sync_item_progress`：用背包回填 type-1 进度，并在满足条件时
   将 state 提升为 2（丢弃道具时可降回 1）。
3. 调用点：接取发奖后、`6/1` 活动列表、NPC 对话交付判定、`6/4` 提交前。

## 验证

1. `make -j2`。
2. 角色接取定情信物后背包有锦帕：`account_role_tasks` 应为
   `task_id=22, task_state=2, progress1=1`；日志含 `mock_task_item_progress_sync`。
3. 对周伯通对话出现提交选项；`6/4` 成功，锦帕消耗，任务 state=3。
4. 回归：纯击杀任务进度仍只靠战斗；无锦帕时不得提交。
