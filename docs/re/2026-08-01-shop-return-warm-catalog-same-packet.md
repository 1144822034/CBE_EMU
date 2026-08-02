# 非野外暖目录出店：禁止二次 DoLoading drip

日期：2026-08-01

**状态：部分 superseded。** 传送后 deferred 同包 `27/11+30/2` 仍卡；权威暖路径见
`2026-08-01-shop-return-warm-type27-single-load.md`（type27 非空一次 + shell 30/2）。
冷目录仍可走 deferred catalog + post-catalog。

## 现象

有 type-21、无野怪的城镇图（蓬莱 / 临安府_05 等）退出商城：场景闪现后**再次**进度条，站立时常卡住。

用户契约：正常非野外图出店**不需要二次加载**。

## 证据链

```text
shell 30/2 settle remaining=0          ← 第一次清条，场景可见
catalog_deliver 27/11 prior_seeded=1   ← 再开 DoLoading（二次加载）
post_catalog arm remaining=3
loading_clear remaining=2              ← drip 未走完即卡
# 无 warm same-packet 30/2 / busy via warm-catalog
```

## 根因陈述

- **触发**：本访已投过 type-21（`priorSeeded`）后出店。
- **被违反的契约**：暖资源场景在 shell `ResetDownloadState` 之后不应再开一段依赖 poll drip 的二次 DoLoading；非空 `27/11` 若必须补客户端 NPC 视图，应在同包用 `30/2` 立刻关掉。
- **首个错误状态**：`catalog_deliver` 只带 `27/11` 并 `arm post_catalog remaining=3`，站立无 moveinfo 时 drip 中断 → 二次进度条卡住。
- **排除**：空图 kind-2；冷目录（`!priorSeeded`）仍可 post-catalog drip。  
  曾证伪「skip 27/11」（NPC 不显示，见 path-matrix）；本修复**仍发**非空 `27/11`，只是同包关闭。

## 修改

[`mock_server_social.c`](../../src/server/mock_server_social.c) `shop_return_npc_catalog_deliver`：

| priorSeeded | 行为 |
|-------------|------|
| 1（暖） | `27/11` + 同包 `30/2-no-posinfo` → `busy_ack`；**不** arm post-catalog |
| 0（冷） | 仅 `27/11` + post-catalog drip（不变） |

## 验证

1. `make -j2`，重启。
2. 临安府_05 / 蓬莱：地图石或登录后进商城再退出（站立）：
   - `catalog_deliver ... prior_seeded=1 completion=same-packet-30/2`
   - **无** `post_catalog=1` arm / drip
   - `shop_return_kind2_skip ... via=warm-catalog-same-packet-30/2`
   - 无二次进度条；可走；NPC 可见
3. 空图仍 kind-2；冷目录仍可 `post_catalog=1`。
