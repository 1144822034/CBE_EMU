/**
 * Shared center toast for LCD panels (auction / wager / arena / social…).
 * Opaque tip layer sits above panel labels so content cannot show through.
 */
import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
  makePixelText,
} from "./constants";
import { measureUiTextWidth, wrapUiText } from "./BitmapLabel";

export type PanelTipState = {
  tipText: string;
  tipUntil: number;
  tipTimer: ReturnType<typeof setTimeout> | null;
  /** Own display list above panel labels / shared Graphics. */
  host: Container;
  gfx: Graphics;
};

export function createPanelTipState(): PanelTipState {
  const host = new Container();
  host.eventMode = "none";
  host.visible = false;
  const gfx = new Graphics();
  host.addChild(gfx);
  return { tipText: "", tipUntil: 0, tipTimer: null, host, gfx };
}

export function showPanelTip(
  state: PanelTipState,
  text: string,
  onRedraw: () => void,
  ms = 1200,
): void {
  const msg = text.trim();
  if (!msg) return;
  state.tipText = msg;
  state.tipUntil = Date.now() + ms;
  if (state.tipTimer) clearTimeout(state.tipTimer);
  state.tipTimer = setTimeout(() => {
    state.tipTimer = null;
    if (Date.now() >= state.tipUntil) {
      state.tipText = "";
      onRedraw();
    }
  }, ms + 20);
  onRedraw();
}

/** Persistent tip (confirm dialogs) — no auto-dismiss until clear/hide. */
export function setPanelTipSticky(state: PanelTipState, text: string): void {
  const msg = text.trim();
  if (!msg) {
    clearPanelTip(state);
    return;
  }
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

function clearTipLayer(state: PanelTipState): void {
  for (const c of [...state.host.children]) {
    if (c !== state.gfx) c.destroy();
  }
  state.gfx.clear();
  state.host.visible = false;
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

  for (const c of [...state.host.children]) {
    if (c !== state.gfx) c.destroy();
  }
  state.gfx.clear();

  if (!state.tipText || Date.now() >= state.tipUntil) {
    if (state.tipText && Date.now() >= state.tipUntil) state.tipText = "";
    state.host.visible = false;
    return;
  }

  const padFrame = 6;
  const padX = 10;
  const padY = 8;
  const lineH = 16;
  // Inner text width must fit UC3 advances (14px CJK), not the old 12px guess.
  const maxInner = LCD_WIDTH - 28 - padFrame * 2;
  const textMax = Math.max(40, maxInner - padX * 2);
  const lines = wrapUiText(state.tipText, textMax);
  const contentW = Math.max(
    96,
    ...lines.map((l) => measureUiTextWidth(l)),
  );
  const boxW = Math.min(maxInner, contentW + padX * 2);
  const boxH = padY * 2 + lines.length * lineH;

  // Same chrome as battle settlement / death prompt (opaque CBE panel).
  const bx = Math.floor((LCD_WIDTH - boxW) / 2) - padFrame;
  const by =
    Math.floor((LCD_HEIGHT - SOFTKEY_H) / 2 - boxH / 2) - padFrame;
  const fw = boxW + padFrame * 2;
  const fh = boxH + padFrame * 2;
  drawCbePanel(state.gfx, bx, by, fw, fh, CBE.bgPanelDark);

  const cx = Math.floor(LCD_WIDTH / 2);
  lines.forEach((line, i) => {
    const lab = makePixelText(line, 12, CBE.inkTipGreen);
    // Anchor center so async UC3 bake still stays mid-screen (width is 0 until then).
    lab.anchor.set(0.5, 0);
    lab.x = cx;
    lab.y = Math.floor(by + padFrame + padY + i * lineH);
    state.host.addChild(lab);
  });
  state.host.visible = true;
}
