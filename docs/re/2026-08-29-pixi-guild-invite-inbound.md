# Pixi 入帮邀请对端投递（10/13 → 10/15）

## 触发条件

- 邀请方已在帮派且位阶为帮主/管理（`member_rank` 1 或 2）
- 对附近玩家或好友发起入帮邀请：`1/10/13 { id, fid }`
- 被邀请人在线，且当前无帮派、目标帮派未满员

## 契约

| 方向 | 包 | 字段 | 说明 |
|------|----|------|------|
| 邀请方 → 服 | `1/10/13` | `id` 目标角色/线 id，`fid` 帮派 id | 服回 empty-WT；`fid` 以邀请方真实帮派为准校验 |
| 服 → 被邀请人（scene-sync poll） | `1/10/13` | `id` 邀请人 roleId，`fid`，`fname` 帮名，`name` 邀请人名 | CBE `HandleGuildBusinessDispatch` subtype 13 @ `0x0103C61E` 开确认框 |
| 被邀请人 → 服 | `1/10/15` | `id` 邀请人 roleId，`result` 1 同意 / 2 拒绝 | 发送器 `0x0103C4D6` |
| 服 → 邀请方（poll） | `1/10/15` | `result`，`name` 被邀请人名 | subtype 15 tip @ `0x0103C7EC` |
| 服 → 被邀请人（同意后 poll） | `1/10/36` | 与申请通过相同 | 更新本机帮派身份 |

先前文档猜测对端通知为 `10/14`；CBE 跳转表中 subtype 14 是邀请失败 tip（人数上限/解散/已有帮派），**入站确认框是 subtype 13**（与请求同号，靠 `fname`/`name` 区分）。

## 根因

服务端 `builtin-nearby-guild-invite` 原先用 `SOCIAL_NOTICE_NONE`，只回 empty-WT，不对目标 session 排队 notice。Pixi 还把 `fid` 写死为 `1`。

## 修改点

- `mock_server_guild.c`：邀请时校验帮派/权限/目标状态并排队 `GUILD_INVITE`；处理 `10/15` 同意写 `guild_members`、拒绝只通知邀请方
- `mock_server_social.c`：poll 投递 `10/13` / `10/15`
- Pixi：`ensureGuildId` 填 `fid`；解析入站邀请与结果；弹窗 softkey 258/259 → `guildInviteReply`

## 验证

- `PixiJS_web` smoke：`10/13` 入站解析、`10/15` 回复与结果
- `make -j2`
- 人工：A 邀请 B → B 弹窗同意 → A 收到同意 tip，B 收到 `10/36` 并刷新帮派
- 2026-08-29：目标找不到时仍 empty-WT，不再回「功能暂未实现(1/10/13)」；目标解析含 nearby/friend/online-role

## 仍未知 / 风险

- subtype 14 失败 tip 尚未在邀请发送失败路径回给邀请方（当前仅日志 `reason=`）
- 跨场景好友邀请依赖 friend/online seed；目标离线则 `reason=target-offline`，邀请方仍见本地「已发送」
- 跑服进程需重启到含本改动的 `jh-online-server`，否则旧二进制仍会 unimplemented
