import { Container, Sprite, Text, Texture, Rectangle } from "pixi.js";
import {
  activeUc3FontUrl,
  loadUc3Font,
  renderUc3ToRgba,
} from "../resources/uc3Font";
import {
  preferSystemFont,
  SYSTEM_FONT_FALLBACK,
  uiFontFamily,
  activeWebFontFamily,
} from "../resources/systemFont";
import { getUc3FontId } from "../resources/uiFontPreference";
import { rgbaToTexture } from "../render/rgbaTexture";
import {
  cssHexColor,
  ensureLcdDomTextLayer,
  lcdDomCssScale,
  lcdDomCssZIndex,
  lcdDomOccludedByPopup,
  lcdDomTierFor,
  registerLcdDomSync,
  unregisterLcdDomSync,
} from "./lcdDomText";

/**
 * OS fallback stack for rare non-UC3 paths / HTML softkeys.
 */
export const UI_FONT_FAMILY = SYSTEM_FONT_FALLBACK;

/** Live CSS stack (custom TTF first when loaded). */
export { uiFontFamily };

/** System-font size when `uc3: false`. UC3 source glyphs are 16×16. */
export const UI_FONT_SIZE = 12;

/** Native FT16 cell in the .uc3 file. */
export const UC3_NATIVE_CELL = 16;

/**
 * Display cell after “one size smaller” (16→14).
 * Wrap / measure use this; sprites scale by {@link UC3_DISPLAY_SCALE}.
 */
export const UC3_CELL = 14;

/** Scale baked 16×16 glyphs down to {@link UC3_CELL}. */
export const UC3_DISPLAY_SCALE = UC3_CELL / UC3_NATIVE_CELL;

/** Floor for opt-in smooth Pixi.Text resolution. */
export const UI_TEXT_RESOLUTION = 2;

/** LCD UI: max full-width (CJK) chars per line; ASCII counts as half (GBK byte width). */
export const UI_WRAP_CHARS = 15;

/** GBK-style bytes per line (= {@link UI_WRAP_CHARS} × 2). */
export const UI_WRAP_BYTES = UI_WRAP_CHARS * 2;

/**
 * CSS scale of #phone-wrap (wrapCssPx / LCD_WIDTH). Crisp bake supersamples
 * at this integer factor so glyphs match softkey pixel density.
 */
let lcdDisplayScale = 1;

export function getLcdDisplayScale(): number {
  return lcdDisplayScale;
}

export function setLcdDisplayScale(scale: number): void {
  lcdDisplayScale = Math.max(0.5, Math.min(4, scale));
}

export type LabelTextOpts = {
  color?: number;
  /** Dark stroke (system Text) or UC3 neighbor outline. */
  outline?: boolean;
  outlineColor?: number;
  /** Thinner stroke / UC3 thin outline. */
  outlineThin?: boolean;
  maxWidth?: number;
  /**
   * System font only. Default true when `maxWidth` is set.
   * Pass false for single-line HUD chat (avoid wrapping into 16px slots).
   */
  wordWrap?: boolean;
  /** UC3 only: 2× supersample soft edge. Default false (hard 1-bit). */
  smooth?: boolean;
  fontSize?: number;
  /**
   * font_gb.uc3 bitmap glyphs. Default true — sharp on pixelated LCD.
   * Pass false only for system-font experiments.
   */
  uc3?: boolean;
  /**
   * System font hard-bake when `uc3: false`. Default true.
   */
  crisp?: boolean;
  /**
   * System / Songti DOM only (UC3 bitmap has no weight). Default false.
   */
  bold?: boolean;
};

/** @deprecated Use LabelTextOpts. */
export type RenderUc3Opts = LabelTextOpts;

const OUTLINE_4: ReadonlyArray<readonly [number, number]> = [
  [-1, 0],
  [1, 0],
  [0, -1],
  [0, 1],
];
const OUTLINE_8: ReadonlyArray<readonly [number, number]> = [
  ...OUTLINE_4,
  [-1, -1],
  [-1, 1],
  [1, -1],
  [1, 1],
];

const uc3TexCache = new Map<string, Texture>();
const crispTexCache = new Map<string, Texture>();
/** Soft / hard caps — chat + nameplates otherwise grow without bound on APK. */
const UC3_CACHE_SOFT = 160;
const UC3_CACHE_HARD = 220;
const CRISP_CACHE_SOFT = 64;
const CRISP_CACHE_HARD = 96;
type Uc3LabelHost = {
  rebake(): Promise<void>;
  destroyed: boolean;
  /** Last UC3 cache key while this label shows a sprite. */
  uc3CacheKey?: string;
  /** Last crisp-bake cache key (same live-protect contract as UC3). */
  crispCacheKey?: string;
};
const liveLabels = new Set<Uc3LabelHost>();

function uc3CacheKey(
  text: string,
  color: number,
  outline: boolean,
  outlineColor: number,
  outlineThin: boolean,
  smooth: boolean,
  maxWidth?: number,
): string {
  return `${activeUc3FontUrl()}\0${UC3_CELL}\0${text}\0${color.toString(16)}\0${outline ? 1 : 0}\0${outlineColor.toString(16)}\0${outlineThin ? 1 : 0}\0${smooth ? 1 : 0}\0${maxWidth ?? ""}`;
}

