# 瞬移资源回调的远程观察顺序

```text
phase: teleport scene enter -> remote WT response queue -> guest callbacks
trigger: position-bearing WT30 scene response followed by scene completion and/or final WT18/7 resource chunks
request_shape: WT2/3 or WT16/4 scene flow; client-driven WT18/7 {name,start}
response_shape: WT30/1 or position-bearing WT30/2; WT30/2 completion; WT18/7 {name,totalsize,data}
client_parser: JianghuOL.CBE:0x01018150 EnterSceneByMapName; 0x0104676C DrawSceneMapLayer; 0x0100575C SetMapAndClampViewport
field_map: WT30 scene+posinfo -> target serial; final WT18/7 -> one resource-completion re-entry; WT30/2 -> clear target after its own callback
runtime_observation: pc=0x0100575E lr=0x0104678F r0=0x00020000 write=0x00020004; r1=0x050129E0 is a valid decoded map record with dimensions 240x293
negative_evidence: m_village_lotus.gif/e_mucusP.actor names and the decoded r1 record are valid; changing teleport coordinates cannot repair the invalid map-layer host in r0
implemented: capture response facts at async drain, attach them to the queued net task, apply immediately before that task's guest callback, clear completed target after the same callback, and refresh the serial guard on the accepted update re-entry
remaining_unknowns: full retail-server timing between each optional Actor/GIF dependency is not captured; current behavior remains client-driven by actual WT18/7 requests
```

## 瞬移完成后的进度条闭合

远程观察顺序修正后，2026-07-20 的实机瞬移不再在地图层绑定处闪退，但目标
场景已显示后仍保留无法消失的进度条。服务日志中的实际链路是：

```text
16/2 item ack
poll -> 30/1 {scene,posinfo}
2/3 current-scene request -> 27/12 + 27/11 + 27/4 + 7/42
6/1 task subset
```

目标已经被服务端标记为 `scene_ready`，后续 `2/3` 和 `6/1` 也已到达，所以这
不是丢包或场景数据缺失。缺失的是客户端下载/等待状态的协议闭合对象。

`江湖OL.CBE:0x01039B8A` 将 `30/2` 分派到
`parse_scene_posinfo_field(0x01039770)`。该函数在 `result==1` 且带
`scene+posinfo` 时会再次进入场景，但无论 `result` 和 `posinfo` 分支如何，最终
都会在 `0x0103993C` 调用 `ResetDownloadState`，并清零下载状态字段。因此：

- 通用 current-scene 分支继续不发送带坐标的 `30/2`，避免重复进入场景；
- 仅当本次 current-scene 完成属于尚未闭合的瞬移 `30/1` 生命周期时，在响应
  最后追加 `30/2 {result=1,type=2,scene}`，不带 `posinfo`；
- 该对象只执行下载/进度状态清理，不调用场景位置提交；
- 发送后同时清理服务端的 teleport direct-enter pending 标志，避免污染下一次
  场景操作。

新增诊断日志：

```text
mock_teleport_stone_current_scene_complete ... response=27-family+30/2-no-posinfo action=reset-download-state
```

## 本次闭合验证

- `make -j2` 通过；
- 新二进制下连续执行客户端登录/首场景进入及商城、消息等普通界面切换，进程均
  正常退出，`stderr=0`；
- 自动化尚未完成背包内瞬移石的目的地选择与确认，因此本轮不把完整瞬移 UI
  标记为自动实机通过；下一次真实瞬移应在服务日志中出现上述唯一闭合日志，且
  对应响应长度会比旧的 `current-scene-ack resp=188` 增加一个 `30/2` 对象。

## 崩溃点

按 `binary_name=江湖OL.CBE` 选择 IDA 实例后，调用链为：

- `DrawSceneMapLayer(0x0104676C)` 从当前场景对象 `scene+0x1C00`
  取地图资源记录与地图层宿主；
- `0x0104678C` 间接调用 `SetMapAndClampViewport(0x0100575C)`；
- `0x0100575E` 执行 `STR R1,[R0,#4]`。

本次 `R1=0x050129E0` 的 `+4/+6` 是 `240/293`，并包含
`m_village_lotus.gif`、`e_mucusP.actor` 等场景资源引用；它已经是可解析的地图
记录。真正无效的是 `R0=0x00020000`，即场景地图层宿主已经被旧场景销毁或替换。

## 回归根因

2026-07-19 的保护已能从远程响应镜像 WT30 目标和最终 WT18/7 完成事实，
但镜像发生在 `vm_net_mock_async_drain_completions()` 的“响应入队”阶段。
该函数会在同一个调度帧内连续取出多个已经完成的 TCP 响应，然后才由
`scheduler_dispatch_net_tasks()` 逐个执行 CBE 回调。因此实际顺序可能是：

```text
捕获 WT30/1 -> 立刻设置目标
捕获后续 WT30/2 -> 立刻清除目标
开始执行前一个 WT30/1 的 guest callback
```

这把网络完成顺序误当成了客户端解析顺序。前一个回调调用
`EnterSceneByMapName()` 时已经看不到自己的目标序号，同场景保护失效；连续资源
回调便可能反复替换 scene shell，最终留下 `R0=0x20000` 的地图层宿主。

## 修正

- 远程工作线程完成时只解析并捕获 WT30/WT18/7 的事实，不再修改场景全局状态；
- 捕获结果跟随对应 `vm_net_task` 入队；
- 调度器在调用该任务的 CBE 网络回调之前才按队列顺序应用捕获结果；
- WT30/2 的目标在自己的回调返回后清理，而不是在入队时抢先清理；
- 若完成目标随后触发最终 WT18/7，资源任务在自己的回调窗口临时恢复同一目标
  与序号；
- 被允许的一次资源完成重入会立即刷新同序号 guard，同一回调中的第二次
  `EnterSceneByMapName()` 会被抑制。

实现没有写 CBE 场景指针、坐标或全局变量；场景目标、资源完成和清理边界仍来自
真实下行包及真实 guest 回调返回。

## 验证

- `make obj/main.o -j2`：通过；
- 临时移出客户端缓存中的 `m_village_lotus.gif`、`e_mucusP.actor`、
  `e_mucusP.gif` 后自动登录：客户端通过真实 WT18/7 请求重新下载
  `m_village_lotus.gif`，下载结果 SHA-256 与原缓存一致；运行到自动退出，
  stderr 为 0；三个缓存文件随后全部原样恢复；
- 缓存齐全与上述资源缺失两轮自动登录都正常进入场景并正常退出，未出现
  `0x0100575E`、地址访问断言或地图层宿主错误；
- 完整 `make -j2`：通过，`bin/main.exe` 已重建；
- 服务已用新二进制重新监听 `127.0.0.1:19090/19091`。
