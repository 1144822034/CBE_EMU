# 梦境 vs 野外遇怪开战对比（2026-08-02 / debug-0b451a）

## 野外（正常）路径

```text
地图壳就绪（冷/热 DF + 完整 EnterScene / 27/12 族）
  → 玩家撞怪
  → 客户端 WT 1/4/1 { id, index, posx, posy [, moveinfo] }
  → 服务端 mock_challenge_battle_start
       25/12 清 banner
     + 1/2/2 scene-monster moveinfo（可选，按 wire id 灌血蓝）
     + 1/4/5 { side, battleinfo }   ← subtype-5 场景开战
     [+ 1/2/1 玩家 moveinfo empty-ack]
  → mmBattle HandleServerBattleCmd → HandleBattleStartMsg(0x66CC)
  → 用 request 的 index/pos 在客户端 25 格 live-node 表里抄怪
  → 进战斗 UI
```

要点（`docs/re/2026-07-22-scene-monster-battle-start-crash.md`）：

- `target_source=request-live-node`：index/pos **必须**是客户端当前 live-node，不能 SCE 重选。
- subtype-5 依赖场景壳上的 live-node 表与 map 状态；从场景壳切到 mmBattle。
- 典型日志：`subtype=5 scene_start=1`，随后有战斗 operate，无 `0x01046C48`。

## 进图契约（2026-08-02 澄清 / debug-0b451a）

1. **登录若在梦境**：`role_dream_kind3_login_relocate` 自动迁到 `c04临安府_06`（禁止冷登录留在 29*）。
2. **进梦只能走副本使者 NPC 请求路径**（OPEN/ENTER → dream_drain `30/1`）。  
   **禁止** scene-poll 投递 `30/1`（hot518 `screen_init_fail emu_err=11`）。
3. 烟测：`CBE_DREAM_ENCOUNTER_SMOKE=1` 时 OPEN(30406) 升格为 ENTER；客户端 `CBE_DREAM_ENCOUNTER_WALK=1` 走向使者。

## 梦境（当前）路径

```text
登录 relocate(若在29*) → 临安 → 使者 NPC OPEN/ENTER → 30/1（dream_drain）
  → 热 DF ScreenInit（518c*，block_dream=1）
  → post_enter **无 27/12**（为消进度条）
  → 可走、可见怪
  → 撞怪 1/4/1
  → 已试包形（均送达后仍 mem_fault pc=0x01046C48 / fault=0x40）：
       A. HAS_FOLLOWUP lone 4/10（resp=117）
       B. outdoor-like 4/5 + real remap vitals（wire=200 real=203）
       C. wire-keep 4/5（wire=real=200，objects=4，resp=209）
```

崩溃 PC 是 `SetMapCtrlViewport`（场景 `SCR_Render`），不是战斗立绘空指针
`0x01004EA8`。含义：**没有进入 mmBattle**，留在场景渲染并空解引用 map ctrl。

## 差异表

| 维度 | 野外 | 梦境（现状） |
|------|------|----------------|
| 进图 | 常有完整 `27/12` / EnterScene | `30/1` 热壳；post_enter **刻意无** `27/12` |
| ScreenInit | 多为可战斗的 mmGame 壳 | `hot518=1`（`init≈0x0518*`） |
| 怪来源 | SCE kind=3 + automonster | 注入 kind=3 + 常 wire→real 绑定 |
| 开战 subtype | `4/5` live-node | 同形 `4/5` 仍不进战；`4/10` followup 也不进战 |
| 撞怪请求 | `1/4/1` + live index/pos | 同（日志 `index=14 pos=(65,84)`） |
| 服务端响应 | `25/12+2/2+4/5` → 进 mmBattle | 同形已送达 → **仍场景壳崩** |
| 失败 PC | （正常无） | `0x01046C48` SetMapCtrlViewport |

## 已证伪（梦境）

1. 仅 followup `4/10` 投递问题（投递成功仍崩）。
2. 仅 wire≠real remap（wire-keep 后 `real=200` 仍崩）。
3. 进图卡条与遇怪闪退不是同一包形问题（进图已好，遇怪仍崩）。

## 当前根因陈述（可检验）

