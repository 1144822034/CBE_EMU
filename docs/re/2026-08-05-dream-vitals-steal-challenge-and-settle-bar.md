# 梦境：vitals 抢 4/1 开战 + 退店后结算卡进度条

日期：2026-08-05

## 现象

1. 刚进 `29梦境空间_01` 遇怪不能战斗。
2. 商城退出后可开战；战斗完成后卡进度条（至 `idle-timeout`）。

## 证据（`801952f9` / `lxh001`）

### 进本不能打

```text
map_actor_vitals_sync_arm … remaining=2
…
map_actor_vitals_sync … via=wt-dispatch response=1/1/14 resp=226
net_send … wt=4/1 len=65 source=builtin-map-actor-vitals-sync
# 直至开商城前无 mock_challenge_battle_start
```

成功退店后再战：

```text
net_send … wt=4/1 len=60 source=builtin-challenge-interaction resp=203
mock_challenge_battle_start … subtype=5 scene_start=1
```

### 退店后结算卡条

```text
shop_return_kind2_skip … match-outdoor-warm-single-load
…
mock_battle_settlement_exit … response=4/8+4/11+4/9
mock_battle_encounter_cooldown_clear … 25/12
session_reuse … idle-timeout
# 拆场后无 moveinfo / 无 ResetDownloadState
```

## 根因

1. **进本**：梦境脚碰 `4/1`（len=65）未过「仅允许 4/1±2/1」的严格 challenge 检测；`wt-dispatch` 用 pending map-actor vitals 抢座，首个错误状态是遇怪请求被 `1/1/14` 回答。
2. **退店结算**：梦境对齐野外暖路径跳过 kind-2 `30/1` 后壳更脆；权威拆场仍带 `4/11 type=0`，可再次拉起 DoLoading，且无后续 `30/2` ResetDownloadState。

## 修改

1. `is_challenge_interaction_request`：忽略非 4/1、非 2/1 的兄弟 object（仍要求唯一 4/1 + id/index/posx/posy）。
2. `wt-dispatch` vitals/shop-return-enter：额外拒绝 WT kind==4 与 object `1/4/1`（纵深防御）。
3. 梦境 `shop_return_kind2_skip` 置 `shopReturnDreamNeedSettleBarClear`；`settlement_exit` 完整 `4/8+4/11+4/9` 后武装 poll `30/2`。

## 续测（`6ba47803`）

进本开战已恢复（`subtype=5`）。退店后开战也正常；结算仍卡并
`idle-timeout`：

```text
shop_return_dream_settle_bar_arm …
mock_battle_settlement_exit_dream_bar … arm-poll-30/2-after-type0
mock_battle_settlement_exit … 4/8+4/11+4/9
mock_battle_encounter_cooldown_clear … 25/12
session_reuse … idle-timeout
# 其后才 drip map_stone_loading_clear remaining→0，仍第二次 idle-timeout
```

### 修正根因

事后 `30/2` **救不回** type=0 已拉起的 DoLoading。改为梦境 kind2-skip 壳上
settlement_exit 只发 **`4/8`**（与 prepend-exit-reenter 同契约），不再带
type0/4/9。

期望：

```text
shop_return_dream_settle_bar_arm …
mock_battle_settlement_exit … response=4/8-only evidence=dream-shop-return-no-type0-DoLoading
# 可走；无 idle-timeout 卡条
```

## 续：进本 NPC 对话被 vitals 抢 26/1（2026-08-05）

### 现象

刚进 `29梦境空间_01` 与 NPC 对话卡住，至 `idle-timeout`。

### 证据

```text
map_actor_vitals_sync … via=wt-dispatch response=1/1/14 resp=226
net_send … wt=26/1 len=30 source=builtin-map-actor-vitals-sync
task_prompt_refresh_deliver …
session_reuse … end=idle-timeout
# 全程无 mock_npc_dialog
```

### 根因

1. **触发**：使者进本 drain 武装 `map_actor_vitals_sync remaining≥1`。
2. **违反契约**：NPC 对话请求为 WT `26/1` type=1；`wt-dispatch` 在
   `build_npc_dialog` 之前用 pending vitals 抢座，回 `1/1/14`。
3. **首个错误状态**：对话回调收到 actorinfo 而非 dialog 对象。
4. **证据**：同族于上文「vitals 抢 4/1」；本日志 `request=30`/`source=builtin-map-actor-vitals-sync`。

### 修改

`wt-dispatch` vitals/shop-return-enter 额外拒绝：

- `is_npc_dialog_request` / `is_npc_service_dialog_request`
- WT kind==26 / object `1/26/1`

Vitals 仍由 scene-poll 投递。

期望：

```text
# 进本点 NPC
net_send … wt=26/1 source=builtin-npc-dialog
mock_npc_dialog actor=…
# 无 builtin-map-actor-vitals-sync 抢 26/1；无 idle-timeout
```
