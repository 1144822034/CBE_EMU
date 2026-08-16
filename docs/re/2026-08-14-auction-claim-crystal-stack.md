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

## 验证

1. 待领 `十二级玄晶x10` → 领取 → 背包 `X10`；日志 `auction_return_item ... grant=10 bag_after=` 升到 ≥10。
2. 已有同级×1 再领×10 → 背包 11。
3. 撤拍退回堆叠物数量正确。
4. `make -j2`；Pixi 相关解析无回归。
