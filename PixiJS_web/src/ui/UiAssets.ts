import { Container, Rectangle, Sprite, Texture } from "pixi.js";
import { loadGif } from "../resources/loader";
import type { GifRgba } from "../resources/lkv2Gif";

const fullCache = new Map<string, Texture>();
const cropCache = new Map<string, Texture>();
/** Decoded RGBA kept for pixel-accurate crops (fuhao flip, digit sheets). */
const rgbaCache = new Map<string, GifRgba>();

/** Soft/hard GPU caps — crop keys otherwise grow without bound on long sessions. */
const FULL_CACHE_SOFT = 48;
const FULL_CACHE_HARD = 64;
const CROP_CACHE_SOFT = 96;
const CROP_CACHE_HARD = 128;

/**
 * HUD sheets that DigitStrip / WorldHud keep bound across remounts.
 * Never LRU-evict these base textures while the soft/hard trim runs.
 */
const PINNED_FULL = new Set([
  "ui_hpspnum.gif",
  "time_number.gif",
  "numberexp.gif",
  "-num.gif",
  "+num.gif",
  "UIsh.gif",
  "softkey.gif",
]);

function touchLru<T>(map: Map<string, T>, key: string, value: T): void {
  if (map.has(key)) map.delete(key);
  map.set(key, value);
}

function trimTextureCache(
  map: Map<string, Texture>,
  soft: number,
  hard: number,
  pinned?: ReadonlySet<string>,
): void {
  if (map.size <= hard) return;
  for (const [k, tex] of map) {
    if (map.size <= soft) break;
    if (pinned?.has(k)) continue;
    try {
      tex.destroy(true);
    } catch {
      /* ignore */
    }
    map.delete(k);
  }
}

/**
 * Drop GPU-bound UI textures after webglcontextrestored.
 * Keeps decoded RGBA so the next loadUiTexture/loadUiCrop rebuilds from CPU.
 * Call before WorldHud / SceneView remount Texture chrome.
 */
export function invalidateUiGpuTextures(): void {
  for (const tex of fullCache.values()) {
    try {
      tex.destroy(true);
    } catch {
      /* ignore */
    }
  }
  fullCache.clear();
  for (const tex of cropCache.values()) {
    try {
      tex.destroy(true);
    } catch {
      /* ignore */
    }
  }
  cropCache.clear();
}

async function loadGifCached(name: string): Promise<GifRgba> {
  const hit = rgbaCache.get(name);
  if (hit) return hit;
  const gif = await loadGif(name);
  rgbaCache.set(name, gif);
  return gif;
}

/** Decode JHOnlineData LKV2 GIF into a Pixi Texture (nearest-neighbor). */
export async function loadUiTexture(name: string): Promise<Texture> {
  const hit = fullCache.get(name);
  if (hit && !hit.destroyed) {
    touchLru(fullCache, name, hit);
    return hit;
  }
  const gif = await loadGifCached(name);
  const canvas = document.createElement("canvas");
  canvas.width = gif.width;
  canvas.height = gif.height;
  const ctx = canvas.getContext("2d")!;
  ctx.putImageData(
    new ImageData(new Uint8ClampedArray(gif.rgba), gif.width, gif.height),
    0,
    0,
  );
  const tex = Texture.from(canvas);
  tex.source.scaleMode = "nearest";
  touchLru(fullCache, name, tex);
  trimTextureCache(fullCache, FULL_CACHE_SOFT, FULL_CACHE_HARD, PINNED_FULL);
  return tex;
}

export async function tryLoadUiTexture(name: string): Promise<Texture | null> {
  try {
    return await loadUiTexture(name);
  } catch (e) {
    console.warn("[ui] missing", name, e);
    return null;
  }
}

export type UiCropOpts = {
  /** Mirror each crop horizontally while copying pixels. */
  flipX?: boolean;
};

/**
 * Crop a sub-rect from a UI GIF into its own texture (pixel copy).
 * Avoids Pixi v8 shared-atlas frame/orig sizing bugs on small marks.
 */
export async function loadUiCrop(
  name: string,
  x: number,
  y: number,
  w: number,
  h: number,
  opts?: UiCropOpts,
): Promise<Texture | null> {
  const flipX = !!opts?.flipX;
  const key = `${name}@${x},${y},${w},${h}${flipX ? ":fx" : ""}`;
  const hit = cropCache.get(key);
  if (hit && !hit.destroyed) {
    touchLru(cropCache, key, hit);
    return hit;
  }
  try {
    const gif = await loadGifCached(name);
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return null;
    if (x + w > gif.width || y + h > gif.height) return null;
    const rgba = new Uint8ClampedArray(w * h * 4);
    for (let row = 0; row < h; row++) {
      for (let col = 0; col < w; col++) {
        const sx = flipX ? x + (w - 1 - col) : x + col;
        const si = ((y + row) * gif.width + sx) * 4;
        const di = (row * w + col) * 4;
        rgba[di] = gif.rgba[si]!;
        rgba[di + 1] = gif.rgba[si + 1]!;
        rgba[di + 2] = gif.rgba[si + 2]!;
        rgba[di + 3] = gif.rgba[si + 3]!;
      }
    }
    const canvas = document.createElement("canvas");
    canvas.width = w;
    canvas.height = h;
    canvas.getContext("2d")!.putImageData(new ImageData(rgba, w, h), 0, 0);
    const tex = Texture.from(canvas);
    tex.source.scaleMode = "nearest";
    touchLru(cropCache, key, tex);
    trimTextureCache(cropCache, CROP_CACHE_SOFT, CROP_CACHE_HARD);
    return tex;
  } catch (e) {
    console.warn("[ui] crop", name, e);
    return null;
  }
}