function rememberUc3Cache(key: string, tex: Texture): void {
  // LRU: re-insert so eviction drops oldest idle keys first.
  if (uc3TexCache.has(key)) uc3TexCache.delete(key);
  uc3TexCache.set(key, tex);
  if (uc3TexCache.size <= UC3_CACHE_HARD) return;
  const live = new Set<string>();
  for (const lab of liveLabels) {
    if (lab.uc3CacheKey) live.add(lab.uc3CacheKey);
  }
  for (const [k, t] of uc3TexCache) {
    if (uc3TexCache.size <= UC3_CACHE_SOFT) break;
    if (live.has(k)) continue;
    try {
      t.destroy(true);
    } catch {
      /* ignore */
    }
    uc3TexCache.delete(k);
  }
}

function rememberCrispCache(key: string, tex: Texture): void {
  if (crispTexCache.has(key)) crispTexCache.delete(key);
  crispTexCache.set(key, tex);
  if (crispTexCache.size <= CRISP_CACHE_HARD) return;
  const live = new Set<string>();
  for (const lab of liveLabels) {
    if (lab.crispCacheKey) live.add(lab.crispCacheKey);
  }
  for (const [k, t] of crispTexCache) {
    if (crispTexCache.size <= CRISP_CACHE_SOFT) break;
    if (live.has(k)) continue;
    try {
      t.destroy(true);
    } catch {
      /* ignore */
    }
    crispTexCache.delete(k);
  }
}

/**
 * Drop idle UC3 bakes. Never destroy textures still bound to live labels —
 * that leaves Sprite.texture with a dead source → Pixi alphaMode crash / 黑屏.
 */
export function clearUc3TexCache(): void {
  const live = new Set<string>();
  for (const lab of liveLabels) {
    if (lab.uc3CacheKey) live.add(lab.uc3CacheKey);
  }
  for (const [k, tex] of [...uc3TexCache]) {
    if (live.has(k)) continue;
    try {
      tex.destroy(true);
    } catch {
      /* ignore */
    }
    uc3TexCache.delete(k);
  }
}

/** Drop idle system-font crisp bakes (same live-protect contract as UC3). */
export function clearCrispTexCache(): void {
  const live = new Set<string>();
  for (const lab of liveLabels) {
    if (lab.crispCacheKey) live.add(lab.crispCacheKey);
  }
  for (const [k, tex] of [...crispTexCache]) {
    if (live.has(k)) continue;
    try {
      tex.destroy(true);
    } catch {
      /* ignore */
    }
    crispTexCache.delete(k);
  }
}

/**
 * Re-bake every live label after font switch (UC3 pack or CN Medium).
 * Rebake first, then prune — never destroy textures still on Sprites
 * (that caused Pixi alphaMode storms / 黑屏).
 */
export async function refreshAllUc3Labels(): Promise<void> {
  const jobs: Promise<void>[] = [];
  for (const label of [...liveLabels]) {
    if (!label.destroyed) jobs.push(label.rebake());
  }
  await Promise.all(jobs);
  // Orphans from the previous pack are no longer live-keyed.
  clearUc3TexCache();
  clearCrispTexCache();
}

function crispCacheKey(
  text: string,
  color: number,
  outline: boolean,
  outlineColor: number,
  outlineThin: boolean,
  fontSize: number,
  maxWidth: number | undefined,
  wordWrap: boolean,
  ss: number,
  softAa: boolean,
): string {
  return `${uiFontFamily()}\0${text}\0${color.toString(16)}\0${outline ? 1 : 0}\0${outlineColor.toString(16)}\0${outlineThin ? 1 : 0}\0${fontSize}\0${maxWidth ?? ""}\0${wordWrap ? 1 : 0}\0${ss}\0${softAa ? 1 : 0}`;
}

function crispSupersample(): number {
  return Math.max(1, Math.min(3, Math.round(lcdDisplayScale)));
}

/** Integer ss so hard fold lands on LCD pixels (UC3 contract). */
function cnHardSupersample(): number {
  return Math.max(2, Math.min(4, Math.round(lcdDisplayScale) || 2));
}

function cssColor(rgb: number): string {
  return `#${(rgb >>> 0).toString(16).padStart(6, "0")}`;
}

/**
 * Binary coverage fold → opaque LCD pixels (fusion / pixel faces).
 */
function foldHardNearest(
  src: Uint8ClampedArray,
  sw: number,
  sh: number,
  ss: number,
  cr: number,
  cg: number,
  cb: number,
  coverageFrac = 0.5,
  inkAlpha = 128,
): { w: number; h: number; rgba: Uint8ClampedArray } {
  const dw = Math.max(1, Math.floor(sw / ss));
  const dh = Math.max(1, Math.floor(sh / ss));
  const rgba = new Uint8ClampedArray(dw * dh * 4);
  const cell = ss * ss;
  const need = Math.max(1, Math.round(cell * coverageFrac));
  for (let y = 0; y < dh; y++) {
    for (let x = 0; x < dw; x++) {
      let on = 0;
      for (let oy = 0; oy < ss; oy++) {
        const sy = y * ss + oy;
        if (sy >= sh) break;
        const row = sy * sw;
        for (let ox = 0; ox < ss; ox++) {
          const sx = x * ss + ox;
          if (sx >= sw) break;
          if (src[(row + sx) * 4 + 3]! >= inkAlpha) on++;
        }
      }
      const di = (y * dw + x) * 4;
      if (on >= need) {
        rgba[di] = cr;
        rgba[di + 1] = cg;
        rgba[di + 2] = cb;
        rgba[di + 3] = 255;
      }
    }
  }
  return { w: dw, h: dh, rgba };
}

