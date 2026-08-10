# NPC 多服务对话：协议与实现调查

日期：2026-08-10  
状态：已实现，待客户端回归验收

## 1. 当前目标

动态 NPC 目前只有一个 `npc_kind`，因此一个 NPC 不能同时提供任务、商店、修理、技能或副本
服务。需要将“服务入口”改为一个有序、可配置的集合；每个入口可单独设置名称和说明，留空时
使用服务默认文案。任务绑定保持独立，故“接取任务 + 传送”应是一个可支持的正常组合。

## 2. 客户端协议证据

### 请求

- 点击 NPC：`WT 26/1`，单对象 `1/26/1`，`type=1,id=<actor-id>`；现有 detector 为
  `vm_net_mock_is_npc_dialog_request`。
- 点击对话服务行：`WT 26/1`，单对象 `1/26/1`，`type=2,id=<service-value>`；现有
  `vm_net_mock_build_npc_service_dialog_response` 负责其子页面。

### 响应与 parser

- `江湖OL.CBE:ParseNPCDialogData (0x010380E8)` 读取 `1/26/1 {hidebtn,dialog}`。
- `dialog` 顺序为：`kind:u8`、`main_text:string`、`option_count:u8`，每项依次为
  `display_type:u8,name:string,action:u8,value:u32,description:string`，随后是
  `button_count:u8`。
- 同函数将选项存入从 `global+38444` 开始的 64 字节槽位；第 11 个槽会与后续任务大厅状态
  (`global+39084` 起)重叠。因此服务端必须限制所有初始对话选项（任务 `action=4` 与服务
  `action=1` 合计）最多 10 项，不能仅依赖客户端的 `u8` count。
- `task_hall_activate_selected_entry`（`江湖OL.CBE:0x010492B0`）确认：`action=1` 发送
  `type=2` 服务请求；`action=4` 进入既有任务详情/接取/提交流程。当前已完成协议只允许
  这两种 action，后台不能开放任意 action/value。

## 3. 当前服务端链路与首个契约缺口

1. `vm_net_mock_build_npc_dialog_response` 读取 NPC seed 的单个 `kind`，在任务选项后仅拼接
   一行 `action=1` 服务入口。
2. `vm_net_mock_npc_service_context_record` 只记录单个 `serviceKind`。子页面中武器/防具
   有部分上下文校验，修理、技能、回收及副本入口没有统一的“该行确实由被点击 NPC 下发”校验。
3. 动态 NPC 表 `server_dynamic_npcs.npc_kind` 与原生覆盖表 `server_native_npc_overrides.service_kind`
   都是一对一字段；上轮新增的 `service_option_name/description` 也只能修饰该单行。

因此首个被违反的契约不是文案，而是：客户端能够安全承载多行 parser-backed `action=1`
入口，但服务端授权上下文、持久化模型和编辑器仍假定一行服务。若只在 UI 加多选，会造成
未授权的 `type=2` 路径或库存归属错误。

## 4. 最小正确数据模型

新增 `server_npc_services`：

- 主键：`(scene,actor_id,service_kind)`；`service_kind=1..7` 对应既有 7 个 parser-backed
  服务种类。
- `sort_order`、`option_name`、`option_description` 为各入口独立配置。
- `service_kind=0` 是“已迁移/已显式配置但没有服务”的标记行，保证把旧单服务 NPC 改为仅任务
  NPC 后不会悄悄回退到旧 `npc_kind`。
- 无该 NPC 配置行时，继续从旧 `npc_kind` / 原生覆盖 `service_kind` 读取一项，兼容现存数据；
  旧单项的名称/说明仍可作为这一项的文案覆盖。

每次服务选择只被授权到当前会话的 `(role,scene,actor,service-mask)`，并且 mask 仅包含此次
实际下发且未被 10 项总上限裁剪的服务；服务子页面必须校验相应 bit。任务 `action=4` 继续由
既有任务上下文管理，不进入该 mask。

## 5. 本轮实现范围

- 用上述关系表读取、保存和解析动态/原生 NPC 的多服务配置；旧单服务数据保持回退。
- 将单值 service context 改为 mask，补齐修理、技能、装备回收和副本入口的授权校验。
- 将动态 NPC 编辑改为“服务多选 + 每项独立名称/说明”；名称和说明默认空，空即使用默认文案。
  原生 NPC 覆盖复用该编辑器，但不允许副本向导（原生 SCE NPC 没有目标场景/坐标/怪物配置）。
- 对包含武器、防具、药品商店的 NPC，按已启用的商店服务分别呈现库存面板；库存 POST 显式
  带 `service_kind` 并以持久化服务集合复核。
- 迁移/规范文档加入 schema、手动升级脚本和场景别名迁移中对服务表的复制。

不改动客户端二进制、寄存器、内存或网络投递；不暴露尚无协议实现的 action。

## 6. 实现落点

- `src/server/mock_server_scene_task.c`：关系表创建、读取、精确场景迁移和动态/原生 NPC 保存事务。
- `src/server/mock_server_scene_sync.c`：以 `(role,scene,actor,service-mask)` 记录本次实际编码的服务集合；服务响应在编码成功后才授权。武器、防具、药品、修理、技能、副本和装备回收均按各自 mask 校验。
- `src/web_admin_server.c`：动态 NPC 与原生 NPC 覆盖统一改为多选服务行；每行都有可选名称、说明。动态 NPC 可额外选择副本向导并填写其目标；任务选择仍为独立字段。
- `server/mysql/migrate_add_npc_multiple_services.sql`：部署到已有数据库时使用的一次性 DDL；服务启动的目录加载路径也会确保表存在。

副本向导的后续 `type=2` 请求也重新解析关系表，而不是检查旧 `npc_kind`。因此“武器商店 + 副本传送”这类首项不是副本向导的组合仍会正确找到同一个 NPC 的副本目标。

## 7. 验证清单

- [x] `make -j2` 通过。（代码最终提交前会重新构建。）
- [x] 旧 `npc_kind` 行没有 `server_npc_services` 配置时仍产生原先的单服务行。
- [ ] 新动态 NPC 可同时下发任务 `action=4` 与副本 `action=1`，服务行 `type=2` 仍走既有子页。
- [ ] 一个 NPC 多项商店服务的库存 POST 只允许对应服务类别与有效商品。
- [ ] 修理、技能、回收、副本服务不能由未点击/已切图的旧上下文调用。
- [ ] 初始对话的任务与服务项总数不会超过 10。
