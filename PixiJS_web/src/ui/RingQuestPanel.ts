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
import type { RingStatus } from "../net/protocolActivities";
import type { SoftkeyLabels } from "./SoftkeyBar";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";

export type RingQuestPanelAction = "accept" | "submit" | "claim" | "close";

const PANEL_X = 8;
const PANEL_Y = 36;

export class RingQuestPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private status: RingStatus | null = null;
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: RingQuestPanelAction) => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.hitArea = new Rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    this.on("pointertap", (e) => e.stopPropagation());
    this.addChild(this.box);
    this.addChild(this.toast.host);
  }

  open(status: RingStatus | null): void {
    this.status = status;
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

  softkeys(): SoftkeyLabels {
    const st = this.status;
    if (!st) return { left: "", right: "关闭" };
    if (st.taskActive) return { left: "提交", right: "关闭" };
    if (st.ringCount >= st.dailyMax && !st.bonusClaimed)
      return { left: "领5玄", right: "关闭" };
    if (st.ringCount < st.dailyMax) return { left: "接环", right: "关闭" };
    return { left: "", right: "关闭" };
  }

  confirm(): void {
    const st = this.status;
    if (!st) return;
    if (st.taskActive) this.onAction?.("submit");
    else if (st.ringCount >= st.dailyMax && !st.bonusClaimed) this.onAction?.("claim");
    else if (st.ringCount < st.dailyMax) this.onAction?.("accept");
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
    const title = makePixelText("跑环任务", 12, CBE.cream);
    title.anchor.set(0.5, 0.5);
    title.x = LCD_WIDTH / 2;
    title.y = PANEL_Y + 17;
    this.addChild(title);
    this.labels.push(title);

    const st = this.status;
    const lines = [
      `今日：${st?.ringCount ?? 0}/${st?.dailyMax ?? 20}环`,
      st?.bonusClaimed ? "满环奖励：已领" : "满环奖励：未领(20环1个5玄)",
      st?.taskActive
        ? `任务：${st.desc || "进行中"}`
        : st && st.ringCount >= st.dailyMax
          ? "今日环数已满"
          : "左键接取随机环任务",
      st?.taskActive
        ? `进度：${st.progress}/${st.targetCount || 1}`
        : "奖励：经验/金币/低概率钥匙",
    ];
    let y = PANEL_Y + 38;
    for (const line of lines) {
      this.addLabel(line, PANEL_X + 14, y);
      y += 17;
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
