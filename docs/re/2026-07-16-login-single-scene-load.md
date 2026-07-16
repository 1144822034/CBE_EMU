# 2026-07-16 登录后首场景单次加载

## 现象

角色选择成功后，客户端先完成一次场景初始化，随后又出现一次相同场景的加载过程。
旧服务端时间线中，角色选择后的首条 `WT 12/1` 返回 `resp=336`，尾部包含带
`scene + posinfo` 的 `30/1`；同一账号重登时，上一连接留下的 completed target
还可能令后续 `WT 2/3` 进入完整 bootstrap，出现 `resp=409`。

## 客户端证据

- `江湖OL.CBE:scene_runtime_init_and_sync(0x01012FB4)` 已在首场景 ScreenInit
  内创建角色、场景节点和 HUD；直到 `0x010137CA` 才构造 `WT 12/1`。因此这条
  请求是首场景初始化末尾的资源/业务跟进，不是新的场景进入请求。
- `江湖OL.CBE:scene_handle_enter_with_scene_pos(0x010396D6)` 读取 `scene`，并且
  仅在对象含 `posinfo` 时调用场景进入回调、写目标坐标并推进场景生命周期。
  所以在首场景 `WT 12/1` 响应末尾再次发送带 `posinfo` 的 `30/1`，会明确启动
  第二次相同场景加载。

## 修复

- 角色选择成功时，按账号记录 `titleRoleSceneFollowupPending`，并清理上一连接遗留
  的 pending/completed scene target、current-scene reload 和传送石中间态。
- 首场景完整资源跟进分支保留客户端请求的资源、技能、任务、角色和提示对象，
  但把尾部 `30/1 scene+posinfo` 收敛为 `30/2` 无 `posinfo` 确认。该对象会关闭
  请求，但不会命中 `0x010396D6` 的场景进入分支。
- 首场景 task-subset 变体本来就不追加场景进入对象；现在它也会统一消费首场景
  跟进标记并登记 completed target，避免标记泄漏到后续正常切图。
- 两个首场景跟进分支统一输出：

```text
mock_scene_startup_followup_complete ... action=no-second-scene-enter
```

## 验证

- `make -j2`：通过。
- 自动登录回归进入 `蓬莱-铜雀台`，场景探针持续为 `ready=1 assets=1`，地图和
  玩家均正常显示。
- 首场景模块仅出现一次：

```text
screen_run kind=init ... init=05018015 logic=05017137 render=050170cf
```

- 服务端首场景跟进记录为：

```text
mock_scene_target_direct_completed ... reason=scene-startup-followup-complete
mock_scene_startup_followup_complete ... source=scene-task-subset-followup ... action=no-second-scene-enter
```

- 回归过程中未出现第二条同场景 ScreenInit，也未出现登录后的
  `builtin-scene-change` 完整 bootstrap。

