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
import { BitmapLabel, wrapChars } from "./BitmapLabel";
import { ChatIme } from "./ChatIme";
import {
  bagVisibleStackCount,
  isDurableMallPassItemId,
  isReservoirFlaskItemId,
  type BagItem,
} from "../net/protocolExtras";
import { lookupItemSync, qualityColor } from "../resources/itemCatalog";
import {
  AUCTION_DURATION_LABELS,
  auctionBidStep,
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
import { attachPageNavButtons } from "./pageNav";

export type AuctionPanelAction =
  | "bid"
  | "buyout"
  | "buyout_challenge"
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
  /** 一口价验证码界面 */
  private buyoutCaptchaOpen = false;
  private buyoutCaptchaCode = "";
  private buyoutCaptchaDraft = "";
  private buyoutCaptchaListingId = 0;
  private ime = new ChatIme({
    maxLength: 10,
    inputMode: "numeric",
    enterkeyhint: "done",
    ariaLabel: "竞拍金额",
    sanitize: (raw) => raw.replace(/\D/g, "").replace(/^0+(?=\d)/, ""),
  });
  private captchaIme = new ChatIme({
    maxLength: 4,
    inputMode: "text",
    enterkeyhint: "done",
    ariaLabel: "一口价验证码",
    sanitize: (raw) =>
      raw
        .toUpperCase()
        .replace(/[^A-Z0-9]/g, "")
        .slice(0, 4),
  });

  onAction: ((action: AuctionPanelAction, listingId?: number) => void) | null =
    null;
  /** Softkey labels may change after tab switch (esp. tap). */
  onSoftkeys: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.addChild(this.box);
    this.inputHit.eventMode = "static";
    this.inputHit.cursor = "text";
    this.inputHit.on("pointertap", () => {
      if (this.isBuyoutCaptchaMode()) this.focusCaptchaInput();
      else this.focusPriceInput();
    });
    this.addChild(this.inputHit);
    this.ime.onChange = (v) => {
      const next = v || "0";
      if (this.priceTarget === "buyout") this.buyoutDraft = next;
      else this.priceDraft = next;
      if (this.visible && this.listMode === "price") this.redraw();
    };
    this.ime.onSubmit = () => this.commitPrice();
    this.captchaIme.onChange = (v) => {
      this.buyoutCaptchaDraft = v;
      if (this.visible && this.buyoutCaptchaOpen) this.redraw();
    };
    this.captchaIme.onSubmit = () => this.confirmBuyoutCaptcha();
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
    this.clearBuyoutCaptcha();
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
      if (!this.detailListing && this.buyoutCaptchaOpen) {
        this.clearBuyoutCaptcha();
      }
    }
    if (this.visible) this.redraw();
  }

  /** 从背包「拍卖」动作进入时预选物品并切到上架页。 */
  preselectBagItem(item: BagItem): void {
    this.clearBuyoutCaptcha();
    this.activeTab = 2;
    this.listMode = "menu";
    this.detailListing = null;
    this.selected = item;
    this.listCount = this.defaultListCount(item);
    this.menuCursor = this.listQtyFixed(item) ? 2 : 1; // 起拍 / 数量
    this.tip = isReservoirFlaskItemId(item.itemId)
      ? `已选 ${item.name}（整壶·储量${item.count}）`
      : isDurableMallPassItemId(item.itemId)
        ? `已选 ${item.name}（整张·耐久${item.count}）`
        : `已选 ${item.name}`;
    if (this.visible) this.redraw();
  }

  close(): void {
    this.clearBuyoutCaptcha();
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
    if (this.buyoutCaptchaOpen) {
      return { left: "确认", mid: "确认", right: "返回" };
    }
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
    this.onSoftkeys?.();
  }

  /** Tap a top tab (absolute index). */
  selectTab(index: number): void {
    if (!this.visible) return;
    if (this.detailListing) return;
    if (index < 0 || index >= TABS.length || index === this.activeTab) return;
    this.releaseIme();
    this.listMode = "menu";
    this.activeTab = index;
    this.listCursor = 0;
    this.menuCursor = 0;
    this.tip = "";
    this.redraw();
    this.onSoftkeys?.();
  }

  move(dir: -1 | 1): void {
    if (!this.visible) return;
    if (this.buyoutCaptchaOpen) return;
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
    if (this.buyoutCaptchaOpen) return false;
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
    if (this.buyoutCaptchaOpen) {
      this.confirmBuyoutCaptcha();
      return;
    }
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
    if (this.buyoutCaptchaOpen) {
      this.clearBuyoutCaptcha();
      this.tip = "";
      this.redraw();
      this.onSoftkeys?.();
      return true;
    }
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

  isBuyoutCaptchaMode(): boolean {
    return this.visible && this.buyoutCaptchaOpen;
  }

  /** After 42/11 challenge: show captcha entry for listing. */
  showBuyoutCaptcha(listingId: number, captcha: string): void {
    if (!this.detailListing || this.detailListing.listingId !== listingId) {
      this.tip = "拍品已变化";
      this.redraw();
      return;
    }
    this.buyoutCaptchaOpen = true;
    this.buyoutCaptchaListingId = listingId;
    this.buyoutCaptchaCode = (captcha || "").toUpperCase().slice(0, 4);
    this.buyoutCaptchaDraft = "";
    this.tip = "输入验证码后确认一口价";
    this.redraw();
    this.onSoftkeys?.();
    this.focusCaptchaInput();
  }

  getBuyoutCaptchaAnswer(): string {
    return this.buyoutCaptchaDraft.trim().toUpperCase();
  }

  getBuyoutCaptchaListingId(): number {
    return this.buyoutCaptchaListingId;
  }

  refreshBuyoutCaptchaCode(captcha: string): void {
    this.buyoutCaptchaCode = (captcha || "").toUpperCase().slice(0, 4);
    this.buyoutCaptchaDraft = "";
    this.captchaIme.setValue("");
    if (this.visible && this.buyoutCaptchaOpen) {
      this.redraw();
      this.focusCaptchaInput();
    }
  }

  typePriceKey(key: string): boolean {
    if (this.isBuyoutCaptchaMode()) {
      if (this.captchaIme.isFocused()) return false;
      if (/^[a-zA-Z0-9]$/.test(key) && this.buyoutCaptchaDraft.length < 4) {
        this.buyoutCaptchaDraft = (this.buyoutCaptchaDraft + key.toUpperCase()).slice(
          0,
          4,
        );
        this.captchaIme.setValue(this.buyoutCaptchaDraft);
        this.redraw();
        return true;
      }
      if (key === "backspace") {
        this.buyoutCaptchaDraft = this.buyoutCaptchaDraft.slice(0, -1);
        this.captchaIme.setValue(this.buyoutCaptchaDraft);
        this.redraw();
        return true;
      }
      return false;
    }
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
     * 834/839 凭证 wire count=剩余耐久，整行上架；
     * 普通道具才按 listCount 部分上架。
     */
    let count: number;
    if (item.kind === "equip") count = 1;
    else if (isReservoirFlaskItemId(item.itemId)) count = Math.max(1, item.count);
    else if (isDurableMallPassItemId(item.itemId)) count = Math.max(1, item.count);
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
    this.clearBuyoutCaptcha();
    this.releaseIme();
    this.ime.destroy();
    this.captchaIme.destroy();
    super.destroy(options);
  }

  private clearBuyoutCaptcha(): void {
    this.buyoutCaptchaOpen = false;
    this.buyoutCaptchaCode = "";
    this.buyoutCaptchaDraft = "";
    this.buyoutCaptchaListingId = 0;
    this.captchaIme.hide();
  }

  private openDetail(listing: AuctionListing): void {
    this.clearBuyoutCaptcha();
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
      {
        id: "bid",
        label: r.highBidderRoleId
          ? `出价(+${formatAuctionPrice(auctionBidStep(r.startPrice), r.currency)})`
          : `出价 ${formatAuctionPrice(r.currentPrice, r.currency)}`,
      },
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
    if (act.id === "buyout") {
      this.tip = "获取验证码…";
      this.redraw();
      this.onAction?.("buyout_challenge", r.listingId);
      return;
    }
    this.onAction?.(act.id, r.listingId);
  }

  private confirmBuyoutCaptcha(): void {
    const id = this.buyoutCaptchaListingId;
    const ans = this.getBuyoutCaptchaAnswer();
    if (!id || !this.buyoutCaptchaCode) {
      this.tip = "验证码无效，请返回重试";
      this.redraw();
      return;
    }
    if (ans.length < 4) {
      this.tip = "请输入4位验证码";
      this.redraw();
      this.focusCaptchaInput();
      return;
    }
    if (ans !== this.buyoutCaptchaCode) {
      this.tip = "验证码错误";
      this.buyoutCaptchaDraft = "";
      this.captchaIme.setValue("");
      this.redraw();
      this.onAction?.("buyout_challenge", id);
      return;
    }
    this.releaseIme();
    this.onAction?.("buyout", id);
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
        : isDurableMallPassItemId(it.itemId)
          ? `已选 ${it.name}（整张·耐久${it.count}）`
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
        this.currency === 0
          ? "一口价金(0=无)须≥起拍且≤10倍"
          : "一口价酷宝(0=无)须≥起拍且≤10倍";
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
      if (buyout > 0 && buyout > price * 10) {
        this.tip = "一口价不能超过起拍价10倍";
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
            : isDurableMallPassItemId(this.selected.itemId)
              ? `1(凭证·耐久${this.selected.count})`
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

  /** 装备 / 壶 / 商城凭证不可调数量（整行上架）. */
  private listQtyFixed(item: BagItem | null): boolean {
    if (!item) return true;
    return (
      item.kind === "equip" ||
      isReservoirFlaskItemId(item.itemId) ||
      isDurableMallPassItemId(item.itemId)
    );
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
    if (isDurableMallPassItemId(this.selected.itemId)) {
      this.listCount = 1;
      this.tip = `凭证整张上架（耐久${this.selected.count}）`;
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
    if (this.priceTarget === "buyout") {
      const start = Math.max(1, Number(this.priceDraft) || 0);
      if (n > 0 && n < start) {
        this.tip = "一口价须≥起拍价";
        this.buyoutDraft = String(n);
        this.redraw();
        return;
      }
      if (n > 0 && n > start * 10) {
        this.tip = "一口价不能超过起拍价10倍";
        this.buyoutDraft = String(n);
        this.redraw();
        return;
      }
      this.buyoutDraft = String(n);
    } else {
      this.priceDraft = String(n);
    }
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
    this.captchaIme.hide();
    this.inputHit.clear();
  }

  private focusCaptchaInput(): void {
    if (!this.isBuyoutCaptchaMode()) return;
    const rect = this.captchaInputRect();
    this.captchaIme.show(rect);
    this.inputHit.clear();
    this.inputHit.rect(rect.x, rect.y, rect.w, rect.h).fill({
      color: 0x000000,
      alpha: 0.01,
    });
    this.addChild(this.inputHit);
    if (this.focusTimer) clearTimeout(this.focusTimer);
    const gen = ++this.openGen;
    this.focusTimer = setTimeout(() => {
      this.focusTimer = null;
      if (!this.visible || this.openGen !== gen || !this.isBuyoutCaptchaMode())
        return;
      this.captchaIme.focus(this.buyoutCaptchaDraft);
    }, 50);
  }

  private priceInputRect(): { x: number; y: number; w: number; h: number } {
    return { x: 20, y: 118, w: LCD_WIDTH - 40, h: 22 };
  }

  private captchaInputRect(): { x: number; y: number; w: number; h: number } {
    return { x: 20, y: 128, w: LCD_WIDTH - 40, h: 24 };
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

  private paintEquipLine(line: EquipPaintLine, x: number, y: number): number {
    if (line.kind === "text") {
      this.addLabel(line.text, x, y, line.color);
      return wrapChars(line.text).length;
    }
    this.addLabel(`${line.key}${line.value}`, x, y, line.keyColor);
    return wrapChars(`${line.key}${line.value}`).length;
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
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, tx, y, tw, h, () => this.selectTab(idx)),
      );
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
    if (this.buyoutCaptchaOpen && this.detailListing)
      this.drawBuyoutCaptcha(x, bodyTop, w, h - (bodyTop - y));
    else if (this.detailListing) this.drawDetail(x, bodyTop, w, h - (bodyTop - y));
    else if (this.tab === "list") this.drawListTab(x, bodyTop, w, h - (bodyTop - y));
    else this.drawBrowseLike(x, bodyTop, w, h - (bodyTop - y));

    drawPanelTipOverlay(this, this.toast);
    if (this.isBuyoutCaptchaMode()) {
      this.addChild(this.inputHit);
      this.captchaIme.show(this.captchaInputRect());
    } else if (this.isPriceMode()) {
      // Keep IME hit above labels/tips added this frame.
      this.addChild(this.inputHit);
      this.ime.show(this.priceInputRect());
    }
  }

  private drawBuyoutCaptcha(
    x: number,
    bodyTop: number,
    w: number,
    bodyH: number,
  ): void {
    const r = this.detailListing;
    if (!r) return;
    const tipY = bodyTop + bodyH - 16;
    const item = listingAsBagItem(r);
    const qColor = qualityColor(item.quality);
    const titleH = 20;
    this.box.rect(x + 6, bodyTop, w - 12, titleH).fill({ color: CBE.bgPanelDark });
    const en = r.enhanceLevel > 0 ? `+${r.enhanceLevel}` : "";
    this.addLabelCentered(
      `${item.name}${en}`,
      LCD_WIDTH / 2,
      bodyTop + titleH / 2,
      qColor,
    );
    this.addLabel(
      `一口价 ${formatAuctionPrice(r.buyoutPrice, r.currency)}`,
      x + 10,
      bodyTop + titleH + 8,
      CBE.inkMint,
    );
    this.addLabel("防秒拍验证码", x + 10, bodyTop + titleH + 28, EQUIP_DETAIL_YELLOW);
    this.addLabel(
      `请输入: ${this.buyoutCaptchaCode || "----"}`,
      x + 10,
      bodyTop + titleH + 46,
      CBE.ink,
    );
    const rect = this.captchaInputRect();
    this.box.rect(rect.x, rect.y, rect.w, rect.h).fill({ color: BAG_SELECT });
    const draft = this.buyoutCaptchaDraft || "_";
    this.addLabel(`输入 ${draft}`, rect.x + 6, rect.y + 4, EQUIP_DETAIL_YELLOW);
    this.rowHits.push(
      attachRowPointerHit(this, rect.x, rect.y, rect.w, rect.h, () => {
        this.focusCaptchaInput();
      }),
    );
    this.addLabel(
      this.tip || "输入上方验证码后确认",
      x + 10,
      tipY,
      CBE.inkMint,
    );
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
      `${item.name}${en}`,
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
      `${buy} 剩${formatRemain(r.remainSec)}`,
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
          : isDurableMallPassItemId(item.itemId)
            ? `耐久 ${item.count}`
            : `数量 ${item.count}`);
      for (const line of wrapChars(desc)) {
        lines.push({ kind: "text", text: line, color: CBE.ink });
      }
      lines.push({
        kind: "text",
        text: isReservoirFlaskItemId(item.itemId)
          ? `储量 ${item.count}`
          : isDurableMallPassItemId(item.itemId)
            ? `耐久 ${item.count}`
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
      const rows = this.paintEquipLine(line, x + 10, ly);
      ly += rowH * Math.max(1, rows);
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
        act.label,
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
    const baseRowH = 16;
    const maxShow = Math.max(1, Math.floor((tipY - listTop - 4) / baseRowH));
    let start = 0;
    if (this.listCursor >= maxShow) start = this.listCursor - maxShow + 1;
    if (!rows.length) {
      this.addLabel("(空)", x + 12, listTop + 4, CBE.inkMint);
    }
    let listY = listTop;
    for (let idx = start; idx < rows.length && listY < tipY - 4; idx++) {
      const row = rows[idx]!;
      const hi = idx === this.listCursor;
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
        let qtyTag = "";
        if (isReservoirFlaskItemId(r.itemId)) qtyTag = `储${r.itemCount}`;
        else if (isDurableMallPassItemId(r.itemId)) qtyTag = `耐${r.itemCount}`;
        else qtyTag = `x${r.itemCount}`;
        label =
          `${r.itemName}${en}${qtyTag} ${formatAuctionPrice(r.currentPrice, r.currency)} ${buy} ${tag}`.trim();
      }
      const lineCount = wrapChars(label).length;
      const effRowH = Math.max(baseRowH, lineCount * 14 + 2);
      if (hi) this.box.rect(x + 6, listY, w - 12, effRowH - 1).fill({ color: BAG_SELECT });
      this.addLabel(label, x + 10, listY + 1, hi ? CBE.menuHi : CBE.ink);
      const hitIdx = idx;
      this.rowHits.push(
        attachRowPointerHit(this, x + 6, listY, w - 12, effRowH - 1, () => {
          this.listCursor = hitIdx;
          this.redraw();
          this.confirm();
        }),
      );
      listY += effRowH;
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
            : isDurableMallPassItemId(it.itemId)
              ? `耐${it.count}`
              : `×${it.count}`;
          this.addLabel(
            `${it.name}${en} ${qty}`,
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
        const pages = Math.max(1, Math.ceil(this.bag.length / BAG_PAGE_SIZE));
        if (pages > 1) {
          const page = Math.floor(this.bagScroll / BAG_PAGE_SIZE) + 1;
          attachPageNavButtons({
            host: this,
            hits: this.rowHits,
            box: this.box,
            x: x + 8,
            y: tipY - 22,
            w: w - 16,
            page,
            pages,
            addLabel: (t, lx, ly, c) => this.addLabel(t, lx, ly, c),
            onPrev: () => {
              this.bagScroll = Math.max(0, this.bagScroll - BAG_PAGE_SIZE);
              this.bagCursor = Math.min(
                this.bagCursor,
                this.bagScroll + BAG_PAGE_SIZE - 1,
              );
              this.bagCursor = Math.max(this.bagScroll, this.bagCursor);
              this.redraw();
            },
            onNext: () => {
              const maxScroll = Math.max(0, this.bag.length - BAG_PAGE_SIZE);
              this.bagScroll = Math.min(maxScroll, this.bagScroll + BAG_PAGE_SIZE);
              this.bagCursor = Math.max(this.bagCursor, this.bagScroll);
              this.bagCursor = Math.min(
                this.bag.length - 1,
                this.bagScroll + BAG_PAGE_SIZE - 1,
              );
              this.redraw();
            },
          });
        }
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
      this.addLabel(sel, x + 10, bodyTop + 18, CBE.inkMint);
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
    let menuY = listTop;
    menu.forEach((row, i) => {
      const hi = i === this.menuCursor;
      const lineCount = wrapChars(row.label).length;
      const effRowH = Math.max(16, lineCount * 14 + 2);
      if (hi) this.box.rect(x + 6, menuY, w - 12, effRowH - 1).fill({ color: BAG_SELECT });
      this.addLabel(row.label, x + 10, menuY + 1, hi ? CBE.menuHi : CBE.ink);
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, x + 6, menuY, w - 12, effRowH - 1, () => {
          this.menuCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
      menuY += effRowH;
    });
    this.addLabel(
      this.tip || "←→切标签；数量/货币/时长行可调",
      x + 10,
      tipY,
      CBE.inkMint,
    );
  }
}
