# 远程服务登录后部分账号角色列表为空

Date: 2026-08-19

Status: root path identified; stale remote server build strongly indicated, remote hash pending

## 现象与边界

- 客户端使用非回环服务端 IP 登录后，部分已有 `account_roles` 数据的账号进入角色选择界面时列表为空。
- 当前工作区没有该次远程客户端 `net_trace`、远程服务端日志或受影响账号标识，因此本轮只做代码、IDA 和本地数据库只读取证，不修改协议或业务行为。

## 请求、响应与客户端 parser

角色列表在选择登录服务器后由以下链路下发：

1. 客户端发送单对象 `1/1/4`，包含 `serverID` 和 `moneytype`。
2. `vm_net_mock_build_title_server_select_response()` 构造 `1/1/4`，其中 `actorinfo` 来自 `vm_net_mock_build_title_role_list_actorinfo()`。
3. `mmTitleMstarWqvga.cbm:title_handle_role_list_response(0x3544)` 在此前 `1/1/16.result=1` 已建立成功门闩时读取 subtype 4 的 `actorinfo`。
4. `0x3646..0x364C` 读取 actorinfo 首个 tagged-u8 计数并直接保存为本地角色行数；值为 0 时循环不执行，但 `0x36D2..0x36D8` 仍完成规则解析和页面跳转。

因此，“页面正常出现但角色列表为空”对应一个合法 `actorinfo.count=0`，而不是客户端必须崩溃或停留在加载状态。

## 已识别的首次偏离

`vm_net_mock_build_title_role_list_actorinfo()` 当前使用：

```c
u32 roleCount = g_vm_net_mock_role_db_valid
                    ? g_vm_net_mock_role_db.roleCount
                    : 0;
```

关系角色加载只要在元数据、角色主行、装备行或背包行任一处违反契约，`vm_net_mock_role_db_load()` 就会把 `g_vm_net_mock_role_db_valid` 保持为 false。角色列表 builder 随后不会拒绝响应，而是把数据库失效折叠为一个合法的零角色列表。客户端无法区分“账号确实无角色”和“账号角色数据库加载失败”。

这就是当前能够确定的首个错误状态：已有角色的账号在 `1/1/4` 构包前被表示为 `roleDbValid=false`，响应层又把该错误状态编码成 `actorinfo.count=0`。

已有最接近的已知数据原因是 `account_role_state.role_count` 与 `account_roles` 实际行数不一致。该问题及一次性迁移记录见 `2026-08-19-role-count-authority-migration.md`。远程数据库可能未运行迁移，或者在迁移标记写入后又导入了旧的错误元数据；但没有远程账号和数据库结果前，不能把这一具体原因标记为已证实。

## 远程 IP 排除项

- `network-client.c` 对回环和非回环地址使用同一 `vm_client_remote_request()`。
- CBMS/CBMR 头声明完整响应长度，`vm_client_recv_all()` 循环读取到该长度，不依赖一次 `recv()` 返回完整 TCP 包。
- 客户端完成队列按单 worker FIFO 处理，每个响应单独分配 guest buffer；`1/16` 与后续 `1/4` 不共享同一临时响应指针。

因此 IP 本身没有角色列表专用分支。非回环 IP 实际改变的是所连接的服务进程、服务版本和数据库，现有证据不支持 TCP 分片导致字段丢失。

## 128 字节 actorinfo 缓冲风险

`vm_net_mock_build_title_server_select_response()` 和备用 staged builder 都使用 `u8 actorInfo[128]`。紧凑角色列表长度为：

```text
3 + 19 * role_count + sum(OCTET_LENGTH(role_name))
```

五角色或较长名称可能超过 128 字节。这是独立的真实边界缺陷，但它会让 builder 返回 0，使远程客户端丢弃零长度响应并更可能停留在等待状态，不符合当前“正常进入空角色页”的主要表象。

本机只读统计结果：

- `jh_online` 最大 actorinfo 为 122 字节；
- `jh_online_release` 最大 actorinfo 为 124 字节；
- 两库当前角色数量、连续索引和活动角色引用检查均无异常。

所以本地数据没有命中该缓冲边界，也不能代替远程数据库证据。

## 远程复现所需证据

一次受影响账号复现应同时保留：

