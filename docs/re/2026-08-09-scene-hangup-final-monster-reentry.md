# 场景挂机：最后一只怪死亡后提前重入战斗

## 触发与预期

在场景中开启挂机自动战斗并击杀一场战斗的最后一只怪物后，客户端应先完成
本场的奖励结算和 BattleScreen 生命周期。随后，客户端原生退出战斗场景，再由
空 `25/5` 确认和场景轮询触发下一场挂机战斗。

本次实际现象是最后怪物死亡后，左侧又出现一只怪物，但客户端无法继续操作并
停在战斗画面。

## 固定运行时证据

`bin/server_out.txt` 本次复现中，同一战斗会话依次出现：

```text
mock_battle_settle enemy=105 enemies=3 victory=1 ...
mock_battle_terminal_close_deferred session=2 ... not_before_tick=474
  source=battle-operate-scene-hangup-auto-continue
scene_hangup_start ... battle=3 source=scene-poll
mock_hangup_battle_start source=scene-poll ... enemies=1 ... resp=213
mock_scene_hangup_auto_continue_deliver previous_session=2
  action_boundary=474 tick=474 response=2/2+4/5+4/11-after-4/7
```

在这段记录中，`mock_scene_hangup_auto_continue_deliver` 之前没有
`scene_hangup_round_complete` 或 `battle_reward_panel_closed`。因此新的 `4/5`
在旧会话的 `4/7` 奖励面板仍存活时到达，这是首次偏离；之后出现的“新怪物”与
卡死只是旧 BattleScreen 与新 battleinfo 重入的后果。

## 客户端契约

- `mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)` 解析 `4/7` 的奖励、HP、MP
  和掉落字段，建立奖励结算状态。
- `mmBattleMstarWqvga.cbm:BattleScene_ExitAndCleanup(0x60C8)` 先清理 BattleScreen
  和战斗状态，随后经网络接口发送空 `25/5`。该函数的 `0x6132` 路径表明 `25/5`
  属于退出后确认，不是可由服务端 `4/5` 替代的消息。
- `mmBattleMstarWqvga.cbm:HandleServerBattleCmd(0x7BD0)` 将 `4/5` 分派到新的战斗
  初始化路径。因此，在 `0x60C8` 之前发送下一份 `4/5` 会使两个战斗生命周期重叠。

先前“动作播放边界后直接下发 `2/2 + 4/5 + 4/11`”的假设被本次运行时记录否定，
不能再作为自动续战契约。

## 根因

`vm_net_mock_scene_hangup_auto_continue_after_reward()` 将“当前仍有奖励面板”当成
可以续战的条件。它令两条操作路径在 `4/7` 仍未关闭时：

1. 为最终攻击设置 `g_vm_net_mock_battle_terminal_close_not_before_tick`；
2. 在轮询到达该 tick 后由
   `vm_net_mock_build_battle_pending_settlement_response()` 直接调用
   `vm_net_mock_build_hangup_battle_start_response(NULL, 0, ...)`。

第二步越过了客户端拥有的 `BattleScene_ExitAndCleanup -> 25/5` 生命周期边界。

## 修正

- 移除奖励面板仍存活时的自动续战判定、延时边界和直接 `4/5` 注入。
- 保留并使用已有的正确链路：

```text
最终 4/6 + 4/7
  -> 客户端完成奖励面板并执行 BattleScene_ExitAndCleanup(0x60C8)
  -> 原生空 25/5
  -> scene_hangup_round_complete
  -> 后续 scene poll
  -> 2/2 + 4/5 + 4/11
```

这确保新场次只在客户端已退出旧战斗后才创建。是否能在不关闭原生奖励面板的前提下
自动续战，目前没有已验证的客户端协议；不能以提前 `4/5` 伪造这一行为。

## 验证

构建后由用户复测三怪挂机战斗：最后怪死亡后，服务端在出现原生 `25/5` 前不得出现
`mock_hangup_battle_start source=scene-poll` 或
`mock_scene_hangup_auto_continue_deliver`；确认奖励面板后才允许下一场 `4/5`。

## 2026-08-09：未确认奖励面板时的续战边界复核

### 触发与首次偏离

角色 `10036` 的最新真实运行记录中，挂机第 6 场三怪战斗已经正常走到最终结算：

```text
mock_hangup_battle_start ... battle=6 ... enemies=3 ...
mock_battle_settle ... victory=1 ... reward_claimed=1 ...
mock_battle_operate ... deaths=3 ... resp=408
```

此后没有收到客户端的空 `25/5`，也没有
`scene_hangup_round_complete`。这正是奖励面板保持显示、不会开始第 7 场的第一次偏离。
同一份日志中第 2 至第 5 场都先出现：

