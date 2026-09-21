# 大力丸/神力丸：叠加时长与掉线冻结

## 需求

大力丸（829）与神力丸（830）与经验卡同一契约：

1. **同种**可叠加剩余时长（再使用同 item / 同 ATK%）。
2. **异种**（大力丸生效中用神力丸，或反之）拒绝，不扣物品。
3. 续时后剩余不得超过 **8 小时**。
4. 掉线 / 断线期间冻结剩余；重登选角后从冻结剩余继续倒计时。

## 规则

| 情况 | 行为 |
| --- | --- |
| 无激活丸药 | `expires = now + 30min`；829→+30% ATK，830→+60% ATK |
| 已有**同种**再使用 | 在剩余结束时刻上再加 30 分钟；`item_id` / multiplier 保持当前激活丸 |
| 同种续时后剩余将 **> 8 小时** | 拒绝，不扣物品；提示「大力丸/神力丸持续时间已达上限（8小时）…」 |
| 已有**异种**再使用 | 拒绝，不扣物品；提示「同类效果已生效…」 |
| 掉线 | `paused_remaining_sec = expires - now`，`expires_unix=0` |
| 重登选角 | `expires_unix = now + paused`，`paused=0` |
| 在线到期 | `expires_unix <= now` 且未暂停时 DELETE 行 |

## 实现归属

- 叠加 / 异种 / 8h：`vm_net_mock_role_consume_backpack_item_with_timed_effect`（`COMBAT_PILL` 与经验卡同 `sameTypeOnly` + max）
- 掉线：`vm_mock_service_session_mark_offline` → `vm_net_mock_role_combat_pill_pause_on_logout`
- 上线：选角后 `vm_net_mock_role_combat_pill_resume_on_login`（先于 ATK% / 「修」列表读取）
- 读取：`get_active_timed_item_effect` 对暂停中的丸药合成 `expires = now + paused`

## 日志

- `mock_combat_pill_stacked`
- `mock_combat_pill_rejected_different_type`
- `mock_combat_pill_rejected_max_duration`
- `combat-pill_paused` / `combat-pill_resumed`（pausable 通用前缀）

## 验证

- [ ] 使用大力丸后再用一张：剩余约 +30 分钟；点「修」可见续时
- [ ] 大力丸生效中用神力丸：提示同类已生效，数量不变
- [ ] 叠到接近 8 小时后再用同种：提示上限，不扣物品
- [ ] 记下剩余后掉线若干分钟再上线：剩余接近掉线前；战斗仍有 ATK%
- [ ] `make -j2`
