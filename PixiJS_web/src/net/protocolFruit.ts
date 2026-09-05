/**
 * Pixi 娱乐「水果机」(WT kind=48) — 开奖/比大小算法在服务端。
 * subtypes: 1 status, 2 insert, 3 spin, 4 collect, 5 compare, 6 cashOut.
 */
import {
  SeqReader,
  WtBuilder,
  decodeTaggedNumber,
  decodeWtPacket,
  findField,
  type WtObject,
  type WtPacket,
} from "./wt";
import { COPPER_PER_GOLD } from "./protocolAuction";

function fieldU32(obj: WtObject, name: string): number {
  const raw = findField(obj, name);
  if (!raw) return 0;
  try {
    const n = decodeTaggedNumber(raw);
    return n == null ? 0 : n >>> 0;
  } catch {
    return 0;
  }
}

function fieldStr(obj: WtObject, name: string): string {
  const raw = findField(obj, name);
  if (!raw) return "";
  try {
    return new SeqReader(raw).string().replace(/\0+$/g, "");
  } catch {
    return "";
  }
}

export const FRUIT_WT_KIND = 48;
export const FRUIT_MIN_INSERT_GOLD = 1;
export const FRUIT_MAX_INSERT_GOLD = 999;
export const FRUIT_CREDITS_PER_GOLD = 10;
export const FRUIT_MULTIPLE_OPTIONS = [1, 5, 10, 50, 100] as const;

/** 1=BAR … 8=APPLE，与 luckyFruit signType 一致。 */
export const FRUIT_BET = {
  BAR: 1,
  SEVEN: 2,
  STAR: 3,
  WATERMELON: 4,
  BELL: 5,
  MANGO: 6,
  ORANGE: 7,
  APPLE: 8,
} as const;

export type FruitBetId = (typeof FRUIT_BET)[keyof typeof FRUIT_BET];

export const FRUIT_BET_OPTIONS: { id: FruitBetId; label: string; short: string }[] = [
  { id: FRUIT_BET.BAR, label: "BAR", short: "BAR" },
  { id: FRUIT_BET.SEVEN, label: "77", short: "77" },
  { id: FRUIT_BET.STAR, label: "星星", short: "星" },
  { id: FRUIT_BET.WATERMELON, label: "西瓜", short: "瓜" },
  { id: FRUIT_BET.BELL, label: "铃铛", short: "铃" },
  { id: FRUIT_BET.MANGO, label: "芒果", short: "芒" },
  { id: FRUIT_BET.ORANGE, label: "橙子", short: "橙" },
  { id: FRUIT_BET.APPLE, label: "苹果", short: "苹" },
];

/** 24 格轮盘展示名（与 luckyFruit typeDef 顺序一致）。 */
export const FRUIT_WHEEL_LABELS: string[] = [
  "LUCK",
  "苹果",
  "铃S",
  "橙",
  "铃",
  "BARs",
  "BAR",
  "苹果",
  "苹果S",
  "芒",
  "瓜",
  "瓜S",
  "LUCK",
  "苹果",
  "橙S",
  "橙",
  "铃",
  "77S",
  "77",
  "苹果",
  "芒S",
  "芒",
  "星",
  "星S",
];

/** 默认轮盘每格 fruitId（0=幸运, 1–8=水果）；与服务端默认 wheel 一致。 */
export const FRUIT_WHEEL_FRUIT_IDS: number[] = [
  0, 8, 5, 7, 5, 1, 1, 8, 8, 6, 4, 4, 0, 8, 7, 7, 5, 2, 2, 8, 6, 6, 3, 3,
];

export const FRUIT_COMPARE = {
  LOWER: 1,
  BIGGER: 2,
} as const;

export const FRUIT_CURRENCY = {
  GOLD: 0,
  KUBAO: 1,
} as const;

export type FruitStatus = {
  credits: number;
  winChips: number;
  goldJin: number;
  wcoin: number;
  copper: number;
  currencyType: number;
  enabled: boolean;
  showEntry: boolean;
  creditsPerGold: number;
  minInsertGold: number;
  maxInsertGold: number;
  hasWin: boolean;
};

export type FruitSpinResult = {
  result: number;
  message: string;
  triggerPos: number;
  cardGroupType: string;
  /** 中奖格下标 0–23 */
  cardGroup: number[];
  win: number;
  credits: number;
  winChips: number;
};

/** 牌型中文名（提示 / 日志）。 */
export const FRUIT_CARD_GROUP_LABELS: Record<string, string> = {
  SINGLE: "单奖",
  LUCK: "幸运",
  BONUS: "奖励",
  XSY: "小三元",
  DSY: "大三元",
  DSX: "大四喜",
  ZHSH: "纵横四海",
  XNSH: "仙女散花",
  CONTINUITY: "送灯",
  HC: "开火车",
  TLBB: "天龙八部",
  JLBD: "九莲宝灯",
  DMG: "大满贯",
};

export function fruitCardGroupLabel(type: string): string {
  return FRUIT_CARD_GROUP_LABELS[type] ?? type;
}

