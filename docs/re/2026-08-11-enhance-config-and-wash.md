# 强化配置表 + 装备洗练（2026-08-11）

## 范围

1. 管理后台可配强化默认档词条与洗练属性池。
2. PixiJS 玩家可对 +4/+8/+12/+16 已解锁档做洗练。
3. CBE 客户端不接洗练 UI。

## 配置表

### `server_enhance_milestones`

默认档词条：`(equip_slot, unlock_level)` → `attr_type` / `flag` / `enabled` / `value_q0..value_q4`（品质档次数值）。
空表启动时 seed 原硬编码 8 部位 × 4 档（含品质公式展开的 Q0..Q4）。
详见 `2026-09-11-enhance-milestone-default-values-admin.md`。

### `server_enhance_attr_pool`

洗练池：`(attr_type, flag)` → `value_min` / `value_max` / `weight` / `enabled`。
玩家洗练 / 首次获得 seed 时按权重随机属性；**实际上限**默认可按装备 `品质`（0..4）缩放：
`scaled_max = value_max * (quality+1) / 5`（0档 1/5 … 4档满值；>4 按 4）。
后台「洗练质量分档」关闭后一律用完整 `value_max`（见 `2026-09-11-enhance-wash-quality-scale-admin.md`）。
`value_min` 不变；若 min > scaled_max 则塌缩为单点。29/7 下发的 pool 区间同此缩放。

管理入口：`/?tab=enhance`（质量分档开关 / 默认档 / 洗练属性池）。
洗练属性池支持「一键保存全部」（`enhance-pool-save-all`）：AJAX 提交后不整页刷新，仅右下角 toast。

按角色调整实例覆盖：账号管理 → 角色明细「装备洗练」→ `set-equip-wash`
（见 `docs/re/2026-08-29-admin-role-equip-wash.md`）。

## 实例覆盖

背包 / 穿戴 / 仓库 / 拍卖表增加 `enhance_wash` 文本列：

```text
w,t,f,v,c;w,t,f,v,c;w,t,f,v,c;w,t,f,v,c
```

对应 +4/+8/+12/+16：`washed,type,flag,value,wash_count`。
**首次获得**装备（`add_backpack` 且无继承 wash）：按后台 `first_seed_percent`
（默认 **20%**）从洗练池随机四档写入
（`washed=1`、`wash_count=0`）；未命中则保持空 wash，走配置默认 + 品质公式
（见 `2026-09-10-equip-first-obtain-wash-seed.md`、
`2026-09-11-enhance-wash-quality-scale-admin.md`）。
历史空 wash / 管理后台清零仍显示配置默认 + 品质公式；玩家洗练或首获命中随机后用覆盖值。
装备详情：默认档黄字，实例覆盖红字（wire `flag` bit7=washed，PixiJS 解析）。
穿脱、仓存、拍卖、强化转移（29/6）、交易跟随装备实例，不重随。

## 洗练协议（PixiJS，family 29）

| WT | 作用 |
| --- | --- |
| 29/7 | 打开：`seq/curlevel/tiers/pool` |
| 29/8 | 预览：`equipseq/unlock/crystal` → `rate/money` |
| 29/9 | 确认：扣 `count` 玄晶 + 金币；成功则随机新词条并 `wash_count++`，**不立刻写入**，回 `old*`+`attr*` 待玩家确认 |
| 29/14 | 替换确认：`equipseq/unlock/apply`；`apply=1` 写入新属性，`apply=0` 保留当前（费用与次数已在 29/9 扣除） |
| 29/13 | 星空石：重置指定档 `wash_count=0`（见 `2026-09-05-star-sky-stone-wash-reset.md`） |

### 成功率

`unit_rate(unlock, crystal_tier)`：玄晶等级 **>** 档次 → 100%；否则与强化同 `/3` 阶梯。

**+16 档特例（2026-08-22）**：可放入 **1..3 颗十六级玄晶** 叠成功率：

| 数量 | 成功率 |
| --- | --- |
| 1 | 33% |
| 2 | 66% |
| 3 | **100%** |

其它档次 / 非十六级玄晶仍只允许 1 颗。请求字段 `count`（默认 1）；确认时按 `count` 扣晶。

### 金币

同装备同档第 N 次洗练（含失败）消耗 `5*N` 金（铜=`*10000`）。
失败不改词条，仍递增 `wash_count`。
成功后无论玩家「替换」还是「保留」，玄晶与金币均已消耗，且 `wash_count` 已 +1。

### 替换确认（2026-09-06）

成功路径：

1. 29/9 扣费、`wash_count++`、随机新属性，服务端挂起 pending（不改当前词条）。
2. 客户端展示「当前属性 / 新属性」，左软「替换」、右软「保留」。
3. 29/14 `apply=1` 才写入红字覆盖；`apply=0` 或关闭面板时丢弃 pending，保留当前。

### 随机（2026-08-11 取证）

首次成功后再次洗练若看起来「属性/数值不变」，根因是成功判定用了近乎固定的
`tick+seq+unlock` 公式、属性池用了 `libc rand()`，且 7/7 同步曾把洗练覆盖写成默认档。
已改为专用 xorshift（混入 wash_count），7/7 从背包实例带 wash，成功后 arm 17/1 列表同步。

## 代码落点

- `src/server/mock_server_enhance_config.inc.c`：配置加载/seed/admin/roll
- `src/server/mock_server_catalog.c`：里程碑 resolve + common-extra/bonus
- `src/server/mock_server_equipment_npc.c`：29/7–9、29/14
- `src/web_admin_enhance.inc.c`：后台页
- `PixiJS_web/src/ui/WashPanel.ts` + `protocolExtras.ts` 29/7–9、29/14
  - 操作：↑↓选档 · ←→加减十六级数量（+16，最多 3）· 左软洗练 · D-pad OK/中键玄晶 · 右键关闭（无方向键上方中软按钮）
  - 成功后：左软替换 / 右软保留
- migrate：`server/mysql/migrate_add_enhance_wash_config.sql`

## 验证

- [x] `make -j2` 服务端通过
- [ ] 后台改默认档后新装备详情词条变化
- [ ] 未洗练显示默认；洗练成功可同装多档同属性
- [ ] 金币 5/10/15…；高等级玄晶 100%；失败不改词条
- [ ] +16 放 1/2/3 颗十六级 → 预览 33%/66%/100%；确认扣对应数量
- [ ] 成功后弹出当前/新属性；替换后红字生效；保留后仍为原属性；两种都扣玄晶+次数
- [ ] 穿脱/仓存后 wash 仍在；重铸转移带走 wash
