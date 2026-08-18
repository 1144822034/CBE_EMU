# 普通药品使用后的分组数量错位

日期：2026-08-18  
状态：implemented，待真实客户端复验

## 复现

背包中同一物品存在两组 `20`、`16` 时，从 `20` 组使用一个，旧实现会显示为
`9`、`12`。服务端数据库只扣减了请求序号对应的物理行，因此数据库日志看似正确，
但客户端显示数量已经偏离。

## 首次偏离与证据

运行时服务端曾记录：

```text
before: seq=16 count=9
before: seq=17 count=10
before: seq=18 count=13
before: seq=19 count=4
request: selected_seq=18
after:  seq=18 count=12
```

IDA `江湖OL.CBE:0x01032EB8 TimerControl_ProcessItem` 显示主背包记录是连续
`324` 字节；category 15 插入时按 `itemId` 合并。达到堆叠上限时，固件在
`0x01032F76..0x01032F82` 交换现有记录和新记录的序号，再重新尝试插入。上述四个
服务端物理行进入固件后实际变成：

```text
visible seq=18 count=20
visible seq=16 count=16
```

所以请求中的 `seq=18` 是可见的 `20` 组，不是数据库中原来的 `13` 组。旧服务端
直接按数据库序号扣减并发送全量 7/11，首次错误状态就在服务端把“客户端可见序号”
误当成“持久化物理序号”之后产生。

## 修改

`src/server/mock_server_catalog.c` 新增
`vm_net_mock_role_consume_client_visible_stack()`：

1. 按固件 `TimerControl_ProcessItem` 的合并、满堆拆分和序号交换规则重建同一
   `itemId` 的客户端可见行；
2. 按请求序号在可见行中扣减；
3. 将重建后的可见行写回角色快照，再由现有 `7/4 + 7/11 + 7/37` 响应发送数量。

普通立即恢复药品不再直接调用持久化物理行的序号扣减。储备瓶、扩容卡和专用道具仍
保留各自已有协议。

客户端 `src/main.c` 的取证扫描同时修正为 `list + i * 324` 连续记录步长，并直接
读取记录首字段，避免把 itemId 当作指针。

## 验证

```text
make -j2                                  # 成功
normal-recovery-item-modal-regression.exe # 通过
```

隔离回归构造 `seq16=9, seq17=10, seq18=13, seq19=4`，选择 `seq18` 使用一次，
断言重建结果为 `seq18=19`、`seq16=16`，并断言 7/11 blob 只包含这两行。该测试不
替代真实 CBE 画面验收；仍需在客户端复测 `20+16 -> 19+16`，并覆盖选择另一组、
最后一份和三组以上堆叠。

## 未决风险

- 如果历史角色数据中的同一物品行已经被错误刷新为重复序号或超过 `item.dsh` 堆叠
  上限，服务端仍会记录 unresolved 状态；这类数据应先通过角色数据迁移修复。
- 真实客户端复测必须同时保留 `backpack_main_row`、`backpack_7_11_parser` 和
  服务端 `item-use rows before/after`，以确认请求序号与可见行继续一致。
