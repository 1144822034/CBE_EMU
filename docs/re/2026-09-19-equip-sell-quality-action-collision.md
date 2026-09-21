# 按品质出售不显示操作界面

## 触发

使用装备出售凭证(839)打开出售对话框 → 点「按品质出售」→ 品质选择框不出现，直接回到背包。

## 根因

Pixi 客户端对话框 `action` 冲突。`dialog.onPick` 里提炼确认在品质出售之前，且原先无条件处理：

| action | 先写用途 | 后写用途 |
|--------|----------|----------|
| 296 | 确认提炼（`pendingExtract`） | `OPEN_QUALITY_MENU` |
| 297 | 取消提炼 | `OPEN_QUALITY_CONFIRM` |

`pendingExtract` 为空时走 `returnFromExtractConfirm()`：`dialog.hide()` 后打开背包，品质菜单分支到不了。

首次偏离：点「按品质出售」进入提炼取消路径，不是 `dialog.show` 品质选项。

## 修复

- 品质出售改用空闲 action：`320` 品质菜单、`321` 确认入口、`322` 确认出售（`EquipSellBatchAction`）。取消仍复用 `295`。
- 提炼 `296`/`297` 仅在 `extractConfirmOpen` 时处理，避免再吞其它对话框。

## 验证

1. 使用 839 → 点「按品质出售」→ 出现白/绿/蓝/紫/粉选项
2. 选品质 → 确认框 → 确认后 tip「已出售 N 件未强化X装」
3. 背包「提炼」确认/取消仍走提炼，不打开品质菜单
