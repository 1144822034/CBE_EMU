# 普通死亡惩罚：保级扣经验与 1% 金钱

## 状态

已实现并由独立服务回归验证。

## 触发条件与既有协议

普通死亡复活只发生在角色 HP 已持久化为 `0` 后，客户端发送严格单对象
`WT 1/7/14(result=2)`。现有的 `mmBattleMstarWqvga.cbm` 复活分支以及
`docs/re/2026-07-23-battle-death-revival-stone.md` 已验证该请求的响应契约是
`1/20/1 + 1/30/1`：客户端正常清理等待态并重入服务端决定的最近传送石场景。

本次不改变请求、响应、场景落点、HP/MP 恢复或复活石 `result=1` 分支；只调整
该普通复活链路中的权威角色数值结算。

## 首个偏离与根因

`vm_net_mock_role_apply_death_penalty()` 仍执行此前的旧规则：将累计经验直接设为
`max(当前等级-2, 1)` 的等级起始值，并扣 5% 金钱。这与新的规则“金钱丢失 1%，
经验丢失当前升级所需的 60%，但不掉级”不一致。偏离发生在持久化前的服务器
角色状态计算，不在客户端 parser 或场景响应层。

角色经验为累计值。当前等级所需升级经验的可检验定义为：

```text
level_required = next_level_start_exp - current_level_start_exp
exp_loss       = ceil(level_required * 60 / 100)
actual_loss    = min(exp_loss, current_exp - current_level_start_exp)
new_exp        = current_exp - actual_loss
```

因此 `new_exp` 永远不低于当前等级的起始经验，重新由累计经验推导等级后仍为原
等级。金钱以相同既有的向上取整百分比函数计算 `ceil(money * 1 / 100)`；金钱大于
零时至少丢失 1，且不会超过现有金额。

例如 5 级经验区间为 `[1000, 1500)`，当前经验 `1300`：升级所需为 `500`，丢失
`300` 后为 `1000`，仍是 5 级；金钱 `654` 丢失 `7` 后为 `647`。

## 修改点

- `src/server/mock_server_core.c`：普通死亡金钱比例从 5 改为 1，并声明经验比例 60。
- `src/server/mock_server_equipment_npc.c`：按当前等级经验区间计算经验损失，并以本级
  起始经验作下限；日志增加 `level_exp_required` 以审计计算结果。
- `scripts/ordinary-death-penalty-regression.php`：建立 5 级/1300 经验/654 金钱的死亡
  夹具，验证结果为 5 级/1000 经验/647 金钱，且重复请求不会重复扣除。

复活石 `WT 1/7/14(result=1)` 仍按物品说明免除普通死亡经验和金钱惩罚。
