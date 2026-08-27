# 离线修炼：`7/18` 信息、`7/21` 设定与修炼丹

## 触发、首个偏离与客户端契约

修炼信息页初始化发送短包 `WT 1/7/18`。点击“设定”并确认黄金修炼时，
`JianghuOL.CBE:HandleTradeInput(0x0102C3D6)` 只发送一个对象：

```
WT 1/7/21 { opengold: tagged-u8(0|1) }
```

运行时原始记录的这个请求长度为 23 字节，且此前服务端记录为：

```
unhandled wt=7/21 len=23 objects=1 first=1/7/21:14
```

这就是“点击设定、开启后进度条不消失”的首次偏离，而不是 UI 或网络
投递顺序问题。`HandleExpBattleResponse(0x0102CB46)` 对应 `case 21` 只读取
`result`，随后设置完成标志并显示“已设置”。空 WT、通用成功包或不同 subtype
都不会走该回调。

`case 18` 依次读取下面的字段，因此信息页也不能再使用固定的
`15 分钟 / 120 经验 / 1小时45分钟` 占位数据：

```
todaypasthour, todaypastmin, getexp,
todaylasthour, todaylastmin,
alllasthour, alllastmin, isgold
```

“点击帮助”是另一个独立请求，而不是 `7/18` 的本地说明：运行时记录为
`WT 1/7/19 { type: tagged-u8(0) }`（总长 19 字节）。此前无 handler，因而
没有任何数据事件完成请求，表现为等待框永久存在。
`HandleExpBattleResponse(0x0102CB46)` 的 `case 19` 仅读取 `helpinfo` 的字节
串及其长度，随后将其交给原生文本窗口并清除等待状态。现在服务端只匹配这个
`1/7/19 + type=0` 签名，并回复 GBK `helpinfo`，内容说明修炼丹、离线结算和两种
模式的已实现时长上限；不会把不相关的 `7/19` 请求吞掉。

相关客户端证据保存在 `tmp/ida_full_jh_actor_update/decompiled.c` 的
`0x0102C3D6` 和 `0x0102CB46`。IDA 访问按 `binary_name=江湖OL.CBE` 选择了
当前实例，而没有依赖固定实例 ID。

## 权威状态与结算

`item.dsh` 的 827（修炼丹）说明明确给出：每颗增加 **1 小时**修炼时间，
离线后自动修炼；每天最多修炼 **8 小时**，累计最多保留 **100 小时**。服务端
为此创建关系表 `account_role_practise`，按 `(account_id, role_id)` 持久化：

- 黄金/普通设定；
- 未消耗的修炼分钟数；
- 当前 UTC 日的已消耗分钟数与已获得经验；
- 本次离线开始时间。

会话进入 offline 生命周期时记录时间戳；下一次在线读取 `7/18` 时才结算该段
离线时间并清除时间戳。因此在线停留、反复打开信息页或服务端 scheduler tick
不会生成修炼经验。

客户端和物品资源能证明时间上限和黄金模式的倍数语义，但没有提供服务端数值
公式。当前采用明确、可审计的平衡规则（不是伪称为原服数值）：

| 模式 | 每日可消耗修炼时间 | 每分钟经验 | 每日经验上限 |
| --- | ---: | ---: | ---: |
| 普通 | 480 分钟 | `8 × 离线开始时等级` | `3840 × 等级` |
| 黄金 | 240 分钟 | `16 × 离线开始时等级` | `3840 × 等级` |

这对应客户端说明“黄金修炼双倍经验、消耗较少时间；普通 8 小时 / 黄金 4 小时”。
一段离线区间使用其开始时的等级作为快照；不会因该区间内升级而递归提高同一段
离线奖励。到达等级经验上限时，仍消耗已经经过的有效修炼分钟，但只记录实际能
写入角色 EXP 上限内的经验。

`1/7/16 {itemseq}` 是修炼丹的数量预检：它锁定背包实例和修炼行，但不改变
任何持久化状态，成功时以 `maxnum=min(该堆数量, 剩余分钟容量/60)` 返回原生数量
控件的上限。银行已满时回 `7/16 {result=2}`，保留实例；成功回的字段是客户端已
证实的 `result,maxnum,iteminfo`。真正的扣除和加时只能在下节的 `1/7/17`
确认请求中完成，不能由通用 `7/1` 冒充成功。

