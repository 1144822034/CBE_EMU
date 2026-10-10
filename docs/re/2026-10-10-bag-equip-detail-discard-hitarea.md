# 背包装备详情「丢弃」点不了

日期：2026-10-10

## 现象

背包打开装备详情，底栏可见「丢弃」，点击无反应；键盘 ↑↓ 选中后软键「确定」仍可丢弃。

## 根因

与 `2026-09-22-status-panel-page-nav-hitarea.md` 同源：

1. `drawBagEquipDetail` / `drawBagItemDetail` / `drawMallItemDetail` 用 `y + h - PANEL_BOTTOM_PAD`（约 y=392）锚底栏。
2. `blockPanelPointerPassthrough` 的 `hitArea` 高度为 `LCD_HEIGHT - SOFTKEY_H = 368`。
3. 装备操作一页 4 钮时，「丢弃」常落在第 4 行（y≈366+），整钮或部分超出 hitArea；Pixi 对带 hitArea 的父节点在区域外不命中子树，故 `pointertap` 不到。

背包列表脚栏此前已走 `panelSafeFootY`，详情底栏漏改。

## 修改

`SystemPanel.ts`：

1. `detailActionSoftY`：操作条底边 = `LCD_HEIGHT - SOFTKEY_H + 2*(btnH+gap)`（相对安全线整段下移两钮行高；装备约 +40px）。
2. 背包/商城 `mode===actions` 时 `blockPanelPointerPassthrough(reserveBottom:0)`，伸进软键带仍可点。
3. 装备详情仍一页 4 个操作（`EQUIP_ACTION_VISIBLE = 4`）。

## 验证

1. 背包 → 装备详情：底栏较「贴 softkey 安全线」更低约两钮高度，首屏仍 4 操作；点「丢弃」出确认。
2. 道具/商城详情底栏同样下移且可点；列表页 hitArea 仍留 softkey 带。
