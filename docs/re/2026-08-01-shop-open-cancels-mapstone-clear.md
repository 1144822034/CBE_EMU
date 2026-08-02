# 地图石未完成 30/2 被 shop-open 取消 → 出店卡条

日期：2026-08-01

## 触发

地图石进 `c00蓬莱仙岛_01` → 在 `map_stone_loading_clear remaining=1` 时开商城 → 退出。进度条卡住。

## 证据链

```text
mock_map_stone_loading_clear ... remaining=1
stale_map_transfer_clear ... reason=shop-open map_stone_clear=1
map_stone_loading_clear_cancel ... remaining=1 reason=shop-open   ← 欠账被抹掉
# … mmShop …
shop_return shell 30/2 → catalog_deliver 27/11 post_catalog=1
# 第二次 DoLoading 打开；地图石 owed ResetDownloadState 已不存在
# 站立无 moveinfo-live；进度条不消
```

对比同会话临安出店：`moveinfo-live-post-catalog` 取消 post-catalog 后仍可走（玩家已在动）。蓬莱站立依赖完整 clear 闭环。

## 根因陈述

- **触发**：地图石 poll `30/2` 尚未耗尽时打开 mmShop。
- **被违反的契约**：map-stone `ResetDownloadState` 窗口必须走完（或等价补发）；不能因「场景已 ready、可开店」就 cancel 未完成的 `remaining`。
- **首个错误状态**：`clear_stale_map_transfer_for_shop(shop-open)` 调用
  `cancel_map_stone_loading_clear`，`remaining=1` 被丢弃；随后出店 `27/11` 再开 DoLoading，clear 账本已空。
- **排除**：仅靠加长 post-catalog drip / 25/5 投递无法偿还被 cancel 掉的地图石次数；与 stale teleport hold 正交。

## 修改

1. [`mock_server_equipment_npc.c`](../../src/server/mock_server_equipment_npc.c)
   `clear_stale_map_transfer_for_shop`：只清 teleport / `last_scene_change_target`；
   **保留** `mapStoneLoadingClearPending`（打 `map_stone_loading_clear_preserve`）。
2. [`mock_server_social.c`](../../src/server/mock_server_social.c) map-stone clear：
   `mmShopShellActive` 时 hold（不投、不 cancel）；`25/5` 可在出店后补投。
3. post-catalog arm：backdate tick，使 catalog 后下一条 keep-alive 即可开始 drip。

## 验证

1. `make -j2`，重启 mock。
2. 地图石进蓬莱 → **在 loading clear 未完成时** 立刻开商城再退出（站立）：
   - `map_stone_loading_clear_preserve ... remaining≥1`（无 `cancel ... reason=shop-open`）
   - 出店后可见 `mock_map_stone_loading_clear` 和/或 post-catalog `30/2`→0
   - 可走；无长期进度条
3. 临安出店 / moveinfo-live 路径仍可走。
