# Pixi 比武擂台

Date: 2026-08-10

Status: implemented (PixiJS_web only; CBE 不做)

## 规则摘要

- 地图：`c04比武擂台`（资源 `web/fs/JHOnlineData/c04比武擂台.sce`）
- 默认日程（后台可改）：周三/周六；进场 `20:20–20:29`；`20:29` 后禁入；比赛 `20:30–21:00`
- 入口：仅场景 NPC（显示名含配置关键字，默认「比武」）
- 匹配：职业 × 等级段 `10/20/30/40/50`；等级 **70** 仅同职业同等级互见/互打
- 计分：胜 +20、负 0、轮空 +20；对局超时 4 分钟平局双方 +10
- 场次：5 分钟一轮；HUD 推送场次/倒计时/积分
- 结算：每场活动独立排行；前 1–3 名冠军/亚军/季军（同职业同段覆盖）；1–10 可领后台配置奖励
赏金擂台：见下文专节。

## 协议（Pixi WT kind=41）

| subtype | 方向 | 用途 |
|--------|------|------|
| 1 | req/resp | status |
| 2 | req/resp | rank（job/bracket/page） |
| 3 | req/resp | claim |
| 4 | req | leave → 30/1 回原场景 |
| 5 | push | HUD（挂在 scene-sync poll） |

NPC 服务 opcode：`0xf6000001` 进场 / `0xf6000002` 离场 / `0xf6000003` 排行提示。

## 服务端落点

- [`src/server/mock_server_arena.c`](../../src/server/mock_server_arena.c)
- 视野过滤：[`mock_server_guild.c`](../../src/server/mock_server_guild.c) `build_scene_role_seeds`
- duel `arenaMode`：[`mock_server_equipment_npc.c`](../../src/server/mock_server_equipment_npc.c) / [`mock_server_battle.c`](../../src/server/mock_server_battle.c)
- tick/HUD：[`mock_server_social.c`](../../src/server/mock_server_social.c) scene-sync poll
- dispatch：[`mock_server_dispatch.c`](../../src/server/mock_server_dispatch.c) `builtin-arena41`
- 管理后台：`?tab=arena`（[`web_admin_arena.inc.c`](../../src/web_admin_arena.inc.c)）

## 表

见 [`server/mysql/migrate_add_arena.sql`](../../server/mysql/migrate_add_arena.sql) / `schema.sql`：

`server_arena_config` / `rewards` / `sessions` / `scores` / `titles` / `wager_challenges`（预留）

## Pixi

- [`protocolArena.ts`](../../PixiJS_web/src/net/protocolArena.ts)
- [`ArenaHud.ts`](../../PixiJS_web/src/ui/ArenaHud.ts) / [`ArenaPanel.ts`](../../PixiJS_web/src/ui/ArenaPanel.ts)
- `GameSession`：`fetchArenaStatus/Rank/claim/leave`；擂台内禁手动切磋

## 配置 NPC

在管理后台「游戏内容」给某 NPC 起名包含「比武」（或改关键字），玩家对话会出现进场/离场/排行选项。

## 验证清单

- [ ] 非活动日/窗外：进场拒绝
- [ ] `20:20–20:28` 可进；`20:29` 起禁入
- [ ] 不同职业或不同段位互相不可见
- [ ] 70 级仅同职业 70 可见
- [ ] 奇数人轮空 +20
- [ ] 4 分钟未分胜负平局 +10 双方退战场
- [ ] 21:00 结算称号覆盖；1–10 领奖幂等
- [ ] 城市场景切磋不受影响；擂台内手动切磋拒绝
- [ ] `make -j2`；Pixi `tsc --noEmit`

## 调试建议

临时把 `server_arena_config` 的 `weekday_mask` 设为当天 bit、进场/比赛分钟改为当前时刻附近，便于联调。

## 赏金擂台（押金对决）

Status: implemented (PixiJS_web)

### 规则

- 不区分职业/等级；任意场景 NPC（`npc_name_keyword`）在 `wager_enabled=1` 时提供「赏金擂台」。
- 单人或组队（复用 `vm_mock_service_team`，每边最多 3）；双方人数必须相等。
- 每人同额铜币押金；挑战方压金后擂主方**全员确认**才开战。
- 战斗：1v1 直接 duel；2v2/3v3 按座位序逐对 duel，胜场多的一方获胜；平局全额退押。
- 胜方均分奖池（`stake*(hostN+challN)` 扣 `wager_fee_percent`）。

### 协议（WT kind=41 追加）

| subtype | 用途 |
|--------|------|
| 10 | 列表 |
| 11 | 摆擂 |
| 12 | 撤擂 |
| 13 | 挑战 |
| 14 | 擂主确认/拒绝 |
| 15 | push 事件 |
| 16 | 我的赏金状态 |

NPC opcode：`0xf6000004` 打开赏金面板。

实现：[`mock_server_arena_wager.inc.c`](../../src/server/mock_server_arena_wager.inc.c)、[`protocolArena.ts`](../../PixiJS_web/src/net/protocolArena.ts)、[`WagerPanel.ts`](../../PixiJS_web/src/ui/WagerPanel.ts)。

### 表

- `server_arena_wager_challenges`（含 `host_count` / `challenger_count`）
- `server_arena_wager_members`

### 复测

- [ ] 关开关拒绝
- [ ] 余额不足整队回滚
- [ ] 1v1 确认后开战与分账
- [ ] 人数不等拒绝
- [ ] 2v2 逐对与多数胜
- [ ] 擂主拒绝退挑战方押金
- [ ] 撤擂退主机押金
- [ ] 与定时比武积分互不污染
