/**
 * Pixi activities: WT46 festival/yanmen, WT47 ring quest.
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

export const ACTIVITIES_WT_KIND = 46;
export const RING_WT_KIND = 47;

export const NPC_FESTIVAL_EXCHANGE = 0xf7000001;
export const NPC_YANMEN_EXCHANGE = 0xf7000002;
export const NPC_RING_QUEST = 0xf7000003;

export type FestivalExchangeRow = {
  exchangeId: number;
  vitalityCost: number;
  itemId: number;
  itemCount: number;
  label: string;
};

export type ActivitiesStatus = {
  vitality: number;
  vitalityMax: number;
  woodCount: number;
  stoneCount: number;
  woodItemId: number;
  stoneItemId: number;
  festivalRows: FestivalExchangeRow[];
};

export type YanmenExchangeRow = {
  exchangeId: number;
  woodCost: number;
  stoneCost: number;
  itemId: number;
  itemCount: number;
  label: string;
};

export type RingStatus = {
  ringCount: number;
  dailyMax: number;
  bonusClaimed: boolean;
  taskType: number;
  targetId: number;
  targetCount: number;
  progress: number;
  targetNpc: string;
  desc: string;
  taskActive: boolean;
};

export type ActivityOpResult = {
  result: number;
  message: string;
};

function parseExchangeRows(raw: Uint8Array | undefined): FestivalExchangeRow[] {
  if (!raw) return [];
  const rows: FestivalExchangeRow[] = [];
  const r = new SeqReader(raw);
  while (r.remaining() > 16) {
    try {
      rows.push({
        exchangeId: r.u32(),
        vitalityCost: r.u32(),
        itemId: r.u32(),
        itemCount: r.u32(),
        label: r.string().replace(/\0+$/g, ""),
      });
    } catch {
      break;
    }
  }
  return rows;
}

function parseYanmenRows(raw: Uint8Array | undefined): YanmenExchangeRow[] {
  if (!raw) return [];
  const rows: YanmenExchangeRow[] = [];
  const r = new SeqReader(raw);
  while (r.remaining() > 16) {
    try {
      rows.push({
        exchangeId: r.u32(),
        woodCost: r.u32(),
        stoneCost: r.u32(),
        itemId: r.u32(),
        itemCount: r.u32(),
        label: r.string().replace(/\0+$/g, ""),
      });
    } catch {
      break;
    }
  }
  return rows;
}

export function buildActivitiesStatusRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ACTIVITIES_WT_KIND, 1);
  return b.finish();
}

export function buildFestivalExchangeRequest(exchangeId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ACTIVITIES_WT_KIND, 2).u32("exchangeId", exchangeId >>> 0);
  return b.finish();
}

export function buildYanmenListRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ACTIVITIES_WT_KIND, 3);
  return b.finish();
}

export function buildYanmenExchangeRequest(exchangeId: number): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ACTIVITIES_WT_KIND, 4).u32("exchangeId", exchangeId >>> 0);
  return b.finish();
}

export function buildRingStatusRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, RING_WT_KIND, 1);
  return b.finish();
}

export function buildRingAcceptRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, RING_WT_KIND, 2);
  return b.finish();
}

export function buildRingSubmitRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, RING_WT_KIND, 3);
  return b.finish();
}

export function buildRingClaimDailyRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, RING_WT_KIND, 4);
  return b.finish();
}

export function parseActivitiesStatus(packet: WtPacket): ActivitiesStatus | null {
  const obj = packet.objects.find(
    (o) => o.kind === ACTIVITIES_WT_KIND && o.subtype === 1,
  );
  if (!obj) return null;
  return {
    vitality: fieldU32(obj, "vitality"),
    vitalityMax: fieldU32(obj, "vitalityMax"),
    woodCount: fieldU32(obj, "woodCount"),
    stoneCount: fieldU32(obj, "stoneCount"),
    woodItemId: fieldU32(obj, "woodItemId"),
    stoneItemId: fieldU32(obj, "stoneItemId"),
    festivalRows: parseExchangeRows(findField(obj, "festivalList") ?? undefined),
  };
}

export function parseYanmenList(packet: WtPacket): YanmenExchangeRow[] {
  const obj = packet.objects.find(
    (o) => o.kind === ACTIVITIES_WT_KIND && o.subtype === 3,
  );
  if (!obj) return [];
  return parseYanmenRows(findField(obj, "yanmenList") ?? undefined);
}

export function parseActivityOp(packet: WtPacket, kind: number, subtype: number): ActivityOpResult | null {
  const obj = packet.objects.find((o) => o.kind === kind && o.subtype === subtype);
  if (!obj) return null;
  return {
    result: fieldU32(obj, "result"),
    message: fieldStr(obj, "message"),
  };
}

export function parseRingStatus(packet: WtPacket): RingStatus | null {
  const obj = packet.objects.find((o) => o.kind === RING_WT_KIND && o.subtype === 1);
  if (!obj) return null;
  return {
    ringCount: fieldU32(obj, "ringCount"),
    dailyMax: fieldU32(obj, "dailyMax"),
    bonusClaimed: fieldU32(obj, "bonusClaimed") !== 0,
    taskType: fieldU32(obj, "taskType"),
    targetId: fieldU32(obj, "targetId"),
    targetCount: fieldU32(obj, "targetCount"),
    progress: fieldU32(obj, "progress"),
    targetNpc: fieldStr(obj, "targetNpc"),
    desc: fieldStr(obj, "desc"),
    taskActive: fieldU32(obj, "taskActive") !== 0,
  };
}

export function activityMsgTip(code: string | undefined, ok: string): string {
  switch (code) {
    case "ok":
      return ok;
    case "novital":
      return "活力不足";
    case "nomat":
      return "材料不足";
    case "bagfull":
      return "背包空间不足";
    case "dailyfull":
      return "今日环数已满";
    case "noactive":
      return "没有进行中的环任务";
    case "notready":
      return "任务条件未满足";
    case "claimed":
      return "今日满环奖励已领取";
    case "notfull":
      return "未满20环";
    default:
      return code?.trim() || "操作失败";
  }
}
