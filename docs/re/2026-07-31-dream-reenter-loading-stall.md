# 梦境再进：场景已显示但进度条卡住

## 现象（2026-07-31）

使者再进 `29梦境空间_01`：DF `ScreenInit Ok` 后进度条不消。可复现客户端序：

```text
remote_scene_target_apply ... block_dream_reenter=1
df_do_loading ... ScreenInit Ok
queue_data resp=197   # 2/3
remote_scene_target_complete_pending ... WT30/2
download_busy_clear reason=same-suppressed-dream-reenter
queue_scene_poll resp=56 ×3
queue_scene_poll resp=77
# 进度条仍在
```

服务端已 `defer_npc=1`、`wait_wt6` + `completion=30/2-no-posinfo`、poll clear 耗尽。

## 根因

与户外 `same-suppressed` 同类（`2026-07-31-mapstone-same-reenter-loading-stall.md`）：

1. 实例 `30/1` 已走 DF ScreenInit（caller≠`01018150`），壳已就绪。
2. `2/3` 仍调用 `EnterSceneByMapName(0x01018150)`（即便 `27/12-ack`）。
3. 在 **screen_mgr** 才拒绝换屏：此时 EnterScene **已武装** busy/DF；`download_busy_clear` + poll `30/2` **不足以**补上被拒的完成态。
4. 户外靠 `allow-once` 放行二次 EnterScene；梦境二次 ScreenInit 会崩（`pc→0x4ad5542`），不能放行。

首个错误状态：在已武装 busy 之后才 suppress 梦境 completion EnterScene。

## 错误尝试

| 尝试 | 结果 |
|------|------|
| `29*` 在 `2/3` 提前 nonempty `27/11`（`defer_npc=0`） | 崩溃风险，已回退 |
| wait_wt6 同包 `30/2` | 服务端已下发；客户端仍卡（根因在 EnterScene 半开） |
| screen_mgr suppress + `download_busy_clear` | 能防崩，清不掉条 |

## 修复

客户端（`CBE_CLIENT_ONLY`）在 **`0x01018150` 函数入口**、武装 busy 之前：

- `g_vm_scene_block_dream_completion_reenter` 且目标为 remote `29*` → `bx lr` 直接返回
- 日志：`enter_scene_skip_dream_completion`
- screen_mgr `same-suppressed-dream-reenter` 仅作兜底

文件：[`src/main.c`](../../src/main.c)、[`JianghuOL/.../cbeEmu/main.c`](../../JianghuOL/app/src/main/jni/src/cbeEmu/main.c)。

服务端仍：`27/12-ack` + wait_wt6；`29*` wait_wt6 follow-up 可带同包 `30/2`（备份）。户外 allow-once 不变。

## 续记：入口跳过返回 r0=0 崩（同日）

`enter_scene_skip_dream_completion` 已命中，但随后：

```text
地址无法访问:0 ... pc:0 lastPc:104d77e
```

**根因**：`EnterSceneByMapName` 调用方把返回值当 screen/object 指针用；入口 `bx lr` 未写 `r0`，原参或 0 被当作返回值 → `blx 0`。

**修复**：跳过时 `vm_set_call_result(vmAddedScreen)`（否则 `g_currentScreenThis`）；无可用 screen 则 fallthrough 到 screen_mgr suppress。

## 验证

1. `make -j2`；重启桌面客户端 + `jh-online-server`（Android 需重编 APK）。
2. 登录已在梦境 → 出口临安 → **等进度条/clear 耗尽后再**点使者再进（覆盖无 pending clear）。
3. 期望服务端：`mock_npc_instance_enter_dream_drain` + `same-packet-ResetDownloadState-then-30/1`。
4. 期望客户端：
   - **无** `enter_scene_skip_dream_completion`（避免 2/3 路径）
   - **无** `pc=0` / `地址无法访问:0`
   - **无**二次 ScreenInit / `pc=4ad5542`
   - 进度条消失、可走
5. 回归：户外地图石仍 `same-reenter-allowed-once`；clear 未耗尽时仍走 pending-clear drain。

## 续记：场景已显示后又二次加载卡住（同日晚）

plain `30/1` ScreenInit Ok 后，客户端短包 `25/5` 命中
`mock_teleport_stone_direct_enter_ack`，旧实现调用
`build_scene_default_event_response` → **再塞一个 scene-enter**（`resp≈449`）→
二次加载。梦境 short `25/5` 改为仅 `result=4`（户外不变）。

## 续记：loading_clear_only 证伪（同日）

`mock_dream_instance_2_3_loading_clear_only`（仅 `30/2`）在 DF
`ScreenInit Ok` 后仍卡进度条：户外对照靠 `2/3` 的 `27/12+posinfo` 触发
`EnterScene`；裸 `30/2` 不够。且 `wait_wt6` 无 type27/WT6，NPC 也不到。

**修正**：去掉 `loading_clear_only` 短路；梦境 `2/3` 与户外一样发
`27/12+posinfo+27-family+30/2`。二次 `EnterScene` 由客户端
`enter_scene_skip_dream_completion`（非空 `return_screen` +
`download_busy_clear`）拦截，避免二次 ScreenInit。

期望日志：

```text
mock_npc_instance_enter ... 30/1
mock_teleport_stone_direct_enter_ack ... dream-short-25/5-no-second-enter  # 若有短包
mock_teleport_stone_current_scene_complete ... 27/12+posinfo+...+30/2
enter_scene_skip_dream_completion ... return_screen=...
# 无二次 ScreenInit / pc=4ad5542；进度条消失；可走
```

## 相关

- `2026-07-31-dream-enter-remote-reenter-crash.md`
- `2026-07-31-dream-reenter-first-screeninit-crash.md`（首次 DF ScreenInit / clear 半开 / 耗尽后再进）
- `2026-07-31-mapstone-same-reenter-loading-stall.md`