## 2026-08-27：827 使用后的原生收尾 `1/7/17`

最初的玩家复现中，首次协议偏离不是绘制或宿主事件顺序，而是成功 `1/7/16` 后的
下一个客户端请求没有 handler：

```
mock_practise_pill16 role=10093 seq=70 success=1 max_use=1 action=preflight
net_send ... wt=7/16 ... source=builtin-practise-pill16
net_send ... wt=2/10 ... source=builtin-actor-other-only10
unhandled wt=7/17 len=38 objects=1 first=1/7/17:29
```

原始记录位于 `bin/server_out.txt`；其中 `2/10` 是原生 `7/16` 成功回调自行发出
的普通角色刷新，不能用它代替 `7/17`。IDA 的
`JianghuOL.CBE:0x0102C104` (`HandleItemUseResponse`) 只在收到
`1/7/(17|34|4)` 时进入这个独立分支。对 `17`，它严格读取：

| 响应字段 | 编码 | 客户端用途 |
| --- | --- | --- |
| `result` | tagged `u8` | `1` 才继续原生成功收尾 |
| `useinfo` | 普通 WT string | 复制至本地提示文本 |
| `pcimg` | tagged `u8` | 更新本地固定状态图片标志 |

在 `result=1` 后，该 CBE 函数发送其自身的 event `100` 并调用原有 UI 收尾路径；
服务端没有发送额外 push、没有写客户端状态，也没有改变 callback 或 event 顺序。

请求的运行时长度为 38 字节，且单对象载荷为 29 字节；原始字节为：

```
57 54 00 26 01 07 11 00 22
06 75 73 65 6E 75 6D 00 06 00 04 00 00 00 01
07 69 74 65 6D 73 65 71 00 04 00 02 00 46
```

这份原始包只是数量 1 的一次实例：字段契约是
`usenum:tagged-u32(玩家选择的数量)` 和 `itemseq:tagged-u16(70)`，而不是
`usenum` 恒等于 1。该收尾请求不带物品 ID；专用 handler 要求同一 client session、role
和这个 `itemseq` 已刚刚成功完成 827 `7/16` 数量预检，因而不会把其他 `7/17` 泛化成
物品使用。

数量 2 的人工复现暴露了旧实现的第二个首个偏离点：旧 detector 错误要求
`usenum==1`，因此 `7/17` 没有获得回包，原生等待条不能收尾；即使仅放宽该 detector，
旧的 `7/16` 也已经只扣了一颗、只加了一小时，仍会违反数量语义。

用 `ida_multi_mcp` 只读反查得到完整的客户端责任链：

- `JianghuOL.CBE:0x0102355E` (`SendItemUseRequest`) 对 827 的 `7/16` 只写
  `itemseq`；
- `JianghuOL.CBE:0x01025AE6` (`HandleShopBuyItem`, case 16) 从回包读 `maxnum`
  并写入数量控件上限；
- `JianghuOL.CBE:0x0102C032` (`HandleBattleActionInput`) 从该控件的当前值写
  `7/17.usenum`，所以数量 2 是客户端已支持、由玩家确认的正常请求；
- `JianghuOL.CBE:0x0102C104` (`HandleItemUseResponse`) 对成功 `7/17` 严格读取
  `result,useinfo,pcimg`，发送 CBE 自己的 event `100` 后走原始 UI 收尾。

服务端据此保存一次临时的 `7/16 -> 7/17` session/role/itemseq/max-use 授权：`7/17`
在同一 MySQL 事务中再次锁定行、核实 `usenum<=max-use`、扣除恰好 `usenum` 颗并增加
`usenum*60` 分钟。只有同一数量的传输重传重放成功回包，不会再次扣除或加时；不同数量
的重传或超过上限的数量回 `result=2`。失败 `7/16`、session offline 和其他 `7/17`
都不会被通用物品处理器吞掉。

