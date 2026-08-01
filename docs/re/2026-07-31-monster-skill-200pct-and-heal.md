# 怪物技能 +200% 与残血回血（2026-07-31）

## 契约

对已开启「反击放技能」的怪物（`cast_skill` / 默认 BOSS）：

1. **进攻技能伤害**：在普攻反击结果上额外 **+200%**（默认倍率 300%，原 165%）。
2. **残血回血**：当该怪当前 HP **&lt; 60%** 最大生命时，按概率释放回血技能；
   **每场战斗仅一次**：`g_mockBattleMonsterHealUsedSerial ==
   g_mockBattleOperateSessionSerial`（与发奖/结算 serial 同模式；开战 `++`
   会话号后自动失效；进账号 capture/restore，多玩家不串）。
3. 回血优先于进攻技能；成功回血本回合不打玩家，actioninfo 目标为自身、
   valueA 为正治疗量、actionType=1（技能特效）。
4. **沉默中**（玩家封魔/`silenceRounds`）不可回血、不可放进攻技能，只普攻。

## 默认参数

| 变量 | 默认 | 含义 |
|------|------|------|
| `CBE_BATTLE_BOSS_SKILL_DAMAGE_PCT` | **300** | 技能伤害倍率（100=无加成） |
| `CBE_BATTLE_BOSS_HEAL_CHANCE` | 40 | 残血时回血概率（%） |
| `CBE_BATTLE_BOSS_HEAL_SKILL_ID` | 261 | 回血技能（三花聚顶1） |

回血量：`max(技能生命变化, 最大生命/4)`，不超过缺口。

## 修改点

- `vm_net_mock_battle_resolve_enemy_counter_damage` / `apply_enemy_counter_strike`
- 各反击编码路径：回血时 target=自身、valueA=正值
- `g_mockBattleMonsterHealUsedSerial`（`main.c`）+ 账号快照字段
  `mockBattleMonsterHealUsedSerial`

## 验证

1. 打可放技能怪：技能反击伤害约为普攻反击的 3 倍。
2. 压到 &lt;60% HP：偶发回血动画与血条回升；同场不会第二次回血。
3. 日志：`mock_battle_boss_skill ... mult=300` / `mock_battle_boss_heal ...`。
