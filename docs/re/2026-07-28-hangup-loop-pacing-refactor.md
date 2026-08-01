# 挂机循环节奏与自动时钟收敛

Date: 2026-07-28

Status: implemented

## 问题

自动挂机三层状态（开战 prefer / 战中 synth / 场间再开战）在迭代中出现：

1. 文档与代码默认不一致：`CBE_HANGUP_LOOP_INTERVAL_MS` 文档 15000、代码曾为 9000；奖励闸门文档 15000、代码曾为 6000。
2. 场间计时从**胜利瞬间**起算，与结算播放 + `4/8` 离场重叠，体感间隔不是「回地图后再等 N 秒」。
3. 战中同时用 wall-clock ms 与 scheduler tick 两套闸门；`ClientDriven` 已无语义但仍参与分支。

## 根因陈述

- 触发：连续挂机；或对照文档调参。
- 被违反的契约：场间间隔应是**结算离场后的地图侧等待**；战中 hold 应以 **ms 为权威**。
- 首个错误状态：胜利时 `hangup_loop_schedule` 把 `not_before` 设在结算窗内，短默认下回地图几乎立刻再开战。
- 证据：代码默认 9s/6s 与 `docs/re/2026-07-25-hangup-loop-15s.md` / `2026-07-28-hangup-interval-15s-restore.md` 冲突；日志 tag 仍写 `hangup-loop-15s`。

## 修改

### 1. 默认对齐

| 环境变量 | 默认 |
|----------|------|
| `CBE_HANGUP_LOOP_INTERVAL_MS` | **2000**（2026-07-30；此前 8000 / 15000） |
| `CBE_BATTLE_REWARD_MIN_INTERVAL_MS`（内建默认） | **8000**（只限发奖；与挂机场间可不同） |

日志 evidence 改为 `hangup-loop-after-exit`。

### 2. 场间计时：结算离场后再武装

```
victory → note_victory_reentry (ScheduleAfterExit=1, 不启 timer)
       → delayed 4/8+4/11type0+4/9
       → schedule_next (now + INTERVAL)
       → sceneVisibleReady poll → hangup start
```

停止条件不变：`4/11 type=0`、逃跑、死亡、无怪、换图、组队 suspend。

### 3. 时钟与旗标

- `vm_net_mock_battle_auto_set_hold_until_ms`：ms 权威，tick 由剩余 ms 推导。
- Flag pending 改为 `FlagPendingNotBeforeMs`（不再用独立 tick 闸门）。
- `ClientDriven` 退出逻辑分支；session 仍保留字段但恒为 0。
- 新增 `HangupLoopScheduleAfterExit`（胜利意图 → 离场后才 pending）。

## 验证

1. 点挂机打完一场：日志先有 `mock_hangup_loop_note_victory ... after_exit=1`，再有 `mock_battle_settlement_exit`，然后 `mock_hangup_loop_schedule ... interval_ms=8000 evidence=hangup-loop-after-exit`。
2. 回地图后约 8s：`mock_hangup_loop_poll_deliver`，再进战。
3. 战中关自动：本场可打完，不再 schedule。
4. 多怪自动：`playback_hold` 后同账号不应在 hold 内再 synth；无施法动画重播。
5. `make -j2` 通过。

## 不做

- 不改奖励闸门语义（仍只限发奖）。
- 不写客户端全局 / 补丁。
- 不在本轮做 hangup 开战失败指数退避（仍 1s 重试）。

## 2026-07-28 Follow-up: default 8000

离场后再计时落地后，默认由 15000 下调为 **8000**（挂机再入 + 奖励闸门对齐）。
可用环境变量覆盖。
