# 默认强化里程碑档次值接入管理后台（2026-09-11）

## 需求

`server_enhance_milestones` 原先只配 `attr_type` / `flag`；未洗练数值仍走代码里的品质公式。
现将各档默认数值（按装备品质 Q0..Q4）一并接到管理后台「强化配置 → 默认档词条」。

## 契约

未洗练（`enhance_wash` 空 / `washed=0`）时：

1. 词条类型：配置行 `attr_type` / `flag`（武器 +16 仍按类别映射三围）
2. 数值：配置行 `value_q0..value_q4`，按 `equip.dsh` 品质取档（>4 钳到 Q4）
3. 若五行全 0（未回填），回退旧硬编码公式

洗练覆盖 / 首获 seed 仍用实例 `enhance_wash`，不受本表数值影响。

## 表变更

`server_enhance_milestones` 增加：

| 列 | 含义 |
| --- | --- |
| `value_q0` .. `value_q4` | 品质 0..4 的默认档数值 |

启动时 `ALTER` 补列；空值行按旧公式 seed/backfill。

## 后台

`/?tab=enhance&section=milestones`：每行可改属性、%、Q0..Q4、启用；「一键保存全部」。

## 代码

- `mock_server_enhance_config.inc.c`：加载/seed/upsert/resolve
- `web_admin_enhance.inc.c`：UI + `enhance-milestone-save-all`
- migrate：`server/mysql/migrate_add_enhance_milestone_values.sql`

## 验证

- [ ] `make -j2`
- [ ] 后台改某档 Q2 后，品质 2、未洗练装备详情数值变化
- [ ] 已洗练红字覆盖不受影响
- [ ] 重启后配置仍在
