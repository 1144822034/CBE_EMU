# 普通复活经验惩罚：当前等级进度 10%

日期：2026-07-26

## 变更

普通战斗失败 / 放弃复活石（`1/7/14 result=2` 及同类普通复活路径）的经验惩罚，由「掉 2 级并落到目标等级起始经验」改为「扣除当前等级已获得经验的 10%」。

## 契约

- 入口：`vm_net_mock_role_apply_death_penalty()`（仅 `role->hp == 0`）。
- 基数：`levelProgress = role.exp - level_start_exp(current_level)`。
- 扣除：`ceil(levelProgress * 10 / 100)`（与金钱惩罚同一 `percent_ceil` 语义；进度为 0 时扣除 0）。
- 结果：等级不变；经验不低于本级起始值（因为只扣进度的一部分）。
- 不变：5% 金钱、30% HP/MP、最近 `n_telestone` 重生。

## 示例

当前等级 5、累计经验 1250（本级起点 1086，进度 164）→ 扣 17 → 剩余 1233，仍为 5 级。

## 修改点

- `src/server/mock_server_core.c`：`VM_NET_MOCK_ROLE_DEATH_EXP_PENALTY_PERCENT = 10`
- `src/server/mock_server_equipment_npc.c`：`vm_net_mock_role_apply_death_penalty()`
- 历史说明见 `docs/re/2026-07-23-battle-death-revival-stone.md`（「掉两级」段落已由本规则取代）
