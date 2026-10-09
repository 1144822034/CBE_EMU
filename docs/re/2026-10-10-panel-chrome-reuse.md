# 面板尺寸不变时复用边框、只改文字/高亮

日期: 2026-10-10

## 问题

打开面板后方向键 / 翻页 / 调数量会整页 `redraw()`：`box.clear()` + `paintCbePanel` / 卷轴贴图 + 拆掉边花再挂，边框闪一下，其实外框 `(x,y,w,h)` 没变。

## 做法

边框与内容分层：

| 层 | 内容 | 何时重建 |
|----|------|----------|
| chrome（`box` / `scrollHost`） | 边框、边花、菜单卷轴 | `chromeKey`（尺寸/模式）变、打开、GL remount |
| content | 选中条、内嵌按钮底 | 每次操作 |
| labels | 标题/正文/选项字 | 每次操作（改字/改色） |

## 已改

- `DialogPanel`：`ensureChrome`；光标/翻页/药品数量跳过 `paintCbePanel`
- `DialogPanel.show` / `showMedicineBuyQty`：已打开时**不再** `remountChrome()`
- `main.ts` `onNpcDialog`：面板已开且同 tallList 时 `keepCursor`
- `main.ts` `keepNpcDialogShell`：**购买该装备 / 买药 / 仓库 / 出售** 等 nested 26/1
  不再先 `dialog.hide()`（原先 hide→等回包→show 必闪边框；chrome 复用帮不上）
- `MenuScreen`：`ensureScrollChrome`；光标/翻页跳过卷轴贴图重建

## 未改（后续）

`SystemPanel` / `SocialPanel` / `listPanelShell` 同理可拆 `content` Graphics，但背包有 Tab、金钱条、详情/列表切换，需按 `kind+mode+bounds` 做 key，改动面更大。

## 验证

1. NPC 对话：↑↓ 选项，边框/边花不闪，高亮与字色变。
2. 对话翻页：边框不动，页码与选项变。
3. 药品 ←→：边框不动，数量/合计字变。
4. 世界菜单 ↑↓：卷轴底不动，选中行与字变。
5. GL 恢复后开对话/菜单：边框仍正常。
