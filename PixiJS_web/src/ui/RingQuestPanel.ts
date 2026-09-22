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

const DESC_CHARS_PER_LINE = 14;

/** Mirror server VM_MOCK_RING_TYPE_* */
const RING_TYPE_VISIT = 1;
const RING_TYPE_KILL_MOB = 2;
const RING_TYPE_KILL_BOSS = 3;
const RING_TYPE_KILL_PLAYER = 4;
const RING_TYPE_COLLECT = 5;
const RING_TYPE_SUBMIT_MED = 6;
const RING_TYPE_WORLD_TRUTH = 8;
const RING_MED_PER_GROUP = 10;

function wrapText(text: string, maxChars: number): string[] {
  if (!text) return [];
  const lines: string[] = [];
  let cur = "";
  for (const ch of text) {
    cur += ch;
    if (cur.length >= maxChars) {
      lines.push(cur);
      cur = "";
    }
  }
  if (cur) lines.push(cur);
  return lines;
}

function progressLine(st: RingStatus): string {
  const target = st.targetCount || 1;
  switch (st.taskType) {
    case RING_TYPE_VISIT:
      return st.progress >= 1 ? "进度：已对话" : "进度：未对话(找NPC对话)";
    case RING_TYPE_KILL_MOB:
    case RING_TYPE_KILL_BOSS:
      return `进度：${st.progress}/${target}场`;
    case RING_TYPE_KILL_PLAYER:
      return `进度：${st.progress}/${target}名`;
    case RING_TYPE_COLLECT:
      return `进度：需${target}个(背包提交)`;
    case RING_TYPE_SUBMIT_MED: {
      const need = target * RING_MED_PER_GROUP;
      return `需交：${target}组(每组${RING_MED_PER_GROUP}个,共${need}个)`;
    }
    case RING_TYPE_WORLD_TRUTH:
      return st.progress >= 1 ? "进度：已发送(可提交)" : "进度：未发送(世界频道)";
    default:
      return `进度：${st.progress}/${target}`;
  }
}

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
    const title = makePixelText("跑环任务", 12, CBE.cream);
    title.anchor.set(0.5, 0.5);
    title.x = LCD_WIDTH / 2;
    title.y = chrome.titleCy;
    this.addChild(title);
    this.labels.push(title);

    const st = this.status;
    const lines: string[] = [
      `今日：${st?.ringCount ?? 0}/${st?.dailyMax ?? 20}环`,
      st?.bonusClaimed ? "满环奖励：已领" : "满环奖励：未领(20环1个5玄)",
    ];
    if (st?.taskActive) {
      const desc = st.desc || "进行中";
      const wrapped = wrapText(`任务：${desc}`, DESC_CHARS_PER_LINE);
      lines.push(...(wrapped.length ? wrapped : ["任务：进行中"]));
      /* 进度与任务描述隔开 3 行，避免长描述视觉重叠 */
      lines.push("", "", "");
      lines.push(progressLine(st));
    } else if (st && st.ringCount >= st.dailyMax) {
      lines.push("今日环数已满");
      lines.push("奖励：经验/金币/低概率钥匙");
    } else {
      lines.push("左键接取随机环任务");
      lines.push("奖励：经验/金币/低概率钥匙");
    }
    let y = chrome.bodyY + 8;
    for (const line of lines) {
      this.addLabel(line, PANEL_X + 14, y);
      y += 15;
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
