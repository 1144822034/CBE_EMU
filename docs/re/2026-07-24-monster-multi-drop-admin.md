# 2026-07-24 怪物多掉落与后台物品选择器

## 触发条件

后台原“怪物管理”只能填写一组 `drop_item_id/drop_rate_percent`。需求是让一
个怪物可以配置多个物品掉落，并复用账号管理“给予物品”的分类、搜索选择器到
所有后台物品 ID 编辑点。

## 首次偏离与证据

单值限制贯穿完整链路，而非单独的 HTML 问题：

1. `server_monsters` 只有一组旧 `drop_item_id/drop_rate_percent` 列；
2. `vm_net_mock_monster_override`、`vm_net_mock_monster_admin_row` 和战斗奖励
   缓存只保存一个物品、背包序号和数量；
3. `vm_net_mock_battle_grant_reward_once` 只为一个物品投掷并写入背包；
4. 战后刷新 `1/7/7 { type=1 }` 只序列化一个 `iteminfo` 行。

因此仅把表单改为多选会让后续字段覆盖或只掉落第一项，违反了后台配置与结算
之间的一致性契约。

已验证的客户端刷新契约来自
`docs/re/2026-06-28-battle-reward-persistence.md`：

```text
1/7/7, type=1
iteminfo:
  u8 row_count
  repeat row_count:
    i16 seq
    u32 item_id
    u32 count_delta
    common item-extra
```

`mmGameMstarWqvga.cbm:sub_D04(0x0D04)` 消费该流；它是安全的无弹窗背包增量
路径。战斗结算 `4/7.iteminfo` 对普通物品仍然不安全，故继续用已有的 `fdata`
只展示首个掉落，实际背包更新由上述多行 `7/7` 完成。

## 修正设计

- 新增 `server_monster_drops(monster_id, drop_slot, item_id,
  drop_rate_percent)`，每行是一个独立投掷概率；当前服务端上限为 64 行。
- 老 `server_monsters.drop_*` 是一次性兼容导入来源。运行时以
  `INSERT IGNORE` 导入旧第 1 条；新的后台保存会在同一 MySQL 事务中写父行、
  删除旧子行、插入完整的新列表，并把父行旧列清零。这样管理员删除掉落后不会
  在服务重启时被旧列恢复。
- 默认资源目录仍可提供一个旧式任务材料掉落。只要某怪物存在 MySQL 覆盖，覆盖
  的掉落列表（包括空列表）就是权威值。
- 每个掉落行每场战斗只随机一次；命中后按本场被击败敌人数量发放对应数量。
  任务材料仍调用既有 `vm_net_mock_task_material_drop_policy`，只在任务需要时
  掉落并按剩余需求封顶。
- 实际成功写入背包的所有行作为按客户端隔离的战斗结果缓存保存；结算后一次性
  下发多行 `1/7/7 type=1`。空背包、无任务材料资格或某一物品写入失败只影响该
  行，不会阻断其他已配置掉落。
- 旧 `CBE_BATTLE_DROP_*` 与毒泥怪环境变量只保留为明确设置时的单行测试覆盖，
  不会部分修改未知的多掉落表。

## 后台物品选择器

`vm_mock_admin_render_item_picker_field` 和
`vm_mock_admin_render_item_picker_modal` 抽取原账号“给予物品”的物品目录、分类
和搜索逻辑。它们用于：

- 账号管理的给予物品；
- 怪物管理的每一个掉落槽；
- 任务的接取给予物品和奖励物品。

任务条件 ID 同时表示“收集物品”或“击败怪物”，不能强行改成纯物品选择器：保留
数值输入以支持怪物 ID，并在收集物品时提供同一个选择窗口。保存时对所有具备
物品语义的任务 ID 校验 `item.dsh` 目录存在性。

## 验证

- `make -j2`：通过（服务端重新链接成功）。
- 构建产物启动后监听 `19090`（游戏）与 `19091`（后台）。
- 尚未使用管理员密码进行自动登录或写入测试数据，避免因本机后台密码已被用户
  修改而造成错误计数和锁定状态变化。应手工验证：为同一怪物保存至少两项
  100%% 掉落，击杀一次后确认两项都写入背包，并在服务日志看到
  `mock_battle_drop_gate slot=1/2` 及单个 `mock_battle_drop_refresh rows=2`。

## 风险与已排除项

- 未把普通物品塞回 `4/7.iteminfo`；已有运行时证据表明该路径会进入不完整的
  装备/详情解析并可能闪退。
- 多掉落条数现在使用统一的 255 条服务端边界（本文原为 64，后由
  2026-08-21 智能装备分配扩展），并在 SQL 读取、后台表单、内存缓存和客户端
  `iteminfo` 构建处重复边界检查；这高于原先人为的 8 条编辑器限制，同时仍受客户端
  `u8` 行计数与单个 WT 对象长度约束。
- `server_monster_drops` 不引用 `item.dsh` 的 SQL 外键，因为物品目录是客户端
  DSH 资源而非 MySQL 表；服务端保存和加载时使用目录校验。
