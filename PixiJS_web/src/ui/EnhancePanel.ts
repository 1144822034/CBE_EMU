/**
 * CBE equipment enhance / recast (强化转移) panels.
 * Evidence: docs/re/2026-07-19-equipment-enhancement.md,
 * docs/re/2026-08-02-equipment-enhance-transfer-29-5.md, screenshots.
 */
import { Container, Graphics } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbePanel,
} from "./constants";
import { BitmapLabel, measureUiTextWidth } from "./BitmapLabel";
import type { BagItem } from "../net/protocolExtras";
import {
  ENHANCE_CRYSTAL_FIRST,
  ENHANCE_CRYSTAL_LAST,
  ENHANCE_MAX_LEVEL,
  ENHANCE_MAX_SLOTS,
  ENHANCE_TRANSFER_FEE_TEXT,
  enhanceCrystalName,
  enhanceCrystalUnitRate,
  enhanceSuccessRate,
  isEnhanceCrystal,
  type EnhanceOpenResult,
} from "../net/protocolExtras";
import { qualityColor } from "../resources/itemCatalog";
import {
  buildEnhanceCompareLines,
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

export type EnhanceAction =
  | "submit"
  | "place"
  | "cancel"
  | "pick_crystal"
  | "pick_dest"
  | "confirm_transfer"
  | "cancel_transfer";

type Mode = "main" | "pickCrystal" | "transferPick" | "transferConfirm";

type CrystalRow = {
  itemId: number;
  tier: number;
  name: string;
  rate: number;
  count: number;
};

const SELECT_BG = 0x5a5a5a;
const INK_CYAN = 0x29faff;
const INK_GREEN = 0x00ff00;

export class EnhancePanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private mode: Mode = "main";
  private equip: BagItem | null = null;
  private bag: BagItem[] = [];
  private openInfo: EnhanceOpenResult | null = null;
  /** Placed crystal itemIds (one per slot, max 5). */
  private slots: number[] = [];
  private rate = 0;
  private money = 0;
  private toast: PanelTipState = createPanelTipState();
  private actionCursor = 0;
  private listCursor = 0;
  private listScroll = 0;
  private destCandidates: BagItem[] = [];
  private destPick: BagItem | null = null;
  private busy = false;

  private readonly mainActions = ["提交", "放置", "取消"] as const;

  onAction: ((action: EnhanceAction, payload?: number) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(
      new Graphics()
        .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
        .fill({ color: 0x000000, alpha: 0.45 }),
    );
    this.addChild(this.box);
  }

  openEnhance(equip: BagItem, bag: BagItem[], info: EnhanceOpenResult): void {
    this.equip = equip;
    this.bag = bag;
    this.openInfo = info;
    this.slots = [];
    this.rate = 0;
    this.money = 0;
    clearPanelTip(this.toast);
    this.mode = "main";
    this.actionCursor = 1; // 放置
    this.listCursor = 0;
    this.listScroll = 0;
    this.destCandidates = [];
    this.destPick = null;
    this.busy = false;
    this.visible = true;
    this.recomputeLocalRate();
    this.redraw();
  }

  openTransfer(source: BagItem, bag: BagItem[]): void {
    this.equip = source;
    this.bag = bag;
    this.openInfo = null;
    this.slots = [];
    this.rate = 0;
    this.money = 0;
    clearPanelTip(this.toast);
    this.mode = "transferPick";
    this.actionCursor = 0;
    this.listCursor = 0;
    this.listScroll = 0;
    this.destPick = null;
    this.busy = false;
    const slot = source.equipSlot;
    const quality = source.quality | 0;
    this.destCandidates = bag.filter(
      (it) =>
        it.kind === "equip" &&
        it.seq > 0 &&
        it.seq !== source.seq &&
        it.equipSlot === slot &&
        (it.quality | 0) === quality &&
        (it.enhance | 0) < (source.enhance | 0),
    );
    this.visible = true;
    this.redraw();
  }

  setBag(bag: BagItem[]): void {
    this.bag = bag;
    if (this.equip) {
      const fresh = bag.find((b) => b.seq === this.equip!.seq);
      if (fresh) this.equip = fresh;
    }
    if (this.mode === "transferPick" && this.equip) {
      const slot = this.equip.equipSlot;
      const quality = this.equip.quality | 0;
      const srcEnhance = this.equip.enhance | 0;
      this.destCandidates = bag.filter(
        (it) =>
          it.kind === "equip" &&
          it.seq > 0 &&
          it.seq !== this.equip!.seq &&
          it.equipSlot === slot &&
          (it.quality | 0) === quality &&
          (it.enhance | 0) < srcEnhance,
      );
      if (this.listCursor >= this.destCandidates.length) {
        this.listCursor = Math.max(0, this.destCandidates.length - 1);
      }
    }
    if (this.visible) this.redraw();
  }

  setOpenInfo(info: EnhanceOpenResult): void {
    this.openInfo = info;
    if (this.equip) this.equip = { ...this.equip, enhance: info.curLevel };
    this.recomputeLocalRate();
    if (this.visible) this.redraw();
  }

  setPreview(rate: number, money: number): void {
    this.rate = rate;
    this.money = money;
    if (this.visible) this.redraw();
  }

  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  setTip(text: string): void {
    this.showTip(text);
  }

  setBusy(busy: boolean): void {
    this.busy = busy;
  }

  showTransferConfirm(dest: BagItem): void {
    this.destPick = dest;
    this.mode = "transferConfirm";
    this.actionCursor = 0;
    this.redraw();
  }

  /** Materials for 29/2 / 29/3 (group same crystal ids). */
  getMaterials(): { itemId: number; count: number }[] {
    const map = new Map<number, number>();
    for (const id of this.slots) {
      map.set(id, (map.get(id) || 0) + 1);
    }
    return [...map.entries()].map(([itemId, count]) => ({ itemId, count }));
  }

  getEquipSeq(): number {
    return this.equip?.seq ?? 0;
  }

  getSourceSeq(): number {
    return this.equip?.seq ?? 0;
  }

  getDestSeq(): number {
    return this.destPick?.seq ?? 0;
  }

  move(delta: number): void {
    if (!this.visible || this.busy) return;
    if (this.mode === "main") {
      const n = this.mainActions.length;
      this.actionCursor = (this.actionCursor + delta + n) % n;
      this.redraw();
      return;
    }
    if (this.mode === "pickCrystal") {
      const rows = this.crystalRows();
      if (!rows.length) return;
      this.listCursor = (this.listCursor + delta + rows.length) % rows.length;
      this.ensureListVisible(rows.length, 7);
      this.redraw();
      return;
    }
    if (this.mode === "transferPick") {
      if (!this.destCandidates.length) return;
      this.listCursor =
        (this.listCursor + delta + this.destCandidates.length) %
        this.destCandidates.length;
      this.ensureListVisible(this.destCandidates.length, 8);
      this.redraw();
      return;
    }
    if (this.mode === "transferConfirm") {
      this.actionCursor = this.actionCursor === 0 ? 1 : 0;
      this.redraw();
    }
  }

  confirm(): void {
    if (!this.visible || this.busy) return;
    if (this.mode === "main") {
      const act = this.mainActions[this.actionCursor];
      if (act === "提交") this.onAction?.("submit");
      else if (act === "放置") this.onAction?.("place");
      else this.onAction?.("cancel");
      return;
    }
    if (this.mode === "pickCrystal") {
      const rows = this.crystalRows();
      const row = rows[this.listCursor];
      if (!row) return;
      if (this.slots.length >= ENHANCE_MAX_SLOTS) {
        this.showTip("最多可放入五颗玄晶");
        return;
      }
      if (row.count <= 0) {
        this.showTip("玄晶数量不足");
        return;
      }
      // Available after already placing same id
      const placed = this.slots.filter((id) => id === row.itemId).length;
      if (placed >= row.count) {
        this.showTip("玄晶数量不足");
        return;
      }
      this.onAction?.("pick_crystal", row.itemId);
      return;
    }
    if (this.mode === "transferPick") {
      const dest = this.destCandidates[this.listCursor];
      if (!dest) {
        this.showTip("没有可转移的同类装备");
        return;
      }
      this.onAction?.("pick_dest", dest.seq);
      return;
    }
    if (this.mode === "transferConfirm") {
      if (this.actionCursor === 0) this.onAction?.("confirm_transfer");
      else this.onAction?.("cancel_transfer");
    }
  }

  cancel(): void {
    if (!this.visible) return;
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
    if (this.busy) return;
    if (this.mode === "pickCrystal") {
      this.mode = "main";
      this.actionCursor = 1;
      clearPanelTip(this.toast);
      this.redraw();
      return;
    }
    if (this.mode === "transferConfirm") {
      this.mode = "transferPick";
      this.destPick = null;
      clearPanelTip(this.toast);
      this.redraw();
      return;
    }
    this.onAction?.("cancel");
  }

  /** Called by main when place opens crystal list. */
  enterPickCrystal(): void {
    if (this.slots.length >= ENHANCE_MAX_SLOTS) {
      this.showTip("最多可放入五颗玄晶");
      return;
    }
    this.mode = "pickCrystal";
    this.listCursor = 0;
    this.listScroll = 0;
    clearPanelTip(this.toast);
    this.redraw();
  }

  /** Add one crystal and return to main. */
  placeCrystal(itemId: number): void {
    if (!isEnhanceCrystal(itemId) || this.slots.length >= ENHANCE_MAX_SLOTS) return;
    this.slots.push(itemId);
    this.mode = "main";
    this.actionCursor = 0; // 提交
    this.recomputeLocalRate();
    this.redraw();
  }

  clearSlots(): void {
    this.slots = [];
    this.rate = 0;
    this.money = 0;
    this.recomputeLocalRate();
    if (this.visible) this.redraw();
  }

  hide(): void {
    clearPanelTip(this.toast);
    this.visible = false;
    this.equip = null;
    this.slots = [];
    this.busy = false;
    for (const t of this.labels) t.destroy();
    this.labels = [];
  }

  private recomputeLocalRate(): void {
    const L = this.openInfo?.curLevel ?? this.equip?.enhance ?? 0;
    const mats = this.getMaterials();
    if (!mats.length) {
      this.rate = 0;
      return;
    }
    // Prefer server table when present
    if (this.openInfo?.crystalPower.length) {
      let total = 0;
      for (const m of mats) {
        const tier = m.itemId - ENHANCE_CRYSTAL_FIRST + 1;
        const unit = this.openInfo.crystalPower[tier - 1] ?? 0;
        total += unit * m.count;
      }
      this.rate = Math.min(100, total);
      return;
    }
    this.rate = enhanceSuccessRate(L, mats);
  }

  private crystalRows(): CrystalRow[] {
    const L = this.openInfo?.curLevel ?? this.equip?.enhance ?? 0;
    const counts = new Map<number, number>();
    for (const it of this.bag) {
      if (!isEnhanceCrystal(it.itemId)) continue;
      counts.set(it.itemId, (counts.get(it.itemId) || 0) + (it.count | 0));
    }
    const rows: CrystalRow[] = [];
    for (let id = ENHANCE_CRYSTAL_FIRST; id <= ENHANCE_CRYSTAL_LAST; id++) {
      const tier = id - ENHANCE_CRYSTAL_FIRST + 1;
      const unit =
        this.openInfo?.crystalPower[tier - 1] ??
        enhanceCrystalUnitRate(L, tier);
      rows.push({
        itemId: id,
        tier,
        name: enhanceCrystalName(id),
        rate: unit,
        count: counts.get(id) || 0,
      });
    }
    return rows;
  }

  private ensureListVisible(len: number, visible: number): void {
    const vs = Math.max(1, visible);
    if (this.listCursor < this.listScroll) {
      this.listScroll = Math.floor(this.listCursor / vs) * vs;
    }
    if (this.listCursor >= this.listScroll + vs) {
      this.listScroll = Math.floor(this.listCursor / vs) * vs;
    }
    const maxScroll = Math.max(0, len - vs);
    this.listScroll = Math.max(0, Math.min(this.listScroll, maxScroll));
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
    for (const t of this.labels) t.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
    this.box.clear();

    const x = 8;
    const y = 20;
    const w = LCD_WIDTH - 16;
    const h = LCD_HEIGHT - SOFTKEY_H - 28;
    paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

    if (this.mode === "transferPick" || this.mode === "transferConfirm") {
      this.drawTransfer(x, y, w, h);
    } else if (this.mode === "pickCrystal") {
      this.drawPickCrystal(x, y, w, h);
    } else {
      this.drawMain(x, y, w, h);
    }

    drawPanelTipOverlay(this, this.toast);
  }

  private drawTitle(name: string, x: number, y: number, w: number): number {
    const titleH = 26;
    const titleY = y + 6;
    const q = qualityColor(this.equip?.quality ?? 3);
    this.box
      .roundRect(x + 10, titleY, w - 20, titleH, 4)
      .fill({ color: CBE.bgPanelDark })
      .stroke({ width: 1, color: CBE.borderBright });
    this.addLabelCentered(name.slice(0, 12), LCD_WIDTH / 2, titleY + titleH / 2, q);
    return titleY + titleH + 6;
  }

  private drawMain(x: number, y: number, w: number, h: number): void {
    const it = this.equip;
    if (!it) return;
    const bodyTop = this.drawTitle(it.name, x, y, w);
    const L = this.openInfo?.curLevel ?? it.enhance ?? 0;
    const next = Math.min(L + 1, this.openInfo?.maxLevel ?? ENHANCE_MAX_LEVEL);

    this.addAttr(`[强化]: ${L} ${next}`, x + 14, bodyTop, EQUIP_DETAIL_YELLOW);

    let ly = bodyTop + 16;
    const compare = buildEnhanceCompareLines(it, L, next).slice(0, 4);
    for (const line of compare) {
      this.paintLine(line, x + 14, ly);
      ly += 15;
    }
    this.addAttr(`强化附加: (${next}/16)`, x + 14, ly, EQUIP_DETAIL_YELLOW);
    ly += 16;
    this.addAttr(`成功率: ${this.rate}%`, x + 14, ly, INK_GREEN);
    if (this.money > 0) {
      ly += 15;
      this.addAttr(`消耗: ${formatCopper(this.money)}`, x + 14, ly, CBE.inkMint);
    }

    const softY = y + h - 28;
    const slotTop = softY - 20 - ENHANCE_MAX_SLOTS * 16;
    // Slot box
    this.box
      .rect(x + 8, slotTop - 4, w - 16, ENHANCE_MAX_SLOTS * 16 + 8)
      .stroke({ width: 1, color: CBE.borderDim });

    for (let i = 0; i < ENHANCE_MAX_SLOTS; i++) {
      const sy = slotTop + i * 16;
      const placed = this.slots[i];
      const text = placed
        ? `${i + 1}/5. ${enhanceCrystalName(placed)}`
        : `${i + 1}/5. 请放入强化玄晶`;
      const emptyFocus = !placed && i === this.slots.length;
      if (emptyFocus) {
        this.box.rect(x + 10, sy, w - 20, 15).fill({ color: 0x4a4a20 });
      }
      this.addAttr(
        text,
        x + 14,
        sy + 1,
        placed ? CBE.ink : emptyFocus ? EQUIP_DETAIL_YELLOW : CBE.ink,
      );
    }
    if (this.slots.length < ENHANCE_MAX_SLOTS) {
      this.drawScrollArrow(x + w - 18, slotTop + ENHANCE_MAX_SLOTS * 16 - 4, false, true);
    }

    // Bottom action bar: 提交 / 放置 / 取消
    const btnW = 56;
    const btnH = 18;
    const gap = 8;
    const totalW = this.mainActions.length * btnW + (this.mainActions.length - 1) * gap;
    let bx = Math.floor((LCD_WIDTH - totalW) / 2);
    const by = softY - 2;
    for (let i = 0; i < this.mainActions.length; i++) {
      const hi = i === this.actionCursor;
      this.box
        .roundRect(bx, by, btnW, btnH, 2)
        .fill({ color: CBE.bgPanelDark })
        .stroke({ width: 1, color: hi ? EQUIP_DETAIL_YELLOW : CBE.borderBright });
      this.addLabelCentered(
        this.mainActions[i]!,
        bx + btnW / 2,
        by + btnH / 2,
        hi ? EQUIP_DETAIL_YELLOW : CBE.ink,
      );
      const idx = i;
      const hitX = bx;
      this.rowHits.push(
        attachRowPointerHit(this, hitX, by, btnW, btnH, () => {
          this.actionCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
      bx += btnW + gap;
    }
  }

  private drawPickCrystal(x: number, y: number, w: number, h: number): void {
    const it = this.equip;
    if (!it) return;
    const bodyTop = this.drawTitle(it.name, x, y, w);
    const L = this.openInfo?.curLevel ?? it.enhance ?? 0;
    const next = Math.min(L + 1, ENHANCE_MAX_LEVEL);
    this.addAttr(`[强化]: ${L} ${next}`, x + 14, bodyTop, EQUIP_DETAIL_YELLOW);
    this.addAttr("请放入玄晶", x + 14, bodyTop + 16, INK_CYAN);

    const rows = this.crystalRows();
    const visible = 7;
    this.ensureListVisible(rows.length, visible);
    const listTop = bodyTop + 36;
    const pages = Math.max(1, Math.ceil(Math.max(rows.length, 1) / visible));
    const pageStart = this.listScroll;
    const page = rows.length ? Math.floor(pageStart / visible) + 1 : 1;
    const navH = pages > 1 ? 22 : 0;
    this.box
      .rect(x + 8, listTop - 2, w - 16, visible * 16 + 4)
      .stroke({ width: 1, color: CBE.borderDim });

    for (let i = 0; i < visible; i++) {
      const idx = pageStart + i;
      const row = rows[idx];
      if (!row) break;
      const sy = listTop + i * 16;
      const hi = idx === this.listCursor;
      if (hi) this.box.rect(x + 10, sy, w - 20, 15).fill({ color: SELECT_BG });
      const line = `${row.name} (+${row.rate}%) X${row.count}`;
      this.addAttr(line, x + 14, sy + 1, hi ? CBE.ink : CBE.inkMint);
      this.rowHits.push(
        attachRowPointerHit(this, x + 10, sy, w - 20, 15, () => {
          this.listCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    }
    if (pages > 1) {
      attachPageNavButtons({
        host: this,
        hits: this.rowHits,
        box: this.box,
        x: x + 10,
        y: listTop + visible * 16 + 4,
        w: w - 20,
        page,
        pages,
        addLabel: (t, lx, ly, c) => this.addAttr(t, lx, ly, c),
        onPrev: () => {
          this.listScroll = Math.max(0, pageStart - visible);
          this.listCursor = Math.min(
            this.listCursor,
            this.listScroll + visible - 1,
          );
          this.listCursor = Math.max(this.listScroll, this.listCursor);
          this.redraw();
        },
        onNext: () => {
          const maxScroll = Math.max(0, rows.length - visible);
          this.listScroll = Math.min(maxScroll, pageStart + visible);
          this.listCursor = Math.max(this.listCursor, this.listScroll);
          this.listCursor = Math.min(
            rows.length - 1,
            this.listScroll + visible - 1,
          );
          this.redraw();
        },
      });
    }

    this.addAttr(
      "每次可放入一颗玄晶，最多可放入五颗。",
      x + 10,
      y + h - 34 - navH,
      CBE.inkMint,
    );
  }

  private drawTransfer(x: number, y: number, w: number, h: number): void {
    const src = this.equip;
    if (!src) return;
    const bodyTop = this.drawTitle(src.name, x, y, w);

    if (this.mode === "transferConfirm" && this.destPick) {
      this.addAttr("确认强化转移", x + 14, bodyTop, EQUIP_DETAIL_YELLOW);
      this.addAttr(
        `目标: ${this.destPick.name}`,
        x + 14,
        bodyTop + 18,
        CBE.ink,
      );
      this.addAttr(
        `强化 +${src.enhance} → 目标`,
        x + 14,
        bodyTop + 36,
        EQUIP_DETAIL_YELLOW,
      );
      this.addAttr(
        `消耗金钱: ${ENHANCE_TRANSFER_FEE_TEXT}`,
        x + 14,
        bodyTop + 54,
        INK_GREEN,
      );
      this.addAttr("源装备强化将归零", x + 14, bodyTop + 72, CBE.inkWarn);

      const acts = ["确认", "取消"] as const;
      const btnW = 64;
      const btnH = 20;
      const by = y + h - 40;
      for (let i = 0; i < acts.length; i++) {
        const hi = i === this.actionCursor;
        const bx = LCD_WIDTH / 2 - btnW - 8 + i * (btnW + 16);
        this.box
          .roundRect(bx, by, btnW, btnH, 2)
          .fill({ color: CBE.bgPanelDark })
          .stroke({ width: 1, color: hi ? EQUIP_DETAIL_YELLOW : CBE.borderBright });
        this.addLabelCentered(
          acts[i]!,
          bx + btnW / 2,
          by + btnH / 2,
          hi ? EQUIP_DETAIL_YELLOW : CBE.ink,
        );
        const idx = i;
        this.rowHits.push(
          attachRowPointerHit(this, bx, by, btnW, btnH, () => {
            this.actionCursor = idx;
            this.redraw();
            this.confirm();
          }),
        );
      }
      return;
    }

    // wrap instruction at full UC3 size; break by glyph width so lines fit panel
    const instruct =
      "使用强化转移功能可将强化效果100%转移到另一件同类装备上，请选择要转移到的目标装备：";
    const instructBottom = this.wrapText(
      instruct,
      x + 12,
      bodyTop,
      w - 24,
      EQUIP_DETAIL_YELLOW,
    );

    const listTop = Math.max(bodyTop + 56, instructBottom + 6);
    const listBottom = y + h - 36;
    const visible = Math.max(3, Math.floor((listBottom - listTop) / 16));
    this.box
      .rect(x + 8, listTop - 2, w - 16, visible * 16 + 4)
      .stroke({ width: 1, color: CBE.borderDim });

    if (!this.destCandidates.length) {
      this.addAttr("(无同品质同类可转移装备)", x + 14, listTop + 4, CBE.inkMint);
    } else {
      this.ensureListVisible(this.destCandidates.length, visible);
      const pageStart = this.listScroll;
      const pageCount = Math.max(
        1,
        Math.ceil(this.destCandidates.length / visible),
      );
      const page = Math.floor(pageStart / visible) + 1;
      for (let i = 0; i < visible; i++) {
        const idx = pageStart + i;
        const row = this.destCandidates[idx];
        if (!row) break;
        const sy = listTop + i * 16;
        const hi = idx === this.listCursor;
        if (hi) this.box.rect(x + 10, sy, w - 20, 15).fill({ color: SELECT_BG });
        const en = row.enhance > 0 ? `(+${row.enhance})` : "";
        this.addAttr(
          `${row.name}${en}`,
          x + 14,
          sy + 1,
          hi ? CBE.ink : CBE.inkMint,
        );
        this.rowHits.push(
          attachRowPointerHit(this, x + 10, sy, w - 20, 15, () => {
            this.listCursor = idx;
            this.redraw();
            this.confirm();
          }),
        );
      }
      if (pageCount > 1) {
        attachPageNavButtons({
          host: this,
          hits: this.rowHits,
          box: this.box,
          x: x + 10,
          y: Math.min(listTop + visible * 16 + 4, y + h - 24),
          w: w - 20,
          page,
          pages: pageCount,
          addLabel: (t, lx, ly, c) => this.addAttr(t, lx, ly, c),
          onPrev: () => {
            this.listScroll = Math.max(0, pageStart - visible);
            this.listCursor = Math.min(
              this.listCursor,
              this.listScroll + visible - 1,
            );
            this.listCursor = Math.max(this.listScroll, this.listCursor);
            this.redraw();
          },
          onNext: () => {
            const maxScroll = Math.max(
              0,
              this.destCandidates.length - visible,
            );
            this.listScroll = Math.min(maxScroll, pageStart + visible);
            this.listCursor = Math.max(this.listCursor, this.listScroll);
            this.listCursor = Math.min(
              this.destCandidates.length - 1,
              this.listScroll + visible - 1,
            );
            this.redraw();
          },
        });
      }
    }
  }

  /** @returns y just below last line */
  private wrapText(
    text: string,
    x: number,
    y: number,
    maxW: number,
    color: number,
  ): number {
    // CBE tip: ~13 汉字/行（略窄于面板，避免贴边）
    const maxChars = 13;
    void maxW;
    const lines: string[] = [];
    let cur = "";
    let chars = 0;
    for (const ch of text) {
      const units = (ch.codePointAt(0) ?? 0) > 0xff ? 1 : 0.5;
      if (chars + units > maxChars && cur) {
        lines.push(cur);
        cur = ch;
        chars = units;
      } else {
        cur += ch;
        chars += units;
      }
    }
    if (cur) lines.push(cur);

    let ly = y;
    for (const line of lines) {
      this.addAttr(line, x, ly, color);
      ly += 15;
    }
    return ly;
  }

  private paintLine(line: EquipPaintLine, x: number, y: number): void {
    if (line.kind === "text" || line.kind === "milestone") {
      this.addAttr(line.text, x, y, line.color);
      return;
    }
    // Same-color key+value → one Label; two-tone (e.g. quality + yellow +N) stays split.
    if (line.keyColor === line.valueColor) {
      this.addAttr(`${line.key}${line.value}`, x, y, line.keyColor);
      return;
    }
    this.addAttr(line.key, x, y, line.keyColor);
    this.addAttr(
      line.value,
      x + measureUiTextWidth(line.key),
      y,
      line.valueColor,
    );
  }

  private drawScrollArrow(ax: number, ay: number, up: boolean, on: boolean): void {
    if (!on) return;
    const c = INK_GREEN;
    if (up) {
      this.box.poly([ax, ay, ax - 5, ay + 7, ax + 5, ay + 7]).fill({ color: c });
    } else {
      this.box.poly([ax, ay + 7, ax - 5, ay, ax + 5, ay]).fill({ color: c });
    }
  }

  private addAttr(text: string, x: number, y: number, color: number): void {
    const t = new BitmapLabel();
    void t.setText(text, color);
    t.position.set(x, y);
    this.addChild(t);
    this.labels.push(t);
  }

  private addLabelCentered(
    text: string,
    cx: number,
    cy: number,
    color: number,
  ): void {
    const t = new BitmapLabel();
    void t.setText(text, color);
    t.anchor.set(0.5, 0.5);
    t.position.set(cx, cy);
    this.addChild(t);
    this.labels.push(t);
  }
}

function formatCopper(copper: number): string {
  const c = Math.max(0, copper >>> 0);
  const jin = Math.floor(c / 10000);
  const yin = Math.floor((c % 10000) / 100);
  const tong = c % 100;
  const parts: string[] = [];
  if (jin) parts.push(`${jin}金`);
  if (yin) parts.push(`${yin}银`);
  if (tong || !parts.length) parts.push(`${tong}铜`);
  return parts.join("");
}
