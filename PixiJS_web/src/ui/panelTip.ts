/**
 * Shared center toast for LCD panels (auction / wager / arena / social / HUD…).
 * Tip host stays above panel labels; tip Songti uses a top DOM z band.
 * Overlapping Songti is covered by a solid DOM plate (not whole-line hidden).
 * UC3 tips stay on the canvas — no DOM plate (it would cover canvas glyphs).
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
import { preferSystemFont } from "../resources/systemFont";
import { UC3_CELL, measureUiTextWidth, wrapUiText } from "./BitmapLabel";
import {
  clearLcdDomTipOccludeRect,
  registerLcdDomPanelTip,
  setLcdDomTipOccludeRect,
} from "./lcdDomText";

/** Above panel labels / HUD chrome / popup Songti. */
const TIP_HOST_Z = 2_000_000;

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
  host.zIndex = TIP_HOST_Z;
  registerLcdDomPanelTip(host);
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
  clearLcdDomTipOccludeRect();
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
  state.host.zIndex = TIP_HOST_Z;
  parent.sortableChildren = true;
  registerLcdDomPanelTip(state.host);

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
  const padY = 10;
  const lineH = UC3_CELL + 4;
  const closeW = 18;
  const maxInner = LCD_WIDTH - 28 - padFrame * 2;
  const textMax = Math.max(40, maxInner - padX * 2);
  const lines = wrapUiText(state.tipText, textMax);
  const contentW = Math.max(
    96,
    ...lines.map((l) => measureUiTextWidth(l)),
  );
  // Keep room for × without biasing body text left.
  const boxW = Math.min(maxInner, contentW + padX * 2 + closeW);
  const textBlockH = lines.length * lineH;
  const boxH = Math.max(textBlockH + padY * 2, 36);

  // Mid-LCD, then +76 (= prior -24 nudge + 100px down).
  const bx = Math.floor((LCD_WIDTH - boxW) / 2) - padFrame;
  const by =
    Math.floor((LCD_HEIGHT - SOFTKEY_H) / 2 - boxH / 2) - padFrame + 76;
  const fw = boxW + padFrame * 2;
  const fh = boxH + padFrame * 2;
  // Solid canvas fill under chrome so map pixels cannot peek through.
  state.gfx.roundRect(bx, by, fw, fh, 2).fill({ color: CBE.bgPanelDark, alpha: 1 });
  paintCbePanel(state.host, state.gfx, bx, by, fw, fh, CBE.bgPanelDark);
  // Songti tip text is DOM above this plate; UC3 tip text is canvas under
  // #lcd-dom-text — a DOM plate would hide the glyphs (十连 tip empty box).
  if (preferSystemFont()) {
    setLcdDomTipOccludeRect(state.host, bx, by, fw, fh, CBE.bgPanelDark);
  } else {
    clearLcdDomTipOccludeRect();
  }

  const textCx = Math.round(bx + padFrame + boxW / 2);
  const textTop =
    by + padFrame + Math.max(0, Math.floor((boxH - textBlockH) / 2));
  lines.forEach((line, i) => {
    const lab = makePixelText(line, 12, CBE.inkTipGreen);
    lab.anchor.set(0.5, 0);
    lab.x = textCx;
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
