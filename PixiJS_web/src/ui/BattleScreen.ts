import { Container, Graphics, Sprite, Texture } from "pixi.js";
import {
  ACTOR_DISPLAY_SCALE,
  actorSpriteScale,
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  paintCbePanel,
  preloadCbeFlowerCorners,
} from "./constants";
import { BitmapLabel, wrapChars } from "./BitmapLabel";
import { preferSystemFont } from "../resources/systemFont";
import type { BattleState, BattleSide } from "../net/protocol";
import {
  battleDisplayToWire,
  battleTargetWire,
  battleUnitForWire,
} from "../net/protocol";
import type { BattleActionUpdate, BattleSettlement, HangupCombatinfo } from "../net/protocolExtras";
import {
  actorResourceForJobSex,
  battleFootAnchorX,
  composeBattlePose,
  composeMapActor,
  composePartStacked,
  loadActor,
  type BattlePose,
  type LoadedActor,
} from "../resources/actor";
import { resolveBattleSce } from "../resources/battleScene";
import { eidolonActorForIndex } from "../resources/eidolon";
import { lookupItemSync } from "../resources/itemCatalog";
import { loadSceneMap, resourceExists, type LoadedScene } from "../resources/loader";
import { rgbaToTexture } from "../render/rgbaTexture";
import {
  createSolidMaskSprite,
  remountSolidMaskSprite,
  nudgeTextureSource,
  replaceGraphics,
} from "../render/gpuResume";
import type { BattleSkillRow, SkillTargetKind } from "../resources/skill";
import { skillNeedsTargetPick } from "../resources/skill";
import type { BagItem } from "../net/protocolExtras";
import { isBattleMedicineOrFlask } from "../net/protocolExtras";
import {
  loadUiCrop,
  tryLoadUiTexture,
  uiSprite,
  DigitStrip,
} from "./UiAssets";
import { BattlePartyHud, BATTLE_PARTY_HUD_H } from "./BattlePartyHud";
import { BattleEnemyHud, type EnemyHudAnchor } from "./BattleEnemyHud";
import { getEnemyHpBarPlacement } from "../resources/enemyHpBarPref";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";
import { attachPageNavButtons, preloadPageNavTextures } from "./pageNav";
import {
  paintHangupPanel,
  preloadHangupPanelTextures,
} from "./hangupSettleUi";
import {
  paintBattleSettlePanel,
  parseSettleDropLines,
} from "./battleSettleUi";
import {
  MENU2_CAP_H,
  MENU2_CENTER_W,
  MENU2_LEFT_W,
  MENU2_RIGHT_W,
  MENU2_WHITE_FILL,
  addMenuScrollBackground,
  menu2ContentRect,
  preloadMenuScrollTexture,
} from "./menuScrollUi";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  type PanelTipState,
} from "./panelTip";

/** Eidolon frames can be 170px+; clamp so FX cannot dominate the 240× stage. */
const FX_MAX_W = 96;
const FX_MAX_H = 96;
/**
 * Combat pose part step (ms). mmBattle PicLib advances strips on the battle
 * tick; 100ms matches the common feature-phone cadence (CBM immediates cluster
 * around 100/160 — prefer 100 so cast/FX/death stay in one beat).
 */
const UNIT_FRAME_MS = 100;
/** Cast windup frames before eidolon overlaps (CBE AnimEffect starts mid-cast). */
const CAST_WINDUP_FRAMES = 2;
/** Brief hit tint — CBE flash, not a long pose lock that delays death. */
const HIT_FLASH_MS = 200;
/** Eidolon / f_death part step when duration is frame-derived. */
const FX_FRAME_MS = 100;
/** After damage floats: hold before starting death pose. */
const AFTER_DAMAGE_BEFORE_DEATH_MS = 200;
/** Extra pause after the prior hit fully settles, before gem 连击 starts. */
const BEFORE_COMBO_ACTION_MS = 200;
/** actioninfo child: 5 = gem combo, 7 = combo that also crit. */
function childFlagIsCombo(flag: number): boolean {
  return flag === 5 || flag === 7;
}
/** After death pose: brief gray freeze so splash is not the first death cue. */
const BEFORE_DEATH_SPLASH_MS = 280;
/** Monster death splash frame step. */
const DEATH_SPLASH_FRAME_MS = 130;
/** Monster corpse fade (non-blocking — must not gate 4/7 settle panel). */
const DEATH_FADE_MS = 420;
/** Brief solid hold after splash starts, before fade. */
const DEATH_FADE_DELAY_MS = 80;
/** Battle idle/death flap — same slow cadence for player + all monsters (鼠尾节奏). */
const BATTLE_IDLE_FRAME_MS = 360;
/** Cap one battle tick so a hitch cannot skip many pose parts at once. */
const BATTLE_DT_CLAMP_MS = 50;

const MONSTER_RED = 0xff8080;
const SETTLE_GOLD = 0xffd080;
/** Match hangup/battle settle Songti row size (pickupimage1 ~11px). */
const SETTLE_FS = 11;
const HEAL_GREEN = 0x50e080;
const TOP_H = 36;
/** Compact party vitals strip — flush to LCD bottom (HTML softkeys are outside). */
const BOTTOM_H = BATTLE_PARTY_HUD_H;
/** Battle log sits above party HUD — right of portrait so face stays visible. */
const BOTTOM_LOG_X = 36;
const BOTTOM_LOG_MAX_W = LCD_WIDTH - BOTTOM_LOG_X - 8;
/**
 * Bottom battle log text (combat narrative, 施展…, menu tips).
 * Off for now. Flip to true to show the log again.
 */
const SHOW_BATTLE_COMBAT_LOG = false;
const STAGE_TOP = TOP_H;
const STAGE_BOTTOM = LCD_HEIGHT - BOTTOM_H;
const UNIT_LEFT_X = 22;
const UNIT_RIGHT_X = 218;
const UNIT_FEET_Y = STAGE_TOP + Math.floor((STAGE_BOTTOM - STAGE_TOP) * 0.64);
/** Horizontal push of the middle slot toward center (screenshot chevron). */
const FORMATION_FORWARD = 12;
/** Vertical gap between formation rows. */
const FORMATION_ROW = 30;

/**
 * Screenshot chevron: top/bottom stay back; middle slot steps toward center.
 * Left faces right (middle +x); right faces left (middle -x).
 */
function formationSlot(
  side: "left" | "right",
  index: number,
  count: number,
): { x: number; y: number } {
  const n = Math.max(1, count);
  const i = Math.max(0, Math.min(index, n - 1));
  const baseX = side === "left" ? UNIT_LEFT_X : UNIT_RIGHT_X;
  const totalH = (n - 1) * FORMATION_ROW;
  const y = UNIT_FEET_Y - totalH / 2 + i * FORMATION_ROW;
  const mid = (n - 1) / 2;
  const maxDist = mid || 1;
  const fwdAmt = FORMATION_FORWARD * (1 - Math.abs(i - mid) / maxDist);
  const x = side === "left" ? baseX + fwdAmt : baseX - fwdAmt;
  return { x, y };
}

export type BattleSkill = BattleSkillRow;

/** Screenshot order: 攻击 / 仙术 / 物品 / 逃跑 / 自动 / 查看 */
const MENU_ITEMS = ["攻击", "仙术", "物品", "逃跑", "自动", "查看"] as const;
/** Battle scroll row pitch inside the white list box. */
const BATTLE_MENU_ROW_H = 16;
/** Green band above white box for 行动 / 仙术 / 物品 title. */
const BATTLE_MENU_TITLE_BAND = 18;
/** Padding below white box before bottom roller. */
const BATTLE_MENU_BOTTOM_PAD = 8;
const BATTLE_MENU_SEL_FILL = 0x505850;
/** CBE 仙术/物品选择面板 — 暗色列表可见行数（+ 翻页按钮）. */
const BATTLE_PICK_VISIBLE = 6;
const BATTLE_PICK_ROW_H = 18;
const BATTLE_PICK_PANEL_X = 12;
const BATTLE_PICK_PANEL_W = LCD_WIDTH - 24;
const BATTLE_PICK_TITLE_H = 26;
const BATTLE_PICK_NAV_H = 16;
const BATTLE_PICK_DESC_LINES = 4;
type MenuItem = (typeof MENU_ITEMS)[number];

/** PvE / 普通切磋默认回合秒数。 */
const TURN_SECONDS = 16;
/** 比武：与服端 CBE_DUEL_ROUND_COMMIT_TIMEOUT_MS 默认 20s 对齐。 */
const TURN_SECONDS_ARENA = 20;

export type BattleBagRow = {
  seq: number;
  itemId: number;
  name: string;
  count: number;
};
/** Spar: no flee entry. */
const MENU_ITEMS_SPAR: readonly MenuItem[] = [
  "攻击",
  "仙术",
  "物品",
  "自动",
  "查看",
];
/** 比武：禁逃跑、禁自动、禁物品。 */
const MENU_ITEMS_ARENA: readonly MenuItem[] = [
  "攻击",
  "仙术",
  "查看",
];

export type BattleVisualContext = {
  worldScene?: string;
  playerActor?: string;
  leftActors?: string[];
  jobIndex?: number;
  sex?: number;
  playerName?: string;
  /** Observer role id — vitals / local seat when party order ≠ self-first. */
  selfId?: number;
};

type UnitView = {
  root: Container;
  sprite: Sprite | null;
  side: BattleSide;
  loaded: LoadedActor | null;
  /** Resource used for loadActor (for FX sheet reload). */
  actorName: string;
  /** true = face right (monsters on left). */
  facingRight: boolean;
  phase: number;
  homeX: number;
  homeY: number;
  feetX: number;
  feetY: number;
  pose: BattlePose;
  /** Hold non-idle pose until this performance.now(). */
  poseUntil: number;
  dead: boolean;
  hidden: boolean;
};

/**
 * mmBattle stage: battle map `b_*.sce`, screenshot HUD, actioninfo FX playback.
 */
export class BattleScreen extends Container {
  private mapHost = new Container();
  private mapSprite: Sprite | null = null;
  /**
   * Single clipped stage band (map + units + fx), same pattern as SceneView.clip.
   * Do NOT share one Graphics as mask across multiple hosts — Pixi 8 StencilMask
   * toggles mask.includeInBuild; replaceGraphics mid-restore left map/units empty
   * after the town-mask fix (docs/re/2026-08-30-pixi-battle-black-recover-incomplete.md).
   */
  private stageHost = new Container();
  /** Sprite mask (not Graphics) — remounts after webglcontextrestored. */
  private stageMask: Sprite;
  private unitsHost = new Container();
  private fxHost = new Container();
  private menuHost = new Container();
  private settleHost = new Container();
  /** Center toast (hangup add-time / battle tips). */
  private toast: PanelTipState = createPanelTipState();
  private floatHost = new Container();
  private topHost = new Container();
  private bottomHost = new Container();
  /** Top-right 「自动/挂机战斗中」+ 关闭. */
  private autoBannerHost = new Container();
  private autoBannerBuilt = false;
  private autoBannerArtGen = 0;
  /** autobattle_words.gif — auto-fight banner copy. */
  private autoBannerWords: Sprite | null = null;
  /** Hangup has no words sheet; UC3 fallback beside the close tile. */
  private autoBannerHangupTip: BitmapLabel | null = null;
  private autoActive = false;
  /** Hangup (client collide-loop) — hide operate scroll while armed. */
  private hangupActive = false;
  /** Client-owned hangup settle tally when wire has no combatinfo. */
  private hangupTally: HangupCombatinfo = {
    completed: 0,
    maximum: 64,
    totalExp: 0,
    totalGold: 0,
    totalHpRecovered: 0,
    totalMpRecovered: 0,
  };
  private hangupTallyArmed = false;
  private partyHud = new BattlePartyHud();
  /** Top-left enemy name + HP (under title bar). */
  private enemyHud = new BattleEnemyHud();
  private log = new BitmapLabel();
  private topTitle = new BitmapLabel();
  private timerDigits: DigitStrip | null = null;
  private timerIcon: Sprite | null = null;
  /** CBE `-num.gif` / `+num.gif` sheets for battle floats. */
  private hurtDigits: DigitStrip | null = null;
  private healDigits: DigitStrip | null = null;

  private state: BattleState | null = null;
  private visuals: BattleVisualContext = {};
  private units: UnitView[] = [];
  private skills: BattleSkill[] = [];
  private skillCursor = 0;
  private menuCursor = 0;
  private menuMode: "root" | "skill" | "item" | "target" = "root";
  private menuOpen = true;
  private awaitingExit = false;
  /** PvE death: wait for 1/7/14 choice (revival stone / ordinary revive). */
  private awaitingDeathPrompt = false;
  private deathPromptBusy = false;
  private deathPromptHasStone = false;
  private deathPromptCursor = 0;
  /** True while actioninfo FX is playing — keep menu hidden and ignore ops. */
  private playbackBusy = false;
  /** Queued playActionScript chain — never run two 4/6 scripts in parallel. */
  private playbackChain: Promise<void> = Promise.resolve();
  /** In-flight + queued applyUpdate play jobs (menu/auto stay locked until 0). */
  private playbackDepth = 0;
  /**
   * Shared round: local 4/2 already sent; waiting for 4/6 playback
   * (team merge or spar duel_round_defer). No menu / ops until applyUpdate.
   */
  private awaitingRound = false;
  /** Timed 比武 duel: no [自动], 20s NA timeout. */
  private arenaDuel = false;
  private tickAcc = 0;
  private idleTickAcc = 0;
  /** performance.now() of last battle tick — wall-clock, not ticker.deltaMS. */
  private lastAnimTick = 0;
  private timerSec = TURN_SECONDS;
  private timerAcc = 0;
  private battleMapFile = "";
  private battleMapWorld = "";
  /**
   * CPU copy of the last battle map pixels. After webglcontextrestored,
   * BufferImageSource.source.update() often leaves a black sprite; rebuild
   * from this buffer instead of reusing the dead GPU texture.
   */
  private mapPixels: {
    width: number;
    height: number;
    rgba: Uint8ClampedArray;
  } | null = null;
  private playbackGen = 0;
  /** Drop stale async map loads when show() races. */
  private mapLoadGen = 0;
  /** Drop stale async settlement paints when a newer settle / exit arrives. */
  private settlePaintGen = 0;
  /** Auto-dismiss settle panel (2.5s); cleared on confirm / hide. */
  private settleCloseTimer: ReturnType<typeof setTimeout> | null = null;
  /** Prevent double onSettleConfirm from timer + 关闭 / softkey. */
  private settleConfirmBusy = false;
  /** Reuse pose textures — destroy/recreate every tick was thrashing GPU mid-attack. */
  private poseTexCache = new Map<string, Texture>();
  private poseTexKey = new WeakMap<Sprite, string>();
  /** Living-target wires for pick mode (enemy or ally). */
  private targetWires: number[] = [];
  private targetCursor = 0;
  /** Pending skill operate while picking; null = attack / item pick. */
  private pendingOperate: number | null = null;
  private pendingSkillName = "";
  /** Pending backpack seq while picking ally/self for 4/3. */
  private pendingItem: { seq: number; name: string } | null = null;
  private selectedTargetWire: number | null = null;
  private targetMark = new Graphics();
  private items: BattleBagRow[] = [];
  private itemCursor = 0;
  /** Menu / skill / item / target pointer hits; cleared each redrawMenu. */
  private rowHits: Container[] = [];

  onAttack: (() => void) | null = null;
  onSkill: ((operate: number) => void) | null = null;
  onFlee: (() => void) | null = null;
  /** Battle medicine/flask use — WT 1/4/3 via session. */
  onItemUse: ((seq: number) => void) | null = null;
  onSettleConfirm: (() => void) | null = null;
  /**
   * Death prompt choice: 1=复活石, 2=普通复活.
   * Evidence: docs/re/2026-07-23-battle-death-revival-stone.md (WT 1/7/14).
   */
  onDeathPromptChoice: ((result: 1 | 2) => void) | null = null;
  /** Fired when death prompt is shown (softkeys / chrome). */
  onDeathPromptShown: (() => void) | null = null;
  /**
   * Toggle in-battle [自动] (separate from world 挂机).
   * Returns the new armed state for the log tip.
   */
  onAutoToggle: (() => boolean) | null = null;
  /** Fired when banner 关闭 or a manual battle op disarms [自动]. */
  onAutoCancel: (() => void) | null = null;
  /** Fired when banner 关闭 while hangup owns the fight — local stop. */
  onHangupCancel: (() => void) | null = null;
  /** Hangup settle「补充时间」— main uses bag 828 / tips mall. */
  onHangupAddTime: (() => void | Promise<void>) | null = null;
  /** Fetch 修/7/31 remain before hangup settle paints 有效时间. */
  onHangupInsightRefresh: (() => Promise<number>) | null = null;
  /** Fired when a round finishes playback and [自动] is still armed. */
  onAutoReady: (() => void) | null = null;
  /**
   * Playback finished and battle should leave to world (4/8 / finished without
   * a settle panel). Settlement uses onSettleConfirm instead.
   */
  onExitReady: (() => void) | null = null;
  /**
   * Bottom party vitals / world HUD sync — fired when HP/MP actually lands
   * during actioninfo playback (not at 4/6 packet arrival).
   */
  onVitalsChanged: (() => void) | null = null;

  constructor() {
    super();
    // Clip stage art so oversized eidolon / map cover cannot spill into HUD.
    // Sprite mask (BufferImageSource) remounts after context restore; Graphics
    // masks stay empty on WebView and clip the whole fight band to black.
    const stageH = STAGE_BOTTOM - STAGE_TOP;
    this.stageMask = createSolidMaskSprite(LCD_WIDTH, stageH);
    this.stageMask.y = STAGE_TOP;
    this.stageHost.mask = this.stageMask;
    this.stageHost.addChild(this.stageMask);
    this.stageHost.addChild(this.mapHost);
    this.stageHost.addChild(this.unitsHost);
    // Keep target cursor under FX (old sibling order) but inside the clip band.
    this.stageHost.addChild(this.targetMark);
    this.stageHost.addChild(this.fxHost);
    this.addChild(this.stageHost);
    this.addChild(this.menuHost);
    this.addChild(this.floatHost);
    this.addChild(this.settleHost);
    this.addChild(this.topHost);
    this.addChild(this.bottomHost);
    // Above stage map so enemy vitals stay readable over the battlefield.
    this.addChild(this.enemyHud);
    // Above stage/menu so the 关闭 hit target stays tappable.
    this.addChild(this.autoBannerHost);
    this.buildChrome();
    preloadCbeFlowerCorners(() => {
      if (
        this.visible &&
        this.menuOpen &&
        (this.menuMode === "skill" || this.menuMode === "item")
      ) {
        this.redrawMenu();
      }
    });
  }

  isAutoBattleActive(): boolean {
    return this.autoActive;
  }

  isHangupActive(): boolean {
    return this.hangupActive;
  }

  /** Pixi panel toast — works while world HUD is hidden in battle. */
  showTip(text: string, ms = 1800): void {
    if (!this.visible) return;
    const msg = text.trim();
    if (!msg) return;
    showPanelTip(this.toast, msg, () => {
      if (this.visible) drawPanelTipOverlay(this, this.toast);
    }, ms);
  }

  /** True when the action scroll may be shown (not [自动] / hangup). */
  private operateMenuAllowed(): boolean {
    return !this.autoActive && !this.hangupActive;
  }

