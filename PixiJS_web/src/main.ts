import { actorResourceForJobSex, uiActorResourceForJobSex } from "./resources/actor";
import { clearUc3TexCache, refreshAllUc3Labels } from "./ui/BitmapLabel";
import { applyUc3FontId, currentUc3FontId, loadUc3Font } from "./resources/uc3Font";
import { UC3_FONT_OPTIONS, type Uc3FontId } from "./resources/uiFontPreference";
import { Application, Graphics, Rectangle } from "pixi.js";
import { loadSceneIndex, loadSceneMap } from "./resources/loader";
import {
  defaultMonsterLevelsUrl,
  formatMonsterMapName,
  loadMonsterLevelOverrides,
} from "./resources/monsterLevels";
import {
  defaultNpcScenesUrl,
  loadNpcSceneIndex,
} from "./resources/npcScenes";
import { loadSMapAliases } from "./resources/smap";
import { defaultLiveGatewayUrl } from "./net/Transport";
import {
  createRechargeOrder,
  formatRechargeYuan,
  type RechargePayType,
} from "./net/recharge";
import { registerAccount } from "./net/register";
import {
  isClientUpdateRequiredError,
  promptDownloadLatestApk,
} from "./net/apkUpdate";
import { openPayUrl } from "./net/openPayUrl";
import { SceneView } from "./render/SceneView";
import { countPlus16Worn } from "./render/Plus16Sparkles";
import { poseTextureCacheStats } from "./render/poseTextureCache";
import { plistAtlasGpuStats } from "./resources/plistAtlas";
import {
  classifyForensic,
  copyForensicText,
  GlForensicRing,
  isAlertForensicClass,
  isGameDisplayPhase,
  isTextureSourceNullError,
  scanBrokenTextures,
  stripBrokenTextureSprites,
  type ForensicClass,
} from "./debug/glForensic";
import { GameSession } from "./game/GameSession";
import { PhoneShell } from "./ui/PhoneShell";
import { TitleScreen } from "./ui/TitleScreen";
import { RoleScreen } from "./ui/RoleScreen";
import { CreateRoleScreen } from "./ui/CreateRoleScreen";
import { WorldHud, sceneDisplayName } from "./ui/WorldHud";
import { MenuScreen, type MenuLeaf } from "./ui/MenuScreen";
import { BattleScreen, flushBattleSleepsForBackground } from "./ui/BattleScreen";
import { DialogPanel } from "./ui/DialogPanel";
import { ApproachPrompt } from "./ui/ApproachPrompt";
import { SystemPanel, type SystemKind } from "./ui/SystemPanel";
import { EnhancePanel } from "./ui/EnhancePanel";
import { WashPanel } from "./ui/WashPanel";
import { SocialPanel } from "./ui/SocialPanel";
import type {
  OtherEquipView,
  PlayerInfoView,
} from "./net/protocolSocial";
import { ArenaHud } from "./ui/ArenaHud";
import { ArenaPanel } from "./ui/ArenaPanel";
import { WagerPanel } from "./ui/WagerPanel";
import { LadderPanel } from "./ui/LadderPanel";
import { NPC_LADDER_CHALLENGE } from "./net/protocolLadder";
import { GuessPanel } from "./ui/GuessPanel";
import { FestivalPanel } from "./ui/FestivalPanel";
import { YanmenPanel } from "./ui/YanmenPanel";
import { RingQuestPanel } from "./ui/RingQuestPanel";
import {
  NPC_FESTIVAL_EXCHANGE,
  NPC_RING_QUEST,
  NPC_YANMEN_EXCHANGE,
  activityMsgTip,
} from "./net/protocolActivities";
import { AuctionPanel } from "./ui/AuctionPanel";
import {
  EntertainmentPanel,
  type EntertainmentEntry,
} from "./ui/EntertainmentPanel";
import { EntertainmentWebLayer } from "./ui/EntertainmentWebLayer";
import { WingPanel } from "./ui/WingPanel";
import { MountPanel } from "./ui/MountPanel";
import {
  ARENA_NPC_ENTER,
  ARENA_NPC_LEAVE,
  ARENA_NPC_RANK,
  ARENA_NPC_WAGER,
  arenaClaimFailTip,
  bracketForLevel,
} from "./net/protocolArena";
import {
  formatAuctionPrice,
  formatAuctionClaimTip,
  goldToCopper,
  type AuctionListing,
  type AuctionStatus,
} from "./net/protocolAuction";
import { TaskPanel } from "./ui/TaskPanel";
import {
  QuestTrackerOverlay,
  buildQuestTrackerRows,
} from "./ui/QuestTrackerOverlay";
import { isStatusStripCollapsed } from "./resources/statusStripPref";
import { ChatPanel, findArenaKillStreakTitle } from "./ui/ChatPanel";
import { TradePanel } from "./ui/TradePanel";
import { GuildPanel } from "./ui/GuildPanel";
import { MiniMapOverlay } from "./ui/MiniMapOverlay";
import { WorldMapPanel } from "./ui/WorldMapPanel";
import { TouchControls, type TouchDir } from "./ui/TouchControls";
import { CBE, LCD_HEIGHT, LCD_WIDTH, preloadCbeFlowerCorners } from "./ui/constants";
import { setLcdDisplayScale } from "./ui/BitmapLabel";
import {
  imeShellAdoptLock,
  imeShellCaptureLockFromWrap,
  imeShellClearLockSize,
  imeShellIsActive,
  imeShellLockedSize,
  forceShellWrapCenteringAnchors,
  repairShellViewportAlignment,
  resetShellViewportLayout,
  SHELL_WRAP_CENTER_TRANSFORM,
  shellViewportSnapshot,
} from "./ui/imeShell";
import { dismissOverlayIme } from "./ui/dismissOverlayIme";
import type { RoleRow } from "./net/protocol";
import { isRedNamePk, PK_POINTS_MAX } from "./net/protocol";
import {
  augmentEquipSellDialog,
  EquipSellBatchAction,
  EquipSellService,
  DREAM_BADGE_EQUIP_IDS,
  bagVisibleStackCount,
  decodeNpcBuyItemId,
  encodeNpcBuyItem,
  isBattleMedicineOrFlask,
  isCrystalFragment,
  isDreamBadge,
  NpcServiceOpcode,
  npcServiceOpcode,
} from "./net/protocolExtras";
import { isMapHealSkill } from "./resources/skill";
import {
  jobAllowsWeaponCategory,
  lookupItemSync,
  warmItemCatalogs,
  weaponJobRestrictTip,
} from "./resources/itemCatalog";
import {
  resumeBgmAfterForeground,
  suspendBgm,
  toggleSoundEnabled,
  unlockAndResumeBgm,
} from "./audio/GameBgm";
import { isPeerCosmeticsVisible, togglePeerCosmeticsVisible } from "./resources/peerCosmeticsPref";
import {
  playKillStreakTitle,
  preloadKillStreakSfx,
} from "./audio/KillStreakSfx";

/** Cap GPU buffer; HTML softkeys stay native DPI regardless. */
const LCD_RESOLUTION_CAP = 3;

function isNativeGl(): boolean {
  try {
    return !!(
      globalThis as { Capacitor?: { isNativePlatform?: () => boolean } }
    ).Capacitor?.isNativePlatform?.();
  } catch {
    return false;
  }
}

type Phase =
  | "splash"
  | "login"
  | "register"
  | "servers"
  | "role"
  | "create-role"
  | "world"
  | "menu"
  | "battle"
  | "dialog"
  | "system"
  | "enhance"
  | "wash"
  | "social"
  | "quest"
  | "chat"
  | "trade"
  | "guild"
  | "map";

/** Bottom-right map button: off → minimap → world map. */
type MapUiMode = "off" | "mini" | "world";

let touchStatus: TouchControls | null = null;
/** Routes player-facing game tips into the active LCD panel / HUD. */
let gameTipSink: ((text: string) => void) | null = null;

function setChromeStatus(text: string, error = false): void {
  touchStatus?.setStatus(text, error);
}

/** Game-level op feedback — panel toast / HUD, not HTML softkey chrome. */
function showGameTip(text: string): void {
  const msg = text.trim();
  if (!msg) return;
  if (gameTipSink) gameTipSink(msg);
  else touchStatus?.setStatus(msg, false);
}

/** Strip chat prefix and keep 比武胜/负/平/轮空 tip body for toast. */
function extractArenaResultTip(chatLine: string): string | null {
  const raw = (chatLine || "").trim();
  if (!raw) return null;
  const markers = ["比武轮空", "比武胜利", "比武失败", "比武平局"] as const;
  for (const m of markers) {
    const i = raw.indexOf(m);
    if (i >= 0) return raw.slice(i).replace(/^\]/, "").trim();
  }
  return null;
}

/** Phase tips from server 1/3/3 / [世] — show as floating game tip. */
function extractArenaPhaseTip(chatLine: string): string | null {
  const raw = (chatLine || "").trim();
  if (!raw) return null;
  const markers = [
    "比武擂台已开放进场",
    "比武比赛开始",
    "比武已结束",
    "比武领奖",
  ] as const;
  for (const m of markers) {
    const i = raw.indexOf(m);
    if (i >= 0) return raw.slice(i).replace(/^\]/, "").trim();
  }
  return null;
}

/** Map server auction/wager short codes to Chinese panel tips. */
function auctionMsgTip(code: string | undefined, okText: string): string {
  const m = (code || "").trim();
  if (!m || m === "ok") return okText;
  const map: Record<string, string> = {
    disabled: "拍卖未开放",
    "no-role": "角色无效",
    "bad-args": "参数错误",
    currency: "货币类型无效",
    "no-item": "背包无此物品",
    limit: "每人最多同时上架2件",
    "global-limit": "拍卖行已满（全服最多50件）",
    fee: "手续费不足",
    consume: "扣物品失败",
    save: "保存失败",
    encode: "编码失败",
    insert: "写入失败",
    "not-open": "该拍品已结束",
    self: "不能竞拍自己的物品",
    "self-account": "不能竞拍同账号上架",
    "already-high": "你已是最高出价",
    low: "出价过低",
    balance: "余额不足",
    update: "更新失败",
    conflict: "冲突，请重试",
    tx: "事务失败",
    refund: "退款登记失败",
    "bid-row": "出价写入失败",
    commit: "提交失败",
    "not-owner": "非卖家不能下架",
    "has-bid": "已有出价，无法下架",
    "bag-full": "背包已满，请清理后再试",
    "flask-full": "壶储量已满，请清理后再试",
    "buyout-low": "一口价须不低于起拍价",
    "no-buyout": "该拍品无一口价",
    "buyout-gone": "当前价已超一口价",
    payout: "卖家结算失败",
    "winner-item": "物品发放登记失败",
    nothing: "暂无待领物品",
    credit: "金钱入账失败",
    "bad-kind": "待领类型无效",
    fail: "领取失败",
  };
  return map[m] ?? `失败:${m}`;
}

/** Map server wager short codes to Chinese panel tips. */
function wagerMsgTip(code: string | undefined, okText: string): string {
  const m = (code || "").trim();
  if (!m || m === "ok") return okText;
  const map: Record<string, string> = {
    disabled: "赏金擂台未开放",
    "no-session": "角色未在线",
    "bad-stake": "押金须至少100金",
    "already-open": "你已有开放中的擂台",
    busy: "战斗或忙碌中无法参与",
    party: "队伍无效",
    "not-leader": "需队长操作",
    "member-offline": "队员不在线",
    "member-scene": "队员不在同场景",
    "member-busy": "队员忙碌中",
    "member-role": "队员角色无效",
    "no-role": "角色无效",
    money: "铜币不足",
    full: "擂台数量已满",
    db: "保存失败",
    "bad-id": "擂台编号无效",
    denied: "无权操作",
    "not-open": "该擂台不可挑战",
    scene: "须先赶到对方所在场景再应战",
    self: "不能挑战自己的擂台",
    size: "双方人数须相等",
    "not-await": "不在待确认状态",
    "not-host": "仅擂主方可确认",
    rejected: "已拒绝挑战",
    pending: "等待队友确认",
    fight: "开战",
    "start-fail": "开战失败，已退挑战方押金",
  };
  return map[m] ?? `失败:${m}`;
}

/** Map server guess short codes to Chinese panel tips. */
function guessMsgTip(code: string | undefined, okText: string): string {
  const m = (code || "").trim();
  if (!m || m === "ok") return okText;
  const map: Record<string, string> = {
    "no-role": "角色无效",
    "bad-choice": "选项无效",
    "bad-stake": "押金须1-999金",
    round: "轮次异常",
    locked: "开奖前30秒禁止押注",
    balance: "金币不足",
    encode: "编码失败",
    insert: "押注保存失败",
    nothing: "暂无可领奖励",
    update: "领取失败",
    conflict: "冲突，请重试",
  };
  return map[m] ?? `失败:${m}`;
}

