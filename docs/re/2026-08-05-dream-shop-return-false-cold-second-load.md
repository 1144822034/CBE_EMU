# 梦境出商城二次加载卡住：shop-open 清 seeded 误走 cold drip

日期：2026-08-05

## 现象

梦境退商城：场景先出来一次，随后再次进度条并卡住（可至 `idle-timeout`）。

## 日志证据（`lxh001` / `3351d4e6`）

```text
shop_return_loading_clear … remaining=0 post_catalog=0   ← 第一次清条，场景可见
shop_return_npc_catalog_defer … via=rehydrate-missed-warm
mock_shop_return_npc_catalog_deliver … prior_seeded=0 rearm_clear=1 post_catalog=1
  evidence=shell-clear-then-27/11-cold                    ← 第二次 DoLoading
mock_shop_return_loading_clear … remaining=7→0 post_catalog=1
shop_return_busy_ack …
session_reuse … end=idle-timeout
```

用户契约：非野外暖资源图出店**不需要二次加载**
（`2026-08-01-shop-return-warm-catalog-same-packet.md`）。

## 根因

1. **触发**：进梦后本访已 `dream-post-enter` 投过 type-21；再开商城退出。
2. **违反契约**：`mark_shop_scene_npc_reseed_pending` 为允许重发 `27/11` 清掉
   进程全局 `scene_moveinfo_npc_seeded`。deferred catalog 只看该标志 →
   `prior_seeded=0` → **cold** 单独 `27/11` + post-catalog drip。
3. **首个错误状态**：shell `30/2` 后场景已可见，再开二次 DoLoading；站立 drip
   即使 `remaining→0` + `26/0` 仍常半开（与临安暖目录二次卡同族）。
4. **证据**：进本有 `mock_scene_npc_seed phase=dream-post-enter`；开仓后
   `prior_seeded=0` / `shell-clear-then-27/11-cold`。

已排除：dream-25/5 饿死 30/2（本会话已有 `hold_via` / poll drip 至 0）。

## 修改

| 点 | 行为 |
|---|---|
| shop-open | 清全局 seeded **前**记下 `shopReturnNpcResourcesWarm` |
| catalog deliver | `priorSeeded \|= resourcesWarm` → 同包 `27/11+30/2` + `busy_ack` |
| 冷目录 | 无 warm 标记时仍 post-catalog drip |

期望日志：

```text
catalog_deliver … prior_seeded=1 resources_warm=1 completion=same-packet-30/2
# 无 post_catalog=1 arm / remaining=8 drip
```

## 续 3：强制禁止二次加载（2026-08-05）

暖同包 `27/11+30/2` 仍在 **shell 已可见之后** 投递 → 用户仍感知二次加载并卡住。

契约（用户）：梦境退商城**不允许二次加载**。

初版：末次 shell `30/2` 同包内联 `27/11+30/2` → **无二次条，但 NPC 不显示**。

## 续 4：早期 shell 窗口注入 27/11（NPC 可见）

### 根因

1. **触发**：梦境退商城；5/10 `finish_shop_return_rehydrate` 常先于 type27/WT6
   消费 `shopSceneNpcReseedPending`。
2. **违反契约**：户外暖单次加载靠 type27/followup 在 **shell `30/2` 窗口内**
   先发非空 `27/11`（无同包 ResetDownloadState），再由 poll `30/2` 关同一
   DoLoading（`2026-08-01-shop-return-warm-type27-single-load.md`）。
3. **首个错误状态**：reseed 被 rehydrate 吃掉后无暖注入；末次 shell 同包
   `27/11+30/2` 时 ScreenInit/`ResetDownloadState` 已收尾 → type-21 未挂上。
4. **证据**：无二次条 + NPC 缺失；户外 `shop_return_npc_warm_immediate via=type27-inject`
   路径 NPC 可见。

### 修改

- rehydrate 梦境/warm：仍设 `shopReturnNpcInlineOnShell`（不 `catalog_defer`）。
- **首次** shell loading-clear poll：单独非空 `27/11`，清 inline，保留
  `loading_clear` arm → 后续 poll `30/2` → `busy_ack`。
- 删除末次同包 `27/11+30/2`。

期望日志：

```text
shop_return_npc_inline_on_shell … evidence=early-shell-27/11-before-30/2
mock_shop_return_npc_early_shell … evidence=27/11-before-shell-30/2-like-type27-inject
shop_return_npc_inline_early …
mock_shop_return_loading_clear … remaining↓→0
shop_return_busy_ack …
# 无 catalog_deliver / post_catalog / shell-inline-27/11+30/2
# 单次加载；NPC 可见；可走
```

