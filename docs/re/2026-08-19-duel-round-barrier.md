# 玩家切磋职业映射与双方行动屏障

Date: 2026-08-19

Status: implemented-pending-regression

## 1. 当前卡点

- 可见现象：玩家切磋开始后，对方职业外观与角色数据不一致；任意一方提交
  `4/2` 后客户端立即播放该方动作，没有等待另一方完成本轮选择。
- 触发方式：同场景两名玩家经 `4/14 -> 4/15 -> 4/16` 同意切磋，双方进入
  `4/10` 战斗后，仅操作其中一端。
- 本轮最小目标：修正 subtype 10 的职业/性别字段顺序，并把 duel 从单方即时
  结算改为双方 `4/2` 都到齐后才释放一个完整的 `4/6` 回合。

## 2. 运行时证据

- `bin/server_out.txt` 的现有复现中，角色 `10001` 提交 `4/2` 后立刻出现：

  ```text
  duel_action ... source=0262aa9c ... delivered=02 resp=110
  net_send ... wt=4/2 ... source=builtin-duel-operate resp=110
  duel_action_deliver ... observer=93819751 ... resp=85
  ```

- 会话日志中的角色来源数据是正常的：`10001` 为 `job=1, sex=1`，规范化后应为
  `jobIndex=0, sexGroup=2`；`10036` 为 `job=3, sex=0`，规范化后应为
  `jobIndex=2, sexGroup=1`。
- 第一次偏离发生在服务端：`vm_net_mock_append_duel_start_object()` 当前按
  `name, jobIndex, sexGroup` 写 subtype 10；`vm_net_mock_build_duel_operate_response()`
  在首个合法 `4/2` 上立即扣 HP/MP、创建 event 并返回 `4/6`。

## 3. IDA 证据

| binary | function/address | findings |
| --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `HandleBattleStartMsg/sub_66CC`, `0x66CC`, subtype 10 branch `0x6898` | 完整对手行在姓名后连续读两个 `u8`。`0x68E4` 的第一个值保存到 `R7`，`0x68EE` 的第二个值保存到 `R0`，随后 `0x68F8 MOV R1,R7; 0x68FA BL sub_23F6`。 |
| `mmBattleMstarWqvga.cbm` | `sub_23F6`, `0x23F6` | 资源索引使用 `2 * R0 + R1 - 1`，故 `R0=jobIndex`、`R1=sexGroup`；线上顺序必须是 `sexGroup, jobIndex`。 |
| `mmBattleMstarWqvga.cbm` | `HandleServerBattleCmd/sub_7BD0`, case 6 at `0x7F64` | 只有可处理的 subtype 6 才在 `0x7F76` 调用 `HandleBattleActionMsg`，随后把 battle phase 设为 5。 |
| `mmBattleMstarWqvga.cbm` | `HandleBattleActionMsg/sub_6EB0`, `0x6EB0` | 读取 `actioninfo` 和 `actionnum` 并建立本地动作槽；任何提前下发的 `4/6` 都会立即开始该端的播放状态。 |

## 4. 调用链与协议契约

```text
客户端 A/B: 1/4/2 { index, Operate }
  -> builtin-duel-operate
  -> duel 会话保存各自本轮 intent
  -> 首位提交：WT(0 objects)，不产生 4/6
  -> 第二位提交：按提交顺序结算两个 intent
  -> 构造一个 1/4/6 { actionnum, actioninfo, optional teaminfo }
  -> 第二位从当前 event-7 请求得到结果
  -> 第一位从既有 pending duel action/event-7 路径得到镜像结果
  -> 双方均由 sub_7BD0 case 6 -> sub_6EB0 开始播放
```

零对象 WT 不是新猜测：组队战斗的已验证 round barrier 使用同一固件契约。
`docs/re/2026-06-25-battle-server-flow.md` 记录了首位提交者收到 5 字节零对象 WT
后只完成请求，不进入 `sub_6EB0`；最后提交者才释放合并 `4/6`，其他成员通过
既有 event-7 路径收到动作。

## 5. 状态归属

- owner：服务端 `vm_mock_service_duel`，不写客户端内存或 CBE/CBM 状态。
- 每轮保存：双方 intent、提交掩码、提交序号和 round serial。
- 每个已释放 event 保存：按提交顺序排列的最多两个动作、最终 HP/MP、双端投递
  mask 和 terminal 状态。
