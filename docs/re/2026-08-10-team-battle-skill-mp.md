# 组队战斗法术后 MP 归零

## 触发与首个偏离

复现条件：两名同队角色进入同一场怪物战斗，由其中任一角色使用需要
法力的法术。服务端在 `bin/server_out.txt` 中已经正确计算并保存法力：

```text
mock_battle_operate ... skill=1 ... mpcost=35 ... mp=890/855
team_battle_state ... rolemp=855/925
team_hsp_deliver ... mp=855/925
```

因此 MP 被清零不是角色持久化、组队状态 `5/11` 或场景 HSP 的问题。首个
错误契约发生在服务端把同一法术动作汇总为最终 `1/4/6` 时：原始动作包含
`teaminfo`，但回合捕获和汇总器用
`vm_net_mock_get_object_blob_field()` 读取它。该读取器只适用于有嵌套长度的
`vm_net_mock_put_object_blob()` 字段；而战斗 `teaminfo` 是
`vm_net_mock_put_object_raw()` 的直接 14-byte 行。因此检查失败，
`includesTeamInfo` 变为 false，最终 `4/6` 漏掉 `teaminfo`。

## 客户端契约

按 `binary_name=mmBattleMstarWqvga.cbm` 选择 IDA 实例取得的
`InitActionSlot_B(0x6DBC)` 证据，以及
`docs/re/2026-06-25-battle-server-flow.md` 的已有运行时验证：

- `4/6.teaminfo` 每个队员行必须是 `00 04, role_id:u32, hp:u32, mp:u32`；
- 客户端按当前队伍人数逐行读取，按 role wire id 定位战斗单位，并把第三个
  数值写入 MP 缓存；
- 法术 type-1 动画播放结束时会从此缓存恢复施法者 MP。若字段缺失，缓存保留
  初始化的 `0`，所以 UI 显示 MP 被清空。

这也解释了为何服务器日志和最终场景 HSP 都仍是正确 MP，而战斗中先显示为零。

## 修改

新增专用原始字段读取器
`vm_net_mock_get_battle_teaminfo_raw_field()`，它只接受 `1/4/6` 内按 14
字节对齐的 `teaminfo`。组队回合的以下三个消费者改用该读取器：

1. 先行动队员的回合动作捕获；
2. 最后一名队员行动时的回合汇总；
3. 对其他观察端转发动作时的 observer-specific wire-id 重写。

所以最终汇总 `4/6` 会按客户端当前队伍人数包含完整、观察端正确映射的 MP
行；修复位于产生该协议字段的服务端层，不修改客户端状态或 UI。

## 验证

构建后应复测：队员 A 施放耗蓝法术、队员 B 完成同一回合；检查
`team_battle_round_capture ... teaminfo=1` 与
`team_battle_round_release ... teaminfo=<14 * 队伍人数>`，并确认法术动画后
施法者 MP 从原值仅减少该技能成本，双方 `5/11` 与场景顶部值一致。还应交换
施法者和重复进入组队战斗，确认观察端 wire-id 映射不泄漏。