## 续 5：NPC 可见但遇怪不进战斗（2026-08-05）

### 现象

早期 shell `27/11` 后 NPC 正常；踩怪不进战。**进商城前同局遇怪正常**。

### 根因链

1. mmShop→mmGame 丢失 SCE type4 战斗 mapctrl（slot64）。
2. kind-2 `30/1`：`pass_first` 放行入口，但 `screen_mgr` 仍
   `same-suppressed-dream-reenter`（same-reenter guard 未武装）→ EnterScene
   未真正换屏/ParseMinfo。
3. HAS_FOLLOWUP lone `4/10`（empty primary）：`flags=2 followup=113` 投递两次，
   客户端继续 moveinfo — **再证伪**。

### 修改

**客户端**（`network-client.c` + `main.c`）：

- remote `30/1` 且目标 = 已完成的同名 `29*` →
  `allow_dream_shop_return_reenter=1`（**不** block_dream）。
- **续修**：须在清 `completed` **之前**快照判定；先前先清再判导致
  `alreadyOnSameDream` 恒假 → 永远 `block_dream`，服务端已下 `30/1` 客户端仍无
  EnterScene。
- `screen_mgr`：该 allow-once 接受一次同场景换屏（含 guard 未武装分支）。
- 使者首进仍 `block_dream_reenter=1`。

**服务端**：梦境 shell/moveinfo 后再武装 kind-2 `via=dream-battle-mapctrl`；
`NeedNonSceneBattle`/4/10 仅作残留 fallback。

期望：

```text
# 客户端
remote_scene_target_apply ... allow_dream_shop_return_reenter=1 block_dream_reenter=0
screen_mgr dream-shop-return-reenter-allowed | same-reenter-allowed-once-dream-shop-return
# 无 same-suppressed-dream-reenter

# 服务端
shop_return_kind2_reenter_arm … via=dream-battle-mapctrl
shop_return_scene_enter … 30/1
mock_challenge_battle_start … subtype=5 → 进 mmBattle
```

## 续 6：出店连闪三次加载，第三次卡条（2026-08-05）


### 现象

梦境退商城：场景闪两次后第三次停在「正在加载中」。

### 日志证据（`lxh001` / `69342d8f`）

```text
shop_return_npc_inline_on_shell … warm=0
mock_shop_return_npc_early_shell … 27/11
# WT18/7 资源风暴
mock_scene_entry_local_fallback … pos=(256,160) ×4   ← SCE safe-spawn
shop_return_loading_clear_cancel … remaining=5 reason=scene-target-remember
scene_pending … 29梦境空间_01 pos=(256,160)
builtin-scene-change / mmgame-scene-transfer-followup
dream-25/5-no-second-enter
session_reuse … idle-timeout
# 无 mock_shop_return_loading_clear remaining↓→0
```

### 根因

1. **触发**：early-shell 非空 `27/11` 后客户端同场景再发 WT2/3（exit=0）。
2. **违反契约**：shell `30/2`（`post_catalog=0`）在 `scene-target-remember` 被
   **cancel**（临安仅 hold 了 post-catalog）；目标落到 SCE spawn `(256,160)`，
   再走完整 `mmgame-transfer-followup` → 第三次 EnterScene。
3. **首个错误状态**：clear 被取消后无 ResetDownloadState drip；dream `25/5`
   只回 result=4。
4. **证据**：`local_fallback` + `loading_clear_cancel remaining=5` + 无 clear 收尾。

### 修改

| 点 | 行为 |
|---|---|
| `mark_scene_pending` | 同场景 shop-return clear（shell/post-catalog）一律 hold；同场景不撤 kind-2 |
| `get_scene_change_target` | clear pending 时 inherit 角色/moveinfo 坐标，禁止 SCE spawn |
| `scene_change_combo` | shell 同场景 2/3 → `alreadyTarget` ack，**不** remember pending |
| clear builder | shell 同场景 transfer noise → hold，不 cancel |

期望：

```text
mock_scene_target_inherit_shop_return … pos=(live) …
mock_scene_change_shop_return_same_scene_ack … action=no-pending-transfer
shop_return_loading_clear_hold … reason=same-scene-shop-return-2/3|shell-same-scene-2/3
mock_shop_return_loading_clear … remaining↓→0
# 无 local_fallback (256,160) / loading_clear_cancel / mmgame-transfer 第三次进图
```

## 续 7：跳过 early-shell 二次加载，NPC 跟 kind-2（2026-08-05）

