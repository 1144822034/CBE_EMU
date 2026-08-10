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
import { ChatIme } from "./ChatIme";
import type { BagItem } from "../net/protocolExtras";
import { qualityColor } from "../resources/itemCatalog";
import {
  AUCTION_DURATION_LABELS,
  currencyLabel,
  formatRemain,
  type AuctionListing,
  type AuctionStatus,
} from "../net/protocolAuction";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  type PanelTipState,
} from "./panelTip";

export type AuctionPanelAction =
  | "bid"
  | "cancel"
  | "claim"
  | "refresh"
  | "close"
  | "confirm_list";

type Tab = "browse" | "mine" | "list" | "claim";
/** 上架页内部模式：菜单 / 选背包 / 设金额 */
type ListMode = "menu" | "bag" | "price";

const TABS: { id: Tab; label: string; short: string }[] = [
  { id: "browse", label: "浏览", short: "浏" },
  { id: "mine", label: "我的", short: "我" },
  { id: "list", label: "上架", short: "上" },
  { id: "claim", label: "领取", short: "领" },
];

const BAG_PAGE_SIZE = 7;
const BAG_TAB_BG = 0x375946;
const BAG_SELECT = 0x20676e;

/**
 * 物品竞拍：背包式顶栏标签；上架走交易式「选物→设金额→确认」。
 */
