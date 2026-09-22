import { Container, Graphics, Rectangle } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbePanel,
  paintCbeTitledChrome,
  cbePopupBounds,
  drawCbeTitlePill,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type { ActivitiesStatus, YanmenExchangeRow } from "../net/protocolActivities";
import type { SoftkeyLabels } from "./SoftkeyBar";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";

export type YanmenPanelAction = "exchange" | "close";


export class YanmenPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private status: ActivitiesStatus | null = null;
  private rows: YanmenExchangeRow[] = [];
  private sel = 0;
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: YanmenPanelAction, exchangeId?: number) => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.hitArea = new Rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    this.on("pointertap", (e) => e.stopPropagation());
    this.addChild(this.box);
    this.addChild(this.toast.host);
  }

  open(status: ActivitiesStatus | null, rows: YanmenExchangeRow[]): void {
    this.status = status;
    this.rows = rows;
    this.sel = 0;
    this.visible = true;
    this.redraw();
  }

  close(): void {
    clearPanelTip(this.toast);
    this.visible = false;
  }

  showTip(text: string, ms = 1400): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => this.redraw(), ms);
  }

  dismissTipOnSoftRight(): boolean {
    return tryDismissPanelTipOnSoftRight(this.toast);
  }

  move(delta: number): void {
    if (this.rows.length <= 0) return;
    this.sel = (this.sel + delta + this.rows.length) % this.rows.length;
    this.redraw();
  }

  confirm(): void {
    const row = this.rows[this.sel];
    if (row) this.onAction?.("exchange", row.exchangeId);
  }

  softkeys(): SoftkeyLabels {
    return { left: this.rows.length ? "兑换" : "", right: "关闭" };
  }

  private clearLabels(): void {
    for (const l of this.labels) l.destroy();
    this.labels = [];
  }

  private addLabel(text: string, x: number, y: number, fill = CBE.inkBody): void {
    const lbl = makePixelText(text, 12, fill);
    lbl.x = x;
    lbl.y = y;
    this.addChild(lbl);
    this.labels.push(lbl);
  }


  /** webglcontextrestored: remount paintCbePanel Graphics then redraw. */
  nudgeAfterGpuResume(): void {
    this.box = replaceGraphics(this.box);
    if (this.toast?.gfx?.parent) {
      this.toast.gfx = replaceGraphics(this.toast.gfx);
    }
    if (this.visible) this.redraw();
  }

  private redraw(): void {
    this.clearLabels();
    this.box.clear();
    const { x: PANEL_X, y: PANEL_Y, w: pw, h: ph } = cbePopupBounds();
    const chrome = paintCbeTitledChrome(this, this.box, PANEL_X, PANEL_Y, pw, ph, CBE.bgPanel);
    const title = makePixelText("材料兑换", 12, CBE.cream);
    title.anchor.set(0.5, 0.5);
    title.x = LCD_WIDTH / 2;
    title.y = chrome.titleCy;
    this.addChild(title);
    this.labels.push(title);

    const st = this.status;
    let y = chrome.bodyY + 8;
    this.addLabel(`木料：${st?.woodCount ?? 0}`, PANEL_X + 14, y);
    y += 16;
    this.addLabel(`石材：${st?.stoneCount ?? 0}`, PANEL_X + 14, y);
    y += 18;
    if (!this.rows.length) {
      this.addLabel("暂无兑换项", PANEL_X + 14, y, CBE.borderDim);
    } else {
      for (let i = 0; i < this.rows.length; i++) {
        const row = this.rows[i]!;
        const sel = i === this.sel;
        const line = `${row.label || `#${row.itemId}`} (木${row.woodCost}/石${row.stoneCost})`;
        this.addLabel(sel ? `> ${line}` : `  ${line}`, PANEL_X + 10, y, sel ? CBE.inkMint : CBE.inkBody);
        y += 16;
        if (y > PANEL_Y + ph - 24) break;
      }
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
