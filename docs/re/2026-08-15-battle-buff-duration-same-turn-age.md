# 2026-08-15 战斗 buff/debuff 持续回合被同回合扣光

## 触发

玩家释放限时增益（金刚不坏/神臂担山等）或减益/沉默（破甲/神堂静默等）后，效果往往只撑约 1 回合；随后用神仙壶/逍遥壶会像「被清除」一样立刻失效。

## 根因

`skill.dsh` `时效` 本身是 2–5 回合，解析正确。首次偏离在**挂上效果的同一行动末尾又执行了 round-age**：

| 路径 | 错误行为 |
|------|----------|
| 单人 PvE operate | 敌方 ailment 在 apply 后立刻 `enemy_ailments_advance_round`（沉默 `时效=2` → 同回合变 1，下一回合归零） |
| 单人 PvE operate | 己方 buff 用 `!friendlyModifier` 整块跳过 age，导致放 buff 的回合**不**推进敌方 DoT/沉默；与「本回合新效果不扣、旧效果扣」契约不一致 |
| 切磋 round resolve | `freshModifierMask` 只保护自 buff；沉默/破甲同回合仍 `--silence` / `advance(modifier)` |
| 壶/药品 | 消耗回合时若剩余已被同回合多扣到 1，再 age 一次即归零，表现为「壶清除 buff」 |

组队己方增益已有 `group_modifier_changed_mask` 跳过新挂座位；敌方 ailment 此前没有对应 skip。

## 修复

1. `g_vm_net_mock_battle_enemy_ailments_applied_mask` / `solo_modifier_applied`：本行动挂上的座位跳过 age。
2. 单人 consume turn（operate + 物品/壶）统一 age **既有**效果；不再用「整块跳过友好 buff」短路。
3. 切磋：`freshSilenceMask` + 破甲座位并入 `freshModifierMask`。

## 验证

1. `make -j2`
2. 神堂静默（时效 2）：施放后怪下一完整回合仍不能放技能，再下一回合才解除
3. 破甲/神臂担山（时效 3）：施放后再打满 3 个玩家回合仍生效，第 4 回合失效
4. 挂 buff 后用壶：只扣 1 回合剩余，不应整段清零
