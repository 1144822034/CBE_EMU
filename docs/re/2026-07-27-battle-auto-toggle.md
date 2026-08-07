# 战斗自动开关（`4/11`）

> 历史说明（2026-08-07）：本记录中的 `4/11` 开关合同仍有效，但其中“`4/12`
> 未确认”及 scene-poll 自动动作调度已被
> [自动战斗沿用上一操作的协议合同](2026-08-07-battle-auto-last-operation-replay.md)
> 的 IDA 证据取代。自动动作现在必须由客户端原生 `4/12` 请求触发。

## 触发与证据

触发步骤：进入普通场景怪物战斗，在战斗菜单选择“自动”。

2026-07-27 的服务端原始运行日志记录了首个异常请求：

```text
mock_challenge_battle_start ... subtype=5 ...
unhandled wt=4/11 len=19 objects=1 first=1/4/11:10 last_source=- last_resp=0
```

该请求不是此前服务端已经猜测处理的 `4/12`。它是单个 `1/4/11`
对象，10 字节 payload 恰好对应字段 `type` 的 tagged-u8 编码。

## 客户端契约

| 证据 | 行为 |
| --- | --- |
| `mmBattleMstarWqvga.cbm:BattleMenu_SelectOption(0x5F78)` | 菜单项 4（自动）调用网络发送器 `(4, 11, 1)`，然后进入等待阶段。 |
| `mmBattleMstarWqvga.cbm:BattleScene_HandleInput(0x6258)` | 自动状态下确认取消调用网络发送器 `(4, 11, 0)`。 |
| `mmBattleMstarWqvga.cbm:HandleServerBattleCmd(0x7BD0)` case 11 | 仅 `result=1` 被接纳；`type=1` 进入客户端原生自动战斗阶段，`type=0` 返回普通战斗阶段。 |

因此服务端响应必须是同一 subtype 的 `1/4/11`，字段顺序为
`result=1`、`type=<请求值>`。这只是开关确认，不能把它伪造成攻击、结算或
自动关闭，也不能推进服务端的回合状态。

## 根因

第一阶段的 `4/11` 缺失确实已经修复；最新人工复现表明客户端能收到成功确认、
进入自动状态，却没有后续行动。

再次核对发送点与状态机后，首次偏离不是按钮回包，也不是客户端的 1000 ms 提示层：

1. `BattleMenu_SelectOption(0x5F78)` 发出唯一的 `4/11(type=1)` 后，将客户端切到
   自动阶段。
2. `BattleScene_HandleInput(0x6258)` 在该阶段关闭普通操作入口；全量搜索导出的
   `mmBattle` 发送器也只剩取消用的 `4/11(type=0)`，不会再自行发普通攻击 `4/2`。
3. `HandleServerBattleCmd(0x7BD0)` 的 case 6 才会把后续 `4/6(actioninfo)` 交给
   `HandleBattleActionMsg(0x6EB0)` 逐帧执行。
4. 旧服务端只在 `scene_sync_poll` 中投递队伍/切磋的待行动事件；它没有保存单人
   自动开关，也没有向自动中的观察端投递下一份 `4/6`。

因此自动确认成功后，客户端与服务端都在等待对方的下一步。首个错误状态是服务端
“已确认自动、却没有排入下一次回合动作”的缺失调度，而不是客户端动画或按钮状态。

### 2026-07-28 调度实现的首次运行偏离

新调度器首次实测已收到 `4/11(type=1)` 并在到期轮询运行，但日志显示：

```text
mock_battle_auto_toggle ... enabled=1 due_tick=132 ...
battle_auto_action_build_failed observer=437d39d2 mode=solo session=1 turn=0 tick=132
```

该失败发生在生成 `4/6` 之前。检查 `vm_net_mock_is_battle_operate_request` 与同一项目中
的副本挑战轮询意图构造后，确认第一次实现错误地使用了**响应包**布局：对象计数位于
offset 4、对象头为 6 字节；但 `4/2` 请求的解析器要求对象从 offset 4 开始、对象头为
5 字节，且不带对象计数。于是内部意图在普通回合 builder 的入口被拒绝。

修复改为与 `vm_net_mock_build_instance_challenge_battle_response` 相同的请求 WT 布局，并在
调用回合 builder 前用 `vm_net_mock_is_battle_operate_request` 自检。该修改只修正服务端
内部适配器的请求字节，不改变客户端可见的协议：客户端仍只接收独立轮询事件中的 `4/6`。

`4/12` 的业务语义仍未由本次触发或客户端反编译确认；本次不扩展它的推测性语义。

## 修复

- `src/server/mock_server_battle.c` 将 `4/11(type=0|1)` 保存为**按账号隔离**的自动
  状态与下一行动时间；新战斗会重置该状态，挂机战斗中服务端主动下发的 `4/11` 会
  正确启用它。
- 自动行动经过客户端可见的普通场景轮询事件（event 7）投递，初次确认后至少等待
  1000 ms，再返回既有 builder 生成的 `4/6`。没有伪造 `actioninfo`、没有写客户端
  内存，也不把 `4/11` 和首个动作合并在同一个响应中。
- 内部的物理攻击意图复用 dispatch 中同一组权威路径：切磋走切磋回合、队伍走队伍
  回合屏障、其他战斗走普通回合。因此队员自动行动仍要等待其他存活队员行动完成，
  才释放怪物行动。
- `src/server/mock_server_social.c` 把自动动作调度放在既有队伍/切磋待事件之后，避免
  抢在已排队的他人动作、终局包之前投递。

### 复测标准

手动测试时，普通战斗点击“自动”后，服务端日志应按顺序出现：

```text
mock_battle_auto_toggle type=1 ... enabled=1 due_tick=...
mock_battle_auto_action ... response=4/6
```

客户端应在约一秒后开始正常攻击动画；取消时应只有
`mock_battle_auto_toggle type=0 ... enabled=0`，之后不再有自动 `4/6`。队伍战斗中，
每个开启自动的存活队员只会在本轮提交一次，不能越过未行动的队友直接进入怪物回合。

## 验证与剩余风险

- `gcc ... -fsyntax-only src/main.c` 已通过，随后 `make -j2` 已通过。构建过程未启动、
  停止或重启服务端。
- 2026-07-28 的后续人工复现发生在 PID 13952 的旧二进制上：该进程于 2026-07-27
  23:49 启动，`bin/jh-online-server.exe` 的时间戳为 23:48，而本修复的
  `obj/server/main.o` 为 2026-07-28 10:38。该旧进程的 stdout 没有重定向到当前
  文件；可找到的 `local-service-current.out.log` 最后更新时间为 7 月 26 日。因此这些
  复现不能作为本修复的运行时反证。旧进程退出后，已重新链接当前
  `bin/jh-online-server.exe`；下一次由用户手动启动并复现时必须二次取证。
- 原始线上服的精确自动行动间隔尚未得到独立包捕获；当前 1000 ms 间隔由客户端处理
  `4/11(type=1)` 时创建的 1000 ms 自动提示层交叉约束。若人工复测显示节奏异常，下一步
  只调整该有日志的服务端调度常量，并保留 `4/11 → 独立 event-7/4/6` 的协议边界。

验证命令（先准备夹具，再启动一个新服务以加载该账号）：

```text
php scripts/battle-auto-toggle-regression.php setup
bin/jh-online-server.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19152
php scripts/battle-auto-toggle-regression.php run 19152
php scripts/battle-auto-toggle-regression.php cleanup
```