/**
 * Web pixel face (JHPixel) → 1-bit LCD texels. Songti uses soft-AA instead.
 */
function bakeWebFontHard(
  text: string,
  fontSize: number,
  color: number,
  outline: boolean,
  outlineColor: number,
  outlineThin: boolean,
  maxWidth: number | undefined,
  wordWrap: boolean,
  ss: number,
): Texture {
  const family = activeWebFontFamily();
  const weight = "400";
  const lines =
    wordWrap && maxWidth
      ? wrapUiText(text, maxWidth, fontSize)
      : text.split("\n");
  const fs = Math.max(1, Math.round(fontSize * ss));
  const pad = (outline ? 3 : 2) * ss;
  const offsets = outline
    ? outlineThin
      ? OUTLINE_4
      : OUTLINE_8
    : [];
  const outlineStep = ss;

  const measure = document.createElement("canvas").getContext("2d")!;
  measure.font = `${weight} ${fs}px ${family}`;
  measure.textBaseline = "alphabetic";

  let textW = 1;
  let ascent = Math.ceil(fs * 0.9);
  let descent = Math.ceil(fs * 0.25);
  for (const line of lines) {
    const m = measure.measureText(line);
    textW = Math.max(textW, Math.ceil(m.width));
    const a = Math.ceil(m.actualBoundingBoxAscent || 0);
    const d = Math.ceil(m.actualBoundingBoxDescent || 0);
    if (a > ascent) ascent = a;
    if (d > descent) descent = d;
  }
  const lineGap = 2 * ss;
  const lineH = ascent + descent + lineGap;
  const w = textW + pad * 2 + (offsets.length ? 2 * ss : 0);
  const h = Math.max(1, lines.length) * lineH - lineGap + pad * 2;

  const canvas = document.createElement("canvas");
  canvas.width = Math.max(1, w);
  canvas.height = Math.max(1, h);
  const ctx = canvas.getContext("2d", { willReadFrequently: true })!;
  ctx.font = `${weight} ${fs}px ${family}`;
  ctx.textBaseline = "alphabetic";
  ctx.imageSmoothingEnabled = true;

  const ox = Math.round(pad + (offsets.length ? ss : 0));
  if (offsets.length) {
    ctx.fillStyle = cssColor(outlineColor);
    for (const [dx, dy] of offsets) {
      for (let i = 0; i < lines.length; i++) {
        const baseY = Math.round(pad + ascent + i * lineH);
        ctx.fillText(
          lines[i]!,
          ox + dx * outlineStep,
          baseY + dy * outlineStep,
        );
      }
    }
  }
  ctx.fillStyle = cssColor(color);
  for (let i = 0; i < lines.length; i++) {
    const baseY = Math.round(pad + ascent + i * lineH);
    ctx.fillText(lines[i]!, ox, baseY);
  }

  const img = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const d = img.data;
  const cr = (color >>> 16) & 0xff;
  const cg = (color >>> 8) & 0xff;
  const cb = color & 0xff;
  for (let i = 0; i < d.length; i += 4) {
    if (d[i + 3]! < 168) {
      d[i] = 0;
      d[i + 1] = 0;
      d[i + 2] = 0;
      d[i + 3] = 0;
    } else {
      d[i] = cr;
      d[i + 1] = cg;
      d[i + 2] = cb;
      d[i + 3] = 255;
    }
  }
  const folded = foldHardNearest(
    d,
    canvas.width,
    canvas.height,
    ss,
    cr,
    cg,
    cb,
    0.5,
    128,
  );
  return rgbaToTexture(folded.w, folded.h, folded.rgba, "nearest");
}

/**
 * System-font bake via canvas → rgbaToTexture (not Texture.from).
 * Hard edge for rare non-CN crisp experiments.
 *
 * Alphabetic baseline + actualBoundingBox* so CJK tops are not clipped.
 */