回包使用 `pcimg=1`，与现有场景实时徽标的“无固定修炼图片”语义一致：827 增加的是
持久化离线修炼时间，并不会立即获得场景左上角的“修”状态。成功 `useinfo` 为 GBK
“修炼时间增加 N 小时。”，其中 `N` 就是原始 `usenum`。

同日的运行复测还留下了一个实现层反证：服务端已记录
`practise_pill17_arm`，却仍把完全相同的 38 字节包记为 unhandled。这排除了“旧
服务未加载”的可能。原因先是误用了仅适合响应对象的 `u8/u16` accessor，随后又把
字段名错误推测为 `type/id/seq`。只读 raw probe 确认上述 `usenum/itemseq` 包装后，
现已改为 `vm_net_mock_get_object_tagged_number_entry`，仍限制为确切长度、字段值域和会话
事务边界，未放宽为通用 `7/17`；该 probe 已移除。

## 修改点

- `src/server/mock_server_interaction_login.c`：真实 `7/18` 字段生成和严格
  `7/19(type=0)` 帮助、`7/21(opengold)` detector/response。
- `src/server/mock_server_role.c`：持久化 schema、离线边界、日上限、经验结算，
  以及 827 的只读数量预检和按 `7/17.usenum` 提交的跨背包事务。
- `src/server/mock_server_equipment_npc.c`：正常 session offline 生命周期标记
  修炼起点。
- `src/server/mock_server_catalog.c` 与 `src/server/mock_server_dispatch.c`：827
  的 `7/16` 数量预检和仅匹配其成功会话边界的 `7/17` 原生确认 handler，都在历史
  unresolved fallback 之前执行。
- `src/server/mock_server_equipment_npc.c`：保存并在 offline 时清理
  827 `7/16 -> 7/17` 的临时 session/role/seq 授权边界；不持久化为角色状态。

## 自动化回归

运行：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD='123456'
.\scripts\run-practise-automation.ps1
```

场景 ID 为 `practise-v1`。它使用隔离端口 `19200/19201`、随机
`jh_online_autotest_<guid>` schema 和独立资源副本，不启动或控制桌面客户端。
场景最多 16 步、总超时 20 秒、单步超时 10 秒，只运行一次；所有 WT 包、服务端
日志和状态断言写入 `artifacts/automation/<run-id>/`。

`7/17` 的精确 `usenum/itemseq` 契约已加入场景：夹具以三颗 827 开始，先断言
`7/16.maxnum=3` 且没有突变，再发送数量 2 的原生 38 字节 `7/17`，断言恰好留下
一颗、增加 120 分钟、返回 `result,useinfo,pcimg`，并验证相同重传不重复扣除。随后
继续验证离线结算。2026-08-27 本地已通过 PHP 语法检查和完整服务端独立链接校验；
隔离 MySQL 凭据在该环境中未配置，因此尚未把这次新增断言标记为已运行通过。

旧基线通过证据（新增 `7/17` 断言前）：
`artifacts/automation/practise-v1-20260808T083926143Z-14552/`。
它断言：初始普通模式的 8 小时显示、原生 19 字节 `7/19.helpinfo`、原生 23 字节
`7/21` 设定回调、827 的原子扣除/加时、黄金离线 15 分钟得到 240 经验、恢复普通
模式后的等级快照速率，以及
100 小时上限拒绝时背包实例不被删除。

帮助包另有不依赖角色资料的场景 `practise-help19-v1`：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD='123456'
.\scripts\run-practise-help19-automation.ps1
```

它使用 `19202/19203` 与随机 schema-only 测试库，先走原生登录、选角，再发送
原生 19 字节 `1/7/19` 请求，
断言服务端真的回 `1/7/19.helpinfo` 且正文以 GBK“修炼帮助”开头。该场景验证
协议响应；文本窗口关闭则由上述 `0x0102CB46 case 19` 客户端解析证据保证。
运行器对每个 PHP 夹具命令显式关闭 Xdebug 远程自动连接，避免本机 `:9000` 调试
端点不可用时占用场景超时。
回归的 CBMR 读取按帧头和载荷长度循环读取，不能将 TCP 分段到达误判为协议
错误。
最近一次帮助专用场景通过证据：
`artifacts/automation/practise-help19-v1-20260808T083905982Z-2000/`。
