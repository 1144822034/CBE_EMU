# NPC 对话时状态栏 / 挂机 / 多开文字消失

日期: 2026-10-10

## 触发

系统宋体（cn）下与 NPC 对话：地图角「修/拍…」、底栏「挂机」、多开芯片字消失；UC3 点阵时画布字仍在，宋体路径走 DOM 才明显。

## 根因

1. `DialogPanel` 在 `setLcdDomCoverBranches` 里是**硬封面**：`lcdDomOccludedByPopup` 对非对话分支一律隐藏 DOM 字（含 WorldHud、SceneView 状态条）。
2. 对话面板布局本就停在挂机行上方（`hangRowTop - h - gap`），旧注释「状态压在 dim 下」随全屏压暗取消已不适用。
3. `blockPanelPointerPassthrough` 默认只留 `SOFTKEY_H`，挂机/多开仍被 hitArea/dim 挡住。

## 修改

| 位置 | 改动 |
|------|------|
| `main.ts` | `dialog` 加入 `setLcdDomSoftCoverBranches`（与 approach/menu 同） |
| `lcdDomText.ts` | 注释：软封面含 NPC 对话 |
| `DialogPanel.ts` | `reserveBottom: WORLD_BOTTOM_H`（hit + dim） |

## 验证

1. cn 字体：开 NPC 对话 → 修/拍、挂机、多开芯片字仍在；名板可被挡住。
2. 对话中可点挂机 / 多开芯片；点地图走动仍被拦。
3. 打开背包等硬封面：HUD Songti 仍应隐藏（无回归）。
