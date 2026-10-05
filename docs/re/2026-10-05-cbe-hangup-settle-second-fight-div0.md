# CBE 挂机结算后第二场 DIV0 闪退

日期：2026-10-05

## 触发与现象

CBE 挂机首场打完，`4/7` 挂机结算壳出现后闪退。用户体感是「第二场」。

`bin/client_out.txt`（空 WT 仍崩，2026-10-05 复测）：

```text
cbe_hangup_fdata_mark evidence=4/7.fdata-挂机中
cbe_hangup_combatinfo tally=1/65535 ... rewrite=0
queue_scene_poll connect=2 event=7 resp=718
cbe_uplink len=9 hdr=1/25/5 objects=1/25/5
queue_data connect=2 event=7 resp=5
Arithmetic exception: Divide By Zero
pc:104e980 lastPc:104e97c lr:1004cd7 r9:1050bd0
semihosting reason:18 arg:20026
```

无第二场 `hangup_loop_poll_deliver` 的 client 日志（`queue_data` 1s 节流），
但 25/5 ACK 紧挨 4/7。`resp=5` 是空 WT，不是 `25/5 result=4`。

## 预期 / 实际

- 预期：停在 `4/7`「挂机中」直到场间到期，下一场 `4/8` 拆壳后再单独 `4/5`。
- 实际：mmGame `scene_runtime_tick` 仍发空 `25/5`。先回 `result=4` 会崩；
  改空 WT 后仍崩。guest 此时仍是 mmBattle 结算壳。

## IDA / 反汇编

- `lr=0x01004CD7`：`JianghuOL.CBE:0x01004CC2` `rand_range(min,max)` 里
  `blx 0x0104D538`（`__aeabi_idiv`）的返回地址。
- `pc=0x0104E974..0x0104E980`：除零半主机桩 `mov r0,#0x18; svc #0x123456`。
- 栈上有 `0x01012E4D`（event 7 / Handle25 入口）以及 mmBattle `0x05xxxxxx`。
- 除数来自 `max-min+1` 或 `GetRandom()`；结算壳上跑 mmGame `25/5` 回调时为 0。

## 根因陈述

第一次偏离在 **25/5 等待被完成**：该 wait 只属于 mmGame 地图 tick。
`4/7` 挂机结算仍由 Battle.cbm 持有时，无论回 `1/25/5 {result=4}` 还是空 WT，
都会投递 event 7 → Handle25 → `rand_range` DIV0。崩溃 PC 是症状。

服务端 suppress 空 WT 不够：宿主仍把 ACK 交给 guest。

## 修改

| 位置 | 改动 |
|------|------|
| `mock_server_social.c` scene-default | 战中 / `AwaitingSettlement`：空 WT，不回 `result=4`（防御） |
| `mock_server_battle.c` settlement_exit | skip-4/8；下场 prepend `4/8`+`4/5` |
| `network-client.c` | 挂机结算壳期间：不上行空 `25/5`；丢弃空 WT ACK；仅在无 `4/5` 的地图 `4/8` 后补 `25/5 result=4` |

## 验证

1. 重启 CBE。挂机打完首场：结算壳保持；日志
   `cbe_hangup_swallow_25_5` 或 `cbe_hangup_drop_empty_wt_ack`，无 DIV0。
2. 场间后再开战：`hangup_loop_poll_deliver` prepend `4/8`+`4/5`，此时不 flush 25/5。
3. 停挂机 / 真回地图的 lone `4/8`：`cbe_hangup_flush_25_5`，地图 tick 恢复。
4. `make -j2` 通过。

## 2026-10-05 复测（空 WT 仍崩）

`cbe_hangup_drop_empty_wt_ack` 在 **4/12 空 ACK** 上误触发（settle_ui 在点挂机 2/10 已 armed）。
guest 仍等 4/12，随后 `queue_scene_poll resp=718`（含 4/7）被当成该 wait 解析 → DIV0。
栈上有 `0x2ce`（718）和 `0x01012E4D`。

修正：只在看到 4/7「挂机中」后吞空 25/5；**不要**在 2/10 武装期间丢 4/12 空 ACK。

## 2026-10-05 复测（吞 25/5 后仍崩）

`bin/client_out.txt`：

```text
cbe_hangup_swallow_25_5 ... evidence=div0-0x0104e980-Handle25-on-4/7-shell
queue_scene_poll connect=2 event=7 resp=5
Arithmetic exception: Divide By Zero
pc:104e980 lr:1004cd7
栈 0x01012E4D + event=7 + len=5
```

