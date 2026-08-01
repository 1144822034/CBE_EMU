# 出店后菜单被顶 / 延迟二次进图

Date: 2026-07-28

Status: implemented (server)

```text
trigger: NPC map shop exit → walk / open menu
symptom: mmGame menu auto-closes; long later kind-2 30/1 reloads scene
root: post-catalog hold kept poll 30/2 after moveinfo; kind-2 waited for
      remaining=0 + delay_ticks=8
fix: moveinfo during post-catalog → cancel clear + busy + kind-2 now
      (delay_ticks=0 when via moveinfo)
```

## 证据

- `shop_return_loading_clear_hold ... post-catalog-until-done` 在已有 moveinfo 时仍投 `30/2`（ResetDownloadState）→ 菜单被弹回。
- `arm_age` 拉到 100+ tick 后 `remaining=0` → `kind2_reenter_arm` → `shop_return_scene_enter 30/1` → 二次进图。
- 代码旧注释已写明：长 sustain `30/2` 会 auto-dismiss menus。

## 契约调整

| 信号 | 旧 | 新 |
|------|----|----|
| post-catalog + moveinfo | hold 直到 remaining=0 | **cancel** + busy_ack + kind-2 |
| kind-2 via moveinfo-* | delay 8 ticks | delay **0**（下一拍 poll 投 30/1） |
| clear arm ≥2s 无 moveinfo（仅 shell） | 继续滴 30/2 | **force remaining=1** 本拍收尾 |
| post-catalog ≥2s 无 moveinfo | （曾误用 2s→临安卡） | **不** 2s 强收；走满 remaining 或 moveinfo-live |

门节点仍靠 kind-2；只是不再用“走了还继续 30/2”换门。

## 验证

```text
# 蓬莱2 出店后走动
shop_return_npc_catalog_deliver ... post_catalog=1
# 首段 moveinfo：
shop_return_loading_clear_cancel ... reason=moveinfo-live-post-catalog
shop_return_kind2_reenter_arm ... via=moveinfo-live-post-catalog delay_ticks=0
# 无长时间 hold / 无 arm_age 拉满后再进图
# 出店后点菜单应能停留；仍可踩门
```
