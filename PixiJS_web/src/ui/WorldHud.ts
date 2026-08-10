import { Container, Graphics, Sprite, Texture } from "pixi.js";
import {
  CBE,
  LCD_HEIGHT,
  LCD_WIDTH,
  SOFTKEY_H,
  WORLD_BOTTOM_H,
  WORLD_TOP_H,
  drawCbePanel,
  drawLocBanner,
  makePixelText,
} from "./constants";
import {
  BitmapLabel,
  UC3_CELL,
  measureUiTextWidth,
  wrapUiText,
} from "./BitmapLabel";
import { DigitStrip, tryLoadUiTexture, uiSprite } from "./UiAssets";
import { colorForChatType } from "./ChatPanel";
import {
  composeActorFrameIndex,
  loadActor,
  uiActorResourceForJobSex,
} from "../resources/actor";
import { fetchResourceBytes } from "../resources/loader";
import { lookupSMapAlias } from "../resources/smap";

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
 *   y342–363 soft: UIxia1 / UI7+挂机 / UIxia3
 *   y364–399 chat
 */
const ICON_FILES = ["UIsh1.gif", "UIsh2.gif", "UIsh3.gif", "UIsh4.gif", "UIsh6.gif"];
const ICON_ACTIONS: HudIconAction[] = ["status", "bag", "quest", "social", "mall"];

/**
 * Hangup softkey sits left of center so the wide chat/home zone is not
 * mistaken for 挂机 (mis-taps were common with a centered 100px hit box).
 */
const HANG_BTN_X = 48;
const HANG_BTN_W = 52;

/** Status / chrome slots (absolute LCD y inside top band). */
const STATUS_BG_Y = 28;
const STATUS_BG_H = 36; // → y63
const PORTRAIT_X = 4;
const PORTRAIT_Y = 35;
const PORTRAIT_INNER = 26;
const TIAO_X = 34;
const TIAO_Y = 38;
const ICON_X0 = 91;
const ICON_Y = 39;
const ICON_PITCH = 31; // 21px art + 10px gap
const EXP_Y = 63;
const SOFT_H = 22;
/** Bottom chat strip: ~14 CJK chars/line (UC3_CELL), 2 visual rows. */
const CHAT_WRAP_CHARS = 14;
const CHAT_VISIBLE_ROWS = 2;
const CHAT_LINE_H = UC3_CELL;
/** Party mates overlay map, stacked under EXP strip. */
const PARTY_Y0 = WORLD_TOP_H + 1;
const PARTY_SLOT_H = 30;
const PARTY_FACE = 22;
const PARTY_MAX = 3;

