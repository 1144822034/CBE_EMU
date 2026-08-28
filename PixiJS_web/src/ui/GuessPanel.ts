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
import {
  GUESS_CHOICE,
  GUESS_MAX_GOLD,
  GUESS_MIN_GOLD,
  GUESS_PAGE_SIZE,
  formatGuessDiceWithHints,
  formatGuessRemain,
  formatGuessStakeGold,
  guessChoiceLabel,
  type GuessHistoryRow,
  type GuessStatus,
} from "../net/protocolGuess";
import { COPPER_PER_GOLD } from "../net/protocolAuction";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { attachPageNavButtons } from "./pageNav";
import type { SoftkeyLabels } from "./SoftkeyBar";

export type GuessPanelAction =
  | "bet"
  | "claim"
  | "refresh"
  | "close"
  | "prev_page"
  | "next_page";

type GuessTab = "bet" | "history";

const PANEL_X = 6;
const PANEL_Y = 28;
const TAB_H = 18;
const TAB_BG = 0x375946;

const CHOICE_OPTIONS: { id: number; label: string }[] = [
  { id: GUESS_CHOICE.BIG, label: "大" },
  { id: GUESS_CHOICE.SMALL, label: "小" },
  { id: GUESS_CHOICE.ODD, label: "单" },
  { id: GUESS_CHOICE.EVEN, label: "双" },
  { id: GUESS_CHOICE.BAOZI, label: "豹子" },
];

/** 押注选项行：大/小、单/双、豹子. */
const CHOICE_ROW_INDICES: number[][] = [
  [0, 1],
  [2, 3],
  [4],
];

/** UC3 12px 字模显示高约 14px，行距须大于字高以免与下一行控件重叠. */
const TEXT_LINE = 16;
const SECTION_GAP = 6;

function choiceGridPos(cursor: number): { row: number; col: number } {
  if (cursor <= 1) return { row: 0, col: cursor };
  if (cursor <= 3) return { row: 1, col: cursor - 2 };
  return { row: 2, col: 0 };
}

/**
 * 娱乐「猜猜看」：30 分钟一轮，猜三骰 大/小/单/双/豹子。
 */
