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
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

const JOBS = ["天机", "幻剑", "鬼道"];
const BRACKETS = [10, 20, 30, 40, 50, 70];
const TAB_BG = 0x375946;
const LIST_PAGE = 8;

export type ArenaPanelAction = "claim" | "leave" | "close" | "refresh";

type TabFocus = "job" | "bracket";

/**
 * 比武排行：职业 × 段位标签 + 列表。任意玩家可切换查看各榜。
 */
export class ArenaPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private page: ArenaRankPage | null = null;
  private jobIndex = 0;
  private bracketIndex = 0;
  private listCursor = 0;
  private listScroll = 0;
  private tabFocus: TabFocus = "bracket";
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
    this.listScroll = 0;
    this.tabFocus = "bracket";
    this.visible = true;
    this.redraw();
  }

  setPage(page: ArenaRankPage | null): void {
    this.page = page;
    this.clampList();
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

  /** ↑↓：列表 / 底部操作 */
  move(dir: -1 | 1): void {
    if (!this.visible) return;
    const n = this.rowCount();
    if (n <= 0) return;
    this.listCursor = (this.listCursor + dir + n) % n;
    this.ensureListVisible();
    this.redraw();
  }

  /** ←→：切换当前焦点标签行（职业或段位） */
  horizontal(dir: -1 | 1): void {
    if (!this.visible) return;
    if (this.tabFocus === "job") this.switchJob(dir);
    else this.switchBracket(dir);
  }

  /** 左软键：在「职业 / 段位」焦点间切换 */
  toggleTabFocus(): void {
    this.tabFocus = this.tabFocus === "job" ? "bracket" : "job";
    this.redraw();
  }

  switchJob(dir: -1 | 1): void {
    this.jobIndex = (this.jobIndex + dir + JOBS.length) % JOBS.length;
    this.listCursor = 0;
    this.listScroll = 0;
    this.onBucketChange?.(this.job, this.bracket);
    this.redraw();
  }

  switchBracket(dir: -1 | 1): void {
    this.bracketIndex =
      (this.bracketIndex + dir + BRACKETS.length) % BRACKETS.length;
    this.listCursor = 0;
    this.listScroll = 0;
    this.onBucketChange?.(this.job, this.bracket);
    this.redraw();
  }

  selectJob(index: number): void {
    if (index < 0 || index >= JOBS.length || index === this.jobIndex) return;
    this.jobIndex = index;
    this.tabFocus = "job";
    this.listCursor = 0;
    this.listScroll = 0;
    this.onBucketChange?.(this.job, this.bracket);
    this.redraw();
  }

  selectBracket(index: number): void {
    if (index < 0 || index >= BRACKETS.length || index === this.bracketIndex)
      return;
    this.bracketIndex = index;
    this.tabFocus = "bracket";
    this.listCursor = 0;
    this.listScroll = 0;
    this.onBucketChange?.(this.job, this.bracket);
    this.redraw();
  }

  confirm(): void {
    if (!this.visible) return;
    const rows = this.page?.rows.length ?? 0;
    if (this.listCursor < rows) return;
    const actionIndex = this.listCursor - rows;
    if (actionIndex === 0) this.onAction?.("claim");
    else if (actionIndex === 1) this.onAction?.("leave");
    else this.onAction?.("close");
  }

  softkeys(): { left: string; mid: string; right: string } {
    return {
      left: this.tabFocus === "job" ? "段位" : "职业",
      mid: "确定",
      right: "关闭",
    };
  }

  private rowCount(): number {
    return (this.page?.rows.length ?? 0) + 3;
  }

  private clampList(): void {
    const n = this.rowCount();
    if (n <= 0) {
      this.listCursor = 0;
      this.listScroll = 0;
      return;
    }
    if (this.listCursor >= n) this.listCursor = n - 1;
    this.ensureListVisible();
  }

  private ensureListVisible(): void {
    const rows = this.page?.rows.length ?? 0;
    /* Only scroll the rank rows; action rows always stay in view below. */
    if (this.listCursor < rows) {
      if (this.listCursor < this.listScroll)
        this.listScroll = this.listCursor;
      if (this.listCursor >= this.listScroll + LIST_PAGE)
        this.listScroll = this.listCursor - LIST_PAGE + 1;
    }
  }

  private clearLabels(): void {
    for (const l of this.labels) l.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
  }

  private addLabel(text: string, x: number, y: number, color: number): void {
    const l = makePixelText(text, 12, color);
    l.x = x;
    l.y = y;
    this.addChild(l);
    this.labels.push(l);
  }

  private addLabelCentered(
    text: string,
    cx: number,
    cy: number,
    color: number,
  ): void {
    const t = new BitmapLabel();
    void t.setText(text, color).then(() => {
      t.position.set(cx - t.width / 2, cy - t.height / 2);
    });
    t.position.set(cx, cy);
    this.addChild(t);
    this.labels.push(t);
  }

  private drawTabs(
    x: number,
    y: number,
    w: number,
    h: number,
    labels: string[],
    activeIndex: number,
    focused: boolean,
    onPick: (i: number) => void,
  ): void {
    const n = labels.length;
    const gap = 2;
    const tw = Math.floor((w - gap * (n - 1)) / n);
    for (let i = 0; i < n; i++) {
      const label = labels[i]!;
      const tx = x + i * (tw + gap);
      const active = i === activeIndex;
      this.box
        .roundRect(tx, y, tw, h, 2)
        .fill({ color: active ? CBE.bgPanelDark : TAB_BG })
        .stroke({
          width: 1,
          color: active
            ? focused
              ? CBE.menuHi
              : CBE.borderBright
            : focused
              ? CBE.border
              : CBE.borderDim,
        });
      const text =
        active || label.length <= 2 ? label.slice(0, 4) : label.slice(0, 2);
      this.addLabelCentered(
        text,
        tx + tw / 2,
        y + h / 2,
        active ? CBE.ink : CBE.inkMint,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, tx, y, tw, h, () => onPick(idx)),
      );
    }
  }

  private redraw(): void {
    this.clearLabels();
    this.box.clear();
    drawCbePanel(this.box, 0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    drawCbeTitlePill(this.box, 8, 6, LCD_WIDTH - 16, 22);
    this.addLabel("比武排行榜", 16, 8, CBE.cream);

    const jobY = 32;
    const tabH = 18;
    this.drawTabs(
      8,
      jobY,
      LCD_WIDTH - 16,
      tabH,
      JOBS,
      this.jobIndex,
      this.tabFocus === "job",
      (i) => this.selectJob(i),
    );

    const brY = jobY + tabH + 4;
    const brLabels = BRACKETS.map((b) =>
      b === 70 ? "70" : String(b),
    );
    this.drawTabs(
      8,
      brY,
      LCD_WIDTH - 16,
      tabH,
      brLabels,
      this.bracketIndex,
      this.tabFocus === "bracket",
      (i) => this.selectBracket(i),
    );

    const bucketHint = `${JOBS[this.jobIndex]} · ${bracketLabel(this.bracket)}`;
    const infoY = brY + tabH + 4;
    this.addLabel(bucketHint, 12, infoY, CBE.inkMint);
    const mine = this.page
      ? `我:第${this.page.myRank || "-"} 分${this.page.myScore}${
          this.page.settled ? " 可领" : ""
        }`
      : "加载中…";
    this.addLabel(mine, 12, infoY + 14, CBE.inkLabel);
    this.addLabel(
      this.tabFocus === "job" ? "←→职业" : "←→段位",
      LCD_WIDTH - 64,
      infoY,
      CBE.inkTip,
    );

    const listTop = infoY + 32;
    const actionH = 3 * 14 + 4;
    const listBottom = LCD_HEIGHT - SOFTKEY_H - actionH - 8;
    const rows = this.page?.rows ?? [];
    if (!rows.length) {
      this.addLabel("(本段暂无积分榜)", 12, listTop + 4, CBE.inkMint);
    } else {
      const shown = rows.slice(this.listScroll, this.listScroll + LIST_PAGE);
      let y = listTop;
      for (let i = 0; i < shown.length; i++) {
        const abs = this.listScroll + i;
        const r = shown[i]!;
        const title =
          r.rank === 1
            ? "冠"
            : r.rank === 2
              ? "亚"
              : r.rank === 3
                ? "季"
                : `${r.rank}`;
        const line = `${title} ${r.name.slice(0, 8)} ${r.score}${
          r.claimed ? "✓" : ""
        }`;
        const hi = abs === this.listCursor;
        if (hi) {
          this.box
            .rect(8, y - 1, LCD_WIDTH - 16, 14)
            .fill({ color: CBE.select });
        }
        this.addLabel(line, 12, y, hi ? CBE.menuHi : CBE.ink);
        this.rowHits.push(
          attachRowPointerHit(this, 8, y - 1, LCD_WIDTH - 16, 14, () => {
            this.listCursor = abs;
            this.ensureListVisible();
            this.redraw();
          }),
        );
        y += 14;
        if (y > listBottom) break;
      }
      if (rows.length > LIST_PAGE) {
        const page =
          Math.floor(this.listScroll / LIST_PAGE) + 1;
        const pages = Math.ceil(rows.length / LIST_PAGE);
        this.addLabel(`↑↓ ${page}/${pages}`, 12, listBottom - 2, CBE.inkLabel);
      }
    }

    const actions = ["领取奖励", "离开擂台", "关闭"];
    let y = LCD_HEIGHT - SOFTKEY_H - actionH;
    for (let i = 0; i < actions.length; i++) {
      const idx = rows.length + i;
      const hi = idx === this.listCursor;
      if (hi) {
        this.box
          .rect(8, y - 1, LCD_WIDTH - 16, 14)
          .fill({ color: CBE.select });
      }
      this.addLabel(actions[i]!, 12, y, hi ? CBE.menuHi : CBE.inkBody);
      this.rowHits.push(
        attachRowPointerHit(this, 8, y - 1, LCD_WIDTH - 16, 14, () => {
          this.listCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
      y += 14;
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