type PartySlot = {
  root: Container;
  portrait: Container;
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
  onIcon: ((action: HudIconAction) => void) | null = null;

  private locText: BitmapLabel;
  private portraitHost = new Container();
  private tiaoHost = new Container();
  private hpFill = new Graphics();
  private spFill = new Graphics();
  private iconSlots: Container[] = [];
  private expHost = new Container();
  private expFill: Sprite | null = null;
  private expDigits: DigitStrip | null = null;
  private hangLabel: BitmapLabel;
  private chatLines: BitmapLabel[] = [];
  private chatLog: string[] = [];
  /** Longer history for ChatPanel (HUD still shows 2 lines). */
  private chatEntries: { type: number; text: string }[] = [];
  private chatHistory: string[] = [];
  private artReady = false;
  private menuHit!: Container;
  private hangHit!: Container;
  private homeHit!: Container;
  private partyHost = new Container();
  private partySlots: PartySlot[] = [];
  private partyTiao: Texture | null = null;

  /**
   * Dream 29* instance countdown (CBE local timer; no WT packet).
   * Paint wall-clock remaining as MM:SS (ttl 3600000 → 60:00, ticks every second).
   * Evidence: mock dreamHudTtl + docs/re/2026-08-07-pixi-dream-hud-countdown.md.
   */
  private dreamTimerHost = new Container();
  private dreamTimerIcon: Sprite | null = null;
  private dreamTimerDigits: DigitStrip | null = null;
  private dreamExpireAtMs = 0;
  private dreamTimerTick: ReturnType<typeof setInterval> | null = null;
  private dreamTtlMsCached: number | null = null;
  private dreamSceneKey = "";

  /** Center toast for「修」buff list / short world tips. */
  private tipHost = new Container();
  private tipBox = new Graphics();
  private tipLabels: BitmapLabel[] = [];
  private tipText = "";
  private tipUntil = 0;
  private tipTimer: ReturnType<typeof setTimeout> | null = null;

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
    this.tipHost.eventMode = "none";
    this.tipHost.zIndex = 100;
    this.tipHost.addChild(this.tipBox);
    this.tipHost.visible = false;
    this.addChild(this.top, this.bottom, this.tipHost);
    this.bottom.y = LCD_HEIGHT - WORLD_BOTTOM_H;
    this.bottom.eventMode = "static";

    // Location floats on deep/map tint; status is the opaque olive band.
    this.top.addChild(
      new Graphics().rect(0, 0, LCD_WIDTH, STATUS_BG_Y).fill({ color: CBE.bgDeep }),
    );
    this.top.addChild(
      new Graphics()
        .rect(0, STATUS_BG_Y, LCD_WIDTH, STATUS_BG_H)
        .fill({ color: CBE.bgHudRow }),
    );
    this.top.addChild(
      new Graphics()
        .rect(0, STATUS_BG_Y + STATUS_BG_H, LCD_WIDTH, WORLD_TOP_H - STATUS_BG_Y - STATUS_BG_H)
        .fill({ color: CBE.bgDeep }),
    );

    const bannerGfx = new Graphics();
    this.top.addChild(bannerGfx);
    this.locText = drawLocBanner(bannerGfx, this.top, "场景", 2);

    // Gold portrait frame (handkuang.gif is item-slot, not face frame)
    this.top.addChild(
      new Graphics()
        .rect(PORTRAIT_X - 1, PORTRAIT_Y - 1, PORTRAIT_INNER + 2, PORTRAIT_INNER + 2)
        .fill({ color: 0x201810 })
        .stroke({ width: 2, color: 0xd4a820 }),
    );
    this.portraitHost.position.set(PORTRAIT_X, PORTRAIT_Y);
    this.top.addChild(this.portraitHost);
    this.top.addChild(drawCrown(PORTRAIT_X + 1, PORTRAIT_Y - 6));

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
    this.bottom.addChild(
      new Graphics().rect(0, 0, LCD_WIDTH, SOFT_H).fill({ color: CBE.bgPanel }),
    );
    const softFx = new Graphics();
    softFx
      .moveTo(2, 4)
      .lineTo(2, 2)
      .lineTo(14, 2)
      .stroke({ width: 1, color: CBE.borderBright });
    softFx
      .moveTo(LCD_WIDTH - 2, 4)
      .lineTo(LCD_WIDTH - 2, 2)
      .lineTo(LCD_WIDTH - 14, 2)
      .stroke({ width: 1, color: CBE.borderBright });
    this.bottom.addChild(softFx);

    this.bottom.addChild(
      new Graphics()
        .rect(0, SOFT_H, LCD_WIDTH, WORLD_BOTTOM_H - SOFT_H)
        .fill({ color: CBE.chatBg }),
    );

    this.hangLabel = makePixelText("挂机", 12, 0xffffff);
    this.hangLabel.anchor.set(0.5, 0.5);
    this.bottom.addChild(this.hangLabel);

    for (let i = 0; i < CHAT_VISIBLE_ROWS; i++) {
      const line = new BitmapLabel();
      line.position.set(4, SOFT_H + 2 + i * CHAT_LINE_H);
      this.bottom.addChild(line);
      this.chatLines.push(line);
    }

    this.menuHit = hitArea(2, 2, 26, 18);
    this.hangHit = hitArea(HANG_BTN_X, 0, HANG_BTN_W, SOFT_H);
    this.homeHit = hitArea(LCD_WIDTH - 28, 2, 26, 18);
    this.bottom.addChild(this.menuHit, this.hangHit, this.homeHit);
    this.menuHit.on("pointertap", () => this.onMenu?.());
    this.hangHit.on("pointertap", () => this.onHangup?.());
    this.homeHit.on("pointertap", () => this.onHome?.());

    this.setVitals(500, 500, 200, 200);
    this.pushChat("[系]系统: 欢迎进入江湖世界");

    this.partyHost.position.set(0, PARTY_Y0);
    this.partyHost.visible = false;
    this.addChild(this.partyHost);

    // Top-right of location band; hidden until dream enter.
    this.dreamTimerHost.visible = false;
    this.dreamTimerHost.position.set(LCD_WIDTH - 54, 6);
    this.top.addChild(this.dreamTimerHost);
  }

  async mountArt(): Promise<void> {
    if (this.artReady) return;
    this.artReady = true;

    const tiao = await tryLoadUiTexture("tiao.gif");
    if (tiao) {
      this.tiaoHost.addChildAt(uiSprite(tiao), 0);
      this.partyTiao = tiao;
    }

    for (let i = 0; i < 5; i++) {
      const tex = await tryLoadUiTexture(ICON_FILES[i]!);
      if (!tex) continue;
      const slot = this.iconSlots[i]!;
      slot.removeChildren().forEach((c) => c.destroy());
      slot.addChild(uiSprite(tex));
    }

    // uiexp 39×13 left badge; level digits in pocket; exp0/exp1 track
    const uiexp = await tryLoadUiTexture("uiexp.gif");
    if (uiexp) this.expHost.addChild(uiSprite(uiexp));
    this.expDigits = await DigitStrip.expNum();
    if (this.expDigits) {
      this.expHost.addChild(this.expDigits.root);
      // Pocket sits in left of uiexp; cell 5×6
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
    }
    const hangBg = await tryLoadUiTexture("UI7.gif");
    const hangX = HANG_BTN_X;
    if (hangBg) {
      const spr = uiSprite(hangBg);
      spr.eventMode = "none";
      spr.position.set(hangX, 0);
      this.bottom.addChild(spr);
    }
    const swords = drawCrossedSwords(hangX + 4, 3);
    swords.eventMode = "none";
    this.bottom.addChild(swords);
    this.hangLabel.eventMode = "none";
    this.hangLabel.position.set(hangX + 32, SOFT_H / 2);
    this.bottom.addChild(this.hangLabel);

    const home = await tryLoadUiTexture("UIxia3.gif");
    if (home) {
      const spr = uiSprite(home);
      spr.eventMode = "none";
      spr.position.set(LCD_WIDTH - 27, 3);
      this.bottom.addChild(spr);
    }
    // Art mounts after hit targets — re-raise so pointer reaches softkeys.
    this.bottom.addChild(this.menuHit, this.hangHit, this.homeHit);

    await this.mountDreamTimerArt();
    this.setLevel(1, 0.1);
  }

  async setPortraitActor(actorName: string): Promise<void> {
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
      this.portraitHost.removeChildren().forEach((c) => c.destroy({ children: true }));
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
   * Arm / keep / clear dream HUD countdown from the visible scene key.
   * Same-scene identity refresh (称号) keeps the clock; outdoor clears;
   * entering a different 29* leaf re-arms (matches server TTL arm on enter).
   */
  async syncDreamTimerForScene(scene: string): Promise<void> {
    if (!isDreamInstanceScene(scene)) {
      this.dreamSceneKey = "";
      this.clearDreamTimer();
      return;
    }
    const key = dreamSceneLeaf(scene);
    if (
      this.dreamExpireAtMs > Date.now() &&
      this.dreamSceneKey === key &&
      this.dreamTimerHost.visible
    ) {
      this.paintDreamTimer();
      return;
    }
    this.dreamSceneKey = key;
    const ttlMs = await this.loadDreamHudTtlMs();
    this.dreamExpireAtMs = Date.now() + ttlMs;
    this.dreamTimerHost.visible = true;
    this.paintDreamTimer();
    if (this.dreamTimerTick == null) {
      this.dreamTimerTick = setInterval(() => this.paintDreamTimer(), 1000);
    }
  }

  clearDreamTimer(): void {
    this.dreamExpireAtMs = 0;
    this.dreamSceneKey = "";
    this.dreamTimerHost.visible = false;
    if (this.dreamTimerTick != null) {
      clearInterval(this.dreamTimerTick);
      this.dreamTimerTick = null;
    }
  }

  private async mountDreamTimerArt(): Promise<void> {
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
    if (!this.dreamTimerHost.visible || this.dreamExpireAtMs <= 0) return;
    const remainMs = Math.max(0, this.dreamExpireAtMs - Date.now());
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

  private layoutDreamTimer(): void {
    const w = this.dreamTimerHost.width || 50;
    this.dreamTimerHost.position.set(Math.max(4, LCD_WIDTH - w - 4), 6);
  }

  setVitals(hp: number, maxHp: number, mp: number, maxMp: number): void {
    const maxW = WorldHud.FILL_W;
    const hpW = Math.max(0, Math.min(maxW, Math.floor((maxW * hp) / Math.max(1, maxHp))));
    const spW = Math.max(0, Math.min(maxW, Math.floor((maxW * mp) / Math.max(1, maxMp))));
    const x = WorldHud.FILL_X;
    this.hpFill.clear().rect(x, WorldHud.HP_Y, hpW, WorldHud.FILL_H).fill({ color: 0xe03828 });
    this.spFill.clear().rect(x, WorldHud.SP_Y, spW, WorldHud.FILL_H).fill({ color: 0x38a0e0 });
  }

  /**
   * Show teammate vitals under self status (map overlay).
   * Pass empty / solo roster to hide (leave team).
   */
  setParty(mates: HudPartyMate[]): void {
    const list = mates.slice(0, PARTY_MAX);
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
  }

  private makePartySlot(index: number): PartySlot {
    const root = new Container();
    root.y = index * PARTY_SLOT_H;

    const frame = new Graphics()
      .rect(PORTRAIT_X - 1, 0, PARTY_FACE + 2, PARTY_FACE + 2)
      .fill({ color: 0x201810 })
      .stroke({ width: 1, color: 0xd4a820 });
    root.addChild(frame);

    const portrait = new Container();
    portrait.position.set(PORTRAIT_X, 1);
    root.addChild(portrait);

    const crown = drawCrown(PORTRAIT_X + 1, -5);
    crown.visible = false;
    root.addChild(crown);

    const tiaoHost = new Container();
    tiaoHost.position.set(TIAO_X, 2);
    if (this.partyTiao) tiaoHost.addChild(uiSprite(this.partyTiao));
    const hpFill = new Graphics();
    const spFill = new Graphics();
    tiaoHost.addChild(hpFill, spFill);
    root.addChild(tiaoHost);

    this.partyHost.addChild(root);
    return { root, portrait, crown, hpFill, spFill, mateId: 0 };
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
      slot.portrait.removeChildren().forEach((c) => c.destroy({ children: true }));
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
    this.expDigits?.setText(String(lv).padStart(2, "0"), 3, 3);
    const frac = Math.min(1, Math.max(0, exp01));
    // exp1.gif is 200×2; scale width by fraction
    if (this.expFill) this.expFill.width = Math.max(1, Math.floor(200 * frac));
  }

  /** Kept for session hooks; CBE main soft bar has no bag counter. */
  setBagSlots(_used: number, _cap: number): void {}

  /** Green center toast (增益列表 / short feedback). */
  showTip(text: string, ms = 1200): void {
    const msg = text.trim();
    if (!msg) return;
    this.tipText = msg;
    this.tipUntil = Date.now() + ms;
    if (this.tipTimer) clearTimeout(this.tipTimer);
    this.tipTimer = setTimeout(() => {
      this.tipTimer = null;
      if (Date.now() >= this.tipUntil) this.clearTip();
    }, ms + 20);
    void this.redrawTip();
  }

  private clearTip(): void {
    this.tipText = "";
    this.tipUntil = 0;
    if (this.tipTimer) {
      clearTimeout(this.tipTimer);
      this.tipTimer = null;
    }
    for (const t of this.tipLabels) t.destroy();
    this.tipLabels = [];
    this.tipBox.clear();
    this.tipHost.visible = false;
  }

  private async redrawTip(): Promise<void> {
    for (const t of this.tipLabels) t.destroy();
    this.tipLabels = [];
    this.tipBox.clear();
    if (!this.tipText || Date.now() >= this.tipUntil) {
      this.tipHost.visible = false;
      return;
    }
    const padFrame = 6;
    const padX = 10;
    const padY = 8;
    const lineH = 16;
    const maxInner = LCD_WIDTH - 28 - padFrame * 2;
    const textMax = Math.max(40, maxInner - padX * 2);
    const lines = wrapUiText(this.tipText, textMax);
    const boxH = padY * 2 + lines.length * lineH;
    const boxW = Math.min(
      maxInner,
      Math.max(96, ...lines.map((l) => measureUiTextWidth(l))) + padX * 2,
    );
    const bx = Math.floor((LCD_WIDTH - boxW) / 2) - padFrame;
    const by =
      Math.floor((LCD_HEIGHT - SOFTKEY_H) / 2 - boxH / 2) - padFrame;
    // Same opaque CBE panel as battle settlement.
    drawCbePanel(
      this.tipBox,
      bx,
      by,
      boxW + padFrame * 2,
      boxH + padFrame * 2,
      CBE.bgPanelDark,
    );
    const cx = Math.floor(LCD_WIDTH / 2);
    for (let i = 0; i < lines.length; i++) {
      const label = new BitmapLabel();
      label.anchor.set(0.5, 0);
      await label.setText(lines[i]!, CBE.inkTipGreen, { outline: false });
      label.position.set(cx, by + padFrame + padY + i * lineH);
      this.tipHost.addChild(label);
      this.tipLabels.push(label);
    }
    this.addChild(this.tipHost);
    this.tipHost.visible = true;
  }

  pushChat(line: string, type?: number): void {
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
    this.chatEntries.unshift({ type: t, text: line });
    this.chatEntries = this.chatEntries.slice(0, 80);
    this.chatHistory.unshift(line);
    this.chatHistory = this.chatHistory.slice(0, 80);
    this.chatLog = this.chatHistory.slice(0, 2);
    this.refreshChatStrip();
  }

  /** Newest messages → wrap ~14 CJK/line → paint first {@link CHAT_VISIBLE_ROWS} rows. */
  private refreshChatStrip(): void {
    const maxW = CHAT_WRAP_CHARS * UC3_CELL;
    const rows: { text: string; color: number }[] = [];
    for (const entry of this.chatEntries) {
      const raw = (entry.text ?? "").replace(/\s*\n\s*/g, " ").trim();
      if (!raw) continue;
      const color = colorForChatType(entry.type);
      for (const part of wrapUiText(raw, maxW, UC3_CELL)) {
        rows.push({ text: part, color });
        if (rows.length >= CHAT_VISIBLE_ROWS) break;
      }
      if (rows.length >= CHAT_VISIBLE_ROWS) break;
    }
    for (let i = 0; i < CHAT_VISIBLE_ROWS; i++) {
      const row = rows[i];
      void this.chatLines[i]!.setText(row?.text ?? "", row?.color ?? CBE.inkChat, {
        outline: false,
        wordWrap: false,
      });
    }
  }

  /** Oldest→newest lines for the chat-info menu panel. */
  getChatHistory(): string[] {
    return this.chatHistory.slice().reverse();
  }

  /** Oldest→newest typed entries for ChatPanel tabs. */
  getChatEntries(): { type: number; text: string }[] {
    return this.chatEntries.slice().reverse();
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

function drawCrossedSwords(x: number, y: number): Graphics {
  const g = new Graphics();
  g.position.set(x, y);
  g.moveTo(1, 1).lineTo(13, 13).stroke({ width: 2, color: 0xc8a060 });
  g.moveTo(13, 1).lineTo(1, 13).stroke({ width: 2, color: 0xa87840 });
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
