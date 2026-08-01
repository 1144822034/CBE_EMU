# 退出商城卡 loading（shop-return poll 30/2，无同包 30/2）

Date: 2026-07-27

Status: implemented (server) — wait_wt6 形：follow-up 只种子；poll 清 download

```text
phase: mmShop exit -> ScreenInit mmGame
       -> type27 empty-27/11 gate（completion=none）
       -> WT6/1|task-subset + 27/11 + resources（completion=none）
       -> poll delayed 30/2 ×5（有界；无 sustain）
       -> poll lone 26/0（≥2 tick，对齐 7/7 busy）
trigger: 进商城再退出；不得发 30/1；不得 type27/WT6 同包 30/2
```

## 根因摘要

1. **禁止路径**：shop-return `30/1-current-pos` → 二次 `EnterSceneByMapName`。
2. **权威闭合**：`30/2 {result=1,type=2,scene}` **无 posinfo** →
   `ResetDownloadState`（`2026-07-22-teleport-penglai-mijing-progress-stall.md`）。
3. **错误 `result=2`（已撤）**：无 posinfo 的 `result=2` 不能可靠清 loading。
4. **12s sustain poll**：反复 `ResetDownloadState` 冲掉菜单（已撤）。
5. **二次加载竞态（蓬莱 2026-07-27 本轮）**：
   ```text
   ScreenInit → 701/34/36 → 162(type27+30/2) → 507(WT6/1+27/11+30/2)
   → poll 57×3 → 26/0 → 77；场景先显示再二次加载卡住
   ```
   - type27 尾随 `30/2` 清掉第一次 loading（场景闪现）。
   - WT6/1 `mark_pending` 再灌非空 `27/11`，推第二次 `DoLoading`。
   - 同包 / 过早 poll `30/2` 在第二次 ScreenInit 前 `ResetDownloadState`，
     与地图石 `wait_wt6` 同源（`2026-07-27-teleport-mapstone-async-loading-clear.md`）。
   - `26/0` 只清 `r9+21808`，不解 DF_DataPackage。

## 修改

1. shop-return **type27**：只回空 `27/11` gate，**无** 尾随 `30/2`。
2. shop-return **WT6/1 / task-subset**：`27/11`+资源，**无** 同包 `30/2`；
   `completion=poll-30/2`。
3. `finish_shop_return_rehydrate` 武装 poll（`remaining=5`，gap≥8）。
4. poll 结束后仍可武装 `26/0`（7/7 busy）；moveinfo / shop-open 取消。
5. **无** 同包 / 紧跟 follow-up 的 `30/1`。空 NPC 图可在 loading clear
   **之后**延迟投递（见 `2026-07-28-shop-return-empty-npc-kind2.md`）。
6. **有 NPC 图（2026-07-28）**：follow-up 只空 `27/11`；shell `30/2` 后再
   poll 非空目录并二次 clear（见 `2026-07-28-shop-return-npc-catalog-defer.md`）。

## 验证

1. 退商城日志：
   - `mock_shop_return_type27_gate ... completion=none`
   - `mock_scene_resource_followup_repeat_ack ... shop_return=1 completion=poll-30/2`
   - 随后 `mock_shop_return_loading_clear ... remaining=N`（无 `sustain=1`）
   - 可选 `shop_return_busy_ack ... 26/0`
2. **无**「场景显示后又二次加载」；loading 一次落下后可操作。
3. 有 NPC 场景：`shop_return_npc_catalog_defer` → shell clear →
   `mock_shop_return_npc_catalog_deliver` → 二次 clear；NPC 仍在。
4. follow-up / type27 **无** `30/1-current-pos`；空 NPC 延迟 `30/1` 仅在
   clear 完成或 moveinfo-live 之后。
