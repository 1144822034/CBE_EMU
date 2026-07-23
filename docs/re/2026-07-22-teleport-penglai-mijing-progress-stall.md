# 传送至蓬莱秘境后进度条不消失

```text
phase: map-stone direct teleport -> delayed WT30/1 -> same-target WT2/3 completion
trigger: 从蓬莱-铜雀台经传送石进入 c00蓬莱仙岛_03.sce（蓬莱秘境）
target: c00蓬莱仙岛_03.sce @ (157,47), resource download=0
client parser: JianghuOL.CBE 0x010396D6 (30/1 scene enter),
               0x01039770 -> 0x0103993C (30/2 ResetDownloadState)
```

## 已确认的当前链路

本地服务最近一次实机会话的关键日志为：

```text
16/4 -> 16/2 + 16/3 + item acknowledgement
poll -> 30/1 {scene=c00蓬莱仙岛_03.sce,posinfo=(157,47)}
2/3 len=90 -> 27/12 + 27/11 + 27/4 + 7/42 + 30/2 {result=1,type=2,scene}
25/5 len=44 -> builtin-scene-task-subset-followup
```

`30/2` 在 `2/3` 响应末尾，且不带 `posinfo`。这正是客户端在不二次进入
场景的前提下执行 `ResetDownloadState` 的已验证闭合方式；本次没有 WT18/7，故不应
将问题归因于资源下载或传送坐标。

## 历史成功基线与首个协议分歧

历史成功的同一地图石进入链路是：

```text
2/3 len=90 -> current-scene completion (ends in 30/2 without posinfo)
25/5 len=34 -> builtin-type27-followup
6/1 len=39 -> builtin-scene-task-subset-followup-current-scene
```

本次在第二步之后直接得到 `25/5 len=44`。按请求对象长度精确拆分，它是：

```text
25/5(empty), 6/1(empty), 6/13(empty), 6/14(empty),
2/10 {Type=101}, 27/11(empty)
```

它不是旧的 `25/5 + 27/11 + 27/4 + 7/42` type-27 跟进，而是“以 25/5 为主
回调”的合并场景运行时请求。旧的宽泛任务子集处理器会在前一个 `2/3` 已经消费
NPC 目录时跳过 `27/11`，仅回复任务和 `25/5` 对象。于是客户端明确请求的
`27/11` 没有对应下行对象，后续也不再发送独立 `6/1`，加载回调无法完整结束。

首个协议偏差因此是**复合请求的 `27/11` 应答缺失**，不是把它错分为 type27，也
不是资源下载或坐标问题。

## 排除项

- 目标 SCE 落点已经由 sMap/SCE 解析，落点 `(157,47)` 与本问题无关。
- 当前响应已包含唯一的无坐标 `30/2`；不能再加一个带坐标或第二个 `30/2`，否则会
  重入 `EnterSceneByMapName`。
- 不以延迟、空响应或全局抑制掩盖问题。

## 本轮计划

1. 仅匹配上述精确的六对象顺序；不改变普通 `6/1` 子集或 type27 跟进。
2. 若此前 `2/3` 已发送非空 NPC 目录，只在任务子集最前面回一个空 `27/11`，不
   重播 NPC、不重发坐标，也不增加第二个 `30/2`。
3. 用针对该合并包的回归确认响应同时含 `27/11`、任务/默认事件对象，且不含
   `30/1` 或 `30/2`；随后执行 `make -j2`。

## 验证

- `make -j2` 通过；
- `php tmp/teleport-penglai-mijing-progress-regression.php 19090` 通过：
  - map-stone 目标仍为 `c00蓬莱仙岛_03.sce @ (157,47)`；
  - 完成响应只有一个无 `posinfo` 的 `30/2`；
  - 精确的 44 字节合并跟进响应含且仅含一个空 `27/11`，仍含 `25/5` 默认事件应答；
  - 跟进响应不含 `30/1` 或 `30/2`。
- 服务日志出现：

```text
mock_scene_task_subset_fb11_ack ...
request=25/5+6/1+6/13+6/14+2/10+27/11
response=empty-27/11 reason=npc-catalog-already-delivered
```

这保持 NPC 只在前一包创建一次，同时闭合当前 `25/5` 回调请求的全部对象。
