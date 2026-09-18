import {
  Container,
  Graphics,
  Rectangle,
  Sprite,
  Text,
  TextStyle,
  Texture,
} from "pixi.js";
import { replaceGraphics } from "../render/gpuResume";
import { CBE, LCD_HEIGHT, LCD_WIDTH, SOFTKEY_H } from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { uiFontFamily } from "../resources/systemFont";
import {
  FRUIT_BET_OPTIONS,
  FRUIT_CREDITS_PER_GOLD,
  FRUIT_MULTIPLE_OPTIONS,
  fruitWalletLabel,
  fruitWalletUnits,
  type FruitStatus,
  type FruitSpinResult,
} from "../net/protocolFruit";
import {
  clearPanelTip,
  createPanelTipState,
  drawPanelTipOverlay,
  showPanelTip,
  tryDismissPanelTipOnSoftRight,
  type PanelTipState,
} from "./panelTip";
import {
  fruitSfxSpinBegin,
  fruitSfxSpinEnd,
  fruitSfxSpinWheelStep,
  fruitSfxStopAll,
} from "../audio/FruitSfx";
import { suspendBgm, unlockAndResumeBgm } from "../audio/GameBgm";
import type { SoftkeyLabels } from "./SoftkeyBar";
import {
  FRUIT_BET_BTN_SCALE,
  FRUIT_BET_UI,
  FRUIT_BG_SCALE,
  FRUIT_HUD,
  FRUIT_ODDS_UI,
  FRUIT_UI_SCALE,
  FRUIT_WHEEL_SLOTS,
} from "./fruit/fruitUiLayout";
import {
  fruitAtlasTexture,
  layoutMachineBg,
  loadFruitUiAssets,
  type FruitUiAssets,
} from "./fruit/fruitUiAssets";
import { FruitDigitLabel } from "./fruit/fruitDigitLabel";
import { playFruitSpecialLighting } from "./fruit/fruitSpecialFx";
import { attachRowPointerHit, clearPointerHits } from "./pointerHit";

export type FruitPanelAction =
  | "insert"
  | "spin"
  | "collect"
  | "compare_lower"
  | "compare_bigger"
  | "cash_out"
  | "refresh"
  | "close";

type FruitMode = "play" | "insert" | "cashout";

const GAME_H = LCD_HEIGHT - SOFTKEY_H;

/**
 * 跑灯间隔：从头到尾连续减速（起步快 → 逐渐变慢）。
 * t^2 让前半段仍偏快，后半段减速更明显。
 */
function spinStepDelayMs(step: number, totalSteps: number): number {
  if (totalSteps <= 0) return 140;
  const t = Math.min(1, Math.max(0, step / totalSteps));
  const eased = t * t;
  const minMs = 16;
  const maxMs = 150;
  return Math.round(minMs + (maxMs - minMs) * eased);
}

function waitStep(ms: number): Promise<void> {
  if (ms <= 16) {
    return new Promise((r) => requestAnimationFrame(() => r()));
  }
  return new Promise((r) => window.setTimeout(r, ms));
}

export class FruitPanel extends Container {
  private scene = new Container();
  private highlight = new Sprite();
  /** 特殊牌型多格常亮（与跑马灯 highlight 分离）。 */
  private litOverlay = new Container();
  private litSprites: Sprite[] = [];
  private goBtn = new Sprite();
  private compareSmallBtn = new Sprite();
  private compareBigBtn = new Sprite();
  private pointLeftBtn = new Sprite();
  private pointRightBtn = new Sprite();
  private betSprites: Sprite[] = [];
  private creditLbl = new FruitDigitLabel();
  private bonusLbl = new FruitDigitLabel();
  private betCountLbls: FruitDigitLabel[] = [];
  private pointFrame = new Sprite();
  private pointDigits = new FruitDigitLabel();
  /** 投币 / 兑币共用遮罩 + 猜猜看风格数字键盘。 */
  private amountOverlay = new Container();
  private amountBox = new Graphics();
  private amountLabels: BitmapLabel[] = [];
  private amountHits: Container[] = [];
  private loadingLbl = new Text({
    text: "加载水果机…",
    style: new TextStyle({
      fill: 0xffffff,
      fontFamily: uiFontFamily(),
      fontSize: 12,
    }),
  });

  private ui: FruitUiAssets | null = null;
  private status: FruitStatus | null = null;
  private lastSpin: FruitSpinResult | null = null;
  private highlightPos = 0;
  /** 收分后熄灭跑马灯；开奖/旋转时再点亮。 */
  private showHighlight = false;
  private spinning = false;
  private mode: FruitMode = "play";
  private fruitCursor = 0;
  private multipleIndex = 0;
  private bets: Record<number, number> = {};
  /** 投币/兑币共用草稿（金或酷宝单位）。 */
  private amountDraft = "1";
  private toast: PanelTipState = createPanelTipState();
  onAction: ((action: FruitPanelAction) => void) | null = null;
  /** 切投币/兑币页时刷新外壳软键文案。 */
  onSoftkeysChange: (() => void) | null = null;

