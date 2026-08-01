# 多角色账号选服卡住（1/1/4 unhandled）

Date: 2026-07-30

Status: fixed (server)

```text
account: wwe000wwe (5 roles)
phase: title server select WT 1/1/4
trigger: 登录后选服卡住；日志 unhandled wt=1/4 ... response=0
```

## 根因

1. Detector 已命中 `1/1/4` + `serverID`/`moneytype`。
2. `vm_net_mock_build_title_server_select_response` 用栈上 `actorInfo[128]`
   组装角色列表；5 个角色的 compact actorinfo 超出 128，
   `build_title_role_list_actorinfo` 返回 0。
3. Builder 静默 `return 0` → dispatch 落入
   `ignored-unhandled-server-only`，客户端无选服 ACK，卡在标题。

登录成功路径已用 `actorInfo[512]`，选服/rolelist 路径漏扩。

## 修改

1. 选服 / rolelist-stage 的 actorinfo 缓冲改为 **512**（与登录一致）。
2. actorinfo 失败时打 `mock_title_server_select actorinfo_len=0` 错误日志（含 `role_count`）。
3. encode 失败与 detector 命中但 builder=0 时分别打错误日志，避免再静默 unhandled。

## 验证

1. **必须重启新二进制**：`bin/jh-online-server.exe` 时间戳需晚于源码修改；旧进程会继续 `unhandled 1/4` 且无新日志。
2. `wwe000wwe` 登录选服：应见 `builtin-title-server-select` /
   `mock_title_server_select ... actorinfo_len=...`，无 `unhandled wt=1/4`。
3. 单角色账号回归不受影响。
4. `make -j2 server`。
