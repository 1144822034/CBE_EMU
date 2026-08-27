# player-3 商城返回后加载条不消失（2026-08-27）

状态：已改为纯平台事件投递；等待用新客户端进行一次人工体验复测。

## 触发与首次偏离

触发步骤：使用 `player-3` 登录，进入商城，再返回原场景。

本次运行的 `bin/multiplayer-data/player-3/logs/screen-lifecycle-order.log`
记录了返回后的同一场景生命周期：

1. `seq=45` 将 `01053f78`（原场景 screen）重新加入 manager；
2. `seq=48` 的同场景请求被接受，`net_slot=0`；
3. `seq=52` 的下一次同场景请求是关闭加载流程所需的正常收尾，但被宿主的同场景
   重复进入 guard 拒绝。

所以最早的错误状态不是商城页面或场景资源加载失败，而是宿主不属于平台 ABI 的
重复进入策略拒绝了固件自己的最后一个场景收尾请求；加载条因而没有走到正常关闭分支。

## 协议与客户端链路

服务端商城返回构造的顺序是位置型 `WT 30/1`，随后是进入后组合响应，其中
包含位置型 `WT 30/2`、`27/11` 和 `7/42`。`JianghuOL.CBE` 的场景分发在
`0x01039B8A`；`30/2` 进入 `0x01039770`
`scene_handle_change_result_scene_pos`，随后走正常的场景完成／下载状态关闭
路径（`0x0103993C`）。这说明 `30/2` 是真实客户端“可以收尾”的证据，不是
可任意放宽重复进入保护的信号。

此前宿主曾试图把 `30/2` 观察附着到特定 event-7 帧，再以此决定是否允许一次
同场景进入。这不是平台回调的职责，且在解包对象被拆分到下一 event-7 时会天然失真。

## 修复（2026-08-27 边界收缩）

进一步追踪证明，`duplicate_guard` 是宿主 Screen Manager 的早期兼容策略，而不是
`WT 30/2`、固件网络包装器或平台 ABI 的契约。固件已经在 `30/2` 回调内发出正常
的同一 screen 生命周期请求，宿主不应依据响应内容、场景 serial 或客户机 PC 拒绝它。

1. 删除同场景 serial guard、资源完成重入许可和 `0x01039770` 的宿主 PC 钩子。
   `idx=2/3` 的每个非空 Screen Manager 请求均按固件参数进入既有平台生命周期。
2. 网络调度器不再在 event-7 回调前后应用／清理宿主侧场景目标状态，也不再从
   `30/1`、`30/2`、`18/7` 提取场景／资源完成元数据。对应字节直接交给 CBE；可选
   取证仅记录传输事实，不附着或影响该 event。
3. 宿主继续只做平台必需的工作：复制响应字节到客户机缓冲区、以固件注册的
   callback/context 排队 event-7、再执行该 CBE 网络包装器。没有修改 CBE/CBM
   指令、客户机内存、PC/LR、寄存器或网络响应字节。

完整的回调边界和历史依据见
`docs/re/2026-08-27-network-callback-platform-boundary.md`。

## 验证

- `make -j2` 成功，重新生成了 `bin/main.exe`；服务端代码未改动，也没有重启
  正在使用的 player-3 模拟服务。
- 编译并运行 `remote-scene-update-reenter-regression.exe` 成功。它构造真实格式的
  `WT 30/1`、`WT 30/2`、拆分 follow-up event 和 `WT 18/7`，验证 event-7 保持
  固件注册的 callback/context，场景／资源信息仅作为随事件附着的只读证据。
- `equipment-enhancement-bootstrap-delivery-regression.exe` 与
  `direct-scene-challenge-progress-client-regression.exe` 均通过，覆盖原子拆分投递
  和相邻的直入场景 event-7 路径。

人工复测时，返回场景的 lifecycle 记录应出现收尾请求的 `accept=1`；加载条应随该请求
消失。若仍复现，应保留新的 `screen-lifecycle-order.log`、
`scene-asset-lifecycle.log` 和 `shop-return-input-v2.log`，按首个不满足的字段
继续取证。