  constructor() {
    super();
    this.visible = false;
    this.eventMode = "static";
    this.hitArea = new Rectangle(0, 0, LCD_WIDTH, GAME_H);
    this.on("pointertap", (e) => e.stopPropagation());
    this.addChild(this.scene);
    this.scene.addChild(this.loadingLbl);
    this.loadingLbl.x = 72;
    this.loadingLbl.y = GAME_H / 2 - 8;
    this.scene.visible = false;
    this.highlight.anchor.set(0.5);
    this.highlight.visible = false;
    this.goBtn.eventMode = "static";
    this.goBtn.cursor = "pointer";
    this.goBtn.on("pointertap", (e) => {
      e.stopPropagation();
      if (!this.spinning && this.mode === "play") this.confirm();
    });
    this.compareSmallBtn.eventMode = "static";
    this.compareSmallBtn.cursor = "pointer";
    this.compareSmallBtn.on("pointertap", (e) => {
      e.stopPropagation();
      if ((this.status?.winChips ?? 0) > 0) this.onAction?.("compare_lower");
    });
    this.compareBigBtn.eventMode = "static";
    this.compareBigBtn.cursor = "pointer";
    this.compareBigBtn.on("pointertap", (e) => {
      e.stopPropagation();
      if ((this.status?.winChips ?? 0) > 0) this.onAction?.("compare_bigger");
    });
    this.pointLeftBtn.eventMode = "static";
    this.pointLeftBtn.cursor = "pointer";
    this.pointLeftBtn.on("pointertap", (e) => {
      e.stopPropagation();
      if (this.spinning || this.mode !== "play") return;
      this.cycleMultiple(-1);
    });
    this.pointRightBtn.eventMode = "static";
    this.pointRightBtn.cursor = "pointer";
    this.pointRightBtn.on("pointertap", (e) => {
      e.stopPropagation();
      if (this.spinning || this.mode !== "play") return;
      this.cycleMultiple(1);
    });
    this.amountOverlay.visible = false;
    this.addChild(this.toast.host);
    void this.ensureAssets();
  }

  private async ensureAssets(): Promise<void> {
    try {
      if (!this.ui) this.ui = await loadFruitUiAssets();
      this.buildScene();
      this.loadingLbl.visible = false;
      this.scene.visible = true;
      this.redraw();
    } catch (e) {
      console.warn("[fruit-ui] load failed", e);
      this.loadingLbl.text = "水果机资源加载失败";
      this.loadingLbl.visible = true;
      this.scene.visible = true;
      if (!this.scene.children.includes(this.loadingLbl)) {
        this.scene.addChild(this.loadingLbl);
      }
    }
  }

