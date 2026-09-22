/**
 * Normal battle settle panel — same chrome as hangup combatinfo stats block.
 * Assets: pickupimage1.gif labels + dark value pills; no title / no level row.
 */
import { Container, Graphics, Texture } from "pixi.js";
import { CBE, LCD_WIDTH, paintCbePanel } from "./constants";
import { BitmapLabel } from "./BitmapLabel";
import { preferSystemFont } from "../resources/systemFont";
import { loadHangupPanelTextures, valuePill } from "./hangupSettleUi";
import { uiSprite } from "./UiAssets";

const VAL_GREEN = 0x40e860;
const DROP_INK = 0xe8f0e8;
const STOP_BTN_BG = 0x2a5058;
const STOP_BTN_BORDER = 0x70a0b0;
const BAG_BAR = 0x0a1210;
/** Match hangup settle / pickupimage1 row height. */
const SETTLE_FS = 11;

export type BattleSettlePaintOpts = {
  expGain: number;
  goldGain: number;
  recoverHp: number;
  recoverMp: number;
  bagUsed: number;
  bagCap: number;
  /** Drop lines from 4/7.fdata (e.g. 「稀钛硬披 x3」). */
  drops: string[];
  onClose?: () => void;
};

function formatSettleNum(n: number): string {
  return `${n | 0}`;
}

/**
 * Paint PvE battle settle into `host`.
 * Returns false when core textures failed to load.
 */
