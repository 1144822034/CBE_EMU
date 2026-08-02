# 地图石后出店：post-catalog 30/2 只靠 poll 被 25/5 饿死

日期：2026-08-01

## 触发

地图石进 `c04临安府_05` → 开商城 → 退出。shell `30/2` + deferred `27/11` 已通；post-catalog 只见一条 `loading_clear remaining=2`，其后仅有 `wt=25/5`，进度条不消。

与 `2026-07-31-shop-return-post-catalog-clear-hold.md` 同一契约缺口的另一条路径：本次日志**无** `post-catalog-transfer-active` hold / mismatch cancel。

## 证据链

```text
shop_return_loading_clear_settle ... post_catalog=0 via=2s-no-moveinfo
mock_shop_return_npc_catalog_deliver ... prior_seeded=1 post_catalog=1
shop_return_loading_clear_arm ... remaining=3 lite=1
mock_shop_return_loading_clear ... remaining=2   ← 仅一次
net_send wt=25/5 builtin-scene-default-event    ← 无更多 scene_sync_poll clear
# 无 remaining→1→0 / kind2_skip / busy_ack via=post-catalog-clear-done
```

蓬莱同会话出店曾靠 `moveinfo-live-post-catalog` 提前 cancel+busy 脱身；临安站立无 moveinfo，必须走满 post-catalog drip。

## 根因陈述

- **触发**：地图石进有 NPC 图 → 站立出店 → deferred `27/11` 打开第二次 DoLoading。
- **被违反的契约**：post-catalog `30/2` 必须在第二次 DoLoading 窗口内多次投递至 `remaining=0`，再 `busy_ack`；不能在 clear 尚未到期时把 poll 让给 task prompt，也不能在客户端改发 `25/5` keep-alive 时完全不投 `30/2`。
- **首个错误状态**：
  1. `build_pending_shop_return_loading_clear` 因 `age < minAge` 返回 0（无日志）；
  2. 同一 `scene_sync_poll` 落到 `task_prompt_refresh`（历史 resp≈77），表现为“有网无清条”；
  3. 二次 DoLoading 期间客户端以 `25/5` 为主、poll 变稀或停，`remaining` 停在 2。
- **排除**：本次无 stale `liveTeleport` hold（`2026-08-01-shop-return-after-teleport-stale-hold.md` 已处理）；无 `scene-mismatch` cancel。

## 修改

1. [`mock_server_social.c`](../../src/server/mock_server_social.c) `scene_default_event`（`25/5`）：若 `shopReturnLoadingClearPending && postCatalog`，优先投递 pending `30/2`。
2. 同文件 `scene_sync_poll`：post-catalog clear 仍 pending 且本拍未投出时 **return 0**，禁止 task prompt / nearby 抢槽。
3. 不恢复 post-catalog `2s-no-moveinfo` settle；不跳过 `27/11`。
4. 若日志出现 `map_stone_loading_clear_cancel ... reason=shop-open`，见
   `2026-08-01-shop-open-cancels-mapstone-clear.md`（欠账被抹掉，单靠本文件不够）。

## 验证

1. `make -j2`，重启 mock + 客户端。
2. 临安府_05：地图石进入 → 商城进出（站立）：
   - `catalog_deliver ... prior_seeded=1`
   - 多条 `mock_shop_return_loading_clear ... post_catalog=1` remaining 2→1→0
     （可出现 `shop_return_loading_clear_via ... path=25/5`）
   - `shop_return_kind2_skip ... via=post-catalog-clear-done` + `busy_ack`
   - 无长期仅 `25/5` / poll 77 而 `remaining>0`
3. 进度条消失，可走；NPC 仍在。蓬莱出店仍可 `moveinfo-live` 脱身。
