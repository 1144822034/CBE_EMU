# 战斗中途开启自动后结算停滞

## 状态

- 日期：2026-08-07
- 阶段：已定位，待修复
- 范围：普通场景怪物战斗中，在已进行若干回合后通过 `WT 4/11 {type=1}`
  开启自动战斗；不涉及场景挂机跨战斗续战、复活、决斗或组队回合屏障。

## 复现步骤

1. 进入普通场景怪物战斗（本次运行是 `enemy=106, enemies=2`）。
2. 手动完成至少一回合后，在战斗菜单开启自动战斗。
3. 自动行动先将最后存活怪物打至低血量，再进行击杀。
4. 最后一个死亡动作播放结束后，客户端停止推进；自动已关闭，后续按键也不能
   正常关闭战斗或继续操作。

## 运行时证据

`bin/server_out.txt` 中同一 battle session `5` 的连续记录：

```text
mock_battle_auto_toggle type=1 session=5 turn=1 ... enabled=1 due_tick=4545 resp=33
mock_battle_auto_action ... session=5 turn=2 tick=4545 actionnum=2 ... response=4/6
mock_battle_terminal_close_deferred session=5 actionnum=2 ... not_before_tick=4587
mock_battle_settle enemy=106 enemies=2 victory=1 ... exp_gain=20 ...
mock_battle_auto_action ... session=5 turn=3 tick=4566 actionnum=2 ... response=4/6
mock_battle_terminal_close_deliver session=5 ... tick=4587 objects=2 response=close
```

这证明前两次自动行动和最终 `4/6` 都已按队列节奏送达；首次偏离不是自动战斗
调度或动作数量，而是最终动作边界后额外送出的两个终止对象。

最终击杀的 `4/6` 同包已经携带有效的 `4/7` 结算（运行日志中对应
`mock_battle_settle`）。该分支设置
`g_vm_net_mock_battle_settlement_sent_serial`，说明不是无奖励结算路径。

## 客户端合同

IDA 按 `binary_name=mmBattleMstarWqvga.cbm` 选择实例得到：

- `HandleServerBattleCmd` (`0x7BD0`)：
  - `4/11 {result=1,type=0}` 清除自动标记并 `SetBattlePhase(0)`；
  - `4/9 {result=1}` 只有自动标记仍为 1 或死亡标记为 1 时才
    `SetBattlePhase(8)`。
- `BattleScene_HandleInput` (`0x6258`)：当结算相位为 7 且
  `battle+1138 == 1` 时，任意输入优先调用 `BattleScene_ExitAndCleanup()`，
  而不是普通战斗菜单或自动取消逻辑。
- `BattleScene_ExitAndCleanup` (`0x60C8`)：普通战斗退出时发送空
  `WT 25/5`，随后清理战斗相位并返回场景。
- `HandleBattleSettleMsg` (`0x743C`) 解析 `4/7` 的等级、经验、金钱、
  HP/MP、`result` 与掉落显示字段，是结算面板的权威输入。

因此带奖励的胜利 `4/7` 必须保留到客户端原生结算相位，由客户端输入触发
`25/5` 退出。`4/11(type=0)` 后紧接 `4/9` 的顺序违反该合同：前者已把
`4/9` 所依赖的自动标记清零，后者不再推进；同时它抢先破坏了结算相位，导致
`0x6258` 不会进入原生退出路径。

## 根因

`src/server/mock_server_battle.c` 将「自动战斗中的最终动作播放边界」与
「无结算面板的终止控制」混为同一条 `terminal_close` 链路：

1. 最终自动动作即便同包已产生带奖励的 `4/7`，仍调用
   `vm_net_mock_battle_schedule_terminal_close_after_actions()`；
2. 后续 scene poll 的
   `vm_net_mock_build_battle_pending_settlement_response()` 只对
   `sceneHangupRewardPanelActive` 保留 `4/7`，普通战斗则追加
   `4/11(type=0)+4/9`；
3. 普通战斗错误地失去原生结果面板的输入/`25/5` 退出生命周期。

这不是客户端卡顿、自动动作超时或需要重试的问题；服务端错误投递了一个只适合
无结果面板/其他终止语义的控制组。

## 修复计划

1. 仅当未产生 `4/7` 结果面板（无奖励终止或显式不内联结算）时，保留原有
   延迟的 `4/11+4/9` 终止控制。
2. 带奖励的普通及场景挂机胜利都保留原生 `4/7` 结果面板；最终动作播放完后
   不再向普通 battle poll 注入 `4/11+4/9`。
3. 在普通结果面板的原生 `WT 25/5` 确认点清理服务端本轮的
   `AwaitingSettlement` / 自动定时标记；场景挂机仍由既有
   `sceneHangup` 所有者根据同一个 `25/5` 安排下一战，不能在普通清理路径中
   抢占。

## 回归边界

- 普通单人战斗：手动开启自动、击杀最后怪物后显示原生结算，确认一次后回到场景，
  且服务端记录普通结算关闭。
- 场景挂机：带奖励 `4/7` 仍由原生 `25/5` 确认，再由 scene-hangup poll
  发起下一场；不得出现 `4/11+4/9` 抢占结算面板。
- 无奖励冷却/决斗/复活：不改变其既有的无结果面板或复活终止合同。
- 新战斗开始：清理后的普通战斗状态不得泄漏到下一 session。

## 本轮修改与验证

- `mock_server_battle.c`：
  - 将自动战斗的延迟终止调度移动到 `4/7` 构造之后；只有确实没有
    结果面板时才调度 `4/11+4/9`。
  - `terminalFollowup` 与 pending-settlement 也按“是否已构造 `4/7`”而非
    “是否为 scene-hangup”决定是否保留原生面板。
  - 新增普通结果面板的 `25/5` 生命周期收口；scene-hangup 在该调用前仍保持
    活跃，因此不会被普通清理分支抢占。
- `mock_server_social.c`：scene-default `25/5` 先让普通 battle 收口，再交给
  scene-hangup 续战所有者。
- 静态核对：普通带奖励结束时 `terminalStatusAppended==true`，不再设置
  `terminal_close_not_before_tick`，也不会在后续 poll 构造 `4/11+4/9`。
- 构建：`make -j2` 通过（2026-08-07）。尚未启动或干预用户的服务端/客户端；
  运行时复测应观察到最终 `4/7` 后客户端发起 `WT 25/5`，服务端记录
  `battle_reward_panel_closed`，且不再出现同 session 的
  `mock_battle_terminal_close_deliver`。
