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
  isDurableMallPassItemId,
  isReservoirFlaskItemId,
  type BagItem,
} from "../net/protocolExtras";
import { lookupItemSync, qualityColor } from "../resources/itemCatalog";
import type { ZangbaoListing } from "../net/zangbaoApi";
import {
  buildEquipDetailLines,
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
const BAG_SELECT = 0x20676e;
/** 上架最低酷币价（与服端 VM_MOCK_ZANGBAO_MIN_PRICE 一致）。 */
const MIN_PRICE_KCOIN = 10;
/** 提现最低酷币（与服端 VM_MOCK_KCOIN_WITHDRAW_MIN 一致，且须为 10 的倍数）。 */
const MIN_WITHDRAW_KCOIN = 50;

type AmountKind = "list" | "withdraw";
type DetailActionId = "buy" | "cancel" | "back";

function listingAsBagItem(r: ZangbaoListing): BagItem {
  const cat = lookupItemSync(r.itemId);
  const kind = cat?.kind === "equip" ? "equip" : "item";
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
  };
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
  /** 浏览/我的：拍品详情（对齐物品竞拍）。 */
  private detailListing: ZangbaoListing | null = null;
  private detailActionCursor = 0;
  private detailPage = 0;
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
    this.browse = data.browse || [];
    this.mine = data.mine || [];
    this.bag = data.bag || [];
    this.kcoin = data.kcoin || 0;
    this.hasPayee = !!data.hasPayee;
    this.withdrawRows = data.withdraws || [];
    this.tab = data.preferTab || "browse";
    this.rowIndex = 0;
    this.page = 0;
    this.priceMode = false;
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
    if (partial.browse) this.browse = partial.browse;
    if (partial.mine) this.mine = partial.mine;
    if (partial.bag) this.bag = partial.bag;
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
    if (this.detailListing) {
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
        left: this.bag[this.rowIndex] ? "定价" : "",
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

  isDetailMode(): boolean {
    return !!this.detailListing;
  }

  showTip(msg: string): void {
    showPanelTip(this.tip, msg, () => this.redraw());
    this.redraw();
  }

  switchTab(dir: -1 | 1): void {
    if (this.priceMode || this.detailListing) return;
    const i = TABS.findIndex((t) => t.id === this.tab);
    const n = (i + dir + TABS.length) % TABS.length;
    this.tab = TABS[n].id;
    this.rowIndex = 0;
    this.page = 0;
    this.redraw();
    this.notifySoftkeys();
  }

  /**
   * 左右方向键：详情翻页；否则切顶栏标签。
   * @returns true 已处理。
   */
  horizontal(dir: -1 | 1): boolean {
    if (!this.visible || this.priceMode) return false;
    if (this.detailListing) {
      this.detailPage = Math.max(0, this.detailPage + dir);
      this.redraw();
      return true;
    }
    this.switchTab(dir);
    return true;
  }

  move(dir: -1 | 1): void {
    if (this.priceMode) return;
    if (this.detailListing) {
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
      if (!this.bag.length) {
        this.showTip("背包为空");
        return;
      }
      this.enterAmountMode("list", String(MIN_PRICE_KCOIN));
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
      this.redraw();
      this.notifySoftkeys();
      return true;
    }
    if (this.detailListing) {
      this.clearDetail();
      this.redraw();
      this.notifySoftkeys();
      return true;
    }
    this.close();
    return true;
  }

  private clearDetail(): void {
    this.detailListing = null;
    this.detailActionCursor = 0;
    this.detailPage = 0;
  }

  private openDetail(listing: ZangbaoListing): void {
    this.detailListing = listing;
    this.detailActionCursor = 0;
    this.detailPage = 0;
    this.redraw();
    this.notifySoftkeys();
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
    this.enterAmountMode(
      "withdraw",
      String(Math.max(MIN_WITHDRAW_KCOIN, maxOk)),
    );
  }

  private enterAmountMode(kind: AmountKind, digits: string): void {
    this.amountKind = kind;
    this.priceMode = true;
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
    this.priceMode = false;
    this.amountKind = "list";
    this.notifySoftkeys();
    this.onAction?.("list", { bagSeq: row.seq, priceKcoin: amount });
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
    const actionH = acts.length * (btnH + 2) + 8;
    const actionTop = tipY - actionH;

    const titleH = 20;
    this.bg.rect(x + 6, bodyTop, w - 12, titleH).fill({ color: CBE.bgPanelDark });
    const en = r.enhanceLevel > 0 ? `+${r.enhanceLevel}` : "";
    this.addLabelCentered(
      `${item.name}${en}`,
      LCD_WIDTH / 2,
      bodyTop + titleH / 2,
      qColor,
    );

    const metaY = bodyTop + titleH + 4;
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

    this.bg.rect(x + 4, actionTop, w - 8, actionH).fill({ color: CBE.bgPanel });
    acts.forEach((act, i) => {
      const hi = i === this.detailActionCursor;
      const by = actionTop + 4 + i * (btnH + 2);
      if (hi) this.bg.rect(x + 8, by, w - 16, btnH).fill({ color: BAG_SELECT });
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
    this.addLabel("上下选操作 ←→翻页", x + 10, tipY, CBE.inkMint);
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
      if (itemName) {
        this.addLabel(
          `${itemName} x${bag?.count ?? 0}`,
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
          if (this.detailListing) return;
          const tabChanged = this.tab !== t.id;
          const wasPrice = this.priceMode;
          this.tab = t.id;
          this.rowIndex = 0;
          this.page = 0;
          this.priceMode = false;
          this.amountKind = "list";
          this.redraw();
          if (tabChanged || wasPrice) this.notifySoftkeys();
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
        const name = lookupItemSync(b.itemId)?.name || `#${b.itemId}`;
        text = `${name} x${b.count} seq${b.seq}`;
      } else {
        const L = slice[i] as ZangbaoListing;
        const name = lookupItemSync(L.itemId)?.name || `#${L.itemId}`;
        const en = L.enhanceLevel > 0 ? `+${L.enhanceLevel}` : "";
        text = `${name}${en}x${L.itemCount} ${L.priceKcoin}酷币`;
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
