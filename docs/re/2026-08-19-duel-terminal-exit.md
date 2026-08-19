# 玩家切磋终局退出与普通复活链隔离

Date: 2026-08-19

Status: implemented-pending-regression

## 1. 复现与预期

- 复现：切磋中任一方 HP 归零。存活方停留在 Battle screen；失败方进入普通
  死亡复活提示。失败方选择“不复活”后收到“当前无需复活”，再确认会离开程序。
- 预期：友好切磋只结束临时 duel，不改变角色持久 HP/MP，不进入普通怪物战斗的
  死亡、复活石或复活请求链；双方都由 Battle screen 正常回到原场景。

## 2. 首次偏离与运行时证据

`bin/server_out.txt:343-360` 的同一次复现按顺序出现：

```text
duel_action_round_release ... hp=6066/6078,0/1845 ... terminal=1
duel_terminal_arm ... pending=03
duel_terminal_deliver ... response=4/11+4/9
duel_terminal_deliver ... response=4/11+4/9
duel_release ...
net_send ... wt=25/5 ... source=builtin-scene-default-event
mock_battle_operate ... rolehp=0 ... reason=battle-operate-death
net_send ... wt=4/2 ... source=builtin-battle-operate
mock_battle_death_prompt_choice result=2 ... reason=not-dead-or-state-unavailable
session_offline ... reason=explicit-disconnect
```

角色 `10001` 在 duel 临时快照中归零，但持久角色没有进入普通死亡态。第一处偏离
不是最后的退出，而是终局 `4/6` 追加了普通 type-3 死亡动作；第二处偏离是
`4/11(type=0)+4/9(result=1)` 没有关闭手动切磋的 Battle screen。服务端随后过早
释放 duel，尚在战斗画面的 `4/2` 才错误落入普通 `builtin-battle-operate`。

移除 type 3 并把自然胜负临时改成 `4/4(result=1)` 后，双方虽然能退出 Battle
screen，但用户复现得到“逃跑成功”。这不是文案层问题：`4/4` 本身就是主动逃跑
结果对象。自然胜负与主动逃跑共用同一 terminal 对象，是这一轮新的首次协议偏离。

后续真实客户端复现进一步推翻了当时的 `4/8` 收尾结论：最终 `4/6` 中的两条
动作尚未播完，延迟投递的 `4/8` 已使双方离开 Battle screen，并显示一个空白提示框。
服务端日志记录该对象在 `duel_action_round_release` 后固定 25 个 scheduler tick
投递。该时长不能覆盖 `sub_6EB0` 串行播放的两个动作，更重要的是，`4/8` 的 `info`
不是胜负提示文本，而是复活恢复状态。首个错误状态因此是把自然胜负误建模为复活收尾。

## 3. 固件证据

| binary | function/address | evidence |
| --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `HandleBattleActionMsg/sub_6EB0`, `0x6EB0` | type 3/4 是无 child 的特殊动作，不是普通伤害记录。 |
| `mmBattleMstarWqvga.cbm` | `sub_30D4`, `0x30D4` | type-3 死亡完成会设置战斗单位死亡态和 `gameObject+986`，随后调用普通自动复活检查。 |
| `mmBattleMstarWqvga.cbm` | `RequestAutoRevive/sub_3008`, `0x3008` | 检查复活石并进入复活石/商城提示；`sub_2F10` 构造 `1/7/14 {result=1|2}`。 |
| `mmBattleMstarWqvga.cbm` | `HandleServerBattleCmd/sub_7BD0`, `0x7CB2` | subtype 1/9 的 `result=1` 只有在 `battleObject+1140==1` 或 `+1136==1` 时才进入退出 phase。 |
| `mmBattleMstarWqvga.cbm` | `HandleServerBattleCmd/sub_7BD0`, `0x7C16` | case 11 把 `type=0` 写到 `battleObject+1140`，所以先发 `4/11(type=0)` 会清除 case 9 的一个退出前提。 |
| `mmBattleMstarWqvga.cbm` | `HandleServerBattleCmd/sub_7BD0`, `0x7DF6-0x7E98` | subtype 8 读取 `result`、`autorevive` 和 12 字节 `info`，随后调用 `BattleSettle_UpdateCharAttrs/sub_2C50`；这是复活收尾，不是胜负结算。 |
| `mmBattleMstarWqvga.cbm` | `BattleSettle_UpdateCharAttrs/sub_2C50`, `0x2E88-0x2ED2` | subtype 8 路径立即发送空 `25/5` 并清理 Battle screen，故会截断未完成的 `4/6` 动画。 |
| `mmBattleMstarWqvga.cbm` | `HandleBattleSettleMsg/sub_743C`, `0x743C` | subtype 7 按 `exp, lastexp, curexp, persentexp, energy, energymax, gold, level, result, bagstatus, hp, mp, itemnum, iteminfo` 读取结算字段，进入原生结果面板。 |
| `mmBattleMstarWqvga.cbm` | `BattleScene_ExitAndCleanup/sub_60C8`, `0x60C8-0x6132` | 结果面板确认后由客户端发送空 `25/5`，随后释放画面资源并回到场景 phase。 |
| `mmBattleMstarWqvga.cbm` | `HandleServerBattleCmd/sub_7BD0`, `0x7F06-0x7F4A` | subtype 4 的 `result=1` 调用 `sub_3092`、`sub_23E4`、`sub_2C50`，清理战斗和死亡相关状态。 |
| `mmBattleMstarWqvga.cbm` | `BattleScene_ExitAndCleanup/sub_60C8`, `0x60C8-0x6132` | Battle screen 正常析构时发送空 `25/5`，随后释放画面资源并回到场景 phase。 |

