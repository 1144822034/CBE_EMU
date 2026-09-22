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
import {
  bracketLabel,
  type ArenaRankPage,
} from "../net/protocolArena";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import {
  paintListShellBody,
} from "./listPanelShell";

const JOBS = ["天机", "幻剑", "鬼道"];
/** 10/50/70 = 分职业段位；77 = 周末70大乱斗（全职业一榜）. */
const BRACKETS = [10, 50, 70, 77];
const TAB_BG = 0x375946;
/** Single-line rank rows. */
const LIST_PAGE = 8;
/** +5 vs old list-shell default 14 that clipped reward mid-name. */
const ROW_NAME_MAX = 19;
/** Reward fragment inside the one-line row (+5 vs old slice 6). */
const REWARD_FRAG_MAX = 11;

/** Rank reward text; summary already embeds qty (x/×/、). */
function formatArenaRewardLine(name: string, count: number): string {
  const n = (name || "").trim();
  if (!n) return "";
  if (/[x×、]/.test(n) || count <= 0) return n;
  return `${n}×${count}`;
}

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
    /* Swallow clicks so NPC dialog / approach under world overlays stay inert. */
    this.eventMode = "static";
    this.hitArea = new Rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H);
    this.on("pointertap", (e) => {
      e.stopPropagation();
    });
    this.addChild(this.box);
  }

  get job(): number {
    /* FFA board uses job=0 on the wire. */
    if (this.bracket === 77) return 0;
    return this.jobIndex + 1;
  }

  get bracket(): number {
    return BRACKETS[this.bracketIndex] ?? 10;
  }

  get isFfa(): boolean {
    return this.bracket === 77;
  }

  open(page: ArenaRankPage | null, job?: number, bracket?: number): void {
    if (bracket) {
      const i = BRACKETS.indexOf(bracket);
      if (i >= 0) this.bracketIndex = i;
    }
    if (this.bracket === 77) {
      /* keep jobIndex for UI cycle after leaving FFA */
    } else if (job && job >= 1 && job <= 3) {
      this.jobIndex = job - 1;
    }
    this.page = page;
    this.listCursor = 0;
    this.listScroll = 0;
    this.tabFocus = this.bracket === 77 ? "bracket" : "bracket";
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
    clearPointerHits(this.rowHits);
    this.visible = false;
  }

  /** Soft-right: dismiss toast first; true = stay on panel. */
  dismissTipOnSoftRight(): boolean {
    return tryDismissPanelTipOnSoftRight(this.toast);
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

  /** ←→：列表时切职业/段位；落在底部操作行时在三按钮间切换 */
  horizontal(dir: -1 | 1): void {
    if (!this.visible) return;
    const rows = this.page?.rows.length ?? 0;
    if (this.listCursor >= rows) {
      const i = this.listCursor - rows;
      this.listCursor = rows + ((i + dir + 3) % 3);
      this.redraw();
      return;
    }
    if (this.tabFocus === "job") this.switchJob(dir);
    else this.switchBracket(dir);
  }

  /** 左软键：在「职业 / 段位」焦点间切换（大乱斗无职业榜） */
  toggleTabFocus(): void {
    if (this.isFfa) {
      this.tabFocus = "bracket";
      this.redraw();
      return;
    }
    this.tabFocus = this.tabFocus === "job" ? "bracket" : "job";
    this.redraw();
  }

  switchJob(dir: -1 | 1): void {
    if (this.isFfa) {
      this.switchBracket(dir);
      return;
    }
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
    if (this.isFfa) {
      return { left: "段位", mid: "确定", right: "关闭" };
    }
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
    /* Only scroll the rank rows; action rows always stay in view below.
     * Keep listScroll page-aligned so 上一页/下一页 page index stays correct
     * when the last page has fewer than LIST_PAGE rows. */
    if (this.listCursor < rows) {
      this.listScroll = Math.floor(this.listCursor / LIST_PAGE) * LIST_PAGE;
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
    t.anchor.set(0.5, 0.5);
    t.position.set(Math.round(cx), Math.round(cy));
    // Don't recenter in setText().then — redraw/clearLabels can destroy t first.
    void t.setText(text, color);
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
    const { x, y, w, h } = cbePopupBounds();
    const chrome = paintCbeTitledChrome(this, this.box, x, y, w, h);
    this.addLabelCentered("比武排行榜", LCD_WIDTH / 2, chrome.titleCy, CBE.cream);

    const jobY = chrome.bodyY + 6;
    const tabH = 18;
    const tabX = x + 4;
    const tabW = w - 8;
    if (this.isFfa) {
      this.drawTabs(
        tabX,
        jobY,
        tabW,
        tabH,
        ["全职业"],
        0,
        false,
        () => undefined,
      );
    } else {
      this.drawTabs(
        tabX,
        jobY,
        tabW,
        tabH,
        JOBS,
        this.jobIndex,
        this.tabFocus === "job",
        (i) => this.selectJob(i),
      );
    }

    const brY = jobY + tabH + 4;
    const brLabels = BRACKETS.map((b) =>
      b === 77 ? "乱斗" : b === 70 ? "70" : bracketLabel(b),
    );
    this.drawTabs(
      tabX,
      brY,
      tabW,
      tabH,
      brLabels,
      this.bracketIndex,
      this.tabFocus === "bracket",
      (i) => this.selectBracket(i),
    );

    const bucketHint = this.isFfa
      ? `大乱斗 · ${bracketLabel(this.bracket)}`
      : `${JOBS[this.jobIndex]} · ${bracketLabel(this.bracket)}`;
    const infoY = brY + tabH + 4;
    this.addLabel(bucketHint, x + 8, infoY, CBE.inkMint);
    const mineReward = formatArenaRewardLine(
      this.page?.myRewardName || "",
      this.page?.myRewardCount || 0,
    );
    const claimMark = this.page?.myClaimed
      ? " 已领取"
      : this.page?.settled
        ? " 可领"
        : "";
    const mineRewardFrag = mineReward
      ? ` ${mineReward.slice(0, REWARD_FRAG_MAX)}`
      : "";
    const mine = this.page
      ? `我:第${this.page.myRank || "-"} 分${this.page.myScore}${claimMark}${mineRewardFrag}`
      : "加载中…";
    this.addLabel(mine, x + 8, infoY + 14, CBE.inkLabel);
    this.addLabel(
      this.tabFocus === "job" ? "←→职业" : "←→段位",
      LCD_WIDTH - 64,
      infoY,
      CBE.inkTip,
    );

    const listTop = infoY + 32;
    /* One row of 领取奖励|离开擂台|关闭. */
    const ACTION_BTN_H = 18;
    const actionH = ACTION_BTN_H + 6;
    const rows = this.page?.rows ?? [];
    const pages = Math.max(1, Math.ceil(Math.max(1, rows.length) / LIST_PAGE));
    /* Page-nav strip (~18px) must sit above action row; old footY overlapped claim hits. */
    const navReserve = pages > 1 ? 22 : 0;
    const listBottom = LCD_HEIGHT - SOFTKEY_H - actionH - 8 - navReserve;
    const shellRows = rows.map((r) => {
      const title =
        r.rank === 1
          ? "冠"
          : r.rank === 2
            ? "亚"
            : r.rank === 3
              ? "季"
              : `${r.rank}`;
      const reward = formatArenaRewardLine(r.rewardName, r.rewardCount);
      const rewardFrag = reward ? ` ${reward.slice(0, REWARD_FRAG_MAX)}` : "";
      return {
        name: `${title} ${r.name.slice(0, 6)} ${r.score}${rewardFrag}${
          r.claimed ? "✓" : ""
        }`,
      };
    });
    const pageStart = this.listScroll;
    const page = rows.length ? Math.floor(pageStart / LIST_PAGE) + 1 : 1;
    const shown = shellRows.slice(pageStart, pageStart + LIST_PAGE);
    const paintCursor =
      this.listCursor < rows.length ? this.listCursor - pageStart : -1;
    paintListShellBody({
      ctx: {
        host: this,
        box: this.box,
        hits: this.rowHits,
        addLabel: (t, lx, ly, c) => this.addLabel(t, lx, ly, c),
        addLabelCentered: (t, cx, cy, c) => this.addLabelCentered(t, cx, cy, c),
      },
      x: 0,
      listTop,
      w: LCD_WIDTH,
      footY: listBottom - 2,
      rows: shown,
      cursor: paintCursor,
      pageSize: LIST_PAGE,
      rowH: 14,
      nameMax: ROW_NAME_MAX,
      emptyText: "(本段暂无积分榜)",
      paging: "server",
      serverPage: page,
      serverPages: pages,
      onRow: (i) => {
        this.listCursor = pageStart + i;
        this.ensureListVisible();
        this.redraw();
      },
      onPrevPage: () => {
        const cur = Math.floor(this.listScroll / LIST_PAGE);
        this.listScroll = Math.max(0, (cur - 1) * LIST_PAGE);
        this.listCursor = Math.min(
          this.listCursor,
          this.listScroll + LIST_PAGE - 1,
        );
        this.listCursor = Math.max(this.listScroll, this.listCursor);
        this.redraw();
      },
      onNextPage: () => {
        /* Page-aligned max — not (rows - LIST_PAGE), else a short last page
         * leaves listScroll mid-page and page stays 1 (上一页 stuck off). */
        const cur = Math.floor(this.listScroll / LIST_PAGE);
        const maxScroll = Math.max(0, (pages - 1) * LIST_PAGE);
        this.listScroll = Math.min(maxScroll, (cur + 1) * LIST_PAGE);
        this.listCursor = Math.max(this.listCursor, this.listScroll);
        this.listCursor = Math.min(
          rows.length - 1,
          this.listScroll + LIST_PAGE - 1,
        );
        this.redraw();
      },
    });

    const actions = ["领取奖励", "离开擂台", "关闭"];
    const actionY = LCD_HEIGHT - SOFTKEY_H - ACTION_BTN_H - 4;
    const actionGap = 4;
    const actionBtnW = Math.max(
      48,
      Math.floor((w - 8 - actionGap * (actions.length - 1)) / actions.length),
    );
    for (let i = 0; i < actions.length; i++) {
      const idx = rows.length + i;
      const hi = idx === this.listCursor;
      const ax = 8 + i * (actionBtnW + actionGap);
      this.box
        .roundRect(ax, actionY, actionBtnW, ACTION_BTN_H, 2)
        .fill({ color: hi ? CBE.select : CBE.bgPanelDark })
        .stroke({
          width: 1,
          color: hi ? CBE.borderBright : CBE.borderDim,
        });
      this.addLabel(
        actions[i]!,
        ax + 6,
        actionY + 3,
        hi ? CBE.menuHi : CBE.inkBody,
      );
      this.rowHits.push(
        attachRowPointerHit(this, ax, actionY, actionBtnW, ACTION_BTN_H, () => {
          this.listCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
