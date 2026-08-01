# 梦境进图 ScreenInit 崩（远程客户端缺场景观察）

## 现象（2026-07-31）

从临安 NPC 进 `29梦境空间_01`：资源全部 `uptodate`（SCE 名牌已是「幽冥鬼火」、version=9738），首次 `ScreenInit Ok`，随后 `resp=215`（`2/3` 地图石完成包），再次 `caller=01018150` ScreenInit →

```text
pc=0x4ad5542  lr=0x010136b3  lastPc=0x010136b0
blx r6  （Thumb，FB/场景 actor 回调槽被垃圾指针）
栈附近 ui_h_war.actor
SCR_Init异常 / UC_ERR_MAP
```

磁盘 `web/fs` 与 `bin/JHOnlineData` 的 `29梦境空间_01` **不含「梦魇」**，与名牌崩（`2026-07-31-dream-custom-nameplate-screeninit-crash.md`）同 PC，但是**二次 EnterScene** 路径。

## 根因

1. `server-only` + `CBE_CLIENT_ONLY`：客户端用 `network-client.c`，原先 `vm_net_mock_apply_remote_observation` / `consume_update_completed_scene_reenter` 为空实现。
2. 实例进入下发 `30/1 {scene,posinfo}` 后，客户端本地**没有**同场景保护目标。
3. `mock_teleport_stone_current_scene_complete` 的 `27/12+posinfo` 再调 `EnterSceneByMapName(0x01018150)`，`screen_mgr` 无法 `same-suppressed`，对已初始化的 mmGame **再跑一遍 ScreenInit**。
4. 梦境 SCE 含 6 个 FB 装饰 + kind=3 战斗点时，二次 init 易把 actor 回调表打坏 → `blx` 未映射地址（证据与 `2026-07-19-linan-resource-reenter-crash.md` 同类）。

**被违反的契约**：远程客户端必须在 guest 回调前从 WT 包镜像 `30/1`/`30/2` 场景目标，以便抑制同目标二次 `01018150`。

## 修复（第一轮）

在 `src/network-client.c`：

1. 实现与 transport 同语义的 `apply` / `finish` / `consume` 与必要全局。
2. 用六字节 response 对象头解析 `30/1`（scene+posinfo）与 `30/2`。
3. `queue_data` 时 `scheduler_attach_net_remote_observation`，在 `net_fire` 前应用。

服务端对 `29*` 改用 `27/12-ack`（无 posinfo）。

## 续记：使者实例路径 guard 未武装（同日）

复测仍崩：已有 `remote_scene_target_apply` / `allow_map_stone_reenter=0` / `27/12-ack`，首次 `ScreenInit Ok`，但 `2/3` 后仍 `caller=01018150` 且**无** `same-suppressed`。

```text
remote_scene_target_apply ... block_dream_reenter 需武装
# 首次进图 caller=051821c8（DF 加载），不是 01018150
# same-reenter guard 从未 remember
# 2/3 完成包仍调 01018150 → 接受换屏 → pc=4ad5542
```

**补充根因**：`same-suppressed` 依赖「先前经 `01018150` 的 remember」。使者实例进入第一次 ScreenInit 走 DF 包加载，guard 空；仅有 remote observation 不够。

**补充修改**（仅 `29*`，不改户外 allow-once）：

1. [`src/network-client.c`](../../src/network-client.c)：remote `30/1` 且场景以 `29` 开头 → `g_vm_scene_block_dream_completion_reenter=1`（与 map-stone allow-once 互斥）。
2. [`src/main.c`](../../src/main.c)：`01018150` 且 block 置位且 `activeTarget` 与 remote 场景同名 → `same-suppressed-dream-reenter` + `download_busy_clear`；命中后清 block。
3. Android JNI 同步。

刷怪可见性需进图稳定后再验，不作为本修复完成标准。

续记：抑制二次进图后场景可见但进度条卡住 → 见 `2026-07-31-dream-reenter-loading-stall.md`（在 `01018150` **入口**跳过，勿等 screen_mgr）。

## 验证

1. `make -j2`（客户端 + 服务端）。
2. 重启 `jh-online-server` 与桌面客户端。
3. 临安府_06 → 梦境使者进图。
4. 期望日志含：
   - `remote_scene_target_apply ... block_dream_reenter=1`
   - `screen_mgr same-suppressed-dream-reenter caller=01018150`
   - **无**二次 `SCR_Init异常` / `pc=4ad5542`
5. 回归：户外地图石仍见 `same-reenter-allowed-once`。

## 与名牌崩的关系

名牌「梦魇」仍禁止写入 SCE `0x0F`。本次磁盘已是「幽冥鬼火」；崩因是二次 ScreenInit，不是名牌内容。
