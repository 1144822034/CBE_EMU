# 空 NPC 场景 shop-return 后延迟重建 kind-2

Date: 2026-07-28

Status: implemented (server) — default on; NPC maps also use kind-2 then 27/11

```text
trigger: mmShop exit on empty type-21 catalog (e.g. 01桃花岛_04 npcnum=0)
         OR npcnum>0 maps that need portal/combat nodes (e.g. 00蓬莱仙岛_02)
symptom: walk through monsters / cannot hit edge portals; hangup may still work
fix: after poll 30/2 clear done OR moveinfo-live cancel shell clear,
       delay-arm same-pos 30/1; NPC maps then poll 27/11 + post-catalog 30/2
note: post-battle mall crash is separate — do not arm victory map 1/1/14
      (docs/re/2026-07-28-post-battle-shop-open-vitals-crash.md)
      portal regression: docs/re/2026-07-28-shop-return-portal-kind2.md
```

## 根因

mmShop→mmGame 壳重建走 `27/11` + poll `30/2`，**禁止**同包 `30/1`
（`2026-07-27-shop-return-loading-stall.md`）。空 NPC 目录又发不出有效
type-21 reseed，地图怪精灵还在，SCE kind-2 碰撞节点未重建。

## 修改

1. `vm_mock_service_session_arm_shop_return_kind2_reenter`：仅 `npcnum==0`；
   取角色当前坐标；`earliestTick = now + 8`。
2. 武装时机：
   - poll loading clear `remaining=0`
   - `moveinfo-live` 取消 clear（真实路径常提前取消，remaining=0 不到）
3. 投递：既有 `try_deliver_pending_shop_return_scene_enter`（scene-poll /
   wt-dispatch），不抢 `4/1` / hangup `2/10`。
4. 取消：shop-open / scene-pending / rehydrate 清 stale arm。
5. 有 type-21 场景：同样 arm kind-2，再 poll 非空 `27/11`（见
   `2026-07-28-shop-return-portal-kind2.md`）；勿再 `has-type21` skip。
6. mmShop 打开期间（`mmShopShellActive`）hold 延迟 `30/1`，避免 EnterScene
   打进商城壳；开店家族请求不被 wt-dispatch 抢成 `30/1`。
7. post-catalog `30/2` 不被 `moveinfo-live` 取消。

## 验证

1. 空图退商城：不卡 loading；日志 `shop_return_kind2_reenter_arm` →
   `shop_return_scene_enter ... 30/1`。
2. 踩怪：`mock_challenge_battle_start ... target_source=request-live-node`。
3. 有 NPC 图：`kind2_reenter_arm ... npcnum>0` → `scene_enter` →
   `npc_catalog_deliver` → post-catalog clear；可踩传送门。
4. 挂机仍可用。
5. 开商城不闪退；若 kind-2 已武装，开店日志可有
   `shop_return_scene_enter_hold ... reason=mmShop-open`。