  private buildScene(): void {
    const ui = this.ui;
    if (!ui) return;
    // 只摘掉子节点，不 destroy 复用字段（goBtn/highlight/labels）
    this.scene.removeChildren();
    this.betSprites = [];
    this.betCountLbls = [];
    this.litSprites = [];
    this.litOverlay.removeChildren();

    const bg = new Sprite(ui.machineBg);
    const bgLayout = layoutMachineBg(ui.machineBg);
    bg.x = bgLayout.x;
    bg.y = bgLayout.y;
    bg.scale.set(bgLayout.scale);
    this.scene.addChild(bg);

    this.highlight.anchor.set(0.5);
    this.highlight.eventMode = "none";
    this.scene.addChild(this.highlight);
    this.litOverlay.eventMode = "none";
    this.scene.addChild(this.litOverlay);

    const btnScale = FRUIT_BET_BTN_SCALE;
    // GO / 比大小稍后叠在押注之上，避免图集公共画布透明区抢走点击

    const digitScale = FRUIT_UI_SCALE * (40 / 33);
    const pointScale = FRUIT_UI_SCALE * (36 / 33);

    this.bonusLbl.setFont(ui.digitFont);
    this.bonusLbl.x = FRUIT_HUD.bonusWin.x;
    this.bonusLbl.y = FRUIT_HUD.bonusWin.y;
    this.bonusLbl.scale.set(digitScale);
    this.bonusLbl.eventMode = "none";
    this.scene.addChild(this.bonusLbl);

    this.creditLbl.setFont(ui.digitFont);
    this.creditLbl.x = FRUIT_HUD.credit.x;
    this.creditLbl.y = FRUIT_HUD.credit.y;
    this.creditLbl.scale.set(digitScale);
    this.creditLbl.eventMode = "none";
    this.scene.addChild(this.creditLbl);

    // 中央数码管：dikuang 底框 + ziti_jp（原版 point）
    const dikuang = fruitAtlasTexture(ui.atlas, "dikuang.png");
    if (dikuang) {
      this.pointFrame.texture = dikuang;
      this.pointFrame.anchor.set(0.5);
      this.pointFrame.x = FRUIT_HUD.point.x;
      this.pointFrame.y = FRUIT_HUD.point.y;
      this.pointFrame.scale.set(FRUIT_UI_SCALE);
      this.pointFrame.eventMode = "none";
      this.scene.addChild(this.pointFrame);
    }
    this.pointDigits.setFont(ui.digitFont);
    this.pointDigits.x = FRUIT_HUD.point.x;
    this.pointDigits.y = FRUIT_HUD.point.y;
    this.pointDigits.scale.set(pointScale);
    this.pointDigits.eventMode = "none";
    this.pointDigits.show(this.multiple);
    this.scene.addChild(this.pointDigits);

    // 中间赔率条（两端 120/5 已在背景上）
    const oddsScale = FRUIT_BG_SCALE * 0.55;
    for (const odds of FRUIT_ODDS_UI) {
      const tex = fruitAtlasTexture(ui.atlas, odds.atlasKey);
      if (!tex) continue;
      const sp = new Sprite(tex);
      sp.anchor.set(0.5);
      sp.x = odds.x;
      sp.y = odds.y;
      sp.scale.set(oddsScale);
      sp.eventMode = "none";
      this.scene.addChild(sp);
    }

    for (const bet of FRUIT_BET_UI) {
      const countLbl = new FruitDigitLabel();
      countLbl.setFont(ui.digitFont);
      countLbl.scale.set(digitScale);
      countLbl.x = bet.countX;
      countLbl.y = bet.countY;
      countLbl.eventMode = "none";
      countLbl.show(0);
      this.scene.addChild(countLbl);
      this.betCountLbls.push(countLbl);

      // 背景底部是空槽，必须叠图集圆形押注键（不可做成透明 hit）
      const tex = fruitAtlasTexture(ui.atlas, bet.atlasKey);
      if (!tex) {
        console.warn(`[fruit-ui] missing bet atlas frame: ${bet.atlasKey}`);
      }
      const sp = new Sprite(tex ?? Texture.WHITE);
      sp.anchor.set(0.5);
      sp.x = bet.x;
      sp.y = bet.y;
      sp.scale.set(btnScale);
      sp.alpha = 1;
      sp.tint = 0xffffff;
      // uiAtlas 公共画布含侧灯(544高)，不收紧 hit 会盖住上方 GO
      sp.hitArea = new Rectangle(-30, -32, 59, 64);
      sp.eventMode = "static";
      sp.cursor = "pointer";
      const betId = bet.betId;
      sp.on("pointertap", (e) => {
        e.stopPropagation();
        if (this.spinning || this.mode !== "play") return;
        this.fruitCursor = FRUIT_BET_OPTIONS.findIndex((o) => o.id === betId);
        if (this.fruitCursor < 0) this.fruitCursor = 0;
        this.addBet();
      });
      this.scene.addChild(sp);
      this.betSprites.push(sp);
    }

    const goTex = fruitAtlasTexture(ui.atlas, "anniu_kaishi_01.png");
    if (goTex) {
      this.goBtn.texture = goTex;
      this.goBtn.anchor.set(0.5);
      this.goBtn.x = FRUIT_HUD.go.x;
      this.goBtn.y = FRUIT_HUD.go.y;
      this.goBtn.scale.set(btnScale);
      this.goBtn.hitArea = new Rectangle(-57, -32, 114, 63);
      this.scene.addChild(this.goBtn);
    }

    const cmpSmall = fruitAtlasTexture(ui.atlas, "anniu_1-7_01.png");
    if (cmpSmall) {
      this.compareSmallBtn.texture = cmpSmall;
      this.compareSmallBtn.anchor.set(0.5);
      this.compareSmallBtn.x = FRUIT_HUD.compareSmall.x;
      this.compareSmallBtn.y = FRUIT_HUD.compareSmall.y;
      this.compareSmallBtn.scale.set(btnScale);
      this.compareSmallBtn.hitArea = new Rectangle(-30, -32, 59, 64);
      this.scene.addChild(this.compareSmallBtn);
    }
    const cmpBig = fruitAtlasTexture(ui.atlas, "anniu_8-14_01.png");
    if (cmpBig) {
      this.compareBigBtn.texture = cmpBig;
      this.compareBigBtn.anchor.set(0.5);
      this.compareBigBtn.x = FRUIT_HUD.compareBig.x;
      this.compareBigBtn.y = FRUIT_HUD.compareBig.y;
      this.compareBigBtn.scale.set(btnScale);
      this.compareBigBtn.hitArea = new Rectangle(-30, -32, 59, 64);
      this.scene.addChild(this.compareBigBtn);
    }

    // 左右箭头（原版 pointLeft/Right；此处用于切换中间倍数）
    const leftTex = fruitAtlasTexture(ui.atlas, "anniu_zuojian_01.png");
    if (leftTex) {
      this.pointLeftBtn.texture = leftTex;
      this.pointLeftBtn.anchor.set(0.5);
      this.pointLeftBtn.x = FRUIT_HUD.pointLeft.x;
      this.pointLeftBtn.y = FRUIT_HUD.pointLeft.y;
      this.pointLeftBtn.scale.set(btnScale);
      this.pointLeftBtn.hitArea = new Rectangle(-40, -28, 80, 56);
      this.scene.addChild(this.pointLeftBtn);
    }
    const rightTex = fruitAtlasTexture(ui.atlas, "anniu_youjian_01.png");
    if (rightTex) {
      this.pointRightBtn.texture = rightTex;
      this.pointRightBtn.anchor.set(0.5);
      this.pointRightBtn.x = FRUIT_HUD.pointRight.x;
      this.pointRightBtn.y = FRUIT_HUD.pointRight.y;
      this.pointRightBtn.scale.set(btnScale);
      this.pointRightBtn.hitArea = new Rectangle(-40, -28, 80, 56);
      this.scene.addChild(this.pointRightBtn);
    }

    this.amountOverlay.removeChildren();
    clearPointerHits(this.amountHits);
    this.amountHits = [];
    this.amountLabels = [];
    this.amountBox = new Graphics();
    this.amountOverlay.addChild(this.amountBox);
    this.amountOverlay.visible = false;
    this.amountOverlay.eventMode = "static";
    this.amountOverlay.hitArea = new Rectangle(0, 0, LCD_WIDTH, GAME_H);
    this.amountOverlay.on("pointertap", (e) => e.stopPropagation());
    this.scene.addChild(this.amountOverlay);
  }