export class GuessPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private tab: GuessTab = "bet";
  private status: GuessStatus | null = null;
  private history: GuessHistoryRow[] = [];
  private historyPage = 0;
  private historyHasMore = false;
  private choiceCursor = 0;
  private listCursor = 0;
  /** LCD 数字键盘草稿（非 HTML 输入）. */
  private stakeDraft = String(GUESS_MIN_GOLD);
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: GuessPanelAction, betId?: number) => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.hitArea = new Rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    this.on("pointertap", (e) => e.stopPropagation());
    this.addChild(this.box);
    this.addChild(this.toast.host);
  }

  open(status: GuessStatus | null, history: GuessHistoryRow[], page = 0, hasMore = false): void {
    this.status = status;
    this.history = history;
    this.historyPage = page;
    this.historyHasMore = hasMore;
    this.stakeDraft = String(status?.minGold ?? GUESS_MIN_GOLD);
    this.visible = true;
    this.redraw();
  }

  setBoard(
    status: GuessStatus | null,
    history: GuessHistoryRow[],
    page = 0,
    hasMore = false,
  ): void {
    this.status = status;
    this.history = history;
    this.historyPage = page;
    this.historyHasMore = hasMore;
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

  get currentTab(): GuessTab {
    return this.tab;
  }

  private stakeLimits(): { min: number; max: number } {
    return {
      min: this.status?.minGold ?? GUESS_MIN_GOLD,
      max: this.status?.maxGold ?? GUESS_MAX_GOLD,
    };
  }

  private parsedStakeGold(): number {
    const { min, max } = this.stakeLimits();
    let n = Math.floor(Number(this.stakeDraft.trim() || "0"));
    if (!Number.isFinite(n)) n = min;
    return Math.min(max, Math.max(min, n));
  }

  isStakeDraftValid(): boolean {
    const raw = this.stakeDraft.trim();
    if (!raw) return false;
    const n = Math.floor(Number(raw));
    const { min, max } = this.stakeLimits();
    return Number.isFinite(n) && n >= min && n <= max;
  }

  get stakeCopper(): number {
    return this.parsedStakeGold() * COPPER_PER_GOLD;
  }

  get stakeGoldAmount(): number {
    return this.parsedStakeGold();
  }

  /** 物理键盘 0-9 / Backspace（押注页）. */
  handleStakeKey(key: string): boolean {
    if (!this.visible || this.tab !== "bet") return false;
    if (key === "Backspace") {
      this.appendStakeKey("删除");
      return true;
    }
    if (/^\d$/.test(key)) {
      this.appendStakeKey(key);
      return true;
    }
    return false;
  }

  appendStakeKey(key: string): void {
    const { min, max } = this.stakeLimits();
    if (key === "删除" || key === "删") {
      this.stakeDraft =
        this.stakeDraft.length <= 1 ? "" : this.stakeDraft.slice(0, -1);
      this.redraw();
      return;
    }
    if (key === "清空" || key === "清") {
      this.stakeDraft = "";
      this.redraw();
      return;
    }
    if (!/^\d$/.test(key)) return;
    const merged =
      this.stakeDraft === "0" || this.stakeDraft === ""
        ? key
        : this.stakeDraft + key;
    const n = Math.floor(Number(merged));
    if (!Number.isFinite(n) || n > max || merged.length > 3) return;
    this.stakeDraft = merged;
    this.redraw();
  }

  get selectedChoice(): number {
    return CHOICE_OPTIONS[this.choiceCursor]?.id ?? GUESS_CHOICE.BIG;
  }

  adjustStake(deltaGold: number): void {
    const { min, max } = this.stakeLimits();
    const next = Math.min(
      max,
      Math.max(min, this.parsedStakeGold() + deltaGold),
    );
    this.stakeDraft = String(next);
    this.redraw();
  }

  move(delta: number): void {
    if (this.tab === "bet") {
      const { row, col } = choiceGridPos(this.choiceCursor);
      const rowCount = CHOICE_ROW_INDICES.length;
      const nextRow = (row + delta + rowCount) % rowCount;
      const rowIndices = CHOICE_ROW_INDICES[nextRow]!;
      const nextCol = Math.min(col, rowIndices.length - 1);
      this.choiceCursor = rowIndices[nextCol]!;
    } else {
      const n = this.history.length;
      if (n <= 0) return;
      this.listCursor = (this.listCursor + delta + n) % n;
    }
    this.redraw();
  }

  /** ←→ 在同排选项间切换；豹子行返回 false 以便微调押金. */
  horizontal(delta: number): boolean {
    if (this.tab !== "bet") return false;
    const { row, col } = choiceGridPos(this.choiceCursor);
    const rowIndices = CHOICE_ROW_INDICES[row]!;
    if (rowIndices.length <= 1) return false;
    const nextCol = (col + delta + rowIndices.length) % rowIndices.length;
    this.choiceCursor = rowIndices[nextCol]!;
    this.redraw();
    return true;
  }

  confirm(): void {
    if (!this.visible) return;
    if (this.tab === "bet") {
      if (!this.isStakeDraftValid()) {
        this.showTip(`请输入${this.stakeLimits().min}-${this.stakeLimits().max}金`);
        return;
      }
      this.onAction?.("bet");
      return;
    }
    const row = this.history[this.listCursor];
    if (row?.win && !row.claimed && row.payoutCopper > 0) {
      this.onAction?.("claim", row.betId);
    }
  }

  softkeys(): SoftkeyLabels {
    if (this.tab === "bet") {
      const open = this.status?.bettingOpen;
      return {
        left: open ? "押注" : "刷新",
        right: "关闭",
      };
    }
    const row = this.history[this.listCursor];
    const canClaim = !!row?.win && !row.claimed && row.payoutCopper > 0;
    return {
      left: canClaim ? "领奖" : "刷新",
      right: "关闭",
    };
  }

  private clearLabels(): void {
    for (const l of this.labels) l.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
  }

  private addLabel(text: string, x: number, y: number, fill = CBE.inkBody): void {
    const lbl = makePixelText(text, 12, fill);
    lbl.x = x;
    lbl.y = y;
    this.addChild(lbl);
    this.labels.push(lbl);
  }

  private addLabelCentered(
    text: string,
    cx: number,
    cy: number,
    fill = CBE.inkBody,
  ): void {
    const t = new BitmapLabel();
    t.anchor.set(0.5, 0.5);
    t.position.set(Math.round(cx), Math.round(cy));
    void t.setText(text, fill);
    this.addChild(t);
    this.labels.push(t);
  }

  private drawTabs(x: number, y: number, w: number): number {
    const labels = ["押注", "记录"];
    const tabs: GuessTab[] = ["bet", "history"];
    const n = labels.length;
    const gap = 2;
    const tw = Math.floor((w - gap * (n - 1)) / n);
    for (let i = 0; i < n; i++) {
      const tx = x + i * (tw + gap);
      const active = tabs[i] === this.tab;
      this.box
        .roundRect(tx, y, tw, TAB_H, 2)
        .fill({ color: active ? CBE.bgPanelDark : TAB_BG })
        .stroke({
          width: 1,
          color: active ? CBE.menuHi : CBE.borderDim,
        });
      this.addLabelCentered(
        labels[i]!,
        tx + tw / 2,
        y + TAB_H / 2,
        active ? CBE.ink : CBE.inkMint,
      );
      const picked = tabs[i]!;
      this.rowHits.push(
        attachRowPointerHit(this, tx, y, tw, TAB_H, () => {
          this.tab = picked;
          this.listCursor = 0;
          this.redraw();
          this.onAction?.("refresh");
        }),
      );
    }
    return y + TAB_H + 6;
  }

  private historyPages(): number {
    const extra = this.historyHasMore ? 1 : 0;
    return Math.max(1, this.historyPage + 1 + extra);
  }

  private drawBetBody(x: number, y: number, w: number, bottom: number): void {
    const st = this.status;
    const { min, max } = this.stakeLimits();
    let cy = y;
    this.addLabel("猜猜看天梯时段通过职业数", x, cy, CBE.inkLabel);
    cy += TEXT_LINE;
    const periodLine = st?.roundDrawn
      ? `第${st.roundId}期 开${formatGuessDiceWithHints(st.dice1, st.dice2, st.dice3)}`
      : `第${st?.roundId ?? "?"}期 剩${formatGuessRemain(st?.remainSec ?? 0)}`;
    this.addLabel(periodLine, x, cy, CBE.inkMint);
    cy += TEXT_LINE;
    if (!st?.roundDrawn) {
      if (st?.bettingOpen) {
        this.addLabel(
          `可押注 ${formatGuessRemain(st.lockRemainSec)}`,
          x,
          cy,
          CBE.inkMint,
        );
      } else {
        this.addLabel("开奖前30秒禁押", x, cy, 0xff8866);
      }
      cy += TEXT_LINE;
    }
    cy += 2;
    this.addLabel("选项:", x, cy, CBE.inkLabel);
    cy += TEXT_LINE;
    cy = this.drawChoiceGrid(x, cy, w);
    cy += SECTION_GAP;
    cy = this.drawStakeKeypad(x, cy, w, bottom, min, max, st);
  }

  /** 大/小、单/双、豹子 三行选项. */
  private drawChoiceGrid(x: number, y: number, w: number): number {
    const rowH = 18;
    const gapY = 3;
    const gapX = 6;
    let cy = y;

    for (let r = 0; r < CHOICE_ROW_INDICES.length; r++) {
      const indices = CHOICE_ROW_INDICES[r]!;
      const n = indices.length;
      const totalGap = gapX * (n - 1);
      const cellW = Math.floor((w - 8 - totalGap) / n);

      for (let c = 0; c < n; c++) {
        const idx = indices[c]!;
        const opt = CHOICE_OPTIONS[idx]!;
        const active = this.choiceCursor === idx;
        let cx: number;
        let cw = cellW;
        if (n === 1) {
          cw = Math.min(100, cellW * 2);
          cx = x + Math.floor((w - cw) / 2);
        } else {
          cx = x + 4 + c * (cellW + gapX);
        }
        if (active) {
          this.box
            .roundRect(cx, cy, cw, rowH, 2)
            .fill({ color: 0x2a4038 })
            .stroke({ width: 1, color: CBE.borderBright });
        } else {
          this.box
            .roundRect(cx, cy, cw, rowH, 2)
            .fill({ color: CBE.bgPanelDark })
            .stroke({ width: 1, color: CBE.borderDim });
        }
        this.addLabelCentered(
          opt.label,
          cx + cw / 2,
          cy + rowH / 2,
          active ? CBE.menuHi : CBE.inkBody,
        );
        const pickIdx = idx;
        this.rowHits.push(
          attachRowPointerHit(this, cx, cy, cw, rowH, () => {
            this.choiceCursor = pickIdx;
            this.redraw();
          }),
        );
      }
      cy += rowH + gapY;
    }
    return cy;
  }

  /**
   * 押金 LCD 数字键盘：←→ ±1 金，点按数字/删/清，不用 HTML 输入。
   */
  private drawStakeKeypad(
    x: number,
    y: number,
    w: number,
    bottom: number,
    min: number,
    max: number,
    st: GuessStatus | null,
  ): number {
    let cy = y;
    this.addLabel("押金(金) ←→微调", x, cy, CBE.inkLabel);
    cy += TEXT_LINE;
    const boxH = 22;
    this.box
      .roundRect(x + 4, cy, w - 8, boxH, 3)
      .fill({ color: CBE.bgPanel })
      .stroke({ width: 1, color: CBE.borderBright });
    const shown = (this.stakeDraft || "").trim() || "0";
    this.addLabelCentered(
      `${shown} 金 (${min}-${max})`,
      LCD_WIDTH / 2,
      cy + boxH / 2,
      CBE.inkTipGreen,
    );
    cy += boxH + SECTION_GAP;
    this.addLabel(
      `手续费${st?.feePercent ?? 8}% 豹子30倍 其余2倍`,
      x,
      cy,
      CBE.inkLabel,
    );
    cy += TEXT_LINE + 4;

    const keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "删", "0", "清"];
    const keyW = 50;
    const keyH = 17;
    const gap = 3;
    const gridW = keyW * 3 + gap * 2;
    const startX = x + Math.floor((w - gridW) / 2);
    for (let i = 0; i < keys.length; i++) {
      const col = i % 3;
      const row = Math.floor(i / 3);
      const kx = startX + col * (keyW + gap);
      const ky = cy + row * (keyH + gap);
      if (ky + keyH > bottom - 4) break;
      const key = keys[i]!;
      const keyInk =
        key === "清" ? CBE.inkWarn : key === "删" ? CBE.inkLabel : CBE.inkMint;
      this.box
        .roundRect(kx, ky, keyW, keyH, 2)
        .fill({ color: CBE.bgDeep })
        .stroke({ width: 1, color: CBE.borderDim });
      this.addLabelCentered(key, kx + keyW / 2, ky + keyH / 2, keyInk);
      this.rowHits.push(
        attachRowPointerHit(this, kx, ky, keyW, keyH, () => {
          this.appendStakeKey(key === "删" ? "删除" : key === "清" ? "清空" : key);
        }),
      );
    }
    return cy + 4 * (keyH + gap);
  }

  private drawHistoryBody(x: number, y: number, w: number, bottom: number): void {
    const st = this.status;
    let cy = y;
    const rowH = 18;
    const rowStride = rowH + 2;
    if ((st?.pendingClaims ?? 0) > 0) {
      this.addLabel(`待领 ${st!.pendingClaims} 笔`, x, cy, CBE.inkMint);
      cy += TEXT_LINE;
    }
    if (this.history.length === 0) {
      this.addLabel("(暂无押注记录)", x, cy, CBE.inkLabel);
      cy += TEXT_LINE;
    } else {
      let drawn = 0;
      for (let i = 0; i < this.history.length; i++) {
        const rowY = cy + i * rowStride;
        if (rowY + rowH > bottom - 24) break;
        drawn++;
        const row = this.history[i]!;
        const active = i === this.listCursor;
        this.box
          .roundRect(x - 2, rowY, w + 4, rowH, 2)
          .fill({ color: active ? 0x2a4038 : CBE.bgPanelDark })
          .stroke({
            width: 1,
            color: active ? CBE.borderBright : CBE.borderDim,
          });
        const stake = formatGuessStakeGold(row.stakeCopper);
        const dice = row.roundDrawn
          ? formatGuessDiceWithHints(row.dice1, row.dice2, row.dice3)
          : "待开";
        const result = !row.roundDrawn
          ? "等待"
          : row.win
            ? row.claimed
              ? "已领"
              : `中${formatGuessStakeGold(row.payoutCopper)}`
            : "未中";
        const line = `#${row.betId} ${guessChoiceLabel(row.choice)} ${stake} ${dice} ${result}`;
        const ink =
          row.win && !row.claimed
            ? CBE.inkMint
            : active
              ? CBE.ink
              : CBE.inkBody;
        this.addLabel(line, x + 4, rowY + 2, ink);
        this.rowHits.push(
          attachRowPointerHit(this, x - 2, rowY, w + 4, rowH, () => {
            this.listCursor = i;
            this.redraw();
          }),
        );
      }
      cy += drawn * rowStride + SECTION_GAP;
    }
    const pages = this.historyPages();
    if (pages > 1 && cy + 20 < bottom) {
      attachPageNavButtons({
        host: this,
        hits: this.rowHits,
        box: this.box,
        x,
        y: cy,
        w,
        page: this.historyPage + 1,
        pages,
        addLabel: (text, lx, ly, color) => this.addLabel(text, lx, ly, color),
        onPrev: () => {
          if (this.historyPage > 0) this.onAction?.("prev_page");
        },
        onNext: () => {
          if (this.historyHasMore) this.onAction?.("next_page");
        },
      });
    }
  }

  private redraw(): void {
    this.clearLabels();
    this.box.clear();
    this.box.removeChildren();

    const pw = LCD_WIDTH - PANEL_X * 2;
    const ph = LCD_HEIGHT - SOFTKEY_H - PANEL_Y - 6;
    paintCbePanel(this, this.box, PANEL_X, PANEL_Y, pw, ph, CBE.bgDeep);
    const pillW = Math.min(pw - 24, 88);
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, PANEL_Y + 6, pillW, 22);
    const title = makePixelText("猜猜看", 12, CBE.cream);
    title.anchor.set(0.5, 0.5);
    title.x = LCD_WIDTH / 2;
    title.y = PANEL_Y + 17;
    this.addChild(title);
    this.labels.push(title);

    const innerX = PANEL_X + 8;
    const innerW = pw - 16;
    let bodyTop = PANEL_Y + 34;
    bodyTop = this.drawTabs(innerX, bodyTop, innerW);
    const bodyBottom = PANEL_Y + ph - 8;
    if (this.tab === "bet") this.drawBetBody(innerX, bodyTop, innerW, bodyBottom);
    else this.drawHistoryBody(innerX, bodyTop, innerW, bodyBottom);

    drawPanelTipOverlay(this, this.toast);
  }
}
