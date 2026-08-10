# 动态 NPC 对话服务选项：名称与功能契约

> 已由 [NPC 多服务对话：协议与实现调查](2026-08-10-npc-multiple-services.md)
> 取代为兼容层说明：这里的旧单服务字段继续只服务于未迁移数据的回退，不再是新编辑器的
> 权威配置入口。

日期：2026-08-10  
状态：已实现；待管理员页面和真实客户端交互回归

## 触发与预期

管理员需要为动态 NPC 定义客户端 NPC 对话框中服务入口的可见名称，并且清楚该入口
对应的实际功能。可见文案不能改变客户端已经固定的 action 协议；否则后台会生成客户端
可点击但服务端没有完整处理链路的菜单项。

## 客户端与协议证据

- 点击场景 NPC 请求 `WT 26/1`；服务端当前由 `builtin-npc-dialog` 处理，并返回同一
  根路径的 `1/26/1 { hidebtn, dialog }`。
- `ParseNPCDialogData`（`江湖OL.CBE:0x010380E8`）消费的 `dialog` 是：
  `kind:u8, main_text:string, option_count:u8`，随后每项为
  `display_type:u8, name:string, action:u8, value:u32, description:string`，末尾为
  `button_count:u8`。
- `task_hall_activate_selected_entry`（`江湖OL.CBE:0x010492B0`）表明：
  `action=1` 发送服务子请求 `26/1 {type=2,id=value}`；`action=4` 进入任务详情、接取
  或提交流程。其余 action 值通向不同的旧模块协议，当前没有完整服务端契约。
- 现有任务选项已经由角色任务状态、任务绑定与 XSE 对话状态机生成并使用 `action=4`。
  因此不应由动态 NPC 表的自由文本覆盖或伪造任务 action。

已排除的方案：把 action、value 作为后台任意数字输入。它会绕开功能检测、库存/任务
上下文记录和相应服务处理器，违背客户端 parser 与服务端 action 所有者的契约。

## 实现

`server_dynamic_npcs` 增加两个可选 GBK 字段：

- `service_option_name`：服务菜单的可见名称。
- `service_option_description`：服务菜单的可见说明。

后台“动态 NPC”编辑器把原“服务类型”标为“对话服务功能”，并提供上述两个输入框。
选择的 `npc_kind` 仍是唯一的功能权威：武器/防具/药品商店、装备修理、技能导师、
副本向导与装备回收各自保留原有的 `action=1,value`。非普通服务 NPC 设置了自定义
文字时，`vm_net_mock_build_npc_dialog_response` 只替换该 action-1 行的 name/description，
不会改动 action、value、任务选项或服务 handler。普通/任务 NPC 没有 action-1 服务行，
保存时显式清空这两个字段。

字段容量为 64/96 个字节；网页将名称限制为 30 个字符、说明限制为 45 个字符，确保
GBK 双字节文本也不会在协议字段中被截断。

新库由 `schema.sql` 与 `migrate_add_dynamic_npcs.sql` 建立字段；已有库可在停服窗口
执行 `migrate_add_dynamic_npc_dialog_options.sql`。运行时也先检查 `INFORMATION_SCHEMA`
后补齐缺列，避免旧库因为查询未知列而加载失败。

## 验证边界

构建后在后台给一个动态武器商人填写自定义名称/说明并保存；重新进入该场景并点击 NPC。
预期 `mock_npc_dialog` 日志含 `service_action` 和 `service_option_custom=1`，客户端显示
自定义文字；点击该行仍请求原有 `26/1 {type=2,id=...}` 并进入 `builtin-npc-service` 的
武器库存路径。再将服务功能改为普通/任务 NPC，预期不会下发服务行，且任务 action-4
流程保持不变。
