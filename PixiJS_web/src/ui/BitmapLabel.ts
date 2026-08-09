import { Container, Sprite, Text, Texture } from "pixi.js";
import {
  loadUc3Font,
  renderUc3ToRgba,
  UC3_FONT_URL,
} from "../resources/uc3Font";
import { rgbaToTexture } from "../render/rgbaTexture";

/**
 * Fallback system stack when `{ uc3: false }` (nameplate experiments only).
 * LCD UI defaults to `font_gb.uc3` — Pixi has no special “mobile font”;
 * HTML softkeys stay DOM-sharp outside the pixelated canvas.
 */
export const UI_FONT_FAMILY =
  'SimSun, "Songti SC", "Microsoft YaHei", "PingFang SC", "Noto Sans SC", sans-serif';

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

function uc3CacheKey(
  text: string,
  color: number,
  outline: boolean,
  outlineColor: number,
  outlineThin: boolean,
  smooth: boolean,
  maxWidth?: number,
): string {
  return `${UC3_FONT_URL}\0${UC3_CELL}\0${text}\0${color.toString(16)}\0${outline ? 1 : 0}\0${outlineColor.toString(16)}\0${outlineThin ? 1 : 0}\0${smooth ? 1 : 0}\0${maxWidth ?? ""}`;
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
): string {
  return `${text}\0${color.toString(16)}\0${outline ? 1 : 0}\0${outlineColor.toString(16)}\0${outlineThin ? 1 : 0}\0${fontSize}\0${maxWidth ?? ""}\0${wordWrap ? 1 : 0}\0${ss}`;
}

function crispSupersample(): number {
  return Math.max(1, Math.min(3, Math.round(lcdDisplayScale)));
}

function cssColor(rgb: number): string {
  return `#${(rgb >>> 0).toString(16).padStart(6, "0")}`;
}