function bakeCrispSystem(
  text: string,
  fontSize: number,
  color: number,
  outline: boolean,
  outlineColor: number,
  outlineThin: boolean,
  maxWidth: number | undefined,
  wordWrap: boolean,
  ss: number,
  softAa: boolean,
): Texture {
  const lines =
    wordWrap && maxWidth
      ? wrapUiText(text, maxWidth, fontSize)
      : text.split("\n");
  const fs = Math.max(1, Math.round(fontSize * ss));
  const pad = (outline ? 3 : 2) * ss;
  const offsets = outline
    ? outlineThin
      ? OUTLINE_4
      : OUTLINE_8
    : [];
  const outlineStep = ss;
  const family = softAa ? activeWebFontFamily() : uiFontFamily();
  const weight = softAa ? "400" : "500";

  const measure = document.createElement("canvas").getContext("2d")!;
  measure.font = `${weight} ${fs}px ${family}`;
  measure.textBaseline = "alphabetic";

  let textW = 1;
  let ascent = Math.ceil(fs * 0.9);
  let descent = Math.ceil(fs * 0.25);
  for (const line of lines) {
    const m = measure.measureText(line);
    textW = Math.max(textW, Math.ceil(m.width));
    const a = Math.ceil(m.actualBoundingBoxAscent || 0);
    const d = Math.ceil(m.actualBoundingBoxDescent || 0);
    if (a > ascent) ascent = a;
    if (d > descent) descent = d;
  }
  const lineGap = 2 * ss;
  const lineH = ascent + descent + lineGap;
  const w = textW + pad * 2 + (offsets.length ? 2 * ss : 0);
  const h = Math.max(1, lines.length) * lineH - lineGap + pad * 2;

  const canvas = document.createElement("canvas");
  canvas.width = Math.max(1, w);
  canvas.height = Math.max(1, h);
  const ctx = canvas.getContext("2d", { willReadFrequently: true })!;
  ctx.font = `${weight} ${fs}px ${family}`;
  ctx.textBaseline = "alphabetic";
  ctx.imageSmoothingEnabled = softAa;

  const ox = Math.round(pad + (offsets.length ? ss : 0));
  if (offsets.length) {
    ctx.fillStyle = cssColor(outlineColor);
    for (const [dx, dy] of offsets) {
      for (let i = 0; i < lines.length; i++) {
        const baseY = Math.round(pad + ascent + i * lineH);
        ctx.fillText(
          lines[i]!,
          ox + dx * outlineStep,
          baseY + dy * outlineStep,
        );
      }
    }
  }
  ctx.fillStyle = cssColor(color);
  for (let i = 0; i < lines.length; i++) {
    const baseY = Math.round(pad + ascent + i * lineH);
    ctx.fillText(lines[i]!, ox, baseY);
  }

  const img = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const d = img.data;
  const cr = (color >>> 16) & 0xff;
  const cg = (color >>> 8) & 0xff;
  const cb = color & 0xff;
  if (!softAa) {
    const cut = 160;
    for (let i = 0; i < d.length; i += 4) {
      if (d[i + 3]! < cut) {
        d[i] = 0;
        d[i + 1] = 0;
        d[i + 2] = 0;
        d[i + 3] = 0;
      } else {
        d[i] = cr;
        d[i + 1] = cg;
        d[i + 2] = cb;
        d[i + 3] = 255;
      }
    }
  } else {
    // Keep coverage AA; re-tint RGB so outline+fill share the label color.
    for (let i = 0; i < d.length; i += 4) {
      const a = d[i + 3]!;
      if (a < 16) {
        d[i] = 0;
        d[i + 1] = 0;
        d[i + 2] = 0;
        d[i + 3] = 0;
      } else {
        d[i] = cr;
        d[i + 1] = cg;
        d[i + 2] = cb;
      }
    }
  }
  // Soft AA keeps coverage alpha; nearest matches prior Songti LCD path.
  return rgbaToTexture(canvas.width, canvas.height, d, "nearest");
}

/**
 * Dual-mode label: UC3 bitmap, or web face when `preferSystemFont()`.
 * Songti (cn) = DOM overlay SimSun (reference); fusion = 1-bit hard bake.
 */
export class BitmapLabel extends Container {
  private pixiText: Text | null = null;
  private spr: Sprite | null = null;
  private _text = "";
  private _color = 0xffffff;
  private _outline = false;
  private _outlineColor = 0x000000;
  private _outlineThin = false;
  private _smooth = false;
  private _maxWidth: number | undefined;
  /** undefined = wrap when maxWidth set; false = single line. */
  private _wordWrap: boolean | undefined;
  private _fontSize = UI_FONT_SIZE;
  /** System / Songti weight; ignored by UC3. */
  private _bold = false;
  /** Default UC3 bitmap glyphs. */
  private _uc3 = true;
  /** Hard-edge bake when uc3:false. */
  private _crisp = true;
  private _bakeSs = 1;
  private _ax = 0;
  private _ay = 0;
  /** Soft-AA (cn) owns its bake — never put in shared crispTexCache. */
  private _ownedCrispTex: Texture | null = null;
  /** Reference Songti: HTML span over pixelated canvas. */
  private _domEl: HTMLSpanElement | null = null;
  private readonly _domSync = (): void => {
    this.syncDomLabel();
  };
  /** Last UC3 cache key (for eviction — keep live label textures). */
  uc3CacheKey?: string;
  /** Last crisp-bake cache key (same live-protect contract as UC3). */
  crispCacheKey?: string;

  readonly anchor = {
    set: (x: number, y: number): void => {
      this._ax = x;
      this._ay = y;
      this.pixiText?.anchor.set(x, y);
      this.applySpriteAnchor();
      if (this._domEl) {
        const w = this.boundsArea?.width ?? 0;
        const h = this.boundsArea?.height ?? 0;
        this.pivot.set(w * this._ax, h * this._ay);
        this.syncDomLabel();
      }
    },
  };

  constructor() {
    super();
    this.eventMode = "none";
    liveLabels.add(this);
  }

  /** Re-bake with current text/style after font pack / CN Medium switch. */
  async rebake(): Promise<void> {
    if (this.destroyed || !this._text) return;
    // Follow global preference (explicit per-label overrides are rare).
    this._uc3 = !preferSystemFont();
    await this.rerender();
  }

