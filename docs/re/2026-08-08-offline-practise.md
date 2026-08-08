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

`1/7/16 {itemseq}` 的修炼丹使用会锁定背包实例和修炼行，在同一个 MySQL
事务中扣除一颗并增加 60 分钟。银行已满时回 `7/16 {result=2}`，保留实例；
成功时回客户端已证实的 `result,maxnum,iteminfo`，由客户端原生刷新背包并关闭
等待界面。不会由通用 `7/1` 冒充成功。

## 修改点

- `src/server/mock_server_interaction_login.c`：真实 `7/18` 字段生成和严格
  `7/19(type=0)` 帮助、`7/21(opengold)` detector/response。
- `src/server/mock_server_role.c`：持久化 schema、离线边界、日上限、经验结算
  与 827 的跨背包事务。
- `src/server/mock_server_equipment_npc.c`：正常 session offline 生命周期标记
  修炼起点。
- `src/server/mock_server_catalog.c` 与 `src/server/mock_server_dispatch.c`：827
  专用 handler 在历史 unresolved fallback 之前执行。

## 自动化回归

运行：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD='123456'
.\scripts\run-practise-automation.ps1
```

场景 ID 为 `practise-v1`。它使用隔离端口 `19200/19201`、随机
`jh_online_autotest_<guid>` schema 和独立资源副本，不启动或控制桌面客户端。
场景最多 14 步、总超时 20 秒、单步超时 10 秒，只运行一次；所有 WT 包、服务端
日志和状态断言写入 `artifacts/automation/<run-id>/`。

最近一次通过证据：
`artifacts/automation/practise-v1-20260808T083926143Z-14552/`。
它断言：初始普通模式的 8 小时显示、原生 19 字节 `7/19.helpinfo`、原生 23 字节 `7/21` 设定回调、827 的原子
扣除/加时、黄金离线 15 分钟得到 240 经验、恢复普通模式后的等级快照速率，以及
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
