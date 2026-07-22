# 同名场景壳重载的 NPC 目录生命周期

## 已确认的客户端事实

- `1/27/11` 的 `npcnum` / `npcinfo` 由
  `scene_parse_npcinfo_and_spawn_npcs`（`江湖OL.CBE:0x01037998`）消费。
- 该解析器仅在 `npcnum != 0` 且存在 `npcinfo` 时分配 NPC 记录并为每行调用
  `scene_node_find_or_create`，随后将节点类型写为 `21`。空的 `27/11` 不会
  为新场景壳创建任何 NPC。
- `1/30/2` 的 `result=1` / `scene` / `posinfo` 由
  `scene_handle_change_result_scene_pos`（`0x01039770`）处理；带坐标时进入
  场景，缺少 `posinfo` 时只完成下载/加载状态收尾。

## 问题根因

服务端原先将 `27/11` 的“一次性”条件只按账号和场景名保存。旧场景壳已经在
`c00蓬莱仙岛_03.sce` 收到目录后，以下两个重新建立本地场景壳的完成包仍会把
相同场景名判为“已发送”，因而回复空 `27/11`：

1. `current-scene-completion`：非近期完成的同场景 `WT 2/3`，请求 `27/11`、
   `27/4`、`7/42`；
2. `post-enter-completion`：`25/5 + 2/3 + 27/11 + 7/42` 的非重复完成路径。

另一个漏点是 `current-scene-reload`：它先以 `30/1` 建立新壳并正确重新武装
目录，但紧跟的 `WT 6/1` 若账户仍保有过期的 completed-target，未被识别为首次
可安全下发目录的后续请求，只能等轮询兜底，表现为 NPC 迟到或消失。

## 修正后的契约

- `current-scene-completion` 与 `post-enter-completion` 在下发其非空 `27/11`
  前显式重新武装目录；各自的 recently-completed 重复确认分支不变，仍返回空
  `27/11`，不会重复创建当前壳已有节点。
- `current-scene-reload` 仍先通过真实的 `30/1` 场景进入路径建立壳。第一个由其
  专属重载标记关联的 `WT 6/1` 把该壳视作已进入：下发一次 `27/11`，并以无
  `posinfo` 的 `30/2` 收尾，避免第二次 `30/1` 重新进入场景。该标记随后立即
  消费，普通刷新不再收到重复完成包。
- 不把 `27/11` 泛化为所有 `WT 6/1` / `WT 2/3` 的重复广播；NPC 目录只能在
  已知的新场景壳边界各消费一次。

## 回归覆盖

`tmp/scene-npc-same-scene-reload-regression.php` 以相同场景名复现
`current-scene-completion`：修复前响应包含空 `27/11`，修复后首次完成响应包含
非空目录，而紧随的重复确认仍为空目录。
