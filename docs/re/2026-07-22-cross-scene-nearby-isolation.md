# 跨场景周围玩家隔离（2026-07-22）

## 触发

两个玩家位于 `01桃花岛_01.sce`。其中一人触发该图已取证的特殊场景交互：

```text
WT 4/1 { id=105, index=4, posx=220, posy=440 }
-> WT 30/1 { scene=01桃花岛_02.sce, posinfo=(80,60) }
```

在修复前，服务端立刻把目标场景坐标保存到角色表，但没有修改该客户端会话的
`sceneVisibleReady/sceneVisiblePending/sceneVisibleScene`。因此该会话仍被旧场景的
`vm_net_mock_build_scene_role_seeds()` 视为可见，旧场景的观察端不会收到 `1/2/6`
离开对象，继续把实际已经切图的角色显示为周围玩家。

## 客户端与服务端契约

- `JianghuOL.CBE` 的 `30/1 scene + posinfo` 由
  `parse_scene_response_entry` (`0x010396D6`) 解析，并进入
  `EnterSceneByMapName` (`0x01018150`)；它不是原场景内的坐标更新。
- 场景同步只允许 `vm_mock_service_session_scene_is_visible()` 为真的会话进入
  `vm_net_mock_build_scene_role_seeds(scene, ...)`。
- 已在场景的观察端通过下一次 scene-sync poll 接收 `1/2/6 { actorid }`；目标场景
  仅在源客户端完成自己的场景 resource/task follow-up、被标为 ready 后接收
  `1/2/10 otherinfo` 基线。

## 根因

特殊交互的 `vm_net_mock_build_special_scene_interaction_response()` 是独立于通用
`2/3` / 传送石切图链的 `30/1` 生产者。通用路径会先调用
`vm_mock_service_mark_active_session_scene_pending()`；这个特例遗漏了同一会话生命周期
转换。它不是 `otherinfo` 坐标、移动队列或客户端 node parser 的问题。

## 修复

在 `src/server/mock_server_interaction_login.c` 中，成功构造该 `30/1` 后创建相同的
`vm_net_mock_scene_change_target` 并立即调用：

```text
vm_mock_service_mark_active_session_scene_pending(
    &target, "special-scene-interaction-30-1")
```

随后才保存持久化目标位置。会话因此立刻从源场景 nearby 集合移除，目标地图完成自身
客户端驱动的 follow-up 后才调用既有 `scene_ready` 路径重新加入。

## 回归验证

新增 `tmp/scene-nearby-isolation-regression.php`，在真实 mock-service TCP 协议上验证：

1. A/B 同在 `01桃花岛_01.sce`，C 在 `01桃花岛_02.sce`；B 初始基线有 A，C 没有 A/B。
2. A 发出真实 `WT 4/1` 特殊传送请求并收到 `30/1` 后，B 的下一次 scene-sync poll
   有 `1/2/6 { actorid=A }`。
3. A 仍 pending 时，C 不会收到 A 的 otherinfo。
4. A 完成目标场景 task follow-up 后，C 的下一次 poll 只收到 A 的 `1/2/10` 基线，
   不会收到仍在源场景的 B。

本机结果：

```text
scene nearby isolation regression passed removal=2/6 target-baseline=2/10
```

后续每个新增的 `30/1` / `30/2 result=1` 场景进入生产者都必须同时拥有相应的
`scene_pending` 和完成后的 `scene_ready` 生命周期调用；仅更新角色表中的 scene/pos
不构成附近玩家切图同步。
