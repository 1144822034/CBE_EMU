# 玩家仓库：商城耐久凭证 + 26/1 对话框存取

## 需求

玩家使用商城道具打开「仓库」：取回 / 存入做成类似 NPC 药品购买的对话框。
凭证有耐久，耗尽后需重新购买才能再用。

## 为何不走原生钱庄

`2026-07-25-warehouse-bank-protocol-unresolved.md`：钱庄 WT / UI / 805 扩容契约仍
unresolved。按 `AGENTS.md` 不能伪造钱庄 kind/subtype。

本实现复用**已证实**的 NPC 服务对话框契约：
`task_hall_activate_selected_entry` `action=1` → `26/1 {type=2,id=value}`，
与药品/武器商人同一 parser（见 `2026-07-19-dynamic-npc-services.md`）。

## 契约

| 项 | 值 |
| --- | --- |
| 商城道具 | `834` 仓库凭证（`item.dsh` 新增；酷宝价 50；**类别 10**） |
| 耐久 | 背包行 `item_count`，购入 `50`；每次使用扣 1 |
| 打开 | 使用响应主 CBMR：`7/1+7/7+7/11`；同连接第二 CBMR：lone `26/1`（`flags|=HAS_FOLLOWUP`） |

### 2026-07-27 使用无反应（已修）

**根因：** 首版把 834 写成 `类别=14`。运行时证据（801）表明背包对多数类别 14
道具本地拒绝使用（「不能直接使用」），**不会发出** `7/1`，服务端自然无日志。

**修复：** `item.dsh` 改为 `类别=10`、`是否消耗=1`。

### 2026-07-27 卡进度条 / 不弹仓库（多次迭代）

| 方案 | 结果 |
| --- | --- |
| 同 WT 包 `7/1+…+26/1` | 进度条卡住（loading 与 kind-26 同事件） |
| 同包 + 客户端 WT peel | 新 APK 可；旧 APK 仍卡 |
| 仅 `scene_sync_poll` 投递 `26/1` | 清 loading；公网 poll 失败/backoff 时不弹仓 |
| **现行：同 TCP 第二帧 CBMR** | 主包清 loading；第二帧 lone `26/1`；不依赖 poll |

**现行部署要求：** 服务端与客户端（`network-client` / Android jni）需一起更新。
仅更新服务端时，旧客户端读完主 CBMR 即关连接，第二帧被丢弃，仓库不弹
（但不应再卡进度条）。poll 仍作 wire 未取走时的兜底。

日志期望：
1. 服务端 `mock_warehouse_pass_use ... dialog=wire-26/1`
2. 服务端 `mock_warehouse_dialog_wire ... evidence=second-CBMR-lone-26/1-after-7/1`
3. 服务端 `flags=… followup=<26/1 len>`
4. 客户端 `queue_data ...` 后紧跟 `queue_data_followup ... resp=<26/1 len>`

### 2026-07-27 存入提示「服务请求无效」（已修）

**根因：** `mock_warehouse_persist_insert` 用 `queryLen > 110` 判断是否加逗号，但
`INSERT ... VALUES` 前缀本身已超过 110，首行变成 `VALUES,(CAST...)`，MySQL 1064。

**修复：** 用 `valueCount` 决定逗号；空值集直接跳过 INSERT。

### 2026-07-27 存取后停留界面（体验）

存取成功不再回根选项终态文案（会关对话框），改为刷新当前取回/存入列表并
提示「请继续选择」。会话内继续操作不扣 `834` 耐久（仅打开时扣 1）。
| 取回 | 选项 value `0xEF000000\|slot`：仓库行 → 背包 |
| 存入 | 选项 value `0xF0000000\|backpack_seq`：背包行 → 仓库表 |
| 容量 | 服务端 `64` 格，对话框每页 5 项 |

## 修改点

- `src/server/mock_server_catalog.c`：834 → `7/1+7/7+7/11` + arm pending
- `src/server/mock_server_scene_sync.c`：`take_warehouse_dialog_wire_followup` + poll 兜底
- `src/server/mock_server_transport.c` / `mock_server_core.c`：`HAS_FOLLOWUP=0x2` 第二 CBMR
- `src/network-client.c` 与 Android jni：读第二 CBMR → `queue_data_followup`
- `bin/.../item.dsh` 与 `JianghuOL/.../item.dsh`：道具行

## 验证

1. `make -j2`；部署服务端；**重编并安装 APK**（或 PC client）
2. 使用 834：进度条消失并弹出仓库
3. 日志含 `mock_warehouse_dialog_wire` 与客户端 `queue_data_followup`
4. 存入/取回正常

## 风险

- 旧客户端不读 `HAS_FOLLOWUP`：不卡 loading，但不弹仓（需升级客户端）
- 公网 poll 仍可能失败；现行主路径不再依赖 poll
- 装备存入后主界面幽灵行仍可能需另案
