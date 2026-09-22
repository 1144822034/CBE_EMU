import { Container, Graphics, Texture } from "pixi.js";
import type { BattleSide } from "../net/protocol";
import {
  DigitStrip,
  tryLoadUiTexture,
  uiSprite,
} from "./UiAssets";
import { nudgeTextureSource, replaceGraphics } from "../render/gpuResume";
import { getEnemyHpBarPlacement } from "../resources/enemyHpBarPref";

const HP_FILL = 0xe03828;
/** xuetiao.gif inner track — fill draws ABOVE opaque black pixels. */
const BAR_FILL = { x: 5, y: 4, w: 34, h: 3 } as const;
/** Fallback bar size when xuetiao.gif is missing. */
const BAR_W = 44;
const BAR_H = 11;
/** Gap between xuetiao right edge and HP digits. */
const NUM_GAP = 2;
/** Digit sheet height (ui_hpspnum.gif). */
const NUM_H = 5;
/** Fallback digit cell width (ui_hpspnum ≈ 4px). */
const DIGIT_W = 4;
/** Keep the shared plate off the LCD left edge (corner mode). */
const BAR_SHIFT_X = DIGIT_W;
/** One enemy row: bar height (digits sit beside it). */
const SLOT_H = BAR_H;
/** Gap between stacked enemy rows (corner mode). */
const SLOT_GAP = 1;
/** Inset so digits sit inside the plate, not on the map. */
const PLATE_PAD = 2;
/** Gap between sprite top and the bottom of the HP plate. */
const HEAD_GAP = 2;
/** Fallback body height (px) when the unit sprite is not ready yet. */
const HEAD_FALLBACK_H = 36;
/** Same dark teal as the battle title bar. */
const PLATE_FILL = 0x1a332e;
const PLATE_STROKE = 0x3a554e;
const PLATE_ALPHA = 0.5;
/** Max enemy slots painted (matches party cap / formation). */
const ENEMY_MAX = 3;

/**
 * Spar / arena foe uses human `h_*.actor` (or `hW_*`).
 * PvE monsters stay on `e_*` — keep HP digits for those.
 */
function isPlayerEnemy(row: BattleSide): boolean {
  const res = (row.actorResource || "").trim();
  if (res) return /^h/i.test(res);
  return row.visual0 != null && row.visual0 >= 1 && row.visual0 <= 2;
}

export type EnemyHudAnchor = {
  side: BattleSide;
  root: Container;
  hidden: boolean;
  /**
   * Local Y of the body top (negative). Battle sprites are foot-anchored,
   * so this is typically `-sprite.height`.
   */
  headLocalY: number;
};

type SlotArt = {
  root: Container;
  /** Per-slot plate used in follow mode; empty in corner mode. */
  plate: Graphics;
  barHost: Container;
  hpFill: Graphics;
  hpNum: DigitStrip | null;
  lastPlateW: number;
  lastPlateH: number;
};

/**
 * Enemy HP HUD: corner stack (shared plate) or follow each unit (overhead).
 */
export class BattleEnemyHud extends Container {
  private slots: SlotArt[] = [];
  /** Shared plate for corner mode only. */
  private plate = new Graphics();
  private ready = false;
  private xuetiao: Texture | null = null;
  private followMode = false;

  constructor() {
    super();
    this.addChild(this.plate);
  }

  get heightPx(): number {
    return SLOT_H * ENEMY_MAX + SLOT_GAP * (ENEMY_MAX - 1);
  }

  async mount(): Promise<void> {
    if (this.ready) return;
    this.xuetiao = await tryLoadUiTexture("xuetiao.gif");
    for (let i = 0; i < ENEMY_MAX; i++) {
      const slot = await this.makeSlot();
      slot.root.visible = false;
      this.slots.push(slot);
      this.addChild(slot.root);
    }
    this.ready = true;
  }

  /**
   * Refresh bars. Pass `anchors` (index-aligned with members) for follow mode.
   * Do NOT match by side.id — multi-monster packs often share one catalog id.
   */
  setEnemies(members: BattleSide[], anchors: EnemyHudAnchor[] = []): void {
    if (!this.ready) {
      void this.mount().then(() => this.setEnemies(members, anchors));
      return;
    }
    this.followMode = getEnemyHpBarPlacement() === "follow";
    if (this.followMode) {
      this.layoutFollow(members, anchors);
    } else {
      this.layoutCorner(members);
    }
  }

