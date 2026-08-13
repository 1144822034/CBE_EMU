# NPC 商人购买：物品增量的帧边界

> **状态：已推翻（2026-08-14）。** 一帧延迟不能修复该问题：运行时记录显示
> `1/7/7 type=1` 本身重新开启 action=1 的等待，而非两个回调缺少帧边界。
> 本文剩余内容是已排除的时序假设；现行协议结论和隔离回归见
> `docs/re/2026-08-13-npc-purchase-progress-trace.md`。

## 新的运行证据

头盔 `5001` 的最新人工复现已确认运行中的服务端返回：

```text
mock_backpack_add item=5001 seq=36 ... response=7/7-type1
mock_npc_service action=shop-buy ... result=1 ... objects=2 ...
inventory_sync=same-response:26/1+item-manager-7/7
```

因此服务端不是旧的 `objects=1` / 场景轮询回包，也不是目录、价格或背包写入失败。

## 客户端边界

现有 `vm_client_extract_item_followup` 能正确从 WT 第 5 字节开始读取对象，识别
精确的 `26/1 + 7/7(type=1)`，并将后者复制成独立 event 7。二进制
`bin/main.exe` 含有对应的 `remote_npc_purchase_backpack_followup` 日志字符串。

但 `scheduler_dispatch_net_tasks()` 在同一次遍历中会继续处理刚入队的下一个任务：

1. 购买对话 event 7 调用 `ParseNPCDialogData`，清理 action=1 等待；
2. 同一调度遍历马上调用物品 event 7；
3. CBE 的网络/界面状态尚未经过一帧主循环收尾，物品模块进入时仍与刚完成的
   对话所有权重叠，表现为等待框不结束。

IDA 复核（均按 `binary_name` 选择实例）：

- `江湖OL.CBE:DispatchItemEvent(0x01039C28)` 的 `26/1` 分支调用
  `ParseNPCDialogData` 后清除业务等待；
- `mmGameMstarWqvga.cbm:sub_11CE(0x11CE)` 只在独立 event 7 中把 `7/7`
  交给 `sub_D04(0x0D04)` 的物品管理器路径。

这要求的不只是两个不同的 scheduler 任务，还必须是先完成对话回调、经过一个
帧边界后再交给物品模块。

## 修复合同

服务端保持已验证的严格二对象购买回复：

```text
1/26/1  购买结果对话
1/7/7   type=1，购买得到的一条增量背包行
```

客户端远程传输层仍拆成两个 event 7，但只对
`VM_CLIENT_ITEM_FOLLOWUP_NPC_PURCHASE` 将第二个任务延后一个调度 tick。
`7/1 + 17/1` 的物品使用/全背包列表配对不延迟、不改字段、不改服务端业务。

## 验证

1. `make -j2`；
2. 隔离的 `run-npc-purchase-equipment-swap-automation.ps1` 继续验证 WT
   顺序、武器/头盔防具/药品及持久化；
3. 人工购买头盔后，客户端控制台应依次记录购买拆分与
   `remote_npc_purchase_followup_queue ... delay_ticks=1`，等待框关闭且背包新增头盔。
