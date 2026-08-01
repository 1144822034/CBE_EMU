# Spar battle wrong sprite + challenged-player crash

Date: 2026-07-25

Status: implemented (visual + terminal follow-up)

## 1. 当前卡点

- 可见现象：切磋发起方战斗画面左侧对手形象变成默认女天机；切磋结束后双方闪退。
- 触发方式：同场景双端 `4/14` 邀请 → 同意 → `4/10` 开战 → 打到一方 HP=0 → 双方退出。
- 本轮目标：开战包左侧视觉正确；终局 `4/11+4/9` 后迟到的 `4/2` 不得再进 PvE 零经验结算。

## 2. 运行时 / 静态证据

### 形象

- `1/4/10 battleinfo` 左侧完整行语法（`HandleBattleStartMsg(0x66CC)`）：
  `id, hp, hpMax, mp, mpMax, name, visual_group, visual_variant`，右侧仅 id/HP/MP。
- 场景 actorinfo / title 约定：`visualGroup = sexGroup(1..2)`，
  `visualVariant = jobIndex(0..2)`；`sub_23F6(variant, group)`。
- 旧 duel builder 把字节写成 `jobIndex, sexGroup`，等于把 group/variant 对调；
  非法索引时客户端落到默认女天机立绘。

### 闪退

- PvE 已默认关闭独立 type=3 终局动作（`CBE_BATTLE_TERMINAL_ACTION_ENABLED=0`），
  因为 type=3 会干扰后续目标选择 / 与结算竞态。
- 切磋路径仍无条件附加 type=3 死亡动作，再延迟投递 `4/11+4/9` 退出。
- 被击杀端更容易走进 Battle.cbm 死亡确认 UI，与友好切磋的 `4/9 result=1` 退出叠在一起。

## 3. 根因

1. 开战包左侧视觉字节顺序违反 subtype-10 `visual_group/visual_variant` 契约。
2. 切磋终局默认附带 type-3，与已验证的 PvE 退出契约不一致。

## 4. 修改

- `duel_begin` 冻结双方 `jobIndex/sexGroup`（优先 durable role，其次 session）。
- `duel_start` 左侧写 `sexGroup` 再写 `jobIndex`，日志打印这两字节。
- 切磋 type-3 与 PvE 共用 `CBE_BATTLE_TERMINAL_ACTION_ENABLED`（默认关）。

## 7. 2026-07-25 follow-up：回合错乱 + resp=5 卡住

### 证据

客户端在终局后出现 `queue_data ... resp=5`；过程中一方未等对方动画就出手。

### 根因

1. `turnIndex` 在接受 `4/2` 后立刻切到对方，但对方可能尚未 poll 到上一招
   `4/6`；对方若此时提交，本机操作响应会早于未送达的上一家动作，回放顺序颠倒。
2. 终局后对迟到的 `4/2` 回空包，Battle.cbm 仍停在战斗屏 → 卡住。

### 修改

- 终局：`4/4 {result=1}` 离场（不打开结算面板）；结果靠 `1/3/3` 系统消息。
- 击杀 `4/6` 与离场分拍；默认约 1s 播放窗；禁止切磋 `4/7`/`4/8`（横幅或闪退）。
- 切磋中 `4/3` 道具回空确认，避免 PvE item 路径。

## 8. 2026-07-25 follow-up：双方提交后再开回合

### 契约（对齐组队战 round barrier）

- A 提交 `4/2` 后只 `duel_round_defer`（零对象 WT），等 B 也提交。
- 双方都提交后 `duel_round_resolve`：按敏捷 `firstTurnIndex` 顺序结算，合并一条 `4/6`。
- 首个提交武装超时（默认 `CBE_DUEL_ROUND_COMMIT_TIMEOUT_MS=20000`）；未提交方默认普攻。

### 修改

- `roundCommitMask` / `roundCommitOperate` / `roundCommitDeadlineTick` 替代顺序 `roundActedMask`。
- 每击独立 `4/6`（单行 `teaminfo`），同包 merge；禁止双技能塞进同一 `actioninfo`。

