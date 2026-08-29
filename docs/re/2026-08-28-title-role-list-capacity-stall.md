# 标题服务器确认时角色列表容量导致的进度条卡住

Date: 2026-08-28

Status: implemented; wire-level validated; deployment pending existing server exit

## 1. 当前卡点

- 可见现象：账号拥有较多角色时，在标题服务器列表确认后，进度条不会自行消失。
- 触发方式：完成 `WT 1/1/12` 登录和 `WT 1/1/16` 标题门闩，再提交带
  `serverID`、`moneytype` 的单对象 `WT 1/1/4`。
- 本轮最小目标：让包含客户端支持范围内全部角色的 `1/1/4.actorinfo` 正常构建并通过
  原有网络事件投递；不裁剪角色、不修改客户端状态或 callback。

## 2. 运行时与代码证据

- `vm_net_mock_build_title_server_select_response()` 以局部 `u8 actorInfo[128]` 调用
  `vm_net_mock_build_title_role_list_actorinfo()`；后者容量不足时返回 `0`，前者也返回 `0`。
  dispatcher 只有在 builder 返回非零时才记录 `builtin-title-server-select` 并投递响应，
  因而该失败路径不会产生客户端 data event。
- 紧凑标题角色表的确定长度为 `3 + 19 * role_count + sum(role_name_bytes)`。
  因此 5 个合法角色（客户端上限）已占 98 字节，加上名称后可超过 128 字节。
- 现有 `docs/re/2026-08-19-remote-title-empty-role-list.md` 已把该 128 字节边界列为
  独立风险：构包失败会产生 `resp=0`，客户端保持等待；这与本次可见现象相符。

## 3. IDA / 客户端契约

| binary | function/address | findings |
| --- | --- | --- |
| `mmTitleMstarWqvga.cbm` | `sub_3544 (0x3544)` | 读取 `actorinfo`：计数后逐条读取 `roleId, jobIndex, sex, name, level`；客户端上限为 5。 |
| `mmTitleMstarWqvga.cbm` | `0x3646..0x36D8` | 读取计数并完成规则解析和页面切换；零计数是合法的“新建角色”页面，不是本问题的成功替代。 |

上述 IDA 结论来自已存档的标题解析记录 `2026-06-26-local-role-db.md` 与
`2026-08-19-remote-title-empty-role-list.md`。当前会话没有可调用的 IDA MCP 实例，
所以本轮不扩展未知客户端字段，只按已确认的紧凑 payload 和最大 5 行边界修复宿主
临时缓冲。

## 4. 调用链 / 首次偏离

1. 标题客户端发送 `WT 1/1/4 { serverID, moneytype }`。
2. `vm_net_mock_build_response()` 命中窄 detector
   `vm_net_mock_is_title_server_select_request()`。
3. `vm_net_mock_build_title_server_select_response()` 构建 `servconf` 和紧凑
   `actorinfo`。
4. 旧实现用 128 字节局部数组装载 `actorinfo`；合法五角色名称总长度较大时，
   `vm_net_mock_build_title_role_list_actorinfo()` 的第一处容量检查失败。
5. builder 返回 `0`，dispatcher 不投递原 event type/callback 的响应，客户端
   `1/1/4` 等待窗口没有自然收尾。

## 5. 请求 / 响应契约

### Request

- WT: `1/4`
- objects: 一个 `1/1/4`
- key fields: `serverID:u32`, `moneytype:u32`

### Response

- WT: `1/4`
- objects: 一个 `1/1/4`
- key fields: `result=1`, `servconf`, `actorinfo`, 既有强化规则 `num/data`
- `actorinfo`: `count:u8` 后重复至多 5 条
  `roleId:u32, jobIndex:u8, sex:u8, name:string, level:i16`。

## 6. 已排除的方案

- 不能把第 6 个及以后角色截断为看似成功的结果；账号角色集合是数据库权威数据，客户端
  支持上限应由数据不变量拒绝，而不是由标题响应悄悄删除。
- 不能以零角色 actorinfo 替代构建失败；这是“无角色”的合法业务状态，无法表示这次
  已有角色却响应无法构建的故障。
- 不改 CBE/CBM、寄存器、PC/LR、screen 或宿主 callback/事件时序。

## 7. 实现

- `VM_NET_MOCK_TITLE_ROLE_LIST_ACTORINFO_CAP` 按已确认的
  `3 + max_roles * (19 + 最大角色名字节数)` 推导；当前 `5 * 31` 名称边界为 253 字节。
  这个符号与 `vm_net_mock_role_state.name` 绑定，角色名存储上限变化时容量会同步变化。
- `vm_net_mock_build_title_server_select_response()` 和遗留
  `vm_net_mock_build_title_rolelist_stage_response()` 都改用这一上限；没有改动请求 detector、
  WT 字段、callback、event type 或客户端状态。
- 新增 `title-role-list-capacity-regression`：纯进程内构造五条 31 字节名称的合法角色，
  通过真实 `WT 1/1/4` dispatcher 验证 `builtin-title-server-select` 与完整 actorinfo，
  并验证 staged 变体及单角色相邻路径。夹具不启动 listener，不配置 MySQL 连接，也不读写
  用户账号；总 dispatcher 中早于标题 handler 的历史查询会因未初始化 MySQL 返回
  `no MySQL error detail`，但没有建立数据库连接或写入。

## 8. 验证清单

- [x] `1/1/4` 被既有标题服务器确认 detector 命中，source 为
  `builtin-title-server-select`
- [x] 最大合法角色表返回非零 WT response；真实宿主原有的返回/网络事件路径未改
- [x] actorinfo 按 `mmTitle:0x3544` 已确认的 5 行字段顺序包含全部最长名称；staged
  变体也保留相同表
- [x] 没有强写客户端全局状态、寄存器、PC/LR 或改动 CBE/CBM
- [x] 单角色相邻路径仍经 `builtin-title-server-select` 返回 53 字节 actorinfo
- [x] 结果已回写到本文件

## 9. 验证结果与部署边界

执行：

```text
make title-role-list-capacity-regression
.\\obj\\server\\title-role-list-capacity-regression.exe
```

结果：

```text
title role-list capacity regression passed max_actorinfo=253 roles=5 single_actorinfo=53
```

`make -j2` 已重新编译 `src/server/mock-server.c` 和客户端目标；最终写入
`bin/jh-online-server.exe` 时被已有 PID `37568` 占用而失败。该进程不是本次自动化启动，
因此没有停止或替换它。将相同对象链接到新的临时输出
`tmp/jh-online-server-title-role-list-capacity-link.exe` 已成功，证明服务器链接本身通过。

待用户停止自己启动的服务后，应重新执行 `make -j2`，再以新
`bin/jh-online-server.exe` 复现原始“5 个长名称角色 -> 服务器确认”路径。预期服务记录
`builtin-title-server-select` 的非零响应，客户端按既有 `mmTitle:0x3544` parser 收到完整
actorinfo，并由其正常网络完成路径收尾进度条。无需、更不得手动隐藏进度条或改写客户端状态。
