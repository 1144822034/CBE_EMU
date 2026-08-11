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
import { BitmapLabel, UC3_CELL, wrapUiText } from "./BitmapLabel";
import type { WagerListRow, WagerMine } from "../net/protocolArena";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import type { SoftkeyLabels } from "./SoftkeyBar";
import { COPPER_PER_GOLD } from "../net/protocolAuction";
import { sceneDisplayName } from "./WorldHud";

export type WagerPanelAction =
  | "create"
  | "challenge"
  | "confirm"
  | "reject"
  | "cancel"
  | "refresh"
  | "close"
  | "prev_page"
  | "next_page"
  | "noop";

/** 摆擂=我的开放擂；擂台=他人可挑战；进行中=待确认/战斗. */
type WagerTab = "host" | "challenge" | "live";

type WagerRowAction = {
  label: string;
  action: WagerPanelAction;
  challengeId?: number;
  softLabel?: string;
};

/** 赏金押金最低 100 金（1金=10000铜）. */
export const WAGER_MIN_STAKE_GOLD = 100;
export const WAGER_MIN_STAKE_COPPER = WAGER_MIN_STAKE_GOLD * COPPER_PER_GOLD;

const PAGE_SIZE = 5;
const TAB_BG = 0x375946;
const TAB_H = 18;
/** 面板相对 LCD 顶/侧留白，避免贴顶盖住世界 HUD. */
const PANEL_X = 6;
const PANEL_Y = 28;
const PANEL_BOTTOM_GAP = 6;
const TABS: { id: WagerTab; label: string }[] = [
  { id: "host", label: "摆擂" },
  { id: "challenge", label: "擂台" },
  { id: "live", label: "进行中" },
];

function formatStakeJin(copper: number): string {
  const jin = Math.max(0, Math.floor((copper >>> 0) / COPPER_PER_GOLD));
  return `${jin}金`;
}

/** 列表位置用 sMap 场景名（与 HUD 一致），勿直接显示地图文件名. */
function formatWagerScene(scene: string): string {
  const s = (scene || "").trim();
  if (!s) return "";
  return sceneDisplayName(s);
}

const WAGER_JOBS = ["天机", "幻剑", "鬼道"];

function formatWagerJobLevel(job: number, level: number): string {
  const j =
    job >= 1 && job <= 3 ? WAGER_JOBS[job - 1]! : job > 0 ? `职${job}` : "?";
  const lv = level > 0 ? level : "?";
  return `${j}Lv${lv}`;
}

function formatWagerSide(
  members: { name: string; job: number; level: number }[],
  fallbackName: string,
): string {
  if (members.length > 0) {
    return members
      .map((m) => `${m.name}(${formatWagerJobLevel(m.job, m.level)})`)
      .join("/");
  }
  return fallbackName || "?";
}

/** 开放擂台（应战/摆擂栏）— 以 status 为准，勿用 challCount（拒绝后可能脏）. */
function isOpenBoard(r: WagerListRow): boolean {
  return (r.status >>> 0) === 0;
}

/** 待确认或战斗中. */
function isLiveBoard(r: WagerListRow): boolean {
  const st = r.status >>> 0;
  return st === 1 || st === 2;
}

/**
 * 赏金擂台：摆擂(表单) / 擂台 / 进行中。
 * 软键：左=刷新，中=当前操作，右=关闭。
 */
