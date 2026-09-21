# 梦境副本倒计时：下线冻结 / 上线恢复

phase: dream-hud-ttl-offline-freeze  
status: implemented

## 需求

梦境 `29*` 玩家倒计时（服务端踢出 TTL）与经验卡一致：

| 情况 | 行为 |
| --- | --- |
| 在线进入 / 层间传送武装 | 写满 TTL（或既有武装）；checkpoint `dream_hud_remaining_ms` |
| 掉线 / 断线 | 剩余 ms 写入 MySQL；离线不计时 |
| 重登选角 | 若剩余 > 0：保留 `29*` 场景、恢复 session 武装；不强制迁出 |
| 剩余归零且仍在 `29*` | 登录仍走 kind=3 迁出；在线到期仍踢出 |
| 主动出本 / TTL 踢出 | `dream_hud_remaining_ms=0` |

## 归属

- Schema：`account_roles.dream_hud_remaining_ms`（启动 `ALTER` 补列）
- 掉线：`vm_mock_service_session_mark_offline`（与经验卡 pause 同级）
- 上线：`vm_net_mock_dream_hud_ttl_resume_on_login`（title 选角后）
- 登录迁出闸：`vm_net_mock_role_relocate_dream_kind3_login` 在 remaining>0 时 `skip-relocate`
- 在线 checkpoint：TTL poll 约每 15s 回写剩余
- Pixi HUD：进本拉 `1/29/23` 武装（与经验卡 `1/7/31` 同模式，见 `2026-09-20-dream-hud-remain-from-server.md`）
  - 掉线仍 `pauseDreamTimer`；权威剩余以服务端为准
  - 29* enter 已武装或 DB remain>0 时不再覆盖成满 TTL

## 验证

1. 进梦记下剩余 → 断线数分钟 → 重登仍在本、剩余接近断线前
2. 在线正常倒数；到 0 踢出并清 checkpoint
3. 出本后再进从满 TTL 开始
4. 日志：`dream_hud_ttl_paused` / `mock_dream_hud_ttl_resumed` / `role_dream_login_keep_instance`
