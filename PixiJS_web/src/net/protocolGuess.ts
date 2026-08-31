/**
 * Pixi 娱乐「猜猜看」(WT kind=45).
 * subtypes: 1 status, 2 bet, 3 history, 4 claim.
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

export const GUESS_WT_KIND = 45;
export const GUESS_PAGE_SIZE = 8;
export const GUESS_MIN_GOLD = 1;
export const GUESS_MAX_GOLD = 999;
export const GUESS_FEE_PERCENT = 8;

export const GUESS_CHOICE = {
  BIG: 1,
  SMALL: 2,
  ODD: 3,
  EVEN: 4,
  BAOZI: 5,
} as const;

export type GuessChoice = (typeof GUESS_CHOICE)[keyof typeof GUESS_CHOICE];

export type GuessStatus = {
  enabled: boolean;
  roundId: number;
  remainSec: number;
  lockRemainSec: number;
  bettingOpen: boolean;
  dice1: number;
  dice2: number;
  dice3: number;
  roundDrawn: boolean;
  pendingClaims: number;
  feePercent: number;
  minGold: number;
  maxGold: number;
};

export type GuessHistoryRow = {
  betId: number;
  roundId: number;
  choice: number;
  stakeCopper: number;
  dice1: number;
  dice2: number;
  dice3: number;
  win: boolean;
  payoutCopper: number;
  claimed: boolean;
  roundDrawn: boolean;
};

export type GuessOpResult = {
  result: number;
  betId: number;
  message: string;
  claimed?: number;
  pendingClaims?: number;
};

export function guessChoiceLabel(choice: number): string {
  switch (choice >>> 0) {
    case GUESS_CHOICE.BIG:
      return "大";
    case GUESS_CHOICE.SMALL:
      return "小";
    case GUESS_CHOICE.ODD:
      return "单";
    case GUESS_CHOICE.EVEN:
      return "双";
    case GUESS_CHOICE.BAOZI:
      return "豹子";
    default:
      return `?${choice}`;
  }
}

export function formatGuessStakeGold(copper: number): string {
  const jin = Math.max(0, Math.floor((copper >>> 0) / COPPER_PER_GOLD));
  return `${jin}金`;
}

export function formatGuessDice(d1: number, d2: number, d3: number): string {
  if (!d1 && !d2 && !d3) return "待开奖";
  return `${d1},${d2},${d3}`;
}

/** 三骰开奖对应 大/小/单/双/豹子（与服务端判定一致）. */
export function guessDiceOutcomeHints(d1: number, d2: number, d3: number): string {
  const a = d1 >>> 0;
  const b = d2 >>> 0;
  const c = d3 >>> 0;
  if (!a && !b && !c) return "";
  if (a === b && b === c) return "豹子";
  const sum = a + b + c;
  const tags: string[] = [];
  if (sum >= 11 && sum <= 17) tags.push("大");
  else if (sum >= 4 && sum <= 10) tags.push("小");
  if ((sum & 1) !== 0) tags.push("单");
  else tags.push("双");
  return tags.join("");
}

export function formatGuessDiceWithHints(d1: number, d2: number, d3: number): string {
  if (!d1 && !d2 && !d3) return "待开";
  const dice = formatGuessDice(d1, d2, d3);
  const hints = guessDiceOutcomeHints(d1, d2, d3);
  return hints ? `${dice}(${hints})` : dice;
}

export function buildGuessStatusRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, GUESS_WT_KIND, 1);
  return b.finish();
}

export function buildGuessBetRequest(
  choice: number,
  stakeGold: number,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, GUESS_WT_KIND, 2)
    .u32("choice", choice >>> 0)
    .u32("stakeGold", stakeGold >>> 0);
  return b.finish();
}

export function buildGuessHistoryRequest(pageIndex = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, GUESS_WT_KIND, 3).u32("pageIndex", pageIndex >>> 0);
  return b.finish();
}

export function buildGuessClaimRequest(betId = 0): Uint8Array {
  const b = new WtBuilder();
  b.object(1, GUESS_WT_KIND, 4).u32("betId", betId >>> 0);
  return b.finish();
}

export function parseGuessStatus(packet: WtPacket): GuessStatus | null {
  const obj = packet.objects.find(
    (o) => o.kind === GUESS_WT_KIND && o.subtype === 1,
  );
  if (!obj) return null;
  return {
    enabled: fieldU32(obj, "enabled") !== 0,
    roundId: fieldU32(obj, "roundId"),
    remainSec: fieldU32(obj, "remainSec"),
    lockRemainSec: fieldU32(obj, "lockRemainSec"),
    bettingOpen: fieldU32(obj, "bettingOpen") !== 0,
    dice1: fieldU32(obj, "dice1"),
    dice2: fieldU32(obj, "dice2"),
    dice3: fieldU32(obj, "dice3"),
    roundDrawn: fieldU32(obj, "roundDrawn") !== 0,
    pendingClaims: fieldU32(obj, "pendingClaims"),
    feePercent: fieldU32(obj, "feePercent") || GUESS_FEE_PERCENT,
    minGold: fieldU32(obj, "minGold") || GUESS_MIN_GOLD,
    maxGold: fieldU32(obj, "maxGold") || GUESS_MAX_GOLD,
  };
}

function parseHistoryRows(obj: WtObject): GuessHistoryRow[] {
  const raw = findField(obj, "rows");
  if (!raw) return [];
  const rows: GuessHistoryRow[] = [];
  try {
    const r = new SeqReader(raw);
    while (r.remaining() > 0) {
      rows.push({
        betId: r.u32(),
        roundId: r.u32(),
        choice: r.u32(),
        stakeCopper: r.u32(),
        dice1: r.u32(),
        dice2: r.u32(),
        dice3: r.u32(),
        win: r.u32() !== 0,
        payoutCopper: r.u32(),
        claimed: r.u32() !== 0,
        roundDrawn: r.u32() !== 0,
      });
    }
  } catch {
    /* ignore malformed */
  }
  return rows;
}

export function parseGuessHistory(packet: WtPacket): {
  result: number;
  rows: GuessHistoryRow[];
  pageIndex: number;
  pageSize: number;
  hasMore: boolean;
  pendingClaims: number;
} | null {
  const obj = packet.objects.find(
    (o) => o.kind === GUESS_WT_KIND && o.subtype === 3,
  );
  if (!obj) return null;
  const pageSize = fieldU32(obj, "pageSize") || GUESS_PAGE_SIZE;
  const rows = parseHistoryRows(obj);
  const hasMoreField = findField(obj, "hasMore");
  const hasMore =
    hasMoreField != null
      ? fieldU32(obj, "hasMore") !== 0
      : rows.length >= pageSize;
  return {
    result: fieldU32(obj, "result"),
    rows,
    pageIndex: fieldU32(obj, "pageIndex"),
    pageSize,
    hasMore,
    pendingClaims: fieldU32(obj, "pendingClaims"),
  };
}

export function parseGuessOpResult(
  packet: WtPacket,
  subtype: number,
): GuessOpResult | null {
  const obj = packet.objects.find(
    (o) => o.kind === GUESS_WT_KIND && o.subtype === subtype,
  );
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    betId: fieldU32(obj, "betId"),
    message: fieldStr(obj, "message"),
    claimed: fieldU32(obj, "claimed"),
    pendingClaims: fieldU32(obj, "pendingClaims"),
  };
}

export function formatGuessRemain(sec: number): string {
  const s = Math.max(0, sec | 0);
  const m = Math.floor(s / 60);
  const r = s % 60;
  if (m <= 0) return `${r}秒`;
  return `${m}分${r}秒`;
}
