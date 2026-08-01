# 梦境再进：首次 DF ScreenInit 崩（户外 clear 半开）

## 现象（2026-07-31）

登录已在梦境 → 出口临安 → 使者再进：场景未显示即崩。

```text
remote_scene_target_apply ... block_dream_reenter=1
df_do_loading ... caller=0518c7c8 init=518c525
地址无法访问:3156ac4  r6=01ffffff  lr=010136b3  pc=20fe860
栈附近 ui_h_war.actor
SCR_Init异常 / UC_ERR_MAP
```

服务端停在 `mock_npc_instance_enter ... 30/1`，客户端未发到 `2/3`。
**不是** `enter_scene_skip` / `pc=0` 路径（那些发生在首次 ScreenInit Ok 之后）。

同会话登录梦境 `init=502eba5` 正常；磁盘 SCE 名牌为「幽冥鬼火」（非名牌崩）。

## 根因

1. 梦境→临安走地图石式 `30/1` + `2/3 27/12+posinfo`，武装 `map_stone_loading_clear`（poll `30/2`×3）。
2. 玩家在 clear 未耗尽时点使者：`scene-target-remember` → `map_stone_loading_clear_cancel remaining=1`，**未再向客户端补发**剩余 `ResetDownloadState`。
3. 同包立即下发梦境 `30/1`，热 DF 加载 FB+kind3 壳（`518c*`，与登录冷加载 `502e*` 不同分配）。
4. 半开 download/DF 生命周期下 `ParseMinfo`/actor 回调槽损坏 → `blx` 垃圾（与二次 ScreenInit / 名牌崩同 `lr=0x010136b3` 族）。

首个错误状态：在户外 clear 未排空时取消 arm 并立刻热进梦境 DF ScreenInit。

## 修复（协议层）

[`src/server/mock_server_scene_sync.c`](../../src/server/mock_server_scene_sync.c) `vm_net_mock_build_instance_enter_response`：

- 若 session 仍有**不同场景**的 `mapStoneLoadingClear` 且 `remaining>0`：
  1. 本响应先下发剩余次数的 `30/2-no-posinfo`（排空 `ResetDownloadState`）
  2. cancel clear
  3. 武装既有 `teleport_stone_deferred_enter`，下一 poll tick 再发梦境 `30/1`
- 无残留 clear 时保持原立即 `30/1` 路径

日志：`mock_npc_instance_enter_deferred ... drain_30_2=N`，随后 `mock_teleport_stone_deferred_enter`。

客户端既有梦境 `01018150` 入口跳过 / `return_screen` 仍负责二次 EnterScene；本修复不扩展 host shim。

## 续记：defer-via-poll 证伪（同日）

排空后将 `30/1` 延到 `scene_sync_poll` 仍崩：

```text
mock_npc_instance_enter_deferred ... drain_30_2=1
mock_teleport_stone_deferred_enter ... resp=52
# 客户端仍首次 ScreenInit：r6=0 lr=010136b3 pc=0
```

**修正**：同包 `30/2-drain + 30/1`（保持 NPC **service 回调上下文**进图）；客户端 observation 在同包已有 `30/1+posinfo` 时忽略 drain `30/2` 的 clear-after。

## 验证

1. `make -j2`；重启 `jh-online-server` 与桌面客户端（observation 改动在 `network-client.c`）。
2. 登录梦境 → 尽快出口临安 → 立刻点使者再进。
3. 期望服务端：
   - `mock_npc_instance_enter ... drain_30_2≥1 ... same-packet-ResetDownloadState-then-30/1`
   - **无** `mock_npc_instance_enter_deferred` / poll 单独 `30/1`
4. 期望客户端：`remote_scene_target_apply ... block_dream_reenter=1`（不要被同包 30/2 clear 掉）；首次 `ScreenInit Ok`；completion 时 `enter_scene_skip_dream_completion ... return_screen=...`。
5. 回归：clear 已耗尽后再进应走 `mock_npc_instance_enter_dream_drain`（合成 outdoor `30/2`）；户外地图石不变。

## 续记：clear 已耗尽后再进仍卡 / pc=0（同日）

复现：临安 poll `map_stone_loading_clear remaining→0` 后再点使者。

```text
mock_npc_instance_enter ... response=30/1 resp=52   # 无 drain
… DF ScreenInit Ok …
queue_data resp=197                                 # WT 2/3
enter_scene_skip_dream_completion ... return_screen=01053f78
地址无法访问:0  r0:0  pc:0  lr:104d775
```

对比成功路径（clear 未耗尽）：同包 outdoor `30/2` + 梦境 `30/1`（`resp=102`）后客户端走
type27/WT6，**不发** 2/3，也就不经过 `01018150` skip。

**根因**：drain 只在 `mapStoneLoadingClearRemaining>0` 时武装；clear 耗尽后的纯 `30/1`
让客户端回到 2/3→EnterScene 半开路径。host `return_screen` 仍未能阻止
`lr=0x0104d775` 处 `r0=0`（shim 不足依赖）。

