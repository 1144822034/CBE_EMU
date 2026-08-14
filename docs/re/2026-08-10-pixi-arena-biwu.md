# Pixi 比武擂台

Date: 2026-08-10

Status: implemented (PixiJS_web only; CBE 不做)

## 2026-08-10 优化

1. 排行榜 / 结算排名：`score==0` 不计入。
2. 切磋/比武 `4/3` 用药：纳入 duel 回合提交屏障（双方用药可推进），不再空 ack 卡死。
3. 战后 `4/7` fdata + 地图 tip：`比武胜利/失败/平局 积分+N 当前积分M`。
4. 进场/比赛期间擂台内禁组队；进场与 scene_ready 自动解散；离线再上线重绑 `clientId`/`inArenaScene`（仍留在场地）。
5. 进场/比赛期间同图仅同职业×同段位互见（含周围列表）；结算后恢复。

## 2026-08-14 修复：阶段隔离周围玩家

- 契约：ENTRY/LIVE 时擂台同图仅同 `job×bracket` 互见（地图种子 + `2/7` 周围列表）；SETTLED/IDLE 恢复。
- 根因：`vm_mock_arena_peer_visible` 要求双方都有内存 `participant` 且 `inArenaScene`。`ensure_session` 换场会 `part_count=0`，重启也不 hydrate scores → `find_part` 失败后一律不可见，或阶段已开仍表现错乱。
- 修复：有报名快照用进场 job/bracket；否则回退 `onlineJob` + `bracket_for_level(onlineLevel)`。阶段切换 / 新 session 时 invalidate 擂台内 `peerSync`，迫使 poll 重 baseline 与 `1/2/6` 清理。
- `peer_visible` 内调用 `arena_tick`，避免只开周围列表、未走 scene-sync 时 status 仍为 IDLE。

## 2026-08-14 修复：比武结束后顶栏 HUD 不消失

- 现象：SETTLED 后 Pixi 最上「比武 比赛 …」条仍停留。
- 根因：结算后服务端不再推 `41/5`，客户端只在 `apply(null)` 时隐藏，最后一帧一直显示。
- 修复：结算后仍推 `status=3` HUD；客户端仅 ENTRY/LIVE 显示，SETTLED/离场/status 非进行中时清条。

## 2026-08-14 修复：轮空 tip

- 现象：奇数人轮空有积分，但 Pixi 无明确提示（旧文案还写成「比武胜利」）。
- 修复：服务端 fdata 改为「比武轮空 积分+N 当前积分M」，经 scene-sync `25/12+1/3/3` 下发；Pixi `onChat` 对 比武轮空/胜/负/平 弹 `showGameTip`。

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
- Pixi：[`ArenaHud.ts`](../../PixiJS_web/src/ui/ArenaHud.ts) / [`ArenaPanel.ts`](../../PixiJS_web/src/ui/ArenaPanel.ts)（职业×段位双行标签 + 列表，任意玩家可切换查看）
- `GameSession`：`fetchArenaStatus/Rank/claim/leave`；擂台内禁手动切磋

## 配置 NPC

在管理后台「游戏内容」给某 NPC 起名包含「比武」（或改关键字），玩家对话会出现进场/离场/排行选项。

## 验证清单

- [ ] 非活动日/窗外：进场拒绝
- [ ] `20:20–20:28` 可进；`20:29` 起禁入
- [ ] 不同职业或不同段位互相不可见；周围列表同样过滤
- [ ] 70 级仅同职业 70 可见
- [ ] 0 分不进排行榜
- [ ] 双方同回合用药/血壶可出 4/6，不卡回合
- [ ] 战后 tip：胜/败/平 + 积分增量 + 当前积分
- [ ] 比武期间擂台内无法组队；已组队进场自动解散
- [ ] 离线再上线仍在擂台且可继续匹配
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
- 每人同额铜币押金（最低 **100金**＝1000000铜，1金=10000铜）；挑战方压金后擂主方**全员确认**才开战。
- 战斗：1v1 直接 duel；2v2/3v3 按座位序逐对 duel，胜场多的一方获胜；平局全额退押。
- 胜方均分奖池（`stake*(hostN+challN)` 扣 `wager_fee_percent`）。
- 玩家赏金**连胜 ≥3** 时世界频道全服通报（平局/败方清零连胜）。

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

### 2026-08-11 修复：摆擂后无法挑战

- 根因：`WagerPanel.softkeys()` 使用无效字段 `mid`（运行时只认 `left/center/right`），且左键只调押金不确认，软键无法应战。
- 修复：左/中软键均确认当前行；押金仅左右方向键调节；己方擂台行不再发 `challenge`；错误码中文 tip；列表/挑战场景用 `scene_names_equal_loose`，create 时 scene 回退 `role->scene`。

### 2026-08-11：列表无「可挑战」行

