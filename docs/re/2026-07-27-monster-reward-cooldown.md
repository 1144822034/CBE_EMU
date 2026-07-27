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

因此，八秒内再次击杀不能靠省略、拒绝或改写 `4/7` 完成；那会使客户端停留在
结算等待态。正确行为是照常结束战斗并下发原有终局对象，只使该次奖励增量为零。

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
