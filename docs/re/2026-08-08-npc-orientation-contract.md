# NPC 朝向配置契约核对

## 触发条件

后台“游戏内容管理 → NPC 编辑”曾允许为动态 NPC 填写“朝向”。实际在客户端
场景内改变该值没有可观察的朝向变化，场景预览也显示了一个并不来自客户端的方向徽标。

## 已确认的契约

SCE 扫描会识别字段 `0x18` 的小整数，并暂存到
`vm_net_mock_scene_npcinfo_seed.orientation`。这是源资源元数据，而不是已确认的网络
NPC 字段。

客户端 `江湖OL.CBE:scene_parse_npcinfo_and_spawn_npcs(0x01037998)` 对 WT `27/11`
每一行严格消费以下八项：

1. `rowId`
2. `x`
3. `y`
4. `displayName`
5. `actorResource`
6. `scriptName`
7. `actorResourceKey`
8. `finalActorId`

`vm_net_mock_build_scene_npcinfo_blob()` 按该顺序构造实际响应，未序列化
`orientation`；在上述 eight-item parser contract 中也没有可供它消费的位置。因此动态
NPC 的朝向表单值不可能到达客户端渲染或动作节点，预览方向徽标同样不是客户端事实。

## 修正

- 删除动态 NPC 新增/编辑表单中的“朝向”输入及请求解析。
- 删除场景预览中的朝向文本和方向徽标，避免把无效数据展示为可视效果。
- 保留 SCE 解析和既有 MySQL `orientation` 列，保证旧场景资源和已部署数据库兼容；它们
  现在明确是未下发的遗留/源元数据，而不是管理设置。

## 验证

- 对构造器与客户端解析文档逐项核对，确认 `orientation` 不在 WT `27/11` wire row。
- 后台源码中不再存在 `orientation` 表单字段、处理变量或预览样式。
- 修改后执行 `make -j2`。
