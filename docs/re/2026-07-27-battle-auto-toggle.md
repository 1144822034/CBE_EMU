# 战斗自动开关（`4/11`）

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

旧服务端只安装了 `4/12` 的推测性 handler，实际的 `4/11` 自动开关没有
detector/builder/dispatch 路径。客户端发送 `type=1` 后一直等待该响应，服务端则
落到 `unhandled`，所以自动战斗不会开始。

`4/12` 的业务语义尚未由本次触发或客户端反编译确认；本次不改变它的既有独立
处理，只修复已经由运行时日志和客户端发送/解析分支共同确认的首个偏离 `4/11`。
后续如自动阶段出现新的请求，必须记录其原始包和对应 parser 后再改变 `4/12`。

## 修改与回归

- `src/server/mock_server_battle.c` 增加严格的单对象 `4/11(type=0|1)` detector 和
  回应 builder。响应保留当前战斗 session、回合计数和待敌方行动状态。
- `src/server/mock_server_dispatch.c` 在普通战斗操作之后分派该开关请求，日志来源为
  `builtin-battle-auto11-toggle`。
- `scripts/battle-auto-toggle-regression.php` 用独立账号夹具验证：`4/11(type=1)` →
  后续 `4/2` 仍得到 `4/6` → `4/11(type=0)`。

验证命令（先准备夹具，再启动一个新服务以加载该账号）：

```text
php scripts/battle-auto-toggle-regression.php setup
bin/jh-online-server.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19152
php scripts/battle-auto-toggle-regression.php run 19152
php scripts/battle-auto-toggle-regression.php cleanup
```
