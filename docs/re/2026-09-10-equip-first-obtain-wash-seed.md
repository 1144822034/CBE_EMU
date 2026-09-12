# 装备首次获得时随机四档里程碑（不计洗练次数）

## 需求

四档里程碑（+4/+8/+12/+16）在「未洗练」装备上：
玩家**第一次获得**该装备实例时，按后台 `first_seed_percent`（默认 **20%**）
从洗练属性池随机一次并写入实例，未命中则仍用配置默认 + 品质公式。

约束：

- 命中随机时 **不增加** `wash_count`（首次洗练费用仍从 5 金起算）
- 交易、穿脱、仓库、拍卖等转移路径**不重随**，属性随实例走

## 根因 / 契约

此前 `enhance_wash` 为空（`washed=0`）时，`equip_enhance_resolve_milestone`
回退到 `server_enhance_milestones` + 品质公式，同名同品装备词条完全一致。

权威归属应是**装备实例**上的 `enhance_wash`，与玩家主动洗练（29/9→29/14）共用覆盖格式。
首获是否写入覆盖由 20% 门闩决定；未命中则保持空 wash，与「默认配置」同路径。

## 修改点

1. `vm_net_mock_enhance_wash_seed_initial`（`mock_server_enhance_config.inc.c`）  
   先 `rand_below(100)`：`< first_seed_percent`（后台可配，默认 20）才对四档各
   `roll_attr`（`washed=1`，`washCount=0`）；否则 clear 后返回 false。

2. `vm_net_mock_role_add_backpack_item_to_role` 装备分支（`mock_server_role.c`）  
   - `wash == NULL`（新掉落/商店/任务等首次入包）→ 尝试 seed；仅成功时 upsert  
   - `wash != NULL`（交易/穿脱退回/仓存/拍卖领取等）→ 原样拷贝，不重随

## 验证

- [ ] 大量新获得：约 2 成 `enhance_wash` 为 `1,t,f,v,0`；约 8 成空/默认黄字
- [ ] 命中随机：洗练次数仍 0，首次预览 5 金；日志 `enhance_wash_seed_initial`
- [ ] 交易 / 穿脱 / 仓存后四档与次数不变
- [ ] 历史空 wash 旧装仍走配置默认

## 仍未知 / 风险

- 库内已有 `washed=0` 的旧装备不会自动补随机；若需迁移需另开任务。
- 属性池为空时 seed 失败，仍回退配置默认。

## 续：随机池上限按品质缩放（2026-09-10）

`enhance_wash_roll_attr` / 29/7 pool wire：`scaled_max = value_max * (Q+1) / 5`，
品质档 0→1/5 … 4→5/5（>4 钳到 4）。首获 seed 与玩家洗练共用。
详见 `vm_net_mock_enhance_wash_pool_scaled_max`。
后台可关：`server_enhance_wash_settings.quality_scale_enabled`
（见 `2026-09-11-enhance-wash-quality-scale-admin.md`）。
