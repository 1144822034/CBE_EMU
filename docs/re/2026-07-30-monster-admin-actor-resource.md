# 怪物管理：目录 Actor 字段

日期：2026-07-30

## 需求

后台「怪物管理」可配置战斗用 `.actor` 资源；开战时从目录/覆盖表读取，不再只依赖 SCE 扫描。

## 契约

| 优先级 | 来源 |
|--------|------|
| 1 | `server_monsters.actor_resource`（管理保存后的覆盖） |
| 2 | `server_monster_catalog_extra.actor_resource`（自定义目录） |
| 3 | SCE / automonster 扫到的 `actorResource` |
| 回退 | 显示名 + visual `0/0`（勿用 `0/1`，易落到女天机） |

**开战包契约（2026-07-30）**

| 试过的 wire | 结果 |
|-------------|------|
| name=`e_boar.actor` | 闪退 `pc=0x01004e1c` |
| name=野猪 + visual=`0/0` | **同样闪退** `pc=0x01004e1c`（空肖像） |
| visual=SCE field-16（野猪 `5/0`） | 显示**男鬼道**（可进战） |
| visual=`0/1` | 女天机（可进战；历史默认） |

subtype-10 左侧 visual 只选玩家六槽肖像；`0/0` 得到空指针会崩。默认恢复
`0/1` 保进战。怪物真立绘仍只在 **subtype-5** 场景节点路径；挑战现为 `4/10`，
目录 `actor_resource` 暂不能驱动立绘（`unresolved`）。

进战投递（2026-07-30 阶段 9）：确认同拍 HAS_FOLLOWUP 保留；临安若忽略，
下一拍 poll 可再挂一次 `4/10` followup。立绘仍须真机 subtype-10 包或挑战
绑到 subtype-5 live combat 节点后，才能用目录 Actor。

## 修改

- MySQL / 后台：`actor_resource` 可配置（目录元数据）
- subtype-10 wire：display name + visual `0/1`（防崩）；禁止 name=`.actor`；禁止 field-16 / `0/0`
- 目录 `actor_resource` **不**写回开战 name/visual（本轮取证边界）

## 验证

1. 挑战小猴子 → `visual=0/1`，进战不闪退（立绘可能仍是女天机）
2. 真怪物立绘：需挑战改走 subtype-5 + 带 `.actor` 的场景战斗节点，或对照真机包
3. 不以「看起来像怪」为本轮完成标准

## 取证边界（2026-07-30 阶段 9）

- 本轮只固化：subtype-10 不能靠 `actor_resource` / field-16 / name=`.actor` 出怪立绘。
- 临安进战改为「同拍 + 一次 poll 二次 HAS_FOLLOWUP」，与立绘解耦。
- 下一步取证：真机 subtype-10 包对照，或挑战能绑到 subtype-5 live combat 节点后
  再用目录 Actor；在此之前目录字段只作配置/日志，不写回开战 name/visual。
