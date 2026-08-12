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
import { BitmapLabel, wrapUiText } from "./BitmapLabel";
import { ChatIme } from "./ChatIme";
import {
  bagVisibleStackCount,
  isReservoirFlaskItemId,
  type BagItem,
} from "../net/protocolExtras";
import { lookupItemSync, qualityColor } from "../resources/itemCatalog";
import {
  AUCTION_DURATION_LABELS,
  auctionPriceUnit,
  currencyLabel,
  formatAuctionClaimRow,
  formatAuctionFee,
  formatAuctionPrice,
  formatRemain,
  goldToCopper,
  type AuctionClaimRow,
  type AuctionListing,
  type AuctionStatus,
} from "../net/protocolAuction";
import {
  buildEquipDetailLines,
  EQUIP_DETAIL_YELLOW,
  type EquipPaintLine,
} from "./equipDetail";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

export type AuctionPanelAction =
  | "bid"
  | "buyout"
  | "cancel"
  | "claim"
  | "refresh"
  | "close"
  | "confirm_list"
  | "prev_page"
  | "next_page";

type Tab = "browse" | "mine" | "list" | "claim";
/** 上架页内部模式：菜单 / 选背包 / 设金额 */
type ListMode = "menu" | "bag" | "price";
/** price 输入目标：起拍 / 一口价 */
type PriceTarget = "start" | "buyout";

const TABS: { id: Tab; label: string; short: string }[] = [
  { id: "browse", label: "浏览", short: "浏" },
  { id: "mine", label: "我的", short: "我" },
  { id: "list", label: "上架", short: "上" },
  { id: "claim", label: "领取", short: "领" },
];

const BAG_PAGE_SIZE = 7;
const BAG_TAB_BG = 0x375946;
const BAG_SELECT = 0x20676e;

function listingAsBagItem(r: AuctionListing): BagItem {
  const cat = lookupItemSync(r.itemId);
  const kind = cat?.kind === "equip" ? "equip" : "item";
  return {
    itemId: r.itemId,
    seq: 0,
    count: r.itemCount,
    name: r.itemName || cat?.name || `物品${r.itemId}`,
    quality: cat?.quality ?? 0,
    enhance: r.enhanceLevel,
    kind,
    category: cat?.category ?? 0,
    desc: cat?.desc ?? "",
    usable: false,
    equipSlot: cat?.equipSlot ?? -1,
    ...(kind === "equip" && r.enhanceAttrs && r.enhanceAttrs.length
      ? { enhanceAttrs: r.enhanceAttrs }
      : {}),
  };
}

/**
 * 物品竞拍：背包式顶栏标签；上架走交易式「选物→设金额→确认」。
 * 浏览/我的：确定进入物品详情，可出价/一口价/撤拍。
 * 我的=在售上架+当前最高出价；领取=待领列表分页。
 */
