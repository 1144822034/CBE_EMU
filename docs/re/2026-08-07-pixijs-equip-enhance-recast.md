# 2026-08-07：PixiJS 装备强化 / 重铸（强化转移）

phase: pixi-equip-enhance-recast  
status: implemented (client UI; server already live)

## 目标

复刻江湖 CBE 背包装备详情里的「强化」「重铸」：材料选择、成功率预览、确认强化、同类装备强化转移。服务端契约已实现（`builtin-equipment-enhance` / `builtin-equipment-enhance-transfer`）。

## 客户端流程

### 强化

1. 背包装备详情 →「强化」→ `1/29/1 {seq}` 打开，解析 `curlevel/maxlevel/data1/data2`。
2. `EnhancePanel` 主界面：名称、`[强化]: L L+1`、属性对比、成功率、5 个玄晶槽、提交/放置/取消。
3. 「放置」→ 玄晶列表（901–916，显示 `N级玄晶 (+rate%) Xcount`）→ 每次放入一颗，最多五颗。
4. 放入后发 `1/29/2` 刷新成功率与金钱；「提交」发 `1/29/3`。
5. 成功/失败 tip；成功可继续强化；失败掷骰仍清槽并刷新背包。

### 重铸（强化转移）

1. 已强化装备详情 →「重铸」→ 当前装备为 **源**（`seqs`）。
2. 列出同 `equipSlot`、强化等级更低的背包目标 → 选中后 `1/29/5` 预览。
3. 确认界面显示 `48金65银` → `1/29/6` 确认转移。

## 关键文件

- `PixiJS_web/src/ui/EnhancePanel.ts`
- `PixiJS_web/src/net/protocolExtras.ts`（`29/1..3`、`29/5..6` 构建/解析）
- `PixiJS_web/src/game/GameSession.ts` / `main.ts`
- `PixiJS_web/src/ui/equipDetail.ts`（强化对比行）

## 契约摘要

| 阶段 | WT | 要点 |
|------|-----|------|
| 打开 | 29/1 | `seq`；响应 rate 表 |
| 预览 | 29/2 | `equipseq` tagged-u16 + `occultinfo` 9×n raw |
| 确认 | 29/3 | 同 29/2；result 1/2 + 同步 |
| 转移预览 | 29/5 | `seqd/seqs`；`money=7`；不扣费 |
| 转移确认 | 29/6 | 扣 486500 铜；源→0 目标→L |

## 验证

- [x] `npx tsc --noEmit`（PixiJS_web）
- [ ] 背包强化：放入玄晶 → 成功率变化 → 提交成功/失败 tip
- [ ] 重铸：选同类目标 → 确认 → 源 +0、目标 +L、扣费
- [ ] 穿戴栏不再提供强化（须先卸下到背包）
