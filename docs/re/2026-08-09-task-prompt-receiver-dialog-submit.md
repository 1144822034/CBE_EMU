# 交任务 NPC 有黄问号但对话无「提交」

Date: 2026-08-09

Status: fixed (server)

```text
phase: NPC dialog 26/1 vs 6/1 prompt
trigger: giver≠receiver；交任务 NPC 头顶 type=1 黄?；对话无「提交*」选项
```

## 症状

- PixiJS_web / CBE：交任务 NPC（与接取 NPC 不同）头顶已显示可提交黄问号。
- 与该 NPC 对话后选项列表没有「提交…」。

## 根因

两条路径契约不一致：

| 路径 | 条件 |
|------|------|
| 头顶黄 `?`（Pixi `computeNpcTaskPrompts` / CBE `scene_refresh_interact_prompt_types`） | `6/1` 交付人字符串 == NPC 显示名 且 `state==2` |
| 对话「提交」（`mock_npc_dialog`） | 仅 XSE `（已完成）` **或** 动态绑定且 `ownsDelivery` |

交任务 NPC 常是目录 `receiver` 同名角色：`6/1` 写对交付人名后头顶正确，但该 NPC 既无交付绑定、XSE 也未挂该任务的 `（已完成）` 时，`26/1` 不会挂 action=4 提交行。

Pixi 只解析并展示服务端选项，不会本地注入「提交」；分裂发生在 mock `26/1` 构造层。

相关前序：`2026-08-09-task-split-giver-receiver-submit.md`（禁止发布者误挂提交）收紧绑定门闩后，目录交付人若只靠同名、无 XSE/绑定，缺口更明显。

## 修改

- `vm_net_mock_npc_matches_task_prompt_receiver`：用与 `6/1` 相同的 `vm_net_mock_task_prompt_receiver_for_scene` 判断本 NPC 是否为提示交付人。
- `mock_npc_dialog` 在 XSE/绑定循环之后增加 prompt-receiver 扫描：已接 `state` 1/2 且名匹配时，可 talk 升到 2，并挂「提交」；日志 `mock_npc_dialog_prompt_receiver_submit`。
- 发布者（非 prompt receiver）仍不挂提交，与 split-giver 修复兼容。

## 验证

1. `make -j2`
2. giver≠receiver、任务 `state=2`、交任务 NPC 头顶黄?：对话应出现「提交…」，日志可有 `prompt_receiver_submit`。
3. 同场景接取 NPC：可提交时仍无「提交」，可有 `submit_gate`。
4. giver==receiver 且原 XSE/绑定提交路径不变。
