# 标题选服、端点与断开/重连协议取证

## 目标

确认江湖 OL 客户端是否存在由 `WT 1/1/12` 服务器列表或 `WT 1/1/4`
选服响应下发 `host/IP/port`、随后由客户端断开并连接到新端点的原生链路；若
存在，再把后台的服务器条目扩展为该已确认的协议字段。

## 可复现的现有链路

1. 标题页请求 `WT 1/1/12`；服务端返回 `result`、`serverinfo`、`servernum`
   和 `newVer`。
2. 用户选择列表行后，标题模块发送 `WT 1/1/4`，请求中只有选中的
   `serverID` 和 `moneytype`。
3. 现有服务端返回 `result`、`servconf` 和角色信息；客户端继续角色列表与
   场景启动。

可工作的 `serverinfo` 行格式为：名称字符串、状态字符串、`serverID:u32`、
显示色 `u24`。该格式及多行行为的已知边界见
[2026-07-24-configurable-login-server-list.md](2026-07-24-configurable-login-server-list.md)。

## 客户端模块证据

分析对象为
`bin/JHOnlineData/mmTitleMstarWqvga.cbm`（SHA-256
`AE114EDB994CE6D29FAFEB9B01D020E06BB265BAB5BD9D3EFA62DCC60F963048`）。
该 CBM 有 `0xA0` 字节模块头；下列地址是模块可见地址，原始文件偏移应加
`0xA0`。

### 登录响应

`net_handle_login_response(0x16DC)`（文件偏移 `0x177C`）的 PC 相对字符串
引用和分支可直接对应为：

| 字段 | 模块地址 | 用途 |
| --- | ---: | --- |
| `result` | `0x18D0` | 选择结果分支 |
| `serverinfo` | `0x18D8` | 取得服务器记录序列 |
| `servernum` | `0x18E4` | 限定记录循环次数 |
| `newVer` | `0x18F0` | 保存版本状态 |
| `information` / `username` / `password` | `0x18F8` 之后 | 仅结果 `3`/`4` 的登录记录处理 |

模块中没有 `host`、`port`、`endpoint`、`connect`、`disconnect`、
`reconnect`、`serverIP` 或 `serverip` 的 ASCII 协议字段。`address` 的唯一
命中在无关资源字符串区域，既不位于上述响应 parser，也没有作为网络字段引用。

这意味着给 `serverinfo` 行追加未证实的端点字节不会触发端点切换：该 parser
没有对应字段的读取或状态写入点。由于行是按 `servernum` 连续读取的，追加字节
还会改变下一行的起始位置，属于会破坏已确认行契约的猜测，不能下发。

### 选服请求

`net_build_login_request(0x1B9C)`（文件偏移 `0x1C3C`）构造的字段常量包括
`channelID`、`coreVer`、`appVer`、`userName`、`imsi`、`serverID`、
`moneytype` 和 `actorID`。其中选服 case 的两个字段为：

```text
serverID
moneytype
```

未发现任何端点字段，也没有发送“切断当前连接”或“连接新地址”的标题 WT
请求。因此 `WT 1/1/4` 是服务器/区域逻辑确认，而不是客户端网络端点迁移。

## 宿主传输证据

本项目已经把真实 TCP 传输严格放在 CBE 宿主适配层：

- [`src/main.c`](../../src/main.c) 的 `vm_mock_service_init_config()` 只在
  客户端进程启动时读取 `CBE_SERVER_ENDPOINT`，写入
  `g_mockServiceHost/g_mockServicePort`（`4127`–`4156`）。
- [`src/network-client.c`](../../src/network-client.c) 的
  `vm_client_connect()` 使用这两个宿主全局值执行 `getaddrinfo/connect`
  （`316`–`365`）。
- `vm_client_remote_request()` 与 `vm_client_remote_poll()` 每次 WT 请求/轮询
  都新建 TCP socket、收一个 CBMR 响应后立即关闭（`400`–`467`）。这里没有
  CBE 侧维护的可重连长连接。
- `vm_net_mock_service_notify_disconnect()`（`470`–`493`）只在宿主 loop 退出
  时发送 CBMS 外层 `flags=0x4`。这是模拟器客户端与独立服务进程之间的控制帧，
  不是标题模块的 WT 协议。

所以服务器响应无权修改 `g_mockServiceHost/g_mockServicePort`；服务端也不应
读取或写入客户端宿主内存。两者正是
[2026-07-23-client-server-process-boundary.md](2026-07-23-client-server-process-boundary.md)
明确规定的进程边界。

## 断开与重新登录

“返回标题”没有观察到客户端 WT 断开请求。相同 clientId 重新认证本身是新的
生命周期边界：服务端在认证时执行 `title-login-rebind`，先将旧场景会话下线，
直到新场景 ready 前不再下发场景数据。该修复和回归见
[2026-07-19-return-title-relogin-scene-lifecycle.md](2026-07-19-return-title-relogin-scene-lifecycle.md)。

正常关闭模拟器才会由宿主发送上述 CBMS `flags=0x4`；强制退出则依赖服务端的
presence timeout。这两条路径最终都调用同一服务端下线逻辑，相关运行时证据见
[2026-07-10-nearby-player-movement-sync.md](2026-07-10-nearby-player-movement-sync.md)。

## 根因与实施结论

期望的“选中服务器条目 -> 下行 IP/端口 -> CBE 原生断开/重连”在当前原始客户端
和当前进程边界中没有协议契约。第一次无法满足该期望的位置不是服务端缺少某个
字段，而是：标题 parser 从未接收或保存端点，实际 TCP 端点又只由宿主启动配置
拥有。

因此本轮**不**向 `serverinfo`、`servconf` 或任何标题响应加入猜测性的 IP/端口
字段，也不伪造断开成功或在服务端强行迁移 session；这些做法不能改变客户端的
连接目标，还会破坏既有 parser/生命周期。

如需支持多个物理游戏端点，必须明确选择一个不同于原生标题协议的产品层方案：

1. **启动器配置**：用户启动客户端前选择服务器，启动器设置
   `CBE_SERVER_ENDPOINT=host:port`；标题列表只负责显示和逻辑 serverID。
2. **固定入口网关**：客户端始终连接一个入口服务，选服后的 `serverID` 由入口
   在服务端路由到对应游戏分区。客户端无需端点迁移。

这两种方案都需要单独定义宿主/网关契约，不能冒充为已确认的 CBE 下行协议。

## 已排除的假设

- `serverinfo` 可安全承载未知尾随 IP/port：否；读取是按连续记录循环，且没有
  endpoint sink。
- `WT 1/1/4` 是端点切换请求：否；请求字段仅为 `serverID/moneytype`。
- 返回标题必须额外发 WT 断开包：否；原生可观察路径是后续认证触发服务端
  生命周期重绑；干净进程退出才有宿主外层断开控制帧。
- 当前 TCP 是可由 CBE 重连的长连接：否；宿主为每个请求和 poll 单独连接。

## 验证

- 静态核对标题 CBM 的响应 parser 与请求 builder 字段；未发现端点或重连字段。
- 源码核对宿主端点只在启动读取、请求/poll 一次一连接、退出时才发外层断开。
- 未改动业务协议或客户端内存；后续若采用启动器或入口网关方案，需要对其新契约
  另做多端点、重连、标题返回与强制退出隔离回归。