**触发**：梦境热壳可走后踩怪发 `4/1`。  
**被违反的契约**：subtype-5 假定「当前场景壳具备可切入 mmBattle 的 map/live-node 状态」；梦境 `518c*` + 无 `27/12` EnterScene 的壳不满足该契约。  
**首个错误状态**：客户端解析开战包后未切 mmBattle，继续 `SCR_Render`。  
**证据**：wire-keep `subtype=5 resp=209` 紧接 `mem_fault pc=0x01046C48`；对照野外同形进战。

## NPC 副本挑战流程能否套到梦境遇怪？

NPC 守关挑战（`docs/re/2026-07-30-instance-challenge-action13-no-battle.md`）：

```text
菜单 → 26/0+30/9 确认框 → 客户端 30/10{agree}
  → 主 CBMR: 30/10 ack
  → HAS_FOLLOWUP: lone 4/10（forceNonScene，objects=1，无 25/12）
  → mmBattle（不依赖场景 live-node）
```

| 可借鉴点 | 结论 |
|----------|------|
| 仅开战包 `lone 4/10` + followup | **梦境踩怪已试过并证伪**（`resp=117`×2 仍 `0x01046C48`） |
| 完整菜单确认机（`30/9→30/10` 上下文） | 与踩怪 `4/1` **不是同一请求机**；踩怪不会发 `30/10` |
| 把 `4/1` 主响应改成 `30/10` | 错请求类型，会撞业务回调闸门（同 action13 负例） |
| 野外/临安上 NPC 挑战本身 | 可用，证明 subtype-10 + followup **在完整场景壳**上成立 |

**结论**：不能把「遇怪 = 直接套 NPC 挑战开战包」当作已验证修复；包形已复用失败。
若要继续沿此方向，只能验证新假设——**梦境壳上 subtype-10 是否在任何正确投递下都能进 mmBattle**（例如梦境内放可挑战 NPC，或另找不破坏 map ctrl 的投递点），而不是再叠一遍 `4/1→followup 4/10`。

## 运行时基线（2026-08-02 / debug-0b451a 同局）

### 路径 A 野外踩怪 — 确认

```text
dream_encounter_remap_probe scene=01桃花岛_01 wire=105 dream=0 use_scene=1
challenge_battle_start_packet subtype=5 scene_start=1 index=8 pos=(146,349)
  objects=3 resp=203 source=request-live-node
queue_data_primary resp=203 followup=0
# 无 mem_fault → 进 mmBattle
```

### 路径 B 蓬莱小猴子挑战 — 确认

```text
dream_encounter_remap_probe scene=00蓬莱仙岛_02 wire=1 use_scene=0
challenge_battle_start_packet subtype=10 scene_start=0 objects=1 resp=113
challenge_battle_wire_followup actor=20021 enemy=1 age=0 resp=113
queue_data_primary resp=23 followup=113
queue_data_followup resp=113
# 无 mem_fault → 进 mmBattle（菜单名「小猴子」，开战 enemy=1 野猪）
```

### 战后进梦 — 闪图（对照局）

```text
loading_clear_ok scene=c04临安府_06 rem→0
queue_data_primary resp=106
mem_fault lr=0x010136B3 type=21
screen_init_fail init=0x051C5B85 block_dream=1 hot518=0 emu_err=11
```

#### 2026-08-03 复现（野外战斗→传送→使者进梦，debug-0b451a）

与上表同族，**不是**遇怪 `0x01046C48`：

```text
challenge_battle_start_packet subtype=5 scene=01桃花岛_01 resp=203  # 野外进战 OK
loading_clear_ok scene=c04临安府_06 rem→0
queue … resp=103,101,106
cbe_mapctrl_probe pc=0x0100629A  # SCE2 仍写入 mapctrl
mem_fault fault=0 pc=0x050003C8 lr=0x010136B3 type=19
screen_init_fail this=17121120 init=0x05199B25 block_dream=1 hot518=0 emu_err=11
```

CBE：`lr=0x010136B3` = `0x010136B0 blx r6` 之后（`r6` 来自 `[obj+8]`）。
`pc=0x050003C8` 为坏回调。`this` 与先前桃花岛热壳相同。无
`dream_encounter_smoke_messenger_enter` 日志。SCE 磁盘 418/ghostfire 双侧一致。

### 干净进梦（无前置战斗）— 进图 OK，遇怪仍崩

