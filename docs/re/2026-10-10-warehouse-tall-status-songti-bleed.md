# 仓库 tallList 状态栏 Songti 透字

日期：2026-10-10

## 现象

系统宋体（cn）下打开个人仓库存入/取回列表（或装备出售 tallList）：面板盖住地图角状态条，但「修/拍…」字仍叠在仓库底板之上。

## 根因

1. NPC `DialogPanel` 是 soft cover：故意保留状态栏 / 挂机 / 多开 Songti（短对话面板在条带下方）。
2. `tallList` 面板 `y=18`，画布橄榄底盖住状态条，但 Songti 在 `#lcd-dom-text`，canvas 挡不住。
3. 背包丢弃确认已有 host 遮挡板，仓库 tallList 未启用。

## 修改

| 位置 | 改动 |
|------|------|
| `lcdDomText.ts` | `setLcdDomSoftCoverOccludeChrome`：tall 列表隐藏状态栏 Songti，**不**铺 DOM 板 |
| `DialogPanel.ts` | tall 开/关同步上述标志；背包确认仍用 host 板 |
| 说明 | 曾用 host 板挡状态栏会盖住 canvas 选择条，改为只 `display:none` 修/拍字 |

## 附：tall→短对话大底板残留

存入/取回列表软键返回仓库主界面后，大底板与小对话板叠在一起。

根因：`paintCbePanel` / `paintCbeTitledChrome` 重绘前未 `box.clear()`，
`drawCbePanel` 只追加几何；tall 大 fill 留在 Graphics 里，短面板画在其上。

修改：`cbePanelCorners.ts` 两处 paint 入口先 `box.clear()`。

## 验证

1. cn：开仓库存入/取回 → 面板内无「修/拍」透字；底栏挂机/多开字仍在。
2. 存入/取回列表 → 返回 → 仅小对话板，无残留大底板。
3. cn：短 NPC 对话 → 状态栏 / 挂机 / 多开仍可见（无回归）。
4. cn：背包 → 丢弃确认 → 确认框内无透字；框外背包字仍在。
5. 点阵字体：无 DOM 板，canvas 填充已盖住状态条。
