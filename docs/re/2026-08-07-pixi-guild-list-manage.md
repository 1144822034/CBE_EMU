# 2026-08-07 Pixi：帮派列表 / 管理 / 审批

phase: pixi-guild-list-manage-approval
status: implemented-client

## Scope

菜单「帮派」打开 `GuildPanel`，按是否入帮分支：

| 未入帮 | 已入帮 |
|--------|--------|
| 其他帮派 `10/21` | 本帮情报 `10/23` |
| 申请入帮 `10/31` | 成员 `10/37→10/20` |
| 创建帮派（既有三阶段） | 招收审批 `10/32` + `10/33` |
| | 脱离 `10/22 {id=roleId}` |
| | 其他帮派（只看详情） |

另：poll `10/36` 入帮/拒绝通知更新本机 `guildId/name`。

## Protocol map

| 文件 | 内容 |
|------|------|
| `protocolGuild.ts` | list/detail/leave/members/apply/apps/review builders + parsers |
| `GameSession` | `openGuild*` / `guildApply` / `guildLeave` / `guildReview` / `ensureGuildId` |
| `GuildPanel.ts` | hub / list / detail / members / apps |
| `MenuScreen` | 「帮派」 |

证据与服务端契约：`docs/re/2026-07-17-guild-list-and-role-display.md`。

## Notes

- `1/1/21` 只有帮派名、无 guildId；已入帮但 id 未知时用 `ensureGuildId()` 静默拉 `10/21.fid`。
- 详情请求 `10/22.id=guildId` → 响应 `10/23`；脱离同 subtype 请求 `id=roleId` → 响应 `10/22 {result}`。
- 位阶变更 / 公告编辑未接（`10/35`/`10/38`/`10/39`/`10/41`）。
- 踢人 `10/40`、解散 `10/22+force`：见 `docs/re/2026-08-29-pixi-guild-kick-disband.md`。

## Validation

- `npx tsc --noEmit`
- `npx tsx scripts/smoke-protocol.ts`
- 未入帮：其他帮派 → 详情 → 申请 tip
- 帮主：招收列表 → 批准/拒绝；申请人 poll 见 `10/36`
- 脱离：确认后清 `guildId`，人物属性帮派名刷新

## Negative

- 勿把 `10/22` 脱离包解析成详情
- 勿在无权限时伪造审批成功
- 招收 `roleinfo` 为 `put_object_entry`（无 blob 外层 BE16）
