# 蓬莱边缘传送门后 loading / 退商城二次 clear

Date: 2026-07-28

Status: implemented (server)

```text
trigger: c00蓬莱_01 -> 00蓬莱_02 edge portal (full-bootstrap)
         then optional shop-return on npcnum=3
symptom: type27 seeds 27/11 without poll 30/2; later shop-return post-catalog
         clear×5 still stuck
fix: type27 wait_post_enter arms map_stone-shaped poll 30/2;
      post-catalog clear remaining=8 gap=12 + settle before 27/11
```

## 根因

1. **传送门**：`scene-change-full-bootstrap` 武装 `wait_post_enter`，客户端用
   `25/5` type27 消费非空 `27/11`，但该分支原走通用 `type27-followup`，**不**
   arm/rearm poll `30/2`（地图石 wait_wt6 才有）。壳上带坐标 `30/2` 后 NPC
   `DoLoading` 无收尾。
2. **退商城二次窗**：deferred `27/11` 后仍用 shell 的 `remaining=5`/`gap=8`；
   `npcnum=3`（蓬莱_02）可晚于窗口结束仍挂 loading。

## 修改

1. type27：识别 `wait_post_enter` → `type27-post-enter` seed +
   `map_stone_loading_clear_arm`（或已有则 rearm）。
2. shop-return：shell clear 后 settle 8 tick 再投 `27/11`；
   `arm_shop_return_loading_clear_after_catalog` → remaining=8、gap=12。

## 验证

1. 蓬莱_01→_02：`mock_scene_npc_seed_deliver ... after=wait-post-enter arm_loading_clear=1`
   后多次 `mock_map_stone_loading_clear`；可走。
2. _02 退商城：`post_catalog=1 remaining=8`；loading 落下；NPC 在。
3. 临安有 NPC 退商城仍正常。
