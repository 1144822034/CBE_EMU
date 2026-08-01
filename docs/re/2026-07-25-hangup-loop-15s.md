# 挂机循环开战（场间间隔）

Date: 2026-07-25（2026-07-27 曾改为 10s；2026-07-28 恢复 15s 并改为结算离场后再计时；同日默认下调为 **8000**；2026-07-30 默认再调为 **2000**）

Status: implemented（节奏契约以 pacing-refactor 为准）

## 目标

点一次挂机后，只要战斗中不显式关闭自动（`4/11 type=0`），胜利离场后约 2 秒由服务端经 scene poll 再投下一场挂机开战包，无需再点挂机。

## 契约

1. 挂机开战且 `auto!=0` / `prefer=1` → `hangupLoopActive=1`，清 pending / ScheduleAfterExit。
2. 胜利（operate / item-use / fallback）且 `prefer` 仍在 →
   `hangupLoopScheduleAfterExit=1`（**不**启 timer）。
3. 延迟 `4/8` 离场投递成功 → `hangupLoopPendingArmed=1`，
   `not_before_ms = now + CBE_HANGUP_LOOP_INTERVAL_MS`（默认 **2000**，纯地图侧等待）。
4. scene-sync poll 在 `sceneVisibleReady` 之后投递：
   - `mock_hangup_loop_poll_deliver` → 合成 `build_hangup_battle_start_response(NULL)`
   - 与按钮挂机同契约（live-node subtype-5 或 subtype-10 回退 + 可选 `4/11`）。
5. 停止循环：
   - 显式 `4/11 type=0`
   - 逃跑成功
   - 死亡 / 挂机拒绝死亡 / 无挂机怪
   - 换场景

## 为何离场后再计时

历史上短间隔与结算离场叠在一起时，容易体感成「出手后怪没动就下场」。
将 timer 挪到结算离场之后后，结算 UI 不再吞掉场间间隔。
奖励闸门 `CBE_BATTLE_REWARD_MIN_INTERVAL_MS` 内建默认仍为 **8000**（只限发奖，与挂机场间不再强制同值）。

## 不做什么

- 不在战斗未结束时强塞开战包。
- 不改奖励闸门语义（`CBE_BATTLE_REWARD_MIN_INTERVAL_MS` 仍只限发奖）。
- 不写客户端全局/补丁；只走 event-7 正常投递。

## 日志

- `mock_hangup_loop_note_victory`（`after_exit=1`）
- `mock_hangup_loop_schedule`（`evidence=hangup-loop-after-exit`）
- `mock_hangup_loop_poll_deliver`
- `mock_hangup_loop_clear reason=...`

## 验证

1. 点挂机 → 自动打完一场 → 回地图。
2. 约 8s 后无需再点 → 日志 `mock_hangup_loop_poll_deliver`，再进战。
3. 战斗中点关自动（`4/11 type=0`）→ 本场可打完，但不再自动下场；
   见 `2026-07-28-auto-button-cancel-hangup.md`。
4. 逃跑 / 换图 → 循环停止。
