# PixiJS 标题创建角色接入

## 契约

服务端已有 `builtin-title-role-create`（`docs/re/2026-06-26-title-role-create.md`）：

| 方向 | WT | 字段 |
|------|-----|------|
| 请求 | `1/1/7` | named `job`(career 1..3)、`sex`(sexGroup 1..2)、`name` |
| 应答 | `1/1/7` | `actorid:u32`、`result:u8`（0=成功） |

CBE 本地成功路径（`mmTitle:0x5324`）把新行插在创建哨兵前，不依赖再拉列表。

## Pixi 改动

1. `buildRoleCreateRequest` / `parseRoleCreateResult`：named 路径发 `job=jobIndex+1`、`sex=sex?2:1`。
2. `GameSession.createRole`：成功后本地追加 `RoleRow`（level=1）并 `onRoles`。
3. `packetHasTitleRoleList` + `confirmServer`：允许 `actorinfo` count=0 进入选角（无角色则创角）。
4. 游客不走空凭据直连：标题「开始游戏/登陆游戏」均进登录创建或登录账号。
5. `CreateRoleScreen`：登录后无角色或追加创角；`RoleScreen` 哨兵 `onCreate`。
6. 角色上限 `TITLE_ROLE_MAX=5`（与 mock DB 一致）。

## 验证

- 未登录游客：点开始/登陆 → 登录页创建账号，不空凭据进服。
- 已登录但无角色：选服 → 选角屏仅「创建角色」→ 提交 → 继续进图。
- 已有角色：可追加创角；男/女与三职业外观正确。
- 满 5 角：哨兵隐藏；服务端 `result=1` 时提示失败。
- 重名：本账号角色列表已有同名，或应答 `result=2`，创建屏明文提示「名已被注册」。
- `npx tsx scripts/smoke-protocol.ts`：create 请求头 1/1/7 与 ack 解析。
