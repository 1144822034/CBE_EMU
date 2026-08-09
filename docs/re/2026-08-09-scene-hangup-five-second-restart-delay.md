# 场景挂机跨战五秒间隔（2026-08-09）

## 触发与首次偏离

用户要求场景挂机自动战斗在上一场完成后等待五秒再进入下一场。此前
`vm_net_mock_scene_hangup_on_scene_default_event()` 已在客户端原生胜利关闭路径
`1/4/7 -> 25/5` 中登记 `sceneHangupRestartNotBeforeTick`，但常量只有一秒。

进一步核对运行日志发现该闸门也不是唯一续战入口：结果面板关闭后，客户端会立即发出
原生挂机启动请求 `1/2/10(Type=2) + 1/25/3`。它被
`vm_net_mock_build_hangup_battle_start_response()` 直接建战，绕过只由场景轮询消费的
`vm_net_mock_build_pending_scene_hangup_battle_response()`。因此，只改轮询分支的延时不能
保证实际间隔。

## 客户端与服务端契约

`25/5` 是 `BattleScene_ExitAndCleanup` 完成结果面板退出后才发送的场景默认事件；它是
允许下一份 `4/5` 到达的第一个安全边界，但客户端协议没有要求服务端在同一回调栈中立刻
建战。现有“无可挂机怪物”路径已经证明，收到同一 `2/10(Type=2)` 请求时，服务端可仅用
`2/10` actor-other 确认让客户端留在场景。

故五秒窗口内的同类自动启动探测返回该既有确认（若原请求附带移动队列，再按已有契约追加
`2/1` ACK），绝不建立新战斗、改写客户端状态或合成界面。窗口到期后，下一次正常场景轮询
仍使用原有 `2/2 + 4/5 + 4/11` 续战合同。

## 修改

- `VM_NET_MOCK_SCENE_HANGUP_RESTART_DELAY_MS` 设为 `5000`；由
  `VM_SCHED_FRAME_MS` 转换为调度 tick，避免硬编码 tick 数；
- 终局 `25/5` 日志记录 `next_tick` 和 `delay_ms=5000`；
- 同一挂机会话在截止前再次收到 `2/10(Type=2)+25/3` 时，只回复已验证的 `2/10(+2/1)`
  确认；场景轮询到点才续战；
- 原生场景碰撞续战资格同样检查 `sceneHangupRestartNotBeforeTick`，避免它把已登记的自动
  间隔当成普通续战。

## 隔离回归

`hangup-auto-restart-delay-v1` 使用已有真实客户端硬件输入驱动
`hangup-auto-rapid-entry-v1`，但独立记录为新的测试场景；服务端、客户端资源、端口
19190/19191 与 `jh_online_autotest_<hex>` 数据库均独立。它要求：

1. 客户端仍完成两次正常 `4/7 -> 25/5`；
2. 两场都是普通奖励结算，不出现旧的 `4/11+4/9` 终局；
3. `scene_hangup_round_complete` 记录 `delay_ms=5000`，随后的真实挂机 `4/5` 建战 tick
   不早于该行的 `next_tick`。

运行：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD='***'
powershell -NoProfile -ExecutionPolicy Bypass \
  -File scripts/run-shop-return-hangup-automation.ps1 \
  -Scenario hangup-auto-restart-delay-v1
```
