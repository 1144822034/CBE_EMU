# 直接退出后重登的旧角色残留

## 现象

同一账号直接退出客户端、立刻重新登录并进入原场景时，新客户端会看到上一
次登录的自己作为一个周围玩家。等待一段时间后该重影才会消失。

## 已确认链路

1. 客户端首次进入场景的既有 `2/10 otherinfo` 槽会被服务端填入周围玩家
   基线；客户端由 `sub_1012958`（`JianghuOL.CBE:0x01012958`）为每一条记录
   创建或刷新场景节点。
2. 服务端以 `vm_net_mock_build_scene_role_seeds` 枚举所有满足
   `vm_mock_service_session_scene_is_visible` 的会话。该条件接受在
   `VM_MOCK_SERVICE_ONLINE_PRESENCE_MAX_AGE_TICKS = 300` 之内仍有心跳记录的
   会话。
3. 实机服务日志捕获到旧会话 `d253c961` 已在桃花岛场景中在线，随后新客户端
   以不同 id `ebd75b24` 成功认证同一账号 `guest00023`：

   ```text
   session_bind client=ebd75b24 account=guest00023
   ...
   session_offline client=d253c961 ... reason=heartbeat-timeout
   ```

   两条记录之间没有 `session_offline ... explicit-disconnect`。因此旧会话在
   300 tick 心跳窗口中仍参与新客户端的 `2/10` 基线，客户端正确地把它显示成
   一个其他角色。
4. 原有 `vm_mock_service_bind_session_account` 只在**相同 client id**已绑定时
   调 `vm_mock_service_session_mark_offline`。直接退出重启会产生新 client id，
   所以该清理分支不会命中旧会话。

## 修复

在认证成功后唯一的账号绑定入口
`vm_mock_service_bind_session_account` 中，绑定新 id 前枚举其它同账号会话并
执行既有 `vm_mock_service_session_mark_offline(..., "account-login-takeover")`。
这不是显示过滤：它使认证成为单账号单在线会话的权威边界，并沿用正常断线的
队伍、交易、决斗和移动队列清理流程。

不会影响不同账号的同场景周围玩家；筛选条件是精确相等的 `accountId` 且
`clientId` 不同。

## 回归

`tmp/account-takeover-nearby-regression.php` 在隔离服务端口上创建一个测试账号，
以 client A 完成首次场景进入，故意不发送断线标志，再以 client B 认证同一
账号并完成场景进入。它验证 B 的 `2/10 otherinfo` 中不含 A 残留的角色节点，
同时服务日志必须记录 `session_account_takeover`。