用户契约：不要第二次（early `27/11`）加载；保留 kind-2 EnterScene，并在其上显示 NPC。

| 点 | 行为 |
|---|---|
| 梦境 rehydrate | 不设 `shopReturnNpcInlineOnShell`（`shop_return_npc_defer_after_kind2`） |
| kind-2 `30/1` | 武装 map-stone clear；`catalogPending` + `readyTick=0` |
| type27 / clear 中 | **禁止**非空 `27/11`（空 gate） |
| map-stone `remaining=0` | `catalog_ready` → 暖同包 `27/11+30/2` |

期望：

```text
shop_return_npc_defer_after_kind2 …
shop_return_kind2_reenter_arm … via=dream-battle-mapctrl
shop_return_npc_defer_until_shell_ready …
shop_return_scene_enter … 30/1
# map_stone_loading_clear remaining↓→0；期间 type27 hold / 无 nonempty 27/11
shop_return_npc_catalog_ready … via=kind2-shell-30/2-done
mock_shop_return_npc_catalog_deliver … completion=same-packet-30/2
# 进度条先关，再出 NPC
```

## 续 8：kind-2 第二次条卡住（过早 mark_completed）（2026-08-05）

### 根因

1. kind-2 `30/1` 交付时立刻 `mark_completed` + `mark_scene_ready`。
2. 梦境 map-stone clear 对 `target_valid` 有豁免 → poll `30/2` 在 ScreenInit
   前就把 remote target 关掉（与 coolmall waitMap 同类）。
3. 首个错误状态：第二次 DoLoading 半开；NPC hold 也走不到 `remaining=0`。

### 修改

- 梦境 kind-2：`remember` 目标，**不**提前 mark_completed。
- kind-2 shell clear：hold `target_valid`（覆盖梦境豁免）；2s settle 或
  same-scene `2/3` ack 后放行 drip。
- same-scene `2/3` / inherit 覆盖 map-stone kind-2 shell（防 SCE 出生点）。

期望：

```text
shop_return_kind2_hold_mark_completed …
# 无过早 map_stone remaining↓；可有 kind2_target_settle 或 same_scene_ack kind2_shell=1
mock_map_stone_loading_clear … remaining→0
shop_return_npc_catalog_ready … via=kind2-shell-30/2-done
```

## 续 9：mmgame followup 中途非空 27/11（2026-08-05）

### 日志（`24e22404` / `lxh001`）

```text
shop_return_kind2_hold_mark_completed …
mock_scene_npc_type27_hold … kind2-shell-clear-pending   ← 正确
mock_scene_npc_seed phase=mmgame-scene-transfer-followup npcnum=2  ← 违契
mock_map_stone_loading_clear … remaining→0
catalog_deliver … same-packet-30/2
session_reuse … idle-timeout   ← 条仍卡
```

### 根因

kind-2 `remember` 后客户端发 `mmgame-scene-transfer-followup`；该路径无视
type27 hold，立即非空 `27/11` + 资源族，在 map-stone 清条中途再开 DoLoading。

### 修改

mmgame followup 在 kind-2 shell clear 窗口只回空 `27/11` + `30/2`，NPC 仍等
`remaining=0` 的 poll catalog。

期望：

```text
mock_mmgame_scene_transfer_hold … reason=kind2-shell-clear-pending
# 无 mmgame-scene-transfer-followup nonempty seed
mock_map_stone_loading_clear … remaining→0
catalog_deliver … same-packet-30/2
```

## 续 10：梦境对齐野外暖单次加载（2026-08-05）

用户：参考野外退出商店。野外暖 NPC 图契约
（`2026-08-01-shop-return-warm-type27-single-load.md`）：

```text
early-shell 非空 27/11 → shell poll 30/2 → busy_ack
# 有 NPC 不武装 kind-2 30/1
```

梦境此前 `defer_after_kind2` + `dream-battle-mapctrl` 故意二次 EnterScene，
与野外相反，且二次条持续卡住。

### 修改

- rehydrate：梦境与暖户外一样设 `shopReturnNpcInlineOnShell`
- shell clear / moveinfo：仅 `npcnum==0` 武装 kind-2；梦境有 NPC 时
  `shop_return_kind2_skip … match-outdoor-warm-single-load`

期望：

```text
shop_return_npc_inline_on_shell … dream=1 …
mock_shop_return_npc_early_shell … npcnum=2
shop_return_loading_clear … remaining→0
shop_return_kind2_skip … match-outdoor-warm-single-load
shop_return_busy_ack
# 无 shop_return_scene_enter / kind2 30/1；无二次进度条
```

