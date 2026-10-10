import { Container, Graphics } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbeTitledChrome,
  cbePopupBounds,
  makePixelText,
  blockPanelPointerPassthrough,
} from "./constants";
import {
  bagVisibleStackCount,
  equipWashNameTag,
  isDurableMallPassItemId,
  isReservoirFlaskItemId,
  ROLE_SEAL_SCROLL_ITEM_ID,
  roleJobDisplayName,
  sortBagByName,
  type BagItem,
} from "../net/protocolExtras";
import { lookupItemSync, qualityColor, WORN_SLOT_NAMES } from "../resources/itemCatalog";
import type {
  ZangbaoListing,
  ZangbaoSealDetail,
  ZangbaoSealItemRow,
} from "../net/zangbaoApi";
import { formatPanelLevelExp } from "../game/roleLevelExp";
import {
  buildEquipDetailLines,
  enhanceAttrsFromWashText,
  EQUIP_DETAIL_YELLOW,
  type EquipPaintLine,
} from "./equipDetail";
import { wrapChars } from "./BitmapLabel";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { panelSafeFootY } from "./pageNav";

export type ZangbaoPanelAction =
  | "buy"
  | "cancel"
  | "list"
  | "refresh"
  | "close"
  | "withdraw"
  | "upload_payee"
  | "prev"
  | "next";

type Tab = "browse" | "mine" | "list" | "kcoin";

const TABS: { id: Tab; label: string }[] = [
  { id: "browse", label: "浏览" },
  { id: "mine", label: "我的" },
  { id: "list", label: "上架" },
  { id: "kcoin", label: "酷币" },
];

const ROW_H = 16;
const PAGE = 7;
/** 上架最低酷币价（与服端 VM_MOCK_ZANGBAO_MIN_PRICE 一致）。 */
const MIN_PRICE_KCOIN = 10;
/** 提现最低酷币（与服端 VM_MOCK_KCOIN_WITHDRAW_MIN 一致，且须为 10 的倍数）。 */
const MIN_WITHDRAW_KCOIN = 50;

type AmountKind = "list" | "withdraw";
type DetailActionId = "buy" | "cancel" | "back";
type SealTabId = "panel" | "equip" | "bag" | "wh";
const SEAL_TABS: { id: SealTabId; label: string }[] = [
  { id: "panel", label: "面板" },
  { id: "equip", label: "装备" },
  { id: "bag", label: "背包" },
  { id: "wh", label: "仓库" },
];

function listingAsBagItem(r: ZangbaoListing): BagItem {
  const cat = lookupItemSync(r.itemId);
  const kind = cat?.kind === "equip" ? "equip" : "item";
  const enhanceAttrs =
    kind === "equip"
      ? enhanceAttrsFromWashText(r.itemId, r.enhanceWash)
      : undefined;
  const gemReflect = r.gemReflect ?? 0;
  const gemCombo = r.gemCombo ?? 0;
  const gemRevive = r.gemRevive ?? 0;
  const gems =
    kind === "equip" && (gemReflect > 0 || gemCombo > 0 || gemRevive > 0)
      ? {
          reflect: gemReflect,
          combo: gemCombo,
          revive: gemRevive,
        }
      : undefined;
  return {
    itemId: r.itemId,
    seq: 0,
    count: r.itemCount,
    name: cat?.name || `物品${r.itemId}`,
    quality: cat?.quality ?? 0,
    enhance: r.enhanceLevel,
    kind,
    category: cat?.category ?? 0,
    desc: cat?.desc ?? "",
    usable: false,
    equipSlot: cat?.equipSlot ?? -1,
    ...(enhanceAttrs?.length ? { enhanceAttrs } : {}),
    ...(gems ? { socketGems: gems } : {}),
  };
}

function zangbaoListingSortName(r: ZangbaoListing): string {
  if (r.itemId === ROLE_SEAL_SCROLL_ITEM_ID && r.sealedRole?.name) {
    return `[印]${r.sealedRole.name}`;
  }
  return lookupItemSync(r.itemId)?.name || `#${r.itemId}`;
}

function sortZangbaoListings(rows: ZangbaoListing[]): ZangbaoListing[] {
  return rows
    .slice()
    .sort((a, b) =>
      zangbaoListingSortName(a).localeCompare(zangbaoListingSortName(b), "zh"),
    );
}

