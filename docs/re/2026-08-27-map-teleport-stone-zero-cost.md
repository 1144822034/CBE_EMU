# 世界地图传送石取消背包道具要求（2026-08-27）

## 触发与首个偏离

用户反馈使用场景中的传送石时，客户端似乎要求背包中有一个传送石才能继续免费传送。
本次工作目录没有可归属此次点击的新 `net_trace.log`、`net_packets.log` 或 player-3 传送记录，
因此没有把缺失的运行时日志伪造成新证据。

现有协议实现和此前的客户端逆向已经固定了两个不同分支：

- 场景内 `n_telestone.actor`：`WT 1/16/1 -> 独立 1/16/2`，服务端已经返回
  `16/2 { result:u8=1, scene, posinfo, exitid }`，没有 `value` 字段；
- 世界地图传送石：`WT 1/16/4 {curid,objid}`，服务端此前返回
  `16/4 { result:u8=0, value:u32=1 }`。

后者是实际仍然会检查背包的最早边界。`value` 被固件原样传给
`ConsumeInventoryItem(0x01018F66)` 作为 `itemId=800` 的数量；数值为 `1` 时，无传送石会在
固件自己的“不足/购买”分支停止，后续 `16/2 + 16/3` 不会发送。

## 客户端与协议证据

`JianghuOL.CBE:SendItemUseReq(0x0103573A)` 发起 `16/4`，其响应仍由
`JianghuOL.CBE:0x010357E0` 交给 `HandleItemUseConfirm(0x010190A8)`。确认后该函数将
`value` 传给 `ConsumeInventoryItem(0x01018F66)`；正常的世界地图控制器清理和场景退出仍由
固件自己产生 `16/2 + 16/3`。

这与项目既有的取证一致：[`2026-06-25-teleport-stone.md`](2026-06-25-teleport-stone.md)
记录过 `value=0` 会令固件显示零个传送石，并且 `7/1` 道具使用对象只在非零数量时出现。
因此零数量是保持原生确认和地图刷新路径、同时取消背包条件的最小服务端契约；不能由宿主
伪造 `16/2`、`16/3`、库存或场景进入来替代。

## 修复

`vm_net_mock_append_teleport_stone_map_confirm_object()` 现在发送：

```text
16/4 { result:u8=0, value:u32=0 }
```

这保留固件管理的确认回调与世界地图控制器收尾，但令 `ConsumeInventoryItem` 的请求数量为零。
空背包确认后，固件发出的组合请求为：

```text
16/2 {exitID,type} + 16/3 {exitID,type}
```

不再有 `7/1` 道具使用对象，服务端仅回空 WT 确认并安排既有的下一网络事件发送 `30/1`。
场景内独立 `16/2` 免费直达与任务传送 `16/6` 的既有费用契约没有改动。

## 证据记录

```text
phase: world-map teleport-stone free confirmation
status: implemented and regression-validated

request:
  wt_kind: 16
  wt_subtype: 4, then combined 2+3
  objects: 16/4{curid,objid}; 16/2{exitID,type}+16/3{exitID,type}
  key_fields: value is the item-800 count owned by the CBE confirmation path
  sample_len: 37-byte 16/4 response; 5-byte empty acknowledgement after 2+3
  packet_log: builtin-teleport-stone-map-confirm, builtin-teleport-stone-confirmed-exit-combo

response:
  wt_kind: 16/4, later scene channel
  wt_subtype: 4, then 30/1
  objects: 16/4{result=0,value=0}; deferred 30/1{scene,posinfo}
  fields: exact sMap scene key and SCE-derived landing remain unchanged

ida_evidence:
  binary: JianghuOL.CBE
  function: SendItemUseReq(0x0103573A), HandleItemUseConfirm(0x010190A8), ConsumeInventoryItem(0x01018F66)
  dispatch_case: world-map item confirmation followed by native scene-exit requests
  parser_reads: value is stored as the local item-800 quantity
  failure_branch: nonzero value with no item 800 enters the native insufficient/purchase branch

runtime_evidence:
  trace_lines: mock_teleport_stone_map_confirm ... value=0; mock_teleport_stone_confirmed_exit_combo ... item=0
  handled_source: builtin-teleport-stone-map-confirm, builtin-teleport-stone-confirmed-exit-combo
  queued_event: existing deferred scene-channel event
  client_effect: empty backpack can complete the normal world-map transfer protocol without 7/1

negative_evidence:
  missing_or_bad_field: prior 16/4 value=1
  observed_failure: CBE required one item-800 instance before emitting 16/2 + 16/3

unknowns:
  - name: fresh player-3 visual capture for this policy change
    current_value: not captured in this isolated regression
    why_kept: regression proves protocol shape only; a user session remains the visual acceptance boundary
```

## 验证

`scripts/teleport-stone-scene-catalog-regression.c` 只读取正式 SCE/DSH 资源，不启动监听器、
不连接数据库、不写角色数据。它构造空背包角色并断言：

1. 场景内独立 `16/2` 仍是无 `value` 的免费直达；
2. 世界地图 `16/4` 返回 `result=0,value=0`；
3. 随后的无 `7/1` 的 `16/2 + 16/3` 被正常接住，返回空确认并保留一次 deferred scene entry。

