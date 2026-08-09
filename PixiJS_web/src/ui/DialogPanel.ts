import { Container, Graphics } from "pixi.js";
import { CBE, LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H, drawCbePanel, makePixelText } from "./constants";
import { BitmapLabel, wrapUiText } from "./BitmapLabel";
import type { NpcDialog } from "../net/protocolExtras";

/** Visible option rows per page (hangup skill list / long NPC menus). */
const OPTION_PAGE_SIZE = 7;

export class DialogPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private cursorIndex = 0;
  private dialog: NpcDialog | null = null;
  onPick: ((action: number, value: number, label: string) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(
      new Graphics()
        .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
        .fill({ color: 0x000000, alpha: 0.4 }),
    );
    this.addChild(this.box);
  }

  show(dialog: NpcDialog): void {
    this.dialog = dialog;
    this.cursorIndex = 0;
    this.visible = true;
    void this.redraw();
  }

  hide(): void {
    this.visible = false;
    this.dialog = null;
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

    const bodyLines = wrapUiText(this.dialog.body, w - 20).slice(0, 4);
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
      this.addChild(t);
      this.labels.push(t);
    });

    if (pages > 1) {
      const foot = makePixelText("↑↓翻页", 11, CBE.inkTip);
      foot.anchor.set(0.5, 0);
      foot.position.set(LCD_WIDTH / 2, y + h - 18);
      this.addChild(foot);
      this.labels.push(foot);
    }
  }
}
