# Scene Transition Loading Baseline

Date: 2026-06-24

Status: validated

## 1. 当前卡点

- 可见现象：刚进入地图后仍会反复进入加载条；进入下一个地图后人物初始位置不对，不在传送点附近。
- 触发方式：正常启动进入场景，或在 Penglai 路线内踩传送点切图。
- 本轮最小目标：先把“重复加载”和“落点错误”拆回到明确的场景切换状态机、scene enter 响应字段和坐标写入路径。

## 2. 运行时证据

- 既有阶段文档 `docs/re/penglai-empty-npc-transfer.md` 已记录：运行时切图问题与 NPC 是否为空不是同一问题，核心症状在 scene change / scene enter 契约上。
- 已有对照现象：
  - 直接进入保存位置的目标地图时，客户端可以正常渲染目标场景。
  - 运行时通过传送点切换到同类场景时，客户端更容易进入重复加载或重入切图链。
- 这说明当前缺口更像是“运行时切图状态推进”问题，而不是“目标地图本身不可渲染”。
- 2026-06-24 当前源码重跑（`bin/main.exe`，保存位置 `00蓬莱仙岛_02.sce @ (396,473)`，6 个启动动作）得到新的回归现象：
  - `_01 -> _02` 初次 scene change 仍命中 `WT 2/3 len=74 -> builtin-scene-change`；
  - 随后命中 `WT 25/5 len=79 -> builtin-scene-change-post-enter-followup`；
  - 但后续没有再出现旧基线中的 `25/5 len=94 -> builtin-type27-followup`、`6/1 len=39 -> builtin-scene-resource-followup`、`2/3 len=89 -> builtin-scene-change-penglai02-repeat`；
  - 取而代之的是多次 `screen_mgr idx=2 type=same-suppressed caller=01018150`，然后直接落到 `WT 25/5 len=44 -> builtin-scene-task-subset-followup`，场景长期停在 `_02`，`pending=1, ready=1, assets=1, parserState=7, pos=(396,473)`。
- 这说明当前源码和 `docs/re/penglai-empty-npc-transfer.md` 里记录的“可继续推进到后续切图链”的旧稳定基线已经分叉，新的回归点更靠前：发生在 `_02` 入图后的 same-screen reenter / post-enter completion 阶段。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `江湖OL.CBE` | `ProcessSceneState` / `0x01003CFC` | 看 loading 与 scene callback 状态机 | `state < 100` 时进入 `HandleSceneTransition`，`state == 100` 画 loading，`state == 101` 走场景回调。 |
| `江湖OL.CBE` | `HandleSceneTransition` / `0x0100369C` | 看场景切换模式如何改状态 | `mode == 1` 走资源下载；`mode == 2` 会把状态字节写成 `100`；其他场景模式会走 callback 路径并把状态字节写成 `101`。 |
| `江湖OL.CBE` | `EnterSceneByMapName` / `0x01018150` | 看切图时 map/pos 如何写入客户端状态 | 会拷贝 scene 名、重置 scene node、写入坐标字段，然后经 screen-change API 触发模式 `3/4` 的场景进入。 |
| `江湖OL.CBE` | `parse_scene_response_entry` / `0x010396D6` | 看服务端 scene enter 响应到底需要哪些字段 | 读取 `scene` 字符串和 `posinfo` blob；`posinfo` 不是两个裸 `short`，而是两个带 tag 的 `i16` 流；成功后更新当前场景与坐标。 |

## 4. 调用链 / 业务流程

1. 客户端场景状态字节位于 `R9 + 19638`，由 `ProcessSceneState` 统一驱动。
2. 当状态字节小于 `100` 时，客户端进入 `HandleSceneTransition`，按 mode 决定是资源下载、普通切图还是退出。
3. `HandleSceneTransition` 会在普通场景路径中把状态推进到 `100` 或 `101`，分别对应 loading 和后续场景回调。
4. `EnterSceneByMapName` 是运行时切图的重要入口：它写入目标 scene 名、目标坐标，并通过 screen-change API 触发新的场景模式。
5. `parse_scene_response_entry` 是服务端 scene enter 响应的关键解析点：如果 `scene` 或 `posinfo` 不符合它的预期，后续状态推进就可能反复回到 loading 或写入错误位置。

## 5. 结构体 / 状态字段笔记

- `R9 + 19638`
  - current meaning: scene state byte
  - evidence: `ProcessSceneState(0x01003CFC)` 直接读取并分发到 `<100`、`100`、`101`
  - confidence: high
- `R9 + 21655`
  - current meaning: resource-load active flag
  - evidence: `HandleSceneTransition(0x0100369C)` 在 `mode == 1` 时置 `1`，切出资源阶段时清 `0`
  - confidence: high
- `R9 + 21676`
  - current meaning: active scene object pointer
  - evidence: `HandleSceneTransition` 与 `parse_scene_response_entry` 都通过它初始化或更新 scene object
  - confidence: high
- `scene_object + 1141`
  - current meaning: current/target scene name buffer
  - evidence: `EnterSceneByMapName(0x01018150)` 会先清 0x20 字节，再拷贝 scene 名到这里
  - confidence: medium
- `R9 + 23694`, `R9 + 23696`
  - current meaning: pending target x/y
  - evidence: `EnterSceneByMapName(0x01018150)` 在 screen-change 前写入这两个 `WORD`
  - confidence: high
- `scene_object + 436`
  - current meaning: parser/scene sub-state with value `7`
  - evidence: `parse_scene_response_entry(0x010396D6)` 成功读取 `scene` + `posinfo` 后写 `7`
  - confidence: medium

## 6. 请求 / 响应契约

### Request

- 当前具体 WT 组合仍需以运行时最新请求为准。
- 但从现有症状看，本轮优先关注“场景切换请求 -> scene enter 响应 -> loading/callback 状态推进”这条链，而不是 NPC、战斗或 UI 包。

### Response

- `parse_scene_response_entry(0x010396D6)` 已证明 scene enter 响应必须至少提供：
  - `scene` 字符串
  - `posinfo` blob
