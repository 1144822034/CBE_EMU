# 账号 21642502（武林盟主）登录后背包为空

## 触发与范围

使用账号 `21642502` 登录，选择当前激活角色 `10036`（名称：武林盟主）并进入场景。
客户端背包界面为空，但 MySQL 中已有该角色的背包记录。

本次仅调查背包初始化和背包打开两个客户端可见路径；不以伪造空列表、重置角色或
修改客户端内存作为规避手段。

## 持久化证据

2026-07-26 对本地 MySQL 的只读查询结果：

- `account_role_state.active_role_id = 10036`，`role_count = 3`；
- `account_roles` 中 `role_id=10036` 的名称为武林盟主，`backpack_capacity=100`，
  `backpack_item_count=47`；
- `account_role_backpack` 中 `role_id=10036` 恰有 47 行。其中 `808` 一行按既有
  客户端契约不属于通用背包网格，剩余 **46** 行均由
  `vm_net_mock_backpack_item_is_client_grid_item()` 判定为客户端普通背包行。

因此数据并未绑定到另一个角色，也不是数据库读空。

## 已确认的客户端与服务端链路

1. 选角后的 `5/10 + 7/7(type=1)` 组响应必须包含一次 `1/30/21` 背包网格。
   既有 IDA 证据（`docs/re/2026-07-24-account-21642502-login-backpack-crash.md`）表明
   `JianghuOL.CBE:0x01039952` 按
   `itemId(u32) + seq(i16) + count(u32) + common-extra` 建立主道具管理器记录。
2. 之后背包界面请求的 `1/17/1` 由 `mmGameMstarWqvga.cbm:0x418C` 读取，供界面列举；
   它不是首个主道具管理器初始化的替代协议。
3. 每个 `30/21` 行在现有序列编码中恰为 27 字节：`u32`（6）+`i16`（4）+
   `u32`（6）+common-extra（11）。46 行需要 `46 * 27 = 1242` 字节。
4. 当前 `vm_net_mock_append_backpack_grid_object()` 给该内部 `iteminfo` 缓冲只分配
   1024 字节。第 38 行开始写入时，
   `vm_net_mock_build_backpack_grid_iteminfo_blob()` 返回失败，继而
   `vm_net_mock_append_backpack_role_grid_main_objects()` 让整个选角组响应构建失败。
5. 同一文件的 `1/17/1` 列表也只有 1024 字节；每行 17 字节加 3 字节计数，当前
   46 行尚能容纳，但客户端允许的 64 行最多需要 1091 字节，仍存在同类上限违约。

先前的 21642502 回归数据只有 29 个普通网格行（783 字节），故 1024 字节缓冲未暴露
这个问题；当前角色数据增长后稳定触发。

## IDA 可用性与已排除假设

本机已按 `binary_name` 枚举 IDA 实例；仅有 `MT6252_CH.bin`，未打开
`JianghuOL.CBE` 或 `mmGameMstarWqvga.cbm`。因此本次复用上述已记录、可定位的客户端
parser 证据，不伪造新的 IDA 结论。

已排除：角色选择错误、MySQL 行缺失、808 金元宝重新进入普通网格、客户端 64 格
容量以外的数据直接下发。后者仍由既有 64 格边界限制。

## 可检验的根因陈述

当一个角色具有 38--64 条客户端可表示的普通背包行时，服务端登录首包的
`30/21.iteminfo` 临时缓冲容量（1024）小于合法协议载荷（最大 1728），违反了
客户端 64 个逻辑背包槽所允许的完整初始化契约。首个错误状态是 `30/21` 构建失败，
不是后续 `17/1` 界面空白。

## 计划修改与验证

- 用与客户端 64 格上限及已确认行编码相匹配的具名最大 `iteminfo` 容量，供 `30/21`
  和 `17/1` 共用；不改变物品筛选、容量公布或持久化数据。
- 以本地 mock-service 真实登录回放账号 21642502，验证首个组响应含 46 行 `30/21`
  （1242 字节）、后续 `17/1` 含同样 46 行，并复测重复选角不会重复播种。
- 覆盖较少物品角色，随后执行 `make -j2`。

## 修复与验证结果

修复把 `30/21` 每行的已确认最大编码长度（27）与客户端 64 格逻辑容量定义为
`VM_NET_MOCK_BACKPACK_CLIENT_ITEMINFO_MAX_BYTES`（1728），并让登录网格与 `17/1`
背包列表共用该上限。它只调整临时序列化工作区：不会改变 MySQL 行、不会把 808
重新发为普通道具，也不会把客户端公布容量提高到 64 以上。