- 远程服务端的 `session_bind` 账号；
- 是否出现 `mock_role_db_mysql_load account=<id> source=relational roles=<n>`；
- `net_send ... wt=1/4 ... source=builtin-title-server-select resp=<len>`；
- 该账号的 `account_role_state(format_version,active_role_id,role_count)`；
- `account_roles` 的实际行数、`role_index`、角色名字节数；
- 该账号装备和背包子表是否包含无效角色、槽位、数量或超范围字段。

以当前响应形状计算，`actorinfo.count=0` 的 subtype-4 响应应约为 `resp=182`；已记录的两角色、actorinfo 55 字节响应为 `resp=234`。若远程日志是 `resp=182` 且主表实际有角色，即可确认 `roleDbValid=false -> count=0` 链路。若 `resp=0`，则优先检查 128 字节 actorinfo 构建失败；若响应含非零计数，则第一次偏离转移到客户端 parser/事件投递，需要客户端原始包和 parser 证据。

## 账号 guest00688 的本地基线

用户确认受影响账号为 `guest00688`。对本机 `jh_online` 和
`jh_online_release` 做只读核对，两库的角色列表契约相同：

- `account_role_state` 为 `format_version=8, active_role_id=10495, role_count=2`；
- `account_roles` 正好两行，`role_index=0,1`，角色 ID 为 `10495,10553`；
- 两个角色名共 14 字节，紧凑 actorinfo 长度为 55 字节，未触发 128 字节边界；
- `role-count-authority-v1` 迁移标记存在，角色数量、活动角色、主行字段、装备行和背包行校验均无异常；
- `account_wallets` 行存在；装备和背包子表分别为 9 行和 64 行，均引用现有角色。

本机服务端日志也记录了同一账号的成功链路：

```text
session_bind client=ff9ac2b5 account=guest00688
mock_role_db_mysql_load account=guest00688 source=relational roles=2 active=10495
net_send connect=0 wt=1/4 len=41 source=builtin-title-server-select resp=234
```

客户端随后成功选择角色 `10495` 并进入场景。因此账号标识、角色名字节和固件 parser
本身已由本地路径排除；远程复现必然还包含远程数据库内容、迁移状态或服务端版本差异。
当前工作区没有远程服务端日志、远程数据库连接信息或正在运行的远程客户端进程，尚不能
在本机判定远程副本具体违反了哪一项关系约束。

## 与“部分账号”吻合的服务端版本边界

`guest00688` 的 `account_role_state.format_version` 为 8。版本 8 在提交
`2e0dcfc7`（2026-08-12）中引入，用于持久化装备强化阶段词条。该提交之前：

- `VM_NET_MOCK_ROLE_DB_VERSION` 为 7；
- 元数据 loader 只接受格式 5、6 和当前格式 7；
- 格式 8 会令 `vm_mock_mysql_role_meta_row()` 设置 `context.invalid=true`；
- 后续 `g_vm_net_mock_role_db_valid=false -> actorinfo.count=0` 的行为已经存在。

本机数据库的格式版本不是全量一致，这解释了为何旧服务端只影响部分账号：

```text
jh_online:         version 5 = 13, version 7 = 752, version 8 = 65
jh_online_release: version 5 = 13, version 7 = 700, version 8 = 247
guest00688:        version 8
```

因此当前最强且可检验的具体根因是：远程 IP 指向的服务进程仍是版本 8 支持加入前的旧
`jh-online-server.exe`，但它连接的数据库已经包含版本 8 账号。旧服务对版本 5/7 账号
正常，对版本 8 账号返回空角色列表，精确符合“只有部分账号、远程 IP 才出现”的现象。

本机当前服务器文件为：

```text
SHA256 67B67C0755F31989ED1AC5C563BBE2330688BE3FA8E90B00C223616FDC25C2E3
LastWriteTime 2026-08-19 14:20:20
```

最终确认只需核对远程 `jh-online-server.exe` 的哈希或构建日期。若远程文件早于
`2e0dcfc7`，根因即确认；若远程文件已经支持版本 8，则返回前述远程日志和关系表校验，
继续定位另一项 loader 契约。

## 当前根因陈述

当前根因陈述是：远程服务端在读取 `guest00688` 的格式 8 角色元数据时将其判为无效，
角色列表响应又把该错误状态降格为合法的零角色 `actorinfo`，客户端因此正常进入空列表
页面。仓库历史和混合格式版本分布强烈指向远程服务端二进制仍停留在仅支持格式 7 的
版本；远程二进制哈希尚未取得，所以该最后一项部署事实仍标记为 `unresolved`。