  /**
   * Sync with session.hangupActive. Hangup hides the operate menu for the
   * whole fight (CBE 4/11 style); stopping hangup restores it when the seat
   * can act.
   */
  setHangupActive(on: boolean): void {
    this.ensureAutoBanner();
    if (on && !this.hangupActive) {
      /* Keep session totals across hide()/next fight; only reset on fresh arm. */
      if (!this.hangupTallyArmed) this.resetHangupTally();
      preloadHangupPanelTextures();
    }
    if (!on) {
      this.hangupTallyArmed = false;
    }
    this.hangupActive = on;
    this.paintAutoBannerTip();
    this.refreshAutoBannerVisibility();
    if (on) {
      this.menuOpen = false;
      this.redrawMenu();
      return;
    }
    if (
      this.awaitingExit ||
      this.awaitingDeathPrompt ||
      this.playbackBusy ||
      this.awaitingRound ||
      this.autoActive
    ) {
      return;
    }
    this.menuMode = "root";
    this.menuOpen = true;
    this.paintLocalActingTitle();
    this.redrawMenu();
  }

  /**
   * Sync banner + menu with session.autoBattleActive.
   * When on, hides the action scroll; when off and the seat can act, reopens it.
   */
  setAutoBattleActive(on: boolean): void {
    this.ensureAutoBanner();
    this.autoActive = on;
    if (on) preloadHangupPanelTextures();
    this.paintAutoBannerTip();
    this.refreshAutoBannerVisibility();
    if (on) {
      // Always hide the scroll while armed — even mid-playback / await.
      this.menuOpen = false;
      this.redrawMenu();
      return;
    }
    if (
      this.awaitingExit ||
      this.awaitingDeathPrompt ||
      this.playbackBusy ||
      this.awaitingRound ||
      this.hangupActive
    ) {
      return;
    }
    this.menuMode = "root";
    this.menuOpen = true;
    this.paintLocalActingTitle();
    this.redrawMenu();
  }

  /** Banner 关闭 — disarm [自动] and return to manual menu when possible. */
  closeAutoBattle(): void {
    if (!this.autoActive) return;
    this.setAutoBattleActive(false);
    void this.log.setText("自动战斗关闭", CBE.inkTip);
    this.onAutoCancel?.();
  }

  /**
   * Banner 关闭 while hangup — clear local hangup UI then session.stopHangup.
   * Pixi hangup is client-only (no 2/10 stop request).
   */
  closeHangup(): void {
    if (!this.hangupActive) return;
    this.setHangupActive(false);
    void this.log.setText("已停止挂机", CBE.inkTip);
    this.onHangupCancel?.();
  }

  setVisualContext(ctx: BattleVisualContext): void {
    this.visuals = ctx;
    this.partyHud.setIdentity(ctx.jobIndex ?? 0, ctx.sex ?? 0, ctx.selfId ?? 0);
  }

  /** Bind server-learned, role-job skills (from 1/12/1 + skill.dsh). */
  setSkills(skills: BattleSkill[]): void {
    this.skills = skills.slice();
    this.skillCursor = 0;
    if (this.menuMode === "skill") this.redrawMenu();
  }

  /** Bind backpack rows usable in battle (medicines + flasks). */
  setBattleItems(bag: BagItem[]): void {
    const prevSeq = this.items[this.itemCursor]?.seq;
    this.items = bag.filter(isBattleMedicineOrFlask).map((it) => ({
      seq: it.seq,
      itemId: it.itemId,
      name: it.name,
      count: it.count,
    }));
    if (prevSeq != null) {
      const keep = this.items.findIndex((it) => it.seq === prevSeq);
      this.itemCursor = keep >= 0 ? keep : 0;
    } else {
      this.itemCursor = 0;
    }
    if (this.itemCursor >= this.items.length) this.itemCursor = 0;
    if (this.menuMode === "item") this.redrawMenu();
  }

  isSkillMode(): boolean {
    return (
      this.menuOpen &&
      !this.awaitingRound &&
      (this.menuMode === "skill" ||
        this.menuMode === "item" ||
        this.menuMode === "target")
    );
  }

  /** True while seat already acted and is waiting for shared actioninfo. */
  isAwaitingRound(): boolean {
    return this.awaitingRound;
  }

  /** True while actioninfo FX is playing. */
  isPlaybackBusy(): boolean {
    return this.playbackBusy;
  }

  /** True on settlement panel awaiting confirm. */
  isAwaitingExit(): boolean {
    return this.awaitingExit;
  }

  /** True on death revival prompt (cannot act / auto-operate). */
  isAwaitingDeathPrompt(): boolean {
    return this.awaitingDeathPrompt;
  }

  /** Seat can submit a new 4/2 (menu turn, not locked / playing back). */
  canSubmitOperate(): boolean {
    return (
      this.visible &&
      !!this.state &&
      !this.awaitingExit &&
      !this.awaitingDeathPrompt &&
      !this.playbackBusy &&
      !this.awaitingRound
    );
  }

  /** Unlock a stuck awaitingRound after a failed operate exchange. */
  clearAwaitingRound(): void {
    if (!this.awaitingRound) return;
    this.awaitingRound = false;
    if (
      this.operateMenuAllowed() &&
      !this.playbackBusy &&
      !this.awaitingExit
    ) {
      this.menuMode = "root";
      this.menuOpen = true;
      this.paintLocalActingTitle();
      this.redrawMenu();
    }
  }

  /** Roster member left/offline during a shared team round. */
  noteTeammateLeft(): void {
    void this.log.setText(
      this.awaitingRound ? "队友已离开，等待回合…" : "队友已离开",
      CBE.inkTip,
      { maxWidth: LCD_WIDTH - 16 },
    );
  }

  /**
   * Spar 4/10 left uses sexGroup 1..2 on a single opponent seat.
   * PvE subtype-10 (hangup/ladder) may reuse sexGroup bytes for jobIndex
   * display but has multiple left seats — must not skip the death prompt.
   */
  isSparBattle(): boolean {
    if (!this.state || this.state.subtype !== 10) return false;
    if (this.state.left.length !== 1) return false;
    const g = this.state.left[0]?.visual0 ?? 0;
    return g >= 1 && g <= 2;
  }

  /** Pixi 比武擂台 duel — set by main when entering battle from arena scene. */
  setArenaDuel(on: boolean): void {
    this.arenaDuel = on;
  }

  isArenaDuel(): boolean {
    return this.arenaDuel && this.isSparBattle();
  }

  private turnSeconds(): number {
    return this.isArenaDuel() ? TURN_SECONDS_ARENA : TURN_SECONDS;
  }

  /** Right softkey label while battle menu is interactive. */
  softkeyRightLabel(): string {
    if (this.awaitingDeathPrompt) {
      return "普通复活";
    }
    if (this.awaitingExit) return "";
    // Hangup / [自动]: no flee / return softkey (menu is hidden).
    if (this.hangupActive || this.autoActive) return "";
    if (this.isSkillMode()) return "返回";
    if (this.isSparBattle()) return "";
    return "逃跑";
  }

  softkeyLeftLabel(): string {
    if (this.awaitingDeathPrompt) {
      return "确定";
    }
    if (this.awaitingExit) return "继续";
    if (this.hangupActive || this.autoActive) return "";
    return "确定";
  }

  private rootMenuItems(): readonly MenuItem[] {
    if (this.isArenaDuel()) return MENU_ITEMS_ARENA;
    return this.isSparBattle() ? MENU_ITEMS_SPAR : MENU_ITEMS;
  }

  /**
   * Mark local operate submitted. Shared-round battles (team + spar) stay
   * locked until 4/6 arrives; solo PvE returns actioninfo in the same
   * exchange and clears via applyUpdate.
   */
  noteOperateSubmitted(tip = "等待回合…"): void {
    this.menuMode = "root";
    this.menuOpen = false;
    // Keep selectedTargetWire for the pending 4/2 or 4/3; only clear pick UI.
    this.targetWires = [];
    this.targetCursor = 0;
    this.pendingOperate = null;
    this.pendingSkillName = "";
    this.pendingItem = null;
    this.clearTargetMark();
    this.redrawMenu();
    // Always lock: spar uses duel_round_defer (empty WT) like team; solo
    // clears on the same exchange's applyUpdate.
    this.awaitingRound = true;
    if (SHOW_BATTLE_COMBAT_LOG) {
      void this.log.setText(tip, CBE.inkBody, { maxWidth: LCD_WIDTH - 16 });
    }
  }

  show(state: BattleState): void {
    preloadMenuScrollTexture(() => {
      if (this.menuOpen && !this.awaitingExit && !this.awaitingDeathPrompt) {
        this.redrawMenu();
      }
    });
    preloadPageNavTextures(() => {
      if (
        this.menuOpen &&
        (this.menuMode === "skill" || this.menuMode === "item")
      ) {
        this.redrawMenu();
      }
    });
    this.visible = true;
    this.state = state;
    this.awaitingExit = false;
    this.awaitingDeathPrompt = false;
    this.deathPromptBusy = false;
    this.deathPromptHasStone = false;
    this.deathPromptCursor = 0;
    this.playbackBusy = false;
    this.playbackDepth = 0;
    this.playbackChain = Promise.resolve();
    this.awaitingRound = false;
    this.playbackGen += 1;
    this.menuMode = "root";
    this.menuCursor = 0;
    this.menuOpen = this.operateMenuAllowed();
    this.resetTargetPick();
    this.timerSec = this.turnSeconds();
    this.timerAcc = 0;
    this.clearFloats();
    this.fxHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    void this.loadBattleMapBg(this.visuals.worldScene || "");
    this.rebuildUnits();
    this.redrawMenu();
    this.refreshBottomHud();
    this.paintTimer();
    this.refreshAutoBannerVisibility();
    this.paintLocalActingTitle();
    void this.log.setText(
      this.hangupActive
        ? "挂机战斗中…"
        : this.autoActive
          ? "自动战斗中…"
          : `遭遇敌×${state.left.length}`,
      this.hangupActive || this.autoActive ? CBE.inkTip : CBE.inkBody,
      { maxWidth: LCD_WIDTH - 16 },
    );
  }

  applyUpdate(up: BattleActionUpdate): void {
    if (!this.state) return;
    // Shared 4/6 (or escape/settle) arrived — leave team wait-for-round lock.
    this.awaitingRound = false;
    this.menuMode = "root";
    this.menuOpen = false;
    this.resetTargetPick();
    this.redrawMenu();
    if (SHOW_BATTLE_COMBAT_LOG) {
      const settleLog = up.settlement != null;
      if (settleLog) {
        const text = formatBattleBottomLog(up.log);
        if (text) {
          this.log.visible = true;
          void this.log.setText(text, CBE.ink, { maxWidth: BOTTOM_LOG_MAX_W });
        } else {
          this.log.visible = false;
        }
      } else {
        this.log.visible = true;
        void this.log.setText(up.log, CBE.inkBody, {
          maxWidth: LCD_WIDTH - 16,
        });
      }
    }

    /*
     * With a round script, defer HP/MP onto each action landing so the bottom
     * bar tracks monster hits / skill cost instead of jumping when 4/6 arrives
     * (player operate). Legacy packets without actions still apply immediately.
     */
    const deferVitals = (up.actions?.length ?? 0) > 0;
    if (!deferVitals) {
      for (const d of up.hpDeltas ?? []) {
        this.applyWireHpDelta(d.wire, d.delta);
      }
      for (const h of up.rightHp) {
        const row = this.state.right.find((r) => r.id === h.id);
        if (!row) continue;
        if (h.mp != null) row.mp = h.mp;
        // teaminfo HP is overlapped/ignored when actioninfo deltas exist.
        if (!(up.hpDeltas && up.hpDeltas.length) && h.hp != null) {
          row.hp = Math.max(0, h.hp);
        }
      }
      this.notifyVitalsChanged();
    }
    for (const h of up.leftHp) {
      const row = this.state.left.find((r) => r.id === h.id) || this.state.left[0];
      if (row) row.hp = Math.max(0, h.hp);
    }

    /*
     * Do NOT zero left HP here when settlement is co-packet with 4/6.
     * Solo PvE killing blow inlines 4/7 with actioninfo; pre-zeroing made
     * isUnitDefeated true before playback → skip cast/attack, jump to death.
     * Wipe after playActionScript (deltas + type-3 already applied).
     */

    /*
     * Serialize scripts. A second 4/6 (auto synth / peer deliver) used to
     * void-start in parallel while gen++ abandoned the first finally — with
     * 3 monsters that looked like「第一只怪后己方又出手」, remaining counters
     * stomped (docs/re/2026-07-28-battle-auto-stomp-counter-playback.md).
     */
    const epoch = this.playbackGen;
    this.playbackBusy = true;
    this.playbackDepth += 1;
    this.playbackChain = this.playbackChain
      .catch(() => undefined)
      .then(async () => {
        try {
          if (!this.state || epoch !== this.playbackGen) return;
          await this.playActionScript(up);
        } finally {
          this.finishActionPlayback(up, deferVitals, epoch);
        }
      });
  }

  /** After one queued script: vitals / settle / menu. Auto only when queue drained. */
  private finishActionPlayback(
    up: BattleActionUpdate,
    deferVitals: boolean,
    epoch: number,
  ): void {
    this.playbackDepth = Math.max(0, this.playbackDepth - 1);
    const drained = this.playbackDepth === 0;
    if (drained) this.playbackBusy = false;
    if (epoch !== this.playbackGen || !this.state) return;

    if (up.settlement) {
      for (const row of this.state.left) row.hp = 0;
    }
    // Ensure final teaminfo MP landed (HP stays on actioninfo deltas only).
    this.applyRightTeaminfo(up.rightHp, /* mpOnly */ true);
    if (deferVitals) {
      /* Death rounds attach teaminfo hp=0; actioninfo deltas alone may leave
       * the local seat alive so beginDeathPrompt never fires. */
      const local = this.localPlayerSide();
      if (local) {
        const tip = up.rightHp.find((h) => h.id === local.id);
        if (tip?.hp === 0) {
          local.hp = 0;
          const view = this.units.find((u) => u.side === local);
          if (view) view.dead = true;
        }
      }
    }
    if (deferVitals && !this.isLocalPlayerDead()) {
      const selfW = this.selfWire();
      const playerDeathRec = up.actions?.some(
        (a) => (a.type === 3 || a.type === 4) && a.actorWire === selfW,
      );
      if (playerDeathRec) {
        const local = this.localPlayerSide();
        if (local) local.hp = 0;
        const view = this.unitForWire(selfW);
        if (view) view.dead = true;
      }
    }
    if (up.settlement) {
      // 4/7.hp/mp are recovery deltas (flask / settle recover). Apply onto
      // the local seat so world HUD matches post-settle durable vitals.
      this.applySettlementRecover(up.settlement);
    }
    this.notifyVitalsChanged();
    if (up.settlement) {
      this.showSettlement(up.settlement);
      this.awaitingExit = true;
      this.awaitingDeathPrompt = false;
      this.menuOpen = false;
      this.redrawMenu();
      this.refreshAutoBannerVisibility();
      // 4/7+4/8 often share one WT — keep settle panel; confirm owns leave.
      return;
    }
    if (up.finished) {
      this.menuOpen = false;
      this.redrawMenu();
      this.refreshAutoBannerVisibility();
      // Only leave when nothing else is queued (lone 4/8 may trail a long 4/6).
      if (drained) this.onExitReady?.();
      return;
    }
    // Death arms revival confirm on server — no 4/7/4/8 until 1/7/14.
    // Spar/duel uses settle/exit (penalty already applied); no 复活石 prompt.
    if (this.isLocalPlayerDead()) {
      if (this.isSparBattle()) {
        this.menuOpen = false;
        this.redrawMenu();
        this.refreshAutoBannerVisibility();
        return;
      }
      this.beginDeathPrompt();
      return;
    }
    // More 4/6 still queued — keep menu down and do not arm auto yet.
    if (!drained) return;
    this.menuMode = "root";
    // Keep scroll hidden while [自动] / hangup; banner or stop-hangup restores.
    this.menuOpen = this.operateMenuAllowed();
    this.timerSec = this.turnSeconds();
    this.timerAcc = 0;
    this.paintTimer();
    this.paintLocalActingTitle();
    this.redrawMenu();
    this.refreshAutoBannerVisibility();
    // Next seat is ready — main arms [自动] client 4/2 if enabled.
    this.onAutoReady?.();
  }

  hide(): void {
    clearPanelTip(this.toast);
    // Flush last known seat vitals before tearing state (world HUD sync).
    if (this.state) this.notifyVitalsChanged();
    this.visible = false;
    this.state = null;
    this.enemyHud.clear();
    this.arenaDuel = false;
    this.hangupActive = false;
    /* Keep hangupTallyArmed so the next fight continues session 战斗场次. */
    this.awaitingExit = false;
    this.awaitingDeathPrompt = false;
    this.playbackBusy = false;
    this.playbackDepth = 0;
    this.playbackChain = Promise.resolve();
    this.awaitingRound = false;
    this.playbackGen += 1;
    this.settlePaintGen += 1;
    this.clearSettleCloseTimer();
    this.settleConfirmBusy = false;
    this.resetTargetPick();
    this.clearFloats();
    clearPointerHits(this.rowHits);
    this.unitsHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.units = [];
    this.fxHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    /* Drop battle GPU — hide used to leave mapSprite + poseTexCache pinned and
     * VRAM climbed across hangup fights until WebView contextlost. */
    for (const tex of this.poseTexCache.values()) {
      try {
        tex.destroy(true);
      } catch {
        /* ignore */
      }
    }
    this.poseTexCache.clear();
    if (this.mapSprite) {
      try {
        if (this.mapSprite.parent) this.mapSprite.parent.removeChild(this.mapSprite);
        this.mapSprite.destroy({ texture: true, textureSource: true });
      } catch {
        /* ignore */
      }
      this.mapSprite = null;
    }
    this.mapPixels = null;
    this.mapHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.refreshAutoBannerVisibility();
  }

  /** Reload e_deity units after client「屏蔽/显示特效」swaps gif ↔ png. */
  async refreshDeitySheetForFxPref(): Promise<void> {
    if (!this.visible || !this.units.length) return;
    for (const u of this.units) {
      if (!u.actorName || !/e_deity/i.test(u.actorName)) continue;
      try {
        const loaded = await loadActor(u.actorName);
        if (!this.visible || !u.root.parent) continue;
        u.loaded = loaded;
        u.phase = 0;
        this.paintUnitSprite(u);
      } catch (e) {
        console.warn("[battle] deity sheet reload failed", u.actorName, e);
      }
    }
  }

