# roleDb 跨账号串号（选角/后台显示错角色）

日期：2026-08-15  
环境：`192.1.1.3` / `jh_online`

## 现象

账号 `15558760210`：

- MySQL `account_roles` / `account_role_state.active_role_id` 只有 **384**（无双）
- 角色 **575**（小小金刚）属于账号 `1501640`
- 客户端选角后进入的却是 575 的皮囊/场景
- 管理后台「账号管理」角色明细也显示 575
- **重启 `jh-online-server` 后恢复正常（384）**

旁证：`account_role_equipment_durability` / `account_role_skills` 在无 FK 的服务表里，
该账号下出现大量外号 `role_id`（含 575），且 575 装备与真号完全一致（14:31 仍在写入）。

## 根因

权威数据在 MySQL；运行时账号角色来自进程堆 `account_state.roleDb`，经
`account_restore` / `account_capture` 与 `g_vm_net_mock_role_db` 镜像。

1. **sticky capture** 在 `globals_account == state` 时拷贝 `roleDb`，未校验
   `active_account_id` 是否仍是该账号。临时切号后若未正确回到原镜像，会把
   **别人的 roleDb** 写进本账号堆缓存。
2. **sticky restore** 同指针即跳过，被污染的 globals 会一直留到进程退出。
3. 管理后台 / 选角都读这份堆缓存，不每次直查 `account_roles`，故与库不一致。
4. `1/1/6` 选角在 `select_active_role` 失败时仍用 `active_role()` 组成功包；
   Pixi 曾丢弃 actorinfo 内 `roleId`，只吃名字/场景 → 表现为「选了 384 进了 575」。
5. `role_service_state_get` 用当前 `active_account_id` + 任意 `role*` 写耐久/技能，
   导致附属表串号（与 roleDb 串号同源症状，但表无 FK）。

## 修复

| 点 | 行为 |
|----|------|
| `account_capture` | 仅当 pointer + `active_account_id` 都镜像本账号时才写 `roleDb`；否则打 error 日志并跳过 |
| `account_restore` sticky | 要求 `roleDbLoaded` 且 id 一致，否则强制全量灌入 |
| `open_account_role_db_for_management` | 打断 sticky，强制 MySQL 重载 |
| `note_durable_role_write` | 拒绝把「当前 globals roleDb」写入非 active 账号 |
| title `1/1/6` | 不匹配则 MySQL 重载一次；仍失败则 `result=0` fail-closed |
| `role_service_state_get` | role 指针必须落在当前 `roleDb.roles[]` |
| Pixi `parseRoleSelectEnter` | 解析并校验 `roleId == selectedRoleId` |

## 验证

1. `make -j2`；Pixi `npx tsc --noEmit`
2. 重启 `192.1.1.3` 服务端
3. 多账号交错在线（含后台打开 A 时 B 在跑）后：后台与选角均显示本账号真角色
4. 日志不应再出现长期 `role_db_capture rejected`；若出现说明仍有切号路径未配对 restore
5. 选角失败应见 `mock_title_role_select rejected` / 客户端「未解析到场景」或串号提示，而不是进错皮

## unresolved

- 历史脏行：`account_role_equipment_durability` / `account_role_skills` 中
  `role_id` 不属于该 `account_id` 的行可择机清理（需确认无 FK 后再删）。
