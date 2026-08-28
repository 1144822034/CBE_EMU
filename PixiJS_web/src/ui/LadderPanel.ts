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
import type { LadderStatus } from "../net/protocolLadder";
import type { SoftkeyLabels } from "./SoftkeyBar";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";

export type LadderPanelAction = "challenge" | "close";

const PANEL_X = 8;
const PANEL_Y = 36;

/**
 * 天梯挑战：每日次数、最高层、开始下一层 PvE 三职业战。
 */
export class LadderPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private status: LadderStatus | null = null;
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: LadderPanelAction) => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.hitArea = new Rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    this.on("pointertap", (e) => {
      e.stopPropagation();
    });
    this.addChild(this.box);
    this.addChild(this.toast.host);
  }

  open(status: LadderStatus | null): void {
    this.status = status;
    this.visible = true;
    this.redraw();
  }

  setStatus(status: LadderStatus | null): void {
    this.status = status;
    this.redraw();
  }

  close(): void {
    clearPanelTip(this.toast);
    this.visible = false;
  }

  dismissTipOnSoftRight(): boolean {
    return tryDismissPanelTipOnSoftRight(this.toast);
  }

  showTip(text: string, ms = 1400): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => this.redraw(), ms);
  }

  confirm(): void {
    if (!this.visible) return;
    this.onAction?.("challenge");
  }

  softkeys(): SoftkeyLabels {
    const st = this.status;
    const can =
      st?.enabled &&
      st.dailyLimit > 0 &&
      (st.dailyUsed < st.dailyLimit || st.dailyLimit === 0);
    return {
      left: can ? "挑战" : "",
      right: "关闭",
    };
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
    const pillW = Math.min(pw - 24, 100);
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, PANEL_Y + 6, pillW, 22);
    const title = makePixelText("天梯挑战", 12, CBE.cream);
    title.anchor.set(0.5, 0.5);
    title.x = LCD_WIDTH / 2;
    title.y = PANEL_Y + 17;
    this.addChild(title);
    this.labels.push(title);

    const st = this.status;
    const lines = [
      st?.enabled ? "状态：已开启" : "状态：未开启",
      `今日最高层：${st?.bestFloor ?? 0}`,
      `下一层：${st?.nextFloor ?? 1}`,
      `今日次数：${st?.dailyUsed ?? 0}/${st?.dailyLimit ?? 0}`,
      "对手：天机 / 幻剑 / 鬼道",
      "层数越高，怪物越强",
    ];
    let y = PANEL_Y + 38;
    for (const line of lines) {
      this.addLabel(line, PANEL_X + 14, y);
      y += 18;
    }

    drawPanelTipOverlay(this, this.toast);
  }
}
