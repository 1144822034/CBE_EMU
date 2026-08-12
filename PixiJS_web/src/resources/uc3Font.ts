/**
 * FT16 / UC3 16×16 bitmap fonts.
 *
 * - `font_gb.uc3` (CBE): ASCII key = `(ch << 8)`, Chinese = GBK LE word.
 * - `gb16.uc2.uc3` / `font_16_sky.uc3`: table key = Unicode BMP code point.
 *
 * Header: i32 magic "FT16", i32 width, i32 height, i32 count;
 * then count×{u16 key LE, u32 offset}.
 */

import {
  getUc3FontId,
  getUc3FontUrl,
  setUc3FontId,
  type Uc3FontId,
  uc3FontOption,
} from "./uiFontPreference";

export type Uc3Font = {
  width: number;
  height: number;
  /** Unicode code point → glyph bitmap (1 bit/pixel, row-major MSB first). */
  glyphs: Map<number, Uint8Array>;
  /** Source file URL (debug). */
  url: string;
};

/** Default pack when preference missing. */
export const UC3_FONT_URL = "./font_16_sky.uc3";

let cached: Uc3Font | null = null;
let loadPromise: Promise<Uc3Font> | null = null;
let cachedUrl: string | null = null;

/** Currently loaded pack URL (or preferred URL before load). */
export function activeUc3FontUrl(): string {
  return cachedUrl ?? getUc3FontUrl();
}

function decodeGbkChar(lo: number, hi: number): number | null {
  try {
    const s = new TextDecoder("gbk").decode(Uint8Array.of(lo, hi));
    if (!s) return null;
    return s.codePointAt(0) ?? null;
  } catch {
    try {
      const s = new TextDecoder("gb18030").decode(Uint8Array.of(lo, hi));
      if (!s) return null;
      return s.codePointAt(0) ?? null;
    } catch {
      return null;
    }
  }
}

/** Unicode pack: has U+4E00 and raw ASCII `A` (0x41), not font_gb's 0x4100. */
function isUnicodeKeyed(keys: number[]): boolean {
  let cjk = 0;
  let hasAsciiA = false;
  let hasShiftA = false;
  for (const k of keys) {
    if (k === 0x41) hasAsciiA = true;
    if (k === 0x4100) hasShiftA = true;
    if (k >= 0x4e00 && k <= 0x9fff) cjk++;
  }
  if (hasAsciiA && !hasShiftA) return true;
  return cjk > keys.length / 4;
}

function parseUc3(buf: Uint8Array, url: string): Uc3Font {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const width = dv.getInt32(4, true);
  const height = dv.getInt32(8, true);
  const count = dv.getInt32(12, true);
  const bitmapSize = (width * height) / 8;
  const table = 16;
  const entries: { key: number; off: number }[] = [];
  for (let i = 0; i < count; i++) {
    const key = dv.getUint16(table + i * 6, true);
    const off = dv.getUint32(table + i * 6 + 2, true);
    if (off + bitmapSize > buf.length) continue;
    entries.push({ key, off });
  }
  const unicode = isUnicodeKeyed(entries.map((e) => e.key));
  const glyphs = new Map<number, Uint8Array>();
  for (const { key, off } of entries) {
    const bits = buf.subarray(off, off + bitmapSize);
    if (unicode) {
      glyphs.set(key, bits);
      continue;
    }
    if ((key & 0xff) === 0 && key >= 0x2000 && key <= 0x7f00) {
      glyphs.set(key >> 8, bits);
    } else {
      const cp = decodeGbkChar(key & 0xff, (key >> 8) & 0xff);
      if (cp != null) glyphs.set(cp, bits);
    }
  }
  return { width, height, glyphs, url };
}

export async function loadUc3Font(url?: string): Promise<Uc3Font> {
  const target = url ?? getUc3FontUrl();
  if (cached && cachedUrl === target) return cached;
  if (loadPromise) {
    try {
      const font = await loadPromise;
      if (cachedUrl === target) return font;
    } catch {
      /* fall through */
    }
  }

  const pending = (async () => {
    const res = await fetch(target);
    if (!res.ok) throw new Error(`${target} HTTP ${res.status}`);
    const buf = new Uint8Array(await res.arrayBuffer());
    const font = parseUc3(buf, target);
    cached = font;
    cachedUrl = target;
    console.info(
      `[font] ${target} ${font.width}x${font.height} glyphs=${font.glyphs.size}`,
    );
    return font;
  })();
  loadPromise = pending;
  try {
    return await pending;
  } catch (e) {
    if (loadPromise === pending) {
      loadPromise = null;
      cached = null;
      cachedUrl = null;
    }
    throw e;
  }
}

/** Persist preference, clear cache, and load the chosen pack. */
export async function applyUc3FontId(id: Uc3FontId): Promise<Uc3Font> {
  setUc3FontId(id);
  clearUc3FontCache();
  return loadUc3Font(uc3FontOption(id).url);
}

/** Drop cache (tests / hot-swap font URL). */
export function clearUc3FontCache(): void {
  cached = null;
  loadPromise = null;
  cachedUrl = null;
}

export function currentUc3FontId(): Uc3FontId {
  return getUc3FontId();
}

export function fontCellWidth(font: Uc3Font, cp: number): number {
  return cp < 0x80 ? font.width / 2 : font.width;
}

export function measureUc3(font: Uc3Font, text: string): number {
  let w = 0;
  for (const ch of text) {
    const cp = ch.codePointAt(0)!;
    w += fontCellWidth(font, cp);
  }
  return w;
}

