# 强化配置表 + 装备洗练（2026-08-11）

## 范围

1. 管理后台可配强化默认档词条与洗练属性池。
2. PixiJS 玩家可对 +4/+8/+12/+16 已解锁档做洗练。
3. CBE 客户端不接洗练 UI。

## 配置表

### `server_enhance_milestones`

默认档词条：`(equip_slot, unlock_level)` → `attr_type` / `flag` / `enabled`。
空表启动时 seed 原硬编码 8 部位 × 4 档。

### `server_enhance_attr_pool`

洗练池：`(attr_type, flag)` → `value_min` / `value_max` / `weight` / `enabled`。
玩家洗练时按权重随机属性，再在区间取整数值。

管理入口：`/?tab=enhance`（默认档 / 洗练属性池）。
洗练属性池支持「一键保存全部」（`enhance-pool-save-all`）：AJAX 提交后不整页刷新，仅右下角 toast。

## 实例覆盖

背包 / 穿戴 / 仓库 / 拍卖表增加 `enhance_wash` 文本列：

```text
w,t,f,v,c;w,t,f,v,c;w,t,f,v,c;w,t,f,v,c
```

对应 +4/+8/+12/+16：`washed,type,flag,value,wash_count`。
未洗练显示配置默认 + 品质公式；已洗练用覆盖值。
装备详情：默认档黄字，洗练档红字（wire `flag` bit7=washed，PixiJS 解析）。
穿脱、仓存、拍卖、强化转移（29/6）跟随装备实例。

## 洗练协议（PixiJS，family 29）

| WT | 作用 |
| --- | --- |
| 29/7 | 打开：`seq/curlevel/tiers/pool` |
| 29/8 | 预览：`equipseq/unlock/crystal` → `rate/money` |
| 29/9 | 确认：扣 1 玄晶 + 金币；成功随机词条并 `wash_count++` |

### 成功率

`unit_rate(unlock, crystal_tier)`：玄晶等级 **>** 档次 → 100%；否则与强化同 `/3` 阶梯。

### 金币

同装备同档第 N 次洗练（含失败）消耗 `5*N` 金（铜=`*10000`）。
失败不改词条，仍递增 `wash_count`。

### 随机（2026-08-11 取证）

首次成功后再次洗练若看起来「属性/数值不变」，根因是成功判定用了近乎固定的
`tick+seq+unlock` 公式、属性池用了 `libc rand()`，且 7/7 同步曾把洗练覆盖写成默认档。
已改为专用 xorshift（混入 wash_count），7/7 从背包实例带 wash，成功后 arm 17/1 列表同步。

## 代码落点

- `src/server/mock_server_enhance_config.inc.c`：配置加载/seed/admin/roll
- `src/server/mock_server_catalog.c`：里程碑 resolve + common-extra/bonus
- `src/server/mock_server_equipment_npc.c`：29/7–9
- `src/web_admin_enhance.inc.c`：后台页
- `PixiJS_web/src/ui/WashPanel.ts` + `protocolExtras.ts` 29/7–9
  - 操作：↑↓选档 · 左软洗练 · D-pad OK/中键玄晶 · 右键关闭（无方向键上方中软按钮）
- migrate：`server/mysql/migrate_add_enhance_wash_config.sql`

## 验证

- [x] `make -j2` 服务端通过
- [ ] 后台改默认档后新装备详情词条变化
- [ ] 未洗练显示默认；洗练成功可同装多档同属性
- [ ] 金币 5/10/15…；高等级玄晶 100%；失败不改词条
- [ ] 穿脱/仓存后 wash 仍在；重铸转移带走 wash
