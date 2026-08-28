# PvE 多怪反击均可放技能（2026-08-28）

## 触发

天梯挑战（三人：天机/幻剑/鬼道）中，仅天机反击偶发技能特效；幻剑、鬼道始终普攻。

## 根因

1. `vm_net_mock_battle_resolve_enemy_counter_damage` 硬限制 `strikeIndex != 0` 时强制普攻，同一回合只有 lead 存活怪可放技能。
2. 服务端战斗全局只存 `g_vm_net_mock_battle_enemy_id_current`（天梯开战只写入天机 ID），多槽反击共用同一 enemyId，无法按槽位判断 `cast_skill`。

## 修改

- 移除 lead-only 技能限制：凡 `vm_net_mock_monster_casts_active_skill(enemyId)` 为真的存活怪均可独立判定。
- 新增 `g_mockBattleEnemyIdSlots[3]`；开战/HP reset/session 切换时维护；反击按槽位取 ID。
- 天梯 `arm_subtype10_multi_pve_start` 传入三职业 enemyId 数组。
- 后续补充：按 job 分池选技、每槽位回血、天梯 `cast_skill` 接入（见 `2026-08-28-ladder-job-skill-heal-fix.md`）。

## 验证

1. `make -j2` 通过。
2. 天梯三人存活时，反击日志可出现多个 `mock_battle_boss_skill enemy=92000x`（各自 ID）。
3. 挂机/场景多怪且 `cast_skill` 开启时，非 lead 槽位也可放技能。
4. 未开 `cast_skill` 的普通怪仍只普攻。
