# 自动战斗沿用上一操作的协议合同

## 状态

- 日期：2026-08-07
- 阶段：已实现；已完成构建验证，待客户端真实回归。
- 范围：普通怪物战斗、场景挂机战斗、组队战斗与切磋战斗的自动回合；不修改客户端
  CBE/CBM 代码、内存或输入状态。

## 触发与首次偏离

触发步骤：在战斗中手动使用一个技能，随后从战斗菜单开启自动战斗。实际结果是后续回合
总是普通攻击；预期结果是客户端自动计时后继续执行这名角色上一次已被服务端接受的
`index` / `Operate` 选择。

当前服务端的首次偏离不在 `4/6` parser 或动画，而在自动开关后的动作来源：

1. `src/server/mock_server_battle.c` 把 `WT 4/11(type=1)` 当作服务端轮询调度的开关。
2. `vm_net_mock_build_pending_auto_battle_action_response()` 随后的 scene poll 人工构造
   `WT 4/2 { index=0, Operate=0 }`。
3. 这个伪造请求覆盖了玩家刚才的技能选择，普通战斗 builder 因而正确地生成了
   **错误来源**的普通攻击 `4/6`。

这也解释了运行日志里的自动动作始终显示 `operate=0`：不是客户端把技能降级为普攻，
而是服务端在收到客户端自动计时请求前已经替它制造了普攻请求。

## 客户端取证

按 `binary_name=mmBattleMstarWqvga.cbm` 选择当前 IDA 实例（不依赖实例 ID）：

- `BattleMenu_SelectOption` (`0x5F78`) 的自动菜单项发送 `WT 4/11`，字段
  `type=1`，随后进入客户端自动阶段。
- `BattleScene_HandleInput` (`0x6258`) 在自动状态下由用户确认取消时发送
  `WT 4/11(type=0)`；这是取消合同。
- `BattleAutoAction_TimerTick` (`0x2952`) 在客户端自动计时到期时发送
  `WT 4/12`，调用参数为 `(4, 12, 0, 0)`，没有 `index`、`Operate` 或目标字段，
  随后进入等待服务器 `4/6` 的阶段。
- `HandleServerBattleCmd` (`0x7BD0`) 的 case 11 消费自动状态确认；case 6 把
  `actioninfo` 交给 `HandleBattleActionMsg` (`0x6EB0`) 逐项播放。

因此 `4/12` 是客户端原生的“自动回放上一操作”请求，不是取消包；服务端必须在这一个
真实客户端请求到达时，从对应角色已成功提交的最近一次可回放战斗操作构造动作结果。

## 根因陈述

服务端错误地把客户端的 `4/12` 自动回放请求解释成取消，同时通过 scene poll 预先伪造
固定的 `4/2 {index=0, Operate=0}`。该实现违反了 `0x2952` 证明的请求时序和无操作字段
的服务端记忆合同，导致上一次技能选择在第一个自动回合前已经丢失。

## 修复设计

1. 每个账号活动角色保存最后一次已成功完成的、可回放的普通攻击或技能
   `index` / `Operate`；仅在真实 `4/2` 经现有战斗 builder 接受并生成动作响应后写入。
2. 删除 scene-poll 人工 `4/2` 调度。客户端自己的 `0x2952` 计时器发出精确的空
   `WT 4/12` 后，再以保存的操作进入既有单人、组队或切磋 builder，复用既有 `4/6`
   codec、回合屏障、HP/MP 与结算状态机。
3. 没有历史可回放选择的首场自动战斗，明确使用一次普通攻击作为 bootstrap；这是因为
   原生 `4/12` 不携带操作字段，且该角色不存在任何可恢复的已接受操作。日志必须标记
   `selection=bootstrap-physical`，不能伪称为“上一操作”。
4. `WT 4/11(type=0)` 仍是唯一取消合同；`WT 4/12` 不清除自动状态，也不清除挂机跨战斗
   continuation。

## 已排除的假设

- 不是 `4/6` 动作编码把技能改为普攻：自动路径在进入相同 builder 前就固定写入了
  `Operate=0`。
- 不是客户端自动计时未执行：`0x2952` 明确有原生 `4/12` sender，当前 mock 的预发
  scene-poll `4/6` 才使该路径失去请求归属。
- 不是自动取消由 `4/12` 表示：`0x6258` 的取消分支发送的是带 `type=0` 的 `4/11`。

## 验证边界

- 手动技能 `Operate=N` 后开启自动，后续客户端 `WT 4/12` 必须有
  `mock_battle_auto_replay ... saved_operate=N`，并由现有 `mock_battle_operate` 记录同一个
  `operate=N`。
- 手动普通攻击后自动，应回放 `Operate=0`。
- `4/11(type=0)` 后不得再接受 `4/12` 自动回放；自动与场景挂机 continuation 均停止。
- 组队未轮到该成员、敌方回合、战斗结算与切磋非本方回合仍由原 builders 拒绝或推进，
  不得因自动回放绕过回合权属。
- 尚未执行客户端回归前，只能确认构建和静态请求/状态归属；不能把它报告为体验验收通过。

## 本轮实现与验证

- `src/server/mock_server_battle.c`：删除 scene-poll 自动动作生成器；将空 `WT 4/12`
  严格识别为自动回放请求，按活动角色保存的最后一次已接受 `index` / `Operate` 进入
  原有单人、组队或切磋 builder。该内部 intent 仅在收到真实 `4/12` 后构造，不会单独
  排队或下发给客户端。
- `src/server/mock_server_social.c`：scene poll 不再抢先产生自动 `4/6`。
- `src/server/mock_server_equipment_npc.c`：将最后可回放操作纳入账号活动状态快照，避免
  多账号切换泄漏并允许场景挂机跨战斗延续同一技能。
- `src/server/mock_server_dispatch.c`：`4/12` 的已处理来源改为
  `builtin-battle-auto12-replay`；取消仍由 `4/11(type=0)` 处理。
- 构建：`make -j2` 通过（2026-08-07）。

客户端回归时，先手动释放一个有 MP 的技能，再开启自动；日志应依次出现
`mock_battle_auto_remember ... operate=N`、`mock_battle_auto_toggle type=1`、
`builtin-battle-auto12-replay`、`mock_battle_auto_replay ... saved_operate=N`，随后
同一回合的 `mock_battle_operate ... operate=N`。首次从未手动操作就开启自动时，日志必须
明确写为 `selection=bootstrap-physical`。
