# Pixi 挂机保活说明（2026-08-10 修订）

## 取消：上线自动开挂

不再做：

- 冷启动读 `pix_hangup_resume` 静默登录并 `startHangup`
- 断线 / 回前台 / poll 恢复后 `tryRearmHangup`
- 持久化账号密码用于自动重挂

挂机需玩家点 HUD 手动开始；同一会话内连场仍由客户端碰怪环维持。

## 保留

- Gateway idle scene-poll 代发（presence）
- Mock 原有 `2/10` 挂机（CBE）
- Pixi 手动挂机 → `4/1` 碰怪路径
- APK 在线 FGS（仅维持连接，不自动开挂）
