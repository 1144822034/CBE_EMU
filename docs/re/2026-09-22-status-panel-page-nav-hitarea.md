# 2026-09-22 属性面板翻页按钮点不了

## 现象

打开「属性」汇总（多页时底栏「上一页 / 下一页」可见），点击翻页无反应；↑↓ 键翻页正常。同类全屏弹层列表（任务 / 社交 / 背包 / 拍卖 / 强化等）若把 pageNav 锚在 `cbePopupBounds()` 底边，也会踩中同一问题。

## 根因

1. 全屏 popup chrome 底边约 `LCD_HEIGHT - 4`，不少面板用 `y + h - N` 放翻页条 → 约 y=350–376。
2. `blockPanelPointerPassthrough` 的 `hitArea` 高度为 `LCD_HEIGHT - SOFTKEY_H = 368`（留给底栏软键）。
3. Pixi 对带 `hitArea` 的父节点：点在 hitArea 外时**整棵子树**不参与命中，因此按钮底部越过 368 的翻页 hit 收不到 `pointertap`。

## 修改

1. `pageNav.ts`：`clampPageNavY` / `panelSafeFootY`；`attachPageNavButtons` 绘制前统一钳制 Y。
2. `listPanelShell.paintListShellBody`：脚栏 `footY` 先 `panelSafeFootY`，列表为翻页条让位。
3. 任务 / 社交 / 强化 / 拍卖 / 对话 / 交易 / 系统背包与属性：脚栏或 tipY 改走 `panelSafeFootY`。

## 验证

- 属性 / 背包 / 任务 / 附近或好友多页 / 拍卖浏览 / 对话长选项：点「上一页 / 下一页」应换页。
- 软键「返回」仍可点；↑↓ 翻页不变。
