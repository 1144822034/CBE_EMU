import { Container, Graphics, Rectangle } from "pixi.js";
import { BitmapLabel } from "./BitmapLabel";

/** Palette sampled from live CBE screenshots (PixiJS_web/reference-shots/*-game.png). */
export const LCD_WIDTH = 240;
export const LCD_HEIGHT = 400;

/**
 * Map / battle unit sprite scale vs native `.actor` pixels.
 * Native frames (~20–40px) read large on the 240×400 LCD; tune vs CBE screenshots.
 */
export const ACTOR_DISPLAY_SCALE = 0.8;

/** Compensate hi-res actor sheets (`ComposedFrame.artScale`). */
export function actorSpriteScale(
  artScale = 1,
  flipX = false,
): { x: number; y: number } {
  const s = ACTOR_DISPLAY_SCALE / Math.max(1, artScale);
  return { x: flipX ? -s : s, y: s };
}

/** Over-head nameplates track actor scale (map + battle unit names). */
export const NAMEPLATE_SCALE = ACTOR_DISPLAY_SCALE;

/**
 * Panel chrome sampled from bag / item-detail / equip refs (title + body):
 *   body #152421 · title #203c38 · border #548e66 / #5c9874 · ornament #488470
 * bgDeep stays near-black for HUD / softkey / full-screen washes.
 */
export const CBE = {
  bgDeep: 0x0e1b18,
  /** Main panel / content well fill (ref 背包·详情·装备 body). */
  bgPanel: 0x152421,
  bgPanelDark: 0x2a4840,
  bgHudRow: 0x4a6e62,
  border: 0x548e66,
  borderBright: 0x5c9874,
  borderDim: 0x487460,
  /** Outer frame hairline (ref panel edge). */
  borderOuter: 0x183418,
  select: 0x2f5248,
  ink: 0xffffff,
  inkMint: 0xd0e8d5,
  inkLabel: 0xd5dba5,
  /** Item-use / chest / tip toast (CBE tip green). */
  inkTipGreen: 0x00ff00,
  inkTip: 0x00ffff,
  inkBody: 0xd5dba5,
  inkWarn: 0xffcc66,
  inkChat: 0xf8dc00,
  gold: 0xd4c489,
  cream: 0xfdf8c2,
  titlePill: 0x203c38,
  softkeyBorder: 0x7aa090,
  chatBg: 0x081810,
  hp: 0xe05040,
  sp: 0x50b0e0,
  exp: 0xb050c8,
  menuHi: 0xff9944,
  menuHiStroke: 0x802020,
  menuNormStroke: 0x102040,
};

/** Softkey / bottom chrome height on list screens. */
export const SOFTKEY_H = 32;

const panelPtrBlocked = new WeakSet<Container>();

/**
 * Full-screen panel tap shield: softkey bar stays clickable; map / HUD under
 * the panel (挂机, status badges, quest tracker) must not receive taps.
 */
export function blockPanelPointerPassthrough(host: Container): void {
  host.eventMode = "static";
  host.hitArea = new Rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
  if (panelPtrBlocked.has(host)) return;
  panelPtrBlocked.add(host);
  const stop = (e: { stopPropagation: () => void }) => {
    e.stopPropagation();
  };
  host.on("pointerdown", stop);
  host.on("pointertap", stop);
}

/**
 * In-world bands measured from CBE reference-shots/*-game.png (240×400):
 *   top 0–71 = location + olive status + uiexp/exp track
 *   map 72–341
 *   bottom 342–399 = soft (22) + chat (36)
 * See docs/re/2026-08-06-pixijs-world-hud-layout.md
 */
export const WORLD_TOP_H = 72;
export const WORLD_BOTTOM_H = 58;
export const MAP_VIEW_H = LCD_HEIGHT - WORLD_TOP_H - WORLD_BOTTOM_H;
/**
 * Map-corner status strip (修/拍/…) height, flush under the exp bar
 * (SceneView.root local y=0 → absolute WORLD_TOP_H).
 */
export const MAP_STATUS_STRIP_H = 19;

/** Jade panel fill + double frame + corner flourishes. */
export function drawCbePanel(
  g: Graphics,
  x: number,
  y: number,
  w: number,
  h: number,
  fill = CBE.bgPanel,
  vectorCorners = true,
): void {
  const r = 2;
  g.roundRect(x, y, w, h, r).fill({ color: fill });
  // Ref frame: dark outer → jade → bright inner recess line.
  g.roundRect(x, y, w, h, r).stroke({ width: 1, color: CBE.borderOuter });
  g.roundRect(x + 1, y + 1, w - 2, h - 2, Math.max(0, r - 1)).stroke({
    width: 1,
    color: CBE.borderBright,
  });
  g.roundRect(x + 2, y + 2, w - 4, h - 4, Math.max(0, r - 1)).stroke({
    width: 1,
    color: CBE.borderOuter,
    alpha: 0.75,
  });
  if (vectorCorners) drawCbePanelVectorCorners(g, x, y, w, h);
}

