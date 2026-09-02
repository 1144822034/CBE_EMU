# 梦境副本右上角计时：远程包取证入口

Date: 2026-08-31

Status: evidence collection only; no timer behavior or server protocol changed

## 已知边界

截图中的 `08:05` 位于 `梦境空间三层` 的场景画面层，但目前没有该次远程
进入的原始 WT 包。现有动态 NPC 副本仅会在用户选择“进入副本”后下发场景进入
`30/1`；它没有已证实的时限字段。不能根据截图向 `30/1`、场景资源或后台表
臆造 `duration`、补时或到期处理。

梦境使者（actor `30406`）的完整客户端边界为：

```text
WT 1/26/1 { type=1, id=30406 }
  -> 26/1 对话（action=1，“进入副本”）
WT 1/26/1 { type=2, id=0xEBxxxxxx }
```

其中 `0xEB` 是已经验证的 `ENTER_INSTANCE` 操作命名空间，后 24 位是 NPC
标识。它们都不是计时字段。远端的 NPC 标识可能与本机夹具不同，启动器因此从第一条
`WT 1/26/1` 且 `type=1` 或 `type=2` 的对话／服务请求开始捕获，而不再限定 actor
数值；这不会捕获登录请求。首个 actor ID 可采用 `u16` 编码，而进入操作常使用 `u32`；
取证解析两种现有 WT 数值编码，仍只保存已发送／已收到的原始字节。

## 捕获方式

使用 `bin/multiplayer/capture-dream-instance-player-3.bat` 启动 player-3。
它沿用 `start-player-3.bat` 所选择的远程目标；启动器本身不更改账号、服务端
或端口。

在客户端中正常与梦境使者对话；第一条 `type=1` 或 `type=2` 的 `1/26/1` 请求会开始
捕获。产物写入该 player-3 profile 的 `logs/`：

```text
dream-instance-capture-<run>.manifest.tsv
dream-instance-capture-<run>-<index>-uplink.wt
dream-instance-capture-<run>-<index>-downlink.wt
dream-instance-capture-audit-<run>.tsv
```

manifest 保存方向、scheduler tick、墙钟、连接、原始 event、传输序号、包长和
WT 对象头摘要。`.wt` 文件是未修改的原始 payload。空的 scene-poll 回复不会写入；
非空 poll 回复会保留，因为它可能承载后续状态变化。上限为 128 个包和 1 MiB，达到
上限会在 manifest 写入 `limit` 行并停止，防止长期远程会话产生无界日志。

`audit` 在此开关已启用时，从第一个上行开始保存至多 512 条**元数据**：时刻、连接、
长度及 WT 对象头。它不保存任何 payload 字节，目的是当远端 NPC 使用尚未记录的请求
签名时，仍能区分“环境变量没有传入”与“触发签名不匹配”。

## 实现边界

`src/network-client.c` 在 CBE 已产生上行包后复制它；下行包在复制到 guest RAM、
加入 scheduler 和调用 CBE callback 之前仅写入本地文件。它不会修改原始字节、
客户内存、寄存器、PC/LR、callback/context、事件类型、事件次序或服务端状态。
没有新增 server handler、数据库字段、补时行为或客户端 UI 写入。

## 下一步判定

将这次进入的 capture 与右上角数字首次出现的时间对齐。重点比对：

1. 对话上行后的第一个 `26/1` 下行包；若不存在，首个偏离就是远端对话响应缺失；
2. 进入选择后的第一个包含 `30/1` 的下行包；
3. 场景资源／跟随请求与其回复；
4. 计时数字出现或变化前的首个非空 scene-poll 回复；
5. 若存在补时、到期、退出或重进操作，分别保存对应的上行和下行包。

只有识别出客户端实际消费的对象、字段和 parser 后，才能设计服务端持久化到期时间、
补时和到期策略；在此之前不应把常规副本传送的 `30/1` 复用为猜测性的计时器协议。

## 2026-08-31 player-3 远端捕获结果

用户使用专用启动器正常进入梦境后，profile 中生成了
`dream-instance-capture-00080494.manifest.tsv` 与 16 个原始 WT 文件。审计文件同时
证明 `CBE_CAPTURE_DREAM_INSTANCE=1` 已传入实际客户端进程。

捕获到的入口契约为：

```text
WT 1/26/1 {type=1,id=0x000076C6}       # actor 30406
<- 1/2/1                                # 普通确认
<- 1/26/1                               # 对话；首项“进入副本”值为 0xEB0076C6
WT 1/26/1 {type=2,id=0xEB0076C6}
<- 1/2/1                                # 普通确认
<- 1/30/1 {scene="29梦境空间_03.sce",posinfo=(50,50)}
```

之后是已有的场景跟随与资源流程：`2/3`、`30/2`、`25/5`、`6/1`、`6/13`、`6/14`、
`27/11`、`27/4` 和 `7/42`。唯一非空 scene-poll 是 `6/1 + 6/14` 的资源／任务数据；
没有 `time`、`duration`、`remain`、`addtime` 或未知额外对象。`30/1` 的完整原始包也
只含 `scene` 和 `posinfo`。

所以截图中最初显示的 `08:05` **不是**由这条远端入口链的已知字段初始化。捕获窗口中的
数据事件跨约 4.6 秒；这不能排除未来存在另一条独立的时限协议，但足以排除“在当前
`30/1`、其资源回包或首个非空 poll 中补一个后台 duration 字段”的方案。当前后台没有
可安全控制该数字的已证实接口；它仍是客户端地图叠层／本地场景状态的待取证来源。
