# 梦境再进：clear=0 + dream_drain 仍 518c ScreenInit（2026-08-02）

## 约束

只改 `29*` 副本进出门闩 / dream_drain；户外地图石与其它场景路径不变。

## 触发（debug-191169）

1. 临安进 `29梦境空间_01` 首次 DF `ScreenInit Ok`（`init=0x0502f0a5`）
2. 使者出口回 `c04临安府_06`（instance enter，`allow_once=1`）
3. 再点使者：`dream_drain` + `block_dream=1` + 资源 `uptodate`
4. 首次 DF ScreenInit 崩：

```text
fault=0x3156ac4 pc=0x20fe860 lr=0x010136b3 r6=0x01ffffff
init=0x0518c525  SCR_Init异常 emu_err=11
session_offline visible=c04临安府_06 pending=29梦境空间_01 ready=0
```

## 假设结论

| 假设 | 结论 | 证据 |
|------|------|------|
| A 首次 DF ScreenInit 崩 | **确认** | mem_fault → screen_init_fail，无二次 `01018150` |
| B block_dream 未武装 | **排除** | `block_dream=1` |
| C 名牌/未发布资源 | **排除** | 磁盘「幽冥鬼火」；`uptodate` |
| D dream_drain 包形错误 | **不足** | `drain=1`/`resp=102` 仍崩 |
| E 未 scene_ready | 症状 | offline 仍临安 |

## 根因

离开 `29*` 后户外 clear 已耗尽时，同包 `1×30/2 + 梦境30/1` 仍走热 DF 壳（`518c*`），ParseMinfo/kind3 打坏 actor 回调（`r6=01ffffff`）。  
被违反的契约：梦境再进必须在**户外已 ready 之后**再经历一轮 poll `ResetDownloadState`，不能仅凭 `clear_rem==0` 立刻 `dream_drain`。

## 修改（仅 29*）

1. 离开 `29*`→户外 instance enter：置 `dreamExitReenterSettleRequired`，并 arm 户外 `map_stone_loading_clear`。
2. 再进 `29*`：若 required 且 clear 已尽且尚未 `ArmedOnce` → **再 arm 一轮 clear 并 gate**（提示稍候），不发 `30/1`。
3. settle 耗尽后再点：`dream_drain` 的合成 outdoor `30/2` 改为 **3**（仍同包 + service 上下文）。
4. 成功进入 `29*` 或 session offline：清 settle 标志。

户外→户外 instance、地图石 allow-once、非 29 副本：不读这些标志。

## 续测（同日，settle+drain=3 后）

再进：`drain_30_2=3` 后 `ScreenInit Ok`（`init=518c525` 未再 mem_fault），但随即：

```text
enter_scene_skip_dream_completion return_screen=...
mem_fault fault=0 pc=0 lr=0x0104d775 lastPc=0x0104d77e
```

首个 ScreenInit 已过；偏离点变为 `2/3`→`01018150` skip 后调用约定仍崩（与文档「成功路径不应出现 skip」一致）。

## 补充修改（仍仅 29*）

1. 梦境 instance 的 WT `2/3`：`mock_dream_instance_2_3_loading_clear_only` —— 只回 `30/2-no-posinfo`，`wait_wt6` + arm poll clear + scene_ready；**不下发 27/12 族**（避免 EnterScene/skip）。
2. 客户端 skip 备份：优先 `g_currentScreenThis`（DF `this`），并打 added/current 日志。
3. 户外 `2/3` / 地图石 27/12+posinfo：**未改**。

## 续测：进图成功但进度条卡住（同日）

`loading_clear_only` 避免了 `pc=0`，地图可走，但条不消——未走
`enter_scene_skip`→`download_busy_clear`，且 `wait_wt6` 挡住 poll `30/2`。

## 进度条补充（仍仅 29*）

1. 客户端：DF `ScreenInit Ok` 且 `block_dream` → `download_busy_clear(dream-df-screeninit-complete)`。
2. 梦境 `2/3`：`30/2`×3；NPC 改 `wait_post_enter`（不用 `wait_wt6`）。
3. poll clear：对 `29*` 武装场景不因 `wait_wt6` 再 hold。

## 续测：提示加载中后进图再崩（同日）

settle 后再进：`drain_30_2=3` 仍间歇 `518c` / `r6=01ffffff`（资源 `uptodate` 热解析）。

## 冷拉 SCE（仍仅 29*）

