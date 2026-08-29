# 背包打开后的地图控制器崩溃调查

Date: 2026-08-18

Status: root cause confirmed; protocol-boundary fix implemented, runtime validation pending.

## 触发与原始证据

账号 `21642502`、角色 `10036` 登录后尝试打开背包，客户端在场景移动帧崩溃：

```text
pc=010469AE lr=01035265 r0=13121110
```

`UpdateSpriteMovement(0x010469AC)` 的首条读取为 `LDRH R1,[R0,#6]`。
其回调 `UpdateSpriteMovementCb(0x01035258)` 先读取 `Global_R9+0x9540`，再将该值
传入。因此 `0x13121110` 是已经损坏的地图控制器指针，崩溃 PC 是后续症状，不是可以
直接加空指针兜底的修复点。

同一次服务端会话的顺序为：

```text
30/21 gridnum=62 iteminfo_len=1674
7/11 reservoir rows=3
7/7 type=2 equipped rows=8
scene-task-subset-followup resp=1917
17/1 rows=62 iteminfo_len=1057 layout=bootstrap-compact
```

客户端主物品管理器的只读观察显示：8 件已装备记录进入类别 15 后，逻辑计数仍为
`62/64`，物理记录占用为 `69/74`。因此没有物理槽耗尽，不能将本次崩溃归为旧的
TimerControl 空槽路径。

## 已排除的假设

`17/1` 的紧凑载荷长度为 `3 + 62 * 17 = 1057` 字节，值得关注，但尚不是根因证据。
对 `mmGameMstarWqvga.cbm:sub_418C(0x418C)` 的 Thumb 反汇编确认：函数以约
`0xB4C` 字节栈工作区初始化 reader，并按已读取的行数乘 `0x145` 分配行列表；不能把
`1057 > 1024` 直接推断为该路径的固定缓冲区溢出。此前 1024 字节限制仅有商城目录
上下文的记录，不可无证外推到当前带 `maxnum` 的背包列表对象。

同时，当前 `1917` 响应的对象数为已确认允许的 10，尚无外层 WT 对象数或字段数越界
证据。

## 当前取证

新增可删除、只读的宿主探针：

- `src/hookRam.c` 观察 `Global_R9+0x9540` 的每次客户机写入，记录 PC、LR、地址、
  写前值和写入值；
- `src/main.c` 在 `UpdateSpriteMovement(0x010469AC)` 前读取该槽位快照。
- `src/main.c` 在 `fmt_sprintf_like(0x0104D744)` 入口检查输出目标；若为
  `Global_R9+0x9540`，记录格式串地址、有限格式串十六进制、调用者 LR、变参寄存器和 SP。
- `src/hookRam.c` 在通用 `WriteByteToStream(0x0104E0F8)` 写入该槽时记录
  `R0-R3` 与有限栈快照；它用于与入口调用者交叉核对。
- 同一写入点还利用 `R1` 指向的 formatter 游标单元反推
  `fmt_sprintf_like` 外层栈帧，记录最初输出目标、当前游标、业务调用者 LR、格式串、
  R2/R3 参数及格式串前 48 字节。即使输出从槽位之前开始并越界写入，也能捕获调用者。

二者只读取寄存器/客户机内存并向当前客户端 profile 的
`logs/map-controller-forensics.log` 追加诊断行；不写 CBE/CBM 内存，不改变 PC、LR、
寄存器、协议响应或输入时序。

IDA 已确认 `江湖OL.CBE:0x0104D744` 是 `fmt_sprintf_like(dest, format, ...)`，
`0x0104E0F8` 只是按 `R0` 写一个字节并递增 `dest` 游标。运行时上下文进一步证明业务调用的
初始目标是 `Global_R9+0x94E0`，不是地图控制器槽；格式化结果超过 96 字节后才写穿到
`Global_R9+0x9540`。因此移动更新函数和地图控制器初始化都只是受害者。
`mmGameMstarWqvga.cbm:0x418C` 的 `17/1` 分支读取 `iteminfo`，先用约 `0x400` 的
栈工作区建立 reader，再按行数乘 `0x144` 展开行并访问 `item.dsh/equip.dsh`；当前
`iteminfo_len=1057` 恰为 `3 + 62 * 17` 的紧凑 tagged-row 格式，长度本身仍是未知项，
不能作为修复依据。

