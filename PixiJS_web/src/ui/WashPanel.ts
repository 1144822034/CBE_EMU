/**
 * PixiJS-only equipment enhance wash (洗练) panel.
 * Protocol: WT 29/7 open, 29/8 preview, 29/9 confirm.
 *
 * Controls: ↑↓ select tier · left soft=wash · D-pad OK=crystal · right=close
 * (No HTML center softkey above the D-pad.)
 */
import { Container, Graphics } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  drawCbePanel,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type { BagItem } from "../net/protocolExtras";
import {
  ENHANCE_CRYSTAL_FIRST,
  enhanceCrystalName,
  enhanceCrystalUnitRate,
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
  | "back";

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

type Mode = "main" | "pickCrystal";

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
  private rate = 0;
  private money = 0;
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
    // 不设 center：避免方向键上方多出中软按钮；玄晶走 D-pad 中键/OK。
    return { left: "洗练", right: "关闭" };
  }

  openWash(
    equip: BagItem,
    bag: BagItem[],
    info: EnhanceWashOpenResult,
    opts?: { keepUnlock?: number; keepCrystalId?: number },
  ): void {
    const prevUnlock = opts?.keepUnlock ?? 0;
    const prevCrystal = opts?.keepCrystalId ?? 0;
    this.equip = equip;
    this.bag = bag;
    this.openInfo = info;
    this.mode = "main";
    this.rate = 0;
    this.money = 0;
    const unlocked = this.unlockedTiers();
    if (prevUnlock && unlocked.includes(prevUnlock)) this.unlock = prevUnlock;
    else this.unlock = unlocked[0] ?? 4;
    const crystals = this.crystals();
    if (prevCrystal && crystals.some((c) => c.itemId === prevCrystal))
      this.crystalId = prevCrystal;
    else this.crystalId = 0;
    this.listCursor = 0;
    this.visible = true;
    this.redraw();
  }

  setBag(bag: BagItem[]): void {
    this.bag = bag;
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
    clearPanelTip(this.toast);
    this.clearLabels();
  }

  /** ↑↓：主界面选档；选玄晶界面移动光标。 */
  move(delta: number): void {
    if (!this.visible) return;
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
    this.rate = 0;
    this.money = 0;
    this.redraw();
    this.onAction?.("back");
  }

  private unlockedTiers(): number[] {
    const enhance = this.equip?.enhance ?? 0;
    return UNLOCKS.filter((u) => u <= enhance);
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
    this.rate = 0;
    this.money = 0;
    this.redraw();
    this.onAction?.("back");
  }

  private confirmCrystalPick(): WashAction {
    const rows = this.crystals();
    const row = rows[this.listCursor];
    if (!row) {
      this.setTip("背包无玄晶");
      return "back";
    }
    this.crystalId = row.itemId;
    this.mode = "main";
    this.rate = 0;
    this.money = 0;
    this.redraw();
    return "back";
  }

  redraw(): void {
    this.clearLabels();
    this.box.clear();
    const x = 8;
    const y = 18;
    const w = LCD_WIDTH - 16;
    const h = LCD_HEIGHT - SOFTKEY_H - y - 4;
    drawCbePanel(this.box, x, y, w, h);
    if (!this.equip || !this.openInfo) return;

    if (this.mode === "pickCrystal") {
      this.drawPickCrystal(x, y, w);
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

    const crystalName = this.crystalId
      ? enhanceCrystalName(this.crystalId)
      : "未放入玄晶";
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

    this.addAttr("↑↓选档 左软洗练 OK玄晶 右键关闭", x + 8, y + h - 16, CBE.inkLabel);
    drawPanelTipOverlay(this, this.toast);
  }

  private drawPickCrystal(x: number, y: number, w: number): void {
    this.addAttr("选择玄晶(1颗)", x + 8, y + 4, EQUIP_DETAIL_YELLOW);
    const rows = this.crystals();
    let ly = y + 24;
    rows.forEach((row, i) => {
      const selected = row.itemId === this.crystalId;
      const cursor = i === this.listCursor;
      if (selected || cursor) {
        this.box.rect(x + 6, ly - 1, w - 12, 14).fill({ color: SELECT_BG });
      }
      const unit = enhanceCrystalUnitRate(this.unlock, row.tier);
      this.addAttr(
        `${row.name} x${row.count}  ${unit}%`,
        x + 10,
        ly,
        selected || cursor ? INK_CYAN : CBE.ink,
      );
      const hit = attachRowPointerHit(this, x + 6, ly - 1, w - 12, 14, () => {
        this.listCursor = i;
        this.crystalId = row.itemId;
        this.mode = "main";
        this.rate = 0;
        this.money = 0;
        this.redraw();
        this.onAction?.("back");
      });
      this.rowHits.push(hit);
      ly += 15;
    });
    if (!rows.length) {
      this.addAttr("背包无玄晶", x + 10, ly, CBE.inkWarn);
    }
    this.addAttr("↑↓选择 左软确定 右键返回", x + 8, y + 24 + Math.max(rows.length, 1) * 15 + 8, CBE.inkLabel);
  }

  handleSoftLeft(): WashAction {
    if (this.mode === "pickCrystal") {
      return this.confirmCrystalPick();
    }
    if (!this.crystalId) {
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
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
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