export class ZangbaoPanel extends Container {
  private bg = new Graphics();
  private tip: PanelTipState = createPanelTipState();
  private rowHits: Container[] = [];
  private labels: Container[] = [];
  private tab: Tab = "browse";
  private rowIndex = 0;
  private page = 0;
  private browse: ZangbaoListing[] = [];
  private mine: ZangbaoListing[] = [];
  private bag: BagItem[] = [];
  private kcoin = 0;
  private hasPayee = false;
  private withdrawRows: { withdrawId: number; amount: number; status: number }[] =
    [];
  /** LCD 数字键盘草稿（对齐商城赞助，非 HTML prompt）。 */
  private priceDigits = "";
  private priceMode = false;
  private amountKind: AmountKind = "list";
  /** 上架数量选择态（可堆叠道具：选物→数量→定价）。 */
  private countMode = false;
  /** 本次上架数量（装备/壶/凭证/封印卷固定整行）。 */
  private listCount = 1;
  /** 浏览/我的：拍品详情（对齐物品竞拍）。 */
  private detailListing: ZangbaoListing | null = null;
  private detailActionCursor = 0;
  private detailPage = 0;
  private sealDetail: ZangbaoSealDetail | null = null;
  private sealLoading = false;
  private sealTab: SealTabId = "panel";
  /** 装备/背包/仓库列表光标（与底栏操作共用上下键）。 */
  private sealListCursor = 0;
  /** 是否正在查看列表中某一件的详情。 */
  private sealItemOpen = false;
  private sealDetailLoader:
    | ((listingId: number) => Promise<ZangbaoSealDetail | null>)
    | null = null;
  onAction:
    | ((a: ZangbaoPanelAction, payload?: Record<string, number | string>) => void)
    | null = null;
  /** 金额键盘 / 详情进出时刷新壳软键。 */
  onSoftkeysChange: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(this.bg);
    blockPanelPointerPassthrough(this);
  }

  open(data: {
    browse: ZangbaoListing[];
    mine: ZangbaoListing[];
    bag: BagItem[];
    kcoin: number;
    hasPayee: boolean;
    withdraws?: { withdrawId: number; amount: number; status: number }[];
    preferTab?: Tab;
  }): void {
    this.browse = sortZangbaoListings(data.browse || []);
    this.mine = sortZangbaoListings(data.mine || []);
    this.bag = sortBagByName(data.bag || []);
    this.kcoin = data.kcoin || 0;
    this.hasPayee = !!data.hasPayee;
    this.withdrawRows = data.withdraws || [];
    this.tab = data.preferTab || "browse";
    this.rowIndex = 0;
    this.page = 0;
    this.priceMode = false;
    this.countMode = false;
    this.listCount = 1;
    this.amountKind = "list";
    this.priceDigits = "";
    this.clearDetail();
    this.visible = true;
    this.redraw();
  }

  setData(
    partial: Partial<{
      browse: ZangbaoListing[];
      mine: ZangbaoListing[];
      bag: BagItem[];
      kcoin: number;
      hasPayee: boolean;
      withdraws: { withdrawId: number; amount: number; status: number }[];
    }>,
  ): void {
    if (partial.browse) this.browse = sortZangbaoListings(partial.browse);
    if (partial.mine) this.mine = sortZangbaoListings(partial.mine);
    if (partial.bag) this.bag = sortBagByName(partial.bag);
    if (partial.kcoin != null) this.kcoin = partial.kcoin;
    if (partial.hasPayee != null) this.hasPayee = partial.hasPayee;
    if (partial.withdraws) this.withdrawRows = partial.withdraws;
    if (this.detailListing) {
      const id = this.detailListing.listingId;
      this.detailListing =
        this.browse.find((x) => x.listingId === id) ||
        this.mine.find((x) => x.listingId === id) ||
        null;
      if (!this.detailListing) {
        this.detailActionCursor = 0;
        this.detailPage = 0;
      }
    }
    this.redraw();
    this.notifySoftkeys();
  }

  close(): void {
    this.visible = false;
    this.clearDetail();
    clearPanelTip(this.tip);
    this.onAction?.("close");
  }

  softkeys(): { left: string; right: string } {
    if (this.priceMode) {
      return {
        left: this.amountKind === "withdraw" ? "提现" : "上架",
        right: "返回",
      };
    }
    if (this.countMode) {
      return { left: "定价", right: "返回" };
    }
    if (this.detailListing) {
      if (this.sealItemOpen) return { left: "", right: "返回" };
      if (this.isSealListTab()) {
        const rows = this.sealListRows();
        const onItem = rows.length > 0 && this.sealListCursor < rows.length;
        return { left: onItem ? "查看" : "确认", right: "返回" };
      }
      return { left: "确认", right: "返回" };
    }
    if (this.tab === "browse") {
      return {
        left: this.browse[this.rowIndex] ? "查看" : "",
        right: "关闭",
      };
    }
    if (this.tab === "mine") {
      return {
        left: this.mine[this.rowIndex] ? "查看" : "",
        right: "关闭",
      };
    }
    if (this.tab === "list") {
      return {
        left: this.bag[this.rowIndex] ? "上架" : "",
        right: "关闭",
      };
    }
    // 酷币页：左键随当前行变化
    const left =
      this.rowIndex === 1
        ? this.hasPayee
          ? "更换"
          : "上传"
        : this.rowIndex === 2
          ? "提现"
          : this.rowIndex === 3
            ? "刷新"
            : "";
    return { left, right: "关闭" };
  }

  private notifySoftkeys(): void {
    this.onSoftkeysChange?.();
  }

  isPriceMode(): boolean {
    return this.priceMode;
  }

  isCountMode(): boolean {
    return this.countMode;
  }

  isDetailMode(): boolean {
    return !!this.detailListing;
  }

  showTip(msg: string): void {
    showPanelTip(this.tip, msg, () => this.redraw());
    this.redraw();
  }

  switchTab(dir: -1 | 1): void {
    if (this.priceMode || this.countMode || this.detailListing) return;
    const i = TABS.findIndex((t) => t.id === this.tab);
    const n = (i + dir + TABS.length) % TABS.length;
    this.tab = TABS[n].id;
    this.rowIndex = 0;
    this.page = 0;
    this.redraw();
    this.notifySoftkeys();
  }

  /**
   * 左右方向键：数量态调上架数；详情翻页；否则切顶栏标签。
   * @returns true 已处理。
   */
  horizontal(dir: -1 | 1): boolean {
    if (!this.visible || this.priceMode) return false;
    if (this.countMode) {
      this.adjustListCount(dir);
      return true;
    }
    if (this.detailListing) {
      this.detailPage = Math.max(0, this.detailPage + dir);
      this.redraw();
      return true;
    }
    this.switchTab(dir);
    return true;
  }

  move(dir: -1 | 1): void {
    if (this.priceMode || this.countMode) return;
    if (this.detailListing) {
      if (this.sealItemOpen) return;
      if (this.isSealListTab()) {
        const rows = this.sealListRows();
        const acts = this.detailActions();
        const total = rows.length + acts.length;
        if (!total) return;
        const cur =
          this.sealListCursor < rows.length
            ? this.sealListCursor
            : rows.length + this.detailActionCursor;
        const next = (cur + dir + total) % total;
        if (next < rows.length) {
          this.sealListCursor = next;
          this.detailActionCursor = 0;
        } else {
          this.sealListCursor = rows.length;
          this.detailActionCursor = next - rows.length;
        }
        this.ensureSealListPageVisible(rows.length);
        this.redraw();
        this.notifySoftkeys();
        return;
      }
      const acts = this.detailActions();
      if (!acts.length) return;
      this.detailActionCursor =
        (this.detailActionCursor + dir + acts.length) % acts.length;
      this.redraw();
      return;
    }
    const rows = this.currentRows();
    if (!rows.length) return;
    const prev = this.rowIndex;
    this.rowIndex = Math.max(0, Math.min(rows.length - 1, this.rowIndex + dir));
    if (this.rowIndex < this.page * PAGE)
      this.page = Math.floor(this.rowIndex / PAGE);
    if (this.rowIndex >= (this.page + 1) * PAGE)
      this.page = Math.floor(this.rowIndex / PAGE);
    this.redraw();
    if (this.rowIndex !== prev) this.notifySoftkeys();
  }

  /** 金额输入态 ←→ 微调酷币（上架 ±1 最低 10；提现 ±10 最低 50）。 */
  adjustPrice(delta: number): void {
    if (!this.priceMode) return;
    const cur = Math.floor(Number(this.priceDigits.trim() || "0"));
    const base = Number.isFinite(cur) ? cur : 0;
    if (this.amountKind === "withdraw") {
      const step = delta < 0 ? -10 : 10;
      let next = base + step;
      next = Math.min(99999990, Math.max(MIN_WITHDRAW_KCOIN, next));
      next = Math.floor(next / 10) * 10;
      this.priceDigits = String(next);
      this.redraw();
      return;
    }
    let next = base + delta;
    if (delta < 0 && next < MIN_PRICE_KCOIN) next = MIN_PRICE_KCOIN;
    next = Math.min(99999999, Math.max(MIN_PRICE_KCOIN, next));
    this.priceDigits = String(next);
    this.redraw();
  }

  /** LCD 键盘 / 物理数字键（与商城赞助 appendRechargeKey 同契约）。 */
  appendPriceKey(key: string): void {
    if (!this.priceMode) return;
    if (key === "删除" || key === "back" || key === "Backspace") {
      this.priceDigits =
        this.priceDigits.length <= 1 ? "" : this.priceDigits.slice(0, -1);
      this.redraw();
      return;
    }
    if (key === "确定") {
      this.commitPrice();
      return;
    }
    if (!/^\d$/.test(key)) return;
    if (this.priceDigits === "0") this.priceDigits = key;
    else if (this.priceDigits.length >= 8) return;
    else this.priceDigits += key;
    this.redraw();
  }

  /** @deprecated 保留给 main 物理键盘；内部转 appendPriceKey。 */
  typePriceKey(ch: string): void {
    if (ch === "back") this.appendPriceKey("删除");
    else this.appendPriceKey(ch);
  }

  confirm(): void {
    if (tryDismissPanelTipOnSoftRight(this.tip)) {
      this.redraw();
      return;
    }
    if (this.priceMode) {
      this.commitPrice();
      return;
    }
    if (this.countMode) {
      this.countMode = false;
      this.enterAmountMode("list", String(MIN_PRICE_KCOIN));
      return;
    }
    if (this.detailListing) {
      this.confirmDetail();
      return;
    }
    if (this.tab === "browse") {
      const row = this.browse[this.rowIndex];
      if (row) this.openDetail(row);
      return;
    }
    if (this.tab === "mine") {
      const row = this.mine[this.rowIndex];
      if (row) this.openDetail(row);
      return;
    }
    if (this.tab === "list") {
      this.beginListFromBag();
      return;
    }
    if (this.tab === "kcoin") {
      if (this.rowIndex === 1) this.onAction?.("upload_payee");
      else if (this.rowIndex === 2) this.beginWithdrawAmount();
      else if (this.rowIndex === 3) this.onAction?.("refresh");
    }
  }

  cancelOrBack(): boolean {
    if (tryDismissPanelTipOnSoftRight(this.tip)) {
      this.redraw();
      return true;
    }
    if (this.priceMode) {
      this.priceMode = false;
      this.amountKind = "list";
      // 上架定价返回：可堆叠则回到数量态，否则回列表。
      const row = this.bag[this.rowIndex];
      if (row && this.canAdjustListCount(row)) {
        this.countMode = true;
        this.clampListCount();
        this.redraw();
        this.notifySoftkeys();
        return true;
      }
      this.countMode = false;
      this.redraw();
      this.notifySoftkeys();
      return true;
    }
    if (this.countMode) {
      this.countMode = false;
      this.redraw();
      this.notifySoftkeys();
      return true;
    }
    if (this.detailListing) {
      if (this.sealItemOpen) {
        this.sealItemOpen = false;
        this.detailPage = 0;
        this.redraw();
        this.notifySoftkeys();
        return true;
      }
      this.clearDetail();
      this.redraw();
      this.notifySoftkeys();
      return true;
    }
    this.close();
    return true;
  }

  setSealDetailLoader(
    loader: ((listingId: number) => Promise<ZangbaoSealDetail | null>) | null,
  ): void {
    this.sealDetailLoader = loader;
  }

  private clearDetail(): void {
    this.detailListing = null;
    this.detailActionCursor = 0;
    this.detailPage = 0;
    this.sealDetail = null;
    this.sealLoading = false;
    this.sealTab = "panel";
    this.sealListCursor = 0;
    this.sealItemOpen = false;
  }

  private openDetail(listing: ZangbaoListing): void {
    this.detailListing = listing;
    this.detailActionCursor = 0;
    this.detailPage = 0;
    this.sealDetail = null;
    this.sealLoading = false;
    this.sealTab = "panel";
    this.sealListCursor = 0;
    this.sealItemOpen = false;
    this.redraw();
    this.notifySoftkeys();
    const needSeal =
      listing.itemId === ROLE_SEAL_SCROLL_ITEM_ID &&
      (!!listing.sealedRole || (listing.itemInstanceId ?? 0) > 0) &&
      !!this.sealDetailLoader;
    if (!needSeal || !this.sealDetailLoader) return;
    const listingId = listing.listingId;
    this.sealLoading = true;
    this.redraw();
    void this.sealDetailLoader(listingId).then((detail) => {
      if (this.detailListing?.listingId !== listingId) return;
      this.sealDetail = detail;
      this.sealLoading = false;
      /* Fill sealedRole summary from detail when browse JOIN missed preview. */
      if (detail?.role?.name && this.detailListing) {
        this.detailListing = {
          ...this.detailListing,
          sealedRole: {
            roleId: detail.sealedRoleId || this.detailListing.sealedRole?.roleId || 0,
            name: detail.role.name,
            level: detail.role.level,
            job: detail.role.job,
            sex: detail.role.sex,
          },
        };
      }
      this.redraw();
      this.notifySoftkeys();
    });
  }

  private detailActions(): { id: DetailActionId; label: string }[] {
    const r = this.detailListing;
    if (!r) return [];
    if (this.tab === "mine") {
      return [
        { id: "cancel", label: "下架" },
        { id: "back", label: "返回" },
      ];
    }
    return [
      { id: "buy", label: `购买 ${r.priceKcoin}酷币` },
      { id: "back", label: "返回" },
    ];
  }

  private confirmDetail(): void {
    const r = this.detailListing;
    if (!r) return;
    if (this.sealItemOpen) {
      this.sealItemOpen = false;
      this.detailPage = 0;
      this.redraw();
      this.notifySoftkeys();
      return;
    }
    if (this.isSealListTab()) {
      const rows = this.sealListRows();
      if (rows.length > 0 && this.sealListCursor < rows.length) {
        this.openSealItemDetail(this.sealListCursor);
        return;
      }
    }
    const acts = this.detailActions();
    const act = acts[this.detailActionCursor];
    if (!act) return;
    if (act.id === "back") {
      this.cancelOrBack();
      return;
    }
    if (act.id === "buy") {
      this.onAction?.("buy", { listingId: r.listingId });
      return;
    }
    if (act.id === "cancel") {
      this.onAction?.("cancel", { listingId: r.listingId });
    }
  }

  private isSealListTab(): boolean {
    return (
      !!this.detailListing &&
      this.detailListing.itemId === ROLE_SEAL_SCROLL_ITEM_ID &&
      (this.sealTab === "equip" ||
        this.sealTab === "bag" ||
        this.sealTab === "wh")
    );
  }

  private sealListRows(): ZangbaoSealItemRow[] {
    const d = this.sealDetail;
    if (!d) return [];
    if (this.sealTab === "equip") return d.equipment;
    if (this.sealTab === "bag") return d.backpack;
    if (this.sealTab === "wh") return d.warehouse;
    return [];
  }

  private ensureSealListPageVisible(rowCount: number): void {
    if (rowCount <= 0) return;
    if (this.sealListCursor >= rowCount) return;
    /* detailPage indexes list pages; fitRows decided at draw time — keep page
     * aligned so selected row stays on-screen when possible (fitRows≈5~7). */
    const approxFit = 5;
    const page = Math.floor(this.sealListCursor / approxFit);
    if (this.detailPage !== page) this.detailPage = page;
  }

  private openSealItemDetail(index: number): void {
    const rows = this.sealListRows();
    if (index < 0 || index >= rows.length) return;
    this.sealListCursor = index;
    this.sealItemOpen = true;
    this.detailPage = 0;
    this.redraw();
    this.notifySoftkeys();
  }

  private sealRowAsBagItem(row: ZangbaoSealItemRow): BagItem {
    const cat = lookupItemSync(row.itemId);
    const kind = cat?.kind === "equip" ? "equip" : "item";
    const enhanceAttrs =
      kind === "equip"
        ? enhanceAttrsFromWashText(row.itemId, row.enhanceWash)
        : undefined;
    const gemReflect = row.gemReflect ?? 0;
    const gemCombo = row.gemCombo ?? 0;
    const gemRevive = row.gemRevive ?? 0;
    const gems =
      kind === "equip" && (gemReflect > 0 || gemCombo > 0 || gemRevive > 0)
        ? { reflect: gemReflect, combo: gemCombo, revive: gemRevive }
        : undefined;
    return {
      itemId: row.itemId,
      seq: row.seq ?? 0,
      count: row.count || 1,
      name: cat?.name || `物品${row.itemId}`,
      quality: cat?.quality ?? 0,
      enhance: row.enhanceLevel,
      kind,
      category: cat?.category ?? 0,
      desc: cat?.desc ?? "",
      usable: false,
      equipSlot: cat?.equipSlot ?? -1,
      ...(enhanceAttrs?.length ? { enhanceAttrs } : {}),
      ...(gems ? { socketGems: gems } : {}),
    };
  }

  private sealRowLabel(row: ZangbaoSealItemRow): string {
    const cat = lookupItemSync(row.itemId);
    const name = cat?.name || `#${row.itemId}`;
    const en = row.enhanceLevel > 0 ? `+${row.enhanceLevel}` : "";
    if (this.sealTab === "equip") {
      const slot = row.slot ?? 0;
      const slotName =
        slot >= 0 && slot < WORN_SLOT_NAMES.length
          ? WORN_SLOT_NAMES[slot]
          : `[${slot}]`;
      return `${slotName} ${name}${en}`.slice(0, 28);
    }
    const count = row.count > 1 ? ` x${row.count}` : "";
    return `${name}${en}${count}`.slice(0, 28);
  }

  private sealItemDetailLines(row: ZangbaoSealItemRow): EquipPaintLine[] {
    const item = this.sealRowAsBagItem(row);
    if (item.kind === "equip") return buildEquipDetailLines(item);
    const lines: EquipPaintLine[] = [];
    lines.push({
      kind: "text",
      text: item.name.slice(0, 28),
      color: qualityColor(item.quality),
    });
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
    return lines;
  }

  private beginWithdrawAmount(): void {
    if (!this.hasPayee) {
      this.showTip("请先上传收款码");
      return;
    }
    if (this.kcoin < MIN_WITHDRAW_KCOIN) {
      this.showTip(`余额不足${MIN_WITHDRAW_KCOIN}`);
      return;
    }
    const maxOk = Math.floor(this.kcoin / 10) * 10;
    this.countMode = false;
    this.enterAmountMode(
      "withdraw",
      String(Math.max(MIN_WITHDRAW_KCOIN, maxOk)),
    );
  }

  /** 装备 / 壶 / 凭证 / 封印卷：整行上架，不可拆数量。 */
  private listQtyFixed(item: BagItem | null): boolean {
    if (!item) return true;
    return (
      item.kind === "equip" ||
      isReservoirFlaskItemId(item.itemId) ||
      isDurableMallPassItemId(item.itemId) ||
      item.itemId === ROLE_SEAL_SCROLL_ITEM_ID
    );
  }

  private canAdjustListCount(item: BagItem | null): boolean {
    if (!item || this.listQtyFixed(item)) return false;
    return bagVisibleStackCount(item) > 1;
  }

  private maxListCount(item: BagItem | null): number {
    if (!item) return 1;
    if (this.listQtyFixed(item)) return Math.max(1, item.count || 1);
    return Math.max(1, bagVisibleStackCount(item));
  }

  private defaultListCount(item: BagItem): number {
    if (this.listQtyFixed(item)) return Math.max(1, item.count || 1);
    return Math.max(1, bagVisibleStackCount(item));
  }

  private clampListCount(): void {
    const row = this.bag[this.rowIndex];
    const max = this.maxListCount(row ?? null);
    this.listCount = Math.max(1, Math.min(this.listCount || 1, max));
  }

  private adjustListCount(dir: -1 | 1): void {
    const row = this.bag[this.rowIndex];
    if (!row || !this.canAdjustListCount(row)) {
      this.showTip("该物品不可拆分数量");
      return;
    }
    const max = this.maxListCount(row);
    this.listCount = ((this.listCount - 1 + dir + max) % max) + 1;
    this.redraw();
  }

  /** 上架页：可堆叠先选数量，其余直接定价。 */
  private beginListFromBag(): void {
    if (!this.bag.length) {
      this.showTip("背包为空");
      return;
    }
    const row = this.bag[this.rowIndex];
    if (!row) {
      this.showTip("请选择物品");
      return;
    }
    this.listCount = this.defaultListCount(row);
    if (this.canAdjustListCount(row)) {
      this.countMode = true;
      this.redraw();
      this.notifySoftkeys();
      return;
    }
    this.countMode = false;
    this.enterAmountMode("list", String(MIN_PRICE_KCOIN));
  }

  private enterAmountMode(kind: AmountKind, digits: string): void {
    this.amountKind = kind;
    this.priceMode = true;
    this.countMode = false;
    this.priceDigits = digits;
    this.redraw();
    this.notifySoftkeys();
  }

  private commitPrice(): void {
    const amount = Math.floor(Number(this.priceDigits.trim() || "0"));
    if (this.amountKind === "withdraw") {
      if (
        !Number.isFinite(amount) ||
        amount < MIN_WITHDRAW_KCOIN ||
        amount % 10 !== 0
      ) {
        this.showTip(`提现须≥${MIN_WITHDRAW_KCOIN}且为10倍数`);
        return;
      }
      if (amount > this.kcoin) {
        this.showTip("余额不足");
        return;
      }
      this.priceMode = false;
      this.amountKind = "list";
      this.notifySoftkeys();
      this.onAction?.("withdraw", { amount });
      return;
    }
    const row = this.bag[this.rowIndex];
    if (!row || !Number.isFinite(amount) || amount < MIN_PRICE_KCOIN) {
      this.showTip(`价格至少${MIN_PRICE_KCOIN}酷币`);
      return;
    }
    this.clampListCount();
    const count = this.listQtyFixed(row)
      ? Math.max(1, row.count || 1)
      : this.listCount;
    this.priceMode = false;
    this.countMode = false;
    this.amountKind = "list";
    this.notifySoftkeys();
    this.onAction?.("list", {
      bagSeq: row.seq,
      priceKcoin: amount,
      count,
    });
  }

  private currentRows(): unknown[] {
    if (this.tab === "browse") return this.browse;
    if (this.tab === "mine") return this.mine;
    if (this.tab === "list") return this.bag;
    // 酷币：余额 / 收款码 / 提现 / 刷新
    return [{}, {}, {}, {}];
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
    const lbl = makePixelText(text, 12, fill);
    lbl.anchor.set(0.5, 0.5);
    lbl.x = Math.round(cx);
    lbl.y = Math.round(cy);
    this.addChild(lbl);
    this.labels.push(lbl);
  }

  private paintEquipLine(line: EquipPaintLine, x: number, y: number): number {
    if (line.kind === "text" || line.kind === "milestone") {
      this.addLabel(line.text, x, y, line.color);
      return wrapChars(line.text).length;
    }
    this.addLabel(`${line.key}${line.value}`, x, y, line.keyColor);
    return wrapChars(`${line.key}${line.value}`).length;
  }

  private sealDetailLines(): EquipPaintLine[] {
    const d = this.sealDetail;
    const r = this.detailListing;
    if (!d) {
      if (this.sealLoading) {
        return [{ kind: "text", text: "加载封印角色…", color: CBE.inkMint }];
      }
      if (r?.sealedRole) {
        const s = r.sealedRole;
        return [
          {
            kind: "text",
            text: `${s.name} Lv${s.level}`,
            color: CBE.ink,
          },
          {
            kind: "text",
            text: "详情加载失败",
            color: CBE.inkWarn,
          },
        ];
      }
      return [{ kind: "text", text: "无封印角色", color: CBE.inkMint }];
    }
    const lines: EquipPaintLine[] = [];
    if (this.sealTab === "panel") {
      const role = d.role;
      const jobName = roleJobDisplayName(role.job);
      const panelRows: { label: string; value: string }[] = [
        { label: "名称", value: role.name || "-" },
        { label: "称号", value: role.title || "无" },
        { label: "等级", value: String(role.level) },
        {
          label: "职业",
          value: `${jobName} ${role.sex ? "女" : "男"}`,
        },
        { label: "配偶", value: role.spouse || "无" },
        { label: "帮派", value: role.guild || "无帮派" },
        { label: "魅力", value: String(role.charm ?? 0) },
        {
          label: "生命",
          value: `${role.hp}/${role.hpMax}`,
        },
        {
          label: "法力",
          value: `${role.mp}/${role.mpMax}`,
        },
        {
          label: "经验",
          value: formatPanelLevelExp(role.exp, role.level),
        },
        { label: "力量", value: String(role.strength ?? 0) },
        { label: "敏捷", value: String(role.agility ?? 0) },
        { label: "智慧", value: String(role.wisdom ?? 0) },
        { label: "物攻", value: String(role.attack ?? 0) },
        { label: "护甲", value: String(role.armor ?? 0) },
        { label: "躲闪", value: String(role.dodge ?? 0) },
        {
          label: "命中",
          value: String(Math.max(0, (role.hit ?? 0) - 101)),
        },
        { label: "暴击", value: String(role.crit ?? 0) },
        { label: "抗性", value: String(role.resist ?? 0) },
        {
          label: "反伤",
          value: `${role.reflectPct ?? 0}%`,
        },
        {
          label: "连击",
          value: `${role.comboPct ?? 0}%`,
        },
        {
          label: "复活",
          value: `${role.revivePct ?? 0}%`,
        },
        {
          label: "活力",
          value: `${role.vitality ?? 0}/${role.vitalityMax ?? 0}`,
        },
        {
          label: "金钱",
          value: `${Math.floor((role.money ?? 0) / 10000)}金`,
        },
        { label: "酷宝", value: String(role.wcoin ?? 0) },
        { label: "酷币", value: String(role.kcoin ?? 0) },
      ];
      if (role.scene) {
        panelRows.push({
          label: "场景",
          value: role.scene.slice(0, 20),
        });
      }
      for (const row of panelRows) {
        lines.push({
          kind: "kv",
          key: `${row.label} `,
          value: row.value,
          keyColor: CBE.inkLabel,
          valueColor: CBE.ink,
        });
      }
      return lines;
    }
    if (this.sealItemOpen) {
      const rows = this.sealListRows();
      const row = rows[this.sealListCursor];
      if (!row) {
        return [{ kind: "text", text: "无物品", color: CBE.inkMint }];
      }
      return this.sealItemDetailLines(row);
    }
    const rows = this.sealListRows();
    if (!rows.length) {
      lines.push({ kind: "text", text: "空", color: CBE.inkMint });
      return lines;
    }
    for (const row of rows) {
      lines.push({
        kind: "text",
        text: this.sealRowLabel(row),
        color: CBE.inkBody,
      });
    }
    return lines;
  }

  /** 浏览/我的详情 — 物品信息 + 购买/下架（对齐物品竞拍）。 */
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
    const tipY = panelSafeFootY(bodyTop + bodyH - 16);
    const acts = this.detailActions();
    const btnH = 18;
    const hideActions = this.sealItemOpen;
    const actionH = hideActions ? 0 : acts.length * (btnH + 2) + 8;
    const actionTop = tipY - Math.max(actionH, 8);
    const isSeal =
      r.itemId === ROLE_SEAL_SCROLL_ITEM_ID &&
      (!!r.sealedRole ||
        !!this.sealDetail ||
        this.sealLoading ||
        (r.itemInstanceId ?? 0) > 0);

    const titleH = 20;
    this.bg.rect(x + 6, bodyTop, w - 12, titleH).fill({ color: CBE.bgPanelDark });
    const en = r.enhanceLevel > 0 ? `+${r.enhanceLevel}` : "";
    const sealName =
      r.sealedRole?.name
        ? `[印]${r.sealedRole.name}`
        : this.sealDetail?.role?.name
          ? `[印]${this.sealDetail.role.name}`
          : "";
    let titleText = `${sealName || item.name}${en}`.slice(0, 22);
    if (this.sealItemOpen) {
      const row = this.sealListRows()[this.sealListCursor];
      if (row) {
        const it = this.sealRowAsBagItem(row);
        titleText = `${it.name}${it.enhance > 0 ? `+${it.enhance}` : ""}`.slice(
          0,
          22,
        );
      }
    }
    this.addLabelCentered(
      titleText,
      LCD_WIDTH / 2,
      bodyTop + titleH / 2,
      this.sealItemOpen
        ? qualityColor(
            this.sealListRows()[this.sealListCursor]
              ? this.sealRowAsBagItem(
                  this.sealListRows()[this.sealListCursor]!,
                ).quality
              : item.quality,
          )
        : qColor,
    );

    const metaY = bodyTop + titleH + 4;
    if (!this.sealItemOpen) {
      this.addLabel(
        `卖家 ${r.sellerName || "#"}`.slice(0, 26),
        x + 10,
        metaY,
        CBE.inkLabel,
      );
      this.addLabel(
        `售价 ${r.priceKcoin}酷币 x${r.itemCount}`,
        x + 10,
        metaY + 14,
        CBE.inkMint,
      );
    } else {
      const tabLabel =
        this.sealTab === "equip"
          ? "装备详情"
          : this.sealTab === "bag"
            ? "背包详情"
            : "仓库详情";
      this.addLabel(tabLabel, x + 10, metaY, CBE.inkLabel);
      this.addLabel("右键返回列表", x + 10, metaY + 14, CBE.inkMint);
    }

    let bodyY = metaY + 30;
    if (isSeal && !this.sealItemOpen) {
      for (let i = 0; i < SEAL_TABS.length; i++) {
        const t = SEAL_TABS[i]!;
        const on = t.id === this.sealTab;
        this.addLabel(
          t.label,
          x + 8 + i * 36,
          bodyY,
          on ? CBE.ink : CBE.inkMint,
        );
        this.rowHits.push(
          attachRowPointerHit(this, x + 4 + i * 36, bodyY, 34, 14, () => {
            if (this.sealTab === t.id) return;
            this.sealTab = t.id;
            this.detailPage = 0;
            this.sealListCursor = 0;
            this.sealItemOpen = false;
            this.detailActionCursor = 0;
            this.redraw();
            this.notifySoftkeys();
          }),
        );
      }
      bodyY += 16;
    }

    const rowH = 14;
    const fitRows = Math.max(1, Math.floor((actionTop - bodyY - 2) / rowH));
    let lines: EquipPaintLine[] = [];
    if (isSeal) {
      lines = this.sealDetailLines();
    } else if (item.kind === "equip") {
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

    const listSelectMode =
      isSeal && this.isSealListTab() && !this.sealItemOpen && !!this.sealDetail;
    const listCount = listSelectMode ? this.sealListRows().length : 0;
    const pageCount = Math.max(1, Math.ceil(lines.length / fitRows));
    let page = Math.min(Math.max(0, this.detailPage), pageCount - 1);
    if (listSelectMode && listCount > 0 && this.sealListCursor < listCount) {
      page = Math.min(
        pageCount - 1,
        Math.floor(this.sealListCursor / fitRows),
      );
    }
    this.detailPage = page;
    const start = page * fitRows;
    const pageLines = lines.slice(start, start + fitRows);
    let ly = bodyY;
    for (let i = 0; i < pageLines.length; i++) {
      const line = pageLines[i]!;
      const absIdx = start + i;
      const selected =
        listSelectMode &&
        absIdx < listCount &&
        absIdx === this.sealListCursor &&
        this.sealListCursor < listCount;
      if (selected) {
        this.bg.rect(x + 6, ly - 1, w - 12, rowH).fill({ color: CBE.select });
      }
      const paintColor =
        selected && line.kind === "text"
          ? { ...line, color: EQUIP_DETAIL_YELLOW }
          : line;
      const rows = this.paintEquipLine(paintColor, x + 10, ly);
      if (listSelectMode && absIdx < listCount) {
        const hitIdx = absIdx;
        this.rowHits.push(
          attachRowPointerHit(this, x + 6, ly - 1, w - 12, rowH, () => {
            if (this.sealListCursor === hitIdx && !this.sealItemOpen) {
              this.openSealItemDetail(hitIdx);
              return;
            }
            this.sealListCursor = hitIdx;
            this.detailActionCursor = 0;
            this.redraw();
            this.notifySoftkeys();
          }),
        );
      }
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

    if (!hideActions) {
      this.bg.rect(x + 4, actionTop, w - 8, actionH).fill({ color: CBE.bgPanel });
      const listLen = listSelectMode ? listCount : 0;
      acts.forEach((act, i) => {
        const hi =
          i === this.detailActionCursor &&
          (!listSelectMode || this.sealListCursor >= listLen);
        const by = actionTop + 4 + i * (btnH + 2);
        if (hi) this.bg.rect(x + 8, by, w - 16, btnH).fill({ color: CBE.select });
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
            if (listSelectMode) this.sealListCursor = listLen;
            this.redraw();
            this.confirm();
          }),
        );
      });
    }
    const tip = this.sealItemOpen
      ? "←→翻页 右键返回"
      : listSelectMode
        ? "上下选 · 确认查看 · ←→翻页"
        : "上下选操作 ←→翻页";
    this.addLabel(tip, x + 10, tipY, CBE.inkMint);
  }

  /**
   * 金额键盘 — 上架定价 / 提现金额（←→ 微调，点按数字/删除/确定）。
   */
  private drawPriceKeypad(x: number, y: number, w: number): void {
    if (this.amountKind === "withdraw") {
      this.addLabel(
        `余额 ${this.kcoin} 酷币`,
        x + 10,
        y,
        CBE.inkMint,
      );
      this.addLabel(
        "←→±10 · ≥50且10倍数",
        x + 10,
        y + 16,
        CBE.inkLabel,
      );
    } else {
      const bag = this.bag[this.rowIndex];
      const itemName = bag
        ? lookupItemSync(bag.itemId)?.name || `#${bag.itemId}`
        : "";
      this.clampListCount();
      const qty = this.listCount;
      if (itemName) {
        this.addLabel(
          `${itemName} x${qty}`,
          x + 10,
          y,
          CBE.inkMint,
        );
      }
      this.addLabel("←→微调 · 点按数字", x + 10, y + 16, CBE.inkLabel);
    }

    const boxY = y + 34;
    const boxH = 28;
    this.bg
      .roundRect(x + 10, boxY, w - 20, boxH, 3)
      .fill({ color: CBE.bgPanel })
      .stroke({ width: 1, color: CBE.borderBright });
    const shown = (this.priceDigits || "").trim() || "0";
    const unit =
      this.amountKind === "withdraw" ? "提现酷币" : "酷币";
    this.addLabelCentered(
      `${shown} ${unit}`,
      LCD_WIDTH / 2,
      boxY + boxH / 2,
      CBE.inkTipGreen,
    );

    const keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "删除", "0", "确定"];
    const keyW = 58;
    const keyH = 26;
    const gap = 6;
    const gridW = keyW * 3 + gap * 2;
    const startX = x + Math.floor((w - gridW) / 2);
    const baseY = boxY + boxH + 10;
    for (let i = 0; i < keys.length; i++) {
      const col = i % 3;
      const row = Math.floor(i / 3);
      const kx = startX + col * (keyW + gap);
      const ky = baseY + row * (keyH + gap);
      const key = keys[i]!;
      const hi = key === "确定";
      const keyInk =
        key === "确定"
          ? CBE.inkTipGreen
          : key === "删除"
            ? CBE.inkWarn
            : CBE.inkMint;
      this.bg
        .roundRect(kx, ky, keyW, keyH, 3)
        .fill({ color: hi ? CBE.bgPanelDark : CBE.bgDeep })
        .stroke({ width: 1, color: CBE.borderBright });
      this.addLabelCentered(key, kx + keyW / 2, ky + keyH / 2, keyInk);
      this.rowHits.push(
        attachRowPointerHit(this, kx, ky, keyW, keyH, () => {
          this.appendPriceKey(key);
        }),
      );
    }
  }

  /** 上架数量选择 — ←→ 调数量，确认进定价。 */
  private drawCountPicker(x: number, y: number, w: number): void {
    const bag = this.bag[this.rowIndex];
    if (!bag) {
      this.addLabel("请选择物品", x + 10, y, CBE.inkWarn);
      return;
    }
    this.clampListCount();
    const max = this.maxListCount(bag);
    const name =
      bag.itemId === ROLE_SEAL_SCROLL_ITEM_ID && bag.sealedRole?.name
        ? `[印]${bag.sealedRole.name}`
        : lookupItemSync(bag.itemId)?.name || `#${bag.itemId}`;
    this.addLabel(name.slice(0, 28), x + 10, y, CBE.inkMint);
    this.addLabel(`背包 ${bag.count}`, x + 10, y + 16, CBE.inkLabel);

    const boxY = y + 40;
    const boxH = 36;
    this.bg
      .roundRect(x + 10, boxY, w - 20, boxH, 3)
      .fill({ color: CBE.bgPanel })
      .stroke({ width: 1, color: CBE.borderBright });
    this.addLabelCentered(
      `⇦ 数量 ${this.listCount}/${max} ⇨`,
      LCD_WIDTH / 2,
      boxY + boxH / 2,
      CBE.inkTipGreen,
    );

    const btnY = boxY + boxH + 14;
    const btnW = 64;
    const btnH = 26;
    const gap = 16;
    const leftX = x + Math.floor((w - (btnW * 2 + gap)) / 2);
    const rightX = leftX + btnW + gap;
    for (const side of [
      { label: "−", x: leftX, dir: -1 as const },
      { label: "+", x: rightX, dir: 1 as const },
    ]) {
      this.bg
        .roundRect(side.x, btnY, btnW, btnH, 3)
        .fill({ color: CBE.bgDeep })
        .stroke({ width: 1, color: CBE.borderBright });
      this.addLabelCentered(
        side.label,
        side.x + btnW / 2,
        btnY + btnH / 2,
        CBE.inkMint,
      );
      this.rowHits.push(
        attachRowPointerHit(this, side.x, btnY, btnW, btnH, () => {
          this.adjustListCount(side.dir);
        }),
      );
    }
    this.addLabel("左右调数量 · 确认定价", x + 10, btnY + btnH + 12, CBE.inkLabel);
  }

  /** webglcontextrestored: remount Graphics then redraw. */
  nudgeAfterGpuResume(): void {
    this.bg = replaceGraphics(this.bg);
    if (this.tip?.gfx?.parent) {
      this.tip.gfx = replaceGraphics(this.tip.gfx);
    }
    if (this.visible) this.redraw();
  }

  private redraw(): void {
    this.clearLabels();
    this.bg.clear();
    this.bg.removeChildren();
    const { x: PANEL_X, y: PANEL_Y, w: pw, h: ph } = cbePopupBounds();
    const chrome = paintCbeTitledChrome(
      this,
      this.bg,
      PANEL_X,
      PANEL_Y,
      pw,
      ph,
      CBE.bgPanel,
    );
    const title = makePixelText("藏宝阁", 12, CBE.cream);
    title.anchor.set(0.5, 0.5);
    title.x = LCD_WIDTH / 2;
    title.y = chrome.titleCy;
    this.addChild(title);
    this.labels.push(title);
    let y = chrome.bodyY + 6;
    if (this.priceMode) {
      this.drawPriceKeypad(PANEL_X, y, pw);
      drawPanelTipOverlay(this, this.tip);
      return;
    }
    if (this.countMode) {
      this.drawCountPicker(PANEL_X, y, pw);
      drawPanelTipOverlay(this, this.tip);
      return;
    }
    if (this.detailListing) {
      this.drawDetail(PANEL_X, y, pw, ph - (y - PANEL_Y));
      drawPanelTipOverlay(this, this.tip);
      return;
    }
    for (let i = 0; i < TABS.length; i++) {
      const t = TABS[i];
      const on = t.id === this.tab;
      this.addLabel(t.label, PANEL_X + 6 + i * 40, y, on ? CBE.ink : CBE.inkMint);
      this.rowHits.push(
        attachRowPointerHit(this, PANEL_X + 4 + i * 40, y, 36, 14, () => {
          if (this.detailListing || this.countMode || this.priceMode) return;
          const tabChanged = this.tab !== t.id;
          this.tab = t.id;
          this.rowIndex = 0;
          this.page = 0;
          this.priceMode = false;
          this.countMode = false;
          this.amountKind = "list";
          this.redraw();
          if (tabChanged) this.notifySoftkeys();
        }),
      );
    }
    y += 16;
    if (this.tab === "kcoin") {
      const lines = [
        `余额 ${this.kcoin} 酷币`,
        this.hasPayee ? "收款码:已设 (点上传可更换)" : "上传收款码",
        "申请提现(≥50且10倍数)",
        "刷新",
      ];
      for (let i = 0; i < lines.length; i++) {
        const on = i === this.rowIndex;
        this.addLabel(
          `${on ? ">" : " "}${lines[i]}`,
          PANEL_X + 4,
          y + i * ROW_H,
          on ? CBE.ink : CBE.inkBody,
        );
        this.rowHits.push(
          attachRowPointerHit(
            this,
            PANEL_X,
            y + i * ROW_H,
            LCD_WIDTH - 8,
            ROW_H,
            () => {
              const prev = this.rowIndex;
              this.rowIndex = i;
              this.redraw();
              if (prev !== i) this.notifySoftkeys();
              // 点按「申请提现」直接进入金额键盘（避免无反馈）。
              if (i === 2) this.beginWithdrawAmount();
              else if (i === 1) this.onAction?.("upload_payee");
              else if (i === 3) this.onAction?.("refresh");
            },
          ),
        );
      }
      y += lines.length * ROW_H + 4;
      for (let i = 0; i < Math.min(4, this.withdrawRows.length); i++) {
        const w = this.withdrawRows[i];
        const st =
          w.status === 0 ? "待审" : w.status === 1 ? "已同意" : "已拒绝";
        this.addLabel(
          `#${w.withdrawId} ${w.amount} ${st}`,
          PANEL_X + 4,
          y + i * 13,
          CBE.inkMint,
        );
      }
      drawPanelTipOverlay(this, this.tip);
      return;
    }
    const rows =
      this.tab === "browse"
        ? this.browse
        : this.tab === "mine"
          ? this.mine
          : this.bag;
    const start = this.page * PAGE;
    const slice = rows.slice(start, start + PAGE);
    if (!slice.length) {
      this.addLabel("暂无内容", PANEL_X + 6, y, CBE.inkMint);
    }
    for (let i = 0; i < slice.length; i++) {
      const idx = start + i;
      const on = idx === this.rowIndex;
      let text = "";
      if (this.tab === "list") {
        const b = slice[i] as BagItem;
        const name =
          b.itemId === ROLE_SEAL_SCROLL_ITEM_ID && b.sealedRole?.name
            ? `[印]${b.sealedRole.name}`
            : lookupItemSync(b.itemId)?.name || `#${b.itemId}`;
        text = `${name} x${b.count} seq${b.seq}`;
      } else {
        const L = slice[i] as ZangbaoListing;
        const name =
          L.itemId === ROLE_SEAL_SCROLL_ITEM_ID && L.sealedRole?.name
            ? `[印]${L.sealedRole.name}`
            : lookupItemSync(L.itemId)?.name || `#${L.itemId}`;
        const en = L.enhanceLevel > 0 ? `+${L.enhanceLevel}` : "";
        const wash = equipWashNameTag(listingAsBagItem(L));
        text = `${name}${wash}${en}x${L.itemCount} ${L.priceKcoin}酷币`;
      }
      this.addLabel(
        `${on ? ">" : " "}${text}`,
        PANEL_X + 2,
        y + i * ROW_H,
        on ? CBE.ink : CBE.inkBody,
      );
      this.rowHits.push(
        attachRowPointerHit(
          this,
          PANEL_X,
          y + i * ROW_H,
          LCD_WIDTH - 8,
          ROW_H,
          () => {
            this.rowIndex = idx;
            if (this.tab === "browse" || this.tab === "mine") {
              const L = (
                this.tab === "browse" ? this.browse : this.mine
              )[idx];
              if (L) this.openDetail(L);
              else {
                this.redraw();
                this.notifySoftkeys();
              }
              return;
            }
            this.redraw();
            this.notifySoftkeys();
          },
        ),
      );
    }
    drawPanelTipOverlay(this, this.tip);
    void LCD_HEIGHT;
    void SOFTKEY_H;
    void ph;
  }
}