- `posinfo` 编码要求：
  - 不是两个裸 `short`
  - 而是两个带 tag 的 `i16` 流
  - 例如 `x=120, y=120` 的线格式应为 `00 02 00 78 00 02 00 78`

## 7. 成功路径与失败路径

### Success path

- 客户端收到合法的 scene enter 响应后，`parse_scene_response_entry` 成功写入 scene 与坐标，随后 scene state 只经过一次必要的 loading / callback 推进，最终稳定渲染目标地图并停在正确落点。

### Failure path

- 如果 `scene` / `posinfo` 格式不对，或运行时切图阶段误复用了一套“重新进图”的响应契约，客户端可能：
  - 再次进入 loading；
  - 重复触发相同 scene change 链；
  - 位置被写成旧坐标、默认坐标或错误坐标。

## 8. Negative Evidence

- 既有阶段文档已经表明：把地图上 NPC 设为空不能解决运行时切图卡 loading 的主问题。
- 既有现象也说明：单靠“看截图对不对”不够，需要回到请求链、scene enter 响应格式和状态机调用点核对。
- 2026-06-24 复现实验：
  - 仅追加第 7 个 `key:f` 动作，不能让 `_02` 后续切图链恢复。
  - 仅追加第 7 个 `hold:d:2000` 动作，也不能让 `_02` 后续切图链恢复。
  - 这说明当前卡点不是“少按了一次确认键”这么表层，更像是 `_02` 入图后的内部 scene reenter / post-enter 过程被提前压断。

## 9. Unknowns / Hypotheses

- unknown: 当前重复加载对应的是哪一类重复请求链
  - current guess: 运行时切图后重复进入了同目标 scene 的 scene-change / scene-enter 合同
  - why it matters: 这会决定修复点是在 detector 分流、响应契约，还是事件时序
  - next probe: 对最新运行请求重新做一次窄签名，并和 `ProcessSceneState` / `EnterSceneByMapName` 的状态推进对照
- unknown: 目标地图错误落点来自哪一层
  - current guess: `posinfo` 编码或目标坐标来源没有和 `EnterSceneByMapName` / parser 预期对齐
  - why it matters: 坐标问题如果在 scene enter 响应层，修正点应该在 mock-server 字段，而不是客户端逻辑
  - next probe: 核对当前响应里的 `posinfo` 构造方式与 `0x010396D6` 的读取方式
- unknown: `same-screen reenter guard` 是否把“已完成切图目标”的 `EnterSceneByMapName(0x01018150)` 也持续压住了
  - current guess: 是。当前 guard 既看 active target，也看 completed target，导致 `_02` 入图后的后续 same-screen reenter 仍被按旧目标 `_02` 抑制。
  - why it matters: 如果这条假设成立，修复点应在 `src/main.c` 的 guard 生命周期，而不是继续改包。
  - next probe: 让 guard 只跟踪 in-flight target，completed target 不再参与 suppress；然后重跑 `_02` 入图链，看旧的 `25/5 len=94 -> 6/1 len=39 -> 2/3 len=89` 是否恢复。
- unknown: `_02 -> _03` 落点错误里的 `145,47` 与 `105,395` 不一致来自哪条 same-target request
  - current guess: 当前 scene name loose compare 没把 `c00..._03(.sce)` 与 `00..._03.sce` 视为同一张图，导致 same-target completion 回退到 `_03` 默认落点 `(105,395)`。
  - why it matters: 这是当前“进下一个地图不在传送点附近”的最直接解释。
  - next probe: 在恢复 `_02` 后续切图链后，再核对 current-scene completion 路径里 `target.scene` 与 `currentScene` 的归一化结果。

## 10. 本轮实现计划

- 已实现一轮窄修复：
  - 文件：`src/main.c`
  - 改动：`same-screen reenter guard` 只跟踪 in-flight scene-change target；当 active target 清空后，guard 立即失效，不再拿 completed target 持续 suppress `EnterSceneByMapName(0x01018150)`。
- 原因：
  - 运行证据显示当前回归不是“包又错了一个字段”，而是 `_02` 入图后的 same-screen reenter 被旧完成目标持续压住，导致旧稳定基线里的 `25/5 len=94 -> 6/1 len=39 -> 2/3 len=89` 后续链断掉。

## 11. 本轮验证结果

- 修复前，同一条 `_02` 基线只能跑到：
  - `2/3 len=74 -> builtin-scene-change`
  - `25/5 len=79 -> builtin-scene-change-post-enter-followup`
  - 然后落到 `25/5 len=44 -> builtin-scene-task-subset-followup`
  - 并长期停在 `_02`，`pending=1, ready=1, assets=1`
- 修复后，同一条 6 动作基线恢复到旧稳定请求链：
  - `25/5 len=94 -> builtin-type27-followup`
  - `6/1 len=39 -> builtin-scene-resource-followup`
  - `2/3 len=89 -> builtin-scene-change-penglai02-repeat`
  - `25/5 len=59 -> builtin-scene-task-subset-followup`
  - 并稳定停在 `_02`，`pending=0, ready=1, assets=1`
- 在此基础上追加 `43000:hold:d:2000`，成功稳定复现 `_02 -> _03`：
  - `2/10 len=19 -> builtin-actor-other-only10`
  - `2/3 len=90 -> builtin-scene-change`
  - `25/5 len=95 -> builtin-type27-followup`
  - `6/1 len=39 -> builtin-scene-task-subset-followup-current-scene`
- `_03` 运行结果：
  - `48037ms`: `pending=0, ready=0, assets=0, parserState=7, pos=(145,47)`
  - `51041ms`: `pending=0, ready=1, assets=1, parserState=7, pos=(145,47)`
  - 持久化位置文件：`c00蓬莱仙岛_03.sce @ (145,47)`
  - 截图：`bin/autotest/screens/000017_00051041.bmp` 显示人物已落在 `_03` 北入口附近，而不是旧的 `(105,395)` 默认点。

## 12. 追加取证：`c00蓬莱仙岛_03` 右侧出口