  /**
   * After webglcontextrestored: world SceneView nudge does not cover battle.
   * BufferImageSource map/pose textures must be *recreated* from CPU RGBA —
   * source.update() alone leaves a black stage with UC3/HUD still painting.
   * Graphics (HP fills / letterbox / foot shadows) need DisplayObject replace.
   *
   * stageHost.mask dead ⇒ map/units/fx clipped empty while floatHost/HUD still paint.
   * Use remounted Sprite mask (Graphics masks stay empty on WebView).
   */
  nudgeAfterGpuResume(): void {
    if (!this.visible) return;
    try {
      const stageH = STAGE_BOTTOM - STAGE_TOP;
      this.stageHost.mask = null;
      this.stageMask = remountSolidMaskSprite(
        this.stageMask,
        LCD_WIDTH,
        stageH,
      );
      this.stageMask.y = STAGE_TOP;
      this.stageHost.mask = this.stageMask;
      this.stageHost.visible = true;
      this.mapHost.visible = true;
      this.unitsHost.visible = true;
      this.fxHost.visible = true;
    } catch (e) {
      console.warn("[battle] stage mask rebuild", e);
      try {
        this.stageHost.mask = null;
      } catch {
        /* ignore */
      }
    }
    try {
      if (this.mapPixels) {
        this.applyMapTexture(
          this.mapPixels.width,
          this.mapPixels.height,
          this.mapPixels.rgba,
        );
      } else if (this.battleMapWorld) {
        // No CPU copy yet (load in flight / failed): bust cache and reload.
        const world = this.battleMapWorld;
        this.battleMapFile = "";
        void this.loadBattleMapBg(world);
      } else {
        // Keep letterbox valid even without a map sprite.
        for (const c of [...this.mapHost.children]) {
          if (!(c instanceof Graphics)) continue;
          const g = replaceGraphics(c);
          g.rect(0, 0, LCD_WIDTH, LCD_HEIGHT).fill({ color: CBE.bgDeep });
          g.eventMode = "none";
        }
      }
      if (this.mapSprite) this.mapSprite.visible = true;
    } catch (e) {
      console.warn("[battle] map rebuild", e);
    }

    // Drop dead BufferImageSource poses; paintUnitSprite rebuilds from actor RGBA.
    for (const tex of this.poseTexCache.values()) {
      try {
        tex.destroy(true);
      } catch {
        /* ignore */
      }
    }
    this.poseTexCache.clear();
    this.poseTexKey = new WeakMap();

    for (const u of this.units) {
      if (u.root.destroyed) continue;
      try {
        const shadow = u.root.children[0];
        if (shadow instanceof Graphics) {
          const g = replaceGraphics(shadow);
          g.ellipse(0, 2, 13, 5).fill({ color: 0x000000, alpha: 0.35 });
        }
        // Remount Sprite DisplayObject — texture rebind alone stays black.
        if (u.sprite) {
          try {
            if (u.sprite.parent) u.sprite.parent.removeChild(u.sprite);
            u.sprite.destroy({ texture: false });
          } catch {
            /* ignore */
          }
          u.sprite = null;
        }
        this.paintUnitSprite(u);
        // CFA loses mutations inside paintUnitSprite after sprite=null.
        const body = u.sprite as Sprite | null;
        if (body && !u.hidden) {
          body.visible = true;
          u.root.visible = true;
        }
      } catch {
        /* ignore per-unit */
      }
    }

    try {
      nudgeTextureSource(this.autoBannerWords?.texture);
      for (const c of this.fxHost.children) {
        nudgeTextureSource((c as Sprite).texture as Texture | undefined);
        if ((c as Sprite).visible !== false) (c as Sprite).visible = true;
      }
    } catch {
      /* ignore */
    }

    if (this.state) {
      this.partyHud.nudgeAfterGpuResume(this.state.right, {
        playerName: this.visuals.playerName,
      });
      this.enemyHud.nudgeAfterGpuResume(
        this.state.left,
        this.enemyHudAnchors(),
      );
    }
  }

  /**
   * Bound cache growth without destroying textures still bound to unit sprites
   * (that left black silhouettes / “黑屏” mid-battle).
   */
  private evictUnusedPoseTextures(): void {
    if (this.poseTexCache.size <= 96) return;
    const inUse = new Set<string>();
    for (const u of this.units) {
      if (!u.sprite) continue;
      const k = this.poseTexKey.get(u.sprite);
      if (k) inUse.add(k);
    }
    for (const [key, tex] of this.poseTexCache) {
      if (this.poseTexCache.size <= 72) break;
      if (inUse.has(key)) continue;
      tex.destroy(true);
      this.poseTexCache.delete(key);
    }
  }

  targetIndex(): number {
    if (!this.state?.left.length && !this.state?.right.length) return 0;
    if (this.selectedTargetWire != null) return this.selectedTargetWire;
    return this.livingEnemyWires()[0] ?? battleTargetWire(this.state!);
  }

  /**
   * Hangup / [自动] 出手：与手动攻击/仙术同一套选目标 + onAttack/onSkill。
   * 不在这里改写 actioninfo 播报（播报照服务器 child wire，与手动一致）。
   */
  private resolveAutoOperateMp(operate: number): number {
    const op = operate | 0;
    if (op === 0) return 0;
    const sk = this.skills.find((s) => s.id + 2 === op);
    const mpCost = sk?.mpCost;
    if (mpCost == null || mpCost <= 0) return op;
    const vitals = this.allyVitals();
    if (vitals != null && vitals.mp < mpCost) return 0;
    return op;
  }

  /** Manual skill path: tip + stay in menu when HUD MP < cost. */
  private ensureSkillMp(operate: number): boolean {
    const op = operate | 0;
    if (op === 0) return true;
    if (this.resolveAutoOperateMp(op) === op) return true;
    void this.log.setText("法力不足", CBE.inkTip);
    return false;
  }

  submitAutoOperate(operate: number, tip: string): boolean {
    if (!this.state || !this.canSubmitOperate()) return false;
    const op = this.resolveAutoOperateMp(operate);
    if (op === 0) {
      const wires = this.livingEnemyWires();
      this.selectedTargetWire =
        wires[0] ?? battleTargetWire(this.state);
      this.noteOperateSubmitted(
        op !== (operate | 0) ? "法力不足·普攻" : tip,
      );
      // Prefer onSkill(0) so main.ts can tag the note as auto (does not
      // erase a remembered skill). Fall back to onAttack for older wires.
      if (this.onSkill) this.onSkill(0);
      else this.onAttack?.();
      return true;
    }
    const sk = this.skills.find((s) => s.id + 2 === op);
    const kind = (sk?.targetKind ?? 3) as SkillTargetKind;
    const name = sk?.name ?? "仙术";
    const self = this.selfWire();
    const firstEnemy = (): number | null => {
      const wires = this.livingEnemyWires();
      const w = wires[0] ?? battleTargetWire(this.state!);
      // Offensive auto must never submit the player wire (subtype-10 wire 0 /
      // count-skew battleTargetWire). Server then aborts → client 普攻.
      return w === self ? (wires.find((x) => x !== self) ?? null) : w;
    };
    // Same branches as fireSkill / confirm（自动不弹选目标 UI，取首个合法 wire）.
    if (skillNeedsTargetPick(kind)) {
      if (kind === 1) {
        this.selectedTargetWire =
          this.livingAllyWires()[0] ?? this.selfWire();
      } else {
        this.selectedTargetWire = firstEnemy();
      }
      if (this.selectedTargetWire == null) return false;
      this.noteOperateSubmitted(tip || `施展${name}…`);
      this.onSkill?.(op);
      return true;
    }
    if (kind === 0) {
      this.selectedTargetWire = this.selfWire();
    } else if (kind === 2) {
      this.selectedTargetWire =
        this.livingAllyWires()[0] ?? this.selfWire();
    } else {
      // kind 3 (default) and 4 (敌群): first living enemy wire.
      this.selectedTargetWire = firstEnemy();
      if (this.selectedTargetWire == null) return false;
    }
    this.noteOperateSubmitted(tip || `施展${name}…`);
    this.onSkill?.(op);
    return true;
  }

  /**
   * System-panel / external manual cast: disarm [自动], then same wire pick
   * as hangup auto (no target UI). Returns false if seat locked / no target.
   */
  submitManualSkill(operate: number, name: string): boolean {
    if (!this.canSubmitOperate()) return false;
    this.clearAutoForManualOp();
    if (!this.ensureSkillMp(operate)) return false;
    return this.submitAutoOperate(operate, `施展${name}…`);
  }

  allyVitals(): { hp: number; maxHp: number; mp: number; maxMp: number } | null {
    const rows = this.state?.right;
    if (!rows?.length) return null;
    const selfId = (this.visuals.selfId ?? 0) >>> 0;
    const row =
      (selfId ? rows.find((r) => r.id === selfId) : undefined) || rows[0];
    if (!row) return null;
    return { hp: row.hp, maxHp: row.maxHp, mp: row.mp, maxMp: row.maxMp };
  }

  cycleSkill(delta: number): void {
    if (this.awaitingDeathPrompt) {
      const n = this.deathPromptOptions().length;
      if (n <= 1) return;
      this.deathPromptCursor = (this.deathPromptCursor + delta + n) % n;
      this.paintDeathPrompt();
      return;
    }
    if (this.playbackBusy || this.awaitingRound || !this.menuOpen) return;
    if (this.menuMode === "target") {
      const n = this.targetWires.length;
      if (!n) return;
      this.targetCursor = (this.targetCursor + delta + n) % n;
      this.refreshTargetMark();
      this.logTargetPick();
      return;
    }
    if (this.menuMode === "skill") {
      if (!this.skills.length) return;
      this.skillCursor =
        (this.skillCursor + delta + this.skills.length) % this.skills.length;
      this.redrawMenu();
      return;
    }
    if (this.menuMode === "item") {
      if (!this.items.length) return;
      this.itemCursor =
        (this.itemCursor + delta + this.items.length) % this.items.length;
      this.redrawMenu();
      return;
    }
    this.menuCursor =
      (this.menuCursor + delta + this.rootMenuItems().length) %
      this.rootMenuItems().length;
    this.redrawMenu();
  }

  currentOperate(): number {
    const sk = this.skills[this.skillCursor] ?? this.skills[0];
    if (!sk) return 0;
    return sk.id + 2;
  }

  confirm(): void {
    if (this.awaitingDeathPrompt) {
      this.commitDeathPrompt(this.deathPromptPrimaryResult());
      return;
    }
    if (this.awaitingExit) {
      this.confirmSettlement();
      return;
    }
    if (this.playbackBusy || this.awaitingRound) return;
    if (!this.menuOpen) {
      // Hangup owns the seat via server synth — do not reopen the scroll.
      if (this.hangupActive) {
        void this.log.setText("挂机中…", CBE.inkTip);
        return;
      }
      // Reopening the scroll is a manual takeover — disarm [自动].
      this.clearAutoForManualOp();
      this.menuOpen = true;
      this.menuMode = "root";
      this.redrawMenu();
      return;
    }
    if (this.menuMode === "target") {
      this.commitTargetPick();
      return;
    }
    if (this.menuMode === "skill") {
      if (!this.skills.length) {
        void this.log.setText("未学会仙术", CBE.inkTip);
        this.menuMode = "root";
        this.redrawMenu();
        return;
      }
      const sk = this.skills[this.skillCursor]!;
      const kind = sk.targetKind ?? 3;
      const operate = sk.id + 2;
      if (!this.ensureSkillMp(operate)) return;
      this.clearAutoForManualOp();
      if (skillNeedsTargetPick(kind)) {
        const wires =
          kind === 1 ? this.livingAllyWires() : this.livingEnemyWires();
        if (wires.length > 1) {
          this.beginTargetPick(kind, operate, sk.name);
          return;
        }
        this.selectedTargetWire = wires[0] ?? null;
        this.noteOperateSubmitted(`施展${sk.name}…`);
        this.onSkill?.(operate);
        return;
      }
      this.fireSkill(operate, sk.name, kind);
      return;
    }
    if (this.menuMode === "item") {
      this.commitItemPick();
      return;
    }
    this.runMenu(this.rootMenuItems()[this.menuCursor]!);
  }

  attack(): void {
    if (this.awaitingDeathPrompt) {
      this.commitDeathPrompt(this.deathPromptPrimaryResult());
      return;
    }
    if (this.awaitingExit) {
      this.confirmSettlement();
      return;
    }
    if (this.playbackBusy || this.awaitingRound) return;
    this.clearAutoForManualOp();
    const wires = this.livingEnemyWires();
    if (wires.length > 1) {
      this.beginTargetPick(3, null, "攻击");
      return;
    }
    this.selectedTargetWire = wires[0] ?? null;
    this.noteOperateSubmitted(
      this.isSparBattle() ? "等待对方出招…" : "请求攻击…",
    );
    this.onAttack?.();
  }

  /**
   * Turn countdown expiry.
   * If the player already opened 仙术/物品 (or is mid target-pick after
   * choosing one), commit that choice — do not wipe to 普攻.
   * Root-menu / idle timeout still defaults to normal attack.
   */
  private submitTimeoutAttack(): void {
    if (
      !this.state ||
      this.playbackBusy ||
      this.awaitingRound ||
      this.awaitingExit ||
      this.awaitingDeathPrompt
    ) {
      return;
    }
    this.clearAutoForManualOp();

    // Target pick after 仙术/物品 confirm — submit the chosen action.
    if (
      this.menuMode === "target" &&
      (this.pendingOperate != null || this.pendingItem)
    ) {
      const pendingOp = this.pendingOperate;
      if (
        pendingOp != null &&
        this.resolveAutoOperateMp(pendingOp) !== (pendingOp | 0)
      ) {
        // Timer already 0 — cannot stay on pick; fall through to 普攻.
        void this.log.setText("超时·法力不足·普攻", CBE.inkTip);
        this.pendingOperate = null;
        this.pendingSkillName = "";
        this.pendingItem = null;
        this.targetWires = [];
        this.targetCursor = 0;
        this.clearTargetMark();
      } else {
        const tip = this.pendingItem
          ? `超时·${this.pendingItem.name}`
          : `超时·${this.pendingSkillName || "仙术"}`;
        void this.log.setText(tip, CBE.inkTip);
        this.commitTargetPick();
        return;
      }
    }

    // Browsing skill list — cast highlighted skill (MP gate → else 普攻).
    if (this.menuMode === "skill" && this.skills.length) {
      const sk = this.skills[this.skillCursor]!;
      if (this.submitAutoOperate(sk.id + 2, `超时·${sk.name}`)) return;
    }

    // Browsing item list — use highlighted medicine/flask.
    if (this.menuMode === "item" && this.items.length) {
      void this.log.setText("超时·使用物品", CBE.inkTip);
      this.commitItemPick();
      return;
    }

    let wire: number | null = null;
    if (
      this.menuMode === "target" &&
      this.pendingOperate == null &&
      !this.pendingItem &&
      this.targetWires.length
    ) {
      wire = this.targetWires[this.targetCursor] ?? null;
    }
    if (wire == null) wire = this.livingEnemyWires()[0] ?? null;
    this.selectedTargetWire = wire;
    this.pendingOperate = null;
    this.pendingSkillName = "";
    this.pendingItem = null;
    this.targetWires = [];
    this.targetCursor = 0;
    this.clearTargetMark();
    this.menuMode = "root";
    void this.log.setText("超时·普攻", CBE.inkTip);
    this.noteOperateSubmitted("超时·普攻…");
    this.onAttack?.();
  }

  skill(): void {
    if (this.awaitingDeathPrompt) {
      this.commitDeathPrompt(this.deathPromptPrimaryResult());
      return;
    }
    if (this.awaitingExit) {
      this.confirmSettlement();
      return;
    }
    if (this.playbackBusy || this.awaitingRound) return;
    this.clearAutoForManualOp();
    if (this.menuMode === "skill" && this.menuOpen) {
      this.confirm();
      return;
    }
    if (!this.skills.length) {
      void this.log.setText("未学会仙术（等 12/1）", CBE.inkTip);
      return;
    }
    this.menuMode = "skill";
    this.menuOpen = true;
    this.skillCursor = Math.min(this.skillCursor, this.skills.length - 1);
    this.clearTargetMark();
    this.redrawMenu();
    void this.log.setText("选择仙术…", CBE.inkBody);
  }

  item(): void {
    if (this.awaitingDeathPrompt) {
      this.commitDeathPrompt(this.deathPromptPrimaryResult());
      return;
    }
    if (this.awaitingExit) {
      this.confirmSettlement();
      return;
    }
    if (this.isArenaDuel()) {
      void this.log.setText("比武中不可用物品", CBE.inkTip);
      return;
    }
    if (this.playbackBusy || this.awaitingRound) return;
    this.clearAutoForManualOp();
    if (this.menuMode === "item" && this.menuOpen) {
      this.confirm();
      return;
    }
    if (!this.items.length) {
      void this.log.setText("没有可用药品/壶", CBE.inkTip);
      return;
    }
    this.menuMode = "item";
    this.menuOpen = true;
    this.itemCursor = Math.min(this.itemCursor, this.items.length - 1);
    this.clearTargetMark();
    this.redrawMenu();
    void this.log.setText("选择物品…", CBE.inkBody);
  }

  flee(): void {
    if (this.awaitingDeathPrompt) {
      // Right softkey / 返回：普通复活 (result=2), matching CBE cancel path.
      this.commitDeathPrompt(2);
      return;
    }
    if (this.awaitingExit) {
      this.confirmSettlement();
      return;
    }
    if (this.playbackBusy || this.awaitingRound) return;
    if (this.menuMode === "target") {
      if (this.pendingItem) {
        this.menuMode = "item";
        this.pendingItem = null;
        this.clearTargetMark();
        this.redrawMenu();
        void this.log.setText("选择物品…", CBE.inkBody);
      } else if (this.pendingOperate != null) {
        this.menuMode = "skill";
        this.pendingOperate = null;
        this.pendingSkillName = "";
        this.clearTargetMark();
        this.redrawMenu();
        void this.log.setText("选择仙术…", CBE.inkBody);
      } else {
        this.menuMode = "root";
        this.clearTargetMark();
        this.redrawMenu();
        void this.log.setText("选择行动…", CBE.inkBody);
      }
      return;
    }
    if (this.menuMode === "skill" || this.menuMode === "item") {
      this.menuMode = "root";
      this.redrawMenu();
      void this.log.setText("选择行动…", CBE.inkBody);
      return;
    }
    if (this.isSparBattle()) {
      void this.log.setText("切磋中无法逃跑", CBE.inkTip);
      return;
    }
    // Lock like attack/skill — bare softkey spam used to fire extra 1/4/4
    // after the first success cleared the server armed session → tip
    // 「功能暂未实现(1/4/4)」.
    this.clearAutoForManualOp();
    this.noteOperateSubmitted("请求逃跑…");
    this.onFlee?.();
  }

  private resetTargetPick(): void {
    this.targetWires = [];
    this.targetCursor = 0;
    this.pendingOperate = null;
    this.pendingSkillName = "";
    this.pendingItem = null;
    this.selectedTargetWire = null;
    this.clearTargetMark();
  }

  private isTeamBattle(): boolean {
    return (this.state?.right.length ?? 0) >= 2;
  }

  /** All legal actioninfo wires for the current formation. */
  private candidateWires(): number[] {
    if (!this.state) return [];
    const P = this.state.right.length;
    const M = this.state.left.length;
    if (this.state.subtype === 10) {
      return [0, ...this.state.left.map((_, i) => i + 1)];
    }
    if (this.state.subtype === 5 && P >= 2 && M >= 1) {
      return Array.from({ length: P + M }, (_, d) =>
        battleDisplayToWire(d, P, M),
      );
    }
    const leftWires =
      M === 1 ? [0] : M === 2 ? [2, 0] : M === 3 ? [2, 3, 0] : [0];
    return [...leftWires, 1];
  }

  private wireForUnit(unit: BattleSide): number | null {
    if (!this.state) return null;
    for (const w of this.candidateWires()) {
      if (battleUnitForWire(this.state, w) === unit) return w;
    }
    return null;
  }

  private selfWire(): number {
    if (!this.state?.right.length) return 0;
    const selfId = (this.visuals.selfId ?? 0) >>> 0;
    const row =
      (selfId ? this.state.right.find((r) => r.id === selfId) : undefined) ||
      this.state.right[0]!;
    return this.wireForUnit(row) ?? 0;
  }

