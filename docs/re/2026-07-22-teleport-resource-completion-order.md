# 瞬移资源下载完成顺序修正

```text
phase: teleport-stone deferred scene enter -> client resource download -> scene completion
trigger: 16/4 map confirmation, 16/2+16/3 confirmation, deferred 30/1, then same-target 2/3 while WT18/7 resources are still requested
request_shape: WT16/4 {curid,objid}; WT16/2+WT16/3 {exitID,type}; WT2/3 {maptype,mapID,exitID}; final client WT6/1 task/resource family
response_shape: item ack; delayed WT30/1 {scene,posinfo}; download-period WT2/3 ack without WT30/2; final WT6/1 resources followed by WT30/2 without posinfo
client_parser: JianghuOL.CBE:0x01037000 WriteResBinToTempFile; 0x01039770 parse_scene_posinfo_field; 0x0103993C ResetDownloadState; 0x0104676C DrawSceneMapLayer; 0x0100575C SetMapAndClampViewport
field_map: WT30/1 scene+posinfo opens the target; WT18/7 installs SCE/MAP/Actor/GIF; no-posinfo WT30/2 closes download state only after the resource callback; it does not submit another scene position
runtime_observation: target 01桃花岛_02.sce at (160,342); final 01桃花岛_02.map response reached pc=0x0100575E with r0=0x20000 and attempted to write 0x20004
negative_evidence: r1 points to a valid decoded 01桃花岛_02 map record and the downloaded SCE/MAP/Actor/GIF SHA-256 values match the service source; changing coordinates or resource bytes cannot repair the invalid map-layer host
implemented: preserve the teleport target across the download-period 2/3 response, omit its premature 30/2, and emit one no-posinfo 30/2 from the real post-resource WT6/1 response
remaining_unknowns: exact retail-server latency between the final WT18/7 callback and WT6/1 is not captured; completion remains driven by the client's actual WT6/1 request rather than a server timer
```

## 运行时根因

出错链路的服务端日志顺序为：

```text
mock_teleport_stone_deferred_enter ... scene=01桃花岛_02.sce
net_send ... wt=2/3 ... source=builtin-scene-change resp=97
mock_update_chunk_complete ... file=01桃花岛_02.sce
mock_scene_change_completed_repeat_ack ... scene=01桃花岛_02.sce
mock_update_chunk_complete ... file=e_batB.actor
mock_update_chunk_complete ... file=e_batG.gif
mock_update_chunk_complete ... file=01桃花岛_02.map
```

旧的通用 `2/3` 构造器会立即附加 `30/2`。客户端因此在 SCE 声明的 MAP、Actor、
GIF 尚未安装完时就执行 `ResetDownloadState`，本地远程观察状态也把场景目标快照为
completed。最后一个 MAP 到达时，资源回调只能临时恢复已完成目标并重入场景；此时
地图层宿主生命周期已经错位，`DrawSceneMapLayer` 最终以 `R0=0x20000` 调用
`SetMapAndClampViewport`。

## 修正后的协议边界

当 `g_vm_net_mock_teleport_stone_direct_enter_pending` 与本次 `2/3` 目标一致时：

1. `2/3` 仍正常回复请求中的其他结果对象，但不附加 `30/2`，目标继续保持 pending；
2. 客户端继续通过真实 `18/7` 请求安装场景依赖；
3. 资源队列完成后，客户端的 `scene_runtime_init_and_sync` 发出真实 `6/1`；
4. 服务端在该响应中返回资源、任务和 NPC 对象，最后附加一个不带 `posinfo` 的
   `30/2`；
5. `30/2` 只执行下载状态清理，不再次调用场景坐标进入分支。

新增诊断日志：

```text
mock_scene_change_teleport_resource_pending ... completion=defer-30/2-until-WT6/1
mock_teleport_resource_followup_complete ... completion=30/2-no-posinfo-after-WT18/7
```

没有写入或伪造 CBE 的地图层指针、场景全局变量或角色坐标；修正只调整真实请求
对应的下行包顺序。

## 回归

- `tmp/teleport-resource-completion-regression.php` 在隔离端口 `19130` 重放
  `16/4 -> 16/2+16/3 -> poll/30/1 -> 2/3 -> 6/1`：

  ```text
  teleport resource completion regression passed
  accepted_len=5 enter_len=54 pending_len=5 completed_len=237
  ```

- 下载期响应确认不含 `30/2`，最终 `6/1` 响应确认含 `30/2`；
- `scene-npc-lifecycle-regression.php` 通过，商城返回响应仍为 `620` 字节并带正确
  NPC 重播及无坐标完成对象；
- `login-scene-ready-regression.php` 通过，首场景响应为 `795` 字节；
- 本地客户端普通登录进入场景运行 42 秒后通过 `autotest` 正常退出，无地址异常、
  解包错误或断言；
- `make -j2` 通过，常驻服务使用新二进制监听 `127.0.0.1:19090/19091`。