export function parseFruitCardGroup(raw: string): number[] {
  if (!raw) return [];
  const out: number[] = [];
  for (const part of raw.split(",")) {
    const n = Math.floor(Number(part.trim()));
    if (!Number.isFinite(n) || n < 0 || n > 23) continue;
    if (!out.includes(n)) out.push(n);
  }
  return out;
}

export type FruitOpResult = {
  result: number;
  message: string;
  credits?: number;
  winChips?: number;
  goldJin?: number;
  point?: number;
  compareType?: string;
};

export function fruitBetLabel(id: number): string {
  return FRUIT_BET_OPTIONS.find((o) => o.id === (id >>> 0))?.label ?? `?${id}`;
}

export function formatFruitGold(copper: number): string {
  const jin = Math.max(0, Math.floor((copper >>> 0) / COPPER_PER_GOLD));
  return `${jin}金`;
}

export function fruitWalletLabel(currencyType: number): string {
  return (currencyType >>> 0) === FRUIT_CURRENCY.KUBAO ? "酷宝" : "金";
}

export function fruitWalletUnits(st: FruitStatus | null): number {
  if (!st) return 0;
  return (st.currencyType >>> 0) === FRUIT_CURRENCY.KUBAO
    ? (st.wcoin >>> 0)
    : (st.goldJin >>> 0);
}

export function buildFruitStatusRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, FRUIT_WT_KIND, 1);
  return b.finish();
}

export function buildFruitInsertRequest(gold: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, FRUIT_WT_KIND, 2).u32("gold", gold >>> 0);
  return b.finish();
}

export function buildFruitSpinRequest(
  multiple: number,
  bets: Record<number, number>,
): Uint8Array {
  const b = new WtBuilder();
  const o = b.object(1, FRUIT_WT_KIND, 3);
  o.u32("multiple", multiple >>> 0);
  for (const opt of FRUIT_BET_OPTIONS) {
    o.u32(`bet${opt.id}`, (bets[opt.id] ?? 0) >>> 0);
  }
  return b.finish();
}

export function buildFruitCollectRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, FRUIT_WT_KIND, 4);
  return b.finish();
}

export function buildFruitCompareRequest(guess: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, FRUIT_WT_KIND, 5).u32("guess", guess >>> 0);
  return b.finish();
}

export function buildFruitCashOutRequest(gold = 0): Uint8Array {
  const b = new WtBuilder();
  const o = b.object(1, FRUIT_WT_KIND, 6);
  if ((gold >>> 0) > 0) o.u32("gold", gold >>> 0);
  return b.finish();
}

export function parseFruitStatus(packet: WtPacket): FruitStatus | null {
  const obj = packet.objects.find(
    (o) => o.kind === FRUIT_WT_KIND && o.subtype === 1,
  );
  if (!obj) return null;
  const copper = fieldU32(obj, "copper");
  const enabled = fieldU32(obj, "enabled") !== 0;
  const showEntryRaw = findField(obj, "showEntry");
  const showEntry = showEntryRaw
    ? fieldU32(obj, "showEntry") !== 0
    : enabled;
  return {
    credits: fieldU32(obj, "credits"),
    winChips: fieldU32(obj, "winChips"),
    goldJin: fieldU32(obj, "goldJin"),
    wcoin: fieldU32(obj, "wcoin"),
    copper,
    currencyType: fieldU32(obj, "currencyType"),
    enabled,
    showEntry,
    creditsPerGold: fieldU32(obj, "creditsPerGold") || FRUIT_CREDITS_PER_GOLD,
    minInsertGold: fieldU32(obj, "minInsertGold") || FRUIT_MIN_INSERT_GOLD,
    maxInsertGold: fieldU32(obj, "maxInsertGold") || FRUIT_MAX_INSERT_GOLD,
    hasWin: fieldU32(obj, "hasWin") !== 0,
  };
}

export function parseFruitSpinResult(packet: WtPacket): FruitSpinResult | null {
  const obj = packet.objects.find(
    (o) => o.kind === FRUIT_WT_KIND && o.subtype === 3,
  );
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    message: fieldStr(obj, "message"),
    triggerPos: fieldU32(obj, "triggerPos"),
    cardGroupType: fieldStr(obj, "cardGroupType") || "SINGLE",
    cardGroup: parseFruitCardGroup(fieldStr(obj, "cardGroup")),
    win: fieldU32(obj, "win"),
    credits: fieldU32(obj, "credits"),
    winChips: fieldU32(obj, "winChips"),
  };
}

export function parseFruitOpResult(
  packet: WtPacket,
  subtype: number,
): FruitOpResult | null {
  const obj = packet.objects.find(
    (o) => o.kind === FRUIT_WT_KIND && o.subtype === subtype,
  );
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    message: fieldStr(obj, "message"),
    credits: fieldU32(obj, "credits"),
    winChips: fieldU32(obj, "winChips"),
    goldJin: fieldU32(obj, "goldJin"),
    point: fieldU32(obj, "point"),
    compareType: fieldStr(obj, "compareType"),
  };
}
