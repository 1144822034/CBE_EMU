/**
 * Hangup settle panel — match native mmBattle combatinfo chrome.
 * Assets: concernf1, concernp, effect, help, addtime, pickupimage1.
 * (UI2 is NOT used for value bars — those are flat dark pills.)
 */
import { Container, Graphics, Texture } from "pixi.js";
import { CBE, LCD_WIDTH, paintCbePanel } from "./constants";
import { BitmapLabel, measureUiTextWidth } from "./BitmapLabel";
import { preferSystemFont } from "../resources/systemFont";
import { formatBattleInsightRemainMmSs } from "../net/protocolExtras";
import { loadUiCrop, tryLoadUiTexture, uiSprite } from "./UiAssets";

const PICKUP_CELL = 12;
/** Match pickupimage1 glyph height (~11px) for Songti value/label rows. */
const SETTLE_FS = 11;
const VAL_GREEN = 0x40e860;
const TITLE_GOLD = 0xf0d060;
const STOP_BTN_BG = 0x2a5058;
const STOP_BTN_BORDER = 0x70a0b0;

/** concernf1.gif: 「挂机中」(0..26) | mid glyph | 「结束」(39..65) */
const CONCERN_TITLE = { x: 0, y: 0, w: 26, h: 13 } as const;

export type HangupPanelTextures = {
  concernp: Texture | null;
  concernTitle: Texture | null;
  effect: Texture | null;
  help: Texture | null;
  addtime: Texture | null;
  exp: Texture | null;
  gold: Texture | null;
  hp: Texture | null;
  mp: Texture | null;
  battles: Texture | null;
  bag: Texture | null;
};

let panelCache: HangupPanelTextures | null = null;

function cacheReady(t: HangupPanelTextures): boolean {
  return !!(t.battles || t.exp || t.concernTitle || t.concernp);
}

export function preloadHangupPanelTextures(): void {
  void loadHangupPanelTextures();
}

export async function loadHangupPanelTextures(): Promise<HangupPanelTextures> {
  if (panelCache && cacheReady(panelCache)) return panelCache;
  const sheet = "pickupimage1.gif";
  const loaded: HangupPanelTextures = {
    concernp: await tryLoadUiTexture("concernp.gif"),
    concernTitle: await loadUiCrop(
      "concernf1.gif",
      CONCERN_TITLE.x,
      CONCERN_TITLE.y,
      CONCERN_TITLE.w,
      CONCERN_TITLE.h,
    ),
    effect: await tryLoadUiTexture("effect.gif"),
    help: await tryLoadUiTexture("help.gif"),
    addtime: await tryLoadUiTexture("addtime.gif"),
    exp: await loadUiCrop(sheet, 0, 0, PICKUP_CELL * 4, 11),
    gold: await loadUiCrop(sheet, PICKUP_CELL * 4, 0, PICKUP_CELL * 2, 11),
    hp: await loadUiCrop(sheet, PICKUP_CELL * 6, 0, PICKUP_CELL * 4, 11),
    mp: await loadUiCrop(sheet, PICKUP_CELL * 10, 0, PICKUP_CELL * 2, 11),
    battles: await loadUiCrop(sheet, PICKUP_CELL * 12, 0, PICKUP_CELL * 4, 11),
    bag: await loadUiCrop(sheet, PICKUP_CELL * 16, 0, PICKUP_CELL * 2, 11),
  };
  if (cacheReady(loaded)) panelCache = loaded;
  return loaded;
}

/** Dark recessed pill behind a numeric value (native hangup / battle settle). */
export function valuePill(w: number, h: number): Graphics {
  return new Graphics()
    .roundRect(0, 0, w, h, 3)
    .fill({ color: 0x0a1210 })
    .stroke({ width: 1, color: 0x1e3028 });
}

export type HangupPanelTally = {
  completed: number;
  maximum: number;
  totalExp: number;
  totalGold: number;
  totalHpRecovered: number;
  totalMpRecovered: number;
};

export type HangupPanelPaintOpts = {
  tally: HangupPanelTally;
  bagUsed: number;
  bagCap: number;
  /** Wall-clock remaining for item 828; 0 when inactive. */
  insightRemainSec?: number;
  onStop?: () => void;
  /** 「补充时间」— auto-use bag 战斗心得 (828). */
  onAddTime?: () => void;
};

