# 2026-08-07：Pixi 梦境右上角倒计时

phase: pixi-dream-hud-countdown
status: fixed

## 症状

进梦境 `29*` 后，服务端已武装 `dreamHudTtl`（踢出 + 1 分钟提醒），但 Pixi 右上角无倒计时。

## 契约

- 实例 timer 是**客户端本地控件**，进本无 WT timer 包（见 `docs/re/2026-08-05-dream-shop-return-false-cold-second-load.md` 续 12）。
- remain 单位 = `ceil(ttlMs/60000)`（默认 60）；绘制为 MM:SS（`60 → 01:00`）；约每墙钟分钟减 1。
+ TTL 默认 3600000ms；Pixi 按墙钟剩余秒绘制 MM:SS（`60:00` → `59:59`…），每秒跳动。
- TTL 配置：`JHOnlineData/dream_hud_ttl_ms.txt`（与 mock 同源）。
- 下线冻结：剩余写入 localStorage / `pauseDreamTimer`；重登同叶子优先用剩余而非满 TTL（服务端权威见 `2026-09-20-dream-hud-ttl-offline-freeze.md`）。

## 修改

`WorldHud`：进 `29*` / `b_29*` 显示 `ui_timer.gif` + `time_number.gif`；出本清除。同图称号刷新不重置；换梦境叶子重武装。断线冻结、重连恢复。PC 完整重登见 `2026-09-20-dream-hud-pc-relogin-countdown-reset.md`。

## 验证

1. 进梦 → 左上角约 `60:00`，每秒变为 `59:59`…
2. 出本 → 倒计时消失
3. 再进 → 重新从满 TTL 开始
4. 断线再上 → 剩余接近断线前，不因离线扣完
5. PC 离开游戏再登 → 同 4（勿从头满 TTL）