**修正**：目标为 `29*` 且仍无 drain、且 `sceneVisibleScene` 为不同户外场景时，
合成一次 outdoor `30/2-no-posinfo` 再发梦境 `30/1`（日志
`mock_npc_instance_enter_dream_drain`）。包形对齐成功路径的
`ResetDownloadState-then-30/1`。

验证：clear 耗尽后再进应见 `dream_drain` + `drain_30_2=1`；客户端无 2/3 /
`enter_scene_skip` / `pc=0`；可走。

## 续记：HAS_FOLLOWUP 拆帧证伪（同日，用户「还不如上次」）

尝试把 outdoor `30/2` 与梦境 `30/1` 拆成 primary+followup：

```text
response=30/2-drain-only ... flags=2 followup=52
queue_data resp=55
queue_data_followup resp=52
# 无 remote_scene_target_apply / block_dream_reenter
DF init=518c525 ScreenInit → r6=0 pc=0 lr=010136b3
```

根因：`network-client.c` 只对 primary CBMR 做 `capture_remote_observation` +
`attach`；followup 仅 `scheduler_queue_net_event`，**不挂** observation。
仓库/挑战 followup（对话/战斗包）不需要 scene-target 门闩；梦境 `30/1` 需要
`block_dream_reenter=1`，拆帧后该门闩丢失，比同包更差。

**回退**：恢复同包 `30/2-drain + 30/1`（`same-packet-ResetDownloadState-then-30/1` /
`dream_drain`）。同包 observation 已有 `hasEnterWithPosinfo` 时忽略 drain
`30/2` clear-after。

## 续记：plain 30/1 ScreenInit Ok 后 2/3 skip 仍崩（用户「卡住」）

去掉合成 `dream_drain` 后：

```text
resp=52 plain 30/1 → ScreenInit Ok
resp=197 2/3 → enter_scene_skip return_screen=01053f78
地址无法访问 r0=0 lr=0104d775 pc=0
```

`return_screen` 不足以满足 `0x0104d775` 调用约定。

**梦境专用协议修复**：instance 直进后的 WT 2/3 只回 `30/2-no-posinfo`
（`mock_dream_instance_2_3_loading_clear_only`），**不下发** `27/12` 族，避免
再调 `EnterSceneByMapName`。户外地图石 2/3 不变。NPC 仍 `wait_wt6`。

客户端：dream 目标 clear-after 后清掉 `block_dream_completion_reenter`。

## 续记：clear 耗尽后 plain 30/1 再崩首次 ScreenInit（同日晚）

```text
map_stone_loading_clear remaining=0
mock_npc_instance_enter ... 30/1 resp=52   # 无 drain
DF init=518c525 → r6=0 lr=010136b3 ui_h_war.actor → SCR_Init异常
```

与「半开 clear」同族，但 arm 已耗尽。先前去掉合成 `dream_drain` 后偶发
`ScreenInit Ok`，本次复现热加载 kind3 仍崩。

**再开梦境专用**：目标 `29*` 且 `sceneVisibleScene` 为不同户外图时，即使
clear 已尽，仍合成 1× outdoor `30/2` + 梦境 `30/1`（
`mock_npc_instance_enter_dream_drain`）。户外→户外不变。

同时撤回梦境 `2/3` 的 `27/12+posinfo`（改回 `27/12-ack`），避免二次
EnterScene / skip 路径。

## 续记：门闩等 remaining=0（同日晚，最稳）

同包 drain 在 `remaining=1` 时仍复现 `518c*` / `r6=1ffffff` / `ui_h_war`。
成功样本是 clear 自然到 `remaining=0` 后再点使者 + `dream_drain`。

**协议门闩**（不再半开时硬进）：

1. 目标 `29*` 且 outdoor `mapStoneLoadingClear` 仍 `remaining>0`：
   - **不** cancel clear、**不**发 `30/1`
   - NPC 对话提示「地图加载中，请稍候再进入。」并保留「进入副本」选项
   - 系统频道同文案；日志 `mock_npc_instance_enter_gate`
2. `remaining=0` 后再点：走既有 `dream_drain`（合成 outdoor `30/2` + 梦境 `30/1`）
3. builder 内二次拒绝，防止其它路径绕过门闩

验证：出口临安后立刻点使者 → 见 gate / 不崩 / 仍在临安；等
`map_stone_loading_clear remaining=0` 后再点 → `dream_drain` 进图可走。

## 相关

- `2026-07-31-dream-enter-remote-reenter-crash.md`（二次 ScreenInit）
- `2026-07-31-dream-reenter-loading-stall.md`（skip + return_screen / 二次 enter）
- `2026-07-31-dream-remap-encounter-crash.md`（真遇怪战斗包）
- `2026-07-27-teleport-mapstone-async-loading-clear.md`
