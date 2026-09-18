# World chat duplicate on single Enter

## Symptom

Send one world-channel message (IME Enter / mobile「发送」) → chat list shows
two identical lines; two small horns can be consumed.

## Root cause

One Enter keydown ran two submit paths before the draft cleared:

1. `ChatIme` input `keydown` Enter → `onSubmit` → `ChatPanel.confirm` →
   `GameSession.sendChat(type=0)` → server `1/3/3` echo → `hud.pushChat`
2. Same event bubbles to `window` `keydown` (`main.ts`) → `handleOk` →
   `chatPanel.confirm` again → second exchange / second echo

World chat has no client local echo (unlike team/guild/private). Server
fanout excludes the sender; each duplicate line is a real second send.

`preventDefault()` alone does not stop bubbling.

## Fix

- `ChatIme`: `stopPropagation()` on Enter submit
- `main.ts`: skip `handleOk` on Enter when chat IME is focused
- `ChatPanel.confirm`: `sendBusy` guard while `onSend` is in flight

## Verify

1. Open chat, focus input, Enter once on world channel → one line, one horn.
2. Softkey「发送」without IME focus still sends once.
3. Team/guild/private still insert their local echo once.
