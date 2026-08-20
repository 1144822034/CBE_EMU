# 玩家切磋终局退出与普通复活链隔离

Date: 2026-08-19

Status: implemented-pending-runtime-regression

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

## 10. 终局 action 队列修正（2026-08-19）

### 新复现

切磋终局客户端崩溃：

```text
pc=05036198 lr=05036951 r0=4255de4c
address unavailable: 4255de5a
```

同次 `bin/server_out.txt` 的最后一个服务端动作事件为：

```text
duel_action_round_release ... actions=2 order=submit actors=0,1
damage=17,6 hp=6060/6078,0/1845 terminal=1 response=4/6+4/7
```

第一条 action 已将 actor 1 的 HP 降为零，第二条 action 却仍以 actor 1 为来源。
这说明异常发生前，服务端已经向 `4/6.actioninfo` 写入一个不再存活单位的后续动作。
它是需要修正的独立队列违规，但在第 11 节的复现中，修正后崩溃仍存在，故不能把它
当作本次退出崩溃的根因。

### 固件交叉证据

- `mmBattleMstarWqvga.cbm:sub_6EB0 (0x6EB0)` 按 actioninfo 顺序建立本地动作槽，
  不会在解析时跳过随后已失效的来源单位。
- `sub_7BD0 (0x7F64)` 会在同一网络事件中先调用 `sub_6EB0`，随后才由 case 7
  调用 `sub_743C` 解析结算。
- 崩溃 PC `0x05036198` 映射到 `sub_60C8+0xD0` 的清理回调区间；它位于结果面板的
  退出路径，不能作为绕过协议错误的修复落点。

### 修复

`vm_net_mock_build_duel_operate_response()` 现在依提交顺序解析双方 intent，但每次
写入 action 后立即检查目标 HP。若该 action 致死：

1. 保留这一条伤害 action 和最终 `4/7`；
2. 不再把已归零目标的待执行 intent 写入同一个 `actioninfo`；
3. 不扣除被跳过 action 的 MP；
4. 仍等待双方在本回合先提交 intent，且双方都收到同一个镜像终局包；
5. 结果面板关闭后继续由双方原生 `25/5` 释放 duel。

这不是按固定时间延迟结算，也不改变客户端状态。终局日志会记录
`post_defeat_actions=0`（另记被抑制的数量）；回归脚本要求该证据，确保致死之后
不存在后续播放 action。

## 11. 终局观察端快照不对称（2026-08-19）

### 新证据与修正后的根因陈述

在第 10 节的动作截断已生效后，两端仍稳定崩溃：

```text
pc=05036198 lr=05036951 r0=4255de4c
address unavailable: 4255de5a
```

该 PC 是 `BattleScene_ExitAndCleanup(0x60C8)` 的切磋退出分支，而不是 action
解包器。最新运行日志已证明终局仅有一条有效 action，且
`suppressed_post_defeat_actions=1`；因此“已死亡单位仍有后续 action”是已排除的
异常输入，不能继续当作本次崩溃的根因。

首个仍可观测的包契约差异是 `vm_net_mock_build_duel_action_packet()` 仅在
**观察端自己施放技能**时附带 `4/6.teaminfo`。终局中最后提交、已经被击败的一端
直连包为 304 字节，攻击方轮询包为 329 字节；这正是单行 `teaminfo` 的字段差异。两端随后都进入同一
`4/7` 结果路径，但一端带局部 MP 快照，另一端完全没有。这违反了
`InitActionSlot_B(0x6DBC) -> HandleBattleSettleMsg(0x743C)` 的终局共享缓存边界。

`sub_6DBC` 会按 battle actor 的 wire id 写入每个匹配单位的 MP 缓存；它支持多行
`teaminfo`，并不要求该字段只能描述本机施法者。普通非终局回合继续保留原有的最小
本机施法者行，避免扩张已验证路径。仅终局 `4/6` 现在为两个观察端都构造相同的两行
快照：双方 wire id、终局 HP 和终局 MP。随后同包 `4/7` 仍为无奖励结算，`hp/mp=0`
继续表示不修改持久角色；新增的 `fdata=切磋结束` 仅提供结果面板的正常文本行，
不发经验、铜钱、掉落、耐久或持久 HP/MP 写入。

### 验证边界

- `duel_terminal_packet` 必须对两端各记录一次，且 `teaminfo_rows=2`、长度为 28。
- 回归脚本直接解码两份 `teaminfo`，要求两个 14 字节重叠 tagged-i32 行、两个非零且
  不重复的 wire id，及两端原始快照完全一致。
- 仍须在真实双客户端复现终局和确认结果面板，确认两端均进入场景并发出原生 `25/5`；
  在此之前本节状态为 `implemented-pending-runtime-regression`。

## 12. 终局清理控制器失效取证（2026-08-20）

最新的双端复现确认第 11 节的终局快照已经实际下发，但仍在相同位置失败：

```text
duel_terminal_packet ... observer=<A> teaminfo_rows=2
duel_terminal_packet ... observer=<B> teaminfo_rows=2
pc=05036198 lr=05036951 r0=4255de4c
address unavailable: 4255de5a
```

