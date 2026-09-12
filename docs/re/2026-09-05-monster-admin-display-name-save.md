# 怪物管理显示名称无法保存（2026-09-05）

## 现象

管理后台「怪物管理」编辑 `#200`（及其他内置/自定义怪）时，改显示名称并保存会提示成功，但刷新后名称仍是 SCE / 目录原名。

## 根因

1. 编辑表单与 `save-monster` handler 已解析 `display_name`（UTF-8→GBK）。
2. `vm_net_mock_monster_admin_save` 只写 `server_monsters` 的属性/Actor/掉落，**从不写入显示名称**。
3. 自定义怪路径仅 `UPDATE server_monster_catalog_extra.actor_resource`，也不更新 `display_name`。
4. 后台列表与 `/api/monster-levels` 的 `names` 只读 SCE 标签 / `catalog_extra` 创建名，因此保存后立刻回退。

第一次偏离：保存事务提交后，内存 override 与 MySQL 均无名称字段；不是 `#200` 特例，而是整条保存契约缺口。`#200` 只是用户最先改名的内置怪。

## 修改

1. `server_monsters.display_name`（`VARBINARY(31)`，空=沿用 SCE/目录名）；启动时 `ensure` 列，迁移脚本 `migrate_add_monster_display_name.sql`。
2. `monster_admin_save` / `create` 写入该列；自定义怪同时更新 `catalog_extra.display_name`。
3. override 结构增加 `displayName`；`admin_list`、开战名解析、wire→real 按名反查优先读覆盖名。
4. `save-monster` 强制要求有效显示名称（与表单 `required` 一致）。

## 验证

1. `make -j2` 通过。
2. 打开 `/?tab=monsters&monster=200`，改显示名称并保存 → 提示成功后刷新仍显示新名。
3. MySQL：`SELECT monster_id,HEX(display_name) FROM server_monsters WHERE monster_id=200` 非空。
4. 「恢复服务端默认」后名称回到 SCE 标签。
5. 自定义怪改名后目录列表与 `/api/monster-levels` `names` 同步。

## 风险

- 地图刷怪点 SCE 内嵌名牌仍需在场景设计里重存刷怪点才会改客户端包内字符串；API 名牌走 catalog 覆盖名。
- 旧库无列时依赖启动 `ALTER`；手工迁移可用上述 SQL。
