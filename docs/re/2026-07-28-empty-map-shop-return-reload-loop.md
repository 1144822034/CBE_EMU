# 打怪空图退出商店后反复加载

Date: 2026-07-28

Status: implemented (server)

```text
trigger: shop exit on npcnum=0 map (01桃花岛_01) after battle
symptom: scene reloads >2 times in a loop
root: empty lifecycle seed does not clear shopSceneNpcReseedPending;
      kind-2 30/1 follow-ups still match shop-return → rehydrate → kind2 again
fix: finish_shop_return_rehydrate consumes reseed pending (one-shot);
      shopReturnKind2Completed blocks a second 30/1 per mmShop exit
```

## 证据

```text
shop_return_rehydrate ... 01桃花岛_01
kind2 → shop_return_scene_enter 30/1
builtin-scene-change-current-scene-ack|repeat
shop_return_type27_gate          # reseed still pending!
shop_return_task_subset_complete
shop_return_rehydrate            # again
kind2 → 30/1 → current-scene-repeat → ...
```

有 NPC 图在 defer catalog 时会清掉 pending，所以不进这个环。

## 修改

1. `finish_shop_return_rehydrate`：`scene_npc_reseed_consume`。
2. `shopReturnKind2Completed`：开店清零，kind-2 投递置位，重复 arm 跳过。

## 验证

```text
空图进出商店：
  shop_return_rehydrate + scene_npc_reseed_consume
  shop_return_kind2_reenter_arm ... (至多一次)
  shop_return_scene_enter ... 30/1
  # 可有一次 current-scene 收尾，不再 type27_gate→rehydrate→kind2 循环
  # 无 kind2_reenter_arm via=moveinfo-live 第二次（unless new shop-open）
```