  open(status: FruitStatus | null): void {
    this.status = status;
    this.lastSpin = null;
    this.highlightPos = 0;
    this.showHighlight = false;
    this.spinning = false;
    this.mode = "play";
    this.fruitCursor = 0;
    this.multipleIndex = 0;
    this.bets = {};
    this.amountDraft = "1";
    this.clearLitOverlays();
    this.visible = true;
    // 与世界 BGM 错开：进机暂停 jh/ss/bgad，机内音效常开
    suspendBgm();
    void this.ensureAssets().then(() => this.redraw());
  }

  setStatus(status: FruitStatus | null): void {
    const hadWin = (this.status?.winChips ?? 0) > 0;
    this.status = status;
    const hasWin = (status?.winChips ?? 0) > 0;
    // 收分 / 比大小输光后熄灭常亮格与跑马灯高亮
    if (hadWin && !hasWin) {
      this.clearLitOverlays();
      this.showHighlight = false;
      this.lastSpin = null;
    }
    this.redraw();
  }

  setSpinResult(result: FruitSpinResult | null): void {
    this.lastSpin = result;
    if (result) {
      this.highlightPos = result.triggerPos;
      this.showHighlight = true;
    }
    this.spinning = false;
    // 开奖后清空押注显示（与原版 UN_START/initState 一致）
    this.bets = {};
    if (result && result.cardGroup.length > 0) {
      this.clearLitOverlays();
      for (const p of result.cardGroup) this.lightOn(p);
    } else {
      this.clearLitOverlays();
    }
    this.redraw();
  }

  async playSpinAnimation(
    targetPos: number,
    onFrame?: (pos: number) => void,
  ): Promise<void> {
    this.spinning = true;
    this.clearLitOverlays();
    this.showHighlight = true;
    fruitSfxSpinBegin();
    const start = ((this.highlightPos % 24) + 24) % 24;
    const totalSteps = 24 * 4 + ((targetPos - start + 24) % 24);
    try {
      for (let step = 0; step <= totalSteps; step++) {
        const pos = (start + step) % 24;
        this.highlightPos = pos;
        onFrame?.(pos);
        const delayMs = spinStepDelayMs(step, totalSteps);
        if (step < totalSteps) fruitSfxSpinWheelStep(delayMs);
        this.updateHighlightOnly();
        if (step < totalSteps) await waitStep(delayMs);
      }
      this.highlightPos = targetPos;
      fruitSfxSpinEnd();
      this.updateHighlightOnly();
    } finally {
      this.spinning = false;
    }
  }