  destroy(options?: Parameters<Container["destroy"]>[0]): void {
    liveLabels.delete(this);
    // Drop live-protect keys first so cache eviction can reclaim unused bakes.
    this.uc3CacheKey = undefined;
    this.crispCacheKey = undefined;
    this.dropOwnedCrisp();
    this.hideDomLabel();
    // Unbind shared bake textures before destroying the Sprite. Pixi v8
    // `destroy()` defaults to children:false — orphaned Sprites kept dead
    // Texture refs in the batcher → alphaMode storm when opening equip detail
    // (many labels rebuilt each redraw).
    if (this.spr && !this.spr.destroyed) {
      try {
        this.spr.texture = Texture.EMPTY;
      } catch {
        /* ignore */
      }
    }
    this.spr = null;
    if (this.pixiText && !this.pixiText.destroyed) {
      try {
        if (this.pixiText.parent) this.removeChild(this.pixiText);
        this.pixiText.destroy(true);
      } catch {
        /* ignore */
      }
    }
    this.pixiText = null;
    super.destroy({
      ...(typeof options === "object" && options ? options : {}),
      children: true,
      texture: false,
      textureSource: false,
    });
  }

  get style(): { fill: number } {
    const self = this;
    return {
      get fill() {
        return self._color;
      },
      set fill(v: number) {
        self.color = v;
      },
    };
  }

  get text(): string {
    return this._text;
  }

  set text(v: string) {
    if (this._text === v) return;
    this._text = v;
    void this.rerender();
  }

  get color(): number {
    return this._color;
  }

  set color(v: number) {
    if (this._color === (v >>> 0)) return;
    this._color = v >>> 0;
    void this.rerender();
  }

  async setText(text: string, color: number, opts?: LabelTextOpts): Promise<void> {
    this._text = text;
    this._color = color >>> 0;
    if (opts) {
      if (opts.outline != null) this._outline = opts.outline;
      if (opts.outlineColor != null) this._outlineColor = opts.outlineColor;
      if (opts.outlineThin != null) this._outlineThin = opts.outlineThin;
      if (opts.smooth != null) this._smooth = opts.smooth;
      if (opts.maxWidth != null) this._maxWidth = opts.maxWidth;
      if (opts.wordWrap != null) this._wordWrap = opts.wordWrap;
      if (opts.fontSize != null) this._fontSize = opts.fontSize;
      if (opts.crisp != null) this._crisp = opts.crisp;
      if (opts.bold != null) this._bold = opts.bold;
    }
    // Explicit uc3 wins; otherwise follow menu preference (cn → OTF).
    if (opts && opts.uc3 !== undefined) {
      this._uc3 = opts.uc3;
    } else {
      this._uc3 = !preferSystemFont();
    }
    await this.rerender();
  }

  show(text: string, color: number, opts?: LabelTextOpts): void {
    void this.setText(text, color, opts);
  }

  private applySpriteAnchor(): void {
    if (this.destroyed) return;
    if (!this.spr) {
      if (!this.pixiText && !this._domEl) this.pivot.set(0, 0);
      return;
    }
    // width/height already include sprite.scale (supersample bake → 1/ss).
    this.pivot.set(this.spr.width * this._ax, this.spr.height * this._ay);
  }

  private hideSystemText(): void {
    if (this.pixiText) {
      this.pixiText.visible = false;
      this.pixiText.text = "";
    }
  }

  private clearBoundsArea(): void {
    // Pixi typings mark boundsArea as Rectangle, but runtime treats any
    // truthy value as authoritative — must delete, not assign empty rect.
    (this as { boundsArea?: Rectangle }).boundsArea = undefined;
  }

  private hideDomLabel(): void {
    unregisterLcdDomSync(this._domSync);
    if (this._domEl) {
      try {
        this._domEl.remove();
      } catch {
        /* ignore */
      }
      this._domEl = null;
    }
    // Must clear — Pixi treats any truthy boundsArea as authoritative and
    // skips child bounds. An empty rect made .width/.height stick at 0, so
    // status badges / nameplates that center via those metrics sat at the
    // bottom-right (floor((slot-0)/2) / -floor(0/2)).
    this.clearBoundsArea();
  }

  private syncDomLabel(): void {
    if (this.destroyed || !this._domEl) return;
    const el = this._domEl;
    // Detached labels (removeChildren without destroy) must not float on screen.
    let show = !!this._text && this.visible && !!this.parent;
    for (let p = this.parent; p && show; p = p.parent) {
      if (!p.visible) show = false;
    }
    // Popup panels paint on the canvas under #lcd-dom-text — hide world/HUD
    // DOM glyphs so they cannot float above SystemPanel / dialog / etc.
    if (show && lcdDomOccludedByPopup(this)) show = false;
    el.style.display = show ? "block" : "none";
    if (!show) return;
    el.style.zIndex = String(lcdDomCssZIndex(this));
    const gp = this.getGlobalPosition();
    const s = lcdDomCssScale();
    const worldScale =
      Math.hypot(this.worldTransform.a, this.worldTransform.b) || 1;
    const fontSize = this.webDisplayFontSize();
    el.style.fontSize = `${fontSize * s * worldScale}px`;
    const w = el.offsetWidth;
    const h = el.offsetHeight;
    el.style.left = `${gp.x * s - w * this._ax}px`;
    el.style.top = `${gp.y * s - h * this._ay}px`;
  }

  private hideSprite(): void {
    if (this.destroyed) return;
    if (this.spr) this.spr.visible = false;
    this.pivot.set(0, 0);
  }

