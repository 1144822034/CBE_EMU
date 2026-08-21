# 2026-08-21 战斗装备掉落概率与数量结算

Date: 2026-08-21

## 触发条件与首次偏离

怪物管理中为装备掉落配置 `5%`，进入包含多个同类怪物的战斗并结算。用户观察到
大多数战斗都会得到装备。

配置概率本身没有被转换为 `50%` 或 `100%`。旧结算位于
`src/server/mock_server_equipment_npc.c` 的 `vm_net_mock_battle_grant_reward_once`，
它对每个掉落行、每一只敌人分别调用 `vm_net_mock_battle_roll_percent`。因此同一
掉落项在一场 3 怪战斗中有 3 次机会；一只怪物配置 4 个 5% 装备时，整场至少命中
一项的概率是 `1 - 0.95^12 = 45.96%`，配置更多装备时还会继续升高。这是第一处
违反用户期望的状态，背包刷新只是后续表现。

## 运行时证据

- `bin/server_out.txt` 中的 `mock_battle_drop_gate` 已记录 `rate=5`，并在同一场
  `enemy=3` 战斗里多个装备槽分别出现 `rolled=1`，证明概率值未被放大但投掷次数被
  放大。
- 线上只读配置显示怪物 `3` 有 4 个 5% 装备，怪物 `15` 有 7 个 5% 装备；旧逻辑
  会按敌人数再次乘上机会次数。
- 旧逻辑未改动客户端 `4/7` 结算解析或 `1/7/7` 背包刷新契约，问题所有权在服务端
  战斗奖励计算层。

## 新结算契约

对每个已配置掉落行：

1. 每场战斗只调用一次百分比投掷。
2. 投掷命中时，最终数量为 `1 x 被击败怪物数量`；未命中时数量为 `0`。
3. 怪物数量只作为命中后的数量乘数，不参与增加投掷次数或改变命中结果。
4. 任务材料仍先经过 `vm_net_mock_task_material_drop_policy`，并在最终数量上按剩余
   任务需求封顶；无有效任务时继续 fail-closed。
5. 本场奖励序列号仍保证重复终结回调不会再次投掷或再次发放。

## 修改点

- `vm_net_mock_battle_drop_count_for_battle(percent, enemyCount)` 集中实现上述契约：
  `enemyCount == 0` 返回 `0`，否则只进行一次百分比判定，命中返回 `enemyCount`。
- `mock_battle_drop_gate` 日志增加 `rolls`、`roll_hit` 和 `quantity_multiplier`，使
  每次结算的投掷次数、命中状态和数量乘数可审计。
- `scripts/battle-task-item-drop-policy-regression.c` 增加同一 RNG 种子下 1 怪与 3
  怪的回归，检查命中/未命中一致、RNG 只前进一次、命中数量分别为 1/3，以及 0%
  始终不掉落；另用 100% 命中断言直接覆盖数量乘法分支。

## 验证结果

- `scripts/run-battle-task-item-drop-policy-regression.ps1`：通过。
- `make -j2`：所有客户端和服务端源文件编译成功；最终不能覆盖
  `bin/jh-online-server.exe`，原因是用户正在运行的服务进程持有该文件。未停止或
  重启该进程。
- 使用同一对象文件集链接隔离目标
  `tmp/jh-online-server-battle-drop.exe`：通过，证明本次服务端改动可链接。
- 未对 `jh_online` 或用户账号执行写入，也未在占用的线上服务上自动化战斗。用户重启
  新构建服务后，应复核 `mock_battle_drop_gate` 每个掉落行均显示 `rolls=1`，命中时
  `rolled=<enemies>` 且 `grant` 仅受任务剩余需求封顶。

## 未知与风险

- 当前回归验证的是服务端纯内存结算和 RNG 消耗；真实客户端战斗的最终背包显示仍需
  在隔离测试账号上人工或仓库自动化场景验收。
- `rate >= 100` 仍按既有确定命中语义处理，不额外消耗 RNG；这与原有百分比函数契约
  一致。
