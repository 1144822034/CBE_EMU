/**
 * Global LCD UI font: YaHei Consolas Hybrid TTF.
 * Source: `web/fs/YaHei Consolas Hybrid.ttf` → `/YaHeiConsolasHybrid.ttf`
 * (Vite middleware in dev; `sync-offline-assets` copies into `public/` for APK).
 *
 * Opt back to UC3 pointillism with `?uc3=1` or `localStorage.pix_uc3=1`.
 */

/** Clean URL; Vite / public serve the TTF under this name. */
export const SYSTEM_FONT_URL = "/YaHeiConsolasHybrid.ttf";

/** TTF name-table family (nid=1). */
export const SYSTEM_FONT_FAMILY_NAME = "YaHei Consolas Hybrid";

export const SYSTEM_FONT_FALLBACK =
  'SimSun, "Songti SC", "Microsoft YaHei", "PingFang SC", "Noto Sans SC", sans-serif';

let faceLoaded = false;
let loadPromise: Promise<boolean> | null = null;
/** Default true — LCD labels use TTF unless UC3 opt-in. */
let preferSystem = true;

export function isSystemFontLoaded(): boolean {
  return faceLoaded;
}

/** False only when `?uc3=1` / localStorage opts into bitmap font. */
export function preferSystemFont(): boolean {
  return preferSystem;
}

export function setPreferSystemFont(on: boolean): void {
  preferSystem = on;
}

/** CSS stack for canvas / Pixi.Text (custom face first when loaded). */
export function uiFontFamily(): string {
  return faceLoaded
    ? `"${SYSTEM_FONT_FAMILY_NAME}", ${SYSTEM_FONT_FALLBACK}`
    : SYSTEM_FONT_FALLBACK;
}

/**
 * UC3 opt-in via `?uc3=1` / `?uc3=0`, or `localStorage.pix_uc3=1`.
 * Query wins over localStorage. Default = system TTF.
 */
export function uc3FontRequested(): boolean {
  try {
    const q = new URLSearchParams(location.search).get("uc3");
    if (q === "1" || q === "true" || q === "yes") return true;
    if (q === "0" || q === "false" || q === "no") return false;
    return localStorage.getItem("pix_uc3") === "1";
  } catch {
    return false;
  }
}

/** Load TTF into `document.fonts`. Safe to call multiple times. */
export function loadSystemFont(): Promise<boolean> {
  if (faceLoaded) return Promise.resolve(true);
  if (loadPromise) return loadPromise;
  loadPromise = (async () => {
    if (typeof FontFace === "undefined" || !document.fonts) {
      console.warn("[sysfont] FontFace API unavailable");
      return false;
    }
    try {
      const face = new FontFace(
        SYSTEM_FONT_FAMILY_NAME,
        `url(${SYSTEM_FONT_URL})`,
        { style: "normal", weight: "400" },
      );
      const loaded = await face.load();
      document.fonts.add(loaded);
      await document.fonts.load(`400 16px "${SYSTEM_FONT_FAMILY_NAME}"`);
      faceLoaded = true;
      try {
        document.documentElement.style.setProperty(
          "--ui-font",
          `"${SYSTEM_FONT_FAMILY_NAME}", ${SYSTEM_FONT_FALLBACK}`,
        );
      } catch {
        /* ignore */
      }
      console.info("[sysfont] loaded", SYSTEM_FONT_FAMILY_NAME);
      return true;
    } catch (e) {
      console.warn("[sysfont] load failed", e);
      return false;
    }
  })();
  return loadPromise;
}
