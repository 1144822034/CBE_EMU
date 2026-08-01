# 临安出店 2s 兜底误伤 post-catalog

Date: 2026-07-28

Status: implemented (server)

```text
trigger: c04临安府_05 shop exit, stand still
symptom: loading stuck after shop-return
root: 2s-no-moveinfo applied to post_catalog=1 → kind-2 at arm_age=20
      immediately after 27/11 (DoLoading not settled)
fix: 2s fallback only for shell clear (post_catalog=0);
      post-catalog keeps remaining=8 or moveinfo-live cancel+kind2
```

## 证据

```text
shop_return_npc_catalog_deliver ... post_catalog=1
shop_return_loading_clear_settle ... arm_age=20 remaining_was=8 via=2s-no-moveinfo post_catalog=1
shop_return_kind2_reenter_arm ... via=post-catalog-clear-timeout delay_ticks=0
shop_return_scene_enter ... 30/1
# 之后无 moveinfo；卡 loading
```

与 `2026-07-28-linan-shop-return-kind2-stall.md` 同类：kind-2 抢在 catalog 加载落定之前。

## 验证

```text
临安府_05 出店站立：
  shell 可有 settle via=2s-no-moveinfo post_catalog=0
  catalog_deliver ... post_catalog=1
  # 无 settle post_catalog=1 via=2s-no-moveinfo
  loading_clear ... remaining 7→0 或 moveinfo-live-post-catalog
  kind2 via=post-catalog-clear-done|moveinfo-live-post-catalog
  # 可走
```
