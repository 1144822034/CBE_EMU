# 打怪传送蓬莱后退出商店卡住

Date: 2026-07-28

Status: mitigated — warm priorSeeded uses lite post-catalog; see
`2026-07-28-shop-return-post-catalog-lite.md` (do not skip 27/11)

```text
trigger: battle → map-stone to c00蓬莱仙岛_01 → shop open/exit
symptom: loading stuck after second 27/11 / post-catalog (standing, no moveinfo)
falsified fix: catalog_skip already-seeded (restores walk but NPCs missing)
current: always defer→poll 27/11; priorSeeded → remaining=3 lite (+2s settle)
unresolved: if lite still stalls standing, re-take evidence (packet shape), not skip
```

## 证据（用户日志）

临安 / 桃花岛：`moveinfo-live` 提前取消 shell，玩家已在走，kind-2 风暴可撑过去。

蓬莱（地图石进图后站立退商店）：

1. shell `30/2` `remaining=0`（无 moveinfo）
2. `kind2_reenter_arm via=loading-clear-done` → `catalog_handoff` + post-catalog `remaining=8`
3. `same-scene-kind2-reenter` → spawn `(223,370)` + `mmgame-transfer-followup`
4. post-catalog `remaining` 7→1，**全程无 moveinfo** → 卡加载

## 契约

```text
shell poll 30/2
  → (npc) poll 27/11 + post-catalog 30/2 + busy 26/0
  → kind-2 30/1（重建门/怪节点）
  → short map-stone poll 30/2（settle EnterScene；同场景 hold）
empty: shell 30/2 → kind-2 30/1 → short map-stone 30/2
```

## 验证

```text
# 蓬莱地图石后进出商店
shop_return_npc_catalog_ready ... via=shell-clear-done|moveinfo-live
mock_shop_return_npc_catalog_deliver ... post_catalog=1
shop_return_kind2_reenter_arm ... via=post-catalog-clear-done
shop_return_scene_enter ... 30/1
map_stone_loading_clear_arm / hold ... same-scene-kind2-reenter
# 可走；可踩门

# 临安 / 空图仍不卡；空图仍 kind2 after shell
```