- 日期：2026-06-24
- 目的：重新核对“切下一个地图就闪退”的真实路径，避免继续沿用旧的 `_03 -> _04` 口头结论。

### IDA / 运行时共同结论

- `scene_runtime_tick(0x01014EE0)` 的真实崩点不是“画 NPC”本身，而是场景初始化后的一次性内部事件补发：
  - `scene_runtime_tick()` 在 `pendingActorUiResync` / `scene init` 分支内连续申请 3 个内部事件；
  - 第 3 次申请返回 `NULL` 后，客户端直接写 `v16[2] = 14`，于是访问地址 `0x8` 断言。
- 这意味着崩溃形态更接近“场景初始化生命周期被重复/半重复推进”，而不是一个单纯的渲染字段错位。

### 本地场景数据对照

- 当前项目 `tmp/all_sce_bundle/c00蓬莱仙岛_03.sce/scene.json` 与导出图 `scene.png` 已确认：
  - 北侧边缘出口：`00蓬莱仙岛_02.sce`
  - 右侧边缘出口：`01桃花岛_01.sce`
- 也就是说，`c00蓬莱仙岛_03` 真实本地路由里并没有“南到 `00蓬莱仙岛_04`”这条边缘出口。

### 当前实现偏差

- `src/mock-server.c` 里的 `kPortalFallbacks` 仍把 `c00蓬莱仙岛_03` 记成：
  - 北回 `_02`
  - 南到 `_04`
- 这和当前导出的本地 SCE 数据不一致，说明项目里的 portal fallback 表已有陈旧路径。

### 最新复现实验

- 启动位：`c00蓬莱仙岛_03.sce @ (401,288)`，靠近右侧出口。
- 自动化动作：完整登录动作后，在 `60000ms` 触发 `hold:d:7000`。
- 运行时证据见 `bin/autotest/state.txt` 与 `bin/autotest/state_portal_c00_03_east_probe2.txt`：
  - `63061ms`: `2/10 len=19 -> builtin-actor-other-only10`
  - 随后场景 `ScreenInit Ok`
  - `net_send connect=2 wt=2/3 len=87 source=builtin-scene-change resp=162`
  - 紧接着再次命中 `scene_runtime_tick(0x01014EE0)` 断言
- 崩溃前场景 probe 已出现桃花岛一图的坐标特征：
  - `scene_probe_move[0] pos=(230,425) target=(208,432)`
  - `scene_probe_move[1] pos=(225,116) target=(240,96)`
- 这些坐标与 `01桃花岛_01.sce` 本地场景出口数据一致，因此这条链的真实目标图是 `01桃花岛_01.sce`，不是旧文档里口头简称的 `_04`。

### 当前结论

- 当前闪退不是“`c00蓬莱仙岛_03 -> 00蓬莱仙岛_04` 没做好”，而是：
  - `c00蓬莱仙岛_03` 右侧本地出口实际进入 `01桃花岛_01.sce`；
  - 客户端发出 `2/10` 后又发 `2/3 len=87`；
  - 当前项目却仍把这条链交给通用 `builtin-scene-change`，没有提供这类跨图边缘出口所需的完整 scene bootstrap；
  - 结果再次落回“半进图 -> 重入初始化 -> 0x01014EE0 断言”的旧崩溃形态。

## 13. 验证清单

- [x] 重新抓到最新重复加载对应的最窄请求签名
- [x] 核对当前响应是否满足 `scene` + `posinfo` 契约
- [x] 确认 loading 反复出现时 scene state 的推进路径
- [x] 确认错误落点是响应编码问题还是目标坐标来源问题
- [x] 形成可实现的最小修复点后再进入代码改动

## 14. 追加取证：`00蓬莱仙岛_04` 冷启动崩溃

- 日期：2026-06-24
- 用户提供的崩溃寄存器：
  - `lr=0x01014edf`
  - `pc=0x01014ee0`
  - `r4=0x01056834`
- IDA 结论仍然落在 `scene_runtime_tick(0x01014EE0)` 的同一处内部事件补发崩点：
  - 场景 init 分支连续申请内部事件；
  - 分配返回空指针后继续写事件字段，访问地址 `0x8`。
- 最新运行时复现：
  - 当前存档：`00蓬莱仙岛_04.sce @ (256,300)`；
  - 不移动，仅登录进入场景；
  - 崩前最后关键包为 `WT 2/3 len=74 -> builtin-scene-change resp=99`；
  - 这说明 `_04` 的同场景 completion 请求落入了通用 scene-change 分支，导致 scene init 生命周期再次被半重复推进。
- 修复：
  - 将 `00蓬莱仙岛_04` 纳入 current-scene completion 路径；
  - current-scene completion 的 `27/12.posinfo` 使用运行中 scene node 的真实玩家坐标，而不是同场景 `2/3` 请求里可能带的入口坐标；
  - `moveinfo-upload` 保存坐标时，若调用方没有传 scene，优先从运行中的 scene object (`Global_R9 + 0x54AC`, `sceneObj + 0x475`) 读取当前场景名，避免把空 scene 写入 `nvram/jhol_mock_player_pos.bin`。
- 验证：
  - `bin/autotest/state_start04_noenv_final.txt`
  - `2/3 len=74 -> builtin-scene-change-current-scene-ack resp=179`
  - `27/11 len=29 -> builtin-type27-followup resp=98`
  - `6/1 len=39 -> builtin-scene-task-subset-followup-current-scene resp=177`
  - 场景 probe 稳定到 `50003ms`：`pending=0, ready=1, assets=1, parserState=1, pos=(256,300)`；
  - 持久化位置仍为 `00蓬莱仙岛_04.sce @ (256,300)`，不再丢失 scene 名。

## 15. 追加取证：传送点坐标与玩家落点混用

