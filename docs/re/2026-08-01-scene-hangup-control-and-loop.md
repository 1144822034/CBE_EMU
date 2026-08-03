# 场景挂机：取消、怪物组与结算续战（2026-08-01）

## 触发与现象

用户人工复现的场景挂机存在三个关联现象：

1. 战斗内的“取消自动”没有停止后续挂机轮次；
2. 每场挂机战斗都固定为一个怪物；
3. 胜利后必须手动关闭结算页，客户端才发出回到场景的后续事件，下一场战斗才会开始。

当前可用运行记录显示挂机开战始终为：

```text
mock_hangup_battle_start ... enemy=105 enemies=1 ... subtype=5 ...
```

此前同一段记录没有出现 `mock_battle_auto_toggle type=0` 或
`scene_hangup_stop ... reason=battle-auto-cancel`。这只能证明服务端没有
处理到已确认的取消请求；不能据此臆造场景侧的取消包。

## 客户端与服务端证据

- `mmBattleMstarWqvga.cbm:BattleScene_HandleInput(0x6258)`：自动状态下的
  取消操作发送 `4/11 { type=0 }`。
- `HandleServerBattleCmd(0x7BD0)` case 11：仅 `result=1` 生效；`type=0`
  恢复普通战斗状态，`type=1` 进入自动状态。
- `HandleServerBattleCmd(0x7BD0)` case 9：`4/9 { result=1 }` 仅在自动标记
  (`battle+1140==1`) 或客户端自身的结束标记存在时选择 phase 8；它不触发结算后的
  Battle 场景退出。
- `HandleServerBattleCmd(0x7BD0)` case 8：读取 `4/8` 的 `autorevive/info`，属于复活
  完成路径，不能作为普通胜利的收尾消息。普通胜利必须保留 `4/7` 奖励面板，等待其
  客户端原生确认后发出的 `25/5`；在这个边界前下发下一份 `4/5` 会重入未释放的战斗状态。
- `vm_net_mock_build_hangup_battle_start_response()` 原先把
  `battleEnemyCount` 直接写成 `1`；而普通场景怪物战斗已通过
  `4/5.battleinfo` 的客户端解析，使用 `vm_net_mock_battle_roll_enemy_count(true)`
  生成 1–3 个同一场景怪物节点的敌人。
- 有奖励的普通终局路径在 `4/6` 后内联 `4/7` 结算。经验、金钱由 `4/7` 数值字段结算；
  `fdata` 是仅在有物品掉落时显示名称和数量的可见文本。`4/4` 是逃跑结果，`4/8` 是
  复活结果，`4/9` 只切换 phase，三者都不能替代奖励面板的原生确认路径。

## 根因陈述

### 固定单怪

触发条件是任何通过 `2/10(Type=2)+25/3` 开始的挂机战斗。首个错误状态是
挂机 builder 在形成 `4/5.battleinfo` 前硬编码 `battleEnemyCount=1`，违背了
该 builder 与正常场景怪物 builder 共用的 1–3 敌人 battleinfo 契约。

### 结算后必须手动点击

触发条件是有奖励的挂机胜利。`4/7` 承载奖励结算；结果面板确认产生安全的场景侧
`25/5`，再启动下一场 `4/5`。服务端此前错误地补发 `4/8`，它不是无提示关闭，而是复活
消息，会把正常结算替换为空白信息提示。

### 取消自动

客户端原生取消契约已确认，现有 handler 也在收到有效 `4/11(type=0)` 时同时清理
按会话保存的 `sceneHangup*`。首次偏离仍未定位：当前运行证据中没有该请求的处理日志。
本次只增加一次性、低频的 type-0 入站/拒绝日志，用来区分“客户端未发送/未到达”与
“到达后被会话状态拒绝”；在获得该包之前不扩展未证实的 `4/12` 或虚构场景取消协议。

## 修改

- 挂机路径改用已经用于正常场景怪物战斗的敌人数量生成器；可由既有
  `CBE_BATTLE_ENEMY_COUNT` 或 min/max 调试配置收敛到固定数量，但默认随机为 1–3。
- 仅对当前角色、当前 battle serial 对应的活跃场景挂机会话：有 `4/7` 结算的胜利包
  在最终动作后保留奖励面板，等待原生 `25/5`；不追加 `4/8` 或提前清除自动标记的
  `4/11(type=0)`。无奖励终局沿用独立的无结算面板关闭契约。
- 非挂机的普通战斗、队伍、切磋、复活继续沿用原有终局对象顺序，避免把该修复扩展到
  没有此会话契约的路径。
- 对有效 `4/11(type=0)` 的入口增加 `mock_battle_auto_cancel_request` 日志；若没有
  活跃战斗则记录 `mock_battle_auto_cancel_rejected`。已有成功回包日志
  `mock_battle_auto_toggle type=0` 保持不变。

## 人工复测

由用户手动操作，不由代理启动服务：

1. 开启场景挂机并连续观察多场，日志中 `mock_hangup_battle_start` 的 `enemies` 应在
   1–3 之间变化；多怪时左侧应由同一已验证场景怪物节点的模型复制生成。
2. 有奖励终局应显示 `4/7` 奖励面板并在用户确认后发出空 `25/5`，再由 scene poll
   进入下一场；八秒奖励窗口内的无奖励终局使用其独立关闭路径。
3. 战斗中点击取消自动；日志应依次出现
   `mock_battle_auto_cancel_request`、`mock_battle_auto_toggle type=0` 和
   `scene_hangup_stop ... reason=battle-auto-cancel`，本场结束后不得有
   `mock_hangup_battle_start source=scene-poll`。若只出现前一条或拒绝条目，保留该段
   请求/响应日志继续取证。

## 2026-08-03 更正与隔离回归

原先将 `4/9` 视为终局关闭的描述已由三怪隔离复现推翻；之后将 `4/8` 视为无提示关闭
的试验又被真实客户端的空白信息框推翻。完整触发条件、首个偏离、
`4/7 奖励面板 -> 25/5 -> 下一场 4/5` 的客户端调用链，以及红叉取消的回归工件见
[`2026-08-03-scene-hangup-terminal-close.md`](2026-08-03-scene-hangup-terminal-close.md)。