  refresh(members: BattleSide[], anchors: EnemyHudAnchor[] = []): void {
    this.setEnemies(members, anchors);
  }

  clear(): void {
    for (const slot of this.slots) this.detachToHost(slot);
    this.paintSharedPlate(0, 0);
  }

  nudgeAfterGpuResume(
    members: BattleSide[],
    anchors: EnemyHudAnchor[] = [],
  ): void {
    if (!this.ready) return;
    try {
      nudgeTextureSource(this.xuetiao);
    } catch {
      /* ignore */
    }
    for (const slot of this.slots) {
      slot.hpFill = replaceGraphics(slot.hpFill);
      slot.plate = replaceGraphics(slot.plate);
      if (slot.root.children[0] !== slot.plate) {
        slot.root.addChildAt(slot.plate, 0);
      }
    }
    this.plate = replaceGraphics(this.plate);
    if (this.children[0] !== this.plate) this.addChildAt(this.plate, 0);
    this.setEnemies(members, anchors);
  }

  private layoutCorner(members: BattleSide[]): void {
    // Pull any follow-mode reparents back onto this HUD.
    for (const slot of this.slots) this.detachToHost(slot, /* keepVisible */ false);

    const n = Math.min(ENEMY_MAX, members.length);
    let contentW = this.barWidth();
    for (let i = 0; i < ENEMY_MAX; i++) {
      const slot = this.slots[i]!;
      const row = members[i];
      if (!row || i >= n) {
        slot.root.visible = false;
        slot.plate.clear();
        continue;
      }
      slot.root.visible = true;
      slot.root.x = BAR_SHIFT_X;
      slot.root.y = i * (SLOT_H + SLOT_GAP);
      slot.plate.clear();
      slot.barHost.position.set(0, 0);
      contentW = Math.max(contentW, this.paintVitals(slot, row).contentW);
    }
    this.paintSharedPlate(n, contentW);
  }

  private layoutFollow(
    members: BattleSide[],
    anchors: EnemyHudAnchor[],
  ): void {
    this.paintSharedPlate(0, 0);
    const n = Math.min(ENEMY_MAX, members.length);
    for (let i = 0; i < ENEMY_MAX; i++) {
      const slot = this.slots[i]!;
      const row = members[i];
      const anchor = anchors[i];
      if (!row || i >= n || !anchor || anchor.hidden) {
        this.detachToHost(slot);
        continue;
      }
      this.attachToUnit(slot, anchor.root);
      const painted = this.paintVitals(slot, row);
      this.paintSlotPlate(
        slot,
        painted.contentW,
        painted.numsBelow,
        anchor.headLocalY,
      );
    }
  }

  private attachToUnit(slot: SlotArt, unitRoot: Container): void {
    if (slot.root.parent !== unitRoot) {
      if (slot.root.parent) slot.root.parent.removeChild(slot.root);
      unitRoot.addChild(slot.root);
    }
    slot.root.visible = true;
    slot.root.zIndex = 20;
    unitRoot.sortableChildren = true;
  }

  private detachToHost(slot: SlotArt, keepVisible = false): void {
    if (!keepVisible) slot.root.visible = false;
    if (slot.root.parent && slot.root.parent !== this) {
      slot.root.parent.removeChild(slot.root);
      this.addChild(slot.root);
    } else if (!slot.root.parent) {
      this.addChild(slot.root);
    }
    slot.plate.clear();
  }

  private paintSharedPlate(count: number, contentW: number): void {
    if (count <= 0) {
      this.plate.clear();
      this.plate.visible = false;
      return;
    }
    const innerH = count * SLOT_H + (count - 1) * SLOT_GAP;
    const w = contentW + PLATE_PAD * 2;
    const h = innerH + PLATE_PAD * 2;
    this.plate.visible = true;
    this.plate
      .clear()
      .roundRect(BAR_SHIFT_X - PLATE_PAD, -PLATE_PAD, w, h, 3)
      .fill({ color: PLATE_FILL, alpha: PLATE_ALPHA })
      .stroke({ width: 1, color: PLATE_STROKE, alpha: PLATE_ALPHA });
  }