## 10. 2026-07-25 follow-up：技能后蓝条归零 + 崩溃

### 证据

首回合双方 `operate=203` 与 `operate=3`，合并 `hits=2` 的一条 `4/6`；客户端
技能后 MP=0，数回合后崩溃。

### 根因

切磋 `current_team_count=1`，`InitActionSlot_B` 只消费一行 `teaminfo`。双技能
同包写两行时，本机施法者 `unit+1344` 仍为 0，type-1 结束把蓝写成 0，并可能
错位解析后续流。

## 9. 2026-07-25 follow-up：HP=0 后 Battle.cbm 不离场

### 证据

`terminal=1` 与 `duel_terminal_deliver ... 4/11+4/9` 已发出，但双方继续 `4/2`，
release 后落入 `mock_battle_operate_ignore empty-ack`，客户端仍停在战斗屏。

### 根因

HP=0 后对**双方**都发 `4/7+4/8(autorevive)`。该序列是死亡 UI 离场契约；胜方仍
存活时走 autorevive 会卡住 Battle.cbm。另：播放窗未到就 `exit-redeliver` 抢发离场包。

### 修改

- 败方：`4/7+4/8+4/11+4/9`（`4/7.mp=0`）
- 胜方：`4/4 result=1`
- `terminalNotBeforeTick` 之前的 `4/2` 只空确认

## 11. 2026-07-25 follow-up：HP=0 后不立即离场

### 证据

日志：`duel_terminal_arm ... delay_ticks=60`（`now`→`not_before` 差 60），
击杀 `4/6`（`resp=111`）送达后仍空等，再 `duel_terminal_deliver`。

### 根因

`CBE_DUEL_TERMINAL_DELAY_TICKS` 默认 60，且代码强制 `delayTicks < 25 → 25`。
mock-service 的 scheduler tick = `ms / VM_SCHED_FRAME_MS`（100ms），故播放窗
实际为 **2.5–6 秒**，伤害动画与回合结束后 Battle.cbm 仍停住。

### 修改

- 默认 delay=0；去掉 25-tick 下限。
- 击杀 `4/6` 投递后由下一拍 `duel_terminal_deliver` 发离场（不与 `4/6` 同包）。

## 12. 2026-07-25 follow-up：胜方“逃跑成功”+ 败方闪退

### 证据

客户端：`queue_scene_poll ... resp=129` 后出现 `resp=23`（纯 `4/4`），胜方 UI
显示“逃跑成功”；败方闪退。服务端曾把胜方终局写成 escape `4/4`，并把离场与击杀
`4/6` 同 WT 合并。

### 根因

1. `1/4/4 result=1` 是 `HandleServerBattleCmd` 逃跑成功分支，不是切磋终局契约。
2. 败方在同一包内先消费击杀 `4/6`（HP=0）再走 `4/7 result=1` 胜利结算行，落进
   与组队终局队员崩溃同类的零血结算渲染路径。

### 修改

- 切磋终局**双方**统一 `4/7(零奖励+恢复 online HP)+4/8+4/11+4/9`。
- 击杀 `4/6` 与离场分开发送（先 action，再 terminal poll）。
- 主动逃跑请求方仍只用 `4/4`。

## 13. 2026-07-25 follow-up：双方 `resp=321` 闪退

### 证据

```text
duel_terminal_deliver ... resp=321 exit=4/7+4/8+4/11+4/9
```
双方收到后均闪退。代码旁证：`mock_battle_operate_ignore` 注释写明
“Spar exits with 4/11+4/9”；零奖励 `4/7` 会走 `DrawBattleHpBar` 崩溃
（同组队终局队员结算）。

### 根因

切磋未武装 PvE settle 会话，却下发了 PvE 胜利/复活链 `4/7+4/8+…`。

### 修改

