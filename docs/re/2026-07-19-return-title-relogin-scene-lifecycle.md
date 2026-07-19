# 返回标题后重新登录的场景生命周期

## 现象

角色已在场景中时选择“返回标题”，不退出模拟器，重新登录同一角色进入场景，
客户端在收到 `resp=36` 后于 `0x01046C48` 访问地址 `0x40`：

```text
r1:40
lr:1014e77 pc:1046c48
```

## 客户端证据

- 按 `binary_name=江湖OL.CBE` 选择 IDA 实例。
- `0x01046C48` 位于 `SetMapCtrlViewport/sub_1046C2C`。
- 该函数先从参数保存地图控制对象，再读取 `a2 + 64`；崩溃时 `a2 == 0`，
  因而实际访问 `0x40`。这说明场景/地图控制对象尚未建立。
- `resp=36` 是 `7/7 {type=3}` 对应的常规 `7/32 {result,expcard}`，只是最后一次
  触发客户端继续初始化的响应，不是损坏的地图指针来源。

## 服务端负面证据

旧实现只在同一 service clientId 改绑到不同账号时调用
`vm_mock_service_session_mark_offline`。从标题重新登录同一账号不会发送显式断线，
所以旧角色的 `sceneVisibleReady` 仍为真。

失败序列中，第二次角色选择完成后、`7/7 type=2/3` 和场景资源请求之前，周期轮询
已经下发旧场景的 `27/11` NPC/玩家数据。客户端此时尚无地图控制对象，最终进入
`SetMapCtrlViewport` 时解引用空参数。

## 协议约束与修正

经过认证的登录请求是新的标题/角色/场景生命周期边界，即使账号和 service
clientId 都没有变化，也必须先结束旧角色在线状态。登录重绑现在会：

- 清空旧场景 ready/pending、场景名、坐标、移动与 peer 可见状态；
- 清空旧生命周期的社交/聊天待投递和邀请回复状态；
- 清空在线角色身份，并按下线语义处理旧队伍/交易关系；
- 保持新场景不可见，直到新的场景资源完成路径重新标记 `scene_ready`。

因此第二次选角后、地图初始化前的 scene-sync poll 必须返回空；NPC 和周围玩家
只能由新的场景资源阶段开始下发。

## 回归验证

`tmp/scene-npc-lifecycle-regression.php` 使用同一 clientId 执行：

1. 登录、选角、完成首次场景与商城返回；
2. 不发送显式 disconnect，再次认证并选择同一角色；
3. 在场景资源请求前发起 service poll，断言响应长度为 `0`；
4. 发送 `7/7 type=2`、`7/7 type=3`，再完成场景资源请求；
5. 断言新场景按正常路径收到非空 `27/11` NPC 目录。

验证结果：

```text
relogin_preinit_poll_len=0
relogin_startup_len=396
```

最终重建后的运行日志：`tmp/mock-service-19090.20260719-110804.stdout.log`，stderr 长度为 0。
