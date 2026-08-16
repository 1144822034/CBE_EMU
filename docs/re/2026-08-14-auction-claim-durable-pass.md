# 拍卖领取仓库/出售凭证：耐久50变成50张满耐久

日期：2026-08-14

## 触发与现象

上架耐久 50 的仓库凭证（834）或装备出售凭证（839），成交领取或撤拍退回后，背包出现 **50 行** 各耐久 50 的凭证。

## 业务链路

1. 背包行：`item.count` = 剩余打开次数（耐久），满额常量 50
2. 上架：`42/4` 把 `bag.count` 写入 `listings.item_count`（整行）
3. 领取/退回：`vm_mock_auction_return_item_to_bag` → `add_backpack_item_to_role(itemId, item_count)`
4. `add_backpack` 对 834/839 的商城语义：`count` = **新购张数**，每张写入满耐久 50

## 首次偏离

领取把 listing 里的「剩余耐久 50」当成「购买 50 张」，循环建 50 行，每行再写满耐久。

仓库取回同一路径：存入时 `moved.count` 是耐久，取回再走 `add_backpack` 会同样放大。

## 根因

834/839 在 `add_backpack_item_to_role` 中的 `count` 合同是商城「买 N 张满耐久」；拍卖/仓库存的权威是「一行剩余耐久」。二者共用同一入口导致语义错位（同类先例：802/803 已拆 `grant_flask_reservoir_points`）。

## 修改

| 位置 | 内容 |
| --- | --- |
| `mock_server_role.c` | `grant_durable_pass_remaining`：一行、`count=remaining`；仓库取回改走此路径 |
| `mock_server_auction.c` | 领取/退回改走 grant；上架强制整行 `count=bag.count` |
| Pixi `AuctionPanel` / `protocolExtras` / `protocolAuction` | 凭证按整张耐久展示，不上架「数量」 |

商城购买仍走 `add_backpack(..., 1)` → 1 张满耐久，不变。

## 验证

1. 耐久 50 的 834/839 上架 → 领取/撤拍 → 背包仍 1 行耐久 50；日志 `mock_grant_durable_pass ... remaining=50`
2. 已用过耐久 30 的凭证同上 → 回包耐久 30，不是 30 张
3. 商城再买 1 张 → 仍新增 1 行满 50
4. 仓库存取同凭证不复制行
5. `make -j2`