- 切磋终局恢复为双方仅 `4/11+4/9`。
- 继续：击杀 `4/6` 先投递；迟到 `4/2` 重投 `4/11+4/9`；主动逃跑仍 `4/4`。

## 14. 2026-07-25 follow-up：`4/11+4/9` 离场失败卡住

### 证据

```text
duel_action_deliver ... terminal=1 resp=66
duel_terminal_deliver ... resp=51 exit=4/11+4/9
duel_operate_post_terminal ... exit-redeliver resp=51
```
客户端收到后仍停在战斗屏：受击方 HP=0，攻击方循环攻击动画，双方可点操作但
无效果（空确认/重投离场包）。

### 根因

`4/11+4/9` 只是 PvE 结算链尾部；真正清战斗标记并退出的是 `4/8`
（`HandleServerBattleCmd` `0x7DF6..0x7E98`）。无前置 `4/7`/`4/8` 时，尾包无法
拆掉 Battle.cbm。另：delay=0 使 `4/8` 与击杀动画竞态。

### 修改

- 双方离场改为 `4/8+4/11+4/9`（仍禁止 `4/7` 与终局 `4/4`）。
- `CBE_DUEL_TERMINAL_DELAY_TICKS` 默认 10（约 1s 播放窗）。

## 15. 2026-07-25 follow-up：结束正常但提示为空

### 证据

`4/8+4/11+4/9` 可离场；结算提示框文案为空。

### 根因

`4/8` 调 `BattleSettle_UpdateCharAttrs()`，提示/属性增量来自前置 `4/7` 缓存；
无 `4/7`（尤其无 `fdata`）则空白。

### 修改

- 离场恢复为 `4/7+4/8+4/11+4/9`：`4/7` 零奖励、恢复 online HP，并写
  `fdata`（胜方「切磋胜利」/ 败方「切磋失败」）。
- 仍与击杀 `4/6` 分拍 + 播放窗，避免此前同包/零血结算闪退。

## 16. 2026-07-25 follow-up：`4/7` 再次双端闪退

### 证据

`duel_terminal_deliver ... resp=339 exit=4/7+4/8+4/11+4/9` 后双方闪退
（含 alive 座位 `dead=0 recover_hp=204` 与 fdata）。

### 根因

切磋 subtype-10 不能走 `HandleBattleSettleMsg(4/7)`；与是否分拍、是否回血、
是否写 fdata 无关。

### 修改

- 离场固定 `4/8+4/11+4/9`（可离场）。
- 胜负提示改为离场时排队 `1/3/3 type=5`：「切磋胜利」/「切磋失败」。
- 战斗内结算框可能仍短暂空白（无 4/7 缓存）；以系统消息为权威结果文案。

## 17. 2026-07-25 follow-up：取消结算横幅

### 证据

两拍「先 `4/7` 再 `4/8+…`」仍双方闪退；`4/8+4/11+4/9` 可离场但出现**空横幅**。

### 根因

- 切磋 subtype-10 不能走 `HandleBattleSettleMsg(4/7)`。
- `4/8` 仍调用 `BattleSettle_UpdateCharAttrs()` 打开结算面板；无 `4/7` 缓存则空白。

### 修改

- 离场改为 `4/4 {result=1}`（与逃跑成功同分支，清战斗标记、不打开结算面板）。
- 胜负只走 `1/3/3 type=5`：「切磋胜利」/「切磋失败」。
- 客户端内置「逃跑成功」文案可能仍闪一下（4/4 分支自带）；不以结算横幅为准。

## 18. 2026-07-25 experiment：`4/7` 双方 +1 经验

### 假设

先前切磋 `4/7` 闪退可能与组队终局同类：**胜利 `result=1` 但经验增量为 0**
（`DrawBattleHpBar` / 零增量结算行），而非 subtype-10 绝对禁止 `4/7`。

### 修改

- 离场武装时对双方 durable 角色各 `+1` EXP 并 `role_db_save`。
- 离场包改回 `4/7(exp=total,+1 可见增量,fdata,recover)+4/8+4/11+4/9`。
- 击杀 `4/6` 仍与离场分拍；系统消息保留备份。