1. 去掉二次 force-settle；离开梦境只 arm 一轮 outdoor clear + gate。
2. 进入 `29*` instance：`dreamForceColdSceResync` —— WT18/7 对 `29*` SCE（非 map/gif/actor）禁止 uptodate，wire version+1 下发 payload，迫使冷 DF（对齐登录 `502*`）。
3. `dream_drain` 恢复为 1× outdoor `30/2`。
4. 进度条：`ScreenInit Ok` + `block_dream` → `download_busy_clear`；`2/3` loading_clear_only 仍保留。

## 续测：无「加载中」直接崩（同日）

`cold_sce=1` 已置位，但进图到崩溃之间**没有** WT18/7：客户端
`uptodateMemo` 在同进程登录进梦境时已记住 `29*`，再进直接跳过网络，
服务端冷拉从未执行；仍热 DF `518c` / `r6=01ffffff`。

## 客户端补丁（仅 29* instance 30/1）— 已证伪后撤回

曾对梦境 `30/1` 清 `uptodateMemo` 并服务端 force payload。再测：

- `dream_memo_forget removed=2` + `dream_force_cold_sce` **已执行**
- 崩溃前**无** `cold_resync_done` / download_complete
- 仍 `init=0x0518c525` / `r6=01ffffff`
- 同会话**首次**进梦境是 `uptodate` + `init=0x0502f0a5` 成功

结论：冷拉 SCE **不是** 502/518 壳差异的根因；强制 payload 与 ScreenInit
竞态，按规范撤回 cold-SCE / memo-forget。

## 当前修复（仍仅 29*）— resume paused login shell

已证伪（debug-191169）：`drain=3` 同包；cold SCE；login_parity `1/1/6`；
deferred poll `30/1`（仍 `this=17121120` + `init=518c525`）。

同会话对照：

| 路径 | this | init | 结果 |
|------|------|------|------|
| 登录进梦境 | 17121120 | `0x0502f0a5` | Ok |
| 出口临安 | 17118264 | `0x0502e5c5` | Ok（dream 仍 PAUSE 在栈下） |
| 使者再进 | 17121120 | `0x0518c525` | mem_fault / SCR_Init异常 |

根因：户外→`29*` 热 `DoLoading` 在同一 `this` 上换成 `518c*` Init，违反
PAUSE→Resume 契约。

### 协议（server）

1. 离开 `29*`：arm 户外 clear + gate。
2. clear 尽后再进：同包 `1× outdoor 30/2` + 梦境 `30/1`（`dream_drain`）。
3. 梦境 `2/3`：`loading_clear_only`（无 27/12）。

### 客户端（仅 29*）

1. 冷 DF Init（`0x0502*` + SCE/target `29*`）成功 → `dream_resumable_shell_remember`。
2. 户外 Init 且 dream 仍在栈 → `dream_resumable_shell_paused`。
3. 再进 `30/1`：`resume_paused=1` → 跳过热 `DoLoading`，`ScreenInit` 改 `ScreenResume`。
4. `block_dream` / `download_busy_clear` 仍保留。

## 续测：keep-shell 证伪（同日）

`dream_shell_add_pause stack_idx=1` 成功后，再进未到 DoLoading，在冷壳
`lr=0x0502d743` / `pc=0` 崩（guest 回调直接进 PAUSE 壳）。

| 路径 | 结果 |
|------|------|
| 热 518c ScreenInit | mem_fault |
| 保留 502 壳 + Resume/再入 | pc=0 @ 502d743 |

当前改为：撤回 keep-shell / DoLoading-skip / Resume / hot_init_noop。

## 续测：NPC 响应塞 `1/1/6`（无 30/1）— 已证伪

`login_parity=1` / `actorinfo=239` / `resp=489` 后：

- 客户端提示 **解包错误**，进度条卡住
- 无后续 DF `ScreenInit`（debug-191169 止于 `scene_target_apply subtype=6`）

根因：instance/NPC 进入响应的 `event_packet_init` 业务族期望 `30/x`，不能塞
title `1/1/6`。登录冷壳只能在 **title 选角** 或 **壳已建成后的 poll** 投递。

## 续测：ReleasePackage — 已证伪（同日）

无解包错误，仍闪退：

```text
init=0x0518c525 this=17121120 block_dream=1
mem_fault r6=0x01ffffff lr=0x010136b3
```

全程**无** `dream_df_release_before_loading`（再进路径未走到可释放的
`DoLoading`，或 source 名不匹配）。`ReleasePackage` 撤回。

## 续测：强制冷 Init — 未触发；卡住在 EnterScene skip（同日）

本局无 `dream_shell_remember`（未走登录冷进梦），`dream_force_login_cold_init`
未触发。热 `init=0x05181f25` **ScreenInit Ok**，随后：

