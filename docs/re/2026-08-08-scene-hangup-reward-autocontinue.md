# 场景挂机：奖励面板后自动续战

## 触发与预期

在场景中开启挂机自动战斗，三只怪物的最后一只死亡后，客户端会收到正常的
`1/4/7` 奖励结算并显示奖励面板。无需点击该面板，挂机应在完整播放最后动作后继续
下一场；手动取消自动战斗或手动关闭面板仍应保持原有语义。

## 链路与首次偏离

正常终局链路为：

```text
客户端 4/12 自动重放请求
  -> 服务端 4/6(最后攻击 + type-3 死亡动作) + 4/7(奖励)
  -> mmBattle HandleBattleActionMsg(0x6EB0) / HandleBattleSettleMsg(0x743C)
  -> 奖励面板 phase=7
```

原实现只在**没有** `4/7` 的终局中调用
`vm_net_mock_battle_schedule_terminal_close_after_actions()`；带奖励的自动挂机终局被
`!terminalStatusAppended` 排除。即使旧的延时字段在其它路径变为可投递，
`vm_net_mock_build_battle_pending_settlement_response()` 也会在检测到 `4/7` 面板后清掉
该字段并返回空包，等待玩家输入触发的 `25/5`。因此首次错误状态是“奖励面板已建立，
但没有未来的服务端续战边界”，不是奖励包或玩家点击本身。

隔离基线 `hangup-auto-reward-continue-v1-20260808T050721330Z-42560` 证明这一点：
服务端已发出最终 `4/6 + 4/7`，客户端未在奖励面板后发出下一场请求，场景在本阶段超时。

## 客户端与协议证据

- `mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)` 解析 `4/7` 的经验、金钱、HP/MP
  和掉落字段；它是奖励面板的权威数据来源。
- `HandleServerBattleCmd(0x7BD0)` 把 `4/5` 送入 `HandleBattleStartMsg(0x66CC)`，把 `4/6`
  送入 `HandleBattleActionMsg(0x6EB0)`；这两个是正常战斗初始化与动作播放入口。
- `4/11(type=1)` 是自动战斗开关的原生确认，之后客户端计时器产生真实 `4/12`，服务端再
  以普通 `4/6` 处理。
- 单独追加 `actioninfo type=4` 的负向实验
  `hangup-auto-reward-continue-v1-20260808T051934360Z-1856` 仍停在 `phase=7`，没有
  `25/5`；因此 type 4 不能与同包 `4/7` 组合来关闭奖励面板。
- 延迟下发 `4/11(type=0)+4/9` 的负向实验
  `hangup-auto-reward-continue-v1-20260808T052552527Z-27704` 只把 phase 写为 0，未使
  BattleScreen 清理，也没有后续 `4/12`。该对是无奖励/PvP 终局契约，不能套用于有奖励
  的场景挂机。

## 修复

当且仅当以下条件同时成立时，服务端为带奖励的场景挂机终局保留动作播放后的延时边界：

1. 当前活动会话是同一角色、同一 battle serial 的场景挂机；
2. 服务端自动战斗状态仍启用；
3. 最后一只怪物死亡且角色仍存活；
4. 已在最终 `4/6` 同包建立真实的 `4/7` 奖励面板。

到达由实际 `actionnum` 计算出的播放边界后，场景同步轮询下发既有的连续挂机启动族：

```text
2/2 (场景怪物移动种子) + 4/5 (场景怪物战斗) + 4/11(type=1)
```

它复用 `vm_net_mock_build_hangup_battle_start_response(NULL, 0, ...)`，不伪造客户端
确认、不会修改客户端内存或跳过 parser。客户端先解析新的 `4/5`，再由它原生的自动
计时器发 `4/12`，服务器返回下一场的 `4/6`。奖励面板在该原生战斗初始化/动作路径中
让位于下一场战斗。

手动关闭奖励面板时，原 `25/5 -> sceneHangupRestartPending -> poll -> 4/5` 路径保留不变；
手动点击自动战斗红叉时，`4/11(type=0)` 清除 scene-hangup 会话，因而不会满足上述条件。

## 自动化回归

场景：`hangup-auto-reward-continue-v1`，由
`scripts/run-shop-return-hangup-automation.ps1` 在独立端口、临时 schema、测试账号和
单独 artifacts 目录运行。

输入只包括登录过程的定时硬件队列事件以及一次场景挂机触摸 `(50,350)`；收到 `4/7`
后不发送任何确认或关闭输入。通过条件为：

1. 收到真实 `4/7`；
2. 输入计数在此后保持不变；
3. 收到下一场真实 `4/5`；
4. 客户端原生自动计时器继续发起并消费下一场 `4/12 -> 4/6`。

最终验证运行（未设置任何挂机自动续战环境开关）及其帧/包/日志目录：
`artifacts/automation/hangup-auto-reward-continue-v1-20260808T054123475Z-30376`。
该运行中服务端记录 `session=2 turn=1 ... response=126`，证明不只是收到新的启动包，而是
下一场自动动作已真正推进。
