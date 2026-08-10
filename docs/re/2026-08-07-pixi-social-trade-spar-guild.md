# 2026-08-07 Pixi 社交扩展：交易 / 切磋 / 入帮

phase: pixi-social-trade-spar-guild
status: implemented

## Scope

在周围玩家操作菜单补齐：

| 操作 | 请求 | 行为 |
|------|------|------|
| 交易 | `1/21/1 {id}` | 对端 poll `21/2` → 同意 `21/3` → 双方 `21/4` → **完整 TradePanel**（`21/5…21/8`）见 `2026-08-07-pixi-trade-stall-guild-create.md` |
| 切磋 | `1/4/14 {id}` | **无需对端同意**：对方空闲则立刻开战 |
| 入帮 | `1/10/13 {id,fid}` | 本机 tip「已发送」；服务端多为 empty-WT ack |

## 切磋契约变更（服务端）

旧：`4/14` 仅 ack，再 enqueue `SPAR_INVITE` → 对端 `4/15` → `4/16` 同意后开战。

新（产品规则 2026-08-07）：

1. 目标同图、在线、`sceneVisibleReady`、双方非 duel/trade busy → `vm_mock_service_duel_begin`
2. 邀请方同包：`4/14 result=1` + 内联 `4/10` start
3. 对端：`startPendingMask`，下一轮 scene-sync poll 取 start
4. 忙碌 / 找不到 → `result=3` / `4`（无邀请弹窗）

证据位：`mock_server_guild.c` `vm_net_mock_build_nearby_spar_request_response`；`JianghuOL.CBE:0x0102E624`。

## Client map

| 层 | 改动 |
|----|------|
| `protocolSocial.ts` | `buildSpar/Trade*/GuildInvite`；`parseTradePackets` / `parseSparAck` |
| `GameSession` | `sparChallenge` / `tradeInvite|Reply|Cancel` / `guildInvite`；`inTrade` |
| `SocialPanel` | 操作：交易 / 切磋 / 入帮 |
| `main.ts` | dialog 242/243 交易答复、244 取消；切磋开战左方用 wire job/sex 换皮 |

## Invite softkeys

- 交易邀请 `21/2` → dialog 242/243 → `tradeReply`
- 切磋 **不**弹邀请；仅 tip `spar_result`
- 入帮本地 `guild_sent` tip

## Validation

- `make -j2`（含 spar 直开战）
- `npx tsc --noEmit`（PixiJS_web）
- `npx tsx scripts/smoke-protocol.ts`
- 双端：A 对空闲 B 切磋 → 双方进战；B 忙碌 → tip「对方忙碌」
- A 邀请交易 → B 同意 → 双方见交易 tip；取消 `21/7`
- 入帮菜单点选后本机 tip

## Negative

- 不要恢复切磋 `4/15` 同意链当作产品路径
- 不要用假默认职业/坐标掩盖未确认的入帮对端包
- 交易完整摆摊见 `2026-08-07-pixi-trade-stall-guild-create.md`；本文件早期「最小 UI」描述已过时。
- 内联 `4/10` 的 WT objectCount 必须是 `1+startCount`，禁止 `out[4]+startCount`（回收缓冲残留会丢掉开战对象，见 `2026-08-07-spar-inviter-missing-battle-start.md`）。
