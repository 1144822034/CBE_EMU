# Pixi 物品竞拍

Date: 2026-08-10

Status: implemented (PixiJS_web only; CBE 不做)

## 规则摘要

- 入口：地图左上角「修」右侧「拍」徽章；背包物品动作「拍卖」。
- 上架：选背包物品 → 设起拍价（铜币或酷宝）与时长档 → **先扣手续费并删除背包物品**，再写入拍卖行。
- 手续费：`fee = max(1, startPrice * feePercent / 100)`；`feePercent = base(2) + tier * perTier(2)`（1/6/12/24 时 → **2/4/6/8%**）。
- 竞拍：同货币出价；被超价后拍金进入 **待领退款**（不自动入账）。
- 禁止同 `roleId` 与同 `accountId` 自拍。
- 全服同时在售（`status=OPEN`）最多 **50** 件；另有每人 `max_active_listings`（默认 20）。
- 超时无出价：下架，卖家待领 **超时退物**。
- 超时有出价：下架成交；卖家待领 **拍金**，赢家待领 **物品**。
- 「拍」有待领时闪烁；领取走 claim 幂等（先占位 `claimed=1` 再发货）。

## 防刷加固（2026-08-10）

| 路径 | 契约 |
|------|------|
| claim | `UPDATE claimed=1 WHERE claimed=0` 且 `ROW_COUNT()=1` 后才 credit/发物；失败则回滚占位 |
| bid | 条件更新（匹配原 `high_bidder`/`current_price`/`OPEN`）成功后才写退款 claim；事务提交后通知 |
| settle | 事务 + `ROW_COUNT()=1` 才插卖家/赢家 claim；提交后再 notify |
| cancel | 先回背包，再 `CANCELLED`；更新失败则回滚背包 |
| self | 同账号禁止出价 |

仍开放风险：低起拍价搬家（手续费按起拍价）；未做成交抽佣。

## 协议（Pixi WT kind=42）

| subtype | 方向 | 用途 |
|--------|------|------|
| 1 | req/resp | status（enabled/blink/pendingClaims/fee0..3） |
| 2 | req/resp | 浏览列表 |
| 3 | req/resp | 我的上架 |
| 4 | req/resp | 上架（bagSeq/count/startPrice/currency/durationTier） |
| 5 | req/resp | 出价 |
| 6 | req/resp | 撤拍（仅无出价）→ 物品直接回背包 |
| 7 | req/resp | 领取待领 |
| 8 | push | 事件（挂 scene-sync poll） |

## 服务端落点

- [`src/server/mock_server_auction.c`](../../src/server/mock_server_auction.c)
- dispatch：`builtin-auction42`
- tick / event：[`mock_server_social.c`](../../src/server/mock_server_social.c) scene-sync poll

## 表

见 [`server/mysql/migrate_add_auction.sql`](../../server/mysql/migrate_add_auction.sql) / `schema.sql`：

`server_auction_config` / `listings` / `bids` / `claims`

## Pixi

- [`protocolAuction.ts`](../../PixiJS_web/src/net/protocolAuction.ts)
- [`AuctionPanel.ts`](../../PixiJS_web/src/ui/AuctionPanel.ts)：顶栏标签（浏览/我的/上架/领取）；上架页交易式流程「选择物品 → 设置金额(IME) → 确认上架」
- `SceneView`「拍」徽章 + blink；`GameSession` fetch/create/bid/cancel/claim

## 验证清单

- [ ] 上架扣费并移出背包；余额不足拒绝且物品仍在
- [ ] 手续费按时长：2/4/6/8%
- [ ] 出价成功；被超价后 blink，领取退回拍金
- [ ] 无出价超时 → 卖家领取物品
- [ ] 有出价超时 → 卖家领钱、赢家领物，列表下架
- [ ] 有出价不可撤；无出价可撤回背包
- [ ] 同账号出价被拒 `self-account`
- [ ] 全服 OPEN≥50 时上架返回 `global-limit`
- [ ] 背包满撤拍不丢物（仍 OPEN）
- [ ] claim 重复请求不双发
- [ ] `make -j2`；Pixi `tsc --noEmit`
