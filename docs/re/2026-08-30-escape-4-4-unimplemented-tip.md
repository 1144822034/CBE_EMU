# 2026-08-30：逃跑后提示「功能暂未实现(1/4/4)」

## 现象

聊天区系统 tip：`功能暂未实现(1/4/4)`；服务端：

```
[error][network] unhandled wt=4/4 len=9 objects=1 first=1/4/4:0
[warn][network] mock_unimplemented_handler_notice tip=功能暂未实现(1/4/4) ...
```

## 根因

1. `1/4/4`（空对象逃跑）已有 `builtin-battle-escape`，**不是**缺实现。
2. Handler 在 `g_mockBattleOperateSessionArmed==0` 且（server-only）无战斗屏时 **return 0**，dispatch 落到 `unimplemented-handler-notice`。
3. 同条件的 `4/2` 早已 `empty-ack`（`mock_battle_operate_ignore`），逃跑路径不对称。
4. Pixi `flee()` 未 `noteOperateSubmitted`，软键可连发；第一发成功清会话后后续包全部 tip。

首次偏离：无武装会话的 `1/4/4` 被当成 unknown，而不是 handled ignore。

## 修改

- 服务端：无武装会话时 `mock_battle_escape_ignore … action=empty-ack`（handled，不发 tip）。
- Pixi：`flee()` → `noteOperateSubmitted("请求逃跑…")`；`onFlee` 在 empty/失败后 `clearAwaitingRound`。

## 验证

1. 战斗中点一次逃跑：成功/失败走原 `4/4`（±`4/6`），**无**「功能暂未实现」。
2. 逃跑成功后连按右软键：服务端应见 `mock_battle_escape_ignore`，聊天无 tip，客户端不卡死在「请求逃跑…」。
3. 切磋仍本地拦截「切磋中无法逃跑」，不发 `4/4`。
