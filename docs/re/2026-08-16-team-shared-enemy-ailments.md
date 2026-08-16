# 2026-08-16 组队敌方 ailment 未共享（毒/沉默/破甲各座私有）

## 触发

组队战斗中，队长上毒/沉默/破甲后，若本回合不是队长收刀（队友先出手或队友收刀）：

- 毒入膏肓后续跳伤不稳或只在某一座位账号上跳；
- 沉默/破甲像「只有队长先手那份」生效，队友侧像另开一套状态。

## 根因

组队已共享 `battleEnemyHpSlots` / `battleMemberModifiers`，但 `g_mockBattleEnemyAilments` 仍只在**各账号快照**里：

1. 队长施放写入队长账号 globals → `finish` 只把 HP 写回 team；
2. 队友 `prepare` 从 team 拉 HP，ailment 却来自队友账号（空/旧）；
3. 收刀座位 age 的也是本座私有 copy → 表现为「不是队长先手时，队友也会先手」各算各的。

## 修复

- `vm_mock_service_team` 增加 `battleEnemyAilments[3]`；开战清零。
- `prepare_operation` / 超时 autofill / orphan / auto hydrate：从 team 载入 globals。
- `finish_operation`（含 defer 席）与 orphan flush：把 globals 写回 team；整队 round 只 age 一次共享行。

## 验证

1. `make -j2`
2. 双人组队：队长毒入膏肓 → 队友普攻收刀 → 合并 4/6 应有毒跳；下一完整回合继续跳。
3. 队友先出手、队长收刀：同上，毒状态不丢。
4. 神堂静默：任一队员上沉默后，整队回合内怪都不能放技能（共享 silenceRounds）。
