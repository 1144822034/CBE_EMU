# Pixi map wings (`chibang` TexturePacker atlas)

Date: 2026-08-16

## Summary

Map local-player wings load from `JHOnlineData/chibang/sfx_<id>_0.{plist,png}`
(TexturePacker / Cocos plist format 2). This is **not** the CBE `.actor`
pipeline; sheets are Pixi-only decorative overlays behind the body sprite.

## Assets

- Durable source: `PixiJS_web/assets/chibang/` (32 complete pairs).
- Packaged into `public/JHOnlineData/chibang/` by `scripts/sync-offline-assets.mjs`
  after the JHOnlineData wipe/copy (same pattern as `arena_kill_sfx`).
- IDs: `88010`–`88036`, `90002`, `90003`, `90006`, `90007`, `90010`.

## Client path

1. `resources/plistAtlas.ts` — parse format-2 plist, build Pixi `Texture`s
   (rotated frames use swapped atlas rect + `rotate: 2`).
2. `resources/wingFx.ts` — `WingOverlay` animates frames, scales to ~body
   height, attaches at mid-back. Plist `offset` is **not** applied as map
   displacement (88014 `{57,-115}` otherwise lands below the feet).
3. `SceneView.setPlayerWing(id)` — mounts overlay on the local player
   container (`zIndex` between +16 sparkles and body).
4. Preference: URL `?wing=88010` or `localStorage.pixi.wingId`.

## Not yet

- No CBE / mock-server equip field for wing id (unresolved protocol).
- Peers / battle units do not show wings yet.
- Fine-tuned per-id attach offsets may need art pass after in-game look.
- `sfx_88011_0_0008` is a TexturePacker 1×1 sentinel; loader drops frames
  with size ≤2 so the loop does not flash a scaled black box.
- Frames are normalized onto a shared `orig` canvas and **opaque-pixel
  centroids** are pinned to the canvas center (90002 content mass drifted
  ~40px even after box-centering). Hard-refresh to pick up `centroid-v1` cache.

## Quick test

1. `npm run sync:assets` (ensures `public/.../chibang` exists).
2. Enter world map → tap status strip **翅** → pick `sfx_88010` etc.
3. Or URL `?wing=88010` / `localStorage.pixi.wingId`.
4. Console still works: `localStorage.setItem('pixi.wingId','88029'); location.reload()`.
