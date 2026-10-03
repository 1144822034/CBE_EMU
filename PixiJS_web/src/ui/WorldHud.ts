import { Container, Graphics, Rectangle, Sprite, Texture } from "pixi.js";
import {
  CBE,
  CBE_TITLE_H,
  LCD_HEIGHT,
  LCD_WIDTH,
  MAP_STATUS_STRIP_H,
  WORLD_BOTTOM_H,
  WORLD_TOP_H,
  clearCbeFlowerCorners,
  makePixelText,
  paintLocTitleChrome,
  preloadCbeFlowerCorners,
} from "./constants";
import {
  BitmapLabel,
  UC3_CELL,
  measureUiTextWidth,
  wrapUiText,
} from "./BitmapLabel";
import { DigitStrip, tryLoadUiTexture, uiSprite } from "./UiAssets";
import { SLOT_FRAME_SIZE } from "./ItemArt";
import {
  colorForChatEntry,
  findArenaKillStreakTitle,
  chatLineColorRuns,
  type ChatEntry,
} from "./ChatPanel";
import {
  composeActorFrameIndex,
  loadActor,
  uiActorResourceForJobSex,
} from "../resources/actor";
import { fetchResourceBytes } from "../resources/loader";
import { lookupSMapAlias } from "../resources/smap";
import { replaceGraphics } from "../render/gpuResume";
import {
  createPanelTipState,
  clearPanelTip,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";

/** Top shortcut icons → system overlays. */
export type HudIconAction = "status" | "bag" | "quest" | "social" | "mall";

/** Teammate strip under self status (excludes self). */
export type HudPartyMate = {
  id: number;
  name: string;
  jobIndex: number;
  sex: number;
  hp: number;
  hpMax: number;
  mp: number;
  mpMax: number;
  isLeader: boolean;
};

/**
 * Measured from PixiJS_web/reference-shots/000017_00025672-game.png
 * and 000018_00027182-game.png (CBE 240×400, toolbar cropped).
 *
 *   y0–27   location pill over map tint
 *   y28–63  olive status: portrait + tiao + UIsh×5
 *   y64–71  uiexp + exp track
 *   y72+    map (SceneView)
 *   y342–363 soft: UIxia1 / UI7+挂机 / 动态昵称芯片… / UIxia3
 *   y364–399 chat
 */
const ICON_FILES = ["UIsh1.gif", "UIsh2.gif", "UIsh3.gif", "UIsh4.gif", "UIsh6.gif"];
const ICON_ACTIONS: HudIconAction[] = ["status", "bag", "quest", "social", "mall"];

/**
 * Hangup softkey sits left of center so the wide chat/home zone is not
 * mistaken for 挂机 (mis-taps were common with a centered 100px hit box).
 * Account-switch chips spread to the right of 挂机 until the home softkey.
 */
const HANG_BTN_X = 40;
const HANG_BTN_W = 52;
const SWITCH_STRIP_X = HANG_BTN_X + HANG_BTN_W + 2;
/** Leave room for UIxia3 home softkey. */
const SWITCH_STRIP_RIGHT = LCD_WIDTH - 30;
/** One CJK glyph + slim pad (chip label is a single nickname char). */
const SWITCH_BTN_W = 18;
const SWITCH_BTN_GAP = 2;

/** One soft-row chip for same-page multi-account switch. */
export type HudAccountSwitchSlot = {
  id: string;
  /** Usually first character of role nickname. */
  label: string;
  active: boolean;
};

type SwitchChip = {
  id: string;
  root: Container;
  bg: Sprite | Graphics;
  label: BitmapLabel;
};

/** Status / chrome slots (absolute LCD y inside top band). */
const STATUS_BG_Y = 28;
const STATUS_BG_H = 36; // → y63
/**
 * Portrait frame = handkuang.gif (25×25), shared with equip slots.
 * Evidence: reference-shots/000017 gold ring vs handkuang @ (3,37) → 97/100 gold hit;
 * ui_selectchar excluded (blue halo leaks onto olive HUD).
 */
const PORTRAIT_FRAME_X = 3;
const PORTRAIT_FRAME_Y = 37;
/** Blue interior of handkuang (probe: 3..21 inclusive → 19×19). */
const PORTRAIT_PAD = 3;
const PORTRAIT_INNER = SLOT_FRAME_SIZE - PORTRAIT_PAD * 2; // 19
const PORTRAIT_X = PORTRAIT_FRAME_X + PORTRAIT_PAD;
const PORTRAIT_Y = PORTRAIT_FRAME_Y + PORTRAIT_PAD;
const TIAO_X = 34;
const TIAO_Y = 38;
const ICON_X0 = 91;
const ICON_Y = 39;
const ICON_PITCH = 31; // 21px art + 10px gap
const EXP_Y = 63;
const SOFT_H = 22;
/**
 * Bottom chat strip under 挂机 soft row.
 * Size matches 高清/宋体 display cell ({@link UC3_CELL}); band is 36px → 2 rows.
 */
const CHAT_FONT_SIZE = UC3_CELL;
const CHAT_WRAP_PX = LCD_WIDTH - 8;
const CHAT_VISIBLE_ROWS = 2;
const CHAT_LINE_H = CHAT_FONT_SIZE;
/**
 * Party mates under the HUD chrome, left of the map.
 * Must clear SceneView map-corner status strip (修/拍/… flush under exp).
 * Evidence: partyHost previously at WORLD_TOP_H+1 covered those badges (HUD above map).
 */
const MAP_STATUS_STRIP_BOTTOM = MAP_STATUS_STRIP_H;
const PARTY_Y0 = WORLD_TOP_H + MAP_STATUS_STRIP_BOTTOM + 2;
const PARTY_SLOT_H = 30;
const PARTY_FACE = PORTRAIT_INNER;
/** Other mates only (excludes self). Team cap = 1 leader + 2 members. */
const PARTY_MAX = 2;
/** Total roster size including self — mirror VM_MOCK_SERVICE_TEAM_MEMBER_MAX. */
export const TEAM_MEMBER_MAX = 3;

type PartySlot = {
  root: Container;
  portrait: Container;
  /** Leader mark (Graphics crown; vip_effect.gif is「斗」status, not VIP). */
  crown: Graphics;
  hpFill: Graphics;
  spFill: Graphics;
  mateId: number;
};

export class WorldHud extends Container {
  readonly top = new Container();
  readonly bottom = new Container();
  onMenu: (() => void) | null = null;
  onHome: (() => void) | null = null;
  onHangup: (() => void) | null = null;
  /** Soft-row chip after 挂机 — direct switch to that account slot. */
  onAccountSwitch: ((slotId: string) => void) | null = null;
  /** Tap bottom chat strip → open ChatPanel. */
  onChat: (() => void) | null = null;
  onIcon: ((action: HudIconAction) => void) | null = null;
  /** Tap self portrait → character status panel. */
  onPortrait: (() => void) | null = null;
  /** Tap party-strip mate portrait → that player's info. */
  onPartyMate: ((mateId: number) => void) | null = null;
  /** Fired after party strip show/hide (quest tracker Y offset). */
  onPartyChanged: ((mateCount: number) => void) | null = null;

  private locText: BitmapLabel;
  private locBannerGfx = new Graphics();
  private locBannerFlowerHost = new Container();
  private portraitHost = new Container();
  private portraitFrameHost = new Container();
  private tiaoHost = new Container();
  private hpFill = new Graphics();
  private spFill = new Graphics();
  /** Opaque HUD bands (die after GL restore — remount in nudge). */
  private topDeepGfx = new Graphics();
  private topStatusGfx = new Graphics();
  private topBelowGfx = new Graphics();
  private softBgGfx = new Graphics();
  private softFxGfx = new Graphics();
  private chatBgGfx = new Graphics();
  private iconSlots: Container[] = [];
  private expHost = new Container();
  private expFill: Sprite | null = null;
  private expDigits: DigitStrip | null = null;
  private hangLabel: BitmapLabel;
  private switchHost = new Container();
  private switchChips: SwitchChip[] = [];
  private switchSlots: HudAccountSwitchSlot[] = [];
  private switchBgTex: Texture | null = null;
  private chatLines: BitmapLabel[] = [];
  private chatStripHost = new Container();
  private chatLog: string[] = [];
  /** Longer history for ChatPanel (HUD still shows 2 lines). */
  private chatEntries: ChatEntry[] = [];
  private chatHistory: string[] = [];
  private artReady = false;
  private menuHit!: Container;
  private hangHit!: Container;
  private homeHit!: Container;
  private chatHit!: Container;
  private partyHost = new Container();
  private partySlots: PartySlot[] = [];
  private partyTiao: Texture | null = null;
  private handkuangTex: Texture | null = null;
  /** Soft-row GIF sprites (menu / hang / home) — remount after GL restore. */
  private softArtSprites: Sprite[] = [];
  private tiaoChrome: Sprite | null = null;
  private lastLevel = 1;
  private lastExp01 = 0.1;

  /**
   * Dream 29* instance countdown (CBE local timer; no WT packet).
   * Paint remaining as MM:SS. Offline freezes leftover (server
   * account_roles.dream_hud_remaining_ms); resume from storage / thaw.
   * Evidence: mock dreamHudTtl + docs/re/2026-08-07-pixi-dream-hud-countdown.md.
   */
  private dreamTimerHost = new Container();
  private dreamTimerIcon: Sprite | null = null;
  private dreamTimerDigits: DigitStrip | null = null;
  private dreamExpireAtMs = 0;
  /** Non-null while disconnected: leftover ms frozen (not wall-clock). */
  private dreamFrozenRemainMs: number | null = null;
  private dreamTimerTick: ReturnType<typeof setInterval> | null = null;
  private dreamTtlMsCached: number | null = null;
  private dreamSceneKey = "";
  /** Role-scoped localStorage checkpoint (matches account_roles row). */
  private dreamRoleId = 0;
  /** Bumps on each sync so overlapping async arms cannot overwrite leftover. */
  private dreamSyncGen = 0;
  /** Last vitals / party for Graphics rebuild after webglcontextrestored. */
  private lastHp = 0;
  private lastMaxHp = 1;
  private lastMp = 0;
  private lastMaxMp = 1;
  private lastParty: HudPartyMate[] = [];
  private lastPortraitActor = "";

  /** Center toast for「修」buff list / short world tips. */
  private toast: PanelTipState = createPanelTipState();

  /** tiao.gif black-slot geometry (probe-tiao / probe-hud-boxes). */
  private static readonly FILL_X = 18;
  private static readonly FILL_W = 36;
  private static readonly HP_Y = 4;
  private static readonly SP_Y = 12;
  private static readonly FILL_H = 4;

  constructor() {
    super();
    this.eventMode = "static";
    this.sortableChildren = true;
    this.toast.host.zIndex = 2_000_000;
    this.addChild(this.top, this.bottom, this.toast.host);
    this.bottom.y = LCD_HEIGHT - WORLD_BOTTOM_H;
    this.bottom.eventMode = "static";

    // Location floats on deep/map tint; status is the opaque olive band.
    this.paintTopBands();
    this.top.addChild(this.topDeepGfx, this.topStatusGfx, this.topBelowGfx);

    this.top.addChild(this.locBannerGfx, this.locBannerFlowerHost);
    this.paintLocBanner();
    preloadCbeFlowerCorners(() => {
      if (!this.destroyed) this.paintLocBanner();
    });
    this.locText = makePixelText("场景", 12, CBE.ink);
    this.locText.anchor.set(0.5, 0.5);
    this.locText.position.set(
      Math.round(LCD_WIDTH / 2),
      Math.round(2 + CBE_TITLE_H / 2),
    );
    this.top.addChild(this.locText);

    // handkuang.gif portrait frame (same asset as equip slots); art in mountArt.
    this.portraitFrameHost.position.set(PORTRAIT_FRAME_X, PORTRAIT_FRAME_Y);
    this.top.addChild(this.portraitFrameHost);
    this.portraitHost.position.set(PORTRAIT_X, PORTRAIT_Y);
    this.top.addChild(this.portraitHost);
    // Hit covers frame (not only inner face) so empty/loading portrait still taps.
    const portraitHit = hitArea(
      PORTRAIT_FRAME_X,
      PORTRAIT_FRAME_Y,
      SLOT_FRAME_SIZE,
      SLOT_FRAME_SIZE,
    );
    portraitHit.on("pointertap", () => this.onPortrait?.());
    this.top.addChild(portraitHit);

    this.tiaoHost.position.set(TIAO_X, TIAO_Y);
    this.tiaoHost.addChild(this.hpFill, this.spFill);
    this.top.addChild(this.tiaoHost);

    for (let i = 0; i < 5; i++) {
      const slot = new Container();
      slot.position.set(ICON_X0 + i * ICON_PITCH, ICON_Y);
      slot.eventMode = "static";
      slot.cursor = "pointer";
      const action = ICON_ACTIONS[i]!;
      slot.on("pointertap", () => this.onIcon?.(action));
      this.top.addChild(slot);
      this.iconSlots.push(slot);
    }

    this.expHost.position.set(0, EXP_Y);
    this.top.addChild(this.expHost);

    // Soft row + chat (reference: no bag counter on soft bar)
    this.paintSoftBands();
    this.bottom.addChild(this.softBgGfx, this.softFxGfx, this.chatBgGfx);

    this.hangLabel = makePixelText("挂机", 12, 0xffffff);
    this.hangLabel.anchor.set(0.5, 0.5);
    this.bottom.addChild(this.hangLabel);
    this.switchHost.eventMode = "static";
    this.bottom.addChild(this.switchHost);

    for (let i = 0; i < CHAT_VISIBLE_ROWS; i++) {
      const line = new BitmapLabel();
      line.position.set(4, SOFT_H + 2 + i * CHAT_LINE_H);
      this.chatStripHost.addChild(line);
      this.chatLines.push(line);
    }
    this.bottom.addChild(this.chatStripHost);

    this.menuHit = hitArea(2, 2, 26, 18);
    this.hangHit = hitArea(HANG_BTN_X, 0, HANG_BTN_W, SOFT_H);
    this.homeHit = hitArea(LCD_WIDTH - 28, 2, 26, 18);
    this.chatHit = hitArea(0, SOFT_H, LCD_WIDTH, WORLD_BOTTOM_H - SOFT_H);
    this.bottom.addChild(
      this.chatHit,
      this.menuHit,
      this.hangHit,
      this.homeHit,
    );
    this.menuHit.on("pointertap", () => this.onMenu?.());
    this.hangHit.on("pointertap", () => this.onHangup?.());
    this.homeHit.on("pointertap", () => this.onHome?.());
    this.chatHit.on("pointertap", () => this.onChat?.());

    this.setVitals(500, 500, 200, 200);

    this.partyHost.position.set(0, PARTY_Y0);
    this.partyHost.visible = false;
    this.addChild(this.partyHost);

    // Top-left of location band; hidden until dream enter.
    // Left avoids the map-corner status strip white dot on the right.
    this.dreamTimerHost.visible = false;
    this.dreamTimerHost.position.set(4, 6);
    this.top.addChild(this.dreamTimerHost);
  }

  async mountArt(): Promise<void> {
    if (this.artReady) return;
    this.artReady = true;
    await this.remountTextureChrome();
  }

  /**
   * Rebuild UIsh / EXP / softkey / tiao / dream-timer Sprites from CPU RGBA.
   * Required after webglcontextrestored — nudgeTextureSource leaves fake-alive blacks.
   */
  async remountTextureChrome(): Promise<void> {
    for (const slot of this.iconSlots) {
      slot.removeChildren().forEach((c) => c.destroy({ children: true }));
    }
    if (this.tiaoChrome) {
      try {
        if (this.tiaoChrome.parent) this.tiaoHost.removeChild(this.tiaoChrome);
        this.tiaoChrome.destroy({ texture: false });
      } catch {
        /* ignore */
      }
      this.tiaoChrome = null;
    }
    this.expHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.expFill = null;
    this.expDigits = null;
    for (const s of this.softArtSprites) {
      try {
        if (s.parent) s.parent.removeChild(s);
        s.destroy({ texture: false });
      } catch {
        /* ignore */
      }
    }
    this.softArtSprites = [];
    this.dreamTimerHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.dreamTimerIcon = null;
    this.dreamTimerDigits = null;

    this.handkuangTex = await tryLoadUiTexture("handkuang.gif");
    if (this.handkuangTex) {
      this.portraitFrameHost.removeChildren().forEach((c) => c.destroy());
      this.portraitFrameHost.addChild(
        uiSprite(this.handkuangTex, SLOT_FRAME_SIZE, SLOT_FRAME_SIZE),
      );
    }

    const tiao = await tryLoadUiTexture("tiao.gif");
    if (tiao) {
      this.tiaoChrome = uiSprite(tiao);
      this.tiaoHost.addChildAt(this.tiaoChrome, 0);
      /*
       * tiao.gif is 66×19 but x≥58 carries leftover red/cyan stubs (sheet junk).
       * Self HUD hides them under UIsh icons; party strip must crop them off.
       */
      const cropW = Math.min(58, tiao.width);
      this.partyTiao = new Texture({
        source: tiao.source,
        frame: new Rectangle(0, 0, cropW, tiao.height),
      });
      this.partyTiao.source.scaleMode = "nearest";
    } else {
      this.partyTiao = null;
    }

    for (let i = 0; i < 5; i++) {
      const tex = await tryLoadUiTexture(ICON_FILES[i]!);
      if (!tex) continue;
      const slot = this.iconSlots[i]!;
      slot.addChild(uiSprite(tex));
    }

    const uiexp = await tryLoadUiTexture("uiexp.gif");
    if (uiexp) this.expHost.addChild(uiSprite(uiexp));
    this.expDigits = await DigitStrip.expNum();
    if (this.expDigits) {
      this.expHost.addChild(this.expDigits.root);
      this.expDigits.setText("01", 3, 3);
    }
    const track = await tryLoadUiTexture("exp0.gif");
    if (track) {
      const spr = uiSprite(track);
      spr.position.set(40, 5);
      this.expHost.addChild(spr);
    }
    const fill = await tryLoadUiTexture("exp1.gif");
    if (fill) {
      this.expFill = uiSprite(fill);
      this.expFill.position.set(41, 6);
      this.expHost.addChild(this.expFill);
    }

    const menu = await tryLoadUiTexture("UIxia1.gif");
    if (menu) {
      const spr = uiSprite(menu);
      spr.eventMode = "none";
      spr.position.set(2, 3);
      this.bottom.addChild(spr);
      this.softArtSprites.push(spr);
    }
    const hangBg = await tryLoadUiTexture("UI7.gif");
    const hangX = HANG_BTN_X;
    if (hangBg) {
      const spr = uiSprite(hangBg);
      spr.eventMode = "none";
      spr.position.set(hangX, 0);
      this.bottom.addChild(spr);
      this.softArtSprites.push(spr);
    }
    // Always re-add: remount puts hangBg after hangLabel and would cover 「挂机」.
    this.hangLabel.eventMode = "none";
    this.hangLabel.position.set(hangX + HANG_BTN_W / 2, SOFT_H / 2);
    this.bottom.addChild(this.hangLabel);
    void this.hangLabel.show("挂机", 0xffffff, {
      fontSize: 12,
      outline: false,
      smooth: false,
      crisp: true,
    });

    this.switchBgTex = await tryLoadUiTexture("UI7.gif");
    this.bottom.addChild(this.switchHost);
    this.rebuildAccountSwitchChips();

    const home = await tryLoadUiTexture("UIxia3.gif");
    if (home) {
      const spr = uiSprite(home);
      spr.eventMode = "none";
      spr.position.set(LCD_WIDTH - 27, 3);
      this.bottom.addChild(spr);
      this.softArtSprites.push(spr);
    }
    // Art mounts after hit targets — re-raise so pointer reaches softkeys / chat.
    this.bottom.addChild(this.chatHit, this.menuHit, this.hangHit, this.homeHit);
    this.bottom.addChild(this.switchHost);

    await this.mountDreamTimerArt();
    this.setLevel(this.lastLevel, this.lastExp01);
    this.refreshChatStrip();
  }

  async setPortraitActor(actorName: string): Promise<void> {
    this.lastPortraitActor = actorName;
    try {
      const loaded = await loadActor(actorName);
      let frame = composeActorFrameIndex(loaded, 0);
      if (!frame || frame.width < 4 || frame.height < 4) {
        frame = composeActorFrameIndex(loaded, 1) ?? frame;
      }
      if (!frame || frame.width < 4 || frame.height < 4) return;
      const canvas = document.createElement("canvas");
      canvas.width = frame.width;
      canvas.height = frame.height;
      canvas.getContext("2d")!.putImageData(
        new ImageData(new Uint8ClampedArray(frame.rgba), frame.width, frame.height),
        0,
        0,
      );
      const tex = Texture.from(canvas);
      tex.source.scaleMode = "nearest";
      this.portraitHost.removeChildren().forEach((c) => {
        try {
          c.destroy({ children: true, texture: true, textureSource: true });
        } catch {
          /* ignore */
        }
      });
      const spr = new Sprite(tex);
      spr.width = PORTRAIT_INNER;
      spr.height = PORTRAIT_INNER;
      this.portraitHost.addChild(spr);
    } catch (e) {
      console.warn("[hud] portrait", actorName, e);
    }
  }

  setIdentity(name: string, scene: string, title = ""): void {
    void name;
    void title;
    this.locText.text = sceneDisplayName(scene);
    void this.syncDreamTimerForScene(scene);
  }

  /**
   * Spread nickname chips after 挂机. Width shrinks to fit until the home key.
   * Clicking a non-active chip switches directly (no dialog).
   */
  setAccountSwitchSlots(slots: HudAccountSwitchSlot[]): void {
    this.switchSlots = slots.slice();
    this.rebuildAccountSwitchChips();
  }

  private clearAccountSwitchChips(): void {
    for (const chip of this.switchChips) {
      try {
        chip.root.removeAllListeners();
        chip.root.destroy({ children: true });
      } catch {
        /* ignore */
      }
    }
    this.switchChips = [];
    this.switchHost.removeChildren();
  }

  private rebuildAccountSwitchChips(): void {
    this.clearAccountSwitchChips();
    const slots = this.switchSlots;
    if (!slots.length) {
      this.switchHost.visible = false;
      return;
    }
    this.switchHost.visible = true;
    const avail = Math.max(0, SWITCH_STRIP_RIGHT - SWITCH_STRIP_X);
    const n = slots.length;
    const btnW = SWITCH_BTN_W;
    let show = n;
    while (show > 1 && show * btnW + SWITCH_BTN_GAP * (show - 1) > avail) {
      show -= 1;
    }
    // Prefer keeping the active chip visible if we must truncate.
    let visible = slots.slice(0, show);
    if (show < n) {
      const active = slots.find((s) => s.active);
      if (active && !visible.some((s) => s.id === active.id)) {
        visible = [...slots.slice(0, show - 1), active];
      }
    }

    let x = SWITCH_STRIP_X;
    for (const slot of visible) {
      const root = new Container();
      root.position.set(x, 0);
      root.eventMode = "static";
      root.cursor = slot.active ? "default" : "pointer";
      root.hitArea = new Rectangle(0, 0, btnW, SOFT_H);

      const active = !!slot.active;
      let bg: Sprite | Graphics;
      if (this.switchBgTex) {
        const spr = uiSprite(this.switchBgTex, btnW, SOFT_H);
        spr.eventMode = "none";
        // Dim non-active chrome so purple ink only reads on the current chip.
        spr.tint = active ? 0xffffff : 0x9a9a9a;
        spr.alpha = active ? 1 : 0.92;
        bg = spr;
      } else {
        const g = new Graphics()
          .rect(0, 0, btnW, SOFT_H)
          .fill({ color: active ? 0x3a5a3a : 0x2a2a2a });
        g.eventMode = "none";
        bg = g;
      }
      root.addChild(bg);

      // One setText only — makePixelText+show raced and could leave wrong ink.
      // Force UC3 bake so soft-row color is the canvas glyph (not Songti DOM).
      const ink = active ? 0xc060ff : 0xffffff;
      const label = new BitmapLabel();
      label.eventMode = "none";
      label.anchor.set(0.5, 0.5);
      label.position.set(btnW / 2, SOFT_H / 2);
      root.addChild(label);
      void label.setText(slot.label || "？", ink, {
        fontSize: 12,
        outline: false,
        smooth: false,
        crisp: true,
        uc3: true,
      });

      const id = slot.id;
      root.on("pointertap", () => {
        if (active) return;
        this.onAccountSwitch?.(id);
      });

      this.switchHost.addChild(root);
      this.switchChips.push({ id, root, bg, label });
      x += btnW + SWITCH_BTN_GAP;
    }
  }

  /**
   * Arm HUD from server remain (29/23), mirroring exp-card wall-clock after
   * pause/resume. Prefer this over local full TTL / localStorage.
   */
  applyServerDreamRemainMs(remainMs: number, scene?: string): void {
    const ms = Math.max(0, remainMs | 0);
    if (scene && isDreamInstanceScene(scene)) {
      this.dreamSceneKey = dreamSceneLeaf(scene);
    }
    if (!this.dreamSceneKey) return;
    this.dreamSyncGen++;
    if (ms <= 0) {
      this.clearDreamTimer(true);
      return;
    }
    this.dreamFrozenRemainMs = null;
    this.dreamExpireAtMs = Date.now() + ms;
    this.dreamTimerHost.visible = true;
    this.persistDreamRemainMs();
    this.paintDreamTimer();
    if (this.dreamTimerTick == null) {
      this.dreamTimerTick = setInterval(() => this.paintDreamTimer(), 1000);
    }
  }

  /**
   * Arm / keep / clear dream HUD countdown from the visible scene key.
   * Server remain (applyServerDreamRemainMs / 29/23) is authoritative after
   * login resume — local frozen/LS are only optimistic until that arrives.
   */
  async syncDreamTimerForScene(scene: string): Promise<void> {
    if (!isDreamInstanceScene(scene)) {
      if (scene.trim()) {
        this.clearDreamTimer(true);
      } else {
        this.pauseDreamTimer();
        this.dreamTimerHost.visible = false;
        if (this.dreamTimerTick != null) {
          clearInterval(this.dreamTimerTick);
          this.dreamTimerTick = null;
        }
      }
      return;
    }
    const key = dreamSceneLeaf(scene);
    if (
      this.dreamFrozenRemainMs != null &&
      this.dreamFrozenRemainMs > 0 &&
      this.dreamSceneKey === key
    ) {
      this.resumeDreamTimer();
      return;
    }
    if (
      this.dreamExpireAtMs > Date.now() &&
      this.dreamSceneKey === key &&
      this.dreamTimerHost.visible
    ) {
      this.paintDreamTimer();
      return;
    }
    // Mark leaf; wait for 29/23 server remain (exp-card style). Soft-show
    // local leftover only so the digit strip is not empty during the roundtrip.
    this.dreamSceneKey = key;
    const stored = this.loadStoredDreamRemainMs(key);
    if (stored != null && stored > 0) {
      this.dreamFrozenRemainMs = null;
      this.dreamExpireAtMs = Date.now() + stored;
      this.dreamTimerHost.visible = true;
      this.paintDreamTimer();
      if (this.dreamTimerTick == null) {
        this.dreamTimerTick = setInterval(() => this.paintDreamTimer(), 1000);
      }
    }
  }

  /** Bind role id so dream remain checkpoints do not leak across characters. */
  setDreamRoleId(roleId: number): void {
    const next = roleId >>> 0;
    // Switching to a different character drops in-memory clock (LS is role-scoped).
    if (this.dreamRoleId !== 0 && next !== 0 && this.dreamRoleId !== next) {
      this.dreamFrozenRemainMs = null;
      this.dreamExpireAtMs = 0;
      this.dreamSceneKey = "";
      this.dreamTimerHost.visible = false;
      if (this.dreamTimerTick != null) {
        clearInterval(this.dreamTimerTick);
        this.dreamTimerTick = null;
      }
    }
    this.dreamRoleId = next;
  }

  /** Freeze leftover across disconnect / leave-game; wall clock must not consume it. */
  pauseDreamTimer(): void {
    if (this.dreamFrozenRemainMs != null) return;
    if (this.dreamExpireAtMs <= 0) return;
    if (!this.dreamSceneKey) return;
    const remain = Math.max(0, this.dreamExpireAtMs - Date.now());
    this.dreamFrozenRemainMs = remain;
    this.dreamExpireAtMs = 0;
    this.persistDreamRemainMs();
    if (this.dreamTimerHost.visible) this.paintDreamTimer();
  }

  /** Resume after reconnect / re-enter with the same dream leaf. */
  resumeDreamTimer(): void {
    if (this.dreamFrozenRemainMs == null) return;
    const remain = this.dreamFrozenRemainMs;
    this.dreamFrozenRemainMs = null;
    if (remain <= 0 || !this.dreamSceneKey) {
      this.clearDreamTimer(true);
      return;
    }
    this.dreamExpireAtMs = Date.now() + remain;
    this.dreamTimerHost.visible = true;
    this.persistDreamRemainMs();
    this.paintDreamTimer();
    if (this.dreamTimerTick == null) {
      this.dreamTimerTick = setInterval(() => this.paintDreamTimer(), 1000);
    }
  }

  /**
   * @param wipeStorage true when leaving 29* for outdoor / expired;
   *   false on logout so re-login can restore leftover.
   */
  clearDreamTimer(wipeStorage = true): void {
    this.dreamExpireAtMs = 0;
    this.dreamFrozenRemainMs = null;
    if (wipeStorage && this.dreamSceneKey) {
      this.clearStoredDreamRemainMs(this.dreamSceneKey);
    }
    this.dreamSceneKey = "";
    this.dreamTimerHost.visible = false;
    if (this.dreamTimerTick != null) {
      clearInterval(this.dreamTimerTick);
      this.dreamTimerTick = null;
    }
  }

  private async mountDreamTimerArt(): Promise<void> {
    this.dreamTimerHost.removeChildren().forEach((c) => c.destroy({ children: true }));
    this.dreamTimerIcon = null;
    this.dreamTimerDigits = null;
    const tex = await tryLoadUiTexture("ui_timer.gif");
    if (tex) {
      this.dreamTimerIcon = uiSprite(tex);
      this.dreamTimerIcon.position.set(0, 0);
      this.dreamTimerHost.addChild(this.dreamTimerIcon);
    }
    this.dreamTimerDigits = await DigitStrip.timeNum();
    if (this.dreamTimerDigits) {
      const iconW = this.dreamTimerIcon ? this.dreamTimerIcon.width + 2 : 0;
      this.dreamTimerDigits.setText("60:00", iconW, 4);
      this.dreamTimerHost.addChild(this.dreamTimerDigits.root);
    }
    this.layoutDreamTimer();
    if (this.dreamTimerHost.visible) this.paintDreamTimer();
  }

  private async loadDreamHudTtlMs(): Promise<number> {
    if (this.dreamTtlMsCached != null) return this.dreamTtlMsCached;
    let ttl = 60 * 60 * 1000;
    try {
      const bytes = await fetchResourceBytes("dream_hud_ttl_ms.txt");
      const text = new TextDecoder().decode(bytes).trim();
      const n = parseInt(text, 10);
      if (Number.isFinite(n) && n >= 1000 && n <= 7 * 24 * 3600 * 1000) {
        ttl = n;
      }
    } catch {
      /* default 60min — same as mock_server dream_hud_ttl_ms */
    }
    this.dreamTtlMsCached = ttl;
    return ttl;
  }

  private paintDreamTimer(): void {
    if (!this.dreamTimerHost.visible) return;
    let remainMs = 0;
    if (this.dreamFrozenRemainMs != null) {
      remainMs = Math.max(0, this.dreamFrozenRemainMs);
    } else if (this.dreamExpireAtMs > 0) {
      remainMs = Math.max(0, this.dreamExpireAtMs - Date.now());
      this.persistDreamRemainMs();
    } else {
      return;
    }
    const totalSec = Math.ceil(remainMs / 1000);
    const mm = Math.floor(totalSec / 60);
    const ss = totalSec % 60;
    /* Wall-clock MM:SS (e.g. 60min → 60:00 … 59:59). time_number has 0-9 + ':'. */
    const text = `${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}`;
    if (this.dreamTimerDigits) {
      const iconW = this.dreamTimerIcon ? this.dreamTimerIcon.width + 2 : 0;
      this.dreamTimerDigits.setText(text, iconW, 4);
    }
    this.layoutDreamTimer();
  }

  private dreamRemainStorageKey(sceneKey: string, roleId = this.dreamRoleId): string {
    const role = roleId >>> 0;
    return role
      ? `pix_dream_hud_remain:${role}:${sceneKey}`
      : `pix_dream_hud_remain:${sceneKey}`;
  }

  private persistDreamRemainMs(): void {
    if (!this.dreamSceneKey || typeof localStorage === "undefined") return;
    let remain = 0;
    if (this.dreamFrozenRemainMs != null) remain = this.dreamFrozenRemainMs;
    else if (this.dreamExpireAtMs > 0)
      remain = Math.max(0, this.dreamExpireAtMs - Date.now());
    try {
      if (remain > 0) {
        localStorage.setItem(
          this.dreamRemainStorageKey(this.dreamSceneKey),
          String(remain),
        );
      } else {
        localStorage.removeItem(this.dreamRemainStorageKey(this.dreamSceneKey));
      }
    } catch {
      /* private mode */
    }
  }

  private loadStoredDreamRemainMs(sceneKey: string): number | null {
    if (typeof localStorage === "undefined") return null;
    try {
      const scoped = localStorage.getItem(this.dreamRemainStorageKey(sceneKey));
      if (scoped) {
        const n = parseInt(scoped, 10);
        if (Number.isFinite(n) && n > 0) return n;
      }
      // Legacy key (pre role-scope) — migrate once.
      const legacy = localStorage.getItem(`pix_dream_hud_remain:${sceneKey}`);
      if (!legacy) return null;
      const n = parseInt(legacy, 10);
      if (!Number.isFinite(n) || n <= 0) return null;
      if (this.dreamRoleId) {
        try {
          localStorage.setItem(this.dreamRemainStorageKey(sceneKey), String(n));
          localStorage.removeItem(`pix_dream_hud_remain:${sceneKey}`);
        } catch {
          /* keep legacy readable */
        }
      }
      return n;
    } catch {
      return null;
    }
  }

  private clearStoredDreamRemainMs(sceneKey: string): void {
    if (typeof localStorage === "undefined") return;
    try {
      localStorage.removeItem(this.dreamRemainStorageKey(sceneKey));
      localStorage.removeItem(`pix_dream_hud_remain:${sceneKey}`);
    } catch {
      /* private mode */
    }
  }

  private layoutDreamTimer(): void {
    this.dreamTimerHost.position.set(4, 6);
  }

  setVitals(hp: number, maxHp: number, mp: number, maxMp: number): void {
    this.lastHp = hp;
    this.lastMaxHp = maxHp;
    this.lastMp = mp;
    this.lastMaxMp = maxMp;
    const maxW = WorldHud.FILL_W;
    const hpW = Math.max(0, Math.min(maxW, Math.floor((maxW * hp) / Math.max(1, maxHp))));
    const spW = Math.max(0, Math.min(maxW, Math.floor((maxW * mp) / Math.max(1, maxMp))));
    const x = WorldHud.FILL_X;
    this.hpFill.clear().rect(x, WorldHud.HP_Y, hpW, WorldHud.FILL_H).fill({ color: 0xe03828 });
    this.spFill.clear().rect(x, WorldHud.SP_Y, spW, WorldHud.FILL_H).fill({ color: 0x38a0e0 });
  }

  private paintTopBands(): void {
    this.topDeepGfx
      .clear()
      .rect(0, 0, LCD_WIDTH, STATUS_BG_Y)
      .fill({ color: CBE.bgDeep });
    this.topStatusGfx
      .clear()
      .rect(0, STATUS_BG_Y, LCD_WIDTH, STATUS_BG_H)
      .fill({ color: CBE.bgHudRow });
    this.topBelowGfx
      .clear()
      .rect(
        0,
        STATUS_BG_Y + STATUS_BG_H,
        LCD_WIDTH,
        WORLD_TOP_H - STATUS_BG_Y - STATUS_BG_H,
      )
      .fill({ color: CBE.bgDeep });
  }

  private paintLocBanner(): void {
    this.locBannerGfx.clear();
    clearCbeFlowerCorners(this.locBannerFlowerHost);
    paintLocTitleChrome(this.locBannerGfx, this.locBannerFlowerHost, 2);
  }

  private paintSoftBands(): void {
    this.softBgGfx
      .clear()
      .rect(0, 0, LCD_WIDTH, SOFT_H)
      .fill({ color: CBE.bgPanel });
    this.softFxGfx.clear();
    this.softFxGfx
      .moveTo(2, 4)
      .lineTo(2, 2)
      .lineTo(14, 2)
      .stroke({ width: 1, color: CBE.borderBright });
    this.softFxGfx
      .moveTo(LCD_WIDTH - 2, 4)
      .lineTo(LCD_WIDTH - 2, 2)
      .lineTo(LCD_WIDTH - 14, 2)
      .stroke({ width: 1, color: CBE.borderBright });
    this.chatBgGfx
      .clear()
      .rect(0, SOFT_H, LCD_WIDTH, WORLD_BOTTOM_H - SOFT_H)
      .fill({ color: CBE.chatBg });
  }

  /**
   * webglcontextrestored: remount Graphics bands + UI GIF Texture chrome
   * (icons / EXP / softkeys / tiao). nudgeTextureSource alone leaves blacks.
   */
  nudgeAfterGpuResume(): void {
    this.topDeepGfx = replaceGraphics(this.topDeepGfx);
    this.topStatusGfx = replaceGraphics(this.topStatusGfx);
    this.topBelowGfx = replaceGraphics(this.topBelowGfx);
    this.paintTopBands();
    this.locBannerGfx = replaceGraphics(this.locBannerGfx);
    this.paintLocBanner();
    this.softBgGfx = replaceGraphics(this.softBgGfx);
    this.softFxGfx = replaceGraphics(this.softFxGfx);
    this.chatBgGfx = replaceGraphics(this.chatBgGfx);
    this.paintSoftBands();
    this.hpFill = replaceGraphics(this.hpFill);
    this.spFill = replaceGraphics(this.spFill);
    this.tiaoHost.addChild(this.hpFill, this.spFill);
    this.setVitals(this.lastHp, this.lastMaxHp, this.lastMp, this.lastMaxMp);
    for (const slot of this.partySlots) {
      slot.hpFill = replaceGraphics(slot.hpFill);
      slot.spFill = replaceGraphics(slot.spFill);
    }
    if (this.toast.gfx.parent) {
      this.toast.gfx = replaceGraphics(this.toast.gfx);
    }
    const finishParty = (): void => {
      /* Rebuild party strip so handkuang/tiao sprites are not dead UiAssets. */
      this.partyHost.removeChildren().forEach((c) => c.destroy({ children: true }));
      this.partySlots = [];
      this.setParty(this.lastParty);
      if (this.lastPortraitActor) {
        void this.setPortraitActor(this.lastPortraitActor);
      }
      this.refreshChatStrip();
    };
    if (this.artReady) {
      void this.remountTextureChrome()
        .then(finishParty)
        .catch((e) => {
          console.warn("[hud] texture chrome remount", e);
          finishParty();
        });
    } else {
      finishParty();
    }
  }

  /**
   * Show teammate vitals under self status (map overlay).
   * Pass empty / solo roster to hide (leave team).
   */
  setParty(mates: HudPartyMate[]): void {
    const list = mates.slice(0, PARTY_MAX);
    this.lastParty = list;
    while (this.partySlots.length < list.length) {
      this.partySlots.push(this.makePartySlot(this.partySlots.length));
    }
    for (let i = 0; i < this.partySlots.length; i++) {
      const slot = this.partySlots[i]!;
      const mate = list[i];
      if (!mate) {
        slot.root.visible = false;
        slot.mateId = 0;
        slot.crown.visible = false;
        slot.portrait.removeChildren().forEach((c) => c.destroy({ children: true }));
        slot.hpFill.clear();
        slot.spFill.clear();
        continue;
      }
      slot.root.visible = true;
      slot.mateId = mate.id;
      slot.root.y = i * PARTY_SLOT_H;
      slot.crown.visible = mate.isLeader;
      this.paintPartyBars(slot, mate.hp, mate.hpMax, mate.mp, mate.mpMax);
      void this.fillPartyPortrait(slot, mate.jobIndex, mate.sex);
    }
    this.partyHost.visible = list.length > 0;
    this.onPartyChanged?.(list.length);
  }

  /**
   * Map-local Y the quest tracker should clear below party slots.
   * 0 when solo / strip hidden.
   */
  partyOverlayOffsetY(): number {
    if (!this.partyHost.visible) return 0;
    return this.lastParty.length * PARTY_SLOT_H;
  }

  private makePartySlot(index: number): PartySlot {
    const root = new Container();
    root.y = index * PARTY_SLOT_H;

    const frameHost = new Container();
    frameHost.position.set(PORTRAIT_FRAME_X, 0);
    if (this.handkuangTex) {
      frameHost.addChild(
        uiSprite(this.handkuangTex, SLOT_FRAME_SIZE, SLOT_FRAME_SIZE),
      );
    } else {
      frameHost.addChild(
        new Graphics()
          .rect(0, 0, SLOT_FRAME_SIZE, SLOT_FRAME_SIZE)
          .stroke({ width: 1, color: 0xd4a820 }),
      );
    }
    root.addChild(frameHost);

    const portrait = new Container();
    portrait.position.set(PORTRAIT_X, PORTRAIT_PAD);
    root.addChild(portrait);

    const crown = drawCrown(PORTRAIT_FRAME_X + 1, -5);
    crown.visible = false;
    root.addChild(crown);

    const tiaoHost = new Container();
    tiaoHost.position.set(TIAO_X, 2);
    if (this.partyTiao) tiaoHost.addChild(uiSprite(this.partyTiao));
    const hpFill = new Graphics();
    const spFill = new Graphics();
    tiaoHost.addChild(hpFill, spFill);
    root.addChild(tiaoHost);

    const slot: PartySlot = {
      root,
      portrait,
      crown,
      hpFill,
      spFill,
      mateId: 0,
    };
    // Portrait frame hit only — bars stay non-interactive walk-through.
    const faceHit = hitArea(
      PORTRAIT_FRAME_X,
      0,
      SLOT_FRAME_SIZE,
      SLOT_FRAME_SIZE,
    );
    faceHit.on("pointertap", () => {
      if (slot.mateId) this.onPartyMate?.(slot.mateId);
    });
    root.addChild(faceHit);

    this.partyHost.addChild(root);
    return slot;
  }

  private paintPartyBars(
    slot: PartySlot,
    hp: number,
    hpMax: number,
    mp: number,
    mpMax: number,
  ): void {
    const maxW = WorldHud.FILL_W;
    const hpW = Math.max(0, Math.min(maxW, Math.floor((maxW * hp) / Math.max(1, hpMax))));
    const spW = Math.max(0, Math.min(maxW, Math.floor((maxW * mp) / Math.max(1, mpMax))));
    const x = WorldHud.FILL_X;
    slot.hpFill.clear().rect(x, WorldHud.HP_Y, hpW, WorldHud.FILL_H).fill({ color: 0xe03828 });
    slot.spFill.clear().rect(x, WorldHud.SP_Y, spW, WorldHud.FILL_H).fill({ color: 0x38a0e0 });
  }

  private async fillPartyPortrait(
    slot: PartySlot,
    jobIndex: number,
    sex: number,
  ): Promise<void> {
    const actorName = uiActorResourceForJobSex(jobIndex, sex);
    const mateId = slot.mateId;
    try {
      const loaded = await loadActor(actorName);
      // Cleared / replaced while loading — do not revive a hidden slot.
      if (slot.mateId !== mateId || mateId === 0 || !slot.root.visible) return;
      let frame = composeActorFrameIndex(loaded, 0);
      if (!frame || frame.width < 4 || frame.height < 4) {
        frame = composeActorFrameIndex(loaded, 1) ?? frame;
      }
      if (!frame || frame.width < 4 || frame.height < 4) return;
      const canvas = document.createElement("canvas");
      canvas.width = frame.width;
      canvas.height = frame.height;
      canvas.getContext("2d")!.putImageData(
        new ImageData(new Uint8ClampedArray(frame.rgba), frame.width, frame.height),
        0,
        0,
      );
      const tex = Texture.from(canvas);
      tex.source.scaleMode = "nearest";
      slot.portrait.removeChildren().forEach((c) => {
        try {
          c.destroy({ children: true, texture: true, textureSource: true });
        } catch {
          /* ignore */
        }
      });
      const spr = new Sprite(tex);
      spr.width = PARTY_FACE;
      spr.height = PARTY_FACE;
      slot.portrait.addChild(spr);
    } catch (e) {
      console.warn("[hud] party portrait", actorName, e);
    }
  }

  setLevel(level: number, exp01: number): void {
    const lv = Math.max(0, Math.min(99, level | 0));
    const frac = Math.min(1, Math.max(0, exp01));
    this.lastLevel = lv;
    this.lastExp01 = frac;
    this.expDigits?.setText(String(lv).padStart(2, "0"), 3, 3);
    // exp1.gif is 200×2; scale width by fraction
    if (this.expFill) this.expFill.width = Math.max(1, Math.floor(200 * frac));
  }

  /** Kept for session hooks; CBE main soft bar has no bag counter. */
  setBagSlots(_used: number, _cap: number): void {}

  /** Green center toast (增益列表 / short feedback). */
  showTip(text: string, ms = 1200): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => {
      if (this.visible) drawPanelTipOverlay(this, this.toast);
    }, ms);
  }

  /** Drop a leftover center toast so a panel tip is not drawn on top of it. */
  clearTip(): void {
    clearPanelTip(this.toast);
  }

  /** Right softkey closes stuck/transient toast without opening 菜单. */
  dismissTipOnSoftRight(): boolean {
    return tryDismissPanelTipOnSoftRight(this.toast);
  }

  pushChat(
    line: string,
    type?: number,
    meta?: {
      senderRoleId?: number;
      senderName?: string;
      color?: number;
      streakTitle?: string;
      streakColor?: number;
    },
  ): void {
    const t =
      type ??
      (line.startsWith("[世]")
        ? 0
        : line.startsWith("[队]")
          ? 2
          : line.startsWith("[帮]")
            ? 3
            : line.startsWith("[本]") || line.startsWith("[旁]")
              ? 4
              : line.startsWith("[私]")
                ? 7
                : 5);
    const streak =
      meta?.streakTitle && meta.streakColor != null
        ? { streakTitle: meta.streakTitle, streakColor: meta.streakColor }
        : (() => {
            const hit = findArenaKillStreakTitle(line);
            return hit
              ? { streakTitle: hit.title, streakColor: hit.color }
              : {};
          })();
    this.chatEntries.unshift({
      type: t,
      text: line,
      senderRoleId: meta?.senderRoleId,
      senderName: meta?.senderName,
      color: meta?.color,
      ...streak,
    });
    this.chatEntries = this.chatEntries.slice(0, 80);
    this.chatHistory.unshift(line);
    this.chatHistory = this.chatHistory.slice(0, 80);
    this.chatLog = this.chatHistory.slice(0, 2);
    this.refreshChatStrip();
  }

  /** Newest messages → wrap to strip width → paint first {@link CHAT_VISIBLE_ROWS} rows. */
  private refreshChatStrip(): void {
    type Row = { text: string; baseColor: number; entry: ChatEntry };
    const rows: Row[] = [];
    for (const entry of this.chatEntries) {
      const raw = (entry.text ?? "").replace(/\s*\n\s*/g, " ").trim();
      if (!raw) continue;
      const baseColor = colorForChatEntry(entry);
      for (const part of wrapUiText(raw, CHAT_WRAP_PX, CHAT_FONT_SIZE)) {
        rows.push({ text: part, baseColor, entry });
        if (rows.length >= CHAT_VISIBLE_ROWS) break;
      }
      if (rows.length >= CHAT_VISIBLE_ROWS) break;
    }
    /* Rebuild labels so streak title can use a second color on the same row. */
    this.chatStripHost.removeChildren().forEach((c) => c.destroy());
    this.chatLines = [];
    for (let i = 0; i < CHAT_VISIBLE_ROWS; i++) {
      const row = rows[i];
      const y = SOFT_H + 2 + i * CHAT_LINE_H;
      if (!row) {
        const empty = new BitmapLabel();
        empty.position.set(4, y);
        void empty.setText("", CBE.inkChat, { outline: false, wordWrap: false });
        this.chatStripHost.addChild(empty);
        this.chatLines.push(empty);
        continue;
      }
      let x = 4;
      const runs = chatLineColorRuns(row.text, row.baseColor, row.entry);
      for (const run of runs) {
        if (!run.text) continue;
        const lab = new BitmapLabel();
        lab.position.set(x, y);
        void lab.setText(run.text, run.color, {
          outline: false,
          wordWrap: false,
          fontSize: CHAT_FONT_SIZE,
        });
        this.chatStripHost.addChild(lab);
        this.chatLines.push(lab);
        x += measureUiTextWidth(run.text, CHAT_FONT_SIZE);
      }
    }
  }

  /** Oldest→newest lines for the chat-info menu panel. */
  getChatHistory(): string[] {
    return this.chatHistory.slice().reverse();
  }

  /** Oldest→newest typed entries for ChatPanel tabs. */
  getChatEntries(): ChatEntry[] {
    return this.chatEntries.slice().reverse();
  }

  /** Newest-first snapshot for per-account multi-open buffers. */
  snapshotChatNewestFirst(): ChatEntry[] {
    return this.chatEntries.map((e) => ({ ...e }));
  }

  /** Restore a per-account chat buffer (newest-first). */
  replaceChatNewestFirst(entries: ChatEntry[]): void {
    this.chatEntries = entries.slice(0, 80).map((e) => ({ ...e }));
    this.chatHistory = this.chatEntries.map((e) => e.text);
    this.chatLog = this.chatHistory.slice(0, 2);
    this.refreshChatStrip();
  }
}

