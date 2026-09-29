/**
 * Player trade stall — two-step flow:
 * ① configure (bag + gold); picking a bag row opens detail confirm before put.
 *    Left softkey 锁定 = submit offer (21/5).
 * ② 确认交易 unlocks only after both sides locked/submitted.
 * After both lock, peer offer rows show 查看 after equip names.
 */
import { Container, Graphics } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  CBE_POPUP_W,
  CBE_POPUP_X,
  CBE_POPUP_Y,
  LCD_HEIGHT,
  LCD_WIDTH,
  WORLD_BOTTOM_H,
  paintCbeTitledChrome,
  blockPanelPointerPassthrough,
} from "./constants";
import { BitmapLabel, wrapChars, measureUiTextWidth } from "./BitmapLabel";
import { ChatIme } from "./ChatIme";
import type { BagItem } from "../net/protocolExtras";
import { formatCultivationBookName, isCultivationBookItem } from "../net/protocolExtras";
import type { TradeOfferItem, TradeOfferView, TradeSessionUpdate } from "../net/protocolSocial";
import { COPPER_PER_GOLD } from "../net/protocolAuction";
import { isTradeableItem, lookupItemSync, qualityColor } from "../resources/itemCatalog";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { attachPageNavButtons, panelSafeFootY } from "./pageNav";
import {
  buildEquipDetailLines,
  equipHasWashedAttr,
  type EquipPaintLine,
} from "./equipDetail";

export type TradeAction =
  | "pick_item"
  | "set_money"
  | "submit"
  | "confirm"
  | "cancel"
  | "add_bag_item"
  | "remove_mine";

/** 1 = configure offer · 2 = wait / confirm. */
type FlowStep = 1 | 2;
type Mode = "list" | "money" | "peer_detail" | "pick_detail";

/** Per-account trade stall snapshot for multi-open switch. */
export type TradePanelSnap = {
  peerName: string;
  bag: BagItem[];
  myMoney: number;
  myItems: TradeOfferItem[];
  peerOffer: TradeOfferView;
  waitingPeer: boolean;
  offerSubmitted: boolean;
  peerSubmitted: boolean;
  flowStep: FlowStep;
  mode: Mode;
  listCursor: number;
  bagScroll: number;
  pickQty: number;
  moneyDraft: string;
  moneyMax: number;
};

const BAG_PAGE_SIZE = 6;
const PICK_ACTIONS = ["确认放入", "取消"] as const;

/**
 * Trade chrome stops above the world soft-row so multi-open chips stay
 * visible/tappable (hud.zIndex is below tradeUi).
 */
function tradePanelBounds(): { x: number; y: number; w: number; h: number } {
  const y = CBE_POPUP_Y;
  const foot = LCD_HEIGHT - WORLD_BOTTOM_H;
  return {
    x: CBE_POPUP_X,
    y,
    w: CBE_POPUP_W,
    h: Math.max(8, foot - y - 2),
  };
}

type ConfigRow = { kind: "money" } | { kind: "bag"; index: number };