### 复测结果（同包 4/7+4/8、以及两拍延迟 4/7）

`+1` 经验后**不再闪退**，但结算横幅仍空。

### 根因（空横幅）

结果面板在**终局动作播完时**就按当前缓存开画。`4/7` 若落在击杀 `4/6`
之后的下一拍，缓存已被拷成全零 → 空横幅（`battle-server-flow` 延迟结算证据）。
两拍「先单独 4/7」同样太晚。

### 修改（对齐 PvE）

- 击杀 `4/6` **同 WT 内联** `4/7`（在 `build_duel_action_packet` 内
  `append` 后 `finish_wt`，不是二次 merge）：`exp/gold` 为奖励后总量，
  `hp/mp=0/0`（显示增量），`fdata` 胜负文案，并 durable `+1 exp/+1 gold`
  以避免零增量闪退。
- 横幅停留后再发 `4/8+4/11+4/9` 拆场（仍不与 4/6 同包）。
- 日志：`duel_settle_inline ... exit=4/6+4/7(pve-append,+1exp,+1gold)`，
  随后 `duel_terminal_deliver ... exit=4/8+4/11+4/9`。

## 19. 2026-07-25 follow-up：继续对齐 PvE 同包 append

### 证据

二次 merge 的「动作包 + 独立 settle 包」与 PvE「同 WT object append」不一致；
切磋 `4/7` 若写非零 `sparExitRecoverHp/Mp`，也偏离 PvE 默认 `hp/mp=0`。

### 修改

- 删除 bundle 层 merge 内联；仅保留 action 包内 `append_duel_spar_status7`。
- `4/7` 固定 `hp=0`/`mp=0`，奖励 `+1 exp` 与 `+1 gold`。
- 迟到座位仍可由 `duel_settle_deliver` 单独补发 `4/7`（再 hold 后拆场）。

## 20. 2026-07-25：空横幅根因（缺对手 type-3）

### 证据

- PvE 终局：`4/6` 含对怪物的 type-3 死亡动作，动作播完后开结算面板；
  同包 `4/7` 填缓存。见 `2026-06-25-battle-server-flow.md`。
- 同文档负向证据：内联 `4/8 autorevive` →「battle ending immediately and a
  **blank prompt**」——`4/8` 不是正常结算开画触发器。
- 切磋此前：同 WT `4/6+4/7(+1exp/fdata)`、两拍 `4/7` 再 `4/8`、同包
  `4/7+4/8` 均**仍空横幅**。字段与 timing 已对齐仍空 → 不是单纯缺 `fdata`
  或延迟 `4/7`。
- 切磋 `4/6` 只有伤害记录，**没有**对倒下对手的 type-3；胜方从未走 PvE
  「敌方死亡动作结束 → 开面板」路径，用户只看到随后 `4/8` 的空白提示框。

### 根因陈述

首个偏离：友好切磋终局 `4/6` 缺少对**对方** wire 的 type-3 死亡动作（PvE
击杀怪物契约）。违反「结算面板由终局死亡动作完成时开画并读取已填的 `4/7`
缓存」。`4/8→UpdateCharAttrs` 只会刷新/拆战斗并露出空白提示，不能替代该
开画路径。不得对**本方** wire 发 type-3（会进玩家死亡确认 UI）。

### 修改

- 胜方终局 `4/6`：伤害记录后追加 type-3（actor=对手 wire 1），再同 WT
  append `4/7`。
- 败方：同样对本方倒下 wire 0 追加 type-3 + `4/7`（试开败方结算横幅）。
- 横幅 hold 后拆场改为 **`4/11+4/9`（不再发 `4/8`）**，避免空白提示替换已填横幅。

## 21. 2026-07-25：取消 `4/8` 空白替换横幅

### 证据

胜方已显示「切磋胜利」后，`duel_terminal_deliver ... exit=4/8+4/11+4/9`
把横幅换成空白（与 battle-server-flow「4/8 blank prompt」一致）。