- 契约：`[可挑战]` 只出现在**他人** `status=OPEN` 行；己方开放擂为 `[我的擂台]`（软键「应战」仅非己方行）。
- 根因1：`wager_list` 在 `sceneVisibleScene` 为空时用 `""` 过滤，而摆擂已回退 `role->scene`。
- 根因2（本地复现）：DB 有 `challenge_id=1` 场景 `c04临安府_04`，但列表按同场景过滤；且擂台只在进程内存，重启后不 hydrate → 列表空。
- 修复：`vm_mock_wager_viewer_scene`；启动/首次协议从 DB hydrate open/await；**列表全局可见**并下发 `scene` 字段；应战仍校验同场景（tip「须先赶到对方所在场景」）。
- 根因3（本地「暂无开放擂台」）：`wager_list` 用 `put_object_blob` 给 `rows` 外包 BE16，Pixi `SeqReader` 按拍卖同款裸 seq 解析 → `u32` 首字节错位，行全丢、tip 空列表。
- 修复：`rows` 改为 `put_object_entry`（与 auction/arena rank 一致）。

### 2026-08-11：最低押金与连胜通报

- 最低押金改为 **100金**（客户端默认/步进按金展示；服务端 `stake < 1000000` 拒收）。
- 结算后胜方成员连胜 +1、败方/平局清零；连胜 ≥3 时 `[世]` 通报「恭喜{名}在赏金擂台连胜N场…」。

### 2026-08-11：列表职业等级 + 拒绝通知挑战者

- 列表 `rows` 在 `scene` 之后：先 `hostCount` 组、再 `challCount` 组，每组 `name` + `job` u32 + `level` u32；成员表增 `job`/`level`，在线列表会 refresh。
- UI：`甲(天机Lv30)/…←乙(幻剑Lv28)/…`。
- 根因：擂主拒绝时先清空 `chall[]` 再 `notify_side(CHALL, REJECT)` → 无人可通知。
- 修复：rebind → `EVT_REJECT` 通知（+ 系统消息「擂主已拒绝你的赏金挑战」）→ 再退押清空；客户端 tip「擂主拒绝」。
- 应战成功：rebind 后 `EVT_AWAIT` 通知擂主方 + 系统消息「有人应战你的赏金擂台，请打开确认」；客户端 tip「有人应战，请确认」。
- 地图左上状态条在「拍」后增加「擂」：收到赏金事件闪烁，打开赏金面板后熄灭；点按打开面板。

### 2026-08-11：重启后开放擂误入「进行中」

- 根因：拒绝后内存清空挑战方，但 `server_arena_wager_members` 的 `side=1` 未删；hydrate 重装 `challCount`，客户端用 `challCount>0` 判进行中。
- 修复：`status=OPEN` 时 DELETE side=1；hydrate 只加载 OPEN 的 host 行并 reconcile；客户端分栏仅按 `status`（0=摆擂/应战，1/2=进行中）。

### 2026-08-11：断线后无法拒绝待确认

- 根因：应战后内存把擂主 `accepted=0`，DB 仍为摆擂时的 `accepted=1`；重启/重登 hydrate 后 `needConfirm=0`，界面无开战/拒绝。
- 修复：进入 AWAIT 时 `UPDATE … accepted=0`；hydrate 若 AWAIT 却全员已 accepted 则强制清零；confirm 入口 hydrate+rebind；进行中己方待确认行直接提供开战/拒绝。

### 2026-08-11：分栏列表 + 翻页

- 客户端 `WagerPanel` 三栏（标签无计数）：**摆擂**（表单：调押金/确认摆擂或撤擂）/ **擂台** / **进行中**；←→ 切栏（摆擂未开擂时 ←→ 调押金）。
- 软键：左=刷新、中=当前操作、右=关闭（不再占用列表行）。
- 应战/进行中每页 5 条可翻页；服务端 `wager_list` 额外下发 `FIGHT` 行。

### 表

- `server_arena_wager_challenges`（含 `host_count` / `challenger_count`）
- `server_arena_wager_members`（含 `job` / `level`）

### 复测

- [ ] 关开关拒绝
- [ ] 余额不足整队回滚
- [ ] 左软键/点按可应战（非己方 open 行）
- [ ] 己方擂台行不发 challenge；提示选应战或撤擂
- [ ] 1v1 确认后开战与分账
- [ ] 列表显示擂主/挑战者职业与等级
- [ ] 擂主拒绝后挑战者收到 tip/系统消息
- [ ] 人数不等拒绝
- [ ] 2v2 逐对与多数胜
- [ ] 擂主拒绝退挑战方押金
- [ ] 撤擂退主机押金
- [ ] 与定时比武积分互不污染

### 2026-08-11：空闲 10 分钟自动撤擂

- 契约：`OPEN`（无人应战）或 `AWAIT`（无人确认/拒绝）连续空闲 **600 秒** → 自动撤擂，退还双方押金。
- 计时从摆擂 / 应战 / 拒绝回 OPEN / 确认操作起算；`FIGHT` 不超时。
- 擂主系统消息「擂主超时，已自动撤擂」；挑战方（若在 AWAIT）「对方擂台超时已撤销」；push `eventKind=5`。
- 触发：`vm_mock_arena_tick`（社交 poll，不依赖比武开关）+ 打开列表时顺带检查。
- hydrate 用 `UNIX_TIMESTAMP(updated_at)` 恢复空闲起点。