```text
enter_scene_skip ... return_screen=17121120
mem_fault fault=0 pc=0 lr=0x0104d775
```

且 **无** `dream_2_3_loading_clear` → skip 拦的是 **30/1 主路径第一次
EnterScene**，不是 2/3 二次进图。DoLoading source=`MMORPG_Resource_Data.bin`
（故先前按 `29*` 名 ReleasePackage 永不会命中）。

## 续测：仅 EnterScene 门闩 — 仍 518c 崩（同日）

有 `dream_shell_remember init=502f`；再进未跑到 `pass_first`，在
`ScreenInit` 热 `0x0518c525` / `lr=0x010136b3` 闪退。门闩来不及生效。

## 续测：强制冷 Init — 已证伪（同日）

`dream_force_login_cold_init` 已触发（hot=`518c525` cold=`502f0a5`），随即：

```text
mem_fault pc=0x0502f0a8 (=cold+3)  screen_init_fail emu_err=11
```

在 DoLoading 打脏后的同一 `this` 上重跑登录 Init **不可行**。

## 续测：skip-DoLoading + Resume — 已证伪（19190 smoke）

自动进出：`dream_shell_remember` → exit → `dream_drain` → `dream_skip_doloading`
（package==shell_dp）后立刻：

```text
mem_fault pc=0x01006232 lr=0x01006231 type=19
stream_data_result caller=0100622e ... result=0
SCR_Render assert
```

全程**无** `dream_login_shell_resume` / ScreenInit。户外时
`dream_shell_paused stack_idx=-1`（登录壳已不在栈上）。

结论：DoLoading 调用方契约不能跳过；Resume/keep-shell 与 skip 一并撤回。

## 续测：allow DoLoading（19190）

`dream_doloading_on_login_dp action=allow` →
`dream_screeninit_on_login_shell hot518=1 init=0x0518c525` →
`mem_fault lr=0x010136b3 r6=0` → `screen_init_fail emu_err=11`。

## 续测：ReleasePackage(name=0) before DoLoading — 已证伪

`dream_release_before_doloading` 后立刻 `mem_fault pc=0x01006232`（与
skip-DoLoading 同族），无 ScreenInit。已撤回。

## 续测：drain=3 — 不足（19190）

`drain_30_2=3` / `resp=202` 仍 `hot518=1`，随即 `hookCpuIntr` assert
（非 `010136b3` mem_fault）。已撤回 drain=3。

## 根因（A/B，后台刷怪点）— 已确认

管理后台「地图刷怪点」会**重写梦境 SCE** 并 publish：

| 文件 | 长度 | kind=3 / ghostfire |
|------|------|-------------------|
| 存量 `bin2/.../29梦境空间_01` | 278 | 无 |
| 后台保存后 `bin|web/.../29梦境空间_01` | 577 | 3× `#200` + `e_ghostfireB/G` + 名牌「幽冥鬼火」 |

MySQL `server_scene_combat_spawns`：`29梦境空间_01` 三槽 `wire=200 real=203`。

A/B（19190 smoke）：

1. **后台 SCE（577）**：再进 `allow_doloading` → `hot518 init=0x0518c525` →
   `mem_fault lr=0x010136b3` → `screen_init_fail`（与名牌/kind3 文档同族）。
2. **恢复存量 SCE（278，无刷怪点）**：再进同样热壳 `init=0x05189d25` 但
   **`screen_init_ok block_dream=1`**，无 mem_fault，进程仍活；随后
   `fresh_shell 1/1/6` 正常。

结论：再进闪退的**首个偏离**是后台注入的 FB kind=3 战斗点；冷登录
`502*` 可吃该 SCE，户外→梦境热 `518*` 再 Init 不能。名牌已是「幽冥鬼火」
（非「梦魇」），问题在 **kind=3 存在本身 + 热再 Init**，不单是自定义名。

后台备份：`tmp_dream_sce_admin_backup/`。

## 当前处置（刷怪 + 再进，2026-08-02 续）

证伪（host）：户外暂停 `ReleasePackage`→`IMG_CreateImageFormRes` assert；
延迟 `Destroy`→`err=11` 且再进仍热 `518c`。

### 使者再进无刷怪（debug-191169，已定位）

