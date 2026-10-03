# 普通复活第一次无反应、第二次出结算、不到传送石

日期: 2026-10-03

## 触发

1. 野外战死，死亡提示点「普通复活」。
2. 第一次点击无离场；再点一次出现结算面板。
3. 出战后不在当前区域 `n_telestone` 旁。

## 预期 / 实际

- 预期：一次 `1/7/14 result=2` 即离开战斗，落到最近（含本图）传送石坐标，约 30% 血蓝。
- 实际：同图 `20/1+30/1` 被当成传送后迟到进图忽略；服务端已复活。第二次 `result=2` 因 HP≠0 走 `already-alive` 的 `4/7+4/8` 结算。落点用了地图中心最近门口，不是 `n_telestone`。

## 契约 / 首次偏离

| 预期 | 实际（修前） |
|------|----------------|
| 死亡座位的 `20/1+30/1` 结束战斗并写传送石坐标 | `keepBattleThroughScenePacket` 在同图且 `inBattle` 时丢弃进图 |
| 同壳 `alreadyOnScene` 仍要拆 mmBattle、改脚位 | `trackEnterScene` 早退，脚位因 `hasServerPos` 不更新 |
| 传送石场景落地 = SCE `n_telestone` | `resolve_nearest_teleport_stone_respawn` 用 `reasonable_spawn`（门口/中心） |

与「传送后立刻遇怪闪出战斗」的守卫冲突：活着的同图 poll 仍应保住战斗；**hp=0 / 带 20/1** 的进图是复活离场。

第二次结算：服务端 `apply_death_penalty` 在第一次已把 HP 写成非 0，再点普通复活走 `already-alive-battle-exit` → `4/7+4/8`。

## 修改

| 位置 | 改动 |
|------|------|
| `GameSession.applyBattleLeaveSceneEnter` | 死亡/`20/1` 同图也 `onEnterScene` 拆战斗并写 `30/1` 坐标 |
| `keepBattleThroughScenePacket` | `hp<=0` 不守卫 |
| `main.onEnterScene` | 同壳保战斗仅当 `hp>0` |
| `vm_net_mock_resolve_nearest_teleport_stone_respawn` | 优先 `n_telestone` 坐标 |
| 普通复活响应 | `arm_map_actor_vitals_sync`（`1/1/14`） |

## 验证

1. 野外死亡 → 一次普通复活 → 立刻回地图，人在本区域传送石旁，可走动。
2. 不应出现第二次才弹出的结算。
3. 传送后立刻遇怪仍应留在战斗（前一条修复）。
4. 复活石原地满血路径不变（`4/7+4/8`）。

## 风险

- 若某条活着的 `20/1+30/1` 与战斗同包，会被当成离场。当前普通复活才这样组包。
