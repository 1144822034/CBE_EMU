# 临安退出商店卡住（kind-2 后 poll 27/11 干涉）

Date: 2026-07-28

Status: implemented (server)

```text
trigger: c04临安府_05 shop exit after portal-kind2 fix (npcnum>0)
symptom: loading stuck after shop return
root: kind-2 30/1 → poll 27/11 → post-catalog 30/2 raced same-scene
      client 2/3 (scene-target-remember); clear cancelled remaining=8
fix: 30/1 hands catalog to follow-up 27/11; hold post-catalog 30/2
      across same-scene re-enter / sceneVisiblePending
```

## 与「刚刚改的」关系

是。把 kind-2 扩到有 NPC 图后，临安路径变成：

1. shell poll `30/2` → `remaining=0`
2. `shop_return_kind2_reenter` → poll `30/1`
3. poll `27/11` + arm post-catalog clear (`remaining=8`)
4. 立刻 `shop_return_loading_clear_cancel reason=scene-target-remember`
5. 同场景 `builtin-scene-change` + `mmgame-transfer-followup`
6. post-catalog `30/2` 再也跑不完 → 卡加载

蓬莱有时还能靠后续 moveinfo / 二次进图「碰巧」走出；临安在这条链上更早卡死。

## 契约（再修正，见蓬莱传送后卡）

先 catalog 再 kind-2，避免站立态先 `30/1`：

```text
shell 30/2 → poll 27/11 → post-catalog 30/2 → busy → kind-2 30/1
→ short map-stone 30/2（同场景 hold）
```

详见 `2026-07-28-penglai-shop-return-after-teleport-stall.md`。

## 验证

```text
临安府_05 / 蓬莱地图石后进出商店：
  shop_return_npc_catalog_ready ... via=shell-clear-done|moveinfo-live
  mock_shop_return_npc_catalog_deliver ... post_catalog=1
  shop_return_kind2_reenter_arm ... via=post-catalog-clear-done
  shop_return_scene_enter ... 30/1
  map_stone_loading_clear_* ...
  # 可走；无长期无 moveinfo 的 DoLoading
```