/**
 * Paint hangup combatinfo panel into `host`.
 * Returns false only when no core textures loaded (caller may fall back).
 */
export async function paintHangupPanel(
  host: Container,
  opts: HangupPanelPaintOpts,
): Promise<boolean> {
  const tex = await loadHangupPanelTextures();
  if (!cacheReady(tex)) return false;

  host.removeChildren().forEach((c) => c.destroy({ children: true }));
  host.eventMode = "static";

  const panelX = 10;
  /* Clear BattleEnemyHud top-left vitals strip. */
  const panelY = 102;
  const panelW = LCD_WIDTH - 20;
  const pad = 8;
  const rowH = 16;
  const titleH = 18;
  const insightH = 86;
  const stopH = 22;
  const panelH =
    pad + titleH + rowH * 3 + 6 + insightH + stopH + pad + 4;

  const box = new Graphics();
  paintCbePanel(host, box, panelX, panelY, panelW, panelH, CBE.bgPanelDark);
  host.addChild(box);

  /* Title: concernp + concernf1「挂机中」 (Songti → text so font switch applies). */
  const titleY = panelY + 5;
  const titleCx = LCD_WIDTH / 2;
  const textTitle = preferSystemFont();
  if (tex.concernp && !textTitle) {
    const icon = uiSprite(tex.concernp);
    icon.position.set(titleCx - 36, titleY);
    host.addChild(icon);
  }
  if (tex.concernTitle && !textTitle) {
    const t = uiSprite(tex.concernTitle);
    t.position.set(titleCx - 12, titleY + 2);
    host.addChild(t);
  } else {
    const lab = new BitmapLabel();
    lab.anchor.set(0.5, 0);
    lab.position.set(titleCx, titleY + 2);
    lab.show("挂机中", TITLE_GOLD, { outline: false, fontSize: SETTLE_FS });
    host.addChild(lab);
  }

  /* Two-column stats */
  const leftX = panelX + 10;
  const rightX = panelX + Math.floor(panelW / 2) + 2;
  const leftLabelW = 50;
  const rightLabelW = 28;
  const pillH = 13;
  const leftPillW = 52;
  const rightPillW = 52;
  let y = panelY + pad + titleH;

  const paintStat = (
    xLabel: number,
    labelW: number,
    yy: number,
    label: Texture | null,
    fallback: string,
    value: string,
    valueColor = VAL_GREEN,
  ) => {
    // Songti: text labels (GIF stays on UC3/fusion so chrome matches pixel packs).
    const textLabels = preferSystemFont();
    if (label && !textLabels) {
      const spr = uiSprite(label);
      spr.position.set(xLabel, yy + 1);
      host.addChild(spr);
    } else {
      const lab = new BitmapLabel();
      lab.position.set(xLabel, yy);
      lab.show(fallback, CBE.inkTip, { outline: false, fontSize: SETTLE_FS });
      host.addChild(lab);
    }
    const pillW = labelW === leftLabelW ? leftPillW : rightPillW;
    const pillX = xLabel + labelW;
    const pill = valuePill(pillW, pillH);
    pill.position.set(pillX, yy);
    host.addChild(pill);
    const val = new BitmapLabel();
    val.position.set(pillX + 3, yy + Math.max(0, Math.floor((pillH - SETTLE_FS) / 2)));
    val.show(value, valueColor, { outline: false, fontSize: SETTLE_FS });
    host.addChild(val);
  };

  const { tally, bagUsed, bagCap } = opts;
  paintStat(
    leftX,
    leftLabelW,
    y,
    tex.battles,
    "战斗场次",
    `${tally.completed}`,
  );
  paintStat(
    rightX,
    rightLabelW,
    y,
    tex.bag,
    "背包",
    `${bagUsed}/${bagCap}`,
    bagUsed >= bagCap ? CBE.inkWarn : VAL_GREEN,
  );
  y += rowH;
  paintStat(leftX, leftLabelW, y, tex.exp, "获得经验", `${tally.totalExp}`);
  paintStat(rightX, rightLabelW, y, tex.gold, "金钱", `${tally.totalGold}`);
  y += rowH;
  paintStat(
    leftX,
    leftLabelW,
    y,
    tex.hp,
    "自动回血",
    `${tally.totalHpRecovered}`,
  );
  paintStat(
    rightX,
    rightLabelW,
    y,
    tex.mp,
    "回魔",
    `${tally.totalMpRecovered}`,
  );
  y += rowH + 4;

  /* Insight block */
  const insX = panelX + 8;
  const insW = panelW - 16;
  const remainSec = Math.max(0, opts.insightRemainSec ?? 0);
  const insightActive = remainSec > 0 || tally.maximum >= 200;
  const insightBonus = insightActive
    ? {
        time: formatBattleInsightRemainMmSs(remainSec),
        exp: "+20%",
        battles: "不限次",
      }
    : { time: "00:00", exp: "+0%", battles: "0" };

  if (tex.effect && !preferSystemFont()) {
    const eff = uiSprite(tex.effect);
    eff.position.set(insX, y);
    host.addChild(eff);
  } else {
    const lab = new BitmapLabel();
    lab.position.set(insX, y);
    lab.show("战斗心得效果", TITLE_GOLD, { outline: false, fontSize: SETTLE_FS });
    host.addChild(lab);
  }
  if (tex.help) {
    const h = uiSprite(tex.help);
    h.position.set(insX + insW - h.width, y);
    h.eventMode = "static";
    h.cursor = "pointer";
    host.addChild(h);
  }
  y += 16;

  /* Insight body — same mint as 「有效时间」; roomier gaps vs cramped native shot. */
  const insightInk = VAL_GREEN;
  const insightLineH = 15;
  const timeLab = new BitmapLabel();
  timeLab.position.set(insX, y);
  timeLab.show("有效时间", insightInk, { outline: false, fontSize: SETTLE_FS });
  host.addChild(timeLab);
  const timeVal = new BitmapLabel();
  const timeGap = measureUiTextWidth("有效时间", SETTLE_FS) + 8;
  timeVal.position.set(insX + timeGap, y);
  timeVal.show(insightBonus.time, insightInk, {
    outline: false,
    fontSize: SETTLE_FS,
  });
  host.addChild(timeVal);
  if (tex.addtime) {
    const add = uiSprite(tex.addtime);
    add.position.set(insX + insW - add.width, y - 1);
    add.eventMode = "static";
    add.cursor = "pointer";
    add.on("pointertap", (e) => {
      e.stopPropagation();
      opts.onAddTime?.();
    });
    host.addChild(add);
  }
  y += insightLineH;

  const lines = [
    `获得经验 ${insightBonus.exp}`,
    `挂机场次提升至 ${insightBonus.battles}`,
    "自动修理，出售多余物品",
  ];
  for (const line of lines) {
    const lab = new BitmapLabel();
    lab.position.set(insX, y);
    lab.show(line, insightInk, {
      outline: false,
      fontSize: SETTLE_FS,
      maxWidth: insW,
    });
    host.addChild(lab);
    y += insightLineH;
  }
  y += 6;

  /* 中止挂机 — flat blue softkey-style button */
  const btnW = 100;
  const btn = new Container();
  const btnBg = new Graphics()
    .roundRect(0, 0, btnW, stopH, 3)
    .fill({ color: STOP_BTN_BG })
    .stroke({ width: 2, color: STOP_BTN_BORDER });
  btn.addChild(btnBg);
  const btnLabel = new BitmapLabel();
  btnLabel.eventMode = "none";
  btnLabel.anchor.set(0.5, 0.5);
  btnLabel.position.set(btnW / 2, stopH / 2);
  btnLabel.show("中止挂机", 0xf0f8ff, { outline: false, fontSize: SETTLE_FS });
  btn.addChild(btnLabel);
  btn.position.set(panelX + Math.floor((panelW - btnW) / 2), y);
  btn.eventMode = "static";
  btn.cursor = "pointer";
  btn.on("pointertap", (e) => {
    e.stopPropagation();
    opts.onStop?.();
  });
  host.addChild(btn);

  return true;
}
