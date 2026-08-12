# 三人队入队后的成员同步

## 触发条件

已有两人队 `A(队长) + B` 时，`A` 邀请 `C`；`C` 接受 `1/5/3` 后，`C`
的队伍界面只显示自己和队长，遗漏 `B`。服务端的权威 `team.memberCount` 已经是 3，
所以该问题不是队伍创建或容量限制造成的。

## 首次偏离

`vm_net_mock_build_team_invite_reply_response()` 的成功路径此前只做了两类通知：

1. 向队长发送 `5/4` 成功结果与 `5/5(C)`；
2. 向既有非队长成员发送 `5/5(C)`。

接收者 `C` 的同步回复只有内联 `5/3(A)`。这符合客户端的已有状态：`C` 自己的行
来自登录 `5/10`，`5/3` 的第一行是队长行；但原队员 `B` 从未以 `5/5` 发送给 `C`。

客户端证据位于 `JianghuOL.CBE` 的
`net_handle_group_info(0x01011F3A)`：`1/5/5` 是追加/刷新一个成员行的原生分支，
而 `1/5/3` 不是可替换完整名单的快照。将完整三人名单塞回 `5/3` 会重复 `C` 的
登录自有行，不是正确契约。

## 修复

入队成功且权威队伍已加入 `C` 后，服务端额外为 `C` 排队每个“已存在且非队长”的
成员 `5/5` 通知。因此三人队的状态顺序为：

```text
C 的 5/10 自己行
  -> 1/5/3(A，队长行)
  -> 1/5/5(B，原队员增量)
```

这些通知经过正常场景同步投递；若队长在下一次场景轮询之前立即进入战斗，既有的
`team_battle_roster_preamble` 会按相同的原生 `5/5` 形式先投递它们，再下发 `4/5`
战斗开始包。

组队战斗本身已直接从 `team.memberClientIds` 收集同场景成员，并冻结到
`battleMemberClientIds`。因此三人均在同一场景时会得到 `battleMemberCount=3`，
回合完成掩码和 HP/MP 同步也使用该同一快照。

## 回归观察点

三人队第三人同意后应看到：

```text
team_add ... count=3
mock_team_invite_reply ... accepted=1 ... queued_existing_for_joiner=1 ... members=3
team_member_join_deliver observer=<C> member=<B> update=5/5
```

若紧接着由队长触发同场景战斗，应看到每个观察者的
`team_battle_deliver ... party=3`，且每回合必须等待三个存活成员都提交行动。