export class WagerPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private rows: WagerListRow[] = [];
  private mine: WagerMine | null = null;
  private selfRoleId = 0;
  private tab: WagerTab = "host";
  private challengePage = 0;
  private livePage = 0;
  private listCursor = 0;
  private stakeInput = WAGER_MIN_STAKE_COPPER;
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: WagerPanelAction, challengeId?: number) => void) | null =
    null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(this.box);
  }

  get currentTab(): WagerTab {
    return this.tab;
  }

  open(
    rows: WagerListRow[],
    mine: WagerMine | null,
    selfRoleId = 0,
  ): void {
    const wasVisible = this.visible;
    this.rows = rows;
    this.mine = mine;
    this.selfRoleId = selfRoleId >>> 0;
    if (!wasVisible) {
      this.tab = "host";
      this.challengePage = 0;
      this.livePage = 0;
      this.listCursor = 0;
    } else {
      this.clampPage();
      const n = this.actions().length;
      if (n > 0 && this.listCursor >= n) this.listCursor = Math.max(0, n - 1);
    }
    this.visible = true;
    this.redraw();
  }

  setData(
    rows: WagerListRow[],
    mine: WagerMine | null,
    selfRoleId?: number,
  ): void {
    this.rows = rows;
    this.mine = mine;
    if (selfRoleId != null) this.selfRoleId = selfRoleId >>> 0;
    this.clampPage();
    const n = this.actions().length;
    if (n > 0 && this.listCursor >= n) this.listCursor = Math.max(0, n - 1);
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

  /** delta 为铜币；左右键通常一次 ±1 金. */
  adjustStake(delta: number): void {
    if (!this.visible || this.tab !== "host" || this.hasOpenHostBoard()) return;
    const next = Math.max(WAGER_MIN_STAKE_COPPER, this.stakeInput + delta);
    this.stakeInput = Math.min(next, 10_000 * COPPER_PER_GOLD);
    this.redraw();
  }

  get stake(): number {
    return this.stakeInput >>> 0;
  }

  /**
   * ←→：摆擂且未摆时调押金（返回 false）；否则切栏。
   */
  horizontal(dir: -1 | 1): boolean {
    if (!this.visible) return false;
    if (this.tab === "host" && !this.hasOpenHostBoard()) return false;
    const idx = TABS.findIndex((t) => t.id === this.tab);
    this.selectTab((idx + dir + TABS.length) % TABS.length);
    return true;
  }

  selectTab(index: number): void {
    if (index < 0 || index >= TABS.length) return;
    const id = TABS[index]!.id;
    if (id === this.tab) return;
    this.tab = id;
    this.listCursor = 0;
    this.clampPage();
    this.redraw();
  }

  move(dir: -1 | 1): void {
    if (!this.visible) return;
    if (this.tab === "host") return;
    const n = this.actions().length;
    if (n <= 0) return;
    this.listCursor = (this.listCursor + dir + n) % n;
    this.redraw();
  }

  confirm(): void {
    if (!this.visible) return;
    if (this.tab === "host") {
      const primary = this.hostPrimaryAction();
      if (primary) this.onAction?.(primary.action, primary.challengeId);
      return;
    }
    const actions = this.actions();
    const a = actions[this.listCursor];
    if (!a || a.action === "noop") return;
    if (a.action === "prev_page") {
      this.changePage(-1);
      return;
    }
    if (a.action === "next_page") {
      this.changePage(1);
      return;
    }
    this.onAction?.(a.action, a.challengeId);
  }

  softkeys(): SoftkeyLabels {
    /* 底栏只留刷新/关闭；摆擂·应战用面板内按钮或中键/回车，不显示中间软键字. */
    return { left: "刷新", right: "关闭" };
  }

  private hasOpenHostBoard(): boolean {
    if (this.mine?.challengeId && this.mine.status === 0) return true;
    return this.hostRows().length > 0;
  }

  private hostPrimaryAction(): WagerRowAction | null {
    const board = this.myOpenBoard();
    const id =
      board?.challengeId ||
      (this.mine?.status === 0 ? this.mine.challengeId : 0);
    if (id) {
      return {
        label: "撤擂",
        action: "cancel",
        challengeId: id,
        softLabel: "撤擂",
      };
    }
    return {
      label: "确认摆擂",
      action: "create",
      softLabel: "摆擂",
    };
  }

  private myOpenBoard(): WagerListRow | null {
    return this.hostRows()[0] ?? null;
  }

  private pageIndex(): number {
    return this.tab === "challenge" ? this.challengePage : this.livePage;
  }

  private setPageIndex(page: number): void {
    if (this.tab === "challenge") this.challengePage = page;
    else this.livePage = page;
  }

  private hostRows(): WagerListRow[] {
    return this.rows.filter((r) => isOpenBoard(r) && this.isOwnHostRow(r));
  }

  private challengeRows(): WagerListRow[] {
    return this.rows.filter((r) => isOpenBoard(r) && !this.isOwnHostRow(r));
  }

  private liveRows(): WagerListRow[] {
    return this.rows.filter(isLiveBoard);
  }

  private filteredRows(): WagerListRow[] {
    if (this.tab === "challenge") return this.challengeRows();
    if (this.tab === "live") return this.liveRows();
    return [];
  }

  private emptyHint(): string {
    if (this.tab === "challenge") return "(暂无可应战擂台)";
    return "(暂无进行中)";
  }

  private pageCount(total: number): number {
    return Math.max(1, Math.ceil(total / PAGE_SIZE));
  }

  private clampPage(): void {
    if (this.tab === "host") return;
    const total = this.filteredRows().length;
    const pages = this.pageCount(total);
    const p = Math.min(this.pageIndex(), pages - 1);
    this.setPageIndex(Math.max(0, p));
  }

  private changePage(dir: -1 | 1): void {
    const total = this.filteredRows().length;
    const pages = this.pageCount(total);
    const next = (this.pageIndex() + dir + pages) % pages;
    this.setPageIndex(next);
    this.listCursor = 0;
    this.redraw();
  }

  private isOwnHostRow(r: WagerListRow): boolean {
    if (this.selfRoleId && r.hostLeaderRoleId === this.selfRoleId) return true;
    if (
      this.mine?.challengeId &&
      this.mine.challengeId === r.challengeId &&
      (this.mine.status === 0 ||
        this.mine.status === 1 ||
        this.mine.status === 2)
    ) {
      return true;
    }
    return false;
  }

  private rowLabel(r: WagerListRow): { label: string; tag: string } {
    const stakeLabel = formatStakeJin(r.stake);
    const sceneLabel = formatWagerScene(r.scene);
    const where = sceneLabel ? ` @${sceneLabel}` : "";
    const hostSide = formatWagerSide(r.hosts, r.hostName);
    const challSide =
      r.challCount > 0 ? formatWagerSide(r.challs, "") : "";
    const vs = challSide ? `←${challSide}` : "";
    const own = this.isOwnHostRow(r);
    if (r.status === 2) {
      return {
        label: `${hostSide}${vs} 押${stakeLabel}${where}`,
        tag: own ? "[我的战斗]" : "[战斗中]",
      };
    }
    if (r.status === 1) {
      return {
        label: `${hostSide}${vs} 押${stakeLabel}${where}`,
        tag: own ? "[待我确认]" : "[待确认]",
      };
    }
    if (own) {
      return {
        label: `${hostSide} 押${stakeLabel}${where}`,
        tag: "[我的擂台]",
      };
    }
    return {
      label: `${hostSide} 押${stakeLabel}${where}`,
      tag: "[可挑战]",
    };
  }

  /** 应战 / 进行中 列表行（不含刷新关闭）. */
  private actions(): WagerRowAction[] {
    if (this.tab === "host") return [];
    const out: WagerRowAction[] = [];
    const filtered = this.filteredRows();
    const pages = this.pageCount(filtered.length);
    const page = this.pageIndex();
    const start = page * PAGE_SIZE;
    const slice = filtered.slice(start, start + PAGE_SIZE);

    if (filtered.length === 0) {
      out.push({
        label: this.emptyHint(),
        action: "noop",
        softLabel: "确认",
      });
    } else {
      for (const r of slice) {
        const { label, tag } = this.rowLabel(r);
        if (this.tab === "challenge") {
          out.push({
            label: `${label} ${tag}`,
            action: "challenge",
            challengeId: r.challengeId,
            softLabel: "应战",
          });
        } else {
          out.push({
            label: `${label} ${tag}`,
            action: "noop",
            challengeId: r.challengeId,
            softLabel: "确认",
          });
          /* 己方待确认：断线重连后也要能开战/拒绝，不单靠 mine.needConfirm */
          if (this.isOwnHostRow(r) && r.status === 1) {
            out.push({
              label: `确认开战 #${r.challengeId}`,
              action: "confirm",
              challengeId: r.challengeId,
              softLabel: "开战",
            });
            out.push({
              label: `拒绝挑战 #${r.challengeId}`,
              action: "reject",
              challengeId: r.challengeId,
              softLabel: "拒绝",
            });
          }
        }
      }
    }

    if (pages > 1) {
      out.push({
        label: `上一页(${page + 1}/${pages})`,
        action: "prev_page",
        softLabel: "上页",
      });
      out.push({
        label: `下一页(${page + 1}/${pages})`,
        action: "next_page",
        softLabel: "下页",
      });
    }

    if (
      this.tab === "live" &&
      this.mine?.needConfirm &&
      this.mine.challengeId &&
      !out.some(
        (a) =>
          a.action === "reject" && a.challengeId === this.mine!.challengeId,
      )
    ) {
      out.push({
        label: `确认开战 #${this.mine.challengeId}`,
        action: "confirm",
        challengeId: this.mine.challengeId,
        softLabel: "开战",
      });
      out.push({
        label: `拒绝挑战 #${this.mine.challengeId}`,
        action: "reject",
        challengeId: this.mine.challengeId,
        softLabel: "拒绝",
      });
    }
    return out;
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

  private drawRankStyleTabs(
    x: number,
    y: number,
    w: number,
    h: number,
    labels: string[],
    activeIndex: number,
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
          color: active ? CBE.menuHi : CBE.borderDim,
        });
      this.addLabelCentered(
        label.slice(0, 4),
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

  private panelInnerRight(): number {
    return PANEL_X + (LCD_WIDTH - PANEL_X * 2) - 8;
  }

  private drawActionButton(
    label: string,
    y: number,
    onTap: () => void,
  ): number {
    const bw = Math.min(140, LCD_WIDTH - PANEL_X * 2 - 24);
    const bh = 22;
    const bx = Math.floor((LCD_WIDTH - bw) / 2);
    this.box
      .roundRect(bx, y, bw, bh, 3)
      .fill({ color: CBE.select })
      .stroke({ width: 1, color: CBE.borderBright });
    this.addLabelCentered(label, LCD_WIDTH / 2, y + bh / 2, CBE.menuHi);
    this.rowHits.push(
      attachRowPointerHit(this, bx, y, bw, bh, onTap),
    );
    return y + bh + 8;
  }

  private drawHostBody(top: number): void {
    const board = this.myOpenBoard();
    const openId =
      board?.challengeId ||
      (this.mine?.status === 0 ? this.mine.challengeId : 0);
    const lx = PANEL_X + 8;
    const cardW = LCD_WIDTH - PANEL_X * 2 - 16;
    let y = top;
    this.addLabel("我的擂台", lx, y, CBE.inkMint);
    y += 16;

    if (openId) {
      const stake = board ? board.stake : this.mine?.stake || 0;
      const scene = board ? formatWagerScene(board.scene) : "";
      const hosts = board
        ? formatWagerSide(board.hosts, board.hostName)
        : "已摆擂";
      this.box
        .roundRect(lx - 2, y, cardW, 56, 3)
        .fill({ color: CBE.bgPanelDark })
        .stroke({ width: 1, color: CBE.border });
      this.addLabel(`#${openId}  押${formatStakeJin(stake)}`, lx + 6, y + 6, CBE.ink);
      this.addLabel(hosts.slice(0, 22), lx + 6, y + 22, CBE.inkLabel);
      this.addLabel(
        scene ? `场景 ${scene} · 等待挑战` : "等待挑战中",
        lx + 6,
        y + 38,
        CBE.inkTip,
      );
      y += 66;
      this.addLabel("中键或点按可撤擂", lx, y, CBE.inkLabel);
      y += 18;
      this.drawActionButton("撤擂", y, () => {
        this.onAction?.("cancel", openId);
      });
      return;
    }

    this.addLabel("当前未摆擂", lx, y, CBE.inkLabel);
    y += 18;
    this.box
      .roundRect(lx - 2, y, cardW, 40, 3)
      .fill({ color: CBE.bgPanelDark })
      .stroke({ width: 1, color: CBE.menuHi });
    this.addLabel("押金(金)", lx + 6, y + 6, CBE.inkLabel);
    this.addLabelCentered(
      formatStakeJin(this.stakeInput),
      LCD_WIDTH / 2,
      y + 28,
      CBE.cream,
    );
    this.addLabel("←→", this.panelInnerRight() - 20, y + 20, CBE.inkTip);
    y += 48;
    this.addLabel(
      `最低${WAGER_MIN_STAKE_GOLD}金  ←→每次±100金  中键确认`,
      lx,
      y,
      CBE.inkLabel,
    );
    y += 18;
    this.drawActionButton("确认摆擂", y, () => {
      this.onAction?.("create");
    });
  }

  private drawListBody(top: number): void {
    const actions = this.actions();
    if (this.listCursor >= actions.length) this.listCursor = 0;
    const lx = PANEL_X + 8;
    const rowW = LCD_WIDTH - PANEL_X * 2 - 12;
    let y = top;
    this.addLabel(
      this.tab === "challenge" ? "选择擂台应战" : "查看进行中",
      lx,
      y,
      CBE.inkLabel,
    );
    y += 16;
    const maxY = LCD_HEIGHT - SOFTKEY_H - PANEL_BOTTOM_GAP - 4;
    const lineH = UC3_CELL;
    const textW = Math.max(40, rowW - 4);
    const wrapped = actions.map((a) => wrapUiText(a.label, textW, UC3_CELL));
    const heights = wrapped.map((lines) => Math.max(1, lines.length) * lineH);
    const avail = Math.max(lineH, maxY - y);
    let start = 0;
    while (start < this.listCursor) {
      let used = 0;
      for (let i = start; i <= this.listCursor; i++) used += heights[i]!;
      if (used <= avail) break;
      start++;
    }
    for (let idx = start; idx < actions.length; idx++) {
      const h = heights[idx]!;
      if (y + h > maxY && idx > start) break;
      const lines = wrapped[idx]!;
      const hi = idx === this.listCursor;
      if (hi) {
        this.box.rect(lx - 2, y - 1, rowW, h).fill({ color: CBE.select });
      }
      for (let li = 0; li < lines.length; li++) {
        this.addLabel(
          lines[li]!,
          lx,
          y + li * lineH,
          hi ? CBE.menuHi : CBE.ink,
        );
      }
      this.rowHits.push(
        attachRowPointerHit(this, lx - 2, y - 1, rowW, h, () => {
          this.listCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
      y += h;
    }
  }

  private redraw(): void {
    this.clearLabels();
    this.box.clear();
    const panelW = LCD_WIDTH - PANEL_X * 2;
    const panelH = LCD_HEIGHT - SOFTKEY_H - PANEL_Y - PANEL_BOTTOM_GAP;
    drawCbePanel(this.box, PANEL_X, PANEL_Y, panelW, panelH, CBE.bgDeep);
    const pillW = Math.min(panelW - 24, 100);
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, PANEL_Y + 6, pillW, 22);
    this.addLabelCentered("赏金擂台", LCD_WIDTH / 2, PANEL_Y + 17, CBE.cream);

    const tabY = PANEL_Y + 32;
    const tabLabels = TABS.map((t) => t.label);
    const activeIndex = TABS.findIndex((t) => t.id === this.tab);
    this.drawRankStyleTabs(
      PANEL_X + 6,
      tabY,
      panelW - 12,
      TAB_H,
      tabLabels,
      Math.max(0, activeIndex),
      (i) => this.selectTab(i),
    );

    const infoY = tabY + TAB_H + 4;
    this.addLabel("←→切栏", this.panelInnerRight() - 40, infoY, CBE.inkTip);
    if (this.tab === "host") {
      this.drawHostBody(infoY + 2);
    } else {
      const mineLine = this.mine?.challengeId
        ? `我的:#${this.mine.challengeId}${
            this.mine.needConfirm ? " 待确认" : ""
          }`
        : "";
      if (mineLine) this.addLabel(mineLine, PANEL_X + 8, infoY, CBE.inkMint);
      this.drawListBody(infoY + (mineLine ? 14 : 0));
    }
    drawPanelTipOverlay(this, this.toast);
  }
}
