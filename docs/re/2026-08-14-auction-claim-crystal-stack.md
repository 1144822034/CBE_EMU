# 拍卖领取玄晶：待领×N 入包只见 1

日期：2026-08-14

## 触发与现象

拍卖行买入后「领取」待领行显示玄晶 `x10`（或十连同级），打开背包同级玄晶数量为 **1**。

## 业务链路

1. 上架：`42/4` 扣背包 `count` → `server_auction_listings.item_count`
2. 成交：赢家 `server_auction_claims` 写入同 `item_id/item_count`
3. 领取：`42/7` 占位 `claimed=1` → `vm_mock_auction_return_item_to_bag` → `add_backpack` 按 `itemId` 堆叠 → 存盘
4. Pixi：`auctionClaim` → `handleBodies` → 再 `openBag`（`1/7/23`→`17/1`+`30/21`）

玄晶 901–916 不在 `equip.dsh`，走普通堆叠；壶 802/803 另走储量路径（2026-08-12）。

## 首次偏离（相对「应有 N 个」）

1. **客户端**：`mergeBag17WithGridSeq` 无条件采信 `30/21` 的 u32 count。当网格包截断/错位把某行读成 `count=1` 时，会盖掉 `17/1` common-extra 里正确的 stackHint=N（与仓库 SET 覆盖、十连 tip/背包分裂同类）。
2. **服务端**：领取成功只置 `backpackListResyncPending=true` 却不设 phase/removeSeq，poll 同步无效；背包权威完全依赖另一次 `openBag`。

## 根因

- 可堆叠物在 `17/1` 与 `30/21` 数量不一致时，旧合并策略「网格优先」会把 N 压成 1。
- 领取回包未附带权威背包列表，放大了上述展示风险。

## 修改

| 位置 | 内容 |
| --- | --- |
| `protocolExtras.ts` | 可堆叠：`count = max(17/1, 30/21)`；装备/壶仍跟网格 |
| `mock_server_auction.c` | `auction_return_item` 打 `bag_before/bag_after`；领取同包附 `17/1+30/21(+7/11)`；去掉半武装 resync |

## 2026-09-22 梦境徽章×N 领取后背包×1

同类症状：`item_id=840` 可堆叠（非装备，stack=99）。服务端领取走
`return_item_to_bag(item_count)`，不应砍成 1。

客户端在 `auctionClaim` 成功后又 `openBag()`，用第二次 7/23 整表替换领取包
已带的 17/1+30/21，堆叠行可能被显示成 ×1。

修改：`PixiJS_web/src/main.ts` 领取成功后不再 `openBag`，只刷新拍卖 UI（领取
回包已含背包权威列表）。

核对服务端（若 log/`item_count` 已是 10 则为纯展示问题）：

```sql
SELECT claim_id,item_id,item_count,claimed FROM server_auction_claims
WHERE item_id=840 ORDER BY claim_id DESC LIMIT 20;
```

日志：`auction_return_item ... item=840 grant=10 bag_after=`
