import { Container, Graphics, Rectangle } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbePanel,
  drawCbeTitlePill,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type { ActivitiesStatus, FestivalExchangeRow } from "../net/protocolActivities";
import type { SoftkeyLabels } from "./SoftkeyBar";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";

export type FestivalPanelAction = "exchange" | "close";

const PANEL_X = 8;
const PANEL_Y = 36;

export class FestivalPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private status: ActivitiesStatus | null = null;
  private sel = 0;
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: FestivalPanelAction, exchangeId?: number) => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.hitArea = new Rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    this.on("pointertap", (e) => e.stopPropagation());
    this.addChild(this.box);
    this.addChild(this.toast.host);
  }

  open(status: ActivitiesStatus | null): void {
    this.status = status;
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
    const n = this.rows().length;
    if (n <= 0) return;
    this.sel = (this.sel + delta + n) % n;
    this.redraw();
  }

  confirm(): void {
    const row = this.rows()[this.sel];
    if (row) this.onAction?.("exchange", row.exchangeId);
  }

  softkeys(): SoftkeyLabels {
    return { left: this.rows().length ? "兑换" : "", right: "关闭" };
  }

  private rows(): FestivalExchangeRow[] {
    return this.status?.festivalRows ?? [];
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

  private redraw(): void {
    this.clearLabels();
    this.box.clear();
    const pw = LCD_WIDTH - PANEL_X * 2;
    const ph = LCD_HEIGHT - SOFTKEY_H - PANEL_Y - 8;
    paintCbePanel(this, this.box, PANEL_X, PANEL_Y, pw, ph, CBE.bgDeep);
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, PANEL_Y + 6, Math.min(pw - 24, 100), 22);
    const title = makePixelText("活力兑换", 12, CBE.cream);
    title.anchor.set(0.5, 0.5);
    title.x = LCD_WIDTH / 2;
    title.y = PANEL_Y + 17;
    this.addChild(title);
    this.labels.push(title);

    const st = this.status;
    let y = PANEL_Y + 38;
    this.addLabel(
      `活力：${st?.vitality ?? 0}/${st?.vitalityMax ?? 0}`,
      PANEL_X + 14,
      y,
    );
    y += 18;
    const rows = this.rows();
    if (!rows.length) {
      this.addLabel("暂无兑换项（后台配置）", PANEL_X + 14, y, CBE.borderDim);
    } else {
      for (let i = 0; i < rows.length; i++) {
        const row = rows[i]!;
        const sel = i === this.sel;
        const line = `${row.label || `#${row.itemId}`} (${row.vitalityCost}活力)`;
        this.addLabel(sel ? `> ${line}` : `  ${line}`, PANEL_X + 10, y, sel ? CBE.inkMint : CBE.inkBody);
        y += 16;
        if (y > PANEL_Y + ph - 24) break;
      }
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
