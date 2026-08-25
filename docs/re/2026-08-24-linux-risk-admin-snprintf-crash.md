# Linux 风险审计页 `snprintf` 崩溃（2026-08-24）

## 触发与首个偏离

Linux 服务进程在后台请求 `?tab=risk` 时收到 `SIGSEGV`，故障地址为 `0x1`。
同构建上的 `addr2line` 将故障帧定位到
`vm_mock_admin_risk_audit_query`（`web_admin_server.c:7489`），调用链为风险页渲染、后台
HTTP dispatch、后台 worker；不是客户端 WT 25/5 的 parser 或响应 builder。

风险页先统计三秒内连续进入战斗的审计记录。只要记录数非零，才会构造带
`DATE_FORMAT` 的当前页查询，因而只有存在此类审计记录时触发。

## 根因

查询使用 `snprintf` 拼接 SQL，却把 MySQL 的日期指令写成单个百分号：

```c
DATE_FORMAT(a.created_at,'%Y-%m-%d %H:%i:%s.%f')
```

其中 C 的 `snprintf` 会将 `%s` 当作自己的变参字符串指令；调用只提供页偏移和页大小，
导致读取不存在的变参。本次运行读取到了 `0x1`，随后在 libc 格式化例程内崩溃。

## 修复与范围

- 通过 `vm_mock_admin_risk_audit_build_query()` 统一构造风险页 SQL，并将 MySQL 百分号写为
  `%%Y-%%m-%%d %%H:%%i:%%s.%%f`，运行时 SQL 仍接收原始 `%Y-%m-%d %H:%i:%s.%f`。
- 完整扫描了项目中的 `DATE_FORMAT`、`TIME_FORMAT`、`STR_TO_DATE` 和 `GET_FORMAT`：仅风险
  页与操作日志页各一处；操作日志页原本已正确转义，未发现其他同类 SQL 日期格式。
- 用标准格式函数的编译器检查审计了服务聚合单元；Windows i686 工具链把已有的 `%llu`
  视为 MSVCRT 兼容性问题，不能作为 Linux 格式错误，但没有揭示第二个未转义的 SQL 日期格式。

## 回归

`scripts/risk-admin-pagination-regression.c` 先在不访问数据库的情况下断言生成的 SQL 含原始
MySQL 日期格式、`LIMIT 50,50`，且不残留 `%%Y`。随后才执行原有只读风险页分页检查。
该测试不会封禁账号，也不会写入审计、角色或钱包数据。
