# 2026-08-10 战斗「物品」无药/壶（背包实际有）

## 现象

战斗菜单选「物品」提示「没有可用药品/壶」，但打开背包能看到神仙壶/回春散等。

## 根因

1. **`parseBagItems` 把 `7/7 type=1` 当整包快照**  
   战斗掉落、仓库取回、商店 peel 等只下发增量 `7/7 type=1` 行。Pixi 若把它当 `bagFull` 整表替换，背包只剩掉落行，药/壶从 `session.bagItems` 消失，`setBattleItems` 随之清空。  
   契约（`2026-08-07-pixijs-system-panels.md`）：`17/1`/`30/21` 才是全量；`7/7 type=1` 只应按 seq 合并。

2. **次要**  
   - 开战仅在 `bagItems.length===0` 时 `openBag`，被冲掉后不会自动拉回。  
   - `openBag` 请求前 `onBag([])` 会瞬时清空战斗物品列表。  
   - `isBattleMedicineOrFlask` 依赖行上 `category`，目录未预热时可能为 0。

## 修改

| 层 | 内容 |
| --- | --- |
| `parseBagItems` | 只认 `17/1`/`30/21`；不再 fallthrough 到 `7/7` |
| `parseBagItemUpdates` | 继续负责 `7/7 type=1` 按 seq 合并 |
| `openBag` | 去掉请求前的空背包 `onBag([])` |
| 开战 | 目录预热；无可用药/壶时强制 `openBag` |
| `isBattleMedicineOrFlask` | 优先 `lookupItemSync` 的类别/HP/MP |

## 验证

- `npx tsx scripts/smoke-protocol.ts`：`7/7 alone not full bag` / flask stale category
- 背包有药或壶 → 进战斗 →「物品」有列表
- 击杀掉落后再开「物品」，药/壶仍在（不被掉落行冲掉）
