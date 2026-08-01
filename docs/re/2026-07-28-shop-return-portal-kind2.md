# 退出商城后穿传送门（有 NPC 图）

Date: 2026-07-28

Status: implemented (server)

```text
trigger: shop-return on npcnum>0 then walk to edge portal
symptom: moveinfo works; no scene-change; portals not hit
fix: after shell+poll 27/11+post-catalog clear, arm kind-2 30/1;
      short map-stone 30/2 settles EnterScene (held on same-scene reenter)
```

## 根因

1. `has-type21` 跳过 kind-2 `30/1` → SCE 边缘传送门 / 碰撞节点未重建。
2. 把 kind-2 放在 catalog **之前** 会触发同场景二次进图并卡 loading
   （临安 / 传送后蓬莱，见 `2026-07-28-linan-shop-return-kind2-stall.md`、
   `2026-07-28-penglai-shop-return-after-teleport-stall.md`）。

## 修改

1. NPC：`shell 30/2 → poll 27/11 → post-catalog 30/2 → kind-2 30/1`。
2. 空图：仍 `shell 30/2 → kind-2`。
3. kind-2 后短 map-stone `30/2`；同场景 `scene-pending` hold，不 cancel。

## 验证

```text
shop_return_npc_catalog_deliver ... post_catalog=1
shop_return_kind2_reenter_arm ... via=post-catalog-clear-done
shop_return_scene_enter ... 30/1
# 踩门
mock_scene_change / mock_scene_change_source_portal
```
