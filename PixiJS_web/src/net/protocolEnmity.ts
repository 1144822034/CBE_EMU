/**
 * Pixi-only 恩怨 / 江湖追杀令 protocol (WT kind=43).
 * subtypes: 1 page (dir 0=我击杀, 1=击杀我), 2 hunt-order use.
 */
import {
  SeqReader,
  WtBuilder,
  decodeFieldString,
  decodeTaggedNumber,
  findField,
  type WtObject,
  type WtPacket,
} from "./wt";

export const ENMITY_WT_KIND = 43;
export const HUNT_ORDER_ITEM_ID = 847;
/** 与好友列表一屏行数对齐. */
export const ENMITY_PAGE_SIZE = 10;

export type EnmityRow = {
  id: number;
  name: string;
  online: boolean;
  kills: number;
};

export type EnmityPage = {
  dir: number;
  allPages: number;
  rows: EnmityRow[];
};

export type HuntOrderResult = {
  ok: boolean;
  remaining: number;
  tip: string;
};

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

export function buildEnmityPageRequest(
  dir: number,
  index = 0,
  pageSize = ENMITY_PAGE_SIZE,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ENMITY_WT_KIND, 1)
    .u8("dir", dir & 0xff)
    .u32("index", index >>> 0)
    .u8("pageSize", pageSize & 0xff);
  return b.finish();
}

export function buildHuntOrderUseRequest(
  seq: number,
  targetId: number,
  itemId = HUNT_ORDER_ITEM_ID,
): Uint8Array {
  const b = new WtBuilder();
  b.object(1, ENMITY_WT_KIND, 2)
    .u32("itemId", itemId >>> 0)
    .u16("seq", seq & 0xffff)
    .u32("targetId", targetId >>> 0);
  return b.finish();
}

export function parseEnmityPage(packet: WtPacket): EnmityPage | null {
  const obj = packet.objects.find(
    (o) => o.kind === ENMITY_WT_KIND && o.subtype === 1,
  );
  if (!obj) return null;
  const dir = fieldU32(obj, "dir");
  const allPages = fieldU32(obj, "allpgs") || 1;
  const raw = findField(obj, "enmityinfo");
  const rows: EnmityRow[] = [];
  if (raw && raw.length >= 4) {
    try {
      const r = new SeqReader(raw);
      const count = r.i16();
      for (let i = 0; i < count; i++) {
        const id = r.u32();
        const name = r.string().replace(/\0+$/g, "").trim() || "玩家";
        const online = r.u8() !== 0;
        const kills = r.u32() >>> 0;
        rows.push({ id, name, online, kills: kills || 1 });
      }
    } catch {
      /* truncated blob — keep what we have */
    }
  }
  return { dir, allPages, rows };
}

export function parseHuntOrderResult(packet: WtPacket): HuntOrderResult | null {
  const obj = packet.objects.find(
    (o) => o.kind === ENMITY_WT_KIND && o.subtype === 2,
  );
  if (!obj) return null;
  const tipField = findField(obj, "tip");
  let tip = "";
  if (tipField) {
    try {
      tip = decodeFieldString(tipField).replace(/\0+$/g, "").trim();
    } catch {
      tip = "";
    }
  }
  return {
    ok: fieldU32(obj, "result") === 1,
    remaining: fieldU32(obj, "remaining"),
    tip: tip || (fieldU32(obj, "result") === 1 ? "使用成功" : "使用失败"),
  };
}
