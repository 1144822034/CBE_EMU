# 怪物战斗奖励：每角色八秒结算窗口

## 状态

已实现并由独立服务回归验证：首次获奖、断线换 client id 后窗口内获胜、窗口到期后的
再次获奖均通过。

## 固定触发链路与客户端契约

场景怪物战斗从 `WT 4/1` 开始，玩家操作经 `WT 4/2` 进入终局。客户端的
`mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)` 会解析终局 `1/4/7` 的
`exp`、`lastexp`、`curexp`、`persentexp`、`gold`、`level`、`hp`、`mp` 等字段，
并据此结束战斗界面和刷新角色状态。已有 `docs/re/2026-06-28-battle-reward-persistence.md`
记录了该字段顺序与含义。

八秒内再次击杀仍须正常结束战斗，但不能把“无奖励”编码成一个 EXP、金钱均为零的
成功 `4/7`。该格式并不是客户端可表示的零奖励状态；其正确的终结对象另见文末的
2026-07-28 勘误。

## 首个偏离与根因

原先 `vm_net_mock_battle_grant_reward_once()` 只有
`g_vm_net_mock_battle_rewarded_serial == g_mockBattleOperateSessionSerial` 的内存内
幂等保护。它仅能防止同一场战斗的重复 `4/7`/终局回调，不会限制新的战斗，也不能
跨客户端连接或服务重启保留状态。

该函数正是首次决定怪物经验、可配置掉落、背包写入和任务材料推进的权威位置；随后
`vm_net_mock_role_apply_battle_settlement()` 另行持久化经验/金钱。两个终局调用者还会
独立计算金钱。因此若只在显示包层抑制字段，持久化奖励仍然会发生；若只抑制经验，
金钱或掉落仍会被刷取。

## 修复契约

新增 MySQL 表 `account_role_monster_reward_cooldowns`，主键为
`(account_id, role_id)`，并外键关联 `account_roles`。领取前在一个事务中：

1. 由 MySQL `CURRENT_TIMESTAMP(3)` 取得毫秒时间，绝不采信客户端、模拟器或进程启动
   时间；
2. `SELECT ... FOR UPDATE` 锁住该角色冷却行；
3. 上次成功领取距今少于 `8000ms`（或数据库时钟回拨）时提交读取事务并返回
   `blocked`；否则写入当前时间后提交领取。

领取成功才允许 `vm_net_mock_battle_grant_reward_once()` 计算经验、执行掉落、推进任务
材料，并允许两个终局调用者追加金钱。冷却命中或领取存储失败时，函数会把当前战斗
session 缓存为“已结算、零奖励”，因此随后的重复结算包也不能在同一战斗内补发奖励。

冷却不影响战斗动作、胜负、`4/7`/`4/11`/`4/9` 终局链路、HP/MP 结算、装备耐久或自动
恢复；它只影响怪物胜利奖励。组队中每个成员按自己的持久化角色身份独立领取。

若奖励领取成功后，既有的后续背包或角色保存发生存储故障，角色可能在本冷却窗口内
无法重复领取。这是现有“掉落写入与角色结算分开事务”的失败语义，不能用二次发奖
掩盖；服务端记录 `storage-denied` 取证日志，后续可在不改变客户端协议的前提下再把
完整战斗奖励聚合到单一角色事务。

## 修改点与验证

- `src/server/mock_server_role.c`：创建并事务性领取冷却行，MySQL 是时间与并发裁决者。
- `src/server/mock_server_equipment_npc.c`：在经验/掉落/任务推进之前领取冷却，并在未领取
  时保留零增量终局语义。
- `src/server/mock_server_battle.c` 与 `src/server/mock_server_equipment_npc.c` 的两个终局
  调用者：只有本次领取成功才追加金钱。
- `scripts/monster-reward-cooldown-regression.php`：在隔离服务上验证首次战斗
  `+11 EXP/+13 金钱/+1 回春散`，断线换 client id 的立即第二场不变，回拨测试行时间后
  第三场再次获得同样奖励；每次均要求响应保留 parser 所需的 `4/7.exp`。

2026-07-27 实测：`php scripts/monster-reward-cooldown-regression.php run 19154` 输出
`first=reward reconnect=blocked expiry=reward`；测试后确认冷却行已写入，再由清理步骤
通过外键级联移除夹具。

