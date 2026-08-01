# 同账号 sticky restore + 跳过干净 roleDb capture（阶段 B）

日期：2026-08-01

## 代码现状（2026-08-01 审计）

**未在当前树。** 无 `g_vm_mock_service_globals_account` / `sticky_restore skipped` /
`role_db_capture skipped`。每请求仍全量 `account_restore`/`capture`（含 roleDb）。
依赖的阶段 D session timer 权威亦不在。详见 `2026-08-01-server-baseline-audit.md`。

下文为曾规划/已撤回的阶段 B 方案，勿按验证清单验收。

## 背景

阶段 D 已把 auto/hangup timer 迁到 session。阶段 B 要缩短
`account_restore` / `account_capture` 占用：最大成本是
`roleDb`（及整份战斗/场景字段）的反复 memcpy。

## 修改

1. **Sticky restore**：`g_vm_mock_service_globals_account` 记录当前进程全局
   镜像的账号。连续请求同一 `account_state*` 时跳过整份字段灌入，只：
   - 清 request-local team wire scratch
   - 从 session 重载 auto/hangup timer
   - 对齐 revival confirm
2. **Capture 跳过 roleDb**：当 globals 仍镜像该账号且
   `!rolePositionDirty && !roleInventoryDirty` 时，不拷贝 `roleDb`。
   warehouse 仍按原 `loaded` 条件写入。
3. 账号 idle release 时若释放的是 globals 镜像，清空 sticky 指针。

## 依赖

- 协议锁仍串行化 restore；跨账号交替会自然 `skipped=0` 全量灌入。
- Session timer 权威（阶段 D）保证 sticky 路径不会丢掉 playback hold。

## 验证

1. 重建并重启 `jh-online-server`。
2. 单人走动/空闲 poll：日志 `sticky_restore skipped=1` 与
   `role_db_capture skipped=1` 应占多数。
3. 多人交替：账号切换出现 `skipped=0`；timer 隔离仍成立（prefer/nextActMs
   按 client 分开）。
4. 挂机 settle / 背包变脏：对应请求 `role_db_capture skipped=0`。
