# 装备栏部位图（0.gif–7.gif）

日期：2026-09-19

相关：`2026-08-07-pixijs-system-panels.md`、`2026-08-10-pixi-jhonlinedata-ui-chrome.md`

## 需求

CBE 装备信息在左右槽与中心立绘之间有部位字标（青字「帽/衣/披/束/裤/鞋/饰品/武器」）。Pixi 此前只画了 `handkuang` + `equipico`，红框空带未渲染。

## 资源

| 文件 | 尺寸 | 字 |
|------|------|----|
| `0.gif`–`5.gif` | 13×13 | 帽衣披束裤鞋 |
| `6.gif`–`7.gif` | 25×13 | 饰品、武器 |

索引与 `EQUIP_SLOT_UI` 视觉顺序一致（左列上→下，再右列上→下），**不是** body slot id（武器=slot0 对应 UI 末位 → `7.gif`）。

## 修改

- `ItemArt.ts`：`warmEquipArt` 预取 `0.gif`..`7.gif`；`equipPartLabelTextureSync`；GPU strip 时一并清空。
- `SystemPanel.drawWornEquipPanel`：每槽内侧（朝立绘）贴对应部位图；立绘 `maxW` 预留两侧最宽字标宽度。

## 验证

1. 打开装备信息：左四槽内侧见帽衣披束，右四槽内侧见裤鞋饰品武器。
2. 穿戴/卸载后字标仍在；GPU resume 后字标能重载。
