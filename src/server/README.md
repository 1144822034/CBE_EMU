# 服务端源码布局

`mock-server.c` 是服务端聚合入口。它按既有顺序包含下面按业务拆分的实现片段：

- `mock_server_core.c`：共享状态、协议基础和持久化基础设施
- `mock_server_catalog.c`：配置、资源和物品目录
- `mock_server_role.c`：账号、角色与角色状态
- `mock_server_equipment_npc.c`：装备、背包、NPC 与商店
- `mock_server_scene_task.c`：场景数据、任务与传送
- `mock_server_scene_sync.c`：场景同步、附近玩家和移动
- `mock_server_guild.c`：帮派
- `mock_server_social.c`：好友、消息、交易与其他社交操作
- `mock_server_battle.c`：怪物、战斗和队伍战斗
- `mock_server_interaction_login.c`：登录、交互流程与客户端阶段响应
- `mock_server_dispatch.c`：请求识别、复合请求和响应调度
- `mock_server_transport.c`：本地服务端传输、轮询和 Web 管理入口

这些文件刻意仍由一个聚合翻译单元编译，而不是分别编译为多个目标文件。现有服务端的大量 `static` 状态、前置声明和处理顺序跨越业务边界；保留单一翻译单元能在不改变协议行为的前提下完成源码拆分。新增实现应放入所属业务文件，并保持聚合入口中的包含顺序。