```text
screen_init_ok hot518=1 block_dream=1 emu_err=0
loading_clear_ok scene=29梦境空间_01 rem→0
dream_wire_keep_outdoor_like path=wire-keep-4/5
challenge_battle_start_packet subtype=5 objects=3 resp=203
  source=request-live-node-dream-wire-keep wire=200 real=200
queue_data_primary resp=203
mem_fault fault=64 pc=0x01046C48
```

### 登录迁出恢复后（含 28* 卡档）— 进图长 + `1/25/5` 未实现 + 遇怪仍崩

```text
dream_kind3_login_relocate from=28野外梦测_01 to=c04临安府_06
screen_init_ok hot518=1
loading_clear_ok rem=2
unimplemented_handler_notice major=1 kind=25 subtype=5 resp=89
# 无 dream_post_enter_no_27_12
… wire-keep 4/5 → mem_fault pc=0x01046C48
```

根因补充：梦境 DF 后客户端发了**非 9 字节 short** 的 lone `1/25/5`，旧
`direct_enter_ack` 只认 short → 落入未实现提示。已放宽为 short|lone + dream
`25/5 result=4`（日志 `dream_direct_enter_25_5_ack`）。

### 复现 `功能暂未实现(1/25/5)`（debug-0b451a 2026-08-03）

```text
screen_init_ok … bmap=b_29梦境空间.sce
loading_clear_ok rem=2
25_5_direct_enter_miss miss=gate req_len=80 short=0 lone=0 objs=5 pending=1
unimplemented_handler_notice major=1 kind=25 subtype=5 resp=89 req_len=80 objs=5
# hex 前缀: WT 25/5空 + 2/3{maptype,mapID=29*,exitID} + 三个 5 字节空对象
# 随后 short 25/5 仍可 dream_direct_enter_25_5_ack
```

根因：进梦后复合 post-enter 为 **5 对象**
`25/5+2/3+27/11+7/42+25/5`（尾部多一个空 `25/5` keep-alive，总长 80；
不是 `27/4`），旧 `is_scene_change_post_enter_followup_request` 只认恰好 4 个；
同时 `direct_enter_pending` 使 mmgame `25/5` fallback 也不接 → tip。
已放宽 detector 接受可选尾部空 `25/5`（及 `27/4`/`12/1`）。
日志：`post_enter_followup_match extra_25_5=1` /
`dream_post_enter_no_27_12`。

### 进图恢复后遇怪仍崩；试跳过开战前置 25/12

进图已见 `dream_post_enter_no_27_12`、无 unimplemented。遇怪：

```text
dream_wire_keep … path=wire-keep-4/5
challenge_battle_start_packet subtype=5 objects=4 resp=209
mem_fault pc=0x01046C48
```

### 已证伪：跳过开战前置 25/12

```text
dream_skip_banner_clear12 path=no-25/12
challenge_battle_start_packet subtype=5 objects=2 resp=185
mem_fault pc=0x01046C48
```

### 已证伪：lone `4/5` only

```text
dream_lone_subtype5 path=lone-4/5
challenge_battle_start_packet subtype=5 objects=1 resp=88
mem_fault pc=0x01046C48
client_out: unmapped access soft-failed
```

**结论**：梦境热壳上 **任何 subtype-5 包形**（含 objects=1）都不能切入 mmBattle。

### 已证伪：followup `4/10`（wirekeep#200 / force enemy=1）

```text
dream_force_enemy1_followup10_armed force_enemy=1
challenge_battle_start_packet wire=1 real=1 subtype=10 resp=113
queue_data_primary resp=11 followup=113
queue_data_followup resp=113
mem_fault pc=0x01046C48
```

怪物 id 与 wire/real 不一致均不是根因；`4/1` 上下文上的 followup `4/10`
在梦境热壳上仍不进 mmBattle。

### 已证伪：hyp M（遇怪 late `27/12-ack` + followup `4/10`）

使者进梦自测（debug-0b451a）：`messenger_enter` → dream_ready → walk kind3 →
`dream_2712ack_followup10_armed` + followup `4/10` 后立刻
`screen_init_fail hot518=1 emu_err=11`（`lr=0x010136B3`），**不是**
`0x01046C48`。与 post-enter `27/12-ack` 同族，已回滚。梦境遇怪禁止任何 `27/12`。

