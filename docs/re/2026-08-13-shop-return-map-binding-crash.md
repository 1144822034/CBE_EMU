# 商城返回场景地图绑定崩溃

## 触发与首个错误状态

触发：从商城返回原场景后，新的 `ScreenInit` 已出现，客户端随即在首帧地图滚动中
崩溃：

```text
pc=01005766 lr=01046A4F r0=01056AD8 r1=00010001
```

按 `binary_name=江湖OL.CBE` 选择 IDA 实例后，`SetMapAndClampViewport`
(`0x0100575C`) 在 `0x01005766` 读取参数 `r1+4/+6` 作为地图宽高。这里的
`r1=0x00010001` 不是指针，故首次违约不是坐标、NPC 或资源文件内容，而是场景地图
资源尚未完成解析就被地图绘制路径消费。

调用链为：

```text
UpdateSpriteMovement(0x010469AC)
  -> scene+0x1C00+0x34 的 picture-library
  -> library+0x10[scene layer 11] == 0x00010001
  -> SetMapAndClampViewport(0x0100575C)
```

`0x00010001` 是未解析的层资源标识；有效路径必须先把它解析为地图记录指针，才可由
`UpdateSpriteMovement` 取用。

## 与商城返回协议的关系

现有商城返回分支把场景对象、任务对象与带 `posinfo` 的 `1/30/2` 放在同一响应中。
`scene_handle_change_result_scene_pos(0x01039770)` 先调用 scene-controller `+116`
建立新场景壳，再立即调用 `ResetDownloadState`。这会在图层资源完成前关闭/重置场景
下载状态，导致新壳首帧仍持有编码层标识。

`scene_handle_enter_with_scene_pos(0x010396D6)` 的 `1/30/1` 同样调用 scene-controller
`+116`，但不执行 `ResetDownloadState`。已有资源下载回归（
`2026-07-22-teleport-resource-completion-order.md`）验证的正确顺序是：

1. `1/30/1 { scene, posinfo }` 建立场景；
2. 客户端完成其真实的场景资源/后进入请求；
3. 服务端在该后进入完成响应中发送带该持久化坐标的 `1/30/2`。客户端的 post-enter
   请求会先采用 SCE 默认落点，故此处必须在资源就绪后重新写入权威坐标并关闭下载状态。

## 修正原则

商城返回改为上述两阶段协议：首个返回响应只交付 `30/1` 的场景进入，不在旧场景壳内
提前发送 NPC、任务、角色同步或 `30/2`。随后仅在客户端真实 post-enter 阶段且资源
已就绪时，发送 NPC 目录、必要的场景确认和带持久化位置的 `30/2`。若资源尚未就绪，
则保持 scene target pending，等待真实资源完成请求，绝不提前关闭下载状态。

不修改客户端内存、地图指针、坐标，也不以屏蔽绘制或默认位置规避崩溃。
