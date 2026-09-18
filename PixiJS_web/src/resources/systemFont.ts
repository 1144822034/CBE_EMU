/**
 * Web fonts + UC3 packs for LCD.
 *
 * - cn: OS Songti stack (SimSun / Songti SC / …) — no bundled TTF
 * - sky/gb/gb16: UC3 packs
 *
 * Bundled simsun.ttf was removed: ~10MB APK cost, and devices that cannot
 * render Songti still failed even with the TTF embedded.
 */

import {
  getUc3FontId,
  isUc3PackId,
  isWebUiFontId,
  setUc3FontId,
  type UiFontId,
} from "./uiFontPreference";
import { applyUc3FontId, loadUc3Font } from "./uc3Font";

/** OS Songti / serif stack for cn (DOM overlay + rare canvas bake). */
export const SYSTEM_SONG_FAMILY =
  'SimSun, "宋体", "Songti SC", "Noto Serif CJK SC", "Source Han Serif SC", serif';

export const SYSTEM_FONT_OS_FALLBACK = SYSTEM_SONG_FAMILY;
export const SYSTEM_FONT_FALLBACK = SYSTEM_FONT_OS_FALLBACK;

/** Generic device default when Songti names are absent. */
export const DEVICE_UI_FONT_FAMILY = "sans-serif";

/**
 * @deprecated Bundled JHSimSun removed — cn uses {@link SYSTEM_SONG_FAMILY}.
 * Kept so old imports compile.
 */
export const JH_SIMSUN_FAMILY = "SimSun";

/** @deprecated No bundled TTF path. */
export const SIMSUN_TTF_PATH = "";

/** True when menu preference is a web face (not UC3 pack). */
export function preferSystemFont(): boolean {
  return isWebUiFontId(getUc3FontId());
}

export function setPreferSystemFont(_on: boolean): void {
  /* Preference is localStorage pix_uc3_font; use applyUiFontId. */
}

/** @deprecated Fusion pack removed from menu; always false. */
export function isFusionPixelReady(): boolean {
  return false;
}

/**
 * cn no longer ships a TTF — “ready” means the OS stack is selected.
 * @deprecated Name kept for callers; prefer preferSystemFont().
 */
export function isBundledSimSunReady(): boolean {
  return getUc3FontId() === "cn";
}

/** @deprecated */
export function isCnMediumReady(): boolean {
  return getUc3FontId() === "cn";
}

/** Family for LCD web-font / DOM Songti path. */
export function activeWebFontFamily(): string {
  if (getUc3FontId() === "cn") return SYSTEM_SONG_FAMILY;
  return DEVICE_UI_FONT_FAMILY;
}

/** @deprecated Use activeWebFontFamily. */
export function cnMediumFontFamily(): string {
  return activeWebFontFamily();
}

export function uiFontFamily(): string {
  return activeWebFontFamily();
}

export function resolveSystemSongFamily(_force = false): string {
  return SYSTEM_SONG_FAMILY;
}

export function resolvedSystemSongtiName(): string | null {
  return getUc3FontId() === "cn" ? "SimSun" : null;
}

export function resolvedDeviceUiFontName(): string | null {
  return getUc3FontId() === "cn" ? null : "sans-serif";
}

function syncCssUiFont(): void {
  try {
    document.documentElement.style.setProperty("--ui-font", uiFontFamily());
    document.documentElement.classList.toggle(
      "jh-system-font",
      getUc3FontId() === "cn",
    );
  } catch {
    /* ignore */
  }
}

/**
 * @deprecated Bundled TTF removed — no-op success so old callers keep working.
 */
export async function loadBundledSimSunFont(): Promise<boolean> {
  syncCssUiFont();
  return true;
}

/** @deprecated Use loadBundledSimSunFont (no-op) / applyUiFontId. */
export async function loadCnMediumFont(): Promise<boolean> {
  return loadBundledSimSunFont();
}

/** @deprecated Fusion removed. */
export async function loadFusionPixelFont(): Promise<boolean> {
  return false;
}

/** Legacy — OS stack is always “available” as a CSS fallback chain. */
export function isLocalFontAvailable(_family: string): boolean {
  return true;
}

export function detectDeviceDefaultFontName(): string | null {
  return null;
}

export function readComputedSansFontFamily(): string {
  return "sans-serif";
}

/**
 * Persist preference and load UC3 pack when needed.
 */
export async function applyUiFontId(id: UiFontId): Promise<void> {
  if (id === "cn") {
    setUc3FontId("cn");
    syncCssUiFont();
    return;
  }
  if (!isUc3PackId(id)) {
    throw new Error(`unknown font id: ${id}`);
  }
  await applyUc3FontId(id);
  syncCssUiFont();
}

/** Boot helper: honor saved preference. */
export async function bootUiFont(): Promise<void> {
  const id = getUc3FontId();
  if (id === "cn") {
    syncCssUiFont();
    try {
      await loadUc3Font("./font_16_sky.uc3");
    } catch {
      /* ignore */
    }
    return;
  }
  await loadUc3Font();
  syncCssUiFont();
}
