# 任务瞬移卡住：确认包 `1/16/6` 未处理

## 症状

活动任务点「瞬移」并确认后，客户端停在等待态。服务端：

```text
unhandled wt=16/6 len=24 objects=1 first=1/16/6:15
source=ignored-unhandled-server-only response=0
```

此前 list/select（`16/5`）已能返回 `16/5 destinfo` / `16/6` 确认 UI。

## 根因

1. `builtin-task-transport` 原先只接受单对象 `1/16/5`。
2. 选择目的地后服务端返回 `1/16/6 {text,destscene,scenename,transid,result,value}`，
   客户端经 `task_handle_destinfo_response(0x01047F0A)` →
   `HandleItemUseConfirm(0x010190A8)` 弹出「确定瞬移？」。
3. 用户确认后客户端再发 **`1/16/6`**（总长 24，payload 15，对应单个 6 字符
   u32 字段，常见为 `result` 或 `taskid`）。
4. 该请求无 handler → `response=0`，等待标志无法按业务路径结束，界面卡住。

这与传送石 `16/2+16/3` 确认事务不同，不能复用 map-stone exitID 路径。

## 修改

- select 阶段返回 `16/6` 确认 UI 时，武装
  `g_vm_net_mock_task_transport_pending_*`（账户状态隔离）。
- 接受入站 `1/16/6`：
  - 有 `result` 或已武装 pending → 确认执行：回空 WT ack，并复用传送石的
    deferred `30/1` 投递（避免确认回调未退出时同包进场景的崩溃边界）。
  - 仅有 `taskid`/`transid` 且无 pending → 按 select 处理，仍回确认 UI。
- 登录/选角清传送相关状态时一并清 pending。

## 验证

1. 活动任务 → 瞬移 → 选目的地 → 确定：
   - `mock_task_transport phase=list/select/confirm`
   - 不再出现 `unhandled wt=16/6`
   - 随后 poll 出现 `mock_teleport_stone_deferred_enter` / 场景进入
2. 同场景与跨场景各测一次；重复进入/退出任务详情不残留 pending。

## unresolved

- 入站 `16/6` payload 15 的精确字段名尚未用 IDA/hex dump 钉死（`result` vs
  `taskid`）；日志 `fields=` 会打印解析结果，便于对照。
- 确认路径是否在客户端本地已扣传送石：未见 `16/2+16/3`，服务端暂未在
  `16/6` 上强制扣 `800`；若本地未扣需另证后补。
