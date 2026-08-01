# 有 NPC 场景退出商城卡 loading

Date: 2026-07-28

Status: implemented (server) — defer nonempty 27/11 after shell 30/2

```text
trigger: mmShop exit on type-21 npcnum>0 (e.g. c04临安府_01 npcnum=4)
symptom: poll 30/2 ×5 + 26/0 后仍卡 loading；空 NPC 图同契约正常
fix: shop-return follow-up 只回空 27/11；shell poll 30/2 后再 poll 非空 27/11，并二次 arm poll 30/2
```

## 根因陈述

- **触发**：有 type-21 的场景退商城；WT6/1/task-subset 在 mmGame 壳重建时同包下发非空 `27/11`。
- **被违反的契约**：`ResetDownloadState`（poll `30/2`）必须落在对应 `DoLoading`/`ScreenInit` 之后。非空 `27/11` 会再开一轮 NPC 资源加载，与壳层 loading 叠在同一 clear 窗口。
- **首个错误状态**：`scene_npc_lifecycle_seed ... shop-return ... npcnum=4` 后，`shop_return_loading_clear remaining=0` + `26/0` 已投完，客户端仍无 moveinfo（loading 未落）。
- **对照**：空图 `npcCount==0` 不发非空 `27/11`，同一 poll `30/2` 窗口可落 loading。
- **排除**：kind-2 `30/1`（`has-type21` 跳过）；胜利 map vitals（本路径无）。

## 修改

1. shop-return + `npcnum>0`：follow-up 只挂**空** `27/11`，武装 `shopReturnNpcCatalogPending`。
2. shell poll `30/2` 到 `remaining=0` 后：不立刻 `26/0`；下一拍 poll 投递非空 `27/11`，并 **rearm** shop-return loading clear。
3. 第二次 clear 结束后：再 `26/0`；仍不武装 empty-NPC kind-2。
4. `moveinfo-live` 提前取消第一段 clear 时：catalog pending 保留，下一拍仍投 `27/11` + rearm clear。

## 验证

1. `c04临安府_01` 进商城再退：
   - `shop_return_npc_catalog_defer ... npcnum>0`
   - follow-up **无** `shop-return-scene-followup-reseed` 非空 seed
   - `mock_shop_return_loading_clear ... remaining=0`
   - `shop_return_npc_catalog_ready` → settle → `mock_shop_return_npc_catalog_deliver ... post_catalog=1`
   - 再一段 `loading_clear ... post_catalog=1 remaining=8`
2. loading 落下后可走；NPC 仍在。
3. 空图退商城：仍无 defer；可有 `shop_return_kind2_reenter_arm`。
4. 蓬莱边缘门：见 `2026-07-28-penglai-portal-post-enter-loading-clear.md`。
