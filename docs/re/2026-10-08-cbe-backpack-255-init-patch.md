# CBE 背包 InitTimerControl 扩到 255

日期：2026-10-08  
范围：二进制 + 主机 MemoryBlock 尺寸字段 + 下发 clamp。

## 目标

CBE 侧可用格逼近 Pixi 的 256。协议本身不需要新 opcode；硬顶在客户端：

- actorinfo 容量仍是 **tagged u8** → 逻辑上限 **255**（不是 256）
- 真机 MF 池 `0x14000` 装不下 `324 * physical`（physical=255 → 82620）
- 流读容量曾用 **`ldrsb`**，≥128 会符号扩展成负数

## 崩溃证据（已排除路径）

| 现象 | 根因 |
|------|------|
| `pc=1031ef4`，item 数组空 | physical 过大，MF `Malloc` 失败 |
| `r1=ffffff80`（-128） | 容量字节 `0x80` 经 `ldrsb`/`ldrsh` 符号扩展 |

## 当前补丁

| 位置 | 值 |
|------|-----|
| `0x01033ABC` `ldrsb r0,[r0,r3]` → `ldrb` | LE `c0 56` → `c0 5c` |
| `InitTimerControl` physical（`0x010038A2`） | **255**（`ff 22`） |
| `InitTimerControl` logical（`0x010038A4`） | **255**（`ff 21`） |
| `codeSign` | 重算 |
| `vm_initMemoryBlock`：CBE 传入 `0x14000` 时 | 主机把 **size 字段** 提到 `0x40000`（池本身已是 4MB） |
| `VM_CLIENT_CBE_BACKPACK_SLOTS` | **255** |

## 备份

`bin/CBE_bak_backpack/`（含原版、127/150、本轮 `bak_to255_*`）

## 验证

1. `make -j2`，**重启** CBE（勿热更旧 `.cbe`）
2. 登录日志应见 `initMemoryBlock grow 0x14000 -> 0x40000`
3. 不应再 `pc=1031ef4` / `r1=ffffff80`
4. 背包逻辑顶 255；服侧 256 经 clamp/`u8` 变成 255
5. `cbe_actorinfo_cap` / 17/1 / 30/21 行数 ≤255

## 续记：登录「解包错误」+ 背包空（同日）

### 现象

`has_17_1=1 resp=9838`，但**没有** `cbe_backpack_clamp object=17/1`；UI「解包错误」，背包无物。

### 根因

`VM_CLIENT_CBE_BACKPACK_SLOTS=255` 后，`17/1` compact 改写仍用 **`malloc(2048)`**。  
compact 行约 17 字节 → 约 120 行后 `truncate` 失败 → rewrite **静默失败** → **原样投递** live/Pixi `17/1`（i16 行数 + gem trailer）。  
`mmGame:0x418C` 把 `iteminfo` 拷进 **1024** 字节流缓冲 → 解包错误；列表不刷新。

### 修复（模拟器）

- `17/1` scratch → `8192`；object rewrite cap 同步加大  
- 列表行数夹到 **`(1024-3)/17 = 60`**（流缓冲契约；`maxnum`/Init 仍可 255）  
- rewrite 失败打 `cbe_17_rewrite_failed`；禁止透传，改投空 compact `17/1`  
- 写满 scratch 时改为截断保局部列表，不再整段 abort  

### 续记：mmGame 流缓冲 1024→8192（同日）

`sub_418C` 栈布局（证据：`0x051842A4`..`0x051842BE`）：

| SP 偏移 | 用途 |
|---------|------|
| `+0x704` | iteminfo 拷贝缓冲 |
| `+0x704+0x400` | stream reader（`movs r6,#1; lsl #10`） |
| `+0xB00` | 局部 context（`movs r3,#0xb; lsl #8`） |
| frame | `add sp, #-2892` |

补丁（`mmGameMstarWqvga.cbm`，备份 `bin/JHOnlineData_bak_stream8k/`）：

- frame **-2892 → -10060**（+0x1C00）
- context **`#0xb` → `#0x27`**（12 处）→ base `0x2700`
- stream 基址改读字面量 **`0x2304`**（洞 `0x05184574`），再 `+0x400` → **`0x2704`**
- 缓冲跨度 `0x2704-0x704 = 0x2000`（8192）；`r6=1024` 仍用于 `manager+0x400`
- `codeSign` 重算；主机 `ITEMINFO17_STREAM_*` → 8192 / 255 行

### 续记：无解包错误但仍空包（同日）

日志：`17/1 rows_kept=129` 成功，登录 `30/21 gridnum=0 iteminfo_len=0`。  
背包可见格靠 **`30/21` → HandleItemGrid**；`17/1` 只补名称。

根因：`vm_client_build_backpack30_grid_cbe` 只认 **u32 seq**，live/Pixi 常为 **i16 seq** → rewrite 失败 → 旧逻辑 `keptRows==0` 时强制 `gridnum=0`，把登录播种清空。

修复：i16 seq；单行 full-attr 失败则该行降级 compact；整包再失败走 compact builder；**禁止透传** Pixi 原包（9960B→解包错误）。scratch 8192→32768。

### 仍未知

拷贝 callee（`[sb+0x2050]+0xc`）若内部仍 `min(len,1024)`，需再补。真·256 格仍受 actorinfo **u8** 限制。

