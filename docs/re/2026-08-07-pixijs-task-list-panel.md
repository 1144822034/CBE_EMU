# PixiJS_web 任务列表接入（已接 / 可接）

Date: 2026-08-07

## 需求

1. 任务列表分「已接」「可接」，可翻页。
2. 可查看详情、放弃任务；放弃时删除背包对应的任务收集物品。

## 协议契约

| 用途 | 请求 | 响应 |
|------|------|------|
| 已接列表 | 空 `6/1` | `6/1 { tasknum, taskinfo }`（与场景 follow-up 同 builder） |
| 可接列表 | 空 `6/14` | `6/14 { action, tasknum, taskinfo }` |
| 详情 | `6/10 { taskid, state }` | `6/10 { info }` |
| 接取 | `6/11` + 空 `25/5` | `6/11 { result, taskinfo? }` |
| 放弃 | `6/7 { taskid }` | `6/7 { result }` + 可选 `7/7+7/11` 背包同步 |

Standalone 列表查询由 `vm_net_mock_build_task_list_query_response` 处理，**不**走场景 task-subset（后者还带 `6/13`/`2/10`/`25/5`）。

`taskinfo` 行布局：

- 已接：`ParseItemDataFields`（`0x01046D24`）— id/diff/class/name/giver/level/receiver/req…/state
- 可接：`DeserializeRoleInfo`（`0x01046E00`）— id/diff/class/name/giver/level

## 放弃与背包

根因：原 `6/7` 只删 `givenItemId×givenItemCount`，不处理 `requirementType==1` 收集物。

修复：`vm_net_mock_task_abandon_consume_items`

1. 接受时发放物：最多扣 `givenItemCount`
2. 类型 1 收集物：扣该 itemId **全部持有量**（与 `6/4` 提交一致）
3. 同包追加 `7/7+7/11` consume sync；Pixi 再 `openBag()` 拉 `17/1`

## Pixi 入口

- HUD 任务图标 / 菜单「玩家信息→任务」→ `TaskPanel`
- ←→ 切换已接/可接；↑↓ 翻页；详情 / 放弃 / 接取

## 验证

- `make -j2` 服务端
- Pixi：打开任务列表应看到已接与当前场景可接；放弃后背包无对应收集物，列表刷新
