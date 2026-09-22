import { Container, Graphics, Rectangle, Sprite, Texture } from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  paintCbePanel,
  drawCbeTitlePill,
} from "./constants";
import { BitmapLabel, UC3_CELL, measureUiTextWidth, wrapChars, wrapUiText } from "./BitmapLabel";
import { tryLoadUiTexture, uiSprite } from "./UiAssets";
import { rgbaToTexture } from "../render/rgbaTexture";
import {
  EQUIP_ICON_SIZE,
  EQUIP_PART_LABEL_MAX_W,
  SLOT_FRAME_SIZE,
  equipIconTextureSync,
  equipPartLabelTextureSync,
  mountEquipIconOnly,
  mountEquipSlot,
  slotFrameTextureSync,
  warmEquipArt,
} from "./ItemArt";
import {
  canCombineCrystalFragment,
  canCombineDreamBadge,
  canCombineEnhanceCrystal,
  canCombineGem,
  canBatchCombineGem,
  canCombineGemFragment,
  canExchangeGemType,
  isGemFragment,
  parseGemItem,
  formatCultivationBookName,
  isChestBoxItem,
  isCrystalFragment,
  isCultivationBookItem,
  isDreamBadge,
  isEnhanceCrystal,
  isCrystalExtractStone,
  isReservoirFlaskItemId,
  maxFlaskBuyUnitsFromBag,
  type BagItem,
  type CharacterStats,
  type MallState,
  type ShopGoods,
} from "../net/protocolExtras";
import { isMapHealSkill, type BattleSkillRow } from "../resources/skill";
import {
  defaultCategoryForBodySlot,
  jobAllowsWeaponCategory,
  lookupItemSync,
  qualityColor,
  WORN_SLOT_NAMES,
  BACKPACK_EXPAND_ITEM_ID,
  maxBackpackExpandBuyUnits,
} from "../resources/itemCatalog";
import {
  actorResourceForJobSex,
  composeMapActor,
  loadActor,
} from "../resources/actor";
import {
  buildEquipCompareMarks,
  buildEquipDetailLines,
  EQUIP_DETAIL_YELLOW,
  equipHasWashedAttr,
  equipKvAttrName,
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
import { attachPageNavButtons, preloadPageNavTextures } from "./pageNav";

export type SystemKind =
  | "bag"
  | "equip"
  | "quest"
  | "mall"
  | "enhance"
  | "team"
  | "skill"
  | "status";

type ViewMode =
  | "list"
  | "detail"
  | "actions"
  | "recharge_amount"
  | "recharge_pay";

export type SystemAction =
  | "use"
  | "combine"
  | "batch_open"
  | "equip"
  | "unequip"
  | "swap"
  | "enhance"
  | "recast"
  | "wash"
  | "extract"
  | "discard"
  | "auction"
  | "gem_embed"
  | "gem_remove"
  | "gem_exchange"
  | "gem_from_crystal"
  | "gem_batch_combine"
  | "buy"
  | "recharge_alipay"
  | "recharge_wechat"
  | "skill_use"
  | "skill_forget"
  | "gem_embed"
  | "gem_remove"
  | "gem_exchange"
  | "gem_crystal16"
  | "refresh";

/**
 * Worn panel visual grid (screenshot)：左帽衣披束 / 右裤鞋饰品武器.
 * slot = body slot index (0 武器 … 7 戒指).
 * Array index i → JHOnlineData `{i}.gif` part label (帽…武器).
 */
const EQUIP_SLOT_UI: { slot: number; label: string; col: number; row: number }[] = [
  { slot: 1, label: "帽", col: 0, row: 0 },
  { slot: 2, label: "衣", col: 0, row: 1 },
  { slot: 3, label: "披", col: 0, row: 2 },
  { slot: 4, label: "束", col: 0, row: 3 },
  { slot: 5, label: "裤", col: 1, row: 0 },
  { slot: 6, label: "鞋", col: 1, row: 1 },
  { slot: 7, label: "饰品", col: 1, row: 2 },
  { slot: 0, label: "武器", col: 1, row: 3 },
];

/** Gap between handkuang slot and inward part label (`0.gif`..`7.gif`). */
const EQUIP_PART_LABEL_GAP = 2;

const EQUIP_REPLACE_PART: Record<number, string> = {
  0: "手部",
  1: "头部",
  2: "上衣",
  3: "披风",
  4: "腰部",
  5: "腿部",
  6: "脚部",
  7: "饰品",
};

const EQUIP_CURSOR = 0xffdf00;
/** Worn slot glow when server durability is 0. */
const EQUIP_BROKEN_RED = 0xff2020;
const EQUIP_BROKEN_RED_HI = 0xff6060;

/** Rows visible per page on bag list (CBE 240×400). */
const BAG_VISIBLE_ROWS = 13;
/** Rows visible per page on mall / other generic lists. */
const PAGE_SIZE = 10;
/** Bottom inset only — L/R actions live on HTML softkeys, not in-panel buttons. */
const PANEL_BOTTOM_PAD = 6;

type BagTab = {
  id: string;
  /** Full label when selected (CBE). */
  label: string;
  /** Single-char clip when inactive (screenshot 道/务/料/物/宝). */
  short: string;
  match: (it: BagItem) => boolean;
};

type MallTab = {
  id: string;
  label: string;
  match: (g: ShopGoods) => boolean;
};

/** CBE bag tabs @0x010250F2..0x0102511A：道具/装备/任务/材料/杂物/秘宝. */
const BAG_TABS: BagTab[] = [
  {
    id: "daoju",
    label: "道具",
    short: "道",
    match: (it) => it.kind === "item" && (it.category === 10 || it.category === 23),
  },
  { id: "equip", label: "装备", short: "装", match: (it) => it.kind === "equip" },
  {
    id: "quest",
    label: "任务",
    short: "务",
    match: (it) => it.kind === "item" && it.category === 11,
  },
  {
    id: "mat",
    label: "材料",
    short: "料",
    match: (it) => it.kind === "item" && (it.category === 12 || it.category === 13),
  },
  {
    id: "misc",
    label: "杂物",
    short: "物",
    match: (it) => {
      if (it.kind === "equip") return false;
      const c = it.category;
      return !(
        c === 10 ||
        c === 11 ||
        c === 12 ||
        c === 13 ||
        c === 14 ||
        c === 20 ||
        c === 21 ||
        c === 23
      );
    },
  },
  {
    id: "secret",
    label: "秘宝",
    short: "宝",
    match: (it) =>
      it.kind === "item" && (it.category === 14 || it.category === 20 || it.category === 21),
  },
];

const MALL_TABS: MallTab[] = [
  { id: "all", label: "全部", match: () => true },
  { id: "drug", label: "药品", match: (g) => g.kind === "item" && g.category === 10 },
  {
    id: "secret",
    label: "秘宝",
    match: (g) =>
      g.kind === "item" &&
      (g.category === 14 || g.category === 20 || g.category === 21),
  },
  {
    id: "other",
    label: "其他",
    match: (g) => {
      if (g.kind === "equip") return false;
      const c = g.category;
      return c !== 10 && c !== 14 && c !== 20 && c !== 21;
    },
  },
];

/** Visible action buttons on equip detail footer. */
const EQUIP_ACTION_VISIBLE = 4;
/** Selection bar on bag list (sampled from CBE shot). */
const BAG_SELECT = 0x20676e;
const BAG_TAB_BG = 0x375946;
/** CBE capacity `%d/%d个` color 0xFFDF00. */
const BAG_CAP_INK = 0xffdf00;
/** CBE bag money amount DrawString color. */
const BAG_MONEY_INK = 0xffffff;

/** Rows visible on CBE 属性 panel (through 物攻 on first page). */
const STATUS_VISIBLE_ROWS = 15;
/** Value / row well (dark). */
const STATUS_ROW_BG = 0x12231e;
/** Label column strip — lighter than value well. */
const STATUS_LABEL_BG = 0x3a5c52;
/** Label mint; values stay white. */
const STATUS_LABEL = 0xabcc98;
/** Value column after 2 字标签（紧凑）. */
/** Two UC3 CJK glyphs (32) + pad — was 40 for 12px system font. */
const STATUS_LABEL_W = 48;

/**
 * CBE-style system overlays: bag / worn / skills / mall / character.
 * All list data is server-driven; local DSH only supplies names/quality paint.
 */
export class SystemPanel extends Container {
  /** Full-LCD dim behind chrome — must remount after GL restore like `box`. */
  private dim = new Graphics();
  private box = new Graphics();
  private labels: BitmapLabel[] = [];
  /** Transient UI sprites (jin/yin/tong); cleared each redraw. */
  private uiSprites: Container[] = [];
  private kind: SystemKind = "bag";
  private mode: ViewMode = "list";
  private cursorIndex = 0;
  private actionCursor = 0;
  private categoryIndex = 0;

  private bagItems: BagItem[] = [];
  private wornItems: BagItem[] = [];
  private skills: BattleSkillRow[] = [];
  private mall: MallState | null = null;
  private stats: CharacterStats | null = null;
  /** Copper total for bag footer 金/银/铜 (CBE money split). */
  private bagMoney = 0;
  /** Actor backpack capacity (default 20). */
  private bagCapacity = 20;
  /** handkuang + equipico warmed for worn panel. */
  private equipArtReady = false;
  private equipArtLoading = false;
  /** Cached JHOnlineData currency icons. */
  private moneyTex: { jin: Texture | null; yin: Texture | null; tong: Texture | null } | null =
    null;
  private moneyTexLoading = false;
  private selected: BagItem | ShopGoods | BattleSkillRow | null = null;
  private actionLabels: string[] = [];
  private detailLines: string[] = [];
  private inBattle = false;
  /** Equip detail attribute page (CBE ⇦ n/m ⇨). */
  private detailPage = 0;
  /** Mall detail purchase quantity (← → adjust). */
  private buyQty = 1;
  /** Pending mall RMB recharge yuan (before channel pick). */
  private rechargeYuan = 0;
  /** Native LCD keypad draft for mall 赞助 (no HTML prompt). */
  private rechargeDraft = "1";
  /** Transient hit targets (mall footer 充值); destroyed each redraw. */
  private hitSprites: Container[] = [];
  /** Selectable row pointer hits (list / actions); cleared each redraw. */
  private rowHits: Container[] = [];
  /** Body slot under equip cursor (0..7). */
  private equipFocusSlot = 1;
  /** Bag candidates when replacing a worn slot. */
  private replaceCandidates: BagItem[] = [];
  /** Cached stand-frame for worn-panel center actor. */
  private equipActorTex: Texture | null = null;
  private equipActorKey = "";
  /** Last job/sex passed to setEquipActor — kept across hide/GPU strip for reload. */
  private equipActorJob = 0;
  private equipActorSex = 1;
  private equipActorConfigured = false;
  /** Transient server feedback (item use / chest reward). */
  private toast: PanelTipState = createPanelTipState();
  /** Pulsing red glow over durability-0 worn slots (equip panel only). */
  private brokenGlowGfx = new Graphics();
  private brokenSlotRects: { x: number; y: number; w: number; h: number }[] = [];
  private brokenGlowPhase = 0;
  private brokenGlowTimer: ReturnType<typeof setInterval> | null = null;

  onAction:
    | ((
        kind: SystemKind,
        action: SystemAction | string,
        item?: BagItem | ShopGoods | BattleSkillRow,
      ) => void)
    | null = null;
  onClose: (() => void) | null = null;
  /**
   * Optional enrich for bag item detail text (e.g. 921 修炼天书 → 7/38 bookdes).
   * Return non-empty string to replace `it.desc` before drawing.
   */
  onEnrichBagDetail: ((it: BagItem) => Promise<string | null>) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.paintDim();
    this.addChild(this.dim, this.box, this.brokenGlowGfx);
    preloadPageNavTextures(() => {
      if (this.visible) this.redraw();
    });
  }

  private paintDim(): void {
    this.dim
      .clear()
      .rect(0, 0, LCD_WIDTH, LCD_HEIGHT - SOFTKEY_H)
      .fill({ color: 0x000000, alpha: 0.35 });
  }

  /**
   * Remount long-lived Graphics. Required after GL restore even while hidden —
   * open() otherwise paints 属性/背包 chrome onto dead GPU geometry.
   */
  private remountChrome(): void {
    this.dim = replaceGraphics(this.dim);
    this.paintDim();
    this.box = replaceGraphics(this.box);
    this.brokenGlowGfx = replaceGraphics(this.brokenGlowGfx);
    if (this.toast.gfx.parent) {
      this.toast.gfx = replaceGraphics(this.toast.gfx);
    }
  }

  /** webglcontextrestored: remount panel Graphics; redraw only if open. */
  nudgeAfterGpuResume(): void {
    this.remountChrome();
    // invalidateUiGpuTextures already destroyed these GPU sources.
    this.moneyTex = null;
    this.moneyTexLoading = false;
    this.equipArtReady = false;
    this.equipArtLoading = false;
    if (this.equipActorTex) {
      try {
        this.equipActorTex.destroy(true);
      } catch {
        /* ignore */
      }
      this.equipActorTex = null;
      this.equipActorKey = "";
    }
    if (!this.visible) return;
    if (this.kind === "bag") this.ensureMoneyIcons();
    if (this.kind === "equip") {
      this.ensureEquipArt();
      // Equip-op redraws can trip alphaMode strip; reload stand here — do not
      // wait for the next openSystem("equip") which is the only main caller.
      this.ensureEquipActorTex();
    }
    this.redraw();
  }

  /** True when the cached center-stand texture is still GPU-usable. */
  private equipActorTexAlive(): boolean {
    const tex = this.equipActorTex;
    if (!tex || tex.destroyed) return false;
    try {
      const src = tex.source;
      if (!src || src.destroyed) return false;
    } catch {
      return false;
    }
    return true;
  }

  /**
   * Reload center stand when tex was cleared (GPU strip / hide) but job/sex
   * is still known. Safe no-op before the first setEquipActor.
   */
  private ensureEquipActorTex(): void {
    if (!this.equipActorConfigured) return;
    if (this.equipActorTexAlive()) return;
    this.equipActorKey = "";
    this.setEquipActor(this.equipActorJob, this.equipActorSex);
  }

  open(kind: SystemKind, _hint = ""): void {
    this.kind = kind;
    this.mode = kind === "status" ? "detail" : "list";
    this.cursorIndex = 0;
    this.actionCursor = 0;
    this.detailPage = 0;
    this.buyQty = 1;
    this.rechargeYuan = 0;
    this.rechargeDraft = "1";
    this.replaceCandidates = [];
    // Bag defaults to 道具; enhance picker still opens on 装备.
    this.categoryIndex = kind === "enhance" ? 1 : 0;
    this.selected = null;
    this.clearTip();
    // Open-after-recover: Graphics may have died while this panel was hidden.
    this.remountChrome();
    this.visible = true;
    if (kind === "bag") this.ensureMoneyIcons();
    if (kind === "equip") {
      // Default focus: 帽 (slot 1), UI index 0.
      this.equipFocusSlot = 1;
      this.cursorIndex = 0;
    } else {
      this.stopBrokenGlowAnim();
    }
    this.redraw();
  }

  /** Current mall buy quantity (for onAction buy). */
  getMallBuyQty(): number {
    return Math.max(1, this.buyQty | 0);
  }

  /** Yuan amount chosen before 支付宝/微信 confirm. */
  getRechargeYuan(): number {
    return Math.max(0, this.rechargeYuan | 0);
  }

  /** Worn body seq for 7/8 type4 / 7/9 body (= slot+1). */
  getEquipFocusBodySeq(): number {
    return (this.equipFocusSlot | 0) + 1;
  }

  getEquipFocusSlot(): number {
    return this.equipFocusSlot | 0;
  }

  /** Full-body stand for worn-panel center (job/sex from selected role). */
  setEquipActor(jobIndex: number, sex: number): void {
    this.equipActorConfigured = true;
    this.equipActorJob = jobIndex | 0;
    this.equipActorSex = sex | 0;
    const name = actorResourceForJobSex(jobIndex, sex);
    const cacheKey = `${name}#d3`;
    if (cacheKey === this.equipActorKey && this.equipActorTexAlive()) return;
    this.equipActorKey = cacheKey;
    const prevTex = this.equipActorTex;
    this.equipActorTex = null;
    if (prevTex) {
      try {
        prevTex.destroy(true);
      } catch {
        /* ignore */
      }
    }
    void loadActor(name)
      .then((loaded) => {
        if (this.equipActorKey !== cacheKey) return;
        // CBE dir: 1=up 2=right 3=down(front) 4=left — equip panel wants front stand.
        const frame = composeMapActor(loaded, 3, false, 0);
        if (!frame || frame.width < 4) return;
        // rgbaToTexture — Texture.from(canvas) has caused CanvasSource.alphaMode
        // storms when the worn panel remounts under WebGL pressure.
        const tex = rgbaToTexture(
          frame.width,
          frame.height,
          frame.rgba,
          "nearest",
        );
        this.equipActorTex = tex;
        if (this.visible && this.kind === "equip") this.redraw();
      })
      .catch((e) => console.warn("[system] equip actor", name, e));
  }

  /** Show a short in-panel tip (item use / 获得奖励). */
  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    // Item operation panel already covers the LCD; generic success overlaps it.
    if (this.mode === "actions" && text.trim() === "使用成功") return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) this.redraw();
    }, ms);
  }

  private clearTip(): void {
    clearPanelTip(this.toast);
  }

  setInBattle(v: boolean): void {
    this.inBattle = v;
  }

  setBag(items: BagItem[]): void {
    this.bagItems = items;
    // Keep detail/actions on the live bag row (enhanceAttrs / wash markers).
    if (this.selected && isBag(this.selected)) {
      const sel = this.selected;
      if (sel.seq > 0) {
        const fresh = items.find((it) => it.seq === sel.seq);
        if (fresh) this.selected = fresh;
      }
    }
    if (this.visible && (this.kind === "bag" || this.kind === "enhance")) {
      if (this.mode === "list") this.clampCursor();
      this.redraw();
      return;
    }
    if (this.visible && this.kind === "equip") {
      if (this.mode === "detail") {
        const slot = this.equipFocusSlot;
        this.replaceCandidates = this.filterJobCompatibleEquips(
          this.bagItems.filter(
            (it) => it.kind === "equip" && it.equipSlot === slot,
          ),
        );
        if (!this.replaceCandidates.length) {
          this.replaceCandidates = this.filterJobCompatibleEquips(
            this.bagItems.filter((it) => it.kind === "equip"),
          );
        }
        if (this.cursorIndex >= this.replaceCandidates.length) this.cursorIndex = 0;
        this.selected = this.replaceCandidates[this.cursorIndex] ?? null;
      }
      this.redraw();
      return;
    }
    // 商城买壶：背包储量刷新后夹住可购数量提示。
    if (
      this.visible &&
      this.kind === "mall" &&
      this.mode === "actions" &&
      this.selected &&
      isGoods(this.selected) &&
      isReservoirFlaskItemId(this.selected.itemId)
    ) {
      this.redraw();
    }
  }

  /** Money (copper) + capacity for CBE bag chrome `%d/%d个` + 金/银/铜. */
  setBagMeta(money: number, capacity: number): void {
    this.bagMoney = money >>> 0;
    this.bagCapacity = Math.max(1, capacity | 0);
    if (this.visible && this.kind === "bag") this.redraw();
    // Mall 806 qty max depends on live capacity after multi-buy expand.
    if (
      this.visible &&
      this.kind === "mall" &&
      this.mode === "actions" &&
      this.selected &&
      isGoods(this.selected) &&
      (this.selected.itemId >>> 0) === BACKPACK_EXPAND_ITEM_ID
    ) {
      this.redraw();
    }
  }

  setWorn(items: BagItem[]): void {
    this.wornItems = items;
    if (!this.visible) return;
    if (this.kind === "equip") {
      if (this.mode === "list" || this.mode === "actions") {
        this.selected = this.wornBySlot(this.equipFocusSlot);
      }
      this.redraw();
      return;
    }
    // Bag equip detail needs worn snapshot for same-slot compare.
    if (
      this.kind === "bag" &&
      this.mode === "actions" &&
      this.selected &&
      isBag(this.selected) &&
      this.selected.kind === "equip"
    ) {
      this.redraw();
    }
  }

  setSkills(skills: BattleSkillRow[]): void {
    this.skills = skills;
    if (this.visible && this.kind === "skill") this.redraw();
  }

  setMall(state: MallState): void {
    this.mall = state;
    if (this.visible && this.kind === "mall") {
      if (this.mode === "list") this.clampCursor();
      this.redraw();
    }
  }

  setStats(stats: CharacterStats): void {
    const prevMax = this.stats?.maxGemLevel ?? 0;
    const prevCrystal =
      this.stats?.crystal16ExchangeEnabled ?? 0;
    this.stats = stats;
    if (this.visible && this.kind === "status") this.redraw();
    else if (
      this.visible &&
      this.kind === "bag" &&
      this.mode === "list" &&
      ((stats.maxGemLevel || 0) !== prevMax ||
        (stats.crystal16ExchangeEnabled ? 1 : 0) !== prevCrystal)
    ) {
      this.redraw();
    }
  }

  setLines(lines: string[]): void {
    this.detailLines = lines;
    this.redraw();
  }

  hide(): void {
    this.clearTip();
    this.visible = false;
    this.stopBrokenGlowAnim();
    this.brokenGlowGfx.clear();
    this.brokenSlotRects = [];
    // Unpin panel UC3 / UI sprites while hidden (VRAM); open() redraws fresh.
    for (const t of this.labels) t.destroy({ children: true });
    this.labels = [];
    for (const s of this.uiSprites) s.destroy({ children: true });
    this.uiSprites = [];
    for (const h of this.hitSprites) h.destroy({ children: true });
    this.hitSprites = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    if (this.equipActorTex) {
      try {
        this.equipActorTex.destroy(true);
      } catch {
        /* ignore */
      }
      this.equipActorTex = null;
      this.equipActorKey = "";
    }
  }

  /** Left/right: bag/mall category tab, equip-detail page, or mall buy qty. */
  moveCategory(delta: number): void {
    if (this.kind === "equip" && this.mode === "list") {
      this.moveEquipGrid(0, delta);
      return;
    }
    if (
      this.kind === "mall" &&
      this.mode === "recharge_amount"
    ) {
      this.adjustRechargeDraft(delta);
      return;
    }
    if (
      this.kind === "mall" &&
      this.mode === "actions" &&
      this.selected &&
      isGoods(this.selected)
    ) {
      this.adjustBuyQty(delta);
      return;
    }
    if (
      this.kind === "bag" &&
      this.mode === "actions" &&
      this.selected &&
      isBag(this.selected) &&
      this.selected.kind === "equip"
    ) {
      const lines = buildEquipDetailLines(this.selected);
      // 与 drawBagEquipDetail 同一套可容纳行数：装得下则不分页
      // 底栏 4 操作时约 15 行（LCD 400 / rowH 16）
      const fitRows = 15;
      const rowsPerPage = lines.length <= fitRows ? lines.length || 1 : fitRows;
      const pages = Math.max(1, Math.ceil(lines.length / rowsPerPage));
      if (pages <= 1) return;
      this.detailPage = (this.detailPage + delta + pages) % pages;
      this.redraw();
      return;
    }
    if (this.mode !== "list" || !this.usesCategoryTabs()) return;
    const tabs = this.categoryTabs();
    if (tabs.length <= 1) return;
    this.categoryIndex = (this.categoryIndex + delta + tabs.length) % tabs.length;
    this.cursorIndex = 0;
    this.redraw();
  }

  /** Tap a category tab (bag / mall / enhance). */
  selectCategory(index: number): void {
    if (this.mode !== "list" || !this.usesCategoryTabs()) return;
    const tabs = this.categoryTabs();
    if (index < 0 || index >= tabs.length || index === this.categoryIndex) return;
    this.categoryIndex = index;
    this.cursorIndex = 0;
    this.redraw();
  }

  private adjustRechargeDraft(delta: number): void {
    const cur = Math.floor(Number(this.rechargeDraft.trim() || "0"));
    let next = (Number.isFinite(cur) ? cur : 0) + delta;
    if (delta < 0 && next < 1) next = 1;
    next = Math.min(100000, Math.max(1, next));
    this.rechargeDraft = String(next);
    this.redraw();
  }

  private appendRechargeKey(key: string): void {
    if (this.mode !== "recharge_amount") return;
    if (key === "删除") {
      this.rechargeDraft =
        this.rechargeDraft.length <= 1 ? "" : this.rechargeDraft.slice(0, -1);
      this.redraw();
      return;
    }
    if (key === "确定") {
      this.confirmRechargeAmount();
      return;
    }
    if (!/^\d$/.test(key)) return;
    if (this.rechargeDraft === "0") this.rechargeDraft = key;
    else if (this.rechargeDraft.length >= 6) return;
    else this.rechargeDraft += key;
    this.redraw();
  }

  private confirmRechargeAmount(): void {
    const yuan = Math.floor(Number(this.rechargeDraft.trim() || "0"));
    if (!Number.isFinite(yuan) || yuan < 1) {
      this.showTip("请输入有效整数金额");
      return;
    }
    if (yuan > 100000) {
      this.showTip("金额超出允许范围");
      return;
    }
    this.rechargeYuan = yuan;
    this.mode = "recharge_pay";
    this.actionLabels = ["支付宝", "微信"];
    this.actionCursor = 0;
    this.redraw();
  }

  private adjustBuyQty(delta: number): void {
    if (!this.selected || !isGoods(this.selected)) return;
    const max = this.maxBuyQty(this.selected);
    if (max < 1) {
      if (this.buyQty !== 0) {
        this.buyQty = 0;
        this.redraw();
      }
      return;
    }
    // 数量为 1 再按 ←：绕到最大可购（余额/库存/壶储量/99）
    let next: number;
    if (delta < 0 && this.buyQty <= 1) {
      next = max;
    } else {
      next = Math.min(max, Math.max(1, this.buyQty + delta));
    }
    if (next === this.buyQty) return;
    this.buyQty = next;
    this.redraw();
  }

  private maxBuyQty(g: ShopGoods): number {
    const bal = this.mall?.coolmoney ?? 0;
    const byMoney = g.price > 0 ? Math.floor(bal / g.price) : 99;
    if (isReservoirFlaskItemId(g.itemId)) {
      // Mall wire `stock` is item.dsh 堆叠数 (=1 for 802/803), not shop
      // inventory — using it caps qty at 1. Cap by balance + reservoir room.
      const byFlask = maxFlaskBuyUnitsFromBag(this.bagItems, g.itemId);
      return Math.max(0, Math.min(99, Math.max(0, byMoney), byFlask));
    }
    if ((g.itemId >>> 0) === BACKPACK_EXPAND_ITEM_ID) {
      // Direct capacity expand (+4/unit). Ignore DSH stock; clamp by remaining
      // slots to 256 and coolmoney.
      const cap =
        this.stats?.backpackCapacity ||
        this.bagCapacity ||
        20;
      const byExpand = maxBackpackExpandBuyUnits(cap);
      return Math.max(0, Math.min(99, Math.max(0, byMoney), byExpand));
    }
    const byStock = g.stock > 0 ? g.stock : 99;
    return Math.max(1, Math.min(99, byStock, Math.max(0, byMoney)));
  }

  move(delta: number): void {
    if (this.kind === "status") {
      const rows = this.statusRows().length;
      const pageCount = Math.max(1, Math.ceil(rows / STATUS_VISIBLE_ROWS));
      if (pageCount <= 1) return;
      this.cursorIndex = (this.cursorIndex + delta + pageCount) % pageCount;
      this.redraw();
      return;
    }
    if (this.kind === "equip" && this.mode === "list") {
      this.moveEquipGrid(delta, 0);
      return;
    }
    if (this.kind === "equip" && this.mode === "detail") {
      const n = this.replaceCandidates.length;
      if (!n) return;
      this.cursorIndex = (this.cursorIndex + delta + n) % n;
      this.selected = this.replaceCandidates[this.cursorIndex] ?? null;
      this.redraw();
      return;
    }
    if (this.mode === "actions" || this.mode === "recharge_pay") {
      if (!this.actionLabels.length) return;
      this.actionCursor =
        (this.actionCursor + delta + this.actionLabels.length) %
        this.actionLabels.length;
      this.redraw();
      return;
    }
    const n = this.listCount();
    if (!n) return;
    this.cursorIndex = (this.cursorIndex + delta + n) % n;
    this.redraw();
  }

  private moveEquipGrid(dRow: number, dCol: number): void {
    const cur = EQUIP_SLOT_UI[this.cursorIndex] ?? EQUIP_SLOT_UI[0]!;
    let row = cur.row + dRow;
    let col = cur.col + dCol;
    if (row < 0) row = 3;
    if (row > 3) row = 0;
    if (col < 0) col = 1;
    if (col > 1) col = 0;
    const next = EQUIP_SLOT_UI.findIndex((s) => s.row === row && s.col === col);
    if (next < 0) return;
    this.cursorIndex = next;
    this.equipFocusSlot = EQUIP_SLOT_UI[next]!.slot;
    this.selected = this.wornBySlot(this.equipFocusSlot);
    this.redraw();
  }

  confirm(): void {
    if (this.kind === "status") {
      // 只读汇总；确定无操作
      return;
    }
    if (this.kind === "equip") {
      this.confirmEquip();
      return;
    }
    if (this.mode === "recharge_amount") {
      this.confirmRechargeAmount();
      return;
    }
    if (this.mode === "recharge_pay") {
      const label = this.actionLabels[this.actionCursor];
      if (!label || !this.rechargeYuan) return;
      const action =
        label === "微信" ? "recharge_wechat" : "recharge_alipay";
      this.onAction?.(this.kind, action);
      this.mode = "list";
      this.actionLabels = [];
      this.redraw();
      return;
    }
    if (this.mode === "list") {
      this.openDetailFromList();
      return;
    }
    if (this.mode === "detail") {
      this.openActions();
      return;
    }
    // actions
    const label = this.actionLabels[this.actionCursor];
    if (!label) return;
    if (label === "返回") {
      this.mode = "list";
      this.selected = null;
      this.redraw();
      return;
    }
    const action = actionKey(label);
    this.onAction?.(this.kind, action, this.selected ?? undefined);
    if (
      action === "discard" ||
      action === "use" ||
      action === "combine" ||
      action === "gem_batch_combine" ||
      action === "gem_embed" ||
      action === "gem_remove" ||
      action === "gem_exchange" ||
      action === "gem_from_crystal" ||
      action === "equip" ||
      action === "buy" ||
      action === "skill_forget"
    ) {
      this.mode = "list";
      this.selected = null;
      this.buyQty = 1;
      this.redraw();
    }
  }

  /** Mall footer 赞助: native LCD amount keypad → 支付宝/微信. */
  beginMallRecharge(): void {
    if (this.kind !== "mall") return;
    this.rechargeDraft = "1";
    this.rechargeYuan = 0;
    this.selected = null;
    this.mode = "recharge_amount";
    this.redraw();
  }

  private confirmEquip(): void {
    if (this.mode === "list") {
      const ui = EQUIP_SLOT_UI[this.cursorIndex] ?? EQUIP_SLOT_UI[0]!;
      this.equipFocusSlot = ui.slot;
      this.selected = this.wornBySlot(ui.slot);
      if (!this.selected) {
        this.actionLabels = ["替换"];
      } else {
        const labels = ["替换"];
        // 强化上限 +16；三孔都满时不再显示镶嵌。
        const sg = this.selected.socketGems;
        const socketsFull = !!(
          sg &&
          sg.reflect &&
          sg.combo &&
          sg.revive
        );
        if ((this.selected.enhance | 0) >= 16 && !socketsFull) {
          labels.push("镶嵌");
        }
        if (sg && (sg.reflect || sg.combo || sg.revive)) labels.push("拆宝石");
        labels.push("卸载");
        this.actionLabels = labels;
      }
      this.actionCursor = 0;
      this.mode = "actions";
      this.redraw();
      return;
    }
    if (this.mode === "actions") {
      const label = this.actionLabels[this.actionCursor];
      if (label === "替换") {
        this.enterEquipReplace();
        return;
      }
      if (label === "卸载") {
        const worn = this.wornBySlot(this.equipFocusSlot);
        if (!worn) return;
        this.onAction?.(this.kind, "unequip", worn);
        this.mode = "list";
        this.redraw();
        return;
      }
      if (label === "拆宝石") {
        const worn = this.wornBySlot(this.equipFocusSlot);
        if (!worn) return;
        this.onAction?.(this.kind, "gem_remove", worn);
        this.mode = "list";
        this.redraw();
        return;
      }
      if (label === "镶嵌") {
        const worn = this.wornBySlot(this.equipFocusSlot);
        if (!worn) return;
        this.onAction?.(this.kind, "gem_embed", worn);
        this.mode = "list";
        this.redraw();
        return;
      }
      return;
    }
    if (this.mode === "detail") {
      const bag = this.replaceCandidates[this.cursorIndex];
      if (!bag) return;
      const worn = this.wornBySlot(this.equipFocusSlot);
      this.onAction?.(this.kind, worn ? "swap" : "equip", bag);
      this.mode = "list";
      this.selected = null;
      this.replaceCandidates = [];
      this.redraw();
    }
  }

  private enterEquipReplace(): void {
    const slot = this.equipFocusSlot;
    this.replaceCandidates = this.filterJobCompatibleEquips(
      this.bagItems.filter(
        (it) => it.kind === "equip" && (it.equipSlot === slot || it.equipSlot === -1),
      ),
    );
    // Prefer strict slot match; if catalog missed equipSlot, fall back to all equips.
    const strict = this.replaceCandidates.filter((it) => it.equipSlot === slot);
    if (strict.length) this.replaceCandidates = strict;
    this.cursorIndex = 0;
    this.selected = this.replaceCandidates[0] ?? null;
    this.mode = "detail";
    this.redraw();
  }

  cancel(): void {
    if (tryDismissPanelTipOnSoftRight(this.toast)) return;
    if (this.kind === "status") {
      this.hide();
      this.onClose?.();
      return;
    }
    if (this.kind === "equip") {
      if (this.mode === "actions" || this.mode === "detail") {
        this.mode = "list";
        this.replaceCandidates = [];
        this.selected = this.wornBySlot(this.equipFocusSlot);
        // Restore visual cursor for focus slot
        const vi = EQUIP_SLOT_UI.findIndex((s) => s.slot === this.equipFocusSlot);
        if (vi >= 0) this.cursorIndex = vi;
        this.redraw();
        return;
      }
      this.hide();
      this.onClose?.();
      return;
    }
    if (this.mode === "recharge_amount") {
      this.mode = "list";
      this.rechargeDraft = "1";
      this.rechargeYuan = 0;
      this.redraw();
      return;
    }
    if (this.mode === "recharge_pay") {
      this.mode = "recharge_amount";
      this.actionLabels = [];
      this.redraw();
      return;
    }
    if (this.mode === "actions") {
      // 背包/商城详情与操作同屏：取消直接回列表
      if (
        (this.kind === "bag" && this.selected && isBag(this.selected)) ||
        (this.kind === "mall" && this.selected && isGoods(this.selected))
      ) {
        this.mode = "list";
        this.selected = null;
        this.buyQty = 1;
        this.redraw();
        return;
      }
      this.mode = "detail";
      this.redraw();
      return;
    }
    if (this.mode === "detail") {
      this.mode = "list";
      this.selected = null;
      this.redraw();
      return;
    }
    this.hide();
    this.onClose?.();
  }

  private usesCategoryTabs(): boolean {
    return this.kind === "bag" || this.kind === "enhance" || this.kind === "mall";
  }

  private categoryTabs(): { id: string; label: string; short?: string }[] {
    if (this.kind === "mall") return MALL_TABS;
    if (this.kind === "bag" || this.kind === "enhance") return BAG_TABS;
    return [];
  }

  private filteredBag(): BagItem[] {
    const tab = BAG_TABS[this.categoryIndex] ?? BAG_TABS[0]!;
    return this.bagItems.filter((it) => tab.match(it));
  }

  private filteredMall(): ShopGoods[] {
    const goods = this.mall?.goods ?? [];
    const tab = MALL_TABS[this.categoryIndex] ?? MALL_TABS[0]!;
    return goods.filter((g) => tab.match(g));
  }

  private clampCursor(): void {
    const n = this.listCount();
    if (!n) {
      this.cursorIndex = 0;
      return;
    }
    this.cursorIndex = Math.min(this.cursorIndex, n - 1);
  }

  private listCount(): number {
    switch (this.kind) {
      case "bag":
      case "enhance":
        return Math.max(1, this.filteredBag().length);
      case "equip":
        return WORN_SLOT_NAMES.length;
      case "skill":
        return Math.max(1, this.skills.length);
      case "mall":
        return Math.max(1, this.filteredMall().length);
      case "status":
        return 1;
      default:
        return 1;
    }
  }

  private openDetailFromList(): void {
    if (this.kind === "status") {
      this.mode = "detail";
      this.redraw();
      return;
    }
    if (this.kind === "bag" || this.kind === "enhance") {
      const filtered = this.filteredBag();
      if (!filtered.length) return;
      const it = filtered[this.cursorIndex];
      if (!it) return;
      this.selected = it;
      this.detailPage = 0;
      // CBE：物品/装备详情与操作同屏
      this.openActions();
      void this.enrichSelectedBagDetail(it);
      return;
    }
    if (this.kind === "equip") {
      this.confirmEquip();
      return;
    }
    if (this.kind === "skill") {
      const sk = this.skills[this.cursorIndex];
      if (!sk) return;
      this.selected = sk;
      this.mode = "detail";
      this.redraw();
      return;
    }
    if (this.kind === "mall") {
      const filtered = this.filteredMall();
      if (!filtered.length) return;
      const g = filtered[this.cursorIndex];
      if (!g) return;
      this.selected = g;
      this.buyQty = 1;
      // 与背包道具详情同屏：说明 + 购买（←→ 调数量）
      this.openActions();
    }
  }

  /** Fetch dynamic desc (921 bookdes) and redraw if still on same item. */
  private async enrichSelectedBagDetail(it: BagItem): Promise<void> {
    if (!this.onEnrichBagDetail) return;
    const seq = it.seq;
    const itemId = it.itemId;
    let text: string | null = null;
    try {
      text = await this.onEnrichBagDetail(it);
    } catch {
      return;
    }
    if (!text || !text.trim()) return;
    if (!this.visible) return;
    if (!this.selected || !isBag(this.selected)) return;
    if (this.selected.seq !== seq || this.selected.itemId !== itemId) return;
    this.selected = { ...this.selected, desc: text.trim() };
    // Keep bag row in sync so reopening detail stays filled.
    this.bagItems = this.bagItems.map((row) =>
      row.seq === seq && row.itemId === itemId
        ? { ...row, desc: text!.trim() }
        : row,
    );
    if (this.mode === "actions" || this.mode === "detail") this.redraw();
  }

  private openActions(): void {
    this.actionLabels = this.buildActions();
    if (!this.actionLabels.length) return;
    this.actionCursor = 0;
    this.mode = "actions";
    // Selecting an item opens this panel; drop a leftover「使用成功」toast.
    this.clearTip();
    this.redraw();
  }

  private buildActions(): string[] {
    if (this.kind === "bag" && this.selected && isBag(this.selected)) {
      const it = this.selected;
      const acts: string[] = [];
      if (it.kind === "equip") {
        acts.push("装备");
        acts.push("强化");
        if (it.enhance >= 4) acts.push("洗练");
        if (it.enhance > 0) acts.push("重铸");
        if (it.enhance > 0) acts.push("提炼");
        const sg = it.socketGems;
        if (sg && (sg.reflect || sg.combo || sg.revive)) acts.push("拆宝石");
      } else if (isEnhanceCrystal(it.itemId)) {
        // 玄晶：≥3 且非十六级 →「合成」代替「使用」
        if (canCombineEnhanceCrystal(it.itemId, it.count)) acts.push("合成");
        if (
          it.itemId === 916 &&
          (this.stats?.crystal16ExchangeEnabled ? 1 : 0) === 1
        ) {
          acts.push("换宝石");
        }
      } else if (isCrystalFragment(it.itemId)) {
        // 玄晶碎片：≥10 →「合成」一级玄晶
        if (canCombineCrystalFragment(it.itemId, it.count)) acts.push("合成");
      } else if (isGemFragment(it.itemId)) {
        if (canCombineGemFragment(it.itemId, it.count)) acts.push("合成");
      } else if (parseGemItem(it.itemId)) {
        const gemMax = this.stats?.maxGemLevel || 3;
        // 镶嵌只出现在装备栏 ≥+16 的穿戴装备上，背包不提供。
        if (canCombineGem(it.itemId, it.count, gemMax)) acts.push("合成");
        if (canBatchCombineGem(it.itemId, it.count, gemMax)) acts.push("批量合成");
        if (canExchangeGemType(it.itemId, it.count)) acts.push("兑换");
      } else if (isDreamBadge(it.itemId)) {
        // 梦境徽章：≥300 →「合成」随机梦境装备
        if (canCombineDreamBadge(it.itemId, it.count)) acts.push("合成");
      } else if (isCrystalExtractStone(it.itemId)) {
        // 玄晶提炼石：从已强化装备详情点「提炼」，不走通用使用
      } else {
        // 非装备一律提供使用（道具/任务/材料/杂物/秘宝）
        acts.push("使用");
        if (isChestBoxItem(it.itemId)) acts.push("批量开启");
      }
      const socketed =
        it.kind === "equip" &&
        !!it.socketGems &&
        !!(it.socketGems.reflect || it.socketGems.combo || it.socketGems.revive);
      if (!socketed) acts.push("拍卖");
      acts.push("丢弃");
      acts.push("返回");
      return acts;
    }
    if (this.kind === "equip" && this.selected && isBag(this.selected)) {
      const acts = ["卸下"];
      const sg = this.selected.socketGems;
      if (sg && (sg.reflect || sg.combo || sg.revive)) acts.push("拆宝石");
      acts.push("返回");
      return acts;
    }
    if (this.kind === "enhance" && this.selected && isBag(this.selected)) {
      const acts = ["强化"];
      if (this.selected.enhance >= 4) acts.push("洗练");
      if (this.selected.enhance > 0) acts.push("重铸");
      if (this.selected.enhance > 0) acts.push("提炼");
      acts.push("返回");
      return acts;
    }
    if (this.kind === "skill" && this.selected && isSkill(this.selected)) {
      const acts: string[] = [];
      if (this.inBattle) acts.push("施放");
      if (isHealSkill(this.selected)) {
        if (this.inBattle) acts.push("回血施放");
        else if (isMapHealSkill(this.selected)) acts.push("回血施放");
        else acts.push("回血(战斗中)");
      }
      acts.push("遗忘(5金)");
      acts.push("返回");
      return acts;
    }
    if (this.kind === "mall" && this.selected && isGoods(this.selected)) {
      // 操作仅购买；数量用 ←→；返回走软键/取消
      return ["购买"];
    }
    return ["返回"];
  }

  private wornBySlot(slot: number): BagItem | null {
    // 7/7 type2 seq = slot+1
    return (
      this.wornItems.find((w) => w.seq === slot + 1) ||
      this.wornItems.find((w) => w.equipSlot === slot) ||
      null
    );
  }

  /** Drop weapons whose equip.dsh category does not match the role job. */
  private filterJobCompatibleEquips(items: BagItem[]): BagItem[] {
    const job = this.stats?.job ?? 0;
    if (job < 1 || job > 3) return items;
    return items.filter((it) => jobAllowsWeaponCategory(job, it.category));
  }

  private paintBrokenGlowOverlay(): void {
    this.brokenGlowGfx.clear();
    if (!this.brokenSlotRects.length) return;
    const pulse = 0.4 + 0.45 * (0.5 + 0.5 * Math.sin(this.brokenGlowPhase * 0.35));
    for (const r of this.brokenSlotRects) {
      this.brokenGlowGfx
        .rect(r.x - 3, r.y - 3, r.w + 6, r.h + 6)
        .stroke({ width: 3, color: EQUIP_BROKEN_RED, alpha: pulse * 0.55 });
      this.brokenGlowGfx
        .rect(r.x - 1, r.y - 1, r.w + 2, r.h + 2)
        .stroke({ width: 2, color: EQUIP_BROKEN_RED_HI, alpha: 0.35 + pulse * 0.45 });
      this.brokenGlowGfx
        .rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2)
        .fill({ color: EQUIP_BROKEN_RED, alpha: 0.08 + pulse * 0.16 });
    }
  }

  private syncBrokenGlowAnim(): void {
    const need =
      this.visible && this.kind === "equip" && this.brokenSlotRects.length > 0;
    if (need) {
      if (!this.brokenGlowTimer) {
        this.brokenGlowTimer = setInterval(() => {
          if (!this.visible || this.kind !== "equip") {
            this.stopBrokenGlowAnim();
            return;
          }
          this.brokenGlowPhase = (this.brokenGlowPhase + 1) % 360;
          this.paintBrokenGlowOverlay();
        }, 60);
      }
    } else {
      this.stopBrokenGlowAnim();
    }
  }

  private stopBrokenGlowAnim(): void {
    if (this.brokenGlowTimer) {
      clearInterval(this.brokenGlowTimer);
      this.brokenGlowTimer = null;
    }
    this.brokenGlowPhase = 0;
  }

  private redraw(): void {
    for (const t of this.labels) t.destroy({ children: true });
    this.labels = [];
    for (const s of this.uiSprites) s.destroy({ children: true });
    this.uiSprites = [];
    for (const h of this.hitSprites) h.destroy({ children: true });
    this.hitSprites = [];
    clearPointerHits(this.rowHits);
    this.box.clear();
    if (this.kind !== "equip") {
      this.brokenGlowGfx.clear();
      this.brokenSlotRects = [];
      this.stopBrokenGlowAnim();
    }

    if (this.kind === "status") {
      this.drawStatusPanel();
    } else if (this.kind === "bag") {
      this.drawBagPanel();
    } else if (this.kind === "equip") {
      this.drawWornEquipPanel();
    } else if (
      this.kind === "mall" &&
      this.mode === "actions" &&
      this.selected &&
      isGoods(this.selected)
    ) {
      const x = 4;
      const y = 2;
      const w = LCD_WIDTH - 8;
      const h = LCD_HEIGHT - 4;
      this.drawMallItemDetail(x, y, w, h);
    } else if (
      this.kind === "skill" &&
      this.mode === "detail" &&
      this.selected &&
      isSkill(this.selected)
    ) {
      const x = 4;
      const y = 2;
      const w = LCD_WIDTH - 8;
      const h = LCD_HEIGHT - 4;
      this.drawSkillDetail(x, y, w, h);
    } else {
      const x = 10;
      const y = 48;
      const w = LCD_WIDTH - 20;
      const h = LCD_HEIGHT - SOFTKEY_H - 64;
      paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

      const rechargeOverlay =
        this.mode === "recharge_amount" || this.mode === "recharge_pay";
      const pillH = 22;
      const pillY = y + 8;
      {
        const title = rechargeOverlay
          ? this.mode === "recharge_amount"
            ? "赞助金额(元)"
            : "赞助支付"
          : titleOf(this.kind);
        const pillW = Math.min(
          w - 20,
          Math.max(72, measureUiTextWidth(title) + 24),
        );
        drawCbeTitlePill(this.box, LCD_WIDTH / 2, pillY, pillW, pillH);
        this.addLabelCentered(
          title,
          LCD_WIDTH / 2,
          pillY + pillH / 2,
          CBE.ink,
        );
      }
      const showCatTabs = this.mode === "list" && this.usesCategoryTabs();
      const tabH = 20;
      const tabY = pillY + pillH + 6;
      if (showCatTabs) {
        this.drawCategoryTabStrip(
          x + 6,
          tabY,
          w - 12,
          tabH,
          this.categoryTabs(),
        );
        this.addLabel(this.listHint(), x + 10, tabY + tabH + 4, CBE.inkLabel);
      } else if (!rechargeOverlay) {
        this.addLabel(this.listHint(), x + 10, tabY, CBE.inkLabel);
      }

      if (this.mode === "recharge_amount") {
        this.drawRechargeAmountPanel(x, y, w);
      } else if (this.mode === "recharge_pay") {
        this.addLabel(`金额 ${this.rechargeYuan} 元`, x + 10, y + 36, CBE.inkTipGreen);
        this.addLabel("选择支付方式", x + 10, y + 54, CBE.inkLabel);
        this.actionLabels.forEach((label, i) => {
          const hi = i === this.actionCursor;
          const rowY = y + 76 + i * 20;
          if (hi) this.box.rect(x + 4, rowY, w - 8, 19).fill({ color: CBE.select });
          this.addLabel(label, x + 12, rowY + 2, hi ? CBE.ink : CBE.inkMint);
          const idx = i;
          this.rowHits.push(
            attachRowPointerHit(this, x + 4, rowY, w - 8, 19, () => {
              this.actionCursor = idx;
              this.redraw();
              this.confirm();
            }),
          );
        });
      } else if (this.mode === "actions") {
        this.drawActionList(x, y, w);
      } else if (this.mode === "detail") {
        this.drawDetail(x, y, w);
      } else {
        const listTop = showCatTabs ? tabY + tabH + 20 : tabY + 18;
        this.drawList(x, y, w, listTop);
      }
    }
    drawPanelTipOverlay(this, this.toast);
  }

  /**
   * CBE 装备信息：胶囊标题、中心角色、左右 4×2 部位格、下方详情；
   * 确定→替换/卸载弹层；替换→背包同部位列表。
   */
  private drawWornEquipPanel(): void {
    const x = 4;
    const y = 2;
    const w = LCD_WIDTH - 8;
    const h = LCD_HEIGHT - 4;
    paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

    if (this.mode === "detail") {
      this.brokenSlotRects = [];
      this.brokenGlowGfx.clear();
      this.stopBrokenGlowAnim();
      this.drawEquipReplacePanel(x, y, w, h);
      return;
    }

    const pillW = 72;
    const pillH = 22;
    const pillY = y + 8;
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, pillY, pillW, pillH);
    this.addLabelCentered("装备", LCD_WIDTH / 2, pillY + pillH / 2, CBE.ink);

    const softY = y + h - PANEL_BOTTOM_PAD;
    // handkuang 25×25; leave room for attr lines + center full-body actor.
    this.ensureEquipArt();
    const slotSize = SLOT_FRAME_SIZE;
    const slotGapY = 3;
    const gridTop = pillY + pillH + 8;
    const gridH = 4 * slotSize + 3 * slotGapY;
    const leftX = x + 10;
    const rightX = x + w - 10 - slotSize;
    const actorCx = LCD_WIDTH / 2;
    const actorCy = gridTop + gridH / 2;

    // Worn/bag refresh after equip ops never calls setEquipActor; if GPU strip
    // or hide() cleared the stand, rebuild from the last configured job/sex.
    this.ensureEquipActorTex();
    if (this.equipActorTexAlive()) {
      const spr = new Sprite(this.equipActorTex!);
      spr.anchor.set(0.5, 0.5);
      // Fit full body between slot columns + part labels without clipping.
      const maxH = gridH + 4;
      const partReserve = EQUIP_PART_LABEL_MAX_W + EQUIP_PART_LABEL_GAP;
      const maxW = Math.max(
        40,
        rightX - leftX - slotSize - 8 - 2 * partReserve,
      );
      const sx = Math.min(maxW / spr.texture.width, maxH / spr.texture.height);
      spr.scale.set(sx);
      spr.position.set(actorCx, actorCy);
      this.addChild(spr);
      this.uiSprites.push(spr);
    }

    const frameTex = slotFrameTextureSync();
    this.brokenSlotRects = [];
    for (let i = 0; i < EQUIP_SLOT_UI.length; i++) {
      const ui = EQUIP_SLOT_UI[i]!;
      const sx = ui.col === 0 ? leftX : rightX;
      const sy = gridTop + ui.row * (slotSize + slotGapY);
      const worn = this.wornBySlot(ui.slot);
      const broken = !!worn && worn.durability === 0;
      const focused =
        i === this.cursorIndex && (this.mode === "list" || this.mode === "actions");
      const cat = worn
        ? (lookupItemSync(worn.itemId)?.category ?? defaultCategoryForBodySlot(ui.slot))
        : defaultCategoryForBodySlot(ui.slot);
      const iconTex = equipIconTextureSync(cat);
      const host = new Container();
      host.position.set(sx, sy);
      host.eventMode = "none";
      mountEquipSlot(host, frameTex, iconTex, {
        category: cat,
        empty: !worn,
        broken,
      });
      this.addChild(host);
      this.uiSprites.push(host);

      // Part label (帽/衣/…/武器) in the strip between slot and center actor.
      const partTex = equipPartLabelTextureSync(i);
      if (partTex && !partTex.destroyed) {
        const pw = partTex.width;
        const ph = partTex.height;
        const partSpr = uiSprite(partTex, pw, ph);
        const py = sy + Math.floor((slotSize - ph) / 2);
        const px =
          ui.col === 0
            ? sx + slotSize + EQUIP_PART_LABEL_GAP
            : sx - EQUIP_PART_LABEL_GAP - pw;
        partSpr.position.set(px, py);
        partSpr.eventMode = "none";
        this.addChild(partSpr);
        this.uiSprites.push(partSpr);
      }

      if (focused || broken) {
        this.box
          .rect(sx - 1, sy - 1, slotSize + 2, slotSize + 2)
          .stroke({
            width: 2,
            color: focused ? EQUIP_CURSOR : EQUIP_BROKEN_RED,
          });
      }
      if (broken) {
        this.brokenSlotRects.push({ x: sx, y: sy, w: slotSize, h: slotSize });
      }
      if (this.mode === "list") {
        const idx = i;
        this.rowHits.push(
          attachRowPointerHit(this, sx, sy, slotSize, slotSize, () => {
            this.cursorIndex = idx;
            this.equipFocusSlot = EQUIP_SLOT_UI[idx]!.slot;
            this.selected = this.wornBySlot(this.equipFocusSlot);
            this.redraw();
            this.confirm();
          }),
        );
      }
    }
    this.paintBrokenGlowOverlay();
    this.syncBrokenGlowAnim();

    const detailTop = gridTop + gridH + 6;
    const detailBottom = softY - 6;
    this.box
      .moveTo(x + 10, detailTop - 4)
      .lineTo(x + w - 10, detailTop - 4)
      .stroke({ width: 1, color: CBE.borderDim });

    const focusSlot = EQUIP_SLOT_UI[this.cursorIndex]?.slot ?? this.equipFocusSlot;
    const focusWorn = this.wornBySlot(focusSlot);
    if (!focusWorn) {
      const part = EQUIP_REPLACE_PART[focusSlot] ?? WORN_SLOT_NAMES[focusSlot] ?? "";
      this.addAttrText(`${part}：空`, x + 14, detailTop + 4, CBE.inkMint);
    } else {
      const qColor = qualityColor(focusWorn.quality);
      this.addAttrText(formatBagName(focusWorn), x + 14, detailTop + 2, qColor);
      const lines = buildEquipDetailLines(focusWorn, { omitTypeJob: true });
      const rowH = UC3_CELL;
      const maxRows = Math.max(1, Math.floor((detailBottom - detailTop - 18) / rowH));
      let ly = detailTop + 16;
      for (let i = 0; i < Math.min(maxRows, lines.length); i++) {
        const rows = this.paintEquipLine(lines[i]!, x + 14, ly);
        ly += rowH * Math.max(1, rows);
      }
    }

    if (this.mode === "actions") {
      this.drawEquipActionPopup();
    }
  }

  private drawEquipActionPopup(): void {
    const labels = this.actionLabels;
    if (!labels.length) return;
    const rowH = 20;
    const padX = 14;
    const padY = 8;
    let maxTextW = 48;
    for (const lab of labels) {
      let tw = 0;
      for (const ch of lab) tw += (ch.codePointAt(0) ?? 0) > 0xff ? 14 : 8;
      if (tw > maxTextW) maxTextW = tw;
    }
    const pw = maxTextW + padX * 2;
    const ph = padY * 2 + labels.length * rowH;
    const px = Math.floor((LCD_WIDTH - pw) / 2);
    const py = Math.floor((LCD_HEIGHT - SOFTKEY_H) / 2 - ph / 2);
    this.box
      .roundRect(px, py, pw, ph, 4)
      .fill({ color: 0x0a1814, alpha: 0.96 })
      .stroke({ width: 1, color: CBE.borderBright });
    labels.forEach((lab, i) => {
      const hi = i === this.actionCursor;
      const ry = py + padY + i * rowH;
      if (hi) this.box.rect(px + 4, ry, pw - 8, rowH - 2).fill({ color: CBE.select });
      this.addLabelCentered(lab, LCD_WIDTH / 2, ry + rowH / 2 - 1, hi ? CBE.ink : CBE.inkMint);
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, px + 4, ry, pw - 8, rowH - 2, () => {
          this.actionCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    });
  }

  /** Replace flow: bag equips for focused body part + detail + 替换/返回. */
  private drawEquipReplacePanel(x: number, y: number, w: number, h: number): void {
    this.ensureEquipArt();
    const part = EQUIP_REPLACE_PART[this.equipFocusSlot] ?? "部位";
    const pillW = Math.min(160, 28 + part.length * 14 + 56);
    const pillH = 22;
    const pillY = y + 8;
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, pillY, pillW, pillH);
    this.addLabelCentered(`替换${part}装备`, LCD_WIDTH / 2, pillY + pillH / 2, CBE.ink);

    const softY = y + h - PANEL_BOTTOM_PAD;
    const splitY = Math.floor(y + h * 0.42);
    const listTop = pillY + pillH + 16;
    const listBottom = splitY - 8;
    const rows = this.replaceCandidates;
    const total = Math.max(1, rows.length);
    const visible = Math.max(1, Math.floor((listBottom - listTop - 20) / 18));
    const pageStart = Math.floor(this.cursorIndex / visible) * visible;
    const page = Math.floor(pageStart / visible);
    const pageCount = Math.max(1, Math.ceil(total / visible));

    this.drawScrollArrow(LCD_WIDTH / 2, listTop - 2, true, page > 0);
    for (let i = 0; i < visible; i++) {
      const idx = pageStart + i;
      const ry = listTop + 10 + i * 18;
      if (!rows.length) {
        if (i === 0) this.addLabelCentered("无可用装备", LCD_WIDTH / 2, ry + 8, CBE.inkMint);
        break;
      }
      const it = rows[idx];
      if (!it) break;
      const hi = idx === this.cursorIndex;
      if (hi) this.box.rect(x + 10, ry, w - 20, 17).fill({ color: CBE.select });
      const cat =
        lookupItemSync(it.itemId)?.category ??
        defaultCategoryForBodySlot(this.equipFocusSlot);
      const iconSpr = mountEquipIconOnly(
        this,
        equipIconTextureSync(cat),
        x + 14,
        ry + Math.floor((17 - EQUIP_ICON_SIZE) / 2),
      );
      if (iconSpr) {
        iconSpr.eventMode = "none";
        this.uiSprites.push(iconSpr);
      }
      this.addLabel(
        `${formatBagName(it)} X${it.count}`,
        x + 14 + EQUIP_ICON_SIZE + 4,
        ry + 1,
        hi ? CBE.ink : qualityColor(it.quality),
      );
      const hitIdx = idx;
      this.rowHits.push(
        attachRowPointerHit(this, x + 10, ry, w - 20, 17, () => {
          this.cursorIndex = hitIdx;
          this.selected = this.replaceCandidates[hitIdx] ?? null;
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
        y: listBottom - 4,
        w: w - 20,
        page: page + 1,
        pages: pageCount,
        addLabel: (t, lx, ly, c) => this.addAttrText(t, lx, ly, c),
        onPrev: () => {
          this.cursorIndex = Math.max(0, pageStart - visible);
          this.selected = this.replaceCandidates[this.cursorIndex] ?? null;
          this.redraw();
        },
        onNext: () => {
          this.cursorIndex = Math.min(total - 1, pageStart + visible);
          this.selected = this.replaceCandidates[this.cursorIndex] ?? null;
          this.redraw();
        },
      });
    } else {
      this.drawScrollArrow(LCD_WIDTH / 2, listBottom + 2, false, false);
    }

    this.box
      .moveTo(x + 10, splitY)
      .lineTo(x + w - 10, splitY)
      .stroke({ width: 1, color: CBE.borderDim });

    const sel = rows[this.cursorIndex] ?? null;
    if (sel) {
      const lines = buildEquipDetailLines(sel);
      const rowH = UC3_CELL;
      const maxRows = Math.max(1, Math.floor((softY - 8 - splitY - 6) / rowH));
      let ly = splitY + 8;
      for (let i = 0; i < Math.min(maxRows, lines.length); i++) {
        const rows = this.paintEquipLine(lines[i]!, x + 14, ly);
        ly += rowH * Math.max(1, rows);
      }
    } else {
      this.addAttrText("选择背包装备后替换", x + 14, splitY + 10, CBE.inkLabel);
    }

  }

  /** Center toast over panel for use/chest feedback. */
  /**
   * CBE 背包页（江湖OL.CBE 0x010250F2 标签簇 + 截图）：
   * 胶囊「背包」、六分类 Tab、黄字 `%d/%d个`、品质色列表、金/银/铜栏；操作走 HTML 软键。
   */
  private drawBagPanel(): void {
    const x = 4;
    const y = 2;
    const w = LCD_WIDTH - 8;
    const h = LCD_HEIGHT - 4;

    const softY = y + h - PANEL_BOTTOM_PAD;
    const moneyH = 22;
    const moneyY = softY - moneyH - 4;

    // Item/equip detail owns the top title (name bar). Skip「背包」pill so
    // addLabelCentered("背包") does not sit under the item name.
    if (this.mode === "actions" && this.selected && isBag(this.selected)) {
      if (this.selected.kind === "equip") {
        this.drawBagEquipDetail(x, y, w, h);
      } else {
        this.drawBagItemDetail(x, y, w, h);
      }
      return;
    }

    paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

    const pillW = 72;
    const pillH = 22;
    const pillY = y + 8;
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, pillY, pillW, pillH);
    this.addLabelCentered("背包", LCD_WIDTH / 2, pillY + pillH / 2, CBE.ink);

    if (this.mode === "actions") {
      this.drawBagInnerActions(x, y, w, pillY + pillH + 8, moneyY - 8);
      return;
    }
    if (this.mode === "detail") {
      this.drawBagInnerDetail(x, y, w, pillY + pillH + 8, moneyY - 8);
      this.drawBagMoneyBar(x + 8, moneyY, w - 16, moneyH);
      return;
    }

    // —— list ——
    const tabY = pillY + pillH + 6;
    const tabH = 20;
    this.drawBagTabs(x + 6, tabY, w - 12, tabH);

    const used = this.bagItems.length;
    const cap = Math.max(this.bagCapacity, used);
    this.addLabelCentered(`${used}/${cap}个`, LCD_WIDTH / 2, tabY + tabH + 12, BAG_CAP_INK);

    const filtered = this.filteredBag();
    const rows = filtered.length
      ? filtered
      : null;
    const emptyText = this.bagItems.length ? "本分类为空" : "背包为空";
    const total = rows ? rows.length : 1;
    const pageStart = Math.floor(this.cursorIndex / BAG_VISIBLE_ROWS) * BAG_VISIBLE_ROWS;
    const localCursor = this.cursorIndex - pageStart;
    const pageCount = Math.max(1, Math.ceil(total / BAG_VISIBLE_ROWS));
    const page = Math.floor(pageStart / BAG_VISIBLE_ROWS);

    const arrowTopY = tabY + tabH + 22;
    const listTop = arrowTopY + 4;
    const listBottom = moneyY - (pageCount > 1 ? 28 : 16);
    const rowH = Math.max(16, Math.floor((listBottom - listTop) / BAG_VISIBLE_ROWS));
    const listX = x + 10;
    const listW = w - 20;

    for (let i = 0; i < BAG_VISIBLE_ROWS; i++) {
      const ry = listTop + i * rowH;
      const ty = ry + Math.floor((rowH - 14) / 2);
      if (!rows) {
        if (i === 0) this.addAttrText(emptyText, listX + 4, ty, CBE.inkMint);
        break;
      }
      const it = rows[pageStart + i];
      if (!it) break;
      const hi = i === localCursor;
      const col = qualityColor(it.quality);
      if (hi) {
        this.box.rect(listX, ry, listW, rowH - 1).fill({ color: BAG_SELECT });
      }
      const name = formatBagName(it);
      this.addAttrText(name, listX + 4, ty, hi ? CBE.ink : col);
      this.addAttrTextRight(String(it.count), listX + listW - 4, ty, hi ? CBE.ink : col);
      const absIdx = pageStart + i;
      this.rowHits.push(
        attachRowPointerHit(this, listX, ry, listW, rowH - 1, () => {
          this.cursorIndex = absIdx;
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
        y: moneyY - 24,
        w: w - 20,
        page: page + 1,
        pages: pageCount,
        addLabel: (t, lx, ly, c) => this.addAttrText(t, lx, ly, c),
        onPrev: () => {
          this.cursorIndex = Math.max(0, pageStart - BAG_VISIBLE_ROWS);
          this.redraw();
        },
        onNext: () => {
          const maxIdx = Math.max(0, (rows?.length ?? 1) - 1);
          this.cursorIndex = Math.min(maxIdx, pageStart + BAG_VISIBLE_ROWS);
          this.redraw();
        },
      });
    }

    this.drawBagMoneyBar(x + 8, moneyY, w - 16, moneyH);
  }

  private drawBagTabs(x: number, y: number, w: number, h: number): void {
    this.drawCategoryTabStrip(
      x,
      y,
      w,
      h,
      BAG_TABS.map((t) => ({ label: t.label, short: t.short })),
    );
  }

  /** Equal-width category tabs; tap switches (same as ←→). */
  private drawCategoryTabStrip(
    x: number,
    y: number,
    w: number,
    h: number,
    tabs: { label: string; short?: string }[],
  ): void {
    const n = tabs.length;
    if (n <= 0) return;
    const gap = 2;
    const tw = Math.floor((w - gap * (n - 1)) / n);
    for (let i = 0; i < n; i++) {
      const tab = tabs[i]!;
      const tx = x + i * (tw + gap);
      const active = i === this.categoryIndex;
      this.box
        .roundRect(tx, y, tw, h, 2)
        .fill({ color: active ? CBE.bgPanelDark : BAG_TAB_BG })
        .stroke({ width: 1, color: active ? CBE.borderBright : CBE.borderDim });
      const text = active
        ? tab.label.slice(0, 4)
        : (tab.short ?? tab.label.slice(0, 1));
      this.addLabelCentered(
        text,
        tx + tw / 2,
        y + h / 2,
        active ? CBE.ink : CBE.inkMint,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, tx, y, tw, h, () => this.selectCategory(idx)),
      );
    }
  }

  private drawBagMoneyBar(x: number, y: number, w: number, h: number): void {
    this.ensureMoneyIcons();
    this.box.rect(x, y, w, h).fill({ color: 0x0c261e });
    const { gold, silver, copper } = splitCopperMoney(this.bagMoney);
    // CBE 0x01023016：icon x ≈ base+0x32 / +0x67 / +0x9c；数字右对齐贴图标左侧。
    const parts: { n: number; tex: Texture | null | undefined; iw: number; ih: number }[] = [
      { n: gold, tex: this.moneyTex?.jin, iw: 11, ih: 8 },
      { n: silver, tex: this.moneyTex?.yin, iw: 11, ih: 8 },
      { n: copper, tex: this.moneyTex?.tong, iw: 9, ih: 9 },
    ];
    const slotW = Math.floor(w / 3);
    for (let i = 0; i < 3; i++) {
      const p = parts[i]!;
      const slotX = x + i * slotW;
      const iconX = slotX + slotW - 8 - p.iw;
      const iconY = y + Math.floor((h - p.ih) / 2);
      if (p.tex) {
        const spr = uiSprite(p.tex);
        spr.position.set(iconX, iconY);
        this.addChild(spr);
        this.uiSprites.push(spr);
      }
      // White money amounts (CBE DrawString 0xFFFFFF)
      this.addAttrTextRight(
        String(p.n),
        iconX - 2,
        y + Math.floor((h - 14) / 2),
        BAG_MONEY_INK,
      );
    }
  }

  /** Preload JHOnlineData jin.gif / yin.gif / tong.gif. */
  private ensureMoneyIcons(): void {
    if (this.moneyTex || this.moneyTexLoading) return;
    this.moneyTexLoading = true;
    void Promise.all([
      tryLoadUiTexture("jin.gif"),
      tryLoadUiTexture("yin.gif"),
      tryLoadUiTexture("tong.gif"),
    ]).then(([jin, yin, tong]) => {
      this.moneyTex = { jin, yin, tong };
      this.moneyTexLoading = false;
      if (this.visible && this.kind === "bag") this.redraw();
    });
  }

  /** Prefetch handkuang.gif + equipico.gif + part labels 0.gif..7.gif. */
  private ensureEquipArt(): void {
    if (this.equipArtReady || this.equipArtLoading) return;
    this.equipArtLoading = true;
    void warmEquipArt()
      .then(() => {
        this.equipArtReady = true;
        this.equipArtLoading = false;
        if (this.visible && this.kind === "equip") this.redraw();
      })
      .catch((e) => {
        console.warn("[system] equip art", e);
        this.equipArtLoading = false;
      });
  }


  /**
   * 技能详情：标题名、暗色「技能介绍」区（skill.dsh 说明）、耗蓝/施放提示。
   */
  private drawSkillDetail(x: number, y: number, w: number, h: number): void {
    const sk = this.selected;
    if (!sk || !isSkill(sk)) return;

    paintCbePanel(this, this.box, x, y, w, h, CBE.bgPanel);

    const titleH = 28;
    const titleY = y + 8;
    this.box
      .rect(x + 4, titleY, w - 8, titleH)
      .fill({ color: CBE.bgPanelDark });
    this.box
      .moveTo(x + 8, titleY + titleH)
      .lineTo(x + w - 8, titleY + titleH)
      .stroke({ width: 1, color: CBE.borderDim });
    this.addLabelCentered(sk.name, LCD_WIDTH / 2, titleY + titleH / 2, CBE.inkBody);

    const softY = y + h - 12;
    const descX = x + 10;
    const descY = titleY + titleH + 8;
    const descW = w - 20;
    const mpLine = sk.mpCost != null ? `耗蓝 ${sk.mpCost}` : "";
    const battleLine = this.inBattle
      ? "战斗中可施放"
      : isMapHealSkill(sk)
        ? "地图可施放回血"
        : "地图不可施放";
    const footerRows: { text: string; color: number }[] = [
      ...(mpLine ? [{ text: mpLine, color: CBE.inkMint }] : []),
      { text: battleLine, color: CBE.inkMint },
      { text: "左=操作 右=返回", color: CBE.inkLabel },
    ];
    const wrappedFooter = footerRows.flatMap(({ text, color }) =>
      wrapUc3Lines(text, descW - 12).map((line) => ({ line, color })),
    );
    const footerLineH = 16;
    const metaH = wrappedFooter.length * footerLineH + 10;
    const metaTop = softY - metaH;

    const descH = Math.max(40, metaTop - descY - 8);
    this.box.rect(descX, descY, descW, descH).fill({ color: 0x0a1410 });

    this.addAttrText("技能介绍", descX + 6, descY + 6, CBE.inkTip);
    const desc =
      (sk.desc && sk.desc !== "-" ? sk.desc : "") || "（无技能介绍）";
    const wrapped = wrapUc3Lines(desc, descW - 12);
    const maxLines = Math.max(1, Math.floor((descH - 28) / footerLineH));
    wrapped.slice(0, maxLines).forEach((line, i) => {
      this.addAttrText(line, descX + 6, descY + 24 + i * footerLineH, CBE.ink);
    });

    this.box.rect(descX, metaTop, descW, metaH).fill({ color: CBE.bgPanelDark });
    this.box
      .moveTo(descX, metaTop)
      .lineTo(descX + descW, metaTop)
      .stroke({ width: 1, color: CBE.borderDim });
    let fy = metaTop + 5;
    for (const row of wrappedFooter) {
      this.addAttrText(row.line, descX + 6, fy, row.color);
      fy += footerLineH;
    }
  }

  /**
   * CBE 道具详情（截图「神仙壶」）：标题名、暗色说明区、底栏使用/丢弃/返回。
   */
  private drawBagItemDetail(x: number, y: number, w: number, h: number): void {
    const it = this.selected;
    if (!it || !isBag(it)) return;

    paintCbePanel(this, this.box, x, y, w, h, CBE.bgPanel);

    const titleH = 28;
    const titleY = y + 8;
    this.box
      .rect(x + 4, titleY, w - 8, titleH)
      .fill({ color: CBE.bgPanelDark });
    this.box
      .moveTo(x + 8, titleY + titleH)
      .lineTo(x + w - 8, titleY + titleH)
      .stroke({ width: 1, color: CBE.borderDim });
    // 标题：原版偏薄荷绿（非品质色）
    this.addLabelCentered(it.name, LCD_WIDTH / 2, titleY + titleH / 2, CBE.inkBody);

    const softY = y + h - PANEL_BOTTOM_PAD;
    const acts = this.actionLabels;
    const btnH = 22;
    const btnGap = 4;
    const actionH = acts.length * (btnH + btnGap) + 12;
    const actionTop = softY - actionH - 4;

    // Description well
    const descX = x + 10;
    const descY = titleY + titleH + 8;
    const descW = w - 20;
    const descH = Math.max(40, actionTop - descY - 8);
    this.box.rect(descX, descY, descW, descH).fill({ color: 0x0a1410 });

    const desc =
      (it.desc && it.desc !== "-" ? it.desc : "") ||
      (it.count > 1 ? `数量 ${it.count}` : "（无说明）");
    const wrapped = wrapUc3Lines(desc, descW - 12);
    const maxLines = Math.floor((descH - 8) / 16);
    wrapped.slice(0, maxLines).forEach((line, i) => {
      this.addAttrText(line, descX + 6, descY + 6 + i * 16, CBE.ink);
    });

    // Action buttons
    this.box.rect(x + 4, actionTop, w - 8, actionH).fill({ color: CBE.bgPanel });
    this.box
      .moveTo(x + 8, actionTop)
      .lineTo(x + w - 8, actionTop)
      .stroke({ width: 1, color: CBE.borderDim });

    const btnW = 72;
    acts.forEach((label, i) => {
      const hi = i === this.actionCursor;
      const by = actionTop + 6 + i * (btnH + btnGap);
      const bx = Math.floor((LCD_WIDTH - btnW) / 2);
      this.box
        .roundRect(bx, by, btnW, btnH, 3)
        .fill({ color: CBE.bgDeep })
        .stroke({ width: 1, color: CBE.borderBright });
      if (hi) {
        const cy = by + btnH / 2;
        this.box.poly([bx - 8, cy, bx - 4, cy - 4, bx, cy, bx - 4, cy + 4]).fill({
          color: CBE.inkMint,
        });
        this.box
          .poly([bx + btnW + 8, cy, bx + btnW + 4, cy - 4, bx + btnW, cy, bx + btnW + 4, cy + 4])
          .fill({ color: CBE.inkMint });
      }
      this.addLabelCentered(
        label,
        LCD_WIDTH / 2,
        by + btnH / 2,
        hi ? CBE.inkMint : CBE.ink,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, bx, by, btnW, btnH, () => {
          this.actionCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    });
  }

  /**
   * Mall 赞助金额 — UC3 keypad on LCD (←→ ±1, tap digits, 确定/软左继续).
   */
  private drawRechargeAmountPanel(x: number, y: number, w: number): void {
    // Title pill drawn by redraw() chrome; body starts below it.
    this.addLabel("←→微调 · 点按数字", x + 10, y + 36, CBE.inkLabel);

    const boxY = y + 54;
    const boxH = 28;
    this.box
      .roundRect(x + 10, boxY, w - 20, boxH, 3)
      .fill({ color: CBE.bgPanel })
      .stroke({ width: 1, color: CBE.borderBright });
    const shown = (this.rechargeDraft || "").trim() || "0";
    this.addLabelCentered(`${shown} 元`, LCD_WIDTH / 2, boxY + boxH / 2, CBE.inkTipGreen);

    const keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "删除", "0", "确定"];
    const keyW = 58;
    const keyH = 26;
    const gap = 6;
    const gridW = keyW * 3 + gap * 2;
    const startX = x + Math.floor((w - gridW) / 2);
    let baseY = boxY + boxH + 10;
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
      this.box
        .roundRect(kx, ky, keyW, keyH, 3)
        .fill({ color: hi ? CBE.bgPanelDark : CBE.bgDeep })
        .stroke({ width: 1, color: CBE.borderBright });
      this.addLabelCentered(key, kx + keyW / 2, ky + keyH / 2, keyInk);
      this.rowHits.push(
        attachRowPointerHit(this, kx, ky, keyW, keyH, () => {
          this.appendRechargeKey(key);
        }),
      );
    }
  }

  /**
   * 商城详情：与背包道具详情同壳——标题、暗色说明、底栏购买；←→ 调数量。
   */
  private drawMallItemDetail(x: number, y: number, w: number, h: number): void {
    const g = this.selected;
    if (!g || !isGoods(g)) return;

    paintCbePanel(this, this.box, x, y, w, h, CBE.bgPanel);

    const titleH = 28;
    const titleY = y + 8;
    this.box.rect(x + 4, titleY, w - 8, titleH).fill({ color: CBE.bgPanelDark });
    this.box
      .moveTo(x + 8, titleY + titleH)
      .lineTo(x + w - 8, titleY + titleH)
      .stroke({ width: 1, color: CBE.borderDim });
    this.addLabelCentered(g.name, LCD_WIDTH / 2, titleY + titleH / 2, CBE.inkBody);

    const softY = y + h - PANEL_BOTTOM_PAD;
    const btnH = 22;
    const btnGap = 4;
    // 数量行 + 购买按钮
    const actionH = (btnH + btnGap) * 2 + 16;
    const actionTop = softY - actionH - 4;

    const descX = x + 10;
    const descY = titleY + titleH + 8;
    const descW = w - 20;
    const descH = Math.max(40, actionTop - descY - 8);
    this.box.rect(descX, descY, descW, descH).fill({ color: 0x0a1410 });

    const stockHint =
      !isReservoirFlaskItemId(g.itemId) && g.stock > 0
        ? `  库存 ${g.stock}`
        : "";
    const desc =
      (g.desc && g.desc !== "-" ? g.desc : "") ||
      `酷宝 ${g.price}` + stockHint;
    const wrapped = wrapUc3Lines(desc, descW - 12);
    const maxLines = Math.max(1, Math.floor((descH - 28) / 16));
    wrapped.slice(0, maxLines).forEach((line, i) => {
      this.addAttrText(line, descX + 6, descY + 6 + i * 16, CBE.ink);
    });
    // Price / balance under desc text (余额右对齐，勿用固定左偏移/窄 maxWidth)
    const metaY = descY + descH - 18;
    this.addAttrText(`价格 ${g.price}酷宝`, descX + 6, metaY, CBE.inkTip);
    const bal = this.mall?.coolmoney ?? 0;
    this.addAttrTextRight(`余额 ${bal}`, descX + descW - 6, metaY, CBE.inkTip, descW - 12);

    this.box.rect(x + 4, actionTop, w - 8, actionH).fill({ color: CBE.bgPanel });
    this.box
      .moveTo(x + 8, actionTop)
      .lineTo(x + w - 8, actionTop)
      .stroke({ width: 1, color: CBE.borderDim });

    const maxQty = this.maxBuyQty(g);
    if (maxQty < 1) this.buyQty = 0;
    else if (this.buyQty < 1 || this.buyQty > maxQty) {
      this.buyQty = Math.min(Math.max(1, this.buyQty), maxQty);
    }
    const qtyY = actionTop + 10;
    const qtyHint =
      maxQty < 1 && isReservoirFlaskItemId(g.itemId)
        ? "储量已满，无法购买"
        : maxQty < 1 && (g.itemId >>> 0) === BACKPACK_EXPAND_ITEM_ID
          ? "背包已达上限，不能再扩容"
          : `⇦ 数量 ${this.buyQty}/${maxQty} ⇨`;
    this.addLabelCentered(qtyHint, LCD_WIDTH / 2, qtyY + btnH / 2, CBE.inkTipGreen);

    const btnW = 72;
    const by = actionTop + 10 + btnH + btnGap;
    const bx = Math.floor((LCD_WIDTH - btnW) / 2);
    const hi = this.actionCursor === 0;
    this.box
      .roundRect(bx, by, btnW, btnH, 3)
      .fill({ color: CBE.bgDeep })
      .stroke({ width: 1, color: CBE.borderBright });
    if (hi) {
      const cy = by + btnH / 2;
      this.box.poly([bx - 8, cy, bx - 4, cy - 4, bx, cy, bx - 4, cy + 4]).fill({
        color: CBE.inkMint,
      });
      this.box
        .poly([bx + btnW + 8, cy, bx + btnW + 4, cy - 4, bx + btnW, cy, bx + btnW + 4, cy + 4])
        .fill({ color: CBE.inkMint });
    }
    this.addLabelCentered("购买", LCD_WIDTH / 2, by + btnH / 2, hi ? CBE.inkMint : CBE.ink);
    this.rowHits.push(
      attachRowPointerHit(this, bx, by, btnW, btnH, () => {
        this.actionCursor = 0;
        this.redraw();
        this.confirm();
      }),
    );
  }

  /**
   * CBE 装备详情（截图）：品质色标题、[等级]/主属性黄值、强化附加亮/灰、底栏操作。
   * 同部位已穿戴时右侧显示对比：= / +m(红) / -n(绿)。
   * 属性能一页装下则不分页。
   */
  private drawBagEquipDetail(x: number, y: number, w: number, h: number): void {
    const it = this.selected;
    if (!it || !isBag(it)) return;

    paintCbePanel(this, this.box, x, y, w, h, CBE.bgDeep);

    const qColor = qualityColor(it.quality);
    const titleH = 28;
    const titleY = y + 6;
    // Name bar
    this.box
      .roundRect(x + 8, titleY, w - 16, titleH, 4)
      .fill({ color: CBE.bgPanelDark })
      .stroke({ width: 1, color: CBE.borderBright });
    this.addLabelCentered(it.name, LCD_WIDTH / 2, titleY + titleH / 2, qColor);

    const softY = y + h - PANEL_BOTTOM_PAD;
    const btnH = 18;
    const btnGap = 2;
    const actionPad = 4;
    // 一页 4 个操作；超出仍用右侧 ▲▼ 翻页
    const actionH =
      actionPad * 2 +
      EQUIP_ACTION_VISIBLE * btnH +
      (EQUIP_ACTION_VISIBLE - 1) * btnGap;
    const actionTop = softY - actionH - 4;
    const bodyTop = titleY + titleH + 8;
    const bodyBottom = actionTop - 4;
    const rowH = UC3_CELL;
    const lines = buildEquipDetailLines(it);
    const slot = it.equipSlot >= 0 ? it.equipSlot : lookupItemSync(it.itemId)?.equipSlot ?? -1;
    const worn = slot >= 0 ? this.wornBySlot(slot) : null;
    const marks = worn ? buildEquipCompareMarks(it, worn) : null;
    const hasCompare = !!marks && marks.size > 0;
    const fitRows = Math.max(1, Math.floor((bodyBottom - bodyTop) / rowH));
    // 能一页显示全则不分页；否则按可容纳行数翻页
    const rowsPerPage = lines.length <= fitRows ? lines.length || 1 : fitRows;
    const pageCount = Math.max(1, Math.ceil(lines.length / rowsPerPage));
    const page = Math.min(Math.max(0, this.detailPage), pageCount - 1);
    this.detailPage = page;
    const start = page * rowsPerPage;
    const pageLines = lines.slice(start, start + rowsPerPage);

    const compareRightX = x + w - 10;
    let ly = bodyTop;
    for (const line of pageLines) {
      const rows = this.paintEquipLine(line, x + 14, ly, hasCompare ? 80 : 0);
      if (marks) {
        let mark: { text: string; color: number } | undefined;
        if (line.kind === "kv") {
          const name = equipKvAttrName(line.key);
          mark = name ? marks.get(name) : undefined;
        } else if (line.kind === "milestone") {
          mark = marks.get(`ms:${line.unlock}`);
        }
        if (mark) {
          this.addAttrTextRight(mark.text, compareRightX, ly, mark.color, 78);
        }
      }
      ly += rowH * Math.max(1, rows);
    }

    if (pageCount > 1) {
      // With compare column on the right, keep page hint centered so it does not cover deltas.
      this.addLabelCentered(
        `⇦ ${page + 1}/${pageCount} ⇨`,
        hasCompare ? LCD_WIDTH / 2 : LCD_WIDTH / 2 + 70,
        bodyTop + Math.floor(fitRows * rowH * 0.45),
        CBE.ink,
      );
    }

    // Footer action strip
    this.box.rect(x + 4, actionTop, w - 8, actionH).fill({ color: CBE.bgPanel });
    this.box
      .moveTo(x + 8, actionTop)
      .lineTo(x + w - 8, actionTop)
      .stroke({ width: 1, color: CBE.borderDim });

    const acts = this.actionLabels;
    const actStart = Math.max(
      0,
      Math.min(this.actionCursor, Math.max(0, acts.length - EQUIP_ACTION_VISIBLE)),
    );
    const btnW = 72;
    for (let i = 0; i < EQUIP_ACTION_VISIBLE; i++) {
      const label = acts[actStart + i];
      if (!label) break;
      const hi = actStart + i === this.actionCursor;
      const by = actionTop + actionPad + i * (btnH + btnGap);
      const bx = Math.floor((LCD_WIDTH - btnW) / 2);
      this.box
        .roundRect(bx, by, btnW, btnH, 3)
        .fill({ color: CBE.bgDeep })
        .stroke({ width: 1, color: CBE.borderBright });
      if (hi) {
        const cy = by + btnH / 2;
        this.box.poly([bx - 8, cy, bx - 4, cy - 4, bx, cy, bx - 4, cy + 4]).fill({
          color: EQUIP_DETAIL_YELLOW,
        });
        this.box
          .poly([bx + btnW + 8, cy, bx + btnW + 4, cy - 4, bx + btnW, cy, bx + btnW + 4, cy + 4])
          .fill({ color: EQUIP_DETAIL_YELLOW });
      }
      this.addLabelCentered(
        label,
        LCD_WIDTH / 2,
        by + btnH / 2,
        hi ? EQUIP_DETAIL_YELLOW : CBE.ink,
      );
      const absIdx = actStart + i;
      this.rowHits.push(
        attachRowPointerHit(this, bx, by, btnW, btnH, () => {
          this.actionCursor = absIdx;
          this.redraw();
          this.confirm();
        }),
      );
    }

    // Footer ▲▼ (scroll actions when more than one page)
    if (acts.length > EQUIP_ACTION_VISIBLE) {
      const ax = x + w - 18;
      const canUp = this.actionCursor > 0;
      const canDown = this.actionCursor < acts.length - 1;
      const upY = actionTop + actionPad + btnH / 2;
      const downY = actionTop + actionH - actionPad - btnH / 2;
      this.drawScrollArrow(ax, upY, true, canUp);
      this.drawScrollArrow(ax, downY, false, canDown);
      // Graphics alone are not interactive — same tap path as ↑↓ / move().
      const hitW = 22;
      const hitH = 18;
      this.rowHits.push(
        attachRowPointerHit(this, ax - hitW / 2, upY - hitH / 2, hitW, hitH, () => {
          if (!canUp) return;
          this.move(-1);
        }),
      );
      this.rowHits.push(
        attachRowPointerHit(this, ax - hitW / 2, downY - hitH / 2, hitW, hitH, () => {
          if (!canDown) return;
          this.move(1);
        }),
      );
    }
  }

  private paintEquipLine(
    line: EquipPaintLine,
    x: number,
    y: number,
    rightReserve = 0,
  ): number {
    const maxW = Math.max(40, LCD_WIDTH - x - 16 - rightReserve);
    const rowH = UC3_CELL;
    if (line.kind === "text" || line.kind === "milestone") {
      const rows = wrapUiText(line.text, maxW);
      rows.forEach((row, i) => {
        this.addAttrText(row, x, y + i * rowH, line.color, maxW);
      });
      return rows.length;
    }
    // Same-color key+value → one Label (merge); two-tone keeps two Labels.
    if (line.keyColor === line.valueColor) {
      const merged = `${line.key}${line.value}`;
      const rows = wrapUiText(merged, maxW);
      rows.forEach((row, i) => {
        this.addAttrText(row, x, y + i * rowH, line.keyColor, maxW);
      });
      return rows.length;
    }
    const keyRows = wrapUiText(line.key, maxW);
    keyRows.forEach((row, i) => {
      this.addAttrText(row, x, y + i * rowH, line.keyColor, maxW);
    });
    // UC3 16×16 — old 12/7 system-font estimate put yellow values on top of keys.
    const keyW = measureUiTextWidth(line.key);
    const valMax = Math.max(24, maxW - keyW);
    const valRows = wrapUiText(line.value, valMax);
    valRows.forEach((row, i) => {
      this.addAttrText(row, x + keyW, y + i * rowH, line.valueColor, valMax);
    });
    return Math.max(keyRows.length, valRows.length);
  }

  private drawBagInnerDetail(x: number, _y: number, w: number, top: number, bottom: number): void {
    const maxW = Math.max(40, w - 28);
    const flat = this.detailText().flatMap((line) => wrapUiText(line, maxW));
    const maxLines = Math.floor((bottom - top) / UC3_CELL);
    let ly = top;
    for (const line of flat.slice(0, maxLines)) {
      this.addAttrText(line, x + 14, ly, CBE.inkMint, maxW);
      ly += UC3_CELL;
    }
    this.addAttrText("左软=操作  右软=返回", x + 14, ly + 8, CBE.inkLabel);
  }

  private drawBagInnerActions(
    x: number,
    _y: number,
    w: number,
    top: number,
    _bottom: number,
  ): void {
    this.actionLabels.forEach((label, i) => {
      const hi = i === this.actionCursor;
      if (hi) this.box.rect(x + 10, top + i * 20, w - 20, 18).fill({ color: BAG_SELECT });
      this.addAttrText(label, x + 18, top + i * 20 + 2, hi ? CBE.ink : CBE.inkMint);
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, x + 10, top + i * 20, w - 20, 18, () => {
          this.actionCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    });
  }

  /**
   * CBE 属性页（docs/re/2026-08-04-attr-panel-label-value-mismatch.md）：
   * 顶栏胶囊「属性」、黄名、标签左/数值右两列、暗行底、▲▼、右下「返回」。
   * ↑↓ 按整页翻；经验单行。
   */
  private drawStatusPanel(): void {
    const x = 4;
    const y = 2;
    const w = LCD_WIDTH - 8;
    const h = LCD_HEIGHT - 4;
    paintCbePanel(this, this.box, x, y, w, h, CBE.bgPanel);

    const pillW = 72;
    const pillH = 22;
    const pillY = y + 8;
    drawCbeTitlePill(this.box, LCD_WIDTH / 2, pillY, pillW, pillH);
    this.addLabelCentered("属性", LCD_WIDTH / 2, pillY + pillH / 2, CBE.ink);

    const name = this.stats?.name || (this.detailLines[0] ?? "…");
    // Role name: system font (not title UC3).
    const nameLab = new BitmapLabel();
    nameLab.anchor.set(0.5, 0.5);
    nameLab.position.set(Math.round(LCD_WIDTH / 2), Math.round(pillY + pillH + 16));
    void nameLab.setText(name, CBE.inkChat, {
      smooth: false,
      outline: false,
    });
    this.addChild(nameLab);
    this.labels.push(nameLab);

    const rows = this.statusRows();
    const pageCount = Math.max(1, Math.ceil(rows.length / STATUS_VISIBLE_ROWS));
    const page = Math.min(Math.max(0, this.cursorIndex), pageCount - 1);
    const scroll = page * STATUS_VISIBLE_ROWS;

    const softY = y + h - PANEL_BOTTOM_PAD;
    const arrowH = 14;
    const arrowY = softY - arrowH - 2;
    const rowTop = y + 54;
    const listBottom = arrowY - 4;
    const rowH = Math.max(16, Math.floor((listBottom - rowTop) / STATUS_VISIBLE_ROWS));
    const listX = x + 8;
    const listW = w - 16;
    const listH = STATUS_VISIBLE_ROWS * rowH + 2;
    const valueX = listX + STATUS_LABEL_W;

    // Value well (dark) + lighter label strip on the left
    this.box.rect(listX, rowTop - 2, listW, listH).fill({ color: STATUS_ROW_BG });
    this.box
      .rect(listX, rowTop - 2, STATUS_LABEL_W, listH)
      .fill({ color: STATUS_LABEL_BG });

    for (let i = 0; i < STATUS_VISIBLE_ROWS; i++) {
      const row = rows[scroll + i];
      if (row == null) break;
      const ry = rowTop + i * rowH;
      if (i > 0) {
        this.box
          .moveTo(listX + 2, ry)
          .lineTo(listX + listW - 2, ry)
          .stroke({ width: 1, color: 0x2a4a42 });
      }
      if (!row.label && !row.value) continue;
      const ty = ry + Math.floor((rowH - 14) / 2);
      if (row.label) {
        this.addAttrText(row.label, listX + 4, ty, STATUS_LABEL);
      }
      if (row.value) {
        this.addAttrText(row.value, valueX, ty, CBE.ink);
      }
    }

    if (pageCount > 1) {
      attachPageNavButtons({
        host: this,
        hits: this.rowHits,
        box: this.box,
        x: listX,
        y: arrowY,
        w: listW,
        page: page + 1,
        pages: pageCount,
        addLabel: (t, lx, ly, c) => this.addAttrText(t, lx, ly, c),
        onPrev: () => {
          this.cursorIndex = Math.max(0, page - 1);
          this.redraw();
        },
        onNext: () => {
          this.cursorIndex = Math.min(pageCount - 1, page + 1);
          this.redraw();
        },
      });
    }
  }

  /** Small green ▲ / ▼ matching CBE attr footer. */
  private drawScrollArrow(cx: number, cy: number, up: boolean, active: boolean): void {
    const col = active ? CBE.borderBright : CBE.borderDim;
    const s = 5;
    if (up) {
      this.box.poly([cx, cy - s, cx - s, cy + 3, cx + s, cy + 3]).fill({ color: col });
    } else {
      this.box.poly([cx, cy + s, cx - s, cy - 3, cx + s, cy - 3]).fill({ color: col });
    }
  }

  /** CBE label order；经验单行装满. */
  private statusRows(): { label: string; value: string }[] {
    if (!this.stats) {
      const loading = this.detailLines.length
        ? this.detailLines
        : ["正在拉取服务端属性汇总…"];
      return loading.map((t) => ({ label: t, value: "" }));
    }
    const s = this.stats;
    const jobName =
      s.job === 1 ? "天机" : s.job === 2 ? "幻剑" : s.job === 3 ? "鬼道" : "无";
    const expNext = formatSignedU32(s.expNext);
    return [
      { label: "称号", value: s.title || "无" },
      { label: "等级", value: String(s.level) },
      { label: "职业", value: jobName },
      { label: "配偶", value: s.spouse || "无" },
      { label: "帮派", value: s.guild || "无帮派" },
      { label: "魅力", value: String(s.charm) },
      { label: "生命", value: `${s.hp}/${s.hpMax}` },
      { label: "法力", value: `${s.mp}/${s.mpMax}` },
      { label: "经验", value: `${s.exp}/${expNext}` },
      { label: "力量", value: String(s.strength) },
      { label: "敏捷", value: String(s.agility) },
      { label: "智慧", value: String(s.wisdom) },
      { label: "物攻", value: String(s.attack) },
      { label: "护甲", value: String(s.armor) },
      { label: "躲闪", value: String(s.dodge) },
      { label: "命中", value: String(s.hit) },
      { label: "暴击", value: String(s.crit) },
      { label: "抗性", value: String(s.resist) },
      { label: "反伤", value: `${s.reflectPct ?? 0}%` },
      { label: "连击", value: `${s.comboPct ?? 0}%` },
      { label: "复活", value: `${s.revivePct ?? 0}%` },
      {
        label: "活力",
        value: `${s.vitality ?? 0}/${s.vitalityMax ?? 0}`,
      },
    ];
  }

  private listHint(): string {
    if (this.mode === "recharge_amount") return "输入赞助金额";
    if (this.mode === "recharge_pay") return "选择支付方式";
    if (this.mode === "actions") return "选择操作";
    if (this.mode === "detail") return "详细信息";
    if (this.usesCategoryTabs()) {
      const tabs = this.categoryTabs();
      const tab = tabs[this.categoryIndex] ?? tabs[0];
      const total =
        this.kind === "mall" ? this.filteredMall().length : this.filteredBag().length;
      const pageSize = this.kind === "bag" ? BAG_VISIBLE_ROWS : PAGE_SIZE;
      const pages = Math.max(1, Math.ceil(Math.max(total, 1) / pageSize));
      const page = total ? Math.floor(this.cursorIndex / pageSize) + 1 : 1;
      return `${tab?.label ?? ""} ${page}/${pages}页 ←→分类`;
    }
    return hintOf(this.kind);
  }

  private drawList(x: number, y: number, w: number, listTop = y + 44): void {
    const lines = this.listLines();
    const paged = this.usesCategoryTabs() || this.kind === "skill";
    const pageSize = PAGE_SIZE;
    const pageStart =
      paged && lines.length
        ? Math.floor(this.cursorIndex / pageSize) * pageSize
        : 0;
    const pageLines = paged
      ? lines.slice(pageStart, pageStart + pageSize)
      : lines.slice(0, Math.min(14, lines.length));
    const localCursor = paged
      ? this.cursorIndex - pageStart
      : this.cursorIndex;

    let listY = listTop;
    for (let i = 0; i < pageLines.length; i++) {
      const hi = i === localCursor;
      const row = pageLines[i]!;
      const ink = hi ? CBE.ink : row.color;
      const lineCount = row.right ? 1 : wrapChars(row.text).length;
      const effH = Math.max(16, lineCount * 14 + 1);
      if (hi) this.box.rect(x + 4, listY, w - 8, effH - 1).fill({ color: CBE.select });
      const ty = listY + 1;
      if (row.right) {
        this.addAttrText(row.text, x + 8, ty, ink);
        this.addAttrTextRight(row.right, x + w - 8, ty, ink, 80);
      } else {
        this.addLabel(row.text, x + 8, ty, ink);
      }
      const absIdx = pageStart + i;
      this.rowHits.push(
        attachRowPointerHit(this, x + 4, listY, w - 8, effH - 1, () => {
          this.cursorIndex = absIdx;
          this.redraw();
          this.confirm();
        }),
      );
      listY += effH;
    }

    const footY = y + LCD_HEIGHT - SOFTKEY_H - 64 - 18;
    if (paged && lines.length > pageSize) {
      const pages = Math.max(1, Math.ceil(lines.length / pageSize));
      const page = Math.floor(pageStart / pageSize) + 1;
      attachPageNavButtons({
        host: this,
        hits: this.rowHits,
        box: this.box,
        x: x + 8,
        y: footY - 22,
        w: w - 16,
        page,
        pages,
        addLabel: (t, lx, ly, c) => this.addAttrText(t, lx, ly, c),
        onPrev: () => {
          this.cursorIndex = Math.max(0, pageStart - pageSize);
          this.redraw();
        },
        onNext: () => {
          this.cursorIndex = Math.min(lines.length - 1, pageStart + pageSize);
          this.redraw();
        },
      });
    }
    if (this.kind === "mall" && this.mall && this.mode === "list") {
      this.addLabel(`酷宝 ${this.mall.coolmoney}`, x + 10, footY, CBE.inkTip);
      const rechargeX = x + w - 48;
      this.addLabel("赞助", rechargeX, footY, CBE.inkTipGreen);
      const hit = new Container();
      hit.position.set(rechargeX - 4, footY - 2);
      hit.hitArea = new Rectangle(0, 0, 44, 18);
      hit.eventMode = "static";
      hit.cursor = "pointer";
      hit.on("pointertap", () => this.beginMallRecharge());
      this.addChild(hit);
      this.hitSprites.push(hit);
    } else if (this.usesCategoryTabs() && this.mode === "list") {
      const total =
        this.kind === "mall" ? this.filteredMall().length : this.filteredBag().length;
      this.addLabelCentered(`共 ${total} 件`, x + w / 2, footY + 6, CBE.inkTip);
    }
  }

  private listLines(): { text: string; color: number; right?: string }[] {
    if (this.kind === "bag" || this.kind === "enhance") {
      const filtered = this.filteredBag();
      if (!filtered.length) {
        return [{ text: this.bagItems.length ? "本分类为空" : "背包为空", color: CBE.inkMint }];
      }
      return filtered.map((it) => ({
        text: formatBagLine(it),
        color: qualityColor(it.quality),
      }));
    }
    if (this.kind === "equip") {
      return WORN_SLOT_NAMES.map((name, slot) => {
        const it = this.wornBySlot(slot);
        if (!it) return { text: `${name}：空`, color: CBE.inkMint };
        return {
          text: `${name}：${formatBagLine(it)}`,
          color: qualityColor(it.quality),
        };
      });
    }
    if (this.kind === "skill") {
      if (!this.skills.length) return [{ text: "未学会仙术（等 12/1）", color: CBE.inkMint }];
      return this.skills.map((sk) => ({
        text: `${sk.name}${sk.mpCost != null ? ` MP${sk.mpCost}` : ""}`,
        color: CBE.inkMint,
      }));
    }
    if (this.kind === "mall") {
      const goods = this.filteredMall();
      if (!goods.length) {
        const raw = this.mall?.goods.length ?? 0;
        return [
          {
            text: raw ? "本分类为空" : "商城加载中…",
            color: CBE.inkMint,
          },
        ];
      }
      return goods.map((g) => ({
        text: g.name,
        right: `${g.price}酷宝`,
        color: qualityColor(g.quality),
      }));
    }
    if (this.kind === "status") {
      return [{ text: "查看人物信息", color: CBE.inkMint }];
    }
    return [{ text: "(未实现)", color: CBE.inkMint }];
  }

  private drawDetail(x: number, y: number, w: number): void {
    const maxW = Math.max(40, w - 16);
    const lines = this.detailText().flatMap((line) => wrapUiText(line, maxW));
    let ly = y + 46;
    for (const line of lines.slice(0, 24)) {
      this.addLabel(line, x + 8, ly, CBE.inkMint);
      ly += UC3_CELL;
    }
    this.addLabel(
      "左软=操作  右软=返回",
      x + 8,
      ly + 8,
      CBE.inkLabel,
    );
  }

  private detailText(): string[] {
    if (this.selected && isBag(this.selected)) {
      const it = this.selected;
      return [
        it.name,
        `数量 ${it.count}  seq ${it.seq}`,
        it.kind === "equip" ? `强化 +${it.enhance}  品质 ${it.quality}` : `品质 ${it.quality}`,
        it.desc ? it.desc : "",
        `id ${it.itemId}`,
      ].filter(Boolean);
    }
    if (this.selected && isGoods(this.selected)) {
      const g = this.selected;
      const lines = [g.name, `价格 ${g.price} 酷宝`];
      // Flask wire stock = DSH 堆叠数, not mall inventory.
      if (!isReservoirFlaskItemId(g.itemId)) lines.push(`库存 ${g.stock}`);
      lines.push(`id ${g.itemId}`);
      return lines;
    }
    if (this.selected && isSkill(this.selected)) {
      const sk = this.selected;
      const desc = sk.desc && sk.desc !== "-" ? sk.desc : "";
      return [
        sk.name,
        desc || "（无技能介绍）",
        sk.mpCost != null ? `耗蓝 ${sk.mpCost}` : "",
        this.inBattle
          ? "战斗中可施放"
          : this.selected && isSkill(this.selected) && isMapHealSkill(this.selected)
            ? "地图可施放回血"
            : "地图上无法施放仙术",
      ].filter(Boolean);
    }
    return this.detailLines.length ? this.detailLines : ["(无详细)"];
  }

  private drawActionList(x: number, y: number, w: number): void {
    this.actionLabels.forEach((label, i) => {
      const hi = i === this.actionCursor;
      if (hi) this.box.rect(x + 4, y + 46 + i * 18, w - 8, 17).fill({ color: CBE.select });
      this.addLabel(label, x + 12, y + 48 + i * 18, hi ? CBE.ink : CBE.inkMint);
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(this, x + 4, y + 46 + i * 18, w - 8, 17, () => {
          this.actionCursor = idx;
          this.redraw();
          this.confirm();
        }),
      );
    });
  }

  private addLabel(text: string, x: number, y: number, color: number): void {
    const t = new BitmapLabel();
    t.eventMode = "none";
    t.position.set(x, y);
    void t.setText(text, color, {
      outline: false,
      maxWidth: LCD_WIDTH - 36,
      wordWrap: false,
    });
    this.addChild(t);
    this.labels.push(t);
  }

  /** Attr-panel / list rows — plain fill, no stroke (clearest on LCD stretch). */
  private addAttrText(
    text: string,
    x: number,
    y: number,
    color: number,
    maxWidth = LCD_WIDTH - x - 16,
  ): void {
    const t = new BitmapLabel();
    t.eventMode = "none";
    t.position.set(x, y);
    void t.setText(text, color, {
      outline: false,
      maxWidth,
      // Caller owns wrapping (wrapUiText); avoid JS+CSS double-wrap overlap.
      wordWrap: false,
    });
    this.addChild(t);
    this.labels.push(t);
  }

  private addAttrTextRight(
    text: string,
    rightX: number,
    y: number,
    color: number,
    maxWidth = 96,
  ): void {
    const t = new BitmapLabel();
    t.eventMode = "none";
    t.anchor.set(1, 0);
    t.position.set(rightX, y);
    void t.setText(text, color, {
      outline: false,
      maxWidth,
      wordWrap: false,
    });
    this.addChild(t);
    this.labels.push(t);
  }

  private addLabelCentered(text: string, cx: number, cy: number, color: number): void {
    const t = new BitmapLabel();
    t.eventMode = "none";
    t.anchor.set(0.5, 0.5);
    t.position.set(Math.round(cx), Math.round(cy));
    // Follow menu font preference (UC3 packs or CN Medium).
    void t.setText(text, color, {
      smooth: false,
      outline: false,
    });
    this.addChild(t);
    this.labels.push(t);
  }
}

