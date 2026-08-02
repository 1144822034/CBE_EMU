# Linux 服务端未处理封包策略

## 适用范围

- 仅适用于定义了 `CBE_SERVER_ONLY` 的 Linux/无界面服务端构建。
- Windows CBE 模拟器构建仍在未处理封包处触发断言，便于协议逆向时及时发现缺失处理器。

## 行为

服务端收到无法识别或尚未实现的 WT 请求时：

1. 保留现有的 `unhandled wt=...` 或 `unhandled malformed ...` 诊断日志；
2. 构造拒绝提示应答：`25/12 {result=4}` 清信息条幅等待，再附带
   `1/3/3 type=5` 系统消息（GBK 文案 `功能暂未实现(major/kind/subtype)`，
   无对象时退化为 `功能暂未实现(wtKind/wtSubtype)`）；
3. 将本次请求来源记录为 `unimplemented-handler-notice`，并返回上述合法 WT 正文；
4. 若提示包编码失败，才回退为历史行为：`ignored-unhandled-server-only` +
   响应正文长度 `0`；
5. 不编造原请求 kind/subtype 的成功/失败 result，不关闭客户端连接，也不终止服务进程；
6. 后续请求继续由同一服务进程正常处理。

## 契约说明

- `25/12 + 1/3/3` 与地图侧切磋结果/遭遇拒绝提示使用同一已验证交付路径
  （`spar_result_deliver` / map encounter reject），用于降低空 CBMR 导致的
  条幅等待卡住；它不是对应业务 handler 的完整替代。
- 仍依赖特定 subtype 清等待位的操作（例如 `7/4` 丢弃）必须继续由窄处理器
  返回正确 result；通用未实现提示不能替代这些契约。

## 验证重点

- 服务端构建能够通过编译；
- 发送未知 WT 包后进程仍存活；
- 日志同时出现原始 `unhandled` 诊断及 `source=unimplemented-handler-notice`；
- 未知包的响应包含 `25/12` 与 `1/3/3`，客户端聊天区可见「功能暂未实现(...)」；
- 随后的正常请求仍能得到响应。
