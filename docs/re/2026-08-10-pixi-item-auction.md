# Pixi 物品竞拍

Date: 2026-08-10

Status: implemented (PixiJS_web only; CBE 不做)

## 规则摘要

- 入口：地图左上角「修」右侧「拍」徽章；背包物品动作「拍卖」。
- 上架：选背包物品 → 设起拍价 / 可选一口价（铜币或酷宝）与时长档 → **先扣手续费并删除背包物品**，再写入拍卖行。
- 一口价：`buyout_price=0` 表示无；若设置必须 `≥ startPrice`。买家可在详情直接一口价成交；出价金额 `≥ buyout` 同样立即成交。
- 手续费：`fee = max(1, startPrice * feePercent / 100)`；`feePercent = base(2) + tier * perTier(2)`（1/6/12/24 时 → **2/4/6/8%**）。
- 竞拍：同货币出价；被超价后拍金进入 **待领退款**（不自动入账）。
- 禁止同 `roleId` 与同 `accountId` 自拍。
- 全服同时在售（`status=OPEN`）最多 **50** 件；另有每人 `max_active_listings`（默认 20）。
- 超时无出价：下架，卖家待领 **超时退物**。
- 超时有出价：下架成交；卖家待领 **拍金**，赢家待领 **物品**。
- 「拍」有待领时闪烁；领取走 claim 幂等（先占位 `claimed=1` 再发货）。

## 浏览详情 / 一口价（2026-08-10）

- 浏览/我的列表「确定」进入物品详情（装备走 `buildEquipDetailLines`，道具显示说明）。
- 详情操作：出价 / 一口价（有则显示）/ 撤拍（我的且无出价）/ 返回。
- 一口价成交：条件更新 `OPEN→SOLD`，退还前高价方，卖家待领拍金、买家待领物品；已是最高出价者补差额即可。

## 防刷加固（2026-08-10）

| 路径 | 契约 |
|------|------|
| claim | `UPDATE claimed=1 WHERE claimed=0` 且 `ROW_COUNT()=1` 后才 credit/发物；失败则回滚占位 |
| bid | 条件更新（匹配原 `high_bidder`/`current_price`/`OPEN`）成功后才写退款 claim；事务提交后通知 |
| buyout | 条件更新（匹配 `buyout`/`high`/`price`/`OPEN`）成功后才退款+成交 claim |
| settle | 事务 + `ROW_COUNT()=1` 才插卖家/赢家 claim；提交后再 notify |
| cancel | 先回背包，再 `CANCELLED`；更新失败则回滚背包 |
| self | 同账号禁止出价/一口价 |

仍开放风险：低起拍价搬家（手续费按起拍价）；未做成交抽佣。

## 协议（Pixi WT kind=42）

| subtype | 方向 | 用途 |
|--------|------|------|
| 1 | req/resp | status（enabled/blink/pendingClaims/fee0..3） |
| 2 | req/resp | 浏览列表（rows 含 buyoutPrice；pageIndex/hasMore） |
| 3 | req/resp | 我的参与：OPEN 且（卖家=自己 或 当前最高出价=自己） |
| 4 | req/resp | 上架（…startPrice/buyoutPrice/currency/durationTier） |
| 5 | req/resp | 出价（≥一口价时走 buyout 成交） |
| 6 | req/resp | 撤拍（仅无出价）→ 物品直接回背包 |
| 7 | req/resp | 领取待领（写 claimed=1 后发货） |
| 8 | push | 事件（挂 scene-sync poll） |
| 9 | req/resp | 一口价购买 |
| 10 | req/resp | 待领列表预览（不发货；pageIndex/hasMore/pendingClaims） |

### 我的 / 领取分页（2026-08-10）

- 「我的」：在售上架 + 当前最高出价中的拍品，标签 `卖` / `竞·领先`；仅卖家无出价可撤。
- 「领取」：subtype=10 拉待领行（退物/拍金/退价/得物），列表底「一键领取」走 subtype=7；浏览/我的/领取均 `pageSize=12`。
- Pixi：`AuctionPanel` 三页独立页码；`fetchAuctionBoard` 并行拉 status/list/mine/claims-list。

## 服务端落点

- [`src/server/mock_server_auction.c`](../../src/server/mock_server_auction.c)
- dispatch：`builtin-auction42`
- tick / event：[`mock_server_social.c`](../../src/server/mock_server_social.c) scene-sync poll

## 表

见 [`server/mysql/migrate_add_auction.sql`](../../server/mysql/migrate_add_auction.sql)：

`server_auction_config` / `listings`（含 `buyout_price`） / `bids` / `claims`

运行时 `ensure_tables` 会对旧库 `ALTER … ADD buyout_price`。

## Pixi

- [`protocolAuction.ts`](../../PixiJS_web/src/net/protocolAuction.ts)
- [`AuctionPanel.ts`](../../PixiJS_web/src/ui/AuctionPanel.ts)：浏览详情；我的参与列表；领取待领列表；上架「选物→起拍→一口价→确认」
- `SceneView`「拍」徽章 + blink；`GameSession` fetch/create/bid/buyout/cancel/claim/claims-list

## 验证清单

- [ ] 上架扣费并移出背包；余额不足拒绝且物品仍在
- [ ] 手续费按时长：2/4/6/8%
- [ ] 出价成功；被超价后 blink，领取退回拍金
- [ ] 浏览确定可查看物品属性/说明，并可出价
- [ ] 「我的」显示自己上架与当前最高出价；分页翻页
- [ ] 「领取」显示待领行（退物/拍金/退价/得物）；分页；一键领取
- [ ] 设一口价上架；买家一口价立即成交，双方待领
- [ ] 出价≥一口价自动成交；已是最高价补差一口价
- [ ] 无出价超时 → 卖家领取物品
- [ ] 有出价超时 → 卖家领钱、赢家领物，列表下架
- [ ] 有出价不可撤；无出价可撤回背包
- [ ] 同账号出价被拒 `self-account`
- [ ] 全服 OPEN≥50 时上架返回 `global-limit`
- [ ] 背包满撤拍不丢物（仍 OPEN）
- [ ] claim 重复请求不双发
- [ ] `make -j2`；Pixi `tsc --noEmit`