- 日期：2026-06-24
- 用户截图显示 `00蓬莱仙岛_04` 的黄色传送标记贴在手机视口底部 UI 附近，看起来已经离开地图可用区域。
- IDA 结论：
  - `scene_handle_enter_with_scene_pos(0x010396D6)` 直接读取响应里的 `scene` 与 `posinfo`；
  - `EnterSceneByMapName(0x01018150)` 把这些坐标写入当前 scene object；
  - 因此玩家落点来自 mock server 响应/持久化坐标，不是客户端自动修正。
- 本地 SCE 结论：
  - `00蓬莱仙岛_04.sce` 的桃花岛入口 `spawn_point=(256,300)`，触发矩形为 `(224,256)-(288,285)`；
  - `00蓬莱仙岛_03.sce` 的底部回 `_04` 入口 `spawn_point=(105,395)`，触发矩形为 `(64,400)-(144,416)`；
  - 这些 `spawn_point` 更像本地图传送标记/触发靠近点，不应作为“下次启动玩家站立点”直接保存。
- 修复：
  - 保存玩家位置时，对已知传送标记附近坐标做安全偏移：
    - `_04` 的 `(224..288, 256..304)` 保存为 `(256,245)`；
    - `_03` 的底部 `(64..144, >=390)` 保存为 `(105,360)`；
  - `_04 -> _03` 的 server fallback 落点同步从 `(105,395)` 改为 `(105,360)`。
- 验证：
  - `bin/autotest/state_penglai04_safe_restore.txt`：无环境变量启动 `_04`，稳定在 `pos=(256,245)`；
  - `bin/autotest/state_penglai03_safe_restore.txt`：无环境变量启动 `_03`，稳定在 `pos=(105,360)`；
  - 两条链路均通过 current-scene completion，不再把玩家恢复到传送标记本身。

## 16. 追加取证：传送表回归到 SCE 来源

- 日期：2026-06-24
- 用户反馈“传送点和 SCE 文件中的不一致，传送目的地也不一致”后，重新核对 `tmp/all_sce_bundle/*/scene.json`。导出结构中传送点不在顶层字段，而是 `records[]` 里的 `record_type=edge_portal`。
- IDA 证据：
  - `江湖OL.CBE:0x010396D6` 解析 `30/2` 场景响应时直接读取 `scene` 和 tagged `posinfo`；
  - `江湖OL.CBE:0x01018150 EnterSceneByMapName` 会把目标场景名复制到 scene object 并触发场景生命周期；
  - 因此 mock 响应里的目标坐标会成为当前角色坐标，必须和 SCE 路由含义对齐。
- 本轮核对的 SCE 边缘出口：
  - `00蓬莱仙岛_02.sce`：`(108,5)-(148,25)` -> `c00蓬莱仙岛_01.sce`，entry=1, targetEntry=2；`(421,453)-(441,490)` -> `c00蓬莱仙岛_03.sce`，entry=0, targetEntry=3。
  - `00蓬莱仙岛_03.sce`：`(75,10)-(135,43)` -> `00蓬莱仙岛_02.sce`，entry=1, targetEntry=2；`(64,400)-(144,416)` -> `00蓬莱仙岛_04.sce`，entry=0, targetEntry=0。
  - `c00蓬莱仙岛_03.sce`：`(105,27)-(125,67)` -> `00蓬莱仙岛_02.sce`，entry=0, targetEntry=1；`(421,268)-(441,308)` -> `01桃花岛_01.sce`，entry=1, targetEntry=3。
  - `00蓬莱仙岛_04.sce`：`(116,10)-(156,43)` -> `00蓬莱仙岛_03.sce`，entry=0, targetEntry=2；`(224,256)-(288,285)` -> `01桃花岛_01.sce`，entry=1, targetEntry=2。
  - `01桃花岛_01.sce`：`(208,432)-(256,448)` -> `01桃花岛_02.sce`，entry=0, targetEntry=0；`(240,96)-(272,128)` -> `00蓬莱仙岛_04.sce`，entry=1, targetEntry=3。
  - `01桃花岛_02.sce`：`(320,272)-(352,336)` -> `01桃花岛_03.sce`，entry=1, targetEntry=3；`(32,0)-(128,48)` -> `01桃花岛_01.sce`，entry=0, targetEntry=2。
  - `01桃花岛_03.sce`：`(160,553)-(240,570)` -> `01桃花岛_04.sce`，entry=0, targetEntry=0；`(20,10)-(60,55)` -> `01桃花岛_02.sce`，entry=1, targetEntry=2。
  - `01桃花岛_04.sce`：`(336,160)-(384,224)` -> `00蓬莱仙岛_04.sce`，entry=1, targetEntry=0；`(20,5)-(64,35)` -> `01桃花岛_03.sce`，entry=0, targetEntry=2。
- 代码修复：
  - `src/mock-server.c` 原本有两份手写 portal fallback 表：精确触发表和 margin 触发表。两份表已经分叉，margin 表还残留 `c00蓬莱仙岛_03` 南侧去 `00蓬莱仙岛_04` 的旧错误路由。
  - 本轮改为单一 `kVmNetMockPortalFallbacks` 表，精确和 margin 查询共用同一数据源。
  - 删除不存在的 `c00蓬莱仙岛_03` 南侧路由，补入 `c00蓬莱仙岛_03` 东侧去 `01桃花岛_01` 以及桃花岛回蓬莱的 SCE 路由。
  - `_04 -> _03` 实时切图目标恢复为 SCE 对应入口 `(105,395)`；第 15 节中的 `(105,360)` 只保留为冷启动持久化安全点，避免直接启动在边缘触发区。
