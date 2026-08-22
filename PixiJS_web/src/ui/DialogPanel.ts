import { Container, Graphics } from "pixi.js";
import { CBE, LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H, drawCbePanel, makePixelText } from "./constants";
import { BitmapLabel, wrapChars } from "./BitmapLabel";
import type { NpcDialog } from "../net/protocolExtras";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { attachPageNavButtons } from "./pageNav";

/** Visible option rows per page (hangup skill list / long NPC menus). */
const OPTION_PAGE_SIZE = 7;

export class DialogPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private cursorIndex = 0;
  private dialog: NpcDialog | null = null;
  onPick: ((action: number, value: number, label: string) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "none";
    const dim = new Graphics()
      .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
      .fill({ color: 0x000000, alpha: 0.4 });
    dim.eventMode = "static";
    dim.on("pointertap", (e) => e.stopPropagation());
    this.addChild(dim);
    this.addChild(this.box);
  }

  hide(): void {
    clearPointerHits(this.rowHits);
    this.visible = false;
    this.dialog = null;
    this.eventMode = "none";
  }

  show(dialog: NpcDialog): void {
    this.dialog = dialog;
    this.cursorIndex = 0;
    this.eventMode = "static";
    this.visible = true;
    void this.redraw();
  }

  move(delta: number): void {
    if (!this.dialog?.options.length) return;
    const n = this.dialog.options.length;
    this.cursorIndex = (this.cursorIndex + delta + n) % n;
    void this.redraw();
  }

  confirm(): void {
    if (!this.dialog) return;
    const opt = this.dialog.options[this.cursorIndex];
    if (opt) this.onPick?.(opt.action, opt.value, opt.label);
    else this.onClose?.();
  }

  cancel(): void {
    this.hide();
    this.onClose?.();
  }

  private async redraw(): Promise<void> {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    if (!this.dialog) return;

    const x = 12;
    const y = 60;
    const w = LCD_WIDTH - 24;
    const h = 220;
    drawCbePanel(this.box, x, y, w, h, CBE.bgDeep);

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
    const title = makePixelText(titleText, 13, CBE.ink);
    title.position.set(x + 10, y + 8);
    this.addChild(title);
    this.labels.push(title);

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
}