修复前的隔离服务回放记录为：选角成功并写出 solo `5/10` 后，复合
`5/10 + 7/7(type=1)` 请求得到 `response=0 source=ignored-unhandled-server-only`；
这是 `30/21` 内部 1024 字节构建失败向上传播后的表象。

修复后，在相同本地 MySQL 数据和同一请求序列上：

```text
backpack login reseed regression passed user=21642502 role=10036 \
  first=1810 duplicate=234 open=853 relogin=1810 forbidden=808 capacity=64 rows=46
mock_backpack_grid role=10036 gridnum=46 stored_rows=47 iteminfo_len=1242
mock_backpack_items role=10036 capacity=64 rows=46 stored_rows=47 iteminfo_len=785
```

首次和重新选角后的组响应都包含完整 46 行 `30/21`；中间的重复组请求为 234 字节且
不重放网格，背包打开的 `17/1` 也精确包含 46 行。相同账号的另一角色 `10311`（7 行、
容量 20、6 个 802 储量项）也通过了相同的登录/重复请求/打开/重新选角回归：

```text
backpack login reseed regression passed user=21642502 role=10311 \
  first=598 duplicate=234 open=190 relogin=598 capacity=20 rows=7
```

该相邻路径会通过正常选角流程暂时更新 `active_role_id`；回归完成后已将本地数据库中
账号 21642502 的该值恢复并验证为原始角色 `10036`，未保留测试状态变更。

`tmp/backpack-login-reseed-regression.php` 现可选地断言首包、背包打开和重新选角的
确切客户端行数，防止这类“存在对象但漏行”的回归。

`make -j2` 通过，且 `git diff --check` 无空白错误。额外的
`make boundary-check` 仍失败；扫描命中了本次未修改的
`src/server/mock_server_transport.c`、`mock_server_social.c` 等历史文件中的
`uc_mem_*`/`Global_R9` 客户端内存访问。该失败不由本次背包序列化修改引入，已作为
服务端与客户端完全分离工作的既有边界债务保留，不能在本次背包修复中用屏蔽规则掩盖。

## 实际远端验证（2026-07-26）

客户端当前配置连接 `23.141.172.143:19090`。该端点可达。以同一 CBMS 登录、选角、
组请求回放验证：远端的相邻角色 `10311`（7 行）仍完全通过，响应长度也与本地修复版
一致（`first=598 duplicate=234 open=190 relogin=598`）；但重新选回原角色 `10036`
后，同一 `5/10 + 7/7(type=1)` 请求仍返回 **0 字节**，没有 `30/21`。

这将问题收窄为远端运行的服务二进制仍包含旧的 1024 字节背包网格缓冲：它能处理
7 行（189 字节）却不能处理 46 行（1242 字节），而本地修复版对两种情况均已通过。
远端回放结束前已重新选中 `10036`，不保留相邻角色测试造成的激活角色改变。要让
实际客户端生效，必须替换并重启远端的 `jh-online-server` 为本次构建产物；单独修改
本地工作区或客户端的连接地址不会更新远端进程。

## 本地端点重新验证（2026-07-26）

用户将客户端端点改回 `127.0.0.1` 后，检查发现源文件虽已是 localhost，
但 `bin/main.exe` 的生成时间早于该修改，仍是连接旧远端的客户端二进制。本地 19090
也没有正在监听的服务进程。

已停止测试期间的旧本地进程，执行 `make -j2` 重新链接客户端和服务端，随后以
`bin/jh-online-server.exe --mock-service-only --mock-service-bind=127.0.0.1
--mock-service-port=19090` 启动当前构建。对 `10036` 的本地真实回放再次通过：

```text
backpack login reseed regression passed user=21642502 role=10036 \
  first=1810 duplicate=234 open=853 relogin=1810 forbidden=808 capacity=64 rows=46
```

服务日志同时确认 `30/21 gridnum=46 iteminfo_len=1242` 与
`17/1 rows=46 iteminfo_len=785`。客户端需要完全退出后从新生成的 `bin/main.exe`
重新启动，才能载入 localhost 端点与当前服务端；不能复用此前已经启动的旧客户端进程。

## 仍存边界

持久化库仍可保留超过 64 条普通行，但客户端没有超过 64 格的已证实初始化协议。
本修复覆盖合法的 0--64 行客户端网格，不能把超过 64 行静默截断或伪造为成功；若出现
该数据状态，仍需以独立的迁移或经过取证的仓储/分页协议处理。
