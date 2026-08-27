# 网络回调的固件／平台边界（2026-08-27）

状态：已收缩为平台事件模拟；等待 player-3 原路径人工复测。

## 问题

商城返回场景的 `WT 30/2` 已由 CBE 解析器正常消费，但它在同一网络 callback
内调用 Screen Manager 时，被宿主的场景 serial 去重策略拒绝，进度条无法完成原生
收尾。该策略此前还需要以 `WT 30/2` 的包观察或解析器 PC 临时放行一次，说明
宿主已经越过平台投递边界，开始替固件解释场景生命周期。

## 固件注册与实际投递链路

`JianghuOL.CBE:InitNetEventConn(0x010348FC)` 通过虚拟网络管理器 `idx=0`
登记异步连接。运行时 ABI 中，CBE 将 callback 放在 `R2`、context／连接对象放在
`R3`；宿主只分配 connectId 并保存这对值。随后：

```text
CBE idx=1 send(connectId, dataPtr, dataLen)
  -> 宿主读取客户机请求并交给后台 socket 工作线程
  -> 宿主在模拟器线程分配客户机响应缓冲区
  -> event=7(responsePtr, responseLen, responseLen, callback, context)
  -> CBE 已登记的 net_wrapper_event_dispatch(0x0103489A)
  -> CBE manager callback / WT 业务 parser
```

`net_wrapper_event_dispatch` 是固件拥有的包装器；它根据自己的 manager 状态和
callback 槽继续分发。宿主不选择商城、场景或 WT parser。

## 首次偏离与历史依据

`player-3` 的 screen 生命周期日志曾记录最后同场景请求被宿主的重复进入 guard
拒绝。该 guard 最初来自 2026-06-25 的“勉强跑图”兼容实现，不是由 CBE Screen
Manager API、`WT 30/2` parser 或平台回调 ABI 证明的行为。

相反，CBE 的 `30/2` 分支 `scene_handle_change_result_scene_pos(0x01039770)` 会在
`0x0103993C` 走下载状态重置。它随后发起的同一 screen 生命周期请求是固件自己的
正常动作，不能由宿主按包类型、场景名称、serial 或调用 PC 选择性拦截。

## 修改

`src/main.c` 的网络调度与 Screen Manager 已改为：

1. `scheduler_dispatch_net_tasks()` 仅调用已登记的 callback，保留响应指针、长度
   与 event 类型；不再从 `WT 30/1`、`30/2` 或 `18/7` 推导并应用场景目标状态。
2. Screen Manager `idx=2/3` 对所有非空的固件请求执行其既有通用生命周期，包括
   当前 screen 与目标 screen 相同的情况；不再有 scene serial guard 或一次性例外。
3. 删除 `WT 30/2` 解析器 PC 钩子，以及客户端传输层对 `30/1`、`30/2`、`18/7`
   的场景／下载字段提取；这些帧现在作为不透明字节直接交给已登记的 CBE callback。
   有界取证只记录传输事件，不把场景元数据附着到 event-7。
4. 删除 event-7 入队时保存、回调前回写 `Global_R9+0x9584` 下载状态块的旧逻辑，
   以及关闭通道时直接写 CBE 网络状态的旧逻辑。下载和连接状态只由固件 callback
   与 parser 的正常分支推进。
5. 删除已确认不可达的宿主侧场景重入状态、event-7 后的 send-ready 重排、登录
   `7/42` 尾包强制 flush 及其 trace 字段。它们不会再根据下行数据、固件全局状态或
   callback 返回结果额外调度网络业务。

这使宿主回到“虚拟平台事件源”的职责：保存固件已经登记的 callback、在正确的
模拟器线程投递事件、调用固件包装器。场景完成、下载状态和 UI 收尾全部由 CBE
parser 与其自身的 Screen Manager 请求决定。

宿主仍会向客户机内存写入响应字节和 connectId 输出值；这是虚拟 I/O API 的必要
边界，并非业务状态或回调策略。

## 验证

- `make -j2` 成功，重新生成 `bin/main.exe`；未改动服务端，也未重启用户的
  player-3 服务。
- `remote-scene-update-reenter-regression.exe` 通过：`WT 30/1`、`WT 30/2` 与
  `WT 18/7` 均保持不透明，并以 event-7 的原始 CBE callback/context 入队；原子
  双 event-7 投递也保持该契约。
- `equipment-enhancement-bootstrap-delivery-regression.exe` 通过：原子双 event-7
  投递仍使用原注册 callback。
- `direct-scene-challenge-progress-client-regression.exe` 通过：相邻的直入场景
  event-7 路径继续由固件 parser／渲染观察自然推进。

## 人工验收

使用新 `bin/main.exe` 以 player-3 登录、进入商城并返回。成功条件是加载条消失，
且 `screen-lifecycle-order.log` 中最后的同场景 Screen Manager 请求为
`accept=1`。如果仍失败，应保留该日志及同次的
`scene-asset-lifecycle.log`、`shop-return-input-v2.log`；下一次调查从固件 callback
收到的 event 类型和包字节开始，而不是重新加入宿主场景例外。
