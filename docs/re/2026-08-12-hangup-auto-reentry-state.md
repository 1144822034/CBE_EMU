# 挂机结束后的自动状态与手动重入（2026-08-12）

## 现象

丹霞山中完成一场自动挂机后，若随后由场景碰撞进入普通战斗，客户端可能发出空
`WT 4/12`，而服务端将其记录为未处理。战斗画面因此已进入自动行动等待阶段，却没有
`4/6` 行动结果，表现为没有可操作选项且无法继续。

同一日志还记录了挂机内点击红叉：

```text
mock_battle_auto_cancel_request ... hangup=1
scene_hangup_stop ... reason=battle-auto-cancel
```

这不是续战失败；`4/11(type=0)` 是客户端确认的“取消自动”包，挂机会话必须在该边界
停止，之后不应再自动开启下一场。

## 客户端依据

`mmBattleMstarWqvga.cbm` 的 `BattleAutoAction_TimerTick(0x2952)` 只有在客户端自己的
自动状态仍为开启、战斗未结算且当前可行动时，才发送空 `1/4/12` 并进入 phase 6。
因此，新的存活战斗内收到严格的空 `4/12`，是客户端仍希望自动行动的直接证明；它不是
服务端可猜测或伪造的操作。

胜利奖励仍遵循既有安全边界：

```text
4/7 奖励面板 -> 客户端真实确认输入 -> 空 25/5 -> scene poll -> 下一场 4/5
```

不能在 `4/7` 面板存在时追加 `4/11(type=0)` 或新的 `4/5`，否则会改写奖励面板 phase 或
重入未销毁的 BattleScreen。

## 修正

`vm_net_mock_build_battle_auto12_replay_response()` 现在在以下全部条件成立时，接受并恢复
服务端的自动状态：

1. 请求严格为单对象、空 payload 的 `WT 4/12`；
2. 请求账号就是当前战斗角色；
3. 当前为已 armed、未结算、双方仍存活的 solo 战斗；
4. 不是队伍战斗、决斗、奖励面板、死亡态或其它账号的会话。

随后仍通过既有 `4/2 -> 4/6` 权威行动 builder 生成结果，不拼造行动包。正常已有
`4/11(type=1)` 的挂机和队伍战斗逻辑不改变。日志以
`mock_battle_auto_resume ... reason=client-4/12-live-solo-proof` 标识该恢复分支。

## 挂机续战的碰怪边界（补充）

运行日志还确认了另一条独立的停止路径：已完成的挂机场次会先记录
`scene_hangup_round_complete ... next_tick=...`，但客户端可能在该五秒间隔内先发送一个真实的
`WT 4/1` 场景碰怪请求。这个请求已经由 `BattleScene` 进入“等待战斗开始”的状态，不能返回
空确认或等待下一次轮询。

此前续战继承条件错误地要求当前 tick 已到 `next_tick`；因此提前到达的 `4/1` 被普通碰怪
builder 重建为 `auto=0` 的新会话，随后的 `25/5` 检查到 battle serial 已变，记录
`scene_hangup_stop reason=battle-session-mismatch`。

现在只有“同一在线角色、严格相同场景、已胜利已结算、等待续战、solo、双方状态有效”的
`4/1` 才能继承挂机会话，无论它在 poll deadline 前或后抵达。该路径仍发送完整的
`2/2 + 4/5 + 4/11(type=1)`，并由客户端自己的 `4/12` 驱动行动；五秒延迟保留为没有原生
碰怪请求时的 scene-poll fallback。日志字段 `hangup_continue=1` 与
`before_poll_deadline=1` 可验证这个分支。

## 验证要点

1. 在丹霞山挂机完成并由奖励确认回到场景后，等待 5 秒，应出现
   `scene_hangup_round_complete`、随后 `scene_hangup_start source=scene-poll`。
2. 战斗内点击红叉后，应出现 `scene_hangup_stop reason=battle-auto-cancel`；这是预期停止，
   不应自动续战。
3. 取消后再次触怪进入普通战斗，若客户端发送 `4/12`，应记录
   `mock_battle_auto_resume` 和后续 `mock_battle_auto_replay`，不再出现 `unhandled wt=4/12`，
   战斗可继续推进。