  private showSprite(tex: Texture, ss = 1): void {
    if (this.destroyed) return;
    this._bakeSs = Math.max(1, ss);
    try {
      tex.source.scaleMode = "nearest";
    } catch {
      /* ignore */
    }
    // Sprite path owns metrics — drop any Songti DOM boundsArea leftover.
    this.clearBoundsArea();
    if (!this.spr) {
      this.spr = new Sprite(tex);
      this.addChild(this.spr);
    } else {
      this.spr.texture = tex;
      this.spr.visible = true;
    }
    // Supersampled bake → logical LCD size (nearest, no AA fringe).
    this.spr.scale.set(1 / this._bakeSs);
    this.applySpriteAnchor();
  }

  private async rerender(): Promise<void> {
    if (this.destroyed) return;
    if (!this._text) {
      this.hideSystemText();
      this.hideSprite();
      this.hideDomLabel();
      return;
    }
    if (this._uc3) {
      this.crispCacheKey = undefined;
      this.dropOwnedCrisp();
      this.hideDomLabel();
      this.hideSystemText();
      await this.rerenderUc3();
    } else if (preferSystemFont()) {
      this.uc3CacheKey = undefined;
      this.crispCacheKey = undefined;
      // Reference: DOM SimSun over pixelated canvas (not bake, not auto).
      if (getUc3FontId() === "cn") {
        this.dropOwnedCrisp();
        this.hideSprite();
        this.hideSystemText();
        this.rerenderSongtiDom();
      } else {
        this.hideDomLabel();
        this.hideSystemText();
        this.rerenderWebHard();
      }
    } else if (this._crisp || this._outline) {
      this.hideDomLabel();
      this.uc3CacheKey = undefined;
      this.dropOwnedCrisp();
      this.hideSystemText();
      this.rerenderSystemCrisp();
    } else {
      this.hideDomLabel();
      this.uc3CacheKey = undefined;
      this.crispCacheKey = undefined;
      this.dropOwnedCrisp();
      this.hideSprite();
      this.rerenderSystem();
    }
  }

  private dropOwnedCrisp(): void {
    if (!this._ownedCrispTex) return;
    try {
      this._ownedCrispTex.destroy(true);
    } catch {
      /* ignore */
    }
    this._ownedCrispTex = null;
  }

  /**
   * Web-font display size. Fusion + SimSun + UC3 all use {@link UC3_CELL}=14.
   */
  private webDisplayFontSize(): number {
    if (this._fontSize === UI_FONT_SIZE || this._fontSize === UC3_CELL) {
      return UC3_CELL;
    }
    return this._fontSize;
  }

  /**
   * System Songti — HTML overlay (reference DOM SimSun).
   * Canvas stays `pixelated` so the map does not fog.
   */
  private rerenderSongtiDom(): void {
    const wrap =
      this._wordWrap ?? (this._maxWidth != null && this._maxWidth > 0);
    const shown = wrap ? this.displayText() : this._text;
    const fontSize = this.webDisplayFontSize();
    const tier = lcdDomTierFor(this);
    const layer = ensureLcdDomTextLayer(tier);
    if (!this._domEl) {
      this._domEl = document.createElement("span");
      this._domEl.className = "lcd-dom-label";
      layer.appendChild(this._domEl);
      registerLcdDomSync(this._domSync);
    } else if (this._domEl.parentElement !== layer) {
      layer.appendChild(this._domEl);
    }
    const el = this._domEl;
    // JS already inserted `\n` via wrapUiText — never CSS-rewrap (double wrap
    // shifted breaks by ~3 CJK glyphs and stacked lines on top of each other).
    el.classList.remove("wrap");
    el.style.whiteSpace = "pre";
    el.style.maxWidth = "";
    el.textContent = shown;
    el.style.fontFamily = activeWebFontFamily();
    el.style.fontWeight = this._bold ? "700" : "400";
    el.style.color = cssHexColor(this._color);
    const s = lcdDomCssScale();
    el.style.fontSize = `${fontSize * s}px`;
    if (this._outline) {
      const oc = cssHexColor(this._outlineColor);
      const r = this._outlineThin ? 1 : 1.25;
      el.style.textShadow = [
        `${-r}px 0 ${oc}`,
        `${r}px 0 ${oc}`,
        `0 ${-r}px ${oc}`,
        `0 ${r}px ${oc}`,
      ].join(", ");
    } else {
      el.style.textShadow = "none";
    }

    // Layout bounds in LCD px (no WebGL glyph; nameplates use .width).
    const lines = shown.split("\n");
    let lcdW = 1;
    for (const line of lines) {
      lcdW = Math.max(lcdW, measureUiTextWidth(line, fontSize));
    }
    const lcdH = Math.max(1, Math.round(fontSize * Math.max(1, lines.length)));
    this.boundsArea = new Rectangle(0, 0, lcdW, lcdH);
    this.pivot.set(lcdW * this._ax, lcdH * this._ay);
    this.syncDomLabel();
  }

  /** JHPixel → opaque 1-bit LCD bake (pixel face). */
  private rerenderWebHard(): void {
    const wrap =
      this._wordWrap ?? (this._maxWidth != null && this._maxWidth > 0);
    const shown = wrap ? this.displayText() : this._text;
    const ss = cnHardSupersample();
    this.dropOwnedCrisp();
    const tex = bakeWebFontHard(
      shown,
      this.webDisplayFontSize(),
      this._color,
      this._outline,
      this._outlineColor,
      this._outlineThin,
      this._maxWidth,
      wrap,
      ss,
    );
    this._ownedCrispTex = tex;
    this.showSprite(tex, 1);
  }

