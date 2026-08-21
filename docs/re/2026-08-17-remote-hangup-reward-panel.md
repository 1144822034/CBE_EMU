# 远程服务端挂机奖励面板停滞

## 现象

本地启动脚本下挂机可以在奖励结算后进入下一场；将服务端地址改为远程 IP 后，
客户端能收到并显示 `4/7` 奖励面板，但不会产生下一场战斗。

## 首个偏离

奖励面板不是服务端可以直接用下一场 `4/5` 覆盖的状态。此前场景挂机启动包只有
`4/5 + 4/11(type=1)`，因此固件会自动出招，却没有收到自动结算退出的协议对象；最终
停在 `4/7` 结算状态。手动链路为：

```text
4/7 -> 客户端结算相位 7 -> 客户端确认输入 -> BattleScene_ExitAndCleanup
     -> 原生空 25/5 -> scene poll -> 下一场 4/5
```

`4/7` 没有“自动关闭”字段；在 `25/5` 到达前下发新的 `4/5` 会重入仍存活的
BattleScreen，不能作为远程延迟的兜底。

此前客户端提供过一个只读确认渲染状态、再经模拟器硬件输入队列发送一次按下/释放的
临时辅助。该辅助不属于固件协议，不能作为挂机跨战的产品实现。

## 旧辅助处理

`vm_hangup_auto_confirm_init()` 现在默认关闭。只有显式设置
`CBE_HANGUP_AUTO_CONFIRM=1` 或 `--hangup-auto-confirm` 时，才允许测试环境使用一次
硬件输入辅助；`0`、`off`、`false` 和未设置均不会下发输入。

辅助仍必须满足既有条件才生效：已识别的场景挂机 `4/5 + 4/11(type=1)`、收到本场
`4/7`、渲染后只读确认 phase=7/result=1、下一 scheduler tick 发送一次按下并在
80 ms 后释放。服务端不修改响应字节、不伪造 `25/5`、不提前构造下一场 `4/5`。

它仍可用于回归旧路径，但不再用于证明原生挂机续战。

## 历史辅助验证

- `make -j2` 通过并重新链接 `bin/main.exe`。
- 显式开启时日志应出现：

  ```text
  [info][hangup] reward_auto_confirm enabled mode=hardware-input-after-rendered-4/7 source=environment
  ```

- 远程挂机终局应依次出现 `reward_auto_confirm_rendered`、
  `reward_auto_confirm_input press/release`、客户端上行 `25/5`，随后服务端记录
  `scene_hangup_round_complete`，再由 scene poll 建立下一场。

## 固件原生挂机复核（2026-08-17）

用户提出固件本身存在挂机自动处理，因此重新区分了两个不能混为一谈的阶段：

| 阶段 | 固件入口 | 可由固件自动完成的行为 | 当前证据 |
| --- | --- | --- | --- |
| 战斗内回合 | `BattleAutoAction_TimerTick(0x2952)` | 自动状态下在 phase `0/4/8` 发送空 `WT 4/12`，服务端以 `4/6` 回放已保存的操作 | phase `7` 奖励面板的关闭 |
| 手动结算退出 | `BattleScene_HandleInput(0x62A2)` / `HandleBattleCharTouch(0xB62)` | 在 phase `7`、结果已建立且收到输入时调用 `0x60C8` | 已运行过的人工确认路径 |
| 自动结算退出 | `Handle25_2(0x8996)` -> `DrawBattleMain(0x5444)` | `25/2 {result=1,type=1}` 写 `battle+0x470=1`；最终动作完成后累计 10 帧，由固件发送 `25/5` | 已加入场景挂机启动包；待隔离自动化取得运行时证据 |

`HandleServerBattleCmd(0x7BD0)` 的 `4/11 { result=1, type=1 }` 仅写入
Battle object `+0x474` 的自动标志并设 phase `8`；`type=0` 清掉标志。`4/9` 也只会在
自动/死亡条件成立时设 phase `8`。两者均没有到
`BattleScene_ExitAndCleanup(0x60C8)` 的调用边，但这不能推出 `0x60C8` 是唯一的
`WT 25/5` 发送点。

原始 `mmBattleMstarWqvga.cbm` 中还有一条独立的固件出口：

1. `Handle25_2(0x8996)` 只在收到 `25/2` 且 `result=1` 时读取 `type`；`type=1` 写
   `battle+0x470=1`，并清理当前动作/画面状态。
