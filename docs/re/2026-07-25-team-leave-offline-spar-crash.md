# 组队断线后切磋 / 找人闪退

```text
phase: party online -> leader heartbeat-timeout -> teammate nearby spar
trigger: 组队中队长掉线（heartbeat-timeout），队员对附近玩家发 4/14 切磋
request_shape: social_notice TEAM_LEAVE queued; later scene_sync_poll should emit 5/7 {id}
observed: team_disband + session_offline(leader); scene_sync_poll delta objects=1 social=unknown;
          无 social_notice_deliver/team_leave；随后队员 explicit-disconnect
client_parser: JianghuOL.CBE group subtype 5/7 (0x01011F3A); leader id clears full roster
```

## 首个偏离

```text
social_notice_queue target=<member> action=team-leave source=<leader>
team_disband leader=... reason=heartbeat-timeout
session_offline ... reason=heartbeat-timeout   # clears onlineRoleId
scene_sync_poll delta ... social=unknown       # 无 5/7 投递
mock_spar_request ... queued=1
session_offline ... reason=explicit-disconnect # 队员闪退
```

`mark_offline` 注释已要求：先 `team_remove` 入队 `5/7`，再清 `onlineRoleId`。
入队时 notice 已冻结正确 `sourceRoleId`，但投递仍优先
`team_member_wire_id(live session)`。断线后 session 还在、`onlineRoleId==0`，
wire 变成 0，notice 被静默丢弃。队员客户端收不到队长离队的 `5/7`，组队 HUD
残留幽灵队长；再切队友/附近切磋即可闪退。

## 根因

`TEAM_LEAVE` 投递在 live session 仍存在但身份已清空时，没有回退到 enqueue 时
写入 notice 的 wire/role id。

## 修改

- `vm_net_mock_append_scene_sync_social_notice_object` 的 `TEAM_LEAVE`：
  live wire 可用则用；否则用 notice 缓存 id（含同 roleId 冲突的 `0x6Axxxxxx`）。
- 增加 `team_leave_deliver` / `team_leave_drop` 日志。

## 验证

1. 两人组队；队长杀进程或停心跳至 `heartbeat-timeout`。
2. 队员日志应出现 `team_leave_deliver ... response=5/7 wire_source=notice-cached`。
3. 队员组队 HUD 清空后，对第三人 `4/14` 切磋不再因幽灵队伍闪退。
4. `make -j2`。
