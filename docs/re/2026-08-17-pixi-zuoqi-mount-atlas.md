# Pixi map mounts (`zuoqi` TexturePacker atlas)

Date: 2026-08-17

## Summary

Map local-player mounts load from `JHOnlineData/zuoqi/`:

1. **sfx** — `sfx_<id>_0.{plist,png}` side-view only (mirror for left).
2. **dir** — `<id>/(monster|player)_<id>_0_<act>_<part>.{plist,png}`  
   Frame names encode facing `…_<act>_<face>_####.png` (0–7).  
   Plist `part` is an atlas split; loader probes parts and filters by face.

## Assets

- Durable: `PixiJS_web/assets/zuoqi/` (synced into public by `sync-offline-assets.mjs`).
- IDs: `11370`, `11372`, `11802`, `16171`, `20148`, `40183`, `75535`, `75537`, `75549`
  (`16128` removed — too large).

## Client path

1. `resources/plistAtlas.ts` — format-2 loader; optional `frameNamePrefix`.
2. `resources/mountFx.ts` — `MountOverlay`; dir packs swap face sheets; sfx flips.
3. Status badge **骑** → `MountPanel`.
4. Preference: `?mount=11370` or `localStorage.pixi.mountId`.

## Facing map (dir packs)

CBE 1=up→face0, 2=right→2, 3=down→4, 4=left→6.

## Quick test

Enter world → **骑** → pick `monster_11370` / `11372` / `11802` and walk all four ways.