  private paintSlotPlate(
    slot: SlotArt,
    contentW: number,
    numsBelow: boolean,
    headLocalY: number,
  ): void {
    const bw = this.barWidth();
    const rowH = numsBelow ? BAR_H + 1 + NUM_H : SLOT_H;
    const plateW = Math.max(bw, contentW) + PLATE_PAD * 2;
    const plateH = rowH + PLATE_PAD * 2;
    slot.plate
      .clear()
      .roundRect(0, 0, plateW, plateH, 3)
      .fill({ color: PLATE_FILL, alpha: PLATE_ALPHA })
      .stroke({ width: 1, color: PLATE_STROKE, alpha: PLATE_ALPHA });
    slot.barHost.position.set(Math.floor((plateW - bw) / 2), PLATE_PAD);
    slot.lastPlateW = plateW;
    slot.lastPlateH = plateH;
    // Plate bottom sits HEAD_GAP above the measured body top.
    slot.root.position.set(
      -Math.floor(plateW / 2),
      headLocalY - HEAD_GAP - plateH,
    );
  }

  /**
   * Re-seat follow-mode bars to updated sprite tops without repainting fills.
   * Call when pose frames change body height.
   */
  syncFollowHeads(anchors: EnemyHudAnchor[]): void {
    if (!this.followMode || !this.ready) return;
    for (let i = 0; i < ENEMY_MAX; i++) {
      const slot = this.slots[i]!;
      const anchor = anchors[i];
      if (!slot.root.visible || !anchor || anchor.hidden) continue;
      const plateW = slot.lastPlateW || this.barWidth() + PLATE_PAD * 2;
      const plateH = slot.lastPlateH || SLOT_H + PLATE_PAD * 2;
      slot.root.position.set(
        -Math.floor(plateW / 2),
        anchor.headLocalY - HEAD_GAP - plateH,
      );
    }
  }

  private barWidth(): number {
    return this.xuetiao?.width || BAR_W;
  }

  private barHeight(): number {
    return this.xuetiao?.height || BAR_H;
  }

  /**
   * Paint bar + digits.
   * Corner: digits to the right of the bar.
   * Follow: digits centered under the bar.
   */
  private paintVitals(
    slot: SlotArt,
    row: BattleSide,
  ): { contentW: number; numsBelow: boolean } {
    const maxHp = Math.max(1, row.maxHp || row.hp || 1);
    const hp = Math.max(0, Math.min(maxHp, row.hp));
    const hpW = Math.max(0, Math.floor((BAR_FILL.w * hp) / maxHp));
    slot.hpFill
      .clear()
      .rect(BAR_FILL.x, BAR_FILL.y, hpW, BAR_FILL.h)
      .fill({ color: HP_FILL });
    const bw = this.barWidth();
    const showNums = !isPlayerEnemy(row);
    if (!slot.hpNum) return { contentW: bw, numsBelow: false };
    slot.hpNum.root.visible = showNums;
    if (!showNums) return { contentW: bw, numsBelow: false };
    const text = `${hp}/${maxHp}`;
    slot.hpNum.setText(text);
    const bh = this.barHeight();
    const cell = slot.hpNum.cellW || DIGIT_W;
    const tw = text.length * cell;
    if (this.followMode) {
      // Center under xuetiao; never hang left of the bar.
      const nx = Math.max(0, Math.floor((bw - tw) / 2));
      slot.hpNum.root.position.set(nx, bh + 1);
      return { contentW: Math.max(bw, nx + tw), numsBelow: true };
    }
    const ny = Math.max(0, Math.floor((bh - NUM_H) / 2));
    slot.hpNum.root.position.set(bw + NUM_GAP, ny);
    return { contentW: bw + NUM_GAP + tw, numsBelow: false };
  }

  private async makeSlot(): Promise<SlotArt> {
    const root = new Container();
    const plate = new Graphics();
    root.addChild(plate);

    const barHost = new Container();
    const fill = new Graphics();
    if (this.xuetiao) {
      barHost.addChild(uiSprite(this.xuetiao));
    } else {
      barHost.addChild(
        new Graphics()
          .roundRect(0, 0, BAR_W, BAR_H, 2)
          .fill({ color: 0x101010 })
          .stroke({ width: 1, color: 0xd4c489 }),
      );
    }
    barHost.addChild(fill);

    const hpNum =
      (await DigitStrip.battleHpspNum()) || (await DigitStrip.hpspNum());
    if (hpNum) {
      hpNum.setText("0/0");
      barHost.addChild(hpNum.root);
    }
    root.addChild(barHost);

    return {
      root,
      plate,
      barHost,
      hpFill: fill,
      hpNum,
      lastPlateW: 0,
      lastPlateH: 0,
    };
  }
}
