/**
 * Kill-switch for TexturePacker map cosmetics (wing / mount / animated chenghao).
 *
 * When false:
 * - No plist/PNG atlas GPU load (cuts WebView VRAM / black-screen pressure)
 * - Equip IDs, 翅膀/坐骑/称号 panels, and server 功效 stay intact
 * - Sheets live under `assets.bak_cosmetics/` until re-enabled
 *
 * Restore: move folders back to `assets/{chibang,zuoqi,chenghao}`, set true,
 * then `npm run sync:assets`.
 *
 * Stable profile (APK): VITE_MAP_COSMETIC_ATLAS unset/1, peer FX default off
 * (`peerCosmeticsPref`), PEER_HEAVY_CAP 2–3, plist unload ~8s, battle drops map GPU.
 * No-FX APK: `npm run build:android:nofx` sets VITE_MAP_COSMETIC_ATLAS=0.
 */
export const MAP_COSMETIC_ATLAS_ENABLED =
  (import.meta.env.VITE_MAP_COSMETIC_ATLAS ?? "1") !== "0";