/** Separate title capsule height / gap to body (ref: title above panel). */
export const CBE_TITLE_H = 28;
/** Minimal air gap between floating title and body (1px keeps them separate). */
export const CBE_TITLE_BODY_GAP = 1;

/** Jade frame fill on title capsule (ref outer bar). */
export const CBE_TITLE_FRAME = 0x487460;
/** Bright scroll stroke on title end-caps. */
export const CBE_TITLE_FLOURISH = 0x90d088;

/** Vector xiangyun corners only — use when flowerStyle.gif is unavailable. */
export function drawCbePanelVectorCorners(
  g: Graphics,
  x: number,
  y: number,
  w: number,
  h: number,
  size = 9,
): void {
  const c = 0x488470;
  const s = Math.max(6, size | 0);
  drawCorner(g, x + 3, y + 3, s, c, 0);
  drawCorner(g, x + w - 3, y + 3, s, c, 1);
  drawCorner(g, x + 3, y + h - 3, s, c, 2);
  drawCorner(g, x + w - 3, y + h - 3, s, c, 3);
}

/** Title-capsule left/right end flourishes (vector fallback). */
export function drawCbeTitleSideVectorFlowers(
  g: Graphics,
  titleX: number,
  titleY: number,
  titleW: number,
  titleH: number,
): void {
  const c = CBE_TITLE_FLOURISH;
  const endW = Math.max(12, Math.min(18, Math.floor(titleH * 0.55)));
  const midY = titleY + Math.floor(titleH / 2);
  const drawEnd = (left: boolean) => {
    const ox = left ? titleX + 2 : titleX + titleW - 2;
    const dir = left ? 1 : -1;
    // Central node
    g.circle(ox + dir * 4, midY, 1.5).fill({ color: c });
    // Upper tendril curling inward
    g.moveTo(ox + dir * 3, midY - 2)
      .lineTo(ox + dir * 5, midY - Math.floor(titleH * 0.28))
      .lineTo(ox + dir * (endW - 3), midY - Math.floor(titleH * 0.18))
      .stroke({ width: 1, color: c, alpha: 0.95 });
    // Lower tendril
    g.moveTo(ox + dir * 3, midY + 2)
      .lineTo(ox + dir * 5, midY + Math.floor(titleH * 0.28))
      .lineTo(ox + dir * (endW - 3), midY + Math.floor(titleH * 0.18))
      .stroke({ width: 1, color: c, alpha: 0.95 });
    // Inner bracket hug
    g.moveTo(ox + dir * (endW - 2), midY - Math.floor(titleH * 0.22))
      .lineTo(ox + dir * (endW - 1), midY)
      .lineTo(ox + dir * (endW - 2), midY + Math.floor(titleH * 0.22))
      .stroke({ width: 1, color: c, alpha: 0.75 });
  };
  drawEnd(true);
  drawEnd(false);
}

/** Full-LCD popup frame — same width as the world view (LCD_WIDTH). */
export const CBE_POPUP_X = 0;
export const CBE_POPUP_Y = 2;
export const CBE_POPUP_W = LCD_WIDTH;
export const CBE_POPUP_H = LCD_HEIGHT - 4;

export function cbePopupBounds(): {
  x: number;
  y: number;
  w: number;
  h: number;
} {
  return {
    x: CBE_POPUP_X,
    y: CBE_POPUP_Y,
    w: CBE_POPUP_W,
    h: CBE_POPUP_H,
  };
}

export type CbeTitledChrome = {
  titleX: number;
  titleY: number;
  titleW: number;
  titleH: number;
  /** Vertical center for title label. */
  titleCy: number;
  bodyX: number;
  bodyY: number;
  bodyW: number;
  bodyH: number;
};

/**
 * Split outer bounds into a floating title capsule + body panel below
 * (reference chrome: title and main panel are separate).
 */
export function layoutCbeTitledChrome(
  x: number,
  y: number,
  w: number,
  h: number,
  opts?: { titleH?: number; gap?: number; titleW?: number },
): CbeTitledChrome {
  const titleH = opts?.titleH ?? CBE_TITLE_H;
  const gap = opts?.gap ?? CBE_TITLE_BODY_GAP;
  // Ref title matches body width (wide capsule, not a short chip).
  const titleW = Math.max(48, Math.min(w, opts?.titleW ?? w));
  const titleX = Math.floor(x + (w - titleW) / 2);
  const titleY = y;
  const bodyY = y + titleH + gap;
  const bodyH = Math.max(8, h - titleH - gap);
  return {
    titleX,
    titleY,
    titleW,
    titleH,
    titleCy: titleY + titleH / 2,
    bodyX: x,
    bodyY,
    bodyW: w,
    bodyH,
  };
}

