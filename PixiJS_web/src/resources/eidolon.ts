import { decodeGbk, readU32LE } from "./binary";
import { fetchResourceBytes } from "./loader";

/** eidolon.dsh: 序列号 → 精灵名字 (plain DSH, not LZSS-wrapped). */
export type EidolonTable = Map<number, string>;

let cached: EidolonTable | null = null;

function parseEidolonDsh(raw: Uint8Array): EidolonTable {
  const map = new Map<number, string>();
  if (raw.length < 16) return map;
  const cols = readU32LE(raw, 4);
  const rows = readU32LE(raw, 8);
  if (cols < 2 || cols > 16 || rows > 1024) return map;
  let pos = 16;
  for (let i = 0; i < cols; i++) {
    if (pos >= raw.length) return map;
    const fl = raw[pos++]!;
    pos += fl;
  }
  for (let row = 0; row < rows && pos + 4 <= raw.length; row++) {
    const rowLen = readU32LE(raw, pos);
    let rp = pos + 4;
    const end = rp + rowLen;
    if (end > raw.length) break;
    let seq = -1;
    let actor = "";
    for (let c = 0; c < cols && rp < end; c++) {
      const vl = raw[rp++]!;
      const cell = raw.subarray(rp, rp + vl);
      rp += vl;
      if (c === 0) {
        const s = decodeGbk(cell).trim();
        if (s) seq = Number(s) | 0;
      } else if (c === 1) {
        actor = decodeGbk(cell).replace(/\0+$/, "").trim();
      }
    }
    if (seq >= 0 && actor.toLowerCase().endsWith(".actor")) {
      map.set(seq, actor);
    }
    pos = end;
  }
  return map;
}

export async function loadEidolonTable(): Promise<EidolonTable> {
  if (cached) return cached;
  const raw = await fetchResourceBytes("eidolon.dsh");
  cached = parseEidolonDsh(raw);
  return cached;
}

export async function eidolonActorForIndex(index: number): Promise<string | null> {
  const table = await loadEidolonTable();
  return table.get(index) ?? null;
}
