# 退出商城后一直加载场景

Date: 2026-10-05

Status: implemented

## 1. 当前卡点

- 可见现象：从江湖商城返回后进度条（加载场景）不落，直到卡死。
- 路径：开 mmShop → 返回 → mmGame `ScreenInit` / `DoLoading` → 出店 `27/11` 或 `5/10` 武装 poll `30/2` → 同图 `2/3`。

## 2. 调用链

1. 开店 `mark_shop_scene_npc_reseed_pending` 置 `mmShopShellActive`。
2. 返回 `1/1/14` / 首包 `5/10+7/7 type=1` → `finish_shop_return_rehydrate` 武装 `shopReturnLoadingClearPending`（`remaining=1`，无同包 `30/2`）。
3. 首轮 poll 可能先投 early-shell `27/11`（故意不同包带 `30/2`）。
4. 客户端因此发同图 `2/3`，`sceneVisiblePending` 或残留 `last_scene_change_target_valid`。
5. 下一轮 `build_pending_shop_return_loading_clear` 把这当成传送，**return 0**。
6. poll 见 pending 仍在，回空 WT；20s 后 `shop_return_loading_clear_drop`，从未 `ResetDownloadState`。

（客户端是 CBE 模拟器，不是 PixiJS。）

## 3. 根因

- **被违反的契约**：出店无坐标 `30/2` 是 `ResetDownloadState`（`0x0103993C`），不是二次 `EnterScene`。同图 `2/3` 不得取消这条 drip，也不得用空 poll 代替投递。
- **首个错误状态**：`shell-same-scene-2/3` / `post-catalog-transfer-active` hold `return 0`，随后 `if (shopReturnLoadingClearPending) empty WT`。
- **证据**：`2026-07-27-shop-return-loading-stall.md`（权威闭合是 poll `30/2`）；`2026-08-05` 梦境是「不要 cancel remaining」，被实现成「hold 且不投」。

## 4. 修改

- `mock_server_social.c`：同图 shop-return `2/3` 保留 arm 并 **继续投递** `30/2`；真切图仍 cancel。
- `mock_server_equipment_npc.c`：有 NPC 图出店 **禁止** early-shell 非空 `27/11`（会第二次 DoLoading 且下一次 poll 常空 WT）。改为 shell `30/2` → 再 poll `27/11` → post-catalog `30/2`。
- 未改 CBE 全局/PC，也未改 PixiJS。

## 续记：两次加载后卡住（2026-10-05）

`bin/client_out.txt` 临安府_06 出店：

1. `1/1/14` pop mmShop → `DoLoading` + host `do-loading-immediate` → `ScreenInit Ok`（第一次）。
2. `5/10` rehydrate 把 warm 图标成 `inline_on_shell`。
3. `queue_scene_poll resp=55` = early `27/11`（第二次加载）。
4. 随后 `resp=11` 空 poll，日志里 **没有** `WT30/2`。
5. 关窗口时仍 `depth=3`。

## 续记：出店不显示金条（2026-10-05）

mmGame 重建仍会 `LoadPackage`/`DoLoading`，金条是 DF `state=2` 覆盖层。

- pop 真正的 mmShop（init-destroy 差 `0xaa2`，勿把 `0x0518` 户外壳当商城）时武装 `ResetDownloadState`
- **必须仍调用** `DoLoading`（跳过会 `stream_data_result res=0` 在 `0x0100622e` 闪退）
- 调用后立刻清 download 状态；户外 DF `ScreenInit` 再清一次并延迟两拍

验证：出店日志 `shop_return_skip_bar_arm`、`DoLoading skip-bar`、`map_stone_dismiss_bar_arm`，界面无进度条，地图可走。

## 5. 验证

1. 重启 `jh-online-server`（以及 CBE 若测模拟器）。
2. 进江湖商城 → 返回：应有 `shop_return_loading_clear` / `shop_return_loading_clear_keep … deliver-30/2`，**无**长期 `loading_clear_hold` + `loading_clear_drop`。
3. 进度条落下，地图可走；有 NPC 图 NPC 仍在。
4. 地图石后再进出商城同样可走。

## 续记：切图 NPC 迟迟不出现 + 出商城再卡金条（2026-10-05）

`bin/client_out.txt`：

1. 临安府_05→_06 门户 `2/3` 走 full-bootstrap（无 27/11），随后 `25/5+27/11+27/4` `resp=116` 空目录；一串空 `2/1`。登录时已对 _06 `sceneNpcCatalogSeeded`，`mark_pending` 只清全局 bit，post-enter `once_or_empty` 回 empty-11，NPC 等到 poll 兜底才出。
2. 进江湖商城 → `1/1/14` → `shop_return_skip_bar` / `ScreenInit` 关条后，`27/4` `resp=110` 带 **WT30/1** `allow_map_stone_reenter=1` 二次 EnterScene，关窗口仍 `depth=3`。

修改：

- 门户 full-bootstrap 清 per-client NPC ledger；`wait_post_enter` 的 27/11 不再因旧 seeded 回空。
- wt-dispatch 禁止用 kind-2 `30/1` 抢 `27/4`/`27/11`；有 NPC 的图取消 kind-2 30/1（只保留 poll `30/2`）。

验证：切图后应有 `mock_scene_npc_seed … npcnum>0`；出商城应有 `shop_return_loading_clear` 的 `30/2`，**无** `remote_scene_target_apply subtype=1`。需重启 `jh-online-server`。

## 续记：切图 27/11 消耗在嵌套包里（2026-10-05）

`bin/client_out.txt` 临安府_06→_05：full-bootstrap `2/3` `resp=898` 无 27/11；`30/2` 回调里立刻再发 `25/5+2/3+27/11`（无 `queue_data`），再 `25/5+27/11+27/4` **`resp=116` 空目录**。

post-enter-repeat 在已完成的门户上又附 27/12，二次 EnterScene 把 27/11 建进嵌套响应，CBE 不入队。随后 type27 见 alreadySeeded 回 empty-11。

修改：wait_post_enter 的 post-enter-repeat 不再附 27/12、不消耗目录；把 nonempty 27/11 留给会投递的 type27。shop-return 空 27/11 闸不挡住 wait_post_enter。
