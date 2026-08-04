# 组队：角色 456 收不到邀请 / 邀请名后乱码

日期：2026-08-04

## 现象

1. 角色 `456`（lxh003 / 阿狸）收不到组队邀请。
2. 收到邀请时，名字后面带乱码。

## 运行时证据（本机 `jh-online-server --debug-log` 控制台）

同一次操作序列（角色 `1`=蔡文姬 发起）：

```text
mock_group_type1_shop_return_solo_roster observer=b4b2113d team_members=1
mock_team_invite actor=456 scope=nearby source=.../1 target=.../456 queued=0 reason=target-in-team
mock_team_invite actor=455 scope=nearby source=.../1 target=.../455 queued=1 reason=queued
social_notice_deliver observer=... action=team-invite source=.../1 name=<蔡文姬 GBK>
scene_sync_poll ... social=team-invite objects=2 resp=335
team_create leader=.../1
team_add leader=... member=.../455 count=2
mock_team_invite actor=456 ... queued=0 reason=target-in-team   # 再次失败
```

对照：

| 目标 | queued | reason | 后续 |
|------|--------|--------|------|
| 456 | 0 | `target-in-team` | 未入队、无 `5/2` |
| 455 | 1 | `queued` | 投递 `5/2`+`1/1/14`，同意入队成功 |

## 根因：456 收不到邀请

### 首个偏离

`vm_net_mock_build_nearby_team_invite_response` 在入队前检查：

```c
else if (vm_mock_service_team_find_for_client(target->clientId) != NULL)
    reason = "target-in-team";
```

只要目标挂在**任意** `active` 队伍槽（含 `memberCount==1` 的孤儿单人队），就直接拒绝，**不** enqueue `TEAM_INVITE`，发起方只收到空 WT ack。

### 被违反的契约

客户端「已在队」应对齐多人队伍（`memberCount >= 2`）。服务端在队友离队后若只剩队长，当前 `team_remove_member` 只清战斗态、**不解散**队伍，留下 `memberCount=1` 的活跃槽。出商城路径 `shop_return_solo_roster ... team_members=1` 也证实 456 当时正挂在这种单人队上，并仍在给自己排队 `team-hsp`。

### 证据链

1. 邀请 456：`queued=0 reason=target-in-team`（重复多次）。
2. 同窗邀请 455：正常 `queued=1` → deliver → accept。
3. 456：`shop_return_solo_roster team_members=1` + `team-hsp` 自指。

## 根因：邀请名后乱码

既有记录见 `docs/re/2026-07-30-team-invite-sync-leave.md`：

- `5/2` 把邀请方 id/name 写入与好友邀请共用的 HUD 槽；确认框/名牌可读成乱码或错名。
- 修复：投递 `5/2` 后追加本机 `1/1/14`（与好友 `10/4` 同契约）；名字按 GBK 边界拷入。

本次 455 的投递已是 `objects=2` / `resp=335`（`5/2`+`1/1/14`），DB 名 `蔡文姬`（GBK `b2cccec4bca7`）本身完整。若客户端仍见「名字后面多余乱码」，更像确认框在读共用槽时带上了槽内尾部残留，而不是 `name` 字段被截断；需对照截图/包内 `name` 长度再钉是否还有第二偏离。

## 计划修改

1. 离队后若 `memberCount < 2`：解散该队（与「不足两人不成队」一致），避免孤儿单人队。
2. 邀请门控：仅当目标队伍 `memberCount >= 2` 时记 `target-in-team`；若目标挂着单人槽则当场清掉再入队。
3. 同意入队：接受方若只有单人孤儿队则先清；`create` 后 `add` 失败也清邀请方单人槽。
4. 不改邀请乱码路径（本次已有 `1/1/14`）；若复测仍见尾部乱码再开 IDA 确认框拼串。

## 已改文件

- `src/server/mock_server_equipment_npc.c`：`team_remove_member` 在人数 < 2 时 `team_disband`
- `src/server/mock_server_guild.c`：邀请门控 + 同意入队清孤儿单人队

## 验证

1. 构造/保留 `memberCount=1` 的目标 → 邀请应 `queued=1`（或日志 `invite-clear-solo-orphan`），目标收到 `5/2`。
2. 两人队中一人离队 → 日志 `team_disband ... trigger=roster-below-two`，原队长可被邀请。
3. 正常两人邀请/同意路径不变。
4. `make -j2 server`（需先停掉占用中的 `jh-online-server.exe` 再链）。

`obj/server/main.o` 已在 2026-08-04 编过；链接因进程占用 `Permission denied`，重启服务端后再 `make server` 即可。
