import { Container, Graphics, Sprite, Texture } from "pixi.js";
import {
  ACTOR_DISPLAY_SCALE,
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  drawCbePanel,
} from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import type { BattleState, BattleSide } from "../net/protocol";
import {
  battleDisplayToWire,
  battleTargetWire,
  battleUnitForWire,
} from "../net/protocol";
import type { BattleActionUpdate, BattleSettlement } from "../net/protocolExtras";
import {
  actorResourceForJobSex,
  composeBattlePose,
  composeMapActor,
  composePartStacked,
  loadActor,
  type BattlePose,
  type LoadedActor,
} from "../resources/actor";
import { resolveBattleSce } from "../resources/battleScene";
import { eidolonActorForIndex } from "../resources/eidolon";
import { loadSceneMap, resourceExists, type LoadedScene } from "../resources/loader";
import { rgbaToTexture } from "../render/rgbaTexture";
import type { BattleSkillRow, SkillTargetKind } from "../resources/skill";
import { skillNeedsTargetPick } from "../resources/skill";
import type { BagItem } from "../net/protocolExtras";
import { isBattleMedicineOrFlask } from "../net/protocolExtras";
import { tryLoadUiTexture, uiSprite, DigitStrip } from "./UiAssets";
import { BattlePartyHud, BATTLE_PARTY_HUD_H } from "./BattlePartyHud";

/** Eidolon frames can be 170px+; clamp so FX cannot dominate the 240× stage. */
const FX_MAX_W = 96;
const FX_MAX_H = 96;
/** Player / combat-pose frame step (ms). */
const UNIT_FRAME_MS = 90;
/** Battle idle/death flap — same slow cadence for player + all monsters (鼠尾节奏). */
const BATTLE_IDLE_FRAME_MS = 360;