- duplicate `4/2`：仅返回零对象 WT，不覆盖已保存 intent，不重复扣除 HP/MP。
- 自动战斗 `4/12` 仍复用 `vm_net_mock_build_duel_operate_response()`，因此和手动
  `4/2` 进入同一屏障，不另设宿主侧自动动作路径。

## 6. Negative Evidence

- 旧实现的 `turnIndex` 只限制谁能行动；合法方的第一次提交仍立即生成 `4/6`，
  所以它不是双方行动屏障。
- 返回 `4/6 actionnum=0` 也不正确：`sub_7BD0` 仍按 subtype 6 进入动作处理边界。
  等待确认必须是不含任何 `4/6` 对象的合法 WT。
- 不能把首个请求无限挂起：当前独立服务用全局 protocol mutex 串行处理请求，挂起
  会阻止另一客户端提交。应复用已经验证的零对象确认与 pending event-7 投递。

## 7. 实现结果

1. subtype 10 对手完整行已修正为 `name, sexGroup, jobIndex`。
2. duel 会话已加入双方 intent、`roundSubmittedMask`、提交序号和回合序号；旧的
   单一 `turnIndex` 已移除。
3. 首位提交只保存 intent 并返回零对象 WT；第二位提交按提交序号建立同一 round
   event，构包成功后才一次性提交双方 HP/MP。
4. `vm_net_mock_build_duel_action_packet()` 为每个观察端生成同一动作顺序、镜像的
   actor/target wire；本机施法时继续附带该观察端的 `teaminfo` MP 快照。
5. duplicate 保留首次 intent；terminal 只在完整 round event 建立后触发。
6. duel 的原生 `4/11(type=1)` 现在以有效 duel 会话作为 standalone service 的
   战斗存活依据，后续 `4/12` 复用相同 intent barrier，不再被普通怪物战斗的
   `g_mockBattleOperateSessionArmed` 错误拒绝。

## 8. Unknowns

- 双方动作的正式速度排序字段尚未从真实服包确认。本轮沿用仓库已验证的组队回合
  规则，以服务器收到的提交顺序排列，且在日志中明确记录 `order=submit`。
- 同一回合双方都受到致死伤害时，客户端最终胜负文案语义尚未有真实包证据；但用户
  复现已否定自然终局使用 `4/8`。自然终局改用固件的 `4/7` 结果面板字段，双方
  同时归零的文案仍列为 unresolved；主动逃跑仍单独使用 `4/4(result=1)`。

## 9. 验证结果

- [x] `make -j2` 通过。
- [x] subtype 10 字节顺序为 `sexGroup, jobIndex`；隔离角色分别验证
  `job=3,sex=0 -> 1,2` 和 `job=1,sex=1 -> 2,0`。
- [x] 第一位 `4/2` 响应为 5 字节零对象 WT，第二位提交前没有
  `duel_action_round_release`。
- [x] 第二位 `4/2` 才产生含双方动作的单个 `4/6`，反向提交顺序也通过。
- [x] duplicate 不覆盖首次 intent；两端镜像 event 只结算一次。
- [x] 双端 `4/11(type=1)` 和自动 `4/12` 通过相同屏障。
- [x] 第 10 轮致死路径仍先等待双方提交，再释放双方 combat actions；日志中
  `terminal=1` 只出现在 `submitted=03` 后。友好切磋不再追加普通 type-3 死亡动作。
- [x] 未新增客户端内存、寄存器或 CBE/CBM 写入。

隔离自动化命令：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD='123456'
.\scripts\run-duel-round-barrier-automation.ps1
```

旧回合屏障与终局类型拆分的证据目录：
`artifacts/automation/duel-round-barrier-v1-20260819T135411389Z-47320/`。
场景使用独立端口 `19340/19341`、临时
`jh_online_autotest_<guid>` schema 和两个专用测试账号，结束后数据库已删除；
`regression.log` 为通过，`server.stderr.log` 为空。回归直接解析 `actioninfo`，逐条
验证两端 actor/target wire 互为镜像；其中自然 `4/8` 终局仅在服务端回归中通过，
已经被真实客户端的提前退出和空白框复现否定。终局正在改为双端最终 `4/6 + 4/7`，
主动逃跑保持 `4/4`；完整调查见 `docs/re/2026-08-19-duel-terminal-exit.md`。

2026-08-19 的后续修正已经以 `make -j2` 重编译服务端，PHP 与 PowerShell 场景脚本
通过静态语法检查。隔离运行需在显式设置 `CBE_AUTOMATION_MYSQL_PASSWORD` 后执行，
不能以用户正在使用的数据库或服务替代。
