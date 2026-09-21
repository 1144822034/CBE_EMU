# Pixi 点阵字体 tip 文字被 DOM 遮罩盖住

## 触发与现象

字体切换为天空点阵 / 全字库 / 经典子集（非「高清字体」系统宋体）时：

- 背包「批量开启」十连成功后，中心 tip 框出现，但「连开N次：…」绿色文字看不到。
- 系统宋体（cn）下同一 tip 正常。
- 短 tip 同样会变成空框，十连文案长、框大，最容易注意到。

## 业务链路

1. `openChestBatch` → `emitItemUseFeedback` → `showGameTip` → `SystemPanel.showTip` / HUD `showPanelTip`。
2. `drawPanelTipOverlay`：canvas 画 tip 底板 + `makePixelText`（UC3 = canvas sprite；cn = DOM Songti）。
3. 此前一律调用 `setLcdDomTipOccludeRect`，在 `#lcd-dom-text` 上垫不透明 DOM 板（z≈1.8M），盖住背后 Songti。

## 首次偏离

DOM tip cover 叠在 WebGL canvas **之上**。cn tip 字在更高 z（≈1.9M）的 DOM 上，能看见；UC3 tip 字在 canvas 上，被同色不透明板整块挡住 → 只剩空框。

## 根因

为宋体「遮住背后字、不整行 `display:none`」加的 DOM 板，在点阵路径下误盖住了 canvas tip 字形。点阵模式面板字本就在 canvas，opaque `paintCbePanel` 已够盖背后，不需要 DOM 板。

## 修改

- `panelTip.ts`：仅 `preferSystemFont()` 时 `setLcdDomTipOccludeRect`，否则 `clearLcdDomTipOccludeRect`。
- `RoleScreen.ts`：删除确认框同样条件。
- `lcdDomText.ts`：注释标明 plate 仅服务 Songti。

## 验证

- [ ] 天空点阵：背包十连开箱，tip 可见「连开10次：…」。
- [ ] 系统宋体：tip 仍可见，背后面板字被板盖住（非整行消失）。
- [ ] 短 tip（如「使用成功」）点阵下也有字。
