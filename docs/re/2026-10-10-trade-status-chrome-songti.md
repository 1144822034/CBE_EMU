# 交易时单独隐藏状态栏文字

日期: 2026-10-10

## 触发

交易面板改为 soft cover 后，底栏挂机 / 多开芯片字保留（见 `2026-10-10-trade-hud-hang-switch-songti.md`），地图角「修/拍…」状态栏 Songti、以及 WorldHud 场景标题条（与交易标题同在 y≈2）也会留下来叠在交易标题上。

## 根因

1. 交易面板 `y=2` 铺到挂机行上方，画布已盖住状态条。
2. 软封面默认只挡地图名板，状态栏与 HUD 同级保留。
3. 仓库 tall 列表用全局 `setLcdDomSoftCoverOccludeChrome`，不能在交易开关时抢这个标志。

## 修改

| 位置 | 改动 |
|------|------|
| `lcdDomText.ts` | `setLcdDomSoftCoverChromeOccludeBranches`：顶层软封面在该集合内时隐藏 chrome Songti；`setLcdDomSoftCoverChromeOccludeHosts` 再藏场景标题（`hud.top`），不挡挂机/多开 |
| `main.ts` | 登记 `tradeUi` + `hud.top` |

仓库存入/取回仍走原 `setLcdDomSoftCoverOccludeChrome`。

## 验证

1. cn：开交易 → 无「修/拍」透字、无场景地名叠在交易标题上；挂机 / 多开芯片字仍在可点。
2. cn：短 NPC 对话 → 状态栏 / 挂机 / 多开仍可见。
3. cn：仓库存入/取回 → 仍无「修/拍」透字。
4. 点阵：交易底板已盖住状态条，无额外 DOM 板。
