# 场景“力”时效图标点击进度条卡住

Status: implemented；报文级回归已通过；客户端运行时复测待重启占用旧服务端二进制的进程后进行。

## 最小现象与首次偏离

用户点击场景左上角由大力丸／神力丸等攻防时效道具显示的“力”图标后，客户端出现进度条且不会自行消失。

这个图标使用的 `ruffianflag` 与背包使用道具的 `1/22/3` 不是同一客户端阶段。旧服务端只处理了道具使用的 `1/22/3`，以及“练”与“悟”图标的 `1/7/31`、`1/7/36` 说明请求；没有 `1/22/6` 的说明回包。因此点击后服务端返回零字节，正常网络 data event 不会入客户端队列，进度条也就无法走自己的完成逻辑。

本次工作区没有这次点击的原始 `net_trace.log`；上述首次偏离由用户复现现象、现有 dispatcher 路由缺口以及 CBE 解析器交叉确认。后续运行时复测应保留新服务端的 `builtin-timed-combat-status` 日志，确认实际请求为该精确 9 字节对象。

## 客户端契约

`江湖OL.CBE:net_handle_ruffianflag_info (0x01010F6C)` 的 subtype `6` 分支先读取 `info` 字符串、再读取 `ruffianflag` 字节，随后调用原生提示路径。该路径是进度条完成后显示状态说明的客户端路径；没有任何宿主 UI 或客户机状态写入参与。

实现严格接受以下单对象、空载荷请求：

```text
WT 1/22/6 {}
```

回应也仅包含一个同 subtype 对象，字段顺序保持客户端读取顺序：

```text
WT 1/22/6 { info:string, ruffianflag:u8 }
```

`ruffianflag` 从现有的、仍未过期的攻防时效记录计算。`info` 只描述该效果当前是否生效；查询不会消耗道具、延长时效或写入角色状态。

## 修改

- `src/server/mock_server_timed_status.c` 新增严格的空 `1/22/6` detector 和说明 response builder。
- `src/server/mock_server_dispatch.c` 在通用时效道具使用逻辑前路由该状态查询，记录来源为 `builtin-timed-combat-status`。
- `src/server/mock_server_role.c` 将已有攻防时效活动标志作为跨模块只读接口公开，供说明响应使用。

## 验证

`timed-combat-status-regression` 构造精确的 9 字节空 `1/22/6` 请求，并断言：

- 正常 dispatcher 返回唯一的 `1/22/6`；
- `info` 非空，`ruffianflag` 与当前无活动效果的状态一致；
- 截断请求或带 payload 的请求均不会被此 handler 接收。

隔离回归执行通过：

```text
timed-combat-status regression passed: empty 22/6 receives one info + ruffianflag description object
```

`make -j2` 完成所有源码编译，但链接 `bin/jh-online-server.exe` 时被既有 PID 4956 占用；该进程未被终止。重启服务为新二进制后，人工复测应确认进度条消失、客户端显示攻防时效说明，并在服务端日志看到 `mock_timed_combat_status request=22/6` 与 `builtin-timed-combat-status`。