`unresolved`：跳过 kind-2 后 SCE type4 战斗 mapctrl 是否仍可用（遇怪），
需复测；若软失败再另取证，不以二次 EnterScene 换战斗。

## 续 11：进本 vitals 抢 4/1 + 退店结算卡条（2026-08-05）

见 `docs/re/2026-08-05-dream-vitals-steal-challenge-and-settle-bar.md`。

- challenge 检测忽略兄弟 object；wt-dispatch 禁抢 kind==4
- kind2_skip 后 settlement_exit 用 **4/8-only**（禁止 type=0；事后 30/2 无效）

## 续 12：kind2_skip 后右上角倒计时消失（2026-08-05）

### 现象

退店暖单次加载 + 战斗/结算正常后，梦境右上角实例倒计时不再显示。

### 根因

1. **触发**：`29*` 出商城走 outdoor warm：`kind2_skip`，无同场景 `30/1`。
2. **违反契约**：实例 timer 是客户端本地控件（进本无 WT timer 包）；旧 kind-2
   `ScreenInit` 会重建该控件（旁证：出店常 `L_POKE`）。跳过 EnterScene 后
   mmShop→mmGame 暖壳不重建 timer → HUD 空白。
3. **首个错误状态**：shell/`busy_ack` 完成后无 `timer_ctor` / 无再次 paint poke。
4. **证据**：契约见 `2026-08-03-instance-hud-countdown.md`；续 10 明确 NPC 图
   不武装 kind-2。

已排除：用 host 伪造控件 / 写 CBE 全局当解法（技能禁止）。

### 修改

| 点 | 行为 |
|---|---|
| `note_dream_shop_return_kind2_skip` | 另置 `shopReturnDreamNeedHudRebuild` |
| `busy_ack` 26/0 投递后 | 对 `29*` 武装 `via=dream-hud-timer-rebuild` 延迟 `30/1` |
| `try_deliver` | `shopReturnWarmNpcDelivered=1` → **不**再开 cold `27/11`；hold mark_completed + `shopReturnDreamHudMapStoneHold` 等 ScreenInit 后再 drip map-stone `30/2` |
| type27 / mmgame followup | HudMapStoneHold 窗口只空 `27/11`（同 kind-2 shell hold） |

期望日志：

```text
shop_return_kind2_skip … match-outdoor-warm-single-load
shop_return_dream_hud_rebuild_arm …
shop_return_busy_ack …
shop_return_dream_hud_rebuild_kind2 … evidence=busy_ack-then-deferred-30/1-timer-ScreenInit
shop_return_kind2_reenter_arm … via=dream-hud-timer-rebuild
shop_return_scene_enter … 30/1
shop_return_kind2_hold_mark_completed … hud_rebuild=1
mock_map_stone_loading_clear … remaining→0
shop_return_dream_hud_map_stone_done …
# 客户端：allow_dream_shop_return_reenter + dream-shop-return-reenter-allowed
#         timer_ctor / L_POKE；无 cold catalog_deliver / 无二次卡死
```

`unresolved`：map-stone clear 仍可能闪一下进度条（为关 EnterScene DoLoading）；
若用户不可接受再收 remaining 或另寻无 EnterScene 的 HUD 契约。

## 续 13：HUD rebuild 30/1 破坏暖单次加载（2026-08-05）

### 现象（`588cdfc4`）

梦境退商城：early-shell `27/11` + shell clear 后本应结束，却又
`dream-hud-timer-rebuild` → `shop_return_scene_enter 30/1` → 第三次资源风暴，
最后卡死至 `idle-timeout`。

### 根因

1. **触发**：续 10 `kind2_skip`（有 NPC，对齐野外暖）。
2. **违反契约**：续 12 在 `busy_ack` 后再武装 deferred kind-2 `30/1` 只为重建
   右上角倒计时；与「暖路径禁止二次 EnterScene」冲突。
3. **首个错误状态**：第三次 DoLoading / map-stone 半开；用户感知多次加载后卡死。
4. **证据**：`kind2_skip` → `hud_rebuild_arm` → `kind2_reenter_arm via=dream-hud-timer-rebuild`
   → `shop_return_scene_enter` → WT18/7 ×N → idle。

### 修正

- `note_dream_shop_return_kind2_skip`：只武装 settle `4/8-only`；**不**置
  `NeedHudRebuild`；`Kind2Completed=1` 防后续误武装。