- 运行时追加证据：
  - 从 `c00蓬莱仙岛_03 @ (401,288)` 右移触发东出口后，客户端发 `2/10 len=19`，随后发 `2/3 len=87`；
  - 该 `2/3` 的目标为 `01桃花岛_01.sce` 且 `exitID=1`，说明请求里的 `exitID` 与源 SCE 的 `entry_id=1` 对齐，而不是源 SCE 的 `target_entry_id=3`；
  - 旧代码把 `01桃花岛_01 exit=1` 落到 `(230,425)`，正好踩在底部去 `01桃花岛_02` 的入口上，导致刚进图又发起下一轮切图并最终回到 `0x01014EE0` 崩溃；
  - 因此 `vm_net_mock_get_scene_change_target()` 的目的坐标选择改为按源 `entry_id` 选择目标场景同 entry 的入口：例如 `01桃花岛_01 exit=1 -> (225,116)`，`exit=0 -> (230,425)`。
  - 自动化用 `CBE_SCENE_KEY` 指定起点时又暴露出一个后续问题：该环境变量在进图后仍优先于 runtime scene object，导致切到 `01桃花岛_01` 后，服务端后续 completion 仍认为当前图是 `c00蓬莱仙岛_03`。
  - 修复为 `vm_net_mock_current_scene_name()` 优先读取运行时 scene object (`sceneObj + 0x475`)；只有运行时场景名不可用时，才回退到 `CBE_SCENE_KEY` 或持久化位置。
  - runtime scene 优先后，`2/10` 后第一条跨图 `2/3 len=87` 又暴露出 `current-scene-repeat` 判定过宽：客户端本地已经把 runtime scene 切到目标图，mock 就把首次跨图 completion 当成重复包，只回 ack-only。
  - 修复为 `current-scene-repeat` 必须同时满足“近期已完成过同一目标”，首次跨图 `2/3 + 27/11 + 27/4 + 7/42` 继续走普通 scene-change/bootstrap 路径。
  - 继续验证发现普通 scene-change 对这种“本地已切到目标图”的包仍只返回 162 字节 ack 组合，缺少 `27/12.posinfo` completion，导致资源表已是桃花岛但玩家坐标仍停在旧图。
  - 修复为：当前 runtime scene 已等于目标图且目标尚未 recent-completed 时，归入 current-scene completion；带 `27/4` 的本地切图 completion 保留 `vm_net_mock_get_scene_change_target()` 解析出的目标入口坐标，不再用旧图运行坐标覆盖。

## 17. 追加取证：运行时 SCE 传送与重复 loading 收敛

- 日期：2026-06-25
- 当前最小目标：
  - 地图传送点与 SCE 文件中的 `edge_portal` 保持一致；
  - 刚进目标图后不再因为后续 `25/5` 或 `2/10` 被误识别而重复进入 loading；
  - 先修通 `c00蓬莱仙岛_03 -> 01桃花岛_01`，再推广到任意 SCE 边缘传送。

### IDA / 客户端证据

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `scene_handle_enter_with_scene_pos` / `0x010396D6` | `30/2` 场景响应读取 `scene` 字符串和 tagged `posinfo`，成功后进入场景坐标写入路径。 |
| `江湖OL.CBE` | `EnterSceneByMapName` / `0x01018150` | 复制目标 scene 名、写入坐标、重置 scene nodes，并触发屏幕/场景生命周期。 |
| `江湖OL.CBE` | `ProcessSceneState` / `0x01003CFC` | `state == 100` 画 loading，`state == 101` 调用场景 callback；重复发起 scene enter 会表现成多段 loading。 |
| `江湖OL.CBE` | `scene_runtime_tick` / `0x01014EE0` | 旧闪退位置仍是场景 init 生命周期被半重复推进后的内部事件空指针写入。 |

### SCE 数据证据

- `c00蓬莱仙岛_03.sce` 右侧 `edge_portal`：
  - `spawn_point=(401,288)`
  - `targetScene=01桃花岛_01.sce`
  - `entryId=1`
  - `trigger_rect=(421,268)-(441,308)`
  - `targetEntryId=3`
- `01桃花岛_01.sce` 的 `entryId=1` 边缘入口：
  - `spawn_point=(225,116)`
  - `trigger_rect=(240,96)-(272,128)`
  - `targetScene=00蓬莱仙岛_04.sce`
- 运行时验证表明，`c00蓬莱仙岛_03` 右侧进入桃花岛时，客户端请求里的 `exitID=1` 应落到 `01桃花岛_01` 的 entry `1` 附近，即 `(225,116)`，不是底部入口 `(230,425)`。

### 实现结论

- `src/mock-server.c` 新增轻量 `edge_portal` 运行时解析：
  - 优先从 `tmp/all_sce_bundle/<scene>.sce/scene.json` 读取已解压的 SCE 导出；
  - `bin/JHOnlineData/<scene>.sce` 是 type-2 压缩资源，不能直接按明文 `SCE2` 扫描；
  - JSON 不存在时才回退旧 raw-SCE 探测和静态 fallback 表；
  - 按玩家当前格点和 trigger rect 查找源地图出口，再按目标 scene 的 entry 查目标落点。
- `vm_net_mock_get_scene_change_target()` 现在优先用目标 scene 的 SCE entry spawn 生成 `30/2 scene+posinfo` 坐标，避免继续维护分叉的手写路由。
- SCE `spawn_point` 往往贴着 `trigger_rect`，运行时切图不能直接把它作为玩家落点；本轮增加 32px 安全间隔，只在 raw spawn 落入或贴近 trigger rect 时，把 `30/2.posinfo` 推到 trigger rect 外侧。
- 对已经完成的目标图，后续 `25/5` 不再重复下发完整 `30/2 scene+posinfo`，只返回轻量 follow-up。否则客户端会再次走 `EnterSceneByMapName(0x01018150)`，可见表现就是刚进图出现多段 loading。
- 刚完成进入目标图后的短窗口内，`2/10 Type=1` 不再走 portal 分支，而是落到 `builtin-actor-other-only10` 空响应。原因是此时 scene node / HUD 状态可能仍指向入口节点，直接按当前位置判 portal 会把“刚落地”误判成“又踩传送点”。

### 验证结果

- 自动化起点：`CBE_SCENE_KEY=c00蓬莱仙岛_03.sce`, `CBE_SCENE_POS_X=421`, `CBE_SCENE_POS_Y=288`。
- 关键包链：
  - `WT 2/3 len=77 -> builtin-scene-change resp=240`
  - `WT 25/5 len=44 -> builtin-scene-task-subset-followup resp=177`
  - 入图后 `WT 2/10 len=19 -> builtin-actor-other-only10 resp=42`
