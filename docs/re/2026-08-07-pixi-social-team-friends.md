# 2026-08-07 Pixi 社交：周围玩家 / 组队 / 好友 / 查看信息

phase: pixi-social-team-friends
status: implemented-client

## Scope

Pixi 客户端落地（服务端契约已存在，本轮未改 mock）：

- 周围玩家列表 `1/2/7` + 操作（玩家信息 / 查看装备 / 组队 / 加好友）
- 查看玩家信息 `10/2`、查看装备 `29/4`
- 组队全流程 `5/1 → 5/2 → 5/3 → 5/4/5/10/5/7/5/11`
- 好友 `10/1` 列表 + `10/3→10/4→10/5→10/6`
- 组队战斗右侧多人名/职业头像（不接 `4/11` 自动）

本轮扩展见 `docs/re/2026-08-07-pixi-social-trade-spar-guild.md`（交易 / 切磋直开战 / 入帮）。

## Wire fixes

| 旧 Pixi | 正确契约 |
|---------|----------|
| `5/1` 字段 `actorID` | `id` |
| `5/2` 仅 `result` 当回复 | `1/5/3 { id, result }` |

证据：`docs/re/2026-07-11-nearby-player-actions.md`。

## Client map

| 层 | 文件 |
|----|------|
| 协议 | `PixiJS_web/src/net/protocolSocial.ts`（builders/parsers） |
| 会话 | `GameSession`：`openNearbyPlayers` / `team*` / `friend*` / poll 消费 |
| UI | `SocialPanel` + 菜单「周围玩家/组队/好友」；邀请用 `DialogPanel` action 220–231 |
| 战斗 | `parseBattleStart` + `BattlePartyHud` 按槽位 job/sex |

## Invite softkeys

- 组队邀请 poll `5/2` → dialog 同意/拒绝 → `teamReply` → `1/5/3`
- 好友邀请 poll `10/4` → dialog → `friendReply` → `1/10/5`
- 同意好友不覆盖本机 `roleName`（只 tip）

## Validation

- `npx tsc --noEmit`
- `npx tsx scripts/smoke-protocol.ts`（含 team invite field、`5/10 groupinfo`）
- 双端复测清单：
  1. 同图打开周围玩家，列表有对端
  2. 玩家信息三行、查看装备行可见
  3. A 邀请 B 组队，双方花名册 ≥2；离队清表
  4. 好友往返后好友列表出现
  5. 组队开战右侧多人名/头像不同

## Negative

- 不要用 `window.prompt` 输入 actorId
- 不要把邀请方 name 写进本机名板
- 怪物 `1/2/2` vitals blob 不要当 peer timeline