2. 最终动作回调 `0x30D4` 写 `battle+0x472=1`。若 `battle+0x470=1`，还会重置
   `r9+0x3450+9` 的帧计数并选择自动终局状态。
3. `DrawBattleMain(0x5444)` 在 `battle+0x470==1 && battle+0x472==1` 时连续渲染 10 帧，
   随后自己构造空 `WT 25/5`、清空 phase 并释放 BattleScreen。这个分支不调用 `0x60C8`。

因此已证实的手动链路为：

```text
4/11(type=1) -> 战斗内 4/12/4/6 自动回合
最后 4/6 -> 4/7 -> 动作完成(0x30D4)
-> 客户端输入 -> 0x60C8 -> 原生 25/5 -> scene poll -> 下一场 4/5 + 4/11
```

`25/2(type=1)` 的解析效果来自原始模块 `mmBattleMstarWqvga.cbm`（SHA256
`2d08c215...`）的完整 parser 分支，而非写客户状态：`0x8996` 精确读取
`result` 与 `type`，`0x5E92` 是独立于手动 `0x60C8` 的 `25/5` 发送点。它不能出现在
启动包：该时序会让战斗在目标列表尚未创建时进入自动出口路径。现在由终局状态构造器仅在
最后 `4/6` 与胜利 `4/7` 已排队后追加：

```text
1/25/2 {
  result: u8 = 1
  type:   u8 = 1
}
```

普通碰怪、决斗、复活和已在进行中的延迟碰撞不会获得该对象。服务端仍只在客户端真实
上行 `25/5` 后设置 `sceneHangupRestartPending`，不会预先下发下一场 `4/5`。

已知的负证据仍成立：在 `25/5` 前提前下发新的 `4/5` 会重入旧 BattleScreen 并卡死，
所以无论采用何种原生出口，下一场仍只能在客户端实际退出后由 scene poll 创建。

## 已确认的响应标志（2026-08-17 复核）

此前提到的“决定自动进入下一场”的标志是每个**新战斗启动响应**中的：

```text
1/4/5  { side, battleinfo }
1/4/11 { result=1, type=1 }
```

`4/11.type=1` 是 `HandleServerBattleCmd(0x7BD0)` 所消费的自动战斗标志。它使新
BattleScene 的 `BattleAutoAction_TimerTick(0x2952)` 发送 `4/12`，从而自动开始该场
的第一回合。场景挂机启动构造器固定在 `4/5` 后追加该对象，不再通过环境变量决定是否
省略它。

它与跨战启动的授权是两个不同层次：服务端仅在客户端发出结算退出 `25/5` 后才将
`sceneHangupRestartPending` 设为真，后续 scene poll 才发送上述 `4/5 + 4/11(type=1)`。
因此该标志确实决定“下一场是否自动出招”，但不会也不能把仍在显示的上一场 `4/7`
结算面板变成已退出状态。

## 本次调整与验证边界（2026-08-17）

已移除 `CBE_HANGUP_BATTLE_AUTO_FLAG`。两个场景挂机启动构造器均固定构造
`4/5 + 4/11 { result=1,type=1 }`，并固定 arm 服务端对客户端原生 `4/12` 的回放状态。
这消除了远程服务进程因环境变量被设置为 `0` 而让新场次无法自动出招的可能。

`make -j2` 已通过，且源码、脚本和文档中不存在该变量的残留引用。新的隔离场景
`hangup-native-auto-exit-v1` 显式设置 `CBE_HANGUP_AUTO_CONFIRM=0`，并要求以下全部证据：

1. 终局响应在最后 `4/6` 与 `4/7` 后实际收到 `25/2 {result=1,type=1}`，且执行 `mmBattle:0x8996`。
2. 最终结算后执行 `mmBattle:0x5E92`，由客户端实际发送 `25/5`；不得执行手动
   入口 `0x60C8`，也不得出现宿主 `reward_auto_confirm_input`。
3. 服务端记录 `scene_hangup_round_complete`，随后 scene poll 发送新的
   `4/5 + 4/11`；下一场终局才会再次发送 `25/2`，客户端再次通过 `0x66CC` 并收到下一场 `4/6`。

运行器使用唯一的 `jh_online_autotest_<uuid>` 数据库，仍要求
`CBE_AUTOMATION_MYSQL_PASSWORD`。缺少该凭据时它会按隔离规则拒绝启动，不能将构建成功
误作端到端通过证据。