## 续记：背包正常、装备栏空（同日）

### 现象

`30/21 rows_kept=129`、`7/7 rows_kept=1 item=50010 seq=8 count=60` 均成功，
但 CBE 装备栏无穿戴。

### 根因假设与主机修复

`sub_D04`：`type=1` → `+52` 背包；`type=2` → `+104` category 15（装备栏）。
主机原先只剥宝石 trailer、**不记不改 type**。Live 登录复合包可能把
wear 行仍标成 `type=1`；CBE 又不发 Pixi 的 `1/7/22` 补拉。

### 修改（`network-client.c` + JNI 同步）

1. 日志：`cbe_backpack_clamp object=7/7 type=… worn_shaped=… coerce_type2=…`
2. 行形态为穿戴（`itemId≥1000` 且 `seq∈1..8`）且 `type≠2` 时强制 `type=2`
3. 同包有 `30/21` 时把 `7/7` peel 成第二个 `event=7`
   （`cbe_login_equip_peel`），避免紧跟超大 HandleItemGrid 后立刻 `+104`

### 验证

1. `make -j2`，重启 CBE，重登
2. 日志应见 `type=2`（或 `coerce_type2=1`）与 `cbe_login_equip_peel`
3. 装备栏显示 seq=8 的 50010（或角色实际穿戴）

## 续记：登录加速 defer 与解包错误（同日）

### 现象

大包 defer 后仍慢；加载完「解包错误」；二次登录画面卡住。

### 根因

主机曾从场景启动包剥走大 `17/1`，留下孤立 `7/42`，再把 `17/1` 在背包 UI
未开时单独投递。`mmGame:0x418C` 契约被破坏 → 解包错误；状态异常后二次登录卡死。
HandleItemGrid 本身仍贵（日志 `deliver_ms≈5230`），挪时机不能消灭该成本。

### 修复（最终）

登录 `30/21` / 装备 `7/7` **都不得**从 group-type1 拆成单独 `event=7`。

证据（defer 已关后仍复现，`client_out` 22:30）：
- 首次：`resp=9058`（含 30/21）+ `queue_data_followup 114`（peel 的 7/7）
- 二次选角 `connect=3`：同样 peel 后停在 followup，画面卡住

- **关闭** `cbe_bag_defer` strip/flush
- **关闭** `cbe_login_equip_peel`
- 登录复合包保持 `5/10+30/21+7/11+7/7+…`；`7/7` 仍做 gem 剥离与 `type=2` coerce

登录大包 HandleItemGrid 仍慢（模拟器成本）；加速需另案，不能靠拆包。

### 验证

1. 重启 CBE：日志**不应**出现 `cbe_bag_defer` / `cbe_login_equip_peel`
2. 应有 `cbe_backpack_packet … list=…30/21…7/7…`（7/7 在同包）
3. 无「解包错误」；二次选角可进场景；背包/装备栏正常

## 续记：8748B 全属性 30/21 → 解包 + 装备空（同日）

### 现象（defer/peel 均已关闭）

`list=5/10,10/26,30/21,7/11,7/7,7/20,7/32`，`7/7 type=2` 改写成功，仍
「解包错误」且装备栏空；`deliver_ms≈4.6s` 跟在登录复合包后。

### 根因陈述

同包 `30/21` 全属性 `iteminfo≈8748`（129 行）落在 **8KB+ HandleItemGrid**
高成本档；guest 长时间卡在网格播种，后续同包 `7/7 type=2`（+104 装备栏）
与解析契约被破坏，表现为解包弹窗 + 装备不显示。背包格仍可能被部分播种或
靠后续 `17/1` 名称看起来「有东西」。

### 修复（已修订）

初版对超限 `30/21` 整包改 `compact-extra-size-cap`；`client_out` 2026-10-09
证实 compact 已生效（`3510B/130` 行）仍「解包错误」。与
`2026-10-05-cbe-login-30-21-compact-assert.md` 一致：HandleItemGrid 对
`attr=0` compact extra 会走 InGame.c:913。

修订：

1. 超限时 **按行裁剪 full-attr**（`mode=full-attr-row-cap`），`GRID30` 交付帽
   **4096**；禁止整包 all-compact。
2. 同 WT 顺序对齐 mock group-type1：`30/21 → 7/7 type=2 → 7/11`
   （`cbe_login_bag_order`）。**不要**把 `7/7` 放到 `30/21` 前（装备管理器未就绪）。
3. `17/1` 另加 `COPY_SAFE_CAP=1000`，防范拷贝 callee 仍 `min(len,1024)`。

### 验证

1. 日志：`mode=full-attr-row-cap`，`iteminfo_len` ≤4096；
   `cbe_login_bag_order`；`list=…,30/21,7/7,7/11,…`
2. 场景 `17/1` 若超 1000：有 `cbe_17_copy_safe_cap`，最终 `iteminfo_len`≤1000
3. **无** `compact-extra-size-cap` / `cbe_equip_before_grid30`
4. 无解包错误；装备栏有物；背包有格
5. 二次登录不卡

## 若还要真·256

actorinfo 容量字段需改成 **i16/u16**（客户端写 `+38` 与协议侧同步），且 `movs` 立即数装不下 256，Init 参数要用 `movw` 或等价扩指令空位。
