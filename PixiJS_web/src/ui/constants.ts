import { Container, Graphics } from "pixi.js";
import { BitmapLabel } from "./BitmapLabel";

/** Palette sampled from live CBE screenshots (PixiJS_web/reference-shots/*-game.png). */
export const LCD_WIDTH = 240;
export const LCD_HEIGHT = 400;

/**
 * Map / battle unit sprite scale vs native `.actor` pixels.
 * Native frames (~20–40px) read large on the 240×400 LCD; tune vs CBE screenshots.
 */
export const ACTOR_DISPLAY_SCALE = 0.8;

/** Over-head nameplates track actor scale (map + battle unit names). */
export const NAMEPLATE_SCALE = ACTOR_DISPLAY_SCALE;

export const CBE = {
  bgDeep: 0x0e1b18,
  bgPanel: 0x3c665c,
  bgPanelDark: 0x2a4a42,
  bgHudRow: 0x4a6e62,
  border: 0x9eb5a8,
  borderBright: 0xb8d0c4,
  borderDim: 0x6a8578,
  select: 0x3a5a50,
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
  titlePill: 0x2a4840,
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

/** Dark teal panel with light border + corner flourishes. */
export function drawCbePanel(
  g: Graphics,
  x: number,
  y: number,
  w: number,
  h: number,
  fill = CBE.bgPanel,
): void {
  g.roundRect(x, y, w, h, 2).fill({ color: fill });
  g.roundRect(x, y, w, h, 2).stroke({ width: 1, color: CBE.border });
  // Inner hairline (CBE double-frame look).
  g.roundRect(x + 2, y + 2, w - 4, h - 4, 1).stroke({
    width: 1,
    color: CBE.borderBright,
    alpha: 0.35,
  });
  const c = CBE.borderBright;
  const s = 12;
  drawCorner(g, x + 3, y + 3, s, c, 0);
  drawCorner(g, x + w - 3, y + 3, s, c, 1);
  drawCorner(g, x + 3, y + h - 3, s, c, 2);
  drawCorner(g, x + w - 3, y + h - 3, s, c, 3);
}

/** Capsule title bar used by 属性 / tip panels. */
export function drawCbeTitlePill(
  g: Graphics,
  cx: number,
  y: number,
  w: number,
  h: number,
): void {
  const x = Math.floor(cx - w / 2);
  g.roundRect(x, y, w, h, Math.floor(h / 2))
    .fill({ color: CBE.titlePill })
    .stroke({ width: 1, color: CBE.borderBright });
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
 * Corner xiangyun-style L + inner scroll (sampled from CBE attr panel shots).
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
  // Outer L
  g.moveTo(cx, cy + dy * s)
    .lineTo(cx, cy)
    .lineTo(cx + dx * s, cy)
    .stroke({ width: 1, color });
  // Inner parallel
  g.moveTo(cx + dx * 3, cy + dy * (s - 1))
    .lineTo(cx + dx * 3, cy + dy * 3)
    .lineTo(cx + dx * (s - 1), cy + dy * 3)
    .stroke({ width: 1, color, alpha: 0.85 });
  // Small curl toward panel center
  g.moveTo(cx + dx * 5, cy + dy * (s - 3))
    .lineTo(cx + dx * (s - 4), cy + dy * 5)
    .stroke({ width: 1, color, alpha: 0.7 });
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

  // Map title: font_gb.uc3 hard pixels (CBE LCD).
  const t = new BitmapLabel();
  t.show(text, CBE.ink, { uc3: true, outline: false, smooth: false });
  t.anchor.set(0.5, 0.5);
  t.position.set(Math.round(LCD_WIDTH / 2), Math.round(y + h / 2));
  labelHost.addChild(t);
  return t;
}

/**
 * Create UI text. Default = font_gb.uc3 hard pointillism (mobile-sharp on
 * pixelated canvas). HTML softkeys stay DOM; Pixi has no separate mobile font.
 * `size` applies only when `{ uc3: false }` (system font).
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
    uc3: opts?.uc3 !== false,
    smooth: opts?.smooth === true,
    crisp: opts?.crisp !== false,
  });
  return t;
}