```text
scene_hangup_round_complete ... evidence=4/7-panel->25/5->poll
scene_hangup_start ... source=scene-poll
```

因此服务端没有遗漏“下一场启动”事件；缺失的是客户端关闭本场奖励面板后的确认请求。

### 客户端复核

`mmBattleMstarWqvga.cbm` 的 IDA 复核结论如下：

- `HandleServerBattleCmd(0x7BD0)` 的 `4/7` 分支只交给
  `HandleBattleSettleMsg(0x743C)` 解析奖励、HP/MP 和掉落字段；该消息没有“自动关闭奖励面板”字段。
- `BattleScene_HandleInput(0x62A2)` 仅在战斗 phase 为 `7` 且奖励状态已建立时，因真实输入调用
  `BattleScene_ExitAndCleanup(0x60C8)`；触摸路径 `HandleBattleCharTouch(0xB62)` 相同。
- `BattleScene_ExitAndCleanup(0x60C8)` 才会发送空 `WT 25/5`，随后清理 BattleScreen。
- `BattleAutoAction_TimerTick(0x2952)` 在奖励状态存在时不会代替输入执行该退出路径。

### 结论与边界

在当前静态客户端中，“奖励面板不点确认仍继续下一场”不存在可由服务端下行包安全触发的已证实协议。
在 `25/5` 前提前下发 `4/5` 已有负面运行时证据：新战斗会重入未销毁的旧 BattleScreen，表现为
重新刷怪、死亡动画循环或卡死。

服务端正确边界仍是 `4/7 -> 客户端原生输入 -> 25/5 -> scene poll -> 下一场 4/5`。若产品必须在
奖励面板不由玩家点击时继续挂机，只能另行实现一个明确启用的模拟器输入辅助：在只读确认已渲染的
`4/7` 奖励面板后，经现有硬件输入队列发送一次真实按下/释放，由客户端自行产生 `25/5`。它不能以
服务端提前开战或修改客户内存替代。

## 实现：挂机奖励自动确认（模拟器输入辅助）

`src/main.c` 提供 `CBE_HANGUP_AUTO_CONFIRM=1`（或
`--hangup-auto-confirm`）开关。所有 `CBE_CLIENT_ONLY` 客户端构建在未设置该变量时
默认启用，`bin/multiplayer/start-player-common.bat` 仍显式设置为 1；设置环境变量
`CBE_HANGUP_AUTO_CONFIRM=0` 可关闭。

其一次性、可审计链路为：

```text
已识别的场景挂机 4/5 + 4/11{type=1}
  -> 收到本场 4/7（仅记录，不改包）
  -> UpdateLcd 后只读确认 phase=7 且 gameState+1138=1
  -> 下一 scheduler tick 经 mouseEvent 队列发送一次 (120,200) 按下
  -> 80ms 后经同一队列发送释放
  -> 客户端 HandleBattleCharTouch / BattleScene_ExitAndCleanup
  -> 客户端原生 WT 25/5
  -> 服务端 scene_hangup_round_complete，下一次 scene poll 启动新场
```

该辅助只在上述 `4/5 + 4/11{type=1}` 已识别的场景挂机会话中对本场 `4/7` 生效；服务端不提前发送
新的 `4/5`。任何 `4/11{type=0}`、客户端已自行退出或 phase/result 状态不再可确认时都会取消这次
输入，不重试、不修改客户内存、寄存器、PC/LR 或网络包。客户端日志可按
`[info][hangup] reward_auto_confirm_*` 核对 `4/7 -> input -> 25/5` 链路。

## 2026-08-09：奖励确认后的续战回归

隔离场景 `hangup-auto-reward-continue-v1` 首次失败时曾显示为“下一场没有轮询”。对
`vm_net_mock_poll_push_if_due` 和异步 worker 的只读取证表明，这不是事实：客户端在本场
`25/5` 后持续完成 scene poll；服务端第一次 due poll 正确读取到尚余 `4410ms` 的奖励窗口，
保持到期前不发送 `4/5`。

首次失败的真正原因是测试把“三怪自动战斗 + 奖励冷却 + 下一场首个自动行动”都放在默认
`15s` 步骤超时内，测试在冷却到期前退出。将**仅该自动化阶段**的等待上限调整为 `60s` 后，
隔离运行 `hangup-auto-reward-continue-v1-20260809T024651560Z-29896` 通过，证据为：