  private rerenderSystemCrisp(): void {
    const wrap =
      this._wordWrap ?? (this._maxWidth != null && this._maxWidth > 0);
    const shown = wrap ? this.displayText() : this._text;
    const ss = crispSupersample();
    const fontSize = this._fontSize;
    const key = crispCacheKey(
      shown,
      this._color,
      this._outline,
      this._outlineColor,
      this._outlineThin,
      fontSize,
      this._maxWidth,
      wrap,
      ss,
      false,
    );
    let tex = crispTexCache.get(key);
    if (!tex) {
      tex = bakeCrispSystem(
        shown,
        fontSize,
        this._color,
        this._outline,
        this._outlineColor,
        this._outlineThin,
        this._maxWidth,
        wrap,
        ss,
        false,
      );
      rememberCrispCache(key, tex);
    } else {
      rememberCrispCache(key, tex);
    }
    this.crispCacheKey = key;
    this.showSprite(tex, ss);
  }

  private rerenderSystem(): void {
    const wrap =
      this._wordWrap ?? (this._maxWidth != null && this._maxWidth > 0);
    const shown = wrap ? this.displayText() : this._text;
    const fontSize = this._fontSize;
    const style: Record<string, unknown> = {
      fontFamily: uiFontFamily(),
      fontSize,
      fontWeight: this._bold ? "700" : "400",
      fill: this._color,
      align: "left" as const,
      wordWrap: wrap,
      breakWords: wrap,
      lineHeight: Math.round(fontSize * 1.25),
    };
    if (wrap) style.wordWrapWidth = this._maxWidth;
    if (this._outline) {
      style.stroke = {
        color: this._outlineColor,
        width: this._outlineThin ? 1 : 2,
      };
    }
    const textRes = Math.max(
      UI_TEXT_RESOLUTION,
      Math.min(4, Math.ceil(lcdDisplayScale)),
    );

    if (!this.pixiText) {
      this.pixiText = new Text({
        text: shown,
        style,
        resolution: textRes,
        roundPixels: true,
      });
      this.pixiText.eventMode = "none";
      this.pixiText.anchor.set(this._ax, this._ay);
      this.addChild(this.pixiText);
    } else {
      this.pixiText.visible = true;
      this.pixiText.resolution = textRes;
      this.pixiText.roundPixels = true;
      this.pixiText.style = style;
      this.pixiText.text = shown;
      this.pixiText.anchor.set(this._ax, this._ay);
    }
  }

  private displayText(): string {
    if (this._wordWrap === false || !this._text) return this._text;
    if (this._maxWidth != null && this._maxWidth > 0) {
      return wrapUiText(
        this._text,
        this._maxWidth,
        this.webDisplayFontSize(),
      ).join("\n");
    }
    return formatUiMultiline(this._text);
  }

  private async rerenderUc3(): Promise<void> {
    const shown = this.displayText();
    const key = uc3CacheKey(
      shown,
      this._color,
      this._outline,
      this._outlineColor,
      this._outlineThin,
      this._smooth,
      this._maxWidth,
    );
    let tex = uc3TexCache.get(key);
    if (!tex) {
      const font = await loadUc3Font();
      // Label may be destroyed while the font load / first bake awaits.
      if (this.destroyed) return;
      // maxWidth is in LCD/display px; bake uses native 16px advances.
      const maxNative =
        this._maxWidth != null
          ? Math.ceil(this._maxWidth / UC3_DISPLAY_SCALE)
          : undefined;
      const { width, height, rgba } = renderUc3ToRgba(font, shown, {
        color: this._color,
        outline: this._outline,
        outlineColor: this._outlineColor,
        outlineThin: this._outlineThin,
        smooth: this._smooth,
        maxWidth: maxNative,
      });
      tex = rgbaToTexture(width, height, rgba, this._smooth ? "linear" : "nearest");
    }
    if (this.destroyed) return;
    rememberUc3Cache(key, tex);
    this.uc3CacheKey = key;
    // 16→14: showSprite(ss) sets scale = 1/ss.
    this.showSprite(tex, UC3_NATIVE_CELL / UC3_CELL);
  }
}

/** Colored text run (e.g. CBE cN markup segments). */
export type ColorRun = { text: string; color: number };

/** One label after same-color merge on a cell grid (TipBoot / rich text). */
export type LaidOutRun = {
  text: string;
  color: number;
  /** Column index (0-based) for cellW positioning. */
  col: number;
  /** Row index (0-based). */
  row: number;
};

/**
 * Layout colored segments onto a fixed cell grid, merging adjacent same-color
 * glyphs on the same row into one run (never one Label per character).
 */
export function layoutSameColorRuns(
  segments: readonly ColorRun[],
  opts: {
    maxCols: number;
    /** Stop after this many glyphs (typewriter). Omit = all. */
    visibleChars?: number;
  },
): LaidOutRun[] {
  const maxCols = Math.max(1, opts.maxCols | 0);
  const maxBytes = maxCols * 2;
  let remaining =
    opts.visibleChars == null
      ? Number.POSITIVE_INFINITY
      : Math.max(0, opts.visibleChars | 0);
  let col = 0;
  let lineBytes = 0;
  let row = 0;
  let run = "";
  let runColor = 0;
  let runCol = 0;
  let runRow = 0;
  const out: LaidOutRun[] = [];

  const flush = () => {
    if (!run) return;
    out.push({ text: run, color: runColor, col: runCol, row: runRow });
    run = "";
  };

  for (const seg of segments) {
    if (remaining <= 0) break;
    const chars = [...seg.text];
    const take = Math.min(chars.length, remaining);
    remaining -= take;
    for (let i = 0; i < take; i++) {
      const ch = chars[i]!;
      const bw = uiCharByteWidth(ch);
      if (lineBytes + bw > maxBytes && run) {
        flush();
        col = 0;
        lineBytes = 0;
        row += 1;
      }
      if (!run) {
        runColor = seg.color;
        runCol = col;
        runRow = row;
        run = ch;
      } else if (seg.color === runColor && row === runRow) {
        run += ch;
      } else {
        flush();
        runColor = seg.color;
        runCol = col;
        runRow = row;
        run = ch;
      }
      col += 1;
      lineBytes += bw;
    }
  }
  flush();
  return out;
}

