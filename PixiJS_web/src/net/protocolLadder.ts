/**
 * Pixi ladder challenge (WT kind=44).
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

export const LADDER_WT_KIND = 44;

export type LadderStatus = {
  enabled: boolean;
  dailyLimit: number;
  dailyUsed: number;
  bestFloor: number;
  nextFloor: number;
};

export function buildLadderStatusRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, LADDER_WT_KIND, 1);
  return b.finish();
}

export function buildLadderStartRequest(): Uint8Array {
  const b = new WtBuilder();
  b.object(1, LADDER_WT_KIND, 2);
  return b.finish();
}

export function parseLadderStatus(packet: WtPacket): LadderStatus | null {
  const obj = packet.objects.find(
    (o) => o.kind === LADDER_WT_KIND && o.subtype === 1,
  );
  if (!obj) return null;
  return {
    enabled: fieldU32(obj, "enabled") !== 0,
    dailyLimit: fieldU32(obj, "dailyLimit"),
    dailyUsed: fieldU32(obj, "dailyUsed"),
    bestFloor: fieldU32(obj, "bestFloor"),
    nextFloor: fieldU32(obj, "nextFloor"),
  };
}

export function parseLadderStartResult(packet: WtPacket): {
  ok: boolean;
  tip: string;
  floor: number;
} {
  const obj = packet.objects.find(
    (o) => o.kind === LADDER_WT_KIND && o.subtype === 2,
  );
  if (!obj) return { ok: false, tip: "", floor: 0 };
  const tipField = findField(obj, "tip");
  let tip = "";
  if (tipField) {
    try {
      const r = new SeqReader(tipField);
      tip = r.string();
    } catch {
      tip = "";
    }
  }
  return {
    ok: fieldU32(obj, "result") === 1,
    tip,
    floor: fieldU32(obj, "floor"),
  };
}

export function ladderObjectsFromBodies(bodies: Uint8Array[]): WtPacket[] {
  const out: WtPacket[] = [];
  for (const body of bodies) {
    try {
      out.push(decodeWtPacket(body));
    } catch {
      /* skip */
    }
  }
  return out;
}
