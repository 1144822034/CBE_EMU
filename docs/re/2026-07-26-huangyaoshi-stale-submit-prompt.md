# 黄药师提交后仍显示黄色问号

日期：2026-07-26

## 结论

- 对话侧已正确：提交任务 3/4 后，`mock_npc_dialog` 对黄药师
  `task_offer=0`、无提交选项。
- 头顶黄色问号来自 `6/1` 活动任务交付人字符串与 NPC `node+68` 匹配且
  `state=2`（`scene_refresh_interact_prompt_types` 写类型 1）。
- 根因：`vm_net_mock_task_prompt_receiver_for_scene` 在原始交付人不在当前场景时，
  把 XSE 的 `（未完成）`（`ref->active`）也当成可覆盖交付人的依据。
  `task3.xse`（黄药师）对任务 5「东邪之女」只有 offer/active，没有 completed；
  真正的交付分支在 `task4.xse`（黄蓉，`c00蓬莱仙岛_03`）。
- 接取或列表下发时，任务 5 的 `6/1`/`6/11` 交付人被写成「黄药师」；之后状态到 2
  就会在黄药师头顶亮黄问号，而对话仍要求黄蓉的 `（已完成）` 分支，二者分裂。

## 运行时证据（192.1.1.3 / lxh001）

| 任务 | 状态 | giver | receiver |
|------|------|-------|----------|
| 3 拜访前辈 | 3 | 郭芙蓉 | 黄药师 |
| 4 救命之药 | 3 | 黄药师 | 黄药师 |
| 5 东邪之女 | 2 | 黄药师 | 黄蓉 |

- 黄药师：`00蓬莱仙岛_02` / `task3.xse`
- 黄蓉：`c00蓬莱仙岛_03` / `task4.xse`
- `task3.xse` 标记：拜访前辈 completed；救命之药 offer/active/completed；
  东邪之女 offer/active（无 completed）
- `task4.xse` 标记：东邪之女 completed

客户端契约见 `docs/re/2026-07-20-task-submit-and-npc-prompts.md`：
`6/14`→类型 2；`6/1` state1→类型 3；`6/1` state2→类型 1。

## 修改

1. XSE 回退覆盖交付人时只认 `ref->completed`，与对话提交选项同一契约。
2. 提交成功（`6/4 result=1`）后挂一次 `task_prompt_refresh`，下一轮场景同步补发
   `6/1+6/14`。
3. NPC 对话响应后同样挂刷新，用于清掉会话里已被错误交付人名污染的活动行。

## 验证

- `make -j2`（server 目标）通过。
- 复测：角色保留任务 5 为 state=2，站在黄药师旁重新对话或提交任意任务后，
  日志出现 `task_prompt_refresh_arm` / `task_prompt_refresh_deliver`，
  黄药师头顶黄问号消失；黄蓉所在场景仍应对任务 5 显示可提交问号。
