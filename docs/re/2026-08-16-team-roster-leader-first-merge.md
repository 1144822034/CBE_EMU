# 2026-08-16 组队合并回合固定队长先手

## 触发

组队 PvE 合并 `4/6` 时，播报顺序跟谁先点 `4/2`（submit serial），队友先点则动画上队友先手，队长不再固定开场。

## 根因

`merge_team_battle_round_response` / terminal / orphan 按 `battleRoundActionSerial` 升序拼接；收刀席整包（玩家动作+怪反击+毒跳）总是贴在末尾。队长收刀时怪反击还会插在队员动作之前（若按席位硬插整包）。

## 修复

1. 收刀组包在怪反击/DoT 前记下 `resolve_player_action_info_len/count`。
2. `append_roster_ordered_actions`：按冻结席位 `0..n-1`（队长=0）拼各座**玩家**动作；收刀席只取前缀；后缀（反击+毒跳+相关死亡）挂在全队玩家动作之后。
3. terminal / orphan 仅 pending 时同样按席位序。

## 验证

1. `make -j2`
2. 双人：队友先点、队长收刀 → 合并动画仍是队长动作 → 队友 → 怪反击。
3. 队长先点、队友收刀 → 同上队长仍先。