| 假设 | 结论 | 证据 |
|------|------|------|
| stock-swap 故意卸 kind=3 | **曾确认** | 离开后 disk=278、`ghost=False`，再进可走但无怪 |
| keep_kind3 后仍无怪 | **确认** | WT18/7 `path=../web/fs/.../29*_01` **chunk=278**；bin `*.sce` 仍 418 含 ghostfire，但权威源是 web/fs |
| 任意 kind=3 热 518 必崩 | **排除** | inject 单点 `(135,49)` size=418：`hot518=1` ScreenInit Ok，无 `mem_fault`；坏坐标 admin Y≈+100 / 三连 577 不稳定 |
| web/bin twin 漂移 | **确认** | 污染 bin=278 后登录：`dream_update_chunk total=418 ghostfire=1`；再进 publish `heal_kind3 healed=1`（补 `.sce` twin）后 `uptodate` 保留 418 |

协议处置（现行）：

1. **权威资源**：`resource_root=../web/fs/JHOnlineData`；扩展名无后缀叶与 `*.sce` 必须同为 hot-safe kind=3。
2. **离开 29***：`keep_kind3=1`（不再默认 `stock_install`）。
3. **publish 前**：`mock_dream_sce_heal_authoritative_kind3` 把含 `e_ghostfire` 的 donor 同步到 web+bin 的 stock twin。
4. **注入**：`scripts/inject_dream_sce_kind3.py` 默认单点 `(65,84)`（418，content-ver≠旧 23971）；三连 inject-v3b 热再进仍 flaky，记 unresolved。
5. 进度条补丁仍有效；冒烟：`alive=True`，登录下发 418+ghostfire，再进 `block_dream=1 hot518=1`，disk 双侧 418 gf。

### 登录闪退回归（同日）

| 假设 | 结论 | 证据 |
|------|------|------|
| catalog 版本漂移 uptodate 错缓存 | **确认（一因）** | 磁盘变更后仍可能打旧 `version`；已改为 prefer disk content-hash |
| 用户冷登录 kind3 ParseMinfo 崩 | **确认** | `screen_init_fail init=0x0502f0a5 emu_err=11` + `mem_fault lr=0x010136b3 type=21`（与 2026-07-31 名牌崩同类） |
| 本机冒烟 kind3 必崩 | **排除** | 同 418 有时 `emu_err=0`——环境/缓存相关，不能据此放行 |

**现行处置（进角色 + 刷怪）**：

1. 已撤回 WT18/7 强制 `serve_stock`（会消灭刷怪）。
2. 保留 catalog **prefer disk content-hash**，避免错误 uptodate。
3. `inject_dream_sce_kind3.py`：单点 `(65,84)` + **华山洞窟同源名牌**
   GBK `c1b6d3fcb9edbbf0`（非「梦魇」、亦非自撰「幽冥鬼火」字符串）。
4. 冒烟：冷登录 `screen_init_ok emu_err=0`，客户端 `418 gf` + cave 名牌；
   ghostfire actor uptodate；无 `mem_fault`。
5. 用户确认「不闪退」后恢复 kind3；若再崩对照名牌/版本戳。

## 续测：存量 SCE 可进但卡进度条（同日）

再进：`screen_init_ok block_dream=1`（无崩），但服务端**无**
`mock_dream_instance_2_3_loading_clear_only` / 梦境 poll `30/2`——
`dream_drain` 路径常跳过 WT 2/3，条只靠 host busy-clear 不够。

### 进度条补丁（仍仅 29* drain 再进）

1. `dream_drain` 进 `29*`：`arm_map_stone_loading_clear(dream)`。
2. **证伪补记**：仅 arm 不够——`build_pending_map_stone_loading_clear`
   因 `direct_enter_pending` + `last_scene_change_target_valid` 一直
   `return 0`，断线时仍 `clear_rem=3`，无任何 `mock_map_stone_loading_clear
   scene=29*`。
3. 放开：清条场景为 `29*` 时不因上述两旗标 hold；`fresh_shell`/vitals
   在梦境 clear 未耗尽前让路。

## 验证

1. `web/fs` 与 `bin` 的 `29梦境空间_01`（无后缀与 `.sce`）均为含
   `e_ghostfire` 的 hot-safe kind=3（当前注入 418 / `(135,49)`）。
2. 端口 **19190**，第一角色：进梦境（应见幽冥鬼火）→ 使者出口 → 再进
   应仍见怪、进度条消失、不闪退。
3. 日志期望：离开 `keep_kind3=1`；登录/补拉
   `mock_update_chunk ... total=418 ... path=../web/fs/...`；再进
   `screen_init_ok block_dream=1` + clear→0；无 `mem_fault`。
4. 若 bin/web twin 漂移，再进 publish 应见 `mock_dream_sce_heal_kind3`。
