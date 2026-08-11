/**
 * Player trade stall (CBE softkey list): bag pick → offer → peer mirror → confirm.
 * Gold amount uses ChatIme numeric field (same path as chat compose).
 */
import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
  drawCbeTitlePill,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { ChatIme } from "./ChatIme";
import type { BagItem } from "../net/protocolExtras";
import type { TradeOfferItem, TradeOfferView } from "../net/protocolSocial";
import { isTradeableItem, qualityColor } from "../resources/itemCatalog";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  setPanelTipSticky,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

export type TradeAction =
  | "pick_item"
  | "set_money"
  | "submit"
  | "confirm"
  | "cancel"
  | "add_bag_item"
  | "remove_mine";

type Mode = "actions" | "bag" | "mine" | "money";

/** Visible rows in bag pick list (scroll/page). */
const BAG_PAGE_SIZE = 8;

export class TradePanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private mode: Mode = "actions";
  private actionCursor = 0;
  private bagCursor = 0;
  private bagScroll = 0;
  private mineCursor = 0;
  private moneyDraft = "";
  private moneyMax = 0;
  private peerName = "";
  private bag: BagItem[] = [];
  private myMoney = 0;
  private myItems: TradeOfferItem[] = [];
  private peerOffer: TradeOfferView = { money: 0, items: [] };
  private status = "";
  private waitingPeer = false;
  private offerSubmitted = false;
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
  private toast: PanelTipState = createPanelTipState();

  private readonly actions = [
    "选择物品",
    "本方物品",
    "设置金币",
    "提交报价",
    "确认交易",
    "取消交易",
  ] as const;

  onAction: ((action: TradeAction, payload?: TradeOfferItem | BagItem) => void) | null =
    null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(
      new Graphics()
        .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
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

  open(peerName: string, bag: BagItem[]): void {
    this.releaseMoneyIme();
    this.peerName = peerName || "对方";
    this.bag = filterTradeBag(bag);
    this.myMoney = 0;
    this.myItems = [];
    this.peerOffer = { money: 0, items: [] };
    this.status = "放入物品或金币后提交报价";
    this.waitingPeer = false;
    this.offerSubmitted = false;
    this.mode = "actions";
    this.actionCursor = 0;
    this.bagCursor = 0;
    this.bagScroll = 0;
    this.moneyDraft = "";
    this.visible = true;
    setPanelTipSticky(
      this.toast,
      "交易步骤\n①选物/设金 → ②提交报价\n③双方确认交易",
      () => {
        if (this.visible) this.redraw();
      },
    );
    this.redraw();
  }

  setBag(bag: BagItem[]): void {
    this.bag = filterTradeBag(bag);
    if (this.bagCursor >= this.bag.length) {
      this.bagCursor = Math.max(0, this.bag.length - 1);
    }
    this.ensureBagVisible();
    if (this.visible) this.redraw();
  }

  setPeerOffer(offer: TradeOfferView): void {
    this.peerOffer = offer;
    this.status = "对方报价已更新，需重新确认";
    // Peer 21/6 only invalidates prior confirmations (server clears confirmedMask).
    // Keep local offerSubmitted so 「确认交易」 stays enabled; do not force re-submit.
    this.waitingPeer = false;
    if (this.visible) this.redraw();
  }

  setStatus(text: string, waiting = false, submitted = this.offerSubmitted): void {
    this.status = text;
    this.waitingPeer = waiting;
    this.offerSubmitted = submitted;
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
    this.offerSubmitted = false;
    this.waitingPeer = false;
    this.status = `本方金币 ${this.myMoney}`;
    this.redraw();
  }

  /** Open chat-style numeric gold entry. */
  beginSetMoney(ownedMax: number): void {
    this.moneyMax = Math.max(0, ownedMax >>> 0);
    this.moneyDraft = this.myMoney > 0 ? String(this.myMoney) : "";
    this.mode = "money";
    this.status = "输入金币后按确定";
    this.redraw();
    this.focusMoneyInput();
  }

  /**
   * Digits / Backspace when the HTML field is not focused (hardware keys).
   * While IME is focused the browser owns input.
   */
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
      this.myItems.push({
        seq: item.seq,
        itemId: item.itemId,
        count: qty,
        name: item.name,
        enhance: item.enhance,
      });
    }
    this.offerSubmitted = false;
    this.waitingPeer = false;
    this.status = `已放入 ${item.name}`;
    this.mode = "actions";
    this.redraw();
  }

  removeMineAt(index: number): void {
    if (index < 0 || index >= this.myItems.length) return;
    const [gone] = this.myItems.splice(index, 1);
    this.offerSubmitted = false;
    this.waitingPeer = false;
    this.status = gone ? `已移出 ${gone.name}` : this.status;
    this.redraw();
  }

  hide(): void {
    this.releaseMoneyIme();
    clearPanelTip(this.toast);
    this.visible = false;
  }

  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (!this.visible) return;
      this.redraw();
    }, ms);
  }

  /** Dismiss sticky step tip after first player action. */
  private clearStepTip(): void {
    if (this.toast.tipUntil === Number.POSITIVE_INFINITY) {
      clearPanelTip(this.toast);
    }
  }

  /** Drop HTML overlay when leaving trade phase (TitleScreen-style). */
  releaseIme(): void {
    this.releaseMoneyIme();
    if (this.mode === "money") this.mode = "actions";
  }

  destroy(options?: boolean | { children?: boolean; texture?: boolean }): void {
    this.releaseMoneyIme();
    this.ime.destroy();
    super.destroy(options);
  }

  move(delta: number): void {
    if (!this.visible) return;
    if (this.mode === "money") return;
    if (this.mode === "actions") {
      const n = this.actions.length;
      let next = this.actionCursor;
      for (let i = 0; i < n; i++) {
        next = (next + delta + n) % n;
        if (this.actionEnabled(this.actions[next]!)) break;
      }
      this.actionCursor = next;
    } else if (this.mode === "bag") {
      const n = Math.max(1, this.bag.length);
      this.bagCursor = (this.bagCursor + delta + n) % n;
      this.ensureBagVisible();
    } else {
      const n = Math.max(1, this.myItems.length);
      this.mineCursor = (this.mineCursor + delta + n) % n;
    }
    this.redraw();
  }

  confirm(): void {
    if (!this.visible) return;
    this.clearStepTip();
    if (this.mode === "bag") {
      const it = this.bag[this.bagCursor];
      if (it) this.onAction?.("add_bag_item", it);
      return;
    }
    if (this.mode === "mine") {
      this.removeMineAt(this.mineCursor);
      if (!this.myItems.length) this.mode = "actions";
      return;
    }
    if (this.mode === "money") {
      this.commitMoney();
      return;
    }
    const label = this.actions[this.actionCursor];
    if (!label || !this.actionEnabled(label)) {
      if (label === "提交报价") this.showTip("请先放入物品或金币");
      else if (label === "确认交易") {
        this.showTip(
          this.step1Done() ? "请先提交报价" : "请先放入物品或金币",
        );
      }
      return;
    }
    if (label === "选择物品") {
      this.mode = "bag";
      this.bagCursor = 0;
      this.bagScroll = 0;
      this.redraw();
      return;
    }
    if (label === "本方物品") {
      this.mode = "mine";
      this.mineCursor = 0;
      this.redraw();
      return;
    }
    if (label === "设置金币") {
      this.onAction?.("set_money");
      return;
    }
    if (label === "提交报价") {
      this.onAction?.("submit");
      return;
    }
    if (label === "确认交易") {
      this.onAction?.("confirm");
      return;
    }
    if (label === "取消交易") {
      this.onAction?.("cancel");
    }
  }

  cancel(): void {
    if (!this.visible) return;
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
    if (this.mode === "money") {
      this.cancelMoney();
      return;
    }
    if (this.mode !== "actions") {
      this.mode = "actions";
      this.redraw();
      return;
    }
    this.onAction?.("cancel");
  }

  /** Left/right — no keypad; refocus input in money mode. */
  moveHorizontal(_delta: number): void {
    if (!this.visible || this.mode !== "money") return;
    this.focusMoneyInput();
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

  private commitMoney(): void {
    if (!this.visible || this.mode !== "money") return;
    const raw = this.moneyDraft || this.ime.value;
    const n = Math.max(0, Math.min(this.moneyMax, Number(raw) || 0));
    this.releaseMoneyIme();
    this.setMoney(n);
    this.mode = "actions";
    this.redraw();
  }

  private cancelMoney(): void {
    this.releaseMoneyIme();
    this.moneyDraft = "";
    this.mode = "actions";
    this.status = "已取消设置金币";
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
    if (n > this.moneyMax) {
      this.status = `最多 ${this.moneyMax}`;
      return String(this.moneyMax);
    }
    return v;
  }

  private moneyInputRect(): { x: number; y: number; w: number; h: number } {
    const x = 6;
    const y = 24;
    const h = LCD_HEIGHT - SOFTKEY_H - 32;
    const barY = y + h - 52;
    return { x: x + 14, y: barY + 4, w: LCD_WIDTH - 12 - 28, h: 22 };
  }

  private ensureBagVisible(): void {
    if (this.bagCursor < this.bagScroll) this.bagScroll = this.bagCursor;
    if (this.bagCursor >= this.bagScroll + BAG_PAGE_SIZE) {
      this.bagScroll = this.bagCursor - BAG_PAGE_SIZE + 1;
    }
    const maxScroll = Math.max(0, this.bag.length - BAG_PAGE_SIZE);
    this.bagScroll = Math.max(0, Math.min(this.bagScroll, maxScroll));
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    this.inputHit.clear();
    const x = 6;
    const y = 24;
    const w = LCD_WIDTH - 12;
    const h = LCD_HEIGHT - SOFTKEY_H - 32;
    drawCbePanel(this.box, x, y, w, h, CBE.bgDeep);
    const title = `交易·${this.peerName}`;
    const pillW = Math.min(w - 16, title.length * 12 + 20);
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, y + 6, pillW, 18);
    this.addLabel(title.slice(0, 16), LCD_WIDTH / 2 - pillW / 2 + 6, y + 8, CBE.ink);

    const leftX = x + 8;
    const rightX = x + w / 2 + 2;
    this.addLabel("我的报价", leftX, y + 28, CBE.inkLabel);
    this.addLabel(
      `金${this.myMoney}${this.offerSubmitted ? "✓" : ""}`,
      leftX,
      y + 42,
      CBE.inkMint,
    );
    this.myItems.slice(0, 4).forEach((it, i) => {
      const hi = this.mode === "mine" && i === this.mineCursor;
      const line = formatOfferLine(it);
      this.addLabel(line, leftX, y + 56 + i * 14, hi ? CBE.ink : CBE.inkBody);
      if (this.mode === "mine") {
        const idx = i;
        this.rowHits.push(
          attachRowPointerHit(this, leftX - 2, y + 54 + i * 14, w / 2 - 12, 14, () => {
            this.mineCursor = idx;
            this.redraw();
            this.confirm();
          }),
        );
      }
    });
    if (!this.myItems.length) {
      this.addLabel("(空)", leftX, y + 56, CBE.inkLabel);
    }

    this.addLabel("对方报价", rightX, y + 28, CBE.inkLabel);
    this.addLabel(`金${this.peerOffer.money}`, rightX, y + 42, CBE.inkMint);
    this.peerOffer.items.slice(0, 4).forEach((it, i) => {
      this.addLabel(formatOfferLine(it), rightX, y + 56 + i * 14, CBE.inkBody);
    });
    if (!this.peerOffer.items.length) {
      this.addLabel("(空)", rightX, y + 56, CBE.inkLabel);
    }

    const midY = y + 120;
    this.box.rect(x + 4, midY - 2, w - 8, 1).fill({ color: CBE.inkLabel });

    if (this.mode === "bag") {
      const pages = Math.max(1, Math.ceil(this.bag.length / BAG_PAGE_SIZE));
      const page = Math.floor(this.bagScroll / BAG_PAGE_SIZE) + 1;
      this.addLabel(
        `背包（确定放入） ${page}/${pages}`,
        leftX,
        midY + 4,
        CBE.inkLabel,
      );
      if (!this.bag.length) {
        this.addLabel("(无可交易物品)", leftX, midY + 22, CBE.inkMint);
      } else {
        this.ensureBagVisible();
        const pageItems = this.bag.slice(
          this.bagScroll,
          this.bagScroll + BAG_PAGE_SIZE,
        );
        pageItems.forEach((it, i) => {
          const idx = this.bagScroll + i;
          const hi = idx === this.bagCursor;
          if (hi) {
            this.box
              .rect(x + 6, midY + 20 + i * 16, w - 12, 15)
              .fill({ color: CBE.select });
          }
          const en = it.enhance ? `+${it.enhance}` : "";
          const qColor = qualityColor(it.quality);
          this.addLabel(
            `${it.name}${en} ×${it.count}`.slice(0, 22),
            leftX,
            midY + 22 + i * 16,
            hi ? CBE.ink : qColor,
          );
          this.rowHits.push(
            attachRowPointerHit(this, x + 6, midY + 20 + i * 16, w - 12, 15, () => {
              this.bagCursor = idx;
              this.redraw();
              this.confirm();
            }),
          );
        });
        if (this.bagScroll > 0) {
          this.addLabel("▲", x + w - 18, midY + 22, CBE.inkMint);
        }
        if (this.bagScroll + BAG_PAGE_SIZE < this.bag.length) {
          this.addLabel(
            "▼",
            x + w - 18,
            midY + 22 + (BAG_PAGE_SIZE - 1) * 16,
            CBE.inkMint,
          );
        }
      }
    } else if (this.mode === "mine") {
      this.addLabel("本方物品（确定移出）", leftX, midY + 4, CBE.inkLabel);
    } else if (this.mode === "money") {
      this.drawMoneyInput(leftX, midY, x, y, w, h);
    } else {
      this.ensureActionCursor();
      this.actions.forEach((lab, i) => {
        const en = this.actionEnabled(lab);
        const hi = i === this.actionCursor;
        if (hi) {
          this.box
            .rect(x + 6, midY + 6 + i * 16, w - 12, 15)
            .fill({ color: CBE.select });
        }
        const text = en ? lab : `${lab}(锁)`;
        this.addLabel(
          text,
          leftX,
          midY + 8 + i * 16,
          hi ? CBE.ink : en ? CBE.inkMint : CBE.inkLabel,
        );
        const idx = i;
        this.rowHits.push(
          attachRowPointerHit(this, x + 6, midY + 6 + i * 16, w - 12, 15, () => {
            this.actionCursor = idx;
            this.redraw();
            this.confirm();
          }),
        );
      });
    }

    if (this.mode !== "money") {
      this.addLabel(this.guideLine(), leftX, y + h - 32, CBE.inkMint);
    }
    const tip = this.waitingPeer ? `等待对方… ${this.status}` : this.status;
    this.addLabel(tip.slice(0, 26), leftX, y + h - 18, CBE.inkLabel);

    if (this.mode === "money") {
      this.ime.show(this.moneyInputRect());
    }
    drawPanelTipOverlay(this, this.toast);
  }

  /** Persistent step line under the action list. */
  private guideLine(): string {
    if (this.mode === "bag") return "步骤：选物后确定放入 · 取消返回";
    if (this.mode === "mine") return "步骤：确定移出 · 取消返回";
    if (this.mode === "money") return "步骤：输入金币后确定";
    if (this.waitingPeer) return "步骤：③等待对方 · 可点确认交易";
    if (this.offerSubmitted) return "步骤：③双方确认交易即可完成";
    if (this.step1Done()) return "步骤：②提交报价 → ③双方确认";
    return "步骤：①选物/设金 → ②提交 → ③确认";
  }

  /** ① done when bag item or gold is offered. */
  private step1Done(): boolean {
    return this.myItems.length > 0 || this.myMoney > 0;
  }

  /** ② done after local offer submitted. */
  private step2Done(): boolean {
    return this.offerSubmitted;
  }

  private actionEnabled(label: string): boolean {
    if (label === "提交报价") return this.step1Done();
    if (label === "确认交易") return this.step1Done() && this.step2Done();
    return true;
  }

  private ensureActionCursor(): void {
    const cur = this.actions[this.actionCursor];
    if (cur && this.actionEnabled(cur)) return;
    const n = this.actions.length;
    for (let i = 0; i < n; i++) {
      if (this.actionEnabled(this.actions[i]!)) {
        this.actionCursor = i;
        return;
      }
    }
  }

  private drawMoneyInput(
    leftX: number,
    midY: number,
    x: number,
    y: number,
    w: number,
    h: number,
  ): void {
    this.addLabel("设置金币", leftX, midY + 4, CBE.inkLabel);
    this.addLabel(`持有 ${this.moneyMax}`, leftX, midY + 22, CBE.inkMint);
    this.addLabel("仅数字 · 确定提交 · 取消返回", leftX, midY + 40, CBE.inkLabel);

    const barY = y + h - 52;
    this.box
      .roundRect(x + 6, barY, w - 12, 28, 2)
      .fill({ color: 0x12241c })
      .stroke({ width: 1, color: CBE.borderDim });
    const shown = this.moneyDraft.length ? this.moneyDraft : "点击输入金币…";
    this.addLabel(
      shown.slice(0, 18),
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
}

function filterTradeBag(bag: BagItem[]): BagItem[] {
  return bag.filter(
    (b) =>
      b.seq > 0 &&
      isTradeableItem(b.itemId, { enhance: b.enhance, kind: b.kind }),
  );
}

function formatOfferLine(it: TradeOfferItem): string {
  const en = it.enhance ? `+${it.enhance}` : "";
  return `${it.name}${en}×${it.count}`.slice(0, 12);
}
