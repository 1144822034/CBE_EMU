/**
 * Persisted UC3 bitmap font preference for LCD UI.
 * TTF paths are retired — only `.uc3` packs.
 */

export type Uc3FontId = "sky" | "gb16" | "gb";

export type Uc3FontOption = {
  id: Uc3FontId;
  /** Public URL served from Vite / Capacitor assets. */
  url: string;
  /** File name for tips. */
  file: string;
  /** Menu / dialog label. */
  label: string;
};

export const UC3_FONT_OPTIONS: readonly Uc3FontOption[] = [
  {
    id: "sky",
    url: "/font_16_sky.uc3",
    file: "font_16_sky.uc3",
    label: "天空点阵",
  },
  {
    id: "gb16",
    url: "/gb16.uc2.uc3",
    file: "gb16.uc2.uc3",
    label: "全字库",
  },
  {
    id: "gb",
    url: "/font_gb.uc3",
    file: "font_gb.uc3",
    label: "经典子集",
  },
] as const;

const LS_KEY = "pix_uc3_font";

export function uc3FontOption(id: Uc3FontId): Uc3FontOption {
  return UC3_FONT_OPTIONS.find((o) => o.id === id) ?? UC3_FONT_OPTIONS[0]!;
}

export function getUc3FontId(): Uc3FontId {
  try {
    const v = localStorage.getItem(LS_KEY);
    if (v === "sky" || v === "gb16" || v === "gb") return v;
  } catch {
    /* ignore */
  }
  return "sky";
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
  const hit = UC3_FONT_OPTIONS.find(
    (o) => o.url === url || url.endsWith(o.file),
  );
  return hit?.id ?? null;
}