  /** 特殊牌型送灯 / 交替换灯 / 开火车等。 */
  async playSpecialEffect(result: FruitSpinResult): Promise<void> {
    if (!this.ui) return;
    const type = result.cardGroupType || "SINGLE";
    if (type === "SINGLE" || type === "BONUS") {
      this.clearLitOverlays();
      this.highlightPos = result.triggerPos;
      this.showHighlight = true;
      this.updateHighlight();
      return;
    }
    this.spinning = true;
    this.showHighlight = true;
    try {
      await playFruitSpecialLighting(
        {
          turnOn: (pos) => this.lightOn(pos),
          turnOff: (pos) => this.lightOff(pos),
          turnOffAll: () => this.clearLitOverlays(),
          setCursor: (pos) => {
            this.highlightPos = pos;
            this.updateHighlight();
          },
        },
        result,
      );
    } finally {
      this.spinning = false;
    }
  }

  private ensureLitSprite(pos: number): Sprite | null {
    const ui = this.ui;
    if (!ui) return null;
    const slot = FRUIT_WHEEL_SLOTS[pos % 24];
    if (!slot) return null;
    let sp = this.litSprites[pos];
    if (!sp) {
      sp = new Sprite();
      sp.anchor.set(0.5);
      sp.eventMode = "none";
      this.litOverlay.addChild(sp);
      this.litSprites[pos] = sp;
    }
    const tex = fruitAtlasTexture(ui.atlas, slot.sprite);
    if (tex) sp.texture = tex;
    sp.x = slot.x;
    sp.y = slot.y;
    sp.scale.set(FRUIT_UI_SCALE);
    return sp;
  }

  private lightOn(pos: number): void {
    const sp = this.ensureLitSprite(pos);
    if (sp) sp.visible = true;
  }

  private lightOff(pos: number): void {
    const sp = this.litSprites[pos % 24];
    if (sp) sp.visible = false;
  }

  private clearLitOverlays(): void {
    for (const sp of this.litSprites) {
      if (sp) sp.visible = false;
    }
  }

  close(): void {
    clearPanelTip(this.toast);
    fruitSfxStopAll();
    this.visible = false;
    this.spinning = false;
    void unlockAndResumeBgm();
  }

  dismissTipOnSoftRight(): boolean {
    return tryDismissPanelTipOnSoftRight(this.toast);
  }

  showTip(text: string, ms = 1400): void {
    if (!this.visible) return;
    showPanelTip(this.toast, text, () => this.redraw(), ms);
  }

  get multiple(): number {
    return FRUIT_MULTIPLE_OPTIONS[this.multipleIndex] ?? 1;
  }

  get betsSnapshot(): Record<number, number> {
    return { ...this.bets };
  }

  get insertGold(): number {
    return this.parsedAmountGold("insert");
  }

  get cashOutGold(): number {
    return this.parsedAmountGold("cashout");
  }

  isInsertMode(): boolean {
    return this.mode === "insert";
  }

  isCashOutMode(): boolean {
    return this.mode === "cashout";
  }

  isAmountMode(): boolean {
    return this.mode === "insert" || this.mode === "cashout";
  }

  private creditsPerUnit(): number {
    return this.status?.creditsPerGold || FRUIT_CREDITS_PER_GOLD;
  }

  private amountLimits(kind: "insert" | "cashout"): { min: number; max: number } {
    if (kind === "insert") {
      const wallet = fruitWalletUnits(this.status);
      const min = this.status?.minInsertGold ?? 1;
      const maxCfg = this.status?.maxInsertGold ?? 999;
      return { min, max: Math.max(min, Math.min(maxCfg, Math.max(0, wallet))) };
    }
    const rate = this.creditsPerUnit();
    const max = Math.floor((this.status?.credits ?? 0) / Math.max(1, rate));
    return { min: 1, max: Math.max(0, max) };
  }

  private parsedAmountGold(kind: "insert" | "cashout"): number {
    const n = Math.floor(Number(this.amountDraft.trim() || "0"));
    if (!Number.isFinite(n) || n <= 0) return 0;
    const { min, max } = this.amountLimits(kind);
    if (max < min) return 0;
    return Math.min(max, Math.max(min, n));
  }

  private isAmountDraftValid(kind: "insert" | "cashout"): boolean {
    const n = Math.floor(Number(this.amountDraft.trim() || "0"));
    if (!Number.isFinite(n)) return false;
    const { min, max } = this.amountLimits(kind);
    return max >= min && n >= min && n <= max;
  }

  toggleInsertMode(): void {
    if (this.mode === "insert") {
      this.mode = "play";
    } else {
      this.mode = "insert";
      this.amountDraft = "1";
    }
    this.redraw();
    this.onSoftkeysChange?.();
  }

