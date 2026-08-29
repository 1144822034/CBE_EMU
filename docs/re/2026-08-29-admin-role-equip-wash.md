# 后台角色明细：调整装备洗练属性（2026-08-29）

## 范围

账号管理 → 角色明细：列出穿戴 / 背包装备的 +4/+8/+12/+16 洗练覆盖，并允许逐档保存。

## 入口

- 页面：`/?tab=accounts&account=...` 角色明细 `#role-details` 内「装备洗练」区块
- Action：`POST /action` · `set-equip-wash`

## 表单字段

| 字段 | 含义 |
| --- | --- |
| `where` | `bag`（背包 seq）或 `equip`（穿戴 slot 0..7） |
| `loc` | bag=seq；equip=slot |
| `unlock` | 4 / 8 / 12 / 16 |
| `washed` | 1=覆盖词条；0=清除覆盖（回退配置默认） |
| `type` / `flag` / `value` | 属性类型 1..10、百分比位、数值（仅 washed=1 生效） |
| `wash_count` | 该档洗练次数（影响下次金币阶梯） |

## 落点

- UI：`src/web_admin_server.c`（`vm_mock_admin_render_role_equip_wash`）
- Service：`src/server/mock_server_equipment_npc.c`（`vm_mock_service_account_set_role_equip_wash`）
- 持久化：`vm_net_mock_role_db_save_role("admin-equip-wash")` + 非空时 `enhance_wash_record_upsert`
- AJAX：保存成功后 `refreshRoleDetails`（含 `set-equip-wash`）

## 契约

与游戏内洗练一致：`enhance_wash` 文本列四组 `w,t,f,v,c`；`washed=0` 时客户端用 `server_enhance_milestones` 默认档。

## 验证

- [ ] 选账号 → 角色明细看到穿戴/背包装备四档
- [ ] 设「已洗练」+ 属性/数值 → 刷新后显示覆盖；MySQL `account_role_backpack/equipment.enhance_wash` 更新
- [ ] 改回「未洗练」→ 覆盖清除，次数可单独保留
- [ ] 在线角色：重开背包/洗练面板或重登后可见（与其它后台改库存路径一致）

## 已知边界

- 仓库 / 拍卖中的装备不在本页编辑（仅穿戴 + 背包）
- 与其它账号管理写路径相同：打开明细会强制 MySQL 重载，未 flush 的在线脏库存可能被覆盖