- 修复前的失败形态：
  - `25/5` 会返回带完整 scene enter 的大包，客户端再次触发 `screen_mgr same` / `ScreenInit`；
  - 或刚进图的 `2/10` 被误判为新 portal，继续触发下一次切图，最后回到 `0x01014EE0`。
- 修复后：
  - `25/5` 后续保持轻量响应，不再出现目标图的重复 `ScreenInit`；
  - `mock_scene_safe_landing scene=01桃花岛_01.sce raw=(225,116) safe=(208,116) rect=(240,96)-(272,128)`；
  - scene probe 稳定显示 `01桃花岛_01` 的两个边缘入口：`(230,425)` 与 `(225,116)`；
  - 不移动时可稳定停留在 `01桃花岛_01`，无断言。
- 蓬莱迷境追加验证：
  - 自动化起点：`00蓬莱仙岛_02.sce @ (396,473)`，登录后 `hold:d:2500` 触发 `_02 -> c00蓬莱仙岛_03`；
  - `mock_scene_safe_landing scene=c00蓬莱仙岛_03.sce raw=(145,47) safe=(157,47) rect=(105,27)-(125,67)`；
  - 关键包链为 `WT 2/3 len=80 -> builtin-scene-change resp=239`，随后 `WT 25/5 len=44 -> builtin-scene-task-subset-followup resp=177`；
  - `48042ms` 到 `69057ms` scene probe 均为 `pending=0, ready=1, assets=1`，没有二次 `2/3` 或断言；
  - 持久化位置保存为 `c00蓬莱仙岛_03 @ (157,47)`。

### Unknowns

- `edge_portal.targetEntryId` 不能作为目标落点 fallback；它可以作为源 portal 反查键。运行证据显示客户端请求里的 `exitID` 可能对应源记录的 `target_entry_id`，而真正目标落点仍应使用同一源记录的 `entry_id` 去目标 SCE 查 spawn。
- 当前 JSON 读取器只覆盖已经确认会影响边缘传送的导出字段，不把它扩展成完整 JSON/SCE 反序列化器；后续每增加新字段都必须先有 IDA 或运行证据。

## 18. 追加规则：缺 SCE 数据时触发客户端下载而不是坐标回退

- 日期：2026-06-26
- 用户约束：如果 mock-server 没读到目标地图的 SCE/JSON 数据，不允许再走手写 fallback 坐标；应让客户端触发对应场景文件下载。

### IDA / 客户端证据

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `scene_handle_enter_with_scene_pos` / `0x010396D6` | `30/2` 场景响应只要带 `scene + posinfo`，就会推进到写当前场景与坐标的路径；缺 SCE 时不能伪造 `posinfo`。 |
| `江湖OL.CBE` | `scene_handle_change_result_scene_pos` / `0x01039890` | `result == 1` 后同样读取 `scene + posinfo`；运行时切图路径也要遵守同一约束。 |
| `江湖OL.CBE` | `HandleSceneTransition` / `0x0100369C` | `mode == 1` 会进入 `InitResourceDownload`，这是缺资源时客户端应走的路径。 |
| `江湖OL.CBE` | `send_update_chunk_request` / `0x01036D80` | 资源下载请求 case 4 构造 `18/7`，字段包含 `name/screen/type/start/version`；现有 `builtin-update-chunk` 已按 `JHOnlineData/<name>` 提供分片。 |

### 实现结论

- `vm_net_mock_get_scene_change_target()` 增加 `hasSceEntry` 与 `needsSceneDownload`：
  - 目标 SCE entry 可读时，照常用 SCE spawn 构造 `30/2 scene+posinfo`；
  - 目标 scene 名是安全下载 key、但找不到 SCE entry 时，标记 `needsSceneDownload=true`，只保留 scene 名，不再写入 fallback 坐标。
- `vm_net_mock_build_scene_change_combo_response()` 和 `vm_net_mock_build_scene_change_post_enter_followup_response()` 在 `needsSceneDownload` 时只返回 scene ack，不附带 `posinfo`，并且不记录 pending target、不标记 scene-change completed、不保存玩家位置。
- portal 入口查询也改成证据优先：
  - 若当前地图能读到 SCE/JSON，但当前位置找不到 `edge_portal`，直接不触发切图，禁止落到静态 fallback；
  - 若当前地图本身缺 SCE/JSON，则记录 `mock_scene_missing_sce ... action=wait-client-download`，等待客户端走资源下载路径；
  - 静态 `kVmNetMockPortalFallbacks` 只保留给非下载 key 的旧兼容面，不再覆盖普通地图数据。

### 验证结果

- 正常 SCE/JSON 存在时，`00蓬莱仙岛_02 -> c00蓬莱仙岛_03` 仍走 SCE spawn：
  - `mock_scene_safe_landing scene=c00蓬莱仙岛_03.sce raw=(145,47) safe=(157,47) rect=(105,27)-(125,67) entry=0 targetEntry=1`
  - `WT 2/3 len=80 -> builtin-scene-change resp=239`
  - `WT 25/5 len=44 -> builtin-scene-task-subset-followup resp=177`
- 控制实验中临时移走 `tmp/all_sce_bundle/c00蓬莱仙岛_03.sce/scene.json` 后，同一路径不再使用旧坐标回退：
  - `mock_scene_missing_sce target=c00蓬莱仙岛_03.sce exit=0 action=download-ack`
  - `mock_scene_download_ack scene=c00蓬莱仙岛_03.sce exit=0 response=scene-ack-without-posinfo evidence=JianghuOL:0x100369C/0x1036C66`
  - `WT 2/3 len=80 -> builtin-scene-change resp=243`
- 这次控制实验没有看到真实 `18/7`，原因是客户端本地仍有 `JHOnlineData/c00蓬莱仙岛_03.sce`；但服务端已停止下发伪 `posinfo`，如果客户端缺本地资源，应由 `HandleSceneTransition(mode==1)` 进入资源下载，并由现有 `builtin-update-chunk` 响应。

### 后续约束