  toggleCashOutMode(): void {
    if (this.mode === "cashout") {
      this.mode = "play";
    } else {
      const { max } = this.amountLimits("cashout");
      if (max < 1) {
        this.showTip("游戏币不足，无法兑出");
        return;
      }
      this.mode = "cashout";
      this.amountDraft = String(Math.min(max, 1));
    }
    this.redraw();
    this.onSoftkeysChange?.();
  }

  closeAmountMode(): void {
    if (!this.isAmountMode()) return;
    this.mode = "play";
    this.redraw();
    this.onSoftkeysChange?.();
  }

  move(delta: number): void {
    if (this.spinning) return;
    if (this.isAmountMode()) return;
    const n = FRUIT_BET_OPTIONS.length;
    this.fruitCursor = (this.fruitCursor + delta + n) % n;
    this.redraw();
  }

  cycleMultiple(delta: number): void {
    if (this.spinning) return;
    if (this.isAmountMode()) return;
    const n = FRUIT_MULTIPLE_OPTIONS.length;
    this.multipleIndex = (this.multipleIndex + delta + n) % n;
    this.redraw();
  }

  addBet(): void {
    if (this.spinning || this.isAmountMode()) return;
    const id = FRUIT_BET_OPTIONS[this.fruitCursor]?.id;
    if (!id) return;
    const cost = this.multiple;
    const credits = this.status?.credits ?? 0;
    const cur = this.bets[id] ?? 0;
    if (credits < this.betCostTotal() + cost) {
      this.showTip("游戏币不足");
      return;
    }
    this.bets[id] = cur + 1;
    this.redraw();
  }

  clearBets(): void {
    if (this.spinning) return;
    this.bets = {};
    this.redraw();
  }

  appendAmountKey(key: string): void {
    if (!this.isAmountMode()) return;
    const kind = this.mode === "cashout" ? "cashout" : "insert";
    const { max } = this.amountLimits(kind);
    if (key === "删除" || key === "删") {
      this.amountDraft =
        this.amountDraft.length <= 1 ? "" : this.amountDraft.slice(0, -1);
      this.redraw();
      return;
    }
    if (key === "清空" || key === "清") {
      this.amountDraft = "";
      this.redraw();
      return;
    }
    if (!/^\d$/.test(key)) return;
    const merged =
      this.amountDraft === "0" || this.amountDraft === ""
        ? key
        : this.amountDraft + key;
    const n = Math.floor(Number(merged));
    if (!Number.isFinite(n) || n > Math.max(max, 999) || merged.length > 3) return;
    this.amountDraft = merged;
    this.redraw();
  }

  handleStakeKey(key: string): boolean {
    if (!this.visible || !this.isAmountMode()) return false;
    if (key === "Backspace") {
      this.appendAmountKey("删除");
      return true;
    }
    if (/^\d$/.test(key)) {
      this.appendAmountKey(key);
      return true;
    }
    return false;
  }

  betCostTotal(): number {
    let sum = 0;
    for (const opt of FRUIT_BET_OPTIONS) sum += (this.bets[opt.id] ?? 0) * this.multiple;
    return sum;
  }

  confirm(): void {
    if (this.spinning) return;
    if (this.mode === "insert") {
      if (!this.isAmountDraftValid("insert")) {
        const { min, max } = this.amountLimits("insert");
        this.showTip(max < min ? "余额不足" : `请输入${min}-${max}`);
        return;
      }
      this.onAction?.("insert");
      return;
    }
    if (this.mode === "cashout") {
      if (!this.isAmountDraftValid("cashout")) {
        const { min, max } = this.amountLimits("cashout");
        this.showTip(max < min ? "游戏币不足" : `请输入${min}-${max}`);
        return;
      }
      this.onAction?.("cash_out");
      return;
    }
    if ((this.status?.winChips ?? 0) > 0) {
      this.onAction?.("collect");
      return;
    }
    if (this.betCostTotal() <= 0) {
      this.showTip("请先押注");
      return;
    }
    // 开始前熄灭上一局常亮 / 跑马灯，避免叠在新一轮上
    this.lastSpin = null;
    this.clearLitOverlays();
    this.showHighlight = false;
    this.updateHighlight();
    this.onAction?.("spin");
  }

  softkeys(): SoftkeyLabels {
    if (this.mode === "insert") {
      return { left: "投币", right: "返回" };
    }
    if (this.mode === "cashout") {
      return { left: "兑币", right: "返回" };
    }
    if ((this.status?.winChips ?? 0) > 0) {
      return { left: "收分", right: "关闭" };
    }
    if (this.spinning) {
      return { left: "", right: "关闭" };
    }
    if (this.betCostTotal() > 0) {
      return { left: "开始", right: "关闭" };
    }
    return { left: "投币", right: "关闭" };
  }

