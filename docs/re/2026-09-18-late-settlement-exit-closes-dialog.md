# Late settlement_exit 4/8 closes NPC dialog after settle

## Symptom

Win a fight → settlement panel auto-closes (~2.5s) → open NPC dialog → dialog
closes by itself a few seconds later.

## Root cause

Client settle auto-close correctly calls `endBattleToWorld` once
(`BattleScreen.armSettleAutoClose` → `onSettleConfirm`). Server still arms
`settlement_exit` (`4/8`) for playback + ~2500ms panel window
(`mock_server_battle.c` `vm_net_mock_battle_settlement_exit_arm`).

When that lone finished packet arrives with `phase !== "battle"`,
`main.ts` `onBattleUpdate` always called `endBattleToWorld` again.
`showPhase("world")` runs `dialog.hide()` for any non-dialog phase.

First divergence: second teardown while UI is already on dialog/world, not the
settle timer (timer is cleared in `confirmSettlement`).

## Fix

Only force `endBattleToWorld` from the lone `4/8` / escape path when still in
battle (`session.inBattle`, or `phase === "battle"` for escape). Ignore late
exit packets after leave.

## Verify

1. Win fight, wait for settle auto-close, open NPC talk immediately.
2. Dialog must stay open past the former ~2–4s window.
3. Mid-fight system/menu overlay + late exit still returns to world when
   `session.inBattle` is true.