async function main(): Promise<void> {
  const wrap = document.getElementById("phone-wrap");
  if (!wrap) throw new Error("missing #phone-wrap");
  const stage = document.getElementById("stage");
  if (!stage) throw new Error("missing #stage");

  const touchBar = document.getElementById("touch-bar");
  const touchChromeToggle = document.getElementById("touch-chrome-toggle");
  const TOUCH_CHROME_KEY = "pix_touch_chrome";
  const app = new Application();
  let rendererReady = false;
  const applyTouchChromeVisible = (visible: boolean) => {
    document.documentElement.classList.toggle("touch-chrome-off", !visible);
    if (touchChromeToggle) {
      touchChromeToggle.setAttribute(
        "aria-label",
        visible ? "隐藏方向键" : "显示方向键",
      );
      touchChromeToggle.title = visible
        ? "隐藏方向键（保留左软/OK/右软）"
        : "显示方向键";
    }
    try {
      localStorage.setItem(TOUCH_CHROME_KEY, visible ? "1" : "0");
    } catch {
      /* ignore */
    }
  };
  // Restore preference before first fit so stage height is correct.
  try {
    applyTouchChromeVisible(localStorage.getItem(TOUCH_CHROME_KEY) !== "0");
  } catch {
    applyTouchChromeVisible(true);
  }
  /**
   * Size HTML D-pad / softkeys from phone viewport:
   * - width → preferred button cell (larger phones → larger keys)
   * - reserve a usable chrome floor first; LCD may shrink below full-width
   *   on short screens (old leftover-only budget crushed keys to ~32px)
   * Driven by viewport, not stage (avoids feedback loops).
   */
  const syncTouchChromeScale = () => {
    if (!touchBar) return;
    const chromeOff =
      document.documentElement.classList.contains("touch-chrome-off") ||
      document.documentElement.classList.contains("ime-open") ||
      imeShellIsActive();
    const appEl = document.getElementById("app");
    const rawH = appEl?.clientHeight || window.innerHeight || 0;
    const rawW = appEl?.clientWidth || window.innerWidth || 0;
    if (rawH < 2 || rawW < 2) return;
    // #app uses border-box + safe-area padding — budget the content box.
    let padT = 0;
    let padB = 0;
    let padL = 0;
    let padR = 0;
    if (appEl) {
      const cs = getComputedStyle(appEl);
      padT = Number.parseFloat(cs.paddingTop) || 0;
      padB = Number.parseFloat(cs.paddingBottom) || 0;
      padL = Number.parseFloat(cs.paddingLeft) || 0;
      padR = Number.parseFloat(cs.paddingRight) || 0;
    }
    const vh = Math.max(1, rawH - padT - padB);
    const vw = Math.max(1, rawW - padL - padR);

    const clamp = (n: number, lo: number, hi: number) =>
      Math.max(lo, Math.min(hi, n));
    // Design reference: 360px-wide → 56px cells. Scale tracks phone size.
    const REF_CELL = 56;
    // ~48px is a usable touch target; never go back to the old 32px floor.
    const CELL_MIN = 48;
    const CELL_MAX = 80;
    const cellFromW = clamp(vw * (REF_CELL / 360), CELL_MIN, CELL_MAX);
    const centerOn = !!touchBar.querySelector("#sk-center.is-on");
    const chromeH = (cell: number, withStatus: boolean) => {
      const gap = cell * (8 / REF_CELL);
      const padY = 14 * (cell / REF_CELL);
      const statusH = withStatus ? 14 * (cell / REF_CELL) : 0;
      const centerH = centerOn ? 32 * (cell / REF_CELL) + 6 : 0;
      return 3 * cell + 2 * gap + padY + statusH + centerH;
    };

    const lcdByWidth = vw * (LCD_HEIGHT / LCD_WIDTH);
    const maxPadFrac = 0.42;
    const minChrome = chromeH(CELL_MIN, false);
    const leftover = vh - lcdByWidth;
    // Prefer leftover after a full-width LCD, but never starve below minChrome
    // (stage fit will shrink the LCD). Also cap so the bar cannot eat the screen.
    let padBudget =
      leftover > 0
        ? Math.min(Math.max(leftover, minChrome), vh * maxPadFrac)
        : Math.min(minChrome, vh * maxPadFrac);
    padBudget = Math.min(padBudget, vh * 0.52);
    padBudget = Math.max(padBudget, Math.min(minChrome, vh * 0.38));
    let withStatus = true;
    let cell = cellFromW;
    let h = chromeH(cell, true);
    if (padBudget > 1 && h > padBudget) {
      cell = clamp(cell * (padBudget / h), CELL_MIN, CELL_MAX);
      h = chromeH(cell, true);
      if (h > padBudget) {
        withStatus = false;
        h = chromeH(cellFromW, false);
        cell = clamp(
          cellFromW * (padBudget / Math.max(1, h)),
          CELL_MIN,
          CELL_MAX,
        );
      }
    } else if (padBudget > 1 && h < padBudget * 0.9) {
      // Tall leftover: grow keys a bit toward the budget (still ≤ CELL_MAX).
      const grown = cellFromW * (padBudget / h);
      cell = clamp(Math.min(grown, cellFromW * 1.35), CELL_MIN, CELL_MAX);
    }

    const scale = cell / REF_CELL;
    const fullChromeH = Math.max(1, Math.round(chromeH(cell, withStatus)));

    // Softkey side columns track width + scale.
    const skColMin = Math.round(clamp(vw * 0.16, 52, 80) * Math.min(1, scale));
    const skColMax = Math.round(clamp(vw * 0.22, 64, 96) * Math.min(1, scale));
    touchBar.style.setProperty("--sk-col-min", `${skColMin}px`);
    touchBar.style.setProperty("--sk-col-max", `${skColMax}px`);
    // Keep CSS bases at design reference; only --pad-scale varies.
    touchBar.style.removeProperty("--pad-cell-base");
    touchBar.style.removeProperty("--pad-gap-base");
    touchBar.style.removeProperty("--sk-h-base");
    touchBar.style.removeProperty("--sk-center-h-base");
    touchBar.style.removeProperty("--pad-cell");
    touchBar.style.removeProperty("--pad-gap");
    touchBar.style.removeProperty("--sk-h");
    touchBar.style.removeProperty("--sk-center-h");

    if (chromeOff) {
      // Hidden D-pad: softkey row band = half the full chrome height.
      const halfH = Math.max(48, Math.round(fullChromeH / 2));
      const btn = Math.min(52, Math.max(36, halfH - 14));
      // OK dominates the compact row — nearly fill the chrome band.
      const ok = Math.min(halfH - 2, Math.max(btn + 18, Math.round(halfH * 0.88)));
      touchBar.style.setProperty("--pad-scale", "1");
      touchBar.style.setProperty("--compact-chrome-h", `${halfH}px`);
      touchBar.style.setProperty("--compact-btn", `${btn}px`);
      touchBar.style.setProperty("--compact-ok", `${ok}px`);
      touchBar.classList.add("pad-compact");
      return;
    }

    touchBar.style.removeProperty("--compact-chrome-h");
    touchBar.style.removeProperty("--compact-btn");
    touchBar.style.removeProperty("--compact-ok");
    const next = (Math.round(scale * 1000) / 1000).toFixed(3);
    if (touchBar.style.getPropertyValue("--pad-scale") !== next) {
      touchBar.style.setProperty("--pad-scale", next);
    }
    touchBar.classList.toggle("pad-compact", !withStatus || scale < 0.88);
  };
  const syncTouchBarWidth = () => {
    // Full stage width so L/R softkeys sit beside ▲ and aren't clipped.
    if (touchBar) {
      const w = stage.clientWidth || wrap.clientWidth || 0;
      if (w > 2) touchBar.style.width = `${w}px`;
    }
    // Toggle lives inside #phone-wrap — keep CSS 8px inset (do not stage-offset).
    if (touchChromeToggle) {
      touchChromeToggle.style.top = "8px";
      touchChromeToggle.style.right = "8px";
      touchChromeToggle.style.left = "";
    }
  };
  const lcdCssScaleFromWrap = (): number => {
    const w = wrap.clientWidth || Number.parseFloat(wrap.style.width) || LCD_WIDTH;
    return Math.max(1, w / LCD_WIDTH);
  };
  const syncLcdResolution = (cssScale: number) => {
    setLcdDisplayScale(cssScale);
    if (!rendererReady) return;
    // UC3 / HUD share this backing store with the map — do not drop native
    // resolution or fonts bilinear-blur when CSS-scaled to the phone.
    const res = Math.min(LCD_RESOLUTION_CAP, Math.max(1, cssScale));
    if (Math.abs(app.renderer.resolution - res) < 0.05) return;
    app.renderer.resolution = res;
    app.renderer.resize(LCD_WIDTH, LCD_HEIGHT);
  };
  const fitPhoneWrap = () => {
    // Size chrome first so #stage height reflects the shrunk D-pad.
    syncTouchChromeScale();
    // CSS vars do not reflow until read — force before measuring #stage.
    if (touchBar) void touchBar.offsetHeight;
    const locked = imeShellLockedSize();
    if (locked) {
      wrap.style.width = `${locked.width}px`;
      wrap.style.height = `${locked.height}px`;
      syncTouchBarWidth();
      syncLcdResolution(locked.width / LCD_WIDTH);
      return;
    }
    let sw = stage.clientWidth;
    let sh = stage.clientHeight;
    if (sw < 2 || sh < 2) return;
    // Mobile URL-bar show/hide changes stage height and was resizing the LCD
    // CSS box mid-battle (attack/skill) — looks like the whole scene zooming.
    // Soft keyboard shrink is handled by imeShell lock instead.
    const prevW = Number(wrap.dataset.fitW || "0");
    const prevH = Number(wrap.dataset.fitH || "0");
    if (prevW > 0 && prevH > 0) {
      const dW = Math.abs(sw - prevW);
      const dH = Math.abs(sh - prevH);
      const heightJitterOnly = dW < 16 && dH < Math.max(64, prevH * 0.1);
      // While IME is arming (lock not yet captured), also ignore large
      // keyboard-driven height drops so we do not fit-to-shrunken stage.
      const imeKeyboardShrink =
        imeShellIsActive() && dW < 16 && sh < prevH && dH > 48;
      if (heightJitterOnly || imeKeyboardShrink) {
        syncTouchBarWidth();
        return;
      }
    }
    wrap.dataset.fitW = String(sw);
    wrap.dataset.fitH = String(sh);
    const s = Math.min(sw / LCD_WIDTH, sh / LCD_HEIGHT);
    wrap.style.width = `${Math.floor(LCD_WIDTH * s)}px`;
    wrap.style.height = `${Math.floor(LCD_HEIGHT * s)}px`;
    syncTouchBarWidth();
    syncLcdResolution(s);
  };
  fitPhoneWrap();
  if (touchChromeToggle) {
    touchChromeToggle.addEventListener("pointerdown", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      const show =
        document.documentElement.classList.contains("touch-chrome-off");
      applyTouchChromeVisible(show);
      wrap.dataset.fitW = "0";
      wrap.dataset.fitH = "0";
      fitPhoneWrap();
    });
  }
  window.addEventListener("resize", fitPhoneWrap);
  window.addEventListener("orientationchange", () => setTimeout(fitPhoneWrap, 50));
  window.addEventListener("ime-shell", () => {
    // Touch-bar hide expands stage — one fresh fit, then lock (never shrink).
    if (imeShellIsActive()) {
      const floor = imeShellLockedSize();
      // Suspend lock so fit can use the larger stage after #touch-bar hide.
      imeShellClearLockSize();
      wrap.dataset.fitW = "0";
      wrap.dataset.fitH = "0";
      fitPhoneWrap();
      imeShellCaptureLockFromWrap();
      if (floor) imeShellAdoptLock(floor.width, floor.height);
      fitPhoneWrap();
    } else {
      forceShellWrapCenteringAnchors();
      wrap.style.transform = SHELL_WRAP_CENTER_TRANSFORM;
      wrap.dataset.fitW = "0";
      wrap.dataset.fitH = "0";
      fitPhoneWrap();
    }
  });
  if (typeof ResizeObserver !== "undefined") {
    new ResizeObserver(fitPhoneWrap).observe(stage);
  }

  const bootScale = lcdCssScaleFromWrap();
  setLcdDisplayScale(bootScale);
  // Buffer resolution tracks CSS scale so UI Text ≈ HTML #sk-left sharpness.
  // Avoid resizeTo+lcd.scale coupling — skill FX texture churn was perceived as zoom.
  const nativeGl =
    typeof (globalThis as { Capacitor?: { isNativePlatform?: () => boolean } })
      .Capacitor?.isNativePlatform === "function" &&
    !!(globalThis as { Capacitor?: { isNativePlatform?: () => boolean } })
      .Capacitor?.isNativePlatform?.();
  // Pixi 8.9+: GCSystem (time-based). textureGC* still maps via TextureGCSystem shim.
  // Native WebView: reclaim idle GPU sooner. Displayed sprites keep sources "touched".
  await app.init({
    background: "#05070a",
    antialias: false,
    resolution: Math.min(LCD_RESOLUTION_CAP, Math.max(1, bootScale)),
    autoDensity: true,
    width: LCD_WIDTH,
    height: LCD_HEIGHT,
    // Native: integrated GPU is less likely to be power-gated than high-performance.
    powerPreference: isNativeGl() ? "low-power" : "high-performance",
    gcActive: true,
    gcMaxUnusedTime: 60_000,
    gcFrequency: nativeGl ? 15_000 : 30_000,
    textureGCActive: true,
    textureGCMaxIdle: nativeGl ? 3600 : 3600,
  });
  rendererReady = true;
  console.info(
    "[gl] TextureGC/GC",
    nativeGl ? "native" : "browser",
    "maxUnusedMs=",
    60_000,
    "freqMs=",
    nativeGl ? 15_000 : 30_000,
    "res=",
    Math.min(LCD_RESOLUTION_CAP, Math.max(1, bootScale)),
  );
  wrap.appendChild(app.canvas);
  // Keep chrome toggle above the canvas (append order + z-index).
  if (touchChromeToggle && touchChromeToggle.parentElement !== wrap) {
    wrap.appendChild(touchChromeToggle);
  } else if (touchChromeToggle) {
    wrap.appendChild(touchChromeToggle);
  }
  forceShellWrapCenteringAnchors();
  wrap.style.transform = SHELL_WRAP_CENTER_TRANSFORM;

  const shell = new PhoneShell(app);
  const touch = new TouchControls();
  touchStatus = touch;
  touch.onLayout = () => {
    wrap.dataset.fitW = "0";
    wrap.dataset.fitH = "0";
    fitPhoneWrap();
  };
  shell.onLabels = (labels) => touch.setSoftkeys(labels);
  shell.content.addChild(
    new Graphics().rect(0, 0, LCD_WIDTH, LCD_HEIGHT).fill({ color: CBE.bgDeep }),
  );

  // UC3 pack from settings (default font_16_sky.uc3).
  try {
    await loadUc3Font();
  } catch (e) {
    console.warn("[boot] uc3 font", e);
  }
  preloadCbeFlowerCorners();

  // server_monsters overrides via gateway → admin; builtin fallback on failure.
  await loadMonsterLevelOverrides(
    defaultMonsterLevelsUrl(defaultLiveGatewayUrl()),
  );
  // Dynamic NPC name→scene for quest tracker (server_dynamic_npcs).
  await loadNpcSceneIndex(defaultNpcScenesUrl(defaultLiveGatewayUrl()));

  const title = new TitleScreen();
  const roles = new RoleScreen();
  const createRole = new CreateRoleScreen();
  const hud = new WorldHud();
  const menu = new MenuScreen();
  const battle = new BattleScreen();
  const dialog = new DialogPanel();
  const approach = new ApproachPrompt();
  const system = new SystemPanel();
  const enhanceUi = new EnhancePanel();
  const washUi = new WashPanel();
  const social = new SocialPanel();
  const arenaHud = new ArenaHud();
  const arenaPanel = new ArenaPanel();
  const wagerPanel = new WagerPanel();
  const ladderPanel = new LadderPanel();
  const guessPanel = new GuessPanel();
  const festivalPanel = new FestivalPanel();
  const yanmenPanel = new YanmenPanel();
  const ringQuestPanel = new RingQuestPanel();
  const auctionPanel = new AuctionPanel();
  const entertainPanel = new EntertainmentPanel();
  const entertainWeb = new EntertainmentWebLayer();
  const wingPanel = new WingPanel();
  const mountPanel = new MountPanel();
  let auctionCache: {
    rows: AuctionListing[];
    mine: AuctionListing[];
    claims: import("./net/protocolAuction").AuctionClaimRow[];
    status: AuctionStatus | null;
    browsePage: number;
    minePage: number;
    claimPage: number;
    browseHasMore: boolean;
    mineHasMore: boolean;
    claimHasMore: boolean;
  } = {
    rows: [],
    mine: [],
    claims: [],
    status: null,
    browsePage: 0,
    minePage: 0,
    claimPage: 0,
    browseHasMore: false,
    mineHasMore: false,
    claimHasMore: false,
  };
  const taskUi = new TaskPanel();
  const questTracker = new QuestTrackerOverlay();
  const chatPanel = new ChatPanel();
  const tradeUi = new TradePanel();
  const guildUi = new GuildPanel();
  const miniMap = new MiniMapOverlay();
  const worldMap = new WorldMapPanel();
  battle.visible = false;
  menu.visible = false;
  roles.visible = false;
  createRole.visible = false;
  hud.visible = false;
  dialog.visible = false;
  approach.visible = false;
  system.visible = false;
  enhanceUi.visible = false;
  washUi.visible = false;
  social.visible = false;
  arenaHud.visible = false;
  arenaPanel.visible = false;
  wagerPanel.visible = false;
  ladderPanel.visible = false;
  guessPanel.visible = false;
  festivalPanel.visible = false;
  yanmenPanel.visible = false;
  ringQuestPanel.visible = false;
  auctionPanel.visible = false;
  entertainPanel.visible = false;
  wingPanel.visible = false;
  mountPanel.visible = false;
  taskUi.visible = false;
  chatPanel.visible = false;
  tradeUi.visible = false;
  guildUi.visible = false;
  worldMap.visible = false;

  const view = new SceneView(app);
  view.root.visible = false;
  (window as unknown as { __pixiView?: SceneView }).__pixiView = view;
  (window as unknown as { __pixiActorsDebug?: () => unknown }).__pixiActorsDebug =
    () => view.actors.debugPeerCosmetics();

  const syncWorldStatusBadges = () => {
    // 「斗」= hangup (vip_effect.gif). 「练」awaits training-buff tracking.
    view.setDouVisible(session.hangupActive || session.hangupDesired);
  };

  const refreshQuestTracker = () => {
    questTracker.setPartyInset(hud.partyOverlayInset());
    questTracker.setTasks(buildQuestTrackerRows(session.acceptedTasks));
    // Keep name→scene map fresh after admin moves (throttled inside loader).
    void loadNpcSceneIndex(defaultNpcScenesUrl(defaultLiveGatewayUrl())).then(
      () => {
        questTracker.setTasks(buildQuestTrackerRows(session.acceptedTasks));
      },
    );
  };

  shell.content.addChild(
    title,
    roles,
    createRole,
    view.root,
    hud,
    questTracker,
    miniMap,
    menu,
    battle,
    dialog,
    approach,
    system,
    enhanceUi,
    washUi,
    social,
    arenaHud,
    arenaPanel,
    wagerPanel,
    ladderPanel,
    guessPanel,
    festivalPanel,
    yanmenPanel,
    ringQuestPanel,
    auctionPanel,
    entertainPanel,
    wingPanel,
    mountPanel,
    taskUi,
    chatPanel,
    tradeUi,
    guildUi,
    worldMap,
  );
  shell.content.sortableChildren = true;
  arenaHud.zIndex = 40;
  dialog.zIndex = 45;
  approach.zIndex = 46;
  /* World overlays above NPC dialog / approach so taps cannot leak through. */
  arenaPanel.zIndex = 55;
  wagerPanel.zIndex = 55;
  ladderPanel.zIndex = 55;
  guessPanel.zIndex = 55;
  festivalPanel.zIndex = 55;
  yanmenPanel.zIndex = 55;
  ringQuestPanel.zIndex = 55;
  auctionPanel.zIndex = 55;
  entertainPanel.zIndex = 56;
  wingPanel.zIndex = 56;
  mountPanel.zIndex = 56;
  await title.mountArt();
  await roles.mountArt();
  await hud.mountArt();
  await worldMap.mountArt();
  await loadSMapAliases();

  gameTipSink = (msg) => {
    type TipHost = { visible: boolean; showTip: (t: string, ms?: number) => void };
    const hosts: TipHost[] = [
      worldMap,
      auctionPanel,
      wagerPanel,
      ladderPanel,
      guessPanel,
      festivalPanel,
      yanmenPanel,
      ringQuestPanel,
      arenaPanel,
      enhanceUi,
    washUi,
      system,
      social,
      guildUi,
      tradeUi,
      chatPanel,
      taskUi,
    ];
    for (const h of hosts) {
      if (h.visible) {
        h.showTip(msg);
        return;
      }
    }
    /* Hangup settle / battle: world HUD is hidden. */
    if (battle.visible) {
      battle.showTip(msg);
      return;
    }
    if (hud.visible) hud.showTip(msg);
  };

  const index = await loadSceneIndex();
  let phase: Phase = "splash";
  let pendingRoles: RoleRow[] = [];
  let activeRole: RoleRow | null = null;
  let mapUiMode: MapUiMode = "off";



  /** World overlays keep phase=world but must freeze map walk (bag uses phase=system). */
  const syncWorldWalkInput = () => {
    const overlayOpen =
      auctionPanel.visible ||
      arenaPanel.visible ||
      wagerPanel.visible ||
      ladderPanel.visible ||
      guessPanel.visible ||
      festivalPanel.visible ||
      yanmenPanel.visible ||
      ringQuestPanel.visible ||
      entertainPanel.visible ||
      wingPanel.visible ||
      mountPanel.visible ||
      entertainWeb.isExpanded();
    if (phase === "world" && !overlayOpen) {
      view.inputEnabled = true;
    } else {
      view.inputEnabled = false;
      view.clearHeldKeys();
    }
  };

  const openWagerPanel = async () => {
    dialog.hide();
    closeApproach(false);
    const board = await session.fetchWagerBoard();
    dialog.hide();
    closeApproach(false);
    view.setLeiBlink(false);
    wagerPanel.open(board.rows, board.mine, session.selectedRoleId);
    showPhase("world");
    syncWorldWalkInput();
    shell.setSoftkeys(wagerPanel.softkeys());
    setChromeStatus("赏金擂台");
    if (board.listResult === 0 && board.listMessage) {
      wagerPanel.showTip(wagerMsgTip(board.listMessage, "列表失败"));
    } else if (board.rows.length === 0) {
      wagerPanel.showTip("暂无擂台");
    }
  };

  const openAuctionPanel = async (opts?: {
    preferTab?: "browse" | "mine" | "list" | "claim";
    preselect?: import("./net/protocolExtras").BagItem;
    browsePage?: number;
    minePage?: number;
    claimPage?: number;
    /** 刷新时保留当前页码（出价/一口价后）. */
    preservePages?: boolean;
  }) => {
    if (!session.bagItems.length) {
      try {
        await session.openBag();
      } catch {
        /* ignore */
      }
    }
    const browsePage = opts?.preservePages
      ? (opts.browsePage ?? auctionPanel.listBrowsePage)
      : (opts?.browsePage ?? 0);
    const minePage = opts?.preservePages
      ? (opts.minePage ?? auctionPanel.listMinePage)
      : (opts?.minePage ?? 0);
    const claimPage = opts?.preservePages
      ? (opts.claimPage ?? auctionPanel.listClaimPage)
      : (opts?.claimPage ?? 0);
    const board = await session.fetchAuctionBoard({
      browsePage,
      minePage,
      claimPage,
    });
    auctionCache = board;
    view.setPaiBlink(!!board.status?.blink);
    auctionPanel.open(
      board.rows,
      board.mine,
      session.bagItems,
      board.status,
      opts?.preferTab,
      {
        browsePage: board.browsePage,
        minePage: board.minePage,
        claimPage: board.claimPage,
        browseHasMore: board.browseHasMore,
        mineHasMore: board.mineHasMore,
        claimHasMore: board.claimHasMore,
      },
      board.claims,
      session.selectedRoleId,
    );
    if (opts?.preselect) auctionPanel.preselectBagItem(opts.preselect);
    showPhase("world");
    syncWorldWalkInput();
    shell.setSoftkeys(auctionPanel.softkeys());
    setChromeStatus(
      board.status?.pendingClaims
        ? `拍卖 · 待领${board.status.pendingClaims}`
        : "物品竞拍",
    );
  };

  wagerPanel.onAction = (action, challengeId) => {
    const refresh = async (tip?: string) => {
      await openWagerPanel();
      if (tip) wagerPanel.showTip(tip);
    };
    if (action === "prev_page" || action === "next_page") {
      shell.setSoftkeys(wagerPanel.softkeys());
      return;
    }
    if (action === "close" || action === "noop") {
      if (action === "noop") {
        wagerPanel.showTip(
          wagerPanel.currentTab === "live"
            ? "进行中仅查看；待确认请选开战/拒绝"
            : "请选择可应战的擂台",
        );
        shell.setSoftkeys(wagerPanel.softkeys());
        return;
      }
      wagerPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      return;
    }
    if (action === "refresh") {
      void refresh();
      return;
    }
    if (action === "create") {
      void session.wagerCreate(wagerPanel.stake).then((r) => {
        const tip = r?.result
          ? `摆擂成功#${r.challengeId}`
          : wagerMsgTip(r?.message, "摆擂失败");
        void refresh(tip);
      });
      return;
    }
    if (action === "challenge" && challengeId) {
      void session.wagerChallenge(challengeId).then((r) => {
        const tip = r?.result
          ? wagerMsgTip(r.message, "已应战，等待擂主确认")
          : wagerMsgTip(r?.message, "应战失败");
        void refresh(tip);
      });
      return;
    }
    if (action === "confirm" && challengeId) {
      void session.wagerConfirm(challengeId, true).then((r) => {
        const tip = r?.result
          ? wagerMsgTip(r.message, "已确认应战")
          : wagerMsgTip(r?.message, "确认失败");
        void refresh(tip);
      });
      return;
    }
    if (action === "reject" && challengeId) {
      void session.wagerConfirm(challengeId, false).then((r) => {
        const tip = wagerMsgTip(r?.message, "已拒绝应战");
        void refresh(tip);
      });
      return;
    }
    if (action === "cancel" && challengeId) {
      void session.wagerCancel(challengeId).then((r) => {
        const tip = r?.result
          ? "已撤擂"
          : wagerMsgTip(r?.message, "撤擂失败");
        void refresh(tip);
      });
    }
  };

  auctionPanel.onSoftkeys = () => {
    if (auctionPanel.visible) shell.setSoftkeys(auctionPanel.softkeys());
  };
  auctionPanel.onAction = (action, listingId) => {
    const refresh = async (tip?: string) => {
      await openAuctionPanel({
        preferTab: auctionPanel.tab,
        preservePages: true,
      });
      if (tip) auctionPanel.showTip(tip);
    };
    if (action === "close") {
      auctionPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      return;
    }
    if (action === "refresh") {
      void refresh();
      return;
    }
    if (action === "prev_page" || action === "next_page") {
      const tab = auctionPanel.tab;
      let browse = auctionPanel.listBrowsePage;
      let mine = auctionPanel.listMinePage;
      let claim = auctionPanel.listClaimPage;
      if (tab === "browse") {
        browse =
          action === "next_page" ? browse + 1 : Math.max(0, browse - 1);
      } else if (tab === "mine") {
        mine = action === "next_page" ? mine + 1 : Math.max(0, mine - 1);
      } else if (tab === "claim") {
        claim = action === "next_page" ? claim + 1 : Math.max(0, claim - 1);
      } else {
        return;
      }
      const pageShown =
        tab === "mine" ? mine : tab === "claim" ? claim : browse;
      void openAuctionPanel({
        preferTab: tab,
        browsePage: browse,
        minePage: mine,
        claimPage: claim,
      }).then(() => {
        auctionPanel.showTip(`第${pageShown + 1}页`);
      });
      return;
    }
    if (action === "claim") {
      void session.auctionClaim().then((r) => {
        view.setPaiBlink(!!r?.blink);
        const tip = formatAuctionClaimTip(r, auctionMsgTip);
        showGameTip(tip);
        void session.openBag().then(() => refresh(tip));
      });
      return;
    }
    if (action === "confirm_list") {
      const p = auctionPanel.listParams;
      if (!p) {
        showGameTip("请选择背包物品");
        return;
      }
      void session.auctionCreate(p).then((r) => {
        const tip =
          r?.result === 1
            ? `上架成功#${r.listingId}`
            : auctionMsgTip(r?.message, "上架失败");
        void session.openBag().then(() => refresh(tip));
      });
      return;
    }
    if (action === "bid" && listingId) {
      const row =
        auctionCache.rows.find((x) => x.listingId === listingId) ||
        auctionCache.mine.find((x) => x.listingId === listingId);
      if (!row) {
        showGameTip("条目不存在");
        return;
      }
      const stepRaw = auctionCache.status?.minBidStep || 1;
      // 铜币档按金展示/加价：至少加 1 金（10000 铜）
      const step =
        row.currency === 0 ? Math.max(stepRaw, goldToCopper(1)) : stepRaw;
      const amount =
        row.highBidderRoleId > 0 ? row.currentPrice + step : row.currentPrice;
      void session.auctionBid(listingId, amount).then((r) => {
        const tip =
          r?.result === 1
            ? `出价${formatAuctionPrice(amount, row.currency)}成功`
            : auctionMsgTip(r?.message, "出价失败");
        void refresh(tip);
      });
      return;
    }
    if (action === "buyout_challenge" && listingId) {
      void session.auctionBuyoutChallenge(listingId).then((r) => {
        if (!r?.captcha) {
          auctionPanel.showTip(auctionMsgTip(r?.message, "获取验证码失败"));
          return;
        }
        auctionPanel.showBuyoutCaptcha(listingId, r.captcha);
      });
      return;
    }
    if (action === "buyout" && listingId) {
      const row =
        auctionCache.rows.find((x) => x.listingId === listingId) ||
        auctionCache.mine.find((x) => x.listingId === listingId);
      if (!row) {
        showGameTip("条目不存在");
        return;
      }
      if (!row.buyoutPrice) {
        showGameTip("无一口价");
        return;
      }
      const captcha = auctionPanel.getBuyoutCaptchaAnswer();
      void session.auctionBuyout(listingId, captcha).then((r) => {
        const tip =
          r?.result === 1
            ? `一口价${formatAuctionPrice(row.buyoutPrice, row.currency)}成交`
            : auctionMsgTip(r?.message, "一口价失败");
        void refresh(tip);
      });
      return;
    }
    if (action === "cancel" && listingId) {
      void session.auctionCancel(listingId).then((r) => {
        const tip =
          r?.result === 1 ? "已下架" : auctionMsgTip(r?.message, "下架失败");
        void session.openBag().then(() => refresh(tip));
      });
    }
  };

  const openArenaPanel = async (job?: number, bracket?: number) => {
    const j = job && job > 0 ? job : (activeRole ? activeRole.jobIndex + 1 : 1);
    const b =
      bracket && bracket > 0
        ? bracket
        : bracketForLevel(session.level || activeRole?.level || 1);
    /* Drop NPC dialog hits before/while rank fetch so taps cannot re-fire options. */
    dialog.hide();
    closeApproach(false);
    const page = await session.fetchArenaRank(j, b, 0);
    dialog.hide();
    closeApproach(false);
    arenaPanel.open(page, j, b);
    showPhase("world");
    syncWorldWalkInput();
    shell.setSoftkeys(arenaPanel.softkeys());
    setChromeStatus("比武排行榜");
  };
  arenaPanel.onBucketChange = (job, bracket) => {
    void openArenaPanel(job, bracket).then(() => {
      shell.setSoftkeys(arenaPanel.softkeys());
    });
  };
  arenaPanel.onAction = (action) => {
    if (action === "close") {
      arenaPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      return;
    }
    if (action === "leave") {
      void session.leaveArena().then(() => {
        arenaPanel.close();
        syncWorldWalkInput();
        shell.setSoftkeys({ left: "菜单", right: "菜单" });
        showGameTip("已离开比武擂台");
      });
      return;
    }
    if (action === "claim") {
      void session.claimArenaReward().then(async (r) => {
        const tip =
          r?.result === 1
            ? (r.tip?.trim() || "比武奖励已领取")
            : (r?.tip?.trim() || arenaClaimFailTip(r?.result));
        showGameTip(tip);
        await openArenaPanel(arenaPanel.job, arenaPanel.bracket);
        arenaPanel.showTip(tip);
        shell.setSoftkeys(arenaPanel.softkeys());
      });
    }
  };

  const openLadderPanel = async () => {
    dialog.hide();
    closeApproach(false);
    const status = await session.fetchLadderStatus();
    dialog.hide();
    closeApproach(false);
    ladderPanel.open(status);
    showPhase("world");
    syncWorldWalkInput();
    shell.setSoftkeys(ladderPanel.softkeys());
    setChromeStatus("天梯挑战");
    if (!status) ladderPanel.showTip("天梯状态获取失败");
  };
  ladderPanel.onAction = (action) => {
    if (action === "close") {
      ladderPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      return;
    }
    if (action === "challenge") {
      void session.startLadderChallenge().then((r) => {
        if (r.ok) {
          ladderPanel.close();
          syncWorldWalkInput();
          if (session.inBattle) showPhase("battle");
          return;
        }
        ladderPanel.showTip(r.tip || "挑战失败");
        shell.setSoftkeys(ladderPanel.softkeys());
      });
    }
  };

  let guessHistoryPage = 0;

  const refreshGuessPanel = async (page = guessHistoryPage) => {
    const board = await session.fetchGuessBoard(page);
    guessHistoryPage = board.pageIndex;
    if (guessPanel.visible) {
      guessPanel.setBoard(board.status, board.rows, board.pageIndex, board.hasMore);
      shell.setSoftkeys(guessPanel.softkeys());
    }
    return board;
  };

  const closeActivityPanels = () => {
    if (festivalPanel.visible) festivalPanel.close();
    if (yanmenPanel.visible) yanmenPanel.close();
    if (ringQuestPanel.visible) ringQuestPanel.close();
  };

  const openGuessPanel = async () => {
    dialog.hide();
    closeApproach(false);
    entertainPanel.close();
    if (auctionPanel.visible) auctionPanel.close();
    if (wagerPanel.visible) wagerPanel.close();
    if (ladderPanel.visible) ladderPanel.close();
    if (guessPanel.visible) guessPanel.close();
    if (arenaPanel.visible) arenaPanel.close();
    closeActivityPanels();
    if (entertainWeb.isExpanded()) entertainWeb.minimize();
    guessHistoryPage = 0;
    const board = await session.fetchGuessBoard(0);
    dialog.hide();
    closeApproach(false);
    guessHistoryPage = board.pageIndex;
    guessPanel.open(board.status, board.rows, board.pageIndex, board.hasMore);
    showPhase("world");
    syncWorldWalkInput();
    shell.setSoftkeys(guessPanel.softkeys());
    setChromeStatus("猜猜看");
    if (!board.status) guessPanel.showTip("状态获取失败");
  };

  const openFestivalPanel = async () => {
    dialog.hide();
    closeApproach(false);
    entertainPanel.close();
    if (auctionPanel.visible) auctionPanel.close();
    if (wagerPanel.visible) wagerPanel.close();
    if (ladderPanel.visible) ladderPanel.close();
    if (guessPanel.visible) guessPanel.close();
    if (arenaPanel.visible) arenaPanel.close();
    if (yanmenPanel.visible) yanmenPanel.close();
    if (ringQuestPanel.visible) ringQuestPanel.close();
    if (entertainWeb.isExpanded()) entertainWeb.minimize();
    const status = await session.fetchActivitiesStatus();
    dialog.hide();
    closeApproach(false);
    festivalPanel.open(status);
    showPhase("world");
    syncWorldWalkInput();
    shell.setSoftkeys(festivalPanel.softkeys());
    setChromeStatus("活力兑换");
    if (!status) festivalPanel.showTip("状态获取失败");
  };

  const openYanmenPanel = async () => {
    dialog.hide();
    closeApproach(false);
    entertainPanel.close();
    if (auctionPanel.visible) auctionPanel.close();
    if (wagerPanel.visible) wagerPanel.close();
    if (ladderPanel.visible) ladderPanel.close();
    if (guessPanel.visible) guessPanel.close();
    if (arenaPanel.visible) arenaPanel.close();
    if (festivalPanel.visible) festivalPanel.close();
    if (ringQuestPanel.visible) ringQuestPanel.close();
    if (entertainWeb.isExpanded()) entertainWeb.minimize();
    const status = await session.fetchActivitiesStatus();
    const rows = await session.fetchYanmenExchangeList();
    dialog.hide();
    closeApproach(false);
    yanmenPanel.open(status, rows);
    showPhase("world");
    syncWorldWalkInput();
    shell.setSoftkeys(yanmenPanel.softkeys());
    setChromeStatus("材料兑换");
    if (!status) yanmenPanel.showTip("状态获取失败");
  };

  const refreshRingQuestPanel = async () => {
    const status = await session.fetchRingStatus();
    if (ringQuestPanel.visible) {
      ringQuestPanel.open(status);
      shell.setSoftkeys(ringQuestPanel.softkeys());
    }
    return status;
  };

  const openRingQuestPanel = async () => {
    dialog.hide();
    closeApproach(false);
    entertainPanel.close();
    if (auctionPanel.visible) auctionPanel.close();
    if (wagerPanel.visible) wagerPanel.close();
    if (ladderPanel.visible) ladderPanel.close();
    if (guessPanel.visible) guessPanel.close();
    if (arenaPanel.visible) arenaPanel.close();
    if (festivalPanel.visible) festivalPanel.close();
    if (yanmenPanel.visible) yanmenPanel.close();
    if (entertainWeb.isExpanded()) entertainWeb.minimize();
    const status = await session.fetchRingStatus();
    dialog.hide();
    closeApproach(false);
    ringQuestPanel.open(status);
    showPhase("world");
    syncWorldWalkInput();
    shell.setSoftkeys(ringQuestPanel.softkeys());
    setChromeStatus("跑环任务");
    if (!status) ringQuestPanel.showTip("状态获取失败");
  };

  guessPanel.onAction = (action, betId) => {
    if (action === "close") {
      guessPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus("");
      return;
    }
    if (action === "refresh") {
      void refreshGuessPanel(guessHistoryPage).then((board) => {
        if (!board.status) guessPanel.showTip("刷新失败");
      });
      return;
    }
    if (action === "prev_page") {
      if (guessHistoryPage <= 0) return;
      guessHistoryPage -= 1;
      void refreshGuessPanel(guessHistoryPage).then(() => {
        guessPanel.showTip(`第${guessHistoryPage + 1}页`);
      });
      return;
    }
    if (action === "next_page") {
      guessHistoryPage += 1;
      void refreshGuessPanel(guessHistoryPage).then((board) => {
        if (board.rows.length === 0) {
          guessHistoryPage = Math.max(0, guessHistoryPage - 1);
          guessPanel.showTip("已是最后一页");
        } else {
          guessPanel.showTip(`第${guessHistoryPage + 1}页`);
        }
      });
      return;
    }
    if (action === "bet") {
      void session
        .guessBet(guessPanel.selectedChoice, guessPanel.stakeGoldAmount)
        .then(async (r) => {
          const tip =
            r?.result === 1
              ? guessMsgTip(r.message, "押注成功")
              : guessMsgTip(r?.message, "押注失败");
          await refreshGuessPanel(guessHistoryPage);
          guessPanel.showTip(tip);
          shell.setSoftkeys(guessPanel.softkeys());
        });
      return;
    }
    if (action === "claim") {
      void session.guessClaim(betId ?? 0).then(async (r) => {
        const tip =
          r?.result === 1
            ? guessMsgTip(r.message, betId ? "领取成功" : "已全部领取")
            : guessMsgTip(r?.message, "领取失败");
        await refreshGuessPanel(guessHistoryPage);
        guessPanel.showTip(tip);
        shell.setSoftkeys(guessPanel.softkeys());
      });
    }
  };

  festivalPanel.onAction = (action, exchangeId) => {
    if (action === "close") {
      festivalPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus("");
      return;
    }
    if (action === "exchange" && exchangeId != null) {
      void session.festivalExchange(exchangeId).then(async (r) => {
        const tip = !r
          ? "兑换失败"
          : r.result === 1
            ? activityMsgTip(r.message, "兑换成功")
            : activityMsgTip(r.message, "兑换失败");
        const status = await session.fetchActivitiesStatus();
        if (festivalPanel.visible) festivalPanel.open(status);
        festivalPanel.showTip(tip);
        shell.setSoftkeys(festivalPanel.softkeys());
      });
    }
  };

  yanmenPanel.onAction = (action, exchangeId) => {
    if (action === "close") {
      yanmenPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus("");
      return;
    }
    if (action === "exchange" && exchangeId != null) {
      void session.yanmenExchange(exchangeId).then(async (r) => {
        const tip = !r
          ? "兑换失败"
          : r.result === 1
            ? activityMsgTip(r.message, "兑换成功")
            : activityMsgTip(r.message, "兑换失败");
        const status = await session.fetchActivitiesStatus();
        const rows = await session.fetchYanmenExchangeList();
        if (yanmenPanel.visible) yanmenPanel.open(status, rows);
        yanmenPanel.showTip(tip);
        shell.setSoftkeys(yanmenPanel.softkeys());
      });
    }
  };

  ringQuestPanel.onAction = (action) => {
    if (action === "close") {
      ringQuestPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus("");
      return;
    }
    if (action === "accept") {
      void session.ringAccept().then(async (st) => {
        if (st) ringQuestPanel.open(st);
        else await refreshRingQuestPanel();
        ringQuestPanel.showTip(st ? "已接取环任务" : "接取失败");
        shell.setSoftkeys(ringQuestPanel.softkeys());
      });
      return;
    }
    if (action === "submit") {
      void session.ringSubmit().then(async (r) => {
        const tip = !r
          ? "提交失败"
          : r.result === 1
            ? activityMsgTip(r.message, "提交成功")
            : activityMsgTip(r.message, "提交失败");
        await refreshRingQuestPanel();
        ringQuestPanel.showTip(tip);
        shell.setSoftkeys(ringQuestPanel.softkeys());
      });
      return;
    }
    if (action === "claim") {
      void session.ringClaimDaily().then(async (r) => {
        const tip = !r
          ? "领取失败"
          : r.result === 1
            ? activityMsgTip(r.message, "领取成功")
            : activityMsgTip(r.message, "领取失败");
        await refreshRingQuestPanel();
        ringQuestPanel.showTip(tip);
        shell.setSoftkeys(ringQuestPanel.softkeys());
      });
    }
  };

  const showPhase = (p: Phase) => {
    phase = p;
    title.visible = p === "splash" || p === "login" || p === "register" || p === "servers";
    if (p === "splash") title.setMode("splash");
    else if (p === "login") title.setMode("login");
    else if (p === "register") title.setMode("register");
    else if (p === "servers") title.setMode("servers");
    else {
      // doLogin → role/world used to only set title.visible=false while mode
      // stayed "login" and the caret kept remounting LoginIme over the game.
      title.releaseIme();
    }
    if (p !== "create-role") createRole.releaseIme();
    roles.visible = p === "role";
    createRole.visible = p === "create-role";
    // Role delete-confirm ChatIme sits mid-LCD; leave role without cancel left it up.
    if (p !== "role") roles.cancelDeleteConfirm();
    // Chat deferred focus / create-role async redraw can remount HTML <input>
    // after the Pixi layer is hidden — always release when leaving those phases.
    if (p !== "chat") chatPanel.releaseIme();
    if (p !== "trade") tradeUi.releaseIme();
    // Battle/world etc.: kill leftover login/chat <input> focus (Android IME).
    // Keep trade while money ChatIme is open (same as chat compose).
    if (
      p !== "login" &&
      p !== "register" &&
      p !== "chat" &&
      p !== "create-role" &&
      p !== "trade"
    ) {
      dismissOverlayIme();
    }
    hud.visible =
      p === "world" ||
      p === "menu" ||
      p === "dialog" ||
      p === "system" ||
      p === "enhance" ||
      p === "wash" ||
      p === "social" ||
      p === "quest" ||
      p === "chat" ||
      p === "trade" ||
      p === "guild";
    // mmBattle replaces the world shell — hide map HUD while fighting.
    view.root.visible =
      p === "world" ||
      p === "menu" ||
      p === "dialog" ||
      p === "system" ||
      p === "enhance" ||
      p === "wash" ||
      p === "social" ||
      p === "quest" ||
      p === "chat" ||
      p === "trade" ||
      p === "guild" ||
      p === "map";
    menu.visible = p === "menu";
    battle.visible = p === "battle";
    dialog.visible = p === "dialog";
    system.visible = p === "system";
    enhanceUi.visible = p === "enhance";
    washUi.visible = p === "wash";
    social.visible = p === "social";
    taskUi.visible = p === "quest";
    if (p !== "quest") taskUi.hide();
    chatPanel.visible = p === "chat";
    if (p === "chat") chatPanel.selfRoleId = session.selectedRoleId >>> 0;
    tradeUi.visible = p === "trade";
    guildUi.visible = p === "guild";
    worldMap.visible = p === "map";
    miniMap.visible = mapUiMode === "mini" && (p === "world" || p === "map");
    questTracker.setWorldActive(p === "world");
    view.inputEnabled = p === "world";
    // Softkeys are HTML (#touch-bar); canvas SoftkeyBar stays hidden.
    if (p !== "world") {
      approach.hide();
      if (auctionPanel.visible) auctionPanel.close();
      if (wagerPanel.visible) wagerPanel.close();
      if (ladderPanel.visible) ladderPanel.close();
      if (guessPanel.visible) guessPanel.close();
      closeActivityPanels();
      if (entertainPanel.visible) entertainPanel.close();
      if (wingPanel.visible) wingPanel.close();
      if (mountPanel.visible) mountPanel.close();
      // Video/music HTML overlay stays up across battle / hangup / auto so
      // players can keep watching. Tear down only on auth / character screens.
      if (
        entertainWeb.isActive() &&
        (p === "splash" ||
          p === "login" ||
          p === "register" ||
          p === "servers" ||
          p === "role" ||
          p === "create-role")
      ) {
        entertainWeb.close();
      }
      // arenaPanel may stay for leave flow; walk still gated below
    }
    if (p === "world") {
      syncWorldWalkInput();
      syncWorldStatusBadges();
    }

    if (p === "splash") shell.setSoftkeys({ left: "确定", right: "" });
    else if (p === "login") shell.setSoftkeys({ left: "登录", right: "返回" });
    else if (p === "register") shell.setSoftkeys({ left: "注册", right: "返回" });
    else if (p === "servers") shell.setSoftkeys({ left: "继续", right: "返回" });
    else if (p === "role") {
      roles.cancelDeleteConfirm();
      shell.setSoftkeys({ left: "继续", right: "返回" });
    }
    else if (p === "create-role") shell.setSoftkeys({ left: "确定", right: "返回" });
    else if (p === "world") {
      if (approach.open) shell.setSoftkeys({ left: "确定", right: "取消" });
      else if (entertainPanel.visible) shell.setSoftkeys(entertainPanel.softkeys());
      else if (wingPanel.visible) shell.setSoftkeys(wingPanel.softkeys());
      else if (mountPanel.visible) shell.setSoftkeys(mountPanel.softkeys());
      else shell.setSoftkeys({ left: "菜单", right: "菜单" });
    }
    else if (p === "menu") {
      menu.reset();
      shell.setSoftkeys({ left: "确定", right: "返回" });
    }
    else if (p === "battle")
      shell.setSoftkeys(
        battle.isAwaitingRound() ||
          battle.isHangupActive() ||
          battle.isAutoBattleActive()
          ? { left: "", right: "" }
          : {
              left: battle.softkeyLeftLabel(),
              right: battle.softkeyRightLabel(),
            },
      );
    else if (p === "dialog") shell.setSoftkeys({ left: "确定", right: "返回" });
    else if (p === "system") shell.setSoftkeys({ left: "确定", right: "返回" });
    else if (p === "enhance") shell.setSoftkeys({ left: "确定", right: "返回" });
    else if (p === "wash") shell.setSoftkeys(washUi.softkeys());
    else if (p === "social") shell.setSoftkeys({ left: "确定", right: "返回" });
    else if (p === "quest") shell.setSoftkeys({ left: "确定", right: "返回" });
    else if (p === "chat") shell.setSoftkeys({ left: "发送", right: "返回" });
    else if (p === "trade") shell.setSoftkeys(tradeUi.softkeys());
    else if (p === "guild") shell.setSoftkeys({ left: "确定", right: "返回" });
    else if (p === "map") shell.setSoftkeys({ left: "确定", right: "返回" });
    // Expanded entertainment stays topmost — softkeys control the player.
    if (entertainWeb.isExpanded()) {
      shell.setSoftkeys({ left: "最小化", right: "关闭" });
    }
  };

  const cycleMapUi = () => {
    if (phase !== "world" && phase !== "map") return;
    if (mapUiMode === "off") {
      mapUiMode = "mini";
      miniMap.show();
      if (phase === "map") showPhase("world");
      else {
        miniMap.visible = true;
        const sz = view.getMapPixelSize();
        miniMap.update(
          view.getMapPreview(),
          sz.w,
          sz.h,
          view.playerX,
          view.playerY,
          view.liveMinimapMarkers(),
        );
        setChromeStatus("小地图");
      }
      return;
    }
    if (mapUiMode === "mini") {
      mapUiMode = "world";
      miniMap.hide();
      void worldMap.open(session.scene || "");
      showPhase("map");
      setChromeStatus("世界地图");
      return;
    }
    // world → off
    mapUiMode = "off";
    miniMap.hide();
    worldMap.hide();
    showPhase("world");
    setChromeStatus("地图关闭");
  };

  const enterSceneFile = async (name: string, x?: number, y?: number) => {
    const same = view.showsSceneFile(name);
    // Cross-map only: wiping monsters on the same shell would reset hangup
    // respawn timers and force another SCE compose.
    if (!same) {
      session.sceneMonsters = [];
      view.actors.setSceneMonsters([]);
    }
    const scene = await loadSceneMap(name);
    if (!same) {
      session.sceneMonsters = (scene.combatSpawns ?? []).map((s, i) => ({
        actorId: 0x70000000 + i,
        x: s.x,
        y: s.y,
        name: formatMonsterMapName(s.name, s.sceActorId),
        actorResource: s.mapActorResource,
        battleActorResource: s.battleActorResource,
        scriptName: `sce_combat_${s.sceActorId}`,
        battleActorId: s.sceActorId,
      }));
    }
    await view.showScene(scene, x, y);
  };

  /**
   * World boss inject/remove mutates live SCE while players stay on the map.
   * Compose cache would otherwise keep stale kind=3 rows.
   */
  let combatSpawnRefreshBusy = false;
  const parseWbossPos = (
    text: string,
  ): { x: number; y: number } | null => {
    const m =
      text.match(/pos=\((\d+),(\d+)\)/) ??
      text.match(/坐标\((\d+),(\d+)\)/);
    if (!m) return null;
    return { x: Number(m[1]) | 0, y: Number(m[2]) | 0 };
  };
  const isWbossDespawnLine = (text: string) =>
    text.includes("[WBOSS:despawn]") || text.includes("世界Boss 已刷新");
  const isWbossSpawnLine = (text: string) =>
    text.includes("[WBOSS:spawn]") ||
    (text.includes("【世界BOSS】") && text.includes("已出现"));
  const dropWorldBossFromMap = (reason: string, text: string) => {
    const pos = parseWbossPos(text);
    let n = 0;
    if (pos) {
      n = view.actors.removeSceneMonsterPermanent({ x: pos.x, y: pos.y });
    }
    session.sceneMonsters = view.actors.liveSceneMonsters();
    console.info(
      `[world-boss] map-drop reason=${reason} pos=${pos ? `${pos.x},${pos.y}` : "-"} removed=${n}`,
    );
    // Secondary: re-pull SCE; re-drop in case WT18 still served a stale row.
    void refreshSceneCombatSpawns(reason).then(() => {
      if (!pos) return;
      view.actors.removeSceneMonsterPermanent({ x: pos.x, y: pos.y });
      session.sceneMonsters = view.actors.liveSceneMonsters();
    });
  };
  const refreshSceneCombatSpawns = async (reason: string) => {
    const name = session.scene || "";
    if (!name || combatSpawnRefreshBusy) return;
    if (phase !== "world" && phase !== "battle") return;
    combatSpawnRefreshBusy = true;
    try {
      const scene = await loadSceneMap(name, { forceReload: true });
      if ((session.scene || "") !== name) return;
      session.sceneMonsters = (scene.combatSpawns ?? []).map((s, i) => ({
        actorId: 0x70000000 + i,
        x: s.x,
        y: s.y,
        name: formatMonsterMapName(s.name, s.sceActorId),
        actorResource: s.mapActorResource,
        battleActorResource: s.battleActorResource,
        scriptName: `sce_combat_${s.sceActorId}`,
        battleActorId: s.sceActorId,
      }));
      view.actors.setSceneMonsters(session.sceneMonsters);
      console.info(
        `[world-boss] combat-spawn refresh reason=${reason} count=${session.sceneMonsters.length}`,
      );
    } catch (e) {
      console.warn(`[world-boss] combat-spawn refresh failed (${reason})`, e);
    } finally {
      combatSpawnRefreshBusy = false;
    }
  };

  const isWorldBossMapNotice = (text: string) =>
    text.includes("[WBOSS:") ||
    text.includes("【世界BOSS】") ||
    text.includes("世界Boss 已刷新");

  const openTaskList = async () => {
    showPhase("quest");
    setChromeStatus("任务列表…");
    const lists = await session.fetchTaskLists();
    taskUi.open(lists.accepted, lists.candidates, "accepted");
    refreshQuestTracker();
    setChromeStatus(
      `已接${lists.accepted.length} · 可接${lists.candidates.length}`,
    );
  };

  const openSystem = (kind: SystemKind, hint?: string) => {
    if (kind === "quest") {
      void openTaskList();
      return;
    }
    system.setInBattle(phase === "battle" || session.inBattle);
    system.open(kind, hint);
    showPhase("system");
    if (kind === "status") shell.setSoftkeys({ left: "", right: "返回" });
    else shell.setSoftkeys({ left: "确定", right: "返回" });
    // 每次打开都向服务端拉最新列表，不沿用本地缓存。
    if (kind === "bag" || kind === "enhance") {
      system.setBag([]);
      const cap = session.characterStats?.backpackCapacity || 20;
      const money = session.characterStats?.money ?? session.gold;
      system.setBagMeta(money, cap);
      void session.openBag();
      // 金钱/容量以 1/1/21 汇总为准（铜拆金/银/铜）
      void session.openCharacterStats().then(() => {
        if (session.characterStats) {
          system.setBagMeta(
            session.characterStats.money,
            session.characterStats.backpackCapacity || 20,
          );
        }
      });
    }
    if (kind === "equip") {
      system.setWorn([]);
      system.setBag([]);
      const role =
        activeRole ||
        pendingRoles.find((r) => r.roleId === session.selectedRoleId) ||
        pendingRoles[0] ||
        null;
      if (role) system.setEquipActor(role.jobIndex, role.sex);
      else system.setEquipActor(session.roleJobIndex, 1);
      void Promise.all([session.openWornEquipment(), session.openBag()]);
    }
    if (kind === "mall") {
      system.setMall({ coolmoney: session.mallState?.coolmoney ?? 0, ticket: 0, goods: [] });
      void session.openMall();
    }
    if (kind === "skill") {
      system.setSkills([]);
      void session.refreshLearnedSkillsFromServer().then(() => {
        system.setSkills(session.learnedSkills);
      });
    }
    if (kind === "status") {
      void session.openCharacterStats().then(() => {
        if (session.characterStats) system.setStats(session.characterStats);
      });
    }
  };

  /** 「查看玩家信息」= 10/2 资料 + 29/4 装备列表（同屏合并）. */
  let playerCardCombine = false;
  let pendingPlayerInfo: PlayerInfoView | null = null;
  let pendingOtherEquip: OtherEquipView | null = null;

  const flushPlayerCard = () => {
    if (!pendingPlayerInfo) return;
    const lines = [...pendingPlayerInfo.lines];
    lines.push("");
    lines.push("—— 装备 ——");
    if (pendingOtherEquip) {
      if (pendingOtherEquip.items.length) {
        for (const it of pendingOtherEquip.items) {
          lines.push(
            `${it.name}${it.enhance ? `+${it.enhance}` : ""}`,
          );
        }
      } else {
        lines.push("(无装备)");
      }
    } else {
      lines.push("加载中…");
    }
    social.showPlayerInfo({
      lines,
      fdegree: pendingPlayerInfo.fdegree,
    });
    showPhase("social");
    if (pendingOtherEquip) playerCardCombine = false;
  };

  const openPlayerCard = (actorId: number, tipName?: string) => {
    if (!actorId) return;
    playerCardCombine = true;
    pendingPlayerInfo = null;
    pendingOtherEquip = null;
    showGameTip(
      tipName ? `查看玩家信息 · ${tipName}` : "查看玩家信息",
    );
    void session.requestPlayerInfo(actorId);
    void session.requestOtherEquip(actorId);
  };

  const session = new GameSession({
    onStatus: (s) => setChromeStatus(`[${s.mode}] ${s.phase}: ${s.detail}`),
    onServers: (servers) => {
      title.setServers(servers);
      const sel =
        servers.find((s) => s.serverId === session.selectedServerId) ??
        title.selectedServer ??
        servers[0];
      title.setStatus(sel ? `${sel.name} · ${sel.label}` : "");
      // Login success navigates via doLogin → confirmServer → role; don't steal to servers.
      if (phase === "servers") return;
    },
    onRoles: (list) => {
      pendingRoles = list;
      roles.setRoles(list);
      if (phase === "servers" || phase === "splash" || phase === "login" || phase === "register") {
        showPhase("role");
      }
    },
    onRoleSwitch: () => {
      // Drop previous role's painted map + party strip before 1/6 scene arrives.
      view.invalidateMap();
      hud.setParty([]);
      hud.setIdentity("", "", "");
      questTracker.setTasks([]);
    },
    onEnterScene: async (scene, x, y, opts) => {
      const hasPos = opts?.hasPos !== false;
      // Same map: keep painted shell (role-switch already called invalidateMap).
      let loadedScene = scene;
      try {
        await enterSceneFile(
          scene,
          hasPos ? x : undefined,
          hasPos ? y : undefined,
        );
      } catch (e) {
        console.warn("[main] enter server scene failed", scene, e);
        throw e;
      }
      const role =
        pendingRoles.find((r) => r.roleId === session.selectedRoleId) ||
        (activeRole && activeRole.roleId === session.selectedRoleId
          ? activeRole
          : null) ||
        session.roles.find((r) => r.roleId === session.selectedRoleId) ||
        null;
      if (role) activeRole = role;
      const titleTag = session.roleTitle || "";
      const titleGif = session.roleTitleGif || "";
      hud.setIdentity(
        role?.name ?? session.roleName ?? "侠客",
        loadedScene,
        titleTag,
      );
      hud.setVitals(session.hp, session.hpMax, session.mp, session.mpMax);
      hud.setLevel(session.level || role?.level || 1, 0.08);
      hud.setBagSlots(15, 20);
      // Real party only (memberCount>=2). Solo/orphan 5/10 must not keep bars.
      const selfId = session.selectedRoleId >>> 0;
      const party =
        session.teamMembers.length >= 2
          ? session.teamMembers
              .filter((m) => m.id !== selfId)
              .map((m) => ({
                id: m.id,
                name: m.name,
                jobIndex: m.jobIndex,
                sex: m.sexGroup >= 1 ? (m.sexGroup - 1) & 1 : 0,
                hp: m.hp,
                hpMax: Math.max(1, m.hpMax),
                mp: m.mp,
                mpMax: Math.max(1, m.mpMax),
                isLeader: m.id === session.teamLeaderId,
              }))
          : [];
      hud.setParty(party);
      hud.pushChat("[系]系统: 欢迎进入江湖世界");
      if (role) {
        void hud.setPortraitActor(uiActorResourceForJobSex(role.jobIndex, role.sex));
        void view.setPlayerActor(role.jobIndex, role.sex);
        void view.setPlayerNameplate(
          role.name,
          titleTag,
          titleGif,
          isRedNamePk(session.pkPoints),
        );
      } else {
        void view.setPlayerActor(0, 0);
        void view.setPlayerNameplate(
          session.roleName || "侠客",
          titleTag,
          titleGif,
          isRedNamePk(session.pkPoints),
        );
      }
      /* Cache may be empty until 1/1/21; apply then refresh. */
      void view.setPlayerGuild(
        session.guildName || session.characterStats?.guild || "",
      );
      // Same-town role switch: showScene skips marker clear on identical map —
      // force catalog/peer refresh so role A's NPCs/players do not linger.
      view.actors.clearPeers();
      view.actors.setNpcs(session.npcs);
      view.actors.setTaskPrompts(session.npcTaskPrompts);
      view.actors.setSceneMonsters(session.sceneMonsters);
      // Instance exit / scene hop can land on SCE combat spawns — brief lockout
      // so the first frames do not auto-start a wild fight before NPCs paint.
      view.armCollideCooldown(3000);
      // Ordinary death revive (1/7/14 result=2) lands via 20/1+30/1 while battle UI
      // was still mounted — tear it down with the scene enter.
      if (phase === "battle") battle.hide();
      showPhase("world");
      // Soft shell: provisional map-center until server posinfo; don't stamp old-map x/y.
      if (hasPos || session.hasServerPos) {
        view.setPlayerPos(session.x, session.y);
      }
      setChromeStatus(
        `[${session.transport?.mode ?? "?"}] ${loadedScene} @${session.x},${session.y} · NPC×${session.npcs.length} 怪×${session.sceneMonsters.length} · WASD/F/Q`,
      );
      void session.fetchAuctionBoard().then((board) => {
        auctionCache = board;
        view.setPaiBlink(!!board.status?.blink);
      });
      void session.fetchWagerBoard().then((board) => {
        if (board.mine?.needConfirm) view.setLeiBlink(true);
      });
      void session.syncCosmetics();
      /* Authoritative guild for self nameplate「门派-xxx」(1/1/21). */
      void session.openCharacterStats();
    },
    onPlayerPos: (x, y) => {
      view.setPlayerPos(x, y);
      setChromeStatus(
        `[${session.transport?.mode ?? "?"}] ${session.scene} @${x},${y} · NPC×${session.npcs.length} 怪×${session.sceneMonsters.length} · WASD/F/Q`,
      );
    },
    onNpcs: (npcs) => {
      view.actors.setNpcs(npcs);
      setChromeStatus(
        `[${session.transport?.mode ?? "?"}] NPC×${npcs.length} 怪×${session.sceneMonsters.length}`,
      );
      refreshQuestTracker();
    },
    onNpcTaskPrompts: (prompts) => {
      view.actors.setTaskPrompts(prompts);
      refreshQuestTracker();
    },
    onPeers: (peers, removed) => {
      view.actors.applyPeers(peers, removed);
      for (const cos of session.peerCosmeticsSnapshot()) {
        view.actors.applyPeerCosmetics(
          cos.actorId,
          cos.eqChenghao,
          cos.eqWing,
          cos.eqMount,
          cos.guild || "",
        );
      }
    },
    onPeerDesignation: (info) => {
      view.actors.applyPeerTitle(info.actorId, info.name, info.gif);
    },
    onCosmeticsPeer: (peer) => {
      view.actors.applyPeerCosmetics(
        peer.actorId,
        peer.eqChenghao,
        peer.eqWing,
        peer.eqMount,
        peer.guild || "",
      );
    },
    onPeerMoves: (moves) => view.actors.applyPeerMoves(moves),
    onNearbyPage: (page) => {
      session.nearbyList = page.peers;
      /*
       * 2/7 list rows share otherinfo with map peers.  Also paint them so
       * opening「周围玩家」does not wait for the next scene-sync 2/10 baseline
       * (arena enter used to leave ready pending until this page).
       */
      const synced = session.syncMapPeersFromNearbyPage(page.peers);
      if (synced.peers.length || synced.removed.length) {
        view.actors.applyPeers(synced.peers, synced.removed);
        for (const cos of session.peerCosmeticsSnapshot()) {
          view.actors.applyPeerCosmetics(
            cos.actorId,
            cos.eqChenghao,
            cos.eqWing,
            cos.eqMount,
            cos.guild || "",
          );
        }
      }
      if (phase === "social" && social.panelKind === "nearby")
        social.openNearby(page.peers);
      setChromeStatus(
        page.peers.length
          ? `周围玩家 ×${page.peers.length}（↑↓翻页）`
          : "周围玩家 ×0",
      );
    },
    onTeamUpdate: (roster) => {
      if (phase === "social" && social.panelKind === "team")
        social.openTeam(roster.members, roster.leaderId);
      const selfId = session.selectedRoleId >>> 0;
      /* Real party only. Solo/orphan 5/10 (memberCount<2) and self row must
       * never paint the top-left teammate vitals after role switch. */
      const mates =
        selfId === 0 || roster.members.length < 2
          ? []
          : roster.members
              .filter((m) => m.id !== selfId)
              .map((m) => ({
                id: m.id,
                name: m.name,
                jobIndex: m.jobIndex,
                sex: m.sexGroup >= 1 ? (m.sexGroup - 1) & 1 : 0,
                hp: m.hp,
                hpMax: Math.max(1, m.hpMax),
                mp: m.mp,
                mpMax: Math.max(1, m.mpMax),
                isLeader: m.id === roster.leaderId,
              }));
      hud.setParty(mates);
      setChromeStatus(
        mates.length
          ? `队伍 ×${mates.length + 1}`
          : roster.members.length
            ? "队伍"
            : "已离队",
      );
    },
    onFriends: (friends) => {
      if (phase === "social" && social.panelKind === "friends") {
        social.openFriends(friends, session.friendPage, session.friendAllPages, {
          keepHub: true,
        });
      }
      setChromeStatus(
        session.friendAllPages > 1
          ? `好友 ${session.friendPage}/${session.friendAllPages}`
          : `好友 ×${friends.length}`,
      );
    },
    onEnmity: ({ dir, rows }) => {
      if (phase === "social" && social.panelKind === "enmity") {
        social.openEnmity(
          rows,
          dir,
          social.huntOrderPickSeq,
          session.enmityPage,
          session.enmityAllPages,
        );
      }
      setChromeStatus(
        session.enmityAllPages > 1
          ? `恩怨 ${session.enmityPage}/${session.enmityAllPages}`
          : `恩怨 ×${rows.length}`,
      );
    },
    onSocialInvite: (inv) => {
      const title =
        inv.kind === "team"
          ? "组队邀请"
          : inv.kind === "friend"
            ? "好友邀请"
            : inv.kind === "guild"
              ? "入帮邀请"
              : "交易邀请";
      const body =
        inv.kind === "team"
          ? `${inv.name} 邀请你组队`
          : inv.kind === "friend"
            ? `${inv.name} 邀请加你为好友`
            : inv.kind === "guild"
              ? `${inv.name} 邀请你加入帮派${inv.guildName ? `「${inv.guildName}」` : ""}`
              : `${inv.name} 邀请交易`;
      const acceptAction =
        inv.kind === "team"
          ? 220
          : inv.kind === "friend"
            ? 230
            : inv.kind === "guild"
              ? 258
              : 242;
      const refuseAction =
        inv.kind === "team"
          ? 221
          : inv.kind === "friend"
            ? 231
            : inv.kind === "guild"
              ? 259
              : 243;
      dialog.show({
        title,
        body,
        options: [
          { label: "同意", action: acceptAction, value: 1 },
          { label: "拒绝", action: refuseAction, value: 0 },
        ],
      });
      showPhase("dialog");
      setChromeStatus(body);
    },
    onSocialNotice: (n) => {
      if (n.kind === "team_result") {
        const ok = n.result === 1;
        const tip = ok ? "组队成功" : "组队被拒";
        hud.pushChat(
          ok ? `[队]${n.name} 同意组队` : `[队]${n.name} 拒绝组队`,
        );
        setChromeStatus(tip, !ok);
        if (social.visible) social.showTip(tip);
      } else if (n.kind === "friend_result") {
        const ok = n.result === 1;
        const tip = ok ? "已加好友" : "好友被拒";
        hud.pushChat(
          ok ? `[友]${n.name} 同意加好友` : `[友]${n.name} 拒绝加好友`,
        );
        setChromeStatus(tip, !ok);
        if (social.visible) social.showTip(tip);
      } else if (n.kind === "team_leave") {
        hud.pushChat(`[队]成员离开 ${n.id >>> 0}`);
        if (social.visible) social.showTip("队员已离开");
        if (phase === "battle" && battle.visible) battle.noteTeammateLeft();
      } else if (n.kind === "trade_result") {
        if (n.result !== 1) {
          hud.pushChat(`[交]${n.name || "对方"} 拒绝/结束交易`);
          showGameTip("交易未开始");
          if (phase === "trade") {
            tradeUi.hide();
            showPhase("world");
          }
        }
      } else if (n.kind === "spar_result") {
        hud.pushChat(`[切]${n.tip}`);
        setChromeStatus(n.tip, n.result !== 1);
        if (social.visible && n.tip) social.showTip(n.tip);
        if (n.result === 1) {
          /* Server awards +20 on initiate (cap 100); mirror for local red-name. */
          session.pkPoints = Math.min(
            PK_POINTS_MAX,
            (session.pkPoints | 0) + 20,
          );
          const name =
            session.roleName ||
            activeRole?.name ||
            "侠客";
          void view.setPlayerNameplate(
            name,
            session.roleTitle,
            session.roleTitleGif,
            isRedNamePk(session.pkPoints),
          );
        }
      } else if (n.kind === "guild_sent") {
        hud.pushChat("[帮]已发送入帮邀请");
        setChromeStatus("入帮邀请已发送");
        if (guildUi.visible) guildUi.showTip("入帮邀请已发送");
        if (social.visible) social.showTip("入帮邀请已发送");
      } else if (n.kind === "guild_invite_result") {
        const ok = n.result === 1;
        const tip = ok ? "入帮邀请已同意" : "入帮邀请被拒绝";
        hud.pushChat(
          ok
            ? `[帮]${n.name || "对方"} 同意加入帮派`
            : `[帮]${n.name || "对方"} 拒绝加入帮派`,
        );
        setChromeStatus(tip, !ok);
        if (guildUi.visible) guildUi.showTip(tip);
        if (social.visible) social.showTip(tip);
      } else if (n.kind === "guild_create") {
        hud.pushChat(`[帮]${n.tip}`);
        setChromeStatus(n.tip, n.stage === "fail");
        if (guildUi.visible && n.tip) guildUi.showTip(n.tip);
        if (n.stage === "need_name") {
          const name = window.prompt("帮派名称", "") ?? "";
          if (name.trim()) void session.guildCreateCheckName(name.trim());
          else setChromeStatus("已取消创建", true);
        } else if (n.stage === "confirm") {
          dialog.show({
            title: "创建帮派",
            body: n.tip,
            options: [
              { label: "确认创建", action: 250, value: 1 },
              { label: "取消", action: 251, value: 0 },
            ],
          });
          showPhase("dialog");
        } else if (n.stage === "done") {
          void session.openCharacterStats();
          if (phase === "guild") {
            guildUi.openHub(session.inGuild, session.guildName || session.characterStats?.guild || "");
            if (n.tip) guildUi.showTip(n.tip);
          }
        }
      }
    },
    onGuildManage: (u) => {
      if (u.membershipNotice) {
        hud.pushChat(`[帮]${u.membershipNotice.info}`);
        setChromeStatus(u.membershipNotice.info, u.membershipNotice.type !== 1);
        if (guildUi.visible) guildUi.showTip(u.membershipNotice.info);
        if (u.membershipNotice.type === 1 && u.membershipNotice.guildName) {
          void view.setPlayerGuild(u.membershipNotice.guildName);
        }
      }
      if (u.applyResult) {
        hud.pushChat(`[帮]${u.applyResult.tip}`);
        setChromeStatus(u.applyResult.tip, u.applyResult.result !== 1);
        if (guildUi.visible) guildUi.showTip(u.applyResult.tip);
      }
      if (u.leaveResult) {
        const tip =
          u.leaveResult.result === 1 && expectGuildDisbandTip
            ? "帮派已解散"
            : u.leaveResult.tip;
        expectGuildDisbandTip = false;
        hud.pushChat(`[帮]${tip}`);
        setChromeStatus(tip, u.leaveResult.result !== 1);
        if (u.leaveResult.result === 1) {
          guildUi.openHub(false, "无帮派");
          showPhase("guild");
          void view.setPlayerGuild("");
        }
        if (guildUi.visible) guildUi.showTip(tip);
      }
      if (u.kickResult) {
        if (guildUi.visible) guildUi.showTip(u.kickResult.tip);
        else showGameTip(u.kickResult.tip);
      }
      if (u.reviewResult) {
        hud.pushChat(`[帮]${u.reviewResult.tip}`);
        setChromeStatus(u.reviewResult.tip, u.reviewResult.result !== 1);
        if (guildUi.visible) guildUi.showTip(u.reviewResult.tip);
        if (u.reviewResult.result === 1) void session.openGuildApplications(session.guildAppPage);
      }
      if (u.list) {
        guildUi.setList(u.list.rows, session.guildListPage, u.list.allpgs, u.list.fid);
        showPhase("guild");
        setChromeStatus(`帮派列表 ×${u.list.rows.length}`);
      }
      if (u.detail) {
        const fromList = phase === "guild";
        guildUi.showDetail(u.detail, fromList);
        showPhase("guild");
        setChromeStatus(`帮派 ${u.detail.guildName}`);
        if (
          (u.detail.guildId >>> 0) === (session.guildId >>> 0) ||
          session.inGuild
        ) {
          void view.setPlayerGuild(u.detail.guildName);
        }
      }
      if (u.members) {
        guildUi.setMembers(
          u.members.members,
          session.guildMemberPage,
          u.members.allpgs,
          u.members.cnum,
          u.members.mnum,
          { selfRoleId: session.selectedRoleId },
        );
        showPhase("guild");
      }
      if (u.applications) {
        guildUi.setApplications(
          u.applications.applicants,
          session.guildAppPage,
          u.applications.allpgs,
          u.applications.result,
          u.applications.roles,
          u.applications.maxroles,
        );
        showPhase("guild");
        if (u.applications.result === 3) {
          setChromeStatus("招收权限不足", true);
          if (guildUi.visible) guildUi.showTip("招收权限不足");
        } else if (u.applications.result === 4) {
          setChromeStatus("暂无入帮申请");
          if (guildUi.visible) guildUi.showTip("暂无入帮申请");
        }
      }
    },
    onTradeSession: (u) => {
      if (u.kind === "open") {
        tradeUi.open(u.peerName, session.bagItems);
        showPhase("trade");
        void session.openBag().then(() => tradeUi.setBag(session.bagItems));
        hud.pushChat(`[交]与 ${u.peerName || "对方"} 交易`);
        return;
      }
      if (u.kind === "peer_offer") {
        tradeUi.setPeerOffer(u.offer);
        return;
      }
      if (u.kind === "offer_ack") {
        tradeUi.setStatus(u.tip, false, u.result === 1);
        return;
      }
      if (u.kind === "waiting") {
        tradeUi.setStatus("等待对方确认", true, true);
        return;
      }
      if (u.kind === "cancel" || u.kind === "done") {
        hud.pushChat(`[交]${u.tip}`);
        tradeUi.hide();
        if (phase === "trade" || phase === "dialog") showPhase("world");
        if (u.tip) showGameTip(u.tip);
        if (u.kind === "done" && u.result === 1) void session.openBag();
      }
    },
    onPlayerInfo: (info) => {
      if (playerCardCombine) {
        pendingPlayerInfo = info;
        flushPlayerCard();
        return;
      }
      social.showPlayerInfo(info);
      showPhase("social");
    },
    onOtherEquip: (eq) => {
      if (playerCardCombine) {
        pendingOtherEquip = eq;
        flushPlayerCard();
        return;
      }
      social.showOtherEquip(eq);
      showPhase("social");
    },
    onDesignationUpdate: (info) => {
      const name =
        session.roleName ||
        activeRole?.name ||
        pendingRoles.find((r) => r.roleId === session.selectedRoleId)?.name ||
        "侠客";
      void view.setPlayerNameplate(
        name,
        info.name,
        info.gif,
        isRedNamePk(session.pkPoints),
      );
      hud.setIdentity(name, session.scene, info.name);
    },
    onCosmeticsUpdate: (state) => {
      void view.setPlayerWing(state.eqWing, false);
      void view.setPlayerMount(state.eqMount, false);
      const name =
        session.roleName ||
        activeRole?.name ||
        pendingRoles.find((r) => r.roleId === session.selectedRoleId)?.name ||
        "侠客";
      void view.setPlayerNameplate(
        name,
        session.roleTitle,
        session.roleTitleGif,
        isRedNamePk(session.pkPoints),
      );
      if (wingPanel.visible) {
        wingPanel.open(state.eqWing, state.unlockedWing);
      }
      if (mountPanel.visible) {
        mountPanel.open(state.eqMount, state.unlockedMount);
      }
    },
    onBattle: (state) => {
      // Hangup / challenge 4/1 may skip onMonsterCollide — hide map SCE spawn now.
      // Temporary suppress only (wild ~10s respawn). World-boss permanent drop is
      // solely via [WBOSS:claim] → dropWorldBossFromMap; do not permanent-remove
      // here or post-battle SCE refresh re-paints the wild spawn immediately.
      {
        const sm = state.sceneMonster;
        const pv = session.pendingEncounterVisual;
        const hideId =
          session.sceneMonsters.find(
            (n) =>
              (sm &&
                (((n.homeX ?? n.x) | 0) === (sm.x | 0) &&
                  ((n.homeY ?? n.y) | 0) === (sm.y | 0))) ||
              (pv &&
                (((n.homeX ?? n.x) | 0) === (pv.x | 0) &&
                  ((n.homeY ?? n.y) | 0) === (pv.y | 0))),
          )?.actorId ?? 0;
        if (hideId >= 0x70000000) {
          view.actors.suppressSceneMonster(hideId);
        }
      }
      // Prefer selectedRoleId — stale activeRole was the first account role and
      // painted the wrong job/sex into battle when multi-role accounts fought.
      const role =
        pendingRoles.find((r) => r.roleId === session.selectedRoleId) ||
        (activeRole && activeRole.roleId === session.selectedRoleId
          ? activeRole
          : null) ||
        session.roles.find((r) => r.roleId === session.selectedRoleId) ||
        null;
      if (role) activeRole = role;
      const selfId = session.selectedRoleId >>> 0;
      /* subtype-10 spar: visual0=sexGroup 1..2. Challenge/hangup PvE uses
       * crash-safe placeholder visual 0/1 — must NOT map to 男幻剑. */
      const leftIsSparPlayer = (u: (typeof state.left)[number]) =>
        state.subtype === 10 &&
        u.visual0 != null &&
        u.visual0 >= 1 &&
        u.visual0 <= 2;
      /* One encounter → one species for every PvE left clone (CBE clones the
       * touched SCE row). Resolve once from collide spawn / sceneMonster pos. */
      const encounterId =
        state.sceneMonster?.index ??
        session.pendingEncounterVisual?.battleActorId ??
        state.left.find((u) => !leftIsSparPlayer(u))?.id ??
        0;
      const encounterName = state.left.find((u) => !leftIsSparPlayer(u))?.name;
      const encounterResolved = session.resolvePveEnemyActor(
        encounterId,
        encounterName,
        state.sceneMonster
          ? { x: state.sceneMonster.x, y: state.sceneMonster.y }
          : session.pendingEncounterVisual
            ? {
                x: session.pendingEncounterVisual.x,
                y: session.pendingEncounterVisual.y,
              }
            : undefined,
      );
      const encounterActor =
        encounterResolved?.actorResource ||
        session.pendingEncounterVisual?.actorResource ||
        (() => {
          const mon =
            session.sceneMonsters.find(
              (n) =>
                (state.sceneMonster &&
                  (n.x | 0) === (state.sceneMonster.x | 0) &&
                  (n.y | 0) === (state.sceneMonster.y | 0)) ||
                n.battleActorId === encounterId ||
                n.actorId === encounterId,
            ) ||
            ((state.subtype === 5 || state.subtype === 10)
              ? session.sceneMonsters[0]
              : undefined);
          return mon?.battleActorResource || mon?.actorResource || "";
        })() ||
        "e_mucusY.actor";
      const encounterLabel =
        encounterResolved?.name ||
        session.pendingEncounterVisual?.name ||
        undefined;
      const leftActors = state.left.map((u) => {
        if (leftIsSparPlayer(u) && u.jobIndex != null) {
          return actorResourceForJobSex(u.jobIndex, u.sex ?? 0);
        }
        return encounterActor;
      });
      for (let i = 0; i < state.left.length; i++) {
        const slot = state.left[i]!;
        slot.actorResource = leftActors[i];
        if (leftIsSparPlayer(slot)) {
          /* spar: keep wire name + job/sex */
          continue;
        }
        // PvE placeholder visuals are not player job/sex — drop so HUD/units
        // do not treat the seat as 男幻剑.
        slot.jobIndex = undefined;
        slot.sex = undefined;
        if (encounterLabel && (!slot.name || slot.name.startsWith("怪#"))) {
          slot.name =
            state.left.length > 1
              ? `${encounterLabel}#${i + 1}`
              : encounterLabel;
        }
      }
      session.pendingEncounterVisual = null;
      if (state.subtype === 10) {
        session.pendingChallengeGuideActorId = 0;
      }
      /* Team 4/5 right rows are frozen-member order (leader first), not
       * "self at [0]". Enrich every seat by wire id; never paint all seats
       * with the local role actor. Prefer live map peer visuals so battle
       * bodies match what was on the scene (2/10 visualGroup/Variant). */
      for (const slot of state.right) {
        const peer = session.peerVisualFor(slot.id);
        const mate = session.teamMembers.find((m) => m.id === slot.id);
        if (peer) {
          if (!slot.name || slot.name.startsWith("队友")) {
            slot.name = peer.name || mate?.name || slot.name;
          }
          slot.jobIndex = peer.jobIndex;
          slot.sex = peer.sex;
          slot.actorResource = actorResourceForJobSex(slot.jobIndex, slot.sex);
          continue;
        }
        if (mate) {
          if (!slot.name || slot.name.startsWith("队友")) slot.name = mate.name;
          slot.jobIndex = mate.jobIndex;
          slot.sex = mate.sexGroup >= 1 ? (mate.sexGroup - 1) & 1 : 0;
          slot.actorResource = actorResourceForJobSex(slot.jobIndex, slot.sex);
          continue;
        }
        if (slot.id === selfId) {
          slot.name = session.roleName || role?.name || slot.name;
          slot.jobIndex = role?.jobIndex ?? session.roleJobIndex;
          slot.sex = role?.sex ?? session.roleSex ?? slot.sex ?? 0;
          slot.actorResource = actorResourceForJobSex(
            slot.jobIndex ?? 0,
            slot.sex ?? 0,
          );
        }
      }
      const selfSlot =
        state.right.find((s) => s.id === selfId) ||
        state.right.find((s) => s.jobIndex != null) ||
        state.right[0] ||
        null;
      const jobIndex =
        selfSlot?.jobIndex ?? role?.jobIndex ?? session.roleJobIndex;
      const sex = selfSlot?.sex ?? role?.sex ?? session.roleSex ?? 0;
      battle.setVisualContext({
        worldScene: session.scene,
        playerActor:
          selfSlot?.actorResource || actorResourceForJobSex(jobIndex, sex),
        leftActors,
        jobIndex,
        sex,
        playerName:
          selfSlot?.name ||
          session.roleName ||
          role?.name ||
          state.right[0]?.name,
        selfId,
      });
      void (async () => {
        const skills = await session.getBattleSkills();
        battle.setSkills(skills);
        try {
          await warmItemCatalogs();
          const hasBattleItem = session.bagItems.some(isBattleMedicineOrFlask);
          // Empty bag, or bag present but no usable medicine/flask seq (stale
          // 17/1-only / wiped by a prior bad 7/7 replace): refresh 7/23.
          if (!session.bagItems.length || !hasBattleItem) {
            await session.openBag();
          }
        } catch {
          /* bag optional for battle entry */
        }
        battle.setBattleItems(session.bagItems);
        const arenaDuel = session.isArenaScene();
        battle.setArenaDuel(arenaDuel);
        if (arenaDuel) {
          /* Fair duel: no [自动]/hangup synth; force manual + 20s NA. */
          if (session.autoBattleActive) {
            void session.setBattleAutoPrefer(false);
          }
          if (session.hangupActive || session.hangupDesired) {
            void session.stopHangup();
          }
          battle.setHangupActive(false);
          battle.setAutoBattleActive(false);
          if (autoArmTimer != null) {
            window.clearTimeout(autoArmTimer);
            autoArmTimer = null;
          }
        } else {
          battle.setHangupActive(session.hangupActive);
          battle.setAutoBattleActive(session.autoBattleActive);
          if (session.hangupActive) {
            const cap =
              session.characterStats?.backpackCapacity ||
              Math.max(64, session.bagItems.length);
            battle.setHangupBagSlots(session.bagItems.length, cap);
            battle.setBattleInsightRemainSec(
              session.getBattleInsightRemainingSec(),
            );
          }
        }
        battle.show(state);
        touch.releaseAll();
        view.clearHeldKeys();
        view.inputEnabled = false;
        if (selfSlot) {
          session.hp = selfSlot.hp;
          session.hpMax = Math.max(selfSlot.maxHp, selfSlot.hp, 1);
          session.mp = selfSlot.mp;
          session.mpMax = Math.max(selfSlot.maxMp, selfSlot.mp, 1);
        }
        showPhase("battle");
        if (arenaDuel) {
          shell.setSoftkeys({ left: "确定", right: "" });
        } else if (session.hangupActive || session.autoBattleActive) {
          shell.setSoftkeys({ left: "", right: "" });
        }
        if (!arenaDuel && session.autoBattleActive) {
          // Re-arm gateway prefer for this fight (poll synth; hangup=0).
          void session.setBattleAutoPrefer(true);
        }
        if (!arenaDuel && session.shouldClientDriveInBattleAuto()) {
          armInBattleAuto("挂机出手…");
        }
      })();
    },
    onSkills: (skills) => {
      battle.setSkills(skills);
      system.setSkills(skills);
    },
    onBattleUpdate: (up) => {
      battle.applyUpdate(up);
      const vitals = battle.allyVitals();
      if (vitals) {
        session.hp = vitals.hp;
        session.hpMax = Math.max(vitals.maxHp, vitals.hp, 1);
        session.mp = vitals.mp;
        session.mpMax = Math.max(vitals.maxMp, vitals.mp, 1);
        hud.setVitals(session.hp, session.hpMax, session.mp, session.mpMax);
      } else if (up.rightHp[0]) {
        const r = up.rightHp[0];
        session.hp = r.hp;
        if (r.mp != null) session.mp = r.mp;
        hud.setVitals(session.hp, session.hpMax, session.mp, session.mpMax);
      }
      if (up.settlement) {
        const s = up.settlement;
        const cap =
          session.characterStats?.backpackCapacity ||
          Math.max(64, session.bagItems.length);
        battle.setHangupBagSlots(session.bagItems.length, cap);
        if (session.hangupActive || s.hangupCombatinfo) {
          battle.setBattleInsightRemainSec(
            session.getBattleInsightRemainingSec(),
          );
          battle.setHangupActive(session.hangupActive || !!s.hangupCombatinfo);
        }
        const pct =
          s.curExp > s.lastExp
            ? (s.expTotal - s.lastExp) / Math.max(1, s.curExp - s.lastExp)
            : s.percentExp / 100;
        hud.setLevel(session.level, pct);
        hud.pushChat(
          `[战] 经验+${s.expGain} 金钱+${s.goldGain}` +
            (s.fdata ? ` ${s.fdata}` : ""),
        );
        setChromeStatus(
          `结算 经验+${s.expGain} 金+${s.goldGain} · 按攻击继续`,
        );
        shell.setSoftkeys({ left: "继续", center: "", right: "继续" });
      } else if (phase === "battle" && !up.finished) {
        const hideKeys =
          battle.isAwaitingRound() ||
          battle.isPlaybackBusy() ||
          (!battle.isArenaDuel() &&
            (session.hangupActive || session.autoBattleActive));
        shell.setSoftkeys(
          hideKeys
            ? { left: "", right: "" }
            : {
                left: battle.softkeyLeftLabel(),
                right: battle.softkeyRightLabel(),
              },
        );
      }
      // Escape leaves immediately. Victory 4/8 waits for BattleScreen playback
      // (onExitReady); 4/7 settle panel uses onSettleConfirm — never tear down
      // mid-playback or deferred HP/MP never reaches the world HUD.
      if (up.escaped && up.escapeOk) {
        endBattleToWorld(up.log);
      } else if (up.finished && !up.settlement && !(up.actions?.length)) {
        endBattleToWorld(up.log);
      }
    },
    onWagerEvent: (ev) => {
      const kind =
        ev.eventKind === 1
          ? "有人应战，请确认"
          : ev.eventKind === 2
            ? "开战"
            : ev.eventKind === 3
              ? ev.result === 1
                ? `结算胜+${ev.payout}`
                : ev.result === 2
                  ? "结算负"
                  : `结算退${ev.payout}`
              : ev.eventKind === 4
                ? "擂主拒绝"
                : ev.eventKind === 5
                  ? "超时，已自动撤擂"
                  : "通知";
      showGameTip(`赏金#${ev.challengeId} ${kind}`);
      if (wagerPanel.visible) void openWagerPanel();
      else view.setLeiBlink(true);
    },
    onAuctionEvent: (ev) => {
      view.setPaiBlink(!!ev.blink);
      const kind =
        ev.eventKind === 1
          ? "可领取"
          : ev.eventKind === 2
            ? `被超价退${ev.amount}`
            : ev.eventKind === 3
              ? `成交${ev.amount}`
              : ev.eventKind === 4
                ? "超时下架"
                : "拍卖通知";
      showGameTip(
        ev.listingId ? `拍卖#${ev.listingId} ${kind}` : `拍卖 ${kind}`,
      );
      if (auctionPanel.visible) {
        void openAuctionPanel({
          preferTab: auctionPanel.tab,
          preservePages: true,
        });
      }
    },
    onArenaHud: (hud) => {
      arenaHud.apply(hud);
    },
    onNpcDialog: (d) => {
      if (session.equipSellBatchSilent) return;
      const shown = augmentEquipSellDialog(d);
      dialog.show(shown);
      showPhase("dialog");
      setChromeStatus(`对话 · ${shown.options.length} 选项`);
    },
    onTaskPrompt: (p) => {
      dialog.show({
        title: p.mode === "submit" ? "提交任务" : "接取任务",
        body: p.text,
        options: [
          {
            label: p.mode === "submit" ? "确认提交" : "确认接取",
            action: 100,
            value: p.taskId,
          },
          { label: "取消", action: 101, value: 0 },
        ],
      });
      showPhase("dialog");
      setChromeStatus(p.mode === "submit" ? "确认提交任务" : "确认接取任务");
    },
    onChallengePrompt: (text) => {
      dialog.show({
        title: "挑战",
        body: text,
        options: [
          { label: "确认挑战", action: 102, value: 1 },
          { label: "取消", action: 103, value: 0 },
        ],
      });
      showPhase("dialog");
      setChromeStatus("挑战确认");
    },
    onChat: (lines) => {
      for (const line of lines) {
        if (isWorldBossMapNotice(line.text)) {
          if (isWbossDespawnLine(line.text) || line.text.includes("[WBOSS:claim]")) {
            dropWorldBossFromMap(
              line.text.includes("[WBOSS:claim]") ? "claim" : "despawn",
              line.text,
            );
          } else if (isWbossSpawnLine(line.text)) {
            void refreshSceneCombatSpawns("spawn");
          } else {
            void refreshSceneCombatSpawns("notice");
          }
        }
        const hangTip = line.text.includes("已停止挂机")
          ? "已停止挂机"
          : line.text.includes("不能挂机")
            ? "当前场景不能挂机"
            : line.text.includes("挂机失败")
              ? "挂机失败"
            : line.text.includes("已开始挂机")
              ? "已开始挂机"
              : line.text.includes("下一场完成后挂机停止")
                ? "下一场完成后挂机停止"
                : null;
        if (hangTip) {
          hud.pushChat(`[挂] ${hangTip}`, 5);
          showGameTip(hangTip);
          battle.setHangupActive(session.hangupActive);
          syncWorldStatusBadges();
          if (phase === "battle" && battle.visible) {
            shell.setSoftkeys(
              session.hangupActive || session.autoBattleActive
                ? { left: "", right: "" }
                : battle.isAwaitingRound() || battle.isPlaybackBusy()
                  ? { left: "", right: "" }
                  : {
                      left: battle.softkeyLeftLabel(),
                      right: battle.softkeyRightLabel(),
                    },
            );
          }
          continue;
        }
        /* 比武胜/负/平/轮空：系统条也弹顶 tip（轮空无 4/7 结算面板）。 */
        const arenaTip = extractArenaResultTip(line.text);
        if (arenaTip) showGameTip(arenaTip);
        const arenaPhaseTip = extractArenaPhaseTip(line.text);
        if (arenaPhaseTip) showGameTip(arenaPhaseTip);
        const streakHit = findArenaKillStreakTitle(line.text);
        if (streakHit) {
          const i = line.text.indexOf("比武");
          const tip =
            i >= 0 ? line.text.slice(i).replace(/^\]/, "").trim() : line.text;
          if (tip) showGameTip(tip);
          /* 连杀播报音效：与称号分段对应，不受 BGM/开关声音影响。 */
          playKillStreakTitle(streakHit.title);
        }
        hud.pushChat(line.text, line.type, {
          senderRoleId: line.senderRoleId,
          senderName: line.senderName,
          streakTitle: streakHit?.title,
          streakColor: streakHit?.color,
        });
      }
      if (phase === "chat") chatPanel.setEntries(hud.getChatEntries());
    },
    onAccountKicked: (reason) => {
      mapUiMode = "off";
      miniMap.hide();
      worldMap.hide();
      battle.hide();
      dialog.hide();
      system.hide();
      enhanceUi.hide();
      social.hide();
      chatPanel.hide();
      tradeUi.hide();
      guildUi.hide();
      hud.pushChat(`[系]${reason}`);
      title.setStatus(reason);
      setChromeStatus(reason, true);
      showPhase("splash");
    },
    onHangupResumeStatus: (message) => {
      if (!message) return;
      hud.pushChat(`[挂]${message}`);
      showGameTip(message);
      setChromeStatus(message);
    },
    onItemUseFeedback: (fb) => {
      showGameTip(fb.message);
    },
    onVitals: (hp, hpMax, mp, mpMax) => {
      hud.setVitals(hp, Math.max(hpMax, hp, 1), mp, Math.max(mpMax, mp, 1));
    },
    onBag: (items) => {
      system.setBag(items);
      enhanceUi.setBag(items);
      tradeUi.setBag(items);
      battle.setBattleItems(items);
      const cap =
        session.characterStats?.backpackCapacity ||
        Math.max(20, items.length);
      const money = session.characterStats?.money ?? session.gold;
      system.setBagMeta(money, cap);
      hud.setBagSlots(items.length, Math.max(cap, items.length));
      if (phase === "system") setChromeStatus(`背包 ${items.length} 件`);
    },
    onWorn: (items) => {
      system.setWorn(items);
      view.setPlus16Count(countPlus16Worn(items));
    },
    onMall: (mall) => {
      system.setMall(mall);
      if (phase === "system") setChromeStatus(`商城 酷宝${mall.coolmoney}`);
    },
    onCharacterStats: (stats) => {
      system.setStats(stats);
      system.setBagMeta(stats.money, stats.backpackCapacity || 20);
      const name =
        stats.name ||
        session.roleName ||
        activeRole?.name ||
        "侠客";
      void view.setPlayerNameplate(
        name,
        stats.title || session.roleTitle,
        session.roleTitleGif,
        isRedNamePk(stats.pkPoints),
      );
      void view.setPlayerGuild(stats.guild || session.guildName || "");
    },
    onHangup: (msg) => {
      hud.pushChat(`[挂] ${msg}`);
      showGameTip(msg);
    },
  });

  view.onMoveDir = (dir) => {
    session.x = view.playerX | 0;
    session.y = view.playerY | 0;
    session.noteMoveDir(dir);
  };
  const closeApproach = (restoreSoftkeys = true) => {
    if (!approach.open) return;
    approach.hide();
    if (restoreSoftkeys && phase === "world") {
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
    }
  };

  const openApproach = (actorId: number, name: string) => {
    if (phase !== "world" || session.inBattle) return;
    approach.show(actorId, name);
    shell.setSoftkeys({ left: "确定", right: "取消" });
  };

  approach.onConfirm = (actorId) => {
    shell.setSoftkeys({ left: "菜单", right: "菜单" });
    void session.openNpc(actorId);
  };
  approach.onCancel = () => {
    // Keep nearNpcId latch so we don't immediately re-pop until walk-away.
    if (phase === "world") shell.setSoftkeys({ left: "菜单", right: "菜单" });
  };

  view.onXiuTap = () => {
    if (phase !== "world" || session.inBattle) return;
    void session.requestBuffStatus().then((info) => {
      hud.showTip(info);
      setChromeStatus(info.replace(/\s*\n\s*/g, "；").slice(0, 48));
    });
  };

  view.onStatusStripCollapsed = (collapsed) => {
    questTracker.setStripExpanded(!collapsed);
  };
  questTracker.setStripExpanded(!isStatusStripCollapsed());
  questTracker.onOpenTasks = () => {
    if (phase !== "world" || session.inBattle) return;
    void openTaskList();
  };
  hud.onPartyChanged = () => {
    questTracker.setPartyInset(hud.partyOverlayInset());
  };

  view.onPaiTap = () => {
    if (phase !== "world" || session.inBattle) return;
    if (auctionPanel.visible) {
      auctionPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      return;
    }
    void openAuctionPanel();
  };

  view.onLeiTap = () => {
    if (phase !== "world" || session.inBattle) return;
    if (wagerPanel.visible) {
      wagerPanel.close();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      return;
    }
    void openWagerPanel();
  };

  const openEntertainmentPanel = () => {
    dialog.hide();
    closeApproach(false);
    if (auctionPanel.visible) auctionPanel.close();
    if (wagerPanel.visible) wagerPanel.close();
    if (ladderPanel.visible) ladderPanel.close();
    if (guessPanel.visible) guessPanel.close();
    closeActivityPanels();
    if (arenaPanel.visible) arenaPanel.close();
    if (entertainWeb.isExpanded()) entertainWeb.minimize();
    entertainPanel.open();
    // Keep current phase (world or battle) — do not drop out of hangup fight.
    if (phase === "world") {
      showPhase("world");
      syncWorldWalkInput();
    }
    shell.setSoftkeys(entertainPanel.softkeys());
    setChromeStatus("娱乐");
  };

  const openEntertainmentWeb = (entry: EntertainmentEntry) => {
    entertainPanel.close();
    syncWorldWalkInput();
    entertainWeb.open(entry.url, entry.label, {
      videoApi: entry.videoApi,
      videoApiJson: entry.videoApiJson,
      panel: entry.panel,
    });
    syncWorldWalkInput();
    shell.setSoftkeys({ left: "最小化", right: "关闭" });
    setChromeStatus(entry.label);
  };

  const entertainMinimizedStatus = () => "娱乐播放中";

  const applyBattleSoftkeys = () => {
    shell.setSoftkeys(
      battle.isAwaitingRound() ||
        battle.isHangupActive() ||
        battle.isAutoBattleActive()
        ? { left: "", right: "" }
        : {
            left: battle.softkeyLeftLabel(),
            right: battle.softkeyRightLabel(),
          },
    );
  };

  entertainPanel.onPick = (entry) => {
    if (entry.nativePanel === "guess") {
      void openGuessPanel();
      return;
    }
    openEntertainmentWeb(entry);
  };
  entertainPanel.onClose = () => {
    syncWorldWalkInput();
    const overlayOpen =
      guessPanel.visible ||
      festivalPanel.visible ||
      yanmenPanel.visible ||
      ringQuestPanel.visible;
    if (phase === "battle") applyBattleSoftkeys();
    else if (!overlayOpen) shell.setSoftkeys({ left: "菜单", right: "菜单" });
    if (!overlayOpen) {
      setChromeStatus(entertainWeb.isMinimized() ? entertainMinimizedStatus() : "");
    }
  };
  entertainWeb.onClose = () => {
    syncWorldWalkInput();
    if (phase === "battle") {
      applyBattleSoftkeys();
      setChromeStatus("");
    } else if (phase === "world" && !entertainPanel.visible) {
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus("");
    }
  };
  entertainWeb.onMinimize = () => {
    syncWorldWalkInput();
    if (phase === "battle") {
      applyBattleSoftkeys();
      setChromeStatus(entertainMinimizedStatus());
    } else if (phase === "world" && !entertainPanel.visible) {
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus(entertainMinimizedStatus());
    }
  };
  entertainWeb.onRestore = () => {
    if (entertainPanel.visible) entertainPanel.close();
    syncWorldWalkInput();
    shell.setSoftkeys({ left: "最小化", right: "关闭" });
    setChromeStatus(entertainWeb.isExpanded() ? "娱乐" : entertainMinimizedStatus());
  };

  view.onYuTap = () => {
    if (phase !== "world" && phase !== "battle") return;
    if (wingPanel.visible) {
      wingPanel.close();
      syncWorldWalkInput();
    }
    if (entertainWeb.isExpanded()) {
      entertainWeb.minimize();
      return;
    }
    if (entertainWeb.isMinimized()) {
      entertainWeb.restore();
      return;
    }
    if (entertainPanel.visible) {
      entertainPanel.close();
      syncWorldWalkInput();
      if (phase === "battle") applyBattleSoftkeys();
      else shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus(
        entertainWeb.isMinimized() ? entertainMinimizedStatus() : "",
      );
      return;
    }
    if (guessPanel.visible) {
      guessPanel.close();
      syncWorldWalkInput();
      if (phase === "battle") applyBattleSoftkeys();
      else shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus("");
      return;
    }
    openEntertainmentPanel();
  };

  const openTitleList = () => {
    dialog.hide();
    closeApproach(false);
    if (auctionPanel.visible) auctionPanel.close();
    if (wagerPanel.visible) wagerPanel.close();
    if (ladderPanel.visible) ladderPanel.close();
    if (guessPanel.visible) guessPanel.close();
    closeActivityPanels();
    if (arenaPanel.visible) arenaPanel.close();
    if (entertainPanel.visible) entertainPanel.close();
    if (wingPanel.visible) wingPanel.close();
    if (mountPanel.visible) mountPanel.close();
    if (entertainWeb.isExpanded()) entertainWeb.minimize();
    showPhase("social");
    social.openTitles([]);
    showGameTip("称号…");
    void session.openDesignations().then((page) => {
      if (!page) {
        showGameTip("称号列表失败");
        return;
      }
      social.openTitles(
        page.rows.map((r) => ({
          id: r.id,
          name: r.name,
          desc: r.desc,
          equipped: r.id === page.equippedId,
        })),
        { focusId: page.equippedId || undefined },
      );
    });
  };

  view.onChenTap = () => {
    if (phase !== "world") return;
    if (social.visible && social.panelKind === "titles") {
      social.hide();
      showPhase("world");
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      setChromeStatus("");
      return;
    }
    openTitleList();
  };

  const openWingPanel = () => {
    if (phase !== "world") return;
    dialog.hide();
    closeApproach(false);
    if (auctionPanel.visible) auctionPanel.close();
    if (wagerPanel.visible) wagerPanel.close();
    if (ladderPanel.visible) ladderPanel.close();
    if (guessPanel.visible) guessPanel.close();
    closeActivityPanels();
    if (arenaPanel.visible) arenaPanel.close();
    if (entertainPanel.visible) entertainPanel.close();
    if (mountPanel.visible) mountPanel.close();
    if (entertainWeb.isExpanded()) entertainWeb.minimize();
    void (async () => {
      await session.syncCosmetics();
      if (phase !== "world") return;
      wingPanel.open(view.getPlayerWingId(), session.cosmetics.unlockedWing);
      showPhase("world");
      syncWorldWalkInput();
      shell.setSoftkeys(wingPanel.softkeys());
      setChromeStatus(
        session.cosmetics.unlockedWing.length
          ? "翅膀"
          : "翅膀（尚无解锁，请管理端授权）",
      );
    })();
  };

  wingPanel.onPick = (wingId) => {
    void (async () => {
      const ok = await session.equipWing(wingId);
      if (!ok) {
        setChromeStatus("翅膀装备失败（未解锁？）");
        return;
      }
      setChromeStatus(wingId ? `翅膀 ${wingId}` : "翅膀已关");
    })();
  };
  wingPanel.onClose = () => {
    syncWorldWalkInput();
    shell.setSoftkeys({ left: "菜单", right: "菜单" });
    setChromeStatus("");
  };

  view.onChiTap = () => {
    if (phase !== "world") return;
    if (wingPanel.visible) {
      wingPanel.cancel();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      return;
    }
    openWingPanel();
  };

  const openMountPanel = () => {
    if (phase !== "world") return;
    dialog.hide();
    closeApproach(false);
    if (auctionPanel.visible) auctionPanel.close();
    if (wagerPanel.visible) wagerPanel.close();
    if (ladderPanel.visible) ladderPanel.close();
    if (guessPanel.visible) guessPanel.close();
    closeActivityPanels();
    if (arenaPanel.visible) arenaPanel.close();
    if (entertainPanel.visible) entertainPanel.close();
    if (wingPanel.visible) wingPanel.close();
    if (entertainWeb.isExpanded()) entertainWeb.minimize();
    void (async () => {
      await session.syncCosmetics();
      if (phase !== "world") return;
      mountPanel.open(view.getPlayerMountId(), session.cosmetics.unlockedMount);
      showPhase("world");
      syncWorldWalkInput();
      shell.setSoftkeys(mountPanel.softkeys());
      setChromeStatus(
        session.cosmetics.unlockedMount.length
          ? "坐骑"
          : "坐骑（尚无解锁，请管理端授权）",
      );
    })();
  };

  mountPanel.onPick = (mountId) => {
    void (async () => {
      const ok = await session.equipMount(mountId);
      if (!ok) {
        setChromeStatus("坐骑装备失败（未解锁？）");
        return;
      }
      setChromeStatus(mountId ? `坐骑 ${mountId}` : "坐骑已关");
    })();
  };
  mountPanel.onClose = () => {
    syncWorldWalkInput();
    shell.setSoftkeys({ left: "菜单", right: "菜单" });
    setChromeStatus("");
  };

  view.onQiTap = () => {
    if (phase !== "world") return;
    if (mountPanel.visible) {
      mountPanel.cancel();
      syncWorldWalkInput();
      shell.setSoftkeys({ left: "菜单", right: "菜单" });
      return;
    }
    openMountPanel();
  };

  /** Hide map SCE spawn on engage; restore if 4/5 never arrives. */
  const engageSceneMonster = (actorId: number) => {
    if (actorId < 0x70000000) return;
    view.actors.suppressSceneMonster(actorId);
  };
  const rollbackSceneMonsterIfNoBattle = (actorId: number) => {
    if (actorId < 0x70000000) return;
    if (!session.inBattle) view.actors.restoreSceneMonster(actorId);
  };

  view.onNpcTap = (actorId) => {
    // SCE combat markers use high ids — tap starts battle, not 26/1 dialog.
    if (actorId >= 0x70000000) {
      session.sceneMonsters = view.actors.seedSceneMonsters();
      engageSceneMonster(actorId);
      void session
        .startBattleNearPlayer(view.playerX, view.playerY, actorId)
        .then(() => rollbackSceneMonsterIfNoBattle(actorId));
      return;
    }
    // SCE teleport stones (0x60000000+) → 16/1 destination list.
    if (actorId >= 0x60000000 && actorId < 0x70000000) {
      hud.pushChat("[系]传送石: 打开目的地列表…");
      void session.requestTeleportStoneList();
      return;
    }
    // Face-to-face peer tap → same options as 周围玩家 menu.
    const mapPeer = view.actors.peerAt(actorId);
    if (mapPeer) {
      closeApproach(false);
      showPhase("social");
      // Do not openNearbyPlayers here: its page callback calls openNearby()
      // and would wipe the action sheet back to the list.
      social.openNearbyPeerActions(actorId, session.nearbyList, mapPeer);
      return;
    }
    closeApproach(false);
    void session.openNpc(actorId);
  };
  view.onNpcApproach = (actorId, name) => {
    openApproach(actorId, name);
  };
  view.onNpcDepart = () => {
    closeApproach(true);
  };
  view.onMonsterCollide = (actorId) => {
    if (session.inBattle || phase !== "world") return;
    // Hangup loop owns encounters (same 4/1 path); ignore walk-in while armed.
    if (session.hangupDesired) return;
    closeApproach(false);
    session.sceneMonsters = view.actors.seedSceneMonsters();
    setChromeStatus("遭遇怪物！");
    engageSceneMonster(actorId);
    void session
      .startBattleNearPlayer(view.playerX, view.playerY, actorId)
      .then(() => rollbackSceneMonsterIfNoBattle(actorId));
  };
  view.onPortalEnter = (portal) => {
    if (session.inBattle || phase !== "world") return;
    closeApproach(false);
    // exitID = source portal entryId (SCE 0x07). Destination landing is the
    // paired reverse door: dest edge portal with the same entryId → its spawn.
    // Do NOT send targetEntryId (0x13); that often points at a different door.
    const exitId = portal.entryId;
    const label = sceneDisplayName(portal.targetScene);
    hud.pushChat(`[系]传送: ${label}`);
    showGameTip(`传送 → ${label}`);
    view.inputEnabled = false;
    void session
      .requestSceneChange(portal.targetScene, exitId, {
        prelude: portal.kind === "edge" || portal.kind === "meta",
      })
      .finally(() => {
        if (phase === "world") view.inputEnabled = true;
      });
  };

  /** Client-side in-battle auto 4/2 — hangup-alone only; [自动] uses gateway. */
  const fireInBattleAuto = (tip: string): boolean => {
    // Prefer battle UI phase over session.inBattle — mid-fight scene packets
    // can clear the flag while mmBattle is still showing.
    if (
      battle.isArenaDuel() ||
      !session.shouldClientDriveInBattleAuto() ||
      phase !== "battle" ||
      !battle.visible
    ) {
      return false;
    }
    if (!battle.canSubmitOperate()) return false;
    const operate = session.inBattleAutoOperate();
    // Same path as manual 攻击/仙术 (selectedTargetWire + onAttack/onSkill).
    if (!battle.submitAutoOperate(operate, tip)) return false;
    shell.setSoftkeys({ left: "", right: "" });
    return true;
  };

  /** Fire now, or retry until the seat can act (or auto is cancelled). */
  let autoArmTimer: number | null = null;
  /**
   * HUD 挂机按钮打开技能菜单后：选技能即本地 startHangup（客户端遇怪环）。
   * 菜单「挂机战斗」仍只记录，不置此标志。
   */
  let hangupStartAfterSkillPick = false;
  /** Skill-forget DialogPanel: cancel/close returns to skill list (or battle). */
  let skillForgetDialogOpen = false;
  /** Friend-delete DialogPanel: cancel/close returns to friend list. */
  let friendDeleteDialogOpen = false;
  let pendingFriendDelete: { id: number; name: string } | null = null;
  /** Guild kick confirm. */
  let pendingGuildKick: { roleId: number; rank: number; name: string } | null =
    null;
  /** Next successful 10/22 is a force-disband (tip wording). */
  let expectGuildDisbandTip = false;
  /** Bag discard confirm: cancel/close returns to bag. */
  let discardConfirmOpen = false;
  let pendingDiscard: import("./net/protocolExtras").BagItem | null = null;
  /** Dream badge combine: pick target equip then 29/12. */
  let dreamBadgeCombineOpen = false;
  let pendingDreamBadge: import("./net/protocolExtras").BagItem | null = null;

  const returnFromSkillForgetDialog = (): void => {
    skillForgetDialogOpen = false;
    system.setSkills(session.learnedSkills);
    if (session.inBattle) {
      showPhase("battle");
      return;
    }
    showPhase("system");
    system.open("skill");
    system.setSkills(session.learnedSkills);
  };

  const returnFromFriendDeleteDialog = (): void => {
    friendDeleteDialogOpen = false;
    pendingFriendDelete = null;
    social.openFriends(session.friends, session.friendPage, session.friendAllPages);
    showPhase("social");
  };

  const returnFromDiscardConfirm = (): void => {
    discardConfirmOpen = false;
    pendingDiscard = null;
    showPhase("system");
    system.open("bag");
    system.setBag(session.bagItems);
    const cap =
      session.characterStats?.backpackCapacity ||
      Math.max(20, session.bagItems.length);
    system.setBagMeta(session.characterStats?.money ?? session.gold, cap);
  };

  const returnFromDreamBadgeCombine = (): void => {
    dreamBadgeCombineOpen = false;
    pendingDreamBadge = null;
    showPhase("system");
    system.open("bag");
    system.setBag(session.bagItems);
    const cap =
      session.characterStats?.backpackCapacity ||
      Math.max(20, session.bagItems.length);
    system.setBagMeta(session.characterStats?.money ?? session.gold, cap);
  };

  const openDreamBadgeCombineDialog = (
    bag: import("./net/protocolExtras").BagItem,
  ): void => {
    pendingDreamBadge = bag;
    dreamBadgeCombineOpen = true;
    void warmItemCatalogs().then(() => {
      const options = [
        ...DREAM_BADGE_EQUIP_IDS.map((id) => ({
          label: (lookupItemSync(id)?.name || `装备#${id}`).slice(0, 10),
          action: 290,
          value: id,
        })),
        { label: "取消", action: 291, value: 0 },
      ];
      dialog.show({
        title: "合成梦境装备",
        body:
          DREAM_BADGE_EQUIP_IDS.length > 6
            ? "消耗300徽章，选择装备（↑↓翻页）"
            : "消耗300梦境徽章，选择要合成的装备",
        options,
      });
      showPhase("dialog");
      setChromeStatus("选择梦境装备");
    });
  };

  const openHangupSkillDialog = (startAfterPick: boolean): void => {
    hangupStartAfterSkillPick = startAfterPick;
    void (async () => {
      const skills = await session.getBattleSkills();
      // Hangup is PvE farm: only 普攻 + offensive (单敌/敌群). Exclude heals/
      // self-buffs so 鬼道清风拂体 / 幻剑尸鬼召还 are not chosen by mistake.
      const offensive = skills.filter(
        (s) =>
          (s.targetKind === 3 || s.targetKind === 4) && (s.hpChange ?? 0) <= 0,
      );
      const options = [
        { label: "普通攻击", action: 260, value: 0 },
        ...offensive.map((s) => ({
          label: s.name.slice(0, 10),
          action: 260,
          value: s.id + 2,
        })),
        { label: "取消", action: 101, value: 0 },
      ];
      dialog.show({
        title: "挂机战斗",
        body: startAfterPick
          ? offensive.length > 6
            ? "选择技能后开始挂机（↑↓翻页）"
            : "选择技能后开始挂机遇怪"
          : offensive.length > 6
            ? "仅记录技能，点挂机开始（↑↓翻页）"
            : "仅记录技能，点挂机按钮后开始遇怪",
        options,
      });
      showPhase("dialog");
    })();
  };
  const armInBattleAuto = (tip = "挂机出手…") => {
    if (autoArmTimer != null) {
      window.clearTimeout(autoArmTimer);
      autoArmTimer = null;
    }
    const started = performance.now();
    const tick = () => {
      autoArmTimer = null;
      if (
        battle.isArenaDuel() ||
        !session.shouldClientDriveInBattleAuto() ||
        phase !== "battle" ||
        !battle.visible
      ) {
        return;
      }
      if (battle.isAwaitingExit() || battle.isAwaitingDeathPrompt()) return;
      if (fireInBattleAuto(tip)) return;
      if (performance.now() - started > 20000) return;
      autoArmTimer = window.setTimeout(tick, 120);
    };
    tick();
  };

  const endBattleToWorld = (log: string) => {
    // Pull seat vitals before hide() clears battle state.
    const vitals = battle.allyVitals();
    if (vitals) {
      session.hp = vitals.hp;
      session.hpMax = Math.max(vitals.maxHp, vitals.hp, 1);
      session.mp = vitals.mp;
      session.mpMax = Math.max(vitals.maxMp, vitals.mp, 1);
    }
    battle.hide();
    session.inBattle = false;
    touch.releaseAll();
    view.clearHeldKeys();
    view.inputEnabled = true;
    // Engaged SCE spawn stays hidden; wild refresh at birth after 10s.
    // World-boss permanent suppress must not be re-armed.
    void refreshSceneCombatSpawns("post-battle").then(() => {
      view.actors.armSuppressedRespawns(10_000);
      session.sceneMonsters = view.actors.liveSceneMonsters();
    });
    view.armCollideCooldown(2000);
    showPhase("world");
    // HUD was hidden in battle — re-paint top-left bars with post-fight vitals.
    hud.setVitals(session.hp, session.hpMax, session.mp, session.mpMax);
    setChromeStatus(`[${session.transport?.mode}] ${log}`);
    // Refresh SCE pool then continue hangup as another collide-style 4/1.
    session.sceneMonsters = view.actors.liveSceneMonsters();
    session.onLeftBattleForHangup();
    syncWorldStatusBadges();
  };

  const hasRevivalStone = () =>
    session.bagItems.some((it) => it.itemId === 801 && it.count > 0);

  let loginInFlight = false;
  const doLogin = async (credentials?: { user: string; password: string }) => {
    if (loginInFlight) return;
    loginInFlight = true;
    try {
      setChromeStatus("登录中…");
      title.setStatus("正在和服务器通讯请稍候....");
      await session.start(credentials);
      // After login / re-login: land on role select (not world, not stuck on server list).
      const sid =
        session.servers.find((s) => s.serverId === session.selectedServerId)?.serverId ??
        session.servers[0]?.serverId;
      if (sid == null) {
        showPhase("servers");
        setChromeStatus("无可用服务器", true);
        return;
      }
      await session.confirmServer(sid);
      showPhase("role");
      setChromeStatus("请选择角色");
      title.setStatus("请选择角色");
    } catch (err) {
      console.error(err);
      const msg = err instanceof Error ? err.message : String(err);
      title.setStatus(msg);
      setChromeStatus(msg, true);
      // Keep login form visible when account login failed.
      showPhase(credentials ? "login" : "splash");
      // codeVersion gate → offer Gitee APK self-update (Android only).
      if (isClientUpdateRequiredError(msg)) {
        void promptDownloadLatestApk({
          onStatus: (text, error) => {
            title.setStatus(text);
            setChromeStatus(text, !!error);
          },
        });
      }
    } finally {
      loginInFlight = false;
    }
  };

  const confirmServer = async () => {
    const sel = title.selectedServer ?? session.servers[0];
    if (!sel) {
      setChromeStatus("无可用服务器", true);
      return;
    }
    try {
      await session.confirmServer(sel.serverId);
      showPhase("role");
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      title.setStatus(msg);
      setChromeStatus(msg, true);
    }
  };

  // 游客不走空凭据直连；标题「开始游戏/登陆游戏」进登录；登录页可进注册。
  title.onStartGame = () => {
    title.setStatus("请先登录或注册账号");
    showPhase("login");
  };
  title.onOpenLogin = () => {
    title.setStatus("输入账号密码登录");
    showPhase("login");
  };
  title.onOpenRegister = () => {
    title.setStatus("输入账号密码注册");
    showPhase("register");
  };
  title.onSubmitLogin = (user, password) => {
    void doLogin({ user, password });
  };
  let registerInFlight = false;
  title.onSubmitRegister = (user, password) => {
    if (registerInFlight || loginInFlight) return;
    registerInFlight = true;
    title.setStatus("正在注册…");
    setChromeStatus("注册中…");
    void registerAccount({ account: user, password })
      .then((result) => {
        if (!result.ok) {
          title.setStatus(result.error);
          setChromeStatus(result.error, true);
          showPhase("register");
          return;
        }
        title.setCredentials(result.account, password);
        title.setStatus("注册成功，正在登录…");
        setChromeStatus("注册成功，登录中…");
        return doLogin({ user: result.account, password });
      })
      .catch((err) => {
        const msg = err instanceof Error ? err.message : String(err);
        title.setStatus(msg);
        setChromeStatus(msg, true);
        showPhase("register");
      })
      .finally(() => {
        registerInFlight = false;
      });
  };
  title.onCancelLogin = () => showPhase("splash");
  title.onConfirmServer = () => void confirmServer();
  roles.onConfirm = () => {
    const role = roles.selected;
    if (!role) {
      setChromeStatus("请选择角色或创建角色", true);
      return;
    }
    activeRole = role;
    pendingRoles = [role, ...pendingRoles.filter((r) => r.roleId !== role.roleId)];
    // UI-side wipe before enter — party strip / vitals must not flash role A.
    hud.setParty([]);
    hud.setIdentity(role.name, "", "");
    hud.setVitals(500, 500, 200, 200);
    hud.setLevel(role.level || 1, 0.08);
    void hud.setPortraitActor(uiActorResourceForJobSex(role.jobIndex, role.sex));
    setChromeStatus(`进入角色 ${role.name}…`);
    void session.selectRole(role.roleId).catch((err) => {
      const msg = err instanceof Error ? err.message : String(err);
      setChromeStatus(msg, true);
    });
  };
  roles.onCreate = () => {
    showPhase("create-role");
    createRole.open();
    setChromeStatus("选择性别职业并输入名字");
  };
  roles.onDeleteConfirmChange = (open) => {
    if (phase !== "role") return;
    shell.setSoftkeys(open ? { left: "确定", right: "取消" } : { left: "继续", right: "返回" });
  };
  roles.onDelete = () => {
    const role = roles.selected;
    if (!role) {
      roles.setTip("请先选中要删除的角色");
      setChromeStatus("请先选中要删除的角色", true);
      return;
    }
    setChromeStatus(`删除角色 ${role.name}…`);
    void session
      .deleteRole(role.roleId)
      .then(() => {
        pendingRoles = session.roles;
        roles.setRoles(session.roles);
        roles.setTip("OK键删除 · 需输入角色名");
        setChromeStatus(session.roles.length ? "已删除，可选其他角色" : "已删除，可创建角色");
      })
      .catch((err) => {
        const msg = err instanceof Error ? err.message : String(err);
        roles.setTip(msg);
        setChromeStatus(msg, true);
      });
  };
  createRole.onCancel = () => {
    showPhase("role");
    setChromeStatus("选择人物登录");
  };
  createRole.onSubmit = (jobIndex, sex, name) => {
    setChromeStatus(`创建角色 ${name}…`);
    void session
      .createRole(jobIndex, sex, name)
      .then((row) => {
        pendingRoles = session.roles;
        roles.setRoles(session.roles);
        roles.selectRoleId(row.roleId);
        activeRole = row;
        createRole.close();
        showPhase("role");
        setChromeStatus(`已创建 ${row.name}，按继续进入`);
      })
      .catch((err) => {
        const msg = err instanceof Error ? err.message : String(err);
        createRole.showTip(msg);
        setChromeStatus(msg, true);
      });
  };

  menu.onClose = () => showPhase("world");
  menu.onSelect = (leaf: MenuLeaf, label: string) => {
    if (leaf === "leave_game") {
      mapUiMode = "off";
      miniMap.hide();
      worldMap.hide();
      battle.hide();
      battle.setSkills([]);
      dialog.hide();
      system.hide();
      system.setSkills([]);
      system.setBag([]);
      system.setWorn([]);
      enhanceUi.hide();
      social.hide();
      chatPanel.hide();
      tradeUi.hide();
      guildUi.hide();
      createRole.close();
      hud.setParty([]);
      hud.setIdentity("", "", "");
      hud.setVitals(500, 500, 200, 200);
      view.invalidateMap();
      view.actors.clear();
      activeRole = null;
      pendingRoles = [];
      // Full local wipe + disconnect — next login rebuilds from wire only.
      session.logout();
      title.setStatus("已退出登录，请重新登录");
      setChromeStatus("已退出登录");
      showPhase("login");
      return;
    }
    if (leaf === "status") {
      openSystem("status");
      return;
    }
    if (leaf === "bag") {
      openSystem("bag");
      return;
    }
    if (leaf === "quest") {
      void openTaskList();
      return;
    }
    if (leaf === "equip") {
      openSystem("equip");
      return;
    }
    if (leaf === "skill") {
      openSystem("skill");
      return;
    }
    if (leaf === "mall") {
      openSystem("mall");
      return;
    }
    if (leaf === "title") {
      openTitleList();
      return;
    }
    if (leaf === "nearby") {
      showPhase("social");
      social.openNearby(session.nearbyList);
      void session.openNearbyPlayers(0);
      return;
    }
    if (leaf === "friends") {
      showPhase("social");
      social.openFriends(session.friends);
      void session.openFriends(1);
      return;
    }
    if (leaf === "chat") {
      showPhase("chat");
      chatPanel.open(hud.getChatEntries());
      setChromeStatus("聊天 · ←→切换频道");
      return;
    }
    if (leaf === "rank") {
      showPhase("social");
      const fetchGen = social.beginRankFetch();
      social.openRank({
        type: 0,
        tabs: ["等级榜", "财富榜", "PK榜"],
        rows: [],
        myorder: 0,
        fetchGen,
      });
      showGameTip("排行榜…");
      void session.openRanking(0, 0).then((page) => {
        if (!page) {
          showGameTip("排行榜失败");
          return;
        }
        social.openRank({
          type: page.type,
          tabs: page.tabs,
          rows: page.rows.map((r) => ({
            id: r.roleId,
            name: r.name,
            score: r.score,
          })),
          myorder: page.myorder,
          fetchGen,
        });
        showGameTip(`排行榜 · ${page.tabs[page.type] ?? "等级榜"}`);
      });
      return;
    }
    if (leaf === "guild") {
      const name =
        session.guildName ||
        session.characterStats?.guild ||
        "";
      const inGuild = !!session.guildId || (name !== "" && name !== "无帮派");
      guildUi.openHub(inGuild, name || "无帮派", {
        selfRank: session.guildSelfRank,
        selfRoleId: session.selectedRoleId,
      });
      showPhase("guild");
      setChromeStatus("帮派");
      if (inGuild && !session.guildId) void session.ensureGuildId();
      return;
    }
    if (leaf === "unstuck") {
      hud.pushChat("[系]脱离卡死…");
      showGameTip("脱离卡死…");
      showPhase("world");
      view.inputEnabled = false;
      void session.requestUnstuck().finally(() => {
        if (phase === "world") view.inputEnabled = true;
        hud.pushChat(
          `[系]落点 ${sceneDisplayName(session.scene)} @${session.x},${session.y}`,
        );
        showGameTip(`已脱离 · ${session.x},${session.y}`);
      });
      return;
    }
    if (leaf === "leave_team") {
      showPhase("world");
      void session.teamLeave().then(() => {
        hud.pushChat("[队]已离开队伍");
        showGameTip("已离队");
      });
      return;
    }
    if (leaf === "quick_repair") {
      showPhase("world");
      showGameTip("快速修理报价…");
      void session.requestQuickRepairQuote().then((quote) => {
        if (!quote) {
          hud.pushChat("[系]无修理报价");
          showGameTip("无修理报价");
          return;
        }
        if (quote.repairnum <= 0) {
          hud.pushChat("[系]没有需要修理的装备");
          showGameTip("无需修理");
          return;
        }
        dialog.show({
          title: "快速修理",
          body: `修理 ${quote.repairnum} 件装备\n消耗酷宝 ${quote.coolmoney}`,
          options: [
            { label: "确认修理", action: 261, value: 1 },
            { label: "取消", action: 101, value: 0 },
          ],
        });
        showPhase("dialog");
      });
      return;
    }
    if (leaf === "hangup") {
      openHangupSkillDialog(false);
      return;
    }
    if (leaf === "font") {
      const cur = currentUc3FontId();
      dialog.show({
        title: "字体切换",
        body: "选择 LCD 点阵字库",
        options: [
          ...UC3_FONT_OPTIONS.map((o, i) => ({
            label: `${o.id === cur ? "● " : "  "}${o.label} (${o.file})`,
            action: 270,
            value: i,
          })),
          { label: "取消", action: 101, value: 0 },
        ],
      });
      showPhase("dialog");
      return;
    }
    if (leaf === "sound") {
      showPhase("world");
      void toggleSoundEnabled().then((on) => {
        showGameTip(on ? "声音已开启" : "声音已关闭");
        setChromeStatus(on ? "BGM 循环中" : "声音关闭");
      });
      return;
    }
    if (leaf === "peer_fx") {
      const on = togglePeerCosmeticsVisible();
      view.actors.refreshPeerCosmeticsVisibility();
      void battle.refreshDeitySheetForFxPref();
      showPhase("world");
      showGameTip(on ? "已显示特效" : "已屏蔽特效");
      setChromeStatus(
        on ? "特效开：翅/骑/称号 + e_deity.png" : "特效关：翅/骑/称号隐藏 + e_deity.gif",
      );
      return;
    }
    setChromeStatus(`菜单: ${label}`);
    showPhase("world");
  };

  const closeEnhanceToBag = async (tip?: string) => {
    enhanceUi.hide();
    await session.openBag();
    showPhase("system");
    system.open("bag");
    system.setBag(session.bagItems);
    const cap =
      session.characterStats?.backpackCapacity ||
      Math.max(20, session.bagItems.length);
    const money = session.characterStats?.money ?? session.gold;
    system.setBagMeta(money, cap);
    if (tip) showGameTip(tip);
  };

  const refreshEnhancePreview = async () => {
    const mats = enhanceUi.getMaterials();
    const seq = enhanceUi.getEquipSeq();
    if (!seq || !mats.length) {
      enhanceUi.setPreview(0, 0);
      return;
    }
    const prev = await session.previewEnhance(seq, mats);
    if (prev && prev.result === 1) {
      enhanceUi.setPreview(prev.rate, prev.money);
    } else if (prev) {
      enhanceUi.setTip(
        prev.result === 4
          ? "玄晶不足"
          : prev.result === 5
            ? "已达强化上限"
            : "预览失败",
      );
    }
  };

  const openEnhanceFlow = async (bag: import("./net/protocolExtras").BagItem) => {
    if (!bag.seq) {
      system.showTip("装备无效");
      return;
    }
    showGameTip(`强化 ${bag.name}…`);
    const info = await session.openEnhance(bag.seq);
    if (!info || info.result !== 1) {
      const msg =
        info?.result === 2
          ? "装备不存在"
          : info?.result === 3
            ? "已达强化上限"
            : "无法打开强化";
      showGameTip(msg);
      return;
    }
    enhanceUi.openEnhance(bag, session.bagItems, info);
    showPhase("enhance");
  };


  const openWashFlow = async (bag: import("./net/protocolExtras").BagItem) => {
    if (!bag.seq) {
      system.showTip("装备无效");
      return;
    }
    if ((bag.enhance | 0) < 4) {
      showGameTip("需强化到+4才可洗练");
      return;
    }
    showGameTip(`洗练 ${bag.name}…`);
    const info = await session.openEnhanceWash(bag.seq);
    if (!info || info.result !== 1) {
      showGameTip(info?.result === 3 ? "装备不存在" : "无法打开洗练");
      return;
    }
    washUi.openWash(bag, session.bagItems, info);
    showPhase("wash");
  };

  const closeWashToBag = async (tip?: string) => {
    washUi.close();
    await session.openBag();
    system.open("bag");
    showPhase("system");
    if (tip) showGameTip(tip);
  };

  const refreshWashPreview = async () => {
    const seq = washUi.getEquipSeq();
    const unlock = washUi.getSelectedUnlock();
    const crystal = washUi.getSelectedCrystalId();
    const count = washUi.getSelectedCrystalCount();
    if (!seq || !crystal || count <= 0) return;
    const prev = await session.previewEnhanceWash(seq, unlock, crystal, count);
    if (prev && prev.result === 1) {
      washUi.setPreview(prev.rate, prev.money);
    } else if (prev) {
      washUi.setTip(
        prev.result === 4
          ? "玄晶不足"
          : prev.result === 5
            ? "档次未解锁"
            : prev.result === 6
              ? "金钱不足"
              : "预览失败",
      );
    }
  };

  const openRecastFlow = async (bag: import("./net/protocolExtras").BagItem) => {
    if ((bag.enhance | 0) <= 0) {
      showGameTip("未强化装备无法重铸");
      return;
    }
    await session.openBag();
    enhanceUi.openTransfer(bag, session.bagItems);
    showPhase("enhance");
  };

  enhanceUi.onClose = () => {
    void closeEnhanceToBag();
  };
  washUi.onClose = () => {
    void closeWashToBag();
  };
  washUi.onAction = (action) => {
    if (action === "cancel") {
      void closeWashToBag();
      return;
    }
    if (action === "pick_crystal" || action === "back") {
      shell.setSoftkeys(washUi.softkeys());
      if (action === "back" && washUi.getSelectedCrystalId()) {
        void refreshWashPreview();
      }
      return;
    }
    if (action === "submit") {
      void (async () => {
        const seq = washUi.getEquipSeq();
        const unlock = washUi.getSelectedUnlock();
        const crystal = washUi.getSelectedCrystalId();
        const count = washUi.getSelectedCrystalCount();
        showGameTip("洗练中…");
        const res = await session.confirmEnhanceWash(seq, unlock, crystal, count);
        await session.openBag();
        washUi.setBag(session.bagItems);
        const fresh = session.bagItems.find((b) => b.seq === seq);
        const reopen = await session.openEnhanceWash(seq);
        if (reopen && reopen.result === 1 && fresh) {
          washUi.openWash(fresh, session.bagItems, reopen, {
            keepUnlock: unlock,
            keepCrystalId: crystal,
            keepCrystalCount: count,
          });
        }
        if (res.ok) {
          const attrNames: Record<number, string> = {
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
          const name = attrNames[res.attrType ?? 0] ?? `属性${res.attrType ?? 0}`;
          const pct = res.attrFlag ? "%" : "";
          const tip =
            res.attrType && res.attrValue != null
              ? `洗练成功 ${name}+${res.attrValue}${pct}`
              : res.message;
          showGameTip(tip);
          washUi.setTip(tip);
          if (crystal) void refreshWashPreview();
          else washUi.setPreview(0, 0);
        } else {
          washUi.setTip(res.message);
          showGameTip(res.message);
          if (crystal) void refreshWashPreview();
        }
      })();
    }
  };

  const handleWashHorizontal = (delta: number) => {
    if (washUi.adjustCount(delta)) void refreshWashPreview();
  };

  enhanceUi.onAction = (action, payload) => {
    if (action === "cancel") {
      void closeEnhanceToBag();
      return;
    }
    if (action === "place") {
      enhanceUi.enterPickCrystal();
      return;
    }
    if (action === "pick_crystal" && payload) {
      enhanceUi.placeCrystal(payload);
      void refreshEnhancePreview();
      return;
    }
    if (action === "submit") {
      void (async () => {
        const mats = enhanceUi.getMaterials();
        const seq = enhanceUi.getEquipSeq();
        if (!mats.length) {
          enhanceUi.setTip("请放入强化玄晶");
          return;
        }
        enhanceUi.setBusy(true);
        showGameTip("强化中…");
        const res = await session.confirmEnhance(seq, mats);
        enhanceUi.setBusy(false);
        await session.openBag();
        enhanceUi.setBag(session.bagItems);
        if (res.ok) {
          const fresh = session.bagItems.find((b) => b.seq === seq);
          const reopen = await session.openEnhance(seq);
          if (reopen && reopen.result === 1 && fresh) {
            enhanceUi.openEnhance(fresh, session.bagItems, reopen);
            showGameTip(res.message);
          } else {
            await closeEnhanceToBag(res.message);
          }
        } else if (res.result === 2) {
          // Failed roll — crystals consumed; refresh slots & preview
          enhanceUi.clearSlots();
          const reopen = await session.openEnhance(seq);
          if (reopen && reopen.result === 1) {
            const fresh = session.bagItems.find((b) => b.seq === seq);
            if (fresh) enhanceUi.openEnhance(fresh, session.bagItems, reopen);
            else enhanceUi.setOpenInfo(reopen);
          }
          showGameTip(res.message);
        } else {
          showGameTip(res.message);
        }
      })();
      return;
    }
    if (action === "pick_dest" && payload) {
      void (async () => {
        const src = enhanceUi.getSourceSeq();
        const dest = payload;
        enhanceUi.setBusy(true);
        const prev = await session.enhanceTransferPreview(dest, src);
        enhanceUi.setBusy(false);
        if (!prev.ok) {
          showGameTip(prev.message);
          return;
        }
        const destItem =
          session.bagItems.find((b) => b.seq === dest) ||
          ({
            itemId: 0,
            seq: dest,
            count: 1,
            name: prev.name || "目标装备",
            quality: 0,
            enhance: 0,
            kind: "equip",
            category: 0,
            desc: "",
            usable: false,
            equipSlot: -1,
          } as import("./net/protocolExtras").BagItem);
        enhanceUi.showTransferConfirm(destItem);
      })();
      return;
    }
    if (action === "confirm_transfer") {
      void (async () => {
        const src = enhanceUi.getSourceSeq();
        const dest = enhanceUi.getDestSeq();
        if (!src || !dest) return;
        enhanceUi.setBusy(true);
        showGameTip("转移中…");
        const res = await session.enhanceTransferConfirm(dest, src);
        enhanceUi.setBusy(false);
        await closeEnhanceToBag(res.message);
      })();
      return;
    }
    if (action === "cancel_transfer") {
      enhanceUi.cancel();
    }
  };

  system.onClose = () => {
    // Mid-battle overlays (bag/mall/skills) must return to battle, not world.
    if (session.inBattle) {
      showPhase("battle");
      if (session.hp <= 0) {
        battle.resumeDeathPrompt();
        const refreshStoneUi = () => {
          battle.setDeathPromptBag(hasRevivalStone());
          setChromeStatus(
            hasRevivalStone()
              ? "死亡 · 左键复活石原地满血 / 右键普通复活"
              : "死亡 · 左键打开商城购买复活石 / 右键普通复活",
          );
          shell.setSoftkeys({
            left: battle.softkeyLeftLabel(),
            right: battle.softkeyRightLabel(),
          });
        };
        refreshStoneUi();
        void session.openBag().then(refreshStoneUi);
      }
      return;
    }
    showPhase("world");
  };
  system.onAction = (kind, action, item) => {
    if (action === "refresh") return;
    if (kind === "bag" || kind === "enhance") {
      const bag = item && "seq" in item ? (item as import("./net/protocolExtras").BagItem) : null;
      if (!bag) return;
      if (action === "use") {
        void (async () => {
          /* 江湖追杀令：打开恩怨选目标，不直接 7/1. */
          if (bag.itemId === 847) {
            system.hide();
            showPhase("social");
            social.openEnmity(session.enmityRows, session.enmityDir, bag.seq, 1, 1);
            void session.openEnmity(0, 1);
            showGameTip("请选择仇人使用追杀令");
            return;
          }
          showGameTip(`使用 ${bag.name}…`);
          const fb = await session.useItem(bag.itemId, bag.seq);
          // Success: refresh bag from server. Failure: keep local stack
          // (CBE only deducts on result=1; do not openBag-resync a fail).
          if (fb.ok) await session.openBag();
          showGameTip(fb.message || (fb.ok ? "使用成功" : "使用失败"));
        })();
        return;
      }
      if (action === "combine") {
        if (isDreamBadge(bag.itemId)) {
          openDreamBadgeCombineDialog(bag);
          return;
        }
        void (async () => {
          showGameTip(`合成 ${bag.name}…`);
          const fb = isCrystalFragment(bag.itemId)
              ? await session.combineCrystalFragment(bag.itemId, bag.seq)
              : await session.combineCrystal(bag.itemId, bag.seq);
          if (fb.ok) {
            await session.openBag();
            system.setBag(session.bagItems);
            const cap =
              session.characterStats?.backpackCapacity ||
              Math.max(20, session.bagItems.length);
            system.setBagMeta(
              session.characterStats?.money ?? session.gold,
              cap,
            );
          }
          showGameTip(fb.message || (fb.ok ? "合成成功" : "合成失败"));
        })();
        return;
      }
      if (action === "batch_open") {
        void (async () => {
          showGameTip(`批量开启 ${bag.name}×10…`);
          const fb = await session.openChestBatch(bag.itemId, bag.seq, 10);
          // openChestBatch already openBag()'s and builds tip from bag delta.
          system.setBag(session.bagItems);
          const cap =
            session.characterStats?.backpackCapacity ||
            Math.max(20, session.bagItems.length);
          system.setBagMeta(
            session.characterStats?.money ?? session.gold,
            cap,
          );
          showGameTip(fb.message || (fb.ok ? "开启成功" : "开启失败"));
        })();
        return;
      }
      if (action === "equip") {
        const job = session.characterStats?.job ?? 0;
        if (!jobAllowsWeaponCategory(job, bag.category)) {
          showGameTip(weaponJobRestrictTip(bag.category));
          return;
        }
        void session.equipItem(bag.itemId, bag.seq).then(() => void session.openBag());
        showGameTip(`装备 ${bag.name}`);
        return;
      }
      if (action === "enhance") {
        void openEnhanceFlow(bag);
        return;
      }
      if (action === "wash") {
        void openWashFlow(bag);
        return;
      }
      if (action === "recast") {
        void openRecastFlow(bag);
        return;
      }
      if (action === "discard") {
        pendingDiscard = bag;
        discardConfirmOpen = true;
        const vis = Math.max(1, bagVisibleStackCount(bag));
        const qty = vis > 1 ? `×${vis}` : "";
        dialog.show({
          title: "确认丢弃",
          body: `确定丢弃「${bag.name}」${qty}？\n丢弃后无法找回。`,
          options: [
            { label: "确认丢弃", action: 282, value: 0 },
            { label: "取消", action: 283, value: 0 },
          ],
        });
        showPhase("dialog");
        setChromeStatus("确认丢弃物品");
        return;
      }
      if (action === "auction") {
        system.visible = false;
        showPhase("world");
        void openAuctionPanel({ preferTab: "list", preselect: bag }).then(() => {
          showGameTip(`已选 ${bag.name}，设置金额后确认上架`);
        });
        return;
      }
    }
    if (kind === "equip") {
      const bag = item && "seq" in item ? (item as import("./net/protocolExtras").BagItem) : null;
      if (!bag) return;
      const refreshEquip = () =>
        Promise.all([session.openWornEquipment(), session.openBag()]);
      if (action === "unequip") {
        const bodySeq = system.getEquipFocusBodySeq();
        void session
          .unequipItem(bag.itemId, bodySeq)
          .then(() => refreshEquip());
        showGameTip(`卸载 ${bag.name}`);
        return;
      }
      if (action === "equip") {
        const job = session.characterStats?.job ?? 0;
        if (!jobAllowsWeaponCategory(job, bag.category)) {
          showGameTip(weaponJobRestrictTip(bag.category));
          return;
        }
        void session.equipItem(bag.itemId, bag.seq).then(() => refreshEquip());
        showGameTip(`装备 ${bag.name}`);
        return;
      }
      if (action === "swap") {
        const job = session.characterStats?.job ?? 0;
        if (!jobAllowsWeaponCategory(job, bag.category)) {
          showGameTip(weaponJobRestrictTip(bag.category));
          return;
        }
        void session
          .swapEquip(system.getEquipFocusBodySeq(), bag.seq)
          .then(() => refreshEquip());
        showGameTip(`替换 ${bag.name}`);
        return;
      }
      if (action === "enhance") {
        showGameTip("穿戴中装备请先卸下再强化");
        return;
      }
    }
    if (kind === "mall" && action === "buy" && item && "price" in item) {
      const g = item as import("./net/protocolExtras").ShopGoods;
      const n = system.getMallBuyQty();
      const total = (g.price >>> 0) * (n >>> 0);
      const bal = session.mallState?.coolmoney ?? 0;
      if (total > 0 && bal < total) {
        showGameTip("酷宝不够请右下角赞助");
        return;
      }
      void session.buyMallItem(g.itemId, n).then((r) => {
        if (r.ok) {
          showGameTip(`购买 ${g.name} ×${n}`);
          return;
        }
        showGameTip(r.message || "购买失败");
      });
      return;
    }
    if (
      kind === "mall" &&
      (action === "recharge_alipay" || action === "recharge_wechat")
    ) {
      const yuan = system.getRechargeYuan();
      const payType: RechargePayType =
        action === "recharge_wechat" ? 1 : 2;
      const creds = session.getCredentials();
      const roleId = session.selectedRoleId >>> 0;
      const account = (creds.user || "").trim();
      const password = (creds.password || "").trim();
      if (!account || !roleId || yuan < 1) {
        showGameTip("无法赞助：请重新登录");
        return;
      }
      const channel = payType === 1 ? "微信" : "支付宝";
      showGameTip(`创建${channel}赞助单…`);
      void (async () => {
        const result = await createRechargeOrder({
          account,
          password,
          roleId,
          yuan,
          payType,
        });
        if (!result.ok) {
          showGameTip(result.error);
          return;
        }
        const amountText = formatRechargeYuan(result.reallyPriceCents);
        const tip = `应付 ${amountText} · ${result.wcoinAmount}酷宝`;
        showGameTip(`${tip} · 打开支付…`);
        const pay = await openPayUrl(result.payUrl, {
          amountText,
          channel,
        });
        if (pay.opened && pay.showedQr) {
          showGameTip(`${tip} · 已开${channel}，也可扫码`);
        } else if (pay.showedQr) {
          showGameTip(`${tip} · 请扫码（个人收款码）`);
        } else if (pay.opened) {
          showGameTip(`${channel}支付已打开 · 完成后重开商城刷新`);
        } else {
          showGameTip("订单已创建，但无法打开支付");
        }
        void session.openMall();
      })();
      return;
    }
    if (kind === "skill" && action === "skill_use") {
      const sk = item && "id" in item && !("itemId" in item)
        ? (item as import("./resources/skill").BattleSkillRow)
        : session.learnedSkills[0];
      if (!sk) {
        showGameTip("无已学仙术");
        return;
      }
      if (!session.inBattle) {
        if (!isMapHealSkill(sk)) {
          showGameTip("仙术需在战斗中施放");
          return;
        }
        void (async () => {
          showGameTip(`施展${sk.name}…`);
          const fb = await session.useMapHealSkill(sk.id);
          showGameTip(fb.message || (fb.ok ? "使用成功" : "使用失败"));
        })();
        return;
      }
      battle.noteOperateSubmitted(`施展${sk.name}…`);
      if (battle.isAwaitingRound()) {
        shell.setSoftkeys({ left: "", right: "" });
      }
      void session.battleOperate(battle.targetIndex(), sk.id + 2);
      showPhase("battle");
    }
    if (kind === "skill" && action === "skill_forget") {
      const sk =
        item && "id" in item && !("itemId" in item)
          ? (item as import("./resources/skill").BattleSkillRow)
          : null;
      if (!sk) {
        showGameTip("无已学仙术");
        return;
      }
      skillForgetDialogOpen = true;
      dialog.show({
        title: "遗忘技能",
        body: `遗忘「${sk.name}」需消耗5金\n确定要遗忘吗？`,
        options: [
          { label: "确认遗忘", action: 280, value: sk.id },
          { label: "取消", action: 281, value: 0 },
        ],
      });
      showPhase("dialog");
    }
  };

  social.onClose = () => showPhase("world");
  taskUi.onClose = () => showPhase("world");
  taskUi.onAction = (action, taskId) => {
    if (action === "refresh") {
      void session.fetchTaskLists().then((lists) => {
        taskUi.setLists(lists.accepted, lists.candidates);
        refreshQuestTracker();
        showGameTip(
          `已接${lists.accepted.length} · 可接${lists.candidates.length}`,
        );
      });
      return;
    }
    if (action === "detail" && taskId) {
      void session.fetchTaskDetailText(taskId).then((text) => {
        taskUi.showDetail(text || "(无详情)");
      });
      return;
    }
    if (action === "abandon" && taskId) {
      void session.abandonTask(taskId).then(async (r) => {
        showGameTip(r.ok ? "已放弃" : "放弃失败");
        const lists = await session.fetchTaskLists();
        taskUi.setLists(lists.accepted, lists.candidates);
        refreshQuestTracker();
      });
      return;
    }
    if (action === "accept" && taskId) {
      void session.acceptTask(taskId).then(async () => {
        showGameTip("已接取");
        const lists = await session.fetchTaskLists();
        taskUi.setLists(lists.accepted, lists.candidates);
        refreshQuestTracker();
      });
    }
  };
  chatPanel.onClose = () => showPhase("world");
  chatPanel.onSend = async (text, type, opts) => {
    const r = await session.sendChat(text, type, opts);
    if (r.tip) showGameTip(r.tip);
    if (!r.ok) {
      if (!r.tip) showGameTip("发送失败");
      return false;
    }
    chatPanel.setEntries(hud.getChatEntries());
    const tipPrefix = type === 7 ? "私聊" : "已发送";
    showGameTip(`${tipPrefix} · ${text.slice(0, 12)}`);
    return true;
  };
  social.onAction = (kind, action, row) => {
    if (action === "player_info" && row?.id) {
      openPlayerCard(row.id, row.name);
      return;
    }
    if (action === "view_equip" && row?.id) {
      void session.requestOtherEquip(row.id);
      showGameTip(`装备 ${row.name}`);
      return;
    }
    if (action === "invite_team" && row?.id) {
      void session.teamInvite(row.id);
      hud.pushChat(`[队]已邀请 ${row.name}`);
      showGameTip(`组队邀请 ${row.name}`);
      return;
    }
    if (action === "add_friend" && row?.id) {
      void session.friendAdd(row.id);
      hud.pushChat(`[友]已发送好友邀请 ${row.name}`);
      showGameTip(`加好友 ${row.name}`);
      return;
    }
    if (action === "delete_friend" && row?.id) {
      friendDeleteDialogOpen = true;
      pendingFriendDelete = { id: row.id >>> 0, name: row.name || "玩家" };
      dialog.show({
        title: "删除好友",
        body: `确定将「${pendingFriendDelete.name}」从好友列表中删除？\n删除好友将同时清除友好度`,
        options: [
          { label: "确定", action: 236, value: pendingFriendDelete.id },
          { label: "取消", action: 237, value: 0 },
        ],
      });
      showPhase("dialog");
      return;
    }
    if (action === "trade" && row?.id) {
      void session.tradeInvite(row.id);
      hud.pushChat(`[交]已邀请 ${row.name} 交易`);
      showGameTip(`交易邀请 ${row.name}`);
      return;
    }
    if (action === "private_chat" && row?.id) {
      social.hide();
      chatPanel.selfRoleId = session.selectedRoleId >>> 0;
      showPhase("chat");
      chatPanel.open(hud.getChatEntries(), {
        pm: { id: row.id >>> 0, name: row.name || "玩家" },
      });
      showGameTip(`私聊 ${row.name}`);
      return;
    }
    if (action === "spar" && row?.id) {
      const leaf = (session.scene || "").split(/[/\\]/).pop() || "";
      const city =
        /^c\d/i.test(leaf) ||
        (/^[1-9]/.test(leaf) && (leaf.charCodeAt(1) || 0) >= 0x80);
      if (city) {
        hud.pushChat("[系]城镇中无法切磋");
        showGameTip("城镇中无法切磋");
        return;
      }
      if ((session.level | 0) < 25) {
        hud.pushChat("[系]等级不足25级无法切磋");
        showGameTip("等级不足25级无法切磋");
        return;
      }
      if ((session.pkPoints | 0) >= PK_POINTS_MAX) {
        hud.pushChat("[系]PK值已满，无法发起切磋");
        showGameTip("PK值已满，无法发起切磋");
        return;
      }
      social.hide();
      void session.sparChallenge(row.id).then(() => {
        if (session.inBattle) showPhase("battle");
        else showPhase("world");
      });
      showGameTip(`切磋 ${row.name}`);
      return;
    }
    if (action === "guild_invite" && row?.id) {
      void session.guildInvite(row.id);
      showGameTip(`入帮邀请 ${row.name}`);
      return;
    }
    if (action === "leave_team") {
      void session.teamLeave().then(() => {
        social.openTeam(session.teamMembers, session.teamLeaderId);
      });
      return;
    }
    if (action === "refresh") {
      if (kind === "team") void session.refreshTeamRoster();
      else if (kind === "friends") void session.openFriends(1);
      else if (kind === "enmity")
        void session.openEnmity(social.enmityDirection, 1);
      else void session.openNearbyPlayers(0);
      return;
    }
    if (action === "friend_hub_tab" && row) {
      if (row.id === 0) {
        social.clearHuntPick();
        social.openFriends(session.friends, 1, session.friendAllPages);
        void session.openFriends(1);
      } else {
        social.openEnmity(
          session.enmityRows,
          social.enmityDirection,
          social.huntOrderPickSeq,
          1,
          session.enmityAllPages,
        );
        void session.openEnmity(social.enmityDirection, 1);
      }
      return;
    }
    if (action === "enmity_dir_tab" && row) {
      void session.openEnmity(row.id, 1);
      return;
    }
    if (action === "list_page" && row?.id) {
      const page = Math.max(1, row.id | 0);
      if (kind === "friends") void session.openFriends(page);
      else if (kind === "enmity")
        void session.openEnmity(social.enmityDirection, page);
      return;
    }
    if (action === "hunt_order" && row?.id) {
      const seq = social.huntOrderPickSeq;
      if (!seq) {
        showGameTip("请从背包使用追杀令");
        return;
      }
      void (async () => {
        showGameTip(`追查 ${row.name}…`);
        const fb = await session.useHuntOrder(seq, row.id);
        social.clearHuntPick();
        if (fb.ok) {
          await session.openBag();
          social.hide();
          showPhase("world");
        } else {
          /* Keep picker open so player can try another (e.g. offline). */
          void session.openEnmity(social.enmityDirection, social.serverListPage);
        }
        showGameTip(fb.message || (fb.ok ? "使用成功" : "使用失败"));
      })();
      return;
    }
    if (action === "invite_pick") {
      social.openNearby(session.nearbyList);
      void session.openNearbyPlayers(0);
      showGameTip("选择邀请目标");
      return;
    }
    if (action === "equip_title" && row && row.id >= 0) {
      void session.selectDesignation(row.id).then((r) => {
        if (r.ok) {
          const titleName = r.name || row.name.replace(/^\★/, "");
          hud.pushChat(`[称]已装备 ${titleName}`);
          showGameTip(`称号 ${titleName}`);
          const page = session.designationPage;
          if (page) {
            social.openTitles(
              page.rows.map((t) => ({
                id: t.id,
                name: t.name,
                desc: t.desc,
                equipped: t.id === page.equippedId,
              })),
              { focusId: page.equippedId || row.id },
            );
          }
          /* Nameplate refresh is driven by onDesignationUpdate from 23/2. */
        } else {
          showGameTip("称号设置失败");
        }
      });
      return;
    }
    if (action === "send_chat") {
      showPhase("chat");
      chatPanel.open(hud.getChatEntries());
      return;
    }
    if (action === "rank_tab") {
      const type = row?.id ?? 0;
      showGameTip(`排行榜 · ${row?.name ?? ""}…`);
      const fetchGen = social.beginRankFetch();
      /* 切榜期间列表已清空；失败时给空榜，避免残留上一榜分数. */
      void session.openRanking(type, 0).then((page) => {
        if (!page) {
          social.openRank({
            type,
            tabs: ["等级榜", "财富榜", "PK榜"],
            rows: [],
            myorder: 0,
            fetchGen,
          });
          showGameTip("排行榜失败");
          return;
        }
        social.openRank({
          type: page.type,
          tabs: page.tabs,
          rows: page.rows.map((r) => ({
            id: r.roleId,
            name: r.name,
            score: r.score,
          })),
          myorder: page.myorder,
          fetchGen,
        });
        showGameTip(
          `排行榜 · ${page.tabs[page.type] ?? row?.name ?? "等级榜"}`,
        );
      });
    }
  };

  guildUi.onClose = () => showPhase("world");
  guildUi.onAction = (action, payload, extra) => {
    if (action === "open_list") {
      void session.openGuildList(1);
      return;
    }
    if (action === "open_create") {
      showPhase("world");
      showGameTip("创建帮派…");
      void session.guildCreateStart();
      return;
    }
    if (action === "open_self") {
      void (async () => {
        if (!session.guildId) await session.ensureGuildId();
        if (session.guildId) await session.openGuildSelfDetail();
        else showGameTip("未加入帮派");
      })();
      return;
    }
    if (action === "open_members") {
      void session.openGuildMembers(1);
      return;
    }
    if (action === "open_apps") {
      void session.openGuildApplications(1);
      return;
    }
    if (action === "leave") {
      dialog.show({
        title: "脱离帮派",
        body: "是否确定脱离帮派？",
        options: [
          { label: "确定", action: 252, value: 1 },
          { label: "取消", action: 253, value: 0 },
        ],
      });
      showPhase("dialog");
      return;
    }
    if (action === "disband") {
      dialog.show({
        title: "解散帮派",
        body: "确定解散本帮？\n全体成员将失去帮派",
        options: [
          { label: "确定解散", action: 254, value: 1 },
          { label: "取消", action: 255, value: 0 },
        ],
      });
      showPhase("dialog");
      return;
    }
    if (action === "kick" && payload && extra) {
      const name = guildUi.peekSelectedMemberName || `角色${payload}`;
      pendingGuildKick = { roleId: payload >>> 0, rank: extra >>> 0, name };
      dialog.show({
        title: "逐出帮派",
        body: `将「${name}」逐出帮派？`,
        options: [
          { label: "确定", action: 256, value: payload },
          { label: "取消", action: 257, value: 0 },
        ],
      });
      showPhase("dialog");
      return;
    }
    if (action === "detail" && payload) {
      void session.openGuildDetail(payload);
      return;
    }
    if (action === "apply" && payload) {
      void session.guildApply(payload);
      return;
    }
    if (action === "approve" && payload) {
      void session.guildReview(payload, true);
      return;
    }
    if (action === "reject" && payload) {
      void session.guildReview(payload, false);
      return;
    }
    if (action === "prev_page" || action === "next_page") {
      const delta = action === "next_page" ? 1 : -1;
      const kind = session.guildBrowseKind || "list";
      if (kind === "list") {
        void session.openGuildList(Math.max(1, session.guildListPage + delta));
      } else if (kind === "members") {
        void session.openGuildMembers(Math.max(1, session.guildMemberPage + delta));
      } else {
        void session.openGuildApplications(Math.max(1, session.guildAppPage + delta));
      }
    }
  };

  tradeUi.onClose = () => showPhase("world");
  tradeUi.onSoftkeys = () => {
    if (phase === "trade") shell.setSoftkeys(tradeUi.softkeys());
  };
  tradeUi.onAction = (action, payload) => {
    if (action === "add_bag_item" && payload && "itemId" in payload) {
      // Qty is chosen in-panel with ←→; no prompt / left-softkey input.
      tradeUi.addFromBag(payload as import("./net/protocolExtras").BagItem);
      return;
    }
    if (action === "set_money") {
      const max = session.characterStats?.money ?? session.gold;
      tradeUi.beginSetMoney(max);
      return;
    }
    if (action === "submit") {
      const offer = tradeUi.localOffer;
      if (!offer.money && !offer.items.length) {
        showGameTip("请先放入物品或金币");
        return;
      }
      void session.tradeOffer(offer.money, offer.items);
      return;
    }
    if (action === "confirm") {
      void session.tradeConfirm();
      return;
    }
    if (action === "cancel") {
      void session.tradeCancel().then(() => {
        tradeUi.hide();
        showPhase("world");
      });
    }
  };

  dialog.onClose = () => {
    hangupStartAfterSkillPick = false;
    if (skillForgetDialogOpen) {
      returnFromSkillForgetDialog();
      return;
    }
    if (friendDeleteDialogOpen) {
      returnFromFriendDeleteDialog();
      return;
    }
    if (discardConfirmOpen) {
      returnFromDiscardConfirm();
      return;
    }
    if (dreamBadgeCombineOpen) {
      returnFromDreamBadgeCombine();
      return;
    }
    // Closing approach prompt while still in range keeps latch (no spam).
    showPhase("world");
  };
  dialog.onMedicineBuyCancel = () => {
    void session.pickNpcOption(1, 0xe7000001, "药品").then(() => {
      if (!dialog.visible) showPhase("world");
    });
  };
  dialog.onPick = (action, value, label) => {
    setChromeStatus(`NPC ${label} a=${action} v=${value >>> 0}`);
    if (dialog.isMedicineBuyQty()) {
      const st = dialog.getMedicineBuyQty();
      if (!st) return;
      const wire = encodeNpcBuyItem(st.itemId, st.qty);
      dialog.hide();
      void session.pickNpcOption(1, wire, label).then(() => {
        if (session.inBattle) {
          showPhase("battle");
          return;
        }
        if (!dialog.visible) showPhase("world");
      });
      return;
    }
    /* NPC 药商：点购买行先调数量，再确认发 BUY_ITEM|(count<<16)|id */
    if (
      action === 1 &&
      npcServiceOpcode(value) === NpcServiceOpcode.BUY_ITEM &&
      label !== "购买该装备"
    ) {
      const itemId = decodeNpcBuyItemId(value);
      const cat = lookupItemSync(itemId);
      if (!cat || cat.kind !== "equip") {
        const m = /^购买(.+)\s+(\d+)(铜|酷宝)/.exec(label);
        const itemName = m?.[1]?.trim() || cat?.name || `物品${itemId}`;
        const unitPrice = m ? Number(m[2]) : cat?.price || 0;
        const currency = m?.[3] || "铜";
        const money = (session.characterStats?.money ?? session.gold) >>> 0;
        const maxByMoney = unitPrice > 0 ? Math.floor(money / unitPrice) : 0;
        const hasStack = session.bagItems.some((b) => b.itemId === itemId);
        const cap =
          session.characterStats?.backpackCapacity ||
          Math.max(20, session.bagItems.length);
        const free = Math.max(0, (cap | 0) - session.bagItems.length);
        if (!hasStack && free < 1) {
          showGameTip("背包已满，无法购买");
          return;
        }
        if (maxByMoney < 1) {
          showGameTip(currency === "酷宝" ? "酷宝不足" : "铜钱不足");
          return;
        }
        const maxQty = Math.min(255, maxByMoney);
        dialog.showMedicineBuyQty({
          itemId,
          itemName,
          unitPrice,
          currency,
          qty: 1,
          maxQty,
        });
        shell.setSoftkeys({ left: "确定", right: "返回" });
        return;
      }
    }
    // Synthetic confirm options from task/challenge prompts.
    if (action === 100) {
      dialog.hide();
      void session.confirmTaskPrompt(true).then(() => {
        if (!session.inBattle) showPhase("world");
      });
      return;
    }
    if (action === 101) {
      hangupStartAfterSkillPick = false;
      if (skillForgetDialogOpen) {
        dialog.hide();
        returnFromSkillForgetDialog();
        return;
      }
      if (friendDeleteDialogOpen) {
        dialog.hide();
        returnFromFriendDeleteDialog();
        return;
      }
      if (dreamBadgeCombineOpen) {
        dialog.hide();
        returnFromDreamBadgeCombine();
        return;
      }
      dialog.hide();
      void session.confirmTaskPrompt(false);
      showPhase("world");
      return;
    }
    if (action === 102) {
      dialog.hide();
      void session.confirmChallenge(true).then(() => {
        if (session.inBattle) showPhase("battle");
        else showPhase("world");
      });
      return;
    }
    if (action === 103) {
      dialog.hide();
      void session.confirmChallenge(false);
      showPhase("world");
      return;
    }
    if (action === 220 || action === 221) {
      dialog.hide();
      void session.teamReply(action === 220).then(() => showPhase("world"));
      return;
    }
    if (action === 230 || action === 231) {
      dialog.hide();
      void session.friendReply(action === 230).then(() => showPhase("world"));
      return;
    }
    if (action === 236 || action === 237) {
      dialog.hide();
      const pending = pendingFriendDelete;
      friendDeleteDialogOpen = false;
      pendingFriendDelete = null;
      if (action === 236 && pending?.id) {
        const name = pending.name;
        void session.friendDelete(pending.id).then(() => {
          hud.pushChat(`[友]已删除好友 ${name}`);
          showGameTip(`已删除 ${name}`);
          social.openFriends(
            session.friends,
            session.friendPage,
            session.friendAllPages,
          );
          showPhase("social");
        });
        return;
      }
      returnFromFriendDeleteDialog();
      return;
    }
    if (action === 242 || action === 243) {
      dialog.hide();
      void session.tradeReply(action === 242).then(() => {
        if (session.inTrade) {
          /* onSocialNotice may reopen trade dialog */
          return;
        }
        showPhase("world");
      });
      return;
    }
    if (action === 258 || action === 259) {
      dialog.hide();
      void session.guildInviteReply(action === 258).then(() => showPhase("world"));
      return;
    }
    if (action === 244) {
      dialog.hide();
      void session.tradeCancel().then(() => showPhase("world"));
      showGameTip("已取消交易");
      return;
    }
    if (action === 250) {
      dialog.hide();
      void session.guildCreateCommit().then(() => showPhase("world"));
      return;
    }
    if (action === 251) {
      dialog.hide();
      session.pendingGuildCreateName = "";
      showGameTip("已取消创建帮派");
      showPhase("world");
      return;
    }
    if (action === 252) {
      dialog.hide();
      void session.guildLeave().then(() => {
        if (phase !== "guild") showPhase("guild");
      });
      return;
    }
    if (action === 253) {
      dialog.hide();
      showPhase("guild");
      return;
    }
    if (action === 254) {
      dialog.hide();
      expectGuildDisbandTip = true;
      void session.guildDisband().then(() => {
        if (!session.inGuild) {
          guildUi.openHub(false, "无帮派");
          showPhase("guild");
        } else {
          expectGuildDisbandTip = false;
          showPhase("guild");
          showGameTip("解散失败（需帮主）");
        }
      });
      return;
    }
    if (action === 255) {
      dialog.hide();
      showPhase("guild");
      return;
    }
    if (action === 256) {
      dialog.hide();
      const pending = pendingGuildKick;
      pendingGuildKick = null;
      if (pending?.roleId && pending.rank) {
        void session.guildKick(pending.roleId, pending.rank).then(() => {
          showPhase("guild");
          showGameTip(`已逐出 ${pending.name}`);
        });
        return;
      }
      showPhase("guild");
      return;
    }
    if (action === 257) {
      dialog.hide();
      pendingGuildKick = null;
      showPhase("guild");
      return;
    }
    if (action === 260) {
      dialog.hide();
      session.hangupSkillOperate = value | 0;
      if (session.hangupDesired) session.markHangupDesired();
      // Keep in-battle [自动] in sync with the configured hangup skill.
      session.lastBattleOperate = value | 0;
      const tip =
        value === 0
          ? "普通攻击"
          : session.learnedSkills.find((s) => s.id + 2 === value)?.name ||
            `技能#${value}`;
      const startNow = hangupStartAfterSkillPick;
      hangupStartAfterSkillPick = false;
      if (startNow) {
        hud.pushChat(`[挂]技能 ${tip}，开始挂机…`);
        showGameTip(`挂机技能: ${tip}`);
        showPhase("world");
        void session.startHangup();
        syncWorldStatusBadges();
        return;
      }
      hud.pushChat(`[挂]已记录技能 ${tip}，点挂机开始`);
      showGameTip(`挂机技能已记录: ${tip}`);
      showPhase("world");
      return;
    }
    // Dream-badge combine only — do not swallow other dialogs that reused 290/291.
    if (action === 290 && dreamBadgeCombineOpen) {
      dialog.hide();
      const bag = pendingDreamBadge;
      dreamBadgeCombineOpen = false;
      pendingDreamBadge = null;
      if (!bag) {
        returnFromDreamBadgeCombine();
        return;
      }
      const destId = value >>> 0;
      const destName = lookupItemSync(destId)?.name || label || `装备#${destId}`;
      void (async () => {
        showGameTip(`合成 ${destName}…`);
        const fb = await session.combineDreamBadge(bag.itemId, bag.seq, destId);
        if (fb.ok) await session.openBag();
        system.setBag(session.bagItems);
        const cap =
          session.characterStats?.backpackCapacity ||
          Math.max(20, session.bagItems.length);
        system.setBagMeta(
          session.characterStats?.money ?? session.gold,
          cap,
        );
        showGameTip(fb.message || (fb.ok ? "合成成功" : "合成失败"));
        showPhase("system");
        system.open("bag");
      })();
      return;
    }
    if (action === 291 && dreamBadgeCombineOpen) {
      dialog.hide();
      returnFromDreamBadgeCombine();
      return;
    }
    if (action === 261) {
      dialog.hide();
      void session.confirmQuickRepair().then((r) => {
        hud.pushChat(`[系]${r.message}`);
        showGameTip(r.message);
        showPhase("world");
      });
      return;
    }
    if (action === 280) {
      dialog.hide();
      skillForgetDialogOpen = false;
      const skillId = value >>> 0;
      void (async () => {
        showGameTip("遗忘中…");
        const fb = await session.forgetSkill(skillId);
        system.setSkills(session.learnedSkills);
        const money = session.characterStats?.money ?? session.gold;
        const cap =
          session.characterStats?.backpackCapacity ||
          Math.max(20, session.bagItems.length);
        system.setBagMeta(money, cap);
        showGameTip(fb.message || (fb.ok ? "遗忘成功" : "遗忘失败"));
        if (session.inBattle) {
          showPhase("battle");
          return;
        }
        showPhase("system");
        system.open("skill");
        system.setSkills(session.learnedSkills);
      })();
      return;
    }
    if (action === 281) {
      dialog.hide();
      returnFromSkillForgetDialog();
      return;
    }
    if (action === 282) {
      dialog.hide();
      discardConfirmOpen = false;
      const bag = pendingDiscard;
      pendingDiscard = null;
      if (!bag) {
        returnFromDiscardConfirm();
        return;
      }
      void (async () => {
        showGameTip(`丢弃 ${bag.name}…`);
        try {
          await session.discardItem(bag.itemId, bag.seq, bag.count);
          await session.openBag();
          system.setBag(session.bagItems);
          const cap =
            session.characterStats?.backpackCapacity ||
            Math.max(20, session.bagItems.length);
          system.setBagMeta(
            session.characterStats?.money ?? session.gold,
            cap,
          );
          showGameTip(`已丢弃 ${bag.name}`);
        } catch (e) {
          console.warn("[bag] discard failed", e);
          showGameTip("丢弃失败，请重试");
        }
        showPhase("system");
        system.open("bag");
      })();
      return;
    }
    if (action === 283) {
      dialog.hide();
      returnFromDiscardConfirm();
      return;
    }
    if (action === 270) {
      dialog.hide();
      const opt = UC3_FONT_OPTIONS[value | 0];
      if (!opt) {
        showPhase("world");
        return;
      }
      void (async () => {
        try {
          clearUc3TexCache();
          await applyUc3FontId(opt.id as Uc3FontId);
          await refreshAllUc3Labels();
          showGameTip(`字体: ${opt.label}`);
          hud.pushChat(`[系]字体已切换为 ${opt.label} (${opt.file})`);
        } catch (e) {
          console.warn("[font] switch failed", e);
          showGameTip("字体切换失败");
        }
        showPhase("world");
      })();
      return;
    }
    // Equip-sell voucher: client-only batch sell of unenhanced bag equips.
    // Actions 293..295 — must not reuse dream-badge combine 290/291.
    if (action === EquipSellBatchAction.OPEN_CONFIRM) {
      dialog.show({
        title: "确认出售",
        body: "出售背包中全部未强化装备？售出后不可找回。",
        options: [
          { label: "确认出售", action: EquipSellBatchAction.CONFIRM, value: 0 },
          { label: "取消", action: EquipSellBatchAction.CANCEL, value: 0 },
        ],
      });
      showPhase("dialog");
      setChromeStatus("确认批量出售");
      return;
    }
    if (action === EquipSellBatchAction.CONFIRM) {
      dialog.hide();
      void (async () => {
        showGameTip("批量出售中…");
        const r = await session.sellAllUnenhancedBagEquips();
        showGameTip(r.message);
        hud.pushChat(`[系]${r.message}`);
        if (!dialog.visible) showPhase("world");
      })();
      return;
    }
    if (action === EquipSellBatchAction.CANCEL) {
      void session
        .pickNpcOption(1, EquipSellService.OPEN, "出售装备")
        .then(() => {
          if (!dialog.visible) showPhase("world");
        });
      return;
    }
    // Approach confirm: 是否与NPC交谈？（legacy action ids from full DialogPanel）
    if (action === 210) {
      dialog.hide();
      closeApproach(false);
      void session.openNpc(value);
      showPhase("world");
      return;
    }
    if (action === 211) {
      dialog.hide();
      closeApproach(true);
      // Stay near without re-prompt until player walks away (latch kept).
      showPhase("world");
      return;
    }
        if ((value >>> 0) === ARENA_NPC_WAGER) {
      dialog.hide();
      void openWagerPanel();
      return;
    }
    if ((value >>> 0) === NPC_FESTIVAL_EXCHANGE) {
      dialog.hide();
      void openFestivalPanel();
      return;
    }
    if ((value >>> 0) === NPC_YANMEN_EXCHANGE) {
      dialog.hide();
      void openYanmenPanel();
      return;
    }
    if ((value >>> 0) === NPC_RING_QUEST) {
      dialog.hide();
      void openRingQuestPanel();
      return;
    }
    if ((value >>> 0) === NPC_LADDER_CHALLENGE) {
      dialog.hide();
      void openLadderPanel();
      return;
    }
    if ((value >>> 0) === ARENA_NPC_RANK) {
      dialog.hide();
      void openArenaPanel();
      return;
    }
    if ((value >>> 0) === ARENA_NPC_ENTER || (value >>> 0) === ARENA_NPC_LEAVE) {
      dialog.hide();
      void session.pickNpcOption(action, value, label).then(async () => {
        if ((value >>> 0) === ARENA_NPC_LEAVE) arenaPanel.close();
        /*
         * Arena NPC enter returns 30/1 like instance enter, but used to skip
         * post-enter followup.  sceneVisible stayed pending → scene-sync never
         * pushed 2/10 peers until「周围玩家」2/7 promoted ready (map only self).
         */
        if ((value >>> 0) === ARENA_NPC_ENTER) {
          await session.settleArenaSceneEnter();
          if (session.isArenaScene()) showGameTip("已进入比武擂台");
        }
        if (!dialog.visible) showPhase("world");
      });
      return;
    }
dialog.hide();
    void session.pickNpcOption(action, value, label).then(() => {
      if (session.inBattle) {
        showPhase("battle");
        return;
      }
      // Nested service dialog may have re-opened the panel via onNpcDialog.
      // Leave (action=1 value=0) only closes locally — stay on world.
      if (!dialog.visible) showPhase("world");
    });
  };

  worldMap.onClose = () => {
    mapUiMode = "off";
    miniMap.hide();
    showPhase("world");
  };
  worldMap.onSoftkeys = (labels) => {
    if (phase === "map") shell.setSoftkeys(labels);
  };
  worldMap.onTeleport = (args) => {
    worldMap.showTip(`传送中…${args.alias}`);
    view.inputEnabled = false;
    void session
      .requestMapTeleport(
        args.curTeleportId,
        args.objTeleportId,
        args.exitId,
        args.scene,
      )
      .then((r) => {
        if (r.ok) {
          mapUiMode = "off";
          miniMap.hide();
          worldMap.hide();
          showPhase("world");
          hud.pushChat(`[系]消耗${r.cost}个传送石 → ${args.alias}`);
          showGameTip(`消耗${r.cost}个传送石 → ${args.alias}`);
        } else {
          hud.pushChat(`[系]传送失败: ${r.detail}`);
          // Longer toast so「传送石不够」is readable on the confirm panel.
          worldMap.showTip(r.detail, 1800);
          if (phase === "map") view.inputEnabled = false;
          else view.inputEnabled = true;
        }
      })
      .finally(() => {
        if (phase === "world") view.inputEnabled = true;
      });
  };

  battle.onAttack = () => {
    // Manual / timeout 普攻 — may intentionally replace skill memory.
    session.noteManualBattleOperate(0, "manual");
    const id = battle.targetIndex();
    if (battle.isAwaitingRound()) {
      shell.setSoftkeys({ left: "", right: "" });
    }
    void session.battleOperate(id, 0).catch(() => {
      if (phase === "battle") battle.clearAwaitingRound();
    });
  };
  battle.onSkill = (operate) => {
    // Hangup/[自动] share this path (incl. submitAutoOperate 普攻 → onSkill(0)).
    // "auto" only blocks Operate=0 from erasing a remembered skill.
    const fromAuto = session.shouldInBattleAuto();
    session.noteManualBattleOperate(operate, fromAuto ? "auto" : "manual");
    const id = battle.targetIndex();
    if (battle.isAwaitingRound()) {
      shell.setSoftkeys({ left: "", right: "" });
    }
    void session.battleOperate(id, operate).catch(() => {
      if (phase === "battle") battle.clearAwaitingRound();
    });
  };
  battle.onFlee = () => void session.battleEscape();
  battle.onItemUse = (seq) => {
    const id = battle.targetIndex();
    if (battle.isAwaitingRound()) {
      shell.setSoftkeys({ left: "", right: "" });
    }
    void session.battleItemUse(id, seq);
  };
  battle.onSettleConfirm = () => {
    endBattleToWorld("结算完成");
  };
  battle.onExitReady = () => {
    if (phase === "battle") endBattleToWorld("战斗结束");
  };
  battle.onDeathPromptShown = () => {
    // Death ends client hangup loop (same as former server「已停止挂机」).
    if (session.hangupDesired || session.hangupActive) {
      void session.stopHangup();
      syncWorldStatusBadges();
    }
    const refreshStoneUi = () => {
      battle.setDeathPromptBag(hasRevivalStone());
      setChromeStatus(
        hasRevivalStone()
          ? "死亡 · 确定=当前项 / 右键普通复活"
          : "死亡 · 确定=当前项（默认普通复活）/ 右键普通复活",
      );
      shell.setSoftkeys({
        left: battle.softkeyLeftLabel(),
        right: battle.softkeyRightLabel(),
      });
    };
    refreshStoneUi();
    // Bag may be stale mid-fight — refresh so 801 shows as usable.
    void session.openBag().then(refreshStoneUi);
  };
  battle.onDeathPromptChoice = (result) => {
    // CBE: no 801 → open mall (no 1/7/14). Buy then return → confirm stone.
    if (result === 1 && !hasRevivalStone()) {
      showGameTip("打开商城购买复活石");
      hud.pushChat("[系]购买复活石后返回，再选使用复活石");
      openSystem("mall");
      return;
    }
    showGameTip(result === 1 ? "使用复活石…" : "普通复活…");
    shell.setSoftkeys({ left: "", right: "" });
    void session.battleDeathPrompt(result).then(() => {
      // result=1 → 4/7+4/8…; result=2 → 20/1+30/1 scene enter.
      if (phase === "battle" && session.inBattle && session.hp <= 0) {
        battle.resumeDeathPrompt();
      }
    });
  };
  battle.onAutoToggle = () => {
    if (battle.isArenaDuel() || session.isArenaScene()) {
      showGameTip("比武中不可自动");
      return false;
    }
    const on = !session.autoBattleActive;
    void session.setBattleAutoPrefer(on);
    hud.pushChat(on ? "[战]自动开启" : "[战]自动关闭");
    showGameTip(on ? "自动战斗开启" : "自动战斗关闭");
    if (on) {
      // Gateway poll synthesizes operates; clear any hangup-alone client arm.
      if (autoArmTimer != null) {
        window.clearTimeout(autoArmTimer);
        autoArmTimer = null;
      }
    } else {
      if (autoArmTimer != null) {
        window.clearTimeout(autoArmTimer);
        autoArmTimer = null;
      }
      if (phase === "battle") {
        shell.setSoftkeys(
          battle.isAwaitingRound()
            ? { left: "", right: "" }
            : { left: "确定", right: battle.softkeyRightLabel() },
        );
      }
    }
    return on;
  };
  battle.onAutoCancel = () => {
    if (autoArmTimer != null) {
      window.clearTimeout(autoArmTimer);
      autoArmTimer = null;
    }
    if (!session.autoBattleActive) return;
    void session.setBattleAutoPrefer(false);
    hud.pushChat("[战]自动关闭");
    showGameTip("自动战斗关闭");
    if (phase === "battle") {
      shell.setSoftkeys(
        battle.isAwaitingRound()
          ? { left: "", right: "" }
          : { left: "确定", right: battle.softkeyRightLabel() },
      );
    }
  };
  /** Dedicated stopHangup — local clear; no server 2/10. */
  battle.onHangupCancel = () => {
    if (autoArmTimer != null) {
      window.clearTimeout(autoArmTimer);
      autoArmTimer = null;
    }
    void session.stopHangup();
    syncWorldStatusBadges();
  };
  battle.onHangupAddTime = async () => {
    const fb = await session.useBattleInsightFromBag();
    if (fb.ok) {
      try {
        await session.openBag();
      } catch {
        /* bag refresh optional after consume */
      }
      const cap =
        session.characterStats?.backpackCapacity ||
        Math.max(64, session.bagItems.length);
      battle.setHangupBagSlots(session.bagItems.length, cap);
      battle.setBattleInsightRemainSec(
        session.getBattleInsightRemainingSec(),
      );
    }
    /* Battle hides world HUD — show Pixi toast on the battle stage. */
    if (fb.message) battle.showTip(fb.message);
  };
  battle.onVitalsChanged = () => {
    const vitals = battle.allyVitals();
    if (!vitals) return;
    session.hp = vitals.hp;
    session.hpMax = Math.max(vitals.maxHp, vitals.hp, 1);
    session.mp = vitals.mp;
    session.mpMax = Math.max(vitals.maxMp, vitals.mp, 1);
    hud.setVitals(session.hp, session.hpMax, session.mp, session.mpMax);
  };
  battle.onAutoReady = () => {
    if (battle.isArenaDuel()) return;
    if (!session.shouldClientDriveInBattleAuto()) return;
    window.setTimeout(() => {
      armInBattleAuto("挂机出手…");
    }, 400);
  };

  hud.onMenu = () => showPhase("menu");
  hud.onHome = () => cycleMapUi();
  hud.onChat = () => {
    if (phase !== "world" || session.inBattle) return;
    if (auctionPanel.visible || arenaPanel.visible || wagerPanel.visible || ladderPanel.visible || guessPanel.visible || festivalPanel.visible || yanmenPanel.visible || ringQuestPanel.visible) return;
    showPhase("chat");
    chatPanel.open(hud.getChatEntries());
    setChromeStatus("聊天 · 点输入框打字 · ←→切换频道");
  };
  hud.onPortrait = () => {
    if (phase !== "world" || session.inBattle) return;
    openSystem("status");
  };
  hud.onPartyMate = (mateId) => {
    if (phase !== "world" || session.inBattle || !mateId) return;
    const mate = session.teamMembers.find((m) => m.id === mateId);
    openPlayerCard(mateId, mate?.name);
  };
  hud.onHangup = () => {
    // Active hangup, or armed resume intent: always stop locally.
    if (session.hangupActive || session.hangupDesired) {
      if (autoArmTimer != null) {
        window.clearTimeout(autoArmTimer);
        autoArmTimer = null;
      }
      void session.stopHangup();
      if (phase === "battle") battle.setHangupActive(false);
      syncWorldStatusBadges();
      return;
    }
    // Idle: pick skill first, then startHangup on confirm (action 260).
    openHangupSkillDialog(true);
  };
  hud.onIcon = (action) => {
    if (action === "status") {
      openSystem("equip");
      return;
    }
    if (action === "bag") {
      openSystem("bag");
      return;
    }
    if (action === "quest") {
      openSystem("quest");
      return;
    }
    if (action === "social") {
      showPhase("chat");
      chatPanel.open(hud.getChatEntries());
      return;
    }
    if (action === "mall") {
      openSystem("mall");
    }
  };

  const handleSoftkey = (which: "left" | "center" | "right") => {
    if (phase === "splash") {
      if (which === "left") title.confirm();
      return;
    }
    if (phase === "login" || phase === "register") {
      if (which === "left") title.confirm();
      if (which === "right") title.cancelLogin();
      return;
    }
    if (phase === "servers") {
      if (which === "left") title.confirm();
      if (which === "right") showPhase("splash");
      return;
    }
    if (phase === "role") {
      if (roles.isDeleteConfirming()) {
        if (which === "left") roles.confirm();
        if (which === "right") roles.cancelDeleteConfirm();
        return;
      }
      if (which === "left") roles.confirm();
      if (which === "right") showPhase("servers");
      return;
    }
    if (phase === "create-role") {
      if (which === "left") createRole.confirm();
      if (which === "right") createRole.cancel();
      return;
    }
    // Entertainment HTML player — topmost across world + hangup/auto battle.
    if (entertainWeb.isExpanded()) {
      if (which === "left" || which === "center") {
        entertainWeb.minimize();
        return;
      }
      if (which === "right") {
        entertainWeb.close();
        return;
      }
    }
    if (phase === "world") {
      // Overlays on world (auction/arena/wager/entertain/wing) must win over 菜单 softkeys.
      if (mountPanel.visible) {
        if (which === "left" || which === "center") {
          mountPanel.confirm();
          return;
        }
        if (which === "right") {
          mountPanel.cancel();
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
        }
        return;
      }
      if (wingPanel.visible) {
        if (which === "left" || which === "center") {
          wingPanel.confirm();
          return;
        }
        if (which === "right") {
          wingPanel.cancel();
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
        }
        return;
      }
      if (entertainPanel.visible) {
        if (which === "left" || which === "center") {
          entertainPanel.confirm();
          return;
        }
        if (which === "right") {
          entertainPanel.cancel();
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
        }
        return;
      }
      if (auctionPanel.visible) {
        if (which === "left" || which === "center") {
          auctionPanel.confirm();
          shell.setSoftkeys(auctionPanel.softkeys());
        } else if (which === "right") {
          if (auctionPanel.cancelOrBack()) {
            shell.setSoftkeys(auctionPanel.softkeys());
            return;
          }
          auctionPanel.close();
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
        }
        return;
      }
      if (arenaPanel.visible) {
        if (which === "left") {
          arenaPanel.toggleTabFocus();
          shell.setSoftkeys(arenaPanel.softkeys());
        }
        if (which === "center") arenaPanel.confirm();
        if (which === "right") {
          if (arenaPanel.dismissTipOnSoftRight()) return;
          arenaPanel.close();
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
        }
        return;
      }
      if (wagerPanel.visible) {
        if (which === "left") {
          wagerPanel.onAction?.("refresh");
          return;
        }
        if (which === "center") {
          wagerPanel.confirm();
          shell.setSoftkeys(wagerPanel.softkeys());
          return;
        }
        if (which === "right") {
          if (wagerPanel.dismissTipOnSoftRight()) return;
          wagerPanel.close();
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
        }
        return;
      }
      if (ladderPanel.visible) {
        if (which === "left") ladderPanel.confirm();
        if (which === "right") {
          if (ladderPanel.dismissTipOnSoftRight()) return;
          ladderPanel.close();
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
        }
        return;
      }
      if (guessPanel.visible) {
        if (which === "left") {
          const sk = guessPanel.softkeys();
          // 「押注」/「领奖」走 confirm；「刷新」才拉状态。
          if (sk.left === "押注" || sk.left === "领奖") guessPanel.confirm();
          else void refreshGuessPanel(guessHistoryPage);
        }
        if (which === "center") {
          guessPanel.confirm();
          shell.setSoftkeys(guessPanel.softkeys());
        }
        if (which === "right") {
          if (guessPanel.dismissTipOnSoftRight()) return;
          guessPanel.close();
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
          setChromeStatus("");
        }
        return;
      }
      if (festivalPanel.visible || yanmenPanel.visible) {
        if (which === "left") {
          if (festivalPanel.visible) festivalPanel.confirm();
          else yanmenPanel.confirm();
        }
        if (which === "right") {
          if (festivalPanel.visible) {
            if (festivalPanel.dismissTipOnSoftRight()) return;
            festivalPanel.onAction?.("close");
          } else {
            if (yanmenPanel.dismissTipOnSoftRight()) return;
            yanmenPanel.onAction?.("close");
          }
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
          setChromeStatus("");
        }
        return;
      }
      if (ringQuestPanel.visible) {
        if (which === "left") ringQuestPanel.confirm();
        if (which === "right") {
          if (ringQuestPanel.dismissTipOnSoftRight()) return;
          ringQuestPanel.onAction?.("close");
          syncWorldWalkInput();
          shell.setSoftkeys({ left: "菜单", right: "菜单" });
          setChromeStatus("");
        }
        return;
      }
      if (approach.open) {
        if (which === "left") approach.confirm();
        if (which === "right") approach.cancel();
        return;
      }
      if (which === "right" && hud.dismissTipOnSoftRight()) return;
      if (which === "left" || which === "right") showPhase("menu");
      return;
    }
    if (phase === "map") {
      if (which === "left") worldMap.confirm();
      if (which === "right") worldMap.cancel();
      return;
    }
    if (phase === "menu") {
      if (which === "left") menu.confirm();
      if (which === "right") menu.cancel();
      return;
    }
    if (phase === "battle") {
      if (battle.isAwaitingRound()) return;
      if (which === "left") battle.confirm();
      if (which === "right") battle.flee();
      shell.setSoftkeys(
        battle.isAwaitingRound()
          ? { left: "", right: "" }
          : {
              left: battle.softkeyLeftLabel(),
              right: battle.softkeyRightLabel(),
            },
      );
      return;
    }
    if (phase === "dialog") {
      if (which === "left") dialog.confirm();
      if (which === "right") dialog.cancel();
      return;
    }
    if (phase === "system") {
      if (which === "left") system.confirm();
      if (which === "right") system.cancel();
      return;
    }
    if (phase === "enhance") {
      if (which === "left") enhanceUi.confirm();
      if (which === "right") enhanceUi.cancel();
      return;
    }
    if (phase === "wash") {
      if (which === "left") washUi.softLeft();
      if (which === "center") washUi.softMid();
      if (which === "right") washUi.softRight();
      shell.setSoftkeys(washUi.softkeys());
      return;
    }
    if (phase === "social") {
      if (which === "left") social.confirm();
      if (which === "right") social.cancel();
      return;
    }
    if (phase === "quest") {
      if (which === "left") taskUi.confirm();
      if (which === "right") taskUi.cancel();
      return;
    }
    if (phase === "chat") {
      if (which === "left") chatPanel.confirm();
      if (which === "right") chatPanel.cancel();
      return;
    }
    if (phase === "trade") {
      if (which === "left") tradeUi.softLeft();
      if (which === "right") tradeUi.cancel();
      return;
    }
    if (phase === "guild") {
      if (which === "left") guildUi.confirm();
      if (which === "right") guildUi.cancel();
    }
  };
  shell.onSoftkey(handleSoftkey);
  touch.onSoftkey = handleSoftkey;

  /** Keyboard F / D-pad OK — confirm action, separate from left softkey (Q). */
  const handleOk = () => {
    if (phase === "map") {
      worldMap.confirm();
      return;
    }
    if (phase === "world" && mountPanel.visible) {
      mountPanel.confirm();
      return;
    }
    if (phase === "world" && wingPanel.visible) {
      wingPanel.confirm();
      return;
    }
    if (phase === "world" && entertainPanel.visible) {
      entertainPanel.confirm();
      return;
    }
    if (phase === "world" && auctionPanel.visible) {
      auctionPanel.confirm();
      shell.setSoftkeys(auctionPanel.softkeys());
      return;
    }
    if (phase === "world" && ladderPanel.visible) {
      ladderPanel.confirm();
      return;
    }
    if (phase === "world" && guessPanel.visible) {
      guessPanel.confirm();
      shell.setSoftkeys(guessPanel.softkeys());
      return;
    }
    if (phase === "world" && (festivalPanel.visible || yanmenPanel.visible)) {
      if (festivalPanel.visible) festivalPanel.confirm();
      else yanmenPanel.confirm();
      return;
    }
    if (phase === "world" && ringQuestPanel.visible) {
      ringQuestPanel.confirm();
      shell.setSoftkeys(ringQuestPanel.softkeys());
      return;
    }
    if (phase === "world" && arenaPanel.visible) {
      arenaPanel.confirm();
      return;
    }
    if (phase === "world" && wagerPanel.visible) {
      wagerPanel.confirm();
      return;
    }
    if (phase === "world" && approach.open) {
      approach.confirm();
      return;
    }
    if (phase === "world") {
      // OK = talk / confirm approach only.  Do NOT force a wild 4/1 here —
      // that made every OK press auto-encounter (nearest SCE / fallback 9001).
      // Encounters stay on collide / monster tap / hangup.
      const npc = view.nearestInteractNpc();
      if (npc) {
        openApproach(npc.actorId, npc.name);
      } else {
        showGameTip("附近没有可对话对象");
      }
      return;
    }
    if (phase === "splash" || phase === "login" || phase === "register") {
      title.confirm();
      return;
    }
    if (phase === "role") {
      // OK = 打开删除确认（需输入角色名）；确认中再按 OK/确定提交。
      roles.deleteSelected();
      return;
    }
    if (phase === "create-role") {
      createRole.confirm();
      return;
    }
    if (phase === "battle") {
      if (battle.isAwaitingRound()) return;
      battle.confirm();
      shell.setSoftkeys(
        battle.isAwaitingRound()
          ? { left: "", right: "" }
          : {
              left: battle.softkeyLeftLabel(),
              right: battle.softkeyRightLabel(),
            },
      );
      return;
    }
    if (phase === "menu") {
      menu.confirm();
      return;
    }
    if (phase === "dialog") {
      dialog.confirm();
      return;
    }
    if (phase === "system") {
      system.confirm();
      return;
    }
    if (phase === "enhance") {
      enhanceUi.confirm();
      return;
    }
    if (phase === "wash") {
      // D-pad 中键/OK：主界面选玄晶；选晶界面确定（不用方向键上方中软按钮）
      if (washUi.softkeys().left === "确定") washUi.softLeft();
      else washUi.softMid();
      shell.setSoftkeys(washUi.softkeys());
      return;
    }
    if (phase === "social") {
      social.confirm();
      return;
    }
    if (phase === "quest") {
      taskUi.confirm();
      return;
    }
    if (phase === "chat") {
      chatPanel.confirm();
      return;
    }
    if (phase === "trade") {
      tradeUi.confirm();
      return;
    }
    if (phase === "guild") {
      guildUi.confirm();
    }
  };
  touch.onOk = handleOk;

  const dirKey: Record<TouchDir, string> = {
    up: "arrowup",
    down: "arrowdown",
    left: "arrowleft",
    right: "arrowright",
  };
  const dirWasd: Record<TouchDir, string> = {
    up: "w",
    down: "s",
    left: "a",
    right: "d",
  };
  touch.onDir = (dir, down) => {
    // Always clear SceneView holds on release — otherwise a D-pad up during
    // battle is dropped and the key stays stuck after returning to world.
    if (!down) {
      view.setHeldKey(dirKey[dir], false);
      view.setHeldKey(dirWasd[dir], false);
    }
    if (phase === "world" && auctionPanel.visible) {
      if (!down) return;
      if (dir === "left" || dir === "right") {
        auctionPanel.horizontal(dir === "left" ? -1 : 1);
        shell.setSoftkeys(auctionPanel.softkeys());
      } else {
        auctionPanel.move(dir === "up" ? -1 : 1);
      }
      return;
    }
    if (phase === "world" && mountPanel.visible) {
      if (!down) return;
      if (dir === "up" || dir === "down") mountPanel.move(dir === "up" ? -1 : 1);
      return;
    }
    if (phase === "world" && wingPanel.visible) {
      if (!down) return;
      if (dir === "up" || dir === "down") wingPanel.move(dir === "up" ? -1 : 1);
      return;
    }
    if (phase === "world" && entertainPanel.visible) {
      if (!down) return;
      if (dir === "up" || dir === "down") entertainPanel.move(dir === "up" ? -1 : 1);
      return;
    }
    if (phase === "world" && guessPanel.visible) {
      if (!down) return;
      // 方向键只切选项/记录；押金走 LCD 数字键盘（含删除）。
      if (dir === "left" || dir === "right") {
        guessPanel.horizontal(dir === "left" ? -1 : 1);
        shell.setSoftkeys(guessPanel.softkeys());
      } else {
        guessPanel.move(dir === "up" ? -1 : 1);
      }
      return;
    }
    if (phase === "world" && (festivalPanel.visible || yanmenPanel.visible)) {
      if (!down) return;
      if (dir === "up" || dir === "down") {
        if (festivalPanel.visible) festivalPanel.move(dir === "up" ? -1 : 1);
        else yanmenPanel.move(dir === "up" ? -1 : 1);
      }
      return;
    }
    if (phase === "world" && (arenaPanel.visible || wagerPanel.visible)) {
      if (!down) return;
      if (arenaPanel.visible) {
        if (dir === "left" || dir === "right") {
          arenaPanel.horizontal(dir === "left" ? -1 : 1);
          shell.setSoftkeys(arenaPanel.softkeys());
        } else arenaPanel.move(dir === "up" ? -1 : 1);
      } else if (wagerPanel.visible) {
        if (dir === "left" || dir === "right") {
          const d = dir === "left" ? -1 : 1;
          // ←→切栏；摆擂未开擂时调押金（步进 100金=1000000铜）
          if (!wagerPanel.horizontal(d)) {
            wagerPanel.adjustStake(d * 100 * 10000);
          }
        } else {
          wagerPanel.move(dir === "up" ? -1 : 1);
        }
        shell.setSoftkeys(wagerPanel.softkeys());
      }
      return;
    }
    if (phase === "world") {
      if (down) {
        view.setHeldKey(dirKey[dir], true);
        view.setHeldKey(dirWasd[dir], true);
      }
      return;
    }
    if (!down) return;
    if (phase === "map") {
      const cbeDir =
        dir === "up" ? 1 : dir === "right" ? 2 : dir === "down" ? 3 : 4;
      worldMap.move(cbeDir);
      return;
    }
    const delta = dir === "up" || dir === "left" ? -1 : 1;
    if (dir === "left" || dir === "right") {
      if (phase === "dialog" && dialog.isMedicineBuyQty()) {
        dialog.adjustMedicineBuyQty(delta);
        return;
      }
      if (phase === "system") {
        system.moveCategory(delta);
        return;
      }
      if (phase === "social") {
        social.moveCategory(delta);
        return;
      }
      if (phase === "quest") {
        taskUi.moveTab(delta);
        return;
      }
      if (phase === "enhance") {
        enhanceUi.move(delta);
        return;
      }
      if (phase === "wash") {
        handleWashHorizontal(delta);
        return;
      }
      if (phase === "chat") {
        chatPanel.cycleChannel(delta);
        return;
      }
      if (phase === "trade") {
        // ←→: stack qty on bag row, or refocus money IME.
        tradeUi.moveHorizontal(delta);
        return;
      }
      if (phase === "create-role") {
        createRole.cycle(delta);
        return;
      }
      // horizontal only used in world; lists use vertical
      if (phase === "splash" || phase === "servers" || phase === "login" || phase === "register") {
        title.move(delta);
      }
      return;
    }
    if (phase === "splash" || phase === "servers" || phase === "login" || phase === "register") title.move(delta);
    else if (phase === "role") roles.move(delta);
    else if (phase === "create-role") createRole.move(delta);
    else if (phase === "menu") menu.move(delta);
    else if (phase === "dialog") dialog.move(delta);
    else if (phase === "system") system.move(delta);
    else if (phase === "enhance") enhanceUi.move(delta);
    else if (phase === "wash") washUi.move(delta);
    else if (phase === "social") social.move(delta);
    else if (phase === "quest") taskUi.move(delta);
    else if (phase === "chat") chatPanel.move(delta);
    else if (phase === "trade") tradeUi.move(delta);
    else if (phase === "guild") guildUi.move(delta);
    else if (phase === "battle") battle.cycleSkill(delta);
  };

  window.addEventListener("keydown", (e) => {
    const k = e.key.toLowerCase();
    if (phase === "login" || phase === "register") {
      if (k === "arrowup" || k === "arrowdown") {
        title.move(k === "arrowup" ? -1 : 1);
        e.preventDefault();
        return;
      }
      if (e.ctrlKey && k === "v") {
        void title.pasteClipboard();
        e.preventDefault();
        return;
      }
      if (
        title.typeKey(
          k === "backspace" || k === "tab" ? k : e.key,
          e,
        )
      ) {
        e.preventDefault();
        return;
      }
    }
    if (phase === "splash" || phase === "servers" || phase === "login" || phase === "register") {
      if (phase !== "login" && phase !== "register" && (k === "arrowup" || k === "w")) title.move(-1);
      if (phase !== "login" && phase !== "register" && (k === "arrowdown" || k === "s")) title.move(1);
    }
    if (phase === "role") {
      if (k === "arrowup" || k === "w") roles.move(-1);
      if (k === "arrowdown" || k === "s") roles.move(1);
    }
    if (phase === "create-role") {
      if (k === "arrowup" || k === "w") {
        createRole.move(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        createRole.move(1);
        e.preventDefault();
      }
      if (k === "arrowleft" || k === "a") {
        createRole.cycle(-1);
        e.preventDefault();
      }
      if (k === "arrowright" || k === "d") {
        createRole.cycle(1);
        e.preventDefault();
      }
    }
    if (phase === "menu") {
      if (k === "arrowup" || k === "w") menu.move(-1);
      if (k === "arrowdown" || k === "s") menu.move(1);
    }
    if (phase === "dialog") {
      if (dialog.isMedicineBuyQty()) {
        if (k === "arrowleft" || k === "a") {
          dialog.adjustMedicineBuyQty(-1);
          e.preventDefault();
        }
        if (k === "arrowright" || k === "d") {
          dialog.adjustMedicineBuyQty(1);
          e.preventDefault();
        }
      } else {
        if (k === "arrowup" || k === "w") dialog.move(-1);
        if (k === "arrowdown" || k === "s") dialog.move(1);
      }
    }
    if (mountPanel.visible) {
      if (k === "arrowup" || k === "w") {
        mountPanel.move(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        mountPanel.move(1);
        e.preventDefault();
      }
      return;
    }
    if (wingPanel.visible) {
      if (k === "arrowup" || k === "w") {
        wingPanel.move(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        wingPanel.move(1);
        e.preventDefault();
      }
      return;
    }
    if (guessPanel.visible) {
      if (guessPanel.currentTab === "bet" && guessPanel.handleStakeKey(e.key)) {
        e.preventDefault();
        return;
      }
      if (k === "arrowup" || k === "w") {
        guessPanel.move(-1);
        e.preventDefault();
        return;
      }
      if (k === "arrowdown" || k === "s") {
        guessPanel.move(1);
        e.preventDefault();
        return;
      }
      if (k === "arrowleft" || k === "a") {
        guessPanel.horizontal(-1);
        shell.setSoftkeys(guessPanel.softkeys());
        e.preventDefault();
        return;
      }
      if (k === "arrowright" || k === "d") {
        guessPanel.horizontal(1);
        shell.setSoftkeys(guessPanel.softkeys());
        e.preventDefault();
        return;
      }
    }
    if (entertainPanel.visible) {
      if (k === "arrowup" || k === "w") {
        entertainPanel.move(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        entertainPanel.move(1);
        e.preventDefault();
      }
      return;
    }
    if (entertainWeb.isExpanded()) {
      if (k === "escape") {
        entertainWeb.close();
        e.preventDefault();
      }
      return;
    }
    if (auctionPanel.visible) {
      if (auctionPanel.isPriceMode()) {
        if (auctionPanel.typePriceKey(k === "backspace" ? "backspace" : e.key)) {
          e.preventDefault();
          return;
        }
      }
      if (k === "arrowup" || k === "w") {
        auctionPanel.move(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        auctionPanel.move(1);
        e.preventDefault();
      }
      if (k === "arrowleft" || k === "a") {
        auctionPanel.horizontal(-1);
        shell.setSoftkeys(auctionPanel.softkeys());
        e.preventDefault();
      }
      if (k === "arrowright" || k === "d") {
        auctionPanel.horizontal(1);
        shell.setSoftkeys(auctionPanel.softkeys());
        e.preventDefault();
      }
      return;
    }
    if (phase === "system") {
      if (k === "arrowup" || k === "w") system.move(-1);
      if (k === "arrowdown" || k === "s") system.move(1);
      if (k === "arrowleft" || k === "a") system.moveCategory(-1);
      if (k === "arrowright" || k === "d") system.moveCategory(1);
    }
    if (phase === "enhance") {
      if (k === "arrowup" || k === "w") enhanceUi.move(-1);
      if (k === "arrowdown" || k === "s") enhanceUi.move(1);
    }
    if (phase === "wash") {
      if (k === "arrowup" || k === "w") {
        washUi.move(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        washUi.move(1);
        e.preventDefault();
      }
      if (k === "arrowleft" || k === "a") {
        handleWashHorizontal(-1);
        e.preventDefault();
      }
      if (k === "arrowright" || k === "d") {
        handleWashHorizontal(1);
        e.preventDefault();
      }
    }
    if (phase === "social") {
      if (k === "arrowup" || k === "w") social.move(-1);
      if (k === "arrowdown" || k === "s") social.move(1);
      if (k === "arrowleft" || k === "a") {
        social.moveCategory(-1);
        e.preventDefault();
      }
      if (k === "arrowright" || k === "d") {
        social.moveCategory(1);
        e.preventDefault();
      }
    }
    if (phase === "quest") {
      if (k === "arrowup" || k === "w") {
        taskUi.move(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        taskUi.move(1);
        e.preventDefault();
      }
      if (k === "arrowleft" || k === "a") {
        taskUi.moveTab(-1);
        e.preventDefault();
      }
      if (k === "arrowright" || k === "d") {
        taskUi.moveTab(1);
        e.preventDefault();
      }
    }
    if (phase === "chat") {
      if (k === "arrowleft" || k === "a") {
        chatPanel.cycleChannel(-1);
        e.preventDefault();
      }
      if (k === "arrowright" || k === "d") {
        chatPanel.cycleChannel(1);
        e.preventDefault();
      }
      if (k === "arrowup" || k === "w") {
        chatPanel.move(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        chatPanel.move(1);
        e.preventDefault();
      }
    }
    if (phase === "trade") {
      if (k === "arrowleft" || k === "a") {
        tradeUi.moveHorizontal(-1);
        e.preventDefault();
        return;
      }
      if (k === "arrowright" || k === "d") {
        tradeUi.moveHorizontal(1);
        e.preventDefault();
        return;
      }
      if (tradeUi.isMoneyMode()) {
        if (tradeUi.typeMoneyKey(k === "backspace" ? "backspace" : e.key)) {
          e.preventDefault();
        }
        return;
      }
      if (k === "arrowup" || k === "w") tradeUi.move(-1);
      if (k === "arrowdown" || k === "s") tradeUi.move(1);
    }
    if (phase === "guild") {
      if (k === "arrowup" || k === "w") guildUi.move(-1);
      if (k === "arrowdown" || k === "s") guildUi.move(1);
    }
    if (phase === "battle") {
      if (k === "arrowup" || k === "w") {
        battle.cycleSkill(-1);
        e.preventDefault();
      }
      if (k === "arrowdown" || k === "s") {
        battle.cycleSkill(1);
        e.preventDefault();
      }
    }
    if (phase === "map") {
      if (k === "arrowup" || k === "w") worldMap.move(1);
      if (k === "arrowright" || k === "d") worldMap.move(2);
      if (k === "arrowdown" || k === "s") worldMap.move(3);
      if (k === "arrowleft" || k === "a") worldMap.move(4);
      e.preventDefault();
    }
    if (k === "m" && (phase === "world" || phase === "map")) {
      cycleMapUi();
      e.preventDefault();
      return;
    }
    if (k === "q") {
      // Q = left softkey everywhere (确定/瞬移/继续); map used to call cancel and quit early.
      if (entertainWeb.isExpanded()) {
        handleSoftkey("left");
      } else if (
        phase === "world" &&
        (auctionPanel.visible ||
          arenaPanel.visible ||
          wagerPanel.visible ||
          ladderPanel.visible ||
          guessPanel.visible ||
          festivalPanel.visible ||
          yanmenPanel.visible ||
          ringQuestPanel.visible ||
          entertainPanel.visible ||
          wingPanel.visible ||
          mountPanel.visible)
      ) {
        handleSoftkey("left");
      } else if (phase === "world" && approach.open) approach.confirm();
      else if (phase === "world") showPhase("menu");
      else if (phase === "splash" || phase === "login" || phase === "register") title.confirm();
      else shell.softkeys.onSoftkey?.("left");
    } else if (k === "f" && phase !== "login" && phase !== "register") {
      // F = D-pad OK（与左软 Q 分开）
      handleOk();
      e.preventDefault();
    } else if (k === "enter") {
      handleOk();
      e.preventDefault();
    } else if (k === "e" || k === "escape") {
      if (entertainWeb.isExpanded()) {
        handleSoftkey("right");
      } else if (
        phase === "world" &&
        (auctionPanel.visible ||
          arenaPanel.visible ||
          wagerPanel.visible ||
          ladderPanel.visible ||
          guessPanel.visible ||
          festivalPanel.visible ||
          yanmenPanel.visible ||
          ringQuestPanel.visible ||
          entertainPanel.visible ||
          wingPanel.visible ||
          mountPanel.visible)
      ) {
        handleSoftkey("right");
      } else if (phase === "world" && approach.open) {
        approach.cancel();
      } else if (phase === "world") {
        showPhase("menu");
      } else if (phase === "map") {
        worldMap.cancel();
      } else if (phase === "login" || phase === "register") {
        title.cancelLogin();
      } else if (phase === "battle") {
        if (battle.isAwaitingRound()) return;
        battle.flee();
        shell.setSoftkeys(
          battle.isAwaitingRound()
            ? { left: "", right: "" }
            : {
                left: battle.softkeyLeftLabel(),
                right: battle.softkeyRightLabel(),
              },
        );
      } else {
        shell.softkeys.onSoftkey?.("right");
      }
    } else if (k === "h" && phase === "world") {
      hud.onHangup?.();
    } else if (k === "u" && phase === "world") {
      hud.pushChat("[系]脱离卡死…");
      view.inputEnabled = false;
      void session.requestUnstuck().finally(() => {
        if (phase === "world") view.inputEnabled = true;
        hud.pushChat(`[系]落点 @${session.x},${session.y}`);
      });
    } else if (k === "c" && phase === "world") {
      showPhase("chat");
      chatPanel.open(hud.getChatEntries());
    }
  });

  showPhase("splash");
  title.setStatus("开始游戏/登陆游戏=创建或登录");
  setChromeStatus("就绪");

  // Screen unlock / return to app: heal Live WS if OEM suspended the WebView.
  // Native FGS also injects window.__pixiNativeKeepAlive every ~2s while in-world.
  // That tick must also drive hangup / [自动] — Chromium starves setTimeout while hidden.
  session.onBackgroundGameplayTick = () => {
    if (
      typeof document !== "undefined" &&
      document.visibilityState === "visible"
    ) {
      return;
    }
    flushBattleSleepsForBackground();
    if (phase !== "battle" || !battle.visible) return;
    if (
      battle.isAwaitingExit() &&
      (session.hangupDesired ||
        session.hangupActive ||
        session.autoBattleActive)
    ) {
      battle.confirmSettlementIfAwaiting();
      return;
    }
    if (
      session.shouldClientDriveInBattleAuto() &&
      !battle.isAwaitingExit() &&
      !battle.isAwaitingDeathPrompt()
    ) {
      fireInBattleAuto("挂机出手…");
    }
  };
  (window as Window & { __pixiNativeKeepAlive?: () => void }).__pixiNativeKeepAlive =
    () => {
      void session.backgroundKeepAliveTick();
    };

  const isGlContextLost = (): boolean => {
    try {
      const ctx = (
        app.renderer as unknown as {
          context?: { isLost?: boolean; gl?: WebGLRenderingContext };
        }
      ).context;
      if (ctx?.isLost) return true;
      const gl =
        ctx?.gl ??
        (app.renderer as unknown as { gl?: WebGLRenderingContext }).gl;
      return !!(
        gl &&
        typeof gl.isContextLost === "function" &&
        gl.isContextLost()
      );
    } catch {
      return false;
    }
  };

  const tryRestoreGlContext = (): void => {
    if (!isGlContextLost()) return;
    try {
      const ctx = (
        app.renderer as unknown as {
          context?: {
            isLost?: boolean;
            gl?: WebGLRenderingContext;
            extensions?: { loseContext?: { restoreContext: () => void } };
          };
        }
      ).context;
      const ext =
        ctx?.extensions?.loseContext ??
        ctx?.gl?.getExtension?.("WEBGL_lose_context") ??
        (
          app.canvas.getContext("webgl2") ||
          app.canvas.getContext("webgl")
        )?.getExtension("WEBGL_lose_context");
      ext?.restoreContext();
    } catch (e) {
      console.warn("[gl] restoreContext", e);
    }
  };

  const setTextureGcEnabled = (on: boolean): void => {
    try {
      const gc = (app.renderer as unknown as { gc?: { enabled: boolean } }).gc;
      if (gc && gc.enabled !== on) gc.enabled = on;
    } catch {
      /* ignore */
    }
  };

  /**
   * Long-running APK (screen on or off): WebGL / layout can die while gateway
   * keepalive keeps the session online → blank LCD. Re-fit, restore GL,
   * rebake labels/sprites.
   *
   * Field issues after prior recover:
   * - Probe only sampled HUD chrome (bgDeep) → false "OK", no reload, stuck black.
   * - recoverInFlight / refreshAllUc3Labels hang → later recovers skipped; input dead.
   * - Interval full-recover every 2s thrashed main thread (buttons unresponsive).
   */
  let recoverInFlight = false;
  let lastRecoverAt = 0;
  /** Consecutive recovers that left GL still lost / visually blank. */
  let consecutiveLostRecovers = 0;
  let glLostAt = 0;
  let glWatchdogTimer = 0;
  let glSoftReloadCount = 0;
  let recoverHangTimer = 0;
  /** Need consecutive failed blank recovers before full reload (→ splash). */
  const GL_SOFT_RELOAD_AFTER = 2;
  const GL_RESTORE_WATCHDOG_MS = 5000;
  const GL_RECOVER_HANG_MS = 8000;
  const GL_RELOAD_CAP = 6;

  const cancelGlRestoreWatchdog = (): void => {
    if (glWatchdogTimer) {
      window.clearTimeout(glWatchdogTimer);
      glWatchdogTimer = 0;
    }
  };

  const softReloadGl = (why: string): void => {
    if (glSoftReloadCount >= GL_RELOAD_CAP) {
      console.error("[gl] reload cap reached", why, glPressureSnapshot());
      void recordForensic(`reload-cap:${why}`, { forceBlankProbe: true });
      return;
    }
    glSoftReloadCount += 1;
    console.error("[gl] soft reload", why, glSoftReloadCount, "/", GL_RELOAD_CAP);
    void recordForensic(`soft-reload:${why}`, { forceBlankProbe: true });
    window.location.reload();
  };

  const armGlRestoreWatchdog = (): void => {
    cancelGlRestoreWatchdog();
    glWatchdogTimer = window.setTimeout(() => {
      glWatchdogTimer = 0;
      if (!glLostAt) return;
      console.error("[gl] restore watchdog", glPressureSnapshot());
      void recordForensic("restore-watchdog", { forceBlankProbe: true });
      softReloadGl("watchdog");
    }, GL_RESTORE_WATCHDOG_MS);
  };

  /** Forensic: black screen happens with or without self wing/mount. */
  const glPressureSnapshot = (): Record<string, unknown> => {
    try {
      const cos = session.cosmetics;
      const probe = view.actors.debugPeerCosmetics();
      const peersWithCos = probe.peers.filter(
        (p) => p.eqWing || p.eqMount || p.eqChenghao,
      ).length;
      const map = view.forensicMapState();
      return {
        phase,
        self: {
          ch: cos.eqChenghao,
          wing: cos.eqWing,
          mount: cos.eqMount,
        },
        peers: probe.peers.length,
        peersWithCos,
        pendingCos: probe.pending.length,
        pose: poseTextureCacheStats(),
        plist: plistAtlasGpuStats(),
        viewRootVisible: view.root.visible,
        battleVisible: battle.visible,
        mapIdentity: map.mapIdentity,
        mapW: map.mapW,
        mapH: map.mapH,
        hasMapSprite: map.hasMapSprite,
        mapSourceValid: map.mapSourceValid,
        worldVisible: map.worldVisible,
        hidden: document.visibilityState === "hidden",
        glLostAt,
        recoverInFlight,
      };
    } catch (e) {
      return { err: e instanceof Error ? e.message : String(e) };
    }
  };

  const forensicRing = new GlForensicRing();
  let forensicHeartbeatN = 0;
  let lastOkRenderAt = performance.now();
  let lastRenderCrashForensicAt = 0;
  let lastRenderStallForensicAt = 0;
  /** Consecutive alphaMode swallows — dead cosmetics tex without contextlost. */
  let alphaModeStormCount = 0;
  let lastAlphaModeStripAt = 0;
  let alphaModeProbeBlockedUntil = 0;
  const RENDER_STALL_MS = 2500;
  /** After this many swallowed frames, strip dead sprites once. */
  const ALPHA_STRIP_AFTER = 24;
  /** Still no ok render this long after strip → soft reload. */
  const ALPHA_RELOAD_AFTER_MS = 4500;

  const recordForensic = async (
    why: string,
    opts?: {
      blank?: boolean | null;
      forceBlankProbe?: boolean;
      classOverride?: ForensicClass;
      extra?: Record<string, unknown>;
      scanBroken?: boolean;
    },
  ): Promise<ForensicClass> => {
    const glLost = isGlContextLost();
    let blank: boolean | null =
      opts?.blank === undefined ? null : opts.blank;
    if (opts?.forceBlankProbe || blank === null) {
      if (
        opts?.forceBlankProbe ||
        glLost ||
        why.includes("blank") ||
        why.includes("context") ||
        why.startsWith("recover") ||
        why.includes("stall") ||
        why.includes("crash")
      ) {
        blank = canvasLooksBlank();
      }
    }
    const map = view.forensicMapState();
    const cls = classifyForensic({
      why,
      phase,
      glLost,
      blank,
      viewRootVisible: view.root.visible,
      battleVisible: battle.visible,
      mapW: map.mapW,
      mapH: map.mapH,
      hasMapSprite: map.hasMapSprite,
      classOverride: opts?.classOverride,
    });
    const payload: Record<string, unknown> = {
      ...glPressureSnapshot(),
      msSinceOkRender: Math.round(performance.now() - lastOkRenderAt),
      ...(opts?.extra ?? {}),
    };
    if (opts?.scanBroken) {
      try {
        payload.brokenTex = scanBrokenTextures(
          app.stage as unknown as Parameters<typeof scanBrokenTextures>[0],
        );
      } catch (e) {
        payload.brokenTexErr = e instanceof Error ? e.message : String(e);
      }
    }
    const ev = {
      t: performance.now(),
      iso: new Date().toISOString(),
      why,
      class: cls,
      blank,
      glLost,
      payload,
    };
    forensicRing.push(ev);
    const verboseForensic =
      typeof localStorage !== "undefined" &&
      localStorage.getItem("pix_gl_forensic") === "1";
    const line = `[gl] forensic ${cls} why=${why} blank=${blank} lost=${glLost}`;
    if (isAlertForensicClass(cls)) console.warn(line, payload);
    else if (verboseForensic) console.info(line);
    // No auto clipboard — alert storms overwrote the user's clipboard.
    // Manual dump: long-press status or window.__pixiDumpForensic().
    return cls;
  };

  const noteRenderCrash = (err: unknown, why: string): void => {
    const now = performance.now();
    if (now - lastRenderCrashForensicAt < 2000) return;
    lastRenderCrashForensicAt = now;
    const alpha = why.includes("alphaMode");
    if (alpha) alphaModeProbeBlockedUntil = now + 800;
    void recordForensic(why, {
      classOverride: "RENDER_CRASH",
      // extract.pixels re-enters render → more alphaMode; skip during TEX_NULL.
      forceBlankProbe: !alpha,
      blank: alpha ? null : undefined,
      scanBroken: !alpha,
      extra: {
        err: err instanceof Error ? err.message : String(err),
        stack: err instanceof Error ? err.stack?.slice(0, 600) : undefined,
      },
    });
  };

  // alphaMode/TEX_NULL: swallow frame so ticker lives, but must not soft-OK forever.
  // Field: lost=false + blank=true + endless swallow → recover never reloads (BLANK_ALIVE).
  {
    const noteOk = (): void => {
      lastOkRenderAt = performance.now();
      alphaModeStormCount = 0;
    };
    const onAlphaModeSwallow = (label: string): void => {
      alphaModeStormCount += 1;
      alphaModeProbeBlockedUntil = performance.now() + 800;
      if (alphaModeStormCount === 1 || alphaModeStormCount % 60 === 0) {
        console.warn(
          `[gl] swallow render alphaMode (${label}, frame skipped)`,
          "storm=",
          alphaModeStormCount,
        );
      }
      const now = performance.now();
      if (
        alphaModeStormCount >= ALPHA_STRIP_AFTER &&
        now - lastAlphaModeStripAt > 2000
      ) {
        lastAlphaModeStripAt = now;
        let stripped = 0;
        try {
          stripped = stripBrokenTextureSprites(
            app.stage as unknown as Parameters<typeof stripBrokenTextureSprites>[0],
          );
        } catch (e) {
          console.warn("[gl] strip broken tex", e);
        }
        console.warn("[gl] stripped broken textures", stripped);
        void recordForensic("alphaMode-strip", {
          classOverride: "RENDER_CRASH",
          blank: null,
          scanBroken: true,
          extra: { stripped, storm: alphaModeStormCount },
        });
        try {
          view.nudgeAfterGpuResume();
        } catch {
          /* ignore */
        }
      }
      if (
        isGameDisplayPhase(phase) &&
        now - lastOkRenderAt >= ALPHA_RELOAD_AFTER_MS &&
        // After strip, wait another window for paint — do not reload→splash
        // while the map may already be visible again.
        now - lastAlphaModeStripAt >= ALPHA_RELOAD_AFTER_MS &&
        alphaModeStormCount >= ALPHA_STRIP_AFTER
      ) {
        softReloadGl("alphaMode-storm");
      }
    };
    const wrapRender = <T extends (...args: never[]) => unknown>(
      raw: T,
      label: string,
    ): T =>
      ((...args: never[]) => {
        try {
          const out = raw(...args);
          noteOk();
          return out;
        } catch (e) {
          if (isTextureSourceNullError(e)) {
            // Block blank extract before forensic (noteRenderCrash used to probe first).
            alphaModeProbeBlockedUntil = performance.now() + 800;
            noteRenderCrash(e, `render-crash:alphaMode:${label}`);
            onAlphaModeSwallow(label);
            return undefined;
          }
          noteRenderCrash(e, `render-crash:other:${label}`);
          throw e;
        }
      }) as T;

    app.render = wrapRender(app.render.bind(app), "app");
    const renderer = app.renderer as unknown as {
      render: (...args: never[]) => unknown;
    };
    renderer.render = wrapRender(
      renderer.render.bind(renderer) as typeof renderer.render,
      "renderer",
    );
  }

  window.addEventListener("error", (ev) => {
    if (!isTextureSourceNullError(ev.error ?? ev.message)) return;
    noteRenderCrash(ev.error ?? ev.message, "render-crash:window-error");
  });

  const dumpForensic = (): string => {
    const text = forensicRing.dumpJson();
    console.info("[gl] forensic dump", text.length, "bytes");
    void copyForensicText(text).then((ok) => {
      setChromeStatus(
        ok ? "取证已复制" : "取证已打印(剪贴板失败)",
        !ok,
      );
    });
    return text;
  };
  (
    window as unknown as { __pixiDumpForensic?: () => string }
  ).__pixiDumpForensic = dumpForensic;
  (
    window as unknown as {
      __pixiDebugPeerCosmetics?: () => Record<string, unknown>;
    }
  ).__pixiDebugPeerCosmetics = () => {
    const dump = {
      peerCosmeticsPref: isPeerCosmeticsVisible(),
      native: !!(
        globalThis as unknown as {
          Capacitor?: { isNativePlatform?: () => boolean };
        }
      ).Capacitor?.isNativePlatform?.(),
      ...view.actors.debugPeerCosmetics(),
      plist: plistAtlasGpuStats(),
    };
    console.info("[gl] peer cosmetics debug", dump);
    return dump;
  };
  touch.onStatusLongPress = () => {
    dumpForensic();
  };

  /** null = inconclusive (do not soft-reload on this alone). */
  const regionLooksBlank = (frame: Rectangle): boolean | null => {
    // extract.pixels → generateTexture → render; during alphaMode storm this
    // re-enters the crash path — treat as unknown, not "blank → reload".
    if (performance.now() < alphaModeProbeBlockedUntil) return null;
    try {
      const extract = (
        app.renderer as unknown as {
          extract?: {
            pixels: (opts: unknown) => {
              pixels?: Uint8ClampedArray;
            };
          };
        }
      ).extract;
      if (!extract?.pixels) return null;
      const out = extract.pixels({
        target: app.stage,
        frame,
        resolution: 1,
      });
      const pixels = out?.pixels;
      if (!pixels?.length) return true;
      let maxC = 0;
      for (let i = 0; i < pixels.length; i += 4) {
        if ((pixels[i + 3] ?? 0) < 8) continue;
        maxC = Math.max(
          maxC,
          pixels[i] ?? 0,
          pixels[i + 1] ?? 0,
          pixels[i + 2] ?? 0,
        );
        if (maxC >= 24) return false;
      }
      return maxC < 24;
    } catch {
      return null;
    }
  };

  /**
   * True when framebuffer presents only clear / HUD chrome.
   * Must sample the map band — HUD bgDeep alone looked "alive" and cancelled reload.
   * null = probe blocked / failed — must not drive softReload (→ splash).
   */
  const canvasLooksBlank = (): boolean | null => {
    try {
      if (isGlContextLost()) return true;
      // Skip probe on title chrome where dark panels are normal.
      if (
        phase === "splash" ||
        phase === "login" ||
        phase === "register" ||
        phase === "servers" ||
        phase === "role" ||
        phase === "create-role"
      ) {
        return false;
      }
      // Map / battle content sits below world HUD (~72px).
      const mapBand = new Rectangle(20, 90, 120, 120);
      return regionLooksBlank(mapBand);
    } catch (e) {
      console.warn("[gl] blank probe", e);
      return null;
    }
  };

  /** Unstick HTML softkeys / D-pad and Pixi hit-testing after GL death. */
  const syncInputAfterGlRecover = (): void => {
    try {
      touch.releaseAll();
      view.clearHeldKeys();
      // Softkeys only — avoid showPhase() which closes overlays / resets menus.
      if (phase === "splash") shell.setSoftkeys({ left: "确定", right: "" });
      else if (phase === "login") shell.setSoftkeys({ left: "登录", right: "返回" });
      else if (phase === "register") shell.setSoftkeys({ left: "注册", right: "返回" });
      else if (phase === "servers") shell.setSoftkeys({ left: "继续", right: "返回" });
      else if (phase === "role") shell.setSoftkeys({ left: "继续", right: "返回" });
      else if (phase === "create-role") shell.setSoftkeys({ left: "确定", right: "返回" });
      else if (phase === "world") {
        syncWorldWalkInput();
        if (approach.open) shell.setSoftkeys({ left: "确定", right: "取消" });
        else if (entertainPanel.visible) shell.setSoftkeys(entertainPanel.softkeys());
        else if (wingPanel.visible) shell.setSoftkeys(wingPanel.softkeys());
        else if (mountPanel.visible) shell.setSoftkeys(mountPanel.softkeys());
        else if (auctionPanel.visible) shell.setSoftkeys(auctionPanel.softkeys());
        else if (wagerPanel.visible) shell.setSoftkeys(wagerPanel.softkeys());
        else if (ladderPanel.visible) shell.setSoftkeys(ladderPanel.softkeys());
        else if (guessPanel.visible) shell.setSoftkeys(guessPanel.softkeys());
        else if (festivalPanel.visible) shell.setSoftkeys(festivalPanel.softkeys());
        else if (yanmenPanel.visible) shell.setSoftkeys(yanmenPanel.softkeys());
        else if (ringQuestPanel.visible) shell.setSoftkeys(ringQuestPanel.softkeys());
        else if (arenaPanel.visible) shell.setSoftkeys(arenaPanel.softkeys());
        else shell.setSoftkeys({ left: "菜单", right: "菜单" });
      } else if (phase === "menu") {
        shell.setSoftkeys({ left: "确定", right: "返回" });
      } else if (phase === "battle") {
        shell.setSoftkeys(
          battle.isAwaitingRound() ||
            battle.isHangupActive() ||
            battle.isAutoBattleActive()
            ? { left: "", right: "" }
            : {
                left: battle.softkeyLeftLabel(),
                right: battle.softkeyRightLabel(),
              },
        );
      } else if (phase === "wash") shell.setSoftkeys(washUi.softkeys());
      else if (phase === "chat") shell.setSoftkeys({ left: "发送", right: "返回" });
      else if (phase === "trade") shell.setSoftkeys(tradeUi.softkeys());
      else if (phase === "dialog" || phase === "system" || phase === "enhance" ||
        phase === "social" || phase === "quest" || phase === "guild" ||
        phase === "map") {
        shell.setSoftkeys({ left: "确定", right: "返回" });
      }
      if (entertainWeb.isExpanded()) {
        shell.setSoftkeys({ left: "最小化", right: "关闭" });
      }
      app.ticker.start();
      const events = (
        app.renderer as unknown as {
          events?: { setTargetElement?: (el: HTMLCanvasElement) => void };
        }
      ).events;
      events?.setTargetElement?.(app.canvas as HTMLCanvasElement);
    } catch (e) {
      console.warn("[gl] sync input", e);
    }
  };

  const recoverDisplayAfterForeground = async (why: string): Promise<void> => {
    if (!rendererReady || recoverInFlight) return;
    const now = performance.now();
    // Debounce focus+visibility storms; allow immediate restore after lost/stall.
    if (
      why !== "contextrestored" &&
      why !== "ticker-lost" &&
      why !== "interval-lost" &&
      why !== "interval-blank" &&
      why !== "interval-stall" &&
      now - lastRecoverAt < 800
    ) {
      return;
    }
    recoverInFlight = true;
    lastRecoverAt = now;
    if (recoverHangTimer) window.clearTimeout(recoverHangTimer);
    recoverHangTimer = window.setTimeout(() => {
      recoverHangTimer = 0;
      if (!recoverInFlight) return;
      console.error("[gl] recover hung", why, glPressureSnapshot());
      recoverInFlight = false;
      void recordForensic(`recover-hung:${why}`, { forceBlankProbe: true });
      softReloadGl("recover-hung");
    }, GL_RECOVER_HANG_MS);
    const refitShellLayout = (): boolean => {
      // Flex-center #phone-wrap (no absolute inset / -50%). Soft reset + force
      // anchors; light transform kick only when still skewed (no canvas hide).
      resetShellViewportLayout();
      forceShellWrapCenteringAnchors();
      wrap.style.transform = SHELL_WRAP_CENTER_TRANSFORM;
      wrap.dataset.fitW = "0";
      wrap.dataset.fitH = "0";
      fitPhoneWrap();
      try {
        shell.layout();
      } catch {
        /* ignore */
      }
      try {
        const canvas = app.canvas as HTMLCanvasElement;
        canvas.style.position = "absolute";
        canvas.style.left = "0";
        canvas.style.top = "0";
        canvas.style.right = "0";
        canvas.style.bottom = "0";
        canvas.style.width = "100%";
        canvas.style.height = "100%";
      } catch {
        /* ignore */
      }
      let after = shellViewportSnapshot();
      if (after.misaligned) {
        repairShellViewportAlignment();
        forceShellWrapCenteringAnchors();
        wrap.style.transform = SHELL_WRAP_CENTER_TRANSFORM;
        after = shellViewportSnapshot();
        if (after.misaligned) {
          console.warn("[gl] shell still misaligned after refit", after);
        }
      }
      return after.misaligned;
    };
    try {
      const stillLostBefore = isGlContextLost();
      console.info(
        "[gl] recover display",
        why,
        "lost=",
        stillLostBefore,
        glPressureSnapshot(),
      );
      void recordForensic(`recover:${why}`, {
        blank: null,
        forceBlankProbe: stillLostBefore || glLostAt > 0,
        extra: { shell: shellViewportSnapshot() },
      });
      refitShellLayout();
      tryRestoreGlContext();
      try {
        syncLcdResolution(lcdCssScaleFromWrap());
        app.renderer.resize(LCD_WIDTH, LCD_HEIGHT);
      } catch (e) {
        console.warn("[gl] resize after resume", e);
      }
      app.ticker.start();
      await new Promise<void>((r) => {
        requestAnimationFrame(() => r());
      });
      // WebView often settles insets/pan one frame late after screen-on.
      refitShellLayout();
      await new Promise<void>((r) => {
        requestAnimationFrame(() => r());
      });
      // Only full UC3 rebake after a real restore — interval thrash froze input.
      // Also rebake when alphaMode storm left blank alive (lost=false).
      let stripped = 0;
      if (
        why === "contextrestored" ||
        why === "interval-stall" ||
        stillLostBefore ||
        glLostAt > 0 ||
        alphaModeStormCount > 0
      ) {
        clearUc3TexCache();
        try {
          await Promise.race([
            refreshAllUc3Labels(),
            new Promise<void>((_, rej) =>
              window.setTimeout(() => rej(new Error("uc3-timeout")), 4000),
            ),
          ]);
        } catch (e) {
          console.warn("[gl] uc3 rebake", e);
        }
        try {
          stripped = stripBrokenTextureSprites(
            app.stage as unknown as Parameters<typeof stripBrokenTextureSprites>[0],
          );
          if (stripped > 0) {
            console.warn("[gl] recover stripped broken textures", stripped);
          }
        } catch (e) {
          console.warn("[gl] recover strip", e);
        }
      }
      try {
        view.nudgeAfterGpuResume();
      } catch (e) {
        console.warn("[gl] scene nudge", e);
      }
      try {
        app.render();
      } catch (e) {
        console.warn("[gl] render after resume", e);
      }
      syncInputAfterGlRecover();
      // Late system-bar / adjustPan settle (single pass like pre-08-28;
      // dual 280+600 soft+hard thrash correlated with post-recover freeze).
      window.setTimeout(() => {
        if (!rendererReady) return;
        const stillSkewed = refitShellLayout();
        try {
          syncLcdResolution(lcdCssScaleFromWrap());
          app.renderer.resize(LCD_WIDTH, LCD_HEIGHT);
          shell.layout();
          app.render();
        } catch (e) {
          console.warn("[gl] late layout refit", e);
        }
        void recordForensic(`recover-late:${why}:280`, {
          blank: null,
          extra: {
            shell: shellViewportSnapshot(),
            stillSkewed,
          },
        });
      }, 280);
      const probeBlank =
        why === "contextrestored" ||
        why === "ticker-lost" ||
        why === "interval-lost" ||
        why === "interval-blank" ||
        why === "interval-stall" ||
        why === "visibility" ||
        why === "focus" ||
        glLostAt > 0 ||
        alphaModeStormCount > 0 ||
        performance.now() - lastOkRenderAt >= RENDER_STALL_MS;
      // Sustained alphaMode stall reloads via softReloadGl("alphaMode-storm"),
      // not here — stallBroken alone used to reload mid-recover while LCD
      // already painted, dumping the player to splash.
      const blankHit = probeBlank ? canvasLooksBlank() : false;
      const stillBroken =
        isGlContextLost() || blankHit === true;
      if (stillBroken) {
        consecutiveLostRecovers += 1;
        console.warn(
          "[gl] recover still blank",
          consecutiveLostRecovers,
          "/",
          GL_SOFT_RELOAD_AFTER,
          "lost=",
          isGlContextLost(),
          "blank=",
          blankHit,
          "stripped=",
          stripped,
          "storm=",
          alphaModeStormCount,
          "msSinceOk=",
          Math.round(performance.now() - lastOkRenderAt),
          glPressureSnapshot(),
        );
        void recordForensic(`recover-still-blank:${why}`, {
          blank: blankHit === true,
          forceBlankProbe: false,
          extra: {
            shell: shellViewportSnapshot(),
            storm: alphaModeStormCount,
            stripped,
            blankHit,
            msSinceOkRender: Math.round(performance.now() - lastOkRenderAt),
          },
        });
        // Just stripped dead sprites — give the next frames a chance to paint
        // before location.reload() (soft reload → title / splash).
        if (
          stripped === 0 &&
          consecutiveLostRecovers >= GL_SOFT_RELOAD_AFTER
        ) {
          softReloadGl("failed-recover");
        } else if (stripped > 0) {
          consecutiveLostRecovers = Math.max(0, consecutiveLostRecovers - 1);
        }
      } else {
        consecutiveLostRecovers = 0;
        glLostAt = 0;
        alphaModeStormCount = 0;
        cancelGlRestoreWatchdog();
        void recordForensic(`recover-ok:${why}`, {
          blank: blankHit === false ? false : null,
          extra: { shell: shellViewportSnapshot(), blankHit, stripped },
        });
      }
    } finally {
      if (recoverHangTimer) {
        window.clearTimeout(recoverHangTimer);
        recoverHangTimer = 0;
      }
      recoverInFlight = false;
    }
  };

  app.canvas.addEventListener(
    "webglcontextlost",
    (ev) => {
      ev.preventDefault();
      glLostAt = performance.now();
      setTextureGcEnabled(false);
      // Drop stuck D-pad / softkey pointer capture from the dying frame.
      touch.releaseAll();
      view.clearHeldKeys();
      console.warn("[gl] webglcontextlost", glPressureSnapshot());
      void recordForensic("webglcontextlost", { blank: true });
      // Pixi only restoreContext() for *forced* loss. Natural OOM on WebView
      // often never fires contextrestored unless we request it.
      window.setTimeout(() => tryRestoreGlContext(), 0);
      armGlRestoreWatchdog();
    },
    false,
  );
  app.canvas.addEventListener(
    "webglcontextrestored",
    () => {
      console.warn("[gl] webglcontextrestored");
      void recordForensic("webglcontextrestored", { forceBlankProbe: true });
      setTextureGcEnabled(true);
      void recoverDisplayAfterForeground("contextrestored");
    },
    false,
  );

  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState !== "visible") {
      // Keepalive still ticks; don't let TextureGC unload the live map/HUD.
      setTextureGcEnabled(false);
      suspendBgm();
      // Immediate tick when leaving foreground — setInterval may freeze next.
      flushBattleSleepsForBackground();
      void session.backgroundKeepAliveTick();
      touch.releaseAll();
      view.clearHeldKeys();
      void recordForensic("visibility-hidden", { blank: null });
      return;
    }
    setTextureGcEnabled(true);
    // Soft reset only (pre-08-28). Full repair/kick here + recover refit
    // double-hit the compositor on every resume and froze input.
    resetShellViewportLayout();
    forceShellWrapCenteringAnchors();
    wrap.style.transform = SHELL_WRAP_CENTER_TRANSFORM;
    wrap.dataset.fitW = "0";
    wrap.dataset.fitH = "0";
    fitPhoneWrap();
    void recordForensic("visibility-visible", {
      forceBlankProbe: true,
      extra: { shell: shellViewportSnapshot() },
    });
    void session.wakeFromBackground();
    void recoverDisplayAfterForeground("visibility");
    void resumeBgmAfterForeground();
  });
  window.addEventListener("focus", () => {
    setTextureGcEnabled(true);
    resetShellViewportLayout();
    forceShellWrapCenteringAnchors();
    wrap.style.transform = SHELL_WRAP_CENTER_TRANSFORM;
    wrap.dataset.fitW = "0";
    wrap.dataset.fitH = "0";
    fitPhoneWrap();
    void recordForensic("window-focus", {
      forceBlankProbe: true,
      extra: { shell: shellViewportSnapshot() },
    });
    void session.wakeFromBackground();
    void recoverDisplayAfterForeground("focus");
    void unlockAndResumeBgm();
  });
  preloadKillStreakSfx();

  let glWatchAcc = 0;
  app.ticker.add(() => {
    glWatchAcc += app.ticker.deltaMS;
    if (glWatchAcc >= 2000) {
      glWatchAcc = 0;
      if (isGlContextLost()) {
        tryRestoreGlContext();
      } else if (glLostAt > 0 && canvasLooksBlank() === true) {
        void recoverDisplayAfterForeground("ticker-lost");
      }
    }
    if (phase === "battle") battle.tick(app.ticker.deltaMS);
    if (miniMap.visible && (phase === "world" || phase === "map")) {
      const sz = view.getMapPixelSize();
      miniMap.update(
        view.getMapPreview(),
        sz.w,
        sz.h,
        view.playerX,
        view.playerY,
        view.liveMinimapMarkers(),
      );
    }
  });

  // Pixi ticker can stop on contextlost; WebView timers usually still run.
  // While still lost: only poke restoreContext — full recover waits for restored
  // (or blank probe after gl thinks it is alive) so we do not thrash UC3/main thread.
  // Also catch BLANK_ALIVE: alphaMode storm with lost=false never sets glLostAt.
  window.setInterval(() => {
    if (!rendererReady) return;
    if (document.visibilityState === "hidden") return;
    if (isGlContextLost()) {
      tryRestoreGlContext();
      return;
    }
    if (glLostAt > 0 && !recoverInFlight) {
      void recoverDisplayAfterForeground("interval-blank");
      return;
    }
    if (
      !recoverInFlight &&
      isGameDisplayPhase(phase) &&
      (alphaModeStormCount >= ALPHA_STRIP_AFTER ||
        performance.now() - lastOkRenderAt >= ALPHA_RELOAD_AFTER_MS)
    ) {
      void recoverDisplayAfterForeground("interval-stall");
    }
  }, 2000);

  // Quiet ring heartbeat (no console). Opt-in verbose: localStorage pix_gl_forensic=1
  // Dump still via __pixiDumpForensic / long-press status.
  window.setInterval(() => {
    if (!rendererReady) return;
    if (document.visibilityState === "hidden") return;
    if (!isGameDisplayPhase(phase)) return;
    forensicHeartbeatN += 1;
    const msSinceOk = performance.now() - lastOkRenderAt;
    if (
      msSinceOk >= RENDER_STALL_MS &&
      performance.now() - lastRenderStallForensicAt > 8000
    ) {
      lastRenderStallForensicAt = performance.now();
      void recordForensic(`render-stall:${Math.round(msSinceOk)}ms`, {
        classOverride: "RENDER_STALL",
        forceBlankProbe: true,
        scanBroken: true,
        extra: { msSinceOkRender: Math.round(msSinceOk) },
      });
    }
    const verboseForensic =
      typeof localStorage !== "undefined" &&
      localStorage.getItem("pix_gl_forensic") === "1";
    if (!verboseForensic && msSinceOk < RENDER_STALL_MS && !isGlContextLost() && glLostAt === 0) {
      /* Still sample into ring without blank-canvas probe every 10s. */
      void recordForensic(`heartbeat:${forensicHeartbeatN}`, {
        blank: null,
      });
      return;
    }
    const probe =
      forensicHeartbeatN % 3 === 0 ||
      isGlContextLost() ||
      glLostAt > 0 ||
      msSinceOk >= RENDER_STALL_MS;
    void recordForensic(`heartbeat:${forensicHeartbeatN}`, {
      forceBlankProbe: probe,
      blank: probe ? undefined : null,
    });
  }, 10_000);
}

main().catch((err) => {
  console.error(err);
  setChromeStatus(err instanceof Error ? err.message : String(err), true);
});