- 后续任何地图切换问题都先确认目标 SCE/JSON 是否可读；不可读时只能走 download ack，不允许新增手写坐标 fallback。
- 新增地图支持时，优先补 SCE 解包/JSON 读取或验证 `18/7` 下载链路；静态 fallback 只能作为明确标注的临时兼容项，并且不能覆盖普通 scene key。

## 19. 追加取证：铜雀台下出口到铸剑谷上入口

- 日期：2026-06-30
- 用户反馈：从 `蓬莱-铜雀台` 下方传送点进入 `蓬莱-铸剑谷` 后，人物出现在铸剑谷下方传送点附近；理论上应出现在铸剑谷上方入口附近。

### SCE 证据

- `c00蓬莱仙岛_01.sce` 下边缘出口：
  - trigger rect: `(203,402)-(240,422)`
  - target scene: `00蓬莱仙岛_02.sce`
  - `entry_id=1`, `target_entry_id=0`
  - source-side spawn point: `(223,382)`
- `00蓬莱仙岛_02.sce` 入口：
  - `entry_id=1` 的 spawn point 是 `(128,45)`，位于铸剑谷上方入口；
  - `entry_id=0` 的 spawn point 是 `(396,473)`，位于铸剑谷下方/右下入口。

### IDA / 客户端证据

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `FindEmptyActorSlot` / `0x01018166` | 2026-08-07 复核：该函数遍历 actor slot 并返回空 slot，不能作为本地传送门创建目标场景的证据。边缘传送进入仍须以 `WT30/1` 或带 `posinfo` 的 `WT30/2` parser 路径为准。 |
| `江湖OL.CBE` | `scene_handle_change_result_scene_pos` / `0x01039890` | `30/2` 在 `result==1` 后读取 `scene` 与 tagged `posinfo`，并调用 scene object 的 enter/update 方法。服务端响应坐标就是最终落点。 |
| `江湖OL.CBE` | `scene_handle_enter_with_scene_pos` / `0x010396D6` | `30/1` 同样读取 `scene + posinfo`，坐标流格式为两个 tagged i16。 |
| `江湖OL.CBE` | `EnterSceneByMapName` / `0x01018150` | 把场景名复制到 scene object，并写入目标 x/y 后触发生命周期切换。 |

### 批量 SCE 证据

- `tmp/all_sce_export/*.json` 统计到 `317` 条 `edge_portal`，覆盖 `151` 张场景。
- 其中 `249` 条记录的 `entry_id != target_entry_id`。
- 示例：`c00蓬莱仙岛_01.sce -> 00蓬莱仙岛_02.sce` 的源入口是 `entry_id=1,target_entry_id=0`；目标图 `entry_id=1` 是铸剑谷上入口 `(128,45)`，`entry_id=0` 是下入口 `(396,473)`。
- 结论：`target_entry_id` 不能作为目标落点 fallback；它只可用于从源 SCE 中按请求 `exitID` 反查 portal。通用落点规则仍是“选中的源 portal 的 `entry_id` 去目标 SCE 查同 id 入口”。
- 2026-06-30 重新核对本地 `web/fs/JHOnlineData/*.sce`：196 个 SCE 文件都是
  `little-endian u32 block_len + resource_type + big-endian compressed_len + big-endian decoded_len + LZSS stream`。
  解压后的负载才以 `SCE2` 开头，原始文件不能直接扫描 `edge_portal`。
- 大理复现样本：`c18大理_02.sce` 原文件 226 字节，资源流声明压缩 213 字节、解包 272 字节；解包后可读到
  `entry_id=1 -> c18大理_03.sce` 与 `entry_id=2 -> c18大理_01.sce` 两条 `edge_portal`。

### 根因

- 旧实现中 `vm_net_mock_get_scene_change_target()` 在解析 `WT 2/3` 时优先按“目标地图 `mapID` + 请求 `exitID`”读取目标 SCE entry。
- 当请求上下文与当前源入口不一致时，旧代码会直接选择目标图的错误 entry，例如铸剑谷 `entry_id=0` 的 `(396,473)`。
- 本轮运行输出确认初次 `WT 2/3 len=74` 后仍进入 `00蓬莱仙岛_02.sce pos=(389,473) exit=0`，说明请求 `exitID=0` 对应的是源记录的 `target_entry_id=0`，不能直接拿去目标图查 spawn。
- 但源 SCE 明确说明玩家踩到的是 `c00蓬莱仙岛_01.sce` 的 `entry_id=1` 边缘出口，对应目标图应优先落到同 entry 的入口，即铸剑谷上方 `(128,45)`。安全落点调整后会避开上方触发矩形。
- 本次大理问题的直接原因是 mock-server 的 SCE 读取层只扫原始文件中的明文 `SCE2`，遇到压缩资源时读不到目标 `edge_portal`，随后落入中心点路径，产生
  `scene=c18大理_02 pos=(200,200)`。

### 实现结论

- `src/mock-server.c` 增加源 SCE portal 上下文优先解析：
  - `WT 2/3` 解析出 `mapID/exitID/maptype` 后，先读取当前玩家格点；
  - 先用角色最后稳定场景与当前 runtime scene 作为源 scene 候选，但必须经过 SCE 校验，不能直接当坐标来源；
  - 若源 scene 的 `edge_portal` trigger rect 命中当前位置，并且该 portal 的 target scene 与请求 `mapID` 一致，则采用该源 portal；
  - 若客户端已经进入 pending 本地切图态，且 scene object `+0x475` 的 pending target scene 与请求 `mapID` 一致，则允许用请求 `exitID == source_portal.target_entry_id` 反查源 portal；
  - 坐标只采用选中源 portal 的 `entry_id` 去目标 scene 查同 id spawn；`target_entry_id` 不再作为目标图落点；
  - 如果源 portal 命中但目标 SCE entry 解不出，则返回无 `posinfo` 的 scene ack，等待客户端下载/补资源，不再伪造另一条入口坐标。
