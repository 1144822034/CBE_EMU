import { actorResourceForJobSex, uiActorResourceForJobSex } from "./resources/actor";
import { clearUc3TexCache, refreshAllUc3Labels } from "./ui/BitmapLabel";
import { applyUc3FontId, currentUc3FontId, loadUc3Font } from "./resources/uc3Font";
import { UC3_FONT_OPTIONS, type Uc3FontId } from "./resources/uiFontPreference";
import { Application, Graphics } from "pixi.js";
import { loadSceneIndex, loadSceneMap } from "./resources/loader";
import {
  defaultMonsterLevelsUrl,
  formatMonsterMapName,
  loadMonsterLevelOverrides,
} from "./resources/monsterLevels";
import { loadSMapAliases } from "./resources/smap";
import { defaultLiveGatewayUrl } from "./net/Transport";
import {
  createRechargeOrder,
  formatRechargeYuan,
  type RechargePayType,
} from "./net/recharge";
import { registerAccount } from "./net/register";
import { openPayUrl } from "./net/openPayUrl";
import { SceneView } from "./render/SceneView";
import { countPlus16Worn } from "./render/Plus16Sparkles";
import { GameSession } from "./game/GameSession";
import { PhoneShell } from "./ui/PhoneShell";
import { TitleScreen } from "./ui/TitleScreen";
import { RoleScreen } from "./ui/RoleScreen";
import { CreateRoleScreen } from "./ui/CreateRoleScreen";
import { WorldHud, sceneDisplayName } from "./ui/WorldHud";
import { MenuScreen, type MenuLeaf } from "./ui/MenuScreen";
import { BattleScreen } from "./ui/BattleScreen";
import { DialogPanel } from "./ui/DialogPanel";
import { ApproachPrompt } from "./ui/ApproachPrompt";
import { SystemPanel, type SystemKind } from "./ui/SystemPanel";
import { EnhancePanel } from "./ui/EnhancePanel";
import { WashPanel } from "./ui/WashPanel";
import { SocialPanel } from "./ui/SocialPanel";
import { ArenaHud } from "./ui/ArenaHud";
import { ArenaPanel } from "./ui/ArenaPanel";
import { WagerPanel } from "./ui/WagerPanel";
import { AuctionPanel } from "./ui/AuctionPanel";
import {
  ARENA_NPC_ENTER,
  ARENA_NPC_LEAVE,
  ARENA_NPC_RANK,
  ARENA_NPC_WAGER,
  bracketForLevel,
} from "./net/protocolArena";
import {
  formatAuctionPrice,
  goldToCopper,
  type AuctionListing,
  type AuctionStatus,
} from "./net/protocolAuction";
import { TaskPanel } from "./ui/TaskPanel";
import { ChatPanel } from "./ui/ChatPanel";
import { TradePanel } from "./ui/TradePanel";
import { GuildPanel } from "./ui/GuildPanel";
import { MiniMapOverlay } from "./ui/MiniMapOverlay";
import { WorldMapPanel } from "./ui/WorldMapPanel";
import { TouchControls, type TouchDir } from "./ui/TouchControls";
import { CBE, LCD_HEIGHT, LCD_WIDTH } from "./ui/constants";
import { setLcdDisplayScale } from "./ui/BitmapLabel";
import {
  imeShellCaptureLockFromWrap,
  imeShellIsActive,
  imeShellLockedSize,
} from "./ui/imeShell";
import { dismissOverlayIme } from "./ui/dismissOverlayIme";
import type { RoleRow } from "./net/protocol";
import { isBattleMedicineOrFlask } from "./net/protocolExtras";
import { isMapHealSkill } from "./resources/skill";
import { warmItemCatalogs } from "./resources/itemCatalog";
import {
  installBgmGestureUnlock,
  resumeBgmAfterForeground,
  suspendBgm,
  toggleSoundEnabled,
  unlockAndResumeBgm,
} from "./audio/GameBgm";

/** Cap GPU buffer; HTML softkeys stay native DPI regardless. */
const LCD_RESOLUTION_CAP = 3;

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
    conflict: "出价冲突，请重试",
    tx: "事务失败",
    refund: "退款登记失败",
    "bid-row": "出价写入失败",
    commit: "提交失败",
    "not-owner": "非卖家不能下架",
    "has-bid": "已有出价，无法下架",
    "bag-full": "背包已满，无法取回",
    "buyout-low": "一口价须不低于起拍价",
    "no-buyout": "该拍品无一口价",
    "buyout-gone": "当前价已超一口价",
    payout: "卖家结算失败",
    "winner-item": "物品发放登记失败",
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

