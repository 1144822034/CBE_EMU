# 商城返回场景完成包与位置保持

## 现象

- 从商城返回 `mmGame` 后，客户端重新请求当前场景资源，但加载提示不会结束。
- 少数情况下人物坐标被重新套用为场景出生点。

最新运行日志中的实际返回请求是 `WT 6/1 len=39`，对象集合为：

- `6/1`
- `6/13`
- `6/14`
- `2/10 Type=101`
- `25/5`

服务端此前把它识别为“已显示场景的普通重复刷新”，只回放资源、任务和
NPC 对象。日志停在 `builtin-scene-resource-followup`，没有结束客户端场景下载
状态的对象。

## 客户端证据

`江湖OL.CBE`：

- `0x01039770` 是 `30/2` 场景结果解析器。
- `result=1` 时解析 `scene`，只有存在 `posinfo` 才读取 `x/y` 并调用场景位置
  入口。
- 无论是否存在 `posinfo`，解析器都会在 `0x0103993C` 调用
  `ResetDownloadState` 并清理下载状态。

因此，`30/2 result=1 + type=2 + scene` 且不携带 `posinfo`，正好满足商城
返回需求：结束加载状态，同时不改写角色当前坐标。

## 服务端修复

商城打开时已有 `shopSceneNpcReseedPending` 和对应场景名。现在
`vm_net_mock_build_scene_resource_followup_response` 在处理最近完成的同场景
`6/1 len=39` 请求时：

1. 在消费 NPC 重播标记前，确认该请求是否属于同一场景的商城返回。
2. 先下发场景资源、任务和一次性 NPC 列表。
3. 仅对匹配的商城返回，最后追加一个不含 `posinfo` 的 `30/2`。
4. 普通同场景刷新仍不追加 `30/2`，避免重复结束或重新进入场景。

新增日志字段：

```text
mock_scene_resource_followup_repeat_ack ... shop_return=1 completion=30/2-no-posinfo
```

## 回归

`tmp/scene-npc-lifecycle-regression.php` 使用真实的 `WT 6/1 len=39` 顺序覆盖：

- 首次进入场景；
- 打开商城并加载各商城分页；
- 商城返回，必须重播 NPC 并收到不含 `posinfo` 的 `30/2`；
- 随后的稳定刷新不得再次收到 NPC 列表或 `30/2`；
- 返回标题后重新登录。

本地结果：

```text
scene npc lifecycle regression passed startup_len=795 shop_return_len=620 steady_refresh_len=221 relogin_preinit_poll_len=0 relogin_startup_len=795
```