export class AuctionPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private inputHit = new Graphics();
  private activeTab = 0;
  private listMode: ListMode = "menu";
  private rows: AuctionListing[] = [];
  private mine: AuctionListing[] = [];
  private bag: BagItem[] = [];
  private status: AuctionStatus | null = null;
  private listCursor = 0;
  private bagCursor = 0;
  private bagScroll = 0;
  private menuCursor = 0;
  private selected: BagItem | null = null;
  private priceDraft = "10000";
  private currency = 0;
  private durationTier = 0;
  private pendingClaims = 0;
  private tip = "";
  private toast: PanelTipState = createPanelTipState();
  private openGen = 0;
  private focusTimer: ReturnType<typeof setTimeout> | null = null;
  private ime = new ChatIme({
    maxLength: 10,
    inputMode: "numeric",
    enterkeyhint: "done",
    ariaLabel: "竞拍金额",
    sanitize: (raw) => raw.replace(/\D/g, "").replace(/^0+(?=\d)/, ""),
  });

  onAction: ((action: AuctionPanelAction, listingId?: number) => void) | null =
    null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(this.box);
    this.inputHit.eventMode = "static";
    this.inputHit.cursor = "text";
    this.inputHit.on("pointertap", () => this.focusPriceInput());
    this.addChild(this.inputHit);
    this.ime.onChange = (v) => {
      this.priceDraft = v || "0";
      if (this.visible && this.listMode === "price") this.redraw();
    };
    this.ime.onSubmit = () => this.commitPrice();
  }

  get tab(): Tab {
    return TABS[this.activeTab]?.id ?? "browse";
  }

  open(
    rows: AuctionListing[],
    mine: AuctionListing[],
    bag: BagItem[],
    status: AuctionStatus | null,
    preferTab?: Tab,
  ): void {
    this.releaseIme();
    this.rows = rows;
    this.mine = mine;
    this.bag = bag.slice();
    this.status = status;
    this.pendingClaims = status?.pendingClaims ?? 0;
    this.listCursor = 0;
    this.bagCursor = 0;
    this.bagScroll = 0;
    this.menuCursor = 0;
    this.listMode = "menu";
    this.tip = "";
    if (preferTab) {
      const i = TABS.findIndex((t) => t.id === preferTab);
      this.activeTab = i >= 0 ? i : 0;
    } else {
      this.activeTab = this.pendingClaims > 0 ? 3 : 0;
    }
    this.visible = true;
    this.redraw();
  }

  setData(
    rows: AuctionListing[],
    mine: AuctionListing[],
    bag: BagItem[],
    status: AuctionStatus | null,
  ): void {
    this.rows = rows;
    this.mine = mine;
    this.bag = bag.slice();
    this.status = status;
    this.pendingClaims = status?.pendingClaims ?? 0;
    if (this.selected) {
      this.selected =
        this.bag.find((b) => b.seq === this.selected!.seq) ?? this.selected;
    }
    if (this.visible) this.redraw();
  }

  /** 从背包「拍卖」动作进入时预选物品并切到上架页。 */
  preselectBagItem(item: BagItem): void {
    this.activeTab = 2;
    this.listMode = "menu";
    this.selected = item;
    this.menuCursor = 4; // 确认上架
    this.tip = `已选 ${item.name}`;
    if (this.visible) this.redraw();
  }

  close(): void {
    this.releaseIme();
    clearPanelTip(this.toast);
    this.visible = false;
  }

  /** Center toast — survives tab/data refresh until timeout. */
  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  softkeys(): { left: string; mid: string; right: string } {
    // 标签只用左右方向键切换（与背包分类一致），软键不做切页。
    if (this.tab === "list" && this.listMode === "bag") {
      return { left: "选择", mid: "选择", right: "返回" };
    }
    if (this.tab === "list" && this.listMode === "price") {
      return { left: "确定", mid: "确定", right: "返回" };
    }
    return { left: "确认", mid: "确认", right: "关闭" };
  }

  switchTab(dir: -1 | 1): void {
    if (!this.visible) return;
    this.releaseIme();
    this.listMode = "menu";
    this.activeTab = (this.activeTab + dir + TABS.length) % TABS.length;
    this.listCursor = 0;
    this.menuCursor = 0;
    this.tip = "";
    this.redraw();
  }

  move(dir: -1 | 1): void {
    if (!this.visible) return;
    if (this.tab === "list" && this.listMode === "price") return;
    if (this.tab === "list" && this.listMode === "bag") {
      const n = Math.max(1, this.bag.length);
      this.bagCursor = (this.bagCursor + dir + n) % n;
      this.ensureBagVisible();
      this.redraw();
      return;
    }
    if (this.tab === "list" && this.listMode === "menu") {
      const n = this.listMenu().length;
      this.menuCursor = (this.menuCursor + dir + n) % n;
      this.redraw();
      return;
    }
    const n = this.listRows().length;
    if (n <= 0) return;
    this.listCursor = (this.listCursor + dir + n) % n;
    this.redraw();
  }

  /**
   * 左右方向键：优先切顶栏标签；上架菜单在货币/时长行时改参数。
   * @returns true 已处理。
   */
  horizontal(dir: -1 | 1): boolean {
    if (!this.visible) return false;
    if (this.tab === "list" && this.listMode === "price") return false;
    if (this.tab === "list" && this.listMode === "menu") {
      const row = this.listMenu()[this.menuCursor];
      if (row?.id === "currency") {
        this.currency = this.currency === 0 ? 1 : 0;
        this.tip = `货币 ${currencyLabel(this.currency)}`;
        this.redraw();
        return true;
      }
      if (row?.id === "duration") {
        this.durationTier = (this.durationTier + dir + 4) % 4;
        this.tip = `时长 ${AUCTION_DURATION_LABELS[this.durationTier]}`;
        this.redraw();
        return true;
      }
    }
    // 子页（选物）内左右也切标签，与背包一致。
    this.switchTab(dir);
    return true;
  }

  confirm(): void {
    if (!this.visible) return;
    if (this.tab === "list") {
      this.confirmList();
      return;
    }
    if (this.tab === "claim") {
      this.onAction?.("claim");
      return;
    }
    const rows = this.listRows();
    const row = rows[this.listCursor];
    if (!row) {
      this.onAction?.("refresh");
      return;
    }
    if (row.kind === "action") {
      this.onAction?.(row.action);
      return;
    }
    if (this.tab === "browse") this.onAction?.("bid", row.listing.listingId);
    else this.onAction?.("cancel", row.listing.listingId);
  }

  cancelOrBack(): boolean {
    if (!this.visible) return false;
    if (this.tab === "list" && this.listMode !== "menu") {
      if (this.listMode === "price") this.releaseIme();
      this.listMode = "menu";
      this.tip = "";
      this.redraw();
      return true;
    }
    return false;
  }

  isPriceMode(): boolean {
    return this.visible && this.tab === "list" && this.listMode === "price";
  }

  typePriceKey(key: string): boolean {
    if (!this.isPriceMode() || this.ime.isFocused()) return false;
    if (key >= "0" && key <= "9") {
      this.priceDraft = (this.priceDraft === "0" ? "" : this.priceDraft) + key;
      if (this.priceDraft.length > 10) this.priceDraft = this.priceDraft.slice(0, 10);
      this.ime.setValue(this.priceDraft);
      this.redraw();
      return true;
    }
    if (key === "backspace") {
      this.priceDraft = this.priceDraft.slice(0, -1);
      this.ime.setValue(this.priceDraft);
      this.redraw();
      return true;
    }
    return false;
  }

  get listParams(): {
    bagSeq: number;
    count: number;
    startPrice: number;
    currency: number;
    durationTier: number;
  } | null {
    const item = this.selected;
    if (!item) return null;
    const price = Math.max(1, Number(this.priceDraft) || 0);
    return {
      bagSeq: item.seq,
      count: item.kind === "equip" ? 1 : Math.max(1, item.count),
      startPrice: price >>> 0,
      currency: this.currency >>> 0,
      durationTier: this.durationTier >>> 0,
    };
  }

  destroy(options?: boolean | { children?: boolean; texture?: boolean }): void {
    this.releaseIme();
    this.ime.destroy();
    super.destroy(options);
  }

  private confirmList(): void {
    if (this.listMode === "bag") {
      const it = this.bag[this.bagCursor];
      if (!it) {
        this.tip = "背包为空";
        this.redraw();
        return;
      }
      this.selected = it;
      this.listMode = "menu";
      this.menuCursor = 1; // 设置金额
      this.tip = `已选 ${it.name}`;
      this.redraw();
      return;
    }
    if (this.listMode === "price") {
      this.commitPrice();
      return;
    }
    const row = this.listMenu()[this.menuCursor];
    if (!row) return;
    if (row.id === "pick") {
      this.listMode = "bag";
      this.bagCursor = 0;
      this.bagScroll = 0;
      this.tip = "选择要上架的物品";
      this.redraw();
      return;
    }
    if (row.id === "price") {
      this.listMode = "price";
      this.tip = "输入起拍金额后确定";
      this.redraw();
      this.focusPriceInput();
      return;
    }
    if (row.id === "currency") {
      this.currency = this.currency === 0 ? 1 : 0;
      this.tip = `货币 ${currencyLabel(this.currency)}`;
      this.redraw();
      return;
    }
    if (row.id === "duration") {
      this.durationTier = (this.durationTier + 1) % 4;
      this.tip = `时长 ${AUCTION_DURATION_LABELS[this.durationTier]}`;
      this.redraw();
      return;
    }
    if (row.id === "submit") {
      if (!this.selected) {
        this.tip = "请先选择物品";
        this.redraw();
        return;
      }
      const price = Number(this.priceDraft) || 0;
      if (price <= 0) {
        this.tip = "请设置起拍金额";
        this.redraw();
        return;
      }
      this.onAction?.("confirm_list");
      return;
    }
    if (row.id === "refresh") this.onAction?.("refresh");
  }

  private listMenu(): { id: string; label: string }[] {
    const fee = this.feePreview();
    const sel = this.selected
      ? `${this.selected.name}${this.selected.enhance ? `+${this.selected.enhance}` : ""}`
      : "(未选)";
    return [
      { id: "pick", label: `选择物品  ${sel}` },
      {
        id: "price",
        label: `设置金额  ${this.priceDraft || "0"}${currencyLabel(this.currency)}`,
      },
      { id: "currency", label: `货币  ${currencyLabel(this.currency)}` },
      {
        id: "duration",
        label: `时长  ${AUCTION_DURATION_LABELS[this.durationTier]} 费${this.feePercent()}%`,
      },
      {
        id: "submit",
        label: `确认上架  手费${fee}${currencyLabel(this.currency)}`,
      },
      { id: "refresh", label: "刷新背包" },
    ];
  }

  private listRows(): (
    | { kind: "listing"; listing: AuctionListing }
    | { kind: "action"; action: AuctionPanelAction; label: string }
  )[] {
    const out: (
      | { kind: "listing"; listing: AuctionListing }
      | { kind: "action"; action: AuctionPanelAction; label: string }
    )[] = [];
    if (this.tab === "browse") {
      for (const r of this.rows) out.push({ kind: "listing", listing: r });
      out.push({ kind: "action", action: "refresh", label: "刷新列表" });
    } else if (this.tab === "mine") {
      for (const r of this.mine) out.push({ kind: "listing", listing: r });
      out.push({ kind: "action", action: "refresh", label: "刷新" });
    } else if (this.tab === "claim") {
      out.push({
        kind: "action",
        action: "claim",
        label: `领取待领(${this.pendingClaims})`,
      });
      out.push({ kind: "action", action: "refresh", label: "刷新" });
    }
    return out;
  }

  private feePercent(): number {
    return (
      this.status?.feePercents[this.durationTier] ?? 2 + this.durationTier * 2
    );
  }

  private feePreview(): number {
    const price = Math.max(1, Number(this.priceDraft) || 0);
    return Math.max(1, Math.floor((price * this.feePercent()) / 100));
  }

  private ensureBagVisible(): void {
    if (this.bagCursor < this.bagScroll) this.bagScroll = this.bagCursor;
    if (this.bagCursor >= this.bagScroll + BAG_PAGE_SIZE) {
      this.bagScroll = this.bagCursor - BAG_PAGE_SIZE + 1;
    }
    const maxScroll = Math.max(0, this.bag.length - BAG_PAGE_SIZE);
    this.bagScroll = Math.max(0, Math.min(this.bagScroll, maxScroll));
  }

  private focusPriceInput(): void {
    if (!this.isPriceMode()) return;
    this.ime.show(this.priceInputRect());
    if (this.focusTimer) clearTimeout(this.focusTimer);
    const gen = ++this.openGen;
    this.focusTimer = setTimeout(() => {
      this.focusTimer = null;
      if (!this.visible || this.openGen !== gen || !this.isPriceMode()) return;
      this.ime.focus(this.priceDraft === "0" ? "" : this.priceDraft);
    }, 50);
  }

  private commitPrice(): void {
    if (!this.isPriceMode()) return;
    const n = Math.max(1, Number(this.priceDraft || this.ime.value) || 0);
    this.priceDraft = String(n);
    this.releaseIme();
    this.listMode = "menu";
    this.menuCursor = 4;
    this.tip = `起拍 ${this.priceDraft}${currencyLabel(this.currency)}`;
    this.redraw();
  }

  private releaseIme(): void {
    this.openGen += 1;
    if (this.focusTimer) {
      clearTimeout(this.focusTimer);
      this.focusTimer = null;
    }
    this.ime.hide();
    this.inputHit.clear();
  }

  private priceInputRect(): { x: number; y: number; w: number; h: number } {
    return { x: 20, y: 118, w: LCD_WIDTH - 40, h: 22 };
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

  private addLabelCentered(
    text: string,
    cx: number,
    cy: number,
    color: number,
  ): void {
    const l = makePixelText(text, 12, color);
    l.x = Math.floor(cx - l.width / 2);
    l.y = Math.floor(cy - l.height / 2);
    this.addChild(l);
    this.labels.push(l);
  }

  private drawTabs(x: number, y: number, w: number, h: number): void {
    const n = TABS.length;
    const gap = 2;
    const tw = Math.floor((w - gap * (n - 1)) / n);
    for (let i = 0; i < n; i++) {
      const tab = TABS[i]!;
      const tx = x + i * (tw + gap);
      const active = i === this.activeTab;
      const claimMark = tab.id === "claim" && this.pendingClaims > 0;
      this.box
        .roundRect(tx, y, tw, h, 2)
        .fill({ color: active ? CBE.bgPanelDark : BAG_TAB_BG })
        .stroke({
          width: 1,
          color: active || claimMark ? CBE.borderBright : CBE.borderDim,
        });
      const label = active ? tab.label : tab.short;
      const color = claimMark && !active ? 0xffe080 : active ? CBE.ink : CBE.inkMint;
      this.addLabelCentered(label, tx + tw / 2, y + h / 2, color);
    }
  }

  private redraw(): void {
    this.clearLabels();
    this.box.clear();
    this.inputHit.clear();
    const x = 4;
    const y = 2;
    const w = LCD_WIDTH - 8;
    const h = LCD_HEIGHT - SOFTKEY_H - 4;
    drawCbePanel(this.box, x, y, w, h);
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, y + 4, Math.min(w - 20, 120), 18);
    this.addLabelCentered("物品竞拍", LCD_WIDTH / 2, y + 13, CBE.cream);

    const tabY = y + 26;
    const tabH = 20;
    this.drawTabs(x + 6, tabY, w - 12, tabH);

    const bodyTop = tabY + tabH + 6;
    if (this.tab === "list") this.drawListTab(x, bodyTop, w, h - (bodyTop - y));
    else this.drawBrowseLike(x, bodyTop, w, h - (bodyTop - y));

    drawPanelTipOverlay(this, this.toast);
    if (this.isPriceMode()) this.ime.show(this.priceInputRect());
  }

  private drawBrowseLike(
    x: number,
    bodyTop: number,
    w: number,
    bodyH: number,
  ): void {
    const rows = this.listRows();
    const tipY = bodyTop + bodyH - 16;
    let hint = "←→标签 上下选择";
    if (this.tab === "browse") hint = "←→标签 确定出价";
    if (this.tab === "mine") hint = "←→标签 确定撤拍";
    if (this.tab === "claim") {
      hint =
        this.pendingClaims > 0
          ? `←→标签 待领${this.pendingClaims}`
          : "←→标签 暂无待领";
    }
    this.addLabel(hint, x + 10, bodyTop, CBE.inkLabel);

    const listTop = bodyTop + 16;
    const rowH = 16;
    const maxShow = Math.max(1, Math.floor((tipY - listTop - 4) / rowH));
    let start = 0;
    if (this.listCursor >= maxShow) start = this.listCursor - maxShow + 1;
    if (!rows.length) {
      this.addLabel("(空)", x + 12, listTop + 4, CBE.inkMint);
    }
    for (let i = 0; i < maxShow && start + i < rows.length; i++) {
      const idx = start + i;
      const row = rows[idx]!;
      const ry = listTop + i * rowH;
      const hi = idx === this.listCursor;
      if (hi) this.box.rect(x + 6, ry, w - 12, rowH - 1).fill({ color: BAG_SELECT });
      let label = "";
      if (row.kind === "action") label = row.label;
      else {
        const r = row.listing;
        const en = r.enhanceLevel > 0 ? `+${r.enhanceLevel}` : "";
        const bid =
          this.tab === "mine"
            ? r.highBidderRoleId
              ? "有价"
              : "无价"
            : formatRemain(r.remainSec);
        label = `${r.itemName}${en}x${r.itemCount} ${r.currentPrice}${currencyLabel(r.currency)} ${bid}`;
      }
      this.addLabel(label.slice(0, 26), x + 10, ry + 1, hi ? CBE.menuHi : CBE.ink);
    }
    this.addLabel(this.tip || hint, x + 10, tipY, CBE.inkMint);
  }

  private drawListTab(
    x: number,
    bodyTop: number,
    w: number,
    bodyH: number,
  ): void {
    const tipY = bodyTop + bodyH - 16;
    if (this.listMode === "bag") {
      this.addLabel("选择物品（确定选入）", x + 10, bodyTop, CBE.inkLabel);
      if (!this.bag.length) {
        this.addLabel("(背包为空)", x + 12, bodyTop + 22, CBE.inkMint);
      } else {
        this.ensureBagVisible();
        const pageItems = this.bag.slice(
          this.bagScroll,
          this.bagScroll + BAG_PAGE_SIZE,
        );
        pageItems.forEach((it, i) => {
          const idx = this.bagScroll + i;
          const hi = idx === this.bagCursor;
          const ry = bodyTop + 20 + i * 16;
          if (hi) {
            this.box.rect(x + 6, ry, w - 12, 15).fill({ color: BAG_SELECT });
          }
          const en = it.enhance ? `+${it.enhance}` : "";
          this.addLabel(
            `${it.name}${en} ×${it.count}`.slice(0, 22),
            x + 10,
            ry + 1,
            hi ? CBE.ink : qualityColor(it.quality),
          );
        });
      }
      this.addLabel(this.tip || "返回菜单", x + 10, tipY, CBE.inkMint);
      return;
    }

    if (this.listMode === "price") {
      this.addLabel("设置起拍金额", x + 10, bodyTop, CBE.inkLabel);
      const sel = this.selected
        ? `${this.selected.name}${this.selected.enhance ? `+${this.selected.enhance}` : ""}`
        : "(未选物品)";
      this.addLabel(sel.slice(0, 22), x + 10, bodyTop + 18, CBE.inkMint);
      this.addLabel(
        `货币 ${currencyLabel(this.currency)}`,
        x + 10,
        bodyTop + 36,
        CBE.inkLabel,
      );
      const rect = this.priceInputRect();
      this.box
        .rect(rect.x - 4, rect.y - 2, rect.w + 8, rect.h + 4)
        .fill({ color: 0x0c261e })
        .stroke({ width: 1, color: CBE.borderBright });
      this.inputHit
        .rect(rect.x - 4, rect.y - 2, rect.w + 8, rect.h + 4)
        .fill({ color: 0xffffff, alpha: 0.001 });
      const shown = this.priceDraft.length ? this.priceDraft : "点击输入…";
      this.addLabel(
        shown,
        rect.x,
        rect.y + 4,
        this.priceDraft.length ? CBE.ink : CBE.inkLabel,
      );
      this.addLabel(
        `手费预览 ${this.feePreview()}${currencyLabel(this.currency)} (${this.feePercent()}%)`,
        x + 10,
        rect.y + 30,
        CBE.inkMint,
      );
      this.addLabel(this.tip || "确定保存金额", x + 10, tipY, CBE.inkMint);
      return;
    }

    // menu
    this.addLabel("←→标签 选物→金额→确认", x + 10, bodyTop, CBE.inkLabel);
    const menu = this.listMenu();
    const listTop = bodyTop + 18;
    menu.forEach((row, i) => {
      const ry = listTop + i * 16;
      const hi = i === this.menuCursor;
      if (hi) this.box.rect(x + 6, ry, w - 12, 15).fill({ color: BAG_SELECT });
      this.addLabel(row.label.slice(0, 26), x + 10, ry + 1, hi ? CBE.menuHi : CBE.ink);
    });
    this.addLabel(
      this.tip || "←→切标签；货币/时长行可调",
      x + 10,
      tipY,
      CBE.inkMint,
    );
  }
}
