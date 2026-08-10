import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
  drawCbeTitlePill,
  makePixelText,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type { WagerListRow, WagerMine } from "../net/protocolArena";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  type PanelTipState,
} from "./panelTip";

export type WagerPanelAction =
  | "create"
  | "challenge"
  | "confirm"
  | "reject"
  | "cancel"
  | "refresh"
  | "close";

/**
 * 赏金擂台面板：摆擂 / 列表挑战 / 擂主确认。
 */
export class WagerPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rows: WagerListRow[] = [];
  private mine: WagerMine | null = null;
  private listCursor = 0;
  private stakeInput = 10000;
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: WagerPanelAction, challengeId?: number) => void) | null =
    null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(this.box);
  }

  open(rows: WagerListRow[], mine: WagerMine | null): void {
    this.rows = rows;
    this.mine = mine;
    this.listCursor = 0;
    this.visible = true;
    this.redraw();
  }

  setData(rows: WagerListRow[], mine: WagerMine | null): void {
    this.rows = rows;
    this.mine = mine;
    this.redraw();
  }

  close(): void {
    clearPanelTip(this.toast);
    this.visible = false;
  }

  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  adjustStake(delta: number): void {
    if (!this.visible) return;
    const next = Math.max(1000, this.stakeInput + delta);
    this.stakeInput = Math.min(next, 100_000_000);
    this.redraw();
  }

  get stake(): number {
    return this.stakeInput >>> 0;
  }

  move(dir: -1 | 1): void {
    if (!this.visible) return;
    const n = this.actions().length;
    this.listCursor = (this.listCursor + dir + n) % n;
    this.redraw();
  }

  confirm(): void {
    if (!this.visible) return;
    const actions = this.actions();
    const a = actions[this.listCursor];
    if (!a) return;
    this.onAction?.(a.action, a.challengeId);
  }

  softkeys(): { left: string; mid: string; right: string } {
    return { left: "押金", mid: "确认", right: "关闭" };
  }

  private actions(): {
    label: string;
    action: WagerPanelAction;
    challengeId?: number;
  }[] {
    const out: {
      label: string;
      action: WagerPanelAction;
      challengeId?: number;
    }[] = [];
    out.push({
      label: `摆擂 押金${this.stakeInput}`,
      action: "create",
    });
    for (const r of this.rows) {
      out.push({
        label: `${r.hostName} ${r.hostCount}人 押${r.stake} [${
          r.status === 1 ? "待确认" : "可挑战"
        }]`,
        action: "challenge",
        challengeId: r.challengeId,
      });
    }
    if (this.mine?.needConfirm) {
      out.push({
        label: `确认开战 #${this.mine.challengeId}`,
        action: "confirm",
        challengeId: this.mine.challengeId,
      });
      out.push({
        label: `拒绝挑战 #${this.mine.challengeId}`,
        action: "reject",
        challengeId: this.mine.challengeId,
      });
    }
    if (this.mine && this.mine.status === 0 && this.mine.challengeId) {
      out.push({
        label: `撤擂 #${this.mine.challengeId}`,
        action: "cancel",
        challengeId: this.mine.challengeId,
      });
    }
    out.push({ label: "刷新", action: "refresh" });
    out.push({ label: "关闭", action: "close" });
    return out;
  }

  private clearLabels(): void {
    for (const l of this.labels) l.destroy();
    this.labels = [];
  }

  private addLabel(text: string, x: number, y: number, color: number): void {
    const l = makePixelText(text, 12, color);
    l.x = x;
    l.y = y;
    this.addChild(l);
    this.labels.push(l);
  }

  private redraw(): void {
    this.clearLabels();
    this.box.clear();
    drawCbePanel(this.box, 0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    drawCbeTitlePill(this.box, 8, 6, LCD_WIDTH - 16, 22);
    this.addLabel("赏金擂台", 16, 8, CBE.cream);
    const mineLine = this.mine?.challengeId
      ? `我的:#${this.mine.challengeId} 状态${this.mine.status}${
          this.mine.needConfirm ? " 待确认" : ""
        }`
      : "我的:无";
    this.addLabel(mineLine, 12, 32, CBE.inkMint);
    this.addLabel("左右调押金 上下选择", 12, 48, CBE.inkLabel);

    const actions = this.actions();
    let y = 68;
    const maxY = LCD_HEIGHT - SOFTKEY_H - 8;
    let start = 0;
    const rowH = 14;
    const maxShow = Math.max(1, Math.floor((maxY - y) / rowH));
    if (this.listCursor >= maxShow) start = this.listCursor - maxShow + 1;
    for (let i = 0; i < maxShow && start + i < actions.length; i++) {
      const idx = start + i;
      const a = actions[idx]!;
      const hi = idx === this.listCursor;
      if (hi) {
        this.box.rect(8, y - 1, LCD_WIDTH - 16, 14).fill({ color: CBE.select });
      }
      this.addLabel(a.label, 12, y, hi ? CBE.menuHi : CBE.ink);
      y += rowH;
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
