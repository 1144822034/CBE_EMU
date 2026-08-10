# 2026-08-08 PixiJS 战斗物品：药品/壶 + 队友目标 + 翻页

## 触发 / 预期

战斗菜单「物品」应列出背包中的 **药品（item.dsh 类别 10 且有 HP/MP）** 与 **壶（802/803）**，支持 ↑↓ 翻页；确认后只能选 **存活队友或自己**，再发 WT `1/4/3`，走 `4/6` 播放。

## 根因

Pixi 原先 `battle.onItem` 仅 chrome 提示「待接 4/3」，未接协议与列表 UI。场景背包使用是 `7/1`，与战斗路径不同。

## 修改

| 层 | 内容 |
| --- | --- |
| `buildBattleItemUseRequest` | `1/4/3` `{ index, seq }` |
| `GameSession.battleItemUse` | exchange + handleBodies |
| `isBattleMedicineOrFlask` | 过滤药品/壶 |
| `BattleScreen` | 物品列表分页、`livingAllyWires` 点选、`onItemUse` |
| `main.ts` | 开战/背包同步 `setBattleItems`，接线 `battleItemUse` |

## Mock 备注

`mock_server_battle.c` 药品回复仍按 **自我 wire** 写 `actioninfo`（防敌方 wire 错加血，见 `2026-07-29-battle-medicine-enemy-heal-and-min-damage.md`）。客户端仍发送所选队友 `index`；组队队友真实回血若与 index 不一致，属 mock 契约缺口（`unresolved`），需另开取证后再改服务端。

## 验证

- 战斗 → 物品 → 有药/壶列表；多页时标题 `物品n/m`，↑↓ 翻页
- 单人自动对自己使用；多人先选队友/自己，不可选怪
- 请求为 `1/4/3`，响应含 `4/6`（及可选 `7/11`）
- `npx tsx scripts/smoke-protocol.ts` 含 4/3 builder 断言；`7/7` 不得当全量背包（见 `2026-08-10-pixi-battle-item-bag-wiped.md`）