  private updateHighlightOnly(): void {
    this.updateHighlight();
    this.updateHud();
  }

  private updateHighlight(): void {
    const ui = this.ui;
    if (!ui || !this.showHighlight) {
      this.highlight.visible = false;
      return;
    }
    const slot = FRUIT_WHEEL_SLOTS[this.highlightPos];
    if (!slot) {
      this.highlight.visible = false;
      return;
    }
    // 跑马灯 = 图集高亮水果格（bg 上是暗图），不是 light_big 倍率横条
    const light = fruitAtlasTexture(ui.atlas, slot.sprite);
    if (!light) {
      this.highlight.visible = false;
      return;
    }
    this.highlight.texture = light;
    this.highlight.x = slot.x;
    this.highlight.y = slot.y;
    // 与 Cocos fruitLights 子节点一致：约 84px 设计尺寸、scale≈1
    this.highlight.scale.set(FRUIT_UI_SCALE);
    this.highlight.visible = true;
  }

  private updateBetButtons(): void {
    const ui = this.ui;
    if (!ui) return;
    for (let i = 0; i < FRUIT_BET_UI.length; i++) {
      const bet = FRUIT_BET_UI[i]!;
      const count = this.bets[bet.betId] ?? 0;
      this.betCountLbls[i]?.show(count);
      const sp = this.betSprites[i];
      if (!sp) continue;
      const selected =
        FRUIT_BET_OPTIONS.findIndex((o) => o.id === bet.betId) === this.fruitCursor;
      const key = bet.atlasKey.replace("_01.png", selected ? "_02.png" : "_01.png");
      const tex = fruitAtlasTexture(ui.atlas, key);
      if (tex) sp.texture = tex;
      sp.alpha = 1;
      sp.tint = 0xffffff;
      sp.scale.set(FRUIT_BET_BTN_SCALE * (selected ? 1.05 : 1));
    }
  }

  private updateHud(): void {
    const st = this.status;
    this.creditLbl.show(st?.credits ?? 0);
    this.bonusLbl.show(st?.winChips ?? 0);
    this.pointDigits.show(this.multiple);
    const hasWin = (st?.winChips ?? 0) > 0;
    const amountMode = this.isAmountMode();
    this.compareSmallBtn.visible = hasWin && !amountMode;
    this.compareBigBtn.visible = hasWin && !amountMode;
    this.goBtn.visible = !hasWin && this.mode === "play";
    this.pointLeftBtn.visible = this.mode === "play";
    this.pointRightBtn.visible = this.mode === "play";
    this.redrawAmountOverlay();
  }

  private addAmountLabel(
    text: string,
    x: number,
    y: number,
    fill: number,
    opts?: { align?: "left" | "center" },
  ): void {
    const lbl = new BitmapLabel();
    lbl.show(text, fill);
    if (opts?.align === "center") {
      lbl.anchor.set(0.5, 0);
      lbl.x = x;
    } else {
      lbl.x = x;
    }
    lbl.y = y;
    this.amountOverlay.addChild(lbl);
    this.amountLabels.push(lbl);
  }

