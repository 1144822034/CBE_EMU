# 组队战斗回合屏障：死亡成员不参与等待

## 本次复现证据

用户报告“队长一行动就进入怪物回合”。当前本地服务的原始记录在
`tmp/mock-service-team-terminal-peer-fix.stdout.log`：

```text
mock_team_battle_member_row ... member=211b455b/10024 hp=0/124 mp=223/223
team_battle_queue serial=6 ... members=2
team_battle_round_prepare battle=6 round=1 source=84267947 actor=0
    acted=00 alive=01 duplicate=0 resolve=1
team_battle_round_release ... actions=4 ...
```

因此第二名成员虽然仍在队伍与场景中，却以 HP=0 进入本场战斗；`alive=01` 只含
队长的 bit。`vm_mock_service_team_battle_alive_mask()` 明确按
`battleMemberHp[i] != 0` 建立要求行动的掩码。

## 结论

本次不是回合屏障提前释放。对仅剩一个存活成员的场景，队长一次行动即完成该回合
是正确行为；等待 HP=0 的角色会使队伍战斗永久无法推进。战斗模块的 `4/6`
动作列表一旦发出就会进入本地动作/怪物阶段，不能通过发送空列表或延迟包来模拟
等待。

## 正确复测前提

先使两名成员都处于非零 HP 状态，再触发同场景怪物战斗。此时第一名成员操作后
必须出现：

```text
team_battle_round_prepare ... alive=03 resolve=0
team_battle_round_capture ...
team_battle_round_defer ... resp=5
```

只有第二名存活成员提交操作后，才允许记录 `resolve=1`、合并并下发 `1/4/6`。
若在 `alive=03` 仍出现队长直接 `round_release`，再以该日志为新的根因调查起点。
