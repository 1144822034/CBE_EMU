# 挂机奖励窗口与自动战斗终局

Date: 2026-07-28

Status: implemented — pending manual verification

## 1. 当前卡点

- 可见现象：场景挂机连续数场后，右侧角色持续播放攻击左侧怪物的动画，自动战斗状态被退出。
- 固定触发：桃花岛场景挂机；前四场正常续接，第五场在首次奖励后的八秒窗口内结束。
- 本轮最小目标：不让处于自动挂机状态的客户端收到无法收束的无奖励终局。

## 2. 运行时证据

`bin/server_out.txt` 的同一次复现中，battle `1` 至 `4` 都是：

```text
mock_battle_settle ... reward_claimed=1 ...
mock_battle_operate ... resp=289
scene_hangup_round_complete ...
scene_hangup_start ... source=scene-poll
```

battle `3` 的首次偏离（边界修复后的复现）：

```text
mock_hangup_battle_start ... battle=3 ...
scene_hangup_reward_window_wait ... remaining_ms=3292 next_tick=841 action=hold-auto-final-hit
mock_battle_reward_cooldown action=blocked ... remaining_ms=770 session=3
mock_battle_operate ... enemyhp=0 ... resp=72
```

其中没有对应的 `mock_battle_auto_action`。后者只能由 scene poll 内部自动构造器打印，
因此这份 `4/6` 是客户端在“末击被静默扣住”后送来的真实 `WT 4/2`，直接绕过了末击前的
服务端预检。它随后触发无奖励终局，自动状态在动画仍未收束时被关闭。

因此最早错误状态不是动画本身，也不是跨场景续接；而是把奖励窗口放在已进入自动战斗的
末击边界，造成客户端自动状态与服务端的静默等待发生竞态。

## 3. IDA 目标

| binary | function/address | findings |
| --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `HandleServerBattleCmd(0x7BD0)` | `4/6` 解析 actioninfo 后设 battle phase 5；`4/11(type=0)` 清掉 `battle+1140` 自动标志并设 phase 0；`4/9(result=1)` 只在 `battle+1140==1` 或 `battle+1136==1` 时设 phase 8。 |
| `mmBattleMstarWqvga.cbm` | `SetBattlePhase(0x259A)` | phase 0 与 phase 8 是不同的状态写入；同一包先 type 0 再 `4/9` 时，后者的自动条件已经不成立。 |
| `mmBattleMstarWqvga.cbm` | `BattleScene_DrawMain(0x5444)` | `4/6` 的动作播放由逐帧状态机消费；战斗退出及空 `25/5` 只能经过其原生收束路径。 |
| `mmBattleMstarWqvga.cbm` | `HandleBattleSettleMsg(0x743C)` | 成功 `4/7` 以总 EXP/金钱差生成结果面板；两项均为零会命中已记录的 `DrawBattleHpBar(0x7228)` 崩溃路径，故不能用零奖励 `4/7` 替代。 |

## 4. 调用链 / 业务流程

1. 场景挂机开战下发 `4/11(type=1)`，客户端进入自动阶段。
2. scene poll 的 `vm_net_mock_build_pending_auto_battle_action_response()` 到期后下发 `4/6`；客户端逐帧播放动作。
3. 正常获奖的最后一击携带正增量 `4/7`，之后原生收束回到场景并发空 `25/5`。
4. `25/5` 是客户端已离开战斗渲染的可靠边界；服务端可在后续 scene poll 决定是否开始下一场。
5. 若在第 4 步检查到 MySQL 奖励窗口仍未到期，保持场景挂机续接状态但不开始 `4/5`/`4/11`；到期后才进入下一场。

## 5. 结构体 / 状态字段笔记

- client Battle object `+1140`：服务端 `4/11.type` 写入的自动状态；`0x7BD0` case 9 读取它决定是否进入 phase 8。置信度：高。
- client Battle object `+1138`：`BattleScene_DrawMain(0x5444)` 的最终动画/离场标志；不能由服务端直接写。置信度：高。
- server `sceneHangupRestartNotBeforeTick`：场景挂机的跨战续接调度点；在场景状态而非 Battle 自动状态中等待。置信度：高。
- MySQL `account_role_monster_reward_cooldowns.last_reward_ms`：奖励窗口的权威时间源；现有领取路径已使用数据库时间和事务。置信度：高。

## 6. Negative Evidence

- 不能发送 EXP、金钱均不变的成功 `4/7`：已由 `0x743C -> 0x7228` 和历史崩溃证实会进入无效渲染路径。
- 不能把 `4/11`/`4/9` 改成相反顺序试验：`0x7BD0` 显示 type 0 仍会覆盖 phase 8，且没有客户端证据证明该顺序是合法自动终局。
- 不能在自动战斗内静默扣住末击：运行日志证明客户端会随后发出真实 `WT 4/2`，使其绕过该预检；这不是客户端可见的“等待下一场”边界。
- 不能通过重放 `4/6` 或强制重置客户端状态结束动画：这会掩盖最早的协议违约，并违反服务端驱动边界。

## 7. 实现

`mock_server_role.c` 新增只读的
`vm_net_mock_role_get_monster_reward_cooldown_remaining()`：它用与奖励领取相同的
`(account_id, role_id)` 及 MySQL `CURRENT_TIMESTAMP(3)` 读取剩余窗口，绝不使用客户端或
进程本地时间。

`mock_server_battle.c` 在 `4/9 → 25/5` 后的
`vm_net_mock_build_pending_scene_hangup_battle_response()` 中读取该窗口。窗口未到期只延后
同一场景的下一次续战检查；再次到点时仍重读 MySQL，scheduler tick 不决定资格。数据库
读失败也只停留场景并按一秒重试，不向客户端伪造成功或关闭自动。窗口到期后才下发下一场
既有的 `2/2 + 4/5 + 4/11`，随后战内所有 `4/6` 仍立即按正常节奏投递。

该改变位于场景挂机跨战续接的拥有层，不改变客户端、`4/6` 编码、`4/7` 字段或场景节点；手动无奖励战斗的既有非自动终局不在本轮扩展范围。

## 8. 构建结果

2026-07-28：边界修正后 `make -j2` 已通过，生成 `bin/jh-online-server.exe`；未由本任务
启动、停止或重启服务端。

## 9. 验证清单

- [ ] 首次击杀仍正常出现 `4/6 + 4/7` 并续接下一场。
- [ ] 八秒窗口内，上一场正常结束后挂机停留在场景；不开始下一场 `4/5`，不下发无奖励终局。
- [ ] 窗口到期后才开始下一场，随后该场自动完成并出现正常结算。
- [ ] 期间点击取消自动，服务端停止当前和跨场景挂机续接。
- [ ] 无重复攻击动画、无自动状态意外退出、无零增量 `4/7`。
- [ ] 修改后只执行 `make -j2`，由用户人工复测。
