/**
 * Persisted UI font preference for LCD.
 *
 * - cn: OS Songti stack (SimSun / Songti SC / …) — not bundled
 * - sky / gb16 / gb: UC3 packs
 */

export type UiFontId = "cn" | "sky" | "gb16" | "gb";

/** Alias kept for existing imports. */
export type Uc3FontId = UiFontId;

export type Uc3FontOption = {
  id: UiFontId;
  /** Public URL served from Vite / Capacitor assets (empty = system face). */
  url: string;
  /** File name for tips. */
  file: string;
  /** Menu / dialog label. */
  label: string;
};

/** Default LCD font pack. */
export const DEFAULT_UI_FONT_ID: UiFontId = "sky";

export const UC3_FONT_OPTIONS: readonly Uc3FontOption[] = [
  {
    id: "cn",
    url: "",
    file: "系统宋体",
    label: "高清字体",
  },
  {
    id: "sky",
    url: "./font_16_sky.uc3",
    file: "font_16_sky.uc3",
    label: "天空点阵",
  },
  {
    id: "gb16",
    url: "./gb16.uc2.uc3",
    file: "gb16.uc2.uc3",
    label: "全字库",
  },
  {
    id: "gb",
    url: "./font_gb.uc3",
    file: "font_gb.uc3",
    label: "经典子集",
  },
] as const;

const LS_KEY = "pix_uc3_font";

const UC3_PACK_IDS: ReadonlySet<string> = new Set(["sky", "gb16", "gb"]);
const WEB_FONT_IDS: ReadonlySet<string> = new Set(["cn"]);

export function isUc3PackId(id: string): id is "sky" | "gb16" | "gb" {
  return UC3_PACK_IDS.has(id);
}

export function isWebUiFontId(id: string): id is "cn" {
  return WEB_FONT_IDS.has(id);
}

export function uc3FontOption(id: Uc3FontId): Uc3FontOption {
  return (
    UC3_FONT_OPTIONS.find((o) => o.id === id) ??
    UC3_FONT_OPTIONS.find((o) => o.id === DEFAULT_UI_FONT_ID)!
  );
}

export function getUc3FontId(): Uc3FontId {
  try {
    const v = localStorage.getItem(LS_KEY);
    if (v === "sky" || v === "gb16" || v === "gb" || v === "cn") {
      return v;
    }
    // Legacy ids no longer shipped.
    if (v === "fusion") return DEFAULT_UI_FONT_ID;
  } catch {
    /* ignore */
  }
  return DEFAULT_UI_FONT_ID;
}

export function getUc3FontUrl(id: Uc3FontId = getUc3FontId()): string {
  return uc3FontOption(id).url;
}

export function setUc3FontId(id: Uc3FontId): void {
  try {
    localStorage.setItem(LS_KEY, id);
  } catch {
    /* ignore */
  }
}

export function uc3FontIdFromUrl(url: string): Uc3FontId | null {
  if (!url) return null;
  const hit = UC3_FONT_OPTIONS.find(
    (o) => o.url && (o.url === url || url.endsWith(o.file)),
  );
  return hit?.id ?? null;
}