export type RenderUc3Opts = {
  color?: number;
  /** 1px outline (default black; map nameplates / battle floats). */
  outline?: boolean;
  /** Outline RGB when `outline` is set (default 0x000000). */
  outlineColor?: number;
  /**
   * Thin outline: only 4 cardinal neighbors (CBE battle float look).
   * Default false = 8-neighbor box (thicker, for nameplates).
   */
  outlineThin?: boolean;
  maxWidth?: number;
  /**
   * Soften 1-bit glyph edges via 2× supersample + box filter.
   * Default false — soft alpha + CSS `pixelated` reads as bold/ghost.
   * Only enable for rare non-pixelated previews.
   */
  smooth?: boolean;
};

/** Render opaque RGBA; transparent where glyph bit is 0. */
export function renderUc3ToRgba(
  font: Uc3Font,
  text: string,
  opts: RenderUc3Opts = {},
): { width: number; height: number; rgba: Uint8ClampedArray } {
  const color = opts.color ?? 0xffffff;
  const outline = opts.outline ?? false;
  const outlineColor = opts.outlineColor ?? 0x000000;
  const outlineThin = opts.outlineThin ?? false;
  const smooth = opts.smooth === true;
  const ss = smooth ? 2 : 1;
  const cr = (color >> 16) & 0xff;
  const cg = (color >> 8) & 0xff;
  const cb = color & 0xff;
  const or_ = (outlineColor >> 16) & 0xff;
  const og = (outlineColor >> 8) & 0xff;
  const ob = outlineColor & 0xff;
  const pad = outline ? 1 : 0;
  let advance = 0;
  const chars: { cp: number; x: number; bits: Uint8Array | null; drawW: number }[] = [];
  for (const ch of text) {
    const cp = ch.codePointAt(0)!;
    const drawW = fontCellWidth(font, cp);
    if (opts.maxWidth != null && advance + drawW > opts.maxWidth) break;
    chars.push({ cp, x: advance, bits: font.glyphs.get(cp) ?? null, drawW });
    advance += drawW;
  }
  const logicalW = Math.max(1, advance + pad * 2);
  const logicalH = font.height + pad * 2;
  const width = logicalW * ss;
  const height = logicalH * ss;
  const rgba = new Uint8ClampedArray(width * height * 4);
  const pitch = font.width / 8;

  const plot = (px: number, py: number, r: number, g: number, b: number) => {
    if (px < 0 || py < 0 || px >= width || py >= height) return;
    const i = (py * width + px) * 4;
    rgba[i] = r;
    rgba[i + 1] = g;
    rgba[i + 2] = b;
    rgba[i + 3] = 255;
  };

  const plotCell = (lx: number, ly: number, r: number, g: number, b: number) => {
    const bx = lx * ss;
    const by = ly * ss;
    for (let dy = 0; dy < ss; dy++) {
      for (let dx = 0; dx < ss; dx++) {
        plot(bx + dx, by + dy, r, g, b);
      }
    }
  };

  const outlineOffsets: ReadonlyArray<readonly [number, number]> = outlineThin
    ? [
        [1, 0],
        [-1, 0],
        [0, 1],
        [0, -1],
      ]
    : [
        [-1, -1],
        [0, -1],
        [1, -1],
        [-1, 0],
        [1, 0],
        [-1, 1],
        [0, 1],
        [1, 1],
      ];

  for (const c of chars) {
    if (!c.bits) continue;
    const ox = c.x + pad;
    const oy = pad;
    for (let j = 0; j < font.height; j++) {
      for (let i = 0; i < c.drawW; i++) {
        const byteIndex = j * pitch + (i >> 3);
        const bitIndex = 7 - (i & 7);
        if (!((c.bits[byteIndex]! >> bitIndex) & 1)) continue;
        if (outline) {
          for (const [dx, dy] of outlineOffsets) {
            plotCell(ox + i + dx, oy + j + dy, or_, og, ob);
          }
        }
      }
    }
    for (let j = 0; j < font.height; j++) {
      for (let i = 0; i < c.drawW; i++) {
        const byteIndex = j * pitch + (i >> 3);
        const bitIndex = 7 - (i & 7);
        if (!((c.bits[byteIndex]! >> bitIndex) & 1)) continue;
        plotCell(ox + i, oy + j, cr, cg, cb);
      }
    }
  }

  if (!smooth) {
    return { width, height, rgba };
  }
  return downsampleBox2x(rgba, width, height);
}

/** 2×2 box filter → logical size with soft edge alpha. */
function downsampleBox2x(
  hi: Uint8ClampedArray,
  hiW: number,
  hiH: number,
): { width: number; height: number; rgba: Uint8ClampedArray } {
  const width = hiW >> 1;
  const height = hiH >> 1;
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let r = 0;
      let g = 0;
      let b = 0;
      let a = 0;
      for (let dy = 0; dy < 2; dy++) {
        for (let dx = 0; dx < 2; dx++) {
          const i = ((y * 2 + dy) * hiW + (x * 2 + dx)) * 4;
          const aa = hi[i + 3]!;
          r += hi[i]! * aa;
          g += hi[i + 1]! * aa;
          b += hi[i + 2]! * aa;
          a += aa;
        }
      }
      const o = (y * width + x) * 4;
      if (a > 0) {
        rgba[o] = Math.round(r / a);
        rgba[o + 1] = Math.round(g / a);
        rgba[o + 2] = Math.round(b / a);
        rgba[o + 3] = Math.round(a / 4);
      }
    }
  }
  return { width, height, rgba };
}
