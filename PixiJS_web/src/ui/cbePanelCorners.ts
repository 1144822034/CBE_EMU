import { Container, Graphics, Sprite, Texture } from "pixi.js";
import {
  CBE,
  CBE_TITLE_H,
  LCD_WIDTH,
  drawCbePanel,
  drawCbePanelVectorCorners,
  drawCbeTitlePill,
  drawCbeTitleSideVectorFlowers,
  layoutCbeTitledChrome,
  type CbeTitledChrome,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { loadUiCrop, tryLoadUiTexture, uiSprite } from "./UiAssets";

/** Horizontal inset so capsule end-caps read against the LCD edge. */
const LOC_BANNER_INSET = 4;
/** Dedicated flower hosts so panel `clearCbeFlowerCorners` on a sibling won't wipe the title. */
const locBannerFlowerHosts = new WeakMap<Container, Container>();

function ensureLocBannerFlowerHost(labelHost: Container): Container {
  let h = locBannerFlowerHosts.get(labelHost);
  if (!h || h.destroyed || h.parent !== labelHost) {
    h = new Container();
    h.eventMode = "none";
    labelHost.addChild(h);
    locBannerFlowerHosts.set(labelHost, h);
  } else {
    clearCbeFlowerCorners(h);
  }
  return h;
}

/**
 * Capsule title chrome for map / login banners (same pill + 边花 as 背包).
 * `flowerHost` should be a dedicated container (not the panel body host).
 */
export function paintLocTitleChrome(
  g: Graphics,
  flowerHost: Container,
  y = 2,
): { titleX: number; titleY: number; titleW: number; titleH: number; titleCy: number } {
  const titleH = CBE_TITLE_H;
  const titleW = LCD_WIDTH - LOC_BANNER_INSET * 2;
  const titleX = Math.floor((LCD_WIDTH - titleW) / 2);
  const titleY = y;
  drawCbeTitlePill(g, titleX + titleW / 2, titleY, titleW, titleH);
  if (!addCbeTitleSideFlowers(flowerHost, titleX, titleY, titleW, titleH)) {
    drawCbeTitleSideVectorFlowers(g, titleX, titleY, titleW, titleH);
  }
  return {
    titleX,
    titleY,
    titleW,
    titleH,
    titleCy: titleY + titleH / 2,
  };
}

/**
 * Floating capsule title strip (login / version / zone / role / map name).
 * Side flowers live on a child host so body `paintCbePanel` clear won't erase them.
 */
export function drawLocBanner(
  g: Graphics,
  labelHost: Container,
  text: string,
  y = 2,
): BitmapLabel {
  const flowerHost = ensureLocBannerFlowerHost(labelHost);
  const { titleCy } = paintLocTitleChrome(g, flowerHost, y);
  const t = new BitmapLabel();
  t.show(text, CBE.ink, { outline: false, smooth: false });
  t.anchor.set(0.5, 0.5);
  t.position.set(Math.round(LCD_WIDTH / 2), Math.round(titleCy));
  labelHost.addChild(t);
  return t;
}

/** flowerStyle.gif — 70×38, four 35×19 corner tiles (TL TR BL BR). */
export const FLOWER_CORNER_W = 35;
export const FLOWER_CORNER_H = 19;

/** Full sheet height — left/right halves are 35×38. */
export const FLOWER_SHEET_H = 38;

/** Body panel corner scale (native 35×19 → ~19×10). */
export const BODY_FLOWER_SCALE = 0.55;

const FLOWER_QUADS = [
  { quad: 0, sx: 0, sy: 0 },
  { quad: 1, sx: 35, sy: 0 },
  { quad: 2, sx: 0, sy: 19 },
  { quad: 3, sx: 35, sy: 19 },
] as const;

const cornerTex = new Map<number, Texture>();
/** Left / right halves of flowerStyle.gif (35×38 each). */
let flowerHalfL: Texture | null = null;
let flowerHalfR: Texture | null = null;
const hostCorners = new WeakMap<Container, Sprite[]>();
const readyCallbacks = new Set<() => void>();
let flowerReady = false;
let flowerLoad: Promise<boolean> | null = null;

/** Warm flowerStyle.gif corner crops + L/R halves (JHOnlineData UI chrome). */
export function preloadCbeFlowerCorners(onReady?: () => void): void {
  if (onReady) readyCallbacks.add(onReady);
  if (flowerReady) {
    onReady?.();
    return;
  }
  if (flowerLoad) return;
  flowerLoad = (async () => {
    const sheet = await tryLoadUiTexture("flowerStyle.gif");
    if (!sheet) return false;
    for (const q of FLOWER_QUADS) {
      const tex = await loadUiCrop(
        "flowerStyle.gif",
        q.sx,
        q.sy,
        FLOWER_CORNER_W,
        FLOWER_CORNER_H,
      );
      if (!tex) return false;
      cornerTex.set(q.quad, tex);
    }
    // Title 边花: whole sheet split into left / right halves.
    flowerHalfL = await loadUiCrop(
      "flowerStyle.gif",
      0,
      0,
      FLOWER_CORNER_W,
      FLOWER_SHEET_H,
    );
    flowerHalfR = await loadUiCrop(
      "flowerStyle.gif",
      FLOWER_CORNER_W,
      0,
      FLOWER_CORNER_W,
      FLOWER_SHEET_H,
    );
    if (!flowerHalfL || !flowerHalfR) return false;
    flowerReady = true;
    for (const cb of readyCallbacks) cb();
    readyCallbacks.clear();
    return true;
  })().catch(() => false);
}

export function cbeFlowerCornersReady(): boolean {
  return flowerReady;
}

/** Remove flower corner sprites previously attached to `host`. */
export function clearCbeFlowerCorners(host: Container): void {
  const sprites = hostCorners.get(host);
  if (!sprites) return;
  for (const s of sprites) {
    if (s.parent) s.parent.removeChild(s);
    s.destroy();
  }
  hostCorners.delete(host);
}

/**
 * Drop flower corner Texture handles after {@link invalidateUiGpuTextures}.
 * The crop textures are already destroyed; keeping `flowerReady` would remount
 * dead sources onto open SystemPanel → alphaMode storm.
 */
export function invalidateCbeFlowerCornerGpu(): void {
  cornerTex.clear();
  flowerHalfL = null;
  flowerHalfR = null;
  flowerReady = false;
  flowerLoad = null;
  // Re-warm so the next paintCbePanel can use GIF corners again.
  preloadCbeFlowerCorners();
}

function appendCornerSprites(host: Container, sprites: Sprite[]): void {
  const prev = hostCorners.get(host) ?? [];
  hostCorners.set(host, prev.concat(sprites));
}

/**
 * Place four flowerStyle corner sprites on a CBE panel body.
 * Sprites are parented under `host` (not `box`).
 */
export function addCbeFlowerCorners(
  host: Container,
  x: number,
  y: number,
  w: number,
  h: number,
  opts?: { scale?: number; clear?: boolean },
): boolean {
  if (opts?.clear !== false) clearCbeFlowerCorners(host);
  if (!flowerReady || cornerTex.size < 4) return false;
  const scale = opts?.scale ?? BODY_FLOWER_SCALE;
  const dw = Math.max(1, Math.round(FLOWER_CORNER_W * scale));
  const dh = Math.max(1, Math.round(FLOWER_CORNER_H * scale));
  const spots = [
    { quad: 0, px: x, py: y },
    { quad: 1, px: x + w - dw, py: y },
    { quad: 2, px: x, py: y + h - dh },
    { quad: 3, px: x + w - dw, py: y + h - dh },
  ];
  const sprites: Sprite[] = [];
  for (const spot of spots) {
    const tex = cornerTex.get(spot.quad);
    if (!tex) {
      for (const s of sprites) {
        if (s.parent) s.parent.removeChild(s);
        s.destroy();
      }
      return false;
    }
    const spr = uiSprite(tex);
    spr.eventMode = "none";
    spr.scale.set(scale);
    spr.position.set(spot.px, spot.py);
    host.addChild(spr);
    sprites.push(spr);
  }
  appendCornerSprites(host, sprites);
  return true;
}

/**
 * Title-capsule 边花: flowerStyle.gif cropped into left / right halves (35×38),
 * scaled to title height.
 */
export function addCbeTitleSideFlowers(
  host: Container,
  titleX: number,
  titleY: number,
  titleW: number,
  titleH: number,
): boolean {
  if (!flowerReady || !flowerHalfL || !flowerHalfR) return false;
  if (flowerHalfL.destroyed || flowerHalfR.destroyed) return false;
  const scale = titleH / FLOWER_SHEET_H;
  const dw = Math.max(1, Math.round(FLOWER_CORNER_W * scale));
  const dh = titleH;
  const sprites: Sprite[] = [];

  const left = uiSprite(flowerHalfL);
  left.eventMode = "none";
  left.width = dw;
  left.height = dh;
  left.position.set(titleX, titleY);
  host.addChild(left);
  sprites.push(left);

  const right = uiSprite(flowerHalfR);
  right.eventMode = "none";
  right.width = dw;
  right.height = dh;
  right.position.set(titleX + titleW - dw, titleY);
  host.addChild(right);
  sprites.push(right);

  appendCornerSprites(host, sprites);
  return true;
}

/**
 * Standard CBE panel chrome: fill + border + flowerStyle corners
 * (vector corners when the GIF is not loaded yet).
 * Always clears `box` first — tall→short (仓库列表→主界面) must not leave
 * the previous larger fill stacked under the new frame.
 */
export function paintCbePanel(
  host: Container,
  box: Graphics,
  x: number,
  y: number,
  w: number,
  h: number,
  fill = CBE.bgPanel,
): void {
  clearCbeFlowerCorners(host);
  box.clear();
  drawCbePanel(box, x, y, w, h, fill, false);
  if (!addCbeFlowerCorners(host, x, y, w, h, { scale: BODY_FLOWER_SCALE })) {
    drawCbePanelVectorCorners(box, x, y, w, h, 9);
  }
}

/**
 * Reference chrome: floating title capsule ABOVE a separate body panel.
 * Title 边花 = flowerStyle L/R halves; body = smaller corner tiles.
 */
export function paintCbeTitledChrome(
  host: Container,
  box: Graphics,
  x: number,
  y: number,
  w: number,
  h: number,
  fill = CBE.bgPanel,
  opts?: { titleH?: number; gap?: number; titleW?: number },
): CbeTitledChrome {
  const layout = layoutCbeTitledChrome(x, y, w, h, opts);
  clearCbeFlowerCorners(host);
  box.clear();
  drawCbeTitlePill(
    box,
    layout.titleX + layout.titleW / 2,
    layout.titleY,
    layout.titleW,
    layout.titleH,
  );
  drawCbePanel(
    box,
    layout.bodyX,
    layout.bodyY,
    layout.bodyW,
    layout.bodyH,
    fill,
    false,
  );

  const bodyOk = addCbeFlowerCorners(
    host,
    layout.bodyX,
    layout.bodyY,
    layout.bodyW,
    layout.bodyH,
    { scale: BODY_FLOWER_SCALE, clear: false },
  );
  if (!bodyOk) {
    drawCbePanelVectorCorners(
      box,
      layout.bodyX,
      layout.bodyY,
      layout.bodyW,
      layout.bodyH,
      9,
    );
  }

  if (
    !addCbeTitleSideFlowers(
      host,
      layout.titleX,
      layout.titleY,
      layout.titleW,
      layout.titleH,
    )
  ) {
    drawCbeTitleSideVectorFlowers(
      box,
      layout.titleX,
      layout.titleY,
      layout.titleW,
      layout.titleH,
    );
  }
  return layout;
}
