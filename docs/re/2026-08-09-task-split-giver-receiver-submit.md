# 发布者/交付者分离时发布者可提交

Date: 2026-08-09

Status: fixed (server)

```text
phase: NPC dialog 26/1 + 6/1 prompt receiver
trigger: task.giver ≠ task.receiver；动态绑定在发布者；交付靠 XSE（已完成）
```

## 症状

- 交付 NPC 无「提交」绑定/选项（或仅靠 XSE 交付分支）。
- 发布者（接取绑定）在任务 `state=2` 时反而出现「提交」。

## 根因

1. `mock_npc_dialog` 动态绑定块在 `matchedSeed->taskId != 0 && state==2` 时无条件挂提交选项，未区分交付归属。XSE 路径本已要求 `ref->completed`。
2. `vm_net_mock_task_prompt_receiver_for_scene` 在目录交付人不在当前场景时，用 `seed.taskId` 绑定名覆盖 `6/1` 交付人；发布者绑定会把黄问号贴到发布者（同 `2026-07-26-huangyaoshi-stale-submit-prompt.md` 一类分裂）。

## 修改

- `vm_net_mock_npc_owns_task_delivery`：仅目录 `receiver` 名匹配或本 NPC XSE `（已完成）` 视为交付者。
- 绑定块：talk 升 `1→2`、「提交」选项、`completedDialog` 均要求 `ownsDelivery`；非交付绑定 `state=2` 仍走进行中文案，并打 `mock_npc_dialog_submit_gate`。
- `6/1` 交付人：giver≠receiver 时禁止用发布者显示名覆盖；XSE completed 覆盖保留。
- NPC 优先级：绑定 `state=2` 仅交付者给 500。

## 验证

1. `make -j2`
2. 任务 giver≠receiver（如 1/2/3/5）：发布者对话 `task_offer` 可接，可提交时无「提交」、日志可有 `submit_gate`；交付者（XSE completed 或同名 receiver）出「提交」。
3. giver==receiver 任务：原绑定提交行为不变。
4. 复测黄药师/黄蓉任务 5：黄药师无黄问号提交，黄蓉场景可提交。