```text
reward_auto_confirm_wait settlement=261 evidence=4/7
reward_auto_confirm_rendered ... phase=7 result=1
reward_auto_confirm_input press ...
reward_auto_confirm_client_exit ... source=auto-input-25/5
scene_hangup_round_complete ... battle=1
scene_hangup_reward_window_wait ... remaining_ms=4410
scene_hangup_start ... battle=2 source=scene-poll
mock_hangup_battle_start source=scene-poll ... resp=213
automation_hangup_action_response ... count=4
```

`result.json` 的通过条件为
`rendered-4-7-auto-input-native-25-5-next-4-5-and-4-6`：不仅确认没有崩溃，而且确认了客户端
自己关闭奖励面板、服务端在冷却后下发第二场、客户端进入第二场自动行动。临时轮询诊断已经
移除，避免在正常挂机时产生高频日志。

## 2026-08-09：奖励冷却中再次点击挂机导致“获取数据”与闪退

### 固定触发与首个偏离

该问题不是“结算面板未确认”的续战路径。它要求先完成一场自动挂机战斗、让客户端原生
发出结果面板关闭用的空 `25/5`，回到场景后在八秒奖励窗口仍未结束时再次点击场景挂机。
`HandleBattleEnterReq(0x01015E14)` 的真实先后顺序为：先发送
`2/10 { 2/10.Type=2, 25/3 }`，再写入 battle-entry state `3`，然后请求 mode-5 战斗切换。

旧服务端把这个**已经进入战斗事务**的请求改为：

```text
1/2/10 {}
1/25/11 { result=8, info="挂机奖励冷却中，请稍后" }
```

`JianghuOL.CBE:net_handle_info_banner_state(0x01010C7E)` 只把 `25/11` 的
`result/info` 写到中心提示条；它不读取、更不回滚 `HandleBattleEnterReq` 写入的
battle-entry state。因此该响应的首个契约违例是：发起 `2/10` 的等待事务没有收到能进入
BattleScene 的 `4/5`。随后旧逻辑又在未来 scene poll 自行发送 `2/2 + 4/5 + 4/11`，将
战斗数据重入到一个仍显示“获取数据”的原始事务中；闪退只是后续生命周期错乱的结果。

隔离失败运行
`hangup-auto-cooldown-restart-v1-20260809T031153728Z-42368` 已保留首偏离证据：

```text
automation_hangup_cooldown_tap ... client_exit=1
scene_hangup_prebattle_wait ... remaining_ms=5171
mock_hangup_battle_start ... response=2/10+25/11 resp=89
automation_hangup_cooldown_response ... actor_other_ack=1 battle_start=0
result=failed reason=hangup-cooldown-response-missing-4-5-start
```

该自动化只通过模拟器既有输入队列发送一次按下/释放，且等待条件是已经渲染过的 `4/7`、
客户端实际发出的 `25/5` 与恢复的原场景 owner；没有调用 UI 回调或修改客户内存。

### 修复

移除了 `vm_net_mock_scene_hangup_arm_prebattle_reward_wait()` 及其
`2/10 + 25/11` 冷却拒绝分支。奖励冷却是**结算授权**，不能作为客户端已经进入战斗的
`2/10` 的启动拒绝。现在每一个通过严格 `Type=2 + 25/3` 检验的场景挂机请求都走既有、
已由 `mmBattle:0x66CC` 消费的直接启动包：

```text
1/2/10 {}
1/2/2  { selected scene monster seed }
1/4/5  { side, battleinfo }
1/4/11 { type=1 }
```

八秒限制仍由战斗结算时的同一 MySQL 事务
`vm_net_mock_role_try_claim_monster_reward_cooldown()` 决定；它仍会拒绝重复经验、金钱和
掉落，不再用无法完成请求的 banner 代替 `4/5`。post-`25/5` 的自动续战冷却等待保留，
因为那条路径没有悬挂的初始 `2/10` 请求。

### 回归验证

`make -j2` 通过后，隔离运行
`hangup-auto-cooldown-restart-v1-20260809T031857112Z-48276` 通过：

```text
automation_hangup_cooldown_tap ... client_exit=1
automation_hangup_cooldown_response ... actor_other_ack=1 battle_start=1
automation_battle_handler local_pc=0x66cc ...
result=passed reason=native-cooldown-tap-response-4-5-and-second-battle-start-handler
```

同次验证会保存第二次直接响应的原始 `248` 字节包与渲染帧
`frames/004_hangup-cooldown-restart-battle-started.png`。相邻回归
`hangup-auto-reward-continue-v1-20260809T032028079Z-23084` 也通过，证明面板自动确认后
等待冷却、再由正常 scene poll 发起下一场的路径未被此次修复破坏。