  private livingEnemyWires(): number[] {
    if (!this.state) return [];
    const fromTable = this.candidateWires().filter((w) => {
      const u = battleUnitForWire(this.state!, w);
      return !!u && this.state!.left.includes(u) && u.hp > 0;
    });
    if (fromTable.length) return fromTable;
    /*
     * Enemy-count / subtype transitions can desync candidateWires from left[].
     * Fall back to per-seat wire lookup so hangup/auto group skills still get
     * a living monster index instead of failing open into 普攻.
     */
    const out: number[] = [];
    for (const u of this.state.left) {
      if (u.hp <= 0) continue;
      const w = this.wireForUnit(u);
      if (w != null) out.push(w);
    }
    return out;
  }

  private livingAllyWires(): number[] {
    if (!this.state) return [];
    return this.candidateWires().filter((w) => {
      const u = battleUnitForWire(this.state!, w);
      return !!u && this.state!.right.includes(u) && u.hp > 0;
    });
  }

  private beginTargetPick(
    kind: SkillTargetKind,
    operate: number | null,
    skillName: string,
  ): void {
    const wires =
      kind === 1 ? this.livingAllyWires() : this.livingEnemyWires();
    if (!wires.length) {
      void this.log.setText("没有可选目标", CBE.inkTip);
      return;
    }
    this.pendingItem = null;
    this.pendingOperate = operate;
    this.pendingSkillName = skillName;
    this.targetWires = wires;
    this.targetCursor = 0;
    this.menuMode = "target";
    this.menuOpen = true;
    this.redrawMenu();
    this.refreshTargetMark();
    this.logTargetPick();
  }

  private commitTargetPick(): void {
    const wire = this.targetWires[this.targetCursor];
    if (wire == null) return;
    const operate = this.pendingOperate;
    const name = this.pendingSkillName;
    const item = this.pendingItem;
    if (!item && operate != null && !this.ensureSkillMp(operate)) return;
    this.clearAutoForManualOp();
    this.selectedTargetWire = wire;
    this.pendingOperate = null;
    this.pendingSkillName = "";
    this.pendingItem = null;
    this.targetWires = [];
    if (item) {
      this.noteOperateSubmitted(`使用${item.name}…`);
      this.onItemUse?.(item.seq);
      return;
    }
    if (operate == null) {
      this.noteOperateSubmitted(
        this.isSparBattle() ? "等待对方出招…" : "请求攻击…",
      );
      this.onAttack?.();
    } else {
      this.noteOperateSubmitted(
        this.isSparBattle() ? `已出招·等待对方…` : `施展${name}…`,
      );
      this.onSkill?.(operate);
    }
  }

  private commitItemPick(): void {
    if (!this.items.length) {
      void this.log.setText("没有可用药品/壶", CBE.inkTip);
      this.menuMode = "root";
      this.redrawMenu();
      return;
    }
    this.clearAutoForManualOp();
    const row = this.items[this.itemCursor]!;
    const wires = this.livingAllyWires();
    if (!wires.length) {
      void this.log.setText("没有可选队友", CBE.inkTip);
      return;
    }
    if (wires.length > 1) {
      this.pendingItem = { seq: row.seq, name: row.name };
      this.pendingOperate = null;
      this.pendingSkillName = "";
      this.targetWires = wires;
      this.targetCursor = 0;
      this.menuMode = "target";
      this.menuOpen = true;
      this.redrawMenu();
      this.refreshTargetMark();
      this.logTargetPick();
      return;
    }
    this.selectedTargetWire = wires[0] ?? this.selfWire();
    this.noteOperateSubmitted(`使用${row.name}…`);
    this.onItemUse?.(row.seq);
  }

  private fireSkill(
    operate: number,
    name: string,
    kind: SkillTargetKind,
  ): void {
    if (!this.state) return;
    this.clearAutoForManualOp();
    if (!this.ensureSkillMp(operate)) return;
    if (kind === 0) {
      this.selectedTargetWire = this.selfWire();
    } else if (kind === 2) {
      this.selectedTargetWire =
        this.livingAllyWires()[0] ?? this.selfWire();
    } else {
      const self = this.selfWire();
      const wires = this.livingEnemyWires();
      let w = wires[0] ?? battleTargetWire(this.state);
      if (w === self) w = wires.find((x) => x !== self) ?? w;
      this.selectedTargetWire = w;
    }
    this.noteOperateSubmitted(`施展${name}…`);
    this.onSkill?.(operate);
  }

  private clearTargetMark(): void {
    this.targetMark.clear();
    this.targetMark.visible = false;
  }

  private refreshTargetMark(): void {
    this.targetMark.clear();
    const wire = this.targetWires[this.targetCursor];
    if (wire == null) {
      this.targetMark.visible = false;
      return;
    }
    const view = this.unitForWire(wire);
    if (!view) {
      this.targetMark.visible = false;
      return;
    }
    const x = view.feetX;
    const y = view.feetY - 52;
    this.targetMark
      .poly([x, y + 10, x - 7, y, x + 7, y])
      .fill({ color: SETTLE_GOLD })
      .stroke({ width: 1, color: 0x3a2a10 });
    this.targetMark.visible = true;
  }

  private logTargetPick(): void {
    const wire = this.targetWires[this.targetCursor];
    if (wire == null || !this.state) return;
    const unit = battleUnitForWire(this.state, wire);
    const name = unit?.name || `目标#${wire}`;
    const verb = this.pendingItem
      ? "使用"
      : this.pendingOperate == null
        ? "攻击"
        : "施放";
    void this.log.setText(`${verb}→${name}`, CBE.inkBody, {
      maxWidth: LCD_WIDTH - 16,
    });
  }

  /**
   * Advance combat / idle flaps on wall-clock ms.
   * Do not trust ticker.deltaMS alone — OEM 90/120Hz WebViews have raced
   * pose cadence the same way walk used to (docs/re/2026-08-09-pixijs-walk-speed-hz.md).
   */
  tick(_dtMs?: number): void {
    const now = performance.now();
    if (!this.visible || !this.state) {
      // Avoid a huge catch-up step when re-entering battle after a pause.
      this.lastAnimTick = now;
      return;
    }
    const raw = this.lastAnimTick > 0 ? now - this.lastAnimTick : 16;
    this.lastAnimTick = now;
    const dtMs = Math.min(Math.max(raw, 0), BATTLE_DT_CLAMP_MS);

    // Turn countdown — only while player can act.
    if (
      this.menuOpen &&
      !this.playbackBusy &&
      !this.awaitingRound &&
      !this.awaitingExit &&
      this.timerSec > 0
    ) {
      this.timerAcc += dtMs;
      while (this.timerAcc >= 1000 && this.timerSec > 0) {
        this.timerAcc -= 1000;
        this.timerSec -= 1;
        this.paintTimer();
        if (this.timerSec <= 0) {
          // Must submit 4/2 immediately — attack() opens target-pick when
          // multiple enemies live, which leaves the seat with timer=0 and no
          // operate (docs/re/2026-08-06-pixijs-battle-scene.md 超时默认普攻).
          this.submitTimeoutAttack();
          break;
        }
      }
    }

    this.tickAcc += dtMs;
    this.idleTickAcc += dtMs;
    while (this.tickAcc >= UNIT_FRAME_MS) {
      this.tickAcc -= UNIT_FRAME_MS;
      for (const u of this.units) {
        if (u.hidden || !u.loaded || !u.sprite) continue;
        const prevPose = u.pose;
        if (u.pose !== "idle" && u.pose !== "death" && now >= u.poseUntil) {
          u.pose = "idle";
        }
        // Idle uses BATTLE_IDLE_FRAME_MS; attack/cast/hit stay at UNIT_FRAME_MS.
        // Dead / death-pose monsters stay frozen (no flap) so splash is not
        // the first visible “death” cue.
        if (u.pose === "idle") {
          if (prevPose !== u.pose) this.paintUnitSprite(u);
          continue;
        }
        if (u.pose === "death" || u.dead) {
          if (prevPose !== u.pose) this.paintUnitSprite(u);
          continue;
        }
        u.phase += 1;
        this.paintUnitSprite(u);
      }
    }
    while (this.idleTickAcc >= BATTLE_IDLE_FRAME_MS) {
      this.idleTickAcc -= BATTLE_IDLE_FRAME_MS;
      for (const u of this.units) {
        if (u.hidden || !u.loaded || !u.sprite) continue;
        if (u.dead || u.pose === "death") continue;
        if (u.pose !== "idle") continue;
        u.phase += 1;
        this.paintUnitSprite(u);
      }
    }
  }

  private buildChrome(): void {
    // Top banner (screenshot: dark teal bar + timer)
    this.topHost.addChild(
      new Graphics()
        .roundRect(4, 4, LCD_WIDTH - 8, TOP_H - 6, 4)
        .fill({ color: 0x1a332e })
        .stroke({ width: 1, color: 0x3a554e }),
    );
    this.topTitle.position.set(48, 10);
    this.topHost.addChild(this.topTitle);
    void this.mountTimerArt();

    this.bottomHost.y = STAGE_BOTTOM;
    this.log.anchor.set(0, 1);
    this.log.position.set(BOTTOM_LOG_X, -4);
    this.log.visible = SHOW_BATTLE_COMBAT_LOG;
    // Log before partyHud so portrait / vitals paint above any wrap spill.
    this.bottomHost.addChild(this.log);
    this.bottomHost.addChild(this.partyHud);
    void this.partyHud.mount();
    this.enemyHud.position.set(4, STAGE_TOP + 2);
    void this.enemyHud.mount();
    void this.mountFloatDigits();
    this.ensureAutoBanner();
  }

  private ensureAutoBanner(): void {
    if (this.autoBannerBuilt) return;
    this.autoBannerBuilt = true;
    this.autoBannerHost.visible = false;
    this.autoBannerHost.eventMode = "static";
    void this.mountAutoBannerArt();
  }

  /** Native autobattle_words.gif + autobattle_button.gif (mmBattle auto banner). */
  private async mountAutoBannerArt(): Promise<void> {
    const gen = ++this.autoBannerArtGen;
    const host = this.autoBannerHost;
    host.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.autoBannerWords = null;
    this.autoBannerHangupTip = null;

    const panelW = 134;
    const wordsTex = await tryLoadUiTexture("autobattle_words.gif");
    const btnTex = await tryLoadUiTexture("autobattle_button.gif");
    if (gen !== this.autoBannerArtGen) return;

    if (!wordsTex) {
      const bg = new Graphics()
        .roundRect(0, 0, panelW, 22, 3)
        .fill({ color: 0x142820, alpha: 0.94 })
        .stroke({ width: 1, color: CBE.inkTip });
      host.addChild(bg);
    }

    if (wordsTex) {
      const words = uiSprite(wordsTex);
      words.eventMode = "none";
      words.position.set(4, 6);
      host.addChild(words);
      this.autoBannerWords = words;
    }

    const hangTip = new BitmapLabel();
    hangTip.eventMode = "none";
    hangTip.position.set(4, 5);
    hangTip.show("挂机战斗中", CBE.inkTip, { outline: false });
    hangTip.visible = false;
    host.addChild(hangTip);
    this.autoBannerHangupTip = hangTip;

    const btn = new Container();
    if (btnTex) {
      const spr = uiSprite(btnTex);
      btn.addChild(spr);
    } else {
      const btnBg = new Graphics()
        .roundRect(0, 0, 34, 16, 2)
        .fill({ color: 0x3a2818 })
        .stroke({ width: 1, color: CBE.inkWarn });
      const btnLabel = new BitmapLabel();
      btnLabel.eventMode = "none";
      btnLabel.position.set(3, 2);
      btnLabel.show("关闭", CBE.inkWarn, { outline: false });
      btn.addChild(btnBg, btnLabel);
    }
    btn.position.set(panelW - (btnTex ? 22 : 38), btnTex ? 4 : 3);
    btn.eventMode = "static";
    btn.cursor = "pointer";
    btn.on("pointertap", (e) => {
      e.stopPropagation();
      if (this.hangupActive) {
        this.closeHangup();
        return;
      }
      this.closeAutoBattle();
    });
    host.addChild(btn);

    host.position.set(LCD_WIDTH - panelW - 4, STAGE_TOP + 2);
    this.paintAutoBannerTip();
  }

  private paintAutoBannerTip(): void {
    const hangup = this.hangupActive;
    if (this.autoBannerWords) {
      this.autoBannerWords.visible = !hangup;
    }
    if (this.autoBannerHangupTip) {
      this.autoBannerHangupTip.visible = hangup;
      if (hangup) {
        this.autoBannerHangupTip.show("挂机战斗中", CBE.inkTip, {
          outline: false,
        });
      }
    }
  }

  private refreshAutoBannerVisibility(): void {
    const show =
      this.visible &&
      (this.hangupActive || this.autoActive) &&
      !this.awaitingExit &&
      !this.awaitingDeathPrompt;
    this.autoBannerHost.visible = show;
    if (show) this.paintAutoBannerTip();
  }

  /** Manual 攻击/仙术/物品/逃跑 disarms [自动] without reopening the scroll. */
  private clearAutoForManualOp(): void {
    if (!this.autoActive) return;
    this.autoActive = false;
    this.refreshAutoBannerVisibility();
    this.onAutoCancel?.();
  }

  private async mountFloatDigits(): Promise<void> {
    this.hurtDigits = await DigitStrip.battleHurtNum();
    this.healDigits = await DigitStrip.battleHealNum();
  }

  private async mountTimerArt(): Promise<void> {
    const tex = await tryLoadUiTexture("ui_timer.gif");
    if (tex) {
      this.timerIcon = uiSprite(tex);
      this.timerIcon.position.set(8, 8);
      this.topHost.addChildAt(this.timerIcon, 1);
    }
    this.timerDigits = await DigitStrip.timeNum();
    if (this.timerDigits) {
      this.timerDigits.setText(String(this.turnSeconds()), 24, 12);
      this.topHost.addChild(this.timerDigits.root);
    }
    this.paintTimer();
  }

  private paintTimer(): void {
    if (this.timerDigits) {
      this.timerDigits.setText(String(Math.max(0, this.timerSec)));
      return;
    }
    void this.topTitle; /* keep title; digits optional fallback via log */
  }

  private async loadBattleMapBg(worldScene: string): Promise<void> {
    if (
      this.mapSprite &&
      this.battleMapFile &&
      this.battleMapWorld === worldScene
    ) {
      return;
    }
    const gen = ++this.mapLoadGen;
    // Keep the previous battle map (if any) while the next b_*.sce loads.
    // Clearing to a green full-screen tint flashed the LCD on every encounter.
    for (const c of [...this.mapHost.children]) {
      if (c === this.mapSprite) continue;
      this.mapHost.removeChild(c);
      c.destroy({ children: true });
    }
    if (!this.mapSprite) {
      const placeholder = new Graphics()
        .rect(0, 0, LCD_WIDTH, LCD_HEIGHT)
        .fill({ color: CBE.bgDeep });
      placeholder.eventMode = "none";
      this.mapHost.addChildAt(placeholder, 0);
    }

    const sce =
      (await resolveBattleSce(worldScene, resourceExists)) ||
      (await resolveBattleSce("17无情谷_01.sce", resourceExists));
    if (gen !== this.mapLoadGen) return;
    if (!sce) {
      console.warn("[battle] no b_*.sce for", worldScene);
      return;
    }
    this.battleMapFile = sce;
    this.battleMapWorld = worldScene;
    try {
      const scene = await loadSceneMap(sce);
      if (gen !== this.mapLoadGen) return;
      this.paintMap(scene);
      this.paintLocalActingTitle();
      console.info("[battle] map", sce, `${scene.width}x${scene.height}`);
    } catch (e) {
      console.warn("[battle] load map failed", sce, e);
    }
  }

  private paintMap(scene: LoadedScene): void {
    const expected = scene.width * scene.height * 4;
    const rgba = new Uint8ClampedArray(expected);
    rgba.set(
      scene.rgba.length >= expected
        ? scene.rgba.subarray(0, expected)
        : scene.rgba,
    );
    this.mapPixels = {
      width: scene.width,
      height: scene.height,
      rgba,
    };
    this.applyMapTexture(scene.width, scene.height, rgba);
  }

  /** Sync map sprite + letterbox from CPU pixels (load path and GPU resume). */
  private applyMapTexture(
    width: number,
    height: number,
    rgba: Uint8ClampedArray,
  ): void {
    const prev = this.mapSprite;
    const tex = rgbaToTexture(width, height, rgba);
    const spr = new Sprite(tex);
    // Fit battle map into stage band (top..bottom), cover LCD width.
    const stageH = STAGE_BOTTOM - STAGE_TOP;
    const scale = Math.max(LCD_WIDTH / width, stageH / height);
    spr.scale.set(scale);
    spr.x = (LCD_WIDTH - width * scale) / 2;
    spr.y = STAGE_TOP + (stageH - height * scale) / 2;
    this.mapSprite = spr;
    // Dark letterbox under cover-scaled map (never the old green flash tint).
    for (const c of [...this.mapHost.children]) {
      if (c === prev || c === spr) continue;
      this.mapHost.removeChild(c);
      c.destroy({ children: true });
    }
    const letterbox = new Graphics()
      .rect(0, 0, LCD_WIDTH, LCD_HEIGHT)
      .fill({ color: CBE.bgDeep });
    letterbox.eventMode = "none";
    this.mapHost.addChildAt(letterbox, 0);
    this.mapHost.addChild(spr);
    if (prev) {
      if (prev.parent) prev.parent.removeChild(prev);
      prev.destroy({ texture: true });
    }
  }

  private rebuildUnits(): void {
    // Detach follow-mode bars before destroying unit roots.
    this.enemyHud.clear();
    this.unitsHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.units = [];
    if (!this.state) return;
    this.unitsHost.sortableChildren = true;
    const leftN = this.state.left.length;
    this.state.left.forEach((side, i) => {
      const { x, y } = formationSlot("left", i, leftN);
      const res =
        side.actorResource ||
        this.visuals.leftActors?.[i] ||
        "e_mucusY.actor";
      void this.spawnUnit(side, x, y, true, res);
    });
    const rightN = this.state.right.length;
    const selfId = this.visuals.selfId ?? 0;
    this.state.right.forEach((side, i) => {
      const { x, y } = formationSlot("right", i, rightN);
      const isSelf = selfId !== 0 && (side.id >>> 0) === selfId;
      const res =
        side.actorResource ||
        (side.jobIndex != null
          ? actorResourceForJobSex(side.jobIndex, side.sex ?? 0)
          : "") ||
        // Only the local seat may fall back to observer visuals — never paint
        // teammates with the local body when roster/peer enrich missed.
        (isSelf
          ? this.visuals.playerActor ||
            actorResourceForJobSex(this.visuals.jobIndex ?? 0, this.visuals.sex ?? 0)
          : "");
      void this.spawnUnit(side, x, y, false, res || "h_warrior.actor");
    });
  }

