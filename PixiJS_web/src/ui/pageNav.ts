import { Container, Graphics, Texture } from "pixi.js";
import { CBE } from "./constants";
import { attachRowPointerHit } from "./pointerHit";
import { loadUiCrop, uiSprite } from "./UiAssets";

export type PageNavDrawOpts = {
  host: Container;
  hits: Container[];
  box: Graphics;
  /** Left edge of the nav strip. */
  x: number;
  y: number;
  /** Total width for 上一页 | 下一页. */
  w: number;
  /** 1-based current page. */
  page: number;
  pages: number;
  addLabel: (text: string, x: number, y: number, color: number) => void;
  onPrev: () => void;
  onNext: () => void;
  /** Optional height (default = GIF height or 18). */
  h?: number;
};

/**
 * CBE sheets: each GIF is enabled|disabled side-by-side (74×13 → 37×13 each).
 * frontmsg = 上一页; backmsg = 下一页 (names are historical, not left/right).
 */
const PREV_GIF = "frontmsg.gif";
const NEXT_GIF = "backmsg.gif";
const SHEET_W = 74;
const SHEET_H = 13;
const HALF_W = SHEET_W >> 1;

type PageNavTex = {
  prevOn: Texture;
  prevOff: Texture;
  nextOn: Texture;
  nextOff: Texture;
};

let navTex: PageNavTex | null = null;
let pageNavLoad: Promise<void> | null = null;
const readyCallbacks = new Set<() => void>();

/** Warm frontmsg/backmsg crops for 上一页 / 下一页. */
export function preloadPageNavTextures(onReady?: () => void): void {
  if (navTex) {
    onReady?.();
    return;
  }
  if (onReady) readyCallbacks.add(onReady);
  if (pageNavLoad) return;
  pageNavLoad = Promise.all([
    loadUiCrop(PREV_GIF, 0, 0, HALF_W, SHEET_H),
    loadUiCrop(PREV_GIF, HALF_W, 0, HALF_W, SHEET_H),
    loadUiCrop(NEXT_GIF, 0, 0, HALF_W, SHEET_H),
    loadUiCrop(NEXT_GIF, HALF_W, 0, HALF_W, SHEET_H),
  ]).then(([prevOn, prevOff, nextOn, nextOff]) => {
    if (prevOn && prevOff && nextOn && nextOff) {
      navTex = { prevOn, prevOff, nextOn, nextOff };
      for (const cb of readyCallbacks) cb();
      readyCallbacks.clear();
    }
  });
}

function pageNavBtnSize(
  tex: Texture | null,
  maxPairW: number,
  preferH?: number,
): { w: number; h: number } {
  const natW = tex?.width || HALF_W;
  const natH = tex?.height || SHEET_H;
  const pageGap = 28;
  let w = natW;
  let h = preferH ?? natH;
  if (preferH != null) {
    w = Math.round(natW * (preferH / natH));
  }
  if (w * 2 + pageGap > maxPairW) {
    w = Math.max(24, Math.floor((maxPairW - pageGap) / 2));
    h = Math.max(10, Math.round(natH * (w / natW)));
  }
  return { w, h };
}

/**
 * Draw tappable 上一页 / 下一页 when pages > 1.
 * Uses half-crops of frontmsg/backmsg (enabled|disabled); falls back to text.
 * GIF sprites live in a strip Container pushed to `hits` so redraws clean up.
 * Returns strip height (0 if not drawn).
 */
export function attachPageNavButtons(opts: PageNavDrawOpts): number {
  const pages = Math.max(1, opts.pages | 0);
  if (pages <= 1) return 0;
  const page = Math.min(Math.max(1, opts.page | 0), pages);
  preloadPageNavTextures();

  const prevOn = page > 1;
  const nextOn = page < pages;
  const y = opts.y;
  const pageGap = 28;

  const strip = new Container();
  strip.eventMode = "passive";
  opts.host.addChild(strip);
  opts.hits.push(strip);

  if (navTex) {
    const size = pageNavBtnSize(navTex.prevOn, opts.w, opts.h);
    const btnW = size.w;
    const btnH = size.h;
    const totalW = btnW * 2 + pageGap;
    const leftX = opts.x + Math.max(0, Math.floor((opts.w - totalW) / 2));
    const rightX = leftX + btnW + pageGap;
    const labelY = y + Math.max(0, Math.floor((btnH - 12) / 2));

    const placeBtn = (
      tex: Texture,
      bx: number,
      enabled: boolean,
      onTap: () => void,
    ): void => {
      const spr = uiSprite(tex, btnW, btnH);
      spr.eventMode = "none";
      spr.position.set(bx, y);
      strip.addChild(spr);
      if (enabled) {
        attachRowPointerHit(strip, bx, y, btnW, btnH, onTap);
      }
    };

    placeBtn(
      prevOn ? navTex.prevOn : navTex.prevOff,
      leftX,
      prevOn,
      opts.onPrev,
    );
    placeBtn(
      nextOn ? navTex.nextOn : navTex.nextOff,
      rightX,
      nextOn,
      opts.onNext,
    );
    opts.addLabel(
      `${page}/${pages}`,
      leftX + btnW + Math.floor((pageGap - 20) / 2),
      labelY,
      CBE.inkLabel,
    );
    return btnH;
  }

  // Fallback: text buttons (assets not loaded yet).
  const h = opts.h ?? 18;
  const gap = 4;
  const btnW = Math.max(48, Math.floor((opts.w - gap) / 2));
  const leftX = opts.x;
  const rightX = opts.x + btnW + gap;

  opts.box
    .roundRect(leftX, y, btnW, h, 2)
    .fill({ color: prevOn ? CBE.bgPanelDark : 0x121c18 })
    .stroke({ width: 1, color: prevOn ? CBE.borderBright : CBE.borderDim });
  opts.addLabel("上一页", leftX + 8, y + 3, prevOn ? CBE.inkMint : 0x5a6a62);
  if (prevOn) {
    attachRowPointerHit(strip, leftX, y, btnW, h, opts.onPrev);
  }

  opts.box
    .roundRect(rightX, y, btnW, h, 2)
    .fill({ color: nextOn ? CBE.bgPanelDark : 0x121c18 })
    .stroke({ width: 1, color: nextOn ? CBE.borderBright : CBE.borderDim });
  opts.addLabel(
    `下一页 ${page}/${pages}`,
    rightX + 6,
    y + 3,
    nextOn ? CBE.inkMint : 0x5a6a62,
  );
  if (nextOn) {
    attachRowPointerHit(strip, rightX, y, btnW, h, opts.onNext);
  }
  return h;
}
