# 组队自动战斗：保留 prefer，走回合栅栏

日期：2026-08-01

## 症状

组队遇怪后无法使用/保持自动；开战日志 `mock_battle_suspend_solo_auto`。

## 根因

防泄漏补丁 `suspend_solo_auto_for_team` 在队长开战、队友 deliver 时清掉
整份 prefer。后续虽有 synchronized synth 能力，prefer 已被清，战中只能再点，
跨场更无法保持。

## 修改

1. `team_start_prepare_auto`：只清挂机环；prefer=1 时 `pull_team_vitals` +
   `arm_pending_at_start`。
2. 队长 challenge / 队友 deliver：prefer 时附带 `4/11 type=1`。
3. Synth 仍仅经 `synchronized_team_battle`（`round_defer`），不走单人 operate。

## 验证

1. 双人组队，任一方按 1 → `team=1` synth / defer，齐手后 `4/6`。
2. 打完再遇怪仍自动（`team_auto_keep`），按 1 可关。
3. 挂机环进组队被清；地图挂机不与组队 4/1 踩踏。