  private async spawnUnit(
    side: BattleSide,
    x: number,
    y: number,
    facingRight: boolean,
    actorName: string,
  ): Promise<void> {
    const root = new Container();
    root.position.set(x, y);
    root.zIndex = Math.round(y);
    this.unitsHost.addChild(root);

    root.addChild(
      new Graphics()
        .ellipse(0, 2, 13, 5)
        .fill({ color: 0x000000, alpha: 0.35 }),
    );

    const view: UnitView = {
      root,
      sprite: null,
      side,
      loaded: null,
      actorName,
      facingRight,
      phase: 0,
      homeX: x,
      homeY: y,
      feetX: x,
      feetY: y,
      pose: "idle",
      poseUntil: 0,
      dead: false,
      hidden: false,
    };
    this.units.push(view);

    try {
      const loaded = await loadActor(actorName);
      if (!this.state || !view.root.parent) return;
      view.loaded = loaded;
      const frame = composeBattlePose(loaded, facingRight, "idle", 0);
      if (!frame) return;
      const key = `${loaded.name}|idle|${facingRight ? 1 : 0}|0|${frame.width}x${frame.height}`;
      let tex = this.poseTexCache.get(key);
      if (!tex) {
        tex = rgbaToTexture(frame.width, frame.height, frame.rgba);
        this.poseTexCache.set(key, tex);
        this.evictUnusedPoseTextures();
      }
      const spr = new Sprite(tex);
      this.poseTexKey.set(spr, key);
      // Foot-mass anchor — cast strips widen around the staff tip; center
      // anchor made 鬼道 look like they stepped back/out (外移).
      spr.anchor.set(battleFootAnchorX(frame), 1);
      spr.scale.set(
        actorSpriteScale(frame.artScale, !!frame.flipX).x,
        actorSpriteScale(frame.artScale, !!frame.flipX).y,
      );
      root.addChildAt(spr, 1);
      view.sprite = spr;
      // Sprite size known — re-seat follow-mode bars to the real head.
      if (facingRight && this.state) {
        this.enemyHud.setEnemies(this.state.left, this.enemyHudAnchors());
      }
    } catch (e) {
      console.warn("[battle] actor", actorName, e);
      root.addChildAt(
        new Graphics().circle(0, -18, 12).fill({ color: facingRight ? 0x884444 : 0x448866 }),
        1,
      );
    }
  }

  private paintUnitSprite(u: UnitView): void {
    if (!u.loaded || u.hidden) return;
    const frame = composeBattlePose(u.loaded, u.facingRight, u.pose, u.phase);
    if (!frame) return;
    const key = `${u.loaded.name}|${u.pose}|${u.facingRight ? 1 : 0}|${u.phase & 31}|${frame.width}x${frame.height}`;
    let tex = this.poseTexCache.get(key);
    if (!tex || tex.destroyed) {
      tex = rgbaToTexture(frame.width, frame.height, frame.rgba);
      this.poseTexCache.set(key, tex);
      this.evictUnusedPoseTextures();
    }
    if (!u.sprite || u.sprite.destroyed) {
      const spr = new Sprite(tex);
      this.poseTexKey.set(spr, key);
      spr.anchor.set(battleFootAnchorX(frame), 1);
      spr.scale.set(
        actorSpriteScale(frame.artScale, !!frame.flipX).x,
        actorSpriteScale(frame.artScale, !!frame.flipX).y,
      );
      // Shadow is children[0]; keep sprite as the body layer.
      const at = Math.min(1, u.root.children.length);
      u.root.addChildAt(spr, at);
      u.sprite = spr;
    } else if (
      this.poseTexKey.get(u.sprite) !== key ||
      u.sprite.texture?.destroyed ||
      u.sprite.texture !== tex
    ) {
      u.sprite.texture = tex;
      this.poseTexKey.set(u.sprite, key);
    }
    u.sprite.anchor.set(battleFootAnchorX(frame), 1);
    u.sprite.scale.set(
      actorSpriteScale(frame.artScale, !!frame.flipX).x,
      actorSpriteScale(frame.artScale, !!frame.flipX).y,
    );
    if (u.pose === "hit") {
      u.sprite.tint = 0xff8080;
    } else if (u.pose === "death") {
      // Enemy: gray frozen corpse before splash — monsters have no death.gif,
      // so without this tint “death” and 水花 look simultaneous.
      if (this.isEnemyUnit(u)) {
        u.sprite.tint = 0x909090;
      } else {
        u.sprite.tint = 0xffffff;
        u.sprite.alpha = 0.95;
      }
    } else {
      u.sprite.tint = 0xffffff;
      u.sprite.alpha = 1;
    }
    // Track body height so follow-mode bars stay just above the head.
    if (
      this.isEnemyUnit(u) &&
      this.state &&
      getEnemyHpBarPlacement() === "follow"
    ) {
      this.enemyHud.syncFollowHeads(this.enemyHudAnchors());
    }
  }

  private setUnitPose(u: UnitView, pose: BattlePose, holdMs: number): void {
    u.pose = pose;
    u.phase = 0;
    u.poseUntil = performance.now() + holdMs;
    this.paintUnitSprite(u);
  }

  private unitForWire(wire: number): UnitView | null {
    if (!this.state) return null;
    const side = battleUnitForWire(this.state, wire);
    if (!side) return null;
    // Prefer living/visible; still return hidden for FX anchor (death splash).
    return (
      this.units.find((u) => u.side === side && !u.hidden) ??
      this.units.find((u) => u.side === side) ??
      null
    );
  }

  private localPlayerSide(): BattleSide | null {
    const rows = this.state?.right;
    if (!rows?.length) return null;
    const selfId = (this.visuals.selfId ?? 0) >>> 0;
    return (
      (selfId ? rows.find((r) => r.id === selfId) : undefined) || rows[0] || null
    );
  }

  /** CBE `%s行动` — unit display name from battleinfo, keyed by action actor wire. */
  private paintActingTitle(name: string): void {
    const label = (name || "").trim() || "侠客";
    void this.topTitle.setText(`${label}行动`, CBE.ink, {
      smooth: false,
      outline: false,
    });
  }

  private paintLocalActingTitle(): void {
    const side = this.localPlayerSide();
    this.paintActingTitle(
      side?.name || this.visuals.playerName || "侠客",
    );
  }

  private paintActingTitleForWire(wire: number): void {
    if (!this.state) return;
    const unit = battleUnitForWire(this.state, wire);
    this.paintActingTitle(unit?.name || `目标#${wire}`);
  }

  /** Playback-time defeat gate — dead/hidden/hp<=0 must not receive dashes. */
  private isUnitDefeated(view: UnitView | null): boolean {
    if (!view) return true;
    if (view.dead || view.hidden) return true;
    return view.side.hp <= 0;
  }

  private isLocalPlayerDead(): boolean {
    const side = this.localPlayerSide();
    if (!side) return false;
    if (side.hp <= 0) return true;
    const view = this.units.find((u) => u.side === side);
    return !!view?.dead;
  }

  private deathPromptOptions(): { label: string; result: 1 | 2 }[] {
    /* PvE only (spar skips beginDeathPrompt).
     * No 801: first option is 商城购买 (client opens mall, no 1/7/14). */
    return [
      {
        label: this.deathPromptHasStone ? "使用复活石" : "商城购买",
        result: 1,
      },
      { label: "普通复活", result: 2 },
    ];
  }

  private deathPromptPrimaryResult(): 1 | 2 {
    const opts = this.deathPromptOptions();
    const pick = opts[this.deathPromptCursor] ?? opts[0];
    return pick?.result ?? 2;
  }

  /** Bag may include 801 复活石 (not in battle medicine list). */
  setDeathPromptBag(hasRevivalStone: boolean): void {
    const hadStone = this.deathPromptHasStone;
    this.deathPromptHasStone = hasRevivalStone;
    if (this.awaitingDeathPrompt && hasRevivalStone && !hadStone) {
      this.deathPromptCursor = 0;
    } else if (this.awaitingDeathPrompt && !hasRevivalStone && hadStone) {
      this.deathPromptCursor = 1;
    }
    if (this.awaitingDeathPrompt) this.paintDeathPrompt();
  }

  private beginDeathPrompt(): void {
    this.awaitingDeathPrompt = true;
    this.deathPromptBusy = false;
    this.menuOpen = false;
    this.autoActive = false;
    /* No 801: default to 普通复活 — left softkey/确定 must not open mall. */
    this.deathPromptCursor = this.deathPromptHasStone ? 0 : 1;
    this.paintDeathPrompt();
    this.refreshAutoBannerVisibility();
    void this.log.setText("你已经死亡", CBE.inkTip, {
      maxWidth: LCD_WIDTH - 16,
    });
    this.onDeathPromptShown?.();
  }

  /** Re-show death UI if 1/7/14 was rejected / produced no exit. */
  resumeDeathPrompt(): void {
    if (!this.isLocalPlayerDead()) return;
    this.beginDeathPrompt();
  }

  private paintDeathPrompt(): void {
    this.settlePaintGen += 1;
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.menuHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    clearPointerHits(this.rowHits);
    const opts = this.deathPromptOptions();
    const h = 80 + opts.length * 18;
    const box = new Graphics();
    paintCbePanel(this.settleHost, box, 20, 100, LCD_WIDTH - 40, h, CBE.bgPanelDark);
    this.settleHost.addChild(box);
    const lines = [
      "你已经死亡",
      this.deathPromptHasStone
        ? "可使用复活石原地满血（无惩罚）"
        : "无复活石 · 可选商城购买或普通",
      ...opts.map((o, i) => `${i === this.deathPromptCursor ? "▶ " : "  "}${o.label}`),
      this.deathPromptHasStone
        ? "确定=当前项 · 右键普通复活"
        : "确定=当前项 · 右键普通复活",
    ];
    lines.forEach((t, i) => {
      const lab = new BitmapLabel();
      lab.position.set(32, 112 + i * 18);
      const hi =
        i >= 2 &&
        i < 2 + opts.length &&
        i - 2 === this.deathPromptCursor;
      void lab.setText(t, hi ? CBE.inkTip : i === 0 ? SETTLE_GOLD : CBE.inkBody, {
        outline: false,
        maxWidth: LCD_WIDTH - 64,
      });
      this.settleHost.addChild(lab);
    });
    opts.forEach((o, i) => {
      const rowY = 112 + (2 + i) * 18;
      this.rowHits.push(
        attachRowPointerHit(
          this.settleHost,
          28,
          rowY - 1,
          LCD_WIDTH - 56,
          17,
          () => {
            this.deathPromptCursor = i;
            this.paintDeathPrompt();
            this.commitDeathPrompt(o.result);
          },
        ),
      );
    });
  }

  /** Softkey / menu path into 1/7/14 (1=复活石, 2=普通). */
  chooseDeathRevive(result: 1 | 2): void {
    this.commitDeathPrompt(result);
  }

  private commitDeathPrompt(result: 1 | 2): void {
    if (!this.awaitingDeathPrompt || this.deathPromptBusy) return;
    this.deathPromptBusy = true;
    void this.log.setText(
      result === 1 ? "使用复活石…" : "普通复活…",
      CBE.inkBody,
    );
    this.onDeathPromptChoice?.(result);
  }

  private async tweenUnitTo(
    u: UnitView,
    x: number,
    y: number,
    ms: number,
  ): Promise<void> {
    const x0 = u.root.x;
    const y0 = u.root.y;
    const t0 = performance.now();
    while (true) {
      const t = Math.min(1, (performance.now() - t0) / Math.max(1, ms));
      const e = t * (2 - t);
      u.root.x = x0 + (x - x0) * e;
      u.root.y = y0 + (y - y0) * e;
      u.feetX = u.root.x;
      u.feetY = u.root.y;
      if (t >= 1) break;
      await sleep(16);
    }
  }

  /** Part count for player attack/cast strip (anim6/7); drives one-shot timing. */
  private attackStripParts(actor: UnitView): number {
    if (!actor.loaded) return 8;
    const anim = actor.facingRight ? 7 : 6;
    return Math.max(1, actor.loaded.animations[anim]?.parts.length ?? 8);
  }

  /**
   * Opening motion. Cast/heal stay planted (鬼道 must not step back). Melee
   * flashes beside the foe. `windupOnly` returns after CAST_WINDUP_FRAMES so
   * eidolon/damage can overlap the remaining cast strip (CBE AnimEffect timing).
   */
  private async playAttackMotion(
    actor: UnitView,
    target: UnitView | null,
    pose: "attack" | "cast",
    opts?: { windupOnly?: boolean },
  ): Promise<void> {
    const midX = (UNIT_LEFT_X + UNIT_RIGHT_X) / 2;
    // Cast/heal: keep formation facing. Facing the heal target (often a left-side
    // ally with smaller homeX) flipped monsters left — looked like 回血后转身.
    // Melee still faces the foe for the dash/swing.
    if (pose === "cast") {
      // Pin feet — cancel any leftover melee drift so casters never retreat.
      actor.root.x = actor.homeX;
      actor.root.y = actor.homeY;
      actor.feetX = actor.homeX;
      actor.feetY = actor.homeY;
      actor.facingRight = actor.homeX < midX;
      const cycleMs = this.attackStripParts(actor) * UNIT_FRAME_MS;
      const hold = cycleMs + 80;
      this.setUnitPose(actor, "cast", hold);
      if (opts?.windupOnly) {
        await sleep(CAST_WINDUP_FRAMES * UNIT_FRAME_MS);
        return;
      }
      await sleep(Math.max(280, cycleMs));
      if (actor.pose !== "death") {
        actor.pose = "idle";
        actor.poseUntil = 0;
        this.paintUnitSprite(actor);
      }
      return;
    }
    if (target) {
      actor.facingRight = target.homeX > actor.homeX;
    } else {
      actor.facingRight = actor.homeX < midX;
    }
    this.paintUnitSprite(actor);

    // Match BattleScreen.tick cadence (UNIT_FRAME_MS/part); one strip only — no double loop.
    const cycleMs = this.attackStripParts(actor) * UNIT_FRAME_MS;
    const hold = cycleMs + 80;
    this.setUnitPose(actor, pose, hold);

    if (target) {
      // Melee: flash beside target on the side that keeps facing them, swing, return.
      const beside = actor.facingRight ? target.homeX - 30 : target.homeX + 30;
      await this.tweenUnitTo(actor, beside, target.homeY, 200);
      actor.facingRight = target.homeX > actor.homeX;
      this.paintUnitSprite(actor);
      await sleep(Math.max(280, cycleMs - 200));
      await this.tweenUnitTo(actor, actor.homeX, actor.homeY, 220);
      actor.feetX = actor.homeX;
      actor.feetY = actor.homeY;
      actor.facingRight = actor.homeX < midX;
    } else {
      // Always step toward mid-field (into the fight), never away / 后退.
      const step = actor.homeX < midX ? 14 : -14;
      await this.tweenUnitTo(actor, actor.homeX + step, actor.homeY, 160);
      await sleep(Math.max(280, cycleMs - 160));
      await this.tweenUnitTo(actor, actor.homeX, actor.homeY, 200);
      actor.feetX = actor.homeX;
      actor.feetY = actor.homeY;
    }

    if (actor.pose !== "death") {
      actor.pose = "idle";
      actor.poseUntil = 0;
      this.paintUnitSprite(actor);
    }
  }

  /** Brief hit flash. Keep short so type-3 death can start on the same beat. */
  private async playHitMotion(u: UnitView): Promise<void> {
    if (u.dead || u.hidden) return;
    const hold = HIT_FLASH_MS;
    this.setUnitPose(u, "hit", hold);
    const x0 = u.root.x;
    // Knock away from facing (into own backline), then snap back.
    u.root.x = x0 + (u.facingRight ? -5 : 5);
    await sleep(Math.min(80, hold));
    u.root.x = x0;
    await sleep(Math.max(0, hold - 80));
    if (u.pose === "hit") {
      u.pose = "idle";
      u.poseUntil = 0;
      this.paintUnitSprite(u);
    }
  }

  /**
   * Drain leftover fxHost strips, then a short beat before the next *actor*.
   * Do not spin on unit poses — long pose waits stacked with auto 4/6 and made
   * parallel stomps more likely (三怪：第一只后己方又动). Keep extraMs small so
   * skill→death in the same script stays continuous.
   */
  private async waitBattleVisualSettle(extraMs = 60): Promise<void> {
    const deadline = performance.now() + 360;
    while (
      performance.now() < deadline &&
      this.fxHost.children.length > 0
    ) {
      await sleep(30);
    }
    if (extraMs > 0) await sleep(extraMs);
  }

  /** Skip type-3/4 death records; used to see if a gem 连击 follows this beat. */
  private nextNonDeathAction(
    actions: BattleActionUpdate["actions"],
    fromIndex: number,
  ): BattleActionUpdate["actions"][number] | null {
    for (let j = fromIndex; j < actions.length; j++) {
      const n = actions[j]!;
      if (n.type === 3 || n.type === 4) continue;
      return n;
    }
    return null;
  }

  private actionIsCombo(
    rec: BattleActionUpdate["actions"][number] | null | undefined,
  ): boolean {
    return !!rec?.children.some((c) => childFlagIsCombo(c.flag));
  }

  /**
   * Combo must not overlap the prior operate beat: wait out eidolon FX and any
   * leftover attack/cast hold, snap the actor home, then a short pause.
   */
  private async waitUntilReadyForCombo(actor: UnitView | null): Promise<void> {
    await this.waitBattleVisualSettle(0);
    if (actor && !actor.dead && !actor.hidden) {
      const deadline = performance.now() + 1500;
      while (performance.now() < deadline) {
        const busy =
          actor.pose === "attack" ||
          actor.pose === "cast" ||
          (actor.poseUntil > 0 && performance.now() < actor.poseUntil);
        if (!busy) break;
        await sleep(30);
      }
      actor.pose = "idle";
      actor.poseUntil = 0;
      actor.root.x = actor.homeX;
      actor.root.y = actor.homeY;
      actor.feetX = actor.homeX;
      actor.feetY = actor.homeY;
      this.paintUnitSprite(actor);
    }
    if (BEFORE_COMBO_ACTION_MS > 0) await sleep(BEFORE_COMBO_ACTION_MS);
  }

  /** Left-side units are monsters (or spar foes); facing can flip mid-cast. */
  private isEnemyUnit(u: UnitView): boolean {
    return !!this.state?.left.includes(u.side);
  }

  /**
   * Enemy death: gray freeze then splash/fade. Splash must not gate settle /
   * the next actor — awaiting the full f_death strip left hangup single-target
   * kills idle ~0.6–1s on an empty stage before 4/7 popped.
   */
  private async playDeathMotion(
    u: UnitView,
    opts?: { briefHold?: boolean },
  ): Promise<void> {
    if (u.hidden) return;
    u.dead = true;
    if (this.isEnemyUnit(u)) {
      this.setUnitPose(u, "death", 1e9);
      if (u.sprite) u.sprite.tint = 0x909090;
      const grayMs = opts?.briefHold
        ? Math.min(120, BEFORE_DEATH_SPLASH_MS)
        : BEFORE_DEATH_SPLASH_MS;
      await sleep(grayMs);
      // Fire-and-forget: settle / next seat must not wait out splash frames.
      void this.playNamedActorFx(
        "f_death.actor",
        u.feetX,
        u.feetY - 8,
        5,
        5 * DEATH_SPLASH_FRAME_MS,
        1,
      );
      await sleep(DEATH_FADE_DELAY_MS);
      void this.fadeEnemyCorpse(u);
      return;
    }
    // Player: hold death.gif pose on stage.
    this.setUnitPose(u, "death", 1e9);
    for (let i = 0; i < 8; i++) {
      u.phase = i;
      this.paintUnitSprite(u);
      await sleep(100);
    }
  }

  /** Fade + hide enemy corpse without blocking settlement / next actor. */
  private async fadeEnemyCorpse(u: UnitView): Promise<void> {
    const t0 = performance.now();
    while (performance.now() - t0 < DEATH_FADE_MS) {
      if (!u.root.parent || u.hidden) return;
      if (u.sprite && !u.sprite.destroyed) {
        u.sprite.alpha = Math.min(
          u.sprite.alpha,
          Math.max(0, 1 - (performance.now() - t0) / DEATH_FADE_MS),
        );
      }
      await sleep(30);
    }
    if (!u.root.parent) return;
    u.hidden = true;
    u.root.visible = false;
    if (u.sprite && !u.sprite.destroyed) u.sprite.alpha = 0;
    if (this.state) {
      this.enemyHud.setEnemies(this.state.left, this.enemyHudAnchors());
    }
  }