function cultivationBookDisplayName(it: BagItem): string {
  if (!isCultivationBookItem(it.itemId) || !(it.bookLevel && it.bookLevel > 0)) {
    return it.name;
  }
  const base = lookupItemSync(it.itemId)?.name ?? "修炼天书";
  return formatCultivationBookName(base, it.bookLevel, it.bookFull);
}

function formatBagLine(it: BagItem): string {
  const wash = equipHasWashedAttr(it) ? "[洗]" : "";
  const plus = it.kind === "equip" && it.enhance > 0 ? `(+${it.enhance})` : "";
  return `${cultivationBookDisplayName(it)}${wash}${plus} ×${it.count}`;
}

function formatBagName(it: BagItem): string {
  const wash = equipHasWashedAttr(it) ? "[洗]" : "";
  const plus = it.kind === "equip" && it.enhance > 0 ? `(+${it.enhance})` : "";
  return `${cultivationBookDisplayName(it)}${wash}${plus}`;
}

/** Wrap description wells at {@link UI_WRAP_CHARS} per line. */
function wrapUc3Lines(text: string, _maxWidth: number): string[] {
  return text.split("\n").flatMap((p) => wrapChars(p));
}

/** CBE / admin：1金=10000铜，1银=100铜. */
function splitCopperMoney(money: number): { gold: number; silver: number; copper: number } {
  const m = money >>> 0;
  return {
    gold: Math.floor(m / 10000),
    silver: Math.floor(m / 100) % 100,
    copper: m % 100,
  };
}

