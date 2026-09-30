# 挂机场次：无战斗心得 64 / 有心得不限次（可后台关闭限额）

日期：2026-09-29

## 契约

| 战斗心得 (828) | 后台 `hangupQuotaWithoutInsight` | 挂机上限 |
| --- | --- | --- |
| 未生效 | **1**（默认） | 最多 **64** 场，满则停止挂机 |
| 未生效 | **0** | **不限次** |
| 生效中 | 任意 | **不限次**（combatinfo.maximum = `0xffff`） |

心得过期后，若限额开启且本轮已完成 ≥64 场，下一场结算离场即停挂；未满 64 则继续打到 64。后台关闭限额后，无心得也可无限挂机。

## 管理后台

- Tab「客户端UI」→「无战斗心得挂机限额」：限制64场 / 不限制
- 表 `server_client_ui_config.hangup_quota_without_insight`（默认 1）
- 人物面板 `1/1/21` 字段 `hangupQuotaWithoutInsight`
- 迁移：`server/mysql/migrate_add_client_ui_hangup_quota.sql`（运行时 `ensure_column` 也会补列）

## 修改点

- `src/server/mock_server_client_ui_config.inc.c` / `web_admin_client_ui.inc.c` / `mock_server_role.c`
- `PixiJS_web/src/net/protocolExtras.ts`：`hangupBattlesMaximum` / `hangupBattlesQuotaExhausted` 增加配额开关参数
- `BattleScreen` / `hangupSettleUi` / `main.ts`：按面板字段同步 maximum 与停挂

## 验证

1. 后台默认「限制64场」：无心得开挂机打满 64 场后「挂机场次已用完」。
2. 后台改「不限制」→ 重拉人物面板 → 无心得可超过 64 场；结算「挂机场次提升至 不限次」，经验加成仍为 +0%。
3. 使用战斗心得后挂机：始终可超过 64；结算「不限次」且经验 +20%。
4. 挂机中点「补充时间」生效后，可继续超过 64。