  private refreshUnitBars(): void {
    /* enemy vitals live in BattleEnemyHud */
  }

  private enemyHudAnchors(): EnemyHudAnchor[] {
    if (!this.state) return [];
    // Index-aligned with state.left — never fold by id (clones share catalog id).
    return this.state.left.map((row) => {
      const view = this.units.find((u) => u.side === row);
      if (!view) {
        return {
          side: row,
          root: this,
          hidden: true,
          headLocalY: -36,
        };
      }
      const spr = view.sprite;
      const bodyH =
        spr && !spr.destroyed && spr.height > 4
          ? Math.ceil(Math.abs(spr.height))
          : 36;
      return {
        side: row,
        root: view.root,
        hidden: view.hidden,
        // Foot-anchored sprite: top of the body is at -height.
        headLocalY: -bodyH,
      };
    });
  }

  /** Re-apply corner/follow placement after preference change. */
  refreshEnemyHpBarPlacement(): void {
    if (!this.state || !this.visible) return;
    this.enemyHud.setEnemies(this.state.left, this.enemyHudAnchors());
  }

  private refreshBottomHud(): void {
    if (!this.state) return;
    this.partyHud.setParty(this.state.right, {
      playerName: this.visuals.playerName,
    });
    this.partyHud.refresh(this.state.right);
    this.enemyHud.setEnemies(this.state.left, this.enemyHudAnchors());
  }

  private notifyVitalsChanged(): void {
    this.refreshBottomHud();
    this.onVitalsChanged?.();
  }

  /** Apply 4/7 recover hp/mp deltas onto the observer / first right seat. */
  private applySettlementRecover(s: BattleSettlement): void {
    if (!this.state) return;
    const recoverHp = s.recoverHp | 0;
    const recoverMp = s.recoverMp | 0;
    if (!recoverHp && !recoverMp) return;
    const selfId = (this.visuals.selfId ?? 0) >>> 0;
    const row =
      (selfId ? this.state.right.find((r) => r.id === selfId) : undefined) ||
      this.state.right[0];
    if (!row) return;
    if (recoverHp > 0) {
      row.hp = Math.max(0, Math.min(row.maxHp, row.hp + recoverHp));
    }
    if (recoverMp > 0) {
      row.mp = Math.max(0, Math.min(row.maxMp, row.mp + recoverMp));
    }
  }

  /** Apply server valueA HP delta onto the battle-state unit for `wire`. */
  private applyWireHpDelta(wire: number, delta: number): void {
    if (!this.state || !delta) return;
    const unit = battleUnitForWire(this.state, wire);
    if (!unit) return;
    unit.hp = Math.max(0, Math.min(unit.maxHp, unit.hp + delta));
  }

  /**
   * teaminfo MP (and optional absolute HP) for right-side seats.
   * CBE InitActionSlot_B caches MP; DrawBattleSceneBg commits after type-1.
   */
  private applyRightTeaminfo(
    rightHp: { id: number; hp: number; mp?: number }[],
    mpOnly: boolean,
  ): void {
    if (!this.state) return;
    for (const h of rightHp) {
      const row = this.state.right.find((r) => r.id === h.id);
      if (!row) continue;
      if (h.mp != null) row.mp = h.mp;
      if (!mpOnly && h.hp != null) row.hp = Math.max(0, h.hp);
    }
  }

  /** Commit caster post-cost MP when a right-side skill/item record lands. */
  private applyCasterMpFromTeaminfo(
    rightHp: { id: number; hp: number; mp?: number }[],
    actorWire: number,
  ): void {
    if (!this.state) return;
    const unit = battleUnitForWire(this.state, actorWire);
    if (!unit || !this.state.right.includes(unit)) return;
    const tip = rightHp.find((r) => r.id === unit.id);
    if (tip?.mp != null) unit.mp = tip.mp;
  }

  private runMenu(item: MenuItem): void {
    if (item === "攻击") {
      this.attack();
      return;
    }
    if (item === "仙术") {
      this.skill();
      return;
    }
    if (item === "物品") {
      if (this.isArenaDuel()) {
        void this.log.setText("比武中不可用物品", CBE.inkTip);
        return;
      }
      this.item();
      return;
    }
    if (item === "逃跑") {
      this.flee();
      return;
    }
    if (item === "自动") {
      if (this.isArenaDuel()) {
        void this.log.setText("比武中不可自动", CBE.inkTip);
        return;
      }
      const on = this.onAutoToggle?.() ?? false;
      this.setAutoBattleActive(on);
      void this.log.setText(on ? "自动战斗开启" : "自动战斗关闭", CBE.inkTip);
      return;
    }
    void this.log.setText("查看…", CBE.inkTip);
  }

  /** Menu row label — horizontally centered like world MenuScreen. */
  private addMenuCenteredLabel(
    host: Container,
    text: string,
    cx: number,
    y: number,
    color: number,
    maxWidth?: number,
  ): void {
    const lab = new BitmapLabel();
    lab.eventMode = "none";
    lab.anchor.set(0.5, 0);
    lab.position.set(cx, y);
    void lab.setText(text, color, {
      outline: false,
      maxWidth: maxWidth != null ? maxWidth - 2 : undefined,
    });
    host.addChild(lab);
  }

  private battlePickWindowStart(cursor: number, total: number): number {
    if (total <= BATTLE_PICK_VISIBLE) return 0;
    return Math.floor(cursor / BATTLE_PICK_VISIBLE) * BATTLE_PICK_VISIBLE;
  }

  private addBattleLabelCentered(
    host: Container,
    text: string,
    cx: number,
    cy: number,
    color: number,
  ): void {
    const lab = new BitmapLabel();
    lab.eventMode = "none";
    lab.anchor.set(0.5, 0.5);
    lab.position.set(Math.round(cx), Math.round(cy));
    void lab.setText(text, color, { outline: false });
    host.addChild(lab);
  }

  private addBattleLabelLeft(
    host: Container,
    text: string,
    x: number,
    y: number,
    color: number,
    maxWidth: number,
  ): void {
    const lab = new BitmapLabel();
    lab.eventMode = "none";
    lab.position.set(x, y);
    void lab.setText(text, color, { outline: false, maxWidth });
    host.addChild(lab);
  }

  private addBattleLabelRight(
    host: Container,
    text: string,
    rightX: number,
    y: number,
    color: number,
    maxWidth: number,
  ): void {
    const lab = new BitmapLabel();
    lab.eventMode = "none";
    lab.anchor.set(1, 0);
    lab.position.set(rightX, y);
    void lab.setText(text, color, { outline: false, maxWidth });
    host.addChild(lab);
  }

  private wrapBattlePickDesc(text: string, maxCols = 14): string[] {
    const body = text.trim() || "（无说明）";
    return body.split("\n").flatMap((part) => wrapChars(part, maxCols));
  }

  private paintBattlePickPanel(
    title: string,
    entries: { name: string; right: string; desc: string }[],
    cursor: number,
    onSelect: (index: number) => void,
  ): void {
    const panelX = BATTLE_PICK_PANEL_X;
    const panelY = STAGE_TOP + 32;
    const panelW = BATTLE_PICK_PANEL_W;
    const listInset = 8;
    const total = entries.length;
    const pages = Math.max(1, Math.ceil(Math.max(total, 1) / BATTLE_PICK_VISIBLE));
    const navReserve = pages > 1 ? BATTLE_PICK_NAV_H + 4 : 0;
    const listBoxH =
      BATTLE_PICK_VISIBLE * BATTLE_PICK_ROW_H + navReserve + 8;
    const descLineH = 16;
    const descH = BATTLE_PICK_DESC_LINES * descLineH + 8;
    const panelH = BATTLE_PICK_TITLE_H + listBoxH + 8 + descH + 8;

    const chrome = new Graphics();
    chrome.eventMode = "none";
    paintCbePanel(this.menuHost, chrome, panelX, panelY, panelW, panelH, CBE.bgPanel);
    this.menuHost.addChild(chrome);

    this.addBattleLabelCentered(
      this.menuHost,
      title,
      panelX + panelW / 2,
      panelY + BATTLE_PICK_TITLE_H / 2,
      CBE.ink,
    );

    const listX = panelX + listInset;
    const listW = panelW - listInset * 2;
    const listY = panelY + BATTLE_PICK_TITLE_H;
    const listBg = new Graphics();
    listBg.eventMode = "none";
    listBg.rect(listX, listY, listW, listBoxH).fill({ color: CBE.bgPanelDark });
    this.menuHost.addChild(listBg);

    const start = this.battlePickWindowStart(cursor, total);
    const visible = entries.slice(start, start + BATTLE_PICK_VISIBLE);
    const page = Math.floor(start / BATTLE_PICK_VISIBLE) + 1;
    const rowTop = listY + 4;

    if (!total) {
      this.addBattleLabelCentered(
        this.menuHost,
        "(无)",
        listX + listW / 2,
        rowTop + BATTLE_PICK_ROW_H / 2,
        CBE.inkMint,
      );
    } else {
      visible.forEach((entry, vi) => {
        const abs = start + vi;
        const rowY = rowTop + vi * BATTLE_PICK_ROW_H;
        const hi = abs === cursor;
        if (hi) {
          const hiGfx = new Graphics()
            .rect(listX + 2, rowY, listW - 4, BATTLE_PICK_ROW_H - 1)
            .fill({ color: CBE.select });
          hiGfx.eventMode = "none";
          this.menuHost.addChild(hiGfx);
        }
        const ink = hi ? CBE.ink : CBE.inkMint;
        this.addBattleLabelLeft(
          this.menuHost,
          entry.name,
          listX + 6,
          rowY + 1,
          ink,
          listW - 56,
        );
        this.addBattleLabelRight(
          this.menuHost,
          entry.right,
          listX + listW - 6,
          rowY + 1,
          ink,
          48,
        );
        this.rowHits.push(
          attachRowPointerHit(
            this.menuHost,
            listX + 2,
            rowY,
            listW - 4,
            BATTLE_PICK_ROW_H - 1,
            () => {
              onSelect(abs);
              this.redrawMenu();
              this.confirm();
            },
          ),
        );
      });
    }

    if (pages > 1) {
      const navY = listY + listBoxH - BATTLE_PICK_NAV_H - 2;
      attachPageNavButtons({
        host: this.menuHost,
        hits: this.rowHits,
        box: chrome,
        x: listX,
        y: navY,
        w: listW,
        page,
        pages,
        addLabel: (t, lx, ly, c) =>
          this.addBattleLabelLeft(this.menuHost, t, lx, ly, c, 48),
        onPrev: () => {
          onSelect(Math.max(0, start - BATTLE_PICK_VISIBLE));
          this.redrawMenu();
        },
        onNext: () => {
          onSelect(Math.min(total - 1, start + BATTLE_PICK_VISIBLE));
          this.redrawMenu();
        },
      });
    }

    const descY = listY + listBoxH + 8;
    const descX = panelX + listInset;
    const descW = panelW - listInset * 2;
    const sel = entries[cursor];
    const descLines = this.wrapBattlePickDesc(sel?.desc ?? "", 14).slice(
      0,
      BATTLE_PICK_DESC_LINES,
    );
    descLines.forEach((line, i) => {
      this.addBattleLabelLeft(
        this.menuHost,
        line,
        descX + 2,
        descY + i * descLineH,
        CBE.ink,
        descW - 4,
      );
    });
  }

  private paintBattleSkillPick(): void {
    const entries = this.skills.map((sk) => ({
      name: sk.name,
      right: sk.mpCost != null && sk.mpCost > 0 ? `${sk.mpCost}术` : "",
      desc:
        (sk.desc && sk.desc !== "-" ? sk.desc : "") || "（无技能介绍）",
    }));
    this.paintBattlePickPanel("仙术", entries, this.skillCursor, (index) => {
      this.skillCursor = index;
    });
  }

  private paintBattleItemPick(): void {
    const entries = this.items.map((it) => {
      const cat = lookupItemSync(it.itemId);
      const desc =
        (cat?.desc && cat.desc !== "-" ? cat.desc : "") || "（无物品说明）";
      return {
        name: it.name,
        right: `${it.count}个`,
        desc,
      };
    });
    this.paintBattlePickPanel("物品", entries, this.itemCursor, (index) => {
      this.itemCursor = index;
    });
  }

  private redrawMenu(): void {
    clearPointerHits(this.rowHits);
    this.menuHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    if (this.awaitingExit || this.awaitingDeathPrompt || !this.menuOpen) return;
    // Target pick: hide action list; cursor mark + log carry the UX.
    if (this.menuMode === "target") {
      this.refreshTargetMark();
      this.menuHost.eventMode = "static";
      for (let i = 0; i < this.targetWires.length; i++) {
        const view = this.unitForWire(this.targetWires[i]!);
        if (!view) continue;
        const idx = i;
        const hitW = 44;
        const hitH = 64;
        this.rowHits.push(
          attachRowPointerHit(
            this.menuHost,
            view.feetX - hitW / 2,
            view.feetY - hitH,
            hitW,
            hitH,
            () => {
              this.targetCursor = idx;
              this.redrawMenu();
              this.confirm();
            },
          ),
        );
      }
      return;
    }

    this.menuHost.eventMode = "static";
    if (this.menuMode === "skill") {
      this.paintBattleSkillPick();
      return;
    }
    if (this.menuMode === "item") {
      this.paintBattleItemPick();
      return;
    }

    const rootItems = this.rootMenuItems();
    const rows = rootItems.length;
    const listH = rows * BATTLE_MENU_ROW_H;
    const h =
      MENU2_CAP_H * 2 +
      BATTLE_MENU_TITLE_BAND +
      listH +
      BATTLE_MENU_BOTTOM_PAD;
    const w = 100;
    const x = (LCD_WIDTH - w) / 2;
    const y = STAGE_TOP + 40;

    addMenuScrollBackground(this.menuHost, x, y, w, h, "parchment");

    const whiteW = Math.max(MENU2_CENTER_W, w - MENU2_LEFT_W - MENU2_RIGHT_W);
    const content = menu2ContentRect(x, whiteW);
    const listTop = y + MENU2_CAP_H + BATTLE_MENU_TITLE_BAND;

    const whiteBox = new Graphics();
    whiteBox.eventMode = "none";
    whiteBox
      .rect(content.x, listTop, content.w, listH)
      .fill({ color: MENU2_WHITE_FILL });
    this.menuHost.addChild(whiteBox);

    const title = new BitmapLabel();
    title.eventMode = "none";
    title.anchor.set(0.5, 0);
    title.position.set(content.cx, y + MENU2_CAP_H + 2);
    void title.setText("行动", SETTLE_GOLD, { outline: false });
    this.menuHost.addChild(title);

    if (this.menuCursor >= rootItems.length) this.menuCursor = 0;
    rootItems.forEach((label, i) => {
      const rowY = listTop + i * BATTLE_MENU_ROW_H;
      const hi = i === this.menuCursor;
      if (hi) {
        const hiGfx = new Graphics()
          .rect(content.x, rowY, content.w, BATTLE_MENU_ROW_H - 1)
          .fill({ color: BATTLE_MENU_SEL_FILL });
        hiGfx.eventMode = "none";
        this.menuHost.addChild(hiGfx);
      }
      this.addMenuCenteredLabel(
        this.menuHost,
        label,
        content.cx,
        rowY + 1,
        hi ? 0xffffff : 0x202020,
      );
      const idx = i;
      this.rowHits.push(
        attachRowPointerHit(
          this.menuHost,
          content.x,
          rowY,
          content.w,
          BATTLE_MENU_ROW_H - 1,
          () => {
          this.menuCursor = idx;
          this.redrawMenu();
          this.confirm();
        },
        ),
      );
    });
  }

