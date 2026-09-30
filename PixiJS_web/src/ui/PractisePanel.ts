/**
 * 「修」status: panel of remaining effect times (7/31 tip + 修炼丹 bank).
 * No 黄金修炼 / help / full practise stats.
 */
import { Container, Graphics, Rectangle } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbeTitledChrome,
  cbePopupBounds,
  makePixelText,
  blockPanelPointerPassthrough,
} from "./constants";
import { BitmapLabel, wrapChars } from "./BitmapLabel";
import { formatPractiseHm } from "../net/protocolExtras";
import type { SoftkeyLabels } from "./SoftkeyBar";

const VALUE_WELL = 0x0a1814;

export class PractisePanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  /** Lines from 7/31 buff tip (already include remain text). */
  private effectLines: string[] = [];
  private pillHour = 0;
  private pillMin = 0;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.hitArea = new Rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    this.on("pointertap", (e) => e.stopPropagation());
    this.addChild(this.box);
    blockPanelPointerPassthrough(this);
  }

  open(opts: {
    buffTip: string;
    pillHour?: number;
    pillMin?: number;
  }): void {
    const tip = (opts.buffTip || "").trim();
    this.effectLines = tip
      ? tip
          .split(/\r?\n/)
          .map((s) => s.trim())
          .filter(Boolean)
      : [];
    this.pillHour = (opts.pillHour ?? 0) | 0;
    this.pillMin = (opts.pillMin ?? 0) | 0;
    this.visible = true;
    this.redraw();
  }

  close(): void {
    this.visible = false;
  }

  softkeys(): SoftkeyLabels {
    return { left: "", right: "返回" };
  }

  cancel(): void {
    this.close();
    this.onClose?.();
  }

  nudgeAfterGpuResume(): void {
    this.box = replaceGraphics(this.box);
    if (this.visible) this.redraw();
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
    const { x: PANEL_X, y: PANEL_Y, w: pw, h: ph } = cbePopupBounds();
    const chrome = paintCbeTitledChrome(
      this,
      this.box,
      PANEL_X,
      PANEL_Y,
      pw,
      ph,
      CBE.bgPanel,
    );
    const title = makePixelText("状态信息", 12, CBE.cream);
    title.anchor.set(0.5, 0.5);
    title.x = LCD_WIDTH / 2;
    title.y = chrome.titleCy;
    this.addChild(title);
    this.labels.push(title);

    const padX = PANEL_X + 10;
    const contentW = pw - 20;
    let y = chrome.bodyY + 10;
    const footY = PANEL_Y + ph - 8;

    const lines: { text: string; color: number }[] = [];
    if (this.effectLines.length) {
      for (const line of this.effectLines) {
        for (const wrapped of wrapChars(line, 15)) {
          lines.push({ text: wrapped, color: CBE.inkTipGreen });
        }
      }
    } else {
      lines.push({ text: "当前没有生效的增益物品", color: CBE.inkBody });
    }
    lines.push({
      text: `修炼丹剩余 ${formatPractiseHm(this.pillHour, this.pillMin)}`,
      color: CBE.inkTipGreen,
    });

    const boxH = Math.min(footY - y, lines.length * 16 + 10);
    this.box
      .roundRect(padX, y, contentW, boxH, 2)
      .fill({ color: VALUE_WELL })
      .stroke({ width: 1, color: CBE.borderDim, alpha: 0.7 });

    let ty = y + 5;
    const maxY = y + boxH - 14;
    for (const row of lines) {
      if (ty > maxY) break;
      this.addLabel(row.text, padX + 4, ty, row.color);
      ty += 16;
    }
  }
}
