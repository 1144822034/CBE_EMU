# NPC 对话前 `2/1 type` 误开任务大厅修复

## 运行时现象

与动态 NPC“新手礼包”交互时，客户端在尚未发出新的 `26/1` NPC 对话请求前崩溃：

```text
pc=00000000 lastPc=0104A2C0
r6=0105A850
```

崩溃前服务端最后一条异常响应为：

```text
net_send ... wt=2/1 len=58 source=builtin-game-type resp=73
```

这说明问题发生在 NPC 对话协议之前；不能通过继续修改 XSE 正文或 `26/1 dialog`
布局解决。

## 客户端证据

- `JianghuOL.CBE:0x0104A074` 是任务大厅触摸处理器；
- `0x0104A2C0` 无条件调用 `R9+0x9CB4` 的列表触摸回调；
- 崩溃时 `R9+0x9C80` 起始的任务大厅状态块全为零，故该回调为零；
- `JianghuOL.CBE:0x01010C34` 消费 subtype `0x1A` 的成功响应并切换到任务大厅屏幕；
- `BuildTaskHallList(0x01037FD8)` 才会通过 `InitListCtrlVTable` 初始化上述回调。

因此，服务端把非任务大厅请求改写成 subtype `0x1A`，会打开一个没有列表数据的半初始化
任务大厅，下一次触摸随即跳到空地址。

## 服务端根因与修复

旧的 `builtin-game-type` 检测器只检查任意对象是否含有 `type=1/2/3`，没有约束
WT kind/subtype。场景/角色控制对象 `2/1` 同样会携带 `type`，因而被错误改答为
`2/26`。

当前实现：

1. 将 `game-type` 限定为已验证的启动交换 `7/7`；
2. 对单对象 `2/1 {type,...}`（且不含 `moveinfo`）返回保持原身份的空 `2/1` ACK；
3. 多对象组合包继续通过 split-safe 路径逐对象产生 `2/1` ACK；
4. `26/1` NPC 对话专用处理及 `25/5 + 6/10` 任务详情组合处理保持不变。

## 验证

- `make -j2` 通过；
- `tmp/game-type-scope-regression.php`：
  - `7/7 type=1 -> 7/26`，响应 73 字节；
  - `2/1 type=1 -> 2/1`，响应 11 字节；
  - 两个 `2/1 type` 组合对象 -> 两个 `2/1` ACK，响应 17 字节；
- `tmp/xse-npc-dialog-regression.php` 通过；
- `tmp/task-detail-combo-regression.php` 通过；
- 新服务监听 `127.0.0.1:19090/19091`，stderr 为空。

