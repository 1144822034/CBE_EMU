# 出店 post-catalog 暖目录短窗（不影响空图 / 冷目录）

Date: 2026-07-28

Status: implemented (server)

```text
trigger: NPC map shop exit after scene already seeded this visit
         (login / map-stone / portal 27/11 already delivered)
symptom: 蓬莱地图石→站立出店：post-catalog remaining=8 ~16s 仍无 moveinfo
contract: still re-poll nonempty 27/11; only shorten clear when priorSeeded
```

## 根因陈述

- **触发**：本场景本访问已投过 type-21，再出店补 `27/11`（mmShop 清显示）。
- **被违反的契约**：暖资源场景不需要与冷首次 catalog 同等长的 `ResetDownloadState`  drip；长站立 `30/2×8` 对齐地图石前「过早/过量 clear 可再武装 DF」风险。
- **首个错误状态**：`catalog_deliver` 后 `post_catalog remaining=8`、`arm_age→160+`、无 moveinfo；`remaining=0`+busy 后仍难走。
- **排除**：再 skip `27/11`（已证伪缺 NPC）；对 NPC 默认 kind-2（体验/临安）；对**冷** post-catalog 开 2s settle+kind-2（临安 2s 误伤）。

## 修改（窄门）

1. `catalog_deliver` 在 `mark_pending` **前**读 `g_vm_net_mock_scene_moveinfo_npc_seeded` → `priorSeeded`。
2. `priorSeeded`：`remaining=3`、`gap=8`、`lite=1`；**禁止** post-catalog `2s-no-moveinfo` settle（2026-07-31：settle+busy 截断 DF；见 `2026-07-31-shop-return-post-catalog-clear-hold.md`）。
3. `!priorSeeded`：保持 `remaining=8`、`gap=12`；**禁止** post-catalog 2s settle。
4. 空图：仍无 catalog；shell→kind-2 不变。

## 验证

```text
# 蓬莱 地图石→商城→站立退出
mock_shop_return_npc_catalog_deliver ... prior_seeded=1
shop_return_loading_clear_arm ... remaining=3 post_catalog=1 lite=1
# loading_clear post_catalog=1 remaining 2→1→0（无 2s settle）
shop_return_kind2_skip ... npc-catalog-live-no-reenter
# 无 shop_return_scene_enter；可走；NPC 可见

# 临安出店（亦多为 prior_seeded=1）
# 同上 lite；无 settle post_catalog=1 lite=0；可走；NPC 可见

# 空图
# 无 post_catalog；仍 kind-2
```

## 明确不做

- 不按 already-seeded 跳过出店 `27/11`
- 不对冷 post-catalog 恢复 2s+kind-2
- 不改地图石进图 / 挂机