  private async playActionScript(up: BattleActionUpdate): Promise<void> {
    if (!this.state) return;
    const actions = up.actions?.length ? up.actions : [];

    if (!actions.length) {
      this.playFloatsFromDeltas(up);
      await sleep(280);
      return;
    }

    /**
     * type-3/4 wires whose death splash already started during the killing
     * record (CBE plays damage+death as one beat). Skip when the script
     * reaches those slots so we do not double-play.
     */
    const consumedDeathWires = new Set<number>();

    for (let ai = 0; ai < actions.length; ai++) {
      const rec = actions[ai]!;
      // CBE DrawBattleSceneBg `%s行动`: advance with each actioninfo actor.
      this.paintActingTitleForWire(rec.actorWire);
      if (rec.type === 3 || rec.type === 4) {
        if (consumedDeathWires.has(rec.actorWire)) {
          consumedDeathWires.delete(rec.actorWire);
          continue;
        }
        const dead = this.unitForWire(rec.actorWire);
        if (dead && !dead.hidden) await this.playDeathMotion(dead);
        else if (!dead) await sleep(400);
        continue;
      }

      const actor = this.unitForWire(rec.actorWire);
      const reflectOnly =
        rec.children.length > 0 &&
        rec.children.every((c) => c.flag === 4);
      const reviveOnly =
        rec.children.length > 0 &&
        rec.children.every((c) => c.flag === 6);
      const isComboAction = this.actionIsCombo(rec);
      // Gem 连击: never overlap the prior operate beat (FX / cast / dash home).
      if (isComboAction) {
        await this.waitUntilReadyForCombo(actor);
      }
      const offensiveGroup =
        rec.children.length > 1 && rec.children.some((c) => c.delta < 0);
      const childWires = rec.children.map((c) => c.wire);
      // Reflect is a float/HP-only sibling on the enemy strike — not a dash target.
      const offensiveChildren = rec.children.filter(
        (c) => c.delta < 0 && c.flag !== 3 && c.flag !== 4,
      );
      const livingOffensiveWires = offensiveChildren
        .map((c) => c.wire)
        .filter((w) => !this.isUnitDefeated(this.unitForWire(w)));
      const allOffensiveDefeated =
        offensiveChildren.length > 0 && livingOffensiveWires.length === 0;
      const firstTargetWire =
        livingOffensiveWires[0] ??
        childWires.find((w) => !this.isUnitDefeated(this.unitForWire(w))) ??
        childWires[0];
      const firstTarget =
        firstTargetWire != null ? this.unitForWire(firstTargetWire) : null;
      const isSkill = rec.type === 1 || rec.type === 2;
      const fxWires = livingOffensiveWires.length
        ? livingOffensiveWires
        : childWires.length
          ? childWires
          : [rec.actorWire];
      const effectIndex =
        isSkill && rec.effectIndex != null && rec.effectIndex > 0
          ? rec.effectIndex
          : 0;

      // Team merge keeps later seats' baked hits on already-dead wires.
      // Skip dash/FX/floats for wholly defeated offensive records; still apply
      // HP/MP deltas so vitals stay in lock-step with the server script.
      if (allOffensiveDefeated) {
        for (let i = 0; i < rec.children.length; i++) {
          const c = rec.children[i]!;
          const wire = childWires[i] ?? c.wire;
          this.applyWireHpDelta(wire, c.delta);
        }
        if (isSkill) {
          this.applyCasterMpFromTeaminfo(up.rightHp, rec.actorWire);
        }
        this.notifyVitalsChanged();
        continue;
      }

      // Immediate trailing type-3/4 for this kill — death after damage beat.
      const followDeathWires = new Set<number>();
      for (let j = ai + 1; j < actions.length; j++) {
        const n = actions[j]!;
        if (n.type !== 3 && n.type !== 4) break;
        followDeathWires.add(n.actorWire);
      }

      let skillFx: Promise<void> | null = null;
      const hitWaits: Promise<void>[] = [];

      if (!reflectOnly && !reviveOnly && actor && !actor.dead) {
        const actorEnemy = this.isEnemyUnit(actor);
        const targetEnemy = !!firstTarget && this.isEnemyUnit(firstTarget);
        // True wire desync is distinct enemy wires. Same-wire is boss self-heal
        // / buff (subtype-10 actor==target on left) — not team-table leak.
        if (
          actorEnemy &&
          targetEnemy &&
          firstTargetWire != null &&
          firstTargetWire !== rec.actorWire &&
          offensiveChildren.length > 0
        ) {
          console.error(
            `[battle] monster-vs-monster dash actorWire=${rec.actorWire} ` +
              `targetWire=${firstTargetWire} P=${this.state.right.length} ` +
              `M=${this.state.left.length} evidence=team-wire-desync`,
          );
        }
        if (isSkill) {
          // Combo waits out the full cast + eidolon before the enemy turn.
          // Primary hits still overlap eidolon after a short windup.
          if (isComboAction) {
            await this.playAttackMotion(
              actor,
              firstTarget && !this.isUnitDefeated(firstTarget)
                ? firstTarget
                : null,
              "cast",
            );
            if (effectIndex > 0) {
              skillFx = this.playSkillEffects(
                effectIndex,
                fxWires,
                actor,
                !offensiveGroup,
              );
            }
          } else {
            // Planted cast windup, then eidolon + damage on the same beat.
            await this.playAttackMotion(
              actor,
              firstTarget && !this.isUnitDefeated(firstTarget)
                ? firstTarget
                : null,
              "cast",
              { windupOnly: true },
            );
            if (effectIndex > 0) {
              skillFx = this.playSkillEffects(
                effectIndex,
                fxWires,
                actor,
                /* fallbackToActor */ !offensiveGroup,
              );
            }
          }
        } else {
          await this.playAttackMotion(
            actor,
            firstTarget && !this.isUnitDefeated(firstTarget)
              ? firstTarget
              : null,
            "attack",
          );
        }
      } else if (!reflectOnly && !reviveOnly && effectIndex > 0) {
        skillFx = this.playSkillEffects(
          effectIndex,
          fxWires,
          actor,
          !offensiveGroup,
        );
      }

      // Damage floats land with FX start (same beat). Death waits until after.
      const deathTargets: UnitView[] = [];
      let comboTipOnActor = false;
      for (let i = 0; i < rec.children.length; i++) {
        const c = rec.children[i]!;
        const wire = childWires[i] ?? c.wire;
        const tgt = this.unitForWire(wire);
        const offensiveHit = c.delta < 0 && c.flag !== 3 && c.flag !== 4;
        if (offensiveHit && this.isUnitDefeated(tgt)) {
          this.applyWireHpDelta(wire, c.delta);
          continue;
        }
        this.spawnFloatAtWire(wire, c.delta, c.flag, rec.effectText);
        if (childFlagIsCombo(c.flag) && !comboTipOnActor) {
          comboTipOnActor = true;
          const ax = actor?.root.x ?? actor?.feetX ?? UNIT_RIGHT_X;
          const ay = (actor?.root.y ?? actor?.feetY ?? UNIT_FEET_Y) - 48;
          this.spawnTextFloat("连击", ax, ay + 14, CBE.inkTip, 0x000000, true);
        }
        this.applyWireHpDelta(wire, c.delta);
        if (c.flag === 6 && tgt && tgt.side.hp > 0) {
          tgt.dead = false;
          tgt.hidden = false;
          tgt.root.visible = true;
          if (tgt.pose === "death") {
            tgt.pose = "idle";
            tgt.poseUntil = 0;
            tgt.root.x = tgt.homeX;
            tgt.root.y = tgt.homeY;
            this.paintUnitSprite(tgt);
          }
        }
        if (
          tgt &&
          !tgt.hidden &&
          followDeathWires.has(wire) &&
          (tgt.side.hp <= 0 || this.isUnitDefeated(tgt))
        ) {
          consumedDeathWires.add(wire);
          deathTargets.push(tgt);
          // Still flash 受击 before death — AOE secondaries used to skip hit
          // and jump gray/settle (hangup multi-kill looked like no hit turn).
          if (offensiveHit) {
            hitWaits.push(this.playHitMotion(tgt));
          }
          continue;
        }
        if (tgt && offensiveHit) {
          hitWaits.push(this.playHitMotion(tgt));
        }
      }
      if (isSkill) {
        this.applyCasterMpFromTeaminfo(up.rightHp, rec.actorWire);
      }
      this.notifyVitalsChanged();

      if (hitWaits.length) await Promise.all(hitWaits);

      const nextPlayable = this.nextNonDeathAction(actions, ai + 1);
      const nextIsCombo = this.actionIsCombo(nextPlayable);
      // Only shorten death cue when this script truly wiped every living foe.
      // Survivors still need their hit/counter records before any 4/7.
      const survivors = this.units.filter(
        (u) =>
          this.isEnemyUnit(u) &&
          !u.hidden &&
          !deathTargets.includes(u) &&
          !this.isUnitDefeated(u),
      );
      const wipeKill =
        deathTargets.length > 0 && !nextPlayable && survivors.length === 0;

      // Death after damage: pose → brief hold → splash (splash/fade async).
      if (deathTargets.length) {
        const deathLead = wipeKill
          ? Math.min(100, AFTER_DAMAGE_BEFORE_DEATH_MS)
          : AFTER_DAMAGE_BEFORE_DEATH_MS;
        await sleep(deathLead);
        await Promise.all(
          deathTargets.map((t) =>
            this.playDeathMotion(t, { briefHold: wipeKill }),
          ),
        );
        if ((nextIsCombo || isComboAction) && skillFx) await skillFx;
      } else if (skillFx) {
        // Non-lethal / no inline death: finish eidolon before next actor.
        // Lethal path above must not await leftover eidolon under an empty stage.
        await skillFx;
      }

      if (actor && (actor.pose === "cast" || (isComboAction && actor.pose !== "death"))) {
        actor.pose = "idle";
        actor.poseUntil = 0;
        actor.root.x = actor.homeX;
        actor.root.y = actor.homeY;
        actor.feetX = actor.homeX;
        actor.feetY = actor.homeY;
        this.paintUnitSprite(actor);
      }

      // After 连击 fully settles, enemy counters (next records) may start.
      const next = actions[ai + 1];
      if (isComboAction) {
        await this.waitUntilReadyForCombo(actor);
      } else if (next && next.type !== 3 && next.type !== 4 && !nextIsCombo) {
        await this.waitBattleVisualSettle(60);
      }
    }

    /*
     * Co-packet 4/7 may arrive after HP deltas zeroed several foes but only one
     * type-3 was inlined. Play hit+death for any leftover 0-HP corpses so
     * hangup multi-enemy wipes do not jump to settle while models still idle.
     */
    if (up.settlement && this.state) {
      const pendingCorpses = this.units.filter(
        (u) =>
          this.isEnemyUnit(u) &&
          !u.hidden &&
          !u.dead &&
          u.side.hp <= 0,
      );
      if (pendingCorpses.length) {
        await Promise.all(pendingCorpses.map((u) => this.playHitMotion(u)));
        await Promise.all(
          pendingCorpses.map((u) =>
            this.playDeathMotion(u, { briefHold: true }),
          ),
        );
      }
    }
  }

  /**
   * Play eidolon FX on every resolved wire.
   * Offensive group skills must not fall back onto the caster — hangup wire
   * desync used to paint 荒魂劫火/天火熔身/雷震八方 on 本方.
   */
  private async playSkillEffects(
    effectIndex: number,
    wires: number[],
    actor: UnitView | null,
    fallbackToActor = true,
  ): Promise<void> {
    const actorName = await eidolonActorForIndex(effectIndex);
    if (!actorName) {
      console.warn(`[battle] eidolon missing effectIndex=${effectIndex}`);
      await sleep(400);
      return;
    }
    const spots: { x: number; y: number }[] = [];
    for (const w of wires) {
      const view = this.unitForWire(w);
      if (view) spots.push({ x: view.feetX, y: view.feetY - 24 });
    }
    if (!spots.length && !fallbackToActor) {
      for (const w of this.livingEnemyWires()) {
        const view = this.unitForWire(w);
        if (view) spots.push({ x: view.feetX, y: view.feetY - 24 });
      }
    }
    if (!spots.length && fallbackToActor && actor) {
      spots.push({ x: actor.feetX, y: actor.feetY - 24 });
    }
    if (!spots.length) {
      spots.push({ x: LCD_WIDTH / 2, y: UNIT_FEET_Y - 24 });
    }
    await Promise.all(
      spots.map((p) =>
        this.playNamedActorFx(actorName, p.x, p.y, undefined, undefined, 0.85),
      ),
    );
  }

  private playFloatsFromDeltas(up: BattleActionUpdate): void {
    for (const d of up.hpDeltas ?? []) {
      this.spawnFloatAtWire(d.wire, d.delta, d.flag ?? 0);
    }
  }

  /**
   * Play a JHOnlineData `.actor` strip (eidolon FX / `f_death` splash).
   * `maxParts` caps frames when set (`f_death` has 5 splash parts).
   * Duration defaults to frameCount * FX_FRAME_MS (CBE part tick).
   */
  private async playNamedActorFx(
    actorName: string,
    x: number,
    y: number,
    maxParts?: number,
    durationMs?: number,
    anchorY = 1,
  ): Promise<void> {
    try {
      const loaded = await loadActor(actorName);
      const host = new Container();
      host.position.set(x, y);
      this.fxHost.addChild(host);
      const partCount = Math.max(
        loaded.animations[0]?.parts.length ?? 0,
        loaded.animations[1]?.parts.length ?? 0,
      );
      const frames =
        maxParts != null
          ? Math.min(maxParts, Math.max(1, partCount))
          : Math.max(1, partCount || 4);
      const totalMs = durationMs ?? frames * FX_FRAME_MS;
      const frameMs = Math.max(40, Math.floor(totalMs / frames));
      let painted = 0;
      for (let i = 0; i < frames; i++) {
        if (!host.parent) break;
        const composed =
          composePartStacked(loaded, 0, i) ||
          composePartStacked(loaded, 1, i) ||
          composeMapActor(loaded, 3, false, i) ||
          composeMapActor(loaded, 2, false, i);
        if (!composed) continue;
        host.removeChildren().forEach((c) =>
          c.destroy({ children: true, texture: true }),
        );
        const spr = new Sprite(
          rgbaToTexture(composed.width, composed.height, composed.rgba),
        );
        spr.anchor.set(0.5, anchorY);
        const fit = Math.min(
          ACTOR_DISPLAY_SCALE,
          FX_MAX_W / Math.max(1, composed.width),
          FX_MAX_H / Math.max(1, composed.height),
        );
        spr.scale.set(fit);
        host.addChild(spr);
        painted += 1;
        await sleep(frameMs);
      }
      if (!painted) {
        console.warn(`[battle] fx empty frames ${actorName}`);
        await sleep(Math.min(600, totalMs));
      }
      host.destroy({ children: true, texture: true });
    } catch (e) {
      console.warn("[battle] fx", actorName, e);
      await sleep(Math.min(600, durationMs ?? 600));
    }
  }

  private spawnFloatAtWire(
    wire: number,
    delta: number,
    flag: number,
    effectText?: string,
  ): void {
    if (!this.state) return;
    const view = this.unitForWire(wire);
    // Prefer head/chest of the resolved unit; left-side fallback is monster column.
    const x = view?.root.x ?? view?.feetX ?? UNIT_LEFT_X;
    const yBody = (view?.root.y ?? view?.feetY ?? UNIT_FEET_Y) - 48;
    // mmBattle 0x24f6: flag 2 → 「暴击」, 3 → 「闪躲」, else numeric float.
    // Pixi also stacks the damage digits under 「暴击」 so the amount stays visible.
    if (flag === 3) {
      // Cyan fill + black outline.
      this.spawnTextFloat("闪躲", x, yBody, CBE.inkTip, 0x000000, true);
      return;
    }
    if (flag === 2 || flag === 7) {
      // Damage higher so 「暴击」 below does not cover it.
      // Flag 7 is a gem combo that also crit; 「连击」 is drawn on the actor.
      if (delta !== 0) this.spawnDigitFloat(delta, x, yBody - 22);
      this.spawnTextFloat("暴击", x, yBody + 14, CBE.inkTip, 0x000000, true);
      return;
    }
    if (flag === 4) {
      if (delta !== 0) this.spawnDigitFloat(delta, x, yBody - 22);
      this.spawnTextFloat("反伤", x, yBody + 14, CBE.inkTip, 0x000000, true);
      return;
    }
    if (flag === 5) {
      // Digits stay on the hit target; 「连击」tip is drawn once on the actor.
      if (delta !== 0) this.spawnDigitFloat(delta, x, yBody - 22);
      return;
    }
    if (flag === 6) {
      if (delta > 0) this.spawnDigitFloat(delta, x, yBody - 22);
      this.spawnTextFloat(
        "复活",
        x,
        delta > 0 ? yBody + 14 : yBody,
        CBE.inkTip,
        0x000000,
        true,
      );
      return;
    }
    if (delta !== 0) {
      this.spawnDigitFloat(delta, x, yBody - 10);
      return;
    }
    const label = effectText?.slice(0, 4) || "";
    if (!label) return;
    this.spawnTextFloat(label, x, yBody, CBE.inkTip);
  }

  /**
   * Keep centered floats inside the 240 LCD. Left/right units sit near x=22/218;
   * pad sized so a 5-digit -num/+num row (~55px) stays fully visible.
   */
  private clampFloatX(x: number, halfW: number): number {
    const pad = 10;
    const minX = halfW + pad;
    const maxX = LCD_WIDTH - halfW - pad;
    if (minX >= maxX) return LCD_WIDTH / 2;
    return Math.max(minX, Math.min(maxX, x));
  }

  /** Pull left/right column floats toward stage center before edge clamp. */
  private inwardFloatX(x: number): number {
    const mid = LCD_WIDTH / 2;
    const pull = 16;
    if (x < mid) return x + pull;
    if (x > mid) return x - pull;
    return x;
  }

  private spawnDigitFloat(delta: number, x: number, y: number): void {
    const strip = delta > 0 ? this.healDigits : this.hurtDigits;
    const digits = String(Math.abs(delta | 0));
    if (strip) {
      const row = strip.buildRow(digits);
      // Prefer cellW×len over row.width so clamp works before layout settles.
      const halfW = Math.max(row.width, digits.length * strip.cellW) / 2;
      row.pivot.set(halfW, 0);
      row.position.set(this.clampFloatX(this.inwardFloatX(x), halfW), y);
      this.floatHost.addChild(row);
      this.animateFloatNode(row, y, 1500, 0.032);
      return;
    }
    const color = delta > 0 ? HEAL_GREEN : MONSTER_RED;
    this.spawnTextFloat(delta > 0 ? `+${delta}` : `${delta}`, x, y, color);
  }

  private spawnTextFloat(
    text: string,
    x: number,
    y: number,
    color: number,
    outlineColor = 0x000000,
    withOutline = false,
  ): void {
    const lab = new BitmapLabel();
    lab.anchor.set(0.5, 0.5);
    // Estimate half-width before async glyph layout (UC3 ~14px/glyph).
    const halfW = Math.min(40, (text.length * 14) / 2);
    lab.position.set(this.clampFloatX(this.inwardFloatX(x), halfW), y);
    this.floatHost.addChild(lab);
    void lab.setText(text, color, {
      outline: withOutline,
      outlineColor,
      outlineThin: withOutline,
      maxWidth: 80,
    });
    this.animateFloatNode(lab, y);
  }

