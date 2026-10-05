import { Container, Graphics, Texture } from "pixi.js";
import type { BattleSide } from "../net/protocol";
import {
  DigitStrip,
  tryLoadUiTexture,
  uiSprite,
} from "./UiAssets";
import {
  BitmapLabel,
  UC3_CELL,
  measureUiTextWidth,
} from "./BitmapLabel";
import {
  applyBattleHpspDigits,
  BATTLE_HPSP_NUM_SIZE,
  hideBattleHpspDigits,
} from "./BattlePartyHud";
import { nudgeTextureSource, replaceGraphics } from "../render/gpuResume";
import { getEnemyHpBarPlacement } from "../resources/enemyHpBarPref";
import { preferSystemFont } from "../resources/systemFont";

const HP_FILL = 0xe03828;
/** Match party HUD leader name ink (PvP foe is a player). */
const NAME_PINK = 0xff40c8;
/** xuetiao.gif inner track — fill draws ABOVE opaque black pixels. */
const BAR_FILL = { x: 5, y: 4, w: 34, h: 3 } as const;
/** Fallback bar size when xuetiao.gif is missing. */
const BAR_W = 44;
const BAR_H = 11;
/** Gap between xuetiao right edge and HP digits / PvP name. */
const NUM_GAP = 2;
/** Digit sheet height (ui_hpspnum.gif). */
const NUM_H = 5;
/** Fallback digit cell width (ui_hpspnum ≈ 4px). */
const DIGIT_W = 4;
/** Visual name size (UC3 scaled); same as BattlePartyHud. */
const NAME_SIZE = 10;
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
/** Cap like party HUD — LCD strip is narrow. */
const NAME_CHARS = 4;

function enemyDisplayName(row: BattleSide): string {
  return (row.name || "")
    .replace(/\0+$/g, "")
    .replace(/#\d+$/, "")
    .trim()
    .slice(0, NAME_CHARS);
}

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
  hpNumLab: BitmapLabel;
  /** PvP foe name (hidden for PvE monsters). */
  name: BitmapLabel;
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
        painted.numH,
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
    numH: number,
    headLocalY: number,
  ): void {
    const bw = this.barWidth();
    const rowH = numsBelow ? BAR_H + 1 + numH : SLOT_H;
    const plateW = Math.max(bw, contentW) + PLATE_PAD * 2;
    const plateH = rowH + PLATE_PAD * 2;
    slot.plate
      .clear()
      .roundRect(0, 0, plateW, plateH, 3)
      .fill({ color: PLATE_FILL, alpha: PLATE_ALPHA })
      .stroke({ width: 1, color: PLATE_STROKE, alpha: PLATE_ALPHA });
    // Digits sit under the bar (anchor on bar width). Name/digits beside the
    // bar widen contentW — center the whole barHost group.
    const anchorW = numsBelow ? bw : Math.max(bw, contentW);
    slot.barHost.position.set(Math.floor((plateW - anchorW) / 2), PLATE_PAD);
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
   * Paint bar + digits (PvE) or player name (PvP).
   * Corner: digits / name to the right of the bar.
   * Follow: digits centered under the bar; PvP name still beside the bar.
   */
  private paintVitals(
    slot: SlotArt,
    row: BattleSide,
  ): { contentW: number; numsBelow: boolean; numH: number } {
    const maxHp = Math.max(1, row.maxHp || row.hp || 1);
    const hp = Math.max(0, Math.min(maxHp, row.hp));
    const hpW = Math.max(0, Math.floor((BAR_FILL.w * hp) / maxHp));
    slot.hpFill
      .clear()
      .rect(BAR_FILL.x, BAR_FILL.y, hpW, BAR_FILL.h)
      .fill({ color: HP_FILL });
    const bw = this.barWidth();
    const bh = this.barHeight();
    const player = isPlayerEnemy(row);
    const showNums = !player;
    const numH = preferSystemFont() ? BATTLE_HPSP_NUM_SIZE : NUM_H;
    if (!showNums) hideBattleHpspDigits(slot.hpNum, slot.hpNumLab);
    slot.name.visible = false;

    if (player) {
      const label = enemyDisplayName(row);
      if (!label) return { contentW: bw, numsBelow: false, numH };
      slot.name.visible = true;
      slot.name.scale.set(NAME_SIZE / UC3_CELL);
      void slot.name.setText(label, NAME_PINK, {
        smooth: false,
        outline: false,
      });
      const tw = Math.ceil(
        measureUiTextWidth(label, UC3_CELL) * (NAME_SIZE / UC3_CELL),
      );
      const ny = Math.max(0, Math.floor((bh - NAME_SIZE) / 2));
      slot.name.position.set(bw + NUM_GAP, ny);
      return { contentW: bw + NUM_GAP + tw, numsBelow: false, numH };
    }

    if (!showNums) return { contentW: bw, numsBelow: false, numH };
    const text = `${hp}/${maxHp}`;
    const painted = applyBattleHpspDigits(slot.hpNum, slot.hpNumLab, text);
    const tw = painted.w;
    const h = painted.h;
    if (this.followMode) {
      const nx = Math.max(0, Math.floor((bw - tw) / 2));
      const ny = bh + 1;
      if (slot.hpNum) slot.hpNum.root.position.set(nx, ny);
      slot.hpNumLab.position.set(nx, ny);
      return { contentW: Math.max(bw, nx + tw), numsBelow: true, numH: h };
    }
    const ny = Math.max(0, Math.floor((bh - h) / 2));
    if (slot.hpNum) slot.hpNum.root.position.set(bw + NUM_GAP, ny);
    slot.hpNumLab.position.set(bw + NUM_GAP, ny);
    return { contentW: bw + NUM_GAP + tw, numsBelow: false, numH: h };
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

    const hpNumLab = new BitmapLabel();
    hpNumLab.visible = false;
    barHost.addChild(hpNumLab);

    const name = new BitmapLabel();
    name.scale.set(NAME_SIZE / UC3_CELL);
    name.visible = false;
    barHost.addChild(name);

    root.addChild(barHost);

    return {
      root,
      plate,
      barHost,
      hpFill: fill,
      hpNum,
      hpNumLab,
      name,
      lastPlateW: 0,
      lastPlateH: 0,
    };
  }
}
