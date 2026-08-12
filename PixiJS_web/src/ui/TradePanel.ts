/**
 * Player trade stall — two-step flow:
 * ① configure (bag + gold); left softkey 锁定 = submit offer (21/5)
 * ② 确认交易 unlocks only after both sides locked/submitted.
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
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

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
type Mode = "list" | "money";

const BAG_PAGE_SIZE = 6;

const STEP2_ACTIONS = ["确认交易"] as const;

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

  softkeys(): { left: string; right: string } {
    // Right softkey 返回 = cancel trade (except leaving money IME).
    if (this.mode === "money") return { left: "确定", right: "返回" };
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

  setBag(bag: BagItem[]): void {
    this.bag = filterTradeBag(bag);
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
    this.mode = "list";
    this.listCursor = 0;
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

  beginSetMoney(ownedMax: number): void {
    this.moneyMax = Math.max(0, ownedMax >>> 0);
    this.moneyDraft = this.myMoney > 0 ? String(this.myMoney) : "";
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
      this.myItems.push({
        seq: item.seq,
        itemId: item.itemId,
        count: qty,
        name: item.name,
        enhance: item.enhance,
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
    if (this.flowStep === 1) {
      const n = Math.max(1, this.configRowCount());
      this.listCursor = (this.listCursor + delta + n) % n;
      this.ensureBagVisible();
      this.resetPickQtyForCursor();
    } else {
      const n = STEP2_ACTIONS.length;
      let next = this.listCursor;
      for (let i = 0; i < n; i++) {
        next = (next + delta + n) % n;
        if (this.actionEnabled(STEP2_ACTIONS[next]!)) break;
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
    if (this.flowStep === 1) {
      this.confirmConfigRow();
      return;
    }
    this.ensureStep2Cursor();
    const label = STEP2_ACTIONS[this.listCursor];
    if (!label || !this.actionEnabled(label)) return;
    if (label === "确认交易") {
      this.onAction?.("confirm");
    }
  }

  /** Right softkey 返回 — leave money IME, else cancel trade. */
  cancel(): void {
    if (!this.visible) return;
    if (this.mode === "money") {
      this.cancelMoney();
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
      if (it) this.addFromBag(it, this.pickQtyForItem(it));
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
    const cur = STEP2_ACTIONS[this.listCursor];
    if (cur && this.actionEnabled(cur)) return;
    for (let i = 0; i < STEP2_ACTIONS.length; i++) {
      if (this.actionEnabled(STEP2_ACTIONS[i]!)) {
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
    const n = Math.max(0, Math.min(this.moneyMax, Number(raw) || 0));
    this.releaseMoneyIme();
    this.setMoney(n);
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
    if (n > this.moneyMax) return String(this.moneyMax);
    return v;
  }

  private moneyInputRect(): { x: number; y: number; w: number; h: number } {
    const x = 6;
    const y = 24;
    const h = LCD_HEIGHT - SOFTKEY_H - 32;
    const barY = y + h - 52;
    return { x: x + 14, y: barY + 4, w: LCD_WIDTH - 12 - 28, h: 22 };
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
      `金${this.myMoney}${this.offerSubmitted ? "*" : ""}`,
      leftX,
      y + 42,
      CBE.inkMint,
    );
    this.myItems.slice(0, 4).forEach((it, i) => {
      const line = formatOfferLine(it);
      this.addLabel(line, leftX, y + 56 + i * 14, CBE.inkBody);
      if (this.flowStep === 1 && this.mode === "list") {
        const idx = i;
        this.rowHits.push(
          attachRowPointerHit(this, leftX - 2, y + 54 + i * 14, w / 2 - 12, 14, () => {
            this.removeMineAt(idx);
          }),
        );
      }
    });
    if (!this.myItems.length) {
      this.addLabel("(空)", leftX, y + 56, CBE.inkLabel);
    }

    this.addLabel("对方报价", rightX, y + 28, CBE.inkLabel);
    this.addLabel(
      `金${this.peerOffer.money}${this.peerSubmitted ? "*" : ""}`,
      rightX,
      y + 42,
      CBE.inkMint,
    );
    this.peerOffer.items.slice(0, 4).forEach((it, i) => {
      this.addLabel(formatOfferLine(it), rightX, y + 56 + i * 14, CBE.inkBody);
    });
    if (!this.peerOffer.items.length) {
      this.addLabel("(空)", rightX, y + 56, CBE.inkLabel);
    }

    const midY = y + 118;
    this.box.rect(x + 4, midY - 2, w - 8, 1).fill({ color: CBE.inkLabel });
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
        text = `设置金币  ${this.myMoney}`;
      } else if (row.kind === "bag") {
        const it = this.bag[row.index];
        if (it) {
          const en = it.enhance ? `+${it.enhance}` : "";
          const stackable = it.kind !== "equip" && it.count > 1;
          if (hi && stackable) {
            text = `${it.name}${en} <${this.pickQtyForItem(it)}/${it.count}>`.slice(
              0,
              22,
            );
          } else {
            text = `${it.name}${en} ×${it.count}`.slice(0, 22);
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
            // Stackables: tap selects; ←→ qty then OK. Single/equip: put immediately.
            if (it && (it.kind === "equip" || it.count <= 1)) this.confirm();
          }
        }),
      );
    });
    if (this.bagScroll > 0) {
      this.addLabel("▲", x + w - 18, midY + 34, CBE.inkMint);
    }
    if (this.bagScroll + BAG_PAGE_SIZE < this.bag.length) {
      this.addLabel(
        "▼",
        x + w - 18,
        midY + 18 + Math.min(BAG_PAGE_SIZE, this.bag.length) * 16,
        CBE.inkMint,
      );
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
    STEP2_ACTIONS.forEach((lab, i) => {
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
    this.addLabel(`持有 ${this.moneyMax}`, leftX, midY + 36, CBE.inkMint);

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
