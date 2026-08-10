# 梦境层间传送卡住（29*_01→_02）

日期：2026-08-05

## 现象

`29梦境空间_01` 点副本向导「副本传送」进 `29梦境空间_02` 后进度条/场景卡住，至
`idle-timeout`。

## 证据（`1966c234` / `lxh001`）

```text
mock_npc_dialog_teleport_prereq_gate … instance=29梦境空间_02 show_teleport=1
mock_npc_instance_enter … scene=29梦境空间_02 … response=30/1 resp=52
# 无 drain_30_2 / 无 map_stone_loading_clear_arm
scene_ready … reason=npc-instance-dream-enter
mock_scene_npc_seed phase=startup-scene-sync-poll … npcnum=1
task_prompt_refresh_deliver …
session_reuse … hold_ms=31526 end=idle-timeout
```

对话与服务选择已正常（`builtin-npc-dialog` / `builtin-npc-service`）；偏离在进图清条。

## 根因

1. **触发**：已在 `29*` 层内再 `30/1` 进另一 `29*`（副本传送）。
2. **违反契约**：户外→梦 `dream_drain` 会同包/随后武装 poll `30/2`
   ResetDownloadState；层间 hop 走 else 裸 `30/1`，不武装 clear。
3. **首个错误状态**：新层 DF 打开 DoLoading 后无 `30/2`；startup poll 又先投
   非空 `27/11`，条无法收尾。
4. **证据**：`resp=52` 裸 enter；无 `mock_map_stone_loading_clear`；有
   `startup-scene-sync-poll` seed 后 idle。

已排除：NPC 对话 vitals 抢包（本日志已有 `mock_npc_dialog`）。

## 修改

| 点 | 行为 |
|---|---|
| `build_instance_enter_response` | `29*A→29*B`（visible≠target）武装 `map_stone_loading_clear` + vitals（与 drain 同） |
| startup scene-sync poll | 梦境 clear `remaining>0` 时 hold 非空 `27/11`（`dream-map-stone-clear-pending`） |

期望：

```text
mock_npc_instance_dream_layer_clear_arm … from=29*_01 to=29*_02
mock_scene_npc_poll_hold … reason=dream-map-stone-clear-pending
mock_map_stone_loading_clear … remaining→0
# 其后可有 startup 27/11；可走；无 idle-timeout
```

## 续：第一次闪一下、第二次卡条（2026-08-05）

### 证据

```text
scene_ready … reason=revival-map-vitals-arm   ← vitals 先把 visible 写成 _02
dream_layer_clear_arm … from=29*_02 to=29*_02  ← 日志误报；hop 判定时仍是 _01
mock_map_stone_loading_clear … remaining=2→1   ← target_valid 仍在却 drip
session_reuse … idle-timeout
```

### 根因

1. 层间 hop 武装了 poll `30/2`，但梦境 map-stone **豁免** `target_valid` hold，
   ScreenInit 前就开始 ResetDownloadState → 闪一下、传送未完成。
2. `arm_map_actor_vitals_sync` 在 `save_player_pos` 之后用 `role→target` 调
   `mark_scene_ready`，与 pending EnterScene 竞态。

### 修正

- 快照 `fromScene` 再判 hop / 打日志。
- 层间 hop：`map_stone_loading_clear` + `shopReturnDreamHudMapStoneHold`
  （与 kind2 shell 同 hold），**不**在此武装 vitals。
- clear 等 ScreenInit / 2s settle 后再 drip；startup `27/11` 仍 hold 至 remaining=0。

期望：

```text
dream_layer_clear_arm … from=29*_01 to=29*_02 action=poll-30/2-hold-until-ScreenInit
map_stone_kind2_target_settle … 或 same-scene 2/3 后
mock_map_stone_loading_clear … remaining→0
shop_return_dream_hud_map_stone_done …
# 进 _02 可走；无闪退条 / 无 idle-timeout
```

## 续：服务端已 hold，客户端仍不传送（2026-08-05）

### 证据（服务端）

