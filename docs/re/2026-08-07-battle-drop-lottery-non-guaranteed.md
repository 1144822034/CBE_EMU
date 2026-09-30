# 普通掉落改为奖池单抽

日期：2026-08-07

## 触发条件

怪物可配置多行掉落。此前每行每场独立投掷，多条低概率装备/消耗品叠在一起时，
“本场掉到任意一件”的体感明显高于单行配置的百分比。

## 预期 / 实际 / 首次偏离

- 预期：任务材料与 100% 保底行仍可多行并存；其余普通掉落每场只竞争一次。
- 实际（改前）：所有合格行各自 `roll(rate)`，两条 1% 约等于 ~2% 出件机会。
- 偏离点：`vm_net_mock_battle_grant_reward_once` 对 `configuredDrops[]` 全表
  独立投掷。

## 根因

多行独立伯努利试验会抬高“至少掉一件”的并集概率；这与后台把多条稀有掉落
配在同一怪物上的常见用法冲突。

## 修正

在 `vm_net_mock_battle_grant_reward_once`（`mock_server_equipment_npc.c`）：

1. 对每行先做任务材料资格判定（既有 `vm_net_mock_task_material_drop_policy`）。
2. **独立投掷（multi）**：任务材料行，或 `ratePercent >= 1000`（percent×10 的 100%）。
3. **奖池单抽（lottery）**：其余合格行放入池子；每场用
   `vm_net_mock_battle_reward_rand()` 均匀抽一条，再按该条配置概率投掷一次
   （`rand % 1000 < rateX10`，支持 0.1%）。
4. 未抽中的奖池行记 `mode=lottery_skip`，不投掷、不发放。

日志：

- `mock_battle_drop_lottery enemy=… pool=… pick_slot=…`
- `mock_battle_drop_gate … mode=multi|lottery|lottery_skip|ineligible …`

后台怪物管理提示文案已同步。

## 验证

- 复测多行普通掉落：同场最多只有一条 lottery 行进入 `selected=1`。
- 接取收集任务时：材料行仍可与 lottery 抽中行同场并存。
- 配置 `rate=100` 的非材料行：仍 `mode=multi` 且命中必发。
- `make -j2` 编译通过。
