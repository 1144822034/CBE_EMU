# CBE 模拟器：背包下发截断为前 64 格

日期：2026-08-20  
范围：**仅模拟器端**（`src/network-client.c` / JianghuOL JNI 同名文件）；服务端与 Pixi 仍可持有/下发至 256。

## 触发

Pixi/服侧背包容量与行数可大于 64。CBE `InitTimerControl(64, …)` 只有 64 个逻辑槽；把完整 `17/1` / `30/21` 交给 CBE 时，超出部分不被认可（闪退或列表异常）。

## 契约

| 端 | 容量 |
|----|------|
| CBE 客户 | 硬顶 **64** 逻辑槽 |
| 服务端 / Pixi | 可达 **256**（`VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY`） |

## 修改

在 remote 响应投递进 VM 之前，`vm_client_cbe_clamp_backpack_packet`：

1. **`1/17/1`（带 `maxnum`，即角色背包列表，不含商店 17/1）**  
   - `maxnum` 夹到 ≤64  
   - `iteminfo` 行数（u8/i16 前缀）与行体只保留前 64 行  
2. **`1/30/21`**  
   - `iteminfo` 只保留前 64 行  
   - `gridnum` 改为实际保留行数（且 ≤64）

## 续记：scene-sync 装扮 31/x + 背包空（2026-08-24）

根因：升级服 scene-sync poll 会追加 Pixi 专用 `1/31/*`（附近玩家翅膀/坐骑/特效称号，
见 `docs/re/2026-08-17-pixi-cosmetics-1-31.md`）。CBE 不识别该对象，且单包对象数可 >10
→ `event_packet_parse_WT` 解包错误；同包内靠后的 `30/21` / `17/1` 也无法生效 → 背包空。

修复（仅 CBE `network-client.c`）：投递前 `vm_client_strip_cbe_unsupported_objects`
剥离 `1/31/*`，再 `vm_client_cbe_clamp_backpack_packet` 截断 256→64 格。
日志：`cbe_strip_unsupported` / `cbe_backpack_clamp`。

### 三次根因（同日）：17/1 行数 i16 → CBE 仍要 u8

日志已有 `cbe_backpack_clamp object=17/1 maxnum=256->64 rows_kept=23`，但背包仍空。
服/Pixi（codeVersion≥7）`iteminfo` 行数为 tagged **i16**；CBE `mmGame:0x418C`
仍读 tagged **u8**。旧 clamp 保留了 i16 前缀 → 客户端把行数解成 0。
已强制把 `17/1` 行数改写为 tagged u8（日志 `count_tag=i16->u8`）。

### 四次根因（同日）：30/21 gridnum 同为 i16

`17/1` 已改写后仍空；登录主路径靠 `1/30/21` 初始化 item manager，而服端
`gridnum` 已改为 tagged i16（防 256→0）。行数≤64 时旧 clamp **不改写**，
i16 `gridnum` 原样进 CBE → 网格数为 0。已强制 `gridnum` i16→u8，并增加
`cbe_backpack_packet has_17_1/has_30_21 list=…` 取证日志。

### 六次根因（同日）：登录 30/21 不可改

in-place `gridnum` i16→u8 仍触发同址崩溃（`pc=0x01031ef4`，`0x11a`），
登录组 `5/10+30/21+7/7… resp=1579` 卡加载。未改 `30/21` 时进场景正常。
**登录 `30/21` 原样透传**；仅 `17/1` 做 in-place 行数/maxnum 修补。
`30/21` 仅在 rows>64 时走 rewrite 截断。

### 十二次（同日）：-1 与 inplace 缩包

后台容量已是 64、日志也有 `maxnum_tag=i16->u8`，UI 仍 `-1`（`0xFFFF`）。
当时误判 `mmGame:0x418C` 读 u8 maxnum，把 `17/1 maxnum` 缩成 `hex=000140`
（tagged u8），反而让 `business+546` 保持 `0xFFFF`。

### 十三次（同日）：对照 D:/works/my/CBE_EMU

参考 mock 序列化（`mock_server_catalog.c`）：

- `maxnum` = `put_object_u16` → tagged **i16**（`00 02 00 40` = 64）
- `iteminfo` 行数 = `seq_put_u8` → tagged **u8**（`00 01 nn`）

CBE 正确契约是 **i16 maxnum + u8 行数**，不是 u8 maxnum。修复：

1. full-rewrite 时 `maxnum` 保持/转回 tagged i16（≤64）
2. 仅把 live i16 行数缩为 tagged u8
3. 已是 i16 maxnum + u8 行数且 ≤64 时跳过 rewrite

### 十四次（同日）：actorinfo 容量槽

日志已有 `17/1 … hex=00020040`，UI 仍不对。根因：`parse_actorinfo`
把 `actorinfo` 内 **u8 backpackCapacity** 写入 manager+38；live/Pixi
`1/1/6` 常省略该槽（live layout），导致容量 0 → 角标 `0/-1`。

在 `vm_client_prepare_cbe_downlink` 增加 `cbe_actorinfo_cap`：

- fresh layout：原位把 `00 01 xx` 容量改为 64
- live layout：在 money 后插入 `00 01 40`

## 验证

1. 选角日志：`cbe_actorinfo_cap object=1/1/6 … new=64`  
2. 开背包：`17/1 … hex=00020040`  
3. 开背包：`mode=compact-extra … iteminfo_len≈19*17`（约 19 行安全物品）  
4. 背包界面应显示物品名称列表（mmGame:0x418C + 本地 item.dsh）  

### 十五次（同日）：17/1 物品列表 compact-extra

容量/金币已修。列表仍空：live `17/1` 行带 Pixi 堆叠 hint / 阶段词条，
CBE `mmGame:0x418C` 按 `bootstrap-compact` 解析（`put_item_compact_extra`）。

`17/1` full-rewrite 改为：

- 丢弃 834/839/845/846 等 CBE 非网格行
- 每行重写为 `u32 itemId + i16 enhance0 + i16 max0 + u8 attr0`
- 日志 `mode=compact-extra dropped=4 rows_kept≈19`