对照同日野外：`subtype=5 objects=3 resp=203` + 亦曾 `hot518=1` → **进 mmBattle**。  
故差异不在包长/subtype，而在梦境 post-enter **无 27/12 完成族**。

系统提示「功能未实现」= `mock_unimplemented_handler_notice`（未实现请求的
`25/12+1/3/3`），不是遇怪闪退根因。

## 已证伪：post-enter `27/12-ack`

```text
screen_init_ok hot518=1
dream_post_enter_27_12_ack objects=5 resp=254 path=27/12-ack-no-posinfo
mem_fault lr=0x010136B3 r6=0
screen_init_fail hot518=1 emu_err=11   # 二次 Init，进图闪退
```

已回滚为 `no-27/12`。梦境进图不能靠任何 `27/12` 补战斗壳；遇怪
`0x01046C48` 需另找不触发二次 EnterScene/ScreenInit 的契约。

## CBE 取证：`0x01046C48` 调用链（非包形假设）

二进制：`bin/CBE/江湖OL.cbe`（code `@0x01000000`，`codeOffset=0x9a`）。

### `SetMapCtrlViewport`（`0x01046C2C`）

```text
str  r1, [r0 + 0x1C30]     ; 写入 scene mapctrl
adds r1, #0x40
ldrh r3, [r1]              ; PC=0x01046C48；r1==0 ⇒ fault@0x40
… copy viewport words …
b    0x010469AC            ; SCR_Render body
```

由场景 map 初始化 `0x01046C60` 装入 **scene+0x1C84（vtable+4）**；同表：
`+0=0x010466D5`，`+8=0x0104674B`，`+C=0x01046725`；`0x010469AD` 装入
`scene+0x1C68`。

### 唯一会 `blx` 到 vt+4 的站点（字面量匹配）

| 地址 | 作用 |
|------|------|
| `0x01014E74` | 历史崩溃 `lr=0x01014E77`（见 `2026-07-19-return-title-relogin-scene-lifecycle.md`） |
| `0x01017E06` | 另一 screen 路径 |
| `0x01017EF6` | 另一 screen 路径 |

`0x01014E54` 片段（对齐反汇编）：

```text
ldrb r0, [r4, #1]
cmp  r0, #1
bne  skip
movs r1, #3
ldr  r0, [r4, #0x60]       ; map/SCE 对象
lsls r1, r1, #9            ; +0x600
adds r0, r0, r1
ldr  r1, [r0, #0x1c]       ; ★ mapctrl 实参（空则进 SetMapCtrl 后崩）
; scene = *(sb-reloc); blx scene.vt+4  → 0x01046C2D
bl   0x01046696            ; 读 scene+0x1C30 做视口尺寸
strb #0, [r4, #1]
```

### mapctrl 写入点（SCE2 加载）

`0x01006204`：校验魔数 `SCE2`，分配后：

```text
adds r6, r5, #(3<<9)       ; obj+0x600
str  r0, [r6, #0x20]
… alloc …
str  r0, [r6, #0x1c]       ; @0x0100629A  ★ 与上表读取同一槽
```

场景 init `0x01046C60` 另将 mapctrl 存 `scene+0x1C34`，并把 vt 装好；**SCR
刷新路径用的是 `[screen+0x60]+0x600+0x1c`，不是盲目猜战斗包字段。**

### 可检验命题（待运行时）

1. 梦境 `4/1` 后走到 `0x01014E54` 且 `mapCtrl==0`，野外 `4/5` 同点非 0。  
2. 梦境热壳未再执行 `0x0100629A`（SCE2 未写入槽），或写入后又被清空。  
3. 禁止再用包形试错绕过；修复点须落在「谁应填充/保持该槽」的协议或进图契约。

探针：`main.c` `cbe_mapctrl_probe` @ `0x01014E5C` / `0x01046C2C` /
`0x0100629A` / `0x01017DF6` / `0x01017EE6`；fault 时 `cbe_setmapctrl_fault_chain`。

## 2026-08-03 试验：梦境 SCE2 改野外壳标志（debug-0b451a）— **已否定并回滚**

对照 `23蟠龙寨_01.sce`：野外壳 `SCE2 90 01 90 01`；原梦境 FB 壳 `SCE2 00 02 40 01`。