```text
dream_layer_clear_arm … from=29*_01 to=29*_02 … hold-until-ScreenInit
mock_scene_npc_poll_hold … dream-map-stone-clear-pending
map_stone_kind2_target_settle … age_ticks≈36
mock_map_stone_loading_clear … remaining→0
# 无客户端 ScreenInit Ok；常 idle-timeout
```

### 证据（客户端）

```text
dream_enter_scene_pass_first … scene=29*_02 gate=2
download_busy_clear reason=same-suppressed-dream-reenter
screen_mgr same-suppressed-dream-reenter … scene=29*_02
  evidence=fallback-after-entry-intercept-miss
```

### 根因

1. **触发**：层间 `30/1`；`pass_first` 放行主 EnterScene（gate=2）。
2. **违反契约**：`screen_mgr` 在 `tmp1==vmAddedScreen`（同 mmGame 换层）时把
   `block_dream_completion_reenter` 的 fallback 当成「禁二次进图」，把 **主**
   EnterScene 也拒掉；户外→`_01` 常新建 screen，不进该分支故正常。
3. **首个错误状态**：ScreenInit 未启动；服务端等不到 target 完成，timeout drip
   `30/2` 后仍卡壳。
4. **证据**：`pass_first` 紧接 `same-suppressed-dream-reenter`；服务端
   `map_stone_kind2_target_settle`。

已排除：服务端未武装 clear（本路径已有 layer_clear_arm + hold）。

### 修正

[`src/main.c`](../../src/main.c) `screen_mgr`：`gate==2`（`pass_first` 后的主进图）
→ `dream-primary-enter-accept` + remember；仅非 gate=2 的 completion 兜底继续
`same-suppressed-dream-reenter`。

期望：

```text
dream_enter_scene_pass_first … gate=2
screen_mgr dream-primary-enter-accept … gate=2
# ScreenInit Ok；map_stone clear remaining→0；_02 可走
# 无 same-suppressed-dream-reenter；无 idle-timeout
```

## 续：有加载但仍卡进度条（2026-08-05 #3）

### 证据（服务端，`89440f1c`）

```text
dream_layer_clear_arm … from=_01 to=_02 hold-until-ScreenInit
# WT18/7 _02 资源 uptodate（加载已启动）
mock_dream_instance_2_3_loading_clear_only … 30/2x3 resp=158
mock_map_stone_loading_clear … remaining→0
mock_scene_npc_seed phase=startup-scene-sync-poll … npcnum=1
# 无 moveinfo；idle-timeout
```

### 根因

1. **触发**：同 mmGame 层间 hop 走 WT `2/3`（不是户外→梦的 post-enter `25/5`）。
2. **违反契约**：`loading_clear_only` 只回 `30/2x3`；`wait_post_enter=true` 后又
   `mark_scene_moveinfo_npc_seed_pending`（该函数会清 wait 标志）→ 标志被抹掉；
   clear 耗尽后 poll 单独投非空 `27/11`，无同包 `ResetDownloadState`，再次武装
   DoLoading（与 shop-return / 丹霞山→桃花岛证据同类）。
3. **首个错误状态**：进度条在 `27/11` 后再开且无收尾 clear。
4. **证据**：`2_3_loading_clear_only` → `startup-scene-sync-poll` → idle；户外→`_01`
   对照为 `dream_post_enter_no_27_12`（`27/11`+arm clear）且无该序。

### 修正

[`src/server/mock_server_social.c`](../../src/server/mock_server_social.c) 梦境
`2/3`：`27/11+27/4+7/42+30/2x3`（仍无 `27/12`，避 debug-0b451a），记
`mock_dream_instance_2_3_complete`；不再假等 post-enter。

期望：

```text
screen_mgr dream-primary-enter-accept …
mock_dream_instance_2_3_complete … action=27/11+27/4+7/42+30/2x3+arm-poll-clear
# 进度条消失；_02 可走；无 startup 单独 27/11 卡条；无 idle-timeout
```
