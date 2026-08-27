# 组队战斗：先手 AOE 灭敌后后手「打空气」

日期：2026-08-21

## 症状

组队战斗中，先出手席位用群体技能秒光怪物后，合并 `4/6` 仍播放后手队友的一次攻击动画（无有效目标）。

## 根因

回合屏障下各席位的 `actioninfo` 在提交时烘焙并按**队伍名册顺序**拼接。后手席位若在怪仍存活时已 `capture`，或其 capture 排在击杀席之后，合并播放会在击杀动画后再播该 blob → 打空气。

`terminal_wait` 只阻止击杀**之后**新提交的 capture；同回合、击杀席之前提交且名册更靠后的 blob 仍会进合并包。末席 `releaseRound` 击杀时原先未记录 wipe 席位，同样漏跳。

## 修改

1. `battleRoundTerminalActorIndex`：首次确认全灭的席位名册下标。
2. `append_roster_ordered_actions`：跳过 `i > wipe_actor` 的 capture / resolver 前缀；全灭时不再拼怪物反击 suffix。
3. `terminal_capture` 与 `releaseRound` 灭敌路径都写入 wipe_actor。

## 验证

1. `make -j2`
2. 两人组队：队长 AOE 秒怪，队员已提交普攻 → 合并只播队长灭敌，无队员打空气；日志可有 `team_battle_round_skip_post_wipe`。
3. 队员先普攻、队长后 AOE 秒怪 → 仍播队员普攻 + 队长 AOE。
4. 击杀后第三人慢操作 / 超时 autofill → 仍走既有 terminal_wait / timeout，不回退 terminal-wipe。

## 相关

- `docs/re/2026-08-05-team-battle-terminal-wipe-premature-playback.md`
- `docs/re/2026-08-05-team-battle-peer-action-starve.md`