export async function paintBattleSettlePanel(
  host: Container,
  opts: BattleSettlePaintOpts,
): Promise<boolean> {
  const tex = await loadHangupPanelTextures();
  if (!tex.exp && !tex.gold && !tex.hp) return false;

  host.removeChildren().forEach((c) => c.destroy({ children: true }));
  host.eventMode = "static";

  const drops = opts.drops.map((d) => d.trim()).filter(Boolean);
  const panelX = 10;
  /* Clear BattleEnemyHud (STAGE_TOP+2 + up to 3×SLOT_H ≈ y98). */
  const panelY = 102;
  const panelW = LCD_WIDTH - 20;
  const pad = 8;
  const rowH = 16;
  const bagBarH = 14;
  const dropLineH = 14;
  const closeH = 22;
  const dropBlockH = drops.length > 0 ? drops.length * dropLineH + 4 : 0;
  const panelH =
    pad + rowH * 2 + 4 + bagBarH + dropBlockH + closeH + pad + 4;

  const box = new Graphics();
  paintCbePanel(host, box, panelX, panelY, panelW, panelH, CBE.bgPanelDark);
  host.addChild(box);

  const leftX = panelX + 10;
  const rightX = panelX + Math.floor(panelW / 2) + 2;
  const leftLabelW = 50;
  const rightLabelW = 28;
  const pillH = 13;
  const leftPillW = 52;
  const rightPillW = 52;
  let y = panelY + pad;

  const paintStat = (
    xLabel: number,
    labelW: number,
    yy: number,
    label: Texture | null,
    fallback: string,
    value: string,
    valueColor = VAL_GREEN,
  ) => {
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
    val.position.set(
      pillX + 3,
      yy + Math.max(0, Math.floor((pillH - SETTLE_FS) / 2)),
    );
    val.show(value, valueColor, { outline: false, fontSize: SETTLE_FS });
    host.addChild(val);
  };

  paintStat(
    leftX,
    leftLabelW,
    y,
    tex.exp,
    "获得经验",
    formatSettleNum(opts.expGain),
  );
  paintStat(
    rightX,
    rightLabelW,
    y,
    tex.gold,
    "金钱",
    formatSettleNum(opts.goldGain),
  );
  y += rowH;
  paintStat(
    leftX,
    leftLabelW,
    y,
    tex.hp,
    "自动回血",
    formatSettleNum(opts.recoverHp),
  );
  paintStat(
    rightX,
    rightLabelW,
    y,
    tex.mp,
    "回魔",
    formatSettleNum(opts.recoverMp),
  );
  y += rowH + 4;

  /* Backpack bar — label + pill aligned right (native settle footer). */
  const bar = new Graphics()
    .rect(panelX + 6, y, panelW - 12, bagBarH)
    .fill({ color: BAG_BAR });
  host.addChild(bar);
  const bagLabelX = panelX + panelW - 6 - rightLabelW - rightPillW - 8;
  if (tex.bag && !preferSystemFont()) {
    const bagSpr = uiSprite(tex.bag);
    bagSpr.position.set(bagLabelX, y + 1);
    host.addChild(bagSpr);
  } else {
    const bagLab = new BitmapLabel();
    bagLab.position.set(bagLabelX, y + 1);
    bagLab.show("背包", CBE.inkTip, { outline: false, fontSize: SETTLE_FS });
    host.addChild(bagLab);
  }
  const bagPillX = bagLabelX + rightLabelW;
  const bagPill = valuePill(rightPillW, pillH);
  bagPill.position.set(bagPillX, y);
  host.addChild(bagPill);
  const bagVal = new BitmapLabel();
  bagVal.position.set(
    bagPillX + 3,
    y + Math.max(0, Math.floor((pillH - SETTLE_FS) / 2)),
  );
  const bagUsed = Math.max(0, opts.bagUsed | 0);
  const bagCap = Math.max(1, opts.bagCap | 0);
  bagVal.show(
    `${bagUsed}/${bagCap}`,
    bagUsed >= bagCap ? CBE.inkWarn : VAL_GREEN,
    { outline: false, fontSize: SETTLE_FS },
  );
  host.addChild(bagVal);
  y += bagBarH + 4;

  /* Drop loot lines */
  const dropX = panelX + 10;
  const dropW = panelW - 20;
  for (const line of drops) {
    const lab = new BitmapLabel();
    lab.position.set(dropX, y);
    lab.show(line, DROP_INK, {
      outline: false,
      fontSize: SETTLE_FS,
      maxWidth: dropW,
    });
    host.addChild(lab);
    y += dropLineH;
  }

  /* 关闭 */
  const btnW = 52;
  const btn = new Container();
  const btnBg = new Graphics()
    .roundRect(0, 0, btnW, closeH, 3)
    .fill({ color: STOP_BTN_BG })
    .stroke({ width: 2, color: STOP_BTN_BORDER });
  btn.addChild(btnBg);
  const btnLabel = new BitmapLabel();
  btnLabel.eventMode = "none";
  btnLabel.anchor.set(0.5, 0.5);
  btnLabel.position.set(btnW / 2, closeH / 2);
  btnLabel.show("关闭", 0xf0f8ff, { outline: false, fontSize: SETTLE_FS });
  btn.addChild(btnLabel);
  btn.position.set(panelX + Math.floor((panelW - btnW) / 2), y + 2);
  btn.eventMode = "static";
  btn.cursor = "pointer";
  btn.on("pointertap", (e) => {
    e.stopPropagation();
    opts.onClose?.();
  });
  host.addChild(btn);

  return true;
}

/** Split 4/7.fdata drop text into display lines (skip flask/title tokens). */
export function parseSettleDropLines(fdata: string): string[] {
  const raw = (fdata || "").trim();
  if (!raw) return [];
  const m = raw.match(/^(\d+)\/(\d+)\s*(.*)$/);
  let rest = raw;
  if (m) rest = (m[3] || "").trim();
  if (
    !rest ||
    rest === "战斗胜利" ||
    rest === "切磋胜利" ||
    rest === "切磋失败" ||
    rest.includes("比武") ||
    rest.includes("挑战")
  ) {
    return [];
  }
  const lines: string[] = [];
  for (const part of rest.split(/\n/)) {
    const t = part.trim();
    if (!t) continue;
    if (/^(战斗|切磋|比武|挑战)/.test(t)) continue;
    lines.push(t);
  }
  return lines;
}
