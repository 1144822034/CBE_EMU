# 丢弃确认框背包 Songti 透字

日期：2026-10-10

## 现象

背包上叠丢弃/提炼确认时，确认框底板能看见背后的背包宋体字（与按钮位置无关；改底栏前即有）。

## 根因

1. 确认走 `showBagOverlayConfirm`：dialog soft cover，不把背包 Songti `display:none`（框外字要保留）。
2. Songti 在 `#lcd-dom-text`（画布之上）；确认框橄榄底只在 canvas，挡不住 DOM 字 → 透字。

## 修改

| 位置 | 改动 |
|------|------|
| `lcdDomText.ts` | `setLcdDomHostOccludeRect`：确认框区域不透明 DOM 板（z≈1.7M）；该 host 下标签抬到 1.75M |
| `lcdDomText.ts` | host 板激活时：`lcdDomOccludedByPopup` 额外隐藏 chrome/HUD（挂机/多开/状态栏），背包分支仍显示 |
| `DialogPanel.ts` | `setOverlaySongtiPlate`；`ensureChrome` 后按 chrome 矩形同步板 |
| `main.ts` | `showBagOverlayConfirm` / hide 开关 plate |

UC3 路径字在 canvas，确认框 fill 已盖住，不铺板。

## 验证

1. cn 字体：背包 → 丢弃 → 确认框内无背包/挂机/多开/状态栏透字；框外背包列表/金钱字仍可见。
2. 取消/确认后板清除；关确认后若仍开背包，挂机等仍被背包硬封面挡住。
3. 单独 NPC 对话（无 host 板）：挂机/多开/状态栏 Songti 仍应可见。
4. 点阵字体：确认框不铺 DOM 板，仍不透字。