/** Capsule title bar — jade frame + recessed dark well (ref title chrome). */
export function drawCbeTitlePill(
  g: Graphics,
  cx: number,
  y: number,
  w: number,
  h: number,
): void {
  const x = Math.floor(cx - w / 2);
  const r = Math.floor(h / 2);
  // End-cap band — flowerStyle half width (35) scaled to title height (38).
  const endW = Math.max(12, Math.round((35 * h) / 38));
  const rail = Math.max(2, Math.floor(h * 0.12));

  // Outer dark bead.
  g.roundRect(x - 1, y - 1, w + 2, h + 2, r + 1).stroke({
    width: 1,
    color: CBE.borderOuter,
  });
  // Jade frame fill (end-caps + top/bottom rails).
  g.roundRect(x, y, w, h, r).fill({ color: CBE_TITLE_FRAME });
  // Recessed dark well — inset leaves jade end-caps for 边花.
  const ix = x + endW;
  const iy = y + rail;
  const iw = Math.max(8, w - endW * 2);
  const ih = Math.max(8, h - rail * 2);
  const ir = Math.floor(ih / 2);
  g.roundRect(ix, iy, iw, ih, ir)
    .fill({ color: CBE.titlePill })
    .stroke({ width: 1, color: CBE.borderOuter, alpha: 0.9 });
}

/** Softkey-sized corner button chrome (继续 / 返回). */
export function drawCbeSoftkeyBtn(
  g: Graphics,
  x: number,
  y: number,
  w: number,
  h = 24,
): void {
  g.roundRect(x, y, w, h, 3)
    .fill({ color: 0x2a5052 })
    .stroke({ width: 2, color: CBE.softkeyBorder });
}

/**
 * Corner xiangyun scroll (jade ornament; fallback when flowerStyle.gif is cold).
 * quad: 0=TL 1=TR 2=BL 3=BR
 */
function drawCorner(
  g: Graphics,
  cx: number,
  cy: number,
  s: number,
  color: number,
  quad: number,
): void {
  const dx = quad === 0 || quad === 2 ? 1 : -1;
  const dy = quad === 0 || quad === 1 ? 1 : -1;
  // Outer L spine
  g.moveTo(cx, cy + dy * s)
    .lineTo(cx, cy)
    .lineTo(cx + dx * s, cy)
    .stroke({ width: 1, color });
  // Inner parallel
  g.moveTo(cx + dx * 3, cy + dy * (s - 1))
    .lineTo(cx + dx * 3, cy + dy * 3)
    .lineTo(cx + dx * (s - 1), cy + dy * 3)
    .stroke({ width: 1, color, alpha: 0.9 });
  // Cloud curl toward panel center
  g.moveTo(cx + dx * 4, cy + dy * (s - 2))
    .lineTo(cx + dx * 7, cy + dy * (s - 5))
    .lineTo(cx + dx * (s - 3), cy + dy * 6)
    .stroke({ width: 1, color, alpha: 0.8 });
  // Secondary swirl tip
  g.moveTo(cx + dx * 6, cy + dy * 4)
    .lineTo(cx + dx * 9, cy + dy * 6)
    .lineTo(cx + dx * 8, cy + dy * 9)
    .stroke({ width: 1, color, alpha: 0.65 });
}

/**
 * Full-LCD-width location / title bar (CBE map name strip).
 */
export function drawLocBanner(
  g: Graphics,
  labelHost: Container,
  text: string,
  y = 2,
): BitmapLabel {
  const h = 24;
  g.rect(0, y, LCD_WIDTH, h).fill({ color: CBE.titlePill });
  g.moveTo(0, y + h)
    .lineTo(LCD_WIDTH, y + h)
    .stroke({ width: 1, color: CBE.border });
  // Top edge highlight
  g.moveTo(0, y)
    .lineTo(LCD_WIDTH, y)
    .stroke({ width: 1, color: CBE.borderDim });

  // Map title — follows global TTF (or ?uc3=1 bitmap).
  const t = new BitmapLabel();
  t.show(text, CBE.ink, { outline: false, smooth: false });
  t.anchor.set(0.5, 0.5);
  t.position.set(Math.round(LCD_WIDTH / 2), Math.round(y + h / 2));
  labelHost.addChild(t);
  return t;
}

/**
 * Create UI text. Default = preferred UC3 pack.
 * `size` is ignored for UC3 cell sizing.
 */
export function makePixelText(
  text: string,
  size: number,
  fill: number,
  opts?: {
    stroke?: number;
    strokeWidth?: number;
    outline?: boolean;
    uc3?: boolean;
    smooth?: boolean;
    crisp?: boolean;
  },
): BitmapLabel {
  const t = new BitmapLabel();
  t.show(text, fill, {
    fontSize: size,
    // Never imply outline from legacy stroke opts — only explicit outline:true.
    outline: opts?.outline === true,
    outlineColor: opts?.stroke,
    outlineThin: opts?.strokeWidth != null ? opts.strokeWidth <= 1 : undefined,
    // Omit uc3 → BitmapLabel follows preferSystemFont() (cn = OTF).
    ...(opts?.uc3 !== undefined ? { uc3: opts.uc3 } : {}),
    smooth: opts?.smooth === true,
    crisp: opts?.crisp !== false,
  });
  return t;
}

export {
  paintCbePanel,
  paintCbeTitledChrome,
  preloadCbeFlowerCorners,
  clearCbeFlowerCorners,
} from "./cbePanelCorners";
