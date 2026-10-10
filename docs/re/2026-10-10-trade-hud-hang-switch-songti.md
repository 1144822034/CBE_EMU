# 交易时挂机 / 多开芯片文字消失

日期: 2026-10-10

## 触发

系统宋体（cn）下进入玩家交易面板：底栏「挂机」、多开账号芯片字消失；UC3 点阵时画布字仍在，宋体路径走 DOM 才明显。热区此前已留空，但看不见字。

## 根因

1. `TradePanel` 布局与 hit/dim 已 `reserveBottom: WORLD_BOTTOM_H`，本意是底栏可点。
2. `tradeUi` 只在 `setLcdDomCoverBranches`（硬封面）：`lcdDomOccludedByPopup` 对非交易分支一律隐藏 DOM 字，含 WorldHud 挂机/多开。
3. 与 NPC 对话同类问题（见 `2026-10-10-npc-dialog-hud-songti.md`），缺 soft-cover 登记。

## 修改

| 位置 | 改动 |
|------|------|
| `main.ts` | `tradeUi` 加入 `setLcdDomSoftCoverBranches`（与 approach/menu/dialog 同） |
| `lcdDomText.ts` | 注释：软封面含交易面板 |

`TradePanel` 的 `reserveBottom` / `tradePanelBounds` 未改。

## 验证

1. cn 字体：开交易 → 挂机、多开芯片字仍在；地图名板可被挡。
2. 交易中可点挂机 / 多开芯片；点地图走动仍被拦。
3. 打开背包等硬封面：HUD Songti 仍应隐藏（无回归）。
4. 状态栏「修/拍」在交易中单独隐藏：见 `2026-10-10-trade-status-chrome-songti.md`。
