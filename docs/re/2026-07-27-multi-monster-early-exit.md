# 三怪打死一只就跳出战斗

## 现象

场景多怪开战（`enemies=3`）后，打死其中一只即进入结算并离开战斗，客户端仍能看到其余怪物模型。

## 触发条件

1. 进入 subtype-5 / 场景怪开战，`battleinfo` 广告 `enemies=2|3`。
2. 普攻或单体技能击杀其中一只。
3. 服务端立刻 `note_victory` / `4/7` / 延后 `4/8`，战斗结束。

## 预期 vs 实际

| | |
|---|---|
| 预期 | 仅对应 slot HP 归零；`enemyhp` 仍为剩余槽位之和；战斗继续直至全部 slot 归零 |
| 实际 | 首杀后 `EnemyHpCurrent==0` 被当成全灭，走胜利退出 |

## 根因

胜利契约原先绑在聚合血量 `g_mockBattleEnemyHpCurrent == 0` 上。

当 `EnemyCountCurrent>1` 但只有 slot0 被 seed（开战 count 曾短暂为 1、账户恢复丢失 `SceneMonsterStartActive`、或 wire 表退化为单怪导致伤害全砸 slot0）时：

1. 首杀把唯一 seeded slot 打到 0 → 聚合为 0。
2. `battleEndsThisRound` / settlement / auto 门闩全部认为胜利。
3. 客户端仍按 battleinfo 显示 2–3 只怪 → 表现为「打死一只就跳出」。

次要契约破坏：场景 wire 表只靠 `SceneMonsterStartActive`；flag 丢失后 `count>1` 仍走单怪映射，伤害塌缩到 slot0。

## 修改点

- `vm_net_mock_battle_all_enemies_defeated()`：按 slot 判定；未 seed（max==0）不算全灭。
- `vm_net_mock_battle_ensure_multi_enemy_slots_seeded()`：operate 入口补齐从未 seed 的 slot，不复活已死（max>0,hp=0）怪。
- operate / fallback：胜利与反击门闩改用 `all_enemies_defeated()`；禁止在 armed 战斗中因聚合 0 盲目 `reset_enemy_hp`。
- `use_scene_monster_wire_maps()`：`count>1` 时即使 scene flag 丢失仍用多怪 wire 表；队伍路径要求 `member_count>=2`。
- 开战：`EnemyCountCurrent>1` 时强制 `SceneMonsterStartActive=1`。

## 验证

1. 三怪开战日志：`enemies=3`，`slots=a/a/a`，`enemyhp=3a`。
2. 打死一只：`slots=0/a/a`，`enemyhp=2a`，无 `note_victory` / 无立即 `4/8`。
3. 打完剩余：才出现结算与退出。
4. `make -j2 server` 通过。

## 残留风险 / 2026-08-01 组队路径补洞

- 若客户端 battleinfo `enemies` 与服务端 `EnemyCountCurrent` 长期不一致，仍可能错判；需对开战包字段继续取证。
- **已补**：组队曾用 `team->battleEnemyHpCurrent == 0` 置 `battleFinished` /
  `terminalVictory`。仅 slot0 seeded 时首杀聚合为 0 → 过早 `battleFinished`，
  merge 又因 `all_enemies_defeated()==false` **不发 4/7**，下一 operate 走
  finished 分支（`4/11` auto-off）→ 体感「怪没死完就退出、结算不显示」。
  现改为 `team_battle_all_enemies_defeated`（按 slot），并在
  `team_battle_prepare_operation` 调用 `ensure_multi_enemy_slots_seeded` 写回队伍快照。