| 项 | 结果 |
|----|------|
| 修改 | `_01.._04` 标志改为 `90 01 90 01`（bin+web） |
| 复现 | 与改前同形：梦境 `F094` 进、**无** `F6A0`/`load64`；`slot64=0`；遇怪 `mem_fault 0x01046C48` |
| 裁决 | **否定**「仅改 SCE2 壳标志即可加载战斗层」 |
| 回滚 | 已从 `_bak_dream_sce_outdoor_shell_try/` 恢复 `00 02 40 01` |

下一探针：`F094` 记录类型 `r7`（是否出现 top-level type=4）。

## 2026-08-03 复现：`b_29` 门闩空名（debug-0b451a）

同局证据（使者进梦 → 踩怪）：

| 观察 | 日志 | 裁决 |
|------|------|------|
| 临安 `c04` ScreenInit 走过门闩 | `cbe_battle_map_gate sce=(empty-bmap) flag1=0`，**无** `load64` | `+0x74` 指针非空但首字节 0 |
| 梦境热壳 ScreenInit | 仅 `LOADER sceMatch=60`；**无** `cbe_battle_map_gate` | 战斗层加载整段未跑 |
| `screen_init_ok` 梦境 | `target=29* slot64=0 mc18_64=0` | 壳可走但无战斗 mapctrl |
| 踩怪 | `4/5` → `pc=0x01017EE6 mapCtrl=0` → `mem_fault 0x01046C48` | 用 `+0x64` 空槽崩 |

CBE / 资源交叉：

- `0x0100F6A0`：SCE 字段 type `0x12` 字符串 → `screen+0x74`
- `0x0100F6E6`：若 `[+0x74]` 非空则 `bl 0x010352AE` 填 `screen+0x64`
- `29梦境空间_01.sce` 传送段已含 type `0x12` / len16 = `b_29梦境空间.sce`（文件不缺）

| 假设 | 内容 | 状态 |
|------|------|------|
| X | 门闩因空名跳过 → 无 `load64` → 遇怪 `mapCtrl=0` | **确认** |
| Y | 梦境热壳未跑 SCE portal/type0x12 解析（相对临安曾进门闩） | **待探针** `F094`/`F6A0`/`DA00` |
| Z | 磁盘缺 `b_29*.sce` | **否定**（167/434 字节在位） |

## 2026-08-03 运行时：遇怪崩在 `+0x64` 槽（非进图）

同次 debug-0b451a（使者进梦 OK 后踩怪）：

```text
dream_instance_enter drain=1 resp=102 → screen_init_ok hot518=1 block_dream=1
loading_clear 29* rem→0
4/1 dream remap wire=200 → challenge_battle_start subtype=5 resp=203
cbe_mapctrl_probe pc=0x01017EE6 mapCtrl=0          # ★ [screen+0x64]+0x600+0x1c
SetMapCtrl r1=0 → mem_fault pc=0x01046C48 lr=0x01017EF9
fault_chain: slot_mapCtrl(+0x60)=88149964 仍有效；scene_1C30=0
```

CBE 对照：

| 站点 | 源对象 | 含义 |
|------|--------|------|
| `0x01014E5C` / `0x01017DF6` | `[screen+0x60]` | 场景 map（进图后非空，本局仍有效） |
| `0x01017ED4`/`0x01017EE6` | `[screen+0x64]` | 第二 map 对象；遇怪刷新走这里 |
| 模块构造 `0x01000166/17c` | 工厂分别填 `+0x60` / `+0x64` | 两槽在 Screen 构造时创建 |

**首个偏离**：梦境热壳在 `4/5` 送达后调用 `0x01017ED4` 时，`+0x64` 的
`+0x600+0x1c` 仍为 0；野外同路径该槽非空（既有对照局）。不是 `27/12`、也不是
`0x010136B3` 进图族。下一步：CBE/运行时查野外何时 SCE2/填充 `+0x64` 槽，梦境缺哪步。

## 2026-08-03 续：户外战后进梦 → stale `slot64` → `0x01005474`

`debug-0b451a`（使者进梦 `hot518=0` 复用壳后踩怪）：

