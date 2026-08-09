# 场景挂机：多怪死亡动画与奖励冷却根因（已移除冷却）

## 原始触发与首次偏离

挂机自动战斗中，群体技能杀死两只或三只怪物后，客户端有时停在第一条 `type=3`
死亡动画。原始 `bin/server_out.txt` 把首次偏离固定为：上一场已经正常结算并得到
奖励，紧接着新的 `2/10(Type=2) + 25/3` 挂机请求又创建了一场战斗；第二场结束时仍在
服务端八秒奖励冷却内。

```text
mock_hangup_battle_start ... enemies=2 ... auto=1 ...
mock_battle_reward_cooldown action=blocked ... remaining_ms=1362 session=2
mock_battle_operate ... targets=2 ... deaths=2 enemyhp=0
mock_battle_terminal_close_deliver ... response=4/11+4/9
```

其中 `4/6` 的命中与死亡记录是完整的。第一个错误状态不是怪物数量、目标槽位或死亡
动作顺序，而是把已经进入胜利状态的战斗改造成客户端没有合法结算对象的“零奖励胜利”。

## 已验证的客户端契约

通过 `mmBattleMstarWqvga.cbm`（按 `binary_name` 选择实例）复核：

- `HandleServerBattleCmd(0x7BD0)` 的 `4/6` 仅消费动作队列；它不会自行离开
  BattleScreen。
- 普通胜利的唯一结果面板入口是 `4/7 -> HandleBattleSettleMsg(0x743C)`；面板退出后
  客户端原生发送空 `25/5`，`BattleScene_ExitAndCleanup(0x60C8)` 才会回到场景。
- `4/11`、`4/9` 属于自动开关、复活/控制类分支，不是胜利结算；把它们放在最终 `4/6`
  后不会生成结果面板，因此死亡动作一直被逐帧重播。
- 进一步的隔离自动化运行证明“字段齐全但经验和金钱增量均为零的 `4/7`”也不是可用
  契约：`HandleBattleSettleMsg` 随后调用 `DrawBattleHpBar(0x7228)`，在该零增量分支以
  无效地址访问崩溃（本地 PC `0x7258`，有效地址 `0x4255de5a`）。

因此，旧的两条尝试均被证伪：

1. 不发 `4/7`、用延迟 `4/11+4/9` 收尾：动画没有终结状态；
2. 下发零奖励 `4/7`：结果面板的客户端增量渲染分支崩溃。

这说明成品客户端并没有可表达“已经胜利、但经验和金钱增量均为零”的安全协议形态。
不能用补发动作、跳过动画或修改客户端状态掩盖该缺口。

## 规则调整（2026-08-09）

按当前产品规则，**完全移除战斗奖励冷却**：每场合法战斗都按正常路径结算经验、金钱和
掉落，并下发常规 `4/7`。这样每一个已创建的多怪战斗都拥有客户端可完成的
`4/6 -> 4/7 -> 25/5` 生命周期，不会再进入上述无合法终结的分支。

“连续三秒内进入战斗”改为仅审计，不影响开战、奖励、掉落、动画或自动挂机：

```text
account_role_battle_entry_state
  account_id + role_id -> last_entry_ms

account_role_rapid_battle_entry_audit
  account_id, role_id, entered_at_ms, previous_entry_ms, interval_ms,
  source, scene_name, enemy_id
```

服务端在实际创建战斗状态时，使用 MySQL `CURRENT_TIMESTAMP(3)` 和同角色行锁更新
`last_entry_ms`；与上一次进入间隔 `<= 3000ms` 时才插入审计行。审计写入失败只记录
`rapid_battle_entry_record_failed`，绝不改变该场已经合法的客户端协议响应。

历史表 `account_role_monster_reward_cooldowns` 不再被读取或写入，以免删除既有历史数据；
它不再参与任何运行时判定。

## 修改点

- `src/server/mock_server_equipment_npc.c`：奖励只由单场 battle serial 幂等保护；删除
  跨战斗冷却闸门。
- `src/server/mock_server_battle.c`：每个场景挂机/场景挑战实际开战时记录快速进入审计；
  挂机奖励面板关闭后的下一场不再等待冷却。
- `src/server/mock_server_role.c`：建立并维护快速进入状态/审计表。
- `scripts/run-shop-return-hangup-automation.ps1`：`hangup-auto-rapid-entry-v1` 在隔离
  数据库中执行两场一击挂机战斗，验证第二场仍为正常有奖励 `4/7`，并拒绝
  `4/11+4/9` 终结。结果面板动画可能使两次实际开战相隔超过三秒，因此该场景不把
  审计行是否出现作为生命周期成功条件。

## 验证边界

回归必须证明：客户端收到第二个 `4/5`，群体死亡动作后收到完整 `4/7` 并由客户端
发送 `25/5`；服务端日志需有至少两条 `reward_claimed=1` 的结算。若两次实际进入战斗
间隔不超过三秒，还必须有一条 `rapid_battle_entry_recorded`。任何 `4/11+4/9` 作为普通
怪物胜利收尾、零奖励 `4/7`、重复死亡动画或无 `25/5` 都是失败。