/**
 * Hard-edge bake at supersample × lcdDisplayScale, then displayed at 1/ss.
 * Optional neighbor outline for nameplates. Binary alpha + nearest avoids the
 * bold/ghost look of AA Text under CSS `image-rendering: pixelated`.
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
  // Outline offsets stay in baked px; scale with supersample.
  const outlineStep = ss;

  const measure = document.createElement("canvas").getContext("2d")!;
  measure.font = `400 ${fs}px ${UI_FONT_FAMILY}`;
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
  const ctx = canvas.getContext("2d")!;
  ctx.font = `400 ${fs}px ${UI_FONT_FAMILY}`;
  ctx.textBaseline = "alphabetic";
  ctx.imageSmoothingEnabled = false;

  const ox = pad + (offsets.length ? ss : 0);
  if (offsets.length) {
    ctx.fillStyle = cssColor(outlineColor);
    for (const [dx, dy] of offsets) {
      for (let i = 0; i < lines.length; i++) {
        const baseY = pad + ascent + i * lineH;
        ctx.fillText(lines[i]!, ox + dx * outlineStep, baseY + dy * outlineStep);
      }
    }
  }
  ctx.fillStyle = cssColor(color);
  for (let i = 0; i < lines.length; i++) {
    const baseY = pad + ascent + i * lineH;
    ctx.fillText(lines[i]!, ox, baseY);
  }

  const img = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const d = img.data;
  // Mid threshold: low values fatten AA fringes into "bold/ghost" strokes.
  for (let i = 3; i < d.length; i += 4) {
    d[i] = d[i]! < 140 ? 0 : 255;
  }
  ctx.putImageData(img, 0, 0);

  const tex = Texture.from(canvas);
  tex.source.scaleMode = "nearest";
  return tex;
}

/**
 * Dual-mode label: system font (default) or UC3 bitmap (`uc3: true` for titles).
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
  /** Default: CBE font_gb.uc3 hard pixels (readable on mobile LCD upscale). */
  private _uc3 = true;
  /** Hard-edge bake when uc3:false. */
  private _crisp = true;
  private _bakeSs = 1;
  private _ax = 0;
  private _ay = 0;

  readonly anchor = {
    set: (x: number, y: number): void => {
      this._ax = x;
      this._ay = y;
      this.pixiText?.anchor.set(x, y);
      this.applySpriteAnchor();
    },
  };

  constructor() {
    super();
    this.eventMode = "none";
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
      if (opts.uc3 != null) this._uc3 = opts.uc3;
      if (opts.crisp != null) this._crisp = opts.crisp;
    }
    await this.rerender();
  }

  show(text: string, color: number, opts?: LabelTextOpts): void {
    void this.setText(text, color, opts);
  }

  private applySpriteAnchor(): void {
    if (!this.spr) {
      if (!this.pixiText) this.pivot.set(0, 0);
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

  private hideSprite(): void {
    if (this.spr) this.spr.visible = false;
    this.pivot.set(0, 0);
  }

  private showSprite(tex: Texture, ss = 1): void {
    this._bakeSs = Math.max(1, ss);
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
    if (!this._text) {
      this.hideSystemText();
      this.hideSprite();
      return;
    }
    if (this._uc3) {
      this.hideSystemText();
      await this.rerenderUc3();
    } else if (this._crisp || this._outline) {
      // Default UI + nameplates: hard bake (pixelated canvas safe).
      this.hideSystemText();
      this.rerenderSystemCrisp();
    } else {
      this.hideSprite();
      this.rerenderSystem();
    }
  }

  private rerenderSystemCrisp(): void {
    const wrap =
      this._wordWrap ?? (this._maxWidth != null && this._maxWidth > 0);
    const ss = crispSupersample();
    const key = crispCacheKey(
      this._text,
      this._color,
      this._outline,
      this._outlineColor,
      this._outlineThin,
      this._fontSize,
      this._maxWidth,
      wrap,
      ss,
    );
    let tex = crispTexCache.get(key);
    if (!tex) {
      tex = bakeCrispSystem(
        this._text,
        this._fontSize,
        this._color,
        this._outline,
        this._outlineColor,
        this._outlineThin,
        this._maxWidth,
        wrap,
        ss,
      );
      crispTexCache.set(key, tex);
    }
    this.showSprite(tex, ss);
  }

  private rerenderSystem(): void {
    const wrap =
      this._wordWrap ?? (this._maxWidth != null && this._maxWidth > 0);
    const style: Record<string, unknown> = {
      fontFamily: UI_FONT_FAMILY,
      fontSize: this._fontSize,
      fill: this._color,
      align: "left" as const,
      wordWrap: wrap,
      breakWords: wrap,
      lineHeight: Math.round(this._fontSize * 1.25),
    };
    if (wrap) style.wordWrapWidth = this._maxWidth;
    // Rasterize near CSS/device pixels so upscale isn't nearest-neighbor mush.
    const textRes = Math.max(
      UI_TEXT_RESOLUTION,
      Math.min(4, Math.ceil(lcdDisplayScale + 0.001)),
    );

    if (!this.pixiText) {
      this.pixiText = new Text({
        text: this._text,
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
      this.pixiText.text = this._text;
      this.pixiText.anchor.set(this._ax, this._ay);
    }
  }

  private async rerenderUc3(): Promise<void> {
    const key = uc3CacheKey(
      this._text,
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
      // maxWidth is in LCD/display px; bake uses native 16px advances.
      const maxNative =
        this._maxWidth != null
          ? Math.ceil(this._maxWidth / UC3_DISPLAY_SCALE)
          : undefined;
      const { width, height, rgba } = renderUc3ToRgba(font, this._text, {
        color: this._color,
        outline: this._outline,
        outlineColor: this._outlineColor,
        outlineThin: this._outlineThin,
        smooth: this._smooth,
        maxWidth: maxNative,
      });
      tex = rgbaToTexture(width, height, rgba, this._smooth ? "linear" : "nearest");
      uc3TexCache.set(key, tex);
    }
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
  let remaining =
    opts.visibleChars == null
      ? Number.POSITIVE_INFINITY
      : Math.max(0, opts.visibleChars | 0);
  let col = 0;
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
      if (col + 1 > maxCols) {
        flush();
        col = 0;
        row += 1;
      }
      const ch = chars[i]!;
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
    }
  }
  flush();
  return out;
}

/** Advance width for default UC3 layout (CJK=16, ASCII=8). */
export function measureUiTextWidth(text: string, cell = UC3_CELL): number {
  const ascii = cell * 0.5;
  let w = 0;
  for (const ch of text) {
    w += (ch.codePointAt(0) ?? 0) > 0xff ? cell : ascii;
  }
  return w;
}

/**
 * Wrap by glyph advances. Default matches font_gb.uc3 (CJK=16, ASCII=8).
 * Pass a smaller fontSize only for system-font layouts.
 */
export function wrapUiText(
  text: string,
  maxWidth: number,
  fontSize = UC3_CELL,
): string[] {
  const cjk = fontSize;
  const ascii = fontSize * 0.5;
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
    const cw = (ch.codePointAt(0) ?? 0) > 0xff ? cjk : ascii;
    if (w + cw > maxWidth && cur) {
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