```text
outdoor 4/5 → 0x01017EE6 slot64=88154276 SetMapCtrl OK → mmBattle
… teleport c04 … dream_instance_enter … screen_init_ok hot518=0
dream 4/5 → 0x01017EE6 同 slot64=88154276 → mem_fault pc=0x01005474 fault=40
```

CBE：`0x0100629A` 是唯一 `str` 到 `obj+0x600+0x1c` 的站点；运行时该站
`mapBase` 从未等于 `screen+0x64`（待 `sceMatch` 探针确认）。  
`0x01005474`：`ldr` from `[mapctrl+0x18]` 表；`fault=40` 像把小整数当指针。

| 假设 | 内容 |
|------|------|
| Q | `+0x61c` 由非 SCE2 路径写入（watch `cbe_mapctrl_slot_write` 抓 pc） |
| R | 户外开战时 `mc18` 合法，梦境开战同指针 `mc18==40`（已释放/覆写） |
| S | 梦境 `hot518=0` ScreenInit 不碰 `base64/slot64`，复用户外战斗 mapctrl |
| T | SCE2 `sceMatch` 永不为 64；`+0x64` 槽另有填充契约 |
| U | 成功标准仍是切入 mmBattle，不是仅避开 SetMapCtrl 空指针 |

### 假设裁决（debug-0b451a 户外双战斗→c04→使者进梦）

| 假设 | 裁决 | 证据 |
|------|------|------|
| Q | **确认** | `cbe_mapctrl_slot_write pc=0x0100D7C4`（函数 `0x0100D6E2`）；SCE2 `sceMatch` 恒 0 |
| T | **确认** | 同上；桃花岛 ScreenInit 连续 WRITE slot60+slot64 |
| R/S | **确认（提前到 c04）** | 户外开战 `mc18_64=88154580`；`c04` INIT 后仅 WRITE slot60，`mc18_64=65540`，指针仍 `88154276` |
| 进梦 | **本局偏到 ScreenInit** | `dream_instance_enter` 后 `blx r6=0x83008300` → `screen_init_fail emu_err=11`（未到遇怪） |

CBE：`0x010136A8` `ldr r6,[r1,#8]`，`r1=r9+0x5c48` ⇒ 回调槽 `r9+0x5c50`。  
下一探针：`cbe_map_loader_enter`（`sceMatch` 60/64 + lr）、`cbe_screeninit_fptr_write`。

## 2026-08-03：户外战后使者进梦 ScreenInit 闪退（非遇怪）

`debug-0b451a`（用户：「进梦境就闪退」）：

```text
c04 INIT: LOADER sceMatch=60 only; mc18_64=65540  # 槽64未刷新
dream_instance_enter drain=1
FPTR write r9+0x5c50 = 0x0100DB83  via 0x0100DECA (合法装表)
LOADER sceMatch=60 lr=0x010352D7   # 仍无 64
BLX r6 = 0x05000000                 # ★ 槽已被改成 DF 基址
mem_fault pc=0x050003C8 lr=0x010136B3
screen_init_fail init=0x05199B25 emu_err=11 block_dream=1
```

| 假设 | 裁决 | 证据 |
|------|------|------|
| V 回调槽被 bulk 覆写 | **待抓** | 精确地址 watch 只见合法 `DECA` 写；其后读已是 `0x05000000` |
| W 仅 loader60 后槽坏 | **待抓** | 时间序：DECA → LOADER60 → BLX 坏；加 `after_str` + overlap watch |
| X 磁盘 SCE 非 kind3 | **排除** | web/bin `29*` = 418 + `e_ghostfire` |

成功标准：使者进梦 `screen_init_ok`，再谈遇怪 mmBattle。

## 2026-08-03：遇怪不闪退但不进战斗（soft-fail）

`debug-0b451a`（用户：「这次没闪退了,遇怪后没进战斗场景」）：

```text
dream_instance_enter → screen_init_ok hot518=1 slot64=0
F094 records: type3, type1(count=38), 27/31/0/0/24/22 — **无 type4**
type1 命中 F604 但 r7!=4 → 字段走 ee1e skip；**无** store74 / gate / load64
4/5 challenge_battle_start resp=203 已入队
其后 **无** 0x01017EE6 / mem_fault — 停在场景壳
```

对照解压后的 `01桃花岛_01.sce`：