const MONSTER_RED = 0xff8080;
const SETTLE_GOLD = 0xffd080;
const HEAL_GREEN = 0x50e080;
const TOP_H = 36;
/** Compact party vitals strip — flush to LCD bottom (HTML softkeys are outside). */
const BOTTOM_H = BATTLE_PARTY_HUD_H;
const STAGE_TOP = TOP_H;
const STAGE_BOTTOM = LCD_HEIGHT - BOTTOM_H;
const UNIT_LEFT_X = 22;
const UNIT_RIGHT_X = 218;
const UNIT_FEET_Y = STAGE_TOP + Math.floor((STAGE_BOTTOM - STAGE_TOP) * 0.64);
/** Horizontal push of the middle slot toward center (screenshot chevron). */
const FORMATION_FORWARD = 12;
/** Vertical gap between formation rows. */
const FORMATION_ROW = 30;
const TURN_SECONDS = 16;

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
/** Visible rows in the 仙术 / 物品 parchment (↑↓ wraps across pages). */
const SKILL_PAGE_SIZE = 6;
const ITEM_PAGE_SIZE = 6;
type MenuItem = (typeof MENU_ITEMS)[number];

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
  private unitsHost = new Container();
  private fxHost = new Container();
  private menuHost = new Container();
  private settleHost = new Container();
  private floatHost = new Container();
  private topHost = new Container();
  private bottomHost = new Container();
  /** Top-right 「自动/挂机战斗中」+ 关闭. */
  private autoBannerHost = new Container();
  private autoBannerBuilt = false;
  private autoBannerTip: BitmapLabel | null = null;
  private autoActive = false;
  /** Hangup (2/10) — hide operate scroll; server synth owns the seat. */
  private hangupActive = false;
  private partyHud = new BattlePartyHud();
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
  /**
   * Shared round: local 4/2 already sent; waiting for 4/6 playback
   * (team merge or spar duel_round_defer). No menu / ops until applyUpdate.
   */
  private awaitingRound = false;
  private tickAcc = 0;
  private idleTickAcc = 0;
  private timerSec = TURN_SECONDS;
  private timerAcc = 0;
  private battleMapFile = "";
  private playbackGen = 0;
  /** Drop stale async map loads when show() races. */
  private mapLoadGen = 0;
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
  /** Fired when banner 关闭 while hangup owns the fight — request stop (2/10). */
  onHangupCancel: (() => void) | null = null;
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
    const stageH = STAGE_BOTTOM - STAGE_TOP;
    const stageMask = new Graphics()
      .rect(0, STAGE_TOP, LCD_WIDTH, stageH)
      .fill({ color: 0xffffff });
    this.mapHost.mask = stageMask;
    this.unitsHost.mask = stageMask;
    this.fxHost.mask = stageMask;
    this.addChild(stageMask);
    this.addChild(this.mapHost);
    this.addChild(this.unitsHost);
    this.addChild(this.targetMark);
    this.addChild(this.fxHost);
    this.addChild(this.menuHost);
    this.addChild(this.floatHost);
    this.addChild(this.settleHost);
    this.addChild(this.topHost);
    this.addChild(this.bottomHost);
    // Above stage/menu so the 关闭 hit target stays tappable.
    this.addChild(this.autoBannerHost);
    this.buildChrome();
  }

  isAutoBattleActive(): boolean {
    return this.autoActive;
  }

  isHangupActive(): boolean {
    return this.hangupActive;
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
    this.hangupActive = on;
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
    this.redrawMenu();
  }

  /**
   * Sync banner + menu with session.autoBattleActive.
   * When on, hides the action scroll; when off and the seat can act, reopens it.
   */
  setAutoBattleActive(on: boolean): void {
    this.ensureAutoBanner();
    this.autoActive = on;
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
   * Banner 关闭 while hangup — session.stopHangup (clear intent + 2/10).
   * Does not locally clear hangupActive; server chat is authority.
   */
  closeHangup(): void {
    if (!this.hangupActive) return;
    void this.log.setText("请求停止挂机…", CBE.inkTip);
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
      this.redrawMenu();
    }
  }

  /**
   * Spar 4/10 left uses sexGroup 1..2; PvE subtype-10 fallback is visual0=0.
   * Flee is not offered in friendly spar.
   */
  isSparBattle(): boolean {
    if (!this.state || this.state.subtype !== 10) return false;
    const g = this.state.left[0]?.visual0 ?? 0;
    return g >= 1 && g <= 2;
  }

  /** Right softkey label while battle menu is interactive. */
  softkeyRightLabel(): string {
    if (this.awaitingDeathPrompt) {
      return this.deathPromptHasStone ? "普通复活" : "复活";
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
      return this.deathPromptHasStone ? "复活石" : "复活";
    }
    if (this.awaitingExit) return "继续";
    if (this.hangupActive || this.autoActive) return "";
    return "确定";
  }

  private rootMenuItems(): readonly MenuItem[] {
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
    void this.log.setText(tip, CBE.inkBody, { maxWidth: LCD_WIDTH - 16 });
  }

  show(state: BattleState): void {
    this.visible = true;
    this.state = state;
    this.awaitingExit = false;
    this.awaitingDeathPrompt = false;
    this.deathPromptBusy = false;
    this.deathPromptHasStone = false;
    this.deathPromptCursor = 0;
    this.playbackBusy = false;
    this.awaitingRound = false;
    this.playbackGen += 1;
    this.menuMode = "root";
    this.menuCursor = 0;
    this.menuOpen = this.operateMenuAllowed();
    this.resetTargetPick();
    this.timerSec = TURN_SECONDS;
    this.timerAcc = 0;
    this.clearFloats();
    this.clearPoseTexCache();
    this.fxHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    void this.loadBattleMapBg(this.visuals.worldScene || "");
    this.rebuildUnits();
    this.redrawMenu();
    this.refreshBottomHud();
    this.paintTimer();
    this.refreshAutoBannerVisibility();
    const pname = this.visuals.playerName || state.right[0]?.name || "侠客";
    void this.topTitle.setText(`${pname}行动`, CBE.ink, {
      uc3: true,
      smooth: false,
      outline: false,
    });
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
    void this.log.setText(up.log.slice(0, 28), CBE.inkBody, {
      maxWidth: LCD_WIDTH - 16,
    });

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

    if (up.settlement) {
      for (const row of this.state.left) row.hp = 0;
    }

    const gen = ++this.playbackGen;
    this.playbackBusy = true;
    void this.playActionScript(up).finally(() => {
      if (gen !== this.playbackGen || !this.state) return;
      this.playbackBusy = false;
      // Ensure final teaminfo MP landed (HP stays on actioninfo deltas only).
      this.applyRightTeaminfo(up.rightHp, /* mpOnly */ true);
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
        // Final deferred vitals already notified above; leave after playback.
        this.onExitReady?.();
        return;
      }
      // Death arms revival confirm on server — no 4/7/4/8 until 1/7/14.
      if (this.isLocalPlayerDead()) {
        this.beginDeathPrompt();
        return;
      }
      this.menuMode = "root";
      // Keep scroll hidden while [自动] / hangup; banner or stop-hangup restores.
      this.menuOpen = this.operateMenuAllowed();
      this.timerSec = TURN_SECONDS;
      this.timerAcc = 0;
      this.paintTimer();
      this.redrawMenu();
      this.refreshAutoBannerVisibility();
      // Next seat is ready — main arms [自动] client 4/2 if enabled.
      this.onAutoReady?.();
    });
  }

  hide(): void {
    // Flush last known seat vitals before tearing state (world HUD sync).
    if (this.state) this.notifyVitalsChanged();
    this.visible = false;
    this.state = null;
    this.hangupActive = false;
    this.awaitingExit = false;
    this.awaitingDeathPrompt = false;
    this.playbackBusy = false;
    this.awaitingRound = false;
    this.playbackGen += 1;
    this.resetTargetPick();
    this.clearFloats();
    this.unitsHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.units = [];
    this.fxHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.clearPoseTexCache();
    this.refreshAutoBannerVisibility();
  }

  private clearPoseTexCache(): void {
    for (const tex of this.poseTexCache.values()) tex.destroy(true);
    this.poseTexCache.clear();
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
  submitAutoOperate(operate: number, tip: string): boolean {
    if (!this.state || !this.canSubmitOperate()) return false;
    const op = operate | 0;
    if (op === 0) {
      const wires = this.livingEnemyWires();
      this.selectedTargetWire =
        wires[0] ?? battleTargetWire(this.state);
      this.noteOperateSubmitted(tip);
      this.onAttack?.();
      return true;
    }
    const sk = this.skills.find((s) => s.id + 2 === op);
    const kind = (sk?.targetKind ?? 3) as SkillTargetKind;
    const name = sk?.name ?? "仙术";
    // Same branches as fireSkill / confirm（自动不弹选目标 UI，取首个合法 wire）.
    if (skillNeedsTargetPick(kind)) {
      const wires =
        kind === 1 ? this.livingAllyWires() : this.livingEnemyWires();
      this.selectedTargetWire =
        wires[0] ?? (kind === 1 ? this.selfWire() : null);
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
    } else if (kind === 4) {
      this.selectedTargetWire =
        this.livingEnemyWires()[0] ?? battleTargetWire(this.state);
    } else {
      this.selectedTargetWire = this.livingEnemyWires()[0] ?? null;
    }
    this.noteOperateSubmitted(tip || `施展${name}…`);
    this.onSkill?.(op);
    return true;
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
      this.onSettleConfirm?.();
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
      this.onSettleConfirm?.();
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
    this.noteOperateSubmitted("请求攻击…");
    this.onAttack?.();
  }

  /**
   * Turn countdown expiry: always send normal attack (operate=0).
   * Never open the multi-enemy target UI — that would stall with timer=0.
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
      this.onSettleConfirm?.();
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
    const pages = Math.max(1, Math.ceil(this.skills.length / SKILL_PAGE_SIZE));
    void this.log.setText(
      pages > 1 ? `选择仙术（↑↓翻页 共${pages}页）` : "选择仙术…",
      CBE.inkBody,
    );
  }

  item(): void {
    if (this.awaitingDeathPrompt) {
      this.commitDeathPrompt(this.deathPromptPrimaryResult());
      return;
    }
    if (this.awaitingExit) {
      this.onSettleConfirm?.();
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
    const pages = Math.max(1, Math.ceil(this.items.length / ITEM_PAGE_SIZE));
    void this.log.setText(
      pages > 1 ? `选择物品（↑↓翻页 共${pages}页）` : "选择物品…",
      CBE.inkBody,
    );
  }

  flee(): void {
    if (this.awaitingDeathPrompt) {
      // Right softkey / 返回：普通复活 (result=2), matching CBE cancel path.
      this.commitDeathPrompt(2);
      return;
    }
    if (this.awaitingExit) {
      this.onSettleConfirm?.();
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
    this.clearAutoForManualOp();
    void this.log.setText("请求逃跑…", CBE.inkBody);
    this.menuOpen = false;
    this.redrawMenu();
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
    return this.candidateWires().filter((w) => {
      const u = battleUnitForWire(this.state!, w);
      return !!u && this.state!.left.includes(u) && u.hp > 0;
    });
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
    this.selectedTargetWire = wire;
    const operate = this.pendingOperate;
    const name = this.pendingSkillName;
    const item = this.pendingItem;
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
      this.noteOperateSubmitted("请求攻击…");
      this.onAttack?.();
    } else {
      this.noteOperateSubmitted(`施展${name}…`);
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
    if (kind === 0) {
      this.selectedTargetWire = this.selfWire();
    } else if (kind === 2) {
      this.selectedTargetWire =
        this.livingAllyWires()[0] ?? this.selfWire();
    } else if (kind === 4) {
      this.selectedTargetWire =
        this.livingEnemyWires()[0] ?? battleTargetWire(this.state);
    } else {
      this.selectedTargetWire = this.livingEnemyWires()[0] ?? null;
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

  tick(dtMs: number): void {
    if (!this.visible || !this.state) return;

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
    const now = performance.now();
    if (this.tickAcc >= UNIT_FRAME_MS) {
      this.tickAcc = 0;
      for (const u of this.units) {
        if (u.hidden || !u.loaded || !u.sprite) continue;
        const prevPose = u.pose;
        if (u.pose !== "idle" && u.pose !== "death" && now >= u.poseUntil) {
          u.pose = "idle";
        }
        // Idle/death use BATTLE_IDLE_FRAME_MS; attack/cast/hit stay at 90ms.
        if (u.pose === "idle" || u.pose === "death") {
          if (prevPose !== u.pose) this.paintUnitSprite(u);
          continue;
        }
        u.phase += 1;
        this.paintUnitSprite(u);
      }
    }
    if (this.idleTickAcc >= BATTLE_IDLE_FRAME_MS) {
      this.idleTickAcc = 0;
      for (const u of this.units) {
        if (u.hidden || !u.loaded || !u.sprite) continue;
        if (u.pose !== "idle" && u.pose !== "death") continue;
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
    this.bottomHost.addChild(this.partyHud);
    void this.partyHud.mount();
    this.log.position.set(8, STAGE_BOTTOM - 12);
    this.addChild(this.log);
    void this.mountFloatDigits();
    this.ensureAutoBanner();
  }

  private ensureAutoBanner(): void {
    if (this.autoBannerBuilt) return;
    this.autoBannerBuilt = true;
    const host = this.autoBannerHost;
    host.visible = false;
    host.eventMode = "static";

    // UC3「自动/挂机战斗中」≈60px; keep clear gap before 关闭.
    const panelW = 134;
    const panelH = 22;
    const bg = new Graphics()
      .roundRect(0, 0, panelW, panelH, 3)
      .fill({ color: 0x142820, alpha: 0.94 })
      .stroke({ width: 1, color: CBE.inkTip });
    host.addChild(bg);

    const tip = new BitmapLabel();
    tip.eventMode = "none";
    tip.position.set(4, 5);
    tip.show("自动战斗中", CBE.inkTip, { outline: false });
    this.autoBannerTip = tip;
    host.addChild(tip);

    const btn = new Container();
    const btnBg = new Graphics()
      .roundRect(0, 0, 34, 16, 2)
      .fill({ color: 0x3a2818 })
      .stroke({ width: 1, color: CBE.inkWarn });
    const btnLabel = new BitmapLabel();
    btnLabel.eventMode = "none";
    btnLabel.position.set(3, 2);
    btnLabel.show("关闭", CBE.inkWarn, { outline: false });
    btn.addChild(btnBg, btnLabel);
    btn.position.set(panelW - 38, 3);
    btn.eventMode = "static";
    btn.cursor = "pointer";
    btn.on("pointertap", (e) => {
      e.stopPropagation();
      // Hangup owns the seat; closing it is the stop-hangup path (not [自动]).
      if (this.hangupActive) {
        this.closeHangup();
        return;
      }
      this.closeAutoBattle();
    });
    host.addChild(btn);

    host.position.set(LCD_WIDTH - panelW - 4, STAGE_TOP + 2);
  }

  private paintAutoBannerTip(): void {
    const tip = this.autoBannerTip;
    if (!tip) return;
    tip.show(this.hangupActive ? "挂机战斗中" : "自动战斗中", CBE.inkTip, {
      outline: false,
    });
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
      this.timerDigits.setText(String(TURN_SECONDS), 24, 12);
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
    const gen = ++this.mapLoadGen;
    this.mapHost.removeChildren().forEach((c) => {
      if (c !== this.mapSprite) c.destroy({ children: true });
    });
    // Fallback tint while loading / missing.
    const fallback = new Graphics()
      .rect(0, 0, LCD_WIDTH, LCD_HEIGHT)
      .fill({ color: 0x2a5a38 });
    this.mapHost.addChildAt(fallback, 0);

    const sce =
      (await resolveBattleSce(worldScene, resourceExists)) ||
      (await resolveBattleSce("17无情谷_01.sce", resourceExists));
    if (gen !== this.mapLoadGen) return;
    if (!sce) {
      console.warn("[battle] no b_*.sce for", worldScene);
      return;
    }
    this.battleMapFile = sce;
    try {
      const scene = await loadSceneMap(sce);
      if (gen !== this.mapLoadGen) return;
      this.paintMap(scene);
      void this.topTitle.setText(
        `${this.visuals.playerName || "侠客"}行动`,
        CBE.ink,
        { uc3: true, smooth: false, outline: false },
      );
      console.info("[battle] map", sce, `${scene.width}x${scene.height}`);
    } catch (e) {
      console.warn("[battle] load map failed", sce, e);
    }
  }

  private paintMap(scene: LoadedScene): void {
    if (this.mapSprite) {
      this.mapHost.removeChild(this.mapSprite);
      this.mapSprite.destroy({ texture: true });
      this.mapSprite = null;
    }
    const tex = rgbaToTexture(scene.width, scene.height, scene.rgba);
    const spr = new Sprite(tex);
    // Fit battle map into stage band (top..bottom), cover LCD width.
    const stageH = STAGE_BOTTOM - STAGE_TOP;
    const scale = Math.max(LCD_WIDTH / scene.width, stageH / scene.height);
    spr.scale.set(scale);
    spr.x = (LCD_WIDTH - scene.width * scale) / 2;
    spr.y = STAGE_TOP + (stageH - scene.height * scale) / 2;
    this.mapSprite = spr;
    // Keep fallback under map for letterbox.
    this.mapHost.addChild(spr);
  }

  private rebuildUnits(): void {
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
      const spr = new Sprite(rgbaToTexture(frame.width, frame.height, frame.rgba));
      spr.anchor.set(0.5, 1);
      spr.scale.set(
        frame.flipX ? -ACTOR_DISPLAY_SCALE : ACTOR_DISPLAY_SCALE,
        ACTOR_DISPLAY_SCALE,
      );
      root.addChildAt(spr, 1);
      view.sprite = spr;
    } catch (e) {
      console.warn("[battle] actor", actorName, e);
      root.addChildAt(
        new Graphics().circle(0, -18, 12).fill({ color: facingRight ? 0x884444 : 0x448866 }),
        1,
      );
    }
  }

  private paintUnitSprite(u: UnitView): void {
    if (!u.loaded || !u.sprite || u.hidden) return;
    const frame = composeBattlePose(u.loaded, u.facingRight, u.pose, u.phase);
    if (!frame) return;
    const key = `${u.loaded.name}|${u.pose}|${u.facingRight ? 1 : 0}|${u.phase & 31}|${frame.width}x${frame.height}`;
    let tex = this.poseTexCache.get(key);
    if (!tex) {
      tex = rgbaToTexture(frame.width, frame.height, frame.rgba);
      this.poseTexCache.set(key, tex);
      this.evictUnusedPoseTextures();
    }
    if (this.poseTexKey.get(u.sprite) !== key) {
      u.sprite.texture = tex;
      this.poseTexKey.set(u.sprite, key);
    }
    u.sprite.anchor.set(0.5, 1);
    u.sprite.scale.set(
      frame.flipX ? -ACTOR_DISPLAY_SCALE : ACTOR_DISPLAY_SCALE,
      ACTOR_DISPLAY_SCALE,
    );
    if (u.pose === "hit") {
      u.sprite.tint = 0xff8080;
    } else if (u.pose === "death") {
      u.sprite.tint = 0xffffff;
      // Enemy death fade owns alpha; do not snap it back to 0.95 every tick.
      if (!this.isEnemyUnit(u)) u.sprite.alpha = 0.95;
    } else {
      u.sprite.tint = 0xffffff;
      u.sprite.alpha = 1;
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

  private isLocalPlayerDead(): boolean {
    const side = this.localPlayerSide();
    if (!side) return false;
    if (side.hp <= 0) return true;
    const view = this.units.find((u) => u.side === side);
    return !!view?.dead;
  }

  private deathPromptOptions(): { label: string; result: 1 | 2 }[] {
    if (this.deathPromptHasStone) {
      return [
        { label: "使用复活石", result: 1 },
        { label: "普通复活", result: 2 },
      ];
    }
    return [{ label: "普通复活", result: 2 }];
  }

  private deathPromptPrimaryResult(): 1 | 2 {
    const opts = this.deathPromptOptions();
    const pick = opts[this.deathPromptCursor] ?? opts[0];
    return pick?.result ?? 2;
  }

  /** Bag may include 801 复活石 (not in battle medicine list). */
  setDeathPromptBag(hasRevivalStone: boolean): void {
    this.deathPromptHasStone = hasRevivalStone;
    if (this.awaitingDeathPrompt) this.paintDeathPrompt();
  }

  private beginDeathPrompt(): void {
    this.awaitingDeathPrompt = true;
    this.deathPromptBusy = false;
    this.menuOpen = false;
    this.autoActive = false;
    this.deathPromptCursor = 0;
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
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.menuHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    const opts = this.deathPromptOptions();
    const h = 80 + opts.length * 18;
    const box = new Graphics();
    drawCbePanel(box, 20, 100, LCD_WIDTH - 40, h, CBE.bgPanelDark);
    this.settleHost.addChild(box);
    const lines = [
      "你已经死亡",
      this.deathPromptHasStone
        ? "可使用复活石原地满血"
        : "确认后传送石附近复活",
      ...opts.map((o, i) => `${i === this.deathPromptCursor ? "▶ " : "  "}${o.label}`),
      "确定选择 · 右键普通复活",
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

  private async playAttackMotion(
    actor: UnitView,
    target: UnitView | null,
    pose: "attack" | "cast",
  ): Promise<void> {
    // Cast/heal: keep formation facing. Facing the heal target (often a left-side
    // ally with smaller homeX) flipped monsters left — looked like 回血后转身.
    // Melee still faces the foe for the dash/swing.
    if (pose === "cast") {
      actor.facingRight = actor.homeX < (UNIT_LEFT_X + UNIT_RIGHT_X) / 2;
      this.paintUnitSprite(actor);
      return;
    }
    if (target) {
      actor.facingRight = target.homeX > actor.homeX;
    } else {
      actor.facingRight = actor.homeX < (UNIT_LEFT_X + UNIT_RIGHT_X) / 2;
    }
    this.paintUnitSprite(actor);

    // Match BattleScreen.tick cadence (UNIT_FRAME_MS/part); one strip only — no double loop.
    const frameMs = UNIT_FRAME_MS;
    const cycleMs = this.attackStripParts(actor) * frameMs;
    const hold = cycleMs + 120;
    this.setUnitPose(actor, pose, hold);

    if (target) {
      // Melee: flash beside target on the side that keeps facing them, swing, return.
      const beside = actor.facingRight ? target.homeX - 30 : target.homeX + 30;
      await this.tweenUnitTo(actor, beside, target.homeY, 260);
      actor.facingRight = target.homeX > actor.homeX;
      this.paintUnitSprite(actor);
      await sleep(Math.max(400, cycleMs - 260));
      await this.tweenUnitTo(actor, actor.homeX, actor.homeY, 300);
      actor.feetX = actor.homeX;
      actor.feetY = actor.homeY;
      actor.facingRight = actor.homeX < (UNIT_LEFT_X + UNIT_RIGHT_X) / 2;
    } else {
      const step = actor.facingRight ? 18 : -18;
      await this.tweenUnitTo(actor, actor.homeX + step, actor.homeY, 220);
      await sleep(Math.max(400, cycleMs - 220));
      await this.tweenUnitTo(actor, actor.homeX, actor.homeY, 280);
      actor.feetX = actor.homeX;
      actor.feetY = actor.homeY;
    }

    if (actor.pose !== "death") {
      actor.pose = "idle";
      actor.poseUntil = 0;
      this.paintUnitSprite(actor);
    }
  }

  private async playHitMotion(u: UnitView): Promise<void> {
    if (u.dead || u.hidden) return;
    this.setUnitPose(u, "hit", 700);
    const x0 = u.root.x;
    u.root.x = x0 + (u.facingRight ? -6 : 6);
    await sleep(120);
    u.root.x = x0;
    await sleep(400);
  }

  /** Left-side units are monsters (or spar foes); facing can flip mid-cast. */
  private isEnemyUnit(u: UnitView): boolean {
    return !!this.state?.left.includes(u.side);
  }

  private async playDeathMotion(u: UnitView): Promise<void> {
    u.dead = true;
    if (this.isEnemyUnit(u)) {
      // Monster: CBE `f_death.actor` (siwang1.gif splash) then leave the stage.
      // Do NOT key off facingRight — cast/heal toward an ally sets facing left
      // and previously kept the first kill frozen on stage.
      const splash = this.playNamedActorFx(
        "f_death.actor",
        u.feetX,
        u.feetY - 8,
        5,
        550,
        1,
      );
      this.setUnitPose(u, "death", 2000);
      const t0 = performance.now();
      while (performance.now() - t0 < 700) {
        if (u.sprite && !u.hidden) {
          // tick/paintUnitSprite also writes death alpha — keep fading down.
          u.sprite.alpha = Math.min(
            u.sprite.alpha,
            Math.max(0, 1 - (performance.now() - t0) / 700),
          );
        }
        await sleep(30);
      }
      u.hidden = true;
      u.root.visible = false;
      if (u.sprite) u.sprite.alpha = 0;
      await splash;
      return;
    }
    // Player: hold death.gif pose on stage.
    this.setUnitPose(u, "death", 1e9);
    for (let i = 0; i < 8; i++) {
      u.phase = i;
      this.paintUnitSprite(u);
      await sleep(120);
    }
  }

  private refreshUnitBars(): void {
    /* overhead HP removed — vitals live in BattlePartyHud */
  }

  private refreshBottomHud(): void {
    if (!this.state) return;
    this.partyHud.setParty(this.state.right, {
      playerName: this.visuals.playerName,
    });
    this.partyHud.refresh(this.state.right);
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
      this.item();
      return;
    }
    if (item === "逃跑") {
      this.flee();
      return;
    }
    if (item === "自动") {
      const on = this.onAutoToggle?.() ?? false;
      this.setAutoBattleActive(on);
      void this.log.setText(on ? "自动战斗开启" : "自动战斗关闭", CBE.inkTip);
      return;
    }
    void this.log.setText("查看…", CBE.inkTip);
  }

  private redrawMenu(): void {
    this.menuHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    if (this.awaitingExit || this.awaitingDeathPrompt || !this.menuOpen) return;
    // Target pick: hide action list; cursor mark + log carry the UX.
    if (this.menuMode === "target") return;

    const rootItems = this.rootMenuItems();
    const skillPages = Math.max(
      1,
      Math.ceil(Math.max(this.skills.length, 1) / SKILL_PAGE_SIZE),
    );
    const skillPage = Math.min(
      skillPages - 1,
      Math.floor(this.skillCursor / SKILL_PAGE_SIZE),
    );
    const skillPageStart = skillPage * SKILL_PAGE_SIZE;
    const itemPages = Math.max(
      1,
      Math.ceil(Math.max(this.items.length, 1) / ITEM_PAGE_SIZE),
    );
    const itemPage = Math.min(
      itemPages - 1,
      Math.floor(this.itemCursor / ITEM_PAGE_SIZE),
    );
    const itemPageStart = itemPage * ITEM_PAGE_SIZE;
    const listPageRows =
      this.menuMode === "skill"
        ? Math.min(
            SKILL_PAGE_SIZE,
            Math.max(1, this.skills.length - skillPageStart),
          )
        : this.menuMode === "item"
          ? Math.min(
              ITEM_PAGE_SIZE,
              Math.max(1, this.items.length - itemPageStart),
            )
          : 0;
    const rows =
      this.menuMode === "skill" || this.menuMode === "item"
        ? listPageRows
        : rootItems.length;
    const showPageHint =
      (this.menuMode === "skill" && skillPages > 1) ||
      (this.menuMode === "item" && itemPages > 1);
    const h = 28 + rows * 16 + (showPageHint ? 14 : 0);
    // 仙术行带「耗蓝N」，比根菜单略宽；物品名更长用 120
    const w =
      this.menuMode === "skill" ? 128 : this.menuMode === "item" ? 120 : 100;
    const x = (LCD_WIDTH - w) / 2;
    const y = STAGE_TOP + 40;

    // Parchment scroll (screenshot) — Graphics approximation + optional UI_menu2.
    const panel = new Graphics();
    panel
      .roundRect(x, y, w, h, 2)
      .fill({ color: 0xc8c4a0 })
      .stroke({ width: 2, color: 0x3a6658 });
    panel.roundRect(x - 4, y - 6, w + 8, 10, 3).fill({ color: 0x4a7a6a });
    panel.roundRect(x - 4, y + h - 4, w + 8, 10, 3).fill({ color: 0x4a7a6a });
    this.menuHost.addChild(panel);

    const title = new BitmapLabel();
    title.position.set(x + 18, y + 4);
    let titleText = "行动";
    if (this.menuMode === "skill" && skillPages > 1) {
      titleText = `仙术${skillPage + 1}/${skillPages}`;
    } else if (this.menuMode === "item" && itemPages > 1) {
      titleText = `物品${itemPage + 1}/${itemPages}`;
    } else if (this.menuMode === "item") {
      titleText = "物品";
    } else if (this.menuMode === "skill") {
      titleText = "仙术";
    }
    void title.setText(titleText, SETTLE_GOLD, { outline: false });
    this.menuHost.addChild(title);

    if (this.menuMode === "skill") {
      if (!this.skills.length) {
        const lab = new BitmapLabel();
        lab.position.set(x + 10, y + 22);
        void lab.setText("(无)", 0x606060);
        this.menuHost.addChild(lab);
        return;
      }
      const pageSkills = this.skills.slice(
        skillPageStart,
        skillPageStart + SKILL_PAGE_SIZE,
      );
      pageSkills.forEach((sk, i) => {
        const abs = skillPageStart + i;
        const lab = new BitmapLabel();
        lab.position.set(x + 10, y + 22 + i * 16);
        const hi = abs === this.skillCursor;
        if (hi) {
          this.menuHost.addChild(
            new Graphics()
              .rect(x + 6, y + 20 + i * 16, w - 12, 14)
              .fill({ color: 0x4a4a4a }),
          );
        }
        const line =
          sk.mpCost != null ? `${sk.name} 耗蓝${sk.mpCost}` : sk.name;
        void lab.setText(line, hi ? CBE.ink : 0x202020, {
          outline: false,
          maxWidth: w - 16,
        });
        this.menuHost.addChild(lab);
      });
      if (showPageHint) {
        const foot = new BitmapLabel();
        foot.position.set(x + 22, y + 22 + pageSkills.length * 16);
        void foot.setText("↑↓翻页", 0x404040);
        this.menuHost.addChild(foot);
      }
      return;
    }

    if (this.menuMode === "item") {
      if (!this.items.length) {
        const lab = new BitmapLabel();
        lab.position.set(x + 10, y + 22);
        void lab.setText("(无)", 0x606060);
        this.menuHost.addChild(lab);
        return;
      }
      const pageItems = this.items.slice(
        itemPageStart,
        itemPageStart + ITEM_PAGE_SIZE,
      );
      pageItems.forEach((it, i) => {
        const abs = itemPageStart + i;
        const lab = new BitmapLabel();
        lab.position.set(x + 8, y + 22 + i * 16);
        const hi = abs === this.itemCursor;
        if (hi) {
          this.menuHost.addChild(
            new Graphics()
              .rect(x + 4, y + 20 + i * 16, w - 8, 14)
              .fill({ color: 0x4a4a4a }),
          );
        }
        const label =
          it.count > 1 ? `${it.name}×${it.count}` : it.name;
        void lab.setText(label, hi ? CBE.ink : 0x202020, {
          outline: false,
          maxWidth: w - 12,
        });
        this.menuHost.addChild(lab);
      });
      if (showPageHint) {
        const foot = new BitmapLabel();
        foot.position.set(x + 28, y + 22 + pageItems.length * 16);
        void foot.setText("↑↓翻页", 0x404040);
        this.menuHost.addChild(foot);
      }
      return;
    }

    if (this.menuCursor >= rootItems.length) this.menuCursor = 0;
    rootItems.forEach((label, i) => {
      const lab = new BitmapLabel();
      lab.position.set(x + 22, y + 22 + i * 16);
      const hi = i === this.menuCursor;
      if (hi) {
        this.menuHost.addChild(
          new Graphics()
            .rect(x + 6, y + 20 + i * 16, w - 12, 14)
            .fill({ color: 0x4a4a4a }),
        );
      }
      void lab.setText(label, hi ? CBE.ink : 0x202020, { outline: false });
      this.menuHost.addChild(lab);
    });
  }

  private async playActionScript(up: BattleActionUpdate): Promise<void> {
    if (!this.state) return;
    const actions = up.actions?.length ? up.actions : [];

    if (!actions.length) {
      this.playFloatsFromDeltas(up);
      await sleep(400);
      return;
    }

    for (const rec of actions) {
      if (rec.type === 3 || rec.type === 4) {
        const dead = this.unitForWire(rec.actorWire);
        if (dead) await this.playDeathMotion(dead);
        else await sleep(600);
        continue;
      }

      const actor = this.unitForWire(rec.actorWire);
      const offensiveGroup =
        rec.children.length > 1 && rec.children.some((c) => c.delta < 0);
      const enemyWires = offensiveGroup ? this.livingEnemyWires() : [];
      const childWires = rec.children.map((c, i) => {
        if (!offensiveGroup || enemyWires.length === 0) return c.wire;
        const tgt = this.unitForWire(c.wire);
        // Hangup subtype-10 + stale scene child wire 0 paints on the player
        // for 天机/幻剑/鬼道 group skills alike — retarget to living enemies.
        if (tgt && this.state!.left.includes(tgt.side)) return c.wire;
        return enemyWires[Math.min(i, enemyWires.length - 1)]!;
      });
      const firstTarget =
        childWires[0] != null ? this.unitForWire(childWires[0]) : null;
      const isSkill = rec.type === 1 || rec.type === 2;
      const fxWires = childWires.length ? childWires : [rec.actorWire];
      const effectIndex =
        isSkill && rec.effectIndex != null && rec.effectIndex > 0
          ? rec.effectIndex
          : 0;

      if (actor && !actor.dead) {
        const motion = this.playAttackMotion(
          actor,
          firstTarget,
          isSkill ? "cast" : "attack",
        );
        if (effectIndex > 0) {
          await Promise.all([
            motion,
            (async () => {
              await sleep(120);
              await this.playSkillEffects(
                effectIndex,
                fxWires,
                actor,
                /* fallbackToActor */ !offensiveGroup,
              );
            })(),
          ]);
        } else {
          await motion;
        }
      } else if (effectIndex > 0) {
        await this.playSkillEffects(
          effectIndex,
          fxWires,
          actor,
          !offensiveGroup,
        );
      }

      for (let i = 0; i < rec.children.length; i++) {
        const c = rec.children[i]!;
        const wire = childWires[i] ?? c.wire;
        const tgt = this.unitForWire(wire);
        if (tgt && c.delta < 0 && c.flag !== 3) {
          void this.playHitMotion(tgt);
        }
        this.spawnFloatAtWire(wire, c.delta, c.flag, rec.effectText);
        this.applyWireHpDelta(wire, c.delta);
      }
      if (isSkill) {
        this.applyCasterMpFromTeaminfo(up.rightHp, rec.actorWire);
      }
      this.notifyVitalsChanged();
      await sleep(450);
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
        this.playNamedActorFx(actorName, p.x, p.y, undefined, 750, 0.85),
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
   */
  private async playNamedActorFx(
    actorName: string,
    x: number,
    y: number,
    maxParts?: number,
    durationMs = 1800,
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
      const frameMs = Math.max(40, Math.floor(durationMs / frames));
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
        await sleep(Math.min(800, durationMs));
      }
      host.destroy({ children: true, texture: true });
    } catch (e) {
      console.warn("[battle] fx", actorName, e);
      await sleep(Math.min(800, durationMs));
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
      // Black fill + thin green outline.
      this.spawnTextFloat("闪躲", x, yBody, 0x000000, 0x00ff00, true);
      return;
    }
    if (flag === 2) {
      // Damage higher so 「暴击」 below does not cover it.
      if (delta !== 0) this.spawnDigitFloat(delta, x, yBody - 22);
      this.spawnTextFloat("暴击", x, yBody + 14, 0x000000, CBE.inkTip, true);
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

  private spawnDigitFloat(delta: number, x: number, y: number): void {
    const strip = delta > 0 ? this.healDigits : this.hurtDigits;
    const digits = String(Math.abs(delta | 0));
    if (strip) {
      const row = strip.buildRow(digits);
      row.pivot.set(row.width / 2, 0);
      row.position.set(x, y);
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
    outlineThin = false,
  ): void {
    const lab = new BitmapLabel();
    lab.anchor.set(0.5, 0.5);
    lab.position.set(x, y);
    this.floatHost.addChild(lab);
    void lab.setText(text, color, {
      outline: false,
      outlineColor,
      outlineThin,
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

  private showSettlement(s: BattleSettlement): void {
    this.awaitingDeathPrompt = false;
    this.settleHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.menuHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    const box = new Graphics();
    drawCbePanel(box, 20, 96, LCD_WIDTH - 40, 160, CBE.bgPanelDark);
    this.settleHost.addChild(box);
    const spar = this.isSparBattle();
    const lose =
      spar &&
      (s.fdata.includes("挑战失败") || s.expGain < 0 || s.goldGain < 0);
    const title = spar
      ? lose
        ? "切磋失败"
        : "切磋胜利"
      : "战斗胜利";
    const lines = [
      title,
      `经验 ${s.expGain}`,
      `金钱 ${s.goldGain}`,
      s.level ? `等级 ${s.level}` : "",
      !spar && s.fdata ? s.fdata.slice(0, 18) : "",
      "按确定继续",
    ].filter(Boolean);
    lines.forEach((t, i) => {
      const lab = new BitmapLabel();
      lab.position.set(32, 110 + i * 18);
      void lab.setText(t, i === 0 ? SETTLE_GOLD : CBE.inkBody, {
        outline: false,
        maxWidth: LCD_WIDTH - 64,
      });
      this.settleHost.addChild(lab);
    });
  }
}
function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}