- 目标 SCE entry 直接查询仍保留为 fallback，用于没有实时源 portal 上下文的启动、传送石、或其他非边缘路径；普通下载 key 不允许再落到静态坐标表。
- `vm_net_mock_load_scene_resource()` 现在先读取真实 `.sce` 资源文件，再按客户端 `GetStreamDataFormRes/LzssDecode` 等价规则解出 `SCE2` 负载，所有
  `edge_portal`、entry spawn、尺寸解析都基于解压后的服务端资源数据。
- `WT 2/3` 解析不再使用“目标 SCE 存在但 entry 查不到时取中心点”的兜底；这种情况记录
  `mock_scene_entry_missing ... action=no-center-fallback`，继续回到入口/资源/协议证据排查。
- 2026-06-30 follow-up: `WT 2/1` 的 `moveinfo` 上传包前两个 tagged i16 是客户端上报的
  `gridPosX/gridPosY`（IDA: `net_handle_actor_move_info(0x01012ADC)` case 2）。服务端现在记录
  “最后一次 moveinfo 源 scene + 坐标”，并在 `WT 2/3` 解析时先用这份服务端可见位置反查源 SCE portal。
  这避免本地 scene object 已经预切到目标图后，role/runtime 源 scene 被污染而选错目标入口。
- 地图瞬移 `16/4` 的 saved-target inherit 不在 target 解析函数里消费；
  `vm_net_mock_get_scene_change_target()` 会被多个 detector 预探测调用，解析阶段必须保持无副作用。
  pending 只在真实进场完成分支清掉，避免 `WT 2/3` 真正构包前丢失 `16/4` 的权威落点。
- `1/27/4.info` 默认不再填“蓬莱-铜雀台”；对象仍保留，但默认文本为空，避免每次切图后弹出错误地点提示。

### 验证点

- 预期日志：
  - `mock_scene_change_source_portal source_kind=role-pending source=c00蓬莱仙岛_01... request=00蓬莱仙岛_02.sce request_exit=0 portal_entry=1 targetEntry=0 match=target-entry ... target=(128,57)`
  - `target=(128,57)` 是 `(128,45)` 经过上方触发矩形 `(108,5)-(148,25)` 的 32px 安全间隔调整后的落点。
- 若出现 `mock_scene_portal_exit_mismatch`，说明请求 `exitID` 与源 SCE `target_entry_id` 不一致，应继续追客户端传送门 request producer 到 `WT 2/3` 组包之间的字段来源，而不是新增地图特殊处理；`0x01018166` 已证实只是 actor-slot 查询，不是该 producer。
- 若出现 `mock_scene_change_source_probe_miss`，说明 pending target 已匹配请求，但角色稳定场景/runtime scene 中没有找到满足 target/exit 的源 portal，需要继续追 SCE 载入到 move entry 的字段映射。
- 2026-06-30 follow-up: map-stone download support had introduced an
  `exitID=0` same-scene pending/completed target inheritance before the source
  portal lookup. That is valid for post-download packets that no longer carry the
  original position, but it is too early for real edge-portal scene changes: a
  new portal into the same scene can inherit stale coordinates and land away
  from the target entrance. The source SCE portal lookup is now tried before the
  inheritance branch, so live portal context always wins.
- 本轮自动化烟测：
  - `make` 通过；
  - 使用 `CBE_SCENE_KEY=c00蓬莱仙岛_01.sce` 与 `hold:8` 能进入地图并连续产生 `WT 2/1 moveinfo`，但脚本没有稳定走入下边缘触发矩形，因此未触发目标 `WT 2/3`；
  - 该自动化结果只证明没有启动/移动断言，不能当作 portal 修复的完整运行通过证据。需要手动或更精确的点击/坐标驱动继续确认。

## 2026-06-30 Same-Scene Repeat With Missing Entry

Runtime negative evidence:

```text
mock_scene_entry_missing scene=00..._02.sce exit=0 action=no-center-fallback
net_send connect=2 wt=2/3 len=74 source=builtin-scene-change resp=99
mock_scene_enter_defer phase=mmgame-scene-transfer-followup scene=00..._02.sce pos=(0,0) exit=1 missing=- keep_pending=1
mock_mmgame_scene_transfer_followup scene=00..._02.sce pos=(0,0) objects=9 resp=355 complete=0
```

The user-observed sequence is: first scene entry lands near the portal, then a
second loading pass starts and the avatar moves outside the valid map area. The
trace shows the second `WT 2/3` was resolved as an existing target scene with no
SCE entry coordinates. The old code treated that as a pending scene-download
target, then the later `25/5` follow-up sent a `30/1 scene+posinfo` object for
`(0,0)`.

Client evidence:

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `net_handle_scene_channel_dispatch(0x01039BB4)` | scene channel case `1` consumes `30/1`, case `2` consumes `30/2`; other objects are informational. |
| `江湖OL.CBE` | `scene_handle_enter_with_scene_pos(0x010396D6)` | `30/1` reads `scene` and tagged `posinfo`, then drives the scene enter path. |
| `江湖OL.CBE` | `EnterSceneByMapName(0x01018150)` | writes target scene and coordinates before triggering the scene lifecycle. |

Implementation rule:

- An existing server-side SCE with a missing entry is not a resource download
  target. It means the current request did not carry enough source-portal
  context for the server to compute an authoritative landing point.
- For that unresolved existing-scene target, `WT 2/3` must be ack-only: send the
  normal scene ack objects requested by the client, but do not call
  `vm_net_mock_remember_scene_change_target()`, do not defer completion, and do
  not save `(0,0)` as role position.
- If a previous bad pending target is already present, the `25/5` follow-up must
  clear it and return a default ack without `30/1`, because `30/1` would call
  `EnterSceneByMapName(scene, 0, 0)`.

Expected trace after the fix:

```text
mock_scene_entry_missing scene=... exit=0 action=no-center-fallback
mock_scene_change_unresolved_entry_ack scene=... exit=0 action=ack-only-no-pending
```

There should be no subsequent
`mock_mmgame_scene_transfer_followup scene=... pos=(0,0)` for that same target.
