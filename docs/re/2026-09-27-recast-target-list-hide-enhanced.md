# 重铸选目标列表仍显示已强化装备

Date: 2026-09-27

Status: fixed (client list filter)

## 现象

持跨品重铸石（或普通重铸）打开强化转移选目标时，背包里**已强化**的同部位装备仍出现在列表中（可带 `(+N)`）。

## 根因

`EnhancePanel.buildTransferCandidates` 使用 `enhance < 源等级`，把 `+1..+(L-1)` 也当作合法目标。产品期望只转移到未强化件，避免覆盖另一件已投入强化的装备。

服务端 `29/5|6` 仍允许 `destLevel < sourceLevel`（含已强化但更低），列表收窄不改变 wire 契约。

## 修改

- `PixiJS_web/src/ui/EnhancePanel.ts`：候选改为 `(enhance | 0) === 0`
- 同步 `docs/re/2026-09-25-cross-quality-recast-stone.md`、`2026-08-07-pixijs-equip-enhance-recast.md`

## 验证

- [ ] 源 `+L` 重铸：列表仅同部位（有石则可跨品质）的 `+0` 装备
- [ ] 同部位已强化件不出现；无候选时提示「无…可转移装备」
- [ ] 选 `+0` 目标确认后源→0、目标→L，跨品质仍扣 855