  private animateFloatNode(
    node: Container,
    y0: number,
    lifeMs = 900,
    risePxPerMs = 0.045,
  ): void {
    const started = performance.now();
    const tick = () => {
      if (!node.parent) return;
      const elapsed = performance.now() - started;
      node.y = y0 - elapsed * risePxPerMs;
      node.alpha = Math.max(0, 1 - elapsed / lifeMs);
      if (elapsed >= lifeMs) {
        node.destroy({ children: true });
        return;
      }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }

  private spawnFloat(text: string, x: number, y: number, color: number): void {
    this.spawnTextFloat(text, x, y, color);
  }

  private clearFloats(): void {
    this.floatHost.removeChildren().forEach((c) => c.destroy({ children: true }));
  }

  private shouldPaintHangupSettlement(s: BattleSettlement): boolean {
    return (
      s.result === 1 &&
      (this.hangupActive ||
        this.hangupTallyArmed ||
        s.hangupCombatinfo != null)
    );
  }

  private showSettlement(s: BattleSettlement): void {
    this.awaitingDeathPrompt = false;
    this.settleConfirmBusy = false;
    this.clearSettleCloseTimer();
    this.clearFloats();
    this.menuHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    if (this.shouldPaintHangupSettlement(s)) {
      void this.paintHangupSettlement(s);
      return;
    }
    void this.paintSettlement(s);
  }

  private resetHangupTally(maximum = 64): void {
    this.hangupTally = {
      completed: 0,
      maximum: maximum >>> 0,
      totalExp: 0,
      totalGold: 0,
      totalHpRecovered: 0,
      totalMpRecovered: 0,
    };
    this.hangupTallyArmed = true;
  }

  /**
   * Session totals for hangup settle — always accumulate client-side.
   * Wire combatinfo often resets completed per fight (1/64…); ignore it for
   * the running 战斗场次 / exp / gold / recover counters.
   */
  private hangupSnapshot(s: BattleSettlement): HangupCombatinfo {
    if (!this.hangupTallyArmed) this.resetHangupTally();
    const t = this.hangupTally;
    const wire = s.hangupCombatinfo;
    if (wire && wire.maximum >= 200) t.maximum = wire.maximum >>> 0;
    t.completed = Math.min(0xffff, t.completed + 1);
    t.totalExp = (t.totalExp + Math.max(0, s.expGain | 0)) >>> 0;
    t.totalGold = (t.totalGold + Math.max(0, s.goldGain | 0)) >>> 0;
    t.totalHpRecovered =
      (t.totalHpRecovered + Math.max(0, s.recoverHp | 0)) >>> 0;
    t.totalMpRecovered =
      (t.totalMpRecovered + Math.max(0, s.recoverMp | 0)) >>> 0;
    return { ...t };
  }

  private clearSettleCloseTimer(): void {
    if (this.settleCloseTimer != null) {
      clearTimeout(this.settleCloseTimer);
      this.settleCloseTimer = null;
    }
  }

  /** Softkey / 关闭 / 2.5s auto — leave battle once. */
  private confirmSettlement(): void {
    if (!this.awaitingExit || this.settleConfirmBusy) return;
    this.settleConfirmBusy = true;
    this.clearSettleCloseTimer();
    this.settlePaintGen += 1;
    this.onSettleConfirm?.();
  }

  /**
   * Native FGS keepalive: confirm settle while document.hidden so the
   * 2.5s setTimeout (and Chromium hidden-tab throttle) cannot stall hangup.
   */
  confirmSettlementIfAwaiting(): boolean {
    if (!this.awaitingExit || this.settleConfirmBusy) return false;
    this.confirmSettlement();
    return true;
  }

  private armSettleAutoClose(gen: number): void {
    this.clearSettleCloseTimer();
    const hidden =
      typeof document !== "undefined" && document.visibilityState === "hidden";
    // Background: 0ms — FGS keepalive also calls confirmSettlementIfAwaiting
    // if Chromium still starves this timer.
    const delayMs = hidden ? 0 : 2500;
    this.settleCloseTimer = setTimeout(() => {
      this.settleCloseTimer = null;
      if (gen !== this.settlePaintGen) return;
      this.confirmSettlement();
    }, delayMs);
  }

  /**
   * Hangup session summary — native mmBattle combatinfo panel
   * (concernf1/concernp/effect/help/addtime/pickupimage1 + dark value pills).
   */
  private async paintHangupSettlement(s: BattleSettlement): Promise<void> {
    const gen = ++this.settlePaintGen;
    const tally = this.hangupSnapshot(s);
    const bagUsed = this.hangupBagUsed;
    const bagCap = this.hangupBagCap > 0 ? this.hangupBagCap : 64;

    /* Auto-refresh 战斗心得 remain — do not require map「修」tap first. */
    if (this.onHangupInsightRefresh) {
      try {
        const sec = await this.onHangupInsightRefresh();
        if (gen !== this.settlePaintGen) return;
        this.insightRemainSec = Math.max(0, sec | 0);
      } catch {
        /* keep prior insightRemainSec */
      }
    }

    const ok = await paintHangupPanel(this.settleHost, {
      tally,
      bagUsed,
      bagCap,
      insightRemainSec: this.insightRemainSec,
      onStop: () => {
        if (gen !== this.settlePaintGen) return;
        this.onHangupCancel?.();
        this.confirmSettlement();
      },
      onAddTime: () => {
        if (gen !== this.settlePaintGen) return;
        void this.handleHangupAddTime(gen, s, tally, bagUsed, bagCap);
      },
    });
    if (gen !== this.settlePaintGen) return;
    if (!ok) {
      /* Fallback if GIF fetch failed — keep prior text-only path. */
      await this.paintHangupSettlementFallback(s, gen, tally, bagUsed, bagCap);
      return;
    }
    this.armSettleAutoClose(gen);
  }

  private hangupAddTimeBusy = false;

  /**
   * 「补充时间」: pause auto-close, let main use bag 828, then refresh remain.
   */
  private async handleHangupAddTime(
    gen: number,
    s: BattleSettlement,
    tally: HangupCombatinfo,
    bagUsed: number,
    bagCap: number,
  ): Promise<void> {
    if (this.hangupAddTimeBusy || gen !== this.settlePaintGen) return;
    this.hangupAddTimeBusy = true;
    this.clearSettleCloseTimer();
    try {
      await this.onHangupAddTime?.();
    } finally {
      this.hangupAddTimeBusy = false;
    }
    if (gen !== this.settlePaintGen) return;
    const nextBagUsed = this.hangupBagUsed;
    const nextBagCap = this.hangupBagCap > 0 ? this.hangupBagCap : bagCap;
    const ok = await paintHangupPanel(this.settleHost, {
      tally,
      bagUsed: nextBagUsed,
      bagCap: nextBagCap,
      insightRemainSec: this.insightRemainSec,
      onStop: () => {
        if (gen !== this.settlePaintGen) return;
        this.onHangupCancel?.();
        this.confirmSettlement();
      },
      onAddTime: () => {
        if (gen !== this.settlePaintGen) return;
        void this.handleHangupAddTime(gen, s, tally, nextBagUsed, nextBagCap);
      },
    });
    if (gen !== this.settlePaintGen) return;
    if (!ok) {
      await this.paintHangupSettlementFallback(
        s,
        gen,
        tally,
        nextBagUsed,
        nextBagCap,
      );
      return;
    }
    this.armSettleAutoClose(gen);
  }

  /** Text-only fallback when JHOnlineData chrome is unavailable. */
  private async paintHangupSettlementFallback(
    s: BattleSettlement,
    gen: number,
    tally: HangupCombatinfo,
    bagUsed: number,
    bagCap: number,
  ): Promise<void> {
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    const panelX = 10;
    const panelY = 102;
    const panelW = LCD_WIDTH - 20;
    const box = new Graphics();
    paintCbePanel(this.settleHost, box, panelX, panelY, panelW, 180, CBE.bgPanelDark);
    this.settleHost.addChild(box);
    this.settleHost.eventMode = "static";
    const labels = await loadHangupLabelTextures();
    if (gen !== this.settlePaintGen) return;
    const titleLab = new BitmapLabel();
    titleLab.anchor.set(0.5, 0);
    titleLab.position.set(LCD_WIDTH / 2, panelY + 8);
    titleLab.show("挂机中", SETTLE_GOLD, { outline: false, fontSize: SETTLE_FS });
    this.settleHost.addChild(titleLab);
    let y = panelY + 30;
    const row = (text: string) => {
      const lab = new BitmapLabel();
      lab.position.set(panelX + 10, y);
      lab.show(text, CBE.inkMint, { outline: false, fontSize: SETTLE_FS });
      this.settleHost.addChild(lab);
      y += SETTLE_FS + 3;
    };
    row(`战斗场次 ${tally.completed}  背包 ${bagUsed}/${bagCap}`);
    row(`获得经验 ${tally.totalExp}  金钱 ${tally.totalGold}`);
    row(`自动回血 ${tally.totalHpRecovered}  回魔 ${tally.totalMpRecovered}`);
    this.armSettleAutoClose(gen);
  }

  /** Bag used/cap for hangup settle panel (from session backpack). */
  private hangupBagUsed = 0;
  private hangupBagCap = 64;
  /** Item 828 remaining — same wall-clock as map「修」status tip. */
  private insightRemainSec = 0;

  setHangupBagSlots(used: number, capacity: number): void {
    this.hangupBagUsed = Math.max(0, used | 0);
    this.hangupBagCap = Math.max(1, capacity | 0);
  }

  setBattleInsightRemainSec(sec: number): void {
    this.insightRemainSec = Math.max(0, sec | 0);
  }

  /**
   * CBE settle panel — native pickupimage1 2-col + bag bar + fdata drops.
   * Arena/spar keeps text layout; PvE uses battleSettleUi chrome.
   */
  private async paintSettlement(s: BattleSettlement): Promise<void> {
    const gen = ++this.settlePaintGen;
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));

    const spar = this.isSparBattle();
    const arenaFdata =
      spar &&
      (s.fdata.includes("比武胜利") ||
        s.fdata.includes("比武失败") ||
        s.fdata.includes("比武平局"));
    const lose =
      spar &&
      (s.fdata.includes("挑战失败") ||
        s.fdata.includes("比武失败") ||
        s.expGain < 0 ||
        s.goldGain < 0);
    const draw = spar && s.fdata.includes("比武平局");

    if (!arenaFdata) {
      const bagUsed = this.hangupBagUsed;
      const bagCap = this.hangupBagCap > 0 ? this.hangupBagCap : 64;
      const ok = await paintBattleSettlePanel(this.settleHost, {
        expGain: s.expGain,
        goldGain: s.goldGain,
        recoverHp: s.recoverHp,
        recoverMp: s.recoverMp,
        bagUsed,
        bagCap,
        drops: parseSettleDropLines(s.fdata),
        onClose: () => {
          if (gen !== this.settlePaintGen) return;
          this.confirmSettlement();
        },
      });
      if (gen !== this.settlePaintGen) return;
      if (ok) {
        this.armSettleAutoClose(gen);
        return;
      }
    }

    const parsed = parseSettleFdata(s.fdata);
    const title = arenaFdata
      ? draw
        ? "比武平局"
        : lose
          ? "比武失败"
          : "比武胜利"
      : spar
        ? lose
          ? "切磋失败"
          : "切磋胜利"
        : parsed.title || "战斗胜利";

    const rows: SettleRow[] = [];
    if (arenaFdata) {
      rows.push({ kind: "text", text: s.fdata, color: CBE.inkBody });
    } else {
      rows.push({ kind: "stat", label: "exp", value: formatSettleDelta(s.expGain) });
      rows.push({ kind: "stat", label: "gold", value: formatSettleDelta(s.goldGain) });
      rows.push({
        kind: "stat",
        label: "hp",
        value: formatSettleDelta(s.recoverHp),
      });
      rows.push({
        kind: "stat",
        label: "mp",
        value: formatSettleDelta(s.recoverMp),
      });
      if (parsed.bloodFlask) {
        rows.push({
          kind: "text",
          text: parsed.bloodFlask,
          color: CBE.inkMint,
        });
      }
      if (parsed.blueFlask) {
        rows.push({
          kind: "text",
          text: parsed.blueFlask,
          color: CBE.inkMint,
        });
      }
      if (parsed.extra) {
        rows.push({
          kind: "text",
          text: parsed.extra,
          color: CBE.inkWarn,
        });
      }
    }

    const panelX = 14;
    const panelY = 102;
    const panelW = LCD_WIDTH - 28;
    const rowH = 16;
    const titleH = 22;
    const closeH = 22;
    const pad = 10;
    const visualRows = rows.reduce((n, row) => {
      if (row.kind === "text") return n + wrapChars(row.text).length;
      return n + 1;
    }, 0);
    const panelH = Math.min(
      260,
      pad + titleH + visualRows * rowH + closeH + pad + 4,
    );

    const box = new Graphics();
    paintCbePanel(this.settleHost, box, panelX, panelY, panelW, panelH, CBE.bgPanelDark);
    this.settleHost.addChild(box);
    this.settleHost.eventMode = "static";

    const titleLab = new BitmapLabel();
    titleLab.anchor.set(0.5, 0);
    titleLab.position.set(LCD_WIDTH / 2, panelY + 8);
    void titleLab.setText(title, SETTLE_GOLD, {
      outline: false,
      fontSize: SETTLE_FS,
      maxWidth: panelW - 24,
    });
    this.settleHost.addChild(titleLab);

    const labelX = panelX + 12;
    const valueX = panelX + 118;
    let y = panelY + 8 + titleH;

    const textLabels = preferSystemFont();
    const labels = arenaFdata || textLabels ? null : await loadSettleLabelTextures();
    if (gen !== this.settlePaintGen) return;

    for (const row of rows) {
      if (row.kind === "text") {
        const lab = new BitmapLabel();
        lab.position.set(labelX, y);
        void lab.setText(row.text, row.color, {
          outline: false,
          fontSize: SETTLE_FS,
          maxWidth: panelW - 24,
        });
        this.settleHost.addChild(lab);
        y += rowH * wrapChars(row.text).length;
      } else {
        const fallback: Record<SettleLabelKey, string> = {
          exp: "获得经验",
          gold: "金钱",
          hp: "自动回血",
          mp: "自动回魔",
        };
        if (labels) {
          if (row.label === "mp" && labels.auto && labels.huoMo) {
            const a = uiSprite(labels.auto);
            a.position.set(labelX, y + 2);
            this.settleHost.addChild(a);
            const b = uiSprite(labels.huoMo);
            b.position.set(labelX + PICKUP_CELL * 2, y + 2);
            this.settleHost.addChild(b);
          } else {
            const tex =
              row.label === "exp"
                ? labels.exp
                : row.label === "gold"
                  ? labels.gold
                  : row.label === "hp"
                    ? labels.hp
                    : null;
            if (tex) {
              const spr = uiSprite(tex);
              spr.position.set(labelX, y + 2);
              this.settleHost.addChild(spr);
            } else {
              const lab = new BitmapLabel();
              lab.position.set(labelX, y);
              void lab.setText(fallback[row.label], CBE.inkTip, {
                outline: false,
                fontSize: SETTLE_FS,
              });
              this.settleHost.addChild(lab);
            }
          }
        } else {
          const lab = new BitmapLabel();
          lab.position.set(labelX, y);
          void lab.setText(fallback[row.label], CBE.inkTip, {
            outline: false,
            fontSize: SETTLE_FS,
          });
          this.settleHost.addChild(lab);
        }
        const val = new BitmapLabel();
        val.position.set(valueX, y);
        void val.setText(row.value, settleValueColor(row.value), {
          outline: false,
          fontSize: SETTLE_FS,
          maxWidth: panelW - (valueX - panelX) - 12,
        });
        this.settleHost.addChild(val);
        y += rowH;
      }
    }

    const btn = new Container();
    const btnW = 52;
    const btnH = 18;
    const btnBg = new Graphics()
      .roundRect(0, 0, btnW, btnH, 2)
      .fill({ color: 0x3a2818 })
      .stroke({ width: 1, color: CBE.inkWarn });
    const btnLabel = new BitmapLabel();
    btnLabel.eventMode = "none";
    btnLabel.anchor.set(0.5, 0.5);
    btnLabel.position.set(btnW / 2, btnH / 2);
    btnLabel.show("关闭", CBE.inkWarn, { outline: false, fontSize: SETTLE_FS });
    btn.addChild(btnBg, btnLabel);
    btn.position.set(
      panelX + Math.floor((panelW - btnW) / 2),
      panelY + panelH - pad - btnH,
    );
    btn.eventMode = "static";
    btn.cursor = "pointer";
    btn.on("pointertap", (e) => {
      e.stopPropagation();
      if (gen !== this.settlePaintGen) return;
      this.confirmSettlement();
    });
    this.settleHost.addChild(btn);

    this.armSettleAutoClose(gen);
  }
}

/** pickupimage1.gif: 12px cells — 获得经验|金钱|自动回血|回魔|战斗场次|背包 */
const PICKUP_CELL = 12;

type SettleLabelKey = "exp" | "gold" | "hp" | "mp";

type SettleRow =
  | { kind: "stat"; label: SettleLabelKey; value: string }
  | { kind: "text"; text: string; color: number };

type SettleLabelTextures = {
  exp: Texture | null;
  gold: Texture | null;
  hp: Texture | null;
  auto: Texture | null;
  huoMo: Texture | null;
};

type HangupLabelTextures = {
  exp: Texture | null;
  gold: Texture | null;
  hp: Texture | null;
  mp: Texture | null;
  battles: Texture | null;
  bag: Texture | null;
  titleIcon: Texture | null;
};

let settleLabelCache: SettleLabelTextures | null = null;
let hangupLabelCache: HangupLabelTextures | null = null;

function uiLabelCacheReady(
  tex: Record<string, Texture | null | undefined>,
): boolean {
  return Object.values(tex).some((t) => t != null);
}

/** Warm JHOnlineData hangup panel chrome. */
function preloadHangupLabelTextures(): void {
  preloadHangupPanelTextures();
}

async function loadSettleLabelTextures(): Promise<SettleLabelTextures> {
  if (settleLabelCache && uiLabelCacheReady(settleLabelCache)) {
    return settleLabelCache;
  }
  const sheet = "pickupimage1.gif";
  const loaded: SettleLabelTextures = {
    exp: await loadUiCrop(sheet, 0, 0, PICKUP_CELL * 4, 11),
    gold: await loadUiCrop(sheet, PICKUP_CELL * 4, 0, PICKUP_CELL * 2, 11),
    hp: await loadUiCrop(sheet, PICKUP_CELL * 6, 0, PICKUP_CELL * 4, 11),
    auto: await loadUiCrop(sheet, PICKUP_CELL * 6, 0, PICKUP_CELL * 2, 11),
    huoMo: await loadUiCrop(sheet, PICKUP_CELL * 10, 0, PICKUP_CELL * 2, 11),
  };
  if (uiLabelCacheReady(loaded)) settleLabelCache = loaded;
  return loaded;
}

/** Legacy loader — title icon now concernp.gif via hangupSettleUi. */
async function loadHangupLabelTextures(): Promise<HangupLabelTextures> {
  if (hangupLabelCache && uiLabelCacheReady(hangupLabelCache)) {
    return hangupLabelCache;
  }
  const sheet = "pickupimage1.gif";
  const loaded: HangupLabelTextures = {
    exp: await loadUiCrop(sheet, 0, 0, PICKUP_CELL * 4, 11),
    gold: await loadUiCrop(sheet, PICKUP_CELL * 4, 0, PICKUP_CELL * 2, 11),
    hp: await loadUiCrop(sheet, PICKUP_CELL * 6, 0, PICKUP_CELL * 4, 11),
    mp: await loadUiCrop(sheet, PICKUP_CELL * 10, 0, PICKUP_CELL * 2, 11),
    battles: await loadUiCrop(sheet, PICKUP_CELL * 12, 0, PICKUP_CELL * 4, 11),
    bag: await loadUiCrop(sheet, PICKUP_CELL * 16, 0, PICKUP_CELL * 2, 11),
    titleIcon: await tryLoadUiTexture("concernp.gif"),
  };
  if (uiLabelCacheReady(loaded)) hangupLabelCache = loaded;
  return loaded;
}

function formatSettleDelta(n: number): string {
  const v = n | 0;
  if (v > 0) return `+${v}`;
  return `${v}`;
}

function settleValueColor(text: string): number {
  if (text.startsWith("-")) return CBE.inkWarn;
  if (text.startsWith("+") && text !== "+0") return SETTLE_GOLD;
  return CBE.inkBody;
}

/** Bottom HUD tip on 4/7 — drop「· 结算」suffix and EXP/金 debug tail. */
function formatBattleBottomLog(raw: string): string {
  const i = raw.indexOf("结算");
  if (i < 0) return raw;
  return raw
    .slice(0, i)
    .replace(/\s*[·．.,，]\s*$/, "")
    .trimEnd();
}

/** fdata may be `N/M 战斗胜利` (flask reserve prefix) or drop/extra text. */
function parseSettleFdata(fdata: string): {
  title: string;
  bloodFlask: string;
  blueFlask: string;
  extra: string;
} {
  const raw = (fdata || "").trim();
  if (!raw) return { title: "", bloodFlask: "", blueFlask: "", extra: "" };
  const m = raw.match(/^(\d+)\/(\d+)\s*(.*)$/);
  if (m) {
    const bloodFlask = `血壶:${m[1]}`;
    const blueFlask = `蓝壶:${m[2]}`;
    const rest = (m[3] || "").trim();
    if (
      !rest ||
      rest === "战斗胜利" ||
      rest === "切磋胜利" ||
      rest === "切磋失败"
    ) {
      return { title: rest, bloodFlask, blueFlask, extra: "" };
    }
    if (
      rest.includes("比武") ||
      rest.includes("挑战") ||
      rest.includes("胜利") ||
      rest.includes("失败")
    ) {
      return { title: rest, bloodFlask, blueFlask, extra: "" };
    }
    return { title: "战斗胜利", bloodFlask, blueFlask, extra: rest };
  }
  if (
    raw === "战斗胜利" ||
    raw.includes("比武") ||
    raw.includes("切磋") ||
    raw.includes("挑战")
  ) {
    return { title: raw, bloodFlask: "", blueFlask: "", extra: "" };
  }
  return { title: "战斗胜利", bloodFlask: "", blueFlask: "", extra: raw };
}

/** Pending battle playback waits — flushed when Android FGS pokes while hidden. */
const battleSleepWaiters = new Set<() => void>();

/**
 * Resolve in-flight playActionScript sleeps. Chromium throttles setTimeout
 * while document.hidden; native keepalive must unblock playbackBusy so the
 * next hangup / [自动] 4/2 can submit.
 */
export function flushBattleSleepsForBackground(): void {
  if (typeof document !== "undefined" && document.visibilityState !== "hidden") {
    return;
  }
  for (const finish of [...battleSleepWaiters]) finish();
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => {
    if (typeof document !== "undefined" && document.visibilityState === "hidden") {
      resolve();
      return;
    }
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      clearTimeout(tid);
      battleSleepWaiters.delete(finish);
      if (typeof document !== "undefined") {
        document.removeEventListener("visibilitychange", onVis);
      }
      resolve();
    };
    const onVis = () => {
      if (document.visibilityState === "hidden") finish();
    };
    const tid = setTimeout(finish, ms);
    battleSleepWaiters.add(finish);
    if (typeof document !== "undefined") {
      document.addEventListener("visibilitychange", onVis);
    }
  });
}
