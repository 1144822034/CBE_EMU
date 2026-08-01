# 自动战斗中仍出现玩家操作菜单

Date: 2026-07-30

Status: implemented (server)

```text
phase: hangup/challenge prefer start (4/11 type=1) -> client 4/12
       -> former ACK 4/11 type=0 -> manual operate panel reappears
trigger: 挂机或跨场自动战斗中操作栏（技能/道具）仍可见
```

## 根因

1. 开战包内联 `4/11 type=1` 负责隐藏手动操作栏（见
   `2026-07-28-hangup-start-countdown-1-flash.md`）。
2. 客户端随后发 `4/12`；原契约对 prefer 回 `type=0` 并 keep prefer
   （`2026-07-25-battle-auto-flag-stall.md`）。
3. `2026-07-28-auto-midcast-flag-replay.md` 在 `HangupStyleFlagOk=1` 时禁止
   再 `flag_poll` 补 `type=1`（避免施法重播）。
4. 结果：`type=0` 把操作栏重新打开，又没有二次 `type=1` → 整场自动都露菜单。

## 修改

`auto12` 且 `prefer=1` 且 `HangupStyleFlagOk=1`：**空 ACK**（不回 `type=0`），
保留自动 UI；仍不回 `type=1`（避免 mid-cast 重播）。

中途按钮首次开自动（`HangupStyleFlagOk=0`）仍走 `type=0` + `flag_poll type=1`。

## 验证

1. 挂机进战：开战含 `+4/11`；后续 `auto12_cancel ... hangup_style=1 response=empty`
   （或 `reply_type=-`），**无** prefer 路径上的 `reply_type=0`。
2. 自动中不应再出现技能/道具操作栏；可点自动关。
3. 手动首场再点自动：仍应有一次 `flag_poll_deliver`。
4. `make -j2 server`。

## 相关

- `2026-07-25-battle-auto-flag-stall.md`
- `2026-07-28-auto-midcast-flag-replay.md`
- `2026-07-28-hangup-start-countdown-1-flash.md`