`mmBattleMstarWqvga.cbm:sub_60C8` 的本地 `0x6198` 是退出清理分支，不是
`4/6` 或 `4/7` 的解包器。该分支在 `sub_259A(0)` 时已成功读取
`main-R9 + 0x285c`，而在中间的 battle-state 虚调用完成后，随后的
`STRB [controller+0x10]` 使用了无效控制器 `0x4255de4c`。因此：

- 完整两行 `teaminfo` 和 `fdata` 的试验没有修复本次崩溃，不能再将二者当根因；
- 当前已知的第一处损坏位于 `sub_60C8` 内两个正常清理步骤之间；
- 尚未确认是错误的 `4/7` 字段组合、切磋开始状态，还是结果面板路径选择使该清理
  回调释放/覆写了这个控制器，故不得在协议层继续猜测性修改。

为把损坏归属到最早的回调，当前构建加入了两项不改变客户状态的临时取证：

1. 服务端把每个终局 `4/6 + 4/7` 的完整 WT 字节写入
   `logs/duel-terminal-wire.log`，含 observer、duel serial 和 action serial；
2. 客户端会在 Battle `4/7` parser 入口自动监视 `R9+0x285c`，把 `sub_60C8` 的入口、
   `sub_259A(0)` 后、三个 battle-state 虚调用前后以及 `0x6198` 前后的控制器快照写入
   `logs/duel-exit-forensics.log`。同一文件还会记录三个虚调用的实际目标地址，以及该
   控制器槽的首次 guest write（PC、LR、调用栈顶部和旧/新值）。监视可用
   `CBE_TRACE_DUEL_EXIT=0` 明确关闭；它只读地记录已发生的写入，不修改客户内存、
   寄存器、PC/LR、响应字节或调度时序。

这些探针仅使用 `uc_mem_read`、寄存器读取和文件日志；不会修改 CBE/CBM 内存、
寄存器、PC/LR、响应字节或调度时序。下一次复现必须同时保存这两个文件和
`bin/server_out.txt` 的终局片段，再按控制器首次变化的边界回溯对应的 client state
和 WT 字段。

## 13. 零奖励结算渲染契约（2026-08-20）

### 更正后的 PC 映射与根因

前一节把 `0x05036198` 直接按固定 `0x05030000` 装载基址换算为
`sub_60C8+0xD0`，该换算错误。Battle CBM 由内存池动态装载；本次 player-1/
player-3 的 `hangup-protocol.log` 已记录实际 `code_base=0x0502EF40`，所以：

```text
0x05036198 - 0x0502EF40 = 0x7258
```

`mmBattleMstarWqvga.cbm:sub_7228+0x30` 的实际指令是：

```text
7252  LSLS R0, R1, #6
7254  ADDS R0, R0, R5
7256  ADDS R0, #0x90
7258  LDRB R0, [R0,#0xE]
```

崩溃寄存器与该调用点逐项吻合：`R5=0x0105493C` 是
`R9+0x3D6C` 的结算临时行，`R1=R4=0x01054252` 是
`R9+0x3682` 的文本缓冲区，左移后的访问地址正是
`0x4255DE5A`。调用者 `sub_7794:0x7A0C` 以这两个地址调用
`sub_7228`，因此这不是控制器释放或 `teaminfo` 缓存损坏。

`HandleBattleSettleMsg(0x743C)` 每次先清零 `R9+0x3D6C`，再将
`4/7.exp`、`4/7.gold` 与客户端旧总值相减，把增量写到 `+8`、`+12`。
当前切磋包发送角色当前总 EXP/铜钱，即两个增量皆为零；`sub_7228` 在两项都不为正时
进入上面的错误索引分支。此前增加两行 `teaminfo` 或 `fdata` 无法改变这两个增量，故
不可能修复本次崩溃。`docs/re/2026-07-27-monster-reward-cooldown.md` 已记录了相同的
PC、寄存器形状和固件渲染契约。

### 修复契约

友好切磋是无持久奖励的临时对局，不能为规避渲染错误伪造 EXP 或铜钱，也不能把
`4/7` 用作零奖励结果面板。终局包改为按既有无奖励控制路径构造：

```text
4/6 final actioninfo
4/11 { result=1, type=1 }
4/9  { result=1 }
```

三个对象保持同一 WT 事件中的上述顺序，`4/6` 仍先让客户端登记最终伤害动画；不再
附加 `4/7`、`4/8`、`4/4` 或普通 type-3 死亡动作。`4/11(type=1)` 是 `4/9(result=1)`
进入 `sub_259A(5)` 原生退出 phase 的先决状态；此前的 `type=0` 会清除该状态，导致
终局动作播放后仍停留在 Battle screen。它不读取结算数值或 `fdata`，因此不会触发
`sub_7228`。此前为错误的结算面板假设附带的 terminal-only `teaminfo` 快照也已移除，
最终 `4/6` 回到常规动作对象格式。服务端继续仅在两端各自原生空 `25/5` 到达后释放
duel，持久 HP/MP、经验、铜钱、掉落和耐久均不变。

