# 世界BOSS：队长遇怪队友不进战

日期：2026-10-09

## 现象

组队（≥2）同图准备好时，队长手动踩/点世界 Boss：队长进入战斗，队友仍停在地图，无 `1/4/5` 拉入。

## 证据 / 链路

1. 队长 `4/1` → `vm_mock_world_boss_try_claim_encounter` → `requestedEnemyId=boss`。
2. `mock_challenge_world_boss_form` 曾无条件 `useSceneMonsterStart=false` / `subtype=10`（为修户外垃圾怪外观，见 `2026-09-12-world-boss-disabled-hangup-form.md`）。
3. `team_begin_battle` 与队长组队 `battleinfo` 仅在 `useSceneMonsterStart` 分支执行；subtype-10 走单人 `build_battle_start_info_blob`，不设 `pendingTeamBattleSerial`。
4. 队友 poll 无 pending → 不进战。日志无 `mock_team_battle_start … source=leader-4/1`。

## 根因

世界 Boss 形态修复把**组队开战契约**一并关掉：组队进战依赖 subtype-5 场景开战 + `team_begin_battle`；强制 subtype-10 只服务单人 catalog 外观。

## 修改

`src/server/mock_server_battle.c` 挑战开战世界 Boss 分支：

| 条件 | 行为 |
|------|------|
| 队长同图可参战席位 ≥2 | 保持 subtype-5；`seedSceneMonsterMoveinfo` + `moveinfoEnemyId=boss`；日志 `keep-scene-start-for-team` |
| 单人 / 仅队长可战 | 仍 subtype-10 + catalog prefill（原外观契约） |

另：不要用通用 left-side env 赋值覆盖已武装的 world-boss solo `prefillEnemyTemplate`。

队友仍走既有 pending `1/4/5`；Pixi `resolvePveEnemyActor` 在 wire≠boss 时用 catalog 形态。

## 验证

- [ ] `make -j2`
- [ ] 两人同图组队、均准备好：队长点世界 Boss → 双方进战；日志 `keep-scene-start-for-team` + `mock_team_battle_start … source=leader-4/1`
- [ ] 单人点世界 Boss → 仍 subtype-10 / Boss catalog 形态（非户外胶质）
- [ ] 队友不同图或未准备好：行为与普通组队遇怪门闩一致
