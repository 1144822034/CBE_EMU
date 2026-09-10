/**
 * PixiJS-only equipment enhance wash (洗练) panel.
 * Protocol: WT 29/7 open, 29/8 preview, 29/9 confirm, 29/14 apply/keep.
 *
 * Controls: ↑↓ select tier · ←→ adjust +16 crystal count · left soft=wash ·
 * D-pad OK=crystal · right=close
 * After a successful roll: show current vs new; left=替换 · right=保留
 * +16 档：←→ 加减十六级玄晶数量（最多 3，3 颗 100%）.
 * (No HTML center softkey above the D-pad.)
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
import { BitmapLabel } from "./BitmapLabel";
import type { BagItem } from "../net/protocolExtras";
import {
  ENHANCE_CRYSTAL_FIRST,
  ENHANCE_CRYSTAL_LAST,
  enhanceCrystalName,
  enhanceWashMaxCrystalCount,
  enhanceWashSuccessRate,
  isEnhanceCrystal,
  type EnhanceWashOpenResult,
  type EnhanceWashTier,
} from "../net/protocolExtras";
import { qualityColor } from "../resources/itemCatalog";
import { EQUIP_DETAIL_GRAY, EQUIP_DETAIL_YELLOW } from "./equipDetail";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

export type WashAction =
  | "submit"
  | "cancel"
  | "pick_crystal"
  | "back"
  | "accept_replace"
  | "reject_replace";

export type WashReplaceOffer = {
  unlock: number;
  oldWashed: number;
  oldType: number;
  oldFlag: number;
  oldValue: number;
  newType: number;
  newFlag: number;
  newValue: number;
};

const ATTR_NAMES: Record<number, string> = {
  1: "力量",
  2: "敏捷",
  3: "智慧",
  4: "物攻",
  5: "护甲",
  6: "躲闪",
  7: "命中",
  8: "暴击",
  9: "气血",
  10: "法力",
};

const UNLOCKS = [4, 8, 12, 16] as const;
const SELECT_BG = 0x5a5a5a;
const INK_CYAN = 0x29faff;
const INK_RED = 0xff4444;

type Mode = "main" | "pickCrystal" | "replaceConfirm";

function formatWashAttr(
  type: number,
  flag: number,
  value: number,
  washed?: number,
): string {
  const name = ATTR_NAMES[type] ?? `属性${type || "?"}`;
  const pct = flag ? "%" : "";
  const tag =
    washed === undefined ? "" : washed ? "[洗]" : "[默]";
  return `${tag}${name}+${value}${pct}`;
}

export class WashPanel extends Container {
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  private rowHits: Container[] = [];
  private mode: Mode = "main";
  private equip: BagItem | null = null;
  private bag: BagItem[] = [];
  private openInfo: EnhanceWashOpenResult | null = null;
  private unlock = 4;
  private crystalId = 0;
  private crystalCount = 0;
  private rate = 0;
  private money = 0;
  private replaceOffer: WashReplaceOffer | null = null;
  private toast: PanelTipState = createPanelTipState();
  private listCursor = 0;

  onAction: ((action: WashAction) => void) | null = null;
  onClose: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.addChild(this.box);
    this.addChild(this.toast.host);
  }

  softkeys(): { left: string; right: string } {
    if (this.mode === "pickCrystal") {
      return { left: "确定", right: "返回" };
    }
    if (this.mode === "replaceConfirm") {
      return { left: "替换", right: "保留" };
    }
    // 不设 center：避免方向键上方多出中软按钮；玄晶走 D-pad 中键/OK。
    return { left: "洗练", right: "关闭" };
  }

  openWash(
    equip: BagItem,
    bag: BagItem[],
    info: EnhanceWashOpenResult,
    opts?: {
      keepUnlock?: number;
      keepCrystalId?: number;
      keepCrystalCount?: number;
    },
  ): void {
    const prevUnlock = opts?.keepUnlock ?? 0;
    const prevCrystal = opts?.keepCrystalId ?? 0;
    const prevCount = opts?.keepCrystalCount ?? 0;
    this.equip = equip;
    this.bag = bag;
    this.openInfo = info;
    this.mode = "main";
    this.replaceOffer = null;
    this.rate = 0;
    this.money = 0;
    const unlocked = this.unlockedTiers();
    if (prevUnlock && unlocked.includes(prevUnlock)) this.unlock = prevUnlock;
    else this.unlock = unlocked[0] ?? 4;
    const crystals = this.crystals();
    if (prevCrystal && crystals.some((c) => c.itemId === prevCrystal)) {
      this.crystalId = prevCrystal;
      this.crystalCount = this.clampCount(prevCrystal, prevCount || 1);
    } else {
      this.crystalId = 0;
      this.crystalCount = 0;
    }
    this.listCursor = 0;
    this.visible = true;
    this.redraw();
  }

  setBag(bag: BagItem[]): void {
    this.bag = bag;
    if (this.crystalId) {
      this.crystalCount = this.clampCount(this.crystalId, this.crystalCount);
      if (this.crystalCount <= 0) {
        this.crystalId = 0;
        this.crystalCount = 0;
      }
    }
    if (this.visible) this.redraw();
  }

  setPreview(rate: number, money: number): void {
    this.rate = rate;
    this.money = money;
    this.redraw();
  }

  applyOpen(info: EnhanceWashOpenResult): void {
    this.openInfo = info;
    this.redraw();
  }

  /** Enter replace-confirm after a successful 29/9 roll (attrs still pending). */
  showReplaceConfirm(offer: WashReplaceOffer): void {
    this.replaceOffer = offer;
    this.unlock = offer.unlock;
    this.mode = "replaceConfirm";
    this.redraw();
  }

  clearReplaceConfirm(): void {
    this.replaceOffer = null;
    if (this.mode === "replaceConfirm") this.mode = "main";
    this.redraw();
  }

  hasPendingReplace(): boolean {
    return this.mode === "replaceConfirm" && this.replaceOffer != null;
  }

  showTip(text: string, ms = 1200): void {
    showPanelTip(this.toast, text, () => this.redraw(), ms);
  }

  setTip(text: string): void {
    this.showTip(text);
  }

  clearTip(): void {
    clearPanelTip(this.toast);
    this.redraw();
  }

  close(): void {
    this.visible = false;
    this.equip = null;
    this.openInfo = null;
    this.crystalId = 0;
    this.crystalCount = 0;
    this.replaceOffer = null;
    this.mode = "main";
    clearPanelTip(this.toast);
    this.clearLabels();
  }

  /** ↑↓：主界面选档；选玄晶界面移动光标。 */
  move(delta: number): void {
    if (!this.visible) return;
    if (this.mode === "replaceConfirm") return;
    if (this.mode === "pickCrystal") {
      const rows = this.crystals();
      if (!rows.length) return;
      this.listCursor = (this.listCursor + delta + rows.length) % rows.length;
      this.redraw();
      return;
    }
    const unlocked = this.unlockedTiers();
    if (!unlocked.length) return;
    let idx = unlocked.indexOf(this.unlock);
    if (idx < 0) idx = 0;
    const next = unlocked[(idx + delta + unlocked.length) % unlocked.length]!;
    if (next === this.unlock) return;
    this.unlock = next;
    this.onUnlockChanged();
    this.redraw();
    this.onAction?.("back");
  }

  /**
   * ←→：+16 已放十六级玄晶时加减数量（1..3，受背包限制）.
   * Returns true when count changed (caller should refresh preview).
   */
  adjustCount(delta: number): boolean {
    if (!this.visible || this.mode !== "main") return false;
    if (!this.crystalId || this.crystalCount <= 0) return false;
    const max = enhanceWashMaxCrystalCount(this.unlock, this.crystalId);
    if (max <= 1) return false;
    const bagN = this.bagCountOf(this.crystalId);
    const lim = Math.min(max, bagN);
    if (lim <= 0) return false;
    const next = this.crystalCount + (delta < 0 ? -1 : 1);
    if (next < 1) {
      this.setTip("至少放入1颗");
      return false;
    }
    if (next > lim) {
      this.setTip(next > max ? "最多可放入三颗十六级玄晶" : "玄晶数量不足");
      return false;
    }
    if (next === this.crystalCount) return false;
    this.crystalCount = next;
    this.rate = 0;
    this.money = 0;
    this.redraw();
    return true;
  }

  private unlockedTiers(): number[] {
    const enhance = this.equip?.enhance ?? 0;
    return UNLOCKS.filter((u) => u <= enhance);
  }

  private bagCountOf(itemId: number): number {
    let n = 0;
    for (const it of this.bag) {
      if (it.itemId === itemId && it.count > 0) n += it.count;
    }
    return n;
  }

  private clampCount(itemId: number, want: number): number {
    if (!itemId || want <= 0) return 0;
    const max = Math.min(
      enhanceWashMaxCrystalCount(this.unlock, itemId),
      this.bagCountOf(itemId),
    );
    if (max <= 0) return 0;
    return Math.max(1, Math.min(max, Math.floor(want)));
  }

  private onUnlockChanged(): void {
    this.rate = 0;
    this.money = 0;
    if (!this.crystalId) {
      this.crystalCount = 0;
      return;
    }
    const next = this.clampCount(this.crystalId, this.crystalCount || 1);
    if (next <= 0) {
      this.crystalId = 0;
      this.crystalCount = 0;
    } else {
      this.crystalCount = next;
    }
  }

  private clearLabels(): void {
    for (const l of this.labels) l.destroy();
    this.labels = [];
    clearPointerHits(this.rowHits);
  }

  private addAttr(text: string, x: number, y: number, color: number): void {
    const t = new BitmapLabel();
    void t.setText(text, color);
    t.position.set(x, y);
    this.addChild(t);
    this.labels.push(t);
  }

  private tierLine(t: EnhanceWashTier): string {
    const name = ATTR_NAMES[t.type] ?? `属性${t.type}`;
    const pct = t.flag ? "%" : "";
    const tag = t.washed ? "洗" : "默";
    return `(+${t.unlock})[${tag}] ${name}+${t.value}${pct} 下次${t.nextGold}金`;
  }

  private crystals(): {
    itemId: number;
    tier: number;
    name: string;
    count: number;
  }[] {
    const map = new Map<number, number>();
    for (const it of this.bag) {
      if (!isEnhanceCrystal(it.itemId) || it.count <= 0) continue;
      map.set(it.itemId, (map.get(it.itemId) ?? 0) + it.count);
    }
    return [...map.entries()]
      .map(([itemId, count]) => {
        const tier = itemId - ENHANCE_CRYSTAL_FIRST + 1;
        return {
          itemId,
          tier,
          name: enhanceCrystalName(itemId),
          count,
        };
      })
      .sort((a, b) => a.tier - b.tier);
  }

  private selectTier(unlock: number): void {
    if (!this.equip || unlock > this.equip.enhance) return;
    if (this.unlock === unlock) return;
    this.unlock = unlock;
    this.onUnlockChanged();
    this.redraw();
    this.onAction?.("back");
  }

  /** Place crystal (count starts at 1); ←→ adjusts stack on main. */
  private applyCrystalPick(itemId: number): boolean {
    if (!isEnhanceCrystal(itemId)) {
      this.setTip("不是玄晶");
      return false;
    }
    const bagN = this.bagCountOf(itemId);
    if (bagN <= 0) {
      this.setTip("玄晶数量不足");
      return false;
    }
    this.crystalId = itemId;
    this.crystalCount = 1;
    this.rate = 0;
    this.money = 0;
    return true;
  }

  private confirmCrystalPick(): WashAction {
    const rows = this.crystals();
    const row = rows[this.listCursor];
    if (!row) {
      this.setTip("背包无玄晶");
      return "back";
    }
    this.mode = "main";
    if (!this.applyCrystalPick(row.itemId)) {
      this.redraw();
      return "back";
    }
    this.redraw();
    return "back";
  }


  /** webglcontextrestored: remount paintCbePanel Graphics then redraw. */
  nudgeAfterGpuResume(): void {
    this.box = replaceGraphics(this.box);
    if (this.toast.gfx.parent) {
      this.toast.gfx = replaceGraphics(this.toast.gfx);
    }
    if (this.visible) this.redraw();
  }

  redraw(): void {
    this.clearLabels();
    this.box.clear();
    const x = 8;
    const y = 18;
    const w = LCD_WIDTH - 16;
    const h = LCD_HEIGHT - SOFTKEY_H - y - 4;
    paintCbePanel(this, this.box, x, y, w, h);
    if (!this.equip || !this.openInfo) return;

    if (this.mode === "pickCrystal") {
      this.drawPickCrystal(x, y, w, h);
      drawPanelTipOverlay(this, this.toast);
      return;
    }

    if (this.mode === "replaceConfirm" && this.replaceOffer) {
      this.drawReplaceConfirm(x, y, w, h);
      drawPanelTipOverlay(this, this.toast);
      return;
    }

    const q = qualityColor(this.equip.quality);
    this.addAttr(`洗练 ${this.equip.name}`, x + 8, y + 4, q);
    this.addAttr(
      `强化 +${this.equip.enhance}  选档 +${this.unlock}`,
      x + 8,
      y + 20,
      EQUIP_DETAIL_YELLOW,
    );

    let ly = y + 38;
    for (const t of this.openInfo.tiers) {
      const locked = t.unlock > this.equip.enhance;
      const selected = t.unlock === this.unlock;
      if (selected) {
        this.box.rect(x + 6, ly - 1, w - 12, 14).fill({ color: SELECT_BG });
      }
      this.addAttr(
        this.tierLine(t),
        x + 10,
        ly,
        locked ? EQUIP_DETAIL_GRAY : selected ? INK_CYAN : EQUIP_DETAIL_YELLOW,
      );
      const hit = attachRowPointerHit(this, x + 6, ly - 1, w - 12, 14, () => {
        this.selectTier(t.unlock);
      });
      this.rowHits.push(hit);
      ly += 15;
    }

    const canStack =
      this.unlock === 16 && this.crystalId === ENHANCE_CRYSTAL_LAST;
    const crystalName = !this.crystalId
      ? "未放入玄晶"
      : canStack || this.crystalCount > 1
        ? `${enhanceCrystalName(this.crystalId)} x${this.crystalCount}`
        : enhanceCrystalName(this.crystalId);
    this.addAttr(`玄晶: ${crystalName}`, x + 10, ly + 4, CBE.ink);
    if (this.rate > 0 || this.money > 0) {
      const gold = Math.floor(this.money / 10000);
      this.addAttr(
        `成功率 ${this.rate}%  消耗 ${gold}金`,
        x + 10,
        ly + 20,
        INK_CYAN,
      );
    } else {
      this.addAttr("放入玄晶后可预览", x + 10, ly + 20, EQUIP_DETAIL_GRAY);
    }

    const hint = canStack
      ? "↑↓选档 ←→数量 左软洗练 OK玄晶 右关"
      : this.unlock === 16
        ? "↑↓选档 ←→数量(十六级) 左软洗练 OK玄晶 右关"
        : "↑↓选档 左软洗练 OK玄晶 右键关闭";
    this.addAttr(hint, x + 8, y + h - 16, CBE.inkLabel);
    drawPanelTipOverlay(this, this.toast);
  }

  private drawReplaceConfirm(x: number, y: number, w: number, h: number): void {
    const offer = this.replaceOffer!;
    const q = qualityColor(this.equip!.quality);
    this.addAttr(`洗练结果 +${offer.unlock}`, x + 8, y + 4, q);
    this.addAttr("是否替换属性？", x + 8, y + 22, EQUIP_DETAIL_YELLOW);
    this.addAttr("当前属性", x + 10, y + 44, CBE.inkLabel);
    this.addAttr(
      formatWashAttr(
        offer.oldType,
        offer.oldFlag,
        offer.oldValue,
        offer.oldWashed,
      ),
      x + 10,
      y + 60,
      EQUIP_DETAIL_YELLOW,
    );
    this.addAttr("新属性", x + 10, y + 82, CBE.inkLabel);
    this.addAttr(
      formatWashAttr(offer.newType, offer.newFlag, offer.newValue, 1),
      x + 10,
      y + 98,
      INK_RED,
    );
    this.addAttr(
      "左软替换  右软保留",
      x + 8,
      y + h - 16,
      CBE.inkLabel,
    );
    const hitReplace = attachRowPointerHit(
      this,
      x + 6,
      y + 118,
      (w - 16) / 2,
      18,
      () => this.onAction?.("accept_replace"),
    );
    const hitKeep = attachRowPointerHit(
      this,
      x + 6 + (w - 16) / 2,
      y + 118,
      (w - 16) / 2,
      18,
      () => this.onAction?.("reject_replace"),
    );
    this.rowHits.push(hitReplace, hitKeep);
    this.addAttr("【替换】", x + 14, y + 122, INK_CYAN);
    this.addAttr("【保留】", x + 14 + (w - 16) / 2, y + 122, INK_CYAN);
  }

  private drawPickCrystal(x: number, y: number, w: number, h: number): void {
    const title =
      this.unlock === 16
        ? "选择玄晶(+16可叠3颗十六级)"
        : "选择玄晶(1颗)";
    this.addAttr(title, x + 8, y + 4, EQUIP_DETAIL_YELLOW);
    const rows = this.crystals();
    let ly = y + 24;
    rows.forEach((row, i) => {
      const selected = row.itemId === this.crystalId;
      const cursor = i === this.listCursor;
      if (selected || cursor) {
        this.box.rect(x + 6, ly - 1, w - 12, 14).fill({ color: SELECT_BG });
      }
      const unit = enhanceWashSuccessRate(this.unlock, row.itemId, 1);
      const stackHint =
        this.unlock === 16 && row.itemId === ENHANCE_CRYSTAL_LAST
          ? ` 1颗${unit}%/3颗100%`
          : `  ${unit}%`;
      this.addAttr(
        `${row.name} x${row.count}${stackHint}`,
        x + 10,
        ly,
        selected || cursor ? INK_CYAN : CBE.ink,
      );
      const hit = attachRowPointerHit(this, x + 6, ly - 1, w - 12, 14, () => {
        this.listCursor = i;
        this.mode = "main";
        if (this.applyCrystalPick(row.itemId)) this.onAction?.("back");
        else this.redraw();
      });
      this.rowHits.push(hit);
      ly += 15;
    });
    if (!rows.length) {
      this.addAttr("背包无玄晶", x + 10, ly, CBE.inkWarn);
    }
    this.addAttr(
      "↑↓选择 左软确定 右键返回",
      x + 8,
      y + h - 16,
      CBE.inkLabel,
    );
  }

  handleSoftLeft(): WashAction {
    if (this.mode === "replaceConfirm") {
      return "accept_replace";
    }
    if (this.mode === "pickCrystal") {
      return this.confirmCrystalPick();
    }
    if (!this.crystalId || this.crystalCount <= 0) {
      this.setTip("请放入玄晶");
      return "back";
    }
    if (!this.equip || this.equip.enhance < this.unlock) {
      this.setTip("档次未解锁");
      return "back";
    }
    return "submit";
  }

  handleSoftMid(): WashAction {
    if (this.mode === "replaceConfirm") {
      return "accept_replace";
    }
    if (this.mode === "pickCrystal") {
      return this.confirmCrystalPick();
    }
    this.mode = "pickCrystal";
    const rows = this.crystals();
    const idx = rows.findIndex((r) => r.itemId === this.crystalId);
    this.listCursor = idx >= 0 ? idx : 0;
    this.redraw();
    return "pick_crystal";
  }

  handleSoftRight(): WashAction {
    if (this.mode === "replaceConfirm") {
      return "reject_replace";
    }
    if (this.mode === "pickCrystal") {
      this.mode = "main";
      this.redraw();
      return "back";
    }
    return "cancel";
  }

  handleCancel(): WashAction {
    return this.handleSoftRight();
  }

  getSelectedUnlock(): number {
    return this.unlock;
  }

  getSelectedCrystalId(): number {
    return this.crystalId;
  }

  getSelectedCrystalCount(): number {
    return this.crystalCount > 0 ? this.crystalCount : 0;
  }

  getEquipSeq(): number {
    return this.equip?.seq ?? 0;
  }

  softLeft(): void {
    const a = this.handleSoftLeft();
    if (a === "cancel") this.onClose?.();
    else this.onAction?.(a);
  }

  softMid(): void {
    this.onAction?.(this.handleSoftMid());
  }

  softRight(): void {
    if (this.mode !== "replaceConfirm" &&
        tryDismissPanelTipOnSoftRight(this.toast)) {
      return;
    }
    const a = this.handleSoftRight();
    if (a === "cancel") this.onClose?.();
    else this.onAction?.(a);
  }

  softCancel(): void {
    const a = this.handleCancel();
    if (a === "cancel") this.onClose?.();
    else this.onAction?.(a);
  }
}