function isBag(v: unknown): v is BagItem {
  return !!v && typeof v === "object" && "itemId" in v && "seq" in v && "count" in v;
}
function isGoods(v: unknown): v is ShopGoods {
  return !!v && typeof v === "object" && "price" in v && "itemId" in v && !("seq" in v);
}
function isSkill(v: unknown): v is BattleSkillRow {
  return !!v && typeof v === "object" && "id" in v && "name" in v && !("itemId" in v);
}

function isHealSkill(sk: BattleSkillRow): boolean {
  return /清风|回血|疗|愈|生息|甘露/.test(sk.name) || sk.id === 211 || sk.id === 11 || sk.id === 301;
}

function actionKey(label: string): SystemAction | string {
  switch (label) {
    case "使用":
      return "use";
    case "合成":
      return "combine";
    case "批量合成":
      return "gem_batch_combine";
    case "批量开启":
      return "batch_open";
    case "装备":
      return "equip";
    case "卸下":
    case "卸载":
      return "unequip";
    case "替换":
      return "swap";
    case "强化":
      return "enhance";
    case "洗练":
      return "wash";
    case "重铸":
      return "recast";
    case "提炼":
      return "extract";
    case "镶嵌":
      return "gem_embed";
    case "拆宝石":
      return "gem_remove";
    case "兑换":
      return "gem_exchange";
    case "换宝石":
      return "gem_from_crystal";
    case "丢弃":
      return "discard";
    case "拍卖":
      return "auction";
    case "购买":
      return "buy";
    case "支付宝":
      return "recharge_alipay";
    case "微信":
      return "recharge_wechat";
    case "施放":
    case "回血施放":
      return "skill_use";
    case "遗忘":
    case "遗忘(5金)":
      return "skill_forget";
    case "回血(战斗中)":
      return "refresh";
    default:
      return "refresh";
  }
}

function titleOf(k: SystemKind): string {
  switch (k) {
    case "bag":
      return "背包物品";
    case "equip":
      return "装备信息";
    case "quest":
      return "任务列表";
    case "mall":
      return "商城";
    case "enhance":
      return "强化";
    case "team":
      return "组队";
    case "skill":
      return "技能列表";
    case "status":
      return "属性";
  }
}

function hintOf(k: SystemKind): string {
  switch (k) {
    case "equip":
      return "每次打开 · 1/7/22";
    case "skill":
      return "已学仙术 · 服务端 12/1";
    case "status":
      return "服务端汇总 · 基础+装备";
    default:
      return "";
  }
}

/** CBE 属性页经验次级：大阈值按有符号 i32 显示（与截图一致）. */
function formatSignedU32(v: number): string {
  const n = v >>> 0;
  if (n >= 0x80000000) return String(n - 0x100000000);
  return String(n);
}
