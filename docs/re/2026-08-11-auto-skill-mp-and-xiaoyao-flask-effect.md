# 自动战斗 MP 校验与逍遥壶特效

## 触发与预期

1. 玩家在战斗中最后一次操作选择一个法术；客户端后续自动战斗定时器发送空的
   `WT 4/12`。
2. 服务端将记住的 `Operate` 重放进普通的 `WT 4/2` 战斗 action builder。
3. 若当前 MP 小于该技能在 `skill.dsh` 中的耗费法力，响应必须编码为普通攻击
   (`Operate=0`)，不得继续下发 type-1 法术动作并把 MP 截断为零。
4. 战斗中使用逍遥壶（item 803）恢复 MP 时，`WT 4/6 actioninfo` 的 effect actor
   应为恢复效果，而非默认的血滴效果。

## 首次偏离与证据

### 自动法术

- `docs/re/2026-08-07-battle-auto-last-operation-replay.md` 已确认 `WT 4/12` 没有新的
  选择，服务端必须在真实请求到达时重放已记住的 `Operate`。
- 修改前 `vm_net_mock_battle_prepare_skill_mp` 在 `MP < cost` 时仍返回成功语义给
  action builder，并计算 `mpAfter=0`。因此 builder 在完成目标、特效和 type-1 法术
  动作选择后才把 MP 写为 0；错误状态早于客户端 parser。
- `skill.dsh` 的技能 201（wire `Operate=203`）具有正 MP 消耗，可作为资源级回归
  样本。`Battle.cbm` 对 `WT 4/2` 的技能操作使用 `skillId + 2` 约定，见
  `docs/re/2026-06-25-battle-server-flow.md`。

### 逍遥壶

- `web/fs/JHOnlineData/item.dsh` 中：802 神仙壶为 HP 恢复 50000，803 逍遥壶为 MP
  恢复 50000，均为 battle-item `consume=2`。
- `web/fs/JHOnlineData/eidolon.dsh` 的 index 13 为 `f_renew1.actor`；其 actor manifest
  引用 `回复.gif`，是客户端现有的恢复效果。
- IDA 实例按 binary name 选择 `mmBattleMstarWqvga.cbm`，反编译
  `HandleBattleActionMsg (0x00006EB0)`：action type 1 和 type 2 都从 action record
  的 effect 字段加载 Eidolon actor；type 2 的背包扣减分支明确把 802 和 803 排除在
  客户端二次扣减之外。因此 803 保持原生 type-2 item action，仅修正其 effect index。
- 修改前 effect selector 只看 `hpEffect`。803 为 MP-only，落到 index 0
  (`f_blood1.actor`)，这是最早的错误选择点。

## 修复

- `src/server/mock_server_battle.c`
  - MP 准备函数仅在当前 MP 大于等于成本时成功；不足时保留当前 MP 输出。
  - 两个 `WT 4/2` action builder（标准与宽容 fallback）均在目标、特效和 actioninfo
    选择前，把“已解析技能且 MP 不足”的操作规范化为 `Operate=0`。
  - 未解析的技能、无活动角色或任何非 MP 不足情况不被此分支吞掉，继续既有路径；避免
    将未知协议问题伪装成普通攻击。
  - 物品 effect selector 同时依据 HP/MP 恢复量，对 HP 或 MP 恢复均选择
    `f_renew1.actor`；803 的 action type 不变。

## 回归验证

`scripts/battle-auto-mp-flask-effect-regression.c` 不启动服务端、不连接 MySQL 或客户端，
而是直接调用上述生产 helper，并以隔离的内存角色和 `web/fs/JHOnlineData` 验证：

1. 技能 201 在 MP=4、小于其成本时，MP prepare 失败且 `Operate=203` 变为 0；
2. MP 恰好等于成本时仍可施法，结果 MP 为 0；
3. HP-only 与 MP-only 恢复均返回同一个非零恢复 effect index。

人工复测应观察服务端日志 `mock_battle_skill_fallback ... reason=mp-insufficient`；紧随其后
的 `mock_battle_operate` 应显示 `operate=0 skill=0`。使用逍遥壶的 4/6 物品 action 则应
带 index 13 的恢复 effect，而不改变 803 的 type-2 背包扣减契约。