`4/4(result=1)` 分支引用固件字符串 `0x8044`“逃跑成功”，因此只属于主动逃跑。
`4/9(result=1)` 只有自动标志存在时才进入 phase 8，随后
`BattleAutoAction_TimerTick/sub_2952(0x2952)` 会发送 `4/12`，它是下一回合控制而
不是终局。角色 full-info 写入的 `UI+0x4FE` 也不是切磋模式标志，不能由服务端
伪造。以上三条均已排除。

## 4. 可检验根因

友好切磋终局没有区分“自然胜负”和“主动逃跑”，并先后错误复用了普通怪物战斗
的 type-3 死亡动作、`4/11+4/9` 自动回合控制以及 `4/4` 逃跑结果。type 3 把失败
方送入普通复活链，`4/11+4/9` 不关闭手动 duel，`4/4` 虽可关闭画面却必然显示
“逃跑成功”。之后又把自然胜负误换成复活专用的 `4/8`，使它绕过了 `4/6` 动画和
`4/7` 结果面板。自然胜负的正确契约是最终 `4/6 + 4/7` 同包，主动逃跑才是 `4/4`。

## 5. 修复契约

1. 致死 duel 回合仍以普通/技能动作把临时 HP 扣到 0，但不追加 type-3 死亡动作。
2. 自然胜负的最终 `4/6` 与专用无奖励 `4/7` 在同一 WT 包中下发。`4/7` 使用当前
   持久角色的经验、等级与铜钱快照，`hp=0`、`mp=0`、`itemnum=0`、空 `iteminfo`、
   `autorevive=0`，不发 `fdata`，不写入奖励、掉落、耐久或 duel 临时 HP/MP；主动逃跑
   仍使用 `4/4 {result=1}`。
3. 终局结果面板投递后 duel 仍归服务端所有。每端只有在收到其原生空 `25/5` 后才完成
   exit acknowledgement；双方确认或相应会话断线后才释放 duel。
4. 退出确认前的 late `4/2` 和 `4/12` 继续由 finished duel 接收并返回合法零对象 WT，
   绝不落入普通怪物 battle handler；自然终局不得再从 scene poll 补投 `4/8`。
5. `25/5` 只有在该端结果面板已随最终动作投递时才能清除 exit acknowledgement，避免把战斗
   期间无关的场景默认事件误当成退出完成。

## 6. Negative Evidence

- 不能仅延长 `duel_release` 的固定时间：远程延迟和动画长度不构成业务完成证据。
- 不能吞掉普通 `4/2` 或强制清空普通战斗全局：这会隐藏 owner 错误并影响正常
  怪物战斗。
- 不能继续使用 type-3 后只屏蔽 `7/14`：失败方已经进入了错误的客户端死亡状态。
- 不能把 `4/9(result=1)` 当通用关闭包：固件分支明确依赖两个模式标志。
- 不能把 `4/4(result=1)` 用于自然胜负：它的固件语义和可见文案都是主动逃跑。
- 不能只把 `4/8` 的固定延迟调大：动画时长不构成结算完成证据，而且 subtype 8 的固件
  语义仍是复活收尾。
- 不能伪造 `UI+0x4FE` 来满足 `4/9`：该字段来自 actor full-info，不是 duel 模式。

## 7. 实现结果

1. `vm_net_mock_build_duel_action_packet()` 不再为 duel 致死目标追加 type-3
   死亡动作；最终 HP delta 仍由本轮普通/技能 action 承载。
2. 自然终局改为 `SETTLEMENT_PANEL`。终局回合构造 `4/6 + 4/7`，不再建立待投递的
   subtype-8 terminal；主动脱离、取消和断线通知继续设置 `ESCAPE` 并构造 `4/4(result=1)`。
3. duel 保留逐端 `terminalExitPendingMask`。最终动作中的结果面板投递完成不释放；只有
   该端随后发送空 `25/5` 才清除对应 bit，双方清零后才 `duel_release`。
4. finished duel 显式接管 late `4/2`、`4/12`、`4/11` 和重复 escape，统一返回合法零对象
   WT，不再落入普通 battle owner，也不从 scene poll 追加第二个自然终局对象。
5. scene-default `25/5` 先交给 duel 生命周期；只有没有 finished duel 时才进入
   普通战斗/挂机的 scene-default 收尾。
6. 断线收尾只把断线端视为已退出，保留仍在线对端的终局投递与 `25/5` 确认。

## 8. 验证结果

- [x] `make -j2` 已重编译并链接 `jh-online-server.exe`；PHP 与 PowerShell 回归脚本
  已通过静态语法检查。
- [ ] 隔离回归待在配置 `CBE_AUTOMATION_MYSQL_PASSWORD` 后，以临时
  `jh_online_autotest_<guid>` schema 重跑。
- [ ] 两端自然终局应各含两个 combat actions 的 `4/6` 和一个无奖励 `4/7`，不得含
  `4/8`、`4/4` 或 `4/11+4/9`。
- [ ] 第一端 `25/5` 后 duel 仍持有另一端；第二端 `25/5` 后才释放。
- [ ] 结束后可立即建立下一次切磋，且两名隔离角色的持久 HP/MP 不受 duel 临时值污染。

隔离自动化命令：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD='123456'
.\scripts\run-duel-round-barrier-automation.ps1
```

旧证据目录 `artifacts/automation/duel-round-barrier-v1-20260819T135411389Z-47320/`
只证明了现已确认错误的 `4/8` 服务端契约，不能作为客户端行为验收。

## 9. 协议边界

自然胜负只使用最终 `4/6 + 4/7` 的结算面板语义；主动点击逃跑、对局取消或对端
断线通知仍使用 subtype 4。测试必须分别检查终局对象、结果字段和客户端原生 `25/5`
生命周期，不能仅以“退出了 Battle screen”作为两条路径都正确的证据。
