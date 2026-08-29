# 重铸成功后源装备强化等级不刷新为 0

Date: 2026-08-29

Status: fixed (Pixi client merge + 29/6 bag write)

## 触发与现象

背包装备「重铸」（强化转移）确认成功后，目标装备显示新强化等级，
**源装备仍显示原 `(+L)`**；重新登录后源装备才变为 `+0`。

服务端 MySQL / 内存 `enhance_level` 在 `29/6` 成功时已写成 0（重登正确）。

## 业务链路

1. 客户端 `1/29/6 { seqd, seqs }` → 服务端扣费，源→0、目标→L，同包
   `7/7+7/11` 同步两侧 + `10/26` 铜币（见
   `2026-08-02-equipment-enhance-transfer-29-5.md`）。
2. `29/6 result=1` 带 `seqs/curlevels`、`seqd/curleveld`；CBE
   `HandleItemUseAndEquip`→`0x010287C0` 写两侧背包行强化。
3. Pixi：`enhanceTransferConfirm` → `handleBodies`（解析 7/7）→
   `closeEnhanceToBag` → `openBag`（17/1+30/21）。

## 首次偏离

Wire 已正确下发源 `enhance=0`，但本地合并用 `a || b` 把 **0 当成缺失**：

| 位置 | 错误写法 | 效果 |
| --- | --- | --- |
| `mergeBagItemUpdateDeltas` | `u.enhance \|\| prev?.enhance` | 7/7 同步后源仍保留旧 L |
| `applyBagEquipDurability` | `next.enhance \|\| prev.enhance` | `openBag` 再被 prev 毒回 |
| `mergeBag17WithGridSeq` | `hit.enhance \|\| it.enhance` | 17/1-only + 旧袋作 grid 时同病 |

另：Pixi 解析了 `curlevels/curleveld` 却从未写回 `bagItems`（CBE 会写）。

重登无 prev 可合并 → 显示正确 → 与「仅重登才刷新」一致。

## 根因

会话内背包合并把合法的强化等级 `0` 当成「字段缺失」并回退到旧行；
`29/6` 权威等级字段未应用到本地背包。

## 修改

1. `protocolExtras.ts`：7/7 与 17/1↔grid 合并以 wire `enhance` 为准（含 0）；
   `enhance===0` 时不再用 `preferEnhanceAttrs` 复活旧洗练词条。
2. `GameSession.applyBagEquipDurability`：同上；仅在 `wireEnhance>0` 时保留
   截断 extras 的洗练属性。
3. `enhanceTransferConfirm` 成功后 `applyEnhanceTransferBagLevels`：按
   `29/6` 的 `curlevels/curleveld` 写源/目标（对齐 CBE `0x010287C0`）。

## 验证

- [x] `npx tsc --noEmit`（PixiJS_web）
- [x] smoke-protocol：`7/7 enhance=0` 不再 `|| prev`
- [ ] 源 `+L` 重铸到同槽 `+0` 目标：确认后背包源无强化标、目标 `+L`，无需重登。
- [ ] 源若有洗练词条：转移后源清空、目标带走（服务端已 copy；客户端不复活源词条）。
- [ ] 扣费后铜币仍刷新（既有 `10/26` 路径）。
