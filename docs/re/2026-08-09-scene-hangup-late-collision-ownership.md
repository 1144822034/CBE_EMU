# 挂机场景延迟碰怪请求的战斗会话归属（2026-08-09）

## 触发与首个偏离

长时间自动挂机后，偶发右侧玩家角色消失、战斗不能推进。`bin/server_out.txt` 的首次偏离为：

1. 场景轮询已经下发下一场挂机的 `2/2 + 4/5 + 4/11(type=1)`，日志为
   `mock_hangup_battle_start ... battle=30 ... auto=1`；该响应已经创建并武装了自动战斗会话。
2. 随后到达一个延迟的场景碰怪 `1/4/1`，其位置为另一处活动场景节点。旧的通用挑战处理器将
   它当成一场新战斗，记录 `mock_challenge_battle_start ... hangup_continue=0 auto=0`，覆盖了刚才的
   挂机会话、敌人槽位和自动状态。
3. 客户端已经由第一份 `4/11(type=1)` 排队了空 `1/4/12` 自动回放；它到达时服务器已被第二场
   非自动战斗取代，得到 `unhandled wt=4/12 len=9`。这是右侧槽位/动作状态不再属于同一战斗，
   而非渲染器自行丢失角色。

## 客户端契约

按 `binary_name=mmBattleMstarWqvga.cbm` 选择 IDA 实例：

- `HandleServerBattleCmd(0x7BD0)` 的 case 5/10 调用 `HandleBattleStartMsg`，因此每一份 `4/5`
  都会重建战斗开始状态，不能为已开始的场景挂机再次发送。
- 同一函数的 case 11 无条件解析 `result/type`；`result=1,type=1` 仅恢复自动标志和自动阶段，
  不重建角色槽位。

所以，既有自动挂机战斗期间到达的延迟 `4/1` 不能再对应新 `4/5`；有效的契约是重申现有
`4/11(type=1)`，让先前已排队的 `4/12` 继续作用于同一个服务端会话。

## 修复

`vm_net_mock_scene_hangup_live_auto_battle` 精确要求：同一在线角色、严格相同场景、场景可见、
非重启/非结算、独立队伍、已武装且未结束的自动场景怪物会话。满足时，
`vm_net_mock_build_challenge_interaction_response_ex` 对延迟 `4/1` 只下发 `4/11(type=1)`，不写入
敌人 ID、不递增会话序号、不重置 HP/MP/槽位，也不取消自动。

运行时可由以下日志确认：

```text
mock_scene_hangup_duplicate_challenge_reaffirm ... request=4/1 response=4/11(type=1) action=retain-live-auto-battle
```

这不是通用的“战斗中丢弃请求”兜底；只有已被正常 `4/5+4/11` 建立的同一挂机会话才适用。

## 隔离回归

新增 `scripts/run-scene-hangup-late-collision-automation.ps1`。它创建独立的
`jh_online_autotest_<随机值>` 数据库、独立服务端口和私有资源副本；不读取或修改正在
运行的服务、用户账号或 `jh_online` 中的数据。场景 ID 为
`scene-hangup-late-collision-v1`，最多六步、总超时 30 秒、单步超时 5 秒：

1. 以测试角色走真实登录、角色选择和 39 字节场景资源 follow-up，使服务端通过正常
   scene-ready 生命周期建立可见场景。
2. 发送客户端实际的 `2/10(Type=2)+25/3` 挂机开始请求，确认返回 `4/5+4/11(type=1)`。
3. 重放运行时捕获的延迟 `4/1 {id=105,index=9,pos=(292,484)}`，断言响应**仅**为
   `4/11(result=1,type=1)`，没有第二个 `4/5`。
4. 发送原生空 `4/12`，断言服务端返回 `4/6`，并且服务端日志没有 `unhandled wt=4/12`。

运行命令：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD='…'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-scene-hangup-late-collision-automation.ps1
```

2026-08-09 的一次通过产物位于
`artifacts/automation/scene-hangup-late-collision-v1-20260809T115459253Z-42480/`；其中
`server.stdout.log` 依次记录了 `mock_hangup_battle_start`、
`mock_scene_hangup_duplicate_challenge_reaffirm` 和 `mock_battle_auto_replay`。
