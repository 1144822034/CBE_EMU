# 2026-09-06 比武神力丸伤害间歇丢失（账号串读）

## 触发

比武开启 `allow_combat_pill` 且攻击方已吃神力丸/大力丸时，同一对攻防多回合技能伤害波动异常大。玩家样本（均为技能）：暴击 15479、暴击 9011、非暴击 9712——非暴击高于一次暴击，且两暴击比约 1.72×，远超技能 ±8% 浮动。

## 根因

`account_role_item_effects` 按 `(account_id, role_id)` 存丹药。决斗回合由**后提交方**触发 `duel_resolve_committed_round`，此时 `g_vm_mock_service_active_account_id` 是后提交方。

旧路径：`vm_net_mock_role_active_combat_pill_attack_bonus_percent(sourceRole)` → `get_active_timed_item_effect` 用 **active 账号** + 攻击方 `roleId` 查库。

- 攻击方后提交：账号匹配 → +60% 生效  
- 对方后提交：账号错配 → 查不到 → 加成变 0  

同一场里神力丸时有时无，叠暴击后绝对差被放大，表现为「波动太高」。

反推与样本一致：无丸暴击 ≈9011；有丸非暴击 ≈9712；有丸暴击 ≈15479（含 ±8%）。

## 修改

- `get_active_timed_item_effect_for(accountId, …)`：显式按效果所属账号查  
- 比武伤害：`combat_pill_attack_bonus_percent_for(source->accountId, sourceRole)`  
- PvE 仍走 active 账号包装，行为不变  

## 验证

- `make -j2`
- 比武双方都开神力丸：连续多回合技能伤，暴击组内应落在约 ±8%（再 ×1.5），不应再出现非暴击稳定高于暴击  
- 日志：`arena_duel_combat_pill … bonus=60% evidence=owner-account-scoped-effect` 应在有丸攻击方每次命中出现（与谁后提交无关）