| 项 | 桃花岛 | 梦境（改前） |
|----|--------|--------------|
| 战斗层记录 | top-level `04 00 1c 00 1c 00 04 00` + `03 00 12 00` + `b_01…` | `01 00 04 00 0e 00 26 00 04 00` + `03 00 12 00` + `b_29…` |
| F094 | type=4 → store74 → load64 | type=1 → skip → `slot64=0` |

**根因**：`b_29` 挂在 top-level type=1 命名传送头下，CBE 只在 type=4 把 field `0x12` 写入 `screen+0x74` 并加载 `+0x64`。缺战斗 mapctrl 时 `4/5` 不再走到 `SetMapCtrl`（相对早前空指针闪退，现为 soft-fail）。

**修复**：去掉多余前缀 `01 00`，使头变为 `04 00 0e 00 26 00 04 00 03 00 12 00…`（与桃花岛同形的 type=4 / count=4）。备份 `_bak_dream_sce_type4_portal_fix/`；catalog 已 bump。

| 假设 | 裁决 |
|------|------|
| H1 无 type4 → 无 load64 → 4/5 不进 mmBattle | **确认**（本局日志） |
| H5 梦境头故意/误用 type=1 相对桃花 type=4 | **确认**（解压 SCE 对照） |

## 2026-08-03：type4 进图闪退（debug-0b451a）

type4 头修复后使者进梦即崩：

```text
record type3 → type4 count=4
store74 sce=b_29梦境空间.sce
gate flag1=98 → load64 sceMatch=64
slot_write slot=64 value≠0          # ★ 战斗层已挂上
map_loader_return b_29
随后 record_type flag1=103 / type4 count=2560   # ★ 过 EOF 脏读
fptr 被 size=1 连续写坏 → blx r6=0x04AD5542
mem_fault pc=0x04AD5542 lr=0x010136B3
screen_init_fail
```

| 假设 | 裁决 |
|------|------|
| H1 load64(b_29) 本身崩 | **否定**（slot64 已写入并 return） |
| H2 store74 前字段错位 | **否定**（bmap 名正确） |
| H6 type4 正确吃到 EOF 后 F094 仍继续；脏 type=4/count 大 → 写坏 `r9+0x5c50` | **确认** |

对照：旧 type=1 用错误 fieldCount=38 skip，过读后尾部记录多为 count=0，进图侥幸不崩，但永不 load64。

**修复**：保留 type4；在记录后追加 8×`(type,aux,aux,count)=0` 填充，让剩余 F094 迭代走空 skip。备份 `_bak_dream_sce_type4_eof_pad/`。

## 2026-08-03：进梦+遇怪验证通过 → 后台场景配置落地

`debug-0b451a` 验证：

```text
dream screen_init_ok hot518=1 slot64≠0 bmap=b_29梦境空间.sce
challenge_battle_start subtype=5 resp=203
0x01017EE6 mapCtrl=slot64 非空；无 mem_fault / screen_init_fail
```

后台（游戏内容管理）已对齐同一契约，避免 admin 再存回 type=1：

| 点 | 变更 |
|----|------|
| `vm_net_mock_sce_find_named_portal_start` | 识别 peach/dream **type=4** 哨兵，兼容 legacy type=1 |
| `vm_net_mock_sce_normalize_battle_named_portal` | type1→type4；终端 portal 后写 64B EOF 空记录 |
| `vm_net_mock_sce_combat_spawn_admin_save_all` | FB 场景保存后强制 normalize |
| 管理页具名传送 | 解析并展示 `battleMapSce`（field 0x12）；只读卡片 |
| `scripts/inject_dream_sce_kind3.py` | `normalize_battle_portal_contract` 与 C 同形 |

### overlap 续测：腐坏在 `after_str` → `blx` 之间

```text
c04: AFTER_STR fptr=0x0100DB83 → BLX 仍好
dream: AFTER_STR fptr=0x0100DB83 → BLX fptr=0x05000000
精确/窄 overlap watch 未见写 0x05000000
```

腐坏发生在 map loader `0x0100D7C6` 之后至 `0x010136A8` 之前（同毫秒）。  
下轮：`before_d144` / `loader_return` / `pre_blx_loop` 采样 + 宽窗仅在 `cur` 变化时记。