此前按固定基址监视 `sub_60C8` 的临时探针不会命中实际 `0x7258`，已移除，避免留下
不能回答当前根因的常驻观测。

### 验证边界

- 两端终局 WT 都应为 `4/6 + 4/11(type=1) + 4/9`，不得包含 `4/7`。
- 两端均不再到达 `sub_7228+0x30` 的零增量结算路径，不出现
  `0x4255DE5A` 访问。
- 最终动作播放完成后，两端客户端各自发出原生 `25/5`；服务端第二个确认后才释放
  duel。
- 失败方不进入 `7/14` 复活选择，双方持久角色的 HP/MP、EXP、铜钱不改变。

## 14. 无奖励终局退出标志修正（2026-08-20）

### 复现与首个偏离

在第 13 节的零奖励 `4/6 + 4/11 + 4/9` 实现后，双方不再崩溃，但任一方被击败后
仍可能停在 Battle screen，既没有结算也没有原生 `25/5`。最新终局日志显示每个观察端
都收到了同一 112 字节三对象包，随后客户端继续发送 `4/2`，服务端只能返回 finished-duel
空确认。这证明末尾的 `4/9` 没有启动退出 phase；不是服务端过早释放或遗漏第二份终局包。

`mmBattleMstarWqvga.cbm:HandleServerBattleCmd/sub_7BD0` 的指令证据：

```text
case 11 (0x7C16): parse type, store it to battleObject+1140
case 9  (0x7CB2): result=1 only branches to sub_259A(5) when
                  battleObject+1140==1 or battleObject+1136==1
```

旧的自然终局对象为 `4/11 {result=1,type=0}`，所以它在同包的 `4/9` 之前主动清除
第一个先决标志。两个标志都未置位时 case 9 直接返回，Battle screen 没有进入清理流程。
这正是停留画面的首个错误状态。

### 修复与验证边界

自然终局专用构包器改为 `4/11 {result=1,type=1}`，紧跟同包的 `4/9 {result=1}`。
`type=1` 是客户端已有 case-11 协议字段，令 case 9 可进入 `sub_259A(5)`；它不伪造
奖励、复活、场景或客户端内存状态。普通怪物战斗、复活和主动逃跑未改。

回归现在要求两端终局包的 case-11 `type` 都为 `1`，并检查服务端日志中的
`4/6-before-4/11(type=1)-4/9`。运行时双端验证仍要求观察到：最终动画完成后每端各发
一个原生 `25/5`，第二个确认后才 `duel_release`，且没有后续普通 `builtin-battle-operate`
或死亡复活链。2026-08-20 已通过 `make -j2`、PHP 和 PowerShell 静态语法校验；隔离
双端回归因当前环境缺少 `CBE_AUTOMATION_MYSQL_PASSWORD` 尚未执行。

## 15. 自然死亡动作与 phase 覆盖（2026-08-20）

### 新的首个偏离

第 14 节的 `4/11(type=1)+4/9` 仍会使失败方停留在 Battle。重新按指令而非伪代码
摘要核对 `HandleServerBattleCmd(0x7BD0)` 后，先前对 phase 的结论不成立：

```text
4/6                 -> sub_259A(5)  at 0x7F84
4/11 type=1         -> sub_259A(8)  at 0x7C88
4/9  result=1       -> sub_259A(8)  at 0x7CE4/0x7F84
```

同一 WT 回调结束时 phase 已经是 `8`，而不是 `5`。`BattleScene_MainLoop/sub_5444`
只在 phase 等于 `5` 时调用 `sub_4BE8` 播放 action 队列；因此同包的 `4/11/4/9` 覆盖了
刚由 `4/6` 建立的播放 phase。

此外，终局 `actioninfo` 只有致命 HP delta。固件的普通死亡完成路径要求后一条
type-3 action：`sub_4BE8` 在 type `3/4` 的完成分支 `0x4C64` 写入 phase `7`，随后
`sub_6258` 才能以客户端已有的清理条件进入 `sub_60C8` 并发送原生 `25/5`。缺少
type-3 动作也就不会产生该 phase-7 收尾信号。

### 修复契约

切磋自然终局现在只下发一个 `4/6` 对象，`actionnum=2`：

```text
action 0: 致命伤害，目标为被击败者
action 1: type=3，actor 为同一被击败者
```

不再附加 `4/11`、`4/9`、`4/7`、`4/8` 或 `4/4`。type-3 没有 child/value 载荷，完全沿用
普通战斗既有的死亡 action 格式；它不是复活、逃跑或奖励结算。两个观察端各自使用
镜像 wire slot 编码，因此第二条 action 在两端都指向各自画面中的同一死亡目标。

服务端仍只在双方客户端各自原生 `25/5` 到达后释放 duel，且不写入持久 HP/MP、经验、
铜钱、掉落或耐久。回归更新为验证单一 `4/6`、两条 action、第二条为匹配第一条目标的
type-3，及两端镜像编码；同时继续禁止零奖励 `4/7`、复活和逃跑路径。