/** GBK-style display bytes: CJK=2, ASCII=1 (matches UC3 advance ratio). */
export function uiCharByteWidth(ch: string): number {
  return (ch.codePointAt(0) ?? 0) > 0xff ? 2 : 1;
}

export function measureUiTextBytes(text: string): number {
  let n = 0;
  for (const ch of text) n += uiCharByteWidth(ch);
  return n;
}

/** Advance width for layout. UC3 cell grid, or canvas measure when CN Medium. */
export function measureUiTextWidth(text: string, cell = UC3_CELL): number {
  if (preferSystemFont() && typeof document !== "undefined") {
    const fs = cell === UI_FONT_SIZE ? UC3_CELL : cell;
    const ctx = document.createElement("canvas").getContext("2d");
    if (ctx) {
      ctx.font = `400 ${fs}px ${activeWebFontFamily()}`;
      let w = 0;
      for (const ch of text) {
        if ((ch.codePointAt(0) ?? 0) > 0xff) w += fs;
        else w += Math.max(1, Math.ceil(ctx.measureText(ch).width));
      }
      return Math.ceil(w);
    }
  }
  const ascii = cell * 0.5;
  let w = 0;
  for (const ch of text) {
    w += (ch.codePointAt(0) ?? 0) > 0xff ? cell : ascii;
  }
  return w;
}

/**
 * Wrap by GBK byte width (CJK=2, ASCII=1). `maxCols` = full-width char budget
 * (default {@link UI_WRAP_CHARS}). Honors existing `\n` paragraphs.
 */
export function wrapChars(
  text: string,
  maxCols = UI_WRAP_CHARS,
): string[] {
  const maxBytes = Math.max(2, (Math.max(1, maxCols | 0)) * 2);
  const lines: string[] = [];
  for (const para of text.replace(/\r\n/g, "\n").replace(/\r/g, "\n").split("\n")) {
    let cur = "";
    let bytes = 0;
    for (const ch of para) {
      const bw = uiCharByteWidth(ch);
      if (bytes + bw > maxBytes && cur) {
        lines.push(cur);
        cur = ch;
        bytes = bw;
      } else {
        cur += ch;
        bytes += bw;
      }
    }
    lines.push(cur || " ");
  }
  return lines.length ? lines : [""];
}

/** Join {@link wrapChars} with `\n` for multiline UC3 labels. */
export function formatUiMultiline(
  text: string,
  maxCols = UI_WRAP_CHARS,
): string {
  return wrapChars(text, maxCols).join("\n");
}

/**
 * Wrap by glyph advances. UC3 uses a fixed CJK/ASCII grid; Songti/web faces
 * measure real advances so chat/panels stay aligned.
 */
export function wrapUiText(
  text: string,
  maxWidth: number,
  fontSize = UC3_CELL,
): string[] {
  const useMeasure =
    preferSystemFont() && typeof document !== "undefined";
  let measure: CanvasRenderingContext2D | null = null;
  if (useMeasure) {
    measure = document.createElement("canvas").getContext("2d");
    if (measure) {
      measure.font = `400 ${fontSize}px ${activeWebFontFamily()}`;
    }
  }
  // Songti canvas metrics run slightly narrow vs DOM SimSun — keep 1px slack
  // so the last glyph does not spill / look like a 2–3 char overlap.
  const budget = Math.max(fontSize, maxWidth - (useMeasure ? 1 : 0));
  const cjk = fontSize;
  const ascii = fontSize * 0.5;
  const charW = (ch: string): number => {
    if (measure) {
      // Prefer full-cell for CJK — measureText(SimSun) often under-reports.
      if ((ch.codePointAt(0) ?? 0) > 0xff) return cjk;
      return Math.max(1, Math.ceil(measure.measureText(ch).width));
    }
    return (ch.codePointAt(0) ?? 0) > 0xff ? cjk : ascii;
  };
  const lines: string[] = [];
  let cur = "";
  let w = 0;
  for (const ch of text) {
    if (ch === "\n") {
      lines.push(cur);
      cur = "";
      w = 0;
      continue;
    }
    const cw = charW(ch);
    if (w + cw > budget && cur) {
      lines.push(cur);
      cur = ch;
      w = cw;
    } else {
      cur += ch;
      w += cw;
    }
  }
  if (cur || !lines.length) lines.push(cur);
  return lines;
}

/** @deprecated Prefer wrapUiText. */
export function wrapUc3Text(
  _font: unknown,
  text: string,
  maxWidth: number,
): string[] {
  return wrapUiText(text, maxWidth, UI_FONT_SIZE);
}
