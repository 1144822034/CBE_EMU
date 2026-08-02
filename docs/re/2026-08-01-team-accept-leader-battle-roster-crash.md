# 组队同意后队长开战闪退（5/4 与 5/5 跨 poll）

日期：2026-08-01

## 症状

二人同场景组队：队长邀请、队员同意后立刻点怪开战，队长客户端闪退。
队员侧可收到入战排队包。

## 运行时证据（lxh001 队长 + lxh003）

```text
team_create / team_add ... count=2
social_notice_queue ... team-result
social_notice_queue ... team-member-join
team_result_deliver ... roster_update=5/4-then-queued-5/5 members=2
scene_sync_poll ... social=team-result resp=47
  ← 无 team_member_join_deliver
mock_challenge_battle_start ... enemies=3 ...
mock_team_battle_start leader=... party=2 response=233
  ← 此后队长无更多请求
```

## 根因

1. 队长登录 `5/10` 只有本机行；同意后原生路径是 `5/4`（结果）再 `5/5`（新成员增量）。
2. scene-sync **每次 poll 只投递一条** social notice；`5/5` 故意留到“下一次 poll”。
3. 队长在收到 `5/4` 后、尚未 poll 到 `5/5` 时发起 `4/1` 开战；开战包
   `battleinfo` 右侧含队员 wire id。
4. `HandleBattleStartMsg` / `sub_66A4` 查本地队伍表 miss → 空单位 →
   首绘 `0x01004EA8` 类闪退（与三人队缺中间行同契约，见
   `2026-07-25-team-3p-joiner-roster-crash.md`）。

首次偏离：队长本地 roster 在开战前未包含新成员行，而不是开战包本身字段错乱。

## 修改

`TEAM_RESULT` 投递成功（result=1）时，在同一 poll/包内立刻追加并清除匹配的
`TEAM_MEMBER_JOIN`（`5/4+5/5-same-poll`）。拒绝或源失效时丢弃对应 join，避免孤儿 `5/5`。

## 验证

1. 重启服务；二人组队，同意后日志：
   `roster_update=5/4+5/5-same-poll` 且同包有
   `team_member_join_deliver ... same_packet_as=5/4`。
2. 同意后立刻开战：双方进战斗 UI，队长不闪退。
3. 三人队回归：第三人 `5/3` rows=2；既有队员仍收增量 `5/5`。