### 修改（已回退部分）

- 曾改拆场为仅 `4/11+4/9` → 胜方 **卡在 Battle.cbm**，`4/2` 反复收到
  `resp=51`，且 operate 在 `terminalPendingMask` 清零后仍重入
  `duel_terminal_phase` 导致 `release_not_before` 不断延长。
- 恢复拆场 `4/8+4/11+4/9`（`4/8` 才能清战斗标记）。
- operate 终局只对仍挂在 `terminalPendingMask` 的座位投递一次拆场。
- 败方终局仍带倒下座位 type-3。
- 空白提示若仍在 `4/8` 离场瞬间闪一下：属于 `4/8` 刷新结算 UI 的已知边角；
  权威结果文案以 type-3 开画的横幅与 `1/3/3` 为准。可用
  `CBE_DUEL_BANNER_HOLD_TICKS` 加长阅读窗。

## 22. 2026-07-25：挑战结果提示与 25/11 卡死

### 根因

- 向地图主动推送 `25/11 {result=8,info}` 后，客户端进入 info-banner 等待，
  并回发空 `1/25/11`；服务端未处理（`response=0`）时出现「斗」状态卡住。
- 同拍的空白 `4/8` 结算条会盖住过早的中心提示。

### 修改

- **禁止**切磋结果推送 `25/11`。到期只发 `25/12` 清条 + `1/3/3` 系统消息。
- 空 `1/25/11` → `builtin-info-banner-clear12-ack`（`25/12`）。
- `4/8` 提前（`TERMINAL_DELAY=4`，`BANNER_HOLD=1`）；结果延后
  （`RESULT_MSG_DELAY` 默认 40）。
- `4/8` arm 时清掉 `sparBattleReadyPending` / 邀请回复挂起。
- 禁止已 arm 后 `post_release` 重发 `4/8`。

### 复测期望日志

`spar_result_deliver ... delivery=25/12+1/3/3`；
若仍有空 `25/11` 请求，应见 `mock_info_banner_clear12_ack`，不应
`ignored-unhandled-server-only`。

## 23. 2026-07-25 experiment：切磋 `4/7` 再加胜负 `fdata`

在现有「同 WT `4/6(+type3)+4/7(+1exp/+1gold,hp/mp=0)` → hold → `4/8…`」
路径上，给 `4/7` 补回 `fdata=挑战成功/失败`。

### 2026-07-25 复测

`duel_settle_fdata` 已写入，但过短 hold 时 `4/8` 会刷空横幅。现默认
`CBE_DUEL_BANNER_HOLD_TICKS=50`（**5 秒**，`VM_SCHED_FRAME_MS=100`）：从投递
`4/7` 起算再发 `4/8`。

## 24. 2026-07-25：切磋辅助技能误伤对手

### 证据

- PvE 已按 `skill.dsh` 分支处理：`td=1` 单体治疗、`td=0/2` 自身/群体 buff、
  无属性封魔类走非伤害（见 `2026-07-25-night-batch-skills-equip-items.md`）。
- 切磋 `duel_round_resolve` 原先一律调用 `duel_damage`：技能缺负向 `hpChange`
  时回落 ATK，对敌方扣血；`4/6` 固定 `actor→对手 wire` + 负向 valueA。

### 根因

首个偏离：切磋回合结算未复用 PvE 的
`targets_friendly_group_heal` / `friendly_group_modifier` /
`enemy_status_no_damage` 契约，辅助技能被当成攻击技能。

### 修改

- `duel_apply_operate` 对齐 PvE：治疗回血到施法者、buff 写入座位
  `modifiers[]`、封魔类对敌 0 伤害；均设 `supportNoDamage`。
- `4/6`：自身目标 `targetWire=actorWire`；support 用正向/零 valueA。
- 满血治疗/纯 buff 仍发 `4/6`（不再因 `damage==0` 跳过）。
