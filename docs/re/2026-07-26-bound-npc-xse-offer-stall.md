# 任务提示与对话不一致：动态绑定屏蔽 XSE 可接选项

## 环境

- MySQL / 服务：`192.1.1.3`
- 账号：`lxh001`，`role_id=1`（蔡文姬），场景 `c00蓬莱仙岛_01`

## 症状

- `6/1`：`tasknum=0 persisted_active=0`（没有进行中任务）
- `6/14`：`tasknum=1`（头顶仍有可接感叹号）
- 提交 NPC 无提交选项；接任务 NPC 点开也接不了

## 权威数据

```text
account_role_tasks: task_id=100003 state=3（新人必读，已完成）
server_dynamic_npc_tasks:
  c00蓬莱仙岛_01 / actor=30005 大侠郭靖 → task_id=100003 repeatable=0
大侠郭靖 script_name=task0.xse（XSE 仍含「初来乍到」task 1 的 offer）
```

玩家**没有** state=1/2 的活动任务；“已经接了”对应的是已完成的 `100003`，
或把 `6/14` 上另一条可接候选误当成已接任务。无活动任务时不显示提交是符合契约的。

## 根因

1. `6/14` 候选会扫描动态绑定 **和** XSE offer：`100003` 已完成后仍可从
   `task0.xse` 推出 task 1 → `tasknum=1`，NPC 名匹配出感叹号。
2. `mock_npc_dialog` 旧逻辑仅在 `matchedSeed->taskId == 0` 时走 XSE 选项；
   大侠郭靖绑定了 `100003`，对话只评估绑定任务（已完成且不可重复）→
   **不出现接取选项**。
3. 首个偏离：同一 NPC 上「候选提示」与「对话选项」使用了两套不一致的任务源过滤。

## 修改

去掉「有动态绑定时跳过 XSE」的条件；XSE offer/submit 与绑定任务一并合并，
按 task id 去重。绑定块原有逻辑保留。

## 验证

1. 重启 `192.1.1.3` 上的服务端。
2. `lxh001` 进蓬莱点「大侠郭靖」：应出现「初来乍到」接取（`task_offer=1` /
   `task_option_action=4`），`6/11` 后 `account_role_tasks` 出现 `task_id=1 state=1`。
3. `6/1` 应含该活动任务；白展堂侧在可提交时应出提交选项。
4. 已完成的 `100003` 仍不重复出现（`repeatable=0`）。
