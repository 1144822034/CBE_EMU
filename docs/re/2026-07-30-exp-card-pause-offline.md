# 经验卡：掉线不计时

## 需求

经验卡剩余时长只在角色在线时消耗；掉线 / 断线期间冻结，重登后从冻结剩余继续倒计时。

## 规则

| 情况 | 行为 |
| --- | --- |
| 在线使用 / 叠加 | 仍写墙钟 `expires_unix = now + 剩余`；`paused_remaining_sec=0`；同种叠加剩余上限 8 小时（见 stack 文档） |
| 掉线 | 若 `expires_unix > now`：`paused_remaining_sec = expires - now`，`expires_unix=0`（离线不计时） |
| 重登选角 | 若 `paused_remaining_sec > 0`：`expires_unix = now + paused`，`paused=0` |
| 战斗心得 | 与经验卡相同：掉线冻结（见 battle-insight 文档） |
| 大力丸等 | 仍按墙钟到期，不改 |

## 证据与归属

- 掉线入口：`vm_mock_service_session_mark_offline`（与离线修炼 stamp 同级）
- 上线入口：title 选角后 `vm_net_mock_role_exp_card_resume_on_login`（先于 7/31 / 倍率读取）
- 读取：`get_active_timed_item_effect` 对暂停中的经验卡合成 `expires = now + paused`，供剩余时间文案与倍率使用

## Schema

`account_role_item_effects.paused_remaining_sec INT UNSIGNED NOT NULL DEFAULT 0`  
（`CREATE` 含该列；旧库 `ALTER` 补列）

## 验证

- [ ] 使用经验卡后记下剩余；掉线若干分钟再上线，剩余应接近掉线前（误差约登录耗时）
- [ ] 在线正常倒计时；叠加仍加 60 分钟
- [ ] 战斗心得掉线仍按墙钟过期
- [ ] 日志：`exp_card_paused` / `exp_card_resumed`