export class AuctionPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private inputHit = new Graphics();
  private rowHits: Container[] = [];
  private activeTab = 0;
  private listMode: ListMode = "menu";
  private rows: AuctionListing[] = [];
  private mine: AuctionListing[] = [];
  private claims: AuctionClaimRow[] = [];
  private bag: BagItem[] = [];
  private status: AuctionStatus | null = null;
  private myRoleId = 0;
  private browsePage = 0;
  private minePage = 0;
  private claimPage = 0;
  private browseHasMore = false;
  private mineHasMore = false;
  private claimHasMore = false;
  private listCursor = 0;
  private bagCursor = 0;
  private bagScroll = 0;
  private menuCursor = 0;
  private selected: BagItem | null = null;
  /** 上架数量（装备/壶固定 1；普通道具 1..背包堆叠）. */
  private listCount = 1;
  private priceDraft = "1";
  private buyoutDraft = "0";
  private priceTarget: PriceTarget = "start";
  private currency = 0;
  private durationTier = 0;
  private pendingClaims = 0;
  private tip = "";
  private toast: PanelTipState = createPanelTipState();
  private openGen = 0;
  private focusTimer: ReturnType<typeof setTimeout> | null = null;
  /** 浏览/我的详情中的拍品 */
  private detailListing: AuctionListing | null = null;
  private detailActionCursor = 0;
  private detailPage = 0;
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
    this.eventMode = "static";
    this.addChild(this.box);
    this.inputHit.eventMode = "static";
    this.inputHit.cursor = "text";
    this.inputHit.on("pointertap", () => this.focusPriceInput());
    this.addChild(this.inputHit);
    this.ime.onChange = (v) => {
      const next = v || "0";
      if (this.priceTarget === "buyout") this.buyoutDraft = next;
      else this.priceDraft = next;
      if (this.visible && this.listMode === "price") this.redraw();
    };
    this.ime.onSubmit = () => this.commitPrice();
  }

  get tab(): Tab {
    return TABS[this.activeTab]?.id ?? "browse";
  }

  get listBrowsePage(): number {
    return this.browsePage;
  }

  get listMinePage(): number {
    return this.minePage;
  }

  get listClaimPage(): number {
    return this.claimPage;
  }

  open(
    rows: AuctionListing[],
    mine: AuctionListing[],
    bag: BagItem[],
    status: AuctionStatus | null,
    preferTab?: Tab,
    pages?: {
      browsePage?: number;
      minePage?: number;
      claimPage?: number;
      browseHasMore?: boolean;
      mineHasMore?: boolean;
      claimHasMore?: boolean;
    },
    claims?: AuctionClaimRow[],
    myRoleId?: number,
  ): void {
    this.releaseIme();
    this.rows = rows;
    this.mine = mine;
    this.claims = claims?.slice() ?? [];
    this.bag = bag.slice();
    this.status = status;
    this.myRoleId = myRoleId ?? 0;
    this.pendingClaims = status?.pendingClaims ?? 0;
    this.browsePage = Math.max(0, pages?.browsePage ?? 0);
    this.minePage = Math.max(0, pages?.minePage ?? 0);
    this.claimPage = Math.max(0, pages?.claimPage ?? 0);
    this.browseHasMore = !!pages?.browseHasMore;
    this.mineHasMore = !!pages?.mineHasMore;
    this.claimHasMore = !!pages?.claimHasMore;
    this.listCursor = 0;
    this.bagCursor = 0;
    this.bagScroll = 0;
    this.menuCursor = 0;
    this.listMode = "menu";
    this.detailListing = null;
    this.detailActionCursor = 0;
    this.detailPage = 0;
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
    pages?: {
      browsePage?: number;
      minePage?: number;
      claimPage?: number;
      browseHasMore?: boolean;
      mineHasMore?: boolean;
      claimHasMore?: boolean;
    },
    claims?: AuctionClaimRow[],
    myRoleId?: number,
  ): void {
    this.rows = rows;
    this.mine = mine;
    if (claims) this.claims = claims.slice();
    this.bag = bag.slice();
    this.status = status;
    if (myRoleId != null) this.myRoleId = myRoleId;
    this.pendingClaims = status?.pendingClaims ?? 0;
    if (pages) {
      if (pages.browsePage != null) this.browsePage = Math.max(0, pages.browsePage);
      if (pages.minePage != null) this.minePage = Math.max(0, pages.minePage);
      if (pages.claimPage != null) this.claimPage = Math.max(0, pages.claimPage);
      if (pages.browseHasMore != null) this.browseHasMore = pages.browseHasMore;
      if (pages.mineHasMore != null) this.mineHasMore = pages.mineHasMore;
      if (pages.claimHasMore != null) this.claimHasMore = pages.claimHasMore;
    }
    if (this.selected) {
      this.selected =
        this.bag.find((b) => b.seq === this.selected!.seq) ?? this.selected;
      this.clampListCount();
    }
    if (this.detailListing) {
      const id = this.detailListing.listingId;
      this.detailListing =
        this.rows.find((x) => x.listingId === id) ||
        this.mine.find((x) => x.listingId === id) ||
        null;
    }
    if (this.visible) this.redraw();
  }

  /** 从背包「拍卖」动作进入时预选物品并切到上架页。 */
  preselectBagItem(item: BagItem): void {
    this.activeTab = 2;
    this.listMode = "menu";
    this.detailListing = null;
    this.selected = item;
    this.listCount = this.defaultListCount(item);
    this.menuCursor = this.listQtyFixed(item) ? 2 : 1; // 起拍 / 数量
    this.tip = isReservoirFlaskItemId(item.itemId)
      ? `已选 ${item.name}（整壶·储量${item.count}）`
      : `已选 ${item.name}`;
    if (this.visible) this.redraw();
  }

  close(): void {
    this.releaseIme();
    clearPanelTip(this.toast);
    clearPointerHits(this.rowHits);
    this.detailListing = null;
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
    if (this.detailListing) {
      return { left: "确认", mid: "确认", right: "返回" };
    }
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
    if (this.detailListing) return;
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
    if (this.detailListing) {
      const acts = this.detailActions();
      if (!acts.length) return;
      this.detailActionCursor =
        (this.detailActionCursor + dir + acts.length) % acts.length;
      this.redraw();
      return;
    }
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
   * 左右方向键：优先切顶栏标签；上架菜单在货币/时长行时改参数；详情翻页。
   * @returns true 已处理。
   */
  horizontal(dir: -1 | 1): boolean {
    if (!this.visible) return false;
    if (this.detailListing) {
      this.detailPage = Math.max(0, this.detailPage + dir);
      this.redraw();
      return true;
    }
    if (this.tab === "list" && this.listMode === "price") return false;
    if (this.tab === "list" && this.listMode === "menu") {
      const row = this.listMenu()[this.menuCursor];
      if (row?.id === "count") {
        this.adjustListCount(dir);
        return true;
      }
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
    this.switchTab(dir);
    return true;
  }

  confirm(): void {
    if (!this.visible) return;
    if (this.detailListing) {
      this.confirmDetail();
      return;
    }
    if (this.tab === "list") {
      this.confirmList();
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
    if (row.kind === "claim") {
      this.tip = "选「一键领取」收全部";
      this.redraw();
      return;
    }
    this.openDetail(row.listing);
  }

  cancelOrBack(): boolean {
    if (!this.visible) return false;
    if (tryDismissPanelTipOnSoftRight(this.toast)) return true;
    if (this.detailListing) {
      this.detailListing = null;
      this.detailActionCursor = 0;
      this.detailPage = 0;
      this.tip = "";
      this.redraw();
      return true;
    }
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
    const cur =
      this.priceTarget === "buyout" ? this.buyoutDraft : this.priceDraft;
    if (key >= "0" && key <= "9") {
      let next = (cur === "0" ? "" : cur) + key;
      if (next.length > 10) next = next.slice(0, 10);
      if (this.priceTarget === "buyout") this.buyoutDraft = next;
      else this.priceDraft = next;
      this.ime.setValue(next);
      this.redraw();
      return true;
    }
    if (key === "backspace") {
      const next = cur.slice(0, -1);
      if (this.priceTarget === "buyout") this.buyoutDraft = next;
      else this.priceDraft = next;
      this.ime.setValue(next);
      this.redraw();
      return true;
    }
    return false;
  }

  get listParams(): {
    bagSeq: number;
    count: number;
    startPrice: number;
    buyoutPrice: number;
    currency: number;
    durationTier: number;
  } | null {
    const item = this.selected;
    if (!item) return null;
    const start = this.startPriceWire();
    let buyout = this.buyoutPriceWire();
    if (buyout > 0 && buyout < start) buyout = 0;
    /*
     * 装备 count=1；壶类 wire count=整行储量（可见堆叠恒 1）；
     * 普通道具才按 listCount 部分上架。
     */
    let count: number;
    if (item.kind === "equip") count = 1;
    else if (isReservoirFlaskItemId(item.itemId)) count = Math.max(1, item.count);
    else {
      const max = Math.max(1, item.count);
      count = Math.max(1, Math.min(this.listCount, max));
    }
    return {
      bagSeq: item.seq,
      count: count >>> 0,
      startPrice: start >>> 0,
      buyoutPrice: buyout >>> 0,
      currency: this.currency >>> 0,
      durationTier: this.durationTier >>> 0,
    };
  }

  destroy(options?: boolean | { children?: boolean; texture?: boolean }): void {
    this.releaseIme();
    this.ime.destroy();
    super.destroy(options);
  }

  private openDetail(listing: AuctionListing): void {
    this.detailListing = listing;
    this.detailActionCursor = 0;
    this.detailPage = 0;
    this.tip = "";
    this.redraw();
  }

  private isMyListing(r: AuctionListing): boolean {
    return this.myRoleId > 0 && r.sellerRoleId === this.myRoleId;
  }

  private isMyHighBid(r: AuctionListing): boolean {
    return this.myRoleId > 0 && r.highBidderRoleId === this.myRoleId;
  }

  private detailActions(): { id: AuctionPanelAction | "back"; label: string }[] {
    const r = this.detailListing;
    if (!r) return [];
    if (this.tab === "mine") {
      if (this.isMyListing(r)) {
        return [
          { id: "cancel", label: r.highBidderRoleId ? "有出价不可撤" : "撤拍" },
          { id: "back", label: "返回" },
        ];
      }
      /* 我在竞拍中（最高出价）— 不可撤，只能返回等结果. */
      return [
        {
          id: "back",
          label: this.isMyHighBid(r) ? "领先中·返回" : "返回",
        },
      ];
    }
    const acts: { id: AuctionPanelAction | "back"; label: string }[] = [
      { id: "bid", label: "出价" },
    ];
    if (r.buyoutPrice > 0 && r.currentPrice <= r.buyoutPrice) {
      acts.push({
        id: "buyout",
        label: `一口价 ${formatAuctionPrice(r.buyoutPrice, r.currency)}`,
      });
    }
    acts.push({ id: "back", label: "返回" });
    return acts;
  }

  private confirmDetail(): void {
    const r = this.detailListing;
    if (!r) return;
    const acts = this.detailActions();
    const act = acts[this.detailActionCursor];
    if (!act) return;
    if (act.id === "back") {
      this.cancelOrBack();
      return;
    }
    if (act.id === "cancel" && r.highBidderRoleId) {
      this.tip = "已有出价，无法下架";
      this.redraw();
      return;
    }
    this.onAction?.(act.id, r.listingId);
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
      this.listCount = this.defaultListCount(it);
      this.listMode = "menu";
      this.menuCursor = this.listQtyFixed(it) ? 2 : 1; // 起拍 / 数量
      this.tip = isReservoirFlaskItemId(it.itemId)
        ? `已选 ${it.name}（整壶·储量${it.count}）`
        : `已选 ${it.name} ×${this.listCount}`;
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
    if (row.id === "count") {
      this.adjustListCount(1);
      return;
    }
    if (row.id === "price") {
      this.priceTarget = "start";
      this.listMode = "price";
      this.tip =
        this.currency === 0 ? "输入起拍金(金)后确定" : "输入起拍酷宝后确定";
      this.redraw();
      this.focusPriceInput();
      return;
    }
    if (row.id === "buyout") {
      this.priceTarget = "buyout";
      this.listMode = "price";
      this.tip =
        this.currency === 0 ? "一口价金(0=无)须≥起拍" : "一口价酷宝(0=无)须≥起拍";
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
        this.tip = this.currency === 0 ? "请设置起拍金" : "请设置起拍金额";
        this.redraw();
        return;
      }
      const buyout = Number(this.buyoutDraft) || 0;
      if (buyout > 0 && buyout < price) {
        this.tip = "一口价须≥起拍价";
        this.redraw();
        return;
      }
      this.onAction?.("confirm_list");
      return;
    }
    if (row.id === "refresh") this.onAction?.("refresh");
  }

  private listMenu(): { id: string; label: string }[] {
    const fee = this.feePreviewWire();
    const unit = auctionPriceUnit(this.currency);
    const sel = this.selected
      ? `${this.selected.name}${this.selected.enhance ? `+${this.selected.enhance}` : ""}`
      : "(未选)";
    const buyoutN = Number(this.buyoutDraft) || 0;
    const buyoutLabel =
      buyoutN > 0 ? `${this.buyoutDraft}${unit}` : "无";
    const max = this.maxListCount();
    const countLabel =
      !this.selected
        ? "—"
        : this.selected.kind === "equip"
          ? "1(装备)"
          : isReservoirFlaskItemId(this.selected.itemId)
            ? `1(壶·储${this.selected.count})`
            : `${this.listCount}/${max} ←→`;
    return [
      { id: "pick", label: `选择物品  ${sel}` },
      { id: "count", label: `上架数量  ${countLabel}` },
      {
        id: "price",
        label: `起拍金额  ${this.priceDraft || "0"}${unit}`,
      },
      { id: "buyout", label: `一口价  ${buyoutLabel}` },
      { id: "currency", label: `货币  ${currencyLabel(this.currency)}` },
      {
        id: "duration",
        label: `时长  ${AUCTION_DURATION_LABELS[this.durationTier]} 费${this.feePercent()}%`,
      },
      {
        id: "submit",
        label: `确认上架  手费${formatAuctionFee(fee, this.currency)}`,
      },
      { id: "refresh", label: "刷新背包" },
    ];
  }

  private listRows(): (
    | { kind: "listing"; listing: AuctionListing }
    | { kind: "claim"; claim: AuctionClaimRow }
    | { kind: "action"; action: AuctionPanelAction; label: string }
  )[] {
    const out: (
      | { kind: "listing"; listing: AuctionListing }
      | { kind: "claim"; claim: AuctionClaimRow }
      | { kind: "action"; action: AuctionPanelAction; label: string }
    )[] = [];
    if (this.tab === "browse") {
      for (const r of this.rows) out.push({ kind: "listing", listing: r });
      const page = this.browsePage + 1;
      if (this.browsePage > 0) {
        out.push({
          kind: "action",
          action: "prev_page",
          label: `上一页(${page})`,
        });
      }
      if (this.browseHasMore) {
        out.push({
          kind: "action",
          action: "next_page",
          label: `下一页(${page})`,
        });
      }
      out.push({ kind: "action", action: "refresh", label: "刷新列表" });
    } else if (this.tab === "mine") {
      for (const r of this.mine) out.push({ kind: "listing", listing: r });
      const page = this.minePage + 1;
      if (this.minePage > 0) {
        out.push({
          kind: "action",
          action: "prev_page",
          label: `上一页(${page})`,
        });
      }
      if (this.mineHasMore) {
        out.push({
          kind: "action",
          action: "next_page",
          label: `下一页(${page})`,
        });
      }
      out.push({ kind: "action", action: "refresh", label: "刷新" });
    } else if (this.tab === "claim") {
      for (const c of this.claims) out.push({ kind: "claim", claim: c });
      const page = this.claimPage + 1;
      if (this.claimPage > 0) {
        out.push({
          kind: "action",
          action: "prev_page",
          label: `上一页(${page})`,
        });
      }
      if (this.claimHasMore) {
        out.push({
          kind: "action",
          action: "next_page",
          label: `下一页(${page})`,
        });
      }
      if (this.pendingClaims > 0) {
        out.push({
          kind: "action",
          action: "claim",
          label: `一键领取(${this.pendingClaims})`,
        });
      }
      out.push({ kind: "action", action: "refresh", label: "刷新" });
    }
    return out;
  }

  private feePercent(): number {
    return (
      this.status?.feePercents[this.durationTier] ?? 2 + this.durationTier * 2
    );
  }

  /** 装备与壶类不可调数量（壶按整行储量上架）. */
  private listQtyFixed(item: BagItem | null): boolean {
    if (!item) return true;
    return item.kind === "equip" || isReservoirFlaskItemId(item.itemId);
  }

  private defaultListCount(item: BagItem): number {
    if (this.listQtyFixed(item)) return 1;
    return Math.max(1, item.count);
  }

  private maxListCount(): number {
    if (!this.selected) return 1;
    return Math.max(1, bagVisibleStackCount(this.selected));
  }

  private clampListCount(): void {
    const max = this.maxListCount();
    this.listCount = Math.max(1, Math.min(this.listCount || 1, max));
  }

  private adjustListCount(dir: -1 | 1): void {
    if (!this.selected) {
      this.tip = "请先选择物品";
      this.redraw();
      return;
    }
    if (this.selected.kind === "equip") {
      this.listCount = 1;
      this.tip = "装备固定数量1";
      this.redraw();
      return;
    }
    if (isReservoirFlaskItemId(this.selected.itemId)) {
      this.listCount = 1;
      this.tip = `壶整件上架（储量${this.selected.count}）`;
      this.redraw();
      return;
    }
    const max = this.maxListCount();
    if (max <= 1) {
      this.listCount = 1;
      this.tip = "数量1";
      this.redraw();
      return;
    }
    this.listCount = ((this.listCount - 1 + dir + max) % max) + 1;
    this.tip = `数量 ${this.listCount}/${max}`;
    this.redraw();
  }

  /** 上架草稿 → 协议铜币/酷宝金额. */
  private startPriceWire(): number {
    const n = Math.max(1, Number(this.priceDraft) || 0);
    return this.currency === 0 ? goldToCopper(n) : n >>> 0;
  }

  private buyoutPriceWire(): number {
    const n = Math.max(0, Number(this.buyoutDraft) || 0);
    if (n <= 0) return 0;
    return this.currency === 0 ? goldToCopper(n) : n >>> 0;
  }

  private feePreviewWire(): number {
    const price = this.startPriceWire();
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
    const cur =
      this.priceTarget === "buyout" ? this.buyoutDraft : this.priceDraft;
    this.focusTimer = setTimeout(() => {
      this.focusTimer = null;
      if (!this.visible || this.openGen !== gen || !this.isPriceMode()) return;
      this.ime.focus(cur === "0" ? "" : cur);
    }, 50);
  }

  private commitPrice(): void {
    if (!this.isPriceMode()) return;
    const raw = this.priceTarget === "buyout" ? this.buyoutDraft : this.priceDraft;
    const n =
      this.priceTarget === "buyout"
        ? Math.max(0, Number(raw || this.ime.value) || 0)
        : Math.max(1, Number(raw || this.ime.value) || 0);
    if (this.priceTarget === "buyout") this.buyoutDraft = String(n);
    else this.priceDraft = String(n);
    this.releaseIme();
    this.listMode = "menu";
    this.menuCursor = this.priceTarget === "buyout" ? 3 : 6;
    if (this.priceTarget === "buyout") {
      this.tip =
        n > 0
          ? `一口价 ${n}${auctionPriceUnit(this.currency)}`
          : "未设一口价";
    } else {
      this.tip = `起拍 ${this.priceDraft}${auctionPriceUnit(this.currency)}`;
    }
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
    l.eventMode = "none";
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
    l.eventMode = "none";
    l.x = Math.floor(cx - l.width / 2);
    l.y = Math.floor(cy - l.height / 2);
    this.addChild(l);
    this.labels.push(l);
  }

  private paintEquipLine(line: EquipPaintLine, x: number, y: number): void {
    if (line.kind === "text") {
      this.addLabel(line.text.slice(0, 26), x, y, line.color);
      return;
    }
    this.addLabel(`${line.key}${line.value}`.slice(0, 26), x, y, line.keyColor);
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
    clearPointerHits(this.rowHits);
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
    if (this.detailListing) this.drawDetail(x, bodyTop, w, h - (bodyTop - y));
    else if (this.tab === "list") this.drawListTab(x, bodyTop, w, h - (bodyTop - y));
    else this.drawBrowseLike(x, bodyTop, w, h - (bodyTop - y));

    drawPanelTipOverlay(this, this.toast);
    if (this.isPriceMode()) {
      // Keep IME hit above labels/tips added this frame.
      this.addChild(this.inputHit);
      this.ime.show(this.priceInputRect());
    }
  }

  private drawDetail(
    x: number,
    bodyTop: number,
    w: number,
    bodyH: number,
  ): void {
    const r = this.detailListing;
    if (!r) return;
    const item = listingAsBagItem(r);
    const qColor = qualityColor(item.quality);
    const tipY = bodyTop + bodyH - 16;
    const acts = this.detailActions();
    const btnH = 18;
    const actionH = acts.length * (btnH + 2) + 8;
    const actionTop = tipY - actionH;

    const titleH = 20;
    this.box.rect(x + 6, bodyTop, w - 12, titleH).fill({ color: CBE.bgPanelDark });
    const en = r.enhanceLevel > 0 ? `+${r.enhanceLevel}` : "";
    this.addLabelCentered(
      `${item.name}${en}`.slice(0, 18),
      LCD_WIDTH / 2,
      bodyTop + titleH / 2,
      qColor,
    );

    const metaY = bodyTop + titleH + 4;
    this.addLabel(
      `卖家 ${r.sellerName || "#"} 现价${formatAuctionPrice(r.currentPrice, r.currency)}`.slice(
        0,
        26,
      ),
      x + 10,
      metaY,
      CBE.inkLabel,
    );
    const buy =
      r.buyoutPrice > 0
        ? `一口价${formatAuctionPrice(r.buyoutPrice, r.currency)}`
        : "无一口价";
    this.addLabel(
      `${buy} 剩${formatRemain(r.remainSec)}`.slice(0, 26),
      x + 10,
      metaY + 14,
      CBE.inkMint,
    );

    const bodyY = metaY + 30;
    const rowH = 14;
    const fitRows = Math.max(1, Math.floor((actionTop - bodyY - 2) / rowH));
    let lines: EquipPaintLine[] = [];
    if (item.kind === "equip") {
      lines = buildEquipDetailLines(item);
    } else {
      const desc =
        (item.desc && item.desc !== "-" ? item.desc : "") ||
        (isReservoirFlaskItemId(item.itemId)
          ? `储量 ${item.count}`
          : `数量 ${item.count}`);
      for (const line of wrapUiText(desc, w - 28, 12)) {
        lines.push({ kind: "text", text: line, color: CBE.ink });
      }
      lines.push({
        kind: "text",
        text: isReservoirFlaskItemId(item.itemId)
          ? `储量 ${item.count}`
          : `数量 ${item.count}`,
        color: CBE.inkLabel,
      });
    }
    const pageCount = Math.max(1, Math.ceil(lines.length / fitRows));
    const page = Math.min(Math.max(0, this.detailPage), pageCount - 1);
    this.detailPage = page;
    const start = page * fitRows;
    const pageLines = lines.slice(start, start + fitRows);
    let ly = bodyY;
    for (const line of pageLines) {
      this.paintEquipLine(line, x + 10, ly);
      ly += rowH;
    }
    if (pageCount > 1) {
      this.addLabel(
        `${page + 1}/${pageCount}`,
        x + w - 36,
        bodyY,
        CBE.inkLabel,
      );
    }

    this.box.rect(x + 4, actionTop, w - 8, actionH).fill({ color: CBE.bgPanel });
    acts.forEach((act, i) => {
      const hi = i === this.detailActionCursor;
      const by = actionTop + 4 + i * (btnH + 2);
      if (hi) this.box.rect(x + 8, by, w - 16, btnH).fill({ color: BAG_SELECT });
      this.addLabel(
        act.label.slice(0, 24),
        x + 14,
        by + 2,
        hi ? EQUIP_DETAIL_YELLOW : CBE.ink,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, x + 8, by, w - 16, btnH, () => {
          this.detailActionCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    });
    this.addLabel(this.tip || "上下选操作 ←→翻页", x + 10, tipY, CBE.inkMint);
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
    if (this.tab === "browse") {
      hint = this.browseHasMore || this.browsePage > 0
        ? `←→标签 第${this.browsePage + 1}页 确定查看`
        : "←→标签 确定查看";
    }
    if (this.tab === "mine") {
      hint = this.mineHasMore || this.minePage > 0
        ? `←→标签 第${this.minePage + 1}页 确定查看`
        : "←→标签 确定查看";
    }
    if (this.tab === "claim") {
      hint =
        this.claimHasMore || this.claimPage > 0
          ? `←→标签 第${this.claimPage + 1}页 待领${this.pendingClaims}`
          : this.pendingClaims > 0
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
      else if (row.kind === "claim") label = formatAuctionClaimRow(row.claim);
      else {
        const r = row.listing;
        const en = r.enhanceLevel > 0 ? `+${r.enhanceLevel}` : "";
        const buy =
          r.buyoutPrice > 0
            ? `一口${formatAuctionPrice(r.buyoutPrice, r.currency)}`
            : "";
        let tag = "";
        if (this.tab === "mine") {
          if (this.isMyListing(r)) tag = r.highBidderRoleId ? "卖·有价" : "卖";
          else if (this.isMyHighBid(r)) tag = "竞·领先";
          else tag = "竞";
        } else {
          tag = formatRemain(r.remainSec);
        }
        const qty =
          isReservoirFlaskItemId(r.itemId) ? "" : `x${r.itemCount}`;
        label =
          `${r.itemName}${en}${qty} ${formatAuctionPrice(r.currentPrice, r.currency)} ${buy} ${tag}`.trim();
      }
      this.addLabel(label.slice(0, 26), x + 10, ry + 1, hi ? CBE.menuHi : CBE.ink);
      const hitIdx = idx;
      this.rowHits.push(
        attachRowPointerHit(this, x + 6, ry, w - 12, rowH - 1, () => {
          this.listCursor = hitIdx;
          this.redraw();
          this.confirm();
        }),
      );
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
          const qty = isReservoirFlaskItemId(it.itemId)
            ? `储${it.count}`
            : `×${it.count}`;
          this.addLabel(
            `${it.name}${en} ${qty}`.slice(0, 22),
            x + 10,
            ry + 1,
            hi ? CBE.ink : qualityColor(it.quality),
          );
          const hitIdx = idx;
          this.rowHits.push(
            attachRowPointerHit(this, x + 6, ry, w - 12, 15, () => {
              this.bagCursor = hitIdx;
              this.redraw();
              this.confirm();
            }),
          );
        });
      }
      this.addLabel(this.tip || "返回菜单", x + 10, tipY, CBE.inkMint);
      return;
    }

    if (this.listMode === "price") {
      const unit = auctionPriceUnit(this.currency);
      const title =
        this.priceTarget === "buyout"
          ? `设置一口价(0=无${unit})`
          : `设置起拍金额(${unit})`;
      this.addLabel(title, x + 10, bodyTop, CBE.inkLabel);
      const sel = this.selected
        ? `${this.selected.name}${this.selected.enhance ? `+${this.selected.enhance}` : ""}`
        : "(未选物品)";
      this.addLabel(sel.slice(0, 22), x + 10, bodyTop + 18, CBE.inkMint);
      this.addLabel(
        `货币 ${currencyLabel(this.currency)} 起拍${this.priceDraft || "0"}${unit}`,
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
      const shownDraft =
        this.priceTarget === "buyout" ? this.buyoutDraft : this.priceDraft;
      const shown = shownDraft.length ? shownDraft : "点击输入…";
      this.addLabel(
        shown,
        rect.x,
        rect.y + 4,
        shownDraft.length ? CBE.ink : CBE.inkLabel,
      );
      this.addLabel(
        this.priceTarget === "buyout"
          ? `一口价须≥起拍；单位${unit}`
          : `手费预览 ${formatAuctionFee(this.feePreviewWire(), this.currency)} (${this.feePercent()}%)`,
        x + 10,
        rect.y + 30,
        CBE.inkMint,
      );
      this.addLabel(this.tip || "确定保存金额", x + 10, tipY, CBE.inkMint);
      return;
    }

    this.addLabel("←→标签/调数量·货币·时长", x + 10, bodyTop, CBE.inkLabel);
    const menu = this.listMenu();
    const listTop = bodyTop + 18;
    menu.forEach((row, i) => {
      const ry = listTop + i * 16;
      const hi = i === this.menuCursor;
      if (hi) this.box.rect(x + 6, ry, w - 12, 15).fill({ color: BAG_SELECT });
      this.addLabel(row.label.slice(0, 26), x + 10, ry + 1, hi ? CBE.menuHi : CBE.ink);
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, x + 6, ry, w - 12, 15, () => {
          this.menuCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    });
    this.addLabel(
      this.tip || "←→切标签；数量/货币/时长行可调",
      x + 10,
      tipY,
      CBE.inkMint,
    );
  }
}
