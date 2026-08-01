# 战后开商城闪退（map 1/1/14 vitals）

Date: 2026-07-28

Status: implemented (server) — confirmed by A/B

```text
trigger: victory arms map 1/1/14 (mmShop actor-state shape) → later mall open
         scene-interaction-followup resp=1156 → client flash
fix: do not arm map 1/1/14 after ordinary victory
```

## 根因陈述

- **触发**：战斗胜利后武装 `pendingMapActorVitalsSync`，经 scene-poll 投递
  `1/1/14`+actorinfo（与 `mmShop:0x9DE` 同源），再开商城。
- **被违反的契约**：该 `1/1/14` 是商城 actor-state 对象族；战后在 mmGame 上投递
  会污染随后 mmShop 开店解析（即使不再 wt-dispatch 抢 `2/10`）。
- **首个错误状态**：开店 `resp=1156` 到达客户端后闪退；服务端包形与曾成功开店
  的 combo 日志一致。
- **证据**：
  1. 关掉胜利 vitals（`map_actor_vitals_sync_skip`）→ 能进店、可翻页/买。
  2. 仅 scene-poll 投 vitals、无抢包时仍闪 → 抢包是加重因素不是充分条件。
  3. `docs/re/2026-06-26-npc-shop-purchase.md`：`1/1/14` 属 mmShop:0x9DE；
     catalog 铜钱退款也禁止误用该对象。
- **排除**：shop combo 字节本身；kind-2 `30/1`（崩溃路径未武装）。

## 修改

1. **胜利默认不武装** map `1/1/14`。地图 HUD 靠战斗 `4/7` 与后续 `5/10` /
   shop-return group-type1。复活/fresh-shell 仍走原 vitals arm。
2. 保留防护：`reenter-clear` / 开店取消残留 vitals；战斗中不投；不抢
   `2/10` / moveinfo。
3. Opt-in：`CBE_MAP_VITALS_AFTER_BATTLE=1` 恢复旧胜利 vitals（仅取证）。

## 验证

1. 打怪 → 开商城：无 `map_actor_vitals_sync_arm`（胜利）；能进店。
2. 退商城：空图可有 `shop_return_kind2_reenter_arm` → `shop_return_scene_enter`；
   踩怪 `request-live-node`。
3. 战死复活路径仍可有 vitals（非本胜利 arm）。
