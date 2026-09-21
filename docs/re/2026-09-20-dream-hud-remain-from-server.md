# 梦境倒计时：按经验卡方式从服务器拉剩余

phase: dream-hud-remain-from-server  
status: implemented  
date: 2026-09-20

## 契约（对齐经验卡）

| 环节 | 经验卡 | 梦境 HUD |
| --- | --- | --- |
| 掉线冻结 | `exp_card_pause_on_logout` → `paused_remaining_sec` | `dream_hud_ttl_paused` → `dream_hud_remaining_ms` |
| 上线恢复 | `exp_card_resume_on_login` 再读剩余 | `dream_hud_ttl_resume_on_login` |
| 客户端取剩余 | `1/7/31` → `7/31{expinfo}` | **PixiJS-only** `1/29/23` → `29/23{remaining}` |
| 显示 | 「修」解析文案 | `WorldHud.applyServerDreamRemainMs` |

不把 `29/23` 塞进 group-type1 推送（CBE 不认识该对象）。

## 客户端

- 进 `29*` / 重连后：`session.fetchDreamHudRemainingMs()` → `hud.applyServerDreamRemainMs`
- 本地 LS / 内存冻结仅作乐观显示；权威值以 29/23 为准

## 发布

须同时发 **jh-online-server**（29/23 处理）与 **Pixi 客户端**。Gateway 无需改。

## 验证

1. 进梦记下剩余 → 离开游戏 → 再登 → HUD≈离开前（非 60:00）
2. 服日志：`mock_dream_hud_ttl_resumed` + `mock_dream_hud_remain_query remain_ms=…`
3. 真出本再进 → 满 TTL
4. CBE 客户端不受影响（从不发 29/23）
