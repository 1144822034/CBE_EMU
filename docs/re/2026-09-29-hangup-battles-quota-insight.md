# 挂机场次：无战斗心得 64 / 有心得不限次（可后台关闭限额）

日期：2026-09-29

## 契约（2026-10-05 起）

挂机场次**始终不限次**（心得 / 非心得相同）。`combatinfo.maximum = 0xffff`；
Pixi `hangupBattlesQuotaExhausted` 恒为 false。后台
`hangupQuotaWithoutInsight` 默认 **0**（保留开关仅作兼容，客户端不再按 64 停挂）。

### 历史契约（已废弃）

| 战斗心得 (828) | 后台 `hangupQuotaWithoutInsight` | 挂机上限 |
| --- | --- | --- |
| 未生效 | **1** | 最多 **64** 场 |
| 未生效 | **0** | **不限次** |
| 生效中 | 任意 | **不限次** |

## 管理后台

- Tab「客户端UI」→「无战斗心得挂机限额」：保留选项（默认**不限制**）
- 表 `server_client_ui_config.hangup_quota_without_insight`（默认 **0**；启动时清掉旧的 1）
- 人物面板 `1/1/21` 字段 `hangupQuotaWithoutInsight`（客户端已忽略停挂）

## 修改点

- CBE `network-client.c`：`combatinfo.maximum` 恒 `0xffff`
- Pixi `hangupBattlesMaximum` / `hangupBattlesQuotaExhausted`：恒不限次 / 不停挂
- 后台默认与 DB 行清为不限制

## 验证

1. 无心得挂机：超过 64 场仍继续，无「挂机场次已用完」。
2. 有心得挂机：同样不限次；经验加成仍随心得生效。
3. CBE 日志 `cbe_hangup_combatinfo tally=N/65535`（或面板显示不限次语义）。
