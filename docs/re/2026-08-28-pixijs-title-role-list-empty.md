# 2026-08-28：PixiJS 本地登录后人物列表为空

phase: title-role-list  
status: fixed

## 现象

本地 `ws://192.1.1.1:19092` 登录 `lxh001` 成功后进入「选择人物登录」，客户端 `parseRoleListFromSelect` 得到 `roles=[]`；1/1/4 `actorinfo` 仅 3 字节 `[0,1,0]`（count=0）。

## 证据

- MySQL `jh_online.account_role_state`：`lxh001` → `role_count=3`
- `account_roles` 三行：role_id 1 / 289 / 997
- 服务端等价 spouse 查询失败：`Unknown column 'spouse_role_id'`
- `vm_net_mock_role_db_load_mysql_relational()` 因此返回 false → `g_vm_net_mock_role_db_valid=false` → 标题列表 actorinfo count=0

## 根因

`vm_net_mock_role_db_load_mysql_relational()` 在 spouse 行加载前只调用了 `vm_net_mock_role_ensure_pixi_cosmetics_columns()`，**未**调用 `vm_net_mock_role_ensure_intimacy_marriage_columns()`（负责 `spouse_role_id/spouse_account_id/spouse_name` 迁移）。旧库缺列时整库加载失败，标题选角只能回空表。

次要：`account_restore` sticky 路径在 `roleDbValid=false` 时仍可能跳过 MySQL 重载；已在 sticky 条件中要求 `roleDbValid`；标题 1/1/4 构建前强制 `g_vm_net_mock_role_db_loaded=false` 再 load。

## 修改

1. `src/server/mock_server_role.c`：spouse 查询前 `vm_net_mock_role_ensure_intimacy_marriage_columns()`
2. `src/server/mock_server_interaction_login.c`：标题 actorinfo 构建前 invalidate loaded 并重载
3. `src/server/mock_server_equipment_npc.c`：sticky restore 需 `state->roleDbValid && g_vm_net_mock_role_db_valid`

## 验证

重启 `bin/jh-online-server.exe` + gateway 后：

`PIX_GATEWAY=ws://127.0.0.1:19092 npx tsx scripts/probe-role-list.ts`

→ roles: 蔡文姬 L66、阿狸 L4、恋颖 L4（3 个）。

## 操作

部署后必须**重启** `jh-online-server.exe`（旧进程会继续返回空列表）。首次启动会自动 ALTER 补 spouse 列。
