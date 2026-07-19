# Linux 服务端未处理封包策略

## 适用范围

- 仅适用于定义了 `CBE_SERVER_ONLY` 的 Linux/无界面服务端构建。
- Windows CBE 模拟器构建仍在未处理封包处触发断言，便于协议逆向时及时发现缺失处理器。

## 行为

服务端收到无法识别或尚未实现的 WT 请求时：

1. 保留现有的 `unhandled wt=...` 或 `unhandled malformed ...` 诊断日志；
2. 将本次请求来源记录为 `ignored-unhandled-server-only`；
3. 返回合法的服务传输响应头，响应正文长度为 `0`；
4. 不生成猜测性的 WT 应答，不关闭客户端连接，也不终止服务进程；
5. 后续请求继续由同一服务进程正常处理。

## 验证重点

- 服务端构建能够通过编译；
- 发送未知 WT 包后进程仍存活；
- 日志同时出现原始 `unhandled` 诊断及 `source=ignored-unhandled-server-only`；
- 未知包的响应正文为空；
- 随后的正常请求仍能得到响应。
