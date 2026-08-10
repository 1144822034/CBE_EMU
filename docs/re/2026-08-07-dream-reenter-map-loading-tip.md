# 2026-08-07：梦境进一次后再进提示「地图加载中」

phase: dream-exit-reenter-gate-unstick
status: fixed

## 症状

使者进梦境一次并出口后，再次点「进入副本」一直提示「地图加载中，请稍候再进入。」（`instance-enter-gate-wait-clear`）。进商店再出有时又能进。

## 契约

出 `29*`→户外：

1. `30/1` 户外 + `dreamExitReenterSettleRequired`
2. 武装户外 `map_stone_loading_clear`（`remaining=3`）
3. poll 投递 `30/2` 耗尽 remaining
4. 再进 `29*` 才放行

`docs/re/2026-08-03-dream-exit-reenter-loading-gate.md` 已描述 unstick，但 **C 代码从未落地**（仅文档）。

## 第一次偏离

1. `remember_scene_change_target` 置 `sceneVisiblePending` + `direct_enter` / `target_valid`
2. 进梦有 `mark_ready(..., npc-instance-dream-enter)`；**出梦没有对称 mark_ready**
3. Pixi `ENTER_INSTANCE` 只 `handleBodies(30/1)`，不发 WT `2/3` → pending 不消
4. 户外 clear 在 `pending` / `direct` / `target` 下 `return 0`，**不消耗** remaining
5. 再进门闩见 `remaining>0` 且 clear≠29* → 永久 tip

首个错误状态：出本 settle clear 无法投递 `30/2`。

## 修改

| 位置 | 改动 |
|------|------|
| `mock_server_scene_sync.c` 出本 settle | `mark_ready(..., npc-instance-dream-exit)` |
| `build_pending_map_stone_loading_clear_response` | 户外 settle 且 shell ready 时 `dream_exit_settle_clear_unstick` 清残留传送标志，再投 `30/2` |
| clear remaining→0 / cancel | 清 `dreamExitReenterSettleRequired`（`dream_exit_settle_complete`） |

## 验证

1. 重启 `jh-online-server`
2. 进梦 → 使者出口 → 等数秒（poll clear）→ 再进应成功
3. 日志期望：`dream_exit_settle_arm ... mark_ready=1` → `dream_exit_settle_clear_unstick` → `mock_map_stone_loading_clear remaining↓` → `dream_exit_settle_complete`
4. 短暂 tip 可接受；不应永久卡住
5. 回归：户外地图石、梦境内商城进出

## Negative

- 不要在门闩处硬放行未耗尽的 outdoor clear（会回 518c ScreenInit）
- 不要用客户端假 `showPhase` 绕过 tip

## 续：出本后 NPC 不显示 / 多打一场（2026-08-07）

### 症状

使者出 `29*` 到户外：地图上无目录 NPC；落点附近立刻遭遇一场战斗。

### 根因

1. **NPC**：Pixi `ENTER_INSTANCE` 只吃 `30/1`，跨图会清空 `session.npcs`，从不发 portal 同款 post-enter `25/5+2/3+27/11+7/42` → 无 `27/11`。
2. **战斗**：出本仍 `bind_instance_hangup_enemy`（副本 boss）；户外 SCE 怪与落点重叠时 Pixi `onMonsterCollide` 立刻开战。

### 修改

| 层 | 改动 |
|----|------|
| `GameSession.pickNpcOption` | EB 进/出本后 `awaitEnterScene` + post-enter 拉 `27/11` + `pollOnce` |
| `main.ts` onEnterScene | `armCollideCooldown(3000)` |
| `build_instance_enter_response` | 仅进 `29*` 绑定 hangup enemy；出本清 bind + `hangup_loop_clear` |

### 验证

出本后应见户外 NPC；数秒内不应自动开战（走近怪仍可遇怪）。
