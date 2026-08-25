import { Container, Graphics, Rectangle, Sprite, Texture } from "pixi.js";
import { CBE, paintCbePanel } from "./constants";
import { tryLoadUiTexture, uiSprite } from "./UiAssets";

/** UI_menu2.gif decoded size. */
export const MENU2_SRC_W = 75;
export const MENU2_SRC_H = 75;
/** Top / bottom roller caps (px in source art). */
export const MENU2_CAP_H = 15;
export const MENU2_MID_H = MENU2_SRC_H - MENU2_CAP_H * 2;
/** Horizontal 3-slice columns in source art (see tmp-menu2-row.txt). */
export const MENU2_LEFT_W = 23;
export const MENU2_CENTER_W = 18;
export const MENU2_RIGHT_W = MENU2_SRC_W - 41;
/** UC3 cell ×4 chars + padding — inner white column target width. */
export const MENU2_WHITE_TARGET_W = 64;

let menuScrollTex: Texture | null = null;
let menuScrollLoad: Promise<Texture | null> | null = null;
const readyCallbacks = new Set<() => void>();
const sliceCache = new Map<string, Texture>();

/** Warm UI_menu2.gif (75×75 scroll frame; stretch middle if height insufficient). */
export function preloadMenuScrollTexture(onReady?: () => void): void {
  if (onReady) readyCallbacks.add(onReady);
  if (menuScrollTex) {
    onReady?.();
    return;
  }
  if (menuScrollLoad) return;
  menuScrollLoad = tryLoadUiTexture("UI_menu2.gif").then((tex) => {
    menuScrollTex = tex;
    if (tex) {
      for (const cb of readyCallbacks) cb();
      readyCallbacks.clear();
    }
    return tex;
  });
}

export function menuScrollTextureReady(): boolean {
  return menuScrollTex != null;
}

export function getMenuScrollTexture(): Texture | null {
  return menuScrollTex;
}

function menu2SliceRect(
  sx: number,
  sy: number,
  sw: number,
  sh: number,
): Texture | null {
  if (!menuScrollTex) return null;
  const key = `r${sx},${sy},${sw},${sh}`;
  const hit = sliceCache.get(key);
  if (hit) return hit;
  const tex = new Texture({
    source: menuScrollTex.source,
    frame: new Rectangle(sx, sy, sw, sh),
  });
  tex.source.scaleMode = "nearest";
  sliceCache.set(key, tex);
  return tex;
}

function menu2Slice(y: number, h: number): Texture | null {
  return menu2SliceRect(0, y, MENU2_SRC_W, h);
}

/** Fixed scroll height for `visibleRows` menu lines (caps + padding). */
export function menu2ScrollFixedHeight(
  visibleRows: number,
  rowH: number,
): number {
  return MENU2_CAP_H * 2 + 6 + visibleRows * rowH + 4;
}

/** Total display width when the inner white column is `whiteW` px. */
export function menu2ScrollWidthForWhite(whiteW: number): number {
  return MENU2_LEFT_W + whiteW + MENU2_RIGHT_W;
}

/** Inner white text column after horizontal 3-slice layout. */
export function menu2WhiteRect(
  scrollX: number,
  scrollW: number,
): { x: number; w: number; cx: number } {
  const w = scrollW - MENU2_LEFT_W - MENU2_RIGHT_W;
  const x = scrollX + MENU2_LEFT_W;
  return { x, w, cx: x + w / 2 };
}

/** Horizontally center the white text column on the LCD. */
export function menu2CenteredLayout(
  lcdWidth: number,
  whiteW: number,
): { scrollX: number; scrollW: number; white: { x: number; w: number; cx: number } } {
  const scrollW = menu2ScrollWidthForWhite(whiteW);
  const whiteX = Math.round(lcdWidth / 2 - whiteW / 2);
  const scrollX = whiteX - MENU2_LEFT_W;
  const white = { x: whiteX, w: whiteW, cx: whiteX + whiteW / 2 };
  return { scrollX, scrollW, white };
}

/** Full inner parchment in source (green margins + white center). */
const MENU2_BODY_SRC_L = 11;
const MENU2_BODY_SRC_R = 52;

function menu2MapSrcX(scrollX: number, whiteW: number, srcX: number): number {
  if (srcX <= MENU2_LEFT_W - 1) {
    return scrollX + (srcX / Math.max(1, MENU2_LEFT_W - 1)) * MENU2_LEFT_W;
  }
  if (srcX <= MENU2_LEFT_W + MENU2_CENTER_W - 1) {
    const t = (srcX - MENU2_LEFT_W) / MENU2_CENTER_W;
    return scrollX + MENU2_LEFT_W + t * whiteW;
  }
  const rel = srcX - (MENU2_LEFT_W + MENU2_CENTER_W);
  const t = rel / Math.max(1, MENU2_RIGHT_W - 1);
  return scrollX + MENU2_LEFT_W + whiteW + t * MENU2_RIGHT_W;
}

