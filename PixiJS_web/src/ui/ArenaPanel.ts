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
import {
  bracketLabel,
  type ArenaRankPage,
} from "../net/protocolArena";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  type PanelTipState,
} from "./panelTip";

const JOBS = ["天机", "幻剑", "鬼道"];
const BRACKETS = [10, 20, 30, 40, 50, 70];

export type ArenaPanelAction = "claim" | "leave" | "close" | "refresh";

/**
 * 比武排行 / 领奖面板（LCD 列表风格）。
 */
export class ArenaPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private page: ArenaRankPage | null = null;
  private jobIndex = 0;
  private bracketIndex = 0;
  private listCursor = 0;
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: ArenaPanelAction) => void) | null = null;
  onBucketChange: ((job: number, bracket: number) => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(this.box);
  }

  get job(): number {
    return this.jobIndex + 1;
  }

  get bracket(): number {
    return BRACKETS[this.bracketIndex] ?? 10;
  }

  open(page: ArenaRankPage | null, job?: number, bracket?: number): void {
    if (job && job >= 1 && job <= 3) this.jobIndex = job - 1;
    if (bracket) {
      const i = BRACKETS.indexOf(bracket);
      if (i >= 0) this.bracketIndex = i;
    }
    this.page = page;
    this.listCursor = 0;
    this.visible = true;
    this.redraw();
  }

  setPage(page: ArenaRankPage | null): void {
    this.page = page;
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

  move(dir: -1 | 1): void {
    if (!this.visible) return;
    const n = (this.page?.rows.length ?? 0) + 3; /* rows + claim/leave/close */
    this.listCursor = (this.listCursor + dir + n) % n;
    this.redraw();
  }

  switchJob(dir: -1 | 1): void {
    this.jobIndex = (this.jobIndex + dir + 3) % 3;
    this.listCursor = 0;
    this.onBucketChange?.(this.job, this.bracket);
    this.redraw();
  }

  switchBracket(dir: -1 | 1): void {
    this.bracketIndex = (this.bracketIndex + dir + BRACKETS.length) % BRACKETS.length;
    this.listCursor = 0;
    this.onBucketChange?.(this.job, this.bracket);
    this.redraw();
  }

  confirm(): void {
    if (!this.visible) return;
    const rowCount = this.page?.rows.length ?? 0;
    if (this.listCursor < rowCount) return;
    const actionIndex = this.listCursor - rowCount;
    if (actionIndex === 0) this.onAction?.("claim");
    else if (actionIndex === 1) this.onAction?.("leave");
    else this.onAction?.("close");
  }

  softkeys(): { left: string; mid: string; right: string } {
    return { left: "切换", mid: "确定", right: "关闭" };
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
    this.addLabel("比武排行榜", 16, 8, CBE.cream);

    const bucket = `${JOBS[this.jobIndex]} ${bracketLabel(this.bracket)}`;
    this.addLabel(bucket, 12, 32, CBE.inkMint);
    const mine = this.page
      ? `我:第${this.page.myRank || "-"}名 分${this.page.myScore}${
          this.page.settled ? " 可领奖" : ""
        }`
      : "加载中…";
    this.addLabel(mine, 12, 48, CBE.inkLabel);

    const rows = this.page?.rows ?? [];
    let y = 68;
    for (let i = 0; i < rows.length; i++) {
      const r = rows[i]!;
      const title =
        r.rank === 1 ? "冠" : r.rank === 2 ? "亚" : r.rank === 3 ? "季" : `${r.rank}`;
      const line = `${title} ${r.name} ${r.score}${r.claimed ? " 已领" : ""}`;
      const hi = i === this.listCursor;
      if (hi) {
        this.box.rect(8, y - 1, LCD_WIDTH - 16, 14).fill({ color: CBE.select });
      }
      this.addLabel(line, 12, y, hi ? CBE.menuHi : CBE.ink);
      y += 14;
    }

    const actions = ["领取奖励", "离开擂台", "关闭"];
    y = Math.max(y + 6, LCD_HEIGHT - SOFTKEY_H - 52);
    for (let i = 0; i < actions.length; i++) {
      const idx = rows.length + i;
      const hi = idx === this.listCursor;
      if (hi) {
        this.box.rect(8, y - 1, LCD_WIDTH - 16, 14).fill({ color: CBE.select });
      }
      this.addLabel(actions[i]!, 12, y, hi ? CBE.menuHi : CBE.inkBody);
      y += 14;
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
