# 组队邀请乱码 / 入队不同步 / 掉线幽灵队员

```text
phase: team invite accept + offline leave
trigger: 接受入队邀请页乱码；同意后队长显示在队、队友显示未在队；队友掉线后仍留在队伍 HUD
request_shape: 5/2 {id,name} confirm; 1/5/3 accept; scene_sync TEAM_RESULT/TEAM_MEMBER_JOIN/TEAM_LEAVE
client_parser: net_handle_group_info 0x01011F3A (5/2/3/4/5/7); HandleGuildJoinConfirm 0x01011ED0
```

## 首个偏离

1. **邀请确认乱码**：`5/2` 与好友 `10/4` 一样把邀请方 id/name 写入 HUD 共用槽；好友路径已在投递后追加 `1/1/14` 还原本机 plate，组队邀请没有，确认框/名牌可读成乱码或错名。名字入队时若被 `snprintf` 截断在 GBK 双字节中间，也会污染 `name` 字段。
2. **入队不同步**：同意后服务端先 `team_add_member` 再构 `5/3`；构包失败时整包 `return 0`，队长仍收到 `TEAM_RESULT`+`TEAM_MEMBER_JOIN`，接受方本地 roster 未更新 → 队长在队、队友不在队。
3. **掉线幽灵**：模态邀请挂起时，`append_scene_sync_social_notice_object` 对**全部**社交通知直接 `return 0`，`TEAM_LEAVE`/`5/5` 被饿死。离队投递还优先 `live-session` wire；`mark_offline` 清空或会话复用后 live 查找会得到 0/错 id，`5/7` 对不上邀请时写入的观察端 wire（含 `0x6Axxxxxx`）。

## 根因

- 模态门控作用域过宽，挡住了非模态的队伍同步契约。
- 离队 wire 未在 enqueue 时按观察端冻结，投递时又信任可能已换身份的 live session。
- 邀请同意路径在 roster 构包失败时未回滚队伍成员，造成服务端/客户端分叉。
- 组队 `5/2` 缺少与好友邀请对等的本机 `1/1/14` 名牌还原。

## 修改

1. 社交通知：roster/结果类（`TEAM_LEAVE`/`TEAM_MEMBER_JOIN`/`TEAM_RESULT`/`TEAM_HSP` 等）可在模态挂起时优先投递；仅继续阻止叠加新的模态邀请。
2. `social_notice` 增加 `sourceWireId`；enqueue 时用 `team_member_wire_id(target, source)` 冻结；`TEAM_LEAVE` 优先用该 wire，live 查找仅在 `onlineRoleId` 仍等于入队时的 role id 时可用。
3. 名字按 GBK 字符边界拷入 `sourceName[32]`；优先 `onlineRoleName`。
4. `mock_team_invite_reply`：先构接受方 `5/3`，失败则 `team_remove` 回滚且不通知队长成功。
5. 组队 `5/2` 投递后追加 `1/1/14`，与好友 `10/4` 同契约。

## 验证

1. 两人组队邀请：确认框名字可读；同意后双方队伍 HUD/菜单均显示两人；日志 `mock_team_invite_reply ... accepted=1` 与 `team_result_deliver` / `team_member_join_deliver`。
2. 同意后一方杀进程或心跳超时：存活方日志 `team_leave_deliver ... wire_source=notice-cached-wire`，HUD 去掉离线队员。
3. 邀请确认框未关时队友掉线：仍应投递 `TEAM_LEAVE`，不因模态门控饿死。
4. `make -j2 server`。
