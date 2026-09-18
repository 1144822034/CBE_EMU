import { Container, Graphics } from "pixi.js";
import { CBE, LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H, makePixelText, paintCbePanel } from "./constants";
import { BitmapLabel, UI_FONT_SIZE, wrapChars } from "./BitmapLabel";
import type { NpcDialog } from "../net/protocolExtras";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { attachPageNavButtons } from "./pageNav";
import { replaceGraphics } from "../render/gpuResume";

/** Visible option rows per page (hangup skill list / long NPC menus). */
const OPTION_PAGE_SIZE = 7;

/** NPC medicine buy qty confirm (←→ adjust; 1←→10 pack). */
export type MedicineBuyQtyState = {
  itemId: number;
  itemName: string;
  unitPrice: number;
  currency: string;
  qty: number;
  maxQty: number;
};

export class DialogPanel extends Container {
  private dim = new Graphics();
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private cursorIndex = 0;
  private dialog: NpcDialog | null = null;
  private medicineBuy: MedicineBuyQtyState | null = null;
  onPick: ((action: number, value: number, label: string) => void) | null = null;
  onClose: (() => void) | null = null;
  /** Fired when medicine qty confirm is cancelled (return to shop list). */
  onMedicineBuyCancel: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "none";
    this.paintDim();
    this.dim.eventMode = "static";
    this.dim.on("pointertap", (e) => e.stopPropagation());
    this.addChild(this.dim);
    this.addChild(this.box);
  }

  private paintDim(): void {
    this.dim
      .clear()
      .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
      .fill({ color: 0x000000, alpha: 0.4 });
  }

  private remountChrome(): void {
    this.dim = replaceGraphics(this.dim);
    this.paintDim();
    this.dim.eventMode = "static";
    this.dim.on("pointertap", (e) => e.stopPropagation());
    this.box = replaceGraphics(this.box);
  }

  hide(): void {
    clearPointerHits(this.rowHits);
    this.visible = false;
    this.dialog = null;
    this.medicineBuy = null;
    this.eventMode = "none";
    for (const t of this.labels) t.destroy();
    this.labels = [];
  }

  show(dialog: NpcDialog): void {
    this.medicineBuy = null;
    this.dialog = dialog;
    this.cursorIndex = 0;
    this.eventMode = "static";
    this.remountChrome();
    this.visible = true;
    void this.redraw();
  }

  /** Open qty confirm for NPC medicine purchase. */
  showMedicineBuyQty(state: MedicineBuyQtyState): void {
    this.dialog = null;
    this.medicineBuy = {
      ...state,
      qty: Math.max(1, Math.min(state.maxQty, state.qty || 1)),
      maxQty: Math.max(1, state.maxQty | 0),
    };
    this.cursorIndex = 0;
    this.eventMode = "static";
    this.remountChrome();
    this.visible = true;
    void this.redraw();
  }

  isMedicineBuyQty(): boolean {
    return this.medicineBuy != null;
  }

  getMedicineBuyQty(): MedicineBuyQtyState | null {
    return this.medicineBuy;
  }

  /**
   * ←→ on medicine qty confirm.
   * qty=1 + ← → jump to min(10, max); → does not wrap (clamped by max).
   */
  adjustMedicineBuyQty(delta: number): boolean {
    if (!this.medicineBuy) return false;
    const max = Math.max(1, this.medicineBuy.maxQty | 0);
    let next: number;
    if (delta < 0 && this.medicineBuy.qty <= 1) {
      next = Math.min(max, 10);
    } else {
      next = Math.min(max, Math.max(1, this.medicineBuy.qty + delta));
    }
    if (next === this.medicineBuy.qty) return true;
    this.medicineBuy.qty = next;
    void this.redraw();
    return true;
  }

  move(delta: number): void {
    if (this.medicineBuy) return;
    if (!this.dialog?.options.length) return;
    const n = this.dialog.options.length;
    this.cursorIndex = (this.cursorIndex + delta + n) % n;
    void this.redraw();
  }

  confirm(): void {
    if (this.medicineBuy) {
      const s = this.medicineBuy;
      this.onPick?.(1, s.itemId, `购买${s.itemName}`);
      return;
    }
    if (!this.dialog) return;
    const opt = this.dialog.options[this.cursorIndex];
    if (opt) this.onPick?.(opt.action, opt.value, opt.label);
    else this.onClose?.();
  }

  cancel(): void {
    if (this.medicineBuy) {
      this.medicineBuy = null;
      this.onMedicineBuyCancel?.();
      return;
    }
    this.hide();
    this.onClose?.();
  }


  /** webglcontextrestored: remount paintCbePanel Graphics; redraw if open. */
  nudgeAfterGpuResume(): void {
    this.remountChrome();
    if (this.visible) void this.redraw();
  }

  private async redraw(): Promise<void> {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();

    if (this.medicineBuy) {
      this.redrawMedicineBuy();
      return;
    }
    if (!this.dialog) return;

    const x = 12;
    const y = 60;
    const w = LCD_WIDTH - 24;
    const h = 220;
    paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

    const opts = this.dialog.options;
    const pages = Math.max(1, Math.ceil(Math.max(opts.length, 1) / OPTION_PAGE_SIZE));
    const page = Math.min(
      pages - 1,
      Math.floor(this.cursorIndex / OPTION_PAGE_SIZE),
    );
    const pageStart = page * OPTION_PAGE_SIZE;
    const pageOpts = opts.slice(pageStart, pageStart + OPTION_PAGE_SIZE);

    const titleText =
      pages > 1 ? `${this.dialog.title} ${page + 1}/${pages}` : this.dialog.title;
    this.addDialogTitle(titleText, y + 8);

    const bodyLines = wrapChars(this.dialog.body).slice(0, 8);
    bodyLines.forEach((line, i) => {
      const t = makePixelText(line, 11, CBE.inkBody);
      t.position.set(x + 10, y + 32 + i * 16);
      this.addChild(t);
      this.labels.push(t);
    });

    const optTop = y + 36 + Math.max(bodyLines.length, 1) * 16;
    pageOpts.forEach((opt, i) => {
      const abs = pageStart + i;
      const hi = abs === this.cursorIndex;
      if (hi) {
        this.box.rect(x + 6, optTop + i * 18, w - 12, 16).fill({ color: CBE.select });
      }
      const t = makePixelText(opt.label || `选项${abs + 1}`, 12, hi ? CBE.ink : CBE.inkMint);
      t.position.set(x + 12, optTop + 1 + i * 18);
      t.eventMode = "none";
      this.addChild(t);
      this.labels.push(t);
      this.rowHits.push(
        attachRowPointerHit(this, x + 6, optTop + i * 18, w - 12, 16, () => {
          this.cursorIndex = abs;
          void this.redraw();
          this.confirm();
        }),
      );
    });

    if (pages > 1) {
      attachPageNavButtons({
        host: this,
        hits: this.rowHits,
        box: this.box,
        x: x + 8,
        y: y + h - 22,
        w: w - 16,
        page: page + 1,
        pages,
        addLabel: (t, lx, ly, c) => {
          const lab = makePixelText(t, 11, c);
          lab.position.set(lx, ly);
          this.addChild(lab);
          this.labels.push(lab);
        },
        onPrev: () => {
          this.cursorIndex = Math.max(0, pageStart - OPTION_PAGE_SIZE);
          void this.redraw();
        },
        onNext: () => {
          this.cursorIndex = Math.min(
            opts.length - 1,
            pageStart + OPTION_PAGE_SIZE,
          );
          void this.redraw();
        },
      });
    }
  }

  private redrawMedicineBuy(): void {
    const s = this.medicineBuy;
    if (!s) return;
    const x = 12;
    const y = 60;
    const w = LCD_WIDTH - 24;
    const h = 220;
    paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

    this.addDialogTitle("购买药品", y + 8);

    const total = s.unitPrice * s.qty;
    const lines = [
      s.itemName,
      `单价 ${s.unitPrice}${s.currency}`,
      `数量 ${s.qty}  合计 ${total}${s.currency}`,
      "←→ 调数量 (1←10)",
      s.maxQty <= 1 ? "余额仅够买1个" : `最多可买 ${s.maxQty}`,
    ];
    lines.forEach((line, i) => {
      const t = makePixelText(line, 12, i === 3 ? CBE.inkTipGreen : CBE.inkBody);
      t.position.set(x + 10, y + 36 + i * 18);
      this.addChild(t);
      this.labels.push(t);
    });

    const btnY = y + h - 40;
    this.box
      .roundRect(x + 24, btnY, w - 48, 24, 3)
      .fill({ color: CBE.bgDeep })
      .stroke({ width: 1, color: CBE.borderBright });
    this.box.rect(x + 24, btnY, w - 48, 24).fill({ color: CBE.select });
    const buyLab = makePixelText("确认购买", 12, CBE.inkMint);
    buyLab.position.set(x + w / 2 - 28, btnY + 5);
    this.addChild(buyLab);
    this.labels.push(buyLab);
    this.rowHits.push(
      attachRowPointerHit(this, x + 24, btnY, w - 48, 24, () => this.confirm()),
    );
  }

  /** Match SystemPanel / bag popup titles: centered, default UI size (Songti→14). */
  private addDialogTitle(text: string, y: number): void {
    const title = makePixelText(text, UI_FONT_SIZE, CBE.ink);
    title.eventMode = "none";
    title.anchor.set(0.5, 0);
    title.position.set(LCD_WIDTH / 2, y);
    this.addChild(title);
    this.labels.push(title);
  }
}