  /** 投币 / 兑币：对齐猜猜看押注数字键盘。 */
  private redrawAmountOverlay(): void {
    clearPointerHits(this.amountHits);
    this.amountHits = [];
    for (const lbl of this.amountLabels) {
      lbl.parent?.removeChild(lbl);
      lbl.destroy();
    }
    this.amountLabels = [];
    this.amountBox.clear();

    if (!this.isAmountMode()) {
      this.amountOverlay.visible = false;
      return;
    }

    const kind = this.mode === "cashout" ? "cashout" : "insert";
    const { min, max } = this.amountLimits(kind);
    const walletLbl = fruitWalletLabel(this.status?.currencyType ?? 0);
    const rate = this.creditsPerUnit();
    const draft = (this.amountDraft || "").trim() || "0";

    this.amountOverlay.visible = true;
    this.amountBox.rect(0, 0, LCD_WIDTH, GAME_H).fill({ color: 0x000000, alpha: 0.62 });

    const panelX = 16;
    const panelY = 48;
    const panelW = LCD_WIDTH - 32;
    const panelH = 260;
    this.amountBox
      .roundRect(panelX, panelY, panelW, panelH, 4)
      .fill({ color: CBE.bgDeep })
      .stroke({ width: 1, color: CBE.borderBright });

    // 顶栏切换投币 / 兑币（兑币确认走 OK，无单独软键）
    const tabY = panelY + 8;
    const tabW = Math.floor((panelW - 24) / 2);
    const tabs: { id: "insert" | "cashout"; label: string }[] = [
      { id: "insert", label: "投币" },
      { id: "cashout", label: "兑币" },
    ];
    for (let ti = 0; ti < tabs.length; ti++) {
      const tab = tabs[ti]!;
      const tx = panelX + 10 + ti * (tabW + 4);
      const active = kind === tab.id;
      const canOpen =
        tab.id === "insert" || this.amountLimits("cashout").max >= 1;
      this.amountBox
        .roundRect(tx, tabY, tabW, 18, 2)
        .fill({ color: active ? CBE.bgPanelDark : CBE.bgDeep })
        .stroke({
          width: 1,
          color: active ? CBE.borderBright : CBE.borderDim,
        });
      this.addAmountLabel(
        tab.label,
        tx + tabW / 2,
        tabY + 3,
        active ? CBE.ink : canOpen ? CBE.inkMint : CBE.inkLabel,
        { align: "center" },
      );
      if (canOpen) {
        this.amountHits.push(
          attachRowPointerHit(this.amountOverlay, tx, tabY, tabW, 18, () => {
            if (tab.id === "insert") {
              this.mode = "insert";
              if (!this.amountDraft) this.amountDraft = "1";
            } else {
              const { max: cashMax } = this.amountLimits("cashout");
              this.mode = "cashout";
              const n = Math.floor(Number(this.amountDraft || "0"));
              if (!Number.isFinite(n) || n < 1 || n > cashMax) {
                this.amountDraft = String(Math.min(cashMax, 1));
              }
            }
            this.redraw();
            this.onSoftkeysChange?.();
          }),
        );
      }
    }

    let cy = tabY + 24;
    if (kind === "insert") {
      this.addAmountLabel(
        `余额 ${fruitWalletUnits(this.status)}${walletLbl}`,
        panelX + 10,
        cy,
        CBE.inkLabel,
      );
      cy += 16;
      this.addAmountLabel(
        `1${walletLbl}=${rate}币 · 范围${min}-${max}`,
        panelX + 10,
        cy,
        CBE.inkLabel,
      );
    } else {
      this.addAmountLabel(
        `游戏币 ${this.status?.credits ?? 0}`,
        panelX + 10,
        cy,
        CBE.inkLabel,
      );
      cy += 16;
      this.addAmountLabel(
        `${rate}币=1${walletLbl} · 可兑${max}`,
        panelX + 10,
        cy,
        CBE.inkLabel,
      );
    }
    cy += 18;

    const boxH = 22;
    this.amountBox
      .roundRect(panelX + 10, cy, panelW - 20, boxH, 3)
      .fill({ color: CBE.bgPanel })
      .stroke({ width: 1, color: CBE.borderBright });
    this.addAmountLabel(
      `${draft} ${walletLbl}`,
      LCD_WIDTH / 2,
      cy + 4,
      CBE.inkTipGreen,
      { align: "center" },
    );
    cy += boxH + 8;

    const keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "删除", "0", "清空"];
    const rows = 4;
    const gap = 3;
    const bottom = panelY + panelH - 8;
    const avail = Math.max(48, bottom - cy);
    const keyH = Math.max(16, Math.min(22, Math.floor((avail - gap * (rows - 1)) / rows)));
    const keyW = 58;
    const gridW = keyW * 3 + gap * 2;
    const startX = panelX + Math.floor((panelW - gridW) / 2);
    for (let i = 0; i < keys.length; i++) {
      const col = i % 3;
      const row = Math.floor(i / 3);
      const kx = startX + col * (keyW + gap);
      const ky = cy + row * (keyH + gap);
      const key = keys[i]!;
      const keyInk =
        key === "清空" ? CBE.inkWarn : key === "删除" ? CBE.inkLabel : CBE.inkMint;
      this.amountBox
        .roundRect(kx, ky, keyW, keyH, 2)
        .fill({ color: CBE.bgPanelDark })
        .stroke({ width: 1, color: CBE.borderDim });
      this.addAmountLabel(key, kx + keyW / 2, ky + Math.floor((keyH - 12) / 2), keyInk, {
        align: "center",
      });
      this.amountHits.push(
        attachRowPointerHit(this.amountOverlay, kx, ky, keyW, keyH, () => {
          this.appendAmountKey(key);
        }),
      );
    }
  }

  /** webglcontextrestored: remount amount chrome / tip Graphics then redraw. */
  nudgeAfterGpuResume(): void {
    if (this.amountBox.parent) {
      this.amountBox = replaceGraphics(this.amountBox);
    }
    if (this.toast.gfx.parent) {
      this.toast.gfx = replaceGraphics(this.toast.gfx);
    }
    if (this.visible) this.redraw();
  }

  private redraw(): void {
    if (!this.ui) return;
    this.updateHighlight();
    this.updateBetButtons();
    this.updateHud();
    drawPanelTipOverlay(this, this.toast);
  }
}
