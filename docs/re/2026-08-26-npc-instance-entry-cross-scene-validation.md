# NPC 副本传送的跨场景目标校验

## 触发与证据

临安南宣门的胡斐（actor `20092`）已保存副本传送配置：目标为 `测试地图.sce`，落点
`(120,120)`，传送后挑战怪物为 `1001`。服务端日志显示保存与 NPC 对话均成功，但点击
“进入副本”后依次出现：

```text
mock_scene_monster_target scene=测试地图.sce actor=1001
  action=reject-unobserved-scene visible_scene=c04临安南门_01.sce
mock_npc_instance_enter_spawn_unresolved actor=20092 scene=测试地图.sce spawn_enemy=1001
```

随后 26/1 对话回显“副本传送点未配置”。因此配置、场景名与落点都不是首个错误状态。

## 根因与修复

`vm_net_mock_scene_battle_monster_instance_entry_scene()` 在构建副本进入的 30/1 之前，错误
复用了仅供已进入场景后的 4/5 战斗路径使用的
`vm_net_mock_select_sce_combat_spawn()`。后者必须验证客户端已创建当前场景的 live node；此时
客户端仍在临安，目标 `测试地图.sce` 尚未加载，故必然被拒绝。

新增的 `vm_net_mock_sce_combat_spawn_resource_has()` 只读取即将由 30/1 指向的目标 SCE，验证
其 kind-3 战斗怪存在且节点序号在客户端 25 行场景表范围内。它不读取或伪造客户端状态。
原有 `vm_net_mock_select_sce_combat_spawn()` 保持不变，仍用于已经可见场景的真实碰撞和 4/5
战斗响应。

## 验证边界

`instance-guide-direct-entry-regression` 模拟“目标 SCE 含战斗怪、客户端当前可见的是另一张
场景”的状态：资源校验必须成功，而 live-node 选择器必须继续拒绝，确保这次放宽不会泄漏到
战斗响应。构建后，胡斐传送的预期日志为：

```text
mock_npc_instance_entry_target scene=测试地图.sce actor=1001
  action=resource-proven-before-scene-enter
mock_npc_instance_enter actor=20092 configured_scene=测试地图.sce scene=测试地图.sce
  pos=(120,120) spawn_enemy=1001 response=30/1
```

客户端随后按既有资源请求和场景加载路径进入测试地图；只有在目标场景加载完成后触碰火团，
才允许现有的 live-node 校验进入 4/5 战斗。该修复不修改数据库配置、CBE 内存、寄存器、
客户端代码或响应字节之外的既有 30/1 正常传送流程。
