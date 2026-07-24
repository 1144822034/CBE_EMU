# 组队战斗终局队员结算崩溃

## 触发条件与原始证据

两名同场景队员进入同一场景怪物战斗。第一回合后队员 `guest00024/10024`
的共享 HP 已变为 `0/112`；第二回合由仍存活的队长
`guest00023/10023` 击杀最后一只怪物。队员下一次 event-7 场景轮询收到
`resp=289` 后崩溃：

```text
queue_scene_poll connect=2 event=7 resp=289
PC=0x05187258 LR=0x05187A11
r1=0x01054252 r4=0x01054252 r5=0x0105493C
```

服务端原始日志位于 `tmp/mock-service-team-roster.stdout.log` 最后一次复现：

```text
team_battle_action_deliver ... observer=e8bfb4a5 ... terminal=1 objects=2 resp=289
mock_battle_settle ... role=10024 battle_role=10024 victory=0 exp_gain=0 ...
```

同一终局动作对队长的直接 `4/2` 响应是正常的胜利结算；日志记录为
`mock_battle_settle ... role=10023 ... victory=1 exp_gain=10`。

## 客户端解析与首次偏离

运行时 PC 映射到动态模块 `mmBattleMstarWqvga.cbm` 的偏移 `0x7258`。
`DrawBattleHpBar(0x7228)` 仅在它的临时显示行 `r9+0x3D6C` 的 `+8`、`+12`
及当前单位标志均为零时，才会把调用者的文本缓冲区指针当作单位索引左移 6 位：

```text
7252  LSLS R0, R1, #6
7254  ADDS R0, R0, R5
7258  LDRB R0, [R0,#0xE]
```

复现寄存器中的 `R1=0x01054252` 正是调用者传入的文本缓冲区，计算出的非法地址
为 `0x4255DE5A`；故 PC 不是根因。调用者 `DrawBattleCharacter` 的返回地址
`0x7A11` 也直接确认它在渲染结算后的该临时行。

`HandleBattleSettleMsg(0x743C)` 先清零同一块 `r9+0x3D6C` 临时区，再按照
成功结算字段填充结果面板。服务端向该死亡队员发送了终局动作 `1/4/6` 后跟
`1/4/7`，但仍沿用单人战斗判定：

```text
victory = (enemy_hp == 0 && observer_hp > 0)
```

因此它在已经由 `terminalVictory=1` 确认的队伍胜利中构造了无奖励、全零变化的
`4/7 { result=1 }`。`result=1` 让客户端进入胜利结算渲染路径，而全零结果行未
满足该路径的临时显示对象契约。这是第一个错误状态；崩溃是随后渲染该错误结算
状态的结果。

## 正确契约与修改点

队伍战斗的胜负属于冻结的共享战斗，而非某个观察者的当前 HP：只要
`terminalVictory=1`，每名参与者（包括战斗中死亡者）都必须取得自己的成功
`1/4/7` 结算、经验/金钱与掉落判定，且保留该角色的真实 `HP=0`。只有队伍整体
失败才进入死亡/复活流程，不能在队伍胜利事件中混入单人失败结算。

修改将把“强制队伍胜利”作为显式参数传给终局 `4/7` 构造和角色持久化结算：
普通单人战斗仍保留 `enemy_hp==0 && own_hp>0` 判定；队长终局释放和轮询投递给
队员的 `terminalVictory` 事件改为按共享胜利结算。这不会伪造 HP、屏蔽客户端
回调或丢弃场景轮询包。

## 验证要求

- 两人队伍中一人死亡、另一人击杀最后怪物：双方均完成结果面板，死亡者 HP
  仍为 0，但得到一次自己的胜利奖励，且不崩溃。
- 两人均存活时终局、队长死亡而队员终结、以及队伍整体失败仍走各自正确路径。
- 确认每个账号每场仅结算一次；重登、切图和后续场景轮询不重复发放。

## 实施与当前验证

- `vm_net_mock_append_battle_terminal_status_objects()` 和
  `vm_net_mock_battle_save_terminal_role_state()` 现在显式接收
  `forceTeamVictory`。普通战斗调用一律传 `false`；仅共享队伍终局释放和已排队的
  `terminalVictory` 队员事件传 `true`。
- 胜利覆盖仍以 `enemy_hp==0` 为前提，因而不会把队伍失败、逃跑或普通死亡改写成
  胜利；角色的 `HP=0` 也不被修改。
- `make -j2` 已于 2026-07-24 通过。由于旧服务占用链接输出，先停止 PID 52344，
  成功链接后已用本地 `127.0.0.1:19090` 重新启动服务（PID 456）。

仍需用上述原始双客户端步骤复测运行时 UI；成功日志应显示死亡队员的
`mock_battle_settle ... victory=1 team_victory=1`，并保留其 `team_hsp` 的
`hp=0/...`，不得再出现 `PC=0x05187258`。