async function main(): Promise<void> {
  const wrap = document.getElementById("phone-wrap");
  if (!wrap) throw new Error("missing #phone-wrap");
  const stage = document.getElementById("stage");
  if (!stage) throw new Error("missing #stage");

  const touchBar = document.getElementById("touch-bar");
  const app = new Application();
  let rendererReady = false;
  const syncTouchBarWidth = () => {
    // Full stage width so L/R softkeys sit beside ▲ and aren't clipped.
    if (!touchBar) return;
    const w = stage.clientWidth || wrap.clientWidth || 0;
    if (w > 2) touchBar.style.width = `${w}px`;
  };
  const lcdCssScaleFromWrap = (): number => {
    const w = wrap.clientWidth || Number.parseFloat(wrap.style.width) || LCD_WIDTH;
    return Math.max(1, w / LCD_WIDTH);
  };
  const syncLcdResolution = (cssScale: number) => {
    setLcdDisplayScale(cssScale);
    if (!rendererReady) return;
    const res = Math.min(LCD_RESOLUTION_CAP, Math.max(1, cssScale));
    if (Math.abs(app.renderer.resolution - res) < 0.05) return;
    app.renderer.resolution = res;
    app.renderer.resize(LCD_WIDTH, LCD_HEIGHT);
  };
  const fitPhoneWrap = () => {
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
    if (prevW > 0 && prevH > 0 && !imeShellIsActive()) {
      const dW = Math.abs(sw - prevW);
      const dH = Math.abs(sh - prevH);
      const heightJitterOnly = dW < 16 && dH < Math.max(64, prevH * 0.1);
      if (heightJitterOnly) {
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
  window.addEventListener("resize", fitPhoneWrap);
  window.addEventListener("orientationchange", () => setTimeout(fitPhoneWrap, 50));
  window.addEventListener("ime-shell", () => {
    // Touch-bar hide expands stage — one fresh fit, then lock that size.
    if (imeShellIsActive()) {
      wrap.dataset.fitW = "0";
      wrap.dataset.fitH = "0";
      fitPhoneWrap();
      imeShellCaptureLockFromWrap();
      fitPhoneWrap();
    } else {
      wrap.style.transform = "";
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
  await app.init({
    background: "#05070a",
    antialias: false,
    resolution: Math.min(LCD_RESOLUTION_CAP, Math.max(1, bootScale)),
    autoDensity: true,
    width: LCD_WIDTH,
    height: LCD_HEIGHT,
  });
  rendererReady = true;
  wrap.appendChild(app.canvas);

  const shell = new PhoneShell(app);
  const touch = new TouchControls();
  touchStatus = touch;
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

  // server_monsters overrides via gateway → admin; builtin fallback on failure.
  await loadMonsterLevelOverrides(
    defaultMonsterLevelsUrl(defaultLiveGatewayUrl()),
  );

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
  const auctionPanel = new AuctionPanel();
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
  auctionPanel.visible = false;
  taskUi.visible = false;
  chatPanel.visible = false;
  tradeUi.visible = false;
  guildUi.visible = false;
  worldMap.visible = false;

  const view = new SceneView(app);
  view.root.visible = false;

  const syncWorldStatusBadges = () => {
    // 「斗」= hangup (vip_effect.gif). 「练」awaits training-buff tracking.
    view.setDouVisible(session.hangupActive || session.hangupDesired);
  };

  shell.content.addChild(
    title,
    roles,
    createRole,
    view.root,
    hud,
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
    auctionPanel,
    taskUi,
    chatPanel,
    tradeUi,
    guildUi,
    worldMap,
  );
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
      auctionPanel.visible || arenaPanel.visible || wagerPanel.visible;
    if (phase === "world" && !overlayOpen) {
      view.inputEnabled = true;
    } else {
      view.inputEnabled = false;
      view.clearHeldKeys();
    }
  };

  const openWagerPanel = async () => {
    const board = await session.fetchWagerBoard();
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
        const tip = r?.claimed
          ? `已领取${r.claimed}项${r.failed ? ` 失败${r.failed}` : ""}`
          : `领取失败${r?.failed ? `:${r.failed}` : ""}`;
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
      void session.auctionBuyout(listingId).then((r) => {
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
    const page = await session.fetchArenaRank(j, b, 0);
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
            ? "比武奖励已领取"
            : `领奖失败(${r?.result ?? "?"})`;
        await openArenaPanel(arenaPanel.job, arenaPanel.bracket);
        arenaPanel.showTip(tip);
        shell.setSoftkeys(arenaPanel.softkeys());
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
    tradeUi.visible = p === "trade";
    guildUi.visible = p === "guild";
    worldMap.visible = p === "map";
    miniMap.visible = mapUiMode === "mini" && (p === "world" || p === "map");
    view.inputEnabled = p === "world";
    // Softkeys are HTML (#touch-bar); canvas SoftkeyBar stays hidden.
    if (p !== "world") {
      approach.hide();
      if (auctionPanel.visible) auctionPanel.close();
      if (wagerPanel.visible) wagerPanel.close();
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
    else if (p === "trade") shell.setSoftkeys({ left: "确定", right: "取消" });
    else if (p === "guild") shell.setSoftkeys({ left: "确定", right: "返回" });
    else if (p === "map") shell.setSoftkeys({ left: "确定", right: "返回" });
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
    // Clear outdoor combat markers before async SCE load so city hops cannot
    // keep bee/ghostfire sprites or F-key targets from the previous map.
    session.sceneMonsters = [];
    view.actors.setSceneMonsters([]);
    const scene = await loadSceneMap(name);
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
    await view.showScene(scene, x, y);
  };

  const openTaskList = async () => {
    showPhase("quest");
    setChromeStatus("任务列表…");
    const lists = await session.fetchTaskLists();
    taskUi.open(lists.accepted, lists.candidates, "accepted");
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
      system.setLines(["正在拉取服务端属性汇总…"]);
      void session.openCharacterStats().then(() => {
        if (session.characterStats) system.setStats(session.characterStats);
      });
    }
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
    },
    onEnterScene: async (scene, x, y, opts) => {
      const hasPos = opts?.hasPos !== false;
      // Always rebuild from the server scene key for this enter (no prior-role map).
      view.invalidateMap();
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
        void view.setPlayerNameplate(role.name, titleTag, titleGif);
      } else {
        void view.setPlayerActor(0, 0);
        void view.setPlayerNameplate(session.roleName || "侠客", titleTag, titleGif);
      }
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
    },
    onNpcTaskPrompts: (prompts) => {
      view.actors.setTaskPrompts(prompts);
    },
    onPeers: (peers, removed) => view.actors.applyPeers(peers, removed),
    onPeerDesignation: (info) => {
      view.actors.applyPeerTitle(info.actorId, info.name, info.gif);
    },
    onPeerMoves: (moves) => view.actors.applyPeerMoves(moves),
    onNearbyPage: (page) => {
      session.nearbyList = page.peers;
      if (phase === "social") social.openNearby(page.peers);
      setChromeStatus(`周围玩家 ×${page.peers.length}`);
    },
    onTeamUpdate: (roster) => {
      if (phase === "social") social.openTeam(roster.members, roster.leaderId);
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
      if (phase === "social") social.openFriends(friends);
      setChromeStatus(`好友 ×${friends.length}`);
    },
    onSocialInvite: (inv) => {
      const title =
        inv.kind === "team"
          ? "组队邀请"
          : inv.kind === "friend"
            ? "好友邀请"
            : "交易邀请";
      const body =
        inv.kind === "team"
          ? `${inv.name} 邀请你组队`
          : inv.kind === "friend"
            ? `${inv.name} 邀请加你为好友`
            : `${inv.name} 邀请交易`;
      const acceptAction =
        inv.kind === "team" ? 220 : inv.kind === "friend" ? 230 : 242;
      const refuseAction =
        inv.kind === "team" ? 221 : inv.kind === "friend" ? 231 : 243;
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
      } else if (n.kind === "guild_sent") {
        hud.pushChat("[帮]已发送入帮邀请");
        setChromeStatus("入帮邀请已发送");
        if (guildUi.visible) guildUi.showTip("入帮邀请已发送");
        if (social.visible) social.showTip("入帮邀请已发送");
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
      }
      if (u.applyResult) {
        hud.pushChat(`[帮]${u.applyResult.tip}`);
        setChromeStatus(u.applyResult.tip, u.applyResult.result !== 1);
        if (guildUi.visible) guildUi.showTip(u.applyResult.tip);
      }
      if (u.leaveResult) {
        hud.pushChat(`[帮]${u.leaveResult.tip}`);
        setChromeStatus(u.leaveResult.tip, u.leaveResult.result !== 1);
        if (u.leaveResult.result === 1) {
          guildUi.openHub(false, "无帮派");
          showPhase("guild");
        }
        if (guildUi.visible) guildUi.showTip(u.leaveResult.tip);
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
      }
      if (u.members) {
        guildUi.setMembers(
          u.members.members,
          session.guildMemberPage,
          u.members.allpgs,
          u.members.cnum,
          u.members.mnum,
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
        showGameTip("对方报价已更新");
        return;
      }
      if (u.kind === "offer_ack") {
        tradeUi.setStatus(u.tip, false, u.result === 1);
        if (u.tip) showGameTip(u.tip);
        return;
      }
      if (u.kind === "waiting") {
        tradeUi.setStatus("等待对方确认", true, true);
        showGameTip("等待对方确认交易");
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
      social.showPlayerInfo(info);
      showPhase("social");
    },
    onOtherEquip: (eq) => {
      social.showOtherEquip(eq);
      showPhase("social");
    },
    onDesignationUpdate: (info) => {
      const name =
        session.roleName ||
        activeRole?.name ||
        pendingRoles.find((r) => r.roleId === session.selectedRoleId)?.name ||
        "侠客";
      void view.setPlayerNameplate(name, info.name, info.gif);
      hud.setIdentity(name, session.scene, info.name);
    },
    onBattle: (state) => {
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
        battle.setHangupActive(session.hangupActive);
        battle.setAutoBattleActive(session.autoBattleActive);
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
        if (session.hangupActive || session.autoBattleActive) {
          shell.setSoftkeys({ left: "", right: "" });
        }
        if (session.shouldInBattleAuto()) {
          armInBattleAuto(session.hangupActive ? "挂机出手…" : "自动出手…");
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
          session.hangupActive ||
          session.autoBattleActive;
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
      dialog.show(d);
      showPhase("dialog");
      setChromeStatus(`对话 · ${d.options.length} 选项`);
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
        hud.pushChat(line.text, line.type);
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
      session.sceneMonsters = view.actors.liveSceneMonsters();
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
    session.sceneMonsters = view.actors.liveSceneMonsters();
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

  /** Client-side in-battle auto 4/2 (挂机 or [自动]); never starts encounters. */
  const fireInBattleAuto = (tip: string): boolean => {
    // Prefer battle UI phase over session.inBattle — mid-fight scene packets
    // can clear the flag while mmBattle is still showing.
    if (!session.shouldInBattleAuto() || phase !== "battle" || !battle.visible) {
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
  const armInBattleAuto = (tip = "自动出手…") => {
    if (autoArmTimer != null) {
      window.clearTimeout(autoArmTimer);
      autoArmTimer = null;
    }
    const started = performance.now();
    const tick = () => {
      autoArmTimer = null;
      if (!session.shouldInBattleAuto() || phase !== "battle" || !battle.visible) {
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
    // Engaged SCE spawn stays hidden; refresh at birth point after 20s.
    view.actors.armSuppressedRespawns(20_000);
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
        roles.setTip("OK键删除 · 需输入角色名确认");
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
        );
      });
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
      void session.openFriends(0);
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
      social.openRank({ type: 0, tabs: ["等级榜", "财富榜", "PK榜"], rows: [], myorder: 0 });
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
      guildUi.openHub(inGuild, name || "无帮派");
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
    if (!seq || !crystal) return;
    const prev = await session.previewEnhanceWash(seq, unlock, crystal);
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
        showGameTip("洗练中…");
        const res = await session.confirmEnhanceWash(seq, unlock, crystal);
        await session.openBag();
        washUi.setBag(session.bagItems);
        const fresh = session.bagItems.find((b) => b.seq === seq);
        const reopen = await session.openEnhanceWash(seq);
        if (reopen && reopen.result === 1 && fresh) {
          washUi.openWash(fresh, session.bagItems, reopen, {
            keepUnlock: unlock,
            keepCrystalId: crystal,
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

  system.onClose = () => showPhase("world");
  system.onAction = (kind, action, item) => {
    if (action === "refresh") return;
    if (kind === "bag" || kind === "enhance") {
      const bag = item && "seq" in item ? (item as import("./net/protocolExtras").BagItem) : null;
      if (!bag) return;
      if (action === "use") {
        void (async () => {
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
        void (async () => {
          showGameTip(`合成 ${bag.name}…`);
          const fb = await session.combineCrystal(bag.itemId, bag.seq);
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
          if (fb.ok) await session.openBag();
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
        void session.discardItem(bag.itemId, bag.seq, bag.count).then(() => void session.openBag());
        showGameTip(`丢弃 ${bag.name}`);
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
        void session.equipItem(bag.itemId, bag.seq).then(() => refreshEquip());
        showGameTip(`装备 ${bag.name}`);
        return;
      }
      if (action === "swap") {
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
  };

  social.onClose = () => showPhase("world");
  taskUi.onClose = () => showPhase("world");
  taskUi.onAction = (action, taskId) => {
    if (action === "refresh") {
      void session.fetchTaskLists().then((lists) => {
        taskUi.setLists(lists.accepted, lists.candidates);
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
      });
      return;
    }
    if (action === "accept" && taskId) {
      void session.acceptTask(taskId).then(async () => {
        showGameTip("已接取");
        const lists = await session.fetchTaskLists();
        taskUi.setLists(lists.accepted, lists.candidates);
      });
    }
  };
  chatPanel.onClose = () => showPhase("world");
  chatPanel.onSend = async (text, type) => {
    const r = await session.sendChat(text, type);
    if (r.tip) showGameTip(r.tip);
    if (!r.ok) {
      if (!r.tip) showGameTip("发送失败");
      return false;
    }
    chatPanel.setEntries(hud.getChatEntries());
    showGameTip(`已发送 · ${text.slice(0, 12)}`);
    return true;
  };
  social.onAction = (kind, action, row) => {
    if (action === "player_info" && row?.id) {
      void session.requestPlayerInfo(row.id);
      showGameTip(`查看 ${row.name}`);
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
    if (action === "trade" && row?.id) {
      void session.tradeInvite(row.id);
      hud.pushChat(`[交]已邀请 ${row.name} 交易`);
      showGameTip(`交易邀请 ${row.name}`);
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
      else if (kind === "friends") void session.openFriends(0);
      else void session.openNearbyPlayers(0);
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
      void session.openRanking(type, 0).then((page) => {
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
        });
        showGameTip(
          `排行榜 · ${page.tabs[page.type] ?? row?.name ?? "等级榜"}`,
        );
      });
    }
  };

  guildUi.onClose = () => showPhase("world");
  guildUi.onAction = (action, payload) => {
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
  tradeUi.onAction = (action, payload) => {
    if (action === "add_bag_item" && payload && "itemId" in payload) {
      const bag = payload as import("./net/protocolExtras").BagItem;
      let count = 1;
      if (bag.kind !== "equip" && bag.count > 1) {
        const raw = window.prompt(`放入数量（最多${bag.count}）`, "1") ?? "1";
        count = Math.max(1, Math.min(bag.count, Number(raw) || 1));
      }
      tradeUi.addFromBag(bag, count);
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
    // Closing approach prompt while still in range keeps latch (no spam).
    showPhase("world");
  };
  dialog.onPick = (action, value, label) => {
    setChromeStatus(`NPC ${label} a=${action} v=${value >>> 0}`);
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
    if (action === 261) {
      dialog.hide();
      void session.confirmQuickRepair().then((r) => {
        hud.pushChat(`[系]${r.message}`);
        showGameTip(r.message);
        showPhase("world");
      });
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
    if ((value >>> 0) === ARENA_NPC_RANK) {
      dialog.hide();
      void openArenaPanel();
      return;
    }
    if ((value >>> 0) === ARENA_NPC_ENTER || (value >>> 0) === ARENA_NPC_LEAVE) {
      dialog.hide();
      void session.pickNpcOption(action, value, label).then(() => {
        if ((value >>> 0) === ARENA_NPC_LEAVE) arenaPanel.close();
        if (!dialog.visible) showPhase("world");
        if (session.isArenaScene()) showGameTip("已进入比武擂台");
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
    session.noteManualBattleOperate(0);
    const id = battle.targetIndex();
    if (battle.isAwaitingRound()) {
      shell.setSoftkeys({ left: "", right: "" });
    }
    void session.battleOperate(id, 0).catch(() => {
      if (phase === "battle") battle.clearAwaitingRound();
    });
  };
  battle.onSkill = (operate) => {
    session.noteManualBattleOperate(operate);
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
    battle.setDeathPromptBag(hasRevivalStone());
    setChromeStatus(
      hasRevivalStone()
        ? "死亡 · 确定使用复活石 / 右键普通复活"
        : "死亡 · 确定普通复活",
    );
    shell.setSoftkeys({
      left: battle.softkeyLeftLabel(),
      right: battle.softkeyRightLabel(),
    });
  };
  battle.onDeathPromptChoice = (result) => {
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
    session.autoBattleActive = !session.autoBattleActive;
    const on = session.autoBattleActive;
    hud.pushChat(on ? "[战]自动开启" : "[战]自动关闭");
    showGameTip(on ? "自动战斗开启" : "自动战斗关闭");
    if (on) {
      // Retry until seat is free — covers re-enable after 关闭/手动技能.
      armInBattleAuto("自动出手…");
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
    session.autoBattleActive = false;
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
    if (!session.shouldInBattleAuto()) return;
    window.setTimeout(() => {
      armInBattleAuto(session.hangupActive ? "挂机出手…" : "自动出手…");
    }, 400);
  };

  hud.onMenu = () => showPhase("menu");
  hud.onHome = () => cycleMapUi();
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
    if (phase === "world") {
      // Overlays on world (auction/arena/wager) must win over 菜单 softkeys.
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
              left: "确定",
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
      if (which === "left") tradeUi.confirm();
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
    if (phase === "world" && auctionPanel.visible) {
      auctionPanel.confirm();
      shell.setSoftkeys(auctionPanel.softkeys());
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
              left: "确定",
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
      if (phase === "chat") {
        chatPanel.cycleChannel(delta);
        return;
      }
      if (phase === "trade" && tradeUi.isMoneyMode()) {
        // Money uses HTML numeric IME — ←→ refocuses the field.
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
      if (k === "arrowup" || k === "w") dialog.move(-1);
      if (k === "arrowdown" || k === "s") dialog.move(1);
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
      if (tradeUi.isMoneyMode()) {
        // Digits go to ChatIme when focused; hardware fallback when blurred.
        if (k === "arrowleft" || k === "a") {
          tradeUi.moveHorizontal(-1);
          e.preventDefault();
        } else if (k === "arrowright" || k === "d") {
          tradeUi.moveHorizontal(1);
          e.preventDefault();
        } else if (tradeUi.typeMoneyKey(k === "backspace" ? "backspace" : e.key)) {
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
      if (phase === "world" && (auctionPanel.visible || arenaPanel.visible || wagerPanel.visible)) {
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
      if (phase === "world" && (auctionPanel.visible || arenaPanel.visible || wagerPanel.visible)) {
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
                left: "确定",
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
  title.setStatus("开始游戏/登陆游戏=创建或登录账号");
  setChromeStatus("就绪");

  // Screen unlock / return to app: heal Live WS if OEM suspended the WebView.
  // Native FGS also injects window.__pixiNativeKeepAlive every ~2s while in-world.
  (window as Window & { __pixiNativeKeepAlive?: () => void }).__pixiNativeKeepAlive =
    () => {
      void session.backgroundKeepAliveTick();
    };
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState !== "visible") {
      suspendBgm();
      // Immediate tick when leaving foreground — setInterval may freeze next.
      void session.backgroundKeepAliveTick();
      return;
    }
    void session.wakeFromBackground();
    void resumeBgmAfterForeground();
  });
  window.addEventListener("focus", () => {
    void session.wakeFromBackground();
    void unlockAndResumeBgm();
  });
  installBgmGestureUnlock();

  app.ticker.add(() => {
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
}

main().catch((err) => {
  console.error(err);
  setChromeStatus(err instanceof Error ? err.message : String(err), true);
});
