/**
 * Shared center toast for LCD panels (auction / wager / arena / social / HUD…).
 * Opaque tip layer sits above panel labels so content cannot show through.
 */
import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  makePixelText,
  paintCbePanel,
} from "./constants";
import { UC3_CELL, measureUiTextWidth, wrapUiText } from "./BitmapLabel";

export type PanelTipState = {
  tipText: string;
  tipUntil: number;
  tipTimer: ReturnType<typeof setTimeout> | null;
  /** Own display list above panel labels / shared Graphics. */
  host: Container;
  gfx: Graphics;
  /** Redraw after auto-dismiss or close-button dismiss. */
  onRedraw: (() => void) | null;
};

export function createPanelTipState(): PanelTipState {
  const host = new Container();
  host.eventMode = "none";
  host.visible = false;
  const gfx = new Graphics();
  host.addChild(gfx);
  return {
    tipText: "",
    tipUntil: 0,
    tipTimer: null,
    host,
    gfx,
    onRedraw: null,
  };
}

export function showPanelTip(
  state: PanelTipState,
  text: string,
  onRedraw: () => void,
  ms = 1200,
): void {
  const msg = text.trim();
  if (!msg) return;
  state.onRedraw = onRedraw;
  state.tipText = msg;
  state.tipUntil = Date.now() + ms;
  if (state.tipTimer) clearTimeout(state.tipTimer);
  // dismissPanelTip clears the host layer even if onRedraw skips drawPanelTipOverlay
  // (common cause of "toast stuck after auto-close").
  state.tipTimer = setTimeout(() => {
    state.tipTimer = null;
    if (Date.now() >= state.tipUntil) dismissPanelTip(state);
  }, ms + 20);
  onRedraw();
}

/**
 * Right softkey: close transient / stuck toasts without leaving the panel.
 * Sticky confirm tips (`tipUntil === Infinity`) stay for cancel handlers.
 */
export function tryDismissPanelTipOnSoftRight(state: PanelTipState): boolean {
  const sticky =
    !!state.tipText && state.tipUntil === Number.POSITIVE_INFINITY;
  if (sticky) return false;
  if (!state.tipText && !state.host.visible) return false;
  dismissPanelTip(state);
  return true;
}

/** Persistent tip (confirm dialogs) — no auto-dismiss until clear/hide/close. */
export function setPanelTipSticky(
  state: PanelTipState,
  text: string,
  onRedraw?: () => void,
): void {
  const msg = text.trim();
  if (!msg) {
    clearPanelTip(state);
    return;
  }
  if (onRedraw) state.onRedraw = onRedraw;
  if (state.tipTimer) {
    clearTimeout(state.tipTimer);
    state.tipTimer = null;
  }
  state.tipText = msg;
  state.tipUntil = Number.POSITIVE_INFINITY;
}

export function clearPanelTip(state: PanelTipState): void {
  state.tipText = "";
  state.tipUntil = 0;
  if (state.tipTimer) {
    clearTimeout(state.tipTimer);
    state.tipTimer = null;
  }
  clearTipLayer(state);
}

/** Close button / manual dismiss — clears and triggers stored redraw. */
export function dismissPanelTip(state: PanelTipState): void {
  const redraw = state.onRedraw;
  clearPanelTip(state);
  redraw?.();
}

function clearTipLayer(state: PanelTipState): void {
  for (const c of [...state.host.children]) {
    if (c !== state.gfx) c.destroy();
  }
  state.gfx.clear();
  state.host.visible = false;
  state.host.eventMode = "none";
}

/**
 * Draw opaque center toast as the topmost child of `parent`.
 * Must run after panel redraw so host stays above labels.
 */
export function drawPanelTipOverlay(
  parent: Container,
  state: PanelTipState,
): void {
  // Re-parent to end → above box + BitmapLabels from this redraw.
  parent.addChild(state.host);
  // Keep above siblings that also bump z during redraw (HUD bands, etc.).
  state.host.zIndex = 1000;
  parent.sortableChildren = true;

  for (const c of [...state.host.children]) {
    if (c !== state.gfx) c.destroy();
  }
  state.gfx.clear();

  if (!state.tipText || Date.now() >= state.tipUntil) {
    if (state.tipText && Date.now() >= state.tipUntil) {
      state.tipText = "";
      state.tipUntil = 0;
      if (state.tipTimer) {
        clearTimeout(state.tipTimer);
        state.tipTimer = null;
      }
    }
    clearTipLayer(state);
    return;
  }

  const padFrame = 6;
  const padX = 10;
  // Asymmetric: UC3 glyphs sit low in the cell — extra bottom so bottoms are
  // not covered by drawCbePanel double-frame / corners.
  const padYTop = 8;
  const padYBottom = 14;
  const lineH = UC3_CELL + 4;
  const closeW = 18;
  const maxInner = LCD_WIDTH - 28 - padFrame * 2;
  const textMax = Math.max(40, maxInner - padX * 2 - closeW);
  const lines = wrapUiText(state.tipText, textMax);
  const contentW = Math.max(
    96,
    ...lines.map((l) => measureUiTextWidth(l)),
  );
  const boxW = Math.min(maxInner, contentW + padX * 2 + closeW);
  const boxH = padYTop + padYBottom + lines.length * lineH;

  // Nudge above mid-LCD so softkey chrome does not clip the toast.
  const bx = Math.floor((LCD_WIDTH - boxW) / 2) - padFrame;
  const by =
    Math.floor((LCD_HEIGHT - SOFTKEY_H) / 2 - boxH / 2) - padFrame - 24;
  const fw = boxW + padFrame * 2;
  const fh = boxH + padFrame * 2;
  paintCbePanel(state.host, state.gfx, bx, by, fw, fh, CBE.bgPanelDark);

  const textLeft = bx + padFrame + padX;
  const textTop = by + padFrame + padYTop;
  lines.forEach((line, i) => {
    const lab = makePixelText(line, 12, CBE.inkTipGreen);
    lab.anchor.set(0, 0);
    lab.x = textLeft;
    lab.y = Math.floor(textTop + i * lineH);
    state.host.addChild(lab);
  });

  // Top-right close hit — clears toast without waiting for timer.
  const closeHit = new Container();
  closeHit.eventMode = "static";
  closeHit.cursor = "pointer";
  const hitPad = 4;
  const hit = new Graphics()
    .rect(0, 0, closeW + hitPad, closeW + hitPad)
    .fill({ color: 0x000000, alpha: 0.001 });
  closeHit.addChild(hit);
  const closeLab = makePixelText("×", 12, CBE.inkTip);
  closeLab.x = 4;
  closeLab.y = 1;
  closeHit.addChild(closeLab);
  closeHit.x = bx + fw - closeW - hitPad - 2;
  closeHit.y = by + 2;
  closeHit.on("pointertap", (e) => {
    e.stopPropagation();
    dismissPanelTip(state);
  });
  state.host.addChild(closeHit);

  state.host.eventMode = "static";
  state.host.visible = true;
}