复测需保留 `world_chat_history_normalize`、`chat_notice_deliver` 和地图控制器探针日志。
角色 `10415` 的旧 81 字节历史正文应在历史回放边界裁为不超过 79 字节，且不应再出现
`caller=01034df3` 随后写入 `Global_R9+0x9540` 的记录。取证代码只读取寄存器/客户机内存，
不改变 PC、LR、协议响应或输入时序；确认回归通过后应删除或严格收窄这些临时探针。

## IDA 目标与当前边界

| binary | function/address | finding | confidence |
| --- | --- | --- | --- |
| `江湖OL.CBE` | `fmt_sprintf_like(0x0104D744)` | `R0` 是输出目标，`R1` 是格式串，内部调用 `fmt_long_to_string(0x0104D7CC)`。 | high |
| `江湖OL.CBE` | `WriteByteToStream(0x0104E0F8)` | `STRB R0,[R2]` 后递增 `[R1]`；崩溃日志中的 LR `0x0104DAF3` 位于格式化器内部循环。 | high |
| `mmGameMstarWqvga.cbm` | `sub_418C(0x418C)` | `17/1` 读取 `maxnum/iteminfo`，按行展开并调用本地数据表方法；未证明固定 1024 字节溢出。 | medium |

## 已确认根因与修复

新探针在原始复现中记录：

```text
map_controller_writer_context caller=01034df3 dest_initial=0105a0b0
format=01034ec4 arg2=0500f608 arg3=0500f618
format_head=25-73-3a-25-73-00
```

`caller=0x01034DF3` 属于 `BuildChatChannelStr(0x01034D84)`，等价调用为
`sprintf(Global_R9+0x94E0, "%s:%s", row+12, row+28)`。输出目标
`Global_R9+0x94E0=0x0105A0B0` 到地图控制器槽 `Global_R9+0x9540=0x0105A110`
只有 96 字节。运行日志同时证明登录历史下发了一条角色 `10415` 的 81 字节世界消息。

`net_handle_type_payload_detail(0x010126C6)` 的正文临时区为 80 字节，先清零后按 wire
长度 `mem_copy`，不会复制终止符。80 字节会填满正文区并让 `%s` 继续读取相邻名字；
81 字节还会覆盖名字首字节。本次 `0x10,0x11,0x12,0x13` 正是格式化器越过 96 字节
显示缓冲后读到的相邻聊天行元数据。打开背包只是与登录历史分批投递重叠的触发时机，
背包 `17/1` 数据不是首次偏离。

服务端现统一采用 79 字节正文契约：聊天队列、下行 builder、世界消息持久化、普通聊天
和宝箱世界公告均按完整 GBK 字符规范化到 79 字节。旧数据库中已有的 80/81 字节历史
在回放边界安全裁剪并记录 `world_chat_history_normalize`；新表 schema 使用
`VARBINARY(79)`。修复不修改 CBE 内存、寄存器、PC/LR 或背包响应。

隔离回归 `scripts/chest-world-broadcast-regression.c` 不启动监听器、不连接 MySQL；除原有
黄金宝箱 GBK 模板外，现断言 79 字节正文原样保留、旧 81 字节正文裁为 79 字节，以及
79 字节边界处不会截断 GBK 双字节字符。

### 验证边界

协议修复门槛已由三方证据满足：运行时探针给出 `caller=0x01034DF3` 与 `%s:%s`，IDA
确认聊天行构造器和 80 字节正文临时区，服务日志给出唯一的 81 字节历史消息。当前仍需用
新构建复测登录、历史投递和打开背包路径；仅构建成功不能将状态提升为 validated。

### 2026-08-28：取证清理

本文件记录的地图控制器 writer/tick/formatter probe 已完成其用途。它们于 2026-08-28
从 `src/main.c` 和 `src/hookRam.c` 移除，不再安装对 `Global_R9+0x9540` 的全局写入观察，
也不会再生成 `logs/map-controller-forensics.log`。本段之前的日志与地址仅保留为历史根因
证据；今后若出现新的地图控制器故障，必须以新的最小、显式启用的观察重新取证，不能恢复
无条件热路径日志。
