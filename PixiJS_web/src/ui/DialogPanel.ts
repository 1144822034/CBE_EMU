import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  WORLD_BOTTOM_H,
  makePixelText,
  paintCbePanel,
} from "./constants";
import { BitmapLabel, UC3_CELL, wrapChars } from "./BitmapLabel";
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
  /**
   * Softkey 返回 / cancel before hide+onClose.
   * Return true if handled (e.g. warehouse list → warehouse root).
   */
  onCancelRequest: (() => boolean) | null = null;

  get currentBody(): string {
    return this.dialog?.body ?? "";
  }

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

  /** Current NPC dialog (null during medicine qty confirm). */
  getDialog(): NpcDialog | null {
    return this.dialog;
  }

  show(dialog: NpcDialog, opts?: { keepCursor?: boolean }): void {
    const prevIndex = this.cursorIndex;
    const prevValue = this.dialog?.options[prevIndex]?.value;
    this.medicineBuy = null;
    this.dialog = dialog;
    this.eventMode = "static";
    this.remountChrome();
    this.visible = true;
    if (opts?.keepCursor) {
      const n = dialog.options.length;
      let next = 0;
      if (n > 0 && prevValue != null) {
        const idx = dialog.options.findIndex(
          (o) => (o.value >>> 0) === (prevValue >>> 0),
        );
        next = idx >= 0 ? idx : Math.min(prevIndex, n - 1);
      }
      this.cursorIndex = next;
    } else {
      this.cursorIndex = 0;
    }
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
    if (this.onCancelRequest?.()) return;
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

    const tall = !!this.dialog.tallList;
    const pageSize = Math.max(
      1,
      this.dialog.optionPageSize ?? OPTION_PAGE_SIZE,
    );
    const x = 12;
    // Bottom edge sits just above the hang softkey row (y342).
    const hangRowTop = LCD_HEIGHT - WORLD_BOTTOM_H;
    const gap = 8;
    const hNormal = 220;
    const y = tall ? 18 : hangRowTop - hNormal - gap;
    const w = LCD_WIDTH - 24;
    const h = tall
      ? Math.max(280, hangRowTop - y - gap)
      : hNormal;
    paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

    const opts = this.dialog.options;
    const pages = Math.max(1, Math.ceil(Math.max(opts.length, 1) / pageSize));
    const page = Math.min(pages - 1, Math.floor(this.cursorIndex / pageSize));
    const pageStart = page * pageSize;
    const pageOpts = opts.slice(pageStart, pageStart + pageSize);

    const titleText =
      pages > 1 ? `${this.dialog.title} ${page + 1}/${pages}` : this.dialog.title;
    this.addDialogTitle(titleText, y + 6);

    const bodyMax = tall ? 2 : 8;
    const bodyLines = wrapChars(this.dialog.body).slice(0, bodyMax);
    bodyLines.forEach((line, i) => {
      const t = makePixelText(line, UC3_CELL, CBE.inkBody);
      t.position.set(x + 10, y + 28 + i * 15);
      this.addChild(t);
      this.labels.push(t);
    });

    const navH = pages > 1 ? 24 : 8;
    const optTop = y + 30 + Math.max(bodyLines.length, 1) * 15;
    const listBottom = y + h - navH;
    const rowH = tall
      ? Math.max(15, Math.floor((listBottom - optTop) / pageSize))
      : 18;

    pageOpts.forEach((opt, i) => {
      const abs = pageStart + i;
      const hi = abs === this.cursorIndex;
      const ry = optTop + i * rowH;
      const ty = ry + Math.floor((rowH - 14) / 2);
      if (hi) {
        this.box.rect(x + 6, ry, w - 12, rowH - 1).fill({ color: CBE.select });
      }
      const ink = hi ? CBE.ink : (opt.nameColor ?? CBE.inkMint);
      const t = makePixelText(opt.label || `选项${abs + 1}`, UC3_CELL, ink);
      t.position.set(x + 12, ty);
      t.eventMode = "none";
      this.addChild(t);
      this.labels.push(t);
      if (opt.count != null && opt.count >= 0) {
        const ct = makePixelText(String(opt.count), UC3_CELL, ink);
        ct.anchor.set(1, 0);
        ct.position.set(x + w - 12, ty);
        ct.eventMode = "none";
        this.addChild(ct);
        this.labels.push(ct);
      }
      this.rowHits.push(
        attachRowPointerHit(this, x + 6, ry, w - 12, rowH - 1, () => {
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
        addLabel: (text, lx, ly, c) => {
          const lab = makePixelText(text, UC3_CELL, c);
          lab.position.set(lx, ly);
          this.addChild(lab);
          this.labels.push(lab);
        },
        onPrev: () => {
          this.cursorIndex = Math.max(0, pageStart - pageSize);
          void this.redraw();
        },
        onNext: () => {
          this.cursorIndex = Math.min(opts.length - 1, pageStart + pageSize);
          void this.redraw();
        },
      });
    }
  }

  private redrawMedicineBuy(): void {
    const s = this.medicineBuy;
    if (!s) return;
    const x = 12;
    const h = 220;
    const hangRowTop = LCD_HEIGHT - WORLD_BOTTOM_H;
    const y = hangRowTop - h - 8;
    const w = LCD_WIDTH - 24;
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
      const t = makePixelText(line, UC3_CELL, i === 3 ? CBE.inkTipGreen : CBE.inkBody);
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
    const buyLab = makePixelText("确认购买", UC3_CELL, CBE.inkMint);
    buyLab.position.set(x + w / 2 - 28, btnY + 5);
    this.addChild(buyLab);
    this.labels.push(buyLab);
    this.rowHits.push(
      attachRowPointerHit(this, x + 24, btnY, w - 48, 24, () => this.confirm()),
    );
  }

  /** Centered title; UC3_CELL so Songti and bitmap match. */
  private addDialogTitle(text: string, y: number): void {
    const title = makePixelText(text, UC3_CELL, CBE.ink);
    title.eventMode = "none";
    title.anchor.set(0.5, 0);
    title.position.set(LCD_WIDTH / 2, y);
    this.addChild(title);
    this.labels.push(title);
  }
}
