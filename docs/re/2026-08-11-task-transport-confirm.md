# 任务面板瞬移确认（WT 16/6）

## 触发与首个偏离

复现路径：打开已接取任务的详情页，选择“瞬移”，从目的地列表中选择一项，并在
“确定瞬移？”确认框中选择“是”。

此前服务端日志在确认后首次偏离：

```text
[error][network] unhandled wt=16/6 len=24 objects=1 first=1/16/6:15
```

这不是场景加载或坐标问题。客户端已完成 `WT 16/5 {taskid}`（目的地列表）和
`WT 16/5 {transid}`（目的地详情）的解析；确认框发出的请求无人处理，网络等待
状态因此不会被回调清除，表现为无法消失的进度条。

## 客户端链路与包契约

- `SendTaskTransportReq(0x01047E9A)` 先请求 `1/16/5 {taskid}`，选择目的地后请求
  `1/16/5 {transid}`。
- `task_handle_destinfo_response(0x01047F0A)` 解析 `16/6` 的
  `text,destscene,scenename,transid`；同场景目的地将对象转交
  `HandleItemUseConfirm(0x010190A8)`。
- 确认后客户端发送单对象 `1/16/6`。运行时包长 24、对象 payload 长 15；其字段
  结构严格为包装的 `u32 taskid`：
  `06 taskid 00 06 00 04 <be-u32>`。

服务端只接受上述精确 16/6 形状，且 `taskid` 必须是当前角色状态为 `1`（已接取）或
`2`（可提交）的任务。其他 16/6 请求不由任务传送处理器认领。

## 正确完成路径

确认不是空 ACK，也不能在确认回调内立即发送 `30/1` 场景对象。后一种做法会在
`HandleItemUseConfirm` 尚未返回时重入场景管理器，违背客户端生命周期。

处理器将任务目录解析出的 `scene,x,y` 组成 `mmGame 16/3` 直接场景进入结果。这与已有
设置/脱离卡死修复的客户端原生入口一致：客户端先完成 `16/3` parser，然后自行发起其
后的场景运行时同步；服务端在发送 16/3 后记录目标和持久化位置，但不额外插入场景进入包。

预期日志顺序：

```text
mock_task_transport phase=list ... response=16/5
mock_task_transport phase=select ... response=16/6
mock_task_transport phase=confirm ... response=16/3
mock_scene_target_direct_completed ... reason=task-transport-confirm
```

随后应出现客户端自然发起的场景运行时请求及相应 `16/3` ACK/场景初始化请求，而不是
`unhandled wt=16/6`。

## 验证边界

构建后用任务面板完成一次同场景和一次跨场景任务瞬移，确认：

1. 确认框关闭，进度条消失；
2. 到达目录解析出的精确场景和坐标；
3. 场景初始化后 NPC、任务提示和玩家位置正常；
4. 无关的 `16/6` 请求仍不会被该处理器误认。
