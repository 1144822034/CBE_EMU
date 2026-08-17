# Pixi chenghao designation FX (`chenghao` TexturePacker atlas)

Date: 2026-08-17

## Summary

Map nameplate titles can use animated atlases from
`JHOnlineData/chenghao/sfx_<id>_0.{plist,png}` (86001–86048), in addition to
legacy GIF designations (`riches_name*` / `level_name*`) and arena text titles.

## Unlock / equip policy

- List shows **unlocked** chenghao only (server CSV via `1/31/2`).
- Equip via `1/31/3` (kind=1); classic `23/3` clears equipped chenghao.
- Nearby peers see equipped id via `1/31/5` (not classic `titleBadge`).
- See `docs/re/2026-08-17-pixi-cosmetics-1-31.md`.

## Client path

1. `resources/chenghaoFx.ts` — catalog + `ChenghaoOverlay`.
2. `SceneView.setPlayerNameplate` / `ActorLayer.refreshTitleBadge` — resolve
   `chenghao:<id>` and play atlas.
3. `GameSession.syncCosmetics` / `selectDesignation` / `withChenghaoDesignations`.
4. Durable assets: `PixiJS_web/assets/chenghao/` via `sync-offline-assets.mjs`.

## List names

Edit `name` on `CHENGHAO_SPECS` in `chenghaoFx.ts`.

## 86043 “不停翻转” / 左右合拢

`sfx_86043_0` packs a Y-axis tumble: full-face frames mixed with left-only /
right-only half frames and a near-empty cell. Looping those halves (and
centroid-centering them) looked like the title closing in from both sides,
with a leftover left half.

`ChenghaoOverlay` uses `loadPlistAtlas(..., { stableFace: true })`:
1. Drop near-empty cells.
2. If opaque mass thrashes L↔R across frames, keep only balanced full-face
   frames (`bboxW≥95%`, `|cx-0.5|≤0.08`, `|L-ratio-0.5|≤0.12`, opaque≥75% max).
   For 86043 that leaves frames 2/3/5/6 (pulse only).
3. Non-thrash sheets keep their normal loops.

Cache bump `centroid-v3-face` (hard-refresh).

## Quick test

菜单 → 称号 → 选任意 `860xx` → 头顶应播动画；换回财富/等级称号后特效清除。
重点回归 `86043 天下无双`：应脉冲发光，不应左右合拢或半幅残留。