/** White fill + menu rows — same width, aligned to scroll inner parchment. */
export function menu2ContentRect(
  scrollX: number,
  whiteW: number,
): { x: number; w: number; cx: number } {
  const x0 = menu2MapSrcX(scrollX, whiteW, MENU2_BODY_SRC_L);
  const x1 = menu2MapSrcX(scrollX, whiteW, MENU2_BODY_SRC_R);
  const w = Math.max(1, Math.round(x1 - x0 + 1));
  const x = Math.round(x0);
  return { x, w, cx: x + w / 2 };
}

/** Parchment fill sampled from UI_menu2 center column. */
export const MENU2_WHITE_FILL = 0xcecebd;

function addMenuScrollHBand(
  host: Container,
  x: number,
  y: number,
  bandY: number,
  bandSrcH: number,
  whiteW: number,
  displayH: number,
): void {
  const leftTex = menu2SliceRect(0, bandY, MENU2_LEFT_W, bandSrcH);
  const midTex = menu2SliceRect(MENU2_LEFT_W, bandY, MENU2_CENTER_W, bandSrcH);
  const rightTex = menu2SliceRect(
    MENU2_SRC_W - MENU2_RIGHT_W,
    bandY,
    MENU2_RIGHT_W,
    bandSrcH,
  );
  if (!leftTex || !midTex || !rightTex) return;

  const left = uiSprite(leftTex, MENU2_LEFT_W, displayH);
  left.eventMode = "none";
  left.position.set(x, y);
  host.addChild(left);

  const mid = uiSprite(midTex, whiteW, displayH);
  mid.eventMode = "none";
  mid.position.set(x + MENU2_LEFT_W, y);
  host.addChild(mid);

  const right = uiSprite(rightTex, MENU2_RIGHT_W, displayH);
  right.eventMode = "none";
  right.position.set(x + MENU2_LEFT_W + whiteW, y);
  host.addChild(right);
}

/**
 * Vertical + horizontal 3-slice: roller caps fixed, white column stretched to
 * `whiteW` so menu text stays inside the parchment box.
 */
export function addMenuScrollBackgroundSliced(
  host: Container,
  x: number,
  y: number,
  h: number,
  whiteW = MENU2_WHITE_TARGET_W,
): boolean {
  preloadMenuScrollTexture();
  if (!menuScrollTex || h < MENU2_CAP_H * 2 + 2) return false;

  const midH = h - MENU2_CAP_H * 2;
  addMenuScrollHBand(host, x, y, 0, MENU2_CAP_H, whiteW, MENU2_CAP_H);
  addMenuScrollHBand(
    host,
    x,
    y + MENU2_CAP_H,
    MENU2_CAP_H,
    MENU2_MID_H,
    whiteW,
    midH,
  );
  addMenuScrollHBand(
    host,
    x,
    y + MENU2_CAP_H + midH,
    MENU2_SRC_H - MENU2_CAP_H,
    MENU2_CAP_H,
    whiteW,
    MENU2_CAP_H,
  );
  return true;
}

/** Stretched scroll frame; null when asset not loaded yet. */
export function menuScrollSprite(w: number, h: number): Sprite | null {
  if (!menuScrollTex) return null;
  const s = uiSprite(menuScrollTex, w, h);
  s.eventMode = "none";
  return s;
}

/** Battle action menu parchment fallback (Graphics). */
export function menuScrollParchmentFallback(
  x: number,
  y: number,
  w: number,
  h: number,
): Graphics {
  const panel = new Graphics();
  panel.eventMode = "none";
  panel
    .roundRect(x, y, w, h, 2)
    .fill({ color: 0xc8c4a0 })
    .stroke({ width: 2, color: 0x3a6658 });
  panel.roundRect(x - 4, y - 6, w + 8, 10, 3).fill({ color: 0x4a7a6a });
  panel.roundRect(x - 4, y + h - 4, w + 8, 10, 3).fill({ color: 0x4a7a6a });
  return panel;
}

/** World menu panel fallback — same chrome as drawCbePanel fill. */
export function menuScrollCbeFallback(
  host: Container,
  x: number,
  y: number,
  w: number,
  h: number,
  fill = CBE.bgPanel,
): Graphics {
  const g = new Graphics();
  g.eventMode = "none";
  paintCbePanel(host, g, x, y, w, h, fill);
  return g;
}

/** Append UI_menu2 (3-slice) or full stretch / fallback panel background. */
export function addMenuScrollBackground(
  host: Container,
  x: number,
  y: number,
  w: number,
  h: number,
  fallback: "parchment" | "cbe" = "parchment",
): void {
  preloadMenuScrollTexture();
  const whiteW = Math.max(
    MENU2_CENTER_W,
    w - MENU2_LEFT_W - MENU2_RIGHT_W,
  );
  if (addMenuScrollBackgroundSliced(host, x, y, h, whiteW)) return;
  const spr = menuScrollSprite(w, h);
  if (spr) {
    spr.position.set(x, y);
    host.addChild(spr);
    return;
  }
  host.addChild(
    fallback === "cbe"
      ? menuScrollCbeFallback(host, x, y, w, h)
      : menuScrollParchmentFallback(x, y, w, h),
  );
}