- `busy_ack`：去掉 `dream-hud-timer-rebuild` 30/1。
- 实例倒计时无 EnterScene 的契约仍 `unresolved`（客户端本地 ctor，无 WT）。

期望：

```text
shop_return_npc_early_shell … 27/11
shop_return_loading_clear … remaining→0
shop_return_kind2_skip … match-outdoor-warm-single-load
shop_return_dream_hud_rebuild_skip … warm-single-load-forbids-deferred-30/1
shop_return_busy_ack
# 无 shop_return_scene_enter / kind2_reenter_arm via=dream-hud-timer-rebuild
# 无第三次 WT18/7；NPC 可见；可走
```

## 续 14：busy_ack 后 startup-poll 第三次 27/11（2026-08-05）

### 现象（`8869ec4e`）

续 13 路径已出现（`hud_rebuild_skip` + `kind2_skip` + `busy_ack`，甚至有
moveinfo），约两次加载后仍卡住：

```text
shop_return_npc_early_shell … 27/11
… WT18/7 ×N …
shop_return_loading_clear … remaining=0
shop_return_kind2_skip / hud_rebuild_skip
shop_return_busy_ack
mock_scene_npc_seed phase=startup-scene-sync-poll … npcnum=2   ← 第三次
session_reuse … idle-timeout
```

### 根因

1. **触发**：early-shell 已按户外暖契约投过 nonempty `27/11`。
2. **违反契约**：NPC once-seed / warm 捕获依赖进程全局
   `g_vm_net_mock_scene_moveinfo_npc_seeded`；shop-open 清全局、后续
   `mark_pending` 再清，startup poll 以为目录未投而再发 nonempty `27/11`，
   无 trailing `30/2` → 第三条卡住。全局也不隔离多客户端。
3. **首个错误状态**：`startup-scene-sync-poll` nonempty catalog。
4. **证据**：early-shell → busy_ack 后仍 `startup-scene-sync-poll`；无新
   EnterScene。

### 修正（每客户端会话账本，不用全局做裁决）

| 点 | 行为 |
|---|---|
| `sceneNpcCatalogSeeded` + scene | 本客户端本场景 nonempty 27/11 已投 |
| shop-open warm | 只读会话账本；**不再**清进程全局 seeded |
| shop-return force | 直接 nonempty；记会话账本；不先清全局 |
| once_or_empty | 会话已投 → empty-11（优先于全局） |
| startup poll | `shopReturnWarmNpcDelivered` → hold，不投第三条 |
| 跨场景 pending | 清本客户端会话账本 |

期望：

```text
shop_return_resources_warm_capture … evidence=per-client-sceneNpcCatalogSeeded
shop_return_npc_early_shell … 27/11
shop_return_loading_clear … remaining→0
shop_return_kind2_skip / hud_rebuild_skip
shop_return_busy_ack
mock_scene_npc_poll_hold … reason=shop-return-warm-per-client
# 无 startup-scene-sync-poll nonempty；无 idle-timeout；NPC+可走
```

## 续 15：场景正常但 NPC 不刷新（2026-08-05）

### 现象

续 14 后加载不再卡死，但出商城后 type-21 NPC 不出现。

### 根因

1. **触发**：shop-open 捕获 warm 后**保留**会话 `sceneNpcCatalogSeeded`。
2. **违反契约**：mmShop 已清客户端 type-21；`once_or_empty` 仍按「已投」回
   empty-11（且曾 OR 进程全局 seeded）。type27 先 empty 确认空目录；early-shell
   / poll-hold 后无有效 nonempty 挂上。
3. **首个错误状态**：shop-return 窗口内第一条 `27/11` 为空。
4. **证据**：用户「场景正常、NPC 不刷新」+ 续 14 hold 去掉第三条补投。

### 修正

| 点 | 行为 |
|---|---|
| shop-open | warm 捕获后**清本客户端** ledger（mmShop 清 type-21） |
| once_or_empty | 有 session 时**只**看会话 ledger，不看全局 |
| note_seeded | 在 shop-return clear/inline/catalog 窗口内顺带置 `WarmNpcDelivered` |
| early-shell | 若会话已再投过则跳过，避免二次 nonempty |

期望：type27 或 early-shell 一条 nonempty `27/11` + shell `30/2`；NPC 可见；无第三条。

## 续 16：野外退商城闪退（关联，不改梦境）

野外空图退出时 `5/10` 同包嵌 `27/4`，type27 抢答 empty-11 跳过 rehydrate。
见 `2026-08-05-outdoor-shop-exit-type27-steals-5-10.md`（type27 遇 `5/10` 让路
给 group-type1）。
