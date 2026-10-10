# 名字含 Boss 遇怪最多刷新数

日期：2026-10-10

## 需求

管理后台可配置：显示名或资源名含 ASCII「Boss」的怪物，玩家遇怪时战场克隆只数上限。
历史默认随机 1–3；现支持上限 1 / 2 / 3（1=固定 1，2=随机 1–2，3=随机 1–3）。

## 存储

`server_monster_global_config.boss_encounter_enemy_max`（`TINYINT`，默认 3，sanitize 到 1–3）。

迁移：`server/mysql/migrate_add_boss_encounter_enemy_max.sql`；服务首次读取也会 `ALTER` 加列。

## 判定

`vm_net_mock_monster_name_contains_boss`：`vm_net_mock_monster_battle_left_appearance` 解析出的显示名或 actor 资源串中，大小写不敏感包含 `Boss`。
不按 `family=首领` 判定（仅名字/资源）。

## 运行时

`vm_net_mock_battle_roll_enemy_count(useScene, enemyId)`：

1. `CBE_BATTLE_ENEMY_COUNT` 强制仍优先（自动化）。
2. 战斗心得（828）：非 Boss 名仍固定 3；Boss 名固定为配置上限。
3. 非场景开战：仍 1。
4. 场景开战：在原 `MIN/MAX`（默认 1–3）上，Boss 名再把 `max` 截到配置值。

组队路径 `vm_net_mock_battle_apply_leader_insight_enemy_count` 同样按队长心得 + Boss 名上限复核。

## 后台

`?tab=monsters` →「全服怪物百分比」→「名字含 Boss 遇怪最多刷新数」→ `save-monster-global-config` 热加载。

## 验证建议

- [ ] 默认 3：名字含 Boss 的怪场景开战 `enemies` 仍在 1–3。
- [ ] 设为 1：同怪开战始终 `enemies=1`；心得开启也不升到 3。
- [ ] 设为 2：同怪开战 `enemies`∈{1,2}。
- [ ] 名字不含 Boss：行为与改前一致（仍 1–3 / 心得 3）。
- [ ] 保存后无需重启服务即可生效。
