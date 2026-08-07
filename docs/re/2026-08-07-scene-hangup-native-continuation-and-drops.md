# 场景挂机原生续战与掉落取证（2026-08-07）

## 问题与复现

在 `01桃花岛_04.sce` 开启场景挂机自动战斗，首场胜利、结果面板关闭后，
有时不会进入下一场；同时观察到本次战斗没有任何物品掉落。

本次取证使用用户提供的原始服务端日志 `bin/server_out.txt`。角色为
`guest00001/10001`，目标怪物为 `106`（瘴气蝙蝠）。不以客户端崩溃点或
“没有掉落”这个最终画面作为根因。

## 客户端与协议契约

- `mmBattleMstarWqvga.cbm:HandleServerBattleCmd(0x7BD0)`：
  - `4/6` 调用行动解析后进入行动阶段；
  - `4/7` 只交给 `HandleBattleSettleMsg(0x743C)` 解析结算；
  - `4/11 { result=1,type=1 }` 打开客户端自动战斗并进入自动阶段。
- `BattleScene_ExitAndCleanup(0x60C8)` 在原生结算面板被关闭、战斗场景退出后
  才发送空的 `25/5`。这不是可以在同一回调中重入战斗的信号，而是场景侧后续
  请求/轮询可以启动下一场的生命周期边界。
- 当前服务端在收到该 `25/5` 时把已完成的场景挂机会话标为
  `sceneHangupRestartPending`，并等待下一次场景侧发包。

## 首个偏离与根因

原始日志中的关键顺序如下：

```text
scene_hangup_round_complete ... battle=1 next_tick=612
mock_challenge_battle_start id=106 ... subtype=5 ... objects=2
scene_hangup_stop ... battle=1 restart_pending=1 reason=battle-session-mismatch
```

第二行是客户端在已关闭原生结算面板后发出的标准场景碰怪 `1/4/1`
请求。分派器先调用一般的
`vm_net_mock_build_challenge_interaction_response_ex()`；该 builder 把它当成
普通战斗，清除自动标志、递增全局战斗 serial，而且没有把新 serial 归属到既有
`sceneHangup` 会话。下一次场景轮询发现旧会话 serial 与全局 serial 不同，因而
停止挂机。

首个错误状态不是“没有轮询”，而是：**原生 `25/5` 已经证明上一场的结果面板
关闭，但紧随其后的有效场景碰怪续战请求没有继承 pending 场景挂机会话。**

## 掉落链路核对

同一段原始日志在每个挂机结算前都有：

```text
mock_battle_drop_gate enemy=106 role=10001 slot=1 item=19 rate=100 \
  task_material=1 remaining=0 policy=ok eligible=0 rolled=0 grant=0
```

这证明：

1. 挂机自动战斗确实进入 `vm_net_mock_battle_grant_reward_once()`，并未绕过
   普通战斗的背包写入和任务推进路径；
2. 怪物 106 当前只有一个来源于 `task.dsh` 的任务材料掉落（19，100%）；
3. `vm_net_mock_task_material_drop_policy()` 已成功读取任务状态，但角色没有一个
   `task_state=1` 且仍缺少物品 19 的任务，故 `remaining=0`；
4. 任务材料在没有对应任务或任务已完成时不掉落，是既有并且正确的反刷取契约。

因此本次不通过取消任务材料门槛来“制造掉落”。普通物品应在怪物管理的
`server_monster_drops` 中另外配置；当对应任务处于未完成状态时，任务材料仍由
相同的结算路径授予、写入背包并推进任务。

## 修复计划

在 `vm_net_mock_build_challenge_interaction_response_ex()` 中仅当下列状态同时满足时，
把后续 `1/4/1` 归类为原生挂机续战，而不是普通碰怪：

1. 活动会话已启用挂机且 `sceneHangupRestartPending=true`；
2. 当前可见场景、挂机场景和当前场景名称精确相同；
3. 会话角色、当前战斗角色和旧战斗 serial 一致；
4. 旧场景怪物战斗已经胜利结算（未 armed、`AwaitingSettlement=1`、敌方 HP 为 0）；
5. 请求仍是场景怪物 subtype-5 路径，且不属于队伍战斗。

该分支复用请求携带的真实 live-node `index/posx/posy`，响应追加原生
`4/11 {result=1,type=1}`，随后将新 battle serial 记回同一个挂机会话。它不伪造
计时器、不重放旧 `4/5`，也不修改客户端内存。

## 验证边界

构建后应验证：

1. 原失败顺序中 `25/5 -> 1/4/1` 产生 `4/5 + 4/11`，日志出现
   `source=native-challenge-continuation`，且不再出现 `battle-session-mismatch`；
2. 下一场的自动行动正常进入 `4/6`；
3. 有普通掉落配置时，挂机结算会记录 `eligible=1`、`grant>0`，并在结算后的
   背包刷新对象中下发；
4. 有匹配且未完成任务时，任务材料同样 `eligible=1`，数量不超过剩余要求；无
   匹配任务时仍为 `remaining=0`，不会掉落任务材料。