/**
 * Top location pill label.
 * Prefer sMap.dsh col-2 alias (临安-庆阳宫); fall back to a short leaf stem.
 */
export function sceneDisplayName(scene: string): string {
  const alias = lookupSMapAlias(scene);
  if (alias) return alias.slice(0, 12);

  const leaf = scene.replace(/\.sce$/i, "").replace(/^c(?=\d)/i, "");
  const m = leaf.match(/^(?:\d+)?(.+?)_(\d+)$/i);
  if (m) {
    const base = m[1]!;
    const dir =
      ({ "01": "北", "02": "南", "03": "东", "04": "西" } as Record<string, string>)[
        m[2]!
      ] ?? `_${m[2]}`;
    return `${base}${dir}`.slice(0, 12);
  }
  const base = leaf.replace(/^\d+/, "").replace(/_[0-9a-z]+$/i, "");
  return (base || "场景").slice(0, 12);
}

/** Dream instance maps: 29* / b_29* (server wall TTL + local HUD clock). */
export function isDreamInstanceScene(scene: string): boolean {
  const leaf = dreamSceneLeaf(scene);
  return leaf.startsWith("29") || leaf.startsWith("b_29");
}

function dreamSceneLeaf(scene: string): string {
  return scene
    .replace(/^.*[/\\]/, "")
    .replace(/\.sce$/i, "")
    .trim()
    .toLowerCase();
}

/** Simple leader crown (vip_effect.gif is the「斗」status tile, not a crown). */
function drawCrown(x: number, y: number): Graphics {
  const g = new Graphics();
  g.moveTo(x, y + 7)
    .lineTo(x + 4, y + 1)
    .lineTo(x + 8, y + 6)
    .lineTo(x + 12, y + 1)
    .lineTo(x + 16, y + 7)
    .lineTo(x, y + 7)
    .fill({ color: 0xe8c040 });
  g.rect(x + 1, y + 7, 14, 2).fill({ color: 0xd4a820 });
  return g;
}

function hitArea(x: number, y: number, w: number, h: number): Container {
  const c = new Container();
  c.position.set(x, y);
  c.hitArea = { contains: (px: number, py: number) => px >= 0 && py >= 0 && px <= w && py <= h };
  c.eventMode = "static";
  c.cursor = "pointer";
  c.addChild(new Graphics().rect(0, 0, w, h).fill({ color: 0xffffff, alpha: 0.001 }));
  return c;
}