export function uiSprite(tex: Texture, w?: number, h?: number): Sprite {
  const s = new Sprite(tex);
  if (w != null) s.width = w;
  if (h != null) s.height = h;
  return s;
}

/** Horizontal digit strip from original number GIF sheets. */
export class DigitStrip {
  readonly root = new Container();
  private readonly glyphs = new Map<string, Texture>();
  /** Glyph advance used by battle float layout / edge clamp. */
  readonly cellW: number;
  private readonly cellH: number;

  private constructor(sheet: Texture, cellW: number, cellH: number, order: string[]) {
    this.cellW = cellW;
    this.cellH = cellH;
    order.forEach((ch, i) => {
      const t = new Texture({
        source: sheet.source,
        frame: new Rectangle(i * cellW, 0, cellW, cellH),
      });
      t.source.scaleMode = "nearest";
      this.glyphs.set(ch, t);
    });
  }

  /** ui_hpspnum.gif 44×5 → 12 glyphs: 0 | 1 2 3 4 5 6 7 8 9 / */
  static async hpspNum(): Promise<DigitStrip | null> {
    const tex = await tryLoadUiTexture("ui_hpspnum.gif");
    if (!tex) return null;
    const cellW = Math.floor(tex.width / 12) || 3;
    return new DigitStrip(tex, cellW, tex.height, [
      "0",
      "|",
      "1",
      "2",
      "3",
      "4",
      "5",
      "6",
      "7",
      "8",
      "9",
      "/",
    ]);
  }

  /**
   * Battle vitals digits — sheet is 11 cells (0-9 + /), not the world 12-glyph map.
   * Evidence: ui_hpspnum.gif 44×5 ascii dump; screenshot 「9161 / 9161」.
   */
  static async battleHpspNum(): Promise<DigitStrip | null> {
    const tex = await tryLoadUiTexture("ui_hpspnum.gif");
    if (!tex) return null;
    const cellW = Math.floor(tex.width / 11) || 4;
    return new DigitStrip(tex, cellW, tex.height, [
      "0",
      "1",
      "2",
      "3",
      "4",
      "5",
      "6",
      "7",
      "8",
      "9",
      "/",
    ]);
  }

  /** time_number.gif 77×7 → 0..9 (+ trailing glyph). */
  static async timeNum(): Promise<DigitStrip | null> {
    const tex = await tryLoadUiTexture("time_number.gif");
    if (!tex) return null;
    const cellW = Math.floor(tex.width / 11) || 7;
    return new DigitStrip(tex, cellW, tex.height, [
      "0",
      "1",
      "2",
      "3",
      "4",
      "5",
      "6",
      "7",
      "8",
      "9",
      ":",
    ]);
  }

  /** numberexp.gif 50×6 → 0..9 */
  static async expNum(): Promise<DigitStrip | null> {
    const tex = await tryLoadUiTexture("numberexp.gif");
    if (!tex) return null;
    const cellW = Math.floor(tex.width / 10) || 5;
    return new DigitStrip(tex, cellW, tex.height, [
      "0",
      "1",
      "2",
      "3",
      "4",
      "5",
      "6",
      "7",
      "8",
      "9",
    ]);
  }

  /**
   * Battle damage float digits — `-num.gif` 110×16, 10 cells (0-9), orange/red.
   * Evidence: JHOnlineData sheet + mmBattle numeric float path.
   */
  static async battleHurtNum(): Promise<DigitStrip | null> {
    const tex = await tryLoadUiTexture("-num.gif");
    if (!tex) return null;
    const cellW = Math.floor(tex.width / 10) || 11;
    return new DigitStrip(tex, cellW, tex.height, [
      "0",
      "1",
      "2",
      "3",
      "4",
      "5",
      "6",
      "7",
      "8",
      "9",
    ]);
  }

  /**
   * Battle heal float digits — `+num.gif` 110×16, 10 cells (0-9), yellow-green.
   */
  static async battleHealNum(): Promise<DigitStrip | null> {
    const tex = await tryLoadUiTexture("+num.gif");
    if (!tex) return null;
    const cellW = Math.floor(tex.width / 10) || 11;
    return new DigitStrip(tex, cellW, tex.height, [
      "0",
      "1",
      "2",
      "3",
      "4",
      "5",
      "6",
      "7",
      "8",
      "9",
    ]);
  }

  /** Disposable digit row for one float (shares sheet textures). */
  buildRow(text: string): Container {
    const row = new Container();
    let cx = 0;
    for (const ch of text) {
      const g = this.glyphs.get(ch);
      if (!g) {
        cx += this.cellW;
        continue;
      }
      const spr = new Sprite(g);
      spr.position.set(cx, 0);
      row.addChild(spr);
      cx += this.cellW;
    }
    return row;
  }

  setText(text: string, x?: number, y?: number): void {
    this.root.removeChildren().forEach((c) => c.destroy());
    if (x != null && y != null) this.root.position.set(x, y);
    let cx = 0;
    for (const ch of text) {
      const g = this.glyphs.get(ch);
      if (!g) {
        cx += this.cellW;
        continue;
      }
      const spr = new Sprite(g);
      spr.position.set(cx, 0);
      this.root.addChild(spr);
      cx += this.cellW;
    }
  }

  get width(): number {
    return this.root.width;
  }
}
