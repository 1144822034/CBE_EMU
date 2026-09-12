# 角色改名卡（2026-09-12）

## 范围

PixiJS 自定义商城道具 **852 角色改名卡**：背包使用后输入新角色名，服务端扣 1 张并更新 `role->name` / `onlineRoleName`。CBE 不接此 UI。

避开雁门旧别名 **850/851**（木材/石料）。

## 道具

| 字段 | 值 |
| --- | --- |
| itemId | 852 |
| 名称 | 角色改名卡 |
| 说明 | 使用后可修改一次角色名。 |
| 类别 | 14（秘宝） |
| 酷宝价 | 100（可后台改价/下架） |
| 耐久 | 1；背包 `count` 为剩余次数（可叠加） |

禁止通用 `7/1` 使用（与 847/849 同）：必须走 29/15。

## 协议（PixiJS，family 29）

| WT | 作用 |
| --- | --- |
| 29/15 | `{ cardseq, name }` → 扣 1 改名卡，写入角色名 |

### result

| 值 | 含义 |
| --- | --- |
| 1 | 成功 |
| 2 | 改名卡不足 / 不是改名卡 |
| 3 | 名称为空 |
| 4 | 过长（GBK ≥32） |
| 5 | 与当前名相同（不消耗） |
| 6 | 名已被占用（本账号 / `account_roles` 全服 / 在线会话） |
| 7 | 扣除失败 |
| 8 | 持久化失败或重名检查查询失败（回滚名称与道具） |

成功响应带 `name`（新名）与 `remaining`。

## 全服重名

权威表：`account_roles.role_name`（VARBINARY）。改名前：

```sql
SELECT COUNT(*) FROM account_roles
 WHERE role_name=X'<gbk hex>' AND role_id<>当前角色
```

另扫在线 `onlineRoleName`（防刚改尚未可见的竞态边缘）。创角 `1/1/7` 与后台 `rename-role` 共用同一 MySQL 检查；创角冲突 `result=2`。

## 客户端流程

背包「使用」改名卡 → `prompt` 输入新名 → `29/15` → 刷新背包 / HUD / 名板。

## 代码落点

- `mock_server_core.c`：`RENAME_CARD_*`
- `mock_server_catalog.c`：商城注入 + special-use 门闩
- `mock_server_role.c`：购买/发放叠堆；`vm_net_mock_account_roles_name_taken`；创角检查
- `mock_server_equipment_npc.c` / `dispatch.c`：29/15；admin 改名
- `PixiJS_web`：`itemCatalog` / `protocolExtras` / `GameSession.renameCharacter` / `main` 使用分支

## 未解决

- 双人同时改成同一名的竞态：无 UNIQUE 索引时仍可能双过 COUNT；可后续对 `role_name` 加唯一约束（需先清历史重名）。
- 帮派成员表 `guild_members.role_name` 等快照字段未同步（与后台 admin 改名一致）。

## 验证

- [ ] 商城秘宝可见「角色改名卡」，购买后背包数量 +1
- [ ] 使用 → 输入新名 → HUD/名板更新，卡 -1
- [ ] 空名 / 同名 / 过长 / 本账号重名 → 对应 tip，不扣卡
- [ ] 其它账号已有同名 → result=6「名已被注册」，不扣卡
- [ ] 创角占用已有全服名 → result=2
- [ ] `make -j2` 通过