运行时可观察证据为：

```text
mock_battle_reward_cooldown action=blocked ... remaining_ms=... session=...
mock_battle_settle ... reward_claimed=0 cooldown_remaining_ms=...
```

## 2026-07-28 勘误：零增量 `4/7` 不是有效的无奖励结算

### 原始失败与首次偏离

挂机在怪物被击杀后的 scene poll 收到 `resp=289` 后闪退：

```text
PC=0x05033ed8 LR=0x05034691
r1=0x01054252 r4=0x01054252 r5=0x0105493c
```

模块基址为 `0x0502cc80`，因此 PC 对应
`mmBattleMstarWqvga.cbm:DrawBattleHpBar(0x7228)+0x30`，LR 对应
`DrawBattleCharacter(0x7A0C)` 调用点。与
`docs/re/2026-07-24-team-battle-terminal-peer-crash.md` 的零奖励终局崩溃是同一
渲染契约，而不是挂机入口、场景节点或异步轮询本身的问题。

`HandleBattleSettleMsg(0x743C)` 先把 `4/7.exp` 和 `4/7.gold` 与客户端旧总值相减，
结果面板把两项差值存到 `r9+0x3D6C+8/+12`。`DrawBattleHpBar(0x7228)` 只有这两项
至少一项为正时才短路安全；两项均为 0 时会把传入的文本临时缓冲区地址左移 6 位并
作为单位行索引，最终访问 `0x4255de5a`。因此：

```text
cooldown blocked
-> 4/7 { result=1, exp=old_total, gold=old_total }
-> result panel zero delta
-> DrawBattleHpBar invalid unit-row access
```

此前“保留 4/7、只让奖励增量为零”的结论错误地把 parser 完整性等同于可渲染性；
本次静态调用链和原始寄存器共同固定了第一次违约在 `4/7` 的零变化成功结果，而非
崩溃 PC。

### 正确终结契约

切磋是已经验证的原生无奖励终局：最后一个 `4/6` 死亡动作播放完毕后，服务端只下发
`4/11 { result=1,type=0 } + 4/9 { result=1 }`，不发送 `4/7`。`4/11` 关闭自动状态，
`4/9` 进入 Battle.cbm 的退出分支；该路径不建立胜利结果面板，自然不存在零差值行。

冷却命中时服务端现在沿用同一终结族：

1. 最后一击仍只通过既有 `4/6` 动作播放，权威角色状态照常保存，且不增加经验、金钱、
   掉落或任务材料；
2. `vm_net_mock_append_battle_status7_object()` 在消费自动壶、MP 自动恢复或写出对象前，
   先计算本次可显示的 EXP/金钱差值；两项为 0 时记录该 battle-session 为
   `no_reward_terminal`，不构造 `4/7`；
3. 同一会话下一次 scene poll 只发送已验证的 `4/11 + 4/9` 并清除该标记。

自动壶和自动 MP 恢复目前只有 `4/7.hp/mp` 与紧随其后的 `7/11` 才有已验证的客户端
承载。无奖励终局不能伪造正 EXP/金钱来重新启用该对象，也不能猜测一个场景属性包来
替代它；故冷却命中时明确不消耗壶、也不应用自动 MP 恢复，以保持服务端持久状态与
客户端战斗条一致。待找到客户端原生的战后 HP/MP 独立同步包后，再单独补齐该功能。

### 回归要求

- 同一角色首次击杀后 8 秒内再用挂机击杀：首包保留 `4/6`，下一 poll 为 `4/11+4/9`，
  无 `4/7`、无闪退、无经验/金钱/掉落/任务进度；
- 8 秒窗口到期后再次击杀：仍为 `4/6 + 4/7` 的正常奖励结算；
- 双客户端、断线重连及组队终局：冷却继续按 `(account_id, role_id)` 隔离，且任一零
  增量成功终局都不得进入 `DrawBattleHpBar(0x7228)` 的错误路径；
- 冷却命中且拥有逍遥壶/神仙壶：壶数量、HP、MP 均不变；窗口后正常获奖终局仍按既有
  `4/7 + 7/11` 自动恢复契约执行。
