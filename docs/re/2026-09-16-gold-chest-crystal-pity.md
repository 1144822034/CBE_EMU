# 黄金宝箱玄晶保底

Date: 2026-09-16

Status: implemented (server + admin)

```text
phase: 1/7/15 or 1/7/24 open
       -> weighted roll from server_chest_rewards
       -> if pity enabled and miss+1 >= threshold: force pity_item_id
       -> on grant success: crystal(901..916) resets miss; else miss++
```

## 契约

| 项 | 默认（黄金 524） |
|---|---|
| 保底次数 | 99（即连续 98 次未出玄晶，第 99 次强制） |
| 保底物品 | 912 十二级玄晶 |
| 重置条件 | 开出任意玄晶 901–916（含保底物） |
| 作用范围 | 按 `chest_item_id` 隔离；各箱可单独开关 |

未启用保底或 `pity_threshold=0` 时行为与旧版纯权重抽取相同。

## 存储

- 配置：`server_chest_pity_config(chest_item_id, enabled, pity_threshold, pity_item_id)`
- 角色计数：`account_roles.chest_crystal_miss`（`522,523,524,532,534` 五种箱的 CSV）
- 后台：`/?tab=chests` →「玄晶保底」卡片；`action=chest-pity-save`

## 修改点

- `mock_server_catalog.c`：pity 加载/保存、roll+override、开箱后 note miss
- `mock_server_core.c`：`role_state.chestCrystalMiss[5]`
- `mock_server_role.c`：列 ensure / 加载 / 写入
- `web_admin_chests.inc.c` + `web_admin_server.c`：保底表单
- `server/mysql/migrate_add_chest_pity.sql`（运行时也会 CREATE/ALTER）

## 验证

1. `make -j2`；后台黄金页确认默认 启用/99/912，可改并热加载。
2. 将某角色 `chest_crystal_miss` 中黄金位（第 3 段）设为 98，开一次黄金箱 → 必得 912，计数归 0。
3. 中途开出任意玄晶 → 计数归 0；十连路径同样按次累计。
4. 关闭保底后不再强制，仅走权重池。
