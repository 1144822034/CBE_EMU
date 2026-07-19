# 临安资源下载后的同场景重入崩溃

## 现象

瞬移到 `c04临安府_01.sce` 时，客户端在收到 `resp=3768` 后崩溃：

```text
pc=0x0100575E lr=0x0104678F
r0=0x00020000
address=0x00020004
```

`resp=3768` 对应临安地图依赖 `cq_01.gif` 的完整 `18/7` 更新响应：
文件为 3666 字节，加上 WT 对象及字段开销后正好是 3768 字节。

## 客户端证据

按 `binary_name=江湖OL.CBE` 选择 IDA 实例：

- `SetMapAndClampViewport(0x0100575C)` 在 `0x0100575E` 执行
  `STR R1,[R0,#4]`；本次 `R0=0x20000`，因此写入 `0x20004`。
- 调用者是 `DrawSceneMapLayer(0x0104676C)`。崩溃表示地图数据已经解码，
  但承载地图层的场景宿主对象已失效。
- `WriteResBinToTempFile(0x01037000)` 只有在资源队列计数归零时才调用
  下载完成回调；`18/7` 本身不是畸形地图对象。

临安权威资源和客户端崩溃后缓存内容一致：

```text
c04临安府_01.sce  253 bytes
04临安府_01.map   1031 bytes
cq_01.gif          3666 bytes
```

因此本次问题不是旧的“服务端源与可写缓存内容不一致”，而是场景重入状态。

## 根因

桌面多人模式由独立服务进程构造响应，客户端进程只接收字节流：

- 服务进程记录了 `30/1 {scene,posinfo}` 的目标；
- 服务进程也在最终 `18/7` 上记录了资源完成；
- 客户端的 `hook_vm_manager_screen_func()` 却使用本进程中的
  `g_vm_net_mock_last_scene_change_target` 和
  `g_vm_net_mock_update_completed_reenter_pending`；独立进程不会自动同步这些值。

结果是客户端同场景保护没有目标可比较。资源安装回调和后续场景包中的重复
`EnterSceneByMapName(0x01018150)` 都被当成新的场景切换，反复重建同一个场景
shell，最终让 `DrawSceneMapLayer` 使用已经失效的地图层宿主对象。

## 修正

远程响应进入 guest 回调之前，仅观察两类已经在包中出现的事实：

1. `30/1`（以及带 `posinfo` 的 `30/2`）镜像场景名与坐标到客户端本地的
   同场景保护目标，并分配新的目标序号；
2. 最终 `18/7` 通过 `request.start + response.data_len >= totalsize` 判定，
   在客户端本地武装一次资源完成重入；
3. `30/2` 无 `posinfo` 清除完成的本地目标；
4. 新 `30/1` 会清除上一场景遗留的资源完成标记，避免旧资源误消费新目标的
   一次重入机会。

没有改写 CBE 场景变量、地图指针或客户端分支；场景和完成状态仍全部来自真实
响应包。这样首次资源完成重入被允许，后续相同目标重复进入由已有 guard 抑制。

新增关键日志：

```text
remote_scene_target_observe ... evidence=WT30/1-before-guest-callback
remote_update_complete_observe ... action=arm-one-scene-reenter
screen_mgr allow-update-reenter ...
screen_mgr same-suppressed ...
remote_scene_target_complete ... evidence=WT30/2-no-posinfo
```

## 回归

隔离服务端口 `19190` 下删除一个场景的 SCE、MAP、Actor、GIF 缓存后重新登录，
客户端完成连续 `18/7` 下载并正常退出。日志逐个确认最终分片观察，包括：

```text
remote_update_complete_observe file=00..._02.sce ...
remote_update_complete_observe file=m_village01.gif ...
remote_update_complete_observe file=b_flowers_trees.actor ...
remote_update_complete_observe file=b_flowers_trees.gif ...
remote_update_complete_observe file=00..._02.map ...
程序已正常退出
```

stderr 为 0，缓存文件在测试后恢复。最终构建：

```text
make -j2
```