第一次偏离仍是 **完成 25/5 wait**：上行已吞，宿主 scene-poll 又把 mock
`suppress-battle-settle` 的空 WT 投进同一条 event 7。栈上 `len=5` 证实
payload 是空 WT，不是 4/7。

修正：`OnSettleShell`（4/7「挂机中」之后、4/8 之前）在
`vm_client_deliver_wt_response` 丢弃空 WT，日志
`cbe_hangup_drop_empty_wt_ack`。4/12 仍在开战阶段，此时壳标未置。
下场 `4/8+4/5` 非空，照常投递。lone `4/8` 后仍 `cbe_hangup_flush_25_5`。

## 2026-10-05 复测（吞 25/5 后仍崩，栈是 4/7 包）

`bin/client_out.txt`：

```text
cbe_hangup_47_packet objects=3 list=4/6,1/14,4/7 resp=718
queue_scene_poll ... resp=718
cbe_hangup_swallow_25_5
Arithmetic exception: Divide By Zero
pc:104e980 lr:1004cd7
栈 len=0x2ce (718) event=7 0x01012E4D
```

没有 `queue_scene_poll resp=5`。第一次偏离在 **挂机结算 WT 本身**：
`4/6` 致死刀 + Pixi 基线 `1/14` + `4/7` 同包。`1/14` 属 mmGame/mmShop
actor-state（`0x9DE`），Battle.cbm 仍占结算壳。`rand_range` 寄存器
`r4=0 r5=0xffffffff`（宽 0）。空 25/5 是结算解析过程中 mmGame 仍在 tick。

权威旁证：`append_battle_status7` 已写明 exit-prime 不得在 Battle 内发
`1/14`（scene_poll resp=623 地址错）。挂机 skip-4/8 让该禁令覆盖 4/7 壳。

修正：挂机结算不再附 `1/14`；CBE 下行 `cbe_hangup_strip_actor14` 再剥一层。
Pixi 非 `sceneHangupEnabled` 仍带基线 `1/14`。

## 2026-10-05 宿主：除 0 当 1

用户要求除数为 0 时按 1 处理，不再闪退。

`rand_range` 在 `max=0xffffffff min=0` 时 `max-min+1` 回绕为 0，
`__aeabi_idiv` r1=0 → `__rt_div0` SYS_EXIT。崩溃 PC 仍是症状。

| 位置 | 改动 |
|------|------|
| `hookRam.c` `vm_host_aeabi_div0_divisor_one` | `0x0104D538` / `0x0104D610` 入口 r1=0 → r1=1 |
| `hookCpuIntr` | `0x0104E97C` SYS_EXIT 0x18 回 LR，不 assert |

验证：挂机结算壳停留数秒不闪退；日志 `aeabi_div0 divisor0->1`，无 `Arithmetic exception` assert。

## 2026-10-05 第二场进战 UC_ERR_MAP

`rand_range` 已不再 DIV0。第二次进战：

```text
cbe_hangup_flush_25_5 ... evidence=4/8-map-then-mmGame-25/5-result4
queue_scene_poll resp=259
cbe_uplink hdr=1/4/1
queue_data resp=231
地址无法访问:c ... pc=1004e36
SCR_Render UC_ERR_MAP
```

第一次偏离在 **把 4/8+4/10 当成回地图**：CBE 下场开战是 poll-synth `4/10`（无 live-node，不是 `4/5`）。宿主只对 `4/5` 抑制 flush，于是补了 `25/5 result=4`，mmGame 再发 `4/1`，与同包 `4/10` 叠进，font 对象 `+0xc=0`。

修正：`4/8` 同包有 `4/5` 或 `4/10` 时 `cbe_hangup_keep_25_5`，不 flush。仅 lone `4/8` 才回地图 25/5。

## 2026-10-05 第二场仍崩：4/1 叠场

keep_25_5 之后仍：

```text
cbe_hangup_keep_25_5
queue_scene_poll resp=259
cbe_uplink hdr=1/4/1
queue_data resp=231
UC_ERR_MAP pc=0x01004E36
```

第一次偏离在 **4/8 回地图后 mmGame 又发 4/1**：宿主转发后服务端再给一场 231，压在已下发的 4/10 上。CBE 挂机下场由 poll `4/10` 开战，不是地图 `4/1`。

修正：`hangupNativeMode==2` 时不上行 `4/1`，空 WT 结束该 wait（`cbe_hangup_swallow_4_1`）。

## unresolved

`game+1136` 原生 `sub_7794` 累计壳仍未闭环。
`rand_range` 宽为 0 时结果固定为 min（除数当成 1）。
