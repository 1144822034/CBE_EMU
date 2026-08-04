# 动态 NPC 多服务选项

## 触发 / 需求

后台「游戏内容管理」动态 NPC 原先只能选一种 `npc_kind`。同一 NPC 需要同时
提供修理、商店、技能等多项服务时，只能拆成多个 Actor。

## 契约

客户端 `ParseNPCDialogData` 已支持多个对话选项；每个服务仍是独立的
`action=1` + 私有 opcode（`0xE1`…`0xEA`），与既有子菜单路径兼容。任务选项
仍排在服务选项之前。任务 + 服务合计不超过 7 项（与嵌套服务菜单上限一致）。

## 数据

| 字段 | 含义 |
| --- | --- |
| `npc_kind` | 最低置位服务种类（兼容旧逻辑 / 列表展示） |
| `npc_service_mask` | bit N = 启用 kind N（1..6）；0 则运行时从 `npc_kind` 推导 |

迁移：`server/mysql/migrate_add_npc_service_mask.sql`；运行时
`vm_net_mock_dynamic_npc_ensure_service_mask_column` 会 ALTER 并回填。

## 修改点

- Seed：`serviceMask`；`vm_net_mock_npc_has_service` / `effective_service_mask`
- 对话：`vm_net_mock_build_npc_dialog_response` 按 mask 循环追加多个 `action=1`
- 后台：服务选项改为多选 checkbox；勾选「副本向导」才显示副本字段
- 副本查找：`kind==6` 改为 mask bit 6

## 验证

- `make -j2`
- 后台对同一 NPC 勾选两项服务并保存，进图对话出现两个服务入口且均可进入子菜单
- 仅勾选副本向导时副本配置仍必填；不勾选任何服务则为普通／任务 NPC
