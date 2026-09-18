import { Container, Graphics, Sprite, Texture } from "pixi.js";
import { CBE, drawCbePanel, drawCbePanelVectorCorners } from "./constants";
import { loadUiCrop, tryLoadUiTexture, uiSprite } from "./UiAssets";

/** flowerStyle.gif — 70×38, four 35×19 corner tiles (TL TR BL BR). */
export const FLOWER_CORNER_W = 35;
export const FLOWER_CORNER_H = 19;

const FLOWER_QUADS = [
  { quad: 0, sx: 0, sy: 0 },
  { quad: 1, sx: 35, sy: 0 },
  { quad: 2, sx: 0, sy: 19 },
  { quad: 3, sx: 35, sy: 19 },
] as const;

const cornerTex = new Map<number, Texture>();
const hostCorners = new WeakMap<Container, Sprite[]>();
const readyCallbacks = new Set<() => void>();
let flowerReady = false;
let flowerLoad: Promise<boolean> | null = null;

/** Warm flowerStyle.gif corner crops (JHOnlineData UI chrome). */
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
  flowerReady = false;
  flowerLoad = null;
  // Re-warm so the next paintCbePanel can use GIF corners again.
  preloadCbeFlowerCorners();
}

/**
 * Place four flowerStyle corner sprites on a CBE panel.
 * Sprites are parented under `host` (not `box`).
 */
export function addCbeFlowerCorners(
  host: Container,
  x: number,
  y: number,
  w: number,
  h: number,
): boolean {
  clearCbeFlowerCorners(host);
  if (!flowerReady || cornerTex.size < 4) return false;
  const spots = [
    { quad: 0, px: x, py: y },
    { quad: 1, px: x + w - FLOWER_CORNER_W, py: y },
    { quad: 2, px: x, py: y + h - FLOWER_CORNER_H },
    { quad: 3, px: x + w - FLOWER_CORNER_W, py: y + h - FLOWER_CORNER_H },
  ];
  const sprites: Sprite[] = [];
  for (const spot of spots) {
    const tex = cornerTex.get(spot.quad);
    if (!tex) {
      clearCbeFlowerCorners(host);
      return false;
    }
    const spr = uiSprite(tex);
    spr.eventMode = "none";
    spr.position.set(spot.px, spot.py);
    host.addChild(spr);
    sprites.push(spr);
  }
  hostCorners.set(host, sprites);
  return true;
}

/**
 * Standard CBE panel chrome: fill + border + flowerStyle corners
 * (vector corners when the GIF is not loaded yet).
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
  drawCbePanel(box, x, y, w, h, fill, false);
  if (!addCbeFlowerCorners(host, x, y, w, h)) {
    drawCbePanelVectorCorners(box, x, y, w, h);
  }
}
