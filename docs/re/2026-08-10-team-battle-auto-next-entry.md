# 组队战斗自动模式：下一场 `4/12` 回放请求被错误拒绝

## 状态

- 日期：2026-08-10
- 阶段：已实现；构建完成，待客户端回归。
- 范围：组队战斗在一场胜利后再次开始时的原生自动回放；不修改客户端二进制、内存、
  寄存器或输入状态。

## 触发与首次偏离

两名同场景队员在第一场组队战斗中开启自动并正常完成战斗。结算完成后由队长进入第二场
组队战斗，两个客户端都进入 `1/4/5` 战斗开始 parser。随后它们各自立即发出精确的空
`WT 4/12` 请求（`len=9`、一个 `1/4/12` 空对象），服务端却记录：

```text
team_battle_deliver serial=2 observer=c540f629 party=2 ... resp=215
[error][network] unhandled wt=4/12 len=9 objects=1 first=1/4/12:0
[error][network] unhandled wt=4/12 len=9 objects=1 first=1/4/12:0
```

先前的第一场同一份日志中，自动请求经过正常路径，包含
`mock_battle_auto_replay ... mode=team`。第二场没有该日志，也没有
`battle_auto_replay_build_failed`，因此首次偏离发生在 auto-replay builder 的早期准入
条件，而不是动作 `4/6` 的生成、队伍回合屏障或怪物动画。

## 客户端契约

按 `binary_name=mmBattleMstarWqvga.cbm` 选择 IDA 实例后：

- `BattleAutoAction_TimerTick` (`0x2952`) 在客户端自动状态仍有效、倒计时到期且没有
  loading/battle-end gate 时，调用网络接口参数 `(4, 12, 0, 0)`，再调用
  `SetBattlePhase(6)` 等待服务端动作结果。
- `BattleScene_HandleInput` (`0x6258`) 中用户取消自动的路径发送的是
  `WT 4/11 {type=0}`；`4/12` 不是取消协议。
- `HandleServerBattleCmd` (`0x7BD0`) 的 case 6 消费服务端下行行动结果；没有这个结果时，
  客户端自然停留在等待阶段。

所以第二场真实发出的 `4/12` 本身是客户端仍保有自动状态的直接证据；服务端不得用自己
上一场结束时清理的临时标志否认它。

## 根因

`vm_net_mock_build_battle_auto12_replay_response()` 将全局临时变量
`g_vm_net_mock_battle_auto_enabled != 0` 作为所有 `4/12` 的前置条件。该变量在普通战斗
结算确认和新一场被动队员 `4/5` 发送时都会被服务端清零：

```text
vm_net_mock_battle_on_scene_default_event()       -> vm_net_mock_battle_auto_reset()
vm_net_mock_build_pending_team_battle_start_response() -> vm_net_mock_battle_auto_reset()
```

它既不是客户端的自动开关，也不是按客户端/队伍归属的状态。因此第二场活跃组队战斗中的
原生 `4/12` 被全局旧状态误拒绝，导致 dispatch 落到 `unhandled`，客户端等待 `4/6` 而卡住。

## 修复原则

对 **严格匹配的空 `WT 1/4/12`**，若发送者是当前 `battleActive && !battleFinished` 队伍
战斗的真实成员，则该请求本身是该客户端自动状态的权威证据，应交给既有同步组队行动
builder。普通单人/切磋路径仍保留原有自动开关准入，防止将任意 `4/12` 解释成战斗操作。

此变更不伪造 `4/2`、不自动推动回合，仍必须等客户端 timer 的真实请求；取消仍只有
`4/11(type=0)`，已结束/非成员/非当前组队战斗的 `4/12` 继续拒绝。

## 验证计划

1. 两名队员在第一场中开启自动并胜利。
2. 进入第二场组队战斗，不重新点击自动。
3. 两端第一次 `4/12` 必须显示 `builtin-battle-auto12-replay` 和
   `mock_battle_auto_replay ... mode=team`，随后由原有队伍回合屏障决定何时下发 `4/6`。
4. 手动取消自动后，第二场不得无故生成自动动作；已结束、离队或切图成员的 `4/12` 仍为
   未处理/拒绝，不得改变队伍状态。

## 本轮实现与构建

- `src/server/mock_server_battle.c`：将 `4/12` 的包格式校验、活动队伍查找与成员归属
  检查置于全局自动临时标志之前。严格匹配且属于当前未结束组队战斗的成员，可由该真实
  `4/12` 重新确认服务端自动状态并进入原有同步组队行动 builder；其余模式仍必须已有
  `4/11(type=1)` 确认状态。
- 运行时日志新增 `mock_team_battle_auto_resume`，只在这种跨战斗、服务端临时标志已清理而
  客户端又明确发出 `4/12` 的情况出现。
- `git diff --check` 与 `make -j2` 于 2026-08-10 通过，已重建
  `bin/jh-online-server.exe`。本轮未启动、停止或替换用户正在使用的服务端/客户端；尚待
  按上述原始两客户端路径进行真实 parser 与回合回归。