export class TradePanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private flowStep: FlowStep = 1;
  private mode: Mode = "list";
  private listCursor = 0;
  private bagScroll = 0;
  /** Qty to put when confirming a stackable bag row (←→). */
  private pickQty = 1;
  private moneyDraft = "";
  private moneyMax = 0;
  private peerName = "";
  private bag: BagItem[] = [];
  private myMoney = 0;
  private myItems: TradeOfferItem[] = [];
  private peerOffer: TradeOfferView = { money: 0, items: [] };
  private waitingPeer = false;
  private offerSubmitted = false;
  private peerSubmitted = false;
  /** Peer equip selected for attribute detail (after both locked). */
  private peerDetailItem: TradeOfferItem | null = null;
  private peerDetailPage = 0;
  /** Own bag item pending put — detail confirm before addFromBag. */
  private pickDetailItem: BagItem | null = null;
  private pickDetailQty = 1;
  private pickDetailPage = 0;
  private pickActionCursor = 0;
  private ime = new ChatIme({
    maxLength: 10,
    inputMode: "numeric",
    enterkeyhint: "done",
    ariaLabel: "交易金币",
    sanitize: (raw) => raw.replace(/\D/g, "").replace(/^0+(?=\d)/, ""),
  });
  private inputHit = new Graphics();
  private openGen = 0;
  private focusTimer: ReturnType<typeof setTimeout> | null = null;

  onAction: ((action: TradeAction, payload?: TradeOfferItem | BagItem) => void) | null =
    null;
  onClose: (() => void) | null = null;
  onSoftkeys: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    // Leave soft-row + chat free so WorldHud multi-open chips stay tappable.
    blockPanelPointerPassthrough(this, { reserveBottom: WORLD_BOTTOM_H });
    this.addChild(
      new Graphics()
        .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - WORLD_BOTTOM_H)
        .fill({ color: 0x000000, alpha: 0.4 }),
    );
    this.addChild(this.box);
    this.inputHit.eventMode = "static";
    this.inputHit.cursor = "text";
    this.inputHit.on("pointertap", () => this.focusMoneyInput());
    this.addChild(this.inputHit);
    this.ime.onChange = (v) => {
      const clamped = this.clampMoneyDraft(v);
      this.moneyDraft = clamped;
      if (clamped !== v) this.ime.setValue(clamped);
      if (this.visible && this.mode === "money") this.redraw();
    };
    this.ime.onSubmit = () => this.commitMoney();
  }

  /** webglcontextrestored: remount panel Graphics then redraw. */
  nudgeAfterGpuResume(): void {
    this.box = replaceGraphics(this.box);
    this.inputHit = replaceGraphics(this.inputHit);
    this.inputHit.eventMode = "static";
    this.inputHit.cursor = "text";
    this.inputHit.removeAllListeners();
    this.inputHit.on("pointertap", () => this.focusMoneyInput());
    if (this.visible) this.redraw();
  }

  softkeys(): { left: string; right: string } {
    // Right softkey 返回 = cancel trade (except leaving money IME / detail).
    if (this.mode === "money") return { left: "确定", right: "返回" };
    if (this.mode === "peer_detail") {
      return { left: "", right: "返回" };
    }
    if (this.mode === "pick_detail") {
      return { left: "确认", right: "返回" };
    }
    if (this.flowStep === 1) {
      // Before gold/items: left softkey disabled; after: 锁定 = submit.
      if (this.hasOffer()) return { left: "锁定", right: "返回" };
      return { left: "", right: "返回" };
    }
    if (this.bothSubmitted() && !this.waitingPeer) {
      return { left: "确认", right: "返回" };
    }
    return { left: "", right: "返回" };
  }

  /** Left softkey — 锁定 submits offer; step2 确认 confirms trade. */
  softLeft(): void {
    if (!this.visible) return;
    if (this.mode === "peer_detail") return;
    if (this.mode === "pick_detail") {
      this.commitPickDetail();
      return;
    }
    if (this.mode === "money") {
      this.commitMoney();
      return;
    }
    if (this.flowStep === 1) {
      if (!this.hasOffer()) return;
      this.lockOffer();
      return;
    }
    if (this.bothSubmitted() && !this.waitingPeer) {
      this.onAction?.("confirm");
    }
  }

  open(peerName: string, bag: BagItem[]): void {
    this.releaseMoneyIme();
    this.peerName = peerName || "对方";
    this.bag = filterTradeBag(bag);
    this.myMoney = 0;
    this.myItems = [];
    this.peerOffer = { money: 0, items: [] };
    this.waitingPeer = false;
    this.offerSubmitted = false;
    this.peerSubmitted = false;
    this.peerDetailItem = null;
    this.peerDetailPage = 0;
    this.clearPickDetailState();
    this.flowStep = 1;
    this.mode = "list";
    this.listCursor = 0;
    this.bagScroll = 0;
    this.pickQty = 1;
    this.moneyDraft = "";
    this.visible = true;
    this.notifySoftkeys();
    this.redraw();
  }

  /** Capture stall state before multi-open switch (IME dropped). */
  snapshot(): TradePanelSnap {
    this.releaseMoneyIme();
    const mode =
      this.mode === "money" ||
      this.mode === "peer_detail" ||
      this.mode === "pick_detail"
        ? "list"
        : this.mode;
    return {
      peerName: this.peerName,
      bag: this.bag.map((b) => ({ ...b })),
      myMoney: this.myMoney,
      myItems: this.myItems.map((it) => ({
        ...it,
        ...(it.enhanceAttrs?.length
          ? { enhanceAttrs: it.enhanceAttrs.map((a) => ({ ...a })) }
          : {}),
      })),
      peerOffer: {
        money: this.peerOffer.money,
        items: this.peerOffer.items.map((it) => ({
          ...it,
          ...(it.enhanceAttrs?.length
            ? { enhanceAttrs: it.enhanceAttrs.map((a) => ({ ...a })) }
            : {}),
        })),
      },
      waitingPeer: this.waitingPeer,
      offerSubmitted: this.offerSubmitted,
      peerSubmitted: this.peerSubmitted,
      flowStep: this.flowStep,
      mode,
      listCursor: this.listCursor,
      bagScroll: this.bagScroll,
      pickQty: this.pickQty,
      moneyDraft: this.moneyDraft,
      moneyMax: this.moneyMax,
    };
  }

  /** Restore a prior multi-open snapshot and show the stall. */
  restore(snap: TradePanelSnap): void {
    this.releaseMoneyIme();
    this.peerName = snap.peerName || "对方";
    this.bag = filterTradeBag(snap.bag);
    this.myMoney = snap.myMoney >>> 0;
    this.myItems = snap.myItems.map((it) => ({ ...it }));
    this.peerOffer = {
      money: snap.peerOffer.money >>> 0,
      items: snap.peerOffer.items.map((it) => ({ ...it })),
    };
    this.waitingPeer = !!snap.waitingPeer;
    this.offerSubmitted = !!snap.offerSubmitted;
    this.peerSubmitted = !!snap.peerSubmitted;
    this.peerDetailItem = null;
    this.peerDetailPage = 0;
    this.clearPickDetailState();
    this.flowStep = snap.flowStep === 2 ? 2 : 1;
    this.mode =
      snap.mode === "money" ||
      snap.mode === "peer_detail" ||
      snap.mode === "pick_detail"
        ? "list"
        : snap.mode;
    this.listCursor = Math.max(0, snap.listCursor | 0);
    this.bagScroll = Math.max(0, snap.bagScroll | 0);
    this.pickQty = Math.max(1, snap.pickQty | 0);
    this.moneyDraft = snap.moneyDraft || "";
    this.moneyMax = Math.max(0, snap.moneyMax | 0);
    this.visible = true;
    this.clampConfigCursor();
    this.ensureBagVisible();
    this.notifySoftkeys();
    this.redraw();
  }

  setBag(bag: BagItem[]): void {
    this.bag = filterTradeBag(bag);
    if (this.mode === "pick_detail" && this.pickDetailItem) {
      const keep = this.bag.find((b) => b.seq === this.pickDetailItem!.seq);
      if (!keep) {
        this.clearPickDetailState();
        this.mode = "list";
      } else {
        this.pickDetailItem = keep;
        this.pickDetailQty = Math.max(
          1,
          Math.min(
            this.pickDetailQty,
            keep.kind === "equip" ? 1 : keep.count,
          ),
        );
      }
    }
    this.clampConfigCursor();
    this.ensureBagVisible();
    if (this.visible) this.redraw();
  }

  setPeerOffer(offer: TradeOfferView): void {
    this.peerOffer = offer;
    this.peerSubmitted = true;
    // Peer 21/6 clears confirmations server-side; keep local offerSubmitted.
    this.waitingPeer = false;
    if (this.offerSubmitted) this.flowStep = 2;
    if (this.mode === "peer_detail" && this.peerDetailItem) {
      const keep = offer.items.find(
        (it) =>
          it.seq === this.peerDetailItem!.seq &&
          it.itemId === this.peerDetailItem!.itemId,
      );
      this.peerDetailItem = keep ?? null;
      if (!keep) this.mode = "list";
    } else if (this.mode === "pick_detail" && this.pickDetailItem) {
      // Keep local pick confirm while peer offer arrives.
    } else {
      this.mode = "list";
      this.listCursor = 0;
    }
    this.notifySoftkeys();
    if (this.visible) this.redraw();
  }

  setStatus(_text: string, waiting = false, submitted = this.offerSubmitted): void {
    this.waitingPeer = waiting;
    this.offerSubmitted = submitted;
    if (submitted) {
      this.flowStep = 2;
      this.mode = "list";
      this.listCursor = 0;
    } else if (!waiting && this.flowStep === 2) {
      // 锁定/21/5 failed — back to configure.
      this.flowStep = 1;
      this.mode = "list";
      this.listCursor = 0;
    }
    this.notifySoftkeys();
    if (this.visible) this.redraw();
  }

  get localOffer(): { money: number; items: { seq: number; count: number }[] } {
    return {
      money: this.myMoney >>> 0,
      items: this.myItems.map((it) => ({
        seq: it.seq,
        count: it.count,
      })),
    };
  }

  setMoney(money: number): void {
    this.myMoney = Math.max(0, money >>> 0);
    this.invalidateOffer();
    this.notifySoftkeys();
    this.redraw();
  }

  /** ownedMax / wire money are copper; UI draft is gold (1金=10000铜). */
  beginSetMoney(ownedMax: number): void {
    this.moneyMax = Math.max(0, ownedMax >>> 0);
    const gold = copperToGold(this.myMoney);
    this.moneyDraft = gold > 0 ? String(gold) : "";
    this.flowStep = 1;
    this.mode = "money";
    this.notifySoftkeys();
    this.redraw();
    this.focusMoneyInput();
  }

  typeMoneyKey(key: string): boolean {
    if (!this.visible || this.mode !== "money") return false;
    if (this.ime.isFocused()) return false;
    if (key >= "0" && key <= "9") {
      this.moneyDraft = this.clampMoneyDraft(this.moneyDraft + key);
      this.ime.setValue(this.moneyDraft);
      this.redraw();
      return true;
    }
    if (key === "backspace") {
      this.moneyDraft = this.moneyDraft.slice(0, -1);
      this.ime.setValue(this.moneyDraft);
      this.redraw();
      return true;
    }
    return false;
  }

  addFromBag(item: BagItem, count?: number): void {
    if (
      !item.seq ||
      !isTradeableItem(item.itemId, { enhance: item.enhance, kind: item.kind })
    ) {
      return;
    }
    const qty = Math.max(
      1,
      Math.min(count ?? 1, item.kind === "equip" ? 1 : item.count),
    );
    const existing = this.myItems.find((x) => x.seq === item.seq);
    if (existing) {
      if (item.kind === "equip") return;
      existing.count = Math.min(item.count, existing.count + qty);
    } else {
      const baseName = isCultivationBookItem(item.itemId)
        ? formatCultivationBookName(
            lookupItemSync(item.itemId)?.name ?? item.name,
            item.bookLevel,
            item.bookFull,
          )
        : item.name;
      this.myItems.push({
        seq: item.seq,
        itemId: item.itemId,
        count: qty,
        name: baseName,
        enhance: item.enhance,
        quality: item.quality,
        kind: item.kind,
        desc: item.desc,
        equipSlot: item.equipSlot,
        ...(item.enhanceAttrs?.length
          ? { enhanceAttrs: item.enhanceAttrs }
          : {}),
        ...(item.bookLevel && item.bookLevel > 0
          ? { bookLevel: item.bookLevel, bookFull: item.bookFull }
          : {}),
      });
    }
    this.invalidateOffer();
    this.mode = "list";
    this.notifySoftkeys();
    this.redraw();
  }

  removeMineAt(index: number): void {
    if (index < 0 || index >= this.myItems.length) return;
    this.myItems.splice(index, 1);
    this.invalidateOffer();
    this.notifySoftkeys();
    this.redraw();
  }

  hide(): void {
    this.releaseMoneyIme();
    this.peerDetailItem = null;
    this.peerDetailPage = 0;
    this.clearPickDetailState();
    this.mode = "list";
    this.visible = false;
  }

  /** Kept for callers; instructional tips removed. */
  showTip(_text: string, _ms = 1200): void {}

  releaseIme(): void {
    this.releaseMoneyIme();
    if (this.mode === "money") this.mode = "list";
  }

  destroy(options?: boolean | { children?: boolean; texture?: boolean }): void {
    this.releaseMoneyIme();
    this.ime.destroy();
    super.destroy(options);
  }

  move(delta: number): void {
    if (!this.visible || this.mode === "money") return;
    if (this.mode === "peer_detail") return;
    if (this.mode === "pick_detail") {
      const n = PICK_ACTIONS.length;
      this.pickActionCursor = (this.pickActionCursor + delta + n) % n;
      this.redraw();
      return;
    }
    if (this.flowStep === 1) {
      const n = Math.max(1, this.configRowCount());
      this.listCursor = (this.listCursor + delta + n) % n;
      this.ensureBagVisible();
      this.resetPickQtyForCursor();
    } else {
      const actions = this.step2Actions();
      const n = actions.length;
      if (!n) return;
      let next = this.listCursor;
      for (let i = 0; i < n; i++) {
        next = (next + delta + n) % n;
        if (this.actionEnabled(actions[next]!)) break;
      }
      this.listCursor = next;
    }
    this.redraw();
  }

  confirm(): void {
    if (!this.visible) return;
    if (this.mode === "money") {
      this.commitMoney();
      return;
    }
    if (this.mode === "peer_detail") {
      this.closePeerInspect();
      return;
    }
    if (this.mode === "pick_detail") {
      if (this.pickActionCursor === 0) this.commitPickDetail();
      else this.closePickDetail();
      return;
    }
    if (this.flowStep === 1) {
      this.confirmConfigRow();
      return;
    }
    this.ensureStep2Cursor();
    const label = this.step2Actions()[this.listCursor];
    if (!label || !this.actionEnabled(label)) return;
    if (label === "确认交易") {
      this.onAction?.("confirm");
    }
  }

  /** Right softkey 返回 — leave money/detail, else cancel trade. */
  cancel(): void {
    if (!this.visible) return;
    if (this.mode === "money") {
      this.cancelMoney();
      return;
    }
    if (this.mode === "peer_detail") {
      this.closePeerInspect();
      return;
    }
    if (this.mode === "pick_detail") {
      this.closePickDetail();
      return;
    }
    this.onAction?.("cancel");
  }

  moveHorizontal(delta: number): void {
    if (!this.visible) return;
    if (this.mode === "money") {
      this.focusMoneyInput();
      return;
    }
    if (this.mode === "peer_detail") {
      this.peerDetailPage = Math.max(0, this.peerDetailPage + delta);
      this.redraw();
      return;
    }
    if (this.mode === "pick_detail") {
      const it = this.pickDetailItem;
      if (it && isTradeBagEquip(it)) {
        this.pickDetailPage = Math.max(0, this.pickDetailPage + delta);
        this.redraw();
        return;
      }
      if (it && it.kind !== "equip" && it.count > 1) {
        const max = it.count;
        this.pickDetailQty = Math.max(
          1,
          Math.min(max, this.pickDetailQty + delta),
        );
        this.redraw();
      }
      return;
    }
    if (this.flowStep === 1 && this.mode === "list") {
      this.adjustPickQty(delta);
    }
  }

  isMoneyMode(): boolean {
    return this.visible && this.mode === "money";
  }

  focusMoneyInput(): void {
    if (!this.visible || this.mode !== "money") return;
    this.ime.show(this.moneyInputRect());
    this.clearFocusTimer();
    const gen = ++this.openGen;
    this.focusTimer = setTimeout(() => {
      this.focusTimer = null;
      if (!this.visible || this.openGen !== gen || this.mode !== "money") return;
      this.ime.focus(this.moneyDraft);
    }, 50);
  }

  private confirmConfigRow(): void {
    const row = this.configRowAt(this.listCursor);
    if (!row) return;
    if (row.kind === "money") {
      this.onAction?.("set_money");
      return;
    }
    if (row.kind === "bag") {
      const it = this.bag[row.index];
      if (it) this.openPickDetail(it, this.pickQtyForItem(it));
    }
  }

  private pickQtyForItem(it: BagItem): number {
    if (it.kind === "equip" || it.count <= 1) return 1;
    return Math.max(1, Math.min(this.pickQty, it.count));
  }

  private resetPickQtyForCursor(): void {
    this.pickQty = 1;
  }

  private adjustPickQty(delta: number): void {
    const row = this.configRowAt(this.listCursor);
    if (!row || row.kind !== "bag") return;
    const it = this.bag[row.index];
    if (!it || it.kind === "equip" || it.count <= 1) return;
    const max = it.count;
    this.pickQty = Math.max(1, Math.min(max, this.pickQty + delta));
    this.redraw();
  }

  /** 锁定 = send 21/5 offer, then show confirm step. */
  private lockOffer(): void {
    if (!this.hasOffer()) return;
    this.offerSubmitted = true;
    this.flowStep = 2;
    this.mode = "list";
    this.listCursor = 0;
    this.notifySoftkeys();
    this.redraw();
    this.onAction?.("submit");
  }

  private invalidateOffer(): void {
    this.offerSubmitted = false;
    this.waitingPeer = false;
    this.flowStep = 1;
  }

  private hasOffer(): boolean {
    return this.myItems.length > 0 || this.myMoney > 0;
  }

  private bothSubmitted(): boolean {
    return this.offerSubmitted && this.peerSubmitted;
  }

  private actionEnabled(label: string): boolean {
    if (label === "确认交易") {
      return this.bothSubmitted() && !this.waitingPeer;
    }
    return true;
  }

  private step2Actions(): string[] {
    return ["确认交易"];
  }

  private openPeerDetail(it: TradeOfferItem): void {
    this.peerDetailItem = it;
    this.peerDetailPage = 0;
    this.mode = "peer_detail";
    this.notifySoftkeys();
    this.redraw();
  }

  private closePeerInspect(): void {
    this.peerDetailItem = null;
    this.peerDetailPage = 0;
    this.mode = "list";
    this.listCursor = 0;
    this.notifySoftkeys();
    this.redraw();
  }

  private openPickDetail(it: BagItem, qty: number): void {
    if (
      !it.seq ||
      !isTradeableItem(it.itemId, { enhance: it.enhance, kind: it.kind })
    ) {
      return;
    }
    this.pickDetailItem = it;
    this.pickDetailQty = Math.max(
      1,
      Math.min(qty, it.kind === "equip" ? 1 : it.count),
    );
    this.pickDetailPage = 0;
    this.pickActionCursor = 0;
    this.mode = "pick_detail";
    this.notifySoftkeys();
    this.redraw();
  }

  private closePickDetail(): void {
    this.clearPickDetailState();
    this.mode = "list";
    this.notifySoftkeys();
    this.redraw();
  }

  private commitPickDetail(): void {
    const it = this.pickDetailItem;
    if (!it) {
      this.closePickDetail();
      return;
    }
    const qty = this.pickDetailQty;
    this.clearPickDetailState();
    this.addFromBag(it, qty);
  }

  private clearPickDetailState(): void {
    this.pickDetailItem = null;
    this.pickDetailQty = 1;
    this.pickDetailPage = 0;
    this.pickActionCursor = 0;
  }

  /** Absolute rows: money · each bag item. */
  private configRowCount(): number {
    return 1 + this.bag.length;
  }

  private configRowAt(abs: number): ConfigRow | null {
    if (abs < 0) return null;
    if (abs === 0) return { kind: "money" };
    if (abs <= this.bag.length) return { kind: "bag", index: abs - 1 };
    return null;
  }

  /** Visible list rows for the current bag page (absolute cursor indices). */
  private visibleConfigAbs(): number[] {
    const out: number[] = [0];
    if (this.bag.length) {
      const end = Math.min(this.bag.length, this.bagScroll + BAG_PAGE_SIZE);
      for (let i = this.bagScroll; i < end; i++) out.push(i + 1);
    }
    return out;
  }

  private clampConfigCursor(): void {
    const n = Math.max(1, this.configRowCount());
    if (this.listCursor >= n) this.listCursor = n - 1;
  }

  private ensureStep2Cursor(): void {
    const actions = this.step2Actions();
    const cur = actions[this.listCursor];
    if (cur && this.actionEnabled(cur)) return;
    for (let i = 0; i < actions.length; i++) {
      if (this.actionEnabled(actions[i]!)) {
        this.listCursor = i;
        return;
      }
    }
    this.listCursor = 0;
  }

  private ensureBagVisible(): void {
    if (this.flowStep !== 1 || this.mode !== "list") return;
    const row = this.configRowAt(this.listCursor);
    if (!row || row.kind !== "bag") return;
    if (row.index < this.bagScroll) this.bagScroll = row.index;
    if (row.index >= this.bagScroll + BAG_PAGE_SIZE) {
      this.bagScroll = row.index - BAG_PAGE_SIZE + 1;
    }
    const maxScroll = Math.max(0, this.bag.length - BAG_PAGE_SIZE);
    this.bagScroll = Math.max(0, Math.min(this.bagScroll, maxScroll));
  }

  private notifySoftkeys(): void {
    this.onSoftkeys?.();
  }

  private commitMoney(): void {
    if (!this.visible || this.mode !== "money") return;
    const raw = this.moneyDraft || this.ime.value;
    const maxGold = copperToGold(this.moneyMax);
    const gold = Math.max(0, Math.min(maxGold, Number(raw) || 0));
    this.releaseMoneyIme();
    this.setMoney(gold * COPPER_PER_GOLD);
    this.mode = "list";
    this.notifySoftkeys();
    this.redraw();
  }

  private cancelMoney(): void {
    this.releaseMoneyIme();
    this.moneyDraft = "";
    this.mode = "list";
    this.notifySoftkeys();
    this.redraw();
  }

  private releaseMoneyIme(): void {
    this.openGen += 1;
    this.clearFocusTimer();
    this.ime.hide();
    this.inputHit.clear();
  }

  private clearFocusTimer(): void {
    if (this.focusTimer) {
      clearTimeout(this.focusTimer);
      this.focusTimer = null;
    }
  }

  private clampMoneyDraft(raw: string): string {
    let v = raw.replace(/\D/g, "").replace(/^0+(?=\d)/, "");
    if (v.length > 10) v = v.slice(0, 10);
    const n = Number(v) || 0;
    const maxGold = copperToGold(this.moneyMax);
    if (n > maxGold) return String(maxGold);
    return v;
  }

  private moneyInputRect(): { x: number; y: number; w: number; h: number } {
    const { x, y, w, h } = tradePanelBounds();
    const barY = y + h - 52;
    return { x: x + 14, y: barY + 4, w: w - 28, h: 22 };
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    this.inputHit.clear();
    const { x, y, w, h } = tradePanelBounds();
    const title = `交易·${this.peerName}`;
    const chrome = paintCbeTitledChrome(this, this.box, x, y, w, h, CBE.bgPanel);
    this.addTitleCentered(title, chrome.titleCy, CBE.ink);

    const leftX = x + 8;
    const rightX = x + w / 2 + 2;
    this.addLabel("我的报价", leftX, chrome.bodyY + 6, CBE.inkLabel);
    this.addLabel(
      `金${copperToGold(this.myMoney)}${this.offerSubmitted ? "*" : ""}`,
      leftX,
      chrome.bodyY + 20,
      CBE.inkMint,
    );
    this.myItems.slice(0, 4).forEach((it, i) => {
      const line = formatOfferLine(it);
      this.addLabel(line, leftX, chrome.bodyY + 34 + i * 14, CBE.inkBody);
      if (this.flowStep === 1 && this.mode === "list") {
        const idx = i;
        this.rowHits.push(
          attachRowPointerHit(this, leftX - 2, chrome.bodyY + 34 + i * 14, w / 2 - 12, 14, () => {
            this.removeMineAt(idx);
          }),
        );
      }
    });
    if (!this.myItems.length) {
      this.addLabel("(空)", leftX, chrome.bodyY + 34, CBE.inkLabel);
    }

    this.addLabel("对方报价", rightX, chrome.bodyY + 6, CBE.inkLabel);
    this.addLabel(
      `金${copperToGold(this.peerOffer.money)}${this.peerSubmitted ? "*" : ""}`,
      rightX,
      chrome.bodyY + 20,
      CBE.inkMint,
    );
    this.peerOffer.items.slice(0, 4).forEach((it, i) => {
      const line = formatOfferLine(it);
      const rowY = chrome.bodyY + 34 + i * 14;
      this.addLabel(line, rightX, rowY, CBE.inkBody);
      const canView =
        this.bothSubmitted() &&
        isTradeOfferEquip(it) &&
        (this.mode === "list" || this.mode === "peer_detail");
      if (canView && this.mode === "list") {
        const viewX = Math.min(
          rightX + measureUiTextWidth(line) + 4,
          x + w - 28,
        );
        this.addLabel("查看", viewX, rowY, CBE.inkMint);
        const item = it;
        this.rowHits.push(
          attachRowPointerHit(this, viewX - 2, rowY - 2, 28, 14, () => {
            this.openPeerDetail(item);
          }),
        );
      }
    });
    if (!this.peerOffer.items.length) {
      this.addLabel("(空)", rightX, chrome.bodyY + 34, CBE.inkLabel);
    }

    const midY = chrome.bodyY + 89;
    this.box.rect(x + 4, midY - 2, w - 8, 1).fill({ color: CBE.inkLabel });

    if (this.mode === "peer_detail" && this.peerDetailItem) {
      this.drawPeerEquipDetail(x, midY, w, h - (midY - y));
      return;
    }
    if (this.mode === "pick_detail" && this.pickDetailItem) {
      this.drawPickItemDetail(x, midY, w, h - (midY - y));
      return;
    }

    this.drawStepBar(leftX, midY);

    if (this.mode === "money") {
      this.drawMoneyInput(leftX, midY, x, y, w, h);
      this.ime.show(this.moneyInputRect());
      return;
    }

    if (this.flowStep === 1) {
      this.drawConfigList(leftX, midY, x, w);
    } else {
      this.drawStep2Menu(leftX, midY, x, w);
    }
  }

  private drawStepBar(leftX: number, midY: number): void {
    const parts: Array<{ n: FlowStep; label: string }> = [
      { n: 1, label: "①配置" },
      { n: 2, label: "②确认" },
    ];
    let ox = leftX;
    parts.forEach((p, i) => {
      const done =
        (p.n === 1 && this.hasOffer()) ||
        (p.n === 2 && this.bothSubmitted());
      const cur = p.n === this.flowStep;
      this.addLabel(p.label, ox, midY + 2, cur ? CBE.ink : done ? CBE.inkMint : CBE.inkLabel);
      ox += p.label.length * 12 + 4;
      if (i < parts.length - 1) {
        this.addLabel("→", ox, midY + 2, CBE.inkLabel);
        ox += 16;
      }
    });
  }

  private drawConfigList(
    leftX: number,
    midY: number,
    x: number,
    w: number,
  ): void {
    this.clampConfigCursor();
    this.ensureBagVisible();
    const absRows = this.visibleConfigAbs();
    absRows.forEach((abs, vis) => {
      const row = this.configRowAt(abs);
      if (!row) return;
      const hi = abs === this.listCursor;
      const ry = midY + 18 + vis * 16;
      if (hi) {
        this.box.rect(x + 6, ry, w - 12, 15).fill({ color: CBE.select });
      }
      let text = "";
      let color = hi ? CBE.ink : CBE.inkMint;
      if (row.kind === "money") {
        text = `设置金币  ${copperToGold(this.myMoney)}`;
      } else if (row.kind === "bag") {
        const it = this.bag[row.index];
        if (it) {
          const en = it.enhance ? `+${it.enhance}` : "";
          const wash = equipHasWashedAttr(it) ? "[洗]" : "";
          const stackable = it.kind !== "equip" && it.count > 1;
          if (hi && stackable) {
            text = `${it.name}${wash}${en} <${this.pickQtyForItem(it)}/${it.count}>`;
          } else {
            text = `${it.name}${wash}${en} ×${it.count}`;
          }
          color = hi ? CBE.ink : qualityColor(it.quality);
        } else {
          text = "(空)";
          color = CBE.inkLabel;
        }
      }
      this.addLabel(text, leftX, ry + 2, color);
      this.rowHits.push(
        attachRowPointerHit(this, x + 6, ry, w - 12, 15, () => {
          this.listCursor = abs;
          this.resetPickQtyForCursor();
          this.redraw();
          const r = this.configRowAt(abs);
          if (r?.kind === "money") {
            this.confirm();
            return;
          }
          if (r?.kind === "bag") {
            const it = this.bag[r.index];
            // Stackables: tap selects; ←→ qty then OK → detail confirm.
            // Single/equip: open detail confirm immediately.
            if (it && (it.kind === "equip" || it.count <= 1)) this.confirm();
          }
        }),
      );
    });
    if (this.bag.length > BAG_PAGE_SIZE) {
      const pages = Math.max(1, Math.ceil(this.bag.length / BAG_PAGE_SIZE));
      const page = Math.floor(this.bagScroll / BAG_PAGE_SIZE) + 1;
      const navY = panelSafeFootY(
        midY + 18 + (1 + Math.min(BAG_PAGE_SIZE, this.bag.length)) * 16,
        14,
      );
      attachPageNavButtons({
        host: this,
        hits: this.rowHits,
        box: this.box,
        x: x + 8,
        y: navY,
        w: w - 16,
        page,
        pages,
        addLabel: (t, lx, ly, c) => this.addLabel(t, lx, ly, c),
        onPrev: () => {
          this.bagScroll = Math.max(0, this.bagScroll - BAG_PAGE_SIZE);
          const row = this.configRowAt(this.listCursor);
          if (row?.kind === "bag" && row.index >= this.bagScroll + BAG_PAGE_SIZE) {
            this.listCursor = this.bagScroll + 1;
          }
          if (row?.kind === "bag" && row.index < this.bagScroll) {
            this.listCursor = this.bagScroll + 1;
          }
          this.redraw();
        },
        onNext: () => {
          const maxScroll = Math.max(0, this.bag.length - BAG_PAGE_SIZE);
          this.bagScroll = Math.min(maxScroll, this.bagScroll + BAG_PAGE_SIZE);
          const row = this.configRowAt(this.listCursor);
          if (row?.kind === "bag" && row.index < this.bagScroll) {
            this.listCursor = this.bagScroll + 1;
          }
          this.redraw();
        },
      });
    }
  }

  private drawStep2Menu(
    leftX: number,
    midY: number,
    x: number,
    w: number,
  ): void {
    const waitLine = this.waitStatusLine();
    if (waitLine) {
      this.addLabel(waitLine, leftX, midY + 20, CBE.inkMint);
      return;
    }

    this.ensureStep2Cursor();
    // Both locked — prompt to confirm.
    this.addLabel("对方已锁定请确认交易", leftX, midY + 20, CBE.inkMint);
    const actions = this.step2Actions();
    actions.forEach((lab, i) => {
      const en = this.actionEnabled(lab);
      const hi = i === this.listCursor;
      const ry = midY + 38 + i * 16;
      if (hi) {
        this.box.rect(x + 6, ry, w - 12, 15).fill({ color: CBE.select });
      }
      const text: string = en ? lab : `${lab}(锁)`;
      this.addLabel(
        text,
        leftX,
        ry + 2,
        hi ? CBE.ink : en ? CBE.inkMint : CBE.inkLabel,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, x + 6, ry, w - 12, 15, () => {
          this.listCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    });
  }

  private drawPeerEquipDetail(
    x: number,
    bodyTop: number,
    w: number,
    bodyH: number,
  ): void {
    const it = this.peerDetailItem;
    if (!it) return;
    const bag = tradeOfferAsBagItem(it);
    const qColor = qualityColor(bag.quality);
    const titleH = 20;
    this.box.rect(x + 6, bodyTop, w - 12, titleH).fill({ color: CBE.bgPanelDark });
    const en = it.enhance > 0 ? `+${it.enhance}` : "";
    this.addLabel(
      `${it.name}${en}`.slice(0, 16),
      x + 10,
      bodyTop + 4,
      qColor,
    );
    this.addLabel("对方提交", x + w - 70, bodyTop + 4, CBE.inkLabel);

    const tipY = bodyTop + bodyH - 16;
    const bodyY = bodyTop + titleH + 6;
    const rowH = 14;
    const fitRows = Math.max(1, Math.floor((tipY - bodyY - 2) / rowH));
    const lines = buildEquipDetailLines(bag);
    const pageCount = Math.max(1, Math.ceil(lines.length / fitRows));
    const page = Math.min(Math.max(0, this.peerDetailPage), pageCount - 1);
    this.peerDetailPage = page;
    const start = page * fitRows;
    const pageLines = lines.slice(start, start + fitRows);
    let ly = bodyY;
    for (const line of pageLines) {
      const rows = this.paintEquipLine(line, x + 10, ly);
      ly += rowH * Math.max(1, rows);
    }
    // Same as right softkey 返回 — close detail, stay in trade.
    this.addLabel("返回", x + 10, tipY, CBE.inkMint);
    this.rowHits.push(
      attachRowPointerHit(this, x + 8, tipY - 2, 36, 16, () => {
        this.closePeerInspect();
      }),
    );
    if (pageCount > 1) {
      this.addLabel(
        `←→ ${page + 1}/${pageCount}`,
        x + w - 56,
        tipY,
        CBE.inkLabel,
      );
    }
  }

  /** Own bag pick confirm — item desc or equip attrs + 确认放入/取消. */
  private drawPickItemDetail(
    x: number,
    bodyTop: number,
    w: number,
    bodyH: number,
  ): void {
    const it = this.pickDetailItem;
    if (!it) return;
    const qColor = qualityColor(it.quality);
    const titleH = 20;
    this.box.rect(x + 6, bodyTop, w - 12, titleH).fill({ color: CBE.bgPanelDark });
    const en = it.enhance > 0 ? `+${it.enhance}` : "";
    const titleName = isCultivationBookItem(it.itemId)
      ? formatCultivationBookName(
          lookupItemSync(it.itemId)?.name ?? it.name,
          it.bookLevel,
          it.bookFull,
        )
      : it.name;
    this.addLabel(
      `${titleName}${en}`.slice(0, 14),
      x + 10,
      bodyTop + 4,
      qColor,
    );
    this.addLabel("放入确认", x + w - 70, bodyTop + 4, CBE.inkLabel);

    const acts = PICK_ACTIONS;
    const btnH = 18;
    const btnGap = 6;
    const btnW = 72;
    const actionTop = bodyTop + bodyH - btnH - 8;
    const tipY = actionTop - 14;
    const bodyY = bodyTop + titleH + 4;
    const rowH = 14;
    const fitRows = Math.max(1, Math.floor((tipY - bodyY - 2) / rowH));

    if (isTradeBagEquip(it)) {
      const lines = buildEquipDetailLines(it);
      const pageCount = Math.max(1, Math.ceil(lines.length / fitRows));
      const page = Math.min(Math.max(0, this.pickDetailPage), pageCount - 1);
      this.pickDetailPage = page;
      const start = page * fitRows;
      const pageLines = lines.slice(start, start + fitRows);
      let ly = bodyY;
      for (const line of pageLines) {
        const rows = this.paintEquipLine(line, x + 10, ly);
        ly += rowH * Math.max(1, rows);
      }
      if (pageCount > 1) {
        this.addLabel(
          `←→ ${page + 1}/${pageCount}`,
          x + w - 56,
          tipY,
          CBE.inkLabel,
        );
      }
    } else {
      const qtyHint =
        it.count > 1
          ? `放入 ${this.pickDetailQty}/${it.count}  ←→`
          : `数量 ${this.pickDetailQty}`;
      this.addLabel(qtyHint, x + 10, bodyY, CBE.inkMint);
      const desc =
        (it.desc && it.desc !== "-" ? it.desc : "") || "（无说明）";
      const wrapped = desc.split("\n").flatMap((p) => wrapChars(p));
      const maxLines = Math.max(1, fitRows - 1);
      wrapped.slice(0, maxLines).forEach((line, i) => {
        this.addLabel(line, x + 10, bodyY + 16 + i * rowH, CBE.inkBody);
      });
    }

    const totalW = acts.length * btnW + (acts.length - 1) * btnGap;
    let bx = Math.floor(x + (w - totalW) / 2);
    acts.forEach((lab, i) => {
      const hi = i === this.pickActionCursor;
      this.box
        .roundRect(bx, actionTop, btnW, btnH, 2)
        .fill({ color: CBE.bgPanelDark })
        .stroke({ width: 1, color: hi ? CBE.inkMint : CBE.borderBright });
      this.addLabel(
        lab,
        bx + Math.floor((btnW - measureUiTextWidth(lab)) / 2),
        actionTop + 3,
        hi ? CBE.inkMint : CBE.ink,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, bx, actionTop, btnW, btnH, () => {
          this.pickActionCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
      bx += btnW + btnGap;
    });
  }

  private paintEquipLine(line: EquipPaintLine, x: number, y: number): number {
    if (line.kind === "text" || line.kind === "milestone") {
      this.addLabel(line.text, x, y, line.color);
      return Math.max(1, wrapChars(line.text).length);
    }
    if (line.keyColor === line.valueColor) {
      const merged = `${line.key}${line.value}`;
      this.addLabel(merged, x, y, line.keyColor);
      return Math.max(1, wrapChars(merged).length);
    }
    this.addLabel(line.key, x, y, line.keyColor);
    const keyW = measureUiTextWidth(line.key);
    this.addLabel(line.value, x + keyW, y, line.valueColor);
    return 1;
  }

  /** Status while waiting on peer lock / confirm. */
  private waitStatusLine(): string | null {
    if (this.waitingPeer) return "等待对方确认";
    if (this.offerSubmitted && !this.peerSubmitted) return "等待对方锁定";
    return null;
  }

  private drawMoneyInput(
    leftX: number,
    midY: number,
    x: number,
    y: number,
    w: number,
    h: number,
  ): void {
    this.addLabel("设置金币", leftX, midY + 18, CBE.inkLabel);
    this.addLabel(
      `持有 ${copperToGold(this.moneyMax)}金`,
      leftX,
      midY + 36,
      CBE.inkMint,
    );

    const barY = y + h - 52;
    this.box
      .roundRect(x + 6, barY, w - 12, 28, 2)
      .fill({ color: 0x12241c })
      .stroke({ width: 1, color: CBE.borderDim });
    const shown = this.moneyDraft.length ? this.moneyDraft : "点击输入金币…";
    this.addLabel(
      shown,
      x + 14,
      barY + 8,
      this.moneyDraft ? CBE.ink : CBE.inkLabel,
    );
    this.inputHit
      .rect(x + 6, barY, w - 12, 28)
      .fill({ color: 0xffffff, alpha: 0.001 });
  }

  private addLabel(text: string, lx: number, ly: number, color: number): void {
    const t = new BitmapLabel();
    void t.setText(text, color);
    t.position.set(lx, ly);
    this.addChild(t);
    this.labels.push(t);
  }

  /** Capsule title — same size/center as SystemPanel popup titles. */
  private addTitleCentered(text: string, cy: number, color: number): void {
    const t = new BitmapLabel();
    t.eventMode = "none";
    t.anchor.set(0.5, 0.5);
    t.position.set(LCD_WIDTH / 2, Math.round(cy));
    void t.setText(text, color, { outline: false });
    this.addChild(t);
    this.labels.push(t);
  }
}

function filterTradeBag(bag: BagItem[]): BagItem[] {
  return bag.filter(
    (b) =>
      b.seq > 0 &&
      isTradeableItem(b.itemId, { enhance: b.enhance, kind: b.kind }),
  );
}

function cloneTradePanelSnap(snap: TradePanelSnap): TradePanelSnap {
  return {
    ...snap,
    bag: snap.bag.map((b) => ({ ...b })),
    myItems: snap.myItems.map((it) => ({
      ...it,
      ...(it.enhanceAttrs?.length
        ? { enhanceAttrs: it.enhanceAttrs.map((a) => ({ ...a })) }
        : {}),
    })),
    peerOffer: {
      money: snap.peerOffer.money,
      items: snap.peerOffer.items.map((it) => ({
        ...it,
        ...(it.enhanceAttrs?.length
          ? { enhanceAttrs: it.enhanceAttrs.map((a) => ({ ...a })) }
          : {}),
      })),
    },
  };
}

/**
 * Fold a background trade session update into a per-slot snap.
 * Returns null when the stall should close (cancel / done).
 */
export function foldTradeSessionSnap(
  snap: TradePanelSnap | null,
  u: TradeSessionUpdate,
  bag?: BagItem[],
): TradePanelSnap | null {
  if (u.kind === "open") {
    return {
      peerName: u.peerName || "对方",
      bag: filterTradeBag(bag ?? []),
      myMoney: 0,
      myItems: [],
      peerOffer: { money: 0, items: [] },
      waitingPeer: false,
      offerSubmitted: false,
      peerSubmitted: false,
      flowStep: 1,
      mode: "list",
      listCursor: 0,
      bagScroll: 0,
      pickQty: 1,
      moneyDraft: "",
      moneyMax: 0,
    };
  }
  if (u.kind === "cancel" || u.kind === "done") return null;
  if (!snap) return null;
  const next = cloneTradePanelSnap(snap);
  if (u.kind === "peer_offer") {
    next.peerOffer = {
      money: u.offer.money >>> 0,
      items: u.offer.items.map((it) => ({ ...it })),
    };
    next.peerSubmitted = true;
    next.waitingPeer = false;
    if (next.offerSubmitted) next.flowStep = 2;
    next.mode = "list";
    next.listCursor = 0;
    return next;
  }
  if (u.kind === "offer_ack") {
    next.waitingPeer = false;
    next.offerSubmitted = u.result === 1;
    if (u.result === 1) {
      next.flowStep = 2;
      next.mode = "list";
      next.listCursor = 0;
    } else if (next.flowStep === 2) {
      next.flowStep = 1;
      next.mode = "list";
      next.listCursor = 0;
    }
    return next;
  }
  if (u.kind === "waiting") {
    next.waitingPeer = true;
    next.offerSubmitted = true;
    next.flowStep = 2;
    next.mode = "list";
    next.listCursor = 0;
    return next;
  }
  return next;
}

/** Display gold from wire copper (1金=10000铜). */
function copperToGold(copper: number): number {
  return Math.floor((copper >>> 0) / COPPER_PER_GOLD);
}

function isTradeOfferEquip(it: TradeOfferItem): boolean {
  if (it.kind === "equip") return true;
  if (it.kind === "item") return false;
  return it.itemId >= 1000;
}

function isTradeBagEquip(it: BagItem): boolean {
  if (it.kind === "equip") return true;
  if (it.kind === "item") return false;
  return it.itemId >= 1000;
}

function tradeOfferAsBagItem(it: TradeOfferItem): BagItem {
  const kind = isTradeOfferEquip(it) ? "equip" : "item";
  return {
    itemId: it.itemId,
    seq: it.seq,
    count: it.count,
    name: it.name,
    quality: it.quality ?? 0,
    enhance: it.enhance,
    kind,
    category: 0,
    desc: it.desc ?? "",
    usable: false,
    equipSlot: it.equipSlot ?? -1,
    ...(it.enhanceAttrs?.length ? { enhanceAttrs: it.enhanceAttrs } : {}),
    ...(it.bookLevel && it.bookLevel > 0
      ? { bookLevel: it.bookLevel, bookFull: it.bookFull }
      : {}),
  };
}

function formatOfferLine(it: TradeOfferItem): string {
  const en = it.enhance ? `+${it.enhance}` : "";
  const wash = equipHasWashedAttr(tradeOfferAsBagItem(it)) ? "[洗]" : "";
  let name = it.name;
  if (isCultivationBookItem(it.itemId) && (it.bookLevel ?? 0) > 0) {
    const base = lookupItemSync(it.itemId)?.name ?? "修炼天书";
    name = formatCultivationBookName(base, it.bookLevel, it.bookFull);
  }
  return `${name}${wash}${en}×${it.count}`.slice(0, 16);
}
