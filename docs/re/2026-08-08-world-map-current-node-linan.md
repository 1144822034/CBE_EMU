# 临安-南宣门世界地图当前位置修复

## 触发与预期

角色实际位于 `c04临安府_01.sce`（临安-南宣门），但打开世界地图仍高亮蓬莱。
预期世界地图使用该场景在 `sMap.dsh` 中的精确行，显示临安当前位置。

## 链路与首次偏离

1. 真实服务日志确认角色已完成场景进入，位置为 `(201,196)`；但数据库审计发现历史服务
   会把部分 `account_roles.scene` 保存为无后缀键，例如 `c04临安府_01`。
2. 登录角色选择返回 `1/1/6.actorinfo`；
   `parse_actorinfo_response()` 将其末尾 sceneKey 写入 `R9+0x5E46`。
3. `LoadSceneRes(0x0103130A)` 将这个值交给
   `LoadMapDataSheet(0x0103581E, mode=4, currentScene)`。mode 4 对
   `sMap.dsh` 的 map-name 执行**精确**查找，只有命中才更新世界/子地图/选中世界索引。
4. 客户端和服务端曾把 `c04临安府_01` 与 `c04临安府_01.sce` 视作同一场景；前者没有
   `sMap.dsh` 的精确行。

这一步是首次偏离：`sMap.dsh` 的精确行需要 `.sce`，无后缀键查找失败后客户端保留之前的
蓬莱控制器索引。

## 修复

场景身份改为严格字节相等：持久化、会话、NPC、传送和世界地图仅接受完整 `*.sce` 资源键，
所有场景比较使用 `strcmp()`。`c00蓬莱仙岛_02.sce`、`00蓬莱仙岛_02.sce`、
`00_蓬莱仙岛02.sce` 和任意无后缀值均不是可互换的别名。

已持久化的历史无后缀角色键只在角色加载时执行一次数据迁移：从权威 `sMap.dsh` 的 map-name
列找到唯一的 `legacy + ".sce"` 精确行后，原子地写回完整键；没有唯一行则保留原值并记录
`role_scene_key_migration_unresolved`，不会回退到蓬莱或任意默认场景。该迁移不是请求期兼容
匹配，迁移完成后的所有运行时路径均拒绝无后缀键。

同一约束也适用于新的场景切换请求：`mapID` 不是完整 `.sce` 键时，
`vm_net_mock_get_scene_change_target()` 记录 `mock_scene_target_rejected` 并返回空目标；后续
builder 因而拒绝该请求，不会构造一个看似成功、实际把角色送到默认场景的响应。

## 自动化回归

`scripts/run-world-map-current-node-automation.ps1` 使用独立
`jh_online_autotest_*` 数据库、独立端口和测试账号，发送真实服务协议
`1/1/12` 登录与 `1/1/6` 角色选择。它严格断言返回的
`1/1/6.actorinfo` 包含 GBK `c04临安府_01.sce\0`（而不是无扩展名形式），并断言数据库中的
历史裸键已经被一次性改写为该精确键。
该字段正是客户端随后用于 mode-4 `sMap.dsh` 查找的字符串；自动化不写客户端内存、
寄存器、PC/LR 或响应字节。

相邻的三种场景键区分继续由
`scripts/forge-valley-npc-lifecycle-regression.php` 覆盖。

2026-08-08 的隔离执行结果：

- `make -j2` 通过；
- `world-map-current-node-v1` 通过，证据目录为
  `artifacts/automation/world-map-current-node-v1-20260808T132526264Z-16256/`；日志同时记录
  `role_scene_key_migration ... action=exact-rewrite` 和 `actorinfo=1/1/6` 精确键断言；
- 相邻的 `npc-stock-bulk-v1` 通过，证据目录为
  `artifacts/automation/npc-stock-bulk-v1-20260808T132526286Z-43256/`，确认动态 NPC 库存也在
  全程使用同一完整场景键。
